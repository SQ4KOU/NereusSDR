#!/usr/bin/env python3
# no-port-check: NereusSDR-original visual preview tool; retained-component proof.
#
# tests/visual/compare_retained.py  (NereusSDR)
#
#   compare_retained.py <run-root> [--out <dir>] [--baseline <run-dir>]
#
# Proves the "retained" claims: for every proposed scene that shows the same
# station data as a baseline scene (the two-band, four-rx and transmit data
# sets), compares at the same profile, pixel by pixel:
#
#   - the slice flag crop ("flag") against the baseline's flag crop;
#   - the spectrum plot area against the baseline's.
#
# Counterparts are found from the sidecars, not from a list of scene names:
# two scenes show the same data when their spectrum_checks agree on every
# pan's centre, sample rate, span, reference level and seeded signals, and
# on whether the transmit display is keyed (the notes name
# MoxDisplayController::setKeyed(true ...)). Among the baseline scenes with
# that data, the plain data-set scene (baseline-two-band, baseline-four-rx,
# baseline-tx) is preferred over the posed ones (header modes, floating).
#
# The spectrum plot area comes from the sidecar regions when a region names
# SpectrumWidget; otherwise from the crops: below the top strip, above the
# status bar, right of a left strip and left of the right panel or right
# strip. The slice flag and pan 1 toolbar crops inside it are excluded (the
# flag is compared on its own, the toolbar is restyled by the look). The
# spectrum grid and scale are not excluded: the look leaves the GPU spectrum
# chrome alone, so they must match.
#
# Writes <out>/retained/retained-comparison.json and a diff PNG per compared
# pair (<out>/retained/<profile>/<scene>--<target>.diff.png: the baseline in
# dim grey, differing pixels in magenta, their bounding box in amber, the
# excluded rects outlined in grey). --out defaults to the run root.
# The JSON lists every comparison made and, for every proposed scene and
# profile, why a comparison was not possible. --baseline reads the
# baseline captures from another run dir (a stored gallery's baseline/)
# instead of <run-root>/baseline; paths outside the run root are written
# relative to it.
#
# Modification history (NereusSDR):
#   2026-10-08 - Written for the workspace visual review. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - setup-look-* counterparts dropped (scenes removed).
#                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - Source maps never catalogued as captures; same-dialog
#                pairs listed as not possible; both spectrum-area paths
#                described; both sides of a spectrum pair use one method;
#                a dialog or menu composited over a target is masked.
#                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - --baseline: the baseline captures from another run dir.
#                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.

import argparse
import datetime
import json
import os
import pathlib
import sys

from PIL import Image, ImageChops, ImageDraw

CROP_KINDS = ("crop", "crop-3x")
# sourcemap.py's SUFFIX: a source map is never a capture.
MAP_SUFFIX = ".sourcemap.png"
SIDES = ("baseline", "proposed")
DATA_SET_PREFERENCE = ("baseline-two-band", "baseline-four-rx", "baseline-tx")
RESULT_DIR = "retained"
RESULT_FILE = "retained-comparison.json"
kDiffColour = (0xFF, 0x2B, 0xD6)
kBboxColour = (0xD8, 0xA5, 0x5F)
kMaskColour = (0x7E, 0x7E, 0x85)
kLargeDelta = 32


# --------------------------------------------------------------------------
# Catalogue of a run root
# --------------------------------------------------------------------------

def image_part(meta, png):
    """The part after "--" in the image stem ("" for the main image)."""
    stem = png.stem
    return stem.split("--", 1)[1] if "--" in stem else ""


def catalogue(root, baseline=None):
    """{side: {profile: {scene: {"main": [...], "crops": [...]}}}}.

    Each image is a dict: png, json (Paths), meta, kind, part.
    A run dir without baseline/ and proposed/ is catalogued as "proposed".
    `baseline`, when given, is the baseline run dir in place of
    <root>/baseline.
    """
    cat = {s: {} for s in SIDES}
    dirs = {"baseline": baseline or root / "baseline", "proposed": root / "proposed"}
    sides = [(s, dirs[s]) for s in SIDES if dirs[s].is_dir()]
    if not sides:
        sides = [("proposed", root)]
    for side, base in sides:
        for prof_dir in sorted(p for p in base.iterdir() if p.is_dir()):
            for js in sorted(prof_dir.glob("*.json")):
                png = js.with_suffix(".png")
                if not png.exists() or png.name.endswith(MAP_SUFFIX):
                    continue
                try:
                    meta = json.loads(js.read_text())
                except (OSError, ValueError):
                    continue
                scene = meta.get("scene") or png.stem.split("--", 1)[0]
                profile = meta.get("profile") or prof_dir.name
                kind = meta.get("kind", "window")
                entry = cat[side].setdefault(profile, {}).setdefault(scene, {"main": [], "crops": []})
                img = {"png": png, "json": js, "meta": meta, "kind": kind, "part": image_part(meta, png)}
                entry["crops" if kind in CROP_KINDS else "main"].append(img)
    for side in cat.values():
        for scenes in side.values():
            for entry in scenes.values():
                entry["main"].sort(key=lambda i: (i["part"] != "", i["part"]))
                entry["crops"].sort(key=lambda i: (i["part"].replace("@3x", ""), i["kind"]))
    return cat


