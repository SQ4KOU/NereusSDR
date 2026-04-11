#include "ContainerSettingsDialog.h"
#include "ContainerWidget.h"
#include "../meters/MeterWidget.h"
#include "../meters/MeterItem.h"

// Core meter item types (MeterItem.h defines: BarItem, SolidColourItem, ImageItem,
//                                               ScaleItem, TextItem, NeedleItem)
// Phase 3G-4 passive item types
#include "../meters/SpacerItem.h"
#include "../meters/FadeCoverItem.h"
#include "../meters/LEDItem.h"
#include "../meters/HistoryGraphItem.h"
#include "../meters/MagicEyeItem.h"
#include "../meters/NeedleScalePwrItem.h"
#include "../meters/SignalTextItem.h"
#include "../meters/DialItem.h"
#include "../meters/TextOverlayItem.h"
#include "../meters/WebImageItem.h"
#include "../meters/FilterDisplayItem.h"
#include "../meters/RotatorItem.h"
// Phase 3G-5 interactive item types
#include "../meters/BandButtonItem.h"
#include "../meters/ModeButtonItem.h"
#include "../meters/FilterButtonItem.h"
#include "../meters/AntennaButtonItem.h"
#include "../meters/TuneStepButtonItem.h"
#include "../meters/OtherButtonItem.h"
#include "../meters/VoiceRecordPlayItem.h"
#include "../meters/DiscordButtonItem.h"
#include "../meters/VfoDisplayItem.h"
#include "../meters/ClockItem.h"
#include "../meters/ClickBoxItem.h"
#include "../meters/DataOutItem.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QColorDialog>
#include <QColor>
#include <QMap>
#include <QStringList>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Style helpers (scoped to this translation unit)
// ---------------------------------------------------------------------------
namespace {

constexpr const char* kBtnStyle =
    "QPushButton {"
    "  background: #1a2a3a;"
    "  color: #c8d8e8;"
    "  border: 1px solid #205070;"
    "  border-radius: 3px;"
    "  padding: 4px 8px;"
    "}"
    "QPushButton:hover { background: #203040; }";

constexpr const char* kOkBtnStyle =
    "QPushButton {"
    "  background: #00b4d8;"
    "  color: #0f0f1a;"
    "  border: 1px solid #00d4f8;"
    "  border-radius: 3px;"
    "  padding: 4px 8px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover { background: #00c8e8; }";

constexpr const char* kEditStyle =
    "background: #0a0a18;"
    "color: #c8d8e8;"
    "border: 1px solid #1e2e3e;"
    "border-radius: 3px;"
    "padding: 2px 4px;";

constexpr const char* kListStyle =
    "QListWidget {"
    "  background: #0a0a18;"
    "  color: #c8d8e8;"
    "  border: 1px solid #203040;"
    "}"
    "QListWidget::item:selected {"
    "  background: #00b4d8;"
    "  color: #0f0f1a;"
    "}";

constexpr const char* kLabelStyle =
    "color: #8090a0; font-size: 12px;";

constexpr const char* kSectionHeaderStyle =
    "color: #8aa8c0; font-weight: bold;";

QPushButton* makeBtn(const QString& text, QWidget* parent)
{
    QPushButton* btn = new QPushButton(text, parent);
    btn->setStyleSheet(kBtnStyle);
    btn->setAutoDefault(false);
    btn->setDefault(false);
    return btn;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ContainerSettingsDialog::ContainerSettingsDialog(ContainerWidget* container,
                                                 QWidget* parent)
    : QDialog(parent)
    , m_container(container)
{
    // Window title uses the first 8 chars of the container ID
    const QString shortId = m_container ? m_container->id().left(8) : QStringLiteral("????????");
    setWindowTitle(QStringLiteral("Container Settings \u2014 ") + shortId);

    setMinimumSize(800, 500);
    resize(900, 600);

    buildLayout();
}

ContainerSettingsDialog::~ContainerSettingsDialog()
{
    qDeleteAll(m_workingItems);
}

// ---------------------------------------------------------------------------
// Layout construction
// ---------------------------------------------------------------------------

void ContainerSettingsDialog::buildLayout()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Dark dialog background
    setStyleSheet("QDialog { background: #0f0f1a; color: #c8d8e8; }");

    // --- Container-level properties bar (top) ---
    buildContainerPropertiesSection(root);

    // --- Three-panel splitter ---
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background: #203040; width: 3px; }");

    // Left panel wrapper
    QWidget* leftWrapper = new QWidget(m_splitter);
    buildLeftPanel(leftWrapper);
    m_splitter->addWidget(leftWrapper);

    // Center panel wrapper
    QWidget* centerWrapper = new QWidget(m_splitter);
    buildCenterPanel(centerWrapper);
    m_splitter->addWidget(centerWrapper);

    // Right panel wrapper
    QWidget* rightWrapper = new QWidget(m_splitter);
    buildRightPanel(rightWrapper);
    m_splitter->addWidget(rightWrapper);

    // Set initial panel widths: 200 / stretch / 200
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({200, 460, 200});

    root->addWidget(m_splitter, 1);

    // --- Button bar (bottom) ---
    buildButtonBar();
}

void ContainerSettingsDialog::buildLeftPanel(QWidget* parent)
{
    QVBoxLayout* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    QLabel* header = new QLabel(QStringLiteral("Content Items"), parent);
    header->setStyleSheet(kSectionHeaderStyle);
    layout->addWidget(header);

    m_itemList = new QListWidget(parent);
    m_itemList->setStyleSheet(kListStyle);
    layout->addWidget(m_itemList, 1);

    connect(m_itemList, &QListWidget::currentRowChanged,
            this, &ContainerSettingsDialog::onItemSelectionChanged);

    // Action buttons row
    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->setSpacing(3);

    m_btnAdd    = makeBtn(QStringLiteral("+"),  parent);
    m_btnRemove = makeBtn(QStringLiteral("\u2212"), parent);  // − (minus sign)
    m_btnMoveUp = makeBtn(QStringLiteral("\u25b2"), parent);  // ▲
    m_btnMoveDown = makeBtn(QStringLiteral("\u25bc"), parent); // ▼

    m_btnAdd->setToolTip(QStringLiteral("Add item"));
    m_btnRemove->setToolTip(QStringLiteral("Remove selected item"));
    m_btnMoveUp->setToolTip(QStringLiteral("Move item up"));
    m_btnMoveDown->setToolTip(QStringLiteral("Move item down"));

    btnRow->addWidget(m_btnAdd);
    btnRow->addWidget(m_btnRemove);
    btnRow->addStretch();
    btnRow->addWidget(m_btnMoveUp);
    btnRow->addWidget(m_btnMoveDown);

    layout->addLayout(btnRow);

    connect(m_btnAdd,      &QPushButton::clicked, this, &ContainerSettingsDialog::onAddItem);
    connect(m_btnRemove,   &QPushButton::clicked, this, &ContainerSettingsDialog::onRemoveItem);
    connect(m_btnMoveUp,   &QPushButton::clicked, this, &ContainerSettingsDialog::onMoveItemUp);
    connect(m_btnMoveDown, &QPushButton::clicked, this, &ContainerSettingsDialog::onMoveItemDown);

    populateItemList();
}

void ContainerSettingsDialog::buildCenterPanel(QWidget* parent)
{
    QVBoxLayout* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    QLabel* header = new QLabel(QStringLiteral("Item Properties"), parent);
    header->setStyleSheet(kSectionHeaderStyle);
    layout->addWidget(header);

    m_propertyStack = new QStackedWidget(parent);
    m_propertyStack->setStyleSheet(
        "QStackedWidget { background: #0a0a18; border: 1px solid #203040; }");

    // Page 0: empty placeholder
    m_emptyPage = new QWidget(m_propertyStack);
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyPage);
    QLabel* emptyLabel = new QLabel(
        QStringLiteral("Select an item to edit properties"), m_emptyPage);
    emptyLabel->setStyleSheet(kLabelStyle);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyLabel);
    m_propertyStack->addWidget(m_emptyPage);

    layout->addWidget(m_propertyStack, 1);
}

