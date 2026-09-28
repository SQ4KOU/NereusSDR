// no-port-check: NereusSDR-original Setup description versus desktop widgets.
#include <QtTest>

#include "core/setup/SetupDescriptionService.h"
#include "core/AppSettings.h"
#include "core/StepAttenuatorController.h"
#include "core/settings/SettingsScope.h"
#include "gui/setup/GeneralOptionsPage.h"
#include "gui/setup/GeneralSetupPages.h"
#include "gui/setup/CatNetworkSetupPages.h"
#include "gui/setup/DspSetupPages.h"
#include "gui/setup/DspOptionsPage.h"
#include "gui/setup/TransmitSetupPages.h"
#include "gui/setup/TxProfileSetupPage.h"
#include "gui/setup/hardware/AntennaAlexAntennaControlTab.h"
#include "gui/setup/TestTwoTonePage.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/NotchModel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QSpinBox>
#include <QSlider>
#include <QTableWidget>

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
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(object)) {
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
    void describedHardwareAntennaScalarsMatchDesktop()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Hermes);
        AntennaAlexAntennaControlTab page(&model);
        SetupDescriptionService service;
        service.setBoardCapabilities(model.boardCapabilities());
        const QJsonObject hardware = service.category(QStringLiteral("hardware"));
        QVERIFY(!hardware.isEmpty());
        const QJsonArray described = controls(hardware);
        QCOMPARE(described.size(), 3);
        for (const QJsonValue& raw : described) {
            compareControl(page, raw.toObject());
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
