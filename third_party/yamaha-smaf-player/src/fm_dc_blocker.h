// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>

namespace fxchain::smaf {
// Renderer-quality conditioning, NOT a calibrated MA hardware output model.
// Normalized Nyquist gain; corner3Hz loses <0.1dB at20Hz. Two scalar histories,
// no allocation/trigonometry during process(). Keep operator DC intact inside
// the FM graph: unipolar modulators legitimately change carrier phase/timbre.
class FmDcBlocker {
public:
    void configure(double sampleRate) {
        pole_ = std::exp(-6.28318530717958647692 * 3.0 / sampleRate);
        gain_ = (1.0 + pole_) * 0.5;
        reset();
    }
    void reset() { previousInput_ = previousOutput_ = 0.0; }
    float process(float input) {
        const double output = gain_ * (double(input) - previousInput_) + pole_ * previousOutput_;
        previousInput_ = input;
        previousOutput_ = output;
        return float(output);
    }
    bool settled() const {
        return std::fabs(previousInput_) < 1e-7 && std::fabs(previousOutput_) < 1e-7;
    }
private:
    double pole_ = 0.0, gain_ = 1.0;
    double previousInput_ = 0.0, previousOutput_ = 0.0;
};
} // namespace fxchain::smaf
