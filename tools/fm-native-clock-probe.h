// SPDX-License-Identifier: Apache-2.0
// Host-only experimental sine source. Keep this include before ma_fm_core.h.
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#define private public
#include "ma_fm_core.h"
#undef private
#include "fm-subframe-sine.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
namespace fm_probe {
using namespace fxchain::smaf;
inline bool eligible(const FmVoice& v) {
    return !v.fourOp_&&v.algo_==0&&v.patch_.ops[0].wave==0&&v.patch_.ops[1].wave==0&&v.opFb_[0]==0&&v.opFb_[1]==0;
}
struct Frame {float native=0,reconstructedEndpoint=0;};
template<bool Fast> inline Frame frameImpl(FmVoice& v,std::span<float> out) {
    if(!eligible(v))throw std::runtime_error("probe only supports two sine operators without feedback");
    const size_t factor=out.size();if(factor<2||factor>32||(factor&(factor-1)))throw std::runtime_error("invalid subframe count");
    const bool active=v.active_;
    const double before[2]={v.ops_[0].phase_,v.ops_[1].phase_};
    Frame result;result.native=v.tick();
    if(!active){std::fill(out.begin(),out.end(),0);return result;}
    double inc[2];float gain[2],am[2];bool mute[2];
    for(int j=0;j<2;++j){const auto& op=v.ops_[j];inc[j]=op.phaseInc_;
        if(op.vibAmt_!=0)inc[j]*=1+op.vibAmt_*v.lfoSin_*v.vibratoScale_;
        mute[j]=op.modernTimbre_&&op.vibAmt_!=0?inc[j]>=.5:op.cachedMute_;
        gain[j]=float(op.env_.level_);
        am[j]=op.amAmt_!=0?1-op.amAmt_*float(.5+.5*v.lfoSin_):1;
    }
    auto sample=[&](size_t sub) {
        float values[2]{};
        for(int j=0;j<2;++j){if(mute[j])continue;const auto& op=v.ops_[j];
            double phase=sub==factor?op.phase_:before[j]+inc[j]*double(sub)/factor;
            if(phase>=1)phase-=std::floor(phase);
            if(j==1)phase+=double(values[0])*v.modDepth_;
            const float sine=fm_probe_math::sample<Fast>(phase,sub==factor);
            values[j]=float(sine*op.tlGain_*op.kslGain_*gain[j]*am[j]);}
        return float(double(values[1])*v.velocity_*v.volume_*.7);
    };
    for(size_t i=0;i+1<factor;++i)out[i]=sample(i+1);
    result.reconstructedEndpoint=sample(factor);
    // Preserve the exact public native endpoint and its allocation-level update.
    out.back()=result.native;return result;
}
inline Frame frame(FmVoice& v,std::span<float> out){return frameImpl<false>(v,out);}
inline Frame frameFast(FmVoice& v,std::span<float> out){return frameImpl<true>(v,out);}
} // namespace fm_probe
