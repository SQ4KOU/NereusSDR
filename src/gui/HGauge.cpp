// src/gui/HGauge.cpp
// Modification history (NereusSDR):
//   2026-10-12 - Apply the approved workspace-only 11 px text minimum.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#include "gui/styles/WorkspacePreview.h"
#include "HGauge.h"
#include "StyleConstants.h"
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

namespace NereusSDR {

namespace {

// The gauge's rows, top down: the bar, one clear row, the scale numbers.
// With a 7 px scale font this is the 30 px gauge the app has always drawn.
constexpr int kBaseHeight = 30;
constexpr int kBarHeight = 12;
constexpr int kBarTop = (kBaseHeight - kBarHeight) / 2;
constexpr int kScaleTop = kBarTop + kBarHeight + 1;
constexpr int kScalePointSize = 7;
constexpr int kTitlePointSize = 8;
// The width of the slot a scale number is centered in, on its tick.
constexpr int kScaleSlotWidth = 40;

} // namespace

HGauge::HGauge(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(heightForScale());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HGauge::setRange(double min, double max) { m_min = min; m_max = max; update(); }
void HGauge::setYellowStart(double val) { m_yellowStart = val; update(); }
void HGauge::setRedStart(double val) { m_redStart = val; update(); }
void HGauge::setReversed(bool rev) { m_reversed = rev; update(); }
void HGauge::setTitle(const QString& t) { m_title = t; update(); }
void HGauge::setUnit(const QString& u) { m_unit = u; update(); }
void HGauge::setValue(double val) {
    // Value-change guard: skip the repaint when the new sample matches the
    // current one within FP noise.  HGauge is driven by many fast timers
    // (TxApplet fwd / SWR at 20 Hz, PhoneCwApplet mic at 20 Hz, VaxApplet
    // levels at 60 Hz aggregate, AudioTxInputPage VU at 100 Hz, etc.).  When
    // the signal is quiet or steady, suppressing the redundant update() cuts
    // a large chunk of the main-thread paint load that was driving the
    // QCALayerBackingStore::recreateBackBufferIfNeeded thrash on macOS.
    if (qFuzzyCompare(1.0 + m_value, 1.0 + val)) { return; }
    m_value = val;
    update();
}
void HGauge::setPeakValue(double val) {
    if (qFuzzyCompare(1.0 + m_peak, 1.0 + val)) { return; }
    m_peak = val;
    update();
}
void HGauge::setTickLabels(const QStringList& labels)
{
    m_tickLabels = labels;
    updateFixedHeight();
    update();
}
void HGauge::setScaleFontPixelSize(int pixelSize)
{
    m_scaleFontPixelSize = pixelSize;
    updateFixedHeight();
    update();
}

void HGauge::setTitleFontPixelSize(int pixelSize)
{
    m_titleFontPixelSize = pixelSize;
    update();
}

QFont HGauge::titleFont(const QString& family) const
{
    QFont f = WorkspacePreview::readableFont(QFont(family, kTitlePointSize, QFont::Bold), this);
    if (m_titleFontPixelSize > 0) {
        f.setPixelSize(WorkspacePreview::readableTextPixelSize(m_titleFontPixelSize));
    }
    return f;
}

QFont HGauge::scaleFont(const QString& family) const
{
    QFont f = WorkspacePreview::readableFont(QFont(family, kScalePointSize), this);
    if (m_scaleFontPixelSize > 0) {
        f.setPixelSize(WorkspacePreview::readableTextPixelSize(m_scaleFontPixelSize));
    }
    return f;
}

int HGauge::heightForScale() const
{
    if (m_tickLabels.isEmpty()) { return kBaseHeight; }
    // The numbers are drawn from kScaleTop with their baseline one ascent
    // below it. Digits end on the row above the baseline, so a widget that
    // ends with the baseline row shows them whole with one clear row under
    // them. 7 points is 7 px on the Mac (ascent 7, so 30 px, as before) and
    // 9 px at 96 dpi (ascent 10 in Segoe UI, so 33 px); at a fixed 30 px
    // the larger font lost its bottom two rows.
    const QFontMetrics fm(scaleFont(font().family()), this);
    return qMax(kBaseHeight, kScaleTop
        + (WorkspacePreview::enabled() ? fm.height() : fm.ascent()) + 1);
}

void HGauge::updateFixedHeight()
{
    setFixedHeight(heightForScale());
    const int currentMinimum = minimumWidth();
    if (m_textFloorMinimumApplied && currentMinimum != m_textFloorMinimumWidth) {
        // A caller changed its own constraint while the floor was active.
        m_minimumWidthBeforeTextFloor = currentMinimum;
    }
    const int baselineMinimum = m_textFloorMinimumApplied
        ? m_minimumWidthBeforeTextFloor : currentMinimum;
    int textMinimum = 0;
    if (WorkspacePreview::enabled() && m_tickLabels.size() > 1) {
        const QFontMetrics metrics(scaleFont(font().family()), this);
        const int intervals = m_tickLabels.size() - 1;
        // End labels stay inside the face while inner labels stay centered
        // on their ticks. Reserve the space those two requirements need;
        // shrinking the font or dropping a scale value would hide information.
        const qreal first = metrics.horizontalAdvance(m_tickLabels.first());
        const qreal second = metrics.horizontalAdvance(m_tickLabels.at(1));
        const qreal last = metrics.horizontalAdvance(m_tickLabels.last());
        const qreal beforeLast = metrics.horizontalAdvance(m_tickLabels.at(intervals - 1));
        qreal spacing = qMax(first + second / 2 + 2, last + beforeLast / 2 + 2);
        for (int i = 1; i < intervals; ++i) {
            spacing = qMax(spacing, (metrics.horizontalAdvance(m_tickLabels.at(i))
                + metrics.horizontalAdvance(m_tickLabels.at(i + 1))) / 2.0 + 2);
        }
        textMinimum = qCeil(spacing * intervals) + 4;
    }
    if (textMinimum > baselineMinimum) {
        if (!m_textFloorMinimumApplied) {
            m_minimumWidthBeforeTextFloor = currentMinimum;
        }
        m_textFloorMinimumApplied = true;
        m_textFloorMinimumWidth = textMinimum;
        setMinimumWidth(textMinimum);
    } else if (m_textFloorMinimumApplied) {
        // Remove only this floor's extra width; the caller's baseline survives.
        m_textFloorMinimumApplied = false;
        setMinimumWidth(baselineMinimum);
    }
}

void HGauge::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange
        || event->type() == QEvent::StyleChange
        || event->type() == QEvent::PaletteChange) {
        updateFixedHeight();
    }
}
void HGauge::setUnavailable(bool unavailable)
{
    if (m_unavailable == unavailable) { return; }
    m_unavailable = unavailable;
    update();
}

