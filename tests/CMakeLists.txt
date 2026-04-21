# NereusSDR test suite. Opt-in via -DNEREUS_BUILD_TESTS=ON.
#
# Test targets compile against NereusSDRObjs (the OBJECT library that backs
# the main executable) so behavior under test is byte-identical to shipped
# code. Each test gets its own thin executable + add_test() registration.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(nereus_add_test name)
    # Additional source files may be passed as extra arguments (ARGN) —
    # needed by Phase 3I tests that link test-only fakes like P1FakeRadio.
    #
    # TestSandboxInit.cpp is always compiled in. Its global static
    # constructor calls QStandardPaths::setTestModeEnabled(true) before
    # main() runs, so AppSettings::instance() resolves to a sandbox
    # path instead of the real user config file. Without this, ctest
    # runs can (and have — see v0.1.1 alpha) silently wipe the
    # developer's NereusSDR.settings via P1FakeRadio-keyed test writes.
    add_executable(${name} ${name}.cpp TestSandboxInit.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE NereusSDRObjs Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ── Pre-existing tests from main (smoke + 3G-9 meter suite) ────────────────
nereus_add_test(tst_setup_helpers)
nereus_add_test(tst_smoke)

# ── Issue #75: receiver leak across disconnect/reconnect ──────────────────
nereus_add_test(tst_receiver_manager_reconnect)
nereus_add_test(tst_container_persistence)
nereus_add_test(tst_meter_item_bar)
nereus_add_test(tst_reading_name)
nereus_add_test(tst_meter_item_scale)
nereus_add_test(tst_meter_presets)

# ── Phase 3G-9b clarity defaults invariants ───────────────────────────────
nereus_add_test(tst_clarity_defaults)

# ── Phase 3G-9c Clarity adaptive display tuning ──────────────────────────
nereus_add_test(tst_noise_floor_estimator)
nereus_add_test(tst_noise_floor_tracker)
nereus_add_test(tst_clarity_controller)

# ── Phase 3I radio-connector tests ────────────────────────────────────────
nereus_add_test(tst_hpsdr_enums)
nereus_add_test(tst_board_capabilities)

nereus_add_test(tst_radio_discovery_parse)
# TEST_DATA_DIR lets the fixture loader resolve paths relative to the tests/
# source directory regardless of where the build tree lives.
target_compile_definitions(tst_radio_discovery_parse PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

nereus_add_test(tst_p1_wire_format)
target_compile_definitions(tst_p1_wire_format PRIVATE NEREUS_BUILD_TESTS)

# ── Phase 3P-A AlexFilterMap shared freq→bits helpers (Task 3+4) ──
nereus_add_test(tst_alex_filter_map)

# ── Phase 3P-A P1CodecStandard ramdor WriteMainLoop port (Task 5+6) ──
nereus_add_test(tst_p1_codec_standard)

# ── Phase 3P-A P1CodecAnvelinaPro3 bank 17 extra OC (Task 7) ──
nereus_add_test(tst_p1_codec_anvelina_pro3)

# ── Phase 3P-A P1CodecRedPitaya bank 12 ADC1 MOX carve-out (Task 8) ──
nereus_add_test(tst_p1_codec_redpitaya)

# ── Phase 3P-A P1CodecHl2 mi0bot WriteMainLoop_HL2 port (Tasks 9+10) ──
nereus_add_test(tst_p1_codec_hl2)

# ── Phase 3P-A regression-freeze gate (Task 16) ──
nereus_add_test(tst_p1_regression_freeze)
target_compile_definitions(tst_p1_regression_freeze PRIVATE
    NEREUS_BUILD_TESTS
    NEREUS_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")

# ── Phase 3P-B P2 codec tests ──
nereus_add_test(tst_p2_codec_orionmkii)

# ── Phase 3P-B Task 5: P2CodecSaturn — Saturn BPF1 band-edge override ──
nereus_add_test(tst_p2_codec_saturn)

# ── Phase 3P-B Task 7: regression-freeze gate ────────────────────────────
nereus_add_test(tst_p2_regression_freeze)

# ── Phase 3P-B Task 8: Alex-1 Filters sub-sub-tab smoke test ─────────────
nereus_add_test(tst_alex1_filters_tab)

# ── Phase 3P-B Task 9: Alex-2 Filters sub-sub-tab smoke test ─────────────
nereus_add_test(tst_alex2_filters_tab)
target_compile_definitions(tst_p2_regression_freeze PRIVATE
    NEREUS_BUILD_TESTS
    NEREUS_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")

# P1 loopback integration test — uses P1FakeRadio from tests/fakes/
nereus_add_test(tst_p1_loopback_connection fakes/P1FakeRadio.cpp)
target_compile_definitions(tst_p1_loopback_connection PRIVATE NEREUS_BUILD_TESTS)

# P1 reconnection state machine test — uses P1FakeRadio auto-streaming (Task 10)
nereus_add_test(tst_reconnect_on_silence fakes/P1FakeRadio.cpp)
target_compile_definitions(tst_reconnect_on_silence PRIVATE NEREUS_BUILD_TESTS)

# Saved-radio persistence round-trip (Phase 3I Task 15)
nereus_add_test(tst_connection_panel_saved_radios)

# HardwarePage capability gating (Phase 3I Task 21)
nereus_add_test(tst_hardware_page_capability_gating)
target_compile_definitions(tst_hardware_page_capability_gating PRIVATE NEREUS_BUILD_TESTS)

# HardwarePage per-MAC persistence round-trip (Phase 3I Task 21)
nereus_add_test(tst_hardware_page_persistence)

# SampleRateCatalog — PR #35
nereus_add_test(tst_sample_rate_catalog)

# Regression test for Windows shutdown/disconnect crash — cross-thread
# destruction of RadioConnection (and its worker-affined QTimers).
nereus_add_test(tst_cross_thread_teardown fakes/P1FakeRadio.cpp)
target_compile_definitions(tst_cross_thread_teardown PRIVATE NEREUS_BUILD_TESTS)

# Regression test for the GUI-thread fexchange2 deadlock — verifies
# that RxDspWorker::processIqBatch runs on its own QThread when driven
# via Qt::QueuedConnection, and that posting a burst of batches does
# not block the sender. See src/models/RxDspWorker.h for the rationale.
nereus_add_test(tst_rx_dsp_worker_thread)

# Regression test for rate-dependent audio jitter: RxDspWorker's drain
# chunk size must follow setBufferSizes() (per-rate Thetis formula
# in_size = 64 * rate / 48000), not a hardcoded 1024 that was correct
# only for 768 kHz and produced fexchange2 size mismatches at every
# other rate after PR #44 changed the default to 192 kHz.
nereus_add_test(tst_rx_dsp_worker_buffer_sizing)

# ── Phase 3G-10 VFO DSP parity + AetherSDR flag port ─────────────────────
nereus_add_test(tst_vfo_level_bar)
nereus_add_test(tst_scrollable_label)
nereus_add_test(tst_vfo_mode_containers)
nereus_add_test(tst_vfo_tooltip_coverage)

# Regression test for issue #73 — VfoWidget frequency-entry parser
nereus_add_test(tst_vfo_frequency_parser)

# ── Phase 3G-10 Stage 2: WDSP wiring — AGC advanced ──────────────────────
nereus_add_test(tst_rxchannel_agc_advanced)
nereus_add_test(tst_slice_agc_advanced)

# ── Phase 3G-10 Stage 2: WDSP wiring — EMNR (NR2) ────────────────────────
nereus_add_test(tst_rxchannel_emnr)
nereus_add_test(tst_slice_emnr)

# ── Phase 3G-10 Stage 2: WDSP wiring — SNB ───────────────────────────────
nereus_add_test(tst_rxchannel_snb)
nereus_add_test(tst_slice_snb)

# ── Phase 3G-10 Stage 2: WDSP wiring — APF (Audio Peak Filter) ───────────
nereus_add_test(tst_rxchannel_apf)
nereus_add_test(tst_slice_apf)

# ── Phase 3G-10 Stage 2: WDSP wiring — Squelch (SSB/AM/FM) ─────────────
nereus_add_test(tst_rxchannel_squelch)
nereus_add_test(tst_slice_squelch)

# ── Phase 3G-10 Stage 2: WDSP wiring — Mute / Audio Pan / Binaural (S2.6) ──
nereus_add_test(tst_rxchannel_audio_panel)
nereus_add_test(tst_slice_audio_panel)

# ── Phase 3G-10 Stage 2: WDSP wiring — NB2 polish (S2.7) ───────────────────
nereus_add_test(tst_rxchannel_nb2_polish)

# ── Phase 3G-10 Stage 2: RIT/XIT client offset (S2.8) ───────────────────────
nereus_add_test(tst_slice_rit_xit)

# ── Phase 3G-10 Stage 2: Frequency lock (S2.9) ──────────────────────────────
nereus_add_test(tst_slice_frequency_lock)

# ── Phase 3G-10 Stage 2: FM OPT container wire (S2.10a) ─────────────────────
nereus_add_test(tst_fm_opt_container_wire)

# ── Phase 3G-10 Stage 2: DIG offset + RTTY mark/shift (S2.10b) ──────────────
nereus_add_test(tst_dig_rtty_wire)

# ── Phase 3G-10 Stage 2: per-slice-per-band DSP persistence (S2.P) ───────────
nereus_add_test(tst_slice_persistence_per_band)

# ── v0.2.0 regression: band-change lambda reentrancy guard ───────────────────
nereus_add_test(tst_band_change_recursion_guard)

# ── Step attenuator / ADC overload detection ─────────────────────────────
nereus_add_test(tst_step_attenuator_controller)

# ── About dialog ─────────────────────────────────────────────────────────
nereus_add_test(tst_about_dialog)

# ── Phase 3O Sub-Phase 11 Task 11a: VaxFirstRunDialog ────────────────────
nereus_add_test(tst_vax_first_run_dialog)

# ── Phase 3O VAX routing ──────────────────────────────────────────────────
nereus_add_test(tst_slice_model_vax)
nereus_add_test(tst_transmit_model_tx_owner)

# ── Phase 3O VAX schema migration ─────────────────────────────────────────
nereus_add_test(tst_app_settings_vax_migration)

# ── Phase 3O Sub-Phase 8: VaxChannelSelector widget ───────────────────────
nereus_add_test(tst_vax_channel_selector)

# ── Phase 3O Sub-Phase 2: MasterMixer ─────────────────────────────────────
nereus_add_test(tst_master_mixer)

# ── Phase 3O Sub-Phase 3: PortAudioBus ────────────────────────────────────
nereus_add_test(tst_port_audio_bus)

# ── Phase 3O Sub-Phase 5.3: CoreAudioHalBus (macOS-only) ──────────────────
# Exercises shm-segment lifecycle + push/pull round-trip against the shared
# memory ring the HAL plugin consumes. shm_open / mmap are macOS-specific
# in the bus impl (Q_OS_MAC guard) — the test executable is only built on
# Apple targets to match.
if(APPLE)
    nereus_add_test(tst_core_audio_hal_bus)
endif()

# ── Phase 3O Sub-Phase 6: LinuxPipeBus (Linux-only) ──────────────────────────
# Exercises FIFO lifecycle + pactl module load/unload + push/pull round-trip +
# role policing + format validation + stale-cleanup idempotency.
# The impl is guarded by Q_OS_LINUX; the test binary is only built on Linux
# to match. Tests that call pactl QSKIP when pactl is not in PATH (CI runners
# do not install pulseaudio-utils; run on a developer Linux box for lifecycle
# and metering coverage).
if(UNIX AND NOT APPLE)
    nereus_add_test(tst_linux_pipe_bus)
endif()

# ── Phase 3O Sub-Phase 7 Task 7.1: VirtualCableDetector ──────────────────────
nereus_add_test(tst_virtual_cable_detector)

# ── Phase 3O Sub-Phase 8.5: AudioEngine VAX tee logic ────────────────────────
# Exercises the rxBlockReady → m_vaxBus[] tee using FakeAudioBus injected
# via the NEREUS_BUILD_TESTS-only setVaxBusForTest / setSpeakersBusForTest
# seam on AudioEngine. No real CoreAudioHalBus / LinuxPipeBus / PortAudio
# backend required — runs on every platform.
nereus_add_test(tst_audio_engine_vax_tee)

# ── Phase 3O Sub-Phase 10 Task 10a: AudioEngine master-mute API ──────────────
# Covers setMasterMuted / masterMuted / masterMutedChanged. Verifies the
# rxBlockReady mute check gates ONLY the speakers push (VAX taps must keep
# running so 3rd-party consumer apps don't hear the local monitor mute).
nereus_add_test(tst_audio_engine_master_mute)

# ── Phase 3O Sub-Phase 10 Task 10b: MasterOutputWidget ──────────────────────
# Menu-bar composite: speaker-mute button, master-volume slider (0–100),
# inset percent readout, right-click output-device picker. Verifies the
# widget→AudioEngine user path, the AudioEngine→widget echo path with
# re-entry prevention, AppSettings round-trip (audio/Master/Volume,
# audio/Master/Muted), and construction-time restore of saved values.
nereus_add_test(tst_master_output_widget)

# ── Phase 3O Sub-Phase 10 Task 10c: TitleBar host strip ─────────────────────
# 32 px strip that hosts the QMenuBar + MasterOutputWidget. Smoke only:
# construction, setMenuBar() re-parenting, masterOutput() accessor, and
# the fixed 32 px height. Live UI verification is the canonical check for
# the menu-re-parent + in-window render on macOS.
nereus_add_test(tst_title_bar)

# ── Phase 3O Sub-Phase 9 Task 9.2a: AudioEngine VAX gain/mute/tx-gain ────────
# Exercises setVaxRxGain / setVaxMuted / setVaxTxGain and their effect on the
# rxBlockReady tee — unity passthrough, half-gain scaling, muted-skip, signal
# emission, clamp-to-range, invalid-channel no-op. Same FakeAudioBus harness
# as tst_audio_engine_vax_tee.
nereus_add_test(tst_audio_engine_vax_gain)

# ── Phase 3O Sub-Phase 9 Task 9.2b: VaxApplet smoke tests ────────────────────
# Widget-level construction, signal plumbing, AppSettings round-trip.
nereus_add_test(tst_vax_applet)

# ── Phase 3O Sub-Phase 9 Task 9.2c: SpectrumOverlayPanel VAX Ch wiring ───────
# setRadioModel() binds the left-edge overlay's VAX Ch combo to slice 0's
# vaxChannel() bidirectionally; IQ Ch stays disabled (feature-flagged per
# design spec §6.7/§11.3).
nereus_add_test(tst_spectrum_overlay_panel)

# ── Phase 3P-A Task 15: RxApplet S-ATT slider range from BoardCapabilities ───
# HL2 → 0-63 dB; Hermes → 0-31 dB. Verifies the spinbox max is sourced from
# BoardCapabilities::attenuator.maxDb at construction time, not hardcoded.
nereus_add_test(tst_rxapplet_att_range)
target_compile_definitions(tst_rxapplet_att_range PRIVATE NEREUS_BUILD_TESTS)

# ── Phase 3P-B Task 10: per-ADC OVL split + RX1 preamp byte 1403 bit 1 ─────
# Single-ADC boards (HL2/Hermes): 1 OVL badge.
# Dual-ADC boards (OrionMKII, p2PreampPerAdc=true): 2 OVL badges.
# P2RadioConnection::setRx1Preamp(bool) routes to byte 1403 bit 1 via codec.
# Default state (rx1Preamp=false) leaves bit 1 clear → regression-freeze safe.
nereus_add_test(tst_rxapplet_adc_ovl)
target_compile_definitions(tst_rxapplet_adc_ovl PRIVATE NEREUS_BUILD_TESTS)

# ── Phase 3P-C Step 2+3: preamp combo per-board contents contract ─────────────
# Locks BoardCapabilities::preampItemsForBoard() output 1:1 with Thetis
# SetComboPreampForHPSDR console.cs:40755-40825 [@501e3f5]:
#   Atlas no-Alex → 2 items (on_off); Atlas with Alex → 7 items (on_off+alex)
#   Hermes no-Alex → 4 items (anan100d); Hermes with Alex → 7 items
#   OrionMKII/Saturn → 4 items always; HL2 → 4 items (anan100d, Phase 3P-C fix)
# Also verifies RxApplet.preampComboItemCountForTest() reflects board caps at
# construction (Phase 3P-C Step 3: no more hardcoded "0dB/-10dB/-20dB/-30dB").
nereus_add_test(tst_preamp_combo)
target_compile_definitions(tst_preamp_combo PRIVATE NEREUS_BUILD_TESTS)
