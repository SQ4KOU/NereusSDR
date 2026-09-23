// =================================================================
// tests/tst_media_transport.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// =================================================================

#include "core/session/media/LibDataChannelMediaTransport.h"
#include "core/session/media/PcmAudioCodec.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include <chrono>
#include <cmath>
#include <thread>

using namespace NereusSDR;

namespace {

constexpr quint32 kTestAudioSsrc = 0x4e523301U;

// The Opus parameters every Core offered before R-R3-23, verbatim: an old
// Core that a new GUI must still answer.
constexpr const char* kOldCoreOpusParameters =
    "minptime=10;maxaveragebitrate=96000;stereo=1;sprop-stereo=1;useinbandfec=1";

// The single Opus a=fmtp line of an offer, without its prefix.
QString opusFormatLine(const QString& sdp)
{
    const QString prefix = QStringLiteral("a=fmtp:111 ");
    QStringList found;
    for (const QString& line : sdp.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
        if (line.startsWith(prefix)) {
            found << line.mid(prefix.size());
        }
    }
    return found.size() == 1 ? found.constFirst() : QString();
}
// The audio section of a description: its m= line and every a=rtpmap and
// a=fmtp line, in order. Fingerprints, ICE credentials and SSRC lines differ
// per run; these do not, so they are the golden part of an offer.
QStringList audioFormatLines(const QString& sdp)
{
    QStringList lines;
    bool inAudio = false;
    for (const QString& line : sdp.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
        if (line.startsWith(QLatin1String("m="))) {
            inAudio = line.startsWith(QLatin1String("m=audio"));
            if (inAudio) { lines << line; }
            continue;
        }
        if (inAudio && (line.startsWith(QLatin1String("a=rtpmap:"))
                        || line.startsWith(QLatin1String("a=fmtp:")))) {
            lines << line;
        }
    }
    return lines;
}

// Set only in the child process that checks when SCTP settings are applied.
constexpr const char* kFirstPeerChildVariable = "NEREUS_TST_MEDIA_TRANSPORT_FIRST_PEER";

} // namespace

class TestMediaTransport : public QObject {
    Q_OBJECT

private slots:
    // Observes a fresh child process, so its place in the list does not
    // matter and it can run any number of times.
    void sctpSettingsAppliedOnceBeforeFirstPeer();
    void encryptedPeersCarryDisplayAndRtp();
    void stalledReceiverRefusesDisplayInsteadOfQueueing();
    void heldDisplayMessageIsTakenAndSignalsWritable();
    void queuedDisplayOverflowDropsOldestAndCountsIt();
    void telemetryCountsValidatedTrafficAndResets();
    void boundedInputsRefuseBeforeTransport();
    void stopCancelsOldCallbacksAndRecreates();
    void signalRestartDropsRemainingOldGenerationMedia();
    void deletionFromReceivedSignalIsSafe();
    void offerDescribesTheRealEncoder_data();
    void offerDescribesTheRealEncoder();
    void answererAcceptsNewAndOldCoreOffers_data();
    void answererAcceptsNewAndOldCoreOffers();
    void preconditionRefusalsReportNoError();
    void losslessRtpMapIsOfferedOnlyWhenAsked();
    void answerKeepsTheLosslessRtpMap_data();
    void answerKeepsTheLosslessRtpMap();
    void losslessAudioCrossesRealEncryptedLoopback();

private:
    static void wireExchange(LibDataChannelMediaTransport& offerer,
                             LibDataChannelMediaTransport& answerer,
                             QString* offerOut, QString* answerOut);
    static void wire(LibDataChannelMediaTransport& offerer,
                     LibDataChannelMediaTransport& answerer);
    static void startPair(LibDataChannelMediaTransport& offerer,
                          LibDataChannelMediaTransport& answerer);
    static QByteArray rtpPacket(quint16 sequence,
                                qsizetype size = 15,
                                quint32 ssrc = kTestAudioSsrc);
};

void TestMediaTransport::wire(LibDataChannelMediaTransport& offerer,
                              LibDataChannelMediaTransport& answerer)
{
    connect(&offerer, &IMediaTransport::localDescription, &answerer,
            [&answerer](const QString& sdp, const QString& type) {
                QVERIFY2(answerer.acceptDescription(sdp, type),
                         "answerer rejected offer");
            });
    connect(&answerer, &IMediaTransport::localDescription, &offerer,
            [&offerer](const QString& sdp, const QString& type) {
                QVERIFY2(offerer.acceptDescription(sdp, type),
                         "offerer rejected answer");
            });
    connect(&offerer, &IMediaTransport::localCandidate, &answerer,
            [&answerer](const QString& candidate, const QString& mid) {
                QVERIFY2(answerer.acceptCandidate(candidate, mid),
                         "answerer rejected host candidate");
            });
    connect(&answerer, &IMediaTransport::localCandidate, &offerer,
            [&offerer](const QString& candidate, const QString& mid) {
                QVERIFY2(offerer.acceptCandidate(candidate, mid),
                         "offerer rejected host candidate");
            });
}

