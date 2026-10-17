// NereusSDR for iOS: the accepted starting colours for the phone's editable receive gradient
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

extension PaletteStop {
    /// The Display rebuild's initial receive Custom gradient. These colour
    /// and position values are part of the accepted NereusSDR phone design;
    /// the interpolation and drawing code is implemented in this app.
    static let receiveDefault = [
        PaletteStop(at: 0, colour: "#000000"),
        PaletteStop(at: Double(Float(0.15)), colour: "#000080"),
        PaletteStop(at: Double(Float(0.30)), colour: "#0040FF"),
        PaletteStop(at: Double(Float(0.45)), colour: "#00C8FF"),
        PaletteStop(at: Double(Float(0.60)), colour: "#00DC00"),
        PaletteStop(at: Double(Float(0.80)), colour: "#FFFF00"),
        PaletteStop(at: 1, colour: "#FF0000"),
    ]
}
