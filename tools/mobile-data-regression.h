// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic Mobile 7-bit field fixtures; no real music/ROM data.
namespace mobileDataRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& data){out.insert(out.end(),data.begin(),data.end());}
SmafFile score(const Bytes& bytes,int voice=0){return ma7DataRegression::score(2,bytes,voice);}
void audited(const SmafFile& f,ScoreAudit::Domain domain) {
    MaPlayer p;check(p.init(f,8000),"Mobile invalid fields retain a playable timed prefix");
    check(!p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]==1,
          "Mobile invalid field is malformed exactly once despite bounded recovery");
    bool found=false;for(const auto& g:p.scoreAudit().groups)if(g.domain==domain&&g.status==ScoreAudit::Status::Malformed)found=true;
    check(found,"Mobile invalid field preserves its audit domain");
}
void controls() {
    for(int voice:{0,1,2})for(int channel:{0,15})for(int raw=128;raw<256;++raw) {
        const uint8_t cc=uint8_t(0xb0|channel),pc=uint8_t(0xc0|channel),
                      bend=uint8_t(0xe0|channel),on=uint8_t(0x90|channel),r=uint8_t(raw);
        for(const auto& bad:std::vector<Bytes>{{5,pc,r},{5,bend,r,72},{5,bend,0,r},
                {5,cc,0,r},{5,cc,32,r},{5,cc,7,r},{5,cc,10,r},{5,cc,11,r},
                {5,cc,1,r},{5,cc,15,r},{5,cc,r,0}}) {
            Bytes prefix={0,pc,0,0,cc,7,50,0,cc,11,70,0,cc,10,25,
                          0,bend,0,72,0,on,60,100,30};
            Bytes actual=prefix,oracle=prefix;append(actual,bad);append(oracle,{5,0xff,0});
            const Bytes suffix={5,uint8_t(0x80|channel),64,15,30,0xff,0x2f,0};append(actual,suffix);append(oracle,suffix);
            auto f=score(actual,voice),ref=score(oracle,voice);
            check(renderAll(f)==renderAll(ref),"Mobile invalid program/control/bend cannot wrap or alter live state/following time");
            audited(f,bad[1]==cc?ScoreAudit::Domain::Controller:ScoreAudit::Domain::Sequence);
            if(raw==128&&voice==2&&channel==15) {
                check(renderAll(channelModeRegression::compressed(f))==renderAll(ref),"packed invalid Mobile controls keep same sound");
                MaPlayer p;check(p.init(f,8000),"invalid controls seek initializes");
                auto first=ma7DataRegression::rerender(p,31);p.seekToStart();
                check(first==ma7DataRegression::rerender(p,1024),"Mobile invalid control seek/block invariance");
                auto other=score({0,0x91,67,100,30},voice);other.tracks[0].trackNumber=1;
                f.tracks.push_back(other.tracks[0]);ref.tracks.push_back(other.tracks[0]);
                check(renderAll(f)==renderAll(ref),"invalid Mobile data cannot damage independent track");
            }
        }
    }
    // Also protect channel modes/RPN/NRPN: malformed bytes are not a controller
    // semantic value (unsupported/unapplied), nor a valid reset/stop request.
    for(int cc:{6,38,64,98,99,100,101,120,121,123})for(int raw:{128,255}) {
        Bytes prefix={0,0x90,60,100,30},actual=prefix,oracle=prefix;
        append(actual,{5,0xb0,uint8_t(cc),uint8_t(raw)});append(oracle,{5,0xff,0});
        append(actual,{30,0xff,0});append(oracle,{30,0xff,0});
        auto f=score(actual);check(renderAll(f)==renderAll(score(oracle)),"invalid mode/controller preserves active notes and authored time");
        audited(f,ScoreAudit::Domain::Controller);
    }
}
void notes() {
    for(int voice:{0,1,2})for(int channel:{0,15})for(int raw=128;raw<256;++raw) {
        uint8_t on=uint8_t(0x90|channel),running=uint8_t(0x80|channel),r=uint8_t(raw);
        for(const auto& bad:std::vector<Bytes>{{5,on,r,1,10},{5,on,60,r,10},{5,running,r,10},
                                             {5,on,r,1,0},{5,on,60,r,0}}) {
            Bytes prefix={0,on,60,64,3},actual=prefix,oracle=prefix;
            append(actual,bad);append(oracle,{5,0xff,0});
            const Bytes suffix={10,running,64,10,20,0xff,0};append(actual,suffix);append(oracle,suffix);
            auto f=score(actual,voice),ref=score(oracle,voice);
            check(renderAll(f)==renderAll(ref),"invalid Mobile note/velocity cannot sound or poison remembered velocity, including gate0");
            audited(f,ScoreAudit::Domain::Sequence);
            if(raw==255&&channel==15&&voice==1)check(renderAll(channelModeRegression::compressed(f))==renderAll(ref),
                "packed invalid Mobile note retains running velocity");
        }
        // A complete malformed note at the chunk end still gets a timed NOP
        // audit, even with gate0. There is no next note to expose poisoned state.
        auto actual=score({0,on,60,64,3,5,on,60,r,0},voice);
        auto oracle=score({0,on,60,64,3,5,0xff,0},voice);
        check(renderAll(actual)==renderAll(oracle),"terminal invalid note retains rest clock without allocation");audited(actual,ScoreAudit::Domain::Sequence);
    }
    // A broken gate is not a successfully delimited malformed note. It stops
    // this track and must not insert a NOP or commit a new running velocity.
    for(const auto& gate:std::vector<Bytes>{{0x81},{0x80,0x80,0x80,0x80,0x80,0,0x90,67,100,10}}) {
        Bytes prefix={0,0x90,60,64,3},broken=prefix;append(broken,{5,0x90,60,128});append(broken,gate);
        auto f=score(broken,2);check(renderAll(f)==renderAll(score(prefix,2)),"broken gate takes priority over recovery and cannot create terminal rest/fake suffix");
        audited(f,ScoreAudit::Domain::Sequence);
    }
    for(const auto& tail:std::vector<Bytes>{{0,0xc0},{0,0xe0,0},{0,0xb0,7},
            {0,0x90,60},{0,0x90,60,128},{0,0x80}}) {
        MaPlayer p;p.init(score(tail),8000);
        check(!p.scoreAudit().scanComplete&&!ma7DataRegression::audible(ma7DataRegression::rerender(p,31)),
              "truncated Mobile fixed fields/gate are bounded and cannot fabricate notes");
    }
}
void valid() {
    for(int channel=0;channel<16;++channel)for(int key:{0,60,114,115,127}) {
        uint8_t on=uint8_t(0x90|channel),running=uint8_t(0x80|channel);
        auto actual=score({0,on,uint8_t(key),1,0,5,running,uint8_t(key),10});
        auto oracle=score({5,on,uint8_t(key),1,10});
        check(renderAll(actual)==renderAll(oracle),"valid Mobile gate0 still remembers velocity1 for following compressed note");
        MaPlayer p;check(p.init(actual,8000)&&p.scoreAudit().scanComplete,"all legal7bit keys retain complete coverage");
    }
    for(int vel:{0,1,64,127}) {
        auto f=score({0,0x90,60,uint8_t(vel),10},1);MaPlayer p;
        check(p.init(f,8000)&&p.scoreAudit().scanComplete,"all legal Mobile velocity boundaries accepted");
        check(ma7DataRegression::audible(renderAll(f))==(vel!=0),"Mobile velocity1 stays audible, unlike MA7 exact mute");
    }
}
}
void checkMobileData(){mobileDataRegression::controls();mobileDataRegression::notes();mobileDataRegression::valid();}
