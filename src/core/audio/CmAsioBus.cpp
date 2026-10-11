// =================================================================
// src/core/audio/CmAsioBus.cpp  (SQ4KOU NereusSDR private build)
// =================================================================
// Runtime bridge to the C ABI exported by Thetis cmASIO.dll.
// No Steinberg ASIO SDK material is compiled into NereusSDR.
// =================================================================

#include "CmAsioBus.h"

#include "AudioRingSpsc.h"
#include "core/LogCategories.h"
#include "core/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QLibrary>
#include <QByteArray>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace NereusSDR {

namespace {

#if defined(Q_OS_WIN)

using AsioCallback = void (__cdecl *)(void*, void*, void*, void*);
using PrepareAsioFn = int (__cdecl *)(int, int, char*, AsioCallback, long, long);
using VoidFn = void (__cdecl *)();
using LongNoArgFn = long (__cdecl *)();
using LongPtrFn = long (__cdecl *)(void*);

constexpr int kCmAsioSampleRate = 48000;
constexpr int kCmAsioChannels = 2;
constexpr int kDefaultBlockSamples = 64;
constexpr double kInt32Scale = 2147483648.0;
constexpr double kInt32Max = 2147483647.0;
constexpr double kInt32Min = -2147483648.0;

QString cmAsioDllPathImpl()
{
    const QString overridePath = qEnvironmentVariable("NEREUS_CMASIO_DLL");
    if (!overridePath.isEmpty()) {
        return QDir::toNativeSeparators(overridePath);
    }
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("cmASIO.dll"));
}

QVector<CmAsioDeviceInfo> registryAsioDevices()
{
    QVector<CmAsioDeviceInfo> out;
    QSet<QString> seen;
    const QStringList roots = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\ASIO"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\ASIO"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\WOW6432Node\\ASIO")
    };

    for (const QString& root : roots) {
        QSettings reg(root, QSettings::NativeFormat);
        for (const QString& raw : reg.childGroups()) {
            const QString name = raw.trimmed();
            if (name.isEmpty()) continue;
            const QString key = name.toCaseFolded();
            if (seen.contains(key)) continue;
            seen.insert(key);
            CmAsioDeviceInfo d;
            d.name = name.left(31); // cmASIO ABI: 32 bytes including NUL
            out.push_back(d);
        }
    }

    std::sort(out.begin(), out.end(),
              [](const CmAsioDeviceInfo& a, const CmAsioDeviceInfo& b) {
                  return QString::localeAwareCompare(a.name, b.name) < 0;
              });
    return out;
}

class CmAsioSession final {
public:
    static CmAsioSession& instance()
    {
        static CmAsioSession s;
        return s;
    }

