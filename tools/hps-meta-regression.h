// SPDX-License-Identifier: Apache-2.0
// Original synthetic HPS reserved-FF boundary fixtures, not song data.
#pragma once
namespace hpsMetaRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& more){out.insert(out.end(),more.begin(),more.end());}
SmafFile score(const Bytes& sequence,int voice=0) {
    auto f=ma7DataRegression::score(0,sequence,voice);
    f.tracks[0].channelStatus={0,0};return f;
}
void suffix() {
    for(int voice:{0,1,2})for(int subtype=1;subtype<=255;++subtype) {
        if(subtype==0xf0)continue;
        const Bytes prefix={0,0,0x37,32,0,0x21,30};
        auto seq=prefix;append(seq,{5,0xff,uint8_t(subtype),0,
            0,0,0x37,0,0,0x64,60,0,0,0,0});
        auto f=score(seq,voice),ref=score(prefix,voice);
        const auto expected=renderAll(ref);
        check(ma7DataRegression::audible(expected)&&renderAll(f)==expected,
              "reserved HPS FF cannot invent a MIDI meta length, suffix note or prefix mute");
        if(subtype==1) {
            MaPlayer p;check(p.init(f,8000),"reserved HPS FF retains playable prefix");
            auto first=ma7DataRegression::rerender(p,31);p.seekToStart();
            check(first==ma7DataRegression::rerender(p,1024),"reserved HPS FF seek/block invariance");
            auto other=score({0,0x24,40},voice);other.tracks[0].trackNumber=1;
            f.tracks.push_back(other.tracks[0]);ref.tracks.push_back(other.tracks[0]);
            check(renderAll(f)==renderAll(ref),"reserved HPS FF stops only its source stream");
            // A later ordinary Mobile track is independently decoded as well.
            auto mobile=ma7DataRegression::score(2,{0,0x91,67,100,40},voice);
            mobile.tracks[0].trackNumber=1;
            f.tracks.push_back(mobile.tracks[0]);ref.tracks.push_back(mobile.tracks[0]);
            check(renderAll(f)==renderAll(ref),"reserved HPS FF cannot stop a separate modern track");
        }
    }
    for(const Bytes& fakeLength:std::vector<Bytes>{{0},{1,0},{127},{128,0},{255,255,127}}) {
        const Bytes prefix={0,0x21,30};auto seq=prefix;
        append(seq,{5,0xff,0x51});append(seq,fakeLength);append(seq,{0,0x24,60});
        check(renderAll(score(seq,1))==renderAll(score(prefix,1)),
              "HPS reserved FF has no invented single-byte or VLQ payload framing");
    }
    for(int subtype:{1,0x2f,0x51,0xff}) {
        auto f=score({0,0xff,uint8_t(subtype),0,0,0x21,30});
        MaPlayer p;check(!p.init(f,8000),"reserved-only HPS FF cannot manufacture playable suffix");
        check(p.diagnostics().fmNotes+p.diagnostics().pcmNotes==0,
              "reserved-only HPS FF allocates no musical voice");
    }
}
void inventory() {
    using S=ScoreAudit::Status;using D=ScoreAudit::Domain;
    for(int subtype=1;subtype<=255;++subtype) {
        if(subtype==0xf0)continue;
        for(const Bytes& tail:std::vector<Bytes>{{},{0,0,0x24,10}}) {
            Bytes seq={0,0x21,30,5,0xff,uint8_t(subtype)};append(seq,tail);
            MaPlayer p;check(p.init(score(seq),8000),"reserved FF audit prefix initializes");
            const auto& a=p.scoreAudit();
            check(!a.scanComplete&&a.totals[size_t(S::Reserved)]==1&&
                  a.totals[size_t(S::Malformed)]==0&&a.groups.size()==1,
                  "HPS reserved FF has one reserved/incomplete audit, not fictitious truncation");
            const auto& g=a.groups[0];
            check(g.domain==D::Sequence&&g.opcode==0xff&&g.minValue==subtype&&
                  g.maxValue==subtype&&g.firstSample==400,
                  "HPS reserved FF audit retains subtype and authored timestamp");
            auto totals=a.totals;p.seekToStart();
            check(p.scoreAudit().totals==totals,"reserved FF inventory stable after seek");
        }
    }
    MaPlayer p;check(p.init(score({0,0x21,30,5,0xff}),8000)&&
        !p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(S::Malformed)]==1,
        "missing HPS FF subtype is still known truncation");
}
void unchanged() {
    // Known FF00 NOP, FFF0 exclusive and 00000000 EOS keep their grammars.
    for(int voice:{0,1,2}) {
        auto f=score({0,0x21,30,10,0xff,0,10,0x24,20,20,0,0,0},voice);
        auto ref=score({0,0x21,30,20,0x24,20,20,0,0,0},voice);
        check(renderAll(f)==renderAll(ref),"defined HPS NOP/EOS timing remains unchanged");
        auto exclusive=score({0,0x21,30,5,0xff,0xf0,1,0xf7,5,0x24,20},voice);
        auto nop=score({0,0x21,30,5,0xff,0,5,0x24,20},voice);
        check(renderAll(exclusive)==renderAll(nop),"defined HPS exclusive retains its bounded payload and resumes");
    }
    // MA7's pre-existing framed compatibility is deliberately a separate rule.
    mobileMetaRegression::unchanged();
    checkMobileEosClock();checkHpsControls();
}
}
void checkHpsMeta(){hpsMetaRegression::suffix();hpsMetaRegression::inventory();hpsMetaRegression::unchanged();}
