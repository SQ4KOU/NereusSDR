// NereusSDR for iOS: approved V29 local mode and level-storage regressions
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission
// 2026-10-12: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

import Foundation
import NereusMedia
import Testing
@testable import NereusBand

@MainActor
@Suite struct DisplayV29FoundationTests {
    static let open = MediaFeatureGates(agreedMinor: 11) { _ in 4 }

    @Test func fiveLocalModesUseTheExistingFourWireModesAndKeepManualEndpoints() throws {
        #expect(BandDisplaySettings.WaterfallLevelMode.allCases.map(\.rawValue)
                == ["clarity", "agc", "noiseFloorAgc", "manual", "scale"])
        var settings = BandDisplaySettings.desktopDefaults
        settings.waterfallLowDbm = -139
        settings.waterfallHighDbm = -53
        settings.scaleBottomDbm = -110
        settings.scaleTopDbm = -40
        let scale = try #require(BandDisplaySettings.WaterfallLevelMode(rawValue: "scale"))
        settings.waterfallLevelMode = scale
        #expect(!settings.clarityEnabled && !settings.waterfallAgc && !settings.waterfallNfAgc)
        #expect(settings.useSpectrumMinMax && settings.waterfallLevelMode == scale)
        let request = try #require(settings.extrasRequest(gates: Self.open)?.waterfallLevels)
        #expect(request.mode == .manual && request.lowDbm == -110 && request.highDbm == -40)
        #expect(settings.waterfallLowDbm == -139 && settings.waterfallHighDbm == -53)
        settings.waterfallLevelMode = .noiseFloorAgc
        #expect(!settings.clarityEnabled && !settings.useSpectrumMinMax)
        #expect(settings.waterfallAgc && settings.waterfallNfAgc)
        #expect(settings.extrasRequest(gates: Self.open)?.waterfallLevels?.mode == .noiseFloorAgc)
        settings.waterfallLevelMode = .agc
        #expect(!settings.waterfallNfAgc && settings.waterfallAgc && !settings.useSpectrumMinMax)
        settings.waterfallLevelMode = .manual
        #expect(!settings.waterfallAgc && !settings.waterfallNfAgc && !settings.useSpectrumMinMax)
        #expect(settings.waterfallLowDbm == -139 && settings.waterfallHighDbm == -53)
    }

    @Test func clarityRetainsInactiveFlagsButWinsTheRequestAndSameModeDoesNothing() throws {
        var settings = BandDisplaySettings.desktopDefaults
        settings.clarityEnabled = false
        settings.useSpectrumMinMax = true
        settings.waterfallAgc = false
        settings.waterfallNfAgc = true
        settings.waterfallLevelMode = .clarity
        #expect(settings.useSpectrumMinMax && !settings.waterfallAgc && settings.waterfallNfAgc)
        #expect(settings.waterfallLevelMode == .clarity)
        #expect(settings.extrasRequest(gates: Self.open)?.waterfallLevels?.mode == .clarity)
        let kept = settings
        settings.waterfallLevelMode = .clarity
        #expect(settings == kept)
        settings.clarityEnabled = false
        settings.useSpectrumMinMax = false
        #expect(settings.waterfallLevelMode == .manual) // NF is an option under AGC.
    }

    @Test func currentDefaultsAndManualCenteredTrimsDoNotMoveStoredEndpoints() {
        var settings = BandDisplaySettings.desktopDefaults
        #expect(settings.waterfallBlackLevel == 57 && settings.waterfallColorGain == 60)
        settings.waterfallLevelMode = .manual
        let stored = settings.manualLevels
        #expect(stored.adjusted(by: settings.waterfallAdjustment) == stored)
        settings.waterfallBlackLevel = 61
        settings.waterfallColorGain = 70
        let adjusted = stored.adjusted(by: settings.waterfallAdjustment)
        #expect(abs(adjusted.lowDbm - (stored.lowDbm + 2)) < 0.001)
        #expect(abs(adjusted.highDbm - (stored.highDbm - 3)) < 0.001)
        #expect(settings.manualLevels == stored)
        settings.waterfallLowDbm = -70
        settings.waterfallHighDbm = -69
        let close = settings.manualLevels.adjusted(by: settings.waterfallAdjustment)
        #expect(close.highDbm - close.lowDbm >= 5)
        #expect(settings.waterfallHighDbm == -69)
    }

    @Test func automaticHighCarriesAboveTrimmedLowWithTheApprovedNegativeHalfRule() {
        var settings = BandDisplaySettings.desktopDefaults
        settings.waterfallLevelMode = .agc
        settings.waterfallBlackLevel = 58
        settings.waterfallColorGain = 59
        settings.waterfallHighDbm = -100
        let measured = WaterfallHistory.Levels(lowDbm: -100, highDbm: -100)
        let drawn = measured.adjusted(by: settings.waterfallAdjustment)
        // Low=-99.5; JS Math.round(-99.5)=-99; carry=-94; high trim=-0.3.
        #expect(abs(drawn.lowDbm - (-99.5)) < 0.001)
        #expect(abs(drawn.highDbm - (-93.7)) < 0.001)
        #expect(settings.waterfallHighDbm == -100)
    }

    @Test func legacyValuesConvertOnceWithTheirOwnBlackAndNamedDesktopVectors() throws {
        let suite = "display-v29-\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = BandDisplaySettingsStore(defaults: defaults)
        // Independent named vectors: tests/tst_clarity_levels.cpp:104-110.
        for (id, black, gain, newBlack, newGain) in [
            ("ship", 104, 45, 57, 60), ("ends", 125, 100, 40, 71),
        ] {
            let raw: [String: Any] = ["desktopValuesVersion": 1, "waterfallBlackLevel": black,
                                      "waterfallColorGain": gain, "traceFillOpacity": 0.63]
            defaults.set(try JSONSerialization.data(withJSONObject: raw),
                         forKey: BandDisplaySettingsStore.keyPrefix + id)
            let converted = store.settings(forPan: id)
            #expect(converted.waterfallBlackLevel == newBlack && converted.waterfallColorGain == newGain)
            #expect(converted.traceFillOpacity == 0.63)
            store.setSettings(converted, forPan: id)
            #expect(store.settings(forPan: id) == converted)
            let saved = try #require(defaults.data(forKey: BandDisplaySettingsStore.keyPrefix + id))
            let json = try #require(JSONSerialization.jsonObject(with: saved) as? [String: Any])
            #expect(json["waterfallLevelsVersion"] as? Int == 1)
        }
        // Missing both keeps today's defaults, rather than converting today's defaults again.
        defaults.set(Data("{}".utf8), forKey: BandDisplaySettingsStore.keyPrefix + "missing")
        let missing = store.settings(forPan: "missing")
        #expect(missing.waterfallBlackLevel == 57 && missing.waterfallColorGain == 60)
        let current: [String: Any] = ["waterfallLevelsVersion": 1,
                                     "waterfallBlackLevel": 25, "waterfallColorGain": 40]
        defaults.set(try JSONSerialization.data(withJSONObject: current),
                     forKey: BandDisplaySettingsStore.keyPrefix + "current")
        let unchanged = store.settings(forPan: "current")
        #expect(unchanged.waterfallBlackLevel == 25 && unchanged.waterfallColorGain == 40)
    }

    @Test func receiveAndTransmitCustomDefinitionsRoundTripIndependently() throws {
        let suite = "display-v29-gradient-\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = BandDisplaySettingsStore(defaults: defaults)
        let rx = [PaletteStop(at: 0, colour: "#010203"), PaletteStop(at: 1, colour: "#040506")]
        let tx = [PaletteStop(at: 0, colour: "#102030"), PaletteStop(at: 1, colour: "#405060")]
        var raw = try #require(JSONSerialization.jsonObject(with: JSONEncoder().encode(
            BandDisplaySettings.desktopDefaults)) as? [String: Any])
        raw["customPalette"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(rx))
        raw["txCustomPalette"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(tx))
        raw["waterfallPaletteId"] = BandPalette.customPaletteId
        raw["txWaterfallPaletteId"] = BandPalette.customPaletteId
        defaults.set(try JSONSerialization.data(withJSONObject: raw),
                     forKey: BandDisplaySettingsStore.keyPrefix + "1")
        let settings = store.settings(forPan: "1")
        #expect(settings.customPalette == rx)
        #expect(settings.keyedOverlay.customPalette == tx)
        store.setSettings(settings, forPan: "1")
        #expect(store.settings(forPan: "1").keyedOverlay.customPalette == tx)
    }
}


extension DisplayV29FoundationTests {
    @Test func typedClarityNeedsAMeasuredFloorAndUsesTheExistingFloatFormula() {
        let seeded = WaterfallHistory.Levels(lowDbm: -122, highDbm: -62)
        var adjustment = BandDisplaySettings.desktopDefaults.waterfallAdjustment
        #expect(seeded.adjusted(by: adjustment) == seeded)
        adjustment.clarityFloorDbm = -120
        let measured = seeded.adjusted(by: adjustment)
        #expect(abs(measured.lowDbm - (-116.5)) < 0.001)
        #expect(abs(measured.highDbm - (-78.5)) < 0.001)
        adjustment.clarityFloorDbm = .nan
        #expect(seeded.adjusted(by: adjustment) == seeded)
    }
}
