// SPDX-License-Identifier: Apache-2.0
#include "pcm_silence.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
using namespace fxchain::smaf::pcm_detail;
void require(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
int main(){
    uint64_t cases=0;SilenceIndex index;
    for(size_t n:{size_t(0),size_t(1),size_t(7),size_t(63),size_t(64),size_t(65),size_t(127),size_t(128),size_t(129),size_t(257),size_t(513),size_t(65536)})
    for(int pattern=0;pattern<6;++pattern){
        std::vector<int16_t> pcm(n,0);uint32_t seed=0x4d4d4600;
        for(size_t i=0;i<n;++i){seed=seed*1664525u+1013904223u;
            if(pattern==1|| (pattern==2&&i==0)||(pattern==3&&i+1==n)||(pattern==4&&i%61==0)||(pattern==5&&(seed&31)==0))
                pcm[i]=int16_t(pattern==1?1:-1);}
        index.build(pcm);
        auto check=[&](size_t first,size_t last){const bool expected=std::all_of(pcm.begin()+first,pcm.begin()+last,[](int16_t x){return x==0;});
            require(index.zero(pcm,first,last)==expected,"cached range query matches independent full sample scan");++cases;};
        if(n<=257){for(size_t first=0;first<=n;++first)for(size_t last=first;last<=n;++last)check(first,last);}
        else {std::vector<size_t> edges={0,1,62,63,64,65,126,127,128,129,n-1,n};
            for(size_t first:edges)for(size_t last:edges)if(first<=last&&last<=n)check(first,last);
            for(int i=0;i<1000;++i){seed=seed*1664525u+1013904223u;size_t a=seed%(n+1);seed=seed*1664525u+1013904223u;size_t b=seed%(n+1);
                check(std::min(a,b),std::max(a,b));}}
        require(!index.zero(pcm,0,n+1)&&!index.zero(pcm,1,0)&&!index.zero(pcm,0,std::numeric_limits<size_t>::max()),"invalid range rejected before indexing");
        auto copied=index;auto moved=std::move(copied);require(moved.zero(pcm,0,n)==index.zero(pcm,0,n),"index has value semantics without sample pointers");
        require(copied.zero(pcm,n/4,3*n/4)==index.zero(pcm,n/4,3*n/4),"moved-from index retains a safe scan fallback");
    }
    SilenceIndex unbuilt;std::vector<int16_t> source={0,0,1,0};
    require(unbuilt.zero(source,0,2)&&!unbuilt.zero(source,0,4),"unprepared source uses safe scan");
    source[2]=0;unbuilt.build(source);require(unbuilt.zero(source,0,4),"rebuild to all zeros clears occupied blocks");
    source[0]=1;unbuilt.build(source);require(!unbuilt.zero(source,0,4)&&unbuilt.zero(source,1,4),"same-size overwrite rebuilds metadata");
    std::cout<<"PCM silence index PASS: "<<cases<<" ranges, boundaries, sparse data, rebuilds and value semantics\n";
}
