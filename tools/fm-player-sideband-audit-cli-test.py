#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Public-player study contract and contrasting steady alias conditions."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
args, rest = parser.parse_known_args()
BINARY = args.binary.resolve()
sys.argv = [sys.argv[0], *rest]


class PlayerSidebandTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        p = subprocess.run([str(BINARY)], capture_output=True, text=True, timeout=180)
        if p.returncode:
            raise AssertionError(p.stderr)
        cls.report = json.loads(p.stdout)

    def test_complete_original_matrix_and_reference(self):
        r = self.report
        self.assertEqual((r['schema_version'], r['sample_rate'], r['frames'], r['warmup_frames']),
                         (1, 44100, 1024, 512))
        self.assertIn('no hardware fidelity', r['scope'])
        self.assertEqual(len(r['results']), 24)
        keys = set()
        for case in r['results']:
            keys.add((case['note'], case['carrier_multi'], case['modulator_multi'], case['modulator_tl']))
            self.assertGreater(case['ideal_band_rms'], 0)
            self.assertEqual(len(case['policies']), 12)
            policies = set()
            for p in case['policies']:
                policies.add((p['factor'], p['math']))
                self.assertEqual(p['latency_frames'], 0 if p['factor'] == 0 else 72)
                for field in ('output_rms', 'full_equation_error_rms', 'ideal_band_error_rms',
                              'math_delta_rms', 'rms_change_db'):
                    self.assertTrue(math.isfinite(p[field]))
                self.assertLess(p['full_equation_error_rms'], 4e-7)
                self.assertLess(p['math_delta_rms'], 1e-7)
            self.assertEqual(policies, {(0, 'exact'), (1, 'exact')} |
                             {(f, m) for f in (2, 4, 8, 16, 32) for m in ('exact', 'hermite')})
        self.assertEqual(keys, {(n, c, m, tl) for n in (60, 72, 84, 96)
                               for c, m in ((2, 3), (5, 7)) for tl in (0, 24, 36)})

    def test_native_control_has_no_gain_or_phase_change(self):
        for case in self.report['results']:
            normal, control = case['policies'][:2]
            for field in ('output_rms', 'full_equation_error_rms', 'ideal_band_error_rms', 'rms_change_db'):
                self.assertEqual(normal[field], control[field])
            self.assertEqual(control['rms_change_db'], 0)

    def test_higher_note_changes_required_factor_and_level(self):
        def policy(note, factor):
            case = next(c for c in self.report['results'] if
                        (c['note'], c['carrier_multi'], c['modulator_multi'], c['modulator_tl']) ==
                        (note, 5, 7, 0))
            return next(p for p in case['policies'] if p['factor'] == factor and p['math'] == 'exact')
        self.assertLess(policy(84, 8)['ideal_band_error_rms'], 2e-5)
        self.assertGreater(policy(96, 8)['ideal_band_error_rms'], 1e-2)
        self.assertLess(policy(96, 16)['ideal_band_error_rms'], 2e-5)
        self.assertLess(policy(96, 16)['rms_change_db'], -10)
        # Filtering can remove substantial existing energy; no makeup gain.
        self.assertLess(policy(84, 8)['output_rms'], policy(84, 0)['output_rms'] / 2)

    def test_self_test_help_and_bad_arguments(self):
        p = subprocess.run([str(BINARY), '--self-test'], capture_output=True, text=True, timeout=120)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn('PASS:', p.stdout)
        p = subprocess.run([str(BINARY), '--help'], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0)
        self.assertIn('Usage:', p.stdout)
        for options in (['--unknown'], ['--self-test', 'extra']):
            p = subprocess.run([str(BINARY), *options], capture_output=True, text=True)
            self.assertEqual(p.returncode, 2)
            self.assertNotIn('"results"', p.stdout)


if __name__ == '__main__':
    unittest.main()
