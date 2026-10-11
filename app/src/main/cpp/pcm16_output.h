// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mmfpocket::audio {
// Same symmetric32767 scale as the established output. Nearest integer,
// half away from zero; deterministic, no dither/no added noise at silence.
// A float times32767 is exact in double, avoiding a second float rounding.
inline bool encodePcm16(float sample, int16_t& encoded) {
    if (!std::isfinite(sample)) return false;
    const double bounded = std::clamp(double(sample), -1.0, 1.0);
    encoded = static_cast<int16_t>(std::lround(bounded * 32767.0));
    return true;
}
} // namespace mmfpocket::audio
