// SPDX-License-Identifier: Apache-2.0
// Original source-lifetime fixtures; no manufacturer samples or songs.
#include "ma_player.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
namespace {
void require(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
void exclusive(Bytes& setup,const Bytes& voice) {
    setup.push_back(0xf0);setup.push_back(uint8_t(voice.size()+1));
    setup.insert(setup.end(),voice.begin(),voice.end());setup.push_back(0xf7);
}
SmafFile score(int format,bool includeLong,bool wt=false,int later=0,bool survives=false,bool mute=false,bool noHeld=false) {
    SmafFile file;TrackChunk m;m.formatType=format;m.durationTimeBase=0x10;m.gateTimeBase=0;
    m.channelStatus.resize(format==3?32:16);
    if(wt) {
        exclusive(m.setupData,{0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0,0xf0,0,0,0,0,0,0,0,80,1});
        Bytes wave{0x43,0x79,7,0x7f,3,1,2};
        for(int i=0;i<80;++i)wave.push_back(uint8_t(128+(i%8<4?16:-16)));
        exclusive(m.setupData,wave);
    } else exclusive(m.setupData,{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,
        0,0xf0,0,0xfc,0,0x10,0, 0,0,0xf0,0x30,0,0x10,0});
    if(!noHeld)m.sequenceData={uint8_t(wt?10:0),uint8_t(format==3?0x10:0x90),69,100,1};
    if(later==2)m.sequenceData.insert(m.sequenceData.end(),
        {uint8_t(wt?90:100),uint8_t(format==3?0x30:0xb0),77,42});
    file.tracks.push_back(m);
    TrackChunk atr;atr.isAudioTrack=true;atr.formatType=0;atr.trackNumber=0;
    atr.durationTimeBase=atr.gateTimeBase=0x13;
    if(mute)atr.sequenceData={0,0,0x37,0};
    const unsigned limit=format==3?32:16;
    for(unsigned id=1;id<=limit+1;++id) {
        WaveData w;w.number=int(id);w.samplingRate=8000;w.adpcm=false;w.bitsPerSample=8;w.signedPcm8=true;
        w.data.resize(id==1?128000:80);
        if(id==1)for(size_t i=0;i<w.data.size();++i)w.data[i]=uint8_t(int8_t(i%8<4?16:-16));
        atr.waves.push_back(w);
        const bool trigger=id==1?includeLong:!noHeld && (!survives||id<=limit);
        if(trigger)atr.sequenceData.insert(atr.sequenceData.end(),{0,uint8_t(id),0x82,16}); //400*50ms gate
    }
    if(later==1)atr.sequenceData.insert(atr.sequenceData.end(),{40,2,0x82,16}); //new short source at2s
    file.tracks.push_back(atr);return file;
}
std::vector<float> drain(MaPlayer& player,int block,unsigned rate) {
    std::vector<float> result,buffer(size_t(block)*2);int n;
    while((n=player.render(buffer.data(),block))>0) {
        result.insert(result.end(),buffer.begin(),buffer.begin()+2*n);
        require(result.size()<size_t(rate)*2*30,"runtime ATR fixture finishes within30 seconds");
    }
    return result;
}
void stolen() {
    for(int format:{2,3})for(unsigned rate:{8000u,44100u})for(bool wt:{false,true})for(int later=0;later<3;++later) {
        // Keep the expensive full matrix at8k;44.1k exercises both held paths.
        if(rate==44100 && (format==3||later!=0))continue;
        const auto file=score(format,true,wt,later),reference=score(format,false,wt,later);
        MaPlayer subject,control;require(subject.init(file,rate)&&control.init(reference,rate),"mixed ATR horizon fixtures initialize");
        const uint64_t initial=subject.totalSamples();
        const auto expected=drain(control,257,rate),actual=drain(subject,31,rate);
        require(actual==expected,"stolen long ATR cannot lengthen held FM/WT output or its safety fade");
        require(subject.totalSamples()==control.totalSamples()&&subject.totalSamples()<initial,"runtime horizon converges to actual surviving source bound");
        require(subject.diagnostics().stolenPcm==1,"only the long first ATR source is stolen");
        require(subject.diagnostics().tailLimitReached&&control.diagnostics().tailLimitReached,"existing held-voice safety ceiling remains enforced");
        if(rate==8000&&format==2&&!wt&&later==0) {
            subject.seekToStart();require(subject.totalSamples()==initial,"seek restores conservative initial source horizon");
            require(drain(subject,1024,rate)==expected,"seek and block size preserve shortened runtime horizon");
            require(subject.init(file,rate)&&drain(subject,257,rate)==expected,"reinit clears runtime completion state");
        }
    }
}
void liveSources() {
    constexpr unsigned rate=8000;
    for(int format:{2,3})for(bool mute:{false,true}) {
        MaPlayer p;require(p.init(score(format,true,false,0,true,mute),rate),"surviving long ATR fixture initializes");
        const uint64_t initial=p.totalSamples();float buffer[2048];
        require(p.render(buffer,1024)==1024&&p.diagnostics().stolenPcm==0,"live long ATR is not displaced");
        require(p.totalSamples()==initial,"silent or muted but live ATR retains its original horizon");
    }
    MaPlayer recording;require(recording.init(score(2,true,false,0,true,false,true),rate),"long recording initializes");
    float buffer[2048];unsigned frames=0;bool heard=false;
    while(frames<rate*11) {
        const int n=recording.render(buffer,int(std::min(1024u,rate*11-frames)));
        require(n>0,"real long recording is not cut at the ten-second tail interval");
        if(frames>=rate*10)for(int i=0;i<2*n;++i)heard|=buffer[i]>1e-4f||buffer[i]<-1e-4f;
        frames+=unsigned(n);
    }
    require(heard&&!recording.diagnostics().tailLimitReached,"actual recording still sounds beyond ten seconds");
}
void wtReplacement() {
    for(unsigned rate:{8000u,44100u})for(int format:{2,3}) {
        if(rate==44100&&format==3)continue;
        auto file=score(format,true,true,0,true);
        // Fill the PCM pool with one long recording and medium recordings.
        // The WT note starts at100ms and replaces the first long ATR source.
        for(auto& wave:file.tracks[1].waves)if(wave.number!=1)wave.data.resize(8000);
        auto reference=file;auto& sequence=reference.tracks[1].sequenceData;
        require(sequence.size()>=4&&sequence[0]==0&&sequence[1]==1&&sequence[2]==0x82&&sequence[3]==16,
                "WT replacement oracle begins with the known long ATR trigger");
        // Independently author a100ms gate for that source. Both paths play
        // its original prefix and replace it at the same sample.
        sequence[2]=2;sequence.erase(sequence.begin()+3);
        MaPlayer actual,oracle;require(actual.init(file,rate)&&oracle.init(reference,rate),"WT replacement fixtures initialize");
        const auto expected=drain(oracle,257,rate),audio=drain(actual,31,rate);
        require(audio==expected&&actual.totalSamples()==oracle.totalSamples(),"WT replacing ATR uses the surviving sources' safety horizon");
        require(actual.diagnostics().stolenPcm==1&&oracle.diagnostics().stolenPcm==1,"WT replacement preserves pool allocation and stale-gate exclusion");
    }
}
}
#ifndef MMF_ATR_RUNTIME_NO_MAIN
int main(){stolen();liveSources();wtReplacement();std::cout<<"PASS: stolen ATR lifetime, held FM/WT,16/32 slots,future events,seek,WT replacement and live recordings\n";}
#endif
