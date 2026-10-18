#!/usr/bin/env bash
set -euo pipefail
# Serialize installation and retention outside the directories they prune.
exec 9>/run/lock/nereus-core-deploy.lock
flock -x 9
retention=/var/lib/nereus-build/prune-core-artifacts.py
checkpoint=${1:?signed checkpoint is required}
if [[ ! "$checkpoint" =~ ^[0-9a-f]{8,40}$ ]]; then exit 2; fi
stage=/home/yonder/nereus-core/$checkpoint/stage
previous=${2:?verified previous checkpoint required}
if [[ ! "$previous" =~ ^[0-9a-f]{8,40}$ ]]; then exit 2; fi
backup=/var/lib/nereus-build/rollback-$previous-before-$checkpoint
state=/var/lib/private/nereusd/.config
test -x "$stage/usr/local/bin/nereusd"
test -s "$stage/usr/local/bin/nereusd"
test -s "$stage/usr/local/lib/libNereusCore.so"
test -s "$stage/usr/local/lib/librade.so.0.1"
test -f "$stage/usr/local/lib/libNereusCore.so"
test -f "$stage/usr/local/share/doc/nereussdr/LICENSE"
test -d "$stage/usr/local/share/doc/nereussdr/licenses"
test -s "$stage/usr/lib/systemd/system/nereusd.service"
test -s "$stage/usr/local/share/NereusSDR/models/dfnet3/DeepFilterNet3_onnx.tar.gz"
for model in Default_large.bin Default_small.bin; do
    test -s "$stage/usr/local/share/NereusSDR/models/rnnoise/$model"
done
for f in LICENSE LICENSE-APACHE LICENSE-MIT COMMIT; do
    test -s "$stage/usr/local/share/doc/nereussdr/deepfilter/$f"
done
systemd-analyze verify "$stage/usr/lib/systemd/system/nereusd.service"
# Every new daemon stage ships maintenance with its native bounded logger.
# Older stages can still use the already installed maintenance helper.
shipped_retention="$stage/usr/local/libexec/nereusd/prune-core-artifacts.py"
if test -f "$shipped_retention"; then
    install -D -m 700 "$shipped_retention" /usr/local/libexec/nereusd/prune-core-artifacts.py
    install -m 700 "$shipped_retention" "$retention.incomplete"
    mv "$retention.incomplete" "$retention"
fi
test -f "$retention"
# BEGIN receive buffer limit
# The radio socket asks for a 4 MB receive buffer and Linux grants at most
# net.core.rmem_max, 212992 bytes on the Rock 5C as found (2026-10-10). A
# stage that carries the setting gets it installed where the system reads it
# at every start (packaging/nereusd-receive-buffer.conf, which the .deb
# packages ship too), and applied now, only ever upward, by the helper the
# stage carries. An older stage carries neither, and whatever is installed is
# left as found. Where the installer rolls back, the file an earlier install
# left is put back (it goes into files.tar) and one this install added is
# removed. A limit already raised is never lowered.
sysctl_conf=/usr/lib/sysctl.d/10-nereusd.conf
rmem_max_file=/proc/sys/net/core/rmem_max
sysctl_conf_added=0
sysctl_conf_saved=()
if [[ -e "$sysctl_conf" || -L "$sysctl_conf" ]]; then sysctl_conf_saved=("${sysctl_conf#/}"); fi
install_receive_buffer_limit() {
    local shipped="$1/usr/lib/sysctl.d/10-nereusd.conf"
    local helper="$1/usr/local/libexec/nereusd/raise-receive-buffer-limit"
    if [[ ! -s "$shipped" || ! -s "$helper" ]]; then
        echo "this stage carries no network receive buffer setting; $sysctl_conf left as found"
        return 0
    fi
    if [[ "${#sysctl_conf_saved[@]}" == 0 ]]; then sysctl_conf_added=1; fi
    install -d -m 755 "$(dirname "$sysctl_conf")"
    install -m 644 "$shipped" "$sysctl_conf"
    sh "$helper" "$sysctl_conf" "$rmem_max_file" \
        || echo "WARNING: the network receive buffer limit was not applied now; $sysctl_conf applies it at the next system start" >&2
}
remove_added_receive_buffer_limit() {
    if [[ "$sysctl_conf_added" == 1 ]]; then rm -f "$sysctl_conf" || return 1; fi
}
# END receive buffer limit
test ! -e "$backup"
mkdir -m 700 "$backup"
tar -C / -cpf "$backup/files.tar" usr/local/bin/nereusd usr/local/lib/librade.so usr/local/lib/librade.so.0.1 usr/lib/systemd/system/nereusd.service etc/nereusd.conf ${sysctl_conf_saved[@]+"${sysctl_conf_saved[@]}"}
if test -e /usr/local/lib/libNereusCore.so; then
    cp -p /usr/local/lib/libNereusCore.so "$backup/libNereusCore.so"
