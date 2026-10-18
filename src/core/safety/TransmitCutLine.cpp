// no-port-check: NereusSDR-original.
// =================================================================
// src/core/safety/TransmitCutLine.cpp  (NereusSDR)
// =================================================================
//
// See TransmitCutLine.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), remote transmit cut off the main thread, with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/safety/TransmitCutLine.h"

#include "core/RadioConnection.h"
#include "core/TxChannel.h"

namespace NereusSDR {

void TransmitCutLine::setTargets(RadioConnection* connection, TxChannel* tx)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connection = connection;
    m_tx = tx;
}

void TransmitCutLine::cut()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // RF first: no further TX I/Q block reaches the connection once this
    // returns, and a channel-on still queued no longer opens the gate.
    if (m_tx != nullptr) {
        m_tx->closeRfGate();
    }
    // Then MOX and the relay, queued onto the connection's own thread.
    if (m_connection != nullptr) {
        m_connection->cutTransmit();
    }
}

} // namespace NereusSDR
