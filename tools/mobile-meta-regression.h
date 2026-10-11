// SPDX-License-Identifier: Apache-2.0
// Original synthetic Mobile FF-subtype boundary fixtures only.
#pragma once
namespace mobileMetaRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& more){out.insert(out.end(),more.begin(),more.end());}
SmafFile score(const Bytes& bytes,int voice=0){return ma7DataRegression::score(2,bytes,voice);}
void suffix() {
    for(int voice:{0,1,2})for(int subtype=1;subtype<=255;++subtype) {
        if(subtype==0x2f)continue;
        Bytes prefix={0,0x90,60,80,30},seq=prefix;
        append(seq,{5,0xff,uint8_t(subtype),0,0,0xb0,7,0,
                    0,0x91,72,127,60,0,0xff,0x2f,0});
        auto f=score(seq,voice),oracle=score(prefix,voice);
        check(ma7DataRegression::audible(renderAll(oracle))&&renderAll(f)==renderAll(oracle),
              "unknown Mobile FF subtype cannot fabricate suffix notes or alter prefix gain");
        if(subtype==1) {
            check(renderAll(channelModeRegression::compressed(f))==renderAll(oracle),
                  "packed Mobile unknown meta preserves prefix audio");
            MaPlayer p;check(p.init(f,8000),"unknown meta preserves playable prefix");
            auto audio=ma7DataRegression::rerender(p,31);p.seekToStart();
            check(audio==ma7DataRegression::rerender(p,1024),"unknown meta seek/block invariance");
            auto other=score({0,0x91,67,100,40},voice);other.tracks[0].trackNumber=1;
            f.tracks.push_back(other.tracks[0]);oracle.tracks.push_back(other.tracks[0]);
            check(renderAll(f)==renderAll(oracle),"unknown meta stops only its own stream");
        }
    }
    for(const Bytes& length:std::vector<Bytes>{{0},{1,0},{127},{128,0},{255,255,127}}) {
        Bytes prefix={0,0x90,60,100,30},seq=prefix;
        append(seq,{5,0xff,0x51});append(seq,length);
        append(seq,{0,0x91,72,127,60});
        check(renderAll(score(seq,1))==renderAll(score(prefix,1)),
              "unknown Mobile meta has neither invented byte nor VLQ payload framing");
    }
}
void inventory() {
    using S=ScoreAudit::Status;using D=ScoreAudit::Domain;
    for(int subtype=1;subtype<=255;++subtype) {
        if(subtype==0x2f)continue;
        // Unknown subtype is unsupported even at exact end: there is no
        // specified length byte whose absence could be called truncation.
        for(const Bytes& tail:std::vector<Bytes>{{},{0,0,0x90,64,100,10}}) {
            Bytes seq={0,0x90,60,100,30,5,0xff,uint8_t(subtype)};append(seq,tail);
            MaPlayer p;check(p.init(score(seq),8000),"unsupported subtype prefix initializes");
            const auto& a=p.scoreAudit();
            check(!a.scanComplete&&a.totals[size_t(S::Unsupported)]==1&&
                  a.totals[size_t(S::Malformed)]==0&&a.groups.size()==1,
                  "unknown Mobile subtype has one unsupported/incomplete diagnostic");
            const auto& g=a.groups[0];
            check(g.domain==D::Sequence&&g.opcode==0xff&&g.channel==15&&
                  g.minValue==subtype&&g.maxValue==subtype&&g.firstSample==400,
                  "unknown FF audit retains subtype, channel and authored time");
            auto totals=a.totals;p.seekToStart();
            check(p.scoreAudit().totals==totals,"unknown meta inventory survives seek");
        }
    }
    MaPlayer p;check(p.init(score({0,0x90,60,100,30,5,0xff}),8000)&&
        !p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(S::Malformed)]==1,
        "missing FF subtype remains a known truncation");
}
void unchanged() {
    checkScoreNops();checkMobileBoundaries();
    // MA7 keeps its existing VLQ-framed meta compatibility, not Mobile rules.
    for(int voice:{0,1,2}) {
        auto f=ma7DataRegression::score(3,{0,0x10,60,100,30,5,0xff,0x51,3,
            7,0xa1,0x20,0,0x10,64,100,10,40,0xff,0},voice);
        auto oracle=ma7DataRegression::score(3,{0,0x10,60,100,30,5,0xff,0,
            0,0x10,64,100,10,40,0xff,0},voice);
        check(renderAll(f)==renderAll(oracle),"MA7 compatibility meta framing unchanged");
        MaPlayer p;check(p.init(f,8000)&&p.scoreAudit().scanComplete,
                        "MA7 compatibility retains complete scan");
    }
    checkAtrPan();
}
}
void checkMobileMeta(){mobileMetaRegression::suffix();mobileMetaRegression::inventory();mobileMetaRegression::unchanged();}
