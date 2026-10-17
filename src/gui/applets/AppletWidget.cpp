// =================================================================
// src/gui/applets/AppletWidget.cpp  (NereusSDR)
// =================================================================
//
// Source attribution (AetherSDR — GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       — per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   NereusSDR is also GPLv3. Attribution follows GPLv3 §5 requirements.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-18 — Implemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//                 Shared applet base class. Title-bar gradient, slider-row,
//                 and toggle-button helpers extracted from the AetherSDR
//                 `src/gui/AppletPanel.{h,cpp}` styling pattern (already
//                 registered for AppletPanelWidget in Bucket A); this base
//                 class hoists that styling up so every NereusSDR applet
//                 (Cat/Cwx/Dvk/Tuner/Eq/Fm/Tx/Rx/PhoneCw/Diversity/Digital/
//                 PureSignal) inherits identical visuals without
//                 duplicating the code.
//   2026-10-16 - setUnavailableReason (NereusSDR-original): grays the
//                 applet with its reason instead of hiding it. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "AppletWidget.h"
#include "gui/StyleConstants.h"
#include "gui/ComboStyle.h"

#include <QPushButton>
#include <QSlider>
#include <QFrame>
#include <QHBoxLayout>
#include <QBoxLayout>
#include <QEvent>
#include <QLabel>

namespace NereusSDR {

AppletWidget::AppletWidget(RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; }"
    ).arg(Style::kTextPrimary)
    + Style::buttonBaseStyle()
    + Style::sliderHStyle());
}

QIcon AppletWidget::appletIcon() const { return {}; }

void AppletWidget::setUnavailableReason(const QString& reason)
{
    // Disabled, never hidden: the applet stays where it is, grayed, with a
    // line saying why and how to turn it on (as the Rotor does).
    if (reason == m_unavailableReason) {
        return;
    }
    const bool wasUnavailable = !m_unavailableReason.isEmpty();
    m_unavailableReason = reason;
    const bool unavailable = !reason.isEmpty();
    if (unavailable && !m_unavailableLabel) {
        m_unavailableLabel = new QLabel(this);
        m_unavailableLabel->setObjectName(QStringLiteral("appletUnavailableReason"));
        m_unavailableLabel->setWordWrap(true);
        m_unavailableLabel->setTextFormat(Qt::PlainText);
        m_unavailableLabel->setStyleSheet(QStringLiteral("QLabel { color: #607080; font-size: 11px; }"));
    }
    if (m_unavailableLabel) {
        m_unavailableLabel->setText(reason);
        placeUnavailableLabel();
        m_unavailableLabel->setVisible(unavailable);
    }
    // The reason is the tooltip while it lasts; the applet's own comes back
    // after (unless the applet set a new one meanwhile).
    if (unavailable && !wasUnavailable) {
        m_ownToolTip = toolTip();
    }
    if (unavailable) {
        setToolTip(reason);
    } else if (wasUnavailable) {
        if (toolTip() == m_reasonToolTip) {
            setToolTip(m_ownToolTip);
        }
        m_ownToolTip.clear();
    }
    m_reasonToolTip = reason;
    // Gray only what this grayed: an applet its owner disabled stays so.
    if (unavailable && !m_grayedUnavailable && !testAttribute(Qt::WA_ForceDisabled)) {
        setEnabled(false);
        m_grayedUnavailable = true;
    } else if (!unavailable && m_grayedUnavailable) {
        m_grayedUnavailable = false;
        setEnabled(true);
    }
}

void AppletWidget::placeUnavailableLabel()
{
    QLayout* own = layout();
    if (own && own->indexOf(m_unavailableLabel) >= 0) {
        return;
    }
    if (auto* box = qobject_cast<QBoxLayout*>(own)) {
        // Under the title bar when the applet has one in its layout.
        const int bar = m_titleBar ? box->indexOf(m_titleBar) : -1;
        box->insertWidget(bar + 1, m_unavailableLabel);
    } else if (own) {
        // Any other layout places it where it adds a widget (a grid's next
        // row, a form's last row).
        own->addWidget(m_unavailableLabel);
    } else {
        // No layout: a line across the top, kept as wide as the applet.
        if (!m_unavailableTracker) {
            m_unavailableTracker = std::make_unique<UnavailableLabelTracker>(this, m_unavailableLabel);
            installEventFilter(m_unavailableTracker.get());
        }
        m_unavailableTracker->fit();
        m_unavailableLabel->raise();
    }
}

