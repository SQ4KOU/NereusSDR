# TCI missing-slice mode crash fix

**Goal:** Keep WSJT-X using Slice A through TCI RX1 and prevent malformed mode messages when either exposed slice is absent.

**Architecture:** Preserve stable slice IDs and the existing two-receiver TCI surface. A missing slice has no mode; suppress its mode message and ignore its mode commands. Do not invent a DSP default or remap another slice.

**Tech stack:** Existing C++20/Qt6 TciProtocol and RadioModel; QtTest.

**Evidence:** macOS WSJT-X crash reports at 08:27 and 17:24 on 2026-10-12 identify the same unchecked mode argument in TCITransceiver::onMessageReceived. Live TCI captures show `modulation:0,;` with A absent, and `modulation:1,;` after B was removed. Thetis v2.10.3.15 TCIServer.cs:2174-2183 suppresses FIRST/LAST sentinel modes; :2193-2194 formats valid modes; :2508-2509 visits both receivers even with RX2 disabled; :4063-4073 handles mode queries through sendMode.

## Constraints and acceptance

- Slice A/id 0 stays TCI RX1; B/id 1 stays RX2, including deletion gaps.
- No empty `modulation` messages on connect or query. Commands for missing or unadvertised receivers have no effect or fabricated acknowledgement.
- Keep existing valid mode output, settings, port 50001, TCI audio, DSP defaults and signal routing.
- Use real RadioModel regression cases: A alone, B alone, neither, deletion while connected, surviving slice mode updates.
- Preserve existing attribution and inline comments. This is a NereusSDR adaptation for dynamic slices, not a new upstream port.

## Task: Prevent unavailable mode messages

Files: `src/core/TciProtocol.cpp`, `tests/tst_tci_init_burst_live_state.cpp`.

1. Add regression cases for initialization with missing receivers and mode query/set after deletion; assert valid remaining receiver messages retain their IDs and missing receivers produce no modulation frame or pending acknowledgement.
2. Build `tst_tci_init_burst_live_state` and observe those cases fail on malformed mode output before the production edit.
3. Guard initial mode emission on a nonempty mode; validate advertised receiver IDs and explicit mapping vacancy before mode commands. Suppress empty query readback and empty post-set authoritative readback; preserve existing remote-window authoritative replies.
4. Rebuild and run the full live-state test, plus relevant init golden/smoke and dispatch coverage. Build the application. Run diff/attribution checks. Do not build the entire test suite by default (AGENTS.md).
5. Review the final diff and report the tested code, current Slice A configuration, and whether the running application has been replaced.

## Progress

- Upstream investigation: Sol 6.1, medium; usage unknown. Verified suppression matches Thetis sentinel behavior; no architectural or DSP change required.
- Worktree: `/Users/j.j.boyd/.codex/worktrees/tci-missing-slice-mode/NereusSDR`.

- Baseline corrected to `a83dad16e`, the exact running `codex/combined-20261012w` build, after finding the app includes development changes ahead of main. Regression edits were preserved. Receiver mapping callbacks must also be guarded against vacancy so RX2 cannot fall back to Slice A.

- RED verified: live-state test had 27 passes and 5 expected failures: A-only/B-only/neither empty init modes, removed-slice query, and vacant receiver query alias. Build log `/tmp/nereus-tci-red-build.log`; test log `/tmp/nereus-tci-red.log`.

- Final RED rerun with corrected cold-start fixture: 27 pass / 5 expected failures (`/tmp/nereus-tci-red2.log`). The model refuses deletion of its last slice, so the zero-slice case now uses a cold empty model.
- Synthetic golden updated to omit its two malformed empty-mode lines; all other lines preserved.
- GREEN: all 5 targeted test executables pass (matrix, dispatch, init smoke, init golden, init live-state); app build succeeds. Logs: `/tmp/nereus-tci-green2.log`, `/tmp/nereus-tci-green2-build.log`.
- Attribution: 447/447 Thetis headers pass; inline tag preservation passes after retaining `//MW0LGE` beside the widened sendMode block. Full scan reports upstream-not-found warnings outside the patch; no missing tags.
- Review: Sol 6.1 high, usage unknown, no actionable code findings. Independent TX investigation (Sol 6.1 high, usage unknown) confirms explicit same-slice TX selection clears the Core's taken-not-chosen marker; changing that admission policy is out of scope.
- Live setup: WSJT-X TCI RX1, endpoint 127.0.0.1:50001, TCI audio enabled. Network error after user restart was retried, and reception at 14.074 MHz verified. Enable Tx was off at verification; on-air WSJT-X transmission not re-tested by the agent.
- Delivery: source remains isolated; running Dev app has not been replaced. The patch is based on the exact a83dad16e development baseline, not main.
