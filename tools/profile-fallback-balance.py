#!/usr/bin/env python3
"""Measure original fallback recipes with fixed score controls and time windows."""
# SPDX-License-Identifier: Apache-2.0
import argparse
from array import array
import binascii
import csv
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import wave

SPEC = importlib.util.spec_from_file_location(
    'tone_probes', Path(__file__).with_name('generate-tone-dependency-probes.py'))
PROBES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBES)
RATE = 44100
LIMIT_SECONDS = 4
WINDOWS = {'whole': (250, 4000), 'attack': (250, 350),
           'body': (350, 750), 'release': (750, 2750)}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cases(quick=False):
    rows = []
    for fmt in (2, 3):
        def add(kind, number, note, route, substitute=None):
            rows.append(dict(name=f'f{fmt}-{kind}-{number:03d}-n{note:03d}',
                             format=fmt, kind=kind, number=number, note=note,
                             expected_reason=route, expected_substitute=substitute))
        for pc in ((0,) if quick else range(0, 128, 8)):
            for note in ((60,) if quick else (48, 60, 72)):
                add('gm', pc, note, 'no_matching_supported_voice', 'gm_modern_recipe')
        for wid in ((0, 22, 26) if quick else range(29)):
            for note in ((60,) if quick or wid < 22 else (48, 60, 72)):
                add('rom', wid, note, 'explicit_rom_wave', 'rom_recipe')
        for note in ((36, 42, 60) if quick else range(35, 82)):
            add('drum', note, note, 'no_matching_supported_voice', 'drum_recipe')
        add('fm-control', 0, 60, 'bound_fm')
        add('pcm-control', 1, 60, 'bound_ram')
    return rows


