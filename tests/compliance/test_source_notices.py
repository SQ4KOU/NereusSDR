"""Tests for scripts/collect-source-notices.py (R-R3-50).

These use small C sources written into tmp_path, then run the presets that
need only files in this repository against the committed *-notices.txt
files, so a vendor update that changes a notice fails here until the file
is regenerated.
"""
import importlib.util
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
