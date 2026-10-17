// =================================================================
// src/core/cat/RigctlTarget.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. What RigctlProtocol reads and changes,
// so one Hamlib parser serves the Core's CAT channels and a remote window.
//
// The Core's channels act as the station on a frozen slice binding
// (CoreRigctlTarget, below). A remote window's rigctld acts as that window
// on the slice it transmits on, and keys through the Core's remote transmit
// (src/gui/RemoteRigctlServer).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created for the remote window's rigctld. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================
#pragma once

#include "CatModelAdapter.h"
#include "CatTypes.h"

#include <QByteArray>
#include <QPointer>

namespace NereusSDR {

class CatService;
class CatTxCoordinator;
class RadioModel;
class SliceModel;

class RigctlTarget {
public:
    virtual ~RigctlTarget() = default;

    /// The model and the target are still there to answer.
    virtual bool live() const = 0;
    /// The slice id VFO A or VFO B stands for now, or -1.
    virtual int sliceIdOf(CatVfo vfo) const = 0;
    virtual SliceModel* resolveSlice(CatVfo vfo) const = 0;
    virtual bool mayRead(CatVfo vfo) const = 0;
    virtual bool mayChange(CatVfo vfo, const QByteArray& property) const = 0;
    /// A change is refused because the slice is on the air for this
    /// target's own key (RPRT -9), not for lack of authority (RPRT -22).
    virtual bool refusedOnAir(CatVfo vfo) const = 0;
    virtual CatWriteToken prepareWrite(CatVfo vfo, const QByteArray& property) const = 0;
    virtual bool revalidateWrite(const CatWriteToken& token) const = 0;
    /// The slice the radio transmits on, or -1.
    virtual int txSliceId() const = 0;
    /// What get_ptt reports.
    virtual bool transmitting() const = 0;
    virtual bool requestTxSelection(int sliceId) = 0;
    virtual bool requestPtt(int sliceId) = 0;
    virtual void releasePtt() = 0;
};

/// The Core's CAT channel: the station, on the channel's slice binding.
class CoreRigctlTarget final : public RigctlTarget {
public:
    CoreRigctlTarget(CatModelAdapter& adapter, CatTxCoordinator& coordinator, int channel,
                     quint64 sessionId);

    bool live() const override;
    int sliceIdOf(CatVfo vfo) const override;
    SliceModel* resolveSlice(CatVfo vfo) const override;
    bool mayRead(CatVfo vfo) const override;
    bool mayChange(CatVfo vfo, const QByteArray& property) const override;
    bool refusedOnAir(CatVfo vfo) const override;
    CatWriteToken prepareWrite(CatVfo vfo, const QByteArray& property) const override;
    bool revalidateWrite(const CatWriteToken& token) const override;
    int txSliceId() const override;
    bool transmitting() const override;
    bool requestTxSelection(int sliceId) override;
    bool requestPtt(int sliceId) override;
    void releasePtt() override;

private:
    CatModelAdapter& m_adapter;
    CatTxCoordinator& m_coordinator;
    QPointer<RadioModel> m_model;
    QPointer<CatService> m_service;
    CatBinding m_binding;
    quint64 m_sessionId;
};

} // namespace NereusSDR
