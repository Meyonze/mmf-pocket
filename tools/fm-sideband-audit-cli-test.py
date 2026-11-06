#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""JSON contract and analytical controls for the steady sine-PM audit."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--binary', required=True, type=Path)
args, rest = parser.parse_known_args()
BINARY = args.binary.resolve()
sys.argv = [sys.argv[0], *rest]


class SidebandCliTest(unittest.TestCase):
    def run_cli(self, *argv):
        return subprocess.run([str(BINARY), *argv], capture_output=True, text=True, timeout=120)

    def report(self, *argv):
        p = self.run_cli(*argv)
        self.assertEqual(p.returncode, 0, p.stderr)
        r = json.loads(p.stdout)
        self.assertEqual(r['schema_version'], 1)
        self.assertEqual(r['sample_rate'], 44100)
        self.assertEqual(r['frames'], 4096)
        self.assertEqual(len(r['results']), 96)
        keys = set()
        for row in r['results']:
            keys.add((row['profile'], row['base_bin'], row['carrier_multi'], row['modulator_multi'], row['modulator_tl']))
            for field in ('reference_rms', 'native_rms', 'native_error_rms', 'full_equation_error_rms'):
                self.assertTrue(math.isfinite(row[field]) and row[field] >= 0)
            self.assertLess(row['full_equation_error_rms'], 3e-6)
        self.assertEqual(len(keys), 96)
        return r

    def test_math_self_test(self):
        p = self.run_cli('--self-test')
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn('PASS:', p.stdout)

    def test_default_report(self):
        r = self.report()
        for row in r['results']:
            self.assertNotIn('offline_oversampling', row)
        low = [x for x in r['results'] if x['base_bin'] == 1]
        self.assertTrue(all(x['native_error_rms'] < 3e-6 for x in low))
        self.assertGreater(max(x['native_error_rms'] for x in r['results']), .1)

    def test_offline_experiments(self):
        r = self.report('--oversampling')
        measured = [x for x in r['results'] if 'offline_oversampling' in x]
        self.assertEqual(len(measured), 6)
        for row in measured:
            self.assertEqual([x['factor'] for x in row['offline_oversampling']], [2, 4, 8, 16, 32])
            for x in row['offline_oversampling']:
                self.assertTrue(math.isfinite(x['error_rms']) and x['error_rms'] >= 0)
                self.assertTrue(math.isfinite(x['rms']) and x['rms'] >= 0)
            self.assertLess(row['offline_oversampling'][-1]['error_rms'], 1e-6)
        self.assertTrue(any(x['offline_oversampling'][2]['error_rms'] > .1 for x in measured))

    def test_arguments(self):
        self.assertEqual(self.run_cli('--help').returncode, 0)
        for argv in (('--unknown',), ('--self-test', 'extra'), ('--oversampling', 'extra')):
            with self.subTest(argv=argv):
                p = self.run_cli(*argv)
                self.assertEqual(p.returncode, 2)
                self.assertEqual(p.stdout, '')


if __name__ == '__main__':
    unittest.main()
