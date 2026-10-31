// SPDX-License-Identifier: Apache-2.0
// Renderer phase-continuity fixtures, not chip phase-accumulator calibration.
#include "ma_fm_core.h"
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace fxchain::smaf;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void continuity(bool modern, bool vibrato) {
    constexpr double rate = 8000;
    FmOpPatch patch;
    patch.vib = vibrato; patch.dvb = 3;
    FmOperator oscillator;
    oscillator.configure(patch, rate, 0, modern);
    oscillator.noteOn(250);
    double phase = 0;
    int count = 0, muted = 0;
    // Pitch enters the renderer's silent region repeatedly, including a note
    // initially above Nyquist. Source time must advance in those regions too.
    for (double frequency : {250.0, 4150.0, 375.0, 4800.0, 625.0, 16000.0, 125.0}) {
        oscillator.setBaseFreq(frequency);
        for (int frame = 0; frame < 37; ++frame, ++count) {
            const double lfo = std::sin(2 * pi * 7 * (count + 1) / rate);
            const double shift = vibrato ? 1 + 0.01548 * lfo * 8 : 1;
            phase += frequency * shift / rate;
            phase -= std::floor(phase);
            const float expected = frequency >= rate / 2 ? 0.0f : float(std::sin(2 * pi * phase));
            const float actual = oscillator.tick(0, lfo, 8);
            require(std::isfinite(actual) && std::fabs(actual - expected) < 2e-6,
                    "pitch return follows continuous integrated phase through muted intervals");
            if (frequency >= rate / 2) { require(actual == 0, "Nyquist suppression remains exact"); ++muted; }
        }
    }
    require(muted == 111, "fixture exercises three muted intervals");
    oscillator.noteOn(5000); phase = 0;
    for (int frame = 0; frame < 11; ++frame) {
        require(oscillator.tick(0) == 0, "initial high note remains silent");
        phase += 5000.0 / rate; phase -= std::floor(phase);
    }
    oscillator.setBaseFreq(250);
    phase += 250.0 / rate; phase -= std::floor(phase);
    require(std::fabs(oscillator.tick(0) - std::sin(2 * pi * phase)) < 2e-6,
            "an initially muted note retains elapsed phase when pitched down");
    oscillator.noteOn(250);
    require(std::fabs(oscillator.tick(0) - std::sin(2 * pi * 250.0 / rate)) < 2e-6,
            "a new note still explicitly resets phase");
}
void envelope() {
    FmOpPatch patch; patch.rr = 15;
    FmOperator oscillator;
    oscillator.configure(patch, 8000);
    oscillator.noteOn(5000); oscillator.tick(0); oscillator.noteOff();
    for (int n = 0; n < 8000; ++n) require(oscillator.tick(0) == 0, "released high note remains silent");
    require(oscillator.finished(), "muted operator still completes its release envelope");
    oscillator.setBaseFreq(250);
    require(oscillator.tick(0) == 0, "expired high note does not revive on pitch return");
}
void voiceLfo(bool modern, int rate, int code) {
    constexpr double frequencies[4] = {1.8, 4.0, 5.9, 7.0};
    FmVoice voice;
    voice.setSampleRate(rate);
    // An independent operator plus an explicit time-based LFO is the oracle
    // for a voice with one silent modulator and one sine carrier. Reuse the
    // same voice through none/VIB/AM/both/none to exercise cached-state resets.
    for (int mode : {0, 1, 2, 3, 0}) {
        FmVoicePatch patch;
        patch.modernTimbre = modern;
        patch.lfo = uint8_t(code);
        patch.ops[0].ar = 0;
        auto& carrier = patch.ops[1];
        carrier.vib = (mode & 1) != 0;
        carrier.am = (mode & 2) != 0;
        carrier.dvb = carrier.dam = 0; // minimum enabled depth is still nonzero
        carrier.rr = 15;
        voice.setVolume(1);
        voice.noteOn(patch, 220, 1);
        FmOperator reference;
        reference.configure(carrier, rate, 0, modern);
        reference.noteOn(220);
        double scale = 1;
        float volume = 1;
        for (int frame = 0; frame < 4096; ++frame) {
            if (frame == 1000) { scale = 0; voice.setVibratoScale(scale); }
            if (frame == 1500) { scale = 2; voice.setVibratoScale(scale); }
            if (frame == 1700) { volume = 0.25f; voice.setVolume(volume); }
            if (frame == 2000) { voice.setPitch(440); reference.setBaseFreq(440); }
            if (frame == 2500) { voice.noteOff(); reference.noteOff(); }
            const double phase = std::fmod((frame + 1) * frequencies[code] / rate, 1.0);
            const double lfo = std::sin(2 * pi * phase);
            const float expected = float(double(reference.tick(0, lfo, scale)) * volume * 0.7);
            const float actual = voice.tick();
            require(std::isfinite(actual) && std::fabs(actual - expected) < 2e-6,
                    "voice LFO matches independent operator schedule through controls and retriggers");
        }
    }
}
void retainedOperators(bool modern, int rate) {
    FmVoice voice;
    voice.setSampleRate(rate);
    std::array<FmOperator, 4> reference;
    FmVoicePatch patch;
    patch.modernTimbre = modern;
    patch.fourOp = true;
    patch.algorithm = 2;
    for (auto& op : patch.ops) op.am = op.vib = true;
    // Preserve the existing public API even for inconsistent synthetic patches:
    // a 2-op configuration can select an algorithm that also ticks old ops2/3.
    // Their modulation must survive an intervening ordinary 2-op note.
    for (int stage = 0; stage < 3; ++stage) {
        if (stage == 1) {
            patch = FmVoicePatch{};
            patch.modernTimbre = modern;
            patch.ops[0].ar = 0;
        }
        if (stage == 2) patch.algorithm = 2;
        voice.noteOn(patch, 220, 1);
        for (int i = 0; i < (patch.fourOp ? 4 : 2); ++i) {
            reference[i].configure(patch.ops[i], rate, 0, modern);
            reference[i].noteOn(220);
        }
        for (int frame = 0; frame < 4096; ++frame) {
            const double phase = std::fmod((frame + 1) * 1.8 / rate, 1.0);
            const double lfo = std::sin(2 * pi * phase);
            const double a = reference[0].tick(0, lfo);
            const double b = reference[1].tick(0, lfo);
            double mixed = b;
            if (stage != 1) {
                const double c = reference[2].tick(0, lfo);
                const double d = reference[3].tick(0, lfo);
                mixed = (a + b + c + d) * 0.35;
            }
            require(std::fabs(voice.tick() - float(mixed * 0.7)) < 2e-6,
                    "retrigger restores LFO for every operator actually used by the algorithm");
        }
    }
}
void fixedWaveValues() {
    const std::array<double, 9> phases{-0.25, -0.0, 0.0, 0.125, 0.25,
        0.5, 0.75, 1.0, -std::numeric_limits<double>::denorm_min()};
    // Exact dyadic signal values: square, cubic, half-square, triangle,
    // repeated quarter-square, saw, modern WS30, unbound user wave.
    const float values[9][8] = {
        {-1,-0.125f,0,-1,0,-0.5f,0,0},
        {1,1,1,0,1,0,1,0}, {1,1,1,0,1,0,1,0},
        {1,0.421875f,1,0.5f,1,0.25f,1,0},
        {1,0.125f,1,1,0,0.5f,0,0},
        {-1,-0.0f,0,0,1,-1,0,0},
        {-1,-0.125f,0,-1,0,-0.5f,0,0},
        {1,1,1,0,1,0,1,0}, {-1,-1,0,0,1,0,0,0}
    };
    for (bool modern : {false, true}) for (int wave = 0; wave < 256; ++wave) {
        int column = -1;
        switch (wave & 31) {
            case 6: column=0; break; case 7: column=1; break;
            case 14: column=2; break; case 16: column=3; break;
            case 22: column=4; break; case 24: column=5; break;
            case 30: column=modern && wave==30 ? 6 : 2; break;
            case 15: case 23: case 31: column=7; break;
        }
        if (column < 0) continue;
        FmOpPatch patch; patch.wave=uint8_t(wave);
        FmOperator oscillator;
        oscillator.configure(patch, 8192, 0, modern);
        oscillator.noteOn(0); // zero advance makes the supplied phase exact
        for (size_t i=0; i<phases.size(); ++i) {
            const float actual=oscillator.tick(phases[i]);
            const float expected=values[i][column];
            require(actual==expected && std::signbit(actual)==std::signbit(expected),
                    "fixed wave values retain aliases, endpoints, signed zero and modern WS30 scope");
        }
    }
}
void foldedWaveValues() {
    const std::array<int, 5> waves{19, 22, 26, 27, 29};
    const float values[9][5] = {
        {0,1,0,0,0}, {0.5f,1,0.25f,0.25f,0.5f},
        {0,0,0.5f,0,0}, {0,0,0.75f,0,0.5f},
        {0,1,0,0,0}, {0.5f,1,0.25f,0.25f,0},
        {0,0,0.5f,0,0}, {0,0,0.75f,0,0}, {0,1,0,0,0}
    };
    const int previousMode = std::fegetround();
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        require(std::fesetround(mode) == 0, "test rounding mode is supported");
        for (bool modern : {false, true}) for (int alias = 0; alias < 8; ++alias)
        for (size_t column = 0; column < waves.size(); ++column) {
            FmOpPatch patch; patch.wave = uint8_t(waves[column] + alias * 32);
            FmOperator oscillator;
            oscillator.configure(patch, 8192, 0, modern);
            oscillator.noteOn(0);
            for (int step = 0; step <= 8; ++step) {
                const float actual = oscillator.tick(step / 8.0);
                float expected = values[step][column];
                // Integer-phase zero can depend on compiler treatment of
                // normalization under nondefault rounding. The positive
                // zeros at quarter/half boundaries are unambiguous.
                const bool checkSign = !(mode == FE_DOWNWARD && (step == 0 || step == 8));
                if (actual != expected || (checkSign && std::signbit(actual) != std::signbit(expected)))
                    std::cerr << "Folded wave mismatch mode=" << mode << " wave=" << int(patch.wave)
                              << " step=" << step << " actual=" << actual << " sign=" << std::signbit(actual)
                              << " expected=" << expected << " sign=" << std::signbit(expected) << '\n';
                require(actual == expected && (!checkSign || std::signbit(actual) == std::signbit(expected)),
                        "folded waves retain exact dyadic values and zero signs in every rounding mode");
            }
            for (double phase : {std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
                const float actual = oscillator.tick(phase);
                require(waves[column] == 26 ? std::isnan(actual)
                            : actual == 0.0f && !std::signbit(actual),
                        "folded waves retain nonfinite phase output classification");
            }
        }
    }
    require(std::fesetround(previousMode) == 0, "test restores rounding mode");
}
int main() {
    for (bool modern : {false, true}) for (bool vibrato : {false, true}) continuity(modern, vibrato);
    envelope();
    fixedWaveValues();
    foldedWaveValues();
    for (bool modern : {false, true}) for (int rate : {8000, 44100})
    {
        for (int code = 0; code < 4; ++code) voiceLfo(modern, rate, code);
        retainedOperators(modern, rate);
    }
    std::cout << "FM phase tests PASS: pitch return, suppression, voice LFO, controls, retrigger and release\n";
}