def window_image(entry):
    for img in entry["main"]:
        if img["part"] == "" and img["kind"] == "window":
            return img
    return None


def crop_image(entry, name):
    for img in entry["crops"]:
        if img["part"] == name and img["kind"] == "crop":
            return img
    return None


# --------------------------------------------------------------------------
# Data sets and counterparts
# --------------------------------------------------------------------------

def keyed(meta):
    for n in meta.get("notes") or []:
        if isinstance(n, str) and "setKeyed(true" in n:
            return True
    return False


def data_signature(meta):
    checks = meta.get("spectrum_checks") or []
    if not checks:
        return None
    pans = []
    for c in checks:
        sigs = tuple((s.get("hz"), s.get("dbm"), s.get("shape")) for s in c.get("signals") or [])
        pans.append((c.get("pan"), c.get("centre_hz"), c.get("sample_rate_hz"), c.get("span_hz"),
                     c.get("ref_level_dbm"), sigs))
    return json.dumps([pans, keyed(meta)], sort_keys=True)


def baseline_by_signature(cat, profile):
    groups = {}
    for scene, entry in cat["baseline"].get(profile, {}).items():
        w = window_image(entry)
        if not w:
            continue
        sig = data_signature(w["meta"])
        if sig:
            groups.setdefault(sig, []).append(scene)
    chosen = {}
    for sig, scenes in groups.items():
        pref = [s for s in DATA_SET_PREFERENCE if s in scenes]
        chosen[sig] = pref[0] if pref else sorted(scenes, key=lambda s: (len(s), s))[0]
    return chosen


def dialog_counterpart(cat, scene, profile):
    base = cat["baseline"].get(profile, {})
    names = []
    if scene.startswith("look-"):
        names.append(scene[len("look-"):])
    for n in names:
        if n in base:
            return n
    return None


def counterpart(cat, scene, profile, _cache={}):
    """(baseline scene, reason) for a proposed scene at a profile, or (None, reason)."""
    entry = cat["proposed"].get(profile, {}).get(scene)
    if not entry:
        return None, "not captured at this profile"
    for img in entry["main"]:
        explicit = img["meta"].get("baseline_counterpart") or img["meta"].get("counterpart")
        if explicit:
            if explicit in cat["baseline"].get(profile, {}):
                return explicit, "named in the sidecar"
            return None, "sidecar names %s, not captured at this profile" % explicit
    d = dialog_counterpart(cat, scene, profile)
    if d:
        return d, "same dialog"
    w = window_image(entry)
    if not w:
        return None, "no main window image"
    sig = data_signature(w["meta"])
    if not sig:
        return None, "no spectrum data in the sidecar"
    key = (id(cat), profile)
    if key not in _cache:
        _cache[key] = baseline_by_signature(cat, profile)
    b = _cache[key].get(sig)
    if not b:
        return None, "no baseline scene shows the same data"
    return b, "same station data"


def data_set_label(baseline_scene):
    return baseline_scene[len("baseline-"):] if baseline_scene.startswith("baseline-") else baseline_scene


# --------------------------------------------------------------------------
# Pixel comparison
# --------------------------------------------------------------------------

def max_channel(diff):
    if diff.mode == "L":
        return diff
    bands = diff.split()
    out = bands[0]
    for b in bands[1:3]:
        out = ImageChops.lighter(out, b)
    return out


def compare_images(a, b, masks):
    """a, b: RGB images of one size; masks: list of (x0, y0, x1, y1) in px."""
    a, b = a.copy(), b.copy()
    for m in masks:
        a.paste((0, 0, 0), m)
        b.paste((0, 0, 0), m)
    d = max_channel(ImageChops.difference(a, b))
    hist = d.histogram()
    total = a.size[0] * a.size[1]
    differing = total - hist[0]
    max_delta = max((i for i, n in enumerate(hist) if n), default=0)
    large = sum(hist[kLargeDelta + 1:])
    bbox = d.point(lambda v: 255 if v else 0).getbbox()
    return {"differing_pixels": differing, "total_pixels": total, "max_channel_delta": max_delta,
            "pixels_over_delta_%d" % kLargeDelta: large, "bbox": list(bbox) if bbox else None}, d


