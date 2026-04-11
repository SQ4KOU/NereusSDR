#pragma once
#include "ButtonBoxItem.h"
namespace NereusSDR {
// DVK record/play/stop controls. From Thetis clsVoiceRecordPlay (MeterManager.cs:10222+).
class VoiceRecordPlayItem : public ButtonBoxItem {
    Q_OBJECT
public:
    enum class Action { Record, Play, Stop, Next, Previous };
    explicit VoiceRecordPlayItem(QObject* parent = nullptr);
    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;
signals:
    void voiceAction(int action);
private:
    void onButtonClicked(int index, Qt::MouseButton button);
    static constexpr int kButtonCount = 5;
};
} // namespace NereusSDR
