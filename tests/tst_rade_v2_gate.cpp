// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
//   2026-10-12 — J.J. Boyd (KG4VCF); AI-assisted via OpenAI Codex.
// no-port-check: NereusSDR-original.
// Experimental V2 requires opt-in from the process that owns DSP.
#include <QtTest>
#include <QComboBox>
#include <QPushButton>
#include <QJsonArray>
#include <QSignalSpy>
#include <QScopeGuard>
#include "core/AppSettings.h"
#include "core/RadeChannel.h"
#include "core/WdspEngine.h"
#include "core/session/StationCatalog.h"
#include "gui/widgets/VfoWidget.h"
#include "gui/applets/RxApplet.h"
#include "models/RadioModel.h"
#include "models/FilterPresetStore.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestRadeV2Gate : public QObject {
    Q_OBJECT
    bool optedIn() const { return qEnvironmentVariableIntValue("NEREUS_ENABLE_RADE_V2") == 1; }
private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }
    void processOptInIsImmutable()
    {
        const bool enabled = optedIn();
        const QByteArray original = qgetenv("NEREUS_ENABLE_RADE_V2");
        const bool existed = qEnvironmentVariableIsSet("NEREUS_ENABLE_RADE_V2");
        const auto restore = qScopeGuard([&] {
            if (existed) { qputenv("NEREUS_ENABLE_RADE_V2", original); }
            else { qunsetenv("NEREUS_ENABLE_RADE_V2"); }
        });
        RadeChannel channel;
        channel.setVersion(RadeChannel::Version::V2);
        QCOMPARE(channel.start(QStringLiteral("dummy")), enabled);
        channel.stop();
        qputenv("NEREUS_ENABLE_RADE_V2", enabled ? "0" : "1");
        QCOMPARE(channel.start(QStringLiteral("dummy")), enabled);
        RadioModel radio;
        QCOMPARE(radio.radeV2Enabled(), enabled);
        VfoWidget vfo;
        for (QComboBox* combo : vfo.findChildren<QComboBox*>()) {
            if (combo->findText(QStringLiteral("RADE-U")) >= 0) {
                QCOMPARE(combo->findText(QStringLiteral("RADE2-U")) >= 0, enabled);
            }
        }
    }
    void directCodecStartRetainsVersionAndFailsClosed()
    {
        RadeChannel channel;
        channel.setVersion(RadeChannel::Version::V2);
        QCOMPARE(channel.version(), RadeChannel::Version::V2);
        QCOMPARE(channel.start(QStringLiteral("dummy")), optedIn());
        QCOMPARE(channel.isActive(), optedIn());
        channel.startRxWorker();
        QCOMPARE(channel.rxWorkerRunning(), optedIn());
        channel.stopRxWorker();
        if (!optedIn()) {
            channel.setTxSelected(true);
            QSignalSpy output(&channel, &RadeChannel::txModemReady);
            channel.txEncode(QByteArray(1600, '\0'));
            QCOMPARE(output.count(), 0);
            QCOMPARE(channel.radeTxCallCountForTest(), 0);
        }
        channel.stop();
        QCOMPARE(channel.version(), RadeChannel::Version::V2);
        QCOMPARE(channel.start(QStringLiteral("dummy")), optedIn());
        channel.stop();
        channel.setVersion(RadeChannel::Version::V1);
        QVERIFY(channel.start(QStringLiteral("dummy")));
    }
    void localSetterRejectsDisabledV2WithoutFallback()
    {
        RadioModel radio;
        const int id = radio.addSlice();
        SliceModel* slice = radio.sliceById(id);
        QVERIFY(slice);
        const DSPMode original = slice->dspMode();
        slice->setDspMode(DSPMode::RADE2_L);
        QCOMPARE(slice->dspMode(), optedIn() ? DSPMode::RADE2_L : original);
        if (!optedIn()) { QVERIFY(radio.wdspEngine()->radeChannel(id) == nullptr); }
        slice->setDspMode(DSPMode::RADE_L);
        QCOMPARE(slice->dspMode(), DSPMode::RADE_L);
    }
    void restoredV2KeepsStoredIdentityWithoutStartingWhenDisabled()
    {
        RadioModel radio;
        const int id = radio.addSlice();
        SliceModel* slice = radio.sliceById(id);
        QVERIFY(slice);
        const QString key = QStringLiteral("Slice%1/Band20m/DspMode").arg(id);
        AppSettings::instance().setValue(key, 15);
        slice->restoreFromSettings(Band::Band20m);
        QCOMPARE(slice->dspMode(), DSPMode::RADE2_L);
        QCOMPARE(AppSettings::instance().value(key).toInt(), 15);
        if (!optedIn()) {
            QVERIFY(radio.wdspEngine()->radeChannel(id) == nullptr);
            QVERIFY(slice->radeReason().contains(QStringLiteral("NEREUS_ENABLE_RADE_V2=1")));
        }
        if (!optedIn()) {
            QVERIFY(slice->restoreReceiveState(14.2e6, DSPMode::RADE2_L));
            QCOMPARE(slice->dspMode(), DSPMode::RADE2_L);
            QVERIFY(radio.wdspEngine()->radeChannel(id) == nullptr);
        }
        VfoWidget vfo;
        vfo.setRadioModel(&radio);
        vfo.setMode(slice->dspMode());
        RxApplet rx(slice, &radio);
        if (!optedIn()) {
            for (QWidget* widget : {static_cast<QWidget*>(&vfo), static_cast<QWidget*>(&rx)}) {
                for (QComboBox* combo : widget->findChildren<QComboBox*>()) {
                    if (combo->findText(QStringLiteral("RADE-U")) < 0) { continue; }
                    QCOMPARE(combo->findText(QStringLiteral("RADE2-L")), -1);
                    QCOMPARE(combo->currentIndex(), -1);
                    QCOMPARE(combo->placeholderText(), QStringLiteral("RADE2-L"));
                }
            }
        }
        if (!optedIn()) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            for (QWidget* widget : {static_cast<QWidget*>(&vfo), static_cast<QWidget*>(&rx)}) {
                for (QPushButton* button : widget->findChildren<QPushButton*>()) {
                    QVERIFY(!button->property("filterLow").isValid());
                    QVERIFY(!button->toolTip().contains(QStringLiteral("-2350 Hz to -650 Hz")));
                }
            }
        }
        slice->saveToSettings(Band::Band20m);
        QCOMPARE(AppSettings::instance().value(key).toInt(), 15);
    }
    void remoteSetterRequiresCoreAuthorityButAcceptsInboundState()
    {
        RadioModel remote(RadioModel::Role::Remote);
        const int id = remote.addSliceWithStationId(0);
        Q_UNUSED(id);
        SliceModel* slice = remote.sliceById(0);
        QVERIFY(slice);
        const DSPMode original = slice->dspMode();
        slice->setDspMode(DSPMode::RADE2_U);
        QCOMPARE(slice->dspMode(), original);
        slice->setStationApplyProbe([] { return true; });
        slice->setDspMode(DSPMode::RADE2_U);
        QCOMPARE(slice->dspMode(), DSPMode::RADE2_U);
        QVERIFY(!remote.wdspEngine() || remote.wdspEngine()->radeChannel(0) == nullptr);
    }
    void storedV2PresetSurvivesWhileItsChoiceIsHidden()
    {
        RadioModel radio;
        const FilterPreset saved{QStringLiteral("Saved V2"), 710, 2290};
        radio.filterPresetStore()->setPreset(DSPMode::RADE2_U, 0, saved);
        FilterPresetStore reloaded;
        const FilterPreset actual = reloaded.presetsForMode(DSPMode::RADE2_U).first();
        QCOMPARE(actual.name, saved.name);
        QCOMPARE(actual.low, saved.low);
        QCOMPARE(actual.high, saved.high);
        const QJsonObject catalog = StationCatalog::build(StationCatalog::inputsFrom(radio));
        const QJsonObject presets = catalog.value(QStringLiteral("filterPresets")).toObject();
        QCOMPARE(presets.contains(QStringLiteral("RADE2-U")), optedIn());
    }
    void catalogOffersV2OnlyWithExplicitOptIn()
    {
        RadioModel radio;
        const QJsonObject catalog = StationCatalog::build(StationCatalog::inputsFrom(radio));
        QCOMPARE(catalog.value(QStringLiteral("radeV2Enabled")).toBool(), optedIn());
        const QJsonArray modes = catalog.value(QStringLiteral("modes")).toArray();
        QCOMPARE(modes.size(), optedIn() ? 16 : 14);
        for (const QJsonValue& entry : modes) {
            QVERIFY(optedIn() || entry.toObject().value(QStringLiteral("id")).toInt() < 14);
        }
    }
    void desktopChoicesFollowOwnerGate()
    {
        RadioModel radio;
        const int id = radio.addSlice();
        VfoWidget vfo;
        vfo.setRadioModel(&radio);
        RxApplet rx(radio.sliceById(id), &radio);
        for (QWidget* widget : {static_cast<QWidget*>(&vfo), static_cast<QWidget*>(&rx)}) {
            bool foundModes = false;
            for (QComboBox* combo : widget->findChildren<QComboBox*>()) {
                if (combo->findText(QStringLiteral("RADE-U")) < 0) { continue; }
                foundModes = true;
                QCOMPARE(combo->findText(QStringLiteral("RADE2-U")) >= 0, optedIn());
                QCOMPARE(combo->findText(QStringLiteral("RADE2-L")) >= 0, optedIn());
            }
            QVERIFY(foundModes);
        }
    }
};
QTEST_MAIN(TestRadeV2Gate)
#include "tst_rade_v2_gate.moc"
