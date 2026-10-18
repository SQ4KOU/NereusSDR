// =================================================================
// src/gui/widgets/DspParamPopup.cpp  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR src/gui/DspParamPopup.cpp @ 0cd4559.
// AetherSDR has no per-file headers — project-level license applies
// (GPLv3 per https://github.com/ten9876/AetherSDR).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-23 — Imported from AetherSDR. Namespace changed from
//                AetherSDR to NereusSDR; otherwise byte-for-byte.
//                Authored by J.J. Boyd (KG4VCF) with AI-assisted
//                review via Anthropic Claude Code.
//   2026-10-10 - showAt() settles the popup's layouts after the show
//                and places the popup again when that changed its
//                size, so a popup opened at the screen's lower edge
//                stays on screen with the workspace look on. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Keep the popup within the click's screen and make oversized
//                content scrollable. J.J. Boyd (KG4VCF), OpenAI Codex.
// =================================================================

#include "DspParamPopup.h"
#include "GuardedSlider.h"
#include "DspPopupGeometry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include <QApplication>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

namespace NereusSDR {

// Existing AetherSDR popup minimum width, retained unchanged.
constexpr int kPopupMinimumWidth = 220;

static const QString kPopupStyle = QStringLiteral(
    "QWidget#DspParamPopup { background: rgba(15, 15, 26, 240);"
    "  border: 1px solid #304050; border-radius: 4px; }"
    "QLabel { color: #8090a0; font-size: 11px; border: none; }"
    "QRadioButton { color: #c8d8e8; font-size: 11px; }"
    "QCheckBox { color: #c8d8e8; font-size: 11px; }"
    "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050;"
    "  border-radius: 3px; padding: 3px 8px; font-size: 11px; }"
    "QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; }");

static const QString kSliderStyle = QStringLiteral(
    "QSlider::groove:horizontal { height: 4px; background: #304050; border-radius: 2px; }"
    "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
    "  background: #c8d8e8; border-radius: 5px; }"
    "QSlider::handle:horizontal:hover { background: #00b4d8; }");

DspParamPopup::DspParamPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName("DspParamPopup");
    setStyleSheet(kPopupStyle);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(kPopupMinimumWidth);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setWidgetResizable(true);
    m_content = new QWidget(m_scroll);
    m_content->installEventFilter(this);
    m_layout = new QVBoxLayout(m_content);
    m_scroll->setWidget(m_content);
    m_content->setAutoFillBackground(false);
    outer->addWidget(m_scroll);
    m_layout->setContentsMargins(10, 8, 10, 8);
    m_layout->setSpacing(6);
}

void DspParamPopup::addRadioGroup(const QString& label, const QStringList& options,
                                   int defaultIdx, std::function<void(int)> onChange)
{
    auto* lbl = new QLabel(label);
    lbl->setStyleSheet("QLabel { color: #8090a0; font-size: 10px; font-weight: bold; }");
    m_layout->addWidget(lbl);

    auto* row = new QHBoxLayout;
    auto* group = new QButtonGroup(this);
    for (int i = 0; i < options.size(); ++i) {
        auto* rb = new QRadioButton(options[i]);
        group->addButton(rb, i);
        row->addWidget(rb);
    }
    if (auto* btn = group->button(defaultIdx))
        btn->setChecked(true);
    m_layout->addLayout(row);

    connect(group, &QButtonGroup::idClicked, this, [onChange](int id) {
        if (onChange) onChange(id);
    });

    m_resetters.append([group, defaultIdx]() {
        if (auto* btn = group->button(defaultIdx))
            btn->setChecked(true);
    });
}

void DspParamPopup::addSlider(const QString& label, int min, int max, int defaultVal,
                               std::function<QString(int)> format,
                               std::function<void(int)> onChange,
                               const QString& tooltip,
                               int factoryDefault)
{
    auto* row = new QHBoxLayout;

    auto* lbl = new QLabel(label);
    lbl->setFixedWidth(90);
    if (!tooltip.isEmpty()) lbl->setToolTip(tooltip);
    row->addWidget(lbl);

    auto* slider = new GuardedSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(defaultVal);
    slider->setStyleSheet(kSliderStyle);
    if (!tooltip.isEmpty()) slider->setToolTip(tooltip);
    row->addWidget(slider);

    auto* val = new QLabel(format ? format(defaultVal) : QString::number(defaultVal));
    val->setStyleSheet("QLabel { color: #c8d8e8; min-width: 36px; }");
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!tooltip.isEmpty()) val->setToolTip(tooltip);
    row->addWidget(val);

    m_layout->addLayout(row);

    connect(slider, &QSlider::valueChanged, this, [val, format, onChange](int v) {
        if (format) val->setText(format(v));
        if (onChange) onChange(v);
    });

    // Reset-to-factory uses factoryDefault if supplied, else falls back to
    // the value we were constructed with (which typically reflects current
    // state, not a sensible factory default).
    const int resetVal = (factoryDefault == INT_MIN) ? defaultVal : factoryDefault;
    m_resetters.append([slider, resetVal]() {
        slider->setValue(resetVal);
    });
}

