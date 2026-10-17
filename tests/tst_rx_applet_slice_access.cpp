// =================================================================
// tests/tst_rx_applet_slice_access.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Slice control and shared listening plan, Task 15: the RX applet's tabs
// list the slices this window controls or listens to. On a listened slice
// (another device controls it) every shared tuning and DSP control in the
// applet is disabled with the reason naming that device, and the applet
// never writes the slice. Take control stays reachable from the tab and
// badge menus; the tabs themselves stay usable, so a listened tab can be
// selected. The applet has no volume or mute (ruling U5: the flag's
// "Your volume" is that surface).
//
// Modification history (NereusSDR):
//   2026-10-14 - PR #262 RX-only applet echo regression. J.J. Boyd
//                 (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-14 - PR #96 actual TX popup blocked-choice regression.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-09-29 - Written for slice control plan Task 15. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
//   2026-09-30 - TX rulings (item 3, JJ): the attenuator and preamp
//                 controls are held on a listened slice with the same
//                 reason, write nothing, and come back on a controlled one,
//                 local and remote. J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-09-30: core-slice take-over: Take control of the Core's own
//               slice is disabled with the Core's words below
//               sliceAccessVersion 3. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================
#include <QtTest/QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QFontMetrics>
#include <QStyleOptionComboBox>
#include "gui/styles/WorkspacePreview.h"
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>

#include "core/AppSettings.h"
#include "core/RadioConnection.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/StepAttenuatorController.h"
#include "core/StepAttenuatorFacade.h"
#include "core/accessories/AlexController.h"
#include "gui/applets/RxApplet.h"
#include "gui/widgets/FilterPassbandWidget.h"
#include "gui/widgets/VfoWidget.h"
#include "models/Band.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