void TestMediaTransport::startPair(LibDataChannelMediaTransport& offerer,
                                   LibDataChannelMediaTransport& answerer)
{
    wire(offerer, answerer);
    QSignalSpy offerReady(&offerer, &IMediaTransport::ready);
    QSignalSpy answerReady(&answerer, &IMediaTransport::ready);
    QVERIFY(answerer.start({IMediaTransport::Role::Answerer,
                            kTestAudioSsrc}));
    QVERIFY(offerer.start({IMediaTransport::Role::Offerer,
                           kTestAudioSsrc}));
    QTRY_COMPARE_WITH_TIMEOUT(offerReady.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(answerReady.count(), 1, 10000);
    QVERIFY(offerer.isReady());
    QVERIFY(answerer.isReady());
}

QByteArray TestMediaTransport::rtpPacket(quint16 sequence, qsizetype size,
                                         quint32 ssrc)
{
    QByteArray packet(size, char(0xa5));
    packet[0] = char(0x80);
    packet[1] = char(111);
    packet[2] = char(sequence >> 8);
    packet[3] = char(sequence & 0xff);
    packet[8] = char(ssrc >> 24);
    packet[9] = char(ssrc >> 16);
    packet[10] = char(ssrc >> 8);
    packet[11] = char(ssrc);
    return packet;
}

void TestMediaTransport::sctpSettingsAppliedOnceBeforeFirstPeer()
{
    // The settings are process-wide and applied once, so only a process in
    // which no peer has existed yet can show when they were applied. Run the
    // check in a fresh copy of this test binary running only this slot.
    if (!qEnvironmentVariableIsSet(kFirstPeerChildVariable)) {
        QProcess child;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QString::fromLatin1(kFirstPeerChildVariable), QStringLiteral("1"));
        child.setProcessEnvironment(environment);
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("sctpSettingsAppliedOnceBeforeFirstPeer")});
        QVERIFY2(child.waitForStarted(10'000), qPrintable(child.errorString()));
        QVERIFY2(child.waitForFinished(60'000), "first-peer child did not finish");
        const QByteArray output = child.readAll();
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 output.constData());
        QVERIFY2(output.contains("PASS   : TestMediaTransport::"
                                 "sctpSettingsAppliedOnceBeforeFirstPeer()"),
                 output.constData());
        return;
    }

    const MediaSctpSettingsRecord before = mediaSctpSettingsRecord();
    QCOMPARE(before.applications, 0);
    QCOMPARE(before.peersCreated, quint64(0));

    {
        LibDataChannelMediaTransport offerer;
        LibDataChannelMediaTransport answerer;
        startPair(offerer, answerer);
        const MediaSctpSettingsRecord first = mediaSctpSettingsRecord();
        QCOMPARE(first.applications, 1);
        QCOMPARE(first.peersCreatedBeforeApplication, quint64(0));
        QCOMPARE(first.peersCreated, quint64(2));
        offerer.stop();
        answerer.stop();
    }

    // Later calls and later peers never apply the settings again.
    QVERIFY(!applyMediaSctpSettingsOnce());
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    startPair(offerer, answerer);
    const MediaSctpSettingsRecord second = mediaSctpSettingsRecord();
    QCOMPARE(second.applications, 1);
    QCOMPARE(second.peersCreatedBeforeApplication, quint64(0));
    QCOMPARE(second.peersCreated, quint64(4));
    offerer.stop();
    answerer.stop();
}

void TestMediaTransport::encryptedPeersCarryDisplayAndRtp()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QSignalSpy offerDescriptions(&offerer, &IMediaTransport::localDescription);
    QSignalSpy answerDescriptions(&answerer, &IMediaTransport::localDescription);
    QSignalSpy displayReceived(&answerer, &IMediaTransport::displayReceived);
    QSignalSpy rtpReceived(&answerer, &IMediaTransport::rtpReceived);
    startPair(offerer, answerer);

    QCOMPARE(offerDescriptions.count(), 1);
    QCOMPARE(answerDescriptions.count(), 1);
    QVERIFY(offerDescriptions.at(0).at(0).toString().contains("a=fingerprint:"));
    QVERIFY(answerDescriptions.at(0).at(0).toString().contains("a=fingerprint:"));
    QVERIFY(offerDescriptions.at(0).at(0).toString().contains("m=application"));
    QVERIFY(offerDescriptions.at(0).at(0).toString().contains("m=audio"));
    QVERIFY(offerDescriptions.at(0).at(0).toString().contains("stereo=1"));
    QVERIFY(offerDescriptions.at(0).at(0).toString().contains(
        QStringLiteral("a=ssrc:%1").arg(kTestAudioSsrc)));
    QVERIFY(!offerDescriptions.at(0).at(0).toString().contains(
        QStringLiteral("a=candidate:"), Qt::CaseInsensitive));
    QVERIFY(!answerDescriptions.at(0).at(0).toString().contains(
        QStringLiteral("a=candidate:"), Qt::CaseInsensitive));

    const QList<QByteArray> displayMessages{
        QByteArrayLiteral("pan-0-display"),
        QByteArrayLiteral("pan-1-display"),
        QByteArrayLiteral("pan-2-display"),
        QByteArrayLiteral("pan-3-display"),
    };
    const QByteArray rtp = rtpPacket(7);
    for (const QByteArray& message : displayMessages) {
        QVERIFY(offerer.sendDisplay(message));
    }
    QVERIFY(offerer.sendRtp(rtp));
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.count(), displayMessages.size(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.count(), 1, 5000);
    for (qsizetype i = 0; i < displayMessages.size(); ++i) {
        QCOMPARE(displayReceived.at(i).at(0).toByteArray(), displayMessages.at(i));
    }
    QCOMPARE(rtpReceived.at(0).at(0).toByteArray(), rtp);

    const QByteArray boundaryDisplay(
        IMediaTransport::kMaxDisplayMessageBytes, char(0x5a));
    const QByteArray boundaryRtp = rtpPacket(
        8, IMediaTransport::kMaxRawRtpBytes);
    QVERIFY(offerer.sendDisplay(boundaryDisplay));
    QVERIFY(offerer.sendRtp(boundaryRtp));
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.count(),
                              displayMessages.size() + 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.count(), 2, 5000);
    QCOMPARE(displayReceived.last().at(0).toByteArray(), boundaryDisplay);
    QCOMPARE(rtpReceived.last().at(0).toByteArray(), boundaryRtp);
    QVERIFY(!offerer.sendRtp(rtpPacket(9, 15, kTestAudioSsrc + 1)));

    offerer.stop();
    answerer.stop();
}

