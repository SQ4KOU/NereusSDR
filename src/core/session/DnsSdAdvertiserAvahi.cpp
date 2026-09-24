// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/DnsSdAdvertiserAvahi.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 16 (D36): the Bonjour backend on Linux, through the
// Avahi daemon's D-Bus interface (org.freedesktop.Avahi) with Qt6::DBus.
// The Rock and Pi 4 builder image has Qt's D-Bus module but no Avahi
// development package, and every machine that runs nereusd already has
// libQt6DBus (libQt6Network depends on it), so this adds no package to the
// build or the Core. It needs avahi-daemon running; without it,
// isAvailable() is false and the Core says so once.
//
// The calls, as Avahi's D-Bus API defines them:
//   Server     EntryGroupNew() -> o, GetAlternativeServiceName(s) -> s
//   EntryGroup AddService(i interface, i protocol, u flags, s name,
//                         s type, s domain, s host, q port, aay txt),
//              UpdateServiceTxt(i, i, u, s name, s type, s domain, aay),
//              Commit(), Free(), signal StateChanged(i state, s error)
// with -1 for "any interface" and "any protocol". An entry group belongs
// to the D-Bus connection that made it, so the daemon withdraws the
// service if the Core goes away.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "DnsSdAdvertiser.h"

#include "core/LogCategories.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QList>

namespace NereusSDR {
namespace {

const QString kAvahiService = QStringLiteral("org.freedesktop.Avahi");
const QString kAvahiServerInterface = QStringLiteral("org.freedesktop.Avahi.Server");
const QString kAvahiEntryGroupInterface = QStringLiteral("org.freedesktop.Avahi.EntryGroup");
constexpr int kAvahiUnspecified = -1;
// AvahiEntryGroupState.
constexpr int kAvahiGroupCollision = 3;
constexpr int kAvahiGroupFailure = 4;
constexpr int kMaxRenames = 10;

QList<QByteArray> avahiTxt(const DnsSdTxtEntries& txt)
{
    QList<QByteArray> strings;
    for (const auto& [key, value] : txt) {
        strings.append(key + '=' + value);
    }
    return strings;
}

class AvahiEntryGroupWatcher : public QObject {
    Q_OBJECT

public:
    std::function<void(int state, const QString& error)> onState;

public slots:
    void stateChanged(int state, const QString& error)
    {
        if (onState) {
            onState(state, error);
        }
    }
};

class AvahiDnsSdBackend final : public DnsSdBackend {
public:
    AvahiDnsSdBackend() { qDBusRegisterMetaType<QList<QByteArray>>(); }
    ~AvahiDnsSdBackend() override { unregisterService(); }

    bool isAvailable() override
    {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!bus.isConnected() || bus.interface() == nullptr) {
            return false;
        }
        const QDBusReply<bool> registered = bus.interface()->isServiceRegistered(kAvahiService);
        return registered.isValid() && registered.value();
    }

    bool registerService(quint16 port, const DnsSdRecord& record,
                         const DnsSdTxtEntries& txt) override
    {
        unregisterService();
        if (record.interfaceIndex == kDnsSdThisComputerOnly) {
            // Avahi has no scope narrower than an interface.
            return false;
        }
        QDBusConnection bus = QDBusConnection::systemBus();
        const QDBusReply<QDBusObjectPath> group = bus.call(QDBusMessage::createMethodCall(
            kAvahiService, QStringLiteral("/"), kAvahiServerInterface,
            QStringLiteral("EntryGroupNew")));
        if (!group.isValid()) {
            qCWarning(lcDiscovery).noquote() << "Avahi refused a new entry group:"
                                             << group.error().message();
            return false;
        }
        m_group = group.value().path();
        m_port = port;
        m_record = record;
        m_txt = avahiTxt(txt);
        m_name = record.instanceName;
        m_renames = 0;
        m_watcher = std::make_unique<AvahiEntryGroupWatcher>();
        m_watcher->onState = [this](int state, const QString& error) { onState(state, error); };
        bus.connect(kAvahiService, m_group, kAvahiEntryGroupInterface,
                    QStringLiteral("StateChanged"), m_watcher.get(),
                    SLOT(stateChanged(int, QString)));
        if (!addAndCommit()) {
            unregisterService();
            return false;
        }
        return true;
    }

