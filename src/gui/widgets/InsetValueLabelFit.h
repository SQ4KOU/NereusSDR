// =================================================================
// src/gui/widgets/InsetValueLabelFit.h (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original GUI geometry helper.
// Modification history (NereusSDR):
//   2026-10-12 - Fit VOX and DEXP inset readouts to every actual slider
//                 value with JJ's 11px workspace text minimum.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#pragma once

#include "gui/styles/WorkspacePreview.h"

#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QSlider>
#include <algorithm>

namespace NereusSDR {

// Geometry only: the owning control retains its text, font, style and wiring.
class InsetValueLabelFit final : public QObject {
public:
    InsetValueLabelFit(QLabel* label, QSlider* slider, const QString& suffix,
                      int classicWidth)
        : QObject(label), m_slider(slider), m_suffix(suffix), m_classicWidth(classicWidth)
    {
        label->installEventFilter(this);
        fit(label);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
            || event->type() == QEvent::Show || event->type() == QEvent::Polish) {
            fit(static_cast<QLabel*>(watched));
        }
        return false;
    }

private:
    void fit(QLabel* label)
    {
        if (m_fitting || !m_slider) { return; }
        if (!WorkspacePreview::enabled()) {
            label->setFixedWidth(m_classicWidth);
            return;
        }
        m_fitting = true;
        label->ensurePolished();
        const QFont finalFont = WorkspacePreview::readableFont(label->font(), label);
        const QFontMetrics metrics(finalFont, label);
        QString widestText;
        int widestAdvance = 0;
        // Inspect the actual range: proportional-font digits need not have
        // equal widths, and an interior value can be wider than an endpoint.
        for (int value = m_slider->minimum(); ; ++value) {
            const QString text = QString::number(value) + m_suffix;
            const int advance = metrics.horizontalAdvance(text);
            if (advance > widestAdvance) {
                widestAdvance = advance;
                widestText = text;
            }
            if (value == m_slider->maximum()) { break; }
        }

        // A hidden, unbounded sibling resolves the same ancestor/local QSS
        // and device DPI. Its natural hint includes padding, border and any
        // label margin/indent without feeding our current fixed width back
        // into the calculation on later style or font changes.
        QLabel probe(label->parentWidget());
        probe.setObjectName(label->objectName());
        probe.setFont(finalFont);
        probe.setAlignment(label->alignment());
        probe.setTextFormat(Qt::PlainText);
        probe.setMargin(label->margin());
        probe.setIndent(label->indent());
        probe.setFrameStyle(label->frameStyle());
        probe.setLineWidth(label->lineWidth());
        probe.setMidLineWidth(label->midLineWidth());
        probe.setContentsMargins(label->contentsMargins());
        probe.setEnabled(label->isEnabled());
        probe.setStyleSheet(label->styleSheet());
        probe.setText(widestText);
        probe.ensurePolished();
        const int styledChrome = probe.sizeHint().width()
            - QFontMetrics(probe.font(), &probe).horizontalAdvance(widestText);
        label->setFixedWidth(std::max(m_classicWidth, widestAdvance + styledChrome));
        m_fitting = false;
    }

    QPointer<QSlider> m_slider;
    QString m_suffix;
    int m_classicWidth;
    bool m_fitting{false};
};

} // namespace NereusSDR
