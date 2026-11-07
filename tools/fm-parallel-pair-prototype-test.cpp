// SPDX-License-Identifier: Apache-2.0
#include "fm-parallel-pair-probe.h"
#include "fm-causal-decimator.h"
#include <cstdlib>
#include <iostream>
using namespace fxchain::smaf;
void require(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
FmVoicePatch patch(int profile,int lfo,int mode){FmVoicePatch p;p.algorithm=5;p.fourOp=true;p.modernTimbre=profile!=0;p.lfo=2;
    const int multis[4]={3,2,7,5};for(int j=0;j<4;++j){auto&o=p.ops[j];o.wave=0;o.multi=uint8_t(multis[j]);o.tl=uint8_t(j%2?7:24);
        o.ar=15;o.dr=o.sr=o.sl=0;o.rr=6;o.egType=true;o.holdEnabled=true;o.ksr=1;o.ma35Multipliers=profile==2;
        o.am=lfo&1;o.vib=lfo&2;o.dam=o.dvb=3;
        if(mode==1){o.ar=9;o.dr=7;o.sr=4;o.sl=6;}
        if(mode==2&&(j==0||j==3))o.ar=0;
        if(mode==3){o.ar=0;o.rateLowBits=8;}}
    return p;
}
void state(const FmVoice&a,const FmVoice&b){require(a.lfoPhase_==b.lfoPhase_&&a.lfoSin_==b.lfoSin_&&a.recentLevel_==b.recentLevel_,"native LFO/allocation clock changed");
    require(a.active_==b.active_&&a.released_==b.released_&&a.keyOffRequested_==b.keyOffRequested_,"native lifecycle changed");
    for(int j=0;j<4;++j){require(a.ops_[j].phase_==b.ops_[j].phase_&&a.ops_[j].env_.level_==b.ops_[j].env_.level_&&a.ops_[j].env_.phase_==b.ops_[j].env_.phase_&&a.ops_[j].env_.pendingKeyOff_==b.ops_[j].env_.pendingKeyOff_,"native operator phase/EG/Hold changed");
        for(int k=0;k<2;++k)require(a.fbMem_[j][k]==b.fbMem_[j][k],"native operator history changed");}}
void clocks(){size_t cases=0,samples=0;
    for(int rate:{8000,44100})for(int profile:{0,1,2})for(int lfo=0;lfo<4;++lfo)for(int mode=0;mode<4;++mode)for(int factor:{2,4,8,16,32}){
        auto p=patch(profile,lfo,mode);FmVoice actual,oracle;actual.setSampleRate(rate);oracle.setSampleRate(rate);actual.noteOn(p,rate*.013,.73f);oracle.noteOn(p,rate*.013,.73f);std::array<float,32> out{};
        for(int n=0;n<768;++n){auto events=[&](FmVoice&v){if(n==64)v.setVibratoScale(8);if(n==128)v.setPitch(rate*.71);if(n==192)v.setPitch(rate*.017);
                if(n==256)v.setVolume(0);if(n==320)v.setVolume(.63f);if(n==384)v.noteOff(true);if(n==448)v.setHold(false);if(n==640)v.noteOn(p,317,.51f);};
            events(actual);events(oracle);const auto r=fm_pairs::frame(actual,std::span<float>(out.data(),size_t(factor)));const float expected=oracle.tick();
            require(r.native==expected&&r.reconstructedEndpoint==expected&&out[size_t(factor-1)]==expected,"pair endpoint or reconstructed gains changed");state(actual,oracle);
            for(int j=0;j<factor;++j){require(std::isfinite(out[size_t(j)]),"nonfinite pair sample");++samples;}}
        ++cases;
    }
    std::cout<<cases<<" native-clock pair cases; "<<samples<<" subframes PASS\n";
}
void equations(){constexpr double tau=6.283185307179586;size_t checks=0;
    for(int profile:{1,2})for(int tl:{0,24})for(int factor:{2,8,32}){auto p=patch(profile,0,0);for(int j=0;j<4;++j){p.ops[j].ksr=0;p.ops[j].tl=uint8_t(j%2?0:tl);}
        FmVoice actual,higher;actual.setSampleRate(44100);higher.setSampleRate(44100*factor);actual.noteOn(p,44100.*17/4096,1);higher.noteOn(p,44100.*17/4096,1);
        const double depth=profile==2?4:.42,amplitude=std::pow(10.,-.75*tl/20.),index=tau*depth*amplitude;std::array<double,97>bessel{};
        for(int order=0;order<=96;++order)bessel[size_t(order)]=std::cyl_bessel_j(double(order),index);
        std::array<float,32> out{};
        for(int n=0;n<512;++n){fm_pairs::frame(actual,std::span<float>(out.data(),size_t(factor)));
            for(int sub=1;sub<=factor;++sub){const double t=n+double(sub)/factor;
                const double a=std::sin(tau*17*2*t/4096+index*std::sin(tau*17*3*t/4096));
                const double b=std::sin(tau*17*5*t/4096+index*std::sin(tau*17*7*t/4096));
                const double expected=.35*(a+b);require(std::abs(out[size_t(sub-1)]-expected)<3e-6,"independent continuous pair equation mismatch");
                require(std::abs(out[size_t(sub-1)]-higher.tick())<3e-6,"steady public high-rate voice mismatch");++checks;
                if((n==0||n==31||n==127||n==511)&&(sub==1||sub==factor/2||sub==factor)){
                    double sum=0;for(int order=-96;order<=96;++order){const double coeff=bessel[size_t(std::abs(order))]*(order<0&&(std::abs(order)&1)?-1:1);
                        sum+=.35*coeff*(std::sin(tau*17*(2+order*3)*t/4096)+std::sin(tau*17*(5+order*7)*t/4096));}
                    require(std::abs(out[size_t(sub-1)]-sum)<3e-6,"independent two-chain Bessel expansion mismatch");}
            }}
    }
    std::cout<<checks<<" continuous/high-rate pair comparisons and Bessel controls PASS\n";
}
void guards(){for(int kind=0;kind<6;++kind){auto p=patch(2,0,0);if(kind==0)p.fourOp=false;if(kind==1)p.algorithm=4;if(kind==2)p.ops[2].wave=6;
        if(kind==3)p.ops[3].fb=2;if(kind==4)p.feedback=2;FmVoice v;v.setSampleRate(44100);v.noteOn(p,317,1);const auto before=v;std::array<float,32> out;out.fill(123);
        bool rejected=false;try{fm_pairs::frame(v,std::span<float>(out.data(),kind==5?3:8));}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&std::all_of(out.begin(),out.end(),[](float x){return x==123;}),"unsupported pair source mutated output");state(v,before);}
    std::cout<<"Unsupported graph/wave/header or operator feedback rejected before tick PASS\n";
}
void spectrum(){constexpr int frames=4096,warmup=256;constexpr double tau=6.283185307179586;size_t cases=0;double maximumEquation=0,hard8=0,hard32=0;
    for(int profile:{1,2})for(int tl:{0,24})for(int factor:{2,4,8,16,32}){
        auto p=patch(profile,0,0);for(int j=0;j<4;++j){p.ops[j].ksr=0;p.ops[j].tl=uint8_t(j%2?0:tl);}
        FmVoice voice;voice.setSampleRate(44100);voice.noteOn(p,44100.*211/frames,1);
        fm_causal::AlignedBus bus(factor);const auto config=fm_causal::alignedConfig(factor);const int padding=72-int(fm_causal::Decimator(config).delayFrames());
        std::vector<double> measured(frames),exact(frames),band(frames);std::array<float,32> source{};std::array<fm_causal::Stereo,32> stereo{};
        for(int n=0;n<warmup+frames;++n){const auto r=fm_pairs::frame(voice,std::span<float>(source.data(),size_t(factor)));require(r.native==r.reconstructedEndpoint,"long steady pair endpoint drift");
            for(int k=0;k<factor;++k)stereo[size_t(k)]={source[size_t(k)],0};const auto y=bus.frame(stereo.data(),size_t(factor),{});require(y.right==0,"pair FIR stereo leakage");
            if(n>=warmup)measured[size_t(n-warmup)]=y.left;}
        const double index=tau*(profile==2?4:.42)*std::pow(10.,-.75*tl/20.);
        for(int order=-96;order<=96;++order){const double coeff=std::cyl_bessel_j(double(std::abs(order)),index)*(order<0&&(std::abs(order)&1)?-1:1);
            for(int branch=0;branch<2;++branch){const int bin=211*((branch==0?2:5)+order*(branch==0?3:7));
                const auto h=fm_causal::response(config,tau*bin/(frames*factor));
                for(int n=0;n<frames;++n){exact[size_t(n)]+=.35*coeff*std::imag(h*std::polar(1.,tau*bin*(n+1.+warmup-padding)/frames));
                    if(std::abs(bin)<frames/2)band[size_t(n)]+=.35*coeff*std::sin(tau*bin*(n+1.+warmup-72)/frames);}}}
        double equation=0,error=0;for(int n=0;n<frames;++n){equation+=std::pow(measured[size_t(n)]-exact[size_t(n)],2);error+=std::pow(measured[size_t(n)]-band[size_t(n)],2);}
        equation=std::sqrt(equation/frames);error=std::sqrt(error/frames);require(equation<3e-6&&std::isfinite(error),"filtered pair output differs from complex Bessel response");
        maximumEquation=std::max(maximumEquation,equation);if(profile==2&&tl==0&&factor==8)hard8=error;if(profile==2&&tl==0&&factor==32)hard32=error;++cases;
    }
    require(hard8>.05&&hard32<.002,"strong parallel PM alias experiment control changed");
    std::cout<<cases<<" causal pair FIR/Bessel cases; max equation RMS="<<maximumEquation<<"; strong8x="<<hard8<<"; strong32x="<<hard32<<" PASS\n";
}
int main(){clocks();equations();guards();spectrum();}
