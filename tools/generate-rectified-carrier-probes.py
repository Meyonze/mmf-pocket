#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original high rectified-sine carriers, nonlinear and low-band controls."""
import argparse
from dataclasses import asdict, replace
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

SPEC = importlib.util.spec_from_file_location('rectified_fm_probes',
    Path(__file__).with_name('generate-fm-probes.py'))
FM = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = FM
SPEC.loader.exec_module(FM)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    cases = []
    masks = (2, 3, 15, 8, 8, 10, 9, 13)
    off = FM.Operator(ar=0, tl=63)

    def write(probe, changed):
        data = FM.encode(probe)
        name = probe.name + '.mmf'
        (directory / name).write_bytes(data)
        cases.append(dict(file=name, expected_changed=changed, render_cap_seconds=5,
                          sha256=hashlib.sha256(data).hexdigest(), probe=asdict(probe)))

    for wave in (1, 2):
        carrier = FM.Operator(ws=wave, multi=2, tl=18)
        for algorithm, mask in enumerate(masks):
            count = 2 if algorithm < 2 else 4
            ops = tuple(carrier if mask & (1 << i) else off for i in range(count))
            for note in (69, 112, 114, 117, 120):
                write(FM.Probe(f'rectified-w{wave}-a{algorithm}-n{note}',
                    'Only direct final rectified-sine carriers sound.',
                    algorithm, ops, note=note), note >= 114)
        for algorithm in (0, 4):
            write(FM.Probe(f'rectified-w{wave}-active-pm-a{algorithm}',
                'Live serial modulation excludes correction.', algorithm,
                (carrier,) * (2 if algorithm == 0 else 4), note=120), False)
        write(FM.Probe(f'rectified-w{wave}-feedback', 'Feedback excludes correction.',
            1, (replace(carrier, fb=3),) * 2, note=120), False)
        write(FM.Probe(f'rectified-w{wave}-vibrato', 'Intrinsic carrier vibrato.',
            0, (off, replace(carrier, evb=True, dvb=3)), note=120, lfo=3), True)
        write(FM.Probe(f'rectified-w{wave}-bend', 'Pitch bend crosses Fs/4.',
            0, (off, carrier), note=112, bend=True), True)
    (directory / 'manifest.json').write_text(json.dumps({'cases': cases}, indent=2) + '\n')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new output directory')
    args = parser.parse_args()
    print(f'Generated {len(generate(args.directory))} original rectified-carrier scores')
