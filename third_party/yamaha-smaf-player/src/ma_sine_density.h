// SPDX-License-Identifier: Apache-2.0
// Conservative PM sideband-tail bound for choosing8x or16x source density.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
namespace fxchain::smaf::sine_density {
// |J_n(beta)| <= (beta/2)^n/n! * exp(beta^2/(4(n+1))). Subsequent
// bounds have ratio <= beta/(2(n+1)); include both positive/negative orders.
inline double tailBound(double beta,int first) {
    if(beta==0)return 0;
    const double n=std::max(1,first),ratio=beta/(2*(n+1));
    if(ratio>=1)return std::numeric_limits<double>::infinity();
    // Robbins' lower bound on log(n!) keeps the coefficient bound above
    // the true value and avoids lgamma's implementation-dependent signgam.
    const double logFactorialLower=(n+.5)*std::log(n)-n+.91893853320467274178+1/(12*n+1);
    const double logTerm=n*std::log(beta/2)-logFactorialLower+beta*beta/(4*(n+1));
    return 2*std::exp(logTerm)/(1-ratio);
}
inline bool needs16(double beta,double carrier,double modulator) {
    if(!std::isfinite(beta)||!std::isfinite(carrier)||!std::isfinite(modulator))return true;
    // Folded energy can enter the final +/-0.5 band near8 +/-0.5,
    // not merely when it crosses the8x source Nyquist at4.
    if(beta<=0||modulator<=0)return false;
    const double available=std::ceil((7.5-carrier)/modulator);
    const int first=int(std::clamp(available,1.,256.));
    return tailBound(beta,first)>1e-4;
}
}
