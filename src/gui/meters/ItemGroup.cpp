#include "ItemGroup.h"
#include "MeterWidget.h"
#include "MeterPoller.h"

#include "SpacerItem.h"
#include "FadeCoverItem.h"
#include "LEDItem.h"
#include "HistoryGraphItem.h"
#include "MagicEyeItem.h"
#include "NeedleScalePwrItem.h"
#include "SignalTextItem.h"
#include "DialItem.h"
#include "TextOverlayItem.h"
#include "WebImageItem.h"
#include "FilterDisplayItem.h"
#include "RotatorItem.h"

#include <QStringList>
#include <QtAlgorithms>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ItemGroup::ItemGroup(const QString& name, QObject* parent)
    : QObject(parent)
    , m_name(name)
{
}

ItemGroup::~ItemGroup()
{
    qDeleteAll(m_items);
    m_items.clear();
}

// ---------------------------------------------------------------------------
// setRect
// ---------------------------------------------------------------------------

void ItemGroup::setRect(float x, float y, float w, float h)
{
    m_x = x;
    m_y = y;
    m_w = w;
    m_h = h;
}

// ---------------------------------------------------------------------------
// addItem / removeItem
// ---------------------------------------------------------------------------

void ItemGroup::addItem(MeterItem* item)
{
    if (!item) {
        return;
    }
    item->setParent(this);
    m_items.append(item);
}

void ItemGroup::removeItem(MeterItem* item)
{
    m_items.removeOne(item);
}

// ---------------------------------------------------------------------------
// serialize
// Format:
//   GROUP
//   name
//   x
//   y
//   w
//   h
//   itemCount
//   item1_serialized
//   item2_serialized
//   ...
// ---------------------------------------------------------------------------

