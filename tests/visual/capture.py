#!/usr/bin/env python3
# no-port-check: NereusSDR-original visual preview tool; capture driver.
#
# tests/visual/capture.py  (NereusSDR)
#
# Runs nereus_visual_preview once per scene and profile (one scene per
# process), writes every PNG with its JSON sidecar under
# <out>/<profile>/, stamps each sidecar with the git HEAD and dirty state,
# and writes <out>/manifest.json. --verify runs each pair twice and compares
# the pixels; --verify --against <dir> runs each pair once and compares it
# with the stored images in <dir>/<profile>/. See tests/visual/README.md.
#
# Modification history (NereusSDR):
#   2026-10-07 - Written for the workspace visual review. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-07 - --against (compare with a stored run) and the
#                proposed-look provenance for look-* scenes. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-07 - "proposed" provenance for proposed-design scenes (any
#                scene not named baseline-*, dialog-* or look-*). J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - Source maps are never taken for captures; a scene's earlier
#                files are cleared before it is captured again; --against
#                reports stored images a run no longer produces. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - compare() looks at every channel: it had compared alpha
#                only and reported every opaque pair identical. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - A scene no longer claims the images of a longer scene
#                name (controls-sheet and controls-sheet--sizes): clearing,
#                stamping and comparing go through owns(). J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - A scene's <name>.geometry.tsv dumps are cleared with its
#                images. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
#                Claude Code.
#   2026-10-08 - The menus, popups, toasts and tooltips scenes are the
#                current app (retained-current-app) but for the four that
#                exist only in the workspace look. J.J. Boyd (KG4VCF),
#                AI-assisted via Anthropic Claude Code.
#   2026-10-09 - A --scrolled scene owns its step images <scene>-<N>.
#                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-09 - A failed run prints the preview's own failure line, not
#                the last log line it wrote. J.J. Boyd (KG4VCF),
#                AI-assisted via Anthropic Claude Code.

import argparse
import concurrent.futures
import datetime
import fnmatch
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO / "build" / "tests" / "visual" / "nereus_visual_preview"

# name: (logical width, logical height, device pixel ratio)
PROFILES = {
    "mac-1512x869@2": (1512, 869, 2.0),
    "laptop-1280x800@1": (1280, 800, 1.0),
    "laptop-1280x800@1.25": (1280, 800, 1.25),
    "laptop-1280x800@1.5": (1280, 800, 1.5),
    "fhd-1920x1080@1": (1920, 1080, 1.0),
}
# Dialog scenes run at these profiles only.
DIALOG_PROFILES = ("mac-1512x869@2", "laptop-1280x800@1")
# Items the baseline cannot show, whatever the scene.
NOT_SHOWN = [
    {"item": "Amp (Power Genius / amplifier applet)",
     "reason": "needs a 4O3A device setting that starts discovery or a device connection"},
    {"item": "Tuner (Tuner Genius applet)",
     "reason": "needs a 4O3A device setting that starts discovery or a device connection"},
    {"item": "RF-Kit applet",
     "reason": "needs an RF-Kit setting that starts a device connection"},
]
RUN_TIMEOUT_S = 180
# sourcemap.py writes <image>.sourcemap.png next to each capture (its
# SUFFIX); those are never captures.
MAP_SUFFIX = ".sourcemap.png"


def failure_line(stderr):
    """The preview's own failure line ("nereus_visual_preview: ..."), not
    whatever log line the run printed last; the last line when it printed
    none."""
    lines = [l for l in (stderr or "").strip().splitlines() if l.strip()]
    for line in reversed(lines):
        if line.startswith("nereus_visual_preview:"):
            return line
    return lines[-1] if lines else ""


def scene_provenance(name):
    # look-* is the proposed workspace look over a current scene; any other
    # scene that is not a baseline or dialog scene is a proposed design.
    if name.startswith("look-"):
        return "proposed-look"
    if name.startswith("baseline-") or name.startswith("dialog-"):
        return "retained-current-app"
    # The menus, popups, toasts and tooltips of the current app; the few
    # that exist only in the workspace look are proposed.
    if name in WORKSPACE_ONLY_MENU_SCENES:
        return "proposed"
    if name.startswith(MENU_SCENE_PREFIXES):
        return "retained-current-app"
    return "proposed"


MENU_SCENE_PREFIXES = ("menu-", "menubar-", "popup-", "toast-", "tooltip-")
WORKSPACE_ONLY_MENU_SCENES = {
    "menu-container-options", "menu-content-host", "menu-profile-rail", "menu-dial",
}


def child_env(scale_factor=None):
    env = dict(os.environ)
    env.pop("QT_SCALE_FACTOR", None)
    env.pop("QT_SCREEN_SCALE_FACTORS", None)
    env.pop("QT_AUTO_SCREEN_SCALE_FACTOR", None)
    if scale_factor is not None:
        env["QT_SCALE_FACTOR"] = format_factor(scale_factor)
    if sys.platform == "darwin":
        # A background-only process: no Dock icon, no activation, no focus.
        env["QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM"] = "1"
    return env


