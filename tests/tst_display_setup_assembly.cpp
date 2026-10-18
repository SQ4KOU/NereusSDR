// no-port-check: NereusSDR-original regression for the approved native
// Display page assembly. J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest/QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleOptionSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include "models/PanadapterModel.h"
#include <QSlider>
#include <QStackedWidget>
#include <QTreeWidget>
#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"
#include "core/ControlRanges.h"
#include "gui/SetupDialog.h"
#include "gui/SetupPage.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"
#include "gui/setup/DisplaySetupPages.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

// Counts transient changes as well as settled geometry: a runtime level
// notification must not reveal a disabled explanation that was deduplicated.
class DisplayRowEventCounter final : public QObject {
public:
    int visibilityChanges{0};
    int geometryChanges{0};
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            ++visibilityChanges;
        }
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            ++geometryChanges;
        }
        return false;
    }
};

class TstDisplaySetupAssembly : public QObject {
    Q_OBJECT
    static QStringList pages() {
        return {"Spectrum", "Levels", "Waterfall", "View", "Peaks & Markers",
                "FFT & Engine", "Transmit Display", "Multimeter"};
    }
    static QWidget* selected(SetupDialog& dialog, const QString& name) {
        dialog.selectPage(name);
        QStackedWidget* stack = dialog.findChild<QStackedWidget*>();
        return stack ? stack->currentWidget() : nullptr;
    }
    static QWidget* byId(QWidget* parent, const QString& id) {
        for (QWidget* widget : parent->findChildren<QWidget*>()) {
            if (widget->property("nereusSetupId").toString() != id) { continue; }
            bool hiddenSource = false;
            for (QWidget* owner = widget->parentWidget(); owner && owner != parent; owner = owner->parentWidget()) {
                hiddenSource = hiddenSource || owner->objectName() == QStringLiteral("displayBoundSourceOwners");
            }
            if (!hiddenSource) { return widget; }
        }
        return nullptr;
    }
private slots:
    void init() { AppSettings::instance().clear(); }

    void runtimeWaterfallLevelsKeepDisplayRowsStable_data() {
        QTest::addColumn<QString>("pageName");
        QTest::newRow("spectrum") << QStringLiteral("Spectrum");
        QTest::newRow("peaks-and-markers") << QStringLiteral("Peaks & Markers");
    }

    void runtimeWaterfallLevelsKeepDisplayRowsStable() {
        QFETCH(QString, pageName);
        SpectrumWidget pan;
        pan.setPanIndex(0); pan.loadSettings();
        pan.setSpectrumRenderMode(1);
        pan.setActivePeakHoldEnabled(false);
        pan.setPeakBlobsEnabled(false);
        pan.setShowNoiseFloor(false);
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setDisplayPanIndex(0);
        QWidget* page = selected(dialog, pageName);
        QVERIFY(page);
        dialog.show();
        QCoreApplication::processEvents();
        DisplayRowEventCounter counter;
        int hiddenReasons = 0;
        for (QLabel* label : page->findChildren<QLabel*>()) {
            label->installEventFilter(&counter);
            if (label->objectName().startsWith(QStringLiteral("setupReason"))
                && !label->text().isEmpty() && label->isHidden()) { ++hiddenReasons; }
        }
        QVERIFY(hiddenReasons > 0);
        for (int frame = 0; frame < 5; ++frame) {
            pan.displaySettingChanged(QStringLiteral("DisplayWfEffectiveLevels"));
            QCoreApplication::processEvents();
        }
        qInfo() << pageName << "runtime label events: visibility" << counter.visibilityChanges
                << "geometry" << counter.geometryChanges;
        QCOMPARE(counter.visibilityChanges, 0);
        QCOMPARE(counter.geometryChanges, 0);
    }

