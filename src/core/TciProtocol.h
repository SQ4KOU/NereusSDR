// src/core/TciProtocol.h  (NereusSDR)
// NereusSDR-original — no Thetis upstream port in this file.
// Phase 1 stub: minimal API the matrix runner needs to compile.
// Phase 3 Task 3.1 replaces this with the full Thetis port:
// handleCommand becomes the two-switch dispatch plus buildInitBurst,
// sliceToTrx/trxToSlice, and the TCI 1.8/2.x protocol version negotiation.

#pragma once

#include <QString>

namespace NereusSDR {

// Phase 1 stub — minimal API the matrix runner needs.
// Phase 3 Task 3.1 replaces this with the full Thetis port (handleCommand
// becomes the two-switch dispatch, plus buildInitBurst, sliceToTrx/trxToSlice).
class TciProtocol {
public:
    // Takes a QObject* so both RadioModel and the test mock can be passed in.
    // Phase 3 may refine to RadioModel* once the design is settled.
    explicit TciProtocol(QObject* radio);

    // Dispatch a single TCI command line and return the synchronous response.
    // Returns an empty string for commands that produce no immediate response.
    QString handleCommand(const QString& command);

    // Returns true if there is at least one pending async notification queued.
    bool hasPendingNotification() const;

    // Dequeue and return the oldest pending notification.
    // Caller must check hasPendingNotification() first.
    QString takePendingNotification();

private:
    QObject* m_radio;
};

} // namespace NereusSDR
