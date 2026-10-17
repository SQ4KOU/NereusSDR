// no-port-check: NereusSDR-original visual preview tool; controls comparison sheet.
// =================================================================
// tests/visual/ControlsSheet.cpp  (NereusSDR)
// =================================================================
//
// See ControlsSheet.h. Every pixel on the sheet is a grab of a real Qt
// widget: the control cells, the popups, the panel crops of the main
// window and the TX EQ dialog, and the QLabels that carry the titles and
// hold those grabs. The zoom image enlarges cut-outs of the same grabs
// 5x, nearest-neighbour.
//
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Choice sheets: checkbox and radio off states, lit push
//                 buttons, field and button sizes. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - controls-sheet--lit-green: stronger greens for lit
//                 buttons, from palette roles only; controls-sheet--
//                 scrollbars: 8, 10 and 14 px bars. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - measureAppletFit: every visible container holding
//                 applets, checked like the strip-fit scenes. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Colour choices: colour-choice--status (+ --zoom) and
//                 colour-choice--data (+ --crops), every variant from one
//                 build through WorkspacePreview's test seams. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - The decided colours: sceneStatusColoursDecided
//                 (decided--status, with the pulse's two steps) and
//                 sceneDataColoursDecided (decided--data, with a user
//                 container background and user item colours) replace the
//                 choice sheets. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - sceneControlsDecided (decided--controls + --zoom): JJ's
//                 decided sliders, indicators, lit buttons, radii and
//                 scroll bars through the product path. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - decided--controls--zoom: the field row zooms each whole
//                 control instead of showing an empty row. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - decided--data: the EQ plot now takes the dialog's
//                 app-bg (D22), so its colour line says so. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "ControlsSheet.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEnterEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHoverEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <tuple>
#include <vector>

#include "LongpathControlStyles.h"
#include "PreviewData.h"
#include "PreviewScenes.h"
#include "core/TciServer.h"
#include "gui/MainWindow.h"
#include "gui/NetworkDiagnosticsDialog.h"
#include "gui/SMeterWidget.h"
#include "gui/SpectrumWidget.h"
#include "gui/StyleConstants.h"
#include "gui/TimeSeriesGraphWidget.h"
#include "gui/TitleBar.h"
#include "gui/applets/PhoneCwApplet.h"
#include "gui/applets/RxApplet.h"
#include "gui/applets/TxApplet.h"
#include "gui/applets/TxEqDialog.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/meters/MeterItem.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/presets/BarPresetItem.h"
#include "gui/setup/CatNetworkSetupPages.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/PopupMenuStyle.h"
#include "gui/styles/QssTranslator.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/styles/WorkspaceStyle.h"
#include "gui/widgets/FilterPassbandWidget.h"
#include "gui/widgets/MeterSlider.h"
#include "gui/widgets/ParametricEqWidget.h"
#include "gui/widgets/StatusBadge.h"

namespace NereusSDR::Preview {

namespace {

constexpr int kLookCount = 3;
constexpr int kToday = 0;
constexpr int kPreview = 1;
constexpr int kLongpath = 2;

constexpr int kSettleMs = 700;
constexpr int kShortSettleMs = 250;

// Control cells: the control sits kCellPadPx inside a cell that fills with
// the look's window colour.
constexpr int kCellPadPx = 6;
constexpr int kSliderValue = 60;
constexpr int kSliderMax = 100;
constexpr int kHSliderWidth = 140;
constexpr int kHSliderHeight = 18;     // RxApplet / TxApplet slider rows
constexpr int kVSliderWidth = 22;
constexpr int kVSliderHeight = 100;
constexpr int kToggleWidth = 120;
constexpr int kToggleHeight = 22;
constexpr int kFieldWidth = 140;
constexpr int kFieldHeight = 24;
constexpr int kSpinWidth = 100;
constexpr int kTabsWidth = 240;
constexpr int kTabsHeight = 100;
constexpr int kScrollBarHeight = 110;
constexpr int kScrollBarPage = 30;
constexpr int kScrollBarValue = 20;
constexpr int kGroupWidth = 200;
constexpr int kGroupHeight = 80;
constexpr int kButtonWidth = 84;
constexpr int kButtonHeight = 24;
constexpr int kMeterSliderWidth = 160;
constexpr float kMeterSliderGain = 0.6f;
constexpr float kMeterSliderLevel = 0.45f;
constexpr int kSpinValue = 42;
constexpr int kComboHoverRow = 1;

// Panel crops: the slider rows' band, widened to the applet, plus this
// much above and below.
constexpr int kBandPadPx = 4;

// Zoom: this much logical source around each handle or indicator, 5x.
constexpr int kZoomSourceWidth = 32;
constexpr int kZoomSourceHeight = 24;
constexpr int kZoomFactor = 5;

// Sheet layout (logical px).
constexpr int kSheetMarginPx = 16;
constexpr int kRowLabelWidth = 170;
constexpr int kShotGapPx = 10;
constexpr int kColumnGapPx = 24;
constexpr int kRowGapPx = 14;
constexpr int kMinColumnWidth = 260;
constexpr auto kSheetBg = "#141418";
constexpr auto kSheetRule = "#2a2a30";
constexpr auto kSheetTitleColour = "#e4e4e8";
constexpr auto kSheetTextColour = "#c4c4ca";
constexpr auto kSheetCaptionColour = "#8a8a92";

QString lookKey(int look)
{
    switch (look) {
    case kToday: return QStringLiteral("today");
    case kPreview: return QStringLiteral("preview");
    default: return QStringLiteral("longpath");
    }
}

QString lookTitle(int look)
{
    switch (look) {
    case kToday: return QStringLiteral("Today (shipped, preview switch off)");
    case kPreview: return QStringLiteral("Preview now (translator + componentize)");
    default: return QStringLiteral("Longpath (its own stylesheets)");
    }
}

// Real QSlider / QCheckBox / QRadioButton (no Q_OBJECT, so stylesheets see
// the Qt class) that can say where the style puts the handle or indicator.
class ProbeSlider : public QSlider
{
public:
    using QSlider::QSlider;
    QRect handleRect() const
    {
        QStyleOptionSlider o;
        initStyleOption(&o);
        return style()->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, this);
    }
};

class ProbeCheckBox : public QCheckBox
{
public:
    using QCheckBox::QCheckBox;
    QRect indicatorRect() const
    {
        QStyleOptionButton o;
        initStyleOption(&o);
        return style()->subElementRect(QStyle::SE_CheckBoxIndicator, &o, this);
    }
};

class ProbeRadioButton : public QRadioButton
{
public:
    using QRadioButton::QRadioButton;
    QRect indicatorRect() const
    {
        QStyleOptionButton o;
        initStyleOption(&o);
        return style()->subElementRect(QStyle::SE_RadioButtonIndicator, &o, this);
    }
};

struct Shot {
    QString caption;
    QImage image;          // device pixels, devicePixelRatio set
    QRect focus;           // logical, in image coordinates; empty: not zoomed
    QSize sizeHint;        // the control's own size hint in this look
    QSize minimumHint;
    QJsonObject source;    // where a panel crop came from
};

struct Row {
    QString key;
    QString title;
    QString note;
    std::array<QVector<Shot>, kLookCount> looks;
};

struct Rows {
    QVector<Row> rows;

    Row& row(const QString& key, const QString& title, const QString& note = {})
    {
        for (Row& r : rows) {
            if (r.key == key) {
                return r;
            }
        }
        rows.append(Row{key, title, note, {}});
        return rows.last();
    }
};

struct LookSheets {
    QString sliderH;
    QString sliderV;
    QString checkBox;
    QString radio;
    QString combo;
    QString lineEdit;
    QString spinBox;
    QString tabs;
    QString scrollBar;
    QString groupBox;
    QString menu;
    QString buttons;
};

struct AppLook {
    QPalette palette;
    QString qss;
};

QApplication* app()
{
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}

// Today's strings. Call with the switch off: the Style:: builders return
// their original text then.
LookSheets todaySheets()
{
    LookSheets s;
    s.sliderH = Style::sliderHStyle();
    s.sliderV = Style::sliderVStyle();
    s.checkBox = QString::fromLatin1(Style::kCheckBoxStyle);
    s.radio = QString::fromLatin1(Style::kRadioButtonStyle);
    s.combo = QString::fromLatin1(Style::kComboStyle);
    s.lineEdit = QString::fromLatin1(Style::kLineEditStyle);
    s.spinBox = QString::fromLatin1(Style::kSpinBoxStyle);
    // The same string as SpotHubDialog.cpp's tab widget.
    s.tabs = QStringLiteral(
        "QTabWidget::pane { border: 1px solid #203040; }"
        "QTabBar::tab { background: #1a1a2e; color: #808890; border: 1px solid #203040; "
        "  padding: 6px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #0f0f1a; color: #00b4d8; border-bottom: none; }");
    // The same string as AppletPanelWidget.cpp's scroll area.
    s.scrollBar = QStringLiteral(
        "QScrollArea { background: %1; border: none; }"
        "QScrollBar:vertical {"
        "  background: %1; width: 8px; border: none;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %2; border-radius: 4px; min-height: 20px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
    ).arg(Style::kPanelBg, Style::kGroove);
    s.groupBox = QString::fromLatin1(Style::kGroupBoxStyle);
    s.menu = QString::fromLatin1(kPopupMenu);
    s.buttons = Style::buttonBaseStyle() + Style::greenCheckedStyle();
    return s;
}

// The preview takes today's strings through the scoped pass; the scroll
// bar is ContainerContentHost's preview string (Longpath's bar at the
// platform thickness).
LookSheets previewSheets(const LookSheets& today, int barPx)
{
    LookSheets s = today;
    QString bars = QString::fromLatin1(Workspace::kScrollBarStyle);
    bars.replace(QLatin1String("width: 10px; margin: 0;"),
                 QStringLiteral("width: %1px; margin: 0;").arg(barPx));
    bars.replace(QLatin1String("height: 10px; margin: 0;"),
                 QStringLiteral("height: %1px; margin: 0;").arg(barPx));
    s.scrollBar = bars;
    return s;
}

LookSheets longpathSheets()
{
    namespace LP = LongpathLook;
    LookSheets s;
    s.sliderH = LP::sliderHStyle();
    s.sliderV = LP::sliderVStyle();
    s.checkBox = QString::fromLatin1(LP::kCheckBoxStyle);
    s.radio = QString::fromLatin1(LP::kRadioButtonStyle);
    s.combo = QString::fromLatin1(LP::kComboStyle);
    s.lineEdit = LP::formFieldStyle();
    s.spinBox = LP::formFieldStyle();
    s.tabs = QString::fromLatin1(LP::kTabStyle);
    s.scrollBar = QString::fromLatin1(Workspace::kScrollBarStyle);
    s.groupBox = QString::fromLatin1(LP::kGroupBoxStyle);
    // Longpath's call sites set kPopupMenu; its ThemeFilter translates the
    // legacy values when the menu polishes.
    s.menu = LP::themed(QString::fromLatin1(LP::kPopupMenu));
    s.buttons = LP::buttonBaseStyle() + LP::blueCheckedStyle();
    return s;
}

void applyLook(int look, const AppLook& today)
{
    QApplication* a = app();
    WorkspacePreview::setEnabledForTest(look == kPreview);
    if (look == kLongpath) {
        // Longpath's startup: Fusion's palette, then its own palette and
        // baseline.
        a->setPalette(a->style()->standardPalette());
        LongpathLook::applyDarkPalette(*a);
        LongpathLook::applyAppBaselineQss(*a);
    } else {
        a->setPalette(today.palette);
        a->setStyleSheet(today.qss);
        if (look == kPreview) {
            applyWorkspacePalette(*a);
        }
    }
    pump(kSettleMs);
}

QImage grabImage(QWidget* w, const QRect& r = QRect(QPoint(0, 0), QSize(-1, -1)))
{
    return w->grab(r).toImage();
}

QSize logicalSizeOf(const QImage& img)
{
    const double dpr = img.devicePixelRatio();
    return QSize(static_cast<int>(std::lround(img.width() / dpr)),
                 static_cast<int>(std::lround(img.height() / dpr)));
}

QRect toDevice(const QRect& logical, double dpr)
{
    return QRect(static_cast<int>(std::lround(logical.x() * dpr)),
                 static_cast<int>(std::lround(logical.y() * dpr)),
                 static_cast<int>(std::lround(logical.width() * dpr)),
                 static_cast<int>(std::lround(logical.height() * dpr)));
}

QJsonArray rectArray(const QRect& r)
{
    return QJsonArray{r.x(), r.y(), r.width(), r.height()};
}

QJsonArray sizeArray(const QSize& s)
{
    return QJsonArray{s.width(), s.height()};
}

// ---- control cells ---------------------------------------------------------

class Column
{
public:
    Column()
        : m_top(std::make_unique<QWidget>())
    {
        m_top->setObjectName(QStringLiteral("controlsSheetColumn"));
        m_top->setAttribute(Qt::WA_DontShowOnScreen, true);
        m_top->setAutoFillBackground(true);
    }

    QWidget* top() const { return m_top.get(); }

    // A cell for a control of `inner` logical size.
    QWidget* cell(const QSize& inner)
    {
        auto* c = new QWidget(m_top.get());
        c->setAutoFillBackground(true);
        c->setGeometry(kCellPadPx, m_y, inner.width() + 2 * kCellPadPx, inner.height() + 2 * kCellPadPx);
        m_y += c->height() + kCellPadPx;
        m_width = qMax(m_width, c->width());
        return c;
    }

    void place(QWidget* control, const QSize& size)
    {
        control->move(kCellPadPx, kCellPadPx);
        control->setFixedSize(size);
    }

    void finish()
    {
        m_top->resize(m_width + 2 * kCellPadPx, m_y);
        m_top->show();
        pump(kSettleMs);
    }

private:
    std::unique_ptr<QWidget> m_top;
    int m_y{kCellPadPx};
    int m_width{0};
};

void hover(QWidget* w, const QPoint& local)
{
    w->setAttribute(Qt::WA_Hover, true);
    w->setAttribute(Qt::WA_UnderMouse, true);
    const QPointF p(local);
    QEnterEvent enter(p, w->mapTo(w->window(), p), w->mapToGlobal(p));
    QCoreApplication::sendEvent(w, &enter);
    QHoverEvent hoverMove(QEvent::HoverMove, p, w->mapToGlobal(p), p);
    QCoreApplication::sendEvent(w, &hoverMove);
    // QPushButton takes its hover from a mouse move it hit-tests.
    QMouseEvent mouseMove(QEvent::MouseMove, p, w->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &mouseMove);
    w->update();
}

void addShot(Rows& rows, int look, const QString& key, const QString& title, const QString& caption,
             QWidget* cell, QWidget* control, const QRect& focusInControl, const QString& note = {})
{
    Shot s;
    s.caption = caption;
    s.image = grabImage(cell);
    if (!focusInControl.isEmpty()) {
        const QPoint centre = control->mapTo(cell, focusInControl.center());
        s.focus = QRect(centre.x() - kZoomSourceWidth / 2, centre.y() - kZoomSourceHeight / 2,
                        kZoomSourceWidth, kZoomSourceHeight);
    }
    s.sizeHint = control->sizeHint();
    s.minimumHint = control->minimumSizeHint();
    rows.row(key, title, note).looks[look].append(s);
}

void addImageShot(Rows& rows, int look, const QString& key, const QString& title, const QString& caption,
                  const QImage& image, const QJsonObject& source = {}, const QString& note = {})
{
    Shot s;
    s.caption = caption;
    s.image = image;
    s.source = source;
    rows.row(key, title, note).looks[look].append(s);
}

void captureControls(SceneContext& ctx, int look, const LookSheets& sheets, Rows& rows)
{
    Column col;

    // Horizontal slider: resting, hover, pressed, disabled.
    std::array<QWidget*, 4> hCells{};
    std::array<ProbeSlider*, 4> hSliders{};
    for (std::size_t i = 0; i < hSliders.size(); ++i) {
        hCells[i] = col.cell(QSize(kHSliderWidth, kHSliderHeight));
        hSliders[i] = new ProbeSlider(Qt::Horizontal, hCells[i]);
        hSliders[i]->setRange(0, kSliderMax);
        hSliders[i]->setValue(kSliderValue);
        hSliders[i]->setStyleSheet(sheets.sliderH);
        col.place(hSliders[i], QSize(kHSliderWidth, kHSliderHeight));
    }
    hSliders[3]->setEnabled(false);

    QWidget* vCell = col.cell(QSize(kVSliderWidth, kVSliderHeight));
    auto* vSlider = new ProbeSlider(Qt::Vertical, vCell);
    vSlider->setRange(0, kSliderMax);
    vSlider->setValue(kSliderValue);
    vSlider->setStyleSheet(sheets.sliderV);
    col.place(vSlider, QSize(kVSliderWidth, kVSliderHeight));

    // Check box: off, on, disabled (on).
    std::array<QWidget*, 3> cbCells{};
    std::array<ProbeCheckBox*, 3> boxes{};
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        cbCells[i] = col.cell(QSize(kToggleWidth, kToggleHeight));
        boxes[i] = new ProbeCheckBox(QStringLiteral("Check box"), cbCells[i]);
        boxes[i]->setStyleSheet(sheets.checkBox);
        col.place(boxes[i], QSize(kToggleWidth, kToggleHeight));
    }
    boxes[1]->setChecked(true);
    boxes[2]->setChecked(true);
    boxes[2]->setEnabled(false);

    // Radio button: off, on (each in its own cell, so not exclusive).
    std::array<QWidget*, 2> rCells{};
    std::array<ProbeRadioButton*, 2> radios{};
    for (std::size_t i = 0; i < radios.size(); ++i) {
        rCells[i] = col.cell(QSize(kToggleWidth, kToggleHeight));
        radios[i] = new ProbeRadioButton(QStringLiteral("Radio"), rCells[i]);
        radios[i]->setStyleSheet(sheets.radio);
        col.place(radios[i], QSize(kToggleWidth, kToggleHeight));
    }
    radios[1]->setChecked(true);

    // Combo box: closed, and one to open.
    std::array<QWidget*, 2> comboCells{};
    std::array<QComboBox*, 2> combos{};
    for (std::size_t i = 0; i < combos.size(); ++i) {
        comboCells[i] = col.cell(QSize(kFieldWidth, kFieldHeight));
        combos[i] = new QComboBox(comboCells[i]);
        combos[i]->addItems({QStringLiteral("USB"), QStringLiteral("LSB"), QStringLiteral("CW"),
                             QStringLiteral("DIGU")});
        combos[i]->setStyleSheet(sheets.combo);
        col.place(combos[i], QSize(kFieldWidth, kFieldHeight));
    }

    // Line edit and spin box: resting, focused.
    std::array<QWidget*, 2> editCells{};
    std::array<QLineEdit*, 2> edits{};
    for (std::size_t i = 0; i < edits.size(); ++i) {
        editCells[i] = col.cell(QSize(kFieldWidth, kFieldHeight));
        edits[i] = new QLineEdit(QStringLiteral("14.225.000"), editCells[i]);
        edits[i]->setStyleSheet(sheets.lineEdit);
        edits[i]->setCursorPosition(0);
        col.place(edits[i], QSize(kFieldWidth, kFieldHeight));
    }
    std::array<QWidget*, 2> spinCells{};
    std::array<QSpinBox*, 2> spins{};
    for (std::size_t i = 0; i < spins.size(); ++i) {
        spinCells[i] = col.cell(QSize(kSpinWidth, kFieldHeight));
        spins[i] = new QSpinBox(spinCells[i]);
        spins[i]->setRange(0, kSliderMax);
        spins[i]->setValue(kSpinValue);
        spins[i]->setStyleSheet(sheets.spinBox);
        col.place(spins[i], QSize(kSpinWidth, kFieldHeight));
    }

