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
#include "pcm_interpolation.h"
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
constexpr size_t kMaxVoiceWaveVersions = 512;
constexpr size_t kMaxVoiceWaveVersionBytes = 16u << 20; // extra inline decoded RAM

// MA7 SMAF guideline 8.3.5: channel pan has cosine/sine amplitude gains,
// including the default control64. Cache at allocation/controller time, never
// evaluate trigonometry in the per-sample mixing loop. Authored voice PE uses
// a separate 5-bit register; leave that uncalibrated law and older profiles alone.
void channelPanGains(float pan, bool ma7, float& left, float& right) {
    if (!ma7) { left = 1.0f - pan; right = 1.0f + pan; return; }
    static const std::array<std::array<float,2>,128> table = [] {
        std::array<std::array<float,2>,128> values{};
        for (int i=0;i<128;++i) {
            const double angle=1.5707963267948966*double(i)/127.0;
            values[i]={float(std::cos(angle)),float(std::sin(angle))};
        }
        values[0]={1,0};values[127]={0,1};
        return values;
    }();
    const int value=std::clamp(int(std::lround(double(pan)*64.0+64.0)),0,127);
    left=table[size_t(value)][0];right=table[size_t(value)][1];
}

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

void MaPlayer::audit_(ScoreAudit::Domain domain, ScoreAudit::Status status,
                      int opcode, int value, uint64_t sample, int channel) {
    scoreAudit_.record(domain, status, auditFormat_, auditTrack_, channel,
                       opcode, value, sample);
}

uint32_t MaPlayer::readScoreLength_(const uint8_t*& p, const uint8_t* end,
                                   bool hps, uint64_t sample, ScoreAudit::Domain domain) {
    const uint8_t* start = p;
    // Use the existing reader and preserve its recovery/output exactly.
    uint32_t result = hps ? readVarHps(p, end) : readVlq(p, end);
    bool valid = p > start;
    if (valid && hps) valid = *start < 0x80 || p - start == 2;
    if (valid && !hps) valid = (p[-1] & 0x80) == 0;
    if (!valid) audit_(domain, ScoreAudit::Status::Malformed, 0x100, int(p - start), sample);
    return result;
}

bool MaPlayer::scoreBytes_(const uint8_t* p, const uint8_t* end, size_t count,
                          int opcode, uint64_t sample, int channel) {
    if (size_t(end - p) >= count) return true;
    audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Malformed,
           opcode, int(count), sample, channel);
    return false;
}

void MaPlayer::routeController_(uint64_t at, int ch, int cc, int val, bool ma7) {
    using S = ScoreAudit::Status;
    S status = S::Routed;
    Ev::T type = Ev::Volume;
    int b = 0, routedValue = val;
    bool route = true;
    // One routing table owns both coverage classification and scheduling.
    // Routed means scheduled, NOT that it necessarily changes an active voice.
    switch (cc) {
        case 0: type = Ev::BankMsb; break;
        case 32: type = Ev::BankLsb; break;
        case 7: type = Ev::Volume; break;
        case 10: type = Ev::Pan; break;
        case 11: type = Ev::Expression; break;
        case 1:
            type = Ev::Modulation;
            if (!ma7 || val > 127) status = S::Unapplied;
            break;
        case 15:
            type = Ev::PitchBendRange;
            // Mobile compact sensitivity remains an empirical compatibility
            // interpretation; diagnostics do not upgrade it to a specification.
            status = ma7 ? S::Routed : S::Compatibility;
            if (val > 24) { route = false; status = S::Unapplied; }
            break;
        case 101: case 100:
            type = cc == 101 ? Ev::RpnMsb : Ev::RpnLsb;
            if (ma7) { route = false; status = S::Unsupported; }
            else status = val > 127 ? S::Unapplied :
                val == 0 || val == 127 ? S::Routed : S::Partial;
            break;
        case 64:
            type = Ev::Hold;
            if (ma7) { route = false; status = S::Unsupported; }
            else if (val > 127) { route = false; status = S::Unapplied; }
            break;
        case 120:
            // All Sound Off stops even release/held/XOF voices, without
            // resetting the channel's authored controls or voice selection.
            type = Ev::AllSoundOff;
            if (ma7) { route = false; status = S::Unsupported; }
            else if (val != 0) { route = false; status = S::Unapplied; }
            break;
        case 121:
            // Mobile standard Reset All Controllers. Reset only implemented
            // per-channel controls; do not infer compact MA7 semantics.
            type = Ev::ResetControllers;
            if (ma7) { route = false; status = S::Unsupported; }
            else if (val != 0) { route = false; status = S::Unapplied; }
            break;
        case 123:
            // Mobile Bch 7B 00: standard MIDI All Notes Off. Do not extend
            // this interpretation to the unconfirmed MA7 compact transport.
            type = Ev::AllNotesOff;
            if (ma7) { route = false; status = S::Unsupported; }
            else if (val != 0) { route = false; status = S::Unapplied; }
            break;
        case 6: case 38:
            type = Ev::RpnData; b = cc == 38 ? 1 : 0;
            if (ma7) { route = false; status = S::Unsupported; }
            else status = val <= (b ? 99 : 24) ? S::Conditional : S::Unapplied;
            break;
        case 98: case 99:
            type = Ev::NrpnSelect; routedValue = 0;
            // Only cancels RPN selection; NRPN parameters are not synthesized.
            if (ma7) { route = false; status = S::Unsupported; }
            else status = S::Partial;
            break;
        default: route = false; status = S::Unsupported; break;
    }
    if ((cc == 7 || cc == 10 || cc == 11 || cc == 32) && val > 127)
        status = S::Partial; // legacy masking, not a validated high-bit value
    audit_(ScoreAudit::Domain::Controller, status, cc, val, at, ch);
    if (route) events_.push_back({at, type, uint16_t(ch), int16_t(routedValue), int16_t(b)});
}

void MaPlayer::auditVoice_(const ParsedVoice& v, uint64_t at) {
    using D = ScoreAudit::Domain;
    using S = ScoreAudit::Status;
    // Definition inventory only. An unused patch is NOT an audible defect.
    audit_(D::VoiceDefinition, S::Routed, 0, v.key.pc, at);
    if (v.al.present) {
        // AL is retained but no AL DSP runs, even when all register bytes are
        // zero. Uncalibrated zero values do not prove a hardware bypass. This
        // records definition presence, never a claim of audible use/impact.
        audit_(D::VoiceDefinition, S::Unapplied, 0x101, v.key.pc, at);
    }
    if (v.isPcm && v.pcm.rom) audit_(D::VoiceDefinition, S::Compatibility, 0x102, v.pcm.waveId, at);
    const int count = v.isPcm ? 1 : v.patch.fourOp ? 4 : 2;
    for (int i = 0; i < count; ++i) {
        const auto& op = v.isPcm ? v.pcm.env : v.patch.ops[size_t(i)];
        if (op.fixedFrequency) audit_(D::VoiceDefinition, S::Unapplied, 0x110 + i, v.key.pc, at);
        if (op.ma7Multipliers && op.ma7Detune)
            audit_(D::VoiceDefinition, S::Partial, 0x120 + i, op.ma7Detune, at);
        // MA5 manual4.18.2: WS15/23/31 select user SRAM waveforms, not
        // reserved values. Their transport/binding is not implemented here.
        // Definition presence is a support gap, not proof a note uses it.
        if (!v.isPcm && (op.wave == 15 || op.wave == 23 || op.wave == 31))
            audit_(D::VoiceDefinition, S::Unsupported, 0x130 + i, op.wave, at);
    }
}