void TestMediaTransport::stalledReceiverRefusesDisplayInsteadOfQueueing()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QSignalSpy displayReceived(&answerer, &IMediaTransport::displayReceived);
    startPair(offerer, answerer);

    // The largest spectrum frame: 4096/4096 points plus a 768-point 3D row.
    constexpr qsizetype kFrameBytes = 9361;
    const QByteArray frame(kFrameBytes, char(0x3c));
    answerer.setDisplayReceiveStalledForTest(true);

    // Offer frames until a full second passes in which the sender took
    // nothing: no frame went to SCTP (Sent) and none was held by the
    // library (Queued). Either one restarts the second.
    int sent = 0;
    int queued = 0;
    int refused = 0;
    QElapsedTimer sinceTaken;
    sinceTaken.start();
    QElapsedTimer overall;
    overall.start();
    while (sinceTaken.elapsed() < 1000 && overall.elapsed() < 60'000) {
        const IMediaTransport::DisplaySendResult result = offerer.submitDisplay(frame);
        if (result == IMediaTransport::DisplaySendResult::Sent) {
            ++sent;
            sinceTaken.restart();
        } else if (result == IMediaTransport::DisplaySendResult::Queued) {
            ++queued;
            sinceTaken.restart();
        } else {
            ++refused;
        }
        QTest::qWait(1);
    }
    QVERIFY2(overall.elapsed() < 60'000, "the sender never stopped taking frames");
    QVERIFY(sent > 0);
    QVERIFY(refused > 0);

    // The SCTP buffers (64 KiB send, 128 KiB receive) and the one message
    // the library holds do not add up to a whole-path limit, so the bound
    // is the measurement: the whole path took exactly 26 frames (243,386
    // bytes) on every run, 2026-09-23. The margin is one frame, for when
    // the receiver's window update lands. libdatachannel's 1 MiB defaults
    // let about 1.4 MB pile up here.
    const quint64 submitted = offerer.telemetry()->submittedDisplayPayloadBytes;
    constexpr quint64 kMeasuredFrames = 26;
    constexpr quint64 kMarginFrames = 1;
    const quint64 bound = (kMeasuredFrames + kMarginFrames) * quint64(kFrameBytes);
    QCOMPARE(submitted, quint64(sent + queued) * quint64(kFrameBytes));
    QVERIFY2(submitted <= bound,
             qPrintable(QStringLiteral("sender took %1 bytes in %2 frames (%3 held by "
                                       "the library), bound %4")
                            .arg(submitted).arg(sent + queued).arg(queued).arg(bound)));

    // Once the receiver drains again the sender recovers, and every frame
    // that arrives is whole.
    answerer.setDisplayReceiveStalledForTest(false);
    QTRY_VERIFY_WITH_TIMEOUT(offerer.sendDisplay(frame), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(!displayReceived.isEmpty(), 10'000);
    for (const QList<QVariant>& arguments : displayReceived) {
        QCOMPARE(arguments.at(0).toByteArray(), frame);
    }

    offerer.stop();
    answerer.stop();
}

// R-R3-03/R-R3-05: usrsctp takes a message only while it fits beside the
// data not yet acknowledged, and its send buffer is the 64 KiB maximum
// message. A second large message sent before the first is acknowledged is
// taken by libdatachannel and held (Queued), not lost; a third is Busy; the
// display channel reports writable once the held message has gone, and the
// receiver gets both messages.
void TestMediaTransport::heldDisplayMessageIsTakenAndSignalsWritable()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QSignalSpy displayReceived(&answerer, &IMediaTransport::displayReceived);
    QSignalSpy writable(&offerer, &IMediaTransport::displayWritable);
    startPair(offerer, answerer);

    constexpr qsizetype kChunkBytes = 49'216; // one PureSignal display chunk
    const QByteArray first(kChunkBytes, char(0x11));
    const QByteArray second(kChunkBytes, char(0x22));
    const QByteArray third(kChunkBytes, char(0x33));
    QCOMPARE(offerer.submitDisplay(first), IMediaTransport::DisplaySendResult::Sent);
    QCOMPARE(offerer.submitDisplay(second), IMediaTransport::DisplaySendResult::Queued);
    QVERIFY(offerer.displayBusy());
    QCOMPARE(offerer.submitDisplay(third), IMediaTransport::DisplaySendResult::Busy);

    QTRY_VERIFY_WITH_TIMEOUT(!writable.isEmpty(), 10'000);
    QVERIFY(!offerer.displayBusy());
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.count(), 2, 10'000);
    QCOMPARE(displayReceived.at(0).at(0).toByteArray(), first);
    QCOMPARE(displayReceived.at(1).at(0).toByteArray(), second);
    // Busy was not taken: nothing more arrives.
    QTest::qWait(200);
    QCOMPARE(displayReceived.count(), 2);
    QCOMPARE(offerer.telemetry()->submittedDisplayPayloadBytes, quint64(2 * kChunkBytes));

    offerer.stop();
    answerer.stop();
}

void TestMediaTransport::queuedDisplayOverflowDropsOldestAndCountsIt()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QSignalSpy displayReceived(&answerer, &IMediaTransport::displayReceived);
    startPair(offerer, answerer);

    QList<QByteArray> sent;
    quint64 sentBytes = 0;
    for (int i = 0; i < 9; ++i) {
        sent.append(QStringLiteral("queued-display-%1").arg(i).toUtf8());
        sentBytes += quint64(sent.last().size());
        QVERIFY(offerer.sendDisplay(sent.last()));
    }
    // Keep this thread out of its event loop, so no drain runs, until the
    // library has delivered all nine messages into the receive queue.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (answerer.telemetry()->receivedDisplayPayloadBytes < sentBytes
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    QCOMPARE(answerer.telemetry()->receivedDisplayPayloadBytes, sentBytes);
    QCOMPARE(answerer.telemetry()->displayMessagesDropped, quint64(1));

    // One drain hands over the newest eight; the eight-message rule dropped
    // exactly one, and received bytes still count all nine arrivals.
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.count(), 8, 5000);
    QTest::qWait(50);
    QCOMPARE(displayReceived.count(), 8);
    QSet<QByteArray> delivered;
    for (const QList<QVariant>& arguments : displayReceived) {
        delivered.insert(arguments.at(0).toByteArray());
    }
    QCOMPARE(delivered.size(), 8);
    for (const QByteArray& message : delivered) {
        QVERIFY(sent.contains(message));
    }
    QCOMPARE(answerer.telemetry()->displayMessagesDropped, quint64(1));
    QCOMPARE(answerer.telemetry()->receivedDisplayPayloadBytes, sentBytes);
    QCOMPARE(offerer.telemetry()->displayMessagesDropped, quint64(0));

    offerer.stop();
    answerer.stop();
}

