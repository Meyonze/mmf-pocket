// SPDX-License-Identifier: Apache-2.0
// Host-only stationary alias-tail planning. Not a player/EG density policy.
#pragma once
#include "fm-nested-tail-bound.h"
namespace fm_nested_plan {
struct Spectrum {
    double innerBeta,middleBeta,innerFrequency,middleFrequency,carrierFrequency,spacing;
    double directAmplitude=0,directFrequency=0;
};
struct Plan {
    int factor=0; // zero means no permitted factor established the bound
    double tail=std::numeric_limits<double>::infinity();
};
enum class Method { IndependentFrequencies, KnownLattice };
inline Plan choose(const Spectrum& s,double tailBudget,int maximumFactor=128,Method method=Method::IndependentFrequencies) {
    if(!std::isfinite(tailBudget)||tailBudget<=0||
       (maximumFactor!=16&&maximumFactor!=32&&maximumFactor!=64&&maximumFactor!=128))
        throw std::invalid_argument("invalid stationary nested density budget/limit");
    for(int factor:{16,32,64,128}) {
        if(factor>maximumFactor)break;
        double tail;
        if(method==Method::KnownLattice)tail=fm_nested_tail::bound(s.innerBeta,s.middleBeta,s.innerFrequency,
            s.middleFrequency,s.carrierFrequency,s.spacing,factor-.5,s.directAmplitude,s.directFrequency);
        else {
            if(!std::isfinite(s.directAmplitude)||s.directAmplitude<0||!std::isfinite(s.directFrequency)||s.directFrequency<0)
                throw std::invalid_argument("invalid direct nested source");
            tail=fm_nested_tail::independentBound(s.innerBeta,s.middleBeta,s.innerFrequency,
                s.middleFrequency,s.carrierFrequency,factor-.5);
            if(s.directFrequency>=factor-.5)tail+=s.directAmplitude;
        }
        if(tail<=tailBudget)return {factor,tail};
    }
    return {};
}
} // namespace fm_nested_plan
