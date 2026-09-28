// no-port-check: NereusSDR-original Remote Access presentation contract.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/setup/RemoteStationPage.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using NereusSDR::RemoteStationPage;

class RemoteStationPageTest : public QObject {
    Q_OBJECT
private:
    static RemoteStationPage::State ready()
    {
        RemoteStationPage::State state;
        state.available = true;
        state.runCore = true;
        state.stationName = QStringLiteral("My station");
        state.reachabilityText = QStringLiteral("Found by devices on this network.");
        state.keyBackupPath = QStringLiteral("/Users/operator/.config/nereus/station.key");
        state.devices = {{QByteArray("device-a"), QStringLiteral("Phone A"),
                          QStringLiteral("Monday"), QStringLiteral("Today"), true}};
        return state;
    }
    static QPushButton* button(RemoteStationPage& page, const char* name)
    {
        const QList<QPushButton*> matches = page.findChildren<QPushButton*>(QString::fromLatin1(name));
        Q_ASSERT(!matches.isEmpty());
        return matches.constLast();
    }
    static QCheckBox* check(RemoteStationPage& page, const char* name)
    {
        QCheckBox* result = page.findChild<QCheckBox*>(QString::fromLatin1(name));
        Q_ASSERT(result);
        return result;
    }
    static void screenshot(RemoteStationPage& page, const QString& file)
    {
        const QString directory = qEnvironmentVariable("NEREUS_REMOTE_PAGE_SHOTS");
        if (directory.isEmpty()) { return; }
        QVERIFY(QDir().mkpath(directory));
        page.resize(760, 760);
        page.show();
        QCoreApplication::processEvents();
        QVERIFY(page.grab().save(directory + QLatin1Char('/') + file));
    }
private slots:
    void actionsStayAuthoritative()
    {
        RemoteStationPage page;
        RemoteStationPage::State state = ready();
        QSignalSpy run(&page, &RemoteStationPage::runCoreRequested);
        QSignalSpy keep(&page, &RemoteStationPage::keepRunningRequested);
        QSignalSpy start(&page, &RemoteStationPage::startWithComputerRequested);
        QSignalSpy rename(&page, &RemoteStationPage::renameRequested);
        QSignalSpy revoke(&page, &RemoteStationPage::revokeRequested);
        QSignalSpy add(&page, &RemoteStationPage::addDeviceRequested);
        QSignalSpy backup(&page, &RemoteStationPage::keyBackupAcknowledgedRequested);
        page.setState(state);
        QCOMPARE(run.size() + keep.size() + start.size() + rename.size()
                 + revoke.size() + add.size() + backup.size(), 0);
        check(page, "remoteAccessRunCore")->click();
        QCOMPARE(run.takeFirst().at(0).toBool(), false);
        QVERIFY(check(page, "remoteAccessRunCore")->isChecked());
        QVERIFY(page.state().runCore);
        check(page, "remoteAccessKeepRunning")->click();
        QCOMPARE(keep.takeFirst().at(0).toBool(), true);
        QVERIFY(!page.state().keepRunning);
        QVERIFY(!check(page, "remoteAccessKeepRunning")->isChecked());
        check(page, "remoteAccessStartWithComputer")->click();
        QCOMPARE(start.takeFirst().at(0).toBool(), true);
        QVERIFY(!page.state().startWithComputer);
        QVERIFY(!check(page, "remoteAccessStartWithComputer")->isChecked());
        button(page, "remoteAccessAddDevice")->click();
        QCOMPARE(add.size(), 1);
        button(page, "remoteAccessBackupAcknowledged")->click();
        QCOMPARE(backup.size(), 1);
        QPushButton* revokeButton = button(page, "remoteAccessRevoke");
        revokeButton->click();
        QCOMPARE(revoke.takeFirst().at(0).toByteArray(), QByteArray("device-a"));
        QTimer::singleShot(0, [] {
            QInputDialog* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->setTextValue(QStringLiteral("  New station  "));
            dialog->accept();
        });
        button(page, "remoteAccessRename")->click();
        QCOMPARE(rename.takeFirst().at(0).toString(), QStringLiteral("New station"));
        QCOMPARE(page.state().stationName, QStringLiteral("My station"));
        QTimer::singleShot(0, [] {
            QInputDialog* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->reject();
        });
        button(page, "remoteAccessRename")->click();
        QCOMPARE(rename.size(), 0);
        state.stationName = QStringLiteral("New station");
        state.keepRunning = true;
        state.startWithComputer = true;
        page.setState(state);
        QCOMPARE(run.size() + keep.size() + start.size() + rename.size()
                 + revoke.size() + add.size() + backup.size(), 2);
        QVERIFY(check(page, "remoteAccessKeepRunning")->isChecked());
        QVERIFY(check(page, "remoteAccessStartWithComputer")->isChecked());
    }
    void gatesAndReplacement()
    {
        RemoteStationPage page;
        QSignalSpy revoke(&page, &RemoteStationPage::revokeRequested);
        QSignalSpy run(&page, &RemoteStationPage::runCoreRequested);
        RemoteStationPage::State state = ready();
        page.setState(state);
        QPushButton* stale = button(page, "remoteAccessRevoke");
        state.devices = {{QByteArray("device-b"), QStringLiteral("Phone B"),
                          QStringLiteral("Yesterday"), QStringLiteral("Today"), false}};
        page.setState(state);
        stale->click();
        QCOMPARE(revoke.size(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QPushButton* current = button(page, "remoteAccessRevoke");
        QVERIFY(!current->isEnabled());
        QVERIFY(!current->accessibleDescription().isEmpty());
        state.devices[0].revocable = true;
        page.setState(state);
        button(page, "remoteAccessRevoke")->click();
        QCOMPARE(revoke.takeFirst().at(0).toByteArray(), QByteArray("device-b"));
        state.available = false;
        page.setState(state);
        QVERIFY(!check(page, "remoteAccessRunCore")->isEnabled());
        QVERIFY(!button(page, "remoteAccessAddDevice")->isEnabled());
        QVERIFY(!button(page, "remoteAccessRevoke")->isEnabled());
        QVERIFY(page.findChild<QLabel*>("remoteAccessReason")->text().contains("local radio"));
        state.available = true;
        state.busy = true;
        page.setState(state);
        QVERIFY(!check(page, "remoteAccessRunCore")->isEnabled());
        state.busy = false;
        state.transmitting = true;
        page.setState(state);
        QVERIFY(!check(page, "remoteAccessRunCore")->isEnabled());
        state.transmitting = false;
        state.runCore = false;
        page.setState(state);
        QVERIFY(check(page, "remoteAccessRunCore")->isEnabled());
        QVERIFY(!check(page, "remoteAccessKeepRunning")->isEnabled());
        QVERIFY(!check(page, "remoteAccessStartWithComputer")->isEnabled());
        QVERIFY(!button(page, "remoteAccessRename")->isEnabled());
        check(page, "remoteAccessRunCore")->click();
        QCOMPARE(run.takeFirst().at(0).toBool(), true);
        QVERIFY(!page.state().runCore);
    }
    void renderStates()
    {
        RemoteStationPage page;
        RemoteStationPage::State state;
        page.setState(state);
        screenshot(page, QStringLiteral("01-off.png"));
        state = ready();
        state.stationName = QStringLiteral("The extremely long station name for a desktop radio in the northern workshop");
        state.keyBackupPath = QStringLiteral("/Users/operator/Documents/Very Long Folder Name/Nereus SDR/Profiles/Workshop Station/station-identity-key.pem");
        state.pairingOpen = true;
        state.pairingCode = QStringLiteral("amber-pine-harbor");
        state.devices.clear();
        page.setState(state);
        screenshot(page, QStringLiteral("02-on-pairing.png"));
        state.pairingOpen = false;
        state.devices = {{QByteArray("phone"), QStringLiteral("A very long phone name from the main desk"),
                          QStringLiteral("September 27, 2026"), QStringLiteral("Today"), true},
                         {QByteArray("tablet"), QStringLiteral("Workshop tablet"),
                          QStringLiteral("September 20, 2026"), QStringLiteral("Yesterday"), false}};
        page.setState(state);
        screenshot(page, QStringLiteral("03-two-devices.png"));
        state.keyBackupAcknowledged = true;
        page.setState(state);
        screenshot(page, QStringLiteral("04-backup-acknowledged.png"));
    }
};
QTEST_MAIN(RemoteStationPageTest)
#include "tst_remote_station_page.moc"
