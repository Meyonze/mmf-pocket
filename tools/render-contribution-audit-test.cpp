// SPDX-License-Identifier: Apache-2.0
#define RENDER_CONTRIBUTION_AUDIT_NO_MAIN
#include "render-contribution-audit.cpp"
#include <sstream>
using namespace contribution;
SmafFile clockFile(){SmafFile f;TrackChunk t;t.formatType=2;t.channelStatus.resize(16);t.durationTimeBase=t.gateTimeBase=0x10;t.sequenceData={0x83,0x10,0xff,0x2f,0};
 const std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,0,0xa0,0,0xfc,0,0x10,0,0,0xa0,0xf0,0,0,0x10,0};
 t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
 f.tracks.push_back(t);return f;}
void init(MaPlayer& p,int factor=0,FmSubframeMath math=FmSubframeMath::Exact){require(p.init(clockFile(),8000,false,{factor,math}),"test clock init");p.lpCoef_=1;}
void pcm(MaPlayer& p,int slot,int channel,int16_t value,bool atr=false,int track=-1){
 static std::array<std::array<int16_t,2>,4> data{{{{1000,1000}},{{2000,2000}},{{-1000,-1000}},{{32767,32767}}}};
 const auto i=value==1000?0:value==2000?1:value==-1000?2:3;
 auto& v=p.pcmPool_[size_t(slot)];v=MaPlayer::PcmVoice{};v.pcm=data[size_t(i)].data();v.len=2;v.loop=true;v.loopEnd=2;v.active=true;v.channel=channel;v.keyNote=atr?-1:60;v.atrTrack=track;
 FmOpPatch flat;flat.ar=15;flat.dr=flat.sr=flat.sl=0;v.env.configure(flat,8000);v.env.keyOn();
}
void dcAndBlocks(){std::string original;
 for(int block:{1,17,512,1024}){MaPlayer p,b;init(p);init(b);for(auto* x:{&p,&b}){pcm(*x,0,0,1000);pcm(*x,1,1,2000);}
  const auto r=analyzePlayers(p,b,256,block);require(r.frames==256&&!r.finished&&r.parts.size()==2,"DC report horizon");
  const auto& a=r.parts.at({0,-1,0});const auto& c=r.parts.at({0,-1,1});
  require(a.voiceFrames[1]==256&&c.voiceFrames[1]==256&&a.peakVoices==1,"exact native source counts");
  require(a.source.rms()>0&&c.source.rms()==2*a.source.rms(),"independent PCM amplitude ratio");
  require(a.output.rms()==a.source.rms()&&a.output.acRms()==0&&r.sumErrorPeak==0,"DC and linear identity-filter oracle");
  std::ostringstream text;write(text,r);if(original.empty())original=text.str();else require(text.str()==original,"reference block size changes contribution report");
 }
 std::cout<<"Analytical DC/amplitude and1/17/512/1024-frame reference invariance PASS\n";
}
void sharedLimiterAndCancellation(){
 {MaPlayer p,b;init(p);init(b);for(auto* x:{&p,&b})for(int i=0;i<32;++i)pcm(*x,i,i%2,32767);
  const auto r=analyzePlayers(p,b,256,17);require(r.totals[11]==256,"hot source must use shared limiter");
  for(const auto& [key,v]:r.parts){require(v.filtered.rms()>5&&std::abs(v.output.rms()-.46)<1e-5,"each symmetric component sees actual shared gain");require(v.output.acRms()==0,"constant output remains DC");}
  require(std::abs(r.mix.rms()-.92)<1e-6&&r.sumErrorPeak<1e-6,"limited components reconstruct bounded actual output");
 }
 {MaPlayer p,b;init(p);init(b);for(auto* x:{&p,&b}){pcm(*x,0,0,1000);pcm(*x,1,1,-1000);}
  const auto r=analyzePlayers(p,b,256);require(r.mix.rms()==0&&r.sumErrorPeak==0,"opposing components cancel in real mix");
  for(const auto& [key,v]:r.parts)require(v.output.rms()>0,"individual powers are not actual mix power");
 }
 std::cout<<"Shared limiter/DC and coherent cancellation oracles PASS\n";
}
void domainsAndTerminal(){
 {MaPlayer p,b;init(p);init(b);for(auto* x:{&p,&b}){pcm(*x,0,120,1000);pcm(*x,1,120,2000,true,7);
   x->events_.insert(x->events_.begin(),{64,MaPlayer::Ev::AllSoundOff,120,0,0});x->nonAtrGateEventEnd_=x->events_.size();}
  const auto r=analyzePlayers(p,b,128);require(r.parts.size()==2,"same number in score and ATR collapsed domains");
  require(r.parts.at({0,-1,120}).voiceFrames[1]==64&&r.parts.at({1,7,120}).voiceFrames[3]==128,"score stop altered independent ATR counts");
 }
 {MaPlayer p,b;init(p);init(b);for(auto* x:{&p,&b}){x->events_={{0,MaPlayer::Ev::ScoreNop,0,0,0}};x->nonAtrGateEventEnd_=1;x->scoreEndSample_=0;
   x->pool_[0].noteOn(FmVoicePatch::defaultPatch(),317,1);x->pool_[0].channel=0;x->pool_[0].setVolume(0);}
  const auto r=analyzePlayers(p,b,128);require(r.frames==0&&r.finished,"terminal silence produced invented output");
  for(const auto& [key,v]:r.parts)for(auto count:v.voiceFrames)require(count==0,"unrendered terminal frame counted");
 }
 std::cout<<"Independent ATR/score domains, channel stop and unrendered terminal-frame counts PASS\n";
}
void hardBoundaryAndGuards(){
 {MaPlayer p,b;init(p);init(b);for(auto* x:{&p,&b}){pcm(*x,0,0,1000);x->endSample_=96;
   x->events_.insert(x->events_.begin(),{96,MaPlayer::Ev::NoteOn,1,69,127,1});x->nonAtrGateEventEnd_=x->events_.size();}
  const auto r=analyzePlayers(p,b,256,17);
  require(r.frames==96&&r.finished&&r.tailLimit&&r.parts.size()==1&&r.totals[0]==0,"hard boundary fired a future note or lost actual EOF");
  require(r.sumErrorPeak==0&&r.parts.at({0,-1,0}).voiceFrames[1]==96,"hard fade contribution reconstruction/counts");
 }
 for(int kind=0;kind<5;++kind){MaPlayer p,b;init(p);init(b);bool rejected=false;
  try{if(kind==0)(void)analyzePlayers(p,p,128);if(kind==1)(void)analyzePlayers(p,b,0);if(kind==2)(void)analyzePlayers(p,b,128,0);if(kind==3)(void)analyzePlayers(p,b,128,1025);if(kind==4)(void)analyzePlayers(p,b,8000ULL*600+1);}
  catch(const std::runtime_error&){rejected=true;}require(rejected,"bad independent-player/render bound accepted");
 }
 {MaPlayer p,b;init(p);init(b);p.sineRender_.emplace(SineRenderOptions{8,FmSubframeMath::Hermite,true});bool rejected=false;
  try{(void)analyzePlayers(p,b,128);}catch(const std::runtime_error&){rejected=true;}require(rejected,"mismatched adaptive policy accepted");}
 std::cout<<"Hard fade/endpoints/future-event exclusion and bounded input guards PASS\n";
}

