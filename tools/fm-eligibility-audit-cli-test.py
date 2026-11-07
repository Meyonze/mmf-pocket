#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Live-voice report bounds and privacy with an original generated SMAF."""
import importlib.util
from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location('eligibility_fm', Path(__file__).with_name('generate-fm-probes.py'))
FM = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = FM
spec.loader.exec_module(FM)


class CliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'private-song-name.mmf'
        self.original = FM.encode(FM.CASES[0])
        self.path.write_bytes(self.original)

    def run_cli(self, *args):
        return subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, timeout=60)

    def report(self, cap):
        p = self.run_cli(self.path, cap)
        self.assertEqual(p.returncode, 0, p.stderr)
        r = json.loads(p.stdout)
        self.assertEqual(r['schema_version'], 1)
        self.assertTrue(r['render_matches_reference'])
        self.assertEqual(r['bessel_l1_tail_target'], 1e-5)
        self.assertEqual(sum(r['reason_voice_frames'].values()), r['active_fm_voice_frames'])
        self.assertEqual(sum(r['steady_model_factor_voice_frames'].values()), r['eligible_voice_frames'])
        self.assertEqual(sum(r['sine_graph_algorithm_voice_frames'].values()), r['sine_graph_no_feedback_voice_frames'])
        self.assertLessEqual(r['peak_eligible_channels'], r['peak_eligible_fm'])
        self.assertLessEqual(r['peak_eligible_fm'], r['peak_active_fm'])
        self.assertLessEqual(r['peak_active_fm'], 32)
        self.assertLessEqual(r['eligible_currently_muted_voice_frames'], r['eligible_voice_frames'])
        self.assertNotIn('private-song-name', p.stdout)
        self.assertNotIn('FM probe', p.stdout)
        self.assertEqual(self.path.read_bytes(), self.original)
        self.assertEqual(list(Path(self.temp.name).iterdir()), [self.path])
        return r

    def test_usage(self):
        self.assertEqual(self.run_cli('--help').returncode, 0)
        for args in [(), ('--unknown',), (self.path, 1, 2)]:
            self.assertEqual(self.run_cli(*args).returncode, 2)

    def test_cap_boundary(self):
        r = self.report('.25')
        self.assertEqual(r['frames'], 11025)
        self.assertEqual(r['fm_notes'], 0)
        self.assertEqual(r['peak_active_fm'], 0)
        self.assertTrue(r['cap_reached'])

    def test_live_report(self):
        r = self.report('15')
        self.assertGreater(r['fm_notes'], 0)
        self.assertGreater(r['eligible_voice_frames'], 0)
        self.assertTrue(r['finished'])
        self.assertTrue(r['scan_complete'])

    def test_high_modulation_is_not_native_bucket(self):
        probe = replace(FM.CASES[1], name='high-sine', note=96,
                        operators=(FM.Operator(tl=0, multi=7), FM.Operator(tl=0, multi=5)))
        self.original = FM.encode(probe)
        self.path.write_bytes(self.original)
        r = self.report('15')
        self.assertGreater(r['maximum_pm_index_upper'], 20)
        self.assertGreater(sum(v for k, v in r['steady_model_factor_voice_frames'].items() if k != '1'), 0)

    def test_polyphony_and_native_stealing(self):
        load_spec = importlib.util.spec_from_file_location('eligibility_load', Path(__file__).with_name('generate-fm-load-probes.py'))
        load = importlib.util.module_from_spec(load_spec)
        load_spec.loader.exec_module(load)
        for count in load.COUNTS:
            self.original = load.encode(count)
            self.path.write_bytes(self.original)
            r = self.report('.3')
            self.assertEqual(r['fm_notes'], count)
            self.assertEqual(r['stolen_fm'], max(0, count - 32))
            self.assertEqual(r['peak_eligible_fm'], min(count, 32))
            self.assertEqual(r['peak_eligible_channels'], min(count, 16))
            self.assertTrue(r['cap_reached'])

    def test_invalid_cap(self):
        for cap in ['nan', 'inf', '-inf', '0', '-1', '601', '0.000001', '1suffix', '']:
            p = self.run_cli(self.path, cap)
            self.assertEqual(p.returncode, 2)
            self.assertEqual(p.stdout, '')

    def test_invalid_input(self):
        for data in [b'', b'not a SMAF file']:
            self.path.write_bytes(data)
            p = self.run_cli(self.path, 1)
            self.assertEqual(p.returncode, 1)
            self.assertEqual(p.stdout, '')
        self.assertEqual(self.run_cli(self.path.with_suffix('.missing')).returncode, 1)

    def test_size_bound(self):
        with self.path.open('wb') as f:
            f.truncate(16 * 1024 * 1024 + 1)
        p = self.run_cli(self.path, '.1')
        self.assertEqual(p.returncode, 1)
        self.assertEqual(p.stdout, '')
        self.assertEqual(self.path.stat().st_size, 16 * 1024 * 1024 + 1)


if __name__ == '__main__':
    unittest.main()
