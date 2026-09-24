#!/usr/bin/env python3
"""Collect the notices inside a vendored library's compiled sources (R-R3-50).

A BSD- or MIT-style licence asks a binary distribution to reproduce the
copyright notice and conditions. The licence text a library ships in
packaging/third-party-licenses/ carries the notice of its top-level
LICENSE or COPYING file; its source files can carry more (other holders,
other years, other licences). This script finds those.

For one library it:

  1. takes the source files the NereusSDR build compiles into a shipped
     binary (the preset below says where the list comes from), and every
     header they include from the library's own tree;
  2. extracts each notice block: a comment (or run of adjacent comments)
     holding a copyright line, from that line to the end of the comment,
     byte for byte. Comments NereusSDR wrote (modification histories, port
     notes, NereusSDR-original file headers: any comment naming NereusSDR
     or KG4VCF) are left out, so the file holds upstream notices only;
  3. drops blocks the library's licence text already carries: every
     copyright line in the block appears in the text, and the rest of the
     block is either in the text or holds no licence terms;
  4. writes each remaining distinct block once, with the files it covers.

Blocks that differ only in whitespace and comment markers count as one; the
text printed is the block as it appears in the first file listed under it.

Usage:
  python3 scripts/collect-source-notices.py LIBRARY [--root PATH]
      [--build-dir PATH] [--opus-source PATH] [--output FILE | --check FILE]
      [--survey]

LIBRARY is one of the presets: fftw3, rade, opus, r8brain, rnnoise,
libspecbleach, wdsp, and the fetched libraries portaudio, libdatachannel,
libjuice, usrsctp, libsrtp, plog and nlohmann-json. rnnoise and
libspecbleach read the FetchContent sources under --build-dir/_deps. The
fetched libraries need --build-dir to be a built tree: they read its
compile_commands.json for the files the build compiled and each file's
include path (libjuice, usrsctp, libsrtp, plog and json are built from
the copies under nereus_libdatachannel-src/deps/). opus needs
--opus-source, an extracted Opus source tree at the pinned commit (a
built tree has one at
<build>/third_party/rade/build_opus-prefix/src/build_opus).

With --check FILE the script exits 1 when FILE differs from what it would
write. With neither --output nor --check it prints the file to stdout.
--survey prints per-library counts to stderr.

Exit 0 on success, 1 on a --check mismatch, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import sys
from dataclasses import dataclass, field
from pathlib import Path

LICENSE_DIR = Path("packaging/third-party-licenses")

# A copyright line: "Copyright (c) 2024", "Copyright 2001-2023 Xiph.Org",
# "Copyright: (C) 2010", "(c) 2003 Mark". "COPYRIGHT HOLDERS" and
# "copyright notice" in a licence's conditions are not copyright lines.
_COPYRIGHT_RE = re.compile(
    r"(?i)(\bcopyright\b\s*:?\s*(\(c\)|©|\d{4})|\(c\)\s*\d{4})")

# Words that mark licence terms. A block whose non-copyright text holds one
# of these and is not already in the library's text is not carried by it.
_LICENCE_MARKERS = ("redistribut", "permission", "warrant", "public license",
                    "licensed under", "spdx-license-identifier",
                    "free software")

# A holder line continuing a copyright statement: "   2012-2017 Jean-Marc
# Valin */" under a "Copyright (c) ..." line.
_CONTINUATION_RE = re.compile(r"^\d{4}(\s*[-,]\s*\d{4})*\b")

# String and character literals, then block and line comments. An
# unterminated block comment runs to the end of the file.
_TOKEN_RE = re.compile(
    r'"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])*'"
    r"|/\*.*?(?:\*/|\Z)"
    r"|//[^\n]*",
    re.DOTALL)

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


# ---------------------------------------------------------------------------
# Comments and notice blocks

def read_source(path: Path) -> str:
    """A file's text with its bytes kept: line endings are not translated
    and bytes that are not UTF-8 round-trip through surrogateescape."""
    with open(path, encoding="utf-8", errors="surrogateescape", newline="") as fh:
        return fh.read()


def _comment_groups(text: str) -> list[list[tuple[int, int]]]:
    """Every run of adjacent C/C++ comments, as a list of comment units.

    A unit is one block comment, or a run of line comments with no blank
    line between them. Units in a run have only whitespace between them.
    String and character literals are matched first so a "/*" inside one
    opens no comment.
    """
    units: list[tuple[int, int]] = []
    for match in _TOKEN_RE.finditer(text):
        if not match.group(0).startswith("/"):
            continue
        start, end = match.start(), match.end()
        if (units and match.group(0).startswith("//")
                and text[units[-1][0]:units[-1][0] + 2] == "//"
                and text[units[-1][1]:start].strip() == ""
                and text[units[-1][1]:start].count("\n") <= 1):
            units[-1] = (units[-1][0], end)
        else:
            units.append((start, end))

    groups: list[list[tuple[int, int]]] = []
    for unit in units:
        if groups and text[groups[-1][-1][1]:unit[0]].strip() == "":
            groups[-1].append(unit)
        else:
            groups.append([unit])
    return groups


def is_nereussdr_comment(comment: str) -> bool:
    """True for a comment NereusSDR wrote: a modification history, a port
    note, a NereusSDR-original file header. Upstream code never names
    NereusSDR or its maintainer's callsign."""
    return "NereusSDR" in comment or "KG4VCF" in comment