fi
# Model assets + licences: older installations may not have them.
asset_present=()
for p in usr/local/share/NereusSDR/models/dfnet3 usr/local/share/NereusSDR/models/rnnoise usr/local/share/doc/nereussdr/deepfilter; do
    if test -e "/$p" || test -L "/$p"; then asset_present+=("$p"); fi
done
printf '%s\n' "${asset_present[@]}" > "$backup/assets-present.txt"
if test "${#asset_present[@]}" -gt 0; then
    tar -C / -cpf "$backup/assets.tar" "${asset_present[@]}"
fi
state_snapshot_ready=0
snapshot_daemon_state() {
    # The previous Core can save settings while stopping. Publish a complete
    # post-stop copy before any new binary can change them.
    cp -a "$state" "$backup/daemon-config.incomplete" || return 1
    mv "$backup/daemon-config.incomplete" "$backup/daemon-config" || return 1
    sync -f "$backup/daemon-config" || return 1
    state_snapshot_ready=1
}
restore_daemon_state() {
    [[ "$state_snapshot_ready" == 1 ]] || return 0
    local restore="$state.restore-$checkpoint"
    local failed="$state.failed-$checkpoint"
    [[ ! -e "$restore" && ! -L "$restore" && ! -e "$failed" && ! -L "$failed" ]] || return 1
    cp -a "$backup/daemon-config" "$restore" || return 1
    local moved=0
    if [[ -e "$state" || -L "$state" ]]; then
        mv "$state" "$failed" || return 1
        moved=1
    fi
    if ! mv "$restore" "$state"; then
        if [[ "$moved" == 1 ]]; then mv "$failed" "$state" || true; fi
        return 1
    fi
    if [[ "$moved" == 1 ]]; then
        if ! mv "$failed" "$backup/daemon-config-failed"; then
            echo "failed upgrade settings retained at $failed"
        fi
    fi
}
# BEGIN group drop-ins
# Sound cards and serial accessories: the unit runs as a DynamicUser account
# with no groups, so drop-ins grant the audio and dialout groups, as the
# station images and the Pi 4 installers do (same content as
# packaging/station-image/common/nereusd-audio.conf and nereusd-serial.conf).
# Without the audio one the Core cannot open a sound card (seen on the Rock
# 5C bench Core, 2026-10-10). A Core that lacks a grant gets it; an existing
# one is left as found. Rollback removes only one this install added.
dropin_dir=/etc/systemd/system/nereusd.service.d
audio_dropin_added=0
serial_dropin_added=0
# One name per grant: <grant>.conf, the name every route writes. The Rock was
# first set up by hand with nereusd-<grant>.conf; either name counts as
# present, so a box never ends up with two files for one grant.
existing_group_dropin() {
    local f
    for f in "$dropin_dir/$1.conf" "$dropin_dir/nereusd-$1.conf"; do
        if [[ -e "$f" || -L "$f" ]]; then printf '%s\n' "$f"; return 0; fi
    done
    return 1
}
add_group_dropins() {
    local found
    if found=$(existing_group_dropin audio); then
        echo "$found already exists; left unchanged"
    else
        audio_dropin_added=1
        install -d -m 755 "$dropin_dir"
        cat > "$dropin_dir/audio.conf" <<'AUDIO'
# Sound cards on a Core: /dev/snd/* is root:audio on Debian, and a DynamicUser
# account has no groups of its own.
[Service]
SupplementaryGroups=audio
AUDIO
        chmod 644 "$dropin_dir/audio.conf"
        echo "added $dropin_dir/audio.conf"
    fi
    if found=$(existing_group_dropin serial); then
        echo "$found already exists; left unchanged"
    else
        serial_dropin_added=1
        install -d -m 755 "$dropin_dir"
        cat > "$dropin_dir/serial.conf" <<'SERIAL'
# Serial accessories on a station image: /dev/ttyUSB* and /dev/ttyACM* are
# root:dialout on Debian, and a DynamicUser account has no groups of its own.
[Service]
SupplementaryGroups=dialout
SERIAL
        chmod 644 "$dropin_dir/serial.conf"
        echo "added $dropin_dir/serial.conf"
    fi
}
remove_added_group_dropins() {
    local removed=0
    if [[ "$audio_dropin_added" == 1 ]]; then rm -f "$dropin_dir/audio.conf" || removed=1; fi
    if [[ "$serial_dropin_added" == 1 ]]; then rm -f "$dropin_dir/serial.conf" || removed=1; fi
    return "$removed"
}
# END group drop-ins
# BEGIN real-time priority drop-in
# Real-time priority for the Core on the Radxa vendor kernel
# (6.1.115-vendor-rk35xx, CONFIG_RT_GROUP_SCHED=y). While the cgroup CPU
# controller is on, that kernel gives a service no real-time budget and
# refuses its SCHED_FIFO requests, and a Core refused at start never asks
# again (measured on the Rock 5C, 2026-10-11). On the Rock only the login
# managers ask for the controller: Debian's user@.service does
# (Delegate=pids memory cpu), so any login switches it on until about ten
# seconds after the last logout. The drop-in takes cpu out of that list for
# every user. The empty Delegate= comes first because a list adds to the one
# before it: "Assigning the empty string will enable delegation, but reset
# the list of controllers" (systemd.resource-control(5), systemd 257;
# config_parse_delegate() in its src/core/load-fragment.c).
# A kernel that does not report CONFIG_RT_GROUP_SCHED=y is left alone, and so
# is an existing file. Rollback removes only one this install added.
realtime_dropin=/etc/systemd/system/user@.service.d/nereusd-realtime-priority.conf
kernel_config_gz=/proc/config.gz
kernel_config_boot=/boot/config-$(uname -r)
cgroup_controllers=/sys/fs/cgroup/cgroup.subtree_control
realtime_dropin_added=0
kernel_has_rt_group_sched() {
    # No grep -q: it stops at the first match, and under pipefail the cut
    # short gzip would then count as "not set".
    if [[ -r "$kernel_config_gz" ]]; then
        gzip -dc "$kernel_config_gz" | grep -x 'CONFIG_RT_GROUP_SCHED=y' > /dev/null
    elif [[ -r "$kernel_config_boot" ]]; then
        grep -x 'CONFIG_RT_GROUP_SCHED=y' "$kernel_config_boot" > /dev/null
    else
        return 1
    fi
}
add_realtime_dropin() {
    if [[ -e "$realtime_dropin" || -L "$realtime_dropin" ]]; then
        echo "$realtime_dropin already exists; left unchanged"
        return 0
    fi
    if ! kernel_has_rt_group_sched; then
        echo "kernel does not report CONFIG_RT_GROUP_SCHED=y; $realtime_dropin not added"
        return 0
    fi
    realtime_dropin_added=1
    install -d -m 755 "${realtime_dropin%/*}"
    cat > "$realtime_dropin" <<'REALTIME'
# Written by the NereusSDR Core installer (scripts/radxa/install-core-selector.sh).
# Login managers no longer ask for the cgroup CPU controller. On a kernel
# built with CONFIG_RT_GROUP_SCHED=y that controller, once on, makes the
# kernel refuse real-time priority to nereusd. The empty line resets the
# list Debian ships (pids memory cpu); the next line puts back the rest.
# Remove this file and run "systemctl daemon-reload" to undo it.
[Service]
Delegate=
Delegate=pids memory
REALTIME
    chmod 644 "$realtime_dropin"
    echo "added $realtime_dropin"
}
remove_added_realtime_dropin() {
    if [[ "$realtime_dropin_added" == 1 ]]; then rm -f "$realtime_dropin" || return 1; fi
}
# Says what the Core is about to start under. It never fails an install.
report_cpu_controller() {
    local delegated controllers
    delegated=$(systemctl show 'user@0.service' -p DelegateControllers --value 2> /dev/null) || delegated=unknown
    controllers=$(cat "$cgroup_controllers" 2> /dev/null) || controllers=unknown
    echo "login managers ask for cgroup controllers: ${delegated:-none}"
    echo "cgroup controllers on as the Core starts: ${controllers:-none}"
    if [[ " $controllers " == *" cpu "* ]] && kernel_has_rt_group_sched; then
        echo "WARNING: the cgroup CPU controller is on as the Core starts. While it stays on, each audio thread of the Core is refused real-time priority when it starts, as a radio or client connects." >&2
        echo "A login from before this change keeps it on until about ten seconds after it ends, or until a reboot for a user who lingers; audio threads that start after that get the priority." >&2
    fi
}
# END real-time priority drop-in
rollback() {
    local result=$?
    if test "$result" -ne 0; then
        set +e
        echo "installation failed ($result); restoring $previous"
        if ! systemctl stop nereusd; then
            echo "rollback stopped: could not stop Core; backup retained at $backup"
            exit "$result"
        fi
        local restored=1
        tar -C / -xpf "$backup/files.tar" || restored=0
        if test -e "$backup/libNereusCore.so"; then
            cp -p "$backup/libNereusCore.so" /usr/local/lib/libNereusCore.so || restored=0
        else
            rm -f /usr/local/lib/libNereusCore.so || restored=0
        fi
        rm -rf /usr/local/share/NereusSDR/models/dfnet3 /usr/local/share/NereusSDR/models/rnnoise /usr/local/share/doc/nereussdr/deepfilter || restored=0
        if test -f "$backup/assets.tar"; then tar -C / -xpf "$backup/assets.tar" || restored=0; fi
        restore_daemon_state || restored=0
        remove_added_realtime_dropin || restored=0
        remove_added_receive_buffer_limit || restored=0
        remove_added_group_dropins || restored=0
        systemctl daemon-reload || restored=0
        if [[ "$restored" == 1 ]]; then
            systemctl start nereusd
        else
            echo "rollback incomplete; Core remains stopped; backup retained at $backup"
        fi
    fi
    exit "$result"
}
sync -f "$backup/files.tar"
if test -e "$backup/libNereusCore.so"; then sync -f "$backup/libNereusCore.so"; fi
if test -f "$backup/assets.tar"; then sync -f "$backup/assets.tar"; fi
trap rollback EXIT
systemctl stop nereusd
test "$(systemctl show nereusd -p ExecMainStatus --value)" = 0
snapshot_daemon_state
install -m 755 "$stage/usr/local/bin/nereusd" /usr/local/bin/nereusd
install -m 644 "$stage/usr/local/lib/libNereusCore.so" /usr/local/lib/libNereusCore.so
install -m 644 "$stage/usr/local/lib/librade.so.0.1" /usr/local/lib/librade.so.0.1
ln -sfn librade.so.0.1 /usr/local/lib/librade.so
install -m 644 "$stage/usr/lib/systemd/system/nereusd.service" /usr/lib/systemd/system/nereusd.service
install -d /usr/local/share/nereusd
install -m 644 "$stage/usr/local/share/nereusd/nereusd.conf.sample" /usr/local/share/nereusd/nereusd.conf.sample
if ldd /usr/local/bin/nereusd | rg -q 'not found'; then exit 1; fi
/usr/local/bin/nereusd --help > "$backup/installed-help.txt"
install -d /usr/local/share/doc/nereussdr
# Includes the DFNR licences (doc/nereussdr/deepfilter/).
cp -a "$stage/usr/local/share/doc/nereussdr/." /usr/local/share/doc/nereussdr/
install -D -m 644 "$stage/usr/local/share/NereusSDR/models/dfnet3/DeepFilterNet3_onnx.tar.gz" /usr/local/share/NereusSDR/models/dfnet3/DeepFilterNet3_onnx.tar.gz
for model in Default_large.bin Default_small.bin; do
    install -D -m 644 "$stage/usr/local/share/NereusSDR/models/rnnoise/$model" "/usr/local/share/NereusSDR/models/rnnoise/$model"
