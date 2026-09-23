// =================================================================
// src/core/session/media/MediaPeer.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
// See MediaPeer.h for the ownership boundary.
//
// =================================================================

#include "core/session/media/MediaPeer.h"

#include "core/session/media/LibDataChannelMediaTransport.h"

#include <QCryptographicHash>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QUuid>

#include <initializer_list>
#include <utility>

namespace NereusSDR {

namespace {

constexpr char kDescriptionOp[] = "description";
constexpr char kCandidateOp[] = "candidate";

struct PendingCandidate {
    QString candidate;
    QString mid;
};

bool hasExactKeys(const QJsonObject& object,
                  const std::initializer_list<QString>& keys)
{
    if (object.size() != static_cast<qsizetype>(keys.size())) {
        return false;
    }
    for (const QString& key : keys) {
        if (!object.contains(key)) {
            return false;
        }
    }
    return true;
}

bool isCanonicalConnectionId(const QString& connectionId)
{
    if (connectionId.size() != 36) {
        return false;
    }
    const QUuid uuid = QUuid::fromString(connectionId);
    return !uuid.isNull()
        && uuid.toString(QUuid::WithoutBraces) == connectionId;
}

bool isBoundedString(const QJsonValue& value, qsizetype maxBytes,
                     bool allowEmpty = false)
{
    if (!value.isString()) {
        return false;
    }
    const QByteArray bytes = value.toString().toUtf8();
    return (allowEmpty || !bytes.isEmpty())
        && bytes.size() <= maxBytes && !bytes.contains('\0');
}

bool isDescriptionType(const QString& type)
{
    return type == QLatin1String("offer") || type == QLatin1String("answer");
}

quint32 audioSsrcForConnection(const QString& connectionId)
{
    QByteArray identity("NereusSDR/media-audio-ssrc/v1:");
    identity.append(connectionId.toUtf8());
    const QByteArray digest = QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256);
    const auto* bytes = reinterpret_cast<const unsigned char*>(
        digest.constData());
    const quint32 derived = (static_cast<quint32>(bytes[0]) << 24)
        | (static_cast<quint32>(bytes[1]) << 16)
        | (static_cast<quint32>(bytes[2]) << 8)
        | static_cast<quint32>(bytes[3]);
    return derived == 0 ? 1 : derived;
}

quint32 rtpSsrc(const QByteArray& packet)
{
    return (static_cast<quint32>(static_cast<quint8>(packet.at(8))) << 24)
        | (static_cast<quint32>(static_cast<quint8>(packet.at(9))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(packet.at(10))) << 8)
        | static_cast<quint32>(static_cast<quint8>(packet.at(11)));
}

} // namespace

struct MediaPeer::Private {
    TransportFactory factory;
    QPointer<IMediaTransport> transport;
    QList<PendingCandidate> pendingCandidates;
    QString connectionId;
    IMediaTransport::Role role = IMediaTransport::Role::Answerer;
    quint32 audioSsrc = 0;
    quint64 generation = 0;
    int remoteCandidateControls = 0;
    int localCandidateControls = 0;
    bool started = false;
    bool remoteDescriptionAccepted = false;
    bool ready = false;
};

MediaPeer::MediaPeer(QObject* parent, TransportFactory factory)
    : QObject(parent)
    , d(new Private)
{
    d->factory = std::move(factory);
    if (!d->factory) {
        d->factory = [](QObject* owner) -> IMediaTransport* {
            return new LibDataChannelMediaTransport(owner);
        };
    }
}

MediaPeer::~MediaPeer()
{
    stopInternal(false);
    delete d;
}

