#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original FM scores that cross Nyquist through intrinsic vibrato."""
import argparse
import binascii
from dataclasses import asdict, replace
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

SPEC = importlib.util.spec_from_file_location('vibrato_fm_probes',
    Path(__file__).with_name('generate-fm-probes.py'))
FM = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = FM
SPEC.loader.exec_module(FM)


def encode(probe, fmt, cc1):
    cc, on = (0x30, 0x10) if fmt == 3 else (0xb0, 0x90)
    sequence = b''.join(bytes((0, cc, key, value)) for key, value in
        ((7, 100), (11, 127), (10, 64), (1, cc1)))
    sequence += bytes((0, on, probe.note, 100, 100, 110, 0xff, 0x2f, 0))
    track = bytes((fmt, 0, 0x10, 0x10, 0x81)) + bytes(31 if fmt == 3 else 15)
    track += FM.chunk(b'Mtsu', FM.voice(probe)) + FM.chunk(b'Mtsq', sequence)
    payload = FM.chunk(b'CNTI', bytes(5) + b'ST:Original vibrato boundary probe,')
    payload += FM.chunk(b'MTR\0', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    cases = []
    masks = (2, 3, 15, 8, 8, 10, 9, 13)
    off = FM.Operator(ar=0, tl=63)
    carrier = FM.Operator(multi=2, evb=True, dvb=3, rr=15)

    def write(fmt, algorithm, note, cc1, ops, changed, label='carrier'):
        name = f'vibrato-nyquist-f{fmt}-a{algorithm}-n{note}-cc{cc1}-{label}'
        probe = FM.Probe(name, 'Intrinsic vibrato around output Nyquist.', algorithm,
                         ops, note=note, lfo=3)
        data = encode(probe, fmt, cc1)
        filename = name + '.mmf'
        (directory / filename).write_bytes(data)
        cases.append(dict(file=filename, format=fmt, cc1=cc1, probe=asdict(probe),
                          expected_changed=changed, render_cap_seconds=4,
                          sha256=hashlib.sha256(data).hexdigest()))

    for algorithm, mask in enumerate(masks):
        count = 2 if algorithm < 2 else 4
        ops = tuple(carrier if mask & (1 << i) else off for i in range(count))
        for fmt in (2, 3):
            for note in (69, 124, 125, 127):
                write(fmt, algorithm, note, 127, ops, note == 125 or fmt == 3 and note >= 124)
    for note in (124, 125):
        write(3, 0, note, 0, (off, carrier), False, 'cc-off')
        write(3, 0, note, 127, (off, replace(carrier, evb=False)), False, 'evb-off')
        # A live modulator crosses while its sine carrier stays below Nyquist.
        write(3, 0, note, 127, (replace(carrier, tl=24), FM.Operator(rr=15)), True, 'modulator')
    (directory / 'manifest.json').write_text(json.dumps({'cases': cases}, indent=2) + '\n')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new output directory')
    args = parser.parse_args()
    print(f'Generated {len(generate(args.directory))} original vibrato Nyquist scores')