AppletWidget::UnavailableLabelTracker::UnavailableLabelTracker(QWidget* applet, QLabel* label)
    : m_applet(applet), m_label(label)
{
}

bool AppletWidget::UnavailableLabelTracker::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_applet && event->type() == QEvent::Resize) {
        fit();
    }
    return false;
}

void AppletWidget::UnavailableLabelTracker::fit()
{
    const int width = m_applet->width();
    const int height = m_label->hasHeightForWidth() ? m_label->heightForWidth(width) : m_label->sizeHint().height();
    m_label->setGeometry(0, 0, width, height);
}

QWidget* AppletWidget::appletTitleBar(const QString& text)
{
    auto* bar = new QWidget(this);
    m_titleBar = bar;
    bar->setFixedHeight(Style::kTitleBarH);
    bar->setStyleSheet(Style::titleBarStyle());

    auto* hbox = new QHBoxLayout(bar);
    hbox->setContentsMargins(2, 0, 4, 0);
    hbox->setSpacing(4);

    auto* grip = new QLabel(QStringLiteral("\u22EE\u22EE"), bar);
    grip->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; background: transparent; }"
    ).arg(Style::kTextScale));
    hbox->addWidget(grip);

    auto* label = new QLabel(text, bar);
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; font-weight: bold;"
        " background: transparent; }"
    ).arg(Style::kTitleText));
    hbox->addWidget(label);
    hbox->addStretch();

    return bar;
}

QHBoxLayout* AppletWidget::sliderRow(const QString& labelText,
                                      QSlider* slider, QLabel* valueLabel,
                                      int labelWidth)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(4);

    auto* lbl = new QLabel(labelText, this);
    lbl->setFixedWidth(labelWidth);
    lbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; }").arg(Style::kTextSecondary));
    row->addWidget(lbl);

    slider->setFixedHeight(18);
    row->addWidget(slider, 1);

    if (valueLabel) {
        valueLabel->setFixedWidth(36);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setStyleSheet(Style::insetValueStyle());
        row->addWidget(valueLabel);
    }

    return row;
}

QPushButton* AppletWidget::styledButton(const QString& text, int w, int h)
{
    auto* btn = new QPushButton(text, this);
    btn->setCheckable(true);
    btn->setFixedHeight(h);
    if (w > 0) { btn->setFixedWidth(w); }
    return btn;
}

QPushButton* AppletWidget::greenToggle(const QString& text, int w, int h)
{
    auto* btn = styledButton(text, w, h);
    btn->setStyleSheet(btn->styleSheet() + Style::greenCheckedStyle());
    return btn;
}

QPushButton* AppletWidget::blueToggle(const QString& text, int w, int h)
{
    auto* btn = styledButton(text, w, h);
    btn->setStyleSheet(btn->styleSheet() + Style::blueCheckedStyle());
    return btn;
}

QPushButton* AppletWidget::amberToggle(const QString& text, int w, int h)
{
    auto* btn = styledButton(text, w, h);
    btn->setStyleSheet(btn->styleSheet() + Style::amberCheckedStyle());
    return btn;
}

QLabel* AppletWidget::insetValue(const QString& text, int w)
{
    auto* lbl = new QLabel(text, this);
    lbl->setFixedWidth(w);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet(Style::insetValueStyle());
    return lbl;
}

QFrame* AppletWidget::divider()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setFixedHeight(2);
    line->setStyleSheet(QStringLiteral("QFrame { color: %1; }").arg(Style::kInsetBorder));
    return line;
}

} // namespace NereusSDR
