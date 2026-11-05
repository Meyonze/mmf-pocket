#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CLI boundaries and JSON contract using one original generated SMAF."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
GENERATOR = Path(__file__).with_name('generate-fm-probes.py')
SPEC = importlib.util.spec_from_file_location('pressure_fm', GENERATOR)
FM = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = FM
SPEC.loader.exec_module(FM)


class CliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'private-filename.mmf'
        self.data = FM.encode(FM.CASES[0])
        self.path.write_bytes(self.data)

    def run_cli(self, *args):
        return subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=30)

    def report(self, *args):
        process = self.run_cli(self.path, *args)
        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result['schema_version'], 1)
        self.assertEqual(sum(w['frames'] for w in result['windows']), result['frames'])
        for key, value in result['totals'].items():
            self.assertEqual(sum(w['counters'][key] for w in result['windows']), value)
        self.assertNotIn('private-filename', process.stdout)
        self.assertNotIn('FM probe', process.stdout)
        self.assertEqual(self.path.read_bytes(), self.data)
        self.assertEqual(list(Path(self.temp.name).iterdir()), [self.path])
        return result

    def test_help_and_usage(self):
        self.assertEqual(self.run_cli('--help').returncode, 0)
        self.assertEqual(self.run_cli().returncode, 2)
        self.assertEqual(self.run_cli(self.path, 1, 2).returncode, 2)

    def test_invalid_cap(self):
        for value in ('nan', 'inf', '-inf', '0', '-1', '601', '0.000001', '1suffix', ''):
            with self.subTest(value=value):
                process = self.run_cli(self.path, value)
                self.assertEqual(process.returncode, 2)
                self.assertEqual(process.stdout, '')

    def test_invalid_input(self):
        for data in (b'', b'not an mmf but long enough'):
            self.path.write_bytes(data)
            process = self.run_cli(self.path, '.3')
            self.assertEqual(process.returncode, 1)
            self.assertEqual(process.stdout, '')
        self.assertEqual(self.run_cli(self.path.with_suffix('.missing')).returncode, 1)

    def test_exact_cap_excludes_boundary_event(self):
        report = self.report('.25')
        self.assertEqual(report['frames'], 11025)
        self.assertEqual(report['totals']['fm_notes'], 0)
        self.assertTrue(report['cap_reached'])
        self.assertFalse(report['finished'])
        self.assertEqual(report['windows'][-1]['frames'], 2205)

    def test_timed_note_and_partial_report(self):
        report = self.report('.3')
        self.assertEqual(report['frames'], 13230)
        self.assertEqual(report['totals']['fm_notes'], 1)
        self.assertEqual(report['windows'][2]['counters']['fm_notes'], 1)
        self.assertGreater(report['windows'][2]['rms'], 0)

    def test_natural_completion(self):
        report = self.report()
        self.assertTrue(report['finished'])
        self.assertFalse(report['cap_reached'])
        self.assertFalse(report['tail_limit_reached'])
        self.assertEqual(report['totals']['fm_notes'], 1)
        self.assertEqual(report['totals']['limited_frames'], 0)


if __name__ == '__main__':
    unittest.main()
