// =================================================================
// src/core/settings/SettingsScope.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 14.
//
// The ordered rule table, first match wins:
//   1. Explicit exceptions -- a key that would otherwise be caught by a
//      prefix rule below but needs the opposite answer. Only the two
//      TciLogWindow* entries qualify under that strict definition (both
//      would match the "Tci" prefix rule in step 2 without this step
//      running first). A key that matches no prefix at all belongs in
//      step 3 below, even if it reads like an "exception" to some
//      family's usual answer in prose.
//   2. Prefixes -- a whole family that shares one scope regardless of
//      what follows the prefix (checked with startsWith(), so ordering
//      between prefixes only matters if one is a leading substring of
//      another; none of the ones below are).
//   3. Whole-key rules -- individual flat keys with no shared prefix
//      family, including several that are departures from what their
//      surrounding family would otherwise suggest (the four FftPoolConfig
//      knobs among the mostly-cosmetic "Display*" keys; audio/DspRate and
//      audio/DspBlockSize among the mostly-local "audio/*" keys) -- see
//      the two paragraphs below for why those six live here and not in
//      step 1.
//   4. Default: OperatorLocal. See SettingsScope.h's top comment for why
//      the default is local, not Station.
//
// Two families deliberately do NOT get a blanket prefix rule even though
// they look like good prefix candidates, because the same textual prefix
// covers keys with genuinely different scopes:
//
//   "Display" -- most Display* keys are pure client-side rendering
//   (colours, line widths, hold timers for the peak/blob overlay) and are
//   correctly OperatorLocal by the bare default below. Exactly four are
//   not: DisplayFftSize, DisplayFftWindow, DisplayHzPerBinTarget and
//   DisplaySpectrumFps, which MainWindow::refreshFftPoolConfig
//   (MainWindow.cpp:1477-1500, this exact quartet named at :1512-1539's
//   "the four display AppSettings-sourced knobs" comment) reads to
//   configure the daemon's actual FFT production rate/size/window/target
//   bin width. Those four are whole-key rules below, Station -- NOT
//   "explicit exceptions" in this file's strict sense, since no prefix
//   rule claims "Display*" for step 1 to need to override. DisplaySpectrumFps
//   carries its own paragraph (see below) because it is not simply
//   "Station" in the same sense as the other three.
//
//   "audio/" -- almost every audio/* key is local sound hardware
//   selection (AudioEngine.cpp's ensureSpeakersOpen/ensureTxInputOpen via
//   AudioDeviceConfig::loadFromSettings("audio/Speakers"|"audio/TxInput"),
//   VAX cable bookkeeping, the v0.3.0 audio/FirstRunComplete migration
//   flag) and correctly falls through to the default below. audio/DspRate
//   and audio/DspBlockSize are the two departures: real WDSP engine
//   parameters (AudioEngine.cpp:1791-1808, read back by
//   AudioAdvancedPage.cpp:145-170), not device selection, so they are
//   Station whole-key rules below. See tests/tst_settings_scope.cpp's
//   kCoreExemptPrefixes for the fuller "audio/*" writeup, including that
//   src/core/daemon/DaemonApp.cpp:220 also writes audio/Speakers/DeviceName
//   directly (from nereusd.conf's audio_device) without that making it
//   Station either -- it is still local sound hardware selection, just on
//   whichever machine happens to be running nereusd standalone.
//
// ---- The DisplaySpectrumFps straddle -----------------------------------
//
// DisplaySpectrumFps is read TWICE for two different reasons on two sides
// of the R2 split, and a three-valued (well, two-valued) scope enum
// cannot encode "both, meaning different things at each end":
//   - MainWindow.cpp:1483, into FftPoolConfig::fps -- the rate at which
//     the daemon's FFT engines actually produce spectrum frames. This
//     belongs to the station.
//   - MainWindow.cpp:3485, into the CLIENT's own SpectrumWidget paint
//     timer (setDisplayFps) -- how often ONE client's own screen redraws
//     the trace it already has. This belongs to the operator's machine;
//     a remote client with a 165 Hz monitor and a local client on a
//     10-year-old laptop have no reason to share this number.
// This function classifies it Station, grouped with the other three
// FftPoolConfig knobs, because the daemon-side consumer is the one that
// actually changes what data exists (the production rate), while the
// client-side consumer only changes how often an already-produced frame
// is redrawn -- the less consequential of the two if the value is wrong.
// This is a known, accepted, UNRESOLVED straddle, not a fix: a future
// task that wants the client's own paint rate independently configurable
// needs its own key (e.g. a client-local "DisplayPaintFps"), not a second
// meaning for this one.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06  J.J. Boyd / KG4VCF  Remote daemon R2 Task 14:
//                                    classifySettingsKey and its
//                                    completeness gate. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-06  J.J. Boyd / KG4VCF  Fix round 1 (review): moved six
//                                    whole-key rules out of kExceptions
//                                    into kWholeKeys (they match no
//                                    prefix, so they were never really
//                                    "exceptions" by this file's own
//                                    definition); added a "radios/"
//                                    prefix rule (hand-seeded, same
//                                    reason "Slice" is); removed the
//                                    "%1/" prefix rule (a test-only
//                                    extraction artifact that had leaked
//                                    into production code, now handled
//                                    entirely inside the test); corrected
//                                    a false claim in stripPanSuffix's
//                                    comment. AI-assisted transformation
//                                    via Anthropic Claude Code.
// =================================================================