void MaPlayer::auditExclusive_(const uint8_t* p, size_t n, uint64_t at) {
    // Call once per exclusive, not once for every voice/wave parser candidate.
    // Numeric header classifications only: never retain lyrics/raw SysEx.
    const bool wave = n >= 5 && p[0] == 0x43 && p[1] == 0x79 && p[3] == 0x7f &&
        (p[4] == 3 || p[4] == 0x23);
    if (wave) return; // actual decoder records success/rejection below
    const ParsedVoice v = parseVoiceExclusive(p, n);
    if (v.valid) return;
    const int opcode = n >= 5 && p[0] == 0x43 && p[1] == 0x79
        ? (int(p[2]) << 8) | p[4] : n ? p[0] : -1;
    audit_(ScoreAudit::Domain::Setup, ScoreAudit::Status::Unsupported, opcode, int(n), at);
}

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
    pcmLimit_ = std::any_of(file.tracks.begin(), file.tracks.end(),
        [](const TrackChunk& t) { return !t.isAudioTrack && t.formatType == 3; }) ? 32 : 16;
    scoreEndSample_ = 0; motionPace_ = 1; ended_ = false; diagnostics_ = {};
    scoreAudit_ = {}; auditFormat_ = auditTrack_ = -1;
    for (auto& wave : voiceWaveBank_) wave = PcmSample{};
    voiceWaveVersions_.clear(); voiceWaveVersionBytes_ = 0;
    activeVoiceWaveVersion_.fill(-1);
    for (auto& c : chans_) c = Chan{};
    for (auto& v : pool_) { v = FmVoice{}; v.setSampleRate(double(rate_)); }
    for (auto& pv : pcmPool_) pv.active = false;
    for (auto& pv : audioPool_) pv.active = false;
    // Reject at engine level too. Unknown grammars must never silently produce
    // plausible-looking wrong events; Format 3 is the validated MA-7 SEQU path.
    for (const auto& t : file.tracks)
        if (!t.isAudioTrack && (t.formatType < 0 || t.formatType > 3)) {
            auditFormat_ = t.formatType; auditTrack_ = t.trackNumber;
            audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Unsupported, 0x101);
            scoreAudit_.scanComplete = false;
            return false;
        }
    poolNoteId_.fill(0);
    poolPanLocked_.fill(false);
    // ~10 kHz analog-lite output filter (two 1-pole stages).
    lpCoef_ = float(1.0 - std::exp(-2.0 * 3.14159265358979 * 10000.0 / double(rate_)));
    lp1L_ = lp1R_ = lp2L_ = lp2R_ = 0.0f;
    limEnv_ = 0.0f;
    limRelease_ = float(std::exp(-1.0 / (0.15 * double(rate_))));   // ~150 ms

    collectVoices_(file);
    buildWaveBank_(file);
    for (auto& pv : pcmPool_) pv.active = false;

    auto play = file.playableScoreTracks();
    if (play.empty()) { scoreAudit_.scanComplete = false; return false; }

    for (size_t idx : play) {
        const TrackChunk& t = file.tracks[idx];
        if (t.isAudioTrack) continue; // pure ATR fallback is decoded below, once
        // handyphone = 4 channels/track, mobile = 16. global channel base keeps
        // multi-track files from colliding.
        int base = (t.formatType == 0x00) ? t.trackNumber * 4
                 : (t.formatType == 0x03) ? 0
                 : t.trackNumber * 16;
        if (base < 0 || base > 112) base = 0;
        decodeTrack_(t, base);
    }

    // audio (ATR) tracks: the wave-trigger sequence that plays the sampled
    // drums/voices alongside the score. It uses a SIX-bit wave ID and its own
    // D/G clocks (SMAF 3.05 pp49,51), never a score track's unrelated clock.
    {
        for (const TrackChunk& t : file.tracks) {
            if (!t.isAudioTrack || t.sequenceData.empty() || t.waves.empty()) continue;
            auditFormat_ = 0; auditTrack_ = t.trackNumber;
            if (t.formatType != 0 || t.sequenceType != 0) {
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Unsupported,
                       t.formatType, t.sequenceType);
                scoreAudit_.scanComplete = false;
                continue;
            }
            ScoreAudit::increment(scoreAudit_.decodedTracks);
            decodeHandyPhone_(t.sequenceData.data(), t.sequenceData.size(),
                              /*base=*/120, TrackChunk::timeBaseMs(t.durationTimeBase),
                              TrackChunk::timeBaseMs(t.gateTimeBase), true, t.trackNumber);
        }
    }

    if (events_.empty()) return false;

    // Duration belongs to the following event (SMAF 3.05, p42). Never move
    // early bank/program messages back to sample zero or move controllers
    // ahead of preceding notes in the same tick. Preserve decode insertion
    // order for ties, including the existing generated gate-off tie-break.
    std::stable_sort(events_.begin(), events_.end(),
                     [](const Ev& a, const Ev& b) {
                         return a.sample < b.sample;
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
    scoreWaveBanks_.clear();
    audioWaveBanks_.clear();
    for (const TrackChunk& t : file.tracks) {
        for (const WaveData& w : t.waves) {
            if (w.number < 0 || w.data.empty()) continue;
            PcmSample ps;
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
                    ps.pcm[i] = int16_t((w.signedPcm8
                        ? (int(w.data[i]) < 128 ? int(w.data[i]) : int(w.data[i]) - 256)
                        : int(w.data[i]) - 128) * 256);
            }
            if (!t.isAudioTrack) {
                if (size_t(w.number) >= waveBank_.size()) waveBank_.resize(w.number + 1);
                waveBank_[w.number] = ps;
                auto& bank = scoreWaveBanks_[t.trackNumber];
                if (size_t(w.number) >= bank.size()) bank.resize(w.number + 1);
                bank[w.number] = std::move(ps);
            } else {
                auto& bank = audioWaveBanks_[t.trackNumber];
                if (size_t(w.number) >= bank.size()) bank.resize(w.number + 1);
                bank[w.number] = std::move(ps);
            }
        }
    }
}

void MaPlayer::collectVoices_(const SmafFile& file) {
    for (const TrackChunk& t : file.tracks) {
        auditFormat_ = t.formatType; auditTrack_ = t.trackNumber;
        bool mobile = (t.formatType != 0x00);
        if (!t.setupData.empty()) addExclusivesFrom_(t.setupData, mobile);
    }
}

void MaPlayer::addVoiceWave_(const uint8_t* p, size_t n) {
    PcmSample decoded;
    if (decodeVoiceWave_(p, n, decoded, 65536 * sizeof(int16_t))) {
        audit_(ScoreAudit::Domain::WaveDefinition, ScoreAudit::Status::Routed, p[6]);
        voiceWaveBank_[p[5]] = std::move(decoded);
    } else if (n >= 5 && p[0] == 0x43 && p[1] == 0x79 && p[3] == 0x7f &&
               (p[4] == 3 || p[4] == 0x23)) {
        audit_(ScoreAudit::Domain::WaveDefinition, ScoreAudit::Status::Unsupported,
               n >= 7 ? p[6] : -1, int(n));
    }
}

void MaPlayer::addTimedWave_(const uint8_t* p, size_t n, uint64_t sample) {
    // Every SysEx is offered to both parsers. A full wave bank must not report
    // a voice-only message as a dropped wave definition.
    const bool waveMessage = n >= 5 && p[0] == 0x43 && p[1] == 0x79 && p[3] == 0x7f &&
        (p[4] == 3 || p[4] == 0x23);
    // Check event/version ceilings BEFORE unpacking or allocating wave memory.
    if (events_.size() >= kMaxEvents || voiceWaveVersions_.size() >= kMaxVoiceWaveVersions ||
        voiceWaveVersionBytes_ >= kMaxVoiceWaveVersionBytes) {
        if (waveMessage) audit_(ScoreAudit::Domain::WaveDefinition, ScoreAudit::Status::ResourceLimit, 0x101, 0, sample);
        return;
    }
    PcmSample decoded;
    if (!decodeVoiceWave_(p, n, decoded, kMaxVoiceWaveVersionBytes - voiceWaveVersionBytes_)) {
        if (n >= 5 && p[0] == 0x43 && p[1] == 0x79 && p[3] == 0x7f &&
            (p[4] == 3 || p[4] == 0x23))
            audit_(ScoreAudit::Domain::WaveDefinition, ScoreAudit::Status::Unsupported,
                   n >= 7 ? p[6] : -1, int(n), sample);
        return;
    }
    audit_(ScoreAudit::Domain::WaveDefinition, ScoreAudit::Status::Routed, p[6], 0, sample);
    const auto version = int16_t(voiceWaveVersions_.size());
    voiceWaveVersionBytes_ += decoded.pcm.size() * sizeof(int16_t);
    voiceWaveVersions_.push_back(std::move(decoded));
    events_.push_back({sample, Ev::WaveActivation, 0, version, int16_t(p[5])});
}