void DspParamPopup::addCheckbox(const QString& label, bool defaultVal,
                                 std::function<void(bool)> onChange)
{
    auto* cb = new QCheckBox(label);
    cb->setChecked(defaultVal);
    m_layout->addWidget(cb);

    connect(cb, &QCheckBox::toggled, this, [onChange](bool on) {
        if (onChange) onChange(on);
    });

    m_resetters.append([cb, defaultVal]() {
        cb->setChecked(defaultVal);
    });
}

void DspParamPopup::addWidget(QWidget* widget)
{
    if (widget) {
        m_layout->addWidget(widget);
    }
}

void DspParamPopup::finalize(std::function<void()> onMore, std::function<void()> onReset)
{
    m_layout->addSpacing(4);

    auto* btnRow = new QHBoxLayout;

    auto* moreBtn = new QPushButton("More Settings...");
    connect(moreBtn, &QPushButton::clicked, this, [this, onMore]() {
        if (onMore) onMore();
        close();
    });
    btnRow->addWidget(moreBtn);

    auto* resetBtn = new QPushButton("Reset");
    connect(resetBtn, &QPushButton::clicked, this, [this, onReset]() {
        for (auto& fn : m_resetters) fn();
        if (onReset) onReset();
    });
    btnRow->addWidget(resetBtn);

    m_layout->addLayout(btnRow);
}

// NereusSDR: every layout under `w` brought up to date now, innermost
// first, so a size change a child asked for reaches the window in this
// call instead of over the next turns of the event loop.
static void settleLayouts(QWidget* w)
{
    const QObjectList kids = w->children();
    for (QObject* kid : kids) {
        if (kid->isWidgetType()) {
            settleLayouts(static_cast<QWidget*>(kid));
        }
    }
    if (QLayout* layout = w->layout()) {
        layout->activate();
    }
}

QRect DspParamPopup::availableGeometry() const
{
    QScreen* target = QApplication::screenAt(m_anchor);
    if (!target && parentWidget()) {
        target = parentWidget()->screen();
    }
    if (!target) {
        target = QApplication::primaryScreen();
    }
    return target ? target->availableGeometry() : QRect();
}

QSize DspParamPopup::sizeHint() const
{
    QSize wanted = m_content->sizeHint().expandedTo(QSize(kPopupMinimumWidth, 0));
    if (m_anchored) {
        const QRect available = availableGeometry();
        if (available.isValid()) {
            if (wanted.height() > available.height()) {
                wanted.rwidth() += m_scroll->verticalScrollBar()->sizeHint().width();
            }
            wanted = wanted.boundedTo(available.size());
        }
    }
    return wanted;
}

void DspParamPopup::containPopup()
{
    if (!m_anchored || m_containing) {
        return;
    }
    m_containing = true;
    settleLayouts(m_content);
    const QRect available = availableGeometry();
    if (available.isValid()) {
        setMinimumWidth(qMin(kPopupMinimumWidth, available.width()));
        setMaximumSize(available.size());
        // Position above-right of the click point, keep on screen
        // [original inline comment from DspParamPopup.cpp:204]
        setGeometry(DspPopupGeometry::boundedGeometry(m_anchor, sizeHint(), available));
    }
    m_containing = false;
}

void DspParamPopup::scheduleContainment()
{
    if (!m_anchored || m_containing || m_containmentPending) {
        return;
    }
    m_containmentPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_containmentPending = false;
        containPopup();
    });
}

void DspParamPopup::showAt(const QPoint& globalPos)
{
    m_anchor = globalPos;
    m_anchored = true;
    containPopup();
    show();
    // NereusSDR: a control can ask for more room when it is shown. With the
    // workspace look on, the look's pass gives each slider thinner than its
    // handle a taller minimum on the slider's Show event
    // (WorkspacePreview.cpp, sliderFit), which leaves the layout to grow
    // the popup on a later turn of the event loop, after it was placed: a
    // popup placed against the screen's lower edge then ran past it by the
    // pixels it grew. So the layouts are settled here and, if that changed
    // the popup's size, it is placed again from the size it ends at. A
    // popup whose size holds (the look off) is not moved.
    settleLayouts(this);
    containPopup();
}

bool DspParamPopup::eventFilter(QObject* watched, QEvent* ev)
{
    if (watched == m_content && ev->type() == QEvent::LayoutRequest) {
        scheduleContainment();
    }
    return QWidget::eventFilter(watched, ev);
}

bool DspParamPopup::event(QEvent* ev)
{
    if (ev->type() == QEvent::LayoutRequest || ev->type() == QEvent::Resize) {
        scheduleContainment();
    }
    // Qt::Popup auto-closes on click outside — no custom handling needed
    return QWidget::event(ev);
}

} // namespace NereusSDR
