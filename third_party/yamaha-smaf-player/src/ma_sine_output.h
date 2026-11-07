// SPDX-License-Identifier: Apache-2.0
// Fixed-factor experimental sine bus. Construction prepares shared kernels;
// frame/reset allocate nothing. Per-player histories remain independent.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace fxchain::smaf::sine_output {
struct Stereo { double left = 0, right = 0; };

inline bool validFactor(int factor) {
    return factor >= 1 && factor <= 32 && (factor & (factor - 1)) == 0;
}

inline const std::vector<double>& coefficients(int taps) {
    // These are the three already-tested integer-delay halfband designs.
    static const auto bank = [] {
        std::array<std::vector<double>, 3> result;
        const int sizes[] = {33, 37, 257};
        for (size_t i = 0; i < result.size(); ++i) {
            const int n = sizes[i], middle = (n - 1) / 2;
            auto& h = result[i]; h.resize(size_t(n), 0.0);
            constexpr double pi = std::numbers::pi;
            for (int k = 1; k <= middle; ++k) {
                const int d = k - middle;
                if (d != 0 && d % 2 == 0) continue;
                const double sinc = d == 0 ? .5 : std::sin(pi * .5 * d) / (pi * d);
                const double window = .42 - .5 * std::cos(2 * pi * k / (n - 1))
                                      + .08 * std::cos(4 * pi * k / (n - 1));
                h[size_t(k)] = h[size_t(n - 1 - k)] = sinc * window;
            }
            double sum = 0; for (double v : h) sum += v;
            for (double& v : h) v /= sum;
        }
        return result;
    }();
    if (taps == 33) return bank[0];
    if (taps == 37) return bank[1];
    if (taps == 257) return bank[2];
    throw std::invalid_argument("unsupported aligned sine kernel");
}

class Stage {
    const std::vector<double>* h_;
    std::vector<double> history_;
    size_t cursor_ = 0, mask_;
    bool odd_ = false;
public:
    explicit Stage(int taps) : h_(&coefficients(taps)),
        history_(std::bit_ceil(size_t(taps)), 0.0), mask_(history_.size() - 1) {}
    bool push(double input, double& output) {
        cursor_ = (cursor_ + 1) & mask_; history_[cursor_] = input;
        odd_ = !odd_; if (odd_) return false;
        const auto& h = *h_; const size_t middle = (h.size() - 1) / 2;
        double sum = h[middle] * history_[(cursor_ - middle) & mask_];
        for (size_t k = 1; k < middle; k += 2)
            sum += h[k] * (history_[(cursor_ - k) & mask_]
                       + history_[(cursor_ - (h.size() - 1 - k)) & mask_]);
        output = sum; return true;
    }
    void reset() {
        std::fill(history_.begin(), history_.end(), 0.0); cursor_ = 0; odd_ = false;
    }
};

class Bus {
    int factor_, padding_, support_;
    std::vector<Stage> left_, right_;
    std::array<Stereo, 72> native_{};
    std::array<Stereo, 8> paddingHistory_{};
    size_t nativeCursor_ = 0, paddingCursor_ = 0;
public:
    static constexpr int latency = 72;
    explicit Bus(int factor) : factor_(factor), padding_(0), support_(0) {
        if (!validFactor(factor)) throw std::invalid_argument("invalid sine bus factor");
        int stride = 1, delayNumerator = 0;
        for (int n = factor; n > 1; n /= 2) {
            const int taps = n == 2 ? 257 : factor == 32 && n == 4 ? 37 : 33;
            left_.emplace_back(taps); right_.emplace_back(taps);
            delayNumerator += (taps - 1) / 2 * stride; stride *= 2;
        }
        if (factor > 1) {
            padding_ = latency - delayNumerator / factor;
            support_ = 2 * delayNumerator / factor + padding_;
        }
    }
    Stereo frame(std::span<const Stereo> fm, Stereo native) {
        if (fm.size() != size_t(factor_)) throw std::invalid_argument("wrong sine bus block");
        Stereo filtered;
        if (factor_ > 1) {
            for (const auto& x : fm) {
                Stereo value = x;
                for (size_t i = 0; i < left_.size(); ++i) {
                    const bool produced = left_[i].push(value.left, value.left);
                    right_[i].push(value.right, value.right);
                    if (!produced) break;
                    if (i + 1 == left_.size()) filtered = value;
                }
            }
            if (padding_ != 0) {
                const auto old = paddingHistory_[paddingCursor_];
                paddingHistory_[paddingCursor_] = filtered; filtered = old;
                paddingCursor_ = (paddingCursor_ + 1) % size_t(padding_);
            }
        }
        const auto old = native_[nativeCursor_]; native_[nativeCursor_] = native;
        nativeCursor_ = (nativeCursor_ + 1) % native_.size();
        return {filtered.left + old.left, filtered.right + old.right};
    }
    void reset() {
        for (auto& s : left_) s.reset(); for (auto& s : right_) s.reset();
        native_.fill({}); paddingHistory_.fill({}); nativeCursor_ = paddingCursor_ = 0;
    }
    int factor() const { return factor_; }
    int tailBoundFrames() const { return support_; }
};
} // namespace fxchain::smaf::sine_output
