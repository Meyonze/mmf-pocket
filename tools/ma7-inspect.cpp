// SPDX-License-Identifier: Apache-2.0
// Anonymous structural probe for local SMAF Format 3 (MA-7/SEQU) files.

#include "smaf_file.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using fxchain::smaf::SmafFile;

namespace {

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

bool vlq(const uint8_t*& p, const uint8_t* end, uint32_t& value) {
    value = 0;
    for (int i = 0; i < 4; ++i) {
        if (p == end) return false;
        uint8_t b = *p++;
        value = (value << 7) | (b & 0x7f);
        if (!(b & 0x80)) return true;
    }
    return false;
}

std::string hexPrefix(const uint8_t* p, size_t n, size_t max = 6) {
    static constexpr char h[] = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < std::min(n, max); ++i) {
        if (i) out += ' ';
        out += h[p[i] >> 4];
        out += h[p[i] & 15];
    }
    return out;
}

struct SeqStats {
    bool ok = true;
    bool eos = false;
    size_t bytes = 0;
    uint64_t ticks = 0;
    uint64_t notes = 0;
    uint64_t controls = 0;
    uint64_t sysex = 0;
    uint64_t nop = 0;
    std::array<uint64_t, 32> channelEvents{};
    std::map<int, uint64_t> eventCodes;
    std::map<std::string, uint64_t> sysexPrefixes;
    size_t errorOffset = 0;
    std::string errorContext;
};

SeqStats inspectSequ(const std::vector<uint8_t>& data) {
    SeqStats s;
    const uint8_t* begin = data.data();
    const uint8_t* p = begin;
    const uint8_t* end = begin + data.size();
    size_t guard = 0;
    while (p < end && guard++ < 4000000) {
        uint32_t duration = 0;
        if (!vlq(p, end, duration) || p == end) { s.ok = false; break; }
        s.ticks += duration;
        uint8_t e1 = *p++;
        if (e1 == 0xf0) {
            uint32_t len = 0;
            if (!vlq(p, end, len) || len > size_t(end - p)) { s.ok = false; break; }
            ++s.sysexPrefixes[hexPrefix(p, len)];
            p += len;
            ++s.sysex;
        } else if (e1 == 0xff) {
            if (p == end) { s.ok = false; break; }
            uint8_t e2 = *p++;
            if (e2 == 0x00) {
                ++s.nop;
            } else if (e2 == 0x2f) {
                if (p == end || *p++ != 0x00) { s.ok = false; break; }
                s.eos = true;
                if (p != end) s.ok = false;
                break;
            } else {
                s.errorOffset = size_t(p - begin - 2);
                s.errorContext = "ff=" + std::to_string(e2) + " bytes=" +
                                 hexPrefix(p - 2, size_t(end - (p - 2)), 16);
                s.ok = false;
                break;
            }
        } else if ((e1 & 0x70) <= 0x60) {
            int channel = (e1 & 0x0f) + ((e1 & 0x80) ? 16 : 0);
            int event = e1 & 0x70;
            ++s.channelEvents[channel];
            ++s.eventCodes[event];
            if (event == 0x00 || event == 0x10) {
                size_t fixed = event == 0x10 ? 2 : 1; // note[, velocity]
                if (size_t(end - p) < fixed) { s.ok = false; break; }
                p += fixed;
                uint32_t gate = 0;
                if (!vlq(p, end, gate)) { s.ok = false; break; }
                ++s.notes;
            } else {
                size_t fixed = (event == 0x40 || event == 0x50) ? 1 : 2;
                if (size_t(end - p) < fixed) { s.ok = false; break; }
                p += fixed;
                ++s.controls;
            }
        } else {
            s.errorOffset = size_t(p - begin - 1);
            s.errorContext = "status=" + std::to_string(e1) + " bytes=" +
                             hexPrefix(p - 1, size_t(end - (p - 1)), 16);
            s.ok = false;
            break;
        }
    }
    if (guard >= 4000000) s.ok = false;
    s.bytes = size_t(p - begin);
    return s;
}

void inspectSetup(const std::vector<uint8_t>& data,
                  std::map<std::string, uint64_t>& prefixes,
                  std::map<std::string, uint64_t>& signatures,
                  std::map<uint32_t, uint64_t>& lengths,
                  uint64_t& good, uint64_t& bad) {
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();
    while (p < end) {
        if (*p == 0xff) { ++p; if (p == end) break; }
        if (*p++ != 0xf0) { ++bad; continue; }
        uint32_t len = 0;
        if (!vlq(p, end, len) || len > size_t(end - p)) { ++bad; break; }
        ++prefixes[hexPrefix(p, len)];
        ++signatures["len=" + std::to_string(len) + " " + hexPrefix(p, len, 80)];
        ++lengths[len];
        ++good;
        p += len;
    }
}