void TestMediaTransport::telemetryCountsValidatedTrafficAndResets()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QSignalSpy displayReceived(&answerer, &IMediaTransport::displayReceived);
    QSignalSpy rtpReceived(&answerer, &IMediaTransport::rtpReceived);
    startPair(offerer, answerer);

    QVERIFY(offerer.telemetry().has_value());
    QVERIFY(answerer.telemetry().has_value());
    const auto countersAreZero = [](const MediaTransportTelemetry& telemetry) {
        return telemetry.receivedDisplayPayloadBytes == 0
            && telemetry.submittedDisplayPayloadBytes == 0
            && telemetry.displayMessagesDropped == 0
            && telemetry.receivedRtpBytes == 0
            && telemetry.submittedRtpBytes == 0;
    };
    QVERIFY(countersAreZero(*offerer.telemetry()));
    QVERIFY(countersAreZero(*answerer.telemetry()));

    const QByteArray display = QByteArrayLiteral("observed-display-payload");
    const QByteArray rtp = rtpPacket(40, 37);
    QVERIFY(offerer.sendDisplay(display));
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.size(), 1, 5000);
    QVERIFY(offerer.sendRtp(rtp));
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.size(), 1, 5000);

    const auto trafficArrived = [&answerer, &display, &rtp] {
        const auto snapshot = answerer.telemetry();
        return snapshot
            && snapshot->receivedDisplayPayloadBytes
                == static_cast<quint64>(display.size())
            && snapshot->receivedRtpBytes == static_cast<quint64>(rtp.size());
    };
    QTRY_VERIFY_WITH_TIMEOUT(trafficArrived(), 5000);
    const MediaTransportTelemetry submitted = *offerer.telemetry();
    QCOMPARE(submitted.submittedDisplayPayloadBytes,
             static_cast<quint64>(display.size()));
    QCOMPARE(submitted.submittedRtpBytes, static_cast<quint64>(rtp.size()));
    QCOMPARE(submitted.receivedDisplayPayloadBytes, quint64(0));
    QCOMPARE(submitted.receivedRtpBytes, quint64(0));

    // These fail the adapter's own preflight and never become submissions.
    QVERIFY(!offerer.sendDisplay(QByteArray{}));
    QVERIFY(!offerer.sendDisplay(QByteArray(
        IMediaTransport::kMaxDisplayMessageBytes + 1, 'x')));
    QVERIFY(!offerer.sendRtp(QByteArray(
        IMediaTransport::kMinRawRtpBytes - 1, 'x')));
    QVERIFY(!offerer.sendRtp(QByteArray(
        IMediaTransport::kMaxRawRtpBytes + 1, 'x')));
    QVERIFY(!offerer.sendRtp(rtpPacket(41, 15, kTestAudioSsrc + 1)));
    const MediaTransportTelemetry afterInvalid = *offerer.telemetry();
    QCOMPARE(afterInvalid.receivedDisplayPayloadBytes,
             submitted.receivedDisplayPayloadBytes);
    QCOMPARE(afterInvalid.submittedDisplayPayloadBytes,
             submitted.submittedDisplayPayloadBytes);
    QCOMPARE(afterInvalid.receivedRtpBytes, submitted.receivedRtpBytes);
    QCOMPARE(afterInvalid.submittedRtpBytes, submitted.submittedRtpBytes);

    offerer.stop();
    answerer.stop();
    QVERIFY(!offerer.telemetry().has_value());
    QVERIFY(!answerer.telemetry().has_value());

    // A new start owns a new callback bridge and therefore fresh counters.
    QSignalSpy offerReady(&offerer, &IMediaTransport::ready);
    QSignalSpy answerReady(&answerer, &IMediaTransport::ready);
    QVERIFY(answerer.start({IMediaTransport::Role::Answerer,
                            kTestAudioSsrc}));
    QVERIFY(offerer.start({IMediaTransport::Role::Offerer,
                           kTestAudioSsrc}));
    QVERIFY(offerer.telemetry().has_value());
    QVERIFY(answerer.telemetry().has_value());
    QVERIFY(countersAreZero(*offerer.telemetry()));
    QVERIFY(countersAreZero(*answerer.telemetry()));
    QTRY_COMPARE_WITH_TIMEOUT(offerReady.size(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(answerReady.size(), 1, 10000);

    offerer.stop();
    answerer.stop();
}

void TestMediaTransport::boundedInputsRefuseBeforeTransport()
{
    LibDataChannelMediaTransport transport;
    QVERIFY(transport.start({IMediaTransport::Role::Answerer,
                             kTestAudioSsrc}));

    LibDataChannelMediaTransport offerer;
    QSignalSpy offerDescriptions(&offerer, &IMediaTransport::localDescription);
    QVERIFY(offerer.start({IMediaTransport::Role::Offerer,
                           kTestAudioSsrc}));
    QTRY_COMPARE_WITH_TIMEOUT(offerDescriptions.count(), 1, 5000);
    const QString candidateFreeOffer =
        offerDescriptions.at(0).at(0).toString();
    QVERIFY(!candidateFreeOffer.contains(QStringLiteral("a=candidate:"),
                                         Qt::CaseInsensitive));

    QVERIFY(!transport.acceptDescription(
        QString(IMediaTransport::kMaxDescriptionBytes + 1, QLatin1Char('x')),
        QStringLiteral("offer")));
    QVERIFY(!transport.acceptDescription(QStringLiteral("v=0"),
                                         QStringLiteral("answer")));

    QString embeddedRelay = candidateFreeOffer;
    if (!embeddedRelay.endsWith(QLatin1Char('\n'))) {
        embeddedRelay.append(QStringLiteral("\r\n"));
    }
    embeddedRelay.append(QStringLiteral(
        "a=candidate:1 1 UDP 1 192.0.2.1 5000 typ relay\r\n"));
    QVERIFY(!transport.acceptDescription(embeddedRelay,
                                         QStringLiteral("offer")));

    QString overBudget = candidateFreeOffer;
    if (!overBudget.endsWith(QLatin1Char('\n'))) {
        overBudget.append(QStringLiteral("\r\n"));
    }
    for (int i = 0; i <= IMediaTransport::kMaxRemoteCandidates; ++i) {
        overBudget.append(QStringLiteral(
            "a=candidate:%1 1 UDP 2122260223 192.0.2.%2 %3 typ host\r\n")
                              .arg(i + 1)
                              .arg(i + 1)
                              .arg(5000 + i));
    }
    QVERIFY(!transport.acceptDescription(overBudget,
                                         QStringLiteral("offer")));
    QVERIFY(!transport.acceptCandidate(
        QString(IMediaTransport::kMaxCandidateBytes + 1, QLatin1Char('x')),
        QStringLiteral("0")));
    QVERIFY(!transport.acceptCandidate(
        QStringLiteral("1 1 UDP 1 192.0.2.1 5000 typ relay"),
        QStringLiteral("0")));
    QVERIFY(!transport.sendDisplay(QByteArray(
        IMediaTransport::kMaxDisplayMessageBytes + 1, 'x')));
    QVERIFY(!transport.sendRtp(QByteArray(
        IMediaTransport::kMaxRawRtpBytes + 1, 'x')));
    QVERIFY(!transport.sendRtp(QByteArray(
        IMediaTransport::kMinRawRtpBytes - 1, 'x')));

    transport.stop();
    transport.stop();
    offerer.stop();
    QVERIFY(!transport.isReady());
}

void TestMediaTransport::stopCancelsOldCallbacksAndRecreates()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    startPair(offerer, answerer);

    QSignalSpy offerClosed(&offerer, &IMediaTransport::closed);
    QSignalSpy answerClosed(&answerer, &IMediaTransport::closed);
    QSignalSpy lateDisplay(&answerer, &IMediaTransport::displayReceived);
    QSignalSpy lateRtp(&answerer, &IMediaTransport::rtpReceived);
    QVERIFY(offerer.sendDisplay(QByteArrayLiteral("queued-before-stop")));
    QVERIFY(offerer.sendRtp(rtpPacket(9)));
    offerer.stop();
    answerer.stop();
    QCOMPARE(offerClosed.count(), 1);
    QCOMPARE(answerClosed.count(), 1);
    QTest::qWait(100);
    QCOMPARE(lateDisplay.count(), 0);
    QCOMPARE(lateRtp.count(), 0);

    disconnect(&offerer, nullptr, &answerer, nullptr);
    disconnect(&answerer, nullptr, &offerer, nullptr);
    startPair(offerer, answerer);

    QSignalSpy displayReceived(&answerer, &IMediaTransport::displayReceived);
    QSignalSpy rtpReceived(&answerer, &IMediaTransport::rtpReceived);
    const QByteArray display("R3-recreated-display");
    const QByteArray rtp = rtpPacket(8);
    QVERIFY(offerer.sendDisplay(display));
    QVERIFY(offerer.sendRtp(rtp));
    QTRY_COMPARE_WITH_TIMEOUT(displayReceived.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.count(), 1, 5000);
    QCOMPARE(displayReceived.at(0).at(0).toByteArray(), display);
    QCOMPARE(rtpReceived.at(0).at(0).toByteArray(), rtp);
}

