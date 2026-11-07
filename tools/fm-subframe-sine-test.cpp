// SPDX-License-Identifier: Apache-2.0
#include "fm-subframe-sine.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
void require(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
int main(){using namespace fm_probe_math;const double theoretical=std::pow(tau/cells,4)/384;double maximum=0;
    for(int i=0;i<1048576;++i){const double phase=double(i)/1048576,exact=std::sin(tau*phase);maximum=std::max(maximum,std::abs(sineCycles(phase)-exact));}
    require(maximum<=theoretical+5e-14,"cubic sine error exceeds Hermite bound");
    for(size_t i=0;i<=cells;++i){const double center=double(i)/cells;
        for(double phase:{std::nextafter(center,-1.),center,std::nextafter(center,2.)})
            require(std::abs(sineCycles(phase)-std::sin(tau*(phase-std::floor(phase))))<=theoretical+5e-14,"near-knot rounding or table bound failure");}
    const auto& table=coefficients();for(size_t i=0;i<cells;++i){const auto&a=table[i];const auto&b=table[(i+1)%cells];
        require(std::abs((a.a+a.b+a.c+a.d)-b.a)<5e-14,"sine table value discontinuity");
        require(std::abs((a.b+2*a.c+3*a.d)-b.b)<5e-14,"sine table derivative discontinuity");}
    for(double phase:{-4.125,-.25,-0.,0.,.25,1.,3.125,17.5})require(std::abs(sineCycles(phase)-std::sin(tau*(phase-std::floor(phase))))<=theoretical+5e-14,"wrapped phase mismatch");
    require(std::isnan(sineCycles(std::numeric_limits<double>::quiet_NaN())),"nonfinite phase must not index the table");
    std::cout<<"1048576 phase controls; maximum sine error="<<maximum<<"; Hermite bound="<<theoretical<<"; table bytes="<<sizeof(table)<<" PASS\n";
}
