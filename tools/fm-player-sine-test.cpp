// SPDX-License-Identifier: Apache-2.0
// Actual MaPlayer scheduling/routing, with dense FIR and original-render controls.
#include <optional>
#include "ma_sine_output.h"
#include "fm-parallel-pair-probe.h"
#define private public
#include "ma_player.h"
#undef private
#include "fm-causal-decimator.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace fxchain::smaf;
using Bytes = std::vector<uint8_t>;
void require(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
void exclusive(Bytes& setup, const Bytes& voice) {
    setup.insert(setup.end(), {0xf0, uint8_t(voice.size() + 1)});
    setup.insert(setup.end(), voice.begin(), voice.end()); setup.push_back(0xf7);
}
SmafFile fixture(int kind) {
    SmafFile f;
    if (kind == 0 || kind == 3) {
        TrackChunk t; t.formatType = 2; t.channelStatus.resize(16);
        t.durationTimeBase = t.gateTimeBase = 0;
        t.sequenceData = {0,0x90,69,100,10,20,0xff,0x2f,0};
        Bytes v{0x43,5,1,0,0,60,1,0};
        for (int j=0;j<2;++j) v.insert(v.end(), {0,0x90,0xf0,0,0,0x10,0});
        exclusive(t.setupData, v); f.tracks.push_back(t);
    }
    if (kind == 1 || kind == 3) {
        TrackChunk t; t.formatType=2; t.trackNumber=1; t.channelStatus.resize(16);
        t.durationTimeBase=t.gateTimeBase=0;
        t.sequenceData={0,0xb0,0,125,0,0xb0,7,127,0,0xb0,11,127,
                        0,0x90,0,100,20,30,0xff,0x2f,0};
        WaveData w; w.number=1; w.samplingRate=8000; w.bitsPerSample=8; w.adpcm=false;
        for (int i=0;i<700;++i) w.data.push_back(uint8_t(128+24*std::sin(i*.17)));
        t.waves.push_back(w); f.tracks.push_back(t);
    }
    if (kind == 2 || kind == 3) {
        TrackChunk t; t.isAudioTrack=true; t.trackNumber=0; t.formatType=0;
        t.durationTimeBase=t.gateTimeBase=0x13; t.sequenceData={0,1,100};
        WaveData w; w.number=1; w.samplingRate=8000; w.bitsPerSample=8;
        w.adpcm=false; w.signedPcm8=true;
        for (int i=0;i<600;++i) w.data.push_back(uint8_t(int8_t(20*std::sin(i*.11))));
        t.waves.push_back(w); f.tracks.push_back(t);
    }
    if (kind == 4) {
        TrackChunk t; t.formatType=3; t.channelStatus.resize(32);
        t.durationTimeBase=t.gateTimeBase=0;
        exclusive(t.setupData, {0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0,0xf0,0,0,0,0,0,0,0,80,1});
        Bytes w{0x43,0x79,7,0x7f,3,1,2};
        for (int i=0;i<80;++i) w.push_back(uint8_t(128+(i%8<4?16:-16)));
        exclusive(t.setupData,w); t.sequenceData={0,0x10,69,100,10,20,0xff,0};
        f.tracks.push_back(t);
    }
    return f;
}
std::vector<float> drain(MaPlayer& p, int block) {
    std::vector<float> out, data(size_t(block)*2);
    int n; while ((n=p.render(data.data(),block))>0) {
        require(n<=block,"render overrun");
        out.insert(out.end(),data.begin(),data.begin()+2*n);
        require(out.size()<=2*p.totalSamples(),"physical output exceeds advertised bound");
    }
    require(p.finished() && p.render(data.data(),block)==0,"physical EOF remains final");
    return out;
}
void shifted(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty()) { require(b.empty(),"empty score gained padding"); return; }
    if (b.size()!=a.size()+144) {
        std::cerr<<"sizes original="<<a.size()<<" delayed="<<b.size()<<'\n';
        require(false,"native alignment changed exact output length");
    }
    require(std::all_of(b.begin(),b.begin()+144,[](float x){return x==0;}),"native latency prefix");
    require(std::equal(a.begin(),a.end(),b.begin()+144),"native output differs after exact72-frame shift");
}
void bus() {
    size_t frames=0;
    for (int factor : {1,2,4,8,16,32}) {
        sine_output::Bus a(factor); fm_causal::AlignedBus oracle(factor==1?2:factor);
        for (int n=0;n<900;++n) {
            std::array<sine_output::Stereo,32> x{};
            std::array<fm_causal::Stereo,32> y{};
            for(int k=0;k<factor;++k) if(n<600) {
                x[size_t(k)]={std::sin((n*factor+k)*.17),std::cos((n*factor+k)*.21)*.3};
                y[size_t(k)]={x[size_t(k)].left,x[size_t(k)].right};
            }
            if (factor==1) { x[0]={}; y[0]=y[1]={}; }
            sine_output::Stereo native=n<500?sine_output::Stereo{.1*std::sin(n*.07),-.03}:sine_output::Stereo{};
            const auto actual=a.frame(std::span(x.data(),size_t(factor)),native);
            const auto expected=oracle.frame(y.data(),size_t(factor==1?2:factor),{native.left,native.right});
            require(actual.left==expected.left && actual.right==expected.right,"core bus differs from validated streaming oracle");
            ++frames;
        }
        a.reset(); sine_output::Bus fresh(factor);
        auto copy=a; std::array<sine_output::Stereo,32> zeros{};
        for (int n=0;n<300;++n) {
            auto x=a.frame(std::span(zeros.data(),size_t(factor)),n==0?sine_output::Stereo{1,-1}:sine_output::Stereo{});
            auto y=fresh.frame(std::span(zeros.data(),size_t(factor)),n==0?sine_output::Stereo{1,-1}:sine_output::Stereo{});
            require(x.left==y.left&&x.right==y.right,"reset differs from fresh histories");
            auto z=copy.frame(std::span(zeros.data(),size_t(factor)),{});
            require(z.left==0&&z.right==0,"bus copy aliases mutable history");
        }
    }
    std::cout<<frames<<" core bus/validated streaming comparisons, reset and copy isolation PASS\n";
}
void publicPaths() {
    size_t cases=0;
    for (unsigned rate : {8000u,44100u}) for (int kind=0;kind<5;++kind) {
        auto file=fixture(kind); MaPlayer old; require(old.init(file,rate),"original source init");
        const auto base=drain(old,512); require(!base.empty(),"original source must produce frames");
        require(std::any_of(base.begin(),base.end(),[](float x){return std::abs(x)>1e-5f;}),"original source must be audible");
        for (int factor : {1,8}) {
            if (factor==8 && (kind==0||kind==3)) continue;
            MaPlayer p; require(p.init(file,rate,false,{factor}),"aligned native source init");
            require(p.renderLatencySamples()==72,"experimental latency declaration");
            shifted(base,drain(p,17)); p.seekToStart(); shifted(base,drain(p,1024));
            require(p.init(file,rate)&&p.renderLatencySamples()==0,"reinit restores ordinary path");
            require(drain(p,31)==base,"reinit ordinary path changed"); ++cases;
        }
    }
    std::cout<<cases<<" public FM/recorded/ATR/WT/mixed exact native-shift cases PASS\n";
}
FmVoicePatch patch(bool pairs=false) {
    FmVoicePatch p; p.modernTimbre=true; p.fourOp=pairs; p.algorithm=pairs?5:0; p.lfo=2;
    for(int j=0;j<4;++j) {auto& o=p.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;
        o.wave=0;o.multi=uint8_t(j%2?2:3);o.tl=uint8_t(j%2?0:24);
        o.am=o.vib=true;o.dam=o.dvb=2;o.holdEnabled=true;}
    return p;
}
void install(MaPlayer& p,int factor,bool pairs,FmSubframeMath math=FmSubframeMath::Exact) {
    require(p.init(fixture(3),8000,false,{factor,math}),"actual mixed source init");
    p.voiceTable_[0].patch=patch(pairs);
    using E=MaPlayer::Ev;
    for(auto e:std::vector<E>{{100,E::Hold,0,127,0},{120,E::AllNotesOff,0,0,0},
        {140,E::AllSoundOff,0,0,0},{141,E::NoteOn,0,69,100,999},
        {180,E::Volume,0,0,0},{220,E::Volume,0,100,0},{250,E::AllSoundOff,32,0,0},
        {260,E::PitchBend,0,4096,0},{280,E::Pan,0,100,0},{300,E::Hold,0,0,0},
        {330,E::AllSoundOff,0,0,0},{331,E::NoteOn,0,73,100,1000},
        {400,E::AllSoundOff,0,0,0},{420,E::AllSoundOff,33,0,0},{800,E::ScoreNop,0,0,0}})
        p.events_.push_back(e);
    std::stable_sort(p.events_.begin(),p.events_.end(),[](const E&a,const E&b){return a.sample<b.sample;});
    p.nonAtrGateEventEnd_=p.events_.size(); p.scoreEndSample_=800; p.endSample_=1800;
    // An unsupported FM graph and a keyed PCM voice use actual engine classes.
    auto q=patch();q.feedback=3;auto& fm=p.pool_[31];fm.noteOn(q,417,.3f);fm.channel=33;
    p.poolPanLeft_[31]=.2f;p.poolPanRight_[31]=.8f;
    static const auto pcm=[] {std::array<int16_t,600> x{};for(size_t n=0;n<x.size();++n)x[n]=int16_t(2000*std::sin(n*.13));return x;}();
    auto& v=p.pcmPool_[0];v=MaPlayer::PcmVoice{};v.pcm=pcm.data();v.len=pcm.size();
    v.active=true;v.channel=32;v.keyNote=60;v.noteId=700;v.rate=.75;v.baseRate=.75;
    v.panLeft=.7f;v.panRight=.3f;v.env.configure(q.ops[1],8000);v.env.keyOn();
}
std::vector<double> dense(int factor) {
    std::vector<double> h{1};size_t stride=1;
    for (int taps:fm_causal::alignedConfig(factor).taps) {
        auto k=fm_causal::kernel(taps); std::vector<double> next(h.size()+(k.size()-1)*stride,0);
        for(size_t i=0;i<h.size();++i)for(size_t j=0;j<k.size();++j)next[i+j*stride]+=h[i]*k[j];
        h=std::move(next);stride*=2;
    }
    return h;
}
void scheduledDense() {
    size_t checks=0;double maxError=0;
    for (bool hot:{false,true}) for (bool pairs:{false,true}) for (int factor:{2,4,8,16,32}) {
        MaPlayer actual,source;install(actual,factor,pairs);install(source,0,pairs);
        if(hot)for(auto* player:{&actual,&source})for(int i=0;i<31;++i) {
            player->pool_[i].noteOn(patch(pairs),317,.9f);player->pool_[i].channel=5;
            player->poolPanLeft_[i]=player->poolPanRight_[i]=.70710677f;
        }
        const auto h=dense(factor);
        const int pad=72-int(fm_causal::Decimator(fm_causal::alignedConfig(factor)).delayFrames());
        std::vector<fm_causal::Stereo> high; std::vector<std::array<float,2>> native;
        float lp1L=0,lp2L=0,lp1R=0,lp2R=0,lim=0;
        for(int n=0;n<600;++n) {
            while(source.nextEvent_<source.events_.size()&&source.events_[source.nextEvent_].sample<=uint64_t(n))
                source.fireEvent_(source.events_[source.nextEvent_++]);
            std::array<fm_causal::Stereo,32> x{};float l=0,r=0;
            for(int i=0;i<32;++i) {auto& v=source.pool_[i];if(!v.active())continue;
                std::array<float,32> samples;
                if(fm_probe::eligible(v)||fm_pairs::eligible(v)) {
                    require(v.tickSineSubframes(samples.data(),factor),"reference sine source");
                    for(int j=0;j<factor;++j) {x[size_t(j)].left+=samples[size_t(j)]*.32f*source.poolPanLeft_[i];x[size_t(j)].right+=samples[size_t(j)]*.32f*source.poolPanRight_[i];}
                } else {const auto s=v.tick();l+=s*.32f*source.poolPanLeft_[i];r+=s*.32f*source.poolPanRight_[i];}
            }
            for(auto& v:source.pcmPool_)if(v.active){auto s=v.tick();l+=s*.32f*v.panLeft;r+=s*.32f*v.panRight;}
            for(auto& v:source.audioPool_)if(v.active){auto s=v.tick();l+=s*.32f*v.panLeft;r+=s*.32f*v.panRight;}
            for(int j=0;j<factor;++j)high.push_back(x[size_t(j)]);native.push_back({l,r});
            fm_causal::Stereo filtered;
            const int64_t end=int64_t(n-pad+1)*factor-1;
            for(size_t k=0;k<h.size();++k)if(end-int64_t(k)>=0) {
                filtered.left+=h[k]*high[size_t(end-int64_t(k))].left;
                filtered.right+=h[k]*high[size_t(end-int64_t(k))].right;
            }
            if(n>=72){filtered.left+=native[size_t(n-72)][0];filtered.right+=native[size_t(n-72)][1];}
            l=float(filtered.left);r=float(filtered.right);
            lp1L+=source.lpCoef_*(l-lp1L);lp2L+=source.lpCoef_*(lp1L-lp2L);
            lp1R+=source.lpCoef_*(r-lp1R);lp2R+=source.lpCoef_*(lp1R-lp2R);l=lp2L;r=lp2R;
            lim=std::max(std::max(std::abs(l),std::abs(r)),lim*source.limRelease_);
            if(lim>.92f){const auto gain=.92f/lim;l*=gain;r*=gain;}
            float y[2];require(actual.render(y,1)==1,"mixed actual render ended before future event");
            const double error=std::max(std::abs(double(y[0])-l),std::abs(double(y[1])-r));maxError=std::max(maxError,error);
            require(error<1e-6,"actual scheduled mix differs from dense FIR and output-stage reference");
            require(actual.diagnostics().fmNotes==source.diagnostics().fmNotes&&actual.diagnostics().audioNotes==source.diagnostics().audioNotes,"scheduled source allocation differs");
            for(int i=0;i<32;++i) {
                require(actual.pool_[i].recentLevel()==source.pool_[i].recentLevel(),"render routing changed native voice allocation clock");
                for(int j=0;j<4;++j)require(actual.pool_[i].ops_[j].phase_==source.pool_[i].ops_[j].phase_,"render routing advanced oscillator twice");
            }
            source.cursor_=uint64_t(n+1);++checks;
        }
        if(hot)require(actual.diagnostics().limitedFrames>0,"hot mix must exercise existing limiter");
    }
    std::cout<<checks<<" actual mixed scheduled/dense-FIR comparisons PASS; max error="<<maxError<<'\n';
}
void replayAndCaps() {
    size_t cases=0;
    for(bool pairs:{false,true})for(int factor:{2,4,8,16,32})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite}) {
        MaPlayer p;install(p,factor,pairs,math);const auto baseline=drain(p,1);
        for(int block:{17,512,1024}){install(p,factor,pairs,math);require(drain(p,block)==baseline,"block size changes actual filtered output or EOF");}
        ++cases;
    }
    // Use a real score for seek: no manually injected table/source state.
    for(int factor:{1,2,8,32}) {
        MaPlayer p;require(p.init(fixture(3),8000,false,{factor,FmSubframeMath::Hermite}),"seek init");
        const auto first=drain(p,17);p.seekToStart();require(drain(p,512)==first,"seek did not reset all bus histories and clocks");
    }
    // A live held note at the hard boundary, with EOF and fade compared exactly.
    for(int factor:{0,1,8,32}) {
        MaPlayer p;install(p,factor,false);p.events_.clear();
        p.events_={{0,MaPlayer::Ev::NoteOn,0,69,100,1},{1000,MaPlayer::Ev::ScoreNop,0,0,0}};
        p.nextEvent_=0;p.nonAtrGateEventEnd_=2;p.scoreEndSample_=1000;p.endSample_=300;
        if(factor==0){auto base=drain(p,17);MaPlayer q;install(q,1,false);q.events_=p.events_;q.nextEvent_=0;q.nonAtrGateEventEnd_=2;q.scoreEndSample_=1000;q.endSample_=300;shifted(base,drain(q,31));}
        else {auto x=drain(p,1024);require(x.size()==744&&x.back()==0&&p.diagnostics().tailLimitReached,"hard output cap/fade/diagnostic");}
    }
    std::cout<<cases<<" filtered block cases, public seek and delayed hard caps PASS\n";
}
void finalFrame() {
    size_t cases=0;
    for(int factor:{2,4,8,16,32})for(float gain:{1.0f,1e-12f}) {
        MaPlayer p;require(p.init(fixture(0),8000,false,{factor}),"last-frame init");
        for(auto& v:p.pool_){v=FmVoice{};v.setSampleRate(8000);}
        for(auto& v:p.pcmPool_)v.active=false;for(auto& v:p.audioPool_)v.active=false;
        auto q=patch();p.pool_[0].noteOn(q,317,gain);p.pool_[0].channel=42;
        p.poolPanLeft_[0]=.7f;p.poolPanRight_[0]=.3f;
        auto oracle=p.pool_[0];std::array<float,32> samples;
        require(oracle.tickSineSubframes(samples.data(),factor),"last-frame source oracle");
        p.events_={{0,MaPlayer::Ev::ScoreNop,0,0,0},{1,MaPlayer::Ev::AllSoundOff,42,0,0}};
        p.nextEvent_=0;p.nonAtrGateEventEnd_=2;p.scoreEndSample_=1;p.endSample_=10000;
        p.lpCoef_=1;const auto out=drain(p,17);const auto h=dense(factor);
        const int pad=72-int(fm_causal::Decimator(fm_causal::alignedConfig(factor)).delayFrames());
        const int bound=sine_output::Bus(factor).tailBoundFrames();
        require(out.size()>=2*size_t(bound+1),"final frame FIR history was truncated");
        require(std::any_of(out.begin()+146,out.end(),[](float x){return x!=0;}),"tiny/last frame lost its late FIR support");
        float lp1[2]{},lp2[2]{};
        for(size_t n=0;n<out.size()/2;++n)for(int ch=0;ch<2;++ch) {
            const int64_t end=(int64_t(n)-pad+1)*factor-1;double sum=0;
            for(int k=0;k<factor;++k)if(end-k>=0&&uint64_t(end-k)<h.size())
                sum+=double(samples[size_t(k)]*.32f*(ch==0?.7f:.3f))*h[size_t(end-k)];
            const auto value=float(sum);lp1[ch]+=value-lp1[ch];lp2[ch]+=lp1[ch]-lp2[ch];
            require(std::abs(double(out[n*2+size_t(ch)])-lp2[ch])<double(gain)*1e-6,"final-frame output differs from complete dense impulse support");
        }
        ++cases;
    }
    std::cout<<cases<<" final-frame and tiny-signal complete FIR tails PASS\n";
}