void TestMediaTransport::signalRestartDropsRemainingOldGenerationMedia()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    startPair(offerer, answerer);

    int displaysReceived = 0;
    int rtpReceived = 0;
    connect(&answerer, &IMediaTransport::displayReceived, &answerer,
            [&answerer, &displaysReceived](const QByteArray&) {
                ++displaysReceived;
                if (displaysReceived == 1) {
                    answerer.stop();
                    QVERIFY(answerer.start({IMediaTransport::Role::Answerer,
                                            kTestAudioSsrc}));
                }
            });
    connect(&answerer, &IMediaTransport::rtpReceived, &answerer,
            [&rtpReceived](const QByteArray&) { ++rtpReceived; });

    QVERIFY(offerer.sendDisplay(QByteArrayLiteral("old-display-0")));
    QVERIFY(offerer.sendDisplay(QByteArrayLiteral("old-display-1")));
    QVERIFY(offerer.sendDisplay(QByteArrayLiteral("old-display-2")));
    QVERIFY(offerer.sendRtp(rtpPacket(10)));
    QTRY_COMPARE_WITH_TIMEOUT(displaysReceived, 1, 5000);
    QTest::qWait(100);
    QCOMPARE(displaysReceived, 1);
    QCOMPARE(rtpReceived, 0);

    offerer.stop();
    answerer.stop();
}

