// no-port-check: NereusSDR-original desktop Core runtime tests; temporary profile only.
#include "gui/GuiDesktopStationRuntime.h"
#include "core/AppSettings.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"
#include "gui/SetupDialog.h"
#include "gui/setup/RemoteStationPage.h"
#include "core/session/StationServer.h"
#include "core/station/StationRadios.h"
#include "core/station/StationHost.h"
#include "core/security/StationIdentity.h"
#include "core/security/DeviceStore.h"
#include "core/ConnectionState.h"
#include "core/settings/ISettingsBackend.h"
#include "core/settings/SettingsScope.h"
#include <QCheckBox>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

using namespace NereusSDR;

namespace {
int freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    return probe.serverPort();
}

QString serviceConfigPath(AppSettings& settings)
{
    return QFileInfo(settings.filePath()).absolutePath() + QStringLiteral("/station.conf");
}

bool writeConfig(const QString& path, int port)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(QStringLiteral("remote_port = %1\nremote_bind = 127.0.0.1\n"
                                     "status_page = off\nrendezvous_servers =\n")
                          .arg(port).toUtf8()) > 0;
}
}

class NeverBackend final : public ISettingsBackend {
public:
    bool handlesKey(const QString&) const override { return false; }
    QVariant value(const QString&, const QVariant& fallback) const override { return fallback; }
    void setValue(const QString&, const QVariant&) override {}
    bool contains(const QString&) const override { return false; }
    void remove(const QString&) override {}
    QStringList handledKeys() const override { return {}; }
};

