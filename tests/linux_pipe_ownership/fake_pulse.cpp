// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 J.J. Boyd (KG4VCF)
// no-port-check: NereusSDR-original fixture boundary double; uses the public
// declarations (and retained upstream licences) in PulseControlAbi.h.
// 2026-10-14: J.J. Boyd, AI-assisted via OpenAI Codex.
#include "core/audio/PulseControlAbi.h"
#include "fixture_config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool ownedPath(const QString& path)
{
    const QString root = QFileInfo(QString::fromUtf8(kFixtureRoot)).canonicalFilePath();
    const QString parent = QFileInfo(path).dir().canonicalPath();
    return !root.isEmpty() && (parent == root || parent.startsWith(root + '/'));
}

class LockedState {
public:
    LockedState()
    {
        m_path = qEnvironmentVariable("NEREUS_PIPE_FIXTURE_STATE");
        m_log = qEnvironmentVariable("NEREUS_PIPE_FIXTURE_LOG");
        if (!ownedPath(m_path) || !ownedPath(m_log)) {
            qFatal("Fake libpulse refuses state/log outside owned root");
        }
        const QByteArray lockPath = QFile::encodeName(m_path + ".lock");
        m_fd = ::open(lockPath.constData(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (m_fd < 0 || ::flock(m_fd, LOCK_EX) != 0) {
            qFatal("Fake libpulse could not acquire private state lock");
        }
        QFile input(m_path);
        if (!input.open(QIODevice::ReadOnly)) {
            qFatal("Fake libpulse cannot read private state");
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            qFatal("Fake libpulse private state is invalid");
        }
        state = document.object();
    }

    ~LockedState()
    {
        if (m_fd >= 0) {
            ::flock(m_fd, LOCK_UN);
            ::close(m_fd);
        }
    }

    bool save()
    {
        QSaveFile output(m_path);
        const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
        return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
    }

    void log(const QJsonObject& entry)
    {
        QFile output(m_log);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Append)) {
            qFatal("Fake libpulse cannot append private transcript");
        }
        output.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
    }

    QJsonObject state;

private:
    QString m_path;
    QString m_log;
    int m_fd = -1;
};
} // namespace