// No-I/O connection: makes RadioModel's real slice antenna handlers
// eligible without a radio, sockets, audio, or a connection lifecycle.
class AntennaConnectionFake : public RadioConnection {
public:
    AntennaConnectionFake() { setState(ConnectionState::Connected); }
    void init() override {}
    void connectToRadio(const RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void setMox(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setWatchdogEnabled(bool) override {}
    void sendTxIq(const float*, int) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
};

const QString kReason = QStringLiteral("Shack iPad controls this slice");

VfoWidget::SliceAccess listened()
{
    VfoWidget::SliceAccess access;
    access.state = VfoWidget::SliceAccess::State::Listening;
    access.line = QStringLiteral("Listening · controlled by Shack iPad");
    access.heldReason = kReason;
    return access;
}

VfoWidget::SliceAccess controlled()
{
    VfoWidget::SliceAccess access;
    access.state = VfoWidget::SliceAccess::State::Controlled;
    access.line = QStringLiteral("You control");
    return access;
}

QAction* findAction(QMenu& menu, const QString& text)
{
    for (QAction* action : menu.actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

// Every control in the applet that changes the slice: combos, sliders,
// buttons and the passband. The radio's own hardware (the ATT/S-ATT row and
// the RX1 preamp toggle, held too since TX rulings item 3 and checked by
// their own tests below) and the slice tabs are not slice settings.
QList<QWidget*> sliceControls(RxApplet& applet)
{
    QWidget* attStack = applet.findChild<QWidget*>(QStringLiteral("RxAttenuatorStack"));
    QList<QWidget*> controls;
    const QList<QWidget*> all = applet.findChildren<QWidget*>();
    for (QWidget* w : all) {
        const bool kind = qobject_cast<QComboBox*>(w) || qobject_cast<QSlider*>(w)
            || qobject_cast<QPushButton*>(w) || qobject_cast<FilterPassbandWidget*>(w);
        if (!kind || qobject_cast<QCheckBox*>(w)) { continue; }
        if (attStack && attStack->isAncestorOf(w)) { continue; }
        controls.append(w);
    }
    return controls;
}

struct Snapshot {
    DSPMode mode;
    int filterLow;
    int filterHigh;
    AGCMode agc;
    bool locked;
    double pan;
    bool ssql;
    double ssqlThresh;
    int agcThreshold;
    bool rit;
    int ritHz;
    bool xit;
    int xitHz;
    int stepHz;
    QString rxAnt;
    QString txAnt;
    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const SliceModel& s)
{
    return {s.dspMode(), s.filterLow(), s.filterHigh(), s.agcMode(), s.locked(),
            s.audioPan(), s.ssqlEnabled(), s.ssqlThresh(), s.agcThreshold(),
            s.ritEnabled(), s.ritHz(), s.xitEnabled(), s.xitHz(), s.stepHz(),
            s.rxAntenna(), s.txAntenna()};
}

// Drives every slice control the way code can even when it is disabled:
// toggles, clicks, index and value changes, and the passband's edit signal.
// The antenna buttons open a menu, so they are left to the caller.
void driveEverything(RxApplet& applet)
{
    for (QWidget* w : sliceControls(applet)) {
        const QString name = w->objectName();
        if (name == QStringLiteral("m_rxAntBtn") || name == QStringLiteral("m_txAntBtn")) {
            continue;
        }
        if (auto* combo = qobject_cast<QComboBox*>(w)) {
            if (combo->count() > 1) {
                combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
            }
        } else if (auto* slider = qobject_cast<QSlider*>(w)) {
            slider->setValue(slider->value() == slider->maximum() ? slider->minimum()
                                                                  : slider->maximum());
        } else if (auto* passband = qobject_cast<FilterPassbandWidget*>(w)) {
            emit passband->filterChanged(150, 1850);
        } else if (auto* button = qobject_cast<QPushButton*>(w)) {
            if (button->isCheckable()) {
                button->toggle();
            }
            emit button->clicked(button->isChecked());
        }
    }
}

} // namespace

class TestRxAppletSliceAccess : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { AppSettings::instance().clear(); }
    void cleanup()      { AppSettings::instance().clear(); }

    void workspaceAgcLabelsAndLateAutoUpdatesFit()
    {
        WorkspacePreview::setEnabledForTest(true);
        SliceModel slice(0);
        RxApplet applet(&slice, nullptr);
        applet.resize(260, 750);
        applet.show();
        WorkspacePreview::themeSubtree(&applet);
        QCoreApplication::processEvents();
        QComboBox* agc = nullptr;
        QPushButton* badge = nullptr;
        for (QComboBox* combo : applet.findChildren<QComboBox*>()) {
            if (combo->findText(QStringLiteral("Long")) >= 0
                && combo->findText(QStringLiteral("Fast")) >= 0) { agc = combo; }
        }
        for (QPushButton* button : applet.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("AUTO")) { badge = button; }
        }
        QVERIFY(agc);
        QVERIFY(badge);
        for (int index = 0; index < agc->count(); ++index) {
            agc->setCurrentIndex(index);
            QStyleOptionComboBox option;
            option.initFrom(agc);
            option.currentText = agc->currentText();
            const QRect field = agc->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                            QStyle::SC_ComboBoxEditField, agc);
            QVERIFY2(field.width() >= QFontMetrics(agc->font(), agc).horizontalAdvance(option.currentText),
                     qPrintable(option.currentText));
        }
        for (bool active : {true, false, true}) {
            applet.updateAgcAutoVisuals(active, -98.0f, 20.0, true);
            WorkspacePreview::themeSubtree(&applet);
            QCoreApplication::processEvents();
            const QFontMetrics metrics(badge->font(), badge);
            QVERIFY(badge->width() >= metrics.horizontalAdvance(badge->text()) + 8);
            QVERIFY(badge->height() >= metrics.height() + 4);
        }
        WorkspacePreview::setEnabledForTest(false);
        // The look is a startup choice. Flipping its test switch does not
        // send an event; themeSubtree deliberately does nothing when off.
        // Verify the observer's classic Show path, then a fresh classic RX.
        agc->hide();
        agc->show();
        applet.updateAgcAutoVisuals(false, -98.0f, 20.0, true);
        QCoreApplication::processEvents();
        QCOMPARE(agc->width(), 52);
        QCOMPARE(badge->font().pixelSize(), 7);
        QCOMPARE(badge->size(), QSize(30, 14));
        RxApplet classic(&slice, nullptr);
        classic.show();
        QCoreApplication::processEvents();
        QComboBox* classicAgc = nullptr;
        QPushButton* classicBadge = nullptr;
        for (QComboBox* combo : classic.findChildren<QComboBox*>()) {
            if (combo->findText(QStringLiteral("Long")) >= 0
                && combo->findText(QStringLiteral("Fast")) >= 0) { classicAgc = combo; }
        }
        for (QPushButton* button : classic.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("AUTO")) { classicBadge = button; }
        }
        QVERIFY(classicAgc);
        QVERIFY(classicBadge);
        QCOMPARE(classicAgc->width(), 52);
        QCOMPARE(classicAgc->font().pixelSize(), 10);
        QCOMPARE(classicBadge->font().pixelSize(), 7);
        QCOMPARE(classicBadge->size(), QSize(30, 14));
    }

    void unshared_applet_is_not_listening()
    {
        SliceModel slice(0);
        RxApplet applet(&slice, nullptr);
        QCOMPARE(applet.sliceAccess().state, VfoWidget::SliceAccess::State::Unshared);
        QVERIFY(!applet.isListening());
        for (QWidget* control : sliceControls(applet)) {
            QVERIFY(control->isEnabled());
        }
    }

    void listened_applet_holds_every_slice_control_with_the_reason()
    {
        SliceModel slice(1);
        RxApplet applet(&slice, nullptr);
        applet.setSliceAccess(listened());
        QVERIFY(applet.isListening());

        const QList<QWidget*> controls = sliceControls(applet);
        QVERIFY(controls.size() >= 20);
        const QList<QWidget*> held = applet.heldControlsForTest();
        for (QWidget* control : controls) {
            const QString what = control->objectName().isEmpty()
                ? QString::fromLatin1(control->metaObject()->className())
                : control->objectName();
            QVERIFY2(held.contains(control), qPrintable(what));
            QVERIFY2(!control->isEnabled(), qPrintable(what));
            QCOMPARE(control->toolTip(), kReason);
            QCOMPARE(control->accessibleDescription(), kReason);
        }
    }

    // TX rulings (item 3, JJ): on a listened slice the attenuator and
    // preamp controls are disabled with the reason naming the controller,
    // and the applet writes neither; on a controlled slice they work.
    void listened_applet_holds_the_attenuator_and_preamp()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        auto* ctrl = new StepAttenuatorController(&model);
        ctrl->setTickTimerEnabled(false);
        model.setStepAttController(ctrl);
        SliceModel* a = model.sliceById(model.addSlice());
        QVERIFY(a);
        ctrl->setStepAttEnabled(false);
        ctrl->setPreampMode(PreampMode::SaMinus10);
        RxApplet applet(nullptr, &model);
        applet.setSlice(a);
        auto* label = applet.findChild<QLabel*>(QStringLiteral("RxAttLabel"));
        auto* stack = applet.findChild<QWidget*>(QStringLiteral("RxAttenuatorStack"));
        auto* combo = applet.findChild<QComboBox*>(QStringLiteral("RxPreampCombo"));
        auto* spin = stack ? stack->findChild<QSpinBox*>() : nullptr;
        QVERIFY(label && stack && combo && spin);
        const QList<QWidget*> att{label, stack, combo, spin};
        QVERIFY(combo->count() > 1);

        applet.setSliceAccess(listened());
        const QList<QWidget*> held = applet.heldControlsForTest();
        for (QWidget* w : att) {
            QVERIFY2(held.contains(w), qPrintable(w->objectName()));
            QVERIFY2(!w->isEnabled(), qPrintable(w->objectName()));
            QCOMPARE(w->toolTip(), kReason);
            QCOMPARE(w->accessibleDescription(), kReason);
        }
        const PreampMode mode = ctrl->preampMode();
        const int dB = ctrl->attenuatorDb();
        combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
        spin->setValue(spin->value() == spin->maximum() ? spin->minimum() : spin->maximum());
        QCOMPARE(ctrl->preampMode(), mode);
        QCOMPARE(ctrl->attenuatorDb(), dB);

        applet.setSliceAccess(controlled());
        for (QWidget* w : att) {
            QVERIFY2(w->isEnabled(), qPrintable(w->objectName()));
            QVERIFY(w->toolTip() != kReason);
        }
        const int next = (combo->currentIndex() + 1) % combo->count();
        combo->setCurrentIndex(next);
        QCOMPARE(static_cast<int>(ctrl->preampMode()), combo->itemData(next).toInt());
    }

    // A remote window: the Core's attenuator arriving while the slice is
    // listened keeps the controls held, and control brings them back.
    void remote_attenuator_offered_while_listening_stays_held()
    {
        RadioModel remote(RadioModel::Role::Remote);
        remote.setBoardForTest(HPSDRHW::Hermes);
        RxApplet applet(nullptr, &remote);
        SliceModel slice(1);
        applet.setSlice(&slice);
        auto* combo = applet.findChild<QComboBox*>(QStringLiteral("RxPreampCombo"));
        QVERIFY(combo);
        applet.setSliceAccess(listened());
        StepAttenuatorFacade* stepAtt = remote.stepAttFacade();
        stepAtt->setWindowAvailability(true, QString());
        QVERIFY(!combo->isEnabled());
        QCOMPARE(combo->toolTip(), kReason);

        applet.setSliceAccess(controlled());
        QVERIFY(combo->isEnabled());
        QVERIFY(combo->toolTip().isEmpty());

        // The Core withdrawing it while listened comes back withdrawn.
        applet.setSliceAccess(listened());
        const QString gone = QStringLiteral("The Core has no attenuator ready.");
        stepAtt->setWindowAvailability(false, gone);
        QCOMPARE(combo->toolTip(), kReason);
        applet.setSliceAccess(controlled());
        QVERIFY(!combo->isEnabled());
        QVERIFY(combo->toolTip() != kReason);
    }

    void controlled_applet_writes_the_slice()
    {
        // The baseline: the same drive does change a slice this window
        // controls, so the listened check below is not vacuous.
        SliceModel slice(0);
        RxApplet applet(&slice, nullptr);
        applet.setSliceAccess(controlled());
        QSignalSpy autoAgc(&applet, &RxApplet::autoAgcToggled);
        const Snapshot before = snapshot(slice);
        driveEverything(applet);
        QVERIFY(!(snapshot(slice) == before));
        QCOMPARE(autoAgc.count(), 1);
    }

    void listened_applet_never_writes_the_slice()
    {
        SliceModel slice(1);
        RxApplet applet(&slice, nullptr);
        applet.setSliceAccess(listened());
        QSignalSpy autoAgc(&applet, &RxApplet::autoAgcToggled);
        QSignalSpy modeSpy(&slice, &SliceModel::dspModeChanged);
        QSignalSpy filterSpy(&slice, &SliceModel::filterChanged);
        QSignalSpy lockSpy(&slice, &SliceModel::lockedChanged);
        const Snapshot before = snapshot(slice);

        driveEverything(applet);

        // The antenna buttons must not open their menus either; a menu that
        // opened is closed and recorded.
        bool menuOpened = false;
        for (const QString& name : {QStringLiteral("m_rxAntBtn"), QStringLiteral("m_txAntBtn")}) {
            auto* button = applet.findChild<QPushButton*>(name);
            QVERIFY(button);
            QTimer::singleShot(0, [&menuOpened]() {
                if (QWidget* popup = QApplication::activePopupWidget()) {
                    menuOpened = true;
                    popup->close();
                }
            });
            emit button->clicked(false);
            QCoreApplication::processEvents();
        }

        QVERIFY(snapshot(slice) == before);
        QCOMPARE(modeSpy.count(), 0);
        QCOMPARE(filterSpy.count(), 0);
        QCOMPARE(lockSpy.count(), 0);
        QCOMPARE(autoAgc.count(), 0);
        QVERIFY(!menuOpened);
    }

    void listened_applet_reads_the_band_antenna_without_writing_it()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Hermes);
        model.addPanadapter();
        model.panadapters().first()->setBand(Band::Band20m);
        model.addSlice();
        model.addSlice();
        // RadioModel keeps its active slice's antenna label in step with
        // the band's (RadioModel.cpp, the antennaChanged handler); the
        // applet here shows the other slice, so any write is the applet's.
        SliceModel* slice = model.sliceById(0) == model.activeSlice()
            ? model.sliceById(1) : model.sliceById(0);
        QVERIFY(slice);
        QVERIFY(slice != model.activeSlice());

        RxApplet applet(slice, &model);
        applet.setSliceAccess(listened());
        const QString before = slice->rxAntenna();
        QSignalSpy rxAntSpy(slice, &SliceModel::rxAntennaChanged);

        const int other = before == QStringLiteral("ANT3") ? 2 : 3;
        model.alexControllerMutable().setRxAnt(Band::Band20m, other);

        QCOMPARE(rxAntSpy.count(), 0);
        QCOMPARE(slice->rxAntenna(), before);
        // The button shows the slice's antenna, which the controller sets.
        QCOMPARE(applet.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"))->text(), before);
    }

    // PR #262: an applet's controller echo must preserve the RX-only mux.
    // Literal slot labels come from SkuUiProfile.cpp's existing ANAN7000D
    // and ANAN100D rows; the regression catches ANT-only read-back.
    void controlling_applet_preserves_rx_only_selection_data()
    {
        QTest::addColumn<int>("sku");
        QTest::addColumn<int>("slot");
        QTest::addColumn<QString>("label");
        QTest::addColumn<bool>("initialPopulate");
        for (bool initial : {false, true}) {
            const QByteArray phase = initial ? "initial" : "echo";
            QTest::newRow((phase + "-7000-byps").constData())
                << static_cast<int>(HPSDRModel::ANAN7000D) << 1 << QStringLiteral("BYPS") << initial;
            QTest::newRow((phase + "-7000-ext1").constData())
                << static_cast<int>(HPSDRModel::ANAN7000D) << 2 << QStringLiteral("EXT1") << initial;
            QTest::newRow((phase + "-7000-xvtr").constData())
                << static_cast<int>(HPSDRModel::ANAN7000D) << 3 << QStringLiteral("XVTR") << initial;
            QTest::newRow((phase + "-100d-ext2").constData())
                << static_cast<int>(HPSDRModel::ANAN100D) << 1 << QStringLiteral("EXT2") << initial;
        }
    }

    void controlling_applet_preserves_rx_only_selection()
    {
        QFETCH(int, sku);
        QFETCH(int, slot);
        QFETCH(QString, label);
        QFETCH(bool, initialPopulate);
        // Add the slice while disconnected, then explicitly wire its real
        // antenna handlers against a no-I/O fake; no DSP channel is started.
        AntennaConnectionFake connection;
        RadioModel model;
        model.setHpsdrModelForTest(static_cast<HPSDRModel>(sku));
        model.addPanadapter();
        model.panadapters().first()->setBand(Band::Band20m);
        model.addSlice();
        SliceModel* slice = model.sliceById(0);
        QVERIFY(slice);
        QCOMPARE(model.activeSlice(), slice);
        QCOMPARE(bandFromFrequency(slice->frequency()), Band::Band20m);
        QCOMPARE(model.lastBand(), Band::Band20m);
        QCOMPARE(model.panadapters().first()->band(), Band::Band20m);
        model.injectConnectionForTest(&connection);
        const auto uninject = qScopeGuard([&model] { model.injectConnectionForTest(nullptr); });
        model.wireSliceSignalsForTest();
        // Normalize restored per-band labels before testing a fresh choice.
        // Keep Alex's TX read-back coherent with the slice's TX intent so
        // RadioModel's existing reverse sync does not change it independently.
        slice->setRxAntenna(QStringLiteral("ANT1"));
        model.alexControllerMutable().setRxOnlyAnt(Band::Band20m, 0);
        model.alexControllerMutable().setTxAnt(Band::Band20m, 3);
        slice->setTxAntenna(QStringLiteral("ANT3"));
        QCOMPARE(slice->rxAntenna(), QStringLiteral("ANT1"));
        QCOMPARE(model.alexController().rxOnlyAnt(Band::Band20m), 0);
        QCOMPARE(model.alexController().txAnt(Band::Band20m), 3);
        QSignalSpy txSpy(slice, &SliceModel::txAntennaChanged);
        if (initialPopulate) {
            model.alexControllerMutable().setRxOnlyAnt(Band::Band20m, slot);
            QCOMPARE(slice->rxAntenna(), label);
        }

        RxApplet applet(slice, &model);
        applet.setSliceAccess(controlled());
        QPushButton* button = applet.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"));
        QVERIFY(button);
        if (!initialPopulate) {
            slice->setRxAntenna(label);
        }
        QCOMPARE(model.alexController().rxOnlyAnt(Band::Band20m), slot);
        QCOMPARE(slice->rxAntenna(), label);
        QCOMPARE(button->text(), label);
        QCOMPARE(slice->txAntenna(), QStringLiteral("ANT3"));
        QCOMPARE(txSpy.count(), 0);

        // A real main-ANT selection must still release the RX-only mux.
        slice->setRxAntenna(QStringLiteral("ANT1"));
        QCOMPARE(model.alexController().rxOnlyAnt(Band::Band20m), 0);
        QCOMPARE(slice->rxAntenna(), QStringLiteral("ANT1"));
        QCOMPARE(button->text(), QStringLiteral("ANT1"));
    }

    void listened_applet_does_not_echo_rx_only_controller_changes()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN7000D);
        model.addPanadapter();
        model.panadapters().first()->setBand(Band::Band20m);
        model.addSlice();
        model.addSlice();
        SliceModel* slice = model.sliceById(0) == model.activeSlice()
            ? model.sliceById(1) : model.sliceById(0);
        QVERIFY(slice);
        RxApplet applet(slice, &model);
        applet.setSliceAccess(listened());
        const QString before = slice->rxAntenna();
        QSignalSpy rxSpy(slice, &SliceModel::rxAntennaChanged);
        model.alexControllerMutable().setRxOnlyAnt(Band::Band20m, 1);
        QCOMPARE(model.alexController().rxOnlyAnt(Band::Band20m), 1);
        QCOMPARE(rxSpy.count(), 0);
        QCOMPARE(slice->rxAntenna(), before);
        QCOMPARE(applet.findChild<QPushButton*>(QStringLiteral("m_rxAntBtn"))->text(), before);
    }

    void tx_popup_refuses_blocked_ports_data()
    {
        QTest::addColumn<bool>("remote");
        QTest::addColumn<int>("port");
        QTest::addColumn<bool>("lateBlock");
        QTest::addColumn<bool>("allowed");
        QTest::addColumn<bool>("nonBound");
        for (bool remote : {false, true}) {
            for (int port : {2, 3}) {
                for (bool late : {false, true}) {
                    const QByteArray name = QByteArray(remote ? "remote" : "local")
                        + QByteArray::number(port) + (late ? "-race" : "-blocked");
                    QTest::newRow(name.constData()) << remote << port << late << false << false;
                }
            }
        }
        QTest::newRow("local-valid-bound") << false << 2 << false << true << false;
        QTest::newRow("local-valid-nonbound") << false << 2 << false << true << true;
        QTest::newRow("remote-valid") << true << 2 << false << true << false;
    }

    void tx_popup_refuses_blocked_ports()
    {
        QFETCH(bool, remote);
        QFETCH(int, port);
        QFETCH(bool, lateBlock);
        QFETCH(bool, allowed);
        QFETCH(bool, nonBound);
        AntennaConnectionFake connection;
        RadioModel model(remote ? RadioModel::Role::Remote : RadioModel::Role::Local);
        model.setBoardForTest(HPSDRHW::Hermes);
        SliceModel remoteSlice(0);
        SliceModel* slice = &remoteSlice;
        if (!remote) {
            model.addPanadapter();
            model.panadapters().first()->setBand(Band::Band20m);
            model.addSlice();
            if (nonBound) {
                model.addSlice();
                model.setActiveSlice(1);
            }
            slice = model.activeSlice();
            QVERIFY(slice);
            QCOMPARE(bandFromFrequency(slice->frequency()), Band::Band20m);
            QCOMPARE(model.lastBand(), Band::Band20m);
            QCOMPARE(model.txBoundSlice(), model.sliceById(0));
            QCOMPARE(slice == model.txBoundSlice(), !nonBound);
            model.injectConnectionForTest(&connection);
            model.wireSliceSignalsForTest();
        }
        const auto uninject = qScopeGuard([&model] { model.injectConnectionForTest(nullptr); });
        slice->setTxAntenna(QStringLiteral("ANT1"));
        AlexAntennaFacade* const facade = model.alexAntennaFacade();
        QVERIFY(facade);
        auto block = [&]() {
            if (remote) {
                return facade->applyRemoteProperty(port == 2 ? QByteArray("blockTxAnt2")
                                                            : QByteArray("blockTxAnt3"), true);
            }
            if (port == 2) { model.alexControllerMutable().setBlockTxAnt2(true); }
            else { model.alexControllerMutable().setBlockTxAnt3(true); }
            return true;
        };
        if (!allowed && !lateBlock) { QVERIFY(block()); }
        // Install subscriptions as the host does; constructing with this
        // slice then setting the same pointer would skip connectSlice().
        RxApplet applet(nullptr, &model);
        applet.setSlice(slice);
        applet.setSliceAccess(controlled());
        applet.setBoardCapabilities(model.boardCapabilities());
        applet.setHpsdrSku(model.hardwareProfile().model);
        QPushButton* const button = applet.findChild<QPushButton*>(QStringLiteral("m_txAntBtn"));
        QVERIFY(button);
        const QString choice = QStringLiteral("ANT%1").arg(port);
        bool menuFound = false;
        bool actionFound = false;
        bool actionEnabled = false;
        bool blockApplied = true;
        bool timedOut = false;
        QTimer guard;
        guard.setSingleShot(true);
        connect(&guard, &QTimer::timeout, &applet, [&] {
            timedOut = true;
            if (QWidget* popup = QApplication::activePopupWidget()) { popup->close(); }
        });
        QTimer::singleShot(0, &applet, [&] {
            QMenu* const menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) { return; }
            menuFound = true;
            QAction* const action = findAction(*menu, choice);
            if (!action) { menu->close(); return; }
            actionFound = true;
            actionEnabled = action->isEnabled();
            if (lateBlock) { blockApplied = block(); }
            if (actionEnabled) {
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
            } else {
                menu->close();
            }
        });
        QSignalSpy changed(slice, &SliceModel::txAntennaChanged);
        guard.start(1000);
        button->click();
        guard.stop();
        QVERIFY(!timedOut);
        QVERIFY(menuFound);
        QVERIFY(actionFound);
        QVERIFY(blockApplied);
        QCOMPARE(actionEnabled, allowed || lateBlock);
        const QString expected = allowed ? choice : QStringLiteral("ANT1");
        QCOMPARE(slice->txAntenna(), expected);
        QCOMPARE(button->text(), expected);
        QCOMPARE(changed.count(), allowed ? 1 : 0);
        if (!remote) {
            QCOMPARE(model.alexController().txAnt(Band::Band20m), allowed && !nonBound ? port : 1);
        }
    }

    void controlling_again_restores_every_control()
    {
        SliceModel slice(0);
        RxApplet applet(&slice, nullptr);
        QHash<QWidget*, QString> tips;
        for (QWidget* control : sliceControls(applet)) {
            tips.insert(control, control->toolTip());
        }

        applet.setSliceAccess(listened());
        applet.setSliceAccess(controlled());

        for (QWidget* control : sliceControls(applet)) {
            QVERIFY(control->isEnabled());
            QCOMPARE(control->toolTip(), tips.value(control));
            QVERIFY(control->accessibleDescription() != kReason);
        }
    }

    void filter_buttons_rebuilt_while_listening_are_held_then_restored()
    {
        SliceModel slice(1);
        RxApplet applet(&slice, nullptr);
        applet.setSliceAccess(listened());

        // The controlling device changes the mode; the applet rebuilds its
        // preset buttons for the new mode.
        slice.setDspMode(slice.dspMode() == DSPMode::AM ? DSPMode::USB : DSPMode::AM);
        QCoreApplication::processEvents();  // the old buttons are deleted later

        const QList<QWidget*> controls = sliceControls(applet);
        for (QWidget* control : controls) {
            QVERIFY(!control->isEnabled());
            QCOMPARE(control->toolTip(), kReason);
        }

        applet.setSliceAccess(controlled());
        for (QWidget* control : sliceControls(applet)) {
            QVERIFY(control->isEnabled());
            QVERIFY(control->toolTip() != kReason);
        }
    }

    void listened_menu_offers_take_control_and_stop_listening()
    {
        SliceModel a(0);
        SliceModel b(1);
        RxApplet applet(&b, nullptr);
        applet.setSliceAccess(listened());
        applet.setSliceTabAccess({{0, controlled()}, {1, listened()}});
        QSignalSpy take(&applet, &RxApplet::takeControlRequested);
        QSignalSpy stop(&applet, &RxApplet::stopListeningRequested);
        QSignalSpy release(&applet, &RxApplet::releaseRequested);

        QMenu listenedMenu;
        applet.populateSliceMenu(listenedMenu, 1);
        QAction* takeAct = findAction(listenedMenu, QStringLiteral("Take control"));
        QAction* stopAct = findAction(listenedMenu, QStringLiteral("Stop listening"));
        QVERIFY(takeAct);
        QVERIFY(stopAct);
        QVERIFY(takeAct->isEnabled());
        QVERIFY(!findAction(listenedMenu, QStringLiteral("Release")));
        takeAct->trigger();
        stopAct->trigger();
        QCOMPARE(take.count(), 1);
        QCOMPARE(take.first().at(0).toInt(), 1);
        QCOMPARE(stop.count(), 1);
        QCOMPARE(stop.first().at(0).toInt(), 1);

        QMenu controlledMenu;
        applet.populateSliceMenu(controlledMenu, 0);
        QAction* releaseAct = findAction(controlledMenu, QStringLiteral("Release"));
        QVERIFY(releaseAct);
        QVERIFY(!findAction(controlledMenu, QStringLiteral("Take control")));
        releaseAct->trigger();
        QCOMPARE(release.count(), 1);
        QCOMPARE(release.first().at(0).toInt(), 0);
    }

    // Core-slice take-over: Take control stays in the tab menu, disabled
    // with the Core's words, when the Core refuses the take.
    void listened_menu_take_control_is_disabled_with_the_cores_words()
    {
        SliceModel a(0);
        RxApplet applet(&a, nullptr);
        VfoWidget::SliceAccess access = listened();
        access.takeHeldReason = QStringLiteral("Slice A is run by the Core itself, so control of it cannot pass to this device.");
        applet.setSliceAccess(access);
        applet.setSliceTabAccess({{0, access}});
        QSignalSpy take(&applet, &RxApplet::takeControlRequested);

        QMenu menu;
        applet.populateSliceMenu(menu, 0);
        QAction* takeAct = findAction(menu, QStringLiteral("Take control"));
        QVERIFY(takeAct);
        QVERIFY(!takeAct->isEnabled());
        QCOMPARE(takeAct->toolTip(), access.takeHeldReason);
        QAction* stopAct = findAction(menu, QStringLiteral("Stop listening"));
        QVERIFY(stopAct && stopAct->isEnabled());
        takeAct->trigger();
        QCOMPARE(take.count(), 0);
    }

    void unshared_menu_has_no_access_actions()
    {
        SliceModel slice(0);
        RxApplet applet(&slice, nullptr);
        QMenu menu;
        applet.populateSliceMenu(menu, 0);
        QVERIFY(menu.actions().isEmpty());
    }

    void a_waiting_request_disables_the_access_actions()
    {
        SliceModel slice(1);
        RxApplet applet(&slice, nullptr);
        applet.setSliceAccess(listened());
        applet.setSliceTabAccess({{1, listened()}});
        applet.setSliceAccessPending(QStringLiteral("Asking the Core…"));
        QSignalSpy take(&applet, &RxApplet::takeControlRequested);

        QMenu menu;
        applet.populateSliceMenu(menu, 1);
        QAction* takeAct = findAction(menu, QStringLiteral("Take control"));
        QVERIFY(takeAct);
        QVERIFY(!takeAct->isEnabled());
        QCOMPARE(takeAct->toolTip(), QStringLiteral("Asking the Core…"));
        takeAct->trigger();
        QCOMPARE(take.count(), 0);

        applet.setSliceAccessPending(QString());
        QMenu again;
        applet.populateSliceMenu(again, 1);
        QVERIFY(findAction(again, QStringLiteral("Take control"))->isEnabled());
    }

    void tabs_say_who_controls_each_slice_and_stay_selectable()
    {
        SliceModel a(0);
        SliceModel b(1);
        RxApplet applet(&a, nullptr);
        applet.setSliceAccess(controlled());
        applet.setSliceTabAccess({{0, controlled()}, {1, listened()}});
        applet.updateSliceButtons({&a, &b}, 0);

        QCOMPARE(applet.sliceTabToolTipForTest(0), QStringLiteral("Slice A: You control"));
        QCOMPARE(applet.sliceTabToolTipForTest(1),
                 QStringLiteral("Slice B: Listening · controlled by Shack iPad"));

        QSignalSpy activate(&applet, &RxApplet::sliceActivationRequested);
        QToolButton* tabB = nullptr;
        for (QToolButton* tab : applet.findChildren<QToolButton*>()) {
            if (tab->text() == QStringLiteral("B")) { tabB = tab; }
        }
        QVERIFY(tabB);
        QVERIFY(tabB->isEnabled());
        tabB->click();
        QCOMPARE(activate.count(), 1);
        QCOMPARE(activate.first().at(0).toInt(), 1);

        // Selecting the listened slice binds it listened: the tabs stay
        // usable while the slice's controls are held.
        applet.setSlice(&b);
        applet.setSliceAccess(listened());
        applet.updateSliceButtons({&a, &b}, 1);
        for (QToolButton* tab : applet.findChildren<QToolButton*>()) {
            QVERIFY(tab->isEnabled());
        }
    }

    void unshared_tabs_keep_the_plain_tooltip()
    {
        SliceModel a(0);
        SliceModel b(1);
        RxApplet applet(&a, nullptr);
        applet.updateSliceButtons({&a, &b}, 0);
        QCOMPARE(applet.sliceTabToolTipForTest(1), QStringLiteral("Slice B"));
    }

    void applet_has_no_volume_or_mute()
    {
        // Ruling U5: the flag's "Your volume" is the listener's volume and
        // mute; the RX applet gains neither.
        SliceModel slice(1);
        RxApplet applet(&slice, nullptr);
        applet.setSliceAccess(listened());
        for (QAbstractButton* button : applet.findChildren<QAbstractButton*>()) {
            QVERIFY2(!button->text().contains(QStringLiteral("mute"), Qt::CaseInsensitive),
                     qPrintable(button->text()));
        }
        for (QLabel* label : applet.findChildren<QLabel*>()) {
            QVERIFY(label->text() != QStringLiteral("AF"));
            QVERIFY(!label->text().contains(QStringLiteral("volume"), Qt::CaseInsensitive));
        }
        QCOMPARE(applet.findChildren<QSlider*>().size(), 3);  // pan, squelch, AGC-T
    }
};

QTEST_MAIN(TestRxAppletSliceAccess)
#include "tst_rx_applet_slice_access.moc"
