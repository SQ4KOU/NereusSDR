// =================================================================
// tests/tst_media_peer.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// =================================================================

#include "core/session/media/MediaPeer.h"

#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <stdexcept>
#include <thread>

using namespace NereusSDR;

namespace {

constexpr char kConnectionA[] = "11111111-2222-4333-8444-555555555555";
constexpr char kConnectionB[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

QJsonObject descriptionControl(const QString& connectionId,
                               const QString& sdp = QStringLiteral("v=0"),
                               const QString& type = QStringLiteral("offer"))
{
    return {
        {QStringLiteral("op"), QStringLiteral("description")},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("sdp"), sdp},
        {QStringLiteral("type"), type},
    };
}

QJsonObject candidateControl(const QString& connectionId,
                             const QString& candidate,
                             const QString& mid = QStringLiteral("0"))
{
    return {
        {QStringLiteral("op"), QStringLiteral("candidate")},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("candidate"), candidate},
        {QStringLiteral("mid"), mid},
    };
}

QByteArray rtpPacket(quint16 sequence, quint32 ssrc)
{
    QByteArray packet(15, '\0');
    packet[0] = char(0x80);
    packet[1] = char(111);
    packet[2] = char(sequence >> 8);
    packet[3] = char(sequence & 0xff);
    packet[8] = char(ssrc >> 24);
    packet[9] = char(ssrc >> 16);
    packet[10] = char(ssrc >> 8);
    packet[11] = char(ssrc);
    packet[12] = char(0xf8);
    packet[13] = char(0xff);
    packet[14] = char(0xfe);
    return packet;
}

class FakeTransport : public IMediaTransport {
public:
    explicit FakeTransport(QObject* parent = nullptr) : IMediaTransport(parent) {}

    bool start(const StartOptions& options) override
    {
        started = startSucceeds;
        startedRole = options.role;
        startedSsrc = options.localAudioSsrc;
        return started;
    }

    void stop() override
    {
        started = false;
        readyState = false;
    }

    bool acceptDescription(const QString& sdp, const QString& type) override
    {
        descriptions.push_back({sdp, type});
        return acceptDescriptionSucceeds;
    }

    bool acceptCandidate(const QString& candidate, const QString& mid) override
    {
        candidates.push_back({candidate, mid});
        return acceptCandidateSucceeds;
    }

    bool sendDisplay(const QByteArray& message) override
    {
        sentDisplay.push_back(message);
        return readyState;
    }

    bool sendRtp(const QByteArray& packet) override
    {
        sentRtp.push_back(packet);
        return readyState;
    }

    bool isReady() const override { return readyState; }
    void fireLocalDescription(const QString& sdp, const QString& type)
    {
        emit localDescription(sdp, type);
    }

    void fireLocalCandidate(const QString& candidate, const QString& mid)
    {
        emit localCandidate(candidate, mid);
    }

    void fireDisplay(const QByteArray& message) { emit displayReceived(message); }
    void fireRtp(const QByteArray& packet) { emit rtpReceived(packet); }
    void fireConnectionFailed(const QString& reason) { emit connectionFailed(reason); }
    void fireGenericError(const QString& reason) { emit errorOccurred(reason); }

    void fireReady()
    {
        readyState = true;
        emit ready();
    }

    bool startSucceeds = true;
    bool acceptDescriptionSucceeds = true;
    bool acceptCandidateSucceeds = true;
    bool started = false;
    bool readyState = false;
    Role startedRole = Role::Answerer;
    quint32 startedSsrc = 0;
    QList<QPair<QString, QString>> descriptions;
    QList<QPair<QString, QString>> candidates;
    QList<QByteArray> sentDisplay;
    QList<QByteArray> sentRtp;
};

class ObservableFakeTransport final : public FakeTransport {
public:
    using FakeTransport::FakeTransport;

    std::optional<MediaTransportTelemetry> telemetry() const override
    {
        return observedTelemetry;
    }

    std::optional<MediaTransportTelemetry> observedTelemetry;
};

} // namespace

