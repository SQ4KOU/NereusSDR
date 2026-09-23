// =================================================================
// tests/tst_remote_window_harness.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Drives one real remote MainWindow
// against an in-process Core; no upstream logic is ported.
//
// R3 remote window harness plan, Task 2 (R-R3-16, R-R3-17, R-R3-21,
// R-R3-24). Every case starts from the window's own controls: a menu
// QAction, a mouse click on a chrome widget or a pan, or a button in a
// dialog the window opened. Nothing here calls a MainWindow slot or
// StationClient directly to make something happen; the harness only
// reads state back and plays the Core's side of the wire.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  R3 remote window harness plan, Task 2.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R3 remote window Setup plan, Task 3
//                                    (R-R3-16, R-R3-17, R-R3-38):
//                                    Connections opens only after the
//                                    operator's Disconnect. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLoggingCategory>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QTreeWidget>

#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "gui/MainWindow.h"
#include "gui/RemoteConnectionController.h"
#include "gui/SetupDialog.h"
#include "gui/SpectrumWidget.h"
#include "gui/TitleBar.h"
#include "gui/applets/RadeApplet.h"
#include "gui/widgets/StationBlock.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/RemoteWindowHarness.h"

using namespace NereusSDR;
using NereusSDR::Test::RemoteWindowHarness;

namespace {

// Long enough for several queued turns and any backoff step a case uses
// to fire if something were wrongly scheduled.
constexpr int kSettleMs = 300;

// Entry points (entryPointsStartAnExplicitConnect).
constexpr int kTitleBar = 0;
constexpr int kStationBlock = 1;
constexpr int kDisconnectedPan = 2;
constexpr int kSetupConnections = 3;

// Disconnect surfaces (cancelDuringBackoffStopsTheRetry,
// operatorDisconnectOpensConnectionsOnce).
constexpr int kRadioMenuDisconnect = 0;
constexpr int kCorePanelDisconnect = 1;

void clickLeft(QWidget* widget)
{
    QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier,
                      widget->rect().center());
}

// Radio > Connect, then wait for the Core's snapshot to be applied.
bool connectFromRadioMenu(RemoteWindowHarness& h)
{
    QAction* connect = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Connect"));
    if (!connect || !connect->isEnabled()) { return false; }
    connect->trigger();
    return QTest::qWaitFor([&h] { return h.client()->isHandshakeComplete(); }, 10000);
}

bool disconnectFromRadioMenu(RemoteWindowHarness& h)
{
    QAction* disconnect = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Disconnect"));
    if (!disconnect || !disconnect->isEnabled()) { return false; }
    disconnect->trigger();
    return true;
}

bool corePanelVisible(RemoteWindowHarness& h)
{
    auto* panel = h.window()->findChild<RemoteConnectionPanel*>();
    return panel && panel->isVisible();
}

// The Core panel, reached the way an operator reaches it on a connected
// window: right-click the title bar's connection segment and choose
// "Core connection details..." from the menu it opens.
bool openCorePanelFromTitleMenu(RemoteWindowHarness& h)
{
    ConnectionSegment* segment = h.titleSegment();
    if (!segment) { return false; }
    bool chosen = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) { return; }
        poll.stop();
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Core connection details...")) {
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
                chosen = true;
                return;
            }
        }
        menu->close();
    });
    poll.start();
    QTest::mouseClick(segment, Qt::RightButton, Qt::NoModifier, segment->rect().center());
    poll.stop();
    return chosen && corePanelVisible(h);
}

// The Setup dialog's Remote Station page, reached the way an operator
// reaches it: File > Settings..., then its entry in the page tree.
QPushButton* openSetupConnectionsButton(RemoteWindowHarness& h)
{
    QAction* settings = h.menuAction(QStringLiteral("&File"), QStringLiteral("&Settings..."));
    if (!settings) { return nullptr; }
    settings->trigger();
    SetupDialog* dialog = h.window()->findChild<SetupDialog*>();
    if (!dialog) { return nullptr; }
    auto* tree = dialog->findChild<QTreeWidget*>();
    if (!tree) { return nullptr; }
    const auto found = tree->findItems(QStringLiteral("Remote Station"),
                                       Qt::MatchExactly | Qt::MatchRecursive);
    if (found.isEmpty()) { return nullptr; }
    tree->setCurrentItem(found.first());
    return dialog->findChild<QPushButton*>(QStringLiteral("remoteStationConnections"));
}