bool MaPlayer::decodeVoiceWave_(const uint8_t* p, size_t n, PcmSample& out,
                                 size_t maxDecodedBytes) {
    if (!p || n < 8 || p[0] != 0x43 || p[1] != 0x79 ||
        p[3] != 0x7f || p[5] >= 128) return false;
    // MA-7 Format 3 carries native 8-bit bytes, NOT MA-3's seven-bit
    // transport. ADPCM (0), unsigned PCM8 (2), and signed PCM8 (3) are
    // separate formats. PCM8 signedness does not change sample addressing.
    // Codec3's wire-to-internal-format mapping is still provisional in this
    // private DEV build; the manufacturer editor confirms internal3=signed8.
    // Bound allocations to the 16-bit voice address space; unsupported
    // codecs and malformed/oversize payloads leave the bank untouched.
    // Native MA-5 07/03 also carries raw bytes. Codec 0/2 interpretation
    // is corpus-supported (decoded lengths match its PCM patch endpoints),
    // not a claim that all MA-5 codecs match MA-7. Keep codec 3 MA-7 only.
    const bool rawMa5 = p[2] == 0x07 && p[4] == 0x03;
    const bool rawMa7 = p[2] == 0x08 && p[4] == 0x23;
    if (rawMa5 || rawMa7) {
        if (p[6] != 0 && p[6] != 2 && !(rawMa7 && p[6] == 3)) return false;
        const size_t bytes = n - 7;
        const size_t sampleBytes = p[6] == 0 ? 4 : 2;
        if (bytes > maxDecodedBytes / sampleBytes) return false;
        if (p[6] == 0 && bytes <= 32768) {
            YamahaAdpcm decoder;
            out.pcm = decoder.decodeAll(p + 7, bytes, false);
            out.retireSilentLoop = false;
        } else if ((p[6] == 2 || p[6] == 3) && bytes <= 65536) {
            std::vector<int16_t> pcm(bytes);
            for (size_t i = 0; i < bytes; ++i) {
                int sample = int(p[7 + i]);
                if (p[6] == 2) sample -= 128;
                else if (sample >= 128) sample -= 256;
                pcm[i] = int16_t(sample * 256);
            }
            out.pcm = std::move(pcm);
            out.retireSilentLoop = p[6] == 3;
        }
        return !out.pcm.empty();
    }
    // MA-3 voice waves use seven-bit transport for ADPCM or unsigned PCM8.
    // Only these validated codecs are enabled. Unknown variants are
    // not guessed. 16-bit LP/EP addressing bounds a wave to 65536 samples.
    if (!p || n < 9 || p[0] != 0x43 || p[1] != 0x79 || p[2] != 0x06 ||
        p[3] != 0x7f || p[4] != 3 || p[5] >= 128 || (p[6] != 0 && p[6] != 2)) return false;
    const size_t packedSize = n - 7;
    const size_t unpackedSize = packedSize - (packedSize + 7) / 8;
    if (unpackedSize > maxDecodedBytes / (p[6] == 0 ? 4 : 2)) return false;
    auto bytes = unpackMa3Bytes(p + 7, n - 7, p[6] == 0 ? 32768 : 65536);
    if (bytes.empty()) return false;
    if (p[6] == 0) {
        YamahaAdpcm decoder;
        out.pcm = decoder.decodeAll(bytes.data(), bytes.size(), false);
    } else {
        std::vector<int16_t> pcm(bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i)
            pcm[i] = int16_t((int(bytes[i]) - 128) * 256);
        out.pcm = std::move(pcm);
    }
    out.retireSilentLoop = false;
    // Playback rate belongs to the selecting PCM patch's Fs, not this block.
    return !out.pcm.empty();
}

// walk a setup-data blob and pull every yamaha voice exclusive out of it.
// hps setup: {FF F0 len payload}* ; mobile setup: {F0 len payload}* ; we accept
// an optional leading FF either way and both length encodings.
void MaPlayer::addExclusivesFrom_(const std::vector<uint8_t>& buf, bool mobile) {
    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();
    while (p < end) {
        if (*p == 0xFF) { ++p; if (p >= end) {
            audit_(ScoreAudit::Domain::Setup, ScoreAudit::Status::Malformed, 0xff); break;
        } }
        if (*p != 0xF0) {
            audit_(ScoreAudit::Domain::Setup, ScoreAudit::Status::Unsupported, *p);
            ++p; continue;
        }
        ++p;                              // consume F0
        if (p >= end) {
            audit_(ScoreAudit::Domain::Setup, ScoreAudit::Status::Malformed, 0xf0); break;
        }
        // length: try vlq for mobile, single byte for hps, but fall back so a
        // len >= 0x80 hps value is read as the 2-byte form.
        uint32_t len;
        len = readScoreLength_(p, end, !mobile, 0, ScoreAudit::Domain::Setup);
        if (len == 0 || len > size_t(end - p)) {
            audit_(ScoreAudit::Domain::Setup, ScoreAudit::Status::Malformed, 0xf0, int(len));
            break;
        }
        // payload includes the trailing F7; parse from the maker id up to it.
        size_t plen = len;
        if (plen && p[plen - 1] == 0xF7) --plen;
        addVoiceWave_(p, plen);
        ParsedVoice v = parseVoiceExclusive(p, plen);
        // isPcm identifies a decoded family, not a complete usable image.
        // Invalid PCM definitions must not mask a previously valid voice.
        if (v.valid) {
            auditVoice_(v, 0);
            if (voiceTable_.size() < kMaxCustomVoices) voiceTable_.push_back(v);
            else audit_(ScoreAudit::Domain::VoiceDefinition, ScoreAudit::Status::ResourceLimit, 0x100);
        } else auditExclusive_(p, plen, 0);
        p += len;
    }
}