done
sync -f /usr/local/bin/nereusd
sync -f /usr/local/lib/libNereusCore.so
sync -f /usr/local/lib/librade.so.0.1
sync -f /usr/lib/systemd/system/nereusd.service
sync -f /usr/local/share/NereusSDR/models/dfnet3/DeepFilterNet3_onnx.tar.gz
for model in Default_large.bin Default_small.bin; do
    sync -f "/usr/local/share/NereusSDR/models/rnnoise/$model"
done
add_group_dropins
install_receive_buffer_limit "$stage"
add_realtime_dropin
systemctl daemon-reload
report_cpu_controller
systemctl start nereusd
sleep 8
systemctl is-active --quiet nereusd
journalctl -u nereusd -n 120 --no-pager > "$backup/startup.log"
systemctl is-active --quiet nereusd
printf '%s\n' "$checkpoint installed; service active. Live client validation follows separately."
systemctl show nereusd -p Result -p ExecMainStatus -p NRestarts
sha256sum /usr/local/bin/nereusd /usr/local/lib/libNereusCore.so /usr/local/lib/librade.so.0.1 \
    /usr/local/share/NereusSDR/models/dfnet3/DeepFilterNet3_onnx.tar.gz \
    /usr/local/share/NereusSDR/models/rnnoise/Default_large.bin \
    /usr/local/share/NereusSDR/models/rnnoise/Default_small.bin
