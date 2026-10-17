# Linux VAX ownership

The pactl VAX backend retains the five audio device names (`nereussdr-vax-1`
through `nereussdr-vax-4`, and `nereussdr-vax-tx`), their descriptions, 48 kHz
stereo float32 format, TX pipe size and callback routing. FIFO paths are private
implementation details for each generation rather than fixed shared endpoints.
JJ approved this ownership and path compatibility change on 2026-10-14.

## Admission and lifetime

Every bus first acquires an exclusive nonblocking `flock` on its role's permanent
regular claim file beneath `/tmp/nereussdr-vax-<effective-uid>/`. The directory
must be owned by that uid with no group/other permissions; symlink, type,
permission, link-count and inode mismatches fail closed. Its location is shared
by GUI and service processes even when their XDG environment differs. Independent
opens conflict within one process as well as between processes. Claim descriptors
are close-on-exec and remain held through every load, error and close operation.
Claim files are never unlinked or replaced.

A contended or unknown role fails through the existing bus-open error/null-slot
path. Other roles can still open independently. Profile-level station admission
continues to serve its existing purpose; it does not own this audio namespace.
This adds no application singleton, audio callback lock, thread or DSP change.

Each admitted generation has a fresh private directory and `audio.pipe`. The
server creates the FIFO: [PulseAudio v17.0 pipe-source, lines 239–253](https://github.com/pulseaudio/pulseaudio/blob/v17.0/src/modules/module-pipe-source.c#L239-L253) rejects an already existing
FIFO, so the client must not precreate it. The [pipe-sink implementation](https://github.com/pulseaudio/pulseaudio/blob/v17.0/src/modules/module-pipe-sink.c) also supports server creation. The returned module index may be zero.
The client verifies exact module type, logical role, private FIFO path and format,
then captures FIFO device/inode identity, restricts its permissions, and verifies
the opened descriptor. `pipePath()` reports the current generation and is valid
until the next open or object destruction.

## Evidence and recovery

The role's separate JSON receipt is written completely to a private exclusive
file, synced, atomically renamed, then its directory is synced. It binds the
permanent claim inode, uid, role, generation directory inode, boot ID, PID,
process start time and PID namespace. A completed FIFO capture adds its inode.
The committed intent precedes any module load. An interrupted replacement leaves
the previous committed receipt; temporary transaction files are not evidence
that a module is stale.

An available lock alone never proves a previous owner dead. Automatic crash
recovery also validates the receipt and establishes a different boot, an absent
recorded process, or PID reuse from a different start time within the same PID
namespace. Missing or unreadable evidence is unknown. A normal close first closes
the transport descriptor. If cleanup fails, it persists a retired-generation
marker before releasing its claim. That marker is positive evidence that this
bus has stopped using the generation and permits a later safe retry even while
its process survives. If retirement cannot be committed, the stricter process
identity check remains necessary.

Recovery unloads only a module matching that proven generation. No prefix scan,
UID-only test, age, or numeric index alone authorizes cleanup. Unknown legacy
modules and their fixed FIFO paths remain untouched. The owned directory and
FIFO must still match their recorded identities before destructive work; a
replacement file is preserved. A crash after directory removal but before receipt
removal can finish only after proving the generation has no module and no pending
load. Reconfiguration and reopen acquire a fresh transaction.

A load has a durable pending marker. Definite failure to start `pactl` clears that
marker. Timeout, crash, nonzero exit or lost/malformed output may mean the server
acted. Observing the unique generation module resolves that ambiguity and allows
owned cleanup. An empty list alone does not fence a request on another connection.
If no exact module can be observed, its intent remains preserved and the role
refuses takeover. This can require explicit recovery after a crash between intent
and command launch, or server loss before load completion was observed. The code
does not guess that an unresolved generation is stale.

## Server control and limits

`LinuxPipeControl` dynamically resolves the existing `libpulse.so.0` runtime using
Qt's `QLibrary`; development headers and a new link dependency are unnecessary.
Missing runtime symbols fail only the bus open. Its plain mainloop runs bounded
main-thread control work, with no autospawn, new worker or stream. It retains one
non-reconnecting context for module inspection and unloading. Server loss fails
closed, so an old index cannot silently address a replacement server. Each query
and unload has a one-second bound; operation callbacks are cancelled before their
stack storage expires. Existing pactl load timeout behavior remains bounded.

The guarantee covers cooperating Nereus buses and normal server restart. The
Pulse protocol unloads by numeric index and has no conditional generation compare.
PipeWire can reuse a freed index within one server. Arbitrary administrator/client
unload and replacement between inspection and unload therefore cannot be made
atomic by this API. An old unfixed Nereus binary also does not honor the claim.
Neither case is represented as solved by this change.

## Reference and verification

The existing port came from AetherSDR. Current reference
`1e0718adf54b035f8f3505eaa579f253aaad76ca` still performs prefix cleanup; its
same-user FIFO checks do not prove a process dead. This ownership transaction is
NereusSDR-original and deliberately differs. Existing port notices and comments
remain, with historical comments labelled at their original locations.

Primary API declarations are selected from PulseAudio v17.0, with original
notices and comments retained in `PulseControlAbi.h`; see
[provenance](../attribution/PULSEAUDIO-PROVENANCE.md). Process identity and file
semantics follow [flock(2)](https://man7.org/linux/man-pages/man2/flock.2.html),
[open(2)](https://man7.org/linux/man-pages/man2/open.2.html), and
[proc_pid_stat(5)](https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html).

The standalone device-free regression lives in `tests/linux_pipe_ownership`.
It compiles staged production sources, replacing only namespace/platform/runtime
library locations, and records source/generated hashes. It uses real private
FIFOs and role locks with a command stub and fake libpulse. Apple runs use synthetic
private proc evidence; they do not prove native Linux proc behavior or actual
PulseAudio/PipeWire server compatibility. No live server/device validation is
claimed. Do not run the historical `tst_linux_pipe_bus` on a live host: its setup
and teardown unlink all fixed host FIFO paths.
