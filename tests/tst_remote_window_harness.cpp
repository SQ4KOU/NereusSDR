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
//   2026-09-23  J.J. Boyd / KG4VCF  R3 remote window Setup plan, Task 2
//                                    (R-R3-21, R-R3-10, R-R3-17): Setup
//                                    opens on a disconnected window.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R3 remote radio hardware plan, Task 3
//                                    (R-R3-46, R-R3-21): the window's
//                                    attenuator, preamp, auto-attenuate
//                                    and overload controls use the Core's
//                                    `stepAtt` object; an older Core
//                                    leaves them disabled with its
//                                    reason. AI-assisted transformation
//                                    via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R3 receiver audio plan, Task 4
//                                    (R-R3-42): a Core's stored TCI values
//                                    are ignored. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49: the Core page floor drops by
//                                    the three leaves not registered while
//                                    their features are not built.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 3): the RADE applet's profile combo and
//                                    Reset vocoder follow the transmit
//                                    settings gate.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  Transmit group fix wave 2 (M8): the
//                                    permission push is watched on MOX;
//                                    VOX waits for the microphone line.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  D79 (R-IOS-11, R-R3-49): the window
//                                    follows the Core's band plan, strip
//                                    and menu check.
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QRadioButton>
#include <QScopeGuard>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QSpinBox>
#include <QLabel>
#include <QStackedWidget>
#include <QTabWidget>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/safety/TxRefusal.h"
#include "OperatorWording.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/MoxController.h"
#include "core/TxAnalyzer.h"
#include "core/BoardCapabilities.h"
#include "core/SkuUiProfile.h"
#include "core/StepAttenuatorController.h"
#include "core/StepAttenuatorFacade.h"
#include "core/session/StationCapabilities.h"
#include "core/session/IStationLink.h"
#include "core/session/StationClient.h"
#include "gui/meters/MeterPoller.h"
#include "gui/MainWindow.h"
#include "gui/MoxDisplayController.h"
#include "gui/RemoteMediaController.h"
#include "gui/OperatorReasonText.h"
#include "gui/RemoteConnectionController.h"
#include "gui/SetupDialog.h"
#include "gui/SpectrumWidget.h"
#include "gui/TitleBar.h"
#include "gui/applets/RadeApplet.h"
#include "gui/applets/RxApplet.h"
#include "gui/setup/DeviceCard.h"
#include "gui/setup/GeneralOptionsPage.h"
#include "core/IoBoardHl2.h"
#include "gui/setup/HardwarePage.h"
#include "gui/setup/hardware/Hl2IoBoardTab.h"
#include "gui/setup/hardware/AntennaAlexAntennaControlTab.h"
#include "gui/setup/hardware/OcOutputsHfTab.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/accessories/AlexController.h"
#include "core/RadioDiscovery.h"
#include "gui/widgets/VfoWidget.h"
#include "gui/widgets/StationBlock.h"
#include "models/BandPlanManager.h"
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

// File > Settings..., as the operator opens it.
SetupDialog* openSettings(RemoteWindowHarness& h)
{
    QAction* settings = h.menuAction(QStringLiteral("&File"), QStringLiteral("&Settings..."));
    if (!settings) { return nullptr; }
    settings->trigger();
    return h.window()->findChild<SetupDialog*>();
}

// Selects a Setup leaf in the page tree and returns the page on screen.
QWidget* showSetupLeaf(SetupDialog* dialog, const QString& label)
{
    auto* tree = dialog ? dialog->findChild<QTreeWidget*>() : nullptr;
    auto* stack = dialog ? dialog->findChild<QStackedWidget*>() : nullptr;
    if (!tree || !stack) { return nullptr; }
    const auto found = tree->findItems(label, Qt::MatchExactly | Qt::MatchRecursive);
    if (found.isEmpty()) { return nullptr; }
    tree->setCurrentItem(found.first());
    return stack->currentWidget();
}

// The Setup dialog's Remote Access page, reached the way an operator
// reaches it: File > Settings..., then its entry in the page tree.
QPushButton* openSetupConnectionsButton(RemoteWindowHarness& h)
{
    SetupDialog* dialog = openSettings(h);
    if (!showSetupLeaf(dialog, QStringLiteral("Remote Access"))) { return nullptr; }
    return dialog->findChild<QPushButton*>(QStringLiteral("remoteStationConnections"));
}

DeviceCard* deviceCardOf(QWidget* page, const QString& title)
{
    for (DeviceCard* card : page->findChildren<DeviceCard*>()) {
        if (card->title() == title) { return card; }
    }
    return nullptr;
}

// A card's buffer-size combo: the one whose first entry is 64 samples.
QComboBox* deviceCardBufferCombo(DeviceCard* card)
{
    for (QComboBox* combo : card->findChildren<QComboBox*>()) {
        if (combo->count() > 1 && combo->itemData(0).toInt() == 64) { return combo; }
    }
    return nullptr;
}

const QString kStationReason = QStringLiteral("Connect to the Core to change these.");

// R-R3-46: triggers Radio > Protocol Info and returns the text of the
// dialog it opens (closing it), or an empty string when none opened.
QString protocolInfoText(RemoteWindowHarness& h)
{
    QAction* info = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Protocol Info"));
    if (!info || !info->isEnabled()) { return {}; }
    QString text;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) { return; }
        poll.stop();
        text = box->text();
        box->accept();
    });
    poll.start();
    info->trigger();
    poll.stop();
    return text;
}

QStringList preampLabelsFor(HPSDRHW board)
{
    const BoardCapabilities& caps = BoardCapsTable::forBoard(board);
    QStringList labels;
    for (const auto& item : BoardCapsTable::preampItemsForBoard(board, caps.hasAlexFilters)) {
        labels.append(QString::fromLatin1(item.label));
    }
    return labels;
}

