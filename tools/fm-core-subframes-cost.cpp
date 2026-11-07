// SPDX-License-Identifier: Apache-2.0
// Public-source-only host timing. No FIR, mixer, I/O or Android speed claim.
#include "ma_fm_core.h"
#include "ma_subframe_sine.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace fxchain::smaf;

int main() {
    constexpr int frames = 4096;
    (void)subframeMath::coefficients(); // exclude first-use preparation
    double checksum = 0;
    std::cout << std::setprecision(12)
              << "{\"scope\":\"Public FM source only, shared host; excludes FIR/mixing/I/O and setup; not Android performance\","
              << "\"sample_rate\":44100,\"frames\":" << frames << ",\"results\":[";
    bool first = true;
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int algorithm : {0, 5}) for (int count : {1, 8, 32}) {
            // Rotate order to avoid always measuring one method first.
            for (int order = 0; order < 11; ++order) {
                const int index = (order + 4 * repeat) % 11;
                const int factor = index == 0 ? 1 : 1 << ((index + 1) / 2);
                const bool fast = index != 0 && index % 2 == 0;
                FmVoicePatch patch;
                patch.modernTimbre = true;
                patch.fourOp = algorithm == 5;
                patch.algorithm = uint8_t(algorithm);
                patch.lfo = 2;
                const int multis[] = {3, 2, 7, 5};
                for (int j = 0; j < 4; ++j) {
                    auto& op = patch.ops[size_t(j)];
                    op.wave = 0; op.multi = uint8_t(multis[j]);
                    op.tl = uint8_t(j % 2 ? 0 : 24);
                    op.ar = 15; op.dr = op.sr = op.sl = 0;
                    op.am = op.vib = true; op.dam = op.dvb = 2;
                }
                std::vector<FmVoice> voices(static_cast<size_t>(count));
                for (int i = 0; i < count; ++i) {
                    auto& voice = voices[size_t(i)];
                    voice.setSampleRate(44100);
                    voice.noteOn(patch, 220 + 9 * i, .6f);
                    for (int n = 0; n < 512; ++n) (void)voice.tick();
                }
                const auto start = std::chrono::steady_clock::now();
                for (int n = 0; n < frames; ++n) for (auto& voice : voices) {
                    if (factor == 1) checksum += voice.tick();
                    else {
                        std::array<float, 32> output;
                        if (!voice.tickSineSubframes(output.data(), factor,
                                fast ? FmSubframeMath::Hermite : FmSubframeMath::Exact)) return 1;
                        // Consume interiors as a real downstream stage would.
                        for (int i = 0; i < factor; ++i) checksum += output[size_t(i)];
                    }
                }
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                if (!std::isfinite(ms) || !std::isfinite(checksum)) return 1;
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"repeat\":" << repeat << ",\"algorithm\":" << algorithm
                          << ",\"voices\":" << count << ",\"factor\":" << factor
                          << ",\"math\":\"" << (factor == 1 ? "native" : fast ? "hermite" : "exact")
                          << "\",\"ms\":" << ms << '}';
            }
        }
    }
    std::cout << "],\"checksum\":" << checksum << "}\n";
}