bool MediaPeer::start(IMediaTransport::Role role, const QString& connectionId)
{
    if (d->started || !isCanonicalConnectionId(connectionId)) {
        return false;
    }

    IMediaTransport* transport = nullptr;
    try {
        transport = d->factory(this);
    } catch (...) {
        emit errorOccurred(QStringLiteral("media transport factory failed"));
        return false;
    }
    if (!transport || transport->thread() != thread()) {
        if (transport && !transport->parent()) {
            transport->deleteLater();
        }
        emit errorOccurred(QStringLiteral("media transport factory returned an invalid object"));
        return false;
    }
    if (transport->parent() != this) {
        transport->setParent(this);
    }

    ++d->generation;
    const quint64 generation = d->generation;
    d->transport = transport;
    d->connectionId = connectionId;
    d->role = role;
    d->audioSsrc = audioSsrcForConnection(connectionId);
    d->remoteCandidateControls = 0;
    d->localCandidateControls = 0;
    d->remoteDescriptionAccepted = false;
    d->pendingCandidates.clear();
    d->ready = false;
    d->started = true;

    QPointer<MediaPeer> self(this);
    QPointer<IMediaTransport> guardedTransport(transport);
    const auto isCurrentGeneration = [self, guardedTransport, generation] {
        return self && guardedTransport
            && self->isCurrent(guardedTransport, generation);
    };

    connect(transport, &IMediaTransport::localDescription, this,
            [self, isCurrentGeneration]
            (const QString& sdp, const QString& type) {
                if (!isCurrentGeneration()) {
                    return;
                }
                const QByteArray sdpBytes = sdp.toUtf8();
                const QString expectedType = self->d->role
                    == IMediaTransport::Role::Offerer
                    ? QStringLiteral("offer") : QStringLiteral("answer");
                if (sdpBytes.isEmpty()
                    || sdpBytes.size() > IMediaTransport::kMaxDescriptionBytes
                    || sdpBytes.contains('\0') || type != expectedType) {
                    emit self->errorOccurred(
                        QStringLiteral("invalid local media description rejected"));
                    return;
                }
                QJsonObject control{
                    {QStringLiteral("op"), QLatin1String(kDescriptionOp)},
                    {QStringLiteral("connectionId"), self->d->connectionId},
                    {QStringLiteral("sdp"), sdp},
                    {QStringLiteral("type"), type},
                };
                emit self->controlReady(control);
            });
    connect(transport, &IMediaTransport::localCandidate, this,
            [self, isCurrentGeneration]
            (const QString& candidate, const QString& mid) {
                if (!isCurrentGeneration()) {
                    return;
                }
                const QByteArray candidateBytes = candidate.toUtf8();
                const QByteArray midBytes = mid.toUtf8();
                if (candidateBytes.isEmpty()
                    || candidateBytes.size() > IMediaTransport::kMaxCandidateBytes
                    || midBytes.isEmpty()
                    || midBytes.size() > IMediaTransport::kMaxCandidateMidBytes
                    || candidateBytes.contains('\0') || midBytes.contains('\0')
                    || self->d->localCandidateControls
                        >= IMediaTransport::kMaxRemoteCandidates) {
                    emit self->errorOccurred(
                        QStringLiteral("invalid local media candidate rejected"));
                    return;
                }
                ++self->d->localCandidateControls;
                QJsonObject control{
                    {QStringLiteral("op"), QLatin1String(kCandidateOp)},
                    {QStringLiteral("connectionId"), self->d->connectionId},
                    {QStringLiteral("candidate"), candidate},
                    {QStringLiteral("mid"), mid},
                };
                emit self->controlReady(control);
            });
    connect(transport, &IMediaTransport::displayReceived, this,
            [self, isCurrentGeneration](const QByteArray& message) {
                if (!isCurrentGeneration()) {
                    return;
                }
                if (message.isEmpty()
                    || message.size() > IMediaTransport::kMaxDisplayMessageBytes) {
                    emit self->errorOccurred(
                        QStringLiteral("invalid display message rejected"));
                    return;
                }
                emit self->displayReceived(message);
            });
    connect(transport, &IMediaTransport::rtpReceived, this,
            [self, isCurrentGeneration](const QByteArray& packet) {
                if (!isCurrentGeneration()) {
                    return;
                }
                if (packet.size() < IMediaTransport::kMinRawRtpBytes
                    || packet.size() > IMediaTransport::kMaxRawRtpBytes
                    || rtpSsrc(packet) != self->d->audioSsrc) {
                    emit self->errorOccurred(
                        QStringLiteral("invalid raw RTP packet rejected"));
                    return;
                }
                emit self->rtpReceived(packet);
            });
    connect(transport, &IMediaTransport::ready, this,
            [self, isCurrentGeneration] {
                if (!isCurrentGeneration() || self->d->ready) {
                    return;
                }
                self->d->ready = true;
                emit self->ready();
            });
    connect(transport, &IMediaTransport::closed, this,
            [self, isCurrentGeneration] {
                if (isCurrentGeneration()) {
                    self->stop();
                }
            });
    connect(transport, &IMediaTransport::connectionFailed, this,
            [self, isCurrentGeneration](const QString& message) {
                if (isCurrentGeneration()) {
                    emit self->connectionFailed(message);
                }
            });
    connect(transport, &IMediaTransport::errorOccurred, this,
            [self, isCurrentGeneration](const QString& message) {
                if (isCurrentGeneration()) {
                    emit self->errorOccurred(message);
                }
            });
    connect(transport, &IMediaTransport::displayErrorOccurred, this,
            [self, isCurrentGeneration](const QString& message) {
                if (isCurrentGeneration()) {
                    emit self->displayErrorOccurred(message);
                }
            });
    connect(transport, &IMediaTransport::displayWritable, this,
            [self, isCurrentGeneration] {
                if (isCurrentGeneration()) {
                    emit self->displayWritable();
                }
            });

    const bool backendStarted = transport->start({role, d->audioSsrc});
    if (!self || !self->isCurrent(transport, generation)) {
        return false;
    }
    if (!backendStarted) {
        stopInternal(false);
        return false;
    }
    return true;
}

