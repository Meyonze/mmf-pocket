// SPDX-License-Identifier: Apache-2.0
// Host diagnostic only: copies native voices without advancing the live player.
#include <optional>
#include "ma_fm_core.h"
#include "ma_sine_output.h"
#include "smaf_file.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <map>
#define private public
#include "ma_player.h"
#undef private
#define RENDER_PRESSURE_AUDIT_NO_MAIN
#include "render-pressure-audit.cpp"
#include <bit>
#include <charconv>
#include <tuple>
namespace contribution {
using namespace fxchain::smaf;
using Metric=renderPressure::Window;
using Stereo=sine_output::Stereo;
constexpr std::array<size_t,6> triggerIndices{0,1,2,8,9,12};
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool bits(float a,float b){return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);}
struct Key {int domain=0,track=-1,channel=0;auto operator<=>(const Key&)const=default;};
struct Part {
 Metric source,filtered,output;
 float left=0,right=0,lp1L=0,lp2L=0,lp1R=0,lp2R=0;
 std::array<Stereo,128> high{};std::optional<sine_output::Bus> bus;
 std::array<Stereo,16> boosted{};std::optional<sine_output::Bus> highBus;bool boostedInput=false;
 sine_output::NestedBankInput extra;std::optional<sine_output::NestedBanks> extraBanks;
 uint64_t sineFrames=0,pendingSineFrames=0;
 uint64_t boostedFrames=0,pendingBoostedFrames=0;
 std::array<uint64_t,7> densityFrames{},pendingDensityFrames{};
 uint64_t notes=0,triggers=0,peakVoices=0;
 std::array<uint64_t,4> voiceFrames{},pendingFrames{};
 std::array<uint64_t,6> triggerCounts{};uint64_t voices=0;
};
struct Report {
 uint32_t rate=0;int factor=0;FmSubframeMath math=FmSubframeMath::Exact;uint64_t frames=0;bool finished=false,tailLimit=false,scanComplete=false,boostHighNotes=false,nestedSine=false,adaptiveNested=false;
 Metric mix;double sumErrorPeak=0,sumErrorSquares=0;
 renderPressure::Counters totals{};
 std::map<Key,Part> parts;
};
Report analyzePlayers(MaPlayer& player,MaPlayer& reference,uint64_t cap,int block=512){
 const bool boost=player.sineRender_&&player.sineRender_->highBus.has_value();
 const bool nested=player.sineRender_&&player.sineRender_->nestedSine;
 const bool adaptive=player.sineRender_&&player.sineRender_->nestedBanks.has_value();
 require(adaptive==bool(reference.sineRender_&&reference.sineRender_->nestedBanks),"nested adaptive policies differ");
 require(nested==bool(reference.sineRender_&&reference.sineRender_->nestedSine),"nested source policies differ");
 require(boost==bool(reference.sineRender_&&reference.sineRender_->highBus),"adaptive source policies differ");
 require(&player!=&reference&&player.rate_==reference.rate_,"independent players with equal sample rates required");
 const int factor=player.sineRender_?player.sineRender_->bus.factor():0;
 const auto math=player.sineRender_?player.sineRender_->math:FmSubframeMath::Exact;
 require(player.cursor_==0&&reference.cursor_==0&&factor==(reference.sineRender_?reference.sineRender_->bus.factor():0)&&math==(reference.sineRender_?reference.sineRender_->math:FmSubframeMath::Exact),"audit requires fresh players with matching source policies");
 require(!factor||(player.sineRender_->outputCursor==0&&reference.sineRender_->outputCursor==0),"physical output already advanced");
 const uint32_t rate=player.rate_;require(rate>0&&cap>0&&cap<=uint64_t(rate)*600+(factor?72:0)&&block>=1&&block<=1024,"invalid audit bounds");
 Report result;result.rate=rate;result.factor=factor;result.math=math;result.boostHighNotes=boost;result.nestedSine=nested;result.adaptiveNested=adaptive;result.scanComplete=player.scoreAudit().scanComplete;
 std::optional<sine_output::Bus> bus,highBus;if(factor)bus.emplace(factor,nested);if(boost)highBus.emplace(16);
 std::optional<sine_output::NestedBanks> extraBanks;if(adaptive)extraBanks.emplace();
 std::array<Part*,128> score{};
 auto part=[&](Key key)->Part& {
  require(key.channel>=0&&key.channel<128,"invalid source channel");
  if(key.domain==0&&score[size_t(key.channel)])return *score[size_t(key.channel)];
  auto [it,inserted]=result.parts.try_emplace(key);
  if(inserted){it->second.source.frames=it->second.filtered.frames=it->second.output.frames=result.frames;if(factor)it->second.bus.emplace(factor,nested);if(boost)it->second.highBus.emplace(16);if(adaptive)it->second.extraBanks.emplace();}
  if(key.domain==0)score[size_t(key.channel)]=&it->second;
  return it->second;
 };
 std::array<float,2048> ref{};int refAt=0,refCount=0;
 float lp1L=0,lp2L=0,lp1R=0,lp2R=0;
 while(result.frames<cap&&!player.finished()){
  // Match the renderer's entry guard before firing terminal boundary events.
  if((!factor||!player.sineRender_->inputFinished)&&player.cursor_<player.endSample_)while(player.nextEvent_<player.events_.size()&&player.events_[player.nextEvent_].sample<=player.cursor_){
   const auto& e=player.events_[player.nextEvent_];
   Part* trigger=nullptr;
   if((e.type==MaPlayer::Ev::NoteOn||e.type==MaPlayer::Ev::Ma7NoteOn)&&e.ch<128){trigger=&part({0,-1,int(e.ch)});++trigger->notes;}
   if(e.type==MaPlayer::Ev::WaveOn&&e.ch<128){trigger=&part({1,e.b,int(e.ch)});++trigger->triggers;}
   const auto before=renderPressure::counters(player.diagnostics());
   player.fireEvent_(e);++player.nextEvent_;
   if(trigger){const auto after=renderPressure::counters(player.diagnostics());for(size_t k=0;k<triggerIndices.size();++k)trigger->triggerCounts[k]+=after[triggerIndices[k]]-before[triggerIndices[k]];}
  }
  sine_output::NestedBankInput extra;
  float left=0,right=0;std::array<Stereo,128> high{};std::array<Stereo,16> boosted{};bool boostedInput=false;
  auto add=[&](Key key,int kind,float sample,float panL,float panR){
   const float l=sample*.32f*panL,r=sample*.32f*panR;
   require(std::isfinite(l)&&std::isfinite(r),"nonfinite native contribution");
   left+=l;right+=r;auto& p=part(key);p.left+=l;p.right+=r;++p.voices;++p.pendingFrames[size_t(kind)];
  };
  auto addSine=[&](Key key,const float* samples,float panL,float panR,int density,bool raised,int nestedDensity){auto& p=part(key);
   auto* sum=raised?boosted.data():high.data();auto* component=raised?p.boosted.data():p.high.data();
   if(nestedDensity>=32){sum=extra.use(nestedDensity);component=p.extra.use(nestedDensity);}
   for(int k=0;k<density;++k){const float l=samples[k]*.32f*panL,r=samples[k]*.32f*panR;
    sum[size_t(k)].left+=l;sum[size_t(k)].right+=r;component[size_t(k)].left+=l;component[size_t(k)].right+=r;}
   ++p.voices;++p.pendingFrames[0];++p.pendingSineFrames;
   ++p.pendingDensityFrames[size_t(std::countr_zero(unsigned(density))-1)];
   if(raised){++p.pendingBoostedFrames;p.boostedInput=boostedInput=true;}
  };
  if((!factor||!player.sineRender_->inputFinished)&&player.cursor_<player.endSample_){
   for(size_t i=0;i<player.pool_.size();++i)if(player.pool_[i].active()){
    auto copy=player.pool_[i];std::array<float,128> samples;
    const int nestedDensity=adaptive?copy.nestedSineDensityHint():0;
    const bool raised=boost&&(nestedDensity==16||(!nestedDensity&&copy.sineDensityHint()==16));const int density=nestedDensity?nestedDensity:raised?16:factor;
    if(density>1&&((nested||nestedDensity)?copy.tickNestedSubframes(samples.data(),density,math):copy.tickSineSubframes(samples.data(),density,math)))addSine({0,-1,copy.channel},samples.data(),player.poolPanLeft_[i],player.poolPanRight_[i],density,raised,nestedDensity);
    else add({0,-1,copy.channel},0,copy.tick(),player.poolPanLeft_[i],player.poolPanRight_[i]);
   }
   for(const auto& v:player.pcmPool_)if(v.active){auto copy=v;
    add(v.keyNote<0?Key{1,v.atrTrack,v.channel}:Key{0,-1,v.channel},v.keyNote<0?3:1,copy.tick(),v.panLeft,v.panRight);
   }
   for(const auto& v:player.audioPool_)if(v.active){auto copy=v;add({0,-1,v.channel},2,copy.tick(),v.panLeft,v.panRight);}
  }
  const uint64_t cursor=player.cursor_,physical=factor?player.sineRender_->outputCursor:cursor;
  float actual[2];const int got=player.render(actual,1);
  require(got==0||got==1,"invalid physical frame count");
  auto clearInputs=[&]{for(auto& [key,p]:result.parts){p.pendingFrames.fill(0);p.pendingSineFrames=p.pendingBoostedFrames=0;p.pendingDensityFrames.fill(0);p.voices=0;p.left=p.right=0;if(factor)p.high.fill({});if(boost)p.boosted.fill({});p.boostedInput=false;p.extra={};}};
  if(got==0){clearInputs();break;}
  if(player.cursor_==cursor){clearInputs();left=right=0;high.fill({});boosted.fill({});boostedInput=false;extra={};}
  else require(player.cursor_==cursor+1,"source clock advanced unexpectedly");
  if(bus){auto x=bus->frame(std::span(high.data(),size_t(factor)),{left,right});
   if(highBus&&(boostedInput||highBus->pendingFrames()!=0)){const auto extra=highBus->frame(boosted,{});x.left+=extra.left;x.right+=extra.right;}
   if(extraBanks){auto y=extraBanks->frame(extra);x.left+=y.left;x.right+=y.right;}
   left=float(x.left);right=float(x.right);}
  // Original summation order is kept independently of per-channel grouping.
  lp1L+=player.lpCoef_*(left-lp1L);lp2L+=player.lpCoef_*(lp1L-lp2L);
  lp1R+=player.lpCoef_*(right-lp1R);lp2R+=player.lpCoef_*(lp1R-lp2R);
  require(bits(lp1L,player.lp1L_)&&bits(lp2L,player.lp2L_)&&bits(lp1R,player.lp1R_)&&bits(lp2R,player.lp2R_),"voice copies changed the native source/filter stream");
  const float limiter=player.limEnv_>.92f?.92f/player.limEnv_:1;
  const uint64_t remaining=player.endSample_+(factor?72:0)-physical,fadeFrames=std::max<uint64_t>(2,rate/50);
  const float fade=remaining<=fadeFrames?float(remaining-1)/float(fadeFrames-1):1;
  float expectedL=lp2L*limiter,expectedR=lp2R*limiter;expectedL*=fade;expectedR*=fade;
  require(bits(expectedL,actual[0])&&bits(expectedR,actual[1]),"native shared gain/fade reconstruction differs");
  if(refAt==refCount){const int want=int(std::min<uint64_t>(block,cap-result.frames));refCount=reference.render(ref.data(),want);refAt=0;require(refCount>0&&refCount<=want,"uninstrumented renderer ended early");}
  require(bits(actual[0],ref[size_t(refAt)*2])&&bits(actual[1],ref[size_t(refAt)*2+1]),"instrumented/default-block output differs");++refAt;
  double sumL=0,sumR=0;
  for(auto& [key,p]:result.parts){
   float l=p.left,r=p.right;
   if(p.bus){auto x=p.bus->frame(std::span(p.high.data(),size_t(factor)),{l,r});
    if(p.highBus&&(p.boostedInput||p.highBus->pendingFrames()!=0)){const auto extra=p.highBus->frame(p.boosted,{});x.left+=extra.left;x.right+=extra.right;}
    if(p.extraBanks){auto y=p.extraBanks->frame(p.extra);x.left+=y.left;x.right+=y.right;}
    l=float(x.left);r=float(x.right);}
   p.source.add(l,r);
   p.lp1L+=player.lpCoef_*(l-p.lp1L);p.lp2L+=player.lpCoef_*(p.lp1L-p.lp2L);
   p.lp1R+=player.lpCoef_*(r-p.lp1R);p.lp2R+=player.lpCoef_*(p.lp1R-p.lp2R);
   p.filtered.add(p.lp2L,p.lp2R);
   l=p.lp2L*limiter;r=p.lp2R*limiter;l*=fade;r*=fade;p.output.add(l,r);sumL+=l;sumR+=r;
   for(size_t k=0;k<4;++k)p.voiceFrames[k]+=p.pendingFrames[k];p.pendingFrames.fill(0);p.sineFrames+=p.pendingSineFrames;p.pendingSineFrames=0;if(factor)p.high.fill({});
   p.boostedFrames+=p.pendingBoostedFrames;p.pendingBoostedFrames=0;
   for(size_t k=0;k<p.densityFrames.size();++k)p.densityFrames[k]+=p.pendingDensityFrames[k];p.pendingDensityFrames.fill(0);
   if(boost)p.boosted.fill({});p.boostedInput=false;p.extra={};
   p.peakVoices=std::max(p.peakVoices,p.voices);p.voices=0;p.left=p.right=0;
  }
  const double errorL=sumL-actual[0],errorR=sumR-actual[1];
  result.sumErrorPeak=std::max({result.sumErrorPeak,std::abs(errorL),std::abs(errorR)});
  result.sumErrorSquares+=errorL*errorL+errorR*errorR;
  result.mix.add(actual[0],actual[1]);++result.frames;
 }
 require(refAt==refCount,"uninstrumented renderer has extra buffered output");
 result.finished=player.finished();result.tailLimit=player.diagnostics().tailLimitReached;
 if(result.finished&&!reference.finished()){float end[2];require(reference.render(end,1)==0,"uninstrumented renderer has extra terminal output");}
 require(result.finished==reference.finished()&&player.diagnostics().tailLimitReached==reference.diagnostics().tailLimitReached&&renderPressure::counters(player.diagnostics())==renderPressure::counters(reference.diagnostics()),"native endpoint or allocation diagnostics differ");
 result.totals=renderPressure::counters(player.diagnostics());
 // A channel may first appear at a silent terminal event; zero prefix is implicit.
 for(auto& [key,p]:result.parts){uint64_t sum=0;for(auto count:p.densityFrames)sum+=count;
  require(p.source.frames==result.frames&&p.filtered.frames==result.frames&&p.output.frames==result.frames&&p.sineFrames<=p.voiceFrames[0]&&sum==p.sineFrames&&p.boostedFrames<=p.densityFrames[3],"part sample/source count differs");}
 require(result.sumErrorPeak<2e-5,"component superposition error too large");return result;
}
Report analyze(const SmafFile& file,uint64_t cap,int block=512,SineRenderOptions options={}){MaPlayer p,ref;require(p.init(file,44100,false,options)&&ref.init(file,44100,false,options),"no playable track or invalid source policy");return analyzePlayers(p,ref,cap,block);}
void metric(std::ostream& out,const Metric& m){out<<"{\"peak\":"<<m.peak<<",\"rms\":"<<m.rms()<<",\"ac_rms\":"<<m.acRms()<<",\"dc_left\":"<<m.meanL<<",\"dc_right\":"<<m.meanR<<'}';}
void write(std::ostream& out,const Report& r){
 out<<std::setprecision(12)<<std::boolalpha<<"{\"schema_version\":1,\"scope\":\"Software player with explicit source policy; per-domain channel linear contributions with actual shared limiter/fade, not isolated or perceptual loudness; component powers do not add; host diagnostic duplicates synthesis, not runtime performance\",\"sample_rate\":"<<r.rate<<",\"sine_factor\":"<<r.factor<<",\"boost_high_notes\":"<<r.boostHighNotes<<",\"sine_math\":\""<<(r.math==FmSubframeMath::Exact?"exact":"hermite")<<"\",\"latency_frames\":"<<(r.factor?72:0)<<",\"frames\":"<<r.frames<<",\"finished\":"<<r.finished<<",\"cap_reached\":"<<!r.finished<<",\"score_scan_complete\":"<<r.scanComplete<<",\"tail_limit_reached\":"<<r.tailLimit<<",\"reference_pcm_exact\":true,\"component_sum_error_peak\":"<<r.sumErrorPeak<<",\"component_sum_error_rms\":"<<(r.frames?std::sqrt(r.sumErrorSquares/(2*r.frames)):0)<<",\"mix\":";
 metric(out,r.mix);if(r.nestedSine)out<<",\"nested_sine\":true";if(r.adaptiveNested)out<<",\"adaptive_nested\":true";out<<",\"totals\":";renderPressure::writeCounters(out,r.totals);out<<",\"parts\":[";bool first=true;
 for(const auto& [key,p]:r.parts){if(!first)out<<',';first=false;
  out<<"{\"domain\":\""<<(key.domain?"atr":"score")<<"\",\"channel\":"<<key.channel<<",\"atr_track\":"<<key.track<<",\"authored_notes\":"<<p.notes<<",\"authored_wave_triggers\":"<<p.triggers<<",\"peak_native_sources\":"<<p.peakVoices<<",\"sine_source_voice_frames\":"<<p.sineFrames<<",\"boosted_sine_voice_frames\":"<<p.boostedFrames<<",\"sine_voice_frames_by_factor\":{";for(size_t k=0;k<((r.nestedSine||r.adaptiveNested)?7u:5u);++k){if(k)out<<',';out<<'"'<<(2u<<k)<<"\":"<<p.densityFrames[k];}out<<"},\"voice_frames\":{\"fm\":"<<p.voiceFrames[0]<<",\"pcm\":"<<p.voiceFrames[1]<<",\"audio\":"<<p.voiceFrames[2]<<",\"atr\":"<<p.voiceFrames[3]<<"},\"before_output_filter\":";metric(out,p.source);out<<",\"trigger_diagnostics\":{";
  for(size_t k=0;k<triggerIndices.size();++k){if(k)out<<',';out<<'"'<<renderPressure::names[triggerIndices[k]]<<"\":"<<p.triggerCounts[k];}out<<'}';
  out<<",\"after_output_filter\":";metric(out,p.filtered);out<<",\"with_shared_output_gain\":";metric(out,p.output);out<<'}';
 }
 out<<"]}\n";out.flush();require(bool(out),"cannot write JSON report");
}
}
#ifndef RENDER_CONTRIBUTION_AUDIT_NO_MAIN
int main(int argc,char** argv){
 using namespace fxchain::smaf;
 const char* usage="Usage: render-contribution-audit <input.mmf> [max-seconds] [--app-quality | --experimental-sine-factor=1|2|4|8|16|32|64|128 --experimental-sine-math=exact|hermite [--experimental-nested-sine] | --experimental-adaptive-nested]\n";
 if(argc==2&&std::string_view(argv[1])=="--help"){std::cout<<usage<<"Read-only channel/ATR contribution study. Default600 seconds; no song metadata exported.\n";return 0;}
 if(argc<2||argc>6){std::cerr<<usage;return 2;}
 double seconds=600;int option=2;SineRenderOptions options;
 if(argc>2&&!std::string_view(argv[2]).starts_with("--")){char* end=nullptr;errno=0;seconds=std::strtod(argv[2],&end);if(end==argv[2]||*end||errno==ERANGE||!std::isfinite(seconds)||seconds<1./44100){std::cerr<<"invalid max-seconds\n";return 2;}++option;}
 bool factorSeen=false,mathSeen=false,appSeen=false;
 for(;option<argc;++option){std::string_view arg(argv[option]);
  if(arg=="--app-quality"&&!appSeen&&!factorSeen&&!mathSeen&&!options.nestedSine&&!options.adaptiveNested){options={8,FmSubframeMath::Hermite,true};appSeen=true;
  }else if(arg.starts_with("--experimental-sine-factor=")&&!factorSeen&&!appSeen&&!options.adaptiveNested){auto value=arg.substr(std::string_view("--experimental-sine-factor=").size());auto parsed=std::from_chars(value.data(),value.data()+value.size(),options.factor);
   if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||!(sine_output::validFactor(options.factor)||sine_output::validNestedFactor(options.factor))){std::cerr<<"invalid sine factor\n";return 2;}factorSeen=true;
  }else if(arg.starts_with("--experimental-sine-math=")&&!mathSeen&&!appSeen&&!options.adaptiveNested){auto value=arg.substr(std::string_view("--experimental-sine-math=").size());
   if(value=="exact")options.math=FmSubframeMath::Exact;else if(value=="hermite")options.math=FmSubframeMath::Hermite;else{std::cerr<<"invalid sine math\n";return 2;}mathSeen=true;
  }else if(arg=="--experimental-adaptive-nested"&&!options.adaptiveNested&&!factorSeen&&!mathSeen&&!appSeen&&!options.nestedSine){options={8,FmSubframeMath::Hermite,true,false,true};
  }else if(arg=="--experimental-nested-sine"&&!options.nestedSine&&!appSeen&&!options.adaptiveNested){options.nestedSine=true;
  }else{std::cerr<<"unknown or duplicate option\n";return 2;}
 }
 if((options.nestedSine?(!factorSeen||!sine_output::validNestedFactor(options.factor)):options.factor>32)||(mathSeen&&!factorSeen)||seconds>600+(options.factor?72./44100:0)){std::cerr<<"invalid source policy or render bound\n";return 2;}
 try{auto data=renderPressure::read(argv[1]);fxchain::smaf::SmafFile f;if(!f.parse(data.data(),data.size()))throw std::runtime_error("invalid SMAF input");contribution::write(std::cout,contribution::analyze(f,uint64_t(seconds*44100),512,options));return 0;}
 catch(const std::exception& e){std::cerr<<"render-contribution-audit: "<<e.what()<<'\n';return 1;}
}
#endif