void TestMediaTransport::deletionFromReceivedSignalIsSafe()
{
    LibDataChannelMediaTransport offerer;
    QPointer<LibDataChannelMediaTransport> answerer =
        new LibDataChannelMediaTransport;
    startPair(offerer, *answerer);

    connect(answerer, &IMediaTransport::displayReceived, answerer,
            [&answerer](const QByteArray&) { delete answerer.data(); });
    QVERIFY(offerer.sendDisplay(QByteArrayLiteral("delete-receiver")));
    QVERIFY(offerer.sendDisplay(QByteArrayLiteral("must-not-follow-delete")));
    QVERIFY(offerer.sendRtp(rtpPacket(11)));
    QTRY_VERIFY_WITH_TIMEOUT(answerer.isNull(), 5000);

    offerer.stop();
}


// R-R3-23 and RFC 7587 section 6.1: the Core's send-only offer describes the
// encoder it runs. No in-band FEC (the encoder has it off) and no average
// bitrate ceiling above the configured target; stereo and minptime stay.
void TestMediaTransport::offerDescribesTheRealEncoder_data()
{
    QTest::addColumn<int>("bitrate");
    QTest::addColumn<bool>("defaulted");
    QTest::newRow("default") << 24000 << true;
    QTest::newRow("24000") << 24000 << false;
    QTest::newRow("48000") << 48000 << false;
}

void TestMediaTransport::offerDescribesTheRealEncoder()
{
    QFETCH(int, bitrate);
    QFETCH(bool, defaulted);
    LibDataChannelMediaTransport offerer;
    QSignalSpy descriptions(&offerer, &IMediaTransport::localDescription);
    IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, kTestAudioSsrc};
    if (!defaulted) {
        options.audioTargetBitrate = bitrate;
    }
    QVERIFY(offerer.start(options));
    QTRY_COMPARE_WITH_TIMEOUT(descriptions.count(), 1, 5000);
    const QString offer = descriptions.at(0).at(0).toString();
    QCOMPARE(descriptions.at(0).at(1).toString(), QStringLiteral("offer"));

    const QString expected =
        QStringLiteral("minptime=10;maxaveragebitrate=%1;stereo=1;sprop-stereo=1").arg(bitrate);
    QCOMPARE(opusFormatLine(offer), expected);
    QCOMPARE(opusOfferFormatParameters(bitrate), expected);
    QVERIFY(!offer.contains(QStringLiteral("useinbandfec"), Qt::CaseInsensitive));
    QVERIFY(offer.contains(QStringLiteral("a=rtpmap:111 opus/48000/2")));
    QVERIFY(offer.contains(QStringLiteral("a=sendonly")));
    offerer.stop();
}

// The GUI answers this Core's offer and an old Core's offer alike: the real
// answerer negotiates, becomes ready and receives the audio track's RTP.
void TestMediaTransport::answererAcceptsNewAndOldCoreOffers_data()
{
    QTest::addColumn<int>("bitrate");
    QTest::addColumn<bool>("oldCore");
    QTest::newRow("new-24000") << 24000 << false;
    QTest::newRow("new-48000") << 48000 << false;
    QTest::newRow("old-core") << 24000 << true;
}

void TestMediaTransport::answererAcceptsNewAndOldCoreOffers()
{
    QFETCH(int, bitrate);
    QFETCH(bool, oldCore);
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;

    QString deliveredOffer;
    connect(&offerer, &IMediaTransport::localDescription, &answerer,
            [&answerer, &deliveredOffer, oldCore](const QString& sdp, const QString& type) {
                QString offer = sdp;
                if (oldCore) {
                    const QString current = opusFormatLine(sdp);
                    QVERIFY(!current.isEmpty());
                    offer.replace(QStringLiteral("a=fmtp:111 ") + current,
                                  QStringLiteral("a=fmtp:111 ")
                                      + QLatin1String(kOldCoreOpusParameters));
                }
                deliveredOffer = offer;
                QVERIFY2(answerer.acceptDescription(offer, type), "answerer rejected offer");
            });
    connect(&answerer, &IMediaTransport::localDescription, &offerer,
            [&offerer](const QString& sdp, const QString& type) {
                QVERIFY2(offerer.acceptDescription(sdp, type), "offerer rejected answer");
            });
    connect(&offerer, &IMediaTransport::localCandidate, &answerer,
            [&answerer](const QString& candidate, const QString& mid) {
                QVERIFY2(answerer.acceptCandidate(candidate, mid),
                         "answerer rejected host candidate");
            });
    connect(&answerer, &IMediaTransport::localCandidate, &offerer,
            [&offerer](const QString& candidate, const QString& mid) {
                QVERIFY2(offerer.acceptCandidate(candidate, mid),
                         "offerer rejected host candidate");
            });

    QSignalSpy offerReady(&offerer, &IMediaTransport::ready);
    QSignalSpy answerReady(&answerer, &IMediaTransport::ready);
    QSignalSpy answerErrors(&answerer, &IMediaTransport::errorOccurred);
    QVERIFY(answerer.start({IMediaTransport::Role::Answerer, kTestAudioSsrc}));
    QVERIFY(offerer.start({IMediaTransport::Role::Offerer, kTestAudioSsrc, bitrate}));
    QTRY_COMPARE_WITH_TIMEOUT(offerReady.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(answerReady.count(), 1, 10000);
    QCOMPARE(opusFormatLine(deliveredOffer),
             oldCore ? QString::fromLatin1(kOldCoreOpusParameters)
                     : opusOfferFormatParameters(bitrate));

    QSignalSpy rtpReceived(&answerer, &IMediaTransport::rtpReceived);
    const QByteArray rtp = rtpPacket(21);
    QVERIFY(offerer.sendRtp(rtp));
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.count(), 1, 5000);
    QCOMPARE(rtpReceived.at(0).at(0).toByteArray(), rtp);
    QCOMPARE(answerErrors.count(), 0);
    offerer.stop();
    answerer.stop();
}