void MediaPeer::stop()
{
    stopInternal(true);
}

void MediaPeer::stopInternal(bool notify)
{
    if (!d->started && !d->transport) {
        return;
    }

    const bool wasStarted = d->started;
    ++d->generation;
    d->started = false;
    d->ready = false;
    d->remoteDescriptionAccepted = false;
    d->remoteCandidateControls = 0;
    d->localCandidateControls = 0;
    d->pendingCandidates.clear();
    d->connectionId.clear();
    d->audioSsrc = 0;

    QPointer<IMediaTransport> transport = d->transport;
    d->transport.clear();
    if (transport) {
        disconnect(transport, nullptr, this, nullptr);
        transport->stop();
        transport->deleteLater();
    }

    if (notify && wasStarted) {
        emit closed();
    }
}

bool MediaPeer::acceptControl(const QJsonObject& control)
{
    if (!d->started || !d->transport
        || !control.value(QStringLiteral("op")).isString()
        || !control.value(QStringLiteral("connectionId")).isString()
        || control.value(QStringLiteral("connectionId")).toString()
            != d->connectionId) {
        return false;
    }

    const QString op = control.value(QStringLiteral("op")).toString();
    if (op == QLatin1String(kDescriptionOp)) {
        const QString expectedType = d->role == IMediaTransport::Role::Offerer
            ? QStringLiteral("answer") : QStringLiteral("offer");
        if (!hasExactKeys(control,
                          {QStringLiteral("op"), QStringLiteral("connectionId"),
                           QStringLiteral("sdp"), QStringLiteral("type")})
            || d->remoteDescriptionAccepted
            || !isBoundedString(control.value(QStringLiteral("sdp")),
                                IMediaTransport::kMaxDescriptionBytes)
            || !isBoundedString(control.value(QStringLiteral("type")), 6)
            || !isDescriptionType(
                control.value(QStringLiteral("type")).toString())
            || control.value(QStringLiteral("type")).toString()
                != expectedType) {
            return false;
        }

        const quint64 generation = d->generation;
        QPointer<MediaPeer> self(this);
        IMediaTransport* transport = d->transport;
        const bool accepted = transport->acceptDescription(
            control.value(QStringLiteral("sdp")).toString(),
            control.value(QStringLiteral("type")).toString());
        if (!self || !self->isCurrent(transport, generation) || !accepted) {
            return false;
        }
        d->remoteDescriptionAccepted = true;

        const QList<PendingCandidate> pending = std::move(d->pendingCandidates);
        for (const PendingCandidate& candidate : pending) {
            if (!transport->acceptCandidate(candidate.candidate, candidate.mid)) {
                if (self && self->isCurrent(transport, generation)) {
                    emit self->errorOccurred(
                        QStringLiteral("buffered media candidate rejected"));
                }
                return false;
            }
            if (!self || !self->isCurrent(transport, generation)) {
                return false;
            }
        }
        return true;
    }

    if (op == QLatin1String(kCandidateOp)) {
        if (!hasExactKeys(control,
                          {QStringLiteral("op"), QStringLiteral("connectionId"),
                           QStringLiteral("candidate"), QStringLiteral("mid")})
            || !isBoundedString(control.value(QStringLiteral("candidate")),
                                IMediaTransport::kMaxCandidateBytes)
            || !isBoundedString(control.value(QStringLiteral("mid")),
                                IMediaTransport::kMaxCandidateMidBytes)
            || d->remoteCandidateControls
                >= IMediaTransport::kMaxRemoteCandidates) {
            return false;
        }

        ++d->remoteCandidateControls;
        PendingCandidate candidate{
            control.value(QStringLiteral("candidate")).toString(),
            control.value(QStringLiteral("mid")).toString(),
        };
        if (!d->remoteDescriptionAccepted) {
            d->pendingCandidates.push_back(std::move(candidate));
            return true;
        }
        const quint64 generation = d->generation;
        QPointer<MediaPeer> self(this);
        IMediaTransport* transport = d->transport;
        const bool accepted = transport->acceptCandidate(candidate.candidate,
                                                          candidate.mid);
        return self && self->isCurrent(transport, generation) && accepted;
    }

    return false;
}

