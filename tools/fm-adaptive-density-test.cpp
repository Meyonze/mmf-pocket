// SPDX-License-Identifier: Apache-2.0
// Selective8/16 PM density: independent tails, spectra and mixed dense FIR.
#include <cstdlib>
#include <new>
namespace allocationProbe {inline bool counting=false;inline size_t count=0;}
void* operator new(size_t n){if(allocationProbe::counting)++allocationProbe::count;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
#include <map>
#include <optional>
#include "fm-native-clock-probe.h"
#include "ma_sine_output.h"
#include "smaf_file.h"
#include "smaf_voice.h"
#include "score_audit.h"
#define private public
#include "ma_player.h"
#undef private
#include "ma_sine_density.h"
#define FM_ALGORITHM7_TEST_NO_MAIN
#include "fm-algorithm7-test.cpp"
namespace adaptive {
using namespace player_sideband;
FmVoicePatch patch(int graph,int tl=0){
 FmVoicePatch p;p.modernTimbre=true;p.fourOp=graph!=0;p.algorithm=uint8_t(graph);
 for(auto& o:p.ops){o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.ma35Multipliers=true;o.multi=1;}
 if(graph==7){p.ops[0].tl=18;p.ops[1].multi=7;p.ops[1].tl=uint8_t(tl);p.ops[2].multi=5;p.ops[3].multi=2;p.ops[3].tl=24;}
 else {p.ops[0].multi=7;p.ops[0].tl=uint8_t(tl);p.ops[1].multi=5;if(graph==5){p.ops[2].multi=3;p.ops[2].tl=24;p.ops[3].multi=2;}}
 return p;
}
double hz(int note){return 440*std::pow(2.,double(note-69)/12);}
void bounds(){size_t checks=0;
 for(double beta:{0.,.05,.5,2.,4.,12.,25.132741228718345})for(int first:{1,2,4,8,12,16,20,24,28,32,36,40,44,48,64,96}){
  double exact=0;for(int n=first;n<256;++n)exact+=2*std::abs(std::cyl_bessel_j(double(n),beta));
  require(exact<=sine_density::tailBound(beta,first)*(1+1e-10)+1e-14,"conservative tail bound underestimated Bessel coefficients");++checks;
 }
 for(int graph:{0,5,7}){
  auto q=patch(graph);FmVoice v;v.setSampleRate(rate);v.noteOn(q,hz(84),1);auto unchanged=v;
  require(v.sineDensityHint()==8,"moderate note should remain8x");for(int n=0;n<64;++n)require(v.tick()==unchanged.tick(),"density query advances native clock");
  v.setPitch(hz(96));require(v.sineDensityHint()==16,"strong high note needs16x");v.setPitch(hz(84));require(v.sineDensityHint()==16,"density decreased mid-note");
  v.noteOn(q,hz(84),1);require(v.sineDensityHint()==8,"new note did not reset density");
  v.noteOn(patch(graph,24),hz(96),1);require(v.sineDensityHint()==8,"weak modulation unnecessarily boosted");
  int m=graph==7?1:0;q.ops[size_t(m)].ar=0;v.noteOn(q,hz(96),1);require(v.sineDensityHint()==8,"permanent zero modulator boosted");
  q.ops[size_t(m)].rateLowBits=8;v.noteOn(q,hz(96),1);require(v.sineDensityHint()==16,"MA7 low-bit attack lost planning headroom");
 }
 bool scaleCrossing=false;
 for(int note=72;note<96;++note){auto q=patch(0);for(auto& o:q.ops){o.vib=true;o.dvb=3;}FmVoice v;v.setSampleRate(rate);v.noteOn(q,hz(note),1);int low=v.sineDensityHint();v.setVibratoScale(8);if(low==8&&v.sineDensityHint()==16)scaleCrossing=true;}
 require(scaleCrossing,"vibrato scale never triggers denser source");
 std::cout<<checks<<" independent Bessel tail bounds, note/density reset and vibrato guards PASS\n";
}
std::vector<double> renderAdaptive(const Case& c,int graph){
 MaPlayer p;const auto f=graph==7?algorithm7::fixture(c):player_sideband::fixture(c);
 require(p.init(f,rate,false,{8,FmSubframeMath::Hermite,true}),"adaptive fixture init");std::vector<float> all(size_t(warmup+72+frames)*2);
 require(p.render(all.data(),warmup+72+frames)==warmup+72+frames,"adaptive fixture ended early");
 require(p.diagnostics().fmNotes==1&&p.diagnostics().fallbackNotes==0&&p.diagnostics().limitedFrames==0,"adaptive fixture routing changed");
 std::vector<double> out(frames);for(int n=0;n<frames;++n)out[size_t(n)]=all[size_t(n+warmup+72)*2];return out;
}
void spectra(){size_t cases=0,boosted=0;double maxError=0;
 for(int graph:{0,7})for(int note:{60,72,84,96})for(auto [carrier,modulator]:std::array<std::pair<int,int>,2>{{{2,3},{5,7}}})for(int tl:{0,24,36}){
  const Case c{note,carrier,modulator,tl};const double base=hz(note)/rate,beta=tau*4*std::pow(10.,-.75*tl/20);
  const int selected=sine_density::needs16(beta,base*carrier,base*modulator)?16:8;boosted+=selected==16;
  const auto actual=renderAdaptive(c,graph);
  const auto expected=graph==7?algorithm7::reference7(c,selected,false):Model(c).reference(selected,false);
  const double e=error(actual,expected);maxError=std::max(maxError,e);require(e<4e-7,"adaptive output disagrees with independent selected-density model");
  const auto fixed=graph==7?algorithm7::render7(c,selected,FmSubframeMath::Hermite):render(c,selected,FmSubframeMath::Hermite);
  require(actual==fixed,"one-source adaptive differs from explicit density");
  if(note==96&&carrier==5&&tl==0){
   const auto ideal=graph==7?algorithm7::reference7(c,0,true):Model(c).reference(0,true);
   const auto old=graph==7?algorithm7::render7(c,8,FmSubframeMath::Hermite):render(c,8,FmSubframeMath::Hermite);
   require(selected==16&&error(actual,ideal)<error(old,ideal)*.001,"strong high-register error did not improve");
   std::cout<<"graph"<<graph<<" note96 ideal-band RMS fixed8="<<error(old,ideal)<<" adaptive="<<error(actual,ideal)<<'\n';
  }++cases;
 }
 require(boosted>0&&boosted<cases/2,"density policy is not selective");std::cout<<cases<<" adaptive public-player spectral cases PASS; "<<boosted<<" boosted; max model RMS "<<maxError<<'\n';
}
std::vector<double> kernel(int factor){std::vector<double> h{1};int stride=1;
 for(int n=factor;n>1;n/=2){const auto stage=fm_causal::kernel(n==2?257:33);std::vector<double> next(h.size()+(stage.size()-1)*stride);
  for(size_t i=0;i<h.size();++i)for(size_t j=0;j<stage.size();++j)next[i+j*stride]+=h[i]*stage[j];h=std::move(next);stride*=2;}return h;
}
void mixedDense(){
 MaPlayer actual,source;const Case c{84,5,7,0};for(auto* p:{&actual,&source}){
  require(p->init(algorithm7::fixture(c),rate,false,{8,FmSubframeMath::Hermite,true}),"mixed init");
  p->events_={{1024,MaPlayer::Ev::ScoreNop,0,0,0}};p->nextEvent_=0;p->nonAtrGateEventEnd_=1;p->scoreEndSample_=1024;p->endSample_=2048;
  for(int i=0;i<2;++i){p->pool_[i].noteOn(patch(i?0:7),hz(i?96:84),.3f);p->pool_[i].setVolume(.07f);p->pool_[i].channel=i;p->poolPanLeft_[i]=i?.2f:.7f;p->poolPanRight_[i]=i?.8f:.3f;}
  static const int16_t pcm[]={1000,-2000,3000,-1500,700,0,-500,2300};auto& v=p->pcmPool_[0];v.pcm=pcm;v.len=v.loopEnd=8;v.loop=true;v.qualityInterpolation=true;v.rate=.5;v.gain=.1f;v.vel=1;v.active=true;v.channel=2;v.keyNote=60;
  FmOpPatch e;e.ar=15;e.dr=e.sr=e.sl=0;v.env.configure(e,rate);v.env.keyOn();
 }
 std::array<std::vector<fm_causal::Stereo>,2> streams;std::vector<std::array<float,2>> native;
 const std::array<std::vector<double>,2> kernels{kernel(8),kernel(16)};
 float lp1L=0,lp2L=0,lp1R=0,lp2R=0;double maxError=0;
 for(int n=0;n<900;++n){for(auto* p:{&actual,&source}){
   if(n==128)p->pool_[0].setPitch(hz(96));if(n==256)p->pool_[0].setPitch(hz(84));
   if(n==384){p->pool_[0].noteOn(patch(7),hz(84),.3f);p->pool_[0].setVolume(.07f);}
   if(n==512)p->pool_[1].noteOff(true);if(n==600)p->pool_[1].setHold(false);
   if(n==650)p->fireEvent_({650,MaPlayer::Ev::AllSoundOff,0,0,0});
  }
  std::array<std::array<fm_causal::Stereo,16>,2> inputs{};
  for(int i=0;i<2;++i)if(source.pool_[i].active()){
   const int group=i==1||(n>=128&&n<384)?1:0,density=group?16:8;std::array<float,16> x{};
   require(source.pool_[i].tickSineSubframes(x.data(),density,FmSubframeMath::Hermite),"dense source");
   for(int j=0;j<density;++j){inputs[size_t(group)][size_t(j)].left+=x[size_t(j)]*.32f*source.poolPanLeft_[i];inputs[size_t(group)][size_t(j)].right+=x[size_t(j)]*.32f*source.poolPanRight_[i];}
  }
  for(int group=0;group<2;++group)for(int j=0;j<(group?16:8);++j)streams[size_t(group)].push_back(inputs[size_t(group)][size_t(j)]);
  const float pcm=source.pcmPool_[0].tick()*.32f;native.push_back({pcm,pcm});fm_causal::Stereo mix;
  for(int group=0;group<2;++group){const int density=group?16:8;const auto& h=kernels[size_t(group)];const int delay=int((h.size()-1)/2)/density,pad=72-delay;const int64_t end=int64_t(n-pad+1)*density-1;
   for(size_t k=0;k<h.size();++k)if(end-int64_t(k)>=0){mix.left+=h[k]*streams[size_t(group)][size_t(end-int64_t(k))].left;mix.right+=h[k]*streams[size_t(group)][size_t(end-int64_t(k))].right;}
  }
  if(n>=72){mix.left+=native[size_t(n-72)][0];mix.right+=native[size_t(n-72)][1];}
  const float l=float(mix.left),r=float(mix.right);lp1L+=actual.lpCoef_*(l-lp1L);lp2L+=actual.lpCoef_*(lp1L-lp2L);lp1R+=actual.lpCoef_*(r-lp1R);lp2R+=actual.lpCoef_*(lp1R-lp2R);
  float y[2];allocationProbe::counting=true;const int rendered=actual.render(y,1);allocationProbe::counting=false;
  require(rendered==1,"adaptive mixed stream ended early");require(actual.diagnostics().limitedFrames==0,"dense oracle requires unlimited mix");
  const double e=std::max(std::abs(double(y[0])-lp2L),std::abs(double(y[1])-lp2R));maxError=std::max(maxError,e);require(e<2e-7,"adaptive mixed source differs from independent dense8+16 FIR");
  for(int i=0;i<2;++i){require(actual.pool_[i].recentLevel()==source.pool_[i].recentLevel(),"adaptive routing altered allocation clock");for(int j=0;j<4;++j)require(actual.pool_[i].ops_[j].phase_==source.pool_[i].ops_[j].phase_,"adaptive routing advanced native phase twice");}
 }
 require(allocationProbe::count==0,"adaptive render performed ordinary C++ heap allocation");
 std::cout<<"900 mixed native/8x/16x frames with pitch raise/lower, retrigger, Hold and stop PASS; max error "<<maxError<<"; ordinary C++ new0\n";
}
void boundaries(){
 for(int factor:{0,1,2,4,16,32}){MaPlayer p;require(!p.init(algorithm7::fixture({96,5,7,0}),rate,false,{factor,FmSubframeMath::Hermite,true}),"invalid boost baseline accepted");}
 for(int block:{1,17,512,1024})for(float gain:{1.f,1e-12f}){
  std::array<MaPlayer,2> players;
  for(int i=0;i<2;++i){auto& p=players[size_t(i)];require(p.init(algorithm7::fixture({96,5,7,0}),rate,false,{i?16:8,FmSubframeMath::Hermite,i==0}),"final-frame init");
   const auto found=std::find_if(p.events_.begin(),p.events_.end(),[](const auto& e){return e.type==MaPlayer::Ev::NoteOn;});require(found!=p.events_.end(),"missing authored note");auto note=*found;note.sample=0;
   p.events_={note,{1,MaPlayer::Ev::AllSoundOff,0,0,0}};p.nextEvent_=0;p.nonAtrGateEventEnd_=2;p.scoreEndSample_=1;p.endSample_=2048;p.chans_[0].volume=gain;
  }
  auto drain=[&](MaPlayer& p){std::vector<float> out,buffer(size_t(block)*2);int n;while((n=p.render(buffer.data(),block))>0){out.insert(out.end(),buffer.begin(),buffer.begin()+n*2);require(out.size()<4096,"tail escaped bound");}return out;};
  const auto a=drain(players[0]),b=drain(players[1]);require(a==b&&!a.empty()&&players[0].cursor_==1,"adaptive final-frame tail lost or counted extra source ticks");
  players[0].seekToStart();players[0].chans_[0].volume=gain;require(drain(players[0])==a,"adaptive seek did not reset both histories");
  for(int i=0;i<2;++i){auto& p=players[size_t(i)];require(p.init(algorithm7::fixture({96,5,7,0}),rate,false,{i?16:8,FmSubframeMath::Hermite,i==0}),"hard cap init");p.endSample_=96;}
  const auto capped=drain(players[0]);require(capped==drain(players[1])&&capped.size()==168*2&&players[0].diagnostics().tailLimitReached,"adaptive hard cap/fade changed");
 }
 std::cout<<"Invalid policy guards, final one-frame/tiny16x tails, hard caps/fade, block invariance and seek PASS\n";
}
}
#ifndef FM_ADAPTIVE_DENSITY_TEST_NO_MAIN
int main(){try{adaptive::bounds();adaptive::spectra();adaptive::mixedDense();adaptive::boundaries();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
