// SPDX-License-Identifier: Apache-2.0
// Modified by MMF Pocket contributors in 2026. See docs/audio-fidelity.md.
#pragma once
//
// ma_fm_core.h -- the fm voice engine of the yamaha MA-series ringtone chips.
// ----------------------------------------------------------------------------
// the MA-3/MA-5 sound source is a close cousin of the classic OPL/OPN yamaha
// fm line: sine-family operators, an exponential envelope generator, 2-op and
// 4-op voices with a small set of connection algorithms. this file is the pure
// dsp: phase generator + envelope generator + operator + voice, all fixed-point
// where the chip was and float where we can afford to be. no ringtone-format
// knowledge lives here, so it unit-tests as "give me a patch + a note, hand me
// samples".
//
// ROM-FREE BY DESIGN. the chips also had a small gm sample rom for the "voices"
// a ringtone could reference by program number. that rom is not public and we
// do not ship it. instead a compact built-in fm patch set approximates the gm
// map, so a file that asks for "program 0" gets a plausible piano-ish fm voice
// rather than silence. it will not be bit-identical to a 2003 handset, and that
// is fine and honest: recognised, and it plays.
//
// references read as documentation (no code copied): the public OPL/OPLL
// register descriptions, the yamaha ymf825 (sd-1) application notes for a
// modern same-family part, and umjammer/vavi-sound for behaviour.

#include <cstdint>
#include <array>
#include <vector>

namespace fxchain::smaf {

// Low-frequency sine lookup, bounded linear-interpolation error <3e-7.
// Input is a finite normalized phase [0,1); used by the new PCM LFO path.
double pcmModulationSine(double phase);

// ── one operator's patch ────────────────────────────────────────────────────
// values are in the chip's own units (0..15 rates, 0..63 tl, ...) so the
// sequencer can drop register bytes straight in. the core converts to internal
// scales at note-on.
struct FmOpPatch {
    uint8_t multi   = 1;   // frequency multiple (0..15; 0 = x0.5)
    uint8_t tl      = 0;   // total level / attenuation (0..63, 0 = loudest)
    uint8_t ar      = 15;  // attack rate  (0..15)
    uint8_t dr      = 0;   // decay rate   (0..15)
    uint8_t sr      = 0;   // sustain rate (0..15)  (aka second decay)
    uint8_t rr      = 7;   // release rate (0..15)
    uint8_t sl      = 0;   // sustain level(0..15)
    uint8_t ksl     = 0;   // key-scale level (0..3)
    uint8_t ksr     = 0;   // key-scale rate  (0..1)
    uint8_t wave    = 0;   // waveform select (0..31; the YMF825/OPL wave set)
    uint8_t dt      = 0;   // detune (0..7)
    uint8_t fb      = 0;   // per-operator self-feedback (0..7)
    bool    am      = false; // amplitude modulation (tremolo) enable
    bool    vib     = false; // vibrato enable
    bool    egType  = true;  // true = sustaining (hold at SL), false = percussive
    // appended after the 15 positional fields so existing brace-inits stay valid.
    uint8_t dvb     = 0;     // vibrato depth (0..3)
    uint8_t dam     = 0;     // tremolo depth (0..3)
    bool    xof     = false; // ignore key-off (drums ring out fully)
    // MA-7's additional least-significant rate bits. The existing nibbles
    // keep the MA-1/2/3/5 scale: MA-7 rate = 2*nibble + selected low bit.
    // Bit0 RR, bit1 SR, bit2 DR, bit3 AR. Legacy/default patches leave zero.
    uint8_t rateLowBits = 0;
    // MA7 fixed-frequency raw transport, retained but not synthesized until
    // the hardware BLOCK/FNUM-to-Hz relationship is established.
    bool fixedFrequency = false;
    uint8_t fixedBlock = 0;
    uint16_t fixedFnum = 0;
    bool holdEnabled = false; // VM35/MA7 SUS: responds to Hold1, not VMA SUS
    // Parsed VM35 synthesis family: multiplier aliases and modern PM scale.
    // Only MA3/MA5 parser branches set this; not MA7/VMA/original fallbacks.
    bool ma35Multipliers = false;
    bool ma7Multipliers = false; // expanded MA7 FM has separate fractional selections
    uint8_t ma7Detune = 0; // all four raw DT bits retained; physical units uncalibrated
};

// ── a voice patch: 2-op or 4-op, one connection algorithm ───────────────────
struct FmVoicePatch {
    bool     fourOp   = false;      // false = 2-op, true = 4-op
    uint8_t  algorithm = 0;         // connection algo (0..1 for 2-op, 0..7 4-op)
    uint8_t  feedback  = 0;         // op1 self-feedback (0..7)
    int      noteShift = 0;         // basic-octave transpose in semitones (BO)
    float    panDefault = 0.0f;     // patch panpot default, -1..+1 (0 = centre)
    uint8_t  lfo = 0;               // voice LFO rate select (0..3)
    std::array<FmOpPatch, 4> ops{}; // op[0..1] used for 2-op, all four for 4-op
    // a simple, safe default patch: single carrier sine, medium decay.
    static FmVoicePatch defaultPatch();
    // the built-in gm-approximation bank, indexed 0..127 by program number.
    static const FmVoicePatch& gmApprox(int program);
    // percussion fallback for rhythm-channel notes with no bound voice: a short
    // fm hit picked by register (low = kick-ish, mid = snare-ish, high = hat-ish)
    // so drums never fall back to a melodic piano.
    static const FmVoicePatch& drumApprox(int note);
    // Original, ROM-free trial voices classified by documented ROM wave ID.
    // Neither the patch parameters nor their spectral balance emulate ROM.
    static const FmVoicePatch* romApprox(int waveId);
    static const FmVoicePatch& gmModernApprox(int program);
    bool modernTimbre = false; // related-chip feedback/tremolo; legacy defaults unchanged
    bool panEnabled = false; // Channel Pan Off: authored pan overrides even at centre
};

// ── envelope generator (adsr, exponential, chip-style discrete rates) ───────
class FmEnvelope {
public:
    // Discrete key-scaling offset added to 4*nibble + 2*MA7-low-bit.
    void configure(const FmOpPatch& p, double sampleRate, int rateOffset = 0);
    void keyOn();
    void keyOff(bool holdOn = false);
    void setHold(bool on);
    bool keyOffDeferred() const { return pendingKeyOff_; }
    float advance();                 // returns linear gain 0..1 for this sample
    bool  isFinished() const { return phase_ == Phase::Idle; }
    // Exact silence until a new configure/keyOn, not a level threshold.
    // The configured step includes MA7 low bits: raw AR0 can still attack.
    bool silentForCurrentNote() const {
        return isFinished() || (atkStep_ == 0.0 && level_ == 0.0);
    }
private:
    friend class FmVoice; // native-clock sine subframe snapshots, no extra EG advance

