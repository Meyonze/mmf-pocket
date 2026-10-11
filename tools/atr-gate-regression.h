// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic ATR gate fixtures; no private recordings or song data.
namespace atrGateRegression {
using atrFormatRegression::Bytes;
using atrFormatRegression::handBuilt;
Bytes tone(size_t frames) {
    Bytes data;
    // Flat adjacent samples retain the older native-rate truncation fixtures.
    // Nonnative tests use the complete source and independent timed hard mute.
    for(size_t i=0;i<frames;++i) {
        const size_t phase=i>0 && i%8==0 ? i-1 : i;
        data.push_back(uint8_t(int8_t(24*std::sin(phase*.2)+12)));
    }
    return data;
}
std::vector<float> atRate(const SmafFile& file,int rate,int block=257) {
    MaPlayer p;check(p.init(file,rate),"ATR gate fixture initializes");
    std::vector<float> out,buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0) {
        out.insert(out.end(),buffer.begin(),buffer.begin()+n*2);
        check(out.size()<size_t(rate)*4,"ATR gate fixture must not acquire a silent safety tail");
    }
    check(!p.diagnostics().tailLimitReached,"ATR gate ends naturally");return out;
}
void testCutAndClock() {
    // D=50ms, G=1ms: trigger at50ms, stop10ms later. Alternate1ms D clock
    // schedules EOS at60ms, with the SAME complete recording (FIR edge taps).
    auto file=handBuilt(0,1,8000,0x13,0,tone(800),{1,1,10});
    auto ended=handBuilt(0,1,8000,0,0,tone(800),{50,1,100,10,0,0,0,0});
    for(int rate:{8000,44100,48000}) {
        const auto expected=atRate(ended,rate);
        const auto actual=atRate(file,rate);
        if(actual!=expected) {
            size_t i=0;while(i<std::min(actual.size(),expected.size()) && actual[i]==expected[i])++i;
            std::cerr<<"ATR oracle rate="<<rate<<" actual="<<actual.size()<<" expected="<<expected.size()<<" first_diff="<<i<<'\n';
        }
        check(actual==expected,"ATR gate cuts at declared G clock, preserving pre-gate PCM/filter output");
        check(actual==atRate(file,rate,1) && actual==atRate(file,rate,1024),"ATR gate is block invariant");
    }
    MaPlayer p;check(p.init(file,8000),"ATR gated seek initializes");
    float buffer[514];while(p.render(buffer,257)>0){}
    p.seekToStart();std::vector<float> replay;int n;
    while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(replay==atRate(file,8000),"ATR gate identity survives timeline replay");
    // HPS two-byte GateTime128 with G=1ms, not MIDI VLQ or D clock.
    auto wide=handBuilt(0,1,8000,0x13,0,tone(2000),{0,1,0x80,0});
    auto wideExpected=handBuilt(0,1,8000,0x13,0,tone(1024),{0,1,0x80,0});
    check(atRate(wide,8000)==atRate(wideExpected,8000),"ATR two-byte gate uses HandyPhone length encoding");
}
void testIdentity() {
    // Overlap the SAME channel/wave. The earlier gate cannot stop the later
    // instance. Independent oracle gives each instance its own truncated wave.
    auto overlap=handBuilt(0,1,8000,0,0,tone(1200),{0,1,40,10,1,80});
    auto expected=handBuilt(0,1,8000,0,0,tone(320),{0,1,40});
    auto second=handBuilt(1,1,8000,0,0,tone(640),{10,1,80});
    expected.tracks.push_back(second.tracks[0]);
    check(atRate(overlap,8000)==atRate(expected,8000),"ATR gate targets a unique trigger, not channel/wave ID");
    auto separate=handBuilt(0,1,8000,0,0,tone(1200),{0,1,40});
    separate.tracks.push_back(handBuilt(1,1,8000,0,0,tone(1200),{10,1,80}).tracks[0]);
    check(atRate(separate,8000)==atRate(expected,8000),"ATR gates do not collide across track-local wave banks");
    // Exhaust the16-slot pool. The stolen oldest gate must not stop its new
    // occupant at10ms. All other samples are zero, so only the new tone sounds.
    auto stolen=handBuilt(0,1,8000,0,0,Bytes(1200,0),{0,1,10});
    for(int i=1;i<16;++i) {
        auto t=handBuilt(i,1,8000,0,0,Bytes(1200,0),{0,1,20});
        stolen.tracks.push_back(t.tracks[0]);
    }
    stolen.tracks.push_back(handBuilt(16,1,8000,0,0,tone(1200),{0,1,80}).tracks[0]);
    auto last=handBuilt(16,1,8000,0,0,tone(640),{0,1,80});
    check(atRate(stolen,8000)==atRate(last,8000),"stolen ATR gate cannot terminate a reused slot");
    auto wt=fixture();addPcmFixture(wt);
    wt.tracks[0].durationTimeBase=wt.tracks[0].gateTimeBase=0;
    wt.tracks[0].sequenceData={5,0x90,60,100,50,55,0xff,0x2f,0};
    auto wtReused=wt;
    for(int i=0;i<16;++i)
        wtReused.tracks.push_back(handBuilt(i,1,8000,0,0,Bytes(1200,0),{0,1,30}).tracks[0]);
    check(atRate(wtReused,8000)==atRate(wt,8000),"ATR gate does not stop a WT voice in a reused slot");
}
void testNaturalEndAndLaterEvent() {
    // Maximum16511 gate *50ms must not force a10ms recording to emit825s
    // of silence or hit the600s safety cutoff.
    auto longGate=handBuilt(0,1,8000,0,0x13,tone(80),{0,1,0xff,0x7f});
    auto ordinary=handBuilt(0,1,8000,0,0,tone(80),{0,1,100});
    check(atRate(longGate,8000)==atRate(ordinary,8000),"already ended ATR gates do not append silence");
    // Later authored WaveOn is retained even when the prior source has ended.
    auto later=longGate;later.tracks[0].sequenceData={0,1,0xff,0x7f,100,1,1};
    auto laterReference=ordinary;laterReference.tracks[0].sequenceData={0,1,100,100,1,100};
    check(atRate(later,8000)==atRate(laterReference,8000),"orphan gate cleanup cannot skip a later authored trigger");
    // A gate-less controller is still an authored timed event; preserve its
    // delay rather than skipping to natural source completion.
    auto controller=longGate;controller.tracks[0].sequenceData={0,1,0xff,0x7f,100,0,0x37,64};
    check(atRate(controller,8000).size()>=1600,"orphan gate cleanup preserves later controller timing");
    auto reserved=ordinary;reserved.tracks[0].sequenceData={0,1,0,0,1,100};
    check(atRate(reserved,8000)==atRate(ordinary,8000),"reserved zero gate never allocates an ATR voice");
    // An independently held WT must not acquire the orphan gate's825s horizon.
    auto held=fixture();addPcmFixture(held,1,0);
    held.tracks[0].durationTimeBase=held.tracks[0].gateTimeBase=0;
    held.tracks[0].sequenceData={0,0x90,60,100,1};
    auto mixed=held;mixed.tracks.push_back(longGate.tracks[0]);
    MaPlayer control,subject;
    check(control.init(held,8000) && subject.init(mixed,8000),"held WT/ATR safety horizon initializes");
    check(subject.totalSamples()<=control.totalSamples()+81,"orphan ATR gate does not extend held WT safety horizon");
    float buffer[514];uint64_t count=0;int n;
    while((n=subject.render(buffer,257))>0)count+=uint64_t(n);
    check(count<=control.totalSamples()+81,"mixed held WT actually terminates within its source-bounded horizon");
    auto longRecording=handBuilt(0,1,8000,0,0x13,tone(96000),{0,1,0x80,112}); //240*50ms=12s
    MaPlayer recording;check(recording.init(longRecording,8000),"long ATR initializes");
    check(recording.totalSamples()>=96000,"real ATR duration is not replaced with a10s gate-less horizon");
    count=0;while((n=recording.render(buffer,257))>0)count+=uint64_t(n);
    check(count>=96000 && count<96128 && !recording.diagnostics().tailLimitReached,
          "real12s recording fully sounds and ends naturally after its authored gate");
}
}
void checkAtrGates() {
    atrGateRegression::testCutAndClock();
    atrGateRegression::testIdentity();
    atrGateRegression::testNaturalEndAndLaterEvent();
}
