// SPDX-License-Identifier: Apache-2.0
// Host-only graph6 sine recurrence and operator rows. No app routing changes.
#pragma once
#include "fm-nested-sine-probe.h"
#include "ma_sine_rotation.h"
namespace fm_recursive {
using fxchain::smaf::sine_rotation::linearSines;
inline fm_probe::Frame frame(fxchain::smaf::FmVoice& v,std::span<float> output) {
    const size_t factor=output.size();
    if(!fm_nested::eligible(v)||!output.data()||factor<2||factor>128||(factor&(factor-1)))
        throw std::runtime_error("recursive probe requires graph6 sine/no-feedback and2..128 samples");
    if(factor<16)return fm_nested::frame<true,128>(v,output);
    const bool active=v.active_;double before[4];
    for(int j=0;j<4;++j)before[j]=v.ops_[j].phase_;
    fm_probe::Frame result;result.native=v.tick();
    if(!active||v.velocity_==0||v.volume_==0) {
        std::fill(output.begin(),output.end(),0);
        output.back()=result.reconstructedEndpoint=result.native;return result;
    }
    double inc[4];float level[4],am[4];bool mute[4];
    for(int j=0;j<4;++j) {
        const auto& op=v.ops_[j];inc[j]=op.phaseInc_;
        if(op.vibAmt_!=0)inc[j]*=1+op.vibAmt_*v.lfoSin_*v.vibratoScale_;
        mute[j]=op.modernTimbre_&&op.vibAmt_!=0?inc[j]>=.5:op.cachedMute_;
        level[j]=float(op.env_.level_);am[j]=op.amAmt_!=0?1-op.amAmt_*float(.5+.5*v.lfoSin_):1;
    }
    // All observed cells are filled before their readers. No frame allocation.
    std::array<std::array<float,128>,4> rows;
    for(int j=0;j<4;++j) {
        if(mute[j]){std::fill_n(rows[j].begin(),factor-1,0.f);continue;}
        const auto& op=v.ops_[j];
        const double phase=2*std::numbers::pi*(before[j]-std::floor(before[j]));
        const bool recurrent=j<2&&linearSines(phase,2*std::numbers::pi*inc[j]/factor,
                                              std::span<float>(rows[j].data(),factor-1));
        for(size_t sub=1;sub<factor;++sub) {
            float sine;
            if(recurrent)sine=rows[j][sub-1];
            else {
                double position=before[j]+inc[j]*double(sub)/factor;
                if(position>=1)position-=std::floor(position);
                if(j>=2)position+=double(rows[j-1][sub-1])*v.modDepth_;
                sine=fm_probe_math::sample<true>(position,false);
            }
            rows[j][sub-1]=float(sine*op.tlGain_*op.kslGain_*level[j]*am[j]);
        }
    }
    for(size_t sub=1;sub<factor;++sub)
        output[sub-1]=float((double(rows[0][sub-1])+double(rows[3][sub-1]))*.5*v.velocity_*v.volume_*.7);
    float endpoint[4]{};
    for(int j=0;j<4;++j)if(!mute[j]) {
        const auto& op=v.ops_[j];double phase=op.phase_;
        if(phase>=1)phase-=std::floor(phase);
        if(j>=2)phase+=double(endpoint[j-1])*v.modDepth_;
        const float sine=fm_probe_math::sample<false>(phase,true);
        endpoint[j]=float(sine*op.tlGain_*op.kslGain_*level[j]*am[j]);
    }
    result.reconstructedEndpoint=float((double(endpoint[0])+double(endpoint[3]))*.5*v.velocity_*v.volume_*.7);
    output.back()=result.native;return result;
}
} // namespace fm_recursive
