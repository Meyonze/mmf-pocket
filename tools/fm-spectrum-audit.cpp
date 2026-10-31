// SPDX-License-Identifier: Apache-2.0
// Host diagnostic: original steady carrier patches, not handset calibration.
#include "ma_fm_core.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fxchain::smaf;
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr unsigned size = 2048, rate = 44100, warmup = 4096;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<Complex> fft(const std::vector<double>& signal) {
    const size_t n = signal.size();
    require(n >= 2 && (n & (n - 1)) == 0, "FFT length must be a power of two");
    std::vector<Complex> bins(signal.begin(), signal.end());
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(bins[i], bins[j]);
    }
    for (size_t length = 2; length <= n; length *= 2) {
        for (size_t begin = 0; begin < n; begin += length) {
            for (size_t j = 0; j < length / 2; ++j) {
                const auto even = bins[begin + j];
                const auto odd = bins[begin + j + length / 2]
                    * std::polar(1.0, -tau * double(j) / double(length));
                bins[begin + j] = even + odd;
                bins[begin + j + length / 2] = even - odd;
            }
        }
    }
    return bins;
}

struct Metrics { double acPower = 0, residualPower = 0, dc = 0, nyquistPower = 0; };
Metrics measure(const std::vector<double>& signal, unsigned fundamentalBin) {
    const size_t n = signal.size();
    require(fundamentalBin > 0 && fundamentalBin < n / 2, "invalid carrier bin");
    const auto bins = fft(signal);
    const double normalization = double(n) * double(n);
    Metrics result;
    result.dc = bins[0].real() / double(n);
    result.nyquistPower = std::norm(bins[n / 2]) / normalization;
    result.acPower = result.nyquistPower;
    result.residualPower = result.nyquistPower;
    // Allow every integer harmonic below Nyquist, not just odd harmonics.
    // Folded components landing on allowed bins are consequently not counted.
    for (size_t k = 1; k < n / 2; ++k) {
        const double power = 2 * std::norm(bins[k]) / normalization;
        result.acPower += power;
        if (k % fundamentalBin != 0) result.residualPower += power;
    }
    return result;
}

void selfTest() {
    const std::vector<double> shortSignal{1, -2, 3, 4, -5, 6, 0, -1};
    const auto bins = fft(shortSignal);
    for (size_t k = 0; k < shortSignal.size(); ++k) {
        Complex direct = 0;
        for (size_t n = 0; n < shortSignal.size(); ++n)
            direct += shortSignal[n] * std::polar(1.0, -tau * double(k * n) / shortSignal.size());
        require(std::abs(bins[k] - direct) < 1e-12, "FFT/direct DFT disagreement");
    }
    std::vector<double> signal(size);
    for (unsigned n = 0; n < size; ++n)
        signal[n] = 0.25 + std::sin(tau * 17 * n / size)
            + 0.5 * std::cos(tau * 34 * n / size)
            + 0.125 * std::sin(tau * 23 * n / size)
            + ((n & 1) ? -0.0625 : 0.0625);
    const auto result = measure(signal, 17);
    require(std::abs(result.dc - 0.25) < 1e-12, "DC mismatch");
    require(std::abs(result.acPower - (0.5 + 0.125 + 0.0078125 + 0.00390625)) < 1e-12,
            "AC Parseval/normalization mismatch");
    require(std::abs(result.residualPower - (0.0078125 + 0.00390625)) < 1e-12,
            "nonharmonic or Nyquist energy mismatch");
    const auto silent = measure(std::vector<double>(size), 17);
    require(silent.acPower == 0 && silent.residualPower == 0, "silence mismatch");
    // Independent sine oracle checks the public operator path, coherent
    // frequency selection, steady envelope and sample-before/after phase order.
    FmOpPatch patch;
    FmOperator op;
    op.configure(patch, rate, 0, true);
    op.noteOn(double(rate) * 127 / size);
    for (unsigned n = 0; n < warmup; ++n) (void)op.tick(0);
    for (unsigned n = 0; n < size; ++n)
        require(std::abs(op.tick(0) - std::sin(tau * 127 * (n + 1) / size)) < 1e-6,
                "steady sine operator differs from analytic oracle");
    op.noteOn(double(rate) / 2);
    for (unsigned n = 0; n < size; ++n)
        require(op.tick(0) == 0, "existing Nyquist guard changed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool testOnly = argc == 2 && std::string(argv[1]) == "--self-test";
        const bool bandlimited = argc == 2 && std::string(argv[1]) == "--bandlimited";
        if (argc != 1 && !testOnly && !bandlimited) {
            std::cerr << "usage: fm-spectrum-audit [--self-test | --bandlimited]\n";
            return 2;
        }
        selfTest();
        if (testOnly) { std::cout << "PASS: spectrum and operator oracles\n"; return 0; }
        std::cout << std::setprecision(12)
            << "{\n  \"schema_version\": 1,\n  \"sample_rate\": " << rate
            << ", \"frames\": " << size << ", \"warmup_frames\": " << warmup
            << ",\n  \"scope\": \"Unmodulated steady modern carrier; no player filter, PCM16, handset or listening test\",\n"
            << "  \"bandlimited_carrier\": " << (bandlimited ? "true" : "false") << ",\n"
            << "  \"metric\": \"AC energy outside all integer-harmonic bins below Nyquist, including Nyquist; DC excluded. Folded energy colliding with harmonic bins is not counted.\",\n"
            << "  \"results\": [\n";
        bool first = true;
        for (unsigned wave : {0u, 6u, 16u, 24u}) for (unsigned bin : {17u, 127u, 401u}) {
            FmOpPatch patch; patch.wave = uint8_t(wave);
            FmOperator op;
            op.configure(patch, rate, 0, true);
            op.noteOn(double(rate) * bin / size);
            op.setBandlimited(bandlimited);
            for (unsigned n = 0; n < warmup; ++n) (void)op.tick(0);
            std::vector<double> signal(size);
            for (auto& sample : signal) sample = op.tick(0);
            const auto result = measure(signal, bin);
            require(std::isfinite(result.acPower) && result.acPower > 0
                    && std::isfinite(result.residualPower), "invalid spectral energy");
            if (!first) std::cout << ",\n";
            first = false;
            std::cout << "    {\"wave\": " << wave << ", \"fundamental_bin\": " << bin
                << ", \"frequency_hz\": " << double(rate) * bin / size
                << ", \"ac_power\": " << result.acPower << ", \"dc\": " << result.dc
                << ", \"nyquist_power\": " << result.nyquistPower
                << ", \"nonharmonic_power\": " << result.residualPower
                << ", \"nonharmonic_fraction\": " << result.residualPower / result.acPower << "}";
        }
        std::cout << "\n  ]\n}\n";
        std::cout.flush();
        require(bool(std::cout), "JSON output failed");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fm-spectrum-audit: " << e.what() << '\n'; return 1;
    }
}