class TestMediaPeer : public QObject {
    Q_OBJECT

private slots:
    void strictControlBuffersCandidatesAndEnforcesCap();
    void outboundControlIsScopedAndBounded();
    void oldQueuedCallbacksCannotEnterNewGeneration();
    void terminalConnectionFailureIsTypedAndGenerationScoped();
    void telemetryDefaultsUnsupportedAndRejectsStaleGeneration();
    void signalHandlersMayRestartOrDeletePeer();
    void realPeersExchangeQueuedControlAndDirectMedia();
    void startRefusalsAreTyped();
};

void TestMediaPeer::terminalConnectionFailureIsTypedAndGenerationScoped()
{
    QList<QPointer<FakeTransport>> transports;
    MediaPeer peer(nullptr, [&transports](QObject* parent) -> IMediaTransport* {
        auto* transport = new FakeTransport(parent);
        transports.push_back(transport);
        return transport;
    });
    QSignalSpy failures(&peer, &MediaPeer::connectionFailed);
    QSignalSpy errors(&peer, &MediaPeer::errorOccurred);

    QVERIFY(peer.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QVERIFY(transports.constLast());
    transports.constLast()->fireGenericError(QStringLiteral("invalid media packet"));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(failures.size(), 0);

    transports.constLast()->fireConnectionFailed(QStringLiteral("media peer connection failed"));
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.constFirst().constFirst().toString(),
             QStringLiteral("media peer connection failed"));
    QCOMPARE(errors.size(), 1);

    QPointer<FakeTransport> stale = transports.constLast();
    peer.stop();
    QVERIFY(peer.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionB)));
    QVERIFY(stale);
    stale->fireConnectionFailed(QStringLiteral("stale peer failed"));
    QCOMPARE(failures.size(), 1);
}

void TestMediaPeer::telemetryDefaultsUnsupportedAndRejectsStaleGeneration()
{
    QPointer<FakeTransport> unsupported;
    MediaPeer unsupportedPeer(
        nullptr, [&unsupported](QObject* parent) -> IMediaTransport* {
            unsupported = new FakeTransport(parent);
            return unsupported;
        });
    QVERIFY(unsupportedPeer.start(IMediaTransport::Role::Answerer,
                                  QLatin1String(kConnectionA)));
    QVERIFY(unsupported);
    QVERIFY(!unsupportedPeer.telemetry().has_value());

    QList<QPointer<ObservableFakeTransport>> transports;
    MediaPeer peer(nullptr, [&transports](QObject* parent) -> IMediaTransport* {
        auto* transport = new ObservableFakeTransport(parent);
        transports.push_back(transport);
        return transport;
    });

    QVERIFY(peer.start(IMediaTransport::Role::Answerer,
                       QLatin1String(kConnectionA)));
    ObservableFakeTransport* first = transports.constLast();
    QVERIFY(!peer.telemetry().has_value());
    first->observedTelemetry = MediaTransportTelemetry{11, 12, 13, 14};
    const auto firstSnapshot = peer.telemetry();
    QVERIFY(firstSnapshot.has_value());
    QVERIFY(firstSnapshot->generation != 0);
    QCOMPARE(firstSnapshot->traffic.receivedDisplayPayloadBytes, quint64(11));
    QCOMPARE(firstSnapshot->traffic.submittedDisplayPayloadBytes, quint64(12));
    QCOMPARE(firstSnapshot->traffic.receivedRtpBytes, quint64(13));
    QCOMPARE(firstSnapshot->traffic.submittedRtpBytes, quint64(14));

    peer.stop();
    QVERIFY(!peer.telemetry().has_value());
    QVERIFY(peer.start(IMediaTransport::Role::Answerer,
                       QLatin1String(kConnectionB)));
    ObservableFakeTransport* second = transports.constLast();
    QVERIFY(second != first);
    first->observedTelemetry = MediaTransportTelemetry{91, 92, 93, 94};
    QVERIFY(!peer.telemetry().has_value());

    second->observedTelemetry = MediaTransportTelemetry{21, 22, 23, 24};
    const auto secondSnapshot = peer.telemetry();
    QVERIFY(secondSnapshot.has_value());
    QVERIFY(secondSnapshot->generation != firstSnapshot->generation);
    QCOMPARE(secondSnapshot->traffic.receivedDisplayPayloadBytes, quint64(21));
    QCOMPARE(secondSnapshot->traffic.submittedDisplayPayloadBytes, quint64(22));
    QCOMPARE(secondSnapshot->traffic.receivedRtpBytes, quint64(23));
    QCOMPARE(secondSnapshot->traffic.submittedRtpBytes, quint64(24));
}