    QWidget* tabsCell = col.cell(QSize(kTabsWidth, kTabsHeight));
    auto* tabs = new QTabWidget(tabsCell);
    for (const QString& name : {QStringLiteral("RX"), QStringLiteral("TX"), QStringLiteral("Audio")}) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(new QLabel(QStringLiteral("%1 page").arg(name), page));
        tabs->addTab(page, name);
    }
    tabs->setStyleSheet(sheets.tabs);
    col.place(tabs, QSize(kTabsWidth, kTabsHeight));

    // Scroll bar: its own thickness in each look (the thickness is one of
    // the differences), fixed length.
    auto* bar = new QScrollBar(Qt::Vertical);
    bar->setStyleSheet(sheets.scrollBar);
    bar->ensurePolished();
    const int barWidth = bar->sizeHint().width();
    QWidget* barCell = col.cell(QSize(barWidth, kScrollBarHeight));
    bar->setParent(barCell);
    bar->show();
    bar->setRange(0, kSliderMax);
    bar->setPageStep(kScrollBarPage);
    bar->setValue(kScrollBarValue);
    col.place(bar, QSize(barWidth, kScrollBarHeight));

    QWidget* groupCell = col.cell(QSize(kGroupWidth, kGroupHeight));
    auto* group = new QGroupBox(QStringLiteral("Group box"), groupCell);
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(new QLabel(QStringLiteral("Content"), group));
    group->setStyleSheet(sheets.groupBox);
    col.place(group, QSize(kGroupWidth, kGroupHeight));

    // Push buttons: resting, hovered, checked, disabled.
    std::array<QWidget*, 4> buttonCells{};
    std::array<QPushButton*, 4> buttons{};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        buttonCells[i] = col.cell(QSize(kButtonWidth, kButtonHeight));
        buttons[i] = new QPushButton(QStringLiteral("OPERATE"), buttonCells[i]);
        buttons[i]->setCheckable(true);
        buttons[i]->setStyleSheet(sheets.buttons);
        col.place(buttons[i], QSize(kButtonWidth, kButtonHeight));
    }
    buttons[2]->setChecked(true);
    buttons[3]->setEnabled(false);

    QWidget* meterCell = col.cell(QSize(kMeterSliderWidth, 16));
    auto* meter = new MeterSlider(meterCell);
    meter->setGain(kMeterSliderGain);
    meter->setLevel(kMeterSliderLevel);
    col.place(meter, QSize(kMeterSliderWidth, 16));

    col.finish();
    if (look == kPreview) {
        WorkspacePreview::themeSubtree(col.top());
        pump(kSettleMs);
    }

    // States that need the shown widget.
    hover(hSliders[1], hSliders[1]->handleRect().center());
    hover(hSliders[2], hSliders[2]->handleRect().center());
    {
        const QPointF p(hSliders[2]->handleRect().center());
        QMouseEvent press(QEvent::MouseButtonPress, p, hSliders[2]->mapToGlobal(p), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(hSliders[2], &press);
    }
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(col.top());
    QT_WARNING_POP
    hover(buttons[1], QPoint(kButtonWidth / 2, kButtonHeight / 2));
    pump(kShortSettleMs);

    const QString hKey = QStringLiteral("slider-h");
    const QString hTitle = QStringLiteral("Horizontal slider, 60 %");
    const std::array<QString, 4> hCaptions{QStringLiteral("resting"), QStringLiteral("hover"),
                                           QStringLiteral("pressed"), QStringLiteral("disabled")};
    for (std::size_t i = 0; i < hSliders.size(); ++i) {
        addShot(rows, look, hKey, hTitle, hCaptions[i], hCells[i], hSliders[i], hSliders[i]->handleRect());
    }
    addShot(rows, look, QStringLiteral("slider-v"), QStringLiteral("Vertical slider, 60 %"),
            QStringLiteral("resting"), vCell, vSlider, vSlider->handleRect());
    const std::array<QString, 3> cbCaptions{QStringLiteral("off"), QStringLiteral("on"),
                                            QStringLiteral("disabled (on)")};
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        addShot(rows, look, QStringLiteral("checkbox"), QStringLiteral("Check box"), cbCaptions[i],
                cbCells[i], boxes[i], boxes[i]->indicatorRect());
    }
    for (std::size_t i = 0; i < radios.size(); ++i) {
        addShot(rows, look, QStringLiteral("radio"), QStringLiteral("Radio button"),
                i == 0 ? QStringLiteral("off") : QStringLiteral("on"), rCells[i], radios[i],
                radios[i]->indicatorRect());
    }
    addShot(rows, look, QStringLiteral("combo"), QStringLiteral("Combo box"), QStringLiteral("closed"),
            comboCells[0], combos[0], {},
            QStringLiteral("Popup row colour unverified: offscreen, Qt paints the selected row without "
                           "the stylesheet's selection colour."));
    addShot(rows, look, QStringLiteral("line-edit"), QStringLiteral("Line edit"), QStringLiteral("resting"),
            editCells[0], edits[0], {});
    addShot(rows, look, QStringLiteral("spin-box"), QStringLiteral("Spin box"), QStringLiteral("resting"),
            spinCells[0], spins[0], {});
    addShot(rows, look, QStringLiteral("tabs"), QStringLiteral("Tab bar with its pane"),
            QStringLiteral("RX selected"), tabsCell, tabs, {});
    addShot(rows, look, QStringLiteral("scroll-bar"), QStringLiteral("Scroll bar"),
            QStringLiteral("%1 px thick").arg(barWidth), barCell, bar, {});
    addShot(rows, look, QStringLiteral("group-box"), QStringLiteral("Group box"), QStringLiteral("resting"),
            groupCell, group, {});
    // The menu and tooltip rows sit here; their shots come below.
    rows.row(QStringLiteral("menu"), QStringLiteral("Menu"));
    rows.row(QStringLiteral("tooltip"), QStringLiteral("Tooltip"));
    const std::array<QString, 4> buttonCaptions{QStringLiteral("resting"), QStringLiteral("hovered"),
                                                QStringLiteral("checked"), QStringLiteral("disabled")};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        addShot(rows, look, QStringLiteral("buttons"),
                QStringLiteral("Push button (checked: today green OPERATE, Longpath blue)"),
                buttonCaptions[i], buttonCells[i], buttons[i], {});
    }
    addShot(rows, look, QStringLiteral("meter-slider"), QStringLiteral("MeterSlider"),
            QStringLiteral("gain 0.6, level 0.45"), meterCell, meter, {},
            QStringLiteral("Paints itself; it does not take stylesheets, so all three are the same."));

    // Focus: one field at a time in the active column window.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(col.top());
    QT_WARNING_POP
    edits[1]->setFocus(Qt::OtherFocusReason);
    edits[1]->setCursorPosition(0);
    pump(kShortSettleMs);
    if (!edits[1]->hasFocus()) {
        ctx.notes.append(QStringLiteral("%1: the focused line edit did not take focus").arg(lookKey(look)));
    }
    addShot(rows, look, QStringLiteral("line-edit"), QString(), QStringLiteral("focused"), editCells[1],
            edits[1], {});
    spins[1]->setFocus(Qt::OtherFocusReason);
    pump(kShortSettleMs);
    if (!spins[1]->hasFocus()) {
        ctx.notes.append(QStringLiteral("%1: the focused spin box did not take focus").arg(lookKey(look)));
    }
    addShot(rows, look, QStringLiteral("spin-box"), QString(), QStringLiteral("focused"), spinCells[1],
            spins[1], {});
    spins[1]->clearFocus();
    pump(kShortSettleMs);

    // Combo popup with a row hovered.
    combos[1]->showPopup();
    pump(kSettleMs);
    QAbstractItemView* view = combos[1]->view();
    // A live popup is the active window; its selection then paints in the
    // Active palette group, as it does under the pointer.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(view->window());
    QT_WARNING_POP
    const QModelIndex hovered = view->model()->index(kComboHoverRow, 0);
    view->setCurrentIndex(hovered);
    hover(view->viewport(), view->visualRect(hovered).center());
    pump(kShortSettleMs);
    if (look == kToday) {
        ctx.notes.append(QStringLiteral(
            "combo popup: LSB is the view's current and selected row and the popup is the active "
            "window, yet the offscreen render paints that row without the stylesheet's selection "
            "colour (today: unhighlighted, preview and Longpath: black). Check the row colour against "
            "a live popup before judging it."));
    }
    addShot(rows, look, QStringLiteral("combo"), QString(), QStringLiteral("open"), comboCells[1], combos[1],
            {});
    addImageShot(rows, look, QStringLiteral("combo"), QString(), QStringLiteral("popup, LSB selected (see note)"),
                 grabImage(view->window()));
    combos[1]->hidePopup();
    pump(kSettleMs);

    // Menu with a checked and a disabled item, the first item active.
    auto menu = std::make_unique<QMenu>();
    menu->setStyleSheet(sheets.menu);
    QAction* first = menu->addAction(QStringLiteral("Active item"));
    QAction* checked = menu->addAction(QStringLiteral("Checked item"));
    checked->setCheckable(true);
    checked->setChecked(true);
    QAction* disabled = menu->addAction(QStringLiteral("Disabled item"));
    disabled->setEnabled(false);
    menu->addAction(QStringLiteral("Plain item"));
    if (look == kPreview) {
        WorkspacePreview::themeSubtree(menu.get());
    }
    menu->popup(col.top()->mapToGlobal(QPoint(0, 0)));
    pump(kSettleMs);
    menu->setActiveAction(first);
    pump(kShortSettleMs);
    addImageShot(rows, look, QStringLiteral("menu"), QString(), QStringLiteral("open, first item active"),
                 grabImage(menu.get()));
    menu->hide();
    pump(kShortSettleMs);

    // Tooltip, styled by the application's stylesheet in every look.
    QToolTip::showText(col.top()->mapToGlobal(QPoint(20, 20)), QStringLiteral("AGC max gain"), col.top());
    pump(kSettleMs);
    QWidget* tip = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && w->inherits("QTipLabel")) {
            tip = w;
        }
    }
    if (tip != nullptr) {
        addImageShot(rows, look, QStringLiteral("tooltip"), QString(), QStringLiteral("shown"), grabImage(tip));
    } else {
        ctx.notes.append(QStringLiteral("%1: the tooltip did not show").arg(lookKey(look)));
    }
    QToolTip::hideText();
    pump(kSettleMs);
}

// ---- real panels -----------------------------------------------------------

struct Panels {
    QPointer<QWidget> mainPanel;
    QPointer<RxApplet> rx;
    QPointer<TxApplet> tx;
    QPointer<PhoneCwApplet> phone;
    QPointer<TxEqDialog> eq;
    QVector<QPair<QPointer<QWidget>, QString>> sheets;   // today's stylesheets
    QString mainWindowSheet;
};

void snapshot(QWidget* root, Panels& panels)
{
    if (root == nullptr) {
        return;
    }
    panels.sheets.append({QPointer<QWidget>(root), root->styleSheet()});
    for (QWidget* w : root->findChildren<QWidget*>()) {
        panels.sheets.append({QPointer<QWidget>(w), w->styleSheet()});
    }
}

void restoreSheets(const Panels& panels)
{
    for (const auto& [w, sheet] : panels.sheets) {
        if (w.isNull()) {
            continue;
        }
        if (w->styleSheet() != sheet) {
            w->setStyleSheet(sheet);
        }
        // The scoped pass keeps its own record; clear it so the next pass
        // starts from today's sheet.
        w->setProperty(WorkspacePreview::kOriginalQssProperty, QVariant());
        w->setProperty(WorkspacePreview::kThemedQssProperty, QVariant());
    }
}

// Longpath on the real panels: every QSlider takes Longpath's slider
// stylesheet (as Longpath's applets and TxEqDialog set it); every other
// stylesheet goes through the legacy-value table, as Longpath's
// ThemeFilter does at polish.
void longpathPanels(const Panels& panels)
{
    for (const auto& [w, sheet] : panels.sheets) {
        if (w.isNull()) {
            continue;
        }
        if (auto* s = qobject_cast<QSlider*>(w.data())) {
            s->setStyleSheet(s->orientation() == Qt::Horizontal ? LongpathLook::sliderHStyle()
                                                                : LongpathLook::sliderVStyle());
        } else if (!sheet.isEmpty()) {
            w->setStyleSheet(LongpathLook::themed(sheet));
        }
    }
}

QScrollArea* scrollAreaOf(QWidget* w)
{
    for (QWidget* p = w != nullptr ? w->parentWidget() : nullptr; p != nullptr; p = p->parentWidget()) {
        if (auto* area = qobject_cast<QScrollArea*>(p)) {
            return area;
        }
    }
    return nullptr;
}

QRect rectIn(QWidget* top, QWidget* w)
{
    return QRect(w->mapTo(top, QPoint(0, 0)), w->size());
}

// The band of `sliders` across `applet`'s full width, clipped to the
// scroll viewport it sits in.
QRect sliderBand(QWidget* top, QWidget* applet, const QList<QSlider*>& sliders)
{
    QRect u;
    for (QSlider* s : sliders) {
        if (s != nullptr && s->isVisible()) {
            u |= rectIn(top, s);
        }
    }
    if (u.isEmpty()) {
        return {};
    }
    const QRect a = rectIn(top, applet);
    QRect band(a.left(), u.top() - kBandPadPx, a.width(), u.height() + 2 * kBandPadPx);
    if (QScrollArea* area = scrollAreaOf(applet)) {
        band &= rectIn(top, area->viewport());
    }
    return band;
}

void cropShot(SceneContext& ctx, Rows& rows, int look, const QString& key, const QString& title,
              QWidget* top, const QRect& band, const QString& sourceName, const QString& missing,
              const QString& caption = QStringLiteral("cropped"), QJsonObject source = {})
{
    if (band.isEmpty()) {
        ctx.notes.append(QStringLiteral("%1 %2: %3").arg(lookKey(look), key, missing));
        return;
    }
    const QImage img = grabImage(top, band);
    source[QStringLiteral("source")] = sourceName;
    source[QStringLiteral("logical_rect")] = rectArray(band);
    source[QStringLiteral("device_rect")] = rectArray(toDevice(band, img.devicePixelRatio()));
    addImageShot(rows, look, key, title, caption, img, source);
}

// The band of `sliders` across `applet`'s full width, in the applet's own
// coordinates. Grabbed from the applet itself, so the Main Panel's scroll
// viewport cannot clip it and no scrolling is needed.
QRect appletBand(QWidget* applet, const QList<QSlider*>& sliders)
{
    QRect u;
    for (QSlider* s : sliders) {
        if (s != nullptr && s->isVisibleTo(applet)) {
            u |= QRect(s->mapTo(applet, QPoint(0, 0)), s->size());
        }
    }
    if (u.isEmpty()) {
        return {};
    }
    return QRect(0, u.top() - kBandPadPx, applet->width(), u.height() + 2 * kBandPadPx)
        .intersected(applet->rect());
}

// One applet row: the band grabbed from the applet at its full width. The
// sidecar also gives the band in window coordinates and the part of it the
// Main Panel viewport shows, so today's column can be checked against the
// baseline window where the viewport shows it.
void appletShot(SceneContext& ctx, Rows& rows, int look, const QString& key, const QString& title,
                QWidget* applet, const QList<QSlider*>& sliders, const QString& missing)
{
    const QRect band = appletBand(applet, sliders);
    QJsonObject source;
    if (!band.isEmpty()) {
        const QRect inWindow = band.translated(applet->mapTo(ctx.window, QPoint(0, 0)));
        source[QStringLiteral("window_logical_rect")] = rectArray(inWindow);
        if (QScrollArea* area = scrollAreaOf(applet)) {
            const QRect shown = inWindow & rectIn(ctx.window, area->viewport());
            source[QStringLiteral("viewport_shown_window_rect")] = rectArray(shown);
            source[QStringLiteral("viewport_hidden_px")] = inWindow.width() - shown.width();
        }
    }
    cropShot(ctx, rows, look, key, title, applet, band,
             QString::fromLatin1(applet->metaObject()->className()) + QStringLiteral(" at full width"),
             missing, QStringLiteral("applet at full width"), source);
}

QList<QSlider*> slidersNamed(QWidget* root, const QStringList& accessibleOrObjectNames)
{
    QList<QSlider*> out;
    if (root == nullptr) {
        return out;
    }
    for (QSlider* s : root->findChildren<QSlider*>()) {
        if (accessibleOrObjectNames.contains(s->accessibleName())
            || accessibleOrObjectNames.contains(s->objectName())) {
            out.append(s);
        }
    }
    return out;
}

void capturePanels(SceneContext& ctx, int look, Panels& panels, Rows& rows)
{
    QWidget* window = ctx.window;
    pump(kSettleMs);

    // RX applet: its visible slider rows at full width, and the same rows
    // as the main window shows them through the Main Panel viewport.
    if (!panels.rx.isNull()) {
        QList<QSlider*> rxSliders;
        for (QSlider* s : panels.rx->findChildren<QSlider*>()) {
            if (s->isVisible()) {
                rxSliders.append(s);
            }
        }
        appletShot(ctx, rows, look, QStringLiteral("panel-rx"),
                   QStringLiteral("RX applet slider rows (the applet at full width)"), panels.rx, rxSliders,
                   QStringLiteral("no visible RX applet slider"));
        cropShot(ctx, rows, look, QStringLiteral("panel-rx-viewport"),
                 QStringLiteral("RX applet through the Main Panel viewport (real clipping, today's window layout)"),
                 window, sliderBand(window, panels.rx, rxSliders), QStringLiteral("main window"),
                 QStringLiteral("no visible RX applet slider"), QStringLiteral("viewport crop"));
    } else {
        ctx.notes.append(QStringLiteral("%1: no RxApplet").arg(lookKey(look)));
    }

    // TX applet: RF power and tune power.
    if (!panels.tx.isNull()) {
        appletShot(ctx, rows, look, QStringLiteral("panel-tx"),
                   QStringLiteral("TX applet RF power and tune power (the applet at full width)"), panels.tx,
                   slidersNamed(panels.tx, {QStringLiteral("TxRfPowerSlider"), QStringLiteral("Tune power")}),
                   QStringLiteral("RF power and tune power sliders not found"));
    }

    // Phone/CW applet, Phone page: microphone gain.
    if (!panels.phone.isNull()) {
        panels.phone->showPage(0);
        pump(kShortSettleMs);
        appletShot(ctx, rows, look, QStringLiteral("panel-mic"),
                   QStringLiteral("Phone/CW applet microphone gain (the applet at full width)"), panels.phone,
                   slidersNamed(panels.phone, {QStringLiteral("Microphone gain")}),
                   QStringLiteral("microphone gain slider not found"));
    }
    pump(kShortSettleMs);

    // TX EQ dialog: its vertical sliders.
    if (!panels.eq.isNull()) {
        QRect u;
        for (QSlider* s : panels.eq->findChildren<QSlider*>()) {
            if (s->isVisible() && s->orientation() == Qt::Vertical) {
                u |= rectIn(panels.eq, s);
            }
        }
        const QRect band = u.isEmpty() ? QRect()
                                       : u.adjusted(-kBandPadPx, -kBandPadPx, kBandPadPx, kBandPadPx)
                                             .intersected(panels.eq->rect());
        cropShot(ctx, rows, look, QStringLiteral("panel-eq"), QStringLiteral("TX EQ dialog sliders"),
                 panels.eq, band, QStringLiteral("TxEqDialog"), QStringLiteral("no visible EQ slider"));
    }
}

// ---- assembly ----------------------------------------------------------------

QString labelSheet(const char* colour, int px, bool bold = false)
{
    return QStringLiteral("QLabel { color: %1; font-size: %2px; %3 background: transparent; }")
        .arg(QLatin1String(colour))
        .arg(px)
        .arg(bold ? QStringLiteral("font-weight: bold;") : QString());
}

QLabel* textLabel(QWidget* parent, const QString& text, const char* colour, int px, bool bold = false)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(labelSheet(colour, px, bold));
    l->adjustSize();
    return l;
}