def diff_png(base, d, masks, bbox, path, device_px):
    grey = base.convert("L").point(lambda v: int(v * 0.35)).convert("RGB")
    on = d.point(lambda v: 255 if v else 0)
    grey.paste(kDiffColour, (0, 0), on)
    draw = ImageDraw.Draw(grey)
    w = max(1, int(round(device_px)))
    for m in masks:
        draw.rectangle((m[0], m[1], m[2] - 1, m[3] - 1), outline=kMaskColour, width=w)
    if bbox:
        draw.rectangle((bbox[0], bbox[1], bbox[2] - 1, bbox[3] - 1), outline=kBboxColour, width=2 * w)
    path.parent.mkdir(parents=True, exist_ok=True)
    grey.save(path)


def crop_logical(meta, name):
    for c in meta.get("crops") or []:
        if c.get("name") == name:
            return c.get("logical")
    return None


def spectrum_regions(meta):
    return [r for r in meta.get("regions") or [] if "SpectrumWidget" in str(r.get("widget_class", "")) or
            str(r.get("type", "")).lower() == "spectrum"]


def spectrum_area(meta, use_regions=True):
    """(rect [x,y,w,h] logical, method, exclusions [(name, rect)]) or (None, reason, []).

    use_regions False takes the crops path even when the sidecar has
    SpectrumWidget regions, so both sides of a pair use one method."""
    regions = meta.get("regions") or []
    spec = spectrum_regions(meta) if use_regions else []
    if spec:
        xs = [r.get("visible_rect") or r.get("rect") for r in spec]
        x0 = min(r[0] for r in xs)
        y0 = min(r[1] for r in xs)
        x1 = max(r[0] + r[2] for r in xs)
        y1 = max(r[1] + r[3] for r in xs)
        rect = [x0, y0, x1 - x0, y1 - y0]
        excl = []
        for r in regions:
            if r in spec:
                continue
            rr = r.get("visible_rect") or r.get("rect")
            if rr and rr[2] > 0 and rr[3] > 0 and inside(rr, rect) and rr[2] * rr[3] < rect[2] * rect[3]:
                excl.append((r.get("name", "region"), rr))
        return rect, "regions (SpectrumWidget)", excl
    top = crop_logical(meta, "top-strip")
    status = crop_logical(meta, "status-bar")
    panel = crop_logical(meta, "right-panel")
    width = (meta.get("logical_size") or [0, 0])[0]
    if not (top and status and panel and width):
        return None, "no SpectrumWidget region and no top-strip / status-bar / right-panel crops", []
    # The "right-panel" crop is the Main Panel wherever it sits; in a left
    # strip it bounds the area on the left instead.
    panel_on_left = panel[0] + panel[2] / 2.0 < width / 2.0
    right = crop_logical(meta, "strip-right") or (None if panel_on_left else panel)
    left = crop_logical(meta, "strip-left") or (panel if panel_on_left else None)
    x0 = left[0] + left[2] if left else 0
    y0 = top[1] + top[3]
    st = crop_logical(meta, "strip-top")
    if st:
        y0 = max(y0, st[1] + st[3])
    y1 = status[1]
    sb = crop_logical(meta, "strip-bottom")
    if sb:
        y1 = min(y1, sb[1])
    x1 = right[0] if right else width
    rect = [x0, y0, x1 - x0, y1 - y0]
    excl = [(n, crop_logical(meta, n)) for n in ("flag", "pan1-toolbar") if crop_logical(meta, n)]
    return rect, "crops (central area between strips and bars; edges approximate)", excl


def covering(meta, rect):
    """[(name, logical rect)]: the parts of `rect` covered by a top-level
    composited over the window (a dialog or menu: a root region after the
    first), which never show the retained component."""
    out = []
    regions = meta.get("regions") or []
    for i, r in enumerate(regions):
        if i == 0 or r.get("parent", 0) != -1:
            continue
        v = r.get("visible_rect") or r.get("rect")
        if not v:
            continue
        x0, y0 = max(v[0], rect[0]), max(v[1], rect[1])
        x1, y1 = min(v[0] + v[2], rect[0] + rect[2]), min(v[1] + v[3], rect[1] + rect[3])
        if x1 > x0 and y1 > y0:
            out.append((r.get("name", "top-level"), [x0, y0, x1 - x0, y1 - y0]))
    return out


