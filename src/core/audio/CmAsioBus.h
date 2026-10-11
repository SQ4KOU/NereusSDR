#pragma once

// =================================================================
// src/core/audio/CmAsioBus.h  (SQ4KOU NereusSDR private build)
// =================================================================
// Windows ASIO output endpoint using the Thetis ChannelMaster cmASIO C ABI.
//
// IMPORTANT LICENSING BOUNDARY
// ----------------------------
// This file does not include, embed, or link against Steinberg ASIO SDK
// headers/libraries.  It loads the user's locally-built Thetis cmASIO.dll
// at runtime and resolves the small C ABI exported by hostsample.h.
// The one-click package likewise does not redistribute the ASIO SDK.
// =================================================================

#include "core/IAudioBus.h"
#include "core/NereusCoreExport.h"

#include <memory>
#include <QString>
#include <QVector>

namespace NereusSDR {

enum class CmAsioDirection {
    Output,
    Input,
};

struct NEREUS_CORE_EXPORT CmAsioSettings {
    bool enabled = false; // RX Speakers (existing behavior)
    bool txEnabled = false; // PC microphone TX (independent opt-in)
    QString driverName;
    int inputBase = 0;
    int outputBase = 0;
    int inputMode = 2;       // 0=Left, 1=Right, 2=Both
    int blockSamples = 64;
    int blockNum = 16; // separate FIFO depth in callback-sized blocks (not callback size)

    static CmAsioSettings load();
    void save() const;
};

struct CmAsioDeviceInfo {
    QString name;
    int inputChannels = -1;
    int outputChannels = -1;
};

struct CmAsioConfig {
    CmAsioDirection direction = CmAsioDirection::Output;
    CmAsioSettings settings;
};

class NEREUS_CORE_EXPORT CmAsioBus final : public IAudioBus {
public:
    CmAsioBus();
    ~CmAsioBus() override;

    static QVector<CmAsioDeviceInfo> availableDevices();
    static bool dllAvailable();
    static QString dllPath();

    void setConfig(const CmAsioConfig& cfg);

    bool open(const AudioFormat& format) override;
    void close() override;
    bool isOpen() const override;

    qint64 push(const char* data, qint64 bytes) override;
    qint64 pull(char* data, qint64 maxBytes) override;
    void flush() override;
    std::optional<OutputPacing> outputPacing() const override;

    float rxLevel() const override;
    float txLevel() const override;

    QString backendName() const override;
    AudioFormat negotiatedFormat() const override;
    QString errorString() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace NereusSDR
