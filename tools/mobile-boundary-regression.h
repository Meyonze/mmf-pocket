// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic Mobile score fixtures; no copyrighted music.
namespace mobileBoundaryRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& data){out.insert(out.end(),data.begin(),data.end());}
SmafFile score(const Bytes& data,bool pcm=false) {
    auto f=fixture();f.tracks[0].sequenceData=data;
    if(pcm)addPcmFixture(f,1,15);
    return f;
}
Bytes prefix(){return {0,0x90,60,100,10};}
void invalid(int first=0,int limit=256) {
    for(bool pcm:{false,true}) {
        const auto before=score(prefix(),pcm);const auto expected=renderAll(before);
        for(int status=first;status<limit;++status) {
            if(status>=128&&(status<241||status==255))continue;
            auto bytes=prefix();append(bytes,{5,uint8_t(status)});
            // An arbitrary unknown payload begins with an apparently valid
            // duration/status/note. It must not be decoded by resynchronizing.
            append(bytes,{0,0x90,67,127,100, 0,0xb0,7,0});
            auto broken=score(bytes,pcm);MaPlayer p;
            check(p.init(broken,8000),"invalid Mobile status preserves playable prefix");
            const auto kind=status<128?ScoreAudit::Status::Malformed:ScoreAudit::Status::Unsupported;
            check(!p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(kind)]==1,
                  "unknown-width Mobile status audited once with incomplete coverage");
            check(renderAll(broken)==expected,"invalid status cannot fabricate suffix notes or change prefix volume/gates");
            p.seekToStart();std::vector<float> replay,buffer(62);int n;
            while((n=p.render(buffer.data(),31))>0)replay.insert(replay.end(),buffer.begin(),buffer.begin()+2*n);
            check(replay==expected,"invalid Mobile status preserves deterministic seek and block behavior");
            if(status==0||status==127||status==241||status==254) {
                auto packed=channelModeRegression::compressed(broken);
                check(renderAll(packed)==expected,"packed Mobile rejects same false suffix");
                check(p.init(packed,8000)&&!p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(kind)]==1,
                      "packed Mobile retains unknown-status audit");
                auto independent=score({0,0x90,64,100,20},pcm);independent.tracks[0].trackNumber=1;
                auto oracle=before,mixed=broken;
                oracle.tracks.push_back(independent.tracks[0]);mixed.tracks.push_back(independent.tracks[0]);
                check(renderAll(mixed)==renderAll(oracle),"bad Mobile stream cannot discard independent track");
                check(p.init(mixed,8000)&&!p.scoreAudit().scanComplete,"independent track does not hide incomplete scan");
            }
            auto tail=prefix();append(tail,{5,uint8_t(status)});
            auto truncated=score(tail,pcm);
            check(renderAll(truncated)==expected,"unknown status at chunk end preserves previous natural release");
            check(p.init(truncated,8000)&&p.scoreAudit().totals[size_t(kind)]==1&&!p.scoreAudit().scanComplete,
                  "terminal unknown status is audited once, without synthetic truncation error");
        }
    }
}
void valid() {
    for(bool pcm:{false,true})for(int channel=0;channel<16;++channel)for(int high:{0xa0,0xd0}) {
        auto bytes=prefix();append(bytes,{5,uint8_t(high|channel),0xf1});
        if(high==0xa0)bytes.push_back(0xff); // payload is opaque, not a status
        append(bytes,{5,0x90,64,100,10});
        auto oracleBytes=prefix();append(oracleBytes,{10,0x90,64,100,10});
        auto f=score(bytes,pcm);MaPlayer p;
        check(renderAll(f)==renderAll(score(oracleBytes,pcm)),"known reserved width skips exact payload and keeps following note clock");
        check(p.init(f,8000)&&p.scoreAudit().scanComplete&&
              p.scoreAudit().totals[size_t(ScoreAudit::Status::Reserved)]==1,
              "A0/D0 reserved width remains complete and distinct from unsupported width");
        check(renderAll(channelModeRegression::compressed(f))==renderAll(f),"known reserved payload unaffected by packed transport");
    }
    // Bit7=0 is valid in MA7's separate channel-bank grammar. Never apply
    // Mobile's status rule to MA7, HPS or ATR.
    for(int channel=0;channel<32;++channel) {
        auto f=fixture(3),oracle=fixture(3);
        uint8_t bank=channel>=16?0x80:0;
        f.tracks[0].sequenceData={0,uint8_t(bank|0x10|(channel&15)),60,100,10,
                                 10,uint8_t(bank|(channel&15)),64,10};
        oracle.tracks[0].sequenceData={0,uint8_t(bank|0x10|(channel&15)),60,100,10,
                                      10,uint8_t(bank|0x10|(channel&15)),64,100,10};
        check(renderAll(f)==renderAll(oracle),"MA7 low/high bank and running-velocity notes remain legal");
        MaPlayer p;check(p.init(f,8000)&&p.scoreAudit().scanComplete,"MA7 channel grammar unaffected");
    }
}
}
void checkMobileBoundaries(){mobileBoundaryRegression::invalid();mobileBoundaryRegression::valid();}
