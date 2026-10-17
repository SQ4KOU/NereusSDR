// src/gui/instruments/PeakHold.h

// --- From src/gui/instruments/PeakHold.h ---
// =================================================================
// src/gui/instruments/PeakHold.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Die Spitze der letzten Sekunden ──────────────────────────────────
//
// Zeiger- und Balkeninstrument hielten sie bis 2026-08-18 je einzeln:
// zweimal dasselbe Feld, zweimal dieselbe notePeak(), zweimal dieselbe
// Konstante. Solange beide unverändert blieben, fiel das nicht auf.
// Sobald die Spitzenhaltung einstellbar wird — OE5SOS, 2026-08-18:
// „RX-Quellen und Spitzenhaltung ins Rechtsklickmenü" — wären es zwei
// Orte gewesen, an denen dieselbe Einstellung hätte ankommen müssen.
//
// Also einmal, hier. Kein QObject: das ist Zustand, kein Widget, und
// beide Instrumente besitzen ihn selbst.
//
// ── Was sie NICHT tut ────────────────────────────────────────────────
//
// Sie klingt nicht ab. Die analoge Anzeige, die bis 2026-08-18 im
// Panelkopf stand, hatte ein Abklingen in dB je Sekunde, weil ihr
// Zeiger stetig lief; hier fällt
// die Spitze auf den Messwert zurück, sobald die Haltezeit um ist.
// Ein Abklingen hier wäre eine Erfindung — es steht in keinem Entwurf
// des Betreibers, und die beiden Verhalten nebeneinander wären
// verwirrender als eines.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-18 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/gui/instruments/PeakHold.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/instruments/PeakHold.h
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QElapsedTimer>

namespace NereusSDR {

// From Longpath src/gui/instruments/PeakHold.h:41 [@551576e3]
class PeakHold {
public:
    PeakHold() { m_age.start(); }

    /// Die erste Messung. Sie IST die erste Spitze — sonst stünde als
    /// Spitze der Anfang der Skala.
    void seed(double value)
    {
        m_value = value;
        m_age.restart();
        m_seeded = true;
    }

    /// Eine weitere Messung.
    void note(double value)
    {
        if (!m_seeded) { seed(value); return; }
        if (value >= m_value || m_age.elapsed() > m_holdMs) {
            m_value = value;
            m_age.restart();
        }
    }

    /// Zurück auf den aktuellen Messwert — die Handlung „Zurücksetzen"
    /// im Rechtsklickmenü.
    void reset(double current)
    {
        m_value = current;
        m_age.restart();
    }

    /// Vergisst auch, dass je gemessen wurde. Für clearValue().
    void forget()
    {
        m_seeded = false;
        m_value = 0.0;
        m_age.restart();
    }

    double value() const { return m_value; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    /// Wie lange die Spitze steht. „Die Spitze der letzten Sekunden" —
    /// drei ist die kleinste Zahl, die im Plural stimmt und lang genug
    /// für ein Sprachsignal.
    int  holdMs() const { return m_holdMs; }
    void setHoldMs(int ms) { m_holdMs = qMax(0, ms); }

    // From Longpath src/gui/instruments/PeakHold.h:91 [@551576e3]
    static constexpr int kShortMs  = 1000;
    static constexpr int kMediumMs = 3000;   ///< Vorgabe
    static constexpr int kLongMs   = 8000;

private:
    QElapsedTimer m_age;
    double m_value{0.0};
    int    m_holdMs{kMediumMs};
    bool   m_enabled{true};
    bool   m_seeded{false};
};

} // namespace NereusSDR
