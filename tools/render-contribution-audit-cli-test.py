#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual MMF channel diagnostics, bounds, metadata exclusion and input protection."""
import argparse
import binascii
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
args, rest = parser.parse_known_args()
BINARY = args.binary.resolve()
sys.argv = [sys.argv[0], *rest]
spec = importlib.util.spec_from_file_location('contribution_load', Path(__file__).with_name('generate-fm-load-probes.py'))
probes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probes)


class ContributionCliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='mmf-contribution-')
        cls.root = Path(cls.temp.name)
        cls.source = cls.root / 'private-source-filename.mmf'
        original = probes.encode(8)
        payload = original[8:-2]
        n = int.from_bytes(payload[4:8], 'big')
        meta = bytes(5) + b'ST:PRIVATE_TITLE_0912,SW:PRIVATE_AUTHOR_0391,'
        body = b'CNTI' + struct.pack('>I', len(meta)) + meta + payload[8+n:]
        data = b'MMMD' + struct.pack('>I', len(body)+2) + body
        cls.input = data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)
        cls.source.write_bytes(cls.input)
        p = cls.run_cli('4')
        if p.returncode:
            raise AssertionError(p.stderr)
        cls.text = p.stdout
        cls.report = json.loads(p.stdout)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def run_cli(cls, seconds='4', source=None, extra=()):
        return subprocess.run([str(BINARY), str(source or cls.source), seconds, *extra],
                              capture_output=True, text=True, timeout=180)

    def test_complete_channel_report(self):
        r = self.report
        self.assertEqual((r['schema_version'], r['sample_rate'], r['frames']), (1, 44100, 132300))
        self.assertTrue(r['finished'] and r['score_scan_complete'] and r['reference_pcm_exact'])
        self.assertFalse(r['cap_reached'])
        self.assertEqual((r['sine_factor'], r['sine_math'], r['latency_frames']), (0, 'exact', 0))
        self.assertLess(r['component_sum_error_peak'], 2e-5)
        self.assertEqual(r['totals']['fm_notes'], 8)
        self.assertEqual(len(r['parts']), 8)
        self.assertEqual({(p['domain'], p['channel'], p['atr_track']) for p in r['parts']},
                         {('score', c, -1) for c in range(8)})
        for part in r['parts']:
            self.assertEqual(part['authored_notes'], 1)
            self.assertEqual(part['trigger_diagnostics']['fm_notes'], 1)
            self.assertEqual(part['trigger_diagnostics']['fallback_notes'], 0)
            self.assertEqual(part['peak_native_sources'], 1)
            self.assertGreater(part['voice_frames']['fm'], 0)
            self.assertEqual(sum(part['voice_frames'][k] for k in ('pcm', 'audio', 'atr')), 0)
            for stage in ('before_output_filter', 'after_output_filter', 'with_shared_output_gain'):
                self.assertGreater(part[stage]['rms'], 0)
                self.assertTrue(all(math.isfinite(v) for v in part[stage].values()))

    def test_no_metadata_or_input_path_export_and_no_mutation(self):
        for forbidden in ('PRIVATE_TITLE_0912', 'PRIVATE_AUTHOR_0391', self.source.name,
                          '"title"', '"author"', '"vendor"', '"input_path"'):
            self.assertNotIn(forbidden, self.text)
        self.assertEqual(self.source.read_bytes(), self.input)
        self.assertIn('not isolated or perceptual loudness', self.report['scope'])
        self.assertIn('powers do not add', self.report['scope'])

    def test_cap_before_first_note_is_explicit(self):
        p = self.run_cli('0.1')
        self.assertEqual(p.returncode, 0, p.stderr)
        r = json.loads(p.stdout)
        self.assertEqual(r['frames'], 4410)
        self.assertTrue(r['cap_reached'] and r['reference_pcm_exact'])
        self.assertFalse(r['finished'])
        self.assertEqual(r['parts'], [])
        self.assertEqual(r['mix']['rms'], 0)

    def test_quality_policy_matches_actual_and_tracks_filtered_sources(self):
        for factor in (1, 8):
            p = self.run_cli(extra=(f'--experimental-sine-factor={factor}',
                                    '--experimental-sine-math=hermite'))
            self.assertEqual(p.returncode, 0, p.stderr)
            r = json.loads(p.stdout)
            self.assertEqual((r['sine_factor'], r['sine_math'], r['latency_frames']), (factor, 'hermite', 72))
            self.assertEqual(r['frames'], self.report['frames'] + 72)
            self.assertTrue(r['finished'] and r['reference_pcm_exact'])
            self.assertLess(r['component_sum_error_peak'], 2e-5)
            for a, b in zip(self.report['parts'], r['parts']):
                self.assertEqual(a['voice_frames'], b['voice_frames'])
                self.assertEqual(a['trigger_diagnostics'], b['trigger_diagnostics'])
                self.assertEqual(b['sine_source_voice_frames'], b['voice_frames']['fm'] if factor > 1 else 0)
            if factor == 1:
                self.assertEqual(r['mix']['peak'], self.report['mix']['peak'])
            else:
                self.assertLess(r['parts'][0]['with_shared_output_gain']['rms'],
                                self.report['parts'][0]['with_shared_output_gain']['rms'] / 2)
        for extra in (('--experimental-sine-factor=3',), ('--experimental-sine-factor=0',),
                      ('--experimental-sine-math=hermite',),
                      ('--experimental-sine-factor=8', '--experimental-sine-factor=8')):
            self.assertEqual(self.run_cli(extra=extra).returncode, 2)

    def test_app_quality_density_attribution_and_high_note_reference(self):
        p = self.run_cli(extra=('--app-quality',))
        self.assertEqual(p.returncode, 0, p.stderr)
        r = json.loads(p.stdout)
        self.assertTrue(r['boost_high_notes'] and r['reference_pcm_exact'] and r['finished'])
        self.assertEqual((r['sine_factor'], r['sine_math'], r['latency_frames']), (8, 'hermite', 72))
        self.assertGreater(sum(x['boosted_sine_voice_frames'] for x in r['parts']), 0)
        self.assertEqual(r['parts'][0]['boosted_sine_voice_frames'], 0)
        for old, part in zip(self.report['parts'], r['parts']):
            self.assertEqual(part['voice_frames'], old['voice_frames'])
            self.assertEqual(part['trigger_diagnostics'], old['trigger_diagnostics'])
            self.assertEqual(sum(part['sine_voice_frames_by_factor'].values()), part['sine_source_voice_frames'])
            self.assertEqual(part['boosted_sine_voice_frames'], part['sine_voice_frames_by_factor']['16'])
        self.assertEqual(self.source.read_bytes(), self.input)
        for forbidden in ('PRIVATE_TITLE_0912', 'PRIVATE_AUTHOR_0391', self.source.name):
            self.assertNotIn(forbidden, p.stdout)

        fm = probes.FM
        high = self.root / 'original-high.mmf'
        high.write_bytes(fm.encode(fm.Probe('high', 'Original high PM', 0,
                                          (fm.Operator(tl=0, multi=7), fm.Operator(tl=0, multi=5)), note=96)))
        reports = []
        for extra in (('--app-quality',), ('--experimental-sine-factor=16', '--experimental-sine-math=hermite')):
            result = self.run_cli(source=high, extra=extra)
            self.assertEqual(result.returncode, 0, result.stderr)
            reports.append(json.loads(result.stdout))
        self.assertEqual(reports[0]['mix'], reports[1]['mix'])
        self.assertEqual(reports[0]['frames'], reports[1]['frames'])
        self.assertEqual(reports[0]['parts'][0]['with_shared_output_gain'], reports[1]['parts'][0]['with_shared_output_gain'])
        self.assertEqual(reports[0]['parts'][0]['boosted_sine_voice_frames'], reports[0]['parts'][0]['voice_frames']['fm'])
        for extra in (('--app-quality', '--app-quality'),
                      ('--app-quality', '--experimental-sine-factor=8'),
                      ('--experimental-sine-factor=8', '--app-quality'),
                      ('--app-quality', '--experimental-sine-math=hermite'),
                      ('--experimental-sine-math=hermite', '--app-quality')):
            self.assertEqual(self.run_cli(extra=extra).returncode, 2)

    def test_reject_arguments_missing_malformed_and_oversized(self):
        for seconds in ('', 'nan', 'inf', '0', '-1', '0.000001', '600.01', '1junk', '1e999'):
            self.assertEqual(self.run_cli(seconds).returncode, 2)
        malformed = self.root / 'malformed.mmf'
        malformed.write_bytes(bytes(12))
        oversized = self.root / 'oversized.mmf'
        with oversized.open('wb') as f:
            f.truncate(16*1024*1024+1)
        for source in (malformed, oversized, self.root / 'missing.mmf'):
            self.assertEqual(self.run_cli(source=source).returncode, 1)
        p = subprocess.run([str(BINARY)], capture_output=True)
        self.assertEqual(p.returncode, 2)
        p = subprocess.run([str(BINARY), '--help'], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0)
        self.assertIn('no song metadata', p.stdout)
        self.assertEqual(self.source.read_bytes(), self.input)


if __name__ == '__main__':
    unittest.main()
