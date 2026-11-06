// SPDX-License-Identifier: Apache-2.0
// Host-only streaming decimation experiment; not used by the app renderer.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
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
    check(taps==31||taps==33||taps==37||taps==63||taps==127||taps==255||taps==257,"unsupported experimental tap count");
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
    size_t stride_,first_;
public:
    explicit Stage(int taps,double fraction=1.):h_(kernel(taps,fraction)),
        history_(std::bit_ceil(size_t(taps)),0.),mask_(history_.size()-1),
        stride_(fraction==1.?2:1),first_(fraction==1.&&((taps-1)/2)%2?2:1){}
    void reset(){std::fill(history_.begin(),history_.end(),0.);cursor_=0;odd_=false;}
    bool push(double input,double& output) {
        cursor_=(cursor_+1)&mask_;history_[cursor_]=input;odd_=!odd_;
        if(odd_)return false;
        const size_t middle=(h_.size()-1)/2;
        double sum=h_[middle]*history_[(cursor_-middle)&mask_];
        // End coefficients are zero; exact halfband stages also skip alternate zeros.
        // Symmetric pairs need one multiplication, two source loads and a sum.
        for(size_t k=first_;k<middle;k+=stride_)
            sum+=h_[k]*(history_[(cursor_-k)&mask_]+history_[(cursor_-(h_.size()-1-k))&mask_]);
        output=sum;return true;
    }
};
struct Config {int factor;std::vector<int> taps;double lastFraction=1.;};
inline void validateFactor(int factor){check(factor>=2&&factor<=32&&(factor&(factor-1))==0,"factor must be2/4/8/16/32");}
inline Config uniformConfig(int factor,int taps,double fraction=1.,int lastTaps=0) {
    validateFactor(factor);Config c{factor,{}};c.lastFraction=fraction;
    for(int n=factor;n>1;n/=2)c.taps.push_back(n==2&&lastTaps?lastTaps:taps);return c;
}
inline Config alignedConfig(int factor) {
    validateFactor(factor);Config c{factor,{}};
    for(int n=factor;n>1;n/=2)c.taps.push_back(n==2?257:factor==32&&n==4?37:33);
    return c;
}
class Decimator {
    Config config_;
    std::vector<Stage> stages_;
public:
    explicit Decimator(const Config& c):config_(c) {
        validateFactor(c.factor);check(c.taps.size()==size_t(std::bit_width(unsigned(c.factor))-1),"stage count mismatch");
        for(size_t j=0;j<c.taps.size();++j)stages_.emplace_back(c.taps[j],j+1==c.taps.size()?c.lastFraction:1.);
    }
    Decimator(int factor,int taps,double fraction=1.,int lastTaps=0):Decimator(uniformConfig(factor,taps,fraction,lastTaps)){}
    bool push(double input,double& output) {
        double value=input;for(auto& stage:stages_)if(!stage.push(value,value))return false;
        output=value;return true;
    }
    void reset(){for(auto& stage:stages_)stage.reset();}
    double delayFrames()const{double d=0;unsigned stride=1;
        for(int taps:config_.taps){d+=double((taps-1)/2)*stride/config_.factor;stride*=2;}return d;}
    int supportBoundFrames()const{return int(std::ceil(2*delayFrames()));}
    int multipliesPerOutput()const{int cost=0,outputs=config_.factor/2;
        for(size_t j=0;j<config_.taps.size();++j){const int taps=config_.taps[j],middle=(taps-1)/2;
            const bool half=j+1<config_.taps.size()||config_.lastFraction==1.;
            cost+=outputs*(half?(taps+3)/4:middle);outputs/=2;}return cost;}
    int stages()const{return int(stages_.size());}
};
// Independent dense frequency response, not the paired streaming accumulation.
inline std::complex<double> response(const Config& c,double omega) {
    std::complex<double> total=1.;
    for(size_t j=0;j<c.taps.size();++j){const auto h=kernel(c.taps[j],j+1==c.taps.size()?c.lastFraction:1.);
        std::complex<double> stage=0.;for(size_t k=0;k<h.size();++k)stage+=h[k]*std::polar(1.,-omega*double(k));
        total*=stage;omega*=2;}
    return total;
}
inline std::complex<double> response(int factor,int taps,double omega,double fraction=1.,int lastTaps=0) {
    return response(uniformConfig(factor,taps,fraction,lastTaps),omega);
}
inline std::vector<double> stream(const std::vector<double>& input,const Config& c) {
    Decimator d(c);std::vector<double> out;out.reserve(input.size()/size_t(c.factor));
    for(double x:input){double y;if(d.push(x,y))out.push_back(y);}return out;
}
inline std::vector<double> stream(const std::vector<double>& input,int factor,int taps,double fraction=1.,int lastTaps=0) {
    return stream(input,uniformConfig(factor,taps,fraction,lastTaps));
}
struct Stereo {double left=0,right=0;};
// Experimental common-delay bus. Full reset is for seek/global stop, not a
// per-channel AllSoundOff policy. Zero input preserves the natural FIR tail.
class AlignedBus {
    int factor_,padding_;
    Decimator left_,right_;
    std::array<Stereo,72> nativeDelay_{};
    std::vector<Stereo> fmDelay_;
    size_t nativeCursor_=0,fmCursor_=0;
public:
    static constexpr int commonDelay=72;
    explicit AlignedBus(int factor):factor_(factor),padding_(commonDelay-int(Decimator(alignedConfig(factor)).delayFrames())),
        left_(alignedConfig(factor)),right_(alignedConfig(factor)),fmDelay_(size_t(padding_)) {
        check(left_.delayFrames()==std::floor(left_.delayFrames())&&padding_>=0,"noninteger common delay");
    }
    Stereo frame(const Stereo* fm,size_t count,Stereo native) {
        check(count==size_t(factor_),"wrong subframe count");Stereo filtered;int produced=0;
        for(size_t i=0;i<count;++i){const bool a=left_.push(fm[i].left,filtered.left),b=right_.push(fm[i].right,filtered.right);
            check(a==b,"stereo phase mismatch");produced+=a;}
        check(produced==1,"expected one output per native frame");
        if(!fmDelay_.empty()){const auto old=fmDelay_[fmCursor_];fmDelay_[fmCursor_]=filtered;filtered=old;fmCursor_=(fmCursor_+1)%fmDelay_.size();}
        const auto old=nativeDelay_[nativeCursor_];nativeDelay_[nativeCursor_]=native;nativeCursor_=(nativeCursor_+1)%nativeDelay_.size();
        return {filtered.left+old.left,filtered.right+old.right};
    }
    void reset(){left_.reset();right_.reset();nativeDelay_.fill({});std::fill(fmDelay_.begin(),fmDelay_.end(),Stereo{});nativeCursor_=fmCursor_=0;}
    int tailBoundFrames()const{return left_.supportBoundFrames()+padding_;}
    int factor()const{return factor_;}
};
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
