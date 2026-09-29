// =================================================================
// src/core/TwoToneController.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original file.  Activation flow ports
// chkTestIMD_CheckedChanged (setup.cs:11040-11191 [v2.10.3.13]).
// See TwoToneController.h header for the full attribution block.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — Phase 3M-1c chunk I.1-I.5 — see header.
//   2026-05-03 — Phase 4 Agent 4B of issue #167 PA-cal hotfix — wires
//                start()/stop() through Phase 3C
//                TransmitModel::setPowerUsingTargetDbm with bTwoTone=true.
//                See header for full attribution.
//   2026-09-22 : R-R3-36 fix wave by J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code. m_keyingMox scoped around the
//                activation walk's own setMox(true). NereusSDR-original.
//   2026-09-23 : R-R3-36 gate fix by J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code. onMoxRejected reacts only to a
//                rejection of two-tone's own key. NereusSDR-original.
//   2026-09-24 : Receiver and transmit gaps plan, Task 7, by J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code. Two-tone
//                holds the manual key (console.ManualMox) around its key,
//                so no mic PTT or VOX releases or takes it.
//   2026-09-24 : Receiver and transmit gaps plan, Task 7 fix wave, by
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//                A start waits out a TUN-off still completing (M9,
//                console.cs:44805-44813 [v2.10.3.15]); a refused start
//                keeps the manual key through the 200 ms settle (M2,
//                setup.cs:11190-11193).
//   2026-09-24 : Receiver and transmit gaps plan, Task 7 follow-up, by
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//                A refused start's settle leaves a manual key another key
//                took (N1). A start with TUN on turns TUN off through its
//                own TUN-off path first, then keys (item 6, ported from
//                console.cs:44805-44813 [v2.10.3.15]).
//   2026-09-25 : iPhone app plan Task 34 (R-IOS-02, ruling 8.5), by
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//                A start asks the keying gate first (admitStationKey), so a
//                refused two-tone never releases or rides another device's
//                key. NereusSDR-original.
//   2026-09-25 : iPhone app plan Task 35 (R-IOS-13), by J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code. A remote
//                device's start asks and keys as that device
//                (setActive(bool, const KeyerIdentity&)). NereusSDR-original.
//   2026-09-29 : PA on-air gate re-review, item 5, by J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code. setTxBandFn:
//                the PA-gain drive reads the held transmit band, as Thetis's
//                GainByBand(TXBand, ...) does (console.cs:46808 [v2.10.3.15]).
// =================================================================

// no-port-check: NereusSDR-original file; Thetis-derived activation flow
// is cited inline below.

#include "TwoToneController.h"

#include "core/LogCategories.h"
#include "core/MoxController.h"
#include "core/PaProfile.h"
#include "core/PaProfileManager.h"
#include "core/TxChannel.h"
#include "models/Band.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QtMath>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
TwoToneController::TwoToneController(QObject* parent)
    : QObject(parent)
{
    m_moxReleaseSettleTimer.setSingleShot(true);
    m_moxReleaseSettleTimer.setInterval(kMoxReleaseSettleMs);
    connect(&m_moxReleaseSettleTimer, &QTimer::timeout,
            this, &TwoToneController::onMoxReleaseSettleElapsed);

    m_tuneReleaseSettleTimer.setSingleShot(true);
    m_tuneReleaseSettleTimer.setInterval(kTuneReleaseSettleMs);
    connect(&m_tuneReleaseSettleTimer, &QTimer::timeout,
            this, &TwoToneController::onTuneReleaseSettleElapsed);

    m_freq2DelayTimer.setSingleShot(true);
    connect(&m_freq2DelayTimer, &QTimer::timeout,
            this, &TwoToneController::onFreq2DelayElapsed);

    m_deactivationSettleTimer.setSingleShot(true);
    m_deactivationSettleTimer.setInterval(kMoxReleaseSettleMs);
    connect(&m_deactivationSettleTimer, &QTimer::timeout,
            this, &TwoToneController::onDeactivationSettleElapsed);

    m_rejectSettleTimer.setSingleShot(true);
    m_rejectSettleTimer.setInterval(kMoxReleaseSettleMs);
    connect(&m_rejectSettleTimer, &QTimer::timeout,
            this, &TwoToneController::onRejectSettleElapsed);
}

TwoToneController::~TwoToneController() = default;

// ---------------------------------------------------------------------------
// Dependency injection
// ---------------------------------------------------------------------------
void TwoToneController::setTransmitModel(TransmitModel* tx)
{
    m_tx = tx;
}

void TwoToneController::setTxChannel(TxChannel* tx)
{
    m_txChannel = tx;
}

