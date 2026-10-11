// SPDX-License-Identifier: Apache-2.0
// Original renderer-quality fixtures; no copyrighted audio or chip-DSP claim.
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
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace fxchain::smaf;
namespace fs=std::filesystem;
void require(bool ok,const char* text) {if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
constexpr double pi=3.14159265358979323846;

// Direct double-precision windowed-sinc assembly. Does not call or read the
// production interpolation tables/index helpers. Fraction phases used below
// fall exactly on rows, so table-quantization is not part of this oracle.
double reference(const std::vector<int16_t>& pcm,double pos,double rate,bool quality) {
    const size_t i=size_t(pos);const double frac=double(float(pos-i));
    double linear=(pcm[i]+(pcm[std::min(i+1,pcm.size()-1)]-pcm[i])*frac)/32768.0;
    if(!quality || rate==1)return linear;
    const int taps=rate>1?32:16,half=taps/2,offset=half-1;
    const double bandwidth=rate>1?1/rate:1;
    double sum=0,result=0;
    for(int t=0;t<taps;++t) {
        const double d=t-offset-frac;
        const double sinc=std::fabs(d)<1e-12?1:std::sin(pi*bandwidth*d)/(pi*bandwidth*d);
        const double window=0.42+0.5*std::cos(pi*d/half)+0.08*std::cos(2*pi*d/half);
        const double weight=sinc*window;
        const int64_t source=std::clamp(int64_t(i)+t-offset,int64_t(0),int64_t(pcm.size()-1));
        result+=pcm[size_t(source)]/32768.0*weight;sum+=weight;
    }
    const double x=rate>1?std::min(1.0,(rate-1)/0.1):rate<=0.8?1:(1-rate)/0.2;
    return linear+x*x*(3-2*x)*(result/sum-linear);
}
MaPlayer::PcmVoice voice(const std::vector<int16_t>& pcm,double rate,bool quality=true) {
    MaPlayer::PcmVoice v;v.pcm=pcm.data();v.len=pcm.size();v.rate=rate;
    v.active=true;v.streamAudio=true;v.qualityInterpolation=quality;return v;
}
std::vector<int16_t> sine(double f,size_t n=8192) {
    std::vector<int16_t> pcm(n);
    for(size_t i=0;i<n;++i)pcm[i]=int16_t(std::lround(12000*std::sin(2*pi*f*i)));
    return pcm;
}
void numerical() {
    for(size_t length:{size_t(1),size_t(2),size_t(3),size_t(7),size_t(16),size_t(32),size_t(79)}) {
        std::vector<int16_t> pcm(length);
        for(size_t i=0;i<length;++i)pcm[i]=int16_t(int((i*7919)%24001)-12000);
        for(double rate:{0.125,0.25,0.5,0.75,1.0,2.0,4.0})for(bool quality:{false,true}) {
            auto v=voice(pcm,rate,quality);size_t n=0;
            while(v.pos<double(length)) {
                const double pos=v.pos,expected=reference(pcm,pos,rate,quality);
                const float actual=v.tick();
                require(std::isfinite(actual)&&std::fabs(actual-expected)<3e-6,
                        "recorded Audio tick matches independent endpoint/FIR oracle");
                require(v.pos==pos+rate,"quality never changes source position");++n;
            }
            require(n==size_t(std::ceil(length/rate)),"quality keeps native EOF frame count");
            require(v.tick()==0&&!v.active,"EOF has no added reconstruction tail");
        }
    }
    // Test well inside the stopband, not the finite FIR transition near the
    //1.5:1 cutoff. This is a quality bound, not an ideal brick-wall claim.
    const auto pass=sine(1.0/64),stop=sine(0.45);
    for(double rate:{1.5,2.0,4.0,6.0}) {
        auto p=voice(pass,rate),a=voice(stop,rate),old=voice(stop,rate,false);
        p.pos=a.pos=old.pos=32.25;double energy=0,alias=0,prior=0;
        for(int n=0;n<512;++n){double x=p.tick(),y=a.tick(),z=old.tick();energy+=x*x;alias+=y*y;prior+=z*z;}
        const double amplitude=std::sqrt(energy/512)*std::sqrt(2.0);
        require(std::fabs(amplitude-12000.0/32768.0)<0.012,"recorded Audio AA preserves passband amplitude");
        require(alias<prior*0.01,"recorded Audio AA reduces stopband alias energy by20dB");
        std::cout<<"Recorded AA rate="<<rate<<" passAmplitude="<<amplitude
                 <<" aliasRMS="<<std::sqrt(alias/512)<<" linearRMS="<<std::sqrt(prior/512)<<'\n';
    }
    const auto source=sine(0.2);auto v=voice(source,0.5),old=voice(source,0.5,false);
    v.pos=old.pos=32.5;double qualityError=0,linearError=0;
    for(int n=0;n<512;++n) {
        const double target=12000.0/32768.0*std::sin(2*pi*0.2*v.pos);
        double x=v.tick()-target,y=old.tick()-target;qualityError+=x*x;linearError+=y*y;
    }
    require(qualityError<linearError*0.01,"recorded Audio upsample reconstruction improves sine error");
    std::cout<<"Recorded upsample errorRMS="<<std::sqrt(qualityError/512)
             <<" linearErrorRMS="<<std::sqrt(linearError/512)<<'\n';
    auto fade=voice(source,0.5);fade.stopFadeTotal=fade.stopFade=16;
    for(int n=16;n>0;--n) {const double expected=reference(source,fade.pos,0.5,true)*n/16;
        require(std::fabs(fade.tick()-expected)<3e-6,"recorded gate keeps its existing fade coefficients");}
    require(fade.tick()==0&&!fade.active,"recorded gate stops without FIR tail");
}
SmafFile file(int format,int fsHz=4000,size_t length=2048) {
    SmafFile f;TrackChunk t;t.trackNumber=0;t.formatType=format;
    t.durationTimeBase=t.gateTimeBase=0x10;t.channelStatus.resize(format==0?2:format==3?32:16);
    const uint8_t cc=format==3?0x30:0xb0,on=format==3?0x10:0x90;
    t.sequenceData={0,cc,0,125,0,cc,32,0,0,on,0,127,20,30,0xff,0};
    if(format==0)t.sequenceData={0,0,0x31,0x80,0,0,0x30,0,0,0x11,20,30,0xff,0};
    WaveData w;w.number=1;w.adpcm=false;w.bitsPerSample=16;w.samplingRate=fsHz;
    for(int16_t x:sine(0.2,length)){uint16_t b=uint16_t(x);w.data.push_back(uint8_t(b>>8));w.data.push_back(uint8_t(b));}
    t.waves.push_back(w);
    if(format==1) {
        const auto raw=t.sequenceData;std::vector<uint8_t> packed{0,0,0,uint8_t(raw.size())};size_t bits=0;
        auto bit=[&](int b){if(bits%8==0)packed.push_back(0);packed.back()|=uint8_t(b<<(7-bits%8));++bits;};
        auto tree=[&](auto&& self,int depth,int value)->void {
            if(depth==8){bit(0);for(int k=7;k>=0;--k)bit((value>>k)&1);return;}
            bit(1);self(self,depth+1,value*2);self(self,depth+1,value*2+1);
        };
        tree(tree,0,0);for(uint8_t b:raw)for(int k=7;k>=0;--k)bit((b>>k)&1);t.sequenceData=packed;
    }
    f.tracks.push_back(t);return f;
}
std::vector<float> drain(MaPlayer& p,int block) {
    std::vector<float> out,b(block*2);int n;
    while((n=p.render(b.data(),block))>0){out.insert(out.end(),b.begin(),b.begin()+2*n);require(out.size()<100000,"test ends naturally");}
    require(p.finished()&&!p.diagnostics().tailLimitReached,"recorded fixture ends naturally");return out;
}
void integration() {
    for(int format:{1,2,3})for(int sourceRate:{4000,8000,16000,32000,48000}) {
        auto f=file(format,sourceRate);MaPlayer p;require(p.init(f,8000),"recorded fixture initializes");
        float b[2];require(p.render(b,1)==1,"recorded starts at first authored frame");
        require(p.audioPool_[0].active&&p.audioPool_[0].qualityInterpolation,"modern source opts in to quality");
        require(p.audioPool_[0].rate==sourceRate/8000.0&&p.audioPool_[0].vel==1,
                "source rate and authored velocity are unchanged");
        p.seekToStart();auto a=drain(p,1);p.seekToStart();require(a==drain(p,257),"seek and block sizes are sample-exact");
        p.seekToStart();require(a==drain(p,1024),"large render blocks are sample-exact");
        require(p.diagnostics().audioNotes==1&&p.diagnostics().stolenAudio==0,"recorded allocation is unchanged");
        // Independently opt out AFTER event routing. Unity must remain bit-exact.
        MaPlayer linear;require(linear.init(f,8000),"linear oracle initializes");
        for(const auto& e:linear.events_){if(e.sample)break;linear.fireEvent_(e);++linear.nextEvent_;}
        linear.audioPool_[0].qualityInterpolation=false;auto old=drain(linear,257);
        require((a==old)==(sourceRate==8000),"only non-unity recorded conversion changes samples");
    }
    // A stale shared modern channel must not upgrade an HPS-origin note.
    MaPlayer p;auto hps=file(0);require(p.init(hps,8000),"HPS exclusion fixture initializes");
    bool tagged=false;for(const auto& decoded:p.events_)if(decoded.type==MaPlayer::Ev::NoteOn){tagged=true;require(decoded.handyPhoneNote,"decoded HPS notes carry origin flag");}
    require(tagged,"HPS exclusion includes an actually decoded note");
    p.chans_[0].modernTimbre=true;
    p.chans_[0].streamTrack=0;p.chans_[0].bankMsb=125;
    MaPlayer::Ev e{0,MaPlayer::Ev::NoteOn,0,0,127,999};e.handyPhoneNote=true;p.fireEvent_(e);
    require(p.audioPool_[0].active&&!p.audioPool_[0].qualityInterpolation,"HPS origin stays linear despite shared channel state");
    auto modern=file(2);require(p.init(modern,8000),"reuse fixture initializes");float b[2];p.render(b,1);
    require(p.audioPool_[0].qualityInterpolation,"modern starts after old HPS state");
    require(p.init(hps,8000),"HPS reinitializes after quality playback");
    require(!p.audioPool_[0].active&&!p.audioPool_[1].active,"init clears prior quality Audio allocations");
}
void exclusions() {
    uint64_t hash=14695981039346656037ull;
    auto add=[&](float x){auto b=std::bit_cast<uint32_t>(x);for(int i=0;i<4;++i){hash^=(b>>(8*i))&255;hash*=1099511628211ull;}};
    for(int sourceRate:{4000,8000,16000,48000}) {
        // ATR now has a separate independent quality/gate regression suite.
        MaPlayer p;
        auto wt=file(2,sourceRate,8192);require(p.init(wt,8000),"WT exclusion initializes");
        p.nextEvent_=p.events_.size();ParsedVoice v;v.valid=v.isPcm=true;
        v.pcm.fs=sourceRate;v.pcm.waveId=1;v.pcm.endPt=8192;v.pcm.env.ar=15;v.pcm.env.egType=true;
        require(p.startPcm_(0,60,60,v,p.chans_[0],1,999),"WT actual instrument starts");
        require(p.pcmPool_[0].active&&p.pcmPool_[0].qualityInterpolation&&!p.pcmPool_[0].streamAudio,
                "WT existing quality selection unchanged");
        std::vector<float> block(1024);require(p.render(block.data(),512)==512,"WT exclusion renders fixed prefix");for(float x:block)add(x);
    }
    std::cout<<"WT exclusions exact float hash="<<std::hex<<hash<<std::dec<<'\n';
}
std::string utf8(const fs::path& p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
int inventory(const fs::path& root,const fs::path& report) {
    std::vector<fs::path> paths;for(const auto& e:fs::recursive_directory_iterator(root)) {
        auto ext=e.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(e.is_regular_file()&&ext==".mmf")paths.push_back(e.path());
    }
    std::sort(paths.begin(),paths.end());std::ofstream out(report,std::ios::binary|std::ios::trunc);if(!out)return 2;
    out<<"file\tinitialized\tcomplete\trecorded_triggers\tquality_triggers\twave_rates\n";size_t affected=0;
    for(const auto& path:paths) {
        std::ifstream in(path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};SmafFile f;
        if(!in||!f.parse(bytes.data(),bytes.size()))return 3;MaPlayer p;const bool init=p.init(f,44100);
        size_t count=0,quality=0;std::string rates;
        for(const auto& t:f.tracks)for(const auto& w:t.waves){if(!rates.empty())rates+=';';rates+=std::to_string(t.isAudioTrack?-1:t.formatType)+":"+std::to_string(w.samplingRate);}
        // Event-routing inventory only, not audio rendering or voice-end proof.
        for(const auto& e:p.events_) {p.cursor_=e.sample;auto before=p.diagnostics().audioNotes;p.fireEvent_(e);
            if(p.diagnostics().audioNotes>before){++count;if(!e.handyPhoneNote)++quality;}}
        out<<utf8(path.lexically_relative(root))<<'\t'<<init<<'\t'<<p.scoreAudit().scanComplete<<'\t'<<count<<'\t'<<quality<<'\t'<<rates<<'\n';
        if(quality)++affected;if(!init||!p.scoreAudit().scanComplete)return 4;
    }
    out.flush();std::cout<<"Routing inventory files="<<paths.size()<<" recorded quality-bearing files="<<affected<<'\n';return out?0:5;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    if(argc==2&&std::wstring(argv[1])==L"--exclusions"){exclusions();return 0;}
    if(argc==4&&std::wstring(argv[1])==L"--inventory")return inventory(argv[2],argv[3]);
    if(argc!=1)return 2;numerical();integration();exclusions();std::cout<<"Recorded Audio resampling tests PASS\n";return 0;
}
#else
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--exclusions"){exclusions();return 0;}
    if(argc==4&&std::string(argv[1])=="--inventory")return inventory(argv[2],argv[3]);
    if(argc!=1)return 2;numerical();integration();exclusions();std::cout<<"Recorded Audio resampling tests PASS\n";return 0;
}
#endif