// Lays the rows out as a sheet of QLabels and grabs it. `placements`
// receives each image's rectangle on the sheet.
QImage assemble(const Rows& rows, const QString& title, const QString& subtitle, QJsonArray& placements)
{
    auto sheet = std::make_unique<QWidget>();
    sheet->setObjectName(QStringLiteral("controlsSheet"));
    sheet->setAttribute(Qt::WA_DontShowOnScreen, true);
    sheet->setAutoFillBackground(true);
    QPalette pal = sheet->palette();
    pal.setColor(QPalette::Window, QColor(QLatin1String(kSheetBg)));
    sheet->setPalette(pal);

    struct Placed {
        QLabel* caption;
        QLabel* image;
        int width;
        int height;
    };
    // Build every label first, then place them.
    std::vector<std::array<QVector<Placed>, kLookCount>> placed(rows.rows.size());
    std::array<int, kLookCount> columnWidth{kMinColumnWidth, kMinColumnWidth, kMinColumnWidth};
    int captionHeight = 0;
    for (std::size_t r = 0; r < rows.rows.size(); ++r) {
        for (int l = 0; l < kLookCount; ++l) {
            int width = 0;
            for (const Shot& s : rows.rows[r].looks[l]) {
                QLabel* cap = textLabel(sheet.get(), s.caption, kSheetCaptionColour, 10);
                auto* img = new QLabel(sheet.get());
                img->setPixmap(QPixmap::fromImage(s.image));
                const QSize logical = logicalSizeOf(s.image);
                img->setFixedSize(logical);
                const int w = qMax(logical.width(), cap->width());
                placed[r][l].append({cap, img, w, logical.height()});
                captionHeight = qMax(captionHeight, cap->height());
                width += (width > 0 ? kShotGapPx : 0) + w;
            }
            columnWidth[l] = qMax(columnWidth[l], width);
        }
    }

    QLabel* titleLabel = textLabel(sheet.get(), title, kSheetTitleColour, 15, true);
    titleLabel->move(kSheetMarginPx, kSheetMarginPx);
    QLabel* subtitleLabel = textLabel(sheet.get(), subtitle, kSheetTextColour, 11);
    subtitleLabel->move(kSheetMarginPx, titleLabel->geometry().bottom() + 4);
    int y = subtitleLabel->geometry().bottom() + 14;

    std::array<int, kLookCount> columnX{};
    int x = kSheetMarginPx + kRowLabelWidth + kColumnGapPx;
    for (int l = 0; l < kLookCount; ++l) {
        columnX[l] = x;
        QLabel* head = textLabel(sheet.get(), lookTitle(l), kSheetTitleColour, 12, true);
        head->move(x, y);
        x += columnWidth[l] + kColumnGapPx;
    }
    const int sheetWidth = x - kColumnGapPx + kSheetMarginPx;
    y += 24;

    for (std::size_t r = 0; r < rows.rows.size(); ++r) {
        const Row& row = rows.rows[r];
        auto* rule = new QFrame(sheet.get());
        rule->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; }").arg(QLatin1String(kSheetRule)));
        rule->setGeometry(kSheetMarginPx, y, sheetWidth - 2 * kSheetMarginPx, 1);
        y += kRowGapPx / 2;
        QLabel* name = textLabel(sheet.get(), row.title, kSheetTextColour, 11, true);
        name->setWordWrap(true);
        name->setFixedWidth(kRowLabelWidth);
        name->adjustSize();
        name->move(kSheetMarginPx, y + captionHeight);
        int labelBottom = name->geometry().bottom();
        if (!row.note.isEmpty()) {
            QLabel* note = textLabel(sheet.get(), row.note, kSheetCaptionColour, 10);
            note->setWordWrap(true);
            note->setFixedWidth(kRowLabelWidth);
            note->adjustSize();
            note->move(kSheetMarginPx, labelBottom + 4);
            labelBottom = note->geometry().bottom();
        }
        int rowHeight = 0;
        for (int l = 0; l < kLookCount; ++l) {
            int sx = columnX[l];
            for (int i = 0; i < placed[r][l].size(); ++i) {
                const Placed& p = placed[r][l][i];
                const Shot& s = row.looks[l][i];
                p.caption->move(sx, y);
                p.image->move(sx, y + captionHeight + 2);
                const double dpr = s.image.devicePixelRatio();
                QJsonObject at{
                    {QStringLiteral("row"), row.key},
                    {QStringLiteral("caption"), s.caption},
                    {QStringLiteral("look"), lookKey(l)},
                    {QStringLiteral("sheet_logical_rect"), rectArray(p.image->geometry())},
                    {QStringLiteral("sheet_device_rect"), rectArray(toDevice(p.image->geometry(), dpr))},
                };
                if (!s.source.isEmpty()) {
                    at.insert(QStringLiteral("crop_source"), s.source);
                }
                placements.append(at);
                rowHeight = qMax(rowHeight, captionHeight + 2 + p.height);
                sx += p.width + kShotGapPx;
            }
        }
        y = qMax(y + rowHeight, labelBottom) + kRowGapPx;
    }
    sheet->resize(sheetWidth, y + kSheetMarginPx);
    sheet->show();
    pump(kSettleMs);
    return grabImage(sheet.get());
}

// The zoom rows: each handle or indicator, 5x nearest-neighbour.
Rows zoomRows(const Rows& rows)
{
    Rows out;
    for (const Row& row : rows.rows) {
        for (int l = 0; l < kLookCount; ++l) {
            for (const Shot& s : row.looks[l]) {
                if (s.focus.isEmpty()) {
                    continue;
                }
                const double dpr = s.image.devicePixelRatio();
                const QRect device = toDevice(s.focus, dpr).intersected(s.image.rect());
                QImage cut = s.image.copy(device);
                QImage big = cut.scaled(cut.width() * kZoomFactor, cut.height() * kZoomFactor,
                                        Qt::IgnoreAspectRatio, Qt::FastTransformation);
                big.setDevicePixelRatio(dpr);
                Shot z;
                z.caption = s.caption;
                z.image = big;
                z.source = QJsonObject{{QStringLiteral("cell_device_rect"), rectArray(device)}};
                out.row(row.key, row.title).looks[l].append(z);
            }
        }
    }
    return out;
}

QJsonArray controlHints(const Rows& rows)
{
    QJsonArray out;
    for (const Row& row : rows.rows) {
        const int n = static_cast<int>(row.looks[kToday].size());
        for (int i = 0; i < n; ++i) {
            const Shot& t = row.looks[kToday][i];
            if (t.sizeHint.isEmpty()) {
                continue;
            }
            QJsonObject o{{QStringLiteral("row"), row.key}, {QStringLiteral("caption"), t.caption}};
            for (int l = 0; l < kLookCount; ++l) {
                if (i < row.looks[l].size()) {
                    const Shot& s = row.looks[l][i];
                    o.insert(lookKey(l), QJsonObject{{QStringLiteral("size_hint"), sizeArray(s.sizeHint)},
                                                     {QStringLiteral("minimum_size_hint"), sizeArray(s.minimumHint)},
                                                     {QStringLiteral("cell_logical"), sizeArray(logicalSizeOf(s.image))}});
                }
            }
            out.append(o);
        }
    }
    return out;
}

bool rowsAgree(const Rows& rows, QString& why)
{
    for (const Row& row : rows.rows) {
        for (int l = 1; l < kLookCount; ++l) {
            if (row.looks[l].size() != row.looks[kToday].size()) {
                why = QStringLiteral("row %1: %2 has %3 shots, today %4")
                          .arg(row.key, lookKey(l))
                          .arg(row.looks[l].size())
                          .arg(row.looks[kToday].size());
                return false;
            }
        }
    }
    return true;
}

} // namespace

void sceneControlsSheet(SceneContext& ctx)
{
    ctx.provenance = QStringLiteral("proposed");
    QApplication* a = app();
    if (a == nullptr || ctx.window == nullptr) {
        ctx.failure = QStringLiteral("controls-sheet: no application or window");
        return;
    }
    if (WorkspacePreview::enabled()) {
        ctx.failure = QStringLiteral("controls-sheet: must start with the preview switch off");
        return;
    }
    QStyleHints* hints = QGuiApplication::styleHints();
    const int flashTime = hints->cursorFlashTime();
    hints->setCursorFlashTime(0);   // a steady caret, so focused fields grab the same every run

    const AppLook today{a->palette(), a->styleSheet()};
    const LookSheets todayStrings = todaySheets();

    Panels panels;
    if (auto* manager = ctx.window->findChild<ContainerManager*>()) {
        panels.mainPanel = manager->panelContainer();
    }
    panels.rx = ctx.window->findChild<RxApplet*>();
    panels.tx = ctx.window->findChild<TxApplet*>();
    panels.phone = ctx.window->findChild<PhoneCwApplet*>();
    panels.mainWindowSheet = ctx.window->styleSheet();
    if (auto* action = ctx.window->findChild<QAction*>(QStringLiteral("toolsTxEqualizer"))) {
        action->trigger();
        pump(kSettleMs);
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* d = qobject_cast<TxEqDialog*>(w); d != nullptr && d->isVisible()) {
                panels.eq = d;
            }
        }
    }
    if (panels.eq.isNull()) {
        ctx.notes.append(QStringLiteral("the TX EQ dialog did not open; no EQ crops"));
    } else {
        for (const QJsonValue& v : stopTimersUnder(panels.eq)) {
            ctx.stoppedTimers.append(v);
        }
    }
    snapshot(panels.mainPanel, panels);
    snapshot(panels.eq, panels);

    Rows rows;
    for (int look = 0; look < kLookCount; ++look) {
        // Today's pass changes nothing: the window is as the baseline
        // scene left it.
        if (look != kToday) {
            applyLook(look, today);
            restoreSheets(panels);
        }
        if (look == kPreview) {
            ctx.window->setStyleSheet(QssTranslator::translateWithAmbiguousRole(
                                          panels.mainWindowSheet, QStringLiteral("border-subtle")).qss);
            WorkspacePreview::themeSubtree(panels.mainPanel);
            WorkspacePreview::themeSubtree(panels.eq);
        } else if (look == kLongpath) {
            ctx.window->setStyleSheet(LongpathLook::themed(panels.mainWindowSheet));
            longpathPanels(panels);
        }
        // Panels first: the RX crop in today's look is the baseline
        // window's own pixels.
        capturePanels(ctx, look, panels, rows);

        LookSheets sheets = todayStrings;
        if (look == kPreview) {
            QWidget probe;
            sheets = previewSheets(todayStrings,
                                   probe.style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, &probe));
        } else if (look == kLongpath) {
            sheets = longpathSheets();
        }
        captureControls(ctx, look, sheets, rows);
    }

    // Back to today's look, switch off.
    applyLook(kToday, today);
    restoreSheets(panels);
    ctx.window->setStyleSheet(panels.mainWindowSheet);
    pump(kSettleMs);
    if (!panels.eq.isNull()) {
        panels.eq->close();
    }
    hints->setCursorFlashTime(flashTime);

    // Panel rows go last on the sheet.
    QVector<Row> ordered;
    for (const Row& r : rows.rows) {
        if (!r.key.startsWith(QLatin1String("panel-"))) {
            ordered.append(r);
        }
    }
    for (const Row& r : rows.rows) {
        if (r.key.startsWith(QLatin1String("panel-"))) {
            ordered.append(r);
        }
    }
    rows.rows = ordered;
    for (Row& r : rows.rows) {
        if (r.key == QLatin1String("panel-rx-viewport")) {
            r.note = QStringLiteral("What the window shows: the RX applet is wider than the Main Panel "
                                    "viewport, so its right edge is cut. This window keeps today's layout in "
                                    "every column; strip-fit-preview measures the preview's own right strip.");
        } else if (r.key == QLatin1String("panel-rx") || r.key == QLatin1String("panel-tx")
                   || r.key == QLatin1String("panel-mic")) {
            r.note = QStringLiteral("Grabbed from the applet itself at its full width, so every column shows "
                                    "the whole applet.");
        }
    }
    QString why;
    if (!rowsAgree(rows, why)) {
        ctx.failure = QStringLiteral("controls-sheet: the looks captured different shots: %1").arg(why);
        return;
    }

    QJsonArray placements;
    CapturedImage sheet;
    sheet.name = QStringLiteral("controls-sheet");
    sheet.kind = QStringLiteral("sheet");
    sheet.image = assemble(rows, QStringLiteral("Controls: today, preview now, Longpath"),
                           QStringLiteral("Real Qt widgets at the same fixed sizes in every column. Panel rows are "
                                          "crops of the live main window and TX EQ dialog in each look."),
                           placements);
    sheet.dpr = sheet.image.devicePixelRatio();
    sheet.logicalSize = logicalSizeOf(sheet.image);
    sheet.extra[QStringLiteral("window")] = QStringLiteral("its own top-level QWidget, not the MainWindow");
    sheet.extra[QStringLiteral("placements")] = placements;
    sheet.extra[QStringLiteral("controls")] = controlHints(rows);
    sheet.extra[QStringLiteral("looks")] = QJsonObject{
        {QStringLiteral("today"), QStringLiteral("Fusion, applyDarkPalette, applyAppBaselineQss; today's "
                                                 "stylesheets; preview switch off")},
        {QStringLiteral("preview"), QStringLiteral("switch on, applyWorkspacePalette; today's stylesheets "
                                                   "through WorkspacePreview::themeSubtree (QssTranslator then "
                                                   "Workspace::Components::componentize)")},
        {QStringLiteral("longpath"), QStringLiteral("Fusion palette, Longpath applyDarkPalette and "
                                                    "applyAppBaselineQss; Longpath's control stylesheets "
                                                    "(tests/visual/LongpathControlStyles.h)")},
    };
    sheet.extra[QStringLiteral("column_provenance")] = QJsonObject{
        {QStringLiteral("today"), QStringLiteral("retained-current-app")},
        {QStringLiteral("preview"), QStringLiteral("proposed-look")},
        {QStringLiteral("longpath"), QStringLiteral("longpath-port")},
    };
    ctx.images.append(sheet);

    QJsonArray zoomPlacements;
    CapturedImage zoom;
    zoom.name = QStringLiteral("controls-sheet--zoom");
    zoom.kind = QStringLiteral("zoom");
    zoom.image = assemble(zoomRows(rows), QStringLiteral("Handles and indicators, 5x nearest-neighbour"),
                          QStringLiteral("Each cut-out is %1x%2 logical px of the control cell around the "
                                         "handle or indicator, enlarged 5x.")
                              .arg(kZoomSourceWidth)
                              .arg(kZoomSourceHeight),
                          zoomPlacements);
    zoom.dpr = zoom.image.devicePixelRatio();
    zoom.logicalSize = logicalSizeOf(zoom.image);
    zoom.extra[QStringLiteral("source")] = sheet.name;
    zoom.extra[QStringLiteral("enlargement")] = QStringLiteral("5x nearest-neighbour");
    zoom.extra[QStringLiteral("placements")] = zoomPlacements;
    ctx.images.append(zoom);
}

// ---- strip fit ---------------------------------------------------------------

namespace {

bool isApplet(const QWidget* w)
{
    return QString::fromLatin1(w->metaObject()->className()).endsWith(QLatin1String("Applet"));
}

// Applets under `body` that are not inside another applet.
QList<QWidget*> outerApplets(QWidget* body)
{
    QList<QWidget*> out;
    for (QWidget* w : body->findChildren<QWidget*>()) {
        if (!isApplet(w) || !w->isVisibleTo(body)) {
            continue;
        }
        bool nested = false;
        for (QWidget* p = w->parentWidget(); p != nullptr && p != body; p = p->parentWidget()) {
            if (isApplet(p)) {
                nested = true;
                break;
            }
        }
        if (!nested) {
            out.append(w);
        }
    }
    return out;
}

int minWidthOf(const QWidget* w)
{
    return qMax(w->minimumWidth(), w->minimumSizeHint().width());
}

} // namespace

QJsonObject measurePanelFit(SceneContext& ctx, const QString& look)
{
    QJsonObject out{{QStringLiteral("look"), look}};
    auto* manager = ctx.window->findChild<ContainerManager*>();
    QWidget* panel = manager != nullptr ? manager->panelContainer() : nullptr;
    if (panel == nullptr) {
        out[QStringLiteral("error")] = QStringLiteral("no Main Panel container");
        return out;
    }
    out[QStringLiteral("panel_rect")] = rectArray(rectIn(ctx.window, panel));
    QJsonArray areas;
    for (QScrollArea* area : panel->findChildren<QScrollArea*>()) {
        QWidget* body = area->widget();
        if (body == nullptr || !area->isVisible()) {
            continue;
        }
        QWidget* vp = area->viewport();
        QScrollBar* v = area->verticalScrollBar();
        QScrollBar* h = area->horizontalScrollBar();
        QStringList chain;
        for (QWidget* p = area->parentWidget(); p != nullptr && p != panel->parentWidget(); p = p->parentWidget()) {
            chain.append(QString::fromLatin1(p->metaObject()->className()));
        }
        QJsonArray applets;
        for (QWidget* a : outerApplets(body)) {
            const QPoint at = a->mapTo(vp, QPoint(0, 0));
            const int minW = minWidthOf(a);
            applets.append(QJsonObject{
                {QStringLiteral("class"), QString::fromLatin1(a->metaObject()->className())},
                {QStringLiteral("x_in_viewport"), at.x()},
                {QStringLiteral("width"), a->width()},
                {QStringLiteral("minimum_width"), minW},
                {QStringLiteral("size_hint_width"), a->sizeHint().width()},
                {QStringLiteral("clipped_left_px"), qMax(0, -at.x())},
                {QStringLiteral("clipped_right_px"), qMax(0, at.x() + a->width() - vp->width())},
                {QStringLiteral("minimum_over_viewport_px"), qMax(0, minW - vp->width())},
            });
        }
        areas.append(QJsonObject{
            {QStringLiteral("parents"), chain.join(QStringLiteral(" < "))},
            {QStringLiteral("area_width"), area->width()},
            {QStringLiteral("viewport_width"), vp->width()},
            {QStringLiteral("vertical_bar_visible"), v->isVisible()},
            {QStringLiteral("vertical_bar_width"), v->isVisible() ? v->width() : 0},
            {QStringLiteral("horizontal_policy"),
             area->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff ? QStringLiteral("always off")
                                                                          : QStringLiteral("as needed")},
            {QStringLiteral("horizontal_value"), h->value()},
            {QStringLiteral("horizontal_maximum"), h->maximum()},
            {QStringLiteral("body_width"), body->width()},
            {QStringLiteral("body_minimum_width"), minWidthOf(body)},
            {QStringLiteral("body_over_viewport_px"), qMax(0, body->width() - vp->width())},
            {QStringLiteral("applets"), applets},
        });
    }
    out[QStringLiteral("scroll_areas")] = areas;
    return out;
}

QJsonArray measureAppletFit(QWidget* window)
{
    QJsonArray out;
    auto* manager = window != nullptr ? window->findChild<ContainerManager*>() : nullptr;
    if (manager == nullptr) {
        return out;
    }
    for (ContainerWidget* c : manager->allContainers()) {
        if (c == nullptr || !c->isVisible()) {
            continue;
        }
        const QList<QWidget*> applets = outerApplets(c);
        if (applets.isEmpty()) {
            continue;
        }
        int hidden = 0;
        int viewportWidth = 0;
        QJsonArray cut;
        for (QWidget* a : applets) {
            QWidget* vp = c;
            for (QWidget* p = a->parentWidget(); p != nullptr && p != c; p = p->parentWidget()) {
                if (auto* area = qobject_cast<QScrollArea*>(p)) {
                    vp = area->viewport();
                    break;
                }
            }
            viewportWidth = qMax(viewportWidth, vp->width());
            const QPoint at = a->mapTo(vp, QPoint(0, 0));
            const int minW = minWidthOf(a);
            const int left = qMax(0, -at.x());
            const int right = qMax(0, at.x() + a->width() - vp->width());
            const int over = qMax(0, minW - vp->width());
            const int px = qMax(left + right, over);
            hidden += px;
            if (px > 0) {
                cut.append(QJsonObject{
                    {QStringLiteral("class"), QString::fromLatin1(a->metaObject()->className())},
                    {QStringLiteral("clipped_left_px"), left},
                    {QStringLiteral("clipped_right_px"), right},
                    {QStringLiteral("minimum_over_viewport_px"), over},
                });
            }
        }
        out.append(QJsonObject{
            {QStringLiteral("id"), c->id()},
            {QStringLiteral("dock_mode"), static_cast<int>(c->dockMode())},
            {QStringLiteral("floating"), c->isFloating()},
            {QStringLiteral("width"), c->width()},
            {QStringLiteral("viewport_width"), viewportWidth},
            {QStringLiteral("applets"), applets.size()},
            {QStringLiteral("hidden_px"), hidden},
            {QStringLiteral("cut"), cut},
        });
    }
    return out;
}

void appendPanelFitCrop(SceneContext& ctx, const QString& scene, const QJsonObject& fit)
{
    for (CapturedImage& img : ctx.images) {
        if (img.kind == QLatin1String("window")) {
            img.extra[QStringLiteral("panel_fit")] = fit;
        }
    }
    auto* manager = ctx.window->findChild<ContainerManager*>();
    QWidget* panel = manager != nullptr ? manager->panelContainer() : nullptr;
    if (panel == nullptr || !panel->isVisible()) {
        ctx.notes.append(QStringLiteral("strip fit: no visible Main Panel container to crop"));
        return;
    }
    const QRect r = rectIn(ctx.window, panel);
    CapturedImage c;
    c.name = scene + QStringLiteral("--panel");
    c.kind = QStringLiteral("crop");
    c.image = grabImage(ctx.window, r);
    c.dpr = c.image.devicePixelRatio();
    c.logicalSize = r.size();
    c.extra[QStringLiteral("source")] = scene;
    c.extra[QStringLiteral("logical_rect")] = rectArray(r);
    c.extra[QStringLiteral("device_rect")] = rectArray(toDevice(r, c.dpr));
    c.extra[QStringLiteral("panel_fit")] = fit;
    ctx.images.append(c);
}

