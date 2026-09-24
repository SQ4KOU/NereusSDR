// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/DnsSdAdvertiserWindows.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 16 (D36): the Bonjour backend on Windows, through
// the DNS-SD functions Windows ships in dnsapi.dll (DnsServiceRegister and
// its companions, Windows 10 1903 and later). They are looked up at run
// time, so a Core on an older Windows starts and reports Bonjour as not
// available rather than failing to load. Nothing is added to the build.
//
// DnsServiceRegister completes on a thread of Windows' own; the completion
// is handed to the advertiser's thread before anything reads it. Changing
// the TXT record registers the service again (Windows has no call that
// replaces it in place).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

// The DNS-SD declarations in windns.h need Windows 10. This file is built
// without the precompiled header (CMakeLists.txt), so these come first.
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
// NTDDI_WIN10_19H1 (1903), the first release with DnsServiceRegister,
// in case the header guards on the release rather than on Windows 10.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000007
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>
#include <windns.h>

#include "DnsSdAdvertiser.h"

#include "core/LogCategories.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QMetaObject>

#include <memory>
#include <string>
#include <vector>

namespace NereusSDR {
namespace {

using ConstructInstanceFn = PDNS_SERVICE_INSTANCE(WINAPI*)(PCWSTR, PCWSTR, PIP4_ADDRESS,
                                                           PIP6_ADDRESS, WORD, WORD, WORD,
                                                           DWORD, PCWSTR*, PCWSTR*);
using RegisterFn = DWORD(WINAPI*)(PDNS_SERVICE_REGISTER_REQUEST, PDNS_SERVICE_CANCEL);
using DeRegisterFn = DWORD(WINAPI*)(PDNS_SERVICE_REGISTER_REQUEST, PDNS_SERVICE_CANCEL);
using FreeInstanceFn = VOID(WINAPI*)(PDNS_SERVICE_INSTANCE);

struct DnsApi {
    ConstructInstanceFn construct = nullptr;
    RegisterFn registerService = nullptr;
    DeRegisterFn deregisterService = nullptr;
    FreeInstanceFn freeInstance = nullptr;

