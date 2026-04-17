#!/usr/bin/env python3
"""Verify that every file declared in THETIS-PROVENANCE.md carries the
required Thetis license header markers. Exit 1 on any failure.

Verbatim-preservation model (Pass 5, 2026-04-17 onward): each NereusSDR
file's header must contain the upstream source's own top-of-file header
BYTE-FOR-BYTE. The verifier therefore only checks for anchor markers
that every Thetis-derived file will carry:

  1. "Ported from" — anchors the NereusSDR port-citation block
  2. "Thetis"      — upstream identity (present in all cited Thetis sources)
  3. "Copyright (C)" — every cited GPL/LGPL source carries a copyright line
  4. "General Public License" — matches both GPL and LGPL
  5. "Modification history (NereusSDR)" — anchors the per-file mod block

The Dual-Licensing Statement check was dropped in Pass 5: its presence is
now 100 % determined by whether the upstream source has one in its
verbatim header, so per-variant gating is redundant.

Files under `docs/attribution/` themselves are exempt (they document the
templates, they are not themselves derived source).
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PROVENANCE = REPO / "docs" / "attribution" / "THETIS-PROVENANCE.md"

REQUIRED_MARKERS = [
    "Ported from",
    "Thetis",
    "Copyright (C)",
    "General Public License",
    "Modification history (NereusSDR)",
]

# Header must appear within this many lines of top of file
HEADER_WINDOW = 160


def parse_provenance(text: str):
    """Yield (file_path, variant) tuples from the PROVENANCE.md tables.

    Table rows we care about look like:
      | src/core/WdspEngine.cpp | cmaster.cs | full | port | samphire | ... |

    We accept any column layout whose first cell is a repo-relative path
    that exists on disk. Variant is detected from a 'variant' or 'template'
    column keyword, or inferred as 'thetis-no-samphire' if unspecified.
    """
    rows = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if not cells:
            continue
        candidate = cells[0]
        if not candidate or candidate.lower() in ("nereussdr file", "file"):
            continue
        rel = candidate.replace("`", "")
        if not (REPO / rel).is_file():
            continue
        rows.append((rel, "plain"))
    return rows


def check_file(path: Path, variant: str):
    head = "\n".join(path.read_text(errors="replace").splitlines()[:HEADER_WINDOW])
    missing = [m for m in REQUIRED_MARKERS if m not in head]
    return missing


def main():
    if not PROVENANCE.is_file():
        print(f"ERROR: {PROVENANCE} not found", file=sys.stderr)
        return 2
    rows = parse_provenance(PROVENANCE.read_text())
    if not rows:
        print("ERROR: no derived-file rows parsed from PROVENANCE.md",
              file=sys.stderr)
        return 2
    failures = 0
    for rel, variant in rows:
        missing = check_file(REPO / rel, variant)
        if missing:
            failures += 1
            print(f"FAIL {rel} — missing: {', '.join(missing)}")
    total = len(rows)
    ok = total - failures
    print(f"\n{ok}/{total} files pass header check")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