void triggerKinds(){
 SmafFile f;TrackChunk t;t.formatType=2;t.durationTimeBase=t.gateTimeBase=0x10;t.channelStatus.resize(16);
 std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,0,0xa0,0,0xfc,0,0x10,0,0,0xa0,0xf0,0,0,0x10,0};
 t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
 t.sequenceData={0,0x90,60,100,10,0,0xc1,1,0,0x91,67,100,10,0,0xb2,0,125,0,0x92,0,100,10,20,0xff,0x2f,0};f.tracks.push_back(t);
 const auto r=analyze(f,44100);
 require(r.parts.at({0,-1,0}).triggerCounts[0]==1&&r.parts.at({0,-1,0}).triggerCounts[2]==0,"bound FM incorrectly reported as fallback");
 require(r.parts.at({0,-1,1}).triggerCounts[0]==1&&r.parts.at({0,-1,1}).triggerCounts[2]==1,"fallback not attributed to triggering channel");
 require(r.parts.at({0,-1,2}).triggerCounts[4]==1&&r.parts.at({0,-1,2}).output.rms()==0,"missing recorded wave not attributed to silent channel");
 SmafFile silent;TrackChunk m;m.formatType=3;m.durationTimeBase=m.gateTimeBase=0x10;m.channelStatus.resize(32);m.sequenceData={0,0x10,60,1,10,20,0xff,0};silent.tracks.push_back(m);
 const auto quiet=analyze(silent,44100);require(quiet.parts.at({0,-1,0}).triggerCounts[5]==1&&quiet.parts.at({0,-1,0}).voiceFrames[0]==0,"immutable velocity1 silence not attributed to part");
 std::cout<<"Bound FM/fallback/missing Audio/immutable-silence trigger attribution PASS\n";
}

