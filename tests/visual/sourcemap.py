#!/usr/bin/env python3
# no-port-check: NereusSDR-original visual preview tool; source-map overlay renderer.
#
# tests/visual/sourcemap.py  (NereusSDR)
#
#   sourcemap.py <run-root-or-run-dir> [--out <dir>] [--force] [--quiet]
#
# For every capture sidecar (<image>.json) that carries a "regions" list,
# writes <image>.sourcemap.png: the capture dimmed to 45%, each top-most
# tagged region filled at 35% in its provenance colour and outlined 2 device
# px, not-implemented regions hatched, and a legend band under the image.
# With --out the overlays go to <out>/<same relative path> instead of next to
# the capture, so a run root can be rendered without writing into it.
#
# Region contract (see README.md, "Region contract"):
#   - rect / visible_rect are [x, y, w, h]. visible_rect, when present, is
#     what is drawn (an empty visible_rect means "not on screen": skipped).
#   - Units are logical px of the captured top-level (window, sheet, dialog
#     or floating window), origin at its top-left, unless the sidecar (or
#     the region) has a "units" value that mentions "device", in which case
#     they are device px and are divided by the sidecar's dpr.
#   - A crop sidecar (kind crop / crop-3x) carries its parent's regions in
#     the parent's coordinates; its own rect is the crops[] entry whose name
#     matches the image suffix. Regions are offset by that crop's logical
#     origin and scaled by image width / crop logical width.
#   - Overlap: an optional per-region "z" (higher on top) or "depth" orders
#     regions; otherwise larger regions are drawn first so the smaller,
#     nested one is the top-most at a pixel. Every region is filled from
#     the dimmed capture, so a nested fill never compounds with its parent.
#
# Modification history (NereusSDR):
#   2026-10-08 - Written for the workspace visual review. J.J. Boyd
#                (KG4VCF), AI-assisted via Anthropic Claude Code.
#   2026-10-08 - A source map is never mapped again. J.J. Boyd (KG4VCF),
#                AI-assisted via Anthropic Claude Code.

import argparse
import json
import os
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

PROVENANCE_COLOURS = [
    ("retained", (0x3F, 0xA7, 0xA0)),
    ("longpath-port", (0xD8, 0xA5, 0x5F)),
    ("proposed", (0x4A, 0x7B, 0xA8)),
    ("exists-not-wired", (0x9A, 0x7F, 0xD1)),
    ("not-implemented", (0x7E, 0x7E, 0x85)),
]
COLOUR = dict(PROVENANCE_COLOURS)
UNKNOWN_COLOUR = (0xC2, 0x5A, 0x5C)  # an unknown provenance value shows in the tx red

kDimFactor = 0.45
kFillAlpha = 0.35
kOutlineDevicePx = 2
kHatchSpacingDevicePx = 8
kHatchWidthDevicePx = 2
kHatchColour = (0xC8, 0xC8, 0xCE)
kLegendBg = (0x0C, 0x0C, 0x0E)
kLegendText = (0xDC, 0xDC, 0xE1)
kLegendMuted = (0x90, 0x90, 0x96)
kLegendBorder = (0x2C, 0x2C, 0x31)

CROP_KINDS = ("crop", "crop-3x")
SUFFIX = ".sourcemap.png"

FONT_CANDIDATES = [
    "/System/Library/Fonts/SFNS.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
]


def load_font(px):
    px = max(8, int(round(px)))
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, px)
            except OSError:
                continue
    try:
        return ImageFont.load_default(size=px)
    except TypeError:
        return ImageFont.load_default()


def units_are_device(value):
    if isinstance(value, dict):
        value = value.get("regions") or value.get("rect") or json.dumps(value)
    return isinstance(value, str) and "device" in value.lower()


def crop_entry(meta):
    """The crops[] entry that describes this crop image itself."""
    crops = meta.get("crops") or []
    stem = pathlib.Path(meta.get("image", "")).stem
    name = stem.split("--", 1)[1] if "--" in stem else stem
    if name.endswith("@3x"):
        name = name[:-3]
    for c in crops:
        if c.get("name") == name:
            return c
    return crops[0] if len(crops) == 1 else None


