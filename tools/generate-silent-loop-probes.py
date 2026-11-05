#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original PCM reconstruction fixtures; no vendor samples or handset claims."""
import argparse
import binascii
import hashlib
import json
import math
from pathlib import Path
import struct


def chunk(tag, payload):
    return tag + struct.pack('>I', len(payload)) + payload


def vlq(value):
    digits = [value & 127]
    while value > 127:
        value >>= 7
        digits.insert(0, (value & 127) | 128)
    return bytes(digits)


def exclusive(payload):
    return b'\xf0' + vlq(len(payload) + 1) + payload + b'\xf7'


def encode(codec, note, frequency, loop_length, nonzero=False, bend=False):
    start = 4096
    samples = [round(64 * math.sin(2 * math.pi * frequency * i)) for i in range(start)]
    samples += [0] * loop_length
    if nonzero:
        samples[-1] = 1
    voice = bytearray.fromhex('43 79 08 7f 21 00 00 00 00 00 01 '
                             '1f 40 00 00 00 00 f0 00 00 00 00 00 00 00 00 00 01')
    voice[23:25] = struct.pack('>H', start)
    voice[25:27] = struct.pack('>H', len(samples))
    data = bytes((sample + (128 if codec == 2 else 0)) & 255 for sample in samples)
    setup = exclusive(voice) + exclusive(bytes.fromhex('43 79 08 7f 23 01') + bytes([codec]) + data)
    events = [(0, bytes([0x30, 7, 127])), (0, bytes([0x30, 11, 127])),
              (0, bytes([0x10, note, 127]) + vlq(80))]
    if bend:
        events += [(0, bytes([0x30, 15, 24])), (2, bytes([0x60, 127, 127])),
                   (8, bytes([0x60, 0, 64]))]
    events += [(100, bytes.fromhex('ff 2f 00'))]
    sequence = bytearray()
    previous = 0
    for at, payload in events:
        sequence += vlq(at - previous) + payload
        previous = at
    track = bytes([3, 0, 16, 16]) + bytes(32)
    track += chunk(b'Mtsu', setup) + chunk(b'Mtsq', sequence)
    payload = chunk(b'CNTI', bytes(5) + b'ST:Original silent-loop reconstruction,')
    payload += chunk(b'MTR\0', track)
    encoded = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    encoded += struct.pack('>H', binascii.crc_hqx(encoded, 0xffff) ^ 0xffff)
    assert len(encoded) - 8 == int.from_bytes(encoded[4:8], 'big')
    assert (binascii.crc_hqx(encoded[:-2], 0xffff) ^ 0xffff) == int.from_bytes(encoded[-2:], 'big')
    return encoded


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    cases = []
    settings = [(note, frequency, 1, False, False)
                for note in (48, 60, 72, 84, 96, 108) for frequency in (0.01, 0.45)]
    settings += [(84, 0.45, length, nonzero, bend)
                 for length, nonzero, bend in ((3, False, False), (31, False, False),
                                              (1, True, False), (1, False, True))]
    for note, frequency, length, nonzero, bend in settings:
        stem = f'pcm-note{note}-f{frequency}-loop{length}-nz{int(nonzero)}-bend{int(bend)}'
        for codec in (2, 3):
            name = f'{stem}-codec{codec}.mmf'
            data = encode(codec, note, frequency, length, nonzero, bend)
            (directory / name).write_bytes(data)
            cases.append(dict(file=name, pair=stem, codec=codec, note=note,
                              frequency=frequency, loop_length=length, nonzero_loop=nonzero,
                              bend=bend, expected_changed=codec == 3 and not nonzero,
                              render_cap_seconds=3, sha256=hashlib.sha256(data).hexdigest()))
    (directory / 'manifest.json').write_text(json.dumps({'cases': cases}, indent=2) + '\n')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new destination directory')
    args = parser.parse_args()
    print(f'Generated {len(generate(args.directory))} original silent-loop probes')
