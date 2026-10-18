"""The Core's installers raise the Linux network receive buffer limit.

The radio socket asks for RadioConnection::kRadioSocketReceiveBufferBytes and
Linux grants at most net.core.rmem_max. The nereusd component ships one
sysctl.d file that sets that limit to what the Core asks for, and a helper
that applies it at install, only ever upward. These tests hold the file to
the Core's request, run the helper and the stage installers' block against
temporary files, and check that every install route carries them. Nothing
here reads or writes /proc, and no service command runs.
"""

from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
CONF = ROOT / "packaging/nereusd-receive-buffer.conf"
HELPER = ROOT / "packaging/nereusd-raise-receive-buffer-limit.sh"
INSTALLED_CONF = "/usr/lib/sysctl.d/10-nereusd.conf"
LIMIT_FILE = "/proc/sys/net/core/rmem_max"
STAGE_INSTALLERS = {
    "rock": ROOT / "scripts/radxa/install-core-selector.sh",
    "pi-install": ROOT / "scripts/pi4/install-core-pi4.sh",
    "pi-upgrade": ROOT / "scripts/pi4/upgrade-core-pi4.sh",
}
BEGIN = "# BEGIN receive buffer limit\n"
END = "# END receive buffer limit\n"
STOCK = 212992  # net.core.rmem_max on the Rock 5C as found, 2026-10-10


def asked_by_the_core() -> int:
    header = (ROOT / "src/core/RadioConnection.h").read_text()
    match = re.search(r"kRadioSocketReceiveBufferBytes\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;", header)
    assert match, "RadioConnection.h must name the receive buffer the radio socket asks for"
    return int(match[1], 0)


def settings(text: str) -> dict:
    found = {}
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, value = line.partition("=")
            found[key.strip()] = value.strip()
    return found


def shells():
    return [name for name in ("sh", "dash", "bash") if shutil.which(name)]


def run_helper(shell, conf, limit, cwd=None):
    return subprocess.run([shell, str(HELPER), str(conf), str(limit)],
                          capture_output=True, text=True, timeout=10, cwd=cwd)


# --- the settings file -------------------------------------------------------

def test_the_file_sets_the_one_limit_to_what_the_core_asks_for():
    assert settings(CONF.read_text()) == {"net.core.rmem_max": str(asked_by_the_core())}


def test_both_protocols_ask_for_the_named_size():
    for name in ("P1RadioConnection.cpp", "P2RadioConnection.cpp"):
        source = (ROOT / "src/core" / name).read_text()
        assert re.search(r"ReceiveBufferSizeSocketOption,\s*QVariant\(kRadioSocketReceiveBufferBytes\)",
                         source), name


def test_the_file_says_how_to_undo_it_and_which_file_wins():
    text = CONF.read_text()
    assert "To undo it" in text
    assert f"sysctl -w net.core.rmem_max={STOCK}" in text
    assert "the one whose name sorts later\n# wins (sysctl.d(5))" in text
    assert "/etc/sysctl.d/10-nereusd.conf to /dev/null" in text
    # systemd reads sysctl.d files in name order and the last one to set a key
    # wins, so a name at the start of the vendor range gives way to the rest.
    assert INSTALLED_CONF.rsplit("/", 1)[1].startswith("10-")


# --- the helper --------------------------------------------------------------

