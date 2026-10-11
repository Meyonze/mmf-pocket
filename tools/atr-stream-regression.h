// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original ATR stream/control fixtures; no song data or vendor recordings.
namespace atrStreamRegression {
using atrFormatRegression::Bytes;
using atrFormatRegression::handBuilt;
using atrGateRegression::tone;
using atrGateRegression::atRate;
void eos() {
    // Stop at20ms, not decode time. A timed NOP precedes zero-duration EOS.
    auto f=handBuilt(0,1,8000,0,0,tone(1600),{0,1,100,20,0xff,0,0,0,0,0});
    // Same complete source: truncating its payload changes FIR endpoint taps.
    // Compare independent20ms gate hard mute against timed EOS hard mute.
    auto expected=handBuilt(0,1,8000,0,0,tone(1600),{0,1,20});
    for(int rate:{8000,44100,48000}) {
        const auto actual=atRate(f,rate);
        if(actual!=atRate(expected,rate))std::cerr<<"EOS oracle rate="<<rate<<" actual="<<actual.size()<<" expected="<<atRate(expected,rate).size()<<'\n';
        check(actual==atRate(expected,rate),"explicit ATR EOS mutes at sequence time, retaining filter history");
        check(actual==atRate(f,rate,1) && actual==atRate(f,rate,1024),"ATR EOS is block invariant");
    }
    auto suffix=f;suffix.tracks[0].sequenceData.insert(suffix.tracks[0].sequenceData.end(),{100,1,100});
    check(atRate(suffix,8000)==atRate(f,8000),"ATR bytes after EOS cannot schedule later triggers");
    auto immediate=f;immediate.tracks[0].sequenceData={0,1,100,0,0,0,0};
    for(float value:atRate(immediate,8000))check(value==0,"same-time WaveOn then EOS has no audible source frame");
    // All overlapping local channels in track0 stop; track1 keeps playing.
    auto overlap=f;overlap.tracks[0].sequenceData={0,1,100,5,0x41,100,15,0xff,0,0,0,0,0};
    auto overlapExpected=expected;
    auto second=handBuilt(0,1,8000,0,0,tone(120),{5,0x41,15});
    second.tracks[0].trackNumber=2;overlapExpected.tracks.push_back(second.tracks[0]);
    auto keep=handBuilt(1,1,8000,0,0,tone(640),{0,1,100});
    overlap.tracks.push_back(keep.tracks[0]);overlapExpected.tracks.push_back(keep.tracks[0]);
    check(atRate(overlap,8000)==atRate(overlapExpected,8000),"ATR EOS stops all and only its source track");
    // Same-time EOS of track0 cannot stop track1's new trigger.
    auto tied=f;auto newTrack=handBuilt(1,1,8000,0,0,tone(320),{20,1,100});
    tied.tracks.push_back(newTrack.tracks[0]);auto tiedExpected=expected;tiedExpected.tracks.push_back(newTrack.tracks[0]);
    check(atRate(tied,8000)==atRate(tiedExpected,8000),"same-time EOS/new-wave order is track isolated");
    auto wt=fixture();addPcmFixture(wt);auto mixed=wt;mixed.tracks.push_back(immediate.tracks[0]);
    check(renderAll(mixed)==renderAll(wt),"ATR EOS leaves keyed WT active");
    auto audio=fixture();addPcmFixture(audio);
    audio.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,0,0x90,0,127,10,10,0xff,0x2f,0};
    auto audioMixed=audio;audioMixed.tracks.push_back(immediate.tracks[0]);
    check(renderAll(audioMixed)==renderAll(audio),"ATR EOS leaves separate recorded Mwa Audio active");
    auto reused=wt;reused.tracks[0].durationTimeBase=reused.tracks[0].gateTimeBase=0;
    reused.tracks[0].sequenceData={5,0x90,60,100,50,55,0xff,0x2f,0};
    auto reusedExpected=reused;
    for(int i=0;i<16;++i)reused.tracks.push_back(handBuilt(i,1,8000,0,0,Bytes(1200,0),
        {0,1,100,30,0xff,0,0,0,0,0}).tracks[0]);
    check(atRate(reused,8000)==atRate(reusedExpected,8000),"ATR EOS cannot stop WT after slot reuse");
    MaPlayer player;check(player.init(f,8000),"ATR EOS seek initializes");float block[514];while(player.render(block,257)>0){}
    player.seekToStart();std::vector<float> replay;int n;
    while((n=player.render(block,257))>0)replay.insert(replay.end(),block,block+n*2);
    check(replay==atRate(f,8000),"ATR EOS replay resets source state");
    auto empty=handBuilt(0,1,8000,0,0,tone(80),{0,0,0,0});
    check(!player.init(empty,8000),"EOS alone is not a playable source");
    auto missing=empty;missing.tracks[0].sequenceData={0,2,100,0,0,0,0};
    check(!player.init(missing,8000),"missing wave plus EOS cannot report fake playback success");
    // Early EOS bounds an otherwise long recording's safety contribution.
    auto held=fixture();addPcmFixture(held,1,0);held.tracks[0].sequenceData={0,0x90,60,100,1};
    auto longMuted=handBuilt(0,1,8000,0,0x13,tone(96000),{0,1,0x80,112,20,0xff,0,0,0,0,0});
    auto heldMixed=held;heldMixed.tracks.push_back(longMuted.tracks[0]);
    MaPlayer baseline;check(baseline.init(held,8000) && player.init(heldMixed,8000),"early EOS safety initializes");
    check(player.totalSamples()<=baseline.totalSamples()+160,"early EOS cannot extend an unrelated held WT to the recording's natural end");
}
void nop() {
    auto f=handBuilt(0,1,8000,0,0,tone(80),{0,1,100,100,0xff,0});
    auto actual=atRate(f,8000);
    check(actual.size()==1600,"ATR final timed NOP preserves authored100ms silence after source ends");
    auto expected=handBuilt(0,1,8000,0,0,tone(80),{0,1,100});auto prefix=atRate(expected,8000);
    check(std::equal(prefix.begin(),prefix.end(),actual.begin()),"NOP changes timing, not prior source output");
    auto empty=f;empty.tracks[0].sequenceData={100,0xff,0};MaPlayer p;
    check(!p.init(empty,8000),"NOP alone is not a playable source");
}
void gainOracle(const std::vector<float>& actual,const std::vector<float>& unity,double gain) {
    // Quiet source stays below limiter. LP is linear; independent constant
    // gain oracle permits only floating roundoff and sub-threshold tail drain.
    check(actual.size()<=unity.size()+64 && unity.size()<=actual.size()+64,"ATR gain tail difference is bounded");
    const size_t length=std::max(actual.size(),unity.size());
    for(size_t i=0;i<length;++i) {
        const double a=i<actual.size()?actual[i]:0;
        const double expected=i<unity.size()?double(unity[i])*gain:0;
        check(std::fabs(a-expected)<1e-7,"ATR authored gain matches independent sample-domain squared curve");
    }
}
void gains() {
    auto base=handBuilt(0,1,8000,0,0,tone(800),{0,1,100});const auto unity=atRate(base,8000);
    for(int value:{0,1,32,64,96,127})for(int op:{0x37,0x3b}) {
        auto f=base;f.tracks[0].sequenceData={0,0,uint8_t(op),uint8_t(value),0,1,100};
        const double normalized=double(value)/127.0;
        gainOracle(atRate(f,8000),unity,normalized*normalized);
    }
    for(int shortValue=1;shortValue<=14;++shortValue) {
        auto shortForm=base;shortForm.tracks[0].sequenceData={0,0,uint8_t(shortValue),0,1,100};
        auto longForm=base;const int value=shortValue==1?0:shortValue*8+15;
        longForm.tracks[0].sequenceData={0,0,0x3b,uint8_t(value),0,1,100};
        check(atRate(shortForm,8000)==atRate(longForm,8000),"ATR short Expression maps exactly to standard values");
    }
    auto controls=base;controls.tracks[0].sequenceData={0,0,0x37,64,0,0,0x3b,32,0,1,100};
    auto repeated=controls;repeated.tracks[0].sequenceData.insert(repeated.tracks[0].sequenceData.begin(),{0,0,0x37,64});
    check(atRate(repeated,8000)==atRate(controls,8000),"repeated ATR gain commands never compound attenuation");
    const double a=64.0/127.0,b=32.0/127.0;
    gainOracle(atRate(controls,8000),unity,a*a*b*b);
    // Live mute after20ms has independent physically truncated-source oracle.
    for(int op:{0x37,0x3b}) {
        auto live=base;live.tracks[0].sequenceData={0,1,100,20,0,uint8_t(op),0};
        auto truncated=handBuilt(0,1,8000,0,0,tone(160),{0,1,100});auto expected=atRate(truncated,8000);
        auto actual=atRate(live,8000);
        check(std::equal(expected.begin(),expected.end(),actual.begin()),"live ATR mute retains existing filter history exactly");
        for(size_t i=expected.size();i<actual.size();++i)check(std::fabs(actual[i])<1e-7,"live mute has no remaining source energy");
    }
    auto reserved=controls;reserved.tracks[0].sequenceData.insert(reserved.tracks[0].sequenceData.begin()+8,
        {0,0,0x37,128,0,0,0x3b,255,0,0,15});
    check(atRate(reserved,8000)==atRate(controls,8000),"reserved ATR gain values retain prior gain, never mask or mute");
    auto channel=base;channel.tracks[0].sequenceData={0,0,0x77,0,0,1,100};
    check(atRate(channel,8000)==unity,"ATR local channel1 gain cannot modify channel0");
    auto track=base;track.tracks.push_back(handBuilt(1,1,8000,0,0,Bytes(800,0),{0,0,0x37,0,0,1,100}).tracks[0]);
    check(atRate(track,8000)==unity,"ATR track1 controller cannot modify track0");
    MaPlayer p;check(p.init(controls,8000),"ATR gain seek initializes");float buffer[514];while(p.render(buffer,257)>0){}
    p.seekToStart();std::vector<float> replay;int n;
    while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(replay==atRate(controls,8000),"ATR gain state resets and replays on seek");
    check(p.init(base,8000),"ATR gain reinit initializes");replay.clear();
    while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(replay==unity,"ATR gain state cannot leak into a new song");
    auto only=base;only.tracks[0].sequenceData={0,0,0x37,64,0,0,0x3b,32,100,0xff,0};
    check(!p.init(only,8000),"controllers with no source are not fake playback");
}
void scoreIsolation() {
    auto score=fixture();addPcmFixture(score);score.tracks[0].trackNumber=7;
    score.tracks[0].sequenceData={0,0x98,60,100,10,10,0xff,0x2f,0};
    // Target ATR channel120, colliding deliberately with score base112+ch8.
    auto mixed=score;mixed.tracks.push_back(handBuilt(0,1,8000,0,0,Bytes(1600,0),
        {0,1,100,20,0,0x37,0,0,0,0x3b,0,0,0,0x34,0,0,0,0x3a,0,0,0,0x32,1}).tracks[0]);
    check(renderAll(mixed)==renderAll(score),"ATR gain and unimplemented controls never mutate score channel120");
    // An ATR exclusive that resembles a valid score WT definition must not
    // activate/replace an MTR instrument. EP=0 would silence a future note.
    auto donor=fixture();addPcmFixture(donor);
    Bytes payload(donor.tracks[0].setupData.begin()+2,donor.tracks[0].setupData.end());
    payload[24]=0; // synthetic patch EP=0, valid transport
    const auto patch=parseVoiceExclusive(payload.data(),payload.size()-1);
    check(patch.valid && patch.pcm.endPt==0,"ATR exclusive isolation fixture defines a valid silent WT patch");
    auto exclusive=score;exclusive.tracks[0].sequenceData={10,0x98,60,100,10,20,0xff,0x2f,0};
    auto sequence=Bytes{0,1,100,0,0xff,0xf0,uint8_t(payload.size())};
    sequence.insert(sequence.end(),payload.begin(),payload.end());
    auto exclusiveMixed=exclusive;exclusiveMixed.tracks.push_back(handBuilt(0,1,8000,0,0,Bytes(80,0),sequence).tracks[0]);
    check(renderAll(exclusiveMixed)==renderAll(exclusive),"ATR device exclusive cannot define a score voice");
}
}
void checkAtrStream() {
    atrStreamRegression::eos();atrStreamRegression::nop();
    atrStreamRegression::gains();atrStreamRegression::scoreIsolation();
}
