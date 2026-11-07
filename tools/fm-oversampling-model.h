// SPDX-License-Identifier: Apache-2.0
// Conservative steady two-sine PM planning bound, not a dynamic voice error bound.
#pragma once
#include <cmath>
#include <initializer_list>
#include <stdexcept>
namespace fm_model {
constexpr double tailTarget=1e-5;
struct Tail {int order;double bound;};
inline Tail sidebandTail(double upperIndex){
    if(!std::isfinite(upperIndex)||upperIndex<0||upperIndex>64)throw std::runtime_error("invalid PM index upper bound");
    if(upperIndex==0)return {0,0};const double half=upperIndex/2;
    // For real x and integer k>=0, |J_k(x)| <= (|x|/2)^k/k!.
    // Bound BOTH tails by a geometric majorant after the retained order.
    for(int order=0;order<256;++order){const double ratio=half/(order+2);if(ratio>=1)continue;
        const double logBound=std::log(2.)+(order+1)*std::log(half)-std::lgamma(order+2.)-std::log1p(-ratio);
        if(logBound<=std::log(tailTarget))return {order,std::exp(logBound)};}
    throw std::runtime_error("PM tail order bound exceeded");
}
inline int factor(double carrierUpper,double modulatorUpper,int order){
    if(!std::isfinite(carrierUpper)||!std::isfinite(modulatorUpper)||carrierUpper<0||modulatorUpper<0||order<0||order>256)
        throw std::runtime_error("invalid normalized frequency bound");
    const double band=carrierUpper+order*modulatorUpper;
    for(int f:{1,2,4,8,16,32})if(band<double(f)/2)return f;
    return 0; // Beyond the tested <=32 factors, never silently clamp.
}
} // namespace fm_model
