// SPDX-License-Identifier: Apache-2.0
// Exact-zero/revival cases and optional differential transport against an older core.
#include "fm-native-clock-probe.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <limits>
using namespace fxchain::smaf;
void require(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
FILE* trace=nullptr;
template<class T>void record(T value){if(trace)require(std::fwrite(&value,sizeof(value),1,trace)==1,"trace write");}
void state(const FmVoice& a,const FmVoice& b){
 require(a.active_==b.active_&&a.released_==b.released_&&a.keyOffRequested_==b.keyOffRequested_&&a.recentLevel_==b.recentLevel_&&a.lfoPhase_==b.lfoPhase_&&a.lfoSin_==b.lfoSin_,"native voice clocks changed");
 record(a.active_);record(a.released_);record(a.keyOffRequested_);record(a.recentLevel_);record(a.lfoPhase_);record(a.lfoSin_);
 for(int j=0;j<4;++j){const auto& x=a.ops_[j];const auto& y=b.ops_[j];
  require(x.phase_==y.phase_&&x.phaseInc_==y.phaseInc_&&x.env_.level_==y.env_.level_&&x.env_.phase_==y.env_.phase_&&x.env_.pendingKeyOff_==y.env_.pendingKeyOff_,"native operator/EG/Hold clocks changed");
  record(x.phase_);record(x.phaseInc_);record(x.env_.level_);record(int(x.env_.phase_));record(x.env_.pendingKeyOff_);
  for(int k=0;k<2;++k){require(a.fbMem_[j][k]==b.fbMem_[j][k],"native history changed");record(a.fbMem_[j][k]);}
 }
}
int main(int argc,char** argv){
 if(argc==2){trace=std::fopen(argv[1],"wbx");require(trace!=nullptr,"trace output must be new");}else if(argc!=1)return 2;
 size_t cases=0,samples=0;bool lowBitAudible=false,tinyAudible=false;
 for(int rate:{8000,44100})for(bool pairs:{false,true})for(int profile:{0,1,2})
 for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})for(int factor:{2,4,8,16,32})for(int pattern=0;pattern<8;++pattern){
  FmVoicePatch patch;patch.fourOp=pairs;patch.algorithm=pairs?5:0;patch.modernTimbre=profile!=0;patch.lfo=2;
  for(int j=0;j<4;++j){auto& o=patch.ops[size_t(j)];o.multi=uint8_t(j%2?2:3);o.tl=uint8_t(j%2?0:24);o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;
   o.am=o.vib=true;o.dam=o.dvb=2;o.ma35Multipliers=profile==2;o.holdEnabled=true;
   if((pattern==1&&j%2==0)||(pattern==2&&j%2==1)||(pattern==3&&j==1))o.ar=0;
   if(pattern==4&&j%2==1){o.ar=0;o.rateLowBits=8;}
  }
  FmVoice v,oracle;v.setSampleRate(rate);oracle.setSampleRate(rate);
  const float velocity=pattern==5?0:.73f;v.noteOn(patch,317,velocity);oracle.noteOn(patch,317,velocity);
  std::array<float,32> output{};
  for(int n=0;n<192;++n){auto controls=[&](FmVoice& x){
    if(n==0&&pattern==0)x.setVolume(0);if(n==0&&pattern==6)x.setVolume(1e-12f);if(n==0&&pattern==7)x.setVolume(std::numeric_limits<float>::min());
    if(n==24)x.setVibratoScale(8);if(n==32)x.setPitch(rate*.71);if(n==48)x.setPitch(rate*.013);
    if(n==64)x.setVolume(0);if(n==80)x.setVolume(.63f);if(n==96)x.noteOff(true);if(n==112)x.setHold(false);
    if(n==128){auto audible=patch;for(auto& o:audible.ops){o.ar=15;o.rateLowBits=0;}x.noteOn(audible,419,.51f);}
   };controls(v);controls(oracle);
   require(v.tickSineSubframes(output.data(),factor,math),"valid zero/revival source rejected");const float native=oracle.tick();
   require(std::bit_cast<uint32_t>(output[size_t(factor-1)])==std::bit_cast<uint32_t>(native),"native endpoint bits changed");
   for(int k=0;k<factor;++k){const auto value=output[size_t(k)];require(std::isfinite(value),"nonfinite interior");
    // Signed zero carries no signal; normalize only its sign for transport.
    record(value==0?0.0f:value);++samples;
    if(pattern==4&&n<24)lowBitAudible|=value!=0;if((pattern==6||pattern==7)&&n<24)tinyAudible|=value!=0;
    if((pattern==0&&n<64)||(pattern==2&&n<128)||(pattern==5&&n<128)||(n>=64&&n<80))require(value==0,"mathematically zero source became audible");
   }
   record(std::bit_cast<uint32_t>(native));state(v,oracle);
  }
  ++cases;
 }
 require(lowBitAudible&&tinyAudible,"low-bit attack or tiny nonzero source was lost");
 if(trace)require(std::fclose(trace)==0,"trace close");
 std::cout<<cases<<" exact-zero/revival source cases; "<<samples<<" samples; native clocks/endpoints and tiny inputs PASS\n";
}
