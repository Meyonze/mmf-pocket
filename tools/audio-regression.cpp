// SPDX-License-Identifier: Apache-2.0
// Synthetic fixtures only. No copyrighted ringtone data is embedded.
#include "ma_player.h"
#include "smaf_voice.h"
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
int main() {
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
    check(player.totalSamples()==8800, "gate and duration use 10ms ticks");
    float buffer[512*2];
    int total=0, n;
    while((n=player.render(buffer,512))>0) {
        total+=n;
        check(total<=16801, "render ends within tail backstop");
        for(int i=0;i<n*2;++i) check(std::isfinite(buffer[i]), "finite output");
    }
    check(player.render(buffer,512)==0, "repeated render stays ended");
    player.seekToStart();
    check(player.render(buffer,512)>0, "seek restarts");
    check(!player.init(fixture(3),8000), "MA7 rejected by core");
    check(!player.init(fixture(4),8000), "unknown format rejected by core");

    // A long-gated event cannot keep conversion alive indefinitely.
    f.tracks[0].sequenceData={0,0x90,60,100,0xff,0xff,0x7f,0,0xff,0x2f,0};
    check(player.init(f,10), "long gate fixture initializes");
    total=0;
    while((n=player.render(buffer,512))>0) { total+=n; check(total<=6011,"hard cap"); }
    check(player.render(buffer,512)==0,"hard cap remains ended");
    std::cout << "Audio regression checks passed\n";
}
