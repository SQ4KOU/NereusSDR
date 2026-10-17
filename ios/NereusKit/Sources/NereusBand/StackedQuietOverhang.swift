// NereusSDR for iOS: retained 3D row quiet levels and overhang sample coverage
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// From accepted HTML1610–1613/call56 and native DssQuietOverhang.h.
// Folded actual calibrated samples supply the retained quiet baseline;
// no simulated texture, current floor substitution or DSP change.

struct StackedHistorySample: Equatable, Sendable {
    let dbm: Float
    let covered: Bool
}

enum StackedQuietOverhang {
    // From accepted display-pill-live.html:1610-1613 (2026-10-12), call 56:
    // sample folded calibrated row x=0,8,... and select floor(count * 0.3).
    static let sampleStride = 8
    static let quietFraction = 0.3
    static func quiet(from row: [Float]) -> Float? {
        let samples = stride(from: 0, to: row.count, by: sampleStride).map { row[$0] }.filter(\.isFinite).sorted()
        guard !samples.isEmpty else { return nil }
        return samples[Int((Double(samples.count) * quietFraction).rounded(.down))]
    }
    static func sample(unit: Float, exact: Float?, wide: Float?, quiet: Float?, currentFloor: Float) -> StackedHistorySample {
        if let exact { return .init(dbm: exact, covered: true) }
        if let wide { return .init(dbm: wide, covered: true) }
        // Real captured data always wins. Only beyond captured coverage in
        // extended overhang does call 56 supply this retained row's quiet level.
        if unit.isFinite, unit < 0 || unit > 1, let quiet, quiet.isFinite {
            return .init(dbm: quiet, covered: true)
        }
        return .init(dbm: currentFloor, covered: false)
    }
}