def geometry(meta, img_size):
    """(origin_x, origin_y, scale, device_px, problems): logical->image mapping.

    scale is image px per logical px; device_px is image px per device px.
    """
    problems = []
    dpr = float(meta.get("dpr") or 1.0)
    w, h = img_size
    kind = meta.get("kind", "window")
    if kind in CROP_KINDS:
        c = crop_entry(meta)
        if not c or not c.get("logical"):
            return None, ["crop sidecar has no matching crops[] entry"]
        lx, ly, lw, lh = c["logical"]
        if lw <= 0 or lh <= 0:
            return None, ["crop has zero logical size"]
        scale = w / float(lw)
        if abs(h / float(lh) - scale) > 0.02 * scale:
            problems.append("crop aspect differs: image %dx%d, logical %dx%d" % (w, h, lw, lh))
        dev = c.get("device")
        if dev and (abs(dev[2] - lw * dpr) > 1 or abs(dev[3] - lh * dpr) > 1):
            problems.append("crop device %s is not logical %s x dpr %s" % (dev, c["logical"], dpr))
        enlarge = 3.0 if kind == "crop-3x" else 1.0
        if abs(scale - dpr * enlarge) > 0.02:
            problems.append("crop scale %.3f is not dpr x %g" % (scale, enlarge))
        return (lx, ly, scale, scale / dpr), problems
    lw, lh = meta.get("logical_size") or [0, 0]
    if not lw or not lh:
        return None, ["sidecar has no logical_size"]
    scale = w / float(lw)
    dev = meta.get("device_size")
    if dev and (abs(dev[0] - w) > 1 or abs(dev[1] - h) > 1):
        problems.append("device_size %s differs from the PNG %dx%d" % (dev, w, h))
    if abs(scale - dpr) > 0.02:
        problems.append("image px per logical px %.3f differs from dpr %s" % (scale, dpr))
    return (0, 0, scale, scale / dpr), problems


def region_box(region, meta, geo, img_size):
    """The region's drawn rect in image px, clipped, or None when not visible."""
    ox, oy, scale, _ = geo
    if "visible_rect" in region:
        rect = region.get("visible_rect")
    else:
        rect = region.get("rect")
    if not rect or len(rect) != 4:
        return None
    x, y, w, h = [float(v) for v in rect]
    if w <= 0 or h <= 0:
        return None
    units = region.get("units", meta.get("units"))
    if units_are_device(units):
        dpr = float(meta.get("dpr") or 1.0)
        x, y, w, h = x / dpr, y / dpr, w / dpr, h / dpr
    x0 = int(round((x - ox) * scale))
    y0 = int(round((y - oy) * scale))
    x1 = int(round((x + w - ox) * scale))
    y1 = int(round((y + h - oy) * scale))
    iw, ih = img_size
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(iw, x1), min(ih, y1)
    if x1 <= x0 or y1 <= y0:
        return None
    return (x0, y0, x1, y1)


def draw_order(regions):
    def key(item):
        idx, r = item
        if "z" in r:
            return (0, float(r["z"]), idx)
        if "depth" in r:
            return (0, float(r["depth"]), idx)
        rect = r.get("visible_rect") or r.get("rect") or [0, 0, 0, 0]
        area = max(0, rect[2]) * max(0, rect[3])
        return (0, -area, idx)
    return [r for _, r in sorted(enumerate(regions), key=key)]


def hatch(base, box, device_px):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    step = max(4, int(round(kHatchSpacingDevicePx * device_px)))
    lw = max(1, int(round(kHatchWidthDevicePx * device_px)))
    for k in range(-h, w + h, step):
        d.line([(k, h), (k + h, 0)], fill=kHatchColour + (150,), width=lw)
    base.paste(layer, (x0, y0), layer)