void zeroFm() {
    MaPlayer old,filtered;
    for(auto* p:{&old,&filtered}) {
        require(p->init(fixture(0),8000,false,{p==&old?0:8}),"zero FM init");
        auto q=patch();q.ops[0].ar=q.ops[1].ar=0;
        p->pool_[0].noteOn(q,317,1);p->pool_[0].channel=0;
        p->events_={{800,MaPlayer::Ev::ScoreNop,0,0,0}};
        p->nextEvent_=0;p->nonAtrGateEventEnd_=1;p->scoreEndSample_=800;p->endSample_=8800;
    }
    shifted(drain(old,17),drain(filtered,512));
    std::cout<<"Exactly zero FM input adds only the common alignment delay PASS\n";
}

void guards() {
    for(int factor:{-1,3,64,std::numeric_limits<int>::max()}) {
        MaPlayer p;require(!p.init(fixture(0),8000,false,{factor}),"invalid policy accepted");
    }
    MaPlayer p;require(!p.init(fixture(0),8000,false,{8,static_cast<FmSubframeMath>(99)}),"invalid math accepted");
    require(p.init(fixture(0),8000,false,{8}),"guard valid init");
    float values[2]={123,456};require(p.render(values,0)==0&&values[0]==123&&values[1]==456,"zero block mutates output");
    p.events_={{0,MaPlayer::Ev::ScoreNop,0,0,0}};p.nextEvent_=0;p.nonAtrGateEventEnd_=1;
    require(drain(p,17).empty(),"empty audible render gained delayed padding");
    std::cout<<"Policy guards, empty score and zero output block PASS\n";
}
int main() {bus();publicPaths();scheduledDense();replayAndCaps();finalFrame();zeroFm();guards();}