double HGauge::filledFraction() const noexcept
{
    if (m_max <= m_min) { return 0.0; }
    const double normalized = qBound(0.0, (m_value - m_min) / (m_max - m_min), 1.0);
    // R-R3-21: a reversed gauge is empty at its maximum and full at its
    // minimum, filled from the right; the same mapping as the AetherSDR
    // gauge this widget's look follows (HGauge.h paintEvent [@1e0718ad]).
    // It read fill = normalized, so a reversed gauge at its maximum (the
    // "none" end) drew a full bar.
    return m_reversed ? 1.0 - normalized : normalized;
}

void HGauge::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // iPhone app plan Task 39: an unavailable gauge is drawn dimmed.
    if (m_unavailable) {
        p.setOpacity(0.4);
    }

    const int w = width();
    const int barH = kBarHeight;
    const int barY = kBarTop;
    const int barX = 2;
    const int barW = w - 4;

    // Background
    p.setPen(QColor(Style::kBorderSubtle));
    p.setBrush(QColor(Style::kInsetBg));
    p.drawRoundedRect(barX, barY, barW, barH, 2, 2);

    if (m_max <= m_min) { return; }

    const double range = m_max - m_min;

    if (m_reversed) {
        const int fillW = static_cast<int>(filledFraction() * barW);
        if (fillW > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(Style::kGaugeDanger));
            p.drawRoundedRect(barX + barW - fillW, barY + 1, fillW, barH - 2, 1, 1);
        }
    } else {
        const int fillW = static_cast<int>(filledFraction() * barW);
        if (fillW > 0) {
            const double yellowNorm = (m_yellowStart - m_min) / range;
            const double redNorm = (m_redStart - m_min) / range;
            const int yellowX = static_cast<int>(yellowNorm * barW);
            const int redX = static_cast<int>(redNorm * barW);

            int normalEnd = qMin(fillW, yellowX);
            if (normalEnd > 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(Style::kGaugeNormal));
                p.drawRoundedRect(barX + 1, barY + 1, normalEnd, barH - 2, 1, 1);
            }
            if (fillW > yellowX) {
                int warnEnd = qMin(fillW, redX) - yellowX;
                if (warnEnd > 0) {
                    p.setBrush(QColor(Style::kGaugeWarning));
                    p.drawRect(barX + 1 + yellowX, barY + 1, warnEnd, barH - 2);
                }
            }
            if (fillW > redX) {
                int dangerW = fillW - redX;
                p.setBrush(QColor(Style::kGaugeDanger));
                p.drawRect(barX + 1 + redX, barY + 1, dangerW, barH - 2);
            }
        }
    }

    // Peak hold marker
    if (m_peak > m_min) {
        const double peakNorm = qBound(0.0, (m_peak - m_min) / range, 1.0);
        const int peakX = barX + 1 + static_cast<int>(peakNorm * (barW - 2));
        p.setPen(QPen(QColor(Style::kGaugePeak), 1));
        p.drawLine(peakX, barY + 1, peakX, barY + barH - 2);
    }

    // Center title
    if (!m_title.isEmpty()) {
        p.setPen(QColor(Style::kTextSecondary));
        p.setFont(titleFont(p.font().family()));
        // Not clipped to the bar: at 96 dpi the title's line (15 px in
        // Segoe UI) is taller than the 12 px bar, and clipping cut the
        // tail of the "p" in "Temp". Where the line fits the bar (the Mac)
        // nothing was clipped and the picture is the same.
        p.drawText(QRect(barX, barY, barW, barH), Qt::AlignCenter | Qt::TextDontClip, m_title);
    }

    // Tick labels along bottom
    if (!m_tickLabels.isEmpty()) {
        p.setFont(scaleFont(p.font().family()));
        const int scaleH = height() - kScaleTop;
        const QFontMetrics fm = p.fontMetrics();
        const int n = m_tickLabels.size();
        for (int i = 0; i < n; ++i) {
            double frac = static_cast<double>(i) / (n - 1);
            int x = barX + static_cast<int>(frac * barW);
            QColor col(Style::kGaugeNormal);
            double val = m_min + frac * range;
            if (val >= m_redStart) { col = QColor(Style::kGaugeDanger); }
            else if (val >= m_yellowStart) { col = QColor(Style::kGaugeWarning); }
            p.setPen(col);
            // A number is centered on its tick. The first and last ticks
            // are the bar's ends, 2 px inside the widget, so half of those
            // two numbers fell outside it and was cut. A number that would
            // pass an end of the bar is drawn flush with that end instead;
            // every other number stays centered on its tick.
            const int textW = fm.horizontalAdvance(m_tickLabels[i]);
            const int halfW = (textW + 1) / 2;
            if (x - halfW < barX) {
                p.drawText(QRect(barX - (WorkspacePreview::enabled() ? 1 : 0), kScaleTop,
                                 textW + kScaleSlotWidth, scaleH),
                           Qt::AlignLeft | Qt::AlignTop, m_tickLabels[i]);
            } else if (x + halfW > barX + barW) {
                p.drawText(QRect(barX + barW - textW - kScaleSlotWidth, kScaleTop,
                                 textW + kScaleSlotWidth, scaleH),
                           Qt::AlignRight | Qt::AlignTop, m_tickLabels[i]);
            } else {
                p.drawText(QRect(x - kScaleSlotWidth / 2, kScaleTop, kScaleSlotWidth, scaleH),
                           Qt::AlignHCenter | Qt::AlignTop, m_tickLabels[i]);
            }
        }
    }
}

} // namespace NereusSDR
