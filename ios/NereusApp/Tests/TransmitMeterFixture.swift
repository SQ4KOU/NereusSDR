// NereusSDR for iOS: local mirror meter fixtures, without network or audio devices
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import NereusBand
import NereusLink
import NereusMirror
import NereusModels
@testable import NereusSDR
import Testing

@MainActor
enum TransmitMeterFixture {
    static func main(operate: Bool = true, keyed: Bool = true, present: Bool = true,
                     amplifierWatts: Double = 1236, radioWatts: Double = 42, store: MirrorStore? = nil) -> MainScreenModel {
        let mirror = store ?? MirrorStore(send: { _ in Issue.record("Meter fixtures send nothing") })
        mirror.apply(.objectCreate(.init(key: "txState", className: "TransmitState", properties: [
            .init(name: "keyed", value: .bool(keyed)), .init(name: "forwardPowerWatts", value: .f64(radioWatts)),
            .init(name: "swr", value: .f64(1.12)), .init(name: "micLevelDb", value: .f64(-10))])))
        mirror.apply(.objectCreate(.init(key: "amplifier", className: "AmplifierModel", properties: [
            .init(name: "connectionPhase", value: .enumeration(6)), .init(name: "present", value: .bool(present)),
            .init(name: "operate", value: .bool(operate)), .init(name: "transmitting", value: .bool(keyed && operate)),
            .init(name: "forwardPowerW", value: .f64(amplifierWatts)), .init(name: "swr", value: .f64(1.12)),
            .init(name: "temperatureC", value: .f64(41))])))
        mirror.apply(.objectCreate(.init(key: "rfkit", className: "RfKitModel", properties: [
            .init(name: "connectionPhase", value: .enumeration(6)), .init(name: "present", value: .bool(true)),
            .init(name: "operate", value: .bool(false)), .init(name: "forwardPowerW", value: .f64(keyed ? 1236 : 0)),
            .init(name: "reflectedPowerW", value: .f64(4)), .init(name: "swr", value: .f64(1.12)),
            .init(name: "temperatureC", value: .f64(41)), .init(name: "voltageV", value: .f64(53.2)),
            .init(name: "currentA", value: .f64(12))])))
        mirror.apply(.objectCreate(.init(key: "tuner", className: "TunerModel", properties: [
            .init(name: "connectionPhase", value: .enumeration(6)), .init(name: "isPresent", value: .bool(true)),
            .init(name: "isOperate", value: .bool(true)), .init(name: "fwdPower", value: .f64(keyed ? (operate ? amplifierWatts : radioWatts) : 0)),
            .init(name: "swr", value: .f64(1.12))])))
        if let json = MainScreenTests.catalogueJSON() {
            mirror.apply(.objectCreate(.init(key: "catalog", className: "StationCatalog", properties: [
                .init(name: "json", value: .utf8(json)), .init(name: "revision", value: .i64(1))])))
        } else { Issue.record("The checked-in catalogue fixture is required") }
        mirror.apply(.snapshotComplete)
        let main = MainScreenModel(mirror: mirror, settings: SettingsProxyClient(send: { _ in }), commands: nil,
                                  operations: BandSubscriber.Operations(subscribe: { _ in }, unsubscribe: { _ in }))
        main.slices.catalog = BandFlagShotTests.catalogue()
        main.transmit.catalog = main.slices.catalog
        main.band.catalog = main.slices.catalog
        main.transmit.refresh()
        main.accessories.refresh()
        main.sMeter.refresh()
        return main
    }
}
