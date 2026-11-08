// SPDX-License-Identifier: Apache-2.0
// Host-only64/128x prototype. Does not extend production source/bus limits.
#pragma once
#include "fm-nested-sine-probe.h"
#include "fm-causal-decimator.h"
#include "ma_sine_output.h"
namespace fm_high_density {
inline fm_causal::Config config(int factor) {
    if(factor!=64&&factor!=128)throw std::invalid_argument("high-density probe requires64/128");
    fm_causal::Config c{factor,{}};
    for(int n=factor;n>1;n/=2)c.taps.push_back(n==2?257:n==factor/8?37:33);
    return c;
}
inline double delayFrames(const fm_causal::Config& c) {
    double delay=0;int stride=1;
    for(int taps:c.taps){delay+=double((taps-1)/2)*stride/c.factor;stride*=2;}
    return delay;
}
class Bus {
    int factor_;
    std::vector<fxchain::smaf::sine_output::StereoStage> stages_;
public:
    explicit Bus(int factor):factor_(factor) {
        const auto c=config(factor);
        if(delayFrames(c)!=72)throw std::runtime_error("high-density delay changed");
        for(int taps:c.taps)stages_.emplace_back(taps);
    }
    fxchain::smaf::sine_output::Stereo frame(std::span<const fxchain::smaf::sine_output::Stereo> input) {
        if(input.size()!=size_t(factor_))throw std::invalid_argument("high-density block size");
        fxchain::smaf::sine_output::Stereo output{};int count=0;
        for(auto value:input)for(size_t i=0;i<stages_.size();++i) {
            if(!stages_[i].push(value,value))break;
            if(i+1==stages_.size()){output=value;++count;}
        }
        if(count!=1)throw std::runtime_error("high-density output phase");
        return output;
    }
    void reset(){for(auto& stage:stages_)stage.reset();}
};
} // namespace fm_high_density
