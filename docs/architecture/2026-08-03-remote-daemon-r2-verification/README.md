# Remote-daemon R2 verification

Design: `docs/architecture/2026-07-28-remote-daemon-architecture-design.md`
Addendum: `docs/architecture/2026-08-03-remote-daemon-r2-r3-design-addendum.md`
Plan: `docs/architecture/2026-08-03-remote-daemon-r2-plan.md`

**Status: seeded by Task 5, not yet a matrix.** Task 16 ("Local direct mode
regression gate") is where the full row-by-row verification matrix for R2
belongs, including the bench rows this gate still needs (a live two-process
run this task cannot perform -- see plan Task 16 steps 3-4 and 8). This file
exists ahead of that task only because Task 5 step 5a requires recording one
specific paragraph here now, while the context for it is fresh. Do not treat
the section below as the matrix, and do not remove this status note until
Task 16 actually populates one.

---

## Task 5: the spot-collector gate is a safety measure, not an ownership decision

Task 5 gates `RadioModel::restoreSpotClientAutoStartState()`
(`src/models/RadioModel.cpp`, declared `RadioModel.h:1118`) on
`Role::Local`. Today `nereusd` never calls this method at all (see
`docs/architecture/2026-08-03-remote-daemon-r2-open-issues.md` issue draft
1), so the gate has no observable effect yet. It exists now, ahead of that
defect being fixed, to stop duplicate cluster logins and duplicate PSK
Reporter uploads under one callsign once a daemon-side caller exists:
**which side owns each collector is an open question R2 does not settle.**
Read as an ownership decision, gating all seven sources to the client puts
WSJT-X on the wrong machine.

The measured starting position, derived 2026-08-03:

- `WsjtxClient` binds and **listens** on UDP 2237
  (`src/core/WsjtxClient.h:103` the socket member, `:107` the default port
  field `m_port{2237}`), so its decoder belongs wherever the operator is,
  which is the **client**.
- `PskReporterClient`, `FreeDVReporterClient` and
  `FreeDVRadeReporterBridge` **upload under the operator's callsign** a
  claim about one physical receiver on a frequency only the station knows,
  so they belong to the **station**.
- `DxClusterClient` (also the RBN instance), `PotaClient` and
  `SpotCollectorClient` are read-only downloads whose only consumer is a
  GUI panel and whose login, filters and callsign are per-operator, so they
  are **arguably client**.

R2 turns all seven off on the client (via this gate) because it ships no
spot delivery over the link, so nothing is lost by doing so. **Do not
extend this gate into a delivery path** -- that is a design decision for
whichever task eventually resolves the ownership question above, informed
by a real daemon-side caller existing (open issue 1) and by an operator
actually running both a client and a station and finding out which
placement is wrong in practice.

Cross-reference: this paragraph is the client-side half of design addendum
risk 9 (section 11, item 9); the daemon-side half is open issue draft 1 in
`docs/architecture/2026-08-03-remote-daemon-r2-open-issues.md`.

### Automated coverage

`tests/tst_remote_role_inert.cpp::remoteRestoreSpotClientAutoStartStateStaysSilent`
seeds `DxClusterAutoConnect` / `RbnAutoConnect` / `WsjtxAutoStart` True
against loopback hosts / a local UDP port (never a real remote host --
POTA's real-network `startPolling()` call is deliberately not exercised;
see that test's header comment) and asserts the Local arm still attempts
each start while the Remote arm produces silence on all three. This is
regression coverage for the gate, not a substitute for a live two-process
bench run; Task 16 owns writing and running that row.
