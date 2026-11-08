// SPDX-License-Identifier: Apache-2.0
// Host-only stationary sine-PM tail study. Not an app density/EG policy.
#pragma once
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
namespace fm_nested_tail {
// Graf's identity: sum J_k(x)^2 exp(2ks)=I0(2x sinh(s)).
// Cauchy-Schwarz gives sum |J_k(x)| exp(a|k|)
// <= sqrt(2*coth(d))*exp(|x|*sinh(a+d)), d>0.
// Apply it to k, then n. Triangle inequality makes the result independent
// of any frequency lattice or relative oscillator phases.
inline double independentBound(double innerBeta,double middleBeta,double innerFrequency,
                               double middleFrequency,double carrierFrequency,double cutoff) {
    for(double v:{innerBeta,middleBeta,innerFrequency,middleFrequency,carrierFrequency,cutoff})
        if(!std::isfinite(v)||v<0)throw std::invalid_argument("invalid independent nested tail domain");
    // Bound the numerical domain as well as the symbolic expression. This
    // covers VM35's maximum2*pi*4 index and native unmuted frequencies.
    if(innerBeta>32||middleBeta>32||innerFrequency>.5||middleFrequency>.5||carrierFrequency>.5||cutoff==0||cutoff>128)
        throw std::invalid_argument("independent nested tail outside bounded host range");
    double best=std::numeric_limits<double>::infinity();
    for(double d:{.001,.003,.01,.03,.1,.3,1.})for(double c:{.001,.003,.01,.03,.1,.3,1.}) {
        const double prefactor=std::log(2.)-.5*(std::log(std::tanh(d))+std::log(std::tanh(c)));
        for(int step=0;step<=256;++step) {
            const double y=1e-6*std::pow(128e6,double(step)/256);
            const double inner=innerFrequency*y+d;
            if(inner>700)continue;
            const double middle=middleFrequency*y+innerBeta*std::sinh(inner)+c;
            if(!std::isfinite(middle)||middle>700)continue;
            double candidate=prefactor+carrierFrequency*y+middleBeta*std::sinh(middle)-cutoff*y;
            if(!std::isfinite(candidate))continue;
            candidate+=1e-10*(1+std::abs(candidate));best=std::min(best,candidate);
        }
    }
    if(best>700)return std::numeric_limits<double>::infinity();
    return std::exp(std::max(best,std::log(std::numeric_limits<double>::min())));
}
// For sin(Ct+b2*sin(Mt+b1*sin(It))), analytic continuation gives
// |wave(t-iy)| <= exp(Cy+b2*sinh(My+b1*sinh(Iy))). Parseval and
// Cauchy-Schwarz then bound the absolute sine-coefficient sum above a cut.
// spacing is a known frequency lattice spacing, not the oscillator minimum.
inline double bound(double innerBeta,double middleBeta,double innerFrequency,
                    double middleFrequency,double carrierFrequency,double spacing,
                    double cutoff,double directAmplitude=0,double directFrequency=0) {
    for(double v:{innerBeta,middleBeta,innerFrequency,middleFrequency,carrierFrequency,spacing,cutoff,directAmplitude,directFrequency})
        if(!std::isfinite(v)||v<0)throw std::invalid_argument("invalid nested tail domain");
    if(spacing==0||cutoff==0)throw std::invalid_argument("zero nested tail spacing/cutoff");
    for(double frequency:{innerFrequency,middleFrequency,carrierFrequency,directFrequency}) {
        const double index=frequency/spacing;
        if(!std::isfinite(index)||std::abs(index-std::round(index))>1e-10*(1+std::abs(index)))
            throw std::invalid_argument("nested tail frequencies do not share the supplied lattice");
    }
    double best=std::numeric_limits<double>::infinity();
    for(int step=0;step<=512;++step) {
        const double y=1e-6*std::pow(128e6,double(step)/512);
        const double argument=middleFrequency*y+innerBeta*std::sinh(innerFrequency*y);
        if(!std::isfinite(argument)||argument>700)continue;
        const double phi=carrierFrequency*y+middleBeta*std::sinh(argument);
        if(!std::isfinite(phi))continue;
        const double direct=directAmplitude>0?std::log(directAmplitude)+directFrequency*y:
            -std::numeric_limits<double>::infinity();
        const double maximum=std::max(phi,direct);
        const double logNorm=maximum+std::log(std::exp(phi-maximum)+std::exp(direct-maximum));
        double logBound=std::log(2.)+logNorm-cutoff*y-.5*std::log(-std::expm1(-2*spacing*y));
        // Numerical slack; every sampled y is itself a valid analytic bound.
        logBound+=1e-10*(1+std::abs(logBound));best=std::min(best,logBound);
    }
    if(best>700)return std::numeric_limits<double>::infinity();
    return std::exp(std::max(best,std::log(std::numeric_limits<double>::min())));
}
} // namespace fm_nested_tail
