// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 J.J. Boyd (KG4VCF)
// no-port-check: NereusSDR-original Linux VAX ownership transaction.
// Modification history (NereusSDR):
//   2026-10-14: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <memory>

namespace NereusSDR {

// Main-thread only. The permanent role lock covers all module and FIFO work.
// An unresolved generation remains journalled when an operation is ambiguous.
class LinuxPipeOwnership {
public:
    explicit LinuxPipeOwnership(int role);
    ~LinuxPipeOwnership();
    LinuxPipeOwnership(const LinuxPipeOwnership&) = delete;
    LinuxPipeOwnership& operator=(const LinuxPipeOwnership&) = delete;

    bool begin();
    bool markLoadAttempt();
    bool cancelUnstartedLoad();
    bool captureFifo();
    bool acceptFd(int fd);
    bool confirmModule(std::uint32_t index);
    bool cleanup();
    bool retire();
    QByteArray pipePath() const;
    QString errorString() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace NereusSDR