@pytest.mark.parametrize("shell", shells())
def test_a_lower_limit_is_raised_and_the_output_says_so(shell, tmp_path):
    limit = tmp_path / "rmem_max"
    limit.write_text(f"{STOCK}\n")
    result = run_helper(shell, CONF, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == asked_by_the_core()
    assert f"from {STOCK} to {asked_by_the_core()} bytes" in result.stdout
    assert "net.core.rmem_max" in result.stdout
    assert "the size the Core asks for" in result.stdout
    assert "removing that file undoes it" in result.stdout


@pytest.mark.parametrize("shell", shells())
@pytest.mark.parametrize("already", ["same", "higher"])
def test_a_limit_already_as_high_is_never_lowered(shell, already, tmp_path):
    value = asked_by_the_core() if already == "same" else 4 * asked_by_the_core()
    limit = tmp_path / "rmem_max"
    limit.write_text(f"{value}\n")
    result = run_helper(shell, CONF, limit)
    assert result.returncode == 0, result.stderr
    assert limit.read_text() == f"{value}\n", "the limit file must not be rewritten"
    assert "Left unchanged" in result.stdout
    assert str(value) in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_a_limit_that_cannot_be_read_does_not_fail_the_install(shell, tmp_path):
    result = run_helper(shell, CONF, tmp_path / "absent" / "rmem_max")
    assert result.returncode == 0, result.stderr
    assert "could not be read" in result.stdout
    assert "at the next system start" in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_a_limit_that_cannot_be_written_does_not_fail_the_install(shell, tmp_path):
    limit = tmp_path / "rmem_max"
    limit.write_text(f"{STOCK}\n")
    limit.chmod(0o444)
    result = run_helper(shell, CONF, limit)
    if int(limit.read_text()) != STOCK:
        pytest.skip("this user can write a read-only file (root)")
    assert result.returncode == 0, result.stderr
    assert "could not be raised" in result.stdout
    assert "at the next system start" in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_a_settings_file_without_the_limit_is_an_error(shell, tmp_path):
    empty = tmp_path / "empty.conf"
    empty.write_text("# nothing here\nnet.core.wmem_max = 1\n")
    limit = tmp_path / "rmem_max"
    limit.write_text(f"{STOCK}\n")
    result = run_helper(shell, empty, limit)
    assert result.returncode == 2
    assert int(limit.read_text()) == STOCK


# --- the helper gives way to the system's own settings ----------------------
#
# The settings file says how to keep the system's own limit: link
# /etc/sysctl.d/10-nereusd.conf to /dev/null, or set the limit in a file that
# is read after it. The helper must honor both at install, or every upgrade
# raises the limit in force again until the next system start. These run the
# helper with the settings file at its installed place under a temporary root.

def installed_root(tmp_path):
    root = tmp_path / "root"
    conf = root / INSTALLED_CONF.lstrip("/")
    conf.parent.mkdir(parents=True)
    shutil.copy(CONF, conf)
    limit = tmp_path / "rmem_max"
    limit.write_text(f"{STOCK}\n")
    return root, conf, limit


def put(root, path, text):
    target = root / path.lstrip("/")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)
    return target


