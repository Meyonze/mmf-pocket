// SPDX-License-Identifier: Apache-2.0
// Synthetic fixtures only. No copyrighted ringtone data is embedded.
#include "ma_player.h"
#include "smaf_voice.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
float advance(FmEnvelope& env, int count) {
    float value = 0;
    for (int i = 0; i < count; ++i) value = env.advance();
    return value;
}
SmafFile fixture(int format = 2) {
    SmafFile f;
    TrackChunk t;
    t.trackNumber = 0; t.formatType = format;
    t.durationTimeBase = t.gateTimeBase = 0x10;
    t.sequenceData = {0,0x90,60,100,10, 10,0xff,0x2f,0};
    f.tracks.push_back(t);
    return f;
}
float firstBlockPeak(const SmafFile& file) {
    MaPlayer p;
    check(p.init(file, 8000), "peak fixture initializes");
    float samples[512 * 2];
    int count = p.render(samples, 512);
    float peak = 0;
    for (int i = 0; i < count * 2; ++i) peak = std::max(peak, std::fabs(samples[i]));
    return peak;
}

std::vector<float> renderAll(const SmafFile& file, int block = 257) {
    MaPlayer player;
    check(player.init(file, 8000), "comparison fixture initializes");
    std::vector<float> result, buffer(block * 2);
    int n;
    while ((n = player.render(buffer.data(), block)) > 0) {
        result.insert(result.end(), buffer.begin(), buffer.begin() + n * 2);
        check(result.size() <= 8000 * 2 * 600, "comparison terminates");
    }
    check(player.finished(), "finished reflects natural or safety termination");
    return result;
}

void appendExclusive(SmafFile& file, const std::vector<uint8_t>& voice) {
    auto& setup = file.tracks[0].setupData;
    setup.push_back(0xf0);
    uint32_t length = uint32_t(voice.size() + 1);
    std::vector<uint8_t> vlq{uint8_t(length & 127)};
    while ((length >>= 7)) vlq.insert(vlq.begin(), uint8_t(0x80 | (length & 127)));
    setup.insert(setup.end(), vlq.begin(), vlq.end());
    setup.insert(setup.end(), voice.begin(), voice.end());
    setup.push_back(0xf7);
}
void setVoice(SmafFile& file, const std::vector<uint8_t>& voice) {
    file.tracks[0].setupData.clear();
    appendExclusive(file,voice);
}
std::vector<uint8_t> packMa3(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    for (size_t i=0;i<raw.size();) {
        size_t mask = out.size(); out.push_back(0);
        for (int b=6;b>=0 && i<raw.size();--b,++i) {
            out[mask] |= uint8_t((raw[i] >> 7) << b);
            out.push_back(raw[i] & 127);
        }
    }
    return out;
}

void addPcmFixture(SmafFile& file, int waveId = 1, uint8_t rr = 9) {
    // Synthetic looping PCM, 8 kHz, neutral envelope, RR=9, WaveID=1.
    setVoice(file, {0x43,0x79,0x07,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,uint8_t(rr<<4),0xf0,0,0,0,0,0,0,0,80,1,0,0,0});
    WaveData wave;
    wave.number = waveId; wave.adpcm = false; wave.bitsPerSample = 16;
    wave.samplingRate = 8000;
    for (int i = 0; i < 80; ++i) {
        uint16_t sample = uint16_t(int16_t(4000 * std::sin(i * 6.283185307179586 / 40)));
        wave.data.push_back(uint8_t(sample >> 8)); wave.data.push_back(uint8_t(sample));
    }
    file.tracks[0].waves.push_back(wave);
}

