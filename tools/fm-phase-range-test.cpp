// SPDX-License-Identifier: Apache-2.0
// Phase clock boundaries against the pre-shortcut floor rule, all rounding modes.
#include "fm-native-clock-probe.h"
#include <bit>
#include <cfenv>
#include <iostream>
using namespace fxchain::smaf;
int main(){try{
    const int previous=std::fegetround();size_t cases=0;
    const std::array<double,12> starts{0.,-0.,std::numeric_limits<double>::denorm_min(),.125,.5,
        std::nextafter(1.,0.),1.,std::nextafter(1.,2.),std::nextafter(2.,0.),2.,-1.,128.};
    const std::array<double,12> increments{0.,-0.,std::numeric_limits<double>::denorm_min(),.125,.5,
        std::nextafter(1.,0.),1.,std::nextafter(1.,2.),2.,128.,1e15,-128.};
    double (*volatile referenceFloor)(double)=std::floor;
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        if(std::fesetround(mode)!=0)throw std::runtime_error("phase rounding mode unavailable");
        for(bool modern:{false,true})for(double start:starts)for(double increment:increments){
            FmOpPatch patch;patch.ar=15;FmOperator op;op.configure(patch,44100,0,modern);op.noteOn(0);
            op.phase_=start;op.phaseInc_=increment;op.cachedMute_=true;
            double expected=start+increment;if(expected>=1)expected-=referenceFloor(expected);
            if(op.tick(0)!=0||std::bit_cast<uint64_t>(op.phase_)!=std::bit_cast<uint64_t>(expected))
                throw std::runtime_error("phase shortcut changed clock or signed zero at a boundary/large increment");
            ++cases;
        }
    }
    if(std::fesetround(previous)!=0)throw std::runtime_error("phase rounding mode restore failed");
    std::cout<<cases<<" native phase boundary/large-increment/rounding-mode cases match original floor clock bit-exact PASS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