#include "core/settings/SettingsScope.h"

#include <QChar>
#include <QLatin1String>

namespace NereusSDR {

namespace {

// From SpectrumWidget.cpp:577-584's settingsKey(base, panIndex) helper
// (AetherSDR pattern): panIndex 0 returns base unchanged; panIndex N>0
// returns "base_N". Strip a trailing "_<digits>" run before matching, so
// a key means the same thing on every pan.
//
// A handful of OTHER index-suffixed key families exist in this tree,
// built via .arg() and therefore invisible to the completeness sweep's
// extraction (see tst_settings_scope.cpp's "Three limits" comment):
// ContainerData_%1, ContainerItems_%1 (ContainerManager.cpp, container
// id, a QUuid string), SpotBandFilter_%1, spotListBandPill_%1 (band name
// like "20m"), SpotSourceFilter_%1, spotListSourcePill_%1 (source label).
// None of these six are misclassified by this function whether or not
// stripping happens to fire on a given instantiation: none of them, base
// or suffixed, matches any rule below, so all six resolve OperatorLocal
// regardless -- correctly, since all six are per-widget/per-filter local
// UI state. This function's correctness does not depend on identifying
// every index-suffixed family in the tree, only on not mis-stripping one
// that WOULD otherwise match a rule; nothing in the current rule table
// shares a base name with any of these six.
QStringView stripPanSuffix(QStringView key)
{
    const qsizetype underscore = key.lastIndexOf(QLatin1Char('_'));
    if (underscore < 0 || underscore == key.size() - 1) {
        return key;
    }
    const QStringView suffix = key.sliced(underscore + 1);
    for (const QChar c : suffix) {
        if (!c.isDigit()) {
            return key;
        }
    }
    return key.first(underscore);
}

struct Rule {
    const char* text;
    SettingsScope scope;
};

// ---- 1. Explicit exceptions ---------------------------------------------
// Both entries below are the ONLY keys in this whole table that would be
// caught by a prefix rule in step 2 if this step didn't run first.
const Rule kExceptions[] = {
    // TCI's own log-viewer dialog (Tools -> ... -> TCI Server Log,
    // TciLogWindow.cpp): pure GUI chrome, escapes the "Tci" prefix rule
    // below on purpose.
    { "TciLogWindowGeometry", SettingsScope::OperatorLocal },
    { "TciLogWindowAutoScroll", SettingsScope::OperatorLocal },
};

// ---- 2. Prefixes ---------------------------------------------------------
const Rule kPrefixes[] = {
    // Every hardware/<mac-or-literal-segment>/* key, without exception.
    // AppSettings::hardwareValue()/setHardwareValue() (AppSettings.cpp:
    // 974, :981) always prepend exactly "hardware/%1/".arg(mac) before a
    // key reaches setValue()/value(), so every key that ever reaches
    // Task 15's change hook through that pair already carries this
    // prefix -- radioInfo/*, options/{stepAtt,autoAtt,preamp}/*,
    // p1AdcCntrl, peripherals/*, alex/{hpf,lpf,bpf1}/*, alex2/*, apollo
    // state, PureSignal autoCalEnabled, PA/mic profile storage, all of
    // it. A handful of call sites build the same "hardware/%1/..." shape
    // by hand via plain setValue()/value() instead of going through that
    // pair (AlexController.cpp/ApolloController.cpp's own
    // persistenceKey() -> "hardware/<mac>/alex/antenna" or
    // "hardware/<mac>/apollo"; SettingsHygiene.cpp; CalibrationController
    // .cpp; MicProfileManager.cpp; PaProfileManager.cpp; OcMatrix.cpp;
    // TxSliceArbiter.cpp) -- same prefix, same scope, no separate rule
    // needed. hardware/oc/pennyExtCtrl (OcOutputsHfTab.cpp:373) is the
    // brief's own canonical proof that the segment after "hardware/" is
    // just whatever text a call site put there, MAC or literal "oc" or
    // anything else -- this rule does not, and must not, try to validate
    // that it looks like a MAC.
    { "hardware/", SettingsScope::Station },

    // TCI server configuration (CatTciServerPage / TciServer /
    // TciProtocol / TciSensorManager): server bind address/port/enabled,
    // ExpertSDR3/SunSDR2Pro/CWLU compatibility flags, IQ/audio stream
    // shape, sensor poll intervals, rate limiting. All of it configures
    // the TCI WebSocket server, which in the R2 split lives in the
    // daemon (it needs live RxChannel/RadioModel access no remote GUI
    // has). The two GUI-only log-window keys are explicit exceptions
    // above, checked first.
    { "Tci", SettingsScope::Station },

    // Per-slice-per-band DSP/VFO state (AppSettings.h's documented
    // "Slice<N>/Band<key>/..." and "Slice<N>/..." families: AGC, filter,
    // NR stack, RIT/XIT, AF/RF gain, antennas, mode, step). Not
    // mechanically discovered by the completeness sweep below --
    // SliceModel.cpp builds every key through bandPrefix()/slicePrefix()
    // helpers (SliceModel.cpp:1740, :1759, :1766, :1768: both literally
    // start with QStringLiteral("Slice%1/...")) and then concatenates a
    // field name onto the result, so no call site passes a literal
    // starting with "Slice" directly to value()/setValue(). Hand-seeded
    // for the same reason the hardwareValue reconstruction below exists:
    // the REAL key Task 15 will see always has this prefix, even though
    // no single source line spells it out. This is the single most
    // important prefix in this table -- it is the live VFO/AGC/filter/NR
    // state that R2 exists to keep in sync.
    { "Slice", SettingsScope::Station },

    // Legacy flat "Vfo*" keys, one-shot migrated into the Slice<N>/...
    // namespace above by SliceModel::migrateLegacyKeys() on startup
    // (AppSettings.h's doc comment on that namespace). Same content,
    // same scope as their successor.
    { "Vfo", SettingsScope::Station },

    // Saved-radio list (name/IP/MAC/port/protocol/autoConnect/pinToMac/
    // lastSeen/lastConnected/discoveryProfile). OperatorLocal, not
    // Station: which radios are known/discoverable and which to
    // auto-connect to is a client-side connection-management concern,
    // not something nereusd needs. Confirmed by absence, not presence:
    // grepping src/core/daemon/DaemonApp.cpp and DaemonConfig.cpp for
    // "radios/"/savedRadio/lastConnected/discoveryProfile returns zero
    // hits -- the daemon picks its target radio from nereusd.conf, never
    // from this namespace. Hand-seeded like "Slice" above: every real
    // call site is inside AppSettings.cpp's own lastConnected()/
    // setLastConnected()/discoveryProfile()/setDiscoveryProfile() (and
    // the radioKeyPrefix()/savedRadio()/saveRadio() family), calling
    // value()/setValue()/remove() with no receiver prefix at all
    // (implicit self-calls), which is outside what the completeness
    // sweep's regex can see (see tst_settings_scope.cpp's "Three limits"
    // comment) -- so nothing forces this rule to exist for either
    // completeness assertion to pass; it is here because it is correct,
    // not because a test demanded it. Explicit rather than left to the
    // bare default so a future reader sees this was reviewed, not missed.
    { "radios/", SettingsScope::OperatorLocal },

    // PGXL / TGXL / RF2K-S: physically attached to the station (the
    // amplifier/tuner sits at the radio site, not on an operator's
    // remote laptop). Connection config, pairing state, identity/nickname,
    // antenna labels (describing what is physically plugged into THIS
    // station's ports), fault history, tune memory, interlock policy.
    { "PGXL_", SettingsScope::Station },
    { "TGXL_", SettingsScope::Station },
    { "RfKit_", SettingsScope::Station },

    // Spot-source client connection + display config (DX cluster/RBN
    // telnet, WSJT-X UDP, SpotCollector/DXLab UDP, POTA HTTPS, FreeDV
    // Reporter Socket.IO, PSK Reporter IPFIX). RadioModel.h/.cpp owns
    // every one of these six client instances directly (not a GUI dialog
    // -- SpotHubDialog/FreeDVReporterDialog only present what RadioModel
    // already collected), so in the R2 split they run in the daemon.
    // Covers both the connection half (host/port/poll-interval) and the
    // per-source display half (spot marker colour/lifetime): both are
    // read by src/models/RadioModel.cpp today, and nothing in this tree
    // currently splits "which server" from "what colour its dots are"
    // into two different scopes for the same source.
    { "DxCluster", SettingsScope::Station },
    { "Rbn", SettingsScope::Station },
    { "Pota", SettingsScope::Station },
    { "PskReporter", SettingsScope::Station },
    { "FreeDv", SettingsScope::Station }, // FreeDvReporter/* and FreeDvSpot*
    { "SpotCollector", SettingsScope::Station },
    { "Wsjtx", SettingsScope::Station },

    // Self-reporting identity used to seed the spot-source clients above
    // (POTA/PSKReporter/FreeDVReporter self-spots, distance/bearing
    // calculations against other stations). Grid square in particular
    // must be the STATION's grid for those calculations to be correct in
    // a genuinely remote session, not the operator's own location.
    { "User/", SettingsScope::Station },

    // TNF (tunable notch filter): an actual WDSP notch in the RX audio
    // chain, not a display annotation. NotchModel state
    // (NotchGlobalEnabled/NotchVisualEnabled/NotchAutoIncrease/
    // NotchCount/Notch<N>{Active,Center,Width}).
    { "Notch", SettingsScope::Station },

    // Setup -> DSP page (DspOptionsPage.cpp): WDSP buffer/filter size,
    // impulse-response cache. Engine configuration, not a display
    // preference.
    { "DspOptions", SettingsScope::Station },

    // Noise blanker (NB/NB2) and sub-band noise blanker (SNB) DEFAULT
    // tuning, applied when a slice/band has no per-band override yet.
    // WDSP parameters (cmaster.c defaults per AppSettings.h's own
    // per-slice-per-band doc comment).
    { "Nb", SettingsScope::Station }, // NbDefault*, Nb2DefaultMode
    { "Snb", SettingsScope::Station }, // SnbDefaultK1/K2/OutputBW

    // RADE peer-mode DSP: neural vocoder model path and the EOO
    // idle-clear timer, both configuring the daemon-side RadeChannel.
    { "Rade", SettingsScope::Station }, // Rade/ModelPath, RadeIdleClearMs
};

// ---- 3. Whole-key rules ---------------------------------------------------
const Rule kWholeKeys[] = {
    // The four FftPoolConfig knobs -- see this file's header comment for
    // the full "Display" writeup and the DisplaySpectrumFps straddle
    // paragraph. Not "explicit exceptions" (step 1): no "Display" prefix
    // rule exists for these to escape.
    { "DisplayFftSize", SettingsScope::Station },
    { "DisplayFftWindow", SettingsScope::Station },
    { "DisplayHzPerBinTarget", SettingsScope::Station },
    { "DisplaySpectrumFps", SettingsScope::Station },

    // The "audio/" split -- see this file's header comment. Both are read
    // by AudioAdvancedPage.cpp:145-170 (Setup -> Audio -> Advanced) and
    // written by AudioEngine.cpp:1791-1808 (setDspSampleRate/
    // setDspBlockSize). Not "explicit exceptions" either: no "audio/"
    // prefix rule exists for these to escape.
    { "audio/DspRate", SettingsScope::Station },
    { "audio/DspBlockSize", SettingsScope::Station },

    // Band-plan / TX-legality is a fact about where the RADIO is, not
    // where the operator's remote GUI happens to be sitting -- the
    // clearest possible case for Station in a genuinely remote session.
    { "BandPlanName", SettingsScope::Station },
    { "BandPlanRegion", SettingsScope::Station },
    { "Region", SettingsScope::Station }, // GeneralOptionsPage.cpp's FRS
                                            // region combo, same reasoning.

    // CW sidetone/RX-filter pitch offset -- RxChannel.cpp:1208's
    // "Freq = CWPitch + tuneOffset" is a DSP tuning computation.
    { "CWPitch", SettingsScope::Station },

    // Neural-net noise-reduction model file path (NR3/rnnoise). The
    // daemon is the process that actually loads and runs the model.
    { "Nr3ModelPath", SettingsScope::Station },

    // Identifies the STATION for self-spotting (POTA/PSKReporter/
    // FreeDVReporter), same family as the "User/" prefix above.
    { "StationCallsign", SettingsScope::Station },

    // Per-model factory S-meter calibration override
    // (HpsdrModel.h/RadioModel.cpp:3087-3101) -- a hardware calibration
    // constant for the connected board, same kind of fact as anything
    // under hardware/, just predating that convention.
    { "RX1_MeterCalOffsetDb", SettingsScope::Station },

    // One-shot migration marker for legacy global peripherals/* keys
    // moving to per-MAC hardware/<mac>/peripherals/* scope
    // (RadioModel.cpp:2956, :2992). Paired with data that is itself
    // Station (hardware/ prefix), so the flag travels with it: a fresh
    // GUI client connecting to an already-migrated daemon must not
    // independently re-run a migration against data it does not own.
    { "PeripheralsMigrationDone", SettingsScope::Station },

    // TX safety interlocks -- SwrProtectionController / TxInhibitMonitor
    // / the tune-power / wind-back-on-high-SWR gates
    // (src/core/safety/). Enforcement lives wherever TX actually
    // happens, which in R2 is the daemon.
    { "SwrProtectionEnabled", SettingsScope::Station },
    { "SwrProtectionLimit", SettingsScope::Station },
    { "SwrTuneProtectionEnabled", SettingsScope::Station },
    { "TunePowerSwrIgnore", SettingsScope::Station },
    { "TxInhibitMonitorEnabled", SettingsScope::Station },
    { "TxInhibitMonitorReversed", SettingsScope::Station },
    { "WindBackPowerSwr", SettingsScope::Station },

    // MeterPoller's S-meter sample cadence (MultimeterPage.cpp: Thetis
    // udDisplayMeterDelay default 100 ms) -- how often the daemon
    // actually samples the WDSP meter, not a rendering choice.
    { "MultimeterDelayMs", SettingsScope::Station },

    // ---- Reviewed and deliberately pinned OperatorLocal --------------
    // Each of these has a name that reads as a TX-safety or station-
    // behaviour flag, which is exactly the shape of key this table
    // exists to get right -- and each was checked, not guessed. Fix
    // round 1 (review) confirmed this narrower and stronger than
    // originally claimed: grepping the quoted literal for each of the
    // five across src/core and src/models (not just the same-named
    // identifier -- an earlier pass's cruder grep matched things like
    // HPSDRHW::HermesLiteRxOnly, BoardCapabilities::isRxOnlySku, and the
    // Alex.cs-ported RxOnlyAnt[] family, none of which are this setting)
    // finds zero AppSettings accessor call sites anywhere outside the
    // Setup page that defines each one (TransmitSetupPages.cpp /
    // GeneralOptionsPage.cpp): all five are write-only settings with no
    // runtime consumer at all, not settings with a consumer this table
    // just doesn't happen to route to the daemon. Pinned explicitly,
    // rather than left to the bare default below, so that a future
    // audit that DOES wire one of these into real enforcement trips over
    // an explicit line to change instead of a silent default.
    { "DisableHfPa", SettingsScope::OperatorLocal },
    { "ExtendedTxAllowed", SettingsScope::OperatorLocal },
    { "PreventTxOnDifferentBandToRx", SettingsScope::OperatorLocal },
    { "NetworkWatchdogEnabled", SettingsScope::OperatorLocal },
    { "RxOnly", SettingsScope::OperatorLocal },
};

} // namespace

SettingsScope classifySettingsKey(QStringView rawKey)
{
    const QStringView key = stripPanSuffix(rawKey);

    for (const Rule& r : kExceptions) {
        if (key == QLatin1String(r.text)) {
            return r.scope;
        }
    }
    for (const Rule& r : kPrefixes) {
        if (key.startsWith(QLatin1String(r.text))) {
            return r.scope;
        }
    }
    for (const Rule& r : kWholeKeys) {
        if (key == QLatin1String(r.text)) {
            return r.scope;
        }
    }
    return SettingsScope::OperatorLocal;
}

} // namespace NereusSDR