StationCapabilities coreRadio(RemoteWindowHarness& h, HPSDRHW board, HPSDRModel model,
                              const QString& mac)
{
    StationCapabilities caps = h.server().buildCapabilities();
    caps.board = board;
    caps.macAddress = mac;
    caps.radioConnected = true;
    caps.firmwareVersion = QStringLiteral("27");
    caps.radioIdentityEntries = true;
    caps.hpsdrModel = model;
    caps.radioProtocol = 2;
    caps.radioAddress = QStringLiteral("192.168.1.50");
    return caps;
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
    // Opt-in full-window companion to the remote pan capture. The Core and
    // window are real, but the trace rows are test-synthesised at the widget.
    void remoteTransmitDisplayWindowEvidence()
    {
        const QString output = qEnvironmentVariable("NEREUS_TX_DISPLAY_EVIDENCE");
        if (output.isEmpty()) { QSKIP("Set NEREUS_TX_DISPLAY_EVIDENCE to capture PNGs"); }
        auto analyzer = std::make_unique<TxAnalyzer>(TxAnalyzer::kTxDispId);
        RemoteWindowHarness h;
        h.station().setTxAnalyzer(analyzer.get());
        auto* coreSlice = h.station().activeSlice();
        QVERIFY(coreSlice);
        coreSlice->setFrequency(7'236'400.0);
        coreSlice->setDspMode(DSPMode::LSB);
        coreSlice->setFilterLow(-3000);
        coreSlice->setFilterHigh(-100);
        coreSlice->setTxSlice(true);
        QVERIFY(h.start());
        h.startStartupConnection();
        QTRY_VERIFY_WITH_TIMEOUT(h.client()->isHandshakeComplete(), 10000);
        auto* pan = h.panSpectrum(QStringLiteral("pan-0"));
        auto* display = h.window()->findChild<MoxDisplayController*>();
        auto* media = h.window()->findChild<RemoteMediaController*>();
        QVERIFY(pan && display && media);
        pan->setTxMode(DSPMode::LSB);
        pan->setVfoFrequency(7'236'400.0);
        pan->setTxFilterRange(100, 2900);
        pan->setTxFilterVisible(true);
        h.window()->resize(1280, 800);
        auto* mox = h.station().moxController();
        QVERIFY(mox);
        mox->setMoxCheck({});
        mox->setMox(true);
        QTRY_VERIFY_WITH_TIMEOUT(display->isKeyed(), 5000);
        // MainWindow has no media transport seam. Supply this synthetic
        // post-reduction display directly after the Core's key reaches it.
        media->setPanTransmitting(QStringLiteral("pan-0"), true, false);
        pan->setTxCenterFrequency(7'236'400.0);
        pan->setTxSampleRate(8'000.0);
        pan->setDisplayWindowPreservingHistory(7'236'400.0, 8'000.0);
        for (int row = 0; row < 600; ++row) {
            QVector<float> trace(1280);
            for (int index = 0; index < trace.size(); ++index) {
                const double hz = 7'232'400.0 + (index + 0.5) * (8'000.0 / trace.size());
                const float wobble = float(std::sin(index * 0.37 + row * 0.21)) * 3.0f;
                if (hz >= 7'233'500.0 && hz <= 7'236'300.0) {
                    trace[index] = -18.0f + float(std::sin(hz / 420.0 + row * 0.09)) * 9.0f + wobble;
                } else {
                    const double skirt = hz > 7'236'300.0 ? hz - 7'236'300.0 : 7'233'500.0 - hz;
                    trace[index] = std::max(-66.0f + wobble, -30.0f - float(skirt / 60.0));
                }
            }
            pan->updateSpectrumFromTxPixels(-1, trace);
            pan->pushTxWaterfallRow(-1, trace);
            QCoreApplication::processEvents();
        }
        QVERIFY(pan->drawsSpectrumTrace());
        QVERIFY(h.window()->grab().save(output + QStringLiteral("/desktop-keyed-full-window.png")));
        h.station().swrProt().setEnabled(true);
        h.station().swrProt().setWindBackEnabled(true);
        for (int sample = 0; sample < 50 && !h.station().swrProt().highSwr(); ++sample) {
            h.station().swrProt().ingest(50.0f, 15.0f, false);
        }
        QTRY_VERIFY_WITH_TIMEOUT(pan->isHighSwrOverlayActive(), 5000);
        QVERIFY(pan->isHighSwrFoldback());
        QVERIFY(h.window()->grab().save(output + QStringLiteral("/desktop-high-swr-full-window.png")));
        mox->setMox(false);
        h.station().setTxAnalyzer(nullptr);
    }

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
            // R-R3-21 / R-R3-10: Setup opens on the disconnected window
            // itself (it used to be refused until the Core's settings
            // arrived), and Remote Station is this computer's page.
            setupConnections = openSetupConnectionsButton(h);
            QVERIFY(setupConnections);
            QVERIFY(setupConnections->isEnabled());
            QTest::qWait(kSettleMs);
            QCOMPARE(h.acceptedConnections(), 0);
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

    void setupConnectionsAsksManagedPickerWithoutDialing()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        h.window()->setConnectionPickerManaged(true);
        QSignalSpy requests(h.window(), &MainWindow::connectionsRequested);
        QPushButton* connections = openSetupConnectionsButton(h);
        QVERIFY(connections);
        QVERIFY(connections->isEnabled());
        connections->click();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(h.acceptedConnections(), 0);
        QVERIFY(!h.client()->isConnectionActive());
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

    // R-R3-21 / R-R3-10 / R-R3-17: a remote window that has never
    // connected opens Setup. This computer's settings work (a Devices
    // change sticks and is the one used once connected); the Core's
    // settings wait, with the reason, and nothing reaches the Core; once
    // connected the Core's pages are built from the Core's values; after
    // Disconnect they are disabled again.
    void disconnectedWindowSetupKeepsThisComputersSettings()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        StationClient* const client = h.client();
        QTest::qWait(kSettleMs);
        QVERIFY(!client->isConnectionActive());
        QSignalSpy writes(&h.proxy(), &SettingsProxy::outboundWriteRequested);
        QSignalSpy removes(&h.proxy(), &SettingsProxy::outboundRemoveRequested);
        // Seeds the window's own models made at startup, before Setup.
        const QSet<QString> seededAtStartup = h.proxy().droppedWhileOffline();

        SetupDialog* const dialog = openSettings(h);
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        auto* const notice = dialog->findChild<QLabel*>(QStringLiteral("setupStationUnavailable"));
        QVERIFY(notice);

        // A Core page: a stand-in with the reason, not ship defaults.
        QWidget* nb = showSetupLeaf(dialog, QStringLiteral("NB/SNB"));
        QVERIFY(nb);
        QCOMPARE(nb->objectName(), QStringLiteral("setupStationPlaceholder"));
        QVERIFY(!nb->isEnabled());
        QVERIFY(notice->isVisible());
        QCOMPARE(notice->text(), kStationReason);

        // This computer's microphone buffer, on Devices.
        QWidget* const devices = showSetupLeaf(dialog, QStringLiteral("Devices"));
        QVERIFY(devices);
        QVERIFY(devices->isEnabled());
        QVERIFY(!notice->isVisible());
        DeviceCard* const mic = deviceCardOf(devices, QStringLiteral("TX Input (Microphone)"));
        QVERIFY(mic);
        QVERIFY(mic->isEnabled());
        QComboBox* const buffer = deviceCardBufferCombo(mic);
        QVERIFY(buffer);
        const int next = (buffer->currentIndex() + 1) % buffer->count();
        const int samples = buffer->itemData(next).toInt();
        buffer->setCurrentIndex(next);  // the card saves after its 200 ms debounce
        const QString bufferKey = QStringLiteral("audio/TxInput/BufferSamples");
        QTRY_COMPARE(AppSettings::instance().value(bufferKey).toString(), QString::number(samples));
        QTRY_COMPARE(h.remoteModel()->localAudioDevices()->txInputConfig().bufferSamples, samples);

        // Nothing towards the Core, and nothing held to be sent later.
        QCOMPARE(writes.size(), 0);
        QCOMPARE(removes.size(), 0);
        QVERIFY((h.proxy().droppedWhileOffline() - seededAtStartup).isEmpty());
        QCOMPARE(h.acceptedConnections(), 0);

        // Connect from the window. The Core's page is built from the
        // Core's settings and is live.
        QVERIFY(connectFromRadioMenu(h));
        QTRY_VERIFY_WITH_TIMEOUT(
            (nb = showSetupLeaf(dialog, QStringLiteral("NB/SNB")))
                && nb->objectName() != QStringLiteral("setupStationPlaceholder")
                && nb->isEnabled(),
            5000);
        QVERIFY(!notice->isVisible());

        // The Devices change stuck and is the one the window uses; it never
        // went to the Core.
        QCOMPARE(AppSettings::instance().value(bufferKey).toString(), QString::number(samples));
        QCOMPARE(h.remoteModel()->localAudioDevices()->txInputConfig().bufferSamples, samples);
        QVERIFY(!h.stationSettings().contains(bufferKey));
        QVERIFY(showSetupLeaf(dialog, QStringLiteral("Devices"))->isEnabled());

        // The operator's Disconnect: the Core's page is disabled again,
        // with the reason, and still shows the Core's last values.
        QVERIFY(disconnectFromRadioMenu(h));
        QTRY_VERIFY(!client->isConnectionActive());
        nb = showSetupLeaf(dialog, QStringLiteral("NB/SNB"));
        QTRY_VERIFY(!nb->isEnabled());
        QCOMPARE(nb->objectName() == QStringLiteral("setupStationPlaceholder"), false);
        QVERIFY(notice->isVisible());
        QCOMPARE(notice->text(), kStationReason);
        QVERIFY(showSetupLeaf(dialog, QStringLiteral("Devices"))->isEnabled());
        QCOMPARE(h.acceptedConnections(), 1);
    }

    // R-R3-21 / R-R3-10 (R3 Setup fix wave, final review I2): connected to
    // a Core whose settings never arrive (its profile is empty and was never
    // marked), Setup's Core pages wait as stand-ins, say what is true (not
    // "Connect to the Core"), and nothing is sent.
    void connectedWithoutTheCoresSettingsCorePagesWait()
    {
        RemoteWindowHarness h;
        h.stationSettings().remove(QLatin1String(AppSettings::kDaemonProfileSeededKey));
        QVERIFY(h.start());
        QVERIFY(connectFromRadioMenu(h));
        // R-R3-49: past the Core's 500 ms coalesced save, which the window's
        // admission schedules. It failed this case whenever it landed before
        // the look (on a busy computer); the harness now makes it at the
        // admission, into the Core's own store, and this wait keeps a
        // regression of that from hiding behind a quick run.
        QTest::qWait(kSettleMs + 600);
        QVERIFY(h.proxy().ready());
        QVERIFY(h.proxy().hasReceivedSnapshot());
        QVERIFY(!h.proxy().setupDialogAllowed());
        QSignalSpy writes(&h.proxy(), &SettingsProxy::outboundWriteRequested);
        QSignalSpy removes(&h.proxy(), &SettingsProxy::outboundRemoveRequested);

        SetupDialog* const dialog = openSettings(h);
        QVERIFY(dialog);
        auto* const notice = dialog->findChild<QLabel*>(QStringLiteral("setupStationUnavailable"));
        QVERIFY(notice);
        QWidget* const nb = showSetupLeaf(dialog, QStringLiteral("NB/SNB"));
        QVERIFY(nb);
        QCOMPARE(nb->objectName(), QStringLiteral("setupStationPlaceholder"));
        QVERIFY(!nb->isEnabled());
        QVERIFY(notice->isVisible());
        const QString reason = QStringLiteral("The Core has not sent its settings.");
        QCOMPARE(notice->text(), reason);
        QVERIFY(OperatorWording::isPlain(reason));
        QVERIFY(showSetupLeaf(dialog, QStringLiteral("Devices"))->isEnabled());
        QCOMPARE(writes.size(), 0);
        QCOMPARE(removes.size(), 0);

        // Disconnected, the reason asks to connect again.
        QVERIFY(disconnectFromRadioMenu(h));
        QTRY_VERIFY(!h.client()->isConnectionActive());
        showSetupLeaf(dialog, QStringLiteral("NB/SNB"));
        QTRY_COMPARE(notice->text(), kStationReason);
    }

    // R-R3-17 / R-R3-21 (R3 Setup fix wave, final review M1): connected
    // once, then disconnected, the operator opens Setup and visits every
    // Core page (built from the Core's last values, disabled). None of
    // them records an edit, so the reconnect warns about nothing.
    void setupOpenedWhileDisconnectedRecordsNoEdit()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        StationClient* const client = h.client();
        QVERIFY(connectFromRadioMenu(h));
        QVERIFY(disconnectFromRadioMenu(h));
        QTRY_VERIFY(!client->isConnectionActive());
        QTest::qWait(kSettleMs);
        const QSet<QString> heldBefore = h.proxy().droppedWhileOffline();
        QSignalSpy writes(&h.proxy(), &SettingsProxy::outboundWriteRequested);
        QSignalSpy removes(&h.proxy(), &SettingsProxy::outboundRemoveRequested);

        SetupDialog* const dialog = openSettings(h);
        QVERIFY(dialog);
        auto* const tree = dialog->findChild<QTreeWidget*>();
        auto* const stack = dialog->findChild<QStackedWidget*>();
        QVERIFY(tree && stack);
        int corePages = 0;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const int index = (*it)->data(0, Qt::UserRole).toInt();
            if (index < 0 || dialog->pageScopeAtForTest(index) != SetupScope::Core) {
                continue;
            }
            tree->setCurrentItem(*it);
            QWidget* const page = stack->currentWidget();
            QVERIFY(page);
            // Built from the Core's last values, not a stand-in, and disabled.
            QVERIFY2(page->objectName() != QStringLiteral("setupStationPlaceholder"),
                     qPrintable((*it)->text(0)));
            QVERIFY2(!page->isEnabled(), qPrintable((*it)->text(0)));
            ++corePages;
        }
        // R-R3-49: three Core leaves are not registered while their
        // features are not built (TX Profiles, Signal Generator, Hardware
        // Tests).
        QVERIFY(corePages >= 22);
        QCOMPARE(writes.size(), 0);
        QCOMPARE(removes.size(), 0);
        const QSet<QString> held = h.proxy().droppedWhileOffline() - heldBefore;
        QVERIFY2(held.isEmpty(),
                 qPrintable(QStringList(held.cbegin(), held.cend()).join(QStringLiteral(", "))));
        QCOMPARE(h.proxy().droppedWhileOffline(), heldBefore);

        QSignalSpy superseded(&h.proxy(), &SettingsProxy::offlineEditsSuperseded);
        // R-R3-46 (carried): the gap between Connect and the snapshot. An
        // edit dropped after the check above (while the link came back)
        // would show as a key the Core's snapshot contradicts.
        QVERIFY(connectFromRadioMenu(h));
        QTest::qWait(kSettleMs);
        QCOMPARE(superseded.size(), 0);
        const QSet<QString> contradicted = h.proxy().keysContradictedByLastSnapshot();
        QVERIFY2(contradicted.isEmpty(),
                 qPrintable(QStringList(contradicted.cbegin(), contradicted.cend())
                                .join(QStringLiteral(", "))));
        QCOMPARE(h.acceptedConnections(), 2);
    }

    // R-R3-17 / R-R3-21: a fresh remote window's first connect tells the
    // operator nothing about edits that did not stick, because it made
    // none. Building the window's own models (the band plan in particular)
    // must not count as a change made while the link was down.
    void freshWindowFirstConnectRaisesNoOfflineEditWarning()
    {
        RemoteWindowHarness h;
        // A Core that has run before holds its band plan choice, and TCI
        // settings stored while they were still the Core's (before R-R3-42
        // made them each computer's own; the window ignores them).
        AppSettings& core = h.stationSettings();
        core.setValue(QStringLiteral("BandPlanName"), QStringLiteral("ARRL (US)"));
        core.setValue(QStringLiteral("TciEmulateExpertSDR3Protocol"), QStringLiteral("True"));
        core.setValue(QStringLiteral("TciEmulateSunSDR2Pro"), QStringLiteral("True"));
        core.setValue(QStringLiteral("TciSliceAGain"), QStringLiteral("-6"));
        core.setValue(QStringLiteral("TciTxGain"), QStringLiteral("-3"));
        QVERIFY(h.start());
        QTest::qWait(kSettleMs);
        QVERIFY2(h.proxy().droppedWhileOffline().isEmpty(),
                 qPrintable(QStringList(h.proxy().droppedWhileOffline().cbegin(),
                                        h.proxy().droppedWhileOffline().cend())
                                .join(QStringLiteral(", "))));
        QSignalSpy superseded(&h.proxy(), &SettingsProxy::offlineEditsSuperseded);

        QVERIFY(connectFromRadioMenu(h));
        QTest::qWait(kSettleMs);
        QCOMPARE(superseded.size(), 0);
        QCOMPARE(h.acceptedConnections(), 1);
        // R-R3-42: the window's TCI settings are its own; the Core's stored
        // values are ignored, not copied here.
        QVERIFY(!AppSettings::instance().contains(QStringLiteral("TciSliceAGain")));
        QVERIFY(!AppSettings::instance().contains(QStringLiteral("TciTxGain")));
        QVERIFY(!h.proxy().handlesKey(QStringLiteral("TciSliceAGain")));
    }

    // R-R3-21: the meter update interval (MultimeterDelayMs) is the Core's
    // setting. The window's meter poller read it at startup, before the
    // Core's settings arrived; it takes the Core's value once they do.
    void meterIntervalFollowsTheCoresSetting()
    {
        RemoteWindowHarness h;
        h.stationSettings().setValue(QStringLiteral("MultimeterDelayMs"), 250);
        QVERIFY(h.start());
        MeterPoller* poller = h.window()->radioModel()->meterPoller();
        QVERIFY(poller != nullptr);
        QCOMPARE(poller->intervalMs(), 100);  // no Core settings yet
        QVERIFY(connectFromRadioMenu(h));
        QTRY_COMPARE(poller->intervalMs(), 250);
    }

    // D79 (R-IOS-11, R-R3-49): the band plan is the Core's. The window
    // read its plan at startup, before the Core's settings arrived; it
    // takes the Core's plan once they do and follows every later change,
    // and so do its strip and its View > Band Plan check.
    void windowFollowsTheCoresBandPlan()
    {
        const QString key = QStringLiteral("BandPlanName");
        RemoteWindowHarness h;
        h.stationSettings().setValue(key, QStringLiteral("IARU Region 2"));
        QVERIFY(h.start());
        RadioModel* model = h.window()->radioModel();
        QVERIFY(model != nullptr);
        const BandPlanManager& plans = model->bandPlanManager();
        QCOMPARE(plans.activePlanName(), QStringLiteral("ARRL (US)"));  // no Core settings yet

        // The strip on every pan draws this manager's plan; the menu's
        // check is the one plan named.
        auto stripDraws = [&](const QString& name) {
            const QList<SpectrumWidget*> strips = h.window()->findChildren<SpectrumWidget*>();
            if (strips.isEmpty() || plans.activePlanName() != name) {
                return false;
            }
            for (SpectrumWidget* strip : strips) {
                if (strip->bandPlanManager() != &plans) {
                    return false;
                }
            }
            for (const BandPlanManager::PlanData& plan : plans.plans()) {
                if (plan.name == name) {
                    return plans.segments().size() == plan.segments.size()
                        && plans.spots().size() == plan.spots.size();
                }
            }
            return false;
        };
        auto checkedPlans = [&]() {
            QStringList checked;
            for (const QString& name : plans.availablePlans()) {
                QAction* action = h.menuAction(QStringLiteral("&Band Plan"), name);
                if (action == nullptr) {
                    checked << QStringLiteral("<no action for %1>").arg(name);
                } else if (action->isChecked()) {
                    checked << name;
                }
            }
            return checked;
        };
        QCOMPARE(checkedPlans(), QStringList{QStringLiteral("ARRL (US)")});

        QVERIFY(connectFromRadioMenu(h));
        QTRY_VERIFY(stripDraws(QStringLiteral("IARU Region 2")));
        QCOMPARE(checkedPlans(), QStringList{QStringLiteral("IARU Region 2")});

        // A later change on the Core (another device's pick) reaches it.
        h.stationSettings().setValue(key, QStringLiteral("RAC (Canada)"));
        QTRY_VERIFY(stripDraws(QStringLiteral("RAC (Canada)")));
        QCOMPARE(checkedPlans(), QStringList{QStringLiteral("RAC (Canada)")});

        // A removal on the Core: ARRL (US), the Core's default.
        h.stationSettings().remove(key);
        QTRY_VERIFY(stripDraws(QStringLiteral("ARRL (US)")));
        QCOMPARE(checkedPlans(), QStringList{QStringLiteral("ARRL (US)")});
        // Following wrote nothing back to the Core.
        QTest::qWait(kSettleMs);
        QVERIFY(!h.stationSettings().contains(key));
    }

    // R-R3-46 / R-R3-21: a Core whose controller stands behind its
    // `stepAtt` object (radioHardwareVersion 1). The window's RX applet row
    // and Setup's Step Attenuator and Auto Attenuate groups are enabled and
    // show the Core's settled values; a change made in either round-trips
    // through the Core's controller; a change on the Core reaches both; the
    // overload alarm lights on the Core's overload report.
    void attenuatorControlsUseTheCoresObject()
    {
        StepAttenuatorController coreAtt;
        coreAtt.setTickTimerEnabled(false);
        coreAtt.setStepAttEnabled(true);
        coreAtt.setAttenuation(12);
        RemoteWindowHarness h;
        h.station().setStepAttController(&coreAtt);
        QVERIFY(h.start());
        QVERIFY(connectFromRadioMenu(h));
        QVERIFY(h.client()->remoteRadioHardwareAvailable());

        auto* rx = h.window()->findChild<RxApplet*>();
        QVERIFY(rx);
        auto* att = rx->findChild<QWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(att);
        auto* spin = att->findChild<QSpinBox*>();
        QVERIFY(spin);
        QTRY_VERIFY(att->isEnabled());
        QVERIFY(att->toolTip().isEmpty());
        QTRY_COMPARE(spin->value(), 12);
        QCOMPARE(spin->maximum(), coreAtt.maxAttenuation());
        QCOMPARE(rx->attLabelTextForTest(), QStringLiteral("S-ATT"));

        // The window's change reaches the Core's controller.
        spin->setValue(18);
        QTRY_COMPARE(coreAtt.attenuatorDb(), 18);

        // Setup > General > Options shows the Core's values too.
        SetupDialog* dialog = openSettings(h);
        QVERIFY(dialog);
        dialog->selectPage(QStringLiteral("Options"));
        GeneralOptionsPage* page = nullptr;
        QTRY_VERIFY((page = dialog->findChild<GeneralOptionsPage*>()) != nullptr);
        auto* stepGroup = page->findChild<QGroupBox*>(QStringLiteral("grpStepAttenuator"));
        auto* autoGroup = page->findChild<QGroupBox*>(QStringLiteral("grpAutoAttRx1"));
        QVERIFY(stepGroup && autoGroup);
        QVERIFY(stepGroup->isEnabled());
        QVERIFY(autoGroup->isEnabled());
        auto* pageSpin = stepGroup->findChild<QSpinBox*>();
        QVERIFY(pageSpin);
        QCOMPARE(pageSpin->value(), 18);

        // A change on the Core reaches both.
        coreAtt.setAttenuation(7);
        QTRY_COMPARE(spin->value(), 7);
        QTRY_COMPARE(pageSpin->value(), 7);

        // Auto-attenuate, from Setup, round-trips through the Core.
        QCheckBox* autoEnable = nullptr;
        for (QCheckBox* box : autoGroup->findChildren<QCheckBox*>()) {
            if (box->text() == QStringLiteral("Enable")) { autoEnable = box; }
        }
        QVERIFY(autoEnable);
        QVERIFY(!coreAtt.autoAttEnabled());
        autoEnable->click();
        QTRY_VERIFY(coreAtt.autoAttEnabled());
        QTRY_COMPARE(rx->attLabelTextForTest(), QStringLiteral("A-ATT"));

        // The preamp: turning the step attenuator off on the Core shows the
        // combo, and the window's choice reaches the Core.
        coreAtt.setStepAttEnabled(false);
        QTRY_COMPARE(rx->attLabelTextForTest(), QStringLiteral("ATT"));
        auto* combo = att->findChild<QComboBox*>();
        QVERIFY(combo);
        if (combo->count() > 1) {
            const int other = combo->currentIndex() == 0 ? 1 : 0;
            combo->setCurrentIndex(other);
            QTRY_COMPARE(static_cast<int>(coreAtt.preampMode()), combo->itemData(other).toInt());
        }

        // The overload alarm lights on the Core's report.
        auto* badge = h.window()->findChild<QWidget*>(QStringLiteral("adcOvlBadge"));
        QVERIFY(badge);
        const auto opacity = [badge] {
            auto* fx = qobject_cast<QGraphicsOpacityEffect*>(badge->graphicsEffect());
            return fx ? fx->opacity() : 1.0;
        };
        QVERIFY(opacity() < 0.5);
        coreAtt.onAdcOverflow(0);
        coreAtt.tick();
        QTRY_COMPARE(opacity(), 1.0);
        QVERIFY(badge->toolTip().contains(QStringLiteral("ADC0: overload")));

        h.station().setStepAttController(nullptr);
    }

    // R-R3-46: Hardware Config in a window connected to a Core that offers
    // it (radioHardwareVersion 2). The tabs are live and show the Core's
    // radio; an RX antenna change reaches the Core's own AlexController;
    // an OC receive pin reaches the Core's settings for that radio and the
    // Core reloads its matrix; the transmit fields that key nothing follow
    // their own versions (parity tasks).
    void hardwareConfigReceiveSettingsReachTheCore()
    {
        StepAttenuatorController coreAtt;
        coreAtt.setTickTimerEnabled(false);
        RemoteWindowHarness h;
        h.station().setStepAttController(&coreAtt);
        const auto unbind = qScopeGuard([&h] { h.station().setStepAttController(nullptr); });
        const QString mac = QStringLiteral("AA:BB:CC:DD:EE:53");
        RadioInfo radio;
        radio.macAddress = mac;
        radio.boardType = HPSDRHW::Saturn;
        h.station().setLastRadioInfoForTest(radio);
        QStringList reloads;
        h.station().setHardwareApplyObserverForTest(
            [&reloads](const QString& name) { reloads << name; });
        QVERIFY(h.start());
        QVERIFY(connectFromRadioMenu(h));
        QVERIFY(h.client()->remoteHardwareConfigAvailable());
        QTRY_COMPARE(h.remoteModel()->currentRadioInfo().macAddress, mac);

        SetupDialog* dialog = openSettings(h);
        QVERIFY(dialog);
        QWidget* const page = showSetupLeaf(dialog, QStringLiteral("Hardware Config"));
        auto* hardware = qobject_cast<HardwarePage*>(page);
        QVERIFY(hardware);
        QVERIFY(hardware->isEnabled());
        QTRY_VERIFY(hardware->remoteEditsAvailableForTest());
        QVERIFY(hardware->findChild<QTabWidget*>()->isEnabled());

        // RX antenna: the Core's controller changes.
        auto* antennas = hardware->findChild<AntennaAlexAntennaControlTab*>();
        QVERIFY(antennas);
        // Parity Task 12: the TX antennas go to the Core too (version 6);
        // tst_remote_tx_antennas covers them.
        QTRY_VERIFY(antennas->txGridForTest()->isEnabled());
        QRadioButton* const ant2 = antennas->rxButtonForTest(Band::Band40m, 2);
        QVERIFY(ant2 && ant2->isEnabled());
        ant2->click();
        QTRY_COMPARE(h.station().alexController().rxAnt(Band::Band40m), 2);
        QTRY_VERIFY(ant2->isChecked());

        // OC receive pin: the Core's settings for its radio, and a reload.
        auto* hf = hardware->findChild<OcOutputsHfTab*>();
        QVERIFY(hf);
        QCheckBox* pin = nullptr;
        for (QCheckBox* box : hf->findChildren<QCheckBox*>()) {
            if (box->toolTip() == QStringLiteral("RX OC pin 4, band 20m")) { pin = box; }
        }
        QVERIFY(pin && pin->isEnabled());
        pin->click();
        const QString key = QStringLiteral("hardware/%1/oc/rx/20m/pin4").arg(mac);
        QTRY_COMPARE(h.stationSettings().value(key).toString(), QStringLiteral("True"));
        QTRY_VERIFY(reloads.contains(QStringLiteral("oc")));

        // R-R3-46 fix wave (radioHardwareVersion 3): the HL2 I/O board tab
        // shows the Core's board, whose readings arrive on the Core after a
        // probe. (4 since the filter policy verb, R-R3-46 / R-R3-21.)
        QCOMPARE(h.client()->capabilities().radioHardwareVersion, 7);
        auto* ioTab = hardware->findChild<Hl2IoBoardTab*>();
        QVERIFY(ioTab);
        const auto statusText = [ioTab]() {
            for (QLabel* label : ioTab->findChildren<QLabel*>()) {
                if (label->text().startsWith(QStringLiteral("mi0bot custom I/O board"))) {
                    return label->text();
                }
            }
            return QString();
        };
        QCOMPARE(statusText(), QStringLiteral("mi0bot custom I/O board (0x41): Not detected"));
        IoBoardHl2& coreBoard = h.station().ioBoardMutable();
        coreBoard.setRegisterValue(IoBoardHl2::Register::REG_FIRMWARE_MAJOR, 0x02);
        coreBoard.setHardwareVersion(IoBoardHl2::kHardwareVersion1);
        coreBoard.setDetected(true);
        QTRY_COMPARE(statusText(), QStringLiteral("mi0bot custom I/O board (0x41): Active"));
    }

    // R-R3-46 / R-R3-21: a Core that does not offer its attenuator
    // (radioHardwareVersion 0) leaves the window's rows disabled, with the
    // plain reason through OperatorReasonText, and a click changes nothing.
    void olderCoreLeavesAttenuatorControlsDisabledWithAReason()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        QVERIFY(connectFromRadioMenu(h));
        QVERIFY(!h.client()->remoteRadioHardwareAvailable());
        const QString reason =
            OperatorReasonText::forDisplay(h.client()->radioHardwareUnavailableReason());
        QVERIFY(!reason.isEmpty());
        QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));

        auto* rx = h.window()->findChild<RxApplet*>();
        QVERIFY(rx);
        auto* att = rx->findChild<QWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(att);
        QTRY_COMPARE(att->toolTip(), reason);
        QVERIFY(!att->isEnabled());

        SetupDialog* dialog = openSettings(h);
        QVERIFY(dialog);
        dialog->selectPage(QStringLiteral("Options"));
        GeneralOptionsPage* page = nullptr;
        QTRY_VERIFY((page = dialog->findChild<GeneralOptionsPage*>()) != nullptr);
        for (const char* name : {"grpStepAttenuator", "grpAutoAttRx1"}) {
            auto* group = page->findChild<QGroupBox*>(QLatin1String(name));
            QVERIFY2(group, name);
            QVERIFY2(!group->isEnabled(), name);
            QCOMPARE(group->toolTip(), reason);
        }
    }

    // R-R3-46: the window follows the Core's radio. A Saturn ANAN-G2 1K
    // brings its preamp items and attenuator range to the RX applet, its
    // antenna labels to the VFO flag and its tabs to Hardware Config;
    // Protocol Info shows P2 and the radio's address; a Core whose radio is
    // offline gives Unknown, not Hermes; an older Core still shows what it
    // has.
    void windowFollowsTheCoresRadio()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        QVERIFY(connectFromRadioMenu(h));
        RadioModel* const windowModel = h.remoteModel();
        QVERIFY(windowModel);
        auto* rx = h.window()->findChild<RxApplet*>();
        QVERIFY(rx);

        // First a radio whose items and range differ from the G2 1K's.
        h.pushCapabilities(coreRadio(h, HPSDRHW::Angelia, HPSDRModel::ANAN100D,
                                     QStringLiteral("AA:BB:CC:DD:EE:10")));
        QTRY_COMPARE(rx->preampComboLabelsForTest(), preampLabelsFor(HPSDRHW::Angelia));
        const int angeliaMax = BoardCapsTable::stepAttMaxDb(
            HPSDRHW::Angelia, BoardCapsTable::forBoard(HPSDRHW::Angelia).hasAlexFilters);
        QCOMPARE(rx->stepAttMaxForTest(), angeliaMax);
        QVERIFY(preampLabelsFor(HPSDRHW::Angelia) != preampLabelsFor(HPSDRHW::Saturn));

        h.pushCapabilities(coreRadio(h, HPSDRHW::Saturn, HPSDRModel::ANAN_G2_1K,
                                     QStringLiteral("AA:BB:CC:DD:EE:46")));
        QTRY_COMPARE(rx->preampComboLabelsForTest(), preampLabelsFor(HPSDRHW::Saturn));
        const int saturnMax = BoardCapsTable::stepAttMaxDb(
            HPSDRHW::Saturn, BoardCapsTable::forBoard(HPSDRHW::Saturn).hasAlexFilters);
        QVERIFY(saturnMax != angeliaMax);
        QCOMPARE(rx->stepAttMaxForTest(), saturnMax);
        QCOMPARE(rx->stepAttMinForTest(),
                 BoardCapsTable::forBoard(HPSDRHW::Saturn).attenuator.minDb);
        QCOMPARE(windowModel->hardwareProfile().model, HPSDRModel::ANAN_G2_1K);

        const auto expectedLabels = [](HPSDRModel sku) {
            const auto labels = skuUiProfileFor(sku).rxOnlyLabels;
            return QStringList(labels.cbegin(), labels.cend());
        };
        const QList<VfoWidget*> flags = h.window()->findChildren<VfoWidget*>();
        QVERIFY(!flags.isEmpty());
        for (VfoWidget* flag : flags) {
            QCOMPARE(flag->rxOnlyAntennaLabelsForTest(), expectedLabels(HPSDRModel::ANAN_G2_1K));
        }
        QVERIFY(expectedLabels(HPSDRModel::ANAN_G2_1K) != expectedLabels(HPSDRModel::HERMES));

        // Hardware Config shows the Saturn's tabs, then follows a change.
        SetupDialog* dialog = openSettings(h);
        QVERIFY(dialog);
        QVERIFY(showSetupLeaf(dialog, QStringLiteral("Hardware Config")));
        auto* hardware = dialog->findChild<HardwarePage*>();
        QVERIFY(hardware);
        const BoardCapabilities& saturn = BoardCapsTable::forBoard(HPSDRHW::Saturn);
        QCOMPARE(hardware->isTabVisibleForTest(HardwarePage::Tab::AntennaAlex),
                 saturn.hasAlexFilters);
        // R-R3-49: the Diversity tab was removed, remote as well as local.
        for (const QTabWidget* tabs : hardware->findChildren<QTabWidget*>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                QVERIFY(tabs->tabText(i) != QStringLiteral("Diversity"));
            }
        }
        QVERIFY(!hardware->isTabVisibleForTest(HardwarePage::Tab::Hl2Options));
        QCOMPARE(hardware->tabTextForTest(HardwarePage::Tab::OcOutputs),
                 QStringLiteral("OC Outputs"));
        StationCapabilities hl2 = coreRadio(h, HPSDRHW::HermesLite, HPSDRModel::HERMESLITE,
                                            QStringLiteral("AA:BB:CC:DD:EE:02"));
        hl2.radioProtocol = 1;
        h.pushCapabilities(hl2);
        QTRY_VERIFY(hardware->isTabVisibleForTest(HardwarePage::Tab::Hl2Options));
        QCOMPARE(hardware->tabTextForTest(HardwarePage::Tab::OcOutputs),
                 QStringLiteral("Hermes Lite Control"));
        dialog->close();

        // Protocol Info shows the Core's radio.
        h.pushCapabilities(coreRadio(h, HPSDRHW::Saturn, HPSDRModel::ANAN_G2_1K,
                                     QStringLiteral("AA:BB:CC:DD:EE:46")));
        QTRY_COMPARE(windowModel->currentRadioInfo().macAddress,
                     QStringLiteral("AA:BB:CC:DD:EE:46"));
        QAction* info = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Protocol Info"));
        QVERIFY(info);
        QVERIFY(info->isEnabled());
        QVERIFY(OperatorWording::isPlain(info->toolTip()));
        const QString shown = protocolInfoText(h);
        QVERIFY2(shown.contains(QStringLiteral("Protocol: P2")), qPrintable(shown));
        QVERIFY2(shown.contains(QStringLiteral("192.168.1.50")), qPrintable(shown));
        QVERIFY2(shown.contains(QStringLiteral("AA:BB:CC:DD:EE:46")), qPrintable(shown));
        QVERIFY2(shown.contains(QStringLiteral("Firmware: 27")), qPrintable(shown));

        // A Core whose radio is offline: Unknown, never Hermes.
        StationCapabilities offline = h.server().buildCapabilities();
        offline.board = HPSDRHW::Unknown;
        offline.macAddress.clear();
        offline.radioConnected = false;
        offline.radioIdentityEntries = true;
        offline.hpsdrModel = HPSDRModel::FIRST;
        offline.radioProtocol = 0;
        offline.radioAddress.clear();
        h.pushCapabilities(offline);
        QTRY_COMPARE(h.client()->capabilities().board, HPSDRHW::Unknown);
        QCOMPARE(windowModel->hardwareProfile().model, HPSDRModel::FIRST);
        QCOMPARE(windowModel->boardCapabilities().board, HPSDRHW::Unknown);

        // An older Core (none of the three entries): Protocol Info shows
        // what it has and says what it does not.
        StationCapabilities older = coreRadio(h, HPSDRHW::Saturn, HPSDRModel::FIRST,
                                              QStringLiteral("AA:BB:CC:DD:EE:46"));
        older.radioIdentityEntries = false;
        older.radioProtocol = 0;
        older.radioAddress.clear();
        h.pushCapabilities(older);
        QTRY_COMPARE(windowModel->hardwareProfile().model, HPSDRModel::ANAN_G2);
        QVERIFY(info->isEnabled());
        const QString partial = protocolInfoText(h);
        QVERIFY2(partial.contains(QStringLiteral("AA:BB:CC:DD:EE:46")), qPrintable(partial));
        QVERIFY2(partial.contains(QStringLiteral("not reported by the Core")),
                 qPrintable(partial));
        QVERIFY(OperatorWording::isPlain(QStringLiteral("not reported by the Core")));
        QVERIFY(h.client()->isHandshakeComplete());
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

        // R-R3-49 (parity Task 4): Tools > TX Equalizer opens in a remote
        // window whatever the Core permits (the dialog shows why it is
        // greyed), so the remote transmit push is watched on the TX
        // applet's MOX button, which keeps it. (Transmit group fix wave 2:
        // VOX also waits for this computer's microphone line, which this
        // harness never opens, so it stays disabled with that reason.)
        QPushButton* txEq = nullptr;
        for (QPushButton* b : h.window()->findChildren<QPushButton*>()) {
            if (b->accessibleName() == QStringLiteral("MOX transmit")) {
                txEq = b;
            }
        }
        QVERIFY(txEq);
        auto* vox = h.window()->findChild<QPushButton*>(QStringLiteral("TxVoxButton"));
        QVERIFY(vox);
        auto* txEqualizer = h.window()->findChild<QAction*>(QStringLiteral("toolsTxEqualizer"));
        QVERIFY(txEqualizer && txEqualizer->isEnabled());
        QVERIFY(!client->capabilities().txPermitted);
        QVERIFY(!txEq->isEnabled());
        const QString reason = txEq->toolTip();
        QVERIFY(!reason.isEmpty());
        // R-R3-49 (parity Task 3): the RADE applet's profile combo and Reset
        // vocoder follow the transmit settings gate, not the remote transmit
        // push: the Core takes them while its radio is off the air.
        auto* rade = h.window()->findChild<RadeApplet*>();
        QVERIFY(rade);
        QComboBox* const radeProfile = rade->profileComboForTest();
        QPushButton* const radeReset = rade->resetVocoderButtonForTest();
        QTRY_VERIFY(radeProfile->isEnabled());
        QVERIFY(radeReset->isEnabled());

        StationCapabilities granted = h.server().buildCapabilities();
        granted.txPermitted = true;
        h.pushCapabilities(granted);
        QTRY_VERIFY(txEq->isEnabled());
        QVERIFY(client->capabilities().txPermitted);
        QVERIFY(txEq->toolTip() != reason);
        QVERIFY(!vox->isEnabled());
        QCOMPARE(vox->toolTip(), TxRefusals::micNotConnected().text);
        QVERIFY(radeProfile->isEnabled());
        QVERIFY(radeReset->isEnabled());

        StationCapabilities withdrawn = h.server().buildCapabilities();
        withdrawn.txPermitted = false;
        h.pushCapabilities(withdrawn);
        QTRY_VERIFY(!txEq->isEnabled());
        QCOMPARE(txEq->toolTip(), reason);
        QVERIFY(radeProfile->isEnabled());
        QVERIFY(radeReset->isEnabled());

        // A Core without the TX profile commands (transmitSettingsVersion
        // below 3) greys both with the plain reason, on the live session.
        StationCapabilities older = h.server().buildCapabilities();
        older.transmitSettingsVersion = 2;
        h.pushCapabilities(older);
        QTRY_VERIFY(!radeProfile->isEnabled());
        QCOMPARE(radeProfile->toolTip(), IStationLink::transmitSettingsUnavailableReason());
        QVERIFY(!radeReset->isEnabled());
        QCOMPARE(radeReset->toolTip(), IStationLink::transmitSettingsUnavailableReason());

        QCOMPARE(client->sessionEpoch(), epoch);
        QCOMPARE(h.acceptedConnections(), 1);
        QVERIFY(client->isHandshakeComplete());
    }
};

QTEST_MAIN(TestRemoteWindowHarness)
#include "tst_remote_window_harness.moc"
