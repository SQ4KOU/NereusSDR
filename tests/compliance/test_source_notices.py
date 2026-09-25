"""Tests for scripts/collect-source-notices.py (R-R3-50).

These use small C sources written into tmp_path, then run the presets that
need only files in this repository against the committed *-notices.txt
files, so a vendor update that changes a notice fails here until the file
is regenerated.
"""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent.parent
SOURCE_SCRIPT = REPO / "scripts" / "collect-source-notices.py"


def _load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


sn = _load(SOURCE_SCRIPT, "collect_source_notices")

BSD_CONDITIONS = """\
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
"""

LICENCE_TEXT = """\
Copyright (c) 2020, Example Org

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
"""


# --------------------------------------------------------------------------
# Notice blocks

def test_block_runs_from_the_copyright_line_across_adjacent_comments():
    text = ("/* foo.c: a file */\n"
            "/* Copyright (c) 2022 Amazon\n   Written by Someone */\n"
            "/*\n" + BSD_CONDITIONS + "*/\n\n#include \"foo.h\"\n")
    blocks = sn.extract_blocks(text)
    assert len(blocks) == 1
    assert blocks[0].startswith("/* Copyright (c) 2022 Amazon\n")
    assert blocks[0].endswith("are met:\n*/")
    assert "#include" not in blocks[0]


def test_block_keeps_bytes_and_crlf(tmp_path):
    src = tmp_path / "crlf.h"
    src.write_bytes(b"/*\r\n * Copyright (c) 2013 Caf\xc3\xa9 Ltd\r\n */\r\nint x;\r\n")
    blocks = sn.extract_blocks(sn.read_source(src))
    assert blocks == [" * Copyright (c) 2013 Café Ltd\r\n */"]


def test_licence_conditions_alone_are_not_a_notice():
    text = "/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS */\nint x;\n"
    assert sn.extract_blocks(text) == []


def test_comment_opener_inside_a_string_is_ignored():
    text = 'const char *s = "/* Copyright (c) 1999 Nobody";\nint y;\n'
    assert sn.extract_blocks(text) == []


def test_nereussdr_modification_history_is_left_out():
    # The WDSP layout: upstream header, the upstream dual-licensing
    # statement right under it, a NereusSDR modification history after a
    # blank line, then another upstream header straight after that.
    upstream = ("/*  cfcomp.c\n\nCopyright (C) 2017, 2021 Warren Pratt, NR0V\n\n"
                "This program is free software; you can redistribute it.\n*/\n"
                "//\n"
                "// Dual-Licensing Statement (Richard Samphire MW0LGE)\n"
                "// reserves the right to license his code under other terms.\n")
    history = ("\n"
               "//\n"
               "// =============================================================\n"
               "// Modification history (NereusSDR):\n"
               "//   2026-04-30 - Synced from Thetis. J.J. Boyd (KG4VCF).\n"
               "// =============================================================\n")
    second = "/* Copyright (C) 2025 Warren Pratt, NR0V */\n"
    text = upstream + history + second + "\n#include \"comm.h\"\n"
    blocks = sn.extract_blocks(text)
    assert blocks == [
        "Copyright (C) 2017, 2021 Warren Pratt, NR0V\n\n"
        "This program is free software; you can redistribute it.\n*/\n"
        "//\n"
        "// Dual-Licensing Statement (Richard Samphire MW0LGE)\n"
        "// reserves the right to license his code under other terms.",
        "/* Copyright (C) 2025 Warren Pratt, NR0V */",
    ]
    assert not any("NereusSDR" in b or "KG4VCF" in b for b in blocks)


def test_nereussdr_original_file_header_is_left_out():
    text = ("/*\n * Copyright (C) 2026 J.J. Boyd, KG4VCF (NereusSDR-original glue)\n"
            " * GPL-3.0-or-later.\n */\nint x;\n")
    assert sn.extract_blocks(text) == []


# --------------------------------------------------------------------------
# Carried by the licence text or not

def test_block_matching_the_licence_text_is_carried():
    block = "/* Copyright (c) 2020 Example Org\n" + BSD_CONDITIONS + "*/"
    assert sn.is_carried(block, [LICENCE_TEXT])


def test_pointer_to_the_licence_file_is_carried():
    block = " * Copyright (c) 2020, Example Org\n * See the LICENSE file.\n */"
    assert sn.is_carried(block, [LICENCE_TEXT])


def test_other_holder_is_not_carried():
    block = "/* Copyright (c) 2020 Another Org\n" + BSD_CONDITIONS + "*/"
    assert not sn.is_carried(block, [LICENCE_TEXT])