// ---- choice sheets -----------------------------------------------------------
//
// Sheets that put a few candidate looks for one kind of control side by
// side for JJ to pick from: checkbox and radio "off" states, lit push
// buttons, and field and button sizes. Each row is one candidate; the
// cells are real Qt widgets with the candidate's stylesheet.

namespace {

constexpr int kGridLabelWidth = 250;
constexpr int kIndicatorRowHeight = 16;   // today's checkbox and radio row height
constexpr int kIndicatorWidth = 90;
constexpr int kLitButtonWidth = 72;
constexpr int kLitButtonHeight = 22;      // Style::kButtonH
// Longpath's corner radius and the field height it sits on (sizeHint of
// its line edit and combo, controls-sheet sidecar): the ratio that row 3
// of the sizes sheet scales today's heights by.
constexpr int kLongpathRadiusPx = 7;
constexpr int kLongpathFieldHeightPx = 26;
// Today's corner radius on combo, line edit, spin box and button
// (StyleConstants.h kComboStyle, kLineEditStyle, kSpinBoxStyle,
// buttonBaseStyle); today's tabs (SpotHubDialog) have square corners.
constexpr int kTodayRadiusPx = 3;
constexpr int kTodayTabRadiusPx = 0;

struct GridRow {
    QString key;
    QString title;
    QString note;
    QVector<QVector<Shot>> groups;
};

// Lays the rows out as a sheet of QLabels and grabs it, one column group
// per entry of `groupTitles`. `placements` receives each image's rect.
QImage assembleGrid(const QVector<GridRow>& rows, const QStringList& groupTitles, const QString& title,
                    const QString& subtitle, QJsonArray& placements)
{
    auto sheet = std::make_unique<QWidget>();
    sheet->setObjectName(QStringLiteral("choiceSheet"));
    sheet->setAttribute(Qt::WA_DontShowOnScreen, true);
    sheet->setAutoFillBackground(true);
    QPalette pal = sheet->palette();
    pal.setColor(QPalette::Window, QColor(QLatin1String(kSheetBg)));
    sheet->setPalette(pal);

    struct Placed {
        QLabel* caption;
        QLabel* image;
        int width;
        int height;
    };
    const int groupCount = static_cast<int>(groupTitles.size());
    QVector<QLabel*> heads;
    QVector<int> columnWidth(groupCount, 0);
    for (int g = 0; g < groupCount; ++g) {
        QLabel* head = textLabel(sheet.get(), groupTitles[g], kSheetTitleColour, 12, true);
        heads.append(head);
        columnWidth[g] = head->width();
    }
    std::vector<QVector<QVector<Placed>>> placed(rows.size());
    int captionHeight = 0;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        placed[r].resize(groupCount);
        for (int g = 0; g < groupCount && g < rows[r].groups.size(); ++g) {
            int width = 0;
            for (const Shot& s : rows[r].groups[g]) {
                QLabel* cap = textLabel(sheet.get(), s.caption, kSheetCaptionColour, 10);
                auto* img = new QLabel(sheet.get());
                img->setPixmap(QPixmap::fromImage(s.image));
                const QSize logical = logicalSizeOf(s.image);
                img->setFixedSize(logical);
                const int w = qMax(logical.width(), cap->width());
                placed[r][g].append({cap, img, w, logical.height()});
                captionHeight = qMax(captionHeight, cap->height());
                width += (width > 0 ? kShotGapPx : 0) + w;
            }
            columnWidth[g] = qMax(columnWidth[g], width);
        }
    }

    QLabel* titleLabel = textLabel(sheet.get(), title, kSheetTitleColour, 15, true);
    titleLabel->move(kSheetMarginPx, kSheetMarginPx);
    QLabel* subtitleLabel = textLabel(sheet.get(), subtitle, kSheetTextColour, 11);
    subtitleLabel->move(kSheetMarginPx, titleLabel->geometry().bottom() + 4);
    int y = subtitleLabel->geometry().bottom() + 14;

    QVector<int> columnX(groupCount, 0);
    int x = kSheetMarginPx + kGridLabelWidth + kColumnGapPx;
    for (int g = 0; g < groupCount; ++g) {
        columnX[g] = x;
        heads[g]->move(x, y);
        x += columnWidth[g] + kColumnGapPx;
    }
    const int sheetWidth = qMax(x - kColumnGapPx + kSheetMarginPx,
                                qMax(titleLabel->width(), subtitleLabel->width()) + 2 * kSheetMarginPx);
    y += 24;

    for (std::size_t r = 0; r < rows.size(); ++r) {
        const GridRow& row = rows[r];
        auto* rule = new QFrame(sheet.get());
        rule->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; }").arg(QLatin1String(kSheetRule)));
        rule->setGeometry(kSheetMarginPx, y, sheetWidth - 2 * kSheetMarginPx, 1);
        y += kRowGapPx / 2;
        QLabel* name = textLabel(sheet.get(), row.title, kSheetTextColour, 11, true);
        name->setWordWrap(true);
        name->setFixedWidth(kGridLabelWidth);
        name->adjustSize();
        name->move(kSheetMarginPx, y + captionHeight);
        int labelBottom = name->geometry().bottom();
        if (!row.note.isEmpty()) {
            QLabel* note = textLabel(sheet.get(), row.note, kSheetCaptionColour, 10);
            note->setWordWrap(true);
            note->setFixedWidth(kGridLabelWidth);
            note->adjustSize();
            note->move(kSheetMarginPx, labelBottom + 4);
            labelBottom = note->geometry().bottom();
        }
        int rowHeight = 0;
        for (int g = 0; g < groupCount; ++g) {
            int sx = columnX[g];
            for (int i = 0; i < placed[r][g].size(); ++i) {
                const Placed& p = placed[r][g][i];
                const Shot& s = row.groups[g][i];
                p.caption->move(sx, y);
                p.image->move(sx, y + captionHeight + 2);
                const double dpr = s.image.devicePixelRatio();
                QJsonObject at{
                    {QStringLiteral("row"), row.key},
                    {QStringLiteral("group"), groupTitles[g]},
                    {QStringLiteral("caption"), s.caption},
                    {QStringLiteral("sheet_logical_rect"), rectArray(p.image->geometry())},
                    {QStringLiteral("sheet_device_rect"), rectArray(toDevice(p.image->geometry(), dpr))},
                };
                if (!s.sizeHint.isEmpty()) {
                    at.insert(QStringLiteral("size_hint"), sizeArray(s.sizeHint));
                }
                if (!s.source.isEmpty()) {
                    at.insert(QStringLiteral("source"), s.source);
                }
                placements.append(at);
                rowHeight = qMax(rowHeight, captionHeight + 2 + p.height);
                sx += p.width + kShotGapPx;
            }
        }
        y = qMax(y + rowHeight, labelBottom) + kRowGapPx;
    }
    sheet->resize(sheetWidth, y + kSheetMarginPx);
    sheet->show();
    pump(kSettleMs);
    return grabImage(sheet.get());
}

Shot zoomShot(const Shot& s)
{
    const double dpr = s.image.devicePixelRatio();
    const QRect device = toDevice(s.focus, dpr).intersected(s.image.rect());
    const QImage cut = s.image.copy(device);
    QImage big = cut.scaled(cut.width() * kZoomFactor, cut.height() * kZoomFactor, Qt::IgnoreAspectRatio,
                            Qt::FastTransformation);
    big.setDevicePixelRatio(dpr);
    Shot z;
    z.caption = s.caption;
    z.image = big;
    z.source = QJsonObject{{QStringLiteral("cell_device_rect"), rectArray(device)}};
    return z;
}

// The zoom grid: every shot with a focus, 5x nearest-neighbour. With
// `splitGroups`, each group of a row becomes its own row (keeps a wide
// row of large cut-outs to a sane width).
QVector<GridRow> zoomGrid(const QVector<GridRow>& rows, const QStringList& groupTitles, bool splitGroups,
                          QStringList& outGroups)
{
    QVector<GridRow> out;
    if (!splitGroups) {
        outGroups = groupTitles;
        for (const GridRow& row : rows) {
            GridRow z{row.key, row.title, QString(), {}};
            for (const QVector<Shot>& group : row.groups) {
                QVector<Shot> zs;
                for (const Shot& s : group) {
                    if (!s.focus.isEmpty()) {
                        zs.append(zoomShot(s));
                    }
                }
                z.groups.append(zs);
            }
            out.append(z);
        }
        return out;
    }
    outGroups = QStringList{QStringLiteral("5x nearest-neighbour")};
    for (const GridRow& row : rows) {
        for (int g = 0; g < row.groups.size(); ++g) {
            GridRow z{row.key + QLatin1Char('-') + QString::number(g),
                      row.title + QStringLiteral(", ") + groupTitles.value(g), QString(), {}};
            QVector<Shot> zs;
            for (const Shot& s : row.groups[g]) {
                if (!s.focus.isEmpty()) {
                    zs.append(zoomShot(s));
                }
            }
            z.groups.append(zs);
            out.append(z);
        }
    }
    return out;
}

// A cell painted in `bg` behind the control, whatever the look's palette
// or app stylesheet says.
QWidget* paintedCell(Column& col, const QSize& inner, const char* bg)
{
    QWidget* cell = col.cell(inner);
    cell->setObjectName(QStringLiteral("choiceCell"));
    cell->setAttribute(Qt::WA_StyledBackground, true);
    cell->setStyleSheet(QStringLiteral("QWidget#choiceCell { background: %1; }").arg(QLatin1String(bg)));
    return cell;
}

Shot cellShot(QWidget* cell, QWidget* control, const QString& caption, const QRect& focusInCell)
{
    Shot s;
    s.caption = caption;
    s.image = grabImage(cell);
    s.focus = focusInCell;
    s.sizeHint = control->sizeHint();
    s.minimumHint = control->minimumSizeHint();
    return s;
}

// The 32x24 zoom patch around an indicator, in cell coordinates.
QRect patchAround(QWidget* control, QWidget* cell, const QRect& inControl)
{
    const QPoint centre = control->mapTo(cell, inControl.center());
    return QRect(centre.x() - kZoomSourceWidth / 2, centre.y() - kZoomSourceHeight / 2, kZoomSourceWidth,
                 kZoomSourceHeight);
}

void activate(QWidget* top)
{
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(top);
    QT_WARNING_POP
}

void setSheetImages(SceneContext& ctx, const QString& name, const QImage& image, const QJsonArray& placements,
                    const QJsonObject& extra)
{
    CapturedImage c;
    c.name = name;
    c.kind = name.endsWith(QLatin1String("--zoom")) ? QStringLiteral("zoom") : QStringLiteral("sheet");
    c.image = image;
    c.dpr = image.devicePixelRatio();
    c.logicalSize = logicalSizeOf(image);
    c.extra = extra;
    c.extra[QStringLiteral("window")] = QStringLiteral("its own top-level QWidget, not the MainWindow");
    c.extra[QStringLiteral("placements")] = placements;
    ctx.images.append(c);
}

bool startChoiceScene(SceneContext& ctx, const QString& scene)
{
    ctx.provenance = QStringLiteral("proposed");
    if (app() == nullptr || ctx.window == nullptr) {
        ctx.failure = scene + QStringLiteral(": no application or window");
        return false;
    }
    if (WorkspacePreview::enabled()) {
        ctx.failure = scene + QStringLiteral(": must start with the preview switch off");
        return false;
    }
    return true;
}

// ---- checkbox and radio off states ----

// One candidate. Empty strings keep Longpath's own value.
struct IndicatorVariant {
    QString key;
    QString title;
    QString note;
    bool today{false};
    QString offRim;
    QString offFill;            // a stylesheet background value
    QString disabledOffRim;
    QString disabledOffFill;
    QString disabledOn;         // fill and rim of a disabled, checked indicator
};

QString sunkenGradient(const char* top, const char* bottom)
{
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
        .arg(QLatin1String(top), QLatin1String(bottom));
}

// Longpath's sheet at today's 16 px row height, with the candidate's off
// and disabled values laid over it. Rules later in the sheet win at equal
// specificity; Longpath's :hover and :checked rules are more specific than
// the plain ::indicator override, so they keep Longpath's values.
QString indicatorSheet(const char* widgetClass, const char* longpathSheet, const IndicatorVariant& v)
{
    const QLatin1String cls(widgetClass);
    QString s = QString::fromLatin1(longpathSheet);
    s += QStringLiteral("%1 { min-height: %2px; }").arg(cls).arg(kIndicatorRowHeight);
    if (!v.offRim.isEmpty()) {
        s += QStringLiteral("%1::indicator { border: 1px solid %2; }").arg(cls, v.offRim);
    }
    if (!v.offFill.isEmpty()) {
        s += QStringLiteral("%1::indicator { background: %2; }").arg(cls, v.offFill);
    }
    if (!v.disabledOffRim.isEmpty()) {
        s += QStringLiteral("%1::indicator:disabled { background: %2; border: 1px solid %3; }")
                 .arg(cls, v.disabledOffFill, v.disabledOffRim);
        s += QStringLiteral("%1::indicator:checked:disabled { background: %2; border: 1px solid %2; }")
                 .arg(cls, v.disabledOn);
    }
    return s;
}

QVector<IndicatorVariant> indicatorVariants()
{
    const QString onNote = QStringLiteral(
        "On: fill %1 to %2 (kGlassSelTop, kGlassSelBot), rim %3 (kGlassSelBorder), top edge "
        "#4b88c1. No check mark.")
                               .arg(QLatin1String(Workspace::kGlassSelTop), QLatin1String(Workspace::kGlassSelBot),
                                    QLatin1String(Workspace::kGlassSelBorder));
    QVector<IndicatorVariant> out;
    out.append({QStringLiteral("today"), QStringLiteral("Today (for reference)"),
                QStringLiteral("Style::kCheckBoxStyle and kRadioButtonStyle, switch off. Off: rim #304050, fill "
                               "#1a2a3a. On: fill and rim #00b4d8. No disabled rule, so disabled looks enabled."),
                true, {}, {}, {}, {}, {}});
    out.append({QStringLiteral("longpath"), QStringLiteral("Longpath as is"),
                QStringLiteral("Longpath's kCheckBoxStyle and kRadioButtonStyle, min-height 16. Off: rim %1 "
                               "(kBorder) with a shade top edge rgba(0,0,0,160) and a light bottom edge "
                               "rgba(255,255,255,18); fill #000000 to #08080c. Disabled on (box only): %2 "
                               "(kDisabledBorder). Disabled off has no rule, and the radio has no disabled "
                               "rule, so a disabled radio looks enabled. ")
                        .arg(QLatin1String(Workspace::kBorder), QLatin1String(Workspace::kDisabledBorder))
                    + onNote,
                false, {}, {}, {}, {}, {}});
    const auto variant = [&](const QString& key, const QString& title, const char* rim, const char* rimRole,
                             const char* fillTop, const char* fillBottom, const QString& fillRoles,
                             const char* dRim, const char* dRimRole, const char* dOn, const char* dOnRole) {
        const QString fill = fillTop == nullptr ? QString() : sunkenGradient(fillTop, fillBottom);
        const QString fillText = fillTop == nullptr
                                     ? QStringLiteral("#000000 to #08080c (Longpath's)")
                                     : QStringLiteral("%1 to %2 (%3)").arg(QLatin1String(fillTop),
                                                                           QLatin1String(fillBottom), fillRoles);
        const QString note =
            QStringLiteral("Off: rim %1 (%2) all round, fill %3. Disabled off: rim %4 (%5), fill %6 (kInsetBg). "
                           "Disabled on: fill and rim %7 (%8). On: as Longpath.")
                .arg(QLatin1String(rim), QLatin1String(rimRole), fillText, QLatin1String(dRim),
                     QLatin1String(dRimRole), QLatin1String(Workspace::kInsetBg), QLatin1String(dOn),
                     QLatin1String(dOnRole));
        out.append({key, title, note, false, QLatin1String(rim), fill, QLatin1String(dRim),
                    QLatin1String(Workspace::kInsetBg), QLatin1String(dOn)});
    };
    variant(QStringLiteral("brighter-rim"), QStringLiteral("Brighter rim"), Workspace::kBorderMuted,
            "kBorderMuted", nullptr, nullptr, QString(), Workspace::kBorder, "kBorder", Workspace::kBorderMuted,
            "kBorderMuted");
    variant(QStringLiteral("brighter-rim-fill"), QStringLiteral("Brighter rim and fill"), Workspace::kTextInactive,
            "kTextInactive", Workspace::kInsetBg, Workspace::kButtonBg, QStringLiteral("kInsetBg to kButtonBg"),
            Workspace::kBorderMuted, "kBorderMuted", Workspace::kDisabledText, "kDisabledText");
    variant(QStringLiteral("strongest"), QStringLiteral("Strongest"), Workspace::kTextScale, "kTextScale",
            Workspace::kGlassBtnBot, Workspace::kButtonHover, QStringLiteral("kGlassBtnBot to kButtonHover"),
            Workspace::kTextInactive, "kTextInactive", Workspace::kTextInactive, "kTextInactive");
    return out;
}

struct Background {
    QString title;
    const char* colour;
};

QVector<Background> indicatorBackgrounds()
{
    // Sampled from the preview's own captures: the Main Panel behind the
    // applets (strip-fit-preview--panel) and the Setup page body
    // (look-dialog-setup-*).
    return {{QStringLiteral("On the Main Panel (%1, kPanelBg)").arg(QLatin1String(Workspace::kPanelBg)),
             Workspace::kPanelBg},
            {QStringLiteral("On a Setup page (%1, kAppBg)").arg(QLatin1String(Workspace::kAppBg)),
             Workspace::kAppBg}};
}

// One variant's row: checkbox off, on, disabled off, disabled on, then the
// same four for the radio button, on each background.
GridRow captureIndicatorRow(const IndicatorVariant& v)
{
    const QString checkSheet = v.today ? QString::fromLatin1(Style::kCheckBoxStyle)
                                       : indicatorSheet("QCheckBox", LongpathLook::kCheckBoxStyle, v);
    const QString radioSheet = v.today ? QString::fromLatin1(Style::kRadioButtonStyle)
                                       : indicatorSheet("QRadioButton", LongpathLook::kRadioButtonStyle, v);
    const QVector<Background> backgrounds = indicatorBackgrounds();
    const QSize size(kIndicatorWidth, kIndicatorRowHeight);
    const std::array<QString, 4> states{QStringLiteral("off"), QStringLiteral("on"), QStringLiteral("disabled off"),
                                        QStringLiteral("disabled on")};

    Column col;
    struct Cell {
        QWidget* cell;
        QAbstractButton* button;
        QRect indicator;
        QString caption;
    };
    QVector<QVector<Cell>> groups;
    for (const Background& bg : backgrounds) {
        QVector<Cell> cells;
        for (int kind = 0; kind < 2; ++kind) {
            for (std::size_t i = 0; i < states.size(); ++i) {
                QWidget* cell = paintedCell(col, size, bg.colour);
                QAbstractButton* b = nullptr;
                if (kind == 0) {
                    auto* box = new ProbeCheckBox(QStringLiteral("Check box"), cell);
                    box->setStyleSheet(checkSheet);
                    b = box;
                } else {
                    auto* radio = new ProbeRadioButton(QStringLiteral("Radio"), cell);
                    radio->setStyleSheet(radioSheet);
                    b = radio;
                }
                col.place(b, size);
                b->setChecked(i == 1 || i == 3);
                b->setEnabled(i < 2);
                cells.append({cell, b, {},
                              (kind == 0 ? QStringLiteral("box ") : QStringLiteral("radio ")) + states[i]});
            }
        }
        groups.append(cells);
    }
    col.finish();

    GridRow row{v.key, v.title, v.note, {}};
    for (QVector<Cell>& cells : groups) {
        QVector<Shot> shots;
        for (Cell& c : cells) {
            QRect ind;
            if (auto* box = dynamic_cast<ProbeCheckBox*>(c.button)) {
                ind = box->indicatorRect();
            } else if (auto* radio = dynamic_cast<ProbeRadioButton*>(c.button)) {
                ind = radio->indicatorRect();
            }
            Shot s = cellShot(c.cell, c.button, c.caption, patchAround(c.button, c.cell, ind));
            s.source = QJsonObject{{QStringLiteral("indicator_rect"), rectArray(ind)}};
            shots.append(s);
        }
        row.groups.append(shots);
    }
    return row;
}

// ---- lit push buttons ----

enum class Meaning { Green, Blue, Amber };

struct LitLabel {
    QString text;
    Meaning meaning;
};