void ContainerSettingsDialog::buildRightPanel(QWidget* parent)
{
    QVBoxLayout* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    QLabel* header = new QLabel(QStringLiteral("Live Preview"), parent);
    header->setStyleSheet(kSectionHeaderStyle);
    layout->addWidget(header);

    m_previewWidget = new MeterWidget(parent);
    m_previewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_previewWidget, 1);
}

void ContainerSettingsDialog::buildContainerPropertiesSection(QVBoxLayout* parentLayout)
{
    QFrame* bar = new QFrame(this);
    bar->setFrameShape(QFrame::StyledPanel);
    bar->setStyleSheet(
        "QFrame { background: #111122; border: 1px solid #203040; border-radius: 4px; }");

    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 4, 8, 4);
    barLayout->setSpacing(10);

    // Title
    QLabel* titleLabel = new QLabel(QStringLiteral("Title:"), bar);
    titleLabel->setStyleSheet(kLabelStyle);
    m_titleEdit = new QLineEdit(bar);
    m_titleEdit->setStyleSheet(kEditStyle);
    m_titleEdit->setPlaceholderText(QStringLiteral("Container title..."));
    m_titleEdit->setFixedWidth(140);

    // BG Color
    QLabel* bgLabel = new QLabel(QStringLiteral("BG:"), bar);
    bgLabel->setStyleSheet(kLabelStyle);
    m_bgColorBtn = new QPushButton(QStringLiteral("  "), bar);
    m_bgColorBtn->setStyleSheet(
        "QPushButton { background: #0f0f1a; border: 1px solid #205070;"
        "  border-radius: 3px; min-width: 32px; min-height: 18px; }"
        "QPushButton:hover { border-color: #00b4d8; }");
    m_bgColorBtn->setAutoDefault(false);
    m_bgColorBtn->setDefault(false);
    m_bgColorBtn->setToolTip(QStringLiteral("Choose background color"));
    connect(m_bgColorBtn, &QPushButton::clicked, this, [this]() {
        QColor initial(QStringLiteral("#0f0f1a"));
        QColor chosen = QColorDialog::getColor(initial, this,
                                               QStringLiteral("Background Color"));
        if (chosen.isValid()) {
            m_bgColorBtn->setStyleSheet(
                QStringLiteral("QPushButton { background: %1; border: 1px solid #205070;"
                                "  border-radius: 3px; min-width: 32px; min-height: 18px; }"
                                "QPushButton:hover { border-color: #00b4d8; }").arg(chosen.name()));
        }
    });

    // Border
    QLabel* borderLabel = new QLabel(QStringLiteral("Border:"), bar);
    borderLabel->setStyleSheet(kLabelStyle);
    m_borderCheck = new QCheckBox(bar);
    m_borderCheck->setStyleSheet("QCheckBox { color: #c8d8e8; }");

    // RX Source
    QLabel* rxLabel = new QLabel(QStringLiteral("RX:"), bar);
    rxLabel->setStyleSheet(kLabelStyle);
    m_rxSourceCombo = new QComboBox(bar);
    m_rxSourceCombo->addItem(QStringLiteral("RX1"), 1);
    m_rxSourceCombo->addItem(QStringLiteral("RX2"), 2);
    m_rxSourceCombo->setStyleSheet(
        "QComboBox { background: #0a0a18; color: #c8d8e8;"
        "  border: 1px solid #1e2e3e; border-radius: 3px; padding: 2px 4px; }"
        "QComboBox QAbstractItemView { background: #0a0a18; color: #c8d8e8;"
        "  border: 1px solid #205070; selection-background-color: #00b4d8; }");
    m_rxSourceCombo->setFixedWidth(64);

    // Show on RX / TX
    QLabel* showRxLabel = new QLabel(QStringLiteral("Show RX:"), bar);
    showRxLabel->setStyleSheet(kLabelStyle);
    m_showOnRxCheck = new QCheckBox(bar);
    m_showOnRxCheck->setStyleSheet("QCheckBox { color: #c8d8e8; }");

    QLabel* showTxLabel = new QLabel(QStringLiteral("Show TX:"), bar);
    showTxLabel->setStyleSheet(kLabelStyle);
    m_showOnTxCheck = new QCheckBox(bar);
    m_showOnTxCheck->setStyleSheet("QCheckBox { color: #c8d8e8; }");

    // Assemble bar
    barLayout->addWidget(titleLabel);
    barLayout->addWidget(m_titleEdit);
    barLayout->addWidget(bgLabel);
    barLayout->addWidget(m_bgColorBtn);
    barLayout->addWidget(borderLabel);
    barLayout->addWidget(m_borderCheck);
    barLayout->addWidget(rxLabel);
    barLayout->addWidget(m_rxSourceCombo);
    barLayout->addWidget(showRxLabel);
    barLayout->addWidget(m_showOnRxCheck);
    barLayout->addWidget(showTxLabel);
    barLayout->addWidget(m_showOnTxCheck);
    barLayout->addStretch();

    // Populate from container
    if (m_container) {
        m_titleEdit->setText(m_container->notes());
        m_borderCheck->setChecked(m_container->hasBorder());
        const int rxSrc = m_container->rxSource();
        const int idx = m_rxSourceCombo->findData(rxSrc);
        if (idx >= 0) {
            m_rxSourceCombo->setCurrentIndex(idx);
        }
        m_showOnRxCheck->setChecked(m_container->showOnRx());
        m_showOnTxCheck->setChecked(m_container->showOnTx());
    }

    parentLayout->addWidget(bar);
}