void MaPlayer::decodeTrack_(const TrackChunk& t, int base) {
    auditFormat_ = t.formatType; auditTrack_ = t.trackNumber;
    ScoreAudit::increment(scoreAudit_.decodedTracks);
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
        for (int i = 0; i < (t.formatType == 3 ? 32 : 16); ++i) {
            auto& c = chans_[(base + i) & 127];
            c.streamTrack = t.trackNumber;
            c.squaredControllers = t.formatType == 3;
            c.modernTimbre = true;
            if (t.formatType == 1 || t.formatType == 2) c.fixedFmDrumPitch = false;
            c.volume = 100.0f / 127.0f;
            if (c.squaredControllers) c.volume *= c.volume;
        }
        for (size_t i = 0; i < t.channelStatus.size() && i < 32; ++i) {
            chans_[(base + i) & 127].rhythm = ((t.channelStatus[i] & 0x03) == 3);
            // Mobile bits7..6: 0 unspecified, 1 OFF (fixed), 2 ON (key-scaled).
            // For unspecified, use score pitch as the original smaf825 player
            // does. This is a compatibility policy, not measured chip behavior.
            // Do not reinterpret the LED/vibration bits as KeyControl.
            if ((t.formatType == 1 || t.formatType == 2) && i < 16)
                chans_[(base + i) & 127].fixedFmDrumPitch = (t.channelStatus[i] >> 6) == 1;
        }
    }
    if (t.formatType == 0x00) {
        decodeHandyPhone_(t.sequenceData.data(), t.sequenceData.size(), base, tbD, tbG);
    } else if (t.formatType == 0x01) {
        // huffman-compressed mobile body.
        auto inflated = smafHuffmanInflate(t.sequenceData.data(), t.sequenceData.size());
        if (t.sequenceData.size() < 4 || inflated.empty() ||
            inflated.size() != be32(t.sequenceData.data()))
            audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Malformed,
                   0x102, int(inflated.size()));
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
                                 bool atrWaveMode, int atrTrack) {
    const uint8_t* end = p + n;
    double curMs = 0.0;
    auto msToSample = [&](double ms) { return uint64_t(ms * rate_ / 1000.0); };
    int guard = 0;
    while (p < end && guard++ < 2000000 && events_.size() < kMaxEvents) {
        uint32_t dur = readScoreLength_(p, end, true, msToSample(curMs), ScoreAudit::Domain::Sequence);
        curMs += dur * tbDms;
        if (!scoreBytes_(p, end, 1, 0x103, msToSample(curMs))) break;
        uint8_t e1 = *p++;
        if (e1 == 0xFF) {
            if (!scoreBytes_(p, end, 1, 0xff, msToSample(curMs))) break;
            uint8_t e2 = *p++;
            if (e2 == 0x00) continue;                 // NOP
            if (e2 == 0xF0) {                         // exclusive (voice)
                uint32_t len = readScoreLength_(p, end, true, msToSample(curMs), ScoreAudit::Domain::Sequence);
                if (!scoreBytes_(p, end, len, 0xf0, msToSample(curMs))) break;
                size_t plen = len; if (plen && p[plen-1]==0xF7) --plen;
                addTimedVoice_(p, plen, msToSample(curMs));
                p += len;
                continue;
            }
            // 0x2F/0x51/0x58 style meta: read a length byte + skip.
            if (!scoreBytes_(p, end, 1, 0xff, msToSample(curMs))) break;
            uint8_t l = *p++;
            scoreBytes_(p, end, l, 0xff, msToSample(curMs));
            audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0xff, e2, msToSample(curMs));
            p += (size_t(end - p) >= l) ? l : size_t(end - p);
            continue;
        }
        if (e1 == 0x00) {
            if (!scoreBytes_(p, end, 1, 0, msToSample(curMs))) break;
            uint8_t e2 = *p++;
            if (e2 == 0x00) {                         // extended / EOS
                if (!scoreBytes_(p, end, 1, 0, msToSample(curMs))) break;
                uint8_t e3 = *p++;
                if (e3 == 0x00) break;                // End Of Sequence
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Unsupported, 0x200, e3, msToSample(curMs));
                continue;
            }
            int ch = base + ((e2 >> 6) & 3);
            int cls = (e2 >> 4) & 3;
            int data = e2 & 0x0f;
            uint64_t s = msToSample(curMs);
            if (cls == 3) {                           // long event: value byte
                if (!scoreBytes_(p, end, 1, data, s, ch)) break;
                uint8_t v = *p++;
                const bool supported = data == 0 || data == 1 || data == 2 ||
                    data == 4 || data == 7 || data == 10 || data == 11;
                audit_(ScoreAudit::Domain::HpsLong, supported ? ScoreAudit::Status::Routed :
                    ScoreAudit::Status::Unsupported, data, v, s, ch);
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
                audit_(ScoreAudit::Domain::HpsShort, ScoreAudit::Status::Routed, cls, data, s, ch);
                // expression SHORT: value = data==1 ? 0 : data*8+15 (spec table).
                int v = (data <= 1) ? (data == 0 ? 0 : 0) : (data * 8 + 15);
                if (v > 0x7f) v = 0x7f;
                events_.push_back({s, Ev::Expression, uint16_t(ch), int16_t(v), 0});
            } else if (cls == 1) {
                audit_(ScoreAudit::Domain::HpsShort, ScoreAudit::Status::Routed, cls, data, s, ch);
                // pitch-bend SHORT: msb = data*8, centre at data==8.
                int v14 = (data * 8) << 7;
                events_.push_back({s, Ev::PitchBend, uint16_t(ch), int16_t(v14 - 8192), 0});
            } else {   // cls == 2
                audit_(ScoreAudit::Domain::HpsShort, ScoreAudit::Status::Unapplied, cls, data, s, ch);
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
        uint32_t gate = readScoreLength_(p, end, true, msToSample(curMs), ScoreAudit::Domain::Sequence);
        if (gate == 0) continue;                       // spec: skip zero-gate
        if (atrWaveMode) {
            const int wave = e1 & 0x3f;
            if (wave == 0 || wave == 63) {
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved,
                       wave, 0, msToSample(curMs), ch);
                continue;
            }
            // One-shot policy unchanged; e.b binds the source ATR, not velocity.
            events_.push_back({msToSample(curMs), Ev::WaveOn, uint16_t(ch),
                               int16_t(wave), int16_t(atrTrack)});
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
    if (p < end && (guard >= 2000000 || events_.size() >= kMaxEvents))
        audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::ResourceLimit, 0x104, 0, msToSample(curMs));
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
        uint32_t dur = readScoreLength_(p, end, false, msToSample(curMs), ScoreAudit::Domain::Sequence);
        curMs += dur * tbDms;
        if (!scoreBytes_(p, end, 1, 0x103, msToSample(curMs))) break;
        uint8_t s = *p++;
        if (s < 0x80) {
            audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Malformed, s, 0, msToSample(curMs));
            continue;
        }
        int ch = base + (s & 0x0f);
        uint64_t at = msToSample(curMs);
        switch (s & 0xf0) {
            case 0x80: case 0x90: {                    // note off-vel / on-vel
                if (!scoreBytes_(p, end, 1, s, at, ch)) break;
                int note = *p++;
                int vel;
                if ((s & 0xf0) == 0x90) {
                    if (!scoreBytes_(p, end, 1, s, at, ch)) break;
                    vel = *p++;
                    runVel[s & 0x0f] = uint8_t(vel & 0x7f);   // remember
                } else {
                    vel = runVel[s & 0x0f];                    // reuse remembered
                }
                uint32_t gate = readScoreLength_(p, end, false, at, ScoreAudit::Domain::Sequence);
                if (gate == 0) break;
                uint64_t off = at + msToSample(gate * tbGms);
                const uint64_t id = nextNoteId_++;
                events_.push_back({at,  Ev::NoteOn,  uint16_t(ch), int16_t(note & 0x7f), int16_t(vel & 0x7f), id});
                events_.push_back({off, Ev::NoteOff, uint16_t(ch), int16_t(note & 0x7f), 0, id});
            } break;
            case 0xA0:
                scoreBytes_(p, end, 2, s, at, ch);
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0xa0, 0, at, ch);
                p += (end - p >= 2 ? 2 : (end - p)); break;   // reserved
            case 0xB0: {                                // control change
                if (!scoreBytes_(p, end, 2, s, at, ch)) { p = end; break; }
                int cc = *p++, val = *p++;
                routeController_(at, ch, cc, val, false);
            } break;
            case 0xC0: { if (!scoreBytes_(p, end, 1, s, at, ch)) break; int pc = *p++;
                events_.push_back({at, Ev::Program, uint16_t(ch), int16_t(pc & 0x7f), 0}); } break;
            case 0xD0:
                scoreBytes_(p, end, 1, s, at, ch);
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0xd0, 0, at, ch);
                if (p < end) ++p; break;          // reserved
            case 0xE0: {                                 // pitch bend lsb,msb
                if (!scoreBytes_(p, end, 2, s, at, ch)) { p = end; break; }
                int lsb = *p++, msb = *p++;
                int v14 = ((msb & 0x7f) << 7) | (lsb & 0x7f);
                events_.push_back({at, Ev::PitchBend, uint16_t(ch), int16_t(v14 - 8192), 0});
            } break;
            case 0xF0: {
                if (s == 0xF0) {                         // exclusive
                    uint32_t len = readScoreLength_(p, end, false, at, ScoreAudit::Domain::Sequence);
                    if (!scoreBytes_(p, end, len, s, at)) { p = end; break; }
                    size_t plen = len; if (plen && p[plen-1]==0xF7) --plen;
                    addTimedWave_(p, plen, at);
                    addTimedVoice_(p, plen, at);
                    p += len;
                } else if (s == 0xFF) {                  // meta
                    if (!scoreBytes_(p, end, 1, s, at, ch)) break;
                    uint8_t m = *p++;
                    if (m == 0x00) break;                // NOP
                    if (m == 0x2F) { p = end; }          // EOS
                    else if (p < end) {
                        uint8_t l = *p++;
                        scoreBytes_(p, end, l, s, at);
                        audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0xff, m, at);
                        p += (end - p >= l ? l : (end - p));   // clamp, never past end
                    } else scoreBytes_(p, end, 1, s, at);
                } else {
                    audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Unsupported, s, 0, at);
                    scoreAudit_.scanComplete = false;
                }
            } break;
            default: break;
        }
    }
    if (p < end && (guard >= 4000000 || events_.size() >= kMaxEvents))
        audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::ResourceLimit, 0x104, 0, msToSample(curMs));
}

