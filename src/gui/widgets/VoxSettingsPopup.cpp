// =================================================================
// src/gui/widgets/VoxSettingsPopup.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-native widget — VOX quick-settings popup.
// Pattern derived from DspParamPopup (AetherSDR-derived, NereusSDR).
// See VoxSettingsPopup.h for design notes and range citations.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-28 — Phase 3M-1b J.2: Created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

#include "VoxSettingsPopup.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

namespace NereusSDR {

// Popup stylesheet — matches DspParamPopup visual palette.
static const QString kPopupStyle = QStringLiteral(
    "QWidget#VoxSettingsPopup { background: rgba(15, 15, 26, 240);"
    "  border: 1px solid #304050; border-radius: 4px; }"
    "QLabel { color: #8090a0; font-size: 11px; border: none; }"
    "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050;"
    "  border-radius: 3px; padding: 3px 8px; font-size: 11px; }"
    "QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; }");

static const QString kSliderStyle = QStringLiteral(
    "QSlider::groove:horizontal { height: 4px; background: #304050; border-radius: 2px; }"
    "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
    "  background: #c8d8e8; border-radius: 5px; }"
    "QSlider::handle:horizontal:hover { background: #00b4d8; }");

// Helper: create one label + slider + value-label row and append to layout.
// Returns the QSlider* so the caller can connect its valueChanged signal.
static QSlider* addSliderRow(QVBoxLayout* layout,
                             const QString& labelText,
                             int min, int max, int initialVal,
                             const QString& tooltip,
                             QLabel** valueLabel_out)
{
    auto* row   = new QHBoxLayout;

    auto* lbl   = new QLabel(labelText);
    lbl->setFixedWidth(90);
    if (!tooltip.isEmpty()) { lbl->setToolTip(tooltip); }
    lbl->setStyleSheet(QStringLiteral("QLabel { color: #8090a0; font-size: 10px; font-weight: bold; }"));
    row->addWidget(lbl);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(initialVal);
    slider->setStyleSheet(kSliderStyle);
    if (!tooltip.isEmpty()) { slider->setToolTip(tooltip); }
    row->addWidget(slider);

    auto* val = new QLabel(QString::number(initialVal));
    val->setStyleSheet(QStringLiteral("QLabel { color: #c8d8e8; min-width: 36px; }"));
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(val);

    layout->addLayout(row);

    if (valueLabel_out) {
        *valueLabel_out = val;
    }
    return slider;
}

VoxSettingsPopup::VoxSettingsPopup(int   currentThresholdDb,
                                   float currentGainScalar,
                                   int   currentHangTimeMs,
                                   QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("VoxSettingsPopup"));
    setStyleSheet(kPopupStyle);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(260);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 8, 10, 8);
    m_layout->setSpacing(6);

    // Title label
    auto* title = new QLabel(QStringLiteral("VOX Settings"));
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: #00d8a0; font-size: 11px; font-weight: bold; }"));
    m_layout->addWidget(title);

    // ── Threshold slider ─────────────────────────────────────────────────────
    // Range from Thetis console.Designer.cs:6018-6019 [v2.10.3.13]:
    //   ptbVOX.Maximum = 0, ptbVOX.Minimum = -80
    // Default in NereusSDR: -40 dB (conservative; plan §C.3)
    QLabel* threshVal = nullptr;
    const int clampedThresh = qBound(-80, currentThresholdDb, 0);
    QSlider* threshSlider = addSliderRow(
        m_layout,
        QStringLiteral("Threshold:"),
        -80, 0, clampedThresh,
        QStringLiteral("VOX activation threshold (dB).\n"
                       "Range from Thetis ptbVOX [-80, 0]."),
        &threshVal);
    threshVal->setText(QStringLiteral("%1 dB").arg(clampedThresh));
    ++m_sliderCount;

    connect(threshSlider, &QSlider::valueChanged, this,
            [this, threshVal](int v) {
        threshVal->setText(QStringLiteral("%1 dB").arg(v));
        emit thresholdDbChanged(v);
    });

    // ── Gain scalar slider ───────────────────────────────────────────────────
    // Thetis audio.cs:194 [v2.10.3.13]: private static float vox_gain = 1.0f;
    // NereusSDR display: int 0–200 representing float 0.00–2.00 (x100 mapping).
    // Upper bound 2.0 is a practical UX limit for the quick-popup; the full
    // range [0.0, 100.0] is accessible from Setup → Transmit → VOX.
    const int gainDisplay    = qBound(0, qRound(currentGainScalar * 100.0f), 200);
    QLabel* gainVal          = nullptr;
    QSlider* gainSlider = addSliderRow(
        m_layout,
        QStringLiteral("Gain:"),
        0, 200, gainDisplay,
        QStringLiteral("VOX gain scalar (x0.01 resolution).\n"
                       "From Thetis audio.cs:194 [v2.10.3.13]: vox_gain = 1.0f"),
        &gainVal);
    gainVal->setText(QStringLiteral("%1").arg(gainDisplay / 100.0, 0, 'f', 2));
    ++m_sliderCount;

    connect(gainSlider, &QSlider::valueChanged, this,
            [this, gainVal](int v) {
        gainVal->setText(QStringLiteral("%1").arg(v / 100.0, 0, 'f', 2));
        emit gainScalarChanged(static_cast<float>(v) / 100.0f);
    });

    // ── Hang time slider ─────────────────────────────────────────────────────
    // Range from Thetis setup.designer.cs:45005-45013 [v2.10.3.13]:
    //   udDEXPHold.Maximum = 2000, udDEXPHold.Minimum = 1  (units: ms)
    //   udDEXPHold.Value   = 500  (setup.designer.cs:45020-45024 [v2.10.3.13])
    const int clampedHang = qBound(1, currentHangTimeMs, 2000);
    QLabel* hangVal       = nullptr;
    QSlider* hangSlider = addSliderRow(
        m_layout,
        QStringLiteral("Hang (ms):"),
        1, 2000, clampedHang,
        QStringLiteral("VOX hang time in ms (delay before TX releases).\n"
                       "Range from Thetis udDEXPHold [1, 2000]."),
        &hangVal);
    hangVal->setText(QStringLiteral("%1 ms").arg(clampedHang));
    ++m_sliderCount;

    connect(hangSlider, &QSlider::valueChanged, this,
            [this, hangVal](int v) {
        hangVal->setText(QStringLiteral("%1 ms").arg(v));
        emit hangTimeMsChanged(v);
    });
}

void VoxSettingsPopup::showAt(const QPoint& globalPos)
{
    adjustSize();
    QPoint pos = globalPos;
    const QRect screen = QApplication::primaryScreen()->availableGeometry();
    if (pos.x() + width() > screen.right()) {
        pos.setX(screen.right() - width());
    }
    if (pos.y() + height() > screen.bottom()) {
        pos.setY(globalPos.y() - height());
    }
    move(pos);
    show();
}

} // namespace NereusSDR
