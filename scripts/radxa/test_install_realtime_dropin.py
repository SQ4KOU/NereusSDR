#!/usr/bin/env python3
"""Exercise the installer's real-time priority drop-in block in a temporary directory.

On a kernel built with CONFIG_RT_GROUP_SCHED=y the Core is refused real-time
priority while the cgroup CPU controller is on, and login managers switch it
on. The drop-in stops them asking for it. Only the paths are redirected and
systemctl is a stand-in; no service commands run.
"""
import gzip
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
INSTALLER = Path(os.environ.get("RETENTION_TEST_INSTALLER", HERE / "install-core-selector.sh"))
DROPIN = "/etc/systemd/system/user@.service.d/nereusd-realtime-priority.conf"
CONFIG_GZ = "/proc/config.gz"
CONFIG_BOOT = "/boot/config-$(uname -r)"
CONTROLLERS = "/sys/fs/cgroup/cgroup.subtree_control"
BEGIN = "# BEGIN real-time priority drop-in\n"
END = "# END real-time priority drop-in\n"
SET = "CONFIG_RT_GROUP_SCHED=y\n"
NOT_SET = "# CONFIG_RT_GROUP_SCHED is not set\n"
# What Debian's user@.service asks for, and what it asks for with the drop-in.
STOCK = "cpu memory pids"
WITHOUT_CPU = "memory pids"


