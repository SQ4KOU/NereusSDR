# Isolated LinuxPipeBus ownership regressions

This standalone project compiles the real `src/core/audio/LinuxPipeBus.cpp`
and its ownership/control helpers against Qt6 Core only. It does not configure the application, link AudioEngine,
open audio devices, or execute the host's `pactl`. The small local definitions
of `lcAudio` and `IAudioBus::nextInstanceId()` provide only otherwise unresolved
link symbols. No production class is replaced.

Do not run the older `tests/tst_linux_pipe_bus.cpp` on a developer machine: its
cleanup unlinks fixed host FIFO paths. This fixture does not use it.

Configure and build only after the lead's fresh resource check authorizes it:

```sh
nice -n 19 cmake -S tests/linux_pipe_ownership -B build-linux-pipe-ownership -G Ninja
nice -n 19 cmake --build build-linux-pipe-ownership --target tst_linux_pipe_ownership -j2
ctest --test-dir build-linux-pipe-ownership --output-on-failure
```

Use a fresh private build directory. Each configure creates a random,
owner-only directory beneath that build directory. Staging replaces the exact
production fixed per-UID ownership-root expression with a path beneath each
case's private `XDG_RUNTIME_DIR`. Configuration fails if the expected expression
is absent. It also replaces legacy literal `/tmp/nereussdr-vax-` and
`/tmp/nereussdr-` prefixes with private build paths. On Apple it replaces
`Q_OS_LINUX` guards with `Q_OS_UNIX` and literal `/proc/` prefixes with an owned
synthetic process tree. Linux keeps its original guards and uses real read-only
process identity. The control adapter's `pulse` library literal becomes the
absolute path of the fixture-only fake library; no system libpulse is loaded.
Other Unix platforms currently retain production's non-Linux refusal. Non-Unix
configure is rejected. `staged/receipt.txt` records both SHA-256 hashes, source,
namespace and transformations. The staged source stays available for review;
there is no production test mode or default override.

Every test case runs in a fresh process, so the original RED run exercised the
baseline process-local cleanup-once behavior. Cases run serially because staged FIFO
paths belong to one build. Per-case JSON state, command transcript and copied
`pactl` executable live in an automatically removed private temporary directory.
`PATH` contains only that directory; the stub uses an absolute Python
interpreter selected by CMake. It accepts a narrow command set, validates every
module file lies inside the configure-created private root and is a real FIFO,
and never starts a subprocess or connects to an audio server. Unknown commands
fail closed. Its JSON state lock also lives inside that private root.

The fake libpulse shares that state lock and never connects to a server. It
exports only the ABI required by the real `LinuxPipeControl` adapter. A context
retains its original server epoch; changing the fake server epoch makes that
context fail without reconnecting. Normal query/unload callbacks complete
synchronously. Negative-EOL, incomplete-query and finite-timeout controls drive
the real adapter's failure paths. To model an RX module's FIFO reader, the fake
retains a real owned FIFO descriptor in the context; it never consumes bytes.
The all-roles case uses a separate reader to check real producer `push()` bytes.
The mainloop fake rejects infinite waits and waits at most the adapter's ten
millisecond poll slice. The one-second query-timeout case uses the production
deadline, with no test seam or timing-default override.

The regression cases assert real bus results, real FIFO device/inode identity,
payload transport through `pull()`, and module identity at the external command
boundary. `baseline` should pass on exact d12. `unrelated-live-rx` should fail
because opening TX deletes preseeded unrelated module 37, whose RX FIFO has a
live reader. `same-role-object` should fail because a second TX object is
accepted and replaces the first FIFO. `same-role-process` adds a fresh process
and should also fail, including the broad module cleanup. These are expected
RED outcomes, not a completed production fix. After lead authorization on
2026-10-14, the isolated configure/build succeeded and one initial run verified
the baseline PASS plus all three expected RED failures (1.47 seconds total).
The transcript shows the unrelated module 37 being unloaded, the same-process
contender replacing the FIFO, and the fresh-process contender unloading the
first TX module and replacing its FIFO. No unexpected fixture errors occurred.
Logs are `configure.log`, `build.log`, and `initial-red.log` in the owned build
directory. No production implementation was changed for that run.

