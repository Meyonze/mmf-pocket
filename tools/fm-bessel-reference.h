// SPDX-License-Identifier: Apache-2.0
// Host-only integer-order Bessel reference for deeply nested PM models.
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace fm_bessel {
inline std::vector<double> downward(double x,int order,int extra) {
    const int end=(std::max(order,int(std::ceil(x)))+64+
                   int(std::ceil(std::sqrt(40.*std::max(order,int(std::ceil(x))))))+extra+1)&~1;
    std::vector<long double> values(size_t(order+1));
    long double current=1,next=0,normalization=2;
    for(int j=end;j>0;--j) {
        const long double previous=(2.L*j/x)*current-next;
        next=current;current=previous;
        if(j-1<=order)values[size_t(j-1)]=current;
        if((j-1)%2==0)normalization+=(j==1?1:2)*current;
        if(std::abs(current)>1e200L) {
            current*=1e-200L;next*=1e-200L;normalization*=1e-200L;
            for(auto& v:values)v*=1e-200L;
        }
    }
    if(!std::isfinite(normalization)||normalization==0)throw std::runtime_error("Bessel normalization failed");
    std::vector<double> result(size_t(order+1));
    for(int k=0;k<=order;++k) {
        result[size_t(k)]=double(values[size_t(k)]/normalization);
        if(!std::isfinite(result[size_t(k)]))throw std::runtime_error("nonfinite Bessel reference");
    }
    return result;
}
inline std::vector<double> coefficients(double argument,int order) {
    if(!std::isfinite(argument)||std::abs(argument)>8192||order<0||order>16384)
        throw std::invalid_argument("Bessel reference outside bounded host range");
    const double x=std::abs(argument);
    if(x==0){std::vector<double> out(size_t(order+1));out[0]=1;return out;}
    // Below this range, the series correction is O(x^2)<1e-200.
    // Evaluate its leading terms without dividing by a subnormal argument.
    if(x<1e-100) {
        std::vector<double> out(size_t(order+1));out[0]=1;long double term=1;
        for(int k=1;k<=order;++k){term*=static_cast<long double>(argument)/(2.L*k);out[size_t(k)]=double(term);}
        return out;
    }
    auto out=downward(x,order,0);const auto longer=downward(x,order,64);
    // Separate recurrence depths must converge before this model is usable.
    for(int k=0;k<=order;++k) {
        if(std::abs(out[size_t(k)]-longer[size_t(k)])>2e-15)
            throw std::runtime_error("Bessel recurrence did not converge");
        if(argument<0&&(k&1))out[size_t(k)]=-out[size_t(k)];
    }
    return out;
}
inline double signedOrder(const std::vector<double>& positive,int n) {
    return positive.at(size_t(std::abs(n)))*((n<0&&(n&1))?-1:1);
}
} // namespace fm_bessel