void TestMediaPeer::strictControlBuffersCandidatesAndEnforcesCap()
{
    QPointer<FakeTransport> transport;
    MediaPeer peer(nullptr, [&transport](QObject* parent) -> IMediaTransport* {
        transport = new FakeTransport(parent);
        return transport;
    });

    QVERIFY(!peer.start(IMediaTransport::Role::Answerer,
                        QStringLiteral("not-a-uuid")));
    QVERIFY(peer.start(IMediaTransport::Role::Answerer,
                       QLatin1String(kConnectionA)));
    QCOMPARE(peer.connectionId(), QLatin1String(kConnectionA));
    QCOMPARE(peer.audioSsrc(), quint32(0xf46d8502U));
    QVERIFY(transport);
    QCOMPARE(transport->startedSsrc, peer.audioSsrc());

    QVERIFY(!peer.acceptControl(candidateControl(
        QLatin1String(kConnectionB), QStringLiteral("stale"))));
    QJsonObject unknown{
        {QStringLiteral("op"), QStringLiteral("modelMutation")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionA)},
    };
    QVERIFY(!peer.acceptControl(unknown));
    QJsonObject extra = descriptionControl(QLatin1String(kConnectionA));
    extra.insert(QStringLiteral("model"), QStringLiteral("forbidden"));
    QVERIFY(!peer.acceptControl(extra));
    QVERIFY(!peer.acceptControl(descriptionControl(
        QLatin1String(kConnectionA), QStringLiteral("v=0"),
        QStringLiteral("answer"))));
    QVERIFY(!peer.acceptControl(descriptionControl(
        QLatin1String(kConnectionA),
        QString(IMediaTransport::kMaxDescriptionBytes + 1, QLatin1Char('x')))));

    QVERIFY(peer.acceptControl(candidateControl(
        QLatin1String(kConnectionA), QStringLiteral("candidate-0"))));
    QVERIFY(peer.acceptControl(candidateControl(
        QLatin1String(kConnectionA), QStringLiteral("candidate-1"))));
    QCOMPARE(transport->candidates.size(), 0);

    QVERIFY(peer.acceptControl(descriptionControl(QLatin1String(kConnectionA))));
    QCOMPARE(transport->descriptions.size(), 1);
    QCOMPARE(transport->candidates.size(), 2);
    QCOMPARE(transport->candidates.at(0).first, QStringLiteral("candidate-0"));
    QCOMPARE(transport->candidates.at(1).first, QStringLiteral("candidate-1"));
    QVERIFY(!peer.acceptControl(descriptionControl(QLatin1String(kConnectionA))));

    for (int i = 2; i < IMediaTransport::kMaxRemoteCandidates; ++i) {
        QVERIFY(peer.acceptControl(candidateControl(
            QLatin1String(kConnectionA), QStringLiteral("candidate-%1").arg(i))));
    }
    QCOMPARE(transport->candidates.size(), IMediaTransport::kMaxRemoteCandidates);
    QVERIFY(!peer.acceptControl(candidateControl(
        QLatin1String(kConnectionA), QStringLiteral("candidate-over-cap"))));
}