def test_other_years_are_not_carried():
    block = "/* Copyright (c) 2018-2020 Example Org */"
    assert not sn.is_carried(block, [LICENCE_TEXT])


def test_continuation_holder_line_is_checked():
    block = "/* Copyright (c) 2020 Example Org\n                 2021 Other Person */"
    assert not sn.is_carried(block, [LICENCE_TEXT])


def test_extra_licence_terms_are_not_carried():
    block = (" * Copyright (c) 2020 Example Org\n"
             " * Permission is also granted to do something else.\n */")
    assert not sn.is_carried(block, [LICENCE_TEXT])


# --------------------------------------------------------------------------
# Collecting over files

def test_same_notice_in_two_layouts_is_listed_once(tmp_path):
    a = tmp_path / "a.c"
    b = tmp_path / "b.c"
    a.write_text("/* Copyright (c) 2021 Other Org\n   All rights reserved. */\n")
    b.write_text("/*\n * Copyright (c) 2021 Other Org\n * All rights reserved.\n */\n")
    c = tmp_path / "c.c"
    c.write_text("/* Copyright (c) 2020 Example Org */\n")
    notices, counts = sn.collect([(a, "a.c"), (b, "b.c"), (c, "c.c")], [LICENCE_TEXT])
    assert len(notices) == 1
    assert notices[0].files == ["a.c", "b.c"]
    assert notices[0].text == "/* Copyright (c) 2021 Other Org\n   All rights reserved. */"
    assert counts["distinct"] == 2 and counts["carried"] == 1


def test_includes_are_followed_within_the_tree(tmp_path):
    (tmp_path / "inc").mkdir()
    main = tmp_path / "main.c"
    main.write_text('#include "local.h"\n#include <inc_only.h>\n#include <stdio.h>\n')
    (tmp_path / "local.h").write_text('#include "inc/deeper.h"\n')
    (tmp_path / "inc" / "deeper.h").write_text("int d;\n")
    (tmp_path / "inc" / "inc_only.h").write_text("int i;\n")
    files = sn.resolve_includes([main], [tmp_path / "inc"])
    names = sorted(p.relative_to(tmp_path.resolve()).as_posix() for p in files)
    assert names == ["inc/deeper.h", "inc/inc_only.h", "local.h", "main.c"]


# --------------------------------------------------------------------------
# The committed files match what the script writes