def format_factor(f):
    return ("%.6f" % f).rstrip("0").rstrip(".")


def run(binary, args, env, timeout=RUN_TIMEOUT_S):
    return subprocess.run([str(binary)] + args, env=env, capture_output=True, text=True,
                          timeout=timeout)


def probe(binary):
    r = run(binary, ["--probe"], child_env(), timeout=60)
    if r.returncode != 0:
        sys.exit("probe failed: %s" % r.stderr.strip())
    for line in r.stdout.splitlines():
        if line.startswith("primary_screen_dpr="):
            return float(line.split("=", 1)[1])
    sys.exit("probe printed no primary_screen_dpr")


def list_scenes(binary):
    r = run(binary, ["--list"], child_env(), timeout=120)
    if r.returncode != 0:
        sys.exit("--list failed: %s" % r.stderr.strip())
    scenes = []
    for line in r.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) == 2:
            scenes.append((parts[0], parts[1] == "dialog"))
    KNOWN_SCENES.update(name for name, _ in scenes)
    return scenes


# Every scene name the binary lists; filled by list_scenes().
KNOWN_SCENES = set()


def is_step_of(stem, scene):
    """True when a file stem is a step image of a --scrolled scene:
    <scene>-1, <scene>-2, ... (dialog-setup-power--scrolled-1)."""
    return (scene.endswith(SCROLLED_SUFFIX) and stem.startswith(scene + "-")
            and stem[len(scene) + 1:].isdigit())


SCROLLED_SUFFIX = "--scrolled"


def owns(stem, scene):
    """True when a file stem is one of the scene's images: <scene>,
    <scene>--<part> or a --scrolled scene's <scene>-<step>, and not an
    image of a longer scene name that starts the same way
    (controls-sheet--sizes is its own scene, not a part of controls-sheet;
    dialog-setup-power--scrolled-1 belongs to dialog-setup-power--scrolled,
    not to dialog-setup-power)."""
    def mine(name):
        return stem == name or stem.startswith(name + "--") or is_step_of(stem, name)

    if not mine(scene):
        return False
    return not any(len(other) > len(scene) and mine(other) for other in KNOWN_SCENES)


def git_state():
    head = subprocess.run(["git", "-C", str(REPO), "rev-parse", "HEAD"],
                          capture_output=True, text=True).stdout.strip()
    status = subprocess.run(["git", "-C", str(REPO), "status", "--porcelain"],
                            capture_output=True, text=True).stdout
    return head, bool(status.strip())


def matches(name, patterns):
    return not patterns or any(fnmatch.fnmatchcase(name, p) for p in patterns)


def plan_runs(scenes, scene_patterns, profile_patterns):
    runs = []
    for name, is_dialog in scenes:
        if not matches(name, scene_patterns):
            continue
        for profile in PROFILES:
            if not matches(profile, profile_patterns):
                continue
            if is_dialog and profile not in DIALOG_PROFILES:
                continue
            runs.append((name, profile))
    return runs


def clear_scene(out_dir, scene):
    """Removes the scene's earlier captures, sidecars and source maps from
    out_dir, so a re-capture lists only what this run wrote."""
    for p in list(out_dir.iterdir()) if out_dir.is_dir() else []:
        stem = p.name.split(".", 1)[0]
        if p.is_file() and p.suffix in (".png", ".json", ".tsv") and owns(stem, scene):
            p.unlink()


def capture_one(binary, screen_dpr, scene, profile, out_dir):
    w, h, dpr = PROFILES[profile]
    factor = dpr / screen_dpr
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_scene(out_dir, scene)
    args = ["--scene", scene, "--size", "%dx%d" % (w, h), "--dpr", format_factor(dpr),
            "--out", str(out_dir), "--profile", profile]
    try:
        r = run(binary, args, child_env(factor))
        return {"scene": scene, "profile": profile, "returncode": r.returncode,
                "stdout": r.stdout, "stderr": r.stderr, "qt_scale_factor": format_factor(factor)}
    except subprocess.TimeoutExpired:
        return {"scene": scene, "profile": profile, "returncode": None, "stdout": "",
                "stderr": "timed out after %d s" % RUN_TIMEOUT_S,
                "qt_scale_factor": format_factor(factor)}


def images_of(out_dir, scene):
    # A scene's images are <scene>.png and <scene>--<part>.png.
    return sorted(p for p in out_dir.glob("*.png")
                  if not p.name.endswith(MAP_SUFFIX)
                  and owns(p.stem, scene))


