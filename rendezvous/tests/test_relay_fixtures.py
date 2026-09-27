# no-port-check: NereusSDR-original.
"""Every frame-protocol fixture in rendezvous/conformance/v1/relay/ against
the real relay (rendezvous document section 12.8)."""

from pathlib import Path

import pytest

from relay_runner import RELAY_FIXTURES, RelayFixtureFailure, load_fixture, load_manifest, run_relay_fixture

FIXTURES = load_manifest()["fixtures"]


def test_every_relay_fixture_is_listed_once():
    listed = [f["file"] for f in FIXTURES]
    assert sorted(listed) == sorted(p.name for p in Path(RELAY_FIXTURES).glob("*.json") if p.name != "manifest.json")
    assert len(set(listed)) == len(listed)
    assert load_manifest()["relayFrameVersions"] == [1]


@pytest.mark.parametrize("entry", FIXTURES, ids=[f["id"] for f in FIXTURES])
def test_relay_fixture(entry):
    fixture = load_fixture(entry["file"])
    assert "relay" in fixture["runs"], "the relay's runner runs every fixture"
    assert set(fixture["runs"]) <= {"relay", "core", "app"}
    run_relay_fixture(fixture, entry["id"])


def test_the_runner_reports_the_step_that_differs():
    fixture = load_fixture("join-and-forward.json")
    fixture["steps"][2]["binary"] = ["81", "01", "01"]
    with pytest.raises(RelayFixtureFailure) as info:
        run_relay_fixture(fixture, "altered")
    assert "step 2" in str(info.value) and "at byte 2" in str(info.value)
