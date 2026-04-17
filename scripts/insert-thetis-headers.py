#!/usr/bin/env python3
"""Insert the correct Thetis header template into files listed in
THETIS-PROVENANCE.md.

Reads PROVENANCE.md, for each derived-file row determines the variant
from the table, loads the matching template block from HEADER-TEMPLATES.md,
substitutes `<FILENAME>`, `<THETIS_SOURCE_PATHS>`, and `<PORT_DATE>`, and
injects the rendered header into the file if it does not already have
one (detected by presence of the 'Ported from' marker).

Idempotent: running twice does nothing on the second pass.

Usage:
    python3 scripts/insert-thetis-headers.py --variant thetis-samphire [--dry-run]
    python3 scripts/insert-thetis-headers.py --file src/core/WdspEngine.cpp [--dry-run]
    python3 scripts/insert-thetis-headers.py --all [--dry-run]
"""

import argparse
import re
import sys
from datetime import date
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PROVENANCE = REPO / "docs" / "attribution" / "THETIS-PROVENANCE.md"
TEMPLATES = REPO / "docs" / "attribution" / "HEADER-TEMPLATES.md"
ALREADY_HEADERED = "Ported from"


def load_templates():
    """Parse HEADER-TEMPLATES.md, return {variant_name: template_text}."""
    text = TEMPLATES.read_text()
    variants = {}
    current = None
    current_block = []
    in_code = False
    for line in text.splitlines():
        m = re.match(r"^## Variant \d+ — `([a-z0-9\-]+)`", line)
        if m:
            if current and current_block:
                variants[current] = "\n".join(current_block).rstrip() + "\n"
            current = m.group(1)
            current_block = []
            in_code = False
            continue
        if current is None:
            continue
        if line.startswith("```cpp"):
            in_code = True
            continue
        if line.startswith("```") and in_code:
            variants[current] = "\n".join(current_block).rstrip() + "\n"
            current_block = []
            current = None
            in_code = False
            continue
        if in_code:
            current_block.append(line)
    return variants


def parse_provenance():
    """Yield dicts: {file, sources, variant}."""
    text = PROVENANCE.read_text()
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 5:
            continue
        rel = cells[0].replace("`", "")
        if not (REPO / rel).is_file():
            continue
        src = cells[1]
        variant = cells[4].strip().lower()
        if variant not in ("thetis-samphire", "thetis-no-samphire",
                           "mi0bot", "multi-source"):
            continue
        yield {"file": rel, "source": src, "variant": variant}


def render(template, filename, sources):
    """Apply substitutions. `sources` is a list of Thetis paths."""
    src_block = "\n".join(f"//   {s}" for s in sources)
    out = template.replace("<FILENAME>", filename)
    out = out.replace("<THETIS_SOURCE_PATHS>", src_block)
    out = out.replace("<PORT_DATE>", date.today().isoformat())
    return out


def insert(path: Path, header: str, dry_run: bool):
    content = path.read_text()
    if ALREADY_HEADERED in content.splitlines()[0:120] if False else (
            ALREADY_HEADERED in "\n".join(content.splitlines()[:120])):
        return False
    # Preserve any leading `#pragma once` or include guard
    lines = content.splitlines()
    insert_at = 0
    for i, line in enumerate(lines[:10]):
        s = line.strip()
        if s.startswith("#pragma once") or s.startswith("#ifndef ") or \
                s.startswith("#define ") or s == "":
            insert_at = i + 1
            continue
        break
    new_content = "\n".join(lines[:insert_at]) + \
        ("\n" if insert_at else "") + header + "\n" + \
        "\n".join(lines[insert_at:]) + "\n"
    if dry_run:
        print(f"[DRY] would header: {path.relative_to(REPO)}")
        return True
    path.write_text(new_content)
    print(f"headered: {path.relative_to(REPO)}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", help="only process this variant")
    ap.add_argument("--file", help="only process this file path")
    ap.add_argument("--all", action="store_true", help="process all")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if not (args.variant or args.file or args.all):
        ap.error("pick one of --variant, --file, --all")
    templates = load_templates()
    touched = 0
    for row in parse_provenance():
        if args.file and row["file"] != args.file:
            continue
        if args.variant and row["variant"] != args.variant:
            continue
        template = templates.get(row["variant"])
        if not template:
            print(f"WARN: no template for variant {row['variant']}",
                  file=sys.stderr)
            continue
        sources = [s.strip() for s in row["source"].split(";") if s.strip()]
        rendered = render(template, row["file"], sources)
        if insert(REPO / row["file"], rendered, args.dry_run):
            touched += 1
    print(f"\n{touched} files touched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
