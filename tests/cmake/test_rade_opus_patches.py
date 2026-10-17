#!/usr/bin/env python3
"""Exercise the RADE Opus patch upgrade using the pinned, local archive."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HASHES = {
    'h': '03e9864f07dbab0a2d0a6599187f5a8b4e5c4e250631357f40fa80f828527cb3',
    'c': '52544d64705c36ecf3a096c4b929c4dcd7547a342c227fbec071456f2acf7bee',
}

class OpusPatches(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = pathlib.Path(self.temp.name)
        with zipfile.ZipFile(ARCHIVE) as archive:
            for extension in HASHES:
                name = next(n for n in archive.namelist() if n.endswith('/dnn/nnet.' + extension))
                target = self.source / 'dnn' / ('nnet.' + extension)
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(archive.read(name))

    def patch(self, extension):
        subprocess.run([shutil.which('patch'), '--batch', '--forward',
                        str(self.source / 'dnn' / ('nnet.' + extension)), '-i',
                        str(ROOT / 'third_party/rade/src' / ('opus-nnet.' + extension + '.diff'))],
                       check=True, capture_output=True)

    def upgrade(self):
        helper = ROOT / 'third_party/rade/cmake/NereusOpusPatches.cmake'
        return subprocess.run(['cmake', '-DSOURCE_DIR=' + str(self.source),
                               '-DPATCH_EXECUTABLE=' + shutil.which('patch'), '-P', str(helper)],
                              capture_output=True, text=True)

    def assertPatched(self):
        import hashlib
        for extension, expected in HASHES.items():
            self.assertEqual(hashlib.sha256((self.source / 'dnn' / ('nnet.' + extension)).read_bytes()).hexdigest(), expected)

    def test_pristine(self):
        result = self.upgrade()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertPatched()

    def test_v1_visibility_patch_already_applied(self):
        self.patch('h')
        result = self.upgrade()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertPatched()

    def test_both_already_applied(self):
        self.patch('h')
        self.patch('c')
        result = self.upgrade()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertPatched()

    def test_unexpected_source_is_rejected_before_mutation(self):
        header = self.source / 'dnn/nnet.h'
        before = header.read_bytes()
        with (self.source / 'dnn/nnet.c').open('ab') as source:
            source.write(b'\n/* unexpected local change */\n')
        result = self.upgrade()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(header.read_bytes(), before)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--opus-archive', required=True)
    args, remaining = parser.parse_known_args()
    ARCHIVE = args.opus_archive
    unittest.main(argv=[__file__, *remaining])
