// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic HPS score containers, not private music or recordings.
namespace hpsControlsRegression {
using atrFormatRegression::Bytes;
using atrPanRegression::hps;
using atrGateRegression::atRate;
void append(Bytes& out,const Bytes& bytes) {out.insert(out.end(),bytes.begin(),bytes.end());}
Bytes command(int op,int value,int ch=0,int delay=0) {
    auto bytes=atrPanRegression::length(delay);
    append(bytes,{0,uint8_t((ch<<6)|0x30|op),uint8_t(value)});return bytes;
}
SmafFile score(Bytes prefix,int ch=0) {
    append(prefix,{0,uint8_t((ch<<6)|0x21),100,0x80,22,uint8_t((ch<<6)|0x24),100});
    return hps(prefix);
}
void reservedAudit(const SmafFile& f,int count) {
    MaPlayer p;check(p.init(f,8000),"HPS reserved-controller fixture initializes");
    check(p.scoreAudit().scanComplete && p.scoreAudit().totals[size_t(ScoreAudit::Status::Reserved)]==uint64_t(count),
          "HPS fixed-width reserved controls remain complete and explicitly reserved");
}
void longControls() {
    // Each control has a nondefault prior setting; high values must neither
    // wrap to low7 bits nor bend beyond the valid range.
    for(const auto [op,prior]:{std::pair{0,40},std::pair{4,48},std::pair{7,64},std::pair{10,32},std::pair{11,64}}) {
        Bytes valid=command(7,32);append(valid,command(op,prior));
        const auto reference=score(valid);const auto expected=renderAll(reference);
        bool audible=false;for(float s:expected)audible|=s!=0;check(audible,"HPS reserved oracle is actually audible");
        for(int value=128;value<=255;++value) {
            auto controls=valid;append(controls,command(op,value));auto broken=score(controls);
            check(renderAll(broken)==expected,"reserved HPS standard value preserves previous program/bend/volume/pan/expression");
            reservedAudit(broken,1);
        }
        // While the first note is active, a reserved command is inert. Its
        // delay still belongs to the next event; NOP is an independent oracle.
        auto live=reference;auto& seq=live.tracks[0].sequenceData;
        seq.insert(seq.begin()+valid.size()+3,{20,0,uint8_t(0x30|op),128});
        auto liveExpected=reference;auto& oracle=liveExpected.tracks[0].sequenceData;
        oracle.insert(oracle.begin()+valid.size()+3,{20,0xff,0});
        check(renderAll(live)==renderAll(liveExpected),"reserved live HPS control is inert without losing20ms of authored time");
        check(renderAll(live,1)==renderAll(live,1024),"reserved live HPS control is block invariant");
    }
    // Bank bit7 is a valid drum marker, NOT a reserved Program bit.
    for(int value=0;value<=255;++value) {
        auto f=score(command(1,value));MaPlayer p;check(p.init(f,8000),"HPS bank fixture initializes");
        check(p.scoreAudit().totals[size_t(ScoreAudit::Status::Reserved)]==0,
              "all full-byte HPS Bank values remain valid, including drum banks");
        bool found=false;for(const auto& group:p.scoreAudit().groups)
            if(group.domain==ScoreAudit::Domain::HpsLong && group.opcode==1)
                found=group.status==ScoreAudit::Status::Routed && group.minValue==value && group.maxValue==value;
        check(found,"HPS Bank audit retains raw bit7 value");
    }
    // Reserved values alone cannot synthesize a note; preserve their old
    // fixed-width timing behavior without masquerading as supported controls.
    auto only=hps(command(7,128));MaPlayer p;
    check(p.init(only,8000),"HPS reserved-only legacy timeline initializes");
    float buffer[514];int n;while((n=p.render(buffer,257))>0)
        for(int i=0;i<n*2;++i)check(buffer[i]==0,"reserved HPS controller never allocates a voice");
    check(p.diagnostics().fmNotes==0 && p.diagnostics().pcmNotes==0,"reserved-only HPS has no false notes");
}
void octave() {
    for(int prior:{1,2,3,4,0x81,0x82,0x83,0x84}) {
        Bytes valid=command(7,32);append(valid,command(2,prior));
        const auto expected=renderAll(score(valid));
        for(int value=5;value<=255;++value) {
            if(value>=0x81 && value<=0x84)continue;
            auto controls=valid;append(controls,command(2,value));auto f=score(controls);
            check(renderAll(f)==expected,"reserved octave command retains the authored absolute transposition");
            reservedAudit(f,1);
        }
        auto repeated=valid;append(repeated,command(2,prior));
        check(renderAll(score(repeated))==expected,"valid HPS octave shift is absolute, never accumulated");
        auto reset=valid;append(reset,command(2,0));
        check(renderAll(score(reset))==renderAll(score(command(7,32))),"valid zero octave explicitly resets transposition");
    }
}
void shortControls() {
    for(int ch=0;ch<4;++ch)for(int cls=0;cls<3;++cls)for(int value:{0,15}) {
        if(ch==0 && cls==0 && value==0)continue; // extended/EOS grammar, not a short event
        Bytes valid=command(7,32,ch);append(valid,command(11,64,ch));append(valid,command(4,48,ch));
        auto controls=valid;append(controls,{0,0,uint8_t((ch<<6)|(cls<<4)|value)});
        const auto f=score(controls,ch);
        check(renderAll(f)==renderAll(score(valid,ch)),"reserved short Expression/Bend/Modulation preserves prior channel state");
        reservedAudit(f,1);
    }
    for(int value=1;value<=14;++value) {
        Bytes expression=command(7,32);append(expression,{0,0,uint8_t(value)});
        Bytes expressionLong=command(7,32);append(expressionLong,command(11,value==1?0:value*8+15));
        check(renderAll(score(expression))==renderAll(score(expressionLong)),"valid short HPS Expression matches the independent specification table");
        Bytes bend=command(7,32);append(bend,{0,0,uint8_t(0x10|value)});
        Bytes bendLong=command(7,32);append(bendLong,command(4,value*8));
        check(renderAll(score(bend))==renderAll(score(bendLong)),"valid short HPS Bend matches standard8-step mapping");
    }
    // A short zero on channel0 is still the existing extended/EOS grammar.
    auto eos=hps({0,0x21,100,20,0,0,0,0,0x24,100});auto prefix=hps({0,0x21,100});
    check(renderAll(eos)==renderAll(prefix),"shared00 extended/EOS prefix retains existing interpretation");
}
void modulation() {
    // Standard HPS Modulation is parsed, not physically calibrated. A late
    // command must retain silence until its own time, like any known control,
    // without applying the different MA7 vibrato law.
    Bytes controls=command(7,32);auto f=score(controls);addPcmFixture(f,1,15);
    f.tracks[0].sequenceData=controls;append(f.tracks[0].sequenceData,{0,0x21,10});
    append(f.tracks[0].sequenceData,command(3,64,0,1000));
    auto expected=f;expected.tracks[0].sequenceData=controls;
    append(expected.tracks[0].sequenceData,{0,0x21,10});append(expected.tracks[0].sequenceData,command(7,32,0,1000));
    check(renderAll(f)==renderAll(expected),"standard HPS modulation preserves authored terminal timeline without changing timbre");
    MaPlayer p;check(p.init(f,8000) && p.scoreAudit().totals[size_t(ScoreAudit::Status::Unapplied)]==1,
          "known HPS modulation is explicitly unapplied, not unsupported grammar");
    for(int value=128;value<=255;++value) {
        auto broken=f;broken.tracks[0].sequenceData.back()=uint8_t(value);
        check(renderAll(broken)==renderAll(expected),"reserved standard HPS modulation is time-preserving and inaudible");
        reservedAudit(broken,1);
    }
    // Shared global channel0: HPS modulation must not become a MA7 wheel.
    for(bool pcm:{false,true}) {
        auto ma7=fixture(3);ma7.tracks[0].sequenceData={0,0x10,60,100,80};
        if(pcm) {
            addPcmFixture(ma7);
            auto voice=std::vector<uint8_t>{0x43,0x79,7,0x7f,1,0,0,0,0,1,
                0x1f,0x40,0,0,0,0x90,0xf0,0,7,0,0,0,0,0,80,1};setVoice(ma7,voice);
        } else {
            auto voice=std::vector<uint8_t>{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
                0,0xf0,0xf0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,0};
            voice[24]=7;setVoice(ma7,voice);
        }
        // Put MA7 last so its channel's existing controller mode is active;
        // the deliberate format collision then exercises the leaked wheel.
        auto mixed=ma7;mixed.tracks.insert(mixed.tracks.begin(),hps({5,0,0x2e,0,0,0x33,127}).tracks[0]);
        auto neutral=ma7;neutral.tracks.insert(neutral.tracks.begin(),hps({5,0,0x3a,64,0,0,0x3a,64}).tracks[0]);
        auto wheel=neutral;wheel.tracks[1].sequenceData.insert(wheel.tracks[1].sequenceData.end(),{1,0x30,1,127});
        check(renderAll(wheel)!=renderAll(neutral),"FM/WT isolation fixture actually responds to MA7 vibrato wheel");
        check(renderAll(mixed)==renderAll(neutral),"HPS modulation does not apply MA7's different wheel curve in mixed FM/WT input");
    }
    auto replay=score(command(7,32));append(replay.tracks[0].sequenceData,command(7,128,0,20));
    check(p.init(replay,8000),"HPS controls replay initializes");float buffer[514];while(p.render(buffer,257)>0){}
    p.seekToStart();std::vector<float> samples;int n;
    while((n=p.render(buffer,257))>0)samples.insert(samples.end(),buffer,buffer+n*2);
    check(samples==renderAll(replay),"HPS reserved controls and NOP timeline replay identically");
}
}
void checkHpsControls() {
    hpsControlsRegression::longControls();hpsControlsRegression::octave();
    hpsControlsRegression::shortControls();hpsControlsRegression::modulation();
}
