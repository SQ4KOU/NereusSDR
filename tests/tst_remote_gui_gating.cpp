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
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08 -- New test file for remote-daemon R2 Task 20. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QApplication>
#include <QMetaObject>
#include <QSignalSpy>
#include <QStringList>
#include <QWidget>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/WdspTypes.h"
#include "core/session/RemoteStationOptions.h"
#include "core/settings/SettingsProxy.h"
#include "core/settings/SettingsScope.h"
#include "gui/MainWindow.h"
#include "gui/SetupDialog.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

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

} // namespace

class TstRemoteGuiGating : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
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
        QStringList offenders;
        int reachedCount = 0;
        const QStringList labels = dialog.pageLabelsForTest();
        const int pageCount = dialog.registeredPageCountForTest();
        QVERIFY(pageCount > 0);
        QCOMPARE(labels.size(), pageCount);

        for (int i = 0; i < pageCount; ++i) {
            const int before = model.localDspHandOutCount();
            QWidget* page = dialog.realizePageAtForTest(i);
            if (page == nullptr) {
                continue;  // a factory that yields nothing has nothing to gate
            }
            const bool reachedLocalDsp = model.localDspHandOutCount() > before;
            if (!reachedLocalDsp) {
                continue;
            }
            ++reachedCount;
            if (page->isEnabled()) {
                offenders << labels.at(i);
            }
        }

        // Minor 2's fix, pinned: the sweep must have BUILT every leaf. With
        // the previous label-driven loop this read pageCount - 1, because
        // the second "Options" resolved back to the first and its factory
        // never ran -- and the test still reported a clean sweep.
        QCOMPARE(dialog.realizedPageCountForTest(), pageCount);

        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral(
                     "%1 Setup page(s) bound themselves to this process's DSP "
                     "on a remote-station model and were left enabled: %2")
                                .arg(offenders.size())
                                .arg(offenders.join(QStringLiteral(", ")))));

        // Non-vacuity. If nothing in the tree reaches local DSP any more,
        // the loop above proved nothing and this test is a no-op that
        // would keep passing after the gate was deleted.
        QVERIFY2(reachedCount > 0,
                 "no Setup page reached local DSP at all, so the gate above "
                 "was never exercised -- either the enumeration changed or "
                 "the audit stopped arming");
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
    // Step 4: MOX refuses on a remote model, through the EXISTING
    // pre-check, so the refusal reaches the status-bar toast.
    // ====================================================================

    void remoteModelRefusesMoxWithAnR4Reason()
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
        QVERIFY2(reason.contains(QStringLiteral("R4")),
                 qPrintable(QStringLiteral("refusal reason must name the phase "
                                           "that brings TX, got: %1")
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
};

QTEST_MAIN(TstRemoteGuiGating)
#include "tst_remote_gui_gating.moc"