def extract_blocks(text: str) -> list[str]:
    """Every upstream notice block in a source file, byte for byte.

    Comments NereusSDR wrote are left out: a run of comments is cut at
    each of them, and the pieces either side are read separately.
    """
    blocks: list[str] = []
    for group in _comment_groups(text):
        pieces: list[list[tuple[int, int]]] = [[]]
        for start, end in group:
            if is_nereussdr_comment(text[start:end]):
                pieces.append([])
            else:
                pieces[-1].append((start, end))
        for piece in pieces:
            if not piece:
                continue
            comment = text[piece[0][0]:piece[-1][1]]
            match = _COPYRIGHT_RE.search(comment)
            if not match:
                continue
            line_start = comment.rfind("\n", 0, match.start()) + 1
            blocks.append(comment[line_start:])
    return blocks


def _strip_markers(text: str) -> str:
    out = []
    for line in text.splitlines():
        line = line.strip()
        line = re.sub(r"^(/\*+|\*+/|\*+|//+|#)", "", line)
        line = re.sub(r"\*+/$", "", line)
        out.append(line)
    return "\n".join(out)


def normalise(text: str) -> str:
    """Text with comment markers dropped and whitespace collapsed."""
    return " ".join(_strip_markers(text).split())


def _statement(line: str) -> str:
    """A copyright line reduced to what it states: holder and years."""
    match = _COPYRIGHT_RE.search(line)
    body = line[match.start():] if match else line
    body = normalise(body).lower()
    body = re.sub(r"\(c\)|©", " ", body)
    body = re.sub(r"[,.;:]", " ", body)
    return " ".join(body.split())


def _is_statement(line: str) -> bool:
    return bool(_COPYRIGHT_RE.search(line) or _CONTINUATION_RE.match(line))


def is_carried(block: str, licence_texts: list[str]) -> bool:
    """True when the library's licence texts already carry this block."""
    statements = [_statement(line) for line in _strip_markers(block).splitlines()
                  if _is_statement(line)]
    lowered = [" ".join(re.sub(r"\(c\)|©|[,.;:]", " ",
                               normalise(t).lower()).split())
               for t in licence_texts]
    for statement in statements:
        if not any(statement in text for text in lowered):
            return False

    rest_lines = [line for line in _strip_markers(block).splitlines()
                  if not _is_statement(line)]
    rest = " ".join(" ".join(rest_lines).split())
    if not rest:
        return True
    plain = [normalise(t) for t in licence_texts]
    if any(rest in text for text in plain):
        return True
    return not any(marker in rest.lower() for marker in _LICENCE_MARKERS)


# ---------------------------------------------------------------------------
# Source sets

def resolve_includes(sources: list[Path], include_dirs: list[Path]) -> list[Path]:
    """The sources plus every header they include that exists in the tree.

    Headers are looked up beside the including file, then in include_dirs.
    Headers not found there (system and other libraries' headers) are
    skipped. Conditional includes are all followed, so the set covers
    every platform branch.
    """
    seen: dict[Path, None] = {}
    stack = [p.resolve() for p in sources]
    while stack:
        path = stack.pop()
        if path in seen or not path.is_file():
            continue
        seen[path] = None
        text = read_source(path)
        for name in _INCLUDE_RE.findall(text):
            for base in [path.parent, *include_dirs]:
                candidate = (base / name).resolve()
                if candidate.is_file():
                    stack.append(candidate)
                    break
    return sorted(seen)


