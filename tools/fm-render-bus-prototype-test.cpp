// SPDX-License-Identifier: Apache-2.0
#include "fm-native-clock-probe.h"
#include "fm-render-bus-prototype.h"
#include "fm-replay-stop-reference.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace fm_causal;
void require(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
std::vector<double> dense(const Config& c){std::vector<double> h{1};size_t stride=1;
    for(int taps:c.taps){auto k=kernel(taps);std::vector<double> next(h.size()+(k.size()-1)*stride,0.);
        for(size_t a=0;a<h.size();++a)for(size_t b=0;b<k.size();++b)next[a+b*stride]+=h[a]*k[b];h=std::move(next);stride*=2;}return h;}
bool stop(size_t ch,int n){if(ch==0)return (n>=40&&n<160)||n==620;if(ch==1)return n==70||n==95||n==160||n==320;return n>=100&&n<210&&n%3==0;}
void timelines(){constexpr int frames=720;const std::array<int,3> ids{0,16,127};size_t checks=0;
    for(int factor:{2,4,8,16,32}){const auto c=alignedConfig(factor);const auto h=dense(c);const int pad=72-int(Decimator(c).delayFrames());
        std::array<std::vector<Stereo>,3> high,native;std::vector<Stereo> independent(frames);
        for(size_t ch=0;ch<3;++ch){high[ch].resize(size_t(frames*factor));native[ch].resize(frames);
            for(int n=0;n<400;++n){native[ch][size_t(n)]={.03*std::sin(n*.11+ch),.02*std::cos(n*.17+ch)};
                for(int j=0;j<factor;++j){double t=n+double(j+1)/factor;high[ch][size_t(n*factor+j)]={.1*std::sin(t*(.2+ch*.07)),.08*std::cos(t*(.31+ch*.03))};}}}
        for(int n=0;n<420;++n)independent[size_t(n)]={.01*std::cos(n*.13),-.02*std::sin(n*.19)};
        for(int block:{1,7,64,257}){RenderBusPrototype bus(factor,ids);std::array<ReplayStopReference,3> refs{ReplayStopReference(factor),ReplayStopReference(factor),ReplayStopReference(factor)};
            std::array<int,3> cut{};int reset=0;
            for(int begin=0;begin<frames;begin+=block)for(int n=begin;n<std::min(frames,begin+block);++n){
                if(n==280){bus.reset();for(auto& ref:refs)ref.reset();cut.fill(n);reset=n;}
                std::array<RenderChannelInput,3> input;Stereo replay{};
                for(size_t ch=0;ch<3;++ch){input[ch]={ids[ch],std::span<const Stereo>(high[ch].data()+n*factor,size_t(factor)),native[ch][size_t(n)],stop(ch,n)};
                    auto y=refs[ch].frame(input[ch].fm,input[ch].native,input[ch].hardStop);replay.left+=y.left;replay.right+=y.right;
                    if(n-72>=reset&&stop(ch,n-72))cut[ch]=n-72;}
                // Input order must not alias channels or alter configured mix order.
                std::swap(input[0],input[2]);auto actual=bus.frame(input,independent[size_t(n)]);Stereo expected{};
                for(size_t ch=0;ch<3;++ch){const int latest=(n-pad+1)*factor-1;
                    for(size_t k=0;k<h.size();++k){const int p=latest-int(k);if(p>=cut[ch]*factor){expected.left+=high[ch][size_t(p)].left*h[k];expected.right+=high[ch][size_t(p)].right*h[k];}}
                    if(n-72>=cut[ch]){expected.left+=native[ch][size_t(n-72)].left;expected.right+=native[ch][size_t(n-72)].right;}}
                if(n-72>=reset){const auto x=independent[size_t(n-72)];expected.left+=x.left;expected.right+=x.right;replay.left+=x.left;replay.right+=x.right;}
                require(std::abs(actual.left-expected.left)<1e-12&&std::abs(actual.right-expected.right)<1e-12,"multichannel routing disagrees with gated convolution");
                require(std::abs(actual.left-replay.left)<1e-12&&std::abs(actual.right-replay.right)<1e-12,"multichannel routing disagrees with frozen replay");++checks;
                if(n==621)require(bus.pendingFrames()>0&&bus.activeChannels()==1,"sleep forgot pending channel stop");
            }
            require(bus.pendingFrames()==0&&bus.activeChannels()==0,"mixed EOF does not drain");}
    }
    std::cout<<checks<<" mixed-channel dense/replay comparisons PASS\n";
}
void isolationAndWake(){constexpr int factor=8;const std::array<int,2> ids{0,127};const std::array<int,1> one{127};RenderBusPrototype mixed(factor,ids),survivor(factor,one);
    std::array<Stereo,factor> a,b;for(int n=0;n<400;++n){a.fill(n<100?Stereo{.3,-.2}:Stereo{});b.fill(n<220?Stereo{.1,.2}:Stereo{});
        std::array<RenderChannelInput,2> batch{{{0,a,n<100?Stereo{.1,0}:Stereo{},n==100},{127,b,{}}}};
        RenderChannelInput keep{127,b,{}};Stereo independent=n<240?Stereo{-.03,.02}:Stereo{};
        auto x=mixed.frame(batch,independent),y=survivor.frame(std::span<const RenderChannelInput>(&keep,1),independent);
        if(n>=172)require(x.left==y.left&&x.right==y.right,"channel stop affected survivor or independent audio");}
    for(int n=0;n<1000;++n)mixed.frame({});RenderBusPrototype fresh(factor,ids);
    for(int n=0;n<200;++n){b.fill(n==10?Stereo{1e-100,-1e-100}:Stereo{});RenderChannelInput input{127,b,{}};
        auto x=mixed.frame(std::span<const RenderChannelInput>(&input,1)),y=fresh.frame(std::span<const RenderChannelInput>(&input,1));require(x.left==y.left&&x.right==y.right,"sleep/wake changed phase or lost tiny input");}
    std::cout<<"Scoped stop preserves surviving channel/independent source; idle wake is exact PASS\n";
}
void invalidBatch(){const std::array<int,2> ids{0,127};constexpr int factor=8;
    for(int kind=0;kind<8;++kind){RenderBusPrototype a(factor,ids),b(factor,ids);std::array<Stereo,factor> data;data.fill({.1,-.2});RenderChannelInput good{0,data,{},true};
        for(int n=0;n<20;++n){a.frame(std::span<const RenderChannelInput>(&good,1),{.2,.3});b.frame(std::span<const RenderChannelInput>(&good,1),{.2,.3});}
        auto corrupt=data;RenderChannelInput bad{127,corrupt,{}};Stereo independent{};
        if(kind==0)bad.channel=-1;if(kind==1)bad.channel=128;if(kind==2)bad.channel=16;if(kind==3)bad.channel=0;
        if(kind==4)bad.fm=bad.fm.first(7);if(kind==5)bad.native.left=std::numeric_limits<double>::quiet_NaN();
        if(kind==6)corrupt[7].left=std::numeric_limits<double>::infinity();if(kind==7)independent.left=std::numeric_limits<double>::infinity();
        std::array<RenderChannelInput,2> batch{good,bad};bool rejected=false;try{a.frame(batch,independent);}catch(const std::runtime_error&){rejected=true;}require(rejected,"bad mixed batch accepted");
        for(int n=0;n<200;++n){auto x=a.frame({}),y=b.frame({});require(x.left==y.left&&x.right==y.right&&a.pendingFrames()==b.pendingFrames(),"bad mixed batch advanced any channel/independent clock");}}
    auto shared=TimedStopPrototype::prepareKernel(8);TimedStopPrototype own(8),borrowed(8,shared);shared.reset();
    std::array<Stereo,8> samples;samples.fill({.1,-.2});
    for(int n=0;n<200;++n){const Stereo x=n<10?Stereo{.1,.2}:Stereo{};auto input=n<100?std::span<const Stereo>(samples):std::span<const Stereo>{};auto a=own.frame(input,x,n>=20&&n<40),b=borrowed.frame(input,x,n>=20&&n<40);require(a.left==b.left&&a.right==b.right,"shared kernel lifetime changed output");}
    bool rejected=false;try{TimedStopPrototype wrong(2,TimedStopPrototype::prepareKernel(8));}catch(const std::runtime_error&){rejected=true;}require(rejected,"mismatched shared kernel accepted");
    for(auto channels:std::vector<std::vector<int>>{{-1},{128},{0,0}}){rejected=false;try{RenderBusPrototype bad(8,channels);}catch(const std::runtime_error&){rejected=true;}require(rejected,"bad configured channels accepted");}
    RenderBusPrototype empty(8,{});require(empty.frame({}).left==0,"empty bank output");
    std::cout<<"Atomic mixed input validation and immutable shared-kernel lifetime PASS\n";
}
void realSources(){using namespace fxchain::smaf;const std::array<int,3> ids{0,16,127};const int owner[4]={0,0,1,2};size_t checks=0;
    for(int rate:{8000,44100})for(int factor:{2,8,32}){
        RenderBusPrototype bus(factor,ids);std::array<ReplayStopReference,3> ref{ReplayStopReference(factor),ReplayStopReference(factor),ReplayStopReference(factor)};
        std::array<FmVoice,4> voice,oracle;std::array<FmVoicePatch,4> patch;
        for(int j=0;j<4;++j){auto&p=patch[size_t(j)];p.modernTimbre=j!=2;p.algorithm=0;p.fourOp=j==3;p.feedback=j==1?3:0;p.lfo=2;
            for(auto&o:p.ops){o.wave=0;o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=2;o.tl=10;o.holdEnabled=true;o.am=o.vib=true;o.dam=o.dvb=2;}
            voice[size_t(j)].setSampleRate(rate);oracle[size_t(j)].setSampleRate(rate);voice[size_t(j)].noteOn(p,317+31*j,.6f);oracle[size_t(j)].noteOn(p,317+31*j,.6f);}
        std::array<Stereo,72> independentHistory{};size_t cursor=0;bool held=false,flipped=false;
        for(int n=0;n<600;++n){std::array<bool,3> stops{n==80||n==210,(n>=100&&n<111&&n%2==0)||n==230,n==250};
            auto events=[&](auto& sources){for(int j=0;j<4;++j){auto& v=sources[size_t(j)];
                if(n==60&&j<2)v.noteOff(true);
                if(stops[size_t(owner[j])]){v=FmVoice{};v.setSampleRate(rate);}
                if((n==80&&j==0)||(stops[1]&&n<111&&j==2))v.noteOn(patch[size_t(j)],503+31*j,.5f);
                if(n==160){v.setPitch(419+23*j);v.setVolume(.4f);}
                if(n==180&&j==3){auto p=patch[3];p.fourOp=false;v.noteOn(p,617,.5f);}
            }};events(voice);events(oracle);
            if(n==79)held=voice[0].active()&&!voice[0].released();if(n==180)flipped=fm_probe::eligible(voice[3]);
            if(n==300){bus.reset();for(auto&r:ref)r.reset();independentHistory.fill({});cursor=0;}
            std::array<std::array<Stereo,32>,3> high{};std::array<Stereo,3> native{};std::array<float,32> sub{};
            for(int j=0;j<4;++j){auto& v=voice[size_t(j)];const float expected=oracle[size_t(j)].tick();const size_t ch=size_t(owner[j]);
                if(fm_probe::eligible(v)){const auto r=fm_probe::frame(v,std::span<float>(sub.data(),size_t(factor)));require(r.native==expected,"eligible source native clock changed");
                    for(int k=0;k<factor;++k){high[ch][size_t(k)].left+=sub[size_t(k)]*.7;high[ch][size_t(k)].right+=sub[size_t(k)]*.3;}}
                else{const float x=v.tick();require(x==expected,"native fallback clock changed");native[ch].left+=x*.4;native[ch].right+=x*.6;}}
            if(n<230)native[1]={native[1].left+.02*std::sin(n*.19),native[1].right-.01*std::cos(n*.13)};
            Stereo independent=n<360?Stereo{.03*std::sin(n*.07),.02*std::cos(n*.17)}:Stereo{};
            std::array<RenderChannelInput,3> batch;Stereo expected{};
            for(size_t ch=0;ch<3;++ch){batch[ch]={ids[ch],std::span<const Stereo>(high[ch].data(),size_t(factor)),native[ch],stops[ch]};auto y=ref[ch].frame(batch[ch].fm,batch[ch].native,batch[ch].hardStop);expected.left+=y.left;expected.right+=y.right;}
            const auto delayed=independentHistory[cursor];independentHistory[cursor]=independent;cursor=(cursor+1)%72;expected.left+=delayed.left;expected.right+=delayed.right;
            const auto y=bus.frame(batch,independent);require(std::abs(y.left-expected.left)<1e-12&&std::abs(y.right-expected.right)<1e-12,"real mixed source routing differs from frozen replay");++checks;
        }
        require(held&&flipped&&bus.pendingFrames()==0,"Hold/eligibility transition/real-source EOF failed");
    }
    std::cout<<checks<<" real-source mixed FM/fallback/PCM/independent comparisons PASS\n";
}
int main(){timelines();isolationAndWake();invalidBatch();realSources();}
