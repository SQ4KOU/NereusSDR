#include "VfoWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMenu>
#include <QFontDatabase>
#include <QSignalBlocker>

#include <cmath>
#include <algorithm>

namespace NereusSDR {

// Styles — matching dark theme from CONTRIBUTING.md
static const char* kFlatBtn =
    "QPushButton {"
    "  background: transparent; border: none;"
    "  padding: 1px 4px; font-size: 11px; font-weight: bold;"
    "}";

static const char* kTabBtn =
    "QPushButton {"
    "  background: transparent; border: none;"
    "  color: #6888a0; font-size: 12px; font-weight: bold;"
    "  padding: 2px 6px;"
    "}"
    "QPushButton:checked {"
    "  color: #00b4d8;"
    "  border-bottom: 2px solid #00b4d8;"
    "}";

// From AetherSDR VfoWidget.cpp:158-162 — kDspToggle style
static const char* kDspToggle =
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #304050;"
    "  border-radius: 2px; color: #c8d8e8;"
    "  font-size: 13px; font-weight: bold;"
    "  padding: 2px 4px; min-width: 32px;"
    "}"
    "QPushButton:checked {"
    "  background: #1a6030; color: #ffffff;"
    "  border: 1px solid #20a040;"
    "}"
    "QPushButton:hover {"
    "  border: 1px solid #0090e0;"
    "}";

// Same green toggle style as DSP tab for consistency
static const char* kFilterBtn =
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #304050;"
    "  border-radius: 2px; color: #c8d8e8;"
    "  font-size: 13px; font-weight: bold; padding: 3px;"
    "}"
    "QPushButton:checked {"
    "  background: #1a6030; color: #ffffff;"
    "  border: 1px solid #20a040;"
    "}"
    "QPushButton:hover {"
    "  border: 1px solid #0090e0;"
    "}";

// ---- Construction ----

VfoWidget::VfoWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(kWidgetW);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);

    buildUI();
}

VfoWidget::~VfoWidget() = default;

void VfoWidget::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    // From AetherSDR VfoWidget.cpp:237-238 — margins (6, 2, 6, 0)
    mainLayout->setContentsMargins(6, 2, 6, 0);
    mainLayout->setSpacing(2);

    buildHeaderRow();
    buildFrequencyRow();
    buildSmeterRow();
    buildTabBar();

    // Tab content stacked widget — HIDDEN by default (compact flag)
    // From AetherSDR VfoWidget.cpp:545 — m_tabStack->hide()
    m_tabStack = new QStackedWidget(this);
    buildAudioTab();
    buildDspTab();
    buildModeTab();

    // Stub X/RIT tab
    auto* ritWidget = new QWidget;
    auto* ritLayout = new QVBoxLayout(ritWidget);
    ritLayout->setContentsMargins(4, 4, 4, 4);
    auto* ritLabel = new QLabel(QStringLiteral("RIT/XIT"), ritWidget);
    ritLabel->setStyleSheet(QStringLiteral("color: #6888a0; font-size: 11px;"));
    ritLayout->addWidget(ritLabel);
    m_tabStack->addWidget(ritWidget);

    // Stub DAX tab
    auto* daxWidget = new QWidget;
    auto* daxLayout = new QVBoxLayout(daxWidget);
    daxLayout->setContentsMargins(4, 4, 4, 4);
    auto* daxLabel = new QLabel(QStringLiteral("DAX"), daxWidget);
    daxLabel->setStyleSheet(QStringLiteral("color: #6888a0; font-size: 11px;"));
    daxLayout->addWidget(daxLabel);
    m_tabStack->addWidget(daxWidget);

    mainLayout->addWidget(m_tabStack);
    m_tabStack->hide();  // Hidden by default — click tab to expand
    m_activeTab = -1;    // No tab active initially

    setLayout(mainLayout);
    adjustSize();

    // Floating buttons are children of our PARENT (SpectrumWidget)
    // so they render outside the VFO flag bounds. Deferred until
    // first updatePosition() when parentWidget() is available.
}

