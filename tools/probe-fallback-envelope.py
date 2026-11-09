#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Characterize authored PCM controls across actual WT and FM fallback routes.

Original synthetic scores only. Records current behavior; does not prescribe
fallback gains or assert that a missing vendor ROM has been reproduced.
"""
import argparse
import binascii
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

SPEC = importlib.util.spec_from_file_location(
    'balance_profile', Path(__file__).with_name('profile-fallback-balance.py'))
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)
PROBES = PROFILE.PROBES
VARIANTS = {'reference': {}, 'tl28': {19: 28 << 2}, 'attack0': {18: 0},
            'decaying': {17: 0x9f, 18: 0xf8, 16: 0x80}, 'release1': {17: 0x10}}


def encode(fmt, source, changes):
    rom = source == 'rom-strings'
    voice = bytearray(PROBES.wt_voice(26 if rom else 1, rom=rom))
    voice[19] = 0  # reference TL=0; all other parameters shared
    for index, value in changes.items():
        voice[index] = value
    setup = bytes(voice) + (PROBES.original_wave() if source == 'bound-ram' else b'')
    cc, on = (0x30, 0x10) if fmt == 3 else (0xb0, 0x90)
    sequence = b''.join(bytes((0, cc, key, value)) for key, value in
                        ((0, 124), (32, 0), (7, 127), (11, 127), (10, 64)))
    sequence += bytes((25, on, 60, 100, 50, 75, 0xff, 0x2f, 0))
    track = bytes((fmt, 0, 0x10, 0x10, 0x81)) + bytes(31 if fmt == 3 else 15)
    track += PROBES.chunk(b'Mtsu', setup) + PROBES.chunk(b'Mtsq', sequence)
    payload = PROBES.chunk(b'MTR\0', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


def run(renderer, audit, directory):
    renderer, audit = renderer.resolve(strict=True), audit.resolve(strict=True)
    directory.mkdir(parents=True, exist_ok=False)
    manifest = {'status': 'generated', 'scope': 'Original probe; no handset or song match.',
                'renderer_sha256': PROFILE.digest(renderer), 'audit_sha256': PROFILE.digest(audit),
                'cases': []}
    report = directory / 'manifest.json'
    report.write_text(json.dumps(manifest, indent=2) + '\n')
    for fmt in (2, 3):
        for source in ('rom-strings', 'missing-ram', 'bound-ram'):
            reference = None
            for variant, changes in VARIANTS.items():
                name = f'f{fmt}-{source}-{variant}'
                mmf, wav = directory / (name + '.mmf'), directory / (name + '.wav')
                mmf.write_bytes(encode(fmt, source, changes))
                rendered = subprocess.run([str(renderer), str(mmf), str(wav), '4'],
                                          capture_output=True, timeout=60)
                if rendered.returncode not in (0, 3):
                    raise RuntimeError('probe renderer failed')
                metrics = PROFILE.analyze(wav)
                if metrics['silent'] != (rendered.returncode == 3):
                    raise ValueError('renderer silence status disagrees with PCM')
                sha = hashlib.sha256(wav.read_bytes()).hexdigest()
                if reference is None:
                    reference = sha
                manifest['cases'].append(dict(format=fmt, source=source, variant=variant,
                    file=mmf.name, input_sha256=PROFILE.digest(mmf), wav_sha256=sha,
                    same_as_reference=sha == reference, **metrics))
    subprocess.run([str(audit), '--details', str(directory), str(directory / 'routes.tsv')],
                   check=True, capture_output=True, timeout=60)
    with (directory / 'routes.tsv').open(newline='') as stream:
        rows = {r['file']: r for r in csv.DictReader(stream, delimiter='\t') if r['kind'] == 'route'}
    for case in manifest['cases']:
        row = rows[case['file']]
        expected = {'bound-ram': 'bound_ram', 'missing-ram': 'missing_ram_wave',
                    'rom-strings': 'explicit_rom_wave'}[case['source']]
        if row['reason'] != expected or row['count'] != '1' or row['complete'] != '1':
            raise ValueError('probe did not follow its declared source route')
        case['reason'] = row['reason']
        case['pcm_envelope_applied'] = int(row['pcm_envelope_applied'])
    manifest['status'] = 'complete'
    report.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Completed {len(manifest["cases"])} original envelope probes: {report}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', required=True, type=Path)
    parser.add_argument('--audit', required=True, type=Path)
    parser.add_argument('directory', type=Path, help='new output directory')
    args = parser.parse_args()
    run(args.renderer, args.audit, args.directory)
