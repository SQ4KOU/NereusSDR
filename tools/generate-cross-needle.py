#!/usr/bin/env python3
"""
generate-cross-needle.py — NereusSDR Cross-Needle meter face generator
Copyright (C) 2026 NereusSDR Contributors
SPDX-License-Identifier: GPL-2.0-or-later

Produces two wholly-original PNG files:
  resources/meters/cross-needle.png      — main meter face (scale + background)
  resources/meters/cross-needle-bg.png   — bottom info strip (call-sign / labels)

The Cross-Needle (dual fwd/rev power) meter has two needles that pivot from
the bottom-left and bottom-right corners of the image, crossing at full-scale:
  - Forward power needle: pivots at (0.322, 0.611) of image, sweeps LEFT→RIGHT
  - Reverse power needle: pivots at (-0.322, 0.611) image-relative (i.e. the
    mirror image pivot just off the right edge), sweeps RIGHT→LEFT

Scale calibration coordinates are taken from the NereusSDR ItemGroup.cpp
CrossNeedle factory (which itself is derived from ItemGroup.cpp in the
NereusSDR source, not from any Thetis bitmap).  The background art rendered
here is ENTIRELY original NereusSDR artwork — no Thetis/OE3IDE skin image was
copied or derived.

Image dimensions follow the 1.0 × 0.782 aspect ratio that Thetis uses for the
meter group size (from MeterManager.cs AddCrossNeedle, ni.Size = (1, 0.782)).
We fix pixel width at 800 px; height = round(800 * 0.782) = 626 px.

The cross-needle-bg image covers the bottom 0.217 fraction of the height and
is 1.0 wide (800 × round(0.217 × 626) = 800 × 136 px).

Palette (matches StyleConstants.h / generate-meter-face.py):
  Background : #0f0f1a  (dark navy)
  Tick/labels: #c8d8e8  (light blue-grey)
  Accent     : #00b4d8  (cyan-blue)
  Red zone   : #e05050
  Panel lines: #1a2a3a
  Fwd needle : #e05050  (red trace guide)
  Rev needle : #5080e0  (cornflower blue trace guide)
"""

import math
import sys
import os
from pathlib import Path
from typing import Optional

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("ERROR: Pillow not installed. Run: pip3 install --user Pillow")

# ---------------------------------------------------------------------------
# Canvas constants
# ---------------------------------------------------------------------------
W        = 800
H        = round(W * 0.782)          # 626
BG_H     = round(H * 0.217)          # 136  (cross-needle-bg strip height)

BG_COLOR   = (0x0f, 0x0f, 0x1a)
TICK_COLOR = (0xc8, 0xd8, 0xe8)
ACCENT     = (0x00, 0xb4, 0xd8)
RED_ZONE   = (0xe0, 0x50, 0x50)
PANEL      = (0x1a, 0x2a, 0x3a)
DIM_TEXT   = (0x70, 0x90, 0xa8)
FWD_GUIDE  = (0xe0, 0x50, 0x50, 0x80)   # red, semi-transparent
REV_GUIDE  = (0x50, 0x80, 0xe0, 0x80)   # cornflower blue, semi-transparent


# ---------------------------------------------------------------------------
# Scale calibrations — normalised (0..1) image coordinates.
# Forward power: from ItemGroup.cpp CrossNeedle fwd needle ScaleCalibration.
# Reverse power: from ItemGroup.cpp CrossNeedle rev needle ScaleCalibration.
# ---------------------------------------------------------------------------
FWD_CAL = [
    (  0,  0.052, 0.732),
    (  5,  0.146, 0.528),
    ( 10,  0.188, 0.434),
    ( 15,  0.235, 0.387),
    ( 20,  0.258, 0.338),
    ( 25,  0.303, 0.313),
    ( 30,  0.321, 0.272),
    ( 35,  0.361, 0.257),
    ( 40,  0.381, 0.223),
    ( 50,  0.438, 0.181),
    ( 60,  0.483, 0.155),
    ( 70,  0.532, 0.130),
    ( 80,  0.577, 0.111),
    ( 90,  0.619, 0.098),
    (100,  0.662, 0.083),
]

REV_CAL = [
    (  0,   0.948, 0.740),
    (  0.25, 0.913, 0.700),
    (  0.5,  0.899, 0.638),
    (  0.75, 0.875, 0.594),
    (  1,    0.854, 0.538),
    (  2,    0.814, 0.443),
    (  3,    0.769, 0.400),
    (  4,    0.744, 0.351),
    (  5,    0.702, 0.321),
    (  6,    0.682, 0.285),
    (  7,    0.646, 0.268),
    (  8,    0.626, 0.234),
    (  9,    0.596, 0.228),
    ( 10,    0.569, 0.196),
    ( 12,    0.524, 0.166),
    ( 14,    0.476, 0.140),
    ( 16,    0.431, 0.121),
    ( 18,    0.393, 0.109),
    ( 20,    0.349, 0.098),
]


