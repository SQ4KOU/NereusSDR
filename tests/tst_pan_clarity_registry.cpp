// no-port-check: NereusSDR-original ownership regressions, GPLv3.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex:
//   Per-pan requested choices, master/reset scope and callback safety.
// 2026-10-05 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// 2026-10-10 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code:
//   Display pill plan Task 12, the level kept through a key, the hold, the
//   re-centre, priming at the real cadence, calibration and the row slew.
#include <QtTest>
#include <cmath>
#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"
#include "core/ClarityController.h"
#include "core/FFTEngine.h"
#include "gui/PanClarityRegistry.h"
#include "gui/SpectrumWidget.h"
#include "gui/SpectrumOverlayPanel.h"
#include "models/RadioModel.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"
#include <memory>
using namespace NereusSDR;
class TestPanClarityRegistry : public QObject {
    Q_OBJECT
    using Source = PanClarityRegistry::SourceAssociation;
private slots:
    // Flat bins: the percentile floor of such a frame is its level.
    static QVector<float> flat(float dbm) { return QVector<float>(1024, dbm); }
    // What ClarityController's smoothing makes of one reading `dtMs` after
    // the last (time constant 3 s).
    static float smoothedStep(float kept, float reading, qint64 dtMs)
    {
        const float alpha = 1.0f - std::exp(-(float(dtMs) / 1000.0f) / 3.0f);
        return kept + alpha * (reading - kept);
    }
private slots:
    void statusRefreshAnnouncesFlagChangesWithoutLevelWritesAndDeduplicates()
    {
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        SpectrumWidget widget;
        widget.setClarityEnabled(true);
        SliceModel slice;
        // Compile-safe while the registry has no status signal: missing
        // signal is RED, then the same case exercises its actual semantics.
        const int signalIndex = registry.metaObject()->indexOfSignal("panStatusChanged(QString)");
        QVERIFY2(signalIndex >= 0, "registry has no GUI status-change refresh signal");
        QSignalSpy changed(&registry, registry.metaObject()->method(signalIndex));
        QSignalSpy levels(&widget, &SpectrumWidget::displaySettingChanged);
        registry.registerPan("p", &widget, nullptr);
        QVERIFY(changed.count() > 0);
        changed.clear();
        registry.registerPan("p", &widget, nullptr);
        QCOMPARE(changed.count(), 0);
        const auto token = registry.bindRemote("p", &widget, &slice,
            {0, 0, 14'225'000, 192'000}, 1, 1, 1);
        registry.setRemoteAvailable("p", token, false);
        changed.clear(); levels.clear();
        registry.setRemoteAvailable("p", token, true);
        QCOMPARE(changed.count(), 1);
        QVERIFY(registry.statusForPan("p")->sourceAvailable);
        QCOMPARE(levels.count(), 0);
        registry.setRemoteAvailable("p", token, true);
        QCOMPARE(changed.count(), 1);
        changed.clear();
        registry.setKeyed(true);
        QCOMPARE(changed.count(), 1);
        QVERIFY(registry.statusForPan("p")->paused);
        registry.setKeyed(true);
        QCOMPARE(changed.count(), 1);
        changed.clear();
        registry.setRemoteAvailable("p", token, false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!registry.statusForPan("p")->sourceAvailable);
        changed.clear();
        registry.retirePan("p");
        QCOMPARE(changed.count(), 1);
        QVERIFY(!registry.statusForPan("p").has_value());
        registry.setRemoteAvailable("p", token, true);
        QCOMPARE(changed.count(), 1); // stale recipient never refreshes GUI status
    }

    void init()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("True"));
        // No band starts with a stored floor left by an earlier case.
        const QStringList keys = settings.allKeys();
        for (const QString& key : keys) {
            if (key.startsWith(QLatin1String("DisplayBandNFEstimate_"))
                || key.startsWith(QLatin1String("ClarityFloor_"))
                || key.startsWith(QLatin1String("ClarityEnabled_"))) {
                settings.remove(key);
            }
        }
    }
    void registeredPansKeepTheirRequestedChoices()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled_1"), QStringLiteral("False"));
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        SpectrumWidget first, second;
        first.setPanIndex(1); first.loadSettings();
        second.setPanIndex(2); second.loadSettings();
        registry.registerPan("first", &first, nullptr);
        registry.registerPan("second", &second, nullptr);
        QVERIFY(!registry.controllerForPan("first")->isEnabled());
        QVERIFY(registry.controllerForPan("second")->isEnabled());
        QVERIFY(model.clarityEnabled());
        QVERIFY(!first.clarityActive());
        QVERIFY(!second.clarityActive());
        settings.remove(QStringLiteral("ClarityEnabled_1"));
    }
    void ownClarityRunsUnderFalseMasterLocallyAndRemotely()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("ClarityEnabled_1"), QStringLiteral("True"));
        settings.setValue(QStringLiteral("ClarityEnabled_2"), QStringLiteral("True"));
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        SpectrumWidget local, remote, inherited;
        local.setPanIndex(1); local.loadSettings();
        remote.setPanIndex(2); remote.loadSettings();
        inherited.setPanIndex(3); inherited.loadSettings();
        SliceModel localSlice, remoteSlice(1);
        localSlice.setStreamIndex(0); remoteSlice.setStreamIndex(1);
        NereusSDR::FFTEngine engine(0);
        registry.registerPan("local", &local, nullptr);
        registry.registerPan("remote", &remote, nullptr);
        registry.registerPan("inherited", &inherited, nullptr);
        QVERIFY(registry.controllerForPan("local")->isEnabled());
        QVERIFY(registry.controllerForPan("remote")->isEnabled());
        QVERIFY(!registry.controllerForPan("inherited")->isEnabled());
        registry.bindLocal("local", &localSlice, &engine, {0,0,14225000,192000});
        const auto token = registry.bindRemote("remote", &remote, &remoteSlice,
            {1,0,14225000,192000},1,1,1);
        emit engine.fftReady(0, flat(-125));
        registry.feedRemoteFloor("remote", &remote, token, -115, 1000);
        QVERIFY(local.clarityActive());
        QVERIFY(remote.clarityActive());
        QCOMPARE(local.clarityFloorDbm(), -125.0f);
        QCOMPARE(remote.clarityFloorDbm(), -115.0f);
        QVERIFY(!model.clarityEnabled());
        settings.remove(QStringLiteral("ClarityEnabled_1"));
        settings.remove(QStringLiteral("ClarityEnabled_2"));
    }
    void sameValueMasterBroadcastReachesOwnChoiceAndResetRestoresIt()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("ClarityEnabled_1"), QStringLiteral("True"));
        settings.setValue(QStringLiteral("ClarityEnabled_9"), QStringLiteral("True"));
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        SpectrumWidget widget;
        widget.setPanIndex(1); widget.loadSettings();
        registry.registerPan("p", &widget, nullptr);
        QVERIFY(registry.controllerForPan("p")->isEnabled());
        registry.setEnabled(false);
        QVERIFY(!registry.controllerForPan("p")->isEnabled());
        widget.saveSettings();
        QVERIFY(!settings.contains(QStringLiteral("ClarityEnabled_1")));
        QVERIFY(!settings.contains(QStringLiteral("ClarityEnabled_9")));
        QVERIFY(!model.clarityEnabled());
        PanDisplayKeys::writeDefault(QStringLiteral("ClarityEnabled"), QStringLiteral("True"));
        widget.resetPanDisplayOverrides();
        QVERIFY(registry.controllerForPan("p")->isEnabled());
        QVERIFY(!widget.clarityActive());
    }
    void newerMasterBroadcastWinsWhenActiveClearReenters()
    {
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        SpectrumWidget first, second;
        first.setPanIndex(1); first.loadSettings();
        second.setPanIndex(2); second.loadSettings();
        SliceModel firstSlice, secondSlice(1);
        firstSlice.setStreamIndex(0); secondSlice.setStreamIndex(1);
        registry.registerPan("first", &first, nullptr);
        registry.registerPan("second", &second, nullptr);
        const auto firstToken = registry.bindRemote("first", &first, &firstSlice,
            {0,0,14225000,192000},1,1,1);
        const auto secondToken = registry.bindRemote("second", &second, &secondSlice,
            {1,0,14225000,192000},1,2,1);
        registry.feedRemoteFloor("first", &first, firstToken, -125, 1000);
        registry.feedRemoteFloor("second", &second, secondToken, -115, 1000);
        QVERIFY(first.clarityActive()); QVERIFY(second.clarityActive());
        bool reentered = false;
        const auto reenable = [&](bool active) {
            if (!active && !reentered) {
                reentered = true;
                registry.setEnabled(true);
            }
        };
        connect(&first, &SpectrumWidget::clarityActiveChanged, &registry, reenable);
        connect(&second, &SpectrumWidget::clarityActiveChanged, &registry, reenable);
        registry.setEnabled(false);
        QVERIFY(reentered);
        QVERIFY(first.clarityEnabled()); QVERIFY(second.clarityEnabled());
        QVERIFY(registry.controllerForPan("first")->isEnabled());
        QVERIFY(registry.controllerForPan("second")->isEnabled());
        QCOMPARE(AppSettings::instance().value(QStringLiteral("ClarityEnabled")).toString(),
                 QStringLiteral("True"));
    }
    void requestedDisableInsideReadingCanDeleteItsRegistry()
    {
        SpectrumWidget widget;
        widget.setPanIndex(1); widget.loadSettings();
        SliceModel slice; slice.setStreamIndex(0);
        auto registry = std::make_unique<PanClarityRegistry>(nullptr);
        registry->registerPan("p", &widget, nullptr);
        const auto token = registry->bindRemote("p", &widget, &slice,
            {0,0,14225000,192000},1,1,1);
        QPointer<ClarityController> controller(registry->controllerForPan("p"));
        registry->feedRemoteFloor("p", &widget, token, -130, 1000);
        QVERIFY(widget.clarityActive());
        connect(&widget, &SpectrumWidget::clarityActiveChanged, &widget, [&](bool active) {
            if (!active) { registry.reset(); }
        });
        bool survived = false;
        connect(controller, &ClarityController::noiseFloorChanged, &widget, [&](float) {
            widget.setClarityEnabled(false);
            survived = bool(controller);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(controller);
        });
        registry->feedRemoteFloor("p", &widget, token, -90, 1500);
        QVERIFY(!registry); QVERIFY(survived); QVERIFY(controller);
        QVERIFY(!widget.clarityEnabled()); QVERIFY(!widget.clarityActive());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!controller);
    }
    void perPanResetInsideReadingSurvivesRebindAndRejectsTail()
    {
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        SpectrumWidget widget;
        widget.setPanIndex(1); widget.loadSettings();
        SliceModel first, second(1);
        first.setStreamIndex(0); second.setStreamIndex(0);
        const Source source{0,0,14225000,192000};
        registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &first, source,1,1,1);
        auto* controller = registry.controllerForPan("p");
        bool changed = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (changed) { return; }
            changed = true;
            PanDisplayKeys::writeDefault(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
            widget.resetPanDisplayOverrides();
            token = registry.bindRemote("p", &widget, &second, source,1,2,2);
        });
        registry.feedRemoteFloor("p", &widget, token, -130, 1000);
        QVERIFY(changed);
        QVERIFY(!controller->isEnabled());
        QVERIFY(!widget.clarityActive());
        QVERIFY(model.clarityEnabled());
        registry.feedRemoteFloor("p", &widget, token, -90, 2000);
        QVERIFY(!widget.clarityActive());
    }
    void ownClaritySurvivesDifferentSourceResetAndUnkey()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("ClarityEnabled_1"), QStringLiteral("True"));
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        qint64 now = 1000;
        registry.setClockForTest([&] { return now; });
        SpectrumWidget widget;
        widget.setPanIndex(1); widget.loadSettings();
        SliceModel first, second(1);
        first.setStreamIndex(0); second.setStreamIndex(1);
        registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &first, {0,0,14225000,192000},1,1,1);
        auto* controller = registry.controllerForPan("p");
        bool rebound = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (rebound) { return; }
            rebound = true;
            token = registry.bindRemote("p", &widget, &second, {1,0,7100000,192000},1,2,2);
        });
        registry.feedRemoteFloor("p", &widget, token, -130);
        QVERIFY(rebound);
        QVERIFY(controller->isEnabled());
        QVERIFY(!widget.clarityActive());
        now += 500;
        registry.feedRemoteFloor("p", &widget, token, -100);
        QCOMPARE(widget.clarityFloorDbm(), -100.0f);
        QVERIFY(widget.clarityActive());
        registry.setKeyed(true);
        registry.setKeyed(false);
        QVERIFY(widget.clarityActive());
        now += 1500;
        registry.feedRemoteFloor("p", &widget, token, -90);
        QVERIFY(controller->isEnabled());
        QVERIFY(widget.clarityActive());
        QVERIFY(!model.clarityEnabled());
        settings.remove(QStringLiteral("ClarityEnabled_1"));
    }
    void perPanResetOutputCallbackCanDeleteRegistry()
    {
        SpectrumWidget widget;
        widget.setPanIndex(1); widget.loadSettings();
        auto registry = std::make_unique<PanClarityRegistry>(nullptr);
        registry->setEnabled(true);
        registry->registerPan("p", &widget, nullptr);
        QPointer<ClarityController> controller(registry->controllerForPan("p"));
        controller->notifyManualOverride();
        connect(&widget, &SpectrumWidget::clarityActiveChanged, &widget, [&](bool active) {
            if (!active) { registry.reset(); }
        });
        widget.setClarityActive(true);
        PanDisplayKeys::writeDefault(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        widget.resetPanDisplayOverrides();
        QVERIFY(!registry);
        QVERIFY(!widget.clarityActive());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!controller);
    }
    void masterTransitionsSurviveSameSourceRebind_data()
    {
        QTest::addColumn<bool>("enableInsideCallback");
        QTest::newRow("on-after-unwind") << false;
        QTest::newRow("queued-off-and-on") << true;
    }
    void masterTransitionsSurviveSameSourceRebind()
    {
        QFETCH(bool, enableInsideCallback);
        PanClarityRegistry registry(nullptr); registry.setEnabled(true);
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(0);
        const Source source{0,0,14225000,192000};
        registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &first, source,1,1,1);
        auto* controller = registry.controllerForPan("p");
        QSignalSpy thresholds(controller, &ClarityController::waterfallThresholdsChanged);
        bool changed = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (changed) { return; }
            changed = true;
            registry.setEnabled(false);
            token = registry.bindRemote("p", &widget, &second, source,1,2,2);
            if (enableInsideCallback) { registry.setEnabled(true); }
        });
        registry.feedRemoteFloor("p", &widget, token,-130,1000);
        QVERIFY(changed); QVERIFY(!widget.clarityActive());
        QCOMPARE(controller->isEnabled(), enableInsideCallback);
        if (!enableInsideCallback) { registry.setEnabled(true); }
        registry.feedRemoteFloor("p", &widget, token,-130,2000);
        QVERIFY(widget.clarityActive());
        QCOMPARE(thresholds.count(), 2);
        QCOMPARE(widget.wfActiveLowThreshold(), -135.0f);
        QCOMPARE(widget.wfActiveHighThreshold(), -75.0f);
    }
    void retuneSurvivesSameSourceRebind()
    {
        PanClarityRegistry registry(nullptr); registry.setEnabled(true);
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(0);
        const Source source{0,0,14225000,192000};
        registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &first, source,1,1,1);
        auto* controller = registry.controllerForPan("p");
        bool changed = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (changed) { return; }
            changed = true;
            controller->notifyManualOverride();
            registry.retunePan("p");
            // The queued old recipient sample must still be rejected.
            registry.feedRemoteFloor("p", &widget, token,-50,2000);
            token = registry.bindRemote("p", &widget, &second, source,1,2,2);
        });
        registry.feedRemoteFloor("p", &widget, token,-130,1000);
        QVERIFY(changed); QVERIFY(!controller->isPaused());
        QVERIFY(!widget.clarityActive());
        QCOMPARE(controller->smoothedFloor(), -130.0f);
        registry.feedRemoteFloor("p", &widget, token,-90,2000);
        QCOMPARE(controller->smoothedFloor(), -90.0f);
        QCOMPARE(widget.wfActiveLowThreshold(), -95.0f);
    }
    void keyedTransitionsSurviveSameSourceRebind_data()
    {
        QTest::addColumn<bool>("unkeyInsideCallback");
        QTest::newRow("keyed-after-unwind") << false;
        QTest::newRow("queued-key-and-unkey") << true;
    }
    void keyedTransitionsSurviveSameSourceRebind()
    {
        QFETCH(bool, unkeyInsideCallback);
        PanClarityRegistry registry(nullptr); registry.setEnabled(true);
        qint64 now = 1000; registry.setClockForTest([&] { return now; });
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(0);
        const Source source{0,0,14225000,192000};
        registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &first, source,1,1,1);
        auto* controller = registry.controllerForPan("p");
        QSignalSpy paused(controller, &ClarityController::pausedChanged);
        bool changed = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (changed) { return; }
            changed = true;
            registry.setKeyed(true);
            token = registry.bindRemote("p", &widget, &second, source,1,2,2);
            if (unkeyInsideCallback) { registry.setKeyed(false); }
        });
        registry.feedRemoteFloor("p", &widget, token,-130);
        // The key arrived inside the reading that would have shown the first
        // level, so the pan has none yet.
        QVERIFY(changed); QVERIFY(!widget.clarityActive());
        QCOMPARE(controller->isTransmitting(), !unkeyInsideCallback);
        QCOMPARE(paused.count(), unkeyInsideCallback ? 2 : 1);
        QCOMPARE(paused.at(0).at(0).toBool(), true);
        if (unkeyInsideCallback) { QCOMPARE(paused.at(1).at(0).toBool(), false); }
        else { registry.setKeyed(false); }
        // The un-key started the hold (one second with no provider): a
        // reading inside it is not taken, the first one after it is.
        QVERIFY(controller->inHold(now));
        now = 1900; registry.feedRemoteFloor("p", &widget, token,-90);
        QVERIFY(!widget.clarityActive());
        QCOMPARE(controller->smoothedFloor(), -130.0f);
        now = 2500; registry.feedRemoteFloor("p", &widget, token,-90);
        QVERIFY(widget.clarityActive());
        QCOMPARE(controller->smoothedFloor(), smoothedStep(-130.0f, -90.0f, 500));
    }
    void queuedUnkeyRetainsAuthoritativeLocalFence()
    {
        PanClarityRegistry registry(nullptr); registry.setEnabled(true);
        qint64 now = 1000; registry.setClockForTest([&] { return now; });
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(0);
        bool actualMox = false;
        registry.setLocalKeyedQueryForTest([&] { return actualMox; });
        registry.registerPan("p", &widget, nullptr);
        const Source source{0,0,14225000,192000};
        auto token = registry.bindRemote("p", &widget, &first, source,1,1,1);
        auto* controller = registry.controllerForPan("p");
        QSignalSpy paused(controller, &ClarityController::pausedChanged);
        bool changed = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (changed) { return; }
            changed = true;
            registry.setKeyed(true);
            token = registry.bindRemote("p", &widget, &second, source,1,2,2);
            actualMox = true;
            registry.setKeyed(false); // older display edge cannot unkey local truth
        });
        registry.feedRemoteFloor("p", &widget, token,-130);
        QVERIFY(changed); QVERIFY(controller->isTransmitting());
        QVERIFY(!widget.clarityActive()); QCOMPARE(paused.count(), 1);
        now = 2000; registry.feedRemoteFloor("p", &widget, token,-90);
        QCOMPARE(controller->smoothedFloor(), -130.0f);
        // The older display edge started no hold: the radio was still keyed.
        QVERIFY(!controller->inHold(now));
        actualMox = false; registry.setKeyed(false);
        QVERIFY(!controller->isTransmitting()); QCOMPARE(paused.count(), 2);
        // The real un-key starts the hold; the level follows after it.
        QVERIFY(controller->inHold(now));
        registry.feedRemoteFloor("p", &widget, token,-90);
        QVERIFY(!widget.clarityActive());
        now = 3500; registry.feedRemoteFloor("p", &widget, token,-90);
        QVERIFY(widget.clarityActive());
    }
    void retiredOwnerRejectsQueuedControls()
    {
        PanClarityRegistry registry(nullptr); registry.setEnabled(true);
        SpectrumWidget oldWidget, replacement; SliceModel slice; slice.setStreamIndex(0);
        registry.registerPan("p", &oldWidget, nullptr);
        const auto token = registry.bindRemote("p", &oldWidget, &slice,{0,0,1,2},1,1,1);
        QPointer<ClarityController> oldController(registry.controllerForPan("p"));
        bool changed = false;
        connect(oldController, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            changed = true;
            oldController->notifyManualOverride();
            registry.retunePan("p"); registry.setKeyed(true); registry.setKeyed(false);
            registry.setEnabled(false);
            registry.retirePan("p");
            // This fixture constructed the replacement before the master
            // write. Seed its requested choice; registration honors it.
            replacement.setClarityEnabled(false);
            registry.registerPan("p", &replacement, nullptr);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(oldController);
        });
        registry.feedRemoteFloor("p", &oldWidget, token,-130,1000);
        QVERIFY(changed); QVERIFY(oldController); QVERIFY(oldController->isPaused());
        QVERIFY(!oldController->isTransmitting()); QVERIFY(oldController->isEnabled());
        QVERIFY(!registry.controllerForPan("p")->isEnabled());
        QVERIFY(!registry.controllerForPan("p")->isPaused());
        QVERIFY(!registry.controllerForPan("p")->isTransmitting());
        QVERIFY(!replacement.clarityActive());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!oldController);
    }
    void differentSourceDuringFloorCannotCommitOldTail()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(1);
        registry.registerPan("p", &widget, nullptr);
        auto token=registry.bindRemote("p", &widget, &first, {0,0,14225000,192000},1,1,1);
        auto* owner=registry.controllerForPan("p");
        bool replaced=false;
        connect(owner, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (replaced) { return; }
            replaced=true;
            token=registry.bindRemote("p", &widget, &second,{1,0,7100000,192000},1,2,2);
            registry.feedRemoteFloor("p", &widget, token,-100,1500);
        });
        registry.feedRemoteFloor("p", &widget, token,-140,1000);
        QCOMPARE(widget.wfActiveLowThreshold(),-105.0f);
        QCOMPARE(owner->smoothedFloor(),-100.0f);
        ClarityController reference; reference.setEnabled(true);
        reference.feedNoiseFloor(-100,1500); reference.feedNoiseFloor(-90,2000);
        registry.feedRemoteFloor("p", &widget, token,-90,2000);
        QCOMPARE(owner->smoothedFloor(),reference.smoothedFloor());
        QCOMPARE(owner->lastLow(),reference.lastLow());
    }
    void rebindWithoutNestedInputResetsAfterUnwind()
    {
        PanClarityRegistry registry(nullptr); registry.setEnabled(true);
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(1);
        registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &first, {0,0,14225000,192000},1,1,1);
        auto* controller = registry.controllerForPan("p");
        bool rebound = false;
        connect(controller, &ClarityController::noiseFloorChanged, &registry, [&](float) {
            if (rebound) { return; }
            rebound = true;
            token = registry.bindRemote("p", &widget, &second, {1,0,7100000,192000},1,2,2);
        });
        registry.feedRemoteFloor("p", &widget, token, -140, 1000);
        QVERIFY(!widget.clarityActive());
        registry.feedRemoteFloor("p", &widget, token, -100, 1100);
        QCOMPARE(controller->smoothedFloor(), -100.0f);
        QCOMPARE(widget.wfActiveLowThreshold(), -105.0f);
    }
    void executingControllerSurvivesRegistryDeletion()
    {
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0);
        auto registry=std::make_unique<PanClarityRegistry>(nullptr);
        registry->setEnabled(true); registry->registerPan("p", &widget,nullptr);
        const auto token=registry->bindRemote("p", &widget,&slice,{0,0,1,2},1,1,1);
        QPointer<ClarityController> owner(registry->controllerForPan("p"));
        bool survivedCallback=false;
        connect(owner,&ClarityController::noiseFloorChanged,&widget,[&](float) {
            registry.reset(); survivedCallback=bool(owner);
        });
        registry->feedRemoteFloor("p", &widget,token,-140,1000);
        QVERIFY(survivedCallback); QVERIFY(owner); QVERIFY(!widget.clarityActive());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(owner.isNull());
    }
    void pausedOperationSurvivesOwnerDeletion()
    {
        QObject widgetOwner;
        SpectrumWidget widget; SliceModel slice;
        auto registry = std::make_unique<PanClarityRegistry>(nullptr);
        registry->setEnabled(true); registry->registerPan("p", &widget, nullptr);
        QPointer<ClarityController> controller(registry->controllerForPan("p"));
        controller->notifyManualOverride();
        bool alive = false;
        connect(controller, &ClarityController::pausedChanged, &widgetOwner, [&](bool paused) {
            if (!paused) { registry.reset(); alive = bool(controller); }
        });
        registry->retunePan("p");
        QVERIFY(alive); QVERIFY(controller);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!controller);
    }
    void hintOperationSurvivesContainingOwnerDeletion()
    {
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0);
        slice.setFrequency(14225000);
        PanadapterModel saved; saved.setBandNFEstimate(Band::Band40m, -100);
        auto containingOwner = std::make_unique<QObject>();
        QPointer<PanClarityRegistry> registry(new PanClarityRegistry(nullptr, containingOwner.get()));
        registry->setEnabled(true); registry->registerPan("p", &widget, nullptr, &saved);
        registry->bindRemote("p", &widget, &slice, {0,0,14225000,192000},1,1,1);
        QPointer<ClarityController> controller(registry->controllerForPan("p"));
        bool alive = false;
        connect(controller, &ClarityController::waterfallThresholdsChanged, &widget, [&](float, float) {
            containingOwner.reset(); alive = bool(controller);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(controller); // no deferred deletion while its call is active
        });
        slice.setFrequency(7100000);
        QVERIFY(!registry); QVERIFY(alive); QVERIFY(controller);
        QVERIFY(!widget.clarityActive());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!controller);
    }
    void hintRebindRetainsNewAvailability()
    {
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model); SpectrumWidget widget;
        SliceModel first(0), second(1); first.setStreamIndex(0); second.setStreamIndex(1); first.setFrequency(14225000);
        PanadapterModel saved; saved.setBandNFEstimate(Band::Band40m, -100);
        registry.registerPan("p", &widget, nullptr, &saved);
        auto token = registry.bindRemote("p", &widget, &first, {0,0,14225000,192000},1,1,1);
        registry.setRemoteAvailable("p", token, false);
        bool rebound = false;
        connect(registry.controllerForPan("p"), &ClarityController::waterfallThresholdsChanged,
            &registry, [&](float, float) {
                if (rebound) { return; }
                rebound = true;
                token = registry.bindRemote("p", &widget, &second, {1,0,7100000,192000},1,2,2);
            });
        first.setFrequency(7100000);
        QVERIFY(rebound);
        registry.feedRemoteFloor("p", &widget, token, -120, 1000);
        QCOMPARE(widget.wfActiveLowThreshold(), -125.0f);
    }
    void localTruthPrecedesQueuedDisplayEdge()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget first, created; SliceModel slice; slice.setStreamIndex(0);
        bool actualMox=false;
        registry.setLocalKeyedQueryForTest([&] { return actualMox; });
        registry.registerPan("p",&first,nullptr);
        const auto token=registry.bindRemote("p",&first,&slice,{0,0,1,2},1,1,1);
        actualMox=true; // committed authoritative truth; display edge still queued
        registry.feedRemoteFloor("p",&first,token,-140,1000);
        QVERIFY(!first.clarityActive());
        emit registry.controllerForPan("p")->waterfallThresholdsChanged(-100,-40);
        QVERIFY(!first.clarityActive());
        registry.registerPan("new",&created,nullptr);
        QVERIFY(registry.controllerForPan("new")->isTransmitting());
        registry.setKeyed(true); actualMox=false; registry.setKeyed(false);
        QVERIFY(!registry.controllerForPan("new")->isTransmitting());
    }
    void independentFloorsAndRetune()
    {
        RadioModel model(RadioModel::Role::Remote);
        PanClarityRegistry registry(&model);
        std::vector<std::unique_ptr<SpectrumWidget>> widgets;
        std::vector<std::unique_ptr<SliceModel>> slices;
        std::vector<std::unique_ptr<ClarityController>> references;
        QList<PanClarityRegistry::RecipientToken> tokens;
        for (int i = 0; i < 4; ++i) {
            widgets.push_back(std::make_unique<SpectrumWidget>());
            slices.push_back(std::make_unique<SliceModel>(i));
            slices.back()->setStreamIndex(i % 2); slices.back()->setStreamEpoch(1);
            references.push_back(std::make_unique<ClarityController>()); references.back()->setEnabled(true);
            const QString id = QString::number(i);
            registry.registerPan(id, widgets.back().get(), nullptr);
            tokens.append(registry.bindRemote(id, widgets.back().get(), slices.back().get(), {i % 2, 1, 14225000, 192000}, 1, i + 1, 1));
        }
        for (qint64 now : {1000, 1500, 2500}) {
            for (int i = 0; i < 4; ++i) {
                const float floor = -150.0f + i * 15.0f + (now == 2500 ? 8.0f : 0.0f);
                references[i]->feedNoiseFloor(floor, now);
                registry.feedRemoteFloor(QString::number(i), widgets[i].get(), tokens[i], floor, now);
                QCOMPARE(widgets[i]->wfActiveLowThreshold(), references[i]->lastLow());
                QCOMPARE(widgets[i]->wfActiveHighThreshold(), references[i]->lastHigh());
            }
        }
        const float untouched = registry.controllerForPan("1")->smoothedFloor();
        registry.controllerForPan("0")->notifyManualOverride();
        registry.retunePan("0"); references[0]->retuneNow();
        registry.feedRemoteFloor("0", widgets[0].get(), tokens[0], -90, 3000);
        references[0]->feedNoiseFloor(-90, 3000);
        QCOMPARE(widgets[0]->wfActiveLowThreshold(), references[0]->lastLow());
        QCOMPARE(registry.controllerForPan("1")->smoothedFloor(), untouched);
    }
    void sameSourceRecipientReplacement()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget widget; SliceModel first(0), second(1);
        first.setStreamIndex(0); second.setStreamIndex(0);
        registry.registerPan("p", &widget, nullptr);
        const Source source{0, 0, 14225000, 192000};
        auto token = registry.bindRemote("p", &widget, &first, source, 1, 1, 1);
        auto* owner = registry.controllerForPan("p");
        registry.feedRemoteFloor("p", &widget, token, -130, 1000);
        const float pair = owner->lastLow();
        owner->notifyManualOverride();
        registry.setRemoteAvailable("p", token, false);
        auto replacement = registry.bindRemote("p", &widget, &second, source, 1, 2, 2);
        QVERIFY(owner->isPaused()); QCOMPARE(owner->lastLow(), pair);
        registry.retunePan("p");
        registry.feedRemoteFloor("p", &widget, token, -50, 2000);
        QCOMPARE(owner->lastLow(), pair);
        registry.feedRemoteFloor("p", &widget, replacement, -125, 2000);
        QCOMPARE(owner->smoothedFloor(), -125.0f);
        registry.setRemoteAvailable("p", replacement, false);
        registry.feedRemoteFloor("p", &widget, replacement, -50, 3000);
        QCOMPARE(owner->smoothedFloor(), -125.0f);
        replacement = registry.bindRemote("p", &widget, &first, source, 1, 3, 3);
        registry.feedRemoteFloor("p", &widget, replacement, -50, 2100);
        QCOMPARE(owner->smoothedFloor(), -125.0f); // existing cadence retained
        replacement = registry.bindRemote("p", &widget, &first, {0, 0, 7100000, 192000}, 1, 4, 4);
        registry.feedRemoteFloor("p", &widget, replacement, -100, 3000);
        QCOMPARE(owner->smoothedFloor(), -100.0f);
    }
    // Display pill plan Task 3: the pan gets Clarity's noise floor with its
    // thresholds, so Black Level and Color Gain (57 and 60 by default)
    // place the colours against the floor itself, and a band-change snap
    // carries its floor the same way.
    void noiseFloorReachesThePanWithTheThresholds()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0); slice.setFrequency(14225000);
        PanadapterModel saved; saved.setBandNFEstimate(Band::Band40m, -100);
        registry.registerPan("p", &widget, nullptr, &saved);
        auto token = registry.bindRemote("p", &widget, &slice, {0,0,14225000,192000},1,1,1);
        QVERIFY(qIsNaN(widget.clarityFloorDbm()));
        registry.feedRemoteFloor("p", &widget, token, -120, 1000);
        QVERIFY(widget.clarityActive());
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        QCOMPARE(widget.clarityFloorDbm(), registry.controllerForPan("p")->smoothedFloor());
        QCOMPARE(widget.effectiveWaterfallLevelsForTest().first, -116.5f);
        QCOMPARE(widget.effectiveWaterfallLevelsForTest().second, -78.5f);
        // snapToFloor on a band change: the stored floor for the new band.
        slice.setFrequency(7100000);
        QCOMPARE(registry.controllerForPan("p")->smoothedFloor(), -100.0f);
        QCOMPARE(widget.clarityFloorDbm(), -100.0f);
        QCOMPARE(widget.effectiveWaterfallLevelsForTest().first, -96.5f);
    }
    void retiredTokenCannotReachReusedId()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget oldWidget, widget; SliceModel slice; slice.setStreamIndex(0);
        registry.registerPan("p", &oldWidget, nullptr);
        auto old = registry.bindRemote("p", &oldWidget, &slice, {0,0,1,2},1,1,1);
        QPointer<ClarityController> oldOwner(registry.controllerForPan("p"));
        QMetaObject::invokeMethod(&registry, [&registry, &oldWidget, old]() {
            registry.feedRemoteFloor("p", &oldWidget, old, -50, 2000);
        }, Qt::QueuedConnection);
        registry.retirePan("p"); registry.registerPan("p", &widget, nullptr);
        auto token = registry.bindRemote("p", &widget, &slice, {0,0,1,2},1,2,1);
        registry.feedRemoteFloor("p", &widget, old, -50, 1000);
        QVERIFY(!widget.clarityActive());
        registry.feedRemoteFloor("p", &oldWidget, token, -50, 1000);
        QVERIFY(!widget.clarityActive());
        registry.feedRemoteFloor("p", &widget, token, -120, 1000);
        QCOMPARE(widget.wfActiveLowThreshold(), -125.0f);
        QCoreApplication::processEvents();
        QCOMPARE(widget.wfActiveLowThreshold(), -125.0f);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(oldOwner.isNull());
        // Retirement during a synchronous output callback cannot reach a reused widget.
        connect(registry.controllerForPan("p"), &ClarityController::noiseFloorChanged, &registry,
            [&] { registry.retirePan("p"); registry.registerPan("p", &oldWidget, nullptr); });
        registry.feedRemoteFloor("p", &widget, token, -80, 2000);
        QVERIFY(!oldWidget.clarityActive());
    }
    void localBinsKeepCurrentScale()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget first, second; SliceModel slice; slice.setStreamIndex(0); NereusSDR::FFTEngine engine(0), replacement(0);
        registry.registerPan("a", &first, nullptr); registry.registerPan("b", &second, nullptr);
        registry.bindLocal("a", &slice, &engine, {0,0,1,192000});
        registry.bindLocal("a", &slice, &engine, {0,0,1,192000});
        registry.bindLocal("b", &slice, &engine, {0,0,1,192000});
        ClarityController direct; direct.setEnabled(true); direct.setPollIntervalMs(0);
        for (const QString& id : {QString("a"), QString("b")}) { registry.controllerForPan(id)->setPollIntervalMs(0); }
        const QVector<float> bins(1024, -123.25f); direct.feedBins(bins);
        emit engine.fftReady(0, bins);
        QCOMPARE(first.wfActiveLowThreshold(), direct.lastLow()); QCOMPARE(second.wfActiveHighThreshold(), direct.lastHigh());
        // Another rate, so the pan starts over (a moved centre alone would
        // keep the floor and hold: sameBandRecentreKeepsTheFloorAndHolds).
        registry.bindLocal("a", &slice, &replacement, {0,0,2,96000});
        registry.controllerForPan("b")->notifyManualOverride();
        emit engine.fftReady(0, QVector<float>(1024, -50));
        QCOMPARE(first.wfActiveLowThreshold(), direct.lastLow());
        emit replacement.fftReady(0, QVector<float>(1024, -100));
        QCOMPARE(first.wfActiveLowThreshold(), -105.0f);
        QCOMPARE(second.wfActiveHighThreshold(), direct.lastHigh());
    }
    // Display pill plan Task 12 (R-DSP-21): a key takes Clarity off no pan.
    void keyKeepsClarityOnEveryPan()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget local, remote; SliceModel slice, remoteSlice(1);
        slice.setStreamIndex(0); remoteSlice.setStreamIndex(1); NereusSDR::FFTEngine engine(0);
        registry.registerPan("local", &local, nullptr); registry.registerPan("remote", &remote, nullptr);
        registry.bindLocal("local", &slice, &engine, {0,0,14225000,192000});
        const auto token = registry.bindRemote("remote", &remote, &remoteSlice, {1,0,14225000,192000},1,1,1);
        // A remote pan whose Core offers levels and whose mode is AGC: the
        // Core's levels would be in use the moment Clarity let go.
        remote.setCoreWaterfallLevelsAvailable(true); remote.setWfAgcEnabled(true);
        emit engine.fftReady(0, flat(-130));
        registry.feedRemoteFloor("remote", &remote, token, -125);
        QVERIFY(local.clarityActive()); QVERIFY(remote.clarityActive());
        QVERIFY(!remote.coreWaterfallLevelsInUse());
        QSignalSpy localActive(&local, &SpectrumWidget::clarityActiveChanged);
        QSignalSpy remoteActive(&remote, &SpectrumWidget::clarityActiveChanged);
        registry.setKeyed(true);
        QVERIFY(local.clarityActive()); QVERIFY(remote.clarityActive());
        QVERIFY(!remote.coreWaterfallLevelsInUse());
        QCOMPARE(local.clarityFloorDbm(), -130.0f); QCOMPARE(remote.clarityFloorDbm(), -125.0f);
        // Nothing is taken in while keyed.
        now += 4000; emit engine.fftReady(0, flat(-60));
        registry.feedRemoteFloor("remote", &remote, token, -60);
        QCOMPARE(local.clarityFloorDbm(), -130.0f); QCOMPARE(remote.clarityFloorDbm(), -125.0f);
        registry.setKeyed(false);
        QVERIFY(local.clarityActive()); QVERIFY(remote.clarityActive());
        QVERIFY(!remote.coreWaterfallLevelsInUse());
        QCOMPARE(localActive.count(), 0); QCOMPARE(remoteActive.count(), 0);
    }
    // A manual pause (the controller's pausedChanged(true)) keeps the level
    // too; only the badge changes.
    void pauseKeepsClarityActive()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0);
        registry.registerPan("p", &widget, nullptr);
        const auto token = registry.bindRemote("p", &widget, &slice, {0,0,14225000,192000},1,1,1);
        registry.feedRemoteFloor("p", &widget, token, -120, 1000);
        QVERIFY(widget.clarityActive());
        QSignalSpy paused(registry.controllerForPan("p"), &ClarityController::pausedChanged);
        registry.controllerForPan("p")->notifyManualOverride();
        QCOMPARE(paused.count(), 1); QCOMPARE(paused.at(0).at(0).toBool(), true);
        QVERIFY(widget.clarityActive());
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
    }
    // R-DSP-22, R-DSP-24, #359: after the key the receive frames carry a
    // floor 15 dB higher. The first frame, and every frame for the hold,
    // leaves the pre-key level where it was; then the level follows smoothly.
    void unkeyRestoresClarityAtTheUnchangedFloor()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        QStringList asked;
        registry.setHoldLengthProvider([&](const QString& id) { asked.append(id); return qint64(3000); });
        SpectrumWidget tx, other; SliceModel slice; slice.setStreamIndex(0); NereusSDR::FFTEngine engine(0);
        tx.resize(400, 200); tx.show(); QVERIFY(QTest::qWaitForWindowExposed(&tx));
        registry.registerPan("tx", &tx, nullptr); registry.registerPan("other", &other, nullptr);
        registry.bindLocal("tx", &slice, &engine, {0,0,14225000,192000});
        registry.bindLocal("other", &slice, &engine, {0,0,14225000,192000});
        emit engine.fftReady(0, flat(-130));
        QVERIFY(tx.clarityActive()); QVERIFY(other.clarityActive());
        const float low = tx.wfActiveLowThreshold(), high = tx.wfActiveHighThreshold();
        QCOMPARE(tx.clarityFloorDbm(), -130.0f);
        registry.setKeyed(true);
        QVERIFY(asked.isEmpty());
        now += 10000; emit engine.fftReady(0, flat(-60));
        registry.setKeyed(false);
        const qint64 unkey = now;
        asked.sort(); QCOMPARE(asked, (QStringList{"other", "tx"}));
        auto* controller = registry.controllerForPan("tx");
        // The first receive frame, and the last one inside the hold.
        for (qint64 at : {unkey, unkey + 1500, unkey + 2999}) {
            now = at; emit engine.fftReady(0, flat(-115));
            tx.pushWaterfallRowForTest(flat(-115));
            QVERIFY(tx.clarityActive()); QVERIFY(other.clarityActive());
            QCOMPARE(tx.clarityFloorDbm(), -130.0f); QCOMPARE(other.clarityFloorDbm(), -130.0f);
            QCOMPARE(controller->smoothedFloor(), -130.0f);
            QCOMPARE(tx.wfActiveLowThreshold(), low); QCOMPARE(tx.wfActiveHighThreshold(), high);
        }
        // The cadence counts from the hold's end: the first reading is half
        // a second after it, smoothed over that half second and not over the
        // ten-second key.
        now = unkey + 3000; emit engine.fftReady(0, flat(-115));
        QCOMPARE(controller->smoothedFloor(), -130.0f);
        now = unkey + 3500; emit engine.fftReady(0, flat(-115));
        const float target = smoothedStep(-130.0f, -115.0f, 500);
        QCOMPARE(controller->smoothedFloor(), target);
        QCOMPARE(registry.controllerForPan("other")->smoothedFloor(), target);
        // The pan glides to it, row by row.
        QCOMPARE(tx.clarityFloorDbm(), -130.0f);
        tx.pushWaterfallRowForTest(flat(-115));
        QCOMPARE(tx.clarityFloorDbm(), -130.0f * 0.6f + target * 0.4f);
        QVERIFY(tx.clarityActive()); QVERIFY(other.clarityActive());
    }
    // R-DSP-22: the band changed while keyed and the new band has a stored
    // level: one snap to it at the un-key, then the hold.
    void bandChangeWhileKeyedSnapsOnceToTheStoredLevel()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0); slice.setFrequency(14225000);
        PanadapterModel saved; saved.setBandNFEstimate(Band::Band40m, -100);
        registry.registerPan("p", &widget, nullptr, &saved);
        const auto token = registry.bindRemote("p", &widget, &slice, {0,0,14225000,192000},1,1,1);
        registry.feedRemoteFloor("p", &widget, token, -120);
        auto* controller = registry.controllerForPan("p");
        QSignalSpy thresholds(controller, &ClarityController::waterfallThresholdsChanged);
        registry.setKeyed(true);
        slice.setFrequency(7100000);
        QCOMPARE(thresholds.count(), 0); QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        registry.setKeyed(false);
        QCOMPARE(thresholds.count(), 1);
        QCOMPARE(controller->smoothedFloor(), -100.0f);
        QCOMPARE(widget.clarityFloorDbm(), -100.0f);
        QVERIFY(widget.clarityActive()); QVERIFY(controller->inHold(now));
        now += 500; registry.feedRemoteFloor("p", &widget, token, -80);
        QCOMPARE(thresholds.count(), 1); QCOMPARE(controller->smoothedFloor(), -100.0f);
        // A second key on the same band snaps nothing.
        registry.setKeyed(true); registry.setKeyed(false);
        QCOMPARE(thresholds.count(), 1);
    }
    // The new band has no stored level: nothing until the first reading
    // after the hold, and that one is taken whole (the old band's level is
    // no guide to the new band).
    void bandChangeWhileKeyedWithNoStoredLevelWaitsForTheHold()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0); slice.setFrequency(14225000);
        PanadapterModel saved;
        registry.registerPan("p", &widget, nullptr, &saved);
        const auto token = registry.bindRemote("p", &widget, &slice, {0,0,14225000,192000},1,1,1);
        registry.feedRemoteFloor("p", &widget, token, -120);
        auto* controller = registry.controllerForPan("p");
        QSignalSpy thresholds(controller, &ClarityController::waterfallThresholdsChanged);
        registry.setKeyed(true);
        slice.setFrequency(21200000);
        registry.setKeyed(false);
        QCOMPARE(thresholds.count(), 0);
        QVERIFY(widget.clarityActive()); QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        now += 999; registry.feedRemoteFloor("p", &widget, token, -90);
        QCOMPARE(thresholds.count(), 0); QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        now += 1; registry.feedRemoteFloor("p", &widget, token, -90);
        QCOMPARE(thresholds.count(), 1);
        QCOMPARE(controller->smoothedFloor(), -90.0f);
        QCOMPARE(widget.clarityFloorDbm(), -90.0f);
    }
    // R-DSP-52: a re-key 300 ms into a 1000 ms hold, then an un-key: a
    // fresh full hold, and the kept level never moves.
    void rekeyInsideTheHoldStartsAFreshHold()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        registry.setHoldLengthProvider([](const QString&) { return qint64(1000); });
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0); NereusSDR::FFTEngine engine(0);
        registry.registerPan("p", &widget, nullptr);
        registry.bindLocal("p", &slice, &engine, {0,0,14225000,192000});
        emit engine.fftReady(0, flat(-130));
        auto* controller = registry.controllerForPan("p");
        QSignalSpy thresholds(controller, &ClarityController::waterfallThresholdsChanged);
        registry.setKeyed(true); now += 2000; registry.setKeyed(false);
        const qint64 firstUnkey = now;
        now = firstUnkey + 300; registry.setKeyed(true);
        QVERIFY(!controller->inHold(now));
        emit engine.fftReady(0, flat(-60));
        now = firstUnkey + 400; registry.setKeyed(false);
        // Past the end of the first hold, inside the fresh one.
        now = firstUnkey + 1100; emit engine.fftReady(0, flat(-115));
        now = firstUnkey + 1399; emit engine.fftReady(0, flat(-115));
        QVERIFY(controller->inHold(now));
        QCOMPARE(controller->smoothedFloor(), -130.0f);
        QCOMPARE(widget.clarityFloorDbm(), -130.0f);
        QCOMPARE(thresholds.count(), 0); QVERIFY(widget.clarityActive());
        now = firstUnkey + 1900; emit engine.fftReady(0, flat(-115));
        QCOMPARE(controller->smoothedFloor(), smoothedStep(-130.0f, -115.0f, 500));
        QCOMPARE(thresholds.count(), 1);
    }
    // R-DSP-26: a re-centre on the same band keeps the smoothed floor and
    // holds; another stream, epoch or rate starts over.
    void sameBandRecentreKeepsTheFloorAndHolds()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0); NereusSDR::FFTEngine engine(0);
        registry.registerPan("p", &widget, nullptr);
        registry.bindLocal("p", &slice, &engine, {0,0,14225000,192000});
        auto* controller = registry.controllerForPan("p");
        emit engine.fftReady(0, flat(-130));
        now += 500; emit engine.fftReady(0, flat(-120));
        const float kept = smoothedStep(-130.0f, -120.0f, 500);
        QCOMPARE(controller->smoothedFloor(), kept);
        // Same stream, epoch and rate; the centre moved 25 kHz.
        registry.bindLocal("p", &slice, &engine, {0,0,14250000,192000});
        QCOMPARE(controller->smoothedFloor(), kept);
        QVERIFY(controller->inHold(now));
        now += 999; emit engine.fftReady(0, flat(-90));
        QCOMPARE(controller->smoothedFloor(), kept);
        QVERIFY(widget.clarityActive());
        now += 501; emit engine.fftReady(0, flat(-90));
        QCOMPARE(controller->smoothedFloor(), smoothedStep(kept, -90.0f, 500));
        // Another rate: the first reading is taken whole, with no hold.
        registry.bindLocal("p", &slice, &engine, {0,0,14250000,96000});
        QVERIFY(!controller->inHold(now));
        emit engine.fftReady(0, flat(-90));
        QCOMPARE(controller->smoothedFloor(), -90.0f);
        // Another epoch.
        slice.setStreamEpoch(1);
        registry.bindLocal("p", &slice, &engine, {0,1,14250000,96000});
        emit engine.fftReady(0, flat(-100));
        QCOMPARE(controller->smoothedFloor(), -100.0f);
        // Another stream.
        slice.setStreamIndex(1);
        registry.bindLocal("p", &slice, &engine, {1,1,14250000,96000});
        emit engine.fftReady(0, flat(-110));
        QCOMPARE(controller->smoothedFloor(), -110.0f);
        // A centre on another band is a band change, not a re-centre.
        registry.bindLocal("p", &slice, &engine, {1,1,7100000,96000});
        QVERIFY(!controller->inHold(now));
        emit engine.fftReady(0, flat(-95));
        QCOMPARE(controller->smoothedFloor(), -95.0f);
    }
    // S8, 8.4: Clarity's floor for a local pan is in calibrated dB, with the
    // offset the pan's rows get; the Core's floor is calibrated already.
    void localFloorTakesThePansCalibration()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget local, remote; SliceModel slice, remoteSlice(1);
        slice.setStreamIndex(0); remoteSlice.setStreamIndex(1); NereusSDR::FFTEngine engine(0);
        local.setStationCalOffsetDb(-12.0f); remote.setStationCalOffsetDb(-12.0f);
        registry.registerPan("local", &local, nullptr); registry.registerPan("remote", &remote, nullptr);
        registry.bindLocal("local", &slice, &engine, {0,0,14225000,192000});
        const auto token = registry.bindRemote("remote", &remote, &remoteSlice, {1,0,14225000,192000},1,1,1);
        emit engine.fftReady(0, flat(-100));
        registry.feedRemoteFloor("remote", &remote, token, -100);
        QCOMPARE(registry.controllerForPan("local")->smoothedFloor(), -112.0f);
        QCOMPARE(local.clarityFloorDbm(), -112.0f);
        QCOMPARE(registry.controllerForPan("remote")->smoothedFloor(), -100.0f);
        QCOMPARE(remote.clarityFloorDbm(), -100.0f);
        // Read with each frame: a changed offset is in the next reading.
        local.setStationCalOffsetDb(-2.0f);
        registry.controllerForPan("local")->setSmoothingTauSec(0);
        now += 500; emit engine.fftReady(0, flat(-100));
        QCOMPARE(registry.controllerForPan("local")->smoothedFloor(), -102.0f);
    }
    // S8: the drawn floor moves toward the target on every waterfall row by
    // Thetis's blend, 0.6 of where it is and 0.4 of the target.
    void drawnFloorSlewsTowardTheTargetRowByRow()
    {
        SpectrumWidget widget; widget.resize(400, 200); widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QVERIFY(qIsNaN(widget.clarityFloorDbm()));
        // No floor yet: the target is taken at once.
        widget.setClarityFloorTargetDbm(-120.0f);
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        widget.pushWaterfallRowForTest(flat(-110));
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        // A target 10 dB up: 4 dB on the next row, then 2.4 dB.
        widget.setClarityFloorTargetDbm(-110.0f);
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        widget.pushWaterfallRowForTest(flat(-110));
        QCOMPARE(widget.clarityFloorDbm(), -116.0f);
        widget.pushWaterfallRowForTest(flat(-110));
        QCOMPARE(widget.clarityFloorDbm(), -113.6f);
        // Not finite: ignored.
        widget.setClarityFloorTargetDbm(qQNaN());
        widget.pushWaterfallRowForTest(flat(-110));
        QCOMPARE(widget.clarityFloorDbm(), -112.16f);
        // The immediate form moves the floor and the target together.
        widget.setClarityFloorDbm(-100.0f);
        QCOMPARE(widget.clarityFloorDbm(), -100.0f);
        widget.pushWaterfallRowForTest(flat(-110));
        QCOMPARE(widget.clarityFloorDbm(), -100.0f);
    }
    // The registry's second and later levels arrive as a target; the first
    // and a band-change snap arrive whole.
    void laterLevelsArriveAsATarget()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget widget; widget.resize(400, 200); widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        SliceModel slice; slice.setStreamIndex(0);
        registry.registerPan("p", &widget, nullptr);
        const auto token = registry.bindRemote("p", &widget, &slice, {0,0,14225000,192000},1,1,1);
        registry.controllerForPan("p")->setSmoothingTauSec(0);
        registry.feedRemoteFloor("p", &widget, token, -120);
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        now += 500; registry.feedRemoteFloor("p", &widget, token, -110);
        QCOMPARE(registry.controllerForPan("p")->smoothedFloor(), -110.0f);
        QCOMPARE(widget.clarityFloorDbm(), -120.0f);
        widget.pushWaterfallRowForTest(flat(-110));
        QCOMPARE(widget.clarityFloorDbm(), -116.0f);
        // Re-tune asks for a fresh level: that one arrives whole.
        registry.retunePan("p");
        now += 500; registry.feedRemoteFloor("p", &widget, token, -90);
        QCOMPARE(widget.clarityFloorDbm(), -90.0f);
    }
    // The one-time clean-up of floors stored before they were calibrated.
    void uncalibratedFloorsAreRemovedOnce()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        const QString marker = QStringLiteral("ClarityFloorsCalibrated");
        AppSettings s(path);
        s.setValue(QStringLiteral("DisplayBandNFEstimate_20m"), QStringLiteral("-118.5"));
        s.setValue(QStringLiteral("DisplayBandNFEstimate_40m"), QStringLiteral("-104"));
        s.setValue(QStringLiteral("ClarityFloor_20m"), QStringLiteral("-120"));
        // Not these: other per-band keys and look-alikes.
        s.setValue(QStringLiteral("DisplayGridMin_20m"), QStringLiteral("-140"));
        s.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("True"));
        s.setValue(QStringLiteral("DisplayBandNFEstimate"), QStringLiteral("1"));
        s.setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("9"));
        AppSettings::clearUncalibratedClarityFloors(s);
        QVERIFY(!s.contains(QStringLiteral("DisplayBandNFEstimate_20m")));
        QVERIFY(!s.contains(QStringLiteral("DisplayBandNFEstimate_40m")));
        QVERIFY(!s.contains(QStringLiteral("ClarityFloor_20m")));
        QCOMPARE(s.value(marker).toString(), QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplayGridMin_20m")).toString(), QStringLiteral("-140"));
        QCOMPARE(s.value(QStringLiteral("ClarityEnabled")).toString(), QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplayBandNFEstimate")).toString(), QStringLiteral("1"));
        QCOMPARE(s.value(QStringLiteral("SettingsSchemaVersion")).toString(), QStringLiteral("9"));
        QCOMPARE(s.allKeys().size(), 5);
        // Stored after the marker: calibrated floors. A second run keeps
        // them, and so does the next launch.
        s.setValue(QStringLiteral("DisplayBandNFEstimate_20m"), QStringLiteral("-125"));
        s.setValue(QStringLiteral("ClarityFloor_20m"), QStringLiteral("-126"));
        AppSettings::clearUncalibratedClarityFloors(s);
        QCOMPARE(s.value(QStringLiteral("DisplayBandNFEstimate_20m")).toString(), QStringLiteral("-125"));
        QCOMPARE(s.value(QStringLiteral("ClarityFloor_20m")).toString(), QStringLiteral("-126"));
        s.save();
        AppSettings reloaded(path); reloaded.load();
        QCOMPARE(reloaded.value(marker).toString(), QStringLiteral("True"));
        AppSettings::clearUncalibratedClarityFloors(reloaded);
        QCOMPARE(reloaded.value(QStringLiteral("DisplayBandNFEstimate_20m")).toString(), QStringLiteral("-125"));
        QVERIFY(!reloaded.contains(QStringLiteral("DisplayBandNFEstimate_40m")));
    }
    // The window cleans up before any pan reads the floors; the Core never does.
    void onlyTheWindowCleansUpTheFloors()
    {
        const auto read = [](const QString& relative) {
            QFile file(QStringLiteral(NEREUS_SOURCE_DIR "/") + relative);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        const QString call = QStringLiteral("clearUncalibratedClarityFloors");
        const QString window = read(QStringLiteral("src/main.cpp"));
        const QString core = read(QStringLiteral("src/server_main.cpp"));
        const QString coreInit = read(QStringLiteral("src/core/CoreInit.cpp"));
        QVERIFY(!window.isEmpty() && !core.isEmpty() && !coreInit.isEmpty());
        QVERIFY(!core.contains(call)); QVERIFY(!coreInit.contains(call));
        const qsizetype loaded = window.indexOf(QStringLiteral("CoreInit::initialize(activeProfile)"));
        const qsizetype cleaned = window.indexOf(call);
        const qsizetype windows = window.indexOf(QStringLiteral("GuiConnectionController connections"));
        QVERIFY(loaded >= 0); QVERIFY(cleaned > loaded); QVERIFY(windows > cleaned);
        // No schema version of its own: 10 is two-tone leaving TX profiles
        // (9 before that merged, 2026-10-11).
        QVERIFY(coreInit.contains(QStringLiteral("ensureSettingsAtVersion(10)")));
    }
    void masterKeepsPauseAndSavedFields()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("DisplayWfLowLevel"), QStringLiteral("-137.1250"));
        settings.setValue(QStringLiteral("DisplayWfHighLevel"), QStringLiteral("-82.3750"));
        const QString savedLow = settings.value(QStringLiteral("DisplayWfLowLevel")).toString();
        const QString savedHigh = settings.value(QStringLiteral("DisplayWfHighLevel")).toString();
        SpectrumWidget widget; SliceModel slice; slice.setStreamIndex(0);
        registry.registerPan("p", &widget, nullptr);
        auto token=registry.bindRemote("p", &widget, &slice,{0,0,1,2},1,1,1);
        const float low=widget.wfLowThreshold(), high=widget.wfHighThreshold();
        registry.feedRemoteFloor("p", &widget, token, -130, 1000);
        auto* owner=registry.controllerForPan("p"); owner->notifyManualOverride();
        QSignalSpy changes(&model, &RadioModel::clarityEnabledChanged);
        model.setClarityEnabled(false); model.setClarityEnabled(false); model.setClarityEnabled(true);
        QCOMPARE(changes.size(), 2); QVERIFY(owner->isPaused()); QVERIFY(!widget.clarityActive());
        registry.retunePan("p"); registry.setKeyed(true);
        registry.feedRemoteFloor("p", &widget, token, -100, 2000); QVERIFY(!widget.clarityActive());
        emit owner->waterfallThresholdsChanged(-50,0); QVERIFY(!widget.clarityActive());
        registry.setKeyed(false); registry.feedRemoteFloor("p", &widget, token, -100, 3000);
        QCOMPARE(widget.wfLowThreshold(),low); QCOMPARE(widget.wfHighThreshold(),high);
        QCOMPARE(settings.value(QStringLiteral("DisplayWfLowLevel")).toString(), savedLow);
        QCOMPARE(settings.value(QStringLiteral("DisplayWfHighLevel")).toString(), savedHigh);
        registry.setKeyed(true); SpectrumWidget created; registry.registerPan("new",&created,nullptr);
        QVERIFY(registry.controllerForPan("new")->isTransmitting());
        registry.invalidateRemoteSession(); registry.feedRemoteFloor("p",&widget,token,-50,4000); QVERIFY(!widget.clarityActive());
    }
    // Was bandHintsAndSolePan0Writer. R-DSP-27: at the real 500 ms cadence a
    // steady floor is saved for the band after 30 readings, through each
    // pan's own saved model (every window pan has one), and a pan without
    // one keeps its floors for this run only.
    void bandHintsAndEveryPanSaves()
    {
        RadioModel model(RadioModel::Role::Remote); PanClarityRegistry registry(&model);
        qint64 now = 100000; registry.setClockForTest([&] { return now; });
        SpectrumWidget zero, one, bare; PanadapterModel saved, savedOne; SliceModel slice, oneSlice(1), bareSlice(2);
        slice.setStreamIndex(0); oneSlice.setStreamIndex(0); bareSlice.setStreamIndex(0);
        slice.setFrequency(14225000); oneSlice.setFrequency(14225000); bareSlice.setFrequency(14225000);
        saved.setBand(Band::Band20m); saved.setBandNFEstimate(Band::Band40m,-135);
        registry.registerPan("pan-0",&zero,nullptr,&saved); registry.registerPan("pan-1",&one,nullptr,&savedOne);
        registry.registerPan("pan-2",&bare,nullptr);
        // A band change on the saved model snaps to that band's stored floor.
        saved.setBand(Band::Band40m); QCOMPARE(zero.wfActiveLowThreshold(),-140.0f); QVERIFY(!one.clarityActive());
        // While keyed it waits; the un-key snaps once to the band the pan is on.
        registry.setKeyed(true); const float before=zero.wfActiveLowThreshold();
        saved.setBandNFEstimate(Band::Band20m,-100); saved.setBand(Band::Band20m); QCOMPARE(zero.wfActiveLowThreshold(),before);
        registry.setKeyed(false);
        QCOMPARE(zero.wfActiveLowThreshold(),-105.0f); QCOMPARE(zero.clarityFloorDbm(),-100.0f);
        auto first=registry.bindRemote("pan-0",&zero,&slice,{0,0,14225000,192000},1,1,1);
        auto second=registry.bindRemote("pan-1",&one,&oneSlice,{0,0,14225000,192000},1,2,1);
        auto third=registry.bindRemote("pan-2",&bare,&bareSlice,{0,0,14225000,192000},1,3,1);
        registry.controllerForPan("pan-0")->setSmoothingTauSec(0);
        // Past the un-key hold and the half second after it, then 30
        // readings half a second apart.
        now += 1500;
        for (int i = 0; i < 30; ++i) {
            if (i == 29) {
                QCOMPARE(saved.bandNFEstimate(Band::Band20m),-100.0f);
                QVERIFY(qIsNaN(savedOne.bandNFEstimate(Band::Band20m)));
            }
            registry.feedRemoteFloor("pan-0",&zero,first,-120); registry.feedRemoteFloor("pan-1",&one,second,-70);
            registry.feedRemoteFloor("pan-2",&bare,third,-60);
            now += 500;
        }
        QCOMPARE(saved.bandNFEstimate(Band::Band20m),-120.0f);
        QCOMPARE(savedOne.bandNFEstimate(Band::Band20m),-70.0f);
        QVERIFY(qIsNaN(savedOne.bandNFEstimate(Band::Band40m)));
        // A pan with no saved model primes a band change from its own floors.
        const float ownRuntimeHint=registry.controllerForPan("pan-2")->smoothedFloor();
        bareSlice.setFrequency(7100000); bareSlice.setFrequency(14225000);
        QCOMPARE(bare.wfActiveLowThreshold(),ownRuntimeHint-5);
        QCOMPARE(saved.bandNFEstimate(Band::Band20m),-120.0f);
        const float ownPair = zero.wfActiveLowThreshold();
        saved.setBand(Band::Band40m); // model follows a different selected slice
        QCOMPARE(zero.wfActiveLowThreshold(), ownPair);
        registry.feedRemoteFloor("pan-0", &zero, first, -120);
        QCOMPARE(saved.bandNFEstimate(Band::Band20m), -120.0f);
        QCOMPARE(saved.bandNFEstimate(Band::Band40m), -135.0f);
        // A floor that is not steady is not saved: 30 readings swinging 6 dB.
        registry.controllerForPan("pan-1")->setSmoothingTauSec(0);
        for (int i = 0; i < 30; ++i) { now += 500; registry.feedRemoteFloor("pan-1",&one,second, i % 2 ? -50 : -44); }
        QCOMPARE(savedOne.bandNFEstimate(Band::Band20m),-70.0f);
    }
};
QTEST_MAIN(TestPanClarityRegistry)
#include "tst_pan_clarity_registry.moc"