void VfoWidget::buildHeaderRow()
{
    auto* hdr = new QHBoxLayout;
    hdr->setSpacing(2);
    hdr->setContentsMargins(0, 0, 0, 0);

    // RX antenna button (blue)
    m_rxAntBtn = new QPushButton(QStringLiteral("ANT1"), this);
    m_rxAntBtn->setStyleSheet(QString(kFlatBtn) +
        QStringLiteral("QPushButton { color: #4488ff; }"));
    m_rxAntBtn->setFixedHeight(18);
    connect(m_rxAntBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        for (const QString& ant : m_antennaList) {
            QAction* act = menu.addAction(ant);
            act->setCheckable(true);
            act->setChecked(ant == m_rxAntBtn->text());
        }
        QAction* sel = menu.exec(m_rxAntBtn->mapToGlobal(
            QPoint(0, m_rxAntBtn->height())));
        if (sel) {
            m_rxAntBtn->setText(sel->text());
            emit rxAntennaChanged(sel->text());
        }
    });
    hdr->addWidget(m_rxAntBtn);

    // TX antenna button (red)
    m_txAntBtn = new QPushButton(QStringLiteral("ANT1"), this);
    m_txAntBtn->setStyleSheet(QString(kFlatBtn) +
        QStringLiteral("QPushButton { color: #ff4444; }"));
    m_txAntBtn->setFixedHeight(18);
    connect(m_txAntBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        for (const QString& ant : m_antennaList) {
            QAction* act = menu.addAction(ant);
            act->setCheckable(true);
            act->setChecked(ant == m_txAntBtn->text());
        }
        QAction* sel = menu.exec(m_txAntBtn->mapToGlobal(
            QPoint(0, m_txAntBtn->height())));
        if (sel) {
            m_txAntBtn->setText(sel->text());
            emit txAntennaChanged(sel->text());
        }
    });
    hdr->addWidget(m_txAntBtn);

    // Filter width label (cyan)
    m_filterWidthLbl = new QLabel(QStringLiteral("2.9K"), this);
    m_filterWidthLbl->setStyleSheet(
        QStringLiteral("color: #00c8ff; font-size: 11px; font-weight: bold;"));
    m_filterWidthLbl->setFixedHeight(18);
    hdr->addWidget(m_filterWidthLbl);

    hdr->addStretch();

    // TX badge
    m_txBadge = new QPushButton(QStringLiteral("TX"), this);
    m_txBadge->setFixedSize(28, 18);
    m_txBadge->setCheckable(true);
    m_txBadge->setStyleSheet(
        QStringLiteral("QPushButton { background: #1a2a3a; border: 1px solid #304050;"
                        "border-radius: 3px; color: #6888a0; font-size: 10px; font-weight: bold; }"
                        "QPushButton:checked { background: #6a3030; border-color: #ff4444; color: #ff8080; }"));
    hdr->addWidget(m_txBadge);

    // Slice letter badge
    m_sliceBadge = new QLabel(QStringLiteral("A"), this);
    m_sliceBadge->setFixedSize(18, 18);
    m_sliceBadge->setAlignment(Qt::AlignCenter);
    m_sliceBadge->setStyleSheet(
        QStringLiteral("background: #0070c0; color: white; font-size: 11px;"
                        "font-weight: bold; border-radius: 3px;"));
    hdr->addWidget(m_sliceBadge);

    static_cast<QVBoxLayout*>(layout())->addLayout(hdr);
}

void VfoWidget::buildFrequencyRow()
{
    m_freqStack = new QStackedWidget(this);
    m_freqStack->setFixedHeight(30);

    // Display label
    m_freqLabel = new QLabel(this);
    m_freqLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_freqLabel->setStyleSheet(
        QStringLiteral("color: #00e5ff; font-size: 24px; font-weight: bold;"
                        "font-family: 'Consolas', 'Menlo', monospace;"
                        "background: transparent;"
                        "border: 1px solid rgba(255,255,255,50);"
                        "border-radius: 3px; padding: 0 4px;"));
    updateFreqLabel();
    m_freqStack->addWidget(m_freqLabel);

    // Edit field
    m_freqEdit = new QLineEdit(this);
    m_freqEdit->setAlignment(Qt::AlignRight);
    m_freqEdit->setStyleSheet(
        QStringLiteral("color: #00e5ff; font-size: 20px; font-weight: bold;"
                        "font-family: 'Consolas', 'Menlo', monospace;"
                        "background: #0a0a18; border: 1px solid #00b4d8;"
                        "border-radius: 3px; padding: 0 4px;"));
    connect(m_freqEdit, &QLineEdit::returnPressed, this, [this]() {
        QString text = m_freqEdit->text().replace(QLatin1Char('.'), QString());
        bool ok = false;
        double mhz = text.toDouble(&ok);
        if (ok && mhz > 0.0) {
            // If user typed a small number, assume MHz; otherwise Hz
            double hz = (mhz < 1000.0) ? mhz * 1e6 : mhz;
            hz = std::clamp(hz, 100000.0, 61440000.0);
            m_frequency = hz;
            updateFreqLabel();
            emit frequencyChanged(hz);
        }
        m_freqStack->setCurrentIndex(0);
    });
    connect(m_freqEdit, &QLineEdit::editingFinished, this, [this]() {
        m_freqStack->setCurrentIndex(0);
    });
    m_freqStack->addWidget(m_freqEdit);

    // Double-click on label → edit
    m_freqLabel->installEventFilter(this);

    static_cast<QVBoxLayout*>(layout())->addWidget(m_freqStack);
}