QString ItemGroup::serialize() const
{
    QStringList lines;
    lines << QStringLiteral("GROUP");
    lines << m_name;
    lines << QString::number(static_cast<double>(m_x));
    lines << QString::number(static_cast<double>(m_y));
    lines << QString::number(static_cast<double>(m_w));
    lines << QString::number(static_cast<double>(m_h));
    lines << QString::number(m_items.size());
    for (const MeterItem* item : m_items) {
        lines << item->serialize();
    }
    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
// deserialize
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::deserialize(const QString& data, QObject* parent)
{
    const QStringList lines = data.split(QLatin1Char('\n'));
    if (lines.size() < 7 || lines[0] != QLatin1String("GROUP")) {
        return nullptr;
    }

    const QString name = lines[1];
    bool ok = true;
    const float x = lines[2].toFloat(&ok); if (!ok) { return nullptr; }
    const float y = lines[3].toFloat(&ok); if (!ok) { return nullptr; }
    const float w = lines[4].toFloat(&ok); if (!ok) { return nullptr; }
    const float h = lines[5].toFloat(&ok); if (!ok) { return nullptr; }
    const int count = lines[6].toInt(&ok); if (!ok) { return nullptr; }

    if (lines.size() < 7 + count) {
        return nullptr;
    }

    ItemGroup* group = new ItemGroup(name, parent);
    group->setRect(x, y, w, h);

    for (int i = 0; i < count; ++i) {
        const QString& itemData = lines[7 + i];
        // Detect type from first pipe-delimited field
        const int pipeIdx = itemData.indexOf(QLatin1Char('|'));
        const QString typeTag = (pipeIdx >= 0) ? itemData.left(pipeIdx) : itemData;

        MeterItem* item = nullptr;
        if (typeTag == QLatin1String("BAR")) {
            BarItem* bar = new BarItem();
            if (bar->deserialize(itemData)) {
                item = bar;
            } else {
                delete bar;
            }
        } else if (typeTag == QLatin1String("SOLID")) {
            SolidColourItem* solid = new SolidColourItem();
            if (solid->deserialize(itemData)) {
                item = solid;
            } else {
                delete solid;
            }
        } else if (typeTag == QLatin1String("IMAGE")) {
            ImageItem* img = new ImageItem();
            if (img->deserialize(itemData)) {
                item = img;
            } else {
                delete img;
            }
        } else if (typeTag == QLatin1String("SCALE")) {
            ScaleItem* scale = new ScaleItem();
            if (scale->deserialize(itemData)) {
                item = scale;
            } else {
                delete scale;
            }
        } else if (typeTag == QLatin1String("TEXT")) {
            TextItem* text = new TextItem();
            if (text->deserialize(itemData)) {
                item = text;
            } else {
                delete text;
            }
        } else if (typeTag == QLatin1String("NEEDLE")) {
            NeedleItem* needle = new NeedleItem();
            if (needle->deserialize(itemData)) {
                item = needle;
            } else {
                delete needle;
            }
        } else if (typeTag == QLatin1String("SPACER")) {
            SpacerItem* spacer = new SpacerItem();
            if (spacer->deserialize(itemData)) {
                item = spacer;
            } else {
                delete spacer;
            }
        } else if (typeTag == QLatin1String("FADECOVER")) {
            FadeCoverItem* fadecover = new FadeCoverItem();
            if (fadecover->deserialize(itemData)) {
                item = fadecover;
            } else {
                delete fadecover;
            }
        } else if (typeTag == QLatin1String("LED")) {
            LEDItem* led = new LEDItem();
            if (led->deserialize(itemData)) {
                item = led;
            } else {
                delete led;
            }
        } else if (typeTag == QLatin1String("HISTORY")) {
            HistoryGraphItem* history = new HistoryGraphItem();
            if (history->deserialize(itemData)) {
                item = history;
            } else {
                delete history;
            }
        } else if (typeTag == QLatin1String("MAGICEYE")) {
            MagicEyeItem* magiceye = new MagicEyeItem();
            if (magiceye->deserialize(itemData)) {
                item = magiceye;
            } else {
                delete magiceye;
            }
        } else if (typeTag == QLatin1String("NEEDLESCALEPWR")) {
            NeedleScalePwrItem* needlescalepwr = new NeedleScalePwrItem();
            if (needlescalepwr->deserialize(itemData)) {
                item = needlescalepwr;
            } else {
                delete needlescalepwr;
            }
        } else if (typeTag == QLatin1String("SIGNALTEXT")) {
            SignalTextItem* signaltext = new SignalTextItem();
            if (signaltext->deserialize(itemData)) {
                item = signaltext;
            } else {
                delete signaltext;
            }
        } else if (typeTag == QLatin1String("DIAL")) {
            DialItem* dial = new DialItem();
            if (dial->deserialize(itemData)) {
                item = dial;
            } else {
                delete dial;
            }
        } else if (typeTag == QLatin1String("TEXTOVERLAY")) {
            TextOverlayItem* textoverlay = new TextOverlayItem();
            if (textoverlay->deserialize(itemData)) {
                item = textoverlay;
            } else {
                delete textoverlay;
            }
        } else if (typeTag == QLatin1String("WEBIMAGE")) {
            WebImageItem* webimage = new WebImageItem();
            if (webimage->deserialize(itemData)) {
                item = webimage;
            } else {
                delete webimage;
            }
        } else if (typeTag == QLatin1String("FILTERDISPLAY")) {
            FilterDisplayItem* filterdisplay = new FilterDisplayItem();
            if (filterdisplay->deserialize(itemData)) {
                item = filterdisplay;
            } else {
                delete filterdisplay;
            }
        } else if (typeTag == QLatin1String("ROTATOR")) {
            RotatorItem* rotator = new RotatorItem();
            if (rotator->deserialize(itemData)) {
                item = rotator;
            } else {
                delete rotator;
            }
        }

        if (item) {
            group->addItem(item);
        }
    }

    return group;
}

// ---------------------------------------------------------------------------
// createHBarPreset
// Factory: horizontal bar meter with scale + readout (AetherSDR styling).
// Layout:
//   top 20%  — label (left) + readout (right)
//   mid 28%  — bar (0.22 top, 0.28 height)
//   bottom 46% — scale (0.52 top, 0.46 height)
// Colors from AetherSDR: cyan bar (#00b4d8), red zone (#ff4444), dark bg (#0f0f1a).
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createHBarPreset(int bindingId, double minVal, double maxVal,
                                        const QString& name, QObject* parent)
{
    ItemGroup* group = new ItemGroup(name, parent);

    // Background fill
    SolidColourItem* bg = new SolidColourItem();
    bg->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    bg->setColour(QColor(QStringLiteral("#0f0f1a")));
    bg->setZOrder(0);
    group->addItem(bg);

    // Label text (static, no binding)
    TextItem* label = new TextItem();
    label->setRect(0.02f, 0.0f, 0.5f, 0.2f);
    label->setLabel(name);
    label->setBindingId(-1);
    label->setTextColor(QColor(QStringLiteral("#8090a0")));
    label->setFontSize(10);
    label->setBold(false);
    label->setZOrder(10);
    group->addItem(label);

    // Readout text (dynamic, bound to bindingId)
    TextItem* readout = new TextItem();
    readout->setRect(0.5f, 0.0f, 0.48f, 0.2f);
    readout->setLabel(QString());
    readout->setBindingId(bindingId);
    readout->setTextColor(QColor(QStringLiteral("#c8d8e8")));
    readout->setFontSize(13);
    readout->setBold(true);
    readout->setSuffix(QStringLiteral(" dBm"));
    readout->setDecimals(1);
    readout->setZOrder(10);
    group->addItem(readout);

    // Bar meter
    BarItem* bar = new BarItem();
    bar->setRect(0.02f, 0.22f, 0.96f, 0.28f);
    bar->setOrientation(BarItem::Orientation::Horizontal);
    bar->setRange(minVal, maxVal);
    bar->setBindingId(bindingId);
    bar->setBarColor(QColor(QStringLiteral("#00b4d8")));
    bar->setBarRedColor(QColor(QStringLiteral("#ff4444")));
    bar->setRedThreshold(minVal + (maxVal - minVal) * 0.9);
    bar->setZOrder(5);
    group->addItem(bar);

    // Scale
    ScaleItem* scale = new ScaleItem();
    scale->setRect(0.02f, 0.52f, 0.96f, 0.46f);
    scale->setOrientation(ScaleItem::Orientation::Horizontal);
    scale->setRange(minVal, maxVal);
    scale->setMajorTicks(7);
    scale->setMinorTicks(5);
    scale->setTickColor(QColor(QStringLiteral("#c8d8e8")));
    scale->setLabelColor(QColor(QStringLiteral("#8090a0")));
    scale->setFontSize(9);
    scale->setZOrder(10);
    group->addItem(scale);

    return group;
}

// ---------------------------------------------------------------------------
// createCompactHBarPreset
// Compact single-line layout: label (left 20%), bar (center 50%), readout (right 28%).
// No scale ticks. From AetherSDR HGauge pattern (24px fixed height).
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createCompactHBarPreset(int bindingId, double minVal, double maxVal,
                                               const QString& name, QObject* parent)
{
    ItemGroup* group = new ItemGroup(name, parent);

    // Background fill
    SolidColourItem* bg = new SolidColourItem();
    bg->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    bg->setColour(QColor(QStringLiteral("#0f0f1a")));
    bg->setZOrder(0);
    group->addItem(bg);

    // Label text (left 15%)
    TextItem* label = new TextItem();
    label->setRect(0.02f, 0.05f, 0.15f, 0.9f);
    label->setLabel(name);
    label->setBindingId(-1);
    label->setTextColor(QColor(QStringLiteral("#8090a0")));
    label->setFontSize(8);
    label->setBold(false);
    label->setZOrder(10);
    group->addItem(label);

    // Bar meter (center 45%)
    BarItem* bar = new BarItem();
    bar->setRect(0.17f, 0.2f, 0.45f, 0.6f);
    bar->setOrientation(BarItem::Orientation::Horizontal);
    bar->setRange(minVal, maxVal);
    bar->setBindingId(bindingId);
    bar->setBarColor(QColor(QStringLiteral("#00b4d8")));
    bar->setBarRedColor(QColor(QStringLiteral("#ff4444")));
    bar->setRedThreshold(minVal + (maxVal - minVal) * 0.9);
    bar->setZOrder(5);
    group->addItem(bar);

    // Readout text (right 36%)
    TextItem* readout = new TextItem();
    readout->setRect(0.62f, 0.05f, 0.36f, 0.9f);
    readout->setBindingId(bindingId);
    readout->setTextColor(QColor(QStringLiteral("#c8d8e8")));
    readout->setFontSize(8);
    readout->setBold(true);
    readout->setSuffix(QStringLiteral(" dBm"));
    readout->setDecimals(1);
    readout->setIdleText(QStringLiteral("\u2014 dBm"));
    readout->setMinValidValue(minVal);
    readout->setZOrder(10);
    group->addItem(readout);

    return group;
}

// ---------------------------------------------------------------------------
// installInto
// Transforms each item's normalized 0-1 rect into the target rect and
// transfers ownership to the given MeterWidget.
// ---------------------------------------------------------------------------

void ItemGroup::installInto(MeterWidget* widget, float gx, float gy, float gw, float gh)
{
    for (MeterItem* item : m_items) {
        item->setRect(
            gx + item->x() * gw,
            gy + item->y() * gh,
            item->itemWidth() * gw,
            item->itemHeight() * gh
        );
        item->setParent(widget);
        widget->addItem(item);
    }
    m_items.clear();
}

// ---------------------------------------------------------------------------
// createSMeterPreset
// From AetherSDR SMeterWidget — single NeedleItem handles all rendering.
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createSMeterPreset(int bindingId, const QString& name,
                                          QObject* parent)
{
    ItemGroup* group = new ItemGroup(name, parent);

    NeedleItem* needle = new NeedleItem();
    needle->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    needle->setBindingId(bindingId);
    needle->setSourceLabel(name);
    needle->setZOrder(5);
    group->addItem(needle);

    return group;
}

// ---------------------------------------------------------------------------
// createPowerSwrPreset
// From Thetis MeterManager.cs AddPWRBar (line 23862) + AddSWRBar (line 23990)
// Power: 0-120W, HighPoint at 100W (75%). DecayRatio = 0.1
// SWR: 1:1-5:1, HighPoint at 3:1 (75%). DecayRatio = 0.1
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createPowerSwrPreset(const QString& name, QObject* parent)
{
    ItemGroup* group = new ItemGroup(name, parent);

    // Background
    SolidColourItem* bg = new SolidColourItem();
    bg->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    bg->setColour(QColor(0x0f, 0x0f, 0x1a));
    bg->setZOrder(0);
    group->addItem(bg);

    // --- Forward Power section (top half) ---

    TextItem* pwrLabel = new TextItem();
    pwrLabel->setRect(0.02f, 0.0f, 0.5f, 0.15f);
    pwrLabel->setLabel(QStringLiteral("Power"));
    pwrLabel->setBindingId(-1);
    pwrLabel->setTextColor(QColor(0x80, 0x90, 0xa0));
    pwrLabel->setFontSize(10);
    pwrLabel->setBold(false);
    pwrLabel->setZOrder(10);
    group->addItem(pwrLabel);

    TextItem* pwrReadout = new TextItem();
    pwrReadout->setRect(0.5f, 0.0f, 0.48f, 0.15f);
    pwrReadout->setBindingId(MeterBinding::TxPower);
    pwrReadout->setTextColor(QColor(0xc8, 0xd8, 0xe8));
    pwrReadout->setFontSize(13);
    pwrReadout->setBold(true);
    pwrReadout->setSuffix(QStringLiteral(" W"));
    pwrReadout->setDecimals(0);
    pwrReadout->setIdleText(QStringLiteral("\u2014 W"));
    pwrReadout->setMinValidValue(0.0);
    pwrReadout->setZOrder(10);
    group->addItem(pwrReadout);

    // From Thetis MeterManager.cs AddPWRBar: 0-120W, red at 100W
    BarItem* pwrBar = new BarItem();
    pwrBar->setRect(0.02f, 0.17f, 0.96f, 0.22f);
    pwrBar->setOrientation(BarItem::Orientation::Horizontal);
    pwrBar->setRange(0.0, 120.0);
    pwrBar->setBindingId(MeterBinding::TxPower);
    pwrBar->setBarColor(QColor(0x00, 0xb4, 0xd8));
    pwrBar->setBarRedColor(QColor(0xff, 0x44, 0x44));
    pwrBar->setRedThreshold(100.0);   // From Thetis: HighPoint = 100W
    pwrBar->setAttackRatio(0.8f);     // From Thetis MeterManager.cs
    pwrBar->setDecayRatio(0.1f);      // From Thetis: PWR DecayRatio = 0.1
    pwrBar->setZOrder(5);
    group->addItem(pwrBar);

    ScaleItem* pwrScale = new ScaleItem();
    pwrScale->setRect(0.02f, 0.40f, 0.96f, 0.12f);
    pwrScale->setOrientation(ScaleItem::Orientation::Horizontal);
    pwrScale->setRange(0.0, 120.0);
    pwrScale->setMajorTicks(7);       // 0, 20, 40, 60, 80, 100, 120
    pwrScale->setMinorTicks(3);
    pwrScale->setFontSize(8);
    pwrScale->setZOrder(10);
    group->addItem(pwrScale);

    // --- SWR section (bottom half) ---

    TextItem* swrLabel = new TextItem();
    swrLabel->setRect(0.02f, 0.55f, 0.5f, 0.12f);
    swrLabel->setLabel(QStringLiteral("SWR"));
    swrLabel->setBindingId(-1);
    swrLabel->setTextColor(QColor(0x80, 0x90, 0xa0));
    swrLabel->setFontSize(10);
    swrLabel->setBold(false);
    swrLabel->setZOrder(10);
    group->addItem(swrLabel);

    TextItem* swrReadout = new TextItem();
    swrReadout->setRect(0.5f, 0.55f, 0.48f, 0.12f);
    swrReadout->setBindingId(MeterBinding::TxSwr);
    swrReadout->setTextColor(QColor(0xc8, 0xd8, 0xe8));
    swrReadout->setFontSize(13);
    swrReadout->setBold(true);
    swrReadout->setSuffix(QStringLiteral(":1"));
    swrReadout->setDecimals(1);
    swrReadout->setIdleText(QStringLiteral("\u221E:1"));
    swrReadout->setMinValidValue(1.0);
    swrReadout->setZOrder(10);
    group->addItem(swrReadout);

    // From Thetis MeterManager.cs AddSWRBar: 1:1-5:1, red at 3:1
    BarItem* swrBar = new BarItem();
    swrBar->setRect(0.02f, 0.69f, 0.96f, 0.15f);
    swrBar->setOrientation(BarItem::Orientation::Horizontal);
    swrBar->setRange(1.0, 5.0);
    swrBar->setBindingId(MeterBinding::TxSwr);
    swrBar->setBarColor(QColor(0x00, 0xb4, 0xd8));
    swrBar->setBarRedColor(QColor(0xff, 0x44, 0x44));
    swrBar->setRedThreshold(3.0);     // From Thetis: HighPoint = SWR 3:1
    swrBar->setAttackRatio(0.8f);
    swrBar->setDecayRatio(0.1f);      // From Thetis: SWR DecayRatio = 0.1
    swrBar->setZOrder(5);
    group->addItem(swrBar);

    ScaleItem* swrScale = new ScaleItem();
    swrScale->setRect(0.02f, 0.86f, 0.96f, 0.12f);
    swrScale->setOrientation(ScaleItem::Orientation::Horizontal);
    swrScale->setRange(1.0, 5.0);
    swrScale->setMajorTicks(5);       // 1, 2, 3, 4, 5
    swrScale->setMinorTicks(1);
    swrScale->setFontSize(8);
    swrScale->setZOrder(10);
    group->addItem(swrScale);

    return group;
}

// ---------------------------------------------------------------------------
// createAlcPreset
// ALC preset: horizontal bar, -30 to 0 dB range.
// From Thetis MeterManager.cs ALC scale: 0 dB = 66.5% bar
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createAlcPreset(QObject* parent)
{
    return createCompactHBarPreset(MeterBinding::TxAlc, -30.0, 0.0,
                                   QStringLiteral("ALC"), parent);
}

// ---------------------------------------------------------------------------
// createMicPreset
// Mic level preset: horizontal bar, -30 to 0 dB range.
// From Thetis MeterManager.cs MIC scale
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createMicPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxMic, -30.0, 0.0,
                            QStringLiteral("Mic"), parent);
}

