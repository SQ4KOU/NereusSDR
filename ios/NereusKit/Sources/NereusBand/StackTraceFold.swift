// NereusSDR for iOS: combines fast display ticks into one retained 3D spectrum row
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import NereusMedia

/// Accumulates a complete 3D row without mixing capture contexts. Each bin
/// keeps the strongest finite sample seen during the row's display period.
struct StackTraceFold: Equatable, Sendable {
    private struct Identity: Equatable, Sendable {
        let endpointId: UInt32
        let contextGeneration: UInt32
        let coverage: BandCoverage
        let wideCoverage: BandCoverage?
        let exactCount: Int
        let wideCount: Int
        let transmitting: Bool
    }

    private var identity: Identity?
    private var ticks = 0
    private var mostRecent: DisplayFrame?
    private var exactPeaks: [Float] = []
    private var widePeaks: [Float] = []

    static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.identity == rhs.identity && lhs.ticks == rhs.ticks && lhs.mostRecent == rhs.mostRecent
            && bitwiseEqual(lhs.exactPeaks, rhs.exactPeaks) && bitwiseEqual(lhs.widePeaks, rhs.widePeaks)
    }

    mutating func clear() {
        identity = nil
        ticks = 0
        mostRecent = nil
        exactPeaks.removeAll(keepingCapacity: true)
        widePeaks.removeAll(keepingCapacity: true)
    }

    mutating func append(_ frame: DisplayFrame, coverage: BandCoverage?, wide: BandCoverage?,
                         transmitting: Bool, divider: Int) -> DisplayFrame? {
        guard let coverage, coverage.spanHz > 0, !frame.waterfallDbm.isEmpty else {
            clear()
            return nil
        }
        let wideSamples = transmitting ? [] : frame.wideDbm
        let wideCoverage = transmitting ? nil : wide
        let offeredIdentity = Identity(endpointId: frame.endpointId,
            contextGeneration: frame.contextGeneration, coverage: coverage,
            wideCoverage: wideCoverage, exactCount: frame.waterfallDbm.count,
            wideCount: wideSamples.count, transmitting: transmitting)

        if identity != offeredIdentity {
            clear()
            identity = offeredIdentity
        }
        if ticks == 0 {
            exactPeaks = frame.waterfallDbm
            widePeaks = wideSamples
        } else {
            Self.keepStrongest(frame.waterfallDbm, in: &exactPeaks)
            Self.keepStrongest(wideSamples, in: &widePeaks)
        }
        ticks += 1
        mostRecent = frame

        guard ticks >= max(1, divider), let last = mostRecent else {
            return nil
        }
        let completed = DisplayFrame(endpointId: last.endpointId,
            contextGeneration: last.contextGeneration, encoderSequence: last.encoderSequence,
            producerTimestamp: last.producerTimestamp, isKeyframe: last.isKeyframe,
            waterfallAdvance: true, minDbm: last.minDbm, maxDbm: last.maxDbm,
            traceDbm: last.traceDbm, waterfallDbm: exactPeaks, wideDbm: widePeaks)
        clear()
        return completed
    }

    private static func keepStrongest(_ offered: [Float], in retained: inout [Float]) {
        for index in offered.indices {
            let next = offered[index]
            if !retained[index].isFinite || next.isFinite && next > retained[index] {
                retained[index] = next
            }
        }
    }

    private static func bitwiseEqual(_ lhs: [Float], _ rhs: [Float]) -> Bool {
        lhs.count == rhs.count && zip(lhs, rhs).allSatisfy { $0.bitPattern == $1.bitPattern }
    }
}
