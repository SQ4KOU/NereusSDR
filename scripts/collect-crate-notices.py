#!/usr/bin/env python3
"""Write the notices of every Rust crate linked into DeepFilterNet (R-R3-50).

setup-deepfilter.sh and setup-deepfilter.ps1 build the DeepFilterNet
library (libdeepfilter.a, deepfilter.dll) from the pinned DeepFilterNet
commit with cargo. Every crate compiled into it has its own licence. This
script lists them from `cargo metadata` over the checkout's Cargo.lock:

  * starting at the library's package (deep_filter), it follows normal
    dependencies only; dev- and build-dependencies, and proc-macro crates,
    run on the build machine and are not linked in;
  * without --filter-platform every platform's dependencies are followed,
    so one file covers every package;
  * for each crate it writes the name, version and licence expression from
    its manifest, then the licence and notice files from its source
    directory (LICENSE*, LICENCE*, COPYING*, NOTICE*, and the manifest's
    license-file), byte for byte. A text identical to one already written
    is named rather than repeated.

Usage (what the setup scripts run after `cargo cbuild`; --offline means it
reads only what the build already fetched):
  python3 scripts/collect-crate-notices.py \\
      --manifest-path <DeepFilterNet>/Cargo.toml --package deep_filter \\
      --features deep_filter/capi --commit <sha> \\
      --output packaging/third-party-licenses/deepfilternet-crates.txt

Or from a saved `cargo metadata --format-version 1` output:
  python3 scripts/collect-crate-notices.py --metadata metadata.json ...

Exit 0 on success, 1 when cargo metadata fails or the package is missing.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

_NOTICE_NAME = re.compile(r"^(licen[cs]e|copying|notice)([-_.].*)?$", re.IGNORECASE)


def run_cargo_metadata(manifest: Path, features: str | None,
                       filter_platform: str | None) -> dict:
    cmd = ["cargo", "metadata", "--format-version", "1", "--locked", "--offline",
           "--manifest-path", str(manifest)]
    if features:
        cmd += ["--features", features]
    if filter_platform:
        cmd += ["--filter-platform", filter_platform]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"cargo metadata failed:\n{result.stderr}")
    return json.loads(result.stdout)


def linked_packages(metadata: dict, root_name: str) -> list[dict]:
    """Packages linked into root_name's library, root first, then by name."""
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    roots = [pid for pid, p in packages.items()
             if p["name"] == root_name and pid in nodes]
    if not roots:
        raise SystemExit(f"package {root_name} is not in the cargo metadata resolve")

    def is_proc_macro(package: dict) -> bool:
        kinds = [k for t in package.get("targets", []) for k in t.get("kind", [])]
        return bool(kinds) and all(k == "proc-macro" for k in kinds)

    seen: list[str] = []
    stack = list(roots)
    while stack:
        pid = stack.pop()
        if pid in seen:
            continue
        seen.append(pid)
        for dep in nodes[pid].get("deps", []):
            kinds = dep.get("dep_kinds") or [{"kind": None}]
            if not any(k.get("kind") is None for k in kinds):
                continue
            if is_proc_macro(packages[dep["pkg"]]):
                continue
            stack.append(dep["pkg"])

    root_set = set(roots)
    ordered = [packages[pid] for pid in roots] + sorted(
        (packages[pid] for pid in seen if pid not in root_set),
        key=lambda p: (p["name"], p["version"]))
    return ordered


def notice_files(package: dict) -> list[Path]:
    """Licence and notice files in the crate's source directory."""
    crate_dir = Path(package["manifest_path"]).parent
    found: list[Path] = []
    if crate_dir.is_dir():
        found = [p for p in sorted(crate_dir.iterdir())
                 if p.is_file() and _NOTICE_NAME.match(p.name)]
    extra = package.get("license_file")
    if extra:
        path = (crate_dir / extra).resolve()
        if path.is_file() and path not in [f.resolve() for f in found]:
            found.append(path)
    return found


def _read(path: Path) -> str:
    with open(path, encoding="utf-8", errors="surrogateescape", newline="") as fh:
        return fh.read()


def _source_label(package: dict) -> str:
    source = package.get("source")
    if source is None:
        return "DeepFilterNet workspace"
    if source.startswith("registry+"):
        return "crates.io"
    return source


def render(packages: list[dict], commit: str, command: str) -> str:
    rule = "=" * 72
    lines = [
        "DeepFilterNet Rust crate notices",
        "",
        "The DeepFilterNet library NereusSDR links (libdeepfilter.a on Linux and",
        "macOS, deepfilter.dll on Windows) is compiled from the Rust crates",
        "listed here. Each entry gives the crate, its version, the licence",
        "expression in its manifest, and the licence and notice files from its",
        "source, copied byte for byte. A text identical to one written earlier",
        "in this file is named instead of repeated.",
        "",
        f"Source: DeepFilterNet {commit}, crates as locked by its Cargo.lock.",
        "Every platform's dependencies are included, so some crates listed are",
        "compiled only into another platform's package.",
        f"Generated by: {command}",
        f"Crates: {len(packages)}",
        "",
    ]
    written: dict[str, str] = {}
    for package in packages:
        lines.append(rule)
        lines.append(f"{package['name']} {package['version']}")
        lines.append(f"Licence: {package.get('license') or '(none given in its manifest)'}")
        lines.append(f"Source: {_source_label(package)}")
        files = notice_files(package)
        if not files:
            lines.append("")
            lines.append("(no licence or notice file in the crate's source directory)")
        for path in files:
            text = _read(path)
            lines.append("")
            first = written.get(text)
            if first is not None:
                lines.append(f"---- {path.name}: identical to {first} above ----")
                continue
            written[text] = f"{path.name} of {package['name']} {package['version']}"
            lines.append(f"---- {path.name} ----")
            lines.append(text.rstrip("\n"))
        lines.append("")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--manifest-path", type=Path)
    source.add_argument("--metadata", type=Path,
                        help="saved `cargo metadata --format-version 1` output")
    parser.add_argument("--package", default="deep_filter")
    parser.add_argument("--features")
    parser.add_argument("--filter-platform")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)

    if args.metadata is not None:
        metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    else:
        metadata = run_cargo_metadata(args.manifest_path, args.features,
                                      args.filter_platform)
    packages = linked_packages(metadata, args.package)
    command = ("python3 scripts/collect-crate-notices.py --package "
               f"{args.package}" + (f" --features {args.features}" if args.features else ""))
    output = render(packages, args.commit, command)
    if args.output is None:
        sys.stdout.write(output)
    else:
        with open(args.output, "w", encoding="utf-8", errors="surrogateescape",
                  newline="") as fh:
            fh.write(output)
        print(f"{args.output}: {len(packages)} crates", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