def px(nx: float, ny: float) -> tuple[int, int]:
    """Convert normalised 0..1 image coords to pixel coords."""
    return (round(nx * W), round(ny * H))


# ---------------------------------------------------------------------------
# Font loader (same strategy as generate-meter-face.py)
# ---------------------------------------------------------------------------
def get_font(size: int):
    search = [
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    ]
    for p in search:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                continue
    return ImageFont.load_default()


def draw_tick_at(draw: ImageDraw.ImageDraw,
                 px0: tuple, px1: tuple,
                 color, width: int = 2):
    """Draw a short tick between two pixel-space points."""
    draw.line([px0, px1], fill=color, width=width)


def label_near(draw: ImageDraw.ImageDraw,
               x: int, y: int,
               text: str, font,
               color, offset_x: int = 0, offset_y: int = 0):
    """Place centred text near pixel (x, y)."""
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    draw.text((x - tw // 2 + offset_x, y - th // 2 + offset_y),
              text, font=font, fill=color)


# ---------------------------------------------------------------------------
# Helper: draw scale marks along a calibration arc
# ---------------------------------------------------------------------------
def draw_scale_marks(draw: ImageDraw.ImageDraw,
                     cal: list,
                     pivot_nx: float, pivot_ny: float,
                     tick_inward: int,
                     major_every: set,
                     font,
                     tick_color,
                     label_color,
                     red_threshold: Optional[float] = None,
                     label_offset_x: int = 0,
                     label_offset_y: int = 0):
    """
    For each calibration point, draw a radial tick (inward from the point
    toward the pivot) and optionally a numeric label.
    """
    piv_x = pivot_nx * W
    piv_y = pivot_ny * H

    for (val, nx, ny) in cal:
        tip_x = nx * W
        tip_y = ny * H
        # Direction from tip toward pivot
        dx = piv_x - tip_x
        dy = piv_y - tip_y
        dist = math.hypot(dx, dy)
        if dist == 0:
            continue
        dx /= dist
        dy /= dist

        # Inner end of the tick (inward from the arc tip)
        is_major = val in major_every
        length = 18 if is_major else 9
        inner_x = tip_x + dx * length
        inner_y = tip_y + dy * length

        color = RED_ZONE if (red_threshold and val >= red_threshold) else tick_color
        draw.line([(tip_x, tip_y), (inner_x, inner_y)],
                  fill=color, width=3 if is_major else 1)

        # Label for major ticks
        if is_major:
            lbl_x = tip_x - dx * tick_inward
            lbl_y = tip_y - dy * tick_inward
            lbl = str(int(val)) if val == int(val) else str(val)
            label_near(draw, int(lbl_x), int(lbl_y), lbl, font,
                       label_color, label_offset_x, label_offset_y)


# ---------------------------------------------------------------------------
# Main meter face (cross-needle.png)
# ---------------------------------------------------------------------------
def generate_main(out_path: str):
    img  = Image.new("RGBA", (W, H), BG_COLOR + (255,))
    draw = ImageDraw.Draw(img)

    font_title = get_font(22)
    font_label = get_font(16)
    font_small = get_font(13)
    font_tiny  = get_font(11)

    # --- Background gradient ---
    for row in range(H):
        frac = row / H
        shade = int(0x18 * (1 - frac * 0.5))
        r = min(0x0f + shade, 0x22)
        g = min(0x0f + shade, 0x22)
        b = min(0x1a + shade, 0x2a)
        draw.line([(0, row), (W - 1, row)], fill=(r, g, b, 255))

    # --- Outer border ---
    draw.rectangle([(0, 0), (W - 1, H - 1)], outline=PANEL, width=2)

    # --- Title bar ---
    title_h = 36
    draw.rectangle([(1, 1), (W - 2, title_h)], fill=(0x08, 0x14, 0x22, 255))
    draw.line([(1, title_h), (W - 2, title_h)], fill=ACCENT, width=2)

    title_txt = "NereusSDR  Cross-Needle Power"
    bbox = draw.textbbox((0, 0), title_txt, font=font_title)
    tw = bbox[2] - bbox[0]
    draw.text(((W - tw) // 2, 8), title_txt, font=font_title, fill=ACCENT)

    # FWD / REV labels in title
    draw.text((8,  10), "FWD", font=font_label, fill=(0xe0, 0x90, 0x90))
    draw.text((W - 44, 10), "REV", font=font_label, fill=(0x90, 0xa8, 0xe0))

    # --- Guide arcs (subtle trace lines along the calibration arcs) ---
    # Forward arc: connect all calibration tip points with a thin polyline
    fwd_pts = [px(nx, ny) for (_, nx, ny) in FWD_CAL]
    for i in range(len(fwd_pts) - 1):
        draw.line([fwd_pts[i], fwd_pts[i+1]], fill=(0xe0, 0x70, 0x70, 160), width=2)

    # Reverse arc: connect all tip points
    rev_pts = [px(nx, ny) for (_, nx, ny) in REV_CAL]
    for i in range(len(rev_pts) - 1):
        draw.line([rev_pts[i], rev_pts[i+1]], fill=(0x70, 0x90, 0xe0, 160), width=2)

    # --- Forward power scale ---
    # Pivot at (0.322, 0.611).  Major ticks at 0, 10, 20, 30, 50, 70, 100 W.
    fwd_major = {0, 10, 20, 30, 50, 70, 100}
    draw_scale_marks(draw, FWD_CAL,
                     pivot_nx=0.322, pivot_ny=0.611,
                     tick_inward=28,
                     major_every=fwd_major,
                     font=font_small,
                     tick_color=TICK_COLOR,
                     label_color=TICK_COLOR,
                     red_threshold=None)

    # "W" legend near top of fwd arc
    draw.text((int(0.65 * W) + 4, int(0.08 * H)), "W", font=font_label, fill=DIM_TEXT)
    draw.text((int(0.03 * W),     int(0.70 * H)), "0", font=font_small,  fill=TICK_COLOR)

    # --- Reverse power scale ---
    # Pivot at (1-0.322, 0.611) = (0.678, 0.611) in normalised image coords.
    # (ItemGroup uses NeedleOffset = (-0.322, 0.611), meaning the pivot is at
    # image_x = 1.0 - 0.322 = 0.678 when x is flipped for CCW needle.)
    rev_major = {0, 1, 2, 5, 10, 20}
    draw_scale_marks(draw, REV_CAL,
                     pivot_nx=0.678, pivot_ny=0.611,
                     tick_inward=28,
                     major_every=rev_major,
                     font=font_small,
                     tick_color=TICK_COLOR,
                     label_color=TICK_COLOR,
                     red_threshold=5.0)

    draw.text((int(0.95 * W) - 14, int(0.70 * H)), "0", font=font_small, fill=TICK_COLOR)

    # --- Pivot dot indicators ---
    for (pnx, pny) in [(0.322, 0.611), (0.678, 0.611)]:
        cx, cy = int(pnx * W), int(pny * H)
        draw.ellipse([(cx - 8, cy - 8), (cx + 8, cy + 8)],
                     fill=ACCENT, outline=TICK_COLOR)

    # --- Division line separating meter face from bg strip ---
    div_y = H - BG_H
    draw.line([(0, div_y), (W, div_y)], fill=PANEL, width=2)

    # --- Legend row inside main face just above division ---
    legend_y = div_y - 22
    draw.text((8, legend_y), "FWD (W)", font=font_tiny, fill=(0xd0, 0x80, 0x80))
    rrev = "REV (W)"
    bb = draw.textbbox((0, 0), rrev, font=font_tiny)
    draw.text((W - (bb[2] - bb[0]) - 8, legend_y), rrev, font=font_tiny,
              fill=(0x80, 0xa0, 0xd0))

    # Save as RGB PNG (strip alpha)
    img_rgb = img.convert("RGB")
    img_rgb.save(out_path, "PNG")
    print(f"Saved {out_path}  ({W}x{H} RGB PNG)")


# ---------------------------------------------------------------------------
# Bottom background strip (cross-needle-bg.png)
# ---------------------------------------------------------------------------
def generate_bg(out_path: str):
    img  = Image.new("RGB", (W, BG_H), BG_COLOR)
    draw = ImageDraw.Draw(img)

    font_label = get_font(14)
    font_tiny  = get_font(11)

    # Gradient
    for row in range(BG_H):
        frac = row / BG_H
        shade = int(0x10 * frac)
        r = min(0x0f + shade, 0x1c)
        g = min(0x0f + shade, 0x1c)
        b = min(0x1a + shade, 0x24)
        draw.line([(0, row), (W - 1, row)], fill=(r, g, b))

    draw.rectangle([(0, 0), (W - 1, BG_H - 1)], outline=PANEL, width=2)
    draw.line([(0, 0), (W, 0)], fill=ACCENT, width=2)

    # Center text block
    center_x = W // 2
    center_y = BG_H // 2

    line1 = "Cross-Needle Power Meter"
    bb = draw.textbbox((0, 0), line1, font=font_label)
    draw.text((center_x - (bb[2] - bb[0]) // 2, center_y - 16),
              line1, font=font_label, fill=TICK_COLOR)

    line2 = "NereusSDR  •  GPL-2.0-or-later  •  KG4VCF"
    bb2 = draw.textbbox((0, 0), line2, font=font_tiny)
    draw.text((center_x - (bb2[2] - bb2[0]) // 2, center_y + 4),
              line2, font=font_tiny, fill=DIM_TEXT)

    img.save(out_path, "PNG")
    print(f"Saved {out_path}  ({W}x{BG_H} RGB PNG)")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    repo_root = Path(__file__).resolve().parent.parent
    meters_dir = repo_root / "resources" / "meters"

    generate_main(str(meters_dir / "cross-needle.png"))
    generate_bg(str(meters_dir / "cross-needle-bg.png"))
