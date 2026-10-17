// NereusSDR for iOS: the closed rows of Setup description V28, Spectrum Defaults' Sizing and averaging from 1 ms
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation

extension SetupDescription {
    /// The version that gave Spectrum Defaults a Sizing row (Auto, Hz/bin,
    /// Fixed), gated Size and Hz/bin Target on it, and let the two
    /// averaging times go down to 1 ms. A Core sends a phone below it the
    /// four older rows and no Sizing row.
    static let v28Version = 28

    static let v28SizingId = "display.spectrumDefaults.fftSizing"
    static let v28SizeId = "display.spectrumDefaults.fftSize"
    static let v28TargetId = "display.spectrumDefaults.hzPerBinTarget"

    /// The two averaging times (`resources/setup/display.json`): the phone
    /// key each sets and its default.
    static let v28AverageTimes: [String: (phone: String, defaultValue: Int64)] = [
        "display.spectrumDefaults.averageTime": ("DisplaySpectrumAverageTimeMs", 30),
        "display.waterfallDefaults.averageTime": ("DisplayWaterfallAverageTimeMs", 120),
    ]

    /// Every V28 row's id. The id alone sends a row to the closed check, so
    /// one of these claiming any other version is refused, never read as an
    /// open row.
    static let v28Ids: Set<String> = Set(v28AverageTimes.keys).union([v28SizingId, v28SizeId, v28TargetId])

    /// The Core's setting the Sizing row writes, and the words it holds
    /// with the label each shows, in the Core's order.
    static let v28SizingKey = "DisplayFftSizingMode"
    static let v28SizingOptions: [(word: String, label: String)] = [
        ("Auto", "Auto"), ("HzPerBin", "Hz/bin"), ("Fixed", "Fixed"),
    ]

    /// `{"setting": "DisplayFftSizingMode", "oneOf": [word]}` exactly; nil
    /// when it differs in any way.
    static func sizingDependency(_ value: Any?, word: String) -> Dependency? {
        guard let raw = value as? [String: Any], Set(raw.keys) == ["setting", "oneOf"],
              raw["setting"] as? String == v28SizingKey, raw["oneOf"] as? [String] == [word] else { return nil }
        return Dependency(setting: v28SizingKey, oneOf: [word])
    }

    /// The Sizing row's options: three objects, each `{"value": word,
    /// "label": label}` exactly, in the Core's order.
    static func sizingOptions(_ value: Any?) -> (options: [Option], literals: [Int64: Literal])? {
        guard let raw = value as? [[String: Any]], raw.count == v28SizingOptions.count else { return nil }
        var options: [Option] = []
        var literals: [Int64: Literal] = [:]
        for (index, item) in raw.enumerated() {
            let want = v28SizingOptions[index]
            guard Set(item.keys) == ["value", "label"], item["value"] as? String == want.word,
                  item["label"] as? String == want.label else { return nil }
            options.append(Option(value: Int64(index), label: want.label))
            literals[Int64(index)] = .text(want.word)
        }
        return (options, literals)
    }

    /// Checks a V28 row as the exact closed object; nil when it differs in
    /// any way.
    ///
    /// - Sizing: a choice on the Core's `DisplayFftSizingMode`, its three
    ///   options writing the words "Auto", "HzPerBin" and "Fixed", default
    ///   "Auto".
    /// - Size: the seven-size slider on `DisplayFftSize` as before, live
    ///   only while Sizing is Fixed.
    /// - Hz/bin Target: 0.5 to 200 by 0.5 on `DisplayHzPerBinTarget`, live
    ///   only while Sizing is Hz/bin; 0 is no longer a choice.
    /// - The two averaging times: 1 to 9999 ms by 1.
    static func checkV28(_ raw: [String: Any], id: String, kind: Kind?, binding: Binding?, applies: Applies?,
                         gate: Gate?, range: Range?, decimals: Int?, required: Int?) -> ExtensionMetadata? {
        guard required == v28Version, applies == .subscription, raw["label"] is String,
              raw["tooltip"] is String else { return nil }
        let base: Set<String> = ["id", "label", "tooltip", "kind", "binding", "applies",
                                 "requiresDescriptionVersion", "default"]
        var result = ExtensionMetadata()
        switch id {
        case v28SizingId:
            guard Set(raw.keys) == base.union(["options"]), kind == .choice, binding == .setting(v28SizingKey),
                  gate == nil, range == nil, decimals == nil,
                  raw["default"] as? String == v28SizingOptions[0].word,
                  let read = sizingOptions(raw["options"]) else { return nil }
            result.options = read.options
            result.optionLiterals = read.literals
            result.defaultValue = .text(v28SizingOptions[0].word)
        case v28SizeId:
            guard Set(raw.keys) == base.union(["options", "enabledWhen"]), kind == .slider,
                  binding == .setting("DisplayFftSize"), gate == nil, range == nil, decimals == nil,
                  int(raw["default"]) == 4096,
                  let options = fftOptions(raw["options"], (0..<7).map { String(4096 << $0) }),
                  let dependency = sizingDependency(raw["enabledWhen"], word: "Fixed") else { return nil }
            result.options = options
            result.dependency = dependency
            result.defaultValue = .integer(4096)
        case v28TargetId:
            guard Set(raw.keys) == base.union(["min", "max", "step", "decimals", "unit", "enabledWhen"]),
                  kind == .decimal, binding == .setting("DisplayHzPerBinTarget"), gate == nil,
                  range == Range(minimum: 0.5, maximum: 200, step: 0.5), decimals == 2,
                  raw["unit"] as? String == "Hz/bin", int(raw["default"]) == 200,
                  let dependency = sizingDependency(raw["enabledWhen"], word: "HzPerBin") else { return nil }
            result.dependency = dependency
            result.defaultValue = .integer(200)
        default:
            guard let spec = v28AverageTimes[id],
                  Set(raw.keys) == base.union(["gate", "min", "max", "step", "unit"]), kind == .integer,
                  binding == .phone(spec.phone), exactGate(gate, "displayExtrasVersion", 1),
                  range == Range(minimum: 1, maximum: 9999, step: 1), decimals == nil,
                  raw["unit"] as? String == "ms", int(raw["default"]) == spec.defaultValue else { return nil }
            result.defaultValue = .integer(spec.defaultValue)
        }
        return result
    }
}
