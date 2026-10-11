// SPDX-License-Identifier: Apache-2.0
#pragma once
// Synthetic samples only. No proprietary ROM data or per-song synthesis rules.
namespace ma7PanNext {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& in){out.insert(out.end(),in.begin(),in.end());}
Bytes message(int id,int mode,int value){return {0x43,0x79,8,0x7f,0x0b,uint8_t(id),uint8_t(mode),uint8_t(value)};}
Bytes timed(int ticks,const Bytes& msg){Bytes out={uint8_t(ticks),0xf0,uint8_t(msg.size()+1)};append(out,msg);out.push_back(0xf7);return out;}
SmafFile audio(int id=0,int channel=0,int format=3) {
    auto f=fixture(format);auto& t=f.tracks[0];
    int key=id<13?id:id-13+92;
    const uint8_t cc=uint8_t((format==3?0x30:0xb0)|channel),on=uint8_t((format==3?0x10:0x90)|channel);
    t.sequenceData={0,cc,0,125,0,cc,32,0,0,on,uint8_t(key),127,30,40,0xff,0x2f,0};
    WaveData w;w.number=id+1;w.adpcm=false;w.bitsPerSample=16;w.samplingRate=8000;
    for(int i=0;i<4000;++i) {int16_t x=int16_t(5000*std::sin(i*0.08));w.data.push_back(uint8_t(uint16_t(x)>>8));w.data.push_back(uint8_t(x));}
    t.waves.push_back(w);return f;
}
std::vector<float> drain(MaPlayer& p,int block=257) {
    std::vector<float> out,buf(size_t(block)*2);int n;
    while((n=p.render(buf.data(),block))>0){out.insert(out.end(),buf.begin(),buf.begin()+2*n);check(out.size()<8000*2*20,"pan fixture bounded");}
    return out;
}
void independentPan(const std::vector<float>& actual,const std::vector<float>& centered,int value,bool off=false) {
    const double center=1.5707963267948966*64.0/127.0,angle=1.5707963267948966*value/127.0;
    const double gains[]={off?1.0:value==127?0.0:std::cos(angle),off?1.0:value==0?0.0:std::sin(angle)};
    check(actual.size()<=centered.size()+64&&centered.size()<=actual.size()+64,"pan keeps source/gate/filter tail bounded");
    check(ma7DataRegression::audible(centered),"recorded Audio pan oracle is audible");
    for(size_t i=0;i<std::max(actual.size(),centered.size());++i) {
        const double a=i<actual.size()?actual[i]:0,b=i<centered.size()?centered[i]:0;
        check(std::fabs(a-b*gains[i&1]/(i&1?std::sin(center):std::cos(center)))<1e-7,
              "native Audio pan matches independently calculated constant-power or unity gain");
        if(!off&&gains[i&1]==0)check(a==0,"native Audio endpoint has exact opposite-side silence");
    }
}
void silentBank() {
    for(int high:{0,0x80}) for(int velocity:{1,2,64,127}) for(int voice:{0,1,2}) {
        auto f=fixture(3);
        if(voice==1){auto v=protocolRegression::tone();v[5]=121;v[6]=6;setVoice(f,v);}
        if(voice==2){addPcmFixture(f);f.tracks[0].setupData[7]=121;f.tracks[0].setupData[8]=6;}
        const uint8_t cc=uint8_t(0x30|high),on=uint8_t(0x10|high);
        f.tracks[0].sequenceData={0,cc,0,121,0,cc,32,6,0,on,60,uint8_t(velocity),30,
            5,cc,7,127,0,cc,11,127,5,uint8_t(high),64,20,30,0xff,0x2f,0};
        check(!ma7DataRegression::audible(renderAll(f)),"SP-MIDI silent bank cannot play a fallback/custom FM/WT or revive on gain change");
        auto legacy=f;legacy.tracks[0].formatType=2;
        legacy.tracks[0].sequenceData={0,0xb0,0,121,0,0xb0,32,6,0,0x90,60,100,10,10,0xff,0x2f,0};
        check(ma7DataRegression::audible(renderAll(legacy)),"silent-bank rule is MA7 only");
        auto neighbor=f;neighbor.tracks[0].sequenceData[7]=5;neighbor.tracks[0].sequenceData[11]=100;
        check(ma7DataRegression::audible(renderAll(neighbor)),"neighbor bank121/5 is not muted");
        MaPlayer p;check(p.init(f,8000),"silent bank initializes");auto first=drain(p,31);p.seekToStart();
        check(first==drain(p,1024)&&p.diagnostics().fmNotes+p.diagnostics().pcmNotes==2,"silent bank keeps allocation/gate and seek policy");
    }
    auto held=fixture(3);setVoice(held,protocolRegression::tone());
    held.tracks[0].sequenceData={0,0x10,60,100,30,5,0x30,0,121,0,0x30,32,6,30,0xff,0x2f,0};
    auto prior=held;prior.tracks[0].sequenceData={0,0x10,60,100,30,5,0xff,0,0,0xff,0,30,0xff,0x2f,0};
    check(renderAll(held)==renderAll(prior),"switch to silent bank does not silence an already-held ordinary note");
    auto recovery=held;recovery.tracks[0].sequenceData={0,0x30,0,121,0,0x30,32,6,0,0x10,60,100,10,
        10,0x30,0,0,0,0x30,32,0,0,0x10,64,100,10,20,0xff,0x2f,0};
    check(ma7DataRegression::audible(renderAll(recovery)),"next normal bank can sound after silent notes");
    auto mobile=fixture(2);mobile.tracks[0].sequenceData={0,0xb0,0,121,0,0xb0,32,6,0,0x90,60,100,10,10,0xff,0x2f,0};
    auto mixed=mobile;auto t=fixture(3).tracks[0];t.sequenceData={0,0xff,0x2f,0};mixed.tracks.push_back(t);
    check(ma7DataRegression::audible(renderAll(mixed)),"silent bank cannot spill to legacy note on shared channels");
}
void pan() {
    for(int id=0;id<32;++id) {
        auto base=audio(id);const auto centered=renderAll(base);
        for(int value:{0,32,64,96,127}) {
            auto setup=base;appendExclusive(setup,message(id,0,value));
            independentPan(renderAll(setup),centered,value);
            auto inlineSet=base;auto bytes=timed(0,message(id,0,value));append(bytes,inlineSet.tracks[0].sequenceData);
            inlineSet.tracks[0].sequenceData=bytes;
            check(renderAll(inlineSet)==renderAll(setup),"setup and inline native Audio pan match for all32 AudioIDs");
            auto blocked=setup;blocked.tracks[0].sequenceData.insert(blocked.tracks[0].sequenceData.begin(),{0,0x30,10,127});
            check(renderAll(blocked)==renderAll(setup),"native Audio pan overrides later channel pan");
        }
        auto off=base;appendExclusive(off,message(id,2,0));independentPan(renderAll(off),centered,64,true);
        auto offLater=off;offLater.tracks[0].sequenceData.insert(offLater.tracks[0].sequenceData.begin(),{0,0x30,10,0});
        check(renderAll(offLater)==renderAll(off),"Panoff means unity L/R and ignores channel pan");
    }
    // Test every valid pan value, not only endpoints/center.
    auto base=audio();const auto centered=renderAll(base);
    for(int value=0;value<128;++value){auto f=base;appendExclusive(f,message(0,0,value));independentPan(renderAll(f),centered,value);}
    auto cleared=base;appendExclusive(cleared,message(0,0,0));appendExclusive(cleared,message(31,1,64));
    check(renderAll(cleared)==centered,"CL1 clears all prior wave overrides, regardless of selected ID");
    auto unrelated=base;appendExclusive(unrelated,message(31,0,0));check(renderAll(unrelated)==centered,"other AudioID cannot change this source");
    auto legacy=audio(0,0,2),old=legacy;appendExclusive(legacy,message(0,0,0));
    check(renderAll(legacy)==renderAll(old),"legacy Mobile native Audio pan grammar is not inferred");
    auto wt=fixture(3);addPcmFixture(wt);wt.tracks[0].sequenceData={0,0x10,60,100,30,30,0xff,0x2f,0};
    auto wtSet=wt;appendExclusive(wtSet,message(0,0,0));check(renderAll(wtSet)==renderAll(wt),"native Audio pan cannot alter WT instrument");
    auto fm=wt;setVoice(fm,protocolRegression::tone());auto fmSet=fm;appendExclusive(fmSet,message(0,0,0));
    check(renderAll(fmSet)==renderAll(fm),"native Audio pan cannot alter FM");
    MaPlayer p;auto f=base;appendExclusive(f,message(0,0,0));check(p.init(f,8000),"pan seek initializes");
    auto first=drain(p,1);p.seekToStart();check(first==drain(p,1024),"native Audio pan seek/block invariance");
    check(p.init(base,8000)&&drain(p)==centered,"fresh init removes previous native overrides");
}
void live() {
    auto actual=audio();auto prefix=timed(0,message(0,0,0));append(prefix,actual.tracks[0].sequenceData);
    prefix.resize(prefix.size()-4);append(prefix,timed(10,message(0,0,127)));append(prefix,{30,0xff,0x2f,0});
    actual.tracks[0].sequenceData=prefix;
    auto oracle=audio();oracle.tracks[0].sequenceData.insert(oracle.tracks[0].sequenceData.begin(),{0,0x30,10,0});
    oracle.tracks[0].sequenceData.resize(oracle.tracks[0].sequenceData.size()-4);append(oracle.tracks[0].sequenceData,{10,0x30,10,127,30,0xff,0x2f,0});
    check(renderAll(actual)==renderAll(oracle),"native live pan follows the same independent source timeline as channel pan");
    auto locked=audio();appendExclusive(locked,message(0,0,0));
    locked.tracks[0].sequenceData.resize(locked.tracks[0].sequenceData.size()-4);append(locked.tracks[0].sequenceData,{5,0x30,10,127});
    append(locked.tracks[0].sequenceData,timed(5,message(31,1,64)));append(locked.tracks[0].sequenceData,{30,0xff,0x2f,0});
    check(renderAll(locked)==renderAll(oracle),"live CL1 restores current channel pan for active Audio");
    // Do not sort/reorder already-authored compact events. Live application
    // makes Note/Pan and Pan/Note at the same tick equivalent without resorting.
    auto same=audio();same.tracks[0].sequenceData.resize(same.tracks[0].sequenceData.size()-4);
    append(same.tracks[0].sequenceData,timed(0,message(0,0,0)));append(same.tracks[0].sequenceData,{40,0xff,0x2f,0});
    auto before=audio();auto seq=timed(0,message(0,0,0));append(seq,before.tracks[0].sequenceData);before.tracks[0].sequenceData=seq;
    check(renderAll(same)==renderAll(before),"same-time Audio pan is applied before producing the first sample");
    // MA7 source-track namespace is retained (tracknumbers do not imply MIDI channels).
    auto first=audio();first.tracks[0].trackNumber=200;auto second=audio(0,1);second.tracks[0].trackNumber=201;
    appendExclusive(first,message(0,0,0));appendExclusive(second,message(0,0,127));
    auto multi=first;multi.tracks.push_back(second.tracks[0]);
    // Per-note MTR source must select the right wave and its native override.
    auto without=multi;without.tracks[0].setupData.clear();
    check(renderAll(multi)!=renderAll(without),"first MTR's override affects its own Audio despite later MTR initialization");
    auto centered=multi;centered.tracks[0].setupData.clear();centered.tracks[1].setupData.clear();
    auto firstRight=multi;firstRight.tracks[0].setupData.clear();
    appendExclusive(firstRight,message(0,0,127));
    check(renderAll(firstRight)!=renderAll(centered),"both originating MTRs retain their own native Audio pan namespace");
}
void invalid() {
    auto base=audio();appendExclusive(base,message(0,0,0));const auto expected=renderAll(base);
    for(int raw=128;raw<=255;++raw)for(int field:{5,6,7}) {
        auto msg=message(0,0,127);msg[size_t(field)]=uint8_t(raw);auto f=base;appendExclusive(f,msg);
        check(renderAll(f)==expected,"invalid native Audio pan keeps prior override");
        MaPlayer p;p.init(f,8000);check(!p.scoreAudit().scanComplete,"bad native pan is not complete coverage");
    }
    for(int id=32;id<128;++id){auto f=base;appendExclusive(f,message(id,1,0));check(renderAll(f)==expected,"invalid AudioID cannot clear prior pan");}
    for(int mode=3;mode<128;++mode){auto f=base;appendExclusive(f,message(0,mode,0));check(renderAll(f)==expected,"reserved clear mode cannot mutate state");}
    for(size_t len=5;len<8;++len){auto f=base;auto msg=message(0,0,127);msg.resize(len);appendExclusive(f,msg);check(renderAll(f)==expected,"short known Audio pan payload cannot mutate state");}
    auto extra=base;auto msg=message(0,0,127);msg.push_back(0);appendExclusive(extra,msg);check(renderAll(extra)==expected,"unverified extended Audio pan shape is rejected");
    auto inlineSet=audio();auto seq=timed(0,message(0,0,0));append(seq,inlineSet.tracks[0].sequenceData);inlineSet.tracks[0].sequenceData=seq;
    MaPlayer p;p.init(inlineSet,8000);bool found=false;for(const auto& g:p.scoreAudit().groups)
        if(g.opcode==0x080b)found=g.domain==ScoreAudit::Domain::Sequence&&g.status==ScoreAudit::Status::Routed;
    check(found,"zero-time inline pan is audited as sequence, not setup");
    auto flood=fixture(3);flood.tracks[0].sequenceData={0,0xff,0x2f,0};
    const auto packet=timed(0,message(0,0,64));
    // Setup has no Duration byte. Original 4.4MB input stays below file cap.
    for(int i=0;i<400001;++i)flood.tracks[0].setupData.insert(flood.tracks[0].setupData.end(),packet.begin()+1,packet.end());
    check(p.init(flood,8000)&&!p.scoreAudit().scanComplete&&
          p.scoreAudit().totals[size_t(ScoreAudit::Status::ResourceLimit)]>0,
          "native Audio pan flood is bounded by decoded event limit");
    check(drain(p).empty()&&p.diagnostics().audioNotes==0,"control-only resource flood cannot create a voice");
}
void reuse() {
    auto f=audio(),extra=audio(1,1);f.tracks[0].waves.push_back(extra.tracks[0].waves[0]);
    f.tracks[0].sequenceData.resize(f.tracks[0].sequenceData.size()-4);
    const auto& second=extra.tracks[0].sequenceData;
    f.tracks[0].sequenceData.insert(f.tracks[0].sequenceData.end(),second.begin(),second.end()-4);
    const size_t initialSequenceBytes=f.tracks[0].sequenceData.size();
    append(f.tracks[0].sequenceData,timed(10,message(0,0,127)));
    append(f.tracks[0].sequenceData,timed(2,message(31,1,64)));
    append(f.tracks[0].sequenceData,{30,0xff,0x2f,0});
    appendExclusive(f,message(0,0,0));
    auto legacy=audio(0,0,2);legacy.tracks[0].trackNumber=2;
    legacy.tracks[0].sequenceData[0]=5;f.tracks.push_back(legacy.tracks[0]);
    auto oracle=f;
    auto& seq=oracle.tracks[0].sequenceData;
    seq.resize(initialSequenceBytes); // preserve the two initial bank/bank/note groups exactly
    append(seq,{10,0xff,0,2,0xff,0,30,0xff,0x2f,0});
    check(renderAll(f)==renderAll(oracle),"native pan cannot retarget a slot stolen by legacy recorded Audio");
    MaPlayer p;check(p.init(f,8000),"Audio reuse initializes");drain(p);
    check(p.diagnostics().audioNotes==3&&p.diagnostics().stolenAudio==1,
          "reuse oracle actually fills both native Audio slots and steals one with legacy Audio");
}
}
void checkMa7PanNext(){ma7PanNext::silentBank();ma7PanNext::pan();ma7PanNext::live();ma7PanNext::invalid();ma7PanNext::reuse();}
