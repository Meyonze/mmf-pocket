#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CLI contract tests using a deterministic fake synth; requires a C++20 compiler.

Compiles the actual example CLI against small test headers, without Qt, FFmpeg,
private MMFs, or the synthesis engine. Engine interoperability is a separate test.
"""
from array import array
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/yamaha-smaf-player/example/smaf-render.cpp"


class RendererCliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="smaf-cli-", dir=ROOT / "build")
        cls.root = Path(cls.temp.name)
        (cls.root / "smaf_file.h").write_text(r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace fxchain::smaf {
struct SmafFile {
    std::string title, vendor;
    std::vector<int> tracks{0};
    uint8_t mode = 0;
    bool parse(const uint8_t* data, size_t) { mode = data[0]; return true; }
};
}
''')
        (cls.root / "ma_player.h").write_text(r'''
#pragma once
#include "smaf_file.h"
#include <limits>
namespace fxchain::smaf {
struct MaPlayer {
    uint8_t mode = 0;
    bool init(const SmafFile& file, uint32_t) { mode = file.mode; return true; }
    int render(float* out, int frames) {
        const float samples[] = {0.5f, -0.5f, 2.0f, -2.0f};
        for (int i = 0; i < frames * 2; ++i)
            out[i] = mode == 0 ? 0.0f : mode == 5 ? 0.000001f : mode == 1 ? 0.0001f :
                     mode == 2 ? samples[i % 4] : mode == 3 ?
                     std::numeric_limits<float>::quiet_NaN() :
                     std::numeric_limits<float>::infinity();
        return frames;
    }
};
}
''')
        cls.binary = cls.root / "smaf-render"
        command = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run(command + ["-std=c++20", "-Wall", "-Wextra", "-I", str(cls.root),
                                  str(SOURCE), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_cli(self, mode=0, seconds="0.01", output=None, extra=()):
        source = self.root / "input.mmf"
        source.write_bytes(bytes([mode]) + bytes(11))
        self.output = output if output is not None else self.root / "output.wav"
        return subprocess.run([str(self.binary), str(source), str(self.output), seconds, *extra],
                              capture_output=True, text=True)

    def read_pcm(self):
        with wave.open(str(self.output)) as reader:
            self.assertEqual(reader.getparams()[:4], (2, 2, 44100, 441))
            data = reader.readframes(reader.getnframes())
        samples = array("h")
        # wave.readframes() already returns native-order PCM samples.
        samples.frombytes(data)
        raw = self.output.read_bytes()
        self.assertEqual(raw[:4], b"RIFF")
        self.assertEqual(struct.unpack_from("<I", raw, 4)[0], len(raw) - 8)
        self.assertEqual(struct.unpack_from("<I", raw, 40)[0], len(data))
        return samples

    def test_quiet_pcm_counts_as_sound(self):
        result = self.run_cli(mode=1)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(self.read_pcm()), {3})

    def test_silence(self):
        for mode in (0, 5):
            with self.subTest(mode=mode):
                result = self.run_cli(mode=mode)
                self.assertEqual(result.returncode, 3, result.stderr)
                self.assertEqual(set(self.read_pcm()), {0})

    def test_rounding_and_symmetric_clamp(self):
        result = self.run_cli(mode=2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(list(self.read_pcm()[:4]), [16384, -16384, 32767, -32767])
        self.assertEqual(self.output.read_bytes()[44:52], b'\x00\x40\x00\xc0\xff\x7f\x01\x80')

    def test_nonfinite_sample_rejected(self):
        for mode in (3, 4):
            with self.subTest(mode=mode):
                result = self.run_cli(mode=mode)
                self.assertEqual(result.returncode, 1)
                self.assertIn("nonfinite", result.stderr)
                self.assertNotIn("wrote", result.stdout)

    def test_invalid_arguments(self):
        for seconds in ("", "garbage", "1oops", "nan", "inf", "-inf", "0", "-1",
                        "600.001", "1e999", "1e-999"):
            with self.subTest(seconds=seconds):
                self.assertEqual(self.run_cli(seconds=seconds).returncode, 2)
        self.assertEqual(self.run_cli(extra=("extra",)).returncode, 2)
        result = subprocess.run([str(self.binary)], capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_default_duration(self):
        source = self.root / "default.mmf"
        source.write_bytes(bytes(12))
        output = self.root / "default.wav"
        result = subprocess.run([str(self.binary), str(source), str(output)], capture_output=True)
        self.assertEqual(result.returncode, 3)
        with wave.open(str(output)) as reader:
            self.assertEqual(reader.getnframes(), 30 * 44100)

    def test_input_size_and_missing_file(self):
        source = self.root / "oversized.mmf"
        with source.open("wb") as stream:
            stream.truncate(16 * 1024 * 1024 + 1)
        for path in (source, self.root / "missing.mmf"):
            result = subprocess.run([str(self.binary), str(path), str(self.root / "size.wav")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertNotIn("wrote", result.stdout)

    def test_write_failures(self):
        targets = [self.root / "missing-directory/output.wav", self.root]
        if Path("/dev/full").exists():
            targets.append(Path("/dev/full"))
        for target in targets:
            with self.subTest(target=target):
                result = self.run_cli(mode=1, output=target)
                self.assertEqual(result.returncode, 1)
                self.assertNotIn("wrote", result.stdout)

    def test_input_output_aliases_preserve_input(self):
        source = self.root / "input.mmf"
        source.write_bytes(bytes(12))
        hardlink = self.root / "hardlink.mmf"
        symlink = self.root / "symlink.mmf"
        os.link(source, hardlink)
        symlink.symlink_to(source)
        for output in (source, hardlink, symlink):
            with self.subTest(output=output):
                result = self.run_cli(output=output)
                self.assertEqual(result.returncode, 1)
                self.assertIn("different files", result.stderr)
                self.assertEqual(source.read_bytes(), bytes(12))


if __name__ == "__main__":
    unittest.main()