void qualityRoutes(){size_t cases=0;
 for(int algorithm:{0,3,4,5,6,7,106})for(int factor:{1,2,8,32})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite}){
  MaPlayer p,b;init(p,factor,math);init(b,factor,math);
  FmVoicePatch q;q.modernTimbre=true;q.fourOp=algorithm!=0;q.algorithm=uint8_t(algorithm==106?6:algorithm);q.lfo=2;
  for(int j=0;j<4;++j){auto& o=q.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(j%2?2:3);o.tl=uint8_t(j%2?0:24);o.am=o.vib=true;o.dam=o.dvb=2;}
  if(algorithm==3||algorithm==4||algorithm==6)q.ops[size_t(algorithm==3?2:1)].ar=0;
  if(algorithm==106)for(auto& o:q.ops){o.ma35Multipliers=true;o.tl=24;}
  for(auto* x:{&p,&b}){x->pool_[0].noteOn(q,317,.6f);x->pool_[0].channel=0;x->poolPanLeft_[0]=.7f;x->poolPanRight_[0]=.3f;
   pcm(*x,0,120,1000);pcm(*x,1,120,2000,true,7);
   std::vector<MaPlayer::Ev> events{{128,MaPlayer::Ev::Volume,0,0,0},{160,MaPlayer::Ev::Volume,0,127,0},{192,MaPlayer::Ev::AllSoundOff,0,0,0},{256,MaPlayer::Ev::AllSoundOff,120,0,0},{384,MaPlayer::Ev::NoteOn,16,69,100,1},{448,MaPlayer::Ev::AllSoundOff,16,0,0}};
   x->events_.insert(x->events_.begin(),events.begin(),events.end());x->nonAtrGateEventEnd_=x->events_.size();
  }
  const auto r=analyzePlayers(p,b,512,cases%2?17:512);
  require(r.factor==factor&&r.math==math&&r.frames==512&&!r.finished,"explicit quality policy/horizon changed");
  require(r.parts.at({0,-1,0}).voiceFrames[0]==192&&r.parts.at({0,-1,16}).voiceFrames[0]==64,"source tick counts changed with physical delay");
  require(r.parts.at({0,-1,120}).voiceFrames[1]==256&&r.parts.at({1,7,120}).voiceFrames[3]==512,"quality route mixed keyed/ATR domains");
  require(r.parts.at({0,-1,0}).sineFrames==uint64_t(factor>1?192:0)&&r.parts.at({0,-1,16}).sineFrames==uint64_t(factor>1?64:0),"subframe source attribution wrong");
  require(r.sumErrorPeak<2e-5,"mixed quality components do not reconstruct actual delayed output");++cases;
 }
 for(int factor:{1,8,32}){MaPlayer p,b;init(p,factor);init(b,factor);for(auto* x:{&p,&b}){pcm(*x,0,0,1000);x->endSample_=96;
   x->events_.insert(x->events_.begin(),{96,MaPlayer::Ev::NoteOn,1,69,127,1});x->nonAtrGateEventEnd_=x->events_.size();}
  const auto r=analyzePlayers(p,b,256,17);require(r.frames==168&&r.finished&&r.tailLimit&&r.parts.size()==1&&r.totals[0]==0,"physical hard boundary/future-event handling wrong");
  require(r.parts.at({0,-1,0}).voiceFrames[1]==96&&r.sumErrorPeak==0,"delayed hard tail counted phantom source ticks");
 }
 for(int factor:{2,8,32})for(float gain:{1.f,1e-12f}){MaPlayer p,b;init(p,factor);init(b,factor);
  for(auto* x:{&p,&b}){auto q=FmVoicePatch::defaultPatch();q.modernTimbre=true;q.algorithm=0;q.fourOp=false;
   for(int j=0;j<2;++j){auto& o=q.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.wave=0;o.fb=0;}
   x->pool_[0].noteOn(q,317,gain);x->pool_[0].channel=0;x->poolPanLeft_[0]=.7f;x->poolPanRight_[0]=.3f;
   x->events_={{0,MaPlayer::Ev::ScoreNop,0,0,0},{1,MaPlayer::Ev::AllSoundOff,0,0,0}};x->nextEvent_=0;x->nonAtrGateEventEnd_=2;x->scoreEndSample_=1;x->endSample_=8001;
  }
  const auto r=analyzePlayers(p,b,512,17);require(r.finished&&r.frames>=uint64_t(sine_output::Bus(factor).tailBoundFrames()+1),"quality audit lost final-frame/tiny FIR support");
  require(r.parts.at({0,-1,0}).voiceFrames[0]==1&&r.parts.at({0,-1,0}).sineFrames==1&&r.parts.at({0,-1,0}).output.rms()>0,"tail drain duplicated source or lost tiny component");
 }
 std::cout<<cases<<" mixed quality/late-part routes plus delayed hard caps and final-frame/tiny tails PASS\n";
}


