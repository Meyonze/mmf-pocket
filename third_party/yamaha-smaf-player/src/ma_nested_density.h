// SPDX-License-Identifier: Apache-2.0
// Opt-in VM35 nested6 stationary envelope; not a transient error bound.
#pragma once
namespace fxchain::smaf::nested_density {
// TL lower bounds12/16/20/24 imply modulation indices below9/6.5/4.5/3.2.
// Each integer is a conservative maximum frequency in units of1/64 cycles
// per native frame, for16/32/64x. Both individual indices/frequencies enter
// the independent stationary alias-tail proof. Tests recompute all64 cells.
// Native Nyquist suppression retains an audible frequency bound of.5;
// 128x always meets the1e-4 stationary budget in the admitted class.
inline constexpr int limits[4][4][3]={
    {{4,8,17},{5,10,20},{6,12,25},{7,14,29}},
    {{5,11,23},{6,13,27},{8,16,32},{9,19,32}},
    {{7,15,31},{9,18,32},{11,22,32},{13,26,32}},
    {{9,20,32},{12,24,32},{14,29,32},{17,32,32}}
};
inline constexpr double betaBounds[4]={9,6.5,4.5,3.2};
inline int tier(int tl){return tl>=24?3:tl>=20?2:tl>=16?1:0;}
// Caller admits TL>=12 for both modulators. No render-loop search/pow/sinh.
inline int factor(double maximumFrequency,int innerTl,int middleTl) {
    const auto& q=limits[tier(innerTl)][tier(middleTl)];
    for(int i=0;i<3;++i)if(maximumFrequency<=q[i]/64.)return 16<<i;
    return 128;
}
}
