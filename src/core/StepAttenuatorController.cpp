// src/core/StepAttenuatorController.cpp
//
// ADC overload detection with hysteresis + auto-attenuate controller.
// Porting from Thetis console.cs:21290-21763 handleOverload().

#include "StepAttenuatorController.h"
#include "RadioConnection.h"
#include "ReceiverManager.h"

#include <QDateTime>

namespace NereusSDR {

StepAttenuatorController::StepAttenuatorController(QObject* parent)
    : QObject(parent)
{
    m_tickTimer.setInterval(kTickIntervalMs);
    connect(&m_tickTimer, &QTimer::timeout, this, &StepAttenuatorController::tick);
    m_tickTimer.start();
}

// --- Timer control ---

void StepAttenuatorController::setTickTimerEnabled(bool on)
{
    if (on) {
        m_tickTimer.start();
    } else {
        m_tickTimer.stop();
    }
}

// --- Accessors ---

int StepAttenuatorController::overloadCounter(int adc) const
{
    if (adc < 0 || adc >= kMaxAdcs) {
        return 0;
    }
    return m_adcState[static_cast<size_t>(adc)].level;
}

OverloadLevel StepAttenuatorController::overloadLevel(int adc) const
{
    return levelToSeverity(overloadCounter(adc));
}

// --- Configuration setters ---

void StepAttenuatorController::setAutoAttEnabled(bool on)
{
    m_autoAttEnabled = on;
    if (!on && m_autoAttApplied) {
        // Clear auto-att state when disabled.
        // From Thetis console.cs:21509-21514: clear stack when auto-att off.
        applyClassicUndo();
    }
}

void StepAttenuatorController::setAutoAttMode(AutoAttMode mode)
{
    m_autoAttMode = mode;
}

void StepAttenuatorController::setAutoAttUndo(bool on)
{
    m_autoUndoEnabled = on;
}

void StepAttenuatorController::setAutoUndoDelaySec(int sec)
{
    m_autoUndoDelaySec = sec;
}

void StepAttenuatorController::setAutoAttHoldSeconds(double sec)
{
    m_adaptiveHoldMs = static_cast<int>(sec * 1000.0);
}

void StepAttenuatorController::setAdaptiveDecayMs(int ms)
{
    m_adaptiveDecayMs = ms;
}

void StepAttenuatorController::setStepAttEnabled(bool on)
{
    m_stepAttEnabled = on;
}

void StepAttenuatorController::setBand(Band band)
{
    if (m_currentBand == band) {
        return;
    }

    // Save current ATT/preamp to old band.
    m_bandState[static_cast<int>(m_currentBand)] = { m_attDb, m_preampMode };

    m_currentBand = band;

    // Restore ATT/preamp from new band (if any stored).
    auto it = m_bandState.find(static_cast<int>(band));
    if (it != m_bandState.end()) {
        if (it->second.attDb != m_attDb) {
            m_attDb = it->second.attDb;
            emit attenuationChanged(m_attDb);
        }
        if (it->second.preamp != m_preampMode) {
            m_preampMode = it->second.preamp;
            emit preampModeChanged(m_preampMode);
        }
    }

    // Clear auto-att state on band change.
    // From Thetis console.cs:21526-21529: keep_att_entries_for_band.
    if (m_autoAttApplied) {
        setAutoAttApplied(false);
        m_classicSavedAttDb = -1;
    }
}

void StepAttenuatorController::setAttenuation(int dB, int rx)
{
    if (dB < 0) { dB = 0; }
    if (dB > m_maxAttDb) { dB = m_maxAttDb; }
    if (m_attDb == dB) { return; }
    m_attDb = dB;
    emit attenuationChanged(m_attDb);

    // ADC-linked: force the other RX to match.
    if (m_adcLinked && (rx == 0 || rx == 1)) {
        // Prevent infinite recursion — only propagate once.
        int otherRx = (rx == 0) ? 1 : 0;
        Q_UNUSED(otherRx);
        // The linked receiver should track this value.
        // (Full per-RX ATT arrays are a future enhancement;
        //  for now both share m_attDb.)
    }
}

void StepAttenuatorController::setPreampMode(PreampMode mode)
{
    if (m_preampMode == mode) { return; }
    m_preampMode = mode;
    emit preampModeChanged(m_preampMode);
}

void StepAttenuatorController::setMaxAttenuation(int dB)
{
    m_maxAttDb = dB;
}

// --- Tick ---

void StepAttenuatorController::tick()
{
    // From Thetis console.cs:21359-21382 — per-ADC hysteresis counter.
    for (int i = 0; i < kMaxAdcs; ++i) {
        AdcState& st = m_adcState[static_cast<size_t>(i)];

        if (st.overflowed) {
            st.level++;
            if (st.level > kMaxOverloadLevel) {
                st.level = kMaxOverloadLevel;
            }
        } else {
            if (st.level > 0) {
                st.level--;
            }
        }

        // Clear the per-tick overflow flag for next cycle.
        st.overflowed = false;

        // Check for level transition and emit.
        OverloadLevel newLevel = levelToSeverity(st.level);
        if (newLevel != st.lastEmitted) {
            st.lastEmitted = newLevel;
            emit overloadStatusChanged(i, newLevel);
        }
    }

    // Auto-attenuate on red overload.
    if (m_autoAttEnabled) {
        bool anyRed = false;
        int redAdc = -1;
        for (int i = 0; i < kMaxAdcs; ++i) {
            if (m_adcState[static_cast<size_t>(i)].level > kRedThreshold) {
                anyRed = true;
                redAdc = i;
                break;
            }
        }

        if (anyRed) {
            if (m_autoAttMode == AutoAttMode::Classic) {
                applyClassicAutoAtt(redAdc);
            } else {
                applyAdaptiveAutoAtt(redAdc);
            }
        } else if (m_autoAttApplied) {
            // No overload — try undo.
            if (m_autoAttMode == AutoAttMode::Classic) {
                applyClassicUndo();
            }
            // Adaptive decay is handled inside applyAdaptiveAutoAtt
            // even when there's no red — but only if previously applied.
            if (m_autoAttMode == AutoAttMode::Adaptive && m_autoAttApplied) {
                applyAdaptiveAutoAtt(-1);  // decay path
            }
        }
    }
}

// --- Slots ---

void StepAttenuatorController::onAdcOverflow(int adc)
{
    if (adc < 0 || adc >= kMaxAdcs) {
        return;
    }
    m_adcState[static_cast<size_t>(adc)].overflowed = true;
}

// --- Classic auto-att ---

void StepAttenuatorController::applyClassicAutoAtt(int adc)
{
    // From Thetis console.cs:21548-21567 — bump ATT by step shift on red.
    if (m_stepAttEnabled) {
        int shift = m_adcState[static_cast<size_t>(adc)].level;
        int newAtt = m_attDb + shift;
        if (newAtt > m_maxAttDb) {
            newAtt = m_maxAttDb;
        }
        if (newAtt != m_attDb) {
            if (m_classicSavedAttDb < 0) {
                m_classicSavedAttDb = m_attDb;
            }
            m_attDb = newAtt;
            m_lastAutoAttTimeMs = QDateTime::currentMSecsSinceEpoch();
            emit attenuationChanged(m_attDb);
            setAutoAttApplied(true);
        }
    } else {
        // Preamp mode fallback — From Thetis console.cs:21574-21594.
        PreampMode newMode = m_preampMode;
        switch (m_preampMode) {
        case PreampMode::Off:
        case PreampMode::On:
            newMode = PreampMode::Minus10;
            break;
        case PreampMode::Minus10:
            newMode = PreampMode::Minus20;
            break;
        case PreampMode::Minus20:
            newMode = PreampMode::Minus30;
            break;
        default:
            break;
        }
        if (newMode != m_preampMode) {
            if (m_classicSavedAttDb < 0) {
                m_classicSavedPreamp = m_preampMode;
                m_classicSavedAttDb = 0;  // sentinel: we have a saved value
            }
            m_preampMode = newMode;
            m_lastAutoAttTimeMs = QDateTime::currentMSecsSinceEpoch();
            emit preampModeChanged(m_preampMode);
            setAutoAttApplied(true);
        }
    }
}

void StepAttenuatorController::applyClassicUndo()
{
    // From Thetis console.cs:21597-21618 — timer-gated undo.
    if (!m_autoAttApplied) {
        return;
    }

    // Timer-based undo: only gate on hold period when auto-undo is enabled.
    // The explicit disable path (setAutoAttEnabled(false)) always restores.
    if (m_autoUndoEnabled) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 holdMs = static_cast<qint64>(m_autoUndoDelaySec) * 1000;
        if ((now - m_lastAutoAttTimeMs) < holdMs) {
            return;  // Hold period not elapsed.
        }
    }

