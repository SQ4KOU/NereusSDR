# RADE audio audit and next experiments

Reviewed latest Nereus main 979d721e69c3d09992ce4c87696e44262f36d368, FreeDV GUI a8d84248837561b618141d0e82d072533d6df127, FreeDV backend f1976cf0ba68a52bec7b0a1e54b3e861baebac89 and RADE C c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9 on 2026-10-11. Findings describe those sources before experimental V2 changes. Line references may move in subsequent edits.

JJ reports a syllable repeating several times, thought to be mostly propagation. No affected received waveform has been captured; the audit cannot establish its cause.

## Encoder input and modem output

The production TX path is microphone at 48 kHz -> mic gain -> peak tracker -> 80 Hz high-pass -> 16 kHz conversion -> int16 -> LPCNet -> RADE -> real modem audio -> 24/48 kHz conversion -> WDSP TXA -> radio. TX worker applies speech gain at src/core/TxWorkerThread.cpp:753 and peak tracker at :761. Modem substitution is :895-917; it falls through to the complete WDSP chain. TxChannel mode mapping at src/core/TxChannel.cpp:2080 only selects USB/LSB.

The factory RADE profile keeps Lev_On=True with 6 dB max gain and 300 ms release (src/core/MicProfileManager.cpp:764-779). RadioModel sends those controls to the pre-encoder tracker and WDSP (src/models/RadioModel.cpp:7687,19361); SetTXALevelerSt follows the on flag in src/core/TxChannel.cpp:4796. This contradicts the TX worker's explicit requirement that WDSP leveling be disabled for the modem waveform (:745). User microphone gain is similarly applied before encoding and through WDSP's panel gain (TxChannel.cpp:3986-3989, RadioModel.cpp:7482,7489).

WDSP TXA stages after injection include panel gain, phase rotation, downward expander, EQ, leveler, CFC/compression, CESSB and ALC (third_party/wdsp/src/TXA.c:554-587). The current optional speech EQ therefore processes modem audio, not speech entering LPCNet. Existing compressor state can survive profile selection. ALC_MaximumGain=0 does not turn off WDSP ALC: it means max gain 1, and attenuation remains possible (third_party/wdsp/src/wcpAGC.c:604-608; TXA.c:318-320). RF protection must be characterized before bypassing stages.

The current pre-encoder tracker aims at -9 dBFS peak. Current FreeDV backend uses -23 LUFS momentary loudness, +12/-20 dB gain limits, 0.5 s gain-reduction and 6 s gain-increase time constants, silence threshold -33 LUFS, and a -1 dBFS peak limiter (src/pipeline/AgcStep.cpp:47-54,82-98). These are distinct quantities, so changing just the target constant would not reproduce current FreeDV behavior. Its optional noise reduction, AGC and EQ are before encoding (freedv-gui/src/pipeline/TxRxThread.cpp:218-253).

Recommended separate experiment: capture speech before conditioning, the exact LPCNet input, raw modem output, and post-WDSP TX audio. Compare a FreeDV-style conditioner against the current tracker on quiet speech, loud speech, silence/background noise and PTT onset. Prove microphone gain acts once and ordinary speech processors do not reshape the modem. Keep fixed modem drive, sideband filtering, TUNE and RF protection explicit. This changes DSP policy and is not silently included in the V2 experiment.

## Receive repetition and lock

Production RX uses WDSP USB/LSB demodulated mono speech-band audio at 48 kHz -> 24 kHz -> RADE 8 kHz real input (imaginary zero) -> neural features -> FARGAN -> 24 kHz decoded speech -> bridge/mixer/playback. It is not the raw DDC IQ path described by older wrapper comments (src/models/RxDspWorker.cpp:1275-1312,781; src/core/RadeChannel.cpp:690; src/core/RxChannel.cpp:606). FreeDV similarly feeds real sound-card audio with zero imaginary component (freedv-backend/src/pipeline/RADEReceiveStep.cpp:216).

The wrapper re-reads dynamic rade_nin after each call and removes exactly the consumed samples, including EOO (RadeChannel.cpp:734-757). Features and output prefixes are consumed once (:760-822). The bridge consumes exact epoch/sequence slots once and drops obsolete decoded audio rather than playing it late (src/core/RadeRxWorker.cpp:132-158). The mixer also consumes once. No explicit whole-syllable replay was found.

V1 can continue feeding neural reconstruction while sync remains set and received features are damaged: valid_output is independent of the latest pilot candidate until timeout/UW conditions release sync (third_party/rade/src/rade_rx.c:249,365-375). This supports propagation as a plausible explanation, not a diagnosis. Remote Opus packet-loss concealment is a separate possible source of repeated speech-like sound (src/core/session/media/OpusAudioCodec.cpp:434). Missing raw PCM produces silence.

The newer C vendor restores oscillator phase normalization in V1 RX and BPF (upstream commits a2d77e85 and 5cb2436, 2026-08-31), preventing long-run phase drift. The V2 vendor refresh incorporates these fixes, but their connection to JJ's symptom is unproven. A long receive replay with nonzero frequency offset will be more informative than inferring causation from the diff.

Recommended capture: received mono waveform before RADE plus sync/SNR/offset and bridge drop counters, decoded speech before mixing, local/Core program PCM, and remote playback PCM. Decode the same received waveform offline in fresh Nereus and FreeDV instances with the same codec pin first; then compare the newer codec. A repeat in offline output implicates waveform/codec; a clean offline decode with paced-playback gaps implicates timing; clean Core PCM with impaired remote PCM implicates transport/concealment.

## Tests and their limits