void VfoWidget::buildSmeterRow()
{
    // S-meter: custom painted in paintEvent, with dBm label
    m_smeterLabel = new QLabel(QStringLiteral("-127 dBm"), this);
    m_smeterLabel->setFixedHeight(20);
    m_smeterLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_smeterLabel->setStyleSheet(
        QStringLiteral("color: #6888a0; font-size: 10px; background: transparent;"));
    static_cast<QVBoxLayout*>(layout())->addWidget(m_smeterLabel);
}

void VfoWidget::buildTabBar()
{
    auto* tabLayout = new QHBoxLayout;
    tabLayout->setSpacing(0);
    tabLayout->setContentsMargins(0, 0, 0, 0);

    // Tab labels — from AetherSDR VfoWidget.cpp:522
    // [🔊] | DSP | USB | X/RIT | DAX
    QStringList tabLabels = {
        QString::fromUtf8("\xF0\x9F\x94\x8A"),  // 🔊 speaker
        QStringLiteral("DSP"),
        SliceModel::modeName(m_currentMode),
        QStringLiteral("X/RIT"),
        QStringLiteral("DAX")
    };

    for (int i = 0; i < tabLabels.size(); ++i) {
        // Add separator before each tab except the first
        // From AetherSDR VfoWidget.cpp:523-530
        if (i > 0) {
            auto* sep = new QLabel(QStringLiteral("|"), this);
            sep->setStyleSheet(QStringLiteral(
                "QLabel { background: transparent; border: none;"
                "color: rgba(255,255,255,80); font-size: 13px; padding: 0; }"));
            sep->setFixedWidth(6);
            tabLayout->addWidget(sep);
        }

        auto* btn = new QPushButton(tabLabels[i], this);
        btn->setCheckable(true);
        btn->setStyleSheet(kTabBtn);
        btn->setFixedHeight(24);  // 24px from AetherSDR
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (m_activeTab == i) {
                // Toggle: clicking active tab hides content
                m_tabStack->hide();
                m_activeTab = -1;
                for (auto* b : m_tabButtons) { b->setChecked(false); }
            } else {
                m_activeTab = i;
                m_tabStack->setCurrentIndex(i);
                m_tabStack->show();
                for (int j = 0; j < m_tabButtons.size(); ++j) {
                    m_tabButtons[j]->setChecked(j == i);
                }
            }
            adjustSize();
            // Notify parent to reposition
            if (parentWidget()) {
                parentWidget()->update();
            }
        });
        tabLayout->addWidget(btn, 1);  // stretch equally
        m_tabButtons.append(btn);
    }

    static_cast<QVBoxLayout*>(layout())->addLayout(tabLayout);
}

