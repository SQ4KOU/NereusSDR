// =================================================================
// src/core/audio/IAudioDeviceCatalog.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original interface for the live device
// catalogue (R-AUD-03); no upstream logic.
//
// Every device list in the program reads one catalogue.  The getters are
// main-thread calls that return the last snapshot; the signals arrive on
// the main thread.  devicesChanged() follows a re-list; defaultChanged()
// follows a change of the system default without waiting for a re-list.
//
// Modification history (NereusSDR):
//   2026-10-09: native audio plan Task 3 (R-AUD-03). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan Task 7 (R-AUD-06): olderDriversRescanned().
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: Windows test fix (R-AUD-03): exported from the Core DLL,
//               so a signal of it is found from outside the DLL on
//               Windows. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-11: hasListed() and firstListed() (R-AUD-03): a reader that
//               saves or compares what it reads can wait for a first list
//               that is slow to arrive. J.J. Boyd (KG4VCF), AI-assisted
//               via Anthropic Claude Code.
//   2026-10-12: wantOlderDrivers(). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-17: firstListWaitLeftMs(): what is left of a start's wait for
//               its first list, so a reader never waits for it twice.
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/NereusCoreExport.h"
#include "core/audio/AudioDeviceTypes.h"

#include <QList>
#include <QObject>

#include <optional>

namespace NereusSDR {

class NEREUS_CORE_EXPORT IAudioDeviceCatalog : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IAudioDeviceCatalog() override = default;

    virtual QList<AudioBackendId> backends() const = 0;                // R-AUD-01 order
    virtual bool backendRunning(AudioBackendId id) const = 0;
    virtual QList<AudioDeviceInfo> devices(AudioBackendId id, AudioDeviceDirection direction) const = 0;
    virtual std::optional<AudioDeviceInfo> defaultDevice(AudioBackendId id,
                                                         AudioDeviceDirection direction) const = 0;
    virtual void rescanOlderDrivers() = 0;
    // The older drivers' devices are not listed until something asks for
    // them: listing them starts PortAudio, which is slow to start.  This
    // asks: the list follows through devicesChanged().  Reading the older
    // drivers' devices() or defaultDevice() asks too.  Nothing on a
    // catalog whose lists are there from the start.
    virtual void wantOlderDrivers() {}

    // R-AUD-03: false until the first device list is in.  The catalog
    // waits a few seconds for it at its start and goes on without it when
    // the system is slow to list, so until then the getters answer as if
    // nothing were installed.  A reader that only shows the list follows
    // devicesChanged(); one that saves what it reads, or compares it with
    // what it saved, asks this first and waits for firstListed().  A
    // catalog whose lists are there from the start (every one but the
    // engine's) has always listed.
    virtual bool hasListed() const { return true; }

    // R-AUD-03: how much longer, in ms, the first list is worth waiting
    // for: what is left of the few seconds a start gives it, when the start
    // returned before it (the window lists in the background).  0 once it
    // is in, once that time has gone, after a start that waited for it
    // itself, and while stopped.  A reader that waits for firstListed()
    // waits no longer than this, so the list is never waited for twice.
    virtual int firstListWaitLeftMs() const { return 0; }

signals:
    void devicesChanged();
    void defaultChanged(NereusSDR::AudioDeviceDirection direction);
    // R-AUD-03: hasListed() has become true.  Once for each start, after
    // the list is in place (and after devicesChanged, when it differs).
    void firstListed();
    // R-AUD-06: a rescanOlderDrivers() has finished; the list it made has
    // been adopted (devicesChanged, when it differs, came first).
    void olderDriversRescanned();
};

} // namespace NereusSDR
