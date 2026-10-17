#pragma once

// =================================================================
// src/gui/VaxFirstRunCheck.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original; no upstream logic.
//
// The launch check behind the VAX first-run dialog and the "new cables"
// notice. It reads this computer's virtual cables once, saves their
// fingerprint (audio/LastDetectedCables and its source), and says which
// dialog is to open, if any:
//
//   - audio/FirstRunComplete not set: the first-run dialog for this
//     system, with the cables found.
//   - set: the "new cables" notice, only when a cable has appeared since
//     the saved fingerprint AND the notice has an end of it to offer to a
//     VAX slot (VirtualCableDetector::bindableEnds). NereusSDR's own VAX
//     devices on macOS and Linux need no binding, so they never open it.
//
// The check reads the device list, so it runs when that list is in. The
// engine's catalog waits a few seconds for its first list and goes on
// without it when the system is slow to list; a check that ran then would
// save an empty fingerprint, and the next launch would call every cable
// new. With a catalog that has not listed yet, run() waits for
// IAudioDeviceCatalog::firstListed() and saves nothing until then.
//
// Before 2026-10-11 this was the first half of
// MainWindow::checkVaxFirstRun(), which ran at once.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-11: taken out of MainWindow::checkVaxFirstRun() (R-AUD-03)
//               with two fixes: it waits for the catalog's first list,
//               and it opens the notice only for cable ends a VAX slot can
//               be bound to. The system it decides for can be set by a
//               test. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include "core/audio/VirtualCableDetector.h"
#include "gui/VaxFirstRunDialog.h"
#include "gui/setup/SoundSystemLine.h"
#include "gui/NereusGuiExport.h"

#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>

namespace NereusSDR {

class IAudioDeviceCatalog;

class NEREUS_GUI_EXPORT VaxFirstRunCheck : public QObject {
    Q_OBJECT

public:
    using Scan = std::function<QVector<DetectedCable>()>;

    // catalog: the engine's device list, or null where there is none;
    // the older PortAudio scan is read then (VirtualCableDetector::scan).
    explicit VaxFirstRunCheck(IAudioDeviceCatalog* catalog, QObject* parent = nullptr);

    // Runs the check once: now when the device list is in, else when the
    // catalog's first list arrives. A second call does nothing.
    void run();

    // True from run() until the check, while the first list is awaited.
    bool isWaiting() const { return m_waiting; }

    // The cables this system's check looks at: third-party cables on
    // Windows (bring your own), NereusSDR's own VAX devices on macOS and
    // Linux (the native HAL plugin, the pipe sources).
    static QVector<DetectedCable> cablesFor(SoundSystemLine::System system,
                                            const QVector<DetectedCable>& all);

#ifdef NEREUS_BUILD_TESTS
    // The system decided for (this build's by default), and the scan read
    // without a catalog, so every system's answer is testable anywhere
    // and no test reads this computer's own devices.
    void setSystemForTest(SoundSystemLine::System system) { m_system = system; }
    void setScanForTest(Scan scan) { m_scan = std::move(scan); }
#endif

signals:
    // A dialog is to open: the first-run dialog for this system, or the
    // "new cables" notice (FirstRunScenario::RescanNewCables) with the
    // cables that are new.
    void dialogWanted(NereusSDR::FirstRunScenario scenario,
                      const QVector<NereusSDR::DetectedCable>& cables);
    // The check has run, whether or not it asked for a dialog.
    void finished();

private:
    void check();

    QPointer<IAudioDeviceCatalog> m_catalog;
    const bool m_fromCatalog;
    SoundSystemLine::System m_system = SoundSystemLine::thisSystem();
    Scan m_scan;
    bool m_started = false;
    bool m_waiting = false;
};

} // namespace NereusSDR
