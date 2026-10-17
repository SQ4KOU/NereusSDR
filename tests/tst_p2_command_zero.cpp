// no-port-check: NereusSDR-original in-memory command construction tests.
// Contract: TAPR e7c6584, Protocol 2/Documentation/openHPSDR Ethernet
// Protocol v4.4.pdf: General p20, DDC p26, DUC p29, High Priority p34.
// Each command's four-byte sequence field is explicitly 0x00000000.
// Modification history (NereusSDR):
//   2026-10-14 - J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest/QtTest>
#include <QtEndian>
#include <array>
#include "core/P2RadioConnection.h"

using namespace NereusSDR;

namespace {
class CodecEnvironmentGuard {
public:
    CodecEnvironmentGuard()
        : m_wasSet(qEnvironmentVariableIsSet("NEREUS_USE_LEGACY_P2_CODEC"))
        , m_value(qgetenv("NEREUS_USE_LEGACY_P2_CODEC")) {}
    ~CodecEnvironmentGuard()
    {
        if (m_wasSet) { qputenv("NEREUS_USE_LEGACY_P2_CODEC", m_value); }
        else { qunsetenv("NEREUS_USE_LEGACY_P2_CODEC"); }
    }
private:
    bool m_wasSet;
    QByteArray m_value;
};

using PacketBuilder = QByteArray (P2RadioConnection::*)();
const std::array<PacketBuilder, 4> kBuilders{
    &P2RadioConnection::cmdGeneralPacketForTest,
    &P2RadioConnection::cmdHighPriorityPacketForTest,
    &P2RadioConnection::cmdRxPacketForTest,
    &P2RadioConnection::cmdTxPacketForTest};
constexpr std::array<int, 4> kLengths{60, 1444, 1444, 60};
const std::array<const char*, 4> kCommands{"General", "HighPriority", "Rx", "Tx"};

QByteArray compositionSnapshot(const P2RadioConnection& connection, int command)
{
    std::array<quint8, 1444> bytes{};
    switch (command) {
        case 0: connection.composeCmdGeneralForTest(bytes.data()); break;
        case 1: connection.composeCmdHighPriorityForTest(bytes.data()); break;
        case 2: connection.composeCmdRxForTest(bytes.data()); break;
        case 3: connection.composeCmdTxForTest(bytes.data()); break;
    }
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), kLengths[command]);
}
} // namespace

class TestP2CommandZero : public QObject {
    Q_OBJECT
private:
    const bool m_legacyWasSet = qEnvironmentVariableIsSet("NEREUS_USE_LEGACY_P2_CODEC");
    const QByteArray m_legacyValue = qgetenv("NEREUS_USE_LEGACY_P2_CODEC");
private slots:
    void cleanup()
    {
        QCOMPARE(qEnvironmentVariableIsSet("NEREUS_USE_LEGACY_P2_CODEC"), m_legacyWasSet);
        QCOMPARE(qgetenv("NEREUS_USE_LEGACY_P2_CODEC"), m_legacyValue);
    }

    void repeatedConstruction_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("board");
        QTest::addColumn<int>("command");
        QTest::addColumn<bool>("nonDefault");
        struct Path { const char* name; int board; };
        const std::array<Path, 6> paths{{
            {"NoCodecFallback", -1},
            {"ExplicitLegacyOrionMkII", static_cast<int>(HPSDRHW::OrionMKII)},
            {"DefaultCodecOrionMkII", static_cast<int>(HPSDRHW::OrionMKII)},
            {"DefaultCodecSaturn", static_cast<int>(HPSDRHW::Saturn)},
            {"DefaultCodecHermes", static_cast<int>(HPSDRHW::Hermes)},
            {"DefaultCodecHermesII", static_cast<int>(HPSDRHW::HermesII)}}};
        for (const Path& path : paths) {
            for (int command = 0; command < 4; ++command) {
                for (bool nonDefault : {false, true}) {
                    const QByteArray row = QByteArray(path.name) + "-" + kCommands[command]
                        + (nonDefault ? "-idle-custom" : "-defaults");
                    QTest::newRow(row.constData()) << QString::fromLatin1(path.name)
                        << path.board << command << nonDefault;
                }
            }
        }
    }

    void repeatedConstruction()
    {
        QFETCH(QString, path);
        QFETCH(int, board);
        QFETCH(int, command);
        QFETCH(bool, nonDefault);
        const CodecEnvironmentGuard environment;
        const bool legacy = path == QStringLiteral("ExplicitLegacyOrionMkII");
        qputenv("NEREUS_USE_LEGACY_P2_CODEC", legacy ? "1" : "0");
        // Bare connection only: never init/connect, so no socket or sender.
        P2RadioConnection connection;
        if (board >= 0) { connection.setBoardForTest(static_cast<HPSDRHW>(board)); }
        QCOMPARE(connection.p2Codec() != nullptr, path.startsWith(QStringLiteral("DefaultCodec")));
        QCOMPARE(connection.socketStateForTest(), QAbstractSocket::UnconnectedState);
        QCOMPARE(connection.socketReceiveBufferForTest(), -1); // No socket was constructed.
        if (nonDefault) {
            // Reuse existing idle fixture values: rate192k / 20m receiver
            // (P2 DDC/codec fixtures), drive50 (tst_p2_drive_level_wire),
            // ports45000/46000 (tst_p2_status_telemetry). No MOX/keying.
            connection.setActiveReceiverCount(2);
            connection.setSampleRate(192000);
            connection.setReceiverFrequency(0, 14200000ULL);
            connection.setPreamp(true);
            connection.setAttenuator(10); // Existing setter clamps to the board's wire range.
            connection.setTxDrive(50);
            connection.setPortBasesForTest(45000, 46000);
        }
        const QByteArray snapshot = compositionSnapshot(connection, command);
        for (int attempt = 1; attempt <= 2; ++attempt) {
            const QByteArray packet = (connection.*kBuilders[command])();
            QCOMPARE(packet.size(), kLengths[command]);
            // Payload preservation is supplementary; the zero header's
            // independent oracle is the packet-specific TAPR requirement.
            QCOMPARE(packet.mid(4), snapshot.mid(4));
            const quint32 header = qFromBigEndian<quint32>(packet.constData());
            const QString detail = QStringLiteral("path=%1 board=%2 command=%3 attempt=%4 header=0x%5")
                .arg(path).arg(board).arg(QString::fromLatin1(kCommands[command])).arg(attempt)
                .arg(header, 8, 16, QLatin1Char('0'));
            QVERIFY2(header == 0, qPrintable(detail));
        }
    }
};

QTEST_APPLESS_MAIN(TestP2CommandZero)
#include "tst_p2_command_zero.moc"