void VfoWidget::buildAudioTab()
{
    auto* audioWidget = new QWidget;
    auto* audioLayout = new QVBoxLayout(audioWidget);
    audioLayout->setContentsMargins(4, 4, 4, 4);
    audioLayout->setSpacing(4);

    // AF Gain slider
    {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(QStringLiteral("AF"), audioWidget);
        label->setStyleSheet(QStringLiteral("color: #8899aa; font-size: 11px;"));
        label->setFixedWidth(24);
        row->addWidget(label);

        m_afGainSlider = new QSlider(Qt::Horizontal, audioWidget);
        m_afGainSlider->setRange(0, 100);
        m_afGainSlider->setValue(50);
        m_afGainSlider->setStyleSheet(
            QStringLiteral("QSlider::groove:horizontal { background: #1a2a3a; height: 6px; border-radius: 3px; }"
                            "QSlider::handle:horizontal { background: #00b4d8; width: 12px; margin: -3px 0; border-radius: 6px; }"));
        row->addWidget(m_afGainSlider);

        m_afGainLabel = new QLabel(QStringLiteral("50"), audioWidget);
        m_afGainLabel->setStyleSheet(QStringLiteral("color: #c8d8e8; font-size: 11px;"));
        m_afGainLabel->setFixedWidth(24);
        m_afGainLabel->setAlignment(Qt::AlignRight);
        row->addWidget(m_afGainLabel);

        connect(m_afGainSlider, &QSlider::valueChanged, this, [this](int val) {
            m_afGainLabel->setText(QString::number(val));
            if (!m_updatingFromModel) {
                emit afGainChanged(val);
            }
        });
        audioLayout->addLayout(row);
    }

    // AGC combo
    {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(QStringLiteral("AGC"), audioWidget);
        label->setStyleSheet(QStringLiteral("color: #8899aa; font-size: 11px;"));
        label->setFixedWidth(24);
        row->addWidget(label);

        m_agcCmb = new QComboBox(audioWidget);
        m_agcCmb->addItems({
            QStringLiteral("Off"), QStringLiteral("Long"),
            QStringLiteral("Slow"), QStringLiteral("Med"),
            QStringLiteral("Fast")
        });
        m_agcCmb->setCurrentIndex(3);  // Med
        m_agcCmb->setStyleSheet(
            QStringLiteral("QComboBox { background: #1a2a3a; color: #c8d8e8;"
                            "border: 1px solid #304050; border-radius: 3px;"
                            "padding: 1px 4px; font-size: 11px; }"
                            "QComboBox::drop-down { border: none; }"
                            "QComboBox QAbstractItemView { background: #1a2a3a; color: #c8d8e8;"
                            "selection-background-color: #0070c0; }"));
        connect(m_agcCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int index) {
            if (!m_updatingFromModel && index >= 0) {
                emit agcModeChanged(static_cast<AGCMode>(index));
            }
        });
        row->addWidget(m_agcCmb);
        audioLayout->addLayout(row);
    }

    audioLayout->addStretch();
    m_tabStack->addWidget(audioWidget);
}

void VfoWidget::buildDspTab()
{
    auto* dspWidget = new QWidget;
    auto* dspLayout = new QHBoxLayout(dspWidget);
    dspLayout->setContentsMargins(4, 4, 4, 4);
    dspLayout->setSpacing(4);

    auto makeToggle = [&](const QString& label) -> QPushButton* {
        auto* btn = new QPushButton(label, dspWidget);
        btn->setCheckable(true);
        btn->setStyleSheet(kDspToggle);
        dspLayout->addWidget(btn);
        return btn;
    };

    m_nbBtn = makeToggle(QStringLiteral("NB"));
    connect(m_nbBtn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel) { emit nb1Changed(on); }
    });

    m_nrBtn = makeToggle(QStringLiteral("NR"));
    connect(m_nrBtn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel) { emit nrChanged(on); }
    });

    m_anfBtn = makeToggle(QStringLiteral("ANF"));
    connect(m_anfBtn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel) { emit anfChanged(on); }
    });

    dspLayout->addStretch();

    m_tabStack->addWidget(dspWidget);
}

void VfoWidget::buildModeTab()
{
    auto* modeWidget = new QWidget;
    auto* modeLayout = new QVBoxLayout(modeWidget);
    modeLayout->setContentsMargins(4, 4, 4, 4);
    modeLayout->setSpacing(4);

    // Mode combo
    {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(QStringLiteral("Mode"), modeWidget);
        label->setStyleSheet(QStringLiteral("color: #8899aa; font-size: 11px;"));
        label->setFixedWidth(32);
        row->addWidget(label);

        m_modeCmb = new QComboBox(modeWidget);
        // From Thetis enums.cs DSPMode — common modes
        m_modeCmb->addItems({
            QStringLiteral("LSB"), QStringLiteral("USB"),
            QStringLiteral("AM"), QStringLiteral("CWL"),
            QStringLiteral("CWU"), QStringLiteral("FM"),
            QStringLiteral("DIGU"), QStringLiteral("DIGL"),
            QStringLiteral("SAM")
        });
        m_modeCmb->setCurrentText(QStringLiteral("USB"));
        m_modeCmb->setStyleSheet(
            QStringLiteral("QComboBox { background: #1a2a3a; color: #c8d8e8;"
                            "border: 1px solid #304050; border-radius: 3px;"
                            "padding: 1px 4px; font-size: 11px; }"
                            "QComboBox::drop-down { border: none; }"
                            "QComboBox QAbstractItemView { background: #1a2a3a; color: #c8d8e8;"
                            "selection-background-color: #0070c0; }"));
        connect(m_modeCmb, &QComboBox::currentTextChanged,
                this, [this](const QString& text) {
            if (!m_updatingFromModel) {
                DSPMode mode = SliceModel::modeFromName(text);
                m_currentMode = mode;
                rebuildFilterButtons(mode);
                // Update mode tab label
                if (m_tabButtons.size() > 2) {
                    m_tabButtons[2]->setText(text);
                }
                emit modeChanged(mode);
            }
        });
        row->addWidget(m_modeCmb);
        modeLayout->addLayout(row);
    }

    // Filter preset buttons (dynamic per mode)
    m_filterBtnContainer = new QWidget(modeWidget);
    modeLayout->addWidget(m_filterBtnContainer);
    rebuildFilterButtons(DSPMode::USB);

    // RF Gain slider
    {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(QStringLiteral("RF"), modeWidget);
        label->setStyleSheet(QStringLiteral("color: #8899aa; font-size: 11px;"));
        label->setFixedWidth(24);
        row->addWidget(label);

        m_rfGainSlider = new QSlider(Qt::Horizontal, modeWidget);
        m_rfGainSlider->setRange(0, 100);
        m_rfGainSlider->setValue(80);
        m_rfGainSlider->setStyleSheet(
            QStringLiteral("QSlider::groove:horizontal { background: #1a2a3a; height: 6px; border-radius: 3px; }"
                            "QSlider::handle:horizontal { background: #00b4d8; width: 12px; margin: -3px 0; border-radius: 6px; }"));
        row->addWidget(m_rfGainSlider);

        m_rfGainLabel = new QLabel(QStringLiteral("80"), modeWidget);
        m_rfGainLabel->setStyleSheet(QStringLiteral("color: #c8d8e8; font-size: 11px;"));
        m_rfGainLabel->setFixedWidth(24);
        m_rfGainLabel->setAlignment(Qt::AlignRight);
        row->addWidget(m_rfGainLabel);

        connect(m_rfGainSlider, &QSlider::valueChanged, this, [this](int val) {
            m_rfGainLabel->setText(QString::number(val));
            if (!m_updatingFromModel) {
                emit rfGainChanged(val);
            }
        });
        modeLayout->addLayout(row);
    }

    modeLayout->addStretch();
    m_tabStack->addWidget(modeWidget);
}