// ---------------------------------------------------------------------------
// createCompPreset
// Compressor level preset: horizontal bar, -25 to 0 dB range.
// From Thetis MeterManager.cs COMP scale
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createCompPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxComp, -25.0, 0.0,
                            QStringLiteral("Comp"), parent);
}

// ---------------------------------------------------------------------------
// Phase 3G-4 bar presets (from Thetis MeterManager.cs AddMeter factory)
// ---------------------------------------------------------------------------

ItemGroup* ItemGroup::createSignalBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::SignalPeak, -140.0, 0.0,
                            QStringLiteral("Signal"), parent);
}

ItemGroup* ItemGroup::createAvgSignalBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::SignalAvg, -140.0, 0.0,
                            QStringLiteral("Avg Signal"), parent);
}

ItemGroup* ItemGroup::createMaxBinBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::SignalMaxBin, -140.0, 0.0,
                            QStringLiteral("Max Bin"), parent);
}

ItemGroup* ItemGroup::createAdcBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::AdcAvg, -140.0, 0.0,
                            QStringLiteral("ADC"), parent);
}

ItemGroup* ItemGroup::createAdcMaxMagPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::AdcPeak, -140.0, 0.0,
                            QStringLiteral("ADC Max"), parent);
}

ItemGroup* ItemGroup::createAgcBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::AgcAvg, -20.0, 120.0,
                            QStringLiteral("AGC"), parent);
}