    bool acquire(CmAsioDirection direction,
                 const AudioFormat& requested,
                 const CmAsioSettings& settings,
                 QString& error)
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);

        if (requested.sample != AudioFormat::Sample::Float32) {
            error = QStringLiteral("cmASIO requires Float32 Nereus bus format");
            return false;
        }
        if (requested.sampleRate != kCmAsioSampleRate) {
            error = QStringLiteral("cmASIO STEP1 supports 48000 Hz only");
            return false;
        }
        if (requested.channels != kCmAsioChannels) {
            error = QStringLiteral("cmASIO STEP1 requires stereo (2 channels)");
            return false;
        }

        if (!m_running) {
            if (!startSession(settings, error)) {
                return false;
            }
        } else {
            if (settings.driverName != m_driverName ||
                settings.inputBase != m_inputBase ||
                settings.outputBase != m_outputBase ||
                settings.inputMode != m_inputMode ||
                settings.blockSamples != m_blockSamples ||
                settings.blockNum != m_blockNum) {
                error = QStringLiteral(
                    "cmASIO session already active with different settings; restart NereusSDR");
                return false;
            }
        }

        if (direction == CmAsioDirection::Output) {
            ++m_outputRefs;
        } else {
            ++m_inputRefs;
        }
        return true;
    }

    void release(CmAsioDirection direction)
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);
        if (direction == CmAsioDirection::Output) {
            if (m_outputRefs > 0) { --m_outputRefs; }
        } else {
            if (m_inputRefs > 0) { --m_inputRefs; }
        }

        if (m_running && m_outputRefs == 0 && m_inputRefs == 0) {
            stopSession();
        }
    }

    bool running() const noexcept
    {
        return m_running.load(std::memory_order_acquire);
    }

    qint64 pushOutput(const char* data, qint64 bytes)
    {
        if (!running() || data == nullptr || bytes <= 0) { return 0; }
        const qint64 frameBytes = kCmAsioChannels * qint64(sizeof(float));
        bytes -= (bytes % frameBytes);
        // AudioRingSpsc reserves one byte; never allow a call to be
        // silently shortened to a non-frame-aligned 65535-byte chunk.
        if (bytes <= 0 || bytes >= (1U << 16)) {
            m_outputOverruns.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        const auto* src = reinterpret_cast<const uint8_t*>(data);
        const qint64 written = m_outputRing.tryPushCopy(src, bytes);
        if (written == 0) {
            m_outputOverruns.fetch_add(1, std::memory_order_relaxed);
        }

        const float* f = reinterpret_cast<const float*>(data);
        const qint64 samples = bytes / qint64(sizeof(float));
        float peak = 0.0f;
        for (qint64 i = 0; i < samples; ++i) {
            peak = std::max(peak, std::abs(f[i]));
        }
        m_outputLevel.store(peak, std::memory_order_release);
        return written;
    }

    qint64 pullInput(char* data, qint64 maxBytes)
    {
        if (!running() || data == nullptr || maxBytes <= 0) { return 0; }
        const qint64 frameBytes = kCmAsioChannels * qint64(sizeof(float));
        maxBytes -= (maxBytes % frameBytes);
        if (maxBytes <= 0) { return 0; }
        // TX/VOX can begin after minutes of RX with no input consumer.
        // The ASIO callback must never advance the consumer's read cursor.
        // Drop a stale backlog here, on the one TX consumer thread; the
        // next ASIO callback then fills the ring with live mic samples.
        // Normal pull cadence (<= 50 ms queued) is unchanged.
        constexpr size_t kMaxFreshInputBytes = 2400U * 2U * sizeof(float);
        const size_t queued = m_inputRing.usedBytes();
        if (queued > kMaxFreshInputBytes) {
            m_inputRing.dropOldest(queued);
            return 0;
        }
        return m_inputRing.popInto(reinterpret_cast<uint8_t*>(data), maxBytes);
    }

    void requestOutputFlush() noexcept
    {
        m_flushOutput.store(true, std::memory_order_release);
    }

    float outputLevel() const noexcept
    {
        return m_outputLevel.load(std::memory_order_acquire);
    }

    float inputLevel() const noexcept
    {
        return m_inputLevel.load(std::memory_order_acquire);
    }

    std::optional<IAudioBus::OutputPacing> outputPacing() const noexcept
    {
        if (!running()) { return std::nullopt; }
        IAudioBus::OutputPacing result;
        result.consumedFrames = m_outputConsumedFrames.load(std::memory_order_acquire);
        result.queuedFrames = int(m_outputRing.usedBytes() / (2 * sizeof(float)));
        // Block Num caps advertised queue depth; callback block is independent.
        const int physical = int(((1U << 16) - 1U) / (2 * sizeof(float)));
        result.capacityFrames = std::min(physical,
            std::max(2 * m_blockSamples, m_blockNum * m_blockSamples));
        result.callbackFrames = m_blockSamples;
        // Device latency beyond the callback quantum is driver-dependent.
        return result;
    }

    int blockSamples() const noexcept { return m_blockSamples; }
    int inputMode() const noexcept { return m_inputMode; }
    QString driverName() const { return m_driverName; }

private:
    CmAsioSession() = default;
    ~CmAsioSession()
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);
        if (m_running) { stopSession(); }
    }

    static void __cdecl asioCallback(void* inputL, void* inputR,
                                     void* outputL, void* outputR)
    {
        CmAsioSession* s = s_active.load(std::memory_order_acquire);
        if (s != nullptr) {
            s->processCallback(inputL, inputR, outputL, outputR);
        }
    }

    template <typename T>
    bool resolve(const char* symbol, T& out, QString& error)
    {
        out = reinterpret_cast<T>(m_library.resolve(symbol));
        if (out == nullptr) {
            error = QStringLiteral("cmASIO.dll missing export: %1")
                        .arg(QString::fromLatin1(symbol));
            return false;
        }
        return true;
    }

    bool startSession(const CmAsioSettings& requestedSettings, QString& error)
    {
        const QString dllPath = cmAsioDllPathImpl();
        if (!QFileInfo::exists(dllPath)) {
            error = QStringLiteral("cmASIO.dll not found: %1").arg(dllPath);
            return false;
        }

        m_library.setFileName(dllPath);
        if (!m_library.load()) {
            error = QStringLiteral("Cannot load cmASIO.dll: %1")
                        .arg(m_library.errorString());
            return false;
        }

        if (!resolve("prepareASIO", m_prepare, error) ||
            !resolve("unloadASIO", m_unload, error) ||
            !resolve("asioStart", m_start, error) ||
            !resolve("asioStop", m_stop, error) ||
            !resolve("getASIODriverString", m_getDriver, error) ||
            !resolve("getASIOBaseInputChannel", m_getBaseIn, error) ||
            !resolve("getASIOBaseOutputChannel", m_getBaseOut, error) ||
            !resolve("getASIOInputMode", m_getInputMode, error)) {
            m_library.unload();
            return false;
        }

        // Settings are explicitly owned by NereusSDR; no hidden overrides.
        CmAsioSettings settings = requestedSettings;

        settings.driverName = settings.driverName.trimmed().left(31);
        if (settings.driverName.isEmpty()) {
            error = QStringLiteral("No ASIO driver selected. Use Setup -> Audio -> cmASIO.");
            m_library.unload();
            return false;
        }
        if (settings.inputBase < 0 || settings.inputBase > 62 ||
            settings.outputBase < 0 || settings.outputBase > 62) {
            error = QStringLiteral("Invalid cmASIO base channel");
            m_library.unload();
            return false;
        }
        settings.inputMode = std::clamp(settings.inputMode, 0, 2);
        if (settings.blockSamples < 16 || settings.blockSamples > 2048) {
            error = QStringLiteral("Invalid cmASIO block size=%1")
                        .arg(settings.blockSamples);
            m_library.unload();
            return false;
        }

        if (settings.blockNum < 2 || settings.blockNum > 128) {
            error = QStringLiteral("Invalid cmASIO Block Num=%1").arg(settings.blockNum);
            m_library.unload();
            return false;
        }

        QByteArray driverBytes = settings.driverName.toLocal8Bit();
        if (driverBytes.size() > 31) driverBytes.truncate(31);
        char driver[32] = {};
        std::memcpy(driver, driverBytes.constData(), size_t(driverBytes.size()));

        const long baseIn = settings.inputBase;
        const long baseOut = settings.outputBase;
        const int block = settings.blockSamples;

        // Clear stale samples from a previous open.  No ASIO callback is
        // running yet, so this is race-free despite dropOldest being the
        // consumer-side primitive.
        m_outputRing.dropOldest(m_outputRing.usedBytes());
        m_inputRing.dropOldest(m_inputRing.usedBytes());
        m_flushOutput.store(false, std::memory_order_release);
        m_outputLevel.store(0.0f, std::memory_order_release);
        m_outputConsumedFrames.store(0, std::memory_order_release);
        m_inputLevel.store(0.0f, std::memory_order_release);

        m_blockSamples = block;
        m_blockNum = settings.blockNum;
        m_inputMode = settings.inputMode;
        m_inputBase = settings.inputBase;
        m_outputBase = settings.outputBase;
        m_driverName = QString::fromLocal8Bit(driver);
        m_inputScratch.resize(size_t(block) * 2U);
        m_outputScratch.resize(size_t(block) * 2U);

        const int prep = m_prepare(block, kCmAsioSampleRate, driver,
                                   &CmAsioSession::asioCallback,
                                   baseIn, baseOut);
        if (prep != 0) {
            error = QStringLiteral(
                "prepareASIO failed (%1), driver='%2', block=%3, inBase=%4, outBase=%5")
                        .arg(prep)
                        .arg(m_driverName)
                        .arg(block)
                        .arg(baseIn)
                        .arg(baseOut);
            m_library.unload();
            return false;
        }

        s_active.store(this, std::memory_order_release);
        // ASIO drivers may invoke the first buffer callback from inside
        // ASIOStart(). Mark the session live before calling it so that first
        // callback always fills the output buffers (silence if the ring is
        // still empty) rather than returning with driver-owned memory stale.
        m_running.store(true, std::memory_order_release);
        const long startResult = m_start();
        if (startResult != 0) {
            m_running.store(false, std::memory_order_release);
            s_active.store(nullptr, std::memory_order_release);
            m_unload();
            error = QStringLiteral("asioStart failed (%1)").arg(startResult);
            m_library.unload();
            return false;
        }
        qCInfo(lcAudio) << "cmASIO started: driver=" << m_driverName
                        << "rate=" << kCmAsioSampleRate
                        << "block=" << block
                        << "blockNum=" << m_blockNum
                        << "inputBase=" << baseIn
                        << "outputBase=" << baseOut
                        << "inputMode=" << m_inputMode;
        return true;
    }

    void stopSession()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (m_stop) {
            const long r = m_stop();
            qCInfo(lcAudio) << "cmASIO asioStop result=" << r;
        }
        s_active.store(nullptr, std::memory_order_release);
        if (m_unload) {
            m_unload();
        }

        m_prepare = nullptr;
        m_unload = nullptr;
        m_start = nullptr;
        m_stop = nullptr;
        m_getDriver = nullptr;
        m_getBaseIn = nullptr;
        m_getBaseOut = nullptr;
        m_getInputMode = nullptr;
        m_library.unload();
        qCInfo(lcAudio) << "cmASIO session closed";
    }

    void processCallback(void* inputL, void* inputR,
                         void* outputL, void* outputR)
    {
        if (!m_running.load(std::memory_order_acquire) ||
            inputL == nullptr || inputR == nullptr ||
            outputL == nullptr || outputR == nullptr) {
            return;
        }

        auto* inL = static_cast<int32_t*>(inputL);
        auto* inR = static_cast<int32_t*>(inputR);
        auto* outL = static_cast<int32_t*>(outputL);
        auto* outR = static_cast<int32_t*>(outputR);
        const int n = m_blockSamples;

        // Capture: convert ASIO Int32LSB planar -> Nereus Float32 stereo.
        // For the main TX-mic path Nereus ultimately consumes channel 0;
        // therefore Left / Right / Both are collapsed to the selected mono
        // value and duplicated to L/R here.  Both = 0.5*(L+R), matching the
        // conservative VAX stereo->mono convention already used by AudioEngine.
        float inputPeak = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float l = float(double(inL[i]) / kInt32Scale);
            const float r = float(double(inR[i]) / kInt32Scale);
            float mono = 0.0f;
            if (m_inputMode == 0) {
                mono = l;
            } else if (m_inputMode == 1) {
                mono = r;
            } else {
                mono = 0.5f * (l + r);
            }
            m_inputScratch[size_t(i) * 2U] = mono;
            m_inputScratch[size_t(i) * 2U + 1U] = mono;
            inputPeak = std::max(inputPeak, std::abs(mono));
        }
        m_inputLevel.store(inputPeak, std::memory_order_release);
        const qint64 inBytes = qint64(n) * 2 * qint64(sizeof(float));
        if (m_inputRefs.load(std::memory_order_acquire) > 0 && m_inputRing.tryPushCopy(
                reinterpret_cast<const uint8_t*>(m_inputScratch.data()),
                inBytes) == 0) {
            m_inputOverruns.fetch_add(1, std::memory_order_relaxed);
        }

        // Render: Nereus Float32 stereo -> ASIO Int32LSB planar.
        if (m_flushOutput.exchange(false, std::memory_order_acq_rel)) {
            m_outputRing.dropOldest(m_outputRing.usedBytes());
        }

        // Bound local RX latency without changing the ASIO callback size.
        // The ASIO callback is the SPSC consumer, so it alone may discard
        // stale frames. A two-callback margin tolerates ordinary scheduling
        // jitter; nothing changes while the queue is below that threshold.
        const size_t targetFrames = size_t(std::max(2 * n, m_blockNum * n));
        const size_t bytesPerFrame = 2U * sizeof(float);
        const size_t targetBytes = targetFrames * bytesPerFrame;
        const size_t queuedBytes = m_outputRing.usedBytes();
        if (queuedBytes > targetBytes + size_t(2 * n) * bytesPerFrame) {
            size_t toDrop = queuedBytes - targetBytes;
            toDrop -= toDrop % bytesPerFrame;
            m_outputRing.dropOldest(toDrop);
            m_outputBacklogDrops.fetch_add(1, std::memory_order_relaxed);
        }

        std::fill(m_outputScratch.begin(), m_outputScratch.end(), 0.0f);
        const qint64 wantBytes = qint64(n) * 2 * qint64(sizeof(float));
        const qint64 gotBytes = m_outputRing.popInto(
            reinterpret_cast<uint8_t*>(m_outputScratch.data()), wantBytes);
        m_outputConsumedFrames.fetch_add(quint64(n), std::memory_order_release);
        if (gotBytes < wantBytes) {
            m_outputUnderruns.fetch_add(1, std::memory_order_relaxed);
        }

        float outputPeak = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float rawL = m_outputScratch[size_t(i) * 2U];
            const float rawR = m_outputScratch[size_t(i) * 2U + 1U];
            // A non-finite DSP sample must never be converted to int32:
            // converting NaN/Inf to a signed integer is undefined behavior.
            const float safeL = std::isfinite(rawL) ? rawL : 0.0f;
            const float safeR = std::isfinite(rawR) ? rawR : 0.0f;
            const double l = std::clamp(double(safeL) * kInt32Scale,
                                        kInt32Min, kInt32Max);
            const double r = std::clamp(double(safeR) * kInt32Scale,
                                        kInt32Min, kInt32Max);
            outL[i] = static_cast<int32_t>(l);
            outR[i] = static_cast<int32_t>(r);
            outputPeak = std::max(outputPeak,
                                  std::max(std::abs(safeL), std::abs(safeR)));
        }
        m_outputLevel.store(outputPeak, std::memory_order_release);
    }

    std::mutex m_controlMutex;
    QLibrary m_library;

    PrepareAsioFn m_prepare{nullptr};
    VoidFn m_unload{nullptr};
    LongNoArgFn m_start{nullptr};
    LongNoArgFn m_stop{nullptr};
    LongPtrFn m_getDriver{nullptr};
    LongPtrFn m_getBaseIn{nullptr};
    LongPtrFn m_getBaseOut{nullptr};
    LongPtrFn m_getInputMode{nullptr};

    std::atomic<bool> m_running{false};
    int m_outputRefs{0};
    std::atomic<int> m_inputRefs{0};
    int m_blockSamples{kDefaultBlockSamples};
    int m_blockNum{16};
    int m_inputMode{2};
    int m_inputBase{0};
    int m_outputBase{0};
    QString m_driverName;

    // 8192 stereo frames (170 ms maximum) instead of a 2.7s queue.
    AudioRingSpsc<(1U << 16)> m_outputRing;
    AudioRingSpsc<(1U << 20)> m_inputRing;
    std::vector<float> m_inputScratch;
    std::vector<float> m_outputScratch;

    std::atomic<bool> m_flushOutput{false};
    std::atomic<float> m_outputLevel{0.0f};
    std::atomic<float> m_inputLevel{0.0f};
    std::atomic<quint64> m_outputOverruns{0};
    std::atomic<quint64> m_inputOverruns{0};
    std::atomic<quint64> m_outputUnderruns{0};
    std::atomic<quint64> m_outputBacklogDrops{0};
    std::atomic<quint64> m_outputConsumedFrames{0};

    static std::atomic<CmAsioSession*> s_active;
};

