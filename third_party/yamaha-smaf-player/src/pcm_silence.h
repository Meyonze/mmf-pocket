// SPDX-License-Identifier: Apache-2.0
// Exact range-silence index for immutable decoded PCM. No amplitude threshold.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fxchain::smaf::pcm_detail {
class SilenceIndex {
public:
    // Rebuild after decoding/replacing a sample, never during a note's life.
    void build(std::span<const int16_t> samples) {
        size_=samples.size();
        prefix_=size_t(std::find_if(samples.begin(),samples.end(),nonzero)-samples.begin());
        suffix_=size_t(std::find_if(samples.rbegin(),samples.rend(),nonzero).base()-samples.begin());
        nonzeroBlocks_.clear();
        if(prefix_==size_)return; // All-zero data needs no block table.
        nonzeroBlocks_.reserve(size_/block+(size_%block!=0)+1);
        nonzeroBlocks_.push_back(0);
        for(size_t first=0;first<size_;first+=block){
            const size_t last=std::min(size_,first+block);
            const bool occupied=std::any_of(samples.begin()+first,samples.begin()+last,nonzero);
            // Decoded inputs are bounded to tens of MiB; uint32_t counts blocks.
            nonzeroBlocks_.push_back(nonzeroBlocks_.back()+uint32_t(occupied));
        }
    }
    bool zero(std::span<const int16_t> samples,size_t first,size_t last) const {
        if(first>last||last>samples.size())return false;
        if(first==last)return true;
        // Safe fallback for an unprepared source; production decoders build it.
        if(size_!=samples.size())return scan(samples,first,last);
        if(last<=prefix_||first>=suffix_)return true;
        if(first<=prefix_&&prefix_<last)return false; // First nonzero is played.
        if(nonzeroBlocks_.empty())return scan(samples,first,last); // Valid moved-from cache.
        const size_t fullBegin=first/block+(first%block!=0),fullEnd=last/block;
        if(fullBegin>=fullEnd)return scan(samples,first,last); // At most126 samples.
        if(nonzeroBlocks_[fullBegin]!=nonzeroBlocks_[fullEnd])return false;
        return scan(samples,first,fullBegin*block)&&scan(samples,fullEnd*block,last);
    }
private:
    static constexpr size_t block=64;
    static bool nonzero(int16_t value){return value!=0;}
    static bool scan(std::span<const int16_t> samples,size_t first,size_t last) {
        return std::none_of(samples.begin()+first,samples.begin()+last,nonzero);
    }
    size_t size_=0,prefix_=0,suffix_=0;
    // Prefix count of occupied64-sample blocks: about3.1% of decoded PCM16.
    std::vector<uint32_t> nonzeroBlocks_;
};
} // namespace fxchain::smaf::pcm_detail
