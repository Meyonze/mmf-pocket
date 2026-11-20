// SPDX-License-Identifier: Apache-2.0
// Deterministic oversampling budget for adaptive nested FM (opt-in, not used by
// the app). Original synthetic voices only; host checks, not device behaviour.
#define main existingNestedPlayerMain
#include "fm-nested-player-test.cpp"
#undef main
#include <cstring>
#include <iostream>

using namespace fxchain::smaf;
namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct Run { std::vector<float> out; int maxUnits = 0, admitted = 0, declined = 0; int grantSum = 0; };

// Strong TL12 nested voices; q selects the density table's factor (0.32 -> 128x).
Run render(int voices, double q, int block, int budget, int frames) {
    MaPlayer p;
    require(p.init(nested_player::clockFile(), 44100, false,
                   SineRenderOptions{8, FmSubframeMath::Hermite, true, false, true}), "adaptive init");
    p.nestedBudgetUnits_ = budget;
    p.endSample_ = uint64_t(frames) + 100000;
    p.scoreEndSample_ = p.endSample_;
    const auto patch = nested::patch({96, 7, 3, 5, 12, 12});
    for (int i = 0; i < voices; ++i) {
        auto& v = p.pool_[size_t(i)];
        v.noteOn(patch, q * 44100 / 7 * (1 - .002 * i), .05f);
        v.channel = 0;
        p.poolPanLeft_[size_t(i)] = .7f;
        p.poolPanRight_[size_t(i)] = .3f;
    }
    Run r;
    std::vector<float> buffer(size_t(block) * 2);
    int done = 0;
    while (done < frames) {
        const int n = p.render(buffer.data(), std::min(block, frames - done));
        require(n > 0, "render ended early");
        r.out.insert(r.out.end(), buffer.begin(), buffer.begin() + n * 2);
        done += n;
        if (block == 1) {
            int total = 0;
            for (auto& v : p.pool_) if (v.active()) total += v.budget.units;
            r.maxUnits = std::max(r.maxUnits, total);
        }
    }
    for (int i = 0; i < voices; ++i) {
        const int g = p.pool_[size_t(i)].budget.grant;
        if (g > 0) { ++r.admitted; r.grantSum += g; }
        if (g < 0) { ++r.declined; require(p.pool_[size_t(i)].budget.units == 0, "declined voice uses no sub-samples"); }
    }
    return r;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

void decisionLogic() {
    MaPlayer p;
    p.nestedBudgetUnits_ = 512;
    FmVoice::NestedBudget b;
    require(p.grantNestedBudget_(b, 128, 384) == 128 && b.grant == 128, "new voice that exactly fits is admitted");
    FmVoice::NestedBudget d;
    require(p.grantNestedBudget_(d, 64, 480) == 0 && d.grant == -1, "new voice over budget is declined");
    require(p.grantNestedBudget_(d, 16, 0) == 0 && d.grant == -1, "declined voice stays declined for the note");
    FmVoice::NestedBudget keep;
    require(p.grantNestedBudget_(keep, 16, 0) == 16, "admit at 16");
    require(p.grantNestedBudget_(keep, 128, 400) == 16 && keep.grant == 16, "promotion that does not fit keeps latched factor");
    require(p.grantNestedBudget_(keep, 64, 400) == 64 && keep.grant == 64, "promotion that fits is taken");
    require(p.grantNestedBudget_(keep, 32, 0) == 64, "never demoted by a smaller request");
    FmVoice::NestedBudget none;
    require(p.grantNestedBudget_(none, 0, 0) == 0 && none.grant == 0, "voice outside the class stays undecided");
}
} // namespace

int main() {
    decisionLogic();

    constexpr int frames = 3000;
    // 32 voices all requesting 128x: only four fit in 512 units.
    const Run capped = render(32, .32, 1, 512, frames);
    require(capped.maxUnits <= 512, "per-frame sub-sample total stays within the budget");
    require(capped.admitted == 4 && capped.grantSum == 512 && capped.declined == 28,
            "four 128x voices admitted, the rest declined");
    // Frame-by-frame state means block size must not change a single bit.
    for (int block : {17, 1024})
        require(same(capped.out, render(32, .32, block, 512, frames).out),
                "output is bit-identical across block sizes 1/17/1024");
    // The budget has an effect when exceeded, and none when not.
    require(!same(capped.out, render(32, .32, 1024, 1 << 20, frames).out), "an exceeded budget changes the output");
    const Run two = render(2, .32, 1024, 512, frames);
    require(same(two.out, render(2, .32, 1024, 1 << 20, frames).out), "within budget output equals an unlimited budget");
    require(two.admitted == 2 && two.declined == 0, "two voices both admitted");
    std::cout << "Adaptive nested budget decisions, 512-unit cap, block independence and "
                 "within-budget equivalence PASS\n";
    return 0;
}
