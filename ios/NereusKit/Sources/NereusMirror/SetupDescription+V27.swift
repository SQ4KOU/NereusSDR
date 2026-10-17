// NereusSDR for iOS: the closed row of Setup description V27, Appearance's Trace Color
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation

extension SetupDescription {
    /// The version that gave Appearance's Spectrum section a Trace Color
    /// row beside Fill Color, as the desktop page has two pickers. A Core
    /// sends a phone below it the one "Trace & Fill Color:" row instead.
    static let v27Version = 27

    /// Trace Color (`resources/setup/appearance.json`): a colour row bound
    /// to the phone key `DisplayTraceColor`, checked with the ten swatches
    /// (`colourSpecs`) and, unlike them, carrying the version it needs.
    /// The trace line has no colour of its own until one is chosen and
    /// follows the fill until then, so its default is the fill's.
    static let v27TraceColourId = "appearance.colorsTheme.traceColor"

    /// The ids of Appearance's Colors & Theme rows: the swatches, Trace
    /// Color and Reset all colors, every one a closed row.
    static let coloursThemePrefix = "appearance.colorsTheme."
}