    enum class Phase { Idle, Attack, Decay, Sustain, Release };
    Phase phase_ = Phase::Idle;
    double level_ = 0.0;             // 0 (silent) .. 1 (peak), linear
    double atkStep_ = 0.0, decStep_ = 0.0, susStep_ = 0.0, relStep_ = 0.0;
    double susLevel_ = 0.0;
    bool   sustaining_ = true;
    bool   xof_ = false;             // ignore key-off (drums ring out fully)
    bool holdEnabled_ = false, pendingKeyOff_ = false;
};

// ── operator: phase generator + waveform + envelope ─────────────────────────
class FmOperator {
public:
    void configure(const FmOpPatch& p, double sampleRate, int rateOffset = 0,
                   bool modernTimbre = false);
    void noteOn(double freqHz);
    void noteOff(bool holdOn = false);
    void setHold(bool on) { env_.setHold(on); }
    bool keyOffDeferred() const { return env_.keyOffDeferred(); }
    bool usesLfo() const { return vibAmt_ != 0.0 || amAmt_ != 0.0f; }
    // modIn is the phase-modulation input in cycles (0 for a pure carrier).
    // lfoSin is the voice LFO value (-1..1); the operator applies its own
    // vibrato/tremolo depth from the patch when VIB/AM are enabled.
    // returns the operator output in roughly [-1, 1] scaled by tl + envelope.
    float tick(double modIn, double lfoSin = 0.0, double vibratoScale = 1.0);
    bool  finished() const { return env_.isFinished(); }
    bool silentForCurrentNote() const { return env_.silentForCurrentNote(); }
    // These unbound user selectors currently synthesize exact zero. No
    // controller changes their binding during an allocated note's lifetime.
    bool hasZeroWaveform() const {
        const auto wave = wave_ & 31;
        return wave == 15 || wave == 23 || wave == 31;
    }
    // Exact zero output throughout the intrinsic LFO cycle at current controls.
    // Pitch/CC1 changes may clear it: use only after future events are exhausted.
    // Keep ticking this operator during the score to preserve phase/envelope.
    bool muted(double vibratoScale = 1.0) const {
        bool pitchMuted = phaseInc_ >= 0.5;
        if (pitchMuted && modernTimbre_ && vibAmt_ != 0.0) {
            const double depth = vibAmt_ * (vibratoScale < 0 ? -vibratoScale : vibratoScale);
            pitchMuted = phaseInc_ * (1.0 - depth) >= 0.5;
        }
        return pitchMuted || hasZeroWaveform()
            || silentForCurrentNote();
    }
    void  setBaseFreq(double f) { freqHz_ = f; recalc_(); }
    void setBandlimited(bool enabled); // FmVoice restricts this to unmodulated final carriers.
private:
    friend class FmVoice; // owning voice reads the post-tick oscillator state

