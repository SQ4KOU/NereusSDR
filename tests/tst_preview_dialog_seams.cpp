// no-port-check: NereusSDR-original test of the visual preview's dialog seams.
// =================================================================
// tests/tst_preview_dialog_seams.cpp  (NereusSDR)
// =================================================================
//
// The visual preview opens every dialog offline. Three dialogs need a seam
// for it, and the remote ones need a window that never dials:
//
//   * ConnectionPanel starts discovery in its constructor; with
//     setDiscoveryStartSkippedForTest(true) it starts none.
//   * LanScanDialog starts a LAN scan in its constructor; with
//     setDiscoveryStartSkippedForTest(true) it builds no LanDiscovery.
//   * MainWindow::buildWisdomProgressDialog builds the FFTW wisdom dialog
//     the wisdomProgress handler shows, without generating wisdom.
//   * A remote window built with GuiSessionCoordinator::replace(..., false)
//     opens Remote Diagnostics, the remote connection panel and Setup's
//     This Core page without asking its connection to start or opening a
//     socket.
//
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest>
#include <QAbstractSocket>
#include <QApplication>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalSpy>
#include <QWebSocket>

#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/session/StationClient.h"
#include "gui/ConnectionPanel.h"
#include "core/LanDiscovery.h"
#include "gui/GuiSessionCoordinator.h"
#include "gui/LanScanDialog.h"
#include "gui/MainWindow.h"
#include "gui/RemoteConnectionController.h"
#include "gui/RemoteDiagnosticsDialog.h"
#include "gui/SetupDialog.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

StationStartupSelection remoteCore()
{
    // Never dialled: replace(..., false) builds the remote window without
    // starting its connection (as tst_unbuilt_features builds it).
    return {{QStringLiteral("ws://127.0.0.1:4433"), {}, {}, true}, QStringLiteral("core")};
}

template <typename T>
T* visibleTopLevel()
{
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* t = qobject_cast<T*>(w); t != nullptr && t->isVisible()) {
            return t;
        }
    }
    return nullptr;
}

// Every socket the application holds, wherever it is parented.
QStringList openSockets()
{
    QStringList open;
    QObjectList roots;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        roots.append(w);
    }
    roots.append(QCoreApplication::instance());
    for (QObject* root : roots) {
        for (QAbstractSocket* s : root->findChildren<QAbstractSocket*>()) {
            if (s->state() != QAbstractSocket::UnconnectedState) {
                open.append(QStringLiteral("%1 %2").arg(QString::fromLatin1(s->metaObject()->className()))
                                .arg(int(s->state())));
            }
        }
        for (QWebSocket* s : root->findChildren<QWebSocket*>()) {
            if (s->state() != QAbstractSocket::UnconnectedState) {
                open.append(QStringLiteral("QWebSocket %1").arg(int(s->state())));
            }
        }
    }
    return open;
}

} // namespace

