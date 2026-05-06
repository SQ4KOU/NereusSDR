// no-port-check: NereusSDR-original wrapper class.  Thetis manages
// PureSignal via a Form (PSForm) directly bound to a static `puresignal`
// helper class on the WDSP P/Invoke side; there is no separate "PureSignal
// coordinator" in Thetis — the form IS the coordinator.  NereusSDR splits
// the host-side coordination (cal lifecycle, MOX integration, polling, save
// /restore, two-tone wiring) into this class so the UI surfaces (PsForm,
// PureSignalApplet, TxApplet [PS-A], PsaIndicatorWidget) can subscribe to
// model signals without each surface re-implementing the cmd-state machine.
//
// Behavior is faithful to PSForm.cs and the puresignal helper class:
//   - timer1code (PSForm.cs:555-728 [v2.10.3.13]) — 7-state cmd-state
//     machine + GetInfo polling + corrections-applied / correcting / cal-
//     count flags + feedback-colour fade.
//   - timer2code (PSForm.cs:729-790 [v2.10.3.13]) — 3-state eAAState
//     auto-attention machine that drives StepAttenuatorController::
//     setAttOnTxValue when the feedback level is out of the 128-181 range.
//   - puresignal helper (PSForm.cs:1060-1162 [v2.10.3.13]) — derived
//     properties (FeedbackLevel = info[4], CorrectionsBeingApplied =
//     info[14] == 1, CalibrationCount = info[5], Correcting = FeedbackLevel
//     > 90, FeedbackColourLevel range thresholds).
//
// =================================================================
// src/core/PureSignal.h  (NereusSDR)
// =================================================================
//
// PureSignal coordinator: cal lifecycle + MOX integration + auto-attention
// + status polling + save/restore + two-tone integration.  Ports the host-
// side logic from Thetis PSForm.cs timer1code [v2.10.3.13] and the
// puresignal helper class.  Calcc/iqc DSP runs autonomously inside WDSP
// (calcc.c:617 pscc() [v2.10.3.13]); this class is configuration +
// monitoring only.
//
// Source:
//   - PSForm.cs:90-122           (eCMDState + eAAState enums)
//   - PSForm.cs:271-289           (AutoCalEnabled property)
//   - PSForm.cs:425-545           (btnPSCalibrate / btnPSReset / btnPSSave
//                                  / btnPSRestore / btnPSTwoToneGen
//                                  handlers; SetDefaultPeaks)
//   - PSForm.cs:555-728           (timer1code — main polling loop +
//                                  cmd-state machine)
//   - PSForm.cs:729-790           (timer2code — eAAState transitions)
//   - PSForm.cs:920-955           (ForcePS body)
//   - PSForm.cs:1060-1162         (puresignal helper class properties)
//   - calcc.c:888 / 900           (PSSaveCorr / PSRestoreCorr exports)
//   - console.cs:43703            (ForcePureSignalAutoCalDisable)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Created by J.J. Boyd (KG4VCF) for Phase 3M-4 Task 7
//                 PureSignal coordinator, with AI-assisted source-first
//                 protocol via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstring>

#include "BoardCapabilities.h"

namespace NereusSDR {

class WdspEngine;
class TxChannel;
class PsFeedbackChannel;
class MoxController;
class StepAttenuatorController;
class TwoToneController;

// PureSignal coordinator.  Owned by RadioModel via std::unique_ptr.
//
// Construction-time deps that DON'T change after connect:
//   - WdspEngine, MoxController, StepAttenuatorController, TwoToneController
//
// Late-bound deps (set after the WDSP-init lambda fires in connectToRadio):
//   - TxChannel  (created by WdspEngine when channel 1 opens)
//   - PsFeedbackChannel (created by WdspEngine when channel 5 opens)
//
// Thread safety: all public methods must be called from the main thread.
// The internal timers fire on the main thread.  Status getters are atomic
// for cheap cross-thread reads (e.g. SpectrumWidget LED indicator).
class PureSignal : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool autoCalEnabled READ isAutoCalEnabled WRITE setAutoCalEnabled NOTIFY autoCalEnabledChanged)
    Q_PROPERTY(bool correctionsApplied READ correctionsBeingApplied NOTIFY correctingChanged)
    Q_PROPERTY(bool correcting READ isCorrecting NOTIFY correctingChanged)
    Q_PROPERTY(int feedbackLevel READ feedbackLevel NOTIFY feedbackLevelChanged)
    Q_PROPERTY(int calibrationCount READ calibrationCount NOTIFY calibrationCountChanged)
    Q_PROPERTY(bool invertRedBlue READ invertRedBlue WRITE setInvertRedBlue NOTIFY invertRedBlueChanged)
    Q_PROPERTY(bool hideFeedback READ hideFeedback WRITE setHideFeedback NOTIFY hideFeedbackChanged)

