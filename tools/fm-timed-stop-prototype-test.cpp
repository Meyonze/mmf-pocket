// SPDX-License-Identifier: Apache-2.0
#include "fm-native-clock-probe.h"
#include "fm-timed-stop-prototype.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace fm_causal;
void require(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
std::vector<double> dense(const Config& c){std::vector<double> h{1};size_t stride=1;
    for(int taps:c.taps){const auto k=kernel(taps);std::vector<double> next(h.size()+(k.size()-1)*stride,0.);
        for(size_t a=0;a<h.size();++a)for(size_t b=0;b<k.size();++b)next[a+b*stride]+=h[a]*k[b];
        h=std::move(next);stride*=2;}return h;}
bool event(int mode,int n){
    if(mode==1)return n==100;
    if(mode==2)return n==100||n==172||n==245;
    if(mode==3)return (n>=80&&n<240)||(n>=240&&n<340&&n%7==0)||n==600;
    return false;
}
void timelines(){constexpr int frames=720;size_t checks=0;
    for(int factor:{2,4,8,16,32}){const auto config=alignedConfig(factor);const auto h=dense(config);
        const int pad=72-int(Decimator(config).delayFrames());
        for(int mode=0;mode<4;++mode){std::vector<Stereo> high(size_t(frames*factor)),native(frames);
            for(int n=0;n<430;++n){if(n>=10)native[size_t(n)]={.02*std::cos(n*.11),.03*std::sin(n*.17)};
                for(int j=0;j<factor;++j)if(n>=20&&!(n>=350&&n<380)){double t=n+double(j+1)/factor;
                    high[size_t(n*factor+j)]={.2*std::sin(t*.37),-.1*std::cos(t*.29)};}}
            for(int block:{1,7,64,257}){TimedStopPrototype actual(factor);AlignedBus unchanged(factor);int cut=0,lastReset=0;uint64_t dueCount=0;
                for(int begin=0;begin<frames;begin+=block)for(int n=begin;n<std::min(frames,begin+block);++n){
                    if(mode==3&&n==280){actual.reset();cut=lastReset=n;dueCount=0;}
                    const int due=n-72;if(due>=lastReset&&event(mode,due)){cut=due;++dueCount;}
                    auto y=actual.frame(std::span<const Stereo>(high.data()+n*factor,size_t(factor)),native[size_t(n)],event(mode,n));
                    if(mode==0){const auto old=unchanged.frame(high.data()+n*factor,size_t(factor),native[size_t(n)]);
                        require(y.left==old.left&&y.right==old.right,"no-stop output changed from existing bus");}
                    Stereo expected{};const int latest=(n-pad+1)*factor-1;
                    for(size_t k=0;k<h.size();++k){int p=latest-int(k);if(p>=cut*factor){expected.left+=high[size_t(p)].left*h[k];expected.right+=high[size_t(p)].right*h[k];}}
                    if(n-72>=cut){expected.left+=native[size_t(n-72)].left;expected.right+=native[size_t(n-72)].right;}
                    require(std::abs(y.left-expected.left)<2e-13&&std::abs(y.right-expected.right)<2e-13,"timed stop/retrigger/reset disagrees with independent gated convolution");
                    require(actual.replayedFrames()==dueCount*73,"stop consumed at wrong frame or replay work not bounded");
                    require(actual.retainedInputFrames()==73,"history grew with stop count");++checks;
                    if(mode==3&&n==601)require(actual.pendingFrames()>0,"pending silent stop was forgotten before output boundary");
                }
                require(actual.pendingFrames()==0,"EOF leaves pending audio/control work");
            }
        }
    }
    std::cout<<checks<<" delayed-stop convolution comparisons PASS\n";
}
void nativeBoundaryAndRestart(){
    for(int factor:{2,4,8,16,32}){TimedStopPrototype bus(factor);
        for(int n=0;n<400;++n){Stereo x=n<100?Stereo{.1,-.1}:n==101?Stereo{1e-100,-1e-100}:Stereo{};
            auto y=bus.frame({},x,n==100);
            const double expected=n>=72&&n<172?.1:n==173?1e-100:0.;
            require(y.left==expected&&y.right==-expected,"native stop must occur at n+72 and retain immediate retrigger");
        }
        require(bus.pendingFrames()==0,"native stop did not drain");
    }
    std::cout<<"Exact delayed PCM stop and tiny immediate retrigger PASS\n";
}
void invalidInputAndReset(){constexpr int factor=8;
    for(int kind=0;kind<3;++kind){TimedStopPrototype a(factor),b(factor);std::array<Stereo,factor> data;data.fill({.1,-.1});
        for(int n=0;n<50;++n){a.frame(data,{},n==20);b.frame(data,{},n==20);}
        std::array<Stereo,factor> corrupt=data;std::span<const Stereo> input=corrupt;Stereo native{};
        if(kind==0)input=input.first(7);if(kind==1)corrupt[7].left=std::numeric_limits<double>::quiet_NaN();
        if(kind==2)native.right=std::numeric_limits<double>::infinity();bool rejected=false;
        try{a.frame(input,native,true);}catch(const std::runtime_error&){rejected=true;}require(rejected,"invalid timed input accepted");
        for(int n=0;n<200;++n){auto x=a.frame({}),y=b.frame({});require(x.left==y.left&&x.right==y.right&&a.pendingFrames()==b.pendingFrames(),"invalid frame mutated samples, stop queue, or clock");}
    }
    TimedStopPrototype dirty(factor),fresh(factor);std::array<Stereo,factor> data;data.fill({.2,.3});
    for(int n=0;n<20;++n)dirty.frame(data,{},n==19);dirty.reset();
    for(int n=0;n<200;++n){auto x=dirty.frame(n<30?std::span<const Stereo>(data):std::span<const Stereo>{}),y=fresh.frame(n<30?std::span<const Stereo>(data):std::span<const Stereo>{});
        require(x.left==y.left&&x.right==y.right&&dirty.replayedFrames()==0,"seek reset retained audio or pending stop");}
    std::cout<<"Invalid batch atomicity and seek reset PASS\n";
}
void realVoices(){using namespace fxchain::smaf;constexpr int factor=8,frames=600;
    FmVoicePatch patch;patch.modernTimbre=true;patch.algorithm=0;patch.lfo=2;
    for(int j=0;j<2;++j){auto&p=patch.ops[j];p.wave=0;p.multi=uint8_t(j?2:3);p.tl=uint8_t(j?0:24);p.ar=15;p.dr=p.sr=p.sl=0;p.rr=15;
        p.holdEnabled=true;p.am=p.vib=true;p.dam=p.dvb=2;}
    FmVoice target,other;target.setSampleRate(44100);other.setSampleRate(44100);target.noteOn(patch,317,.7f);other.noteOn(patch,419,.6f);
    TimedStopPrototype timed(factor);AlignedBus survivor(factor);const auto h=dense(alignedConfig(factor));
    std::vector<Stereo> high(frames*factor),native(frames);std::array<float,factor> samples{};int cut=0;bool held=false,restarted=false;
    for(int n=0;n<frames;++n){bool stop=n==100||n==101||n==172;
        if(n==60)target.noteOff(true);if(n==99)held=target.active()&&!target.released();
        if(stop){target=FmVoice{};target.setSampleRate(44100);}
        // A later NoteOn on the same boundary survives that boundary's stop.
        if(n==101||n==102||n==172)target.noteOn(patch,503+n,.5f);
        if(n==230)target.noteOff(false);if(n==240)other.noteOff(false);
        fm_probe::frame(target,samples);for(int j=0;j<factor;++j)high[size_t(n*factor+j)]={samples[size_t(j)]*.7,samples[size_t(j)]*.3};
        if(n<100||(n>=101&&n<200))native[size_t(n)]={.03,-.02};
        std::array<Stereo,factor> keep{};fm_probe::frame(other,samples);for(int j=0;j<factor;++j)keep[size_t(j)]={samples[size_t(j)]*.3,samples[size_t(j)]*.7};
        const auto live=survivor.frame(keep.data(),factor,{});
        const auto y=timed.frame(std::span<const Stereo>(high.data()+n*factor,size_t(factor)),native[size_t(n)],stop);
        if(n==172||n==173||n==244)cut=n-72;
        Stereo expected{};const int latest=(n-2+1)*factor-1;
        for(size_t k=0;k<h.size();++k){int p=latest-int(k);if(p>=cut*factor){expected.left+=high[size_t(p)].left*h[k];expected.right+=high[size_t(p)].right*h[k];}}
        if(n-72>=cut){expected.left+=native[size_t(n-72)].left;expected.right+=native[size_t(n-72)].right;}
        require(std::abs((y.left+live.left)-(expected.left+live.left))<2e-13&&std::abs(y.right-expected.right)<2e-13,"real Hold/stop/retrigger with surviving channel disagrees with oracle");
        if(n==173)restarted=std::abs(y.left)>1e-5;
    }
    require(held&&restarted,"lost held old note or retriggered new note");require(timed.pendingFrames()==0,"real note EOF incomplete");
    std::cout<<"Real FM Hold, consecutive stops, same-frame NoteOn, PCM and other channel PASS\n";
}
int main(){timelines();nativeBoundaryAndRestart();invalidInputAndReset();realVoices();}