The fake server supports controlled later additions through JSON fields:
`next_index: 0`, `load_result: "empty"`, `"malformed"`, or `"failure"`,
`list_failure: true`, `unload_failure: true`, and `replace_fifo_on_load: true`.
Empty/malformed successful loads retain their module in state, modeling the
important difference between an ambiguous response and no module creation.
The extended fixture covers unknown same-role refusal, module zero,
failed/empty/malformed load cleanup, unload failure, replacement before close
and during load, confirmed-child crash recovery, normal reopen, query failures,
all five simultaneous roles, different-role processes, server-epoch restart,
a pending load whose exact module arrives later, a crash after generation
resources were removed but before the receipt was cleared, and a missing
private `pactl` executable followed by restoration and retry. The nonzero-exit
load case deliberately retains its unresolved generation because the current
command helper conflates launched-command failure and an ambiguous empty reply;
the definite FailedToStart case proves that no server request was launched and
therefore permits retry. Retired unload failure is retried only after control
returns, using the production persisted proof that the old bus has closed.

After fresh resource checks and bounded `nice -n 19`, `-j2` standalone builds,
all **23 cases passed in 8.20 seconds** on the final 2026-10-14 run. The earlier
22-case production checkpoint also passed in 7.19 seconds. Logs are
`checkpoint-{configure,build,tests}.log`, `missing-executable-{build,test}.log`
and `final-tests.log` in the owned build directory. One accidental targeted run
began before its rebuild finished and exited without test output; it is
preserved separately as `invalid-overlapping-build-test.log` and is not valid
production evidence. The run after confirmed build completion passed.
Two additional timeout cases exercise the production deadlines without changing
them. The private `pactl` child blocks in an owned FIFO open with no writer,
without sleeping or creating a module. After the five-second command deadline,
the test checks the logged child PID with `waitpid(WNOHANG)` and requires
`ECHILD`, confirming that QProcess terminated and reaped its own child. Pending
FIFO evidence remains and a contender refuses the unresolved role. The current
production helper emits Qt's expected `QProcess: Destroyed while process ... is
still running` diagnostic when its destructor performs that termination.

The unload-timeout case holds the fake libpulse operation in RUNNING until the
real one-second deadline and cancellation. It verifies that the closed bus
preserves its module, FIFO identity and durable receipt, then restores control
and checks that positive retirement evidence permits recovery and successor
transport. Both targeted cases passed in 7.03 seconds; their complete
transcripts are in `timeouts-targeted.log`, with the bounded rebuild in
`timeouts-build.log`. The final **25-case run passed in 10.78 seconds**;
`final-25-tests.log` preserves that result, and the earlier valid 23-case run
remains in `final-tests.log`. Every fixture test carries the `core` CTest label.
After the final mechanical private-field naming correction, a fresh bounded
reconfigure/build and one 25-case run also passed in **10.91 seconds** at
18:40–18:41 local time on 2026-10-14. The current source/hash receipt is
`staged/receipt.txt`; matching logs are `final-reviewed-configure.log`,
`final-reviewed-build.log` and `final-reviewed-25-tests.log`. The prior 25-case
receipt remains in `prior-final-25-receipt.txt`.

This fixture cannot prove server-side generation identity. A `pactl list` check
followed by numeric `unload-module` is not an atomic compare-and-swap, and an
arbitrary third party can reuse an index between those commands. Nor does this
fake model actual PulseAudio/PipeWire acceptance of duplicate names, restart
semantics, Linux security policy, or real-time scheduling. On Apple, process
identity is synthetic: after the crash worker has actually exited, its parent
explicitly removes only that worker's private synthetic stat file. This does
not validate native `/proc` behavior or PID-reuse races. The staging maps the
fixed production claim root to per-case XDG directories, so these runs do not
test independence from different production XDG environments. The initial fake
allows a duplicate load deliberately: safe role admission must refuse before
the first owner's FIFO is touched, regardless of what a server later accepts.
The preserved initial RED source and receipt are in `initial-red-staged/` and
`initial-red-receipt.txt` beneath the owned build directory.
