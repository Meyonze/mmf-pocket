// SPDX-License-Identifier: Apache-2.0
#include "fm-native-clock-probe.h"
#include "fm-channel-bus-prototype.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace fm_causal;
void require(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
std::vector<double> impulse(const Config& c){
    std::vector<double> h{1};size_t stride=1;
    for(size_t stage=0;stage<c.taps.size();++stage){const auto k=kernel(c.taps[stage]);
        std::vector<double> next(h.size()+(k.size()-1)*stride,0.);
        for(size_t a=0;a<h.size();++a)for(size_t b=0;b<k.size();++b)next[a+b*stride]+=h[a]*k[b];
        h=std::move(next);stride*=2;}return h;
}
void timelines(){
    constexpr int frames=600,inputFrames=430;const std::array<int,3> ids{0,16,127};size_t comparisons=0;
    for(int factor:{2,4,8,16,32}){
        const auto config=alignedConfig(factor);const auto h=impulse(config);const int padding=72-int(Decimator(config).delayFrames());
        std::array<std::vector<Stereo>,3> high,native;
        for(size_t ch=0;ch<3;++ch){high[ch].resize(size_t(frames*factor));native[ch].resize(frames);
            for(int n=0;n<inputFrames;++n){
                if(n>=24&&n<430)native[ch][size_t(n)]={.03*std::sin((n+ch)*.17),-.02*std::cos(n*.11+ch)};
                for(int j=0;j<factor;++j)if(n>=12&&!(n>=180&&n<220)){
                    double t=n+double(j+1)/factor;high[ch][size_t(n*factor+j)]={.2*std::sin(t*(.23+.07*ch)),.1*std::cos(t*(.31+.03*ch))};}}
        }
        for(int block:{1,7,64,257}){
            ChannelBusPrototype mixer(factor,ids);std::array<int,3> since{};
            bool retained=false,idled=false;
            for(int begin=0;begin<frames;begin+=block)for(int n=begin;n<std::min(frames,begin+block);++n){
                if(n==150||n==151||n==315){mixer.purge(0);since[0]=n;}
                if(n==200){mixer.purge(16);since[1]=n;}
                if(n==240){mixer.reset();since.fill(n);}
                std::array<ChannelInput,3> inputs;
                for(size_t ch=0;ch<3;++ch)inputs[ch]={ids[ch],std::span<const Stereo>(high[ch].data()+n*factor,size_t(factor)),native[ch][size_t(n)]};
                const auto y=mixer.frame(inputs);Stereo expected{};
                for(size_t ch=0;ch<3;++ch){
                    const int latest=(n-padding+1)*factor-1;
                    for(size_t k=0;k<h.size();++k){const int p=latest-int(k);
                        if(p>=since[ch]*factor){expected.left+=high[ch][size_t(p)].left*h[k];expected.right+=high[ch][size_t(p)].right*h[k];}}
                    if(n-72>=since[ch]){expected.left+=native[ch][size_t(n-72)].left;expected.right+=native[ch][size_t(n-72)].right;}}
                require(std::abs(y.left-expected.left)<2e-13&&std::abs(y.right-expected.right)<2e-13,"channel/reset/EOF timeline disagrees with dense convolution");++comparisons;
                if(n==inputFrames)retained=mixer.activeChannels()==3&&mixer.pendingFrames()>0&&(y.left!=0||y.right!=0);
                if(n>=inputFrames+144){require(y.left==0&&y.right==0&&mixer.activeChannels()==0&&mixer.pendingFrames()==0,"idle channel must drain exactly before sleeping");idled=true;}
            }
            require(retained&&idled,"must retain tails then sleep");
        }
        // Starting after a long idle must equal a freshly constructed pipeline.
        ChannelBusPrototype idle(factor,ids),fresh(factor,ids);for(int n=0;n<1000;++n){auto y=idle.frame({});require(y.left==0&&y.right==0,"idle output");}
        for(int n=0;n<200;++n){ChannelInput x{127,std::span<const Stereo>(high[2].data()+n*factor,size_t(factor)),native[2][size_t(n)]};
            auto a=idle.frame(std::span<const ChannelInput>(&x,1)),b=fresh.frame(std::span<const ChannelInput>(&x,1));require(a.left==b.left&&a.right==b.right,"wake after idle changes phase or delay");}
    }
    std::cout<<comparisons<<" dense multichannel timeline comparisons PASS\n";
}
void isolation(){
    const std::array<int,2> ids{0,127};constexpr int factor=8;ChannelBusPrototype mixed(factor,ids);
    const std::array<int,1> survivorId{127};ChannelBusPrototype survivor(factor,survivorId);
    std::array<Stereo,factor> a,b;bool pending=false;
    for(int n=0;n<400;++n){for(int j=0;j<factor;++j){a[size_t(j)]={n<130?.3:0.,0};b[size_t(j)]={n<220?.2:0.,n<220?-.1:0.};}
        if(n==130)mixed.purge(0); // Target has pending FM and native PCM history.
        std::array<ChannelInput,2> in{{{0,a,n<130?Stereo{.1,.2}:Stereo{}},{127,b,{}}}};
        ChannelInput keep{127,b,{}};auto y=mixed.frame(in),z=survivor.frame(std::span<const ChannelInput>(&keep,1));
        if(n>=130)require(y.left==z.left&&y.right==z.right,"purging one channel must preserve survivor bit-exactly");
        if(n==221)pending=survivor.activeChannels()==1;
    }
    require(pending,"omitting sources must retain FIR tails");require(mixed.activeChannels()==0,"all channels eventually idle");
    std::cout<<"Channel purge preserves other channel bit-exactly PASS\n";
}
void invalidInputs(){
    const std::array<int,2> ids{0,127};constexpr int factor=4;std::array<Stereo,4> data;data.fill({.3,-.2});
    for(int kind=0;kind<7;++kind){ChannelBusPrototype a(factor,ids),b(factor,ids);ChannelInput valid{0,data,{.1,.2}};
        a.frame(std::span<const ChannelInput>(&valid,1));b.frame(std::span<const ChannelInput>(&valid,1));
        ChannelInput bad{127,data,{}};std::array<Stereo,4> corrupt=data;
        if(kind==0)bad.channel=-1;if(kind==1)bad.channel=128;if(kind==2)bad.channel=16;
        if(kind==3)bad.fm=std::span<const Stereo>(data.data(),3);
        if(kind==4)bad.channel=0;
        if(kind==5)bad.native.left=std::numeric_limits<double>::quiet_NaN();
        if(kind==6){corrupt[3].right=std::numeric_limits<double>::infinity();bad.fm=corrupt;}
        std::array<ChannelInput,2> batch{valid,bad};bool rejected=false;
        try{a.frame(batch);}catch(const std::runtime_error&){rejected=true;}require(rejected,"invalid batch accepted");
        for(int n=0;n<160;++n){auto x=a.frame({}),y=b.frame({});require(x.left==y.left&&x.right==y.right,"rejected batch advanced state");}
    }
    for(const auto& bad:std::vector<std::vector<int>>{{-1},{128},{0,0}}){bool rejected=false;
        try{ChannelBusPrototype x(factor,bad);}catch(const std::runtime_error&){rejected=true;}require(rejected,"invalid channel setup accepted");}
    ChannelBusPrototype empty(factor,{});require(empty.frame({}).left==0&&empty.activeChannels()==0,"empty configuration must be silent");
    std::cout<<"Invalid configuration/batch validation before mutation PASS\n";
}
void nativeOnlyAndTiny(){
    const std::array<int,1> ids{127};
    for(int factor:{2,4,8,16,32}){ChannelBusPrototype mixer(factor,ids);
        for(int n=0;n<200;++n){const ChannelInput x{127,{},n==0?Stereo{1e-100,-1e-100}:Stereo{}};
            auto y=mixer.frame(std::span<const ChannelInput>(&x,1));
            require(y.left==(n==72?1e-100:0)&&y.right==(n==72?-1e-100:0),"native-only tiny input must not be thresholded or rescheduled");}
        require(mixer.activeChannels()==0,"native-only path drains");}
    std::cout<<"PCM-only input and tiny nonzero wakeup PASS\n";
}
void realSourceLifecycle(){
    using namespace fxchain::smaf;
    constexpr int factor=8;const std::array<int,2> ids{0,127};ChannelBusPrototype mixer(factor,ids);
    AlignedBus survivor(factor),restarted(factor);std::array<FmVoice,3> voices;
    FmVoicePatch p;p.modernTimbre=true;p.algorithm=0;p.lfo=2;
    for(int j=0;j<2;++j){auto&o=p.ops[j];o.wave=0;o.multi=uint8_t(j?2:3);o.tl=uint8_t(j?0:24);
        o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.holdEnabled=true;o.am=o.vib=true;o.dam=o.dvb=2;}
    for(int j=0;j<3;++j){voices[size_t(j)].setSampleRate(44100);voices[size_t(j)].noteOn(p,317+103*j,.6f);}
    bool held=false,tail=false;
    for(int n=0;n<600;++n){
        if(n==60){voices[0].noteOff(true);voices[1].noteOff(true);}
        if(n==79)held=voices[0].active()&&!voices[0].released()&&voices[1].active()&&!voices[1].released();
        if(n==80){mixer.purge(0);for(int j=0;j<2;++j){voices[size_t(j)]=FmVoice{};voices[size_t(j)].setSampleRate(44100);}}
        if(n==100)for(int j=0;j<2;++j)voices[size_t(j)].noteOn(p,503+71*j,.5f);
        if(n==200)for(int j=0;j<2;++j)voices[size_t(j)].noteOff(false);
        if(n==220)voices[2].noteOff(false);
        std::array<Stereo,factor> target{},keep{};std::array<float,factor> sub{};
        for(int j=0;j<3;++j){fm_probe::frame(voices[size_t(j)],sub);auto& dst=j==2?keep:target;
            for(int k=0;k<factor;++k){dst[size_t(k)].left+=sub[size_t(k)]*.7;dst[size_t(k)].right+=sub[size_t(k)]*.3;}}
        Stereo pcm=(n<80||(n>=100&&n<180))?Stereo{.1,-.03}:Stereo{};
        const std::array<ChannelInput,2> batch{{{0,target,pcm},{127,keep,{}}}};
        auto actual=mixer.frame(batch),other=survivor.frame(keep.data(),factor,{});
        if(n>=80){const auto own=restarted.frame(target.data(),factor,pcm);
            require(actual.left==own.left+other.left&&actual.right==own.right+other.right,"held voices/PCM purge and retrigger disturb independent survivor");}
        if(n>280&&!voices[2].active()&&(actual.left!=0||actual.right!=0))tail=true;
    }
    require(held&&tail,"real sources preserve Hold then drain after release");require(mixer.activeChannels()==0,"real source EOF drains");
    std::cout<<"Real FM Hold/purge/retrigger with PCM and surviving channel PASS\n";
}
int main(){timelines();isolation();invalidInputs();nativeOnlyAndTiny();realSourceLifecycle();}
