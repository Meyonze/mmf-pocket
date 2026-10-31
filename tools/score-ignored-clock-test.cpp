// SPDX-License-Identifier: Apache-2.0
// Original score-clock fixtures; no private songs or hardware sound oracle.
#include "ma_player.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
namespace {
void require(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
SmafFile score(int format,const Bytes& command,bool note=true,bool eos=false,int bank=0,bool sustained=false) {
    SmafFile file;TrackChunk track;track.formatType=format;track.durationTimeBase=track.gateTimeBase=0x10;
    track.channelStatus.resize(format==3?32:16);
    // Original VM5 voice: inactive AR0 modulator and fast RR15 sine carrier.
    // Its audio/filter tail ends before the terminal one-second command.
    Bytes voice={0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,
        0,0xf0,0,0xfc,0,0x10,0, 0,0xf0,0xf0,0x30,0,0x10,0};
    if(sustained)voice[21]=0; // RR0: existing safety tail, not guessed decay
    track.setupData={0xf0,uint8_t(voice.size()+1)};
    track.setupData.insert(track.setupData.end(),voice.begin(),voice.end());track.setupData.push_back(0xf7);
    if(note)track.sequenceData={0,uint8_t((format==3?0x10:0x90)|bank),60,100,3};
    track.sequenceData.push_back(100);
    track.sequenceData.insert(track.sequenceData.end(),command.begin(),command.end());
    if(eos)track.sequenceData.insert(track.sequenceData.end(),{50,0xff,0x2f,0});
    file.tracks.push_back(track);return file;
}
SmafFile packed(SmafFile file) {
    const auto plain=file.tracks[0].sequenceData;
    const uint32_t length=uint32_t(plain.size());
    Bytes data{uint8_t(length>>24),uint8_t(length>>16),uint8_t(length>>8),uint8_t(length)};
    size_t bit=32;
    auto emit=[&](unsigned value){if(bit/8>=data.size())data.push_back(0);data[bit/8]|=uint8_t((value&1)<<(7-(bit&7)));++bit;};
    auto byte=[&](unsigned value){for(int i=7;i>=0;--i)emit(value>>i);};
    auto tree=[&](auto&& self,unsigned first,unsigned count)->void {
        if(count==1){emit(0);byte(first);return;}
        emit(1);self(self,first,count/2);self(self,first+count/2,count/2);
    };
    tree(tree,0,256);for(uint8_t value:plain)byte(value);
    file.tracks[0].formatType=1;file.tracks[0].sequenceData=std::move(data);return file;
}
std::vector<float> drain(MaPlayer& player,int block) {
    std::vector<float> result,buffer(size_t(block)*2);int count;
    while((count=player.render(buffer.data(),block))>0){
        result.insert(result.end(),buffer.begin(),buffer.begin()+2*count);
        require(result.size()<300000,"clock fixture finishes within bounded duration");
    }
    require(!player.diagnostics().tailLimitReached,"clock fixture does not hit safety tail");return result;
}
std::vector<float> render(const SmafFile& file,unsigned rate,int block=257) {
    MaPlayer player;require(player.init(file,rate),"clock fixture initializes");return drain(player,block);
}
void clocks() {
    using Status=ScoreAudit::Status;using Domain=ScoreAudit::Domain;
    for(int format:{2,3})for(unsigned rate:{8000u,44100u})for(int bank:{0,0x80}) {
        if(bank&&(format!=3||rate!=8000))continue;
        const uint8_t control=uint8_t((format==3?0x30:0xb0)|bank);
        const uint8_t two=uint8_t((format==3?0x20:0xa0)|bank),one=uint8_t((format==3?0x50:0xd0)|bank);
        std::vector<std::pair<Bytes,Status>> cases={{{control,77,42},Status::Unsupported},
            {{control,15,25},Status::Unapplied},{{two,0x90,0xff},Status::Reserved},{{one,0xff},Status::Reserved}};
        if(format==2)for(int cc:{120,121,123})cases.push_back({{control,uint8_t(cc),1},Status::Unapplied});
        else for(int cc:{64,120,121,123,101,100,98,99,6,38})cases.push_back({{control,uint8_t(cc),0},Status::Unsupported});
        const auto oracle=score(format,{0xff,0},true,false,bank);
        const auto expected=render(oracle,rate);
        require(expected.size()==size_t(rate)*2,"independent timed-NOP oracle ends at exactly one second");
        for(const auto& [command,status]:cases) {
            const auto file=score(format,command,true,false,bank);
            MaPlayer player;require(player.init(file,rate),"complete ignored command initializes");
            require(player.scoreEndSamples()==rate,"ignored complete command retains authored score clock");
            bool audited=false;
            for(const auto& group:player.scoreAudit().groups)
                if(group.status==status && group.domain==(status==Status::Reserved?Domain::Sequence:Domain::Controller))audited=true;
            require(audited,"unsupported/unapplied/reserved classification remains unchanged");
            require(drain(player,257)==expected,"ignored command equals independent timed-NOP audio oracle");
            if(format==2&&rate==8000)require(render(packed(file),rate)==expected,"compressed transport retains the same clock");
        }
        const auto representative=score(format,{control,77,42},true,false,bank);
        MaPlayer replay;require(replay.init(representative,rate),"replay initializes");
        require(drain(replay,1)==expected,"ignored clock is independent of one-frame rendering");
        replay.seekToStart();require(drain(replay,1024)==expected,"seek restores ignored-event clock");
        const auto silent=score(format,{control,77,42},false,false,bank);
        const auto silence=render(silent,rate);
        require(silence.size()==size_t(rate)*2 && std::all_of(silence.begin(),silence.end(),[](float x){return x==0;}),
                "a complete ignored-only score preserves authored silence");
        require(render(score(format,{control,77,42},true,true,bank),rate)
                ==render(score(format,{0xff,0},true,true,bank),rate),"later EOS retains its existing time and audio");
        for(const Bytes& truncated:std::vector<Bytes>{{control,77},{control},{two,0x90},{two},{one}}) {
            MaPlayer partial;require(partial.init(score(format,truncated,true,false,bank),rate),"partial score preserves its valid note prefix");
            require(partial.scoreEndSamples()==uint64_t(rate)*3/100,"truncated payload does not manufacture a complete timed instruction");
            require(partial.scoreAudit().totals[size_t(Status::Malformed)]>0,"truncated payload remains malformed");
        }
    }
}
void sustainedTail() {
    constexpr unsigned rate=8000,target=rate*10+rate/20;
    for(int format:{2,3}) {
        const uint8_t cc=format==3?0x30:0xb0;
        MaPlayer actual,oracle;
        require(actual.init(score(format,{cc,77,42},true,false,0,true),rate)
                &&oracle.init(score(format,{0xff,0},true,false,0,true),rate),"sustained clock fixtures initialize");
        require(actual.totalSamples()==rate*11,"tail ceiling is based on the retained one-second score endpoint");
        float a[514],b[514];bool audibleAfterOldEnd=false;
        for(unsigned frame=0;frame<target;) {
            const unsigned wanted=std::min(257u,target-frame);
            const int count=actual.render(a,int(wanted));
            require(count==int(wanted)&&oracle.render(b,int(wanted))==count,"sustained sound survives the former early safety endpoint");
            require(std::equal(a,a+2*count,b),"sustained tail and safety fade timing match independent NOP oracle");
            for(int i=0;i<count;++i)if(frame+unsigned(i)>=rate*10+rate*3/100)
                audibleAfterOldEnd|=a[2*i]>1e-4f||a[2*i]<-1e-4f||a[2*i+1]>1e-4f||a[2*i+1]<-1e-4f;
            frame+=unsigned(count);
        }
        require(audibleAfterOldEnd,"corrected tail retains actual nonzero audio beyond the old cutoff");
    }
}
}
int main(){clocks();sustainedTail();std::cout<<"PASS: complete ignored score clocks, compression, audit, EOS, seek, sustained tails and truncated-prefix tests\n";}
