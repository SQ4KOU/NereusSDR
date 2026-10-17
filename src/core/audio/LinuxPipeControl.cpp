// =================================================================
// src/core/audio/LinuxPipeControl.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original control flow using the public PulseAudio
// API declarations retained, with their licences, in PulseControlAbi.h.
//
// Modification history (NereusSDR):
//   2026-10-14 - Bounded, optional libpulse module control on one retained
//                connection. J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// =================================================================

#include "core/audio/LinuxPipeControl.h"

#include "core/audio/PulseControlAbi.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLibrary>
#include <QScopeGuard>
#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>

namespace NereusSDR {

namespace {

namespace Pulse = PulseControlAbi;

// A control request shares the existing PulseAudio engine's one-second
// connection/list budget (PulseAudioSystem.h:kPulseConnectWaitMs).
constexpr qint64 kControlTimeoutMs = 1000;
constexpr qint64 kPollSliceMs = 10;
constexpr qint64 kMicrosecondsPerMillisecond = 1000;

struct ModuleQuery {
    QList<LinuxPipeModule> m_modules;
    bool m_complete = false;
    bool m_failed = false;
};

struct UnloadRequest {
    bool m_complete = false;
    bool m_success = false;
};

void moduleInfo(Pulse::pa_context*, const Pulse::pa_module_info* info,
                int eol, void* userdata)
{
    auto* query = static_cast<ModuleQuery*>(userdata);
    if (eol < 0) {
        query->m_failed = true;
        return;
    }
    if (query->m_complete || query->m_failed) {
        query->m_failed = true;
        return;
    }
    if (eol > 0) {
        query->m_complete = true;
        return;
    }
    if (!info || !info->name || info->name[0] == '\0'
        || info->index == std::numeric_limits<uint32_t>::max()) {
        query->m_failed = true;
        return;
    }
    for (const LinuxPipeModule& module : query->m_modules) {
        if (module.index == info->index) {
            query->m_failed = true;
            return;
        }
    }
    // A module loaded with no arguments legitimately has a null argument.
    query->m_modules.append({info->index, QString::fromUtf8(info->name),
                            info->argument ? QString::fromUtf8(info->argument) : QString{}});
}

void moduleUnloaded(Pulse::pa_context*, int success, void* userdata)
{
    auto* request = static_cast<UnloadRequest*>(userdata);
    request->m_complete = true;
    request->m_success = success > 0;
}

} // namespace

class LinuxPipeControl::Impl {
public:
    ~Impl()
    {
        // All operations are cancelled/unreferenced before their public call
        // returns. Tear down the context before its mainloop and the library.
        Q_ASSERT(!m_context || isMainThread());
        if (m_context) {
            m_contextDisconnect(m_context);
            m_contextUnref(m_context);
        }
        if (m_mainloop) {
            m_mainloopFree(m_mainloop);
        }
    }

