#!/bin/sh
# NereusSDR for iOS: boots a simulator and waits for the Mac to go quiet after its first boot
# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-NereusSDR-AppStore-permission
#
# Usage: ios/scripts/sim-settle.sh <simulator name>
#
# Run between xcodebuild's build-for-testing and test-without-building where
# the simulator has never booted, as on a hosted CI runner. A simulator's
# first boot goes on long after `simctl bootstatus -b` returns: iOS sets up
# its wallpapers and widgets and builds Spotlight's index, a few hundred
# processes runnable at once. On GitHub's hosted Mac (3 cores, 7 GB;
# measured 2026-10-11) that held the one-minute load average over 30, with
# peaks near 600, for 12 minutes on one runner and for more than 16 on
# another. The app's tests run through it took three to five times as long,
# and waits in them ran out of time. A build beside it took three times as
# long too, so the tests are built before this boots anything.
#
# So this:
#
#   1. boots the simulator named <simulator name> on the newest iOS the Mac
#      has (the one xcodebuild takes for that name; it must already exist)
#      and loops until simctl lists it as Booted and `simctl bootstatus -b`
#      exits 0,
#   2. reads the one-minute load average every 10 seconds until three
#      readings in a row are under SIM_SETTLE_LOAD (default: eight times the
#      number of cores, well over a booted simulator at rest and well under
#      its first boot).
#
# Step 2 ends after SIM_SETTLE_TIMEOUT seconds (default 1500) whatever the
# load, says so, and still exits 0: the tests then run as they would have
# without this. Exit 1 only when no such simulator exists or it has not
# booted in that time.

set -u

if [ $# -ne 1 ]; then
    echo "usage: $0 <simulator name>" >&2
    exit 2
fi

name=$1
timeout=${SIM_SETTLE_TIMEOUT:-1500}
limit=${SIM_SETTLE_LOAD:-$(( $(sysctl -n hw.ncpu) * 8 ))}
started=$(date +%s)

say() {
    echo "sim-settle: $*"
}

elapsed() {
    echo $(( $(date +%s) - started ))
}

# The simulator's UDID and state, from simctl's JSON: "<udid> <state>". Of
# several with the name, the one on the newest iOS.
device_line() {
    xcrun simctl list devices -j | python3 -c '
import json, re, sys
name = sys.argv[1]
found = []
for runtime, devices in json.load(sys.stdin)["devices"].items():
    version = re.search(r"\.iOS-([0-9-]+)$", runtime)
    if version is None:
        continue
    for device in devices:
        if device.get("name") == name and device.get("isAvailable", True):
            found.append(([int(part) for part in version.group(1).split("-")],
                          device["udid"], device["state"]))
if not found:
    sys.exit(1)
newest = max(found)
print(newest[1], newest[2])
' "$name"
}

line=$(device_line) || {
    echo "$0: no available iOS simulator is named \"$name\"" >&2
    exit 1
}
udid=${line%% *}
say "$name is $udid"

round=0
while :; do
    round=$((round + 1))
    state=$(device_line | awk '{print $2}')
    if [ "$state" != "Booted" ]; then
        say "round $round: state $state, booting"
        xcrun simctl boot "$udid" 2>/dev/null
    fi
    xcrun simctl bootstatus "$udid" -b >/dev/null 2>&1
    status=$?
    state=$(device_line | awk '{print $2}')
    if [ "$status" -eq 0 ] && [ "$state" = "Booted" ]; then
        say "round $round: booted after $(elapsed) s"
        break
    fi
    say "round $round: bootstatus exit $status, state $state; waiting"
    if [ "$(elapsed)" -ge "$timeout" ]; then
        echo "$0: $name did not finish booting within ${timeout}s" >&2
        exit 1
    fi
    sleep 5
done

started=$(date +%s)
calm=0
reading=0
while :; do
    load=$(sysctl -n vm.loadavg | tr -d '{}' | awk '{print $1}')
    if awk -v load="$load" -v limit="$limit" 'BEGIN { exit !(load < limit) }'; then
        calm=$((calm + 1))
    else
        calm=0
    fi
    if [ "$calm" -ge 3 ]; then
        say "quiet after $(elapsed) s: load $load, under $limit three readings in a row"
        exit 0
    fi
    if [ "$(elapsed)" -ge "$timeout" ]; then
        break
    fi
    # One line a minute, so the log shows the load coming down.
    if [ $((reading % 6)) -eq 0 ]; then
        say "$(elapsed) s: load $load, waiting for under $limit"
    fi
    reading=$((reading + 1))
    sleep 10
done

waited=$(elapsed)
say "still busy after $waited s: load $load, not under $limit; carrying on"
if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
    echo "::warning title=The simulator did not settle::After $waited s the load was still $load (waiting for under $limit), so what follows runs on a busy machine."
fi
exit 0
