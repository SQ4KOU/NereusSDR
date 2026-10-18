// no-port-check: NereusSDR-original CAT station admission/lifecycle.
// Independently implemented from the approved Nereus settings schema; no upstream serialization logic.
// 2026-10-04 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// 2026-10-11 One serial device under two spellings compares equal.
//            J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// 2026-10-12 A refused global setting names the rule it broke.
//            J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#pragma once
#include "CatConfiguration.h"
#include <QList>
#include <functional>
#include <memory>
#include <optional>
namespace NereusSDR {
class AppSettings; class RadioModel;
class CatSettings {
public:
    explicit CatSettings(AppSettings& settings);
    static QList<CatEndpointConfig> load(AppSettings&, const RadioModel&);
    static bool save(AppSettings&, CatEndpointConfig, std::function<bool()> current = {});
    static bool validate(const CatEndpointConfig&, QString* reason = nullptr);
    CatGlobalConfig global() const;
    bool setGlobal(CatGlobalConfig, std::function<bool()> current = {});
    static bool validateGlobal(const CatGlobalConfig&, QString* reason = nullptr);
#ifdef Q_OS_WIN
    static constexpr bool kWindowsSerialNames = true;
#else
    static constexpr bool kWindowsSerialNames = false;
#endif
    // What a serial device name is compared by, so one device is never assigned twice.
    static QString serialDeviceKey(const QString& name, bool windowsNames = kWindowsSerialNames);
    static bool sameSerialDevice(const QString& a, const QString& b, bool windowsNames = kWindowsSerialNames);
private:
    struct WriteState { quint64 revision{0}; std::optional<CatGlobalConfig> desired; };
    std::shared_ptr<WriteState> m_writeState{std::make_shared<WriteState>()};
    AppSettings& m_settings;
};
} // namespace NereusSDR
