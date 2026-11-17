// SPDX-License-Identifier: Apache-2.0
// Generic stereo reverb for the CC91 send. Original synthesis
// recipes, not a model of any handset or sound-source chip: the structure and
// constants are DEV approximations chosen against one reference rendering.
// Deterministic per-sample processing; no allocation after prepare().
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fxchain::smaf {

inline float flushTiny(float v) { return std::fabs(v) < 1e-20f ? 0.0f : v; }

struct EffectParams {
    double reverbSeconds = 1.6;    // time for the low band to fall by 60 dB
    double reverbDamping = 0.2;    // 0..1, higher darkens the tail faster
    double reverbPreDelayMs = 12.0;
    double reverbHighPassHz = 150.0; // the send keeps the bass out of the tail
};

class StereoReverb {
public:
    static constexpr int kLines = 8;
    void prepare(double sampleRate, const EffectParams& p) {
        // Mutually unrelated delay lengths (ms) keep the modes dense.
        static constexpr double ms[kLines] = {31.7, 37.3, 43.1, 47.9, 53.3, 59.9, 67.1, 73.7};
        const double scale = sampleRate / 1000.0;
        for (int i = 0; i < kLines; ++i) {
            const int n = std::max(8, int(std::lround(ms[i] * scale)));
            line_[i].assign(size_t(n), 0.0f);
            pos_[i] = 0;
            // 60 dB over reverbSeconds, evaluated at the line's own delay.
            gain_[i] = float(std::pow(10.0, -3.0 * double(n) / (std::max(0.05, p.reverbSeconds) * sampleRate)));
            lp_[i] = 0.0f;
        }
        damp_ = float(std::clamp(p.reverbDamping, 0.0, 0.95));
        hpCoef_ = p.reverbHighPassHz > 0 ? float(std::exp(-2.0 * 3.14159265358979323846 * p.reverbHighPassHz / sampleRate)) : 0.0f;
        hpState_ = 0.0f;
        const int pre = std::max(1, int(std::lround(p.reverbPreDelayMs * scale)));
        pre_.assign(size_t(pre), 0.0f); prePos_ = 0;
        for (int i = 0; i < 2; ++i) {
            const int n = std::max(4, int(std::lround((i ? 7.9 : 5.3) * scale)));
            ap_[i].assign(size_t(n), 0.0f); apPos_[i] = 0;
        }
    }
    void reset() {
        for (auto& l : line_) std::fill(l.begin(), l.end(), 0.0f);
        for (auto& l : ap_) std::fill(l.begin(), l.end(), 0.0f);
        std::fill(pre_.begin(), pre_.end(), 0.0f);
        pos_.fill(0); apPos_.fill(0); prePos_ = 0; lp_.fill(0.0f); hpState_ = 0.0f;
    }
    // Returns the wet stereo pair for one input frame.
    void process(float inL, float inR, float& outL, float& outR) {
        float x = 0.5f * (inL + inR);
        // One-pole high-pass on the send: low notes stay centered, as in the
        // reference, instead of spreading into a decorrelated bass tail.
        hpState_ = flushTiny(hpCoef_ * hpState_ + (1.0f - hpCoef_) * x);
        x -= hpState_;
        // Pre-delay, then two series all-pass diffusers.
        float d = pre_[prePos_]; pre_[prePos_] = x; if (++prePos_ == pre_.size()) prePos_ = 0;
        x = d;
        for (int i = 0; i < 2; ++i) {
            auto& buf = ap_[i];
            const float delayed = buf[apPos_[i]];
            const float v = x + 0.6f * delayed;
            buf[apPos_[i]] = flushTiny(v);
            if (++apPos_[i] == buf.size()) apPos_[i] = 0;
            x = delayed - 0.6f * v;
        }
        // Read the lines, damp, and mix with an orthonormal Hadamard matrix.
        std::array<float, kLines> y{};
        for (int i = 0; i < kLines; ++i) {
            const float raw = line_[i][pos_[i]];
            lp_[i] = flushTiny((1.0f - damp_) * raw + damp_ * lp_[i]);
            y[i] = lp_[i] * gain_[i];
        }
        std::array<float, kLines> m = y;
        for (int len = 1; len < kLines; len <<= 1)
            for (int i = 0; i < kLines; i += len << 1)
                for (int j = i; j < i + len; ++j) { const float a = m[j], b = m[j + len]; m[j] = a + b; m[j + len] = a - b; }
        constexpr float norm = 0.35355339f; // 1/sqrt(8)
        // Alternating-sign input weights decorrelate the lines.
        static constexpr float sign[kLines] = {1, -1, 1, 1, -1, 1, -1, -1};
        for (int i = 0; i < kLines; ++i) {
            line_[i][pos_[i]] = flushTiny(m[i] * norm + x * 0.35f * sign[i]);
            if (++pos_[i] == line_[i].size()) pos_[i] = 0;
        }
        float l = 0, r = 0;
        for (int i = 0; i < kLines; ++i) ((i & 1) ? r : l) += y[i] * sign[i];
        outL = l * 0.5f; outR = r * 0.5f;
    }

private:
    std::array<std::vector<float>, kLines> line_;
    std::array<size_t, kLines> pos_{};
    std::array<float, kLines> gain_{}, lp_{};
    float damp_ = 0.35f;
    float hpCoef_ = 0.0f, hpState_ = 0.0f;
    std::vector<float> pre_; size_t prePos_ = 0;
    std::array<std::vector<float>, 2> ap_; std::array<size_t, 2> apPos_{};
};

} // namespace fxchain::smaf
