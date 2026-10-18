// =================================================================
// src/gui/setup/SoundSystemLine.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original "Sound system" status line for Setup > Audio >
// Outputs. See SoundSystemLine.h for the full header.
//
// 2026-10-06: Written for the radio speaker and Audio Setup plan, Task 9,
// by J.J. Boyd (KG4VCF), with AI-assisted implementation via Anthropic
// Claude Code.
// 2026-10-09: native audio plan Task 16 (R-AUD-01): the engine the device
// catalogue describes, then the older drivers in use; Windows reads
// "Windows audio (WASAPI)" and PulseAudio is talked to directly.
// J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: settings scope fix (R-AUD-07, R-AUD-20): the saved ASIO
//               buffer size and rate and the headphones Enabled box are
//               read through AudioEngine, so a Setup page does not read
//               a key a core consumer reads. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-14: describeEngine() shares the line's words with Network
//               Diagnostics' Audio Backend row. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#include "SoundSystemLine.h"
#include "gui/styles/WorkspacePreview.h"

#include "core/AudioDeviceConfig.h"
#include "core/AudioEngine.h"
#include "core/audio/IAudioDeviceCatalog.h"
#include "gui/setup/AudioDriverList.h"

#include <QHBoxLayout>
#include <QLabel>

namespace NereusSDR {

namespace {

constexpr int kDotPx = 8;

const char* const kDotOk =
    "QLabel { background: #33dd88; border-radius: 4px; }";
const char* const kDotProblem =
    "QLabel { background: #e04848; border-radius: 4px; }";
const char* const kTextOk = "QLabel { color: #8aa8c0; font-size: 12px; }";
const char* const kTextProblem = "QLabel { color: #e04848; font-size: 12px; }";

} // namespace

SoundSystemLine::SoundSystemLine(AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
{
    setObjectName(QStringLiteral("soundSystemLine"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_dot = new QLabel(this);
    m_dot->setObjectName(QStringLiteral("soundSystemDot"));
    m_dot->setFixedSize(kDotPx, kDotPx);
    layout->addWidget(m_dot, 0, Qt::AlignVCenter);

    m_text = new QLabel(this);
    m_text->setObjectName(QStringLiteral("soundSystemText"));
    m_text->setWordWrap(true);
    m_text->setTextFormat(Qt::RichText);
    layout->addWidget(m_text, 1);

#if defined(Q_OS_LINUX)
    if (m_engine != nullptr) {
        connect(m_engine, &AudioEngine::linuxBackendChanged, this,
                [this](LinuxAudioBackend, LinuxAudioBackend) { refresh(); });
    }
#endif
    refresh();
}

SoundSystemLine::System SoundSystemLine::thisSystem()
{
#if defined(Q_OS_MAC)
    return System::Mac;
#elif defined(Q_OS_WIN)
    return System::Windows;
#else
    return System::Linux;
#endif
}

QString SoundSystemLine::describe(System system, LinuxAudioBackend backend)
{
    switch (system) {
    case System::Mac:
        return tr("Core Audio");
    case System::Windows:
        return tr("Windows audio (WASAPI)");
    case System::Linux:
        break;
    }
    switch (backend) {
    case LinuxAudioBackend::PipeWire:
        return tr("PipeWire. NereusSDR talks to it directly.");
    case LinuxAudioBackend::Pactl:
        return tr("PulseAudio. PipeWire was not found, so NereusSDR talks to "
                  "PulseAudio directly.");
    case LinuxAudioBackend::None:
        break;
    }
    return tr("None found. Start PipeWire or PulseAudio, then click Rescan devices.");
}

bool SoundSystemLine::isProblem(System system, LinuxAudioBackend backend)
{
    return system == System::Linux && backend == LinuxAudioBackend::None;
}

QString SoundSystemLine::text() const
{
    return m_described;
}

namespace {

// The choices the line reads: the Speakers, the Headphones while Enabled,
// and the Microphone.
QList<AudioDeviceConfig> choicesInUse()
{
    QList<AudioDeviceConfig> choices;
    choices.append(AudioDeviceConfig::loadFromSettings(QStringLiteral("audio/Speakers")));
    if (AudioEngine::savedHeadphonesEnabled()) {
        choices.append(AudioDeviceConfig::loadFromSettings(QStringLiteral("audio/Headphones")));
    }
    choices.append(AudioDeviceConfig::loadFromSettings(QStringLiteral("audio/TxInput")));
    return choices;
}

} // namespace

QStringList SoundSystemLine::olderDriversInUse()
{
    QStringList names;
    for (const AudioDeviceConfig& cfg : choicesInUse()) {
        if (cfg.engine == AudioEngineKind::PortAudio && !cfg.driverApi.isEmpty()) {
            const QString name = olderDriverDisplayName(cfg.driverApi);
            if (!names.contains(name)) {
                names.append(name);
            }
        }
    }
    return names;
}

QString SoundSystemLine::asioDriverInUse()
{
    for (const AudioDeviceConfig& cfg : choicesInUse()) {
        if (cfg.engine == AudioEngineKind::Asio && !cfg.deviceName.isEmpty()) {
            return cfg.deviceName;
        }
    }
    return {};
}

SoundSystemLine::Description SoundSystemLine::describeFor(IAudioDeviceCatalog* catalogue,
                                                         AudioEngine* engine)
{
    Description described;
    if (catalogue != nullptr) {
        described.text = soundSystemDescription(*catalogue, asioDriverInUse());
        described.problem = soundSystemMissing(*catalogue);
    }
    if (described.text.isEmpty()) {
        LinuxAudioBackend backend = LinuxAudioBackend::None;
#if defined(Q_OS_LINUX)
        if (engine != nullptr) {
            backend = engine->linuxBackend();
        }
#else
        Q_UNUSED(engine);
#endif
        described.text = describe(thisSystem(), backend);
        described.problem = isProblem(thisSystem(), backend);
    }
    described.text = withOlderDriversInUse(described.text, olderDriversInUse());
    return described;
}

QString SoundSystemLine::describeEngine(AudioEngine* engine)
{
    IAudioDeviceCatalog* catalogue = engine != nullptr ? engine->catalogue() : nullptr;
    return describeFor(catalogue, engine).text;
}

void SoundSystemLine::refresh()
{
    if (m_engine != nullptr && !m_catalogue) {
        if (IAudioDeviceCatalog* catalogue = m_engine->catalogue()) {
            m_catalogue = catalogue;
            connect(catalogue, &IAudioDeviceCatalog::devicesChanged, this,
                    [this]() { refresh(); });
        }
    }
    const Description described = describeFor(m_catalogue.data(), m_engine);
    show(described.text, described.problem);
}

void SoundSystemLine::show(const QString& described, bool problem)
{
    m_described = described;
    m_problem = problem;
    m_dot->setStyleSheet(QLatin1String(m_problem ? kDotProblem : kDotOk));
    m_text->setStyleSheet(QLatin1String(m_problem ? kTextProblem : kTextOk));
    m_text->setText(QStringLiteral("<b style=\"color:%3\">%1</b> %2")
                        .arg(tr("Sound system:"), m_described.toHtmlEscaped(),
                             WorkspacePreview::roleColourName("#c8d8e8", "text")));
}

} // namespace NereusSDR
