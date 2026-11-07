// SPDX-License-Identifier: Apache-2.0
// Single-thread host measurements, not Android performance or process RSS.
#include "fm-parallel-pair-probe.h"
#include "fm-render-bus-prototype.h"
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
namespace meter {
size_t live=0,allocations=0;
struct alignas(std::max_align_t) Header {size_t bytes;};
}
void* operator new(size_t n){if(n>std::numeric_limits<size_t>::max()-sizeof(meter::Header))throw std::bad_alloc();
    auto* h=static_cast<meter::Header*>(std::malloc(n+sizeof(meter::Header)));if(!h)throw std::bad_alloc();h->bytes=n;meter::live+=n;++meter::allocations;return h+1;}
void operator delete(void* p)noexcept{if(p){auto*h=static_cast<meter::Header*>(p)-1;meter::live-=h->bytes;std::free(h);}}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
using namespace fm_causal;
using namespace fxchain::smaf;
int main(){std::cout<<std::setprecision(12)<<"{\"scope\":\"Single-thread host O2, requested ordinary C++ heap bytes (not RSS); render includes native-clock FM diagnostic probe, keyed PCM and independent input; no Android/battery claim\",\"memory\":[";
    bool first=true;double checksum=0;
    for(int factor:{8,32})for(int count:{1,8,32,128})for(bool share:{false,true}){
        std::vector<int> ids(size_t(count),0);std::iota(ids.begin(),ids.end(),0);const auto baseline=meter::live;const auto start=std::chrono::steady_clock::now();size_t bytes=0;
        if(share){RenderBusPrototype bank(factor,ids);bytes=meter::live-baseline;}
        else{std::vector<TimedStopPrototype> bank;bank.reserve(size_t(count));for(int ch=0;ch<count;++ch)bank.emplace_back(factor);bytes=meter::live-baseline;}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(!first)std::cout<<',';first=false;std::cout<<"{\"factor\":"<<factor<<",\"channels\":"<<count<<",\"shared\":"<<(share?"true":"false")<<",\"heap_bytes\":"<<bytes<<",\"setup_and_destroy_ms\":"<<ms<<'}';
    }
    std::cout<<"],\"frames\":2048,\"render\":[";first=true;
    for(int repeat=0;repeat<3;++repeat)for(int factor:{8,32})for(int count:{1,8,32})for(int mode:{0,1,2})for(int algorithm:{0,5}){
        std::vector<int> ids(size_t(count),0);std::iota(ids.begin(),ids.end(),0);RenderBusPrototype bank(factor,ids);
        std::vector<FmVoice> voices(static_cast<size_t>(count));std::vector<std::array<Stereo,32>> high(static_cast<size_t>(count));std::vector<RenderChannelInput> input(static_cast<size_t>(count));
        FmVoicePatch patch;patch.modernTimbre=true;patch.algorithm=uint8_t(algorithm);patch.fourOp=algorithm==5;patch.lfo=2;
        const int multis[4]={3,2,7,5};for(int j=0;j<4;++j){auto&o=patch.ops[j];o.wave=0;o.multi=uint8_t(multis[j]);o.tl=uint8_t(j%2?0:24);o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.am=o.vib=true;o.dam=o.dvb=2;}
        for(int ch=0;ch<count;++ch){voices[size_t(ch)].setSampleRate(44100);if(mode!=0)voices[size_t(ch)].noteOn(patch,220+9*ch,.6f);}
        const auto allocations=meter::allocations;const auto start=std::chrono::steady_clock::now();
        for(int n=0;n<2048;++n){for(int ch=0;ch<count;++ch){auto&v=voices[size_t(ch)];if(mode==2)v.noteOn(patch,220+9*ch,.6f);
                std::array<float,32> samples{};if(v.active()){if(algorithm==5)fm_pairs::frame(v,std::span<float>(samples.data(),size_t(factor)));else fm_probe::frame(v,std::span<float>(samples.data(),size_t(factor)));}
                for(int k=0;k<factor;++k)high[size_t(ch)][size_t(k)]={samples[size_t(k)]*.7,samples[size_t(k)]*.3};
                input[size_t(ch)]={ch,std::span<const Stereo>(high[size_t(ch)].data(),size_t(factor)),mode==0?Stereo{}:Stereo{.005*std::sin(n*.17+ch),-.003},mode==2};}
            auto y=bank.frame(input,mode==0?Stereo{}:Stereo{.01*std::sin(n*.05),.005});checksum+=y.left+y.right;}
        const auto newAllocations=meter::allocations-allocations;const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(!first)std::cout<<',';first=false;std::cout<<"{\"repeat\":"<<repeat<<",\"factor\":"<<factor<<",\"channels\":"<<count<<",\"algorithm\":"<<algorithm<<",\"mode\":\""<<(mode==0?"idle":mode==1?"steady":"dense_stop_retrigger")<<"\",\"ms\":"<<ms<<",\"frame_allocations\":"<<newAllocations<<'}';
    }
    std::cout<<"],\"checksum\":"<<checksum<<"}\n";
}
