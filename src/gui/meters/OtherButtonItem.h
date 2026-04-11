#pragma once

#include "ButtonBoxItem.h"

#include <QVector>

namespace NereusSDR {

// Miscellaneous control buttons + 31 macro slots.
// Ported from Thetis clsOtherButtons (MeterManager.cs:8225+).
class OtherButtonItem : public ButtonBoxItem {
    Q_OBJECT

public:
    enum class ButtonId {
        Power, Rx2, Mon, Tun, Mox, TwoTon, Dup, PsA,
        Play, Rec, Anf, Snb, Mnf, Avg, PeakHold, Ctun,
        Vac1, Vac2, Mute, Bin, SubRx, PanSwap, Xpa,
        Spectrum, Panadapter, Scope, Scope2, Phase,
        Waterfall, Histogram, Panafall, Panascope, Spectrascope,
        DisplayOff,
        Macro0 = 64
    };

    enum class MacroType { Off, On, Toggle, Led, ContainerVis, Cat };

    struct MacroSettings {
        MacroType type{MacroType::Toggle};
        QString onText;
        QString offText;
        QString catCommand;
        int containerVisibleId{-1};
    };

    explicit OtherButtonItem(QObject* parent = nullptr);

    void setButtonState(ButtonId id, bool on);
    MacroSettings& macroSettings(int macroIndex);

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;

signals:
    void otherButtonClicked(int buttonId);
    void macroTriggered(int macroIndex);

private:
    void onButtonClicked(int index, Qt::MouseButton button);

    static constexpr int kCoreButtonCount = 34;
    static constexpr int kMacroCount = 31;

    QVector<int> m_buttonMap;
    QVector<MacroSettings> m_macroSettings;
};

} // namespace NereusSDR