trap - EXIT
# Never let maintenance failure trigger rollback of a healthy new Core.
# Publish success only after the complete post-stop recovery and startup checks.
if python3 - "$backup" "$checkpoint" "$previous" <<'PY'
import datetime, hashlib, json, os, pathlib, sys
backup = pathlib.Path(sys.argv[1])
proof = {
    "checkpoint": sys.argv[2], "previous": sys.argv[3],
    "backup": backup.name,
    "installed_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "post_stop_config_complete": (backup / "daemon-config").is_dir(),
    "recovery_sha256": {}, "installed_sha256": {},
}
assert proof["post_stop_config_complete"]
for name in ("files.tar", "libNereusCore.so", "assets.tar"):
    path = backup / name
    assert path.is_file() and path.stat().st_size > 0
    with path.open("rb") as source:
        digest = hashlib.sha256()
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    proof["recovery_sha256"][name] = digest.hexdigest()
for name in ("bin/nereusd", "lib/libNereusCore.so", "lib/librade.so.0.1"):
    installed = pathlib.Path("/usr/local") / name
    stage = pathlib.Path("/home/yonder/nereus-core") / sys.argv[2] / "stage/usr/local" / name
    digest = hashlib.sha256(installed.read_bytes()).hexdigest()
    assert digest == hashlib.sha256(stage.read_bytes()).hexdigest(), name
    proof["installed_sha256"][name] = digest
path = backup / "successful-install.json"
tmp = path.with_suffix(".json.incomplete")
with tmp.open("w") as output:
    json.dump(proof, output, indent=2)
    output.write("\n")
    output.flush()
    os.fsync(output.fileno())
os.replace(tmp, path)
fd = os.open(backup, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(fd)
finally:
    os.close(fd)
latest = backup.parent / "current-install.json"
tmp = latest.with_suffix(".json.incomplete")
with tmp.open("w") as output:
    json.dump(proof, output, indent=2)
    output.write("\n")
    output.flush()
    os.fsync(output.fileno())
os.replace(tmp, latest)
fd = os.open(backup.parent, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(fd)
finally:
    os.close(fd)
PY
then
    if ! python3 "$retention" --stage-root /home/yonder/nereus-core \
        --backup-root /var/lib/nereus-build --current "$checkpoint" --previous "$previous" \
        --backup "${backup##*/}" --apply; then
        echo "WARNING: installed Core is healthy; artifact cleanup failed. Recovery retained at $backup" >&2
    fi
else
    echo "WARNING: installed Core is healthy; success manifest failed. Older recovery artifacts retained." >&2
fi
