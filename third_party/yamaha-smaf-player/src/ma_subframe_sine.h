// SPDX-License-Identifier: Apache-2.0
// Optional math for extra sine subframes only. The native tick stays exact.
#pragma once
#include <array>
#include <cstddef>
#include <cmath>
namespace fxchain::smaf::subframeMath {
constexpr double tau=6.283185307179586;
constexpr size_t cells=256;
struct Cell {double a,b,c,d;};
inline const std::array<Cell,cells>& coefficients(){
    static const auto table=[] {std::array<Cell,cells> result{};const double h=tau/cells;
        for(size_t i=0;i<cells;++i){const double x=tau*double(i)/cells,y=tau*double(i+1)/cells;
            const double a=std::sin(x),end=std::sin(y),b=h*std::cos(x),slopeEnd=h*std::cos(y);
            result[i]={a,b,3*(end-a)-2*b-slopeEnd,2*(a-end)+b+slopeEnd};}
        return result;}();return table;
}
inline double sineCycles(double phase){
    if(!(phase>=0&&phase<1)){phase-=std::floor(phase);
        if(!(phase>=0&&phase<1))return std::sin(tau*phase);}
    const double position=phase*cells;const size_t i=size_t(position);const double t=position-double(i);const auto& c=coefficients()[i];
    return c.a+t*(c.b+t*(c.c+t*c.d));
}
template<bool Fast> inline float sample(double cycles,bool nativeEndpoint){
    if constexpr(Fast)if(!nativeEndpoint)return float(sineCycles(cycles));
    return float(std::sin(tau*(cycles-std::floor(cycles))));
}
} // namespace fxchain::smaf::subframeMath
