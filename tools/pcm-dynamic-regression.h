// SPDX-License-Identifier: Apache-2.0
// Original synthetic regression for loop history under changing source rates.
// Include after the host test exposes MaPlayer::PcmVoice and PCM interpolation.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace pcm_dynamic_regression {
inline void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

inline void run() {
    using namespace fxchain::smaf;
    size_t checks = 0, lateWidenings = 0;
    double maxError = 0;
    for (size_t length : {size_t(1), size_t(3), size_t(9), size_t(31), size_t(79), size_t(257)}) {
        for (size_t start : {size_t(0), length / 2, length - 1}) {
            std::vector<int16_t> pcm(length);
            for (size_t i = 0; i < length; ++i)
                pcm[i] = int16_t(int((i * 7919 + 317) % 60001) - 30000);

            // Explicitly expand the attack and loop onto a source timeline.
            // The oracle shares the production interpolation kernel, but has
            // no loop, wraps, travel cap or periodic sample-index resolution.
            // This isolates source-history/state correctness; it is neither
            // an independent FIR check nor hardware calibration.
            std::vector<int16_t> timeline(50000);
            for (size_t i = 0; i < timeline.size(); ++i)
                timeline[i] = pcm[i < length ? i : start + (i - length) % (length - start)];

            MaPlayer::PcmVoice voice;
            voice.pcm = pcm.data();
            voice.len = voice.loopEnd = length;
            voice.loopStart = start;
            voice.loop = true;
            voice.active = voice.streamAudio = voice.qualityInterpolation = true;
            double position = 0;
            for (int frame = 0; frame < 3000; ++frame) {
                // Widen only after many slow wraps, narrow again, then cross
                // the 4..5 and 8..10 filter-bank transitions repeatedly.
                voice.rate = frame < 600 ? 0.3 : frame < 1000 ? 16.01
                    : frame < 1400 ? 0.5 : frame < 2200
                    ? 4.5 + 0.65 * std::sin(frame * 0.173)
                    : 9.0 + 1.3 * std::sin(frame * 0.173);
                if (frame == 600 && length <= 31) {
                    check(voice.loopTravel >= length + 64,
                          "short-loop history has reached its cap before filter widening");
                    ++lateWidenings;
                }
                const size_t index = size_t(position);
                check(index + 64 < timeline.size(), "expanded source covers all future filter taps");
                const float expected = pcm_detail::interpolatePcm(timeline.data(), timeline.size(),
                    index, float(position - double(index)), false, 0, 0, true, voice.rate);
                const float actual = voice.tick();
                const double error = std::fabs(double(actual) - expected);
                maxError = std::max(maxError, error);
                if (!std::isfinite(actual) || !voice.active || error > 3e-5) {
                    std::cerr << "FAIL: changing-rate loop differs from expanded source: length="
                              << length << " start=" << start << " frame=" << frame
                              << " rate=" << voice.rate << " error=" << error << '\n';
                    std::exit(1);
                }
                position += voice.rate;
                ++checks;
            }
        }
    }
    check(checks == 54000 && lateWidenings == 12, "all dynamic loop-history cases execute");
    std::cout << "Dynamic PCM loop tests PASS: " << checks << " samples, " << lateWidenings
              << " widenings after history cap, max error=" << maxError << '\n';
}
} // namespace pcm_dynamic_regression