void TestMediaPeer::outboundControlIsScopedAndBounded()
{
    QPointer<FakeTransport> transport;
    MediaPeer peer(nullptr, [&transport](QObject* parent) -> IMediaTransport* {
        transport = new FakeTransport(parent);
        return transport;
    });
    QSignalSpy controls(&peer, &MediaPeer::controlReady);
    QSignalSpy errors(&peer, &MediaPeer::errorOccurred);
    QVERIFY(peer.start(IMediaTransport::Role::Offerer,
                       QLatin1String(kConnectionA)));

    transport->fireLocalDescription(QStringLiteral("v=0\r\n"),
                                    QStringLiteral("offer"));
    transport->fireLocalCandidate(QStringLiteral("host-candidate"),
                                  QStringLiteral("audio"));
    QCOMPARE(controls.size(), 2);
    const QJsonObject description = controls.at(0).at(0).toJsonObject();
    QCOMPARE(description.size(), 4);
    QCOMPARE(description.value(QStringLiteral("op")).toString(),
             QStringLiteral("description"));
    QCOMPARE(description.value(QStringLiteral("connectionId")).toString(),
             QLatin1String(kConnectionA));
    const QJsonObject candidate = controls.at(1).at(0).toJsonObject();
    QCOMPARE(candidate.size(), 4);
    QCOMPARE(candidate.value(QStringLiteral("op")).toString(),
             QStringLiteral("candidate"));
    QCOMPARE(candidate.value(QStringLiteral("connectionId")).toString(),
             QLatin1String(kConnectionA));

    transport->fireLocalDescription(
        QString(IMediaTransport::kMaxDescriptionBytes + 1, QLatin1Char('x')),
        QStringLiteral("offer"));
    transport->fireLocalCandidate(
        QString(IMediaTransport::kMaxCandidateBytes + 1, QLatin1Char('x')),
        QStringLiteral("audio"));
    transport->fireLocalDescription(QStringLiteral("v=0"),
                                    QStringLiteral("answer"));
    QCOMPARE(controls.size(), 2);
    QCOMPARE(errors.size(), 3);
}

void TestMediaPeer::oldQueuedCallbacksCannotEnterNewGeneration()
{
    QList<QPointer<FakeTransport>> transports;
    MediaPeer peer(nullptr, [&transports](QObject* parent) -> IMediaTransport* {
        auto* transport = new FakeTransport(parent);
        transports.push_back(transport);
        return transport;
    });
    QSignalSpy controls(&peer, &MediaPeer::controlReady);
    QSignalSpy displays(&peer, &MediaPeer::displayReceived);
    QSignalSpy ready(&peer, &MediaPeer::ready);
    bool callbackOnOwnerThread = false;
    connect(&peer, &MediaPeer::controlReady, &peer,
            [&peer, &callbackOnOwnerThread](const QJsonObject&) {
                callbackOnOwnerThread = QThread::currentThread() == peer.thread();
            });
    QVERIFY(peer.start(IMediaTransport::Role::Offerer,
                       QLatin1String(kConnectionA)));
    const quint32 firstSsrc = peer.audioSsrc();
    FakeTransport* oldTransport = transports.at(0);

    std::thread worker([oldTransport] {
        oldTransport->fireLocalDescription(QStringLiteral("v=0"),
                                           QStringLiteral("offer"));
        oldTransport->fireDisplay(QByteArrayLiteral("old-display"));
        oldTransport->fireReady();
    });
    worker.join();

    peer.stop();
    QVERIFY(peer.start(IMediaTransport::Role::Offerer,
                       QLatin1String(kConnectionB)));
    QCOMPARE(peer.audioSsrc(), quint32(0xd2a6ade1U));
    QVERIFY(peer.audioSsrc() != firstSsrc);
    QCoreApplication::processEvents();
    QCOMPARE(controls.size(), 0);
    QCOMPARE(displays.size(), 0);
    QCOMPARE(ready.size(), 0);

    FakeTransport* currentTransport = transports.at(1);
    std::thread currentWorker([currentTransport] {
        currentTransport->fireLocalDescription(QStringLiteral("v=0"),
                                               QStringLiteral("offer"));
    });
    currentWorker.join();
    QTRY_COMPARE_WITH_TIMEOUT(controls.size(), 1, 5000);
    QVERIFY(callbackOnOwnerThread);
    currentTransport->fireReady();
    QCOMPARE(controls.at(0).at(0).toJsonObject()
                 .value(QStringLiteral("connectionId")).toString(),
             QLatin1String(kConnectionB));
    QCOMPARE(ready.size(), 1);
    QVERIFY(peer.isReady());

    const QByteArray display("new-display");
    const QByteArray rtp = rtpPacket(12, peer.audioSsrc());
    QVERIFY(peer.sendDisplay(display));
    QVERIFY(peer.sendRtp(rtp));
    QVERIFY(!peer.sendRtp(rtpPacket(13, peer.audioSsrc() + 1)));
    QVERIFY(!peer.sendDisplay(QByteArray(
        IMediaTransport::kMaxDisplayMessageBytes + 1, 'x')));
    QVERIFY(!peer.sendRtp(QByteArray(
        IMediaTransport::kMaxRawRtpBytes + 1, 'x')));
    QCOMPARE(currentTransport->sentDisplay, QList<QByteArray>{display});
    QCOMPARE(currentTransport->sentRtp, QList<QByteArray>{rtp});
}

