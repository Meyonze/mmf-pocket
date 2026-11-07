#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original high-PM score fixtures for admission/load diagnostics; no music/ROM."""
import argparse
import binascii
import importlib.util
import json
from pathlib import Path
import struct
import sys

spec = importlib.util.spec_from_file_location('fm_load_base', Path(__file__).with_name('generate-fm-probes.py'))
FM = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = FM
spec.loader.exec_module(FM)
COUNTS = (1, 8, 32, 40)


def encode(count):
    if count not in COUNTS:
        raise ValueError('supported counts: 1/8/32/40')
    probe = FM.Probe('high-pm-load', 'Original high PM load, not music.', 0,
                     (FM.Operator(tl=0, multi=7), FM.Operator(tl=0, multi=5)))
    events = []
    for ch in range(min(16, count)):
        for cc, value in ((0, 0), (32, 0), (7, 100), (11, 127), (10, 64), (64, 0)):
            events.append((0, bytes((0xb0 | ch, cc, value))))
        events.append((0, bytes((0xc0 | ch, 0))))
    for i in range(count):
        events.append((25, bytes((0x90 | (i % 16), 84 + i % 12, 100)) + FM.vlq(150)))
    events.append((300, b'\xff\x2f\x00'))
    events.sort(key=lambda x: x[0])
    sequence, previous = bytearray(), 0
    for tick, event in events:
        sequence.extend(FM.vlq(tick - previous) + event)
        previous = tick
    track = bytes((2, 0, 0x10, 0x10)) + bytes((0x81,)) * 16
    track += FM.chunk(b'Mtsu', FM.voice(probe)) + FM.chunk(b'Mtsq', sequence)
    payload = FM.chunk(b'CNTI', bytes(5) + b'ST:Original high PM stress,') + FM.chunk(b'MTR\x00', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    for count in COUNTS:
        (args.output / f'high-pm-{count:02}.mmf').write_bytes(encode(count))
    (args.output / 'manifest.json').write_text(json.dumps({
        'purpose': 'Original high PM/polyphony fixtures, not music or handset calibration',
        'counts': COUNTS, 'channels': 16, 'note_on_ms': 250, 'gate_ms': 1500,
        'sequence_end_ms': 3000, 'native_pool_size': 32,
    }, indent=2) + '\n', encoding='utf-8')
