#!/usr/bin/env python3
"""Generate original SMAF fixtures for scheduled tone-dependency reporting."""
# SPDX-License-Identifier: Apache-2.0
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
    if not 0 <= value <= 0x1fffff:
        raise ValueError('delta/gate exceeds three bytes')
    digits = [value & 127]
    while value > 127:
        value >>= 7
        digits.append(128 | (value & 127))
    return bytes(reversed(digits))


def exclusive(payload):
    return b'\xf0' + vlq(len(payload) + 1) + payload + b'\xf7'


def fm_voice():
    # MA5 long-form bank0/program0, algorithm0; silent AR0 modulator and
    # original sine carrier. Seven-byte operators follow the three globals.
    off = bytes((0, 0x90, 0, 0xfc, 0, 0x10, 0))
    carrier = bytes((0, 0x90, 0xf0, 0x20, 0, 0x10, 0))
    return exclusive(bytes.fromhex('43 79 07 7f 01 00 00 00 00 00')
                     + bytes((0, 0x79, 0)) + off + carrier)


def wt_voice(wave_id, rom=False, bank=124, drum_note=0):
    # smaf_voice.cpp applyPcmBody: Fs b0..1; pan b2; SR b4; RR/DR b5;
    # AR/SL b6; TL b7; loop b11..12; end b13..14; ROM bit b15&0x80.
    body = bytes((0x1f, 0x40, 0x78, 0, 0, 0x90, 0xf0, 0x20,
                  0, 0, 0, 0, 0, 0, 80, (0x80 if rom else 0) | wave_id))
    return exclusive(bytes.fromhex('43 79 07 7f 01')
                     + bytes((bank, 0, 0, drum_note, 1)) + body)


def original_wave():
    # Self-authored unsigned PCM8, 80 samples. Not a vendor sample or melody.
    samples = bytes(round(128 + 38 * math.sin(2 * math.pi * i / 64)
                          + 9 * math.sin(2 * math.pi * i / 17)) for i in range(80))
    return exclusive(bytes.fromhex('43 79 07 7f 03 01 02') + samples)


# Explicit expectations are fixture declarations, not a second routing engine.
CASES = (
    dict(name='embedded-fm', bank=0, program=0, note=69, kind='fm',
         reason='bound_fm', substitute=None),
    dict(name='unbound-melodic-gm', bank=0, program=40, note=69, kind='none',
         reason='no_matching_supported_voice', substitute='gm_modern_recipe'),
    dict(name='unbound-percussion', bank=125, program=0, note=60, kind='none',
         reason='no_matching_supported_voice', substitute='drum_recipe'),
    dict(name='bound-ram-wt', bank=124, program=0, note=69, kind='ram', wave_id=1,
         reason='bound_ram', substitute=None),
    dict(name='missing-ram-wt', bank=124, program=0, note=69, kind='missing_ram', wave_id=1,
         reason='missing_ram_wave', substitute='gm_modern_recipe'),
    *(dict(name=f'explicit-rom-wt-{wave}', bank=124, program=0, note=69, kind='rom',
           wave_id=wave, reason='explicit_rom_wave', substitute=substitute)
      for wave, substitute in ((0, 'rom_recipe'), (22, 'rom_recipe'),
                               (26, 'rom_recipe'), (63, 'gm_modern_recipe'))),
    *(dict(name=f'custom-drum-{kind}-bank{bank}', bank=bank, program=0, note=60,
           drum_note=60, kind=kind, wave_id=63,
           reason='explicit_rom_wave' if kind == 'rom' else 'missing_ram_wave',
           substitute='drum_recipe')
      for kind in ('missing_ram', 'rom') for bank in (0, 124)),
    dict(name='custom-drum-bound-ram', bank=124, program=0, note=60, drum_note=60,
         kind='ram', wave_id=1, reason='bound_ram', substitute=None),
    dict(name='custom-drum-known-rom', bank=124, program=0, note=60, drum_note=60,
         kind='rom', wave_id=22, reason='explicit_rom_wave', substitute='rom_recipe'),
)


def encode(case):
    kind = case['kind']
    setup = fm_voice() if kind == 'fm' else b''
    if kind in ('ram', 'missing_ram', 'rom'):
        setup = wt_voice(case['wave_id'], kind == 'rom', case['bank'], case.get('drum_note', 0))
    if kind == 'ram':
        setup += original_wave()
    events = [(0, bytes((0xb0, cc, value))) for cc, value in
              ((0, case['bank']), (32, 0), (7, 100), (11, 127), (10, 64), (64, 0))]
    events += [(0, bytes((0xc0, case['program']))), (0, b'\xe0\x00\x40'),
               (25, bytes((0x90, case['note'], 100)) + vlq(150)),
               (300, b'\xff\x2f\x00')]
    previous = 0
    sequence = bytearray()
    for tick, message in events:
        sequence += vlq(tick - previous) + message
        previous = tick
    track = bytes((2, 0, 0x10, 0x10, 0x81)) + bytes(15)
    track += chunk(b'Mtsu', setup) + chunk(b'Mtsq', sequence)
    info = bytes(5) + ('ST:Original tone dependency ' + case['name'] + ',').encode('ascii')
    payload = chunk(b'CNTI', info) + chunk(b'MTR\0', track)
    data = b'MMMD' + struct.pack('>I', len(payload) + 2) + payload
    data += struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)
    return data, events


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema=1, purpose='Original scheduled tone-dependency fixtures; no vendor ROM or melody.',
                    scope='Expected logical routing; not hardware acceptance or timbre accuracy.',
                    format='Mobile Standard format2, MA5 long-form setup, 10ms time bases',
                    note_on_ms=250, gate_ms=1500, sequence_end_ms=3000, cases=[])
    for case in CASES:
        data, events = encode(case)
        filename = case['name'] + '.mmf'
        (directory / filename).write_bytes(data)
        authored = {key: value for key, value in case.items() if key not in ('reason', 'substitute')}
        manifest['cases'].append(dict(**authored, file=filename, bytes=len(data),
            sha256=hashlib.sha256(data).hexdigest(), crc=data[-2:].hex(),
            expected=dict(reason=case['reason'], substitute=case['substitute']),
            events=[dict(tick=tick, message_hex=message.hex()) for tick, message in events]))
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new destination; must not exist')
    args = parser.parse_args()
    try:
        manifest = generate(args.directory)
    except (OSError, ValueError) as error:
        parser.exit(1, f'error: {error}\n')
    print(f"Generated {len(manifest['cases'])} tone-dependency probes in {args.directory}")


if __name__ == '__main__':
    main()