int run(const fs::path& root) {
    uint64_t files = 0, tracks = 0, seqOk = 0, seqBad = 0, eos = 0;
    uint64_t notes = 0, controls = 0, inlineSysex = 0, setupGood = 0, setupBad = 0;
    uint64_t totalSeqBytes = 0;
    std::array<uint64_t, 32> channels{};
    std::map<int, uint64_t> events;
    std::map<std::string, uint64_t> setupPrefixes, setupSignatures, inlinePrefixes;
    std::map<uint32_t, uint64_t> setupLengths;
    std::map<int, uint64_t> sequenceTypes, channelStatusValues;
    size_t anonymousIndex = 0;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mmf") continue;
        SmafFile f;
        auto bytes = readFile(entry.path());
        if (!f.parse(bytes.data(), bytes.size())) continue;
        bool countedFile = false;
        for (const auto& t : f.tracks) {
            if (t.isAudioTrack || t.formatType != 3) continue;
            if (!countedFile) { ++files; countedFile = true; ++anonymousIndex; }
            ++tracks;
            ++sequenceTypes[t.sequenceType];
            for (uint8_t v : t.channelStatus) ++channelStatusValues[v];
            inspectSetup(t.setupData, setupPrefixes, setupSignatures, setupLengths, setupGood, setupBad);
            auto s = inspectSequ(t.sequenceData);
            totalSeqBytes += t.sequenceData.size();
            notes += s.notes; controls += s.controls; inlineSysex += s.sysex;
            eos += s.eos;
            for (size_t i = 0; i < channels.size(); ++i) channels[i] += s.channelEvents[i];
            for (auto [k, v] : s.eventCodes) events[k] += v;
            for (auto& [k, v] : s.sysexPrefixes) inlinePrefixes[k] += v;
            if (s.ok && s.bytes == t.sequenceData.size()) ++seqOk;
            else {
                ++seqBad;
                std::cout << "bad_sequence anonymous=" << anonymousIndex
                          << " consumed=" << s.bytes << '/' << t.sequenceData.size()
                          << " error=" << s.errorOffset << ' ' << s.errorContext
                          << " prefix=" << hexPrefix(t.sequenceData.data(), t.sequenceData.size(), 48)
                          << '\n';
            }
        }
    }

    std::cout << "files=" << files << " tracks=" << tracks
              << " seq_ok=" << seqOk << " seq_bad=" << seqBad << " eos=" << eos << '\n';
    std::cout << "sequence_bytes=" << totalSeqBytes << " notes=" << notes
              << " controls=" << controls << " inline_sysex=" << inlineSysex << '\n';
    std::cout << "channels";
    for (size_t i = 0; i < channels.size(); ++i) std::cout << " ch" << i << '=' << channels[i];
    std::cout << '\n';
    std::cout << "sequence_types";
    for (auto [k, v] : sequenceTypes) std::cout << " 0x" << std::hex << k << std::dec << '=' << v;
    std::cout << '\n';
    std::cout << "event_codes";
    for (auto [k, v] : events) std::cout << " 0x" << std::hex << k << std::dec << '=' << v;
    std::cout << '\n';
    std::cout << "channel_status_values";
    for (auto [k, v] : channelStatusValues) std::cout << " 0x" << std::hex << k << std::dec << '=' << v;
    std::cout << '\n';
    std::cout << "setup_sysex good=" << setupGood << " bad=" << setupBad << '\n';
    for (auto& [k, v] : setupPrefixes) std::cout << "setup_prefix " << v << "\t" << k << '\n';
    for (auto& [k, v] : setupSignatures) std::cout << "setup_signature " << v << "\t" << k << '\n';
    for (auto& [k, v] : setupLengths) std::cout << "setup_length " << v << "\t" << k << '\n';
    for (auto& [k, v] : inlinePrefixes) std::cout << "inline_prefix " << v << "\t" << k << '\n';
    return seqBad || setupBad ? 1 : 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    return run(fs::path(argv[1]));
}
#else
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    return run(fs::path(argv[1]));
}
#endif