def cmake_list(cmake_text: str, command: str, name: str) -> list[str]:
    """Words of `command(name ...)` in a CMake file, comments dropped."""
    body = "\n".join(line.split("#", 1)[0] for line in cmake_text.splitlines())
    match = re.search(rf"\b{command}\s*\(\s*{re.escape(name)}\b([^)]*)\)", body)
    if not match:
        raise SystemExit(f"{command}({name} ...) not found")
    return match.group(1).split()


def mk_list(mk_text: str, variable: str) -> list[str]:
    """Words of an automake `VARIABLE = ...` assignment with continuations."""
    joined = mk_text.replace("\\\n", " ")
    match = re.search(rf"^{re.escape(variable)}\s*=(.*)$", joined, re.MULTILINE)
    if not match:
        raise SystemExit(f"{variable} not found in the Opus source lists")
    return match.group(1).split()


@dataclass
class SourceSet:
    library: str
    base: Path                 # paths in the output are relative to this
    label: str                 # how the output names the base
    pin: str
    files: list[Path]
    licence_files: list[str]   # names in packaging/third-party-licenses/
    extra: list[tuple[Path, str]] = field(default_factory=list)  # (file, label)
    regen: str = ""            # extra arguments the regeneration command needs


def _rade(root: Path, _args: argparse.Namespace) -> SourceSet:
    src = root / "third_party/rade/src"
    text = (src / "CMakeLists.txt").read_text(encoding="utf-8")
    words = cmake_list(text, "add_library", "rade")
    dsp = cmake_list(text, "set", "RADE_DSP_SOURCES")
    names: list[str] = []
    for word in words:
        if word == "${RADE_DSP_SOURCES}":
            names.extend(dsp)
        elif word.endswith(".c"):
            names.append(word)
    files = resolve_includes([src / n for n in names], [src])
    return SourceSet("RADE", src, "third_party/rade/src", "radae_nopy b2891023",
                     files, ["rade.txt"])


# Opus source-list variables the RADE Opus build compiles: float build,
# DRED and OSCE on (DRED brings the deep PLC sources), and the x86 and ARM
# intrinsics variants, since the packages span both. The fixed-point, NE10
# and external ARM assembly lists are not built.
_OPUS_VARIABLES = {
    "celt_sources.mk": ["CELT_SOURCES", "CELT_SOURCES_X86_RTCD", "CELT_SOURCES_SSE",
                        "CELT_SOURCES_SSE2", "CELT_SOURCES_SSE4_1", "CELT_SOURCES_AVX2",
                        "CELT_SOURCES_ARM_RTCD", "CELT_SOURCES_ARM_NEON_INTR"],
    "silk_sources.mk": ["SILK_SOURCES", "SILK_SOURCES_FLOAT", "SILK_SOURCES_X86_RTCD",
                        "SILK_SOURCES_SSE4_1", "SILK_SOURCES_AVX2",
                        "SILK_SOURCES_FLOAT_AVX2", "SILK_SOURCES_ARM_RTCD",
                        "SILK_SOURCES_ARM_NEON_INTR"],
    "opus_sources.mk": ["OPUS_SOURCES", "OPUS_SOURCES_FLOAT"],
    "lpcnet_sources.mk": ["DEEP_PLC_SOURCES", "DRED_SOURCES", "OSCE_SOURCES",
                          "DNN_SOURCES_X86_RTCD", "DNN_SOURCES_AVX2",
                          "DNN_SOURCES_SSE4_1", "DNN_SOURCES_SSE2",
                          "DNN_SOURCES_ARM_RTCD", "DNN_SOURCES_DOTPROD",
                          "DNN_SOURCES_NEON"],
}


def _opus(_root: Path, args: argparse.Namespace) -> SourceSet:
    if args.opus_source is None:
        raise SystemExit("opus needs --opus-source (an extracted Opus tree)")
    base = args.opus_source.resolve()
    names: list[str] = []
    for mk, variables in _OPUS_VARIABLES.items():
        text = (base / mk).read_text(encoding="utf-8")
        for variable in variables:
            names.extend(mk_list(text, variable))
    # Include path from Opus's Makefile.am AM_CPPFLAGS.
    include_dirs = [base / d for d in ("include", "celt", "silk", "silk/float",
                                       "silk/fixed", "dnn")] + [base]
    files = resolve_includes([base / n for n in names], include_dirs)
    # Headers for MIPS and Xtensa builds, reached through conditional
    # includes; no NereusSDR package targets those processors.
    files = [f for f in files
             if not re.search(r"/(mips|xtensa)/", f.relative_to(base).as_posix())]
    return SourceSet("Opus", base, "opus", "940d4e5a", files, ["opus.txt"],
                     regen=" --opus-source <build>/third_party/rade/build_opus-prefix/src/build_opus")