QVector<LitLabel> litLabels()
{
    return {{QStringLiteral("NR"), Meaning::Green},     {QStringLiteral("MON"), Meaning::Green},
            {QStringLiteral("2.7K"), Meaning::Blue},    {QStringLiteral("ANT1"), Meaning::Blue},
            {QStringLiteral("RIT"), Meaning::Amber},    {QStringLiteral("XIT"), Meaning::Amber},
            {QStringLiteral("OPERATE"), Meaning::Green}};
}

// Longpath's amber roles in the raised form of its blueCheckedStyle
// (raisedFill instead of a flat fill, a lighter top edge on the border).
// Our composition: Longpath has no amberCheckedStyle.
QString longpathAmberChecked()
{
    return QStringLiteral(
        "QPushButton:checked {"
        "  background: %1;"
        "  color: %2; border: 1px solid %3; border-top-color: %4; }")
        .arg(LongpathLook::raisedFill(Workspace::kAmberBg), QLatin1String(Workspace::kAmberText),
             QLatin1String(Workspace::kAmberBorder), Workspace::shiftL(QColor(QLatin1String(Workspace::kAmberBorder)), 16));
}

struct LitVariant {
    QString key;
    QString title;
    QString note;
    bool today;
    std::function<QString(Meaning)> sheet;
};

QVector<LitVariant> litVariants()
{
    QVector<LitVariant> out;
    out.append({QStringLiteral("today"), QStringLiteral("Today: three meanings"),
                QStringLiteral("Style::buttonBaseStyle plus greenCheckedStyle (processing on: NR, MON, OPERATE), "
                               "blueCheckedStyle (selection: 2.7K, ANT1) or amberCheckedStyle (offsets: RIT, XIT), "
                               "switch off. Green #006040 / #00ff88 / #00a060, blue #0070c0 / #ffffff / #0090e0, "
                               "amber #604000 / #ffb800 / #906000 (fill / text / rim, STYLEGUIDE.md)."),
                true, [](Meaning m) {
                    switch (m) {
                    case Meaning::Green: return Style::buttonBaseStyle() + Style::greenCheckedStyle();
                    case Meaning::Blue: return Style::buttonBaseStyle() + Style::blueCheckedStyle();
                    default: return Style::buttonBaseStyle() + Style::amberCheckedStyle();
                    }
                }});
    out.append({QStringLiteral("longpath-three"), QStringLiteral("Longpath tones: three meanings"),
                QStringLiteral("Longpath's raised buttonBaseStyle. Green: Longpath's greenCheckedStyle, a flat fill "
                               "%1, text %2, rim %3 (Longpath's green is flat, not raised). Blue: Longpath's "
                               "blueCheckedStyle, raised %4 to %5, text %6, rim %7. Amber: our composition from "
                               "Longpath's roles in the raised form of its blue: raisedFill(%8), text %9, rim %10 "
                               "with a lighter top edge.")
                    .arg(QLatin1String(Workspace::kGreenBg), QLatin1String(Workspace::kGreenText),
                         QLatin1String(Workspace::kGreenBorder), QLatin1String(Workspace::kGlassSelTop),
                         QLatin1String(Workspace::kGlassSelBot), QLatin1String(Workspace::kGlassSelText),
                         QLatin1String(Workspace::kGlassSelBorder), QLatin1String(Workspace::kAmberBg),
                         QLatin1String(Workspace::kAmberText))
                    .arg(QLatin1String(Workspace::kAmberBorder)),
                false, [](Meaning m) {
                    switch (m) {
                    case Meaning::Green: return LongpathLook::buttonBaseStyle() + LongpathLook::greenCheckedStyle();
                    case Meaning::Blue: return LongpathLook::buttonBaseStyle() + LongpathLook::blueCheckedStyle();
                    default: return LongpathLook::buttonBaseStyle() + longpathAmberChecked();
                    }
                }});
    out.append({QStringLiteral("longpath-one-blue"), QStringLiteral("Longpath's rule: one blue for every lit button"),
                QStringLiteral("Longpath's raised buttonBaseStyle plus blueCheckedStyle on every button: raised %1 "
                               "to %2, text %3, rim %4.")
                    .arg(QLatin1String(Workspace::kGlassSelTop), QLatin1String(Workspace::kGlassSelBot),
                         QLatin1String(Workspace::kGlassSelText), QLatin1String(Workspace::kGlassSelBorder)),
                false, [](Meaning) { return LongpathLook::buttonBaseStyle() + LongpathLook::blueCheckedStyle(); }});
    return out;
}

GridRow captureLitRow(const LitVariant& v)
{
    const QVector<LitLabel> labels = litLabels();
    const QSize size(kLitButtonWidth, kLitButtonHeight);
    Column col;
    std::array<QVector<QPair<QWidget*, QPushButton*>>, 2> groups;
    for (auto& group : groups) {
        for (const LitLabel& l : labels) {
            QWidget* cell = paintedCell(col, size, Workspace::kPanelBg);
            auto* b = new QPushButton(l.text, cell);
            b->setCheckable(true);
            b->setChecked(true);
            b->setStyleSheet(v.sheet(l.meaning));
            col.place(b, size);
            group.append({cell, b});
        }
    }
    col.finish();
    activate(col.top());
    for (const auto& [cell, b] : groups[1]) {
        hover(b, QPoint(kLitButtonWidth / 2, kLitButtonHeight / 2));
    }
    pump(kShortSettleMs);

    GridRow row{v.key, v.title, v.note, {}};
    for (const auto& group : groups) {
        QVector<Shot> shots;
        for (const auto& [cell, b] : group) {
            shots.append(cellShot(cell, b, b->text(), QRect(b->pos(), b->size())));
        }
        row.groups.append(shots);
    }
    return row;
}

// ---- stronger greens for lit buttons ----
//
// JJ chose Longpath's tones with three meanings for lit buttons; next to
// Longpath's raised blue its flat green reads as nearly off. Each row is
// one green; the blue and amber are the "Longpath tones" row's. Every
// green colour is a palette role, or Longpath's raisedFill / shiftL
// applied to one. No new palette values.

// Lit buttons on the green sheet, and the one unlit NR for comparison.
constexpr auto kUnlitLabel = "NR";
// Lightness steps (shiftL, 0..255) for the green rows below.
constexpr int kGreenEdgeLift = 16;          // top edge, as blueCheckedStyle's
constexpr int kGreenWeightedBaseLift = 14;  // row 3 fill base: kGreenBg lifted
constexpr int kGreenWeightedTextLift = 80;  // row 3 text: kLiveGreen lifted
constexpr int kGreenWeightedRimLift = 16;   // row 3 rim: kDspToggleBorder lifted
constexpr int kGreenStrongTextLift = 100;   // row 4 text: kLiveGreen lifted

QString lifted(const char* hex, int deltaL)
{
    return Workspace::shiftL(QColor(QLatin1String(hex)), deltaL);
}

// A raised green checked rule in the form of blueCheckedStyle: a raised
// fill, a rim and a top edge kGreenEdgeLift lighter than the rim.
QString raisedGreenChecked(const QString& fillBase, const QString& text, const QString& rim)
{
    return QStringLiteral(
        "QPushButton:checked {"
        "  background: %1;"
        "  color: %2; border: 1px solid %3; border-top-color: %4; }")
        .arg(LongpathLook::raisedFill(fillBase.toLatin1().constData()), text, rim,
             Workspace::shiftL(QColor(rim), kGreenEdgeLift));
}

struct GreenVariant {
    QString key;
    QString title;
    QString note;
    QString greenSheet;   // the checked rule laid over Longpath's buttonBaseStyle
};

QVector<GreenVariant> greenVariants()
{
    namespace W = Workspace;
    QVector<GreenVariant> out;
    out.append({QStringLiteral("longpath"), QStringLiteral("Longpath as is (flat)"),
                QStringLiteral("Longpath's greenCheckedStyle: flat fill %1 (kGreenBg), text %2 (kGreenText), rim %3 "
                               "(kGreenBorder).")
                    .arg(QLatin1String(W::kGreenBg), QLatin1String(W::kGreenText), QLatin1String(W::kGreenBorder)),
                LongpathLook::greenCheckedStyle()});

    const auto raised = [&](const QString& key, const QString& title, const QString& base, const QString& baseRole,
                            const QString& text, const QString& textRole, const QString& rim,
                            const QString& rimRole) {
        const QString note =
            QStringLiteral("raisedFill(%1, %2): %3 to %1 to %4. Text %5 (%6). Rim %7 (%8), top edge %9 "
                           "(shiftL(rim, +%10)).")
                .arg(base, baseRole, lifted(base.toLatin1().constData(), 16),
                     lifted(base.toLatin1().constData(), -12), text, textRole, rim, rimRole,
                     Workspace::shiftL(QColor(rim), kGreenEdgeLift))
                .arg(kGreenEdgeLift);
        out.append({key, title, note, raisedGreenChecked(base, text, rim)});
    };
    raised(QStringLiteral("raised"), QStringLiteral("Raised"), QLatin1String(W::kGreenBg),
           QStringLiteral("kGreenBg"), QLatin1String(W::kDspToggleText), QStringLiteral("kDspToggleText"),
           QLatin1String(W::kDspToggleBorder), QStringLiteral("kDspToggleBorder"));
    raised(QStringLiteral("raised-bright"), QStringLiteral("Raised, brighter text (aimed at the blue)"),
           lifted(W::kGreenBg, kGreenWeightedBaseLift),
           QStringLiteral("shiftL(kGreenBg, +%1)").arg(kGreenWeightedBaseLift),
           lifted(W::kLiveGreen, kGreenWeightedTextLift),
           QStringLiteral("shiftL(kLiveGreen, +%1)").arg(kGreenWeightedTextLift),
           lifted(W::kDspToggleBorder, kGreenWeightedRimLift),
           QStringLiteral("shiftL(kDspToggleBorder, +%1)").arg(kGreenWeightedRimLift));
    raised(QStringLiteral("strongest"), QStringLiteral("Strongest"), QLatin1String(W::kGreenBorder),
           QStringLiteral("kGreenBorder"), lifted(W::kLiveGreen, kGreenStrongTextLift),
           QStringLiteral("shiftL(kLiveGreen, +%1)").arg(kGreenStrongTextLift), QLatin1String(W::kLiveGreen),
           QStringLiteral("kLiveGreen"));
    return out;
}

// One green row: the seven lit buttons (green from the variant, blue and
// amber as the "Longpath tones" row), then one unlit NR with the same
// sheet. All resting.
GridRow captureGreenRow(const GreenVariant& v)
{
    const QVector<LitLabel> labels = litLabels();
    const QSize size(kLitButtonWidth, kLitButtonHeight);
    const auto sheetFor = [&](Meaning m) {
        switch (m) {
        case Meaning::Green: return LongpathLook::buttonBaseStyle() + v.greenSheet;
        case Meaning::Blue: return LongpathLook::buttonBaseStyle() + LongpathLook::blueCheckedStyle();
        default: return LongpathLook::buttonBaseStyle() + longpathAmberChecked();
        }
    };
    Column col;
    std::array<QVector<QPair<QWidget*, QPushButton*>>, 2> groups;
    for (const LitLabel& l : labels) {
        QWidget* cell = paintedCell(col, size, Workspace::kPanelBg);
        auto* b = new QPushButton(l.text, cell);
        b->setCheckable(true);
        b->setChecked(true);
        b->setStyleSheet(sheetFor(l.meaning));
        col.place(b, size);
        groups[0].append({cell, b});
    }
    QWidget* unlitCell = paintedCell(col, size, Workspace::kPanelBg);
    auto* unlit = new QPushButton(QLatin1String(kUnlitLabel), unlitCell);
    unlit->setCheckable(true);
    unlit->setChecked(false);
    unlit->setStyleSheet(sheetFor(Meaning::Green));
    col.place(unlit, size);
    groups[1].append({unlitCell, unlit});
    col.finish();
    pump(kShortSettleMs);

    GridRow row{v.key, v.title, v.note, {}};
    for (const auto& group : groups) {
        QVector<Shot> shots;
        for (const auto& [cell, b] : group) {
            shots.append(cellShot(cell, b, b->text(), QRect(b->pos(), b->size())));
        }
        row.groups.append(shots);
    }
    return row;
}

// ---- scroll bar thickness ----

class ProbeScrollBar : public QScrollBar
{
public:
    using QScrollBar::QScrollBar;
    QRect handleRect() const
    {
        QStyleOptionSlider o;
        initStyleOption(&o);
        return style()->subControlRect(QStyle::CC_ScrollBar, &o, QStyle::SC_ScrollBarSlider, this);
    }
};

// Thicknesses JJ chooses between (logical px): today's applet panel
// sheet (AppletPanelWidget.cpp), Longpath's kScrollBarStyle, and the
// Fusion PM_ScrollBarExtent the Main Panel's bar has in both looks
// (strip-fit-today--panel and strip-fit-preview--panel sidecars).
constexpr std::array<int, 3> kBarThicknesses{8, 10, 14};
constexpr int kBarReferencePx = 8;
// The Main Panel's scroll area at laptop-1280x800@1: 340 px wide (the
// strip-fit-today--panel sidecar's area_width) and this tall (the live
// window's area, measured by this scene: live_bars.main_panel_today).
constexpr int kBarAreaWidth = 340;
constexpr int kBarAreaHeight = 700;
// The list in the area: label and slider rows, about three viewports tall,
// scrolled to a third.
constexpr int kBarListRows = 72;
constexpr int kBarListRowHeight = 28;
constexpr int kBarScrollPercent = 33;
// The zoom patch: the bar plus some content, around the handle's top.
constexpr int kBarZoomWidth = 40;
constexpr int kBarZoomHeight = 48;
constexpr int kBarZoomAboveHandle = 8;

QString barSheet(int px)
{
    QString bars = QString::fromLatin1(Workspace::kScrollBarStyle);
    bars.replace(QLatin1String("width: 10px; margin: 0;"), QStringLiteral("width: %1px; margin: 0;").arg(px));
    bars.replace(QLatin1String("height: 10px; margin: 0;"), QStringLiteral("height: %1px; margin: 0;").arg(px));
    return bars;
}

struct BarCell {
    QWidget* cell;
    QScrollArea* area;
    ProbeScrollBar* bar;
};

BarCell barCell(Column& col, int px)
{
    const QSize size(kBarAreaWidth, kBarAreaHeight);
    QWidget* cell = paintedCell(col, size, Workspace::kPanelBg);
    auto* area = new QScrollArea(cell);
    area->setObjectName(QStringLiteral("barArea"));
    auto* bar = new ProbeScrollBar(Qt::Vertical);
    area->setVerticalScrollBar(bar);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setWidgetResizable(true);
    area->setStyleSheet(QStringLiteral("QScrollArea#barArea { background: %1; border: none; }"
                                       "QWidget#barBody { background: %1; }")
                            .arg(QLatin1String(Workspace::kPanelBg))
                        + barSheet(px));
    auto* body = new QWidget;
    body->setObjectName(QStringLiteral("barBody"));
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(0);
    const QStringList names{QStringLiteral("AF gain"), QStringLiteral("AGC gain"), QStringLiteral("Squelch"),
                            QStringLiteral("Pan"), QStringLiteral("NR level"), QStringLiteral("Mic gain")};
    for (int i = 0; i < kBarListRows; ++i) {
        auto* row = new QWidget(body);
        row->setFixedHeight(kBarListRowHeight);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(QStringLiteral("%1 %2").arg(names[i % names.size()]).arg(i / names.size() + 1), row);
        label->setFixedWidth(84);
        label->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(QLatin1String(Workspace::kTextPrimary)));
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, kSliderMax);
        slider->setValue((i * 37) % kSliderMax);
        slider->setFixedHeight(kHSliderHeight);
        slider->setStyleSheet(LongpathLook::sliderHStyle());
        rl->addWidget(label);
        rl->addWidget(slider, 1);
        layout->addWidget(row);
    }
    area->setWidget(body);
    col.place(area, size);
    return {cell, area, bar};
}

GridRow captureBarRow(int px, QJsonObject& measured)
{
    Column col;
    std::array<BarCell, 2> cells{barCell(col, px), barCell(col, px)};
    col.finish();
    for (const BarCell& c : cells) {
        c.bar->setValue(c.bar->maximum() * kBarScrollPercent / 100);
    }
    activate(col.top());
    pump(kShortSettleMs);
    hover(cells[1].bar, cells[1].bar->handleRect().center());
    pump(kShortSettleMs);

    const int width = cells[0].bar->isVisible() ? cells[0].bar->width() : 0;
    const int viewport = cells[0].area->viewport()->width();
    measured = QJsonObject{{QStringLiteral("stylesheet_px"), px},
                           {QStringLiteral("bar_width"), width},
                           {QStringLiteral("bar_visible"), cells[0].bar->isVisible()},
                           {QStringLiteral("area_width"), cells[0].area->width()},
                           {QStringLiteral("viewport_width"), viewport},
                           {QStringLiteral("handle_rect"), rectArray(cells[0].bar->handleRect())}};

    const QString note = px == kBarReferencePx
        ? QStringLiteral("kScrollBarStyle at %1 px. Viewport %2 of %3 px: the reference.")
              .arg(px)
              .arg(viewport)
              .arg(kBarAreaWidth)
        : QStringLiteral("kScrollBarStyle at %1 px. Viewport %2 of %3 px: %4 px less content width than 8 px.")
              .arg(px)
              .arg(viewport)
              .arg(kBarAreaWidth)
              .arg(px - kBarReferencePx);
    GridRow row{QStringLiteral("bar-%1").arg(px), QStringLiteral("%1 px").arg(px), note, {}};
    const std::array<QString, 2> captions{QStringLiteral("resting"), QStringLiteral("handle hovered")};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const BarCell& c = cells[i];
        const QRect handle = c.bar->handleRect().translated(c.bar->mapTo(c.cell, QPoint(0, 0)));
        const QRect focus(c.area->geometry().right() + 1 - kBarZoomWidth, handle.top() - kBarZoomAboveHandle,
                          kBarZoomWidth, kBarZoomHeight);
        Shot shot = cellShot(c.cell, c.area, QStringLiteral("%1, viewport %2 px").arg(captions[i]).arg(viewport),
                             focus);
        shot.source = QJsonObject{{QStringLiteral("handle_in_cell"), rectArray(handle)}};
        row.groups.append(QVector<Shot>{shot});
    }
    return row;
}

// ---- field and button sizes ----

struct SizeSheets {
    QString combo;
    QString lineEdit;
    QString spinBox;
    QString tabs;
    QString button;
};

struct SizeSet {
    QSize combo;
    QSize lineEdit;
    QSize spinBox;
    QSize tabs;
    QSize button;
    int tabBarHeight{0};
};

struct SizeControls {
    QComboBox* combo{nullptr};
    QLineEdit* lineEdit{nullptr};
    QSpinBox* spinBox{nullptr};
    QTabWidget* tabs{nullptr};
    QPushButton* button{nullptr};
};

void fillSizeControls(SizeControls& c, QWidget* parent, const SizeSheets& sheets)
{
    c.combo = new QComboBox(parent);
    c.combo->addItems({QStringLiteral("USB"), QStringLiteral("LSB"), QStringLiteral("CW"), QStringLiteral("DIGU")});
    c.combo->setStyleSheet(sheets.combo);
    c.lineEdit = new QLineEdit(QStringLiteral("14.225.000"), parent);
    c.lineEdit->setStyleSheet(sheets.lineEdit);
    c.lineEdit->setCursorPosition(0);
    c.spinBox = new QSpinBox(parent);
    c.spinBox->setRange(0, kSliderMax);
    c.spinBox->setValue(kSpinValue);
    c.spinBox->setStyleSheet(sheets.spinBox);
    c.tabs = new QTabWidget(parent);
    for (const QString& name : {QStringLiteral("RX"), QStringLiteral("TX"), QStringLiteral("Audio")}) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(new QLabel(QStringLiteral("%1 page").arg(name), page));
        c.tabs->addTab(page, name);
    }
    c.tabs->setStyleSheet(sheets.tabs);
    c.button = new QPushButton(QStringLiteral("OPERATE"), parent);
    c.button->setStyleSheet(sheets.button);
}

// Today's sizes: each control's sizeHint with today's stylesheet, in
// today's look.
SizeSet measureTodaySizes()
{
    QWidget holder;
    holder.setAttribute(Qt::WA_DontShowOnScreen, true);
    const LookSheets t = todaySheets();
    SizeControls c;
    fillSizeControls(c, &holder, SizeSheets{t.combo, t.lineEdit, t.spinBox, t.tabs, Style::buttonBaseStyle()});
    holder.show();
    pump(kShortSettleMs);
    SizeSet s;
    s.combo = c.combo->sizeHint();
    s.lineEdit = c.lineEdit->sizeHint();
    s.spinBox = c.spinBox->sizeHint();
    s.tabs = c.tabs->sizeHint();
    s.button = c.button->sizeHint();
    s.tabBarHeight = c.tabs->tabBar()->sizeHint().height();
    return s;
}

