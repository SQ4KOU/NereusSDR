#!/usr/bin/env python3
"""Mechanically regenerate the two Thetis contributor audit docs:
   docs/attribution/thetis-contributor-index.md
   docs/attribution/thetis-inline-mods-index.md

Inputs:
   docs/attribution/thetis-author-tags.json  (corpus + human-edited names)
   ../Thetis/                                (ramdor upstream)
   ../mi0bot-Thetis/                         (HL2 fork)

Per-file output:
  Block — contributors named in the file's top copyright/license block
           (regex scan of first ~60 lines for `Copyright (C) YEARS
           Name (CALLSIGN)` patterns).
  Inline — callsigns from the corpus that appear in the file body
           (after the header-end heuristic), with line numbers.

Emits a "Generated mechanically by scripts/generate-contributor-indexes.py
on DATE from corpus at thetis-sha/mi0bot-sha" banner so anyone reading
the file knows it is machine-produced and where to edit the source of
truth (the corpus JSON).

Scope: every *.cs / *.c / *.h in both upstreams (not just the files
NereusSDR cites in PROVENANCE — broader is safer).

Usage:
    python3 scripts/generate-contributor-indexes.py
    python3 scripts/generate-contributor-indexes.py --check
       # generate to /tmp and diff against committed; exit 1 on drift.
"""
from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
THETIS_DIR = Path(os.environ.get(
    "NEREUS_THETIS_DIR", REPO.parent.parent.parent / "Thetis")).expanduser()
MI0BOT_DIR = Path(os.environ.get(
    "NEREUS_MI0BOT_DIR", REPO.parent.parent.parent / "mi0bot-Thetis")).expanduser()
CORPUS_PATH = REPO / "docs" / "attribution" / "thetis-author-tags.json"

CONTRIBUTOR_INDEX_PATH = REPO / "docs" / "attribution" / \
    "thetis-contributor-index.md"
INLINE_MODS_INDEX_PATH = REPO / "docs" / "attribution" / \
    "thetis-inline-mods-index.md"

SCAN_SUFFIXES = {".cs", ".c", ".h", ".cpp"}

# Header block detection mirrors verify-inline-tag-preservation.py
HEADER_TOKENS = re.compile(
    r"^\s*(?:#(?:include|define|ifdef|ifndef|pragma|if)\b"
    r"|using\s|namespace\s|class\s|public:|private:|protected:"
    r"|static\s|void\s|int\s|float\s|double\s|bool\s|enum\s"
    r"|struct\s|typedef\s|template\s|extern\s|inline\s|return\s)"
)

# Copyright line scan for Block attribution.
RE_COPYRIGHT = re.compile(
    r"Copyright\s*\([Cc]\)\s*"
    r"(?P<years>\d{4}(?:\s*[-–]\s*\d{4})?(?:\s*,\s*\d{4}(?:\s*[-–]\s*\d{4})?)*)"
    r"\s+(?P<name>[^,(\r\n]+?)"
    r"(?:\s*[,(]\s*(?P<callsign>[A-Z][A-Z0-9]{2,6})[,\s)])?"
)


def load_corpus():
    if not CORPUS_PATH.is_file():
        print(f"FATAL: {CORPUS_PATH} missing. Run "
              f"scripts/discover-thetis-author-tags.py first.",
              file=sys.stderr)
        sys.exit(2)
    return json.loads(CORPUS_PATH.read_text())


def find_header_end(text: list[str]) -> int:
    for idx, line in enumerate(text):
        if HEADER_TOKENS.match(line):
            return idx + 1
    return 1


def iter_upstream_files(base: Path):
    if not base.is_dir():
        return
    # Sort by POSIX-style relative path string so traversal order is
    # identical across OSes. Comparing `Path` objects directly is
    # case-insensitive on Windows and case-sensitive on Linux/macOS,
    # which silently changes the per-file iteration order — and
    # therefore the dict insertion order that drives index output.
    candidates = [p for p in base.rglob("*")
                  if p.is_file() and p.suffix in SCAN_SUFFIXES]
    candidates.sort(key=lambda p: p.relative_to(base).as_posix())
    yield from candidates


def scan_header_block(text: list[str], header_end: int):
    """Return list of (years, name, callsign_or_empty) for Copyright
    lines in the top-of-file comment block."""
    out = []
    scan_to = min(header_end, 80, len(text))
    for line in text[:scan_to]:
        m = RE_COPYRIGHT.search(line)
        if m:
            years = re.sub(r"\s+", " ", m.group("years")).strip()
            name = m.group("name").strip().rstrip("*/").strip()
            name = re.sub(r"\s*\(\s*$", "", name)  # trailing ( if regex split oddly
            callsign = m.group("callsign") or ""
            if name and len(name) > 2:
                out.append((years, name, callsign))
    # Dedupe while preserving order
    seen = set()
    deduped = []
    for row in out:
        key = (row[1], row[2])
        if key not in seen:
            seen.add(key)
            deduped.append(row)
    return deduped


