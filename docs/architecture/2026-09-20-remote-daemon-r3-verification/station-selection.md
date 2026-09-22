# Core/radio selection — R-R3-38

September 22, 2026. Implements the promoted R3 task 4g without changing the
saved R5-before-R4 roadmap. See the [design](../2026-09-22-station-selection-design.md)
and [implementation plan](../2026-09-22-station-selection-plan.md).

## Implemented behavior

Connections groups local radios, LAN Core announcements and saved stations.
Local radios use the desktop's embedded Core/DSP. Saved stations retain separate
address, token and certificate-pin tuples in operator-local settings. Existing
single-station settings migrate once; explicit local selection survives restart.
Discovery can supply an address for a saved identity, but cannot change its pin,
grant trust or overwrite its saved address. New Cores require connection setup.

Explicit Connect switches the whole operating window/model/settings proxy after
retiring the previous session. Highlighting a row, editing or cancelling does
not switch. Disconnect and Cancel retry always address the current session.
Reconnecting the same selected endpoint preserves its existing session model.
Core connectivity and radio availability are distinct, and LAN/cached identity
remains labelled until authenticated. The Core still owns one configured radio;
the picker does not reassign that radio.

The daemon advertises its actual listening address/port and Core/radio identity
through bounded IPv4/IPv6 multicast announcements. Advertisements contain no
token. Invalid packets, oversized input, stale endpoints and old queued drains
are rejected or retired. Manual addresses remain available without multicast.

## Software verification

| Boundary | Evidence |
| --- | --- |
| Target persistence, one-time migration, CLI tuple isolation, settings scope | Three focused targets passed |
| Whole-window lifetime, retry retirement, local/remote proxy ownership | Real session tests passed; AddressSanitizer regression covers retry-toast destruction |
| Actual Core A→B, local→remote→local, edit/cancel, retry cancellation | Controller tests with two loopback Cores passed |
| LAN trust | Actual TLS server tests accept the saved pin, reject a spoofed identity before token authentication, and leave unknown credentials blank |
| Codec/cache/socket/daemon | Bounded parsing, expiry, UDP ingestion and actual listener close/relisten tests passed |
| Widget presentation | Three rendered QWidget fixtures inspected; actions visible, no overlap, token masked. Illustrative data, not hardware captures |
| Integrated AddressSanitizer | Coordinator, controller and TLS selection: 3/3 targets passed, 50.48 s before final review corrections |
| Consolidated source review | Five findings corrected; no remaining blocking source finding. Includes interface recovery, credential-free errors, applicable connection actions and config identity validation |
| Matching GUI/Core/all-tests build and unfiltered suite | Passed: 738/738 CTest executables, 245.15 s |
| Signed checkpoint and matching installed build | Pending |

The session test exposed and corrected a toast-destruction use-after-free:
QWidget child destruction called back into a MainWindow member list after that
list was destroyed. Retirement now destroys those toasts while their owner state
is alive. A separate guard prevents whole-session replacement inside synchronous
WDSP initialization, whose nested event loop could otherwise retire the current
local model before its connection call returned.

The first full run found two failures: the existing TLS assertion expected the
word “fingerprint” (retained without either pin), and a restart/persistence test
used the shared default test settings file. That persistence test passed alone;
a process-specific profile now prevents parallel tests overwriting its saved
restart data. The matching rebuild and corrected full run pass without exclusions.

Load averages were 2.97 / 5.15 / 3.88 before the final build,
9.49 / 6.47 / 4.40 before CTest, and 6.36 / 6.50 / 4.92 afterward.
Twelve existing inner Qt cases skip: two unavailable device/UI alternatives,
one optional screenshot, one modal-menu timing case, two obsolete RADE TX-route
cases and six WDSP TX harness cases. These are coverage gaps, not passed TX
behavior. No executable timed out or was excluded.

## Native acceptance still open

- Actual IPv4 and IPv6 multicast reception, interface/address changes and
  discovery expiry on a real LAN. Loopback UDP is not proof of multicast reachability.
- Native renderer/audio retirement while switching between a local radio and
  the Rock/Saturn station; second real Core if available. Offscreen/ASAN tests
  do not prove native GPU or physical-radio ownership behavior.
- Operator use of title/menu/Connect, add/edit/forget, disconnect/reconnect,
  restart selection and separate Core-online/radio-offline presentation.

During deployment preparation the Rock at `.106` was reachable, but systemd
reported Core inactive/masked: `/usr/lib/systemd/system/nereusd.service` and
the previous `55e7d49f` staging copy were empty. The installed executable,
Core library and RADE library were also empty, as was that installation's
rollback archive. The generated service template
and older staging copy were nonempty. The cause of the empty files is not yet
established. Installation preparation now checks a nonempty unit with
`systemd-analyze verify` before stopping the previous service and flushes the
installed binary, libraries and service file before startup. The retained `3402d171` executable/Core/RADE hashes matched the earlier
successful installation log. Those files and its verified unit were restored
with the current configuration/credentials preserved and backed up. systemd
reports active and enabled again. Fresh boot and matching selector installation
acceptance remain pending; recovery is not an application reconnect pass.