    void recalc_();
    FmOpPatch  patch_{};
    FmEnvelope env_{};
    double sampleRate_ = 48000.0;
    double freqHz_     = 440.0;
    double phase_      = 0.0;   // 0..1
    double phaseInc_   = 0.0;
    double tlGain_     = 1.0;   // linear gain from total-level
    double kslGain_    = 1.0;   // key-scale level attenuation (freq-dependent)
    double vibAmt_     = 0.0;   // vibrato depth as a phase-inc factor
    float  amAmt_      = 0.0f;  // tremolo depth as a linear gain span
    bool   cachedMute_ = false; // nominal Nyquist or an exactly stopped attack
    uint8_t wave_      = 0;
    bool modernTimbre_ = false;
    bool blepEnabled_ = false;
    double blepGain_ = 1.0, blepInverse_ = 1.0;
    static float waveform_(uint8_t wave, double phase01);
};

enum class FmSubframeMath : uint8_t { Exact, Hermite };

// ── a playing voice ─────────────────────────────────────────────────────────
class FmVoice {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; }
    void noteOn(const FmVoicePatch& patch, double freqHz, float velocity);
    void noteOff(bool holdOn = false);
    void setHold(bool on);
    void setVibratoScale(double scale) { vibratoScale_ = scale; }
    void setPitch(double freqHz);         // pitch bend / portamento
    void setVolume(float v) { volume_ = v; }   // channel volume 0..1
    float tick();                          // one mono sample, ~[-1,1]
    // Additive source API: sine graphs0/5/7 without feedback, plus graphs3/4/6
    // reduced to one PM pair by an exactly stopped attack, and weak VM35
    // nested graph6 (both modulator TL>=24), or coherent MA7 nested graph6
    // without fixed-frequency/detune. Both nested classes always select16x.
    // Advances tick() ONCE and emits2/4/8/16/32 samples ending at that result.
    // EG/LFO/allocation timing stay native; intermediate gains are held per frame.
    // False leaves state/output untouched (bad shape/count/math/null output).
    // Caller owns FIR filtering, common delay, source routing and EOF policy.
    bool supportsSineSubframes() const;
    // Explicit fixed-density graph6 experiment for coherent modern VM35/MA7
    // sine/no-feedback patches without fixed frequency/detune.16/32/64/128.
    // Strong modulation is allowed only through this separate opt-in API.
    bool supportsNestedSubframes() const;
    bool tickNestedSubframes(float* output,int factor,FmSubframeMath math=FmSubframeMath::Exact);

    // Conservative8/16 density hint for supported PM graphs. Within a note it
    // can only rise; a new note resets it. Does not advance synthesis clocks.
    int sineDensityHint();
    // Separate opt-in bounded VM35 nested class;0 retains the r91 policy.
    // No vibrato;16/32/64/128 only rise within a note, reset at noteOn.
    int nestedSineDensityHint();
    bool tickSineSubframes(float* output, int factor,
                          FmSubframeMath math = FmSubframeMath::Exact);
    bool  active() const { return active_; }
    // Exact zero gain or provably silent carriers, never a level threshold.
    // Used only at score EOF, after future controllers/notes are exhausted;
    // this query does not retire slots or change voice-allocation policy.
    bool  muted() const;
    // All output envelopes/waveforms are permanently zero until another noteOn.
    // Unlike muted(), this excludes reversible channel gain/pitch suppression.
    bool silentForCurrentNote() const;
    bool  released() const { return released_; }
    float recentLevel() const { return recentLevel_; }
    int   note() const { return note_; }
    int   channel = -1;                    // owning smaf channel (for stealing)
    int   note_   = -1;
private:
    FmVoicePatch patch_{};
    std::array<FmOperator, 4> ops_{};
    double sampleRate_ = 48000.0;
    float  velocity_ = 1.0f;
    float  volume_   = 1.0f;
    bool   active_   = false;
    bool   released_ = false;
    bool keyOffRequested_ = false;
    void updateReleaseState_();
    float  recentLevel_ = 0.0f;
    bool   fourOp_   = false;
    uint8_t algo_    = 0;
    uint8_t feedback_= 0;
    uint8_t opFb_[4] = {0,0,0,0};          // per-operator feedback amount (0..7)
    double  fbMem_[4][2] = {{0,0},{0,0},{0,0},{0,0}};  // per-op feedback history
    double  lfoPhase_ = 0.0, lfoInc_ = 0.0;            // one LFO per voice
    double  lfoSin_   = 0.0;                           // this sample's LFO value
    double vibratoScale_ = 1.0; // channel modulation affects pitch only, not AM
    double modDepth_ = 0.42; // selected once per note; compatibility default
    int densityFactor_ = 8;
    int nestedDensityFactor_ = -1; // -1 unchecked,0 outside the opt-in class
    double nestedDensityPhase_ = -1;
    double densityPhase_ = -1, densityVibrato_ = 0;
    // one modulator step: run op[i] with its own feedback + an external phase-mod
    // input (in cycles). returns its output. keeps the FM index musical.
    float modOp_(int i, double extModCycles);
    template<bool Fast> bool tickSineSubframesImpl_(float* output, int factor);
    bool weakNestedSine_() const;
    template<bool Fast> bool tickNestedSubframesImpl_(float* output,int factor);
};

} // namespace fxchain::smaf
