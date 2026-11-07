// SPDX-License-Identifier: Apache-2.0
// Black-box public source API tests: no private access or host-only DSP helper.
#include "ma_fm_core.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace fxchain::smaf;
void require(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
FmVoicePatch patch(bool pairs,int profile){FmVoicePatch p;p.fourOp=pairs;p.algorithm=pairs?5:0;p.modernTimbre=profile!=0;p.lfo=2;
    const int multis[]={3,2,7,5};for(int j=0;j<4;++j){auto&o=p.ops[j];o.wave=0;o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(multis[j]);o.tl=uint8_t(j%2?0:24);
        o.am=o.vib=true;o.dam=o.dvb=2;o.holdEnabled=true;o.ma35Multipliers=profile==2;}
    return p;}
void publicState(const FmVoice&a,const FmVoice&b){require(a.active()==b.active()&&a.released()==b.released()&&a.recentLevel()==b.recentLevel()&&a.channel==b.channel&&a.note()==b.note(),"public voice state changed");}
void clocks(){size_t cases=0;
    for(int rate:{8000,44100})for(bool pairs:{false,true})for(int profile:{0,1,2})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})for(int factor:{2,4,8,16,32}){
        auto p=patch(pairs,profile);FmVoice v,oracle;v.setSampleRate(rate);oracle.setSampleRate(rate);v.noteOn(p,317,.73f);oracle.noteOn(p,317,.73f);std::array<float,32> out;
        for(int n=0;n<320;++n){auto controls=[&](FmVoice&x){if(n==32)x.setVibratoScale(8);if(n==64)x.setPitch(rate*.71);if(n==96)x.setPitch(rate*.013);
                if(n==128)x.setVolume(0);if(n==160)x.setVolume(.63f);if(n==192)x.noteOff(true);if(n==224)x.setHold(false);if(n==240)x.noteOn(p,419,.51f);};
            controls(v);controls(oracle);out.fill(123);require(v.supportsSineSubframes()&&v.tickSineSubframes(out.data(),factor,math),"supported source rejected");
            require(out[size_t(factor-1)]==oracle.tick(),"public subframe API changed native endpoint");publicState(v,oracle);
            for(int i=0;i<factor;++i)require(std::isfinite(out[size_t(i)]),"nonfinite public subframe");for(int i=factor;i<32;++i)require(out[size_t(i)]==123,"public source wrote beyond requested count");}
        ++cases;
    }
    // Sampling density is caller-owned; changing it must not advance time twice.
    auto p=patch(true,2);FmVoice v,oracle;v.setSampleRate(44100);oracle.setSampleRate(44100);v.noteOn(p,317,1);oracle.noteOn(p,317,1);std::array<float,32> out{};
    for(int n=0;n<320;++n){int factor=n%2?32:2;require(v.tickSineSubframes(out.data(),factor,n%3?FmSubframeMath::Hermite:FmSubframeMath::Exact),"changing source density rejected");require(out[size_t(factor-1)]==oracle.tick(),"density/math switch changed native clock");publicState(v,oracle);}
    std::cout<<cases<<" public clock cases and sampling-density switches PASS\n";
}
void guards(){for(int kind=0;kind<13;++kind){auto p=patch(kind%2,2);if(kind==0)p.algorithm=1;if(kind==1)p.algorithm=4;if(kind==2)p.ops[0].wave=6;if(kind==3)p.ops[3].fb=2;if(kind==4)p.feedback=3;
        FmVoice v,oracle;v.setSampleRate(44100);oracle.setSampleRate(44100);v.noteOn(p,317,1);oracle.noteOn(p,317,1);std::array<float,64> out;out.fill(123);int factor=8;auto math=FmSubframeMath::Exact;float* target=out.data();
        if(kind==5)factor=-1;if(kind==6)factor=0;if(kind==7)factor=1;if(kind==8)factor=3;if(kind==9)factor=64;if(kind==10)factor=INT_MAX;if(kind==11)math=static_cast<FmSubframeMath>(99);if(kind==12)target=nullptr;
        require(!v.tickSineSubframes(target,factor,math),"bad public source request accepted");require(std::all_of(out.begin(),out.end(),[](float x){return x==123;}),"rejected public source request changed output");
        for(int n=0;n<200;++n){require(v.tick()==oracle.tick(),"rejected source request advanced clock");publicState(v,oracle);}}
    auto p=patch(false,1);p.ops[3].wave=31;p.ops[3].fb=7;FmVoice v;v.setSampleRate(44100);v.noteOn(p,317,1);std::array<float,8> out{};
    require(v.supportsSineSubframes()&&v.tickSineSubframes(out.data(),8),"unused operator metadata blocked valid2-op source");
    FmVoice idle;out.fill(123);require(idle.tickSineSubframes(out.data(),8,FmSubframeMath::Hermite)&&std::all_of(out.begin(),out.end(),[](float x){return x==0;}),"inactive supported source must emit zero");
    std::cout<<"Public rejection atomicity, unused operators and idle source PASS\n";
}
int main(){clocks();guards();}
