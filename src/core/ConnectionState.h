// src/core/ConnectionState.h  (NereusSDR)
// NereusSDR-original — no Thetis upstream port.
#pragma once

#include <QMetaType>
#include <QString>

namespace NereusSDR {

// Lifecycle of a single connection attempt + steady-state.
// Single source of truth for connection-state semantics — TitleBar,
// ConnectionPanel, status bar, spectrum overlay all read this.
enum class ConnectionState : int {
    Disconnected = 0,  // No connection. UI: grey dot, "Disconnected".
    Probing      = 1,  // Unicast/broadcast probe in flight (~1.5s budget).
                       // UI: amber pulse, "Probing 192.168.x.x…".
    Connecting   = 2,  // Probe replied, metis-start sent, awaiting first ep6.
                       // UI: amber pulse fast, "Connecting to ANAN-G2…".
    Connected    = 3,  // First ep6 frame received; data flowing.
                       // UI: green dot + radio name + Mbps + activity LED.
    LinkLost     = 4,  // Was Connected; no frames for >5s.
                       // UI: red pulse, "Link lost — last frame Xs ago".
};

inline QString connectionStateName(ConnectionState s) {
    switch (s) {
        case ConnectionState::Disconnected: return QStringLiteral("Disconnected");
        case ConnectionState::Probing:      return QStringLiteral("Probing");
        case ConnectionState::Connecting:   return QStringLiteral("Connecting");
        case ConnectionState::Connected:    return QStringLiteral("Connected");
        case ConnectionState::LinkLost:     return QStringLiteral("Link lost");
    }
    return QStringLiteral("Unknown");
}

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::ConnectionState)
