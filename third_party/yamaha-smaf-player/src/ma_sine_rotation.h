// SPDX-License-Identifier: Apache-2.0
// Bounded within-frame sine rotation; no oscillator clocks or frame allocations.
#pragma once
#include <cmath>
#include <numbers>
#include <span>
#include <type_traits>
namespace fxchain::smaf::sine_rotation {
// For |step|<=.2, the sin9/cos10 remainders are <=.2^11/11!
// and .2^12/12!. Rotation error accumulates for at most127 steps.
template<class T>
inline bool linearSines(double phase,double step,std::span<T> output) {
    static_assert(std::is_floating_point_v<T>);
    if(!std::isfinite(phase)||!std::isfinite(step)||phase<0||phase>2*std::numbers::pi||
       std::abs(step)>.2||output.empty()||output.size()>127||!output.data())return false;
    const double z=step*step;
    const double sineStep=step*(1+z*(-1./6+z*(1./120+z*(-1./5040+z/362880))));
    const double cosineStep=1+z*(-.5+z*(1./24+z*(-1./720+z*(1./40320-z/3628800))));
    double a=std::sin(phase),b=std::cos(phase);
    for(auto& value:output) {
        const double nextA=a*cosineStep+b*sineStep,nextB=b*cosineStep-a*sineStep;
        a=nextA;b=nextB;value=T(a);
    }
    return true;
}
} // namespace fxchain::smaf::sine_rotation
