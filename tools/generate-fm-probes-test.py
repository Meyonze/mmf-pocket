#!/usr/bin/env python3
"""Probe checks; --oracle adds independent go-smaf checks, --native builds C++."""
# SPDX-License-Identifier: Apache-2.0
import argparse
from dataclasses import asdict
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('fm_probes', HERE / 'generate-fm-probes.py')
GEN = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GEN
SPEC.loader.exec_module(GEN)
OPTIONS = argparse.Namespace(native=False, oracle=None, cxx=shutil.which('g++') or 'c++')


def chunks(data):
    result = []
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError('truncated header')
        size = struct.unpack_from('>I', data, offset + 4)[0]
        end = offset + 8 + size
        if end > len(data):
            raise ValueError('truncated chunk')
        result.append((data[offset:offset + 4], data[offset + 8:end]))
        offset = end
    return result


def check_crc(data):
    # Bitwise implementation independent of the generator's stdlib CRC call.
    crc = 65535
    for value in data[:-2]:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 32768 else 0)) & 65535
    return (crc ^ 65535) == int.from_bytes(data[-2:], 'big')


NATIVE_SOURCE = r'''
#include "smaf_file.h"
#include "smaf_voice.h"
#include "ma_player.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace fxchain::smaf;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {});
    SmafFile f;
    if (!f.parse(b.data(), b.size()) || f.tracks.size()!=1) return 3;
    auto& t=f.tracks[0];
    if (t.formatType!=2 || t.isAudioTrack || t.durationTimeBase!=16 ||
        t.gateTimeBase!=16 || t.setupData.size()<3) return 4;
    auto& s=t.setupData;
    auto v=parseVoiceExclusive(s.data()+2, s.size()-3);
    if (!v.valid || v.isPcm || v.key.bankMSB || v.key.bankLSB || v.key.pc ||
        v.key.drumNote || v.patch.noteShift || v.patch.panEnabled) return 5;
    MaPlayer p;
    if (!p.init(f,44100) || !p.scoreAudit().scanComplete ||
        p.scoreEndSamples()!=132300) return 6;
    float samples[512];
    uint64_t frames=0, first=~uint64_t(0), last=0;
    double peak=0;
    for (;;) {
        int n=p.render(samples,256);
        if (n<0 || n>256 || frames>44100*5) return 7;
        if (!n) break;
        for (int i=0;i<n*2;i++) {
            if (!std::isfinite(samples[i])) return 8;
            peak=std::max(peak,std::abs(double(samples[i])));
            if (std::abs(samples[i])>1e-7) {
                first=std::min(first,frames+uint64_t(i/2));
                last=frames+uint64_t(i/2);
            }
        }
        frames+=n;
    }
    const auto& d=p.diagnostics();
    if(d.fmNotes!=1 || d.fallbackNotes || d.pcmNotes || d.audioNotes ||
       d.tailLimitReached || d.limitedFrames || first<11025 || first>12000 ||
       last<=first || frames<132300 || frames>176400 || peak<=0 || peak>=1) return 9;
    std::cout << "{\"frames\":" << frames << ",\"first\":" << first
      << ",\"last\":" << last << ",\"peak\":" << peak
      << ",\"algorithm\":" << int(v.patch.algorithm)
      << ",\"lfo\":" << int(v.patch.lfo) << ",\"operators\":[";
    for(int i=0;i<(v.patch.fourOp?4:2);i++) {
        const auto& o=v.patch.ops[i];
        if(i) std::cout << ',';
        std::cout << '[' << int(o.ar) << ',' << int(o.dr) << ',' << int(o.sr)
          << ',' << int(o.rr) << ',' << int(o.sl) << ',' << int(o.tl)
          << ',' << int(o.multi) << ',' << int(o.wave) << ',' << int(o.fb)
          << ',' << int(o.dvb) << ',' << int(o.vib) << ',' << int(o.ksr) << ']';
    }
    std::cout << "]}\n";
}
'''


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='fm-probe-test-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.directory = cls.root / 'probes'
        cls.manifest = GEN.generate(cls.directory)
        cls.native = None
        if OPTIONS.native:
            source = cls.root / 'check.cpp'
            source.write_text(NATIVE_SOURCE, encoding='utf-8')
            library = HERE.parent / 'third_party/yamaha-smaf-player/src'
            cls.native = cls.root / 'check'
            subprocess.run([OPTIONS.cxx, '-std=c++20', '-O2', '-I' + str(library), str(source),
                            *(str(library / f) for f in ('smaf_file.cpp', 'smaf_voice.cpp',
                                                        'ma_fm_core.cpp', 'ma_player.cpp')),
                            '-o', str(cls.native)], check=True)

    def test_existing_twenty_six_bytes_unchanged(self):
        # Preserve all twenty-six validated input files through the r59 waveform study.
        hashes = {
            'sine': '2c7a4838359498ed9672668d973984798452ab8c6036c5252427b16311e1ca3c',
            'fm-2op': 'ec34e57c03ff98bde9d95b6bc6afecba69e2226f2949cac22942b5a2459f9f63',
            'fm-4op': '795b7373e5df22872ef2d739469eb1ee6fc59fc2fad2bfece6a7010955c27845',
            'waveform-1': '17bad615e91595c7f11c9215d75ccdeadaf7b18ebc0b1669c3f1070dfc18079f',
            'envelope': 'c6d839c74a8bfc3b14b5d676f7febcdc8c4cb7dace8e64b60c323aafd889fb5c',
            'feedback': '198210bd546630610e7aba912ce79acae118f47ba3d16e661db8e0c1c7ddd6a3',
            'vibrato-slow': '92820e40d209d8f7673fb1283cc68d939c6b7d999b87c7836f21ef60cb8db65c',
            'vibrato-fast': '96c420ada5e1e7a1e9036b86aa7c482e9f87a683c05c7ecf83057db596a97aec',
            'pitch-bend': '4923c7c93d4d7f90e173563fe7732e2cffe6cab39d3aa3ca920c56eda8f41a0b',
            'waveform-square': 'e8866e07bcc05710c6c0b2ec494da4de0453518460f1fb763eea1fb4d684c8ed',
            'waveform-triangle': '954e5ad6e35390da4d542d6f2c2313f32123029da743312ee8aabe610620aa05',
            'waveform-saw': '2f76f59108958ef9a15e8aa327f60d7e04fd848700897a26b077486d5f50d71a',
            'ksr-bend-before-note': 'f3342ba5442c1d972bbf8ae3790ae76003d22a14c7a9445497acb3525ec57419',
            'ksr-bend-after-note': 'db3faa57e97b9d51d9cbfdce7a424b1cf27052b395dcc7e0d0c843635a8fdbae',
            'waveform-19': '73797a3b71e940b0ce08597adaeeb297f8d501d9f6c7ef9ee97497f49e7ee89c',
            'waveform-22': '916e46a5d687dcdab2e0552ffc8dea1f2d03718c74112cb7925bd9d8bde3529b',
            'waveform-26': '697c2b3a88b03b420b5811ac49a8e79f6d285aa3411d76529d7643c3322f8387',
            'waveform-27': '9849867e7c2cf3ec0957b09ea0edf7c6249aaa018cc01f034876c7ef32ab9598',
            'waveform-29': '5f44ce12d0b6410860b8d167d32d710870b6bf1bfe160118932483563ecf9ec0',
        }
        cases = {probe.name: probe for probe in GEN.CASES}
        hashes.update({
            'waveform-7': '49e0f85e9c776fed5362a563fb2956b11c2b9937dcc618b5ef7e40231a628d3f',
            'waveform-8': '41eca5f76e8c756468a65896421f4e9bd6e570bedc4ee3894d4d7dc3c3571490',
            'waveform-9': '047f6725d199223129e8ffb4d3adacc5cc5aa3e82d7acf72875595ed2f107b1c',
            'waveform-10': '5b5b16cc9a4491930e25b5981a6dae84983a03efbcdb62156b502314245c0582',
            'waveform-11': '3355b5c3643e1e84baa961f3e0d848ff252bef08dada8e71728b3c8def234664',
            'waveform-12': 'e4bd7ce3f7ddc783602816d35f650c655332220126ad744d65885c4a0c17feb3',
            'waveform-13': '19142724ebcaa0549db0c2bed8f0186763b14525ee1176e54c62c2ac55d5fc7b',
        })
        self.assertEqual(len(hashes), 26)
        self.assertEqual(len(GEN.CASES), 34)
        self.assertEqual(len(cases), 34)
        for code in (*range(7, 14), 19, 22, 26, 27, 29):
            probe = cases[f'waveform-{code}']
            self.assertEqual(probe.algorithm, 0)
            self.assertEqual(probe.operators, (GEN.OFF, GEN.replace(GEN.CARRIER, ws=code)))
        for name, expected in hashes.items():
            self.assertEqual(hashlib.sha256(GEN.encode(cases[name])).hexdigest(), expected)

    def test_ksr_same_tick_pair(self):
        before, after = GEN.CASES[-2:]
        self.assertEqual(GEN.voice(before), GEN.voice(after))
        self.assertEqual(before.operators, after.operators)
        self.assertTrue(before.operators[1].ksr)
        self.assertEqual(before.operators[1].encode()[0], 1)
        # Current FmVoice::noteOn uses C0 as its octave reference. This is
        # coverage of the configured offset boundary, not a hardware oracle.
        def current_ksr_offset(note, bend):
            frequency = 440.0 * 2.0 ** ((note - 69 + 2 * bend / 8192.0) / 12)
            octave = math.log2(max(frequency, 1.0) / 16.3515978313)
            block = min(7, max(0, math.floor(octave)))
            high = int(octave - math.floor(octave) >= 0.5)
            return block * 2 + high
        self.assertEqual(before.note, 65)
        self.assertEqual(after.note, 65)
        self.assertEqual(current_ksr_offset(before.note, 0), 8)
        self.assertEqual(current_ksr_offset(before.note, 8191), 9)
        a, b = GEN.events(before), GEN.events(after)
        indices = [i for i, event in enumerate(a) if event[0] == 25]
        self.assertEqual(len(indices), 2)
        i, j = indices
        self.assertEqual(a[i], (25, bytes.fromhex('e0 7f 7f')))
        self.assertEqual(a[j], (25, bytes.fromhex('90 41 64 81 16')))
        b[i], b[j] = b[j], b[i]
        self.assertEqual(a, b)
        # Equal timestamp: neither ordering advances the sample clock before
        # both events run. Only NoteOn's envelope KSR initialization may differ.
        self.assertEqual([tick for tick, raw in a if raw[0] == 0xe0], [0, 25])

    def test_lengths_crc_and_setup(self):
        for probe in GEN.CASES:
            with self.subTest(probe=probe.name):
                data = (self.directory / (probe.name + '.mmf')).read_bytes()
                self.assertTrue(check_crc(data))
                broken = bytearray(data)
                broken[20] ^= 1
                self.assertFalse(check_crc(broken))
                outer = chunks(data)
                self.assertEqual(len(outer), 1)
                self.assertEqual(outer[0][0], b'MMMD')
                children = chunks(outer[0][1][:-2])
                self.assertEqual([x[0] for x in children], [b'CNTI', b'MTR\0'])
                track = children[1][1]
                self.assertEqual(track[:5], bytes((2, 0, 16, 16, 129)))
                parts = chunks(track[20:])
                self.assertEqual([x[0] for x in parts], [b'Mtsu', b'Mtsq'])
                setup = parts[0][1]
                self.assertEqual(setup[0], 240)
                self.assertEqual(setup[-1], 247)
                self.assertEqual(setup[1], len(setup) - 2)
                self.assertEqual(setup[2:12], bytes.fromhex('43 79 07 7f 01 00 00 00 00 00'))

    def test_reproducible_and_no_overwrite(self):
        other = self.root / 'repeat'
        GEN.generate(other)
        for path in self.directory.iterdir():
            self.assertEqual(path.read_bytes(), (other / path.name).read_bytes())
        before = (self.directory / 'sine.mmf').read_bytes()
        with self.assertRaises(FileExistsError):
            GEN.generate(self.directory)
        self.assertEqual(before, (self.directory / 'sine.mmf').read_bytes())

    def test_variable_integer_boundaries(self):
        for value, encoded in ((0,'00'),(127,'7f'),(128,'8100'),(150,'8116'),
                               (275,'8213'),(16383,'ff7f'),(16384,'818000'),
                               (2097151,'ffff7f')):
            self.assertEqual(GEN.vlq(value).hex(), encoded)
        for value in (-1, 2097152):
            with self.assertRaises(ValueError):
                GEN.vlq(value)

    def test_score_timing_and_controls(self):
        for probe in GEN.CASES:
            with self.subTest(probe=probe.name):
                outer = chunks(GEN.encode(probe))[0][1]
                track = chunks(outer[:-2])[1][1]
                sequence = chunks(track[20:])[1][1]
                pos, tick, notes, bends, controls = 0, 0, [], [], []

                def read_vlq():
                    nonlocal pos
                    value = 0
                    for _ in range(3):
                        byte = sequence[pos]
                        pos += 1
                        value = value * 128 + (byte & 127)
                        if byte < 128:
                            return value
                    raise ValueError('unterminated VLQ')

                while pos < len(sequence):
                    tick += read_vlq()
                    status = sequence[pos]
                    pos += 1
                    if status == 0xff:
                        self.assertEqual(sequence[pos:], b'\x2f\x00')
                        pos += 2
                        break
                    if status == 0xc0:
                        self.assertEqual(sequence[pos], 0)
                        pos += 1
                        continue
                    a, b = sequence[pos:pos + 2]
                    pos += 2
                    if status == 0x90:
                        notes.append((tick, a, b, read_vlq()))
                    elif status == 0xe0:
                        bends.append((tick, a + b * 128 - 8192))
                    elif status == 0xb0:
                        controls.append((tick, a, b))
                    else:
                        self.fail(f'unexpected status {status:02x}')
                self.assertEqual(pos, len(sequence))
                self.assertEqual(tick, 300)
                self.assertEqual(notes, [(25, probe.note, 100, 150)])
                self.assertEqual(bends, [(0, 0)] + ([(75, 4096), (125, -4096), (200, 0)]
                                                   if probe.bend else [(25, 8191)] if probe.ksr_bend_order else []))
                self.assertEqual(controls[:6], [(0, a, b) for a, b in
                                 ((0,0),(32,0),(7,100),(11,127),(10,64),(64,0))])
                self.assertEqual(controls[6:], [(0, a, b) for a, b in
                                 ((101,0),(100,0),(6,2),(38,0),(101,127),(100,127))]
                                 if probe.bend or probe.ksr_bend_order else [])

    def test_native_parser_voice_binding_and_render(self):
        if not self.native:
            self.skipTest('pass --native to compile project parser/render checks')
        for probe in GEN.CASES:
            with self.subTest(probe=probe.name):
                path = self.directory / (probe.name + '.mmf')
                run = subprocess.run([str(self.native), str(path)], check=True,
                                     capture_output=True, text=True)
                report = json.loads(run.stdout)
                self.assertEqual(report['algorithm'], probe.algorithm)
                self.assertEqual(report['lfo'], probe.lfo)
                self.assertEqual(report['operators'], [[int(v) for v in asdict(op).values()]
                                                       for op in probe.operators])

    def test_independent_go_smaf(self):
        if not OPTIONS.oracle:
            self.skipTest('pass --oracle to validate with independent go-smaf parser')
        for probe in GEN.CASES:
            with self.subTest(probe=probe.name):
                path = self.directory / (probe.name + '.mmf')
                run = subprocess.run([str(OPTIONS.oracle), str(path)], check=True,
                                     capture_output=True, text=True)
                report = json.loads(run.stdout)
                self.assertTrue(report['crc_valid'])
                file = report['file']
                self.assertEqual(file['signature'], 'MMMD')
                track = file['sub_chunks'][1]
                self.assertEqual(track['format_type'], 'MobileStandardNonCompressed')
                self.assertEqual(track['duration_time_base'], 10)
                self.assertEqual(track['gate_time_base'], 10)
                self.assertEqual(track['channel_status']['0']['key_control_status'], 'On')
                self.assertEqual(track['channel_status']['0']['channel_type'], 'Melody')
                setup, sequence = track['sub_chunks']
                self.assertEqual(len(setup['exclusives']), 1)
                vpc = setup['exclusives'][0]['vm35_voice_pc']
                for name in ('bank_msb', 'bank_lsb', 'pc', 'drum_note'):
                    self.assertEqual(vpc[name], 0)
                voice = vpc['voice']
                self.assertEqual(voice['alg'], probe.algorithm)
                self.assertEqual(voice['lfo'], probe.lfo)
                self.assertEqual(voice['bo'], 1)
                self.assertFalse(voice['pe'])
                for expected, actual in zip(probe.operators, voice['operators']):
                    for key, value in asdict(expected).items():
                        self.assertEqual(actual[key], value, key)
                # go-smaf omits EOS from its event list; inspect all preceding
                # events and exact accumulated deltas, including pitch bends.
                actual_events = sequence['events']
                self.assertEqual(len(actual_events), len(GEN.events(probe)) - 1)
                tick = 0
                for actual, (expected_tick, raw) in zip(actual_events, GEN.events(probe)):
                    tick += actual['duration']
                    self.assertEqual(tick, expected_tick)
                    event = actual['event']
                    self.assertEqual(event['channel'], 0)
                    if raw[0] == 0xb0:
                        self.assertEqual(event['cc'], raw[1])
                        self.assertEqual(event['value'], raw[2])
                    elif raw[0] == 0xc0:
                        self.assertEqual(event['pc'], 0)
                    elif raw[0] == 0xe0:
                        self.assertEqual(event['value'], raw[1] + (raw[2] << 7) - 8192)
                    elif raw[0] == 0x90:
                        self.assertEqual(event['note'], probe.note)
                        self.assertEqual(event['velocity'], 100)
                        self.assertEqual(event['gate_time'], 150)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, help='go-smaf JSON/CRC wrapper described in docs/fm-probes.md')
    parser.add_argument('--native', action='store_true', help='compile and run project parser/render checks')
    parser.add_argument('--cxx', default=shutil.which('g++') or 'c++')
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
