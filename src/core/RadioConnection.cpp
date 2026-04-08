#include "RadioConnection.h"
#include "P2RadioConnection.h"
#include "LogCategories.h"

namespace NereusSDR {

RadioConnection::RadioConnection(QObject* parent)
    : QObject(parent)
{
}

RadioConnection::~RadioConnection() = default;

std::unique_ptr<RadioConnection> RadioConnection::create(const RadioInfo& info)
{
    switch (info.protocol) {
    case ProtocolVersion::Protocol2: {
        auto conn = std::make_unique<P2RadioConnection>();
        return conn;
    }
    case ProtocolVersion::Protocol1:
        qCWarning(lcConnection) << "Protocol 1 not yet implemented (Phase 3I)";
        return nullptr;
    }
    qCWarning(lcConnection) << "Unknown protocol version:" << static_cast<int>(info.protocol);
    return nullptr;
}

void RadioConnection::setState(ConnectionState newState)
{
    ConnectionState expected = m_state.load();
    if (expected != newState) {
        m_state.store(newState);
        emit connectionStateChanged(newState);
    }
}

} // namespace NereusSDR
