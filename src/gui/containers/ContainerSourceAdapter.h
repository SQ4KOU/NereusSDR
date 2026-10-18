#pragma once
// no-port-check: NereusSDR-original read-only adapter for calibrated slice caches.
// Modification history (NereusSDR):
//   2026-10-08 - writeContainerSource: the container source write shared by
//                 Container Settings and the workspace header's source menu.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-04 — Selected RX source identity and RX-only presentation reset by
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-02 — Mixed container ownership, persistence and source routing by
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QByteArray>
#include <QJsonObject>
#include <functional>
#include "models/RadeWaveformModel.h"
#include <optional>
namespace NereusSDR {
class RadioModel;
class SliceModel;
class ContainerSourceAdapter {
public:
    static WaveformSnapshotPtr waveform(RadioModel* model, const QJsonObject& context, SliceModel* inherited, WaveformKind kind, const QString& session = {});
    static std::optional<WaveformRequest> waveformRequest(RadioModel* model, const QJsonObject& context, SliceModel* inherited, WaveformKind kind, const QString& session = {});
    static bool followsSelectedRx(const QJsonObject& context);
    // The container-level source write of Container Settings' Slice combo:
    // `rxSource` is 1-based (slices A to D are 1..4). A config that saves a
    // sliceId keeps that key; otherwise rxSource is written.
    static void writeContainerSource(QJsonObject& config, int rxSource);
    static SliceModel* slice(RadioModel* model, const QJsonObject& context, SliceModel* inherited, const QString& currentSessionId = {});
    // Runtime identity is separate from the persisted symbolic source choice.
    static QByteArray sourceIdentity(RadioModel* model, const QJsonObject& context, SliceModel* inherited,
                                     const QString& currentSessionId = {}, bool ready = true);
    static double reading(RadioModel* model, const QJsonObject& context, SliceModel* inherited,
                          int binding, bool ready, bool extendedReadings,
                          const std::function<double(const SliceModel*)>& maxBin, const QString& currentSessionId = {});
};
}
