// SPDX-License-Identifier: Apache-2.0
// Host-only hard-stop timing policy experiment, not a player API.
#pragma once
#include "fm-causal-decimator.h"
#include <span>
#include <memory>
namespace fm_causal {
class TimedStopPrototype {
public:
    class Kernel {
        friend class TimedStopPrototype;
        int factor_;
        std::vector<double> values_;
        Kernel(int factor,std::vector<double> values):factor_(factor),values_(std::move(values)){}
    };
    using KernelHandle=std::shared_ptr<const Kernel>;
private:
    static constexpr size_t delay=AlignedBus::commonDelay,maxStopGap=4,historyFrames=delay+maxStopGap+1;
    int factor_,padding_;
    AlignedBus bus_;
    KernelHandle impulse_;
    std::vector<Stereo> fmHistory_;
    std::array<Stereo,historyFrames> nativeHistory_{};
    std::array<bool,historyFrames> stops_{},nonzero_{};
    size_t cursor_=0,cutAge_=0;
    bool direct_=false;
    int audioPending_=0,queuedStops_=0;
    uint64_t replayedFrames_=0,directFrames_=0;
    static std::vector<double> impulse(int factor) {
        const auto config=alignedConfig(factor);std::vector<double> h{1};size_t stride=1;
        for(int taps:config.taps){const auto k=kernel(taps);std::vector<double> next(h.size()+(k.size()-1)*stride,0.);
            for(size_t a=0;a<h.size();++a)for(size_t b=0;b<k.size();++b)next[a+b*stride]+=h[a]*k[b];
            h=std::move(next);stride*=2;}
        return h;
    }
    Stereo advance(size_t slot) {
        if(nonzero_[slot])audioPending_=bus_.tailBoundFrames();
        if(audioPending_==0)return {};
        const auto y=bus_.frame(fmHistory_.data()+slot*size_t(factor_),size_t(factor_),nativeHistory_[slot]);
        if(!nonzero_[slot]&&--audioPending_==0)bus_.reset();return y;
    }
    bool nextStopSoon(size_t due)const {
        for(size_t gap=1;gap<=maxStopGap;++gap)if(stops_[(due+gap)%historyFrames])return true;
        return false;
    }
    Stereo directOutput() {
        check(cutAge_<historyFrames,"timed-stop direct history exceeded bound");
        ++directFrames_;audioPending_=0;
        for(size_t age=0;age<=cutAge_;++age)if(nonzero_[(cursor_+historyFrames-age)%historyFrames]){
            audioPending_=std::max(0,bus_.tailBoundFrames()-int(age));break;}
        if(audioPending_==0)return {};
        Stereo y{};
        // Same finite cascade, sampled at the usual decimation phase. Remove
        // terms before the latest cut without replaying intermediate outputs.
        for(size_t age=size_t(padding_);age<=cutAge_;++age){
            const auto* x=fmHistory_.data()+((cursor_+historyFrames-age)%historyFrames)*size_t(factor_);
            const auto* h=impulse_->values_.data()+(age-size_t(padding_))*size_t(factor_);
            for(int k=0;k<factor_;++k)if(h[k]!=0){const auto v=x[factor_-1-k];y.left+=h[k]*v.left;y.right+=h[k]*v.right;}
        }
        const auto native=nativeHistory_[(cursor_+historyFrames-delay)%historyFrames];
        y.left+=native.left;y.right+=native.right;
        return y;
    }
public:
    static KernelHandle prepareKernel(int factor){validateFactor(factor);return KernelHandle(new Kernel(factor,impulse(factor)));}
    explicit TimedStopPrototype(int factor,KernelHandle sharedKernel={}):factor_(factor),
        padding_(AlignedBus::commonDelay-int(Decimator(alignedConfig(factor)).delayFrames())),
        bus_(factor),impulse_(sharedKernel?std::move(sharedKernel):prepareKernel(factor)),fmHistory_(historyFrames*size_t(factor)){
        check(impulse_->factor_==factor,"shared kernel factor mismatch");}
    // Caller kills source voices in score order before submitting this frame.
    // Stop at input n takes effect at n+72, retaining NoteOn ordered after it.
    // Ordinary NoteOff/Hold remain source-envelope operations.
    Stereo frame(std::span<const Stereo> fm,Stereo native={},bool hardStop=false) {
        check(fm.empty()||fm.size()==size_t(factor_),"wrong timed-stop subframe count");
        check(std::isfinite(native.left)&&std::isfinite(native.right),"nonfinite timed-stop native input");
        bool nonzero=native.left!=0||native.right!=0;
        for(const auto& x:fm){check(std::isfinite(x.left)&&std::isfinite(x.right),"nonfinite timed-stop FM input");
            nonzero=nonzero||x.left!=0||x.right!=0;}
        auto* dest=fmHistory_.data()+cursor_*size_t(factor_);
        if(fm.empty())std::fill_n(dest,size_t(factor_),Stereo{});else std::copy(fm.begin(),fm.end(),dest);
        nativeHistory_[cursor_]=native;stops_[cursor_]=hardStop;queuedStops_+=hardStop;nonzero_[cursor_]=nonzero;
        const size_t due=(cursor_+historyFrames-delay)%historyFrames;Stereo y;
        if(stops_[due]) {
            stops_[due]=false;--queuedStops_;cutAge_=delay;direct_=nextStopSoon(due);
            if(direct_)y=directOutput();
            else {
                bus_.reset();audioPending_=0;
                // Restore once at the last stop of a nearby run. The 73-frame
                // rebuild also retains notes entered after this stop.
                for(size_t i=0;i<=delay;++i)y=advance((due+i)%historyFrames);
                replayedFrames_+=delay+1;
            }
        } else if(direct_){++cutAge_;y=directOutput();}
        else y=advance(cursor_);
        cursor_=(cursor_+1)%historyFrames;return y;
    }
    void reset(){bus_.reset();std::fill(fmHistory_.begin(),fmHistory_.end(),Stereo{});nativeHistory_.fill({});
        stops_.fill(false);nonzero_.fill(false);cursor_=cutAge_=0;direct_=false;audioPending_=queuedStops_=0;replayedFrames_=directFrames_=0;}
    bool hasPendingWork()const{return audioPending_!=0||queuedStops_!=0;}
    int pendingFrames()const {
        if(queuedStops_==0)return audioPending_;
        int remaining=audioPending_;
        for(size_t age=0;age<delay;++age)if(stops_[(cursor_+historyFrames-1-age)%historyFrames])
            remaining=std::max(remaining,int(delay-age));
        return remaining;
    }
    uint64_t replayedFrames()const{return replayedFrames_;}
    uint64_t directFrames()const{return directFrames_;}
    size_t retainedInputFrames()const{return historyFrames;}
};
} // namespace fm_causal
