// SPDX-License-Identifier: Apache-2.0
// Original Mwa namespace fixtures; no handset/reference-app calibration.
#include "ma_player.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
namespace {
void require(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
Bytes packed(const Bytes& plain) {
    const uint32_t n=uint32_t(plain.size());Bytes data{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};
    size_t bit=32;
    auto emit=[&](unsigned v){if(bit/8>=data.size())data.push_back(0);data[bit/8]|=uint8_t((v&1)<<(7-(bit&7)));++bit;};
    auto byte=[&](unsigned v){for(int i=7;i>=0;--i)emit(v>>i);};
    auto tree=[&](auto&& self,unsigned first,unsigned count)->void{
        if(count==1){emit(0);byte(first);return;}emit(1);self(self,first,count/2);self(self,first+count/2,count/2);
    };
    tree(tree,0,256);for(uint8_t v:plain)byte(v);return data;
}
SmafFile file(int format,int track,int note,bool implicit,int decoy,bool reverse,bool missing=false,bool shared=true) {
    SmafFile f;TrackChunk m;m.trackNumber=track;m.formatType=format;
    m.durationTimeBase=m.gateTimeBase=0x10;m.channelStatus.resize(16);
    // Explicit unity gain and hard left avoid unrelated shared-profile laws.
    m.sequenceData={0,0xb0,0,125,0,0xb0,32,0,0,0xb0,7,127,0,0xb0,11,127,0,0xb0,10,0,
                    0,0x90,uint8_t(note),100,5};
    if(implicit)m.sequenceData.insert(m.sequenceData.end(),{10,0x80,uint8_t(note),5});
    m.sequenceData.insert(m.sequenceData.end(),{20,0xff,0x2f,0});
    if(format==1)m.sequenceData=packed(m.sequenceData);
    WaveData wave;wave.number=note<=12?note+1:note-92+14;
    wave.adpcm=false;wave.bitsPerSample=8;wave.samplingRate=8000;
    for(int i=0;i<3200;++i)wave.data.push_back(uint8_t(160+(i%11)));
    if(!missing)m.waves.push_back(wave);f.tracks.push_back(m);
    if(decoy>=0){
        TrackChunk other;other.trackNumber=track==0?1:0;other.formatType=shared?3:2;
        other.durationTimeBase=other.gateTimeBase=0x10;other.channelStatus.resize(shared?32:16);
        other.sequenceData={0,0xff,0x2f,0};
        if(decoy){wave.data.assign(3200,96);other.waves.push_back(wave);}
        if(reverse)f.tracks.insert(f.tracks.begin(),other);else f.tracks.push_back(other);
    }
    return f;
}
std::vector<float> drain(MaPlayer& p,int block){
    std::vector<float> result,buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0){result.insert(result.end(),buffer.begin(),buffer.begin()+2*n);require(result.size()<300000,"finite source-binding fixture");}
    require(!p.diagnostics().tailLimitReached,"source-binding fixture ends naturally");return result;
}
std::vector<float> render(const SmafFile& f,unsigned rate,int block=257){MaPlayer p;require(p.init(f,rate),"source fixture initializes");return drain(p,block);}
void matrix(){
    size_t checks=0;
    for(int format:{1,2})for(int track:{0,1})for(int note:{0,92,110})for(bool implicit:{false,true}) {
        const auto base=file(format,track,note,implicit,-1,false);
        const auto expected=render(base,8000);
        require(std::any_of(expected.begin(),expected.end(),[](float x){return x>0.01f;}),"own source is audible and positive");
        for(int decoy:{0,1})for(bool reverse:{false,true}) {
            auto mixed=file(format,track,note,implicit,decoy,reverse);
            // Preserve the existing shared-channel pan/controller profile in
            // the oracle. Put the original source in both namespaces so source
            // selection cannot affect its sound; the target changes only the
            // unrelated MTR's wave contents/presence.
            auto oracle=mixed;
            oracle.tracks[reverse?0:1].waves=base.tracks[0].waves;
            const auto oracleAudio=render(oracle,8000);
            MaPlayer p;require(p.init(mixed,8000),"mixed source initializes");
            require(p.scoreAudit().scanComplete,"mixed score decoding is complete");
            const auto actual=drain(p,257);
            if(actual!=oracleAudio){size_t first=0;while(first<std::min(actual.size(),oracleAudio.size())&&actual[first]==oracleAudio[first])++first;
                std::cerr<<"format="<<format<<" track="<<track<<" note="<<note<<" implicit="<<implicit<<" decoy="<<decoy<<" reverse="<<reverse<<" sizes="<<actual.size()<<","<<oracleAudio.size()<<" first="<<first<<" audio="<<p.diagnostics().audioNotes<<" missing="<<p.diagnostics().missingAudio<<'\n';}
            require(actual==oracleAudio,"unrelated MA7 cannot replace or mute Mobile Mwa");
            require(p.diagnostics().audioNotes==uint64_t(implicit?2:1)&&p.diagnostics().missingAudio==0,
                    "Audio count and resolved own source are retained");
            p.seekToStart();require(drain(p,1)==oracleAudio,"source binding survives seek and one-frame blocks");
            require(p.init(mixed,8000)&&drain(p,1024)==oracleAudio,"source binding survives reinit and large blocks");++checks;
        }
        // A missing own source stays missing even with a present decoy Mwa.
        auto absent=file(format,track,note,implicit,1,false,true);
        MaPlayer p;require(p.init(absent,8000),"missing-own-source fixture initializes");
        const auto zero=drain(p,257);require(std::all_of(zero.begin(),zero.end(),[](float x){return x==0;}),"missing own wave cannot borrow another MTR");
        require(p.diagnostics().audioNotes==0&&p.diagnostics().missingAudio==uint64_t(implicit?2:1),"missing source diagnostics remain local");
        // A separate Mobile channel namespace already worked and stays exact.
        require(render(file(format,track,note,implicit,1,false,false,false),8000)==expected,"nonoverlapping Mobile MTR is unchanged");
    }
    for(int format:{1,2})for(bool reverse:{false,true}) {
        const auto base=file(format,0,0,true,-1,false),mixed=file(format,0,0,true,1,reverse);
        auto oracle=mixed;oracle.tracks[reverse?0:1].waves=base.tracks[0].waves;
        require(render(oracle,44100)==render(mixed,44100),"fractional-rate reconstruction retains its source");
    }
    std::cout<<"Recorded source binding PASS: "<<checks<<" mixed cases; packed/plain; two channel banks; three wave IDs; explicit/running velocity; missing-own; seek/init/blocks;44.1kHz\n";
}
}
int main(){matrix();}
