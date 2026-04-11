#pragma once
#include "MeterItem.h"
#include <QString>

namespace NereusSDR {
// External data output bridge (MMIO). From Thetis clsDataOut (MeterManager.cs:16047+).
class DataOutItem : public MeterItem {
    Q_OBJECT
public:
    enum class OutputFormat { Json, Xml, Raw };
    enum class TransportMode { Udp, Tcp, Serial, TcpClient };

    explicit DataOutItem(QObject* parent = nullptr) : MeterItem(parent) {}

    void setMmioGuid(const QString& guid) { m_mmioGuid = guid; }
    QString mmioGuid() const { return m_mmioGuid; }
    void setMmioVariable(const QString& var) { m_mmioVariable = var; }
    QString mmioVariable() const { return m_mmioVariable; }
    void setOutputFormat(OutputFormat fmt) { m_outputFormat = fmt; }
    OutputFormat outputFormat() const { return m_outputFormat; }
    void setTransportMode(TransportMode mode) { m_transportMode = mode; }
    TransportMode transportMode() const { return m_transportMode; }

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    QString m_mmioGuid;
    QString m_mmioVariable;
    OutputFormat m_outputFormat{OutputFormat::Json};
    TransportMode m_transportMode{TransportMode::Udp};
};
} // namespace NereusSDR
