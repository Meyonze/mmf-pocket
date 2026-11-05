#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Analytical signal, fixture and failure tests; fake renders do not test synthesis."""
from array import array
import argparse
import binascii
import csv
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

SPEC = importlib.util.spec_from_file_location(
    'profile_balance', Path(__file__).with_name('profile-fallback-balance.py'))
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)
AUDIT_BINARY = None


class MetricsTest(unittest.TestCase):
    def test_stereo_phase_never_cancels_power(self):
        for data in ([100, 100, -100, -100], [100, -100, -100, 100]):
            report = PROFILE.window_metrics(array('h', data), 0, 2)
            self.assertEqual(report['rms_pcm'], 100)
            self.assertEqual(report['ac_rms_pcm'], 100)
            self.assertEqual(report['dc_left_pcm'], 0)

    def test_dc_is_not_reported_as_ac(self):
        report = PROFILE.window_metrics(array('h', [300, -400] * 4), 0, 4)
        self.assertEqual(report['rms_pcm'], math.sqrt(125000))
        self.assertEqual(report['ac_rms_pcm'], 0)
        self.assertEqual((report['dc_left_pcm'], report['dc_right_pcm']), (300, -400))

    def test_fixed_windows_include_silence_after_eof(self):
        report = PROFILE.window_metrics(array('h', [200, 200]), 0, 4)
        self.assertEqual(report['rms_pcm'], 100)
        self.assertEqual(report['dc_left_pcm'], 50)
        self.assertAlmostEqual(report['ac_rms_pcm'], math.sqrt(7500))
        self.assertEqual(PROFILE.window_metrics(array('h', [200, 200]), 2, 5)['rms_pcm'], 0)

    def test_signed_limit_and_interval_validation(self):
        self.assertEqual(PROFILE.window_metrics(array('h', [-32768, 32767]), 0, 1)['peak_pcm'], 32768)
        for start, end, data in ((-1, 2, [0, 0]), (2, 2, [0, 0]), (0, 1, [0])):
            with self.assertRaises(ValueError):
                PROFILE.window_metrics(array('h', data), start, end)

    def test_analysis_rejects_wrong_format_and_truncation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'probe.wav'
            for channels, rate in ((1, 44100), (2, 8000)):
                with wave.open(str(path), 'wb') as f:
                    f.setparams((channels, 2, rate, 0, 'NONE', 'not compressed'))
                    f.writeframes(bytes(8))
                with self.assertRaises(ValueError):
                    PROFILE.analyze(path)
            with wave.open(str(path), 'wb') as f:
                f.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
                f.writeframes(bytes(16))
            path.write_bytes(path.read_bytes()[:-4])
            with self.assertRaisesRegex(ValueError, 'truncated'):
                PROFILE.analyze(path)


class FixturesTest(unittest.TestCase):
    def test_native_routes_and_note_times(self):
        if AUDIT_BINARY is None:
            self.skipTest('pass --audit-binary to check all280 scores with the native engine')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inputs = root / 'inputs'
            inputs.mkdir()
            for row in PROFILE.cases():
                (inputs / (row['name'] + '.mmf')).write_bytes(PROFILE.encode(row))
            output = root / 'routes.tsv'
            result = subprocess.run([str(AUDIT_BINARY), '--details', str(inputs), str(output)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            with output.open() as f:
                rows = list(csv.DictReader(f, delimiter='\t'))
            groups = {r['file']: r for r in rows if r['kind'] == 'route'}
            summaries = [r for r in rows if r['kind'] == 'summary']
            self.assertEqual((len(groups), len(summaries)), (280, 280))
            for r in summaries:
                self.assertEqual((r['initialized'], r['complete'], r['after_bound'], r['count']),
                                 ('1', '1', '0', '1'))
            for case in PROFILE.cases():
                r = groups[case['name'] + '.mmf']
                self.assertEqual((r['reason'], r['substitute']),
                                 (case['expected_reason'], case['expected_substitute'] or '-'))
                self.assertEqual((r['count'], r['first_seconds'], r['last_seconds']), ('1', '0.25', '0.25'))
                if case['kind'] in ('rom', 'pcm-control'):
                    self.assertEqual(int(r['wave_id']), case['number'])

    def test_complete_matrix_has_unique_deterministic_valid_crc_scores(self):
        rows = PROFILE.cases()
        self.assertEqual(len(rows), 280)
        self.assertEqual(len({r['name'] for r in rows}), 280)
        self.assertEqual(len(PROFILE.cases(True)), 18)
        for row in rows:
            data = PROFILE.encode(row)
            self.assertEqual(data, PROFILE.encode(row))
            self.assertEqual(data[:4], b'MMMD')
            self.assertEqual(struct.unpack('>I', data[4:8])[0], len(data) - 8)
            self.assertEqual(struct.unpack('>H', data[-2:])[0], binascii.crc_hqx(data[:-2], 0xffff) ^ 0xffff)
            seq_at = data.index(b'Mtsq') + 8
            on = 0x10 if row['format'] == 3 else 0x90
            self.assertTrue(data[seq_at:len(data)-2].endswith(bytes(
                (64, 25, on, row['note'], 100, 50, 75, 0xff, 0x2f, 0))))


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.renderer = self.root / 'renderer'

    def fake(self, behavior='', exit_code=3):
        self.renderer.write_text('''#!/usr/bin/env python3
import pathlib, sys, wave
''' + behavior + '''
with wave.open(sys.argv[2], 'wb') as f:
    f.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    f.writeframes(bytes(16))
sys.exit(''' + str(exit_code) + ''')
''', encoding='utf-8')
        self.renderer.chmod(0o755)

    def test_valid_silent_renders_and_no_overwrite(self):
        self.fake()
        target = self.root / 'results'
        rows = PROFILE.run(self.renderer, target, quick=True)
        self.assertEqual(len(rows), 18)
        self.assertTrue(all(r['silent'] and r['whole_rms_pcm'] == 0 for r in rows))
        manifest = target / 'manifest.json'
        before = manifest.read_bytes()
        self.assertEqual(json.loads(before)['status'], 'complete')
        with self.assertRaises(FileExistsError):
            PROFILE.run(self.renderer, target, quick=True)
        self.assertEqual(before, manifest.read_bytes())

    def test_failure_and_modified_inputs_never_mark_complete(self):
        for index, behavior in enumerate(('sys.exit(1)\n',
                'pathlib.Path(sys.argv[1]).write_bytes(b"changed")\n',
                'sys.exit(0)\n')):
            self.fake(behavior)
            target = self.root / str(index)
            with self.assertRaises((ValueError, FileNotFoundError)):
                PROFILE.run(self.renderer, target, quick=True)
            self.assertEqual(json.loads((target / 'manifest.json').read_text())['status'], 'generated')

    def test_missing_renderer_never_creates_directory(self):
        target = self.root / 'results'
        with self.assertRaises(FileNotFoundError):
            PROFILE.run(self.renderer, target, quick=True)
        self.assertFalse(target.exists())

    def test_silence_status_must_agree_with_samples(self):
        self.fake(exit_code=0)
        with self.assertRaisesRegex(ValueError, 'silence status'):
            PROFILE.run(self.renderer, self.root / 'disagreement', quick=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--audit-binary', type=Path)
    args, remaining = parser.parse_known_args()
    AUDIT_BINARY = args.audit_binary.resolve() if args.audit_binary else None
    unittest.main(argv=[sys.argv[0], *remaining])