void TwoToneController::setMoxController(MoxController* mox)
{
    // Disconnect any previous moxRejected hookup before swapping.
    if (m_moxController) {
        disconnect(m_moxController, &MoxController::moxRejected,
                   this, &TwoToneController::onMoxRejected);
    }
    m_moxController = mox;
    if (m_moxController) {
        connect(m_moxController, &MoxController::moxRejected,
                this, &TwoToneController::onMoxRejected,
                Qt::UniqueConnection);
    }
}

void TwoToneController::setSliceModel(SliceModel* slice)
{
    m_slice = slice;
}

void TwoToneController::setPaProfileManager(PaProfileManager* mgr)
{
    // Phase 4B of #167.  Non-owning pointer.  RadioModel injects the
    // manager on connect; tests inject a manager they own directly.
    // nullptr -> Phase 4B wrapper invocation is skipped and the
    // controller falls back to its pre-Phase-4B behaviour (TXPostGen +
    // MOX + Fixed-mode setPower snapshot only).
    m_paProfileManager = mgr;
}

void TwoToneController::setPowerOn(bool on)
{
    m_powerOn = on;
}

void TwoToneController::setTuneOffPendingFn(std::function<bool()> fn)
{
    m_tuneOffPending = std::move(fn);
}

void TwoToneController::setTuneActiveFn(std::function<bool()> fn)
{
    m_tuneActive = std::move(fn);
}

void TwoToneController::setTuneOffFn(std::function<void()> fn)
{
    m_tuneOff = std::move(fn);
}

void TwoToneController::setTxBandFn(std::function<Band()> fn)
{
    m_txBand = std::move(fn);
}

void TwoToneController::setSettleDelaysMs(int moxReleaseMs, int tuneReleaseMs)
{
    m_moxReleaseSettleTimer.setInterval(moxReleaseMs);
    m_tuneReleaseSettleTimer.setInterval(tuneReleaseMs);
    m_deactivationSettleTimer.setInterval(moxReleaseMs);
    m_rejectSettleTimer.setInterval(moxReleaseMs);
}

// ---------------------------------------------------------------------------
// setActive — canonical entry point
// ---------------------------------------------------------------------------
//
// From Thetis setup.cs:11040-11191 [v2.10.3.13] — chkTestIMD_CheckedChanged.
// ---------------------------------------------------------------------------
void TwoToneController::setActive(bool on, const KeyerIdentity& keyer)
{
    if (on) {
        m_keyer = keyer;
        m_keyerFromCaller = true;
    }
    setActive(on);
}