    // Always restore the saved ATT/preamp value — the undo-enabled flag
    // only gates the timer-based automatic path, not explicit disable.
    if (m_stepAttEnabled && m_classicSavedAttDb >= 0) {
        if (m_classicSavedAttDb != m_attDb) {
            m_attDb = m_classicSavedAttDb;
            emit attenuationChanged(m_attDb);
        }
    } else if (!m_stepAttEnabled && m_classicSavedAttDb >= 0) {
        if (m_classicSavedPreamp != m_preampMode) {
            m_preampMode = m_classicSavedPreamp;
            emit preampModeChanged(m_preampMode);
        }
    }

    m_classicSavedAttDb = -1;
    setAutoAttApplied(false);
}

// --- Adaptive auto-att (NereusSDR extension) ---

void StepAttenuatorController::applyAdaptiveAutoAtt(int adc)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (adc >= 0) {
        // Attack: 1 dB per tick on red overload.
        int newAtt = m_attDb + 1;
        if (newAtt > m_maxAttDb) {
            newAtt = m_maxAttDb;
        }
        if (newAtt != m_attDb) {
            m_attDb = newAtt;
            m_adaptiveLastAttackMs = now;
            emit attenuationChanged(m_attDb);
            setAutoAttApplied(true);
        }
    } else {
        // Decay path — only if hold period elapsed since last attack.
        if ((now - m_adaptiveLastAttackMs) < m_adaptiveHoldMs) {
            return;  // Still in hold period.
        }
        if ((now - m_adaptiveLastDecayMs) < m_adaptiveDecayMs) {
            return;  // Decay rate limit.
        }

        // Decay by 1 dB toward the per-band floor.
        int floor = m_adaptiveFloorDb;
        auto it = m_bandState.find(static_cast<int>(m_currentBand));
        if (it != m_bandState.end()) {
            floor = it->second.attDb;
        }

        if (m_attDb > floor) {
            m_attDb--;
            m_adaptiveLastDecayMs = now;
            emit attenuationChanged(m_attDb);
            if (m_attDb <= floor) {
                setAutoAttApplied(false);
            }
        } else {
            setAutoAttApplied(false);
        }
    }
}