int scaledRadius(int height)
{
    return static_cast<int>(std::lround(static_cast<double>(height) * kLongpathRadiusPx / kLongpathFieldHeightPx));
}

// Longpath's sheets with today's geometry laid over them: today's
// padding, font size and the given corner radii. Colours, borders and the
// raised and sunken gradients stay Longpath's.
SizeSheets longpathAtTodayGeometry(int comboR, int fieldR, int tabR, int buttonR)
{
    namespace LP = LongpathLook;
    SizeSheets s;
    s.combo = QString::fromLatin1(LP::kComboStyle)
        + QStringLiteral("QComboBox { border-radius: %1px; padding: 2px 4px; font-size: 12px; }").arg(comboR);
    s.lineEdit = LP::formFieldStyle()
        + QStringLiteral("QLineEdit { border-radius: %1px; padding: 2px 4px; font-size: 12px; }").arg(fieldR);
    s.spinBox = LP::formFieldStyle()
        + QStringLiteral("QSpinBox { border-radius: %1px; padding: 2px 4px; font-size: 12px; }").arg(fieldR);
    s.tabs = QString::fromLatin1(LP::kTabStyle)
        + QStringLiteral("QTabWidget::pane { border-radius: %1px; }"
                         "QTabBar::tab { border-top-left-radius: %1px; border-top-right-radius: %1px;"
                         "  padding: 6px 16px; }")
              .arg(tabR);
    s.button = LP::buttonBaseStyle()
        + QStringLiteral("QPushButton { border-radius: %1px; font-size: 10px; padding: 2px 4px; }").arg(buttonR);
    return s;
}

GridRow captureSizeRow(const QString& key, const QString& title, const QString& note, const SizeSheets& sheets,
                       const SizeSet* fixed)
{
    Column col;
    QWidget scratch;
    SizeControls probe;
    fillSizeControls(probe, &scratch, sheets);
    const std::array<QWidget*, 5> probes{probe.combo, probe.lineEdit, probe.spinBox, probe.tabs, probe.button};
    std::array<QSize, 5> sizes{};
    for (std::size_t i = 0; i < probes.size(); ++i) {
        probes[i]->ensurePolished();
        sizes[i] = probes[i]->sizeHint();
    }
    if (fixed != nullptr) {
        sizes = {fixed->combo, fixed->lineEdit, fixed->spinBox, fixed->tabs, fixed->button};
    }
    std::array<QWidget*, 5> cells{};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cells[i] = paintedCell(col, sizes[i], Workspace::kPanelBg);
    }
    SizeControls c;
    fillSizeControls(c, cells[0], sheets);
    const std::array<QWidget*, 5> controls{c.combo, c.lineEdit, c.spinBox, c.tabs, c.button};
    const std::array<QString, 5> names{QStringLiteral("combo"), QStringLiteral("line edit"),
                                       QStringLiteral("spin box"), QStringLiteral("tabs"),
                                       QStringLiteral("button")};
    for (std::size_t i = 0; i < controls.size(); ++i) {
        controls[i]->setParent(cells[i]);
        col.place(controls[i], sizes[i]);
    }
    col.finish();
    GridRow row{key, title, note, {}};
    QVector<Shot> shots;
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const QSize s = controls[i]->size();
        Shot shot = cellShot(cells[i], controls[i],
                             QStringLiteral("%1 %2x%3").arg(names[i]).arg(s.width()).arg(s.height()), {});
        shot.source = QJsonObject{{QStringLiteral("logical_size"), sizeArray(s)}};
        shots.append(shot);
    }
    row.groups.append(shots);
    return row;
}

} // namespace

void sceneIndicatorChoices(SceneContext& ctx)
{
    const QString scene = QStringLiteral("controls-sheet--indicators");
    if (!startChoiceScene(ctx, scene)) {
        return;
    }
    QApplication* a = app();
    const AppLook today{a->palette(), a->styleSheet()};
    QVector<GridRow> rows;
    QJsonArray variants;
    for (const IndicatorVariant& v : indicatorVariants()) {
        applyLook(v.today ? kToday : kLongpath, today);
        rows.append(captureIndicatorRow(v));
        variants.append(QJsonObject{{QStringLiteral("key"), v.key},
                                    {QStringLiteral("title"), v.title},
                                    {QStringLiteral("colours"), v.note},
                                    {QStringLiteral("look"), v.today ? lookKey(kToday) : lookKey(kLongpath)}});
    }
    applyLook(kToday, today);

    QStringList groups;
    for (const Background& bg : indicatorBackgrounds()) {
        groups.append(bg.title);
    }
    QJsonObject extra{{QStringLiteral("variants"), variants},
                      {QStringLiteral("row_height"), kIndicatorRowHeight}};
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, groups, QStringLiteral("Checkbox and radio: off state choices"),
        QStringLiteral("Real QCheckBox and QRadioButton widgets, %1 px row height, %2 px wide with a text label. "
                       "Each row: box off, on, disabled off, disabled on, then the same for the radio.")
            .arg(kIndicatorRowHeight)
            .arg(kIndicatorWidth),
        placements);
    setSheetImages(ctx, scene, sheet, placements, extra);

    QStringList zoomGroups;
    const QVector<GridRow> zoom = zoomGrid(rows, groups, false, zoomGroups);
    QJsonArray zoomPlacements;
    const QImage zoomImage = assembleGrid(
        zoom, zoomGroups, QStringLiteral("Checkbox and radio off states, 5x nearest-neighbour"),
        QStringLiteral("Each cut-out is %1x%2 logical px around the indicator, enlarged 5x.")
            .arg(kZoomSourceWidth)
            .arg(kZoomSourceHeight),
        zoomPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--zoom"), zoomImage, zoomPlacements,
                   QJsonObject{{QStringLiteral("source"), scene},
                               {QStringLiteral("enlargement"), QStringLiteral("5x nearest-neighbour")}});
}

void sceneLitButtons(SceneContext& ctx)
{
    const QString scene = QStringLiteral("controls-sheet--lit-buttons");
    if (!startChoiceScene(ctx, scene)) {
        return;
    }
    QApplication* a = app();
    const AppLook today{a->palette(), a->styleSheet()};
    QVector<GridRow> rows;
    QJsonArray variants;
    for (const LitVariant& v : litVariants()) {
        applyLook(v.today ? kToday : kLongpath, today);
        rows.append(captureLitRow(v));
        variants.append(QJsonObject{{QStringLiteral("key"), v.key},
                                    {QStringLiteral("title"), v.title},
                                    {QStringLiteral("colours"), v.note}});
    }
    applyLook(kToday, today);

    const QStringList groups{QStringLiteral("Checked, resting"), QStringLiteral("Checked, hovered")};
    QJsonObject extra{{QStringLiteral("variants"), variants},
                      {QStringLiteral("background"), QLatin1String(Workspace::kPanelBg)},
                      {QStringLiteral("button_size"), sizeArray(QSize(kLitButtonWidth, kLitButtonHeight))}};
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, groups, QStringLiteral("Lit push buttons: which colour means what"),
        QStringLiteral("Real checked QPushButtons, %1x%2 logical px, on the Main Panel background %3.")
            .arg(kLitButtonWidth)
            .arg(kLitButtonHeight)
            .arg(QLatin1String(Workspace::kPanelBg)),
        placements);
    setSheetImages(ctx, scene, sheet, placements, extra);

    QStringList zoomGroups;
    const QVector<GridRow> zoom = zoomGrid(rows, groups, true, zoomGroups);
    QJsonArray zoomPlacements;
    const QImage zoomImage = assembleGrid(zoom, zoomGroups,
                                          QStringLiteral("Lit push buttons, 5x nearest-neighbour"),
                                          QStringLiteral("Each cut-out is the whole button, enlarged 5x."),
                                          zoomPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--zoom"), zoomImage, zoomPlacements,
                   QJsonObject{{QStringLiteral("source"), scene},
                               {QStringLiteral("enlargement"), QStringLiteral("5x nearest-neighbour")}});
}

void sceneLitGreenChoices(SceneContext& ctx)
{
    const QString scene = QStringLiteral("controls-sheet--lit-green");
    if (!startChoiceScene(ctx, scene)) {
        return;
    }
    QApplication* a = app();
    const AppLook today{a->palette(), a->styleSheet()};
    applyLook(kLongpath, today);
    QVector<GridRow> rows;
    QJsonArray variants;
    for (const GreenVariant& v : greenVariants()) {
        rows.append(captureGreenRow(v));
        variants.append(QJsonObject{{QStringLiteral("key"), v.key},
                                    {QStringLiteral("title"), v.title},
                                    {QStringLiteral("colours"), v.note}});
    }
    applyLook(kToday, today);

    const QString blueAmber =
        QStringLiteral("Blue (2.7K, ANT1): Longpath's blueCheckedStyle, %1 to %2, text %3, rim %4. Amber (RIT, XIT): "
                       "raisedFill(%5), text %6, rim %7. Both as the \"Longpath tones\" row of the lit-buttons sheet.")
            .arg(QLatin1String(Workspace::kGlassSelTop), QLatin1String(Workspace::kGlassSelBot),
                 QLatin1String(Workspace::kGlassSelText), QLatin1String(Workspace::kGlassSelBorder),
                 QLatin1String(Workspace::kAmberBg), QLatin1String(Workspace::kAmberText),
                 QLatin1String(Workspace::kAmberBorder));
    const QStringList groups{QStringLiteral("Lit, resting"), QStringLiteral("Unlit, resting")};
    QJsonObject extra{{QStringLiteral("variants"), variants},
                      {QStringLiteral("blue_amber"), blueAmber},
                      {QStringLiteral("background"), QLatin1String(Workspace::kPanelBg)},
                      {QStringLiteral("button_size"), sizeArray(QSize(kLitButtonWidth, kLitButtonHeight))}};
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, groups, QStringLiteral("Lit buttons: green choices"),
        QStringLiteral("Real QPushButtons, %1x%2 logical px, on the Main Panel background %3, Longpath's raised "
                       "buttonBaseStyle. Green: NR, MON, OPERATE.\n%4")
            .arg(kLitButtonWidth)
            .arg(kLitButtonHeight)
            .arg(QLatin1String(Workspace::kPanelBg), blueAmber),
        placements);
    setSheetImages(ctx, scene, sheet, placements, extra);

    QStringList zoomGroups;
    const QVector<GridRow> zoom = zoomGrid(rows, groups, false, zoomGroups);
    QJsonArray zoomPlacements;
    const QImage zoomImage = assembleGrid(zoom, zoomGroups, QStringLiteral("Lit buttons: green choices, 5x"),
                                          QStringLiteral("Each cut-out is the whole button, enlarged 5x "
                                                         "nearest-neighbour."),
                                          zoomPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--zoom"), zoomImage, zoomPlacements,
                   QJsonObject{{QStringLiteral("source"), scene},
                               {QStringLiteral("enlargement"), QStringLiteral("5x nearest-neighbour")}});
}

void sceneScrollBarChoices(SceneContext& ctx, const QJsonObject& liveBars)
{
    const QString scene = QStringLiteral("controls-sheet--scrollbars");
    if (!startChoiceScene(ctx, scene)) {
        return;
    }
    QApplication* a = app();
    const AppLook today{a->palette(), a->styleSheet()};
    applyLook(kLongpath, today);
    QVector<GridRow> rows;
    QJsonArray measured;
    for (int px : kBarThicknesses) {
        QJsonObject m;
        rows.append(captureBarRow(px, m));
        measured.append(m);
    }
    applyLook(kToday, today);

    const QStringList groups{QStringLiteral("Resting"), QStringLiteral("Handle hovered")};
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, groups, QStringLiteral("Scroll bar thickness"),
        QStringLiteral("Real QScrollArea, %1x%2 logical px (the Main Panel's area at laptop 1280x800), on %3, "
                       "holding label and slider rows, scrolled to %4 %. Longpath's kScrollBarStyle with only the "
                       "thickness changed: handle %5, hovered %6, 2 px margin.")
            .arg(kBarAreaWidth)
            .arg(kBarAreaHeight)
            .arg(QLatin1String(Workspace::kPanelBg))
            .arg(kBarScrollPercent)
            .arg(QStringLiteral("#2c2c31"), QStringLiteral("#3a3a41")),
        placements);
    setSheetImages(ctx, scene, sheet, placements,
                   QJsonObject{{QStringLiteral("measured"), measured},
                               {QStringLiteral("live_bars"), liveBars},
                               {QStringLiteral("area_size"), sizeArray(QSize(kBarAreaWidth, kBarAreaHeight))}});

    QStringList zoomGroups;
    const QVector<GridRow> zoom = zoomGrid(rows, groups, false, zoomGroups);
    QJsonArray zoomPlacements;
    const QImage zoomImage = assembleGrid(
        zoom, zoomGroups, QStringLiteral("Scroll bar thickness, 5x"),
        QStringLiteral("Each cut-out is %1x%2 logical px at the area's right edge around the handle's top, "
                       "enlarged 5x nearest-neighbour.")
            .arg(kBarZoomWidth)
            .arg(kBarZoomHeight),
        zoomPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--zoom"), zoomImage, zoomPlacements,
                   QJsonObject{{QStringLiteral("source"), scene},
                               {QStringLiteral("enlargement"), QStringLiteral("5x nearest-neighbour")}});
}

void sceneControlSizes(SceneContext& ctx)
{
    const QString scene = QStringLiteral("controls-sheet--sizes");
    if (!startChoiceScene(ctx, scene)) {
        return;
    }
    QApplication* a = app();
    const AppLook today{a->palette(), a->styleSheet()};
    applyLook(kToday, today);
    const SizeSet todaySize = measureTodaySizes();

    applyLook(kLongpath, today);
    QVector<GridRow> rows;
    namespace LP = LongpathLook;
    const LookSheets lp = longpathSheets();
    rows.append(captureSizeRow(
        QStringLiteral("longpath"), QStringLiteral("Longpath at its own sizes"),
        QStringLiteral("Longpath's stylesheets as in the controls sheet: padding 3px 8px on fields, 4px 10px and "
                       "11px bold on buttons, 5px 14px on tabs; corners %1 px (panes 10 px). Each control at its "
                       "own sizeHint.")
            .arg(kLongpathRadiusPx),
        SizeSheets{lp.combo, lp.lineEdit, lp.spinBox, lp.tabs, LP::buttonBaseStyle()}, nullptr));
    rows.append(captureSizeRow(
        QStringLiteral("today-geometry"), QStringLiteral("Today's sizes and corners, Longpath shading"),
        QStringLiteral("Longpath's colours, borders and raised and sunken gradients with today's padding "
                       "(2px 4px, tabs 6px 16px), font size (12 px fields, 10 px bold buttons) and corners (%1 px; "
                       "tabs %2 px). Each control at today's sizeHint.")
            .arg(kTodayRadiusPx)
            .arg(kTodayTabRadiusPx),
        longpathAtTodayGeometry(kTodayRadiusPx, kTodayRadiusPx, kTodayTabRadiusPx, kTodayRadiusPx), &todaySize));
    const int comboR = scaledRadius(todaySize.combo.height());
    const int fieldR = scaledRadius(qMax(todaySize.lineEdit.height(), todaySize.spinBox.height()));
    const int tabR = scaledRadius(todaySize.tabBarHeight);
    const int buttonR = scaledRadius(todaySize.button.height());
    rows.append(captureSizeRow(
        QStringLiteral("today-geometry-round"), QStringLiteral("Today's sizes, rounder corners"),
        QStringLiteral("As the row above, with each corner radius scaled to the control's height by Longpath's own "
                       "ratio, %1 px on a %2 px field: combo %3 px, line edit and spin box %4 px, tabs and pane %5 "
                       "px, button %6 px on a %7 px button. A pill would need half the height (%8 px).")
            .arg(kLongpathRadiusPx)
            .arg(kLongpathFieldHeightPx)
            .arg(comboR)
            .arg(fieldR)
            .arg(tabR)
            .arg(buttonR)
            .arg(todaySize.button.height())
            .arg(todaySize.button.height() / 2),
        longpathAtTodayGeometry(comboR, fieldR, tabR, buttonR), &todaySize));
    applyLook(kToday, today);

    QJsonObject radii{{QStringLiteral("longpath"), kLongpathRadiusPx},
                      {QStringLiteral("today"), kTodayRadiusPx},
                      {QStringLiteral("today_tabs"), kTodayTabRadiusPx},
                      {QStringLiteral("scaled_combo"), comboR},
                      {QStringLiteral("scaled_field"), fieldR},
                      {QStringLiteral("scaled_tabs"), tabR},
                      {QStringLiteral("scaled_button"), buttonR}};
    QJsonObject todaySizes{{QStringLiteral("combo"), sizeArray(todaySize.combo)},
                           {QStringLiteral("line_edit"), sizeArray(todaySize.lineEdit)},
                           {QStringLiteral("spin_box"), sizeArray(todaySize.spinBox)},
                           {QStringLiteral("tabs"), sizeArray(todaySize.tabs)},
                           {QStringLiteral("tab_bar_height"), todaySize.tabBarHeight},
                           {QStringLiteral("button"), sizeArray(todaySize.button)}};
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, {QStringLiteral("Combo, line edit, spin box, tabs, push button (logical px)")},
        QStringLiteral("Field and button sizes: Longpath's, or today's with Longpath shading"),
        QStringLiteral("Real Qt widgets on the Main Panel background %1. Today's sizes are each control's "
                       "sizeHint with today's stylesheet in today's look.")
            .arg(QLatin1String(Workspace::kPanelBg)),
        placements);
    setSheetImages(ctx, scene, sheet, placements,
                   QJsonObject{{QStringLiteral("radii"), radii}, {QStringLiteral("today_sizes"), todaySizes}});
}

// ---- decided status and data colours ------------------------------------------

namespace {

// The status sheet's zoom enlarges each cell 4x, nearest-neighbour.
constexpr int kStatusZoomFactor = 4;
constexpr double kGoodRttMs = 12;
constexpr double kMiddlingRttMs = 80;
constexpr double kPoorRttMs = 250;
constexpr double kRxMbps = 18.4;
constexpr double kTxMbps = 0.6;
constexpr quint16 kTciPreviewPort = 50001;
constexpr int kStatusCropPadPx = 4;
// Network Diagnostics' Connection rows: the field names and their values.
constexpr int kDiagnosticsCropWidthPx = 260;
// The SIGNALAVG reading the decided--data scene feeds (PreviewScenes.cpp),
// settled again after the user-colour row reconfigures the strip.
constexpr double kStripAvgDbm = -73.0;
constexpr int kStripSettleSteps = 40;
constexpr qint64 kStripClockMs = 2'000'000'000'000;
constexpr qint64 kStripStepMs = 100;
constexpr int kBadgeCellPadPx = 8;
constexpr int kBadgeGapPx = 6;
constexpr int kGraphWidth = 380;
constexpr int kGraphHeight = 220;
constexpr int kGraphRangeSeconds = 60;
constexpr int kGraphPoints = 61;
constexpr int kEqWidth = 520;
constexpr int kEqHeight = 300;
constexpr int kDataCellPadPx = 8;
// A user's own colours for the user-colour rows: a warm dark container
// background (the colour sheet's brown example), a blue bar and a blue-grey
// strip backdrop.
constexpr QRgb kUserContainerBg = 0xff2a1e14;
constexpr QRgb kUserBarColour = 0xff3c9cff;
constexpr QRgb kUserBackdrop = 0xff14283c;

constexpr auto kStatusTones = "ok -> live kLiveGreen #4caf6a, warn -> measured kAmberText #d8a55f, "
                              "danger -> tx kTxRed #c25a5c; accent, sel-hover, text-scale, "
                              "text-inactive unchanged";
constexpr auto kDataColours = "spectrum and waterfall app-bg #08080a; meters, dial, filter passband and "
                              "the Thetis bar strip backdrop take the container's background (panel "
                              "#0c0c0e, or the user's colour); graph app-bg; EQ client and plot app-bg "
                              "(the dialog's background); all ink unchanged";

QWidget* firstVisibleTopLevel(const char* className)
{
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && w->inherits(className)) {
            return w;
        }
    }
    return nullptr;
}