def encode(case):
    fmt, kind = case['format'], case['kind']
    bank = 124 if kind in ('rom', 'pcm-control') else 125 if kind == 'drum' else 0
    program = case['number'] if kind == 'gm' else 0
    setup = b''
    if kind == 'rom':
        # Addresses only: no vendor waveform. Fs16000 is an explicit probe
        # parameter, not measured native tuning of the proprietary ROM.
        voice = bytearray(PROBES.wt_voice(case['number'], rom=True, bank=bank))
        voice[12:14] = bytes((0x3e, 0x80))
        setup = bytes(voice)
    elif kind == 'fm-control':
        setup = PROBES.fm_voice()
    elif kind == 'pcm-control':
        setup = PROBES.wt_voice(1) + PROBES.original_wave()
    cc, pc, bend, on = (0x30, 0x40, 0x60, 0x10) if fmt == 3 else (0xb0, 0xc0, 0xe0, 0x90)
    seq = b''.join(bytes((0, cc, key, value)) for key, value in
                   ((0, bank), (32, 0), (7, 127), (11, 127), (10, 64)))
    seq += bytes((0, pc, program, 0, bend, 0, 64, 25, on, case['note'], 100, 50,
                  75, 0xff, 0x2f, 0))  # start250ms, gate500ms, EOS1000ms
    channels = bytes((0x81,)) + bytes(15) if fmt == 2 else bytes((0x81,)) + bytes(31)
    track = bytes((fmt, 0, 0x10, 0x10)) + channels
    track += PROBES.chunk(b'Mtsu', setup) + PROBES.chunk(b'Mtsq', seq)
    payload = PROBES.chunk(b'CNTI', bytes(5) + b'ST:Original balance probe,')
    payload += PROBES.chunk(b'MTR\0', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


def window_metrics(data, start, end):
    """Stereo power: never sum L/R; include zeros after natural source EOF."""
    count = end - start
    if start < 0 or count <= 0 or len(data) % 2:
        raise ValueError('invalid stereo analysis window')
    signals = [data[start * 2 + ch:end * 2:2] for ch in (0, 1)]
    means = [sum(s) / count for s in signals]
    square = sum(sum(x * x for x in s) for s in signals) / (2 * count)
    ac = max(0.0, square - sum(x * x for x in means) / 2)
    return dict(peak_pcm=max((abs(x) for s in signals for x in s), default=0),
                rms_pcm=math.sqrt(square), ac_rms_pcm=math.sqrt(ac),
                dc_left_pcm=means[0], dc_right_pcm=means[1])


def analyze(path):
    with wave.open(str(path), 'rb') as reader:
        if (reader.getnchannels(), reader.getsampwidth(), reader.getframerate(),
                reader.getcomptype()) != (2, 2, RATE, 'NONE'):
            raise ValueError('renderer must produce stereo PCM16 at 44100Hz')
        frames = reader.getnframes()
        if not 0 < frames <= RATE * LIMIT_SECONDS:
            raise ValueError('invalid or unbounded render length')
        raw = reader.readframes(frames)
        if len(raw) != frames * 4:
            raise ValueError('truncated WAV')
    # wave.readframes supplies native-endian PCM (also on big-endian hosts).
    data = array('h')
    data.frombytes(raw)
    result = dict(frames=frames, render_bound_hit=frames == RATE * LIMIT_SECONDS,
                  silent=not any(data))
    for name, (first, last) in WINDOWS.items():
        for key, value in window_metrics(data, first * RATE // 1000, last * RATE // 1000).items():
            result[f'{name}_{key}'] = value
    return result


def run(renderer, directory, quick=False):
    renderer = renderer.resolve(strict=True)
    if not renderer.is_file() or not os.access(renderer, os.X_OK):
        raise ValueError('renderer must be an executable file')
    directory.mkdir(parents=True, exist_ok=False)
    rows = cases(quick)
    manifest = dict(schema=1, status='generated', renderer_sha256=digest(renderer),
                    rate=RATE, velocity=100, volume=127, expression=127, pan=64,
                    note_on_ms=250, gate_ms=500, sequence_end_ms=1000,
                    rom_fs_hz=16000,
                    render_limit_seconds=LIMIT_SECONDS, windows_ms=WINDOWS,
                    scope='Software fallback measurements; not handset loudness calibration or LUFS.',
                    cases=[])
    for case in rows:
        source = directory / (case['name'] + '.mmf')
        with source.open('xb') as f:
            f.write(encode(case))
        manifest['cases'].append({**case, 'file': source.name, 'sha256': digest(source)})
    manifest_path = directory / 'manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    results = []
    with (directory / 'measurements.csv').open('x', newline='', encoding='utf-8') as report:
        writer = None
        for case in manifest['cases']:
            source = directory / case['file']
            target = source.with_suffix('.wav')
            process = subprocess.run([str(renderer), str(source), str(target), str(LIMIT_SECONDS)],
                                     capture_output=True, text=True, timeout=60)
            (directory / (case['name'] + '.log')).write_text(
                process.stdout + process.stderr, encoding='utf-8')
            if process.returncode not in (0, 3):
                raise ValueError(f"renderer failed for {source.name}: exit {process.returncode}")
            if digest(source) != case['sha256']:
                raise ValueError('renderer modified an input fixture')
            metrics = analyze(target)
            if metrics['silent'] != (process.returncode == 3):
                raise ValueError('renderer silence status disagrees with WAV samples')
            row = {**case, **metrics, 'wav_sha256': digest(target)}
            if writer is None:
                writer = csv.DictWriter(report, fieldnames=list(row))
                writer.writeheader()
            writer.writerow(row)
            results.append(row)
    if digest(renderer) != manifest['renderer_sha256']:
        raise ValueError('renderer changed during measurement')
    manifest['status'] = 'complete'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', type=Path, required=True, help='smaf-render executable')
    parser.add_argument('--quick', action='store_true', help='18 route/control probes instead of 280')
    parser.add_argument('directory', type=Path, help='new destination; existing paths are refused')
    args = parser.parse_args()
    try:
        results = run(args.renderer, args.directory, args.quick)
    except (OSError, ValueError, EOFError, wave.Error, subprocess.TimeoutExpired) as error:
        parser.exit(1, f'error: {error}\n')
    print(f'Measured {len(results)} original fallback/control scores in {args.directory}')


if __name__ == '__main__':
    main()
