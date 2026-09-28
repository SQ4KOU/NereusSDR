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
#include "gui/setup/TestTwoTonePage.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>

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
        QCOMPARE(dsp.value("pages").toArray().size(), 2);
        for (const QJsonValue& raw : controls(dsp)) {
            const QJsonObject c = raw.toObject();
            QWidget& page = c.value("id").toString().startsWith("dsp.nrAnf.")
                ? static_cast<QWidget&>(nr) : static_cast<QWidget&>(nb);
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
};

QTEST_MAIN(SetupDescriptionParityTest)
#include "tst_setup_description_parity.moc"