void ContainerSettingsDialog::buildButtonBar()
{
    QHBoxLayout* barLayout = new QHBoxLayout;
    barLayout->setSpacing(6);

    // Left cluster: Presets / Import / Export
    m_btnPreset = makeBtn(QStringLiteral("Presets\u2026"), this);
    m_btnImport = makeBtn(QStringLiteral("Import"),        this);
    m_btnExport = makeBtn(QStringLiteral("Export"),        this);

    barLayout->addWidget(m_btnPreset);
    barLayout->addWidget(m_btnImport);
    barLayout->addWidget(m_btnExport);
    barLayout->addStretch();

    // Right cluster: Apply / Cancel / OK
    m_btnApply  = makeBtn(QStringLiteral("Apply"),  this);
    m_btnCancel = makeBtn(QStringLiteral("Cancel"), this);

    m_btnOk = new QPushButton(QStringLiteral("OK"), this);
    m_btnOk->setStyleSheet(kOkBtnStyle);
    m_btnOk->setAutoDefault(false);
    m_btnOk->setDefault(false);

    barLayout->addWidget(m_btnApply);
    barLayout->addWidget(m_btnCancel);
    barLayout->addWidget(m_btnOk);

    // Wire buttons
    connect(m_btnApply,  &QPushButton::clicked, this, &ContainerSettingsDialog::applyToContainer);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnOk,     &QPushButton::clicked, this, [this]() {
        applyToContainer();
        accept();
    });

    // Add bar to the root layout
    QVBoxLayout* root = qobject_cast<QVBoxLayout*>(layout());
    if (root) {
        root->addLayout(barLayout);
    }
}

