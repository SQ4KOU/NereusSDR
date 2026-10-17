// =================================================================
// src/core/audio/LinuxPipeControl.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original bounded module-control adapter.
//
// Modification history (NereusSDR):
//   2026-10-14 - Keep Linux VAX module inspection and unloading on one
//                server connection. J.J. Boyd (KG4VCF), AI-assisted via
//                OpenAI Codex.
// =================================================================

#pragma once

#include <QList>
#include <QString>

#include <cstdint>
#include <memory>

namespace NereusSDR {

struct LinuxPipeModule {
    uint32_t index;
    QString name;
    QString arguments;
};

// Main-thread-only control; no streams, devices, or worker thread. A control
// object connects at most once and retains that exact server connection for
// every query and unload. The caller decides which generation it owns.
// The Pulse protocol unloads by index; identity checks and the cooperative
// per-role lock belong to the caller. This is not an atomic compare/unload
// against an administrator replacing a module on the same server.
class LinuxPipeControl {
public:
    LinuxPipeControl();
    ~LinuxPipeControl();

    LinuxPipeControl(const LinuxPipeControl&) = delete;
    LinuxPipeControl& operator=(const LinuxPipeControl&) = delete;

    bool connect();
    bool modules(QList<LinuxPipeModule>& modules);
    bool unload(uint32_t index);
    QString errorString() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace NereusSDR
