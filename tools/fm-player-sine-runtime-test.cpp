// SPDX-License-Identifier: Apache-2.0
// Reuse the original ATR fixtures, including genuine pool steals and gates.
#define MMF_ATR_RUNTIME_NO_MAIN
#include "atr-runtime-end-test.cpp"

int main() {
    size_t cases = 0;
    for (int format : {2, 3}) for (bool wt : {false, true}) for (int later : {0, 1, 2}) {
        const auto longFile = score(format, true, wt, later);
        const auto shortFile = score(format, false, wt, later);
        MaPlayer old;
        require(old.init(shortFile, 8000), "original horizon control init");
        const auto native = drain(old, 257, 8000);
        for (int factor : {1, 8}) {
            MaPlayer actual, control;
            require(actual.init(longFile, 8000, false, {factor, FmSubframeMath::Hermite}) &&
                    control.init(shortFile, 8000, false, {factor, FmSubframeMath::Hermite}),
                    "experimental horizon init");
            const auto initial = actual.totalSamples();
            const auto expected = drain(control, 257, 8000);
            const auto output = drain(actual, 31, 8000);
            require(output == expected, "stolen ATR changes filtered surviving sources or safety fade");
            require(actual.totalSamples() == old.totalSamples() + 72 && actual.totalSamples() < initial,
                    "runtime source horizon or physical upper bound is wrong");
            require(actual.diagnostics().stolenPcm == 1 && actual.diagnostics().tailLimitReached,
                    "runtime steal and hard-tail diagnostics changed");
            if (factor == 1 || wt) {
                require(output.size() == native.size() + 144 &&
                        std::equal(native.begin(), native.end(), output.begin() + 144),
                        "native-only sources no longer equal exact delayed original output");
            }
            if (format == 2 && !wt && later == 0) {
                actual.seekToStart();
                require(actual.totalSamples() == initial, "seek lost initial conservative source horizon");
                require(drain(actual, 1024, 8000) == expected, "seek lost runtime horizon or bus state");
            }
            ++cases;
        }
    }
    for (bool mute : {false, true}) for (int factor : {1, 8}) {
        MaPlayer p;
        require(p.init(score(2, true, false, 0, true, mute, true), 8000, false, {factor}),
                "live recording init");
        float buffer[2048]; unsigned frames = 0;
        while (frames < 8000 * 11) {
            const int n = p.render(buffer, int(std::min(1024u, 8000 * 11 - frames)));
            require(n > 0, "live/muted ATR ended before its actual source lifetime");
            frames += unsigned(n);
        }
        require(!p.finished() && !p.diagnostics().tailLimitReached, "live recording reached false safety EOF");
        ++cases;
    }
    std::cout << cases << " experimental ATR horizon/steal/seek/live-source cases PASS\n";
}
