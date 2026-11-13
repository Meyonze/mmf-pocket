// SPDX-License-Identifier: Apache-2.0
// Synthetic renderer-quality checks, not handset or ROM calibration.
#include "smaf_file.h"
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <array>
#include <cstdint>
#include <map>
#include <vector>
#define private public
#include "ma_player.h"
#undef private
#include "pcm_interpolation.h"
#include "pcm-dynamic-regression.h"
#include "pcm-boundary-cursor-regression.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace fxchain::smaf;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

// Assemble a double-precision FIR on an unwrapped source timeline. No
// production kernels, phase/cutoff tables or loop-index helpers are used.
double reference(const std::vector<int16_t>& pcm, double position,
                 double rate, int taps, bool loop = false, size_t start = 0) {
    const auto whole = int64_t(std::floor(position));
    const double fraction = position - double(whole);
    double result = 0, sum = 0;
    for (int tap = 0; tap < taps; ++tap) {
        const double distance = tap - (taps / 2 - 1) - fraction;
        const double x = pi * distance / rate;
        const double weight = (std::fabs(x) < 1e-12 ? 1 : std::sin(x) / x)
            * (0.42 + 0.5 * std::cos(pi * distance / (taps / 2))
                    + 0.08 * std::cos(2 * pi * distance / (taps / 2)));
        int64_t index = whole + tap - (taps / 2 - 1);
        if (loop && index >= int64_t(pcm.size()))
            index = int64_t(start) + (index - int64_t(pcm.size())) % int64_t(pcm.size() - start);
        index = std::clamp(index, int64_t(0), int64_t(pcm.size() - 1));
        result += weight * pcm[size_t(index)] / 32768.0;
        sum += weight;
    }
    return result / sum;
}
std::vector<int16_t> sine(double frequency, size_t length = 16384) {
    std::vector<int16_t> pcm(length);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = int16_t(std::lround(12000 * std::sin(2 * pi * frequency * i)));
    return pcm;
}
float sample(const std::vector<int16_t>& pcm, double position, double rate,
             bool quality = true) {
    const size_t index = size_t(position);
    return pcm_detail::interpolatePcm(pcm.data(), pcm.size(), index,
        float(position - index), false, 0, 0, quality, rate);
}
void spectral() {
    for (double rate : {6.0, 8.0, 12.0}) {
        const auto stop = sine(0.7 / rate), pass = sine(0.35 / rate);
        double alias = 0, oldAlias = 0, passError = 0, oldPassError = 0;
        for (int n = 0; n < 1024; ++n) {
            const double pos = 128.25 + n * rate;
            const double actual = sample(stop, pos, rate);
            const double before = reference(stop, pos, rate, 32);
            alias += actual * actual; oldAlias += before * before;
            const double target = 12000.0 / 32768 * std::sin(2 * pi * (0.35 / rate) * pos);
            const double error = sample(pass, pos, rate) - target;
            const double oldError = reference(pass, pos, rate, 32) - target;
            passError += error * error; oldPassError += oldError * oldError;
        }
        require(alias < oldAlias * (rate <= 8 ? 0.0631 : 0.20),
                "high-rate stopband improves at least 12 dB at 6/8, 7 dB at 12");
        require(passError < oldPassError,
                "high-rate passband sine error also decreases");
        std::cout << "ratio=" << rate << " alias RMS " << std::sqrt(oldAlias / 1024)
                  << " -> " << std::sqrt(alias / 1024)
                  << ", improvement dB=" << 10 * std::log10(oldAlias / alias) << '\n';
    }
}
void boundaries() {
    for (size_t length : {size_t(1), size_t(3), size_t(9), size_t(63), size_t(79), size_t(129), size_t(257)}) {
        std::vector<int16_t> pcm(length);
        for (size_t i = 0; i < length; ++i) pcm[i] = int16_t(int(i * 7919 % 24001) - 12000);
        for (double rate : {8.0, 16.0, 32.0, 64.0}) for (bool loop : {false, true}) {
            // Includes one-sample loops and attack history across many wraps.
            for (size_t start : {size_t(0), length - 1}) {
                MaPlayer::PcmVoice voice;
                voice.pcm = pcm.data(); voice.len = voice.loopEnd = length;
                voice.loop = loop; voice.loopStart = start; voice.rate = rate;
                voice.active = voice.streamAudio = voice.qualityInterpolation = true;
                int count = 0;
                for (; count < 128 && (loop || count * rate < length); ++count) {
                    const double expected = reference(pcm, count * rate, rate,
                        rate <= 8 ? 64 : 128, loop, start);
                    require(std::fabs(voice.tick() - expected) < 3e-6,
                            "voice output matches unwrapped FIR through attack, loops and edges");
                }
                if (!loop) {
                    require(count == int(std::ceil(length / rate)), "source EOF frame count preserved");
                    require(voice.tick() == 0 && !voice.active, "no extra reconstruction tail");
                }
            }
        }
        std::fill(pcm.begin(), pcm.end(), 9137);
        for (double rate : {4.0001, 4.5, 5.0, 8.0, 9.0, 10.0, 12.0, 32.0, 64.0, 100.0})
            for (size_t i = 0; i < length; ++i)
                require(std::fabs(sample(pcm, i + 0.25, rate) - 9137.0 / 32768) < 2e-6,
                        "short sources retain unity DC gain at high ratios");
    }
    const auto pcm = sine(0.13);
    for (double rate : {0.5, 0.9, 1.0, 2.0, 4.0, 4.5, 5.0, 8.0, 9.0, 10.0, 16.0, 64.0}) {
        const float left = pcm_detail::interpolatePcm(pcm.data(), pcm.size(), 64,
            1.0f, false, 0, 0, true, rate);
        require(std::fabs(left - sample(pcm, 65, rate)) < 1e-6,
                "phase endpoint meets the next source sample smoothly");
    }
    for (double boundary : {4.0, 5.0, 8.0, 10.0}) {
        const float center = sample(pcm, 64.375, boundary);
        require(std::fabs(sample(pcm, 64.375, boundary - 1e-5) - center) < 1e-5
             && std::fabs(sample(pcm, 64.375, boundary + 1e-5) - center) < 1e-5,
                "filter transition is continuous for changing pitch");
    }
    for (double rate : {4.1, 4.5, 6.0, 8.0, 9.0, 16.0, 64.0, 100.0}) {
        const float linear = sample(pcm, 64.375, rate, false);
        float allocation = 123;
        (void)pcm_detail::interpolatePcm(pcm.data(), pcm.size(), 64, 0.375f,
            false, 0, 0, true, rate, &allocation);
        require(allocation == linear, "voice-steal level still uses legacy linear samples");
        const float a = pcm[64] * (1.0f / 32768), b = pcm[65] * (1.0f / 32768);
        require(linear == a + (b - a) * 0.375f, "legacy profiles remain exactly linear");
    }
}

