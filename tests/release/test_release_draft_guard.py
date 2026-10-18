"""Offline regression for the actual release step; gh is always a local stub."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
TAG = "v2026.10.0"
GH_STUB = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
root = Path(os.environ["RELEASE_FIXTURE_ROOT"])
scenario = json.loads((root / "scenario.json").read_text())
state = json.loads((root / "state.json").read_text())
args = sys.argv[1:]
with (root / "calls.jsonl").open("a") as calls:
    calls.write(json.dumps(args) + "\n")
if args[:2] == ["api", "graphql"]:
    for value in ("owner=boydsoftprez", "name=NereusSDR", "tag=v2026.10.0"):
        if value not in args:
            sys.exit(3)
    print(scenario["lookup_body"])
    sys.exit(scenario.get("lookup_exit", 0))
if args[:2] == ["release", "delete"] or (
        args[:3] == ["api", "--method", "DELETE"]):
    if scenario.get("delete_exit", 0):
        sys.exit(scenario["delete_exit"])
    if args[0] == "api" and args[3] != "repos/boydsoftprez/NereusSDR/releases/42":
        sys.exit(3)
    state["release"] = None
    state["assets"] = []
elif args[:2] == ["release", "create"]:
    if scenario.get("create_exit", 0):
        sys.exit(scenario["create_exit"])
    state["release"] = {"id": 77, "tag_name": args[2], "draft": True}
    state["assets"] = ["replacement-assets"]
else:
    sys.exit(3)
(root / "state.json").write_text(json.dumps(state))
'''


def release_response(draft=True, **fields):
    release = {"databaseId": 42, "tagName": TAG, "isDraft": draft, **fields}
    return json.dumps({"data": {"repository": {"id": "R_fixture", "release": release}}})


def actual_release_script(prerelease):
    workflow = (ROOT / ".github/workflows/release.yml").read_text()
    matches = re.findall(
        r"(?m)^      - name: Create draft GitHub Release\n(.*?)(?=^      - name:|\Z)",
        workflow, re.S)
    if len(matches) != 1:
        raise AssertionError("Expected one actual release creation step")
    script = textwrap.dedent(matches[0].split("        run: |\n", 1)[1])
    for key, value in {"version": "2026.10.0", "tag": TAG,
                       "is_prerelease": str(prerelease).lower()}.items():
        script = script.replace("${{ needs.prepare.outputs." + key + " }}", value)
    if "${{" in script:
        raise AssertionError("Unresolved workflow expression")
    return script


class ReleaseDraftGuard(unittest.TestCase):
    def test_release_state_and_failures(self):
        absent = json.dumps({"data": {"repository": {"id": "R_fixture", "release": None}}})
        cases = [
            ("absent", absent, {}, True, 0),
            ("draft", release_response(), {}, True, 1),
            ("draft-prerelease", release_response(isPrerelease=True), {"prerelease": True}, True, 1),
            ("published", release_response(False), {}, False, 0),
            ("published-prerelease", release_response(False, isPrerelease=True), {"prerelease": True}, False, 0),
            ("malformed-json", "{", {}, False, 0),
            ("empty-success-response", "", {}, False, 0),
            ("null-response", "null", {}, False, 0),
            ("repository-unavailable", '{"data":{"repository":null}}', {}, False, 0),
            ("release-field-missing", '{"data":{"repository":{"id":"R_fixture"}}}', {}, False, 0),
            ("draft-field-invalid", release_response("true"), {}, False, 0),
            ("wrong-tag", release_response(tagName="v2026.10.1"), {}, False, 0),
            ("id-boolean", release_response(databaseId=True), {}, False, 0),
            ("id-negative", release_response(databaseId=-42), {}, False, 0),
            ("id-fractional", release_response(databaseId=42.5), {}, False, 0),
            ("id-string", release_response(databaseId="42"), {}, False, 0),
            ("id-null", release_response(databaseId=None), {}, False, 0),
            ("multiple-responses", release_response() + "\n" + absent, {}, False, 0),
            ("graphql-error", '{"errors":[{"message":"Forbidden"}],"data":{"repository":{"id":"R_fixture","release":null}}}', {}, False, 0),
            ("auth-failure", '{"message":"Bad credentials"}', {"lookup_exit": 1}, False, 0),
            ("network-failure", "", {"lookup_exit": 1}, False, 0),
            ("http404-failure", '{"message":"Not Found"}', {"lookup_exit": 1}, False, 0),
            ("delete-failure", release_response(), {"delete_exit": 1}, False, 1),
        ]
        for name, body, options, should_create, delete_attempts in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix="release-guard-") as directory:
                root = Path(directory)
                (root / "bin").mkdir()
                stub = root / "bin/gh"
                stub.write_text(GH_STUB)
                stub.chmod(0o755)
                (root / "scenario.json").write_text(json.dumps({"lookup_body": body, **options}))
                original = {"release": None if name == "absent" else {
                                "id": 42, "draft": name in ("draft", "draft-prerelease", "delete-failure"),
                                "tag_name": TAG},
                            "assets": [] if name == "absent" else ["original-asset"],
                            "tag_exists": True}
                (root / "state.json").write_text(json.dumps(original))
                env = {"PATH": str(root / "bin") + os.pathsep + os.defpath,
                       "RELEASE_FIXTURE_ROOT": str(root), "GITHUB_REPOSITORY": "boydsoftprez/NereusSDR"}
                script = actual_release_script(options.get("prerelease", False))
                # GitHub's Bash runner already stops on errors; use that even for the baseline.
                result = subprocess.run(["/bin/bash", "--noprofile", "--norc", "-eo", "pipefail"],
                                        input=script, env=env, cwd=root, text=True,
                                        capture_output=True, timeout=10)
                calls = [json.loads(line) for line in (root / "calls.jsonl").read_text().splitlines()]
                creates = [call for call in calls if call[:2] == ["release", "create"]]
                deletes = [call for call in calls if call[:2] == ["release", "delete"] or call[:3] == ["api", "--method", "DELETE"]]
                final = json.loads((root / "state.json").read_text())
                print(json.dumps({"case": name, "exit": result.returncode, "calls": calls,
                                  "state": final, "stdout": result.stdout, "stderr": result.stderr}))
                self.assertEqual(len(creates), int(should_create), "destructive continuation")
                self.assertEqual(len(deletes), delete_attempts)
                self.assertEqual(result.returncode == 0, should_create)
                self.assertTrue(final["tag_exists"])
                if not should_create:
                    self.assertEqual(final, original, "existing release or assets changed")
                else:
                    self.assertIn("--draft", creates[0])
                    self.assertEqual("--prerelease" in creates[0], options.get("prerelease", False))
                if deletes:
                    self.assertEqual(deletes[0], ["api", "--method", "DELETE",
                                                 "repos/boydsoftprez/NereusSDR/releases/42"])


if __name__ == "__main__":
    unittest.main()
