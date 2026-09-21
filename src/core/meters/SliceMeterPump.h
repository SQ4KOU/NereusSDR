#pragma once

// =================================================================
// src/core/meters/SliceMeterPump.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/MeterManager.cs, original licence from Thetis source is included below
//
// Extracted from src/gui/meters/MeterPoller.cpp's pollSliceSMeters() /
// pollSMeter() (Remote-daemon R2 Task 12). MeterPoller lives in src/gui/,
// so a headless nereusd -- which links NereusCore only, no GUI object code
// (see tst_core_has_no_gui_includes) -- never ran it, and the per-slice
// S-meter readings SliceModel::signalStrengthDbm, signalPeakDbm and
// signalAverageDbm had no producer on that build. This class is the same
// polling logic, moved to
// src/core/ so both the GUI and the headless daemon produce it, exactly
// the way TciServer.cpp's own rx_sensors timer (src/core/TciServer.cpp)
// already proves core-side WDSP meter polling needs no GUI at all.
//
// Design:
//   - Owned directly by RadioModel (see RadioModel's constructor), which
//     already holds everything this class reads: WdspEngine (per-slice
//     RxChannel lookup, MaxBin detector), RadioStatus (the MOX gate),
//     slices() (the poll set), and rxMeterOffsetDb() (the RXOffset cal
//     term). No separate wiring step the way MeterPoller's setSMeter /
//     setWdspEngine / setRxOffsetSource need, because RadioModel is not
//     assembled piecemeal the way MainWindow's GUI construction is.
//   - Constructed and start()ed ONLY when RadioModel::role() ==
//     Role::Local. RadioModel constructs WdspEngine unconditionally, so an
//     unguarded pump would run this 10 Hz timer against a channel-less
//     engine on a Role::Remote model and clobber every mirrored needle
//     with the -140.0 fallback the instant a future task wires a real
//     inbound delta into any S-meter reading. On Role::Remote, all three
//     readings are written exclusively through SliceModel::applyMirroredValue()
//     -- the mirror's own inbound-apply path.
//   - The ONE thing RadioModel cannot supply is which WDSP meter type to
//     read: that is a property of the analog SMeterWidget's rxMode()
//     selector (SMeter/SMeterPeak -> SignalPeak, SignalAverage ->
//     SignalAvg, MaxBin -> MaxBin), and SMeterWidget is a src/gui/ class
//     this file may never include. setSourceSelector() takes a
//     std::function the caller (MainWindow, for the GUI build) supplies
//     instead -- the same std::function-callback shape MeterPoller::
//     setRxOffsetSource already uses to cross this exact GUI/core seam.
//     Left unset (every headless-daemon construction, and every test that
//     does not call it), poll() defaults to SignalAverage, matching what
//     the pre-Task-12 pollSliceSMeters() always did.
//
// Without the selector wired, every slice's flag bar and the analog
// SMeterWidget could disagree by the 3-15 dB MeterPoller.cpp's own
// pollSMeter() comment records (SignalPeak vs SignalAverage on a typical
// SSB signal). Task 12's MainWindow wiring supplies the selector
// specifically to keep that from happening once more.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06 -- Remote-daemon R2 Task 12: extracted from
//                 src/gui/meters/MeterPoller.cpp's pollSliceSMeters() /
//                 pollSMeter() into a core-side, RadioModel-owned QTimer.
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
// =================================================================

/*  MeterManager.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2020-2026 Richard Samphire MW0LGE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

mw0lge@grange-lane.co.uk
*/
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

namespace NereusSDR {

class RadioModel;

class SliceMeterPump : public QObject {
    Q_OBJECT

public:
    // Mirrors SMeterWidget::RxMode (src/gui/SMeterWidget.h) without
    // depending on it -- src/core/ may not include a GUI header (enforced
    // by tst_core_has_no_gui_includes). SMeter and SMeterPeak both map to
    // SignalPeak, the same collapse MeterPoller::pollSMeter()'s switch
    // performs; there is nothing separate for this pump to do with them.
    enum class MeterSource { SignalPeak, SignalAverage, MaxBin };

    explicit SliceMeterPump(RadioModel* radioModel, QObject* parent = nullptr);
    ~SliceMeterPump() override;

    // Queried once per poll() tick; never called synchronously from
    // within setSourceSelector() itself. Leaving this unset (the default)
    // makes poll() behave exactly like the pre-Task-12 pollSliceSMeters():
    // always SignalAverage. MainWindow wires a real selector once the
    // analog SMeterWidget exists; a headless daemon build never calls
    // this at all.
    void setSourceSelector(std::function<MeterSource()> selector);

    // Clamped to [10, 2000] ms, matching MeterPoller::setIntervalMs's
    // clamp (MeterPoller.cpp) against the same MultimeterPage spinbox
    // range, so a stale/corrupt persisted MultimeterDelayMs of 0 can never
    // stall this timer either.
    void setIntervalMs(int ms);
    int  intervalMs() const;

    void start();
    void stop();

public slots:
    // Public (unlike MeterPoller::poll(), a private slot) so a test can
    // tick this deterministically instead of waiting on the real QTimer.
    // Also the internal QTimer::timeout target.
    void poll();

private:
    // Not owned -- RadioModel owns this pump (Qt-parented), not the other
    // way around. QPointer for the same defensive reason TciServer.h holds
    // its own RadioModel pointer this way, even though RadioModel's own
    // child-destruction order means this pump is destroyed first in
    // practice.
    QPointer<RadioModel> m_radioModel;

    QTimer m_timer;

    std::function<MeterSource()> m_sourceSelector;
};

} // namespace NereusSDR