// ── voice resolution ────────────────────────────────────────────────────────
void MaPlayer::addTimedVoice_(const uint8_t* p, size_t n, uint64_t sample) {
    if (voiceTable_.size() >= kMaxCustomVoices || events_.size() >= kMaxEvents) {
        const bool waveMessage = n >= 5 && p[0] == 0x43 && p[1] == 0x79 && p[3] == 0x7f &&
            (p[4] == 3 || p[4] == 0x23);
        if (!waveMessage) audit_(ScoreAudit::Domain::VoiceDefinition, ScoreAudit::Status::ResourceLimit, 0x100, 0, sample);
        return;
    }
    ParsedVoice v = parseVoiceExclusive(p, n);
    if (!v.valid) { auditExclusive_(p, n, sample); return; }
    auditVoice_(v, sample);
    v.sequenced = true;
    v.activationSample = sample;
    events_.push_back({sample, Ev::VoiceActivation, 0, int16_t(voiceTable_.size()), 0});
    voiceTable_.push_back(v);
}

const FmVoicePatch& MaPlayer::patchFor_(int ch, int note, bool& isPcmOut) const {
    const ParsedVoice* v = resolveVoice_(ch, note);
    isPcmOut = v && v->isPcm;
    return v ? v->patch : FmVoicePatch::gmApprox(chans_[ch & 127].program);
}

// the matched voice for a channel + note, or nullptr when only the gm fallback
// applies. drum voices bind on (bank, pc, drumNote==note); melody on (bank, pc).
const ParsedVoice* MaPlayer::resolveVoice_(int ch, int note) const {
    const Chan& c = chans_[ch & 127];
    const ParsedVoice* melodyHit = nullptr;
    const ParsedVoice* drumHit = nullptr;
    auto newer = [](const ParsedVoice& v, const ParsedVoice* old) {
        return !old || (v.sequenced && (!old->sequenced || v.activationSample >= old->activationSample));
    };
    for (const ParsedVoice& v : voiceTable_) {
        if (v.sequenced && !v.activated) continue;
        if (v.key.bankMSB == c.bankMsb && v.key.bankLSB == c.bankLsb && v.key.pc == c.program) {
            if (v.key.bankMSB == 125 || v.key.drumNote != 0) {
                if (v.key.drumNote == note && newer(v, drumHit)) drumHit = &v;
            } else if (note <= v.keyHigh && (!melodyHit || v.keyHigh < melodyHit->keyHigh ||
                       (v.keyHigh == melodyHit->keyHigh && newer(v, melodyHit))))
                melodyHit = &v;
        }
    }
    return drumHit ? drumHit : melodyHit;
}

// start a sampled (pcm) note on a free pcm slot. drums play at native rate,
// melodic pcm is pitched from a fixed root note.
int MaPlayer::allocatePcmSlot_() {
    for (int i=0;i<pcmLimit_;++i) if (!pcmPool_[i].active) return i;
    int slot=-1;
    for (int i=0;i<pcmLimit_;++i)
        if (pcmPool_[i].released && (slot<0 || pcmPool_[i].recentLevel<pcmPool_[slot].recentLevel)) slot=i;
    ++diagnostics_.stolenPcm;
    if (slot<0) { slot=pcmSteal_++%pcmLimit_; ++diagnostics_.stolenHeldPcm; }
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
        uint32_t dur = readScoreLength_(p, end, false, msToSample(curMs), ScoreAudit::Domain::Sequence);
        curMs += dur * tbDms;
        if (!scoreBytes_(p, end, 1, 0x103, msToSample(curMs))) break;
        uint8_t status = *p++;
        uint64_t at = msToSample(curMs);

        if (status == 0xF0) {
            uint32_t len = readScoreLength_(p, end, false, at, ScoreAudit::Domain::Sequence);
            if (!scoreBytes_(p, end, len, status, at)) break;
            size_t plen = len;
            if (plen && p[plen - 1] == 0xF7) --plen;
            addTimedWave_(p, plen, at);
            addTimedVoice_(p, plen, at);
            p += len;
            continue;
        }
        if (status == 0xFF) {
            if (!scoreBytes_(p, end, 1, status, at)) break;
            uint8_t meta = *p++;
            if (meta == 0x00) continue;
            if (meta == 0x2F) {
                if (p < end) ++p; // zero-length byte
                break;
            }
            if (!scoreBytes_(p, end, 1, status, at)) break;
            uint32_t len = readScoreLength_(p, end, false, at, ScoreAudit::Domain::Sequence);
            scoreBytes_(p, end, len, status, at);
            audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0xff, meta, at);
            p += std::min<size_t>(len, size_t(end - p));
            continue;
        }

        const int local = (status & 0x0F) + ((status & 0x80) ? 16 : 0);
        const int ch = base + local;
        const int eventClass = status & 0x70;
        switch (eventClass) {
            case 0x00:
            case 0x10: {
                if (!scoreBytes_(p, end, 1, status, at, ch)) { p = end; break; }
                int note = *p++ & 0x7F;
                int vel = runVel[local];
                if (eventClass == 0x10) {
                    if (!scoreBytes_(p, end, 1, status, at, ch)) { p = end; break; }
                    vel = *p++ & 0x7F;
                    runVel[local] = uint8_t(vel);
                }
                uint32_t gate = readScoreLength_(p, end, false, at, ScoreAudit::Domain::Sequence);
                if (gate == 0) break;
                uint64_t off = at + msToSample(gate * tbGms);
                const uint64_t id = nextNoteId_++;
                events_.push_back({at, Ev::NoteOn, uint16_t(ch), int16_t(note), int16_t(vel), id});
                events_.push_back({off, Ev::NoteOff, uint16_t(ch), int16_t(note), 0, id});
            } break;
            case 0x20: // reserved, two data bytes
                scoreBytes_(p, end, 2, status, at, ch);
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0x20, 0, at, ch);
                p += std::min<size_t>(2, size_t(end - p));
                break;
            case 0x30: {
                if (!scoreBytes_(p, end, 2, status, at, ch)) { p = end; break; }
                int cc = *p++, val = *p++;
                routeController_(at, ch, cc, val, true);
            } break;
            case 0x40:
                scoreBytes_(p, end, 1, status, at, ch);
                if (p < end)
                    events_.push_back({at, Ev::Program, uint16_t(ch), int16_t(*p++ & 0x7F), 0});
                break;
            case 0x50: // reserved, one data byte
                scoreBytes_(p, end, 1, status, at, ch);
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Reserved, 0x50, 0, at, ch);
                if (p < end) ++p;
                break;
            case 0x60: {
                if (!scoreBytes_(p, end, 2, status, at, ch)) { p = end; break; }
                int lsb = *p++, msb = *p++;
                int v14 = ((msb & 0x7F) << 7) | (lsb & 0x7F);
                events_.push_back({at, Ev::PitchBend, uint16_t(ch), int16_t(v14 - 8192), 0});
            } break;
            default:
                audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::Unsupported, eventClass, 0, at, ch);
                scoreAudit_.scanComplete = false;
                p = end; // 0x70 is reserved and has no validated width.
                break;
        }
    }
    if (p < end && (guard >= 4000000 || events_.size() >= kMaxEvents))
        audit_(ScoreAudit::Domain::Sequence, ScoreAudit::Status::ResourceLimit, 0x104, 0, msToSample(curMs));
}

