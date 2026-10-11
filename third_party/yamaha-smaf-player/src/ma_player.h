// SPDX-License-Identifier: Apache-2.0
// Modified by MMF Pocket contributors in 2026. See docs/audio-fidelity.md.
#pragma once
//
// ma_player.h -- the smaf playback engine. turns a parsed SmafFile into sound.
// ----------------------------------------------------------------------------
// the flow, once the container is parsed:
//   1. collect voice patches from every setup/inline exclusive into a table
//      keyed by (bank, program[, drum-note]).
//   2. decode each score track's event stream (handyphone OR mobile, huffman-
//      inflated if needed) into ONE flat, time-sorted event list, absolute in
//      output samples. octave-shift + running velocity are resolved here.
//   3. render: walk a sample clock, fire due events (note-on allocates an fm
//      voice from a pool, note-off releases it, cc/pc/bend update channel
//      state), sum every live voice to stereo with per-channel pan/volume.
//
// timing is purely metric (smaf has no tempo events): event_ms = sum(duration
// * timebase_ms), note length = gate * timebase_ms. so 1 tick maps cleanly to
// milliseconds and then to samples at the output rate. the fm core runs at the
// output rate directly, so there is no resampling on the synth path.

#include "smaf_file.h"
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"

#include <cstdint>
#include <vector>
#include <array>
#include <map>

namespace fxchain::smaf {

class MaPlayer {
public:
    // build the engine from a parsed file. returns false only if there is
    // nothing playable at all. sampleRate is the output/device rate.
    bool init(const SmafFile& file, uint32_t sampleRate,
              bool suppressHpsSlapbackDuplicates = false);

    // render interleaved stereo float. returns frames produced (< frames at
    // end of song). safe to call after the song ends (returns 0).
    int render(float* interleaved, int frames);

    void  seekToStart();
    // Upper bound including release safety allowance, not exact WAV duration.
    uint64_t totalSamples() const { return endSample_; }
    // Estimated event/ATR-source endpoint; slot stealing may finish earlier.
    uint64_t scoreEndSamples() const { return scoreEndSample_; }
    // Coarse note-on activity used only for the UI mascot: 0 slow, 1 normal, 2 fast.
    int motionPace() const { return motionPace_; }
    bool  finished() const { return ended_; }
    int   channelHint() const { return 2; }

    struct Diagnostics {
        uint64_t fmNotes = 0, pcmNotes = 0, fallbackNotes = 0;
        uint64_t stolenFm = 0, stolenPcm = 0;
        uint64_t stolenHeldFm = 0, stolenHeldPcm = 0;
        uint64_t suppressedHpsDuplicates = 0;
        uint64_t audioNotes = 0, missingAudio = 0, stolenAudio = 0;
        uint64_t limitedFrames = 0; // output-quality diagnostic, not note fidelity
        bool tailLimitReached = false;
    };
    const Diagnostics& diagnostics() const { return diagnostics_; }
    // Whole decoded score/setup coverage. Immutable until the next init();
    // neither render() nor seekToStart() mutates it. No private metadata stored.
    const ScoreAudit& scoreAudit() const { return scoreAudit_; }

private:
    // ── the decoded event stream ────────────────────────────────────────────
    struct Ev {
        uint64_t sample;
        enum T : uint8_t { NoteOn, Ma7NoteOn, NoteOff, Program, BankMsb, BankLsb,
                           Volume, Pan, Expression, PitchBend, Modulation, PitchBendRange,
                           WaveOn, WaveOff, AtrEnd, AtrNop, AtrVolume, AtrExpression, AtrPan, ScoreNop,
                           RpnMsb, RpnLsb, RpnData, NrpnSelect, Hold, VoiceActivation, WaveActivation,
                           AllNotesOff, AllSoundOff, ResetControllers, Ma7AudioPan } type;
        uint16_t ch;
        int16_t  a;   // note / pc / cc-value / bend / wave number
        int16_t  b;   // velocity
        uint64_t noteId = 0; // paired gate identity, not pitch or pool slot
        int sourceTrack = -1; // MA7 note's originating MTR, not channel state
        bool handyPhoneNote = false; // HPS FM retrigger policy belongs to a note
    };
    std::vector<Ev> events_;
    size_t nextEvent_ = 0;
    size_t nonAtrGateEventEnd_ = 0; // only generated ATR gates may follow this index
    uint64_t atrPlaybackEndSample_ = 0; // bounded by both gate and source length
    uint64_t nextNoteId_ = 1;

