#!/usr/bin/env python3
# no-port-check: NereusSDR-original visual preview tool; re-capture check.
#
# tests/visual/check_recapture.py  (NereusSDR)
#
#   check_recapture.py [--binary <nereus_visual_preview>] [--keep]
#
# Checks that a source map is never taken for a capture when a run root is
# captured again after the gallery has been built (the README flow: capture,
# build the gallery, re-capture one scene with --scenes, build again). It
# captures one baseline and one proposed scene at mac-1512x869@2 (the
# profile with detail crops, whose maps are <scene>--<crop>.sourcemap.png)
# into a scratch run root, builds the gallery (which writes the source maps
# next to the captures), re-captures the proposed scene, builds again, and
# fails when the manifest, the catalogue or the page lists a
# *.sourcemap.png as a capture, or when a map of a map exists. Exit 0 on
# success. Writes only into a temporary directory.
#
# Modification history (NereusSDR):
#   2026-10-08 - Written for the workspace visual review (review C). J.J.
#                Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import capture  # noqa: E402
import compare_retained  # noqa: E402
import sourcemap  # noqa: E402

PROFILE = "mac-1512x869@2"
BASELINE_SCENE = "baseline-two-band"
PROPOSED_SCENE = "look-two-band"


def run(args):
    r = subprocess.run([sys.executable] + [str(a) for a in args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("FAILED: %s\n%s%s" % (" ".join(str(a) for a in args), r.stdout, r.stderr))
    return r.stdout


def problems_in(root):
    found = []
    manifest = json.loads((root / "proposed" / "manifest.json").read_text())
    for r in manifest.get("runs", []):
        for img in r.get("images", []):
            if img["png"].endswith(sourcemap.SUFFIX):
                found.append("manifest lists %s" % img["png"])
    cat = compare_retained.catalogue(root)
    for side in cat.values():
        for scenes in side.values():
            for entry in scenes.values():
                for img in entry["main"] + entry["crops"]:
                    if img["png"].name.endswith(sourcemap.SUFFIX):
                        found.append("catalogue lists %s" % img["png"])
    for p in root.rglob("*" + sourcemap.SUFFIX):
        if p.name.endswith(".sourcemap" + sourcemap.SUFFIX):
            found.append("map of a map: %s" % p)
    page = (root / "index.html").read_text()
    for p in root.rglob("*" + sourcemap.SUFFIX):
        # A map is shown beside its capture, never as a card of its own.
        if ("alt='%s'" % p.stem) in page or ("alt='%s'" % p.name) in page:
            found.append("page shows %s as a capture" % p.name)
    return found


def main():
    ap = argparse.ArgumentParser(description="Re-capture into a root with source maps lists no map as a capture.")
    ap.add_argument("--binary", type=pathlib.Path, default=capture.DEFAULT_BINARY)
    ap.add_argument("--keep", action="store_true", help="keep the scratch run root and print its path")
    opts = ap.parse_args()
    if not opts.binary.exists():
        sys.exit("no binary at %s" % opts.binary)
    # capture's own guards first: a map beside a capture is not an image of
    # its scene.
    unit = pathlib.Path(tempfile.mkdtemp(prefix="nereus-recapture-unit-"))
    try:
        for name in ("s.png", "s--flag.png", "s.sourcemap.png", "s--flag.sourcemap.png", "s-other.png"):
            (unit / name).write_bytes(b"")
        got = [p.name for p in capture.images_of(unit, "s")]
        if got != ["s--flag.png", "s.png"]:
            sys.exit("FAILED: images_of returned %s" % got)
        capture.clear_scene(unit, "s")
        left = sorted(p.name for p in unit.iterdir())
        if left != ["s-other.png"]:
            sys.exit("FAILED: clear_scene left %s" % left)
    finally:
        shutil.rmtree(unit, ignore_errors=True)

    root = pathlib.Path(tempfile.mkdtemp(prefix="nereus-recapture-"))
    try:
        cap = HERE / "capture.py"
        gallery = HERE / "build_gallery.py"
        common = ["--binary", opts.binary, "--profiles", PROFILE]
        run([cap, "--out", root / "baseline", "--scenes", BASELINE_SCENE] + common)
        run([cap, "--out", root / "proposed", "--scenes", PROPOSED_SCENE] + common)
        run([gallery, root])
        maps = list(root.rglob("*" + sourcemap.SUFFIX))
        if not maps:
            sys.exit("FAILED: the first gallery build wrote no source map, so the check proves nothing")
        run([cap, "--out", root / "proposed", "--scenes", PROPOSED_SCENE] + common)
        run([gallery, root])
        found = problems_in(root)
        if found:
            print("\n".join(found))
            sys.exit("FAILED: %d problem(s)" % len(found))
        print("check_recapture: OK (%d source maps, none listed as a capture)" % len(
            list(root.rglob("*" + sourcemap.SUFFIX))))
    finally:
        if opts.keep:
            print("kept %s" % root)
        else:
            shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