Existing codec tests verify accumulation, output dimensions and EOO callsigns. Two direct-IQ TX pump assertions are currently skipped because they describe the old path (tests/tst_rade_tx_pump.cpp:172,482). Mic-profile tests assert settings, and contain stale explanations that ALC is off and WDSP is bypassed. Multislice tests wait for decode idle between blocks, so passing them is not proof of real-time deadlines (tests/tst_rade_rx_multislice_routing.cpp:415).

The standalone V2 lab in work/radev2-lab uses an ideal software channel and real upstream speech. It verifies successful sync, nonzero finite reconstructed speech, EOO and streaming text in the upstream test. It does not pass through Nereus TX/RX queues or WDSP, simulate HF fading, or establish perceptual superiority. CPU wall timings were recorded on a heavily loaded machine and are not performance benchmarks.

## Instruments

JJ selected microphone and decoded-speech waveform instruments as the next FreeDV-style container contents. Bind each instrument to a slice and use PR #381's workspace palette, headers and registry. The mockup also proposes receive sync/SNR/offset history, conditioning meters, audio spectrum, received station list, and separate RF/decoder/playback health indications. Readings in that artifact are illustrative; it does not add live telemetry to Nereus.

JJ refined this requirement: match FreeDV users' expectations for the microphone and speech waveform, and retain high resolution in all charts with high response rates. FreeDV's Frm Mic/Frm Decoder plots use five seconds of history, fixed normalized -1..+1 amplitude, one-second grid divisions and 0.2 amplitude divisions (freedv-gui src/main.cpp:1377-1382; src/defines.h:41,54-56; src/gui/controls/plot_scalar.cpp:783-842). Its plot input is microphone speech after conditioning/EQ and decoder speech before output processing (src/pipeline/TxRxThread.cpp:266-278,443-457). These facts guide a Nereus-native display design, rather than a wxWidgets code port.

Proposed plot contract: retain full-rate PCM history in a bounded buffer; display unsmoothed samples at close zoom, and preserve minimum/maximum excursions per physical screen column at broader views. Render at the actual pixel density. Default to the familiar five-second view, with one-second and 100-ms detail views. Spectrum uses the latest FFT frame, with no hidden temporal averaging; any optional averaging must be explicit. SNR plots retain the decoder's native reports and actual timestamps rather than inventing interpolated measurements. Rendering cadence is independent of capture cadence and cannot stall DSP or create an unbounded GUI queue. The browser preview now carries actual offline PCM fixtures and one FFT frame; its status/SNR and impairment examples are illustrative.


## Meter composition refinement — approved by JJ

JJ clarified that each RADE meter must behave like any other meter and be usable in either Stack or Canvas containers. The receive instruments and plots are independent items in the existing meter/content catalog; a dedicated RADE console is an optional saved arrangement of those items, rather than a separate host or an indivisible panel.

Use the ordinary add-item, selection, configuration, source binding, placement, resize, visibility, duplication and layout persistence workflows. RADE items can be mixed with existing meters in the same container or placed across several containers. Stack presents the items in ordered rows; Canvas uses the existing geometry and paint-order mechanisms. Switching layout changes presentation and preserves the items, their source bindings and plot settings. Container chrome belongs to the host; a meter does not acquire a separate container frame just because it reads RADE data. Apply PR #381's workspace styling to that existing host and its controls.

Reference basis: ContainerContentRegistry.cpp:336-389 restores item configuration, binding, stack metadata, RX/TX visibility, geometry and paint order; :415-447 captures ordinary items. FreeCanvasSurface.cpp:47-97 projects content entries and :145-205 edits their geometry. The design extends these mechanisms rather than creating a RADE-specific placement system. The V2 codec increment was followed by the separately approved native microphone and decoded-speech meter increment described below.

The microphone and decoded-speech waveforms remain the first items to implement. The full-rate history, peak-preserving broad view, close sample view and explicit averaging rules above apply independently of Stack/Canvas placement. The browser mockup now shows Stack and Canvas presentations of the same content under one regular container header. Its positions and readings remain illustrative.

## Native instrument increment

The approved first native items are Microphone waveform and Decoded speech waveform. Both are ordinary non-singleton container contents with normal source binding, properties, visibility, duplication, Stack/Canvas placement and persistence. The original faces match the pinned PR #381 palette; the independent workspace style framework is not merged by this increment. Details and acceptance are in [the waveform design](2026-10-11-rade-waveform-meters-design.md) and [implementation plan](2026-10-11-rade-waveform-meters-plan.md).

Mic defaults to the actual conditioned 16 kHz encoder input; the explicit Raw microphone option observes the existing 48 kHz input for setup. Decoded speech observes actual 24 kHz decoder output before playback padding. Five-second, one-second and 100 ms windows retain full-rate samples. Wide views preserve each physical display column's positive and negative extrema; close views draw the actual sample sequence. Missing input is a gap, while genuine zero samples remain silence. The source captions make tap/rate and unavailable/gated state explicit.

JJ additionally authorized remote Core desktop support. A dedicated negotiated observer channel carries exact bounded float32 samples, source/delivery identities and timing over the existing authenticated media transport. It uses existing access control and budgets, with explicit Unsupported/Unavailable/refused state rather than substituting program audio or downsampling. The contract is [Waveform media v1](2026-10-11-waveform-media-v1.md). This instrumentation does not alter the inherited conditioner, WDSP modem-stage policy, decoder lock thresholds or speech reconstruction. Those audio-audit findings still require a characterized follow-up; neither the V2 experiment nor waveform plots prove the reported on-air repetition is fixed.
