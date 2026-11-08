// SPDX-License-Identifier: Apache-2.0
// Host prototype source+mix+shared FIR cost. Not app/device performance.
#include "fm-high-density-probe.h"
#include "fm-recursive-nested-probe.h"
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>
int main(int argc,char** argv) {
    if(argc!=3&&argc!=4)return 2;
    const bool recursive=argc==4;
    if(recursive&&std::string_view(argv[3])!="--recursive")return 2;
    const int factor=std::atoi(argv[1]),count=std::atoi(argv[2]);
    if((factor!=16&&factor!=32&&factor!=64&&factor!=128)||count<1||count>32)return 2;
    using namespace fxchain::smaf;
    constexpr int frames=4096;
    FmVoicePatch p;p.fourOp=true;p.algorithm=6;p.modernTimbre=true;
    const int mult[]={1,7,3,5},tl[]={18,12,12,0};
    for(int j=0;j<4;++j){auto& o=p.ops[size_t(j)];o.ma35Multipliers=true;o.ar=15;o.dr=o.sr=o.sl=0;o.rr=15;o.multi=uint8_t(mult[j]);o.tl=uint8_t(tl[j]);}
    std::vector<FmVoice> voices(size_t(count),FmVoice{});
    for(int j=0;j<count;++j){voices[size_t(j)].setSampleRate(44100);voices[size_t(j)].noteOn(p,220+13*j,.1f);}
    std::unique_ptr<sine_output::Bus> ordinary;
    std::unique_ptr<fm_high_density::Bus> high;
    if(factor<=32)ordinary=std::make_unique<sine_output::Bus>(factor);
    else high=std::make_unique<fm_high_density::Bus>(factor);
    std::array<float,128> source{};std::array<sine_output::Stereo,128> mix{};
    (void)fm_probe_math::sample<true>(.125,false);
    double checksum=0;const auto start=std::clock();
    for(int n=0;n<frames;++n) {
        std::fill_n(mix.begin(),factor,sine_output::Stereo{});
        for(auto& v:voices) {
            if(recursive)fm_recursive::frame(v,std::span<float>(source.data(),size_t(factor)));
            else fm_nested::frame<true,128>(v,std::span<float>(source.data(),size_t(factor)));
            for(int k=0;k<factor;++k){mix[size_t(k)].left+=source[size_t(k)]*.32;mix[size_t(k)].right+=source[size_t(k)]*.16;}
        }
        const std::span<const sine_output::Stereo> input(mix.data(),size_t(factor));
        const auto y=ordinary?ordinary->frame(input,{}):high->frame(input);
        checksum+=y.left+y.right;
    }
    const double milliseconds=1000.*(std::clock()-start)/CLOCKS_PER_SEC;
    if(!std::isfinite(checksum))return 1;
    std::cout<<std::setprecision(12)<<"{\"scope\":\"Host prototype source+mix+shared stereo FIR; excludes LP/limiter/JNI/I/O; not production or Android performance\",\"recursive\":"<<(recursive?"true":"false")<<",\"factor\":"<<factor<<",\"voices\":"<<count<<",\"frames\":"<<frames<<",\"audio_ms\":"<<1000.*frames/44100<<",\"cpu_ms\":"<<milliseconds<<",\"checksum\":"<<checksum<<"}\n";
}