public:
    // From Thetis PSForm.cs:97-122 [v2.10.3.13] — eAAState enum.
    enum class AutoAttenuateState {
        Monitor          = 0,
        SetNewValues     = 1,
        RestoreOperation = 2
    };

    explicit PureSignal(WdspEngine* engine,
                        TxChannel* tx,
                        PsFeedbackChannel* fb,
                        MoxController* mox,
                        StepAttenuatorController* stepAtt,
                        TwoToneController* twoTone,
                        QObject* parent = nullptr);
    ~PureSignal() override;

    // ── Late-bound dependency setters ──────────────────────────────────────
    //
    // Both TxChannel and PsFeedbackChannel come up in the WDSP-init lambda,
    // AFTER PureSignal is constructed.  RadioModel calls these setters as
    // soon as the relevant pointers go live.  Pass nullptr to clear on
    // teardown (so a late timer tick after disconnect is a no-op).
    void setTxChannel(TxChannel* tx);
    void setPsFeedbackChannel(PsFeedbackChannel* fb);

    // ── Cal lifecycle ──────────────────────────────────────────────────────

    // PSForm.cs btnPSCalibrate_Click [v2.10.3.13] — kicks off a single
    // calibration.  Sets _singlecalON which the cmd-state machine picks up
    // on the next tick to transition through eCMDState::TurnOnSingleCalibrate
    // -> SingleCalibrate -> StayON.
    void singleCalibrate();

    // Master enable.  PSForm.cs:202-269 PSEnabled property [v2.10.3.13]
    // backs SetPureSignal(0/1) on the connection layer plus calcc setup.
    // For the coordinator we drive the cmd-state machine into a known
    // state.  setEnabled(false) ≡ PSForm.cs btnPSReset_Click (full OFF).
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return m_enabled; }

    // Auto-cal toggle (Thetis PSForm.cs:272-289 AutoCalEnabled property
    // [v2.10.3.13]).  setAutoCalEnabled(true) sets _autoON=true and
    // PSState=true; setAutoCalEnabled(false) sets _OFF=true and
    // PSState=false.  The next cmd-state tick acts on those flags.
    void setAutoCalEnabled(bool on);
    bool isAutoCalEnabled() const noexcept { return m_autoCalEnabled; }

    // ForcePS body — PSForm.cs:924-954 [v2.10.3.13].  Re-issues
    // SetPSControl based on _autoON (single 1,0,0,0 vs auto 0,0,1,0)
    // plus all the state-transfer setter calls (LoopDelay / TXDelay /
    // MoxDelay / RelaxPtol / AutoAttenuate / Pin / Map / Stbl / Tint /
    // OnTop / QuickAttenuate / Show2ToneMeasurements).  The coordinator
    // implements the SetPSControl half — the persisted-state pushes are
    // handled by the UI surfaces (PsForm) wiring AppSettings → setter
    // chain at task 11+.
    void forcePS();

    // PSForm.cs btnPSReset_Click [v2.10.3.13] — full OFF: _OFF=true,
    // PSState=false.  The state machine drains to eCMDState::OFF on the
    // next tick.
    void reset();

    // console.cs:43703 ForcePureSignalAutoCalDisable [v2.10.3.13] —
    // toggles the [PS-A] checkbox off, which fans out via
    // chkFWCATUBypass_CheckedChanged to AutoCalEnabled=false.  Provided
    // here as a convenience that calls setAutoCalEnabled(false).
    void forceAutoCalDisable();

    // PSForm.cs SetDefaultPeaks [v2.10.3.13] — pushes the per-board
    // psDefaultPeak constant into the UI text box AND through to
    // SetPSHWPeak.  We call setPSHWPeak directly with the value previously
    // set via applyBoardCapabilities.
    void setDefaultPeaks();

    // ── Save / restore (file-based, user-chosen path) ──────────────────────
    //
    // Returns false when m_tx is null (e.g. before the WDSP-init lambda
    // wires it).  filename should be a full filesystem path.  Calcc spawns
    // a detached thread for the actual disk I/O — true is returned as soon
    // as the thread is dispatched.
    bool saveCorrections(const QString& filename);
    bool restoreCorrections(const QString& filename);

    // ── Two-tone integration ───────────────────────────────────────────────
    //
    // PsForm.cs btnPSTwoToneGen_Click [v2.10.3.13] toggles
    // SetupForm.TTgenrun.  In NereusSDR the TwoToneController owns the
    // activation / deactivation orchestrator (PSForm.cs route is just a
    // pass-through to console.SetupForm.TTgenrun).  PureSignal::setTwoToneOn
    // forwards to TwoToneController::setActive when wired; otherwise
    // it's a no-op (test-friendly).
    void setTwoToneOn(bool on);

    // ── Status reads ───────────────────────────────────────────────────────
    //
    // All four atomic so the SpectrumWidget LED indicator (cross-thread)
    // and any other UI subscriber can read without locks.

    // From Thetis PSForm.cs:1120-1122 [v2.10.3.13]:
    //   public static int FeedbackLevel { get { return _info[4]; } }
    int feedbackLevel() const noexcept { return m_feedbackLevel.load(); }

    // From Thetis PSForm.cs:1100-1102 [v2.10.3.13]:
    //   public static bool CorrectionsBeingApplied
    //     { get { return _info[14] == 1; } }
    bool correctionsBeingApplied() const noexcept { return m_correctionsApplied.load(); }

    // From Thetis PSForm.cs:1106-1108 [v2.10.3.13]:
    //   public static bool Correcting { get { return FeedbackLevel > 90; } }
    bool isCorrecting() const noexcept { return m_correcting.load(); }

    // From Thetis PSForm.cs:1103-1105 [v2.10.3.13]:
    //   public static int CalibrationCount { get { return _info[5]; } }
    int calibrationCount() const noexcept { return m_calCount.load(); }

    // From Thetis PSForm.cs:1123-1138 [v2.10.3.13] — FeedbackColourLevel:
    //   FB > 181 → DodgerBlue (or Red when InvertRedBlue)
    //   FB > 128 → Lime
    //   FB > 90  → Yellow
    //   else     → Red (or DodgerBlue when InvertRedBlue)
    QColor feedbackColour() const;

    // ── UI mirror state (no DSP effect) ────────────────────────────────────
    //
    // Both back the equivalent FB-label left/right click behaviors (Task
    // 10's PsaIndicatorWidget) AND the Setup → General → Options checkboxes
    // (Task 11).  Persisted by RadioModel via the per-MAC AppSettings
    // namespace at task time.

    bool invertRedBlue() const noexcept { return m_invertRedBlue; }
    void setInvertRedBlue(bool on);

    bool hideFeedback() const noexcept { return m_hideFeedback; }
    void setHideFeedback(bool on);

    // ── Per-board defaults ─────────────────────────────────────────────────
    //
    // Pushes psDefaultPeak + psSampleRate from BoardCapabilities (Task 1)
    // into the feedback channel and the calcc HW peak.  Called from
    // RadioModel::onConnected when the per-board codec applies its DDC
    // config.
    void applyBoardCapabilities(const BoardCapabilities& caps);

    // ── State accessors for tests ─────────────────────────────────────────
    AutoAttenuateState autoAttenuateState() const noexcept { return m_aaState; }

    // ── Test seam ─────────────────────────────────────────────────────────
    //
    // pollTimerTick() and autoAttentionTick() are public for unit testing
    // (Qt's invokeMethod or direct call from tst_puresignal_coordinator).
    // Production code uses the QTimer signals; tests stop the timers via
    // setTimersEnabled(false) and drive the ticks manually.
    void setTimersEnabled(bool on);

