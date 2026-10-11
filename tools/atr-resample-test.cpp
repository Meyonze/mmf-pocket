// SPDX-License-Identifier: Apache-2.0
// Original ATR waveform/clock fixtures; no recordings, vendor ROM or PDF data.
#include "smaf_file.h"
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <array>
#include <cstdint>
#include <map>
#include <vector>
#define private public
#include "ma_player.h"
#undef private
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
constexpr double pi=3.14159265358979323846;
void require(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
Bytes tone(size_t length,double frequency=0.2) {
    Bytes data;for(size_t i=0;i<length;++i)data.push_back(uint8_t(int8_t(std::lround(35*std::sin(2*pi*frequency*i)))));
    return data;
}
SmafFile fixture(int sourceRate,const Bytes& data,const Bytes& sequence={0,1,100},bool adpcm=false,int track=0) {
    TrackChunk t;t.trackNumber=track;t.isAudioTrack=true;t.formatType=0;
    t.durationTimeBase=t.gateTimeBase=0;t.sequenceData=sequence;
    WaveData w;w.number=1;w.samplingRate=sourceRate;w.bitsPerSample=adpcm?4:8;
    w.adpcm=adpcm;w.signedPcm8=true;w.data=data;t.waves.push_back(w);
    SmafFile f;f.tracks.push_back(t);return f;
}
void start(MaPlayer& p) {
    while(p.nextEvent_<p.events_.size() && p.events_[p.nextEvent_].sample==0)
        p.fireEvent_(p.events_[p.nextEvent_++]);
}
// Independent direct-double normalized Blackman/sinc construction; no use of
// production phase/cutoff tables or index helpers. Exact rows in numeric tests.
double reference(const std::vector<int16_t>& pcm,double pos,double rate) {
    const size_t i=size_t(pos);const double frac=double(float(pos-i));
    const double linear=(pcm[i]+(pcm[std::min(i+1,pcm.size()-1)]-pcm[i])*frac)/32768.0;
    if(rate==1)return linear;
    const int taps=rate>1?32:16,half=taps/2,offset=half-1;
    const double bandwidth=rate>1?1/rate:1;double sum=0,result=0;
    for(int tap=0;tap<taps;++tap) {
        const double d=tap-offset-frac;
        const double sinc=std::fabs(d)<1e-12?1:std::sin(pi*bandwidth*d)/(pi*bandwidth*d);
        const double weight=sinc*(0.42+0.5*std::cos(pi*d/half)+0.08*std::cos(2*pi*d/half));
        const int64_t source=std::clamp(int64_t(i)+tap-offset,int64_t(0),int64_t(pcm.size()-1));
        result+=pcm[size_t(source)]/32768.0*weight;sum+=weight;
    }
    const double blend=rate>1?std::min(1.0,(rate-1)/0.1):rate<=0.8?1:(1-rate)/0.2;
    return linear+blend*blend*(3-2*blend)*(result/sum-linear);
}
std::vector<float> drain(MaPlayer& p,int block=257) {
    std::vector<float> out,b(size_t(block)*2);int n;
    while((n=p.render(b.data(),block))>0){out.insert(out.end(),b.begin(),b.begin()+n*2);require(out.size()<2000000,"finite fixture terminates");}
    require(p.finished()&&!p.diagnostics().tailLimitReached,"ATR ends naturally");return out;
}
void numerical() {
    for(size_t length:{size_t(1),size_t(2),size_t(3),size_t(7),size_t(16),size_t(32),size_t(79)})
    for(auto pair:{std::pair{4000,32000},std::pair{4000,16000},std::pair{4000,8000},std::pair{8000,8000},std::pair{44100,22050},std::pair{44100,11025}})
    for(bool adpcm:{false,true}) {
        MaPlayer p;require(p.init(fixture(pair.first,tone(length),{0,1,100},adpcm),pair.second),"ATR numeric fixture initializes");start(p);
        auto v=p.pcmPool_[0];require(v.active&&v.qualityInterpolation&&!v.streamAudio&&!v.loop,"ATR actual trigger selects non-loop quality");
        const auto& pcm=p.audioWaveBanks_.at(0)[1].pcm;
        if(!adpcm)for(size_t i=0;i<length;++i)require(pcm[i]==int(int8_t(tone(length)[i]))*256,"signed8 payload unchanged");
        require(v.rate==pair.first/double(pair.second) && v.gain==1 && v.vel==1 && v.noteId==0,"ATR source pitch/gain/identity unchanged");
        size_t frames=0;
        while(v.pos<pcm.size()) {
            const double position=v.pos,expected=reference(pcm,position,v.rate);const float actual=v.tick();
            require(std::isfinite(actual)&&std::fabs(actual-expected)<3e-6,"ATR actual tick matches independent FIR/endpoint oracle");
            require(v.pos==position+v.rate,"quality does not alter source clock");++frames;
        }
        require(frames==size_t(std::ceil(pcm.size()/v.rate)),"ATR native EOF frame count unchanged");
        require(v.tick()==0&&!v.active,"ATR has no added FIR tail");
    }
    // Actual ATR voices (not generic stand-ins) exercise pass/stopband conversion.
    for(int outputRate:{22050,11025}) {
        MaPlayer pass,stop;
        require(pass.init(fixture(44100,tone(8192,1.0/64),{0,1,100}),outputRate),"passband initializes");
        require(stop.init(fixture(44100,tone(8192,0.45),{0,1,100}),outputRate),"stopband initializes");start(pass);start(stop);
        auto a=pass.pcmPool_[0],b=stop.pcmPool_[0],linear=b;linear.qualityInterpolation=false;
        a.pos=b.pos=linear.pos=32.25;double energy=0,alias=0,old=0;
        for(int n=0;n<512;++n){double x=a.tick(),y=b.tick(),z=linear.tick();energy+=x*x;alias+=y*y;old+=z*z;}
        require(std::fabs(std::sqrt(energy/512)*std::sqrt(2.0)-35.0/128)<0.012,"ATR passband amplitude retained");
        require(alias<old*0.01,"ATR stopband alias energy reduced by20dB");
        std::cout<<"ATR AA ratio="<<a.rate<<" aliasRMS="<<std::sqrt(alias/512)<<" linearRMS="<<std::sqrt(old/512)<<'\n';
    }
}
void hardMute() {
    // Independent authored clock and post-mix filter reference. Gate and EOS
    // cannot both pass merely by sharing a wrong stop time. No production event
    // reader, gate evaluator, interpolation table or filter helper in oracle.
    const auto data=tone(1600);
    std::vector<int16_t> pcm;for(uint8_t x:data)pcm.push_back(int(int8_t(x))*256);
    for(int rate:{8000,44100,48000})for(bool eos:{false,true}) {
        auto f=fixture(8000,data,eos?Bytes{50,1,100,10,0,0,0,0}:Bytes{1,1,10});
        if(!eos)f.tracks[0].durationTimeBase=0x13;
        MaPlayer p;require(p.init(f,rate),"ATR hard mute initializes");const auto actual=drain(p);
        const size_t begin=uint64_t(50.0*rate/1000),end=uint64_t(60.0*rate/1000);
        const float coef=float(1-std::exp(-2*pi*10000/rate));float lp1=0,lp2=0;double pos=0;
        for(size_t n=0;n<actual.size()/2;++n) {
            float input=0;
            if(n>=begin&&n<end){input=float(reference(pcm,pos,8000.0/rate))*0.32f;pos+=8000.0/rate;}
            lp1+=coef*(input-lp1);lp2+=coef*(lp1-lp2);
            require(std::fabs(actual[n*2]-lp2)<5e-6&&std::fabs(actual[n*2+1]-lp2)<5e-6,"ATR independent50ms start/60ms hard mute and retained filter history");
        }
        require(actual.size()/2>=end&&actual.size()/2<end+128,"ATR gate/EOS only leaves bounded output-filter drain");
        p.seekToStart();require(actual==drain(p,1),"ATR seek/single-frame exact");p.seekToStart();require(actual==drain(p,1024),"ATR large block exact");
    }
}
void integration() {
    for(int source:{4000,8000,11000,22050,44100})for(int output:{8000,44100,48000}) {
        auto f=fixture(source,tone(2048),{0,0,0x37,96,0,0,0x3b,64,0,0,0x3a,127,0,1,100});
        MaPlayer p,old;require(p.init(f,output)&&old.init(f,output),"ATR controls initialize");start(p);start(old);
        auto& v=p.pcmPool_[0];old.pcmPool_[0].qualityInterpolation=false;
        require(v.vel==old.pcmPool_[0].vel&&v.panLeft==0&&v.panRight==1,"authored gain and exact pan endpoints retained");
        require(p.events_.size()==old.events_.size()&&p.totalSamples()==old.totalSamples(),"conversion does not change decoded clock bounds");
        const auto a=drain(p),b=drain(old);require((a==b)==(source==output),"ATR unity exact, non-unity reconstruction changes output");
        for(size_t i=0;i<a.size();i+=2)require(a[i]==0,"ATR right-only pan remains right-only");
        require(p.init(f,output),"ATR reinit initializes");require(a==drain(p),"ATR reinit resets source/controller state");
    }
    // Reusing a WT slot must clear loop/LFO and apply the new ATR quality flag.
    MaPlayer p;require(p.init(fixture(4000,tone(2048)),8000),"reuse fixture initializes");
    p.pcmPool_[0].active=false;p.pcmPool_[0].qualityInterpolation=false;p.pcmPool_[0].loop=true;
    p.pcmPool_[0].lfoInc=0.1;p.pcmPool_[0].vibDepth=0.2;p.pcmPool_[0].amDepth=0.3f;start(p);
    require(p.pcmPool_[0].qualityInterpolation&&!p.pcmPool_[0].loop&&p.pcmPool_[0].lfoInc==0&&p.pcmPool_[0].amDepth==0,"ATR cannot inherit stolen WT modulation/loop/profile");
    // The allocation detector remains linear-sample-based even with FIR.
    auto quality=p.pcmPool_[0],linear=quality;linear.qualityInterpolation=false;
    for(int n=0;n<300;++n){quality.tick();linear.tick();require(quality.recentLevel==linear.recentLevel,"ATR allocation loudness remains sample-exact linear oracle");}
    p.pcmPool_[0].active=false;p.chans_[0].modernTimbre=false;
    p.voiceWaveBank_[1].pcm=p.audioWaveBanks_.at(0)[1].pcm;
    ParsedVoice wt;wt.valid=wt.isPcm=true;wt.pcm.fs=4000;wt.pcm.waveId=1;wt.pcm.endPt=2048;wt.pcm.env.ar=15;wt.pcm.env.egType=true;
    require(p.startPcm_(0,60,60,wt,p.chans_[0],1,999),"ATR slot reuses as legacy WT");
    require(!p.pcmPool_[0].qualityInterpolation&&p.pcmPool_[0].noteId==999,"legacy WT cannot inherit ATR quality/identity");
}
void benchmark() {
    const auto f=fixture(44100,tone(65536),{0,1,100});volatile float sink=0;
    for(int rate:{88200,22050})for(bool quality:{false,true}) {
        MaPlayer p;require(p.init(f,rate),"ATR benchmark initializes");start(p);
        std::array<MaPlayer::PcmVoice,32> voices;for(auto& v:voices){v=p.pcmPool_[0];v.qualityInterpolation=quality;}
        const auto startTime=std::chrono::steady_clock::now();
        for(int frame=0;frame<16000;++frame){float mix=0;for(auto& v:voices)mix+=v.tick();sink=mix;}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-startTime).count();
        std::cout<<"ATR host32voices ratio="<<p.pcmPool_[0].rate<<" quality="<<quality<<" frames=16000 elapsedMs="<<ms<<'\n';
    }
    (void)sink;
}
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--benchmark"){benchmark();return 0;}
    numerical();hardMute();integration();std::cout<<"ATR resampling tests PASS\n";return 0;
}