namespace NereusSDR::PulseControlAbi {

struct pa_mainloop_api {};
struct pa_mainloop {
    pa_mainloop_api api;
    int timeoutUs = 0;
};
struct pa_context {
    int epoch = 0;
    pa_context_state_t state = PA_CONTEXT_UNCONNECTED;
    QHash<QString, int> readers;
};
struct pa_operation {
    pa_operation_state_t state = PA_OPERATION_DONE;
};

extern "C" {
pa_mainloop* pa_mainloop_new()
{
    return std::make_unique<pa_mainloop>().release();
}

void pa_mainloop_free(pa_mainloop* loop)
{
    const std::unique_ptr<pa_mainloop> owner(loop);
}

pa_mainloop_api* pa_mainloop_get_api(pa_mainloop* loop)
{
    return &loop->api;
}

int pa_mainloop_prepare(pa_mainloop* loop, int timeout)
{
    if (timeout < 0 || timeout > 10000) {
        return -1;
    }
    loop->timeoutUs = timeout;
    return 0;
}

int pa_mainloop_poll(pa_mainloop* loop)
{
    return ::poll(nullptr, 0, (loop->timeoutUs + 999) / 1000);
}

int pa_mainloop_dispatch(pa_mainloop*)
{
    return 0;
}

pa_context* pa_context_new(pa_mainloop_api*, const char*)
{
    return std::make_unique<pa_context>().release();
}

int pa_context_connect(pa_context* context, const char*, pa_context_flags_t flags, const pa_spawn_api*)
{
    LockedState data;
    data.log({{"pulse", "connect"}, {"noautospawn", (flags & PA_CONTEXT_NOAUTOSPAWN) != 0}});
    if ((flags & PA_CONTEXT_NOAUTOSPAWN) == 0 || data.state.value("connect_result") == "failure") {
        context->state = PA_CONTEXT_FAILED;
        return -1;
    }
    context->epoch = data.state.value("epoch").toInt(1);
    context->state = data.state.value("connect_result") == "timeout" ? PA_CONTEXT_CONNECTING : PA_CONTEXT_READY;
    return 0;
}

pa_context_state_t pa_context_get_state(const pa_context* context)
{
    LockedState data;
    if (data.state.value("epoch").toInt(1) != context->epoch || data.state.value("context_failure").toBool()) {
        return PA_CONTEXT_FAILED;
    }
    return context->state;
}

void pa_context_disconnect(pa_context* context)
{
    context->state = PA_CONTEXT_TERMINATED;
}

void pa_context_unref(pa_context* context)
{
    const std::unique_ptr<pa_context> owner(context);
    for (const int fd : context->readers) {
        ::close(fd);
    }
}

pa_operation* pa_context_get_module_info_list(pa_context* context, pa_module_info_cb_t callback, void* userdata)
{
    LockedState data;
    data.log({{"pulse", "list"}, {"epoch", context->epoch}});
    std::unique_ptr<pa_operation> operation = std::make_unique<pa_operation>();
    const QString result = data.state.value("query_result").toString("normal");
    if (result == "timeout") {
        operation->state = PA_OPERATION_RUNNING;
        return operation.release();
    }
    if (result == "failure") {
        callback(context, nullptr, -1, userdata);
        return operation.release();
    }
    for (const QJsonValue& value : data.state.value("modules").toArray()) {
        const QJsonObject object = value.toObject();
        const QByteArray name = object.value("name").toString().toUtf8();
        QStringList arguments;
        for (const QJsonValue& arg : object.value("arguments").toArray()) {
            arguments.append(arg.toString());
        }
        // The command stub creates the FIFO; retaining a real duplex reader
        // here models the RX module opening its read side before bus open().
        if (name == "module-pipe-source") {
            for (const QString& argument : arguments) {
                if (argument.startsWith("file=")) {
                    // Stub metadata retains its parsed file value; hand-seeded
                    // unrelated modules use an unquoted, literal path.
                    const QString path = object.value("file").toString(argument.mid(5));
                    if (!ownedPath(path)) {
                        qFatal("Fake libpulse refuses an RX FIFO outside owned root");
                    }
                    if (!context->readers.contains(path)) {
                        const QByteArray encoded = QFile::encodeName(path);
                        struct stat info {};
                        if (::lstat(encoded.constData(), &info) == 0 && S_ISFIFO(info.st_mode)) {
                            const int fd = ::open(encoded.constData(), O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
                            if (fd >= 0) {
                                context->readers.insert(path, fd);
                            }
                        }
                    }
                }
            }
        }
        const QByteArray argument = arguments.join(' ').toUtf8();
        const pa_module_info info{static_cast<std::uint32_t>(object.value("index").toInteger()),
                                  name.constData(), argument.constData(), 0, 0, nullptr};
        callback(context, &info, 0, userdata);
    }
    if (result != "incomplete") {
        callback(context, nullptr, 1, userdata);
    }
    return operation.release();
}

pa_operation* pa_context_unload_module(pa_context* context, std::uint32_t index,
                                     pa_context_success_cb_t callback, void* userdata)
{
    LockedState data;
    data.log({{"pulse", "unload"}, {"index", static_cast<qint64>(index)}, {"epoch", context->epoch}});
    std::unique_ptr<pa_operation> operation = std::make_unique<pa_operation>();
    if (data.state.value("epoch").toInt(1) != context->epoch) {
        context->state = PA_CONTEXT_FAILED;
        callback(context, 0, userdata);
        return operation.release();
    }
    if (data.state.value("unload_failure").toBool()) {
        callback(context, 0, userdata);
        return operation.release();
    }
    if (data.state.value("unload_result") == "timeout") {
        operation->state = PA_OPERATION_RUNNING;
        return operation.release();
    }
    QJsonArray remaining;
    bool found = false;
    for (const QJsonValue& value : data.state.value("modules").toArray()) {
        if (value.toObject().value("index").toInteger(-1) == index) {
            found = true;
        } else {
            remaining.append(value);
        }
    }
    data.state.insert("modules", remaining);
    callback(context, found && data.save() ? 1 : 0, userdata);
    return operation.release();
}

pa_operation_state_t pa_operation_get_state(const pa_operation* operation)
{
    return operation->state;
}

void pa_operation_cancel(pa_operation* operation)
{
    if (operation->state == PA_OPERATION_RUNNING) {
        operation->state = PA_OPERATION_CANCELLED;
    }
}

void pa_operation_unref(pa_operation* operation)
{
    const std::unique_ptr<pa_operation> owner(operation);
}
} // extern "C"
} // namespace NereusSDR::PulseControlAbi
