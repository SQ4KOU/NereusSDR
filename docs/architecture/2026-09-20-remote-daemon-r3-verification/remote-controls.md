# R3 remote control acceptance matrix

Requirements: R-R3-16/17/21/22/24/25. Execution uses
`yonder-cost-aware-execution`. The first tables record the bounded
connection, capability and telemetry checkpoint. The complete owner-by-owner
inventory of every visible menu item, applet control group and Setup leaf is
the "Visible control inventory by owner" section below (September 23); it is
not an assertion that all station accessories work, and its live checks are
hardware pending.

## Changed controls and ownership

| Surface | Owner and production path | Automated evidence | Live acceptance |
| --- | --- | --- | --- |
| Radio menu Connect / Disconnect | GUI -> configured StationClient through RemoteConnectionController | Real WebSocket connect, intentional disconnect and reconnect; duplicate connect does not create another session | Installed; intentional disconnect/cancel observed; successful reconnect pending |
| Connections after a disconnect (R-R3-16, R-R3-38) | Every operator Disconnect (Radio > Disconnect, the Connections window, the Core panel) goes through `RemoteConnectionController::disconnectFromStation`, which emits `operatorDisconnected()`. MainWindow then opens Connections once with the picker, or shows the Core panel (never dials) in a `--station` window. The automatic open on a Disconnected state is local-only: link loss retries and shows "Retrying Core (attempt N)", an offline radio at the Core shows "Radio offline", and a disconnect the app starts itself (preemption, a Core refusal) opens nothing | `tst_remote_window_harness` (`operatorDisconnectOpensConnectionsOnce`, `linkLossOpensNothingAndRetries`, `radioOfflineOpensNothing`), `tst_gui_connection_controller` (`connectionsDisconnectReopensConnectionsOnceWithoutDialling`), `tst_gui_session_coordinator` (`pickerOpensConnectionsOnlyForOperatorDisconnect`) | Pending installed smoke |
| Title connection segment, station block, disconnected pan | MainWindow::connectionRequestedByOperator -> same controller; automatic local panel callbacks remain inert remotely, and the automatic open on a Disconnected state runs for local models only | Actual title mouse activation; controller tests. MainWindow routing inspected, full window interactions still need smoke | Title segment opens details; all three paths and reconnect still pending |
| Core connection details | Persistent endpoint, session state, radio state, failure and retry/cancel; no credentials displayed | Authenticated Core with offline radio; failed dial/cancel beyond retry deadline; credential stripping | Endpoint, separated state and stopped presentation observed; recovery pending |
| Saved layout / snapshot | Core owns slices; GUI restoration only hydrates. Explicit post-snapshot layout/add remains a command | Authenticated attach/reconnect preserves one station slice; production population helper emits no implicit add; explicit action waits for readiness | Fresh attach displayed one slice; reconnect pending |
| TX applet / PureSignal / TX filter match | Negotiated txPermitted + handshake; Core retains admission checks | Widget activation, no mutation when gating, preservation of independent disabled state; remote refusal | Tune/MOX/VOX disabled appearance observed; remaining controls pending; no RF test |
| High-resolution FIR graph | Requires absent local RxChannel; visibly unavailable remotely | Remote/local checkbox interaction tests | Pending appearance; ordinary mirrored RX DSP is not classified as broken |
| Tuner telemetry | Core TunerModel -> outbound mirror -> client-only assign/notify adapter | All 13 fields, false/zero updates and absence of accessory commands | Core connected after restart to .234:9010 and received actual tuner info; live configuration/identity admission remains pending |
| Tuner TUNE / operate / antenna / relay / recall | Receive-only remote capability blocks commands; Core policy additionally guards autotune callbacks | Authenticated band change and telemetry replay issue no tune; MOX/TUNE admission blocked before radio connection and after session teardown | No RF or tuner actuation permitted in this checkpoint |
| Tuner cached values after Core loss | Retained values explicitly marked stale; unsupported remote accessory reconnect disabled | Applet session presentation/command tests | Pending disconnect smoke |
| Tools > Test antenna switch toast | Owner GUI-local test surface; disabled while connected to a Core with the standard remote transmit reason, enabled locally | `tst_remote_gui_gating`; cannot reach `emitAntennaAutoSwitched` while disabled | Commit b9e8811c; installed acceptance pending |
| Tools > Test TX-bound re-route dialog | Owner GUI-local test surface; disabled while connected to a Core with the standard remote transmit reason, enabled locally | `tst_remote_gui_gating`; cannot reach `requestTxBoundReRoute` while disabled | Commit b9e8811c; installed acceptance pending |

## Visible control inventory by owner (R-R3-21), September 23

This is the full inventory of what a remote receive session shows: every
menu item, every applet control group, the VFO flag, the overlay and chrome,
and every Setup leaf. It was read from source at `afa926a8` (MainWindow
`buildMenuBar()` `MainWindow.cpp:6568-7712`, `applyRemoteRoleGating()`
`:10173-10269`, each applet, and each Setup factory in `SetupDialog.cpp`),
and the gating it found is fixed in the same change. Line numbers are at
`afa926a8`.

**Owner key.**
- **GUI-local**: acts on this window only (layout, display, local playback,
  preferences). Correct in a remote session.
- **Station-backed**: reaches the Core through a mirrored property
  (`MirrorPolicy.cpp`: 142 SliceModel, 15 TransmitModel, 21 TunerModel, 19
  RadioModel, 4 PanadapterModel entries), a session verb
  (`SessionCommandDispatcher.cpp:297-317`: addSlice, removeSlice,
  addSliceOnPan, setActiveSliceById, requestSliceSampleRate,
  requestStreamCtunPinned, requestStreamCentre, configureTgxl,
  disconnectTgxl, setFourO3AEnabled, nnr.*, dspAssets.*, ps3.*), or a
  Station-scoped settings key through `SettingsProxy` (`SettingsScope.cpp`).
- **Transmit**: follows the negotiated `txPermitted` through
  `MainWindow::transmitControlsPermitted()` (`:10166`) with the reason
  "Remote transmit controls are not available from this Core yet."; the
  Core still refuses transmit writes under its receive-only policy
  (`StationServer.cpp:1021-1037`).
- **Unavailable**: acts on this computer's own radio connection, DSP,
  amplifier socket or VAX buses and moves nothing on a Core. Disabled with a
  plain reason, or not shown at all (noted).
- **Placeholder**: an existing disabled "NYI" item; unchanged.

**Generic mirrored DSP setters were traced, not assumed broken.** Every
receive DSP control on the flag, the RX applet, the DSP menu and Setup
AGC/ALC, NR/ANF (NR1-NR4, DFNR, MNR, NNR) and NB/SNB writes a SliceModel
setter. On a remote model those properties are Bidirectional in
`MirrorPolicy.cpp` and forwarded by `MirrorForwarder`; local RadioModel's
`wireSliceSignals()` returns early without a connection
(`RadioModel.cpp:11337`), so no local DSP is touched. Automated evidence is
the mirror suites (`tst_mirror_forwarder`, `tst_mirror_inbound`,
`tst_slice_mirror_identity`) and the real-window RIT case in
`tst_remote_tx_presentation`
(`authenticatedRemoteControlsCannotWriteXitButRitStillWorks`). The audible
effect of each setting on the Core is **hardware pending (S1)**, the
operator receive session. The three exceptions found are listed under
"Findings not gated here" below.