std::atomic<CmAsioSession*> CmAsioSession::s_active{nullptr};

#endif // Q_OS_WIN

} // namespace


CmAsioSettings CmAsioSettings::load()
{
    auto& s = AppSettings::instance();
    const QString base = QStringLiteral("audio/CmAsio/");

    // Never enable cmASIO based on a Thetis registry entry.  An audio
    // backend must be explicitly selected by the operator in NereusSDR;
    // switching it on without consent can prevent the SDR from starting.
    if (!s.contains(base + QStringLiteral("Configured"))) {
        CmAsioSettings fresh;
        fresh.save();
        return fresh;
    }

    CmAsioSettings cfg;
    cfg.enabled = s.value(base + QStringLiteral("Enabled"),
                          QStringLiteral("False")).toString() == QStringLiteral("True");
    cfg.txEnabled = s.value(base + QStringLiteral("TxEnabled"),
                            QStringLiteral("False")).toString() == QStringLiteral("True");
    cfg.driverName = s.value(base + QStringLiteral("DriverName"), QString()).toString();
    cfg.inputBase = s.value(base + QStringLiteral("InputBase"), QStringLiteral("0")).toInt();
    cfg.outputBase = s.value(base + QStringLiteral("OutputBase"), QStringLiteral("0")).toInt();
    cfg.inputMode = s.value(base + QStringLiteral("InputMode"), QStringLiteral("2")).toInt();
    cfg.blockSamples = s.value(base + QStringLiteral("BlockSamples"),
                               QStringLiteral("64")).toInt();
    cfg.blockNum = s.value(base + QStringLiteral("BlockNum"),
                           QStringLiteral("16")).toInt();

    cfg.inputBase = std::clamp(cfg.inputBase, 0, 62);
    cfg.outputBase = std::clamp(cfg.outputBase, 0, 62);
    cfg.inputMode = std::clamp(cfg.inputMode, 0, 2);
    cfg.blockSamples = std::clamp(cfg.blockSamples, 16, 2048);
    cfg.blockNum = std::clamp(cfg.blockNum, 2, 128);
    cfg.driverName = cfg.driverName.trimmed().left(31);
    return cfg;
}

