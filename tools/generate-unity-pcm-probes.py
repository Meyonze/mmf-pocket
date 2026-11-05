#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original near-native PCM and bend-return fixtures; no recordings or ROM."""
import argparse
import binascii
import hashlib
import json
import math
from pathlib import Path
import struct


def chunk(tag, data):
    return tag + struct.pack('>I', len(data)) + data


def vlq(value):
    digits = [value & 127]
    while value > 127:
        value >>= 7
        digits.insert(0, (value & 127) | 128)
    return bytes(digits)


def exclusive(data):
    return b'\xf0' + vlq(len(data) + 1) + data + b'\xf7'


def encode(rate, frequency, bend=False, note=60, length=4096, recorded=False, fmt=3):
    wave = bytes(round(128 + 64 * math.sin(2 * math.pi * frequency * i)) for i in range(length))
    cc, on, pitch = (0x30, 0x10, 0x60) if fmt == 3 else (0xb0, 0x90, 0xe0)
    events = [(0, bytes([cc, 7, 127])), (0, bytes([cc, 11, 127]))]
    if recorded:
        events += [(0, bytes([cc, 0, 125])), (0, bytes([cc, 32, 0]))]
        note = 0
    if bend:
        events += [(0, bytes([pitch, 0, 96]))]  # +1 semitone at the default +/-2 range
    events += [(0, bytes([on, note, 127]) + vlq(20))]
    if bend:
        events += [(2, bytes([pitch, 0, 64]))]  # native rate after20ms, fractional cursor retained
    events += [(100, b'\xff\x2f\x00')]
    sequence = bytearray()
    previous = 0
    for at, payload in events:
        sequence += vlq(at - previous) + payload
        previous = at
    track = bytes([fmt, 0, 16, 16]) + bytes(32 if fmt == 3 else 16)
    if recorded:
        waves = chunk(b'Mtsp', chunk(b'Mwa\x01', b'\x11' + struct.pack('>H', rate) + wave))
        track += chunk(b'Mtsq', sequence) + waves
    else:
        voice = bytearray.fromhex('43 79 08 7f 21 00 00 00 00 00 01 '
                                 'ac 44 00 00 00 a0 f0 00 00 00 00 00 00 00 10 00 01')
        voice[11:13] = struct.pack('>H', rate)
        voice[25:27] = struct.pack('>H', length)
        setup = exclusive(voice) + exclusive(bytes.fromhex('43 79 08 7f 23 01 02') + wave)
        track += chunk(b'Mtsu', setup) + chunk(b'Mtsq', sequence)
    payload = chunk(b'CNTI', bytes(5) + b'ST:Original native-rate PCM quality,') + chunk(b'MTR\0', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    cases = []

    def write(rate, frequency=0.25, bend=False, note=60, length=4096, recorded=False, fmt=3):
        name = f'unity-{("audio" if recorded else "wt")}-fmt{fmt}-fs{rate}-f{frequency}-bend{int(bend)}-note{note}-len{length}.mmf'
        data = encode(rate, frequency, bend, note, length, recorded, fmt)
        advances = [rate / 44100 * (2 ** ((note - 60) / 12) if not recorded else 1)]
        if bend:
            advances.append(advances[0] * 2 ** (1 / 12))
        changed = any(0.8 < advance < 1.1 and advance != 1 for advance in advances)
        changed = changed or (bend and advances[0] == 1)
        (directory / name).write_bytes(data)
        cases.append(dict(file=name, rate=rate, frequency=frequency, bend=bend, note=note,
                          length=length, recorded=recorded, format=fmt, expected_changed=changed,
                          render_cap_seconds=3, sha256=hashlib.sha256(data).hexdigest()))

    for rate in (8000, 32000, 35280, 36000, 40000, 44100, 48000):
        for frequency in (0.25, 0.4):
            for bend in (False, True):
                write(rate, frequency, bend)
    for rate in (32000, 48000):
        write(rate, note=72)
    for fmt in (2, 3):
        for rate in (32000, 35280, 36000, 40000, 44100, 48000):
            write(rate, recorded=True, fmt=fmt)
    for length in (8, 32):
        write(44100, bend=True, length=length)
    (directory / 'manifest.json').write_text(json.dumps({'cases': cases}, indent=2) + '\n')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new output directory')
    args = parser.parse_args()
    print(f'Generated {len(generate(args.directory))} original near-native PCM scores')
