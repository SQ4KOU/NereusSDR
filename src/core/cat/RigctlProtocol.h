// Ported from AetherSDR src/core/RigctlProtocol.h [@1e0718ad].
// AetherSDR project attribution: Jeremy (KK7GWY), primary author, and contributors.
// https://github.com/ten9876/AetherSDR — GNU GPL v3, project LICENSE applies.
// Upstream source has no top-of-file GPL header; no notice is fabricated.
// Modification history (NereusSDR):
// 2026-10-04 - Slice-bound wire contracts adapted by J.J. Boyd (KG4VCF),
//              AI-assisted via OpenAI Codex. Native lifecycle/authority guards.
// 2026-10-06 - q and Q answer RPRT 0 and close the rigctld client, as Hamlib
//              4.7.2 rigctld does. J.J. Boyd (KG4VCF), AI-assisted via
//              Anthropic Claude Code.
// 2026-10-15 - Reads and changes through a RigctlTarget, so a remote window's
//              rigctld uses this parser too. J.J. Boyd (KG4VCF), AI-assisted
//              via Anthropic Claude Code.
#pragma once
#include "core/NereusCoreExport.h"
#include "CatTypes.h"
#include <QStringList>
#include <memory>
#include <utility>
namespace NereusSDR {
class CatModelAdapter; class CatTxCoordinator; class RigctlTarget;
// Pure protocol handler for Hamlib rigctld emulation.
// No I/O — receives a text line, returns the response string.
// Shared by both the TCP server and the PTY virtual serial port.
// [original inline comment from RigctlProtocol.h:14-16]
class NEREUS_CORE_EXPORT RigctlProtocol {
public:
    RigctlProtocol(CatModelAdapter&, CatTxCoordinator&, int channel, quint64 sessionId);
    // Any other target, such as a remote window's (src/gui/RemoteRigctlServer).
    explicit RigctlProtocol(std::shared_ptr<RigctlTarget>);
    QString handleLine(const QString&);
    void reset() { m_vfo = CatVfo::Primary; }
    // True once after q or Q; the TCP listener then closes this client.
    bool takeQuit() { return std::exchange(m_quit, false); }
private:
    bool live() const;
    int writeError(CatVfo, const QByteArray&) const;
    std::shared_ptr<RigctlTarget> m_target;
    CatVfo m_vfo{CatVfo::Primary};
    bool m_quit{false};
};
} // namespace NereusSDR
