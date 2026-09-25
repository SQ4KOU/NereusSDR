// no-port-check: NereusSDR-original. The one list of features not built yet.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/UnbuiltFeatures.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port.
//
// R-R3-49: every control a user can see does what its label says. A control
// whose feature is not built yet is hidden, in local and remote windows,
// through this one list. Each entry names one unbuilt feature from the
// operator's decisions of 2026-09-23 (the "Hide" rows of the R3 unfinished
// controls plan). Every menu item, Setup page or group, applet control,
// status bar item and container control that fronts such a feature asks
// isBuilt() before it is shown.
//
// Building a feature later is one change here (drop its entry, or return
// true for it) plus the feature itself: its surfaces appear again, because
// a hidden control keeps its code and its saved settings. Nothing here
// removes a control or touches a saved value.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Created (R-R3-49, R-R3-21). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  Fix wave: one entry per feature for the
//                                    container buttons and the review's
//                                    unfinished controls. AI-assisted via
//                                    Anthropic Claude Code.
// =================================================================

#pragma once

#include <QList>
#include <QString>

class QAction;
class QLayout;
class QWidget;

namespace NereusSDR {

// One entry per unbuilt feature. The comment on each names its surfaces.
enum class UnbuiltFeature {
    DisplayMode,      // View > Display Mode; the eleven container display-mode buttons
    UiScale,          // View > UI Scale; Setup > General > UI Scale & Theme
    MinimalMode,      // View > Minimal Mode; Setup > Appearance > Collapsible Display
    Keyboard,         // View > Keyboard Shortcuts; Setup > Keyboard > Shortcuts
    Equalizer,        // DSP > Equalizer (built after R4)
    Transverters,     // Radio > Transverters; Band > VHF; Hardware > XVTR; OC Outputs VHF tab;
                      // the container band and antenna XVTR buttons
    BandStack,        // Band > Band Stacking; the band stack dots on the status bar
    Cwx,              // Tools > CWX; Phone/CW applet CW page; CW keyer settings; CWX on the status bar
    Memories,         // Tools > Memory Manager; the Spot Hub Memories option
    Cat,              // Tools > CAT Control; Setup > Serial Ports, TCP/IP CAT; CAT on the status bar
    Midi,             // Tools > MIDI Mapping; Setup > MIDI Control
    Help,             // Help > Getting Started, NereusSDR Help, Understanding Data Modes
    Acc,              // Phone/CW applet +ACC and the microphone source ACC item
    PhoneMon,         // Phone/CW applet MON and its level
    FmPage,           // Phone/CW applet FM page
    RfkitTune,        // RF-Kit applet TUNE and BYPASS
    MacroButtons,     // The container macro buttons (built after R4)
    DisplayAveraging, // Display averaging from a container: the container AVG button
    TwoReceiverLayout,// The two-receiver layout (RX1 and RX2, never built for slices A to D):
                      // the container RX2, SUB RX and Pan Swap buttons
    VariableFilters,  // The variable filter slots: the container filter Var1 and Var2 buttons
    AntennaRxTxSplit, // The antenna box's receive/transmit split: the container antenna Rx/Tx button
    Voice,            // Voice Rec/Play container control; VFO flag record and play; DVK on the status bar;
                      // the container Play and Rec buttons
    Fdx,              // FDX on the status bar; the container DUP button
    Navigation,       // Setup > General > Navigation
    Sam,              // Setup > DSP > AM/SAM synchronous AM options (built after R4)
    Skins,            // Setup > Appearance > Skins
    TxProfilesLeaf,   // Setup > Transmit > TX Profiles (a page that only says it moved)
    BandwidthMonitor, // Setup > Hardware > Bandwidth Monitor
    Hl2SecondI2cBus,  // Setup > Hardware > HL2 Options second I2C bus
    ConnectionHistory,// Setup > Diagnostics > Connection Quality 60 s history (built after R4)
    Logging,          // Setup > Diagnostics > Logging: log level, open and clear, categories (built after R4)
    SignalGenerator,  // Setup > Diagnostics > Signal Generator and Hardware Tests
    LocalNetworkStats,// Network Diagnostics with a local radio: Jitter, Packet loss, Packet gap (built after R4)
    DspRate,          // Setup > Audio > Advanced DSP sample rate and block size
    IqToVax,          // Setup > Audio > Advanced Send IQ to VAX, TX Monitor to VAX
    MuteVaxDuringTx,  // Setup > Audio > Advanced Mute VAX during TX on other slice (built after R4)
    AntennaConflict,  // Setup > Hardware > Antenna Control conflict policy
    OcExtras,         // Setup > Hardware > OC Outputs hot switching, USB BCD, external PA;
                      // the container xPA button
    MultimeterHolds,  // Setup > Display > Multimeter peak hold, text hold, digital delay, history (built after R4)
    WsjtxFilters,     // Spot Hub WSJT-X filters (three) (built after R4)
    RbnRateLimit,     // Spot Hub RBN rate limit (built after R4)
    FreeDvToPsk,      // Spot Hub report FreeDV decodes to PSK Reporter (built after R4)
    TciExtras,        // TCI CW to CWU, TX channel, sensor intervals, RX2 VFO options,
                      // stream channels (built after R4)
    SmallFilter,      // Setup > Appearance small filter display on the VFO flag
    ApfParams,        // Setup > DSP > CW peak filter bandwidth and gain (built after R4)
    AmSquelchTail,    // Setup > DSP > AM/SAM maximum squelch tail (built after R4)
    FmDeviation,      // Setup > DSP > FM deviation and de-emphasis (built after R4)
    ExportRadio,      // Setup > Diagnostics > Export / Import: Export Connected Radio
    FmTransmit,       // Setup > DSP > FM transmit group; the VFO flag's FM repeater minus,
                      // simplex and plus buttons, the Offset box and Rev (table rows fm-tx,
                      // fm-repeater and fm-flag)
    DdcRouting,       // Setup > Hardware > DDC Routing (multi-panadapter receiver routing)
    HpfBroadcastReject,   // The filter policy dialog's "HPF (broadcast band reject) enabled"
    FrequencyCalibration, // Setup > Hardware > Calibration: the frequency calibration Start button
    FmTones,          // CTCSS tone encode and tone squelch: the VFO flag's FM tone mode and tone
                      // choices (plan row fm-flag)
};

namespace UnbuiltFeatures {

struct Entry {
    UnbuiltFeature feature;
    QString key;          // the decisions table's item name, e.g. "band-stack"
    QString description;  // what the feature is, for logs and tests
};

// Every entry of the list, in the decisions table's order.
const QList<Entry>& all();

// The decisions table's item name for a feature.
QString key(UnbuiltFeature feature);

// The single query every surface calls. False for every entry until its
// feature is built.
bool isBuilt(UnbuiltFeature feature);

// Hide `widget` while `feature` is not built. A widget in a QFormLayout
// hides its whole form row (label and field). Nothing happens once the
// feature is built, so a surface keeps whatever other gate it has.
void hideUnlessBuilt(QWidget* widget, UnbuiltFeature feature);

// Hide the labelled row `control` sits in: its QFormLayout row, its
// QGridLayout row, or the horizontal row layout holding it and its label.
// A control directly in a vertical layout hides alone. The row is found
// from the control's parent widget, or from `searchFrom` for a layout not
// yet installed on a widget.
void hideRowUnlessBuilt(QWidget* control, UnbuiltFeature feature,
                        QLayout* searchFrom = nullptr);

// Hide every widget inside `layout` (a row built as its own layout).
void hideLayoutUnlessBuilt(QLayout* layout, UnbuiltFeature feature);

// Hide a menu item (for a submenu, pass menu->menuAction()).
void hideUnlessBuilt(QAction* action, UnbuiltFeature feature);

// Tests only: mark a feature built (or not) to prove its surfaces appear.
// The surfaces read the list when they are built, so build them after.
void setBuiltForTest(UnbuiltFeature feature, bool built);
void resetForTest();

} // namespace UnbuiltFeatures
} // namespace NereusSDR
