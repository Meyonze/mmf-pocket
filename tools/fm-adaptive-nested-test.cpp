// SPDX-License-Identifier: Apache-2.0
// Actual adaptive player: independent dense convolution across migrating banks.
#define main fixedNestedExistingMain
#include "fm-nested-player-test.cpp"
#undef main
#include "fm-nested-tail-bound.h"
#include "ma_nested_density.h"
namespace adaptive_nested {
using nested_player::require;
constexpr SineRenderOptions options{8,FmSubframeMath::Hermite,true,false,true};
void bounds(){
    int count=0;
    for(int a=0;a<4;++a)for(int b=0;b<4;++b)for(int i=0;i<4;++i){
        const int factor=16<<i;const double q=i==3?.5:nested_density::limits[a][b][i]/64.;
        const double tail=fm_nested_tail::independentBound(nested_density::betaBounds[a],nested_density::betaBounds[b],q,q,q,factor-.5);
        require(tail<1e-4,"nested stationary threshold certificate failed");
        require(nested_density::factor(q,12+4*a,12+4*b)<=factor,"nested exact threshold excluded");
        if(i<3&&q<.5)require(nested_density::factor(std::nextafter(q,1.),12+4*a,12+4*b)>factor,"nested upper threshold neighbor not promoted");++count;
    }
    for(int kind=0;kind<9;++kind){auto patch=nested::patch({96,7,3,5,12,12});
        if(kind==0)patch.ops[1].tl=11;if(kind==1)patch.ops[2].tl=11;
        if(kind==2)for(auto& op:patch.ops){op.ma7Multipliers=true;op.ma35Multipliers=false;}
        if(kind==3)for(auto& op:patch.ops)op.tl=24;
        if(kind==4)patch.ops[2].ar=0;if(kind==5)patch.ops[1].vib=true;
        if(kind==6)patch.ops[3].fb=1;if(kind==7)patch.ops[0].fixedFrequency=true;
        if(kind==8)patch.algorithm=5;
        FmVoice v;v.setSampleRate(44100);v.noteOn(patch,317,1);auto native=v;
        require(v.nestedSineDensityHint()==0&&v.tick()==native.tick(),"automatic nested admission changed unsupported/native clock");
    }
    FmVoice v;v.setSampleRate(44100);const auto patch=nested::patch({96,7,3,5,12,12});
    v.noteOn(patch,100,1);require(v.nestedSineDensityHint()==16,"nested low pitch density");
    for(auto [q,factor]:{std::pair{.08,32},std::pair{.16,64},std::pair{.32,128},std::pair{.01,128}}){v.setPitch(q*44100/7);require(v.nestedSineDensityHint()==factor,"nested pitch density latch");}
    v.noteOn(patch,100,1);require(v.nestedSineDensityHint()==16,"nested density did not reset on retrigger");
    std::cout<<count<<" stationary threshold certificates;9 admission guards;pitch latch/retrigger PASS\n";
}
void transitions(FmSubframeMath math){
    constexpr int frames=640;MaPlayer p;require(p.init(nested_player::clockFile(),44100,false,{8,math,true,false,true}),"adaptive actual player init");
    auto patch=nested::patch({96,7,3,5,12,12});FmVoice oracle;oracle.setSampleRate(44100);
    std::array<std::vector<double>,4> sources;for(int i=0;i<4;++i)sources[i].resize(size_t(frames*(16<<i)),0);
    std::vector<double> actual(frames);std::array<float,128> samples{};
    int factor=16;
    for(int n=0;n<frames;++n){
        for(auto* v:{&p.pool_[0],&oracle}){
            if(n==0||n==200){v->noteOn(patch,.04*44100/7,.4f);v->channel=0;factor=16;}
            if(n==17)v->setPitch(.08*44100/7);if(n==33||n==240)v->setPitch(.16*44100/7);
            if(n==65)v->setPitch(.32*44100/7);if(n==97)v->setPitch(.01*44100/7);
            if(n==45)v->setVolume(0);if(n==55)v->setVolume(1e-12f);if(n==57)v->setVolume(1);
            if(n==129)v->noteOff(true);if(n==145)v->setHold(false);
            if(n==160||n==320)v->active_=false;
        }
        p.poolPanLeft_[0]=1;p.poolPanRight_[0]=0;
        if(n==17)factor=32;if(n==33||n==240)factor=64;if(n==65)factor=128;
        if(oracle.active()){
            if(math==FmSubframeMath::Hermite)fm_recursive::frame(oracle,std::span<float>(samples.data(),size_t(factor)));
            else fm_nested::frame<false,128>(oracle,std::span<float>(samples.data(),size_t(factor)));
            const int bank=std::countr_zero(unsigned(factor))-4;
            for(int k=0;k<factor;++k)sources[size_t(bank)][size_t(n*factor+k)]=float(samples[size_t(k)]*.32f);
        }
        float out[2];require(p.render(out,1)==1&&out[1]==0,"adaptive transition render/mono pan");actual[size_t(n)]=out[0];
        require(p.pool_[0].active_==oracle.active_&&p.pool_[0].lfoPhase_==oracle.lfoPhase_,"adaptive transition native voice state");
        for(int j=0;j<4;++j)require(p.pool_[0].ops_[j].phase_==oracle.ops_[j].phase_&&p.pool_[0].ops_[j].env_.level_==oracle.ops_[j].env_.level_,"adaptive transition operator clock");
    }
    std::vector<double> filtered(frames,0);
    for(int i=0;i<4;++i){const int density=16<<i;const auto config=density>32?fm_high_density::config(density):fm_causal::alignedConfig(density);
        std::vector<double> h{1};int stride=1;
        for(int taps:config.taps){const auto stage=fm_causal::kernel(taps);std::vector<double> next(h.size()+(stage.size()-1)*stride);for(size_t x=0;x<h.size();++x)for(size_t y=0;y<stage.size();++y)next[x+y*stride]+=h[x]*stage[y];h=std::move(next);stride*=2;}
        const int padding=72-int((h.size()-1)/(2*density));h.insert(h.begin(),size_t(padding*density),0);
        for(int n=0;n<frames;++n){const int at=(n+1)*density-1;
            for(int k=0;k<int(h.size())&&k<=at;++k)filtered[size_t(n)]+=h[size_t(k)]*sources[size_t(i)][size_t(at-k)];
        }
    }
    float lp1=0,lp2=0;double peak=0;
    for(int n=0;n<frames;++n){lp1+=p.lpCoef_*(float(filtered[size_t(n)])-lp1);lp2+=p.lpCoef_*(lp1-lp2);peak=std::max(peak,std::abs(actual[size_t(n)]-lp2));}
    require(peak<2e-7,"adaptive bank transition erased old FIR history or changed dense convolution");
    require(!p.sineRender_->nestedBanks->pending()&&p.sineRender_->highBus->pendingFrames()==0,"adaptive old banks did not drain");
    require(p.diagnostics().limitedFrames==0,"adaptive reference clipped");
    std::cout<<"Actual16->32->64->128,descending pitch,retrigger/overlapping tails,mute/tiny revival/Hold: dense FIR max delta "<<peak<<" PASS\n";
}
void lifecycle(){
    auto file=nested::fixture({96,7,3,5,12,12});file.tracks[0].sequenceData.resize(27);file.tracks[0].sequenceData.insert(file.tracks[0].sequenceData.end(),{0,0x90,96,127,5,12,0xff,0x2f,0});
    MaPlayer a,b;require(a.init(file,44100,false,options)&&b.init(file,44100,false,options),"adaptive lifecycle init");
    const auto out=nested_player::drain(a,1);require(!out.empty()&&nested_player::drain(b,1024)==out,"adaptive block/EOF");a.seekToStart();require(nested_player::drain(a,17)==out,"adaptive seek bank state");
    a.seekToStart();require(!a.init(file,44100,false,{16,FmSubframeMath::Hermite,false,false,true}),"adaptive invalid config accepted");require(nested_player::drain(a,31)==out,"adaptive rejected init destroyed state");
    for(int cap:{1,17,441}){MaPlayer p;require(p.init(file,44100,false,options),"adaptive capped init");p.endSample_=cap;const auto pcm=nested_player::drain(p,31);require(pcm.size()==size_t(cap+72)*2&&pcm.back()==0&&pcm[pcm.size()-2]==0,"adaptive cap/fade length");}
    require(a.init(file,44100)&&!a.sineRender_,"adaptive reinit failed to restore ordinary renderer");
    std::cout<<"Actual adaptive EOF/block1,17,1024/seek/reinit/atomic invalid init/3hard caps PASS\n";
}
void legacy(){
    int count=0;
    for(int family=0;family<3;++family)for(int algorithm=0;algorithm<8;++algorithm){
        auto patch=nested::patch({84,3,2,5,family==0?24:0,family==0?24:0,family==1});patch.algorithm=algorithm;
        MaPlayer a,b;require(a.init(nested_player::clockFile(),44100,false,{8,FmSubframeMath::Hermite,true})&&b.init(nested_player::clockFile(),44100,false,options),"adaptive legacy init");
        for(auto* p:{&a,&b}){p->pool_[0].noteOn(patch,317,.3f);p->pool_[0].channel=0;p->poolPanLeft_[0]=.3f;p->poolPanRight_[0]=.7f;nested_player::pcm(*p,0,120,1000);}
        for(int n=0;n<400;++n){for(auto* p:{&a,&b}){if(n==17)p->pool_[0].setPitch(1417);if(n==33)p->pool_[0].setVolume(0);if(n==49)p->pool_[0].setVolume(1);if(n==129)p->pool_[0].noteOff(false);}
            float x[2],y[2];require(a.render(x,1)==1&&b.render(y,1)==1&&contribution::bits(x[0],y[0])&&contribution::bits(x[1],y[1]),"adaptive changed existing r91 class");}
        require(!b.sineRender_->nestedBanks->pending(),"legacy class incorrectly entered new banks");++count;
    }
    std::cout<<count<<" legacy family/graph policies with pitch/mute/release/PCM remain bit-exact PASS\n";
}
void mixed(){
    MaPlayer a,b;for(auto* p:{&a,&b}){require(p->init(nested_player::clockFile(),44100,false,options),"adaptive mixed init");
        for(int i=0;i<4;++i){auto patch=nested::patch({96,7,3,5,12,12});p->pool_[i].noteOn(patch,std::array{.04,.08,.16,.32}[i]*44100/7,.1f);p->pool_[i].channel=i;p->poolPanLeft_[i]=p->poolPanRight_[i]=.5f;}
        nested_player::pcm(*p,0,120,1000);nested_player::pcm(*p,1,121,2000,true,7);
        for(int i=0;i<4;++i)p->events_.insert(p->events_.begin(),{192,MaPlayer::Ev::AllSoundOff,uint16_t(i),0,0});
        std::stable_sort(p->events_.begin(),p->events_.end(),[](const auto& x,const auto& y){return x.sample<y.sample;});p->nonAtrGateEventEnd_=p->events_.size();
    }
    auto report=contribution::analyzePlayers(a,b,512,17);
    for(int i=0;i<4;++i){const auto& part=report.parts.at({0,-1,i});require(part.sineFrames==192&&part.densityFrames[size_t(i+3)]==192,"adaptive mixed per-bank counters");}
    require(report.adaptiveNested&&report.sumErrorPeak<2e-5,"adaptive contribution stream");
    std::cout<<"Actual simultaneous16/32/64/128+PCM/ATR contributions and block-exact reference PASS\n";
}
}
int main(){try{adaptive_nested::bounds();adaptive_nested::transitions(FmSubframeMath::Exact);adaptive_nested::transitions(FmSubframeMath::Hermite);adaptive_nested::lifecycle();adaptive_nested::legacy();adaptive_nested::mixed();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
