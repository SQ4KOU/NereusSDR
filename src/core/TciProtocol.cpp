// src/core/TciProtocol.cpp  (NereusSDR)
// NereusSDR-original — no Thetis upstream port in this file.
// Phase 1 stub: empty bodies so the matrix runner links.
// Phase 3 Task 3.1 replaces these bodies with the full Thetis port.

#include "TciProtocol.h"

namespace NereusSDR {

TciProtocol::TciProtocol(QObject* radio)
    : m_radio(radio)
{
    (void)m_radio;  // Phase 1 stub; Phase 3 Task 3.1 reads m_radio for dispatch.
}

QString TciProtocol::handleCommand(const QString& /*command*/)
{
    return {};
}

bool TciProtocol::hasPendingNotification() const
{
    return false;
}

QString TciProtocol::takePendingNotification()
{
    return {};
}

} // namespace NereusSDR
