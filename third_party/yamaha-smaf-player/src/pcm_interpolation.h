// SPDX-License-Identifier: Apache-2.0
// Renderer-quality interpolation, not a model of proprietary handset DSP.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fxchain::smaf {
namespace pcm_detail {
constexpr int kTaps = 16;
constexpr int kPhases = 256;
using KernelTable = std::array<std::array<float, kTaps>, kPhases + 1>;

constexpr int kDownsampleTaps = 32;
constexpr int kDownsamplePhases = 128;
constexpr int kCutoffSteps = 32;
// Shared bounded storage; constructing in-place avoids a large initializer
// temporary on Android worker stacks. No per-voice or per-sample allocation.
struct DownsampleBank {
    using PhaseRows = std::array<std::array<float, kDownsampleTaps>, kDownsamplePhases + 1>;
    std::array<PhaseRows, kCutoffSteps + 1> weights{};
    DownsampleBank() {
        constexpr double pi = 3.14159265358979323846;
        for (int cutoff = 0; cutoff <= kCutoffSteps; ++cutoff) {
            const double bandwidth = double(cutoff) / kCutoffSteps;
            for (int phase = 0; phase <= kDownsamplePhases; ++phase) {
                auto& row = weights[cutoff][phase];
                double sum = 0;
                for (int tap = 0; tap < kDownsampleTaps; ++tap) {
                    const double distance = double(tap - 15) - double(phase) / kDownsamplePhases;
                    // cutoff0 is the normalized limit as bandwidth -> 0.
                    const double sinc = cutoff == 0 || std::fabs(distance) < 1e-12 ? 1.0
                        : std::sin(pi * bandwidth * distance) / (pi * bandwidth * distance);
                    const double window = 0.42 + 0.5 * std::cos(pi * distance / 16.0)
                        + 0.08 * std::cos(2 * pi * distance / 16.0);
                    row[tap] = float(sinc * window);
                    sum += row[tap];
                }
                for (float& weight : row) weight = float(weight / sum);
                // Deliberately no delta at integer phases: decimation by2/4
                // must still low-pass even when every source position is integer.
            }
        }
    }
};

inline const DownsampleBank& downsamplingKernels() {
    static const DownsampleBank bank;
    return bank;
}

inline const KernelTable& interpolationKernels() {
    static const KernelTable table = [] {
        KernelTable result{};
        constexpr double pi = 3.14159265358979323846;
        for (int phase = 0; phase <= kPhases; ++phase) {
            double sum = 0.0;
            for (int tap = 0; tap < kTaps; ++tap) {
                const double distance = double(tap - 7) - double(phase) / kPhases;
                const double sinc = std::fabs(distance) < 1e-12 ? 1.0
                    : std::sin(pi * distance) / (pi * distance);
                const double window = 0.42 + 0.5 * std::cos(pi * distance / 8.0)
                    + 0.08 * std::cos(2.0 * pi * distance / 8.0);
                result[phase][tap] = float(sinc * window);
                sum += result[phase][tap];
            }
            for (float& weight : result[phase]) weight = float(weight / sum);
        }
        // Exact source samples at integer phase, including table endpoint.
        result[0].fill(0); result[0][7] = 1;
        result[kPhases].fill(0); result[kPhases][8] = 1;
        return result;
    }();
    return table;
}

inline size_t sampleIndex(int64_t index, size_t end, bool loop,
                          size_t loopStart, size_t loopTravel) {
    // end and loopStart are clamped by the owning voice. Keep pre-loop attack
    // history until the first traversal; later history is the periodic loop.
    const int64_t virtualIndex = index + int64_t(loopTravel);
    if (loop && loopStart < end && virtualIndex >= int64_t(end)) {
        const int64_t length = int64_t(end - loopStart);
        const int64_t relative = (virtualIndex - int64_t(end)) % length;
        return loopStart + size_t(relative);
    }
    if (virtualIndex < 0) return 0;
    if (virtualIndex >= int64_t(end)) return end - 1;
    return size_t(virtualIndex);
}

inline float downsamplePcm(const int16_t* pcm, size_t end, size_t index,
                          float fraction, bool loop, size_t loopStart,
                          size_t loopTravel, double advance, float linear) {
    const float phase = fraction * kDownsamplePhases;
    const int phaseRow = std::min(kDownsamplePhases - 1, int(phase));
    const float phaseBlend = phase - phaseRow;
    const float cutoff = float(kCutoffSteps / advance);
    const int cutoffRow = std::min(kCutoffSteps - 1, int(cutoff));
    const float cutoffBlend = cutoff - cutoffRow;
    const auto& bank = downsamplingKernels().weights;
    const auto& a = bank[cutoffRow][phaseRow];
    const auto& b = bank[cutoffRow][phaseRow + 1];
    const auto& c = bank[cutoffRow + 1][phaseRow];
    const auto& d = bank[cutoffRow + 1][phaseRow + 1];
    float filtered = 0;
    const bool interior = index >= 15 && index + 16 < end
        && (!loop || loopTravel == 0 || index >= loopStart + 15);
    auto weightAt = [&](int tap) {
        const float low = a[tap] + (b[tap] - a[tap]) * phaseBlend;
        const float high = c[tap] + (d[tap] - c[tap]) * phaseBlend;
        return low + (high - low) * cutoffBlend;
    };
    if (interior) {
        const int16_t* samples = pcm + index - 15;
        for (int tap = 0; tap < kDownsampleTaps; ++tap)
            filtered += samples[tap] * (1.0f / 32768.0f) * weightAt(tap);
    } else {
        for (int tap = 0; tap < kDownsampleTaps; ++tap) {
            const size_t source = sampleIndex(int64_t(index) + tap - 15,
                                             end, loop, loopStart, loopTravel);
            filtered += pcm[source] * (1.0f / 32768.0f) * weightAt(tap);
        }
    }
    // Preserve the established unity boundary and smoothly engage AA over
    //1..1.1. Near unity this compromise attenuates alias less than a full FIR.
    const double t = std::min(1.0, (advance - 1.0) / 0.1);
    const float mix = float(t * t * (3.0 - 2.0 * t));
    return linear + mix * (filtered - linear);
}

inline float interpolatePcm(const int16_t* pcm, size_t end, size_t index,
                            float fraction, bool loop, size_t loopStart,
                            size_t loopTravel, bool modern, double advance,
                            float* linearOutput = nullptr) {
    const size_t next = index + 1 < end ? index + 1 : (loop ? loopStart : index);
    const float s0 = pcm[index] * (1.0f / 32768.0f);
    const float s1 = pcm[next] * (1.0f / 32768.0f);
    const float linear = s0 + (s1 - s0) * fraction;
    if (linearOutput) *linearOutput = linear;
    // Profiles not opting in remain exactly linear; unity is exact too.
    if (!modern || !std::isfinite(advance)) return linear;
    if (advance > 1.0) return downsamplePcm(pcm, end, index, fraction,
        loop, loopStart, loopTravel, advance, linear);
    if (advance == 1.0) return linear;
    const float phase = fraction * kPhases;
    const int row = std::min(kPhases - 1, int(phase));
    const float blend = phase - row;
    const auto& table = interpolationKernels();
    float reconstructed = 0;
    const bool interior = index >= 7 && index + 8 < end
        && (!loop || loopTravel == 0 || index >= loopStart + 7);
    if (interior) {
        // Most ticks are away from loop/attack boundaries. Avoid signed modulo
        // and branch-heavy history resolution per tap on the common path.
        const int16_t* samples = pcm + index - 7;
        for (int tap = 0; tap < kTaps; ++tap) {
            const float weight = table[row][tap]
                + (table[row + 1][tap] - table[row][tap]) * blend;
            reconstructed += samples[tap] * (1.0f / 32768.0f) * weight;
        }
    } else {
        for (int tap = 0; tap < kTaps; ++tap) {
            const size_t source = sampleIndex(int64_t(index) + tap - 7,
                                              end, loop, loopStart, loopTravel);
            const float weight = table[row][tap]
                + (table[row + 1][tap] - table[row][tap]) * blend;
            reconstructed += pcm[source] * (1.0f / 32768.0f) * weight;
        }
    }
    const double t = advance <= 0.8 ? 1.0 : (1.0 - advance) / 0.2;
    const double mix = t * t * (3.0 - 2.0 * t);
    return linear + float(mix) * (reconstructed - linear);
}
} // namespace pcm_detail
} // namespace fxchain::smaf
