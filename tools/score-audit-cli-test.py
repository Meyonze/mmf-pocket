#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual score inventory CLI with original synthetic inputs."""
import argparse
import csv
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'tone_probes', ROOT / 'tools/generate-tone-dependency-probes.py')
PROBES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBES)
BINARY = None


class ScoreAuditCliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='score-audit-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'original.mmf'
        self.original, _ = PROBES.encode(PROBES.CASES[0])
        self.source.write_bytes(self.original)

    def run_cli(self, source, report):
        return subprocess.run([str(BINARY), str(source), str(report)],
                              capture_output=True, text=True, timeout=30)

    def test_input_aliases_preserve_original(self):
        aliases = [self.source, self.root / 'hard.tsv', self.root / 'symbolic.tsv']
        aliases[1].hardlink_to(self.source)
        aliases[2].symlink_to(self.source)
        for source in (self.source, self.root):
            for report in aliases:
                with self.subTest(source=source.name, report=report.name):
                    result = self.run_cli(source, report)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn('refusing overwrite', result.stderr)
                    self.assertEqual(self.source.read_bytes(), self.original)
                    self.assertEqual(report.read_bytes(), self.original)

    def test_missing_or_empty_input_does_not_touch_report(self):
        empty = self.root / 'empty'
        empty.mkdir()
        for source in (empty, self.root / 'missing'):
            for exists in (False, True):
                with self.subTest(source=source.name, existing=exists):
                    report = self.root / ('existing.tsv' if exists else 'absent.tsv')
                    if exists:
                        report.write_bytes(b'Keep the previous report\n')
                    result = self.run_cli(source, report)
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                    if exists:
                        self.assertEqual(report.read_bytes(), b'Keep the previous report\n')
                    else:
                        self.assertFalse(report.exists())

    def test_valid_score_keeps_tsv_contract(self):
        report = self.root / 'report.tsv'
        result = self.run_cli(self.source, report)
        self.assertEqual(result.returncode, 0, result.stderr)
        with report.open(newline='', encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream, delimiter='\t'))
        summary = [row for row in rows if row['kind'] == 'summary']
        self.assertEqual(len(summary), 1)
        self.assertEqual(summary[0]['file'], self.source.name)
        self.assertEqual(summary[0]['status'], 'initialized')
        self.assertEqual(summary[0]['complete'], '1')
        self.assertEqual(summary[0]['overflow'], '0')
        self.assertEqual(self.source.read_bytes(), self.original)

    @unittest.skipUnless(Path('/dev/full').exists(), 'requires /dev/full')
    def test_write_failure_is_reported(self):
        result = self.run_cli(self.source, '/dev/full')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('report write failed', result.stderr)
        self.assertEqual(self.source.read_bytes(), self.original)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve()
    unittest.main(argv=[__file__, *remaining])