bool MaPlayer::startPcm_(int ch, int rawNote, int soundingNote, const ParsedVoice& v,
                         const Chan& c, float noteVelocity, uint64_t noteId) {
    // Bind ONLY the referenced wave. An unrelated short sample is not a
    // substitute for a missing instrument and can sound like random noise.
    const PcmSample* ws = nullptr;
    int wid = v.pcm.waveId;
    if (v.pcm.rom) return false; // ROM IDs must never alias a local RAM sample
    if (wid >= 0 && wid < 128) {
        const int version = activeVoiceWaveVersion_[wid];
        const PcmSample& selected = version >= 0 ? voiceWaveVersions_[size_t(version)] : voiceWaveBank_[wid];
        if (!selected.pcm.empty()) ws = &selected;
    }
    if (!ws && !v.pcm.voiceWaveOnly && wid >= 0 && size_t(wid) < waveBank_.size() && !waveBank_[wid].pcm.empty())
        ws = &waveBank_[wid];
    if (!ws) return false;
    if (v.pcm.endPt == 0) return true; // EP=0 intentionally does not pronounce

    int slot = allocatePcmSlot_();
    PcmVoice& pv = pcmPool_[slot];
    pv.streamAudio = false;
    pv.pcm = ws->pcm.data(); pv.len = ws->pcm.size();
    // Bank125 is a custom drum voice even when its mapped trigger key is zero.
    // A zero key must not turn a native-rate drum into a five-octave-down tone.
    bool isDrum = v.key.bankMSB == 125 || v.key.drumNote != 0;
    double base = double(v.pcm.fs) / double(rate_);
    double semis = isDrum ? 0.0 : double(soundingNote - 60);   // root C4
    pv.baseRate = base * std::pow(2.0, semis / 12.0);
    pv.rate = pv.baseRate * std::pow(2.0, c.bend / 12.0);
    pv.pos = 0.0;
    pv.qualityInterpolation = c.modernTimbre;
    pv.loopTravel = 0;
    if (pv.qualityInterpolation) {
        (void)pcm_detail::interpolationKernels();
        (void)pcm_detail::downsamplingKernels();
    }
    // Share the existing FM approximation rather than invent a different
    // WT vibrato curve. The hardware LFO rate/shape remains uncalibrated.
    static constexpr double lfoHz[4] = {1.8, 4.0, 5.9, 7.0};
    static constexpr double vibrato[4] = {0.00196, 0.00387, 0.00774, 0.01548};
    static constexpr float legacyTremolo[4] = {0.129f, 0.242f, 0.424f, 0.669f};
    static constexpr double damDb[4] = {1.3, 2.8, 5.8, 11.8};
    pv.lfoPhase = 0.0;
    pv.baseVibDepth = v.pcm.env.vib ? vibrato[v.pcm.env.dvb & 3] : 0.0;
    pv.vibDepth = pv.baseVibDepth * (c.squaredControllers ? c.vibratoScale : 1.0);
    pv.amDepth = v.pcm.env.am ? (c.modernTimbre
        ? float(1.0 - std::pow(10.0, -damDb[v.pcm.env.dam & 3] / 20.0))
        : legacyTremolo[v.pcm.env.dam & 3]) : 0.0f;
    pv.lfoInc = (pv.baseVibDepth != 0.0 || pv.amDepth != 0.0f)
        ? lfoHz[v.pcm.lfo & 3] / double(rate_) : 0.0;
    // LP/EP are sample indices into the decoded stream; clamp to the wave.
    pv.loopEnd   = (v.pcm.endPt > 0 && size_t(v.pcm.endPt) <= pv.len) ? size_t(v.pcm.endPt) : pv.len;
    pv.loopStart = (size_t(v.pcm.loopPt) < pv.loopEnd) ? size_t(v.pcm.loopPt) : 0;
    pv.loop = v.pcm.loop && v.pcm.loopPt >= 0 && size_t(v.pcm.loopPt) < pv.loopEnd;
    pv.silentLoop = ws->retireSilentLoop && pv.loop &&
        std::all_of(ws->pcm.begin() + pv.loopStart, ws->pcm.begin() + pv.loopEnd,
                    [](int16_t sample) { return sample == 0; });
    // Preserve the existing exact-zero retirement contract (sinc has history).
    if (pv.silentLoop) pv.qualityInterpolation = false;
    pv.env.configure(v.pcm.env, double(rate_)); pv.env.keyOn();
    pv.gain = float(std::pow(10.0, -0.75 * double(v.pcm.env.tl) / 20.0));
    // Match FM voices: authored note velocity is part of the musical balance,
    // not just channel volume. Ignoring it made sampled drums and instruments
    // dominate quieter FM parts.
    pv.vel = c.volume * c.expression * noteVelocity;
    pv.noteVelocity = noteVelocity;
    pv.pan = v.pcm.panEnabled ? v.pcm.pan : c.pan;
    pv.panLocked = v.pcm.panEnabled;
    channelPanGains(pv.pan, c.squaredControllers && !pv.panLocked, pv.panLeft, pv.panRight);
    pv.channel = ch; pv.keyNote = rawNote; pv.active = true;
    pv.noteId = noteId;
    pv.released = false; pv.recentLevel = 0;
    ++diagnostics_.pcmNotes;
    return true;
}

void MaPlayer::startAudio_(int ch, int note, float velocity, uint64_t noteId) {
    if (velocity <= (1.0f / (127.0f * 127.0f))) return; // velocity 1 means mute
    const auto found = scoreWaveBanks_.find(chans_[ch & 127].streamTrack);
    const int id = note <= 12 ? note + 1 : note - 92 + 14;
    if (found == scoreWaveBanks_.end() || size_t(id) >= found->second.size() ||
        found->second[id].pcm.empty()) { ++diagnostics_.missingAudio; return; }
    const auto& ws = found->second[id];
    PcmVoice* slot = nullptr;
    for (auto& v : audioPool_) if (!v.active) { slot = &v; break; }
    if (!slot) {
        slot = &*std::min_element(audioPool_.begin(), audioPool_.end(),
            [](const PcmVoice& a, const PcmVoice& b) { return a.startedAt < b.startedAt; });
        ++diagnostics_.stolenAudio;
    }
    *slot = PcmVoice{};
    slot->pcm = ws.pcm.data(); slot->len = ws.pcm.size();
    slot->rate = double(ws.fs) / double(rate_);
    slot->vel = velocity; slot->pan = chans_[ch & 127].pan;
    channelPanGains(slot->pan, chans_[ch & 127].squaredControllers, slot->panLeft, slot->panRight);
    slot->channel = ch; slot->keyNote = note; slot->noteId = noteId;
    slot->startedAt = cursor_;
    slot->streamAudio = true; slot->active = true;
    ++diagnostics_.audioNotes; ++diagnostics_.pcmNotes;
}

float MaPlayer::PcmVoice::tick() {
    if (!active || !pcm || len == 0) { active = false; return 0.0f; }
    // A zero-only loop cannot become audible again after reaching its start.
    // Retire it without cutting any nonzero sample or guessing an EG duration.
    if (silentLoop && pos >= double(loopStart)) { active = false; return 0.0f; }
    size_t end = (loopEnd > 0 && loopEnd <= len) ? loopEnd : len;
    if (pos >= double(end)) {
        if (loop && loopEnd > loopStart) {
            const double wrapped = double(loopStart)
                + std::fmod(pos - double(end), double(loopEnd - loopStart));
            // Preserve original attack history across the first few traversals
            // of a very short loop; cap once every kernel tap is periodic.
            const size_t length = loopEnd - loopStart;
            const size_t history = qualityInterpolation ? 16 : 8;
            const size_t limit = ((end + history + length - 1) / length) * length;
            if (loopTravel < limit) {
                const double traversals = std::floor((pos - double(end)) / double(length)) + 1.0;
                const size_t removed = size_t(std::min(double(limit), traversals * double(length)));
                loopTravel = std::min(limit, loopTravel + removed);
            }
            pos = wrapped;
        }
        else { active = false; return 0.0f; }
    }
    size_t i = size_t(pos);
    if (i >= len) { active = false; return 0.0f; }
    float frac = float(pos - double(i));
    double lfo = 0.0;
    if (lfoInc != 0.0) {
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= std::floor(lfoPhase);
        lfo = pcmModulationSine(lfoPhase);
    }
    // Keep controller bend in rate; apply vibrato transiently, not by
    // accumulating it into rate/baseRate on each sample or controller event.
    const double advance = rate * (1.0 + vibDepth * lfo);
    float allocationSample;
    const float s = pcm_detail::interpolatePcm(pcm, end, i, frac, loop,
        loopStart, loopTravel, qualityInterpolation && !streamAudio, advance,
        &allocationSample);
    pos += advance;
    float e;
    if (streamAudio) {
        if (stopFade == 0) { active = false; return 0.0f; }
        e = stopFade < 0 ? 1.0f : float(stopFade--) / float(stopFadeTotal);
    } else {
        e = env.advance();
        if (env.isFinished()) active = false;
    }
    float result = s * e * gain * vel;
    float allocationLevel = allocationSample * e * gain * vel;
    if (amDepth != 0.0f) {
        const float modulation = 1.0f - amDepth * float(0.5 + 0.5 * lfo);
        result *= modulation;
        allocationLevel *= modulation;
    }
    // Interpolation quality must not reorder the established voice-steal policy.
    recentLevel += (std::fabs(allocationLevel) - recentLevel) * 0.01f;
    return result;
}

