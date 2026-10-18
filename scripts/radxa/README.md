# Radxa deployment retention

These are the maintained deployment helpers. Copy `install-core-selector.sh`
and `prune-core-artifacts.py` into `/var/lib/nereus-build/` on Radxa. Copy
`deploy-stage-to-rock.sh` into the local Rock Docker kit, beside its existing
`out/` directory. The stage script checks and publishes artifacts; it never
installs them or restarts the Core.

Beside the unit, the installer makes sure two drop-ins exist in
`/etc/systemd/system/nereusd.service.d/`, because the unit runs as a
`DynamicUser` account with no groups of its own: `audio.conf` adds the `audio`
group so the Core can open sound cards, and `serial.conf` adds the `dialout`
group so it can open serial accessories. They have the same content as the
station images' `packaging/station-image/common/nereusd-audio.conf` and
`nereusd-serial.conf`, and the Pi 4 installers write the same two files. The
Rock was first set up by hand with `nereusd-audio.conf` and
`nereusd-serial.conf`; either name counts as present and is left as found, so
a box never has two files for one grant. A failed installation removes only
the drop-ins it added. Without the audio grant the Core speaker cannot open
(found on the Rock 5C bench Core on 2026-10-10). For a silent headphone jack
with the card open, and for refused real-time priority on the Radxa vendor
kernel, see "Sound from the Core's own sound card" in the top-level
`README.md`.

The installer also changes a system setting, when the stage carries it (this is one of two changes it makes; the other is described below):
it installs `/usr/lib/sysctl.d/10-nereusd.conf`, which raises the network
receive buffer limit (`net.core.rmem_max`) to the 4 MB (4194304 bytes) the
Core asks for on the socket its radio sends to, and before the Core starts
it runs the stage's `raise-receive-buffer-limit`, which applies the limit at
once, only ever upward, and prints what it did. The Rock's limit was 212992
bytes as found, which three or four wide receivers fill in 6 to 9 ms
(measured 2026-10-10). An older stage carries neither file, and the box is
then left as found. A failed installation puts back the file that was there
before, or removes one it added; a limit it raised stays raised until the
next system start. What the setting is and how to undo it: "The system
setting the Core's installer changes" in the top-level `README.md`.

On a kernel built with `CONFIG_RT_GROUP_SCHED=y` (the Radxa vendor kernel
`6.1.115-vendor-rk35xx` is one) the installer also writes
`/etc/systemd/system/user@.service.d/nereusd-realtime-priority.conf`. While
the cgroup CPU controller is on, that kernel refuses real-time priority to the
Core, and Debian's login managers (`user@.service`, `Delegate=pids memory
cpu`) switch it on at any login and leave it on until about ten seconds after
the last logout (until a reboot for a user who lingers). The drop-in takes
`cpu` out of that list for every user on the box, so user services there can
no longer be given CPU weights or quotas. It applies to login managers started
after it is written. The installer prints which controllers are on as the Core
starts and warns when `cpu` still is. The Core's audio threads ask for
real-time priority when they start, as a radio or client connects, so those
that start after the earlier logins have ended (or after a reboot, for a user
who lingers) get it. An existing file is left as found, a kernel that does not
report the setting gets no file, and a failed installation removes only a file
it added. To undo it, remove the file and run `systemctl daemon-reload`.

All three deployment operations use `/run/lock/nereus-core-deploy.lock`.
The pruning helper expects its caller to hold this lock.

After a healthy installation, the installer publishes a durable receipt in
the new rollback directory and `/var/lib/nereus-build/current-install.json`.
It verifies the installed binaries against the new stage before pruning.
It retains:

- The running stage and its previous stage.
- The complete rollback for that successful installation.

Publishing a verified new candidate also retains that candidate, replacing
older unused candidates. Uploads, builds, incomplete recovery, symlinks, and
unknown directory names are never silently removed. Failed installation does
not invoke pruning. Maintenance failure after successful installation warns
and preserves the healthy running Core; it cannot trigger rollback.

One-time age cleanup can preserve newer artifacts while removing only older
ones. Preview first, then use the same command with `--apply`, while holding
the shared deployment lock. Example for October 1, 2026 at midnight Chicago:

```sh
flock /run/lock/nereus-core-deploy.lock python3 \
  /var/lib/nereus-build/prune-core-artifacts.py \
  --stage-root /home/yonder/nereus-core --backup-root /var/lib/nereus-build \
  --current bc8b65dc --previous 6b340562 \
  --backup rollback-6b340562-before-bc8b65dc \
  --before-epoch 1790830800
```

The age test is strictly earlier than the cutoff. Protection takes precedence
over age. The helper defaults to a JSON preview and only deletes with
`--apply`.

Closed Unix sockets copied into configuration snapshots can be removed.
On Linux the helper checks kernel socket filesystem identities, open files,
and mapped libraries before deleting candidates. It fails safely if it cannot
inspect all live processes or their network namespaces.

Run its temporary-filesystem tests without a radio or device:

```sh
python3 scripts/radxa/test_prune_core_artifacts.py
python3 scripts/radxa/test_deploy_retention.py
python3 scripts/radxa/test_install_group_dropins.py
python3 -m pytest tests/release/test_core_receive_buffer_limit.py -q
python3 scripts/radxa/test_install_realtime_dropin.py
```

The daemon stage now carries the shared maintenance helper, which the installer
publishes automatically. Native Core logging also rotates while the Core runs:
five files per profile, each at most 32 MiB, with the newest diagnostics retained.
The background log writer performs rotation; logging producers never do disk I/O.
Existing oversized closed logs are reduced to recent text at startup. External
`copytruncate` is neither required nor configured.
