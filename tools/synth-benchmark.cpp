// SPDX-License-Identifier: Apache-2.0
// Original synthetic workloads. No song, manufacturer patch, or ROM assets.
#include "smaf_file.h"
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <array>
#include <map>
#include <vector>
// Benchmark the real private PCM renderer without adding a production API.
// Include its dependencies first, as in high-rate-resample-test.cpp.
#define private public
#include "ma_player.h"
#undef private
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace fxchain::smaf;
namespace {
constexpr unsigned sampleRate = 44100, voiceCount = 32;
struct Result { std::string name; double seconds, checksum, setupSeconds, maxBlockSeconds; unsigned lateBlocks; };
template<class Voices> Result measure(const std::string& name, Voices& voices, unsigned frames, std::chrono::steady_clock::time_point setupStart) {
    // Exclude lazy waveform/filter table initialization and a short warmup.
    for (unsigned f = 0; f < 512; ++f) for (auto& v : voices) (void)v.tick();
    const double setupSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - setupStart).count();
    double checksum = 0, maxBlockSeconds = 0;
    unsigned lateBlocks = 0;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned block = 0; block < frames; block += 1024) {
      const auto blockStart = std::chrono::steady_clock::now();
      for (unsigned f = 0; f < 1024; ++f) {
        double mixed = 0;
        for (auto& v : voices) mixed += v.tick();
        if (!std::isfinite(mixed)) throw std::runtime_error("nonfinite synthesis output");
        checksum += mixed * mixed + mixed * 0.001;
      }
      const double blockSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - blockStart).count();
      maxBlockSeconds = std::max(maxBlockSeconds, blockSeconds);
      if (blockSeconds > 1024.0 / sampleRate) ++lateBlocks;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!(seconds > 0) || !std::isfinite(seconds) || !std::isfinite(checksum) || checksum <= 0)
        throw std::runtime_error("invalid timing or silent/nonfinite checksum");
    return {name, seconds, checksum, setupSeconds, maxBlockSeconds, lateBlocks};
}
void usage(std::ostream& out) {
    out << "Usage: synth-benchmark [--quick] [--help]\n"
        << "CPU synthesis only: 32 voices, 44100 Hz, 22 original synthetic workloads.\n"
        << "Excludes AudioTrack underruns, device latency and power. JSON on stdout.\n"
        << "Default 98304 frames/case; --quick 4096. Warmup excluded from timing.\n";
}
}
int main(int argc, char** argv) {
    bool quick = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" && argc == 2) { usage(std::cout); return 0; }
        if (arg == "--quick" && !quick) quick = true;
        else { usage(std::cerr); return 2; }
    }
    try {
        const unsigned frames = quick ? 4096 : 98304;
        std::vector<Result> results;
        const unsigned waves[] = {0, 6, 16, 24};
        const char* names[] = {"sine", "square", "triangle", "saw"};
        for (unsigned w = 0; w < 4; ++w) for (unsigned algorithm : {0u, 7u}) for (bool lfo : {false, true}) {
            const auto setupStart = std::chrono::steady_clock::now();
            FmVoicePatch patch; // All parameters authored here; no bank patches.
            patch.fourOp = true; patch.algorithm = uint8_t(algorithm);
            patch.modernTimbre = true; patch.lfo = 2;
            for (unsigned op = 0; op < 4; ++op) {
                auto& p = patch.ops[op]; p.wave = uint8_t(waves[w]);
                p.multi = uint8_t(1 + op % 2); p.tl = uint8_t(12 + op * 3);
                p.ar = 15; p.dr = p.sr = p.sl = 0; p.egType = true;
                p.am = p.vib = lfo; p.dam = p.dvb = lfo ? 3 : 0;
            }
            std::array<FmVoice, voiceCount> voices;
            for (unsigned i = 0; i < voices.size(); ++i) {
                voices[i].setSampleRate(sampleRate);
                voices[i].noteOn(patch, 110 * std::exp2(double(i % 24) / 12), 0.75f);
            }
            results.push_back(measure(std::string("fm_") + names[w] + "_algo" + std::to_string(algorithm)
                + (lfo ? "_amvib_both" : "_amvib_none"), voices, frames, setupStart));
        }
        // A bounded, fixed-seed original generated source shared by 32 cursors.
        std::vector<int16_t> pcm(16384);
        uint32_t seed = 0x4d4d4650;
        for (size_t i = 0; i < pcm.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            pcm[i] = int16_t(int(seed >> 17) - 16384);
        }
        for (unsigned ratio : {4u, 8u, 16u}) for (bool quality : {false, true}) {
            const auto setupStart = std::chrono::steady_clock::now();
            std::array<MaPlayer::PcmVoice, voiceCount> voices;
            for (unsigned i = 0; i < voices.size(); ++i) {
                auto& v = voices[i]; v.pcm = pcm.data(); v.len = v.loopEnd = pcm.size();
                v.loop = v.active = v.streamAudio = true;
                v.rate = v.baseRate = ratio; v.pos = i * 37 + 0.375;
                v.qualityInterpolation = quality;
            }
            results.push_back(measure("pcm_ratio" + std::to_string(ratio)
                + (quality ? "_quality" : "_linear"), voices, frames, setupStart));
        }
        std::cout << std::setprecision(12)
            << "{\n  \"schema_version\": 1,\n  \"compiler\": \"" << __VERSION__ << "\",\n"
            << "  \"scope\": \"CPU synthesis only; excludes AudioTrack underruns, device latency and power\",\n"
            << "  \"workload_notes\": \"Original modernTimbre FM patches; synthetic looping streamAudio PCM stress; checksum=sum(mixed*mixed+0.001*mixed); setup_seconds includes voice initialization and 512-frame warmup, including first-use shared tables\",\n"
            << "  \"sample_rate\": " << sampleRate << ", \"voices\": " << voiceCount
            << ", \"frames_per_case\": " << frames << ", \"warmup_frames_per_case\": 512,\n"
            << "  \"realtime_ratio_definition\": \"rendered_audio_seconds / wall_seconds (higher is faster)\",\n"
            << "  \"block_frames\": 1024, \"block_timing_scope\": \"CPU deadline proxy only, not AudioTrack underruns\",\n"
            << "  \"results\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i]; const double audio = double(frames) / sampleRate;
            std::cout << "    {\"name\": \"" << r.name << "\", \"rendered_audio_seconds\": " << audio
                << ", \"wall_seconds\": " << r.seconds << ", \"realtime_ratio\": " << audio / r.seconds
                << ", \"setup_seconds\": " << r.setupSeconds
                << ", \"max_block_seconds\": " << r.maxBlockSeconds
                << ", \"blocks_over_realtime_budget\": " << r.lateBlocks
                << ", \"checksum\": " << r.checksum << "}" << (i + 1 == results.size() ? "\n" : ",\n");
        }
        std::cout << "  ]\n}\n";
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("failed to write JSON output");
    } catch (const std::exception& e) { std::cerr << "synth-benchmark: " << e.what() << '\n'; return 1; }
}
