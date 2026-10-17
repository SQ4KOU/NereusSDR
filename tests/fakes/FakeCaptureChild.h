// =================================================================
// tests/fakes/FakeCaptureChild.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Scripted stand-in for the
// nereus-audio-capture helper, run by a test binary re-executing itself
// with --fake-capture-child <scenario>; no Thetis logic.
//
// Protocol 3: AttachRing attaches the window's region and answers
// RingAttached; "streaming" builds a clock matcher (48 kHz in and out,
// 480-frame bursts) in the region and writes 480 stereo frames of a 1 kHz
// tone at 0.5 every 10 ms, posting the wake after each write.  No
// scenario but oversize, pcm-record and pcm-record-on-probe-enable sends
// a Pcm record.
//
// Every Ready reports the configured device's name ("Fake microphone" when
// the default is configured) at 48000 Hz, 1 channel, latency 1500 us and a
// 480-frame buffer.
//
// Scenarios (N is the generation of the Open being answered):
//   ready                 Hello; on Open: Opening, Ready, then streaming.
//                         Stop ends streaming and answers Stopped.
//   hang-open             Hello; on Open: Opening, then nothing at all
//                         (Stop and Shutdown are ignored too).  With
//                         NEREUS_FAKE_CAPTURE_HANG_DIR set, it then creates
//                         the empty file <dir>/<its pid>.  Before that
//                         Open, Shutdown still ends it.
//   hang-open-after-step  As hang-open, after one "open step" line on
//                         stderr ("devices listed at 12 ms"); no marker file.
//   hang-open-after-tagged-steps
//                         As hang-open with current, stale-generation,
//                         wrong-process and malformed open-step tags.
//   no-hello              Never writes anything and ignores every command.
//   permission-then-ready Hello; on Open: Permission, 300 ms later as ready.
//   permission-while-marked
//                         Hello; on Open: Permission, held while the file
//                         NEREUS_FAKE_CAPTURE_PERMISSION_FILE names exists;
//                         once the test removes it, as ready.  The hold
//                         lasts as long as the test wants, so it can
//                         outlast a deadline without racing it.
//   crash-after-ready     As ready; exits with code 3 after 100 ms of streaming.
//   malformed             Hello, then a record with bad magic at once.
//   oversize              Hello; on Open: Opening, Ready, then one PCM
//                         record whose header claims frameCount 4801.
//   input-lost            As ready; after 100 ms of streaming sends Failed /
//                         input-lost and stops streaming.
//   ignore-stop           As ready but never answers Stop.
//   stale                 Hello; on Open: Opening for N, then Ready tagged
//                         with generation N-1 (4294967295 when N is 1), and
//                         streaming into N's ring.
//   probe                 As ready; a ProbeEnable before its Ready exits with
//                         code 4.  While enabled, a ProbeHit every 50 ms with
//                         captureNs 1000 x (times enabled) + (hits since).
//                         A disable answers an AsioCaps record whose driver
//                         is "Probe off", after which no hit is sent.
//   version-1             A protocol 1 Hello in a version 1 record header.
//   pcm-record            Hello; on Open: Opening, Ready and one valid
//                         480-frame Pcm record.  No ring is built and no
//                         wake posted, so the window is never Ready.
//   pcm-record-on-probe-enable
//                         As ready; a ProbeEnable (on) is answered with one
//                         valid 480-frame Pcm record.
//   crash-on-probe-enable As ready; a ProbeEnable (on) exits with code 3.
//   input-lost-on-probe-enable
//                         As ready; a ProbeEnable (on) is answered with
//                         Failed / input-lost and streaming stops.
//                         These three let a test fix the order: it turns
//                         the probe on once it has seen Ready, so the act
//                         always follows that Ready.  crash-after-ready and
//                         input-lost act on a clock and suit a test that
//                         waits for the failure alone.
//   busy                  Hello; on Open: Opening, then Failed /
//                         device-in-use.
//   busy-while-marked     As busy while the file NEREUS_FAKE_CAPTURE_BUSY_FILE
//                         names exists, else as ready: another program lets
//                         the mic go when the test removes the file.
//   busy-while-marked-then-pending
//                         As busy while the file NEREUS_FAKE_CAPTURE_BUSY_FILE
//                         names exists; once it is gone, an Open answers
//                         Opening and nothing more (Stop and Shutdown are
//                         still honoured): the engine's retry stays pending.
//   bad-ring              Hello; on Open: Opening, Ready, then a wake with
//                         the region's ring header zeroed.
//   hang-warm             As ready until Warm; then nothing at all (Stop
//                         and Shutdown are ignored too), as a helper stuck
//                         in its first audio call.  With
//                         NEREUS_FAKE_CAPTURE_HANG_DIR set, it then creates
//                         the empty file <dir>/<its pid>.
//   warm-while-marked     As ready, but Warm is answered only once the file
//                         NEREUS_FAKE_CAPTURE_WARM_FILE names is gone; every
//                         command after it (Shutdown too) waits behind it,
//                         as in the real helper.  With
//                         NEREUS_FAKE_CAPTURE_HANG_DIR set, it creates the
//                         empty file <dir>/<its pid> once it holds Warm.
//   exit-after-warm       As ready until Warm; answers it with Warmed, then
//                         exits with code 3, as a helper that warms and
//                         then crashes.
//   warm-wrong-mic-mode   As ready, but its Warmed answers the Mic Mode
//                         question its Warm did not ask: none for a Warm
//                         that names a bundle, one for a Warm that names
//                         none.
// Every scenario accepts ProbeEnable; only probe acts on it (V-HW-8).
// Every scenario answers AsioDescribe with one driver, "Fake ASIO" (4
// outputs, 2 inputs), AsioOpen with running (256 frames, 48 kHz) or, with
// no uses, closed, and accepts AsioControlPanel (Task 15).
// Every scenario that is still answering commands answers Warm with Warmed
// (one input, listed at once; a Warm that names a bundle, as a stored Voice
// Isolation made Standard); hang-open and its kin answer it until their
// Open, no-hello and hang-warm never (2026-10-16).
// Every scenario exits with code 0 on stdin EOF, and on Shutdown unless
// it is ignoring commands.  An unknown scenario exits with code 2.
//
// Modification history (NereusSDR):
//   2026-10-08: native audio plan Task 1 (V-HW-8): probe and version-1
//               scenarios. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-09: native audio plan Task 13 (R-AUD-17): protocol 3, the
//               tone through a clock matcher ring in the window's shared
//               region; pcm-record, busy and bad-ring scenarios.  J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan Task 15 (R-AUD-19): the ASIO records.
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan Task 16 fix round 2: Ready names the
//               configured device; busy-while-marked. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan Task 20 round 3 (R-AUD-24):
//               busy-while-marked-then-pending. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan final fix wave (V-HW-8): the probe
//               scenario marks a disable.  J.J. Boyd (KG4VCF), AI-assisted
//               via Anthropic Claude Code.
//   2026-10-11: pcm-record builds no ring; the three -on-probe-enable
//               scenarios.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-11: kFakeCaptureChildStartMs; the permission-while-marked
//               scenario.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-12: generation-tagged open-step diagnostic regression.
//               J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-16: Warm is answered with Warmed (the persistent helper);
//               hang-warm, warm-while-marked and exit-after-warm.
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-16: a Warm that names a bundle is answered as a stored Voice
//               Isolation made Standard; warm-wrong-mic-mode.  J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QString>

namespace NereusSDR::Test {

// How long a test allows a newly started scripted helper before it calls the
// start failed: the window's hello deadline (3000 ms in the app), and the
// test's own wait for the first thing that start leads to.  The helper is
// the test binary started again, so it loads the app's libraries before its
// first write, and on a very busy computer that has taken more than 3
// seconds (twice in about 750 starts at a load of 133 to 156 on 18 cores,
// 2026-10-11).  A pass costs nothing: the hello comes in about 80 ms and a
// wait returns when its state is seen.  Only a helper that never starts
// takes this long to fail.  The cases that test the hello deadline itself
// set their own short value.
inline constexpr int kFakeCaptureChildStartMs = 30000;

int runFakeCaptureChild(const QString& scenario);

} // namespace NereusSDR::Test