void CmAsioSettings::save() const
{
    auto& s = AppSettings::instance();
    const QString base = QStringLiteral("audio/CmAsio/");
    s.setValue(base + QStringLiteral("Configured"), QStringLiteral("True"));
    s.setValue(base + QStringLiteral("Enabled"),
               enabled ? QStringLiteral("True") : QStringLiteral("False"));
    s.setValue(base + QStringLiteral("TxEnabled"),
               txEnabled ? QStringLiteral("True") : QStringLiteral("False"));
    s.setValue(base + QStringLiteral("DriverName"), driverName.trimmed().left(31));
    s.setValue(base + QStringLiteral("InputBase"),
               QString::number(std::clamp(inputBase, 0, 62)));
    s.setValue(base + QStringLiteral("OutputBase"),
               QString::number(std::clamp(outputBase, 0, 62)));
    s.setValue(base + QStringLiteral("InputMode"),
               QString::number(std::clamp(inputMode, 0, 2)));
    s.setValue(base + QStringLiteral("BlockSamples"),
               QString::number(std::clamp(blockSamples, 16, 2048)));
    s.setValue(base + QStringLiteral("BlockNum"),
               QString::number(std::clamp(blockNum, 2, 128)));
}

QVector<CmAsioDeviceInfo> CmAsioBus::availableDevices()
{
#if defined(Q_OS_WIN)
    return registryAsioDevices();
#else
    return {};
#endif
}

