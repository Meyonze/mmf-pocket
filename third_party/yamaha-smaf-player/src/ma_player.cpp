// SPDX-License-Identifier: Apache-2.0
// Modified by MMF Pocket contributors in 2026. See NOTICE and
// THIRD_PARTY_NOTICES.md at the repository root for details.
//
// ma_player.cpp -- decode the score, schedule it, synth it.
// ----------------------------------------------------------------------------
// two event grammars share one output path: handyphone standard (the MA-1/2
// 2-byte world) and mobile standard (the MA-3/5 midi-flavoured world, optionally
// huffman-packed). both decode into the same flat event list, which the render
// loop plays back against a pool of fm voices. the whole thing is deterministic
// and allocation-free once init() returns, so it is safe to run on a decode
// thread and trivial to smoke-test.

#include "ma_player.h"
#include "yamaha_adpcm.h"   // unused until m1 (embedded pcm voices); included so
                            // the codec always compiles alongside the engine.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace fxchain::smaf {

namespace {
constexpr double kSafetySeconds = 600.0;   // hard cap so a broken loop can't hang
constexpr size_t kMaxEvents     = 400000;  // total decoded-event ceiling (anti-DoS)
constexpr size_t kMaxCustomVoices = 2048;  // cap malformed exclusive-message floods

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

// handyphone variable length (form a): 1 or 2 bytes.
// b0 < 0x80 -> b0 ; else -> ((b0 & 0x7f) + 1) << 7 | b1.
inline uint32_t readVarHps(const uint8_t*& p, const uint8_t* end) {
    if (p >= end) return 0;
    uint8_t b0 = *p++;
    if (b0 < 0x80) return b0;
    if (p >= end) return b0 & 0x7f;
    uint8_t b1 = *p++;
    return (uint32_t((b0 & 0x7f) + 1) << 7) | b1;
}

// mobile / midi variable length quantity: 7 bits per byte, msb = continue.
inline uint32_t readVlq(const uint8_t*& p, const uint8_t* end) {
    uint32_t v = 0; int guard = 0;
    while (p < end && guard++ < 5) {
        uint8_t b = *p++;
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) break;
    }
    return v;
}
} // namespace

// ── huffman (okumura tree) inflate ──────────────────────────────────────────
std::vector<uint8_t> smafHuffmanInflate(const uint8_t* p, size_t n) {
    std::vector<uint8_t> out;
    if (n < 4) return out;
    uint32_t decoded = be32(p);
    // ringtone sequences are small; an 8 MB decoded ceiling blocks the
    // "6-byte body claims 64 MB output" amplification a fuzzer loves.
    if (decoded == 0 || decoded > (8u << 20)) return out;
    const uint8_t* bits = p + 4;
    size_t bn = n - 4;
    size_t bitPos = 0;
    auto getBit = [&]() -> int {
        if ((bitPos >> 3) >= bn) return -1;
        int b = (bits[bitPos >> 3] >> (7 - (bitPos & 7))) & 1;
        ++bitPos; return b;
    };
    auto getByte = [&]() -> int {
        int v = 0;
        for (int i = 0; i < 8; ++i) { int b = getBit(); if (b < 0) return -1; v = (v << 1) | b; }
        return v;
    };
    // node ids: 0..255 leaves (=byte value), 256..510 internal.
    std::array<int, 511> left{}, right{};
    int internalNext = 256;
    bool fail = false;
    // build the tree recursively with a depth guard. a legal okumura tree can
    // be up to 255 levels deep (a fully degenerate chain of the 255 internal
    // nodes), so the guard sits just above that; anything deeper is malformed.
    std::function<int(int)> readTree = [&](int depth) -> int {
        if (fail || depth > 256) { fail = true; return 0; }
        int bit = getBit();
        if (bit < 0) { fail = true; return 0; }
        if (bit == 1) {
            if (internalNext > 510) { fail = true; return 0; }
            int idx = internalNext++;
            left[idx]  = readTree(depth + 1);
            right[idx] = readTree(depth + 1);
            return idx;
        }
        int val = getByte();
        if (val < 0) { fail = true; return 0; }
        return val;   // leaf 0..255
    };
    int root = readTree(0);
    if (fail) return {};
    out.reserve(decoded);
    for (uint32_t i = 0; i < decoded; ++i) {
        int node = root;
        int guard = 0;
        while (node >= 256 && guard++ < 256) {
            int bit = getBit();
            if (bit < 0) return out;   // ran out; return what we have
            node = bit ? right[node] : left[node];
        }
        out.push_back(uint8_t(node & 0xff));
    }
    return out;
}