def inside(r, outer):
    return (r[0] >= outer[0] and r[1] >= outer[1] and r[0] + r[2] <= outer[0] + outer[2]
            and r[1] + r[3] <= outer[1] + outer[3])


def to_px(rect, origin, dpr):
    x, y, w, h = rect
    ox, oy = origin
    return (int(round((x - ox) * dpr)), int(round((y - oy) * dpr)),
            int(round((x + w - ox) * dpr)), int(round((y + h - oy) * dpr)))


def clip(box, size):
    x0, y0, x1, y1 = box
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(size[0], x1), min(size[1], y1)
    return (x0, y0, x1, y1) if x1 > x0 and y1 > y0 else None


# --------------------------------------------------------------------------
# Runs
# --------------------------------------------------------------------------

def rel(path, base):
    # A --baseline outside the run root is written relative to it ("../").
    return os.path.relpath(pathlib.Path(path).resolve(), base)


def compare_flag(root, out, profile, scene, bscene, pentry, bentry):
    p = crop_image(pentry, "flag")
    b = crop_image(bentry, "flag")
    if not p or not b:
        missing = "proposed" if not p else "baseline"
        return None, "no flag crop in the %s scene at this profile" % missing
    pa = Image.open(p["png"]).convert("RGB")
    ba = Image.open(b["png"]).convert("RGB")
    res = {"target": "flag", "proposed_png": rel(p["png"], root), "baseline_png": rel(b["png"], root),
           "size": list(pa.size), "geometry": "flag crop", "excluded": []}
    if pa.size != ba.size:
        res.update(status="not-comparable", reason="flag crop sizes differ: %s vs %s" % (pa.size, ba.size))
        return res, None
    dpr = float(p["meta"].get("dpr") or 1.0)
    # A dialog or menu composited over the flag hides it: mask what it
    # covers, and do not compare a flag it hides.
    masks = []
    w = window_image(pentry)
    rect = crop_logical(w["meta"], "flag") if w else None
    if rect:
        for name, r in covering(w["meta"], rect):
            box = clip(to_px(r, rect[:2], dpr), pa.size)
            if box:
                masks.append(box)
                res["excluded"].append({"name": name, "px": list(box)})
    covered = sum((m[2] - m[0]) * (m[3] - m[1]) for m in masks)
    if covered >= 0.9 * pa.size[0] * pa.size[1]:
        res.update(status="not-comparable", reason="the flag is covered by %s" % ", ".join(
            e["name"] for e in res["excluded"]))
        return res, None
    stats, d = compare_images(pa, ba, masks)
    target = out / RESULT_DIR / profile / ("%s--flag.diff.png" % scene)
    diff_png(ba, d, masks, stats["bbox"], target, dpr)
    res.update(stats)
    res.update(status="compared", identical=stats["differing_pixels"] == 0, diff_png=rel(target, out))
    return res, None


def compare_spectrum(root, out, profile, scene, bscene, pentry, bentry):
    p = window_image(pentry)
    b = window_image(bentry)
    if not p or not b:
        return None, "no main window image"
    # One method for both sides: SpectrumWidget regions only when both
    # sidecars have them (stored baselines from before the regions contract
    # have none), else the crops path for both.
    both = bool(spectrum_regions(p["meta"])) and bool(spectrum_regions(b["meta"]))
    prect, pmethod, pex = spectrum_area(p["meta"], both)
    brect, bmethod, bex = spectrum_area(b["meta"], both)
    if prect is None or brect is None:
        return None, "spectrum area unknown: %s" % (pmethod if prect is None else bmethod)
    res = {"target": "spectrum-area", "proposed_png": rel(p["png"], root), "baseline_png": rel(b["png"], root),
           "geometry": pmethod, "proposed_rect": prect, "baseline_rect": brect,
           "grid": "included (the look leaves the GPU spectrum grid and scale alone)"}
    if prect[2:] != brect[2:]:
        res.update(status="not-comparable", reason="spectrum area sizes differ: %dx%d vs %dx%d logical" % (
            prect[2], prect[3], brect[2], brect[3]), excluded=[])
        return res, None
    dpr = float(p["meta"].get("dpr") or 1.0)
    pimg = Image.open(p["png"]).convert("RGB")
    bimg = Image.open(b["png"]).convert("RGB")
    pa = pimg.crop(to_px(prect, (0, 0), dpr))
    ba = bimg.crop(to_px(brect, (0, 0), dpr))
    if pa.size != ba.size:
        res.update(status="not-comparable", reason="pixel sizes differ: %s vs %s" % (pa.size, ba.size), excluded=[])
        return res, None
    masks, excluded = [], []
    pex = list(pex) + covering(p["meta"], prect)
    for (name, r), origin in [(e, prect[:2]) for e in pex] + [(e, brect[:2]) for e in bex]:
        box = clip(to_px(r, origin, dpr), pa.size)
        if box and box not in masks:
            masks.append(box)
            excluded.append({"name": name, "px": list(box)})
    stats, d = compare_images(pa, ba, masks)
    target = out / RESULT_DIR / profile / ("%s--spectrum-area.diff.png" % scene)
    diff_png(ba, d, masks, stats["bbox"], target, dpr)
    res.update(stats)
    res.update(status="compared", identical=stats["differing_pixels"] == 0, size=list(pa.size),
               excluded=excluded, diff_png=rel(target, out))
    return res, None


