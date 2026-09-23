// =================================================================
// tests/tst_media_transport.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// =================================================================

#include "core/session/media/LibDataChannelMediaTransport.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include <chrono>
#include <thread>

using namespace NereusSDR;

namespace {

constexpr quint32 kTestAudioSsrc = 0x4e523301U;

} // namespace

class TestMediaTransport : public QObject {
    Q_OBJECT

private slots:
    // Must stay first: it observes the process before any peer exists.
    void sctpSettingsAppliedOnceBeforeFirstPeer();
    void encryptedPeersCarryDisplayAndRtp();
    void stalledReceiverRefusesDisplayInsteadOfQueueing();
    void queuedDisplayOverflowDropsOldestAndCountsIt();
    void telemetryCountsValidatedTrafficAndResets();
    void boundedInputsRefuseBeforeTransport();
    void stopCancelsOldCallbacksAndRecreates();
    void signalRestartDropsRemainingOldGenerationMedia();
    void deletionFromReceivedSignalIsSafe();

private:
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

    // Offer frames until the sender has refused every one for a full second.
    int accepted = 0;
    int refused = 0;
    QElapsedTimer sinceAccepted;
    sinceAccepted.start();
    QElapsedTimer overall;
    overall.start();
    while (sinceAccepted.elapsed() < 1000 && overall.elapsed() < 60'000) {
        if (offerer.sendDisplay(frame)) {
            ++accepted;
            sinceAccepted.restart();
        } else {
            ++refused;
        }
        QTest::qWait(1);
    }
    QVERIFY2(overall.elapsed() < 60'000, "the sender never started refusing");
    QVERIFY(accepted > 0);
    QVERIFY(refused > 0);

    // Everything handed to the library is either held by this sender (its
    // SCTP send buffer plus the one message libdatachannel queues when that
    // buffer is full) or on the stalled receiver (its SCTP receive window
    // plus the one message blocked in the delivery callback). With
    // libdatachannel's 1 MiB defaults this is about 2 MiB.
    const quint64 submitted = offerer.telemetry()->submittedDisplayPayloadBytes;
    const quint64 bound = quint64(IMediaTransport::kSctpSendBufferBytes)
        + quint64(IMediaTransport::kSctpReceiveBufferBytes)
        + 2 * quint64(kFrameBytes);
    QVERIFY2(submitted <= bound,
             qPrintable(QStringLiteral("sender held %1 bytes in %2 frames, bound %3")
                            .arg(submitted).arg(accepted).arg(bound)));

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

QTEST_GUILESS_MAIN(TestMediaTransport)
#include "tst_media_transport.moc"