// ── build ────────────────────────────────────────────────────────────────────
bool MaPlayer::init(const SmafFile& file, uint32_t sampleRate,
                    bool suppressHpsSlapbackDuplicates) {
    rate_ = sampleRate ? sampleRate : 48000;
    events_.clear(); voiceTable_.clear();
    nextEvent_ = 0; cursor_ = 0; endSample_ = 0;
    nextNoteId_ = 1;
    fmSteal_ = pcmSteal_ = 0;
    scoreEndSample_ = 0; motionPace_ = 1; ended_ = false; diagnostics_ = {};
    for (auto& wave : voiceWaveBank_) wave = PcmSample{};
    for (auto& c : chans_) c = Chan{};
    for (auto& v : pool_) { v = FmVoice{}; v.setSampleRate(double(rate_)); }
    for (auto& pv : pcmPool_) pv.active = false;
    // Reject at engine level too. Unknown grammars must never silently produce
    // plausible-looking wrong events; Format 3 is the validated MA-7 SEQU path.
    for (const auto& t : file.tracks)
        if (!t.isAudioTrack && (t.formatType < 0 || t.formatType > 3)) return false;
    poolNoteId_.fill(0);
    // ~10 kHz analog-lite output filter (two 1-pole stages).
    lpCoef_ = float(1.0 - std::exp(-2.0 * 3.14159265358979 * 10000.0 / double(rate_)));
    lp1L_ = lp1R_ = lp2L_ = lp2R_ = 0.0f;
    limEnv_ = 0.0f;
    limRelease_ = float(std::exp(-1.0 / (0.15 * double(rate_))));   // ~150 ms

    collectVoices_(file);
    buildWaveBank_(file);
    for (auto& pv : pcmPool_) pv.active = false;

    auto play = file.playableScoreTracks();
    if (play.empty()) return false;

    for (size_t idx : play) {
        const TrackChunk& t = file.tracks[idx];
        // handyphone = 4 channels/track, mobile = 16. global channel base keeps
        // multi-track files from colliding.
        int base = (t.formatType == 0x00) ? t.trackNumber * 4
                 : (t.formatType == 0x03) ? 0
                 : t.trackNumber * 16;
        if (base < 0 || base > 112) base = 0;
        decodeTrack_(t, base);
    }

    // audio (ATR) tracks: the wave-trigger sequence that plays the sampled
    // drums/voices alongside the score. it reuses the handyphone grammar with
    // the note nibble as the wave number. an ATR often declares timebase 0
    // (1 ms) while it is authored against the score's tick, so inherit the
    // first score track's timebase (same song, same clock).
    {
        double scoreTb = 0.0;
        if (!play.empty()) scoreTb = TrackChunk::timeBaseMs(file.tracks[play[0]].durationTimeBase);
        for (const TrackChunk& t : file.tracks) {
            if (!t.isAudioTrack || t.sequenceData.empty() || t.waves.empty()) continue;
            double tb = TrackChunk::timeBaseMs(t.durationTimeBase);
            if (t.durationTimeBase == 0 && scoreTb > 0.0) tb = scoreTb;
            decodeHandyPhone_(t.sequenceData.data(), t.sequenceData.size(),
                              /*base=*/120, tb, tb, /*atrWaveMode=*/true);
        }
    }

    if (events_.empty()) return false;

    // setup-ramp hoist: many files stagger each channel's first BankMsb/BankLsb/
    // Program over the first couple of ticks WHILE the first notes already play
    // (the real chip rewrites the channel registers, so a sounding note takes the
    // patch over mid-note; we snapshot at note-on). hoist each channel's FIRST
    // bank/program assignment to t=0 when it lands inside the setup window, so
    // the opening notes bind the intended voice instead of the gm fallback.
    {
        const uint64_t setupWin = uint64_t(rate_) / 20;     // 50 ms
        bool seen[128][3] = {};                             // per ch: msb/lsb/pc
        for (Ev& e : events_) {
            int k = (e.type == Ev::BankMsb) ? 0
                  : (e.type == Ev::BankLsb) ? 1
                  : (e.type == Ev::Program) ? 2 : -1;
            if (k < 0) continue;
            bool& s = seen[e.ch & 127][k];
            if (s) continue;
            s = true;
            if (e.sample <= setupWin) e.sample = 0;
        }
    }
    // controllers first at equal timestamps, so a same-tick bank/program lands
    // before the note-on that depends on it.
    std::stable_sort(events_.begin(), events_.end(),
                     [](const Ev& a, const Ev& b) {
                         if (a.sample != b.sample) return a.sample < b.sample;
                         auto isNote = [](const Ev& e) {
                             return e.type == Ev::NoteOn || e.type == Ev::NoteOff;
                         };
                         return !isNote(a) && isNote(b);
                     });

    if (suppressHpsSlapbackDuplicates) suppressHpsSlapbackDuplicates_();
    calculateMotionPace_();

    // Let real envelopes finish, rather than cutting every song at +1 s.
    // Ten seconds is only a safety ceiling for held/very slow envelopes;
    // render() normally ends earlier and fades the safety boundary if reached.
    scoreEndSample_ = events_.back().sample;
    endSample_ = scoreEndSample_ + uint64_t(rate_) * 10;
    uint64_t cap = uint64_t(kSafetySeconds * rate_);
    if (endSample_ > cap) endSample_ = cap;
    return true;
}

void MaPlayer::calculateMotionPace_() {
    // SMAF timing is metric and carries no BPM event. Use the lower-middle
    // note-on spacing per channel as a stable activity estimate. Combining all
    // channels makes ordinary polyphony look artificially frantic. This value
    // never affects playback; it only selects the mascot's motion vocabulary.
    std::array<uint64_t, 128> lastOnset{};
    std::array<bool, 128> hasOnset{};
    std::vector<uint32_t> gapsMs;
    for (const Ev& event : events_) {
        if (event.type != Ev::NoteOn && event.type != Ev::WaveOn) continue;
        const size_t channel = size_t(event.ch & 127);
        if (!hasOnset[channel]) {
            hasOnset[channel] = true;
            lastOnset[channel] = event.sample;
            continue;
        }
        uint64_t frames = event.sample - lastOnset[channel];
        lastOnset[channel] = event.sample;
        uint64_t milliseconds = frames * 1000 / std::max<uint32_t>(1, rate_);
        if (milliseconds >= 90 && milliseconds <= 2000)
            gapsMs.push_back(static_cast<uint32_t>(milliseconds));
    }
    if (gapsMs.size() < 4) {
        motionPace_ = 1;
        return;
    }

    std::sort(gapsMs.begin(), gapsMs.end());
    const size_t representativeIndex = (gapsMs.size() - 1) * 45 / 100;
    const uint32_t representativeGap = gapsMs[representativeIndex];
    motionPace_ = representativeGap <= 185 ? 2 : representativeGap >= 620 ? 0 : 1;
}

void MaPlayer::suppressHpsSlapbackDuplicates_() {
    // Compatibility correction for one verified HandyPhone score whose string
    // accompaniment is duplicated on separate parts 40/44 ms apart. On a
    // modern full-range output that authored handset trick is heard as a strong
    // slapback echo. The caller enables this only for the exact source-file
    // fingerprint; ordinary SMAF scores retain their authored layering.
    struct RecentNote {
        uint64_t sample = 0;
        uint16_t channel = 0;
        bool valid = false;
    };
    std::array<int, 128> program{};
    std::array<RecentNote, 128> recent{};
    std::vector<uint64_t> suppressedIds;
    std::vector<Ev> filtered;
    filtered.reserve(events_.size());

    const uint64_t minDelay = uint64_t(rate_) * 35 / 1000;
    const uint64_t maxDelay = uint64_t(rate_) * 50 / 1000;
    for (const Ev& event : events_) {
        const unsigned channel = event.ch & 127;
        if (event.type == Ev::Program) program[channel] = event.a & 0x7f;

        if (event.type == Ev::NoteOff &&
            std::find(suppressedIds.begin(), suppressedIds.end(), event.noteId) !=
                suppressedIds.end()) {
            continue;
        }

        if (event.type == Ev::NoteOn && program[channel] == 81 &&
            event.a >= 0 && event.a < int(recent.size())) {
            RecentNote& previous = recent[size_t(event.a)];
            const uint64_t delay = previous.valid && event.sample >= previous.sample
                ? event.sample - previous.sample : 0;
            if (previous.valid && previous.channel != event.ch &&
                delay >= minDelay && delay <= maxDelay) {
                suppressedIds.push_back(event.noteId);
                ++diagnostics_.suppressedHpsDuplicates;
                continue;
            }
            previous = {event.sample, event.ch, true};
        }
        filtered.push_back(event);
    }
    events_.swap(filtered);
}

