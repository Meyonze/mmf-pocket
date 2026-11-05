// SPDX-License-Identifier: Apache-2.0
// Original dense-part fixtures: immutable silence must not cut audible parts.
#include "ma_player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
void require(bool value,const char* message) {
    if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
void exclusive(SmafFile& file,const Bytes& voice) {
    auto& out=file.tracks[0].setupData;
    out.push_back(0xf0);out.push_back(uint8_t(voice.size()+1));
    out.insert(out.end(),voice.begin(),voice.end());out.push_back(0xf7);
}
SmafFile base(bool pcm) {
    SmafFile file;TrackChunk track;track.formatType=3;track.trackNumber=0;
    track.durationTimeBase=track.gateTimeBase=0x10;file.tracks.push_back(track);
    Bytes voice;
    if(pcm) {
        voice={0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0xf0,0xf0,0,0,0,0,0,0,0,80,1};
        WaveData wave;wave.number=1;wave.bitsPerSample=16;wave.samplingRate=8000;
        for(int i=0;i<80;++i) {
            const auto v=uint16_t(int16_t(std::lround(4000*std::sin(6.283185307179586*i/40))));
            wave.data.push_back(uint8_t(v>>8));wave.data.push_back(uint8_t(v));
        }
        file.tracks[0].waves.push_back(wave);
    } else {
        voice={0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,
               0,0xf0,0,0xfc,0,0x10,0,0,0xf0,0xf0,0x30,0,0x10,0};
    }
    exclusive(file,voice);
    voice[5]=121;voice[6]=6;exclusive(file,voice);
    return file;
}
SmafFile dense(bool pcm,bool includeMute,bool bankMute,int high,bool running,bool future) {
    auto file=base(pcm);using Event=std::pair<int,Bytes>;std::vector<Event> events;
    const uint8_t cc=uint8_t(0x31|high),on=uint8_t(0x11|high),reuse=uint8_t(1|high);
    events={{0,{0x30,7,32}},{0,{0x30,11,127}},{0,{cc,7,0}},{0,{cc,11,0}}};
    if(bankMute){events.push_back({0,{cc,0,121}});events.push_back({0,{cc,32,6}});}
    if(includeMute && running)events.push_back({0,{on,60,uint8_t(bankMute?127:1),3}});
    for(unsigned i=0;i<32;++i)events.push_back({0,{0x10,60,100,20}});
    if(includeMute)events.push_back({2,running?Bytes{reuse,60,3}:Bytes{on,60,uint8_t(bankMute?127:1),3}});
    events.push_back({3,{cc,7,127}});events.push_back({3,{cc,11,127}});
    if(future) {
        events.push_back({5,{cc,0,0}});events.push_back({5,{cc,32,0}});
        events.push_back({5,{on,60,100,10}});
    }
    events.push_back({30,{0xff,0x2f,0}});
    std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b){return a.first<b.first;});
    int previous=0;auto& out=file.tracks[0].sequenceData;
    for(const auto& [at,data]:events){out.push_back(uint8_t(at-previous));out.insert(out.end(),data.begin(),data.end());previous=at;}
    return file;
}
std::vector<float> drain(MaPlayer& p,int block) {
    std::vector<float> result,buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0) {
        result.insert(result.end(),buffer.begin(),buffer.begin()+n*2);
        require(result.size()<8000*2*12,"silent-note fixture stays bounded");
    }
    require(p.finished()&&!p.diagnostics().tailLimitReached,"silent-note fixture ends naturally");
    return result;
}
int main() {
    unsigned cases=0;
    for(bool pcm:{false,true})for(bool bankMute:{false,true})for(int high:{0,0x80})
    for(bool running:{false,true})for(bool future:{false,true}) {
        const auto source=dense(pcm,true,bankMute,high,running,future);
        MaPlayer reference;require(reference.init(dense(pcm,false,bankMute,high,running,future),8000),"audible reference initializes");
        const auto expected=drain(reference,257);
        require(std::any_of(expected.begin(),expected.end(),[](float x){return std::abs(x)>1e-5f;}),"dense reference actually sounds");
        for(int block:{1,257,1024}) {
            MaPlayer actual;require(actual.init(source,8000),"dense silent-note fixture initializes");
            require(drain(actual,block)==expected,"immutable silent notes must not cut or rebalance 32 sounding voices");
            const auto& d=actual.diagnostics();
            require(d.suppressedSilentNotes==unsigned(running?2:1),"all immutable silent notes are counted without allocation");
            require((pcm?d.stolenHeldPcm:d.stolenHeldFm)==unsigned(future),"silent notes do not advance voice-steal order");
            if(block==257){
                actual.seekToStart();
                require(actual.diagnostics().suppressedSilentNotes==0,"seek resets suppression diagnostic");
                require(drain(actual,block)==expected,"seek restores silent-note allocation policy");
                require(actual.diagnostics().suppressedSilentNotes==unsigned(running?2:1),"seek recounts each silent note once");
            }
            ++cases;
        }
    }
    // A channel at zero gain may later become audible: it still needs a voice.
    for(bool pcm:{false,true})for(int controller:{7,11}) {
        auto file=base(pcm);file.tracks[0].sequenceData={0,0x30,uint8_t(controller),0,
            0,0x10,60,100,20,3,0x30,uint8_t(controller),127,27,0xff,0x2f,0};
        MaPlayer p;require(p.init(file,8000),"reversible zero gain initializes");const auto audio=drain(p,257);
        require(p.diagnostics().suppressedSilentNotes==0,"reversible zero channel gain is never suppressed");
        require(std::all_of(audio.begin(),audio.begin()+240*2,[](float x){return x==0;}),"zero channel gain is initially silent");
        require(std::any_of(audio.begin()+240*2,audio.end(),[](float x){return std::abs(x)>1e-5f;}),"future channel gain revives a real note");
    }
    std::cout<<"Silent-note pool tests PASS: "<<cases<<" dense cases, future voices, gains, blocks and seeks\n";
}
