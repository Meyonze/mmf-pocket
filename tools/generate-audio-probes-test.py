# SPDX-License-Identifier: Apache-2.0
import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import wave

TOOL = Path(__file__).with_name("generate-audio-probes.py")
spec = importlib.util.spec_from_file_location("generate_audio_probes", TOOL)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def chunk(tag, payload):
    return tag + struct.pack('>I', len(payload)) + payload


def crc(data):
    value = 65535
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 32768 else 0)) & 65535
    return struct.pack('>H', value ^ 65535)


def audio_fixture(opda=True, with_crc=False, rate=0x11):
    cnti = chunk(b'CNTI', bytes((0, 1, 1, 0, 0)))
    sequence = chunk(b'Atsq', bytes.fromhex('00 01 01 01 ff 00 00'))
    # The audio tail includes bytes that could be mistaken for a CRC trailer.
    wave_data = chunk(b'Awa\x01', bytes.fromhex('80 08 ff f7 00 00 80 80'))
    atr = chunk(b'ATR\0', bytes((0, 0, rate, 0, 2, 2)) + sequence + wave_data)
    payload = cnti + (chunk(b'OPDA', b'VN:Lavf-test,') if opda else b'') + atr
    data = b'MMMD' + struct.pack('>I', len(payload) + (2 if with_crc else 0)) + payload
    return data + crc(data) if with_crc else data


class GenerateAudioProbesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_pcm_probes_and_manifest(self):
        directory = self.root / "probes"
        manifest = module.generate(directory)
        self.assertEqual(len(manifest["cases"]), 5)
        self.assertEqual(manifest, json.loads((directory / "manifest.json").read_text()))
        for case in manifest["cases"]:
            filename, checksum = next(iter(case["files"].items()))
            path = directory / filename
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), checksum)
            self.assertEqual(len(case["files"]), 1)  # no FFmpeg dependency by default
            with wave.open(str(path), "rb") as wav:
                self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()),
                                 (1, 2, case["source_rate"]))
                self.assertEqual(wav.getnframes(), case["source_frames"])
                pcm = struct.unpack("<" + "h" * wav.getnframes(), wav.readframes(wav.getnframes()))
            self.assertLessEqual(max(abs(x) for x in pcm), 8192)
            if case["id"].startswith("silence"):
                self.assertTrue(all(x == 0 for x in pcm))
            elif case["id"].startswith("quiet"):
                half_second = case["source_rate"] // 2
                self.assertTrue(all(x == 0 for x in pcm[:half_second]))
                self.assertTrue(all(x == 0 for x in pcm[-half_second:]))
                self.assertGreater(max(abs(x) for x in pcm), 80)
                self.assertLess(max(abs(x) for x in pcm), 110)
            elif case["id"].startswith("steps"):
                self.assertEqual(pcm[4000], 6553)
                self.assertEqual(pcm[12000], -6553)
            else:
                self.assertGreater(max(abs(x) for x in pcm), 6000)

    def test_existing_output_is_preserved(self):
        directory = self.root / "existing"
        directory.mkdir()
        original = directory / "capture.wav"
        original.write_bytes(b"existing user capture")
        with self.assertRaises(FileExistsError):
            module.generate(directory)
        self.assertEqual(original.read_bytes(), b"existing user capture")
        self.assertEqual(list(directory.iterdir()), [original])

    def test_pcm_is_repeatable(self):
        first = module.generate(self.root / "one")
        second = module.generate(self.root / "two")
        self.assertEqual(first, second)

    def test_optional_encoder_preflight(self):
        directory = self.root / "missing-encoder"
        with patch.object(module.shutil, "which", return_value=None):
            with self.assertRaisesRegex(ValueError, "FFmpeg"):
                module.generate(directory, smaf=True)
        self.assertFalse(directory.exists())

    def test_quantization(self):
        self.assertEqual(module.quantize(0), 0)
        self.assertEqual(module.quantize(0.5 / 32767), 1)
        self.assertEqual(module.quantize(-0.5 / 32767), -1)
        self.assertEqual(module.quantize(2), 32767)
        self.assertEqual(module.quantize(-2), -32767)

    def test_native_wave_pcm_is_compared_as_little_endian(self):
        samples = (0, 1, -1, 32767, -32768, 0x1234, -0x1234)
        little = struct.pack('<7h', *samples)
        big = struct.pack('>7h', *samples)
        # Model wave.readframes output on each host without mutating sys itself.
        for byteorder, native in (('little', little), ('big', big)):
            with self.subTest(byteorder=byteorder):
                with patch.object(module, 'sys', SimpleNamespace(byteorder=byteorder)):
                    self.assertEqual(module.native_pcm16_to_le(native), little)
                    self.assertEqual(module.native_pcm16_to_le(b''), b'')
                    with self.assertRaises(ValueError):
                        module.native_pcm16_to_le(b'\0')

    def test_normalization_preserves_encoded_chunks_and_adds_crc(self):
        for rate in (0x11, 0x13, 0x14):
            with self.subTest(rate=rate):
                original = audio_fixture(rate=rate)
                normalized, record = module.normalize_smaf(original)
                before, trailer = module.bounded_chunks(original[8:], allow_crc=True)
                after, new_crc = module.bounded_chunks(normalized[8:], allow_crc=True)
                self.assertEqual(trailer, b'')
                self.assertEqual(after, [before[0], before[-1]])
                self.assertEqual(normalized[-2:], crc(normalized[:-2]))
                self.assertEqual(new_crc, normalized[-2:])
                self.assertEqual(int.from_bytes(normalized[4:8], 'big'), len(normalized) - 8)
                self.assertFalse(record['encoded_crc_present'])
                self.assertTrue(record['removed_opda'])
                self.assertEqual(record['encoded_sha256'], hashlib.sha256(original).hexdigest())
                self.assertEqual(record['preserved_atr_sha256'], hashlib.sha256(before[-1]).hexdigest())

    def test_valid_existing_crc_and_idempotence(self):
        for opda in (False, True):
            original = audio_fixture(opda=opda, with_crc=True)
            normalized, record = module.normalize_smaf(original)
            self.assertTrue(record['encoded_crc_present'])
            self.assertEqual(record['removed_opda'], opda)
            again, record = module.normalize_smaf(normalized)
            self.assertEqual(again, normalized)
            self.assertFalse(record['removed_opda'])
            self.assertTrue(record['encoded_crc_present'])
        invalid = bytearray(audio_fixture(with_crc=True))
        invalid[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'existing SMAF CRC'):
            module.normalize_smaf(invalid)

    def test_normalization_rejects_bad_outer_lengths_and_shapes(self):
        valid = audio_fixture()
        for data in (b'', b'RIFF' + valid[4:], valid[:-1], valid + b'\0',
                     b'MMMD' + struct.pack('>I', len(valid) - 9) + valid[8:],
                     chunk(b'MMMD', valid[8:-1]), chunk(b'MMMD', b'CNTI'),
                     chunk(b'MMMD', chunk(b'NOPE', b'')),
                     chunk(b'MMMD', valid[8:] + b'\0'),
                     chunk(b'MMMD', valid[8:] + chunk(b'JUNK', b'')),
                     audio_fixture(rate=0x15)):
            with self.subTest(data=data[:24]):
                with self.assertRaises(ValueError):
                    module.normalize_smaf(data)

    def test_normalization_rejects_malformed_nested_chunks(self):
        parts, _ = module.bounded_chunks(audio_fixture()[8:])
        cnti, opda, atr = parts
        header = atr[8:14]
        for nested in (b'Atsq', b'Atsq\0\0\0\x10abc',
                       chunk(b'Atsq', b'x') + b'Awa\x01\xff\xff\xff\xff',
                       chunk(b'Atsq', b'x') + chunk(b'Awa\x01', b''),
                       chunk(b'Awa\x01', b'x') + chunk(b'Atsq', b'x'),
                       atr[14:] + b'\0\0'):
            with self.subTest(nested=nested):
                data = chunk(b'MMMD', cnti + opda + chunk(b'ATR\0', header + nested))
                with self.assertRaises(ValueError):
                    module.normalize_smaf(data)
        for metadata in (chunk(b'CNTI', b'\0') + atr,
                         cnti + chunk(b'OPDA', b'other metadata') + atr,
                         cnti + opda + opda + atr,
                         cnti + chunk(b'ATR\0', b'\0\0')):
            with self.assertRaises(ValueError):
                module.normalize_smaf(chunk(b'MMMD', metadata))


if __name__ == "__main__":
    unittest.main()
