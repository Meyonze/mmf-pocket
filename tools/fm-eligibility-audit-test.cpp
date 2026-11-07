// SPDX-License-Identifier: Apache-2.0
#define FM_ELIGIBILITY_AUDIT_NO_MAIN
#include "fm-eligibility-audit.cpp"
#include <limits>
using namespace eligibility;
void require(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
SmafFile fixture(){SmafFile f;TrackChunk t;t.formatType=2;t.trackNumber=0;t.durationTimeBase=t.gateTimeBase=0;t.channelStatus.resize(16);
    t.sequenceData={0,0x90,60,100,10};std::vector<uint8_t> v{0x43,5,1,0,0,60,1,0};for(int j=0;j<2;++j)v.insert(v.end(),{0,0x90,0xf0,0,0,0x10,0});
    t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);f.tracks.push_back(t);return f;}
FmVoicePatch patch(){FmVoicePatch p;p.modernTimbre=true;p.algorithm=0;for(int i=0;i<4;++i){auto&o=p.ops[i];o.wave=0;o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(i?2:3);o.tl=uint8_t(i?0:24);o.holdEnabled=true;}return p;}
void model(){for(double x:{0.,.01,.1,1.,2.64,25.132741228718345,64.}){auto b=fm_model::sidebandTail(x);double actual=0;
        for(int n=b.order+1;n<256;++n)actual+=2*std::abs(std::cyl_bessel_j(double(n),x));
        require(b.bound<=fm_model::tailTarget&&actual<=b.bound*(1+1e-8)+1e-16,"Bessel coefficient tail exceeds geometric bound");}
    int previous=-1;for(int i=0;i<=256;++i){auto b=fm_model::sidebandTail(i*.25);require(b.order>=previous,"upper index order must be conservative");previous=b.order;}
    require(fm_model::factor(.125,.125,3)==2&&fm_model::factor(0,0,0)==1&&fm_model::factor(16,0,0)==0,"factor boundaries and beyond32 guard");
    for(double x:{-1.,65.,std::numeric_limits<double>::infinity()}){bool rejected=false;try{fm_model::sidebandTail(x);}catch(const std::runtime_error&){rejected=true;}require(rejected,"bad PM index accepted");}
    std::cout<<"Independent Bessel tail and factor-bound controls PASS\n";
}
void classify(){MaPlayer p;require(p.init(fixture(),rate),"classification init");
    for(int i=0;i<7;++i){auto q=patch();if(i==0)q.ops[0].ar=0;if(i==1)for(auto&o:q.ops){o.ma35Multipliers=true;o.vib=true;o.dvb=2;}
        if(i==2){q.fourOp=true;q.algorithm=4;}if(i==3)q.algorithm=1;if(i==4)q.ops[0].wave=4;if(i==5)q.feedback=2;if(i==6){q.fourOp=true;q.algorithm=5;}
        p.pool_[size_t(i)].setSampleRate(rate);p.pool_[size_t(i)].noteOn(q,440,.7f);p.pool_[size_t(i)].channel=i==1?16:0;}
    p.pool_[0].setVolume(0);CacheBank cache{};auto s=snapshot(p,cache);
    require(s.active==7&&s.eligible==2&&s.channels==2&&s.muted==1&&s.lfo==1,"live eligibility/liveness/channel classification");
    require(s.sineGraphs==5&&s.graph[0]==2&&s.graph[1]==1&&s.graph[4]==1,"no-feedback sine graph opportunity counts");
    require(s.reason==std::array<uint64_t,5>{2,2,1,1,1},"unsupported reasons must partition voice frames");
    uint64_t pairSum=0;for(auto n:s.pairFactor)pairSum+=n;require(s.pairs==1&&pairSum==1,"parallel pair planning remains separate from two-op eligibility");
    uint64_t sum=0;for(auto n:s.factor)sum+=n;require(sum==2,"factor groups must partition eligible voice frames");
    require(cache[0][0].index==0&&cache[0][0].tail.order==0,"permanently stopped modulator cannot create sidebands");
    require(s.maxIndex>0&&s.maxCarrier>0,"actual PM index and phase increment captured");
    std::cout<<"Eligibility, unsupported paths, muted revival and live channel counts PASS\n";
}
void install(MaPlayer& p){require(p.init(fixture(),rate),"parity init");p.voiceTable_[0].patch=patch();p.events_.clear();
    using E=MaPlayer::Ev;for(int i=0;i<40;++i)p.events_.push_back({0,E::NoteOn,0,int16_t(40+i),100,uint64_t(i+1)});
    p.events_.push_back({1,E::Volume,0,0,0});p.events_.push_back({2,E::Volume,0,100,0});p.events_.push_back({10,E::Hold,0,127,0});
    p.events_.push_back({32,E::PitchBend,0,4096,0});p.events_.push_back({33,E::NoteOff,0,79,0,40});
    p.events_.push_back({64,E::AllSoundOff,0,0,0});p.events_.push_back({64,E::NoteOn,16,60,100,41});p.events_.push_back({128,E::Hold,0,0,0});
    p.events_.push_back({200,E::AllSoundOff,16,0,0});p.events_.push_back({250,E::Volume,0,100,0});
    p.nextEvent_=0;p.nonAtrGateEventEnd_=p.events_.size();p.scoreEndSample_=250;p.endSample_=2000;
}
void parity(){for(uint64_t cap:{1,31,64,65,200,250,500}){MaPlayer a,b;install(a);install(b);auto r=measure(a,b,cap);
        require(r.peakFm==32&&r.peakEligible==32&&r.peakChannels==1,"pre-tick observer missed initial/polyphonic voices");
        require(r.fmNotes==(cap<=64?40:41)&&r.stolenFm==8,"same-frame steal or boundary event observation changed");
        if(cap==1)require(r.total.active==32&&r.total.eligible==32,"single-frame voices must be counted");
        if(cap==500)require(r.frames==250&&r.finished&&!r.capReached&&r.total.eligible==2184&&r.total.muted==32,"live duration/hold/zero-gain/EOF counts");
    }
    MaPlayer a,b;install(a);install(b);a.events_={{0,MaPlayer::Ev::ScoreNop,0,0,0}};b.events_=a.events_;a.nonAtrGateEventEnd_=b.nonAtrGateEventEnd_=1;
    auto r=measure(a,b,500);require(r.frames==0&&r.finished,"empty audible render observer");
    std::cout<<"Per-sample render parity, event bounds, Hold, steals and EOF PASS\n";
}
int main(){model();classify();parity();}
