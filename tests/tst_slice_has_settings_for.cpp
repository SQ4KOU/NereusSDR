// tst_slice_has_settings_for.cpp
//
// no-port-check: Test file references Thetis behavior in commentary only;
// no Thetis source is translated here.
//
// Unit tests for SliceModel::hasSettingsFor(Band) — first-visit probe used
// by RadioModel::onBandButtonClicked to decide between seed and restore.
// Key format matches Phase 3G-10 Stage 2 persistence:
// "Slice<N>/Band<key>/DspMode".

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "models/Band.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestSliceHasSettingsFor : public QObject {
    Q_OBJECT

private:
    static void clearKeys() {
        auto& s = AppSettings::instance();
        for (const QString& band : {
                QStringLiteral("20m"), QStringLiteral("40m"),
                QStringLiteral("60m"), QStringLiteral("XVTR") }) {
            for (const QString& field : {
                    QStringLiteral("DspMode"),
                    QStringLiteral("Frequency"),
                    QStringLiteral("FilterLow") }) {
                s.remove(QStringLiteral("Slice0/Band") + band + QStringLiteral("/") + field);
                s.remove(QStringLiteral("Slice1/Band") + band + QStringLiteral("/") + field);
            }
        }
    }

private slots:
    void init() { clearKeys(); }
    void cleanup() { clearKeys(); }

    void absent_key_returns_false() {
        SliceModel slice(0);
        QVERIFY(!slice.hasSettingsFor(Band::Band20m));
        QVERIFY(!slice.hasSettingsFor(Band::Band40m));
        QVERIFY(!slice.hasSettingsFor(Band::XVTR));
    }

    void present_key_returns_true() {
        SliceModel slice(0);
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice0/Band20m/DspMode"),
                   static_cast<int>(DSPMode::USB));
        QVERIFY(slice.hasSettingsFor(Band::Band20m));
        QVERIFY(!slice.hasSettingsFor(Band::Band40m));
    }

    void per_slice_scoping() {
        SliceModel slice0(0);
        SliceModel slice1(1);
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice0/Band20m/DspMode"),
                   static_cast<int>(DSPMode::USB));
        QVERIFY(slice0.hasSettingsFor(Band::Band20m));
        QVERIFY(!slice1.hasSettingsFor(Band::Band20m));
    }

    void other_keys_present_without_dspmode_returns_false() {
        // Pins the sentinel contract: hasSettingsFor probes DspMode
        // specifically. A partial write that sets Frequency or FilterLow
        // but never stored DspMode (possible via migrateLegacyKeys when
        // VfoDspMode was absent upstream) is treated as "not visited"
        // and the handler will apply the seed.
        SliceModel slice(0);
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice0/Band20m/Frequency"), 14155000.0);
        s.setValue(QStringLiteral("Slice0/Band20m/FilterLow"), -2800);
        QVERIFY(!slice.hasSettingsFor(Band::Band20m));
    }
};

QTEST_MAIN(TestSliceHasSettingsFor)
#include "tst_slice_has_settings_for.moc"