void checkPackedPcm() {
    // Independent small known packed vector: high bits at first/last positions.
    const uint8_t encoded[] = {0x41,0,1,2,3,4,5,127};
    check(unpackMa3Bytes(encoded,8,7)==std::vector<uint8_t>({128,1,2,3,4,5,255}), "MA3 high-bit transport");
    check(unpackMa3Bytes(encoded,8,6).empty(), "MA3 output allocation bound");
    const uint8_t malformed[] = {0,128};
    check(unpackMa3Bytes(malformed,2,7).empty(), "MA3 rejects non-seven-bit data");
    check(unpackMa3Bytes(encoded,1,7).empty(), "MA3 rejects trailing mask");

    auto direct = fixture(), packed = fixture();
    std::vector<uint8_t> pcm = {0x43,0x79,7,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,1};
    setVoice(direct,pcm);
    std::vector<uint8_t> sysex(pcm.begin(),pcm.begin()+10);
    sysex[2]=6;
    auto body = packMa3({pcm.begin()+10,pcm.end()});
    sysex.insert(sysex.end(),body.begin(),body.end());
    const auto parsed = parseVoiceExclusive(sysex.data(),sysex.size());
    check(parsed.valid && parsed.isPcm && parsed.pcm.fs==8000 && parsed.pcm.env.rr==9 &&
          parsed.pcm.env.ar==15 && parsed.pcm.waveId==1 && !parsed.pcm.rom && parsed.pcm.loop,
          "packed PCM Fs/envelope/WaveID/ROM/loop fields");
    setVoice(packed,sysex);
    WaveData wave; wave.number=1; wave.samplingRate=12000; // Fs MUST come from voice (8000)
    wave.data.assign(40,0x81); // only synthesized codec bytes
    direct.tracks[0].waves.push_back(wave);
    std::vector<uint8_t> bulk{0x43,0x79,6,0x7f,3,1,0};
    auto data = packMa3(wave.data);
    bulk.insert(bulk.end(),data.begin(),data.end());
    appendExclusive(packed,bulk);
    check(renderAll(direct)==renderAll(packed), "bulk voice wave equals embedded ADPCM with authored Fs");
    MaPlayer player; check(player.init(packed,8000), "bulk fixture initializes");
    float samples[512*2]; player.render(samples,512);
    check(player.diagnostics().pcmNotes==1 && player.diagnostics().fallbackNotes==0,
          "bulk wave actually renders PCM, never substitutes FM");
    pcm[25] |= 0x80; // same ID, but ROM selected
    setVoice(direct,pcm);
    check(player.init(direct,8000), "ROM collision fixture initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0 && player.diagnostics().fallbackNotes==1,
          "ROM selector never aliases a RAM wave with same ID");
    // Unrecognized bulk codec is ignored, not decoded into noise.
    setVoice(packed,sysex); bulk[6]=3; appendExclusive(packed,bulk);
    check(player.init(packed,8000), "unknown bulk codec fixture initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0, "unknown bulk codec not guessed");
}

void checkPcmControllers() {
    for (int cc : {7,11}) {
        auto f=fixture(); addPcmFixture(f);
        f.tracks[0].sequenceData={0,0x90,60,100,40, 10,0xb0,uint8_t(cc),0, 30,0xff,0x2f,0};
        auto samples=renderAll(f);
        for(size_t i=1000*2;i<samples.size();++i)
            check(std::fabs(samples[i])<1e-7, "held PCM follows volume/expression changes");
    }
    auto panned=fixture(); addPcmFixture(panned);
    panned.tracks[0].sequenceData={0,0x90,60,100,40, 10,0xb0,10,0, 30,0xff,0x2f,0};
    auto samples=renderAll(panned);
    float leftPeak=0;
    for(size_t i=1000;i<2000;++i) {
        leftPeak=std::max(leftPeak,std::fabs(samples[i*2]));
        check(std::fabs(samples[i*2+1])<1e-7, "held PCM follows pan changes");
    }
    check(leftPeak>0.001f,"panned PCM remains audible");
    auto bendBefore=fixture(),bendDuring=fixture(); addPcmFixture(bendBefore); addPcmFixture(bendDuring);
    bendBefore.tracks[0].sequenceData={0,0xe0,0,96, 0,0x90,60,100,40, 40,0xff,0x2f,0};
    bendDuring.tracks[0].sequenceData={0,0x90,60,100,40, 10,0xe0,0,96, 30,0xff,0x2f,0};
    auto unbent=fixture(); addPcmFixture(unbent);
    unbent.tracks[0].sequenceData={0,0x90,60,100,40,40,0xff,0x2f,0};
    check(renderAll(bendBefore)!=renderAll(unbent), "PCM applies initial pitch bend");
    check(renderAll(bendDuring)!=renderAll(unbent), "PCM applies live pitch bend");
}

void checkGateIdentity() {
    // The younger note ends BEFORE the older one. A key-only/FIFO match is
    // insufficient; each gate must release its own note instance, including
    // while an older instance is still releasing.
    auto same = fixture(), separate = fixture();
    same.tracks[0].sequenceData = {
        0,0x90,60,60,40, 5,0x90,60,100,10, 15,0x90,60,80,10,
        20,0xff,0x2f,0};
    separate.tracks[0].sequenceData = same.tracks[0].sequenceData;
    separate.tracks[0].sequenceData[6] = 0x91;
    separate.tracks[0].sequenceData[11] = 0x92;
    for (bool pcm : {false, true}) {
        if (pcm) { addPcmFixture(same); addPcmFixture(separate); }
        const auto actual = renderAll(same), expected = renderAll(separate);
        check(actual.size() == expected.size(), "same-key gates retain independent release duration");
        for (size_t i = 0; i < actual.size(); ++i)
            check(std::fabs(actual[i] - expected[i]) < 1e-6,
                  "same-key FM/PCM gates match independent-channel rendering");
        check(renderAll(same,1) == actual, "render independent of buffer size");
    }
}

void checkAlgorithms() {
    // Independently assemble algorithm 3: op0 + (op1 -> op2) feeds op3.
    FmVoicePatch patch;
    patch.fourOp = true; patch.algorithm = 3;
    FmVoice voice;
    voice.setSampleRate(8000); voice.noteOn(patch, 220, 1);
    FmOperator ops[4];
    for (auto& op : ops) { op.configure(FmOpPatch{},8000,1); op.noteOn(220); }
    for (int i = 0; i < 100; ++i) {
        const float a = ops[0].tick(0), b = ops[1].tick(0);
        const float c = ops[2].tick(double(b) * 0.42);
        const float expected = float(ops[3].tick((double(a) + c) * 0.42) * 0.7);
        check(std::fabs(voice.tick() - expected) < 1e-6, "algorithm 3 routing");
    }
    // A held/XOF modulator cannot keep a silent voice occupying the pool once
    // every output carrier has finished. Test each topology's carrier set.
    constexpr unsigned carriers[8] = {2,3,15,8,8,10,9,13};
    for (int algo = 0; algo < 8; ++algo) {
        patch.algorithm = uint8_t(algo); patch.fourOp = algo >= 2;
        for (int op = 0; op < 4; ++op) {
            patch.ops[op] = FmOpPatch{};
            patch.ops[op].rr = 15;
            patch.ops[op].xof = !(carriers[algo] & (1u << op));
        }
        voice.noteOn(patch,220,1);
        for (int i = 0; i < 100; ++i) voice.tick();
        voice.noteOff();
        for (int i = 0; i < 8000; ++i) voice.tick();
        check(!voice.active(), "finished carriers retire voice despite held modulators");
    }
}

void checkVoiceStealing() {
    auto f = fixture();
    // The first note remains held while short notes fill the pool with tails.
    setVoice(f, {0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0, 0,0x20,0xf0,0xfc,0,0x10,0, 0,0x20,0xf0,0,0,0x10,0});
    auto& seq = f.tracks[0].sequenceData;
    seq = {0,0x90,60,100,120};
    for (int i = 0; i < 50; ++i) seq.insert(seq.end(), {1,0x91,64,40,1});
    seq.insert(seq.end(), {1,0xff,0x2f,0});
    MaPlayer player;
    check(player.init(f,8000), "stealing fixture initializes");
    std::vector<float> reference, buffer(512*2);
    int n;
    while ((n=player.render(buffer.data(),512))>0)
        reference.insert(reference.end(), buffer.begin(), buffer.begin()+n*2);
    check(player.diagnostics().stolenFm>0, "fixture exercises saturated FM pool");
    check(player.diagnostics().stolenHeldFm==0, "release tails stolen before held melody");
    player.seekToStart();
    size_t pos=0;
    while ((n=player.render(buffer.data(),512))>0) {
        check(pos+n*2<=reference.size(), "seek replay length");
        for (int i=0;i<n*2;++i) check(buffer[i]==reference[pos++], "seek stealing is deterministic");
    }
    check(pos==reference.size(), "seek replay complete");
    addPcmFixture(f,1,2);
    check(player.init(f,8000), "PCM stealing fixture initializes");
    while(player.render(buffer.data(),512)>0) {}
    check(player.diagnostics().stolenPcm>0, "fixture exercises saturated PCM pool");
    check(player.diagnostics().stolenHeldPcm==0, "PCM release tails stolen before held melody");
}

int main() {
    checkGateIdentity();
    checkAlgorithms();
    checkVoiceStealing();
    checkPackedPcm();
    checkPcmControllers();
    FmOpPatch p;
    p.ar=15; p.dr=12; p.sl=4; p.sr=0; p.rr=12;
    FmEnvelope e;
    e.configure(p,44100); e.keyOn();
    float held = advance(e,44100);
    check(std::fabs(held - std::pow(10.0,-12.0/20.0)) < 1e-5, "SL attenuation");
    check(advance(e,44100*4) == held, "SR zero holds exactly");
    e.keyOff(); advance(e,44100);
    check(e.isFinished(), "normal keyoff releases");

    p.ar=0; e.configure(p,44100,15); e.keyOn();
    check(advance(e,44100)==0, "AR zero does not become instant attack under KSR");
    p.ar=15; p.dr=0; e.configure(p,44100); e.keyOn();
    check(advance(e,44100)==1, "DR zero freezes decay");
    p.dr=12; p.xof=true; e.configure(p,44100); e.keyOn();
    held=advance(e,44100); e.keyOff();
    check(advance(e,44100)==held, "XOF ignores keyoff even at SR zero");

    p.xof=false; p.dr=0; p.rr=8; e.configure(p,44100); e.keyOn();
    advance(e,10); e.keyOff();
    float a=advance(e,1000), b=advance(e,1000), c=advance(e,1000);
    check(a>b && b>c && std::fabs(b/a-c/b)<1e-5, "release is exponential");

    // MA-5 direct voice with AR=15 and SR=0 must hold, not select RR early.
    std::vector<uint8_t> voice={0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0, 0,0xc8,0xf4,0,0,0x10,0, 0,0xc8,0xf4,0,0,0x10,0};
    auto v=parseVoiceExclusive(voice.data(),voice.size());
    check(v.valid && v.patch.ops[0].egType && v.patch.ops[0].sr==0,
          "VM35 zero sustain rate preserved");

    FmOperator op;
    p=FmOpPatch{}; p.dt=0; p.dr=0;
    op.configure(p,44100); op.noteOn(441);
    check(std::fabs(op.tick(0)-std::sin(2*3.141592653589793/100))<1e-6,
          "DT zero does not detune");
    p.dt=4; op.configure(p,44100); op.noteOn(441);
    check(std::fabs(op.tick(0)-std::sin(2*3.141592653589793/100))<1e-6,
          "DT negative zero also neutral");

    check(TrackChunk::timeBaseMs(0x10)==10 && TrackChunk::timeBaseMs(0x13)==50,
          "slow timebase range");
    MaPlayer player;
    auto f=fixture();
    check(player.init(f,8000), "synthetic score initializes");
    check(player.scoreEndSamples()==800, "gate and duration use 10ms ticks");
    check(player.totalSamples()==80800, "bounded ten-second release allowance");
    float buffer[512*2];
    int total=0, n;
    while((n=player.render(buffer,512))>0) {
        total+=n;
        check(total<=80800, "render ends within declared tail backstop");
        for(int i=0;i<n*2;++i) check(std::isfinite(buffer[i]), "finite output");
    }
    check(player.render(buffer,512)==0, "repeated render stays ended");
    check(player.finished(), "finished after natural end");
    player.seekToStart();
    check(!player.finished(), "seek resets finished flag");
    check(player.render(buffer,512)>0, "seek restarts");

    // Missing ROM waves use an independently voiced FM approximation. Its
    // levels are not calibrated against ROM PCM: do not apply a second,
    // unverified TL correction to the substitute (beta.6 regression).
    SmafFile plainFallback = fixture();
    SmafFile quietFallback = fixture();
    std::vector<uint8_t> pcmVoice = {
        0x43,0x79,0x07,0x7f,0x01, 0,0,0,0,1,
        0x1f,0x40,0,0,0,0,0, uint8_t(28 << 2), 0,0,0,0,0,0,0,0
    };
    quietFallback.tracks[0].setupData = {0xf0, uint8_t(pcmVoice.size() + 1)};
    quietFallback.tracks[0].setupData.insert(quietFallback.tracks[0].setupData.end(),
                                             pcmVoice.begin(), pcmVoice.end());
    quietFallback.tracks[0].setupData.push_back(0xf7);
    float plainPeak = firstBlockPeak(plainFallback);
    float quietPeak = firstBlockPeak(quietFallback);
    check(std::fabs(quietPeak - plainPeak) < 1e-6, "no uncalibrated ROM-to-FM level transfer");
    pcmVoice[9] = 2;
    auto unknown = parseVoiceExclusive(pcmVoice.data(), pcmVoice.size());
    check(!unknown.valid && !unknown.isPcm, "Analog Lite is not misparsed as PCM");

    auto missingWave = fixture();
    addPcmFixture(missingWave,2); // patch asks for 1, only unrelated 2 exists
    check(renderAll(missingWave) == renderAll(plainFallback), "missing wave never plays unrelated sample");

    auto quietPcm = fixture(), loudPcm = fixture();
    addPcmFixture(quietPcm); addPcmFixture(loudPcm);
    quietPcm.tracks[0].sequenceData[3] = 50;
    check(std::fabs(firstBlockPeak(quietPcm) / firstBlockPeak(loudPcm) - 0.25f) < 1e-5,
          "embedded PCM retains squared velocity");
    auto muted = fixture();
    muted.tracks[0].sequenceData = {0,0x90,60,0,10, 10,0x80,60,10, 10,0xff,0x2f,0};
    for (float sample : renderAll(muted)) check(sample == 0, "explicit and running zero velocity are silent");

    // Long natural release survives the previous arbitrary +1-second cutoff.
    auto longRelease = fixture();
    std::vector<uint8_t> longVoice = {0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0, 8,0x50,0xf0,0xfc,0,0x10,0, 0,0x50,0xf0,0,0,0x10,0};
    setVoice(longRelease,longVoice);
    auto releaseAudio = renderAll(longRelease);
    check(releaseAudio.size() > 8800*2 && releaseAudio.size() < 80800*2,
          "natural release, not a one-second cutoff or full safety tail");
    check(std::fabs(releaseAudio.back()) < 1e-7, "natural ending drains output filter");
    longVoice[13] = 8; longVoice[20] = 8; // XOF with SR=0 on both ops: never ends
    setVoice(longRelease,longVoice);
    releaseAudio = renderAll(longRelease);
    check(releaseAudio.size() == 80800*2, "nondecaying patch reaches bounded safety tail");
    check(releaseAudio.back() == 0 && releaseAudio[releaseAudio.size()-2] == 0,
          "safety boundary fades to zero without abrupt cutoff");

    check(!player.init(fixture(3),8000), "MA7 rejected by core");
    check(!player.init(fixture(4),8000), "unknown format rejected by core");

    // A long-gated event cannot keep conversion alive indefinitely.
    f.tracks[0].sequenceData={0,0x90,60,100,0xff,0xff,0x7f,0,0xff,0x2f,0};
    check(player.init(f,10), "long gate fixture initializes");
    total=0;
    while((n=player.render(buffer,512))>0) { total+=n; check(total<=6000,"hard cap"); }
    check(player.render(buffer,512)==0,"hard cap remains ended");
    check(player.finished(),"finished at hard cap even with active envelopes");
    std::cout << "Audio regression checks passed\n";
}
