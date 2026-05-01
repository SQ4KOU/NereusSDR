// src/gui/widgets/AdcOverloadBadge.cpp
// no-port-check: NereusSDR-original Qt widget — see header for rationale.
#include "AdcOverloadBadge.h"

#include <QLabel>
#include <QVBoxLayout>

namespace NereusSDR {

AdcOverloadBadge::AdcOverloadBadge(QWidget* parent) : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(8, 1, 8, 1);
    vbox->setSpacing(0);

    m_topLabel = new QLabel(this);
    m_topLabel->setObjectName(QStringLiteral("AdcOverloadBadge_Top"));
    m_topLabel->setAlignment(Qt::AlignHCenter);
    vbox->addWidget(m_topLabel);

    m_bottomLabel = new QLabel(QStringLiteral("OVERLOAD"), this);
    m_bottomLabel->setObjectName(QStringLiteral("AdcOverloadBadge_Bottom"));
    m_bottomLabel->setAlignment(Qt::AlignHCenter);
    vbox->addWidget(m_bottomLabel);

    setAttribute(Qt::WA_StyledBackground, true);
    // Vertical: Fixed at the strip height (46 px); horizontal: Minimum so
    // the badge claims its sizeHint() width and the host's QHBoxLayout
    // can't squeeze the OVERLOAD text below its natural width.
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    applyStyle();
}

void AdcOverloadBadge::setAdcs(const QString& adcs)
{
    if (m_adcs == adcs) { return; }
    m_adcs = adcs;
    m_topLabel->setText(QStringLiteral("ADC%1").arg(adcs));
    updateGeometry();
}

void AdcOverloadBadge::setVariant(Variant v)
{
    if (m_variant == v) { return; }
    m_variant = v;
    applyStyle();
}

void AdcOverloadBadge::applyStyle()
{
    // ucInfoBar.cs:928 [@501e3f5] — red_warning ? Red : Yellow.
    // Match the StatusBadge palette so the alarm reads as part of the
    // badge family in the right-side strip.
    QString fg, bg;
    switch (m_variant) {
        case Variant::Warn:
            fg = QStringLiteral("#ffd700");
            bg = QStringLiteral("rgba(255,215,0,30)");
            break;
        case Variant::Tx:
            fg = QStringLiteral("#ff6060");
            bg = QStringLiteral("rgba(255,96,96,51)");
            break;
    }

    setStyleSheet(QStringLiteral(
        "NereusSDR--AdcOverloadBadge {"
        " background: %1; border-radius: 3px;"
        "}"
        // Top row: small caps, semi-bold, tight letter-spacing — reads as
        // a label rather than a value.
        "QLabel#AdcOverloadBadge_Top {"
        " color: %2;"
        " font-family: 'SF Mono', Menlo, monospace;"
        " font-size: 9px; font-weight: 600;"
        " letter-spacing: 1px;"
        " background: transparent; border: none;"
        "}"
        // Bottom row: bigger, bold, slightly tighter line height — the
        // alarm word that the user reads first.
        "QLabel#AdcOverloadBadge_Bottom {"
        " color: %2;"
        " font-family: 'SF Mono', Menlo, monospace;"
        " font-size: 11px; font-weight: 800;"
        " letter-spacing: 0.5px;"
        " background: transparent; border: none;"
        "}"
    ).arg(bg, fg));
}

} // namespace NereusSDR
