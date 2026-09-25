// SPDX-License-Identifier: Apache-2.0
//
// ma_fm_core.cpp -- the fm dsp. phase generator, exponential adsr, operators,
//                   2-op/4-op algorithms, and a compact built-in gm patch bank.
// ----------------------------------------------------------------------------
// Floating-point approximation. Related Yamaha documentation informs envelope
// semantics and rate timing; this is not measured or bit-exact handset emulation.

#include "ma_fm_core.h"

#include <cmath>
#include <algorithm>

namespace fxchain::smaf {

namespace {
constexpr double kTwoPi = 6.283185307179586;

// total-level (attenuation) -> linear gain. the chip steps 0.75 dB per tl unit,
// 63 units ~= -48 dB ~= silence. classic OPL scale.
inline double tlToGain(uint8_t tl) {
    return std::pow(10.0, (-0.75 * double(tl)) / 20.0);
}

// A chip rate mapped to a fraction of the documented 0..96 dB transition time.
inline double rateToStep(uint8_t rate, double sampleRate, bool attack, int offset) {
    // Related Yamaha YMF825 manual, EG rate table. Approximation of the
    // continuous curve, not a cycle-accurate MA chip emulator. Rate zero must
    // stay stopped, including when key scaling is enabled.
    if (rate == 0) return 0.0;
    int r = std::clamp(int(rate) * 4 + offset, 4, 63);
    if (attack && r >= 60) return 1.0;
    static constexpr double attackMs[16] = {
        2942.78,2354.23,1961.86,1681.59,1471.39,1177.11,980.92,840.80,
        735.69,588.55,490.47,420.40,367.85,294.28,245.23,210.20
    };
    static constexpr double otherMs[4] = {43008.0,34406.4,28672.0,24576.0};
    double ms;
    if (attack) {
        // Base table 4..19; each increment of four approximately halves time.
        ms = attackMs[(r - 4) % 16] * std::pow(0.5, 4 * ((r - 4) / 16));
    } else {
        ms = r >= 60 ? 2.63 : otherMs[(r - 4) % 4] * std::pow(0.5, (r - 4) / 4);
    }
    return 1.0 / std::max(1.0, ms * sampleRate / 1000.0);
}
} // namespace

// ── FmVoicePatch ─────────────────────────────────────────────────────────────
FmVoicePatch FmVoicePatch::defaultPatch() {
    FmVoicePatch v;
    v.fourOp = false;
    v.algorithm = 0;   // op0 modulates op1 (carrier)
    v.feedback = 0;
    // op0 modulator: bright-ish ratio 2, quick decay
    v.ops[0] = FmOpPatch{ /*multi*/2, /*tl*/20, /*ar*/15, /*dr*/6, /*sr*/2,
                          /*rr*/7, /*sl*/4, /*ksl*/0, /*ksr*/0, /*wave*/0,
                          /*dt*/0, /*fb*/0, /*am*/false, /*vib*/false, /*egType*/true };
    // op1 carrier: full level, sustaining
    v.ops[1] = FmOpPatch{ /*multi*/1, /*tl*/0,  /*ar*/15, /*dr*/4, /*sr*/1,
                          /*rr*/7, /*sl*/2, /*ksl*/0, /*ksr*/0, /*wave*/0,
                          /*dt*/0, /*fb*/0, /*am*/false, /*vib*/false, /*egType*/true };
    return v;
}

const FmVoicePatch& FmVoicePatch::gmApprox(int program) {
    // a small, deterministic, rom-free approximation of the gm map. we key a
    // handful of timbral families off the gm program ranges and reuse a base
    // patch with tweaked modulator ratio / decay. it is intentionally compact:
    // the goal is "sounds like the right KIND of instrument", not sample-accuracy.
    //
    // built via a c++ magic static (thread-safe once-init): two decoders CAN
    // hit this concurrently (a SmartScan probe + a playback open), and a plain
    // `static bool built` flag would race.
    static const std::array<FmVoicePatch, 128> bank = [] {
        std::array<FmVoicePatch, 128> b{};
        for (int i = 0; i < 128; ++i) {
            FmVoicePatch v = FmVoicePatch::defaultPatch();
            int fam = i / 8;                 // 16 gm families
            switch (fam) {
                case 0:  v.ops[0].multi=1; v.ops[0].dr=7; break;      // pianos
                case 1:  v.ops[0].multi=2; v.ops[0].tl=14; break;     // chromatic perc
                case 2:  v.ops[0].multi=1; v.ops[1].sr=0; v.ops[0].dr=2; break; // organs (sustain)
                case 3:  v.ops[0].multi=1; v.ops[0].dr=5; break;      // guitars
                case 4:  v.ops[0].multi=2; v.ops[0].dr=4; break;      // basses
                case 5:  v.ops[0].multi=1; v.ops[1].sr=0; v.ops[0].dr=1; break; // strings
                case 6:  v.ops[0].multi=1; v.ops[1].sr=0; break;      // ensemble
                case 7:  v.ops[0].multi=3; v.ops[1].sr=0; break;      // brass
                case 8:  v.ops[0].multi=1; v.ops[1].sr=0; v.ops[0].tl=26; break; // reed
                case 9:  v.ops[0].multi=1; v.ops[1].sr=0; v.ops[0].tl=28; break; // pipe
                case 10: v.ops[0].multi=4; v.ops[0].wave=1; break;    // synth lead
                case 11: v.ops[0].multi=1; v.ops[0].wave=2; v.ops[1].sr=0; break; // synth pad
                case 12: v.ops[0].multi=6; v.ops[0].wave=3; break;    // synth fx
                case 13: v.ops[0].multi=2; v.ops[0].dr=6; break;      // ethnic
                case 14: v.ops[0].multi=8; v.ops[0].dr=10; v.ops[0].wave=1; break; // percussive
                default: v.ops[0].multi=12; v.ops[0].wave=4; v.ops[0].dr=12; break; // sfx
            }
            b[i] = v;
        }
        return b;
    }();
    if (program < 0) program = 0;
    if (program > 127) program = 127;
    return bank[program];
}

const FmVoicePatch& FmVoicePatch::drumApprox(int note) {
    // three short percussive hits, all egType=false so they retire on their own.
    static const std::array<FmVoicePatch, 3> kit = [] {
        std::array<FmVoicePatch, 3> k{};
        // kick-ish: sub carrier, fast pitch-less thump.
        k[0] = FmVoicePatch::defaultPatch();
        k[0].ops[0] = FmOpPatch{ /*multi*/0, /*tl*/8,  /*ar*/15, /*dr*/10, /*sr*/0,
                                 /*rr*/12, /*sl*/15, 0,0, /*wave*/0, 0, 0, false, false, false };
        k[0].ops[1] = FmOpPatch{ /*multi*/0, /*tl*/0,  /*ar*/15, /*dr*/9,  /*sr*/0,
                                 /*rr*/12, /*sl*/15, 0,0, /*wave*/0, 0, /*fb*/5, false, false, false };
        // snare-ish: mid noise burst (high-multi modulator, heavy feedback).
        k[1] = FmVoicePatch::defaultPatch();
        k[1].ops[0] = FmOpPatch{ /*multi*/11, /*tl*/4, /*ar*/15, /*dr*/9,  /*sr*/0,
                                 /*rr*/11, /*sl*/15, 0,0, /*wave*/0, 0, /*fb*/7, false, false, false };
        k[1].ops[1] = FmOpPatch{ /*multi*/1,  /*tl*/2, /*ar*/15, /*dr*/8,  /*sr*/0,
                                 /*rr*/11, /*sl*/15, 0,0, /*wave*/0, 0, 0, false, false, false };
        // hat-ish: short metallic tick.
        k[2] = FmVoicePatch::defaultPatch();
        k[2].ops[0] = FmOpPatch{ /*multi*/15, /*tl*/6, /*ar*/15, /*dr*/12, /*sr*/0,
                                 /*rr*/13, /*sl*/15, 0,0, /*wave*/0, 0, /*fb*/7, false, false, false };
        k[2].ops[1] = FmOpPatch{ /*multi*/9,  /*tl*/8, /*ar*/15, /*dr*/12, /*sr*/0,
                                 /*rr*/13, /*sl*/15, 0,0, /*wave*/0, 0, 0, false, false, false };
        return k;
    }();
    return kit[note < 44 ? 0 : note < 52 ? 1 : 2];
}

// ── FmEnvelope ───────────────────────────────────────────────────────────────
void FmEnvelope::configure(const FmOpPatch& p, double sampleRate, int rateOffset) {
    atkStep_ = rateToStep(p.ar, sampleRate, true, rateOffset);
    decStep_ = std::exp(-11.052408446 * rateToStep(p.dr, sampleRate, false, rateOffset));
    susStep_ = std::exp(-11.052408446 * rateToStep(p.sr, sampleRate, false, rateOffset));
    relStep_ = std::exp(-11.052408446 * rateToStep(p.rr, sampleRate, false, rateOffset));
    // Yamaha-family sustain attenuation: 3 dB steps, final code ~93 dB.
    susLevel_ = std::pow(10.0, -(p.sl == 15 ? 93.0 : 3.0 * p.sl) / 20.0);
    sustaining_ = p.egType;
    xof_ = p.xof;
    phase_ = Phase::Idle;
    level_ = 0.0;
}

void FmEnvelope::keyOn() { level_ = 0.0; phase_ = Phase::Attack; }

void FmEnvelope::keyOff() {
    // XOF ignores key-off. A non-decaying authored voice is still bounded by
    // the player's song-end backstop, without changing its envelope semantics.
    if (xof_) return;
    if (phase_ != Phase::Idle) phase_ = Phase::Release;
}

float FmEnvelope::advance() {
    switch (phase_) {
        case Phase::Idle: return 0.0f;
        case Phase::Attack:
            if (atkStep_ == 0.0) break;
            level_ += (1.0 - level_) * std::min(1.0, 9.210340372 * atkStep_);
            if (level_ >= 0.9999) level_ = 1.0;
            if (level_ >= 1.0) { level_ = 1.0; phase_ = Phase::Decay; }
            break;
        case Phase::Decay:
            level_ *= decStep_; // exponential amplitude / linear dB decay
            if (level_ <= susLevel_) {
                level_ = susLevel_;
                phase_ = sustaining_ ? Phase::Sustain : Phase::Release;
            }
            break;
        case Phase::Sustain:
            // second-decay (sr): a slow bleed toward silence, chip behaviour.
            level_ *= susStep_;
            if (level_ <= 0.000015849) { level_ = 0.0; phase_ = Phase::Idle; }
            break;
        case Phase::Release:
            level_ *= relStep_;
            if (level_ <= 0.000015849) { level_ = 0.0; phase_ = Phase::Idle; }
            break;
    }
    return float(level_);
}

// ── FmOperator ───────────────────────────────────────────────────────────────
void FmOperator::configure(const FmOpPatch& p, double sampleRate, int rateOffset) {
    patch_ = p;
    sampleRate_ = sampleRate;
    wave_ = p.wave;
    tlGain_ = tlToGain(p.tl);
    // vibrato depth (DVB 0..3) as a phase-inc factor: ~3.4/6.7/13.4/26.8 cents,
    // the MA/YMF825 depth family. tremolo depth (DAM 0..3) as a linear gain
    // span: ~1.2/2.4/4.8/9.6 dB dips. both only engage when VIB/AM are set.
    static constexpr double kVib[4] = { 0.00196, 0.00387, 0.00774, 0.01548 };
    static constexpr float  kTrem[4] = { 0.129f, 0.242f, 0.424f, 0.669f };
    vibAmt_ = p.vib ? kVib[p.dvb & 3] : 0.0;
    amAmt_  = p.am  ? kTrem[p.dam & 3] : 0.0f;
    env_.configure(p, sampleRate, rateOffset);
}

void FmOperator::recalc_() {
    // MA/YMF825 frequency multiple: value 0 => x0.5, value n(1..15) => x n.
    // LINEAR, not the OPL {..10,10,12,12,15,15} doubling table (that gave every
    // high-MULTI voice the wrong operator ratio, a big source of the harshness).
    double m = (patch_.multi == 0) ? 0.5 : double(patch_.multi & 15);
    // DT is sign/magnitude, not an offset-binary value. DT=0 and 4 are neutral.
    double detune = 1.0 + ((patch_.dt & 4) ? -1.0 : 1.0) * (patch_.dt & 3) * 0.0006;
    phaseInc_ = (freqHz_ * m * detune) / sampleRate_;
    // key-scale level: attenuate as the note rises above middle C, 0/1.5/3/6 dB
    // per octave by KSL (high notes thin out on the chip instead of blaring).
    static constexpr double kKslDbPerOct[4] = { 0.0, 3.0, 1.5, 6.0 };
    double oct = std::log2(std::max(freqHz_, 1.0) / 261.63);
    double att = kKslDbPerOct[patch_.ksl & 3] * std::max(0.0, oct);
    kslGain_ = std::pow(10.0, -att / 20.0);
    // nyquist guard: a high note through a high MULTI (percussion patches use
    // 14/15) can land the operator above fs/2; mute it instead of splattering
    // alias noise across the band.
    nyMute_ = (phaseInc_ >= 0.5);
}

void FmOperator::noteOn(double freqHz) {
    freqHz_ = freqHz;
    phase_ = 0.0;
    recalc_();
    env_.keyOn();
}

void FmOperator::noteOff() { env_.keyOff(); }

float FmOperator::waveform_(uint8_t wave, double phase01) {
    // SD-1/VM35's 29 published operator shapes. Values 15, 23 and 31 are
    // reserved. Earlier code folded every value onto the eight OPL shapes with
    // `wave & 7`; that made most authored custom voices use the wrong oscillator
    // and was a major source of poor timbre reproduction.
    double x = phase01 - std::floor(phase01);
    double s = std::sin(kTwoPi * x);
    auto positiveHalf = [](double value) { return value > 0.0 ? value : 0.0; };
    auto triangle = [](double phase) {
        return phase < 0.25 ? phase * 4.0
             : phase < 0.75 ? 2.0 - phase * 4.0
                            : phase * 4.0 - 4.0;
    };
    auto positiveTriangle = [](double phase) {
        return phase < 0.25 ? phase * 4.0
             : phase < 0.5 ? 2.0 - phase * 4.0
                           : 0.0;
    };
    auto clipped = [](double value) { return std::clamp(value * 1.7, -1.0, 1.0); };
    switch (wave & 31) {
        case 0: return float(s);                                  // sine
        case 1: return float(positiveHalf(s));                    // half sine
        case 2: return float(std::fabs(s));                       // abs sine (rectified)
        case 3: return float(x < 0.25 ? std::sin(kTwoPi*x)
                           : x >= 0.5 && x < 0.75 ? -std::sin(kTwoPi*x) : 0.0);
        case 4: return float(x < 0.5 ? std::sin(kTwoPi * 2.0 * x) : 0.0);
        case 5: return float(x < 0.5 ? std::fabs(std::sin(kTwoPi * 2.0 * x)) : 0.0);
        case 6: return x < 0.5 ? 1.0f : -1.0f;                    // square
        case 7: return float(x < 0.5 ? std::pow(1.0 - x*2.0, 3.0)
                                     : -std::pow(x*2.0 - 1.0, 3.0));

        case 8:  return float(clipped(s));
        case 9:  return float(positiveHalf(clipped(s)));
        case 10: return float(std::fabs(clipped(s)));
        case 11: return float(x < 0.25 ? positiveHalf(clipped(s))
                            : x >= 0.5 && x < 0.75 ? positiveHalf(clipped(-s)) : 0.0);
        case 12: return float(x < 0.5 ? clipped(std::sin(kTwoPi*2.0*x)) : 0.0);
        case 13: return float(x < 0.5 ? std::fabs(clipped(std::sin(kTwoPi*2.0*x))) : 0.0);
        case 14: return x < 0.5 ? 1.0f : 0.0f;
        case 15: return 0.0f;                                    // reserved

        case 16: return float(triangle(x));
        case 17: return float(positiveTriangle(x));
        case 18: return float(std::fabs(triangle(x)));
        case 19: { double h = std::fmod(x, 0.5); return float(h < 0.25 ? h*4.0 : 0.0); }
        case 20: return float(x < 0.5 ? triangle(x*2.0) : 0.0);
        case 21: return float(x < 0.5 ? std::fabs(triangle(x*2.0)) : 0.0);
        case 22: { double h = std::fmod(x, 0.5); return h < 0.25 ? 1.0f : 0.0f; }
        case 23: return 0.0f;                                    // reserved

        case 24: return float(x < 0.5 ? x*2.0 : x*2.0 - 2.0);    // bipolar saw
        case 25: return float(x < 0.5 ? x*2.0 : 0.0);
        case 26: return float(std::fmod(x, 0.5) * 2.0);
        case 27: { double h = std::fmod(x, 0.5); return float(h < 0.25 ? h*2.0 : 0.0); }
        case 28: return float(x < 0.25 ? x*4.0 : x < 0.5 ? x*4.0-2.0 : 0.0);
        case 29: { double q = std::fmod(x, 0.25); return float(x < 0.5 ? q*4.0 : 0.0); }
        case 30: return x < 0.5 ? 1.0f : 0.0f;
        default: return 0.0f;                                    // reserved (31)
    }
}

float FmOperator::tick(double modCycles, double lfoSin) {
    // modCycles: phase modulation input in CYCLES (0..1 = one period), added
    // straight to the phase. the caller scales the modulator output into a
    // musical index, so this stays simple. lfoSin drives this operator's own
    // vibrato (phase-inc scale) and tremolo (gain dip) when VIB/AM are set.
    float envGain = env_.advance();
    if (nyMute_) return 0.0f;      // above fs/2: silent, never alias
    double inc = phaseInc_;
    if (vibAmt_ != 0.0) inc *= 1.0 + vibAmt_ * lfoSin;
    phase_ += inc;
    if (phase_ >= 1.0) phase_ -= std::floor(phase_);
    double out = waveform_(wave_, phase_ + modCycles);
    float amGain = (amAmt_ != 0.0f) ? 1.0f - amAmt_ * float(0.5 + 0.5 * lfoSin) : 1.0f;
    return float(out * tlGain_ * kslGain_ * envGain * amGain);
}

// ── FmVoice ──────────────────────────────────────────────────────────────────
namespace {
// how deep a modulator drives a carrier's phase. a full-scale modulator (+-1)
// shifts the carrier by +-kModDepth cycles. the old code used 1.0 (a whole
// period, index 2*pi) which is why everything screamed; ~0.42 cycles (index
// ~2.6 rad) is the musical FM range yamaha voices sit in and sounds warm.
constexpr double kModDepth = 0.42;
}

void FmVoice::noteOn(const FmVoicePatch& patch, double freqHz, float velocity) {
    patch_ = patch;
    fourOp_ = patch.fourOp;
    algo_ = patch.algorithm & 7;
    feedback_ = patch.feedback;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    for (int i = 0; i < 4; ++i) { fbMem_[i][0] = fbMem_[i][1] = 0.0; opFb_[i] = patch.ops[i].fb; }
    // VMA/2-op voices carry feedback only in the voice header; honour it on op0.
    if (patch.feedback && !opFb_[0]) opFb_[0] = patch.feedback;
    // one LFO per voice, rate selected by the patch (the chip's four speeds).
    static constexpr double kLfoHz[4] = { 1.8, 4.0, 5.9, 7.0 };
    lfoInc_ = kLfoHz[patch.lfo & 3] / sampleRate_;
    lfoPhase_ = 0.0; lfoSin_ = 0.0;
    // Approximate pitch-to-BLOCK/FNUM mapping; use discrete rate offsets rather
    // than multiplying envelope speeds by an arbitrary exponential curve.
    double octave = std::log2(std::max(freqHz, 1.0) / 16.3515978313);
    int block = std::clamp(int(std::floor(octave)), 0, 7);
    int high = (octave - std::floor(octave)) >= 0.5 ? 1 : 0;
    int nops = fourOp_ ? 4 : 2;
    for (int i = 0; i < nops; ++i) {
        int offset = patch.ops[i].ksr ? block * 2 + high : block / 2;
        ops_[i].configure(patch.ops[i], sampleRate_, offset);
        ops_[i].noteOn(freqHz);
    }
    active_ = true;
}

void FmVoice::noteOff() {
    int nops = fourOp_ ? 4 : 2;
    for (int i = 0; i < nops; ++i) ops_[i].noteOff();
}

void FmVoice::setPitch(double freqHz) {
    int nops = fourOp_ ? 4 : 2;
    for (int i = 0; i < nops; ++i) ops_[i].setBaseFreq(freqHz);
}

// run operator i with its own feedback added to the external phase-mod input.
float FmVoice::modOp_(int i, double extModCycles) {
    double fbc = 0.0;
    if (opFb_[i] > 0) {
        // feedback in cycles, averaged over the last two outputs (OPL trick to
        // damp the self-oscillation), scaled gently by the 0..7 amount.
        double avg = (fbMem_[i][0] + fbMem_[i][1]) * 0.5;
        fbc = avg * (double(opFb_[i]) / 24.0);
    }
    float o = ops_[i].tick(extModCycles + fbc, lfoSin_);
    fbMem_[i][1] = fbMem_[i][0]; fbMem_[i][0] = o;
    return o;
}

float FmVoice::tick() {
    if (!active_) return 0.0f;
    // advance the voice LFO once; every VIB/AM-enabled operator reads it.
    lfoPhase_ += lfoInc_;
    if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    lfoSin_ = std::sin(2.0 * 3.14159265358979 * lfoPhase_);
    const double d = kModDepth;
    double out = 0.0;

    // the real MA/YMF825 connection algorithms (enums/voice.go). op indices 0..3
    // = chip ops 1..4. carriers are summed; "a->b" = a modulates b's phase.
    switch (algo_) {
        case 0: {                                   // FB(1)->2        [2-op]
            double m = modOp_(0, 0.0);
            out = modOp_(1, m * d);
        } break;
        case 1: {                                   // FB(1) + 2       [2-op]
            double a = modOp_(0, 0.0);
            double b = modOp_(1, 0.0);
            out = (a + b) * 0.5;
        } break;
        case 2: {                                   // FB(1)+2+FB(3)+4 (all direct)
            double a = modOp_(0, 0.0);
            double b = modOp_(1, 0.0);
            double c = modOp_(2, 0.0);
            double e = modOp_(3, 0.0);
            out = (a + b + c + e) * 0.35;
        } break;
        case 3: {                                   // (FB(1)+2->3)->4
            double a = modOp_(0, 0.0);
            double b = modOp_(1, 0.0);
            double c = modOp_(2, (a + b) * d);
            out = modOp_(3, c * d);
        } break;
        case 4: {                                   // FB(1)->2->3->4 (serial)
            double a = modOp_(0, 0.0);
            double b = modOp_(1, a * d);
            double c = modOp_(2, b * d);
            out = modOp_(3, c * d);
        } break;
        case 5: {                                   // FB(1)->2 + FB(3)->4
            double a = modOp_(0, 0.0);
            double b = modOp_(1, a * d);
            double c = modOp_(2, 0.0);
            double e = modOp_(3, c * d);
            out = (b + e) * 0.5;
        } break;
        case 6: {                                   // FB(1) + 2->3->4
            double a = modOp_(0, 0.0);
            double b = modOp_(1, 0.0);
            double c = modOp_(2, b * d);
            double e = modOp_(3, c * d);
            out = (a + e) * 0.5;
        } break;
        default: {                                  // 7: FB(1) + 2->3 + 4
            double a = modOp_(0, 0.0);
            double b = modOp_(1, 0.0);
            double c = modOp_(2, b * d);
            double e = modOp_(3, 0.0);
            out = (a + c + e) * 0.4;
        } break;
    }

    // all operators finished -> voice is done.
    bool anyLive = false;
    int nops = fourOp_ ? 4 : 2;
    for (int i = 0; i < nops; ++i) if (!ops_[i].finished()) { anyLive = true; break; }
    if (!anyLive) active_ = false;

    return float(out * velocity_ * volume_ * 0.7);
}

} // namespace fxchain::smaf