void TwoToneController::setActive(bool on)
{
    // Task 35: a start from setActive(true) alone is the station device's.
    const bool keyerFromCaller = m_keyerFromCaller;
    m_keyerFromCaller = false;
    if (on && !keyerFromCaller && !m_activationInFlight && !m_active) {
        m_keyer = KeyerIdentity::station(PttMode::None);
    }
    if (on == m_active && !m_activationInFlight) {
        // Idempotent: already in the requested state and not mid-walk.
        return;
    }

    if (on) {
        // ── Stage 1: power-on precondition.  From Thetis setup.cs:11063-11071
        //     [v2.10.3.13]:
        //       if (!console.PowerOn) {
        //           MessageBox.Show("Power must be on to run this test.", ...);
        //           chkTestIMD.Checked = false;
        //           return;
        //       }
        if (!m_powerOn) {
            qCWarning(lcDsp).noquote()
                << "TwoToneController: power must be on to run two-tone test "
                   "— ignoring activation request.";
            // Emit a transition to false so any optimistic UI highlight
            // gets reverted.  setActive(false) on inactive is harmless,
            // but skip the timer walk by emitting directly.
            if (m_active) {
                m_active = false;
                emit twoToneActiveChanged(false);
            }
            return;
        }

        if (!m_tx || !m_txChannel || !m_moxController) {
            qCWarning(lcDsp).noquote()
                << "TwoToneController: missing dependencies (tx/txChannel/mox); "
                   "cannot activate.";
            return;
        }

        // iPhone app plan Task 34 (ruling 8.5): two-tone is a station key.
        // Asked before anything releases MOX, so a refused start never
        // unkeys, or rides on, another device's key.
        // Task 35: a remote device's start asks for that device.
        if (!m_moxController->admitKey(m_keyer)) {
            return;
        }

        m_activationInFlight = true;
        // Task 7 fix wave, M2: a new start owns the manual key from here.
        m_rejectSettleTimer.stop();

        // ── Stage 2a: if TUN is on, turn it off first.  Porting from Thetis
        //     console.cs:44805-44813 [v2.10.3.15], chk2TONE_CheckedChanged,
        //     original C# logic:
        //       // stop tune if currently running and we want to run 2tone
        //       if (chk2TONE.Checked && chkTUN.Checked)
        //       {
        //           //dont want this to fire the checked changed event late, so unlink it, call it, then relink it
        //           chkTUN.CheckedChanged -= new System.EventHandler(chkTUN_CheckedChanged);
        //           chkTUN.Checked = false;
        //           chkTUN_CheckedChanged(this, EventArgs.Empty); // it needs to happen here and now
        //           chkTUN.CheckedChanged += new System.EventHandler(chkTUN_CheckedChanged);
        //           await Task.Delay(300);
        //       }
        //     and only then SetupForm.TestIMD = true, whose Stage 2 below
        //     releases MOX if anything still holds it.
        //
        // Task 7 follow-up, item 6: TUN ends through its own TUN-off path
        // (RadioModel::setTune(false): tune tone off, mode, power and TX
        // VFO back, TUN no longer counted on at tune power), not through a
        // bare setMox(false) that left RadioModel's TUN state on. This
        // replaces the 3M-1c TODO that left Stage 2b unported. The 300 ms
        // wait (kTuneReleaseSettleMs) then also waits until that TUN-off
        // has completed (M9, below).
        if (m_tuneActive && m_tuneActive()
            && !(m_tuneOffPending && m_tuneOffPending()) && m_tuneOff) {
            // chkTUN.Checked = false; chkTUN_CheckedChanged(this, EventArgs.Empty); // it needs to happen here and now  [original inline comment from console.cs:44810]
            m_tuneOff();
            // await Task.Delay(300);  [console.cs:44812]
            m_tuneReleaseSettleTimer.start();
            return;
        }

        // Task 7 fix wave, M9: a TUN-off already under way is waited out.
        // Keying now would cancel the TX-to-RX walk its completion waits for
        // and leave the tune tone running under two-tone. Thetis waits
        // 300 ms after turning TUN off (console.cs:44805-44813 [v2.10.3.15]):
        //   chkTUN.Checked = false;
        //   chkTUN_CheckedChanged(this, EventArgs.Empty); // it needs to happen here and now
        //   ...
        //   await Task.Delay(300);
        if (m_tuneOffPending && m_tuneOffPending()) {
            m_tuneReleaseSettleTimer.start();
            return;
        }

        releaseMoxThenContinue();
    } else {
        // Deactivation.  From Thetis setup.cs:11149-11177 [v2.10.3.13].
        if (!m_active && !m_activationInFlight) {
            return;
        }

        // Cancel any in-flight activation timers before starting teardown.
        m_moxReleaseSettleTimer.stop();
        m_tuneReleaseSettleTimer.stop();
        m_freq2DelayTimer.stop();

        if (m_moxController) {
            m_moxController->setMox(false);
        }
        // From Thetis setup.cs:11151-11152 [v2.10.3.13]:
        //   console.MOX = false;
        //   await Task.Delay(200); // MW0LGE_21a
        m_deactivationSettleTimer.start();
    }
}

// ---------------------------------------------------------------------------
// releaseMoxThenContinue: Stage 2 of activation (the TestIMD setter, after
// chk2TONE_CheckedChanged has turned TUN off).
// ---------------------------------------------------------------------------
void TwoToneController::releaseMoxThenContinue()
{
    if (m_moxController == nullptr) {
        m_activationInFlight = false;
        return;
    }
    // ── Stage 2: if MOX is currently engaged, release first.  From Thetis
    //     setup.cs:11072-11077 [v2.10.3.13]:
    //       if (console.MOX) {
    //           Audio.MOX = false;
    //           console.MOX = false;
    //           await Task.Delay(200); // MW0LGE_21a
    //       }
    if (m_moxController->isMox()) {
        m_moxController->setMox(false);
        m_moxReleaseSettleTimer.start();
        return;
    }
    continueActivation();
}

// ---------------------------------------------------------------------------
// onMoxReleaseSettleElapsed — Stage 2 of activation
// ---------------------------------------------------------------------------
void TwoToneController::onMoxReleaseSettleElapsed()
{
    // After the 200 ms MOX-release settle, continue the activation walk.
    // (TUN, Stage 2a, is turned off before this stage; Task 7 follow-up.)
    continueActivation();
}

// ---------------------------------------------------------------------------
// onTuneReleaseSettleElapsed — Stage 2b of activation
// ---------------------------------------------------------------------------
void TwoToneController::onTuneReleaseSettleElapsed()
{
    // From Thetis console.cs:44740 [v2.10.3.13]:
    //   await Task.Delay(300);
    // Task 7 fix wave, M9: never key while the TUN-off is still completing
    // (it holds the manual key and the tune tone until then).
    if (m_tuneOffPending && m_tuneOffPending()) {
        m_tuneReleaseSettleTimer.start();
        return;
    }
    // Task 7 follow-up, item 6: TUN pressed on again inside the wait is
    // turned off again (Stage 2a); two-tone never keys with TUN on.
    if (m_tuneActive && m_tuneActive() && m_tuneOff) {
        m_tuneOff();
        m_tuneReleaseSettleTimer.start();
        return;
    }
    // Then the TestIMD setter's own Stage 2 (setup.cs:11072-11077): a key
    // made after the TUN-off completed (a held mic) is released first.
    releaseMoxThenContinue();
}