// decode every Mwa/Awa wave once into the bank, keyed by wave number. the
// yamaha adpcm payloads are LOW-nibble-first (the vavi-sound decoder's
// little-endian bit order), which is the difference between clean audio and hash.
void MaPlayer::buildWaveBank_(const SmafFile& file) {
    waveBank_.clear();
    for (const TrackChunk& t : file.tracks) {
        for (const WaveData& w : t.waves) {
            if (w.number < 0 || w.data.empty()) continue;
            if (size_t(w.number) >= waveBank_.size()) waveBank_.resize(w.number + 1);
            PcmSample& ps = waveBank_[w.number];
            ps.fs = w.samplingRate ? int(w.samplingRate) : 8000;
            if (w.adpcm) {
                YamahaAdpcm dec;
                ps.pcm = dec.decodeAll(w.data.data(), w.data.size(), /*highNibbleFirst=*/false);
            } else if (w.bitsPerSample == 16) {
                size_t nS = w.data.size() / 2;
                ps.pcm.resize(nS);
                for (size_t i = 0; i < nS; ++i)
                    ps.pcm[i] = int16_t((w.data[2*i] << 8) | w.data[2*i+1]);
            } else {  // 8-bit
                ps.pcm.resize(w.data.size());
                for (size_t i = 0; i < w.data.size(); ++i)
                    ps.pcm[i] = int16_t((int(w.data[i]) - 128) << 8);
            }
        }
    }
}

void MaPlayer::collectVoices_(const SmafFile& file) {
    for (const TrackChunk& t : file.tracks) {
        bool mobile = (t.formatType != 0x00);
        if (!t.setupData.empty()) addExclusivesFrom_(t.setupData, mobile);
    }
}

void MaPlayer::addVoiceWave_(const uint8_t* p, size_t n) {
    // Observed MA-3 voice-wave command: 43 79 06 7f 03 ID 00 packed ADPCM.
    // Only this transport/codec combination is enabled. Unknown variants are
    // not guessed. 16-bit LP/EP addressing bounds a wave to 65536 samples.
    if (!p || n < 9 || p[0] != 0x43 || p[1] != 0x79 || p[2] != 0x06 ||
        p[3] != 0x7f || p[4] != 3 || p[5] >= 128 || p[6] != 0) return;
    auto bytes = unpackMa3Bytes(p + 7, n - 7, 32768);
    if (bytes.empty()) return;
    YamahaAdpcm decoder;
    voiceWaveBank_[p[5]].pcm = decoder.decodeAll(bytes.data(), bytes.size(), false);
    // Playback rate belongs to the selecting PCM patch's Fs, not this block.
}

// walk a setup-data blob and pull every yamaha voice exclusive out of it.
// hps setup: {FF F0 len payload}* ; mobile setup: {F0 len payload}* ; we accept
// an optional leading FF either way and both length encodings.
void MaPlayer::addExclusivesFrom_(const std::vector<uint8_t>& buf, bool mobile) {
    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();
    while (p < end) {
        if (*p == 0xFF) { ++p; if (p >= end) break; }
        if (*p != 0xF0) { ++p; continue; }
        ++p;                              // consume F0
        if (p >= end) break;
        // length: try vlq for mobile, single byte for hps, but fall back so a
        // len >= 0x80 hps value is read as the 2-byte form.
        uint32_t len;
        if (mobile) len = readVlq(p, end);
        else { len = readVarHps(p, end); }
        if (len == 0 || len > size_t(end - p)) break;   // count, never a past-end ptr
        // payload includes the trailing F7; parse from the maker id up to it.
        size_t plen = len;
        if (plen && p[plen - 1] == 0xF7) --plen;
        addVoiceWave_(p, plen);
        ParsedVoice v = parseVoiceExclusive(p, plen);
        if ((v.valid || v.isPcm) && voiceTable_.size() < kMaxCustomVoices)
            voiceTable_.push_back(v);
        p += len;
    }
}

void MaPlayer::decodeTrack_(const TrackChunk& t, int base) {
    double tbD = TrackChunk::timeBaseMs(t.durationTimeBase);
    double tbG = TrackChunk::timeBaseMs(t.gateTimeBase);
    // MTR channel status: per-channel 2-bit type field (0 no-care / 1 melody /
    // 2 no-melody / 3 RHYTHM). handyphone packs 4 nibbles into 2 bytes (high
    // nibble first), mobile carries one byte per channel. a rhythm channel's
    // unmatched notes must fall back to a percussion patch, never to a melodic
    // gm voice (the "drums play piano" class).
    if (t.formatType == 0x00) {
        for (size_t i = 0; i < 4 && i / 2 < t.channelStatus.size(); ++i) {
            chans_[(base + i) & 127].monophonic = true;
            uint8_t nib = (i % 2 == 0) ? (t.channelStatus[i / 2] >> 4)
                                       : (t.channelStatus[i / 2] & 0x0f);
            chans_[(base + i) & 127].rhythm = ((nib & 0x03) == 3);
        }
    } else {
        for (size_t i = 0; i < t.channelStatus.size() && i < 32; ++i)
            chans_[(base + i) & 127].rhythm = ((t.channelStatus[i] & 0x03) == 3);
    }
    if (t.formatType == 0x00) {
        decodeHandyPhone_(t.sequenceData.data(), t.sequenceData.size(), base, tbD, tbG);
    } else if (t.formatType == 0x01) {
        // huffman-compressed mobile body.
        auto inflated = smafHuffmanInflate(t.sequenceData.data(), t.sequenceData.size());
        if (!inflated.empty())
            decodeMobile_(inflated.data(), inflated.size(), base, tbD, tbG);
    } else if (t.formatType == 0x02) {
        decodeMobile_(t.sequenceData.data(), t.sequenceData.size(), base, tbD, tbG);
    } else if (t.formatType == 0x03) {
        decodeMa7_(t.sequenceData.data(), t.sequenceData.size(), base, tbD, tbG);
    }
}