Shot plainShot(const QString& caption, const QImage& image)
{
    Shot s;
    s.caption = caption;
    s.image = image;
    s.focus = QRect(QPoint(0, 0), logicalSizeOf(image));
    return s;
}

Shot zoomShotBy(const Shot& s, int factor)
{
    const double dpr = s.image.devicePixelRatio();
    const QRect device = toDevice(s.focus, dpr).intersected(s.image.rect());
    const QImage cut = s.image.copy(device);
    QImage big = cut.scaled(cut.width() * factor, cut.height() * factor, Qt::IgnoreAspectRatio,
                            Qt::FastTransformation);
    big.setDevicePixelRatio(dpr);
    Shot z;
    z.caption = s.caption;
    z.image = big;
    z.source = QJsonObject{{QStringLiteral("cell_device_rect"), rectArray(device)}};
    return z;
}

// ---- status cells ----

struct TitleBarState {
    QString caption;
    ConnectionState state;
    int rttMs;
    AudioEngine::FlowState audio;
};

QVector<TitleBarState> titleBarStates()
{
    return {
        {QStringLiteral("good: 12 ms, audio healthy"), ConnectionState::Connected,
         static_cast<int>(kGoodRttMs), AudioEngine::FlowState::Healthy},
        {QStringLiteral("middling: 80 ms, audio underrun"), ConnectionState::Connected,
         static_cast<int>(kMiddlingRttMs), AudioEngine::FlowState::Underrun},
        {QStringLiteral("poor: 250 ms, audio stalled"), ConnectionState::Connected,
         static_cast<int>(kPoorRttMs), AudioEngine::FlowState::Stalled},
        {QStringLiteral("disconnected"), ConnectionState::Disconnected, -1, AudioEngine::FlowState::Dead},
    };
}

QVector<Shot> titleBarShots(ConnectionSegment* seg)
{
    QVector<Shot> shots;
    auto* bar = seg->parentWidget();
    for (const TitleBarState& st : titleBarStates()) {
        seg->setState(st.state);
        seg->setRates(kRxMbps, kTxMbps);
        // A negative sample clears the smoothing window, so the painted
        // value is this state's reading alone.
        seg->setRttMs(-1);
        if (st.rttMs >= 0) {
            seg->setRttMs(st.rttMs);
        }
        seg->setAudioFlowState(st.audio);
        seg->setPulsePausedForTest(true);
        seg->update();
        pump(kShortSettleMs);
        const QRect r = seg->geometry()
                            .adjusted(-kStatusCropPadPx, -kStatusCropPadPx, kStatusCropPadPx, kStatusCropPadPx)
                            .intersected(bar->rect());
        shots.append(plainShot(st.caption, grabImage(bar, r)));
    }
    return shots;
}

// The title bar dot's two pulse steps, held, for the states that pulse
// with a dimmer step made from the bright tone: Connected and Link lost.
QVector<Shot> pulseShots(ConnectionSegment* seg)
{
    QVector<Shot> shots;
    auto* bar = seg->parentWidget();
    const struct { QString caption; ConnectionState state; } states[] = {
        {QStringLiteral("connected"), ConnectionState::Connected},
        {QStringLiteral("link lost"), ConnectionState::LinkLost},
    };
    seg->setRttMs(-1);
    seg->setRttMs(static_cast<int>(kGoodRttMs));
    seg->setAudioFlowState(AudioEngine::FlowState::Healthy);
    for (const auto& st : states) {
        seg->setState(st.state);
        for (bool bright : {true, false}) {
            seg->setPulsePausedForTest(true, bright);
            seg->update();
            pump(kShortSettleMs);
            const QRect r = seg->geometry()
                                .adjusted(-kStatusCropPadPx, -kStatusCropPadPx, kStatusCropPadPx, kStatusCropPadPx)
                                .intersected(bar->rect());
            shots.append(plainShot(st.caption + (bright ? QStringLiteral(": bright step")
                                                        : QStringLiteral(": dimmer step")),
                                   grabImage(bar, r)));
        }
    }
    seg->setPulsePausedForTest(true);
    return shots;
}

struct BadgeSpec {
    StatusBadge::Variant variant;
    const char* label;
};

QVector<Shot> badgeShots(const QColor& statusBarBg)
{
    const BadgeSpec specs[] = {
        {StatusBadge::Variant::Info, "LSB"},
        {StatusBadge::Variant::On, "2.7k"},
        {StatusBadge::Variant::Off, "OFF"},
        {StatusBadge::Variant::Warn, "WARN"},
        {StatusBadge::Variant::Tx, "TX"},
    };
    auto cell = std::make_unique<QWidget>();
    cell->setAttribute(Qt::WA_DontShowOnScreen, true);
    cell->setAutoFillBackground(true);
    QPalette pal = cell->palette();
    pal.setColor(QPalette::Window, statusBarBg);
    cell->setPalette(pal);
    int x = kBadgeCellPadPx;
    int h = 0;
    for (const BadgeSpec& b : specs) {
        auto* badge = new StatusBadge(cell.get());
        badge->setVariant(b.variant);
        badge->setSvgIcon(QStringLiteral(":/icons/badge-mode.svg"));
        badge->setLabel(QString::fromLatin1(b.label));
        badge->adjustSize();
        badge->move(x, kBadgeCellPadPx);
        x += badge->width() + kBadgeGapPx;
        h = qMax(h, badge->height());
    }
    cell->resize(x - kBadgeGapPx + kBadgeCellPadPx, h + 2 * kBadgeCellPadPx);
    cell->show();
    pump(kSettleMs);
    return {plainShot(QStringLiteral("Info, On, Off, Warn, Tx on statusbar-bg %1").arg(statusBarBg.name()),
                      grabImage(cell.get()))};
}

QImage tciStatusCrop(QLabel* status)
{
    QWidget* group = status->parentWidget();
    // The "Status:" caption and the status word, not the page's empty width.
    const int width = qMin(group->width(), status->x() + status->sizeHint().width() + kStatusCropPadPx);
    const QRect r(0, status->y() - kStatusCropPadPx, width, status->height() + 2 * kStatusCropPadPx);
    return grabImage(group, r.intersected(group->rect()));
}

QLabel* labelWithText(QWidget* root, const QString& text)
{
    for (QLabel* l : root->findChildren<QLabel*>()) {
        if (l->text() == text && l->isVisible()) {
            return l;
        }
    }
    return nullptr;
}

QImage diagnosticsConnectionCrop(QWidget* dialog)
{
    QLabel* head = labelWithText(dialog, QObject::tr("Connection"));
    QLabel* last = labelWithText(dialog, QObject::tr("MAC"));
    if (head == nullptr || last == nullptr) {
        return {};
    }
    const QRect top(head->mapTo(dialog, QPoint(0, 0)), head->size());
    const QRect bottom(last->mapTo(dialog, QPoint(0, 0)), last->size());
    const QRect r(top.left() - kStatusCropPadPx, top.top() - kStatusCropPadPx,
                  qMin(kDiagnosticsCropWidthPx, dialog->width() - 2 * (top.left() - kStatusCropPadPx)),
                  bottom.bottom() - top.top() + 2 * kStatusCropPadPx);
    return grabImage(dialog, r.intersected(dialog->rect()));
}

// ---- data cells ----

void rerenderData(QWidget* window)
{
    for (MeterWidget* m : window->findChildren<MeterWidget*>()) {
        m->invalidateGeometry();
        for (MeterItem* item : m->items()) {
            m->invalidatePresentation(item);
        }
        m->update();
    }
    for (SpectrumWidget* s : window->findChildren<SpectrumWidget*>()) {
        s->update();
    }
    for (QWidget* w : window->findChildren<QWidget*>()) {
        if (qobject_cast<SMeterWidget*>(w) != nullptr || qobject_cast<FilterPassbandWidget*>(w) != nullptr) {
            w->update();
        }
    }
    pump(kSettleMs);
}

QRect windowRectOf(QWidget* window, QWidget* w)
{
    return w == nullptr ? QRect() : QRect(w->mapTo(window, QPoint(0, 0)), w->size());
}

QRect pansRect(QWidget* window)
{
    QRect r;
    for (SpectrumWidget* s : window->findChildren<SpectrumWidget*>()) {
        if (s->isVisible()) {
            r = r.united(windowRectOf(window, s));
        }
    }
    return r;
}

QImage cropOf(const QImage& full, const QRect& logical)
{
    const double dpr = full.devicePixelRatio();
    QImage c = full.copy(toDevice(logical, dpr));
    c.setDevicePixelRatio(dpr);
    return c;
}

QWidget* graphCell(QVector<TimeSeriesGraphWidget::Series>& out)
{
    auto* cell = new QWidget;
    cell->setAttribute(Qt::WA_DontShowOnScreen, true);
    cell->setAutoFillBackground(true);
    QPalette pal = cell->palette();
    // RemoteDiagnosticsDialog's surface (Style::kAppBg) in the look.
    pal.setColor(QPalette::Window, WorkspacePreview::roleColour(QColor(QLatin1String(Style::kAppBg)), "app-bg"));
    cell->setPalette(pal);
    auto* graph = new TimeSeriesGraphWidget(QStringLiteral("Total traffic"), QStringLiteral(" kbps"), cell);
    graph->setGeometry(kDataCellPadPx, kDataCellPadPx, kGraphWidth, kGraphHeight);
    struct Spec { const char* label; const char* colour; double base; double swing; };
    // RemoteDiagnosticsDialog's total traffic graph: its labels and colours.
    const Spec specs[] = {
        {"Core → app received", "#00b4d8", 410.0, 60.0},
        {"App → Core outgoing", "#5fff8a", 48.0, 12.0},
        {"Total", "#ffd700", 458.0, 66.0},
    };
    for (const Spec& sp : specs) {
        TimeSeriesGraphWidget::Series s;
        s.label = QString::fromUtf8(sp.label);
        s.color = QColor(QLatin1String(sp.colour));
        s.unitSuffix = QStringLiteral(" kbps");
        for (int i = 0; i < kGraphPoints; ++i) {
            const double t = static_cast<double>(i);
            s.points.append(QPointF(t, sp.base + sp.swing * std::sin(t / 6.0) * std::cos(t / 17.0)));
        }
        out.append(s);
    }
    graph->setSeries(out, kGraphRangeSeconds);
    cell->resize(kGraphWidth + 2 * kDataCellPadPx, kGraphHeight + 2 * kDataCellPadPx);
    return cell;
}

QWidget* eqCell()
{
    auto* cell = new QWidget;
    cell->setAttribute(Qt::WA_DontShowOnScreen, true);
    cell->setAutoFillBackground(true);
    QPalette pal = cell->palette();
    // TxEqDialog's surface (Style::kAppBg) in the look.
    pal.setColor(QPalette::Window, WorkspacePreview::roleColour(QColor(QLatin1String(Style::kAppBg)), "app-bg"));
    cell->setPalette(pal);
    auto* eq = new ParametricEqWidget(cell);
    eq->setEditorPresentationEnabled(true);
    eq->setGeometry(kDataCellPadPx, kDataCellPadPx, kEqWidth, kEqHeight);
    cell->resize(kEqWidth + 2 * kDataCellPadPx, kEqHeight + 2 * kDataCellPadPx);
    return cell;
}

QImage grabCell(QWidget* cell)
{
    cell->show();
    for (QWidget* w : cell->findChildren<QWidget*>()) {
        w->update();
    }
    pump(kSettleMs);
    return grabImage(cell);
}

struct DataCrops {
    QRect pans;
    QRect topStrip;
    QRect levels;
    QRect needle;
    QRect dial;
    QRect passband;
};

DataCrops dataCrops(QWidget* window)
{
    DataCrops c;
    c.pans = pansRect(window);
    auto* manager = window->findChild<ContainerManager*>();
    if (manager != nullptr) {
        c.topStrip = windowRectOf(window, manager->container(QStringLiteral("preview-meters")));
        c.levels = windowRectOf(window, manager->container(QStringLiteral("preview-levels")));
        c.needle = windowRectOf(window, manager->container(QStringLiteral("preview-needle")));
    }
    c.dial = windowRectOf(window, window->findChild<SMeterWidget*>());
    c.passband = windowRectOf(window, window->findChild<FilterPassbandWidget*>());
    return c;
}

QVector<Shot> surfaceShots(const QImage& full, const DataCrops& c)
{
    return {
        plainShot(QStringLiteral("both pans"), cropOf(full, c.pans)),
        plainShot(QStringLiteral("top strip: SIGNALAVG, Power/SWR"), cropOf(full, c.topStrip)),
        plainShot(QStringLiteral("left: SIGNALMAXBIN, COMP"), cropOf(full, c.levels)),
        plainShot(QStringLiteral("Longpath needle"), cropOf(full, c.needle)),
        plainShot(QStringLiteral("SIGNAL STRENGTH dial"), cropOf(full, c.dial)),
        plainShot(QStringLiteral("filter passband"), cropOf(full, c.passband)),
    };
}

// The first Thetis bar strip (BarPresetItem) bound to `binding` anywhere in the window (docked,
// edge strip or floating container), or null.
BarPresetItem* boundBar(QWidget* window, int binding)
{
    QList<MeterWidget*> meters = window->findChildren<MeterWidget*>();
    if (auto* manager = window->findChild<ContainerManager*>()) {
        for (const QString& id : {QStringLiteral("preview-meters"), QStringLiteral("preview-levels")}) {
            if (ContainerWidget* c = manager->container(id)) {
                meters += c->findChildren<MeterWidget*>();
            }
        }
    }
    for (MeterWidget* m : meters) {
        for (MeterItem* item : m->items()) {
            auto* bar = qobject_cast<BarPresetItem*>(item);
            if (bar != nullptr && bar->bindingId() == binding) {
                return bar;
            }
        }
    }
    return nullptr;
}

} // namespace

void sceneStatusColoursDecided(SceneContext& ctx)
{
    const QString scene = QStringLiteral("decided--status");
    ctx.provenance = QStringLiteral("proposed");
    if (app() == nullptr || ctx.window == nullptr || !WorkspacePreview::enabled()) {
        ctx.failure = scene + QStringLiteral(": needs the window with the preview switch on");
        return;
    }
    auto* titleBar = ctx.window->findChild<TitleBar*>();
    ConnectionSegment* seg = titleBar != nullptr ? titleBar->findChild<ConnectionSegment*>() : nullptr;
    if (seg == nullptr) {
        ctx.failure = scene + QStringLiteral(": no title bar connection segment");
        return;
    }

    // Setup at the TCI Server page; a TciServer that never starts says
    // "running" through its own signal (no socket is opened).
    QMetaObject::invokeMethod(ctx.window, "openSetupAtPage", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("TCI Server")));
    pump(kSettleMs);
    QWidget* setup = firstVisibleTopLevel("NereusSDR::SetupDialog");
    auto* tciPage = setup != nullptr ? setup->findChild<CatTciServerPage*>() : nullptr;
    QLabel* tciStatus = tciPage != nullptr ? tciPage->findChild<QLabel*>(QStringLiteral("tciStatusLabel")) : nullptr;
    if (tciStatus == nullptr) {
        ctx.failure = scene + QStringLiteral(": no TCI Server status label");
        return;
    }
    TciServer tci(nullptr);

    // Network Diagnostics with no radio: Status reads Disconnected.
    NetworkDiagnosticsDialog diagnostics(nullptr, nullptr);
    diagnostics.setAttribute(Qt::WA_DontShowOnScreen, true);
    diagnostics.show();
    pump(kSettleMs);

    const QColor statusBarBg = WorkspacePreview::roleColour(QColor(0x0a, 0x0a, 0x14), "statusbar-bg");
    GridRow row{QStringLiteral("decided"), QStringLiteral("Decided: Longpath's brighter tones (B)"),
                QString::fromLatin1(kStatusTones), {}};
    row.groups.append(titleBarShots(seg));
    row.groups.append(pulseShots(seg));
    row.groups.append(badgeShots(statusBarBg));

    QVector<Shot> tciShots;
    tciPage->setTciServer(&tci);
    pump(kShortSettleMs);
    tciShots.append(plainShot(QStringLiteral("stopped"), tciStatusCrop(tciStatus)));
    emit tci.serverStarted(kTciPreviewPort);
    pump(kShortSettleMs);
    tciShots.append(plainShot(QStringLiteral("running"), tciStatusCrop(tciStatus)));
    row.groups.append(tciShots);

    // The 250 ms refresh writes the status HTML again.
    pump(kSettleMs);
    row.groups.append(QVector<Shot>{
        plainShot(QStringLiteral("no radio: Disconnected"), diagnosticsConnectionCrop(&diagnostics))});
    const QVector<GridRow> rows{row};
    tciPage->setTciServer(nullptr);
    diagnostics.close();
    setup->close();

    const QStringList groups{QStringLiteral("Title bar status area (real window)"),
                             QStringLiteral("Title bar dot: the pulse's two steps"),
                             QStringLiteral("Status badges"),
                             QStringLiteral("Setup > TCI Server: Status"),
                             QStringLiteral("Network Diagnostics: Connection")};
    QJsonObject extra{{QStringLiteral("colours"), QString::fromLatin1(kStatusTones)},
                      {QStringLiteral("pulse_dimmer_step"),
                       QStringLiteral("the bright tone mixed 25 % with titlebar-mid kTitleGradMid #1c1c20")},
                      {QStringLiteral("not_shown"), QJsonArray{
                           QStringLiteral("Network Diagnostics Connected: needs a radio connection")}}};
    // The sheet is chrome around the grabs: keep the look's pass off it.
    WorkspacePreview::setEnabledForTest(false);
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, groups, QStringLiteral("Status colours: decided"),
        QStringLiteral("Real widgets with the workspace preview on. Title bar: the window's own connection "
                       "segment, pulse held at its resting step; the pulse group holds each step."),
        placements);
    setSheetImages(ctx, scene, sheet, placements, extra);

    QVector<GridRow> zoom;
    for (const GridRow& r : rows) {
        GridRow z{r.key, r.title, QString(), {}};
        for (const QVector<Shot>& group : r.groups) {
            QVector<Shot> zs;
            for (const Shot& shot : group) {
                zs.append(zoomShotBy(shot, kStatusZoomFactor));
            }
            z.groups.append(zs);
        }
        zoom.append(z);
    }
    QJsonArray zoomPlacements;
    const QImage zoomImage = assembleGrid(
        zoom, groups, QStringLiteral("Status colours, 4x nearest-neighbour"),
        QStringLiteral("Each cell of decided--status enlarged 4x."), zoomPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--zoom"), zoomImage, zoomPlacements,
                   QJsonObject{{QStringLiteral("source"), scene},
                               {QStringLiteral("enlargement"), QStringLiteral("4x nearest-neighbour")}});
    WorkspacePreview::setEnabledForTest(true);
}