ItemGroup* ItemGroup::createAgcGainBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::AgcGain, -20.0, 120.0,
                            QStringLiteral("AGC Gain"), parent);
}

ItemGroup* ItemGroup::createPbsnrBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::PbSnr, 0.0, 60.0,
                            QStringLiteral("PBSNR"), parent);
}

ItemGroup* ItemGroup::createEqBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxEq, -30.0, 0.0,
                            QStringLiteral("EQ"), parent);
}

ItemGroup* ItemGroup::createLevelerBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxLeveler, -30.0, 0.0,
                            QStringLiteral("Leveler"), parent);
}

ItemGroup* ItemGroup::createLevelerGainBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxLevelerGain, 0.0, 30.0,
                            QStringLiteral("Leveler Gain"), parent);
}

ItemGroup* ItemGroup::createAlcGainBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxAlcGain, 0.0, 30.0,
                            QStringLiteral("ALC Gain"), parent);
}

ItemGroup* ItemGroup::createAlcGroupBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxAlcGroup, -30.0, 25.0,
                            QStringLiteral("ALC Group"), parent);
}

ItemGroup* ItemGroup::createCfcBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxCfc, -30.0, 0.0,
                            QStringLiteral("CFC"), parent);
}

ItemGroup* ItemGroup::createCfcGainBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::TxCfcGain, 0.0, 30.0,
                            QStringLiteral("CFC Gain"), parent);
}