def legend(width, scale, counts, title, skipped):
    """A legend band, laid out in logical px and drawn at the image's scale."""
    u = max(1.0, scale)
    pad = int(round(8 * u))
    sw = int(round(12 * u))
    gap = int(round(6 * u))
    item_gap = int(round(16 * u))
    row_h = int(round(20 * u))
    font = load_font(11 * u)
    small = load_font(10 * u)
    probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))

    def tw(text, f):
        b = probe.textbbox((0, 0), text, font=f)
        return b[2] - b[0]

    items = []
    for name, colour in PROVENANCE_COLOURS:
        items.append((name, colour, "%s (%d)" % (name, counts.get(name, 0))))
    for name in sorted(k for k in counts if k not in COLOUR):
        items.append((name, UNKNOWN_COLOUR, "%s (%d, unknown value)" % (name, counts[name])))
    lines = [[]]
    x = pad
    for it in items:
        need = sw + gap + tw(it[2], font)
        if lines[-1] and x + need > width - pad:
            lines.append([])
            x = pad
        lines[-1].append((x, it))
        x += need + item_gap
    note = "Dimmed, no fill: untagged in this sidecar (inherits; the window root is retained)."
    if skipped:
        note += " %d region(s) with zero visible area skipped." % skipped
    note_lines = wrap(note, small, width - 2 * pad, tw)
    title_lines = wrap(title, font, width - 2 * pad, tw)
    height = pad + row_h * (len(title_lines) + len(lines)) + int(round(15 * u)) * len(note_lines) + pad
    band = Image.new("RGB", (width, height), kLegendBg)
    d = ImageDraw.Draw(band)
    d.line([(0, 0), (width, 0)], fill=kLegendBorder, width=max(1, int(round(u))))
    y = pad
    for t in title_lines:
        d.text((pad, y), t, font=font, fill=kLegendText)
        y += row_h
    for line in lines:
        for x, (name, colour, label) in line:
            box = (x, y + (row_h - sw) // 2 - gap // 2, x + sw, y + (row_h - sw) // 2 - gap // 2 + sw)
            fill = tuple(int(c * kFillAlpha + 40 * (1 - kFillAlpha)) for c in colour)
            d.rectangle(box, fill=fill, outline=colour, width=max(1, int(round(u))))
            if name == "not-implemented":
                tile = Image.new("RGB", (sw, sw), fill)
                td = ImageDraw.Draw(tile)
                for k in range(-sw, sw, max(3, int(round(4 * u)))):
                    td.line([(k, sw), (k + sw, 0)], fill=kHatchColour, width=max(1, int(round(u / 2))))
                band.paste(tile, box[:2])
                d.rectangle(box, outline=colour, width=max(1, int(round(u))))
            d.text((x + sw + gap, y), label, font=font, fill=kLegendText)
        y += row_h
    for t in note_lines:
        d.text((pad, y), t, font=small, fill=kLegendMuted)
        y += int(round(15 * u))
    return band


def wrap(text, font, width, tw):
    words, out, cur = text.split(), [], ""
    for w in words:
        trial = (cur + " " + w).strip()
        if cur and tw(trial, font) > width:
            out.append(cur)
            cur = w
        else:
            cur = trial
    if cur:
        out.append(cur)
    return out or [""]


def render(png_path, meta):
    """Returns (image, info) or (None, info) when nothing could be drawn."""
    info = {"regions": 0, "drawn": 0, "skipped": 0, "problems": [], "counts": {}}
    regions = meta.get("regions") or []
    info["regions"] = len(regions)
    src = Image.open(png_path).convert("RGB")
    geo, problems = geometry(meta, src.size)
    info["problems"] = problems
    if geo is None:
        return None, info
    dimmed = src.point(lambda v: int(v * kDimFactor))
    out = dimmed.copy()
    drawn = []
    for r in draw_order(regions):
        box = region_box(r, meta, geo, src.size)
        if box is None:
            info["skipped"] += 1
            continue
        prov = str(r.get("provenance", "retained"))
        colour = COLOUR.get(prov, UNKNOWN_COLOUR)
        tile = dimmed.crop(box)
        tile = Image.blend(tile, Image.new("RGB", tile.size, colour), kFillAlpha)
        out.paste(tile, box[:2])
        if prov == "not-implemented":
            hatch(out, box, geo[3])
        drawn.append((box, colour))
        info["counts"][prov] = info["counts"].get(prov, 0) + 1
    d = ImageDraw.Draw(out)
    lw = max(1, int(round(kOutlineDevicePx * geo[3])))
    for box, colour in drawn:
        x0, y0, x1, y1 = box
        d.rectangle((x0, y0, x1 - 1, y1 - 1), outline=colour, width=lw)
    info["drawn"] = len(drawn)
    title = "%s source map: %d region(s) drawn" % (pathlib.Path(png_path).stem, len(drawn))
    band = legend(src.size[0], geo[2], info["counts"], title, info["skipped"])
    final = Image.new("RGB", (src.size[0], src.size[1] + band.size[1]), kLegendBg)
    final.paste(out, (0, 0))
    final.paste(band, (0, src.size[1]))
    return final, info


def sidecars(root):
    for path in sorted(root.rglob("*.json")):
        if path.name.startswith("manifest") or path.parent == root:
            continue
        png = path.with_suffix(".png")
        if png.exists() and not png.name.endswith(SUFFIX):
            yield path


def output_path(png, root, out_dir):
    rel = png.relative_to(root)
    target = (out_dir or root) / rel
    return target.with_name(png.stem + SUFFIX)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render provenance source maps for capture sidecars.")
    ap.add_argument("root", help="a run root (baseline/ and proposed/) or a single capture.py run dir")
    ap.add_argument("--out", help="write overlays under this dir (same relative paths) instead of next to the captures")
    ap.add_argument("--force", action="store_true", help="re-render even when the overlay is newer than its inputs")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    root = pathlib.Path(args.root).resolve()
    out_dir = pathlib.Path(args.out).resolve() if args.out else None
    written = fresh = skipped_empty = failed = 0
    for js in sidecars(root):
        try:
            meta = json.loads(js.read_text())
        except (OSError, ValueError) as e:
            print("skip %s: %s" % (js, e), file=sys.stderr)
            failed += 1
            continue
        if not meta.get("regions"):
            continue
        png = js.with_suffix(".png")
        target = output_path(png, root, out_dir)
        if (not args.force and target.exists()
                and target.stat().st_mtime >= max(png.stat().st_mtime, js.stat().st_mtime)):
            fresh += 1
            continue
        img, info = render(png, meta)
        rel = png.relative_to(root)
        if img is None:
            print("FAILED %s: %s" % (rel, "; ".join(info["problems"])), file=sys.stderr)
            failed += 1
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        img.save(target, optimize=False)
        written += 1
        if not info["drawn"]:
            skipped_empty += 1
        if not args.quiet:
            extra = ("; " + "; ".join(info["problems"])) if info["problems"] else ""
            print("%s: %d drawn, %d skipped%s" % (rel, info["drawn"], info["skipped"], extra))
    print("sourcemap: %d written, %d up to date, %d with no visible region, %d failed" % (
        written, fresh, skipped_empty, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
