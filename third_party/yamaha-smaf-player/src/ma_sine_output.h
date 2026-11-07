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

class StereoStage {
    const std::vector<double>* h_;
    std::vector<Stereo> history_;
    size_t cursor_ = 0, mask_;
    bool odd_ = false;
public:
    explicit StereoStage(int taps) : h_(&coefficients(taps)),
        history_(std::bit_ceil(size_t(taps))), mask_(history_.size() - 1) {}
    bool push(Stereo input, Stereo& output) {
        cursor_ = (cursor_ + 1) & mask_; history_[cursor_] = input;
        odd_ = !odd_; if (odd_) return false;
        const auto& h = *h_; const size_t middle = (h.size() - 1) / 2;
        const auto center = history_[(cursor_ - middle) & mask_];
        Stereo sum{h[middle] * center.left, h[middle] * center.right};
        for (size_t k = 1; k < middle; k += 2) {
            const auto a = history_[(cursor_ - k) & mask_];
            const auto b = history_[(cursor_ - (h.size() - 1 - k)) & mask_];
            sum.left += h[k] * (a.left + b.left);
            sum.right += h[k] * (a.right + b.right);
        }
        output = sum; return true;
    }
    void reset() {
        std::fill(history_.begin(), history_.end(), Stereo{}); cursor_ = 0; odd_ = false;
    }
};

class Bus {
    int factor_, padding_, support_, pending_ = 0;
    std::vector<StereoStage> stages_;
    std::array<Stereo, 72> native_{};
    std::array<Stereo, 8> paddingHistory_{};
    size_t nativeCursor_ = 0, paddingCursor_ = 0;
    void resetSine_() {
        for (auto& s : stages_) s.reset();
        paddingHistory_.fill({}); paddingCursor_ = 0; pending_ = 0;
    }
public:
    static constexpr int latency = 72;
    explicit Bus(int factor) : factor_(factor), padding_(0), support_(0) {
        if (!validFactor(factor)) throw std::invalid_argument("invalid sine bus factor");
        int stride = 1, delayNumerator = 0;
        for (int n = factor; n > 1; n /= 2) {
            const int taps = n == 2 ? 257 : factor == 32 && n == 4 ? 37 : 33;
            stages_.emplace_back(taps);
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
        const bool nonzero = factor_ > 1 && std::any_of(fm.begin(), fm.end(),
            [](const Stereo& x) { return x.left != 0 || x.right != 0; });
        if (factor_ > 1 && (nonzero || pending_ != 0)) {
            for (const auto& x : fm) {
                Stereo value = x;
                for (size_t i = 0; i < stages_.size(); ++i) {
                    const bool produced = stages_[i].push(value, value);
                    if (!produced) break;
                    if (i + 1 == stages_.size()) filtered = value;
                }
            }
            if (padding_ != 0) {
                const auto old = paddingHistory_[paddingCursor_];
                paddingHistory_[paddingCursor_] = filtered; filtered = old;
                paddingCursor_ = (paddingCursor_ + 1) % size_t(padding_);
            }
            if (nonzero) pending_ = support_;
            else if (--pending_ == 0) resetSine_();
        }
        const auto old = native_[nativeCursor_]; native_[nativeCursor_] = native;
        nativeCursor_ = (nativeCursor_ + 1) % native_.size();
        return {filtered.left + old.left, filtered.right + old.right};
    }
    void reset() {
        resetSine_(); native_.fill({}); nativeCursor_ = 0;
    }
    int factor() const { return factor_; }
    int tailBoundFrames() const { return support_; }
    // Remaining FIR/padding support only; the native FIFO is separate.
    int pendingFrames() const { return pending_; }
};
} // namespace fxchain::smaf::sine_output