// ---------------------------------------------------------------------------
// continueActivation — stages 3-10 of the activation flow
// ---------------------------------------------------------------------------
void TwoToneController::continueActivation()
{
    if (!m_tx || !m_txChannel || !m_moxController) {
        m_activationInFlight = false;
        return;
    }

    // ── Stage 3: read tone parameters from TransmitModel and compute the
    //     magnitude.  From Thetis setup.cs:11052-11056 [v2.10.3.13]:
    //       double ttfreq1 = (double)udTestIMDFreq1.Value;
    //       double ttfreq2 = (double)udTestIMDFreq2.Value;
    //       double ttmag = (double)udTwoToneLevel.Value;
    //       double ttmag1, ttmag2;
    //       ttmag1 = ttmag2 = 0.49999 * Math.Pow(10.0, ttmag / 20.0);
    //
    // The literal 0.49999 MUST be preserved verbatim per source-first
    // protocol (CLAUDE.md "Constants and Magic Numbers").
    double ttfreq1 = static_cast<double>(m_tx->twoToneFreq1());
    double ttfreq2 = static_cast<double>(m_tx->twoToneFreq2());
    const double ttLevel = m_tx->twoToneLevel();
    const double ttmag1 = 0.49999 * std::pow(10.0, ttLevel / 20.0); // setup.cs:11056 [v2.10.3.13]
    const double ttmag2 = ttmag1; // ttmag1 = ttmag2 = ... [setup.cs:11056]

    // ── Stage 4: mode-aware invert.  From Thetis setup.cs:11057-11062
    //     [v2.10.3.13]:
    //       DSPMode mode = console.radio.GetDSPTX(0).CurrentDSPMode;
    //       if (chkInvertTones.Checked && ((mode == DSPMode.CWL) ||
    //                                      (mode == DSPMode.DIGL) ||
    //                                      (mode == DSPMode.LSB))) {
    //           ttfreq1 = -ttfreq1;
    //           ttfreq2 = -ttfreq2;
    //       }
    const DSPMode txMode = currentTxMode();
    if (m_tx->twoToneInvert() && isLowerSidebandMode(txMode)) {
        ttfreq1 = -ttfreq1;
        ttfreq2 = -ttfreq2;
    }

    // ── Stage 5: pulsed vs continuous branch.  From Thetis setup.cs:11079-
    //     11106 [v2.10.3.13].
    const bool pulsed = m_tx->twoTonePulsed();
    const int  freq2DelayMs = m_tx->twoToneFreq2Delay();

    if (pulsed) {
        // From Thetis setup.cs:11083-11092 [v2.10.3.13]:
        //   setupTwoTonePulse();
        //   console.radio.GetDSPTX(0).TXPostGenMode = 7; // pulsed two tone
        //   console.radio.GetDSPTX(0).TXPostGenTTPulseToneFreq1 = ttfreq1;
        //   console.radio.GetDSPTX(0).TXPostGenTTPulseToneFreq2 = ttfreq2;
        //   console.radio.GetDSPTX(0).TXPostGenTTPulseMag1 = ttmag1;
        //   if ((int)udFreq2Delay.Value == 0)
        //       console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = ttmag2;
        //   else
        //       console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = 0.0;
        //
        // setupTwoTonePulse() applies the Designer-default pulse profile.
        // From Thetis setup.cs:34409-34418 [v2.10.3.13]:
        //   TXPostGenTTPulseIQOut      = true;                                  [line 34414]
        //   TXPostGenTTPulseFreq       = (int)nudPulsed_TwoTone_window.Value;   [line 34415]
        //   TXPostGenTTPulseDutyCycle  = (float)(percent.Value)/100f;            [line 34416]
        //   TXPostGenTTPulseTransition = (float)(ramp.Value)/1000f;              [line 34417]
        m_txChannel->setTxPostGenTTPulseIQOut(true);
        m_txChannel->setTxPostGenTTPulseFreq(kPulseWindowPpsDefault);
        m_txChannel->setTxPostGenTTPulseDutyCycle(
            static_cast<double>(kPulsePercentDefault) / 100.0);
        m_txChannel->setTxPostGenTTPulseTransition(
            static_cast<double>(kPulseRampMsDefault) / 1000.0);

        m_txChannel->setTxPostGenMode(7);
        m_txChannel->setTxPostGenTTPulseToneFreq1(ttfreq1);
        m_txChannel->setTxPostGenTTPulseToneFreq2(ttfreq2);
        m_txChannel->setTxPostGenTTPulseMag1(ttmag1);

        if (freq2DelayMs == 0) {
            m_txChannel->setTxPostGenTTPulseMag2(ttmag2);
        } else {
            m_txChannel->setTxPostGenTTPulseMag2(0.0);
        }
    } else {
        // From Thetis setup.cs:11096-11105 [v2.10.3.13]:
        //   console.radio.GetDSPTX(0).TXPostGenMode = 1;
        //   console.radio.GetDSPTX(0).TXPostGenTTFreq1 = ttfreq1;
        //   console.radio.GetDSPTX(0).TXPostGenTTFreq2 = ttfreq2;
        //   console.radio.GetDSPTX(0).TXPostGenTTMag1 = ttmag1;
        //   //MW0LGE_21a change to delay Freq2 output. Fixes problems with some Amps frequency counters
        //   if ((int)udFreq2Delay.Value == 0)
        //       console.radio.GetDSPTX(0).TXPostGenTTMag2 = ttmag2;
        //   else
        //       console.radio.GetDSPTX(0).TXPostGenTTMag2 = 0.0;
        m_txChannel->setTxPostGenMode(1);
        m_txChannel->setTxPostGenTTFreq1(ttfreq1);
        m_txChannel->setTxPostGenTTFreq2(ttfreq2);
        m_txChannel->setTxPostGenTTMag1(ttmag1);

        // MW0LGE_21a change to delay Freq2 output. Fixes problems with
        // some Amps frequency counters  [from setup.cs:11101 [v2.10.3.13]]
        if (freq2DelayMs == 0) {
            m_txChannel->setTxPostGenTTMag2(ttmag2);
        } else {
            m_txChannel->setTxPostGenTTMag2(0.0);
        }
    }

    // ── Stage 6: setTxPostGenRun(true).  From Thetis setup.cs:11107
    //     [v2.10.3.13]:
    //       console.radio.GetDSPTX(0).TXPostGenRun = 1;
    m_txChannel->setTxPostGenRun(true);

    // ── Stage 7: DrivePowerSource handling.  From Thetis setup.cs:11109-
    //     11120 [v2.10.3.13]:
    //       //MW0LGE_22b
    //       // remember old power //MW0LGE_22b
    //       if (console.TwoToneDrivePowerOrigin == DrivePowerSource.FIXED)
    //           console.PreviousPWR = console.PWR;
    //       // set power
    //       int new_pwr = console.SetPowerUsingTargetDBM(out bool bUseConstrain,
    //                                                    out double targetdBm,
    //                                                    true, true, true);
    //       if (console.TwoToneDrivePowerOrigin == DrivePowerSource.FIXED) {
    //           console.PWRSliderLimitEnabled = false;
    //           console.PWR = new_pwr;
    //       }
    //
    // NereusSDR deviation: SetPowerUsingTargetDBM is a Thetis-internal helper
    // that doesn't yet exist here.  The cleanest semantic match for "Fixed
    // mode" is to use twoTonePower() (the Setup-page-fixed value) directly
    // as the override.  See I.1 step 7 note.
    m_savedPwrValid = false;
    if (m_tx->twoToneDrivePowerSource() == DrivePowerSource::Fixed) {
        m_savedPwr = m_tx->power();
        m_savedPwrValid = true;
        m_tx->setPower(m_tx->twoTonePower());
    }

    // ── Stage 8: engage MOX.  From Thetis setup.cs:11122-11131 [v2.10.3.13]:
    //       console.ManualMox = true;
    //       console.TwoTone = true; // MW0LGE_21a
    //       Audio.MOX = true;
    //       console.MOX = true;
    //       if (!console.MOX) {
    //           chkTestIMD.Checked = false;
    //           return;
    //       }
    //
    // The (!console.MOX) check above corresponds to BandPlanGuard rejecting
    // the request.  The MoxController emits moxRejected(...) on rejection;
    // we catch that via onMoxRejected() and run the cleanup there.
    // R-R3-36: m_keyingMox marks this call (and only this call) as
    // two-tone keying for the PC-microphone admission check.
    //
    // Receiver and transmit gaps plan, Task 7: console.ManualMox = true is
    // MoxController::setManualKey(true), set before the key as Thetis does
    // (setup.cs:11162 [v2.10.3.15]). While it is set no mic PTT, VOX, CAT or
    // TCI keys or releases (PollPTT, console.cs:25470 [v2.10.3.15]).
    m_moxController->setManualKey(true);
    {
        const QScopedValueRollback<bool> keying(m_keyingMox, true);
        // Task 35: a remote device's two-tone keys as that device.
        if (m_keyer.isStation()) {
            m_moxController->setMox(true);
        } else {
            m_moxController->setMox(true, m_keyer);
        }
    }

    // If the setMox call above resulted in immediate rejection (synchronous
    // moxRejected emission), m_active will already be false here and we
    // should not commit.  Use isMox() to detect: a successful setMox(true)
    // walk completes synchronously when MoxController has 0-ms test timers,
    // but most importantly, on rejection isMox() will still be false.
    //
    // In production (real timer durations), MOX is mid-walk after setMox(true)
    // returns; we still commit m_active=true here because the walk WILL
    // complete unless rejected (and rejection is signalled synchronously by
    // moxRejected — see onMoxRejected which clears m_active synchronously).
    if (!m_activationInFlight) {
        // onMoxRejected fired synchronously; nothing more to do.
        return;
    }

    // ── Stage 8b: Phase 4B of #167 — PA-cal hotfix integration.
    //
    // From Thetis console.cs:46693-46708 [v2.10.3.13] — chk2TONE_CheckedChanged
    // SetPowerUsingTargetDBM txMode=2 drive-source enum routing.  In Thetis,
    // chk2TONE.Checked is the runtime mirror that drives txMode=2 inside
    // SetPowerUsingTargetDBM (console.cs:46667-46668).  The wrapper invocation
    // routes through the active drive-source enum (DriveSlider / TuneSlider /
    // Fixed) and emits TransmitModel::audioVolumeChanged so RadioModel can
    // pump audio_volume to TxChannel (iq_gain) + RadioConnection (wire_byte).
    //
    // Sequenced AFTER MOX engagement (Stage 8) so:
    //   - if BandPlanGuard rejected MOX, we never emit audio_volume to the
    //     wire (onMoxRejected has already fired and m_activationInFlight is
    //     cleared above; we return early then).
    //   - the MOX state is consistent with Thetis console.cs:11122-11125
    //     ordering (ManualMox = TwoTone = MOX = true) before the wrapper
    //     reads it.
    //
    // Skips when:
    //   - m_paProfileManager is null (test seam + pre-RadioModel-wired state).
    //   - activeProfile() returns null (manager exists but unloaded).
    // In both cases the controller falls back to its pre-Phase-4B behaviour
    // (TXPostGen + MOX + Fixed-mode setPower snapshot only).  This keeps the
    // existing tst_two_tone_controller suite green and matches early-boot
    // RadioModel state where the manager hasn't loaded yet.
    if (m_paProfileManager) {
        if (const PaProfile* profile = m_paProfileManager->activeProfile()) {
            // Mark TransmitModel two-tone-active FIRST so the wrapper's
            // txMode determination resolves to 2 (per console.cs:46667-46668
            // [v2.10.3.13] — else if (chk2TONE.Checked) txMode = 2).
            m_tx->setTwoToneActive(true);

            // Resolve current TX band from the slice.  bandFromFrequency
            // never returns Band::XVTR — XVTR slot is set explicitly by UI
            // when transverter mode is active and isn't yet wired here
            // (deferred per plan §"Open follow-ups").  GEN/SWL bands fall
            // through to the sentinel fallback in computeAudioVolume
            // (PaProfile::getGainForBand returns 1000.0f for Band::XVTR
            // and out-of-range Bands → linear fallback in the math kernel).
            //
            // RadioModel supplies Thetis TXBand (setTxBandFn). From Thetis
            // setup.cs:11153 [v2.10.3.15]:
            //   // remember old power //MW0LGE_22b
            //   ...
            //   int new_pwr = console.SetPowerUsingTargetDBM(out bool bUseConstrain, out double targetdBm, true, true, true);
            // SetPowerUsingTargetDBM gains
            // with GainByBand(TXBand, new_pwr) (console.cs:46808
            // [v2.10.3.15]), and TXBand holds through a keyed retune.
            // Without it, the slice's band as before.
            Band band = m_slice
                ? bandFromFrequency(m_slice->frequency())
                : Band::Band20m;
            if (m_txBand) {
                band = m_txBand();
            }

            // From Thetis console.cs:46693-46708 [v2.10.3.13] —
            // chk2TONE.Checked txMode=2 drive-source enum routing.  Caller
            // composes wire_byte + iq_gain from result.audioVolume; that's
            // RadioModel's job — see RadioModel.cpp connect block subscribing
            // to TransmitModel::audioVolumeChanged.
            //
            // PR #212 follow-up bench fix (J.J. KG4VCF, 2026-05-07): pass the
            // connected HPSDRModel so the HL2 audio-volume formula
            // `(hl2Power * gbb/100) / 93.75` engages instead of the linear
            // fallback when model defaults to FIRST.  Combined with the
            // RadioModel audioVolumeChanged listener fix, this closes the
            // 2-tone TX-amplitude gap that pinned txEnv at ~0.117 regardless
            // of slider position (calcc LCOLLECT bins 8-15 never filled).
            (void) m_tx->setPowerUsingTargetDbm(
                *profile, band,
                /*bSetPower=*/true,
                /*bFromTune=*/false,
                /*bTwoTone=*/true,
                m_tx->hpsdrModel());
        }
    }

    // ── Stage 9: Freq2Delay deferred Mag2.  From Thetis setup.cs:11134-11142
    //     [v2.10.3.13]:
    //       if ((int)udFreq2Delay.Value > 0) {
    //           await Task.Delay((int)udFreq2Delay.Value);
    //           if (pulsed)
    //               console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = ttmag2;
    //           else
    //               console.radio.GetDSPTX(0).TXPostGenTTMag2 = ttmag2;
    //       }
    if (freq2DelayMs > 0) {
        m_pulsedAtMag2Defer = pulsed;
        m_deferredMag2 = ttmag2;
        m_freq2DelayTimer.setInterval(freq2DelayMs);
        m_freq2DelayTimer.start();
    }

    // ── Stage 10: commit active state.
    m_activationInFlight = false;
    if (!m_active) {
        m_active = true;
        emit twoToneActiveChanged(true);
    }
}

