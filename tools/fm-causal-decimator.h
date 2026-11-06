// SPDX-License-Identifier: Apache-2.0
// Host-only streaming decimation experiment; not used by the app renderer.
#pragma once
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>
namespace fm_causal {
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline std::vector<double> kernel(int taps,double fraction=1.) {
    check(taps==31||taps==63||taps==127||taps==255,"halfband taps must be31/63/127/255");
    check(fraction==1.||fraction==.95,"last-stage Nyquist fraction must be1/.95");
    const int middle=(taps-1)/2;const double pi=std::numbers::pi;
    std::vector<double> h(size_t(taps),0.);
    for(int k=1;k<=middle;++k) {
        const int d=k-middle;
        if(fraction==1.&&d!=0&&d%2==0)continue; // exact halfband zeros
        const double sinc=d==0?.5*fraction:std::sin(pi*.5*fraction*d)/(pi*d);
        const double window=.42-.5*std::cos(2*pi*k/(taps-1))+.08*std::cos(4*pi*k/(taps-1));
        h[size_t(k)]=h[size_t(taps-1-k)]=sinc*window;
    }
    double sum=0;for(double v:h)sum+=v;
    for(double& v:h)v/=sum;
    return h;
}
class Stage {
    std::vector<double> h_,history_;
    size_t cursor_=0,mask_;
    bool odd_=false;
    size_t stride_;
public:
    explicit Stage(int taps,double fraction=1.):h_(kernel(taps,fraction)),history_(size_t(taps+1),0.),mask_(size_t(taps)),stride_(fraction==1.?2:1){}
    bool push(double input,double& output) {
        cursor_=(cursor_+1)&mask_;history_[cursor_]=input;odd_=!odd_;
        if(odd_)return false;
        const size_t middle=(h_.size()-1)/2;
        double sum=h_[middle]*history_[(cursor_-middle)&mask_];
        // End coefficients are zero; exact halfband stages also skip alternate zeros.
        // Symmetric pairs need one multiplication, two source loads and a sum.
        for(size_t k=stride_;k<middle;k+=stride_)
            sum+=h_[k]*(history_[(cursor_-k)&mask_]+history_[(cursor_-(h_.size()-1-k))&mask_]);
        output=sum;return true;
    }
};
class Decimator {
    int factor_,taps_,lastTaps_;
    double fraction_;
    std::vector<Stage> stages_;
public:
    Decimator(int factor,int taps,double fraction=1.,int lastTaps=0):factor_(factor),taps_(taps),lastTaps_(lastTaps?lastTaps:taps),fraction_(fraction) {
        check(factor>=2&&factor<=32&&(factor&(factor-1))==0,"factor must be2/4/8/16/32");
        (void)kernel(taps);(void)kernel(lastTaps_,fraction);
        for(int n=factor;n>1;n/=2)stages_.emplace_back(n==2?lastTaps_:taps,n==2?fraction:1.);
    }
    bool push(double input,double& output) {
        double value=input;
        for(auto& stage:stages_)if(!stage.push(value,value))return false;
        output=value;return true;
    }
    double delayFrames()const{return double((taps_-1)/2)*(factor_/2-1)/factor_+double((lastTaps_-1)/2)/2;}
    int supportBoundFrames()const{return int(std::ceil(2*delayFrames()));}
    int multipliesPerOutput()const{return (taps_+1)/4*(factor_-2)+(fraction_==1.?(lastTaps_+1)/4:(lastTaps_-1)/2);}
    int stages()const{return int(stages_.size());}
};
// Independent dense frequency response, not the paired streaming accumulation.
inline std::complex<double> response(int factor,int taps,double omega,double fraction=1.,int lastTaps=0) {std::complex<double> total=1.;
    for(int n=factor;n>1;n/=2) {
        const auto h=kernel(n==2&&lastTaps?lastTaps:taps,n==2?fraction:1.);
        std::complex<double> stage=0.;
        for(size_t k=0;k<h.size();++k)stage+=h[k]*std::polar(1.,-omega*double(k));
        total*=stage;omega*=2;
    }
    return total;
}
inline std::vector<double> stream(const std::vector<double>& input,int factor,int taps,double fraction=1.,int lastTaps=0) {
    Decimator d(factor,taps,fraction,lastTaps);std::vector<double> out;out.reserve(input.size()/size_t(factor));
    for(double x:input){double y;if(d.push(x,y))out.push_back(y);}return out;
}
inline void selfTest() {
    for(auto sizes:std::vector<std::pair<int,int>>{{31,31},{63,63},{127,127},{31,255}})
    for(double fraction:{1.,.95}) {
        const int taps=sizes.first,lastTaps=sizes.second;
        for(auto spec:std::vector<std::pair<int,double>>{{taps,1.},{lastTaps,fraction}}) {
            const auto h=kernel(spec.first,spec.second);double dc=0;
            for(size_t k=0;k<h.size();++k){dc+=h[k];check(h[k]==h[h.size()-1-k],"kernel symmetry");
                if(spec.second==1.&&int(k)!=(spec.first-1)/2&&int(k)%2==1)check(h[k]==0,"halfband zero coefficient");}
            check(std::abs(dc-1)<1e-14,"filter DC gain");
        }
        for(int factor:{2,4,8,16,32}) {
            // Independent dense convolution of each stage's upsampled kernel.
            std::vector<double> cascade{1};int stride=1;
            for(int n=factor;n>1;n/=2){const auto h=kernel(n==2?lastTaps:taps,n==2?fraction:1.);
                std::vector<double> next(cascade.size()+(h.size()-1)*size_t(stride),0.);
                for(size_t a=0;a<cascade.size();++a)for(size_t b=0;b<h.size();++b)
                    next[a+b*size_t(stride)]+=cascade[a]*h[b];
                cascade=std::move(next);stride*=2;}
            const size_t length=cascade.size()+size_t(factor*64);
            for(int phase=0;phase<factor;++phase) {
                std::vector<double> input(length,0.);input[size_t(phase)]=1;
                const auto out=stream(input,factor,taps,fraction,lastTaps);
                for(size_t i=0;i<out.size();++i){const auto position=int64_t((i+1)*size_t(factor)-1)-phase;
                    const double expected=position>=0&&size_t(position)<cascade.size()?cascade[size_t(position)]:0.;
                    check(std::abs(out[i]-expected)<1e-13,"stream/dense impulse phase mismatch");}
            }
            std::vector<double> input(length,1.);const auto step=stream(input,factor,taps,fraction,lastTaps);
            check(std::abs(step.back()-1)<1e-12,"settled step/DC mismatch");
            auto changed=input;for(size_t i=length/2;i<length;++i)changed[i]=std::sin(i*.31);
            const auto altered=stream(changed,factor,taps,fraction,lastTaps);
            for(size_t i=0;(i+1)*size_t(factor)<=length/2;++i)check(step[i]==altered[i],"future input changes past output");
            for(double omega:{0.,.017,.173,.711}) {
                std::complex<double> direct=0.;for(size_t k=0;k<cascade.size();++k)direct+=cascade[k]*std::polar(1.,-omega*k);
                check(std::abs(direct-response(factor,taps,omega,fraction,lastTaps))<1e-11,"cascade frequency response mismatch");
            }
        }
    }
    for(int factor:{0,1,3,64}){bool failed=false;try{Decimator d(factor,31);}catch(const std::runtime_error&){failed=true;}check(failed,"invalid factor accepted");}
    for(int taps:{0,30,32,256}){bool failed=false;try{Decimator d(2,taps);}catch(const std::runtime_error&){failed=true;}check(failed,"invalid taps accepted");}
}
} // namespace fm_causal
