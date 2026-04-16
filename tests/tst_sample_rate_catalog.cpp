// tests/tst_sample_rate_catalog.cpp
//
// Unit tests for SampleRateCatalog rate lists and resolvers.

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "core/SampleRateCatalog.h"
#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h" // ProtocolVersion

using namespace NereusSDR;

class TestSampleRateCatalog : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    // ── allowedSampleRates ────────────────────────────────────────────────────

    void p1_hermes_allows_48_96_192()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMES);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000}));
    }

    void p2_anan_g2_allows_all_six_p2_rates()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000, 384000, 768000, 1536000}));
    }

    void p1_redpitaya_gets_extra_384()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::REDPITAYA);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::REDPITAYA);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000, 384000}));
    }

    void p1_orionmkii_does_not_get_extra_384()
    {
        // OrionMKII and RedPitaya share HPSDRHW::OrionMKII but only
        // REDPITAYA gets the extra 384k on P1 (setup.cs:847 flag).
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ORIONMKII);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::ORIONMKII);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000}));
    }

    void p2_redpitaya_gets_full_p2_list()
    {
        // RedPitaya is P1-special (extra 384k per setup.cs:847) but on
        // Protocol 2 it falls through to the generic P2 list (setup.cs:850).
        // Hand-build caps rather than relying on registry mapping, since
        // REDPITAYA shares HPSDRHW with OrionMKII.
        BoardCapabilities caps{};
        caps.sampleRates  = {48000, 96000, 192000, 384000, 768000, 1536000};
        caps.maxReceivers = 2;
        caps.maxSampleRate = 1536000;
        const auto got = allowedSampleRates(ProtocolVersion::Protocol2, caps, HPSDRModel::REDPITAYA);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000, 384000, 768000, 1536000}));
    }

    void caps_sample_rates_intersect_with_master_list()
    {
        // Hand-built caps with only 48k and 192k populated; should drop
        // 96k even though the master P1 list has it.
        BoardCapabilities caps{};
        caps.sampleRates  = {48000, 192000, 0, 0, 0, 0};
        caps.maxReceivers = 1;
        caps.maxSampleRate = 192000;
        const auto got = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES);
        QCOMPARE(got, std::vector<int>({48000, 192000}));
    }

    void empty_caps_sample_rates_returns_empty()
    {
        BoardCapabilities caps{};
        caps.sampleRates = {0, 0, 0, 0, 0, 0};
        const auto got = allowedSampleRates(ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2);
        QVERIFY(got.empty());
    }

    // ── defaultSampleRate ─────────────────────────────────────────────────────

    void default_is_192k_when_present()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(defaultSampleRate(ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2), 192000);
    }

    void default_falls_back_to_first_when_192k_missing()
    {
        BoardCapabilities caps{};
        caps.sampleRates = {48000, 96000, 0, 0, 0, 0};
        const auto got = defaultSampleRate(ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES);
        QCOMPARE(got, 48000);
    }

    // ── bufferSizeForRate ─────────────────────────────────────────────────────

    void buffer_size_matches_thetis_formula()
    {
        // cmsetup.c:104-111 — base_size * rate / base_rate, base_size=64, base_rate=48000.
        QCOMPARE(bufferSizeForRate(48000),   64);
        QCOMPARE(bufferSizeForRate(96000),   128);
        QCOMPARE(bufferSizeForRate(192000),  256);
        QCOMPARE(bufferSizeForRate(384000),  512);
        QCOMPARE(bufferSizeForRate(768000),  1024);
        QCOMPARE(bufferSizeForRate(1536000), 2048);
    }

    // ── resolveSampleRate ─────────────────────────────────────────────────────

    void resolve_returns_persisted_when_valid()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r1.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/sampleRate"), 384000);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(resolveSampleRate(s, mac, ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2), 384000);
    }

    void resolve_falls_back_to_default_when_unset()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r2.xml")));
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMES);
        QCOMPARE(resolveSampleRate(s, QStringLiteral("aa:bb:cc:11:22:33"),
                                    ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES),
                 192000);
    }

    void resolve_falls_back_when_persisted_not_in_allowed()
    {
        // User persisted 1.536M for ANAN-G2, then plugs in an HL2 (caps max 192k).
        AppSettings s(m_dir.filePath(QStringLiteral("r3.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/sampleRate"), 1536000);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMESLITE);
        QCOMPARE(resolveSampleRate(s, mac, ProtocolVersion::Protocol1, caps, HPSDRModel::HERMESLITE),
                 192000);
    }

    // ── resolveActiveRxCount ──────────────────────────────────────────────────

    void resolve_rx_count_returns_persisted_when_in_range()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r4.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/activeRxCount"), 3);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(resolveActiveRxCount(s, mac, caps), 3);
    }

    void resolve_rx_count_clamps_to_max_receivers()
    {
        // User persisted 7 while on G2, then swapped to HL2 (max 2).
        AppSettings s(m_dir.filePath(QStringLiteral("r5.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/activeRxCount"), 7);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMESLITE);
        QCOMPARE(resolveActiveRxCount(s, mac, caps), caps.maxReceivers);
    }

    void resolve_rx_count_defaults_to_1_when_unset()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r6.xml")));
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(resolveActiveRxCount(s, QStringLiteral("aa:bb:cc:11:22:33"), caps), 1);
    }
};

QTEST_MAIN(TestSampleRateCatalog)
#include "tst_sample_rate_catalog.moc"