### Menu bar

| Menu > item | Owner | Handler / property | Acceptance |
| --- | --- | --- | --- |
| File > Settings..., Tools > TCI Server... | GUI-local shell; pages per the Setup table | `createSetupDialog()` opens in every state, connected or not (no refusal toast); it pushes `SetupDialog::setStationSettingsAvailable` (the Core's settings are available only while the session is ready and holds the Core's settings snapshot), and `applyRemoteRoleGating` pushes it again on every link change. While unavailable: Core pages disabled with "Connect to the Core to change these." (or, while connected to a Core that has not sent its settings, "The Core has not sent its settings."); a Core page opened before the Core's settings really arrived (a snapshot with content, or the Core's seed marker) is a stand-in, not built; Mixed pages disable only their Core controls; this computer's pages usable. Realized Core and Mixed pages are rebuilt from each new snapshot once available, except a page with its own dialog open (rebuilt once the dialog is gone), a page the local-DSP gate keeps disabled (not rebuilt) and a rebuild that yields nothing (the page is kept) | `tst_remote_gui_gating` (`disconnectedRemoteSetupPerPageTable`, `reconnectRebuildsSetupPagesFromTheCoresValues`, `setupPageRebuildIsGuardedAgainstReentry`, `coreSetupPagesWaitForTheCoresSettingsNotAnySnapshot`, `setupPageIsNotRebuiltUnderItsOwnOpenDialog`, `setupPageRebuildThatYieldsNothingKeepsThePage`, `pagesTheLocalDspGateDisablesAreNotRebuilt`, `localSetupIgnoresStationSettingsAvailability`, `setupGateFollowsTheInstalledProxyThroughItsStates`), `tst_remote_window_harness` (`disconnectedWindowSetupKeepsThisComputersSettings`, `setupOpenedWhileDisconnectedRecordsNoEdit`, `freshWindowFirstConnectRaisesNoOfflineEditWarning`) |
| File > Profiles > (4 items); Radio > Antenna Setup, Transverters; View > Display Mode, UI Scale, Minimal Mode, Keyboard Shortcuts; DSP > Equalizer, Diversity...; Band > VHF, GEN, Band Stacking; Tools > CWX, Memory Manager, CAT Control, VAX Audio, MIDI Mapping, Macro Buttons; Help > Getting Started, Help, Data Modes, What's New | Placeholder | disabled "NYI" actions | n/a |
| File > Quit | GUI-local | `QApplication::quit` | n/a |
| Radio > Connect, Disconnect | Station-backed (session) | `connectToStation()` / `disconnectFromStation()` through RemoteConnectionController; enable rules `:10236-10245` | `tst_remote_connection_controls`, `tst_gui_connection_controller`; hardware pending (S1) |
| Radio > Manage Radios / Connections | GUI-local (picker) | `showConnectionPanel()`; disabled with the `--station` reason when not picker-managed `:10247` | `tst_station_startup_selection`, `tst_gui_session_coordinator` |
| Radio > Protocol Info | Unavailable | always disabled remotely with "the radio's protocol details live on the station" `:10257` | `tst_remote_gui_gating` (`mainWindowExposesTheRemoteGatingSlots`); hardware pending (S1) |
| View > Pan Layout... | Station-backed (layout verbs after hydrate) | `showPanLayoutDialog()` `:10886`, `applyPanLayout()` refuses before handshake `:10776` | `tst_pan_layout_dialog_gating`; hardware pending (S1) |
| View > Add slice on active pan | Station-backed | `RadioModel::addSliceOnPan` -> `requestAddSliceOnPan` verb | `tst_remote_slice_commands` |
| View > Float active pan | GUI-local | `PanadapterStack::floatPanadapter` | n/a |
| View > Band Plan (font sizes, plan list) | GUI-local display | `SpectrumWidget::setBandPlanFontSize`, `setActivePlan`; `BandPlanName` is Station-scoped | hardware pending (S1) |
| View > Dark Theme, Performance Overlay | GUI-local | log only / `SpectrumWidget::setShowPerfOverlay` | n/a |
| DSP > NR (Off..BNR), NB, ANF, SNB, APF, BIN, AGC | Station-backed | active `SliceModel` `setActiveNr`, `setNbMode`, `setAnfEnabled`, `setSnbEnabled`, `setApfEnabled`, `setBinauralEnabled`, `setAgcMode` (mirrored) | mirror suites; hardware pending (S1) |
| DSP > TNF, status bar TNF light | Station-backed setting, **not applied live** (finding F1) | `NotchModel::setGlobalEnabled` -> `NotchGlobalEnabled` (Station key) | hardware pending (S1); see F1 |
| DSP > PureSignal..., Tools > PureSignal... | Station-backed (PS3 facade) | `openPureSignalDialog()`; enabled only when the Core advertises PureSignal 3 `:10220` | `tst_ps3_session`, `tst_ps3_lifecycle` |
| Band > HF bands, WWV | Station-backed | active `SliceModel::setFrequency` | mirror suites; hardware pending (S1) |
| Mode > LSB..RADE-L | Station-backed | active `SliceModel::setDspMode` | `tst_mode_menu_rade`, mirror suites; hardware pending (S1) |
| Containers > New, Edit, Reset Default Layout | GUI-local | `ContainerManager`, `ContainerSettingsDialog` | n/a |
| Containers > Applets section; panel banner menu | GUI-local | `AppletVisibilityController::setVisible`; availability per applet below | n/a |
| Tools > Spot Hub..., FreeDV Reporter... | Station-backed spot clients (Station keys); tune writes the active slice | `openSpotHub()`, `openFreeDVReporter()`; remote auto-start gated to Local (`RadioModel.cpp:2615`) | hardware pending (S1) |
| Tools > TX Equalizer... | Transmit | `m_actTxEqualizer`, refused in handler `:7523` | `tst_remote_gui_gating` (`toolsMenuTestEntriesAreDisabledInARemoteSession`), `tst_remote_tx_presentation` |
| Tools > Diversity... | Station-backed | `DiversityDialog` -> `SliceModel::setDiversity*` (mirrored) | hardware pending (S1) |
| Tools > Network Diagnostics... | GUI-local (remote branch) | `openNetworkDiagnostics()` -> `RemoteDiagnosticsDialog` | `tst_remote_connection_controls`, `tst_remote_diagnostics` |
| Tools > Support Bundle... | GUI-local | `SupportBundle::createBundle`; `connection()` null-checked | n/a |
| Tools > Test antenna switch toast, Test TX-bound re-route dialog | Transmit | disabled with the transmit reason `:10205-10214` | `tst_remote_gui_gating` (`toolsMenuTestEntriesAreDisabledInARemoteSession`) |
| Help > Diagnose audio backend (Linux), About | GUI-local | local `AudioEngine` diagnosis (remote playback uses it) / `AboutDialog` | n/a |

