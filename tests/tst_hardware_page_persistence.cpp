// tst_hardware_page_persistence.cpp
//
// Phase 3I Task 21 — persistence round-trip tests for AppSettings hardware
// value API.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestHardwarePagePersistence : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void writeReadRoundTrip()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("hw1.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/sampleRate"),        192000);
        s.setHardwareValue(mac, QStringLiteral("antennaAlex/rxAnt[0]"),        QStringLiteral("ANT2"));
        s.setHardwareValue(mac, QStringLiteral("pureSignal/enabled"),          false);
        s.save();

        AppSettings s2(m_dir.filePath(QStringLiteral("hw1.xml")));
        s2.load();
        QCOMPARE(s2.hardwareValue(mac, QStringLiteral("radioInfo/sampleRate")).toInt(), 192000);
        QCOMPARE(s2.hardwareValue(mac, QStringLiteral("antennaAlex/rxAnt[0]")).toString(),
                 QStringLiteral("ANT2"));
        QCOMPARE(s2.hardwareValue(mac, QStringLiteral("pureSignal/enabled")).toBool(), false);
    }

    void radioSwapPreservesEachRadiosValues()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("hw2.xml")));
        const QString hl2 = QStringLiteral("aa:bb:cc:11:22:33");
        const QString g2  = QStringLiteral("aa:bb:cc:44:55:66");

        // Write HL2 values
        s.setHardwareValue(hl2, QStringLiteral("radioInfo/sampleRate"),           48000);
        s.setHardwareValue(hl2, QStringLiteral("bandwidthMonitor/thresholdMbps"), 80);

        // Write G2 values
        s.setHardwareValue(g2,  QStringLiteral("radioInfo/sampleRate"),   384000);
        s.setHardwareValue(g2,  QStringLiteral("diversity/phase"),        45);

        s.save();

        AppSettings s2(m_dir.filePath(QStringLiteral("hw2.xml")));
        s2.load();

        // HL2 values intact
        QCOMPARE(s2.hardwareValue(hl2, QStringLiteral("radioInfo/sampleRate")).toInt(), 48000);
        QCOMPARE(s2.hardwareValue(hl2, QStringLiteral("bandwidthMonitor/thresholdMbps")).toInt(), 80);

        // G2 values intact
        QCOMPARE(s2.hardwareValue(g2, QStringLiteral("radioInfo/sampleRate")).toInt(), 384000);
        QCOMPARE(s2.hardwareValue(g2, QStringLiteral("diversity/phase")).toInt(), 45);

        // Cross-pollution check: HL2 should not see G2's diversity/phase
        QVERIFY(!s2.hardwareValue(hl2, QStringLiteral("diversity/phase")).isValid());
    }

    void hardwareValuesReturnsNamespacedMap()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("hw3.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/sampleRate"),      192000);
        s.setHardwareValue(mac, QStringLiteral("ocOutputs/txMask[20m][0]"),  true);
        s.save();

        auto all = s.hardwareValues(mac);
        QVERIFY(all.contains(QStringLiteral("radioInfo/sampleRate")));
        QVERIFY(all.contains(QStringLiteral("ocOutputs/txMask[20m][0]")));
        QCOMPARE(all.size(), 2);
    }

    void activeRxCountRoundTrip()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("hw4.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/activeRxCount"), 3);
        s.save();

        AppSettings s2(m_dir.filePath(QStringLiteral("hw4.xml")));
        s2.load();
        QCOMPARE(s2.hardwareValue(mac, QStringLiteral("radioInfo/activeRxCount")).toInt(), 3);
    }
};

QTEST_MAIN(TestHardwarePagePersistence)
#include "tst_hardware_page_persistence.moc"