@pytest.mark.parametrize("library", ["fftw3", "rade", "r8brain", "wdsp"])
def test_committed_notices_are_current(library):
    target = REPO / "packaging" / "third-party-licenses" / f"{library}-notices.txt"
    result = subprocess.run(
        [sys.executable, str(SOURCE_SCRIPT), library, "--check", str(target)],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def test_rnnoise_notices_are_current_when_fetched():
    build = REPO / "build-licences"
    if not (build / "_deps" / "rnnoise_upstream-src").is_dir():
        pytest.skip("no configured build with rnnoise fetched")
    target = REPO / "packaging" / "third-party-licenses" / "rnnoise-notices.txt"
    result = subprocess.run(
        [sys.executable, str(SOURCE_SCRIPT), "rnnoise", "--build-dir", str(build),
         "--check", str(target)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def test_check_fails_on_a_stale_file(tmp_path):
    stale = tmp_path / "rade-notices.txt"
    stale.write_text("out of date\n")
    result = subprocess.run(
        [sys.executable, str(SOURCE_SCRIPT), "rade", "--check", str(stale)],
        capture_output=True, text=True)
    assert result.returncode == 1
    assert "out of date" in result.stderr


def test_every_listed_notice_is_verbatim_in_its_files():
    source_set, notices, _ = sn.build("rade", REPO, None)
    assert notices
    for notice in notices:
        for name in notice.files:
            text = sn.read_source(source_set.base / name)
            assert notice.text in text or sn.normalise(notice.text) in sn.normalise(text)
        assert notice.text in sn.read_source(source_set.base / notice.files[0])


# --------------------------------------------------------------------------
# Fetched libraries: files from a built tree's compile_commands.json

def test_build_tree_files_come_from_compile_commands(tmp_path):
    build = tmp_path / "build"
    dc = build / "_deps" / "nereus_libdatachannel-src"
    (dc / "src").mkdir(parents=True)
    (dc / "deps" / "plog" / "include" / "plog").mkdir(parents=True)
    (dc / "deps" / "libjuice" / "src").mkdir(parents=True)
    (dc / "src" / "global.cpp").write_text('#include <plog/Log.h>\n#include "local.hpp"\n')
    (dc / "src" / "local.hpp").write_text("/* Copyright (c) 2020 Someone */\n")
    (dc / "src" / "unbuilt.cpp").write_text("/* Copyright (c) 1999 Nobody */\n")
    (dc / "deps" / "plog" / "include" / "plog" / "Log.h").write_text("// plog\n")
    (dc / "deps" / "libjuice" / "src" / "agent.c").write_text("int a;\n")
    commands = [
        {"directory": str(build), "file": str(dc / "src" / "global.cpp"),
         "command": f"c++ -DX=1 -I{dc}/src -I {dc}/deps/plog/include -c global.cpp"},
        {"directory": str(build), "file": str(dc / "deps" / "libjuice" / "src" / "agent.c"),
         "arguments": ["cc", "-isystem", "/opt/homebrew/include", "-c", "agent.c"]},
    ]
    (build / "compile_commands.json").write_text(json.dumps(commands))

    def names(files, base):
        return sorted(f.relative_to(base.resolve()).as_posix() for f in files)

    own = sn._from_build(build, dc, dc)
    assert "src/unbuilt.cpp" not in names(own, dc)
    assert names(own, dc) == ["deps/libjuice/src/agent.c", "deps/plog/include/plog/Log.h",
                              "src/global.cpp", "src/local.hpp"]
    plog = dc / "deps" / "plog"
    assert names(sn._from_build(build, plog, dc), plog) == ["include/plog/Log.h"]


# --------------------------------------------------------------------------
# Dedications (libsodium): public-domain and CC0 comments are notices too

DEDICATED = """\
/*
version 20080912
D. J. Bernstein
Public domain.
*/

/* a plain comment */
int x;
"""

WAIVED = """\
/*
 * Written by Someone. To the extent possible under law, the
 * author has waived all copyright and related or neighboring rights.
 *
 * Copyright (c) 2015 Someone
 */
"""


def test_dedications_are_read_only_when_asked(tmp_path):
    assert sn.extract_blocks(DEDICATED) == []
    assert sn.extract_dedications(DEDICATED) == [DEDICATED.split("\n\n")[0]]
    src = tmp_path / "salsa.c"
    src.write_text(DEDICATED)
    plain, _ = sn.collect([(src, "salsa.c")], [LICENCE_TEXT])
    assert plain == []
    notices, _ = sn.collect([(src, "salsa.c")], [LICENCE_TEXT], dedications=True)
    assert [n.text for n in notices] == [DEDICATED.split("\n\n")[0]]


def test_a_waiver_above_the_copyright_line_is_kept_with_it():
    # Without dedications the block starts at the copyright line; with
    # them, at the comment's start, so the waiver travels with it.
    assert sn.extract_blocks(WAIVED)[0].startswith(" * Copyright (c) 2015 Someone")
    assert sn.extract_blocks(WAIVED, dedications=True) == [WAIVED.rstrip("\n")]
    # And it is not listed a second time as a bare dedication.
    assert sn.extract_dedications(WAIVED) == []


def test_libsodium_headers_are_read_where_the_archive_has_them(tmp_path):
    build = tmp_path / "build"
    src = build / "_deps" / "nereus_libsodium-src" / "src" / "libsodium"
    copy = build / "_deps" / "nereus_libsodium-include"
    (src / "crypto_x").mkdir(parents=True)
    (src / "include" / "sodium").mkdir(parents=True)
    (copy / "sodium").mkdir(parents=True)
    (src / "crypto_x" / "x.c").write_text('#include "sodium/x.h"\n')
    (src / "include" / "sodium" / "x.h").write_text("/* Public domain. */\n")
    (copy / "sodium" / "x.h").write_text("/* Public domain. */\n")
    commands = [{"directory": str(build), "file": str(src / "crypto_x" / "x.c"),
                 "command": f"cc -I{copy} -c x.c"}]
    (build / "compile_commands.json").write_text(json.dumps(commands))
    args = type("Args", (), {"build_dir": build})()
    source_set = sn.PRESETS["libsodium"](REPO, args)
    names = sorted(f.relative_to(source_set.base.resolve()).as_posix()
                   for f in source_set.files)
    assert names == ["src/libsodium/crypto_x/x.c", "src/libsodium/include/sodium/x.h"]
    assert source_set.dedications


def test_libsodium_notices_are_current_when_fetched():
    for name in ("build-lane-b", "build-integration", "build", "build-licences"):
        build = REPO / name
        if (build / "_deps" / "nereus_libsodium-src").is_dir() \
                and (build / "compile_commands.json").is_file():
            break
    else:
        pytest.skip("no configured build with libsodium fetched")
    target = REPO / "packaging" / "third-party-licenses" / "libsodium-notices.txt"
    result = subprocess.run(
        [sys.executable, str(SOURCE_SCRIPT), "libsodium", "--build-dir", str(build),
         "--check", str(target)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