### Applets, VFO flag, overlay and chrome

| Surface > control group | Owner | Handler / property | Acceptance |
| --- | --- | --- | --- |
| RX applet: slice tabs, lock, mode, step, filter passband and preset buttons, pan, SQL, AGC mode/AUTO/AGC-T, RIT | Station-backed | `SliceModel` setters (mirrored); slice tabs via `setActiveSliceById` verb | mirror suites, `tst_remote_slice_commands`; hardware pending (S1) |
| RX applet: RX and TX antenna popups | Station-backed | `SliceModel::setRxAntenna` / `setTxAntenna`; TX antenna deliberately not transmit-gated (TRX receive routing) | `tst_rxapplet_antenna_buttons`; hardware pending (S1) |
| RX applet: XIT on, zero, minus, plus | **Transmit (gated here)** | `SliceModel::setXitEnabled/Hz`; `RxApplet::setTransmitPermitted` from `applyRemoteRoleGating()` | `tst_remote_gui_gating` (`remoteRxAppletXitFollowsTheTransmitPermission`, real window in `toolsMenuTestEntriesAreDisabledInARemoteSession`) |
| RX applet: ATT/S-ATT spin, preamp combo, RX1 preamp | **Unavailable (gated here)** | `StepAttenuatorController`, never wired without a local connection (`MainWindow.cpp:11031`); `P2RadioConnection` cast | `tst_remote_gui_gating` (`remoteRxAppletAttenuatorRowIsUnavailable`) |
| RX applet: filter preset Shift-click (TX filter match) | **Transmit (gated here)** | `TransmitModel::setFilterLow/High`, skipped while transmit is not permitted | `tst_remote_gui_gating` (`remoteRxAppletShiftClickLeavesTheTxFilterAlone`); hardware pending (S1) |
| TX applet: RF/tune power, TUNE, MOX, VOX, MON, LEV, EQ, CFC, profile, TX BW, 2-Tone, PS-A, EQ/CFC right-click | Transmit | `TxApplet::setTransmitPermitted` `TxApplet.cpp:2039-2091` | `tst_remote_gui_gating`, `tst_remote_tx_widgets`, `tst_remote_tx_presentation` |
| Phone/CW applet: MIC, PROC, VAX source, DEXP | Transmit | `PhoneCwApplet::setTransmitPermitted` `:1207-1249` | `tst_remote_tx_widgets` |
| Phone/CW applet: compression, mic profile/source, MON, AM carrier, CW and FM pages | Placeholder | NYI overlays | n/a |
| RADE applet: profile combo | **Transmit (gated here)** | `MicProfileManager::setActiveProfile`, the same mic profile Audio > TX Profile gates; `RadeApplet::setTransmitPermitted`, starts denied on a remote model, pushed from `applyRemoteRoleGating()` | `tst_remote_gui_gating` (`remoteRadeAppletFollowsTheTransmitPermission`, real window in `toolsMenuTestEntriesAreDisabledInARemoteSession`), `tst_remote_window_harness` (`capabilityChangeRegatesWithoutReconnect`) |
| RADE applet: Reset vocoder | **Unavailable (gated here)** | local `WdspEngine::radeChannel()`; on a remote model always disabled and the window's own DSP is never looked up; while transmit is denied it gives the transmit reason, once transmit is permitted "The RADE vocoder runs on the station computer and cannot be reset from a remote window." | `tst_remote_gui_gating` (`remoteRadeAppletFollowsTheTransmitPermission`), `tst_remote_window_harness` (`capabilityChangeRegatesWithoutReconnect`); hardware pending (S1) |
| VAX applet: RX gain/mute x4, TX gain | **Unavailable (gated here)** | local `AudioEngine` VAX buses; remote audio plays through the speakers only (`AudioEngine::writeRemotePlayback`) | `tst_remote_gui_gating` (`remoteVaxSurfacesAreUnavailable`) |
| PureSignal applet | Station-backed (PS3 facade); hidden unless PS3 is advertised `:10227-10234` | `PureSignalSessionFacade::requestAction` | `tst_ps3_session` |
| AM Mod Monitor applet | GUI-local display of a transmit analyser; no remote feed | `RadioModel::setAmModFeedbackWanted`, `ModMon/*` keys | hardware pending (S1) |
| Power Genius applet: OPERATE, Disconnect/Reconnect | **Unavailable (gated here)** | this computer's `PgxlConnection` (`MainWindow.cpp:11113`, `:11210`) | `tst_remote_gui_gating` (`remoteAmplifierAppletControlsAreUnavailable`) |
| Power Genius applet: gauges, Open PGXL Advanced | GUI-local display / Setup 4O3A (remote placeholder tabs) | availability follows the mirrored `fourO3AEnabled` | `tst_remote_peripherals` |
| Tuner Genius applet | Transmit + Core-owned accessory | `TunerApplet::setTransmitPermitted`, `setStationConnected`; remote menu uses `requestDisconnectTgxl` | `tst_station_accessory_state`, `tst_remote_peripherals` |
| RF-Kit RF2K-S applet: OPERATE, antenna buttons, Disconnect/Reconnect | **Unavailable (gated here)** | shown when the Core has RF-Kit enabled: `rfKitEnabled` is mirrored Bidirectional and its inbound write emits `rfKitEnabledChanged(true)` even with no radio MAC; the controls drive this computer's own `Rf2ksConnection`, so they carry the Power Genius amplifier reason | `tst_remote_gui_gating` (`remoteRfKitAppletControlsAreUnavailable`); hardware pending (S1) |
| RF-Kit RF2K-S applet: gauges, tuner status, Open RF-Kit Advanced | GUI-local display / Setup RF-Kit (declared unavailable remotely) | availability follows the mirrored `rfKitEnabled` | hardware pending (S1) |
| TCI Server and TCI Clients applets | GUI-local server on this computer | local `TciServer` start/stop, gains, client close | hardware pending (S1); open question in F6 |
| S-meter header and right-click menu | GUI-local | `SMeter_*`, `PeakHold*` keys; needle fed by `MeterPoller::setRemoteRadioModel` | `tst_remote_meter_poller` |
| VFO flag: frequency, wheel, AF, AGC, pan, mute, BIN, SQL, AGC-T/AUTO, NB, NR bank and popups, ANF, SNB, APF, FM/DIG/RTTY containers, mode, filters, RIT, step, lock, close, sample rate, antenna picker | Station-backed | `SliceModel` setters (mirrored), `removeSlice` / `requestSliceSampleRate` verbs | mirror suites, `tst_remote_slice_commands`, `tst_nnr_controls`; hardware pending (S1) |
| VFO flag: XIT, TX badge, Make TX slice, BYPS, filter Shift-click | Transmit | `VfoWidget::setTransmitPermitted` `:3214-3254`, MainWindow `:1784-1792` | `tst_remote_tx_presentation` |
| VFO flag: VAX tab selector | **Unavailable (gated here)** | `SliceModel::setVaxChannel`; `vaxChannel` is not a mirrored property | `tst_remote_gui_gating` (`remoteVaxSurfacesAreUnavailable`) |
| VFO flag: record/play | Placeholder | signals with no consumer | n/a |
| Overlay: +RX, BAND, RX/TX antenna combos | Station-backed | `addSliceOnPan` verb, `onBandButtonClicked`, `SliceModel::setRx/TxAntenna` | `tst_spectrum_overlay_panel`, `tst_remote_slice_commands` |
| Overlay: +TNF | Station-backed setting, not applied live (F1) | `RadioModel::addNotchForSlice` -> `NotchModel` | see F1 |
| Overlay: display flyout (scheme, gains, fill, cursor, Clarity re-tune, More) | GUI-local display | `SpectrumWidget` setters, `ClarityController` | `tst_remote_spectrum_render`; hardware pending (S1) |
| Overlay: VAX channel combo | **Unavailable (gated here)** | as the flag's VAX selector | `tst_remote_gui_gating` (`remoteVaxSurfacesAreUnavailable`) |
| Overlay: ATT, IQ combo, RF gain, WNB, zoom buttons | Placeholder / unwired | disabled or no consumer | n/a |
| Title bar connection segment, audio pip, right-click | GUI-local, opens station session actions | `openNetworkDiagnostics`, `connectionRequestedByOperator`, `showSegmentContextMenu` remote branch | `tst_remote_connection_controls`; hardware pending (S1) |
| Master output: volume, mute, output device | GUI-local, drives remote playback | local `AudioEngine` through `RadioModel::localAudioDevices()`; `RemoteMediaController.cpp:534-541` follows mute and device; the picked device is saved to `audio/Speakers/DeviceName` before it is announced, so remote playback re-reads the new device (`MasterOutputWidget::selectOutputDevice`) | `tst_remote_audio_receiver`, `tst_remote_media_controller`, `tst_master_output_widget` (`pickedDeviceIsSavedBeforeItIsAnnounced`); hardware pending (S1) |
| Status bar: +PAN, panel toggle, station block, TCI indicator, RX dashboard, system tile, TGXL chip | GUI-local / station session | as above; station block -> `connectionRequestedByOperator` | `tst_station_block`, `tst_remote_receive_indicators` |
| Status bar: PSA indicator menu | GUI-local preference (no local PureSignal remotely) | `InvertRedBluePsa`, `HideFeedbackLevel` | n/a |
| Status bar: CWX, DVK, FDX | Placeholder | inert labels | n/a |

