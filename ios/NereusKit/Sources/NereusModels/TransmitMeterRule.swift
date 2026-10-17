// NereusSDR for iOS: shared accessory meter values and display rules
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation

/// Display selection only. The legal-limit marker never limits reported power or controls.
public struct TransmitMeterRule: Equatable, Sendable {
    public enum Source: Equatable, Sendable { case radio, powerGenius, rfKit }
    public struct Amplifier: Equatable, Sendable {
        public var present: Bool
        public var operate: Bool
        public var forwardW: Double?
        public var swr: Double?
        public init(present: Bool = false, operate: Bool = false, forwardW: Double? = nil, swr: Double? = nil) {
            self.present = present; self.operate = operate; self.forwardW = forwardW; self.swr = swr
        }
    }
    public let source: Source
    public let forwardW: Double?
    public let swr: Double?
    public let driveW: Double?
    public let scaleTopW: Double
    public let displayLimitW: Double
    public var amplifying: Bool { source != .radio }
    public init(powerGenius: Amplifier = .init(), rfKit: Amplifier = .init(),
                radioForwardW: Double?, radioSwr: Double?, radioScaleTopW: Double = 120, radioLimitW: Double = 100) {
        let selected: Amplifier?
        if powerGenius.present && powerGenius.operate { source = .powerGenius; selected = powerGenius }
        else if rfKit.present && rfKit.operate { source = .rfKit; selected = rfKit }
        else { source = .radio; selected = nil }
        forwardW = MeterReadout.valid(selected?.forwardW ?? (selected == nil ? radioForwardW : nil))
        swr = MeterReadout.valid(selected?.swr ?? (selected == nil ? radioSwr : nil), minimum: 1)
        driveW = selected == nil ? nil : MeterReadout.valid(radioForwardW)
        scaleTopW = selected == nil ? radioScaleTopW : 2000
        displayLimitW = selected == nil ? radioLimitW : 1500
    }
}

public enum MeterReadout {
    public enum Device: Equatable, Sendable { case powerGenius, rfKit, tuner }
    public struct Readings: Equatable, Sendable {
        public let forwardW: Double?
        public let reflectedW: Double?
        public let swr: Double?
        public let temperatureC: Double?
    }
    public static func accessory(_ device: Device, present: Bool, transmitting: Bool = false,
                                 forwardW: Double?, reflectedW: Double? = nil, swr: Double?,
                                 temperatureC: Double? = nil) -> Readings {
        let forward = present ? valid(forwardW) : nil
        let passesPower: Bool
        switch device {
        case .powerGenius: passesPower = transmitting
        case .rfKit: passesPower = (forward ?? 0) > 0
        case .tuner: passesPower = (forward ?? 0) >= 0.5
        }
        let shownForward = passesPower ? forward : nil
        let shownSwr = shownForward == nil ? nil : valid(swr, minimum: 1)
        return Readings(forwardW: shownForward,
            reflectedW: device == .rfKit ? (shownForward == nil ? nil : valid(reflectedW))
                : reflected(forwardW: shownForward, swr: shownSwr), swr: shownSwr,
            temperatureC: present ? valid(temperatureC, minimum: nil) : nil)
    }

    public static func valid(_ value: Double?, minimum: Double? = 0) -> Double? {
        guard let value, value.isFinite, minimum.map({ value >= $0 }) ?? true else { return nil }
        return value
    }
    public static func power(_ value: Double?) -> String {
        valid(value).map { String(format: "%.0f W", $0) } ?? "--"
    }
    public static func swr(_ value: Double?) -> String {
        guard let value = valid(value, minimum: 1) else { return "--" }
        if value < 9.995 { return String(format: "%.2f", value) }
        if value < 99.95 { return String(format: "%.1f", value) }
        return ">99"
    }
    public static func temperature(_ value: Double?) -> String {
        valid(value, minimum: nil).map { String(format: "%.0f °C", $0) } ?? "--"
    }
    public static func reflected(forwardW: Double?, swr: Double?) -> Double? {
        guard let forward = valid(forwardW), let swr = valid(swr, minimum: 1) else { return nil }
        if swr >= 99 { return forward }
        let reflection = (swr - 1) / (swr + 1)
        return forward * reflection * reflection
    }
}
