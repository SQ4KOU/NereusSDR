// NereusSDR for iOS: the time an opening's deadline is counted on, injectable for tests
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation

/// Where an opening reads the time and arms its deadline. The app counts
/// on the continuous clock and arms the deadline on the connection's own
/// queue; a test holds time still until the far end has the opening, then
/// moves it to the deadline, so a busy test process cannot spend the
/// deadline before the dial.
protocol OpeningDeadlineClock: Sendable {
    var now: ContinuousClock.Instant { get }
    /// Runs `action` on `queue` once `delay` has passed.
    func run(after delay: Duration, on queue: DispatchQueue, _ action: @escaping @Sendable () -> Void)
}

/// The continuous clock and the queue's own timer.
struct SystemOpeningDeadlineClock: OpeningDeadlineClock {
    var now: ContinuousClock.Instant { ContinuousClock().now }

    func run(after delay: Duration, on queue: DispatchQueue, _ action: @escaping @Sendable () -> Void) {
        let (seconds, attoseconds) = delay.components
        let nanoseconds = Int(seconds) * 1_000_000_000 + Int(attoseconds / 1_000_000_000)
        queue.asyncAfter(deadline: .now() + .nanoseconds(nanoseconds), execute: action)
    }
}
