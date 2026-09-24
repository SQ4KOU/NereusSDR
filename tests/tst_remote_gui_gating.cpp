// no-port-check: NereusSDR-original unit-test file. No Thetis logic is
// ported here; this exercises NereusSDR's own remote-station GUI gate
// (remote-daemon R2 Task 20).
// =================================================================
// tests/tst_remote_gui_gating.cpp  (NereusSDR)
// =================================================================
//
// Remote-daemon R2 Task 20 -- the remote-mode GUI gate.
//
// A Role::Remote RadioModel drives a radio that is in another process.
// Everything in src/gui that reaches for this process's RadioConnection,
// WdspEngine, AudioEngine or ReceiverManager is therefore reaching for
// something that either does not exist or exists and does nothing. There
// are roughly 77 such call sites across 16 files (the enumeration is in
// .superpowers/sdd/2026-08-03-remote-daemon-r2-plan/task-20-report.md),
// which is far too many to gate one at a time and keep gated.
//
// Two of the four accessors are why this file exists at all. connection()
// is nullptr on a remote model, so an unguarded caller crashes and any
// test that merely runs the path finds it. audioEngine(),
// receiverManager() and wdspEngine() are constructed unconditionally
// (RadioModel's constructor initializer list), so on a remote model they
// hand back a REAL BUT INERT object. The caller's writes land nowhere,
// the connects it makes never fire, and nothing is observably wrong until
// an operator notices a control that does not work. A null-dereference
// assertion cannot catch that shape. RadioModel's hand-out audit
// (localDspHandOutCount / localDspHandOutNames), armed for Role::Remote
// only, is what catches it, and this file is what asserts on the audit.
//
// MainWindow is NOT constructed here and cannot be: it boots WDSP, the
// audio engine and the discovery thread. Three existing test banners say
// so (tst_notch_hit_test.cpp, tst_mainwindow_status_bar_safety.cpp,
// tst_pan_active_slice_sync.cpp). Its gating entry points are pinned by
// name off MainWindow::staticMetaObject instead -- the same seam
// tst_notch_hit_test.cpp uses for the notch fan-out slots -- and the
// behaviour behind them is exercised through SetupDialog, which CAN be
// stood up against a Role::Remote model.
//
// That stopped being true with R-R3-38. GuiSessionCoordinator builds each
// MainWindow with ConnectionStartup::Deferred, so nothing is dialled or
// scanned until asked, and the Tools menu test entries case at the bottom
// of this file drives real windows through it.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08 -- New test file for remote-daemon R2 Task 20. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
//   2026-08-08 -- Fix round 2: the Setup gate's production entry point
//                 (Important 2) and the local-sweep sabotage detector
//                 (Minor 1). J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-08-09 -- Fix round 4: the isConnected()-no-longer-implies-a-
//                 connection precondition (Critical), the TUNE refusal
//                 and its mirrored-state leak (Important), and the MOX
//                 button's missing follow of a refusal. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-22 -- The Tools menu's two developer test entries are
//                 disabled in a remote session (R-R3-21, R-R3-25),
//                 checked through real windows. J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-09-22 -- R-R3-36 Task 7: the local keying case selects the radio
//                 mic, since PC-mic keying now waits for a ready
//                 microphone. J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-09-23 -- R-R3-23 / R-R3-36: the Setup sweep reads each page's
//                 scope and is proved against a deliberately wrong
//                 ThisComputer page; Audio > Devices and TX Input work in a
//                 remote window (TX Input gates only the controls held for
//                 the radio); a remote window runs no VAX first-run check;
//                 and (R-R3-16) a local window still opens Connections when
//                 its radio drops. J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-09-23 -- R-R3-21 control inventory: DSP > CFC and Test >
//                 Two-Tone IMD follow the transmit permission, a page the
//                 local-DSP gate disables now says why, and the controls
//                 the inventory found acting on this computer's own radio
//                 connection, amplifier socket or VAX buses are disabled
//                 with a plain reason in a remote session. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "OperatorWording.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QHostAddress>
#include <QLabel>
#include <QGroupBox>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSlider>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWebSocketServer>
#include <QWidget>

#include <chrono>
#include <memory>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/MicProfileManager.h"
#include "core/MoxController.h"
#include "core/RadioDiscovery.h"
#include "core/WdspTypes.h"
#include "core/session/RemoteStationOptions.h"
#include "core/session/SessionTransport.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/ISettingsBackend.h"
#include "core/settings/SettingsProxy.h"
#include "core/settings/SettingsScope.h"
#include "gui/ConnectionPanel.h"
#include "gui/GuiSessionCoordinator.h"
#include "gui/MainWindow.h"
#include "gui/SetupDialog.h"
#include "gui/StationStartupSelection.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/applets/AmpApplet.h"
#include "gui/applets/PhoneCwApplet.h"
#include "gui/applets/RadeApplet.h"
#include "gui/applets/Rf2ksApplet.h"
#include "gui/applets/RxApplet.h"
#include "gui/applets/TxApplet.h"
#include "gui/applets/VaxApplet.h"
#include "gui/HGauge.h"
#include "gui/VaxFirstRunDialog.h"
#include "gui/setup/AudioDevicesPage.h"
#include "gui/setup/AudioTxInputPage.h"
#include "gui/setup/DeviceCard.h"
#include "gui/setup/DspOptionsPage.h"
#include "gui/setup/DspSetupPages.h"
#include "gui/setup/GeneralOptionsPage.h"
#include "gui/widgets/VaxChannelSelector.h"
#include "gui/widgets/VfoWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

// The AppSettings keys the Setup field group (Setup -> CAT & Network ->
// Remote Station) binds. Spelled here rather than #included from the page
// on purpose: this test is the thing that pins them as OperatorLocal, and
// a rename that silently moved them would otherwise rename the assertion
// with them and prove nothing.
const QString kStationUrlKey         = QStringLiteral("RemoteStationUrl");
const QString kStationTokenKey       = QStringLiteral("RemoteStationToken");
const QString kStationFingerprintKey = QStringLiteral("RemoteStationFingerprint");
const QString kStationAllowUnpinnedKey =
    QStringLiteral("RemoteStationAllowUnpinned");

// An ISettingsBackend that is NOT a SettingsProxy. Exists to prove the
// Setup gate's cross-cast has no opinion about backends it does not
// recognise, rather than refusing whenever any backend is installed.
class StubBackend : public ISettingsBackend {
public:
    bool handlesKey(const QString&) const override { return false; }
    QVariant value(const QString&, const QVariant& def) const override { return def; }
    void setValue(const QString&, const QVariant&) override {}
    bool contains(const QString&) const override { return false; }
    void remove(const QString&) override {}
    QStringList handledKeys() const override { return {}; }
};

// R-R3-21 / R-R3-25: the Tools menu's two developer test entries, and the
// TX Equalizer entry whose remote tooltip they are required to share.
const QString kTestToastActionName   = QStringLiteral("toolsTestAntennaSwitchToast");
const QString kTestReRouteActionName = QStringLiteral("toolsTestTxBoundReRoute");
const QString kTxEqualizerActionName = QStringLiteral("toolsTxEqualizer");

// MainWindow answers antennaAutoSwitched with an AntennaSwitchToast tool
// window and txBoundReRouteRequested with a modal TxBoundConfirmDialog,
// whose exec() would block the test. Neither is what the test entries
// case asserts: it asks whether an entry reaches RadioModel at all, and
// watches RadioModel's own signals for that. So the window's two
// consumers are detached first. False means one of them was not there to
// detach, i.e. the wiring moved and the case needs another look.
bool detachTestSurfaceConsumers(MainWindow* window)
{
    RadioModel* const model = window->radioModel();
    const bool toast = QObject::disconnect(
        model, &RadioModel::antennaAutoSwitched, window, nullptr);
    const bool reRoute = QObject::disconnect(
        model, &RadioModel::txBoundReRouteRequested, window, nullptr);
    return toast && reRoute;
}

// The Setup tree leaf registered under `label` (first match), for its
// tooltip. Setup's leaves are the second level of the tree.
QTreeWidgetItem* setupLeaf(SetupDialog& dialog, const QString& label)
{
    auto* tree = dialog.findChild<QTreeWidget*>();
    if (tree == nullptr) { return nullptr; }
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->parent() != nullptr && (*it)->text(0) == label) {
            return *it;
        }
    }
    return nullptr;
}

// R-R3-23: realizes every registered Setup leaf against `model` (a
// Role::Remote one) and returns the leaves that break the gate's rule:
//   - a Core or Mixed page that reached this process's DSP and was left
//     enabled (the rule since R2 Task 20);
//   - a ThisComputer page that reached this process's DSP at all, or came
//     up disabled. Such a page is supposed to work in a remote window, so
//     either is a bug in the page, even though the gate disables it.
// Iterates by INDEX: two leaves share the label "Options". `reachedCount`
// counts the pages that reached local DSP while being realized here.
QStringList remoteSetupSweepOffenders(RadioModel& model, SetupDialog& dialog,
                                      int* reachedCount = nullptr)
{
    QStringList offenders;
    int reached = 0;
    const QStringList labels = dialog.pageLabelsForTest();
    const int pageCount = dialog.registeredPageCountForTest();
    for (int i = 0; i < pageCount; ++i) {
        const int before = model.localDspHandOutCount();
        QWidget* page = dialog.realizePageAtForTest(i);
        if (page == nullptr) {
            continue;  // a factory that yields nothing has nothing to gate
        }
        const bool reachedLocalDsp = model.localDspHandOutCount() > before;
        if (reachedLocalDsp) {
            ++reached;
        }
        if (dialog.pageScopeAtForTest(i) == SetupScope::ThisComputer) {
            if (reachedLocalDsp || !page->isEnabled()) {
                offenders << labels.at(i);
            }
        } else if (reachedLocalDsp && page->isEnabled()) {
            offenders << labels.at(i);
        }
    }
    if (reachedCount != nullptr) {
        *reachedCount = reached;
    }
    return offenders;
}

// The Devices page's card titled `title`.
DeviceCard* deviceCardOf(QWidget* page, const QString& title)
{
    for (DeviceCard* card : page->findChildren<DeviceCard*>()) {
        if (card->title() == title) {
            return card;
        }
    }
    return nullptr;
}

// The microphone card's buffer-size combo (its items start at 64 samples;
// no other card combo's do).
QComboBox* deviceCardBufferCombo(DeviceCard* card)
{
    for (QComboBox* combo : card->findChildren<QComboBox*>()) {
        if (combo->count() > 1 && combo->itemData(0).toInt() == 64) {
            return combo;
        }
    }
    return nullptr;
}

// The value of every spin box, combo box and check box under `root`, in
// child order: a before/after fingerprint for "activation moved nothing".
QString widgetStateOf(QWidget* root)
{
    QStringList state;
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (auto* spin = qobject_cast<QSpinBox*>(w)) {
            state << QString::number(spin->value());
        } else if (auto* combo = qobject_cast<QComboBox*>(w)) {
            state << QString::number(combo->currentIndex());
        } else if (auto* button = qobject_cast<QAbstractButton*>(w); button && button->isCheckable()) {
            state << (button->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
        }
    }
    return state.join(QLatin1Char(','));
}

// R-R3-21: the Core settings reason MainWindow pushes to a disconnected
// remote window's Setup dialog.
const QString kStationReason = QStringLiteral("Connect to the Core to change these.");

// Shows the leaf at a registry index (labels are not unique: "Options"
// is both General and DSP) the way the operator does, by selecting it in
// the tree, and returns the page now on screen.
QWidget* showSetupLeafAt(SetupDialog& dialog, int entryIndex)
{
    auto* tree = dialog.findChild<QTreeWidget*>();
    auto* stack = dialog.findChild<QStackedWidget*>();
    if (tree == nullptr || stack == nullptr) { return nullptr; }
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == entryIndex) {
            tree->setCurrentItem(*it);
            return stack->currentWidget();
        }
    }
    return nullptr;
}

QTreeWidgetItem* setupLeafAt(SetupDialog& dialog, int entryIndex)
{
    auto* tree = dialog.findChild<QTreeWidget*>();
    if (tree == nullptr) { return nullptr; }
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == entryIndex) { return *it; }
    }
    return nullptr;
}

// The controls on `page` that carry `reason` as their accessible
// description: the ones a gate disabled with that reason.
QList<QWidget*> controlsGatedWith(QWidget* page, const QString& reason)
{
    QList<QWidget*> gated;
    for (QWidget* w : page->findChildren<QWidget*>()) {
        if (w->accessibleDescription() == reason) { gated << w; }
    }
    return gated;
}

// R3 Setup fix wave: the reason shown while connected to a Core that has
// not sent its settings (MainWindow::stationSettingsReason()).
const QString kCoreSettingsMissingReason = QStringLiteral("The Core has not sent its settings.");

QPushButton* buttonWithText(QWidget* page, const QString& text)
{
    for (QPushButton* button : page->findChildren<QPushButton*>()) {
        if (button->text() == text) { return button; }
    }
    return nullptr;
}

// A connected remote dialog: the Core's settings (seed marker plus
// `settings`) have arrived, the session is ready and MainWindow's push
// has made them available.
void connectDialog(SettingsProxy& proxy, SetupDialog& dialog,
                   const QMap<QString, QString>& settings = {})
{
    QMap<QString, QString> snapshot = settings;
    snapshot.insert(QLatin1String(AppSettings::kDaemonProfileSeededKey), QStringLiteral("True"));
    proxy.applySnapshot(snapshot);
    proxy.setReady(true);
    dialog.setStationSettingsAvailable(true, kStationReason);
    QCoreApplication::processEvents();
}

} // namespace

