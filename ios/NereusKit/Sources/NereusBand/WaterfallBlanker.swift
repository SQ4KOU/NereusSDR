// NereusSDR for iOS: suppresses isolated high-energy waterfall rows on the phone
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation

/// Removes a short impulse row after a stable receive baseline has formed.
/// The history pauses during transmit so receive resumes against its prior
/// receive baseline. The operator's threshold is the permitted dB increase.
public struct WaterfallBlanker: Equatable, Sendable {
    private static let historyLimit = 32
    private static let rowsBeforeDetection = 8
    private static let impulseContributionLimit: Float = 1.05

    private var recentPowers: [Float] = []
    private var lastAcceptedRow: [Float] = []

    public init() {}

    public mutating func reset() {
        recentPowers.removeAll(keepingCapacity: true)
        lastAcceptedRow.removeAll(keepingCapacity: true)
    }

    public mutating func filtered(_ row: [Float], enabled: Bool, transmitting: Bool,
                                  thresholdDb: Int) -> [Float] {
        guard enabled, !transmitting else {
            return row
        }
        let rowPower = Self.meanPower(of: row)
        let baseline = recentPowers.isEmpty ? 0 : recentPowers.reduce(0, +) / Float(recentPowers.count)
        let hasBaseline = recentPowers.count >= Self.rowsBeforeDetection && baseline > 0 && rowPower > 0
        let rowDb = hasBaseline ? 10 * log10(rowPower) : 0
        let baselineDb = hasBaseline ? 10 * log10(baseline) : 0
        let exceedsThreshold = hasBaseline && rowDb - baselineDb > Float(thresholdDb)

        let displayed: [Float]
        let recordedPower: Float
        if exceedsThreshold {
            displayed = lastAcceptedRow.count == row.count
                ? lastAcceptedRow
                : [Float](repeating: 10 * log10(baseline), count: row.count)
            recordedPower = min(rowPower, baseline * Self.impulseContributionLimit)
        } else {
            displayed = row
            lastAcceptedRow = row
            recordedPower = rowPower
        }
        recentPowers.append(recordedPower)
        if recentPowers.count > Self.historyLimit {
            recentPowers.removeFirst(recentPowers.count - Self.historyLimit)
        }
        return displayed
    }

    private static func meanPower(of row: [Float]) -> Float {
        guard !row.isEmpty else {
            return 0
        }
        let sum = row.reduce(0.0) { $0 + pow(10.0, Double($1) / 10.0) }
        return Float(sum / Double(row.count))
    }
}
