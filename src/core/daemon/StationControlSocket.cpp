// no-port-check: NereusSDR-original.
// =================================================================
// src/core/daemon/StationControlSocket.cpp  (NereusSDR)
// =================================================================
// See StationControlSocket.h.
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/daemon/StationControlSocket.h"

#include "core/AppSettings.h"
#include "core/LogCategories.h"
#include "core/daemon/DaemonConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace NereusSDR {

namespace {

QByteArray encodeReply(const StationControlReply& reply)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("ok"), reply.ok},
                                     {QStringLiteral("text"), reply.text}})
               .toJson(QJsonDocument::Compact)
           + '\n';
}

StationControlReply notReached(const QString& path, QLocalSocket::LocalSocketError error)
{
    if (error == QLocalSocket::SocketAccessError) {
        return {false, QStringLiteral("This account may not manage the Core at %1. On a packaged "
                                      "Core, run the command with sudo.")
                           .arg(path)};
    }
    return {false, QStringLiteral("No Core answered at %1. Check that nereusd is running and that "
                                  "this command has the same --config and --profile. On a "
                                  "packaged Core, run the command with sudo.")
                       .arg(path)};
}

} // namespace

StationControlSocket::StationControlSocket(Handler handler, QObject* parent)
    : QObject(parent)
    , m_handler(std::move(handler))
{
}

StationControlSocket::~StationControlSocket()
{
    close();
}

QString StationControlSocket::directoryFor(const DaemonConfig& config, const QString& profile)
{
    if (!config.stateDirectory.isEmpty()) {
        return QDir::cleanPath(config.stateDirectory);
    }
    return AppSettings::resolveConfigDir(profile);
}

QString StationControlSocket::socketPathFor(const DaemonConfig& config, const QString& profile)
{
    return QDir(directoryFor(config, profile)).filePath(QString::fromLatin1(kSocketName));
}

bool StationControlSocket::listen(const QString& path)
{
    close();
    m_path = path;
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir(directory).exists()) {
        if (!QDir().mkpath(directory)) {
            m_lastError = QStringLiteral("could not create %1").arg(directory);
            return false;
        }
        QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                             | QFileDevice::ExeOwner);
    }
    if (QFileInfo::exists(path)) {
        // Another Core answering here keeps its socket; a file left by one
        // that stopped is replaced.
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(500)) {
            probe.abort();
            m_lastError = QStringLiteral("another Core is already answering at %1").arg(path);
            return false;
        }
        QLocalServer::removeServer(path);
    }
    m_server = new QLocalServer(this);
    // Owner-only: Qt binds in a private directory, sets the mode, then
    // moves the socket to its name, so it never exists more widely open.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &StationControlSocket::onNewConnection);
    if (!m_server->listen(path)) {
        m_lastError = m_server->errorString();
        // A local socket's name has a short limit (about 100 bytes, less
        // the private directory Qt binds in first); a deep profile
        // directory can pass it.
        if (QFile::encodeName(path).size() > kLongPathBytes) {
            m_lastError += QStringLiteral(" (the path may be too long for a local socket; "
                                          "set state_directory to a shorter directory)");
        }
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_lastError.clear();
    return true;
}

void StationControlSocket::close()
{
    if (m_server != nullptr) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
        QLocalServer::removeServer(m_path);
    }
}

bool StationControlSocket::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

void StationControlSocket::onNewConnection()
{
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QLocalSocket* socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            break;
        }
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        serve(socket);
    }
}

void StationControlSocket::serve(QLocalSocket* socket)
{
    QPointer<QLocalSocket> guarded(socket);
    QTimer::singleShot(kRequestTimeoutMs, socket, [guarded]() {
        if (guarded) {
            guarded->abort();
            guarded->deleteLater();
        }
    });
    auto buffer = std::make_shared<QByteArray>();
    connect(socket, &QLocalSocket::readyRead, socket, [this, socket, buffer]() {
        buffer->append(socket->readAll());
        const qsizetype end = buffer->indexOf('\n');
        if (end < 0) {
            if (buffer->size() > kMaxRequestBytes) {
                socket->abort();
                socket->deleteLater();
            }
            return;
        }
        disconnect(socket, &QLocalSocket::readyRead, socket, nullptr);
        StationControlReply reply;
        const QJsonDocument doc = QJsonDocument::fromJson(buffer->left(end));
        const QJsonValue args = doc.object().value(QStringLiteral("args"));
        if (end > kMaxRequestBytes || !doc.isObject() || !args.isArray()) {
            reply = {false, QStringLiteral("The Core could not read that command.")};
        } else {
            QStringList list;
            for (const QJsonValue& value : args.toArray()) {
                list << value.toString();
            }
            reply = m_handler ? m_handler(list) : StationControlReply{};
        }
        socket->write(encodeReply(reply));
        socket->flush();
        socket->disconnectFromServer();
    });
}

StationControlReply StationControlSocket::request(const QString& path, const QStringList& args,
                                                  int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(timeoutMs)) {
        return notReached(path, socket.error());
    }
    const QByteArray line = QJsonDocument(QJsonObject{{QStringLiteral("args"),
                                                       QJsonArray::fromStringList(args)}})
                                .toJson(QJsonDocument::Compact)
                            + '\n';
    socket.write(line);
    if (!socket.waitForBytesWritten(timeoutMs)) {
        return notReached(path, socket.error());
    }
    QByteArray received;
    while (!received.contains('\n') && received.size() <= kMaxReplyBytes) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeoutMs)) {
            break;
        }
        received += socket.readAll();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(received.left(received.indexOf('\n')));
    if (!doc.isObject()) {
        return {false, QStringLiteral("The Core at %1 did not answer that command.").arg(path)};
    }
    return {doc.object().value(QStringLiteral("ok")).toBool(false),
            doc.object().value(QStringLiteral("text")).toString()};
}

} // namespace NereusSDR
