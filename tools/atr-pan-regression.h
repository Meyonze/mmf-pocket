// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original quiet recordings/containers only; no private songs or vendor ROM.
namespace atrPanRegression {
using atrFormatRegression::Bytes;
using atrFormatRegression::handBuilt;
using atrGateRegression::tone;
using atrGateRegression::atRate;
void stereoOracle(const std::vector<float>& actual,const std::vector<float>& unity,
                  double left,double right) {
    check(actual.size()<=unity.size()+64 && unity.size()<=actual.size()+64,
          "ATR pan filter-drain difference is bounded");
    for(size_t i=0;i<std::max(actual.size(),unity.size());++i) {
        const double a=i<actual.size()?actual[i]:0;
        const double b=i<unity.size()?unity[i]*(i%2?right:left):0;
        check(std::fabs(a-b)<1e-7,"ATR pan matches independent constant-power sample oracle");
    }
}
void pan() {
    auto base=handBuilt(0,1,8000,0,0,tone(800),{0,1,100});
    for(int rate:{8000,44100,48000}) {
        const auto unity=atRate(base,rate);
        for(int value=0;value<=127;++value) {
            auto f=base;f.tracks[0].sequenceData={0,0,0x3a,uint8_t(value),0,1,100};
            const double angle=double(value)/127.0*1.5707963267948966;
            const auto actual=atRate(f,rate);
            stereoOracle(actual,unity,value==127?0:std::cos(angle),value==0?0:std::sin(angle));
            if(value==0 || value==127) {
                for(size_t i=value==0?1:0;i<actual.size();i+=2)
                    check(actual[i]==0,"ATR pan endpoint is exactly silent on the opposite side");
            }
        }
    }
    auto controlled=base;controlled.tracks[0].sequenceData={0,0,0x3a,32,0,0,0x37,64,0,0,0x3b,96,0,1,100};
    const double angle=32.0/127.0*1.5707963267948966;
    const double gain=std::pow(64.0/127.0,2)*std::pow(96.0/127.0,2);
    stereoOracle(atRate(controlled,8000),atRate(base,8000),std::cos(angle)*gain,std::sin(angle)*gain);
    auto repeated=controlled;repeated.tracks[0].sequenceData.insert(repeated.tracks[0].sequenceData.begin(),{0,0,0x3a,32});
    check(atRate(repeated,8000)==atRate(controlled,8000),"repeated ATR pan cannot compound gain");
    for(int value=128;value<=255;++value) {
        auto reserved=controlled;reserved.tracks[0].sequenceData.insert(reserved.tracks[0].sequenceData.begin()+4,{0,0,0x3a,uint8_t(value)});
        check(atRate(reserved,8000)==atRate(controlled,8000),"reserved ATR pan preserves previous gain and pan");
    }
    // Live endpoint movement: independent sources divide the original PCM
    // at20ms, keeping source position and the existing linear filter history.
    auto moving=base;moving.tracks[0].sequenceData={0,0,0x3a,0,0,1,100,20,0,0x3a,127};
    auto leftData=tone(800),rightData=leftData;
    std::fill(leftData.begin()+160,leftData.end(),0);
    std::fill(rightData.begin(),rightData.begin()+160,0);
    auto expected=handBuilt(0,1,8000,0,0,leftData,{0,0,0x3a,0,0,1,100});
    expected.tracks.push_back(handBuilt(1,1,8000,0,0,rightData,{0,0,0x3a,127,0,1,100}).tracks[0]);
    stereoOracle(atRate(moving,8000),atRate(expected,8000),1,1);
    auto liveGain=controlled;
    liveGain.tracks[0].sequenceData={0,0,0x3a,32,0,1,100,20,0,0x37,64,0,0,0x3b,96,
                                   20,0,0x37,64};
    auto gainExpected=handBuilt(0,1,8000,0,0,leftData,{0,0,0x3a,32,0,1,100});
    gainExpected.tracks.push_back(handBuilt(1,1,8000,0,0,rightData,
        {0,0,0x3a,32,0,0,0x37,64,0,0,0x3b,96,0,1,100}).tracks[0]);
    stereoOracle(atRate(liveGain,8000),atRate(gainExpected,8000),1,1);
    check(atRate(moving,8000)==atRate(moving,8000,1) && atRate(moving,8000)==atRate(moving,8000,1024),
          "live ATR pan is render-block invariant");
    auto channel=base;channel.tracks[0].sequenceData={0,0,0x7a,0,0,1,100};
    check(atRate(channel,8000)==atRate(base,8000),"ATR pan is local-channel scoped");
    auto track=base;track.tracks.push_back(handBuilt(1,1,8000,0,0,Bytes(800,0),{0,0,0x3a,0,0,1,100}).tracks[0]);
    check(atRate(track,8000)==atRate(base,8000),"ATR pan cannot affect another track");
    MaPlayer p;check(p.init(moving,8000),"ATR pan replay initializes");
    float buffer[514];while(p.render(buffer,257)>0){}
    p.seekToStart();std::vector<float> replay;int n;
    while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(replay==atRate(moving,8000),"ATR pan resets on seek, then replays authored controls");
    check(p.init(base,8000),"ATR pan reinit initializes");replay.clear();
    while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(replay==atRate(base,8000),"ATR pan does not leak into another file; unspecified remains unity");
    auto noSource=base;noSource.tracks[0].sequenceData={0,0,0x3a,32};
    check(!p.init(noSource,8000),"ATR pan alone cannot invent audio playback");
    // Deliberately collide score channel120 with ATR local channel0.
    auto score=fixture();addPcmFixture(score);score.tracks[0].trackNumber=7;
    score.tracks[0].sequenceData={0,0x98,60,100,10,10,0xff,0x2f,0};
    auto mixed=score;mixed.tracks.push_back(handBuilt(0,1,8000,0,0,Bytes(800,0),{0,1,100,20,0,0x3a,0}).tracks[0]);
    check(renderAll(mixed)==renderAll(score),"live ATR pan never alters WT/score even on a colliding global channel");
    // A panned ATR slot stolen by WT must reinitialize its own pan.
    auto wt=fixture();addPcmFixture(wt);wt.tracks[0].durationTimeBase=wt.tracks[0].gateTimeBase=0;
    wt.tracks[0].sequenceData={5,0x90,60,100,50,55,0xff,0x2f,0};
    auto reused=wt;
    for(int i=0;i<16;++i)reused.tracks.push_back(handBuilt(i,1,8000,0,0,Bytes(1200,0),{0,0,0x3a,0,0,1,30}).tracks[0]);
    check(atRate(reused,8000)==atRate(wt,8000),"WT stolen slot cannot inherit ATR pan");
}
Bytes length(int value) {
    if(value<128)return {uint8_t(value)};
    return {uint8_t(0x80|((value>>7)-1)),uint8_t(value&127)};
}
SmafFile hps(const Bytes& sequence) {
    // Original binary score container: HPS header + two channel-status bytes.
    Bytes body{0,0,0,0,0,0};atrFormatRegression::append(body,atrFormatRegression::chunk("Mtsq",sequence));
    return atrFormatRegression::parse(atrFormatRegression::chunk("MTR0",body));
}
void incomplete(const SmafFile& f) {
    MaPlayer p;p.init(f,8000);
    check(!p.scoreAudit().scanComplete && p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]>0,
          "broken HPS length is explicitly malformed/incomplete");
}
void lengths() {
    for(int value:{127,128,16511}) {
        Bytes seq{0,1};atrFormatRegression::append(seq,length(value));
        auto f=handBuilt(0,1,8000,0,0,tone(80),seq);MaPlayer p;
        check(p.init(f,8000) && p.scoreAudit().scanComplete,"valid HPS gate boundary is complete");
        auto expected=handBuilt(0,1,8000,0,0,tone(80),{0,1,100});
        check(renderAll(f)==renderAll(expected),"valid wide HPS gate never truncates source");
        seq=length(value);seq.insert(seq.end(),{1,100});
        f=handBuilt(0,1,8000,0,0,tone(80),seq);
        check(p.init(f,8000) && p.scoreAudit().scanComplete,"valid HPS duration boundary is complete");
        auto samples=renderAll(f);
        check(samples.size()>=size_t(value)*16 && samples[size_t(value)*16]!=0,
              "HPS duration127/128/16511 schedules the exact first sample");
    }
    const Bytes prefix{0,1,100};
    auto base=atrFormatRegression::parse(atrFormatRegression::atr(0,1,1,0x10,0,0,tone(800),prefix));
    auto score=hps({0,0x21,100});
    for(const Bytes& tail:std::vector<Bytes>{{0,1,0x80},{0,1,0x80,0x80,0,1,100},
            {0,1,0xff,0xff,0,1,100},{0x80},{0x80,0x80,1,100},
            {0,0xff,0xf0,0x80},{0,0xff,0xf0,0x80,0x80,0,1,100}}) {
        auto broken=base;auto& seq=broken.tracks[0].sequenceData;seq.insert(seq.end(),tail.begin(),tail.end());
        incomplete(broken);
        check(renderAll(broken)==renderAll(base),"bad ATR gate/duration/exclusive keeps only valid prefix, not fake notes or EOS");
        auto brokenScore=score;auto& scoreSeq=brokenScore.tracks[0].sequenceData;
        auto scoreTail=tail;if(scoreTail.size()>1 && scoreTail[0]==0 && scoreTail[1]==1)scoreTail[1]=0x21;
        scoreSeq.insert(scoreSeq.end(),scoreTail.begin(),scoreTail.end());incomplete(brokenScore);
        check(renderAll(brokenScore)==renderAll(score),"bad score length cannot become a spurious note");
        broken.tracks.push_back(score.tracks[0]);auto mixed=base;mixed.tracks.push_back(score.tracks[0]);
        check(renderAll(broken)==renderAll(mixed),"broken HPS length stops only its track, preserving other tracks");
    }
    for(const Bytes& setup:std::vector<Bytes>{{0xf0,0x80},{0xf0,0x80,0x80,0x43}}) {
        auto badSetup=score;badSetup.tracks[0].setupData=setup;
        incomplete(badSetup);check(renderAll(badSetup)==renderAll(score),"broken setup length cannot invent a voice");
    }
}
void reservedFf() {
    auto base=handBuilt(0,1,8000,0,0,tone(800),{0,1,100});
    for(int op:{1,0x2f,0x51,0x58,0x7f,0xf1,0xff}) {
        auto broken=base;broken.tracks[0].sequenceData.insert(broken.tracks[0].sequenceData.end(),
            {20,0xff,uint8_t(op),0,0,1,100});
        check(atRate(broken,8000)==atRate(base,8000),"unknown ATR FF stops decoding without guessing MIDI meta or muting valid gate");
        MaPlayer p;check(p.init(broken,8000) && !p.scoreAudit().scanComplete &&
              p.scoreAudit().totals[size_t(ScoreAudit::Status::Reserved)]>0,"unknown ATR FF is reserved/incomplete, not EOS");
        auto score=hps({0,0x21,100});auto mixed=base;mixed.tracks.push_back(score.tracks[0]);
        broken.tracks.push_back(score.tracks[0]);
        check(renderAll(broken)==renderAll(mixed),"unknown ATR FF cannot stop another score track");
    }
    // An exclusion oracle must use a defined HPS message, not fabricated
    // MIDI-meta framing. Reserved HPS FF is covered by hps-meta-regression.
    auto score=hps({0,0x21,100,20,0xff,0,0,0x21,100});
    auto reference=hps({0,0x21,100,20,0x21,100});
    check(renderAll(score)==renderAll(reference),"defined HPS score NOP remains independent of ATR reserved-FF policy");
}
}
void checkAtrPan() {
    atrPanRegression::pan();atrPanRegression::lengths();atrPanRegression::reservedFf();
}