    // ── voice table + per-channel runtime state ─────────────────────────────
    std::vector<ParsedVoice> voiceTable_;
    struct Chan {
        int bankMsb = 0, bankLsb = 0, program = 0;
        float volume = 100.0f / 127.0f;
        float expression = 1.0f;
        bool squaredControllers = false; // MA7 SMAF 40-log volume / expression curve
        bool modernTimbre = false; // Mobile/MA7 trial synthesis, not HandyPhone
        // Mobile KeyControl OFF uses DrumKey; unspecified/ON use score pitch.
        // MA7/HandyPhone retain the prior policy pending separate calibration.
        bool fixedFmDrumPitch = true;
        float pan = 0.0f;             // -1..+1
        bool hold = false;            // Mobile Hold1, default off; Audio/ATR excluded
        double vibratoScale = 0.0;    // MA7 CC1 initial0 OFF; legacy paths retain1
        double bend = 0.0;            // in semitones
        int bendRange = 2, rawBend = 0; // MA7 CC15 range, default +/-2 semitones
        int rpnMsb = 127, rpnLsb = 127, bendCents = 0; // Mobile RPN0/0, null by default
        int   octShift = 0;           // handyphone octave-shift state (semitones)
        bool  drum = false;           // bank bit7 (hps) or rhythm channel
        bool  rhythm = false;         // MTR channel-status type 3 (rhythm channel)
        bool  monophonic = false;      // HandyPhone parts own one retriggered voice slot
        int   streamTrack = -1;        // source MTR, preserved across seek
        int   lastVel = 64;
    };
    std::array<Chan, 128> chans_{};
    struct AtrChan {
        float volume = 1.0f, expression = 1.0f;
        // Compatibility default until an explicit, valid ATR Pan command.
        float panLeft = 1.0f, panRight = 1.0f;
    };
    // Independent of score channels120..123. Initial unity gain is retained;
    // only the documented authored Volume/Expression curve is applied.
    std::map<int, std::array<AtrChan, 4>> atrChans_;

    // ── fm voice pool ────────────────────────────────────────────────────────
    static constexpr int kPoolSize = 32;
    std::array<FmVoice, kPoolSize> pool_{};
    std::array<float, kPoolSize> poolPan_{};      // live channel pan unless explicitly locked
    std::array<float, kPoolSize> poolPanLeft_{}, poolPanRight_{};
    std::array<bool, kPoolSize> poolPanLocked_{};
    std::array<uint64_t, kPoolSize> poolNoteId_{};
    std::array<double,kPoolSize> poolBaseFreq_{}; // unbent pitch incl. original ROM substitute tuning
    unsigned fmSteal_ = 0, pcmSteal_ = 0;
    int activeVoices_() const;
    bool hasOutputVoices_() const;

    // ── pcm (sampled) voice pool + the decoded wave bank ──────────────────────
    // drum kits and sampled instruments play these instead of fm. the bank holds
    // each Mwa/Awa wave decoded once (yamaha adpcm) keyed by its wave number.
    struct PcmSample {
        std::vector<int16_t> pcm;
        int fs = 8000;
        // Only the newly supported MA-7 signed8 path opts in. Preserve older
        // codecs' pool scheduling and exact output, including silent tails.
        bool retireSilentLoop = false;
    };
    struct PcmVoice {
        const int16_t* pcm = nullptr; size_t len = 0;
        double pos = 0.0, rate = 1.0, baseRate = 1.0;
        double lfoPhase = 0.0, lfoInc = 0.0, vibDepth = 0.0, baseVibDepth = 0.0;
        float amDepth = 0.0f;
        size_t loopStart = 0, loopEnd = 0; bool loop = false;
        bool silentLoop = false; // after loopStart all future source samples are exactly zero
        bool qualityInterpolation = false;
        size_t loopTravel = 0; // virtual history distance removed by loop wraps
        FmEnvelope env;
        float pan = 0.0f, vel = 1.0f, gain = 1.0f, noteVelocity = 1.0f;
        float panLeft = 1.0f, panRight = 1.0f;
        bool panLocked = false, released = false;
        float recentLevel = 0.0f;
        int channel = -1, keyNote = -1;
        uint64_t noteId = 0;
        uint64_t atrGateId = 0; // independent of score NoteOff/AllNotesOff identity
        int atrTrack = -1;
        int audioSourceTrack = -1; // MA7 recorded Audio only, never WT/ATR/legacy
        bool active = false;
        bool streamAudio = false;
        uint64_t startedAt = 0;
        int stopFade = -1, stopFadeTotal = 1; // Audio gate: bounded 2ms anti-click fade
        float tick();
    };
    std::vector<PcmSample> waveBank_;
    std::map<int, std::vector<PcmSample>> scoreWaveBanks_; // Mwa IDs local to each MTR
    std::map<int, std::vector<PcmSample>> audioWaveBanks_; // Awa IDs local to each ATR
    struct AudioPanState {
        uint8_t mode = 1; //1 channel pan,0 per-wave pan,2 unity L/R (Panoff)
        uint8_t value = 64;
    };
    std::map<int, std::array<AudioPanState,32>> ma7AudioPan_; // Mwa source namespace
    void applyAudioPan_(PcmVoice& voice, const AudioPanState& state);
    std::array<PcmVoice, 2> audioPool_{}; // Audio hardware has two slots, separate from WT
    // Voice-wave RAM is a different namespace from Mwa/Awa stream samples.
    std::array<PcmSample, 128> voiceWaveBank_{};
    // Initial setup RAM stays immutable during playback. Inline decoded versions
    // are immutable too: new notes select the current version, while existing
    // notes keep their allocated sample memory (not a mid-note hardware RAM model).
    std::vector<PcmSample> voiceWaveVersions_;
    std::array<int16_t, 128> activeVoiceWaveVersion_{}; // -1 selects setup RAM
    size_t voiceWaveVersionBytes_ = 0;
    static constexpr int kPcmPool = 32;
    int pcmLimit_ = 16; // MA-7 has 32 PCM voices; retain the legacy 16-slot policy
    std::array<PcmVoice, kPcmPool> pcmPool_{};
    void buildWaveBank_(const SmafFile& file);
    void addVoiceWave_(const uint8_t* p, size_t n);
    bool decodeVoiceWave_(const uint8_t* p, size_t n, PcmSample& out,
                          size_t maxDecodedBytes);
    void addTimedWave_(const uint8_t* p, size_t n, uint64_t sample);
    int allocatePcmSlot_();
    void startAudio_(int ch, int note, float velocity, uint64_t noteId, bool ma7,
                     bool modern, int sourceTrack);
    const ParsedVoice* resolveVoice_(int ch, int note) const;
    bool startPcm_(int ch, int rawNote, int soundingNote, const ParsedVoice& v,
                   const Chan& c, float noteVelocity, uint64_t noteId);

