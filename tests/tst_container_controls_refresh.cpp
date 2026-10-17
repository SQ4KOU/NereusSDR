// =================================================================
// tests/tst_container_controls_refresh.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test. It drives a real local
// MainWindow and its containers; no upstream logic is ported here.
//
// A container's controls are refreshed by the event that changes what they
// show. With a radio connected the PureSignal status, announced ten times a
// second whether or not it had changed, used to refresh every container as
// a side effect, so a missing connection did not show. These cases run with
// no radio, where nothing announces on a timer.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-16: The RADE applet in its container is enabled in a RADE mode
//               and grayed with its reason off one. J.J. Boyd / KG4VCF,
//               AI-assisted via Anthropic Claude Code.
//   2026-10-12: Refresh CTUN container state on mirrored pan changes without
//               a new pin request. J.J. Boyd / KG4VCF, AI-assisted via Codex.
//
//   2026-10-11  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>

#include "core/AppSettings.h"
#include "core/BuildIdentity.h"
#include "core/RadioDiscovery.h"
#include "core/WdspTypes.h"
#include "gui/GuiSessionCoordinator.h"
#include "gui/MainWindow.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/SpectrumWidget.h"
#include "gui/applets/RadeApplet.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/meters/FilterButtonItem.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/ModeButtonItem.h"
#include "gui/meters/OtherButtonItem.h"
#include "models/FilterPresetStore.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

SliceModel* ensureSlice(RadioModel* model)
{
    if (model->activeSlice() == nullptr) {
        model->addSlice();
        model->setActiveSlice(0);
    }
    return model->activeSlice();
}

} // namespace

