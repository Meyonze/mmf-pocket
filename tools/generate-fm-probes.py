#!/usr/bin/env python3
"""Write original custom-FM score probes in uncompressed Mobile Standard SMAF."""
# SPDX-License-Identifier: Apache-2.0
import argparse
import binascii
from dataclasses import asdict, dataclass, replace
import hashlib
import json
from pathlib import Path
import struct


@dataclass(frozen=True)
class Operator:
    ar: int = 15
    dr: int = 0
    sr: int = 0
    rr: int = 10
    sl: int = 0
    tl: int = 12
    multi: int = 1
    ws: int = 0
    fb: int = 0
    dvb: int = 0
    evb: bool = False
    ksr: bool = False

    def encode(self):
        for name in ('ar', 'dr', 'sr', 'rr', 'sl', 'multi'):
            if not 0 <= getattr(self, name) <= 15:
                raise ValueError(name + ' out of range')
        for name, limit in (('tl', 63), ('ws', 31), ('fb', 7), ('dvb', 3)):
            if not 0 <= getattr(self, name) <= limit:
                raise ValueError(name + ' out of range')
        # VM5 seven-byte operator image; unused flags and detune are zero.
        return bytes((self.sr << 4 | int(self.ksr), self.rr << 4 | self.dr,
                      self.ar << 4 | self.sl, self.tl << 2,
                      self.dvb << 1 | int(self.evb), self.multi << 4,
                      self.ws << 3 | self.fb))


@dataclass(frozen=True)
class Probe:
    name: str
    description: str
    algorithm: int
    operators: tuple
    lfo: int = 0
    bend: bool = False
    note: int = 69
    ksr_bend_order: str = ''


