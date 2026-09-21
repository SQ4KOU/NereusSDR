# Core and remote GUI: restart point

Recovered on 2026-09-20 by J.J. Boyd (KG4VCF), with OpenAI Codex assistance.

## Where the work lives

| Work | Location | Verified state |
| --- | --- | --- |
| Shipping baseline | `main` at `efd88e69` | Does not contain the daemon split |
| R1 foundation | [PR #315](https://github.com/boydsoftprez/NereusSDR/pull/315), branch `claude/nereus-thin-client-arch-8f24ec`, tip `a7324cdd` | Open; conflicts with main. Its last Linux, Windows, macOS, compliance and CodeQL checks passed. Parts of its PR description still predate those results. |
| R2 control session | `claude/remote-daemon-r2`, worktree `/Users/j.j.boyd/NereusSDR/.worktrees/remote-daemon-r2` | Recovered clean at `efddd7ea`, 75 commits after R1. No remote branch or R2 PR was found. |
| R3 media | Existing architecture and R2/R3 addendum | No R3 implementation plan or media implementation found in the recovered branch |

The build targets are `NereusCore`, `NereusGui`, `nereusd`, and `NereusSDR`.
"NereusUI" refers to the existing GUI running in remote mode; there is no
separate executable with that name yet.

## What works and what remains

R1 extracts spectrum production from the GUI and builds a headless daemon.
It was built and run on a Raspberry Pi 4B, 8 GB, Debian 13 aarch64. Its
bench recorded multi-slice I/Q degradation and a systemd shutdown crash.
The missing installed RADE library was subsequently fixed.

R2 implements TLS and token authentication, the state mirror, station settings
proxy, slice commands, meters, heartbeat/reconnect, and `--station` GUI mode.
Its 20 planned tasks and subsequent code review fixes are committed. Its
last August handoff still had incomplete hardware and UI acceptance rows.
The August 9 result of 618 passing tests excluded ten known failing tests;
it was not a clean pass of every registered test.

R2 intentionally has **no remote spectrum, waterfall, audio, or transmit**.
R2 plus R3 is the agreed remote receive release unit. R4 adds transmit and
its safety checks. R5 adds NAT traversal and relaying.

## Work performed on September 20

Two unresolved PR #315 review findings also existed on R2:

1. `DaemonApp`'s connection-state relay dereferenced its owning `unique_ptr`
   after `reset()` had cleared it. A regression reproducing that lifetime
   window failed with SIGSEGV. The relay now uses the emitted state value.
2. The termination signal handler posted a queued Qt call, allocating inside
   signal context. The new regression caught allocation for both SIGTERM
   and SIGINT. The handler now sets a `sig_atomic_t` flag; a main-thread timer
   requests shutdown. Subprocess tests also exercise the actual entry point
   and require both signals to produce normal exit code 0.

Both executables were rebuilt. These six test executables passed after the
fixes on macOS arm64 with Qt 6.11.0:

- `tst_daemon_app`
- `tst_daemon_signals`
- `tst_remote_slice_commands`
- `tst_station_session`
- `tst_session_link_loss`
- `tst_remote_gui_gating`

This is focused verification, not a new full-suite or cross-platform claim.
The fixes are on the R2 worktree; PR #315 has not been updated with them.

## Hardware rediscovery

The current target is the maintainer's **Radxa 5C running Raspbian Trixie**.
Its SSH username/address is still needed. Verify its actual architecture,
OS packages, and free storage after connecting, before choosing build steps.

The previous SBC was `raspberrypi-flex`, reached as `jj@192.168.109.133`.
SSH to that address timed out on September 20; `raspberrypi-flex.local` did
not resolve. That previous Pi is not the selected installation target.

Read-only P1 and P2 discovery on the current LAN found:

| Radio | IP | MAC | Board byte | State at discovery |
| --- | --- | --- | --- | --- |
| ANAN-G2E / HermesC10 | `192.168.109.198` | `40:84:32:B0:B0:8D` | `0x14` | Idle |
| ANAN-G2 / Saturn | `192.168.109.45` | `2C:CF:67:AB:FC:F4` | `0x0A` | Idle |

The existing bench instructions authorize the G2E and explicitly exclude the
G2. Always repeat both discovery probes and identify by board byte before a
future run; pin the chosen MAC in the daemon config. Never fall back to the
first radio discovered.

September 20 bench used separate `r2resume_20260920` daemon and
`r2resume_20260920_client` GUI profiles, loopback TLS port 50056, one slice,
192 kHz, and the G2E's pinned MAC. No MOX or TUNE command was issued.

- First daemon run generated its cold WDSP cache and logged
  `Connected to "ANAN-G2E"` at 20:06:10 local time. Both processes exited 0
  on SIGTERM. The first client attempt correctly refused a fingerprint
  supplied in the wrong format by the temporary launch helper; the helper
  was corrected to the documented colon-separated format.
- On restart, discovery found only the excluded G2. The daemon remained
  disconnected instead of selecting a different radio. The G2E no longer
  answered either broadcast or direct discovery. This follows shutdown but
  does not yet establish its cause; the radio's power/network state and
  reconnect behavior need investigation.
- The second GUI established the authenticated TLS session. Settings opened
  through the remote proxy. Radio > Connect, Disconnect, Manage Radios and
  Protocol Info were visibly disabled. Spectrum and waterfall stayed blank,
  as expected for R2. The session survived opening Settings.
- Both second-run processes also stopped with exit code 0. No bench process
  was left running. A direct P2 discovery retry and ping still received no
  answer from the G2E; the G2 remained discoverable.
- The live-connected GUI, populated-settings round trip, moving meters,
  multi-slice throughput, and systemd lifecycle are **not verified** by
  this run. Do not promote these partial observations to full R2 acceptance.

Temporary logs and the supervised launch helper are under
`/tmp/nereus-resume-20260920`. Raw first-run output can contain pairing
credentials; keep these files local and do not attach them to a PR.

## Next milestones

1. Finish R2's populated-settings, UI interaction, and G2E receive-only
   acceptance rows. Recheck multi-slice throughput and connected shutdown.
2. Resolve PR #315's conflicts and carry the shutdown fixes into the branch
   that will be merged. Publish/review R2 as the next development stage.
3. Finish the R3 decisions in addendum section 10.1: master versus per-slice
   audio, shared versus separate FFTs, source-verified Opus settings, and
   codec attribution. Then write its implementation plan.
4. Bring up one remote audio stream and one spectrum/waterfall subscription
   end to end early, then expand to the planned multi-pan budgets and soak
   tests. The previous design explicitly rejected a throwaway raw-media
   protocol; reuse that decision.
5. Install the resulting daemon on the selected SBC and verify the real
   systemd service, local-network client, restart, and reconnect behavior.

## Sources to resume from

- [R2 implementation plan](../architecture/2026-08-03-remote-daemon-r2-plan.md)
- [R2 acceptance procedure and ledger](../architecture/2026-08-03-remote-daemon-r2-verification/README.md)
- [R2/R3 design addendum](../architecture/2026-08-03-remote-daemon-r2-r3-design-addendum.md)
- [Umbrella architecture](../architecture/2026-07-28-remote-daemon-architecture-design.md)
- [Pi R1 bench evidence](../architecture/2026-08-02-remote-daemon-r1-verification/README.md)
- [PR #315 review](https://github.com/boydsoftprez/NereusSDR/pull/315/files)

Historical conversation: Claude session `cce211f4-844a-4816-8f4e-893075723aca`,
August 3-9, 2026. The final handoff and bench discussion are in archived
lines 2480-2551 under the `peaceful-ramanujan-64ff46` conversation archive.
