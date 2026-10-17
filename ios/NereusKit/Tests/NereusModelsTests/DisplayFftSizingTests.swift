// NereusSDR for iOS: tests for the Sizing choice (Auto, Hz/bin, Fixed), against the vectors the desktop and the Core read
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation
import LinkTestSupport
import Testing
@testable import NereusModels

/// Link document section 7.4 (`display`, Sizing). The shared vectors are
/// `tests/data/display_fft_sizing_vectors.json`, which the desktop's and the
/// Core's tests read too, so one file pins every reader to the same sizes.
@Suite struct DisplayFftSizingTests {
    typealias Sizing = StationCatalog.Display.FftSizing

    /// The sizes the Core computes (the catalog's `fftPlan`).
    static let plan = StationCatalog.Display.FftPlan(minFftSize: 1024, maxFftSize: 262_144)

    static func vectors() throws -> [String: Any] {
        // tests/data/link/v1 is three folders below the repository's tests/data.
        let file = LinkFixtureLoader.root.deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("display_fft_sizing_vectors.json")
        return try #require(try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as? [String: Any])
    }

    static func number(_ item: [String: Any], _ key: String) throws -> Double {
        try #require((item[key] as? NSNumber)?.doubleValue, "\(key) in \(item["name"] ?? "")")
    }

    @Test func theModeOnReadIsTheSharedRule() throws {
        let cases = try #require(try Self.vectors()["onRead"] as? [[String: Any]])
        #expect(cases.count == 11)
        for item in cases {
            let name = try #require(item["name"] as? String)
            let stored = try #require(item["storedMode"] as? String)
            let word = try #require(item["mode"] as? String)
            let expected = try #require(Sizing(rawValue: word))
            let target = try Self.number(item, "hzPerBinTarget")
            #expect(Sizing.onRead(stored: stored, hzPerBinTarget: target) == expected, "\(name)")
            // An absent key reads as an empty one.
            if stored.isEmpty {
                #expect(Sizing.onRead(stored: nil, hzPerBinTarget: target) == expected, "\(name), no key")
            }
        }
        // A target that is not a number is no target.
        #expect(Sizing.onRead(stored: "HzPerBin", hzPerBinTarget: .nan) == .auto)
        #expect(Sizing.onRead(stored: nil, hzPerBinTarget: .infinity) == .auto)
        #expect(Sizing.onRead(stored: nil, hzPerBinTarget: nil) == .auto)
    }

    /// Fixed and Hz/bin give the desktop's size for every shared vector. The
    /// one Hz/bin vector with no target plans as Auto, whose rule on the
    /// desktop keeps 4096 points across the pan; 4096 pixels here make the
    /// phone's pixel rule ask for the same.
    @Test func fixedAndHzPerBinPlanTheDesktopsSizes() throws {
        let cases = try #require(try Self.vectors()["plan"] as? [[String: Any]])
        var counted: [Sizing: Int] = [:]
        for item in cases {
            let name = try #require(item["name"] as? String)
            let word = try #require(item["mode"] as? String)
            let mode = try #require(Sizing(rawValue: word))
            guard mode != .auto else {
                continue
            }
            counted[mode, default: 0] += 1
            let planned = Self.plan.plan(mode: mode, storedSize: try Self.number(item, "storedSize"),
                                         sampleRateHz: try Self.number(item, "sampleRateHz"), pixels: 4096,
                                         spanHz: try Self.number(item, "spanHz"),
                                         hzPerBinTarget: try Self.number(item, "hzPerBinTarget"))
            #expect(planned.fftSize == Int(try Self.number(item, "size")), "\(name)")
            if mode == .fixed {
                #expect(!planned.fine, "\(name)")
            }
        }
        #expect(counted == [.fixed: 15, .hzPerBin: 8])
    }

    /// Auto on the phone: enough bins for the pan's pixels, never below the
    /// built-in size, and the stored size plays no part.
    @Test func autoFollowsThePixelsFromTheBuiltInSize() {
        let rate = 192_000.0
        for stored in [4096.0, 65_536.0, 262_144.0, 5000.0] {
            // The whole rate across 1206 pixels: the built-in size.
            var result = Self.plan.plan(mode: .auto, storedSize: stored, sampleRateHz: rate, pixels: 1206,
                                        spanHz: rate, hzPerBinTarget: 0)
            #expect(result.fftSize == 4096 && !result.fine)
            // Zoomed eight times: 9648 bins wanted, so 16384, fine.
            result = Self.plan.plan(mode: .auto, storedSize: stored, sampleRateHz: rate, pixels: 1206,
                                    spanHz: rate / 8, hzPerBinTarget: nil)
            #expect(result.fftSize == 16_384 && result.fine)
            // A stray target plays no part in Auto.
            result = Self.plan.plan(mode: .auto, storedSize: stored, sampleRateHz: rate, pixels: 1206,
                                    spanHz: rate, hzPerBinTarget: 0.5)
            #expect(result.fftSize == 4096 && !result.fine)
        }
        // Never past the largest size, and no view yet is the built-in size.
        var result = Self.plan.plan(mode: .auto, storedSize: 4096, sampleRateHz: 1_536_000, pixels: 1206, spanHz: 100,
                                    hzPerBinTarget: 0)
        #expect(result.fftSize == Self.plan.maxFftSize && result.fine)
        result = Self.plan.plan(mode: .auto, storedSize: 4096, sampleRateHz: 0, pixels: 1206, spanHz: 0,
                                hzPerBinTarget: 0)
        #expect(result.fftSize == 4096 && !result.fine)
    }

