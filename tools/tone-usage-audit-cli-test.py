#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate tone dependency reports against original wire-format SMAF fixtures."""
import argparse
import binascii
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'tone_probes', ROOT / 'tools/generate-tone-dependency-probes.py')
PROBES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBES)
BINARY = None


def original_score(sequence):
    track = bytes((2, 0, 0x10, 0x10, 0x81)) + bytes(15)
    track += PROBES.chunk(b'Mtsq', sequence)
    payload = PROBES.chunk(b'MTR\0', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


class ToneUsageCliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tone-usage-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = self.root / 'inputs'
        self.manifest = PROBES.generate(self.inputs)

    def run_cli(self, source=None, report=None, details=True):
        return subprocess.run([str(BINARY), *(['--details'] if details else []),
                               str(source or self.inputs), str(report or self.root / 'report.tsv')],
                              capture_output=True, text=True, timeout=30)

    def read_report(self):
        with (self.root / 'report.tsv').open(newline='', encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream, delimiter='\t'))
        self.assertTrue(all(None not in row and None not in row.values() for row in rows))
        return rows

    def test_original_wire_fixtures_match_declared_dependencies(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = self.read_report()
        summaries = {row['file']: row for row in rows if row['kind'] == 'summary'}
        routes = {row['file']: row for row in rows if row['kind'] == 'route'}
        self.assertEqual(len(rows), 2 * len(self.manifest['cases']))
        for case in self.manifest['cases']:
            with self.subTest(case=case['name']):
                summary, route = summaries[case['file']], routes[case['file']]
                for key in ('initialized', 'complete', 'count'):
                    self.assertEqual(summary[key], '1')
                for key in ('after_bound', 'detail_overflow'):
                    self.assertEqual(summary[key], '0')
                self.assertEqual(route['reason'], case['expected']['reason'])
                self.assertEqual(route['substitute'], case['expected']['substitute'] or '-')
                for column, value in (('channel', 0), ('source_track', 0), ('bank_msb', case['bank']),
                                      ('bank_lsb', 0), ('program', case['program']), ('note', case['note']),
                                      ('wave_id', case.get('wave_id', -1)), ('count', 1), ('modern', 1)):
                    self.assertEqual(int(route[column]), value)
                self.assertEqual(route['definition'], '-1' if case['kind'] == 'none' else '0')
                self.assertEqual(float(route['first_seconds']), 0.25)
                self.assertEqual(float(route['last_seconds']), 0.25)
                for column in ('requested_al', 'requested_fixed', 'requested_dt', 'requested_sram'):
                    self.assertEqual(route[column], '0')
                self.assertEqual(hashlib.sha256((self.inputs / case['file']).read_bytes()).hexdigest(),
                                 case['sha256'])

    def test_default_aggregate_contract_is_preserved(self):
        result = self.run_cli(details=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = self.read_report()
        self.assertEqual(len(rows), len(self.manifest['cases']))
        for row in rows:
            self.assertEqual(len(row), 28)
            self.assertNotIn('kind', row)
            self.assertEqual(row['initialized'], '1')
            self.assertEqual(row['complete'], '1')
            self.assertEqual(row['notes'], '1')
        self.assertEqual(sum(int(row['fm']) for row in rows), 13)
        self.assertEqual(sum(int(row['wt']) for row in rows), 2)
        self.assertEqual(sum(int(row['fallback']) for row in rows), 12)

    def test_bound_wave_provenance_for_original_fixtures(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = {r['file']: r for r in self.read_report() if r['kind'] == 'route'}
        for case in self.manifest['cases']:
            with self.subTest(case=case['name']):
                expected = {'ram': 'instrument_ram', 'missing_ram': 'missing_ram',
                            'rom': 'rom_address'}.get(case['kind'], '-')
                route = rows[case['file']]
                self.assertEqual(route['wave_binding'], expected)
                self.assertEqual(route['wave_source_track'], '-1')
                self.assertEqual(route['legacy_mwa_tracks'], '0')

    def test_legacy_mwa_provenance_separates_note_and_wave_sources(self):
        for reverse in (False, True):
            for second in (False, True):
                for ram in (False, True):
                    with self.subTest(reverse=reverse, second=second, ram=ram):
                        def track(number, setup, sequence, wave_id, level):
                            body = bytes((2, 0, 0x10, 0x10)) + bytes(16)
                            if setup:
                                body += PROBES.chunk(b'Mtsu', setup)
                            body += PROBES.chunk(b'Mtsq', sequence)
                            wave = PROBES.chunk(b'Mwa' + bytes((wave_id,)),
                                                bytes((0x11, 0x1f, 0x40)) + bytes((level,)) * 80)
                            body += PROBES.chunk(b'Mtsp', wave)
                            return PROBES.chunk(b'MTR' + bytes((number,)), body)
                        setup = PROBES.wt_voice(1, bank=0)
                        if ram:
                            setup += PROBES.original_wave()
                        tracks = [track(0, setup, bytes((0, 0x90, 69, 100, 10, 30, 0xff, 0x2f, 0)), 1, 150),
                                  track(1, b'', bytes((0, 0xff, 0x2f, 0)), 1 if second else 2, 170)]
                        payload = b''.join(reversed(tracks) if reverse else tracks)
                        data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
                        data += struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)
                        source = self.root / 'legacy.mmf'
                        source.write_bytes(data)
                        result = self.run_cli(source)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        rows = [r for r in self.read_report() if r['kind'] == 'route']
                        self.assertEqual(len(rows), 1)
                        route = rows[0]
                        self.assertEqual(route['reason'], 'bound_ram')
                        self.assertEqual(route['source_track'], '0')
                        self.assertEqual(route['wave_binding'], 'instrument_ram' if ram else 'legacy_mwa')
                        self.assertEqual(int(route['wave_source_track']), -1 if ram else 1 if second and not reverse else 0)
                        self.assertEqual(int(route['legacy_mwa_tracks']), 0 if ram else 2 if second else 1)

    def test_control_only_score_still_has_explicit_summary(self):
        # A complete original score with a Program event and EOS, but no notes.
        source = self.root / 'controls-only.mmf'
        source.write_bytes(original_score(bytes.fromhex('00 c0 00 1e ff 2f 00')))
        result = self.run_cli(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = self.read_report()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['kind'], 'summary')
        self.assertEqual(rows[0]['count'], '0')
        self.assertEqual(rows[0]['initialized'], '1')
        self.assertEqual(rows[0]['complete'], '1')

    def test_detail_capacity_reports_exclusions_and_keeps_existing_groups(self):
        # 4224 distinct program/note pairs, then repeat the first at100ms.
        # Group storage is bounded; an existing route must still be updated.
        sequence = bytearray()
        for program in range(33):
            sequence += bytes((0, 0xc0, program))
            for note in range(128):
                sequence += bytes((0, 0x90, note, 100, 1))
        sequence += bytes.fromhex('0a c0 00 00 90 00 64 01 14 ff 2f 00')
        source = self.root / 'capacity.mmf'
        source.write_bytes(original_score(sequence))
        result = self.run_cli(source)
        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertIn('incomplete', result.stderr)
        rows = self.read_report()
        summary = [row for row in rows if row['kind'] == 'summary']
        routes = [row for row in rows if row['kind'] == 'route']
        self.assertEqual(len(routes), 4096)
        self.assertEqual(len(summary), 1)
        self.assertEqual(int(summary[0]['count']), 4225)
        self.assertEqual(int(summary[0]['detail_overflow']), 128)
        self.assertEqual(sum(int(row['count']) for row in routes), 4097)
        first = next(row for row in routes if row['program'] == '0' and row['note'] == '0')
        self.assertEqual(first['count'], '2')
        self.assertEqual(float(first['first_seconds']), 0)
        self.assertEqual(float(first['last_seconds']), 0.1)

    @unittest.skipUnless(Path('/dev/full').exists(), 'requires /dev/full')
    def test_output_failure_is_not_success(self):
        for details in (False, True):
            with self.subTest(details=details):
                result = self.run_cli(report=Path('/dev/full'), details=details)
                self.assertEqual(result.returncode, 5, result.stderr)

    def test_both_modes_refuse_input_aliases(self):
        source = self.inputs / self.manifest['cases'][0]['file']
        original = source.read_bytes()
        hard, symbolic = self.root / 'hard.tsv', self.root / 'symbolic.tsv'
        hard.hardlink_to(source)
        symbolic.symlink_to(source)
        for details in (False, True):
            for root in (source, self.inputs):
                for alias in (source, hard, symbolic):
                    with self.subTest(details=details, root=root.name, alias=alias.name):
                        result = self.run_cli(root, alias, details)
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertIn('refusing overwrite', result.stderr)
                        self.assertEqual(source.read_bytes(), original)

    def test_mobile_recorded_source_belongs_to_originating_mtr(self):
        sequence = bytes((0, 0xb0, 0, 125, 0, 0xb0, 32, 0,
                          0, 0x90, 0, 127, 5, 20, 0xff, 0x2f, 0))
        wave = PROBES.chunk(b'Mtsp', PROBES.chunk(
            b'Mwa\x01', bytes((0x11, 0x1f, 0x40)) + bytes((160,)) * 800))
        for own_present in (False, True):
            for decoy_present in (False, True):
                with self.subTest(own=own_present, decoy=decoy_present):
                    mobile = bytes((2, 0, 0x10, 0x10)) + bytes(16)
                    mobile += PROBES.chunk(b'Mtsq', sequence)
                    if own_present:
                        mobile += wave
                    ma7 = bytes((3, 0, 0x10, 0x10)) + bytes(32)
                    ma7 += PROBES.chunk(b'Mtsq', bytes((0, 0xff, 0x2f, 0)))
                    if decoy_present:
                        ma7 += wave
                    payload = PROBES.chunk(b'MTR\0', mobile) + PROBES.chunk(b'MTR\x01', ma7)
                    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
                    data += struct.pack('>H', binascii.crc_hqx(data, 65535) ^ 65535)
                    source = self.root / 'mixed-source.mmf'
                    source.write_bytes(data)
                    result = self.run_cli(source=source)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    rows = self.read_report()
                    routes = [row for row in rows if row['kind'] == 'route']
                    self.assertEqual(len(routes), 1)
                    self.assertEqual(routes[0]['source_track'], '0')
                    self.assertEqual(routes[0]['wave_id'], '1')
                    self.assertEqual(routes[0]['reason'],
                                     'bound_recorded' if own_present else 'missing_recorded_wave')

    def test_empty_or_missing_input_preserves_existing_report(self):
        empty = self.root / 'empty'
        empty.mkdir()
        report = self.root / 'report.tsv'
        for details in (False, True):
            for source in (empty, self.root / 'missing'):
                with self.subTest(details=details, source=source.name):
                    report.write_bytes(b'Previous report\n')
                    self.assertNotEqual(self.run_cli(source, report, details).returncode, 0)
                    self.assertEqual(report.read_bytes(), b'Previous report\n')

    def test_generator_is_deterministic_and_refuses_overwrite(self):
        second = self.root / 'second'
        PROBES.generate(second)
        self.assertEqual((second / 'manifest.json').read_bytes(), (self.inputs / 'manifest.json').read_bytes())
        for case in self.manifest['cases']:
            self.assertEqual((second / case['file']).read_bytes(), (self.inputs / case['file']).read_bytes())
        with self.assertRaises(FileExistsError):
            PROBES.generate(self.inputs)
        self.assertEqual(json.loads((self.inputs / 'manifest.json').read_text()), self.manifest)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve()
    unittest.main(argv=[__file__, *remaining])
