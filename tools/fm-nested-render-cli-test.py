#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original short MMF through actual fixed nested source/player/PCM16 WAV."""
import argparse
from array import array
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

parser=argparse.ArgumentParser()
parser.add_argument('--binary',required=True,type=Path)
parser.add_argument('--audit',required=True,type=Path)
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('nested_probes',Path(__file__).with_name('generate-fm-probes.py'))
m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m)
m.events=lambda p:[(0,bytes((0xb0,cc,v))) for cc,v in [(0,0),(32,0),(7,127),(11,127),(10,0),(64,0)]]+[
    (0,b'\xc0\x00'),(0,bytes((0x90,p.note,127))+m.vlq(8)),(3,b'\xb0\x07\x00'),
    (4,b'\xb0\x07\x7f'),(6,b'\xe0\x00\x60'),(12,b'\xb0\x78\x00'),(16,b'\xff\x2f\x00')]
probe=m.Probe('nested-cli','Original short nested sine study.',6,tuple(m.Operator(multi=multi,tl=tl) for multi,tl in [(1,18),(3,0),(2,0),(5,0)]),note=96)
binary=str(args.binary.resolve());audit=str(args.audit.resolve())
with tempfile.TemporaryDirectory(prefix='nested-cli-') as directory:
    root=Path(directory);source=root/'original.mmf';data=m.encode(probe);source.write_bytes(data);outputs=[]
    for factor in [16,32,64,128]:
        pcm=[]
        for math in ['exact','hermite']:
            target=root/f'{factor}-{math}.wav';flags=[f'--experimental-sine-factor={factor}',f'--experimental-sine-math={math}','--experimental-nested-sine']
            r=subprocess.run([binary,str(source),str(target),'2',*flags],capture_output=True);assert r.returncode==0,r.stderr
            with wave.open(str(target)) as w:
                assert w.getparams()[:3]==(2,2,44100) and 0<w.getnframes()<44100
                samples=array('h');samples.frombytes(w.readframes(w.getnframes()));pcm.append(samples)
            assert any(pcm[-1]),'synthetic nested source unexpectedly silent'
            if math=='hermite':
                reordered=root/f'{factor}-reordered.wav'
                r=subprocess.run([binary,str(source),str(reordered),'--experimental-nested-sine',flags[1],flags[0]],capture_output=True)
                assert r.returncode==0 and reordered.read_bytes()==target.read_bytes()
        assert len(pcm[0])==len(pcm[1]) and max(abs(a-b) for a,b in zip(*pcm))<=1
        outputs.append(pcm[1].tobytes())
        r=subprocess.run([audit,str(source),'1',f'--experimental-sine-factor={factor}','--experimental-sine-math=hermite','--experimental-nested-sine'],capture_output=True,text=True)
        assert r.returncode==0,r.stderr
        report=json.loads(r.stdout);assert report['nested_sine'] and report['reference_pcm_exact'] and not report['cap_reached']
        parts=[p for p in report['parts'] if p['sine_source_voice_frames']]
        assert parts and all(p['sine_voice_frames_by_factor'][str(factor)]==p['sine_source_voice_frames'] for p in parts)
    assert len(set(outputs))>=3,'factors did not reach actual nested synthesis'
    for flags in [['--experimental-sine-factor=64'],['--experimental-nested-sine'],['--experimental-sine-factor=8','--experimental-nested-sine'],['--experimental-sine-factor=256','--experimental-nested-sine'],['--experimental-sine-factor=64','--experimental-nested-sine','--experimental-nested-sine']]:
        target=root/'invalid.wav';r=subprocess.run([binary,str(source),str(target),*flags],capture_output=True)
        assert r.returncode!=0 and not target.exists()
    for flags in [['--app-quality','--experimental-nested-sine'],['--experimental-nested-sine','--app-quality'],['--experimental-sine-factor=64']]:
        assert subprocess.run([audit,str(source),*flags],capture_output=True).returncode!=0
    assert source.read_bytes()==data
    for note,inner,middle,factor in [(60,12,12,16),(72,12,12,32),(84,12,12,64),(96,12,12,128),
                                      (91,12,24,64),(91,24,12,32),(94,20,20,32),(96,16,16,64)]:
        adaptive_probe=m.Probe('adaptive-cli','Original bounded nested density study.',6,
            tuple(m.Operator(multi=multi,tl=tl) for multi,tl in [(1,18),(7,inner),(3,middle),(5,0)]),note=note)
        adaptive_data=m.encode(adaptive_probe);source.write_bytes(adaptive_data);selected=root/f'adaptive-{note}.wav';fixed=root/f'fixed-{note}.wav'
        r=subprocess.run([binary,str(source),str(selected),'2','--experimental-adaptive-nested'],capture_output=True);assert r.returncode==0,r.stderr
        r=subprocess.run([binary,str(source),str(fixed),'2','--experimental-nested-sine',f'--experimental-sine-factor={factor}','--experimental-sine-math=hermite'],capture_output=True);assert r.returncode==0,r.stderr
        assert selected.read_bytes()==fixed.read_bytes(),('adaptive fixed-policy mismatch',note,factor)
        r=subprocess.run([audit,str(source),'1','--experimental-adaptive-nested'],capture_output=True,text=True);assert r.returncode==0,r.stderr
        report=json.loads(r.stdout);assert report['adaptive_nested'] and report['reference_pcm_exact'] and not report['cap_reached']
        parts=[p for p in report['parts'] if p['sine_source_voice_frames']]
        assert parts and all(p['sine_voice_frames_by_factor'][str(factor)]==p['sine_source_voice_frames'] for p in parts)
        assert source.read_bytes()==adaptive_data
    for note,factor in [(60,16),(72,32),(84,64),(96,128)]:
        vibrato_probe=m.Probe('vibrato-cli','Original nested vibrato density study.',6,
            tuple(m.Operator(multi=multi,tl=tl,evb=True,dvb=3) for multi,tl in [(1,18),(7,12),(3,12),(5,0)]),note=note,lfo=3)
        vibrato_data=m.encode(vibrato_probe);source.write_bytes(vibrato_data);selected=root/f'vibrato-{note}.wav';fixed=root/f'vibrato-fixed-{note}.wav'
        r=subprocess.run([binary,str(source),str(selected),'2','--experimental-adaptive-nested'],capture_output=True);assert r.returncode==0,r.stderr
        r=subprocess.run([binary,str(source),str(fixed),'2','--experimental-nested-sine',f'--experimental-sine-factor={factor}','--experimental-sine-math=hermite'],capture_output=True);assert r.returncode==0,r.stderr
        assert selected.read_bytes()==fixed.read_bytes(),('vibrato adaptive fixed-policy mismatch',note,factor)
        r=subprocess.run([audit,str(source),'1','--experimental-adaptive-nested'],capture_output=True,text=True);assert r.returncode==0,r.stderr
        report=json.loads(r.stdout);assert report['adaptive_nested'] and report['reference_pcm_exact'] and not report['cap_reached']
        parts=[p for p in report['parts'] if p['sine_source_voice_frames']]
        assert parts and all(p['sine_voice_frames_by_factor'][str(factor)]==p['sine_source_voice_frames'] for p in parts)
        assert source.read_bytes()==vibrato_data
    controller_probe=m.Probe('vibrato-cc1','Original VM35 patch with Format3 CC1 study.',6,
        tuple(m.Operator(multi=multi,tl=tl,evb=True,dvb=3) for multi,tl in [(1,18),(7,12),(3,12),(5,0)]),note=67,lfo=3)
    sequence=bytearray()
    for cc,value in [(0,0),(32,0),(7,127),(11,127),(10,0),(64,0),(1,0)]:
        sequence.extend(bytes((0,0x30,cc,value)))
    sequence.extend(bytes((0,0x40,0,0,0x10,67,127,16,1,0x30,1,127,1,0x30,1,0,1,0x30,1,64,7,0x30,120,0,2,0xff,0x2f,0)))
    track=bytes((3,0,0x10,0x10,0x81))+bytes(31)+m.chunk(b'Mtsu',m.voice(controller_probe))+m.chunk(b'Mtsq',sequence)
    payload=m.chunk(b'CNTI',bytes(5)+b'ST:Original vibrato CC1,')+m.chunk(b'MTR\x00',track)
    controller_data=b'MMMD'+m.struct.pack('>I',len(payload)+2)+payload
    controller_data+=m.struct.pack('>H',m.binascii.crc_hqx(controller_data,0xffff)^0xffff)
    source.write_bytes(controller_data);selected=root/'cc1-adaptive.wav';fixed=root/'cc1-fixed32.wav'
    r=subprocess.run([binary,str(source),str(selected),'2','--experimental-adaptive-nested'],capture_output=True);assert r.returncode==0,r.stderr
    r=subprocess.run([binary,str(source),str(fixed),'2','--experimental-nested-sine','--experimental-sine-factor=32','--experimental-sine-math=hermite'],capture_output=True);assert r.returncode==0,r.stderr
    streams=[]
    for target in [selected,fixed]:
        with wave.open(str(target)) as w:
            pcm=array('h');pcm.frombytes(w.readframes(w.getnframes()));streams.append(pcm)
    assert len(streams[0])==len(streams[1]) and len(streams[0])>2200
    assert max(abs(a-b) for a,b in zip(streams[0][2200:],streams[1][2200:]))<=1,'CC1 bank failed to settle to fixed32 reference'
    r=subprocess.run([audit,str(source),'1','--experimental-adaptive-nested'],capture_output=True,text=True);assert r.returncode==0,r.stderr
    report=json.loads(r.stdout);assert report['reference_pcm_exact'] and not report['cap_reached']
    parts=[p for p in report['parts'] if p['sine_source_voice_frames']];assert len(parts)==1
    counts=parts[0]['sine_voice_frames_by_factor'];assert counts['16']>0 and counts['32']>0 and counts['64']==counts['128']==0
    assert source.read_bytes()==controller_data
    for flag in ['--experimental-sine-factor=16','--experimental-sine-math=exact','--experimental-nested-sine','--experimental-adaptive-nested']:
        for flags in [['--experimental-adaptive-nested',flag],[flag,'--experimental-adaptive-nested']]:
            target=root/'bad-adaptive.wav';r=subprocess.run([binary,str(source),str(target),*flags],capture_output=True)
            assert r.returncode!=0 and not target.exists()
            assert subprocess.run([audit,str(source),*flags],capture_output=True).returncode!=0
    for flags in [['--app-quality','--experimental-adaptive-nested'],['--experimental-adaptive-nested','--app-quality']]:
        assert subprocess.run([audit,str(source),*flags],capture_output=True).returncode!=0
    source.write_bytes(data)
    assert source.read_bytes()==data
print('Nested MMF/WAV CLI PASS:adaptive8cases+vibrato4cases/fixed WAV exact;Format3 CC1 migration/settling + guards; 4 factors, Exact/Hermite PCM16<=1, order, EOF, contribution counters and opt-in guards.')
