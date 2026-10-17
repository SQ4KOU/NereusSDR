"""Exercise both CLI guards against isolated reconciliation/header fixtures."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import pytest


REPO = Path(__file__).resolve().parents[2]
MIXED = "src/models/PanadapterModel.h"
THETIS_MARKERS = [
    "Ported from", "Thetis", "Copyright (C)", "General Public License",
    "Modification history (NereusSDR)",
]
ALL_MARKERS = THETIS_MARKERS + ["AetherSDR"]


@pytest.fixture
def fixture_repo(tmp_path):
    scripts = tmp_path / "scripts"
    scripts.mkdir()
    # Copy the actual CLI implementations and any shared Python helpers.
    for source in (REPO / "scripts").glob("*.py"):
        shutil.copy(source, scripts / source.name)
    attribution = tmp_path / "docs/attribution"
    attribution.mkdir(parents=True)
    reconciliation = (REPO / "docs/attribution/aethersdr-reconciliation.md").read_text()
    # Keep the real C.1/C.2 headings, table rows, subtotal, unheaded incidental
    # table and prose intact: an artificial "## Bucket D" would hide the bug.
    bucket_c = reconciliation.split("## Bucket C —", 1)[1].split("## Appendix", 1)[0]
    bucket_c = "## Bucket C —" + bucket_c
    text = (
        "## Bucket A — Genuine AetherSDR derivations\n\n"
        "| NereusSDR file | AetherSDR counterpart |\n|---|---|\n"
        "| `src/OnlyA.cpp` | upstream.cpp |\n"
        "| `src/MixedA.cpp` | upstream.cpp |\n\n"
        "## Bucket B — False AetherSDR citations\n\n"
        "| NereusSDR file | Evidence |\n|---|---|\n"
        "| `src/OnlyB.cpp` | false boilerplate |\n\n"
        + bucket_c
        + "## Bucket D — Deferred files\n\n"
        "| NereusSDR file | Evidence |\n|---|---|\n"
        "| `src/OnlyD.cpp` | incidental |\n"
    )
    (attribution / "aethersdr-reconciliation.md").write_text(text)
    c_table = bucket_c.split("(Subtotal for Bucket C:", 1)[0]
    mixed_paths = re.findall(r"^\| `(src/[^`]+)` \|", c_table, re.MULTILINE)
    assert len(mixed_paths) == 12
    incidental = re.findall(r"^\| `((?:src|tests)/[^`]+)` \|", bucket_c, re.MULTILINE)
    tracked = set(incidental) | {"src/OnlyA.cpp", "src/MixedA.cpp", "src/OnlyB.cpp", "src/OnlyD.cpp"}
    for relative in tracked:
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        markers = ALL_MARKERS if relative in mixed_paths or relative == "src/MixedA.cpp" else (
            ["AetherSDR", "Modification history (NereusSDR)"] if relative == "src/OnlyA.cpp" else []
        )
        path.write_text("\n".join(f"// {marker}" for marker in markers) + "\nint fixture;\n")
    thetis_paths = mixed_paths + ["src/MixedA.cpp"]
    (attribution / "THETIS-PROVENANCE.md").write_text(
        "| NereusSDR file | Source |\n|---|---|\n"
        + "".join(f"| {path} | display.cs |\n" for path in thetis_paths)
    )
    # Inventory needs ls-files only. Stub that read-only query without making
    # any Git repository or touching the developer's real index.
    stub = tmp_path / "bin/git"
    stub.parent.mkdir()
    stub.write_text(
        f"#!{sys.executable}\nimport sys\n"
        "assert sys.argv[1:] == ['-C', " + repr(str(tmp_path)) + ", 'ls-files']\n"
        "print(" + repr("\n".join(sorted(tracked))) + ")\n"
    )
    stub.chmod(0o755)
    return tmp_path


def run_cli(repo, script, *args):
    env = dict(os.environ, PATH=str(repo / "bin") + os.pathsep + os.environ["PATH"])
    return subprocess.run(
        [sys.executable, str(repo / "scripts" / script), *args],
        cwd=repo, env=env, capture_output=True, text=True,
    )


def test_actual_bucket_c_tables_are_guarded_and_incidental_table_is_excluded(fixture_repo):
    guard = run_cli(fixture_repo, "verify-thetis-headers.py", "--kind=aethersdr")
    assert guard.returncode == 0, guard.stdout + guard.stderr
    assert "14/14 files pass header check" in guard.stdout
    inventory = run_cli(fixture_repo, "compliance-inventory.py", "--format=json", "--fail-on-unclassified")
    assert inventory.returncode == 0, inventory.stdout + inventory.stderr
    rows = {row["path"]: row for row in json.loads(inventory.stdout)}
    for path in (MIXED, "src/MixedA.cpp"):
        assert rows[path]["classification"] == "thetis-aethersdr-port"
    assert rows["src/OnlyA.cpp"]["classification"] == "aethersdr-port"
    for path in ("src/OnlyB.cpp", "src/OnlyD.cpp", "src/core/WdspEngine.cpp"):
        assert rows[path]["classification"] == "nereussdr-original"
    assert rows["tests/tst_about_dialog.cpp"]["classification"] == "test"


@pytest.mark.parametrize("marker", ALL_MARKERS)
@pytest.mark.parametrize("path", [MIXED, "src/MixedA.cpp"])
def test_mixed_lineage_keeps_both_marker_obligations(fixture_repo, path, marker):
    source = fixture_repo / path
    source.write_text(source.read_text().replace(f"// {marker}\n", ""))
    kind = "aethersdr" if marker == "AetherSDR" else "thetis"
    guard = run_cli(fixture_repo, "verify-thetis-headers.py", "--kind=" + kind)
    assert guard.returncode == 1, guard.stdout + guard.stderr
    assert f"{path} — missing-markers: {marker}" in guard.stdout
    inventory = run_cli(fixture_repo, "compliance-inventory.py", "--format=json", "--fail-on-unclassified")
    assert inventory.returncode == 1, inventory.stdout + inventory.stderr
    row = next(row for row in json.loads(inventory.stdout) if row["path"] == path)
    assert row["missing_markers"] == [marker]


def test_deleted_mixed_attribution_header_is_rejected(fixture_repo):
    (fixture_repo / MIXED).write_text("int fixture;\n")
    for kind in ("aethersdr", "thetis"):
        result = run_cli(fixture_repo, "verify-thetis-headers.py", "--kind=" + kind)
        assert result.returncode == 1, result.stdout + result.stderr
        assert f"FAIL [{kind}] {MIXED}" in result.stdout
    result = run_cli(fixture_repo, "compliance-inventory.py", "--format=json", "--fail-on-unclassified")
    assert result.returncode == 1, result.stdout + result.stderr
    row = next(row for row in json.loads(result.stdout) if row["path"] == MIXED)
    assert set(row["missing_markers"]) == set(ALL_MARKERS)
