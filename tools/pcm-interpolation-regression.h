// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "pcm_interpolation.h"
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

void testPcmInterpolation() {
    using fxchain::smaf::pcm_detail::interpolatePcm;
    using fxchain::smaf::pcm_detail::sampleIndex;

    constexpr float tolerance = 1.0e-5f;
    for (size_t length : {size_t(1), size_t(2), size_t(7), size_t(8), size_t(16)}) {
        std::vector<int16_t> constant(length, 7321);
        const size_t last = length - 1;
        for (bool loop : {false, true}) {
            for (float phase : {0.0f, 0.19f, 0.5f, 0.99f, 1.0f}) {
                const float output = interpolatePcm(constant.data(), length, last, phase,
                        loop, 0, 0, true, 0.45);
                check(std::fabs(output - 7321.0f / 32768.0f) < tolerance,
                        "PCM interpolation preserves DC for short and singleton waves");
            }
        }

        // Loop history advances by whole loop lengths and remains periodic,
        // including short loops whose 16-tap kernel revisits the same samples.
        for (size_t index = 0; index < length; ++index) {
            for (size_t travel : {size_t(0), length, length * 2}) {
                check(sampleIndex(int64_t(index), length, true, 0, travel) == index,
                        "PCM loop history is stable across whole loop traversals");
                check(sampleIndex(int64_t(index + length), length, true, 0, travel) == index,
                        "PCM loop tap wraps with singleton and short loops");
            }
        }

        std::vector<int16_t> pattern(length);
        for (size_t i = 0; i < length; ++i) {
            pattern[i] = int16_t((int(i) * 7919) % 24001 - 12000);
        }
        const size_t index = length - 1;
        constexpr bool phaseLoop = true;
        const size_t next = phaseLoop ? 0 : index;
        const float expectedZero = pattern[index] / 32768.0f;
        const float expectedOne = pattern[next] / 32768.0f;
        check(std::fabs(interpolatePcm(pattern.data(), length, index, 0.0f,
                        phaseLoop, 0, 0, true, 0.4) - expectedZero) < 1.0e-7f,
                "PCM phase zero returns the current source sample");
        check(std::fabs(interpolatePcm(pattern.data(), length, index, 1.0f,
                        phaseLoop, 0, 0, true, 0.4) - expectedOne) < 1.0e-7f,
                "PCM phase one returns the next source sample");
    }

    // The first interpolation history before a short loop wraps still belongs
    // to the attack; later history follows the periodic loop body.
    constexpr size_t attackEnd = 9;
    constexpr size_t attackLoopStart = 5;
    constexpr size_t attackLoopLength = attackEnd - attackLoopStart;
    check(sampleIndex(-1, attackEnd, true, attackLoopStart, 0) == 0,
            "PCM first-loop virtual history retains attack boundary sample");
    check(sampleIndex(-1, attackEnd, true, attackLoopStart, attackLoopLength) == 3,
            "PCM first traversal history does not prematurely wrap attack");
    check(sampleIndex(1, attackEnd, true, attackLoopStart,
                    attackLoopLength * 2) == attackLoopStart,
            "PCM history wraps to loop start after attack traversal");
    check(sampleIndex(attackEnd, attackEnd, true, attackLoopStart, 0) == attackLoopStart,
            "PCM short loop begins at its declared loop start");

    const std::vector<int16_t> edge{12000, -9000, 7000, -3000, 11000, -6000};
    constexpr size_t edgeIndex = 2;
    constexpr float edgePhase = 0.37f;
    const float s0 = edge[edgeIndex] / 32768.0f;
    const float s1 = edge[edgeIndex + 1] / 32768.0f;
    const float linear = s0 + (s1 - s0) * edgePhase;
    const float oldProfile = interpolatePcm(edge.data(), edge.size(), edgeIndex,
            edgePhase, false, 0, 0, false, 0.4);
    check(oldProfile == linear,
            "PCM nonmodern profile remains exactly linear");
    for (double advance : {1.0, 1.25, 4.0}) {
        check(interpolatePcm(edge.data(), edge.size(), edgeIndex, edgePhase,
                        false, 0, 0, false, advance) == linear,
                "PCM legacy profile remains exactly linear above unity");
    }

    const float modernAtBoundary = interpolatePcm(edge.data(), edge.size(), edgeIndex,
            edgePhase, false, 0, 0, true, 0.8);
    const float modernAtNinety = interpolatePcm(edge.data(), edge.size(), edgeIndex,
            edgePhase, false, 0, 0, true, 0.9);
    const float modernAtNinetyFive = interpolatePcm(edge.data(), edge.size(), edgeIndex,
            edgePhase, false, 0, 0, true, 0.95);
    const float modernNearUnity = interpolatePcm(edge.data(), edge.size(), edgeIndex,
            edgePhase, false, 0, 0, true, 0.999);
    const float expectedNinety = linear + 0.5f * (modernAtBoundary - linear);
    const float expectedNinetyFive = linear + 0.15625f * (modernAtBoundary - linear);
    check(std::fabs(modernAtNinety - expectedNinety) < tolerance,
            "PCM crossfade is halfway at advance 0.9");
    check(std::fabs(modernAtNinetyFive - expectedNinetyFive) < tolerance,
            "PCM crossfade uses a smooth curve near unity");
    check(std::fabs(modernNearUnity - linear) < 1.0e-3f,
            "PCM crossfade approaches exact linear interpolation at unity");
    check(interpolatePcm(edge.data(), edge.size(), edgeIndex, edgePhase,
                    false, 0, 0, true, 1.0) == linear,
            "PCM crossfade reaches exact linear interpolation at unity");

    // A periodic 0.2-cycle/source sine stresses high-frequency upsampling.
    // The band-limited reconstruction should improve RMS error over linear.
    constexpr size_t sineLength = 50;
    constexpr double pi = 3.14159265358979323846;
    constexpr float sinePhase = 0.37f;
    std::vector<int16_t> sine(sineLength);
    for (size_t i = 0; i < sineLength; ++i) {
        sine[i] = int16_t(std::lrint(12000.0 * std::sin(2.0 * pi * 0.2 * double(i))));
    }
    double linearError = 0.0;
    double modernError = 0.0;
    for (size_t i = 0; i < sineLength; ++i) {
        const double expected = 12000.0 / 32768.0
                * std::sin(2.0 * pi * 0.2 * (double(i) + sinePhase));
        const float linearSample = interpolatePcm(sine.data(), sine.size(), i,
                sinePhase, true, 0, 0, false, 0.5);
        const float modernSample = interpolatePcm(sine.data(), sine.size(), i,
                sinePhase, true, 0, sineLength, true, 0.5);
        linearError += (linearSample - expected) * (linearSample - expected);
        modernError += (modernSample - expected) * (modernSample - expected);
    }
    check(modernError < linearError * 0.8,
            "PCM band-limited upsampling improves periodic sine interpolation error");
    std::cout << "PCM sine RMS error: linear=" << std::sqrt(linearError / sineLength)
              << " sinc=" << std::sqrt(modernError / sineLength) << '\n';
}