class TstGuiDesktopStationRuntime : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("DesktopRuntimeTest_%1")
                                            .arg(QCoreApplication::applicationPid()));
        QDir().mkpath(QFileInfo(AppSettings::instance().filePath()).absolutePath());
    }
    void cleanup()
    {
        auto& settings = AppSettings::instance();
        settings.remove(QStringLiteral("DesktopCore/Run"));
        settings.remove(QStringLiteral("DesktopCore/KeepRunning"));
        settings.remove(QStringLiteral("StationLabel"));
        settings.remove(QStringLiteral("StationKeyBackupAcknowledged"));
        settings.remove(QStringLiteral("StationRadioChoice"));
        settings.save();
        QFile::remove(QFileInfo(settings.filePath()).absolutePath()
                      + QStringLiteral("/station.conf"));
    }
    void cleanupTestCase()
    {
        QDir(QFileInfo(AppSettings::instance().filePath()).absolutePath()).removeRecursively();
    }
    void desktopPreferencesStayOnThisComputer()
    {
        QCOMPARE(classifySettingsKey(QStringLiteral("DesktopCore/Run")),
                 SettingsScope::OperatorLocal);
        QCOMPARE(classifySettingsKey(QStringLiteral("DesktopCore/KeepRunning")),
                 SettingsScope::OperatorLocal);
        QCOMPARE(classifySettingsKey(QStringLiteral("StationRadioChoice")),
                 SettingsScope::OperatorLocal);
    }

    void defaultsDoNotStartAListener()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        RadioModel model;
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(!runtime.state().runCore);
        QVERIFY(!runtime.state().keepRunning);
        QVERIFY(!runtime.state().startWithComputer);
        QVERIFY(runtime.controller() != nullptr);
        runtime.restore();
        QVERIFY(!runtime.controller()->enabled());
    }

    void runOpensAndClosesRealTemporaryListener()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const int port = freePort();
        QVERIFY(port > 0);
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), port));
        RadioModel model;
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.controller()->enabled());
        QCOMPARE(settings.value(QStringLiteral("DesktopCore/Run")).toBool(), true);
        QTcpServer occupied;
        QVERIFY(!occupied.listen(QHostAddress::LocalHost, port));
        QVERIFY(runtime.setRunCore(false));
        QVERIFY(!runtime.controller()->enabled());
        QVERIFY(occupied.listen(QHostAddress::LocalHost, port));
        QCOMPARE(settings.value(QStringLiteral("DesktopCore/Run")).toBool(), false);
    }

    void blockedPortCannotRetryAfterRejectedRunIntent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QTcpServer occupied;
        QVERIFY(occupied.listen(QHostAddress::LocalHost, 0));
        const int port = occupied.serverPort();
        QVERIFY(writeConfig(serviceConfigPath(settings), port));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(!runtime.setRunCore(true));
        QVERIFY(runtime.controller()->host() == nullptr);
        QVERIFY(!runtime.controller()->enabled());
        QVERIFY(!settings.value(QStringLiteral("DesktopCore/Run"), false).toBool());
        occupied.close();
        QTcpServer next;
        QVERIFY(next.listen(QHostAddress::LocalHost, port));
        QVERIFY(runtime.controller()->host() == nullptr);
    }

    void restoreUsesSavedRunPreferenceAndActualListener()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        settings.setValue(QStringLiteral("DesktopCore/Run"), true);
        QVERIFY(settings.save());
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(!runtime.state().runCore);
        QVERIFY(runtime.restore());
        QVERIFY(runtime.state().runCore);
        QVERIFY(runtime.controller()->enabled());
        runtime.stop();
        QVERIFY(!runtime.state().runCore);
    }

    void suppliedRadioBindingsAdvertiseCurrentChoiceAndCapability()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        const QString path = serviceConfigPath(settings);
        QVERIFY(writeConfig(path, freePort()));
        QFile config(path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Append));
        config.write("station_bind = 127.0.0.1\n");
        config.close();
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        StationRadios radios(settings);
        const QString selected = QStringLiteral("02:00:00:00:00:92");
        RuntimeStationBindings bindings;
        bindings.stationRadios = &radios;
        bindings.selectedRadioMac = [selected] { return selected; };
        bindings.linkMajors = {1};
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(),
                                         true, service, bindings);
        QCOMPARE(runtime.config().stationBind, QStringLiteral("127.0.0.1"));
        QVERIFY(runtime.setRunCore(true));
        QCOMPARE(runtime.controller()->server()->stationRadiosVersion(), 1);
        QCOMPARE(runtime.controller()->host()->stationAnnouncementForTest().radioMac,
                 selected);
        runtime.stop();
    }

    void explicitNetworkAndMediaPolicyFeedsBorrowedHost()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        const QString path = serviceConfigPath(settings);
        const int port = freePort();
        QVERIFY(writeConfig(path, port));
        QFile config(path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Append));
        config.write("pairing_lan_click = deny\nremote_transmit = deny\n"
                     "relay = deny\naudio_bitrate = 24000\naudio_lossless = deny\n");
        config.close();
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        StationServer* server = runtime.controller()->server();
        QVERIFY(server != nullptr);
        QCOMPARE(server->serverPort(), quint16(port));
        QCOMPARE(server->serverAddress(), QHostAddress::LocalHost);
        QVERIFY(!server->pairingLanClickAllowed());
        QVERIFY(!server->remoteTransmitAllowed());
        QVERIFY(!server->relayAllowed());
        QVERIFY(!runtime.state().reachabilityText.contains(QStringLiteral("anywhere"),
                                                            Qt::CaseInsensitive));
        runtime.stop();
    }

    void invalidExplicitConfigIsNeverOverwritten()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        const QString path = serviceConfigPath(settings);
        QFile config(path);
        QVERIFY(config.open(QIODevice::WriteOnly));
        const QByteArray bytes("remote_port = 70000\n");
        QCOMPARE(config.write(bytes), bytes.size());
        config.close();
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(!runtime.state().available);
        QVERIFY(!runtime.setRunCore(true));
        QVERIFY(!runtime.setKeepRunning(true));
        QFile preserved(path);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), bytes);
    }

    void wrongSettingsStoreCannotHostOrRetire()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings other(temp.filePath(QStringLiteral("other.settings")));
        RadioModel model;
        StationServiceOptions service;
        service.profileDirectory = temp.path();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        GuiDesktopStationRuntime runtime(&model, &other, {}, true, service);
        QVERIFY(!runtime.state().available);
        QVERIFY(!runtime.setRunCore(true));
        QVERIFY(!runtime.setKeepRunning(true));
        QVERIFY(!runtime.setStartWithComputer(true));
        QString reason;
        QVERIFY(!runtime.prepareForRetirement(true, &reason));
        QVERIFY(!reason.isEmpty());
        QVERIFY(!runtime.backgroundStartWanted());
        QVERIFY(!runtime.controller()->enabled());
    }

    void busyAndRemoteModelsRefuseAllPageActions()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel local;
        GuiDesktopStationRuntime runtime(&local, &settings, AppSettings::profileOverride(), true, service);
        local.setConnectionStateForTest(ConnectionState::Connecting);
        QVERIFY(runtime.state().busy);
        QVERIFY(!runtime.setRunCore(true));
        QVERIFY(!runtime.setKeepRunning(true));
        QVERIFY(!runtime.setStartWithComputer(true));
        QVERIFY(!runtime.renameStation(QStringLiteral("KG4VCF")));
        QVERIFY(!runtime.revokeDevice(QByteArrayLiteral("invalid")));
        QVERIFY(!runtime.addDevice());
        QVERIFY(!runtime.acknowledgeKeyBackup());
        QVERIFY(!runtime.controller()->enabled());
        local.setConnectionStateForTest(ConnectionState::Disconnected);

        RadioModel remote(RadioModel::Role::Remote);
        GuiDesktopStationRuntime remoteRuntime(&remote, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(!remoteRuntime.state().available);
        QVERIFY(!remoteRuntime.setRunCore(true));
        QVERIFY(!remoteRuntime.addDevice());
    }

    void remoteSettingsBackendAndUnownedProfileRefuseActions()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime unowned(&model, &settings, AppSettings::profileOverride(), false, service);
        QVERIFY(!unowned.state().available);
        QVERIFY(!unowned.setRunCore(true));
        NeverBackend backend;
        settings.setRemoteBackend(&backend);
        {
            GuiDesktopStationRuntime proxied(&model, &settings, AppSettings::profileOverride(), true, service);
            QVERIFY(!proxied.state().available);
            QVERIFY(!proxied.setRunCore(true));
            QVERIFY(!proxied.renameStation(QStringLiteral("KG4VCF")));
            QVERIFY(!proxied.addDevice());
        }
        settings.setRemoteBackend(nullptr);
    }

    void staleActionsCannotBypassOnAirGate()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        model.transmitModel().setTune(true); // logical model state; no radio is connected.
        QVERIFY(model.isCoreOnAir());
        QVERIFY(!runtime.setKeepRunning(true));
        QVERIFY(!runtime.setStartWithComputer(true));
        QVERIFY(!runtime.renameStation(QStringLiteral("KG4VCF")));
        QVERIFY(!runtime.revokeDevice(QByteArrayLiteral("invalid")));
        QVERIFY(!runtime.addDevice());
        QVERIFY(!runtime.acknowledgeKeyBackup());
        QVERIFY(runtime.setRunCore(false)); // TX-first off remains available.
        QVERIFY(!runtime.controller()->enabled());
    }

    void startEntryUsesFakeRunnerAndPreservesExplicitConfig()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        const QString path = serviceConfigPath(settings);
        QVERIFY(writeConfig(path, freePort()));
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray bytes = original.readAll();
        original.close();
        const QString binary = temp.filePath(QStringLiteral("nereusd"));
        QFile executable(binary);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write("binary");
        executable.close();
        QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner));
        StationServiceOptions service;
        service.platform = StationPlatform::MacOS;
        service.binaryPath = binary;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        QStringList commands;
        service.runner = [&commands](const QString& program, const QStringList& args) {
            commands << program + args.join(QLatin1Char(' '));
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.setKeepRunning(true));
        QVERIFY(runtime.setStartWithComputer(true));
        QVERIFY(runtime.state().keepRunning);
        QVERIFY(runtime.state().startWithComputer);
        QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral(
            "home/Library/LaunchAgents/com.boydsoftprez.NereusSDR.station.plist"))));
        const StationServiceOptions later = runtime.backgroundServiceOptions();
        QVERIFY(!later.inheritActiveProfile);
        QCOMPARE(later.profileDirectory, QFileInfo(settings.filePath()).absolutePath());
        QCOMPARE(later.homeDirectory, service.homeDirectory);
        QVERIFY(runtime.setRunCore(false));
        QVERIFY(!runtime.state().startWithComputer);
        QVERIFY(!runtime.state().keepRunning);
        QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral(
            "home/Library/LaunchAgents/com.boydsoftprez.NereusSDR.station.plist"))));
        QCOMPARE(settings.value(QStringLiteral("DesktopCore/Run")).toBool(), false);
        QCOMPARE(settings.value(QStringLiteral("DesktopCore/KeepRunning")).toBool(), false);
        QFile preserved(path);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), bytes);
        QVERIFY(commands.isEmpty());
    }

    void linuxStartupEntryFollowsFakeServiceResult()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        const QString binary = temp.filePath(QStringLiteral("nereusd"));
        QFile executable(binary);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write("binary");
        executable.close();
        QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner));
        bool enabled = false;
        QStringList calls;
        StationServiceOptions service;
        service.platform = StationPlatform::Linux;
        service.binaryPath = binary;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.userName = QStringLiteral("tester");
        service.inheritActiveProfile = false;
        service.runner = [&enabled, &calls](const QString& program, const QStringList& args) {
            calls << program + QLatin1Char(' ') + args.join(QLatin1Char(' '));
            if (program == QStringLiteral("systemctl")
                && args.contains(QStringLiteral("is-enabled"))) {
                return StationServiceCommandResult{enabled ? 0 : 1, {}};
            }
            if (program == QStringLiteral("systemctl")
                && args.contains(QStringLiteral("enable"))) { enabled = true; }
            if (program == QStringLiteral("systemctl")
                && args.contains(QStringLiteral("disable"))) { enabled = false; }
            if (program == QStringLiteral("loginctl")) {
                return StationServiceCommandResult{0, QStringLiteral("yes\n")};
            }
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.setStartWithComputer(true));
        QVERIFY(runtime.state().startWithComputer);
        QVERIFY(enabled);
        QVERIFY(calls.join(QLatin1Char(' ')).contains(QStringLiteral("enable")));
        QVERIFY(runtime.setRunCore(false));
        QVERIFY(!enabled);
        QVERIFY(!runtime.state().startWithComputer);
        QVERIFY(calls.join(QLatin1Char(' ')).contains(QStringLiteral("disable")));
        QVERIFY(!calls.join(QLatin1Char(' ')).contains(QStringLiteral("start nereusd")));
    }

    void deviceActionsUseLiveFacadeAndPersist()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.addDevice());
        QVERIFY(runtime.state().pairingOpen);
        QVERIFY(!runtime.state().pairingCode.isEmpty());
        QVERIFY(runtime.renameStation(QStringLiteral("KG4VCF")));
        QCOMPARE(runtime.state().stationName, QStringLiteral("KG4VCF"));
        QCOMPARE(settings.value(QStringLiteral("StationLabel")).toString(),
                 QStringLiteral("KG4VCF"));
        QVERIFY(!runtime.state().keyBackupPath.isEmpty());
        QVERIFY(runtime.acknowledgeKeyBackup());
        QVERIFY(runtime.state().keyBackupAcknowledged);
        QVERIFY(!runtime.revokeDevice(QByteArrayLiteral("bad-id")));
        runtime.stop();
    }

    void revokeRemovesPairedDeviceThroughServerFacade()
    {
        QTemporaryDir temp;
        QTemporaryDir firstDir;
        QTemporaryDir secondDir;
        QVERIFY(temp.isValid() && firstDir.isValid() && secondDir.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        const StationIdentity firstKey = StationIdentity::loadOrCreate(firstDir.path());
        const StationIdentity secondKey = StationIdentity::loadOrCreate(secondDir.path());
        QVERIFY(firstKey.isValid() && secondKey.isValid());
        PairedDevice first;
        first.id = firstKey.fingerprint();
        first.publicKeySpki = firstKey.publicKeySpki();
        first.name = QStringLiteral("First phone");
        first.kind = QStringLiteral("phone");
        PairedDevice second;
        second.id = secondKey.fingerprint();
        second.publicKeySpki = secondKey.publicKeySpki();
        second.name = QStringLiteral("Second phone");
        second.kind = QStringLiteral("phone");
        DeviceStore* store = runtime.controller()->server()->deviceStore();
        QVERIFY(store->add(first));
        QVERIFY(store->add(second));
        QCOMPARE(runtime.state().devices.size(), 2);
        QVERIFY(runtime.state().devices.at(0).revocable);
        QSignalSpy removed(store, &DeviceStore::deviceRemoved);
        const QByteArray firstId = StationIdentity::toBase64Url(first.id).toLatin1();
        QVERIFY(runtime.revokeDevice(firstId));
        QCOMPARE(removed.size(), 1);
        QVERIFY(!store->find(first.id).has_value());
        QCOMPARE(runtime.state().devices.size(), 1);
        QVERIFY(!runtime.state().devices.at(0).revocable);
        QVERIFY(!runtime.revokeDevice(StationIdentity::toBase64Url(second.id).toLatin1()));
        QVERIFY(store->find(second.id).has_value());
        runtime.stop();
    }

    void explicitQuitAfterRunAndKeepLatchesOnlyBackgroundIntent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        QStringList commands;
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [&commands](const QString& program, const QStringList& args) {
            commands << program + args.join(QLatin1Char(' '));
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.setKeepRunning(true));
        QString reason;
        QVERIFY2(runtime.prepareForRetirement(true, &reason), qPrintable(reason));
        QVERIFY(runtime.backgroundStartWanted());
        QVERIFY(!runtime.controller()->enabled());
        QVERIFY(!runtime.setRunCore(true));
        QVERIFY(!runtime.prepareForRetirement(true, &reason));
        QVERIFY(runtime.backgroundStartWanted());
        QVERIFY(commands.isEmpty()); // final service start belongs to the caller after unlock.
        runtime.stop();
        QVERIFY(runtime.backgroundStartWanted());
    }

    void invalidBackgroundConfigPreventsRetirementIntent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        const QString path = serviceConfigPath(settings);
        QVERIFY(writeConfig(path, freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.setKeepRunning(true));
        QFile config(path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        config.write("remote_port = 47910\nstate_directory = /outside-profile\n");
        config.close();
        QString reason;
        QVERIFY(!runtime.prepareForRetirement(true, &reason));
        QVERIFY(reason.contains(QStringLiteral("profile"), Qt::CaseInsensitive));
        QVERIFY(!runtime.backgroundStartWanted());
        QVERIFY(!runtime.controller()->enabled());
    }

    void failedSettingsSaveCannotLatchBackgroundStart()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.setKeepRunning(true));
        const QString directory = QFileInfo(settings.filePath()).absolutePath();
        const QFileDevice::Permissions original = QFileInfo(directory).permissions();
        struct RestorePermissions {
            QString path;
            QFileDevice::Permissions original;
            ~RestorePermissions() { QFile::setPermissions(path, original); }
        } restore{directory, original};
        QVERIFY(QFile::setPermissions(directory,
                QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QString reason;
        QVERIFY(!runtime.prepareForRetirement(true, &reason));
        QVERIFY(!reason.isEmpty());
        QVERIFY(!runtime.backgroundStartWanted());
        QVERIFY(!runtime.controller()->enabled());
        QVERIFY(QFile::setPermissions(directory, original));
        reason.clear();
        QVERIFY2(runtime.prepareForRetirement(true, &reason), qPrintable(reason));
        QVERIFY(runtime.backgroundStartWanted());
    }

    void retirementSavesConnectedChoiceOnlyAndNeverStartsOnReplacement()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        QVERIFY(runtime.setRunCore(true));
        QVERIFY(runtime.setKeepRunning(true));
        RadioInfo info;
        info.macAddress = QStringLiteral("02:00:00:00:00:91");
        model.setLastRadioInfoForTest(info);
        model.setConnectionStateForTest(ConnectionState::Connected);
        QString reason;
        QVERIFY2(runtime.prepareForRetirement(false, &reason), qPrintable(reason));
        QVERIFY(!runtime.backgroundStartWanted());
        QCOMPARE(settings.value(QStringLiteral("StationRadioChoice")).toString(),
                 info.macAddress);
        QVERIFY(!runtime.controller()->enabled());
        // No newly connected radio may replace the saved choice with empty.
        model.setConnectionStateForTest(ConnectionState::Disconnected);
        GuiDesktopStationRuntime nextRun(&model, &settings, AppSettings::profileOverride(),
                                         true, service);
        QVERIFY2(nextRun.prepareForRetirement(false, &reason), qPrintable(reason));
        QCOMPARE(settings.value(QStringLiteral("StationRadioChoice")).toString(),
                 info.macAddress);
    }

    void runtimeBindsLatePageActionToActualListener()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        AppSettings& settings = AppSettings::instance();
        QVERIFY(writeConfig(serviceConfigPath(settings), freePort()));
        StationServiceOptions service;
        service.profileDirectory = QFileInfo(settings.filePath()).absolutePath();
        service.homeDirectory = temp.filePath(QStringLiteral("home"));
        service.inheritActiveProfile = false;
        service.runner = [](const QString&, const QStringList&) {
            return StationServiceCommandResult{0, {}};
        };
        RadioModel model;
        GuiDesktopStationRuntime runtime(&model, &settings, AppSettings::profileOverride(), true, service);
        SetupDialog dialog(&model);
        runtime.bindSetupDialog(&dialog);
        dialog.selectPage(QStringLiteral("Remote Access"));
        auto* page = dialog.findChild<RemoteStationPage*>();
        QVERIFY(page != nullptr);
        QVERIFY(page->state().available);
        auto* check = page->findChild<QCheckBox*>(QStringLiteral("remoteAccessRunCore"));
        QVERIFY(check != nullptr);
        check->click();
        QVERIFY(runtime.controller()->enabled());
        QVERIFY(page->state().runCore);
        check->click();
        QVERIFY(!runtime.controller()->enabled());
        QVERIFY(!page->state().runCore);
    }

    void lazyBinderCoversNewAndExistingRemotePages()
    {
        RadioModel model;
        SetupDialog dialog(&model);
        int calls = 0;
        dialog.setRemoteStationPageBinder([&calls](RemoteStationPage*) { ++calls; });
        dialog.selectPage(QStringLiteral("Remote Access"));
        QCOMPARE(calls, 1);
        QVERIFY(dialog.findChild<RemoteStationPage*>() != nullptr);
        dialog.setRemoteStationPageBinder([&calls](RemoteStationPage*) { ++calls; });
        QCOMPARE(calls, 2);
    }
};
QTEST_MAIN(TstGuiDesktopStationRuntime)
#include "tst_gui_desktop_station_runtime.moc"
