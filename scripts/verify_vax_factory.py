#!/usr/bin/env python3
"""Check source/bundle plists and an extracted VAX component, without loading it.

NereusSDR-original; no code ported.
2026-10-12: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
"""
import argparse
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

# Fixed Nereus registration; distinct from Longpath's factory at 551576e.
FACTORY_UUID = "42B0B248-3FF0-4993-AB3A-39210FC08B3C"
PLUGIN_TYPE_UUID = "443ABAB8-E7B3-491A-B985-BEB9187030DB"
FACTORY_FUNCTION = "NereusSDRVAX_Create"
STABLE_METADATA = {
    "CFBundleExecutable": "NereusSDRVAX",
    "CFBundleIdentifier": "com.nereussdr.vax",
    "CFBundleName": "NereusSDR VAX",
    "CFBundlePackageType": "BNDL",
    "CFBundleShortVersionString": "0.1.0",
    "CFBundleVersion": "1",
    "AudioServerPlugIn_BundleResourceID": "com.nereussdr.vax",
}


class UniqueKeyDict(dict):
    """Fail closed before XML or binary plist keys can overwrite each other."""
    def __setitem__(self, key, value):
        if key in self:
            raise ValueError(f"duplicate plist dictionary key: {key}")
        super().__setitem__(key, value)


def validate_plist(path):
    """Reject a coherent but stale factory as well as mismatched references."""
    with Path(path).open("rb") as source:
        metadata = plistlib.load(source, dict_type=UniqueKeyDict)
    if not isinstance(metadata, dict):
        raise ValueError(f"{path}: expected a plist dictionary")
    if metadata.get("CFPlugInFactories") != {FACTORY_UUID: FACTORY_FUNCTION}:
        raise ValueError(f"{path}: expected only approved Nereus factory {FACTORY_UUID}")
    if metadata.get("CFPlugInTypes") != {PLUGIN_TYPE_UUID: [FACTORY_UUID]}:
        raise ValueError(f"{path}: plug-in type must reference the approved factory exactly once")
    for key, expected in STABLE_METADATA.items():
        if metadata.get(key) != expected:
            raise ValueError(f"{path}: unexpected {key}; registration fix preserves this identity")
    return metadata


def validate_package(path, *, product=False):
    """Expand actual component/product Payload; never run installer scripts."""
    with tempfile.TemporaryDirectory(prefix="nereus-vax-factory-") as scratch:
        expanded = Path(scratch) / "expanded"
        subprocess.run(["pkgutil", "--expand-full", str(Path(path).resolve()), str(expanded)],
                       check=True, capture_output=True, text=True)
        component = expanded / "NereusSDR-vax.pkg" if product else expanded
        package_info = ET.parse(component / "PackageInfo").getroot()
        if package_info.get("identifier") != "com.nereussdr.vax":
            raise ValueError(f"{path}: expected the com.nereussdr.vax component package")
        if product:
            vax_components = [info.parent for info in expanded.rglob("PackageInfo")
                              if ET.parse(info).getroot().get("identifier") == "com.nereussdr.vax"]
            if vax_components != [component]:
                raise ValueError(f"{path}: expected exactly one NereusSDR-vax.pkg component")
        expected = component / "Payload" / "NereusSDRVAX.driver" / "Contents" / "Info.plist"
        payloads = list(expanded.rglob("NereusSDRVAX.driver/Contents/Info.plist"))
        if payloads != [expected]:
            raise ValueError(f"{path}: expected exactly one VAX plist at {expected.relative_to(expanded)}")
        validate_plist(expected)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plist", action="append", default=[], type=Path,
                        help="source, built or staged Info.plist (repeatable)")
    parser.add_argument("--package", action="append", default=[], type=Path,
                        help="flat NereusSDR-vax.pkg component; inspect its extracted Payload")
    parser.add_argument("--product-package", action="append", default=[], type=Path,
                        help="product archive; inspect exactly one NereusSDR-vax.pkg component")
    args = parser.parse_args(argv)
    if not args.plist and not args.package and not args.product_package:
        parser.error("provide at least one --plist, --package or --product-package")
    try:
        for path in args.plist:
            validate_plist(path)
            print(f"VAX factory verified: {path}")
        for path in args.package:
            validate_package(path)
            print(f"VAX component payload verified: {path}")
        for path in args.product_package:
            validate_package(path, product=True)
            print(f"VAX product payload verified: {path}")
    except (OSError, ValueError, plistlib.InvalidFileException, ET.ParseError,
            subprocess.CalledProcessError) as error:
        print(f"VAX factory verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
