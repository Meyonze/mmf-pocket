// SPDX-License-Identifier: Apache-2.0
// Synthetic operator clocks and complete-voice differential sample transport.
#include <cstdint>
#include <array>
#include <vector>
#define private public
#include "ma_fm_core.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
using namespace fxchain::smaf;
void require(bool ok,const char* m){if(!ok){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);}}
void selfTest() {
    unsigned count=0;
    for(int rate:{8000,44100})for(bool modern:{false,true})for(int wave=0;wave<32;++wave)
    for(bool lfo:{false,true})for(int offset:{0,15})for(bool odd:{false,true}) {
        FmOpPatch p;p.wave=uint8_t(wave);p.ar=0;p.rateLowBits=odd?8:0;p.dr=p.sr=p.sl=0;
        p.rr=15;p.tl=12;p.multi=1;p.vib=p.am=lfo;p.dvb=p.dam=3;p.holdEnabled=true;
        FmOperator silent,clock;silent.configure(p,rate,offset,modern);
        auto loud=p;loud.ar=15;loud.rateLowBits=0;clock.configure(loud,rate,offset,modern);
        silent.noteOn(rate*.061);clock.noteOn(rate*.061);
        bool audible=false;
        for(int n=0;n<512;++n) {
            if(n==128||n==256){double f=rate*(n==128?.71:.071);silent.setBaseFreq(f);clock.setBaseFreq(f);}
            if(n==320){silent.noteOff(true);clock.noteOff(true);}
            if(n==384){silent.setHold(false);clock.setHold(false);}
            double l=std::sin(n*.073),m=.42*std::sin(n*.031);double scale=n<64?0:n<192?8:1;
            float a=silent.tick(m,l,scale);(void)clock.tick(m,l,scale);
            require(silent.phase_==clock.phase_,"zero output preserves oscillator time through bend and vibrato");
            require(std::isfinite(a),"finite operator output");
            if(!odd) require(a==0,"stopped attack remains exactly silent");
            else audible=audible||a!=0;
        }
        if(odd&&wave!=15&&wave!=23&&wave!=31)require(audible,"AR1 retains real nonzero output");
        // Reusing a stopped operator must not leave the cached guard enabled.
        silent.configure(loud,rate,offset,modern);silent.noteOn(317);
        FmOperator fresh;fresh.configure(loud,rate,offset,modern);fresh.noteOn(317);
        for(int n=0;n<128;++n)require(silent.tick(.1)==fresh.tick(.1),"retrigger restores the audible patch exactly");
        ++count;
    }
    std::cout<<"FM stopped-attack clock/rate/retrigger cases: "<<count<<" PASS\n";
}
void samples(const char* path) {
    FILE* f=std::fopen(path,"wbx");require(f!=nullptr,"sample output must be new");size_t count=0;
    for(bool modern:{false,true})for(int algorithm=0;algorithm<8;++algorithm)
    for(int wave=0;wave<32;++wave)for(int pattern=0;pattern<3;++pattern)for(bool lfo:{false,true}) {
        FmVoicePatch p;p.modernTimbre=modern;p.algorithm=uint8_t(algorithm);p.fourOp=algorithm>=2;p.lfo=2;
        static constexpr unsigned carriers[8]={2,3,15,8,8,10,9,13};
        unsigned keep=carriers[algorithm]&(~carriers[algorithm]+1);
        unsigned zero=pattern==0?0:pattern==1?(15&~carriers[algorithm]):(15&~keep);
        for(int j=0;j<4;++j){auto&o=p.ops[j];o.wave=uint8_t(wave);o.multi=uint8_t(1+j%3);o.tl=uint8_t(12+j*2);
            o.ar=(zero&(1u<<j))?0:15;o.dr=o.sr=o.sl=0;o.rr=6;o.egType=true;
            o.ma35Multipliers=modern;o.fb=uint8_t(j==0?(wave&7):0);o.am=o.vib=lfo;o.dvb=o.dam=3;o.holdEnabled=true;}
        FmVoice v;v.setSampleRate(44100);v.noteOn(p,170+wave*3.1,.63f);
        for(int n=0;n<1024;++n){
            if(n==64)v.setVibratoScale(8);if(n==128)v.setPitch(32000);if(n==192)v.setPitch(713.7);
            if(n==256)v.setVolume(0);if(n==320)v.setVolume(.73f);if(n==384)v.noteOff(true);
            if(n==448)v.setHold(false);if(n==512)v.setPitch(404.4);if(n==768)v.noteOn(p,317,.63f);
            float s=v.tick();require(std::isfinite(s),"finite differential sample");require(std::fwrite(&s,sizeof(s),1,f)==1,"write sample");++count;
        }
    }
    require(std::fclose(f)==0,"close sample output");std::cout<<"Differential FM samples: "<<count<<'\n';
}
int main(int argc,char**argv){if(argc==1){selfTest();return 0;}
 if(argc==3&&std::string(argv[1])=="--samples"){samples(argv[2]);return 0;}return 2;}