bool CmAsioBus::dllAvailable()
{
#if defined(Q_OS_WIN)
    return QFileInfo::exists(cmAsioDllPathImpl());
#else
    return false;
#endif
}

QString CmAsioBus::dllPath()
{
#if defined(Q_OS_WIN)
    return cmAsioDllPathImpl();
#else
    return {};
#endif
}

struct CmAsioBus::Impl {
    CmAsioConfig cfg;
    AudioFormat negotiated;
    QString error;
    bool open{false};
};

CmAsioBus::CmAsioBus()
    : m_impl(std::make_unique<Impl>())
{
}

CmAsioBus::~CmAsioBus()
{
    close();
}

void CmAsioBus::setConfig(const CmAsioConfig& cfg)
{
    if (!m_impl->open) {
        m_impl->cfg = cfg;
    }
}

bool CmAsioBus::open(const AudioFormat& format)
{
    if (m_impl->open) { return true; }
    m_impl->error.clear();

#if defined(Q_OS_WIN)
    if (!CmAsioSession::instance().acquire(
            m_impl->cfg.direction, format,
            m_impl->cfg.settings, m_impl->error)) {
        return false;
    }
    m_impl->negotiated = format;
    m_impl->negotiated.sampleRate = kCmAsioSampleRate;
    m_impl->negotiated.channels = kCmAsioChannels;
    m_impl->negotiated.sample = AudioFormat::Sample::Float32;
    m_impl->open = true;
    return true;
#else
    Q_UNUSED(format);
    m_impl->error = QStringLiteral("cmASIO is Windows-only");
    return false;
#endif
}