CARRIER = Operator()
OFF = Operator(ar=0, tl=63)
MODULATOR = Operator(tl=24)
CASES = (
    Probe('sine', 'Sine carrier with an inactive modulator (AR=0).', 0, (OFF, CARRIER)),
    Probe('fm-2op', 'Two sine operators in series; modulator TL=24.', 0, (MODULATOR, CARRIER)),
    Probe('fm-4op', 'Four sine operators in series; algorithm 4.', 4,
          (Operator(tl=32), Operator(tl=28), MODULATOR, CARRIER)),
    Probe('waveform-1', 'Carrier wave-shape code 1; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=1))),
    Probe('envelope', 'Carrier AR=8 DR=6 SL=6 SR=0 RR=5; gate 1.5 seconds.', 0,
          (OFF, replace(CARRIER, ar=8, dr=6, sl=6, rr=5))),
    Probe('feedback', 'Two-operator FM with modulator feedback code 3.', 0,
          (replace(MODULATOR, fb=3), CARRIER)),
    Probe('vibrato-slow', 'Carrier EVB=1 DVB=3, LFO rate code 0.', 0,
          (OFF, replace(CARRIER, evb=True, dvb=3)), lfo=0),
    Probe('vibrato-fast', 'Carrier EVB=1 DVB=3, LFO rate code 3.', 0,
          (OFF, replace(CARRIER, evb=True, dvb=3)), lfo=3),
    Probe('pitch-bend', 'Sine with explicit two-semitone bend range; center, +4096, -4096.',
          0, (OFF, CARRIER), bend=True),
    Probe('waveform-square', 'Carrier square wave-shape code 6; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=6))),
    Probe('waveform-triangle', 'Carrier triangle wave-shape code 16; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=16))),
    Probe('waveform-saw', 'Carrier saw wave-shape code 24; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=24))),
    Probe('waveform-19', 'Carrier wave-shape code 19; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=19))),
    Probe('waveform-22', 'Carrier wave-shape code 22; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=22))),
    Probe('waveform-26', 'Carrier wave-shape code 26; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=26))),
    Probe('waveform-27', 'Carrier wave-shape code 27; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=27))),
    Probe('waveform-29', 'Carrier wave-shape code 29; inactive modulator.', 0,
          (OFF, replace(CARRIER, ws=29))),
    *(Probe(f'waveform-{code}', f'Carrier wave-shape code {code}; reference shape unresolved.',
            0, (OFF, replace(CARRIER, ws=code))) for code in range(7, 14)),
    *(probe for label, code in (('square', 6), ('saw', 24)) for probe in (
        Probe(f'{label}-active-modulator', 'Active shaped modulator into a sine carrier; retain nonlinear FM.',
              0, (replace(MODULATOR, ws=code), CARRIER)),
        Probe(f'{label}-active-pm-carrier', 'Active sine modulator into a shaped carrier; retain nonlinear FM.',
              0, (MODULATOR, replace(CARRIER, ws=code))),
        Probe(f'{label}-feedback-output', 'Shaped direct output with its own feedback; retain feedback synthesis.',
              1, (OFF, replace(CARRIER, ws=code, fb=3))),
        Probe(f'{label}-additive-output', 'Independent sine and shaped direct outputs; bandlimit only the shaped output.',
              1, (CARRIER, replace(CARRIER, ws=code))),
    )),
    Probe('ksr-bend-before-note', 'KSR reference question: +8191 bend and NoteOn at the same tick.',
          0, (OFF, replace(CARRIER, ar=6, dr=0, sr=0, rr=5, ksr=True)),
          note=65, ksr_bend_order='before'),
    Probe('ksr-bend-after-note', 'KSR reference question: +8191 bend and NoteOn at the same tick.',
          0, (OFF, replace(CARRIER, ar=6, dr=0, sr=0, rr=5, ksr=True)),
          note=65, ksr_bend_order='after'),
)


def vlq(value):
    if not 0 <= value <= 0x1fffff:
        raise ValueError('Mobile Standard delta/gate exceeds three bytes')
    digits = [value & 127]
    while value > 127:
        value >>= 7
        digits.append(128 | (value & 127))
    return bytes(reversed(digits))


def chunk(tag, payload):
    if len(tag) != 4:
        raise ValueError('chunk tag must have four bytes')
    return tag + struct.pack('>I', len(payload)) + payload


def voice(probe):
    count = 2 if probe.algorithm < 2 else 4
    if not 0 <= probe.algorithm <= 7 or len(probe.operators) != count:
        raise ValueError('algorithm/operator count mismatch')
    if not 0 <= probe.lfo <= 3:
        raise ValueError('LFO code out of range')
    # Yamaha VM5 voice: bank MSB/LSB/program/drum-note/type all zero.
    prefix = bytes.fromhex('43 79 07 7f 01 00 00 00 00 00')
    # Drum key unused; pan 15 (disabled); basic octave 1 (no transpose).
    body = bytes((0, (15 << 3) | 1, (probe.lfo << 6) | probe.algorithm))
    payload = prefix + body + b''.join(op.encode() for op in probe.operators)
    # Setup exclusive uses a single-byte length, including the F7 terminator.
    return b'\xf0' + bytes((len(payload) + 1,)) + payload + b'\xf7'


def events(probe):
    # All times in 10 ms ticks. Set supported channel state explicitly. Do not
    # send CC1: Mobile/MA-5 modulation semantics need external calibration.
    result = [(0, bytes((0xb0, cc, value))) for cc, value in
              ((0, 0), (32, 0), (7, 100), (11, 127), (10, 64), (64, 0))]
    result.extend(((0, b'\xc0\x00'), (0, b'\xe0\x00\x40')))
    if probe.bend or probe.ksr_bend_order:
        result.extend((0, bytes((0xb0, cc, value))) for cc, value in
                      ((101, 0), (100, 0), (6, 2), (38, 0), (101, 127), (100, 127)))
    note = (25, bytes((0x90, probe.note, 100)) + vlq(150))
    bend = (25, b'\xe0\x7f\x7f')
    if probe.ksr_bend_order:
        result.extend((bend, note) if probe.ksr_bend_order == 'before' else (note, bend))
    else:
        result.append(note)
    if probe.bend:
        result.extend(((75, b'\xe0\x00\x60'), (125, b'\xe0\x00\x20'),
                       (200, b'\xe0\x00\x40')))
    result.append((300, b'\xff\x2f\x00'))
    return result


def encode(probe):
    sequence = bytearray()
    previous = 0
    for tick, message in events(probe):
        sequence.extend(vlq(tick - previous) + message)
        previous = tick
    # Format 2, sequence 0, duration/gate time bases 0x10 = 10 ms.
    # Channel 0 KeyControl ON + melody; other channels unspecified and unused.
    track = bytes((2, 0, 0x10, 0x10, 0x81)) + bytes(15)
    track += chunk(b'Mtsu', voice(probe)) + chunk(b'Mtsq', sequence)
    info = bytes((0, 0, 0, 0, 0)) + ('ST:FM probe ' + probe.name + ',').encode('ascii')
    payload = chunk(b'CNTI', info) + chunk(b'MTR\x00', track)
    header = b'MMMD' + struct.pack('>I', len(payload) + 2)
    data = header + payload
    return data + struct.pack('>H', binascii.crc_hqx(data, 0xffff) ^ 0xffff)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    manifest = {
        'schema': 1,
        'purpose': 'Original synthetic custom FM score probes; no ROM voices or music.',
        'format': 'Mobile Standard uncompressed, MA-5 custom FM setup exclusive',
        'validation_scope': 'Software structure/CRC/events/voice validation; external playback unverified.',
        'timebase_ms': 10, 'velocity': 100, 'note_on_ms': 250,
        'gate_ms': 1500, 'sequence_end_ms': 3000,
        'cases': [],
    }
    for probe in CASES:
        data = encode(probe)
        path = directory / (probe.name + '.mmf')
        path.write_bytes(data)
        metadata = asdict(probe)
        # Event order already records the comparison variable.
        metadata.pop('ksr_bend_order')
        manifest['cases'].append({**metadata, 'file': path.name, 'bytes': len(data),
                                  'sha256': hashlib.sha256(data).hexdigest(),
                                  'crc': data[-2:].hex(),
                                  'events': [{'tick': tick, 'message_hex': message.hex()}
                                             for tick, message in events(probe)]})
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new output directory (must not exist)')
    args = parser.parse_args()
    try:
        manifest = generate(args.directory)
    except (OSError, ValueError) as error:
        parser.exit(1, f'error: {error}\n')
    print(f"Generated {len(manifest['cases'])} FM probes in {args.directory}")


if __name__ == '__main__':
    main()