def stamp_sidecars(out_dir, scene, head, dirty, screen_dpr, factor):
    entries = []
    for png in images_of(out_dir, scene):
        sidecar = png.with_suffix(".json")
        data = json.loads(sidecar.read_text()) if sidecar.exists() else {}
        data["git_head"] = head
        data["git_dirty"] = dirty
        data["probed_primary_screen_dpr"] = screen_dpr
        data["qt_scale_factor"] = factor
        sidecar.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
        entries.append({"png": str(png.relative_to(out_dir.parent)),
                        "json": str(sidecar.relative_to(out_dir.parent)),
                        "kind": data.get("kind"),
                        "sha256": data.get("sha256")})
    return entries


def compare(png_a, png_b):
    """None when identical, else (differing pixel count, bounding box)."""
    from PIL import Image, ImageChops
    a = Image.open(png_a).convert("RGBA")
    b = Image.open(png_b).convert("RGBA")
    if a.size != b.size:
        return (max(a.size[0] * a.size[1], b.size[0] * b.size[1]),
                "sizes differ %s vs %s" % (a.size, b.size))
    diff = ImageChops.difference(a, b)
    # Every channel counts. getbbox() looks only at alpha by default when
    # there is one, and the difference of two opaque images has alpha 0
    # everywhere, so it called every pair identical; an "L" conversion
    # also rounds small single-channel differences to zero.
    box = diff.getbbox(alpha_only=False)
    if box is None:
        return None
    count = sum(1 for px in diff.getdata() if any(px))
    return count, box


def verify_against(binary, screen_dpr, runs, jobs, stored):
    """Captures each pair once into a scratch directory and compares every
    image with the same file under <stored>/<profile>/. Never writes to
    <stored>."""
    failures = 0
    work = pathlib.Path(tempfile.mkdtemp(prefix="nereus-visual-against-"))
    try:
        def once(item):
            scene, profile = item
            return scene, profile, capture_one(binary, screen_dpr, scene, profile, work / profile)

        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            results = list(pool.map(once, runs))
        for scene, profile, r in results:
            if r["returncode"] != 0:
                print("%s @ %s: RUN FAILED (%s) %s" % (
                    scene, profile, r["returncode"], failure_line(r["stderr"])))
                failures += 1
                continue
            produced = {png.name for png in images_of(work / profile, scene)}
            for name in sorted({p.name for p in images_of(stored / profile, scene)} - produced):
                print("%s @ %s: stored %s is no longer produced" % (scene, profile, name))
                failures += 1
            for png in images_of(work / profile, scene):
                ref = stored / profile / png.name
                if not ref.exists():
                    print("%s @ %s: %s has no stored image" % (scene, profile, png.name))
                    failures += 1
                    continue
                result = compare(png, ref)
                if result is None:
                    print("%s @ %s: %s IDENTICAL to stored" % (scene, profile, png.name))
                else:
                    count, box = result
                    print("%s @ %s: %s DIFFERS from stored: %d pixels, bounding box %s" % (
                        scene, profile, png.name, count, box))
                    failures += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("verify against %s: %d run(s), %s" % (
        stored, len(runs), "all identical" if failures == 0
        else "%d difference(s) or failure(s)" % failures))
    return failures


def verify(binary, screen_dpr, runs, jobs):
    failures = 0
    work = pathlib.Path(tempfile.mkdtemp(prefix="nereus-visual-verify-"))
    try:
        def both(item):
            scene, profile = item
            a = capture_one(binary, screen_dpr, scene, profile, work / "a" / profile)
            b = capture_one(binary, screen_dpr, scene, profile, work / "b" / profile)
            return scene, profile, a, b

        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            results = list(pool.map(both, runs))
        for scene, profile, a, b in results:
            if a["returncode"] != 0 or b["returncode"] != 0:
                print("%s @ %s: RUN FAILED (%s / %s) %s" % (
                    scene, profile, a["returncode"], b["returncode"],
                    failure_line(a["stderr"] if a["returncode"] != 0 else b["stderr"])))
                failures += 1
                continue
            for png_a in images_of(work / "a" / profile, scene):
                png_b = work / "b" / profile / png_a.name
                if not png_b.exists():
                    print("%s @ %s: %s missing from the second run" % (scene, profile, png_a.name))
                    failures += 1
                    continue
                result = compare(png_a, png_b)
                if result is None:
                    print("%s @ %s: %s IDENTICAL" % (scene, profile, png_a.name))
                else:
                    count, box = result
                    print("%s @ %s: %s DIFFERS: %d pixels, bounding box %s" % (
                        scene, profile, png_a.name, count, box))
                    failures += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("verify: %d run(s), %s" % (len(runs), "all identical" if failures == 0
                                       else "%d difference(s) or failure(s)" % failures))
    return failures


