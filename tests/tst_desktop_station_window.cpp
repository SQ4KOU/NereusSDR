// no-port-check: NereusSDR-original. Desktop host presentation over a borrowed local model.
#include "gui/MainWindow.h"

#include "core/TxSliceArbiter.h"
#include "core/AppSettings.h"
#include "core/SliceOwnership.h"
#include "core/TciServer.h"
#include "core/safety/TransmitHolder.h"
#include "core/session/DeviceSessionRegistry.h"
#include "core/session/StationServer.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/OtherButtonItem.h"
#include "gui/applets/RxApplet.h"
#include "gui/applets/TxApplet.h"
#include "gui/multidevice/TakeTransmitDialog.h"
#include "gui/SpectrumWidget.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/widgets/VfoWidget.h"
#include "gui/widgets/RxDashboard.h"
#include "gui/widgets/StatusToast.h"
#include "gui/SliceChooser.h"
#include "gui/PanFloatingWindow.h"
#include "gui/SetupDialog.h"
#include "gui/setup/DspSetupPages.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/NotchModel.h"

#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolButton>
#include <QWheelEvent>
#include <QApplication>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace NereusSDR;

namespace {
StationHostOptions optionsFor(AppSettings& settings, const QString& directory)
{
    StationHostOptions options;
    options.settings = &settings;
    options.securityDirectory = directory;
    options.hostingDevice = StationHostOptions::HostingDevice{
        QStringLiteral("Shack Mac mini"), QStringLiteral("Shack")};
    options.remoteBind = QStringLiteral("127.0.0.1");
    options.statusPage = false;
    QTcpServer probe;
    if (probe.listen(QHostAddress::LocalHost, 0)) {
        options.remotePort = probe.serverPort();
        probe.close();
    }
    return options;
}

VfoWidget* flagFor(MainWindow& window, int id)
{
    for (VfoWidget* flag : window.findChildren<VfoWidget*>()) {
        if (flag->sliceIndex() == id) { return flag; }
    }
    return nullptr;
}

QMenu* menuNamed(MainWindow& window, const QString& name)
{
    for (QMenu* menu : window.findChildren<QMenu*>()) {
        if (menu->title().remove(QLatin1Char('&')) == name) { return menu; }
    }
    return nullptr;
}

QAction* actionNamed(QMenu* menu, const QString& name)
{
    if (!menu) { return nullptr; }
    for (QAction* action : menu->actions()) {
        if (action->text().remove(QLatin1Char('&')) == name) { return action; }
    }
    return nullptr;
}

QToolButton* sliceTabFor(RxApplet& applet, QChar letter)
{
    for (QToolButton* tab : applet.findChildren<QToolButton*>()) {
        if (tab->isCheckable() && tab->text() == QString(letter)
            && tab->toolTip().startsWith(QStringLiteral("Slice %1").arg(letter))) {
            return tab;
        }
    }
    return nullptr;
}

DeviceSessionRegistry::Entry admitPhone(StationServer& server, QObject& session,
                                        const QByteArray& deviceId)
{
    DeviceSessionRegistry::Entry phone;
    phone.deviceId = deviceId;
    phone.kind = DeviceSessionRegistry::Kind::Paired;
    phone.name = QStringLiteral("Living room iPhone");
    phone.shortName = QStringLiteral("iPhone");
    phone.deviceKind = QStringLiteral("phone");
    const bool admitted = server.deviceSessions()->admit(phone, &session).admission
        == DeviceSessionRegistry::Admission::Admitted;
    if (!admitted) { phone.deviceId.clear(); }
    return phone;
}

int toastsSaying(MainWindow& window, const QString& words)
{
    int count = 0;
    for (StatusToast* toast : window.findChildren<StatusToast*>()) {
        if (toast->message() == words) { ++count; }
    }
    return count;
}

int flagCountFor(MainWindow& window, int id)
{
    int count = 0;
    for (VfoWidget* flag : window.findChildren<VfoWidget*>()) {
        if (flag->sliceIndex() == id) { ++count; }
    }
    return count;
}

bool applyLayout(MainWindow& window, const QString& layoutId)
{
    return QMetaObject::invokeMethod(&window, "applyPanLayout", Q_ARG(QString, layoutId));
}
}

class TstDesktopStationWindow final : public QObject {
    Q_OBJECT
private slots:
    void hostedDashboardClearsWhenDesktopLosesLastReceiver()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(a && b);
        a->setDspMode(DSPMode::CWU);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        SliceOwnership* ownership = model->sliceOwnership();
        ownership->setOwner(bId, QByteArrayLiteral("token:phone"));
        auto* dashboard = window.findChild<RxDashboard*>();
        QVERIFY(dashboard);
        QCOMPARE(dashboard->slice(), a);
        QCOMPARE(dashboard->modeText(), QStringLiteral("CWU"));

        ownership->setOwner(aId, QByteArrayLiteral("token:phone"));
        // Slice control plan Task 3: the former controller stays a listener,
        // so the bottom bar follows A as a listened slice (ruling U7).
        QVERIFY(ownership->isListening(SliceOwnership::stationDevice(), aId));
        QCOMPARE(dashboard->slice(), a);
        // It clears once the desktop neither controls nor listens to a slice.
        QVERIFY(ownership->leave(SliceOwnership::stationDevice(), aId));
        QVERIFY(ownership->leave(SliceOwnership::stationDevice(), bId));
        QCOMPARE(dashboard->slice(), nullptr);
        QCOMPARE(dashboard->modeText(), QStringLiteral("–"));
        QVERIFY(dashboard->sliceLetter().isNull());

