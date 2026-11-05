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
template<int Taps, int CutoffSteps, int MaxCutoff = CutoffSteps,
         int Phases = kDownsamplePhases, int CutoffScale = 1>
struct DownsampleKernelBank {
    static_assert(MaxCutoff > 0 && MaxCutoff <= CutoffSteps && Phases > 0 && CutoffScale > 0);
    using PhaseRows = std::array<std::array<float, Taps>, Phases + 1>;
    std::array<PhaseRows, MaxCutoff + 1> weights{};
    DownsampleKernelBank() {
        constexpr double pi = 3.14159265358979323846;
        for (int cutoff = 0; cutoff <= MaxCutoff; ++cutoff) {
            const double bandwidth = double(cutoff) / (CutoffSteps * CutoffScale);
            for (int phase = 0; phase <= Phases; ++phase) {
                auto& row = weights[cutoff][phase];
                double sum = 0;
                for (int tap = 0; tap < Taps; ++tap) {
                    const double distance = double(tap - (Taps / 2 - 1)) - double(phase) / Phases;
                    // cutoff0 is the normalized limit as bandwidth -> 0.
                    const double sinc = cutoff == 0 || std::fabs(distance) < 1e-12 ? 1.0
                        : std::sin(pi * bandwidth * distance) / (pi * bandwidth * distance);
                    const double window = 0.42 + 0.5 * std::cos(pi * distance / double(Taps / 2))
                        + 0.08 * std::cos(2 * pi * distance / double(Taps / 2));
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

using DownsampleBank = DownsampleKernelBank<kDownsampleTaps, kCutoffSteps>;
// Above 4:1 cutoff can round to 16 in float. Keep row 17 as the zero-blend
// guard so endpoint interpolation preserves its original arithmetic exactly.
using HighRateBank = DownsampleKernelBank<64, 64, 17>;
// Above 8:1, bandwidth is at most 1/8. A narrower cutoff grid and eight
// fractional phases provide the longer kernel without a large shared table.
constexpr int kExtremeRateTaps = 128;
constexpr int kMaxPastSamples = kExtremeRateTaps / 2 - 1;
using ExtremeRateBank = DownsampleKernelBank<kExtremeRateTaps, 32, 32, 8, 8>;

inline const DownsampleBank& downsamplingKernels() {
    static const DownsampleBank bank;
    return bank;
}

// Higher reduction ratios need a longer source-domain filter. Keep the
// established 32-tap path exact through 4:1. Reachable rows plus guard occupy 594,432 B;
// the 128-tap bank below adds 152,064 B, less than the old full 64-tap bank.
inline const HighRateBank& highRateKernels() {
    static const HighRateBank bank;
    return bank;
}

inline const ExtremeRateBank& extremeRateKernels() {
    static const ExtremeRateBank bank;
    return bank;
}

inline void prepareDownsamplingKernels() {
    (void)downsamplingKernels();
    (void)highRateKernels();
    (void)extremeRateKernels();
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

// Sequential source indices for a boundary-spanning convolution. Resolve the
// first tap once, preserving attack extension and periodic loop history.
struct PcmBoundaryCursor {
    size_t source;
    int64_t virtualIndex;
    size_t end;
    size_t restart;

    PcmBoundaryCursor(int64_t first, size_t end, bool loop, size_t loopStart, size_t loopTravel)
        : source(sampleIndex(first, end, loop, loopStart, loopTravel)),
          virtualIndex(first + int64_t(loopTravel)), end(end),
          restart(loop && loopStart < end ? loopStart : end - 1) {}

    void advance() {
        if (virtualIndex >= 0) source = source + 1 < end ? source + 1 : restart;
        ++virtualIndex;
    }
};

template<int Taps, int CutoffSteps, int MaxCutoff, int Phases, int CutoffScale>
inline float filterPcm(const DownsampleKernelBank<Taps, CutoffSteps, MaxCutoff, Phases, CutoffScale>& kernels,
                          const int16_t* pcm, size_t end, size_t index,
                          float fraction, bool loop, size_t loopStart,
                          size_t loopTravel, double advance) {
    const float phase = fraction * Phases;
    const int phaseRow = std::min(Phases - 1, int(phase));
    const float phaseBlend = phase - phaseRow;
    const float cutoff = float((CutoffSteps * CutoffScale) / advance);
    const int cutoffRow = std::min(CutoffSteps - 1, int(cutoff));
    const float cutoffBlend = cutoff - cutoffRow;
    const auto& bank = kernels.weights;
    const auto& a = bank[cutoffRow][phaseRow];
    const auto& b = bank[cutoffRow][phaseRow + 1];
    const auto& c = bank[cutoffRow + 1][phaseRow];
    const auto& d = bank[cutoffRow + 1][phaseRow + 1];
    float filtered = 0;
    const bool interior = index >= size_t(Taps / 2 - 1) && index + Taps / 2 < end
        && (!loop || loopTravel == 0 || index >= loopStart + Taps / 2 - 1);
    auto weightAt = [&](int tap) {
        const float low = a[tap] + (b[tap] - a[tap]) * phaseBlend;
        const float high = c[tap] + (d[tap] - c[tap]) * phaseBlend;
        return low + (high - low) * cutoffBlend;
    };
    // Separate longer-kernel coefficient interpolation so it can use SIMD
    // while the source sum retains its order. No heap or persistent voice
    // storage. Compiler FMA choices may change float rounding (cache r54).
    // Leave the established 32-tap path together: splitting it can scalarize
    // its accumulation on Android, with little measured benefit.
    std::array<float, Taps> preparedWeights;
    if constexpr (Taps > 32)
        for (int tap = 0; tap < Taps; ++tap) preparedWeights[tap] = weightAt(tap);
    auto preparedWeightAt = [&](int tap) {
        if constexpr (Taps > 32) return preparedWeights[tap];
        else return weightAt(tap);
    };
    if (interior) {
        const int16_t* samples = pcm + index - (Taps / 2 - 1);
        for (int tap = 0; tap < Taps; ++tap)
            filtered += samples[tap] * (1.0f / 32768.0f) * preparedWeightAt(tap);
    } else if constexpr (Taps > 64) {
        // Resolve once, then walk contiguous source samples. Preserve start
        // extension until virtual time reaches zero and wrap only at loop end.
        // Avoiding a modulo per tap bounds the cost of the longer filter.
        int64_t virtualIndex = int64_t(index) + int64_t(loopTravel) - (Taps / 2 - 1);
        size_t source = sampleIndex(int64_t(index) - (Taps / 2 - 1), end, loop, loopStart, loopTravel);
        for (int tap = 0; tap < Taps; ++tap) {
            filtered += pcm[source] * (1.0f / 32768.0f) * preparedWeightAt(tap);
            if (virtualIndex >= 0) {
                if (source + 1 < end) ++source;
                else if (loop && loopStart < end) source = loopStart;
            }
            ++virtualIndex;
        }
    } else {
        PcmBoundaryCursor cursor(int64_t(index) - (Taps / 2 - 1), end, loop, loopStart, loopTravel);
        for (int tap = 0; tap < Taps; ++tap) {
            filtered += pcm[cursor.source] * (1.0f / 32768.0f) * preparedWeightAt(tap);
            cursor.advance();
        }
    }
    return filtered;
}

inline float downsamplePcm(const int16_t* pcm, size_t end, size_t index,
                          float fraction, bool loop, size_t loopStart,
                          size_t loopTravel, double advance, float linear) {
    if (advance > 8.0) {
        // Retain the established filter through 8:1; crossfade smoothly to
        // 128 taps over 8..10 for pitch bends and vibrato across the boundary.
        const float high = filterPcm(extremeRateKernels(), pcm, end, index,
            fraction, loop, loopStart, loopTravel, advance);
        if (advance >= 10.0) return high;
        const float low = filterPcm(highRateKernels(), pcm, end, index,
            fraction, loop, loopStart, loopTravel, advance);
        const double t = (advance - 8.0) / 2.0;
        return low + float(t * t * (3.0 - 2.0 * t)) * (high - low);
    }
    if (advance > 4.0) {
        const float high = filterPcm(highRateKernels(), pcm, end, index,
            fraction, loop, loopStart, loopTravel, advance);
        if (advance >= 5.0) return high;
        // Smoothly crossfade over 4..5, so pitch bend/vibrato crossing the
        // quality boundary cannot abruptly switch reconstruction filters.
        const float low = filterPcm(downsamplingKernels(), pcm, end, index,
            fraction, loop, loopStart, loopTravel, advance);
        const double t = advance - 4.0;
        return low + float(t * t * (3.0 - 2.0 * t)) * (high - low);
    }
    const float filtered = filterPcm(downsamplingKernels(), pcm, end, index,
        fraction, loop, loopStart, loopTravel, advance);
    // Preserve fractional-delay reconstruction instead of fading to linear.
    // Keep the old float arithmetic above1.1, where the fade was already full.
    return linear + (filtered - linear);
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
    // Legacy stays linear. A bend can leave a fractional native-rate cursor.
    if (!modern || !std::isfinite(advance)) return linear;
    if (advance > 1.0) return downsamplePcm(pcm, end, index, fraction,
        loop, loopStart, loopTravel, advance, linear);
    if (advance == 1.0) {
        if (fraction == 0.0f) return linear;
        const float filtered = filterPcm(downsamplingKernels(), pcm, end, index,
            fraction, loop, loopStart, loopTravel, 1.0);
        return linear + (filtered - linear);
    }
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
        PcmBoundaryCursor cursor(int64_t(index) - 7, end, loop, loopStart, loopTravel);
        for (int tap = 0; tap < kTaps; ++tap) {
            const size_t source = cursor.source;
            const float weight = table[row][tap]
                + (table[row + 1][tap] - table[row][tap]) * blend;
            reconstructed += pcm[source] * (1.0f / 32768.0f) * weight;
            cursor.advance();
        }
    }
    if (advance <= 0.8) return linear + (reconstructed - linear);
    // Widen the full-band interpolation kernel from16 to32 taps over0.8..1.
    // At unity it joins the32-tap downsampling kernel continuously.
    const float high = filterPcm(downsamplingKernels(), pcm, end, index,
        fraction, loop, loopStart, loopTravel, 1.0);
    const double t = (advance - 0.8) / 0.2;
    return reconstructed + float(t * t * (3.0 - 2.0 * t)) * (high - reconstructed);
}
} // namespace pcm_detail
} // namespace fxchain::smaf