// R-R3-28, amended 2026-09-23. MediaPeer tells a transient refusal (the peer
// could not be built: an error, then false) from a permanent one (false with
// no error). The real transport's precondition refusals must stay silent.
void TestMediaTransport::preconditionRefusalsReportNoError()
{
    LibDataChannelMediaTransport transport;
    QSignalSpy errors(&transport, &IMediaTransport::errorOccurred);
    // An SSRC of zero.
    QVERIFY(!transport.start({IMediaTransport::Role::Answerer, 0}));
    QCOMPARE(errors.size(), 0);
    // Already started.
    QVERIFY(transport.start({IMediaTransport::Role::Answerer, kTestAudioSsrc}));
    QVERIFY(!transport.start({IMediaTransport::Role::Answerer, kTestAudioSsrc}));
    QCOMPARE(errors.size(), 0);
    transport.stop();
}

void TestMediaTransport::wireExchange(LibDataChannelMediaTransport& offerer,
                                      LibDataChannelMediaTransport& answerer,
                                      QString* offerOut, QString* answerOut)
{
    connect(&offerer, &IMediaTransport::localDescription, &answerer,
            [&answerer, offerOut](const QString& sdp, const QString& type) {
                *offerOut = sdp;
                QVERIFY2(answerer.acceptDescription(sdp, type), "answerer rejected offer");
            });
    connect(&answerer, &IMediaTransport::localDescription, &offerer,
            [&offerer, answerOut](const QString& sdp, const QString& type) {
                *answerOut = sdp;
                QVERIFY2(offerer.acceptDescription(sdp, type), "offerer rejected answer");
            });
    connect(&offerer, &IMediaTransport::localCandidate, &answerer,
            [&answerer](const QString& candidate, const QString& mid) {
                QVERIFY2(answerer.acceptCandidate(candidate, mid),
                         "answerer rejected host candidate");
            });
    connect(&answerer, &IMediaTransport::localCandidate, &offerer,
            [&offerer](const QString& candidate, const QString& mid) {
                QVERIFY2(offerer.acceptCandidate(candidate, mid),
                         "offerer rejected host candidate");
            });
}

// R-R3-23: a GUI that did not ask for lossless receives exactly today's
// audio description (golden); one that asked gets the L16 rtpmap added to
// the same m-line, after Opus, with no fmtp of its own.
void TestMediaTransport::losslessRtpMapIsOfferedOnlyWhenAsked()
{
    const QStringList todaysAudio{
        QStringLiteral("m=audio 9 UDP/TLS/RTP/SAVPF 111"),
        QStringLiteral("a=rtpmap:111 opus/48000/2"),
        QStringLiteral("a=fmtp:111 minptime=10;maxaveragebitrate=24000;stereo=1;sprop-stereo=1"),
    };
    for (const bool lossless : {false, true}) {
        LibDataChannelMediaTransport offerer;
        QSignalSpy descriptions(&offerer, &IMediaTransport::localDescription);
        IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, kTestAudioSsrc};
        options.offerLosslessAudio = lossless;
        QVERIFY(offerer.start(options));
        QTRY_COMPARE_WITH_TIMEOUT(descriptions.count(), 1, 5000);
        const QString offer = descriptions.at(0).at(0).toString();
        if (!lossless) {
            QCOMPARE(audioFormatLines(offer), todaysAudio);
            QVERIFY(!offer.contains(QStringLiteral("L16"), Qt::CaseInsensitive));
        } else {
            // The m= line's format order is the preference: Opus first.
            // libdatachannel writes the rtpmap lines in payload type order.
            QCOMPARE(audioFormatLines(offer), (QStringList{
                QStringLiteral("m=audio 9 UDP/TLS/RTP/SAVPF 111 96"),
                QStringLiteral("a=rtpmap:96 L16/48000/2"),
                todaysAudio.at(1), todaysAudio.at(2)}));
        }
        // One m=audio line either way, and nothing negotiated before an answer.
        QCOMPARE(offer.count(QStringLiteral("m=audio")), 1);
        QVERIFY(!offerer.losslessAudioNegotiated());
        offerer.stop();
    }
}

void TestMediaTransport::answerKeepsTheLosslessRtpMap_data()
{
    QTest::addColumn<bool>("lossless");
    QTest::newRow("today") << false;
    QTest::newRow("lossless") << true;
}

