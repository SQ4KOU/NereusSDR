// NereusSDR for iOS: a test's wait for what the app does, with no deadline of its own
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation
import Testing

/// A test's wait for something the app does, with no deadline of its own. A
/// wait counted in seconds measures the computer, not the app: on the hosted
/// simulator the app has stood still for 14 s and more at a time (2026-10-09
/// and 2026-10-11), and a 5 s wait that began before such a stall failed
/// although the app did everything asked of it once it ran again. Here a
/// slow computer only makes the test slower.
///
/// The test's own `.timeLimit` ends one that never gets there. That cancels
/// the test, the wait returns what the condition says then, and the
/// expectation around it fails naming what it waited for. Under xcodebuild
/// the test process is then started again for the tests that remain, and the
/// other cases of a test that ran out of time are not run.
@MainActor
enum Eventually {
    /// Returns true once `condition` holds, looking again every 20 ms. The
    /// sleep only spaces the looks; nothing here measures how long the app
    /// took. Give the test (or its suite) a `.timeLimit`: without one a
    /// condition that never holds waits forever.
    static func holds(_ condition: () -> Bool) async -> Bool {
        while !condition() {
            do {
                try await Task.sleep(for: .milliseconds(20))
            } catch {
                // The test's time limit canceled it.
                return condition()
            }
        }
        return true
    }
}
