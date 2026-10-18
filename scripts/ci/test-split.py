#!/usr/bin/env python3
"""Split the Linux tests between the self-hosted CI jobs by measured time.

The two linux-box jobs in .github/workflows/ci.yml each build and run part
of the tests. The name-hash shards (NEREUS_TEST_SHARDS) gave them very
different amounts of work: on main run 39036605879 one part ran 4484
test-seconds in 661 s and the other 2702 in 346 s. This script balances the
parts by the times the jobs measured, so they finish together.

The unit is a test executable, the name given to nereus_add_test(): CMake
registers or skips a whole executable, with every ctest entry it runs (a
split test's <name>_<group> entries, or extra add_test() calls whose
COMMAND is that executable).

  test-split.py times <build-dir>
      After a job's tests: print "<executable> <seconds>" for each test
      executable this build registered and ctest has a time for. The
      seconds are ctest's average time for each entry (the build's
      Testing/Temporary/CTestCostData.txt), times the slots it holds at
      once: its PROCESSORS, or all of them for RUN_SERIAL. Tests labeled
      realtime are left out: these jobs do not run them.

  test-split.py split <parts> <times-file>...
      Before the jobs: read every times file (one per job) and print
      "<executable> <part>" for each executable, longest first into the
      part with the least time so far. An executable with no time (a new
      test) is not listed: CMake gives it its name-hash shard, as before.
      Prints each part's total to stderr.

CI speed round, 2026-10-17.
"""

import json
import os
import re
import subprocess
import sys

# The jobs run 8 tests at once (scripts/ci/box-linux.sh), so a RUN_SERIAL
# test holds all 8 slots for its time.
SLOTS = 8

# A test executable's name, as tests/CMakeLists.txt reads the split.
NAME = re.compile(r"[A-Za-z0-9_]+")


def read_costs(path):
    """ctest's cost file: "<name> <runs> <seconds>" lines, then "---"."""
    costs = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line == "---":
                    break
                fields = line.split()
                if len(fields) != 3:
                    continue
                try:
                    runs, seconds = int(fields[1]), float(fields[2])
                except ValueError:
                    continue
                if runs > 0:
                    costs[fields[0]] = seconds
    except FileNotFoundError:
        pass
    return costs


def times(build_dir):
    listing = subprocess.run(
        ["ctest", "--show-only=json-v1"], cwd=build_dir,
        check=True, capture_output=True, text=True)
    tests = json.loads(listing.stdout).get("tests", [])
    costs = read_costs(os.path.join(build_dir, "Testing", "Temporary",
                                    "CTestCostData.txt"))
    totals = {}
    for test in tests:
        name = test.get("name", "")
        command = test.get("command") or []
        if name not in costs or not command:
            continue
        props = {p.get("name"): p.get("value")
                 for p in test.get("properties", [])}
        if "realtime" in (props.get("LABELS") or []):
            continue
        if props.get("RUN_SERIAL"):
            slots = SLOTS
        else:
            try:
                slots = min(max(int(props.get("PROCESSORS") or 1), 1), SLOTS)
            except (TypeError, ValueError):
                slots = 1
        exe = os.path.basename(command[0])
        # A test executable's name; anything else (cmake -P, a script
        # interpreter) is not a nereus_add_test() name and is registered
        # in every job anyway.
        if not NAME.fullmatch(exe):
            continue
        totals[exe] = totals.get(exe, 0.0) + costs[name] * slots
    for exe in sorted(totals):
        print(f"{exe} {totals[exe]:.3f}")


def split(parts, paths):
    weights = {}
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for line in f:
                fields = line.split()
                if len(fields) != 2 or not NAME.fullmatch(fields[0]):
                    continue
                try:
                    seconds = float(fields[1])
                except ValueError:
                    continue
                # The jobs run disjoint tests; should one executable appear
                # twice, keep the longer time.
                weights[fields[0]] = max(weights.get(fields[0], 0.0), seconds)
    load = [0.0] * parts
    assignment = {}
    for exe in sorted(weights, key=lambda e: (-weights[e], e)):
        part = min(range(parts), key=lambda p: (load[p], p))
        assignment[exe] = part
        load[part] += weights[exe]
    for exe in sorted(assignment):
        print(f"{exe} {assignment[exe]}")
    for part, seconds in enumerate(load):
        print(f"part {part}: {seconds:.0f} test-seconds", file=sys.stderr)


def main(argv):
    if len(argv) == 3 and argv[1] == "times":
        times(argv[2])
        return 0
    if len(argv) >= 3 and argv[1] == "split" and argv[2].isdigit() \
            and int(argv[2]) > 0:
        split(int(argv[2]), argv[3:])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
