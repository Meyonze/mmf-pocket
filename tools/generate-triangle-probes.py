#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original high-triangle FM scores and controls; no vendor waveforms."""
import argparse
from dataclasses import asdict, replace
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

SPEC = importlib.util.spec_from_file_location('triangle_fm_probes',
                                             Path(__file__).with_name('generate-fm-probes.py'))
FM = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = FM
SPEC.loader.exec_module(FM)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=False)
    cases = []
    carrier_masks = (2, 3, 15, 8, 8, 10, 9, 13)
    silent = FM.Operator(ar=0, tl=63)
    carrier = FM.Operator(ws=16, tl=18)

    def write(probe, changed):
        data = FM.encode(probe)
        name = probe.name + '.mmf'
        (directory / name).write_bytes(data)
        cases.append(dict(file=name, expected_changed=changed, render_cap_seconds=5,
                          sha256=hashlib.sha256(data).hexdigest(), probe=asdict(probe)))

    for algorithm, mask in enumerate(carrier_masks):
        count = 2 if algorithm < 2 else 4
        operators = tuple(carrier if mask & (1 << i) else silent for i in range(count))
        for note in (69, 117, 120, 124):
            probe = FM.Probe(f'triangle-alg{algorithm}-note{note}',
                             'Only direct final carriers sound; upstream sources have stopped attacks.',
                             algorithm, operators, note=note)
            write(probe, note > 117)

    for algorithm in (0, 4):
        count = 2 if algorithm == 0 else 4
        write(FM.Probe(f'triangle-active-pm-alg{algorithm}', 'Live serial phase modulation control.',
                       algorithm, (carrier,) * count, note=124), False)
    write(FM.Probe('triangle-feedback', 'Every direct output has feedback.',
                   1, (replace(carrier, fb=3),) * 2, note=124), False)
    write(FM.Probe('triangle-vibrato', 'Intrinsic vibrato on a high unmodulated carrier.',
                   0, (silent, replace(carrier, evb=True, dvb=3)), note=120, lfo=2), True)
    write(FM.Probe('triangle-bend-crossing', 'Bend crosses the third-harmonic Nyquist boundary.',
                   0, (silent, carrier), note=117, bend=True), True)
    (directory / 'manifest.json').write_text(json.dumps({'cases': cases}, indent=2) + '\n')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='new output directory')
    args = parser.parse_args()
    print(f'Generated {len(generate(args.directory))} original triangle scores')
