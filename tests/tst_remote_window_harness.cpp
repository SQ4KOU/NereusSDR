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
// =================================================================

#include <QtTest/QtTest>

#include <QAction>
#include <QLoggingCategory>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>

#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "gui/MainWindow.h"
#include "gui/RemoteConnectionController.h"
#include "gui/SetupDialog.h"
#include "gui/SpectrumWidget.h"
#include "gui/TitleBar.h"
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

// Cancel surfaces (cancelDuringBackoffStopsTheRetry).
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
        RemoteWindowHarness::useIsolatedProfile(QStringLiteral("remote-window-harness"));
    }

    void init()
    {
        RemoteWindowHarness::clearIsolatedProfile();
    }

    void cleanupTestCase()
    {
        RemoteWindowHarness::removeIsolatedProfile();
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

    // R-R3-16: after a manual Disconnect the window's automatic reopen
    // path still runs. It must never dial. What it opens depends on mode:
    // with the Connections picker managing the window it asks for
    // Connections; in direct mode (a --station window) it opens nothing.
    void manualDisconnectThenAutomaticReopen_data()
    {
        QTest::addColumn<bool>("picker");
        QTest::addColumn<int>("connectionsOpened");
        QTest::newRow("direct mode") << false << 0;
        QTest::newRow("picker mode") << true << 1;
    }

    void manualDisconnectThenAutomaticReopen()
    {
        QFETCH(bool, picker);
        QFETCH(int, connectionsOpened);
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

        QSignalSpy connectionsRequested(h.window(), &MainWindow::connectionsRequested);
        QVERIFY(disconnectFromRadioMenu(h));
        QTest::qWait(kSettleMs);

        QCOMPARE(connectionsRequested.size(), connectionsOpened);
        QCOMPARE(h.acceptedConnections(), 1);
        QVERIFY(!client->isConnectionActive());
        QVERIFY(!client->isReconnectPending());
        QCOMPARE(h.controls()->statusText(), QStringLiteral("Core disconnected"));
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
        const QStringList stationIds = sliceIds(h.station());
        QCOMPARE(stationIds.size(), stationSlices);
        // The saved layout was restored: its last pan exists.
        QVERIFY(h.panSpectrum(MainWindow::panIdsForLayout(layout).constLast()));

        for (int attachment = 0; attachment < 2; ++attachment) {
            h.holdNextSnapshot();
            if (attachment == 0) {
                h.startStartupConnection();
            } else {
                h.dropLink();
            }
            QTRY_VERIFY_WITH_TIMEOUT(h.acceptedConnections() == attachment + 1
                                     && h.snapshotHeld(), 10000);

            // Capabilities say the radio is connected; the slices have not
            // arrived. The window's connect handler queues its real
            // populateEmptyPans(); give it every chance to run.
            QTRY_VERIFY(remote->isConnected());
            QTest::qWait(kSettleMs);
            QVERIFY(!client->isHandshakeComplete());
            QVERIFY2(h.addSliceCommands().isEmpty(),
                     qPrintable(h.addSliceCommands().join(QLatin1Char(','))));
            QCOMPARE(stationAdds.size(), 0);
            QCOMPARE(sliceIds(h.station()), stationIds);

            h.releaseSnapshot();
            QTRY_VERIFY_WITH_TIMEOUT(client->isHandshakeComplete(), 10000);
            QTest::qWait(kSettleMs);
            QVERIFY2(h.addSliceCommands().isEmpty(),
                     qPrintable(h.addSliceCommands().join(QLatin1Char(','))));
            QCOMPARE(stationAdds.size(), 0);
            QCOMPARE(sliceIds(h.station()), stationIds);
            QCOMPARE(sliceIds(*remote), stationIds);
        }

        // Hydration and layout restore did not create; an explicit operator
        // create still does, once, within the station's capacity.
        QAction* add = h.menuAction(QStringLiteral("&View"),
                                    QStringLiteral("&Add slice on active pan"));
        QVERIFY(add);
        add->trigger();
        QTRY_COMPARE(h.addSliceCommands().size(), 1);
        QVERIFY(h.addSliceCommands().first().startsWith(QStringLiteral("addSliceOnPan:pan-")));
        QTRY_COMPARE(h.station().slices().size(), stationSlices + 1);
        QTRY_COMPARE(remote->slices().size(), stationSlices + 1);
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

        StationCapabilities granted = h.server().buildCapabilities();
        granted.txPermitted = true;
        h.pushCapabilities(granted);
        QTRY_VERIFY(txEq->isEnabled());
        QVERIFY(client->capabilities().txPermitted);
        QVERIFY(txEq->toolTip() != reason);

        StationCapabilities withdrawn = h.server().buildCapabilities();
        withdrawn.txPermitted = false;
        h.pushCapabilities(withdrawn);
        QTRY_VERIFY(!txEq->isEnabled());
        QCOMPARE(txEq->toolTip(), reason);

        QCOMPARE(client->sessionEpoch(), epoch);
        QCOMPARE(h.acceptedConnections(), 1);
        QVERIFY(client->isHandshakeComplete());
    }
};

QTEST_MAIN(TestRemoteWindowHarness)
#include "tst_remote_window_harness.moc"
