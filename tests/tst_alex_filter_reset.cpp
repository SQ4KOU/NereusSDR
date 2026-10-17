// no-port-check: NereusSDR-original behavioral tests; no upstream logic.
#include <QtTest/QtTest>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QScopeGuard>
#include "core/AppSettings.h"
#include "core/codec/AlexFilterMap.h"
#include "core/settings/SettingsProxy.h"
#include "gui/setup/hardware/AntennaAlexAlex1Tab.h"
#include "gui/setup/hardware/AntennaAlexAlex2Tab.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
namespace {
const QString kMac = QStringLiteral("FA:CE:00:00:00:42");
const char* kPreselectors[] = {"1_5MHz", "6_5MHz", "9_5MHz", "13MHz", "20MHz", "6mBP"};
const char* kLpf[] = {"160m", "80m", "40m", "20m", "15m", "10m", "6m"};
const double kHpf[][2] = {{1.8,6.499999},{6.5,9.499999},{9.5,12.999999},{13,19.999999},{20,49.999999},{50,61.44}};
const double kBpf[][2] = {{1.5,2.099999},{2.1,5.499999},{5.5,10.999999},{11,21.999999},{22,34.999999},{35,61.44}};
const double kLpf1[][2] = {{0,2.5},{2.500001,5},{5.000001,8},{8.000001,16.5},{16.500001,24},{24.000001,35.6},{35.600001,61.44}};
const double kLpf2[][2] = {{1.8,2},{3.5,4},{5.33,7.3},{10.1,14.35},{18.068,21.45},{24.89,29.7},{50,54}};
}
class TestAlexFilterReset : public QObject {
    Q_OBJECT
private slots:
    void init() { AppSettings::instance().clearHardwareValues(kMac); }
    void cleanup() { AppSettings::instance().setRemoteBackend(nullptr); AppSettings::instance().clearHardwareValues(kMac); }
    void resetRestoresOnlySelectedTabsEdges_data() {
        QTest::addColumn<bool>("alex2");
        QTest::addColumn<bool>("remote");
        QTest::addColumn<bool>("bpfPanel");
        QTest::newRow("alex1-hpf-local") << false << false << false;
        QTest::newRow("alex1-bpf-local") << false << false << true;
        QTest::newRow("alex2-local") << true << false << false;
        QTest::newRow("alex1-hpf-remote") << false << true << false;
        QTest::newRow("alex1-bpf-remote") << false << true << true;
        QTest::newRow("alex2-remote") << true << true << false;
    }
    void resetRestoresOnlySelectedTabsEdges() {
        QFETCH(bool, alex2);
        QFETCH(bool, remote);
        QFETCH(bool, bpfPanel);
        AppSettings& settings = AppSettings::instance();
        SettingsProxy proxy;
        if (remote) { settings.setRemoteBackend(&proxy); proxy.setReady(true); }
        const auto releaseBackend = qScopeGuard([&settings] { settings.setRemoteBackend(nullptr); });
        QSignalSpy writes(&proxy, &SettingsProxy::outboundWriteRequested);
        const QString bank = alex2 ? QStringLiteral("alex2") : QStringLiteral("alex");
        for (const QString& type : {QStringLiteral("hpf"), QStringLiteral("bpf1"), QStringLiteral("lpf")}) {
            for (int row = 0; row < (type == QStringLiteral("lpf") ? 7 : 6); ++row) {
                const QString slug = QString::fromLatin1(type == QStringLiteral("lpf") ? kLpf[row] : kPreselectors[row]);
                for (const QString& edge : {QStringLiteral("start"), QStringLiteral("end")}) {
                    // Deliberately non-monotonic values expose sequential clamping mistakes.
                    settings.setHardwareValue(kMac, bank + "/" + type + "/" + slug + "/" + edge, 60.0 - row);
                }
                settings.setHardwareValue(kMac, bank + "/" + type + "/" + slug + "/enabled", QStringLiteral("True"));
            }
        }
        if (!alex2) {
            settings.setHardwareValue(kMac, QStringLiteral("alex/lpf/80m/end"), 4.999);
        }
        settings.setHardwareValue(kMac, QStringLiteral("alex/master/hpfBypassOnTx"), QStringLiteral("True"));
        settings.setHardwareValue(kMac, QStringLiteral("alex2/master/bypass55MhzBpf"), QStringLiteral("True"));
        settings.setHardwareValue(kMac, QStringLiteral("alex/antenna/ext2OutOnTx"), QStringLiteral("True"));
        settings.setHardwareValue(kMac, QStringLiteral("options/stepAtt/adc0"), 12);
        settings.setHardwareValue(kMac, QStringLiteral("paCalibration/calPoint1"), 9.5);
        settings.setHardwareValue(kMac, QStringLiteral("pureSignal/AutoCalEnabled"), QStringLiteral("True"));
        const QString other = alex2 ? QStringLiteral("alex/lpf/40m/start") : QStringLiteral("alex2/lpf/40m/start");
        settings.setHardwareValue(kMac, other, 6.1);
        const auto before = settings.hardwareValues(kMac);
        RadioModel model(remote ? RadioModel::Role::Remote : RadioModel::Role::Local);
        AntennaAlexAlex1Tab tab1(&model);
        AntennaAlexAlex2Tab tab2(&model);
        tab1.updateBoardCapabilities(bpfPanel);
        tab1.restoreSettings(kMac); tab2.restoreSettings(kMac);
        if (!alex2) {
            auto* end80 = tab1.findChild<QDoubleSpinBox*>(QStringLiteral("alexLpfEnd_80m"));
            QVERIFY(end80);
            QCOMPARE(end80->value(), 4.999);
        }
        QWidget* tab = alex2 ? static_cast<QWidget*>(&tab2) : static_cast<QWidget*>(&tab1);
        const auto boxes = tab->findChildren<QCheckBox*>();
        QList<bool> checks;
        for (QCheckBox* box : boxes) { checks << box->isChecked(); }
        auto* reset = tab->findChild<QPushButton*>(QStringLiteral("resetFilterDefaults"));
        QVERIFY(reset);
        QVERIFY(reset->isEnabled());
        codec::alex::AlexLpfRows coreLpfRows{};
        if (remote && !alex2) {
            for (int row = 0; row < 7; ++row) {
                coreLpfRows[row] = {
                    settings.hardwareValue(kMac, QStringLiteral("alex/lpf/%1/start").arg(kLpf[row])).toDouble(),
                    settings.hardwareValue(kMac, QStringLiteral("alex/lpf/%1/end").arg(kLpf[row])).toDouble()};
            }
        }
        writes.clear();
        reset->click();
        const auto after = settings.hardwareValues(kMac);
        for (auto it = before.cbegin(); it != before.cend(); ++it) {
            const QString active = bank + (bpfPanel && !alex2 ? "/bpf1/" : "/hpf/");
            const bool selectedEdge = (it.key().startsWith(active) || it.key().startsWith(bank + "/lpf/"))
                && (it.key().endsWith("/start") || it.key().endsWith("/end"));
            if (!selectedEdge) {
                QCOMPARE(after.value(it.key()), it.value());
            }
        }
        for (int i = 0; i < boxes.size(); ++i) { QCOMPARE(boxes[i]->isChecked(), checks[i]); }
        const auto verify = [&](const char* type, const char* const* slugs, const double values[][2], int count) {
            for (int row = 0; row < count; ++row) {
                for (int edge = 0; edge < 2; ++edge) {
                    const QString key = bank + "/" + type + "/" + slugs[row] + (edge ? "/end" : "/start");
                    QCOMPARE(settings.hardwareValue(kMac, key).toDouble(), values[row][edge]);
                    const QString prefix = alex2 ? (QString(type) == "lpf" ? "alex2Lpf" : "alex2Hpf")
                        : (QString(type) == "lpf" ? "alexLpf" : (QString(type) == "bpf1" ? "alexBpf1" : "alexHpf"));
                    const QString name = prefix + (edge ? "End_" : "Start_") + slugs[row];
                    auto* spin = tab->findChild<QDoubleSpinBox*>(name);
                    QVERIFY2(spin, qPrintable(name));
                    QCOMPARE(spin->value(), values[row][edge]);
                }
            }
        };
        if (bpfPanel && !alex2) { verify("bpf1", kPreselectors, kBpf, 6); }
        else { verify("hpf", kPreselectors, alex2 ? kBpf : kHpf, 6); }
        verify("lpf", kLpf, alex2 ? kLpf2 : kLpf1, 7);
        if (remote) {
            QCOMPARE(writes.count(), 26);
            QVERIFY(settings.snapshot({QStringLiteral("hardware/%1/").arg(kMac)}).isEmpty());
            for (const auto& write : writes) {
                const QString key = write[0].toString();
                QVERIFY(key.startsWith(QStringLiteral("hardware/%1/%2/").arg(kMac, bank)));
                QVERIFY(key.endsWith("/start") || key.endsWith("/end"));
                if (!alex2 && key.contains("/alex/lpf/")) {
                    const QString slug = key.section('/', -2, -2);
                    for (int row = 0; row < 7; ++row) {
                        if (slug == QLatin1String(kLpf[row])) {
                            // Replay the ordered writes through the same edge rule the Core uses.
                            codec::alex::applyAlexLpfEdgeEdit(coreLpfRows, row, key.endsWith("/end"), write[1].toDouble());
                        }
                    }
                }
            }
            if (!alex2) {
                for (int row = 0; row < 7; ++row) {
                    QCOMPARE(coreLpfRows[row].startMhz, kLpf1[row][0]);
                    QCOMPARE(coreLpfRows[row].endMhz, kLpf1[row][1]);
                }
            }
        }
    }
    void resetRecoversWhenItsParentIsReenabled() {
        QWidget parent;
        RadioModel model(RadioModel::Role::Remote);
        AntennaAlexAlex1Tab tab1(&model, &parent);
        AntennaAlexAlex2Tab tab2(&model, &parent);
        tab1.restoreSettings(kMac); tab2.restoreSettings(kMac);
        parent.setEnabled(false);
        tab1.setHpfRowsAvailable(true, {});
        tab1.setLpfRowsAvailable(true, {});
        tab2.setHpfRowsAvailable(true, {});
        parent.setEnabled(true);
        auto* reset1 = tab1.findChild<QPushButton*>(QStringLiteral("resetFilterDefaults"));
        auto* reset2 = tab2.findChild<QPushButton*>(QStringLiteral("resetFilterDefaults"));
        QVERIFY(reset1); QVERIFY(reset2);
        QVERIFY(reset1->isEnabled()); QVERIFY(reset2->isEnabled());
        QSignalSpy edits1(&tab1, &AntennaAlexAlex1Tab::settingChanged);
        QSignalSpy edits2(&tab2, &AntennaAlexAlex2Tab::settingChanged);
        QVERIFY(edits1.isValid());
        QVERIFY(edits2.isValid());
        reset1->click(); reset2->click();
        QCOMPARE(edits1.count(), 26); QCOMPARE(edits2.count(), 26);
    }
    void resetHonorsWriteGates() {
        RadioModel model(RadioModel::Role::Remote);
        AntennaAlexAlex1Tab tab1(&model);
        AntennaAlexAlex2Tab tab2(&model);
        tab1.restoreSettings(kMac); tab2.restoreSettings(kMac);
        auto* reset1 = tab1.findChild<QPushButton*>(QStringLiteral("resetFilterDefaults"));
        auto* reset2 = tab2.findChild<QPushButton*>(QStringLiteral("resetFilterDefaults"));
        QVERIFY(reset1); QVERIFY(reset2);
        const QString why = QStringLiteral("This Core cannot change these filters.");
        tab1.setHpfRowsAvailable(false, why); tab2.setHpfRowsAvailable(false, why);
        QVERIFY(!reset1->isEnabled()); QVERIFY(!reset2->isEnabled());
        QCOMPARE(reset1->toolTip(), why); QCOMPARE(reset2->toolTip(), why);
        QSignalSpy changed(&tab1, &AntennaAlexAlex1Tab::settingChanged);
        QVERIFY(changed.isValid());
        reset1->click(); QCOMPARE(changed.count(), 0);
        tab1.setHpfRowsAvailable(true, {});
        QVERIFY(reset1->isEnabled());
        tab1.setLpfRowsAvailable(false, why);
        QVERIFY(!reset1->isEnabled()); QCOMPARE(reset1->toolTip(), why);
        tab1.setLpfRowsAvailable(true, {});
        QVERIFY(reset1->isEnabled());
    }
};
QTEST_MAIN(TestAlexFilterReset)
#include "tst_alex_filter_reset.moc"