void CmAsioBus::close()
{
    if (!m_impl->open) { return; }
#if defined(Q_OS_WIN)
    CmAsioSession::instance().release(m_impl->cfg.direction);
#endif
    m_impl->open = false;
}

bool CmAsioBus::isOpen() const
{
#if defined(Q_OS_WIN)
    return m_impl->open && CmAsioSession::instance().running();
#else
    return false;
#endif
}

qint64 CmAsioBus::push(const char* data, qint64 bytes)
{
#if defined(Q_OS_WIN)
    if (!m_impl->open || m_impl->cfg.direction != CmAsioDirection::Output) {
        return 0;
    }
    return CmAsioSession::instance().pushOutput(data, bytes);
#else
    Q_UNUSED(data); Q_UNUSED(bytes);
    return 0;
#endif
}

qint64 CmAsioBus::pull(char* data, qint64 maxBytes)
{
#if defined(Q_OS_WIN)
    if (!m_impl->open || m_impl->cfg.direction != CmAsioDirection::Input) {
        return 0;
    }
    return CmAsioSession::instance().pullInput(data, maxBytes);
#else
    Q_UNUSED(data); Q_UNUSED(maxBytes);
    return 0;
#endif
}

std::optional<IAudioBus::OutputPacing> CmAsioBus::outputPacing() const
{
#if defined(Q_OS_WIN)
    if (m_impl->open && m_impl->cfg.direction == CmAsioDirection::Output) {
        return CmAsioSession::instance().outputPacing();
    }
#endif
    return std::nullopt;
}

void CmAsioBus::flush()
{
#if defined(Q_OS_WIN)
    if (m_impl->open && m_impl->cfg.direction == CmAsioDirection::Output) {
        CmAsioSession::instance().requestOutputFlush();
    }
#endif
}

float CmAsioBus::rxLevel() const
{
#if defined(Q_OS_WIN)
    if (m_impl->cfg.direction == CmAsioDirection::Output) {
        return CmAsioSession::instance().outputLevel();
    }
#endif
    return 0.0f;
}

float CmAsioBus::txLevel() const
{
#if defined(Q_OS_WIN)
    if (m_impl->cfg.direction == CmAsioDirection::Input) {
        return CmAsioSession::instance().inputLevel();
    }
#endif
    return 0.0f;
}

QString CmAsioBus::backendName() const
{
#if defined(Q_OS_WIN)
    const QString driver = CmAsioSession::instance().driverName();
    if (!driver.isEmpty()) {
        return QStringLiteral("cmASIO / %1").arg(driver);
    }
#endif
    return QStringLiteral("cmASIO");
}

AudioFormat CmAsioBus::negotiatedFormat() const
{
    return m_impl->negotiated;
}

QString CmAsioBus::errorString() const
{
    return m_impl->error;
}

} // namespace NereusSDR