void adaptiveRoutes(){size_t cases=0;
 const auto hz=[](int note){return 440*std::pow(2.,double(note-69)/12);};
 for(int graph:{0,3,4,5,6,7,106})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})for(int block:{1,17,512,1024}){
  MaPlayer p,b;for(auto* x:{&p,&b}){
   require(x->init(clockFile(),44100,false,{8,math,true}),"adaptive diagnostic init");
   FmVoicePatch q;q.modernTimbre=true;q.algorithm=uint8_t(graph==106?6:graph);q.fourOp=graph!=0;
   for(auto& o:q.ops){o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.ma35Multipliers=true;o.multi=1;}
   if(graph==7){q.ops[0].tl=18;q.ops[1].multi=7;q.ops[2].multi=5;q.ops[3].multi=2;q.ops[3].tl=24;}
   else if(graph==3){q.ops[0].multi=7;q.ops[2].ar=0;q.ops[3].multi=5;}
   else if(graph==4||graph==6){q.ops[1].ar=0;q.ops[2].multi=7;q.ops[3].multi=5;if(graph==6)q.ops[0].tl=18;}
   else if(graph==106){q.ops[0].tl=18;q.ops[1].multi=7;q.ops[1].tl=24;q.ops[2].multi=3;q.ops[2].tl=24;q.ops[3].multi=5;}
   else {q.ops[0].multi=7;q.ops[1].multi=5;if(graph==5){q.ops[2].multi=3;q.ops[2].tl=24;q.ops[3].multi=2;}}
   for(int i=0;i<3;++i){auto& v=x->pool_[i];const double f=hz(i==0?84:96);v.noteOn(q,f,.2f);v.setVolume(.2f);v.channel=i==1?1:0;x->poolBaseFreq_[i]=f;x->poolPanLeft_[i]=i==2?.8f:.3f;x->poolPanRight_[i]=i==2?.2f:.7f;}
   pcm(*x,0,120,1000);pcm(*x,1,120,2000,true,7);
   std::vector<MaPlayer::Ev> events{{128,MaPlayer::Ev::PitchBend,0,8191,0},{256,MaPlayer::Ev::PitchBend,0,0,0},{256,MaPlayer::Ev::AllSoundOff,120,0,0},{384,MaPlayer::Ev::AllSoundOff,0,0,0},{512,MaPlayer::Ev::AllSoundOff,1,0,0},{640,MaPlayer::Ev::NoteOn,16,69,100,1},{704,MaPlayer::Ev::AllSoundOff,16,0,0}};
   x->events_.insert(x->events_.begin(),events.begin(),events.end());x->nonAtrGateEventEnd_=x->events_.size();
  }
  const auto r=analyzePlayers(p,b,768,block);require(r.boostHighNotes&&r.frames==768&&!r.finished,"adaptive report policy/interval");
  const auto& a=r.parts.at({0,-1,0});const auto& high=r.parts.at({0,-1,1});
  require(a.voiceFrames[0]==768&&a.sineFrames==768&&a.densityFrames[2]==uint64_t(graph==106?0:128)&&a.densityFrames[3]==uint64_t(graph==106?768:640)&&a.boostedFrames==uint64_t(graph==106?768:640),"mixed-density part/latching counts");
  require(high.voiceFrames[0]==512&&high.boostedFrames==512&&high.densityFrames[3]==512,"high part source attribution");
  require(r.parts.at({0,-1,120}).voiceFrames[1]==256&&r.parts.at({1,7,120}).voiceFrames[3]==768,"adaptive mix changed score/ATR domains");
  require(r.parts.at({0,-1,16}).voiceFrames[0]==64&&r.sumErrorPeak<2e-5,"adaptive late part or contribution sum changed");++cases;
 }
 for(float gain:{1.f,1e-12f}){MaPlayer p,b;for(auto* x:{&p,&b}){
   require(x->init(clockFile(),44100,false,{8,FmSubframeMath::Hermite,true}),"adaptive tail init");
   FmVoicePatch q;q.modernTimbre=true;for(auto& o:q.ops){o.ar=15;o.dr=o.sr=o.sl=0;o.ma35Multipliers=true;}q.ops[0].multi=7;q.ops[1].multi=5;
   x->pool_[0].noteOn(q,hz(96),gain);x->pool_[0].channel=0;x->poolPanLeft_[0]=.7f;x->poolPanRight_[0]=.3f;
   x->events_={{1,MaPlayer::Ev::AllSoundOff,0,0,0}};x->nextEvent_=0;x->nonAtrGateEventEnd_=1;x->scoreEndSample_=1;x->endSample_=44101;
  }
  const auto r=analyzePlayers(p,b,1024,17);const auto& a=r.parts.at({0,-1,0});
  require(r.finished&&r.frames>=144&&a.sineFrames==1&&a.boostedFrames==1&&a.output.rms()>0,"adaptive final-frame/tiny tail count or signal lost");
 }
 {MaPlayer p,b;for(auto* x:{&p,&b}){
   require(x->init(clockFile(),44100,false,{8,FmSubframeMath::Hermite,true}),"adaptive limiter init");
   FmVoicePatch q;q.modernTimbre=true;for(auto& o:q.ops){o.ar=15;o.dr=o.sr=o.sl=0;o.ma35Multipliers=true;}q.ops[0].multi=7;q.ops[1].multi=5;
   x->pool_[0].noteOn(q,hz(96),.2f);x->pool_[0].channel=0;x->poolPanLeft_[0]=.7f;x->poolPanRight_[0]=.3f;
   for(int slot=0;slot<32;++slot)pcm(*x,slot,2,32767);
  }
  const auto r=analyzePlayers(p,b,192,17);
  require(r.totals[11]>0&&r.parts.at({0,-1,0}).boostedFrames==192,"adaptive shared limiter did not run");
  require(r.parts.at({0,-1,2}).source.peak>10&&r.parts.at({0,-1,2}).output.peak<1&&r.sumErrorPeak<2e-6,"adaptive contributions did not retain actual shared limiting");
 }
 std::cout<<cases<<" adaptive mixed-density/late-part/source-counter cases and tiny tails PASS\n";
}

