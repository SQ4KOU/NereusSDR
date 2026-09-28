// no-port-check: NereusSDR-original Setup description versus desktop widgets.
#include <QtTest>

#include "core/setup/SetupDescriptionService.h"
#include "core/AppSettings.h"
#include "core/ControlRanges.h"
#include "core/TxAnalyzer.h"
#include "core/StepAttenuatorController.h"
#include "core/settings/SettingsScope.h"
#include "gui/setup/GeneralOptionsPage.h"
#include "gui/setup/AppearanceSetupPages.h"
#include "gui/ColorSwatchButton.h"
#include "gui/setup/GeneralSetupPages.h"
#include "gui/setup/CatNetworkSetupPages.h"
#include "gui/setup/DspSetupPages.h"
#include "gui/setup/DspOptionsPage.h"
#include "gui/setup/DisplaySetupPages.h"
#include "gui/setup/MultimeterPage.h"
#include "gui/setup/TransmitSetupPages.h"
#include "gui/setup/TxProfileSetupPage.h"
#include "gui/setup/hardware/AntennaAlexAntennaControlTab.h"
#include "gui/setup/PaSetupPages.h"
#include "gui/widgets/MetricLabel.h"
#include "gui/setup/TestTwoTonePage.h"
#include "gui/diagnostics/DiagnosticsPhaseHPages.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/NotchModel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QSlider>
#include <QTableWidget>

#include <utility>

using namespace NereusSDR;

namespace {
QJsonArray controls(const QJsonObject& category)
{
    QJsonArray result;
    for (const QJsonValue& page : category.value("pages").toArray()) {
        for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
            for (const QJsonValue& control : section.toObject().value("controls").toArray()) {
                result.append(control);
            }
        }
    }
    return result;
}

QObject* bySetupId(QWidget& page, const QString& id)
{
    for (QObject* object : page.findChildren<QObject*>()) {
        if (object->property("nereusSetupId").toString() == id) { return object; }
    }
    return nullptr;
}

void compareControl(QWidget& page, const QJsonObject& control)
{
    const QString id = control.value("id").toString();
    QObject* object = bySetupId(page, id);
    QVERIFY2(object != nullptr, qPrintable(id + " has no desktop widget"));
    const QString kind = control.value("kind").toString();
    if (kind == "toggle" || kind == "button") {
        auto* button = qobject_cast<QAbstractButton*>(object);
        QVERIFY2(button != nullptr, qPrintable(id));
        QCOMPARE(button->text(), control.value("label").toString());
    } else if (kind == "integer") {
        auto* spin = qobject_cast<QSpinBox*>(object);
        QVERIFY2(spin != nullptr, qPrintable(id));
        QCOMPARE(spin->minimum(), control.value("min").toInt());
        QCOMPARE(spin->maximum(), control.value("max").toInt());
        QCOMPARE(spin->singleStep(), control.value("step").toInt());
    } else if (kind == "decimal" || kind == "slider") {
        if (kind == "slider" && control.contains("options")) {
            auto* slider = qobject_cast<QSlider*>(object);
            QVERIFY2(slider != nullptr, qPrintable(id));
            const QJsonArray options = control.value("options").toArray();
            QCOMPARE(slider->minimum(), 0);
            QCOMPARE(slider->maximum(), options.size() - 1);
            QCOMPARE(slider->singleStep(), 1);
            for (int i = 0; i < options.size(); ++i) {
                const int value = 4096 << i;
                QCOMPARE(options.at(i).toObject().value("value"), QJsonValue(value));
                QCOMPARE(options.at(i).toObject().value("label"), QJsonValue(QString::number(value)));
            }
        } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(object)) {
            QCOMPARE(spin->minimum(), control.value("min").toDouble());
            QCOMPARE(spin->maximum(), control.value("max").toDouble());
            QCOMPARE(spin->singleStep(), control.value("step").toDouble());
        } else if (auto* spin = qobject_cast<QSpinBox*>(object)) {
            QCOMPARE(double(spin->minimum()), control.value("min").toDouble());
            QCOMPARE(double(spin->maximum()), control.value("max").toDouble());
            QCOMPARE(double(spin->singleStep()), control.value("step").toDouble());
        } else {
            auto* slider = qobject_cast<QSlider*>(object);
            QVERIFY2(slider != nullptr, qPrintable(id));
            const double scale = slider->property("nereusSetupScale").isValid()
                ? slider->property("nereusSetupScale").toDouble() : 1.0;
            QCOMPARE(slider->minimum() / scale, control.value("min").toDouble());
            QCOMPARE(slider->maximum() / scale, control.value("max").toDouble());
            QCOMPARE(slider->singleStep() / scale, control.value("step").toDouble());
        }
    } else if (kind == "choice") {
        if (auto* combo = qobject_cast<QComboBox*>(object)) {
            const QJsonArray choices = control.value("choices").toArray();
            QCOMPARE(combo->count(), choices.size());
            for (int i = 0; i < choices.size(); ++i) {
                QCOMPARE(combo->itemText(i), choices.at(i).toString());
            }
        } else {
            auto* group = qobject_cast<QButtonGroup*>(object);
            QVERIFY2(group != nullptr, qPrintable(id));
            const QJsonArray choices = control.value("choices").toArray();
            QCOMPARE(group->buttons().size(), choices.size());
            for (int i = 0; i < choices.size(); ++i) {
                auto* button = group->button(i);
                QVERIFY(button != nullptr);
                QCOMPARE(button->text(), choices.at(i).toString());
            }
        }
    } else if (kind == "table") {
        auto* table = qobject_cast<QTableWidget*>(object);
        QVERIFY2(table != nullptr, qPrintable(id));
        auto* group = qobject_cast<QGroupBox*>(table->parentWidget());
        QVERIFY(group != nullptr);
        QCOMPARE(group->title(), control.value("label").toString());
        QCOMPARE(table->rowCount(), 1);
        const QJsonArray columns = control.value("columns").toArray();
        QCOMPARE(table->columnCount(), columns.size());
        for (int col = 0; col < columns.size(); ++col) {
            const QJsonObject column = columns.at(col).toObject();
            QWidget* cell = table->cellWidget(0, col);
            QVERIFY(cell != nullptr);
            QCOMPARE(cell->property("nereusSetupId").toString(), column.value("id").toString());
            QCOMPARE(cell->toolTip(), column.value("tooltip").toString());
            const QString header = col == 3 ? QString() : column.value("label").toString();
            QCOMPARE(table->horizontalHeaderItem(col)->text(), header);
            if (col == 0 || col == 1) {
                auto* spin = qobject_cast<QDoubleSpinBox*>(cell);
                QVERIFY(spin != nullptr);
                QCOMPARE(spin->minimum(), column.value("min").toDouble());
                QCOMPARE(spin->maximum(), column.value("max").toDouble());
                QCOMPARE(spin->singleStep(), column.value("step").toDouble());
            } else if (col == 2) {
                QVERIFY(qobject_cast<QCheckBox*>(cell) != nullptr);
            } else {
                auto* button = qobject_cast<QAbstractButton*>(cell);
                QVERIFY(button != nullptr);
                QCOMPARE(button->text(), column.value("label").toString());
            }
        }
    } else if (kind == "text") {
        QVERIFY2(qobject_cast<QLineEdit*>(object) != nullptr, qPrintable(id));
    } else if (kind == "colour") {
        auto* swatch = qobject_cast<ColorSwatchButton*>(object);
        QVERIFY2(swatch != nullptr, qPrintable(id));
        QCOMPARE(ColorSwatchButton::colorToHex(swatch->color()).toUpper(),
                 control.value("default").toString());
    }
    if (auto* widget = qobject_cast<QWidget*>(object)) {
        QCOMPARE(widget->toolTip(), control.value("tooltip").toString());
        if (control.value("availability").toObject().value("enabled") == QJsonValue(false)) {
            QVERIFY(!widget->isEnabled());
        }
    }
    const QString key = control.value("binding").toObject().value("setting").toString();
    if (!key.isEmpty()) {
        QCOMPARE(classifySettingsKey(key), SettingsScope::Station);
    }
}
}

