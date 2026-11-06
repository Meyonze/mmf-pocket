// SPDX-License-Identifier: Apache-2.0
// Host-only channel ownership experiment; no score-event timing policy.
#pragma once
#include "fm-causal-decimator.h"
#include <span>
namespace fm_causal {
struct ChannelInput {
    int channel;
    std::span<const Stereo> fm; // Empty means zero FM input, not an event.
    Stereo native{};
};
class ChannelBusPrototype {
    struct Slot {
        int channel;
        AlignedBus bus;
        int pending=0;
        Slot(int ch,int factor):channel(ch),bus(factor){}
    };
    std::vector<Slot> slots_;
    std::array<int,128> index_;
    int factor_;
    int index(int channel)const {
        check(channel>=0&&channel<128,"score channel out of range");
        check(index_[size_t(channel)]>=0,"score channel was not configured");
        return index_[size_t(channel)];
    }
public:
    // Allocate only at setup, once per configured score channel, not per voice.
    ChannelBusPrototype(int factor,std::span<const int> channels):factor_(factor) {
        validateFactor(factor);index_.fill(-1);slots_.reserve(channels.size());
        for(int ch:channels){check(ch>=0&&ch<128,"score channel out of range");
            check(index_[size_t(ch)]==-1,"duplicate configured score channel");
            index_[size_t(ch)]=int(slots_.size());slots_.emplace_back(ch,factor);}
    }
    Stereo frame(std::span<const ChannelInput> input) {
        std::array<const ChannelInput*,128> byChannel{};
        // Validate the whole batch before any channel advances.
        for(const auto& x:input){(void)index(x.channel);
            check(!byChannel[size_t(x.channel)],"duplicate input channel");
            check(x.fm.empty()||x.fm.size()==size_t(factor_),"wrong channel subframe count");
            check(std::isfinite(x.native.left)&&std::isfinite(x.native.right),"nonfinite native input");
            for(const auto& v:x.fm)check(std::isfinite(v.left)&&std::isfinite(v.right),"nonfinite FM input");
            byChannel[size_t(x.channel)]=&x;}
        std::array<Stereo,32> zeros{};Stereo mixed{};
        for(auto& slot:slots_){const auto* x=byChannel[size_t(slot.channel)];
            if(!x&&slot.pending==0)continue;
            auto fm=x&&!x->fm.empty()?x->fm:std::span<const Stereo>(zeros.data(),size_t(factor_));
            const Stereo native=x?x->native:Stereo{};
            bool nonzero=native.left!=0||native.right!=0;
            if(x)for(const auto& v:x->fm)nonzero=nonzero||v.left!=0||v.right!=0;
            if(nonzero)slot.pending=slot.bus.tailBoundFrames();
            if(slot.pending==0)continue;
            const auto y=slot.bus.frame(fm.data(),fm.size(),native);
            mixed.left+=y.left;mixed.right+=y.right;
            if(!nonzero&&--slot.pending==0)slot.bus.reset();
        }
        return mixed;
    }
    // Purge at the caller's chosen processing boundary. Caller must also stop
    // source voices. This does not define delayed score AllSoundOff semantics.
    void purge(int channel){auto& s=slots_[size_t(index(channel))];s.bus.reset();s.pending=0;}
    void reset(){for(auto& s:slots_){s.bus.reset();s.pending=0;}}
    int pendingFrames()const {int frames=0;for(const auto& s:slots_)frames=std::max(frames,s.pending);return frames;}
    size_t activeChannels()const {size_t n=0;for(const auto& s:slots_)n+=s.pending!=0;return n;}
};
} // namespace fm_causal