    @Test func hzPerBinHoldsTheWidthWithinTheTimeCap() {
        // 192 kHz at 5 Hz/bin: 38400 bins wanted, so 65536, whatever the zoom.
        for span in [192_000.0, 35_000.0, 1000.0] {
            let result = Self.plan.plan(mode: .hzPerBin, storedSize: 262_144, sampleRateHz: 192_000, pixels: 1206,
                                        spanHz: span, hzPerBinTarget: 5)
            #expect(result.fftSize == 65_536 && result.fine)
        }
        // 0.5 Hz/bin wants 384000 bins; 0.7 s of 192 kHz holds 131072.
        #expect(Self.plan.largestWithinTransformTime(sampleRateHz: 192_000) == 131_072)
        #expect(Self.plan.largestWithinTransformTime(sampleRateHz: 48_000) == 32_768)
        #expect(Self.plan.largestWithinTransformTime(sampleRateHz: 1_536_000) == 262_144)
        // No rate yet: the built-in size.
        let result = Self.plan.plan(mode: .hzPerBin, storedSize: 4096, sampleRateHz: 0, pixels: 1206, spanHz: 0,
                                    hzPerBinTarget: 5)
        #expect(result.fftSize == 4096 && !result.fine)
    }

    @Test func aRunningBinWidthRoundsUpToTheTargetsStep() {
        // 192 kHz over 8192: 23.4375 rounds up, so the size does not double.
        #expect(Sizing.hzPerBinTarget(forBinWidth: 23.4375) == 23.5)
        #expect(Sizing.hzPerBinTarget(forBinWidth: 46.875) == 47)
        #expect(Sizing.hzPerBinTarget(forBinWidth: 12.5) == 12.5)
        #expect(Sizing.hzPerBinTarget(forBinWidth: 0.1) == 0.5)
        #expect(Sizing.hzPerBinTarget(forBinWidth: 375) == 200)
        // No width is the coarsest target.
        #expect(Sizing.hzPerBinTarget(forBinWidth: 0) == 200)
        #expect(Sizing.hzPerBinTarget(forBinWidth: -3) == 200)
        #expect(Sizing.hzPerBinTarget(forBinWidth: .nan) == 200)
        #expect(Sizing.hzPerBinTarget(forBinWidth: .infinity) == 200)
    }

    @Test func fixedRunsOnlyASizeTheControlOffers() {
        #expect(Sizing.fixedSizes == [4096, 8192, 16_384, 32_768, 65_536, 131_072, 262_144])
        #expect(Sizing.fixedSize(32_768) == 32_768)
        #expect(Sizing.fixedSize(5000) == 4096)
        #expect(Sizing.fixedSize(2048) == 4096)
        #expect(Sizing.fixedSize(524_288) == 4096)
        #expect(Sizing.fixedSize(.nan) == 4096)
    }

    /// The catalog's Sizing control reads with its words, and the Hz/bin
    /// target names the setting that keeps the remembered target.
    @Test func theCatalogDescribesSizingAndTheRememberedTarget() throws {
        let display = try StationCatalogDisplayTests.display()
        let sizing = try #require(display.sizingControl)
        #expect(sizing.kind == .choice && sizing.isStation && sizing.label == "Sizing")
        #expect(sizing.options.map(\.token) == ["Auto", "HzPerBin", "Fixed"])
        #expect(sizing.options.map(\.value) == [0, 1, 2])
        #expect(sizing.options.map(\.label) == ["Auto", "Hz/bin", "Fixed"])
        #expect(sizing.defaultValue == 0)
        #expect(sizing.rememberedKey == nil)
        let target = try #require(display.control(settingsKey: "DisplayHzPerBinTarget"))
        #expect(target.rememberedKey == Sizing.rememberedTargetKey)
        // The other controls' options write their numbers.
        let size = try #require(display.control(settingsKey: "DisplayFftSize"))
        #expect(size.options.allSatisfy { $0.token == nil })
        #expect(size.options.map(\.value) == Sizing.fixedSizes.map(Double.init))
    }
}