    bool usable() const
    {
        return construct != nullptr && registerService != nullptr
            && deregisterService != nullptr && freeInstance != nullptr;
    }
};

const DnsApi& dnsApi()
{
    static const DnsApi api = [] {
        DnsApi result;
        HMODULE module = LoadLibraryExW(L"dnsapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr) {
            return result;
        }
        result.construct = reinterpret_cast<ConstructInstanceFn>(
            GetProcAddress(module, "DnsServiceConstructInstance"));
        result.registerService = reinterpret_cast<RegisterFn>(
            GetProcAddress(module, "DnsServiceRegister"));
        result.deregisterService = reinterpret_cast<DeRegisterFn>(
            GetProcAddress(module, "DnsServiceDeRegister"));
        result.freeInstance = reinterpret_cast<FreeInstanceFn>(
            GetProcAddress(module, "DnsServiceFreeInstance"));
        return result;
    }();
    return api;
}

// What Windows' completion thread may touch: a weak reference to the
// backend's completion handler, which only the application's thread
// follows (the backend lives there), so the backend can go away at any
// moment without a race.
struct CompletionHandler {
    std::function<void(DWORD status, quint64 generation)> handle;
};

struct Completion {
    std::weak_ptr<CompletionHandler> handler;
    quint64 generation = 0;
};

VOID WINAPI onRegisterComplete(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
    if (instance != nullptr && dnsApi().freeInstance != nullptr) {
        dnsApi().freeInstance(instance);
    }
    const std::unique_ptr<Completion> completion(static_cast<Completion*>(context));
    QCoreApplication* application = QCoreApplication::instance();
    if (!completion || application == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(
        application,
        [handler = completion->handler, generation = completion->generation, status]() {
            if (const std::shared_ptr<CompletionHandler> alive = handler.lock()) {
                alive->handle(status, generation);
            }
        },
        Qt::QueuedConnection);
}

class WindowsDnsSdBackend final : public DnsSdBackend {
public:
    WindowsDnsSdBackend()
        : m_handler(std::make_shared<CompletionHandler>())
    {
        m_handler->handle = [this](DWORD status, quint64 generation) {
            onCompleted(status, generation);
        };
    }
    ~WindowsDnsSdBackend() override { unregisterService(); }

    bool isAvailable() override { return dnsApi().usable(); }

    bool registerService(quint16 port, const DnsSdRecord& record,
                         const DnsSdTxtEntries& txt) override
    {
        unregisterService();
        const DnsApi& api = dnsApi();
        if (!api.usable() || record.interfaceIndex == kDnsSdThisComputerOnly) {
            return false;
        }
        const std::wstring service = (record.instanceName + QLatin1Char('.')
                                      + QString::fromLatin1(kDnsSdServiceType)
                                      + QStringLiteral(".local"))
                                         .toStdWString();
        const std::wstring host =
            (QHostInfo::localHostName() + QStringLiteral(".local")).toStdWString();
        std::vector<std::wstring> keys;
        std::vector<std::wstring> values;
        for (const auto& [key, value] : txt) {
            keys.push_back(QString::fromLatin1(key).toStdWString());
            values.push_back(QString::fromLatin1(value).toStdWString());
        }
        std::vector<PCWSTR> keyPointers;
        std::vector<PCWSTR> valuePointers;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            keyPointers.push_back(keys[i].c_str());
            valuePointers.push_back(values[i].c_str());
        }
        m_instance = api.construct(service.c_str(), host.c_str(), nullptr, nullptr, port, 0, 0,
                                   static_cast<DWORD>(keys.size()), keyPointers.data(),
                                   valuePointers.data());
        if (m_instance == nullptr) {
            return false;
        }
        auto completion = std::make_unique<Completion>();
        completion->handler = m_handler;
        completion->generation = ++m_generation;
        m_request = {};
        m_request.Version = DNS_QUERY_REQUEST_VERSION1;
        m_request.InterfaceIndex = record.interfaceIndex;
        m_request.pServiceInstance = m_instance;
        m_request.pRegisterCompletionCallback = &onRegisterComplete;
        m_request.pQueryContext = completion.get();
        m_request.unicastEnabled = FALSE;
        if (api.registerService(&m_request, nullptr) != DNS_REQUEST_PENDING) {
            api.freeInstance(m_instance);
            m_instance = nullptr;
            return false;
        }
        completion.release(); // onRegisterComplete owns it now
        m_registered = true;
        qCInfo(lcDiscovery).noquote() << "Bonjour: advertising" << record.instanceName << "as"
                                      << kDnsSdServiceType;
        return true;
    }

    bool updateTxt(const DnsSdRecord&, const DnsSdTxtEntries&) override { return false; }

    void unregisterService() override
    {
        ++m_generation;
        const DnsApi& api = dnsApi();
        if (m_registered && api.usable()) {
            // The deregistration's own completion carries no context of
            // ours: the one the registration left, if it has not run yet,
            // frees itself when it does.
            m_request.pQueryContext = nullptr;
            api.deregisterService(&m_request, nullptr);
        }
        m_registered = false;
        if (m_instance != nullptr && api.freeInstance != nullptr) {
            api.freeInstance(m_instance);
        }
        m_instance = nullptr;
    }

private:
    void onCompleted(DWORD status, quint64 generation)
    {
        // A registration withdrawn since (unregisterService moves the
        // generation on) no longer speaks for this one.
        if (!m_registered || generation != m_generation || status == ERROR_SUCCESS) {
            return;
        }
        const QString reason =
            QStringLiteral("Windows could not publish the service (%1).").arg(status);
        unregisterService();
        if (failed) {
            failed(reason);
        }
    }

    std::shared_ptr<CompletionHandler> m_handler;
    DNS_SERVICE_REGISTER_REQUEST m_request{};
    PDNS_SERVICE_INSTANCE m_instance = nullptr;
    quint64 m_generation = 0;
    bool m_registered = false;
};

} // namespace

std::unique_ptr<DnsSdBackend> createPlatformDnsSdBackend()
{
    return std::make_unique<WindowsDnsSdBackend>();
}

} // namespace NereusSDR
