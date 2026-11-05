#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original near-native anti-alias scores, passband signals and scope controls."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

SPEC = importlib.util.spec_from_file_location('near_nyquist_source',
                                             Path(__file__).with_name('generate-unity-pcm-probes.py'))
SOURCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOURCE)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    cases = []

    def write(rate, frequency=0.25, note=60, length=4096, recorded=False, fmt=3, bend=False):
        name = f'near-nyquist-{("audio" if recorded else "wt")}-fmt{fmt}-fs{rate}-f{frequency}-note{note}-len{length}-bend{int(bend)}.mmf'
        data = SOURCE.encode(rate, frequency, bend, note, length, recorded, fmt)
        advance = rate / 44100 * (1 if recorded else 2 ** ((note - 60) / 12))
        advances = (advance, advance * 2 ** (1 / 12)) if bend else (advance,)
        (directory / name).write_bytes(data)
        cases.append(dict(file=name, rate=rate, frequency=frequency, note=note, length=length,
                          recorded=recorded, format=fmt, bend=bend,
                          expected_changed=any(1 < value < 1.1 for value in advances),
                          render_cap_seconds=3, sha256=hashlib.sha256(data).hexdigest()))

    rates = (44100, 44541, 44982, 46305, 47628, 48000)
    for rate in rates:
        for frequency in (0.25, 0.4, 0.499):
            write(rate, frequency)
    for fmt in (2, 3):
        for rate in rates:
            for frequency in (0.25, 0.499):
                write(rate, frequency, recorded=True, fmt=fmt)
    for rate, note in ((32000, 72), (40000, 60), (44100, 72), (48000, 72),
                       (22050, 72), (24255, 72)):
        write(rate, note=note)
    for rate in (44982, 46305):
        for length in (8, 32):
            write(rate, length=length)
    for frequency in (0.25, 0.4):
        write(44100, frequency, bend=True)
    (directory / 'manifest.json').write_text(json.dumps({'cases': cases}, indent=2) + '\n')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new output directory')
    args = parser.parse_args()
    print(f'Generated {len(generate(args.directory))} original near-Nyquist scores')
