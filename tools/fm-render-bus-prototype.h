// SPDX-License-Identifier: Apache-2.0
// Host-only routing: timed score channels plus an independent native source.
#pragma once
#include "fm-timed-stop-prototype.h"
namespace fm_causal {
struct RenderChannelInput {
    int channel;
    std::span<const Stereo> fm;
    Stereo native{};
    bool hardStop=false;
};
class RenderBusPrototype {
    struct Slot {
        int channel;
        TimedStopPrototype bus;
        Slot(int ch,int factor,const TimedStopPrototype::KernelHandle& kernel):channel(ch),bus(factor,kernel){}
    };
    TimedStopPrototype::KernelHandle kernel_;
    std::vector<Slot> slots_;
    std::array<int,128> index_;
    std::array<Stereo,AlignedBus::commonDelay> independentDelay_{};
    size_t independentCursor_=0;
    int factor_,independentPending_=0;
    int index(int channel)const {
        check(channel>=0&&channel<128,"render channel out of range");
        check(index_[size_t(channel)]>=0,"render channel not configured");return index_[size_t(channel)];
    }
public:
    RenderBusPrototype(int factor,std::span<const int> channels):kernel_(TimedStopPrototype::prepareKernel(factor)),factor_(factor){
        index_.fill(-1);slots_.reserve(channels.size());
        for(int channel:channels){check(channel>=0&&channel<128,"render channel out of range");
            check(index_[size_t(channel)]<0,"duplicate render channel");
            index_[size_t(channel)]=int(slots_.size());slots_.emplace_back(channel,factor,kernel_);}
    }
    Stereo frame(std::span<const RenderChannelInput> input,Stereo independent={}) {
        std::array<const RenderChannelInput*,128> byChannel{};std::array<bool,128> nonzero{};
        check(std::isfinite(independent.left)&&std::isfinite(independent.right),"nonfinite independent source");
        // Reject the whole batch before advancing channels or independent audio.
        for(const auto& x:input){(void)index(x.channel);check(!byChannel[size_t(x.channel)],"duplicate render input");
            check(x.fm.empty()||x.fm.size()==size_t(factor_),"wrong render subframe count");
            check(std::isfinite(x.native.left)&&std::isfinite(x.native.right),"nonfinite render native input");
            bool nz=x.native.left!=0||x.native.right!=0;
            for(const auto& sample:x.fm){check(std::isfinite(sample.left)&&std::isfinite(sample.right),"nonfinite render FM input");
                nz=nz||sample.left!=0||sample.right!=0;}
            byChannel[size_t(x.channel)]=&x;nonzero[size_t(x.channel)]=nz;}
        Stereo mixed{};
        for(auto& slot:slots_){const auto* x=byChannel[size_t(slot.channel)];
            if(!slot.bus.hasPendingWork()&&(!x||(!nonzero[size_t(slot.channel)]&&!x->hardStop)))continue;
            const auto y=x?slot.bus.frame(x->fm,x->native,x->hardStop):slot.bus.frame({});
            mixed.left+=y.left;mixed.right+=y.right;}
        const auto delayed=independentDelay_[independentCursor_];independentDelay_[independentCursor_]=independent;
        independentCursor_=(independentCursor_+1)%independentDelay_.size();
        if(independent.left!=0||independent.right!=0)independentPending_=AlignedBus::commonDelay;
        else if(independentPending_>0)--independentPending_;
        return {mixed.left+delayed.left,mixed.right+delayed.right};
    }
    void reset(){for(auto& slot:slots_)slot.bus.reset();independentDelay_.fill({});independentCursor_=0;independentPending_=0;}
    int pendingFrames()const {int remaining=independentPending_;for(const auto& slot:slots_)remaining=std::max(remaining,slot.bus.pendingFrames());return remaining;}
    size_t activeChannels()const {size_t n=0;for(const auto& slot:slots_)n+=slot.bus.hasPendingWork();return n;}
};
} // namespace fm_causal