def scan_inline_tags(text: list[str], header_end: int,
                     corpus_callsigns: list[str]):
    """Return dict callsign -> list of (line_num, raw_comment_snippet)
    for every inline hit of a corpus callsign below the header block.

    `corpus_callsigns` MUST be an ordered sequence (not a set) — the
    inner loop breaks after the first match, so iteration order
    determines which callsign claims a multi-tag line. A set's
    iteration order is hash-randomized between Python invocations,
    which would make the generator's output non-deterministic and
    cause the --check drift gate to flake."""
    hits = defaultdict(list)
    for idx in range(header_end - 1, len(text)):
        line = text[idx]
        if "//" not in line:
            continue
        tail = line.split("//", 1)[1]
        for cs in corpus_callsigns:
            if re.search(r"\b" + re.escape(cs) + r"\b", tail):
                snippet = ("//" + tail).strip()
                if len(snippet) > 140:
                    snippet = snippet[:137] + "..."
                hits[cs].append((idx + 1, snippet))
                break  # one callsign per line is enough for indexing
    return hits


def render_contributor_index(corpus, per_file_data) -> str:
    now = datetime.now(timezone.utc).isoformat(timespec="seconds")
    lines = []
    lines.append("# Thetis Contributor Index")
    lines.append("")
    lines.append("**Generated mechanically** by "
                 "`scripts/generate-contributor-indexes.py` on "
                 f"`{now}`")
    lines.append(f"from corpus `docs/attribution/thetis-author-tags.json` "
                 f"(thetis@`{corpus.get('thetis_sha')}`, "
                 f"mi0bot@`{corpus.get('mi0bot_sha')}`).")
    lines.append("")
    lines.append("**Do NOT hand-edit this file.** To add or correct a "
                 "contributor, edit `docs/attribution/thetis-author-tags.json` "
                 "and re-run the generator.")
    lines.append("")
    lines.append("Historical Pass 5/6a snapshot preserved at "
                 "`thetis-contributor-index-v020-snapshot.md`.")
    lines.append("")
    lines.append("## Callsign glossary")
    lines.append("")
    lines.append("| Callsign | Name | Role |")
    lines.append("|---|---|---|")
    for cs, meta in corpus["callsign_tags"].items():
        name = meta.get("name") or "*(unknown)*"
        role = (meta.get("role") or "").replace("|", "\\|")
        lines.append(f"| `{cs}` | {name} | {role} |")
    lines.append("")
    lines.append("## Per-file index")
    lines.append("")
    lines.append("Scope: every `.cs/.c/.h/.cpp` in upstream Thetis + mi0bot "
                 "fork. Each entry lists **Block** contributors (found in "
                 "the file's top Copyright block) and **Inline** "
                 "contributors (corpus callsigns found in the file body "
                 "with line numbers).")
    lines.append("")
    for (upstream, rel_path), data in per_file_data.items():
        if not data["block"] and not data["inline"]:
            continue  # skip files with no tracked contributors
        lines.append(f"### `{upstream}/{rel_path}`")
        if data["block"]:
            for years, name, callsign in data["block"]:
                tag = f" ({callsign})" if callsign else ""
                lines.append(f"- **Block:** {name}{tag} — {years}")
        else:
            lines.append("- Block: (none matched)")
        if data["inline"]:
            for cs in sorted(data["inline"].keys()):
                lines_hit = data["inline"][cs]
                meta = corpus["callsign_tags"].get(cs, {})
                name = meta.get("name") or "*(unknown)*"
                line_nums = ", ".join(str(ln) for ln, _ in lines_hit[:8])
                more = "" if len(lines_hit) <= 8 else \
                       f" (+{len(lines_hit) - 8} more)"
                lines.append(f"- **Inline:** `{cs}` ({name}) — "
                             f"line{'s' if len(lines_hit) > 1 else ''} "
                             f"{line_nums}{more}")
        lines.append("")
    return "\n".join(lines) + "\n"