def _r8brain(root: Path, _args: argparse.Namespace) -> SourceSet:
    base = root / "third_party/r8brain"
    files = resolve_includes([base / "r8bbase.cpp"], [base])
    # r8bbase.cpp is NereusSDR's own compilation anchor, not upstream code.
    files = [f for f in files if f.name != "r8bbase.cpp"]
    return SourceSet("r8brain-free-src", base, "third_party/r8brain", "5c44bebe",
                     files, ["r8brain.txt"])


def _deps(args: argparse.Namespace, name: str) -> Path:
    if args.build_dir is None:
        raise SystemExit("this library needs --build-dir (a configured build)")
    path = (args.build_dir / "_deps" / f"{name}-src").resolve()
    if not path.is_dir():
        raise SystemExit(f"{path} is missing; configure the build first")
    return path


def _rnnoise(root: Path, args: argparse.Namespace) -> SourceSet:
    base = _deps(args, "rnnoise_upstream")
    excluded = re.compile(
        r"/(dump_features|dump_rnnoise_tables|write_weights|rnnoise_data|"
        r"rnnoise_data_little)\.c$")
    sources = [p for p in sorted((base / "src").glob("*.c"))
               if not excluded.search(p.as_posix())]
    files = resolve_includes(sources, [base / "include", base / "src"])
    shim = root / "third_party/rnnoise/rnnoise_model_init.c"
    return SourceSet("rnnoise", base, "rnnoise", "70f1d256", files,
                     ["rnnoise.txt"], extra=[(shim, "third_party/rnnoise/rnnoise_model_init.c")],
                     regen=" --build-dir <a configured build>")


def _libspecbleach(_root: Path, args: argparse.Namespace) -> SourceSet:
    base = _deps(args, "libspecbleach_upstream")
    sources = [p for p in sorted((base / "src").rglob("*.c"))
               if not re.search(r"/(test|example|demo)", p.as_posix())]
    files = resolve_includes(sources, [base / "include", base / "src"])
    return SourceSet("libspecbleach", base, "libspecbleach", "41d3f583", files,
                     ["libspecbleach.txt", "LGPLv2.1.txt"],
                     regen=" --build-dir <a configured build>")


def _wdsp(root: Path, _args: argparse.Namespace) -> SourceSet:
    base = root / "third_party/wdsp"
    text = (base / "CMakeLists.txt").read_text(encoding="utf-8")
    names = cmake_list(text, "set", "WDSP_SOURCES")
    files = resolve_includes([base / n for n in names], [base / "src"])
    return SourceSet("WDSP", base, "third_party/wdsp", "TAPR v1.29 with NereusSDR changes",
                     files, ["wdsp.txt", "GPLv2.txt"])


def _compile_entries(build: Path) -> list[tuple[Path, list[Path]]]:
    """(source file, include directories) for every compile command."""
    path = build / "compile_commands.json"
    if not path.is_file():
        raise SystemExit(f"{path} is missing; configure and build the tree first")
    entries: list[tuple[Path, list[Path]]] = []
    for entry in json.loads(path.read_text(encoding="utf-8")):
        args = entry.get("arguments") or shlex.split(entry["command"])
        directory = Path(entry["directory"])
        dirs: list[Path] = []
        i = 0
        while i < len(args):
            arg = args[i]
            value = None
            for flag in ("-I", "-isystem", "-iquote"):
                if arg == flag and i + 1 < len(args):
                    value = args[i + 1]
                    i += 1
                elif arg.startswith(flag) and len(arg) > len(flag) and flag == "-I":
                    value = arg[2:]
            if value is not None:
                dirs.append((directory / value).resolve())
            i += 1
        entries.append(((directory / entry["file"]).resolve(), dirs))
    return entries


def _within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _from_build(build: Path, keep: Path, seeds: Path,
                extra: list[str] | None = None,
                extra_dirs: list[str] | None = None) -> list[Path]:
    """Files under keep that the build compiled, or that the compiled files
    under seeds include, resolved with each command's own include path.
    extra adds sources (relative to keep) other platforms compile."""
    keep = keep.resolve()
    seeds = seeds.resolve()
    found: set[Path] = set()
    for source, dirs in _compile_entries(build):
        if not _within(source, seeds):
            continue
        for path in resolve_includes([source], dirs):
            if _within(path, keep):
                found.add(path)
    if extra:
        dirs = [keep / d for d in (extra_dirs or [])]
        for path in resolve_includes([keep / name for name in extra], dirs):
            if _within(path, keep):
                found.add(path)
    return sorted(found)


