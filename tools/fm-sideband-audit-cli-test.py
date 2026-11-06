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
            self.assertNotIn('causal_oversampling', row)
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

    def test_causal_experiments_and_filter_controls(self):
        r = self.report('--causal-oversampling')
        self.assertEqual(r['causal_warmup_frames'], 256)
        controls = r['causal_filter_controls']
        self.assertEqual(len(controls), 40)
        expected = {(factor, taps, last, fraction) for factor in (2, 4, 8, 16, 32)
                    for taps, last in ((31, 31), (63, 63), (127, 127), (31, 255)) for fraction in (1, .95)}
        by_key = {}
        for c in controls:
            key = (c['factor'], c['early_taps'], c['final_taps'], c['last_nyquist_fraction'])
            self.assertNotIn(key, by_key)
            by_key[key] = c
            factor, taps, last, fraction = key
            stages = factor.bit_length() - 1
            # Derive delay and work from each stage's input/output rates.
            delay = sum((((last if j == stages - 1 else taps) - 1) / 2) *
                        (2 ** j) / factor for j in range(stages))
            work = sum((factor // (2 ** (j + 1))) *
                       (((last - 1) // 2 if fraction != 1 else (last + 1) // 4)
                        if j == stages - 1 else (taps + 1) // 4)
                       for j in range(stages))
            self.assertAlmostEqual(c['group_delay_frames'], delay, delta=1e-10)
            self.assertAlmostEqual(c['group_delay_ms'], 1000 * delay / 44100, delta=1e-10)
            self.assertEqual(c['fir_multiplies_per_output'], work)
            self.assertEqual(c['support_bound_frames'], math.ceil(2 * delay))
            self.assertLess(c['step_final_error'], 1e-12)
            self.assertLess(c['step_overshoot'], .25)
            self.assertLess(c['step_undershoot'], .25)
            self.assertLess(abs(c['gain_1000hz_db']), .05)
            self.assertLess(c['gain_24000hz_db'], -3)
            self.assertTrue(all(math.isfinite(v) for v in c.values()))
        self.assertEqual(set(by_key), expected)
        for factor, taps, last, fraction in expected:
            if fraction == .95:
                self.assertLess(by_key[(factor, taps, last, .95)]['gain_21800hz_db'],
                                by_key[(factor, taps, last, 1)]['gain_21800hz_db'])
        measured = [row for row in r['results'] if 'causal_oversampling' in row]
        self.assertEqual(len(measured), 12)
        for row in measured:
            with self.subTest(profile=row['profile'], carrier=row['carrier_multi'], tl=row['modulator_tl']):
                self.assertEqual(row['base_bin'], 211)
                self.assertIn(row['modulator_tl'], (0, 24))
                self.assertNotIn('offline_oversampling', row)
                cases = row['causal_oversampling']
                self.assertEqual(len(cases), 40)
                self.assertEqual({(x['factor'], x['early_taps'], x['final_taps'], x['last_nyquist_fraction'])
                                  for x in cases}, expected)
                for x in cases:
                    self.assertEqual(x['fm_operator_ticks_per_output'], 2 * x['factor'])
                    self.assertEqual(x['stages'], x['factor'].bit_length() - 1)
                    self.assertLess(x['fir_equation_error_rms'], 3e-6)
                    self.assertTrue(all(math.isfinite(v) for v in x.values()))
                    self.assertGreaterEqual(x['aligned_band_error_rms'], 0)
        hard = next(x for x in measured if x['profile'] == 'vm35' and
                    x['carrier_multi'] == 7 and x['modulator_tl'] == 0)
        def error(factor):
            return next(x['aligned_band_error_rms'] for x in hard['causal_oversampling']
                        if x['factor'] == factor and x['early_taps'] == 127 and x['final_taps'] == 127 and
                        x['last_nyquist_fraction'] == 1)
        self.assertGreater(error(8), .1)
        self.assertLess(error(32), 1e-4)

    def test_aligned_native_clock_experiments(self):
        r = self.report('--aligned-oversampling')
        self.assertIn('Native-frame', r['source_clock'])
        self.assertIn('Streaming FIR', r['oversampling_scope'])
        self.assertEqual(r['aligned_warmup_frames'], 256)
        layouts = {2: ([257], 64, 65), 4: ([33, 257], 68, 83),
                   8: ([33, 33, 257], 70, 119), 16: ([33, 33, 33, 257], 71, 191),
                   32: ([33, 33, 33, 37, 257], 72, 337)}
        controls = r['aligned_filter_controls']
        self.assertEqual(len(controls), 5)
        measured = [x for x in r['results'] if 'aligned_oversampling' in x]
        self.assertEqual(len(measured), 12)
        groups = [controls] + [x['aligned_oversampling'] for x in measured]
        for group in groups:
            self.assertEqual([x['factor'] for x in group], list(layouts))
            for x in group:
                taps, delay, work = layouts[x['factor']]
                self.assertEqual(x['stage_taps'], taps)
                self.assertEqual(x['group_delay_frames'], delay)
                self.assertEqual(x['fir_multiplies_per_output'], work)
                self.assertEqual(x['common_delay_frames'], 72)
                self.assertEqual(x['padding_frames'], 72 - delay)
                self.assertEqual(x['pipeline_tail_bound_frames'], 72 + delay)
                self.assertTrue(all(math.isfinite(v) for k, v in x.items() if k != 'stage_taps'))
        for row in measured:
            for x in row['aligned_oversampling']:
                self.assertEqual(x['native_operator_ticks_per_output'], 2)
                self.assertLess(x['fir_equation_error_rms'], 3e-6)
        for x in controls:
            self.assertLess(x['step_final_error'], 1e-12)
            self.assertLess(abs(x['gain_1000hz_db']), .05)

    def test_arguments(self):
        self.assertEqual(self.run_cli('--help').returncode, 0)
        for argv in (('--unknown',), ('--self-test', 'extra'), ('--oversampling', 'extra'), ('--causal-oversampling', 'extra'),
                     ('--oversampling', '--causal-oversampling'), ('--aligned-oversampling', 'extra'),
                     ('--aligned-oversampling', '--causal-oversampling')):
            with self.subTest(argv=argv):
                p = self.run_cli(*argv)
                self.assertEqual(p.returncode, 2)
                self.assertEqual(p.stdout, '')


if __name__ == '__main__':
    unittest.main()
