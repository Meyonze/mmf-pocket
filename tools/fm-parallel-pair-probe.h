// SPDX-License-Identifier: Apache-2.0
// Host-only native-clock source for algorithm5: two independent sine PM pairs.
#pragma once
#include "fm-native-clock-probe.h"
namespace fm_pairs {
using namespace fxchain::smaf;
inline bool eligible(const FmVoice& v){
    if(!v.fourOp_||v.algo_!=5)return false;
    for(int j=0;j<4;++j)if(v.patch_.ops[size_t(j)].wave!=0||v.opFb_[j]!=0)return false;
    return true;
}
template<bool Fast> inline fm_probe::Frame frameImpl(FmVoice& v,std::span<float> output){
    if(!eligible(v))throw std::runtime_error("pair probe requires algorithm5 sine operators without feedback");
    const size_t factor=output.size();if(factor<2||factor>32||(factor&(factor-1)))throw std::runtime_error("invalid pair subframe count");
    const bool active=v.active_;double before[4];for(int j=0;j<4;++j)before[j]=v.ops_[j].phase_;
    fm_probe::Frame result;result.native=v.tick();
    if(!active){std::fill(output.begin(),output.end(),0);return result;}
    double inc[4];float gain[4],am[4];bool mute[4];
    for(int j=0;j<4;++j){const auto&o=v.ops_[j];inc[j]=o.phaseInc_;if(o.vibAmt_!=0)inc[j]*=1+o.vibAmt_*v.lfoSin_*v.vibratoScale_;
        mute[j]=o.modernTimbre_&&o.vibAmt_!=0?inc[j]>=.5:o.cachedMute_;
        gain[j]=float(o.env_.level_);am[j]=o.amAmt_!=0?1-o.amAmt_*float(.5+.5*v.lfoSin_):1;}
    auto sample=[&](size_t sub){float values[4]{};
        for(int j=0;j<4;++j){if(mute[j])continue;const auto&o=v.ops_[j];double phase=sub==factor?o.phase_:before[j]+inc[j]*double(sub)/factor;
            if(phase>=1)phase-=std::floor(phase);if(j==1||j==3)phase+=double(values[j-1])*v.modDepth_;
            const float sine=fm_probe_math::sample<Fast>(phase,sub==factor);
            values[j]=float(sine*o.tlGain_*o.kslGain_*gain[j]*am[j]);}
        const double combined=(double(values[1])+double(values[3]))*.5;
        return float(combined*v.velocity_*v.volume_*.7);};
    for(size_t j=0;j+1<factor;++j)output[j]=sample(j+1);result.reconstructedEndpoint=sample(factor);output.back()=result.native;return result;
}
inline fm_probe::Frame frame(FmVoice& v,std::span<float> out){return frameImpl<false>(v,out);}
inline fm_probe::Frame frameFast(FmVoice& v,std::span<float> out){return frameImpl<true>(v,out);}
} // namespace fm_pairs
