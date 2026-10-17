// NereusSDR for iOS: Setup description V28's Sizing choice as the dispatcher reads, gates and writes it
// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission

import Foundation
import NereusModels

extension StationCatalog.Display.FftSizing {
    /// The Core's setting that holds the Hz/bin target.
    public static let targetKey = "DisplayHzPerBinTarget"
    /// The Core's setting that holds the Size control's size.
    public static let sizeKey = "DisplayFftSize"

    /// The Hz/bin target choosing `self` writes after the choice (the
    /// desktop's order): 0 for Auto and Fixed, which hold no target; for
    /// Hz/bin the remembered target, or with none the running bin width
    /// rounded up to the target's step.
    public func targetWritten(remembered: Double?, runningBinWidthHz: Double?) -> Double {
        guard self == .hzPerBin else {
            return 0
        }
        if let remembered, remembered.isFinite, remembered > 0 {
            return Swift.min(Swift.max(remembered, Self.targetRange.lowerBound), Self.targetRange.upperBound)
        }
        return Self.hzPerBinTarget(forBinWidth: runningBinWidthHz ?? 0)
    }

    /// The Hz/bin target shown while another choice is in charge: the
    /// remembered target, or with none the running bin width; nil when
    /// there is neither. Never 0.
    public static func targetShown(remembered: Double?, runningBinWidthHz: Double?) -> Double? {
        if let remembered, remembered.isFinite, remembered > 0 {
            return remembered
        }
        if let width = runningBinWidthHz, width.isFinite, width > 0 {
            return width
        }
        return nil
    }
}

extension SetupControlDispatcher {
    typealias Sizing = StationCatalog.Display.FftSizing

    /// The Sizing choice in charge now, by the rule every reader shares: a
    /// Core that never wrote the choice reads as Hz/bin with a target above
    /// 0 and as Auto without.
    public var sizingMode: StationCatalog.Display.FftSizing {
        Sizing.onRead(stored: settings.value(Sizing.settingsKey),
                      hzPerBinTarget: settings.value(Sizing.targetKey).flatMap(Self.settingNumber))
    }

    /// The remembered Hz/bin target the Core keeps, or nil with none.
    var sizingRememberedTarget: Double? {
        settings.value(Sizing.rememberedTargetKey).flatMap(Self.settingNumber)
    }

    static func settingNumber(_ text: String) -> Double? {
        Double(text.trimmingCharacters(in: .whitespaces))
    }

    /// A station setting as a row reads it: the Sizing choice by the shared
    /// rule, so a key the Core never wrote still reads; any other as kept.
    func settingText(_ key: String) -> String? {
        key == Sizing.settingsKey ? sizingMode.rawValue : settings.value(key)
    }

    /// A station setting as `control` reads it. The Core sends only the
    /// settings it has stored, so the two rows Sizing gates read a key it
    /// never stored by the rule the planner uses: Size as the size Fixed
    /// runs (the built-in size, also for a stored size the row does not
    /// offer) and the Hz/bin Target as 0, no target. Any other row reads
    /// the setting as kept, so a row of an older description is unchanged.
    func settingText(_ key: String, for control: SetupDescription.Control) -> String? {
        guard control.enabledWhen?.setting == Sizing.settingsKey else {
            return settingText(key)
        }
        switch key {
        case Sizing.sizeKey:
            return String(Sizing.fixedSize(settings.value(key).flatMap(Self.settingNumber) ?? .nan))
        case Sizing.targetKey:
            return settings.value(key) ?? "0"
        default:
            return settingText(key)
        }
    }

    /// A row's `enabledWhen` on a station setting holds now.
    func holds(_ dependency: SetupDescription.Dependency) -> Bool {
        guard let chosen = settingText(dependency.setting) else {
            return false
        }
        return dependency.oneOf.contains(chosen)
    }

    /// The Hz/bin target to write after choosing the Sizing word `word`.
    func sizingTargetText(choosing word: String) -> String? {
        guard let mode = Sizing(rawValue: word) else {
            return nil
        }
        return SetupValueCoding.decimalText(mode.targetWritten(remembered: sizingRememberedTarget,
                                                               runningBinWidthHz: runningBinWidthHz?()))
    }

    /// What a grayed row shows. The Size row while Sizing is not Fixed
    /// shows the size this phone's pan runs; the Hz/bin Target row while
    /// Sizing is not Hz/bin shows the remembered target, or with none the
    /// running bin width, never 0. Any other row shows `stored`.
    func sizingShown(_ control: SetupDescription.Control, stored: SetupValue?) -> SetupValue? {
        guard control.enabledWhen?.setting == Sizing.settingsKey, case .setting(let key)? = control.binding,
              let dependency = control.enabledWhen, !holds(dependency) else {
            return stored
        }
        switch key {
        case Sizing.sizeKey:
            guard let size = runningFftSize?(), control.options?.contains(where: { $0.value == Int64(size) }) == true else {
                return nil
            }
            return .integer(Int64(size))
        case Sizing.targetKey:
            return Sizing.targetShown(remembered: sizingRememberedTarget,
                                      runningBinWidthHz: runningBinWidthHz?()).map(SetupValue.decimal)
        default:
            return stored
        }
    }
}
