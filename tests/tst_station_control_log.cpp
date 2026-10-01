// no-port-check: NereusSDR-original. Control logging lane: the Core logs
// each control write and command a device sends, each answer with how long
// the Core took, and the gaps between a device's control messages with the
// control channel's send buffer, each rate-limited per device. Logging
// only. Loopback link, no radio, no audio device, never keyed.
// =================================================================
// Modification history (NereusSDR):
//   2026-10-01  J.J. Boyd / KG4VCF  Created (control logging lane).
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/HardwareProfile.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationServer.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

std::unique_ptr<RadioModel> makeStationRadioModel()
{
    auto model = std::make_unique<RadioModel>();
    model->setBoardForTest(HPSDRHW::HermesLite);
    RadioInfo info;
    info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:01");
    info.name = QStringLiteral("Bench HL2");
    info.boardType = HPSDRHW::HermesLite;
    model->setLastRadioInfoForTest(info);
    model->setConnectionStateForTest(ConnectionState::Connected);
    model->addSlice(QStringLiteral("pan-0"));
    return model;
}

// The station's end of the link, with bytes waiting in its send buffer.
class BufferedLoopback : public LoopbackTransport {
public:
    using LoopbackTransport::LoopbackTransport;
    qint64 backlogBytes() const override { return 1234; }
};

// Collects the control log lines.
class ControlLog {
public:
    ControlLog()
    {
        s_lines.clear();
        s_previous = qInstallMessageHandler(&ControlLog::handle);
    }
    ~ControlLog() { qInstallMessageHandler(s_previous); }
    QStringList lines(const QString& prefix) const
    {
        QStringList out;
        for (const QString& line : std::as_const(s_lines)) {
            if (line.startsWith(prefix)) {
                out.append(line);
            }
        }
        return out;
    }
    void clear() { s_lines.clear(); }

private:
    static void handle(QtMsgType type, const QMessageLogContext& context, const QString& msg)
    {
        if (msg.startsWith(QStringLiteral("Control "))) {
            s_lines.append(msg);
            return;
        }
        if (s_previous) {
            s_previous(type, context, msg);
        }
    }
    static inline QStringList s_lines;
    static inline QtMessageHandler s_previous = nullptr;
};

struct Device {
    Device(StationServer* server, QObject* parent)
    {
        app = new LoopbackTransport(QStringLiteral("app"), parent);
        station = new BufferedLoopback(QStringLiteral("station"), server);
        station->linkTo(app);
        server->acceptTransport(station);
        static_cast<void>(QTest::qWaitFor([this]() { return !app->received().isEmpty(); }, 5000));
        app->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("Log phone"),
            {kSessionProtocolMajor}, {})));
        app->sendText(SessionMessages::encode(SessionMessages::authRequest(server->token())));
    }
    bool ready() const
    {
        return QTest::qWaitFor([this]() {
            return app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"));
        }, 5000);
    }
    QList<SessionMessage> messages() const
    {
        QList<SessionMessage> out;
        for (const QByteArray& wire : app->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)) {
                out.append(message);
            }
        }
        return out;
    }
    bool hasResult(SessionMessageKind kind, quint32 id) const
    {
        for (const SessionMessage& message : messages()) {
            if (message.kind == kind
                && (kind == SessionMessageKind::PropertyResult ? message.writeId
                                                               : message.commandId) == id) {
                return true;
            }
        }
        return false;
    }
    SessionMessage writeResult(quint32 writeId) const
    {
        for (const SessionMessage& message : messages()) {
            if (message.kind == SessionMessageKind::PropertyResult && message.writeId == writeId) {
                return message;
            }
        }
        return {};
    }
    // Sends a command and waits for its answer.
    bool invoke(const QByteArray& verb, quint32 id)
    {
        app->sendText(SessionMessages::encode(SessionMessages::commandInvoke(verb, id, {})));
        return QTest::qWaitFor(
            [&]() { return hasResult(SessionMessageKind::CommandResult, id); }, 3000);
    }
    // Sends a write and waits for its answer.
    bool write(const QByteArray& object, const QList<MirrorUpdate>& updates, quint32 writeId)
    {
        app->sendText(
            SessionMessages::encode(SessionMessages::propertyWrite(object, updates, writeId)));
        return QTest::qWaitFor(
            [&]() { return hasResult(SessionMessageKind::PropertyResult, writeId); }, 3000);
    }

    LoopbackTransport* app = nullptr;
    BufferedLoopback* station = nullptr;
};

// The device id as the log prints it: hex, at least two characters.
const QRegularExpression kHexDevice(QStringLiteral("^[0-9a-f]{2,}$"));

} // namespace

