"""Offline lint for the station card-image inputs; never boots or enables services."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
STATION = ROOT / "packaging/station-image"
WORKFLOW = ROOT / ".github/workflows/station-image.yml"


def read(relative: str) -> str:
    return (STATION / relative).read_text()


def test_shell_scripts_parse_without_execution():
    for path in STATION.rglob("*.sh"):
        subprocess.run(["bash", "-n", str(path)], check=True)


def test_stage_inherits_lite_rootfs_and_installs_only_matching_package():
    assert "copy_previous" in read("pi-gen/stage-nereus/prerun.sh")
    assert read("pi-gen/stage-nereus/00-install/00-packages").splitlines() == ["avahi-daemon"]
    stage = read("pi-gen/stage-nereus/00-install/00-run.sh")
    assert "${ROOTFS_DIR}/tmp/nereus-install" in stage
    assert "on_chroot" in stage
    assert "rm -rf \"${ROOTFS_DIR}/tmp/nereus-install\"" in stage
    installer = read("common/install-station.sh")
    for required in (
        "VERSION_CODENAME:-}\" = trixie",
        "--print-architecture)",
        "dpkg-deb --field \"$deb\" Package",
        "dpkg-deb --field \"$deb\" Architecture",
        "apt-get install -y --no-install-recommends \"$deb\" avahi-daemon",
        "DynamicUser=yes",
        "systemctl enable nereus-firstboot.service nereusd.service avahi-daemon.service",
    ):
        assert required in installer
    assert not re.search(r"--force-(?:depends|architecture)|--allow-unauthenticated", installer)


def test_first_boot_and_config_are_clean():
    assets = [p for p in STATION.rglob("*") if p.is_file()]
    forbidden_names = ("identity", "private", "token", "devices.json", ".pem", ".key")
    assert all(not any(word in p.name.lower() for word in forbidden_names) for p in assets)
    installer = read("common/install-station.sh")
    assert "nereusd.conf.sample /etc/nereusd.conf" in installer
    sample = (ROOT / "packaging/nereusd.conf.sample").read_text()
    assert re.search(r"^radio_mac\s*=\s*$", sample, re.MULTILINE)
    assert "station-identity" not in installer
    firstboot = read("common/nereus-firstboot.sh")
    assert "first-boot-pending" in firstboot
    assert "hostnamectl set-hostname nereus-station" in firstboot
    assert "rm -f \"$marker\"" in firstboot
    assert "pairing" not in firstboot.lower()
    assert "token" not in firstboot.lower()
    unit = read("common/nereus-firstboot.service")
    assert "Before=nereusd.service" in unit
    assert "ConditionPathExists=/var/lib/nereus-station/first-boot-pending" in unit


def test_armbian_contract_and_workflow_provenance():
    armbian = read("armbian/customize-image.sh")
    assert "trixie" in armbian and "rock-5c" in armbian
    assert "/tmp/overlay/nereus-station" in armbian
    assert "install-station.sh" in armbian
    workflow = WORKFLOW.read_text()
    assert "workflow_dispatch:" in workflow
    assert "ubuntu-24.04-arm" in workflow
    assert "74d08a337bd29da289b9aedbe5b48c79fb2e5a03" in workflow
    assert "RELEASE=trixie" in workflow
    assert "ENABLE_CLOUD_INIT=0" in workflow
    assert "04-cloud-init/SKIP" in workflow
    assert "ENABLE_SSH=0" in workflow
    assert "DEPLOY_COMPRESSION=xz" in workflow
    assert "source_sha" in workflow and "image_sha256" in workflow
    assert "_arm64_trixie.deb" in workflow
    assert "upload-artifact@v4" in workflow
    assert "release.yml" not in workflow
    builder = read("build-trixie-deb.sh")
    assert "-march=armv8-a" in builder
    assert "-DENABLE_DFNR=OFF" in builder
    assert "-march=native" not in builder and "-mcpu=native" not in builder
