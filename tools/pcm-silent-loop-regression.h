// SPDX-License-Identifier: Apache-2.0
// Original PCM fixtures: zero-loop termination must retain FIR attack history.
#pragma once
namespace pcm_silent_loop_regression {
inline void run() {
    using namespace fxchain::smaf;
    size_t checks = 0, tails = 0;
    double maxError = 0;
    for (size_t start : {size_t(0), size_t(1), size_t(7), size_t(63), size_t(257)}) {
        for (size_t length : {size_t(1), size_t(3), size_t(31)}) {
            std::vector<int16_t> pcm(start + length, 0), timeline(start + 512, 0);
            for (size_t i = 0; i < start; ++i)
                timeline[i] = pcm[i] = int16_t(int((i * 7919 + 317) % 60001) - 30000);
            for (double rate : {0.125, 0.5, 0.8, 1.0, 1.01, 2.0, 4.0, 4.5,
                                6.0, 8.0, 9.0, 12.0, 16.0, 32.0, 64.0, -1.0}) {
                MaPlayer::PcmVoice voice;
                voice.pcm = pcm.data(); voice.len = voice.loopEnd = pcm.size();
                voice.loopStart = start; voice.loop = voice.silentLoop = true;
                voice.active = voice.streamAudio = voice.qualityInterpolation = true;
                const double limit = start == 0 ? 0 : start + 63;
                double position = 0;
                for (int frame = 0; position < limit; ++frame) {
                    // Widen after entry into the zero loop as well as before
                    // it: future filters may still read a nonzero attack tap.
                    voice.rate = rate > 0 ? rate : position < start ? 0.5
                        : frame % 3 == 0 ? 16.0 : frame % 3 == 1 ? 1.0 : 0.25;
                    const size_t index = size_t(position);
                    const float expected = pcm_detail::interpolatePcm(timeline.data(), timeline.size(),
                        index, float(position - index), false, 0, 0, true, voice.rate);
                    const float actual = voice.tick();
                    maxError = std::max(maxError, std::fabs(double(actual) - expected));
                    const bool premature = !voice.active && position + 1e-8 < limit;
                    if (premature || std::fabs(double(actual) - expected) >= 3e-5)
                        std::cerr << "zero-loop start=" << start << " length=" << length
                                  << " rate=" << voice.rate << " frame=" << frame
                                  << " source=" << position << " actual=" << actual
                                  << " expected=" << expected << " active=" << voice.active << '\n';
                    require(!premature && std::fabs(double(actual) - expected) < 3e-5,
                            "zero loop retains the reconstructed attack and filter history");
                    if (position >= start && expected != 0) ++tails;
                    position += voice.rate; ++checks;
                    require(frame < 10000, "zero-loop source clock advances to its bounded end");
                }
                require(voice.tick() == 0 && !voice.active,
                        "zero loop retires exactly once all possible FIR source taps are zero");
                // Widening after retirement cannot resurrect an old sample.
                const size_t index = size_t(position);
                for (double nextRate : {0.125, 1.0, 4.5, 9.0, 64.0}) {
                    require(pcm_detail::interpolatePcm(timeline.data(), timeline.size(), index,
                            float(position - index), false, 0, 0, true, nextRate) == 0,
                            "retirement remains exact zero under future filter widening");
                }
            }
        }
    }
    require(tails > 0, "fixtures actually exercise nonzero reconstruction after the zero-loop start");
    std::cout << "Silent-loop reconstruction PASS: " << checks << " samples, " << tails
              << " retained tail samples, max error=" << maxError << '\n';
}
} // namespace pcm_silent_loop_regression
