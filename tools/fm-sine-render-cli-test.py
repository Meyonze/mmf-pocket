#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual MMF -> experimental player -> PCM16 WAV, using original high-PM data."""
from array import array
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
args, rest = parser.parse_known_args()
BINARY = args.binary.resolve()
sys.argv = [sys.argv[0], *rest]
spec = importlib.util.spec_from_file_location('load_probes', Path(__file__).with_name('generate-fm-load-probes.py'))
probes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probes)


class SineRenderCliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='mmf-sine-cli-')
        cls.root = Path(cls.temp.name)
        cls.source = cls.root / 'original.mmf'
        cls.input = probes.encode(1)
        cls.source.write_bytes(cls.input)
        cls.cache = {}

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def render(self, factor=0, math='exact', no_seconds=False, reverse=False):
        key = (factor, math, no_seconds, reverse)
        if key in self.cache:
            return self.cache[key]
        output = self.root / f'out-{len(self.cache)}.wav'
        options = [] if not factor else [f'--experimental-sine-factor={factor}',
                                        f'--experimental-sine-math={math}']
        if reverse:
            options.reverse()
        result = subprocess.run([str(BINARY), str(self.source), str(output),
                                 *([] if no_seconds else ['4']), *options],
                                capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.read_bytes(), self.input)
        with wave.open(str(output)) as w:
            self.assertEqual(w.getparams()[:3], (2, 2, 44100))
            self.assertLess(w.getnframes(), 4 * 44100, 'must reach physical EOF, not the duration cap')
            pcm = w.readframes(w.getnframes())
        self.cache[key] = pcm
        return pcm

    def test_actual_native_alignment(self):
        normal, delayed = self.render(), self.render(1)
        self.assertEqual(delayed, bytes(72 * 4) + normal)

    def test_filtered_math_and_factors(self):
        native = self.render(1)
        outputs = []
        for factor in (2, 8, 32):
            exact = self.render(factor)
            fast = self.render(factor, 'hermite')
            self.assertEqual(len(exact), len(native))
            self.assertNotEqual(exact, native, 'candidate must actually use the new source/filter path')
            a, b = array('h'), array('h')
            a.frombytes(exact)
            b.frombytes(fast)
            self.assertEqual(len(a), len(b))
            self.assertLessEqual(max(abs(x - y) for x, y in zip(a, b)), 1)
            outputs.append(exact)
        self.assertEqual(len(set(outputs)), 3, 'requested factor must reach actual synthesis')

    def test_default_duration_and_option_order(self):
        expected = self.render(8, 'hermite')
        self.assertEqual(expected, self.render(8, 'hermite', no_seconds=True, reverse=True))


if __name__ == '__main__':
    unittest.main()
