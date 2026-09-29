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

#include <cstdint>
#include <vector>
#include <array>

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
        bool tailLimitReached = false;
    };
    const Diagnostics& diagnostics() const { return diagnostics_; }

private:
    // ── the decoded event stream ────────────────────────────────────────────
    struct Ev {
        uint64_t sample;
        enum T : uint8_t { NoteOn, NoteOff, Program, BankMsb, BankLsb,
                           Volume, Pan, Expression, PitchBend, Modulation,
                           WaveOn } type;   // WaveOn: ATR trigger, a = wave number
        uint16_t ch;
        int16_t  a;   // note / pc / cc-value / bend / wave number
        int16_t  b;   // velocity
        uint64_t noteId = 0; // paired gate identity, not pitch or pool slot
    };
    std::vector<Ev> events_;
    size_t nextEvent_ = 0;
    uint64_t nextNoteId_ = 1;

    // ── voice table + per-channel runtime state ─────────────────────────────
    std::vector<ParsedVoice> voiceTable_;
    struct Chan {
        int bankMsb = 0, bankLsb = 0, program = 0;
        float volume = 100.0f / 127.0f;
        float expression = 1.0f;
        float pan = 0.0f;             // -1..+1
        double bend = 0.0;            // in semitones
        int   octShift = 0;           // handyphone octave-shift state (semitones)
        bool  drum = false;           // bank bit7 (hps) or rhythm channel
        bool  rhythm = false;         // MTR channel-status type 3 (rhythm channel)
        bool  monophonic = false;      // HandyPhone parts own one retriggered voice slot
        int   lastVel = 64;
    };
    std::array<Chan, 128> chans_{};

    // ── fm voice pool ────────────────────────────────────────────────────────
    static constexpr int kPoolSize = 32;
    std::array<FmVoice, kPoolSize> pool_{};
    std::array<float, kPoolSize> poolPan_{};      // captured at note-on
    std::array<uint64_t, kPoolSize> poolNoteId_{};
    unsigned fmSteal_ = 0, pcmSteal_ = 0;
    int activeVoices_() const;

    // ── pcm (sampled) voice pool + the decoded wave bank ──────────────────────
    // drum kits and sampled instruments play these instead of fm. the bank holds
    // each Mwa/Awa wave decoded once (yamaha adpcm) keyed by its wave number.
    struct PcmSample { std::vector<int16_t> pcm; int fs = 8000; };
    struct PcmVoice {
        const int16_t* pcm = nullptr; size_t len = 0;
        double pos = 0.0, rate = 1.0, baseRate = 1.0;
        size_t loopStart = 0, loopEnd = 0; bool loop = false;
        FmEnvelope env;
        float pan = 0.0f, vel = 1.0f, gain = 1.0f, noteVelocity = 1.0f;
        bool panLocked = false, released = false;
        float recentLevel = 0.0f;
        int channel = -1, keyNote = -1;
        uint64_t noteId = 0;
        bool active = false;
        float tick();
    };
    std::vector<PcmSample> waveBank_;
    // Voice-wave RAM is a different namespace from Mwa/Awa stream samples.
    std::array<PcmSample, 128> voiceWaveBank_{};
    static constexpr int kPcmPool = 16;
    std::array<PcmVoice, kPcmPool> pcmPool_{};
    void buildWaveBank_(const SmafFile& file);
    void addVoiceWave_(const uint8_t* p, size_t n);
    int allocatePcmSlot_();
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

    // gentle output low-pass (2-pole), the MA "analog lite" character: warms the
    // FM and tames the top-octave aliasing of the naive waveforms.
    float lp1L_ = 0, lp1R_ = 0, lp2L_ = 0, lp2R_ = 0, lpCoef_ = 0.0f;
    // transparent output peak limiter (instant attack, ~150 ms release).
    float limEnv_ = 0.0f, limRelease_ = 0.9999f;

    // ── build helpers ────────────────────────────────────────────────────────
    void collectVoices_(const SmafFile& file);
    void addExclusivesFrom_(const std::vector<uint8_t>& setup, bool mobile);
    void decodeTrack_(const TrackChunk& t, int baseChannel);
    // atrWaveMode: the audio-track sequence reuses the handyphone grammar with
    // the note nibble carrying a WAVE NUMBER; notes become WaveOn triggers.
    void decodeHandyPhone_(const uint8_t* p, size_t n, int base, double tbDms, double tbGms,
                           bool atrWaveMode = false);
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
