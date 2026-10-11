// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic Mobile remembered-velocity reset fixtures.
namespace mobileVelocityResetRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& data){out.insert(out.end(),data.begin(),data.end());}
SmafFile score(const Bytes& bytes,int voice=0){return ma7DataRegression::score(2,bytes,voice);}
void note(Bytes& bytes,int ticks,int channel,int key,int velocity,int gate) {
    append(bytes,{uint8_t(ticks),uint8_t(0x90|channel),uint8_t(key),uint8_t(velocity),uint8_t(gate)});
}
void running(Bytes& bytes,int ticks,int channel,int key,int gate){append(bytes,{uint8_t(ticks),uint8_t(0x80|channel),uint8_t(key),uint8_t(gate)});}
void reset(Bytes& bytes,int ticks,int channel,bool oracle) {
    // The independently authored ordinary controllers reproduce implemented
    // live reset state without invoking its remembered-velocity path.
    resetControllersRegression::reset(bytes,ticks,channel,oracle);
}
void end(Bytes& bytes){append(bytes,{40,0xff,0});}
void resetVelocity() {
    for(int voice:{0,1,2})for(int channel=0;channel<16;++channel)for(int velocity:{0,1,32,55,100,127}) {
        Bytes actual,oracle;
        note(actual,0,channel,60,velocity,30);note(oracle,0,channel,60,velocity,30);
        reset(actual,5,channel,false);reset(oracle,5,channel,true);
        running(actual,0,channel,64,10);note(oracle,0,channel,64,64,10);
        running(actual,10,channel,67,10);running(oracle,10,channel,67,10);end(actual);end(oracle);
        auto f=score(actual,voice),ref=score(oracle,voice);
        check(renderAll(f)==renderAll(ref),"Mobile CC121 resets remembered velocity to64 for all subsequent velocityless notes");
        MaPlayer p;check(p.init(f,8000)&&p.scoreAudit().scanComplete,"valid reset keeps complete coverage");
        if(velocity==1&&channel==15) {
            check(renderAll(channelModeRegression::compressed(f))==renderAll(ref),"packed Mobile remembered-velocity reset matches explicit64");
            auto first=ma7DataRegression::rerender(p,31);p.seekToStart();
            check(first==ma7DataRegression::rerender(p,1024),"reset remembered velocity deterministic across seek/blocks");
            auto other=score({0,0x91,67,100,30},voice);other.tracks[0].trackNumber=1;
            f.tracks.push_back(other.tracks[0]);ref.tracks.push_back(other.tracks[0]);
            check(renderAll(f)==renderAll(ref),"remembered-velocity reset cannot alter independent track");
        }
    }
    // This fixture has no sounding prefix: a gate0 velocity0 note remembers
    // zero, but after reset the following note must sound at authored default64.
    auto f=score({0,0x90,60,0,0,5,0xb0,121,0,0,0x80,64,10},1);
    auto oracle=score({5,0x90,64,64,10},1);
    check(renderAll(f)==renderAll(oracle)&&ma7DataRegression::audible(renderAll(f)),
          "reset releases future notes from remembered mute without inventing a prefix voice");
}
void ordering() {
    for(int voice:{0,1,2}) {
        Bytes actual,oracle;
        // Existing key and same-tick note before reset keep40; after reset64.
        note(actual,0,0,60,40,30);note(oracle,0,0,60,40,30);
        running(actual,5,0,64,10);note(oracle,5,0,64,40,10);
        reset(actual,0,0,false);reset(oracle,0,0,true);
        running(actual,0,0,67,10);note(oracle,0,0,67,64,10);end(actual);end(oracle);
        check(renderAll(score(actual,voice))==renderAll(score(oracle,voice)),"same-tick reset preserves before/after authored note order and live velocity");
        // A subsequent explicit gate0 note replaces64 with100 for this channel.
        actual.clear();oracle.clear();note(actual,0,0,60,40,30);note(oracle,0,0,60,40,30);
        reset(actual,5,0,false);reset(oracle,5,0,true);
        note(actual,0,0,64,100,0);note(oracle,0,0,64,100,0);
        running(actual,0,0,67,10);note(oracle,0,0,67,100,10);end(actual);end(oracle);
        check(renderAll(score(actual,voice))==renderAll(score(oracle,voice)),"explicit gate0 after reset legitimately overwrites default64");
        // Reset only channel0: channel1 still reuses its independently authored100.
        actual.clear();oracle.clear();
        for(auto* s:{&actual,&oracle}){note(*s,0,0,60,40,30);note(*s,0,1,67,100,30);}
        reset(actual,5,0,false);reset(oracle,5,0,true);
        running(actual,0,1,69,10);note(oracle,0,1,69,100,10);
        running(actual,0,0,64,10);note(oracle,0,0,64,64,10);end(actual);end(oracle);
        check(renderAll(score(actual,voice))==renderAll(score(oracle,voice)),"reset cannot overwrite other channel's remembered velocity");
        actual.clear();oracle.clear();note(actual,0,0,60,40,30);note(oracle,0,0,60,40,30);
        reset(actual,5,0,false);reset(actual,0,0,false);reset(oracle,5,0,true);
        running(actual,0,0,64,10);note(oracle,0,0,64,64,10);end(actual);end(oracle);
        check(renderAll(score(actual,voice))==renderAll(score(oracle,voice)),"repeated reset is idempotent for live and remembered state");
    }
}
void unchanged() {
    for(int voice:{0,1,2})for(int value:{1,127,128,255}) {
        Bytes actual,oracle;note(actual,0,0,60,40,30);note(oracle,0,0,60,40,30);
        append(actual,{5,0xb0,121,uint8_t(value)});append(oracle,{5,0xff,0});
        running(actual,0,0,64,10);note(oracle,0,0,64,40,10);end(actual);end(oracle);
        check(renderAll(score(actual,voice))==renderAll(score(oracle,voice)),"invalid CC121 cannot reset remembered velocity");
    }
    for(int voice:{0,1,2})for(int bank:{0,0x80}) {
        auto f=ma7DataRegression::score(3,{0,uint8_t(bank|0x10),60,100,30,
            5,uint8_t(bank|0x30),121,0,0,uint8_t(bank),64,10,40,0xff,0},voice);
        auto ref=f;ref.tracks[0].sequenceData[7]=77;
        check(renderAll(f)==renderAll(ref),"unsupported MA7 CC121 retains both banks' old remembered velocity");
    }
}
}
void checkMobileVelocityReset(){mobileVelocityResetRegression::resetVelocity();mobileVelocityResetRegression::ordering();mobileVelocityResetRegression::unchanged();}
