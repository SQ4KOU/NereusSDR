// no-port-check: NereusSDR-original. Approved common power meter rule.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// 2026-10-12 Use the existing shared Core export/import declaration for
//            the new model and its typed Qt signals across Windows DLLs.
//            J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#pragma once
#include <QObject>
#include "core/HpsdrModel.h"
#include "core/NereusCoreExport.h"

namespace NereusSDR {
class AmplifierModel;
class RfKitModel;
class RadioStatus;

// Every consumer uses the mirrored accessory models in both Local and Remote
// roles. This selects readings only; transmit safety remains in RadioModel.
class NEREUS_CORE_EXPORT MainPowerMeter : public QObject {
    Q_OBJECT
public:
    enum class Source { Radio, PowerGenius, RfKit };
    Q_ENUM(Source)
    // Existing panel ranges: AmpApplet.cpp, from AetherSDR
    // src/gui/AmpApplet.cpp:18-22 [@0cd4559]; SMeterWidget::setPowerScale.
    static constexpr int kAmplifierScaleW = 2000;
    // JJ: this is a legal-limit display marker only. It is not hardware
    // capacity or an output cap. Reported watts remain unchanged above it.
    static constexpr int kAmplifierLegalLimitW = 1500;

    MainPowerMeter(AmplifierModel* amplifier, RfKitModel* rfKit,
                   RadioStatus* radio, QObject* parent = nullptr);
    Source source() const { return m_source; }
    bool onAmplifier() const { return m_source != Source::Radio; }
    double forwardW() const { return m_forwardW; }
    double swr() const { return m_swr; }
    double driveW() const { return m_driveW; }
    // The visual warning threshold; never a drive, PTT or control limit.
    int visualLimitW() const { return m_visualLimitW; }
    int scaleTopW() const { return m_scaleTopW; }
    void setHardwareModel(HPSDRModel model);
signals:
    void sourceChanged();
    void readingChanged(double forwardW, double swr);
    void driveChanged(double watts);
    void scaleChanged();
private:
    void refresh();
    AmplifierModel* m_amplifier;
    RfKitModel* m_rfKit;
    RadioStatus* m_radio;
    HPSDRModel m_hardwareModel{HPSDRModel::HPSDR};
    Source m_source{Source::Radio};
    double m_forwardW{0.0};
    double m_swr{1.0};
    double m_driveW{0.0};
    int m_visualLimitW{0};
    int m_scaleTopW{0};
};
} // namespace NereusSDR
