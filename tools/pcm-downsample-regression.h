// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "pcm_interpolation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// Synthetic quality checks for resampling only, not hardware-DSP fidelity.
void testPcmDownsampling() {
    using namespace fxchain::smaf::pcm_detail;
    constexpr double pi = 3.14159265358979323846;
    constexpr size_t signalLength = 8192;
    constexpr size_t guard = 16;
    constexpr int outputCount = 256;
    constexpr float sourceAmplitude = 12000.0f / 32768.0f;

    auto makeSine = [&](double cyclesPerSourceSample) {
        std::vector<int16_t> pcm(signalLength);
        for (size_t i = 0; i < pcm.size(); ++i) {
            pcm[i] = int16_t(std::lrint(12000.0
                    * std::sin(2.0 * pi * cyclesPerSourceSample * double(i))));
        }
        return pcm;
    };
    auto measure = [&](const std::vector<int16_t>& pcm, double rate,
                       double offset, bool modern) {
        double sumSquares = 0.0;
        float peak = 0.0f;
        for (int n = 0; n < outputCount; ++n) {
            const double position = offset + double(n) * rate;
            const size_t whole = size_t(std::floor(position));
            const size_t index = guard + whole;
            const float fraction = float(position - double(whole));
            const float value = interpolatePcm(pcm.data(), pcm.size(), index, fraction,
                    false, 0, 0, modern, rate);
            sumSquares += double(value) * value;
            peak = std::max(peak, std::fabs(value));
        }
        return std::pair<double, float>{std::sqrt(sumSquares / outputCount), peak};
    };

    // A low periodic tone remains in the passband at both integer rate ratios.
    const auto passband = makeSine(1.0 / 32.0);
    for (double rate : {2.0, 4.0}) {
        for (double offset : {0.0, 0.37, 0.5}) {
            const auto [rms, peak] = measure(passband, rate, offset, true);
            check(std::fabs(rms * std::sqrt(2.0) - sourceAmplitude) < 0.025,
                    "PCM downsampling preserves passband sine amplitude");
            check(peak > sourceAmplitude * 0.90,
                    "PCM downsampling preserves passband sine peak");
        }
    }

    // Above-Nyquist tones should be rejected even where integer-phase samples
    // would alias to a plausible tone (or happen to land on zero crossings).
    const auto stopAtTwo = makeSine(0.375);
    for (double offset : {0.0, 0.37, 0.5}) {
        const auto [rms, peak] = measure(stopAtTwo, 2.0, offset, true);
        check(rms < 0.012 && peak < 0.035,
                "PCM rate-2 filter rejects a 0.375-cycle/source stopband tone");
        const auto baseline = measure(stopAtTwo,2.0,offset,false);
        check(rms < baseline.first * 0.05,"PCM rate-2 alias falls more than26dB versus linear");
        std::cout<<"PCM AA stopband rate2 offset="<<offset<<" linearRms="<<baseline.first
                 <<" filteredRms="<<rms<<'\n';
    }
    const auto stopAtFour = makeSine(0.25);
    for (double offset : {0.0, 0.25, 0.5, 0.75}) {
        const auto [rms, peak] = measure(stopAtFour, 4.0, offset, true);
        check(rms < 0.012 && peak < 0.035,
                "PCM rate-4 filter rejects a 0.25-cycle/source stopband tone");
        if(offset>0){const auto baseline=measure(stopAtFour,4.0,offset,false);
            check(rms < baseline.first * 0.05,"PCM rate-4 alias falls more than26dB versus linear");
            std::cout<<"PCM AA stopband rate4 offset="<<offset<<" linearRms="<<baseline.first
                     <<" filteredRms="<<rms<<'\n';}
    }
    const auto stopAtOneHalf=makeSine(0.40625);
    for(double offset:{0.,.37,.5}){
        const auto modern=measure(stopAtOneHalf,1.5,offset,true);
        const auto legacy=measure(stopAtOneHalf,1.5,offset,false);
        check(modern.first<legacy.first*.1,"PCM rate-1.5 stopband alias falls more than20dB");
    }

    // DC gain must remain unity for tiny non-looping samples, singleton loops,
    // and high reduction ratios including the zero-cutoff end of the grid.
    for (size_t length : {size_t(1), size_t(3), size_t(8)}) {
        std::vector<int16_t> dc(length, 9137);
        for (double rate : {1.01, 1.1, 2.0, 4.0, 100.0}) {
            const float value = interpolatePcm(dc.data(), length, length - 1, 0.37f,
                    true, 0, 0, true, rate);
            check(std::isfinite(value)
                            && std::fabs(value - 9137.0f / 32768.0f) < 2.0e-5f,
                    "PCM downsampling preserves DC for short and singleton loops");
        }
    }

    // Legacy profiles and all reduction rates continue to use the exact
    // beta9 linear arithmetic, while rates at or below one retain the prior
    // phase-table plus smoothstep-upsample result.
    std::vector<int16_t> probe(80);
    for (size_t i = 0; i < probe.size(); ++i) probe[i] = int16_t(int(i * 733) % 28001 - 14000);
    const size_t index = 32;
    constexpr float fraction = 0.43f;
    const float a = probe[index] * (1.0f / 32768.0f);
    const float b = probe[index + 1] * (1.0f / 32768.0f);
    const float linear = a + (b - a) * fraction;
    for (double rate : {1.5, 2.0, 4.0}) {
        check(interpolatePcm(probe.data(), probe.size(), index, fraction,
                        false, 0, 0, false, rate) == linear,
                "PCM legacy profile remains exact linear at reduction rates");
    }
    for (double rate : {1.0}) {
        check(interpolatePcm(probe.data(), probe.size(), index, fraction,
                        false, 0, 0, true, rate) == linear,
                "PCM modern unity preserves beta9 linear baseline");
    }
    for(double rate:{1.05,1.1,1.5,2.,4.,100.}){
        float referenceLinear=123;
        (void)interpolatePcm(probe.data(),probe.size(),index,fraction,false,0,0,true,rate,&referenceLinear);
        check(referenceLinear==linear,"PCM allocator sees exact old linear sample, not filtered output");
        const float left=interpolatePcm(probe.data(),probe.size(),index,1.f,false,0,0,true,rate);
        const float right=interpolatePcm(probe.data(),probe.size(),index+1,0.f,false,0,0,true,rate);
        check(std::fabs(left-right)<1e-6f,"PCM downsampling phase endpoint joins next integer smoothly");
    }
    const auto& table = interpolationKernels();
    for (float phase : {0.0f, 0.23f, 0.5f, 0.91f, 1.0f}) {
        const float phasePosition = phase * kPhases;
        const int row = std::min(kPhases - 1, int(phasePosition));
        const float blend = phasePosition - row;
        float reconstructed = 0.0f;
        for (int tap = 0; tap < kTaps; ++tap) {
            const float weight = table[row][tap]
                    + (table[row + 1][tap] - table[row][tap]) * blend;
            reconstructed += probe[index + tap - 7] * (1.0f / 32768.0f) * weight;
        }
        const float phaseS0 = probe[index] * (1.0f / 32768.0f);
        const float phaseS1 = probe[index + 1] * (1.0f / 32768.0f);
        const float phaseLinear = phaseS0 + (phaseS1 - phaseS0) * phase;
        for (double rate : {0.0, 0.5, 0.8, 0.9, 1.0}) {
            const double t = rate <= 0.8 ? 1.0 : (1.0 - rate) / 0.2;
            const float mix = float(t * t * (3.0 - 2.0 * t));
            const float expected = phaseLinear + mix * (reconstructed - phaseLinear);
            const float actual = interpolatePcm(probe.data(), probe.size(), index, phase,
                    false, 0, 0, true, rate);
            check(std::fabs(actual - expected) < 1.0e-6f,
                    "PCM upsampling retains beta9 phase-table and blend arithmetic");
        }
    }

    // Filter activation is smooth around the unity transition and cutoff-bin
    // edges; no exact-value assumption is made at fractional phase one.
    float previous = 0.0f;
    bool havePrevious = false;
    for (double rate : {1.0, 1.0001, 1.01, 1.05, 1.0999, 1.1, 1.1001,
                        1.99, 2.0, 2.01, 3.99, 4.0, 4.01}) {
        const float value = interpolatePcm(probe.data(), probe.size(), index, 1.0f,
                false, 0, 0, true, rate);
        check(std::isfinite(value), "PCM fractional endpoint remains finite across filter grid");
        if (havePrevious && rate < 1.2) {
            check(std::fabs(value - previous) < 0.04f,
                    "PCM filter activation is smooth through unity");
        }
        previous = value;
        havePrevious = true;
    }
    auto atRate = [&](double rate) {
        const float value = interpolatePcm(probe.data(), probe.size(), index, 0.37f,
                false, 0, 0, true, rate);
        check(std::isfinite(value), "PCM cutoff-grid interpolation remains finite");
        return value;
    };
    const float atTwoLow = atRate(1.999);
    const float atTwo = atRate(2.0);
    const float atTwoHigh = atRate(2.001);
    const float atFourLow = atRate(3.999);
    const float atFour = atRate(4.0);
    const float atFourHigh = atRate(4.001);
    check(std::fabs(atTwoLow - atTwo) < 0.03f
                    && std::fabs(atTwoHigh - atTwo) < 0.03f,
            "PCM cutoff-bin transition is smooth around rate two");
    check(std::fabs(atFourLow - atFour) < 0.03f
                    && std::fabs(atFourHigh - atFour) < 0.03f,
            "PCM cutoff-bin transition is smooth around rate four");
}
