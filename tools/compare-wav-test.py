# SPDX-License-Identifier: Apache-2.0
import importlib.util
from array import array
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import wave

TOOL = Path(__file__).with_name("compare-wav.py")
spec = importlib.util.spec_from_file_location("compare_wav", TOOL)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CompareWavTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def wav(self, name, values, channels=1, rate=8000):
        path = self.root / name
        with wave.open(str(path), "wb") as out:
            out.setparams((channels, 2, rate, 0, "NONE", "not compressed"))
            out.writeframes(struct.pack("<" + "h" * len(values), *values))
        return path

    def test_identical_and_silence(self):
        for values in ([], [0] * 19, [-32768, -1, 0, 32767]):
            path = self.wav("same.wav", values)
            result = module.compare(path, path)
            self.assertTrue(result["identical_pcm"])
            self.assertIsNone(result["first_different_frame"])
            self.assertEqual(result["channel_metrics"][0]["difference"]["rms_pcm"], 0)
            json.dumps(result, allow_nan=False)

    def test_reader_samples_are_already_native_endian(self):
        class Reader:
            def readframes(self, frames):
                # wave.readframes converts the on-disk little-endian PCM to
                # native order, including on big-endian hosts.
                return array("h", [-32768, -1234, 0, 1234, 32767]).tobytes()

        with patch.object(module, "sys", SimpleNamespace(byteorder="big")):
            self.assertEqual(list(module.samples(Reader(), 5, 1)),
                             [-32768, -1234, 0, 1234, 32767])

    def test_stereo_metrics_and_chunk_boundaries(self):
        # A single changed right-channel sample just past the read boundary.
        a = self.wav("a.wav", [100, -100] * 5000, channels=2)
        values = [100, -100] * 5000
        values[4096 * 2 + 1] = -80
        result = module.compare(a, self.wav("b.wav", values, channels=2))
        self.assertEqual(result["first_different_frame"], 4096)
        self.assertEqual(result["last_different_frame"], 4096)
        self.assertEqual(result["channel_metrics"][0]["changed_samples"], 0)
        right = result["channel_metrics"][1]
        self.assertEqual(right["changed_samples"], 1)
        self.assertAlmostEqual(right["difference"]["rms_pcm"], 20 / 5000 ** 0.5)
        self.assertAlmostEqual(right["difference"]["dc_pcm"], 20 / 5000)

    def test_tail_is_reported_not_discarded(self):
        a = self.wav("a.wav", [1, 2, 0, 4])
        b = self.wav("b.wav", [1, 2])
        result = module.compare(a, b)
        self.assertEqual(result["reference_only_frames"], 2)
        self.assertEqual(result["first_different_frame"], 3)
        self.assertEqual(result["nonzero_samples_in_unmatched_tail"], 1)
        self.assertEqual(result["channel_metrics"][0]["difference"]["rms_pcm"], 2)
        # Silent trailing frames differ in duration even with zero PCM error.
        result = module.compare(a, self.wav("longer.wav", [1, 2, 0, 4, 0]))
        self.assertFalse(result["identical_pcm"])
        self.assertEqual(result["candidate_only_frames"], 1)
        self.assertIsNone(result["first_different_frame"])

    def test_reject_mismatch_and_truncation(self):
        a = self.wav("a.wav", [1, 2])
        for b in (self.wav("rate.wav", [1, 2], rate=16000),
                  self.wav("stereo.wav", [1, 2], channels=2)):
            with self.assertRaisesRegex(ValueError, "must match"):
                module.compare(a, b)
        b = self.wav("truncated.wav", [1, 2, 3])
        b.write_bytes(b.read_bytes()[:-2])
        with self.assertRaisesRegex(ValueError, "truncated"):
            module.compare(a, b)

    def test_reject_width_and_partial_frame(self):
        a = self.wav("a.wav", [1, 2])
        b = self.root / "eight-bit.wav"
        with wave.open(str(b), "wb") as out:
            out.setparams((1, 1, 8000, 0, "NONE", "not compressed"))
            out.writeframes(bytes([128, 129]))
        with self.assertRaisesRegex(ValueError, "PCM16"):
            module.compare(a, b)
        b = self.wav("odd.wav", [1, 2, 3])
        b.write_bytes(b.read_bytes()[:-1])
        with self.assertRaisesRegex(ValueError, "incomplete"):
            module.compare(a, b)

    def test_reject_zero_sample_rate(self):
        a = self.wav("a.wav", [1, 2])
        b = self.wav("zero-rate.wav", [1, 2])
        data = bytearray(b.read_bytes())
        data[24:28] = bytes(4)
        b.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "positive"):
            module.compare(a, b)

    def test_cli_exit_codes(self):
        a = self.wav("a.wav", [1, 2]); b = self.wav("b.wav", [1, 3])
        for path, expected in ((a, 0), (b, 1), (self.root / "missing.wav", 2)):
            result = subprocess.run([sys.executable, str(TOOL), str(a), str(path),
                                     "--require-identical"], capture_output=True, text=True)
            self.assertEqual(result.returncode, expected)
            if expected != 2:
                self.assertEqual(json.loads(result.stdout)["identical_pcm"], expected == 0)

    def test_selected_interval_excludes_onset_and_tail(self):
        a = self.wav("a.wav", [99, 100, -100, 7])
        b = self.wav("b.wav", [-99, 100, -100, 8, 9])
        result = module.compare(a, b, start_frame=1, frames=2)
        self.assertTrue(result["identical_pcm"])
        self.assertEqual(result["comparison_start_frame"], 1)
        self.assertEqual(result["comparison_frame_limit"], 2)
        self.assertEqual(result["reference_source_frames"], 4)
        self.assertEqual(result["candidate_source_frames"], 5)
        self.assertEqual(result["reference_frames"], 2)
        self.assertEqual(result["candidate_frames"], 2)
        self.assertEqual(result["reference_seconds"], 2 / 8000)
        self.assertEqual(result["channel_metrics"][0]["reference"]["rms_pcm"], 100)
        self.assertIsNone(result["first_different_frame"])
        # Selection never alters the source files or whole-file result.
        self.assertFalse(module.compare(a, b)["identical_pcm"])

    def test_selected_stereo_chunk_boundary_uses_source_frame_positions(self):
        a = self.wav("a.wav", [100, -100] * 6000, channels=2)
        values = [100, -100] * 6000
        values[4106 * 2 + 1] = -80
        values[5010 * 2] = 7  # First frame outside the selected interval.
        b = self.wav("b.wav", values, channels=2)
        result = module.compare(a, b, start_frame=10, frames=5000)
        self.assertEqual(result["first_different_frame"], 4106)
        self.assertEqual(result["last_different_frame"], 4106)
        self.assertEqual(result["channel_metrics"][0]["changed_samples"], 0)
        self.assertEqual(result["channel_metrics"][1]["changed_samples"], 1)
        self.assertAlmostEqual(result["channel_metrics"][1]["difference"]["rms_pcm"],
                               20 / 5000 ** 0.5)

    def test_start_without_limit_keeps_unmatched_tail(self):
        a = self.wav("a.wav", [1, 2, 3])
        b = self.wav("b.wav", [1, 2, 3, 4])
        result = module.compare(a, b, start_frame=3)
        self.assertFalse(result["identical_pcm"])
        self.assertEqual(result["reference_frames"], 0)
        self.assertEqual(result["candidate_frames"], 1)
        self.assertEqual(result["first_different_frame"], 3)
        self.assertEqual(result["nonzero_samples_in_unmatched_tail"], 1)

    def test_selected_interval_rejects_invalid_bounds_and_truncation(self):
        a = self.wav("a.wav", list(range(20)))
        for kwargs in ({"start_frame": -1}, {"frames": 0}, {"frames": -1},
                       {"start_frame": 21}, {"start_frame": 20, "frames": 1}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                module.compare(a, a, **kwargs)
        short = self.wav("short.wav", list(range(19)))
        with self.assertRaisesRegex(ValueError, "fit in both"):
            module.compare(a, short, frames=20)
        truncated = self.wav("truncated.wav", list(range(20)))
        truncated.write_bytes(truncated.read_bytes()[:-4])
        with self.assertRaisesRegex(ValueError, "truncated"):
            module.compare(a, truncated, start_frame=15, frames=5)

    def test_interval_cli_exit_codes(self):
        a = self.wav("a.wav", [1, 2, 3])
        b = self.wav("b.wav", [9, 2, 4])
        for extra, expected in ((["--start-frame", "1", "--frames", "1"], 0),
                                (["--start-frame", "1"], 1),
                                (["--frames", "4"], 2), (["--frames", "0"], 2),
                                (["--start-frame", "-1"], 2)):
            with self.subTest(extra=extra):
                result = subprocess.run([sys.executable, str(TOOL), str(a), str(b),
                                         "--require-identical", *extra],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stderr)
                if expected != 2:
                    self.assertEqual(json.loads(result.stdout)["identical_pcm"], expected == 0)


if __name__ == "__main__":
    unittest.main()