class SetupDescriptionParityTest : public QObject {
    Q_OBJECT
private slots:
    void describedDisplayScalarsMatchDesktopAndStayBoardIndependent()
    {
        RadioModel model;
        SpectrumDefaultsPage spectrum(&model);
        MultimeterPage multimeter(&model);
        TxDisplayPage tx(&model);
        SetupDescriptionService service;
        const QJsonObject display = service.category(QStringLiteral("display"));
        const QJsonArray pages = display.value("pages").toArray();
        QCOMPARE(pages.size(), 3);
        const QStringList expectedIds{
            "display.spectrumDefaults.fftSize", "display.spectrumDefaults.window",
            "display.spectrumDefaults.hzPerBinTarget", "display.spectrumDefaults.fps",
            "display.multimeter.pollingDelay", "display.txDisplay.fftSize",
            "display.txDisplay.window", "display.txDisplay.panDetector",
            "display.txDisplay.panAveraging", "display.txDisplay.panAvTime",
            "display.txDisplay.panNormalize", "display.txDisplay.wfDetector",
            "display.txDisplay.wfAveraging", "display.txDisplay.wfAvTime"};
        const QList<QJsonValue> expectedDefaults{
            ControlRanges::kDisplayFftSizeDefault, ControlRanges::kDisplayFftWindowDefault,
            ControlRanges::kDisplayHzPerBinTargetDefault, ControlRanges::kDisplaySpectrumFpsDefault,
            100, TxAnalyzer::kDefaultFftSize, TxAnalyzer::kDefaultWindowType,
            TxAnalyzer::kDefaultPanDetector, TxAnalyzer::kDefaultPanAveraging,
            TxAnalyzer::kDefaultPanAvTimeMs, TxAnalyzer::kDefaultPanNormalize,
            TxAnalyzer::kDefaultWfDetector, TxAnalyzer::kDefaultWfAveraging,
            TxAnalyzer::kDefaultWfAvTimeMs};
        QStringList actualIds;
        QCOMPARE(pages.at(0).toObject().value("sections").toArray().at(0).toObject().value("title"),
                 QJsonValue("Fast Fourier Transform"));
        QCOMPARE(pages.at(0).toObject().value("sections").toArray().at(1).toObject().value("title"),
                 QJsonValue("Rendering"));
        QCOMPARE(pages.at(1).toObject().value("sections").toArray().at(0).toObject().value("title"),
                 QJsonValue("Multimeter"));
        QCOMPARE(pages.at(2).toObject().value("sections").toArray().at(1).toObject().value("title"),
                 QJsonValue("Panadapter"));
        QCOMPARE(pages.at(2).toObject().value("sections").toArray().at(2).toObject().value("title"),
                 QJsonValue("Waterfall"));
        for (int p = 0; p < pages.size(); ++p) {
            QWidget* native = p == 0 ? static_cast<QWidget*>(&spectrum)
                : p == 1 ? static_cast<QWidget*>(&multimeter) : static_cast<QWidget*>(&tx);
            for (const QJsonValue& section : pages.at(p).toObject().value("sections").toArray()) {
                for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                    const QJsonObject control = raw.toObject();
                    QCOMPARE(control.value("default"), expectedDefaults.at(actualIds.size()));
                    actualIds << control.value("id").toString();
                    QVERIFY(SetupDescriptionService::validateDisplaySettingBinding(control));
                    compareControl(*native, control);
                }
            }
        }
        QCOMPARE(actualIds, expectedIds);
        model.setBoardForTest(HPSDRHW::HermesLite);
        service.setRadioContext(model.boardCapabilities(), model.hardwareProfile().model);
        QCOMPARE(service.category(QStringLiteral("display")), display);
    }
    void describedAppearanceColoursMatchNativePage()
    {
        RadioModel model;
        ColorsThemePage page(&model);
        SetupDescriptionService service;
        const QJsonObject appearance = service.category(QStringLiteral("appearance"));
        QCOMPARE(appearance.value("category").toObject().value("where"), QJsonValue("phone"));
        QCOMPARE(appearance.value("category").toObject().value("coverage"), QJsonValue("partial"));
        const QJsonArray pages = appearance.value("pages").toArray();
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().toObject().value("title"), QJsonValue("Colors & Theme"));
        QCOMPARE(pages.first().toObject().value("where"), QJsonValue("phone"));
        QCOMPARE(pages.first().toObject().value("coverage"), QJsonValue("partial"));
        const QJsonArray sections = pages.first().toObject().value("sections").toArray();
        QCOMPARE(sections.size(), 1);
        QCOMPARE(sections.first().toObject().value("title"), QJsonValue("Spectrum"));
        const QJsonArray described = sections.first().toObject().value("controls").toArray();
        QCOMPARE(described.size(), 10);
        const QStringList expectedKeys{
            "DisplayFillColor", "DisplayGridColor", "DisplayGridFineColor",
            "DisplayHGridColor", "DisplayGridTextColor", "DisplayBandEdgeColor",
            "DisplayRxZeroLineColor", "DisplayTxZeroLineColor", "DisplayRxFilterColor",
            "DisplayTxFilterColor"};
        QStringList actualKeys;
        for (const QJsonValue& raw : described) {
            const QJsonObject control = raw.toObject();
            actualKeys << control.value("binding").toObject().value("phone").toString();
            QVERIFY(SetupDescriptionService::validateAppearanceColourBinding(control));
            compareControl(page, control);
            auto* swatch = qobject_cast<ColorSwatchButton*>(bySetupId(page,
                control.value("id").toString()));
            QVERIFY(swatch != nullptr);
            auto* form = qobject_cast<QFormLayout*>(swatch->parentWidget()->layout());
            QVERIFY(form != nullptr);
            auto* label = qobject_cast<QLabel*>(form->labelForField(swatch));
            QVERIFY(label != nullptr);
            QCOMPARE(label->text(), control.value("label").toString());
        }
        QCOMPARE(actualKeys, expectedKeys);
        QVERIFY(bySetupId(page, QStringLiteral("appearance.colorsTheme.waterfallLowColor")) == nullptr);
        model.setBoardForTest(HPSDRHW::HermesLite);
        service.setRadioContext(model.boardCapabilities(), model.hardwareProfile().model);
        QCOMPARE(service.category(QStringLiteral("appearance")), appearance);
    }
    void settingsValidationPanelMatchesDesktopActions()
    {
        RadioModel model;
        SettingsValidationPage page(&model);
        SetupDescriptionService service;
        const QJsonObject diagnostics = service.category(QStringLiteral("diagnostics"));
        const QJsonObject panel = diagnostics.value("pages").toArray().first().toObject()
            .value("sections").toArray().first().toObject()
            .value("controls").toArray().first().toObject();
        QVERIFY(SetupDescriptionService::validateSettingsHygienePanel(panel));
        const QJsonArray actions = panel.value("actions").toArray();
        QCOMPARE(actions.size(), 3);
        const auto buttons = page.findChildren<QPushButton*>();
        QCOMPARE(buttons.size(), 3);
        for (int i = 0; i < actions.size(); ++i) {
            QCOMPARE(buttons.at(i)->text(), actions.at(i).toObject().value("label").toString());
        }
        QCOMPARE(actions.at(2).toObject().value("confirmation").toObject()
                     .value("message"), QJsonValue("Forget all settings for this radio?"));
        QVERIFY(!actions.at(1).toObject().value("enabled").toBool(true));
    }

    void describedPaBypassMatchesG2eDesktopOnly()
    {
        RadioModel g2e;
        g2e.setHpsdrModelForTest(HPSDRModel::ANAN_G2E);
        PaGainByBandPage page(&g2e);
        page.applyCapabilityVisibility(g2e.boardCapabilities());
        SetupDescriptionService service;
        service.setRadioContext(g2e.boardCapabilities(), g2e.hardwareProfile().model);
        const QJsonArray pages = service.category(QStringLiteral("pa")).value("pages").toArray();
        QCOMPARE(pages.size(), 2);
        const QJsonObject gain = pages.first().toObject();
        QCOMPARE(gain.value("id"), QJsonValue("pa.gain"));
        QCOMPARE(gain.value("coverage"), QJsonValue("partial"));
        const QJsonArray described = gain.value("sections").toArray().first().toObject()
            .value("controls").toArray();
        QCOMPARE(described.size(), 1);
        const QJsonObject control = described.first().toObject();
        auto* check = page.bypassPaSettingsCheckForTest();
        QVERIFY(check != nullptr);
        QCOMPARE(check->property("nereusSetupId").toString(), control.value("id").toString());
        QCOMPARE(check->text(), control.value("label").toString());
        QCOMPARE(check->toolTip(), control.value("tooltip").toString());
        QVERIFY(!check->isHidden());
        QVERIFY(SetupDescriptionService::validatePaBypassBinding(control));
        QCOMPARE(control.value("gate").toObject().value("offAir"), QJsonValue(true));
        QVERIFY(!control.value("gate").toObject().contains("transmit"));

        RadioModel other;
        other.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        PaGainByBandPage otherPage(&other);
        otherPage.applyCapabilityVisibility(other.boardCapabilities());
        service.setRadioContext(other.boardCapabilities(), other.hardwareProfile().model);
        QCOMPARE(service.category(QStringLiteral("pa")).value("pages").toArray().size(), 1);
        QVERIFY(otherPage.bypassPaSettingsCheckForTest()->isHidden());
    }

    void describedPaReadoutsMatchDesktopRows()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        PaValuesPage page(&model);
        SetupDescriptionService service;
        service.setRadioContext(model.boardCapabilities(), model.hardwareProfile().model);
        const QJsonObject pa = service.category(QStringLiteral("pa"));
        QVERIFY(!pa.isEmpty());
        const QJsonArray sections = pa.value("pages").toArray().first().toObject()
            .value("sections").toArray();
        QCOMPARE(sections.size(), 3);
        const QStringList expectedIds{QStringLiteral("pa.values.forwardCalibrated"),
                                      QStringLiteral("pa.values.forwardRawPower"),
                                      QStringLiteral("pa.values.reflectedPower"),
                                      QStringLiteral("pa.values.swr"),
                                      QStringLiteral("pa.values.drive"),
                                      QStringLiteral("pa.values.paCurrent"),
                                      QStringLiteral("pa.values.dcVoltage"),
                                      QStringLiteral("pa.values.forwardVoltage"),
                                      QStringLiteral("pa.values.reflectedVoltage"),
                                      QStringLiteral("pa.values.forwardAdc"),
                                      QStringLiteral("pa.values.reflectedAdc")};
        QStringList actualIds;
        for (const QJsonValue& rawSection : sections) {
            const QJsonObject section = rawSection.toObject();
            const QJsonArray described = section.value("controls").toArray();
            for (const QJsonValue& rawControl : described) {
                const QJsonObject control = rawControl.toObject();
                const QString id = control.value("id").toString();
                actualIds.append(id);
                auto* widget = qobject_cast<MetricLabel*>(bySetupId(page, id));
                QVERIFY2(widget != nullptr, qPrintable(id));
                QVERIFY(id == QLatin1String("pa.values.drive")
                            ? SetupDescriptionService::validatePaDriveReadoutBinding(control)
                            : (id == QLatin1String("pa.values.paCurrent")
                               || id == QLatin1String("pa.values.dcVoltage"))
                                ? SetupDescriptionService::validatePaTelemetryReadoutBinding(control)
                                : SetupDescriptionService::validatePaReadoutBinding(control));
                QCOMPARE(widget->toolTip(), control.value("tooltip").toString());
                auto* group = qobject_cast<QGroupBox*>(widget->parentWidget());
                QVERIFY(group != nullptr);
                QCOMPARE(group->title(), section.value("title").toString());
                auto* form = qobject_cast<QFormLayout*>(group->layout());
                QVERIFY(form != nullptr);
                auto* label = qobject_cast<QLabel*>(form->labelForField(widget));
                QVERIFY(label != nullptr);
                QCOMPARE(label->text(), control.value("label").toString());
            }
        }
        QCOMPARE(actualIds, expectedIds);
        const QJsonArray power = sections.first().toObject().value("controls").toArray();
        const QJsonArray raw = sections.last().toObject().value("controls").toArray();
        QCOMPARE(power.size(), 5);
        QCOMPARE(raw.size(), 2);
        model.transmitModel().setPower(37);
        QCOMPARE(page.driveTextForTest(), QStringLiteral("37 W"));
    }

    void describedHardwareAntennaScalarsMatchDesktop_data()
    {
        QTest::addColumn<int>("model");
        QTest::addColumn<bool>("rxOut");
        QTest::addColumn<bool>("ext1");
        QTest::addColumn<bool>("ext2");
        QTest::addColumn<bool>("overrideRelay");
        QTest::newRow("HPSDR") << int(HPSDRModel::HPSDR) << true << true << true << true;
        QTest::newRow("Hermes") << int(HPSDRModel::HERMES) << true << true << true << false;
        QTest::newRow("ANAN-10") << int(HPSDRModel::ANAN10) << false << false << false << false;
        QTest::newRow("ANAN-10E") << int(HPSDRModel::ANAN10E) << false << false << false << false;
        QTest::newRow("ANAN-100") << int(HPSDRModel::ANAN100) << true << true << true << true;
        QTest::newRow("ANAN-100B") << int(HPSDRModel::ANAN100B) << true << true << true << true;
        QTest::newRow("ANAN-100D") << int(HPSDRModel::ANAN100D) << true << true << true << true;
        QTest::newRow("ANAN-200D") << int(HPSDRModel::ANAN200D) << true << true << true << true;
        QTest::newRow("Orion MKII") << int(HPSDRModel::ORIONMKII) << true << true << true << true;
        QTest::newRow("ANAN-7000D") << int(HPSDRModel::ANAN7000D) << false << true << true << false;
        QTest::newRow("ANAN-8000D") << int(HPSDRModel::ANAN8000D) << false << false << false << false;
        QTest::newRow("ANAN-G2") << int(HPSDRModel::ANAN_G2) << false << true << true << false;
        QTest::newRow("ANAN-G2-1K") << int(HPSDRModel::ANAN_G2_1K) << false << false << false << false;
        QTest::newRow("Anvelina Pro 3") << int(HPSDRModel::ANVELINAPRO3) << false << true << true << false;
        QTest::newRow("Hermes Lite") << int(HPSDRModel::HERMESLITE) << false << false << false << false;
        QTest::newRow("Red Pitaya") << int(HPSDRModel::REDPITAYA) << false << true << true << false;
        QTest::newRow("ANAN-G2E") << int(HPSDRModel::ANAN_G2E) << false << true << true << false;
    }

    void describedHardwareAntennaScalarsMatchDesktop()
    {
        QFETCH(int, model);
        QFETCH(bool, rxOut);
        QFETCH(bool, ext1);
        QFETCH(bool, ext2);
        QFETCH(bool, overrideRelay);
        const auto sku = static_cast<HPSDRModel>(model);
        RadioModel radio;
        radio.setHpsdrModelForTest(sku);
        AntennaAlexAntennaControlTab page(&radio);
        SetupDescriptionService service;
        service.setRadioContext(radio.boardCapabilities(), sku);
        const QJsonObject hardware = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.hardware(), 1).toUtf8())
            .object();
        if (!radio.boardCapabilities().hasAlexFilters) {
            QVERIFY(hardware.isEmpty());
            return;
        }
        QVERIFY(!hardware.isEmpty());
        const QJsonArray described = controls(hardware);
        QCOMPARE(described.size(), 3 + int(rxOut) + int(ext1) + int(ext2) + int(overrideRelay));
        QStringList expected{QStringLiteral("hardware.antennaAlex.blockTxAnt2"),
                             QStringLiteral("hardware.antennaAlex.blockTxAnt3")};
        if (rxOut) { expected << QStringLiteral("hardware.antennaAlex.rxOutOnTx"); }
        if (ext1) { expected << QStringLiteral("hardware.antennaAlex.ext1OutOnTx"); }
        if (ext2) { expected << QStringLiteral("hardware.antennaAlex.ext2OutOnTx"); }
        if (overrideRelay) { expected << QStringLiteral("hardware.antennaAlex.rxOutOverride"); }
        expected << QStringLiteral("hardware.antennaAlex.useTxAntennaForRx");
        QStringList actual;
        for (const QJsonValue& raw : described) {
            actual << raw.toObject().value(QStringLiteral("id")).toString();
            compareControl(page, raw.toObject());
            QVERIFY(SetupDescriptionService::validateHardwarePropertyBinding(raw.toObject(), sku));
        }
        QCOMPARE(actual, expected);
        for (const auto& [name, shown] : {
                 std::pair{"rxOutOnTx", rxOut}, std::pair{"ext1OutOnTx", ext1},
                 std::pair{"ext2OutOnTx", ext2}, std::pair{"rxOutOverride", overrideRelay}}) {
            const auto* widget = qobject_cast<QWidget*>(bySetupId(
                page, QStringLiteral("hardware.antennaAlex.") + QString::fromLatin1(name)));
            QVERIFY(widget != nullptr);
            QCOMPARE(!widget->isHidden(), shown);
        }
        if (sku == HPSDRModel::ANAN_G2E) {
            const QJsonObject ext2Control = described.at(3).toObject();
            QCOMPARE(ext2Control.value("label"), QJsonValue("Rx BYPASS on Tx"));
            QCOMPARE(ext2Control.value("tooltip"),
                     QJsonValue("Enable RX 1 IN on Alex or Ext 2 on ANAN during transmit."));
        }
    }

    void describedAntennaRowsMatchNativeGrid_data()
    {
        QTest::addColumn<int>("model");
        QTest::newRow("Hermes") << int(HPSDRModel::HERMES);
        QTest::newRow("ANAN-100") << int(HPSDRModel::ANAN100);
        QTest::newRow("ANAN-G2") << int(HPSDRModel::ANAN_G2);
        QTest::newRow("HL2-no-Alex") << int(HPSDRModel::HERMESLITE);
    }

    void describedAntennaRowsMatchNativeGrid()
    {
        QFETCH(int, model);
        const auto sku = static_cast<HPSDRModel>(model);
        RadioModel radio;
        radio.setHpsdrModelForTest(sku);
        AntennaAlexAntennaControlTab page(&radio);
        SetupDescriptionService service;
        service.setRadioContext(radio.boardCapabilities(), sku);
        const QJsonObject hardware = service.category(QStringLiteral("hardware"));
        if (!radio.boardCapabilities().hasAlexFilters) {
            QVERIFY(hardware.isEmpty());
            return;
        }
        QCOMPARE(hardware.value("version"), QJsonValue(6));
        const QJsonArray described = controls(hardware);
        const QJsonObject tx = described.at(described.size() - 2).toObject();
        const QJsonObject rx = described.last().toObject();
        QCOMPARE(tx.value("id"), QJsonValue("hardware.antenna.txRows"));
        QCOMPARE(rx.value("id"), QJsonValue("hardware.antenna.rxRows"));
        const auto verify = [&page, sku](const QJsonObject& table) {
            QVERIFY(SetupDescriptionService::validateAntennaRowsTable(table, sku));
            auto* group = qobject_cast<QGroupBox*>(bySetupId(page,
                table.value("id").toString()));
            QVERIFY(group != nullptr);
            QCOMPARE(group->title(), table.value("label").toString());
            QCOMPARE(group->toolTip(), table.value("tooltip").toString());
            const QJsonArray columns = table.value("columns").toArray();
            const QJsonArray rows = table.value("rows").toArray();
            QCOMPARE(rows.size(), 14);
            QStringList describedColumns;
            for (const QJsonValue& rawColumn : columns) {
                describedColumns << rawColumn.toObject().value("id").toString();
            }
            QStringList nativeColumns;
            for (QLabel* label : group->findChildren<QLabel*>()) {
                const QString id = label->property("nereusAntennaColumn").toString();
                if (!id.isEmpty()) { nativeColumns << id; }
            }
            QCOMPARE(nativeColumns, describedColumns);
            for (const QJsonValue& rawColumn : columns) {
                const QJsonObject column = rawColumn.toObject();
                QList<QLabel*> headers;
                for (QLabel* label : group->findChildren<QLabel*>()) {
                    if (label->property("nereusAntennaColumn")
                        == column.value("id").toVariant()) { headers.append(label); }
                }
                QCOMPARE(headers.size(), 1);
                QCOMPARE(headers.first()->text(), column.value("label").toString());
            }
            const QJsonArray columnGroups = table.value("columnGroups").toArray();
            QStringList nativeGroups;
            for (QLabel* label : group->findChildren<QLabel*>()) {
                const QString title = label->property("nereusAntennaColumnGroup").toString();
                if (!title.isEmpty()) { nativeGroups << title; }
            }
            QStringList describedGroups;
            for (const QJsonValue& rawGroup : columnGroups) {
                describedGroups << rawGroup.toObject().value("label").toString();
            }
            QCOMPARE(nativeGroups, describedGroups);
            for (const QJsonValue& rawGroup : columnGroups) {
                const QJsonObject header = rawGroup.toObject();
                QList<QLabel*> matches;
                for (QLabel* label : group->findChildren<QLabel*>()) {
                    if (label->property("nereusAntennaColumnGroup")
                        == header.value("label").toVariant()) { matches.append(label); }
                }
                QCOMPARE(matches.size(), 1);
                QCOMPARE(matches.first()->text(), header.value("label").toString());
            }
            for (int band = 0; band < rows.size(); ++band) {
                const QJsonObject row = rows.at(band).toObject();
                QCOMPARE(row.value("band"), QJsonValue(band));
                QList<QLabel*> labels;
                for (QLabel* label : group->findChildren<QLabel*>()) {
                    if (label->property("nereusAntennaRowLabel").toBool()
                        && label->property("nereusAntennaBand").toInt() == band) {
                        labels.append(label);
                    }
                }
                QCOMPARE(labels.size(), 1);
                QCOMPARE(labels.first()->text(), row.value("label").toString());
                const QJsonArray cells = row.value("cells").toArray();
                QCOMPARE(cells.size(), columns.size());
                QStringList nativeCellOrder;
                for (QRadioButton* button : group->findChildren<QRadioButton*>()) {
                    if (button->property("nereusAntennaBand").toInt() == band) {
                        nativeCellOrder << button->property("nereusAntennaColumn").toString();
                    }
                }
                QCOMPARE(nativeCellOrder, describedColumns);
                for (int i = 0; i < cells.size(); ++i) {
                    const QJsonObject cell = cells.at(i).toObject();
                    QCOMPARE(cell.value("column"), columns.at(i).toObject().value("id"));
                    QList<QRadioButton*> buttons;
                    for (QRadioButton* button : group->findChildren<QRadioButton*>()) {
                        if (button->property("nereusAntennaBand").toInt() == band
                            && button->property("nereusAntennaColumn")
                                == cell.value("column").toVariant()) {
                            buttons.append(button);
                        }
                    }
                    QCOMPARE(buttons.size(), 1);
                    QCOMPARE(buttons.first()->toolTip(), cell.value("tooltip").toString());
                }
            }
        };
        verify(tx);
        verify(rx);
        auto* rxGroup = qobject_cast<QGroupBox*>(bySetupId(page,
            QStringLiteral("hardware.antenna.rxRows")));
        QVERIFY(rxGroup != nullptr);
        for (int band = 0; band < 14; ++band) {
            int selected = 0;
            for (QRadioButton* button : rxGroup->findChildren<QRadioButton*>()) {
                if (button->property("nereusAntennaBand").toInt() == band
                    && button->property("nereusAntennaColumn").toString().startsWith(
                        QStringLiteral("rxOnly")) && button->isChecked()) {
                    ++selected;
                }
            }
            QCOMPARE(selected, radio.alexController().rxOnlyAnt(static_cast<Band>(band)) == 0
                                    ? 0 : 1);
        }
        auto* txGroup = qobject_cast<QGroupBox*>(bySetupId(page,
            QStringLiteral("hardware.antenna.txRows")));
        QVERIFY(txGroup != nullptr);
        radio.alexControllerMutable().setBlockTxAnt2(true);
        radio.alexControllerMutable().setBlockTxAnt3(true);
        for (QRadioButton* button : txGroup->findChildren<QRadioButton*>()) {
            const QString column = button->property("nereusAntennaColumn").toString();
            QCOMPARE(button->isEnabled(), column == QLatin1String("tx1"));
        }
    }

    void describedAntennaRowsRetargetOnSameBoardSkuChange()
    {
        RadioModel radio;
        radio.setHpsdrModelForTest(HPSDRModel::HERMES);
        AntennaAlexAntennaControlTab page(&radio);
        SetupDescriptionService service;
        service.setRadioContext(radio.boardCapabilities(), HPSDRModel::HERMES);
        const quint32 before = service.revision();
        const QString oldDescription = service.hardware();
        radio.setHpsdrModelForTest(HPSDRModel::ANAN100);
        radio.currentRadioChanged(radio.currentRadioInfo());
        service.setRadioContext(radio.boardCapabilities(), HPSDRModel::ANAN100);
        QVERIFY(service.revision() > before);
        QVERIFY(service.hardware() != oldDescription);
        const QJsonArray described = controls(service.category(QStringLiteral("hardware")));
        const QJsonObject rx = described.last().toObject();
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(rx, HPSDRModel::ANAN100));
        auto* group = qobject_cast<QGroupBox*>(bySetupId(page,
            QStringLiteral("hardware.antenna.rxRows")));
        QVERIFY(group != nullptr);
        const QJsonArray columns = rx.value("columns").toArray();
        for (int i = 3; i < 6; ++i) {
            const QString id = columns.at(i).toObject().value("id").toString();
            QLabel* header = nullptr;
            for (QLabel* label : group->findChildren<QLabel*>()) {
                if (label->property("nereusAntennaColumn").toString() == id) {
                    header = label;
                    break;
                }
            }
            QVERIFY(header != nullptr);
            QCOMPARE(header->text(), columns.at(i).toObject().value("label").toString());
            for (int band = 0; band < 14; ++band) {
                QRadioButton* button = nullptr;
                for (QRadioButton* candidate : group->findChildren<QRadioButton*>()) {
                    if (candidate->property("nereusAntennaBand").toInt() == band
                        && candidate->property("nereusAntennaColumn").toString() == id) {
                        button = candidate;
                        break;
                    }
                }
                QVERIFY(button != nullptr);
                const QJsonObject row = rx.value("rows").toArray().at(band).toObject();
                QCOMPARE(button->toolTip(), row.value("cells").toArray().at(i).toObject()
                             .value("tooltip").toString());
            }
        }
    }

    void describedTransmitDexpControlsMatchDesktop_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("ANAN-G2") << int(HPSDRHW::Saturn);
        QTest::newRow("HL2") << int(HPSDRHW::HermesLite);
    }

    void describedAudioTxProfileControlsMatchDesktop()
    {
        RadioModel model;
        TxProfileSetupPage page(&model, nullptr, &model.transmitModel());
        SetupDescriptionService service;
        const QJsonObject audio = service.category(QStringLiteral("audio"));
        QVERIFY(!audio.isEmpty());
        const QJsonArray pages = audio.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().toObject().value(QStringLiteral("id")),
                 QJsonValue(QStringLiteral("audio.txProfile")));
        const QJsonArray described = controls(audio);
        QCOMPARE(described.size(), 3);
        for (const QJsonValue& raw : described) {
            compareControl(page, raw.toObject());
        }
    }

    void describedTransmitDexpControlsMatchDesktop()
    {
        QFETCH(int, board);
        RadioModel model;
        model.setBoardForTest(static_cast<HPSDRHW>(board));
        DexpVoxPage dexp(&model);
        PowerPage power(&model);
        SetupDescriptionService service;
        const QJsonObject transmit = service.category(QStringLiteral("transmit"));
        QVERIFY(!transmit.isEmpty());
        const QJsonArray pages = transmit.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages.first().toObject().value(QStringLiteral("id")),
                 QJsonValue(QStringLiteral("transmit.power")));
        QCOMPARE(pages.last().toObject().value(QStringLiteral("id")),
                 QJsonValue(QStringLiteral("transmit.dexpVox")));
        const QJsonArray described = controls(transmit);
        QCOMPARE(described.size(), 24);
        for (const QJsonValue& raw : described) {
            const QJsonObject control = raw.toObject();
            compareControl(control.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("transmit.power."))
                               ? static_cast<QWidget&>(power) : static_cast<QWidget&>(dexp), control);
        }
    }

    void describedGeneralAndTestControlsMatchDesktop_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("ANAN-G2") << int(HPSDRHW::Saturn);
        QTest::newRow("HL2") << int(HPSDRHW::HermesLite);
    }

    void describedGeneralAndTestControlsMatchDesktop()
    {
        QFETCH(int, board);
        AppSettings::instance().clear();
        RadioModel model;
        model.setBoardForTest(static_cast<HPSDRHW>(board));
        model.addSlice();
        QVERIFY(model.notchModel() != nullptr);
        QVERIFY(model.notchModel()->addNotch(14074000.0) >= 0);
        // A real Setup page opens after connectToRadio has installed and
        // ranged the controller. Recreate that state without RF hardware.
        auto* attenuator = new StepAttenuatorController(&model);
        attenuator->setMinAttenuation(model.boardCapabilities().attenuator.minDb);
        attenuator->setMaxAttenuation(model.boardCapabilities().attenuator.maxDb);
        model.setStepAttController(attenuator);
        SetupDescriptionService service;
        service.setBoardCapabilities(model.boardCapabilities());
        StartupPrefsPage startup(&model);
        GeneralOptionsPage options(&model);
        TestTwoTonePage test(&model);
        CatTciServerPage cat;
        NrAnfSetupPage nr(&model);
        NbSnbSetupPage nb(&model);
        CwSetupPage cw(&model);
        AmSamSetupPage am(&model);
        FmSetupPage fm(&model);
        CfcSetupPage cfc(&model);
        AgcAlcSetupPage agc(&model);
        MnfSetupPage tnf(&model);
        DspOptionsPage dspOptions(&model);
        for (const QJsonValue& raw : controls(service.category(QStringLiteral("general")))) {
            const QJsonObject c = raw.toObject();
            QWidget& page = c.value("id").toString().startsWith("general.startup.")
                ? static_cast<QWidget&>(startup) : static_cast<QWidget&>(options);
            compareControl(page, c);
        }
        for (const QJsonValue& raw : controls(service.category(QStringLiteral("test")))) {
            compareControl(test, raw.toObject());
        }
        for (const QJsonValue& raw : controls(service.category(QStringLiteral("catNetwork")))) {
            compareControl(cat, raw.toObject());
        }
        const QJsonObject dsp = service.category(QStringLiteral("dsp"));
        QCOMPARE(dsp.value("pages").toArray().size(), 9);
        QStringList pageIds;
        for (const QJsonValue& page : dsp.value("pages").toArray()) {
            pageIds.append(page.toObject().value("id").toString());
        }
        QCOMPARE(pageIds, (QStringList{"dsp.agcAlc", "dsp.nrAnf", "dsp.nbSnb",
                                       "dsp.cw", "dsp.amSam", "dsp.fm", "dsp.cfc",
                                       "dsp.tnf", "dsp.options"}));
        for (const QJsonValue& raw : controls(dsp)) {
            const QJsonObject c = raw.toObject();
            const QString id = c.value("id").toString();
            QWidget& page = id.startsWith("dsp.nrAnf.") ? static_cast<QWidget&>(nr)
                : id.startsWith("dsp.agcAlc.") ? static_cast<QWidget&>(agc)
                : id.startsWith("dsp.nbSnb.") ? static_cast<QWidget&>(nb)
                : id.startsWith("dsp.cw.") ? static_cast<QWidget&>(cw)
                : id.startsWith("dsp.amSam.") ? static_cast<QWidget&>(am)
                : id.startsWith("dsp.fm.") ? static_cast<QWidget&>(fm)
                : id.startsWith("dsp.options.") ? static_cast<QWidget&>(dspOptions)
                : id.startsWith("dsp.tnf.") ? static_cast<QWidget&>(tnf)
                : static_cast<QWidget&>(cfc);
            compareControl(page, c);
        }
    }

    void nrAnfEditsFollowTheActiveSliceAndDisableWithoutOne()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        QCOMPARE(model.addSlice(), 0);
        QCOMPARE(model.addSlice(), 1);
        SliceModel* first = model.sliceById(0);
        SliceModel* second = model.sliceById(1);
        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);
        QVERIFY(model.setActiveSliceById(0));
        NrAnfSetupPage page(&model);
        auto taps = [&page]() {
            return qobject_cast<QSlider*>(bySetupId(page, QStringLiteral("dsp.nrAnf.nr1Taps")));
        };
        QVERIFY(taps() != nullptr);
        taps()->setValue(80);
        QCOMPARE(first->nr1Taps(), 80);
        const int secondOld = second->nr1Taps();
        QPointer<QSlider> oldGestureControl(taps());
        QVERIFY(model.setActiveSliceById(1));
        QVERIFY(oldGestureControl.isNull()); // a pending drag cannot retarget B
        QVERIFY(taps() != nullptr);
        auto* position = qobject_cast<QButtonGroup*>(bySetupId(
            page, QStringLiteral("dsp.nrAnf.nr1Position")));
        QVERIFY(position != nullptr);
        QCOMPARE(position->buttons().size(), 2);
        QCOMPARE(taps()->value(), secondOld);
        taps()->setValue(96);
        QCOMPARE(second->nr1Taps(), 96);
        QCOMPARE(first->nr1Taps(), 80);
        model.removeSlice(1);
        QVERIFY(taps() != nullptr);
        taps()->setValue(104);
        QCOMPARE(first->nr1Taps(), 104);
        // A local Core deliberately retains its last slice. A fresh model
        // supplies the no-selection state without changing that invariant.
        RadioModel empty;
        NrAnfSetupPage withoutSlice(&empty);
        auto* unavailable = qobject_cast<QSlider*>(bySetupId(
            withoutSlice, QStringLiteral("dsp.nrAnf.nr1Taps")));
        QVERIFY(unavailable != nullptr);
        QVERIFY(!unavailable->isEnabledTo(&withoutSlice));
    }

    void agcEditsFollowTheSelectedSliceAndRetireOldWidgets()
    {
        RadioModel model;
        QCOMPARE(model.addSlice(), 0);
        QCOMPARE(model.addSlice(), 1);
        SliceModel* first = model.sliceById(0);
        SliceModel* second = model.sliceById(1);
        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);
        QVERIFY(model.setActiveSliceById(0));
        AgcAlcSetupPage page(&model);
        auto attack = [&page]() {
            return qobject_cast<QSpinBox*>(bySetupId(
                page, QStringLiteral("dsp.agcAlc.agcAttack")));
        };
        QVERIFY(attack() != nullptr);
        attack()->setValue(37);
        QCOMPARE(first->agcAttack(), 37);
        QPointer<QSpinBox> old(attack());
        QVERIFY(model.setActiveSliceById(1));
        QVERIFY(old.isNull());
        QVERIFY(attack() != nullptr);
        attack()->setValue(53);
        QCOMPARE(second->agcAttack(), 53);
        QCOMPARE(first->agcAttack(), 37);
    }
};

QTEST_MAIN(SetupDescriptionParityTest)
#include "tst_setup_description_parity.moc"
