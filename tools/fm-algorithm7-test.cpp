// SPDX-License-Identifier: Apache-2.0
// Algorithm7 clocks plus public-player PM and two direct-carrier superposition.
#include "fm-native-clock-probe.h"
#define FM_PLAYER_SIDEBAND_AUDIT_NO_MAIN
#include "fm-player-sideband-audit.cpp"
#include <bit>
using namespace fxchain::smaf;
namespace algorithm7 {
using namespace player_sideband;
SmafFile fixture(const Case& c){
 auto f=player_sideband::fixture(c);auto& t=f.tracks[0];
 std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,7};
 const int multiples[]={1,c.modulator,c.carrier,2},levels[]={18,c.tl,0,24};
 for(int j=0;j<4;++j)v.insert(v.end(),{0,0xa0,0xf0,uint8_t(levels[j]<<2),0,uint8_t(multiples[j]<<4),0});
 t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);return f;
}
std::vector<double> render7(const Case& c,int factor,FmSubframeMath math){
 MaPlayer p;require(p.init(algorithm7::fixture(c),rate,false,{factor,math}),"algorithm7 fixture init");
 const int latency=factor?72:0;std::vector<float> all(size_t(warmup+latency+frames)*2);
 require(p.render(all.data(),warmup+latency+frames)==warmup+latency+frames,"algorithm7 window ended early");
 require(p.diagnostics().fmNotes==1&&p.diagnostics().fallbackNotes==0&&p.diagnostics().limitedFrames==0,"algorithm7 fixture routing/limiter");
 std::vector<double> out(frames);for(int n=0;n<frames;++n){out[size_t(n)]=all[size_t(n+warmup+latency)*2];require(all[size_t(n+warmup+latency)*2+1]==0,"algorithm7 hard pan");}return out;
}
std::vector<double> reference7(const Case& c,int factor,bool ideal){
 auto out=Model(c).reference(factor,ideal);for(auto& x:out)x*=.4;
 const double a=float(1-std::exp(-2*3.14159265358979*10000/rate));
 const auto config=factor>1?fm_causal::alignedConfig(factor):fm_causal::Config{1,{}};
 const double delay=factor>1?fm_causal::Decimator(config).delayFrames():0;
 for(auto [multiple,level]:std::array<std::pair<int,int>,2>{{{1,18},{2,24}}}){
  const double f=440*std::pow(2.,double(c.note-69)/12)*multiple/rate,omega=tau*f;
  require(f<.5,"direct sine outside native band");
  const auto lp=a/(Complex(1)-std::polar(1-a,-omega));Complex h=lp*lp;
  if(!ideal&&factor>1)h*=fm_causal::response(config,omega/factor)*std::polar(1.,omega*delay);
  const double gain=.4*.7*double(.32f)*2*std::pow(10.,-.75*level/20);
  auto phase=std::polar(1.,omega*(warmup+1));const auto step=std::polar(1.,omega);
  for(auto& value:out){value+=gain*std::imag(h*phase);phase*=step;}
 }return out;
}
void spectral(){size_t cases=0;double maxError=0;
 for(int note:{60,72,84,96})for(auto [carrier,modulator]:std::array<std::pair<int,int>,2>{{{2,3},{5,7}}})for(int tl:{0,24,36}){
  const Case c{note,carrier,modulator,tl};const auto ideal=reference7(c,0,true),native=render7(c,0,FmSubframeMath::Exact);
  for(int factor:{0,1,2,4,8,16,32}){std::vector<double> exact;
   for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite}){
    if(factor<2&&math==FmSubframeMath::Hermite)continue;
    const auto actual=render7(c,factor,math);if(math==FmSubframeMath::Exact)exact=actual;
    const double e=error(actual,reference7(c,factor,false));maxError=std::max(maxError,e);
    require(e<4e-7&&error(actual,exact)<1e-7,"algorithm7 disagrees with independent PM plus two sine/FIR/LP reference");
    if(factor==1)require(actual==native,"algorithm7 native alignment control");
    if(note==84&&carrier==5&&tl==0&&factor==8&&math==FmSubframeMath::Hermite){
     const double old=error(native,ideal),now=error(actual,ideal);require(now<old*.001,"algorithm7 alias error did not decrease");
     std::cout<<"note84 C5/M7 TL0 ideal error native="<<old<<" filtered8="<<now<<'\n';
    }++cases;
   }
  }
 }
 std::cout<<cases<<" public-player spectral policies PASS; max independent model RMS "<<maxError<<'\n';
}
void clocks(){size_t cases=0;
 for(int sampleRate:{8000,44100})for(int profile:{0,1,2})for(int factor:{2,4,8,16,32})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})for(int pattern=0;pattern<4;++pattern){
  FmVoicePatch p;p.fourOp=true;p.algorithm=7;p.modernTimbre=profile!=0;p.lfo=2;
  for(int j=0;j<4;++j){auto& o=p.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(j+1);o.tl=uint8_t(j*6);o.am=o.vib=true;o.dam=o.dvb=2;o.holdEnabled=true;o.ma35Multipliers=profile==2;
   if(pattern==1&&j!=1)o.ar=0;if(pattern==2&&j!=1){o.ar=0;o.rateLowBits=8;}}
  FmVoice v,original;v.setSampleRate(sampleRate);original.setSampleRate(sampleRate);v.noteOn(p,317,.63f);original.noteOn(p,317,.63f);std::array<float,32> output{};
  for(int n=0;n<256;++n){auto control=[&](FmVoice& x){if(n==0&&pattern==3)x.setVolume(1e-12f);if(n==32)x.setPitch(sampleRate*.71);if(n==64)x.setPitch(sampleRate*.013);if(n==96)x.setVolume(0);if(n==128)x.setVolume(.71f);if(n==160)x.noteOff(true);if(n==192)x.setHold(false);if(n==224)x.noteOn(p,419,.51f);};control(v);control(original);
   require(v.tickSineSubframes(output.data(),factor,math),"algorithm7 source rejected");const float endpoint=original.tick();require(std::bit_cast<uint32_t>(output[size_t(factor-1)])==std::bit_cast<uint32_t>(endpoint),"algorithm7 endpoint changed");
   require(v.active_==original.active_&&v.released_==original.released_&&v.recentLevel_==original.recentLevel_&&v.lfoPhase_==original.lfoPhase_&&v.lfoSin_==original.lfoSin_,"algorithm7 voice clock changed");
   for(int j=0;j<4;++j){const auto& a=v.ops_[j];const auto& b=original.ops_[j];require(a.phase_==b.phase_&&a.env_.level_==b.env_.level_&&a.env_.phase_==b.env_.phase_&&a.env_.pendingKeyOff_==b.env_.pendingKeyOff_,"algorithm7 operator clock changed");}
   for(int j=0;j<factor;++j){require(std::isfinite(output[size_t(j)]),"algorithm7 nonfinite output");if(pattern==1&&n<224)require(output[size_t(j)]==0,"algorithm7 silent carriers became audible");}
  }++cases;
 }
 for(int j=0;j<4;++j)for(bool feedback:{false,true}){FmVoicePatch p;p.fourOp=true;p.algorithm=7;if(feedback)p.ops[size_t(j)].fb=3;else p.ops[size_t(j)].wave=6;FmVoice v;v.setSampleRate(rate);v.noteOn(p,317,1);auto ref=v;std::array<float,8> out;out.fill(123);
  require(!v.supportsSineSubframes()&&!v.tickSineSubframes(out.data(),8),"algorithm7 unsafe shape admitted");for(float x:out)require(x==123,"algorithm7 rejection touched output");require(v.tick()==ref.tick(),"algorithm7 rejection advanced clock");}
 std::cout<<cases<<" algorithm7 complete clock/Hold/tiny/gain cases and8 unsafe-graph guards PASS\n";
}
}
int main(){try{algorithm7::clocks();algorithm7::spectral();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