ItemGroup* ItemGroup::createCustomBarPreset(int bindingId, double minVal, double maxVal,
                                             const QString& name, QObject* parent)
{
    return createHBarPreset(bindingId, minVal, maxVal, name, parent);
}

// ---------------------------------------------------------------------------
// Phase 3G-4 composite presets
// ---------------------------------------------------------------------------

// createMagicEyePreset
// Single MagicEyeItem fills the full group rect.
// From Thetis MeterManager.cs clsMagicEye usage.
ItemGroup* ItemGroup::createMagicEyePreset(int bindingId, QObject* parent)
{
    ItemGroup* group = new ItemGroup(QStringLiteral("Magic Eye"), parent);

    MagicEyeItem* eye = new MagicEyeItem();
    eye->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    eye->setBindingId(bindingId);
    eye->setZOrder(5);
    group->addItem(eye);

    return group;
}

// createSignalTextPreset
// Dark background + large SignalTextItem (S-units mode).
// From Thetis MeterManager.cs clsSignalText (line 20286+).
// Yellow text (#ffff00) from Thetis line 21708.
ItemGroup* ItemGroup::createSignalTextPreset(int bindingId, QObject* parent)
{
    ItemGroup* group = new ItemGroup(QStringLiteral("Signal Text"), parent);

    // Background
    SolidColourItem* bg = new SolidColourItem();
    bg->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    bg->setColour(QColor(0x0f, 0x0f, 0x1a));
    bg->setZOrder(0);
    group->addItem(bg);

    SignalTextItem* text = new SignalTextItem();
    text->setRect(0.02f, 0.05f, 0.96f, 0.9f);
    text->setBindingId(bindingId);
    text->setUnits(SignalTextItem::Units::SUnits);
    text->setFontSize(40.0f);
    text->setColour(QColor(0xff, 0xff, 0x00));  // Yellow (from Thetis line 21708)
    text->setZOrder(5);
    group->addItem(text);

    return group;
}