Applets not constructed (EQ, FM, Digital, Diversity, CWX, DVK, CAT) are not
visible and are not listed.

### Setup leaves

| Leaf | Owner | Handler / property | Acceptance |
| --- | --- | --- | --- |
| General > Startup & Preferences, UI Scale & Theme, Navigation | Placeholder | no writes | n/a |
| General > Options: Hardware Configuration, Options groups | GUI-local (scope Mixed), except `Region` (Station), whose combo is disabled with the Core reason while the Core's settings are unavailable; `ExtendedTxAllowed`, `RxOnly`, `PreventTxOnDifferentBandToRx`, `NetworkWatchdogEnabled` are OperatorLocal and have no remote effect | AppSettings; `GeneralOptionsPage::setStationSettingsAvailable` | `tst_remote_gui_gating` (`disconnectedRemoteSetupPerPageTable`); hardware pending (S1) |
| General > Options: Step Attenuator, Auto Attenuate | **Unavailable (gated here)** | unwired local `StepAttenuatorController` | `tst_remote_gui_gating` (`remoteGeneralOptionsDisablesOnlyTheAttenuatorGroups`) |
| Hardware > Hardware Config, DDC Routing | **Unavailable (gated here)** | `HardwarePage` never learns a MAC remotely (`currentRadioChanged` is local-connect only, `RadioModel.cpp:14541`), so `onTabSettingChanged` drops every edit; DDC override keys are per-MAC and unread | `tst_remote_gui_gating` (`remoteDeclaredUnavailableSetupLeavesSayWhy`) |
| PA > PA Gain, Watt Meter, PA Values | Unavailable (not shown) | category hidden: remote capabilities are the Unknown board's, `hasPaProfile` false | `tst_remote_gui_gating` (`remotePaCategoryIsNotShown`) |
| Audio > Devices | GUI-local (scope ThisComputer), usable connected or not | Speakers, Headphones and Microphone cards pick this computer's devices through `RadioModel::localAudioDevices()`, which the local-DSP audit does not count; a card saves `audio/{Speakers,Headphones,TxInput}/*` then hands the config to the engine, and remote playback re-reads `audio/Speakers` on `speakersConfigChanged`. The title-bar picker is a shortcut to the same setting. Headphones behaves as it does locally. Remote playback still refuses speaker formats other than 48 kHz stereo (receiver audio plan). Microphone status and Retry follow this computer's capture | `tst_remote_gui_gating` (`remoteDevicesPageWorksOnThisComputer`, `everyRemoteSetupPageIsEitherLocalDspFreeOrDisabled`), `tst_settings_scope` (card keys); speaker restart on a real device hardware pending (S1) |
| Audio > VAX, TCI, Advanced | Unavailable (local-DSP gate; reason added here) | VAX and Advanced reach `audioEngine()` themselves; TCI reached it only through the backend strip, which no longer counts, so it is declared unavailable with the same reason (`markRemoteUnavailable`). Advanced Reset in a remote window removes only this computer's `audio/*` keys (never `audio/DspRate`, `audio/DspBlockSize`) and re-creates no VAX output; local Reset unchanged | `tst_remote_gui_gating` (`remoteLocalDspSetupPagesShowAPlainReason`), `tst_audio_advanced_page` |
| Audio > TX Input | Mixed: this computer's PC microphone usable; mic source, Mic Gain and radio microphone hardware **Transmit (gated here)** | PC Mic backend, device, buffer, Test Mic and Retry through `RadioModel::localAudioDevices()`, saved to `audio/TxInput/*`; Test Mic opens this computer's microphone and meters it. `AudioTxInputPage::setTransmitPermitted` gates the Mic Source group, Mic Gain and the Hermes / Orion-MkII / Saturn radio mic groups with the transmit reason; while the Core's settings are unavailable the same controls carry "Connect to the Core to change these." instead (`setStationSettingsAvailable`, one combined gate). No longer a whole-page transmit leaf | `tst_remote_gui_gating` (`remoteTxInputKeepsThisComputersMicrophoneUsable`), `tst_audio_tx_input_pc_mic_group` (`testMic_opensThisComputersMicrophoneInARemoteWindow`) |
| Audio > TX Profile | Transmit | `MicProfileManager`, `TransmitModel` | `tst_remote_tx_presentation` |
| DSP > AGC/ALC (RX AGC) | Station-backed | `SliceModel` AGC setters | mirror suites; hardware pending (S1) |
| DSP > AGC/ALC (TX Leveler, TX ALC groups) | **Transmit (gated here)** | `TransmitModel::setTxLeveler*`, `setTxAlc*`, not mirrored; `AgcAlcSetupPage::setTransmitPermitted` (group enable, tooltip, accessible description), pushed by `SetupDialog` | `tst_remote_gui_gating` (`remoteAgcAlcTransmitGroupsFollowThePermission`) |
| DSP > NR/ANF | Station-backed (scope Core since the R3 remote window Setup plan, Task 2: every control writes the active receiver or chooses among the Core's models), including the NR3 model selector (F2) | `SliceModel` NR setters; NNR through the station NNR capability | `tst_nnr_controls`, mirror suites; hardware pending (S1) |
| DSP > NB/SNB | Station-backed | `SliceModel` NB/SNB setters | `tst_mirror_inbound` (peer NB cases); hardware pending (S1) |
| DSP > CW, AM/SAM, FM | Placeholder | all groups disabled | n/a |
| DSP > CFC | **Transmit (gated here)** | `TransmitModel` phase rotator, CFC, CESSB; opens the TX CFC editor | `tst_remote_gui_gating` (`remoteTransmitOnlySetupLeavesFollowThePermission`) |
| DSP > TNF | Station-backed setting, not applied live (F1) | `NotchModel`, `addNotchForSlice` | see F1 |
| DSP > Filter Presets | GUI-local (scope ThisComputer) | `FilterPresetStore` (`filters/...`, OperatorLocal); buttons then write `SliceModel::setFilter` | n/a |
| DSP > Options | Station-backed settings, applied on the Core (F2); high-resolution graph unavailable; the nine TX combos **Transmit (gated here)** | `DspOptions*` keys; the Core applies an accepted RX write or remove to every slice whose mode group reads that key, once per 50 ms coalesce window (`RadioModel::scheduleRemoteDspOptionsApply`, `kDspOptionsApplyCoalesceMs`), with no mode change; the window's own `rebuildDspOptionsForMode` stays local; FIR graph checkbox disabled remotely; `DspOptionsPage::setTransmitPermitted` gates the SSB/AM, FM and Digital TX buffer, filter size and filter type combos; the receive-only Core refuses `DspOptions*Tx` settings writes and removes with the TransmitModel refusal reason (`StationServer::handleSettingsWrite`, `handleSettingsRemove`) and still accepts the RX keys | `tst_remote_gui_gating` (high-resolution cases, `remoteDspOptionsTransmitCombosFollowThePermission`), `tst_station_session` (`receiveOnlyStationRefusesTransmitDspOptionsSettingsWrites`, `receiveOnlyStationRefusesTransmitDspOptionsSettingsRemoves`, `acceptedReceiveDspOptionsWriteAppliesToMatchingSlices`), `tst_dsp_options_per_mode_apply` (`remote_rx_burst_applies_matching_slice_once`, `remote_rx_burst_across_groups_applies_each_slice_once`, `remote_unrelated_keys_apply_nothing`, `remote_apply_is_core_only`, `mode_group_mapping_is_pinned`); hardware pending (S1) |
| Display > Spectrum Defaults, Spectrum Peaks, Waterfall Defaults, Grid & Scales, Multimeter, 3D View | GUI-local display (Spectrum Defaults, Grid & Scales and Multimeter scope Mixed; Spectrum Peaks, Waterfall Defaults and 3D View scope ThisComputer); `DisplayFft*`, `DisplaySpectrumFps`, `MultimeterDelayMs` are Station keys; grid dB writes the mirrored `PanadapterModel`. While the Core's settings are unavailable, only those controls are disabled with the Core reason: Spectrum Defaults FFT size, window, Hz/bin target and frame rate (slider and box); Grid & Scales dB max, dB min and the copy from the waterfall thresholds; Multimeter sample interval. Spectrum Peaks, Waterfall Defaults and 3D View write no Core key | `SpectrumWidget`, `FFTEngine`, `MeterPoller` setters; each page's `setStationSettingsAvailable` | `tst_remote_fft_production`, `tst_remote_spectrum_render`, `tst_remote_gui_gating` (`disconnectedRemoteSetupPerPageTable`); hardware pending (S1) |
| Display > RX2 Display | Placeholder | all controls disabled | n/a |
| Display > TX Display | GUI-local display of TX (scope Mixed); TxAnalyzer absent remotely, setters null-guarded. The nine TX analyzer controls (`DisplayTx*` Station keys) are disabled with the Core reason while the Core's settings are unavailable | `SpectrumWidget` TX setters; `TxDisplayPage::setStationSettingsAvailable` | `tst_remote_gui_gating` (`disconnectedRemoteSetupPerPageTable`) |
| Transmit > Power, TX Profiles, Speech Processor, DEXP/VOX | Transmit | existing six-leaf pass | `tst_remote_tx_presentation` |
| Appearance > Colors & Theme, Meter Styles | GUI-local | `SpectrumWidget` colours, `AppearanceSmallModeFilterOnVfos` | n/a |
| Appearance > Gradients, Skins, Collapsible Display | Placeholder | disabled | n/a |
| CAT & Network > Serial Ports, TCP/IP CAT, MIDI Control | Placeholder | no writes | n/a |
| CAT & Network > TCI Server | GUI-local server; `Tci*` keys are Station-scoped | `CatTciServerPage`, local `TciServer` restart | hardware pending (S1); F6 |
| CAT & Network > 4O3A | Station-backed | remote master toggle through `requestFourO3AEnabled`, placeholder PGXL/TGXL tabs, no interlock page | `tst_remote_peripherals` |
| CAT & Network > Remote Station | GUI-local | `connectionsRequested` -> `connectionRequestedByOperator` | `tst_remote_gui_gating` (`theRemoteStationPageStaysUsableOnARemoteModel`) |
| CAT & Network > RF-Kit | **Unavailable (gated here)** | `RfKitPage` connects this computer's own `RfKitConnection` (`RfKitPage.cpp:174-190`) | `tst_remote_gui_gating` (`remoteDeclaredUnavailableSetupLeavesSayWhy`) |
| Keyboard > Shortcuts | Placeholder | no writes | n/a |
| Test > Two-Tone IMD | **Transmit (gated here)** | `TransmitModel::setTwoTone*` (not mirrored) | `tst_remote_gui_gating` (`remoteTransmitOnlySetupLeavesFollowThePermission`) |
| Diagnostics > Radio Status, Connection Quality, Logs, Logging & Performance | GUI-local | read-only or local keys | n/a |
| Diagnostics > Settings Validation, Export / Import | GUI-local file operations (Settings Validation scope Mixed, Export / Import scope ThisComputer); Settings Validation Reset and Forget change `hardware/*` (Station) keys and are disabled with the Core reason while the Core's settings are unavailable, and a Yes given after the link dropped under the open question changes nothing; Export / Import has no Core controls | `settingsHygiene()`, AppSettings file copy; `SettingsValidationPage::setStationSettingsAvailable` | `tst_remote_gui_gating` (`disconnectedRemoteSetupPerPageTable`, `settingsValidationYesAfterTheLinkDropsWritesNothing`); hardware pending (S1) |
| Diagnostics > Signal Generator, Hardware Tests | Placeholder | NYI | n/a |

### Gating added by this pass

- Setup leaves disabled with a visible reason above the page and as the page
  and leaf tooltip: DSP > CFC and Test > Two-Tone IMD follow the transmit
  permission; Hardware Config, DDC Routing and RF-Kit are declared
  unavailable in a remote session (`SetupDialog::markRemoteUnavailable`).
- Pages the local-DSP gate disables (Audio > VAX, TCI, Advanced) now show
  the reason "These settings control audio and signal processing on this
  computer. While connected to a Core, the Core does that work, so they
  cannot be changed here." instead of an unexplained grey page. Audio >
  Devices and TX Input were in this list until the R3 remote window Setup
  plan (Task 1): every Setup page now declares a scope (`SetupScope`:
  ThisComputer, Core, Mixed), the backend strip no longer counts as local
  DSP, and those two pages work in a remote window (rows above). A
  ThisComputer page that reaches an audited accessor is disabled, logged at
  critical and fails the Setup sweep. A remote window also skips the VAX
  first-run check (`tst_remote_gui_gating`,
  `vaxFirstRunCheckRunsOnlyInALocalWindow`).
- Setup in a disconnected remote window (R3 remote window Setup plan, Task
  2): File > Settings... opens in every state. Pages declared ThisComputer
  stay usable (a Devices change made while disconnected is saved on this
  computer and used once connected); Core pages are disabled with "Connect
  to the Core to change these." above the page and as the page and leaf
  tooltip, and a Core page opened before the Core's settings first arrive
  is an empty stand-in rather than a page built from this computer's
  defaults; Mixed pages disable only their Core controls with that reason.
  Nothing is sent to the Core, and nothing is held as an offline edit.
  When the Core's settings return, every realized Core and Mixed page built
  from an older snapshot is rebuilt (a later snapshot on a live session
  rebuilds it again); after Disconnect the pages keep the Core's last
  values, disabled. That reason comes before the transmit and local-DSP
  reasons on a page that has several (`tst_remote_gui_gating`
  `disconnectedRemoteSetupPerPageTable`,
  `reconnectRebuildsSetupPagesFromTheCoresValues`,
  `setupPageRebuildIsGuardedAgainstReentry`,
  `localSetupIgnoresStationSettingsAvailability`; `tst_settings_scope`
  `everyThisComputerPageKeyIsThisComputers`; `tst_remote_window_harness`
  `disconnectedWindowSetupKeepsThisComputersSettings`).
- Setup fix wave (R3 remote window Setup plan, final review): building a
  remote window, or showing a Core page while disconnected, writes none of
  the Core's settings, so the next connect never says that settings
  changed while the link was down did not stick when the operator changed
  none (the band plan name, the TCI compatibility flags and gains, and DSP
  Options' high-resolution filter setting were the writers;
  `tst_remote_window_harness` `freshWindowFirstConnectRaisesNoOfflineEditWarning`,
  `setupOpenedWhileDisconnectedRecordsNoEdit`). Connected to a Core that
  has not sent its settings, Core pages wait as stand-ins with "The Core
  has not sent its settings." and nothing is sent
  (`coreSetupPagesWaitForTheCoresSettingsNotAnySnapshot`). A page is never
  rebuilt under its own open dialog
  (`setupPageIsNotRebuiltUnderItsOwnOpenDialog`).
- The RX applet ATT/S-ATT row and RX1 preamp, and the General > Options Step
  Attenuator and Auto Attenuate groups: "The attenuator and preamp cannot be
  changed from a remote window yet."
- The RX applet's XIT row and its filter-preset Shift-click TX passband
  match follow the negotiated transmit permission, as the VFO flag's XIT
  and Shift-click already did.
- The VAX applet, the VFO flag's VAX selector and the overlay VAX combo:
  "VAX audio channels are not available while connected to a Core."
- Power Genius OPERATE and its Disconnect/Reconnect action: "Amplifier
  control is not available from a remote window yet."
- Fix wave (September 23): DSP > AGC/ALC TX Leveler and TX ALC groups and
  the nine DSP > Options TX combos follow the transmit permission
  (`SetupPage::setTransmitPermitted`, pushed by `SetupDialog` to every
  realized page); the RADE applet profile combo follows it and Reset
  vocoder carries the transmit reason remotely while transmit is denied
  and a station-computer reason once it is permitted; the RF-Kit RF2K-S OPERATE,
  antenna buttons and Disconnect/Reconnect carry the amplifier reason. The
  default reason before the station answers is now "Transmit controls are
  unavailable until the station confirms transmit permission." on the TX,
  RX and Phone/CW applets, the VFO flag and the RADE applet.

All of these are role-based or permission-based and never run in local
direct mode; the local halves of each new case, and the existing local
suites listed in the verification line, pass unchanged.

### Findings not gated here

- **F1, TNF is not applied live on the Core.** DSP > TNF, the status-bar TNF
  light, overlay +TNF and notch editing write `NotchModel`, whose state
  persists under Station-scoped `Notch*` keys. The Core accepts the write
  (`SettingsProxyServer::applyInboundWrite`) but only reads those keys at
  construction (`RadioModel.cpp:1828`), and a remote model has no local
  channels to apply them to, so the notch is drawn remotely and not applied
  until the Core restarts. TNF is a receive function, so it is not disabled
  here; it needs a Core-side live apply or a mirrored notch object.
  Update, September 23: the Core now owns the notch list and a window with
  capability `notchControlVersion=1` edits it with `notch.*` commands; see
  [remote notch control version 1](../2026-09-23-remote-notch-control-v1.md).
- **F2, settings written to the Core; DSP > Options now applied there.** The
  NR3 model selector writes `Nr3ModelPath` and calls `RNNRloadModel()` in
  this process; whether the Core applies it before its next restart is
  **hardware pending (S1)**. DSP > Options buffer and filter combos write
  `DspOptions*` (and call the window's local `rebuildDspOptionsForMode()`).
  The Core applies each accepted RX write or remove to the slices whose mode
  group reads the key, after a 50 ms coalesce and without a mode change
  (R-R3-21); a remove of a `DspOptions*Tx` key is refused on the
  receive-only Core like a write. Covered by `tst_station_session`
  (`acceptedReceiveDspOptionsWriteAppliesToMatchingSlices`,
  `receiveOnlyStationRefusesTransmitDspOptionsSettingsRemoves`) and
  `tst_dsp_options_per_mode_apply` (the `remote_*` cases); the audible
  effect on a live Core is **hardware pending (S1)**.
- **F3, closed in the fix wave.** DSP > AGC/ALC TX Leveler and TX ALC
  groups and the DSP > Options TX buffer/filter combos were not
  transmit-gated. The TX Leveler and ALC setters are not mirrored
  (`MirrorPolicy.cpp`), so an edit landed only in this window's own
  TransmitModel, the same reason CFC and Two-Tone are gated. The TX combos
  write `DspOptions*Tx`, and the `DspOptions` keys are station-scoped
  (`SettingsScope.cpp:366`), so each edit was a write to the Core's
  station transmit settings. All of them now follow the transmit
  permission, and the receive-only Core also refuses `DspOptions*Tx`
  settings writes the way it refuses TransmitModel writes; the pages stay
  available for their receive halves.
- **F4, closed here.** The RX applet's XIT row and filter-preset Shift-click
  were transmit gestures outside the flag's gate; both now follow the
  permission (see "Gating added").
- **F5, closed in the fix wave.** The profile combo set a TX mic profile
  without the transmit gate, and Reset vocoder was disabled without a
  stated reason. The combo now follows the permission and Reset vocoder
  gives the transmit reason remotely while transmit is denied, and once it
  is permitted says the vocoder runs on the station computer and cannot be
  reset from a remote window.
- **F6, TCI server in a remote window.** A remote window runs its own TCI
  server against the mirrored model. Receive state follows the Core; TX
  audio would feed this window's inert TX channel. Whether the server should
  run remotely at all is a product decision, not decided here.


## Current follow-ups (September 22)

The historical results below describe their named checkpoints. This table
tracks subsequent source work; installation and operator acceptance remain
separate, as recorded in the linked evidence and master plan.

| Surface | Remaining work |
| --- | --- |
| Peripheral configuration, scan and connect/disconnect | Task 4d's Core-owned configuration, identity checks and lifecycle commands are implemented. Actual TGXL identity/admission and operator acceptance remain open; see the master plan and deployment ledger. |
| 4O3A master enable / SmartSDR reporting / PGXL band tracking | Core-owned implementation is present; retain task 4d's hardware acceptance requirements. |
| Audio profile and health | Profile/telemetry/recovery implementation is present. Sustained playback remains open: the installed c28e1565 session recorded audio-context recoveries and source queue drops. No cause is inferred from those counters alone. |
| Full-band zoom | ADC-wide display and adaptive transport are implemented. Wideband/3D hardware parity and capacity acceptance remain open; see [display evidence](display-capacity.md). |
| Core/radio and local-radio selection | Task 4g implemented and installed at c28e1565, with desktop Core/DSP retained. Native interaction acceptance is pending; see [selector evidence](station-selection.md). Full pairing/administration remains R6. |

## Evidence and remaining checks

The snapshot regression first failed because automatic and pre-snapshot explicit
population each emitted an add. The accessory regressions first failed on the
missing daemon policy, band-triggered autotune and unapplied tuner fields.
These were behavior reproductions, not merely checks that slots exist.

Connection, visible TX/FIR gating, tuner and daemon suites pass after separating
the daemon test's synthetic controller notification from actual MOX admission.
The real admission request is required to be refused with RX state unchanged.

One consolidated independent review found that raw authenticated TransmitModel
writes still bypassed RadioModel's guard. A loopback regression reproduced the
station adopting the requested MOX model state. StationServer now rejects all
inbound transmit-object writes under its receive-only policy and uses the
existing correction delta to return authoritative values. This regression is
about model admission, not evidence that RF was emitted. The review's second
finding, the remote applet's local accessory reconnect action, is corrected
and covered by disabled-action/no-emission tests.

The lead also found that radio teardown cleared the local-role MOX check,
including the daemon's persistent receive-only policy. A regression using the
real non-null connection teardown path reproduced the missing refusal. Teardown
now preserves the receive-only check, while ordinary local-direct teardown
retains its existing behavior. This complements the session-disconnect test.

An initial unfiltered 682-test run exposed an unrelated popup-exposure wait in
`tst_dss_overlay_menu`. That test verifies pre-show value seeding/no signal echo;
it now performs those same assertions without requiring OS popup exposure.
The next full run passed 682/682 in 90.53 seconds before the additional radio
teardown regression. Final combined source, including the radio-teardown correction, passed a fresh
`all_tests` build and unfiltered `ctest --test-dir build-integration -j8
--no-tests=error --output-on-failure`: **682 test executables passed, zero failed, zero executables skipped**
in **93.58 seconds**. Logs are retained privately as
`r3-controls-teardown-full-{build,test}.log`. Corrected native build and
installation of signed `95b19467` passed; the matching GUI bundle also passed
code-signature verification. The [deployment ledger](README.md) records source
verification, installed hashes and the corrected packaging timestamp failure.

The receive smoke is **failed/incomplete**: authentication and waterfall frames
arrived, but audio stalled, the session timed out and steady spectrum/meter
operation was not established. The user reported slow waterfall painting that
stopped after a VFO movement. Manual Disconnect cancelled retry and remained
stopped. Network reachability to both the board and switch management endpoint
was lost while the router and Saturn remained reachable. No tuning cause or
hardware root cause is claimed; no live tuner connectivity is established.

Hardware smoke is receive-only: attach to the existing station, inspect endpoint
and radio state, disconnect and leave stopped, reconnect once, exercise the
three click surfaces and confirm slice count does not grow. Check disabled
controls and stale tuner presentation. Do not classify cached amplifier data
from the currently misconfigured TGXL endpoint as a working tuner.

The passive discovery capture was re-read after board recovery: the actual
announcement includes `TunerGenius`, version `1.2.17`, serial `241288-1` and
nickname `Tuner_Genius_XL`. An abbreviated investigation note omitted the last
two fields; there is no evidence that this live announcement lacks them.
Native captured `info` uses `serial`, while the existing model accepts only
`serial_num`; that normalization belongs with task 4d's identity validation.

## Scoped follow-up inventory

The second bounded control pass identified Phone/CW TX writers (mic/PROC/VAX/
DEXP), XIT controls, the inert remote TX-slice handoff, VFO RX-bypass-on-TX,
Tools TX Equalizer and editable TX-specific Setup pages. The September 22
follow-up below implements their consistent unavailable presentation and
interaction coverage in task 4c. This does not claim all visible controls or
their eventual remote TX implementations are complete. Keep shared
receive functions available: in particular, the antenna labelled TX participates
in TRX receive routing, so its name alone is not grounds for disabling it.
Review the actual station/receive effect before restricting Slice properties.

## Remaining named TX controls — September 22

Requirements R-R3-16/17/21/24. The existing negotiated `txPermitted` value
now reaches Phone/CW, every VFO flag, open and newly created Setup dialogs,
and Tools TX Equalizer. Remote widgets start unavailable before a handshake.
This is presentation of current capability, not a replacement for R4's TX
commands or Core's independent refusal of TX writes.

| Surface | Concrete boundary and acceptance evidence |
| --- | --- |
| Phone/CW MIC, PROC, VAX source, DEXP | Disable their writer widgets and guard direct value/context callbacks. Tests activate the widgets, observe zero model mutation or setup request, preserve model-to-widget readback, and restore local interaction without overriding mic mute. |
| XIT | Disable enable/offset/zero. A real MainWindow with an authenticated loopback Core produces no XIT property write or state change, while its RIT button produces an accepted Core change. |
| TX-slice badge and menu | Retain the indicator; suppress badge handoff and disable the actual dynamically constructed context action. Tests execute the nested menu and verify no remote handoff, with local handoff retained. |
| RX-bypass-on-TX | Disable BYPS and its writer callback. Do not restrict the TX-labelled antenna selector used for TRX receive routing. |
| TX Equalizer | Disable the actual Tools QAction and guard activation. The applet's alternate context callback is also guarded. Tests show no remote editor and successful local editor launch. |
| TX-specific Setup | Six explicit leaves: Audio TX Input/TX Profile; Transmit Power/TX Profiles/Speech Processor/DEXP/VOX (since September 23, Audio TX Input gates only its controls held for the radio; see the Setup leaves table). Content remains readable with a visible reason; editing and TX-EQ cross-links are disabled. Tests cover each page, local enablement, permission changes and preservation of the independent local-DSP gate. |

VAX Setup configures receive export through the audio engine. It retains its
existing resource restriction; it is not newly labelled a TX-only page.
Receive NR/ANF remains usable. No protocol schema or radio/DSP behavior changes.

Focused verification: rebuilt matching production libraries and 13 relevant
test targets, then ran all 13 successfully in 11.14 seconds with zero inner
Qt skips. The rendered remote Power page was inspected: the unavailable reason
is visible above the readable content, with no overlap. It is an offscreen
fixture, not evidence from the installed Mac.

Consolidated review found that capability updates after the initial handshake
did not refresh existing controls. An authenticated second-capability regression
first reproduced the stale XIT permission, then passed after wiring the existing
station-link notification into presentation refresh. It checks both directions
without changing the session epoch, including an already-open Setup page.

The first unfiltered run passed 739 of 740 targets but exposed a Core teardown
use-after-free in the native receive-layout test. AddressSanitizer reproduced
the VOX callback unregister accessing DEXP after destruction. The correction
unregisters the C++ wrapper while DSP objects are alive, closes the channel,
destroys DEXP and clears its dangling lookup slot before releasing its backing
buffer. The normal test passed in 38.82 s and the sanitizer test in 43.51 s.
The bounded follow-up lifetime review found no actionable issue. This is not
evidence that the earlier physical power or network failures share that cause.

The new context-menu fixture also needed to expose its parent VFO window before
opening native popups; after correction the widget test passed alone and with
the remote display controller. The matching production/all-tests rebuild and
unfiltered suite passed all 740 targets in 259.73 s. Signed checkpoint and
installed interaction verification remain pending. Native inspection of the
previously installed selector exposed a separate macOS accessibility crash;
that acceptance defect is being corrected before delivery.

The full-run load averages were 2.95 / 3.09 / 3.43 before build,
24.95 / 8.82 / 5.51 before CTest, and 7.58 / 8.14 / 6.09 afterward.
Twelve existing inner Qt cases skipped for the same device/UI, optional
capture, modal-menu, obsolete RADE TX and unopened WDSP TX harness reasons
recorded at the selector checkpoint. No executable was excluded or timed out;
those skipped TX behaviors still require R4 evidence.

The subsequent signed `46b01ebf` checkpoint includes the selector accessibility
correction and passed the final combined 740-target suite in 258.45 seconds.
Matching Core/GUI were installed; GUI authentication, Opus and display delivery
resumed after Core restart. External AppKit selector interaction and installed
appearance remain pending the Mac unlock. These backend observations do not
claim operator listening or visual acceptance.

The next presentation cleanup removes roadmap phase labels from remote
MOX/Tune/TGXL refusal messages and the PureSignal tooltip. They now identify the
unsupported operation in plain language. Existing admission guards and the
tests proving no state advance remain intact. The missing optional aggregate
display-capacity notice was already moved to diagnostics in `46b01ebf`; a
healthy fallback has no operating overlay. Actual limiting/failure states
remain visible. The associated S-meter report remains unreproduced: two live
observations showed the analog meter updating and matching the selected slice;
no meter source change is claimed.

This wording cleanup passed the combined GUI/Core build and the unfiltered
740-target suite in 277.34 seconds, with the same twelve existing inner Qt
skips. The [PC-source prerequisite evidence](pc-mic-source-intent.md) records
the shared run's load and log details. Installed appearance remains a separate
operator check.

## Follow-up at 8c011066

Title-segment activation opened Core details while media had failed but the
control session remained connected. Disconnect changed the panel to stopped
with Connect enabled; Connect began a fresh handshake and restored audio at
17:47:35. That verifies this manual recovery path. Automatic media-only
recovery is still missing (R-R3-28); later full control-link losses exercised
the existing retry path. The live receive status is tracked in the current
[deployment ledger](README.md), not the older 95b19467 result above.

## TGXL socket report and corrected endpoint, September 21

At 18:23 the GUI was repeatedly attempting the actual tuner IP with port 9008.
A read-only TCP connection check reached `.234:9010` in 0.01 seconds; port 9008
timed out. The user corrected the port, and the GUI log then showed recurring
status responses. This establishes local-GUI connectivity, not completion of
Core-owned accessory configuration or positive identity validation.

The retry log also exposed a separate lifecycle defect: after a connect timeout,
source binding reported a socket that was not in UnconnectedState, followed by
an invalid descriptor. That fault remains under investigation in task 4d.

The user next reported that ANT1–3 appeared but did not work. These controls
are explicitly disabled by the current receive-only remote policy; they are
not repaired by correcting the TCP port. Enabling tuner RF-path actions remains
part of the authorized remote tuner workflow, with Core ownership and capability
checks. No tuner antenna, operate or tune commands were exercised here.

After installation of 706b9a5f, Core itself auto-connected at 18:31:15 from
`.106` to `.234:9010`; `ss` confirmed the socket belonged to nereusd. It received
the captured real tuner info (`serial=241288-1`, `version=1.2.17`,
`nickname=Tuner_Genius_XL`, `3way=1`). Thus the corrected setting persisted to
Core and applied at restart. This does not establish the missing ordered live
apply/connect contract or identity rejection; no tuner RF action was sent.

A later socket check confirmed the separate ownership defect directly: GUI PID
54322 had `.30:52225 -> .234:9010` established while Core PID 3656 already owned
`.106:37183 -> .234:9010`. The GUI connection began at 18:32:22. This demonstrates
that remote Peripherals can still operate a Mac-local socket; it does not prove
that two clients caused any earlier device/network failure. Task 4d must route
remote configuration/connect/cancel to Core and keep the local socket inert.

The bounded TGXL retry/cancellation regression is tracked in
[TGXL connection lifecycle](tgxl-recovery.md).