class TstRemoteGuiGating : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Before the first AppSettings::instance() call, so the singleton
        // resolves to this run's own file. The test entries case builds
        // MainWindows, which save; parallel ctest jobs must not share a
        // settings file (the same arrangement as tst_gui_session_coordinator).
        AppSettings::setProfileOverride(QStringLiteral("remote-gui-gating-%1")
                                            .arg(QCoreApplication::applicationPid()));
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().setRemoteBackend(nullptr);
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // ====================================================================
    // Step 3: one capability authority, and the audit that catches the
    // three accessors a crash cannot.
    // ====================================================================

    void localModelOwnsLocalDsp()
    {
        RadioModel model;
        QVERIFY(model.ownsLocalDsp());
    }

    void remoteModelDoesNotOwnLocalDsp()
    {
        RadioModel model(RadioModel::Role::Remote);
        QVERIFY(!model.ownsLocalDsp());
    }

    // The headline assertion of step 3. Each of the three accessors must
    // hand back a NON-NULL object (that is the whole problem) and must be
    // recorded doing it, by name.
    void remoteModelRecordsEveryLiveLocalDspHandOut()
    {
        RadioModel model(RadioModel::Role::Remote);
        QCOMPARE(model.localDspHandOutCount(), 0);

        QVERIFY2(model.audioEngine() != nullptr,
                 "audioEngine() is expected to be non-null even on a remote "
                 "model; if this ever becomes null the silent-failure premise "
                 "of this whole gate changed and the gate should be revisited");
        QVERIFY(model.receiverManager() != nullptr);
        QVERIFY(model.wdspEngine() != nullptr);

        QCOMPARE(model.localDspHandOutCount(), 3);
        const QSet<QByteArray> names = model.localDspHandOutNames();
        QVERIFY(names.contains(QByteArrayLiteral("audioEngine")));
        QVERIFY(names.contains(QByteArrayLiteral("receiverManager")));
        QVERIFY(names.contains(QByteArrayLiteral("wdspEngine")));
    }

    // connection() is the one that CAN be caught by a crash, and it must
    // not be counted: counting it would make every correctly-null-guarded
    // call site look like a leak and drown the real ones.
    void remoteModelConnectionIsNullAndUncounted()
    {
        RadioModel model(RadioModel::Role::Remote);
        QVERIFY(model.connection() == nullptr);
        QCOMPARE(model.localDspHandOutCount(), 0);
    }

    // The audit must be inert for local direct mode, which is the mode
    // every existing user is in. If it armed there, the SetupDialog gate
    // below would start disabling pages on a local radio.
    void localModelNeverArmsTheAudit()
    {
        RadioModel model;
        for (int i = 0; i < 5; ++i) {
            (void)model.audioEngine();
            (void)model.receiverManager();
            (void)model.wdspEngine();
        }
        QCOMPARE(model.localDspHandOutCount(), 0);
        QVERIFY(model.localDspHandOutNames().isEmpty());
    }

    // The audit's KNOWN BLIND SPOT, asserted rather than only described.
    //
    // rxChannelForSlice() forwards to WdspEngine::rxChannel(), which on a
    // channel-less remote engine returns nullptr. Every call site guards
    // with `if (RxChannel* ch = ...)`, so the null is swallowed and the
    // control silently does nothing -- the same shape as the three counted
    // accessors, reached through a null instead of an inert object.
    //
    // It is deliberately NOT counted: two of its seven src/gui call sites
    // run at page-construction time (DspOptionsPage::buildUI and
    // MnfSetupPage's constructor), so counting it would disable DSP >
    // Options and MNF, two pages that exist mainly to edit Station-scoped
    // settings. Pinning the decision here means a future change that routes
    // the wrapper has to come through this test and say so on purpose.
    // Fix round 1, Important 1.
    void rxChannelForSliceIsSilentOnRemoteAndDeliberatelyUncounted()
    {
        RadioModel model(RadioModel::Role::Remote);

        QVERIFY2(model.rxChannelForSlice(0) == nullptr,
                 "a remote model's WdspEngine has no channels, so this must "
                 "resolve to nullptr rather than a usable channel");
        QCOMPARE(model.localDspHandOutCount(), 0);
        QVERIFY(model.localDspHandOutNames().isEmpty());
    }

    void auditResetClearsCountAndNames()
    {
        RadioModel model(RadioModel::Role::Remote);
        (void)model.audioEngine();
        QCOMPARE(model.localDspHandOutCount(), 1);
        model.resetLocalDspHandOutAudit();
        QCOMPARE(model.localDspHandOutCount(), 0);
        QVERIFY(model.localDspHandOutNames().isEmpty());
    }

    // ====================================================================
    // Step 1a: every registered Setup page, realized against a
    // Role::Remote model.
    // ====================================================================

    // The crash sweep. Realizing all 55-odd leaves against a remote model
    // must not dereference the null connection(). This is the assertion
    // that catches the connection() third of the enumeration.
    void everySetupPageRealizesAgainstARemoteModel()
    {
        RadioModel model(RadioModel::Role::Remote);
        SetupDialog dialog(&model);

        QVERIFY2(dialog.registeredPageCountForTest() > 0,
                 "buildTree() must register at least one navigation leaf");
        dialog.realizeAllPagesForTest();
        QCOMPARE(dialog.realizedPageCountForTest(),
                 dialog.registeredPageCountForTest());
    }

    // The invariant that IS the gate: on a remote model, a page either
    // reached for no live local-DSP object, or it is disabled. Stated as
    // an invariant over the whole tree rather than as a list of page
    // names, so a leaf added tomorrow is covered on the day it lands.
    void everyRemoteSetupPageIsEitherLocalDspFreeOrDisabled()
    {
        RadioModel model(RadioModel::Role::Remote);
        SetupDialog dialog(&model);

        // Iterate by INDEX, not by label. Two leaves are registered as
        // "Options" (General and DSP), and pageEntryIndex() returns the
        // first match, so a label-driven loop realizes the General one
        // twice and never builds the DSP one -- while still reporting a
        // clean sweep, which is the worst possible failure for a test
        // whose whole job is coverage. Fix round 1, Minor 2.
        //
        // R-R3-23: scope-aware. A ThisComputer page (Audio > Devices, the
        // General placeholders, Remote Station, ...) must neither reach
        // local DSP nor come up disabled; see remoteSetupSweepOffenders.
        const int pageCount = dialog.registeredPageCountForTest();
        QVERIFY(pageCount > 0);
        QCOMPARE(dialog.pageLabelsForTest().size(), pageCount);

        int reachedCount = 0;
        const QStringList offenders = remoteSetupSweepOffenders(model, dialog, &reachedCount);

        // Minor 2's fix, pinned: the sweep must have BUILT every leaf. With
        // the previous label-driven loop this read pageCount - 1, because
        // the second "Options" resolved back to the first and its factory
        // never ran -- and the test still reported a clean sweep.
        QCOMPARE(dialog.realizedPageCountForTest(), pageCount);

        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral(
                     "%1 Setup page(s) broke the remote gate: a Core or Mixed "
                     "page reached this process's DSP and stayed enabled, or a "
                     "ThisComputer page reached it or came up disabled: %2")
                                .arg(offenders.size())
                                .arg(offenders.join(QStringLiteral(", ")))));

        // Non-vacuity. If nothing in the tree reaches local DSP any more,
        // the loop above proved nothing and this test is a no-op that
        // would keep passing after the gate was deleted.
        QVERIFY2(reachedCount > 0,
                 "no Setup page reached local DSP at all, so the gate above "
                 "was never exercised -- either the enumeration changed or "
                 "the audit stopped arming");

        // And the scopes the plan fixes (R-R3-23).
        const QStringList labels = dialog.pageLabelsForTest();
        const auto scopeOf = [&](const QString& label) {
            return dialog.pageScopeAtForTest(static_cast<int>(labels.indexOf(label)));
        };
        QCOMPARE(scopeOf(QStringLiteral("Devices")), SetupScope::ThisComputer);
        QCOMPARE(scopeOf(QStringLiteral("TX Input")), SetupScope::Mixed);
        QCOMPARE(scopeOf(QStringLiteral("Advanced")), SetupScope::Mixed);
    }

    // R-R3-23: the sweep catches a ThisComputer page that reaches an
    // audited accessor. Proved with a page that does so on purpose: it is
    // disabled, the critical log names the accessor, and the sweep lists
    // it. A Core page doing the same is disabled with no critical log and
    // is not an offender (the gate working as designed).
    void aThisComputerPageThatReachesLocalDspFailsTheSweep()
    {
        RadioModel model(RadioModel::Role::Remote);
        SetupDialog dialog(&model);
        const QString wrongLabel = QStringLiteral("Deliberately wrong page");
        const QString coreLabel = QStringLiteral("Deliberately gated Core page");
        const int wrong = dialog.registerPageForTest(
            wrongLabel, SetupScope::ThisComputer, [&model]() -> QWidget* {
                (void)model.wdspEngine();
                return new QWidget;
            });
        const int core = dialog.registerPageForTest(
            coreLabel, SetupScope::Core, [&model]() -> QWidget* {
                (void)model.receiverManager();
                return new QWidget;
            });

        // Realized first, on its own, so the log can name exactly the one
        // accessor it reached.
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            QStringLiteral("^Setup page \"?%1\"? is declared ThisComputer but "
                           "reached wdspEngine on a remote-station model")
                .arg(QRegularExpression::escape(wrongLabel))));
        QWidget* const wrongPage = dialog.realizePageAtForTest(wrong);
        QVERIFY(wrongPage != nullptr);
        QVERIFY(!wrongPage->isEnabled());

        // No critical line for the Core page: captured around its
        // realization, then the previous handler is put back.
        static QStringList criticals;
        criticals.clear();
        static QtMessageHandler previous = nullptr;
        previous = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
                if (type == QtCriticalMsg) {
                    criticals << msg;
                    return;
                }
                if (previous != nullptr) {
                    previous(type, context, msg);
                }
            });
        QWidget* const corePage = dialog.realizePageAtForTest(core);
        qInstallMessageHandler(previous);
        QVERIFY(corePage != nullptr);
        QVERIFY(!corePage->isEnabled());
        QVERIFY2(criticals.isEmpty(), qPrintable(criticals.join(QLatin1Char('\n'))));

        const QStringList offenders = remoteSetupSweepOffenders(model, dialog);
        QCOMPARE(offenders, QStringList{wrongLabel});
    }

    // Local direct mode is the regression risk. The same sweep against a
    // Role::Local model must leave every page enabled.
    void theSameSweepAgainstALocalModelDisablesNothing()
    {
        RadioModel model;
        SetupDialog dialog(&model);

        // By index, for the same reason as the remote twin above: the
        // duplicate "Options" label would otherwise leave DSP > Options
        // unvisited in the local-mode regression guard too.
        QStringList disabled;
        const QStringList labels = dialog.pageLabelsForTest();
        const int pageCount = dialog.registeredPageCountForTest();
        for (int i = 0; i < pageCount; ++i) {
            QWidget* page = dialog.realizePageAtForTest(i);
            if (page != nullptr && !page->isEnabled()) {
                disabled << labels.at(i);
            }
        }

        // The same sabotage detector its remote-mode twin carries at the
        // top of this file. Without it, a revert to label-driven iteration
        // here leaves DSP > Options unvisited and this regression guard
        // reports local mode clean while never having built the one page
        // most likely to break it. Fix round 2, Minor 1.
        QCOMPARE(dialog.realizedPageCountForTest(), pageCount);

        QVERIFY2(disabled.isEmpty(),
                 qPrintable(QStringLiteral(
                     "local direct mode regressed: %1 Setup page(s) came up "
                     "disabled: %2")
                                .arg(disabled.size())
                                .arg(disabled.join(QStringLiteral(", ")))));
    }

    // ====================================================================
    // Step 1b: MainWindow's gating entry points, pinned by name.
    // ====================================================================

    // MainWindow cannot be constructed in a unit test (WDSP, audio engine,
    // discovery thread), so the gate's entry points are resolved off the
    // meta-object instead. A rename that stranded the gate -- leaving a
    // remote client with a live Connect action and a live Network
    // Diagnostics dialog reading a null connection -- would otherwise
    // reach a release silently.
    //
    // These must be SLOTS, not plain methods, because that is what makes
    // them resolvable here at all.
    void mainWindowExposesTheRemoteGatingSlots()
    {
        const QMetaObject& mo = MainWindow::staticMetaObject;
        for (const char* sig : {"applyRemoteRoleGating()",
                                "showConnectionPanel()",
                                "openNetworkDiagnostics()"}) {
            QVERIFY2(mo.indexOfSlot(sig) >= 0,
                     qPrintable(QStringLiteral("MainWindow::%1 is not an "
                                               "invokable slot; the remote-mode "
                                               "gate would be unreachable")
                                    .arg(QLatin1String(sig))));
        }
    }

    // ====================================================================
    // Fix round 2, Important 2: the Setup gate, and the fact that it is
    // now hung on something.
    // ====================================================================

    // MainWindow::createSetupDialog() is the only place in src/gui that
    // runs `new SetupDialog`; all twelve former call sites go through it.
    // MainWindow cannot be constructed here, so the name is pinned off the
    // meta-object, the same seam the gating slots above use. A rename or a
    // demotion to a plain method would strand the test that proves the gate
    // exists at all, and a thirteenth site constructing the dialog inline
    // would slip past a gate nobody was asserting on.
    void mainWindowRoutesSetupDialogThroughOneGatedFactory()
    {
        const QMetaObject& mo = MainWindow::staticMetaObject;
        QVERIFY2(mo.indexOfSlot("createSetupDialog()") >= 0,
                 "MainWindow::createSetupDialog() is not an invokable slot; "
                 "the Setup gate would be unreachable and unpinnable");
    }

    // Local direct mode, which is every existing user. AppSettings holds no
    // remote backend there, so the gate must not have an opinion.
    void setupGateIsOpenWhenNoRemoteBackendIsInstalled()
    {
        QVERIFY(AppSettings::instance().remoteBackend() == nullptr);
        QVERIFY2(setupDialogAllowedForCurrentBackend(),
                 "local direct mode has no SettingsProxy, so the gate must "
                 "open unconditionally; refusing here would take Setup away "
                 "from every non-remote user");
    }

    // A backend that is not a SettingsProxy is not this gate's business
    // either. The cross-cast yields nullptr and the gate opens.
    void setupGateIsOpenBehindAnUnrecognisedBackend()
    {
        StubBackend stub;
        AppSettings::instance().setRemoteBackend(&stub);
        QVERIFY(setupDialogAllowedForCurrentBackend());
        AppSettings::instance().setRemoteBackend(nullptr);
    }

    // The states that matter, asked through the production entry point
    // rather than through the method directly: this is what
    // MainWindow::createSetupDialog() actually calls, so a regression in
    // the cross-cast or the delegation shows up here and not only in
    // tst_settings_proxy's method-level coverage.
    void setupGateFollowsTheInstalledProxyThroughItsStates()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);

        // Pre-handshake. This is the window in which 187 widget
        // constructors would otherwise read their ship defaults and start
        // writing them into the STATION store on first touch.
        QVERIFY2(!setupDialogAllowedForCurrentBackend(),
                 "the gate must be shut before the handshake completes");

        // Ready, but nothing has arrived: a freshly reserved daemon
        // profile looks exactly like this, which is why ready() alone was
        // never sufficient.
        proxy.setReady(true);
        QVERIFY2(!setupDialogAllowedForCurrentBackend(),
                 "ready() alone must not open the gate");

        // Ready and empty is still shut.
        proxy.applySnapshot(QMap<QString, QString>{});
        QVERIFY(!setupDialogAllowedForCurrentBackend());

        // The seed marker alone opens it: that is what tells "empty
        // because the daemon profile is fresh" apart from "empty because
        // something is broken".
        QMap<QString, QString> seeded;
        seeded.insert(QLatin1String(AppSettings::kDaemonProfileSeededKey),
                      QStringLiteral("True"));
        proxy.applySnapshot(seeded);
        QVERIFY2(setupDialogAllowedForCurrentBackend(),
                 "a legitimately fresh daemon profile must not be locked out "
                 "of Setup, or a remote operator can never configure one");

        // And so does real station content.
        proxy.applySnapshot(QMap<QString, QString>{
            {QStringLiteral("Slice0/Locked"), QStringLiteral("True")}});
        QVERIFY(setupDialogAllowedForCurrentBackend());

        AppSettings::instance().setRemoteBackend(nullptr);
    }

    // ====================================================================
    // Step 4: MOX refuses on a remote model, through the EXISTING
    // pre-check, so the refusal reaches the status-bar toast.
    // ====================================================================

    void remoteModelRefusesMoxWithAnOperatorReason()
    {
        RadioModel model(RadioModel::Role::Remote);
        MoxController* mox = model.moxController();
        QVERIFY(mox != nullptr);
        mox->setTimerIntervals(0, 0, 0, 0, 0, 0);

        QSignalSpy rejected(mox, &MoxController::moxRejected);
        QVERIFY(rejected.isValid());

        model.setMox(true);

        QCOMPARE(rejected.count(), 1);
        const QString reason = rejected.at(0).at(0).toString();
        QVERIFY2(reason.contains(QStringLiteral("transmit"))
                     && reason.contains(QStringLiteral("Core"))
                     && !reason.contains(QStringLiteral("R4")),
                 qPrintable(QStringLiteral("refusal must explain the unavailable "
                                           "Core operation without roadmap jargon: %1")
                                .arg(reason)));
        QVERIFY(!mox->isMox());
        QVERIFY(!model.mox());
    }

    // The refusal must survive a disconnect. disconnectFromRadio() is
    // reachable on a remote client (aboutToQuit calls it unconditionally),
    // and teardownConnection() contains a setMoxCheck({}) -- an EMPTY
    // MoxCheckFn being MoxController's BYPASS, not its deny.
    //
    // Honest about what this pins: today the clear is not reached at all,
    // because teardownConnection() returns early on a null m_connection and
    // a Role::Remote model always has one. Verified by sabotage -- removing
    // the role guard on that clear leaves this case green. So what this
    // asserts is the OBSERVABLE invariant (MOX stays refused across a
    // disconnect), which is currently made true by the constructor-time
    // install rather than by the guard. It is still the assertion worth
    // having: it is stated in terms of behaviour, so it keeps holding
    // whichever of the two mechanisms is the live one.
    void remoteMoxRefusalSurvivesDisconnectFromRadio()
    {
        RadioModel model(RadioModel::Role::Remote);
        MoxController* mox = model.moxController();
        QVERIFY(mox != nullptr);
        mox->setTimerIntervals(0, 0, 0, 0, 0, 0);

        model.disconnectFromRadio();

        QSignalSpy rejected(mox, &MoxController::moxRejected);
        model.setMox(true);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!mox->isMox());
    }

    // Non-vacuity for the two above: a Role::Local model with the same
    // pre-check installed and a legal mode/frequency must still key up.
    // Without this, a gate that refused MOX unconditionally would pass
    // both tests above and break every local user.
    void localModelStillKeysUpOnALegalFrequency()
    {
        RadioModel model;
        model.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5, 192000);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.installBandPlanMoxCheckForTest();
        // Key from the radio mic: with the PC mic selected the R-R3-36
        // admission would refuse (no capture is Ready here), which is not
        // what this case is about.
        model.transmitModel().setMicSource(MicSource::Radio);

        const int aId = model.addSlice();
        SliceModel* const a = model.sliceById(aId);
        QVERIFY(a != nullptr);
        a->setDspMode(DSPMode::USB);
        a->setFrequency(14'200'000.0);

        QSignalSpy rejected(model.moxController(), &MoxController::moxRejected);
        model.setMox(true);
        QCOMPARE(rejected.count(), 0);
        QVERIFY(model.moxController()->isMox());
        model.setMox(false);
    }

    // ====================================================================
    // Step 2: --station / --token, and the field group's two keys.
    // ====================================================================

    void stationUrlAcceptsWsAndWss()
    {
        QString why;
        QVERIFY(RemoteStationOptions::isValidStationUrl(
            QStringLiteral("wss://station.example:4433"), &why));
        QVERIFY(RemoteStationOptions::isValidStationUrl(
            QStringLiteral("ws://127.0.0.1:50100"), &why));
    }

    void stationUrlRejectsHttpEmptyAndHostless()
    {
        QString why;

        QVERIFY(!RemoteStationOptions::isValidStationUrl(QString(), &why));
        QVERIFY(!why.isEmpty());

        why.clear();
        QVERIFY(!RemoteStationOptions::isValidStationUrl(
            QStringLiteral("https://station.example:4433"), &why));
        QVERIFY2(why.contains(QStringLiteral("wss://")),
                 qPrintable(QStringLiteral("the reason must name the scheme the "
                                           "field wants, got: %1").arg(why)));

        why.clear();
        QVERIFY(!RemoteStationOptions::isValidStationUrl(
            QStringLiteral("wss://"), &why));
        QVERIFY(!why.isEmpty());
    }

    void emptyStationUrlMeansLocalDirectMode()
    {
        RemoteStationOptions opts;
        QVERIFY(!opts.isRemote());
        opts.url = QStringLiteral("wss://127.0.0.1:50100");
        QVERIFY(opts.isRemote());
    }

    // The station address and its token are the client's own. If either
    // ever classified Station, every GUI connected to one daemon would
    // write its address book -- and its credentials -- into the shared
    // store, and read each other's back.
    void stationFieldGroupKeysAreOperatorLocal()
    {
        QCOMPARE(classifySettingsKey(kStationUrlKey), SettingsScope::OperatorLocal);
        QCOMPARE(classifySettingsKey(kStationTokenKey), SettingsScope::OperatorLocal);

        // All four keys the field group writes, not just the two the brief
        // named. The fingerprint is per-client TRUST state: a rule that
        // classified it Station would share one client's certificate pin
        // with every other client of the same daemon, which is a downgrade
        // no operator asked for and none would see. The allow-unpinned flag
        // is worse, because it would let one bench client turn pinning off
        // for everyone. Fix round 1, Minor 6.
        QCOMPARE(classifySettingsKey(kStationFingerprintKey),
                 SettingsScope::OperatorLocal);
        QCOMPARE(classifySettingsKey(kStationAllowUnpinnedKey),
                 SettingsScope::OperatorLocal);
    }

    // The one page a remote operator must be able to reach, since it is
    // where the station address lives. If the gate above ever disabled it,
    // a mistyped station would be unrecoverable from the GUI. This is also
    // the counter-example that keeps the gate honest: it proves the sweep
    // disables SOME pages and not simply all of them.
    void theRemoteStationPageStaysUsableOnARemoteModel()
    {
        RadioModel model(RadioModel::Role::Remote);
        SetupDialog dialog(&model);

        QVERIFY2(dialog.pageLabelsForTest().contains(
                     QStringLiteral("Remote Station")),
                 "the Setup field group carrying --station / --token is not "
                 "registered under any leaf");

        QWidget* page = dialog.realizePageForTest(QStringLiteral("Remote Station"));
        QVERIFY(page != nullptr);
        QSignalSpy requests(&dialog, &SetupDialog::connectionsRequested);
        auto* button = page->findChild<QPushButton*>(QStringLiteral("remoteStationConnections"));
        QVERIFY(button);
        button->click();
        QCOMPARE(requests.count(), 1);
        QVERIFY2(page->isEnabled(),
                 "the Remote Station page was disabled by the local-DSP gate; "
                 "it must not touch this process's DSP at all");
    }

    // ====================================================================
    // The ordering SettingsProxy.h explicitly asks Task 20 to confirm.
    // ====================================================================

    // SliceModel, NotchModel, FilterPresetStore and TciServer all do
    // contains()-then-seed against Station-classified prefixes in their
    // constructors. On a remote client those run before any snapshot can
    // land. What stops them baking this client's ship defaults into the
    // STATION store is entirely that SettingsProxy::ready() is still
    // false -- writes update the cache and are dropped rather than sent.
    // SettingsProxy.h:217-238 records that as load-bearing-but-accidental
    // and asks Task 20 to pin the ordering rather than inherit it.
    void constructingARemoteModelBehindTheProxyOffersNoOutboundWrite()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        QVERIFY(!proxy.ready());

        QSignalSpy outbound(&proxy, &SettingsProxy::outboundWriteRequested);
        QVERIFY(outbound.isValid());

        {
            RadioModel model(RadioModel::Role::Remote);
            model.addSlice();
        }

        QVERIFY2(!proxy.ready(),
                 "nothing in RadioModel construction may flip the proxy ready; "
                 "if it does, every seed-if-absent constructor write starts "
                 "reaching the station store");
        QCOMPARE(outbound.count(), 0);

        AppSettings::instance().setRemoteBackend(nullptr);
    }

    // ====================================================================
    // Fix round 4, Critical: isConnected() stopped implying a connection.
    //
    // Before this branch, RadioModel::isConnected() was
    // `m_connection && m_connection->isConnected()`, so any caller that
    // tested it had ALSO tested connection() for null without meaning to.
    // Task 3 made it storage-backed (m_connectionState == Connected) so a
    // client that deliberately owns no RadioConnection can report
    // Connected. Every caller that leaned on the old implication became a
    // null dereference the moment a station handshake completed.
    //
    // The two tests below pin the two halves of what is now true, so the
    // next reader is not left inferring the implication from the name.
    // ====================================================================

    // Half one: on a Role::Remote model the implication is FALSE, and that
    // is the supported steady state, not a transient. This is the exact
    // precondition MainWindow::onConnectionStateChanged() crashed on.
    //
    // setStationConnectionState() is used rather than
    // applyStationCapabilities() on purpose: it is the narrowest public
    // writer of the same m_connectionState, so the assertion does not go
    // stale if the capabilities struct gains or loses a field.
    void remoteModelReportsConnectedWhileConnectionStaysNull()
    {
        RadioModel model(RadioModel::Role::Remote);
        QVERIFY(!model.isConnected());
        QVERIFY(model.connection() == nullptr);

        model.setStationConnectionState(ConnectionState::Connected);

        QCOMPARE(model.connectionState(), ConnectionState::Connected);
        QVERIFY2(model.isConnected(),
                 "a remote client whose station holds the radio must report "
                 "Connected; that is the premise of R2");
        QVERIFY2(model.connection() == nullptr,
                 "and it must still hold no RadioConnection. Any GUI branch "
                 "that reads isConnected() and then dereferences connection() "
                 "runs here, on a null pointer");
    }

    // Half two, and the non-vacuity for half one: a Role::Local model
    // cannot be talked into that state. Its connection state has exactly
    // one writer, its own RadioConnection, so the old implication still
    // holds for every existing local user. A change that let the storage
    // be forced on a local model would put local direct mode into the
    // same shape as the crash above.
    void localModelConnectionStateCannotBeForcedFromStorage()
    {
        RadioModel model;
        QVERIFY(model.connection() == nullptr);
        QVERIFY(!model.isConnected());

        model.setStationConnectionState(ConnectionState::Connected);

        QVERIFY2(!model.isConnected(),
                 "setStationConnectionState must be refused on a local model; "
                 "if it is not, isConnected() can go true with no connection "
                 "in local direct mode too");
        QVERIFY(model.connection() == nullptr);
    }

    // MainWindow cannot be constructed here (see the file banner), so the
    // slot that carries the crash is pinned by name only.
    //
    // Stated plainly, because it matters: this proves the slot still
    // exists and is still invokable, so the connect at MainWindow.cpp's
    // connectionStateChanged wiring cannot be silently unmade by a rename.
    // It proves NOTHING about the body -- it does not execute one line of
    // it, and it would pass just as happily with the null dereference
    // still in place. The guard itself is unreachable from a unit test in
    // this tree; the two model-side cases above are what state the
    // precondition, and the audit note in RadioModel.h is what tells the
    // next author the precondition is real.
    void mainWindowExposesTheConnectionStateSlot()
    {
        const QMetaObject& mo = MainWindow::staticMetaObject;
        QVERIFY2(mo.indexOfSlot("onConnectionStateChanged()") >= 0,
                 "MainWindow::onConnectionStateChanged() is not an invokable "
                 "slot; the connectionStateChanged wiring would be unmade");
    }

    // ====================================================================
    // Fix round 4, Important: TUNE is a second door into the transmitter
    // and it was not gated.
    //
    // RadioModel::setTune(true)'s power-on guard is `!isConnected() ||
    // !m_audioEngine`. Both halves pass on a connected remote model:
    // isConnected() is storage-backed (above), and m_audioEngine is
    // constructed unconditionally. MoxController::setTune(true) then sets
    // PttMode::Manual and m_manualMox and EMITS manualMoxChanged(true)
    // before it calls setMox(), and only setMox consults the R2 refusal --
    // so the refusal arrived after the state had already advanced.
    // ====================================================================

    void remoteModelRefusesTuneBeforeAnyStateAdvances()
    {
        RadioModel model(RadioModel::Role::Remote);
        MoxController* mox = model.moxController();
        QVERIFY(mox != nullptr);
        mox->setTimerIntervals(0, 0, 0, 0, 0, 0);

        // The precondition that made the old guard pass.
        model.setStationConnectionState(ConnectionState::Connected);
        QVERIFY(model.isConnected());

        QSignalSpy refused(&model, &RadioModel::tuneRefused);
        QSignalSpy manual(mox, &MoxController::manualMoxChanged);
        QVERIFY(refused.isValid());
        QVERIFY(manual.isValid());

        model.setTune(true);

        QCOMPARE(refused.count(), 1);
        const QString reason = refused.at(0).at(0).toString();
        QVERIFY2(reason.contains(QStringLiteral("transmit"))
                     && reason.contains(QStringLiteral("Core"))
                     && !reason.contains(QStringLiteral("R4")),
                 qPrintable(QStringLiteral("refusal must explain the unavailable "
                                           "Core operation without roadmap jargon: %1")
                                .arg(reason)));

        // Nothing may have advanced. manualMoxChanged is the one that
        // reaches the UI: TxApplet paints the TUNE button "TUNING..." off
        // it, so an emission here leaves an operator looking at a button
        // that says the radio is transmitting.
        QCOMPARE(manual.count(), 0);
        QVERIFY(!mox->isManualMox());
        QVERIFY(!mox->isMox());
        QVERIFY2(!model.isTune(),
                 "m_isTuning must not latch: nothing clears it on a remote "
                 "model, because teardownConnection()'s clear sits behind "
                 "`if (!m_connection) return;`");
    }

    // The leak this closes does not stop at the client. TransmitModel is
    // watched for outbound mirroring and MirrorPolicy marks `tune`
    // Bidirectional, so a client-side TUNE press wrote tune=true on the
    // DAEMON -- which is what TransmitModel::setPowerUsingTargetDbm reads
    // to select txMode = 1, silently switching the station's drive-power
    // source out from under the operator sitting at it.
    void remoteTuneRefusalWritesNoMirroredTransmitState()
    {
        RadioModel model(RadioModel::Role::Remote);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.setStationConnectionState(ConnectionState::Connected);

        const int powerBefore = model.transmitModel().power();

        QSignalSpy tuneChanged(&model.transmitModel(),
                               &TransmitModel::tuneChanged);
        QSignalSpy powerChanged(&model.transmitModel(),
                                &TransmitModel::powerChanged);
        QVERIFY(tuneChanged.isValid());
        QVERIFY(powerChanged.isValid());

        model.setTune(true);

        QCOMPARE(tuneChanged.count(), 0);
        QVERIFY2(!model.transmitModel().isTune(),
                 "TransmitModel::tune is Bidirectional in MirrorPolicy; "
                 "setting it here writes it on the station");
        QCOMPARE(powerChanged.count(), 0);
        QCOMPARE(model.transmitModel().power(), powerBefore);
    }

    // Non-vacuity: a Role::Local model must keep refusing TUNE for the
    // ORIGINAL reason (power off), not the new one. A gate that refused
    // unconditionally, or that reported the Core refusal locally, would pass
    // the two cases above and mislead every local user.
    void localModelStillRefusesTuneForPowerNotForRole()
    {
        RadioModel model;
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        QVERIFY(!model.isConnected());

        QSignalSpy refused(&model, &RadioModel::tuneRefused);
        model.setTune(true);

        QCOMPARE(refused.count(), 1);
        const QString reason = refused.at(0).at(0).toString();
        QVERIFY2(reason.contains(QStringLiteral("Power")),
                 qPrintable(QStringLiteral("local direct mode must still get "
                                           "the power-on reason; got: %1")
                                .arg(reason)));
        QVERIFY2(!reason.contains(QStringLiteral("Core")),
                 "the remote reason must not leak into local direct mode");
        QVERIFY(!model.isTune());
    }

    // ====================================================================
    // Fix round 4, Important (sibling): the MOX button stayed checked
    // after a refusal.
    //
    // MoxController::setMox(true) returns on rejection without advancing
    // state, so moxStateChanged never fires -- and moxStateChanged was the
    // ONLY thing that unchecked the button. Locally that is occasional
    // (band-plan / interlock rejections). Remotely EVERY press is
    // rejected, so the button was permanently wrong.
    // ====================================================================

    void moxButtonUnchecksItselfWhenTheRequestIsRefused()
    {
        RadioModel model(RadioModel::Role::Remote);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.setStationConnectionState(ConnectionState::Connected);

        // TxApplet::wireControls() reads m_model->moxController() itself,
        // from the constructor, so there is nothing to inject.
        TxApplet applet(&model);
        applet.setTransmitPermitted(true);

        QPushButton* moxBtn = nullptr;
        for (QPushButton* b : applet.findChildren<QPushButton*>()) {
            if (b->accessibleName() == QStringLiteral("MOX transmit")) {
                moxBtn = b;
                break;
            }
        }
        QVERIFY2(moxBtn != nullptr,
                 "MOX button not found by accessible name; the applet's "
                 "accessible names are the only stable handle a test has");

        QSignalSpy rejected(model.moxController(), &MoxController::moxRejected);
        QVERIFY(rejected.isValid());

        moxBtn->setChecked(true);

        QCOMPARE(rejected.count(), 1);
        QVERIFY(!model.moxController()->isMox());
        QVERIFY2(!moxBtn->isChecked(),
                 "the button must follow the refusal; leaving it checked "
                 "tells the operator the radio is transmitting when it is "
                 "not");
    }

    void remoteTransmitPermissionDisablesActivationWithoutWritingModelState()
    {
        RadioModel model(RadioModel::Role::Remote);
        model.setStationConnectionState(ConnectionState::Connected);
        TxApplet applet(&model);
        applet.setTransmitPermitted(false,
                                    QStringLiteral("Remote transmit is unavailable"));

        const auto findButton = [&applet](const QString& accessibleName) {
            for (QPushButton* button : applet.findChildren<QPushButton*>()) {
                if (button->accessibleName() == accessibleName) {
                    return button;
                }
            }
            return static_cast<QPushButton*>(nullptr);
        };
        QPushButton* const tune = findButton(QStringLiteral("Tune carrier"));
        QPushButton* const mox = findButton(QStringLiteral("MOX transmit"));
        QPushButton* const vox = findButton(
            QStringLiteral("VOX voice-operated transmit"));
        QVERIFY(tune != nullptr);
        QVERIFY(mox != nullptr);
        QVERIFY(vox != nullptr);
        QVERIFY(!tune->isEnabled());
        QVERIFY(!mox->isEnabled());
        QVERIFY(!vox->isEnabled());
        QCOMPARE(mox->toolTip(), QStringLiteral("Remote transmit is unavailable"));

        QSignalSpy moxRejected(model.moxController(), &MoxController::moxRejected);
        QSignalSpy tuneRefused(&model, &RadioModel::tuneRefused);
        QSignalSpy voxChanged(&model.transmitModel(), &TransmitModel::voxEnabledChanged);

        // QAbstractButton::click() is the widget activation path and is a
        // no-op while disabled. It verifies the presentation gate prevents
        // reaching the existing MOX/TUNE/VOX model handlers.
        tune->click();
        mox->click();
        vox->click();

        QCOMPARE(moxRejected.count(), 0);
        QCOMPARE(tuneRefused.count(), 0);
        QCOMPARE(voxChanged.count(), 0);
        QVERIFY(!model.mox());
        QVERIFY(!model.isTune());
        QVERIFY(!model.transmitModel().voxEnabled());
    }

    void remoteTransmitControlsStartDeniedBeforeHandshakePermission()
    {
        RadioModel model(RadioModel::Role::Remote);
        TxApplet applet(&model);

        const auto findButton = [&applet](const QString& accessibleName) {
            for (QPushButton* button : applet.findChildren<QPushButton*>()) {
                if (button->accessibleName() == accessibleName) {
                    return button;
                }
            }
            return static_cast<QPushButton*>(nullptr);
        };
        QPushButton* const tune = findButton(QStringLiteral("Tune carrier"));
        QPushButton* const mox = findButton(QStringLiteral("MOX transmit"));
        QVERIFY(tune != nullptr);
        QVERIFY(mox != nullptr);

        QVERIFY(!tune->isEnabled());
        QVERIFY(!mox->isEnabled());
        QCOMPARE(mox->toolTip(),
                 QStringLiteral("Transmit controls are unavailable until the station "
                                "confirms transmit permission."));
        QVERIFY2(OperatorWording::isPlain(mox->toolTip()), qPrintable(mox->toolTip()));

        QSignalSpy moxRejected(model.moxController(), &MoxController::moxRejected);
        QSignalSpy tuneRefused(&model, &RadioModel::tuneRefused);
        tune->click();
        mox->click();
        QCOMPARE(moxRejected.count(), 0);
        QCOMPARE(tuneRefused.count(), 0);
    }

    void localTransmitControlsRemainEnabledByDefault()
    {
        RadioModel model;
        TxApplet applet(&model);

        QVERIFY(applet.rfPowerSlider()->isEnabled());
        QVERIFY(applet.tunePowerSlider()->isEnabled());
        QVERIFY(applet.findChild<QPushButton*>(QStringLiteral("TxVoxButton"))->isEnabled());
    }

    void transmitPermissionRestorePreservesAnExistingFeatureGate()
    {
        RadioModel model;
        TxApplet applet(&model);
        QPushButton* const twoTone = applet.twoToneButton();
        QVERIFY(twoTone != nullptr);

        // Simulate an independent feature/dependency gate that was already
        // in effect before remote permission was denied.
        twoTone->setEnabled(false);
        applet.setTransmitPermitted(false, QStringLiteral("Remote transmit is unavailable"));
        applet.setTransmitPermitted(true);

        QVERIFY(!twoTone->isEnabled());
    }

    void remoteHighResolutionFilterGraphControlIsExplicitlyUnavailable()
    {
        RadioModel model(RadioModel::Role::Remote);
        DspOptionsPage page(&model);
        QCheckBox* const highRes = page.highResolutionFilterCharacteristicsCheckBox();
        QVERIFY(highRes != nullptr);
        QVERIFY(!highRes->isEnabled());
        QVERIFY(highRes->toolTip().contains(QStringLiteral("local direct mode")));

        QSignalSpy toggled(highRes, &QCheckBox::toggled);
        highRes->click();
        QCOMPARE(toggled.count(), 0);
        QVERIFY(!highRes->isChecked());
    }

    void localHighResolutionFilterGraphControlRemainsAvailable()
    {
        RadioModel model;
        DspOptionsPage page(&model);
        QCheckBox* const highRes = page.highResolutionFilterCharacteristicsCheckBox();
        QVERIFY(highRes != nullptr);
        QVERIFY(highRes->isEnabled());
    }

    // ====================================================================
    // R-R3-21 control inventory: the two Setup leaves the audit found
    // transmit-only but not following the transmit permission.
    //
    // DSP > CFC holds the Phase Rotator, CFC and CESSB, all TX stages, and
    // its [Configure CFC bands] button opens the TX CFC editor. Test >
    // Two-Tone IMD writes the two-tone test settings of a keyed test
    // transmission. Neither writes anything the Core mirrors, so on a
    // receive-only session each was a live-looking page whose edits
    // landed in this window's own TransmitModel and nowhere else.
    // ====================================================================
    void remoteTransmitOnlySetupLeavesFollowThePermission_data()
    {
        QTest::addColumn<QString>("label");
        QTest::newRow("CFC") << QStringLiteral("CFC");
        QTest::newRow("Two-Tone IMD") << QStringLiteral("Two-Tone IMD");
    }

    void remoteTransmitOnlySetupLeavesFollowThePermission()
    {
        QFETCH(QString, label);
        const QString reason = QStringLiteral("Remote transmit is unavailable");

        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setTransmitPermitted(false, reason);
        QSignalSpy cfcEditor(&dialog, &SetupDialog::cfcDialogRequested);

        const int handOutsBefore = remote.localDspHandOutCount();
        dialog.selectPage(label);
        QWidget* const page = dialog.realizedPageForTest(label);
        QVERIFY(page != nullptr);
        // Neither page reaches this process's DSP, so the transmit gate is
        // the only thing disabling it; the case below proves the gate, not
        // the resource audit.
        QCOMPARE(remote.localDspHandOutCount(), handOutsBefore);
        QVERIFY(!page->isEnabled());
        QCOMPARE(page->toolTip(), reason);
        QTreeWidgetItem* const leaf = setupLeaf(dialog, label);
        QVERIFY(leaf != nullptr);
        QCOMPARE(leaf->toolTip(0), reason);
        auto* const notice = dialog.findChild<QLabel*>(QStringLiteral("setupTransmitUnavailable"));
        QVERIFY(notice != nullptr);
        QVERIFY(!notice->isHidden());
        QCOMPARE(notice->text(), reason);

        // Activation reaches nothing: every button, check box and spin box
        // on the page is driven, and no transmit setting moves.
        const TransmitModel& tx = remote.transmitModel();
        const bool cfc = tx.cfcEnabled();
        const bool cfcPostEq = tx.cfcPostEqEnabled();
        const int precomp = tx.cfcPrecompDb();
        const bool phaseRotator = tx.phaseRotatorEnabled();
        const bool cessb = tx.cessbOn();
        const int freq1 = tx.twoToneFreq1();
        const int freq2 = tx.twoToneFreq2();
        const bool pulsed = tx.twoTonePulsed();
        const bool invert = tx.twoToneInvert();
        for (QAbstractButton* button : page->findChildren<QAbstractButton*>()) {
            QVERIFY2(!button->isEnabled(), qPrintable(button->text()));
            button->click();
        }
        for (QSpinBox* spin : page->findChildren<QSpinBox*>()) {
            QVERIFY(!spin->isEnabled());
            QTest::keyClick(spin, Qt::Key_Up);
        }
        QCOMPARE(tx.cfcEnabled(), cfc);
        QCOMPARE(tx.cfcPostEqEnabled(), cfcPostEq);
        QCOMPARE(tx.cfcPrecompDb(), precomp);
        QCOMPARE(tx.phaseRotatorEnabled(), phaseRotator);
        QCOMPARE(tx.cessbOn(), cessb);
        QCOMPARE(tx.twoToneFreq1(), freq1);
        QCOMPARE(tx.twoToneFreq2(), freq2);
        QCOMPARE(tx.twoTonePulsed(), pulsed);
        QCOMPARE(tx.twoToneInvert(), invert);
        QCOMPARE(cfcEditor.count(), 0);

        // A Core that permits transmit lifts the gate, and withdrawing it
        // puts it back.
        dialog.setTransmitPermitted(true);
        QVERIFY(page->isEnabled());
        QVERIFY(page->toolTip().isEmpty());
        QVERIFY(leaf->toolTip(0).isEmpty());
        QVERIFY(notice->isHidden());
        dialog.setTransmitPermitted(false, reason);
        QVERIFY(!page->isEnabled());
        QVERIFY(!notice->isHidden());

        // The reason MainWindow actually passes is plain English.
        dialog.setTransmitPermitted(false);
        QVERIFY2(OperatorWording::isPlain(notice->text()), qPrintable(notice->text()));

        // Local direct mode: unchanged, live, no reason shown.
        RadioModel local;
        SetupDialog localDialog(&local);
        localDialog.selectPage(label);
        QWidget* const localPage = localDialog.realizedPageForTest(label);
        QVERIFY(localPage != nullptr);
        QVERIFY(localPage->isEnabled());
        QVERIFY(localPage->toolTip().isEmpty());
        QVERIFY(localDialog.findChild<QLabel*>(
                    QStringLiteral("setupTransmitUnavailable"))->isHidden());
    }

    // ====================================================================
    // R-R3-21: a page the local-DSP gate disables says why.
    //
    // The gate (SetupDialog::realizePage) disabled the Audio leaves on a
    // remote model but gave no reason, so the operator saw a greyed page
    // and nothing else. R-R3-23 narrowed the set: Devices and TX Input
    // pick this computer's devices and now work (cases below). VAX and
    // Advanced still reach this process's engine themselves; TCI reached it
    // only through the backend strip, which no longer counts, so it keeps
    // its remote behaviour by declaration, with the same reason.
    // ====================================================================
    void remoteLocalDspSetupPagesShowAPlainReason_data()
    {
        QTest::addColumn<QString>("label");
        QTest::addColumn<bool>("reachesLocalDsp");
        QTest::newRow("VAX") << QStringLiteral("VAX") << true;
        QTest::newRow("TCI") << QStringLiteral("TCI") << false;
        QTest::newRow("Advanced") << QStringLiteral("Advanced") << true;
    }

    void remoteLocalDspSetupPagesShowAPlainReason()
    {
        QFETCH(QString, label);
        QFETCH(bool, reachesLocalDsp);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setTransmitPermitted(false, QStringLiteral("Remote transmit is unavailable"));

        const int handOutsBefore = remote.localDspHandOutCount();
        dialog.selectPage(label);
        QWidget* const page = dialog.realizedPageForTest(label);
        QVERIFY(page != nullptr);
        QVERIFY2((remote.localDspHandOutCount() > handOutsBefore) == reachesLocalDsp,
                 "whether the page reaches local DSP changed; re-audit its row "
                 "in remote-controls.md before changing this case");
        QVERIFY(!page->isEnabled());

        auto* const localNotice = dialog.findChild<QLabel*>(QStringLiteral("setupLocalUnavailable"));
        auto* const txNotice = dialog.findChild<QLabel*>(QStringLiteral("setupTransmitUnavailable"));
        QVERIFY(localNotice != nullptr);
        QVERIFY(txNotice != nullptr);
        QVERIFY(!localNotice->isHidden());
        QVERIFY(txNotice->isHidden());
        const QString reason = localNotice->text();
        QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
        QCOMPARE(page->toolTip(), reason);
        QTreeWidgetItem* const leaf = setupLeaf(dialog, label);
        QVERIFY(leaf != nullptr);
        QCOMPARE(leaf->toolTip(0), reason);

        // A transmit permission does not make this computer's audio engine
        // the station's; the page and its reason stay.
        dialog.setTransmitPermitted(true);
        QVERIFY(!page->isEnabled());
        QVERIFY(!localNotice->isHidden());

        // Moving to a receive page that is available hides the notice.
        dialog.selectPage(QStringLiteral("NR/ANF"));
        QVERIFY(localNotice->isHidden());
        QVERIFY(txNotice->isHidden());
    }

    // R-R3-23: Audio > Devices in a remote window picks this computer's
    // speakers, headphones and microphone as it always has, whatever the
    // transmit permission. Nothing counted by the local-DSP audit is
    // reached, so the page is enabled with no reason shown, and a card
    // change is saved to this computer's audio/* keys and handed to the
    // engine that plays remote audio.
    void remoteDevicesPageWorksOnThisComputer()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setTransmitPermitted(false, QStringLiteral("Remote transmit is unavailable"));

        const int handOutsBefore = remote.localDspHandOutCount();
        dialog.selectPage(QStringLiteral("Devices"));
        QWidget* const page = dialog.realizedPageForTest(QStringLiteral("Devices"));
        QVERIFY(page != nullptr);
        QCOMPARE(remote.localDspHandOutCount(), handOutsBefore);
        QVERIFY(page->isEnabled());
        QVERIFY(page->toolTip().isEmpty());
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("setupLocalUnavailable"))->isHidden());
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("setupTransmitUnavailable"))->isHidden());
        QTreeWidgetItem* const leaf = setupLeaf(dialog, QStringLiteral("Devices"));
        QVERIFY(leaf != nullptr);
        QVERIFY(leaf->toolTip(0).isEmpty());

        for (const char* title : {"Speakers", "Headphones", "TX Input (Microphone)"}) {
            DeviceCard* const card = deviceCardOf(page, QString::fromLatin1(title));
            QVERIFY2(card != nullptr, title);
            QVERIFY2(card->isEnabled(), title);
        }

        // The microphone choice: saved to audio/TxInput/* and handed to the
        // engine (the one Test Mic and a later remote microphone open).
        DeviceCard* const mic = deviceCardOf(page, QStringLiteral("TX Input (Microphone)"));
        QComboBox* const buffer = deviceCardBufferCombo(mic);
        QVERIFY(buffer != nullptr);
        const int next = (buffer->currentIndex() + 1) % buffer->count();
        const int samples = buffer->itemData(next).toInt();
        // The card debounces its buffer combo by 200 ms, then saves and
        // hands the config on.
        buffer->setCurrentIndex(next);
        QTRY_COMPARE(remote.localAudioDevices()->txInputConfig().bufferSamples, samples);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("audio/TxInput/BufferSamples"))
                     .toString(),
                 QString::number(samples));
        QCOMPARE(remote.localDspHandOutCount(), handOutsBefore);
    }

    // R-R3-36: TX Input is Mixed. This computer's PC microphone (backend,
    // device, buffer, Test Mic) works in a remote window; the mic source,
    // Mic Gain and the radio's microphone hardware follow the transmit
    // permission with its reason, and move nothing while it is withheld.
    void remoteTxInputKeepsThisComputersMicrophoneUsable()
    {
        const QString txReason = QStringLiteral("Remote transmit is unavailable");
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setTransmitPermitted(false, txReason);

        const int handOutsBefore = remote.localDspHandOutCount();
        dialog.selectPage(QStringLiteral("TX Input"));
        QWidget* const container = dialog.realizedPageForTest(QStringLiteral("TX Input"));
        QVERIFY(container != nullptr);
        QCOMPARE(remote.localDspHandOutCount(), handOutsBefore);
        QVERIFY(container->isEnabled());
        auto* const page = container->findChild<AudioTxInputPage*>();
        QVERIFY(page != nullptr);
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("setupLocalUnavailable"))->isHidden());
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("setupTransmitUnavailable"))->isHidden());

        // This computer's microphone.
        QVERIFY(page->backendCombo()->isEnabled());
        QVERIFY(page->deviceCombo()->isEnabled());
        QVERIFY(page->bufferSlider()->isEnabled());
        QVERIFY(page->testMicButton()->isEnabled());

        // The controls held for the radio: disabled, with the reason.
        QList<QWidget*> held{page->micSourceGroup(), page->micGainSlider()};
        for (QGroupBox* group : {page->hermesRadioMicGroup(), page->orionRadioMicGroup(),
                                 page->saturnRadioMicGroup()}) {
            if (group != nullptr) { held << group; }
        }
        for (QWidget* control : held) {
            QVERIFY(control != nullptr);
            QVERIFY2(!control->isEnabled(), qPrintable(control->objectName()));
            QCOMPARE(control->toolTip(), txReason);
        }

        // Activation moves nothing held for the radio.
        TransmitModel& tx = remote.transmitModel();
        const MicSource source = tx.micSource();
        const int micGain = tx.micGainDb();
        for (QRadioButton* button : page->micSourceGroup()->findChildren<QRadioButton*>()) {
            button->click();
        }
        QTest::keyClick(page->micGainSlider(), Qt::Key_Right);
        QCOMPARE(tx.micSource(), source);
        QCOMPARE(tx.micGainDb(), micGain);

        // The microphone choice is saved to this computer's audio/TxInput.
        QSlider* const buffer = page->bufferSlider();
        const int next = (buffer->value() + 1) % (buffer->maximum() + 1);
        buffer->setValue(next);
        const int samples = AudioTxInputPage::kBufferSizes.at(next);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("audio/TxInput/BufferSamples"))
                     .toString(),
                 QString::number(samples));
        QCOMPARE(remote.localAudioDevices()->txInputConfig().bufferSamples, samples);

        // A Core that permits transmit lifts the held controls' gate.
        dialog.setTransmitPermitted(true);
        QVERIFY(page->micSourceGroup()->isEnabled());
        QVERIFY(page->micGainSlider()->isEnabled());
        QVERIFY(page->micGainSlider()->toolTip() != txReason);
        dialog.setTransmitPermitted(false, txReason);
        QVERIFY(!page->micSourceGroup()->isEnabled());
        QVERIFY(page->bufferSlider()->isEnabled());

        // Local direct mode: every control live.
        RadioModel local;
        SetupDialog localDialog(&local);
        localDialog.selectPage(QStringLiteral("TX Input"));
        QWidget* const localContainer = localDialog.realizedPageForTest(QStringLiteral("TX Input"));
        QVERIFY(localContainer != nullptr && localContainer->isEnabled());
        auto* const localPage = localContainer->findChild<AudioTxInputPage*>();
        QVERIFY(localPage != nullptr);
        QVERIFY(localPage->micSourceGroup()->isEnabled());
        QVERIFY(localPage->micGainSlider()->isEnabled());
        QVERIFY(localPage->micGainSlider()->toolTip() != txReason);
    }

    // Local direct mode never runs the local-DSP gate: no Audio page is
    // disabled and the notice never shows.
    void localSetupNeverShowsTheLocalUnavailableNotice()
    {
        RadioModel local;
        SetupDialog dialog(&local);
        auto* const localNotice = dialog.findChild<QLabel*>(QStringLiteral("setupLocalUnavailable"));
        QVERIFY(localNotice != nullptr);
        for (const char* label : {"Devices", "TX Input", "VAX", "TCI", "Advanced"}) {
            const QString name = QString::fromLatin1(label);
            dialog.selectPage(name);
            QWidget* const page = dialog.realizedPageForTest(name);
            QVERIFY(page != nullptr);
            QVERIFY2(page->isEnabled(), label);
            QVERIFY2(page->toolTip().isEmpty(), label);
            QVERIFY2(localNotice->isHidden(), label);
        }
    }

    // ====================================================================
    // R-R3-21 / R-R3-10 / R-R3-17: Setup in a disconnected remote window.
    //
    // The per-page table. MainWindow pushes "Connect to the Core to change
    // these." while the window has no live session with the Core's
    // settings. ThisComputer pages stay usable; Core pages are disabled
    // with that reason (and, before the Core's settings ever arrived, are
    // not built at all); Mixed pages disable exactly their Core controls.
    // Realizing every page writes nothing towards the Core, not even an
    // edit the proxy would hold for later.
    // ====================================================================
    void disconnectedRemoteSetupPerPageTable()
    {
        SettingsProxy proxy;  // never ready, no snapshot: never connected
        AppSettings::instance().setRemoteBackend(&proxy);
        QSignalSpy writes(&proxy, &SettingsProxy::outboundWriteRequested);
        QSignalSpy removes(&proxy, &SettingsProxy::outboundRemoveRequested);

        RadioModel remote(RadioModel::Role::Remote);
        // The model's own constructors seed a few station keys while the
        // proxy is not ready (BandPlanManager's "BandPlanName"; see
        // SettingsProxy.h, "ready()==false is load-bearing"). That is not
        // Setup's doing, so it is the baseline the pages must not add to.
        const QSet<QString> seededByTheModel = proxy.droppedWhileOffline();
        SetupDialog dialog(&remote);
        // The dialog reads the same predicate MainWindow pushes from.
        QVERIFY(!dialog.stationSettingsAvailableForTest());
        dialog.setTransmitPermitted(false, QStringLiteral("Remote transmit is unavailable"));
        dialog.setStationSettingsAvailable(false, kStationReason);
        QVERIFY(OperatorWording::isPlain(kStationReason));

        auto* const notice = dialog.findChild<QLabel*>(QStringLiteral("setupStationUnavailable"));
        QVERIFY(notice != nullptr);

        // Mixed pages: how many Core controls each disables. A Mixed page
        // not listed is already unavailable as a whole in a remote window
        // (VAX, Advanced: local-DSP gate). Filter Presets, Spectrum Peaks,
        // Waterfall Defaults, 3D View and Export / Import have no Core
        // controls and are ThisComputer (R3 Setup fix wave, final review I4).
        const QMap<QString, int> coreControls{
            {QStringLiteral("Options"), 1},             // General: Region
            {QStringLiteral("Spectrum Defaults"), 5},   // FFT size, window, Hz/bin, fps x2
            {QStringLiteral("Grid & Scales"), 3},       // dB max, dB min, copy
            {QStringLiteral("Multimeter"), 1},          // sample interval
            {QStringLiteral("TX Display"), 9},          // TX analyzer
            {QStringLiteral("Settings Validation"), 2}, // Reset, Forget
        };
        const QStringList wholePageLocalDsp{QStringLiteral("VAX"), QStringLiteral("Advanced")};

        const QStringList labels = dialog.pageLabelsForTest();
        int thisComputer = 0;
        int core = 0;
        int mixed = 0;
        for (int i = 0; i < dialog.registeredPageCountForTest(); ++i) {
            const QString& label = labels.at(i);
            QWidget* const page = showSetupLeafAt(dialog, i);
            if (page == nullptr) { continue; }  // hidden PA leaves still realize; none yield null
            const QList<QWidget*> gated = controlsGatedWith(page, kStationReason);
            switch (dialog.pageScopeAtForTest(i)) {
            case SetupScope::ThisComputer:
                ++thisComputer;
                QVERIFY2(page->isEnabled(), qPrintable(label));
                QVERIFY2(notice->isHidden(), qPrintable(label));
                QVERIFY2(gated.isEmpty(), qPrintable(label));
                break;
            case SetupScope::Core:
                ++core;
                QVERIFY2(!page->isEnabled(), qPrintable(label));
                // Never connected: a stand-in, not a page built from this
                // computer's ship defaults.
                QCOMPARE(page->objectName(), QStringLiteral("setupStationPlaceholder"));
                QVERIFY2(!notice->isHidden(), qPrintable(label));
                QCOMPARE(notice->text(), kStationReason);
                QCOMPARE(page->toolTip(), kStationReason);
                QCOMPARE(setupLeafAt(dialog, i)->toolTip(0), kStationReason);
                break;
            case SetupScope::Mixed: {
                ++mixed;
                QVERIFY2(notice->isHidden(), qPrintable(label));
                QCOMPARE(page->objectName() == QStringLiteral("setupStationPlaceholder"), false);
                if (wholePageLocalDsp.contains(label)) {
                    QVERIFY2(!page->isEnabled(), qPrintable(label));
                    break;
                }
                QVERIFY2(page->isEnabled(), qPrintable(label));
                if (label == QStringLiteral("TX Input")) {
                    // The controls held for the radio: the Core reason wins
                    // over the transmit reason while disconnected.
                    auto* const txInput = page->findChild<AudioTxInputPage*>();
                    QVERIFY(txInput != nullptr);
                    for (QWidget* held : {static_cast<QWidget*>(txInput->micSourceGroup()),
                                          static_cast<QWidget*>(txInput->micGainSlider())}) {
                        QVERIFY(!held->isEnabled());
                        QCOMPARE(held->toolTip(), kStationReason);
                    }
                    QVERIFY(txInput->deviceCombo()->isEnabled());
                    QVERIFY(txInput->testMicButton()->isEnabled());
                    QVERIFY(gated.size() >= 2);
                    break;
                }
                QVERIFY2(gated.size() == coreControls.value(label, 0),
                         qPrintable(QStringLiteral("%1: %2 controls gated, expected %3")
                                        .arg(label).arg(gated.size())
                                        .arg(coreControls.value(label, 0))));
                for (QWidget* control : gated) {
                    QVERIFY2(!control->isEnabled(), qPrintable(label));
                    QCOMPARE(control->toolTip(), kStationReason);
                }
                break;
            }
            }
        }
        QVERIFY(thisComputer >= 20);
        QVERIFY(core >= 25);
        QVERIFY(mixed >= 9);

        // Nothing towards the Core: nothing sent, and nothing held as an
        // edit to be reported as lost on the next connect.
        QCOMPARE(writes.size(), 0);
        QCOMPARE(removes.size(), 0);
        const QSet<QString> heldByPages = proxy.droppedWhileOffline() - seededByTheModel;
        QStringList held(heldByPages.cbegin(), heldByPages.cend());
        held.sort();
        QVERIFY2(held.isEmpty(), qPrintable(held.join(QStringLiteral(", "))));
    }

    // R-R3-21: once the Core's settings arrive, a page realized before them
    // (or while disconnected) is rebuilt and shows the Core's values; a
    // later snapshot on the live session rebuilds it again; losing the Core
    // disables its pages again without rebuilding anything.
    void reconnectRebuildsSetupPagesFromTheCoresValues()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setStationSettingsAvailable(false, kStationReason);
        auto* const stack = dialog.findChild<QStackedWidget*>();

        // Before any snapshot: the Mixed pages show this computer's
        // defaults for the Core's settings (disabled), the Core page is a
        // stand-in.
        dialog.selectPage(QStringLiteral("Options"));   // General
        QPointer<QWidget> oldOptions = dialog.realizedPageForTest(QStringLiteral("Options"));
        QVERIFY(oldOptions);
        auto* region = oldOptions->findChild<QComboBox*>(QStringLiteral("comboFRSRegion"));
        QVERIFY(region != nullptr);
        QCOMPARE(region->currentText(), QStringLiteral("United States"));
        QVERIFY(!region->isEnabled());
        dialog.selectPage(QStringLiteral("NB/SNB"));
        QVERIFY(dialog.isPagePlaceholderForTest(QStringLiteral("NB/SNB")));
        QPointer<QWidget> oldNb = dialog.realizedPageForTest(QStringLiteral("NB/SNB"));
        dialog.selectPage(QStringLiteral("Multimeter"));
        QPointer<QWidget> oldMultimeter = dialog.realizedPageForTest(QStringLiteral("Multimeter"));
        QList<QWidget*> delay = controlsGatedWith(oldMultimeter, kStationReason);
        QCOMPARE(delay.size(), 1);
        QCOMPARE(qobject_cast<QSpinBox*>(delay.first())->value(), 100);

        // Connected: the snapshot, then ready, then (in a later event, when
        // the session reports itself established) MainWindow's push. The
        // snapshot's own queued rebuild runs first and finds the settings
        // still unavailable, so it is the push that rebuilds.
        proxy.applySnapshot({{QLatin1String(AppSettings::kDaemonProfileSeededKey), QStringLiteral("True")},
                             {QStringLiteral("Region"), QStringLiteral("Japan")},
                             {QStringLiteral("MultimeterDelayMs"), QStringLiteral("250")}});
        proxy.setReady(true);
        QCoreApplication::processEvents();
        QVERIFY(oldOptions && oldNb && oldMultimeter);
        dialog.setStationSettingsAvailable(true, kStationReason);
        QTRY_VERIFY(!oldOptions && !oldNb && !oldMultimeter);

        QWidget* const options = dialog.realizedPageForTest(QStringLiteral("Options"));
        region = options->findChild<QComboBox*>(QStringLiteral("comboFRSRegion"));
        QCOMPARE(region->currentText(), QStringLiteral("Japan"));
        QVERIFY(region->isEnabled());
        QVERIFY(!dialog.isPagePlaceholderForTest(QStringLiteral("NB/SNB")));
        QWidget* const nb = dialog.realizedPageForTest(QStringLiteral("NB/SNB"));
        QVERIFY(nb->isEnabled());
        QVERIFY(nb->toolTip().isEmpty());
        QVERIFY(setupLeaf(dialog, QStringLiteral("NB/SNB"))->toolTip(0).isEmpty());
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("setupStationUnavailable"))->isHidden());
        QWidget* const multimeter = dialog.realizedPageForTest(QStringLiteral("Multimeter"));
        // The page on screen is still the one the operator was looking at.
        QCOMPARE(stack->currentWidget(), multimeter);
        QVERIFY(controlsGatedWith(multimeter, kStationReason).isEmpty());
        int delayMs = -1;
        for (QSpinBox* spin : multimeter->findChildren<QSpinBox*>()) {
            if (spin->value() == 250) { delayMs = spin->value(); }
        }
        QCOMPARE(delayMs, 250);

        // A later snapshot on the same live session (a Core whose radio
        // came online sends one): rebuilt again, queued.
        QPointer<QWidget> connectedOptions = options;
        proxy.applySnapshot({{QStringLiteral("Region"), QStringLiteral("Italy")}});
        QTRY_VERIFY(!connectedOptions);
        region = dialog.realizedPageForTest(QStringLiteral("Options"))
                     ->findChild<QComboBox*>(QStringLiteral("comboFRSRegion"));
        QCOMPARE(region->currentText(), QStringLiteral("Italy"));

        // The Core goes away: nothing is rebuilt, the Core's pages and
        // controls are disabled with the reason, and they keep showing the
        // Core's last values.
        QPointer<QWidget> nbAfter = dialog.realizedPageForTest(QStringLiteral("NB/SNB"));
        QPointer<QWidget> optionsAfter = dialog.realizedPageForTest(QStringLiteral("Options"));
        proxy.setReady(false);
        dialog.setStationSettingsAvailable(false, kStationReason);
        QCoreApplication::processEvents();
        QVERIFY(nbAfter && optionsAfter);
        QVERIFY(!nbAfter->isEnabled());
        QCOMPARE(nbAfter->toolTip(), kStationReason);
        QVERIFY(!region->isEnabled());
        QCOMPARE(region->currentText(), QStringLiteral("Italy"));
        dialog.selectPage(QStringLiteral("NB/SNB"));
        QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("setupStationUnavailable"))->text(),
                 kStationReason);
        QVERIFY(!dialog.findChild<QLabel*>(QStringLiteral("setupStationUnavailable"))->isHidden());
    }

    // R-R3-21: a page whose construction pushes availability again (the
    // push can arrive from anywhere on the main thread) does not start a
    // second rebuild from inside its own construction: every page is built
    // once per snapshot, one at a time. A return of availability without a
    // new snapshot rebuilds nothing.
    void setupPageRebuildIsGuardedAgainstReentry()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setStationSettingsAvailable(false, kStationReason);

        int builds = 0;
        int building = 0;
        bool nested = false;
        const auto probe = [&] {
            ++builds;
            nested = nested || building > 0;
            ++building;
            dialog.setStationSettingsAvailable(false, kStationReason);
            dialog.setStationSettingsAvailable(true, kStationReason);
            --building;
            return new QWidget;
        };
        const int first = dialog.registerPageForTest(QStringLiteral("Probe one"),
                                                     SetupScope::Mixed, probe);
        const int second = dialog.registerPageForTest(QStringLiteral("Probe two"),
                                                      SetupScope::Mixed, probe);
        dialog.setStationSettingsAvailable(false, kStationReason);
        QVERIFY(dialog.realizePageAtForTest(first) != nullptr);
        dialog.setStationSettingsAvailable(false, kStationReason);
        QVERIFY(dialog.realizePageAtForTest(second) != nullptr);
        QCOMPARE(builds, 2);

        // A new snapshot while unavailable, then availability returns: both
        // are rebuilt, each once, neither inside the other.
        dialog.setStationSettingsAvailable(false, kStationReason);
        proxy.applySnapshot({{QStringLiteral("Region"), QStringLiteral("Italy")}});
        dialog.setStationSettingsAvailable(true, kStationReason);
        QCOMPARE(builds, 4);
        QVERIFY(!nested);
        QTest::qWait(50);  // the snapshot's queued rebuild finds nothing stale
        QCOMPARE(builds, 4);

        dialog.setStationSettingsAvailable(false, kStationReason);
        dialog.setStationSettingsAvailable(true, kStationReason);
        QCoreApplication::processEvents();
        QCOMPARE(builds, 4);  // no new snapshot since they were built
    }

    // R3 Setup fix wave (final review I2): connected to a Core whose
    // snapshot was empty and carried no seed marker, the session is ready
    // but the Core's settings have not arrived. No Core page is built (its
    // constructor would send this computer's defaults to the Core), nothing
    // is sent while every page is shown, and the reason says what is true.
    void coreSetupPagesWaitForTheCoresSettingsNotAnySnapshot()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        proxy.applySnapshot({});
        proxy.setReady(true);
        QVERIFY(proxy.hasReceivedSnapshot());
        QVERIFY(!proxy.setupDialogAllowed());
        QSignalSpy writes(&proxy, &SettingsProxy::outboundWriteRequested);
        QSignalSpy removes(&proxy, &SettingsProxy::outboundRemoveRequested);

        SetupDialog dialog(&remote);
        QVERIFY(!dialog.stationSettingsAvailableForTest());
        dialog.setStationSettingsAvailable(false, kCoreSettingsMissingReason);
        QVERIFY(OperatorWording::isPlain(kCoreSettingsMissingReason));
        int probeBuilds = 0;
        const int probe = dialog.registerPageForTest(
            QStringLiteral("Core probe"), SetupScope::Core,
            [&probeBuilds] { ++probeBuilds; return new QWidget; });

        auto* const notice = dialog.findChild<QLabel*>(QStringLiteral("setupStationUnavailable"));
        QVERIFY(notice != nullptr);
        const QStringList labels = dialog.pageLabelsForTest();
        int core = 0;
        for (int i = 0; i < dialog.registeredPageCountForTest(); ++i) {
            QWidget* const page = showSetupLeafAt(dialog, i);
            if (page == nullptr || dialog.pageScopeAtForTest(i) != SetupScope::Core) {
                continue;
            }
            ++core;
            QVERIFY2(page->objectName() == QStringLiteral("setupStationPlaceholder"),
                     qPrintable(labels.at(i)));
            QVERIFY2(!page->isEnabled(), qPrintable(labels.at(i)));
            QCOMPARE(notice->text(), kCoreSettingsMissingReason);
            QVERIFY2(!notice->isHidden(), qPrintable(labels.at(i)));
        }
        QVERIFY(core >= 25);
        QCOMPARE(probeBuilds, 0);
        QVERIFY(dialog.realizePageAtForTest(probe) != nullptr);
        QCOMPARE(probeBuilds, 0);
        QCOMPARE(writes.size(), 0);
        QCOMPARE(removes.size(), 0);

        // The Core's settings arrive: the stand-ins become the real pages.
        connectDialog(proxy, dialog);
        QCOMPARE(probeBuilds, 1);
        QVERIFY(!dialog.isPagePlaceholderForTest(QStringLiteral("NB/SNB")));
    }

    // R3 Setup fix wave (final review I1): a page is never rebuilt under
    // its own open dialog. A snapshot that arrives while Settings
    // Validation's question is open leaves the page (and the question)
    // alone; the page is rebuilt once the question has closed.
    void setupPageIsNotRebuiltUnderItsOwnOpenDialog()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        connectDialog(proxy, dialog, {{QStringLiteral("Region"), QStringLiteral("Japan")}});

        const QString label = QStringLiteral("Settings Validation");
        dialog.selectPage(label);
        QPointer<QWidget> page = dialog.realizedPageForTest(label);
        QVERIFY(page);
        QPushButton* const forget = buttonWithText(page, QStringLiteral("Forget This Radio"));
        QVERIFY(forget != nullptr && forget->isEnabled());

        bool boxOpen = false;
        bool pageKeptWhileOpen = false;
        QTimer::singleShot(0, this, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            boxOpen = box != nullptr;
            proxy.applySnapshot({{QStringLiteral("Region"), QStringLiteral("Italy")}});
            QTest::qWait(100);  // the snapshot's queued rebuild runs in the box's loop
            pageKeptWhileOpen = !page.isNull() && dialog.realizedPageForTest(label) == page;
            if (box != nullptr) {
                box->button(QMessageBox::No)->click();
            }
        });
        forget->click();  // returns when the question is answered
        QVERIFY(boxOpen);
        QVERIFY(pageKeptWhileOpen);
        // Rebuilt once the question has gone.
        QTRY_VERIFY(page.isNull());
        QWidget* const rebuilt = dialog.realizedPageForTest(label);
        QVERIFY(rebuilt != nullptr);
        QCOMPARE(dialog.findChild<QStackedWidget*>()->currentWidget(), rebuilt);
    }

    // R3 Setup fix wave (final review M2): the link drops while Settings
    // Validation's Forget question is open. Yes then writes nothing, not
    // even an edit held for the next connect.
    void settingsValidationYesAfterTheLinkDropsWritesNothing()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        connectDialog(proxy, dialog);
        const QString label = QStringLiteral("Settings Validation");
        dialog.selectPage(label);
        QWidget* const page = dialog.realizedPageForTest(label);
        QVERIFY(page != nullptr);
        QSignalSpy writes(&proxy, &SettingsProxy::outboundWriteRequested);
        QSignalSpy removes(&proxy, &SettingsProxy::outboundRemoveRequested);
        const QSet<QString> heldBefore = proxy.droppedWhileOffline();

        for (const QString& text : {QStringLiteral("Forget This Radio"),
                                    QStringLiteral("Reset to Defaults")}) {
            QPushButton* const button = buttonWithText(page, text);
            QVERIFY(button != nullptr);
            proxy.setReady(true);
            dialog.setStationSettingsAvailable(true, kStationReason);
            QVERIFY(button->isEnabled());
            bool answered = false;
            QTimer::singleShot(0, this, [&] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (box == nullptr) { return; }
                // The link drops: the session is no longer ready and
                // MainWindow pushes the change to the open dialog.
                proxy.setReady(false);
                dialog.setStationSettingsAvailable(false, kStationReason);
                box->button(QMessageBox::Yes)->click();
                answered = true;
            });
            button->click();
            QVERIFY2(answered, qPrintable(text));
        }
        QCOMPARE(writes.size(), 0);
        QCOMPARE(removes.size(), 0);
        const QSet<QString> held = proxy.droppedWhileOffline() - heldBefore;
        QVERIFY2(held.isEmpty(),
                 qPrintable(QStringList(held.cbegin(), held.cend()).join(QStringLiteral(", "))));
    }

    // R3 Setup fix wave (final review M3): a page whose factory yields
    // nothing on a rebuild keeps the page it had instead of losing its
    // entry until Setup is reopened.
    void setupPageRebuildThatYieldsNothingKeepsThePage()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        connectDialog(proxy, dialog);

        int builds = 0;
        const int probe = dialog.registerPageForTest(
            QStringLiteral("Failing probe"), SetupScope::Mixed, [&builds]() -> QWidget* {
                ++builds;
                return builds == 1 ? new QWidget : nullptr;
            });
        QPointer<QWidget> first = dialog.realizePageAtForTest(probe);
        QVERIFY(first);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("page factory yielded nothing")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("rebuilding Setup page.*yielded nothing")));
        proxy.applySnapshot({{QStringLiteral("Region"), QStringLiteral("Italy")}});
        QTRY_COMPARE(builds, 2);
        QCoreApplication::processEvents();
        QVERIFY(first);
        QCOMPARE(dialog.realizePageAtForTest(probe), first.data());
        QCOMPARE(showSetupLeafAt(dialog, probe), first.data());
    }

    // R3 Setup fix wave (final review M5): VAX and Advanced stay disabled
    // by the local-DSP gate for the whole remote session, so a new snapshot
    // does not rebuild them (or log the gate's warning again).
    void pagesTheLocalDspGateDisablesAreNotRebuilt()
    {
        SettingsProxy proxy;
        AppSettings::instance().setRemoteBackend(&proxy);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        connectDialog(proxy, dialog);

        QMap<QString, QPointer<QWidget>> pages;
        for (const QString& label : {QStringLiteral("VAX"), QStringLiteral("Advanced")}) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
                QStringLiteral("Setup page.*reached local DSP on a remote-station model")));
            dialog.selectPage(label);
            pages.insert(label, dialog.realizedPageForTest(label));
            QVERIFY2(pages.value(label) && !pages.value(label)->isEnabled(), qPrintable(label));
        }
        // Another page is rebuilt by the same snapshot, so the rebuild ran.
        dialog.selectPage(QStringLiteral("Multimeter"));
        QPointer<QWidget> multimeter = dialog.realizedPageForTest(QStringLiteral("Multimeter"));
        proxy.applySnapshot({{QStringLiteral("Region"), QStringLiteral("Italy")}});
        QTRY_VERIFY(multimeter.isNull());
        for (auto it = pages.cbegin(); it != pages.cend(); ++it) {
            QVERIFY2(it.value(), qPrintable(it.key()));
            QCOMPARE(dialog.realizedPageForTest(it.key()), it.value().data());
        }
    }

    // Local direct mode: the push has no effect. Core pages are built and
    // live, Mixed pages gate nothing, no notice.
    void localSetupIgnoresStationSettingsAvailability()
    {
        RadioModel local;
        SetupDialog dialog(&local);
        QVERIFY(dialog.stationSettingsAvailableForTest());
        dialog.setStationSettingsAvailable(false, kStationReason);
        for (const char* label : {"NB/SNB", "Multimeter", "Options", "Spectrum Defaults"}) {
            const QString name = QString::fromLatin1(label);
            dialog.selectPage(name);
            QWidget* const page = dialog.realizedPageForTest(name);
            QVERIFY2(page != nullptr, label);
            QVERIFY2(page->isEnabled(), label);
            QVERIFY2(!dialog.isPagePlaceholderForTest(name), label);
            QVERIFY2(controlsGatedWith(page, kStationReason).isEmpty(), label);
            QVERIFY2(dialog.findChild<QLabel*>(QStringLiteral("setupStationUnavailable"))->isHidden(),
                     label);
        }
    }

    // ====================================================================
    // R-R3-21: Setup leaves declared unavailable in a remote session.
    //
    // Hardware Config and DDC Routing act on this computer's own radio
    // connection: on a remote model currentRadioChanged never fires, so
    // HardwarePage never learns a MAC and drops every edit. RF-Kit
    // connects this computer's own amplifier socket. None reaches local
    // DSP, so the resource audit does not catch them; they are declared.
    // ====================================================================
    void remoteDeclaredUnavailableSetupLeavesSayWhy_data()
    {
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("reasonWord");
        QTest::newRow("Hardware Config") << QStringLiteral("Hardware Config")
                                         << QStringLiteral("hardware");
        QTest::newRow("DDC Routing") << QStringLiteral("DDC Routing")
                                     << QStringLiteral("hardware");
        QTest::newRow("RF-Kit") << QStringLiteral("RF-Kit") << QStringLiteral("Amplifier");
    }

    void remoteDeclaredUnavailableSetupLeavesSayWhy()
    {
        QFETCH(QString, label);
        QFETCH(QString, reasonWord);

        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);

        // The leaf says why before the page is ever opened.
        QTreeWidgetItem* const leaf = setupLeaf(dialog, label);
        QVERIFY(leaf != nullptr);
        const QString reason = leaf->toolTip(0);
        QVERIFY2(reason.contains(reasonWord), qPrintable(reason));
        QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
        QVERIFY(!dialog.isPageRealizedForTest(label));

        dialog.selectPage(label);
        QWidget* const page = dialog.realizedPageForTest(label);
        QVERIFY(page != nullptr);
        QVERIFY(!page->isEnabled());
        QCOMPARE(page->toolTip(), reason);
        auto* const notice = dialog.findChild<QLabel*>(QStringLiteral("setupLocalUnavailable"));
        QVERIFY(notice != nullptr);
        QVERIFY(!notice->isHidden());
        QCOMPARE(notice->text(), reason);

        // Not a transmit gate: a Core that permits transmit changes nothing.
        dialog.setTransmitPermitted(true);
        QVERIFY(!page->isEnabled());
        QVERIFY(!notice->isHidden());

        // Local direct mode: live, no reason.
        RadioModel local;
        SetupDialog localDialog(&local);
        QTreeWidgetItem* const localLeaf = setupLeaf(localDialog, label);
        QVERIFY(localLeaf != nullptr);
        QVERIFY(localLeaf->toolTip(0).isEmpty());
        localDialog.selectPage(label);
        QWidget* const localPage = localDialog.realizedPageForTest(label);
        QVERIFY(localPage != nullptr);
        QVERIFY(localPage->isEnabled());
        QVERIFY(localDialog.findChild<QLabel*>(
                    QStringLiteral("setupLocalUnavailable"))->isHidden());
    }

    // The PA category is not shown in a remote session: a remote model has
    // no hardware profile, so its capabilities are the Unknown board's and
    // hasPaProfile is false (SetupDialog::applyPaVisibility). The inventory
    // records it as unavailable by absence; this pins that.
    void remotePaCategoryIsNotShown()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        auto* tree = dialog.findChild<QTreeWidget*>();
        QVERIFY(tree != nullptr);
        QTreeWidgetItem* pa = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->text(0) == QStringLiteral("PA")) {
                pa = tree->topLevelItem(i);
            }
        }
        QVERIFY(pa != nullptr);
        QVERIFY(pa->isHidden());
    }

    // General > Options keeps its Region and Options groups; only the two
    // attenuator groups, which drive the unwired local step attenuator,
    // are unavailable.
    void remoteGeneralOptionsDisablesOnlyTheAttenuatorGroups()
    {
        RadioModel remote(RadioModel::Role::Remote);
        GeneralOptionsPage page(&remote);
        // A remote model builds no step attenuator controller, so what must
        // not move is the widgets' own state (and so anything they persist).
        QVERIFY(remote.stepAttController() == nullptr);
        for (const char* name : {"grpStepAttenuator", "grpAutoAttRx1", "grpAutoAttRx2"}) {
            auto* group = page.findChild<QGroupBox*>(QLatin1String(name));
            QVERIFY2(group != nullptr, name);
            QVERIFY2(!group->isEnabled(), name);
            QVERIFY2(OperatorWording::isPlain(group->toolTip()), qPrintable(group->toolTip()));
            // Activation reaches nothing.
            const QString before = widgetStateOf(group);
            QVERIFY2(!before.isEmpty(), name);
            for (QAbstractButton* button : group->findChildren<QAbstractButton*>()) {
                button->click();
            }
            for (QSpinBox* spin : group->findChildren<QSpinBox*>()) {
                QTest::keyClick(spin, Qt::Key_Up);
            }
            for (QComboBox* combo : group->findChildren<QComboBox*>()) {
                QTest::keyClick(combo, Qt::Key_Down);
            }
            QCOMPARE(widgetStateOf(group), before);
        }
        auto* hardware = page.findChild<QGroupBox*>(QStringLiteral("grpHardwareConfig"));
        QVERIFY(hardware != nullptr);
        QVERIFY(hardware->isEnabled());

        RadioModel local;
        GeneralOptionsPage localPage(&local);
        auto* localStepAtt = localPage.findChild<QGroupBox*>(QStringLiteral("grpStepAttenuator"));
        QVERIFY(localStepAtt != nullptr);
        QVERIFY(localStepAtt->isEnabled());
        QVERIFY(localStepAtt->toolTip().isEmpty());
    }

    // ====================================================================
    // R-R3-21: applet and flag controls the inventory found acting on this
    // computer's own radio connection, amplifier socket or VAX buses.
    // ====================================================================
    void remoteRxAppletAttenuatorRowIsUnavailable()
    {
        RadioModel remote(RadioModel::Role::Remote);
        RxApplet applet(nullptr, &remote);
        auto* att = applet.findChild<QWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(att != nullptr);
        QVERIFY(!att->isEnabled());
        QVERIFY2(OperatorWording::isPlain(att->toolTip()), qPrintable(att->toolTip()));
        QVERIFY(att->toolTip().contains(QStringLiteral("attenuator")));

        // Activation reaches nothing: the step spin, the preamp combo and
        // (where the board shows it) the RX1 preamp toggle.
        QVERIFY(remote.stepAttController() == nullptr);
        const QString attBefore = widgetStateOf(&applet);
        auto* spin = att->findChild<QSpinBox*>();
        auto* combo = att->findChild<QComboBox*>();
        QVERIFY(spin != nullptr);
        QVERIFY(combo != nullptr);
        QVERIFY(!spin->isEnabled());
        QVERIFY(!combo->isEnabled());
        const int spinBefore = spin->value();
        const int comboBefore = combo->currentIndex();
        QTest::keyClick(spin, Qt::Key_Up);
        QTest::keyClick(combo, Qt::Key_Down);
        if (auto* preamp = applet.findChild<QCheckBox*>(QStringLiteral("RxRx1PreampToggle"))) {
            QVERIFY(!preamp->isEnabled());
            preamp->click();
        }
        QCOMPARE(spin->value(), spinBefore);
        QCOMPARE(combo->currentIndex(), comboBefore);
        QCOMPARE(widgetStateOf(&applet), attBefore);

        RadioModel local;
        RxApplet localApplet(nullptr, &local);
        auto* localAtt = localApplet.findChild<QWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(localAtt != nullptr);
        QVERIFY(localAtt->isEnabled());
        QVERIFY(localAtt->toolTip().isEmpty());
    }

    // The RX applet's XIT row offsets the transmit frequency, so it takes
    // the transmit permission the VFO flag's XIT takes. A remote model
    // starts denied; clicking writes nothing; permission restores it.
    void remoteRxAppletXitFollowsTheTransmitPermission()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SliceModel slice(0);
        RxApplet applet(&slice, &remote);
        QPushButton* xit = nullptr;
        for (QPushButton* b : applet.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("XIT")) { xit = b; }
        }
        QVERIFY(xit != nullptr);
        QVERIFY(!xit->isEnabled());
        QVERIFY(xit->toolTip().contains(QStringLiteral("transmit")));

        QSignalSpy xitChanged(&slice, &SliceModel::xitEnabledChanged);
        xit->click();
        QCOMPARE(xitChanged.count(), 0);
        QVERIFY(!slice.xitEnabled());

        const QString reason = QStringLiteral("Remote transmit is unavailable");
        applet.setTransmitPermitted(false, reason);
        QCOMPARE(xit->toolTip(), reason);
        applet.setTransmitPermitted(true);
        QVERIFY(xit->isEnabled());
        QVERIFY(xit->toolTip() != reason);

        RadioModel local;
        SliceModel localSlice(0);
        RxApplet localApplet(&localSlice, &local);
        for (QPushButton* b : localApplet.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("XIT")) {
                QVERIFY(b->isEnabled());
                b->click();
            }
        }
        QVERIFY(localSlice.xitEnabled());
    }

    void remoteVaxSurfacesAreUnavailable()
    {
        const QString vaxWord = QStringLiteral("VAX");
        RadioModel remote(RadioModel::Role::Remote);
        RadioModel local;

        // VAX applet: the whole applet.
        VaxApplet remoteApplet(&remote, remote.audioEngine());
        QVERIFY(!remoteApplet.isEnabled());
        QVERIFY(remoteApplet.toolTip().contains(vaxWord));
        QVERIFY2(OperatorWording::isPlain(remoteApplet.toolTip()),
                 qPrintable(remoteApplet.toolTip()));
        VaxApplet localApplet(&local, local.audioEngine());
        QVERIFY(localApplet.isEnabled());

        // Activation on the remote applet reaches no VAX bus.
        {
            QSignalSpy muted(remote.audioEngine(), &AudioEngine::vaxMutedChanged);
            QSignalSpy rxGain(remote.audioEngine(), &AudioEngine::vaxRxGainChanged);
            QSignalSpy txGain(remote.audioEngine(), &AudioEngine::vaxTxGainChanged);
            int buttons = 0;
            for (QAbstractButton* button : remoteApplet.findChildren<QAbstractButton*>()) {
                ++buttons;
                button->click();
            }
            QVERIFY(buttons > 0);
            for (QWidget* w : remoteApplet.findChildren<QWidget*>()) {
                QTest::keyClick(w, Qt::Key_Up);
            }
            QCOMPARE(muted.count(), 0);
            QCOMPARE(rxGain.count(), 0);
            QCOMPARE(txGain.count(), 0);
            for (int ch = 1; ch <= 4; ++ch) {
                QVERIFY(!remote.audioEngine()->vaxMuted(ch));
            }
        }

        // VFO flag's VAX tab selector.
        VfoWidget remoteFlag;
        remoteFlag.setRadioModel(&remote);
        auto* selector = remoteFlag.findChild<VaxChannelSelector*>();
        QVERIFY(selector != nullptr);
        QVERIFY(!selector->isEnabled());
        QCOMPARE(selector->toolTip(), remoteApplet.toolTip());
        {
            QSignalSpy picked(selector, &VaxChannelSelector::valueChanged);
            const int before = selector->value();
            for (QAbstractButton* button : selector->findChildren<QAbstractButton*>()) {
                QTest::mouseClick(button, Qt::LeftButton);
            }
            QCOMPARE(picked.count(), 0);
            QCOMPARE(selector->value(), before);
        }
        VfoWidget localFlag;
        localFlag.setRadioModel(&local);
        auto* localSelector = localFlag.findChild<VaxChannelSelector*>();
        QVERIFY(localSelector != nullptr);
        QVERIFY(localSelector->isEnabled());
        {
            // Non-vacuity: the same clicks do pick a channel locally.
            QSignalSpy picked(localSelector, &VaxChannelSelector::valueChanged);
            for (QAbstractButton* button : localSelector->findChildren<QAbstractButton*>()) {
                QTest::mouseClick(button, Qt::LeftButton);
            }
            QVERIFY(picked.count() > 0);
        }

        // Spectrum overlay VAX flyout, bound to a slice.
        SliceModel slice(0);
        QWidget host;
        auto* panel = new SpectrumOverlayPanel(&host);
        panel->setSliceResolver([&slice]() { return &slice; });
        panel->setRadioModel(&remote);
        auto* combo = host.findChild<QComboBox*>(QStringLiteral("vaxCombo"));
        QVERIFY(combo != nullptr);
        QVERIFY(!combo->isEnabled());
        QCOMPARE(combo->toolTip(), remoteApplet.toolTip());
        {
            QSignalSpy vaxChanged(&slice, &SliceModel::vaxChannelChanged);
            const int before = slice.vaxChannel();
            QTest::keyClick(combo, Qt::Key_Down);
            QTest::keyClick(combo, Qt::Key_Down);
            QCOMPARE(vaxChanged.count(), 0);
            QCOMPARE(slice.vaxChannel(), before);
        }

        QWidget localHost;
        auto* localPanel = new SpectrumOverlayPanel(&localHost);
        localPanel->setSliceResolver([&slice]() { return &slice; });
        localPanel->setRadioModel(&local);
        auto* localCombo = localHost.findChild<QComboBox*>(QStringLiteral("vaxCombo"));
        QVERIFY(localCombo != nullptr);
        QVERIFY(localCombo->isEnabled());
    }

    void remoteAmplifierAppletControlsAreUnavailable()
    {
        RadioModel remote(RadioModel::Role::Remote);
        AmpApplet applet(&remote);
        QSignalSpy toggles(&applet, &AmpApplet::connectionToggleRequested);
        QSignalSpy operate(&applet, &AmpApplet::operateToggled);

        QPushButton* operateBtn = nullptr;
        for (QPushButton* b : applet.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("OPERATE")) { operateBtn = b; }
        }
        QVERIFY(operateBtn != nullptr);
        QVERIFY(!operateBtn->isEnabled());
        operateBtn->click();
        QCOMPARE(operate.count(), 0);

        std::unique_ptr<QMenu> menu(applet.buildContextMenuForTesting());
        QAction* toggle = nullptr;
        for (QAction* a : menu->actions()) {
            if (a->text() == QStringLiteral("Reconnect")
                || a->text() == QStringLiteral("Disconnect")) {
                toggle = a;
            }
        }
        QVERIFY(toggle != nullptr);
        QVERIFY(!toggle->isEnabled());
        QVERIFY2(OperatorWording::isPlain(toggle->toolTip()), qPrintable(toggle->toolTip()));
        QCOMPARE(toggle->toolTip(), operateBtn->toolTip());
        toggle->trigger();
        QCOMPARE(toggles.count(), 0);

        RadioModel local;
        AmpApplet localApplet(&local);
        std::unique_ptr<QMenu> localMenu(localApplet.buildContextMenuForTesting());
        QSignalSpy localToggles(&localApplet, &AmpApplet::connectionToggleRequested);
        for (QAction* a : localMenu->actions()) {
            if (a->text() == QStringLiteral("Reconnect")) {
                QVERIFY(a->isEnabled());
                a->trigger();
            }
        }
        QCOMPARE(localToggles.count(), 1);
    }

    // ====================================================================
    // R-R3-21 fix wave: transmit sections inside receive Setup pages.
    //
    // DSP > AGC/ALC carries the TX Leveler and TX ALC groups and DSP >
    // Options carries a TX combo per mode for buffer size, filter size and
    // filter type. The TX Leveler and TX ALC edits are not mirrored, so they
    // would land only in this window's TransmitModel; the DSP > Options TX
    // combos write station transmit settings (the DspOptions keys are
    // station-scoped). Each follows the transmit permission the rest of the
    // transmit surfaces follow. The pages stay live for their receive
    // halves.
    // ====================================================================
    void remoteAgcAlcTransmitGroupsFollowThePermission()
    {
        const QString reason = QStringLiteral("Remote transmit is unavailable");
        const auto findGroup = [](QWidget* page, const QString& title) {
            for (QGroupBox* group : page->findChildren<QGroupBox*>()) {
                if (group->title() == title) { return group; }
            }
            return static_cast<QGroupBox*>(nullptr);
        };

        RadioModel remote(RadioModel::Role::Remote);
        remote.addSliceWithStationId(0, QStringLiteral("pan-0"));
        QVERIFY(remote.activeSlice() != nullptr);
        SetupDialog dialog(&remote);
        dialog.setTransmitPermitted(false, reason);
        dialog.selectPage(QStringLiteral("AGC/ALC"));
        QWidget* const page = dialog.realizedPageForTest(QStringLiteral("AGC/ALC"));
        QVERIFY(page != nullptr);
        QVERIFY(page->isEnabled());
        QGroupBox* const leveler = findGroup(page, QStringLiteral("TX Leveler"));
        QGroupBox* const alc = findGroup(page, QStringLiteral("TX ALC"));
        QVERIFY(leveler != nullptr);
        QVERIFY(alc != nullptr);
        for (QGroupBox* group : {leveler, alc}) {
            QVERIFY2(!group->isEnabled(), qPrintable(group->title()));
            QCOMPARE(group->toolTip(), reason);
            QCOMPARE(group->accessibleDescription(), reason);
        }
        // The receive AGC controls on the same page stay live.
        for (QGroupBox* group : page->findChildren<QGroupBox*>()) {
            if (group != leveler && group != alc && !leveler->isAncestorOf(group)
                && !alc->isAncestorOf(group) && group->title().startsWith(QStringLiteral("AGC"))) {
                QVERIFY2(group->isEnabled(), qPrintable(group->title()));
            }
        }

        // Activation writes nothing.
        const TransmitModel& tx = remote.transmitModel();
        const bool levelerOn = tx.txLevelerOn();
        const int levelerMax = tx.txLevelerMaxGain();
        const int levelerDecay = tx.txLevelerDecay();
        const int alcMax = tx.txAlcMaxGain();
        const int alcDecay = tx.txAlcDecay();
        for (QGroupBox* group : {leveler, alc}) {
            for (QAbstractButton* button : group->findChildren<QAbstractButton*>()) {
                button->click();
            }
            for (QSpinBox* spin : group->findChildren<QSpinBox*>()) {
                QVERIFY(!spin->isEnabled());
                QTest::keyClick(spin, Qt::Key_Up);
            }
        }
        QCOMPARE(tx.txLevelerOn(), levelerOn);
        QCOMPARE(tx.txLevelerMaxGain(), levelerMax);
        QCOMPARE(tx.txLevelerDecay(), levelerDecay);
        QCOMPARE(tx.txAlcMaxGain(), alcMax);
        QCOMPARE(tx.txAlcDecay(), alcDecay);

        // Permission restores the groups and their own tooltips; withdrawing
        // it gates them again with the reason MainWindow passes.
        dialog.setTransmitPermitted(true);
        for (QGroupBox* group : {leveler, alc}) {
            QVERIFY(group->isEnabled());
            QVERIFY(group->toolTip().isEmpty());
            QVERIFY(group->accessibleDescription().isEmpty());
        }
        dialog.setTransmitPermitted(false);
        QVERIFY(!leveler->isEnabled());
        QVERIFY2(OperatorWording::isPlain(leveler->toolTip()), qPrintable(leveler->toolTip()));

        // A page built on its own for a remote model starts denied.
        AgcAlcSetupPage standalone(&remote);
        QGroupBox* const standaloneLeveler = findGroup(&standalone, QStringLiteral("TX Leveler"));
        QVERIFY(standaloneLeveler != nullptr);
        QVERIFY(!standaloneLeveler->isEnabled());

        // Local direct mode: live, and the controls still write.
        RadioModel local;
        local.addSlice();
        SetupDialog localDialog(&local);
        localDialog.selectPage(QStringLiteral("AGC/ALC"));
        QWidget* const localPage = localDialog.realizedPageForTest(QStringLiteral("AGC/ALC"));
        QVERIFY(localPage != nullptr);
        QGroupBox* const localLeveler = findGroup(localPage, QStringLiteral("TX Leveler"));
        QVERIFY(localLeveler != nullptr);
        QVERIFY(localLeveler->isEnabled());
        QVERIFY(localLeveler->toolTip().isEmpty());
        const bool localOn = local.transmitModel().txLevelerOn();
        localLeveler->findChild<QCheckBox*>()->click();
        QCOMPARE(local.transmitModel().txLevelerOn(), !localOn);
    }

    void remoteDspOptionsTransmitCombosFollowThePermission()
    {
        const QString reason = QStringLiteral("Remote transmit is unavailable");
        const QStringList txKeys = {
            QStringLiteral("DspOptionsBufferSizePhoneTx"),
            QStringLiteral("DspOptionsBufferSizeFmTx"),
            QStringLiteral("DspOptionsBufferSizeDigTx"),
            QStringLiteral("DspOptionsFilterSizePhoneTx"),
            QStringLiteral("DspOptionsFilterSizeFmTx"),
            QStringLiteral("DspOptionsFilterSizeDigTx"),
            QStringLiteral("DspOptionsFilterTypePhoneTx"),
            QStringLiteral("DspOptionsFilterTypeFmTx"),
            QStringLiteral("DspOptionsFilterTypeDigTx"),
        };

        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.setTransmitPermitted(false, reason);
        // "Options" is also a General leaf; select the one under DSP.
        auto* tree = dialog.findChild<QTreeWidget*>();
        QVERIFY(tree != nullptr);
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(0) == QStringLiteral("Options") && (*it)->parent()
                && (*it)->parent()->text(0) == QStringLiteral("DSP")) {
                tree->setCurrentItem(*it);
            }
        }
        DspOptionsPage* page = dialog.findChild<DspOptionsPage*>();
        QVERIFY(page != nullptr);
        QVERIFY(page->isEnabled());

        QMap<QString, QVariant> before;
        for (const QString& key : txKeys) {
            auto* combo = page->findChild<QComboBox*>(key);
            QVERIFY2(combo != nullptr, qPrintable(key));
            QVERIFY2(!combo->isEnabled(), qPrintable(key));
            QCOMPARE(combo->toolTip(), reason);
            QCOMPARE(combo->accessibleDescription(), reason);
            before.insert(key, AppSettings::instance().value(key));
            QTest::keyClick(combo, Qt::Key_Down);
        }
        for (const QString& key : txKeys) {
            QCOMPARE(AppSettings::instance().value(key), before.value(key));
        }
        // The receive combos beside them stay live.
        auto* phoneRx = page->findChild<QComboBox*>(QStringLiteral("DspOptionsBufferSizePhoneRx"));
        QVERIFY(phoneRx != nullptr);
        QVERIFY(phoneRx->isEnabled());

        dialog.setTransmitPermitted(true);
        for (const QString& key : txKeys) {
            auto* combo = page->findChild<QComboBox*>(key);
            QVERIFY2(combo->isEnabled(), qPrintable(key));
            QVERIFY(combo->toolTip() != reason);
            QVERIFY(combo->accessibleDescription().isEmpty());
        }

        DspOptionsPage standalone(&remote);
        QVERIFY(!standalone.findChild<QComboBox*>(txKeys.first())->isEnabled());

        RadioModel local;
        DspOptionsPage localPage(&local);
        for (const QString& key : txKeys) {
            auto* combo = localPage.findChild<QComboBox*>(key);
            QVERIFY2(combo != nullptr, qPrintable(key));
            QVERIFY2(combo->isEnabled(), qPrintable(key));
        }
        auto* localCombo = localPage.findChild<QComboBox*>(txKeys.first());
        const QString localBefore = localCombo->currentText();
        QTest::keyClick(localCombo, Qt::Key_Down);
        QVERIFY(localCombo->currentText() != localBefore);
        QCOMPARE(AppSettings::instance().value(txKeys.first()).toString(),
                 localCombo->currentText());
    }

    // ====================================================================
    // R-R3-21 fix wave: the RADE applet. Its profile combo writes the same
    // microphone profile Audio > TX Profile gates, and Reset vocoder acts on
    // a RADE channel only this computer's own DSP could hold.
    // ====================================================================
    void remoteRadeAppletFollowsTheTransmitPermission()
    {
        const QString reason = QStringLiteral("Remote transmit is unavailable");
        RadioModel remote(RadioModel::Role::Remote);
        remote.addSliceWithStationId(0, QStringLiteral("pan-0"));
        QVERIFY(remote.activeSlice() != nullptr);
        MicProfileManager* const mgr = remote.micProfileManager();
        QVERIFY(mgr != nullptr);
        mgr->setMacAddress(QStringLiteral("00:11:22:33:44:55"));
        mgr->load();
        QVERIFY(mgr->profileNames().size() > 1);

        remote.resetLocalDspHandOutAudit();
        RadeApplet applet(&remote);
        QComboBox* const combo = applet.profileComboForTest();
        QPushButton* const reset = applet.resetVocoderButtonForTest();
        QVERIFY(combo != nullptr);
        QVERIFY(reset != nullptr);
        QVERIFY(!combo->isEnabled());
        QVERIFY(!reset->isEnabled());
        QVERIFY2(OperatorWording::isPlain(combo->toolTip()), qPrintable(combo->toolTip()));
        QVERIFY2(OperatorWording::isPlain(reset->toolTip()), qPrintable(reset->toolTip()));

        applet.setTransmitPermitted(false, reason);
        QCOMPARE(combo->toolTip(), reason);
        QCOMPARE(reset->toolTip(), reason);

        const QString activeBefore = mgr->activeProfileName();
        QTest::keyClick(combo, Qt::Key_Down);
        emit combo->textActivated(combo->itemText(combo->count() - 1));
        QCOMPARE(mgr->activeProfileName(), activeBefore);
        reset->click();
        emit mgr->profileListChanged();
        QVERIFY(!combo->isEnabled());
        // Nothing here looked up this window's own DSP.
        QCOMPARE(remote.localDspHandOutCount(), 0);

        // Permission restores the profile combo. Reset vocoder stays
        // unavailable: the vocoder runs on the Core.
        applet.setTransmitPermitted(true);
        QVERIFY(combo->isEnabled());
        QVERIFY(combo->toolTip() != reason);
        QVERIFY(!reset->isEnabled());
        const QString coreVocoder = QStringLiteral(
            "The RADE vocoder runs on the station computer and cannot be reset "
            "from a remote window.");
        QCOMPARE(reset->toolTip(), coreVocoder);
        QCOMPARE(reset->accessibleDescription(), coreVocoder);
        applet.setTransmitPermitted(false, reason);
        QVERIFY(!combo->isEnabled());
        QCOMPARE(reset->toolTip(), reason);
        QCOMPARE(reset->accessibleDescription(), reason);

        RadioModel local;
        local.addSlice();
        MicProfileManager* const localMgr = local.micProfileManager();
        localMgr->setMacAddress(QStringLiteral("00:11:22:33:44:55"));
        localMgr->load();
        RadeApplet localApplet(&local);
        QComboBox* const localCombo = localApplet.profileComboForTest();
        QVERIFY(localCombo->isEnabled());
        QVERIFY(localCombo->toolTip().isEmpty());
        const QString target = localCombo->itemText(0) != localMgr->activeProfileName()
            ? localCombo->itemText(0) : localCombo->itemText(1);
        emit localCombo->textActivated(target);
        QCOMPARE(localMgr->activeProfileName(), target);
    }

    // ====================================================================
    // R-R3-21 fix wave: the RF-Kit RF2K-S applet. A Core with RF-Kit
    // enabled shows it in a remote window (rfKitEnabled is mirrored), and
    // OPERATE, the antenna buttons and Disconnect/Reconnect drive this
    // computer's own RF2K-S connection, the amplifier the station owns.
    // ====================================================================
    void remoteRfKitAppletControlsAreUnavailable()
    {
        RadioModel remote(RadioModel::Role::Remote);
        Rf2ksApplet applet(&remote);
        QSignalSpy operate(&applet, &Rf2ksApplet::operateToggled);
        QSignalSpy antenna(&applet, &Rf2ksApplet::antennaRequested);
        QSignalSpy toggles(&applet, &Rf2ksApplet::connectionToggleRequested);

        QPushButton* operateBtn = nullptr;
        for (QPushButton* b : applet.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("STANDBY")) { operateBtn = b; }
        }
        QVERIFY(operateBtn != nullptr);
        QVERIFY(!operateBtn->isEnabled());
        QCOMPARE(operateBtn->toolTip(), AmpApplet::remoteUnavailableReason());
        QVERIFY2(OperatorWording::isPlain(operateBtn->toolTip()), qPrintable(operateBtn->toolTip()));
        operateBtn->click();
        applet.clickOperateButtonForTesting();
        QCOMPARE(operate.count(), 0);

        // The amplifier reporting its antennas must not re-enable them.
        QList<RfKitAntenna> antennas;
        for (int i = 1; i <= 4; ++i) {
            RfKitAntenna a;
            a.type = RfKitAntenna::Type::Internal;
            a.number = i;
            a.state = RfKitAntenna::State::Available;
            antennas << a;
        }
        applet.setAntennas(antennas);
        for (int i = 1; i <= 4; ++i) {
            QVERIFY(!applet.antennaButtonIsEnabledForTesting(i));
            applet.clickAntennaButtonForTesting(i);
        }
        QCOMPARE(antenna.count(), 0);

        std::unique_ptr<QMenu> menu(applet.buildContextMenuForTesting());
        QAction* toggle = nullptr;
        for (QAction* a : menu->actions()) {
            if (a->text() == QStringLiteral("Reconnect")
                || a->text() == QStringLiteral("Disconnect")) {
                toggle = a;
            }
        }
        QVERIFY(toggle != nullptr);
        QVERIFY(!toggle->isEnabled());
        QCOMPARE(toggle->toolTip(), operateBtn->toolTip());
        toggle->trigger();
        QCOMPARE(toggles.count(), 0);

        RadioModel local;
        Rf2ksApplet localApplet(&local);
        QSignalSpy localOperate(&localApplet, &Rf2ksApplet::operateToggled);
        QSignalSpy localAntenna(&localApplet, &Rf2ksApplet::antennaRequested);
        QSignalSpy localToggles(&localApplet, &Rf2ksApplet::connectionToggleRequested);
        localApplet.clickOperateButtonForTesting();
        QCOMPARE(localOperate.count(), 1);
        localApplet.setAntennas(antennas);
        QVERIFY(localApplet.antennaButtonIsEnabledForTesting(1));
        localApplet.clickAntennaButtonForTesting(1);
        QCOMPARE(localAntenna.count(), 1);
        std::unique_ptr<QMenu> localMenu(localApplet.buildContextMenuForTesting());
        for (QAction* a : localMenu->actions()) {
            if (a->text() == QStringLiteral("Reconnect")) {
                QVERIFY(a->isEnabled());
                a->trigger();
            }
        }
        QCOMPARE(localToggles.count(), 1);
    }

    // ====================================================================
    // R-R3-21 fix wave: the RX applet's Shift-click on a filter preset also
    // matches the TX passband. That half is a transmit write.
    // ====================================================================
    void remoteRxAppletShiftClickLeavesTheTxFilterAlone()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SliceModel slice(0);
        slice.setDspMode(DSPMode::USB);
        RxApplet applet(&slice, &remote);
        applet.show();
        TransmitModel& tx = remote.transmitModel();
        const int lowBefore = tx.filterLow();
        const int highBefore = tx.filterHigh();
        QSignalSpy txFilterChanged(&tx, &TransmitModel::filterChanged);
        int presets = 0;
        for (QPushButton* b : applet.findChildren<QPushButton*>()) {
            if (!b->isCheckable() || !b->toolTip().contains(QStringLiteral(" Hz to "))) {
                continue;
            }
            ++presets;
            QTest::mouseClick(b, Qt::LeftButton, Qt::ShiftModifier);
        }
        QVERIFY(presets > 1);
        QCOMPARE(txFilterChanged.count(), 0);
        QCOMPARE(tx.filterLow(), lowBefore);
        QCOMPARE(tx.filterHigh(), highBefore);

        // Local direct mode: the same Shift-click moves the TX passband,
        // so the remote half above is not vacuous.
        RadioModel local;
        SliceModel localSlice(0);
        localSlice.setDspMode(DSPMode::USB);
        RxApplet localApplet(&localSlice, &local);
        localApplet.show();
        TransmitModel& localTx = local.transmitModel();
        QSignalSpy localTxFilterChanged(&localTx, &TransmitModel::filterChanged);
        for (QPushButton* b : localApplet.findChildren<QPushButton*>()) {
            if (!b->isCheckable() || !b->toolTip().contains(QStringLiteral(" Hz to "))) {
                continue;
            }
            QTest::mouseClick(b, Qt::LeftButton, Qt::ShiftModifier);
        }
        QVERIFY(localTxFilterChanged.count() > 0);
    }

    // ====================================================================
    // R-R3-21 fix wave: the default reason every transmit surface shows
    // before MainWindow pushes its own is plain English too.
    // ====================================================================
    void defaultTransmitReasonsArePlainEnglish()
    {
        const QString expected = QStringLiteral(
            "Transmit controls are unavailable until the station confirms "
            "transmit permission.");
        const auto reasonsOn = [](QWidget* root) {
            QStringList reasons;
            for (QWidget* w : root->findChildren<QWidget*>()) {
                if (w->toolTip().startsWith(QStringLiteral("Transmit controls are unavailable"))) {
                    reasons << w->toolTip();
                }
            }
            return reasons;
        };

        RadioModel remote(RadioModel::Role::Remote);
        SliceModel slice(0);
        TxApplet tx(&remote);
        RxApplet rx(&slice, &remote);
        PhoneCwApplet phone(&remote);
        VfoWidget flag;
        flag.setRadioModel(&remote);
        for (QWidget* surface : std::initializer_list<QWidget*>{&tx, &rx, &phone, &flag}) {
            const QStringList reasons = reasonsOn(surface);
            QVERIFY2(!reasons.isEmpty(), surface->metaObject()->className());
            for (const QString& reason : reasons) {
                QCOMPARE(reason, expected);
                QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
            }
        }
    }

    // ====================================================================
    // R-R3-21 / R-R3-25: the Tools menu's two developer test entries.
    //
    // "Test antenna switch toast" and "Test TX-bound re-route dialog" fake
    // an antenna switch and a TX-bound antenna re-route on this window's
    // own RadioModel. Nothing on the Core stands behind either, so a
    // remote session gives them the transmit gate and the tooltip the
    // other unavailable transmit controls carry. Local direct mode keeps
    // them exactly as they were.
    //
    // Real windows, through GuiSessionCoordinator: the production path
    // from local mode to a Core session and back again.
    // ====================================================================
    void toolsMenuTestEntriesAreDisabledInARemoteSession()
    {
        // The arrangement tst_gui_session_coordinator makes first: no VAX
        // first-run dialog, and no discovery broadcast from the local
        // windows onto the LAN.
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"),
                                         QStringLiteral("True"));
        RadioDiscovery::clearHoldOffForTest();
        {
            RadioDiscovery discovery;
            discovery.holdOffScans(std::chrono::minutes{5});
        }
        const auto releaseHoldOff = qScopeGuard([] {
            RadioDiscovery::clearHoldOffForTest();
        });

        // A Core on loopback. Its model has no radio behind it, which is
        // all this needs: the handshake is what makes the session
        // Core-connected.
        QTemporaryDir stationDir;
        QVERIFY(stationDir.isValid());
        AppSettings stationSettings(stationDir.filePath(QStringLiteral("station.settings")));

        // Both ends of a real session have run CoreInit::initialize()'s
        // settings migrations before it opens. StationClient warns when
        // the client has not, and each end warns when their versions
        // differ. Which version does not matter here, only that both
        // ends have one and it is the same.
        constexpr int kMigratedSchema = 6;
        AppSettings::instance().ensureSettingsAtVersion(kMigratedSchema);
        stationSettings.ensureSettingsAtVersion(kMigratedSchema);

        RadioModel station;
        StationServer server(&station, stationSettings, stationDir.path());
        QWebSocketServer listener(QStringLiteral("core"), QWebSocketServer::NonSecureMode);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        connect(&listener, &QWebSocketServer::newConnection, &server, [&listener, &server] {
            server.acceptTransport(new WebSocketTransport(
                listener.nextPendingConnection(), StationServer::kMaxIncomingMessageBytes));
        });
        StationStartupSelection core;
        core.connection.url = QStringLiteral("ws://127.0.0.1:%1").arg(listener.serverPort());
        core.connection.token = server.token();
        core.connection.allowUnpinned = true;
        core.savedId = QStringLiteral("core");

        GuiSessionCoordinator sessions;
        QString localToastTip;
        QString localReRouteTip;
        QString remoteReason;

        // ---- Local direct mode: live, and as it has always been ----
        {
            QVERIFY(sessions.replace({}, false));
            MainWindow* const window = sessions.window();
            QVERIFY(window->radioModel()->ownsLocalDsp());
            QAction* const toast = window->findChild<QAction*>(kTestToastActionName);
            QAction* const reRoute = window->findChild<QAction*>(kTestReRouteActionName);
            QVERIFY(toast != nullptr);
            QVERIFY(reRoute != nullptr);
            QCOMPARE(toast->text(), QStringLiteral("Test antenna switch &toast"));
            QCOMPARE(reRoute->text(), QStringLiteral("Test TX-bound &re-route dialog"));
            QVERIFY(toast->isEnabled());
            QVERIFY(reRoute->isEnabled());
            localToastTip = toast->toolTip();
            localReRouteTip = reRoute->toolTip();
            QVERIFY(!localToastTip.isEmpty());
            QVERIFY(!localReRouteTip.isEmpty());

            QVERIFY(detachTestSurfaceConsumers(window));
            QSignalSpy switched(window->radioModel(), &RadioModel::antennaAutoSwitched);
            QSignalSpy reRouted(window->radioModel(), &RadioModel::txBoundReRouteRequested);
            toast->trigger();
            reRoute->trigger();
            // Non-vacuity for the remote half: the same trigger on a live
            // entry does reach RadioModel.
            QCOMPARE(switched.count(), 1);
            QCOMPARE(reRouted.count(), 1);
        }

        // ---- Connected to a Core: disabled, with the transmit reason ----
        {
            QVERIFY(sessions.replace(core, true));
            MainWindow* const window = sessions.window();
            QVERIFY(!window->radioModel()->ownsLocalDsp());
            QAction* const toast = window->findChild<QAction*>(kTestToastActionName);
            QAction* const reRoute = window->findChild<QAction*>(kTestReRouteActionName);
            QAction* const txEqualizer = window->findChild<QAction*>(kTxEqualizerActionName);
            QVERIFY(toast != nullptr);
            QVERIFY(reRoute != nullptr);
            QVERIFY(txEqualizer != nullptr);
            // Unavailable from the start, before the handshake lands.
            QVERIFY(!toast->isEnabled());
            QVERIFY(!reRoute->isEnabled());

            auto* const client = window->findChild<StationClient*>();
            QVERIFY(client != nullptr);
            QTRY_VERIFY(client->isHandshakeComplete());
            QVERIFY2(!client->capabilities().txPermitted,
                     "the Core advertises txPermitted=false in R3; this case is "
                     "the receive-only session an operator actually has");

            QVERIFY(!toast->isEnabled());
            QVERIFY(!reRoute->isEnabled());
            QVERIFY(!txEqualizer->isEnabled());
            remoteReason = txEqualizer->toolTip();
            QVERIFY2(remoteReason.contains(QStringLiteral("transmit")),
                     qPrintable(QStringLiteral("the TX Equalizer entry no longer "
                                               "carries the remote transmit reason: %1")
                                    .arg(remoteReason)));
            QCOMPARE(toast->toolTip(), remoteReason);
            QCOMPARE(reRoute->toolTip(), remoteReason);

            // R-R3-21: the RX applet's XIT row gets the same reason through
            // applyRemoteRoleGating once the handshake lands.
            auto* const rxApplet = window->findChild<RxApplet*>();
            QVERIFY(rxApplet != nullptr);
            QPushButton* rxXit = nullptr;
            for (QPushButton* b : rxApplet->findChildren<QPushButton*>()) {
                if (b->text() == QStringLiteral("XIT")) { rxXit = b; }
            }
            QVERIFY(rxXit != nullptr);
            QVERIFY(!rxXit->isEnabled());
            QCOMPARE(rxXit->toolTip(), remoteReason);

            // The RADE applet's profile combo and Reset vocoder get it too.
            auto* const rade = window->findChild<RadeApplet*>();
            QVERIFY(rade != nullptr);
            QVERIFY(!rade->profileComboForTest()->isEnabled());
            QCOMPARE(rade->profileComboForTest()->toolTip(), remoteReason);
            QVERIFY(!rade->resetVocoderButtonForTest()->isEnabled());
            QCOMPARE(rade->resetVocoderButtonForTest()->toolTip(), remoteReason);

            QVERIFY(detachTestSurfaceConsumers(window));
            QSignalSpy switched(window->radioModel(), &RadioModel::antennaAutoSwitched);
            QSignalSpy reRouted(window->radioModel(), &RadioModel::txBoundReRouteRequested);
            toast->trigger();
            reRoute->trigger();
            // Past the disabled action to the handlers themselves, which
            // refuse the way the TX Equalizer entry's handler does.
            emit toast->triggered(false);
            emit reRoute->triggered(false);
            QCOMPARE(switched.count(), 0);
            QCOMPARE(reRouted.count(), 0);
        }

        // ---- Back to local mode: live again, local tooltips back ----
        {
            QVERIFY(sessions.replace({}, false));
            MainWindow* const window = sessions.window();
            QVERIFY(window->radioModel()->ownsLocalDsp());
            QAction* const toast = window->findChild<QAction*>(kTestToastActionName);
            QAction* const reRoute = window->findChild<QAction*>(kTestReRouteActionName);
            QVERIFY(toast != nullptr);
            QVERIFY(reRoute != nullptr);
            QVERIFY(toast->isEnabled());
            QVERIFY(reRoute->isEnabled());
            QCOMPARE(toast->toolTip(), localToastTip);
            QCOMPARE(reRoute->toolTip(), localReRouteTip);
            auto* const rade = window->findChild<RadeApplet*>();
            QVERIFY(rade != nullptr);
            QVERIFY(rade->profileComboForTest()->isEnabled());
            QVERIFY(rade->profileComboForTest()->toolTip() != remoteReason);
            QVERIFY(localToastTip != remoteReason);
            QVERIFY(localReRouteTip != remoteReason);

            QVERIFY(detachTestSurfaceConsumers(window));
            QSignalSpy switched(window->radioModel(), &RadioModel::antennaAutoSwitched);
            QSignalSpy reRouted(window->radioModel(), &RadioModel::txBoundReRouteRequested);
            toast->trigger();
            reRoute->trigger();
            QCOMPARE(switched.count(), 1);
            QCOMPARE(reRouted.count(), 1);
        }

        sessions.shutdown();
    }

    // ====================================================================
    // R-R3-16 (carried from the Connections task): a LOCAL window still
    // opens Connections on its own when its radio drops. Since R-R3-16 the
    // automatic open in MainWindow::onConnectionStateChanged runs only
    // for a model that owns its local DSP; this pins that the local half
    // still behaves exactly as before. The open keys on a radio name the
    // model learns on connect, so setNameForTest stands in for a connect.
    // ====================================================================
    void localWindowDisconnectStillOpensConnections_data()
    {
        QTest::addColumn<bool>("pickerManaged");
        QTest::newRow("picker") << true;
        // R3 Setup fix wave (final review M4): a window without the picker
        // shows its own connection panel.
        QTest::newRow("direct panel") << false;
    }

    void localWindowDisconnectStillOpensConnections()
    {
        QFETCH(bool, pickerManaged);
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"),
                                         QStringLiteral("True"));
        RadioDiscovery::clearHoldOffForTest();
        {
            RadioDiscovery discovery;
            discovery.holdOffScans(std::chrono::minutes{5});
        }
        const auto releaseHoldOff = qScopeGuard([] {
            RadioDiscovery::clearHoldOffForTest();
        });

        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.setConnectionPickerManaged(pickerManaged);
        RadioModel* const model = window.radioModel();
        QVERIFY(model->ownsLocalDsp());
        QSignalSpy requests(&window, &MainWindow::connectionsRequested);
        const auto panelShown = [&window] {
            auto* panel = window.findChild<ConnectionPanel*>();
            return panel != nullptr && panel->isVisible();
        };

        // No radio name yet (the startup case): a disconnected state opens
        // nothing.
        model->setConnectionStateForTest(ConnectionState::Connecting);
        model->setConnectionStateForTest(ConnectionState::Disconnected);
        QCOMPARE(requests.count(), 0);
        QVERIFY(!panelShown());

        // After a connect has named the radio, losing it opens Connections,
        // once: the picker's request, or this window's own panel.
        model->setNameForTest(QStringLiteral("ANAN-G2"));
        model->setConnectionStateForTest(ConnectionState::Connecting);
        requests.clear();
        model->setConnectionStateForTest(ConnectionState::Disconnected);
        if (pickerManaged) {
            QCOMPARE(requests.count(), 1);
            QVERIFY(!panelShown());
        } else {
            QCOMPARE(requests.count(), 0);
            QVERIFY(panelShown());
            QCOMPARE(window.findChildren<ConnectionPanel*>().size(), 1);
        }
    }

    // ====================================================================
    // R-R3-23: a remote window runs no VAX first-run check, as it already
    // runs no Linux audio first-run. It opens no VAX outputs, so the dialog
    // would offer to bind cables to nothing, and it would record
    // audio/FirstRunComplete and the cable fingerprint for a later local
    // session that never saw it. A local window still runs it.
    // ====================================================================
    void vaxFirstRunCheckRunsOnlyInALocalWindow()
    {
        RadioDiscovery::clearHoldOffForTest();
        {
            RadioDiscovery discovery;
            discovery.holdOffScans(std::chrono::minutes{5});
        }
        const auto releaseHoldOff = qScopeGuard([] {
            RadioDiscovery::clearHoldOffForTest();
        });

        // Migrated settings, so the remote window's StationClient does not
        // warn that CoreInit has not run (as the Tools menu case above).
        AppSettings::instance().ensureSettingsAtVersion(6);
        {
            SettingsProxy proxy;
            AppSettings::instance().setRemoteBackend(&proxy);
            const auto dropBackend = qScopeGuard([] {
                AppSettings::instance().setRemoteBackend(nullptr);
            });
            MainWindow remote({QStringLiteral("ws://127.0.0.1:1"), {}, {}, true}, nullptr,
                              MainWindow::ConnectionStartup::Deferred);
            QVERIFY(!remote.radioModel()->ownsLocalDsp());
            QCoreApplication::processEvents();
            QVERIFY(remote.findChild<VaxFirstRunDialog*>() == nullptr);
            QVERIFY(!AppSettings::instance().contains(QStringLiteral("audio/FirstRunComplete")));
            QVERIFY(!AppSettings::instance().contains(QStringLiteral("audio/LastDetectedCables")));
        }

        // Non-vacuity: the same settings, a local window, and the check runs
        // (it always records the cable fingerprint, and with first-run not
        // yet complete it shows the dialog).
        MainWindow local({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        QVERIFY(local.radioModel()->ownsLocalDsp());
        QCoreApplication::processEvents();
        QVERIFY(AppSettings::instance().contains(QStringLiteral("audio/LastDetectedCables")));
        QVERIFY(local.findChild<VaxFirstRunDialog*>() != nullptr);
    }
};

QTEST_MAIN(TstRemoteGuiGating)
#include "tst_remote_gui_gating.moc"
