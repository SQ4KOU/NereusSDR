// =================================================================
// src/gui/VaxFirstRunCheck.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original; no upstream logic. See
// VaxFirstRunCheck.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-11: taken out of MainWindow::checkVaxFirstRun() (R-AUD-03);
//               the header says what changed on the way. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/VaxFirstRunCheck.h"

#include "core/AppSettings.h"
#include "core/audio/IAudioDeviceCatalog.h"

namespace NereusSDR {

VaxFirstRunCheck::VaxFirstRunCheck(IAudioDeviceCatalog* catalog, QObject* parent)
    : QObject(parent)
    , m_catalog(catalog)
    , m_fromCatalog(catalog != nullptr)
    , m_scan([] { return VirtualCableDetector::scan(); })
{
}

// First-run/rescan wants the "relevant" virtual cables for the current
// platform — 3rd-party cables on Windows (BYO), our own NereusSdrVax
// entries on Mac/Linux (native HAL plugin / pipe-source). Centralising
// the platform split here keeps checkVaxFirstRun() focused on
// scenario-selection + dialog wiring.
// [original comment from MainWindow.cpp detectedForFirstRun()]
QVector<DetectedCable> VaxFirstRunCheck::cablesFor(SoundSystemLine::System system,
                                                   const QVector<DetectedCable>& all)
{
    if (system == SoundSystemLine::System::Windows) {
        return VirtualCableDetector::filterThirdParty(all);
    }
    QVector<DetectedCable> out;
    for (const auto& c : all) {
        if (c.product == VirtualCableProduct::NereusSdrVax) {
            out.push_back(c);
        }
    }
    return out;
}

void VaxFirstRunCheck::run()
{
    if (m_started) {
        return;
    }
    m_started = true;
    if (m_fromCatalog && m_catalog != nullptr && !m_catalog->hasListed()) {
        // R-AUD-03: the first list is still on its way (the catalog
        // waited for it at its start and went on without it). What it
        // lists now is nothing, whatever is installed, so nothing is read
        // or saved until the list is in.
        m_waiting = true;
        connect(m_catalog, &IAudioDeviceCatalog::firstListed, this,
                &VaxFirstRunCheck::check, Qt::SingleShotConnection);
        return;
    }
    check();
}

void VaxFirstRunCheck::check()
{
    m_waiting = false;
    if (m_fromCatalog && m_catalog == nullptr) {
        // The engine let its catalog go while its first list was awaited.
        // Nothing was read, so nothing is saved; the next launch checks.
        emit finished();
        return;
    }

    auto& s = AppSettings::instance();
    const bool firstRunDone =
        (s.value(QStringLiteral("audio/FirstRunComplete"),
                 QStringLiteral("False")).toString() == QStringLiteral("True"));

    // R-AUD-03 (native audio plan Task 18): from the engine's device
    // catalogue when it has one, else a PortAudio scan.
    const QVector<DetectedCable> all =
        m_fromCatalog ? VirtualCableDetector::detect(*m_catalog) : m_scan();
    const QVector<DetectedCable> detected = cablesFor(m_system, all);

    // Always refresh the stored fingerprint so a cable being removed +
    // later reinstalled doesn't permanently re-flag itself as "new".
    const QString newCsv = VirtualCableDetector::fingerprintCsv(detected);
    const QString lastCsv = s.value(QStringLiteral("audio/LastDetectedCables"),
                                    QString()).toString();
    // R-AUD-03: the names came from the catalogue or, without one, from the
    // older PortAudio scan; a fingerprint is compared only with one from
    // the same list, so an upgrade raises no false "new cable" notice.
    const QString lastSource =
        s.value(QStringLiteral("audio/LastDetectedCablesSource"), QString()).toString();
    const QString source = VirtualCableDetector::fingerprintSource(m_fromCatalog);
    s.setValue(QStringLiteral("audio/LastDetectedCables"), newCsv);
    s.setValue(QStringLiteral("audio/LastDetectedCablesSource"), source);
    s.save();

    if (!firstRunDone) {
        FirstRunScenario scenario = FirstRunScenario::LinuxNative;
        switch (m_system) {
        case SoundSystemLine::System::Windows:
            scenario = detected.isEmpty() ? FirstRunScenario::WindowsNoCables
                                          : FirstRunScenario::WindowsCablesFound;
            break;
        case SoundSystemLine::System::Mac:
            scenario = FirstRunScenario::MacNative;
            break;
        case SoundSystemLine::System::Linux:
            scenario = FirstRunScenario::LinuxNative;
            break;
        }
        emit dialogWanted(scenario, detected);
        emit finished();
        return;
    }

    // First-run already complete — only pop the dialog if NEW cables
    // have appeared since the last launch.
    const auto fresh =
        VirtualCableDetector::newCablesSince(detected, lastCsv, lastSource, source);
    // And only when the notice has one of them to offer: it binds a cable's
    // playback end to a VAX slot and nothing else. Without this, four newly
    // seen VAX devices on a Mac (recording devices, fed directly) opened a
    // notice that counted "0 new" cables.
    if (!VirtualCableDetector::bindableEnds(fresh).isEmpty()) {
        emit dialogWanted(FirstRunScenario::RescanNewCables, fresh);
    }
    emit finished();
}

} // namespace NereusSDR
