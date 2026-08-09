#!/usr/bin/env python3
"""Two checks over src/gui/'s reach into the local DSP engine.

CHECK 1 -- rxChannel() is BANNED in src/gui/.

Phase 3F Sub-Epic J. The per-slice pipeline is SliceModel property ->
RadioModel push -> rxChannel(slice->sliceIndex()). Controls that call
rxChannel() from the GUI bypass it and, historically, hardcode channel 0,
which is how ANF on slice B ended up toggling slice A.

Allowlist entries are files whose rxChannel() use is engine-internal rather
than a control write.

CHECK 2 -- rxChannelForSlice() is INVENTORIED in src/gui/, not banned.

Remote-daemon R2 Task 20, fix round 2. This is a different problem with a
deliberately different answer, which is why it is a second check rather
than a second pattern bolted onto the first.

rxChannelForSlice() is the per-slice-correct wrapper the tree is actively
migrating reach-through onto (see setup/DspOptionsPage.cpp, which carries a
comment recording exactly that migration). In LOCAL direct mode these calls
are legitimate and correct: banning them would undo Sub-Epic J's own fix.

The hazard is remote-station mode (R2). A Role::Remote RadioModel holds a
WdspEngine with no channels, so rxChannelForSlice() returns nullptr, every
call site's `if (RxChannel* ch = ...)` guard swallows it, and the control
silently does nothing. RadioModel's local-DSP hand-out audit -- which is
what disables the Setup pages that reach for this process's DSP -- does NOT
count this wrapper, deliberately: two of its call sites run at page
construction time, so counting it would disable Setup > DSP > Options and
Setup > DSP > MNF outright, and both pages exist mainly to edit
Station-scoped settings that must round-trip. That reasoning is written out
at src/models/RadioModel.h's "KNOWN BLIND SPOT" block.

So the accessor is silent on remote and uncounted by the audit, and a new
Setup page that reaches DSP through it comes up ENABLED and dead with
nothing in the tree noticing. A ban is the wrong answer (the calls are
legitimate); an inventory is the right one. Adding a call in a file not
listed below fails, and so does changing the count in a file that is
listed. Either way the person making the change has to come here and say
what they are doing, which is the whole point.

If you are adding a call: add or bump the entry AND check whether the
surface you are adding it to is reachable in remote mode. If it is a Setup
page, it will be enabled and dead there. If you are removing one: drop the
count, and drop the entry entirely when it reaches zero.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GUI = ROOT / "src" / "gui"
PATTERN = re.compile(r"rxChannel\s*\(")
ALLOWLIST = {
    # Meter driver: resolves per-slice channels for polling, not a control
    # write. Lives under src/gui/ for packaging reasons.
    "src/gui/meters/MeterPoller.cpp",
}

# Check 2's pattern. Distinct from PATTERN on purpose: PATTERN does not
# match this spelling at all (the character after "rxChannel" is "F", not
# whitespace or an open paren), which is exactly why this wrapper slipped
# past check 1 and needed its own.
FOR_SLICE_PATTERN = re.compile(r"rxChannelForSlice\s*\(")

# Repo-relative path -> number of rxChannelForSlice() CALLS in it. Comment
# lines mentioning the name are not calls and are not counted (see
# _is_comment_line). Verified against the tree on 2026-08-08.
FOR_SLICE_INVENTORY = {
    # Applet and flag wiring: three DSP-parameter pushes plus the two
    # filter-characteristics bindings.
    "src/gui/MainWindow.cpp": 5,
    # Setup > DSP > Options. Runs at page-construction time, which is why
    # the audit cannot count this wrapper -- see the module docstring.
    "src/gui/setup/DspOptionsPage.cpp": 1,
    # Setup > DSP > MNF's minimum-notch-width readout.
    "src/gui/setup/DspSetupPages.cpp": 1,
}


def _is_comment_line(stripped: str) -> bool:
    return stripped.startswith("//") or stripped.startswith("*")


def check_rx_channel_ban() -> int:
    """Check 1: no direct rxChannel() calls in src/gui/."""
    failures = []
    for path in sorted(GUI.rglob("*.cpp")):
        rel = path.relative_to(ROOT).as_posix()
        if rel in ALLOWLIST:
            continue
        for num, line in enumerate(path.read_text().splitlines(), 1):
            stripped = line.strip()
            if _is_comment_line(stripped):
                continue
            if PATTERN.search(line):
                failures.append(f"{rel}:{num}: {stripped}")
    if failures:
        print("[gui-dsp-access] GUI code must not call rxChannel() directly.")
        print("Route through SliceModel; RadioModel pushes to the right channel.")
        for f in failures:
            print(f"  {f}")
        return 1
    print(f"[gui-dsp-access] OK: no direct rxChannel() use in src/gui/")
    return 0


def check_rx_channel_for_slice_inventory() -> int:
    """Check 2: rxChannelForSlice() call sites match FOR_SLICE_INVENTORY."""
    found = {}
    sites = {}
    # Headers too, not only .cpp: an inline call in a header would be just
    # as silent on a remote client, and check 1's .cpp-only walk is the
    # reason nothing would notice.
    for pattern in ("*.cpp", "*.h"):
        for path in sorted(GUI.rglob(pattern)):
            rel = path.relative_to(ROOT).as_posix()
            for num, line in enumerate(path.read_text().splitlines(), 1):
                stripped = line.strip()
                if _is_comment_line(stripped):
                    continue
                if FOR_SLICE_PATTERN.search(line):
                    found[rel] = found.get(rel, 0) + 1
                    sites.setdefault(rel, []).append(f"{rel}:{num}: {stripped}")

    failures = []
    for rel in sorted(set(found) | set(FOR_SLICE_INVENTORY)):
        actual = found.get(rel, 0)
        expected = FOR_SLICE_INVENTORY.get(rel)
        if expected is None:
            failures.append(
                f"{rel}: {actual} call(s) to rxChannelForSlice(), but this "
                f"file is not in the inventory")
            failures.extend(f"    {s}" for s in sites[rel])
        elif actual == 0:
            failures.append(
                f"{rel}: inventory expects {expected} call(s) to "
                f"rxChannelForSlice() but the file has none; remove the entry")
        elif actual != expected:
            failures.append(
                f"{rel}: inventory says {expected} call(s) to "
                f"rxChannelForSlice(), found {actual}")
            failures.extend(f"    {s}" for s in sites[rel])

    if failures:
        print("[gui-dsp-access] The rxChannelForSlice() call-site inventory in")
        print("scripts/verify-no-gui-dsp-access.py no longer matches src/gui/.")
        print("These calls are legitimate in LOCAL mode and are not banned. On a")
        print("remote-station client they resolve to nullptr and the control")
        print("silently does nothing, and RadioModel's hand-out audit does not")
        print("count this wrapper, so nothing else in the tree notices. Update")
        print("the inventory, and check that the surface you added it to is not")
        print("a Setup page that would come up enabled and dead in remote mode.")
        for f in failures:
            print(f"  {f}")
        return 1

    total = sum(found.values())
    print(f"[gui-dsp-access] OK: {total} rxChannelForSlice() call(s) in "
          f"src/gui/ across {len(found)} file(s), all inventoried")
    return 0


def main() -> int:
    # Both checks always run, so one invocation reports every problem
    # rather than hiding the second behind the first.
    rc = check_rx_channel_ban()
    rc |= check_rx_channel_for_slice_inventory()
    return rc

if __name__ == "__main__":
    sys.exit(main())
