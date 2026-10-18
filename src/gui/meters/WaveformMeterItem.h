// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#pragma once
// no-port-check: NereusSDR-original native PCM face; FreeDV plot facts only.
#include "MeterItem.h"
#include "core/audio/WaveformSnapshot.h"
#include <QJsonObject>
#include <QSize>
namespace NereusSDR {
class WaveformMeterItem : public MeterItem
{
    Q_OBJECT
  public:
    enum class Source { Microphone, DecodedSpeech };
    enum class SourceStage { Conditioned, RawMic };
    explicit WaveformMeterItem(Source source = Source::Microphone, QObject* parent = nullptr);
    QString typeId() const;
    QJsonObject configuration() const;
    bool applyConfiguration(const QJsonObject& config);
    QString serialize() const override;
    bool deserialize(const QString& record) override;
    int preferredFaceHeight() const
    {
        return m_config.value("faceHeight").toInt(300);
    }
    QSize minimumFaceSize() const
    {
        return {240, 180};
    }
    int viewWindowMs() const
    {
        return m_config.value("viewWindowMs").toInt(5000);
    }
    WaveformKind waveformKind() const;
    bool setSnapshot(WaveformSnapshotPtr snapshot);
    WaveformSnapshotPtr snapshot() const
    {
        return m_snapshot;
    }
    QSet<int> readingBindings() const override
    {
        return {};
    }
    void setValue(double) override {}
    void pushBindingValue(int, double) override {}
    void resetRxSource() override
    {
        setSnapshot({});
    }
    Layer renderLayer() const override
    {
        return Layer::OverlayDynamic;
    }
    bool participatesIn(Layer layer) const override
    {
        return layer == Layer::Background || layer == Layer::OverlayDynamic;
    }
    bool takeStaticPresentationChange() override
    {
        const bool dirty = m_staticDirty;
        m_staticDirty = false;
        return dirty;
    }
    void paint(QPainter&, int, int) override;
    void paintForLayer(QPainter&, int, int, Layer) override;

  private:
    Source m_source;
    QJsonObject m_config;
    WaveformSnapshotPtr m_snapshot;
    bool m_staticDirty = true;
};
} // namespace NereusSDR
