// SPDX-License-Identifier: Apache-2.0
// Host-only hard-stop timing policy experiment, not a player API.
#pragma once
#include "fm-causal-decimator.h"
#include <span>
namespace fm_causal {
class TimedStopPrototype {
    static constexpr size_t delay=AlignedBus::commonDelay,historyFrames=delay+1;
    int factor_;
    AlignedBus bus_;
    // Enough to rebuild from a cut's input boundary to its output boundary.
    std::vector<Stereo> fmHistory_;
    std::array<Stereo,historyFrames> nativeHistory_{};
    std::array<bool,historyFrames> stops_{};
    size_t cursor_=0;
    int audioPending_=0;
    uint64_t replayedFrames_=0;
    Stereo advance(size_t slot) {
        const auto* fm=fmHistory_.data()+slot*size_t(factor_);
        const auto native=nativeHistory_[slot];bool nonzero=native.left!=0||native.right!=0;
        for(int j=0;j<factor_;++j)nonzero=nonzero||fm[j].left!=0||fm[j].right!=0;
        if(nonzero)audioPending_=bus_.tailBoundFrames();
        if(audioPending_==0)return {};
        const auto y=bus_.frame(fm,size_t(factor_),native);
        if(!nonzero&&--audioPending_==0)bus_.reset();return y;
    }
public:
    explicit TimedStopPrototype(int factor):factor_(factor),bus_(factor),fmHistory_(historyFrames*size_t(factor)){}
    // Caller kills source voices in score order before submitting this frame.
    // A stop applies at input boundary n + commonDelay; same-frame new input
    // is retained (for a NoteOn ordered after the stop). Ordinary NoteOff is
    // handled by the source and must not set hardStop.
    Stereo frame(std::span<const Stereo> fm,Stereo native={},bool hardStop=false) {
        check(fm.empty()||fm.size()==size_t(factor_),"wrong timed-stop subframe count");
        check(std::isfinite(native.left)&&std::isfinite(native.right),"nonfinite timed-stop native input");
        for(const auto& x:fm)check(std::isfinite(x.left)&&std::isfinite(x.right),"nonfinite timed-stop FM input");
        auto* dest=fmHistory_.data()+cursor_*size_t(factor_);
        if(fm.empty())std::fill_n(dest,size_t(factor_),Stereo{});else std::copy(fm.begin(),fm.end(),dest);
        nativeHistory_[cursor_]=native;stops_[cursor_]=hardStop;
        const size_t due=(cursor_+1)%historyFrames;Stereo y;
        if(stops_[due]) {
            bus_.reset();audioPending_=0;
            // Reconstruct only samples at/after the stopped generation's end.
            // Includes later notes already submitted during the common delay.
            for(size_t i=0;i<historyFrames;++i)y=advance((due+i)%historyFrames);
            replayedFrames_+=historyFrames;stops_[due]=false;
        } else y=advance(cursor_);
        cursor_=due;return y;
    }
    void reset(){bus_.reset();std::fill(fmHistory_.begin(),fmHistory_.end(),Stereo{});nativeHistory_.fill({});
        stops_.fill(false);cursor_=0;audioPending_=0;replayedFrames_=0;}
    int pendingFrames()const {
        int remaining=audioPending_;
        for(size_t age=0;age<delay;++age)if(stops_[(cursor_+historyFrames-1-age)%historyFrames])
            remaining=std::max(remaining,int(delay-age));
        return remaining;
    }
    uint64_t replayedFrames()const{return replayedFrames_;}
    size_t retainedInputFrames()const{return historyFrames;}
};
} // namespace fm_causal