void TestMediaPeer::signalHandlersMayRestartOrDeletePeer()
{
    QList<QPointer<FakeTransport>> transports;
    MediaPeer peer(nullptr, [&transports](QObject* parent) -> IMediaTransport* {
        auto* transport = new FakeTransport(parent);
        transports.push_back(transport);
        return transport;
    });
    QVERIFY(peer.start(IMediaTransport::Role::Offerer,
                       QLatin1String(kConnectionA)));
    connect(&peer, &MediaPeer::controlReady, &peer,
            [&peer](const QJsonObject&) {
                peer.stop();
                QVERIFY(peer.start(IMediaTransport::Role::Offerer,
                                   QLatin1String(kConnectionB)));
            });
    transports.at(0)->fireLocalDescription(QStringLiteral("v=0"),
                                           QStringLiteral("offer"));
    QCOMPARE(peer.connectionId(), QLatin1String(kConnectionB));

    QPointer<MediaPeer> deletedPeer;
    QPointer<FakeTransport> deletedTransport;
    deletedPeer = new MediaPeer(
        nullptr, [&deletedTransport](QObject* parent) -> IMediaTransport* {
            deletedTransport = new FakeTransport(parent);
            return deletedTransport;
        });
    QVERIFY(deletedPeer->start(IMediaTransport::Role::Offerer,
                               QLatin1String(kConnectionA)));
    connect(deletedPeer, &MediaPeer::controlReady, deletedPeer,
            [&deletedPeer](const QJsonObject&) { delete deletedPeer.data(); });
    deletedTransport->fireLocalDescription(QStringLiteral("v=0"),
                                           QStringLiteral("offer"));
    QVERIFY(deletedPeer.isNull());
}