void sceneDataColoursDecided(SceneContext& ctx)
{
    const QString scene = QStringLiteral("decided--data");
    ctx.provenance = QStringLiteral("proposed");
    if (app() == nullptr || ctx.window == nullptr || !WorkspacePreview::enabled()) {
        ctx.failure = scene + QStringLiteral(": needs the window with the preview switch on");
        return;
    }
    QWidget* window = ctx.window;
    const DataCrops crops = dataCrops(window);
    if (crops.pans.isEmpty() || crops.topStrip.isEmpty() || crops.levels.isEmpty() || crops.needle.isEmpty()
        || crops.dial.isEmpty() || crops.passband.isEmpty()) {
        ctx.failure = scene + QStringLiteral(": a data surface is missing from the window");
        return;
    }
    QVector<TimeSeriesGraphWidget::Series> series;
    std::unique_ptr<QWidget> graph(graphCell(series));
    std::unique_ptr<QWidget> eq(eqCell());

    QVector<GridRow> rows;
    QVector<GridRow> cropRows;
    QJsonArray rowNotes;
    auto captureRow = [&](const QString& key, const QString& title, const QString& note) {
        rerenderData(window);
        const QImage full = grabImage(window);
        const QImage graphImage = grabCell(graph.get());
        const QImage eqImage = grabCell(eq.get());
        GridRow cropRow{key, title, note, {}};
        cropRow.groups.append(surfaceShots(full, crops));
        cropRow.groups.append(QVector<Shot>{plainShot(QStringLiteral("time-series graph"), graphImage),
                                            plainShot(QStringLiteral("parametric EQ"), eqImage)});
        cropRows.append(cropRow);
        GridRow row{key, title, note, {}};
        row.groups.append(QVector<Shot>{plainShot(QStringLiteral("window"), full)});
        row.groups.append(QVector<Shot>{plainShot(QStringLiteral("time-series graph"), graphImage),
                                        plainShot(QStringLiteral("parametric EQ"), eqImage)});
        rows.append(row);
        rowNotes.append(QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("title"), title},
                                    {QStringLiteral("colours"), note}});
    };

    captureRow(QStringLiteral("decided"), QStringLiteral("Decided: neutral backgrounds, ink unchanged (B)"),
               QString::fromLatin1(kDataColours));

    // A container background the user set shows through the meters inside
    // it (Levels: SIGNALMAXBIN and COMP).
    auto* manager = window->findChild<ContainerManager*>();
    ContainerWidget* levels = manager->container(QStringLiteral("preview-levels"));
    ContainerWidget* meters = manager->container(QStringLiteral("preview-meters"));
    const QColor levelsBefore = levels->backgroundColor();
    const QColor metersBefore = meters->backgroundColor();
    const QColor userBg = QColor::fromRgba(kUserContainerBg);
    levels->setBackgroundColor(userBg);
    captureRow(QStringLiteral("user-container"), QStringLiteral("A user container background (Levels)"),
               QStringLiteral("Levels container background set to %1 by the user: the meters inside show it "
                              "in place of the panel role")
                   .arg(userBg.name()));
    levels->setBackgroundColor(levelsBefore);

    // A colour the user set on a meter item wins over both: the SIGNALAVG
    // strip's own backdrop and bar inside a Meters container with the
    // user's background.
    BarPresetItem* bar = boundBar(window, MeterBinding::SignalAvg);
    if (bar == nullptr) {
        ctx.failure = scene + QStringLiteral(": no SIGNALAVG bar item");
        return;
    }
    const QJsonObject barBefore{{QStringLiteral("barColor"), bar->configuration().value(QStringLiteral("barColor"))},
                                {QStringLiteral("backdropColor"),
                                 bar->configuration().value(QStringLiteral("backdropColor"))}};
    meters->setBackgroundColor(userBg);
    bar->applyConfiguration({{QStringLiteral("barColor"), QColor::fromRgba(kUserBarColour).name()},
                             {QStringLiteral("backdropColor"), QColor::fromRgba(kUserBackdrop).name()}});
    // Applying a configuration resets the strip's smoothing: settle the
    // scene's SIGNALAVG reading again on a hand-stepped clock past the
    // scene's own, so no real frame moves it.
    for (int i = 0; i < kStripSettleSteps; ++i) {
        bar->pushBindingValue(MeterBinding::SignalAvg, kStripAvgDbm);
        bar->advanceMeter(kStripClockMs + i * kStripStepMs);
    }
    captureRow(QStringLiteral("user-item"), QStringLiteral("User item colours inside a user container (Meters)"),
               QStringLiteral("Meters container background %1; SIGNALAVG backdrop %2 and bar %3 set by the user: "
                              "the strip keeps its own colours, Power/SWR shows the container's")
                   .arg(userBg.name(), QColor::fromRgba(kUserBackdrop).name(),
                        QColor::fromRgba(kUserBarColour).name()));
    bar->applyConfiguration(barBefore);
    meters->setBackgroundColor(metersBefore);
    rerenderData(window);

    QJsonObject cropsJson{{QStringLiteral("pans"), rectArray(crops.pans)},
                          {QStringLiteral("top_strip"), rectArray(crops.topStrip)},
                          {QStringLiteral("levels"), rectArray(crops.levels)},
                          {QStringLiteral("needle"), rectArray(crops.needle)},
                          {QStringLiteral("dial"), rectArray(crops.dial)},
                          {QStringLiteral("passband"), rectArray(crops.passband)}};
    QJsonObject extra{
        {QStringLiteral("rows"), rowNotes},
        {QStringLiteral("window_crops_logical"), cropsJson},
        {QStringLiteral("not_shown"), QJsonArray{
             QStringLiteral("a user spectrum background: NereusSDR has no such setting (Setup > Colors & "
                            "Theme sets trace, grid, text, band edge, zero line, filter and waterfall low "
                            "colours only)")}}};

    WorkspacePreview::setEnabledForTest(false);
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, {QStringLiteral("Window: pans, meter strips, Longpath needle, Main Panel"),
               QStringLiteral("Remote diagnostics graph, TX EQ editor")},
        QStringLiteral("Data colours: decided"),
        QStringLiteral("Real widgets with the workspace preview on. Graph and EQ are the real widgets on their "
                       "dialogs' surface, with fixed data."),
        placements);
    setSheetImages(ctx, scene, sheet, placements, extra);

    QJsonArray cropPlacements;
    const QImage cropSheet = assembleGrid(
        cropRows, {QStringLiteral("Window crops"), QStringLiteral("Graph and EQ")},
        QStringLiteral("Data colours: surface crops, with user colours"),
        QStringLiteral("The same grabs cut per surface; the last two rows show a user container background "
                       "and user item colours."),
        cropPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--crops"), cropSheet, cropPlacements,
                   QJsonObject{{QStringLiteral("source"), scene}, {QStringLiteral("rows"), rowNotes}});
    WorkspacePreview::setEnabledForTest(true);
}

// ---- decided controls ----------------------------------------------------------

namespace {

// One row of the decided controls sheet: its cells, built with today's
// widget stylesheets (the product's Style:: strings with the switch on),
// then the scoped pass over the column, as a shown dialog gets it.
struct DecidedCell {
    QWidget* cell;
    QWidget* control;
    QString caption;
    QRect focus;   // in cell coordinates; empty: not zoomed
};

void pressHandle(ProbeSlider* s)
{
    const QPointF p(s->handleRect().center());
    QMouseEvent press(QEvent::MouseButtonPress, p, s->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(s, &press);
}

QVector<Shot> decidedShots(const QVector<DecidedCell>& cells)
{
    QVector<Shot> shots;
    for (const DecidedCell& c : cells) {
        shots.append(cellShot(c.cell, c.control, c.caption, c.focus));
    }
    return shots;
}

} // namespace

void sceneControlsDecided(SceneContext& ctx)
{
    const QString scene = QStringLiteral("decided--controls");
    ctx.provenance = QStringLiteral("proposed");
    if (app() == nullptr || ctx.window == nullptr || !WorkspacePreview::enabled()) {
        ctx.failure = scene + QStringLiteral(": needs the window with the preview switch on");
        return;
    }
    const std::array<QString, 4> sliderStates{QStringLiteral("resting"), QStringLiteral("hover"),
                                              QStringLiteral("pressed"), QStringLiteral("disabled")};
    const std::array<QString, 4> toggleStates{QStringLiteral("off"), QStringLiteral("on"),
                                              QStringLiteral("disabled off"), QStringLiteral("disabled on")};
    Column col;

    // Sliders: the product's Style::sliderHStyle / sliderVStyle, at the
    // approved cells' sizes, on the window colour.
    std::array<ProbeSlider*, 4> hSliders{};
    QVector<DecidedCell> hCells;
    for (std::size_t i = 0; i < hSliders.size(); ++i) {
        QWidget* cell = col.cell(QSize(kHSliderWidth, kHSliderHeight));
        hSliders[i] = new ProbeSlider(Qt::Horizontal, cell);
        hSliders[i]->setRange(0, kSliderMax);
        hSliders[i]->setValue(kSliderValue);
        hSliders[i]->setStyleSheet(Style::sliderHStyle());
        col.place(hSliders[i], QSize(kHSliderWidth, kHSliderHeight));
        hCells.append({cell, hSliders[i], sliderStates[i], {}});
    }
    hSliders[3]->setEnabled(false);
    std::array<ProbeSlider*, 4> vSliders{};
    QVector<DecidedCell> vCells;
    for (std::size_t i = 0; i < vSliders.size(); ++i) {
        QWidget* cell = col.cell(QSize(kVSliderWidth, kVSliderHeight));
        vSliders[i] = new ProbeSlider(Qt::Vertical, cell);
        vSliders[i]->setRange(0, kSliderMax);
        vSliders[i]->setValue(kSliderValue);
        vSliders[i]->setStyleSheet(Style::sliderVStyle());
        col.place(vSliders[i], QSize(kVSliderWidth, kVSliderHeight));
        vCells.append({cell, vSliders[i], sliderStates[i], {}});
    }
    vSliders[3]->setEnabled(false);

    // Check boxes and radios: with Style::kCheckBoxStyle /
    // kRadioButtonStyle on the widget (applets) and with no widget sheet
    // (Setup pages, the app sheet's rules), on the Main Panel and Setup
    // page backgrounds, as the approved indicator cells.
    struct IndicatorGroup {
        QString title;
        const char* bg;
        bool widgetSheet;
    };
    const QVector<IndicatorGroup> indicatorGroups{
        {QStringLiteral("Style:: sheet, Main Panel (%1)").arg(QLatin1String(Workspace::kPanelBg)), Workspace::kPanelBg,
         true},
        {QStringLiteral("Style:: sheet, Setup page (%1)").arg(QLatin1String(Workspace::kAppBg)), Workspace::kAppBg,
         true},
        {QStringLiteral("App sheet only, Setup page (%1)").arg(QLatin1String(Workspace::kAppBg)), Workspace::kAppBg,
         false}};
    QVector<QVector<DecidedCell>> indicatorCells;
    const QSize indicatorSize(kIndicatorWidth, kIndicatorRowHeight);
    for (const IndicatorGroup& g : indicatorGroups) {
        QVector<DecidedCell> cells;
        for (int kind = 0; kind < 2; ++kind) {
            for (std::size_t i = 0; i < toggleStates.size(); ++i) {
                QWidget* cell = paintedCell(col, indicatorSize, g.bg);
                QAbstractButton* b = nullptr;
                if (kind == 0) {
                    b = new ProbeCheckBox(QStringLiteral("Check box"), cell);
                    if (g.widgetSheet) {
                        b->setStyleSheet(WorkspacePreview::lookQss(QString::fromLatin1(Style::kCheckBoxStyle)));
                    }
                } else {
                    b = new ProbeRadioButton(QStringLiteral("Radio"), cell);
                    if (g.widgetSheet) {
                        b->setStyleSheet(
                            WorkspacePreview::lookQss(QString::fromLatin1(Style::kRadioButtonStyle)));
                    }
                }
                col.place(b, indicatorSize);
                b->setChecked(i == 1 || i == 3);
                b->setEnabled(i < 2);
                cells.append({cell, b, (kind == 0 ? QStringLiteral("box ") : QStringLiteral("radio ")) + toggleStates[i],
                              {}});
            }
        }
        indicatorCells.append(cells);
    }

    // Lit buttons: the product's buttonBaseStyle plus each meaning's
    // checked style, checked, resting and hovered.
    const auto litSheet = [](Meaning m) {
        switch (m) {
        case Meaning::Green: return Style::buttonBaseStyle() + Style::greenCheckedStyle();
        case Meaning::Blue: return Style::buttonBaseStyle() + Style::blueCheckedStyle();
        default: return Style::buttonBaseStyle() + Style::amberCheckedStyle();
        }
    };
    std::array<QVector<DecidedCell>, 2> litCells;
    for (std::size_t g = 0; g < litCells.size(); ++g) {
        for (const LitLabel& l : litLabels()) {
            QWidget* cell = paintedCell(col, QSize(kLitButtonWidth, kLitButtonHeight), Workspace::kPanelBg);
            auto* b = new QPushButton(l.text, cell);
            b->setCheckable(true);
            b->setChecked(true);
            b->setStyleSheet(WorkspacePreview::lookQss(litSheet(l.meaning)));
            col.place(b, QSize(kLitButtonWidth, kLitButtonHeight));
            litCells[g].append({cell, b, l.text, {}});
        }
    }

    // Combo, line edit, spin box, tabs and push button at today's sizes
    // with today's sheets (the corner radius follows each height).
    WorkspacePreview::setEnabledForTest(false);
    const SizeSet todaySize = measureTodaySizes();
    const LookSheets today = todaySheets();
    WorkspacePreview::setEnabledForTest(true);
    QVector<DecidedCell> fieldCells;
    {
        const std::array<QSize, 5> sizes{todaySize.combo, todaySize.lineEdit, todaySize.spinBox, todaySize.tabs,
                                         todaySize.button};
        std::array<QWidget*, 5> cells{};
        for (std::size_t i = 0; i < cells.size(); ++i) {
            cells[i] = paintedCell(col, sizes[i], Workspace::kPanelBg);
        }
        SizeControls c;
        fillSizeControls(c, cells[0],
                         SizeSheets{WorkspacePreview::lookQss(today.combo), WorkspacePreview::lookQss(today.lineEdit),
                                    WorkspacePreview::lookQss(today.spinBox), WorkspacePreview::lookQss(today.tabs),
                                    Style::buttonBaseStyle()});
        const std::array<QWidget*, 5> controls{c.combo, c.lineEdit, c.spinBox, c.tabs, c.button};
        const std::array<QString, 5> names{QStringLiteral("combo"), QStringLiteral("line edit"),
                                           QStringLiteral("spin box"), QStringLiteral("tabs"),
                                           QStringLiteral("button")};
        for (std::size_t i = 0; i < controls.size(); ++i) {
            controls[i]->setParent(cells[i]);
            col.place(controls[i], sizes[i]);
            fieldCells.append({cells[i], controls[i],
                               QStringLiteral("%1 %2x%3").arg(names[i]).arg(sizes[i].width()).arg(sizes[i].height()),
                               {}});
        }
    }

    // Scroll bars with no sheet of their own (the app sheet's bar, as on
    // Setup pages and dialogs): resting and handle hovered.
    std::array<ProbeScrollBar*, 2> bars{};
    QVector<DecidedCell> barCells;
    for (std::size_t i = 0; i < bars.size(); ++i) {
        auto* bar = new ProbeScrollBar(Qt::Vertical);
        bar->ensurePolished();
        const int barWidth = bar->sizeHint().width();
        QWidget* cell = col.cell(QSize(barWidth, kScrollBarHeight));
        bar->setParent(cell);
        bar->show();
        bar->setRange(0, kSliderMax);
        bar->setPageStep(kScrollBarPage);
        bar->setValue(kScrollBarValue);
        col.place(bar, QSize(barWidth, kScrollBarHeight));
        bars[i] = bar;
        barCells.append({cell, bar,
                         QStringLiteral("%1, %2 px").arg(i == 0 ? QStringLiteral("resting") : QStringLiteral("hovered"))
                             .arg(barWidth),
                         {}});
    }

    col.finish();
    WorkspacePreview::themeSubtree(col.top());
    pump(kSettleMs);
    activate(col.top());

    // States that need the shown widget.
    for (ProbeSlider* s : {hSliders[1], hSliders[2], vSliders[1], vSliders[2]}) {
        hover(s, s->handleRect().center());
    }
    pressHandle(hSliders[2]);
    pressHandle(vSliders[2]);
    for (DecidedCell& c : litCells[1]) {
        hover(c.control, QPoint(kLitButtonWidth / 2, kLitButtonHeight / 2));
    }
    hover(bars[1], bars[1]->handleRect().center());
    pump(kShortSettleMs);

    // Zoom patches, now that the geometry is final.
    for (QVector<DecidedCell>* cells : {&hCells, &vCells}) {
        for (DecidedCell& c : *cells) {
            auto* s = static_cast<ProbeSlider*>(c.control);
            c.focus = patchAround(s, c.cell, s->handleRect());
        }
    }
    QJsonArray indicatorRects;
    for (QVector<DecidedCell>& cells : indicatorCells) {
        for (DecidedCell& c : cells) {
            QRect ind;
            if (auto* box = dynamic_cast<ProbeCheckBox*>(c.control)) {
                ind = box->indicatorRect();
            } else if (auto* radio = dynamic_cast<ProbeRadioButton*>(c.control)) {
                ind = radio->indicatorRect();
            }
            c.focus = patchAround(c.control, c.cell, ind);
            indicatorRects.append(QJsonObject{{QStringLiteral("caption"), c.caption},
                                              {QStringLiteral("indicator_rect"), rectArray(ind)}});
        }
    }
    for (auto& group : litCells) {
        for (DecidedCell& c : group) {
            c.focus = QRect(c.control->pos(), c.control->size());
        }
    }
    // The field row zooms each whole control, as the lit buttons do.
    for (DecidedCell& c : fieldCells) {
        c.focus = QRect(c.control->pos(), c.control->size());
    }
    for (DecidedCell& c : barCells) {
        const QRect h = static_cast<ProbeScrollBar*>(c.control)->handleRect();
        c.focus = patchAround(c.control, c.cell, h.adjusted(0, 0, 0, -(h.height() - kZoomSourceHeight)));
    }

    QVector<GridRow> rows;
    rows.append({QStringLiteral("slider-h"), QStringLiteral("Horizontal slider, 60 % (Style::sliderHStyle)"),
                 QStringLiteral("Longpath's slider as is: 12x16 handle, 6 px sunken groove, no filled side."),
                 {decidedShots(hCells)}});
    rows.append({QStringLiteral("slider-v"), QStringLiteral("Vertical slider, 60 % (Style::sliderVStyle)"), QString(),
                 {decidedShots(vCells)}});
    for (int g = 0; g < indicatorGroups.size(); ++g) {
        rows.append({QStringLiteral("indicators-%1").arg(g), indicatorGroups[g].title,
                     g == 0 ? QStringLiteral("Off: rim #58585e, fill #050507 to #1a1a1e. Disabled off: rim #3d3d41, "
                                             "fill #050507. Disabled on: #4e4e53. On: Longpath blue, no check mark.")
                            : QString(),
                     {decidedShots(indicatorCells[g])}});
    }
    rows.append({QStringLiteral("lit"), QStringLiteral("Lit buttons, checked (resting, then hovered)"),
                 QStringLiteral("Green raised bright, blue Longpath's glass, amber raised."),
                 {decidedShots(litCells[0]), decidedShots(litCells[1])}});
    rows.append({QStringLiteral("fields"), QStringLiteral("Combo, line edit, spin box, tabs, push button"),
                 QStringLiteral("Today's sizes; corner radius round(height x 7 / 26)."), {decidedShots(fieldCells)}});
    rows.append({QStringLiteral("scroll-bar"), QStringLiteral("Scroll bar (app sheet)"), QString(),
                 {decidedShots(barCells)}});

    QJsonObject sizes{{QStringLiteral("combo"), sizeArray(todaySize.combo)},
                      {QStringLiteral("line_edit"), sizeArray(todaySize.lineEdit)},
                      {QStringLiteral("spin_box"), sizeArray(todaySize.spinBox)},
                      {QStringLiteral("tabs"), sizeArray(todaySize.tabs)},
                      {QStringLiteral("tab_bar_height"), todaySize.tabBarHeight},
                      {QStringLiteral("button"), sizeArray(todaySize.button)}};
    QJsonObject handles{
        {QStringLiteral("slider_h"), rectArray(hSliders[0]->handleRect())},
        {QStringLiteral("slider_v"), rectArray(vSliders[0]->handleRect())},
        {QStringLiteral("scroll_bar"), rectArray(bars[0]->handleRect())}};
    QJsonObject extra{{QStringLiteral("today_sizes"), sizes},
                      {QStringLiteral("handles"), handles},
                      {QStringLiteral("indicators"), indicatorRects},
                      {QStringLiteral("path"), QStringLiteral("Style:: sheets set with the preview on, then "
                                                              "WorkspacePreview::themeSubtree on the column")}};

    // The sheet's own labels are drawn in today's look.
    WorkspacePreview::setEnabledForTest(false);
    QJsonArray placements;
    const QImage sheet = assembleGrid(
        rows, {QStringLiteral("Product, preview on"), QStringLiteral("Hovered")},
        QStringLiteral("Controls: decided"),
        QStringLiteral("Real Qt widgets with the workspace preview on, through the product path: the Style:: "
                       "stylesheets, the app sheet and the scoped pass. Cells match the approved choice cells."),
        placements);
    setSheetImages(ctx, scene, sheet, placements, extra);

    QStringList zoomGroups;
    const QVector<GridRow> zoom = zoomGrid(rows, {QStringLiteral("Product, preview on"), QStringLiteral("Hovered")},
                                           true, zoomGroups);
    QJsonArray zoomPlacements;
    const QImage zoomImage = assembleGrid(
        zoom, zoomGroups, QStringLiteral("Controls: decided, 5x nearest-neighbour"),
        QStringLiteral("Cut-outs around each handle, indicator and lit button, enlarged 5x."), zoomPlacements);
    setSheetImages(ctx, scene + QStringLiteral("--zoom"), zoomImage, zoomPlacements,
                   QJsonObject{{QStringLiteral("source"), scene},
                               {QStringLiteral("enlargement"), QStringLiteral("5x nearest-neighbour")}});
    WorkspacePreview::setEnabledForTest(true);
}

} // namespace NereusSDR::Preview