bool MediaPeer::sendDisplay(const QByteArray& message)
{
    return d->started && d->transport && !message.isEmpty()
        && message.size() <= IMediaTransport::kMaxDisplayMessageBytes
        && d->transport->sendDisplay(message);
}

IMediaTransport::DisplaySendResult MediaPeer::submitDisplay(const QByteArray& message)
{
    if (!d->started || !d->transport || message.isEmpty()
        || message.size() > IMediaTransport::kMaxDisplayMessageBytes) {
        return IMediaTransport::DisplaySendResult::Refused;
    }
    return d->transport->submitDisplay(message);
}

bool MediaPeer::displayBusy() const
{
    return d->started && d->transport && d->transport->displayBusy();
}

bool MediaPeer::sendRtp(const QByteArray& packet)
{
    return d->started && d->transport
        && packet.size() >= IMediaTransport::kMinRawRtpBytes
        && packet.size() <= IMediaTransport::kMaxRawRtpBytes
        && rtpSsrc(packet) == d->audioSsrc
        && d->transport->sendRtp(packet);
}

bool MediaPeer::isReady() const
{
    return d->started && d->ready && d->transport
        && d->transport->isReady();
}

QString MediaPeer::connectionId() const
{
    return d->connectionId;
}

quint32 MediaPeer::audioSsrc() const
{
    return d->audioSsrc;
}

std::optional<MediaPeerTelemetry> MediaPeer::telemetry() const
{
    if (!d->started || !d->transport) {
        return std::nullopt;
    }
    IMediaTransport* const transport = d->transport.data();
    const quint64 generation = d->generation;
    const std::optional<MediaTransportTelemetry> traffic = transport->telemetry();
    if (!traffic || !isCurrent(transport, generation)) {
        return std::nullopt;
    }
    return MediaPeerTelemetry{generation, *traffic};
}

bool MediaPeer::isCurrent(const IMediaTransport* transport,
                          quint64 generation) const
{
    return d->started && d->transport == transport
        && d->generation == generation;
}

} // namespace NereusSDR
