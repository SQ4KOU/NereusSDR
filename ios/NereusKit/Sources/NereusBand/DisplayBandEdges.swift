// NereusSDR for iOS: offline amateur-band identity boundaries used by the spectrum overlay
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import NereusMedia

/// Broad amateur-band identities for the display overlay. These are not a
/// transmit-permission plan. The fixed ranges follow the published ARRL US
/// Amateur Radio Bands chart and its 60 m channel centres; the 60 m display
/// envelope is rounded outward to 10 kHz so all five channels share one band.
enum DisplayBandEdges {
    private static let ranges: [ClosedRange<Double>] = [
        1_800_000...2_000_000,
        3_500_000...4_000_000,
        5_330_000...5_410_000,
        7_000_000...7_300_000,
        10_100_000...10_150_000,
        14_000_000...14_350_000,
        18_068_000...18_168_000,
        21_000_000...21_450_000,
        24_890_000...24_990_000,
        28_000_000...29_700_000,
        50_000_000...54_000_000,
        144_000_000...148_000_000,
    ]

    static func frequencies(in coverage: BandCoverage) -> [Double] {
        guard coverage.centerHz.isFinite, coverage.spanHz.isFinite, coverage.spanHz > 0 else {
            return []
        }
        return ranges.flatMap { [$0.lowerBound, $0.upperBound] }
            .filter { $0 >= coverage.lowHz && $0 <= coverage.highHz }
    }
}