// ── handyphone standard event stream ────────────────────────────────────────
void MaPlayer::decodeHandyPhone_(const uint8_t* p, size_t n, int base, double tbDms, double tbGms,
                                 bool atrWaveMode) {
    const uint8_t* end = p + n;
    double curMs = 0.0;
    auto msToSample = [&](double ms) { return uint64_t(ms * rate_ / 1000.0); };
    int guard = 0;
    while (p < end && guard++ < 2000000 && events_.size() < kMaxEvents) {
        uint32_t dur = readVarHps(p, end);
        curMs += dur * tbDms;
        if (p >= end) break;
        uint8_t e1 = *p++;
        if (e1 == 0xFF) {
            if (p >= end) break;
            uint8_t e2 = *p++;
            if (e2 == 0x00) continue;                 // NOP
            if (e2 == 0xF0) {                         // exclusive (voice)
                uint32_t len = readVarHps(p, end);
                if (len > size_t(end - p)) break;
                size_t plen = len; if (plen && p[plen-1]==0xF7) --plen;
                ParsedVoice v = parseVoiceExclusive(p, plen);
                if ((v.valid || v.isPcm) && voiceTable_.size() < kMaxCustomVoices)
                    voiceTable_.push_back(v);
                p += len;
                continue;
            }
            // 0x2F/0x51/0x58 style meta: read a length byte + skip.
            if (p >= end) break;
            uint8_t l = *p++;
            p += (size_t(end - p) >= l) ? l : size_t(end - p);
            continue;
        }
        if (e1 == 0x00) {
            if (p >= end) break;
            uint8_t e2 = *p++;
            if (e2 == 0x00) {                         // extended / EOS
                if (p >= end) break;
                uint8_t e3 = *p++;
                if (e3 == 0x00) break;                // End Of Sequence
                continue;
            }
            int ch = base + ((e2 >> 6) & 3);
            int cls = (e2 >> 4) & 3;
            int data = e2 & 0x0f;
            uint64_t s = msToSample(curMs);
            if (cls == 3) {                           // long event: value byte
                if (p >= end) break;
                uint8_t v = *p++;
                switch (data) {
                    case 0x0: events_.push_back({s, Ev::Program, uint16_t(ch), int16_t(v & 0x7f), 0}); break;
                    case 0x1:                          // bank select (raw; bit7 = drum)
                        events_.push_back({s, Ev::BankLsb, uint16_t(ch), int16_t(v), 0});
                        break;
                    case 0x2: {                        // octave shift
                        int semis = 0;
                        if (v >= 0x81 && v <= 0x84) semis = -12 * (v - 0x80);
                        else if (v >= 0x01 && v <= 0x04) semis = 12 * v;
                        events_.push_back({s, Ev::Modulation, uint16_t(ch), int16_t(1000 + semis), 1 /*octave marker*/});
                    } break;
                    case 0x4: events_.push_back({s, Ev::PitchBend, uint16_t(ch), int16_t((int(v) - 64) * 128), 0}); break;
                    case 0x7: events_.push_back({s, Ev::Volume, uint16_t(ch), int16_t(v & 0x7f), 0}); break;
                    case 0xA: events_.push_back({s, Ev::Pan, uint16_t(ch), int16_t(v & 0x7f), 0}); break;
                    case 0xB: events_.push_back({s, Ev::Expression, uint16_t(ch), int16_t(v & 0x7f), 0}); break;
                    default: break;                    // consume + ignore
                }
            } else if (cls == 0) {
                // expression SHORT: value = data==1 ? 0 : data*8+15 (spec table).
                int v = (data <= 1) ? (data == 0 ? 0 : 0) : (data * 8 + 15);
                if (v > 0x7f) v = 0x7f;
                events_.push_back({s, Ev::Expression, uint16_t(ch), int16_t(v), 0});
            } else if (cls == 1) {
                // pitch-bend SHORT: msb = data*8, centre at data==8.
                int v14 = (data * 8) << 7;
                events_.push_back({s, Ev::PitchBend, uint16_t(ch), int16_t(v14 - 8192), 0});
            } else {   // cls == 2
                // modulation SHORT: spec table, index 1..14.
                static const uint8_t modTab[16] = { 0,0x00,0x08,0x10,0x18,0x20,0x28,0x30,
                                                    0x38,0x40,0x48,0x50,0x60,0x70,0x7f,0x7f };
                events_.push_back({s, Ev::Modulation, uint16_t(ch), int16_t(modTab[data & 15]), 0});
            }
            continue;
        }
        // note: e1 = cc oo nnnn
        int ch = base + ((e1 >> 6) & 3);
        int octave = (e1 >> 4) & 3;
        int note = e1 & 0x0f;
        uint32_t gate = readVarHps(p, end);
        if (gate == 0) continue;                       // spec: skip zero-gate
        if (atrWaveMode) {
            // ATR sequence: the note nibble IS the wave number; the wave plays
            // one-shot at native rate, so only the trigger matters.
            events_.push_back({msToSample(curMs), Ev::WaveOn, uint16_t(ch), int16_t(note), 127});
            continue;
        }
        // (oct 0, note 0) = MIDI 36, per vavi-sound MidiContext.retrievePitch
        // ("pitch += 36"); files then place themselves with the score's own
        // octave-shift event (+-1..4 octaves). a +60 base derived from go-smaf's
        // Note.Freq (whose Name()/Freq() disagree by 2 octaves) played every
        // HandyPhone tune two octaves sharp, into chirp territory with the
        // shifts on top. ear-verified on the corpus: +36.
        int midi = note + octave * 12 + 36;
        uint64_t on = msToSample(curMs);
        uint64_t off = on + msToSample(gate * tbGms);
        const uint64_t id = nextNoteId_++;
        events_.push_back({on,  Ev::NoteOn,  uint16_t(ch), int16_t(midi), 127, id});
        events_.push_back({off, Ev::NoteOff, uint16_t(ch), int16_t(midi), 0, id});
    }
}

