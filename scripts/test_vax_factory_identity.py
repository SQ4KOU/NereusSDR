#!/usr/bin/env python3
"""Offline VAX registration and package-gate regression tests.

NereusSDR-original; no code ported.
2026-10-12: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
"""
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import verify_vax_factory as validator

REPO = Path(__file__).resolve().parents[1]
# Independently pin the approved identity; matching two stale references fails.
APPROVED = "42B0B248-3FF0-4993-AB3A-39210FC08B3C"
# Longpath hal-plugin/Info.plist:26,33 [@551576e]; historical Nereus collision.
LEGACY = "F58308D0-1ABD-4D44-845B-F308A64CDA16"
PLUGIN_TYPE = "443ABAB8-E7B3-491A-B985-BEB9187030DB"


def metadata(factory=APPROVED):
    result = dict(validator.STABLE_METADATA)
    result.update(CFPlugInFactories={factory: "NereusSDRVAX_Create"},
                  CFPlugInTypes={PLUGIN_TYPE: [factory]})
    return result


def write_plist(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(plistlib.dumps(contents))


class FactoryIdentityTests(unittest.TestCase):
    def test_source_has_approved_factory_and_coherent_type(self):
        source = plistlib.loads((REPO / "hal-plugin/Info.plist").read_bytes())
        self.assertEqual(source["CFPlugInFactories"], {APPROVED: "NereusSDRVAX_Create"})
        self.assertEqual(source["CFPlugInTypes"], {PLUGIN_TYPE: [APPROVED]})
        self.assertEqual(validator.FACTORY_UUID, APPROVED)
        validator.validate_plist(REPO / "hal-plugin/Info.plist")

    def test_coherent_legacy_registration_and_alias_are_rejected(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "Info.plist"
            for bad in (metadata(LEGACY), metadata()):
                if bad["CFPlugInFactories"].get(APPROVED):
                    bad["CFPlugInFactories"][LEGACY] = "NereusSDRVAX_Create"
                write_plist(path, bad)
                with self.assertRaisesRegex(ValueError, "approved Nereus factory"):
                    validator.validate_plist(path)

    def test_type_symbol_and_stable_metadata_drift_are_rejected(self):
        variants = []
        bad = metadata(); bad["CFPlugInTypes"] = {PLUGIN_TYPE: [LEGACY]}; variants.append(bad)
        bad = metadata(); bad["CFPlugInFactories"][APPROVED] = "LongpathVAX_Create"; variants.append(bad)
        bad = metadata(); bad["CFPlugInTypes"] = {LEGACY: [APPROVED]}; variants.append(bad)
        for key in validator.STABLE_METADATA:
            bad = metadata(); bad[key] = "changed"; variants.append(bad)
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "Info.plist"
            for bad in variants:
                with self.subTest(metadata=bad):
                    write_plist(path, bad)
                    with self.assertRaises(ValueError):
                        validator.validate_plist(path)

    def test_raw_xml_duplicate_dictionary_keys_are_rejected(self):
        # Raw entries deliberately survive serialization; normalized Python
        # dictionaries cannot represent the duplicate-key parser bypass.
        xml = plistlib.dumps(metadata()).decode()
        variants = {
            "top-factories-old-first": xml.replace(
                "<key>CFPlugInFactories</key>",
                f"<key>CFPlugInFactories</key><dict><key>{LEGACY}</key>"
                "<string>NereusSDRVAX_Create</string></dict><key>CFPlugInFactories</key>", 1),
            "top-types-old-first": xml.replace(
                "<key>CFPlugInTypes</key>",
                f"<key>CFPlugInTypes</key><dict><key>{PLUGIN_TYPE}</key>"
                f"<array><string>{LEGACY}</string></array></dict><key>CFPlugInTypes</key>", 1),
            "nested-factory-old-first": xml.replace(
                f"<key>{APPROVED}</key>",
                f"<key>{APPROVED}</key><string>LongpathVAX_Create</string><key>{APPROVED}</key>", 1),
            "nested-type-old-first": xml.replace(
                f"<key>{PLUGIN_TYPE}</key>",
                f"<key>{PLUGIN_TYPE}</key><array><string>{LEGACY}</string></array>"
                f"<key>{PLUGIN_TYPE}</key>", 1),
            "stable-identity-old-first": xml.replace(
                "<key>CFBundleIdentifier</key>",
                "<key>CFBundleIdentifier</key><string>com.other.product</string>"
                "<key>CFBundleIdentifier</key>", 1),
            "unrelated-nested-duplicate": xml.replace(
                "<dict>", "<dict><key>Unrelated</key><dict><key>Repeated</key><string>first</string>"
                "<key>Repeated</key><string>last</string></dict>", 1),
        }
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "Info.plist"
            for name, raw_xml in variants.items():
                with self.subTest(case=name):
                    path.write_text(raw_xml)
                    with self.assertRaisesRegex(ValueError, "duplicate plist dictionary key"):
                        validator.validate_plist(path)

    def test_raw_binary_duplicate_dictionary_keys_are_rejected(self):
        # Mutate root key references in an otherwise valid binary plist, so
        # duplicate keys reach the binary reader rather than a Python dict.
        contents = metadata()
        contents.update(ExtraFirst="first", ExtraSecond="last")
        raw = bytearray(plistlib.dumps(contents, fmt=plistlib.FMT_BINARY))
        offset_size, reference_size = raw[-26], raw[-25]
        top_object = int.from_bytes(raw[-16:-8], "big")
        offset_table = int.from_bytes(raw[-8:], "big")
        object_offset = int.from_bytes(raw[offset_table + top_object * offset_size:
                                           offset_table + (top_object + 1) * offset_size], "big")
        # This small root dictionary uses the inline count (no length object).
        self.assertEqual(raw[object_offset], 0xD0 | len(contents))
        keys = sorted(contents)
        first = object_offset + 1 + keys.index("ExtraFirst") * reference_size
        second = object_offset + 1 + keys.index("ExtraSecond") * reference_size
        raw[first:first + reference_size] = raw[second:second + reference_size]
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "Info.plist"
            path.write_bytes(raw)
            with self.assertRaises((ValueError, plistlib.InvalidFileException)):
                validator.validate_plist(path)

    def test_missing_malformed_and_binary_plists(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "Info.plist"
            with self.assertRaises(OSError):
                validator.validate_plist(path)
            path.write_bytes(b"not a plist")
            with self.assertRaises(plistlib.InvalidFileException):
                validator.validate_plist(path)
            path.write_bytes(plistlib.dumps(metadata(), fmt=plistlib.FMT_BINARY))
            validator.validate_plist(path)

    def test_package_reads_extracted_payload_not_source_or_scripts(self):
        for factory, identifier, valid in ((APPROVED, "com.nereussdr.vax", True),
                                          (LEGACY, "com.nereussdr.vax", False),
                                          (APPROVED, "com.other.component", False)):
            def expand(command, **kwargs):
                self.assertEqual(command[:2], ["pkgutil", "--expand-full"])
                self.assertTrue(kwargs["check"])
                root = Path(command[3]); root.mkdir()
                (root / "PackageInfo").write_text(f'<pkg-info identifier="{identifier}"/>')
                write_plist(root / "Payload/NereusSDRVAX.driver/Contents/Info.plist", metadata(factory))
                # Extraction never executes this poison installer script.
                (root / "Scripts").mkdir()
                (root / "Scripts/postinstall").write_text("exit 99\n")
            with self.subTest(factory=factory, identifier=identifier):
                with mock.patch.object(validator.subprocess, "run", side_effect=expand) as run:
                    if valid:
                        validator.validate_package("private-vax.pkg")
                    else:
                        with self.assertRaises(ValueError):
                            validator.validate_package("private-vax.pkg")
                    self.assertEqual(run.call_count, 1)

    def test_product_requires_expected_unique_component_and_payload(self):
        for fault in (None, "missing", "duplicate", "misplaced", "duplicate-payload"):
            def expand(command, **kwargs):
                root = Path(command[3]); root.mkdir()
                component = root / "NereusSDR-vax.pkg"; component.mkdir()
                (component / "PackageInfo").write_text('<pkg-info identifier="com.nereussdr.vax"/>')
                if fault != "missing":
                    location = component / ("Other" if fault == "misplaced" else "Payload")
                    write_plist(location / "NereusSDRVAX.driver/Contents/Info.plist", metadata())
                if fault == "duplicate":
                    duplicate = root / "Duplicate.pkg"; duplicate.mkdir()
                    (duplicate / "PackageInfo").write_text('<pkg-info identifier="com.nereussdr.vax"/>')
                if fault == "duplicate-payload":
                    write_plist(root / "Duplicate/Payload/NereusSDRVAX.driver/Contents/Info.plist", metadata())
            with self.subTest(fault=fault), mock.patch.object(validator.subprocess, "run", side_effect=expand):
                if fault:
                    with self.assertRaises(ValueError):
                        validator.validate_package("private-product.pkg", product=True)
                else:
                    validator.validate_package("private-product.pkg", product=True)

    def test_failed_or_missing_package_payload_fails_closed(self):
        with mock.patch.object(validator.subprocess, "run", side_effect=
                               subprocess.CalledProcessError(1, ["pkgutil"])):
            with self.assertRaises(subprocess.CalledProcessError):
                validator.validate_package("private-vax.pkg")
        with mock.patch.object(validator.subprocess, "run"):
            with self.assertRaises(OSError):
                validator.validate_package("private-vax.pkg")


# These executables replace package/build tools in a private fixture. The .pkg
# is a zip of fixture metadata only; this is an offline gate test, not an Apple
# package/executable or native-driver validation claim.
MOCK_TOOL = r"""#!/usr/bin/env python3
import os
from pathlib import Path
import plistlib
import shutil
import sys
import zipfile
command = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["MOCK_EVENTS"], "a") as events:
    events.write(command + "\n")
if command in ("cmake", "sysctl"):
    sys.exit("unexpected build or machine probe in offline fixture")
if command == "cp":
    recursive = args[0] == "-R"
    source, target = map(Path, args[1:] if recursive else args)
    if recursive:
        dest = target / source.name
        shutil.copytree(source, dest)
        if source.name == "NereusSDRVAX.driver" and os.environ.get("MOCK_STALE_STAGE"):
            plist = dest / "Contents/Info.plist"
            data = plistlib.loads(plist.read_bytes())
            old = "F58308D0-1ABD-4D44-845B-F308A64CDA16"
            data["CFPlugInFactories"] = {old: "NereusSDRVAX_Create"}
            data["CFPlugInTypes"] = {"443ABAB8-E7B3-491A-B985-BEB9187030DB": [old]}
            plist.write_bytes(plistlib.dumps(data))
    else:
        shutil.copyfile(source, target)
elif command == "pkgbuild":
    root = Path(args[args.index("--root") + 1])
    identifier = args[args.index("--identifier") + 1]
    with zipfile.ZipFile(args[-1], "w") as archive:
        archive.writestr("PackageInfo", '<pkg-info identifier="' + identifier + '"/>')
        for path in root.rglob("*"):
            if path.is_file():
                data = path.read_bytes()
                if identifier == "com.nereussdr.vax" and path.name == "Info.plist" and os.environ.get("MOCK_STALE_PAYLOAD"):
                    data = plistlib.loads(data)
                    old = "F58308D0-1ABD-4D44-845B-F308A64CDA16"
                    data["CFPlugInFactories"] = {old: "NereusSDRVAX_Create"}
                    data["CFPlugInTypes"] = {"443ABAB8-E7B3-491A-B985-BEB9187030DB": [old]}
                    data = plistlib.dumps(data)
                archive.writestr("Payload/" + str(path.relative_to(root)), data)
elif command == "pkgutil":
    assert args[0] == "--expand-full"
    with zipfile.ZipFile(args[1]) as archive:
        archive.extractall(args[2])
elif command == "productbuild":
    packages = Path(args[args.index("--package-path") + 1])
    with zipfile.ZipFile(args[-1], "w") as product:
        for component in packages.glob("*.pkg"):
            with zipfile.ZipFile(component) as archive:
                for name in archive.namelist():
                    data = archive.read(name)
                    if component.name == "NereusSDR-vax.pkg" and name.endswith("Info.plist") and os.environ.get("MOCK_STALE_PRODUCT"):
                        data = plistlib.loads(data)
                        old = "F58308D0-1ABD-4D44-845B-F308A64CDA16"
                        data["CFPlugInFactories"] = {old: "NereusSDRVAX_Create"}
                        data["CFPlugInTypes"] = {"443ABAB8-E7B3-491A-B985-BEB9187030DB": [old]}
                        data = plistlib.dumps(data)
                    product.writestr(component.name + "/" + name, data)
else:
    sys.exit("unexpected offline command")
"""


class InstallerGateTests(unittest.TestCase):
    def run_installer(self, *, factory=APPROVED, fallback=False, fault=None):
        with tempfile.TemporaryDirectory(prefix="nereus-vax-offline-") as scratch:
            root = Path(scratch)
            for name in ("packaging/macos/hal-installer.sh", "scripts/verify_vax_factory.py",
                         "hal-plugin/Info.plist"):
                target = root / name; target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPO / name, target)
            # Poison script is copied into staging but must never run.
            (root / "packaging/macos/hal-postinstall.sh").write_text("exit 99\n")
            (root / "build/NereusSDR.app").mkdir(parents=True)
            if not fallback:
                (root / "hal-plugin/CMakeLists.txt").write_text("offline fixture\n")
            bundle_root = root / ("build" if fallback else "build-hal")
            write_plist(bundle_root / "NereusSDRVAX.driver/Contents/Info.plist", metadata(factory))
            mock_bin = root / "mock-bin"; mock_bin.mkdir()
            for tool in ("cp", "cmake", "sysctl", "pkgbuild", "pkgutil", "productbuild"):
                path = mock_bin / tool; path.write_text(MOCK_TOOL); path.chmod(0o755)
            events = root / "events.log"
            env = dict(os.environ, VERSION="2026.10.0", PKG_SUFFIX="offline",
                       PATH=str(mock_bin) + os.pathsep + os.environ["PATH"], MOCK_EVENTS=str(events))
            # Ambient mock flags cannot accidentally steer another case.
            env.pop("MOCK_STALE_STAGE", None); env.pop("MOCK_STALE_PAYLOAD", None)
            env.pop("MOCK_STALE_PRODUCT", None)
            env.pop("APPLE_INSTALLER_ID", None)
            if fault:
                env[fault] = "1"
            result = subprocess.run(["bash", "packaging/macos/hal-installer.sh"], cwd=root,
                                    env=env, capture_output=True, text=True, timeout=20)
            actions = events.read_text().splitlines() if events.exists() else []
            staged = (root / "build/pkg-staging").exists()
            return result, actions, staged

    def test_fresh_primary_and_fallback_bundles_reach_product_gate(self):
        for fallback in (False, True):
            with self.subTest(fallback=fallback):
                result, actions, staged = self.run_installer(fallback=fallback)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue(staged)
                self.assertEqual(actions.count("pkgutil"), 2)
                self.assertEqual(actions.count("productbuild"), 1)
                self.assertLess(actions.index("pkgutil"), actions.index("productbuild"))
                self.assertEqual(actions[-1], "pkgutil")
                self.assertNotIn("cmake", actions); self.assertNotIn("sysctl", actions)

    def test_stale_reused_primary_and_fallback_fail_before_staging(self):
        for fallback in (False, True):
            with self.subTest(fallback=fallback):
                result, actions, staged = self.run_installer(factory=LEGACY, fallback=fallback)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("approved Nereus factory", result.stderr)
                self.assertFalse(staged)
                self.assertEqual(actions, [])

    def test_staged_drift_fails_before_component_build(self):
        result, actions, _ = self.run_installer(fault="MOCK_STALE_STAGE")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("approved Nereus factory", result.stderr)
        self.assertNotIn("pkgbuild", actions); self.assertNotIn("productbuild", actions)

    def test_stale_final_product_payload_fails_before_success(self):
        result, actions, _ = self.run_installer(fault="MOCK_STALE_PRODUCT")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("approved Nereus factory", result.stderr)
        self.assertEqual(actions.count("pkgutil"), 2)
        self.assertEqual(actions.count("productbuild"), 1)
        self.assertNotIn("=== Installer created:", result.stdout)

    def test_stale_actual_component_payload_fails_before_productbuild(self):
        result, actions, _ = self.run_installer(fault="MOCK_STALE_PAYLOAD")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("approved Nereus factory", result.stderr)
        self.assertEqual(actions.count("pkgutil"), 1)
        self.assertNotIn("productbuild", actions)


if __name__ == "__main__":
    unittest.main(verbosity=2)