    // ── clock ────────────────────────────────────────────────────────────────
    uint32_t rate_ = 48000;
    uint64_t cursor_ = 0;
    uint64_t endSample_ = 0;
    uint64_t scoreEndSample_ = 0;
    int motionPace_ = 1;
    bool ended_ = false;
    Diagnostics diagnostics_{};
    ScoreAudit scoreAudit_{};
    int auditFormat_ = -1, auditTrack_ = -1;
    void audit_(ScoreAudit::Domain domain, ScoreAudit::Status status, int opcode,
                int value = 0, uint64_t sample = 0, int channel = -1);
    uint32_t readScoreLength_(const uint8_t*& p, const uint8_t* end,
                              bool hps, uint64_t sample, ScoreAudit::Domain domain);
    bool scoreBytes_(const uint8_t* p, const uint8_t* end, size_t count,
                     int opcode, uint64_t sample, int channel = -1);
    void routeController_(uint64_t sample, int channel, int cc, int value, bool ma7);
    void auditVoice_(const ParsedVoice& voice, uint64_t sample);
    void auditExclusive_(const uint8_t* p, size_t n, uint64_t sample);

    // gentle output low-pass (2-pole), the MA "analog lite" character: warms the
    // FM and tames the top-octave aliasing of the naive waveforms.
    float lp1L_ = 0, lp1R_ = 0, lp2L_ = 0, lp2R_ = 0, lpCoef_ = 0.0f;
    // transparent output peak limiter (instant attack, ~150 ms release).
    float limEnv_ = 0.0f, limRelease_ = 0.9999f;

    // ── build helpers ────────────────────────────────────────────────────────
    void collectVoices_(const SmafFile& file);
    void addExclusivesFrom_(const std::vector<uint8_t>& setup, bool mobile);
    void addTimedVoice_(const uint8_t* p, size_t n, uint64_t sample);
    bool routeMa7Exclusive_(const uint8_t* p, size_t n, uint64_t sample, bool setup = false);
    void decodeTrack_(const TrackChunk& t, int baseChannel);
    // atrWaveMode: audio tracks use their own six-bit wave IDs and local bank;
    // wave messages become WaveOn triggers, never pitched score notes.
    void decodeHandyPhone_(const uint8_t* p, size_t n, int base, double tbDms, double tbGms,
                           bool atrWaveMode = false, int atrTrack = -1);
    void decodeMobile_(const uint8_t* p, size_t n, int base, double tbDms, double tbGms);
    // Format 3 / MA-7 SEQU: mobile-style events extended from 16 to 32
    // channels by using status bit 7 as the channel-bank selector.
    void decodeMa7_(const uint8_t* p, size_t n, int base, double tbDms, double tbGms);
    void suppressHpsSlapbackDuplicates_();
    void calculateMotionPace_();

    // resolve a channel's current patch (voice table hit or gm fallback).
    const FmVoicePatch& patchFor_(int ch, int note, bool& isPcmOut) const;
    void  fireEvent_(const Ev& e);
    double noteToFreq_(int midiNote) const;

    // static default patch storage for gm fallbacks that live for the render.
    FmVoicePatch scratchPatch_;
};

// huffman (okumura tree) inflate for mobile format-0x01 sequence bodies.
// returns the decoded bytes, or empty on malformed input.
std::vector<uint8_t> smafHuffmanInflate(const uint8_t* p, size_t n);

} // namespace fxchain::smaf
