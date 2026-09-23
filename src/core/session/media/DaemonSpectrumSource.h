#pragma once
// =================================================================
// src/core/session/media/DaemonSpectrumSource.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  A daemon-owned, explicit spectrum
// producer.  It intentionally contains no endpoint, reducer, codec or
// transport policy.
// =================================================================

#include "core/spectrum/FftEnginePool.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QVector>

#include <memory>
#include <optional>

class FFTEngine;

namespace NereusSDR {

class RadioModel;

struct DaemonSpectrumSourceConfig {
    FftPoolConfig fft;
    double centreHz{0.0};
    double sampleRateHz{0.0};
    /// Explicit queue capacity in interleaved floats.  There is deliberately
    /// no guessed default: the service accepts a bounded ingress budget.
    int maxPendingIqFloats{0};
    /// R-R3-08/40: the Core is busy, so each transform advances a whole
    /// frame period (FFTEngine::setTransformsFollowFrameRate). Like the
    /// frame rate it changes how far the window moves, not what a bin
    /// represents.
    bool transformsFollowFrameRate{false};
};

/// A complete, unreduced FFT frame.  `generation` changes on every accepted
/// activate/update so consumers can discard a previous frequency context.
/// `producedAtNs` is a monotonic producer timestamp, not wall-clock time.
struct DaemonSpectrumFrame {
    MediaSourceKey source;
    quint64 generation{0};
    double centreHz{0.0};
    double sampleRateHz{0.0};
    qint64 producedAtNs{0};
    QVector<float> binsLinear;
    double windowEnb{0.0};
    double dbmOffset{0.0};
};

class DaemonSpectrumSource final : public QObject {
    Q_OBJECT
public:
    explicit DaemonSpectrumSource(QObject* parent = nullptr);
    ~DaemonSpectrumSource() override;

    /// The RadioModel remains externally owned.  Supplying it wires its
    /// tagged I/Q signal straight to each active engine's worker thread;
    /// no I/Q packet is bounced through this object's thread.
    void setRadioModel(RadioModel* radioModel);

    /// Creates a source only after an explicit subscription activation.
    /// Returns false without side effects when config is invalid or the key
    /// already exists.  The first accepted generation is one.
    bool activate(const MediaSourceKey& key,
                  const DaemonSpectrumSourceConfig& config);

    /// Applies a validated source context and increments its generation.
    /// A changed RF/FFT context drops queued input and resets the engine's
    /// overlap history before the new generation accepts I/Q.
    bool update(const MediaSourceKey& key,
                const DaemonSpectrumSourceConfig& config);

    /// Changes only the engine's output frame rate and whether transforms
    /// follow it. Both set how far the FFT window advances between frames,
    /// not what a bin represents, so the generation, queued input and
    /// overlap history all stand and no consumer's context is renewed.
    /// Returns false for an unknown key or a rate outside 1..60.
    bool updateFrameRate(const MediaSourceKey& key, int fps,
                         bool transformsFollowFrameRate = false);

    /// Stops a single source tier and releases its engine. Safe for an
    /// unknown key.  The other tier for the same stream remains active.
    void deactivate(const MediaSourceKey& key);
    void deactivateStream(int streamIndex);

    bool isActive(const MediaSourceKey& key) const;
    QList<MediaSourceKey> activeSources() const;
    quint64 droppedInputFrames(const MediaSourceKey& key) const;
    /// Number of completed engine I/Q handoffs since source activation.
    quint64 completedInputHandoffs(const MediaSourceKey& key) const;
    /// Frames the engine published into this source's latest-frame slot,
    /// each replacing the one before (so it can exceed frameAvailable).
    quint64 publishedFrames(const MediaSourceKey& key) const;

    /// Returns and clears the one latest frame slot for key.  A slow consumer
    /// can therefore miss frames but cannot cause an output-frame backlog.
    std::optional<DaemonSpectrumFrame> takeLatest(const MediaSourceKey& key);

    /// Test and non-RadioModel ingress.  Real daemon production uses
    /// RadioModel::rawIqDataForStream.  This is queued to the engine thread
    /// and intentionally has the same input-backlog characteristic as that
    /// production route.
    void submitIq(int streamIndex, const QVector<float>& interleavedIq);

    /// Test seam: publishes one frame for an active source as its engine
    /// would, stamped `producedAtNs` instead of the clock, with every bin at
    /// `binLinear`, and emits frameAvailable now rather than queued. Lets a
    /// test drive the source's cadence itself. False for an unknown source
    /// or one whose engine has not yet taken its configuration.
    bool publishFrameForTest(const MediaSourceKey& key, qint64 producedAtNs,
                             float binLinear = 1.0e-6f);

signals:
    /// At most one queued notification per active source exists at a time.
    /// Consumers call takeLatest() to acquire the current frame.
    void frameAvailable(NereusSDR::MediaSourceKey key);

private:
    struct FrameState;
    struct SourceEntry;

    static bool isValidConfig(const MediaSourceKey& key,
                              const DaemonSpectrumSourceConfig& config);
    static bool inputHistoryIsCompatible(const DaemonSpectrumSourceConfig& before,
                                         const DaemonSpectrumSourceConfig& after);
    static qint64 monotonicNowNs();
    static void applyEngineConfig(FFTEngine* engine,
                                  const DaemonSpectrumSourceConfig& config);
    static void enqueueIq(FFTEngine* engine,
                          const QSharedPointer<FrameState>& state,
                          const QVector<float>& interleavedIq);

    void attachRadioIq(SourceEntry& entry);
    void queueConfiguredActivation(SourceEntry& entry,
                                   const DaemonSpectrumSourceConfig& config,
                                   quint64 generation,
                                   bool resetInputHistory);
    void publishFrame(const MediaSourceKey& key,
                      const QSharedPointer<FrameState>& state,
                      const QVector<float>& binsLinear,
                      double windowEnb,
                      double dbmOffset);

    QPointer<RadioModel> m_radioModel;
    std::unique_ptr<FftEnginePool> m_pool;
    QMap<MediaSourceKey, SourceEntry> m_sources;
    quint64 m_nextGeneration{0};
};

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::FftSourceKey)
Q_DECLARE_METATYPE(NereusSDR::DaemonSpectrumFrame)