void VfoWidget::rebuildFilterButtons(DSPMode mode)
{
    // Remove old layout and buttons
    if (m_filterBtnContainer->layout()) {
        QLayoutItem* item;
        while ((item = m_filterBtnContainer->layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete m_filterBtnContainer->layout();
    }

    auto* grid = new QHBoxLayout(m_filterBtnContainer);
    grid->setSpacing(2);
    grid->setContentsMargins(0, 0, 0, 0);

    // Per-mode filter presets — ported from Thetis console.cs:5180-5575
    // Showing a selection of useful widths for each mode family
    struct Preset { const char* label; int low; int high; };

    QVector<Preset> presets;
    switch (mode) {
    case DSPMode::LSB:
        // From Thetis console.cs:5191-5231 (LSB F1-F10)
        presets = {{"5.0K",-5100,-100}, {"3.8K",-3900,-100}, {"2.9K",-3000,-100},
                   {"2.7K",-2800,-100}, {"2.4K",-2500,-100}, {"1.8K",-1900,-100}};
        break;
    case DSPMode::USB:
        // From Thetis console.cs:5233-5273 (USB F1-F10)
        presets = {{"5.0K",100,5100}, {"3.8K",100,3900}, {"2.9K",100,3000},
                   {"2.7K",100,2800}, {"2.4K",100,2500}, {"1.8K",100,1900}};
        break;
    case DSPMode::CWL:
    case DSPMode::CWU: {
        // From Thetis console.cs:5359-5441 — centered on cw_pitch (600 Hz)
        int sign = (mode == DSPMode::CWL) ? -1 : 1;
        int p = 600;  // From Thetis display.cs:1023
        presets = {{"1.0K", sign*(p-500), sign*(p+500)},
                   {"500",  sign*(p-250), sign*(p+250)},
                   {"400",  sign*(p-200), sign*(p+200)},
                   {"250",  sign*(p-125), sign*(p+125)},
                   {"100",  sign*(p-50),  sign*(p+50)}};
        break;
    }
    case DSPMode::AM:
    case DSPMode::SAM:
        // From Thetis console.cs:5443-5525 (AM/SAM F1-F10)
        presets = {{"20K",-10000,10000}, {"10K",-5000,5000}, {"8.0K",-4000,4000},
                   {"6.0K",-3000,3000}, {"5.0K",-2500,2500}};
        break;
    case DSPMode::FM:
        presets = {{"16K",-8000,8000}, {"12K",-6000,6000}, {"8.0K",-4000,4000}};
        break;
    case DSPMode::DIGU: {
        // From Thetis console.cs:5317-5357, offset=1500
        int o = 1500;
        presets = {{"3.0K",o-1500,o+1500}, {"2.0K",o-1000,o+1000},
                   {"1.0K",o-500,o+500}, {"600",o-300,o+300}};
        break;
    }
    case DSPMode::DIGL: {
        // From Thetis console.cs:5275-5315, offset=2210
        int o = 2210;
        presets = {{"3.0K",-(o+1500),-(o-1500)}, {"2.0K",-(o+1000),-(o-1000)},
                   {"1.0K",-(o+500),-(o-500)}, {"600",-(o+300),-(o-300)}};
        break;
    }
    default:
        presets = {{"10K",-5000,5000}, {"6.0K",-3000,3000}};
        break;
    }

    auto [defLow, defHigh] = SliceModel::defaultFilterForMode(mode);

    for (const auto& p : presets) {
        auto* btn = new QPushButton(QString::fromLatin1(p.label), m_filterBtnContainer);
        btn->setCheckable(true);
        btn->setStyleSheet(kFilterBtn);
        btn->setFixedHeight(26);
        int low = p.low;
        int high = p.high;
        btn->setProperty("filterLow", low);
        btn->setProperty("filterHigh", high);
        // Check if this matches current filter
        if (low == defLow && high == defHigh) {
            btn->setChecked(true);
        }
        // Exclusive toggle: click selects this preset, emits filterChanged
        connect(btn, &QPushButton::clicked, this, [this, low, high, btn](bool checked) {
            if (!checked) {
                // Don't allow unchecking the active preset — keep it toggled on
                btn->setChecked(true);
                return;
            }
            // Uncheck all other filter buttons (exclusive group)
            for (auto* child : m_filterBtnContainer->findChildren<QPushButton*>()) {
                if (child != btn) {
                    child->setChecked(false);
                }
            }
            if (!m_updatingFromModel) {
                emit filterChanged(low, high);
            }
        });
        grid->addWidget(btn);
    }

    m_filterBtnContainer->setLayout(grid);
}

// ---- State setters (guarded) ----

void VfoWidget::setFrequency(double hz)
{
    m_updatingFromModel = true;
    m_frequency = hz;
    updateFreqLabel();
    m_updatingFromModel = false;
}

void VfoWidget::setMode(DSPMode mode)
{
    m_updatingFromModel = true;
    m_currentMode = mode;
    QString name = SliceModel::modeName(mode);
    m_modeCmb->setCurrentText(name);
    if (m_tabButtons.size() > 2) {
        m_tabButtons[2]->setText(name);
    }
    rebuildFilterButtons(mode);
    m_updatingFromModel = false;
}

void VfoWidget::setFilter(int low, int high)
{
    m_updatingFromModel = true;
    m_filterWidthLbl->setText(formatFilterWidth(low, high));
    // Update checked state of filter buttons — match by stored property
    for (auto* btn : m_filterBtnContainer->findChildren<QPushButton*>()) {
        int bLow = btn->property("filterLow").toInt();
        int bHigh = btn->property("filterHigh").toInt();
        btn->setChecked(bLow == low && bHigh == high);
    }
    m_updatingFromModel = false;
}

void VfoWidget::setAgcMode(AGCMode mode)
{
    m_updatingFromModel = true;
    m_agcCmb->setCurrentIndex(static_cast<int>(mode));
    m_updatingFromModel = false;
}

void VfoWidget::setAfGain(int gain)
{
    m_updatingFromModel = true;
    m_afGainSlider->setValue(gain);
    m_afGainLabel->setText(QString::number(gain));
    m_updatingFromModel = false;
}

void VfoWidget::setRfGain(int gain)
{
    m_updatingFromModel = true;
    m_rfGainSlider->setValue(gain);
    m_rfGainLabel->setText(QString::number(gain));
    m_updatingFromModel = false;
}

void VfoWidget::setRxAntenna(const QString& ant)
{
    m_updatingFromModel = true;
    m_rxAntBtn->setText(ant);
    m_updatingFromModel = false;
}

void VfoWidget::setTxAntenna(const QString& ant)
{
    m_updatingFromModel = true;
    m_txAntBtn->setText(ant);
    m_updatingFromModel = false;
}

void VfoWidget::setStepHz(int hz)
{
    m_stepHz = hz;
}

void VfoWidget::setSliceIndex(int index)
{
    m_sliceIndex = index;
    static const QChar letters[] = {'A', 'B', 'C', 'D'};
    if (index >= 0 && index < 4) {
        m_sliceBadge->setText(QString(letters[index]));
        QColor c = sliceColor(index);
        m_sliceBadge->setStyleSheet(
            QStringLiteral("background: %1; color: white; font-size: 11px;"
                            "font-weight: bold; border-radius: 3px;").arg(c.name()));
    }
}

void VfoWidget::setTxSlice(bool isTx)
{
    m_txBadge->setChecked(isTx);
}

void VfoWidget::setAntennaList(const QStringList& ants)
{
    m_antennaList = ants;
}

void VfoWidget::setSmeter(double dbm)
{
    m_smeterDbm = dbm;
    m_smeterLabel->setText(QStringLiteral("%1 dBm").arg(dbm, 0, 'f', 0));
    update();  // repaint S-meter bar
}

// ---- Floating control buttons (AetherSDR pattern) ----
// Close, Lock, Record, Play — rendered on parent SpectrumWidget

static const char* kFloatingBtn =
    "QPushButton {"
    "  background: rgba(255,255,255,15); border: none;"
    "  border-radius: 10px; color: #c8d8e8; font-size: 11px; padding: 0;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(255,255,255,40);"
    "}";

static const char* kFloatingBtnClose =
    "QPushButton {"
    "  background: rgba(255,255,255,15); border: none;"
    "  border-radius: 10px; color: #c8d8e8; font-size: 11px; padding: 0;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(204,32,32,180); color: #ffffff;"
    "}";

// Not wired up yet — show red X overlay
static const char* kFloatingBtnDisabled =
    "QPushButton {"
    "  background: rgba(255,255,255,8); border: none;"
    "  border-radius: 10px; color: #556070; font-size: 11px; padding: 0;"
    "}";

void VfoWidget::buildFloatingButtons()
{
    QWidget* parent = parentWidget();
    if (!parent || m_closeBtn) {
        return;  // Already built or no parent
    }

    auto makeBtn = [&](const QString& text, const char* style) -> QPushButton* {
        auto* btn = new QPushButton(text, parent);
        btn->setFixedSize(20, 20);
        btn->setStyleSheet(style);
        btn->show();
        return btn;
    };

    // Close button — wired
    m_closeBtn = makeBtn(QStringLiteral("\u2715"), kFloatingBtnClose);
    m_closeBtn->setToolTip(QStringLiteral("Close slice"));
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested(m_sliceIndex);
    });

    // Lock button — wired
    m_lockBtn = makeBtn(QStringLiteral("\U0001F513"), kFloatingBtn);
    m_lockBtn->setToolTip(QStringLiteral("Lock VFO frequency"));
    m_lockBtn->setCheckable(true);
    connect(m_lockBtn, &QPushButton::toggled, this, [this](bool locked) {
        m_locked = locked;
        m_lockBtn->setText(locked ? QStringLiteral("\U0001F512") : QStringLiteral("\U0001F513"));
        if (locked) {
            m_lockBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: rgba(255,100,100,80); border: none;"
                "  border-radius: 10px; color: #c8d8e8; font-size: 11px; padding: 0; }"
                "QPushButton:hover { background: rgba(255,100,100,120); }"));
        } else {
            m_lockBtn->setStyleSheet(kFloatingBtn);
        }
        emit lockChanged(locked);
    });

    // Record button — not wired yet
    m_recBtn = makeBtn(QStringLiteral("\u23FA"), kFloatingBtnDisabled);
    m_recBtn->setToolTip(QStringLiteral("Record (not implemented)"));

    // Play button — not wired yet
    m_playBtn = makeBtn(QStringLiteral("\u25B6"), kFloatingBtnDisabled);
    m_playBtn->setToolTip(QStringLiteral("Play (not implemented)"));
}