// ---------------------------------------------------------------------------
// onFreq2DelayElapsed — Stage 9 of activation
// ---------------------------------------------------------------------------
void TwoToneController::onFreq2DelayElapsed()
{
    if (!m_active) {
        return; // raced with deactivation
    }
    applyMag2Now();
}

// ---------------------------------------------------------------------------
// applyMag2Now
// ---------------------------------------------------------------------------
void TwoToneController::applyMag2Now()
{
    if (!m_txChannel) {
        return;
    }
    // From Thetis setup.cs:11138-11141 [v2.10.3.13]:
    //   if (pulsed)
    //       console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = ttmag2;
    //   else
    //       console.radio.GetDSPTX(0).TXPostGenTTMag2 = ttmag2;
    if (m_pulsedAtMag2Defer) {
        m_txChannel->setTxPostGenTTPulseMag2(m_deferredMag2);
    } else {
        m_txChannel->setTxPostGenTTMag2(m_deferredMag2);
    }
}

// ---------------------------------------------------------------------------
// onDeactivationSettleElapsed — Stage 1 of deactivation completes
// ---------------------------------------------------------------------------
void TwoToneController::onDeactivationSettleElapsed()
{
    continueDeactivation();
}

// ---------------------------------------------------------------------------
// continueDeactivation
// ---------------------------------------------------------------------------
void TwoToneController::continueDeactivation()
{
    // From Thetis setup.cs:11151-11177 [v2.10.3.13]:
    //   console.MOX = false;
    //   await Task.Delay(200); // MW0LGE_21a
    //   Audio.MOX = false;
    //   console.ManualMox = false;
    //   console.TwoTone = false; // MW0LGE_21a
    //
    //   //MW0LGE_22b
    //   if (console.TwoToneDrivePowerOrigin == DrivePowerSource.FIXED) {
    //       console.PWRSliderLimitEnabled = true;
    //       console.PWR = console.PreviousPWR;
    //   }
    //
    //   chkTestIMD.BackColor = SystemColors.Control;
    //   console.psform.TTgenON = false;
    //   console.radio.GetDSPTX(0).TXPostGenRun = 0;

    if (m_txChannel) {
        m_txChannel->setTxPostGenRun(false);
    }

    // Receiver and transmit gaps plan, Task 7: console.ManualMox = false
    // after the release settle (setup.cs:11193 [v2.10.3.15]).
    if (m_moxController) {
        m_moxController->setManualKey(false);
    }

    if (m_savedPwrValid && m_tx) {
        m_tx->setPower(m_savedPwr);
        m_savedPwrValid = false;
    }

    // Phase 4B of #167: clear the TransmitModel two-tone-active mirror.
    // The next setPowerUsingTargetDbm call (e.g. from RadioModel's
    // powerChanged lambda when MOX drops) will see m_twoToneActive=false
    // and route through txMode=0 (normal mode), restoring the normal-mode
    // audio_volume to the wire.  Phase 4B intentionally does NOT itself
    // call setPowerUsingTargetDbm here — it would emit a "ghost" volume
    // against now-stale 2-tone state.
    //
    // Cite: console.cs:11151-11177 [v2.10.3.13] — setActive(false) maps
    //       console.TwoTone = false at line 11154.
    if (m_tx) {
        m_tx->setTwoToneActive(false);
    }

    if (m_active) {
        m_active = false;
        emit twoToneActiveChanged(false);
    }
    m_activationInFlight = false;
}

