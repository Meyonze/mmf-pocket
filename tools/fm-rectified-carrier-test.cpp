// SPDX-License-Identifier: Apache-2.0
// Analytic high-band references for original half/full-rectified sine carriers.
#include <array>
#include <vector>
#define private public
#include "ma_fm_core.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
using namespace fxchain::smaf;
constexpr double pi=std::numbers::pi,tau=2*pi;
void require(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
void referenceSweep() {
    constexpr int frames=2048;unsigned cases=0;double maxError=0;
    for(int wave:{1,2})for(double rate:{8000.0,44100.0})
    for(int bin:{1,17,127,256,511,512,513,550,600,650,683,769,1001})
    for(double initial:{0.0,0.17,0.499,0.75,std::nextafter(1.0,0.0)}) {
        const double step=double(bin)/frames;
        FmOpPatch patch;patch.wave=uint8_t(wave);
        FmOperator actual;actual.configure(patch,rate,0,true);actual.noteOn(rate*step);
        actual.phase_=initial;actual.setBandlimited(true);
        auto raw=actual;raw.setBandlimited(false);
        double phase=initial, oldError=0,newError=0,mean=0,sine=0,cosine=0;
        for(int i=0;i<frames;++i) {
            phase+=step;if(phase>=1)phase-=std::floor(phase);
            const float a=actual.tick(0),r=raw.tick(0);
            require(std::isfinite(a),"rectified carrier remains finite");
            if(step<=0.25)require(a==r,"rectified carrier preserves low/mid-band float output exactly");
            else {
                // All non-DC harmonics except WS1's 0.5*sin are above Nyquist.
                const double target=wave==1?1/pi+0.5*std::sin(tau*phase):2/pi;
                oldError+=(r-target)*(r-target);newError+=(a-target)*(a-target);
                mean+=a;sine+=a*std::sin(tau*phase);cosine+=a*std::cos(tau*phase);
            }
        }
        if(step>0.25) {
            require(newError<=oldError+1e-10,"high-band rectification error never increases");
            if(step>=1.0/3) {
                maxError=std::max(maxError,std::sqrt(newError/frames));
                require(std::sqrt(newError/frames)<2e-7,"full-band result matches independent DC/fundamental reference");
                require(std::abs(mean/frames-(wave==1?1/pi:2/pi))<2e-7,"analytic DC is retained");
                require(std::abs(2*sine/frames-(wave==1?0.5:0))<2e-7&&std::abs(2*cosine/frames)<2e-7,
                    "in-band fundamental amplitude and phase are retained");
            }
        }
        ++cases;
    }
    std::cout<<"Rectified sweep PASS: "<<cases<<" cases, full-band maximum RMS error="<<maxError<<'\n';
}
unsigned enabled(const FmVoice& v){unsigned mask=0;for(int i=0;i<4;++i)if(v.ops_[i].blepEnabled_)mask|=1u<<i;return mask;}
void eligibility() {
    constexpr unsigned masks[]={2,3,15,8,8,10,9,13};
    for(int wave:{1,2})for(int algorithm=0;algorithm<8;++algorithm) {
        FmVoicePatch p;p.modernTimbre=true;p.algorithm=uint8_t(algorithm);p.fourOp=algorithm>=2;
        for(int i=0;i<4;++i){p.ops[i].wave=uint8_t(wave);p.ops[i].ar=masks[algorithm]&(1u<<i)?15:0;}
        FmVoice v;v.setSampleRate(44100);v.noteOn(p,17000,1);
        require(enabled(v)==masks[algorithm],"only unmodulated final carriers receive rectified correction");
        for(bool legacy:{false,true}) {
            auto nonlinear=p;nonlinear.modernTimbre=!legacy;
            for(auto& op:nonlinear.ops){op.ar=15;op.fb=3;}
            v.noteOn(nonlinear,17000,1);require(enabled(v)==0,"legacy/feedback paths remain excluded");
            auto old=v;for(auto& op:old.ops_)op.setBandlimited(false);
            for(int i=0;i<1024;++i){if(i==257){v.setPitch(16000);old.setPitch(16000);}require(v.tick()==old.tick(),"excluded nonlinear path remains exact");}
        }
        if(algorithm==0||algorithm==4) {
            for(auto& op:p.ops)op.ar=15;
            v.noteOn(p,17000,1);require(enabled(v)==0,"live serial modulation remains excluded");
        }
    }
}
void dynamics() {
    for(int wave:{1,2})for(double base:{0.24,0.30,0.49}) {
        FmOpPatch p;p.wave=uint8_t(wave);p.vib=true;p.dvb=3;
        FmOperator actual;actual.configure(p,44100,0,true);actual.noteOn(44100*base);actual.setBandlimited(true);
        auto raw=actual;raw.setBandlimited(false);double phase=0;
        for(int i=0;i<4096;++i) {
            const double lfo=std::sin(tau*(i+1)/1024),step=base*(1+0.01548*lfo*8);
            phase+=step;if(phase>=1)phase-=std::floor(phase);
            const float a=actual.tick(0,lfo,8),r=raw.tick(0,lfo,8);
            require(std::isfinite(a)&&std::abs(a)<1.01,"vibrato stays finite through all correction boundaries");
            if(step>=0.5)require(a==0,"instantaneous Nyquist suppression stays exact");
            else if(step<=0.25)require(a==r,"vibrato returning below correction restores raw rounding");
            else if(step>=1.0/3) {
                const double target=wave==1?1/pi+0.5*std::sin(tau*phase):2/pi;
                require(std::abs(a-target)<2e-7,"vibrato uses instantaneous correction amount and integrated phase");
            }
        }
    }
    for(int wave:{1,2})for(double step:{0.25,0.4}) {
        FmOpPatch p;p.wave=uint8_t(wave);FmOperator actual;
        actual.configure(p,44100,0,true);actual.noteOn(44100*step);actual.setBandlimited(true);
        auto raw=actual;raw.setBandlimited(false);
        const double modulation=-std::nextafter(step,1.0);
        const float a=actual.tick(modulation),r=raw.tick(modulation);
        require(std::isfinite(a),"rounded periodic endpoint stays within lookup table");
        if(step<=.25)require(a==r,"rounded low-band endpoint retains legacy float output");
    }
}
int main(){referenceSweep();eligibility();dynamics();std::cout<<"Rectified carrier tests PASS\n";}
