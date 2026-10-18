# Maintainer P2 PureSignal bench check

This procedure is for a maintainer operating the established station setup after reviewing the exact build SHA. The latest signed implementation tip is `cd059a154cf47fae90897300f20c318fa41ad643`; record the exact build SHA used for the actual station run. Independent review returned Ready with no blocking findings, and the exact-tip app/headless Mac build passed. Live P2 acceptance remains pending. Protocol 2 is user-confirmed. The manager has reported an Anvilina Pro3 on a STRIX Windows local station and a Mac remote Core client; this implementation did not verify that station configuration, firmware, DUP state, cabling, receiver coupling, or the original bench symptom. Record the actual setup before testing.

Workers have not connected to or keyed a radio. **Only the maintainer keys the radio for this check. No automated TX operation is part of the procedure.** Use existing two-tone controls and station defaults. The UI refresh does not prescribe test power, tones, DSP parameters, attenuation, or calibration settings.

## Before the test

Record the application and Core commit SHAs, build type, Windows local or Mac remote client, radio model, firmware, Protocol 2 state, and actual receiver/feedback source. Record DUP, Show Two-tone Measurements, MOX, active TX slice, keyed TX pan, focused pan, span, CTUN, FFT settings, and whether receiver display frames are arriving. Do not infer PA coupling from the UI's **Receiver spectrum** label: that label identifies accepted receiver pixels and does not certify a coupler or dedicated PureSignal feedback DDC.

The existing source contract makes DUP state material. With DUP enabled, ordinary receiver frames can be shown as receiver-spectrum IMD when the other gates and current source frame are valid. With DUP disabled, the digital TX analyzer feed is not measured RF and must not be interpreted as PA IMD. If the receiver is suspended or current eligible frames are absent, the UI should wait and blank current readings. Do not enable DUP, reactivate a receiver, change routing, or otherwise bypass station policy to make a display appear.

## During intentional two-tone operation

After the maintainer keys the station through the established workflow, verify the actual TX-host pan owns the measurement even when another pan has focus. Record the displayed source/status, six RF product locations, feedback reading and its classification, correction-applied state, successful calibration count, and attempt count. Feedback values may be the latest calibration feedback value rather than an instantaneous RF meter; the UI identifies that distinction. Hardware peak is a digital feedback reference, not a distortion-improvement measurement.

Identify the six detected locations from the actual tone pair: fundamentals `F1` and `F2`; third-order products `2F1−F2` and `2F2−F1`; fifth-order products `3F1−2F2` and `3F2−2F1`. Record their RF frequencies, visible positions and levels. For unequal tones, record the individual-product reference (stronger fundamental) separately from the worst-order summary reference (weaker fundamental against the stronger product). An unresolved or out-of-view product must stay blank or show the corresponding partial/outside state; the full summary must not imply a valid six-product measurement.

Record whether frames continue arriving during the test. The GUI has a Nereus-only freshness boundary of `max(1000 ms, five expected frame intervals)`: after that, current values and markers clear while the held trace and waterfall remain. A remote `NeedKeyframe` loss invalidates immediately and stays waiting until an accepted keyframe recovers. Frames already queued before that loss remain displayable in playout order, but are measurement-ineligible and must not restore current readings or advance EMA; only a newly accepted recovery-keyframe Frame can restore current IMD. If a queued frame renders after the loss, confirm IMD remains Waiting with blank current readings until recovery. This is a presentation policy, not a protocol or DSP timeout. Compare observed timing with the frame cadence and note any false expiry or stale display.

Switch between rail, inspector and Bench tabs and move the AmpView cursor. These actions must not key the radio, change DUP or advance measurement smoothing. AmpView cursor values use each curve's actual input domain; unavailable, invalid, or out-of-domain readings remain blank. Check current versus retained curve captions after receive/transmit transitions and reconnection.

## Correction and recovery observations

Using the station's established procedure, record readings before and after correction while preserving the same test conditions. Record actual correction-applied transitions, not desired automatic state. A waterfall event should align with an accepted receiver row after the observed state change. Pause, rewind, resume and allow ring wrap; the event must remain attached to the same retained row and expire with that row. Frequency guides mark expected product locations, not historical measurements. Hiding guides must leave correction history visible.

Turn off the measurement display and two-tone with existing controls. Confirm measurement values and GPU markers clear while MOX follows its existing lifecycle. Confirm no UI action changed radio settings or keyed the station. Change source, view/context or connection and verify current measurements clear; recovery should start a new smoothing interval from a newly accepted eligible frame.

## Record the outcome

Preserve the build SHA, radio/protocol/firmware information, actual gates, receiver source and frame-arrival observations, TX pan and focus, six RF locations, before/after correction state, screenshots or notes, and any relevant protocol evidence. Mark each of these separately: GUI status correctness, receiver-spectrum source eligibility, physical RF coupling, and PA calibration behavior. Passing GUI fixtures do not establish the latter two.

If eligible receiver frames do not arrive, record the source and gate state and investigate using existing P2 receiver/DDC assignments and suspension behavior. Request a packet capture only when a concrete protocol ambiguity remains. A dedicated feedback FFT, new route, or Core/DSP change is outside this procedure and requires separate architecture approval.

**Result:** pending maintainer operation. No live RF, PA-coupler, or calibration result is claimed here.