public slots:
    void onMoxChanged(bool mox);
    void pollTimerTick();        // PSForm.cs:555-728 [v2.10.3.13] timer1code
    void autoAttentionTick();    // PSForm.cs:729-790 [v2.10.3.13] timer2code

signals:
    void enabledChanged(bool);
    void autoCalEnabledChanged(bool);
    void correctingChanged(bool);
    void feedbackLevelChanged(int);
    void calibrationCountChanged(int);
    void feedbackColourChanged(QColor);
    void invertRedBlueChanged(bool);
    void hideFeedbackChanged(bool);
    void calibrationStarted();
    void calibrationComplete(bool success);
    void feedbackError(const QString& message);

private:
    // From Thetis PSForm.cs:79-89 [v2.10.3.13] — eCMDState enum.  The
    // cmd-state machine is the brain of timer1code.  Transitions are driven
    // by the input flags _autoON / _singlecalON / _restoreON / _OFF.
    enum class CommandState {
        Off                       = 0,
        TurnOnAutoCalibrate       = 1,
        AutoCalibrate             = 2,
        TurnOnSingleCalibrate     = 3,
        SingleCalibrate           = 4,
        StayOn                    = 5,
        TurnOff                   = 6,
        InitiateRestoredCorrection = 7
    };

    // From Thetis PSForm.cs:1140-1151 [v2.10.3.13] — info[15] enum.
    enum class EngineState {
        LRESET = 0,
        LWAIT,
        LMOXDELAY,
        LSETUP,
        LCOLLECT,
        MOXCHECK,
        LCALC,
        LDELAY,
        LSTAYON,
        LTURNON
    };

    // PSForm.cs:1086-1095 HasInfoChanged [v2.10.3.13] — true if any of
    // info[0..15] differs from the previous snapshot.
    bool hasInfoChanged(const int* current16) const;

    QColor computeFeedbackColour(int level) const;

    // Construction-time non-owning pointers
    WdspEngine* m_engine;
    TxChannel* m_tx;
    PsFeedbackChannel* m_fb;
    MoxController* m_mox;
    StepAttenuatorController* m_stepAtt;
    TwoToneController* m_twoTone;

    QTimer m_pollTimer;          // 100 ms — drives pollTimerTick (timer1code)
    QTimer m_autoAttTimer;       // 100 ms — drives autoAttentionTick (timer2code)

    // Cmd-state-machine flags (Thetis PSForm.cs:51-56 + 56 [v2.10.3.13]):
    //   private static bool _autoON = false;
    //   private static bool _singlecalON = false;
    //   private static bool _restoreON = false;
    //   private static bool _OFF = true;
    bool m_autoON{false};
    bool m_singleCalON{false};
    bool m_restoreON{false};
    bool m_OFF{true};
    CommandState m_cmdState{CommandState::Off};
    AutoAttenuateState m_aaState{AutoAttenuateState::Monitor};

    // Auto-attention scratch values (PSForm.cs:64-66 [v2.10.3.13]):
    //   private int _save_autoON = 0;
    //   private int _save_singlecalON = 0;
    //   private int _deltadB = 0;
    int m_saveAutoOn{0};
    int m_saveSingleCalOn{0};
    int m_deltaDb{0};

    // Master enable + auto-cal mirrors (drive Q_PROPERTY signals)
    bool m_enabled{false};
    bool m_autoCalEnabled{false};

    // Atomic status mirrors (cross-thread cheap reads)
    std::atomic<int>  m_feedbackLevel{0};
    std::atomic<bool> m_correctionsApplied{false};
    std::atomic<bool> m_correcting{false};
    std::atomic<int>  m_calCount{0};

    // UI mirror state
    bool m_invertRedBlue{false};
    bool m_hideFeedback{false};

    // Per-tick info[] snapshots for HasInfoChanged equivalence.  Sized 16
    // per Thetis _info / _oldInfo layout (PSForm.cs:1061-1062 [v2.10.3.13]).
    int m_info[16] = {};
    int m_oldInfo[16] = {};

    BoardCapabilities m_caps;
};

} // namespace NereusSDR