class TstStationControlLog : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_securityDir.isValid());
        AppSettings::setProfileOverride(
            QStringLiteral("station-control-log-%1").arg(QCoreApplication::applicationPid()));
        AppSettings::instance().clear();
    }
    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }
    void init()
    {
        AppSettings::instance().clear();
        m_nowMs = 1000;
        m_core = makeStationRadioModel();
        m_server = std::make_unique<StationServer>(
            m_core.get(), AppSettings::instance(),
            NereusSDR::Test::seedUpgradedCoreToken(m_securityDir.path()));
        m_server->setControlLogClockForTest([this]() { return m_nowMs; });
    }
    void cleanup()
    {
        m_server.reset();
        m_core.reset();
    }

    // An accepted write: one line in, with the device in hex, the object,
    // the property, the writeId and the receive time; one line out, with
    // accepted and the handling time on the log's clock.
    void anAcceptedWriteIsLoggedInAndOut()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        const quint32 writeId = 5101;
        QVERIFY(phone.write(QByteArrayLiteral("slice:0"),
                            {MirrorUpdate{0, "frequency", MirrorWireKind::Float64,
                                          QVariant(7074000.0)}},
                            writeId));
        const SessionMessage result = phone.writeResult(writeId);
        QVERIFY(!result.propertyResults.isEmpty());
        QVERIFY2(result.propertyResults.first().accepted,
                 qPrintable(result.propertyResults.first().reason));

        const QStringList in = log.lines(QStringLiteral("Control in from "));
        QCOMPARE(in.size(), 1);
        const QRegularExpression inShape(QStringLiteral(
            "^Control in from ([0-9a-f]+): property.write slice:0 frequency, write 5101, "
            "received \\d\\d:\\d\\d:\\d\\d\\.\\d\\d\\d$"));
        const QRegularExpressionMatch inMatch = inShape.match(in.first());
        QVERIFY2(inMatch.hasMatch(), qPrintable(in.first()));
        QVERIFY(kHexDevice.match(inMatch.captured(1)).hasMatch());

        const QStringList out = log.lines(QStringLiteral("Control out to "));
        QCOMPARE(out, QStringList{QStringLiteral(
                          "Control out to %1: property.result slice:0, write 5101, accepted, "
                          "handled in 0 ms")
                                      .arg(inMatch.captured(1))});
        // The value written is never in the log.
        QVERIFY(!in.first().contains(QStringLiteral("7074")));
    }

    // A refused write: its answer line names the property and the Core's
    // reason.
    void aRefusedWriteIsLoggedWithItsReason()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        const quint32 writeId = 5102;
        QVERIFY(phone.write(QByteArrayLiteral("setup"),
                            {MirrorUpdate{0, "label", MirrorWireKind::Utf8,
                                          QVariant(QStringLiteral("secret words"))}},
                            writeId));
        const QStringList in = log.lines(QStringLiteral("Control in from "));
        QCOMPARE(in.size(), 1);
        QVERIFY2(in.first().contains(QStringLiteral(": property.write setup label, write 5102, ")),
                 qPrintable(in.first()));
        QVERIFY(!in.first().contains(QStringLiteral("secret")));
        const QStringList out = log.lines(QStringLiteral("Control out to "));
        QCOMPARE(out.size(), 1);
        QVERIFY2(out.first().endsWith(QStringLiteral(
                     ": property.result setup, write 5102, refused (label: The Core owns "
                     "Setup descriptions.), handled in 0 ms")),
                 qPrintable(out.first()));
    }

    // A command: in with its verb and commandId, out with the Core's
    // answer and the time it took on the log's clock.
    void aCommandIsLoggedInAndOut()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        QVERIFY(phone.invoke(QByteArrayLiteral("control.notAVerb"), 6101));
        const QStringList in = log.lines(QStringLiteral("Control in from "));
        QCOMPARE(in.size(), 1);
        QVERIFY2(QRegularExpression(QStringLiteral(
                     "^Control in from [0-9a-f]+: command.invoke control.notAVerb, command "
                     "6101, received \\d\\d:\\d\\d:\\d\\d\\.\\d\\d\\d$"))
                     .match(in.first())
                     .hasMatch(),
                 qPrintable(in.first()));
        const QStringList out = log.lines(QStringLiteral("Control out to "));
        QCOMPARE(out.size(), 1);
        QVERIFY2(QRegularExpression(QStringLiteral(
                     "^Control out to [0-9a-f]+: command.result control.notAVerb, command "
                     "6101, refused \\(.+\\), handled in 0 ms$"))
                     .match(out.first())
                     .hasMatch(),
                 qPrintable(out.first()));
    }

    // The handling time is from receipt to the answer, on the log's clock.
    void theHandlingTimeIsReceiptToAnswer()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        // The clock moves 7 ms while the Core handles the command: the
        // answer is read 7 ms after the receipt.
        int reads = 0;
        m_server->setControlLogClockForTest([this, &reads]() {
            return m_nowMs + (reads++ > 0 ? 7 : 0);
        });
        QVERIFY(phone.invoke(QByteArrayLiteral("control.notAVerb"), 6102));
        const QStringList out = log.lines(QStringLiteral("Control out to "));
        QCOMPARE(out.size(), 1);
        m_server->setControlLogClockForTest([this]() { return m_nowMs; });
        QVERIFY2(out.first().endsWith(QStringLiteral(", handled in 7 ms")), qPrintable(out.first()));
    }

    // A gap at or over the threshold between two control messages is
    // logged with both messages' names and the control channel's send
    // buffer; a shorter one is not.
    void aControlGapOverTheThresholdIsLogged()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        QVERIFY(phone.invoke(QByteArrayLiteral("control.first"), 6201));
        m_nowMs += StationServer::kControlGapLogMs - 1;
        QVERIFY(phone.invoke(QByteArrayLiteral("control.second"), 6202));
        QCOMPARE(log.lines(QStringLiteral("Control gap from ")).size(), 0);
        m_nowMs += 402;
        QVERIFY(phone.write(QByteArrayLiteral("setup"),
                            {MirrorUpdate{0, "label", MirrorWireKind::Utf8,
                                          QVariant(QStringLiteral("x"))}},
                            6203));
        const QStringList gaps = log.lines(QStringLiteral("Control gap from "));
        QCOMPARE(gaps.size(), 1);
        QVERIFY2(QRegularExpression(QStringLiteral(
                     "^Control gap from [0-9a-f]+: 402 ms between control.second and "
                     "property.write, 1234 bytes waiting in the control channel's send "
                     "buffer$"))
                     .match(gaps.first())
                     .hasMatch(),
                 qPrintable(gaps.first()));
    }

    // At most kControlGapLinesPerWindow gap lines a window; the rest are
    // counted and the count rides on the next gap line logged.
    void theGapLinesAreRateLimited()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        quint32 id = 6300;
        QVERIFY(phone.invoke(QByteArrayLiteral("control.gap"), ++id));
        const int over = 3;
        for (int i = 0; i < StationServer::kControlGapLinesPerWindow + over; ++i) {
            m_nowMs += 300;
            QVERIFY(phone.invoke(QByteArrayLiteral("control.gap"), ++id));
        }
        QCOMPARE(log.lines(QStringLiteral("Control gap from ")).size(),
                 StationServer::kControlGapLinesPerWindow);
        m_nowMs += StationServer::kControlGapWindowMs;
        QVERIFY(phone.invoke(QByteArrayLiteral("control.gap"), ++id));
        const QStringList gaps = log.lines(QStringLiteral("Control gap from "));
        QCOMPARE(gaps.size(), StationServer::kControlGapLinesPerWindow + 1);
        QVERIFY2(gaps.last().endsWith(QStringLiteral(" buffer (3 more gaps not logged)")),
                 qPrintable(gaps.last()));
    }

    // At most kControlLinesPerSecond messages a second are logged, each
    // with its answer; the rest are counted and the count is logged once
    // the second is over.
    void theControlLinesAreRateLimited()
    {
        Device phone(m_server.get(), this);
        QVERIFY(phone.ready());
        ControlLog log;
        const int over = 5;
        quint32 id = 6400;
        for (int i = 0; i < StationServer::kControlLinesPerSecond + over; ++i) {
            QVERIFY(phone.invoke(QByteArrayLiteral("control.storm"), ++id));
        }
        QCOMPARE(log.lines(QStringLiteral("Control in from ")).size(),
                 StationServer::kControlLinesPerSecond);
        QCOMPARE(log.lines(QStringLiteral("Control out to ")).size(),
                 StationServer::kControlLinesPerSecond);
        QCOMPARE(log.lines(QStringLiteral("Control log for ")).size(), 0);
        m_nowMs += StationServer::kControlLineWindowMs;
        QVERIFY(phone.invoke(QByteArrayLiteral("control.storm"), ++id));
        const QStringList dropped = log.lines(QStringLiteral("Control log for "));
        QCOMPARE(dropped.size(), 1);
        QVERIFY2(QRegularExpression(QStringLiteral(
                     "^Control log for [0-9a-f]+: 5 control messages and their answers not "
                     "logged \\(over 50 a second\\)$"))
                     .match(dropped.first())
                     .hasMatch(),
                 qPrintable(dropped.first()));
        QCOMPARE(log.lines(QStringLiteral("Control in from ")).size(),
                 StationServer::kControlLinesPerSecond + 1);
        QCOMPARE(log.lines(QStringLiteral("Control out to ")).size(),
                 StationServer::kControlLinesPerSecond + 1);
    }

private:
    QTemporaryDir m_securityDir;
    qint64 m_nowMs = 1000;
    std::unique_ptr<RadioModel> m_core;
    std::unique_ptr<StationServer> m_server;
};

QTEST_GUILESS_MAIN(TstStationControlLog)
#include "tst_station_control_log.moc"