    bool connect()
    {
        if (!requireMainThread()) {
            return false;
        }
        if (m_connectAttempted) {
            return requireReady();
        }
        m_connectAttempted = true;
        if (!loadSymbols()) {
            return false;
        }
        m_mainloop = m_mainloopNew();
        if (!m_mainloop) {
            return fail(QStringLiteral("libpulse could not create the control mainloop."));
        }
        Pulse::pa_mainloop_api* const api = m_mainloopGetApi(m_mainloop);
        if (!api) {
            return fail(QStringLiteral("libpulse did not provide the control mainloop API."));
        }
        m_context = m_contextNew(api, "NereusSDR VAX ownership");
        if (!m_context) {
            return fail(QStringLiteral("libpulse could not create the control connection."));
        }
        // From PulseAudio src/pulse/context.h:201-210 [v17.0]: this flag
        // prohibits server autospawning; this object never reconnects.
        if (m_contextConnect(m_context, nullptr, Pulse::PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            return fail(QStringLiteral("The audio server refused the control connection."));
        }

        QElapsedTimer elapsed;
        elapsed.start();
        while (true) {
            const Pulse::pa_context_state_t state = m_contextGetState(m_context);
            if (state == Pulse::PA_CONTEXT_READY) {
                m_usable = true;
                m_error.clear();
                return true;
            }
            if (state != Pulse::PA_CONTEXT_CONNECTING && state != Pulse::PA_CONTEXT_AUTHORIZING
                && state != Pulse::PA_CONTEXT_SETTING_NAME) {
                return fail(QStringLiteral("The audio server control connection failed."));
            }
            if (!dispatch(elapsed)) {
                return false;
            }
        }
    }

    bool modules(QList<LinuxPipeModule>& modules)
    {
        modules.clear();
        if (!requireMainThread() || !requireReady()) {
            return false;
        }
        ModuleQuery query;
        Pulse::pa_operation* const operation = m_getModuleInfoList(m_context, moduleInfo, &query);
        if (!operation) {
            return fail(QStringLiteral("The audio server rejected the module query."));
        }
        // From PulseAudio src/pulse/operation.h:44-49 [v17.0]: cancellation
        // disables later callbacks even if the server already acted. The
        // guard runs before query's stack storage goes out of scope.
        const auto release = qScopeGuard([this, operation] {
            m_operationCancel(operation);
            m_operationUnref(operation);
        });
        QElapsedTimer elapsed;
        elapsed.start();
        while (true) {
            if (!requireReady()) {
                return false;
            }
            if (query.m_failed) {
                return fail(QStringLiteral("The audio server returned an invalid or failed module query."));
            }
            const Pulse::pa_operation_state_t state = m_operationGetState(operation);
            if (state == Pulse::PA_OPERATION_DONE) {
                if (!query.m_complete) {
                    return fail(QStringLiteral("The audio server module query was incomplete."));
                }
                modules = std::move(query.m_modules);
                m_error.clear();
                return true;
            }
            if (state != Pulse::PA_OPERATION_RUNNING) {
                return fail(QStringLiteral("The audio server module query was cancelled."));
            }
            if (!dispatch(elapsed)) {
                return false;
            }
        }
    }

    bool unload(uint32_t index)
    {
        if (!requireMainThread() || !requireReady()) {
            return false;
        }
        if (index == std::numeric_limits<uint32_t>::max()) {
            return fail(QStringLiteral("The audio server module index is invalid."));
        }
        UnloadRequest request;
        Pulse::pa_operation* const operation = m_unloadModule(m_context, index, moduleUnloaded, &request);
        if (!operation) {
            return fail(QStringLiteral("The audio server rejected the module unload request."));
        }
        const auto release = qScopeGuard([this, operation] {
            m_operationCancel(operation);
            m_operationUnref(operation);
        });
        QElapsedTimer elapsed;
        elapsed.start();
        while (true) {
            if (!requireReady()) {
                return false;
            }
            const Pulse::pa_operation_state_t state = m_operationGetState(operation);
            if (state == Pulse::PA_OPERATION_DONE) {
                if (!request.m_complete || !request.m_success) {
                    return fail(QStringLiteral("The audio server did not confirm the module unload."));
                }
                m_error.clear();
                return true;
            }
            if (state != Pulse::PA_OPERATION_RUNNING) {
                return fail(QStringLiteral("The audio server module unload was cancelled."));
            }
            if (!dispatch(elapsed)) {
                return false;
            }
        }
    }

    QString errorString() const { return m_error; }

private:
    static bool isMainThread()
    {
        const QCoreApplication* const application = QCoreApplication::instance();
        return application && QThread::currentThread() == application->thread();
    }

    bool requireMainThread()
    {
        if (!isMainThread()) {
            return fail(QStringLiteral("Linux VAX module control requires the main application thread."));
        }
        return true;
    }

    bool fail(const QString& message)
    {
        m_error = message;
        m_usable = false;
        return false;
    }

    bool requireReady()
    {
        if (!m_context || !m_usable) {
            if (m_error.isEmpty()) {
                m_error = QStringLiteral("The audio server control connection is not available.");
            }
            return false;
        }
        if (m_contextGetState(m_context) != Pulse::PA_CONTEXT_READY) {
            return fail(QStringLiteral("The audio server control connection was lost; no reconnect was attempted."));
        }
        return true;
    }

    bool dispatch(const QElapsedTimer& elapsed)
    {
        const qint64 remainingMs = kControlTimeoutMs - elapsed.elapsed();
        if (remainingMs <= 0) {
            return fail(QStringLiteral("The audio server control request timed out."));
        }
        // From PulseAudio src/pulse/mainloop.h:86-96 [v17.0]: prepare's
        // timeout is MICROSECONDS. Every poll receives a finite maximum.
        const int timeoutUs = static_cast<int>(std::min(remainingMs, kPollSliceMs)
                                               * kMicrosecondsPerMillisecond);
        if (m_mainloopPrepare(m_mainloop, timeoutUs) < 0
            || m_mainloopPoll(m_mainloop) < 0 || m_mainloopDispatch(m_mainloop) < 0) {
            return fail(QStringLiteral("The audio server control mainloop failed."));
        }
        return true;
    }

    template<typename Function>
    bool resolve(Function& function, const char* symbol)
    {
        function = reinterpret_cast<Function>(m_library.resolve(symbol));
        if (!function) {
            return fail(QStringLiteral("libpulse is missing the required control symbol %1.")
                            .arg(QString::fromLatin1(symbol)));
        }
        return true;
    }

    bool loadSymbols()
    {
        if (!m_library.load()) {
            return fail(QStringLiteral("libpulse control is unavailable: %1").arg(m_library.errorString()));
        }
        // Resolve the complete surface before creating a libpulse resource.
        return resolve(m_mainloopNew, "pa_mainloop_new")
            && resolve(m_mainloopFree, "pa_mainloop_free")
            && resolve(m_mainloopGetApi, "pa_mainloop_get_api")
            && resolve(m_mainloopPrepare, "pa_mainloop_prepare")
            && resolve(m_mainloopPoll, "pa_mainloop_poll")
            && resolve(m_mainloopDispatch, "pa_mainloop_dispatch")
            && resolve(m_contextNew, "pa_context_new")
            && resolve(m_contextUnref, "pa_context_unref")
            && resolve(m_contextConnect, "pa_context_connect")
            && resolve(m_contextDisconnect, "pa_context_disconnect")
            && resolve(m_contextGetState, "pa_context_get_state")
            && resolve(m_getModuleInfoList, "pa_context_get_module_info_list")
            && resolve(m_unloadModule, "pa_context_unload_module")
            && resolve(m_operationCancel, "pa_operation_cancel")
            && resolve(m_operationUnref, "pa_operation_unref")
            && resolve(m_operationGetState, "pa_operation_get_state");
    }

    // QLibrary keeps the existing runtime dependency optional even when its
    // development headers were absent when NereusSDR was built.
    QLibrary m_library{QStringLiteral("pulse"), 0};
    decltype(&Pulse::pa_mainloop_new) m_mainloopNew = nullptr;
    decltype(&Pulse::pa_mainloop_free) m_mainloopFree = nullptr;
    decltype(&Pulse::pa_mainloop_get_api) m_mainloopGetApi = nullptr;
    decltype(&Pulse::pa_mainloop_prepare) m_mainloopPrepare = nullptr;
    decltype(&Pulse::pa_mainloop_poll) m_mainloopPoll = nullptr;
    decltype(&Pulse::pa_mainloop_dispatch) m_mainloopDispatch = nullptr;
    decltype(&Pulse::pa_context_new) m_contextNew = nullptr;
    decltype(&Pulse::pa_context_unref) m_contextUnref = nullptr;
    decltype(&Pulse::pa_context_connect) m_contextConnect = nullptr;
    decltype(&Pulse::pa_context_disconnect) m_contextDisconnect = nullptr;
    decltype(&Pulse::pa_context_get_state) m_contextGetState = nullptr;
    decltype(&Pulse::pa_context_get_module_info_list) m_getModuleInfoList = nullptr;
    decltype(&Pulse::pa_context_unload_module) m_unloadModule = nullptr;
    decltype(&Pulse::pa_operation_cancel) m_operationCancel = nullptr;
    decltype(&Pulse::pa_operation_unref) m_operationUnref = nullptr;
    decltype(&Pulse::pa_operation_get_state) m_operationGetState = nullptr;
    Pulse::pa_mainloop* m_mainloop = nullptr;
    Pulse::pa_context* m_context = nullptr;
    bool m_connectAttempted = false;
    bool m_usable = false;
    QString m_error;
};

LinuxPipeControl::LinuxPipeControl()
    : m_impl(std::make_unique<Impl>())
{}

LinuxPipeControl::~LinuxPipeControl() = default;

bool LinuxPipeControl::connect()
{
    return m_impl->connect();
}

bool LinuxPipeControl::modules(QList<LinuxPipeModule>& modules)
{
    return m_impl->modules(modules);
}

bool LinuxPipeControl::unload(uint32_t index)
{
    return m_impl->unload(index);
}

QString LinuxPipeControl::errorString() const
{
    return m_impl->errorString();
}

} // namespace NereusSDR