void VfoWidget::positionFloatingButtons()
{
    if (!m_closeBtn) {
        return;
    }

    // Stack vertically on the opposite side of the flag from the VFO marker
    // From AetherSDR VfoWidget.cpp:1724-1749
    int btnX;
    if (m_onLeft) {
        // Flag is on the left of marker → buttons on right side of flag
        btnX = x() + width() + 2;
    } else {
        // Flag is on the right of marker → buttons on left side of flag
        btnX = x() - 22;
    }

    // Clamp to parent bounds
    if (parentWidget()) {
        btnX = std::clamp(btnX, 0, parentWidget()->width() - 20);
    }

    int btnY = y();
    QPushButton* btns[] = {m_closeBtn, m_lockBtn, m_recBtn, m_playBtn};
    for (QPushButton* btn : btns) {
        btn->move(btnX, btnY);
        btn->setVisible(isVisible());
        if (isVisible()) {
            btn->raise();
        }
        btnY += 22;
    }
}

// ---- Positioning ----

void VfoWidget::updatePosition(int vfoX, int specTop, FlagDir dir)
{
    // Build floating buttons on first call (parent is now available)
    if (!m_closeBtn && parentWidget()) {
        buildFloatingButtons();
    }

    int flagW = width();
    int parentW = parentWidget() ? parentWidget()->width() : 2000;
    bool onLeft = false;

    if (dir == FlagDir::ForceLeft) {
        onLeft = true;
    } else if (dir == FlagDir::ForceRight) {
        onLeft = false;
    } else {
        // Auto: flag goes OPPOSITE side of passband so it doesn't cover signals.
        // From AetherSDR VfoWidget.cpp:1696 — onLeft = !lowerSideband
        bool lowerSideband = (m_currentMode == DSPMode::LSB ||
                              m_currentMode == DSPMode::DIGL ||
                              m_currentMode == DSPMode::CWL);
        onLeft = !lowerSideband;
    }

    int x;
    if (onLeft) {
        x = vfoX - flagW;
        // Flip to right if clipped off left edge
        if (x < 0) {
            x = vfoX;
            onLeft = false;
        }
    } else {
        x = vfoX;
        // Flip to left if clipped off right edge
        if (x + flagW > parentW) {
            x = vfoX - flagW;
            onLeft = true;
        }
    }

    // Final clamp to stay on screen
    x = std::clamp(x, 0, std::max(0, parentW - flagW));

    m_onLeft = onLeft;
    move(x, specTop);
    positionFloatingButtons();
}

