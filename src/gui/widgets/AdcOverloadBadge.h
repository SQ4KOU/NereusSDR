// src/gui/widgets/AdcOverloadBadge.h
// no-port-check: NereusSDR-original Qt widget. The inline comments
// reference Thetis ucInfoBar.cs / console.cs only as behavioral cites
// for severity-flag rules (red_warning gate, 2 s auto-hide); no Thetis
// code is ported here — the widget is a Qt6 QWidget composed of two
// QLabels with QSS styling, independently implemented.
#pragma once

#include <QString>
#include <QWidget>

class QLabel;

namespace NereusSDR {

// AdcOverloadBadge — stacked alarm badge for ADC overload events.
//
// Renders as a two-line vertical pair inside a tinted pill:
//
//     ┌──────────┐
//     │ ADC0     │   ← small caps; updated to ADC0 / ADC0/1 / ADC0/1/2
//     │ OVERLOAD │   ← bold caps; static literal per design intent
//     └──────────┘
//
// Variant::Warn (yellow tint) for any ADC at hysteresis level > 0,
// Variant::Tx (red tint) when any level > 3 — same severity rules as
// Thetis ucInfoBar.Warning() red_warning flag (console.cs:21369 +
// ucInfoBar.cs:927-932 [@501e3f5]). The badge is hidden by default;
// the host wires StepAttenuatorController::overloadStatusChanged to
// setAdcs() + setVariant() + setVisible(true).
//
// Spec: docs/architecture/2026-04-30-shell-chrome-redesign-design.md
// §Right-side strip — between PA OK and TX. Width changes consume the
// strip's right stretch space rather than shifting the STATION anchor
// (layout-stability rule §278.4).
class AdcOverloadBadge : public QWidget {
    Q_OBJECT

public:
    enum class Variant {
        Warn,    // yellow tint — at least one ADC at level > 0
        Tx,      // red tint — at least one ADC at level > 3
    };

    explicit AdcOverloadBadge(QWidget* parent = nullptr);

    // Set the ADC list shown on the top row. Examples: "0", "0/1", "0/1/2".
    // The widget prepends "ADC" so the rendered top row reads "ADC0/1".
    void setAdcs(const QString& adcs);
    QString adcs() const noexcept { return m_adcs; }

    void setVariant(Variant v);
    Variant variant() const noexcept { return m_variant; }

private:
    void applyStyle();

    QString  m_adcs;
    Variant  m_variant{Variant::Warn};
    QLabel*  m_topLabel{nullptr};
    QLabel*  m_bottomLabel{nullptr};
};

} // namespace NereusSDR
