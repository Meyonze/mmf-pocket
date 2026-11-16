// SPDX-License-Identifier: Apache-2.0
// CPU cost of strong serial (nested) FM through the real MaPlayer render loop.
// Original synthetic voices only: no song, manufacturer patch or ROM asset.
// Includes the player/filters/limiter, unlike tools/synth-benchmark.cpp, which
// times bare voices. Not an AudioTrack, battery or thermal measurement.
#define main existingNestedPlayerMain
#include "fm-nested-player-test.cpp"
#undef main
#include <chrono>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <algorithm>

using namespace fxchain::smaf;
namespace {
constexpr int sampleRate = 44100;

double cpuSeconds() {
    timespec t{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return double(t.tv_sec) + double(t.tv_nsec) * 1e-9;
}

// Condition: how the player is configured and which strong/weak voice is played.
//  native      app options {8,Hermite,boost}; TL12 modulators are declined, so the
//              native-rate tick plus alignment delay is used.
//  r91_weak16  app options; TL24 modulators are admitted at fixed 16x.
//  adaptiveNN  adaptiveNested on; the multiplier is chosen so the density table
//              selects NN (16/32/64/128) for TL12 modulators.
struct Condition { const char* name; int mode; int factor; int tl; double q; };
constexpr Condition conditions[] = {
    {"native_tl12_declined", 0, 1, 12, 0.04},
    {"r91_weak_tl24_16x", 0, 16, 24, 0.04},
    {"adaptive_tl12_16x", 1, 16, 12, 0.04},
    {"adaptive_tl12_32x", 1, 32, 12, 0.08},
    {"adaptive_tl12_64x", 1, 64, 12, 0.16},
    {"adaptive_tl12_128x", 1, 128, 12, 0.32},
};

struct Sample { double cpuPerAudioSecond, wallPerAudioSecond, checksum; bool ok; };

Sample run(const Condition& c, int voices, int frames) {
    MaPlayer p;
    SineRenderOptions o{8, FmSubframeMath::Hermite, true};
    if (c.mode == 1) o = {8, FmSubframeMath::Hermite, true, false, true};
    if (!p.init(nested_player::clockFile(), sampleRate, false, o)) return {0, 0, 0, false};
    p.endSample_ = uint64_t(frames) + 100000;
    p.scoreEndSample_ = p.endSample_;
    const auto patch = nested::patch({96, 7, 3, 5, c.tl, c.tl});
    for (int i = 0; i < voices; ++i) {
        auto& v = p.pool_[size_t(i)];
        v.noteOn(patch, c.q * sampleRate / 7 * (1 - .002 * i), .05f);
        v.channel = 0;
        p.poolPanLeft_[size_t(i)] = .7f;
        p.poolPanRight_[size_t(i)] = .3f;
    }
    std::vector<float> buffer(2048);
    double checksum = 0;
    (void)p.render(buffer.data(), 512); // warm-up: tables, banks, first blocks
    const double cpu0 = cpuSeconds();
    const auto wall0 = std::chrono::steady_clock::now();
    int done = 0;
    while (done < frames) {
        const int n = p.render(buffer.data(), 1024);
        if (n <= 0) break;
        done += n;
        checksum += buffer[0];
    }
    const double cpu = cpuSeconds() - cpu0;
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    if (done < frames) return {0, 0, checksum, false};
    const double audio = double(done) / sampleRate;
    return {cpu / audio, wall / audio, checksum, true};
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}
} // namespace

int main(int argc, char** argv) {
    bool quick = false, reverse = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--quick")) quick = true;
        else if (!std::strcmp(argv[i], "--reverse")) reverse = true;
        else {
            std::cerr << "Usage: nested-fm-benchmark [--quick] [--reverse]\n"
                      << "CPU cost of strong serial FM via the real MaPlayer render loop, 1/8/32 voices.\n"
                      << "--quick: 0.5 s of audio, 1 repeat. Default 2 s, 5 repeats, median reported.\n"
                      << "--reverse: run conditions in reverse order (compare with a forward run).\n";
            return std::strcmp(argv[i], "--help") ? 2 : 0;
        }
    }
    const int frames = quick ? sampleRate / 2 : sampleRate * 2;
    const int repeats = quick ? 1 : 5;
    constexpr int voiceSet[] = {1, 8, 32};
    constexpr size_t count = std::size(conditions);
    struct Row { const Condition* c; int voices; std::vector<double> cpu, wall; double checksum; bool ok; };
    std::vector<Row> rows;
    for (size_t k = 0; k < count; ++k) {
        const Condition& c = conditions[reverse ? count - 1 - k : k];
        for (int voices : voiceSet) rows.push_back({&c, voices, {}, {}, 0, true});
    }
    for (int rep = 0; rep < repeats; ++rep)
        for (auto& r : rows) {
            const Sample s = run(*r.c, r.voices, frames);
            r.ok = r.ok && s.ok;
            r.cpu.push_back(s.cpuPerAudioSecond);
            r.wall.push_back(s.wallPerAudioSecond);
            r.checksum = s.checksum;
        }
    std::cout << std::setprecision(10)
        << "{\n  \"schema_version\": 1,\n"
        << "  \"scope\": \"MaPlayer render loop (voices, FIR, limiter); CPU seconds per second of audio, "
           "not AudioTrack underruns, battery or thermal behaviour\",\n"
        << "  \"sample_rate\": " << sampleRate << ", \"frames_per_case\": " << frames
        << ", \"repeats\": " << repeats << ", \"order\": \"" << (reverse ? "reverse" : "forward") << "\",\n"
        << "  \"results\": [\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::cout << "    {\"name\": \"" << r.c->name << "\", \"voices\": " << r.voices
                  << ", \"factor\": " << r.c->factor
                  << ", \"cpu_seconds_per_audio_second_median\": " << median(r.cpu)
                  << ", \"cpu_min\": " << *std::min_element(r.cpu.begin(), r.cpu.end())
                  << ", \"cpu_max\": " << *std::max_element(r.cpu.begin(), r.cpu.end())
                  << ", \"wall_seconds_per_audio_second_median\": " << median(r.wall)
                  << ", \"completed\": " << (r.ok ? "true" : "false")
                  << ", \"checksum\": " << r.checksum << "}" << (i + 1 == rows.size() ? "\n" : ",\n");
    }
    std::cout << "  ]\n}\n";
    for (const auto& r : rows) if (!r.ok) return 1;
    return 0;
}