// createHistoryPreset
// Dark background + HistoryGraphItem (top 80%) + TextItem readout (bottom 20%).
// From Thetis MeterManager.cs clsHistoryItem (line 16149+).
ItemGroup* ItemGroup::createHistoryPreset(int bindingId, QObject* parent)
{
    ItemGroup* group = new ItemGroup(QStringLiteral("History"), parent);

    // Background
    SolidColourItem* bg = new SolidColourItem();
    bg->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    bg->setColour(QColor(0x0a, 0x0a, 0x18));
    bg->setZOrder(0);
    group->addItem(bg);

    // History graph (top 80%)
    HistoryGraphItem* hist = new HistoryGraphItem();
    hist->setRect(0.0f, 0.0f, 1.0f, 0.8f);
    hist->setBindingId(bindingId);
    hist->setCapacity(300);
    hist->setZOrder(5);
    group->addItem(hist);

    // Text readout (bottom 20%)
    TextItem* readout = new TextItem();
    readout->setRect(0.02f, 0.8f, 0.96f, 0.2f);
    readout->setBindingId(bindingId);
    readout->setTextColor(QColor(0xc8, 0xd8, 0xe8));
    readout->setFontSize(13);
    readout->setBold(true);
    readout->setSuffix(QStringLiteral(" dBm"));
    readout->setDecimals(1);
    readout->setZOrder(10);
    group->addItem(readout);

    return group;
}

// createSpacerPreset
// Invisible spacer that occupies the full group rect.
ItemGroup* ItemGroup::createSpacerPreset(QObject* parent)
{
    ItemGroup* group = new ItemGroup(QStringLiteral("Spacer"), parent);

    SpacerItem* spacer = new SpacerItem();
    spacer->setRect(0.0f, 0.0f, 1.0f, 1.0f);
    spacer->setZOrder(0);
    group->addItem(spacer);

    return group;
}

} // namespace NereusSDR