def run(root, out, baseline=None):
    cat = catalogue(root, baseline)
    comparisons, not_possible = [], []
    profiles = sorted(set(cat["proposed"]) | set(cat["baseline"]))
    for profile in profiles:
        for scene in sorted(cat["proposed"].get(profile, {})):
            pentry = cat["proposed"][profile][scene]
            bscene, how = counterpart(cat, scene, profile)
            if bscene and how == "same dialog":
                not_possible.append({"profile": profile, "scene": scene, "baseline_scene": bscene,
                                     "reason": "dialog: compared only side by side on the page"})
                continue
            if not bscene or not window_image(pentry):
                if not bscene and window_image(pentry):
                    not_possible.append({"profile": profile, "scene": scene, "reason": how})
                continue
            bentry = cat["baseline"][profile][bscene]
            for fn in (compare_flag, compare_spectrum):
                res, why = fn(root, out, profile, scene, bscene, pentry, bentry)
                target = "flag" if fn is compare_flag else "spectrum-area"
                if res is None:
                    not_possible.append({"profile": profile, "scene": scene, "target": target,
                                         "baseline_scene": bscene, "reason": why})
                    continue
                res.update(profile=profile, scene=scene, baseline_scene=bscene,
                           data_set=data_set_label(bscene), matched_by=how)
                comparisons.append(res)
    doc = {
        "tool": "compare_retained.py",
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
        "method": {
            "counterparts": "same spectrum_checks data (pan centre, sample rate, span, reference level, "
                            "seeded signals) and transmit keying; plain data-set baseline preferred",
            "flag": "the flag crop, pixel by pixel; any part covered by a dialog or menu composited "
                    "over the window is masked, and a flag covered almost whole is not compared",
            "spectrum": "the spectrum plot area, grid included. Regions path (sidecar has SpectrumWidget "
                        "regions): the union of the pan plots, excluding every smaller region inside them "
                        "(flags, toolbars, buttons, badges), listed per row. Crops path (either sidecar "
                        "lacks them; then both sides use it): the area between the strip, bar and panel crops, with the flag and pan 1 toolbar "
                        "crops excluded",
            "large_delta": kLargeDelta,
        },
        "comparisons": comparisons,
        "not_possible": not_possible,
    }
    path = out / RESULT_DIR / RESULT_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1, sort_keys=True))
    return doc, path


def summary_rows(doc):
    rows = []
    for c in doc["comparisons"]:
        if c["status"] == "compared":
            verdict = "IDENTICAL" if c["identical"] else "%d px differ, max delta %d, bbox %s" % (
                c["differing_pixels"], c["max_channel_delta"], c["bbox"])
        else:
            verdict = "not comparable: " + c["reason"]
        rows.append((c["profile"], c["scene"], c["target"], c["baseline_scene"], verdict))
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compare retained components against the baseline.")
    ap.add_argument("root", help="the run root (baseline/ and proposed/)")
    ap.add_argument("--out", help="write retained/ here instead of into the run root")
    ap.add_argument("--baseline", help="the baseline run dir (default: <root>/baseline)")
    args = ap.parse_args(argv)
    root = pathlib.Path(args.root).resolve()
    out = pathlib.Path(args.out).resolve() if args.out else root
    baseline = pathlib.Path(args.baseline).resolve() if args.baseline else root / "baseline"
    if not baseline.is_dir() or not (root / "proposed").is_dir():
        sys.exit("%s has no proposed/ or %s is not a dir" % (root, baseline))
    doc, path = run(root, out, baseline)
    for row in summary_rows(doc):
        print("%s  %s  %s  vs %s: %s" % row)
    compared = [c for c in doc["comparisons"] if c["status"] == "compared"]
    print("retained: %d compared (%d identical), %d not comparable, %d not possible; %s" % (
        len(compared), sum(1 for c in compared if c["identical"]),
        len(doc["comparisons"]) - len(compared), len(doc["not_possible"]), path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