// ---------------------------------------------------------------------------
// onRejectSettleElapsed: Task 7 fix wave, M2 (see onMoxRejected).
// ---------------------------------------------------------------------------
void TwoToneController::onRejectSettleElapsed()
{
    if (m_active || m_activationInFlight || m_moxController == nullptr) {
        return;   // a new start owns the manual key now
    }
    // Task 7 follow-up, N1: inside the settle another manual key may have
    // taken over (the MOX button or TUN keyed, or a TUN-off is still
    // completing). That key is theirs and ends on its own path (chkMOX_Click,
    // completeTuneOff). Clearing it here let a held mic key inside the
    // TUN-off window and leave the tune tone on air under it.
    if (m_moxController->isMox()
        || (m_tuneActive && m_tuneActive())
        || (m_tuneOffPending && m_tuneOffPending())) {
        return;
    }
    m_moxController->setManualKey(false);
}

// ---------------------------------------------------------------------------
// onMoxRejected — BandPlanGuard rejected our setMox(true) call
// ---------------------------------------------------------------------------
//
// I.5 contract: the existing 3M-1b K.1 SSB-mode allow-list rule already
// rejects CW for SSB-related operations, including two-tone (which runs
// on the SSB modulator stage).  We don't add a new BandPlanGuard rule
// for two-tone — we just clean up our own state if MoxController emits
// moxRejected after we called setMox(true).
//
// Cleanup:
//   - clear m_activationInFlight so continueActivation() returns early.
//   - revert PWR if we snapshotted it for Fixed mode.
//   - if we had committed m_active=true, revert and emit false.
// ---------------------------------------------------------------------------
void TwoToneController::onMoxRejected(const QString& reason)
{
    Q_UNUSED(reason);

    // R-R3-36: act only on a rejection of the activation walk's own
    // setMox(true). moxRejected is emitted synchronously from inside that
    // call, so m_keyingMox is set exactly then. Any other refused press
    // (a voice key during the MOX-release settle, or a later press while
    // two-tone is live) is not ours: tearing down here would stop the
    // generator and leave MOX keyed on a path that now reads the PC
    // microphone, or abandon a start the operator did not cancel.
    if (!m_keyingMox) {
        return;
    }

    // Stop any in-flight activation timers.
    m_moxReleaseSettleTimer.stop();
    m_tuneReleaseSettleTimer.stop();
    m_freq2DelayTimer.stop();

    // Tear down the gen if it was started in continueActivation.
    if (m_txChannel) {
        m_txChannel->setTxPostGenRun(false);
    }

    // Receiver and transmit gaps plan, Task 7: a refused key unchecks
    // chkTestIMD in Thetis, whose off branch ends with console.ManualMox =
    // false (setup.cs:11193 [v2.10.3.15]).
    //
    // Task 7 fix wave, M2: after the 200 ms settle, not here. From Thetis
    // setup.cs:11190-11193 [v2.10.3.15]:
    //   console.MOX = false;
    //   await Task.Delay(200); //MW0LGE_21a
    //   Audio.MOX = false;//
    //   console.ManualMox = false;
    // Clearing it inside this refused call ran a PollPTT pass while
    // m_keyingMox was still set: a held mic was tried at once, refused
    // again, re-entered here and raised a second message.
    m_rejectSettleTimer.start();

    // Restore PWR if we had snapshotted it.
    if (m_savedPwrValid && m_tx) {
        m_tx->setPower(m_savedPwr);
        m_savedPwrValid = false;
    }

    // Phase 4B of #167: clear the TransmitModel two-tone-active mirror in
    // case it was set in continueActivation Stage 8b before MOX rejection
    // landed.  Idempotent (TransmitModel::setTwoToneActive guards
    // duplicates).
    if (m_tx) {
        m_tx->setTwoToneActive(false);
    }

    m_activationInFlight = false;

    if (m_active) {
        m_active = false;
        emit twoToneActiveChanged(false);
    } else {
        // Emit twoToneActiveChanged(false) anyway so UI can revert any
        // optimistic-on highlight (TxApplet 2-TONE button toggle).
        emit twoToneActiveChanged(false);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool TwoToneController::isLowerSidebandMode(DSPMode mode) noexcept
{
    // From Thetis setup.cs:11058 [v2.10.3.13]:
    //   ((mode == DSPMode.CWL) || (mode == DSPMode.DIGL) || (mode == DSPMode.LSB))
    return mode == DSPMode::LSB
        || mode == DSPMode::DIGL
        || mode == DSPMode::CWL;
}

DSPMode TwoToneController::currentTxMode() const
{
    if (m_slice) {
        return m_slice->dspMode();
    }
    // Default USB mirrors SliceModel::m_dspMode default.
    return DSPMode::USB;
}

} // namespace NereusSDR
