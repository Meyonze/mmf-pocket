// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace fxchain::smaf {

// Init-time coverage inventory, NOT a fidelity score or proof of audible use.
// Runtime voice allocation/fallback counters belong to MaPlayer::Diagnostics.
struct ScoreAudit {
    enum class Domain : uint8_t { Controller, HpsLong, HpsShort, Sequence,
                                  Setup, VoiceDefinition, WaveDefinition };
    enum class Status : uint8_t { Routed, Conditional, Unapplied, Compatibility,
                                  Partial, Unsupported, Reserved, Malformed,
                                  ResourceLimit, Count };
    struct Group {
        Domain domain;
        Status status;
        int format, track, channel, opcode;
        uint64_t count, firstSample, lastSample;
        int minValue, maxValue;
    };
    static constexpr size_t kMaxGroups = 256;
    std::vector<Group> groups;
    std::array<uint64_t, size_t(Status::Count)> totals{};
    uint64_t overflowOccurrences = 0;
    uint64_t decodedTracks = 0;
    // False for incomplete grammar/resource decoding, not for known unsupported
    // synthesis. Inventory truncation is independently signalled by overflow.
    bool scanComplete = true;

    static void increment(uint64_t& n) {
        if (n != std::numeric_limits<uint64_t>::max()) ++n;
    }
    void record(Domain domain, Status status, int format, int track, int channel,
                int opcode, int value, uint64_t sample) {
        increment(totals[size_t(status)]);
        if (status == Status::Malformed || status == Status::ResourceLimit)
            scanComplete = false;
        for (auto& g : groups) {
            if (g.domain != domain || g.status != status || g.format != format ||
                g.track != track || g.channel != channel || g.opcode != opcode) continue;
            increment(g.count);
            g.firstSample = std::min(g.firstSample, sample);
            g.lastSample = std::max(g.lastSample, sample);
            g.minValue = std::min(g.minValue, value);
            g.maxValue = std::max(g.maxValue, value);
            return;
        }
        if (groups.size() == kMaxGroups) { increment(overflowOccurrences); return; }
        groups.push_back({domain, status, format, track, channel, opcode, 1,
                          sample, sample, value, value});
    }
    static const char* name(Status s) {
        switch (s) {
            case Status::Routed: return "routed";
            case Status::Conditional: return "conditional";
            case Status::Unapplied: return "unapplied";
            case Status::Compatibility: return "compatibility";
            case Status::Partial: return "partial";
            case Status::Unsupported: return "unsupported";
            case Status::Reserved: return "reserved";
            case Status::Malformed: return "malformed";
            case Status::ResourceLimit: return "resource_limit";
            default: return "invalid";
        }
    }
};
} // namespace fxchain::smaf
