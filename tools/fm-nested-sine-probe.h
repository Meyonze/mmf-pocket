// SPDX-License-Identifier: Apache-2.0
// Host-only graph6 prototype. Does not enable app or public-player routing.
#pragma once
#include "fm-native-clock-probe.h"

namespace fm_nested {
using namespace fxchain::smaf;

inline bool eligible(const FmVoice& v) {
    if (!v.fourOp_ || v.algo_ != 6) return false;
    for (int j=0;j<4;++j)
        if (v.patch_.ops[size_t(j)].wave!=0 || v.opFb_[j]!=0) return false;
    return true;
}

template<bool Fast,size_t MaxFactor=32>
inline fm_probe::Frame frame(FmVoice& v, std::span<float> output) {
    static_assert(MaxFactor==32||MaxFactor==128);
    const size_t factor=output.size();
    if (!eligible(v) || !output.data() || factor<2 || factor>MaxFactor || (factor&(factor-1)))
        throw std::runtime_error("nested probe requires graph6 sine/no-feedback and a valid host factor");
    const bool active=v.active_;
    double before[4];
    for(int j=0;j<4;++j)before[j]=v.ops_[j].phase_;
    fm_probe::Frame result;
    result.native=v.tick();
    if (!active || v.velocity_==0 || v.volume_==0) {
        std::fill(output.begin(),output.end(),0);
        output.back()=result.reconstructedEndpoint=result.native;
        return result;
    }
    double inc[4];float level[4],am[4];bool mute[4];
    for(int j=0;j<4;++j) {
        const auto& o=v.ops_[j];inc[j]=o.phaseInc_;
        if(o.vibAmt_!=0)inc[j]*=1+o.vibAmt_*v.lfoSin_*v.vibratoScale_;
        mute[j]=o.modernTimbre_&&o.vibAmt_!=0?inc[j]>=.5:o.cachedMute_;
        level[j]=float(o.env_.level_);
        am[j]=o.amAmt_!=0?1-o.amAmt_*float(.5+.5*v.lfoSin_):1;
    }
    auto sample=[&](size_t sub) {
        float values[4]{};
        for(int j=0;j<4;++j) {
            if(mute[j])continue;
            const auto& o=v.ops_[j];
            double phase=sub==factor?o.phase_:before[j]+inc[j]*double(sub)/factor;
            if(phase>=1)phase-=std::floor(phase);
            if(j>=2)phase+=double(values[j-1])*v.modDepth_;
            const float sine=fm_probe_math::sample<Fast>(phase,sub==factor);
            values[j]=float(sine*o.tlGain_*o.kslGain_*level[j]*am[j]);
        }
        return float((double(values[0])+double(values[3]))*.5*v.velocity_*v.volume_*.7);
    };
    for(size_t sub=1;sub<factor;++sub)output[sub-1]=sample(sub);
    result.reconstructedEndpoint=sample(factor);
    output.back()=result.native;
    return result;
}
} // namespace fm_nested