// ---------------------------------------------------------------------------
// Slots (stubs — filled in later tasks)
// ---------------------------------------------------------------------------

void ContainerSettingsDialog::onItemSelectionChanged() {}
void ContainerSettingsDialog::onAddItem()              {}
void ContainerSettingsDialog::onRemoveItem()           {}
void ContainerSettingsDialog::onMoveItemUp()           {}
void ContainerSettingsDialog::onMoveItemDown()         {}

// ---------------------------------------------------------------------------
// createItemFromSerialized — factory matching ItemGroup::deserialize() pattern
// ---------------------------------------------------------------------------

MeterItem* ContainerSettingsDialog::createItemFromSerialized(const QString& data)
{
    if (data.isEmpty()) {
        return nullptr;
    }

    const int pipeIdx = data.indexOf(QLatin1Char('|'));
    const QString typeTag = (pipeIdx >= 0) ? data.left(pipeIdx) : data;

    // Core types (defined in MeterItem.h)
    if (typeTag == QLatin1String("BAR")) {
        BarItem* item = new BarItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("SOLID")) {
        SolidColourItem* item = new SolidColourItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("IMAGE")) {
        ImageItem* item = new ImageItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("SCALE")) {
        ScaleItem* item = new ScaleItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("TEXT")) {
        TextItem* item = new TextItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("NEEDLE")) {
        NeedleItem* item = new NeedleItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    }
    // Phase 3G-4 passive types
    else if (typeTag == QLatin1String("SPACER")) {
        SpacerItem* item = new SpacerItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("FADECOVER")) {
        FadeCoverItem* item = new FadeCoverItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("LED")) {
        LEDItem* item = new LEDItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("HISTORY")) {
        HistoryGraphItem* item = new HistoryGraphItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("MAGICEYE")) {
        MagicEyeItem* item = new MagicEyeItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("NEEDLESCALEPWR")) {
        NeedleScalePwrItem* item = new NeedleScalePwrItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("SIGNALTEXT")) {
        SignalTextItem* item = new SignalTextItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("DIAL")) {
        DialItem* item = new DialItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("TEXTOVERLAY")) {
        TextOverlayItem* item = new TextOverlayItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("WEBIMAGE")) {
        WebImageItem* item = new WebImageItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("FILTERDISPLAY")) {
        FilterDisplayItem* item = new FilterDisplayItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("ROTATOR")) {
        RotatorItem* item = new RotatorItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    }
    // Phase 3G-5 interactive types
    else if (typeTag == QLatin1String("BANDBTNS")) {
        BandButtonItem* item = new BandButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("MODEBTNS")) {
        ModeButtonItem* item = new ModeButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("FILTERBTNS")) {
        FilterButtonItem* item = new FilterButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("ANTENNABTNS")) {
        AntennaButtonItem* item = new AntennaButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("TUNESTEPBTNS")) {
        TuneStepButtonItem* item = new TuneStepButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("OTHERBTNS")) {
        OtherButtonItem* item = new OtherButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("VOICERECPLAY")) {
        VoiceRecordPlayItem* item = new VoiceRecordPlayItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("DISCORDBTNS")) {
        DiscordButtonItem* item = new DiscordButtonItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("VFO")) {
        VfoDisplayItem* item = new VfoDisplayItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("CLOCK")) {
        ClockItem* item = new ClockItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("CLICKBOX")) {
        ClickBoxItem* item = new ClickBoxItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    } else if (typeTag == QLatin1String("DATAOUT")) {
        DataOutItem* item = new DataOutItem();
        if (item->deserialize(data)) { return item; }
        delete item;
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// typeTagDisplayName — human-readable label for each type tag
// ---------------------------------------------------------------------------

QString ContainerSettingsDialog::typeTagDisplayName(const QString& tag)
{
    static const QMap<QString, QString> kDisplayNames = {
        // Core types
        { QStringLiteral("BAR"),           QStringLiteral("Bar Meter") },
        { QStringLiteral("SOLID"),         QStringLiteral("Solid Colour") },
        { QStringLiteral("IMAGE"),         QStringLiteral("Image") },
        { QStringLiteral("SCALE"),         QStringLiteral("Scale") },
        { QStringLiteral("TEXT"),          QStringLiteral("Text Readout") },
        { QStringLiteral("NEEDLE"),        QStringLiteral("Needle Meter") },
        // Phase 3G-4 passive types
        { QStringLiteral("SPACER"),        QStringLiteral("Spacer") },
        { QStringLiteral("FADECOVER"),     QStringLiteral("Fade Cover") },
        { QStringLiteral("LED"),           QStringLiteral("LED Indicator") },
        { QStringLiteral("HISTORY"),       QStringLiteral("History Graph") },
        { QStringLiteral("MAGICEYE"),      QStringLiteral("Magic Eye") },
        { QStringLiteral("NEEDLESCALEPWR"),QStringLiteral("Needle Scale Power") },
        { QStringLiteral("SIGNALTEXT"),    QStringLiteral("Signal Text") },
        { QStringLiteral("DIAL"),          QStringLiteral("Dial") },
        { QStringLiteral("TEXTOVERLAY"),   QStringLiteral("Text Overlay") },
        { QStringLiteral("WEBIMAGE"),      QStringLiteral("Web Image") },
        { QStringLiteral("FILTERDISPLAY"), QStringLiteral("Filter Display") },
        { QStringLiteral("ROTATOR"),       QStringLiteral("Rotator") },
        // Phase 3G-5 interactive types
        { QStringLiteral("BANDBTNS"),      QStringLiteral("Band Buttons") },
        { QStringLiteral("MODEBTNS"),      QStringLiteral("Mode Buttons") },
        { QStringLiteral("FILTERBTNS"),    QStringLiteral("Filter Buttons") },
        { QStringLiteral("ANTENNABTNS"),   QStringLiteral("Antenna Buttons") },
        { QStringLiteral("TUNESTEPBTNS"),  QStringLiteral("Tune Step Buttons") },
        { QStringLiteral("OTHERBTNS"),     QStringLiteral("Other Buttons") },
        { QStringLiteral("VOICERECPLAY"),  QStringLiteral("Voice Rec/Play") },
        { QStringLiteral("DISCORDBTNS"),   QStringLiteral("Discord Buttons") },
        { QStringLiteral("VFO"),           QStringLiteral("VFO Display") },
        { QStringLiteral("CLOCK"),         QStringLiteral("Clock") },
        { QStringLiteral("CLICKBOX"),      QStringLiteral("Click Box") },
        { QStringLiteral("DATAOUT"),       QStringLiteral("Data Out") },
    };

    const auto it = kDisplayNames.constFind(tag);
    if (it != kDisplayNames.constEnd()) {
        return it.value();
    }
    return tag;  // fall back to raw tag if unknown
}

// ---------------------------------------------------------------------------
// refreshItemList — rebuild the QListWidget from m_workingItems
// ---------------------------------------------------------------------------

void ContainerSettingsDialog::refreshItemList()
{
    m_itemList->clear();
    for (const MeterItem* item : m_workingItems) {
        // Derive the type tag from serialize() (first pipe-delimited field)
        const QString serialized = item->serialize();
        const int pipeIdx = serialized.indexOf(QLatin1Char('|'));
        const QString typeTag = (pipeIdx >= 0) ? serialized.left(pipeIdx) : serialized;

        const QString displayName = typeTagDisplayName(typeTag);
        const int bindingId = item->bindingId();

        QString label = displayName;
        if (bindingId >= 0) {
            label += QStringLiteral(" [id:%1]").arg(bindingId);
        }
        m_itemList->addItem(label);
    }
}

// ---------------------------------------------------------------------------
// Helpers (stubs / partial — filled in later tasks)
// ---------------------------------------------------------------------------

void ContainerSettingsDialog::populateItemList()
{
    qDeleteAll(m_workingItems);
    m_workingItems.clear();

    if (!m_container) {
        return;
    }

    MeterWidget* meter = qobject_cast<MeterWidget*>(m_container->content());
    if (!meter) {
        return;
    }

    const QString serialized = meter->serializeItems();
    const QStringList lines = serialized.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        MeterItem* item = createItemFromSerialized(line);
        if (item) {
            m_workingItems.append(item);
        }
    }

    refreshItemList();
    updatePreview();
}

void ContainerSettingsDialog::updatePreview()
{
    if (!m_previewWidget) {
        return;
    }

    m_previewWidget->clearItems();

    for (const MeterItem* item : m_workingItems) {
        const QString serialized = item->serialize();
        MeterItem* clone = createItemFromSerialized(serialized);
        if (clone) {
            m_previewWidget->addItem(clone);
        }
    }

    m_previewWidget->update();
}

void ContainerSettingsDialog::applyToContainer()
{
    if (!m_container) {
        return;
    }

    m_container->setNotes(m_titleEdit->text());
    m_container->setBorder(m_borderCheck->isChecked());

    const int rxData = m_rxSourceCombo->currentData().toInt();
    m_container->setRxSource(rxData);

    m_container->setShowOnRx(m_showOnRxCheck->isChecked());
    m_container->setShowOnTx(m_showOnTxCheck->isChecked());
}

} // namespace NereusSDR
