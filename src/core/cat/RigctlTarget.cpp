// =================================================================
// src/core/cat/RigctlTarget.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See the header. CoreRigctlTarget is
// RigctlProtocol's former direct use of CatModelAdapter, CatTxCoordinator
// and CatService, moved here unchanged.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created for the remote window's rigctld. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================
#include "RigctlTarget.h"

#include "CatService.h"
#include "CatTxCoordinator.h"
#include "core/MoxController.h"
#include "core/TxSliceArbiter.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

namespace NereusSDR {

CoreRigctlTarget::CoreRigctlTarget(CatModelAdapter& adapter, CatTxCoordinator& coordinator,
                                   int channel, quint64 sessionId)
    : m_adapter(adapter)
    , m_coordinator(coordinator)
    , m_model(&adapter.radioModel())
    , m_service(adapter.radioModel().catService())
    , m_binding(m_service->channelConfig(channel).binding)
    , m_sessionId(sessionId)
{
}

bool CoreRigctlTarget::live() const
{
    // Retained protocol/session may outlive synchronous service/model destruction.
    return m_model && m_service && m_service->isStarted() && m_service->session(m_sessionId)
        && m_service->session(m_sessionId)->dialect() == CatWireDialect::Rigctld;
}

int CoreRigctlTarget::sliceIdOf(CatVfo vfo) const
{
    return vfo == CatVfo::Primary ? m_binding.primarySliceId
                                  : m_binding.secondarySliceId.value_or(-1);
}

SliceModel* CoreRigctlTarget::resolveSlice(CatVfo vfo) const
{
    return m_adapter.resolveSlice(m_binding, vfo);
}

bool CoreRigctlTarget::mayRead(CatVfo vfo) const
{
    return m_adapter.mayRead(m_binding, vfo);
}

bool CoreRigctlTarget::mayChange(CatVfo vfo, const QByteArray& property) const
{
    return m_adapter.mayChange(m_binding, vfo, property);
}

bool CoreRigctlTarget::refusedOnAir(CatVfo vfo) const
{
    const SliceModel* slice = m_adapter.resolveSlice(m_binding, vfo);
    return m_model && slice && m_model->moxController()->isMox()
        && m_model->txBoundSlice() == slice
        && m_model->moxController()->currentKeyer().isStation();
}

CatWriteToken CoreRigctlTarget::prepareWrite(CatVfo vfo, const QByteArray& property) const
{
    return m_adapter.prepareWrite(m_binding, vfo, property);
}

bool CoreRigctlTarget::revalidateWrite(const CatWriteToken& token) const
{
    return m_adapter.revalidateWrite(token);
}

int CoreRigctlTarget::txSliceId() const
{
    return m_model ? m_model->txSliceArbiter()->txBoundSliceId() : -1;
}

bool CoreRigctlTarget::transmitting() const
{
    return m_model && m_model->moxController()->isMox();
}

bool CoreRigctlTarget::requestTxSelection(int sliceId)
{
    return m_coordinator.requestTxSelection(m_sessionId, sliceId);
}

bool CoreRigctlTarget::requestPtt(int sliceId)
{
    return m_coordinator.requestPtt(m_sessionId, sliceId);
}

void CoreRigctlTarget::releasePtt()
{
    m_coordinator.releasePtt(m_sessionId);
}

} // namespace NereusSDR
