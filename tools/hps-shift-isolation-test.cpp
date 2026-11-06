// SPDX-License-Identifier: Apache-2.0
// Synthetic score fixtures only; no private tunes or manufacturer waveforms.
#include "ma_player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
void require(bool ok,const char* message) {
    if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
std::vector<float> drain(MaPlayer& p,int block=257) {
    std::vector<float> out,buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0) {
        require(out.size()<2000000,"bounded fixture duration");
        for(int i=0;i<n*2;++i)require(std::isfinite(buffer[i]),"finite output");
        out.insert(out.end(),buffer.begin(),buffer.begin()+n*2);
    }
    return out;
}
std::vector<float> render(const SmafFile& f,int rate=8000,int block=257) {
    MaPlayer p;require(p.init(f,rate),"fixture initializes");return drain(p,block);
}
void exclusive(TrackChunk& t,const Bytes& voice) {
    t.setupData={0xf0,uint8_t(voice.size()+1)};
    t.setupData.insert(t.setupData.end(),voice.begin(),voice.end());t.setupData.push_back(0xf7);
}
void compress(TrackChunk& t) {
    const auto plain=t.sequenceData;const auto n=uint32_t(plain.size());
    Bytes packed{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};size_t bit=32;
    auto emit=[&](int value){if(bit/8==packed.size())packed.push_back(0);
        packed[bit/8]|=uint8_t((value&1)<<(7-(bit&7)));++bit;};
    auto byte=[&](int value){for(int i=7;i>=0;--i)emit(value>>i);};
    auto tree=[&](auto&& self,int first,int count)->void {
        if(count==1){emit(0);byte(first);return;}emit(1);
        self(self,first,count/2);self(self,first+count/2,count/2);
    };
    tree(tree,0,256);for(uint8_t value:plain)byte(value);
    require(smafHuffmanInflate(packed.data(),packed.size())==plain,"compressed fixture roundtrips");
    t.formatType=1;t.sequenceData=std::move(packed);
}
SmafFile modern(int format,int kind,int local) {
    SmafFile f;TrackChunk t;t.trackNumber=format==3?2:0;t.formatType=uint8_t(format);
    t.durationTimeBase=t.gateTimeBase=0x10;t.channelStatus.assign(32,0);
    const uint8_t on=uint8_t((format==3?0x10:0x90)|local);
    const uint8_t cc=uint8_t((format==3?0x30:0xb0)|local);
    const uint8_t key=kind==3?0:69;
    t.sequenceData={0,cc,7,100,1,on,key,100,5,11,on,key,100,5,18,0xff,0x2f,0};
    if(kind==1) exclusive(t,{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,1,
        0,0xf0,0xf0,0,0,0x10,0,0,0xf0,0xf0,0,7,0x10,0});
    if(kind==2||kind==3) {
        WaveData w;w.number=1;w.samplingRate=8000;w.bitsPerSample=8;w.adpcm=false;
        for(int i=0;i<800;++i)w.data.push_back(uint8_t(std::lround(128+24*std::sin(i*0.157))));
        t.waves.push_back(w);
        if(kind==2) exclusive(t,{0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0xf0,0xf0,0,0,0,0,0,0,0,80,1});
        else t.sequenceData.insert(t.sequenceData.begin(),{0,cc,0,125,0,cc,32,0});
    }
    if(format==1)compress(t);f.tracks.push_back(t);return f;
}
TrackChunk shift(int local,int value,bool during) {
    TrackChunk t;t.trackNumber=1;t.formatType=0;
    t.durationTimeBase=t.gateTimeBase=0x10;t.channelStatus={0,0};
    const uint8_t op=uint8_t(((local-4)<<6)|0x32);
    // During=after the first note, before the second. Preserve the score clock.
    t.sequenceData={uint8_t(during?8:0),0,op,uint8_t(value),0,0,0,0};return t;
}
int checks=0;
void isolated(int format,int kind,int local,int value,bool first,bool during,int rate=8000) {
    const auto source=modern(format,kind,local);const auto expected=render(source,rate);
    require(std::any_of(expected.begin(),expected.end(),[](float x){return x!=0;}),"audible independent modern oracle");
    auto mixed=source;const auto hps=shift(local,value,during);
    if(first)mixed.tracks.insert(mixed.tracks.begin(),hps);else mixed.tracks.push_back(hps);
    MaPlayer p;require(p.init(mixed,rate),"mixed fixture initializes");
    const auto actual=drain(p);
    require(actual==expected,"HPS octave setting must not transpose Mobile/MA7 notes");++checks;
    const auto& d=p.diagnostics();
    require(kind==2?d.pcmNotes==2&&d.fmNotes==0:
            kind==3?d.audioNotes==2&&d.fmNotes==0:
            d.fmNotes==2&&d.fallbackNotes==uint64_t(kind==0?2:0),"intended source route is exercised");
    require(p.scoreAudit().scanComplete&&p.scoreAudit().decodedTracks==2,"both grammars decoded completely");
    if(local==4&&value==1&&first&&!during) {
        p.seekToStart();require(drain(p,1)==expected,"seek and single-frame rendering preserve isolation");++checks;
        require(p.init(mixed,rate),"same player reinitializes");
        require(drain(p,1024)==expected,"reinitialization and large blocks preserve isolation");++checks;
    }
}
void handyPhone() {
    auto file=[](int value,int octave) {
        SmafFile f;TrackChunk t=shift(4,value,false);
        // A quiet HPS part makes the nonzero shift equivalence measurable.
        t.sequenceData={0,0,0x37,32,0,0,0x32,uint8_t(value),
            1,uint8_t((octave<<4)|9),10,20,0,0,0};f.tracks.push_back(t);return f;
    };
    for(int value:{0,1,2,3,4,0x81,0x82,0x83,0x84}) {
        // Independent equivalent score: move one octave from the controller
        // into the HPS note's own two-bit octave representation.
        const int alternate=value==0?1:value<=4?value-1:value==0x81?0:value-1;
        const int octave=value==0?1:value<=4?3:1;
        require(render(file(value,2))==render(file(alternate,octave)),"HPS authored octave shift is retained");++checks;
        if(value!=0){require(render(file(value,2))!=render(file(0,2)),"HPS shift still changes its own pitch");++checks;}
    }
}
int main() {
    for(int format:{1,2,3})for(int kind=0;kind<4;++kind)
    for(int ch=4;ch<8;++ch)for(int value:{1,4,0x81,0x84})for(bool first:{false,true})
        isolated(format,kind,ch,value,first,(ch&1)!=0);
    for(int format:{1,2,3})for(int value:{0,1,2,3,4,0x81,0x82,0x83,0x84})for(bool during:{false,true})
        isolated(format,0,4,value,true,during);
    for(int format:{1,2,3})for(int kind=0;kind<4;++kind)for(int value:{1,0x81})
        isolated(format,kind,4,value,true,true,44100);
    handyPhone();
    std::cout<<"HPS octave source-isolation checks: "<<checks<<" PASS\n";
}
