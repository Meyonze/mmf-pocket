// SPDX-License-Identifier: Apache-2.0
// Single-thread source-only host measurements; no FIR/mix/I/O or Android claim.
#include "ma_fm_core.h"
#include "ma_subframe_sine.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;
int main(){(void)subframeMath::coefficients();double checksum=0;bool first=true;
 std::cout<<std::setprecision(12)<<"{\"frames\":1024,\"scope\":\"Shared host, FM source only; setup excluded; not Android performance\",\"results\":[";
 for(int repeat=0;repeat<3;++repeat)for(int algorithm:{0,5})for(int count:{8,32})for(int factor:{8,32})for(int backend:{0,1})for(int order=0;order<6;++order){
  int mode=(order+repeat)%6;if(mode==4&&algorithm==0)continue;bool fast=(backend+repeat)%2!=0;
  FmVoicePatch p;p.modernTimbre=true;p.fourOp=algorithm==5;p.algorithm=uint8_t(algorithm);p.lfo=2;
  for(int j=0;j<4;++j){auto& o=p.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(j%2?2:3);o.tl=uint8_t(j%2?0:24);o.am=o.vib=true;o.dam=o.dvb=2;
   if((mode==2&&j%2==0)||(mode==3&&j%2==1)||(mode==4&&j==1))o.ar=0;}
  std::vector<FmVoice> voices(static_cast<size_t>(count));for(int i=0;i<count;++i){auto& v=voices[size_t(i)];v.setSampleRate(44100);v.noteOn(p,220+9*i,.6f);if(mode==1)v.setVolume(0);if(mode==5)v.setVolume(1e-12f);}
  const auto start=std::chrono::steady_clock::now();for(int n=0;n<1024;++n)for(auto& v:voices){float out[32];if(!v.tickSineSubframes(out,factor,fast?FmSubframeMath::Hermite:FmSubframeMath::Exact))return 1;for(int k=0;k<factor;++k)checksum+=out[k];}
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(!first)std::cout<<',';first=false;std::cout<<"{\"repeat\":"<<repeat<<",\"algorithm\":"<<algorithm<<",\"voices\":"<<count<<",\"factor\":"<<factor<<",\"math\":\""<<(fast?"hermite":"exact")<<"\",\"mode\":"<<mode<<",\"ms\":"<<ms<<'}';
 }
 std::cout<<"],\"checksum\":"<<checksum<<"}\n";
}