    void transmitScaleRowsAreVisibleSharedPairsAndFollowExternalEditsWithoutEcho() {
        SpectrumWidget pan0, pan1;
        pan0.setPanIndex(0); pan1.setPanIndex(1);
        pan0.loadSettings(); pan1.loadSettings();
        pan0.setTransmitDbmRange(-80, 20);
        RadioModel model;
        model.setSpectrumWidget(&pan0);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan0, &pan1}; });
        QWidget* page = selected(dialog, QStringLiteral("Transmit Display"));
        QVERIFY(page);
        dialog.show();
        QCoreApplication::processEvents();
        auto* maximum = qobject_cast<QSpinBox*>(byId(page, QStringLiteral("display.txDisplay.dbMax")));
        auto* minimum = qobject_cast<QSpinBox*>(byId(page, QStringLiteral("display.txDisplay.dbMin")));
        QVERIFY(maximum);
        QVERIFY(minimum);
        QVERIFY(maximum->isVisibleTo(page)); QVERIFY(minimum->isVisibleTo(page));
        QCOMPARE(maximum->minimum(), -200); QCOMPARE(maximum->maximum(), 200);
        QCOMPARE(minimum->minimum(), -200); QCOMPARE(minimum->maximum(), 200);
        QCOMPARE(maximum->singleStep(), 1); QCOMPARE(minimum->singleStep(), 1);
        QCOMPARE(maximum->value(), 20); QCOMPARE(minimum->value(), -80);
        auto* maxSlider = maximum->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
        auto* minSlider = minimum->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(maxSlider); QVERIFY(minSlider);
        const auto caption = [](QSpinBox* number) {
            QWidget* field = number->parentWidget();
            QWidget* parent = field->parentWidget();
            QList<QFormLayout*> forms = parent->findChildren<QFormLayout*>();
            if (auto* form = qobject_cast<QFormLayout*>(parent->layout())) { forms.prepend(form); }
            for (QFormLayout* form : forms) {
                if (auto* label = qobject_cast<QLabel*>(form->labelForField(field))) { return label->text(); }
            }
            for (QLabel* label : parent->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (label->text() == QStringLiteral("dB Max:") || label->text() == QStringLiteral("dB Min:")) { return label->text(); }
            }
            return QString();
        };
        QCOMPARE(caption(maximum), QStringLiteral("dB Max:"));
        QCOMPARE(caption(minimum), QStringLiteral("dB Min:"));
        QCOMPARE(maxSlider->minimum(), -200); QCOMPARE(maxSlider->maximum(), 200);
        QCOMPARE(minSlider->minimum(), -200); QCOMPARE(minSlider->maximum(), 200);
        QSignalSpy changes(&pan0, &SpectrumWidget::displaySettingChanged);
        const auto storedChanges = [&changes] {
            int count = 0;
            for (const QList<QVariant>& change : changes) {
                const QString key = change.front().toString();
                if (key == QStringLiteral("DisplayTxGridRefLevel") || key == QStringLiteral("DisplayTxGridDynamicRange")) { ++count; }
            }
            return count;
        };
        maximum->setValue(45);
        QCOMPARE(pan0.transmitRefLevel(), 45.0f); QCOMPARE(pan1.transmitRefLevel(), 45.0f);
        QCOMPARE(pan1.transmitDynamicRange(), 125.0f);
        QCOMPARE(minimum->value(), -80);
        QCOMPARE(maxSlider->value(), 45);
        QCOMPARE(storedChanges(), 2);
        changes.clear();
        minSlider->setValue(-100);
        QCOMPARE(minimum->value(), -100);
        QCOMPARE(pan0.transmitDynamicRange(), 145.0f); QCOMPARE(pan1.transmitDynamicRange(), 145.0f);
        QCOMPARE(pan1.transmitRefLevel(), 45.0f);
        QCOMPARE(storedChanges(), 2);
        changes.clear();
        pan1.setTransmitDbmRange(-100, 30);
        QCOMPARE(maximum->value(), 30); QCOMPARE(minimum->value(), -100);
        QCOMPARE(maxSlider->value(), 30); QCOMPARE(minSlider->value(), -100);
        QCOMPARE(storedChanges(), 2); // Only the external shared setter, no UI echo.
        QCOMPARE(AppSettings::instance().value("DisplayTxGridRefLevel").toInt(), 30);
        QCOMPARE(AppSettings::instance().value("DisplayTxGridDynamicRange").toInt(), 130);
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayTxGridRefLevel"), 0));
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayTxGridDynamicRange"), 1));
        changes.clear();
        minimum->setValue(200); // Backend retains at least 10 dB, reseeding the endpoint.
        QCOMPARE(pan0.transmitDynamicRange(), 10.0f);
        QCOMPARE(minimum->value(), 20); QCOMPARE(minSlider->value(), 20);
        QCOMPARE(maximum->value(), 30); QCOMPARE(storedChanges(), 2);
        changes.clear();
        minSlider->setValue(-200); // Backend retains at most 200 dB.
        QCOMPARE(pan0.transmitDynamicRange(), 200.0f);
        QCOMPARE(minimum->value(), -170); QCOMPARE(minSlider->value(), -170);
        QCOMPARE(maximum->value(), 30); QCOMPARE(storedChanges(), 2);
    }

    void importedTransmitEndpointBelowSliderRangeIsShownWithoutWriting() {
        SpectrumWidget pan;
        pan.setPanIndex(0); pan.loadSettings();
        pan.setTransmitDbmRange(-210, -200);
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan}; });
        QSignalSpy changes(&pan, &SpectrumWidget::displaySettingChanged);
        QWidget* page = selected(dialog, QStringLiteral("Transmit Display"));
        QVERIFY(page);
        auto* maximum = qobject_cast<QSpinBox*>(byId(page, QStringLiteral("display.txDisplay.dbMax")));
        auto* minimum = qobject_cast<QSpinBox*>(byId(page, QStringLiteral("display.txDisplay.dbMin")));
        QVERIFY(maximum); QVERIFY(minimum);
        auto* slider = minimum->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(slider);
        QCOMPARE(maximum->value(), -200); QCOMPARE(minimum->value(), -210);
        QCOMPARE(slider->minimum(), -200); QCOMPARE(slider->maximum(), 200);
        QCOMPARE(slider->value(), -200);
        QCOMPARE(changes.size(), 0);
        QCOMPARE(AppSettings::instance().value("DisplayTxGridRefLevel").toInt(), -200);
        QCOMPARE(AppSettings::instance().value("DisplayTxGridDynamicRange").toInt(), 10);
        pan.setTransmitDbmRange(-390, -190);
        QCOMPARE(maximum->value(), -190); QCOMPARE(minimum->value(), -390);
        QCOMPARE(slider->value(), -200);
        int storedChanges = 0;
        for (const QList<QVariant>& change : changes) {
            if (change.front().toString() == QStringLiteral("DisplayTxGridRefLevel") || change.front().toString() == QStringLiteral("DisplayTxGridDynamicRange")) { ++storedChanges; }
        }
        QCOMPARE(storedChanges, 2); // External setter only; no clipping write-back.
        QCOMPARE(AppSettings::instance().value("DisplayTxGridDynamicRange").toInt(), 200);
    }

    void sharedPagesExplainTheirStationAndComputerOwners() {
        RadioModel model;
        SetupDialog dialog(&model);
        dialog.show();
        const QList<QStringList> groups = {
            {"FFT & Engine", "FFT size", "CORE"},
            {"FFT & Engine", "Frame rate", "CORE"},
            {"FFT & Engine", "This computer", "THIS COMPUTER"},
            {"Transmit Display", "FFT", "CORE"},
            {"Transmit Display", "Panadapter", "CORE"},
            {"Transmit Display", "Transmit scale", "THIS COMPUTER"},
            {"Transmit Display", "Waterfall", "CORE"},
            {"Transmit Display", "Waterfall Amplitude Scale", "THIS COMPUTER"},
            {"Multimeter", "Signal Units", "THIS COMPUTER"},
            {"Multimeter", "Signal History", "THIS COMPUTER"}};
        for (const QStringList& expected : groups) {
            QWidget* page = selected(dialog, expected.at(0));
            QVERIFY(page);
            QGroupBox* matching = nullptr;
            for (QGroupBox* group : page->findChildren<QGroupBox*>()) {
                if (group->title() == expected.at(1) && group->isVisibleTo(page)) { matching = group; break; }
            }
            QVERIFY2(matching, qPrintable(expected.join(QStringLiteral(" / "))));
            int visibleOwnerLabels = 0;
            for (QLabel* label : matching->findChildren<QLabel*>()) {
                if (label->text() == expected.at(2) && label->isVisibleTo(page)) { ++visibleOwnerLabels; }
            }
            QCOMPARE(visibleOwnerLabels, 1);
        }
        QWidget* fft = selected(dialog, QStringLiteral("FFT & Engine"));
        QLabel* fftBanner = fft->findChild<QLabel*>(QStringLiteral("setupDisplayOwnerBanner"));
        QVERIFY(fftBanner);
        QCOMPARE(fftBanner->text(), QStringLiteral("The Core holds these. Every pan uses them, and so does every window connected to this Core."));
        QVERIFY(fftBanner->isVisibleTo(fft));
        QWidget* transmit = selected(dialog, QStringLiteral("Transmit Display"));
        QLabel* txBanner = transmit->findChild<QLabel*>(QStringLiteral("setupDisplayOwnerBanner"));
        QVERIFY(txBanner);
        QCOMPARE(txBanner->text(), QStringLiteral("The pan that transmits is drawn with these while it transmits. The Core holds the three shared groups; this computer holds the rest."));
        QVERIFY(txBanner->isVisibleTo(transmit));
        QWidget* meter = selected(dialog, QStringLiteral("Multimeter"));
        QWidget* polling = byId(meter, QStringLiteral("display.multimeter.pollingDelay"));
        QVERIFY(polling);
        QVERIFY(polling->toolTip().contains(QStringLiteral("Core"), Qt::CaseInsensitive));
        const QList<QPair<QString, QString>> rowOwners = {
            {QStringLiteral("display.multimeter.pollingDelay"), QStringLiteral("the Core holds this")},
            {QStringLiteral("display.multimeter.showDecimal"), QStringLiteral("this computer holds this")}};
        for (const auto& expected : rowOwners) {
            QVERIFY(byId(meter, expected.first));
            QLabel* owner = meter->findChild<QLabel*>(QStringLiteral("setupDisplayRowOwner.") + expected.first);
            QVERIFY(owner);
            QCOMPARE(owner->text(), expected.second);
            QVERIFY(owner->isVisibleTo(meter));
        }
    }

    void disabledFormGroupsShowEachDistinctReasonOnceAndKeepControlTooltips() {
        SpectrumWidget pan;
        pan.setPanIndex(0); pan.loadSettings();
        pan.setActivePeakHoldEnabled(false);
        pan.setPeakBlobsEnabled(false);
        pan.setShowNoiseFloor(false);
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setDisplayPanIndex(0);
        QWidget* page = selected(dialog, QStringLiteral("Peaks & Markers"));
        QVERIFY(page);
        dialog.show(); QCoreApplication::processEvents();
        const QStringList reasons = {QStringLiteral("Peak trace is off."), QStringLiteral("Peak markers are off."), QStringLiteral("Noise Floor is off.")};
        for (const QString& why : reasons) {
            int displayed = 0, disabledControls = 0;
            for (QLabel* label : page->findChildren<QLabel*>()) {
                if (label->objectName().startsWith(QStringLiteral("setupReason")) && label->isVisibleTo(page) && label->text() == why) { ++displayed; }
            }
            for (QWidget* control : page->findChildren<QWidget*>()) {
                if (!control->property("nereusSetupId").toString().isEmpty() && !control->isEnabled() && control->toolTip() == why) { ++disabledControls; }
            }
            QVERIFY2(disabledControls > 1, qPrintable(why));
            QCOMPARE(displayed, 1);
        }
        pan.setActivePeakHoldEnabled(true);
        QCoreApplication::processEvents();
        for (QLabel* label : page->findChildren<QLabel*>()) {
            QVERIFY(!(label->objectName().startsWith(QStringLiteral("setupReason")) && label->isVisibleTo(page) && label->text() == QStringLiteral("Peak trace is off.")));
        }
    }

    void selectedTransmittingLevelsGateEveryTrimAndRestoreReceiveModeWithoutWrites() {
        SpectrumWidget pan0, pan1;
        pan0.setPanIndex(0); pan1.setPanIndex(1);
        pan0.loadSettings(); pan1.loadSettings();
        pan0.setAdjustGridMinToNoiseFloor(true);
        pan1.setAdjustGridMinToNoiseFloor(true);
        pan0.setWfBlackLevel(61); pan0.setWfColorGain(67);
        pan1.setWfBlackLevel(54); pan1.setWfColorGain(63);
        RadioModel model;
        model.setSpectrumWidget(&pan0);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan0, &pan1}; });
        dialog.setDisplayPanIndex(0);
        QWidget* page = selected(dialog, QStringLiteral("Levels"));
        QVERIFY(page);
        QStringList ids = {QStringLiteral("display.levels.blackLevel"), QStringLiteral("display.levels.colorGain"),
            QStringLiteral("display.gridScales.noiseFloorOffset"), QStringLiteral("display.gridScales.maintainGridRange")};
        for (int index = 0; index < 2; ++index) { ids.append(QStringLiteral("display.levels.spectrumMode.%1").arg(index)); }
        for (int index = 0; index < 5; ++index) { ids.append(QStringLiteral("display.levels.waterfallMode.%1").arg(index)); }
        const QStringList keys = {QStringLiteral("DisplayWfBlackLevel"), QStringLiteral("DisplayWfColorGain"),
            QStringLiteral("DisplayNFOffsetGridFollow"), QStringLiteral("DisplayMaintainNFAdjustDelta")};
        QStringList rx0, rx1;
        for (const QString& key : keys) { rx0.append(pan0.perPanDisplayValue(key)); rx1.append(pan1.perPanDisplayValue(key)); }
        const auto assertUnchanged = [&]() {
            for (int index = 0; index < keys.size(); ++index) {
                QCOMPARE(pan0.perPanDisplayValue(keys[index]), rx0[index]);
                QCOMPARE(pan1.perPanDisplayValue(keys[index]), rx1[index]);
            }
        };
        QString phase = QStringLiteral("initial receive");
        const auto assertAvailable = [page, &phase](const QString& id, bool available) {
            QWidget* control = byId(page, id);
            QVERIFY2(control, qPrintable(id));
            QVERIFY2(control->isEnabled() == available, qPrintable(QStringLiteral("%1 / %2: control enabled=%3 expected=%4").arg(phase, id).arg(control->isEnabled()).arg(available)));
            QWidget* field = control;
            if (qobject_cast<QSlider*>(control) || qobject_cast<QAbstractSpinBox*>(control)) {
                field = control->parentWidget();
                for (QSlider* slider : field->findChildren<QSlider*>(QString(), Qt::FindDirectChildrenOnly)) { QCOMPARE(slider->isEnabled(), available); }
                for (QAbstractSpinBox* number : field->findChildren<QAbstractSpinBox*>(QString(), Qt::FindDirectChildrenOnly)) { QCOMPARE(number->isEnabled(), available); }
                for (QLabel* label : field->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) { QCOMPARE(label->isEnabled(), available); }
            }
            // The retained source rows may be a QWidget row or a nested
            // QHBoxLayout in a group; prove the actual caption gates too.
            QWidget* parent = field->parentWidget();
            QList<QHBoxLayout*> layouts = parent->findChildren<QHBoxLayout*>();
            if (auto* layout = qobject_cast<QHBoxLayout*>(parent->layout())) { layouts.prepend(layout); }
            for (QHBoxLayout* layout : layouts) {
                if (layout->indexOf(field) < 0) { continue; }
                for (int index = 0; index < layout->count(); ++index) {
                    if (auto* label = qobject_cast<QLabel*>(layout->itemAt(index)->widget())) { QCOMPARE(label->isEnabled(), available); }
                }
            }
            if (!available) { QVERIFY(control->toolTip().contains(QStringLiteral("transmit"), Qt::CaseInsensitive)); }
        };
        for (const QString& id : ids) { assertAvailable(id, true); }
        // Match the existing key-edge fixtures: zero hold and band context
        // 0. The window key edge reaches both pans; only Pan 1 transmits.
        pan0.setDisplayKeyed(true, 0, 0);
        pan1.setDisplayKeyed(true, 0, 0);
        pan0.setMoxOverlay(true);
        QVERIFY(pan0.isMoxOverlayActive()); QVERIFY(pan0.displayKeyed());
        QVERIFY(pan1.displayKeyed());
        phase = QStringLiteral("first transmit onset");
        for (const QString& id : ids) { assertAvailable(id, false); }
        assertUnchanged();
        dialog.setDisplayPanIndex(1);
        QVERIFY(!pan1.isMoxOverlayActive());
        phase = QStringLiteral("other receive scope during window key");
        for (const QString& id : ids) { assertAvailable(id, true); }
        dialog.setDisplayPanIndex(-1);
        phase = QStringLiteral("All scope during transmit");
        for (int index = 0; index < ids.size(); ++index) {
            if (index != 2 && index != 3) { assertAvailable(ids[index], true); }
        }
        dialog.setDisplayPanIndex(0);
        phase = QStringLiteral("transmitting scope restored");
        for (const QString& id : ids) { assertAvailable(id, false); }
        pan0.setDisplayKeyed(false, 0, 0);
        pan1.setDisplayKeyed(false, 0, 0);
        pan0.setMoxOverlay(false);
        QVERIFY(!pan0.isMoxOverlayActive()); QVERIFY(!pan0.displayKeyed());
        phase = QStringLiteral("first unkey Track floor restored");
        for (const QString& id : ids) { assertAvailable(id, true); }
        assertUnchanged();
        pan0.setAdjustGridMinToNoiseFloor(false);
        pan0.setDisplayKeyed(true, 0, 0);
        pan1.setDisplayKeyed(true, 0, 0);
        pan0.setMoxOverlay(true);
        phase = QStringLiteral("second transmit onset from Manual");
        for (const QString& id : ids) { assertAvailable(id, false); }
        pan0.setDisplayKeyed(false, 0, 0);
        pan1.setDisplayKeyed(false, 0, 0);
        pan0.setMoxOverlay(false);
        phase = QStringLiteral("second unkey Manual restored");
        assertAvailable(ids[0], true); assertAvailable(ids[1], true);
        for (const QString& id : {ids[2], ids[3]}) {
            QWidget* control = byId(page, id);
            QVERIFY(control); QVERIFY(!control->isEnabled());
            QVERIFY(control->toolTip().contains(QStringLiteral("Track floor")));
        }
        assertUnchanged();
    }

    void manualScaleCaptionsAndNotesStayEnabledWhenTrackingRowsAreUnavailable() {
        AppSettings::instance().setValue(QStringLiteral("DisplayAdjustGridMinToNoiseFloor"), QStringLiteral("False"));
        SpectrumWidget pan;
        pan.setPanIndex(0); pan.loadSettings();
        PanadapterModel band;
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setBandGridModelProvider([&] { return &band; });
        dialog.setDisplayPanIndex(-1);
        QWidget* page = selected(dialog, QStringLiteral("Levels"));
        QVERIFY(page);
        QWidget* maximum = byId(page, QStringLiteral("display.gridScales.dbMax"));
        QWidget* minimum = byId(page, QStringLiteral("display.gridScales.dbMin"));
        QVERIFY(maximum); QVERIFY(minimum);
        QVERIFY(maximum->isEnabled()); QVERIFY(minimum->isEnabled());
        QWidget* offset = byId(page, QStringLiteral("display.gridScales.noiseFloorOffset"));
        QWidget* keep = byId(page, QStringLiteral("display.gridScales.maintainGridRange"));
        QVERIFY(offset); QVERIFY(keep);
        QVERIFY(!offset->isEnabled()); QVERIFY(!keep->isEnabled());
        QGroupBox* scale = nullptr;
        for (QWidget* parent = maximum->parentWidget(); parent; parent = parent->parentWidget()) {
            if ((scale = qobject_cast<QGroupBox*>(parent))) { break; }
        }
        QVERIFY(scale);
        int captions = 0, notes = 0;
        for (QLabel* label : scale->findChildren<QLabel*>()) {
            if (label->text().endsWith(QLatin1Char(':'))
                && (label->text().startsWith(QStringLiteral("dB Max")) || label->text().startsWith(QStringLiteral("dB Min")))) {
                ++captions;
                QVERIFY2(label->isEnabled(), qPrintable(label->text()));
            }
            if (label->objectName() == QStringLiteral("setupBandScaleNote") || label->text().startsWith(QStringLiteral("You set dB Max and dB Min."))) {
                ++notes;
                QVERIFY2(label->isEnabled(), qPrintable(label->text()));
            }
        }
        QCOMPARE(captions, 2);
        QVERIFY(notes >= 2);
    }

    void displayTreeHasExactlyTheEightApprovedPagesAndLegacyAliases() {
        RadioModel model;
        SetupDialog dialog(&model);
        QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
        QVERIFY(tree);
        QStringList labels;
        for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            QTreeWidgetItem* category = tree->topLevelItem(index);
            if (category->text(0) != QStringLiteral("Display")) { continue; }
            for (int child = 0; child < category->childCount(); ++child) {
                labels.append(category->child(child)->text(0));
            }
        }
        QCOMPARE(labels, pages());
        const QList<QPair<QString, QString>> aliases = {
            {"Spectrum Defaults", "Spectrum"}, {"Grid & Scales", "Levels"},
            {"Waterfall Defaults", "Waterfall"}, {"3D View", "View"},
            {"Spectrum Peaks", "Peaks & Markers"}, {"TX Display", "Transmit Display"}};
        for (const auto& alias : aliases) {
            QWidget* canonical = selected(dialog, alias.second);
            QVERIFY(canonical);
            dialog.selectPage(alias.first);
            QCOMPARE(tree->currentItem()->text(0), alias.second);
            QCOMPARE(dialog.findChild<QStackedWidget*>()->currentWidget(), canonical);
        }
    }

    // Each Display pill tab's "Setup defaults" link opens the Setup page
    // that holds that tab's defaults. FFT's sizing, size, window, Hz/bin
    // and frame rate are on FFT & Engine, not on Spectrum.
    void pillSetupDefaultsOpensTheTabsSetupPage() {
        RadioModel model;
        SetupDialog dialog(&model);
        QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
        QVERIFY(tree);
        SpectrumWidget pan;
        pan.resize(900, 600);
        pan.loadSettings();
        auto* panel = new SpectrumOverlayPanel(&pan);
        connect(panel, &SpectrumOverlayPanel::openSetupRequested,
                &dialog, &SetupDialog::selectPage);
        using Tab = SpectrumOverlayPanel::DisplayPillTab;
        const struct {
            Tab tab;
            const char* pillPage;
            const char* setupPage;
            const char* control;
        } tabs[] = {
            {Tab::Spectrum, "displayPageSpectrum", "Spectrum", "display.spectrumDefaults.detector"},
            {Tab::Levels, "displayPageLevels", "Levels", "display.gridScales.dbMax"},
            {Tab::Fft, "displayPageFft", "FFT & Engine", "display.spectrumDefaults.fftSize"},
            {Tab::Waterfall, "displayPageWaterfall", "Waterfall",
             "display.waterfallDefaults.colorScheme"},
            {Tab::View3D, "displayPage3D", "View", "display.threeD.gain"},
        };
        for (const auto& tab : tabs) {
            // Start on another Display page, so the click has to move.
            dialog.selectPage(QStringLiteral("Multimeter"));
            QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Multimeter"));
            panel->setDisplayPillTab(tab.tab);
            QWidget* page = pan.findChild<QWidget*>(QString::fromLatin1(tab.pillPage));
            QVERIFY2(page, tab.pillPage);
            auto* link = page->findChild<QPushButton*>(QStringLiteral("pillSetupDefaults"));
            QVERIFY2(link, tab.pillPage);
            link->click();
            QCOMPARE(tree->currentItem()->text(0), QString::fromLatin1(tab.setupPage));
            QWidget* shown = dialog.findChild<QStackedWidget*>()->currentWidget();
            QVERIFY2(byId(shown, QString::fromLatin1(tab.control)), tab.control);
        }
    }

    void acceptedControlsLiveOnTheirPagesWithOneSourceBuilder() {
        RadioModel model;
        SetupDialog dialog(&model);
        const QList<QPair<QString, QStringList>> membership = {
            {"Spectrum", {"display.spectrumDefaults.detector", "display.spectrumDefaults.normalize",
                "display.spectrumDefaults.averageTime", "display.gridScales.dbStep",
                "display.spectrum.traceColor", "display.spectrum.fillColor",
                "display.spectrum.gridSpacing", "display.spectrum.scaleText"}},
            {"Levels", {"display.gridScales.dbMax", "display.gridScales.dbMin",
                "display.gridScales.noiseFloorOffset", "display.waterfallDefaults.lowThreshold",
                "display.waterfallDefaults.highThreshold", "display.levels.blackLevel",
                "display.levels.colorGain", "display.levels.retune"}},
            {"Waterfall", {"display.waterfallDefaults.colorScheme", "display.waterfallDefaults.updatePeriod",
                "display.waterfallDefaults.averageTime", "display.waterfallDefaults.timestampMode",
                "display.waterfall.nbBlank", "display.waterfall.nbThreshold"}},
            {"View", {"display.threeD.gain", "display.threeD.span", "display.threeD.floor"}},
            {"Peaks & Markers", {"display.spectrumDefaults.showNoiseFloor",
                "display.spectrumDefaults.showPeakValue", "display.spectrumDefaults.showBinWidth"}},
            {"FFT & Engine", {"display.spectrumDefaults.fftSize", "display.spectrumDefaults.window",
                "display.spectrumDefaults.hzPerBinTarget", "display.spectrumDefaults.fps"}},
            {"Transmit Display", {"display.txDisplay.fftSize", "display.txDisplay.wfLowLevel"}}
        };
        for (const auto& page : membership) {
            QWidget* widget = selected(dialog, page.first);
            QVERIFY(widget);
            for (const QString& id : page.second) {
                QVERIFY2(byId(widget, id), qPrintable(page.first + QStringLiteral(": ") + id));
                int copies = 0;
                for (QWidget* control : dialog.findChildren<QWidget*>()) {
                    copies += control->property("nereusSetupId").toString() == id ? 1 : 0;
                }
                QCOMPARE(copies, 1);
            }
        }
        QWidget* levelsBand = byId(selected(dialog, QStringLiteral("Levels")), QStringLiteral("display.selectedBand"));
        QWidget* viewBand = byId(selected(dialog, QStringLiteral("View")), QStringLiteral("display.selectedBand"));
        QVERIFY(levelsBand);
        QVERIFY(viewBand);
        QVERIFY(levelsBand != viewBand);
        QCOMPARE(levelsBand->parentWidget()->property("displayAssemblyGroupTitle").toString(), QStringLiteral("Spectrum scale"));
        QCOMPARE(viewBand->parentWidget()->property("displayAssemblyGroupTitle").toString(), QStringLiteral("3D floor"));
        QCOMPARE(dialog.findChildren<SpectrumDefaultsPage*>().size(), 1);
        QCOMPARE(dialog.findChildren<WaterfallDefaultsPage*>().size(), 1);
        QCOMPARE(dialog.findChildren<GridScalesPage*>().size(), 1);
        QCOMPARE(dialog.findChildren<Display3DSetupPage*>().size(), 1);
    }

    void gradientEditorsAreWholeRowsImmediatelyAfterTheirAcceptedAnchors() {
        RadioModel model;
        SetupDialog dialog(&model);
        const auto groupOf = [](QWidget* widget) {
            for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
                if (auto* group = qobject_cast<QGroupBox*>(parent)) { return group; }
            }
            return static_cast<QGroupBox*>(nullptr);
        };
        const QList<QStringList> rows = {
            {"Waterfall", "display.waterfall.customGradient", "display.waterfallDefaults.colorScheme", "Drawing"},
            {"Transmit Display", "display.txDisplay.customGradient", "display.txDisplay.wfLowColor", "Waterfall Amplitude Scale"}};
        for (const QStringList& row : rows) {
            QWidget* page = selected(dialog, row.at(0));
            QVERIFY(page);
            QWidget* editor = byId(page, row.at(1));
            QWidget* anchor = byId(page, row.at(2));
            QVERIFY(editor);
            QVERIFY(anchor);
            QGroupBox* group = groupOf(anchor);
            QVERIFY(group);
            QCOMPARE(group->title(), row.at(3));
            QCOMPARE(editor->parentWidget(), group);
            QWidget* wholeRow = anchor;
            while (wholeRow->parentWidget() != group) { wholeRow = wholeRow->parentWidget(); }
            if (auto* column = qobject_cast<QBoxLayout*>(group->layout())) {
                QCOMPARE(column->indexOf(editor), column->indexOf(wholeRow) + 1);
            } else {
                auto* form = qobject_cast<QFormLayout*>(group->layout());
                QVERIFY(form);
                int anchorIndex = -1, editorIndex = -1;
                QFormLayout::ItemRole role;
                form->getWidgetPosition(wholeRow, &anchorIndex, &role);
                form->getWidgetPosition(editor, &editorIndex, &role);
                QCOMPARE(editorIndex, anchorIndex + 1);
            }
        }
    }

    void ownRowDotReadsPendingValuesAndResetsOnlyTheSelectedRow() {
        SpectrumWidget pan;
        pan.setPanIndex(3);
        pan.loadSettings();
        const float inherited = pan.lineWidth();
        pan.setWfOpacity(71);
        pan.setLineWidth(2.2f);
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setDisplayPanIndex(3);
        const QStringList before = AppSettings::instance().allKeys();
        QWidget* spectrum = selected(dialog, QStringLiteral("Spectrum"));
        QVERIFY(spectrum);
        auto* dot = spectrum->findChild<QPushButton*>(QStringLiteral("setupOwnDisplayLineWidth"));
        QVERIFY(dot);
        QVERIFY(!dot->isHidden());
        QVERIFY(dot->toolTip().contains(QStringLiteral("Pan 4")));
        QVERIFY(dot->toolTip().contains(QStringLiteral("Line Width")));
        QCOMPARE(AppSettings::instance().allKeys(), before);
        dot->click();
        QCOMPARE(pan.lineWidth(), inherited);
        QCOMPARE(pan.wfOpacity(), 71);
        QVERIFY(dot->isHidden());
        dialog.setDisplayPanIndex(-1);
        QVERIFY(dot->isHidden());
    }

    void trackedScaleAndTrimNotesFollowLiveStateWithoutWriting() {
        SpectrumWidget pan;
        pan.setPanIndex(3);
        pan.loadSettings();
        pan.setClarityEnabled(false);
        pan.setWfAgcEnabled(false);
        pan.setWfUseSpectrumMinMax(false);
        pan.setAdjustGridMinToNoiseFloor(true);
        pan.setMaintainNFAdjustDelta(true);
        pan.applyGridFollow(-112.0f, true);
        PanadapterModel band;
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setBandGridModelForPanProvider([&band](int) { return &band; });
        dialog.setDisplayPanIndex(3);
        QWidget* levels = selected(dialog, QStringLiteral("Levels"));
        QVERIFY(levels);
        auto* low = qobject_cast<QSpinBox*>(byId(levels, QStringLiteral("display.gridScales.dbMin")));
        auto* high = qobject_cast<QSpinBox*>(byId(levels, QStringLiteral("display.gridScales.dbMax")));
        QVERIFY(low);
        QVERIFY(high);
        QCOMPARE(low->value(), qRound(pan.refLevel() - pan.dynamicRange()));
        QCOMPARE(high->value(), qRound(pan.refLevel()));
        QVERIFY(!low->isEnabled());
        QVERIFY(!high->isEnabled());
        auto* note = levels->findChild<QLabel*>(QStringLiteral("setupWaterfallModeNote"));
        QVERIFY(note);
        pan.setWfBlackLevel(60);
        QTRY_VERIFY(note->text().contains(QStringLiteral("Black Level and Color Gain trim both.")));
        QHash<QString, QVariant> values;
        for (const QString& key : AppSettings::instance().allKeys()) { values.insert(key, AppSettings::instance().value(key)); }
        pan.applyGridFollow(-104.0f, true);
        QTRY_COMPARE(low->value(), qRound(pan.refLevel() - pan.dynamicRange()));
        QTRY_COMPARE(high->value(), qRound(pan.refLevel()));
        for (const QString& key : AppSettings::instance().allKeys()) { QCOMPARE(AppSettings::instance().value(key), values.value(key)); }
        // Closed Pan8 is explicitly Track floor; merely selecting it
        // would otherwise inherit Manual, whose stored number is known.
        PanDisplayKeys::writeSparse(QStringLiteral("DisplayAdjustGridMinToNoiseFloor"), 7,
            QStringLiteral("True"), SpectrumWidget::perPanShipDefaultForClosedPan(QStringLiteral("DisplayAdjustGridMinToNoiseFloor")));
        dialog.setDisplayPanIndex(7);
        QCOMPARE(low->specialValueText(), QStringLiteral("no signal"));
    }

    void providerRefreshUpdatesActualClarityStateWithoutLevelWrites() {
        SpectrumWidget pan;
        pan.setPanIndex(3);
        pan.loadSettings();
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setDisplayPanIndex(3);
        SetupDisplayClarityStatus status{true, true, false, true};
        const auto provider = [&status](int index) -> std::optional<SetupDisplayClarityStatus> {
            return index == 3 ? std::optional<SetupDisplayClarityStatus>(status) : std::nullopt;
        };
        dialog.setDisplayClarityStatusProvider(provider);
        QWidget* levels = selected(dialog, QStringLiteral("Levels"));
        auto* retune = qobject_cast<QPushButton*>(byId(levels, QStringLiteral("display.levels.retune")));
        QVERIFY(retune);
        QCOMPARE(retune->property("clarityState").toString(), QStringLiteral("active"));
        const QString low = pan.perPanDisplayValue(QStringLiteral("DisplayWfLowLevel"));
        const QString high = pan.perPanDisplayValue(QStringLiteral("DisplayWfHighLevel"));
        QHash<QString, QVariant> stored;
        for (const QString& key : AppSettings::instance().allKeys()) { stored.insert(key, AppSettings::instance().value(key)); }
        status.paused = true;
        dialog.setDisplayClarityStatusProvider(provider);
        QCOMPARE(retune->property("clarityState").toString(), QStringLiteral("paused"));
        status.paused = false;
        status.active = false;
        status.sourceAvailable = false;
        dialog.setDisplayClarityStatusProvider(provider);
        QCOMPARE(retune->property("clarityState").toString(), QStringLiteral("off"));
        QVERIFY(!retune->property("claritySourceAvailable").toBool());
        QCOMPARE(pan.perPanDisplayValue(QStringLiteral("DisplayWfLowLevel")), low);
        QCOMPARE(pan.perPanDisplayValue(QStringLiteral("DisplayWfHighLevel")), high);
        QCOMPARE(AppSettings::instance().allKeys().size(), stored.size());
        for (const QString& key : AppSettings::instance().allKeys()) { QCOMPARE(AppSettings::instance().value(key), stored.value(key)); }
    }

    void everyAssembledNumberRowHasAnActualPairedSlider() {
        RadioModel model;
        SetupDialog dialog(&model);
        int numberRows = 0;
        for (const QString& name : pages()) {
            QWidget* page = selected(dialog, name);
            QVERIFY(page);
            for (QAbstractSpinBox* number : page->findChildren<QAbstractSpinBox*>()) {
                if (!number->isVisibleTo(page)) { continue; }
                const QList<QSlider*> sliders = number->parentWidget()->findChildren<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
                QVERIFY2(sliders.size() == 1, qPrintable(name + QStringLiteral(": number row has no unique shared slider")));
                ++numberRows;
            }
        }
        QVERIFY(numberRows >= 30);
    }

    void retainedNumberPairUsesOneScopedWriteAndFollowsQuietModelSeeds() {
        SpectrumWidget pan0, pan1;
        pan0.setPanIndex(0); pan1.setPanIndex(1);
        pan0.loadSettings(); pan1.loadSettings();
        pan0.setSpectrumAveraging(SpectrumAveraging::Recursive);
        pan1.setSpectrumAveraging(SpectrumAveraging::Recursive);
        RadioModel model;
        model.setSpectrumWidget(&pan0);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan0, &pan1}; });
        dialog.setDisplayPanIndex(0);
        QWidget* spectrum = selected(dialog, QStringLiteral("Spectrum"));
        auto* number = qobject_cast<QSpinBox*>(byId(spectrum, QStringLiteral("display.spectrumDefaults.averageTime")));
        QVERIFY(number);
        auto* slider = number->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(slider);
        const int untouched = pan1.spectrumAverageTimeMs();
        QSignalSpy writes(&pan0, &SpectrumWidget::displaySettingChanged);
        number->setValue(137);
        QCOMPARE(pan0.spectrumAverageTimeMs(), 137);
        QCOMPARE(slider->value(), 137);
        QCOMPARE(pan1.spectrumAverageTimeMs(), untouched);
        QCOMPARE(writes.size(), 1);
        writes.clear();
        slider->setValue(183);
        QCOMPARE(number->value(), 183);
        QCOMPARE(pan0.spectrumAverageTimeMs(), 183);
        QCOMPARE(writes.size(), 1);
        writes.clear();
        number->setValue(9999);
        QCOMPARE(number->value(), 9999);
        QCOMPARE(pan0.spectrumAverageTimeMs(), 9999);
        QCOMPARE(slider->value(), 500);
        QCOMPARE(writes.size(), 1);
        pan0.setSpectrumAverageTimeMs(219);
        QCOMPARE(number->value(), 219);
        QCOMPARE(slider->value(), 219);
        writes.clear();
        dialog.setDisplayPanIndex(1);
        QCOMPARE(number->value(), untouched);
        QCOMPARE(slider->value(), untouched);
        QCOMPARE(writes.size(), 0);
    }

    void numberPairAndItsLabelsFollowInitialToggleAndScopeGates() {
        SpectrumWidget pan0, pan1;
        pan0.setPanIndex(0); pan1.setPanIndex(1);
        pan0.loadSettings(); pan1.loadSettings();
        pan0.setWaterfallAveraging(SpectrumAveraging::None);
        pan1.setWaterfallAveraging(SpectrumAveraging::None);
        pan0.setActivePeakHoldEnabled(false);
        RadioModel model;
        model.setSpectrumWidget(&pan0);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&] { return QList<SpectrumWidget*>{&pan0, &pan1}; });
        dialog.setDisplayPanIndex(0);
        QWidget* waterfall = selected(dialog, QStringLiteral("Waterfall"));
        auto* time = qobject_cast<QSpinBox*>(byId(waterfall, QStringLiteral("display.waterfallDefaults.averageTime")));
        QVERIFY(time);
        auto* timeSlider = time->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(timeSlider);
        QVERIFY(!time->isEnabled());
        QVERIFY(!timeSlider->isEnabled());
        QVERIFY(!time->parentWidget()->isEnabled());
        const QList<QLabel*> labels = time->parentWidget()->parentWidget()->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(!labels.isEmpty());
        for (QLabel* label : labels) { QVERIFY(!label->isEnabled()); }
        QVERIFY(time->toolTip().contains(QStringLiteral("None")));
        pan0.setWaterfallAveraging(SpectrumAveraging::Recursive);
        QVERIFY(time->isEnabled());
        QVERIFY(timeSlider->isEnabled());
        QVERIFY(time->parentWidget()->isEnabled());
        for (QLabel* label : labels) { QVERIFY(label->isEnabled()); }
        dialog.setDisplayPanIndex(1);
        QVERIFY(!time->isEnabled());
        QVERIFY(!timeSlider->isEnabled());
        dialog.setDisplayPanIndex(0);
        QWidget* peaks = selected(dialog, QStringLiteral("Peaks & Markers"));
        auto* hold = qobject_cast<QSpinBox*>(byId(peaks, QStringLiteral("display.spectrumPeaks.activePeakHoldTime")));
        QVERIFY(hold);
        auto* holdSlider = hold->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(holdSlider);
        QVERIFY(!hold->isEnabled());
        QVERIFY(!holdSlider->isEnabled());
        QWidget* colour = byId(peaks, QStringLiteral("display.spectrumPeaks.activePeakHoldColor"));
        QVERIFY(colour);
        auto* colourForm = qobject_cast<QFormLayout*>(colour->parentWidget()->layout());
        QVERIFY(colourForm);
        QWidget* colourLabel = colourForm->labelForField(colour);
        QVERIFY(colourLabel);
        QVERIFY(!colour->isEnabled());
        QVERIFY(!colourLabel->isEnabled());
        pan0.setActivePeakHoldEnabled(true);
        QVERIFY(hold->isEnabled());
        QVERIFY(holdSlider->isEnabled());
        QVERIFY(colour->isEnabled());
        QVERIFY(colourLabel->isEnabled());
        QWidget* view = selected(dialog, QStringLiteral("View"));
        QWidget* gain = byId(view, QStringLiteral("display.threeD.gain"));
        QVERIFY(gain);
        QVERIFY(!gain->isEnabled());
        QWidget* speed = byId(view, QStringLiteral("display.threeD.speed"));
        QVERIFY(speed);
        QLabel* speedValue = view->findChild<QLabel*>(QStringLiteral("setup3DSpeedValueLabel"));
        QVERIFY(speedValue);
        QVERIFY(!speed->isEnabled());
        QVERIFY(!speedValue->isEnabled());
        pan0.setSpectrumRenderMode(1);
        QVERIFY(gain->isEnabled());
        QVERIFY(speed->isEnabled());
        QVERIFY(speedValue->isEnabled());
        pan0.setSpectrumRenderMode(0);
        QVERIFY(!speed->isEnabled());
        QVERIFY(!speedValue->isEnabled());
    }

    void multimeterKeepsHiddenLegacyNumbersOutOfTheApprovedVisibleInventory() {
        RadioModel model;
        SetupDialog dialog(&model);
        QWidget* meter = selected(dialog, QStringLiteral("Multimeter"));
        QVERIFY(meter);
        dialog.show();
        WorkspacePreview::themeSubtree(&dialog);
        QCoreApplication::processEvents();
        QStringList visibleNumbers;
        int hiddenLegacy = 0;
        for (QSpinBox* number : meter->findChildren<QSpinBox*>()) {
            if (number->isVisibleTo(meter)) {
                visibleNumbers.append(number->property("nereusSetupId").toString());
                QVERIFY(number->parentWidget()->findChild<QSlider*>(QString(), Qt::FindDirectChildrenOnly));
            } else if (number->isHidden()) {
                ++hiddenLegacy;
                QVERIFY(!number->property("nativeNumberPairReady").toBool());
            }
        }
        visibleNumbers.sort();
        QStringList expected{QStringLiteral("display.multimeter.pollingDelay"), QStringLiteral("display.multimeter.historyDuration")};
        expected.sort();
        QCOMPARE(visibleNumbers, expected);
        QCOMPARE(hiddenLegacy, 4);
    }

    void fillRowsKeepTheApprovedPhysicalOrder() {
        RadioModel model;
        SetupDialog dialog(&model);
        QWidget* spectrum = selected(dialog, QStringLiteral("Spectrum"));
        dialog.show();
        QCoreApplication::processEvents();
        int previousY = -1;
        for (const QString& id : {QStringLiteral("display.spectrumDefaults.panFill"), QStringLiteral("display.spectrumDefaults.fillAlpha"),
                                 QStringLiteral("display.spectrum.fillColor"), QStringLiteral("display.spectrumDefaults.gradient")}) {
            QWidget* control = byId(spectrum, id);
            QVERIFY(control);
            const int y = control->mapTo(spectrum, QPoint()).y();
            QVERIFY2(y > previousY, qPrintable(id));
            previousY = y;
        }
    }

    void nativeSelectedChoicesUseTheSharedCheckedRecipe() {
        WorkspacePreview::setEnabledForTest(true);
        const auto restore = qScopeGuard([] { WorkspacePreview::setEnabledForTest(false); });
        SpectrumWidget pan;
        pan.loadSettings();
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        const QList<QPair<QString, QString>> choices = {
            {"Levels", "display.levels.spectrumMode.0"},
            {"Levels", "display.levels.waterfallMode.0"},
            {"View", "display.view.mode.0"}};
        for (const auto& choice : choices) {
            QWidget* page = selected(dialog, choice.first);
            WorkspacePreview::themeSubtree(page);
            auto* button = qobject_cast<QPushButton*>(byId(page, choice.second));
            QVERIFY(button);
            QVERIFY(button->isChecked());
            QVERIFY(button->styleSheet().contains(QStringLiteral(":checked")));
            QVERIFY(button->property(WorkspacePreview::kThemedQssProperty).isValid());
        }
        QPushButton* all = dialog.findChild<QPushButton*>(QStringLiteral("setupDisplayPanAll"));
        QVERIFY(all);
        QVERIFY(all->isChecked());
        QVERIFY(all->styleSheet().contains(QStringLiteral(":checked")));
    }

    void captureActualNativePagesWhenRequested() {
        const QString directory = qEnvironmentVariable("NEREUS_NATIVE_DISPLAY_CAPTURE_DIR");
        if (directory.isEmpty()) { return; }
        QVERIFY(QDir().mkpath(directory));
        const QFont previousFont = qApp->font();
        const QPalette previousPalette = qApp->palette();
        const QString previousSheet = qApp->styleSheet();
        const auto restore = qScopeGuard([previousFont, previousPalette, previousSheet]() {
            WorkspacePreview::setEnabledForTest(false);
            qApp->setFont(previousFont);
            qApp->setPalette(previousPalette);
            qApp->setStyleSheet(previousSheet);
        });
        WorkspacePreview::setEnabledForTest(true);
        applyDarkPalette(*qApp);
        applyAppBaselineQss(*qApp);
        applyWorkspacePalette(*qApp);
        SpectrumWidget pan;
        pan.loadSettings();
        RadioModel model; // Isolated unit-test model; no transport is started.
        model.setSpectrumWidget(&pan);
        PanadapterModel band;
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        dialog.setBandGridModelProvider([&band] { return &band; });
        dialog.setBandGridModelForPanProvider([&band](int) { return &band; });
        // Keep constructor geometry, widget fonts and shipped control state.
        // The actual palette installs live theming and shared Font B.
        dialog.show();
        QJsonArray captures;
        for (const QString& pageName : pages()) {
            QWidget* page = selected(dialog, pageName);
            QVERIFY(page);
            WorkspacePreview::themeSubtree(&dialog);
            QCoreApplication::processEvents();
            QTest::qWait(20);
            QScrollArea* area = page->findChild<QScrollArea*>();
            QScrollBar* scroll = area ? area->verticalScrollBar() : nullptr;
            const int maximum = scroll ? scroll->maximum() : 0;
            const QStringList slices = maximum > 0 ? QStringList{"top", "middle", "bottom"} : QStringList{"top"};
            for (const QString& slice : slices) {
                if (scroll) { scroll->setValue(slice == QStringLiteral("top") ? 0 : slice == QStringLiteral("middle") ? maximum / 2 : maximum); }
                QCoreApplication::processEvents();
                QTest::qWait(20);
                QJsonArray controls;
            for (QWidget* control : page->findChildren<QWidget*>()) {
                if (!control->isVisibleTo(&dialog)) { continue; }
                const QFont font = control->font();
                const qreal pixels = font.pixelSize() > 0 ? font.pixelSize()
                    : font.pointSizeF() * QFontMetricsF(font, control).fontDpi() / 72.0;
                QVERIFY2(pixels >= 11.0, "Native capture must use the actual shared Font B pipeline");
                const QRect rect(control->mapTo(&dialog, QPoint()), control->size());
                QJsonObject item{{"object", control->objectName()},
                    {"id", control->property("nereusSetupId").toString()},
                    {"key", control->property("nereusDisplayKeyBase").toString()},
                    {"class", QString::fromLatin1(control->metaObject()->className())},
                    {"fontPixels", pixels}, {"enabled", control->isEnabled()},
                    {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
                if (area) {
                    const QRect viewport(area->viewport()->mapTo(&dialog, QPoint()), area->viewport()->size());
                    item.insert("inViewport", rect.intersects(viewport));
                }
                if (auto* label = qobject_cast<QLabel*>(control)) { item.insert("text", label->text()); }
                if (auto* button = qobject_cast<QAbstractButton*>(control)) {
                    item.insert("text", button->text()); item.insert("checked", button->isChecked());
                }
                if (auto* combo = qobject_cast<QComboBox*>(control)) {
                    item.insert("text", combo->currentText()); item.insert("index", combo->currentIndex());
                }
                if (auto* spin = qobject_cast<QSpinBox*>(control)) {
                    item.insert("text", spin->text()); item.insert("value", spin->value()); item.insert("step", spin->singleStep());
                }
                if (auto* spin = qobject_cast<QDoubleSpinBox*>(control)) {
                    item.insert("text", spin->text()); item.insert("value", spin->value()); item.insert("step", spin->singleStep());
                }
                if (auto* slider = qobject_cast<QSlider*>(control)) {
                    QStyleOptionSlider option;
                    option.initFrom(slider);
                    option.orientation = slider->orientation();
                    option.minimum = slider->minimum(); option.maximum = slider->maximum();
                    option.sliderPosition = slider->sliderPosition(); option.sliderValue = slider->value();
                    option.singleStep = slider->singleStep(); option.pageStep = slider->pageStep();
                    if (slider->orientation() == Qt::Horizontal) { option.state |= QStyle::State_Horizontal; }
                    const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
                    item.insert("value", slider->value()); item.insert("step", slider->singleStep());
                    item.insert("handleWidth", handle.width()); item.insert("handleHeight", handle.height());
                }
                controls.append(item);
            }
            const QString stem = pageName.toLower().replace(QStringLiteral(" & "), QStringLiteral("-"))
                .replace(QLatin1Char(' '), QLatin1Char('-'));
            const QString fileName = stem + (slice == QStringLiteral("top") ? QString() : QLatin1Char('-') + slice) + QStringLiteral(".png");
            QVERIFY(dialog.grab().save(directory + QLatin1Char('/') + fileName));
            captures.append(QJsonObject{{"page", pageName}, {"image", fileName},
                {"width", dialog.width()}, {"height", dialog.height()},
                {"devicePixelRatio", dialog.devicePixelRatioF()}, {"slice", slice},
                {"scrollPosition", scroll ? scroll->value() : 0}, {"scrollMaximum", maximum},
                {"viewportWidth", area ? area->viewport()->width() : page->width()},
                {"viewportHeight", area ? area->viewport()->height() : page->height()}, {"controls", controls}});
            }
        }
        const QJsonObject receipt{{"harness", QString::fromLatin1(__FILE__)},
            {"qtVersion", QString::fromLatin1(qVersion())},
            {"theme", "actual shared WorkspacePreview live pipeline and Font B"},
            {"styleClass", QString::fromLatin1(qApp->style()->metaObject()->className())},
            {"fixture", "isolated RadioModel plus loaded SpectrumWidget; no SDR/Core transport; constructor geometry; no font overrides"},
            {"captures", captures}};
        QFile file(directory + QStringLiteral("/native-display-receipt.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) > 0);
    }

    void relocatedFftKeepsOneReleaseHandlerAndNavigationWritesNothing() {
        AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
            QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
        SpectrumWidget pan;
        pan.loadSettings();
        RadioModel model;
        model.setSpectrumWidget(&pan);
        SetupDialog dialog(&model);
        dialog.setPanWidgetsProvider([&pan] { return QList<SpectrumWidget*>{&pan}; });
        int asks = 0;
        int requested = 0;
        dialog.setFftBaselineHandler([&](int size) { ++asks; requested = size; });
        const QStringList before = AppSettings::instance().allKeys();
        for (const QString& page : pages()) { QVERIFY(selected(dialog, page)); }
        QCOMPARE(AppSettings::instance().allKeys(), before);
        QWidget* fft = selected(dialog, QStringLiteral("FFT & Engine"));
        auto* slider = qobject_cast<QSlider*>(byId(fft, QStringLiteral("display.spectrumDefaults.fftSize")));
        QVERIFY(slider);
        QVERIFY(slider->isEnabled());
        slider->setSliderDown(true);
        slider->setSliderPosition(6);
        QCOMPARE(asks, 0);
        slider->setSliderDown(false);
        QCOMPARE(asks, 1);
        QCOMPARE(requested, 262144);
        selected(dialog, QStringLiteral("Spectrum"));
        QCOMPARE(dialog.findChildren<SpectrumDefaultsPage*>().size(), 1);
    }
};
QTEST_MAIN(TstDisplaySetupAssembly)
#include "tst_display_setup_assembly.moc"