// ── mobile standard event stream ────────────────────────────────────────────
void MaPlayer::decodeMobile_(const uint8_t* p, size_t n, int base, double tbDms, double tbGms) {
    const uint8_t* end = p + n;
    double curMs = 0.0;
    auto msToSample = [&](double ms) { return uint64_t(ms * rate_ / 1000.0); };
    // per-channel running velocity: a 0x9n note SETS it, later 0x8n notes REUSE
    // it (spec behaviour; initial value 64).
    uint8_t runVel[16];
    for (auto& v : runVel) v = 64;
    int guard = 0;
    while (p < end && guard++ < 4000000 && events_.size() < kMaxEvents) {
        uint32_t dur = readVlq(p, end);
        curMs += dur * tbDms;
        if (p >= end) break;
        uint8_t s = *p++;
        if (s < 0x80) continue;                        // stray data byte
        int ch = base + (s & 0x0f);
        uint64_t at = msToSample(curMs);
        switch (s & 0xf0) {
            case 0x80: case 0x90: {                    // note off-vel / on-vel
                if (p >= end) break;
                int note = *p++;
                int vel;
                if ((s & 0xf0) == 0x90) {
                    if (p >= end) break;
                    vel = *p++;
                    runVel[s & 0x0f] = uint8_t(vel & 0x7f);   // remember
                } else {
                    vel = runVel[s & 0x0f];                    // reuse remembered
                }
                uint32_t gate = readVlq(p, end);
                if (gate == 0) break;
                uint64_t off = at + msToSample(gate * tbGms);
                const uint64_t id = nextNoteId_++;
                events_.push_back({at,  Ev::NoteOn,  uint16_t(ch), int16_t(note & 0x7f), int16_t(vel & 0x7f), id});
                events_.push_back({off, Ev::NoteOff, uint16_t(ch), int16_t(note & 0x7f), 0, id});
            } break;
            case 0xA0: p += (end - p >= 2 ? 2 : (end - p)); break;   // reserved
            case 0xB0: {                                // control change
                if (size_t(end - p) < 2) { p = end; break; }
                int cc = *p++, val = *p++;
                switch (cc) {
                    case 0x00: events_.push_back({at, Ev::BankMsb, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x20: events_.push_back({at, Ev::BankLsb, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x07: events_.push_back({at, Ev::Volume,  uint16_t(ch), int16_t(val), 0}); break;
                    case 0x0A: events_.push_back({at, Ev::Pan,     uint16_t(ch), int16_t(val), 0}); break;
                    case 0x0B: events_.push_back({at, Ev::Expression, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x01: events_.push_back({at, Ev::Modulation, uint16_t(ch), int16_t(val), 0}); break;
                    default: break;
                }
            } break;
            case 0xC0: { if (p >= end) break; int pc = *p++;
                events_.push_back({at, Ev::Program, uint16_t(ch), int16_t(pc & 0x7f), 0}); } break;
            case 0xD0: if (p < end) ++p; break;          // reserved
            case 0xE0: {                                 // pitch bend lsb,msb
                if (size_t(end - p) < 2) { p = end; break; }
                int lsb = *p++, msb = *p++;
                int v14 = ((msb & 0x7f) << 7) | (lsb & 0x7f);
                events_.push_back({at, Ev::PitchBend, uint16_t(ch), int16_t(v14 - 8192), 0});
            } break;
            case 0xF0: {
                if (s == 0xF0) {                         // exclusive
                    uint32_t len = readVlq(p, end);
                    if (len > size_t(end - p)) { p = end; break; }
                    size_t plen = len; if (plen && p[plen-1]==0xF7) --plen;
                    addVoiceWave_(p, plen);
                    ParsedVoice v = parseVoiceExclusive(p, plen);
                    if ((v.valid || v.isPcm) && voiceTable_.size() < kMaxCustomVoices)
                        voiceTable_.push_back(v);
                    p += len;
                } else if (s == 0xFF) {                  // meta
                    if (p >= end) break;
                    uint8_t m = *p++;
                    if (m == 0x00) break;                // NOP
                    if (m == 0x2F) { p = end; }          // EOS
                    else if (p < end) {
                        uint8_t l = *p++;
                        p += (end - p >= l ? l : (end - p));   // clamp, never past end
                    }
                }
            } break;
            default: break;
        }
    }
}

// ── voice resolution ────────────────────────────────────────────────────────
const FmVoicePatch& MaPlayer::patchFor_(int ch, int note, bool& isPcmOut) const {
    isPcmOut = false;
    const Chan& c = chans_[ch & 127];
    // drum voices bind on (bank, pc, drumNote==note); melody on (bank, pc).
    const ParsedVoice* melodyHit = nullptr;
    for (const ParsedVoice& v : voiceTable_) {
        if (v.key.bankMSB == c.bankMsb && v.key.bankLSB == c.bankLsb && v.key.pc == c.program) {
            if (v.key.drumNote != 0) { if (v.key.drumNote == note) { if (v.isPcm){isPcmOut=true;} return v.patch; } }
            else if (note <= v.keyHigh && (!melodyHit || v.keyHigh < melodyHit->keyHigh)) {
                melodyHit = &v;
            }
        }
    }
    if (melodyHit) { if (melodyHit->isPcm) isPcmOut = true; return melodyHit->patch; }
    return FmVoicePatch::gmApprox(c.program);
}

// the matched voice for a channel + note, or nullptr when only the gm fallback
// applies. drum voices bind on (bank, pc, drumNote==note); melody on (bank, pc).
const ParsedVoice* MaPlayer::resolveVoice_(int ch, int note) const {
    const Chan& c = chans_[ch & 127];
    const ParsedVoice* melodyHit = nullptr;
    for (const ParsedVoice& v : voiceTable_) {
        if (v.key.bankMSB == c.bankMsb && v.key.bankLSB == c.bankLsb && v.key.pc == c.program) {
            if (v.key.drumNote != 0) { if (v.key.drumNote == note) return &v; }
            else if (note <= v.keyHigh && (!melodyHit || v.keyHigh < melodyHit->keyHigh))
                melodyHit = &v;
        }
    }
    return melodyHit;
}

// start a sampled (pcm) note on a free pcm slot. drums play at native rate,
// melodic pcm is pitched from a fixed root note.
int MaPlayer::allocatePcmSlot_() {
    for (int i=0;i<kPcmPool;++i) if (!pcmPool_[i].active) return i;
    int slot=-1;
    for (int i=0;i<kPcmPool;++i)
        if (pcmPool_[i].released && (slot<0 || pcmPool_[i].recentLevel<pcmPool_[slot].recentLevel)) slot=i;
    ++diagnostics_.stolenPcm;
    if (slot<0) { slot=pcmSteal_++%kPcmPool; ++diagnostics_.stolenHeldPcm; }
    return slot;
}

// ── MA-7 SEQU event stream ─────────────────────────────────────────────────
// Format 3 keeps the Mobile event classes, but exposes 32 channels. Status bit
// 7 selects channels 16..31 and bits 3..0 select the channel inside that bank:
//   0x00/0x80 note (running velocity), 0x10/0x90 note+velocity,
//   0x30/0xB0 control, 0x40/0xC0 program, 0x60/0xE0 pitch bend.
// This grammar consumes every event in the local 61-file MA-7 corpus exactly;
// reserved classes are skipped with their specified Mobile payload widths.
void MaPlayer::decodeMa7_(const uint8_t* p, size_t n, int base,
                          double tbDms, double tbGms) {
    const uint8_t* end = p + n;
    double curMs = 0.0;
    auto msToSample = [&](double ms) { return uint64_t(ms * rate_ / 1000.0); };
    uint8_t runVel[32];
    for (auto& v : runVel) v = 64;

    int guard = 0;
    while (p < end && guard++ < 4000000 && events_.size() < kMaxEvents) {
        uint32_t dur = readVlq(p, end);
        curMs += dur * tbDms;
        if (p >= end) break;
        uint8_t status = *p++;
        uint64_t at = msToSample(curMs);

        if (status == 0xF0) {
            uint32_t len = readVlq(p, end);
            if (len > size_t(end - p)) break;
            size_t plen = len;
            if (plen && p[plen - 1] == 0xF7) --plen;
            ParsedVoice v = parseVoiceExclusive(p, plen);
            if ((v.valid || v.isPcm) && voiceTable_.size() < kMaxCustomVoices)
                voiceTable_.push_back(v);
            p += len;
            continue;
        }
        if (status == 0xFF) {
            if (p >= end) break;
            uint8_t meta = *p++;
            if (meta == 0x00) continue;
            if (meta == 0x2F) {
                if (p < end) ++p; // zero-length byte
                break;
            }
            if (p >= end) break;
            uint32_t len = readVlq(p, end);
            p += std::min<size_t>(len, size_t(end - p));
            continue;
        }

        const int local = (status & 0x0F) + ((status & 0x80) ? 16 : 0);
        const int ch = base + local;
        const int eventClass = status & 0x70;
        switch (eventClass) {
            case 0x00:
            case 0x10: {
                if (p >= end) { p = end; break; }
                int note = *p++ & 0x7F;
                int vel = runVel[local];
                if (eventClass == 0x10) {
                    if (p >= end) { p = end; break; }
                    vel = *p++ & 0x7F;
                    runVel[local] = uint8_t(vel);
                }
                uint32_t gate = readVlq(p, end);
                if (gate == 0) break;
                uint64_t off = at + msToSample(gate * tbGms);
                const uint64_t id = nextNoteId_++;
                events_.push_back({at, Ev::NoteOn, uint16_t(ch), int16_t(note), int16_t(vel), id});
                events_.push_back({off, Ev::NoteOff, uint16_t(ch), int16_t(note), 0, id});
            } break;
            case 0x20: // reserved, two data bytes
                p += std::min<size_t>(2, size_t(end - p));
                break;
            case 0x30: {
                if (size_t(end - p) < 2) { p = end; break; }
                int cc = *p++, val = *p++;
                switch (cc) {
                    case 0x00: events_.push_back({at, Ev::BankMsb, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x20: events_.push_back({at, Ev::BankLsb, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x07: events_.push_back({at, Ev::Volume, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x0A: events_.push_back({at, Ev::Pan, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x0B: events_.push_back({at, Ev::Expression, uint16_t(ch), int16_t(val), 0}); break;
                    case 0x01: events_.push_back({at, Ev::Modulation, uint16_t(ch), int16_t(val), 0}); break;
                    default: break;
                }
            } break;
            case 0x40:
                if (p < end)
                    events_.push_back({at, Ev::Program, uint16_t(ch), int16_t(*p++ & 0x7F), 0});
                break;
            case 0x50: // reserved, one data byte
                if (p < end) ++p;
                break;
            case 0x60: {
                if (size_t(end - p) < 2) { p = end; break; }
                int lsb = *p++, msb = *p++;
                int v14 = ((msb & 0x7F) << 7) | (lsb & 0x7F);
                events_.push_back({at, Ev::PitchBend, uint16_t(ch), int16_t(v14 - 8192), 0});
            } break;
            default:
                p = end; // 0x70 is reserved and has no validated width.
                break;
        }
    }
}

bool MaPlayer::startPcm_(int ch, int rawNote, int soundingNote, const ParsedVoice& v,
                         const Chan& c, float noteVelocity, uint64_t noteId) {
    // Bind ONLY the referenced wave. An unrelated short sample is not a
    // substitute for a missing instrument and can sound like random noise.
    const PcmSample* ws = nullptr;
    int wid = v.pcm.waveId;
    if (v.pcm.rom) return false; // ROM IDs must never alias a local RAM sample
    if (wid >= 0 && wid < 128 && !voiceWaveBank_[wid].pcm.empty())
        ws = &voiceWaveBank_[wid];
    else if (wid >= 0 && size_t(wid) < waveBank_.size() && !waveBank_[wid].pcm.empty())
        ws = &waveBank_[wid];
    if (!ws) return false;
    if (v.pcm.endPt == 0) return true; // EP=0 intentionally does not pronounce

    int slot = allocatePcmSlot_();
    PcmVoice& pv = pcmPool_[slot];
    pv.pcm = ws->pcm.data(); pv.len = ws->pcm.size();
    bool isDrum = v.key.drumNote != 0;
    double base = double(v.pcm.fs) / double(rate_);
    double semis = isDrum ? 0.0 : double(soundingNote - 60);   // root C4
    pv.baseRate = base * std::pow(2.0, semis / 12.0);
    pv.rate = pv.baseRate * std::pow(2.0, c.bend / 12.0);
    pv.pos = 0.0;
    // LP/EP are sample indices into the decoded stream; clamp to the wave.
    pv.loopEnd   = (v.pcm.endPt > 0 && size_t(v.pcm.endPt) <= pv.len) ? size_t(v.pcm.endPt) : pv.len;
    pv.loopStart = (size_t(v.pcm.loopPt) < pv.loopEnd) ? size_t(v.pcm.loopPt) : 0;
    pv.loop = v.pcm.loop && v.pcm.loopPt >= 0 && size_t(v.pcm.loopPt) < pv.loopEnd;
    pv.env.configure(v.pcm.env, double(rate_)); pv.env.keyOn();
    pv.gain = float(std::pow(10.0, -0.75 * double(v.pcm.env.tl) / 20.0));
    // Match FM voices: authored note velocity is part of the musical balance,
    // not just channel volume. Ignoring it made sampled drums and instruments
    // dominate quieter FM parts.
    pv.vel = c.volume * c.expression * noteVelocity;
    pv.noteVelocity = noteVelocity;
    pv.pan = v.pcm.panEnabled ? v.pcm.pan : c.pan;
    pv.panLocked = v.pcm.panEnabled;
    pv.channel = ch; pv.keyNote = rawNote; pv.active = true;
    pv.noteId = noteId;
    pv.released = false; pv.recentLevel = 0;
    ++diagnostics_.pcmNotes;
    return true;
}

float MaPlayer::PcmVoice::tick() {
    if (!active || !pcm || len == 0) { active = false; return 0.0f; }
    size_t end = (loopEnd > 0 && loopEnd <= len) ? loopEnd : len;
    if (pos >= double(end)) {
        if (loop && loopEnd > loopStart)
            pos = double(loopStart) + std::fmod(pos - double(end), double(loopEnd - loopStart));
        else { active = false; return 0.0f; }
    }
    size_t i = size_t(pos);
    if (i >= len) { active = false; return 0.0f; }
    float frac = float(pos - double(i));
    float s0 = pcm[i] * (1.0f / 32768.0f);
    size_t next = i + 1 < end ? i + 1 : (loop ? loopStart : i);
    float s1 = pcm[next] * (1.0f / 32768.0f);
    float s = s0 + (s1 - s0) * frac;
    pos += rate;
    float e = env.advance();
    if (env.isFinished()) active = false;
    float result = s * e * gain * vel;
    recentLevel += (std::fabs(result) - recentLevel) * 0.01f;
    return result;
}

double MaPlayer::noteToFreq_(int midiNote) const {
    return 440.0 * std::pow(2.0, (double(midiNote) - 69.0) / 12.0);
}

int MaPlayer::activeVoices_() const {
    int n = 0;
    for (const auto& v : pool_) if (v.active()) ++n;
    for (const auto& v : pcmPool_) if (v.active) ++n;
    return n;
}

void MaPlayer::fireEvent_(const Ev& e) {
    Chan& c = chans_[e.ch & 127];
    switch (e.type) {
        case Ev::Program:   c.program = e.a; break;
        case Ev::BankMsb:   c.bankMsb = e.a; break;
        case Ev::BankLsb:
            c.bankLsb = e.a & 0x7f;              // 7-bit bank
            c.drum    = (e.a & 0x80) != 0;       // bit7 = drum bank
            break;
        case Ev::Volume: {
            c.volume = (e.a & 0x7f) / 127.0f;
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                    voice.vel = voice.noteVelocity * c.volume * c.expression;
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch))
                    pool_[i].setVolume(c.volume * c.expression);
        } break;
        case Ev::Expression: {
            c.expression = (e.a & 0x7f) / 127.0f;
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                    voice.vel = voice.noteVelocity * c.volume * c.expression;
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch))
                    pool_[i].setVolume(c.volume * c.expression);
        } break;
        case Ev::Pan:
            c.pan = ((e.a & 0x7f) - 64) / 64.0f;
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0 && !voice.panLocked)
                    voice.pan = c.pan;
            break;
        case Ev::Modulation:
            if (e.b == 1) c.octShift = e.a - 1000;    // octave-shift marker
            break;
        case Ev::PitchBend: {
            c.bend = double(e.a) / 8192.0 * 2.0;       // +-2 semitones default range
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                    voice.rate = voice.baseRate * std::pow(2.0, c.bend / 12.0);
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch))
                    pool_[i].setPitch(noteToFreq_(pool_[i].note_) * std::pow(2.0, c.bend / 12.0));
        } break;
        case Ev::NoteOff: {
            // Every scheduled gate belongs to one note instance. Same-pitch
            // notes may overlap, finish out of order, or reuse a stolen slot.
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && poolNoteId_[i] == e.noteId) {
                    pool_[i].noteOff();
                    break;
                }
            for (int i = 0; i < kPcmPool; ++i)          // release a held pcm note
                if (pcmPool_[i].active && pcmPool_[i].noteId == e.noteId) {
                    pcmPool_[i].env.keyOff();
                    pcmPool_[i].released = true;
                }
        } break;
        case Ev::WaveOn: {
            // ATR wave trigger: play wave e.a one-shot at its native rate, no
            // envelope shaping (the sample IS the sound, it ends on its own).
            int wid = e.a;
            if (wid < 0 || size_t(wid) >= waveBank_.size() || waveBank_[wid].pcm.empty()) break;
            const PcmSample& ws = waveBank_[wid];
            int slot = allocatePcmSlot_();
            PcmVoice& pv = pcmPool_[slot];
            pv.pcm = ws.pcm.data(); pv.len = ws.pcm.size();
            pv.rate = double(ws.fs) / double(rate_);
            pv.pos = 0.0; pv.loopStart = 0; pv.loopEnd = pv.len; pv.loop = false;
            FmOpPatch flat{};                      // full-open sustaining envelope
            flat.ar = 15; flat.sl = 0; flat.sr = 0; flat.egType = true;
            pv.env.configure(flat, double(rate_)); pv.env.keyOn();
            pv.gain = 1.0f; pv.vel = 1.0f; pv.pan = 0.0f;
            pv.channel = int(e.ch); pv.keyNote = -1; pv.active = true;
            pv.noteId = 0; // ATR one-shots have no scheduled key-off
            pv.released = false; pv.recentLevel = 0;
            ++diagnostics_.pcmNotes;
        } break;
        case Ev::NoteOn: {
            int soundingNote = e.a + c.octShift;
            if (e.b <= 0) break; // explicit/running zero velocity is silent
            float vel01 = e.b / 127.0f;
            float noteVelocity = vel01 * vel01;
            const ParsedVoice* rv = resolveVoice_(e.ch, e.a);
            if (rv && rv->isPcm) {                      // sampled (drum / pcm) voice
                if (rv->valid && startPcm_(e.ch, e.a, soundingNote, *rv, c, noteVelocity, e.noteId)) {
                    break;
                }
                // Some files select a handset-ROM PCM voice but do not carry
                // the referenced waveform. The proprietary ROM is deliberately
                // not bundled, so use the built-in FM approximation instead of
                // turning every note into silence. Do NOT transfer PCM TL onto
                // an independently voiced FM patch: its gain/envelope and the
                // ROM's native waveform level have not been calibrated. The
                // beta.6 compensation made some fallback layers ~21 dB quieter.
                rv = nullptr;
            }
            ++diagnostics_.fmNotes;
            if (!rv) ++diagnostics_.fallbackNotes;
            // a rhythm-channel note with no bound voice gets a percussion hit,
            // never a melodic gm patch (the "drums play piano" class).
            const FmVoicePatch& patch = rv ? rv->patch
                : (c.rhythm || c.drum) ? FmVoicePatch::drumApprox(e.a)
                                       : FmVoicePatch::gmApprox(c.program);
            int slot = -1;
            // HandyPhone score parts are fixed monophonic hardware parts. A
            // new note retriggers that part's slot; keeping its released voice
            // alongside the replacement turns SUS tails into an audible echo.
            if (c.monophonic) {
                for (int i = 0; i < kPoolSize; ++i) {
                    if (pool_[i].active() && pool_[i].channel == int(e.ch)) {
                        slot = i;
                        break;
                    }
                }
            }
            if (slot < 0) {
                for (int i = 0; i < kPoolSize; ++i)
                    if (!pool_[i].active()) { slot = i; break; }
            }
            if (slot < 0) {
                // Preserve held melody notes when release tails fill the pool.
                // Prefer the quietest released voice; fully held pools use
                // deterministic per-player round-robin, reset on seek/init.
                for (int i = 0; i < kPoolSize; ++i)
                    if (pool_[i].released() &&
                        (slot < 0 || pool_[i].recentLevel() < pool_[slot].recentLevel())) slot = i;
                if (slot < 0) { slot = fmSteal_++ % kPoolSize; ++diagnostics_.stolenHeldFm; }
                ++diagnostics_.stolenFm;
            }
            int midi = soundingNote + patch.noteShift;
            double freq = noteToFreq_(midi) * std::pow(2.0, c.bend / 12.0);
            pool_[slot].channel = e.ch;
            pool_[slot].note_   = midi;                 // sounding pitch (for bend)
            poolNoteId_[slot] = e.noteId;
            pool_[slot].setVolume(c.volume * c.expression);
            // gm-style squared velocity curve: linear velocity made every mid-
            // velocity note nearly full-scale and the mix bus pump the limiter.
            pool_[slot].noteOn(patch, freq, noteVelocity);
            poolPan_[slot] = (c.pan != 0.0f) ? c.pan : patch.panDefault;
        } break;
    }
}

// ── render ───────────────────────────────────────────────────────────────────
int MaPlayer::render(float* out, int frames) {
    // A malformed or pathological envelope must not keep a conversion alive
    // after the song safety limit. Previously the inner loop broke here but a
    // following render() call produced one more frame again, so a caller could
    // spend hours draining an effectively endless release tail.
    const uint64_t absoluteEnd = endSample_;
    if (ended_ || cursor_ >= absoluteEnd) { ended_ = true; return 0; }

    int produced = 0;
    while (produced < frames) {
        // fire all events due at or before the current sample.
        while (nextEvent_ < events_.size() && events_[nextEvent_].sample <= cursor_) {
            fireEvent_(events_[nextEvent_]);
            ++nextEvent_;
        }
        if (nextEvent_ >= events_.size() && activeVoices_() == 0 &&
            std::max({std::fabs(lp1L_), std::fabs(lp1R_),
                      std::fabs(lp2L_), std::fabs(lp2R_)}) < 1e-7f) {
            ended_ = true;
            break; // envelopes AND filter history have drained
        }

        float l = 0.0f, r = 0.0f;
        // 0.32 mix headroom: a dense tune runs a dozen voices at once; at 0.5
        // the bus sat inside the soft knee permanently and everything sounded
        // overdriven. with this headroom the knee is a rare safety, not an
        // always-on compressor.
        constexpr float kMixGain = 0.32f;
        for (int i = 0; i < kPoolSize; ++i) {
            if (!pool_[i].active()) continue;
            float s = pool_[i].tick();
            float pan = poolPan_[i];
            l += s * kMixGain * (1.0f - pan); r += s * kMixGain * (1.0f + pan);
        }
        for (int i = 0; i < kPcmPool; ++i) {           // sampled drum/pcm voices
            if (!pcmPool_[i].active) continue;
            float s = pcmPool_[i].tick();
            float pan = pcmPool_[i].pan;
            l += s * kMixGain * (1.0f - pan); r += s * kMixGain * (1.0f + pan);
        }
        // 2-pole one-pole-cascade low-pass (the analog output filter's warmth),
        // then a transparent peak limiter. the old tanh soft-knee WAS the
        // "overdriven" sound: on a hot bus it reshaped the waveform on every
        // sample. a gain-rider only turns the level down on peaks and recovers
        // smoothly, so loud tuttis stay clean.
        lp1L_ += lpCoef_ * (l - lp1L_); lp2L_ += lpCoef_ * (lp1L_ - lp2L_);
        lp1R_ += lpCoef_ * (r - lp1R_); lp2R_ += lpCoef_ * (lp1R_ - lp2R_);
        l = lp2L_; r = lp2R_;
        float peak = std::max(std::fabs(l), std::fabs(r));
        limEnv_ = std::max(peak, limEnv_ * limRelease_);   // instant attack
        if (limEnv_ > 0.92f) { float g = 0.92f / limEnv_; l *= g; r *= g; }
        // Do not introduce a discontinuity when a pathological envelope or
        // the ten-minute file limit reaches the hard safety boundary.
        const uint64_t fadeFrames = std::max<uint64_t>(2, rate_ / 50);
        const uint64_t remaining = absoluteEnd - cursor_;
        if (remaining <= fadeFrames) {
            float gain = float(remaining - 1) / float(fadeFrames - 1);
            l *= gain; r *= gain;
        }
        out[produced * 2]     = l;
        out[produced * 2 + 1] = r;
        ++produced;
        ++cursor_;
        if (cursor_ >= absoluteEnd) {
            diagnostics_.tailLimitReached = activeVoices_() > 0;
            ended_ = true;
            break;
        }
    }
    return produced;
}

void MaPlayer::seekToStart() {
    nextEvent_ = 0; cursor_ = 0;
    ended_ = false; diagnostics_ = {};
    // the rhythm flag is init-time state (MTR channel status), not runtime
    // state; it must survive the channel reset or drums degrade after a seek.
    for (auto& c : chans_) { bool rh = c.rhythm; c = Chan{}; c.rhythm = rh; }
    for (auto& v : pool_) { v = FmVoice{}; v.setSampleRate(double(rate_)); }
    for (auto& pv : pcmPool_) pv.active = false;
    poolNoteId_.fill(0);
    fmSteal_ = pcmSteal_ = 0;
    lp1L_ = lp1R_ = lp2L_ = lp2R_ = 0.0f;
    limEnv_ = 0.0f;
}

} // namespace fxchain::smaf