def _need_build(args: argparse.Namespace) -> Path:
    if args.build_dir is None:
        raise SystemExit("this library needs --build-dir (a built tree)")
    return args.build_dir.resolve()


# PortAudio host API and platform sources the Windows and Linux packages
# compile (portaudio-src/CMakeLists.txt at v19.7.0 with NereusSDR's options:
# ASIO off; MME, DirectSound, WASAPI and WDM-KS on Windows; ALSA and JACK
# on Linux). A macOS build tree lists only the CoreAudio ones.
_PORTAUDIO_OTHER_PLATFORMS = [
    "src/os/win/pa_win_hostapis.c", "src/os/win/pa_win_util.c",
    "src/os/win/pa_win_waveformat.c", "src/os/win/pa_win_wdmks_utils.c",
    "src/os/win/pa_win_coinitialize.c", "src/os/win/pa_x86_plain_converters.c",
    "src/hostapi/dsound/pa_win_ds.c", "src/hostapi/dsound/pa_win_ds_dynlink.c",
    "src/hostapi/wmme/pa_win_wmme.c", "src/hostapi/wasapi/pa_win_wasapi.c",
    "src/hostapi/wdmks/pa_win_wdmks.c",
    "src/os/unix/pa_unix_hostapis.c", "src/os/unix/pa_unix_util.c",
    "src/hostapi/alsa/pa_linux_alsa.c", "src/hostapi/jack/pa_jack.c",
]


def _portaudio(_root: Path, args: argparse.Namespace) -> SourceSet:
    build = _need_build(args)
    base = build / "_deps/portaudio-src"
    files = _from_build(build, base, base, _PORTAUDIO_OTHER_PLATFORMS,
                        ["include", "src/common", "src/os/win", "src/os/unix"])
    return SourceSet("PortAudio", base, "portaudio", "v19.7.0", files,
                     ["portaudio.txt"], regen=" --build-dir <a built tree>")


def _datachannel_dep(name: str, library: str, pin: str, texts: list[str]):
    """A library libdatachannel builds from its deps/<name> copy.

    NereusRemoteMedia.cmake copies each fetched source into
    nereus_libdatachannel-src/deps/<name> and builds it from there; the
    header-only ones (plog, json) are reached through libdatachannel's
    includes, so every compiled libdatachannel file seeds the search."""
    def preset(_root: Path, args: argparse.Namespace) -> SourceSet:
        build = _need_build(args)
        dc = build / "_deps/nereus_libdatachannel-src"
        base = dc / "deps" / name
        files = _from_build(build, base, dc)
        return SourceSet(library, base, name, pin, files, texts,
                         regen=" --build-dir <a built tree>")
    return preset


def _libdatachannel(_root: Path, args: argparse.Namespace) -> SourceSet:
    build = _need_build(args)
    base = build / "_deps/nereus_libdatachannel-src"
    files = [f for f in _from_build(build, base, base)
             if not _within(f, (base / "deps").resolve())]
    return SourceSet("libdatachannel", base, "libdatachannel", "v0.24.5", files,
                     ["libdatachannel.txt", "MPLv2.txt"],
                     regen=" --build-dir <a built tree>")


def _fftw3(root: Path, _args: argparse.Namespace) -> SourceSet:
    # NereusSDR compiles against FFTW's public header; the library itself
    # arrives prebuilt (Windows DLL) or from the system. The header in
    # third_party/fftw3 is the one the Windows build uses.
    base = root / "third_party/fftw3"
    return SourceSet("FFTW3", base, "third_party/fftw3", "3.3.5 (Windows header)",
                     [base / "include/fftw3.h"], ["fftw3.txt", "GPLv2.txt"])


