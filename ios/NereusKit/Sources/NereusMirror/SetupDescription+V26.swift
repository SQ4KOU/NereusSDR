// NereusSDR for iOS: the closed rows of Setup description V26, Waterfall Defaults' level gates
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation

extension SetupDescription {
    /// The version that gated Waterfall Defaults' level rows.
    static let v26Version = 26

    /// One term of a phone dependency, `{"phone": key, "oneOf": [values]}`,
    /// exactly as expected; nil when it differs in any way (another key, a
    /// nested `allOf`, another phone key or other values).
    static func phoneTerm(_ value: Any?, phone: String, oneOf: [Literal]) -> PhoneDependency? {
        guard let object = value as? [String: Any], Set(object.keys) == ["phone", "oneOf"],
              object["phone"] as? String == phone,
              let values = object["oneOf"] as? [Any], values.count == oneOf.count else { return nil }
        for (value, want) in zip(values, oneOf) {
            switch want {
            case .bool(let on):
                guard bool(value) == on else { return nil }
            case .integer(let whole):
                guard int(value) == whole else { return nil }
            default:
                return nil
            }
        }
        return PhoneDependency(phone: phone, oneOf: oneOf)
    }

    /// V26's `allOf` form of `enabledWhen`: `{"allOf": [term, ...]}` with
    /// nothing beside it, at least one term, and each term exactly the one
    /// expected in the Core's order. Nil when it differs in any way.
    static func phoneAllOf(_ value: Any?, expected: [(phone: String, oneOf: [Literal])]) -> [PhoneDependency]? {
        guard let object = value as? [String: Any], Set(object.keys) == ["allOf"],
              let terms = object["allOf"] as? [Any], !terms.isEmpty,
              terms.count == expected.count else { return nil }
        var parsed: [PhoneDependency] = []
        for (term, want) in zip(terms, expected) {
            guard let dependency = phoneTerm(term, phone: want.phone, oneOf: want.oneOf) else { return nil }
            parsed.append(dependency)
        }
        return parsed
    }

    private static let clarityOff: (String, [Literal]) = ("ClarityEnabled", [.bool(false)])
    private static let minMaxOff: (String, [Literal]) = ("DisplayWfUseSpectrumMinMax", [.bool(false)])
    private static let agcOn: (String, [Literal]) = ("DisplayWfAgc", [.bool(true)])

    /// Every V26 row, by control ID (`resources/setup/display.json`): the
    /// six Waterfall Defaults level rows, as V12 has them but for their
    /// dependency. Clarity sets the levels while it is on and Use spectrum
    /// min/max sets them while it is on, so the thresholds and AGC need
    /// both off; NF-AGC is an option under AGC, so its two rows need AGC on
    /// as well. Use spectrum min/max itself needs Clarity off. And Reset to
    /// Smooth Defaults, whose tooltip and question say at 26 that it sets
    /// every panadapter's defaults; the row is otherwise as V12 has it. And
    /// Enable Clarity, whose tooltip says at 26 that Clarity keeps the black
    /// point just above the noise floor; that row too is otherwise V12's.
    static let v26Specs: [String: V12Spec] = [
        "display.spectrumDefaults.smoothDefaults": .init(phone: "smoothDefaults", kind: .button, applies: .live, confirm: true),
        "display.spectrumDefaults.clarity": .init(phone: "ClarityEnabled", kind: .toggle, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .bool(true)),
        "display.waterfallDefaults.highThreshold": .init(phone: "DisplayWfHighLevel", kind: .slider, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .integer(-62), range: Range(minimum: -200, maximum: 0, step: 1), unit: "dBm", allOf: [clarityOff, minMaxOff]),
        "display.waterfallDefaults.lowThreshold": .init(phone: "DisplayWfLowLevel", kind: .slider, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .integer(-122), range: Range(minimum: -200, maximum: 0, step: 1), unit: "dBm", allOf: [clarityOff, minMaxOff]),
        "display.waterfallDefaults.agc": .init(phone: "DisplayWfAgc", kind: .toggle, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .bool(true), allOf: [clarityOff, minMaxOff]),
        "display.waterfallDefaults.useSpectrumMinMax": .init(phone: "DisplayWfUseSpectrumMinMax", kind: .toggle, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .bool(false), dependency: clarityOff),
        "display.waterfallDefaults.nfAgc": .init(phone: "WaterfallNFAGCEnabled", kind: .toggle, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .bool(false), allOf: [clarityOff, minMaxOff, agcOn]),
        "display.waterfallDefaults.nfAgcOffset": .init(phone: "WaterfallAGCOffsetDb", kind: .integer, applies: .subscription, gate: ("displayExtrasVersion", 1), defaultValue: .integer(0), range: Range(minimum: -60, maximum: 60, step: 1), unit: "dB", allOf: [clarityOff, minMaxOff, agcOn]),
    ]
}