class TstPreviewDialogSeams : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("preview-dialog-seams-%1")
                                            .arg(QCoreApplication::applicationPid()));
        AppSettings::instance().clear();
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"),
                                         QStringLiteral("True"));
        AppSettings::instance().ensureSettingsAtVersion(6);
    }

    void cleanup()
    {
        ConnectionPanel::setDiscoveryStartSkippedForTest(false);
        LanScanDialog::setDiscoveryStartSkippedForTest(false);
        RadioDiscovery::clearHoldOffForTest();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // With the seam, the panel never asks the model's discovery to start.
    // The guard stops any start before the scan touches a network
    // interface (RadioDiscovery::scanAllNics checks scanCancelled() before
    // its first socket), so even a broken seam sends nothing.
    void connectionPanelStartsNoDiscoveryWithTheSeam()
    {
        RadioModel model;
        RadioDiscovery* disc = model.discovery();
        QVERIFY(disc != nullptr);
        QObject::connect(disc, &RadioDiscovery::discoveryStarted, disc,
                         [disc] { disc->stopDiscovery(); }, Qt::DirectConnection);
        QSignalSpy started(disc, &RadioDiscovery::discoveryStarted);

        ConnectionPanel::setDiscoveryStartSkippedForTest(true);
        {
            ConnectionPanel panel(&model);
            panel.show();
            QCoreApplication::processEvents();
            QCOMPARE(started.count(), 0);
        }
        QCOMPARE(started.count(), 0);
    }

    // With the seam, the dialog builds no LanDiscovery, so no socket binds
    // and no scan timer runs. (Without the seam the constructor binds UDP
    // 9008 and 9010 at once, which this test must never do, so its red run
    // was the build failing on the missing seam.)
    void lanScanDialogStartsNoScanWithTheSeam()
    {
        LanScanDialog::setDiscoveryStartSkippedForTest(true);
        LanScanDialog dialog;
        dialog.show();
        QCoreApplication::processEvents();
        QVERIFY(dialog.findChild<LanDiscovery*>() == nullptr);
        const QStringList open = openSockets();
        QVERIFY2(open.isEmpty(), qPrintable(open.join(QStringLiteral(", "))));
    }

    // The builder makes the dialog the wisdomProgress handler shows:
    // title, range, no cancel button, its own stylesheet; nothing shown.
    void wisdomProgressDialogIsBuiltWithoutWisdom()
    {
        QWidget parent;
        std::unique_ptr<QProgressDialog> dialog(MainWindow::buildWisdomProgressDialog(&parent));
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->parentWidget(), &parent);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("NereusSDR: FFTW Wisdom"));
        QCOMPARE(dialog->minimum(), 0);
        QCOMPARE(dialog->maximum(), 100);
        QCOMPARE(dialog->value(), 0);
        QVERIFY(dialog->labelText().startsWith(QStringLiteral("Optimizing FFT plans")));
        QVERIFY(dialog->findChild<QPushButton*>() == nullptr);
        QVERIFY(dialog->styleSheet().contains(QStringLiteral("QProgressDialog { background: #0f0f1a; }")));
        QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
        QVERIFY(!dialog->isVisible());
    }

    // The visual preview's remote set: a never-dialled remote window opens
    // its diagnostics, its connection panel and This Core, and its
    // connection is never asked to start; no socket opens.
    void remoteWindowOpensItsDialogsWithoutDialling()
    {
        RadioDiscovery holdOff;
        holdOff.holdOffScans(std::chrono::minutes{5});
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace(remoteCore(), false));
        MainWindow* window = sessions.window();
        QVERIFY(window != nullptr);
        window->setAttribute(Qt::WA_DontShowOnScreen, true);
        window->show();
        QCoreApplication::processEvents();

        QVERIFY(QMetaObject::invokeMethod(window, "openNetworkDiagnostics", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(window, "showRemoteConnectionPanel", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(window, "openSetupAtPage", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("This Core"))));
        // Long enough for any deferred dial (singleShot(0) and the first
        // event-loop turns after show) to have run.
        QTest::qWait(300);

        QVERIFY(visibleTopLevel<RemoteDiagnosticsDialog>() != nullptr);
        QVERIFY(visibleTopLevel<RemoteConnectionPanel>() != nullptr);
        QVERIFY(visibleTopLevel<SetupDialog>() != nullptr);

        const QList<StationClient*> clients = window->findChildren<StationClient*>();
        QVERIFY(!clients.isEmpty());
        for (StationClient* client : clients) {
            QVERIFY(!client->isConnectionActive());
            QVERIFY(!client->isHandshakeComplete());
        }
        const QStringList open = openSockets();
        QVERIFY2(open.isEmpty(), qPrintable(open.join(QStringLiteral(", "))));
        sessions.shutdown();
    }
};

QTEST_MAIN(TstPreviewDialogSeams)
#include "tst_preview_dialog_seams.moc"