def render_inline_mods_index(corpus, per_file_data) -> str:
    now = datetime.now(timezone.utc).isoformat(timespec="seconds")
    total_markers = sum(
        sum(len(v) for v in data["inline"].values())
        for data in per_file_data.values()
    )
    files_with_markers = sum(
        1 for data in per_file_data.values() if data["inline"]
    )
    distinct_callsigns = set()
    for data in per_file_data.values():
        distinct_callsigns.update(data["inline"].keys())

    lines = []
    lines.append("# Thetis Inline Mod Index")
    lines.append("")
    lines.append("**Generated mechanically** by "
                 "`scripts/generate-contributor-indexes.py` on "
                 f"`{now}`")
    lines.append(f"from corpus `docs/attribution/thetis-author-tags.json` "
                 f"(thetis@`{corpus.get('thetis_sha')}`, "
                 f"mi0bot@`{corpus.get('mi0bot_sha')}`).")
    lines.append("")
    lines.append("**Do NOT hand-edit this file.** To add or correct a "
                 "contributor, edit `docs/attribution/thetis-author-tags.json` "
                 "and re-run the generator.")
    lines.append("")
    lines.append("Historical Pass 6a snapshot preserved at "
                 "`thetis-inline-mods-index-v020-snapshot.md`.")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- Upstream sources scanned: {len(per_file_data)}")
    lines.append(f"- Files with ≥1 inline marker: {files_with_markers}")
    lines.append(f"- Total inline markers: {total_markers}")
    lines.append(f"- Distinct callsigns inline: {len(distinct_callsigns)}")
    lines.append("")
    lines.append("## Per-file markers")
    lines.append("")
    for (upstream, rel_path), data in per_file_data.items():
        if not data["inline"]:
            continue
        lines.append(f"### `{upstream}/{rel_path}`")
        lines.append("")
        for cs in sorted(data["inline"].keys()):
            hits = data["inline"][cs]
            meta = corpus["callsign_tags"].get(cs, {})
            name = meta.get("name") or "*(unknown)*"
            lines.append(f"- **`{cs}`** ({name}): {len(hits)} marker"
                         f"{'s' if len(hits) > 1 else ''}")
            for ln_num, snippet in hits[:20]:
                lines.append(f"  - L{ln_num}: `{snippet}`")
            if len(hits) > 20:
                lines.append(f"  - *(+{len(hits) - 20} more)*")
        lines.append("")
    return "\n".join(lines) + "\n"


def build_per_file_data(corpus):
    # Sorted list (NOT set) so scan_inline_tags' first-match-wins loop
    # is deterministic across Python invocations — set iteration order
    # is hash-randomized, which silently produced spurious drift in CI.
    callsigns = sorted(corpus["callsign_tags"].keys())
    data = {}
    for base, tag in ((THETIS_DIR, "thetis"), (MI0BOT_DIR, "mi0bot")):
        if not base.is_dir():
            continue
        for src in iter_upstream_files(base):
            try:
                text = src.read_text(encoding="utf-8",
                                     errors="replace").splitlines()
            except Exception:
                continue
            header_end = find_header_end(text)
            block = scan_header_block(text, header_end)
            inline = scan_inline_tags(text, header_end, callsigns)
            if not block and not inline:
                continue
            rel = src.relative_to(base).as_posix()
            data[(tag, rel)] = {"block": block, "inline": inline}
    return data


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="Generate to /tmp and diff against committed; "
                         "exit 1 on drift")
    args = ap.parse_args()

    if not THETIS_DIR.is_dir():
        print(f"FATAL: ../Thetis not found at {THETIS_DIR}",
              file=sys.stderr)
        return 2

    corpus = load_corpus()
    per_file = build_per_file_data(corpus)
    contrib_md = render_contributor_index(corpus, per_file)
    inline_md = render_inline_mods_index(corpus, per_file)

    if args.check:
        current_contrib = (CONTRIBUTOR_INDEX_PATH.read_text()
                           if CONTRIBUTOR_INDEX_PATH.is_file() else "")
        current_inline = (INLINE_MODS_INDEX_PATH.read_text()
                          if INLINE_MODS_INDEX_PATH.is_file() else "")
        # Strip the "generated_utc" banner line before diffing so
        # re-running the generator doesn't spuriously flag drift.
        def strip_ts(s):
            return "\n".join(ln for ln in s.splitlines()
                             if not ln.startswith("**Generated mechanically**")
                             and not ln.startswith("on `"))
        drifts = []
        if strip_ts(current_contrib) != strip_ts(contrib_md):
            drifts.append("thetis-contributor-index.md")
        if strip_ts(current_inline) != strip_ts(inline_md):
            drifts.append("thetis-inline-mods-index.md")
        if drifts:
            print(f"DRIFT: contributor indexes stale: {drifts}")
            print("Run: python3 scripts/generate-contributor-indexes.py")
            return 1
        print("[indexes] up to date")
        return 0

    CONTRIBUTOR_INDEX_PATH.write_text(contrib_md)
    INLINE_MODS_INDEX_PATH.write_text(inline_md)
    total_files = len(per_file)
    total_markers = sum(
        sum(len(v) for v in d["inline"].values())
        for d in per_file.values()
    )
    print(f"[generate-indexes] wrote {CONTRIBUTOR_INDEX_PATH.name}")
    print(f"[generate-indexes] wrote {INLINE_MODS_INDEX_PATH.name}")
    print(f"  {total_files} upstream files indexed")
    print(f"  {total_markers} inline markers catalogued")
    return 0


if __name__ == "__main__":
    sys.exit(main())