@pytest.mark.parametrize("shell", shells())
def test_a_masked_settings_file_leaves_the_limit_alone(shell, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    mask = root / "etc/sysctl.d/10-nereusd.conf"
    mask.parent.mkdir(parents=True)
    mask.symlink_to("/dev/null")
    result = run_helper(shell, conf, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == STOCK
    assert f"{mask} replaces" in result.stdout
    assert "Left unchanged" in result.stdout


@pytest.mark.parametrize("shell", shells())
@pytest.mark.parametrize("where", ["/etc/sysctl.d", "/run/sysctl.d"])
def test_a_same_named_file_that_replaces_it_leaves_the_limit_alone(shell, where, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    own = put(root, f"{where}/10-nereusd.conf", "# the administrator's own\n")
    result = run_helper(shell, conf, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == STOCK
    assert f"{own} replaces" in result.stdout


@pytest.mark.parametrize("shell", shells())
@pytest.mark.parametrize("value", [STOCK, 16777216])
@pytest.mark.parametrize("path", ["/etc/sysctl.d/60-local.conf", "/usr/lib/sysctl.d/50-vendor.conf",
                                  "/usr/local/lib/sysctl.d/90-site.conf", "/etc/sysctl.d/99 local.conf"])
def test_a_file_read_later_that_sets_the_limit_leaves_it_alone(shell, value, path, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    later = put(root, path, f"# local\nnet.core.rmem_max = {value}\n")
    result = run_helper(shell, conf, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == STOCK
    assert f"{later} sets this limit" in result.stdout
    assert "Left unchanged" in result.stdout


# System start (systemd-sysctl) reads /etc/sysctl.conf only through a
# sysctl.d file that links to it, as Debian's /etc/sysctl.d/99-sysctl.conf
# did up to bookworm; trixie ships no such link.

@pytest.mark.parametrize("shell", shells())
def test_sysctl_conf_alone_does_not_stop_the_raise(shell, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    put(root, "/etc/sysctl.conf", "net.core.rmem_max = 212992\n")
    result = run_helper(shell, conf, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == asked_by_the_core()
    assert "Raised the network receive buffer limit" in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_sysctl_conf_linked_into_sysctl_d_leaves_the_limit_alone(shell, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    put(root, "/etc/sysctl.conf", "net.core.rmem_max = 212992\n")
    link = root / "etc/sysctl.d/99-sysctl.conf"
    link.parent.mkdir(parents=True, exist_ok=True)
    link.symlink_to("../sysctl.conf")
    result = run_helper(shell, conf, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == STOCK
    assert f"{link} sets this limit" in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_a_name_with_a_wildcard_is_read_as_it_is(shell, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    # Read as a pattern from the directory it is in, "[6]0-other.conf"
    # matches only 60-other.conf, which sets something else, so the file
    # that sets the limit is missed.
    put(root, "/etc/sysctl.d/60-other.conf", "net.core.wmem_max = 1000000\n")
    later = put(root, "/etc/sysctl.d/[6]0-other.conf", "net.core.rmem_max = 212992\n")
    result = run_helper(shell, conf, limit, cwd=later.parent)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == STOCK
    assert f"{later} sets this limit" in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_the_slash_spelling_counts_too(shell, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    later = put(root, "/etc/sysctl.d/70-slash.conf", "-net/core/rmem_max=212992\n")
    result = run_helper(shell, conf, limit)
    assert int(limit.read_text()) == STOCK
    assert f"{later} sets this limit" in result.stdout


@pytest.mark.parametrize("shell", shells())
def test_files_that_do_not_win_do_not_stop_the_raise(shell, tmp_path):
    root, conf, limit = installed_root(tmp_path)
    # Read before the Core's file, so the Core's file wins over it.
    put(root, "/etc/sysctl.d/05-early.conf", "net.core.rmem_max = 1000\n")
    # Read later, but sets something else.
    put(root, "/etc/sysctl.d/60-other.conf", "net.core.wmem_max = 1000000\n")
    # Would win, but an /etc file of the same name replaces it and sets nothing.
    put(root, "/usr/lib/sysctl.d/80-shadowed.conf", "net.core.rmem_max = 1000\n")
    put(root, "/etc/sysctl.d/80-shadowed.conf", "# emptied by the administrator\n")
    result = run_helper(shell, conf, limit)
    assert result.returncode == 0, result.stderr
    assert int(limit.read_text()) == asked_by_the_core()
    assert "Raised the network receive buffer limit" in result.stdout


# --- the stage installers (Rock, Pi) -----------------------------------------

def block_of(path: Path) -> str:
    text = path.read_text()
    assert BEGIN in text and END in text, f"{path.name} must carry the receive buffer block"
    return text.split(BEGIN, 1)[1].split(END, 1)[0]


def test_the_three_stage_installers_carry_the_same_block():
    blocks = {name: block_of(path) for name, path in STAGE_INSTALLERS.items()}
    assert blocks["rock"] == blocks["pi-install"] == blocks["pi-upgrade"]
    assert f"sysctl_conf={INSTALLED_CONF}\n" in blocks["rock"]
    assert f"rmem_max_file={LIMIT_FILE}\n" in blocks["rock"]


class Box:
    """A temporary root and stage for running an installer's block."""

    def __init__(self, tmp_path, stage_carries_it=True):
        self.conf = tmp_path / "root" / INSTALLED_CONF.lstrip("/")
        self.limit = tmp_path / "rmem_max"
        self.limit.write_text(f"{STOCK}\n")
        self.stage = tmp_path / "stage"
        self.stage.mkdir()
        if stage_carries_it:
            shipped = self.stage / INSTALLED_CONF.lstrip("/")
            shipped.parent.mkdir(parents=True)
            shutil.copy(CONF, shipped)
            helper = self.stage / "usr/local/libexec/nereusd/raise-receive-buffer-limit"
            helper.parent.mkdir(parents=True)
            shutil.copy(HELPER, helper)

    def run(self, commands):
        block = block_of(STAGE_INSTALLERS["rock"])
        block = block.replace(f"sysctl_conf={INSTALLED_CONF}\n", f"sysctl_conf='{self.conf}'\n")
        block = block.replace(f"rmem_max_file={LIMIT_FILE}\n", f"rmem_max_file='{self.limit}'\n")
        assert str(self.conf) in block and str(self.limit) in block
        return subprocess.run(["bash", "-s"], input="set -euo pipefail\n" + block + commands,
                              capture_output=True, text=True, timeout=10)


def test_a_box_without_the_setting_gets_it_and_the_limit_is_raised(tmp_path):
    box = Box(tmp_path)
    result = box.run(f"install_receive_buffer_limit '{box.stage}'\n"
                     "echo added=$sysctl_conf_added saved=${#sysctl_conf_saved[@]}\n")
    assert result.returncode == 0, result.stderr
    assert "added=1 saved=0" in result.stdout
    assert box.conf.read_text() == CONF.read_text()
    assert box.conf.stat().st_mode & 0o777 == 0o644
    assert int(box.limit.read_text()) == asked_by_the_core()
    assert "Raised the network receive buffer limit" in result.stdout


def test_a_higher_limit_on_the_box_is_left_alone(tmp_path):
    box = Box(tmp_path)
    box.limit.write_text("16777216\n")
    result = box.run(f"install_receive_buffer_limit '{box.stage}'\n")
    assert result.returncode == 0, result.stderr
    assert box.limit.read_text() == "16777216\n"
    assert "Left unchanged" in result.stdout
    assert box.conf.is_file()


def test_rollback_removes_the_file_only_when_this_install_added_it(tmp_path):
    box = Box(tmp_path)
    result = box.run(f"install_receive_buffer_limit '{box.stage}'\n"
                     "remove_added_receive_buffer_limit\n")
    assert result.returncode == 0, result.stderr
    assert not box.conf.exists()
    # The limit in force stays raised: nothing here ever lowers it.
    assert int(box.limit.read_text()) == asked_by_the_core()


def test_a_file_an_earlier_install_left_goes_into_the_rollback_copy(tmp_path):
    box = Box(tmp_path)
    box.conf.parent.mkdir(parents=True)
    box.conf.write_text("# from an earlier install\nnet.core.rmem_max = 4194304\n")
    result = box.run(f"install_receive_buffer_limit '{box.stage}'\n"
                     "echo added=$sysctl_conf_added saved=${sysctl_conf_saved[0]}\n"
                     "remove_added_receive_buffer_limit\n")
    assert result.returncode == 0, result.stderr
    # Not counted as added, so rollback's files.tar is what puts the old one
    # back; the block names it for that archive, relative to /.
    assert f"added=0 saved={str(box.conf).lstrip('/')}" in result.stdout
    assert box.conf.read_text() == CONF.read_text()


def test_an_older_stage_without_the_setting_leaves_the_box_as_found(tmp_path):
    box = Box(tmp_path, stage_carries_it=False)
    result = box.run(f"install_receive_buffer_limit '{box.stage}'\n"
                     "echo added=$sysctl_conf_added\n")
    assert result.returncode == 0, result.stderr
    assert "added=0" in result.stdout
    assert "carries no network receive buffer setting" in result.stdout
    assert not box.conf.exists()
    assert int(box.limit.read_text()) == STOCK


def test_a_failing_helper_warns_and_does_not_stop_the_install(tmp_path):
    box = Box(tmp_path)
    (box.stage / "usr/local/libexec/nereusd/raise-receive-buffer-limit").write_text("exit 3\n")
    result = box.run(f"install_receive_buffer_limit '{box.stage}'\necho carried-on\n")
    assert result.returncode == 0, result.stderr
    assert "carried-on" in result.stdout
    assert "WARNING: the network receive buffer limit was not applied now" in result.stderr
    assert box.conf.is_file()


@pytest.mark.parametrize("name,stage", [("rock", "$stage"), ("pi-install", "$unpack"),
                                        ("pi-upgrade", "$unpack")])
def test_each_installer_applies_the_limit_before_it_starts_the_core(name, stage):
    text = STAGE_INSTALLERS[name].read_text()
    call = f'\ninstall_receive_buffer_limit "{stage}"\n'
    assert text.count(call) == 1
    assert text.index(END) < text.index(call), "the block is defined before it is used"
    assert text.index(call) < text.index("systemctl start nereusd\n", text.index(call))


@pytest.mark.parametrize("name", ["rock", "pi-upgrade"])
def test_the_installers_that_roll_back_restore_and_remove_the_file(name):
    text = STAGE_INSTALLERS[name].read_text()
    rollback = text.split("rollback() {", 1)[1].split("\n}\n", 1)[0]
    assert "remove_added_receive_buffer_limit || restored=0" in rollback
    assert rollback.index('tar -C / -xpf "$backup/files.tar"') \
        < rollback.index("remove_added_receive_buffer_limit")
    # The earlier file rides in the archive rollback unpacks.
    archive = re.search(r'tar -C / -cpf "\$backup/files\.tar"(?:\\\n|[^\n])*', text)[0]
    assert '${sysctl_conf_saved[@]+"${sysctl_conf_saved[@]}"}' in archive
    assert text.index(END) < text.index('tar -C / -cpf "$backup/files.tar"')


# --- the .deb packages and the component they are made from -------------------

def test_the_package_applies_the_limit_only_on_a_running_system():
    postinst = (ROOT / "packaging/deb/postinst").read_text()
    step = re.search(r'if \[ "\$\{1:-\}" = configure \] && \[ -d /run/systemd/system \] \\\n'
                     r'\s+&& \[ -x (\S+) \]; then\n\s+(\S+) (\S+) \|\| true\nfi', postinst)
    assert step, "the postinst must guard the helper and never fail on it"
    assert step[1] == step[2] == "/usr/libexec/nereusd/raise-receive-buffer-limit"
    assert step[3] == INSTALLED_CONF
    # Removing the package removes the file with the rest; no script lowers
    # the limit in force.
    postrm = (ROOT / "packaging/deb/postrm").read_text()
    assert "sysctl -w" not in postrm and "/proc/sys" not in postrm


def test_the_daemon_component_installs_the_file_and_the_helper():
    cmake = (ROOT / "CMakeLists.txt").read_text()
    conf_rule = re.search(r"install\(FILES packaging/nereusd-receive-buffer\.conf\s+"
                          r'DESTINATION "/usr/lib/sysctl\.d"\s+RENAME 10-nereusd\.conf\s+'
                          r"COMPONENT nereusd\s+EXCLUDE_FROM_ALL\)", cmake)
    helper_rule = re.search(r"install\(PROGRAMS packaging/nereusd-raise-receive-buffer-limit\.sh\s+"
                            r'DESTINATION "\$\{CMAKE_INSTALL_LIBEXECDIR\}/nereusd"\s+'
                            r"RENAME raise-receive-buffer-limit\s+"
                            r"COMPONENT nereusd\s+EXCLUDE_FROM_ALL\)", cmake)
    assert conf_rule and helper_rule


def test_the_package_checks_require_them():
    verify = (ROOT / "packaging/station-image/verify-package.sh").read_text()
    release = (ROOT / ".github/workflows/release.yml").read_text()
    for needle in ("usr/lib/sysctl.d/10-nereusd.conf$", "usr/libexec/nereusd/raise-receive-buffer-limit$"):
        assert needle in verify
        assert needle in release


def test_the_scripts_parse():
    for shell in shells():
        if shell == "bash":
            continue
        subprocess.run([shell, "-n", str(HELPER)], check=True)
        subprocess.run([shell, "-n", str(ROOT / "packaging/deb/postinst")], check=True)
    for path in STAGE_INSTALLERS.values():
        subprocess.run(["bash", "-n", str(path)], check=True)