void waveMatrix(){size_t cases=0;
 for(bool modern:{false,true})for(int algo=0;algo<8;++algo)for(int wave=0;wave<32;++wave){MaPlayer p,b;init(p);init(b);
  FmVoicePatch q;q.modernTimbre=modern;q.algorithm=uint8_t(algo);q.fourOp=algo>=2;q.lfo=2;
  for(int j=0;j<4;++j){auto&o=q.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(j%3+1);o.wave=uint8_t(wave);o.tl=uint8_t(j*4);o.fb=uint8_t(j==0?wave%8:0);o.am=o.vib=true;o.dam=o.dvb=2;}
  for(auto* x:{&p,&b}){x->pool_[0].noteOn(q,317,.6f);x->pool_[0].channel=0;x->poolPanLeft_[0]=.7f;x->poolPanRight_[0]=.3f;}
  const auto r=analyzePlayers(p,b,64,cases%2?17:512);require(r.frames==64&&r.sumErrorPeak==0,"single FM contribution differs from actual output");++cases;
 }
 std::cout<<cases<<" FM graph/wave/profile contribution streams match actual renderer PASS\n";
}
int main(){dcAndBlocks();sharedLimiterAndCancellation();domainsAndTerminal();hardBoundaryAndGuards();triggerKinds();qualityRoutes();adaptiveRoutes();waveMatrix();}
