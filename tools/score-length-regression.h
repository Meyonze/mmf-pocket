// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original transport fixtures only; no music, ROM or per-song sound rules.
namespace scoreLengthRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& bytes){out.insert(out.end(),bytes.begin(),bytes.end());}
Bytes vlq(uint64_t value,int width=0) {
    Bytes result{uint8_t(value&127)};
    while((value>>=7)!=0)result.insert(result.begin(),uint8_t(128|(value&127)));
    while(int(result.size())<width)result.insert(result.begin(),128);
    return result;
}
SmafFile score(int format,const Bytes& sequence,bool pcm=false) {
    auto f=fixture(format);f.tracks[0].sequenceData=sequence;
    if(pcm)addPcmFixture(f);
    return f;
}
std::vector<float> drain(MaPlayer& player,int block=257) {
    std::vector<float> result,buffer(size_t(block)*2);int count;
    while((count=player.render(buffer.data(),block))>0) {
        result.insert(result.end(),buffer.begin(),buffer.begin()+2*count);
        check(result.size()<8000*2*10,"length recovery fixture bounded");
    }
    return result;
}
void rejected(const SmafFile& file) {
    MaPlayer p;bool initialized=p.init(file,8000);
    check(!p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]>0,
          "overflow length is malformed, never silently narrowed");
    if(initialized)check(drain(p).empty(),"overflow length cannot manufacture playable commands");
    check(p.diagnostics().fmNotes+p.diagnostics().pcmNotes+p.diagnostics().audioNotes==0,
          "overflow duration/gate/payload cannot allocate a voice");
}
void overflow() {
    for(int format:{2,3})for(int lead=16;lead<128;++lead)for(uint32_t tail:{0u,1u,10u,127u}) {
        const uint64_t value=(uint64_t(lead)<<28)|tail;
        const Bytes bad=vlq(value);check(bad.size()==5,"overflow oracle uses exactly five terminal bytes");
        const uint8_t on=format==3?0x10:0x90;
        Bytes seq={0,on,60,100};append(seq,bad);append(seq,{10,0xff,0x2f,0});
        auto gate=score(format,seq);rejected(gate);
        if(format==2)rejected(channelModeRegression::compressed(gate));
        seq=bad;append(seq,{on,60,100,10,10,0xff,0x2f,0});
        auto duration=score(format,seq);rejected(duration);
        if(format==2)rejected(channelModeRegression::compressed(duration));
        seq={0,0xf0};append(seq,bad);append(seq,{0xf7,0,on,60,100,10,10,0xff,0x2f,0});
        rejected(score(format,seq));
        if(format==3) {
            seq={0,0xff,1};append(seq,bad);append(seq,{0,on,60,100,10,10,0xff,0x2f,0});
            rejected(score(format,seq));
        }
    }
    // UINT32_MAX+1, +2, +11 and the largest five-group integer are explicit
    // boundaries independent of the loop's first-group construction.
    for(uint64_t value:{uint64_t(0x100000000),uint64_t(0x100000001),
                        uint64_t(0x10000000a),uint64_t(0x7ffffffff)})for(int format:{2,3}) {
        const uint8_t on=format==3?0x10:0x90;
        Bytes seq={0,on,60,100};append(seq,vlq(value));append(seq,{10,0xff,0x2f,0});
        rejected(score(format,seq,true));
    }
}
void recovery() {
    for(int format:{2,3})for(bool pcm:{false,true}) {
        const uint8_t on=format==3?0x10:0x90;
        // The broken suffix has no accepted timed EOS; compare only the valid
        // prefix, not a separately authored later endpoint.
        const auto expected=score(format,{0,on,60,100,3},pcm);
        const auto original=renderAll(expected);
        for(bool gate:{false,true}) {
            Bytes sequence={0,on,60,100,3};
            if(gate)append(sequence,{10,on,64,127});
            append(sequence,vlq(0x10000000a));
            append(sequence,{on,67,100,10,10,0xff,0x2f,0});
            auto broken=score(format,sequence,pcm);
            check(renderAll(broken)==original,"overflow cannot damage normal prefix or render its fake suffix");
            MaPlayer p;check(p.init(broken,8000),"valid prefix remains playable");
            auto a=drain(p,31);p.seekToStart();check(a==drain(p,1024),"overflow-prefix seek and block invariance");
            check(p.init(expected,8000)&&drain(p)==original,"fresh init removes malformed-input state");
            auto other=score(format,{0,uint8_t(on|1),67,100,3,10,0xff,0x2f,0},pcm);
            other.tracks[0].trackNumber=1;
            auto multi=expected;multi.tracks.push_back(other.tracks[0]);broken.tracks.push_back(other.tracks[0]);
            check(renderAll(broken)==renderAll(multi),"overflow stops only its source stream, not other tracks");
        }
        auto setup=expected;
        setup.tracks[0].setupData.push_back(0xf0);append(setup.tracks[0].setupData,vlq(0x100000001));
        append(setup.tracks[0].setupData,{0xf7});
        // A later apparently legal patch is still inside an unknowable oversized
        // payload. Stop this setup stream; do not resynchronize by guessing.
        auto replacement=protocolRegression::tone();replacement[10]=2;
        appendExclusive(setup,replacement);
        check(renderAll(setup)==original,"overflow setup keeps prior definitions and independent valid score");
        MaPlayer p;p.init(setup,8000);check(!p.scoreAudit().scanComplete,"overflow setup is visible in coverage");
    }
}
void valid() {
    // Do not impose a new MIDI four-byte restriction. Every accepted32-bit
    // value, including five-byte leading-zero groups, stays representable.
    for(int format:{2,3})for(uint32_t value:{0u,1u,10u,127u,128u,0xfffffffu,0x10000000u,0xffffffffu}) {
        const uint8_t on=format==3?0x10:0x90;
        for(bool gate:{false,true}) {
            auto sequence=[&](int width) {
                Bytes seq;
                if(gate)append(seq,{0,on,60,100});
                append(seq,vlq(value,width));
                if(!gate)append(seq,{on,60,100,3});
                append(seq,{10,0xff,0x2f,0});return seq;
            };
            auto canonical=score(format,sequence(0)),padded=score(format,sequence(5));
            MaPlayer a,b;const bool activeA=a.init(canonical,8000),activeB=b.init(padded,8000);
            check(activeA==activeB&&a.scoreAudit().scanComplete&&b.scoreAudit().scanComplete,
                  "terminal five-byte values within32 bits remain legal compatibility input");
            check(a.scoreEndSamples()==b.scoreEndSamples()&&a.totalSamples()==b.totalSamples(),
                  "canonical and padded32-bit clocks agree without wrap");
            const uint64_t expectedTicks=gate
                ? std::max<uint64_t>(value,10)
                : uint64_t(value)+10; // both Mobile/MA7 retain their accepted10-tick EOS
            if(activeA)check(a.scoreEndSamples()==expectedTicks*80,
                  "valid32-bit endpoint matches independently calculated10ms ticks at8kHz");
            if(activeA) {
                std::array<float,512> left{},right{};
                int na=a.render(left.data(),256),nb=b.render(right.data(),256);
                check(na==nb&&left==right,"valid huge clocks checked with a bounded prefix, not a long render");
                if(value>=0x10000000u)check(a.scoreEndSamples()>uint64_t(600*8000)&&a.totalSamples()<=600*8000,
                      "large valid gate/duration retains its real endpoint and the existing safety cap");
            }
        }
        auto base=score(format,{0,on,60,100,3,10,0xff,0x2f,0});
        setVoice(base,protocolRegression::tone());auto padded=base;
        auto& setup=padded.tracks[0].setupData;
        const uint64_t length=setup[1];Bytes newSetup={0xf0};append(newSetup,vlq(length,5));
        newSetup.insert(newSetup.end(),setup.begin()+2,setup.end());setup=std::move(newSetup);
        check(renderAll(base)==renderAll(padded),"valid padded setup length preserves custom tone");
    }
    // Existing HPS two-byte length arithmetic is a separate format and stays intact.
    const auto a=atrPanRegression::hps({0,0x21,1,10});
    check(renderAll(a)==renderAll(a,31),"HPS transport remains block-invariant");
}
}
void checkScoreLengths(){scoreLengthRegression::overflow();scoreLengthRegression::recovery();scoreLengthRegression::valid();}
