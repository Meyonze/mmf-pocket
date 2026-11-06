// SPDX-License-Identifier: Apache-2.0
// Host-only bus timing comparison, excluding setup and FM synthesis.
#include "fm-timed-stop-prototype.h"
#include "fm-replay-stop-reference.h"
#include <chrono>
#include <iomanip>
#include <iostream>
using namespace fm_causal;
template<class Bus> void measure(const char* name,int repeat,int factor,int period,bool& first,double& checksum){
    Bus bus(factor);std::array<Stereo,32> data;
    for(int j=0;j<factor;++j)data[size_t(j)]={.1+.001*j,-.13};
    const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<4096;++n){auto y=bus.frame(std::span<const Stereo>(data.data(),size_t(factor)),{.1,-.1},period!=0&&n%period==0);checksum+=y.left+y.right;}
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(!first)std::cout<<',';first=false;
    std::cout<<"{\"implementation\":\""<<name<<"\",\"repeat\":"<<repeat<<",\"factor\":"<<factor<<",\"stop_period\":"<<period
        <<",\"ms\":"<<ms<<",\"replayed_frames\":"<<bus.replayedFrames()<<'}';
}
int main(){std::cout<<std::setprecision(12)<<"{\"scope\":\"Host O2 one bus, setup and FM synthesis excluded; no Android or battery claim\",\"frames\":4096,\"results\":[";
    bool first=true;double checksum=0;
    for(int repeat=0;repeat<5;++repeat)for(int factor:{8,32})for(int period:{0,256,1,2,3,4,5}){
        // Alternate order to reduce systematic warmup/load bias.
        if(repeat%2==0){measure<ReplayStopReference>("replay",repeat,factor,period,first,checksum);measure<TimedStopPrototype>("candidate",repeat,factor,period,first,checksum);}
        else{measure<TimedStopPrototype>("candidate",repeat,factor,period,first,checksum);measure<ReplayStopReference>("replay",repeat,factor,period,first,checksum);}
    }
    std::cout<<"],\"checksum\":"<<checksum<<"}\n";
}