// Established against the real library, not assumed: libdatachannel's
// answer to an offer carrying L16 keeps the rtpmap (it reciprocates the
// offered media), so both ends agree lossless audio may flow. Without the
// offer flag neither end does, and the answer is today's.
void TestMediaTransport::answerKeepsTheLosslessRtpMap()
{
    QFETCH(bool, lossless);
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QString offer;
    QString answer;
    wireExchange(offerer, answerer, &offer, &answer);
    QSignalSpy offerReady(&offerer, &IMediaTransport::ready);
    QSignalSpy answerReady(&answerer, &IMediaTransport::ready);
    QSignalSpy answerErrors(&answerer, &IMediaTransport::errorOccurred);
    QVERIFY(answerer.start({IMediaTransport::Role::Answerer, kTestAudioSsrc}));
    IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, kTestAudioSsrc};
    options.offerLosslessAudio = lossless;
    QVERIFY(offerer.start(options));
    QTRY_COMPARE_WITH_TIMEOUT(offerReady.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(answerReady.count(), 1, 10000);

    const QStringList answerAudio = audioFormatLines(answer);
    QVERIFY2(!answerAudio.isEmpty(), qPrintable(answer));
    QCOMPARE(answerAudio.contains(QStringLiteral("a=rtpmap:96 L16/48000/2")), lossless);
    QVERIFY(answerAudio.contains(QStringLiteral("a=rtpmap:111 opus/48000/2")));
    QVERIFY(answer.contains(QStringLiteral("a=recvonly")));
    QCOMPARE(offerer.losslessAudioNegotiated(), lossless);
    QCOMPARE(answerer.losslessAudioNegotiated(), lossless);

    // Both payload types cross the same track.
    QSignalSpy rtpReceived(&answerer, &IMediaTransport::rtpReceived);
    const QByteArray opus = rtpPacket(30);
    const QByteArray l16 = PcmAudioPacketiser{}.encode(
        QVector<float>(PcmAudioCodecConfig::kPacketFrames * 2, 0.5f), 31, 1920,
        kTestAudioSsrc).packet;
    QVERIFY(offerer.sendRtp(opus));
    QVERIFY(offerer.sendRtp(l16));
    QTRY_COMPARE_WITH_TIMEOUT(rtpReceived.count(), 2, 5000);
    QCOMPARE(rtpReceived.at(0).at(0).toByteArray(), opus);
    QCOMPARE(rtpReceived.at(1).at(0).toByteArray(), l16);
    QCOMPARE(answerErrors.count(), 0);
    offerer.stop();
    answerer.stop();
    QVERIFY(!offerer.losslessAudioNegotiated());
    QVERIFY(!answerer.losslessAudioNegotiated());
}

// R-R3-23 acceptance: real DTLS/SRTP between two real peers carries the
// lossless stream at its own rate, 48 kHz x 2 x 16 bit = 1.536 Mbit/s of
// payload, sent as the Core sends it: ten packets back to back per 40 ms
// capture block. Every packet arrives intact and in order.
void TestMediaTransport::losslessAudioCrossesRealEncryptedLoopback()
{
    LibDataChannelMediaTransport offerer;
    LibDataChannelMediaTransport answerer;
    QString offer;
    QString answer;
    wireExchange(offerer, answerer, &offer, &answer);
    QSignalSpy offerReady(&offerer, &IMediaTransport::ready);
    QSignalSpy answerReady(&answerer, &IMediaTransport::ready);
    QVERIFY(answerer.start({IMediaTransport::Role::Answerer, kTestAudioSsrc}));
    IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, kTestAudioSsrc};
    options.offerLosslessAudio = true;
    QVERIFY(offerer.start(options));
    QTRY_COMPARE_WITH_TIMEOUT(offerReady.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(answerReady.count(), 1, 10000);
    QVERIFY(offerer.losslessAudioNegotiated());

    QList<QByteArray> received;
    connect(&answerer, &IMediaTransport::rtpReceived, this,
            [&received](const QByteArray& packet) { received.append(packet); });

    constexpr int kBlocks = 50; // two seconds of audio
    constexpr int kPackets = kBlocks * PcmAudioCodecConfig::kPacketsPerBlock;
    QVector<float> block(PcmAudioCodecConfig::kBlockFrames * 2);
    for (int index = 0; index < block.size(); ++index) {
        block[index] = static_cast<float>(std::sin(0.01 * index)) * 0.8f;
    }
    const PcmAudioPacketiser packetiser;
    QList<QByteArray> sent;
    int accepted = 0;
    QElapsedTimer wall;
    wall.start();
    for (int b = 0; b < kBlocks; ++b) {
        // Hold the Core's cadence: block b is due at b * 40 ms.
        while (wall.elapsed() < b * 40) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        const QList<QByteArray> packets = packetiser.packetiseBlock(
            block, static_cast<quint16>(b * 10), static_cast<quint32>(b * 1920),
            kTestAudioSsrc);
        QCOMPARE(packets.size(), 10);
        for (const QByteArray& packet : packets) {
            sent.append(packet);
            accepted += offerer.sendRtp(packet) ? 1 : 0;
        }
    }
    const qint64 sendMs = wall.elapsed();
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), kPackets, 10000);
    const qint64 receiveMs = wall.elapsed();
    QCOMPARE(accepted, kPackets);
    QCOMPARE(received, sent);

    quint64 payloadBytes = 0;
    for (const QByteArray& packet : received) {
        const PcmRtpDecodeResult decoded = decodeL16Rtp(packet, kTestAudioSsrc);
        QCOMPARE(decoded.status, OpusAudioCodecStatus::Accepted);
        payloadBytes += PcmAudioCodecConfig::kPayloadBytes;
    }
    // Media time, from the RTP clock: exactly the lossless payload rate.
    const double mediaSeconds = static_cast<double>(kPackets)
        * PcmAudioCodecConfig::kPacketFrames / PcmAudioCodecConfig::kSampleRate;
    const double payloadBitsPerSecond = payloadBytes * 8.0 / mediaSeconds;
    QCOMPARE(payloadBitsPerSecond, 1'536'000.0);
    // Wall time, for the record: the whole stream was carried in about its
    // own duration.
    const double wallMbps = payloadBytes * 8.0 / (receiveMs / 1000.0) / 1e6;
    qInfo("lossless loopback: %d packets, %llu payload bytes, sent over %lld ms, "
          "all received by %lld ms, %.3f Mbit/s payload by wall clock",
          kPackets, static_cast<unsigned long long>(payloadBytes),
          static_cast<long long>(sendMs), static_cast<long long>(receiveMs), wallMbps);
    const std::optional<MediaTransportTelemetry> traffic = answerer.telemetry();
    QVERIFY(traffic.has_value());
    QCOMPARE(traffic->receivedRtpBytes,
             quint64(kPackets) * quint64(PcmAudioCodecConfig::kRtpPacketBytes));
    offerer.stop();
    answerer.stop();
}

QTEST_GUILESS_MAIN(TestMediaTransport)
#include "tst_media_transport.moc"