class TstContainerControlsRefresh : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("container-controls-refresh-%1")
                                            .arg(QCoreApplication::applicationPid()));
    }

    void init()
    {
        QVERIFY(!AppSettings::instance().remoteBackend());
        AppSettings::instance().clear();
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"),
                                         QStringLiteral("True"));
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{5});
        BuildIdentity::setBuildTag(QString());
    }

    void cleanup()
    {
        RadioDiscovery::clearHoldOffForTest();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // A container set to follow the selected RX shows the newly selected
    // slice at once.
    void aContainerFollowingTheSelectedRxShowsTheNewlySelectedSlice()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        RadioModel* model = window->radioModel();
        SliceModel* first = ensureSlice(model);
        QVERIFY(first != nullptr);
        model->addSlice();
        QVERIFY(model->slices().size() >= 2);
        SliceModel* second = model->slices().value(1);
        QVERIFY(second && second != first);
        first->setDspMode(DSPMode::USB);
        second->setDspMode(DSPMode::LSB);
        model->setActiveSlice(0);
        QCOMPARE(model->activeSlice(), first);

        ContainerManager* cm = model->containerManager();
        ContainerWidget* container = cm->createContainer(1, DockMode::OverlayDocked);
        QVERIFY(container != nullptr);
        auto* meter = new MeterWidget();
        meter->setProperty("containerSourceContext",
            QJsonObject{{QStringLiteral("rxSourceMode"), QStringLiteral("followSelectedRx")}});
        container->setContent(meter);
        auto* mode = new ModeButtonItem();
        meter->addItem(mode);
        QCOMPARE(ModeButtonItem::modeLabel(mode->activeMode()), QStringLiteral("USB"));

        model->setActiveSlice(1);
        QCOMPARE(model->activeSlice(), second);
        QCOMPARE(ModeButtonItem::modeLabel(mode->activeMode()), QStringLiteral("LSB"));
        model->setActiveSlice(0);
        QCOMPARE(ModeButtonItem::modeLabel(mode->activeMode()), QStringLiteral("USB"));
        QVERIFY(sessions.replace({}, false));
    }

    // A Core update reaches the pan without being an operator gesture.
    // The container must show it immediately, before selecting another
    // slice and returning happens to refresh the controls.
    void aContainersCtunButtonFollowsMirroredPinWithoutReselecting()
    {
        using Id = OtherButtonItem::ButtonId;
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        RadioModel* model = window->radioModel();
        SliceModel* slice = ensureSlice(model);
        QVERIFY(slice != nullptr);
        model->addSlice();
        QVERIFY(model->slices().size() >= 2);
        model->setActiveSlice(0);
        SpectrumWidget* spectrum = window->spectrumForSlice(slice);
        QVERIFY(spectrum != nullptr);
        spectrum->applyRemoteCtunState(true, false);
        ContainerWidget* container = model->containerManager()->createContainer(
            1, DockMode::OverlayDocked);
        QVERIFY(container != nullptr);
        auto* meter = new MeterWidget();
        container->setContent(meter);
        auto* buttons = new OtherButtonItem();
        meter->addItem(buttons);
        QVERIFY(!buttons->buttonState(Id::Ctun));
        QSignalSpy gestures(spectrum, &SpectrumWidget::ctunEnabledChanged);

        QSignalSpy presentation(spectrum, &SpectrumWidget::remoteCtunStateChanged);
        spectrum->applyRemoteCtunState(true, true);
        QCOMPARE(presentation.count(), 1);
        spectrum->applyRemoteCtunState(true, true);
        QCOMPARE(presentation.count(), 1);
        QVERIFY(spectrum->ctunEnabled());
        const bool immediatelyOn = buttons->buttonState(Id::Ctun);
        model->setActiveSlice(1);
        model->setActiveSlice(0);
        QVERIFY(buttons->buttonState(Id::Ctun));
        QCOMPARE(gestures.count(), 0);
        QVERIFY2(immediatelyOn, "CTUN only lights after selecting another slice and returning");
        QVERIFY(sessions.replace({}, false));
    }

    void aContainersCtunButtonFollowsMirroredAvailability()
    {
        using Id = OtherButtonItem::ButtonId;
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        RadioModel* model = window->radioModel();
        SliceModel* slice = ensureSlice(model);
        QVERIFY(slice != nullptr);
        SpectrumWidget* spectrum = window->spectrumForSlice(slice);
        QVERIFY(spectrum != nullptr);
        spectrum->applyRemoteCtunState(true, false);
        ContainerWidget* container = model->containerManager()->createContainer(
            1, DockMode::OverlayDocked);
        QVERIFY(container != nullptr);
        auto* meter = new MeterWidget();
        container->setContent(meter);
        auto* buttons = new OtherButtonItem();
        meter->addItem(buttons);
        QVERIFY(buttons->isButtonAvailable(Id::Ctun));
        QSignalSpy gestures(spectrum, &SpectrumWidget::ctunEnabledChanged);
        const QString held = QStringLiteral("This panadapter shows STRIX's receiver. Its C-Tune setting is STRIX's.");
        spectrum->applyRemoteCtunState(true, false, held);
        QVERIFY(!buttons->isButtonAvailable(Id::Ctun));
        QCOMPARE(buttons->buttonUnavailableReason(buttons->indexOf(Id::Ctun)), held);
        spectrum->setCtunEnabled(true);
        QVERIFY(!spectrum->ctunEnabled());
        spectrum->applyRemoteCtunState(true, false);
        QVERIFY(buttons->isButtonAvailable(Id::Ctun));
        spectrum->applyRemoteCtunState(false, false);
        QVERIFY(!buttons->isButtonAvailable(Id::Ctun));
        spectrum->applyRemoteCtunState(true, true);
        QVERIFY(buttons->isButtonAvailable(Id::Ctun));
        QVERIFY(buttons->buttonState(Id::Ctun));
        spectrum->applyRemoteCtunState(true, false);
        QVERIFY(!buttons->buttonState(Id::Ctun));
        QCOMPARE(gestures.count(), 0);
        QVERIFY(sessions.replace({}, false));
    }

    // A container's filter buttons show a preset's new name when it is
    // renamed (Setup, or the Core's presets in a remote window).
    void aContainersFilterButtonsShowARenamedPreset()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        RadioModel* model = window->radioModel();
        SliceModel* slice = ensureSlice(model);
        QVERIFY(slice != nullptr);
        slice->setDspMode(DSPMode::USB);

        ContainerManager* cm = model->containerManager();
        ContainerWidget* container = cm->createContainer(1, DockMode::OverlayDocked);
        QVERIFY(container != nullptr);
        auto* meter = new MeterWidget();
        container->setContent(meter);
        auto* filter = new FilterButtonItem();
        meter->addItem(filter);

        FilterPresetStore* store = model->filterPresetStore();
        QVERIFY(store != nullptr);
        const FilterPreset original = store->presetsForMode(DSPMode::USB).value(0);
        QCOMPARE(filter->filterLabel(0), original.name);
        FilterPreset renamed = original;
        renamed.name = QStringLiteral("Wide");
        QVERIFY(renamed.name != original.name);
        store->setPreset(DSPMode::USB, 0, renamed);
        QCOMPARE(filter->filterLabel(0), QStringLiteral("Wide"));
        store->resetPreset(DSPMode::USB, 0);
        QCOMPARE(filter->filterLabel(0), original.name);
        QVERIFY(sessions.replace({}, false));
    }

    // A container's PEAK button lights from the pan its slice is on, so a
    // slice moved to another pan takes that pan's peak hold.
    void aContainersPeakButtonFollowsItsSliceToAnotherPan()
    {
        using Id = OtherButtonItem::ButtonId;
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        RadioModel* model = window->radioModel();
        SliceModel* slice = ensureSlice(model);
        QVERIFY(slice != nullptr);

        auto* stack = window->findChild<PanadapterStack*>();
        QVERIFY(stack != nullptr);
        SpectrumWidget* home = window->spectrumForSlice(slice);
        QVERIFY(home != nullptr);
        PanadapterApplet* extra = stack->addPanadapter(QStringLiteral("pan-1"));
        QVERIFY(extra && extra->spectrumWidget());
        SpectrumWidget* other = extra->spectrumWidget();
        QVERIFY(other != home);
        home->setPeakHoldEnabled(true);
        other->setPeakHoldEnabled(false);

        ContainerManager* cm = model->containerManager();
        ContainerWidget* container = cm->createContainer(1, DockMode::OverlayDocked);
        QVERIFY(container != nullptr);
        auto* meter = new MeterWidget();
        container->setContent(meter);
        auto* buttons = new OtherButtonItem();
        meter->addItem(buttons);
        QVERIFY(buttons->buttonState(Id::PeakHold));

        slice->setPanKey(QStringLiteral("pan-1"));
        QCOMPARE(window->spectrumForSlice(slice), other);
        QVERIFY(!buttons->buttonState(Id::PeakHold));
        QVERIFY(sessions.replace({}, false));
    }

    // The RADE applet in its container follows the active slice's mode in
    // a real window: grayed with its reason off a RADE mode (disabled,
    // never hidden), enabled with no reason line in one.
    void theRadeAppletIsEnabledInARadeMode()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        RadioModel* model = window->radioModel();
        SliceModel* slice = ensureSlice(model);
        QVERIFY(slice != nullptr);
        auto* rade = window->findChild<RadeApplet*>();
        QVERIFY(rade != nullptr);
        ContainerManager* cm = model->containerManager();
        QVERIFY(cm != nullptr);
        QVERIFY2(cm->workspaceStore() != nullptr, "the window shows its applets from the workspace");

        slice->setDspMode(DSPMode::USB);
        QTRY_VERIFY(!rade->unavailableReason().isEmpty());
        QVERIFY(!rade->isEnabled());

        slice->setDspMode(DSPMode::RADE_U);
        QTRY_VERIFY(rade->unavailableReason().isEmpty());
        QVERIFY2(rade->isEnabled(), "in a RADE mode the RADE applet is enabled");
        if (auto* label = rade->findChild<QLabel*>(QStringLiteral("appletUnavailableReason"))) {
            QVERIFY(!label->isVisibleTo(rade));
        }

        slice->setDspMode(DSPMode::USB);
        QTRY_VERIFY(!rade->isEnabled());
        QVERIFY(sessions.replace({}, false));
    }
};

QTEST_MAIN(TstContainerControlsRefresh)
#include "tst_container_controls_refresh.moc"
