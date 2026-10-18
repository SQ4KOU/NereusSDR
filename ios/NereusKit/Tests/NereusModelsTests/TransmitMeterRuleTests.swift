// NereusSDR for iOS: accessory meter source, display limits and readout regressions
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import NereusModels
import Testing

@Suite("Transmit meter rule")
struct TransmitMeterRuleTests {
    @Test func everySourceCombination() {
        for pgPresent in [false, true] {
            for pgOperate in [false, true] {
                for rfPresent in [false, true] {
                    for rfOperate in [false, true] {
                        let pg = TransmitMeterRule.Amplifier(present: pgPresent, operate: pgOperate, forwardW: 1236, swr: 1.12)
                        let rf = TransmitMeterRule.Amplifier(present: rfPresent, operate: rfOperate, forwardW: 987, swr: 1.34)
                        let result = TransmitMeterRule(powerGenius: pg, rfKit: rf, radioForwardW: 42,
                            radioSwr: 1.56, radioScaleTopW: 600, radioLimitW: 500)
                        let expected: TransmitMeterRule.Source = pgPresent && pgOperate ? .powerGenius
                            : rfPresent && rfOperate ? .rfKit : .radio
                        #expect(result.source == expected)
                        #expect(result.forwardW == (expected == .powerGenius ? 1236 : expected == .rfKit ? 987 : 42))
                        #expect(result.swr == (expected == .powerGenius ? 1.12 : expected == .rfKit ? 1.34 : 1.56))
                        #expect(result.driveW == (expected == .radio ? nil : 42))
                        #expect(result.scaleTopW == (expected == .radio ? 600 : 2000))
                        #expect(result.displayLimitW == (expected == .radio ? 500 : 1500))
                    }
                }
            }
        }
    }
    @Test func overScaleAndMissingAmplifierReading() {
        let amplifier = TransmitMeterRule.Amplifier(present: true, operate: true, forwardW: 2300, swr: 1.12)
        let rule = TransmitMeterRule(powerGenius: amplifier, radioForwardW: 42, radioSwr: 1.34)
        #expect(rule.forwardW == 2300)
        #expect(MeterReadout.power(rule.forwardW) == "2300 W")
        #expect(rule.scaleTopW == 2000 && rule.displayLimitW == 1500)
        let absent = TransmitMeterRule(powerGenius: .init(present: false, operate: true, forwardW: 2300),
                                      radioForwardW: 98, radioSwr: 1.12)
        #expect(absent.source == .radio && absent.forwardW == 98 && absent.driveW == nil)
        let missing = TransmitMeterRule(powerGenius: .init(present: true, operate: true), radioForwardW: 42, radioSwr: 1.34)
        #expect(missing.source == .powerGenius && missing.forwardW == nil && missing.swr == nil)
    }
    @Test func formatterBoundaries() {
        #expect(MeterReadout.swr(9.994) == "9.99")
        #expect(MeterReadout.swr(9.995) == "10.0")
        #expect(MeterReadout.swr(99.949) == "99.9")
        #expect(MeterReadout.swr(99.95) == ">99")
        #expect(MeterReadout.swr(1.12) == "1.12")
        #expect(MeterReadout.power(1235.6) == "1236 W")
        #expect(MeterReadout.temperature(40.6) == "41 °C")
        #expect(MeterReadout.temperature(-5) == "-5 °C")
        let invalidValues: [Double?] = [nil, .nan, .infinity]
        for invalid in invalidValues {
            #expect(MeterReadout.power(invalid) == "--")
            #expect(MeterReadout.swr(invalid) == "--")
            #expect(MeterReadout.temperature(invalid) == "--")
        }
    }
    @Test func reflectedDerivationAndReportedRfKit() {
        #expect(MeterReadout.reflected(forwardW: 100, swr: 3) == 25)
        #expect(MeterReadout.reflected(forwardW: 100, swr: 1) == 0)
        #expect(MeterReadout.reflected(forwardW: 100, swr: 99) == 100)
        #expect(MeterReadout.reflected(forwardW: nil, swr: 3) == nil)
        let rf = MeterReadout.accessory(.rfKit, present: true, forwardW: 100, reflectedW: 7, swr: 3)
        #expect(rf.reflectedW == 7)
    }
    @Test func powerGatesAndLiveTemperature() {
        for device in [MeterReadout.Device.powerGenius, .rfKit, .tuner] {
            let idle = MeterReadout.accessory(device, present: true, forwardW: 0, reflectedW: 0, swr: 1, temperatureC: 41)
            #expect(idle.forwardW == nil && idle.swr == nil && idle.reflectedW == nil && idle.temperatureC == 41)
            let absent = MeterReadout.accessory(device, present: false, transmitting: true, forwardW: 1236, reflectedW: 5, swr: 1.12, temperatureC: 41)
            #expect(absent.forwardW == nil && absent.swr == nil && absent.reflectedW == nil && absent.temperatureC == nil)
        }
        let standby = MeterReadout.accessory(.powerGenius, present: true, transmitting: false, forwardW: 1236, swr: 1.12)
        #expect(standby.forwardW == nil && standby.swr == nil)
        #expect(MeterReadout.accessory(.tuner, present: true, forwardW: 0.499, swr: 1.12).forwardW == nil)
        #expect(MeterReadout.accessory(.tuner, present: true, forwardW: 0.5, swr: 1.12).forwardW == 0.5)
    }
}