    bool updateTxt(const DnsSdRecord& record, const DnsSdTxtEntries& txt) override
    {
        if (m_group.isEmpty()) {
            return false;
        }
        const QList<QByteArray> strings = avahiTxt(txt);
        const QDBusMessage reply = QDBusConnection::systemBus().call(groupCall(
            QStringLiteral("UpdateServiceTxt"),
            {interfaceArgument(), kAvahiUnspecified, 0u, m_name,
             QString::fromLatin1(kDnsSdServiceType), QString(),
             QVariant::fromValue(strings)}));
        if (reply.type() == QDBusMessage::ErrorMessage) {
            return false;
        }
        m_record = record;
        m_txt = strings;
        return true;
    }

    void unregisterService() override
    {
        if (m_group.isEmpty()) {
            return;
        }
        QDBusConnection bus = QDBusConnection::systemBus();
        bus.disconnect(kAvahiService, m_group, kAvahiEntryGroupInterface,
                       QStringLiteral("StateChanged"), m_watcher.get(),
                       SLOT(stateChanged(int, QString)));
        bus.call(groupCall(QStringLiteral("Free"), {}));
        m_group.clear();
        if (m_watcher) {
            m_watcher->onState = nullptr;
            m_watcher.release()->deleteLater();
        }
    }

private:
    QDBusMessage groupCall(const QString& method, const QVariantList& arguments) const
    {
        QDBusMessage message = QDBusMessage::createMethodCall(kAvahiService, m_group,
                                                              kAvahiEntryGroupInterface, method);
        message.setArguments(arguments);
        return message;
    }

    int interfaceArgument() const
    {
        return m_record.interfaceIndex == kDnsSdAllInterfaces
            ? kAvahiUnspecified
            : static_cast<int>(m_record.interfaceIndex);
    }

    bool addAndCommit()
    {
        QDBusConnection bus = QDBusConnection::systemBus();
        const QDBusMessage added = bus.call(groupCall(
            QStringLiteral("AddService"),
            {interfaceArgument(), kAvahiUnspecified, 0u, m_name,
             QString::fromLatin1(kDnsSdServiceType), QString(), QString(),
             QVariant::fromValue(static_cast<ushort>(m_port)), QVariant::fromValue(m_txt)}));
        if (added.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcDiscovery).noquote() << "Avahi refused the service:"
                                             << added.errorMessage();
            return false;
        }
        const QDBusMessage committed = bus.call(groupCall(QStringLiteral("Commit"), {}));
        if (committed.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcDiscovery).noquote() << "Avahi refused to publish the service:"
                                             << committed.errorMessage();
            return false;
        }
        qCInfo(lcDiscovery).noquote() << "Bonjour: advertising" << m_name << "as"
                                      << kDnsSdServiceType;
        return true;
    }

    void onState(int state, const QString& error)
    {
        if (state == kAvahiGroupCollision && m_renames < kMaxRenames) {
            // Another computer holds the name: take Avahi's next one
            // ("Name #2") and publish again.
            QDBusConnection bus = QDBusConnection::systemBus();
            QDBusMessage call = QDBusMessage::createMethodCall(
                kAvahiService, QStringLiteral("/"), kAvahiServerInterface,
                QStringLiteral("GetAlternativeServiceName"));
            call.setArguments({m_name});
            const QDBusReply<QString> alternative = bus.call(call);
            ++m_renames;
            if (alternative.isValid()) {
                m_name = alternative.value();
                bus.call(groupCall(QStringLiteral("Reset"), {}));
                if (addAndCommit()) {
                    return;
                }
            }
        }
        if (state != kAvahiGroupCollision && state != kAvahiGroupFailure) {
            return;
        }
        const QString reason = state == kAvahiGroupCollision
            ? QStringLiteral("Avahi could not find a free name for the service.")
            : QStringLiteral("Avahi could not publish the service: %1").arg(error);
        unregisterService();
        if (failed) {
            failed(reason);
        }
    }

    QString m_group;
    QString m_name;
    quint16 m_port = 0;
    DnsSdRecord m_record;
    QList<QByteArray> m_txt;
    int m_renames = 0;
    std::unique_ptr<AvahiEntryGroupWatcher> m_watcher;
};

} // namespace

std::unique_ptr<DnsSdBackend> createPlatformDnsSdBackend()
{
    return std::make_unique<AvahiDnsSdBackend>();
}

} // namespace NereusSDR

#include "DnsSdAdvertiserAvahi.moc"
