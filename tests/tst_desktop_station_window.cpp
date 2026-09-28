// no-port-check: NereusSDR-original. Desktop host presentation over a borrowed local model.
#include "gui/MainWindow.h"

#include "core/AppSettings.h"
#include "core/SliceOwnership.h"
#include "core/TciServer.h"
#include "core/safety/TransmitHolder.h"
#include "core/session/DeviceSessionRegistry.h"
#include "core/session/StationServer.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/meters/OtherButtonItem.h"
#include "gui/applets/TxApplet.h"
#include "gui/multidevice/TakeTransmitDialog.h"
#include "gui/SpectrumWidget.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/widgets/VfoWidget.h"
#include "gui/widgets/RxDashboard.h"
#include "gui/SetupDialog.h"
#include "gui/setup/DspSetupPages.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/NotchModel.h"

#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
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
}

class TstDesktopStationWindow final : public QObject {
    Q_OBJECT
private slots:
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
        QVERIFY(flagA->stationPresentationAllowed());
        QVERIFY(!flagB->stationPresentationAllowed());
        QVERIFY(!flagB->txSliceShown());
        SpectrumWidget* spectrum = qobject_cast<SpectrumWidget*>(flagB->parentWidget());
        QVERIFY(spectrum);
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
        auto replacement = std::make_unique<DesktopStationController>(
            model, optionsFor(settings, directory.path()));
        window.setDesktopStationController(replacement.get());
        QVERIFY(replacement->start(true));
        QVERIFY(tci->desktopHostMode());
        replacement.reset();
        QVERIFY(!tci->desktopHostMode());
        QVERIFY(flagB->stationPresentationAllowed());
        SetupDialog* observed = nullptr;
        connect(&window, &MainWindow::setupDialogCreated, &window,
                [&observed](SetupDialog* dialog) { observed = dialog; });
        QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
        QVERIFY(observed);
        observed->close();
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
};

QTEST_MAIN(TstDesktopStationWindow)
#include "tst_desktop_station_window.moc"