        ownership->setOwner(bId, SliceOwnership::stationDevice());
        b->setDspMode(DSPMode::AM);
        QCOMPARE(dashboard->slice(), b);
        QCOMPARE(dashboard->sliceLetter(), QLatin1Char('B'));
        QCOMPARE(dashboard->modeText(), QStringLiteral("AM"));
    }

    void hostedSetupEditsOnlyDesktopReceiver()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        const int cId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        SliceModel* c = model->sliceById(cId);
        QVERIFY(a && b && c);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        SliceOwnership* ownership = model->sliceOwnership();
        ownership->setOwner(bId, QByteArrayLiteral("token:phone"));
        QVERIFY(model->setActiveSliceByIdFor(QByteArrayLiteral("token:phone"), bId));
        model->setTransmitHolder(QByteArrayLiteral("token:phone"));
        QCOMPARE(model->activeSlice(), b);
        QCOMPARE(ownership->activeFor(SliceOwnership::stationDevice()), aId);

        SetupDialog* dialog = nullptr;
        connect(&window, &MainWindow::setupDialogCreated, &window,
                [&dialog](SetupDialog* created) { dialog = created; });
        QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
        QVERIFY(dialog);
        dialog->selectPage(QStringLiteral("AGC/ALC"));
        auto* agc = dialog->findChild<AgcAlcSetupPage*>();
        QVERIFY(agc);
        auto attack = [agc] {
            for (QSpinBox* spin : agc->findChildren<QSpinBox*>()) {
                if (spin->property("nereusSetupId").toString() == QStringLiteral("dsp.agcAlc.agcAttack")) {
                    return spin;
                }
            }
            return static_cast<QSpinBox*>(nullptr);
        };
        QVERIFY(attack());
        attack()->setValue(37);
        QCOMPARE(a->agcAttack(), 37);
        QVERIFY(b->agcAttack() != 37);

        QVERIFY(model->setActiveSliceByIdFor(SliceOwnership::stationDevice(), cId));
        QCOMPARE(model->activeSlice(), b);
        QVERIFY(attack());
        attack()->setValue(53);
        QCOMPARE(c->agcAttack(), 53);
        QCOMPARE(a->agcAttack(), 37);
        QVERIFY(b->agcAttack() != 53);

        dialog->selectPage(QStringLiteral("TNF"));
        auto* tnf = dialog->findChild<MnfSetupPage*>();
        QVERIFY(tnf);
        auto* add = tnf->findChild<QPushButton*>(QStringLiteral("btnMNFAdd"));
        QVERIFY(add);
        c->setFrequency(14074000.0);
        b->setFrequency(7100000.0);
        add->click();
        QCOMPARE(model->notchModel()->notches().size(), 1);
        QCOMPARE(model->notchModel()->notches().first().centerHz, c->frequency());
        dialog->close();

        // A flag shortcut names C explicitly. It must first select C for
        // this desktop, even while B remains the Core's active receiver.
        QVERIFY(model->setActiveSliceByIdFor(SliceOwnership::stationDevice(), aId));
        QCOMPARE(model->activeSlice(), b);
        VfoWidget* cFlag = flagFor(window, cId);
        QVERIFY(cFlag);
        dialog = nullptr;
        QVERIFY(QMetaObject::invokeMethod(cFlag, "openSetupRequested"));
        QCOMPARE(ownership->activeFor(SliceOwnership::stationDevice()), cId);
        QVERIFY(dialog);
        auto* flagAgc = dialog->findChild<AgcAlcSetupPage*>();
        QVERIFY(flagAgc);
        QSpinBox* flagAttack = nullptr;
        for (QSpinBox* spin : flagAgc->findChildren<QSpinBox*>()) {
            if (spin->property("nereusSetupId").toString()
                == QStringLiteral("dsp.agcAlc.agcAttack")) {
                flagAttack = spin;
                break;
            }
        }
        QVERIFY(flagAttack);
        flagAttack->setValue(69);
        QCOMPARE(c->agcAttack(), 69);
        QVERIFY(b->agcAttack() != 69);

        QPointer<QSpinBox> oldFlagAttack(flagAttack);
        ownership->setOwner(cId, QByteArrayLiteral("token:phone"));
        ownership->setOwner(aId, QByteArrayLiteral("token:phone"));
        QCOMPARE(ownership->activeFor(SliceOwnership::stationDevice()), -1);
        QVERIFY(oldFlagAttack.isNull());
        dialog->selectPage(QStringLiteral("TNF"));
        auto* emptyTnf = dialog->findChild<MnfSetupPage*>();
        QVERIFY(emptyTnf);
        auto* disabledAdd = emptyTnf->findChild<QPushButton*>(QStringLiteral("btnMNFAdd"));
        QVERIFY(disabledAdd);
        QVERIFY(!disabledAdd->isEnabled());
        disabledAdd->click();
        QCOMPARE(model->notchModel()->notches().size(), 1);
        dialog->close();
    }

    void foreignGlobalActiveNeverBecomesDesktopTarget()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        const int cId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        SliceModel* c = model->sliceById(cId);
        QVERIFY(a && b && c);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        SliceOwnership* ownership = model->sliceOwnership();
        ownership->setOwner(bId, QByteArrayLiteral("token:phone"));
        QVERIFY(model->setActiveSliceByIdFor(QByteArrayLiteral("token:phone"), bId));
        model->setTransmitHolder(QByteArrayLiteral("token:phone"));
        QCOMPARE(model->activeSlice(), b);
        QCOMPARE(ownership->activeFor(SliceOwnership::stationDevice()), aId);

        QMenu* dsp = menuNamed(window, QStringLiteral("DSP"));
        QVERIFY(dsp);
        QAction* anf = actionNamed(dsp, QStringLiteral("ANF"));
        QVERIFY(anf);
        anf->trigger();
        QVERIFY(a->anfEnabled());
        QVERIFY(!b->anfEnabled());
        QAction* nbMenuAction = actionNamed(dsp, QStringLiteral("NB"));
        QVERIFY(nbMenuAction);
        QMenu* nb = nbMenuAction->menu();
        QVERIFY(nb);
        QAction* nbAction = actionNamed(nb, QStringLiteral("NB"));
        QVERIFY(nbAction);
        nbAction->trigger();
        QCOMPARE(a->nbMode(), NbMode::NB);
        // Co-hosted slices intentionally share the stream's single NB state.
        QCOMPARE(b->nbMode(), NbMode::NB);
        QMenu* mode = menuNamed(window, QStringLiteral("Mode"));
        QVERIFY(mode);
        QAction* am = actionNamed(mode, QStringLiteral("AM"));
        QVERIFY(am);
        const DSPMode foreignMode = b->dspMode();
        am->trigger();
        QCOMPARE(a->dspMode(), DSPMode::AM);
        QCOMPARE(b->dspMode(), foreignMode);

        auto* dashboard = window.findChild<RxDashboard*>();
        QVERIFY(dashboard);
        QCOMPARE(dashboard->sliceLetter(), a->sliceLetter());
        auto* pans = window.findChild<PanadapterStack*>();
        QVERIFY(pans);
        auto* pan = pans->panadapter(QStringLiteral("pan-0"));
        QVERIFY(pan);
        QCOMPARE(pan->activeSliceIndex(), aId);
        pan->setActiveSliceIndex(bId);  // stale foreign pan selection
        const double foreignHz = b->frequency();
        const double tunedHz = a->frequency() + 1000.0;
        QVERIFY(QMetaObject::invokeMethod(pan->spectrumWidget(), "frequencyClicked",
            Qt::DirectConnection, Q_ARG(double, tunedHz)));
        QCOMPARE(a->frequency(), tunedHz);
        QCOMPARE(b->frequency(), foreignHz);

        QVERIFY(model->setActiveSliceByIdFor(SliceOwnership::stationDevice(), cId));
        QCOMPARE(model->activeSlice(), b);
        QCOMPARE(dashboard->sliceLetter(), c->sliceLetter());
        QCOMPARE(pan->activeSliceIndex(), cId);
        anf->trigger();
        QVERIFY(c->anfEnabled());
        QVERIFY(!b->anfEnabled());

        ContainerWidget* container = window.findChild<ContainerWidget*>();
        QVERIFY(container);
        container->setRxSource(bId + 1);
        QVERIFY(QMetaObject::invokeMethod(container, "otherButtonClicked", Qt::DirectConnection,
            Q_ARG(int, int(OtherButtonItem::ButtonId::Snb))));
        QVERIFY(!b->snbEnabled());
    }

    void hostModeOwnFlagsAndDetach()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(a && b);
        VfoWidget* flagA = flagFor(window, a->sliceIndex());
        VfoWidget* flagB = flagFor(window, b->sliceIndex());
        QVERIFY(flagA && flagB);

        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        auto* tci = window.findChild<TciServer*>();
        QVERIFY(tci);
        QVERIFY(tci->desktopHostMode());
        SliceOwnership* ownership = model->sliceOwnership();
        QCOMPARE(ownership->activeFor(SliceOwnership::stationDevice()), a->sliceIndex());
        QCOMPARE(controller.server()->takeTransmitForStation({}, {}),
                 TransmitHolder::TakeVerdict::AtOnce);
        window.refreshDesktopStationState();
        QVERIFY(flagA->txSliceShown());

        ownership->setOwner(b->sliceIndex(), QByteArrayLiteral("token:phone"));
        // The former controller stays a listener (Task 3); leaving makes B
        // wholly the phone's, shown as its marker.
        QVERIFY(ownership->leave(SliceOwnership::stationDevice(), b->sliceIndex()));
        QVERIFY(flagA->stationPresentationAllowed());
        QVERIFY(!flagB->stationPresentationAllowed());
        QVERIFY(!flagB->txSliceShown());
        SpectrumWidget* spectrum = qobject_cast<SpectrumWidget*>(flagB->parentWidget());
        QVERIFY(spectrum);
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);
        spectrum->updateVfoPositions();
        QVERIFY(flagB->isHidden());
        QVERIFY(model->setActiveSliceByIdFor(SliceOwnership::stationDevice(),
                                             a->sliceIndex()));
        QCOMPARE(ownership->activeFor(SliceOwnership::stationDevice()), a->sliceIndex());
        QVERIFY(!model->setActiveSliceByIdFor(SliceOwnership::stationDevice(),
                                              b->sliceIndex()));

        window.setDesktopStationController(nullptr);
        QVERIFY(!controller.enabled());
        QVERIFY(!tci->desktopHostMode());
        QVERIFY(flagB->stationPresentationAllowed());
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
        auto replacement = std::make_unique<DesktopStationController>(
            model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(replacement.get());
        QVERIFY(replacement->start(true));
        QVERIFY(tci->desktopHostMode());
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);
        replacement.reset();
        QVERIFY(!tci->desktopHostMode());
        QVERIFY(flagB->stationPresentationAllowed());
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
        SetupDialog* observed = nullptr;
        connect(&window, &MainWindow::setupDialogCreated, &window,
                [&observed](SetupDialog* dialog) { observed = dialog; });
        QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
        QVERIFY(observed);
        observed->close();
    }

    void hostShowsOtherDeviceAsForeignMarker()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(a && b);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        DeviceSessionRegistry::Entry phone;
        phone.deviceId = QByteArrayLiteral("phone-device-id-for-away-state-01");
        phone.kind = DeviceSessionRegistry::Kind::Paired;
        phone.name = QStringLiteral("Living room iPhone");
        phone.shortName = QStringLiteral("iPhone");
        phone.deviceKind = QStringLiteral("phone");
        QCOMPARE(server->deviceSessions()->admit(phone, &phoneSession).admission,
                 DeviceSessionRegistry::Admission::Admitted);
        auto* spectrum = window.findChild<PanadapterStack*>()->panadapter(
            QStringLiteral("pan-0"))->spectrumWidget();
        QVERIFY(spectrum);
        SliceOwnership* ownership = model->sliceOwnership();
        ownership->setOwner(bId, phone.deviceId);
        // The former controller stays a listener (Task 3); leaving makes B
        // wholly the phone's, shown as its marker.
        QVERIFY(ownership->leave(SliceOwnership::stationDevice(), bId));
        QVERIFY(flagFor(window, aId)->stationPresentationAllowed());
        QVERIFY(!flagFor(window, bId)->stationPresentationAllowed());
        QCOMPARE(spectrum->sliceMarkerGeometry().size(), 1);
        QCOMPARE(spectrum->sliceMarkerGeometry().first().flag,
                 static_cast<const VfoWidget*>(flagFor(window, aId)));
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);
        const auto first = spectrum->foreignSliceMarkers().first();
        QCOMPARE(first.sliceId, bId);
        QCOMPARE(first.letter, QStringLiteral("B"));
        QCOMPARE(first.ownerShortName, QStringLiteral("iPhone"));
        QCOMPARE(first.color, VfoWidget::sliceColor(bId));
        QCOMPARE(SpectrumWidget::foreignMarkerLabel(first), QStringLiteral("B iPhone"));

        TransmitHolder::KeyRequest key;
        key.deviceId = phone.deviceId;
        QCOMPARE(server->transmitHolder()->askKey(key).verdict, KeyingVerdict::Admit);
        b->setTxSlice(true);
        QVERIFY(b->txSliceMarked());
        QVERIFY(spectrum->foreignSliceMarkers().first().tx);
        QCOMPARE(SpectrumWidget::foreignMarkerLabel(spectrum->foreignSliceMarkers().first()),
                 QStringLiteral("B iPhone TX"));
        server->transmitHolder()->release(phone.deviceId, QStringLiteral("test release"));
        QVERIFY(!spectrum->foreignSliceMarkers().first().tx);

        auto* extra = window.findChild<PanadapterStack*>()->addPanadapter(
            QStringLiteral("pan-1"));
        QVERIFY(extra && extra->spectrumWidget());
        QCOMPARE(extra->spectrumWidget()->foreignSliceMarkers().size(), 1);

        b->setFrequency(b->frequency() + 1200.0);
        b->setFilter(150, 2700);
        QCOMPARE(spectrum->foreignSliceMarkers().first().centreHz, b->frequency());
        QCOMPARE(spectrum->foreignSliceMarkers().first().filterLowHz, 150);
        QCOMPARE(spectrum->foreignSliceMarkers().first().filterHighHz, 2700);
        server->deviceSessions()->sessionEnded(phone.deviceId, &phoneSession,
            DeviceSessionRegistry::EndKind::Dropped);
        QVERIFY(spectrum->foreignSliceMarkers().first().away);

        ownership->setOwner(bId, SliceOwnership::stationDevice());
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
        QCOMPARE(spectrum->sliceMarkerGeometry().size(), 2);
        ownership->hold(bId, phone.deviceId);
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);
        QVERIFY(spectrum->foreignSliceMarkers().first().away);
        QCOMPARE(spectrum->sliceMarkerGeometry().size(), 1);
        ownership->setOwner(bId, phone.deviceId);
        // Still a listener: B keeps a read-only flag, not a marker.
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
        QCOMPARE(spectrum->sliceMarkerGeometry().size(), 2);
        QVERIFY(flagFor(window, bId)->isListening());
        QVERIFY(ownership->leave(SliceOwnership::stationDevice(), bId));
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);

        ownership->setOwner(aId, phone.deviceId);
        QVERIFY(ownership->leave(SliceOwnership::stationDevice(), aId));
        QVERIFY(spectrum->sliceMarkerGeometry().isEmpty());
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 2);
        model->removeSlice(bId);
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);
        controller.stop();
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
    }

    // Slice control plan Task 14a: a slice the hosting window listens to
    // keeps its flag, says who controls it, never writes the slice, and its
    // Stop listening leaves the slice.
    void hostListensToAnotherDevicesSlice()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* b = model->sliceById(bId);
        QVERIFY(b);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        DeviceSessionRegistry::Entry phone;
        phone.deviceId = QByteArrayLiteral("phone-device-id-for-listening-01");
        phone.kind = DeviceSessionRegistry::Kind::Paired;
        phone.name = QStringLiteral("Living room iPhone");
        phone.shortName = QStringLiteral("iPhone");
        phone.deviceKind = QStringLiteral("phone");
        QCOMPARE(server->deviceSessions()->admit(phone, &phoneSession).admission,
                 DeviceSessionRegistry::Admission::Admitted);
        auto* spectrum = window.findChild<PanadapterStack*>()->panadapter(
            QStringLiteral("pan-0"))->spectrumWidget();
        QVERIFY(spectrum);
        SliceOwnership* ownership = model->sliceOwnership();
        ownership->setOwner(bId, phone.deviceId);
        VfoWidget* flagA = flagFor(window, aId);
        VfoWidget* flagB = flagFor(window, bId);
        QVERIFY(flagA && flagB);
        // The phone took the slice; this window, its former controller,
        // stays a listener.
        QVERIFY(ownership->isListening(SliceOwnership::stationDevice(), bId));
        QVERIFY(flagB->stationPresentationAllowed());
        QVERIFY(flagB->isListening());
        QVERIFY(flagB->accessLineText().startsWith(QStringLiteral("Listening")));
        QVERIFY(flagB->accessLineText().contains(QStringLiteral("iPhone")));
        QCOMPARE(flagA->sliceAccess().state, VfoWidget::SliceAccess::State::Controlled);
        QCOMPARE(flagA->accessLineText(), QStringLiteral("You control"));
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
        QCOMPARE(spectrum->sliceMarkerGeometry().size(), 2);

        // The listener never writes the slice.
        const double before = b->frequency();
        QSignalSpy freq(b, &SliceModel::frequencyChanged);
        QSignalSpy af(b, &SliceModel::afGainChanged);
        QSignalSpy mute(b, &SliceModel::mutedChanged);
        const QPointF at(flagB->width() / 2.0, flagB->height() / 2.0);
        QWheelEvent wheel(at, flagB->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(flagB, &wheel);
        QCOMPARE(freq.count(), 0);
        QCOMPARE(af.count(), 0);
        QCOMPARE(mute.count(), 0);
        QCOMPARE(b->frequency(), before);

        // The phone keys on B, then moves transmit to its other slice C.
        // Only the TX slice changed, and C is on the air, so C's listened
        // flag turns red and B's clears; this window's own A stays clear.
        const int cId = model->addSlice(QStringLiteral("pan-0"));
        ownership->setOwner(cId, phone.deviceId);
        VfoWidget* flagC = flagFor(window, cId);
        QVERIFY(flagC);
        QVERIFY(flagC->isListening());
        TxSliceArbiter* arbiter = model->txSliceArbiter();
        QVERIFY(arbiter);
        TransmitHolder::KeyRequest key;
        key.deviceId = phone.deviceId;
        QCOMPARE(server->transmitHolder()->askKey(key).verdict, KeyingVerdict::Admit);
        server->transmitHolder()->setKeyed(true);
        if (arbiter->txBoundSliceId() != bId) {
            QVERIFY(arbiter->requestHandoff(bId, phone.deviceId));
        }
        QCOMPARE(arbiter->txBoundSliceId(), bId);
        QVERIFY(server->sliceOnAir(bId));
        QVERIFY(flagB->txSliceShown());
        QVERIFY(!flagC->txSliceShown());
        QVERIFY(arbiter->requestHandoff(cId, phone.deviceId));
        QCOMPARE(arbiter->txBoundSliceId(), cId);
        QVERIFY(server->sliceOnAir(cId));
        QVERIFY(flagC->txSliceShown());
        QVERIFY(!flagB->txSliceShown());
        QVERIFY(!flagA->txSliceShown());
        server->transmitHolder()->release(phone.deviceId, QStringLiteral("test release"));
        QVERIFY(!server->sliceOnAir(cId));
        QVERIFY(!flagC->txSliceShown());
        model->removeSlice(cId);

        // Stop listening from the flag: the Core answers, nothing is left
        // waiting, and the slice is the phone's marker again.
        emit flagB->stopListeningRequested(bId);
        QVERIFY(!ownership->isListening(SliceOwnership::stationDevice(), bId));
        QVERIFY(!flagB->sliceAccessPending());
        QVERIFY(!flagB->stationPresentationAllowed());
        QCOMPARE(spectrum->foreignSliceMarkers().size(), 1);

        // Listening again brings the read-only flag back.
        QVERIFY(ownership->join(SliceOwnership::stationDevice(), bId));
        QVERIFY(flagB->stationPresentationAllowed());
        QVERIFY(flagB->isListening());
        QVERIFY(spectrum->foreignSliceMarkers().isEmpty());
        controller.stop();
    }

    // Slice control plan Task 15 (rulings U5, U6, U7): the RX applet has a
    // tab for the slice this window listens to, saying who controls it;
    // selecting it moves this window's RX (the applet, the bottom bar and
    // the flag focus) and never the active slice, the TX slice or the
    // holder; the applet holds its shared controls with the controller
    // named; Stop listening from the applet leaves the slice.
    void hostRxAppletFollowsAListenedTab()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(a && b);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        DeviceSessionRegistry::Entry phone;
        phone.deviceId = QByteArrayLiteral("phone-device-id-for-rx-applet-01");
        phone.kind = DeviceSessionRegistry::Kind::Paired;
        phone.name = QStringLiteral("Living room iPhone");
        phone.shortName = QStringLiteral("iPhone");
        phone.deviceKind = QStringLiteral("phone");
        QCOMPARE(server->deviceSessions()->admit(phone, &phoneSession).admission,
                 DeviceSessionRegistry::Admission::Admitted);
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        ownership->setOwner(bId, phone.deviceId);
        QVERIFY(ownership->isListening(station, bId));

        RxApplet* applet = window.findChild<RxApplet*>();
        QVERIFY(applet);
        QToolButton* tabA = sliceTabFor(*applet, QLatin1Char('A'));
        QToolButton* tabB = sliceTabFor(*applet, QLatin1Char('B'));
        QVERIFY(tabA && tabB);
        QVERIFY(tabB->isEnabled());
        QVERIFY(tabB->toolTip().contains(QStringLiteral("Listening")));
        QVERIFY(tabB->toolTip().contains(QStringLiteral("iPhone")));
        QCOMPARE(applet->slice(), a);
        QVERIFY(!applet->isListening());

        const int activeBefore = ownership->activeFor(station);
        QCOMPARE(activeBefore, aId);
        TxSliceArbiter* arbiter = model->txSliceArbiter();
        QVERIFY(arbiter);
        const int txBefore = arbiter->txBoundSliceId();
        const TransmitHolder::State holderBefore = server->transmitHolder()->state();
        QSignalSpy freq(b, &SliceModel::frequencyChanged);
        QSignalSpy mode(b, &SliceModel::dspModeChanged);
        QSignalSpy af(b, &SliceModel::afGainChanged);
        QSignalSpy mute(b, &SliceModel::mutedChanged);

        tabB->click();
        QCOMPARE(ownership->activeRxFor(station), bId);
        QCOMPARE(ownership->activeFor(station), activeBefore);
        QCOMPARE(arbiter->txBoundSliceId(), txBefore);
        QCOMPARE(server->transmitHolder()->state(), holderBefore);
        QCOMPARE(applet->slice(), b);
        QVERIFY(applet->isListening());
        QVERIFY(applet->sliceAccess().heldReason.contains(QStringLiteral("iPhone")));
        RxDashboard* dashboard = window.findChild<RxDashboard*>();
        QVERIFY(dashboard);
        QCOMPARE(dashboard->slice(), b);
        PanadapterApplet* pan = window.findChild<PanadapterStack*>()->panadapter(
            QStringLiteral("pan-0"));
        QVERIFY(pan);
        QCOMPARE(pan->activeSliceIndex(), bId);
        QCOMPARE(freq.count(), 0);
        QCOMPARE(mode.count(), 0);
        QCOMPARE(af.count(), 0);
        QCOMPARE(mute.count(), 0);

        // Back to the slice this window controls.
        tabA->click();
        QCOMPARE(ownership->activeRxFor(station), aId);
        QCOMPARE(applet->slice(), a);
        QVERIFY(!applet->isListening());
        QCOMPARE(dashboard->slice(), a);

        // Stop listening from the applet: the Core answers, nothing is left
        // waiting, and the applet keeps the slice this window controls.
        tabB->click();
        QCOMPARE(applet->slice(), b);
        emit applet->stopListeningRequested(bId);
        QVERIFY(!ownership->isListening(station, bId));
        QCOMPARE(applet->slice(), a);
        QVERIFY(!applet->isListening());
        QCOMPARE(dashboard->slice(), a);
        controller.stop();
    }

    // Slice control plan Task 15 fix round 1: a container set to a slice
    // this window listens to refuses Mute (and the other slice buttons)
    // with the RX applet's reason, naming the device that controls it.
    void hostContainerOnAListenedSliceRefusesMute()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(a && b);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        DeviceSessionRegistry::Entry phone;
        phone.deviceId = QByteArrayLiteral("phone-device-id-for-rx-applet-01");
        phone.kind = DeviceSessionRegistry::Kind::Paired;
        phone.name = QStringLiteral("Living room iPhone");
        phone.shortName = QStringLiteral("iPhone");
        phone.deviceKind = QStringLiteral("phone");
        QCOMPARE(server->deviceSessions()->admit(phone, &phoneSession).admission,
                 DeviceSessionRegistry::Admission::Admitted);
        auto* manager = window.findChild<ContainerManager*>();
        QVERIFY(manager);
        ContainerWidget* container =
            manager->createContainer(bId + 1, DockMode::Floating);  // slice B
        auto* meter = new MeterWidget();
        container->setContent(meter);
        auto* buttons = new OtherButtonItem();
        meter->addItem(buttons);
        container->wireInteractiveItem(buttons);
        const auto destroy = qScopeGuard([manager, container] {
            manager->destroyContainer(container->id());
        });

        // The button follows the change of control with no other refresh.
        using Id = OtherButtonItem::ButtonId;
        QVERIFY(buttons->isButtonAvailable(Id::Mute));
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        ownership->setOwner(bId, phone.deviceId);
        QVERIFY(ownership->isListening(station, bId));
        const bool mutedBefore = b->muted();
        QSignalSpy muted(b, &SliceModel::mutedChanged);
        QVERIFY(!buttons->isButtonAvailable(Id::Mute));
        const QString reason = buttons->buttonUnavailableReason(buttons->indexOf(Id::Mute));
        QCOMPARE(reason, QStringLiteral("Living room iPhone controls this slice"));
        emit container->otherButtonClicked(int(Id::Mute));
        QCOMPARE(b->muted(), mutedBefore);
        QCOMPARE(muted.count(), 0);

        ownership->setOwner(bId, station);
        QVERIFY(buttons->isButtonAvailable(Id::Mute));
        controller.stop();
    }

    // Slice control plan Task 15 fix round 1: without the hosting slice
    // requests (the fallback path), a tab click still reaches a listened
    // slice as this window's RX without moving its active slice, and
    // reaches the slice it controls.
    void hostTabFallbackReachesAListenedSlice()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-0"));
        SliceModel* a = model->sliceById(aId);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(a && b);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        DeviceSessionRegistry::Entry phone;
        phone.deviceId = QByteArrayLiteral("phone-device-id-for-rx-applet-01");
        phone.kind = DeviceSessionRegistry::Kind::Paired;
        phone.name = QStringLiteral("Living room iPhone");
        phone.shortName = QStringLiteral("iPhone");
        phone.deviceKind = QStringLiteral("phone");
        QCOMPARE(server->deviceSessions()->admit(phone, &phoneSession).admission,
                 DeviceSessionRegistry::Admission::Admitted);
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        ownership->setOwner(bId, phone.deviceId);
        QVERIFY(ownership->isListening(station, bId));

        window.dropHostingSliceActionsForTest();
        RxApplet* applet = window.findChild<RxApplet*>();
        QVERIFY(applet);
        QToolButton* tabA = sliceTabFor(*applet, QLatin1Char('A'));
        QToolButton* tabB = sliceTabFor(*applet, QLatin1Char('B'));
        QVERIFY(tabA && tabB);
        const int activeBefore = ownership->activeFor(station);
        QCOMPARE(activeBefore, aId);

        tabB->click();
        QCOMPARE(ownership->activeRxFor(station), bId);
        QCOMPARE(ownership->activeFor(station), activeBefore);
        QCOMPARE(applet->slice(), b);
        QVERIFY(applet->isListening());

        tabA->click();
        QCOMPARE(ownership->activeRxFor(station), aId);
        QCOMPARE(ownership->activeFor(station), aId);
        QCOMPARE(applet->slice(), a);
        QVERIFY(!applet->isListening());
        controller.stop();
    }

    void appletAndContainerAskWithoutOptimisticKey()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        QVERIFY(model->addSlice(QStringLiteral("pan-0")) >= 0);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        TxApplet* tx = window.findChild<TxApplet*>();
        QVERIFY(tx && tx->moxButton() && tx->tuneButton());
        TransmitHolder* holder = controller.server()->transmitHolder();
        tx->tuneButton()->click();
        QTRY_VERIFY(holder->isHeldBy(SliceOwnership::stationDevice()));
        QVERIFY(!tx->tuneButton()->isChecked() || model->isTune());
        controller.requestTune(false);
        QTRY_VERIFY(!model->isTune());
        holder->release(SliceOwnership::stationDevice(), QStringLiteral("test hand-back"));
        QTRY_VERIFY(holder->state() == TransmitHolder::State::Unheld);
        QObject peerSession;
        DeviceSessionRegistry::Entry peer;
        peer.deviceId = QByteArrayLiteral("token:phone");
        peer.kind = DeviceSessionRegistry::Kind::Token;
        peer.name = QStringLiteral("Phone");
        peer.shortName = QStringLiteral("Phone");
        peer.deviceKind = QStringLiteral("phone");
        StationServer* server = controller.server();
        QCOMPARE(server->deviceSessions()->admit(peer, &peerSession).admission,
                 DeviceSessionRegistry::Admission::Admitted);
        TransmitHolder::KeyRequest key;
        key.deviceId = peer.deviceId;
        QCOMPARE(holder->askKey(key).verdict, KeyingVerdict::Admit);
        tx->moxButton()->click();
        TakeTransmitDialog* question = window.findChild<TakeTransmitDialog*>();
        QVERIFY(question);
        QCOMPARE(question->questionLabel()->text(), QStringLiteral("Take transmit from Phone?"));
        QVERIFY(!tx->moxButton()->isChecked());
        QVERIFY(holder->isHeldBy(peer.deviceId));
        QPointer<TakeTransmitDialog> cancelled(question);
        question->cancelButton()->click();
        QTRY_VERIFY(cancelled.isNull());
        QVERIFY(!model->mox());

        holder->setKeyed(true);
        tx->moxButton()->click();
        question = window.findChild<TakeTransmitDialog*>();
        QVERIFY(question);
        QVERIFY(question->redButton());
        QCOMPARE(question->takeButton()->text(), QStringLiteral("Unkey and take over"));
        QVERIFY(!tx->moxButton()->isChecked());
        QPointer<TakeTransmitDialog> keyedQuestion(question);
        question->cancelButton()->click();
        QTRY_VERIFY(keyedQuestion.isNull());
        holder->setKeyed(false);

        ContainerWidget* container = window.findChild<ContainerWidget*>();
        QVERIFY(container);
        QVERIFY(QMetaObject::invokeMethod(container, "otherButtonClicked", Qt::DirectConnection,
            Q_ARG(int, int(OtherButtonItem::ButtonId::Tun))));
        question = window.findChild<TakeTransmitDialog*>();
        QVERIFY(question);
        QVERIFY(!tx->tuneButton()->isChecked());
        QVERIFY(holder->isHeldBy(peer.deviceId));
        QPointer<TakeTransmitDialog> confirmed(question);
        question->takeButton()->click();
        QTRY_VERIFY(confirmed.isNull());
        QTRY_VERIFY(holder->isHeldBy(SliceOwnership::stationDevice()));
        QVERIFY(!tx->tuneButton()->isChecked() || model->isTune());
        controller.requestTune(false);
        QTRY_VERIFY(!model->isTune());
        holder->release(SliceOwnership::stationDevice(), QStringLiteral("test hand-back"));
        QTRY_VERIFY(holder->state() == TransmitHolder::State::Unheld);
        QCOMPARE(holder->askKey(key).verdict, KeyingVerdict::Admit);
        tx->moxButton()->click();
        question = window.findChild<TakeTransmitDialog*>();
        QVERIFY(question);
        QPointer<TakeTransmitDialog> pending(question);
        controller.stop();
        QVERIFY(!pending || !pending->isVisible());
        QVERIFY(!controller.enabled());
        QCOMPARE(tx->tuneButton()->isChecked(), model->isTune());
    }

    // Slice control plan Task 16 (ruling U7): a layout change that takes
    // away the pan showing a slice this window listens to (another device
    // controls it) stops listening to it with a notice, and changes nothing
    // about the slice: no slice is added or removed, and its stream, its
    // receiver, its tuning and its pan for the controller all stay.
    void layoutChangeStopsListeningToASliceItNoLongerShows()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-1"));
        QVERIFY(applyLayout(window, QStringLiteral("2v")));
        auto* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        QCOMPARE(stack->currentLayoutId(), QStringLiteral("2v"));
        QCOMPARE(model->slices().size(), 2);
        SliceModel* b = model->sliceById(bId);
        QVERIFY(b);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        const DeviceSessionRegistry::Entry phone =
            admitPhone(*server, phoneSession, QByteArrayLiteral("phone-device-id-for-layouts-0001"));
        QVERIFY(!phone.deviceId.isEmpty());
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        ownership->setOwner(bId, phone.deviceId);
        QVERIFY(ownership->isListening(station, bId));
        QVERIFY(flagFor(window, bId) && flagFor(window, bId)->isListening());

        QHash<int, int> ddcs;
        QHash<int, int> streams;
        for (SliceModel* slice : model->slices()) {
            streams.insert(slice->sliceIndex(), slice->streamIndex());
            ddcs.insert(slice->sliceIndex(), model->ddcForStream(slice->streamIndex()));
        }
        const int bDdc = b->ddcIndex();
        const double frequency = b->frequency();
        QSignalSpy added(model, &RadioModel::sliceAdded);
        QSignalSpy removed(model, &RadioModel::sliceRemoved);
        QSignalSpy tuned(b, &SliceModel::frequencyChanged);
        QSignalSpy moved(b, &SliceModel::panKeyChanged);

        QVERIFY(applyLayout(window, QStringLiteral("1")));
        QCOMPARE(stack->currentLayoutId(), QStringLiteral("1"));
        QVERIFY(!ownership->isListening(station, bId));
        QCOMPARE(ownership->mark(bId).subject(), phone.deviceId);
        QCOMPARE(toastsSaying(window, QStringLiteral(
                     "Stopped listening to Slice B: it is no longer shown in this window.")), 1);
        QCOMPARE(added.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(model->slices().size(), 2);
        for (SliceModel* slice : model->slices()) {
            QCOMPARE(slice->streamIndex(), streams.value(slice->sliceIndex()));
            QCOMPARE(model->ddcForStream(slice->streamIndex()), ddcs.value(slice->sliceIndex()));
        }
        QCOMPARE(b->ddcIndex(), bDdc);
        QCOMPARE(b->frequency(), frequency);
        QCOMPARE(tuned.count(), 0);
        QCOMPARE(moved.count(), 0);
        QCOMPARE(b->panKey(), QStringLiteral("pan-1"));
        QCOMPARE(model->sliceById(aId)->panKey(), QStringLiteral("pan-0"));
        // The flag survives its pan and shows nothing for this window.
        VfoWidget* flagB = flagFor(window, bId);
        QVERIFY(flagB);
        QVERIFY(!flagB->stationPresentationAllowed());
        QCOMPARE(flagCountFor(window, bId), 1);
        controller.stop();
    }

    // Slice control plan Task 16 (ruling U1): listening to a slice this
    // window does not show places it in the main window. With one pan and
    // no empty one, the window grows to the next layout and the slice
    // appears on the new pan. No slice is added, and the slice's pan for
    // its controller does not move.
    void listeningToAnUnseenSliceGrowsTheMainWindow()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        model->addSlice(QStringLiteral("pan-0"));
        QVERIFY(applyLayout(window, QStringLiteral("1")));
        auto* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        StationServer* server = controller.server();
        QVERIFY(server);
        QObject phoneSession;
        const DeviceSessionRegistry::Entry phone =
            admitPhone(*server, phoneSession, QByteArrayLiteral("phone-device-id-for-layouts-0002"));
        QVERIFY(!phone.deviceId.isEmpty());
        // The phone's slice, on a pan this window does not have.
        const int bId = model->addSlice(QStringLiteral("pan-3"));
        SliceModel* b = model->sliceById(bId);
        QVERIFY(b);
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        ownership->setOwner(bId, phone.deviceId);
        ownership->leave(station, bId);
        QVERIFY(!ownership->isListening(station, bId));
        QCOMPARE(stack->currentLayoutId(), QStringLiteral("1"));
        const int count = model->slices().size();
        QSignalSpy added(model, &RadioModel::sliceAdded);
        QSignalSpy moved(b, &SliceModel::panKeyChanged);

        RxDashboard* dashboard = window.findChild<RxDashboard*>();
        QVERIFY(dashboard);
        dashboard->chooserButton()->click();
        SliceChooser* chooser = window.findChild<SliceChooser*>();
        QVERIFY(chooser);
        emit chooser->listenRequested(bId);
        QVERIFY(ownership->isListening(station, bId));
        QCOMPARE(stack->currentLayoutId(), QStringLiteral("2v"));
        PanadapterApplet* grown = stack->panadapter(QStringLiteral("pan-1"));
        QVERIFY(grown);
        QVERIFY(grown->associatedSlices().contains(bId));
        QVERIFY(!stack->panadapter(QStringLiteral("pan-0"))->associatedSlices().contains(bId));
        VfoWidget* flagB = flagFor(window, bId);
        QVERIFY(flagB);
        QCOMPARE(flagB->parentWidget(), grown->spectrumWidget());
        QVERIFY(flagB->stationPresentationAllowed());
        QCOMPARE(flagCountFor(window, bId), 1);
        QCOMPARE(model->slices().size(), count);
        QCOMPARE(added.count(), 0);
        QCOMPARE(moved.count(), 0);
        QCOMPARE(b->panKey(), QStringLiteral("pan-3"));
        QCOMPARE(ownership->mark(bId).subject(), phone.deviceId);
        controller.stop();
    }

    // Slice control plan Task 16 (ruling U2): selecting a slice shown in a
    // floating pan brings that pan forward and makes the slice this
    // window's RX. Nothing moves and no second flag appears.
    void selectingASliceInAFloatingPanBringsItForward()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        const int bId = model->addSlice(QStringLiteral("pan-1"));
        QVERIFY(applyLayout(window, QStringLiteral("2v")));
        auto* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        stack->setActivePan(QStringLiteral("pan-0"));
        stack->floatPanadapter(QStringLiteral("pan-1"));
        QVERIFY(stack->floatingWindowForTest(QStringLiteral("pan-1")));
        // The floating pan put away. (The offscreen platform keeps no
        // stacking order and will not move activation off the floating pan,
        // so a minimized floating pan stands in for one hidden behind other
        // windows.)
        PanFloatingWindow* floater = stack->floatingWindowForTest(QStringLiteral("pan-1"));
        floater->showMinimized();
        QTRY_VERIFY(floater->isMinimized());
        QCOMPARE(ownership->activeRxFor(station), aId);
        SliceModel* b = model->sliceById(bId);
        QSignalSpy moved(b, &SliceModel::panKeyChanged);
        const int count = model->slices().size();

        RxApplet* applet = window.findChild<RxApplet*>();
        QVERIFY(applet);
        QToolButton* tabB = sliceTabFor(*applet, QLatin1Char('B'));
        QVERIFY(tabB);
        tabB->click();
        QCOMPARE(ownership->activeRxFor(station), bId);
        QCOMPARE(stack->activePanId(), QStringLiteral("pan-1"));
        QCOMPARE(stack->floatingWindowForTest(QStringLiteral("pan-1")), floater);
        QVERIFY(floater->isVisible());
        QTRY_VERIFY(!floater->isMinimized());
        QCOMPARE(b->panKey(), QStringLiteral("pan-1"));
        QCOMPARE(moved.count(), 0);
        QCOMPARE(flagCountFor(window, bId), 1);
        QCOMPARE(flagFor(window, bId)->parentWidget(),
                 stack->panadapter(QStringLiteral("pan-1"))->spectrumWidget());
        QCOMPARE(model->slices().size(), count);
        controller.stop();
    }

    // Slice control plan Task 16 (ruling U3): a click on a pan's background
    // gives that pan keyboard and scroll focus only. The slice this window
    // hears and the slice it tunes stay where they were.
    void panBackgroundClickNeverChangesTheSlice()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("Qt reports no working TLS backend."); }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int aId = model->addSlice(QStringLiteral("pan-0"));
        model->addSlice(QStringLiteral("pan-1"));
        QVERIFY(applyLayout(window, QStringLiteral("2v")));
        auto* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        DesktopStationController controller(model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(&controller);
        QVERIFY(controller.start(true));
        SliceOwnership* ownership = model->sliceOwnership();
        const QByteArray station = SliceOwnership::stationDevice();
        QCOMPARE(ownership->activeRxFor(station), aId);
        const int activeBefore = ownership->activeFor(station);
        const int modelActiveBefore = model->activeSlice() ? model->activeSlice()->sliceIndex() : -1;
        PanadapterApplet* pan1 = stack->panadapter(QStringLiteral("pan-1"));
        QVERIFY(pan1);
        emit pan1->activated(QStringLiteral("pan-1"));
        QCOMPARE(ownership->activeRxFor(station), aId);
        QCOMPARE(ownership->activeFor(station), activeBefore);
        QCOMPARE(model->activeSlice() ? model->activeSlice()->sliceIndex() : -1, modelActiveBefore);
        controller.stop();
    }
};

QTEST_MAIN(TstDesktopStationWindow)
#include "tst_desktop_station_window.moc"
