#pragma once
#include "ButtonBoxItem.h"
namespace NereusSDR {
// Discord Rich Presence controls. From Thetis clsDiscordButtonBox (MeterManager.cs:11983+).
class DiscordButtonItem : public ButtonBoxItem {
    Q_OBJECT
public:
    explicit DiscordButtonItem(QObject* parent = nullptr);
    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;
signals:
    void discordAction(int action);
private:
    void onButtonClicked(int index, Qt::MouseButton button);
    static constexpr int kDiscordButtonCount = 12;
};
} // namespace NereusSDR
