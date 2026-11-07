// SPDX-License-Identifier: Apache-2.0
// Exact-zero PCM optimization: differential native clocks, envelopes and samples.
#include <array>
#include <map>
#include <vector>
#include <optional>
#include "ma_sine_output.h"
#define private public
#include "ma_player.h"
#undef private
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>
using namespace fxchain::smaf;
void require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
MaPlayer::PcmVoice voice(const std::vector<int16_t>& source,bool modern,bool loop,double rate){
 MaPlayer::PcmVoice v;v.pcm=source.data();v.len=source.size();v.loopEnd=source.size()-1;v.loopStart=v.loopEnd/2;v.loop=loop;
 v.zeroSource=true;v.qualityInterpolation=modern;v.rate=v.baseRate=rate;v.pos=.371;v.active=true;v.keyNote=60;v.gain=.7f;v.vel=.63f;
 v.lfoInc=5.9/44100;v.vibDepth=.01548;v.amDepth=.669f;
 FmOpPatch p;p.ar=15;p.dr=5;p.sr=0;p.rr=12;p.sl=4;p.holdEnabled=true;v.env.configure(p,44100);v.env.keyOn();return v;
}
void test(){
 unsigned cases=0;uint64_t samples=0;
 for(size_t len:{size_t(2),size_t(9),size_t(81),size_t(513)})for(bool modern:{false,true})for(bool loop:{false,true})
 for(double rate:{.25,1.,1.01,1.04,1.08,1.1,2.,4.5,6.,9.,12.})for(int pattern=0;pattern<3;++pattern){
  std::vector<int16_t> source(len,0);source.back()=32767; // outside exclusive playback endpoint
  if(pattern==1)source[0]=1; // tiny attack; never eligible
  if(pattern==2)source[len-2]=-1; // single tiny loop sample; never eligible
  auto a=voice(source,modern,loop,rate);a.zeroSource=pattern==0;auto b=a;b.zeroSource=false;
  for(int n=0;n<768;++n){
   if(n==100){a.rate=b.rate=1.04;a.gain=b.gain=0;}
   if(n==160){a.rate=b.rate=9.;a.gain=b.gain=.71f;}
   if(n==230){a.env.keyOff(true);b.env.keyOff(true);}
   if(n==320){a.env.setHold(false);b.env.setHold(false);}
   if(n==500){a=voice(source,modern,loop,rate);a.zeroSource=pattern==0;b=a;b.zeroSource=false;}
   const float x=a.tick(),y=b.tick();require(std::bit_cast<uint32_t>(x)==std::bit_cast<uint32_t>(y),"PCM sample bits differ");
   require(a.active==b.active&&a.pos==b.pos&&a.loopTravel==b.loopTravel&&a.lfoPhase==b.lfoPhase&&a.recentLevel==b.recentLevel,"PCM clocks/allocation differ");
   require(a.env.phase_==b.env.phase_&&a.env.level_==b.env.level_&&a.env.pendingKeyOff_==b.env.pendingKeyOff_,"PCM envelope/Hold differs");++samples;
  }++cases;
 }
 std::cout<<cases<<" PCM conditions / "<<samples<<" exact samples and states PASS\n";
}
void bench(){
 std::vector<int16_t> source(257,0);
 for(bool silent:{false,true})for(double rate:{.25,1.04,2.,6.,12.}){
  std::array<MaPlayer::PcmVoice,32> voices;for(auto& v:voices){v=voice(source,true,true,rate);v.zeroSource=silent;}
  const auto start=std::chrono::steady_clock::now();double sum=0;
  for(int n=0;n<8192;++n)for(auto& v:voices)sum+=v.tick();
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::cout<<silent<<','<<rate<<','<<ms<<','<<sum<<'\n';
 }
}
int main(int argc,char**argv){if(argc==2&&std::string_view(argv[1])=="--bench")bench();else test();}
