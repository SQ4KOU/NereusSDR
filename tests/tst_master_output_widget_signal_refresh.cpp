// =================================================================
// tests/tst_master_output_widget_signal_refresh.cpp  (NereusSDR)
// =================================================================
//
// Exercises MasterOutputWidget's speakersConfigChanged subscription —
// Sub-Phase 12 Task 12.2 Step 0.
//
// Coverage:
//   1. After emit speakersConfigChanged(cfg), MasterOutputWidget's
//      m_currentDeviceName is updated within a 50 ms timeout (verifiable
//      via a follow-up setCurrentOutputDevice call that should NOT fire
//      outputDeviceChanged since the name is already sync'd).
//   2. speakersConfigChanged does NOT emit outputDeviceChanged (it's a
//      sync-from-engine path, not a user action).
//   3. After the engine updates device name, a second
//      setCurrentOutputDevice with the same name doesn't emit.
//   4. Smoke: widget construction connects to speakersConfigChanged
//      without crashing.
//
// Design spec:
//   docs/architecture/2026-04-20-phase3o-subphase12-addendum.md §4
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/AudioDeviceConfig.h"
#include "core/AudioEngine.h"
#include "gui/widgets/MasterOutputWidget.h"

using namespace NereusSDR;

class TstMasterOutputWidgetSignalRefresh : public QObject {
    Q_OBJECT

private:
    void clearKeys() {
        auto& s = AppSettings::instance();
        s.remove(QStringLiteral("audio/Master/Volume"));
        s.remove(QStringLiteral("audio/Master/Muted"));
        s.remove(QStringLiteral("audio/Speakers/DeviceName"));
    }

private slots:

    void init()    { clearKeys(); }
    void cleanup() { clearKeys(); }

    // ── 1. Smoke: widget construction connects to speakersConfigChanged ─────

    void constructsWithEngineSignalConnected() {
        AudioEngine engine;
        MasterOutputWidget w(&engine);
        QVERIFY(true);  // if we get here, no crash during connect
    }

    // ── 2. speakersConfigChanged does NOT emit outputDeviceChanged ──────────
    //
    // The spec says onSpeakersConfigChanged is a "sync-from-engine path,
    // not a user action" and must not emit outputDeviceChanged.

    void speakersConfigChangedDoesNotEmitOutputDeviceChanged() {
        AudioEngine engine;
        MasterOutputWidget w(&engine);

        QSignalSpy spy(&w, &MasterOutputWidget::outputDeviceChanged);

        AudioDeviceConfig cfg;
        cfg.deviceName = QStringLiteral("TestDevice");
        emit engine.speakersConfigChanged(cfg);

        // Process events so slots have a chance to run.
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 0);
    }

    // ── 3. After speakersConfigChanged, m_currentDeviceName is synced ───────
    //
    // We can indirectly verify this: after the engine emits the signal,
    // a subsequent setCurrentOutputDevice call with the SAME name must
    // NOT emit outputDeviceChanged (since the name is already current).
    // This tests the round-trip: engine emits → widget stores name →
    // setCurrentOutputDevice with same name → no emission.

    void afterSignalSameNameDoesNotEmit() {
        AudioEngine engine;
        MasterOutputWidget w(&engine);

        AudioDeviceConfig cfg;
        cfg.deviceName = QStringLiteral("MyDevice");
        emit engine.speakersConfigChanged(cfg);
        QCoreApplication::processEvents();

        QSignalSpy spy(&w, &MasterOutputWidget::outputDeviceChanged);
        // Calling setCurrentOutputDevice with the name the signal just synced
        // should be a no-op (no emission per the widget contract).
        w.setCurrentOutputDevice(QStringLiteral("MyDevice"));
        QCOMPARE(spy.count(), 0);
    }

    // ── 4. speakersConfigChanged with empty deviceName clears the device ─────

    void speakersConfigChangedWithEmptyNameClears() {
        AudioEngine engine;
        MasterOutputWidget w(&engine);

        // First set to a specific device.
        AudioDeviceConfig cfg1;
        cfg1.deviceName = QStringLiteral("Specific Device");
        emit engine.speakersConfigChanged(cfg1);
        QCoreApplication::processEvents();

        // Now emit with empty deviceName (platform default).
        AudioDeviceConfig cfg2;
        cfg2.deviceName = QString();
        emit engine.speakersConfigChanged(cfg2);
        QCoreApplication::processEvents();

        // Again verify no spurious outputDeviceChanged.
        QSignalSpy spy(&w, &MasterOutputWidget::outputDeviceChanged);
        w.setCurrentOutputDevice(QString());
        QCOMPARE(spy.count(), 0);
    }

    // ── 5. Multiple rapid speakersConfigChanged don't crash ─────────────────

    void rapidConfigChangedDoesNotCrash() {
        AudioEngine engine;
        MasterOutputWidget w(&engine);

        for (int i = 0; i < 20; ++i) {
            AudioDeviceConfig cfg;
            cfg.deviceName = QStringLiteral("Device %1").arg(i);
            emit engine.speakersConfigChanged(cfg);
        }
        QCoreApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TstMasterOutputWidgetSignalRefresh)
#include "tst_master_output_widget_signal_refresh.moc"
