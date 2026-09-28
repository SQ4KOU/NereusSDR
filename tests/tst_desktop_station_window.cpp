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
#include "gui/widgets/VfoWidget.h"
#include "gui/SetupDialog.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QLabel>
#include <QPointer>
#include <QPushButton>
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
}

class TstDesktopStationWindow final : public QObject {
    Q_OBJECT
private slots:
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
