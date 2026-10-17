// NereusSDR for iOS: the Modes tab's mode buttons: the Core's modes, the slice's lit
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import NereusMirror
import SwiftUI

/// The Mode section: every mode the Core's catalogue lists, in its order,
/// the active slice's lit. A tap writes the slice's `dspMode`.
struct ModePicker: View {
    @ObservedObject var model: ModesTabModel

    static func displayLabel(_ label: String) -> String {
        if label == "RADE2-U" || label == "RADE2-L" {
            return "\(label)*"
        }
        return label
    }

    static func spokenLabel(_ label: String) -> String {
        if label == "RADE2-U" || label == "RADE2-L" {
            return "\(label) (Experimental)"
        }
        return label
    }

    var body: some View {
        ModesChrome.section("Mode") {
            if model.modes.isEmpty {
                ModesChrome.note(CatalogFeed.needsNewerCoreText)
            } else {
                ModesChrome.grid(columns: 5) {
                    ForEach(model.modes) { mode in
                        PanelButton(label: Self.displayLabel(mode.label), lit: mode.lit, style: .blue) {
                            model.selectMode(mode.id)
                        }
                        .accessibilityLabel(Self.spokenLabel(mode.label))
                        .accessibilityIdentifier("modesMode\(mode.label)")
                    }
                }
                if model.modes.contains(where: { $0.label == "RADE2-U" || $0.label == "RADE2-L" }) {
                    ModesChrome.note("* RADE2 modes are experimental.")
                }
            }
        }
    }
}
