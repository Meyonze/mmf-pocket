// SPDX-License-Identifier: Apache-2.0
// Independent numerical Fourier references for remaining fixed-wave carriers.
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
struct Fourier {double dc=0,sine=0,cosine=0;};
const Fourier& coeff(int wave) {
    static const std::array<Fourier,32> table=[] {
        std::array<Fourier,32> values{};constexpr int count=262144;
        for(int w:{7,14,17,18,19,20,21,22,25,26,27,28,29,30}) {
            FmOpPatch p;p.wave=uint8_t(w);
            FmOperator reference;reference.configure(p,44100,0,true);reference.noteOn(0);
            reference.setBandlimited(false);
            for(int i=0;i<count;++i) {
                const double phase=(i+.5)/count;
                // Public raw output includes modern WS30's quarter pulse.
                const double x=reference.tick(phase);
                values[w].dc+=x/count;
                values[w].sine+=2*x*std::sin(tau*phase)/count;
                values[w].cosine+=2*x*std::cos(tau*phase)/count;
            }
        }
        return values;
    }();return table[wave];
}
double start(int wave){return wave==14?1.0/6:0.25;}
double finish(int wave){return wave==14?1.0/5:1.0/3;}
double ideal(int wave,double phase){const auto& c=coeff(wave);return c.dc+c.sine*std::sin(tau*phase)+c.cosine*std::cos(tau*phase);}
void referenceSweep() {
    constexpr int frames=2048;unsigned cases=0;double maxError=0;
    for(int wave:{7,14,17,18,19,20,21,22,25,26,27,28,29,30})for(double rate:{8000.0,44100.0})
    for(int bin:{1,17,127,341,342,400,410,512,513,600,683,769,1001})
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
            require(std::isfinite(a),"remaining fixed-wave carrier remains finite");
            if(step<=start(wave))require(a==r,"remaining fixed-wave carrier preserves low/mid-band float output exactly");
            else {
                // Independent midpoint quadrature supplies DC and the fundamental.
                const double target=ideal(wave,phase);
                oldError+=(r-target)*(r-target);newError+=(a-target)*(a-target);
                mean+=a;sine+=a*std::sin(tau*phase);cosine+=a*std::cos(tau*phase);
            }
        }
        if(step>start(wave)) {
            require(newError<=oldError+1e-10,"high-band reconstruction error never increases");
            if(step>=finish(wave)) {
                maxError=std::max(maxError,std::sqrt(newError/frames));
                require(std::sqrt(newError/frames)<5e-7,"full-band result matches independent DC/fundamental reference");
                require(std::abs(mean/frames-coeff(wave).dc)<5e-7,"analytic DC is retained");
                require(std::abs(2*sine/frames-coeff(wave).sine)<5e-7&&std::abs(2*cosine/frames-coeff(wave).cosine)<5e-7,
                    "in-band fundamental amplitude and phase are retained");
            }
        }
        ++cases;
    }
    std::cout<<"Remaining-carrier sweep PASS: "<<cases<<" cases, full-band maximum RMS error="<<maxError<<'\n';
}
unsigned enabled(const FmVoice& v){unsigned mask=0;for(int i=0;i<4;++i)if(v.ops_[i].blepEnabled_)mask|=1u<<i;return mask;}
void eligibility() {
    constexpr unsigned masks[]={2,3,15,8,8,10,9,13};
    for(int wave:{7,14,17,18,19,20,21,22,25,26,27,28,29,30})for(int algorithm=0;algorithm<8;++algorithm) {
        FmVoicePatch p;p.modernTimbre=true;p.algorithm=uint8_t(algorithm);p.fourOp=algorithm>=2;
        for(int i=0;i<4;++i){p.ops[i].wave=uint8_t(wave);p.ops[i].ar=masks[algorithm]&(1u<<i)?15:0;}
        FmVoice v;v.setSampleRate(44100);v.noteOn(p,17000,1);
        require(enabled(v)==masks[algorithm],"only unmodulated final carriers receive remaining-carrier correction");
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
    for(int wave:{7,14,17,18,19,20,21,22,25,26,27,28,29,30})for(double base:{0.165,0.195,0.24,0.30,0.49}) {
        FmOpPatch p;p.wave=uint8_t(wave);p.vib=true;p.dvb=3;
        FmOperator actual;actual.configure(p,44100,0,true);actual.noteOn(44100*base);actual.setBandlimited(true);
        auto raw=actual;raw.setBandlimited(false);double phase=0;
        for(int i=0;i<4096;++i) {
            const double lfo=std::sin(tau*(i+1)/1024),step=base*(1+0.01548*lfo*8);
            phase+=step;if(phase>=1)phase-=std::floor(phase);
            const float a=actual.tick(0,lfo,8),r=raw.tick(0,lfo,8);
            require(std::isfinite(a)&&std::abs(a)<=std::max(1.0,std::abs(coeff(wave).dc)+std::hypot(coeff(wave).sine,coeff(wave).cosine))+1e-6,"vibrato stays finite through all correction boundaries");
            if(step>=0.5)require(a==0,"instantaneous Nyquist suppression stays exact");
            else if(step<=start(wave))require(a==r,"vibrato returning below correction restores raw rounding");
            else if(step>=finish(wave)) {
                const double target=ideal(wave,phase);
                require(std::abs(a-target)<5e-7,"vibrato uses instantaneous correction amount and integrated phase");
            }
        }
    }
    for(int wave:{7,14,17,18,19,20,21,22,25,26,27,28,29,30})for(double step:{start(wave),0.4}) {
        FmOpPatch p;p.wave=uint8_t(wave);FmOperator actual;
        actual.configure(p,44100,0,true);actual.noteOn(44100*step);actual.setBandlimited(true);
        auto raw=actual;raw.setBandlimited(false);
        const double modulation=-std::nextafter(step,1.0);
        const float a=actual.tick(modulation),r=raw.tick(modulation);
        require(std::isfinite(a),"rounded periodic endpoint stays within lookup table");
        if(step<=start(wave))require(a==r,"rounded low-band endpoint retains legacy float output");
    }
}
int main(){referenceSweep();eligibility();dynamics();std::cout<<"Remaining fixed-wave carrier tests PASS\n";}