// ---- Painting ----

void VfoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark panel background — from AetherSDR VfoWidget::paintEvent
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x0a, 0x0a, 0x14, 230));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

    // Subtle border
    p.setPen(QColor(255, 255, 255, 30));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

    // Colored top border matching slice color
    QColor c = sliceColor(m_sliceIndex);
    p.setPen(QPen(c, 2));
    p.drawLine(2, 1, width() - 3, 1);
}

void VfoWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    emit sliceActivationRequested(m_sliceIndex);

    // Double-click on frequency area → enter edit mode
    if (event->type() == QEvent::MouseButtonDblClick) {
        QRect freqRect = m_freqStack->geometry();
        if (freqRect.contains(event->pos())) {
            // Format current frequency as MHz for editing
            double mhz = m_frequency / 1e6;
            m_freqEdit->setText(QString::number(mhz, 'f', 6));
            m_freqEdit->selectAll();
            m_freqStack->setCurrentIndex(1);
            m_freqEdit->setFocus();
        }
    }
}

void VfoWidget::wheelEvent(QWheelEvent* event)
{
    event->accept();
    if (m_locked) {
        return;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        return;
    }
    int steps = (delta > 0) ? 1 : -1;
    double newFreq = m_frequency + steps * m_stepHz;
    newFreq = std::clamp(newFreq, 100000.0, 61440000.0);

    if (!qFuzzyCompare(newFreq, m_frequency)) {
        m_frequency = newFreq;
        updateFreqLabel();
        emit frequencyChanged(newFreq);
    }
}