QStringList sliceIds(const RadioModel& model)
{
    QStringList ids;
    for (SliceModel* slice : model.slices()) {
        ids << QString::number(slice->sliceIndex());
    }
    ids.sort();
    return ids;
}

} // namespace

class TestRemoteWindowHarness final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // A whole window logs every settings read at debug level; keep the
        // info and warning lines that explain a failure.
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        QVERIFY(RemoteWindowHarness::useIsolatedProfile(QStringLiteral("remote-window-harness")));
    }

    void init()
    {
        QVERIFY(RemoteWindowHarness::clearIsolatedProfile());
    }

    void cleanupTestCase()
    {
        QVERIFY(RemoteWindowHarness::removeIsolatedProfile());
    }

    // R-R3-16: each disconnected entry point dials the configured Core once.
    void entryPointsStartAnExplicitConnect_data()
    {
        QTest::addColumn<int>("entry");
        QTest::newRow("title bar") << kTitleBar;
        QTest::newRow("station block") << kStationBlock;
        QTest::newRow("disconnected pan") << kDisconnectedPan;
        QTest::newRow("Setup Connections") << kSetupConnections;
    }

    void entryPointsStartAnExplicitConnect()
    {
        QFETCH(int, entry);
        RemoteWindowHarness h;
        QVERIFY(h.start());
        StationClient* const client = h.client();
        QVERIFY(client);

        // Nothing dials by itself: the window was built with its startup
        // connection deferred, and showing it runs no automatic dial.
        QTest::qWait(kSettleMs);
        QCOMPARE(h.acceptedConnections(), 0);
        QVERIFY(!client->isConnectionActive());

        int expectedDials = 1;
        QPointer<QPushButton> setupConnections;
        if (entry == kSetupConnections) {
            // A remote window opens Setup only while it holds the Core's
            // settings (the gate in MainWindow::createSetupDialog), so the
            // operator's route is: Setup opened during a session, the
            // session ended, then Connections... in the Setup still open.
            QVERIFY(connectFromRadioMenu(h));
            setupConnections = openSetupConnectionsButton(h);
            QVERIFY(setupConnections);
            QVERIFY(disconnectFromRadioMenu(h));
            QTRY_VERIFY(!client->isConnectionActive());
            QTest::qWait(kSettleMs);
            QCOMPARE(h.acceptedConnections(), 1);
            expectedDials = 2;
        }

        switch (entry) {
        case kTitleBar: {
            ConnectionSegment* segment = h.titleSegment();
            QVERIFY(segment);
            QCOMPARE(segment->state(), ConnectionState::Disconnected);
            clickLeft(segment);
            break;
        }
        case kStationBlock: {
            StationBlock* block = h.stationBlock();
            QVERIFY(block);
            clickLeft(block);
            break;
        }
        case kDisconnectedPan: {
            SpectrumWidget* pan = h.panSpectrum(QStringLiteral("pan-0"));
            QVERIFY(pan);
            clickLeft(pan);
            break;
        }
        case kSetupConnections: {
            QVERIFY(setupConnections);
            QVERIFY(setupConnections->isVisible());
            setupConnections->click();
            break;
        }
        }

        QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);
        QCOMPARE(h.acceptedConnections(), expectedDials);
        QCOMPARE(h.controls()->state(), ConnectionState::Connected);
        QCOMPARE(h.titleSegment()->state(), ConnectionState::Connected);
        QCOMPARE(h.titleSegment()->remoteStatusText(), QStringLiteral("Core connected"));

        // The same gesture on a live session never opens a second one.
        const quint32 epoch = client->sessionEpoch();
        if (entry == kTitleBar) {
            clickLeft(h.titleSegment());
        } else if (entry == kStationBlock) {
            clickLeft(h.stationBlock());
        }
        QTest::qWait(kSettleMs);
        QCOMPARE(h.acceptedConnections(), expectedDials);
        QCOMPARE(client->sessionEpoch(), epoch);
    }

    // R-R3-16 / R-R3-17: cancelling while a retry waits stops it for good.
    void cancelDuringBackoffStopsTheRetry_data()
    {
        QTest::addColumn<int>("surface");
        QTest::newRow("Radio > Disconnect") << kRadioMenuDisconnect;
        QTest::newRow("Core panel Disconnect") << kCorePanelDisconnect;
    }

    void cancelDuringBackoffStopsTheRetry()
    {
        QFETCH(int, surface);
        RemoteWindowHarness::Options options;
        options.backoffUnitMs = 1000;
        RemoteWindowHarness h(options);
        QVERIFY(h.start());
        StationClient* const client = h.client();
        QSignalSpy retries(client, &StationClient::reconnectScheduled);

        // The title bar dial also opens the Core panel the second surface uses.
        clickLeft(h.titleSegment());
        QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);

        h.dropLink();
        QTRY_VERIFY_WITH_TIMEOUT(client->isReconnectPending(), 5000);
        QCOMPARE(retries.size(), 1);
        QCOMPARE(h.controls()->state(), ConnectionState::LinkLost);
        QVERIFY(h.titleSegment()->remoteStatusText().startsWith(QStringLiteral("Retrying Core")));

        if (surface == kRadioMenuDisconnect) {
            QVERIFY(disconnectFromRadioMenu(h));
        } else {
            auto* panel = h.window()->findChild<RemoteConnectionPanel*>();
            QVERIFY(panel);
            auto* stop = panel->findChild<QPushButton*>(QStringLiteral("disconnectCore"));
            QVERIFY(stop);
            QVERIFY(stop->isEnabled());
            stop->click();
        }

        // Past the first retry's deadline: nothing may dial.
        QTest::qWait(2 * options.backoffUnitMs);
        QCOMPARE(h.acceptedConnections(), 1);
        QCOMPARE(retries.size(), 1);
        QVERIFY(!client->isConnectionActive());
        QVERIFY(!client->isReconnectPending());

        // The window stays disconnected and says so where it persists.
        QCOMPARE(h.controls()->state(), ConnectionState::Disconnected);
        QCOMPARE(h.controls()->statusText(), QStringLiteral("Core disconnected"));
        QCOMPARE(h.titleSegment()->state(), ConnectionState::Disconnected);
        QCOMPARE(h.titleSegment()->remoteStatusText(), QStringLiteral("Core disconnected"));
        QVERIFY(h.stationBlock()->radioName().contains(h.controls()->endpointText()));
        QCOMPARE(h.stationBlock()->hardwareLine(), QStringLiteral("Core disconnected"));
        QAction* connect = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Connect"));
        QAction* disconnect = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Disconnect"));
        QVERIFY(connect && disconnect);
        QVERIFY(connect->isEnabled());
        QVERIFY(!disconnect->isEnabled());
    }

    // R-R3-16 / R-R3-38: the operator's own Disconnect opens Connections
    // exactly once with the picker managing the window, and shows the Core
    // panel in direct mode (a --station window has no picker). Neither
    // dials: the Core still accepted exactly one connection. The window's
    // automatic open on a Disconnected state is for local models only, so
    // it adds no second open here.
    void operatorDisconnectOpensConnectionsOnce_data()
    {
        QTest::addColumn<bool>("picker");
        QTest::addColumn<int>("surface");
        QTest::newRow("direct, Radio > Disconnect") << false << kRadioMenuDisconnect;
        QTest::newRow("direct, Core panel Disconnect") << false << kCorePanelDisconnect;
        QTest::newRow("picker, Radio > Disconnect") << true << kRadioMenuDisconnect;
        QTest::newRow("picker, Core panel Disconnect") << true << kCorePanelDisconnect;
    }

    void operatorDisconnectOpensConnectionsOnce()
    {
        QFETCH(bool, picker);
        QFETCH(int, surface);
        RemoteWindowHarness h;
        QVERIFY(h.start());
        // GuiConnectionController makes this call on every window it
        // attaches; a --station window never gets it.
        h.window()->setConnectionPickerManaged(picker);
        StationClient* const client = h.client();

        // The window's own startup connection, as the application starts it.
        h.startStartupConnection();
        QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);
        QVERIFY(h.remoteModel()->isConnected());
        QVERIFY(!h.remoteModel()->name().isEmpty());

        QPointer<RemoteConnectionPanel> panel;
        if (surface == kCorePanelDisconnect) {
            QVERIFY(openCorePanelFromTitleMenu(h));
            panel = h.window()->findChild<RemoteConnectionPanel*>();
            QVERIFY(panel);
            QVERIFY(panel->isVisible());
        } else {
            QVERIFY(!corePanelVisible(h));
        }

        QSignalSpy connectionsRequested(h.window(), &MainWindow::connectionsRequested);
        QSignalSpy operatorDisconnects(h.controls(),
                                       &RemoteConnectionController::operatorDisconnected);
        if (surface == kRadioMenuDisconnect) {
            QVERIFY(disconnectFromRadioMenu(h));
        } else {
            auto* stop = panel->findChild<QPushButton*>(QStringLiteral("disconnectCore"));
            QVERIFY(stop);
            QVERIFY(stop->isEnabled());
            stop->click();
        }
        QTest::qWait(kSettleMs);

        QCOMPARE(operatorDisconnects.size(), 1);
        QCOMPARE(connectionsRequested.size(), picker ? 1 : 0);
        if (!picker || surface == kCorePanelDisconnect) {
            // Direct mode shows the Core panel; a panel the operator
            // already had open stays open.
            QVERIFY(corePanelVisible(h));
        } else {
            QVERIFY(!corePanelVisible(h));
        }
        QCOMPARE(h.acceptedConnections(), 1);
        QVERIFY(!client->isConnectionActive());
        QVERIFY(!client->isReconnectPending());
        QCOMPARE(h.controls()->statusText(), QStringLiteral("Core disconnected"));
    }

    // R-R3-16 / R-R3-17: link loss is not the operator's Disconnect.
    // Nothing opens; the title bar says the window is retrying; the retry
    // reaches the Core.
    void linkLossOpensNothingAndRetries_data()
    {
        QTest::addColumn<bool>("picker");
        QTest::newRow("direct mode") << false;
        QTest::newRow("picker mode") << true;
    }

    void linkLossOpensNothingAndRetries()
    {
        QFETCH(bool, picker);
        RemoteWindowHarness::Options options;
        // Long enough to read the retrying state before the redial.
        options.backoffUnitMs = 1000;
        RemoteWindowHarness h(options);
        QVERIFY(h.start());
        h.window()->setConnectionPickerManaged(picker);
        StationClient* const client = h.client();
        h.startStartupConnection();
        QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);
        QVERIFY(!corePanelVisible(h));

        QSignalSpy connectionsRequested(h.window(), &MainWindow::connectionsRequested);
        QSignalSpy operatorDisconnects(h.controls(),
                                       &RemoteConnectionController::operatorDisconnected);
        QSignalSpy retries(client, &StationClient::reconnectScheduled);
        h.dropLink();
        QTRY_VERIFY_WITH_TIMEOUT(client->isReconnectPending(), 5000);
        QCOMPARE(retries.size(), 1);
        QCOMPARE(h.controls()->state(), ConnectionState::LinkLost);
        QCOMPARE(h.titleSegment()->remoteStatusText(),
                 QStringLiteral("Retrying Core (attempt 1)"));

        // The redial happens, and still nothing opened.
        QTRY_VERIFY_WITH_TIMEOUT(h.acceptedConnections() == 2
                                 && client->isHandshakeComplete(), 10000);
        QTest::qWait(kSettleMs);
        QCOMPARE(connectionsRequested.size(), 0);
        QCOMPARE(operatorDisconnects.size(), 0);
        QVERIFY(!corePanelVisible(h));
        QCOMPARE(h.titleSegment()->remoteStatusText(), QStringLiteral("Core connected"));
    }

    // R-R3-17: a Core session whose radio is offline is not a disconnected
    // window. Nothing opens; the station block says the radio is offline;
    // the session stays up.
    void radioOfflineOpensNothing_data()
    {
        QTest::addColumn<bool>("picker");
        QTest::newRow("direct mode") << false;
        QTest::newRow("picker mode") << true;
    }

    void radioOfflineOpensNothing()
    {
        QFETCH(bool, picker);
        RemoteWindowHarness h;
        QVERIFY(h.start());
        h.window()->setConnectionPickerManaged(picker);
        StationClient* const client = h.client();
        h.startStartupConnection();
        QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);
        QVERIFY(h.remoteModel()->isConnected());
        QVERIFY(!h.remoteModel()->name().isEmpty());
        QVERIFY(!corePanelVisible(h));

        QSignalSpy connectionsRequested(h.window(), &MainWindow::connectionsRequested);
        QSignalSpy operatorDisconnects(h.controls(),
                                       &RemoteConnectionController::operatorDisconnected);
        h.reportRadioOffline();
        QTRY_VERIFY_WITH_TIMEOUT(!h.remoteModel()->isConnected(), 5000);
        QCOMPARE(h.remoteModel()->connectionState(), ConnectionState::Disconnected);
        QTest::qWait(kSettleMs);

        QCOMPARE(h.stationBlock()->hardwareLine(), QStringLiteral("Radio offline"));
        QCOMPARE(h.controls()->state(), ConnectionState::Connected);
        QCOMPARE(h.titleSegment()->remoteStatusText(), QStringLiteral("Core connected"));
        QCOMPARE(connectionsRequested.size(), 0);
        QCOMPARE(operatorDisconnects.size(), 0);
        QVERIFY(!corePanelVisible(h));
        QVERIFY(client->isHandshakeComplete());
        QCOMPARE(h.acceptedConnections(), 1);
    }

    // R-R3-24: the extra-slice startup reproduction through the real
    // connection, snapshot and populateEmptyPans path.
    void heldSnapshotCreatesNoSliceOnConnectOrReconnect_data()
    {
        QTest::addColumn<int>("stationSlices");
        QTest::addColumn<QString>("layout");
        QTest::newRow("one slice, one saved pan") << 1 << QStringLiteral("1");
        QTest::newRow("one slice, two saved pans") << 1 << QStringLiteral("2v");
        QTest::newRow("two slices, two saved pans") << 2 << QStringLiteral("2v");
    }

    void heldSnapshotCreatesNoSliceOnConnectOrReconnect()
    {
        QFETCH(int, stationSlices);
        QFETCH(QString, layout);
        RemoteWindowHarness::Options options;
        options.stationSlices = stationSlices;
        options.panLayout = layout;
        RemoteWindowHarness h(options);
        QVERIFY(h.start());
        StationClient* const client = h.client();
        RadioModel* const remote = h.remoteModel();
        QSignalSpy stationAdds(&h.station(), &RadioModel::sliceAdded);
        QSignalSpy connectedStates(remote, &RadioModel::connectionStateChanged);
        QCOMPARE(sliceIds(h.station()).size(), stationSlices);
        const QStringList panIds = MainWindow::panIdsForLayout(layout);
        // The saved layout was restored: its last pan exists.
        QVERIFY(h.panSpectrum(panIds.constLast()));

        const auto connectedCount = [&connectedStates] {
            int n = 0;
            for (const QList<QVariant>& args : connectedStates) {
                if (args.constFirst().value<ConnectionState>() == ConnectionState::Connected) {
                    ++n;
                }
            }
            return n;
        };

        quint32 previousEpoch = client->sessionEpoch();
        for (int attachment = 0; attachment < 2; ++attachment) {
            if (attachment == 1 && panIds.size() > 1) {
                // Before the link drops, the Core closes whatever sits on the
                // saved layout's last pan, the way an operator at the station
                // would. The window keeps its retained slices across a drop,
                // so without an empty pan the reconnect could not create
                // anything whether or not the guard held; this makes the
                // reconnect half test the guard on its own. The single-pan
                // row cannot test the populatePanSlices guard on reconnect:
                // its only pan always holds the Core's only slice.
                for (SliceModel* slice : h.station().slicesOnPan(panIds.constLast())) {
                    h.station().removeSlice(slice->sliceIndex());
                }
                QTRY_VERIFY(remote->pansWithoutSlices(panIds).contains(panIds.constLast()));
                QCOMPARE(sliceIds(*remote), sliceIds(h.station()));
            }

            // This attachment's baseline: nothing below may add to it.
            const QStringList stationIds = sliceIds(h.station());
            const int addsBefore = static_cast<int>(stationAdds.size());
            const int connectedBefore = connectedCount();

            h.holdNextSnapshot();
            if (attachment == 0) {
                h.startStartupConnection();
            } else {
                h.dropLink();
            }
            QTRY_VERIFY_WITH_TIMEOUT(h.acceptedConnections() == attachment + 1
                                     && h.snapshotHeld(), 10000);

            // Evidence of this attachment's own session before the negative
            // check: a new epoch, and the window's model reporting the radio
            // connected again from this session's capabilities. That report
            // is what queues the window's real populateEmptyPans().
            QTRY_VERIFY_WITH_TIMEOUT(client->sessionEpoch() != previousEpoch
                                     && connectedCount() == connectedBefore + 1, 10000);
            previousEpoch = client->sessionEpoch();
            QVERIFY(remote->isConnected());
            // The slices have not arrived; give the queued call every chance
            // to run.
            QTest::qWait(kSettleMs);
            QVERIFY(!client->isHandshakeComplete());
            QVERIFY2(h.addSliceCommands().isEmpty(),
                     qPrintable(h.addSliceCommands().join(QLatin1Char(','))));
            QCOMPARE(static_cast<int>(stationAdds.size()), addsBefore);
            QCOMPARE(sliceIds(h.station()), stationIds);

            h.releaseSnapshot();
            QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);
            QTest::qWait(kSettleMs);
            QVERIFY2(h.addSliceCommands().isEmpty(),
                     qPrintable(h.addSliceCommands().join(QLatin1Char(','))));
            QCOMPARE(static_cast<int>(stationAdds.size()), addsBefore);
            QCOMPARE(sliceIds(h.station()), stationIds);
            QCOMPARE(sliceIds(*remote), stationIds);
        }

        // Hydration and layout restore did not create; an explicit operator
        // create still does, once, within the station's capacity.
        const int slicesBeforeAdd = static_cast<int>(h.station().slices().size());
        QAction* add = h.menuAction(QStringLiteral("&View"),
                                    QStringLiteral("&Add slice on active pan"));
        QVERIFY(add);
        add->trigger();
        QTRY_COMPARE(h.addSliceCommands().size(), 1);
        QVERIFY(h.addSliceCommands().first().startsWith(QStringLiteral("addSliceOnPan:pan-")));
        QTRY_COMPARE(static_cast<int>(h.station().slices().size()), slicesBeforeAdd + 1);
        QTRY_COMPARE(static_cast<int>(remote->slices().size()), slicesBeforeAdd + 1);
        QTest::qWait(kSettleMs);
        QCOMPARE(h.addSliceCommands().size(), 1);
        QCOMPARE(h.acceptedConnections(), 2);
    }

    // R-R3-21: a capability change from the Core re-gates the window on the
    // live session.
    void capabilityChangeRegatesWithoutReconnect()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        StationClient* const client = h.client();
        QVERIFY(connectFromRadioMenu(h));
        const quint32 epoch = client->sessionEpoch();

        auto* txEq = h.window()->findChild<QAction*>(QStringLiteral("toolsTxEqualizer"));
        QVERIFY(txEq);
        QVERIFY(!client->capabilities().txPermitted);
        QVERIFY(!txEq->isEnabled());
        const QString reason = txEq->toolTip();
        QVERIFY(!reason.isEmpty());
        // The RADE applet's profile combo follows the same push.
        auto* rade = h.window()->findChild<RadeApplet*>();
        QVERIFY(rade);
        QComboBox* const radeProfile = rade->profileComboForTest();
        QVERIFY(!radeProfile->isEnabled());
        QCOMPARE(radeProfile->toolTip(), reason);

        StationCapabilities granted = h.server().buildCapabilities();
        granted.txPermitted = true;
        h.pushCapabilities(granted);
        QTRY_VERIFY(txEq->isEnabled());
        QVERIFY(client->capabilities().txPermitted);
        QVERIFY(txEq->toolTip() != reason);
        QVERIFY(radeProfile->isEnabled());
        QVERIFY(radeProfile->toolTip() != reason);
        // Reset vocoder stays unavailable: the vocoder runs on the Core,
        // and once transmit is permitted it says so instead of the
        // transmit reason.
        QPushButton* const radeReset = rade->resetVocoderButtonForTest();
        QVERIFY(!radeReset->isEnabled());
        QCOMPARE(radeReset->toolTip(),
                 QStringLiteral("The RADE vocoder runs on the station computer and "
                                "cannot be reset from a remote window."));

        StationCapabilities withdrawn = h.server().buildCapabilities();
        withdrawn.txPermitted = false;
        h.pushCapabilities(withdrawn);
        QTRY_VERIFY(!txEq->isEnabled());
        QCOMPARE(txEq->toolTip(), reason);
        QVERIFY(!radeProfile->isEnabled());
        QCOMPARE(radeProfile->toolTip(), reason);
        QCOMPARE(radeReset->toolTip(), reason);

        QCOMPARE(client->sessionEpoch(), epoch);
        QCOMPARE(h.acceptedConnections(), 1);
        QVERIFY(client->isHandshakeComplete());
    }
};

QTEST_MAIN(TestRemoteWindowHarness)
#include "tst_remote_window_harness.moc"