def main():
    ap = argparse.ArgumentParser(description="Capture NereusSDR visual preview scenes.")
    ap.add_argument("--out", type=pathlib.Path,
                    help="run directory (PNGs land in <out>/<profile>/)")
    ap.add_argument("--binary", type=pathlib.Path, default=DEFAULT_BINARY)
    ap.add_argument("--scenes", default="",
                    help="comma-separated scene names or globs (default: all)")
    ap.add_argument("--profiles", default="",
                    help="comma-separated profile names or globs (default: all)")
    ap.add_argument("--verify", action="store_true",
                    help="run each pair twice and compare the pixels; writes nothing to --out")
    ap.add_argument("--against", type=pathlib.Path,
                    help="with --verify: run each pair once and compare with the stored "
                         "images in <dir>/<profile>/ (never written)")
    ap.add_argument("--jobs", type=int, default=4, help="processes at once (default 4)")
    ap.add_argument("--list", action="store_true", help="print the planned runs and exit")
    opts = ap.parse_args()

    if not opts.binary.exists():
        sys.exit("no binary at %s; build it with "
                 "cmake --build build --target nereus_visual_preview" % opts.binary)
    scene_patterns = [p for p in opts.scenes.split(",") if p]
    profile_patterns = [p for p in opts.profiles.split(",") if p]
    unknown = [p for p in profile_patterns if not any(fnmatch.fnmatchcase(n, p) for n in PROFILES)]
    if unknown:
        sys.exit("unknown profile(s): %s; known: %s" % (", ".join(unknown), ", ".join(PROFILES)))

    screen_dpr = probe(opts.binary)
    scenes = list_scenes(opts.binary)
    runs = plan_runs(scenes, scene_patterns, profile_patterns)
    if not runs:
        sys.exit("no scene/profile pairs match")
    if opts.list:
        for scene, profile in runs:
            print("%s\t%s" % (scene, profile))
        return 0
    if opts.against is not None and not opts.verify:
        sys.exit("--against needs --verify")
    if opts.verify and opts.against is not None:
        return 1 if verify_against(opts.binary, screen_dpr, runs, opts.jobs, opts.against) else 0
    if opts.verify:
        return 1 if verify(opts.binary, screen_dpr, runs, opts.jobs) else 0
    if opts.out is None:
        sys.exit("--out is required unless --verify or --list")

    head, dirty = git_state()
    opts.out.mkdir(parents=True, exist_ok=True)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
    with concurrent.futures.ThreadPoolExecutor(max_workers=opts.jobs) as pool:
        futures = [pool.submit(capture_one, opts.binary, screen_dpr, scene, profile,
                               opts.out / profile) for scene, profile in runs]
        results = []
        for future in concurrent.futures.as_completed(futures):
            r = future.result()
            status = "ok" if r["returncode"] == 0 else "FAILED (%s)" % r["returncode"]
            print("%s @ %s: %s" % (r["scene"], r["profile"], status), flush=True)
            if r["returncode"] != 0 and r["stderr"].strip():
                print("    " + failure_line(r["stderr"]), flush=True)
            results.append(r)

    order = {pair: i for i, pair in enumerate(runs)}
    results.sort(key=lambda r: order[(r["scene"], r["profile"])])
    manifest_runs = []
    image_count = 0
    for r in results:
        entry = {"scene": r["scene"], "profile": r["profile"],
                 "qt_scale_factor": r["qt_scale_factor"], "returncode": r["returncode"]}
        if r["returncode"] == 0:
            entry["images"] = stamp_sidecars(opts.out / r["profile"], r["scene"], head, dirty,
                                             screen_dpr, r["qt_scale_factor"])
            image_count += len(entry["images"])
        else:
            entry["error"] = r["stderr"].strip()
        manifest_runs.append(entry)

    manifest = {
        "tool": "nereus_visual_preview",
        # Per run: see scene_provenance(); one value when every run agrees.
        "provenance": (scene_provenance(runs[0][0])
                       if runs and len({scene_provenance(s) for s, _ in runs}) == 1
                       else "mixed; see each sidecar"),
        "started_utc": started,
        "git_head": head,
        "git_dirty": dirty,
        "binary": str(opts.binary),
        "host": {"platform": sys.platform, "machine": platform.machine(),
                 "os": platform.platform()},
        "probed_primary_screen_dpr": screen_dpr,
        "profiles": {name: {"logical_size": [w, h], "dpr": dpr,
                            "qt_scale_factor": format_factor(dpr / screen_dpr)}
                     for name, (w, h, dpr) in PROFILES.items()},
        "dialog_profiles": list(DIALOG_PROFILES),
        "not_shown": NOT_SHOWN,
        "image_count": image_count,
        "runs": manifest_runs,
    }
    (opts.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    failed = [r for r in results if r["returncode"] != 0]
    print("%d run(s), %d image(s), %d failure(s); manifest at %s" % (
        len(results), image_count, len(failed), opts.out / "manifest.json"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