// ---- Helpers ----

void VfoWidget::updateFreqLabel()
{
    // Format: "14.225.000" (MHz with period separators every 3 digits after decimal)
    // From Thetis txtVFOAFreq format: freq.ToString("f6")
    double mhz = m_frequency / 1e6;
    int intPart = static_cast<int>(mhz);
    int fracPart = static_cast<int>(std::round((mhz - intPart) * 1e6));
    int khz = fracPart / 1000;
    int hz = fracPart % 1000;

    m_freqLabel->setText(QStringLiteral("%1.%2.%3")
        .arg(intPart)
        .arg(khz, 3, 10, QLatin1Char('0'))
        .arg(hz, 3, 10, QLatin1Char('0')));

    // Also update filter width display
    // (will be properly synced from setFilter)
}

QString VfoWidget::formatFilterWidth(int low, int high) const
{
    int width = std::abs(high - low);
    if (width >= 1000) {
        return QStringLiteral("%1K").arg(width / 1000.0, 0, 'f', 1);
    }
    return QString::number(width);
}

QColor VfoWidget::sliceColor(int index)
{
    // From AetherSDR SliceColors.h
    switch (index) {
    case 0: return QColor(0x00, 0xd4, 0xff);  // cyan
    case 1: return QColor(0xff, 0x40, 0xff);  // magenta
    case 2: return QColor(0x40, 0xff, 0x40);  // green
    case 3: return QColor(0xff, 0xff, 0x00);  // yellow
    default: return QColor(0x00, 0xd4, 0xff);
    }
}

} // namespace NereusSDR