void TestMediaPeer::realPeersExchangeQueuedControlAndDirectMedia()
{
    MediaPeer offerer;
    MediaPeer answerer;
    bool controlRejected = false;
    connect(&offerer, &MediaPeer::controlReady, &answerer,
            [&answerer, &controlRejected](const QJsonObject& control) {
                if (!answerer.acceptControl(control)) {
                    controlRejected = true;
                }
            }, Qt::QueuedConnection);
    connect(&answerer, &MediaPeer::controlReady, &offerer,
            [&offerer, &controlRejected](const QJsonObject& control) {
                if (!offerer.acceptControl(control)) {
                    controlRejected = true;
                }
            }, Qt::QueuedConnection);

    QSignalSpy offerReady(&offerer, &MediaPeer::ready);
    QSignalSpy answerReady(&answerer, &MediaPeer::ready);
    QSignalSpy offerControls(&offerer, &MediaPeer::controlReady);
    QSignalSpy displayReceived(&answerer, &MediaPeer::displayReceived);
    QSignalSpy rtpReceived(&answerer, &MediaPeer::rtpReceived);
    QVERIFY(answerer.start(IMediaTransport::Role::Answerer,
                           QLatin1String(kConnectionA)));
    QVERIFY(offerer.start(IMediaTransport::Role::Offerer,
                          QLatin1String(kConnectionA)));
    QTRY_COMPARE_WITH_TIMEOUT(offerReady.size(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(answerReady.size(), 1, 10000);
    QVERIFY(!controlRejected);

    const QByteArray display("media-peer-display");
    QCOMPARE(offerer.audioSsrc(), answerer.audioSsrc());
    QVERIFY(offerer.audioSsrc() != 0);
    bool foundExpectedSsrc = false;
    for (const QList<QVariant>& arguments : offerControls) {
        const QJsonObject control = arguments.at(0).toJsonObject();
        if (control.value(QStringLiteral("op")).toString()
            == QStringLiteral("description")) {
            foundExpectedSsrc = control.value(QStringLiteral("sdp")).toString()
                .contains(QStringLiteral("a=ssrc:%1").arg(offerer.audioSsrc()));
        }
    }
    QVERIFY(foundExpectedSsrc);
    const QByteArray rtp = rtpPacket(13, offerer.audioSsrc());
    QVERIFY(offerer.sendDisplay(display));
    QVERIFY(offerer.sendRtp(rtp));
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.size(), 1, 5000);
    QCOMPARE(displayReceived.at(0).at(0).toByteArray(), display);
    QCOMPARE(rtpReceived.at(0).at(0).toByteArray(), rtp);
    const auto offerTelemetry = offerer.telemetry();
    QVERIFY(offerTelemetry.has_value());
    QCOMPARE(offerTelemetry->traffic.submittedDisplayPayloadBytes,
             static_cast<quint64>(display.size()));
    QCOMPARE(offerTelemetry->traffic.submittedRtpBytes,
             static_cast<quint64>(rtp.size()));
    const auto answerTrafficArrived = [&answerer, &display, &rtp] {
        const auto snapshot = answerer.telemetry();
        return snapshot
            && snapshot->traffic.receivedDisplayPayloadBytes
                == static_cast<quint64>(display.size())
            && snapshot->traffic.receivedRtpBytes
                == static_cast<quint64>(rtp.size());
    };
    QTRY_VERIFY_WITH_TIMEOUT(answerTrafficArrived(), 5000);
}

// R-R3-28, amended 2026-09-23. Only a transport that could not be built is
// worth retrying; the controller reads which refusal it was from here.
void TestMediaPeer::startRefusalsAreTyped()
{
    using Refusal = MediaPeer::StartRefusal;
    QList<QPointer<FakeTransport>> transports;
    bool succeeds = true;
    MediaPeer peer(nullptr, [&](QObject* parent) -> IMediaTransport* {
        auto* transport = new FakeTransport(parent);
        transport->startSucceeds = succeeds;
        transports.push_back(transport);
        return transport;
    });
    QCOMPARE(peer.lastStartRefusal(), Refusal::None);

    // Preconditions: a non-canonical connection id, an already started peer.
    QVERIFY(!peer.start(IMediaTransport::Role::Answerer, QStringLiteral("not-an-id")));
    QCOMPARE(peer.lastStartRefusal(), Refusal::Precondition);
    QVERIFY(peer.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QCOMPARE(peer.lastStartRefusal(), Refusal::None);
    QVERIFY(!peer.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionB)));
    QCOMPARE(peer.lastStartRefusal(), Refusal::Precondition);
    peer.stop();

    // The transport refuses without reporting an error: a precondition of
    // its own, such as an SSRC of zero. Permanent.
    succeeds = false;
    QVERIFY(!peer.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QCOMPARE(peer.lastStartRefusal(), Refusal::TransportRefused);

    // A later successful start clears the record.
    succeeds = true;
    QVERIFY(peer.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QCOMPARE(peer.lastStartRefusal(), Refusal::None);
    peer.stop();

    // The transport reports an error and refuses: its peer could not be
    // built (LibDataChannelMediaTransport::start's catch). Transient.
    class BuildFailing final : public FakeTransport {
    public:
        using FakeTransport::FakeTransport;
        bool start(const StartOptions&) override
        {
            emit errorOccurred(QStringLiteral("could not create the peer connection"));
            return false;
        }
    };
    MediaPeer buildFailing(nullptr, [](QObject* parent) -> IMediaTransport* {
        return new BuildFailing(parent);
    });
    QSignalSpy buildErrors(&buildFailing, &MediaPeer::errorOccurred);
    QVERIFY(!buildFailing.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QCOMPARE(buildFailing.lastStartRefusal(), Refusal::TransportConstructionFailed);
    QCOMPARE(buildErrors.size(), 1);

    // The factory throws: the transport could not be built. Transient.
    MediaPeer throwing(nullptr, [](QObject*) -> IMediaTransport* {
        throw std::runtime_error("no transport");
    });
    QVERIFY(!throwing.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QCOMPARE(throwing.lastStartRefusal(), Refusal::TransportConstructionFailed);

    // The factory returns no transport. Permanent.
    MediaPeer empty(nullptr, [](QObject*) -> IMediaTransport* { return nullptr; });
    QVERIFY(!empty.start(IMediaTransport::Role::Answerer, QLatin1String(kConnectionA)));
    QCOMPARE(empty.lastStartRefusal(), Refusal::InvalidTransport);
}

QTEST_GUILESS_MAIN(TestMediaPeer)
#include "tst_media_peer.moc"