PRESETS = {
    "fftw3": _fftw3,
    "portaudio": _portaudio,
    "libdatachannel": _libdatachannel,
    "libjuice": _datachannel_dep("libjuice", "libjuice", "3c40a354",
                                 ["libjuice.txt", "MPLv2.txt"]),
    "usrsctp": _datachannel_dep("usrsctp", "usrsctp", "fec583d5", ["usrsctp.txt"]),
    "libsrtp": _datachannel_dep("libsrtp", "libsrtp", "24b3bf8f", ["libsrtp.txt"]),
    "plog": _datachannel_dep("plog", "plog", "94899e0b", ["plog.txt"]),
    "nlohmann-json": _datachannel_dep("json", "nlohmann json", "55f93686",
                                      ["nlohmann-json.txt"]),
    "rade": _rade,
    "opus": _opus,
    "r8brain": _r8brain,
    "rnnoise": _rnnoise,
    "libspecbleach": _libspecbleach,
    "wdsp": _wdsp,
}


# ---------------------------------------------------------------------------
# Output

@dataclass
class Notice:
    text: str
    files: list[str]


def collect(files: list[tuple[Path, str]], licence_texts: list[str]
            ) -> tuple[list[Notice], dict[str, int]]:
    """Distinct uncarried notices over (path, display name) pairs."""
    by_key: dict[str, Notice] = {}
    counts = {"files": 0, "files_with_notice": 0, "blocks": 0,
              "distinct": 0, "carried": 0}
    distinct: set[str] = set()
    for path, display in files:
        counts["files"] += 1
        text = read_source(path)
        blocks = extract_blocks(text)
        if blocks:
            counts["files_with_notice"] += 1
        for block in blocks:
            counts["blocks"] += 1
            key = normalise(block)
            if key not in distinct:
                distinct.add(key)
                if is_carried(block, licence_texts):
                    counts["carried"] += 1
            if is_carried(block, licence_texts):
                continue
            notice = by_key.setdefault(key, Notice(block, []))
            if display not in notice.files:
                notice.files.append(display)
    counts["distinct"] = len(distinct)
    return list(by_key.values()), counts


def render(source_set: SourceSet, notices: list[Notice], library_arg: str) -> str:
    rule = "=" * 72
    texts = " and ".join(source_set.licence_files)
    lines = [
        f"{source_set.library} source notices",
        "",
        f"These copyright and licence notices appear in {source_set.library} source",
        "files that NereusSDR compiles into its programs, and are not carried",
        f"by {texts}. Each notice appears once, exactly as it is",
        "written in the first file listed under it; the other files listed",
        "carry the same notice.",
        "",
        f"Source: {source_set.library} {source_set.pin}. File paths are relative to",
        f"{source_set.label}/ unless shown in full.",
        f"Generated by: python3 scripts/collect-source-notices.py {library_arg}"
        f"{source_set.regen}",
        "",
    ]
    for index, notice in enumerate(notices, 1):
        lines.append(rule)
        lines.append(f"Notice {index} of {len(notices)}, in:")
        lines.extend(f"  {name}" for name in notice.files)
        lines.append("-" * 72)
        lines.append(notice.text.rstrip("\n"))
        lines.append("")
    return "\n".join(lines) + "\n"


def build(library: str, root: Path, args: argparse.Namespace
          ) -> tuple[SourceSet, list[Notice], dict[str, int]]:
    source_set = PRESETS[library](root, args)
    pairs = [(f, f.relative_to(source_set.base).as_posix()) for f in source_set.files]
    pairs += source_set.extra
    licence_texts = [(root / LICENSE_DIR / name).read_text(encoding="utf-8")
                     for name in source_set.licence_files]
    notices, counts = collect(pairs, licence_texts)
    return source_set, notices, counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("library", choices=sorted(PRESETS))
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--opus-source", type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--output", type=Path)
    group.add_argument("--check", type=Path)
    parser.add_argument("--survey", action="store_true")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    source_set, notices, counts = build(args.library, root, args)
    if args.survey:
        print(f"{args.library}: {counts['files']} files, "
              f"{counts['files_with_notice']} with a notice, "
              f"{counts['blocks']} blocks, {counts['distinct']} distinct, "
              f"{counts['carried']} carried by the licence text, "
              f"{len(notices)} to list", file=sys.stderr)
    output = render(source_set, notices, args.library) if notices else ""

    if args.check is not None:
        current = read_source(args.check) if args.check.is_file() else ""
        if current != output:
            print(f"{args.check} is out of date; rerun with --output", file=sys.stderr)
            return 1
        return 0
    if args.output is not None:
        if output:
            with open(args.output, "w", encoding="utf-8",
                      errors="surrogateescape", newline="") as fh:
                fh.write(output)
        else:
            print(f"{args.library}: nothing to list, no file written", file=sys.stderr)
        return 0
    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
