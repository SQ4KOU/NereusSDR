"""Regression tests for how scripts/verify-inline-tag-preservation.py picks
the ramdor Thetis tree a cite is checked against.

../Thetis moved from 501e3f5 (v2.10.3.13 plus seven commits) to the
CLAUDE.md pin, v2.10.3.15 (3759d09). Cites written against the old pin
must still be checked against 501e3f5, or a port could drop an author
tag that sits beside the cited lines there but not in v2.10.3.15, and
the check would pass. That covers cites stamped [v2.10.3.13] or
[@501e3f5], and also every cite with no stamp or a stamp for a release
with no tree here (CLAUDE.md requires a stamp on every new cite, so an
unstamped cite is a legacy one).

Each test runs the real script, copied into a throwaway checkout under
``tmp_path`` beside three fake upstream trees. Each tree carries a
different author tag at the same cited line, so the tag the script
demands shows which tree it opened.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent.parent
SCRIPT = REPO / "scripts" / "verify-inline-tag-preservation.py"
CORPUS = REPO / "docs" / "attribution" / "thetis-author-tags.json"

# Tree directory name -> (env var, the tag that tree carries at line 4).
TREES = {
    "Thetis": ("NEREUS_THETIS_DIR", "W5WC"),
    "Thetis-v2.10.3.15": ("NEREUS_THETIS_V21015_DIR", "G8NJJ"),
    "Thetis-v2.10.3.13": ("NEREUS_THETIS_V21013_DIR", "MW0LGE"),
}
OLD_PIN_TAG = TREES["Thetis-v2.10.3.13"][1]
NEW_PIN_TAG = TREES["Thetis-v2.10.3.15"][1]


def _upstream(tag: str) -> str:
    return ("using System;\n"
            "public class Console {\n"
            "    void Run() {\n"
            f"        int x = 1; //{tag} note\n"
            "    }\n"
            "}\n")


def _checkout(tmp_path: Path, port: str, absent=()) -> tuple[Path, dict]:
    """A throwaway checkout holding `port` as src/Port.cpp, with every
    fake tree in TREES beside it except those in `absent`."""
    repo = tmp_path / "NereusSDR"
    (repo / "scripts").mkdir(parents=True)
    (repo / "docs" / "attribution").mkdir(parents=True)
    (repo / "src").mkdir()
    shutil.copy(SCRIPT, repo / "scripts" / SCRIPT.name)
    shutil.copy(CORPUS, repo / "docs" / "attribution" / CORPUS.name)
    (repo / "src" / "Port.cpp").write_text(port)

    env = {k: v for k, v in os.environ.items()
           if not k.startswith("NEREUS_")}
    for name, (var, tag) in TREES.items():
        tree = tmp_path / name
        env[var] = str(tree)
        if name in absent:
            continue
        source = tree / "Project Files" / "Source" / "Console"
        source.mkdir(parents=True)
        (source / "console.cs").write_text(_upstream(tag))
    for var, name in (("NEREUS_MI0BOT_DIR", "mi0bot-Thetis"),
                      ("NEREUS_DESKHPSDR_DIR", "deskhpsdr"),
                      ("NEREUS_FREEDV_DIR", "freedv-gui")):
        env[var] = str(tmp_path / name)
    return repo, env


def _run(repo: Path, env: dict):
    result = subprocess.run(
        [sys.executable, "-I", str(repo / "scripts" / SCRIPT.name), "--json"],
        cwd=repo, capture_output=True, text=True, env=env)
    return result, json.loads(result.stdout)["findings"]


def _port(stamp: str, keep: str | None = None) -> str:
    tail = f" //{keep}" if keep else ""
    return ("#include <cstdint>\n"
            f"// From Thetis console.cs:4 {stamp}\n"
            f"int x = 1;{tail}\n")


def _describe(result) -> str:
    return (f"exit {result.returncode}\nstdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}")


def _missing(findings) -> set[str]:
    return {f["tag"] for f in findings if f["issue"] == "missing-inline-tag"}


@pytest.mark.parametrize("stamp", [
    "[v2.10.3.13]", "[@501e3f5]", "[@501e3f51]", "[v2.10.3.13 @501e3f5]",
    "[@501e3f513f73f07742d7e1b85a0e9528bd14977d]",
])
def test_dropped_tag_under_an_old_pin_cite_fails(tmp_path, stamp):
    repo, env = _checkout(tmp_path, _port(stamp))
    result, findings = _run(repo, env)
    assert result.returncode == 1, _describe(result)
    assert _missing(findings) == {OLD_PIN_TAG}, _describe(result)


@pytest.mark.parametrize("stamp", [
    "", "[v2.10.3.6]", "[v2.10.3.14]", "[@0cd4559]",
])
def test_unstamped_or_unknown_cite_is_checked_against_the_old_pin(
        tmp_path, stamp):
    repo, env = _checkout(tmp_path, _port(stamp))
    result, findings = _run(repo, env)
    assert result.returncode == 1, _describe(result)
    # The old pin's tag, not the one ../Thetis or v2.10.3.15 carries.
    assert _missing(findings) == {OLD_PIN_TAG}, _describe(result)


@pytest.mark.parametrize("stamp", ["", "[v2.10.3.13]"])
def test_kept_tag_under_an_old_pin_cite_passes(tmp_path, stamp):
    repo, env = _checkout(tmp_path, _port(stamp, keep=OLD_PIN_TAG))
    result, findings = _run(repo, env)
    assert result.returncode == 0, _describe(result)
    assert not findings, _describe(result)


@pytest.mark.parametrize("stamp", [
    "[v2.10.3.15]", "[@3759d09]", "[@3759d096]", "[v2.10.3.15 @3759d09]",
])
def test_new_pin_cite_is_checked_against_v2_10_3_15(tmp_path, stamp):
    repo, env = _checkout(tmp_path, _port(stamp))
    result, findings = _run(repo, env)
    assert result.returncode == 1, _describe(result)
    assert _missing(findings) == {NEW_PIN_TAG}, _describe(result)


@pytest.mark.parametrize("stamp", ["", "[v2.10.3.13]", "[v2.10.3.6]"])
def test_missing_old_pin_tree_warns_and_never_falls_back(tmp_path, stamp):
    repo, env = _checkout(tmp_path, _port(stamp),
                          absent=("Thetis-v2.10.3.13",))
    result, findings = _run(repo, env)
    assert result.returncode == 0, _describe(result)
    assert [f["issue"] for f in findings] == ["upstream-not-found"], \
        _describe(result)