double MaPlayer::noteToFreq_(int midiNote) const {
    return 440.0 * std::pow(2.0, (double(midiNote) - 69.0) / 12.0);
}

int MaPlayer::activeVoices_() const {
    int n = 0;
    for (const auto& v : pool_) if (v.active()) ++n;
    for (const auto& v : pcmPool_) if (v.active) ++n;
    for (const auto& v : audioPool_) if (v.active) ++n;
    return n;
}

void MaPlayer::fireEvent_(const Ev& e) {
    Chan& c = chans_[e.ch & 127];
    switch (e.type) {
        case Ev::WaveActivation:
            if (e.a >= 0 && size_t(e.a) < voiceWaveVersions_.size() && e.b >= 0 && e.b < 128)
                activeVoiceWaveVersion_[size_t(e.b)] = e.a;
            break;
        case Ev::VoiceActivation:
            if (e.a >= 0 && size_t(e.a) < voiceTable_.size())
                voiceTable_[size_t(e.a)].activated = true;
            break;
        case Ev::Program:   c.program = e.a; break;
        case Ev::BankMsb:   c.bankMsb = e.a; break;
        case Ev::BankLsb:
            c.bankLsb = e.a & 0x7f;              // 7-bit bank
            c.drum    = (e.a & 0x80) != 0;       // bit7 = drum bank
            break;
        case Ev::Volume: {
            c.volume = (e.a & 0x7f) / 127.0f;
            if (c.squaredControllers) c.volume *= c.volume;
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                    voice.vel = voice.noteVelocity * c.volume * c.expression;
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch))
                    pool_[i].setVolume(c.volume * c.expression);
        } break;
        case Ev::Expression: {
            c.expression = (e.a & 0x7f) / 127.0f;
            if (c.squaredControllers) c.expression *= c.expression;
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                    voice.vel = voice.noteVelocity * c.volume * c.expression;
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch))
                    pool_[i].setVolume(c.volume * c.expression);
        } break;
        case Ev::Pan:
            c.pan = ((e.a & 0x7f) - 64) / 64.0f;
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch) && !poolPanLocked_[i]) {
                    poolPan_[i] = c.pan;
                    channelPanGains(c.pan, c.squaredControllers, poolPanLeft_[i], poolPanRight_[i]);
                }
            for (auto& voice : audioPool_)
                if (voice.active && voice.channel == int(e.ch)) {
                    voice.pan = c.pan;
                    channelPanGains(c.pan, c.squaredControllers, voice.panLeft, voice.panRight);
                }
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0 && !voice.panLocked) {
                    voice.pan = c.pan;
                    channelPanGains(c.pan, c.squaredControllers, voice.panLeft, voice.panRight);
                }
            break;
        case Ev::Modulation:
            if (e.b == 1) c.octShift = e.a - 1000;    // octave-shift marker
            else if (c.squaredControllers && e.a >= 0 && e.a <= 127) {
                // MA7 SMAF guideline8.3.2/Table6: magnify each voice's own
                // vibrato by OFF,1,2,4,8. No effect on tremolo or recorded Audio.
                c.vibratoScale = e.a == 0 ? 0.0 : double(1 << (e.a / 32));
                for (auto& voice : pool_)
                    if (voice.active() && voice.channel == int(e.ch)) voice.setVibratoScale(c.vibratoScale);
                for (auto& voice : pcmPool_)
                    if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                        voice.vibDepth = voice.baseVibDepth * c.vibratoScale;
            }
            break;
        case Ev::RpnMsb: if(e.a>=0 && e.a<=127) c.rpnMsb=e.a; break;
        case Ev::RpnLsb: if(e.a>=0 && e.a<=127) c.rpnLsb=e.a; break;
        case Ev::NrpnSelect: c.rpnMsb=c.rpnLsb=127; break;
        case Ev::Hold: {
            c.hold = e.a >= 64;
            for (auto& voice : pool_)
                if (voice.active() && voice.channel == int(e.ch)) voice.setHold(c.hold);
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0) {
                    bool pending = voice.env.keyOffDeferred();
                    voice.env.setHold(c.hold);
                    if (pending && !voice.env.keyOffDeferred()) voice.released = true;
                }
        } break;
        case Ev::RpnData:
        case Ev::PitchBendRange:
        case Ev::PitchBend: {
            if(e.type==Ev::RpnData) {
                // Standard Mobile RPN0/0 only. Null/other RPNs and NRPNs must
                // not change bend sensitivity. Data Entry LSB means cents.
                if(c.rpnMsb!=0 || c.rpnLsb!=0) break;
                if(e.b==0) { if(e.a<0 || e.a>24) break; c.bendRange=e.a; }
                else { if(e.a<0 || e.a>99) break; c.bendCents=e.a; }
            } else if (e.type == Ev::PitchBendRange) {
                if (e.a < 0 || e.a > 24) break;
                c.bendRange = e.a; c.bendCents=0;
            } else {
                c.rawBend = e.a;
            }
            c.bend = double(c.rawBend) / 8192.0 * (c.bendRange+c.bendCents/100.0);
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0)
                    voice.rate = voice.baseRate * std::pow(2.0, c.bend / 12.0);
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch))
                    pool_[i].setPitch(poolBaseFreq_[i] * std::pow(2.0, c.bend / 12.0));
        } break;
        case Ev::ResetControllers:
            // RP-015: reset expression, pedal and pitch position, and null
            // parameter selection, not bank/PC/volume/pan or parameter VALUES.
            // Reuse live-control paths so released/Hold-deferred voices also
            // receive the update. Do not manufacture any new NoteOff events.
            fireEvent_({e.sample, Ev::Expression, e.ch, 127, 0});
            fireEvent_({e.sample, Ev::Hold, e.ch, 0, 0});
            fireEvent_({e.sample, Ev::PitchBend, e.ch, 0, 0});
            c.rpnMsb = c.rpnLsb = 127;
            // Mobile CC1/pressures/other pedals are not synthesized yet.
            // Authored intrinsic EVB is not a modulation-wheel setting and
            // must remain intact. Audio/ATR keep their existing CC exclusions.
            break;
        case Ev::AllSoundOff: {
            // No key-identity test: already released and pedal-deferred voices
            // also belong to All Sound Off. Preserve shared filter/limiter
            // history so other channels are not disturbed by this command.
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && pool_[i].channel == int(e.ch)) {
                    pool_[i] = FmVoice{};
                    pool_[i].setSampleRate(double(rate_));
                    poolNoteId_[i] = 0;
                }
            for (auto& voice : pcmPool_)
                if (voice.active && voice.channel == int(e.ch) && voice.keyNote >= 0) {
                    voice.active = false; voice.noteId = 0;
                }
            for (auto& voice : audioPool_)
                if (voice.active && voice.channel == int(e.ch)) {
                    voice.active = false; voice.noteId = 0;
                }
            // ATR one-shots are an independent AudioTrack source, not keyed
            // score voices; retain their existing exclusion from channel CCs.
        } break;
        case Ev::AllNotesOff:
        case Ev::NoteOff: {
            const bool all = e.type == Ev::AllNotesOff;
            // A consumed identity denotes an already lifted key, not a new
            // key-off request. This also excludes ATR one-shots (identity0).
            // Consuming once prevents later gates/commands from rearming Hold
            // or restarting an Audio stop fade. A new allocation gets a new ID.
            auto matches = [&](int channel, uint64_t id) {
                return channel == int(e.ch) && id != 0 && (all || id == e.noteId);
            };
            for (auto& voice : audioPool_)
                if (voice.active && matches(voice.channel, voice.noteId)) {
                    voice.noteId = 0;
                    voice.stopFadeTotal = std::max(1, int(rate_ / 500));
                    voice.stopFade = voice.stopFadeTotal;
                    voice.released = true;
                }
            // Every scheduled gate belongs to one note instance. Same-pitch
            // notes may overlap, finish out of order, or reuse a stolen slot.
            for (int i = 0; i < kPoolSize; ++i)
                if (pool_[i].active() && matches(pool_[i].channel, poolNoteId_[i])) {
                    poolNoteId_[i] = 0;
                    pool_[i].noteOff(c.hold);
                    if (!all) break;
                }
            for (int i = 0; i < kPcmPool; ++i)          // release a held pcm note
                if (pcmPool_[i].active && matches(pcmPool_[i].channel, pcmPool_[i].noteId)) {
                    pcmPool_[i].noteId = 0;
                    pcmPool_[i].env.keyOff(c.hold);
                    pcmPool_[i].released = !pcmPool_[i].env.keyOffDeferred();
                }
        } break;
        case Ev::WaveOn: {
            // ATR wave trigger: play wave e.a one-shot at its native rate, no
            // envelope shaping (the sample IS the sound, it ends on its own).
            int wid = e.a;
            const auto bank = audioWaveBanks_.find(e.b);
            if (wid < 1 || wid > 62 || bank == audioWaveBanks_.end() ||
                size_t(wid) >= bank->second.size() || bank->second[wid].pcm.empty()) break;
            const PcmSample& ws = bank->second[wid];
            int slot = allocatePcmSlot_();
            PcmVoice& pv = pcmPool_[slot];
            pv.streamAudio = false;
            pv.qualityInterpolation = false;
            pv.loopTravel = 0;
            pv.pcm = ws.pcm.data(); pv.len = ws.pcm.size();
            pv.rate = double(ws.fs) / double(rate_);
            pv.lfoPhase = pv.lfoInc = pv.vibDepth = pv.baseVibDepth = 0.0;
            pv.amDepth = 0.0f; // ATR must not inherit a stolen WT slot's LFO
            pv.pos = 0.0; pv.loopStart = 0; pv.loopEnd = pv.len; pv.loop = false;
            pv.silentLoop = false;
            FmOpPatch flat{};                      // full-open sustaining envelope
            flat.ar = 15; flat.sl = 0; flat.sr = 0; flat.egType = true;
            pv.env.configure(flat, double(rate_)); pv.env.keyOn();
            pv.gain = 1.0f; pv.vel = 1.0f; pv.pan = 0.0f;
            pv.panLeft = pv.panRight = 1.0f;
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
            if (c.streamTrack >= 0 && c.bankMsb == 125 && c.bankLsb <= 9 &&
                c.program <= 9 && (e.a <= 12 || (e.a >= 92 && e.a <= 110))) {
                startAudio_(e.ch, e.a, noteVelocity, e.noteId);
                break;
            }
            const ParsedVoice* rv = resolveVoice_(e.ch, e.a);
            const ParsedVoice* missingRom = nullptr;
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
                if(rv->valid && rv->pcm.rom && c.modernTimbre) missingRom=rv;
                rv = nullptr;
            }
            ++diagnostics_.fmNotes;
            if (!rv) ++diagnostics_.fallbackNotes;
            // A rhythm channel OR the explicit custom drum bank gets percussion,
            // even when its MTR channel type is "no care". Missing ROM drums
            // must not become a pitched piano after the PCM binding is dropped.
            // a rhythm-channel note with no bound voice gets a percussion hit,
            // never a melodic gm patch (the "drums play piano" class).
            const FmVoicePatch* romPatch=missingRom?FmVoicePatch::romApprox(missingRom->pcm.waveId):nullptr;
            FmVoicePatch patch = rv ? rv->patch
                : romPatch ? *romPatch
                : (c.rhythm || c.drum || c.bankMsb == 125) ? FmVoicePatch::drumApprox(e.a)
                : c.modernTimbre ? FmVoicePatch::gmModernApprox(c.program)
                                : FmVoicePatch::gmApprox(c.program);
            patch.modernTimbre=c.modernTimbre;
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
            // Retain the trigger key and noteId for voice lookup / scheduled
            // key-off. Only the selected key-control policy uses DrumKey.
            int midi = (rv && c.fixedFmDrumPitch && rv->fixedFmNote >= 0
                        ? rv->fixedFmNote : soundingNote) + patch.noteShift;
            double baseFreq=noteToFreq_(midi);
            if(romPatch && missingRom->pcm.waveId<22) {
                // Mapped drum keys are addresses, not pitches (e.g. crash on 90).
                // Designer reference tuning, NOT measured ROM native Fs/pitch.
                const int id=missingRom->pcm.waveId;
                midi=id==0?36:id==2?48:id==14||id==15||id==16?57:60;
                baseFreq=noteToFreq_(midi)*(double(missingRom->pcm.fs)/16000.0);
            }
            poolBaseFreq_[slot]=baseFreq;
            double freq = baseFreq * std::pow(2.0, c.bend / 12.0);
            pool_[slot].channel = e.ch;
            pool_[slot].note_   = midi;                 // sounding pitch (for bend)
            poolNoteId_[slot] = e.noteId;
            pool_[slot].setVolume(c.volume * c.expression);
            // gm-style squared velocity curve: linear velocity made every mid-
            // velocity note nearly full-scale and the mix bus pump the limiter.
            pool_[slot].noteOn(patch, freq, noteVelocity);
            pool_[slot].setVibratoScale(c.squaredControllers ? c.vibratoScale : 1.0);
            // PE is a flag, not a nonzero-pan test: explicit centre must also
            // ignore channel Pan. A missing ROM waveform does not discard its
            // independently defined authored pan when selecting a substitute.
            poolPanLocked_[slot] = missingRom ? missingRom->pcm.panEnabled : patch.panEnabled;
            poolPan_[slot] = poolPanLocked_[slot]
                ? (missingRom ? missingRom->pcm.pan : patch.panDefault) : c.pan;
            channelPanGains(poolPan_[slot], c.squaredControllers && !poolPanLocked_[slot],
                            poolPanLeft_[slot], poolPanRight_[slot]);
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
            l += s * kMixGain * poolPanLeft_[i]; r += s * kMixGain * poolPanRight_[i];
        }
        for (int i = 0; i < kPcmPool; ++i) {           // sampled drum/pcm voices
            if (!pcmPool_[i].active) continue;
            float s = pcmPool_[i].tick();
            l += s * kMixGain * pcmPool_[i].panLeft; r += s * kMixGain * pcmPool_[i].panRight;
        }
        for (auto& voice : audioPool_) {
            if (!voice.active) continue;
            const float s = voice.tick();
            l += s * kMixGain * voice.panLeft;
            r += s * kMixGain * voice.panRight;
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
        if (limEnv_ > 0.92f) {
            ++diagnostics_.limitedFrames;
            float g = 0.92f / limEnv_; l *= g; r *= g;
        }
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
    for (auto& v : voiceTable_) if (v.sequenced) v.activated = false;
    activeVoiceWaveVersion_.fill(-1);
    ended_ = false; diagnostics_ = {};
    // MTR mode/source association is init-time state, not controller state.
    // Preserve it so drum retriggering and Audio binding survive a seek.
    for (auto& c : chans_) {
        bool rh = c.rhythm, mono = c.monophonic, squared = c.squaredControllers, modern=c.modernTimbre;
        bool fixedDrum = c.fixedFmDrumPitch; int track = c.streamTrack;
        c = Chan{}; c.rhythm = rh; c.monophonic = mono; c.streamTrack = track;
        c.squaredControllers = squared;
        c.modernTimbre=modern;
        c.fixedFmDrumPitch=fixedDrum;
        if (squared) c.volume *= c.volume;
    }
    for (auto& v : pool_) { v = FmVoice{}; v.setSampleRate(double(rate_)); }
    for (auto& pv : pcmPool_) pv.active = false;
    for (auto& pv : audioPool_) pv.active = false;
    poolNoteId_.fill(0);
    poolPanLocked_.fill(false);
    fmSteal_ = pcmSteal_ = 0;
    lp1L_ = lp1R_ = lp2L_ = lp2R_ = 0.0f;
    limEnv_ = 0.0f;
}

} // namespace fxchain::smaf