void extremeRatios() {
    for (double rate : {16.0, 32.0, 64.0}) {
        const auto stop = sine(0.7 / rate, 131072);
        const auto pass = sine(0.2 / rate, 131072);
        double energy = 0, beforeEnergy = 0, passEnergy = 0;
        for (int frame = 0; frame < 1024; ++frame) {
            const double position = 256.25 + frame * rate;
            const double actual = sample(stop, position, rate);
            const double before = reference(stop, position, rate, 64);
            const double passed = sample(pass, position, rate);
            energy += actual * actual;
            beforeEnergy += before * before;
            passEnergy += passed * passed;
            require(std::fabs(actual - reference(stop, position, rate, 128)) < 3e-6,
                    "128-tap path matches independent FIR at exact phase/cutoff rows");
        }
        const double maximumRatio = rate == 16 ? 0.0631 : rate == 32 ? 0.2512 : 0.3981;
        require(energy < beforeEnergy * maximumRatio,
                "extreme-ratio stopband improves at least 12/6/4 dB over 64 taps");
        require(std::sqrt(passEnergy / 1024) * std::sqrt(2.0) > 0.9 * 12000 / 32768,
                "low passband tone retains at least 90 percent amplitude");
        std::cout << "extreme ratio=" << rate << " alias RMS " << std::sqrt(beforeEnergy / 1024)
                  << " -> " << std::sqrt(energy / 1024)
                  << ", improvement dB=" << 10 * std::log10(beforeEnergy / energy) << '\n';
    }
}

void tableEdges() {
    const auto pcm = sine(0.13);
    const double infinity = std::numeric_limits<double>::infinity();
    for (double boundary : {0.5, 1.0, 2.0, 4.0, 8.0}) {
        // Just above four, float(64/rate) rounds back to 16. The trimmed
        // bank must retain its upper guard row even with zero cutoff blend.
        for (double rate : {boundary, std::nextafter(boundary, infinity), boundary + 1e-8}) {
            for (float phase : {0.0f, 0.137f, std::nextafter(1.0f, 0.0f), 1.0f}) {
                const float actual = pcm_detail::interpolatePcm(pcm.data(), pcm.size(), 128,
                    phase, false, 0, 0, true, rate);
                const float atBoundary = pcm_detail::interpolatePcm(pcm.data(), pcm.size(), 128,
                    phase, false, 0, 0, true, boundary);
                require(std::isfinite(actual) && std::fabs(actual - atBoundary) < 1e-6,
                        "rounded cutoff/phase endpoints stay finite and continuous");
            }
        }
    }
    for (double rate : {infinity, -infinity, std::numeric_limits<double>::quiet_NaN()}) {
        require(sample(pcm, 128.375, rate) == sample(pcm, 128.375, rate, false),
                "nonfinite source advances retain linear fallback");
    }
}

void extremeTableAccuracy() {
    double maximumError = 0;
    for (double rate : {10.0, 12.0, 15.99, 16.01, 24.0, 32.0, 64.0, 100.0}) {
        for (double frequency : {0.35, 0.5, 0.7}) {
            for (double offset : {0.0, pi / 2}) {
                std::vector<int16_t> pcm(1024);
                for (size_t i = 0; i < pcm.size(); ++i)
                    pcm[i] = int16_t(std::lround(30000 * std::sin(2 * pi * frequency / rate * i + offset)));
                for (double fraction : {0.037, 0.137, 0.333, 0.731, 0.999}) {
                    const double position = 256 + fraction;
                    const double error = std::fabs(sample(pcm, position, rate)
                        - reference(pcm, position, rate, 128));
                    maximumError = std::max(maximumError, error);
                    require(error < 0.0005,
                            "off-grid reconstruction stays within 0.0005 of independent continuous FIR");
                }
            }
        }
    }
    std::cout << "128-tap off-grid maximum absolute sample error=" << maximumError << '\n';
}
#include "pcm-silent-loop-regression.h"
#include "pcm-unity-regression.h"
#include "pcm-near-nyquist-regression.h"
int main() {
    spectral(); boundaries(); extremeRatios(); tableEdges(); extremeTableAccuracy();
    pcm_dynamic_regression::run(); pcm_boundary_cursor_regression::run();
    pcm_silent_loop_regression::run();
    pcm_unity_regression::run();
    pcm_near_nyquist_regression::run();
    std::cout << "High-rate resampling tests PASS\n";
}
