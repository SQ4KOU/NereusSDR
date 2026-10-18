// NereusSDR for iOS: waterfall level controls and migration from the earlier saved scale
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

/// The phone-side waterfall level model accepted in the Display rebuild.
/// The values and conversion rules are part of NereusSDR's product design;
/// see `2026-10-09-display-pill-and-levels-design.md` and the accepted
/// Display rebuild reference.
public enum ClarityLevels {
    public static let kBlackLevelMin = 0
    public static let kBlackLevelMax = 100
    public static let kBlackLevelAtNoise = 50
    public static let kBlackLevelDefault = 57
    public static let kBlackLevelDbPerStep: Float = 0.5

    public static let kColorGainMin = 0
    public static let kColorGainMax = 100
    public static let kColorGainDefault = 60
    public static let kColorSpanAtZeroGainDb: Float = 80
    public static let kColorSpanDbPerGainStep: Float = 0.7

    /// Returns the level mapped to the first palette colour.
    public static func blackPointDbm(_ noiseFloorDbm: Float, _ blackLevel: Int) -> Float {
        let distanceFromNoise = blackLevel - kBlackLevelAtNoise
        return noiseFloorDbm + Float(distanceFromNoise) * kBlackLevelDbPerStep
    }

    /// Returns the distance between the first and last palette colours.
    public static func colorSpanDb(_ colorGain: Int) -> Float {
        kColorSpanAtZeroGainDb - Float(colorGain) * kColorSpanDbPerGainStep
    }

    // Values written by the preceding NereusSDR phone settings model. They
    // remain here only so an installed app can migrate its own saved data.
    public static let kLegacyBlackLevelMax = 125
    public static let kLegacyBlackLevelDefault = 104
    public static let kLegacyColorGainMax = 100
    public static let kLegacyColorGainDefault = 45
    public static let kLegacyBlackDbPerStep = 0.4
    public static let kLegacyGainDbPerStep = 0.3
    public static let kLegacyClarityLowMarginDb = -5.0
    public static let kLegacyClarityHighMarginDb = 55.0

    public static func legacyBlackOffsetDb(_ oldBlack: Int) -> Double {
        let bounded = min(max(oldBlack, 0), kLegacyBlackLevelMax)
        let distanceFromTop = kLegacyBlackLevelMax - bounded
        return kLegacyClarityLowMarginDb + Double(distanceFromTop) * kLegacyBlackDbPerStep
    }

    public static func legacyColorSpanDb(_ oldGain: Int, _ oldBlack: Int) -> Double {
        let boundedGain = min(max(oldGain, 0), kLegacyColorGainMax)
        let oldTop = kLegacyClarityHighMarginDb - Double(boundedGain) * kLegacyGainDbPerStep
        return oldTop - legacyBlackOffsetDb(oldBlack)
    }

    public static func convertLegacyBlackLevel(_ oldBlack: Int) -> Int {
        let offsetSteps = legacyBlackOffsetDb(oldBlack) / Double(kBlackLevelDbPerStep)
        let converted = Int((Double(kBlackLevelAtNoise) + offsetSteps).rounded(.toNearestOrAwayFromZero))
        return min(max(converted, kBlackLevelMin), kBlackLevelMax)
    }

    public static func convertLegacyColorGain(_ oldGain: Int, _ oldBlack: Int) -> Int {
        let oldSpan = legacyColorSpanDb(oldGain, oldBlack)
        let converted = Int(((Double(kColorSpanAtZeroGainDb) - oldSpan)
            / Double(kColorSpanDbPerGainStep)).rounded(.toNearestOrAwayFromZero))
        return min(max(converted, kColorGainMin), kColorGainMax)
    }
}