class RealtimeDropin(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="nereus-realtime-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        self.dropin = self.root / "user@.service.d" / "nereusd-realtime-priority.conf"
        self.config_gz = self.root / "config.gz"
        self.config_boot = self.root / "config-boot"
        self.controllers = self.root / "cgroup.subtree_control"
        self.text = INSTALLER.read_text()

    def block(self):
        self.assertIn(BEGIN, self.text, "the installer must carry the real-time priority block")
        block = self.text.split(BEGIN, 1)[1].split(END, 1)[0]
        for real, temporary in ((DROPIN, self.dropin), (CONFIG_GZ, self.config_gz),
                                (CONFIG_BOOT, self.config_boot), (CONTROLLERS, self.controllers)):
            self.assertIn("=" + real + "\n", block)
            block = block.replace("=" + real + "\n", "='" + str(temporary) + "'\n")
        return block

    def run_block(self, commands, delegated=WITHOUT_CPU):
        # systemctl answers only the one question the block asks.
        stand_in = ("systemctl() {\n"
                    "    [[ \"$*\" == \"show user@0.service -p DelegateControllers --value\" ]] || return 1\n"
                    "    echo '" + delegated + "'\n"
                    "}\n")
        return subprocess.run(["bash", "-s"],
                              input="set -euo pipefail\n" + stand_in + self.block() + commands,
                              capture_output=True, text=True, timeout=10)

    def kernel_config_gz(self, line, lines_after=0):
        with gzip.open(self.config_gz, "wt") as output:
            output.write("CONFIG_CGROUPS=y\n" + line)
            for n in range(lines_after):
                output.write("CONFIG_FILLER_%d=y\n" % n)

    def test_vendor_kernel_gets_the_dropin_without_the_cpu_controller(self):
        self.kernel_config_gz(SET)
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=1", result.stdout)
        self.assertIn("added " + str(self.dropin), result.stdout)
        settings = [line for line in self.dropin.read_text().splitlines()
                    if line and not line.startswith("#")]
        # The empty assignment comes first: a list adds to the list before it,
        # so without the reset Debian's "pids memory cpu" would stand.
        self.assertEqual(settings, ["[Service]", "Delegate=", "Delegate=pids memory"])
        self.assertEqual(self.dropin.stat().st_mode & 0o777, 0o644)
        self.assertEqual(self.dropin.parent.stat().st_mode & 0o777, 0o755)

    def test_setting_far_from_the_end_of_a_long_config_is_still_found(self):
        # A search that stops at the first match cuts the decompressor short,
        # and under pipefail that reads as "not set".
        self.kernel_config_gz(SET, lines_after=200000)
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=1", result.stdout)
        self.assertTrue(self.dropin.is_file())

    def test_kernel_without_the_setting_is_left_alone(self):
        self.kernel_config_gz(NOT_SET)
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=0", result.stdout)
        self.assertIn("not added", result.stdout)
        self.assertFalse(self.dropin.parent.exists())

    def test_setting_as_a_module_or_inside_a_longer_name_does_not_count(self):
        self.kernel_config_gz("CONFIG_RT_GROUP_SCHED=m\nCONFIG_RT_GROUP_SCHED=yes\n"
                              "CONFIG_NOT_RT_GROUP_SCHED=y\n")
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=0", result.stdout)
        self.assertFalse(self.dropin.exists())

    def test_boot_config_is_read_when_proc_has_none(self):
        self.config_boot.write_text("CONFIG_CGROUPS=y\n" + SET)
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=1", result.stdout)
        self.assertTrue(self.dropin.is_file())

    def test_unreadable_kernel_config_adds_nothing(self):
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=0", result.stdout)
        self.assertFalse(self.dropin.parent.exists())

    def test_existing_dropin_is_left_as_found_and_survives_rollback(self):
        self.kernel_config_gz(SET)
        self.dropin.parent.mkdir(parents=True)
        self.dropin.write_text("[Service]\nDelegate=yes\n# mine\n")
        result = self.run_block("add_realtime_dropin\necho flag=$realtime_dropin_added\n"
                                "remove_added_realtime_dropin\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flag=0", result.stdout)
        self.assertIn("already exists; left unchanged", result.stdout)
        self.assertEqual(self.dropin.read_text(), "[Service]\nDelegate=yes\n# mine\n")

    def test_rollback_removes_only_what_this_install_added(self):
        self.kernel_config_gz(SET)
        self.dropin.parent.mkdir(parents=True)
        other = self.dropin.parent / "10-login-barrier.conf"
        other.write_text("[Unit]\nAfter=systemd-user-sessions.service\n")
        result = self.run_block("add_realtime_dropin\n"
                                "test -f '" + str(self.dropin) + "'\n"
                                "remove_added_realtime_dropin\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in self.dropin.parent.iterdir()),
                         ["10-login-barrier.conf"])

    def test_report_warns_when_the_cpu_controller_is_on_as_the_core_starts(self):
        self.kernel_config_gz(SET)
        self.controllers.write_text(STOCK + "\n")
        result = self.run_block("report_cpu_controller\n", delegated=STOCK)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("login managers ask for cgroup controllers: " + STOCK, result.stdout)
        self.assertIn("cgroup controllers on as the Core starts: " + STOCK, result.stdout)
        self.assertIn("is refused real-time priority when it starts", result.stderr)
        self.assertIn("or until a reboot for a user who lingers", result.stderr)

    def test_report_is_quiet_when_the_cpu_controller_is_off(self):
        self.kernel_config_gz(SET)
        self.controllers.write_text(WITHOUT_CPU + "\n")
        result = self.run_block("report_cpu_controller\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("login managers ask for cgroup controllers: " + WITHOUT_CPU, result.stdout)
        self.assertIn("cgroup controllers on as the Core starts: " + WITHOUT_CPU, result.stdout)
        self.assertEqual(result.stderr, "")

    def test_report_does_not_take_cpuset_for_the_cpu_controller(self):
        self.kernel_config_gz(SET)
        self.controllers.write_text("cpuset memory pids\n")
        result = self.run_block("report_cpu_controller\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_report_is_quiet_on_a_kernel_that_grants_priority_either_way(self):
        self.kernel_config_gz(NOT_SET)
        self.controllers.write_text(STOCK + "\n")
        result = self.run_block("report_cpu_controller\n", delegated=STOCK)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_report_never_fails_an_install(self):
        # No cgroup file to read, and a systemctl that answers nothing.
        self.kernel_config_gz(SET)
        result = self.run_block("systemctl() { return 1; }\nreport_cpu_controller\necho still-running\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("login managers ask for cgroup controllers: unknown", result.stdout)
        self.assertIn("cgroup controllers on as the Core starts: unknown", result.stdout)
        self.assertIn("still-running", result.stdout)

    def test_install_writes_the_dropin_before_reload_and_reports_before_start(self):
        install = self.text.split("trap rollback EXIT", 1)[1].split("trap - EXIT", 1)[0]
        self.assertIn("\nadd_realtime_dropin\nsystemctl daemon-reload\n"
                      "report_cpu_controller\nsystemctl start nereusd\n", install)
        rollback = self.text.split("rollback() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("remove_added_realtime_dropin || restored=0", rollback)
        self.assertLess(rollback.index("remove_added_realtime_dropin"),
                        rollback.index("systemctl daemon-reload"))
        # The functions exist before the trap that may call them.
        self.assertLess(self.text.index(END), self.text.index("trap rollback EXIT"))


if __name__ == "__main__":
    unittest.main()