// --- Helpers ---

void StepAttenuatorController::setAutoAttApplied(bool applied)
{
    if (m_autoAttApplied == applied) {
        return;
    }
    m_autoAttApplied = applied;
    emit autoAttActiveChanged(applied);
}

OverloadLevel StepAttenuatorController::levelToSeverity(int level) const
{
    // From Thetis console.cs:21369/21378:
    //   level > 0 → yellow (any overload)
    //   level > 3 → red (sustained overload)
    if (level > kRedThreshold) {
        return OverloadLevel::Red;
    }
    if (level > 0) {
        return OverloadLevel::Yellow;
    }
    return OverloadLevel::None;
}

// --- RadioConnection wiring ---

void StepAttenuatorController::setRadioConnection(RadioConnection* conn)
{
    // Disconnect from old connection.
    if (m_adcOverflowConn) {
        disconnect(m_adcOverflowConn);
        m_adcOverflowConn = {};
    }

    m_connection = conn;

    if (conn) {
        m_adcOverflowConn = connect(conn, &RadioConnection::adcOverflow,
                                    this, &StepAttenuatorController::onAdcOverflow);
        m_tickTimer.start();
    } else {
        m_tickTimer.stop();
    }
}

// --- ReceiverManager wiring ---

void StepAttenuatorController::setReceiverManager(ReceiverManager* mgr)
{
    m_receiverManager = mgr;
    // ReceiverManager doesn't currently emit a ddcMappingChanged signal,
    // so checkAdcLinked() is called explicitly when mapping changes.
}

// --- ADC-linked synchronization ---

void StepAttenuatorController::checkAdcLinked()
{
    if (!m_receiverManager) {
        if (m_adcLinked) {
            m_adcLinked = false;
            emit adcLinkedChanged(false);
        }
        return;
    }

    // Compare ADC assignments for RX0 and RX1.
    ReceiverConfig cfg0 = m_receiverManager->receiverConfig(0);
    ReceiverConfig cfg1 = m_receiverManager->receiverConfig(1);

    bool linked = (cfg0.receiverIndex >= 0 && cfg1.receiverIndex >= 0 &&
                   cfg0.adcIndex == cfg1.adcIndex);

    if (linked != m_adcLinked) {
        m_adcLinked = linked;
        emit adcLinkedChanged(linked);
    }
}

void StepAttenuatorController::onDdcMappingChanged()
{
    checkAdcLinked();
}

}  // namespace NereusSDR
