// SPDX-License-Identifier: Apache-2.0
// Original renderer tests. Fourier references are not handset calibration.
#include <array>
#include <cstdint>
#include <vector>
#define private public
#include "ma_fm_core.h"
#undef private
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
using namespace fxchain::smaf;
namespace {
constexpr double pi=std::numbers::pi, tau=2*pi;
void require(bool ok,const char* message) {
    if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
double fourier(unsigned wave,double phase,unsigned bin,unsigned frames) {
    double sum=0;
    for(unsigned harmonic=1;harmonic*bin<frames/2;++harmonic) {
        if(wave==6 && harmonic%2==0)continue;
        const double coefficient=wave==6?4/(pi*harmonic)
            :(harmonic%2?2:-2)/(pi*harmonic);
        sum+=coefficient*std::sin(tau*harmonic*phase);
    }
    return sum;
}
void stationary() {
    constexpr unsigned frames=2048;
    for(double rate:{8000.0,44100.0})for(unsigned wave:{6u,24u})
    for(unsigned bin:{17u,127u,401u}) {
        FmOpPatch patch;patch.wave=uint8_t(wave);
        FmOperator plain,filtered;
        plain.configure(patch,rate,0,true);filtered.configure(patch,rate,0,true);
        plain.noteOn(rate*bin/frames);filtered.noteOn(rate*bin/frames);
        filtered.setBandlimited(true);
        for(unsigned n=0;n<4096;++n){(void)plain.tick(0);(void)filtered.tick(0);}
        double before=0,after=0,sine=0,cosine=0,dc=0;
        for(unsigned n=0;n<frames;++n) {
            const double phase=double(bin)*(n+1)/frames;
            const double reference=fourier(wave,phase,bin,frames);
            const double a=plain.tick(0),b=filtered.tick(0);
            require(std::isfinite(b)&&std::abs(b)<1.5,"bandlimited carrier stays finite and bounded");
            before+=(a-reference)*(a-reference);after+=(b-reference)*(b-reference);
            sine+=b*std::sin(tau*phase);cosine+=b*std::cos(tau*phase);dc+=b;
        }
        require(after<before*0.25,"stationary error to independent Fourier series drops by at least 75 percent");
        const double amplitude=2*std::hypot(sine,cosine)/frames;
        const double target=(wave==6?4:2)/pi;
        require(std::abs(amplitude/target-1)<0.001,"fundamental amplitude preserved within 0.1 percent");
        require(std::abs(dc/frames)<1e-6,"symmetric filtered carrier has no appreciable DC");
    }
}
FmVoicePatch patch(unsigned algorithm,unsigned wave) {
    FmVoicePatch p;p.modernTimbre=true;p.algorithm=uint8_t(algorithm);p.fourOp=algorithm>=2;
    for(auto& op:p.ops){op.wave=uint8_t(wave);op.ma35Multipliers=true;op.ar=0;}
    return p;
}
unsigned enabled(const FmVoice& voice) {
    unsigned mask=0;
    for(unsigned i=0;i<4;++i)if(voice.ops_[i].blepEnabled_)mask|=1u<<i;
    return mask;
}
void policy() {
    // Independent documented direct-output lists for all eight algorithms.
    constexpr unsigned carriers[8]={2,3,15,8,8,10,9,13};
    FmVoice voice;voice.setSampleRate(44100);
    for(unsigned wave:{6u,24u})for(unsigned algorithm=0;algorithm<8;++algorithm) {
        auto p=patch(algorithm,wave);
        // Only the final outputs sound; every upstream modulator is AR0.
        for(unsigned i=0;i<4;++i)if(carriers[algorithm]&(1u<<i))p.ops[i].ar=15;
        voice.noteOn(p,440,1);
        require(enabled(voice)==carriers[algorithm],"silent upstream operators permit only final carriers");
        p.modernTimbre=false;voice.noteOn(p,440,1);
        require(enabled(voice)==0,"legacy profile does not enable bandlimiting");
    }
    auto serial=patch(0,6);serial.ops[0].ar=serial.ops[1].ar=15;
    voice.noteOn(serial,440,1);require(enabled(voice)==0,"active serial modulator excludes both modulator and modulated carrier");
    // MA7 AR0 with its low bit set is a real attack, not a silent source.
    serial.ops[0].ar=0;serial.ops[0].rateLowBits=8;
    voice.noteOn(serial,440,1);require(enabled(voice)==0,"MA7 odd attack is not classified as silent");
    serial.ops[0].rateLowBits=0;
    voice.noteOn(serial,440,1);require(enabled(voice)==2,"stopped attack safely permits the final carrier");
    for(bool header:{false,true}) {
        auto additive=patch(1,24);additive.ops[0].ar=additive.ops[1].ar=15;
        if(header)additive.feedback=1;else additive.ops[0].fb=1;
        voice.noteOn(additive,440,1);
        require(enabled(voice)==2,"header and operator feedback exclude the feedback output only");
    }
    auto sum=patch(3,6);sum.ops[0].ar=sum.ops[3].ar=15;
    voice.noteOn(sum,440,1);require(enabled(voice)==0,"either live input to summed PM excludes its carrier");
    sum.ops[0].ar=0;
    voice.noteOn(sum,440,1);require(enabled(voice)==8,"both silent inputs permit a summed-PM carrier");
    auto chain=patch(4,24);chain.ops[0].ar=chain.ops[1].ar=chain.ops[3].ar=15;
    voice.noteOn(chain,440,1);require(enabled(voice)==8,"a stopped intervening operator isolates upstream modulation");
    // Retained operators must lose their former eligibility on an incomplete
    // public-C++ patch; no stale flag may turn a retained modulator into BLEP.
    auto all=patch(2,6);for(auto& op:all.ops)op.ar=15;
    voice.noteOn(all,440,1);require(enabled(voice)==15,"retained-operator fixture starts with four direct outputs");
    all.fourOp=false;all.algorithm=4;
    voice.noteOn(all,440,1);require(enabled(voice)==0,"incomplete operator configuration disables every retained quality flag");
}
void unchangedModulation() {
    // A second voice with all quality flags explicitly disabled is an exact
    // oracle for paths that must retain the original nonlinear FM synthesis.
    for(unsigned algorithm=0;algorithm<8;++algorithm)for(unsigned wave:{0u,6u,24u})
    for(bool feedback:{false,true}) {
        auto p=patch(algorithm,wave);
        for(auto& op:p.ops){op.ar=15;op.tl=24;op.am=op.vib=true;op.dam=op.dvb=3;}
        if(feedback)for(auto& op:p.ops)op.fb=3;
        FmVoice actual,oracle;actual.setSampleRate(8000);oracle.setSampleRate(8000);
        actual.noteOn(p,440,0.75f);oracle.noteOn(p,440,0.75f);
        for(auto& op:oracle.ops_)op.setBandlimited(false);
        // Compare only cases with no eligible direct output. Additive outputs
        // deliberately change, and are covered by the independent Fourier test.
        if(enabled(actual)!=0)continue;
        for(unsigned n=0;n<1024;++n) {
            if(n==257){actual.setPitch(880);oracle.setPitch(880);}
            if(n==513){actual.noteOff();oracle.noteOff();}
            require(actual.tick()==oracle.tick(),"excluded nonlinear FM/feedback path is bit-exact");
        }
    }
}
void boundaries() {
    for(unsigned wave:{6u,24u}) {
        FmOpPatch p;p.wave=uint8_t(wave);p.vib=true;p.dvb=3;
        FmOperator op;op.configure(p,44100,0,true);op.noteOn(44100*0.49);op.setBandlimited(true);
        for(unsigned n=0;n<2048;++n) {
            const double sample=op.tick(0,std::sin(tau*n/1024),8);
            require(std::isfinite(sample)&&std::abs(sample)<1.5,"vibrato crossing instantaneous Nyquist remains finite and bounded");
        }
        for(double frequency:{0.0,std::numeric_limits<double>::denorm_min(),1e-300,440.0,22050.0,30000.0}) {
            op.setBaseFreq(frequency);
            for(unsigned n=0;n<16;++n)require(std::isfinite(op.tick(0)),"zero/subnormal/pitch changes avoid reciprocal overflow");
        }
        op.noteOn(440);op.setBandlimited(true);
        for(unsigned i=1;i<8192;++i) {
            const double step=(double(i)+0.5)/16384;
            op.setBaseFreq(44100*step);
            const double angle=pi*step;
            const double exact=std::pow(angle/std::sin(angle),2);
            require(std::abs(op.blepGain_/exact-1)<3e-7,"interpolated gain agrees with independent exact sinc gain");
        }
    }
}
void retrigger() {
    for(double rate:{8000.0,44100.0})for(unsigned wave:{6u,24u}) {
        FmVoice reused;reused.setSampleRate(rate);
        for(unsigned stage=0;stage<4;++stage) {
            auto p=patch(stage==0?2:stage==1?0:stage==2?7:1,wave);
            for(auto& op:p.ops){op.ar=15;op.am=op.vib=true;op.dam=op.dvb=3;}
            if(stage==2)p.ops[1].ar=0;
            if(stage==3)p.ops[0].fb=3;
            const double pitch=220*(stage+1);
            reused.noteOn(p,pitch,0.75f);
            FmVoice fresh;fresh.setSampleRate(rate);fresh.noteOn(p,pitch,0.75f);
            for(unsigned frame=0;frame<1024;++frame) {
                if(frame==257){reused.setPitch(pitch*1.5);fresh.setPitch(pitch*1.5);}
                if(frame==513){reused.noteOff();fresh.noteOff();}
                require(reused.tick()==fresh.tick(),"retrigger refreshes cached gains and eligibility like a fresh voice");
            }
        }
    }
}
}
int main() {
    stationary();policy();unchangedModulation();boundaries();retrigger();
    std::cout<<"PASS: bandlimited carrier Fourier, scope, nonlinear isolation and boundary tests\n";
}
