// SPDX-License-Identifier: Apache-2.0
// Host-only prototype tests. No app renderer or manufacturer calibration.
#include "fm-native-clock-probe.h"
#include "fm-causal-decimator.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace fxchain::smaf;
using fm_causal::Stereo;
constexpr double tau=6.283185307179586;
void require(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
std::vector<double> dense(const fm_causal::Config& c) {
    std::vector<double> result{1};size_t stride=1;
    for(size_t j=0;j<c.taps.size();++j){auto h=fm_causal::kernel(c.taps[j],j+1==c.taps.size()?c.lastFraction:1.);
        std::vector<double> next(result.size()+(h.size()-1)*stride,0.);
        for(size_t a=0;a<result.size();++a)for(size_t b=0;b<h.size();++b)next[a+b*stride]+=result[a]*h[b];
        result=std::move(next);stride*=2;}return result;
}
void nativeState(const FmVoice& a,const FmVoice& b) {
    require(a.lfoPhase_==b.lfoPhase_&&a.lfoSin_==b.lfoSin_&&a.recentLevel_==b.recentLevel_,"native LFO/allocation level unchanged");
    require(a.active_==b.active_&&a.released_==b.released_&&a.keyOffRequested_==b.keyOffRequested_,"native lifecycle unchanged");
    for(int j=0;j<2;++j){require(a.ops_[j].phase_==b.ops_[j].phase_,"native oscillator phase unchanged");
        require(a.ops_[j].env_.level_==b.ops_[j].env_.level_&&a.ops_[j].env_.phase_==b.ops_[j].env_.phase_,"native envelope unchanged");
        require(a.ops_[j].env_.pendingKeyOff_==b.ops_[j].env_.pendingKeyOff_,"native Hold state unchanged");
        for(int k=0;k<2;++k)require(a.fbMem_[j][k]==b.fbMem_[j][k],"native operator history unchanged");}
}
FmVoicePatch patch(int profile,int lfo,int mode) {
    FmVoicePatch p;p.modernTimbre=profile!=0;p.algorithm=0;p.fourOp=false;p.lfo=2;
    for(int j=0;j<2;++j){auto&o=p.ops[j];o.wave=0;o.multi=uint8_t(j==0?3:2);o.tl=uint8_t(j==0?24:7);
        o.ar=15;o.dr=o.sr=o.sl=0;o.rr=6;o.egType=true;o.ma35Multipliers=profile==2;
        o.am=(lfo&1)!=0;o.vib=(lfo&2)!=0;o.dvb=o.dam=3;o.holdEnabled=true;o.ksr=1;
        if(mode==1){o.ar=9;o.dr=7;o.sr=4;o.sl=6;}
        if(mode==2&&j==0)o.ar=0;
        if(mode==3){o.ar=0;o.rateLowBits=8;}}
    return p;
}
void sourceChecks() {
    size_t samples=0,cases=0;double fastMaximum=0,fastSquares=0;
    for(int rate:{8000,44100})for(int profile:{0,1,2})for(int lfo=0;lfo<4;++lfo)for(int mode=0;mode<4;++mode)
    for(int factor:{2,4,8,16,32}) {
        const auto p=patch(profile,lfo,mode);FmVoice actual,oracle,fast;actual.setSampleRate(rate);oracle.setSampleRate(rate);fast.setSampleRate(rate);
        actual.noteOn(p,rate*.03,.73f);oracle.noteOn(p,rate*.03,.73f);fast.noteOn(p,rate*.03,.73f);std::array<float,32> buffer{},fastBuffer{};
        for(int n=0;n<768;++n){
            auto controls=[&](FmVoice& v){if(n==64)v.setVibratoScale(8);if(n==128)v.setPitch(rate*.71);
                if(n==192)v.setPitch(rate*.031);if(n==256)v.setVolume(0);if(n==320)v.setVolume(.63f);
                if(n==384)v.noteOff(true);if(n==448)v.setHold(false);if(n==640)v.noteOn(p,317,.51f);};
            controls(actual);controls(oracle);controls(fast);
            const auto r=fm_probe::frame(actual,std::span<float>(buffer.data(),size_t(factor)));const float expected=oracle.tick();
            require(r.native==expected&&buffer[size_t(factor-1)]==expected,"public native endpoint unchanged");
            require(r.reconstructedEndpoint==expected,"independent subframe endpoint/gains agree with native");
            nativeState(actual,oracle);
            const auto fr=fm_probe::frameFast(fast,std::span<float>(fastBuffer.data(),size_t(factor)));nativeState(fast,oracle);
            require(fr.native==expected&&fr.reconstructedEndpoint==expected,"fast source changed native endpoint");
            for(int j=0;j<factor;++j){require(std::isfinite(buffer[size_t(j)]),"finite subframe");
                const double delta=double(buffer[size_t(j)])-fastBuffer[size_t(j)];require(std::abs(delta)<3e-6,"fast two-op waveform error");
                fastMaximum=std::max(fastMaximum,std::abs(delta));fastSquares+=delta*delta;++samples;}
        }++cases;
    }
    // Independent continuous two-sine equation validates interior subframes.
    for(bool vm35:{false,true})for(int factor:{2,4,8,16,32}) {
        auto p=patch(vm35?2:1,0,0);for(auto&o:p.ops){o.ksr=0;o.tl=0;}
        FmVoice v,fast;v.setSampleRate(44100);fast.setSampleRate(44100);v.noteOn(p,44100.*17/4096,1);fast.noteOn(p,44100.*17/4096,1);
        std::array<float,32> b{},fb{};
        for(int n=0;n<512;++n){(void)fm_probe::frame(v,std::span<float>(b.data(),size_t(factor)));(void)fm_probe::frameFast(fast,std::span<float>(fb.data(),size_t(factor)));
            for(int j=0;j<factor;++j){double t=n+double(j+1)/factor;
                double m=std::sin(tau*17*3*t/4096);double expected=.7*std::sin(tau*17*2*t/4096+tau*(vm35?4:.42)*m);
                require(std::abs(b[size_t(j)]-expected)<3e-6&&std::abs(fb[size_t(j)]-expected)<3e-6,"interior subframes disagree with continuous PM oracle");}}
    }
    // Unsupported algorithms/waves/feedback must be rejected before any tick.
    for(int kind=0;kind<3;++kind)for(bool fast:{false,true}){auto p=patch(2,0,0);if(kind==0)p.algorithm=1;if(kind==1)p.ops[0].wave=8;if(kind==2)p.feedback=3;
        FmVoice v;v.setSampleRate(44100);v.noteOn(p,317,1);auto original=v;std::array<float,8> b;b.fill(123);
        bool rejected=false;try{if(fast)fm_probe::frameFast(v,b);else fm_probe::frame(v,b);}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&std::all_of(b.begin(),b.end(),[](float x){return x==123;}),"unsupported source must not render");nativeState(v,original);}
    std::cout<<"Fast two-op delta max="<<fastMaximum<<" RMS="<<std::sqrt(fastSquares/samples)<<"; ";
    std::cout<<"Native-clock sine source: "<<cases<<" cases; "<<samples<<" subframes PASS\n";
}
void busChecks() {
    size_t count=0;
    const int factors[]={2,4,8,16,32},delays[]={64,68,70,71,72},costs[]={65,83,119,191,337};
    for(int k=0;k<5;++k){int factor=factors[k];auto config=fm_causal::alignedConfig(factor);fm_causal::Decimator d(config);
        require(d.delayFrames()==delays[k]&&d.multipliesPerOutput()==costs[k],"aligned delay/cost oracle");
        const auto h=dense(config);
        for(int taps:config.taps){const auto coeff=fm_causal::kernel(taps);double sum=0;
            for(size_t j=0;j<coeff.size();++j){sum+=coeff[j];require(coeff[j]==coeff[coeff.size()-1-j],"aligned kernel symmetry");
                if(int(j)!=(taps-1)/2&&(int(j)-(taps-1)/2)%2==0)require(coeff[j]==0,"aligned halfband zero parity");}
            require(std::abs(sum-1)<1e-14,"aligned DC gain");}
        require(std::abs(std::abs(fm_causal::response(config,tau*1000/(44100*factor)))-1)<.01,"aligned low-band gain");
        for(int phase=0;phase<factor;++phase){std::vector<double> x(h.size()+size_t(factor*32),0.);x[size_t(phase)]=1;
            const auto actual=fm_causal::stream(x,config);for(size_t i=0;i<actual.size();++i){int64_t p=int64_t((i+1)*size_t(factor)-1)-phase;
                double expected=p>=0&&size_t(p)<h.size()?h[size_t(p)]:0.;require(std::abs(actual[i]-expected)<1e-13,"aligned impulse/dense convolution mismatch");}++count;}
        constexpr int inputFrames=360;fm_causal::AlignedBus bus(factor);const int total=inputFrames+bus.tailBoundFrames()+8;
        std::vector<Stereo> high(size_t(total*factor)),native(static_cast<size_t>(total)),reference(static_cast<size_t>(total)),actual;
        for(int n=0;n<inputFrames;++n){if(n>=48&&n<330)native[size_t(n)]={.1*std::cos(n*.17),.03*std::sin(n*.23)};
            for(int j=0;j<factor;++j)if(n>=12&&n<245){double t=n+double(j+1)/factor;high[size_t(n*factor+j)]={.2*std::sin(t*.31)+.1,.13*std::cos(t*.41)};}}
        const int pad=72-delays[k];
        for(int n=0;n<total;++n){Stereo expected{};
            if(n>=pad){int64_t latest=int64_t(n-pad+1)*factor-1;
                for(size_t j=0;j<h.size();++j)if(latest>=int64_t(j)){const auto x=high[size_t(latest-int64_t(j))];expected.left+=x.left*h[j];expected.right+=x.right*h[j];}}
            if(n>=72){expected.left+=native[size_t(n-72)].left;expected.right+=native[size_t(n-72)].right;}
            reference[size_t(n)]=expected;
        }
        for(int block:{1,7,64,257}){bus.reset();actual.clear();for(int begin=0;begin<total;begin+=block)
                for(int n=begin;n<std::min(total,begin+block);++n)actual.push_back(bus.frame(high.data()+n*factor,size_t(factor),native[size_t(n)]));
            for(int n=0;n<total;++n){require(std::abs(actual[size_t(n)].left-reference[size_t(n)].left)<2e-13&&std::abs(actual[size_t(n)].right-reference[size_t(n)].right)<2e-13,"stereo/native-delay/gate/tail oracle");}
            bool tail=false;for(int n=245;n<330;++n)tail=tail||actual[size_t(n)].left!=native[size_t(std::max(0,n-72))].left;
            require(tail,"gate must not discard FIR history");
            for(int n=inputFrames+bus.tailBoundFrames();n<total;++n)require(actual[size_t(n)].left==0&&actual[size_t(n)].right==0,"tail exceeds exact bound");++count;}
        // Reset after nonzero history must be equivalent to a fresh bus.
        fm_causal::AlignedBus fresh(factor);bus.reset();for(int n=0;n<150;++n){const auto a=bus.frame(high.data()+n*factor,size_t(factor),native[size_t(n)]),b=fresh.frame(high.data()+n*factor,size_t(factor),native[size_t(n)]);
            require(a.left==b.left&&a.right==b.right,"seek/global-reset leaves stale history");}
        std::array<Stereo,32> zeros{};bus.reset();
        for(int n=0;n<100;++n){const auto y=bus.frame(zeros.data(),size_t(factor),n==20?Stereo{1,-1}:Stereo{});
            require(y.left==(n==92?1:0)&&y.right==(n==92?-1:0),"native PCM path must delay exactly72 frames");}
    }
    std::cout<<"Integer-delay stereo/gate/tail/reset checks: "<<count<<" PASS\n";
}
void combined() {
    constexpr int factor=8,frames=640;auto p=patch(2,3,0);for(auto&o:p.ops)o.rr=15;
    FmVoice source,referenceSource;source.setSampleRate(44100);referenceSource.setSampleRate(44100);
    source.noteOn(p,317,.63f);referenceSource.noteOn(p,317,.63f);
    fm_causal::AlignedBus bus(factor);const auto h=dense(fm_causal::alignedConfig(factor));
    std::vector<Stereo> high(frames*factor),native(frames),actual(frames);std::array<float,factor> b{};
    bool held=false,tail=false;
    for(int n=0;n<frames;++n){auto events=[&](FmVoice& v){if(n==120)v.noteOff(true);if(n==200)v.setHold(false);
            if(n==300)v.noteOn(p,503,.7f);if(n==380)v.noteOff(false);};events(source);events(referenceSource);
        const auto r=fm_probe::frame(source,b);require(r.native==referenceSource.tick(),"combined native endpoint");nativeState(source,referenceSource);
        if(n==199)held=source.active()&&!source.released();
        for(int j=0;j<factor;++j)high[size_t(n*factor+j)]={b[size_t(j)]*.7,b[size_t(j)]*.3};
        if(n<360)native[size_t(n)]={.03*std::sin(n*.11),.02*std::cos(n*.19)};
        actual[size_t(n)]=bus.frame(high.data()+n*factor,factor,native[size_t(n)]);
        if(n>440&&!source.active()&&(actual[size_t(n)].left!=0||actual[size_t(n)].right!=0))tail=true;
    }
    require(held,"combined source preserves Hold-deferred key-off");require(tail,"finished source must retain pending filter tail");
    const int padding=72-70;
    for(int n=0;n<frames;++n){Stereo expected{};
        if(n>=padding){const int latest=(n-padding+1)*factor-1;
            for(size_t j=0;j<h.size();++j)if(latest>=int(j)){expected.left+=high[size_t(latest-int(j))].left*h[j];expected.right+=high[size_t(latest-int(j))].right*h[j];}}
        if(n>=72){expected.left+=native[size_t(n-72)].left;expected.right+=native[size_t(n-72)].right;}
        require(std::abs(actual[size_t(n)].left-expected.left)<2e-13&&std::abs(actual[size_t(n)].right-expected.right)<2e-13,"combined envelope/gate/PCM timeline oracle");
    }
    require(actual.back().left==0&&actual.back().right==0,"combined EOF fully drains");
    std::cout<<"Combined native-clock FM/PCM Hold/gate/retrigger/EOF prototype PASS\n";
}
int main(){sourceChecks();busChecks();combined();}
