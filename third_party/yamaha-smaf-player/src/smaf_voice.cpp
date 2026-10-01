// SPDX-License-Identifier: Apache-2.0
// Modified by MMF Pocket contributors in 2026. See docs/audio-fidelity.md.
//
// smaf_voice.cpp -- the VM35 / VMA exclusive -> FmVoicePatch decode.
// ----------------------------------------------------------------------------
// the tricky bit is the MA-3 "packed" form: yamaha stole the top bit of a few
// fields and stashed them in two carrier bytes per operator to save room. we
// un-pack those first, then both MA-3 and MA-5 share the same 3-global + 7/op
// layout. the MA-1/2 (VMA) form is a smaller 2-global + 5/op layout that we
// widen into the same struct.

#include "smaf_voice.h"
#include <algorithm>

namespace fxchain::smaf {

namespace {

// basic-octave code -> semitone transpose. BO 0 = +12, 1 = 0, 2 = -12, 3 = -24.
int boToSemitones(int bo) {
    switch (bo & 3) { case 0: return 12; case 1: return 0; case 2: return -12; default: return -24; }
}

// panpot 0..31 -> -1..+1 (0..14 = left, 15 = centre, 16..31 = right).
float panpotToPan(int pp) {
    pp &= 31;
    if (pp == 15) return 0.0f;
    if (pp < 15)  return -(15 - pp) / 15.0f;
    return (pp - 15) / 16.0f;
}

// the fm core (FmVoice::tick) implements ALL 8 smaf/YMF825 connection algorithms
// directly, so pass the real algorithm straight through. alg 0/1 are 2-op, 2..7
// are 4-op. (an earlier version collapsed 2..7 onto four topologies, which threw
// the routing information away and made every real 4-op voice mis-time.)
void applyAlg(FmVoicePatch& v, int alg) {
    alg &= 7;
    v.fourOp = (alg >= 2);
    v.algorithm = uint8_t(alg);
}

// apply the shared 3-global + 7-byte/op VM35 body starting at g[0].
void applyVm35Body(const uint8_t* g, size_t n, int operatorCount, FmVoicePatch& v) {
    if (n < 3) return;
    int bo  = g[1] & 0x03;
    int pp  = (g[1] >> 3) & 0x1f;
    bool pe = (g[2] >> 5) & 1;
    int alg = g[2] & 0x07;
    applyAlg(v, alg);
    v.noteShift = boToSemitones(bo);
    v.panDefault = pe ? panpotToPan(pp) : 0.0f;
    v.lfo = (g[2] >> 6) & 0x03;            // voice LFO rate (vm35fm.go Global+2)

    const uint8_t* op = g + 3;
    int count = std::clamp(operatorCount, 1, 4);
    for (int i = 0; i < count; ++i) {
        if (size_t(op - g) + 7 > n) break;
        FmOpPatch& o = v.ops[i];
        o.sr  =  (op[0] >> 4) & 0x0f;
        o.ksr =   op[0] & 0x01;
        // SR=0 holds after decay. Do not reinterpret it as a percussive flag.
        // SUS (op+0 bit1) remains unimplemented; XOF is handled separately.
        o.egType = true; // VM35 always enters SR stage; SR=0 holds the level.
        o.rr  =  (op[1] >> 4) & 0x0f;
        o.dr  =   op[1] & 0x0f;
        o.ar  =  (op[2] >> 4) & 0x0f;
        o.sl  =   op[2] & 0x0f;
        o.tl  =  (op[3] >> 2) & 0x3f;
        o.ksl =   op[3] & 0x03;
        o.xof = ((op[0] >> 3) & 1) != 0;   // ignore key-off (drums ring out)
        o.am  =  (op[4] >> 4) & 1;         // EAM
        o.dam = uint8_t((op[4] >> 5) & 3); // tremolo depth
        o.vib =   op[4] & 1;               // EVB
        o.dvb = uint8_t((op[4] >> 1) & 3); // vibrato depth
        o.multi = (op[5] >> 4) & 0x0f;
        o.dt  =   op[5] & 0x07;
        o.wave =  (op[6] >> 3) & 0x1f;
        o.fb   =   op[6] & 0x07;            // feedback is PER-OPERATOR on YMF825
        if (i == 0) v.feedback = o.fb;      // also expose op0's fb on the header
        op += 7;
    }
    // 2-op voices leave ops[2..3] at defaults (harmless; core only ticks 2).
}

// un-pack the MA-3 (VM3Exclusive) form into the plain VM35 body, byte-for-byte
// per go-smaf voice/vm35fm.go Read(). the packed data is 36 bytes (4 global +
// 8 per op). carriers live at raw[op*8] (stealing SR/RR/AR/TL bit7) and at
// raw[8+op*8] (stealing MUL/WS bit7); raw[0] doubles as the global carrier.
// after restoring the stolen bits we splice out the carriers to build the
// standard 3-global + 7/op stream and hand it to applyVm35Body.
void applyMa3Packed(const uint8_t* body, size_t n, int operatorCount, FmVoicePatch& v) {
    uint8_t raw[36] = {0};
    size_t copy = (n < 36) ? n : 36;
    for (size_t i = 0; i < copy; ++i) raw[i] = body[i];

    raw[2] = uint8_t(raw[2] | ((raw[0] << 2) & 0x80));   // PANPOT bit7
    raw[3] = uint8_t(raw[3] | ((raw[0] << 3) & 0x80));   // ALG-area bit7
    for (int op = 0; op < 4; ++op) {
        raw[4 + op*8]  = uint8_t(raw[4 + op*8]  | ((raw[op*8]     << 4) & 0x80)); // SR
        raw[5 + op*8]  = uint8_t(raw[5 + op*8]  | ((raw[op*8]     << 5) & 0x80)); // RR
        raw[6 + op*8]  = uint8_t(raw[6 + op*8]  | ((raw[op*8]     << 6) & 0x80)); // AR
        raw[7 + op*8]  = uint8_t(raw[7 + op*8]  | ((raw[op*8]     << 7) & 0x80)); // TL
        raw[10 + op*8] = uint8_t(raw[10 + op*8] | ((raw[8 + op*8] << 2) & 0x80)); // MUL
        raw[11 + op*8] = uint8_t(raw[11 + op*8] | ((raw[8 + op*8] << 3) & 0x80)); // WS
    }
    // fixed = raw[1:4] (3 global) + per op raw[4+op*8:8+op*8] + raw[9+op*8:12+op*8]
    uint8_t fixed[3 + 7*4];
    fixed[0] = raw[1]; fixed[1] = raw[2]; fixed[2] = raw[3];
    size_t w = 3;
    for (int op = 0; op < 4; ++op) {
        fixed[w++] = raw[4 + op*8]; fixed[w++] = raw[5 + op*8];
        fixed[w++] = raw[6 + op*8]; fixed[w++] = raw[7 + op*8];
        fixed[w++] = raw[9 + op*8]; fixed[w++] = raw[10 + op*8];
        fixed[w++] = raw[11 + op*8];
    }
    applyVm35Body(fixed, w, operatorCount, v);
}

// operator count from the algorithm nibble (alg 0-1 = 2op, 2-7 = 4op).
int opCountFromAlg(int alg) { return (alg & 7) <= 1 ? 2 : 4; }

bool applyPcmBody(const uint8_t* b, size_t n, ParsedVoice& out) {
    if (!b || n < 16) return false;
    PcmParams& pc = out.pcm;
    pc.fs     = (int(b[0]) << 8) | b[1];
    pc.panEnabled = (b[2] & 1) != 0;
    pc.pan = panpotToPan(b[2] >> 3);
    pc.env.tl = (b[7] >> 2) & 0x3f;
    pc.env.sr = (b[4] >> 4) & 0x0f;
    pc.env.rr = (b[5] >> 4) & 0x0f;
    pc.env.dr =  b[5] & 0x0f;
    pc.env.ar = (b[6] >> 4) & 0x0f;
    pc.env.sl =  b[6] & 0x0f;
    pc.env.egType = true;
    pc.env.xof = (b[4] & 8) != 0;
    // VM35 PCM has its own authored EAM/EVB and depth fields. These were
    // discarded even when the referenced wave decoded correctly.
    pc.lfo = (b[3] >> 6) & 3;
    pc.env.am = (b[8] & 0x10) != 0;
    pc.env.dam = (b[8] >> 5) & 3;
    pc.env.vib = (b[8] & 1) != 0;
    pc.env.dvb = (b[8] >> 1) & 3;
    pc.loopPt = (int(b[11]) << 8) | b[12];
    pc.endPt  = (int(b[13]) << 8) | b[14];
    pc.rom    = (b[15] & 0x80) != 0;
    pc.loop   = pc.loopPt < pc.endPt;
    pc.waveId =  b[15] & 0x7f;
    return pc.fs >= 1500 && pc.fs <= 48000;
}

void readAnalogLite(const uint8_t* tail, AnalogLiteParams& al) {
    al.present = true;
    al.resonance = tail[0] & 31;
    al.depth = tail[1] >> 5;
    al.mode = (tail[1] >> 4) & 1;
    al.reset = (tail[1] & 8) != 0; // NOT an enable bit
    al.frequency = tail[1] & 7;
    for (int i = 0; i < 5; ++i)
        al.cutoff[i] = uint16_t(((tail[2 + i * 2] & 31) << 8) | tail[3 + i * 2]);
    for (int i = 0; i < 4; ++i) al.rates[i] = tail[12 + i] & 31;
    al.xof = (tail[12] & 128) != 0;
    al.sus = (tail[13] & 128) != 0;
    al.keyFollow = (tail[14] & 128) != 0;
    al.vsl = (tail[15] & 128) != 0;
}

} // namespace

std::vector<uint8_t> unpackMa3Bytes(const uint8_t* p, size_t n, size_t maxOutput) {
    std::vector<uint8_t> out;
    if (!p || !n || n > maxOutput + (maxOutput + 6) / 7) return out;
    out.reserve(std::min(n, maxOutput));
    size_t i = 0;
    while (i < n) {
        const uint8_t mask = p[i++];
        if (mask & 0x80 || i == n) return {};
        for (int bit = 6; bit >= 0 && i < n; --bit) {
            if (p[i] & 0x80 || out.size() >= maxOutput) return {};
            out.push_back(uint8_t(p[i++] | (((mask >> bit) & 1) << 7)));
        }
    }
    return out;
}

ParsedVoice parseVoiceExclusive(const uint8_t* p, size_t n) {
    ParsedVoice out;
    out.patch = FmVoicePatch::defaultPatch();
    if (!p || n < 5) return out;
    if (p[0] != 0x43) return out;   // not a yamaha maker id

    // MA-7 Format 3 tone setting: 43 79 08 7F 21 followed by
    // bankMSB, bankLSB, program, note/split, key-high, then the expanded
    // MA-7 register image. The six exact image shapes below were validated
    // over the local corpus and match Yamaha's 2-op/4-op/WT + AL layouts.
    // AL's filter tail remains unsupported. The oscillator and rate nibbles
    // fold into the VM35 representation; WT's extra rate bits stay separate.
    if (n >= 11 && p[1] == 0x79 && p[2] == 0x08 && p[3] == 0x7f && p[4] == 0x21) {
        out.key.bankMSB = p[5];
        out.key.bankLSB = p[6];
        out.key.pc = p[7];
        out.key.drumNote = (p[5] == 0x7d) ? p[8] : 0;
        out.keyHigh = (p[5] == 0x7d || p[9] == 0) ? 127 : p[9];
        const uint8_t* voice = p + 10;
        const size_t vn = n - 10;
        const int flags = voice[0];

        int operators = 0;
        if (flags == 0 && vn == 24) operators = 2;
        else if (flags == 0 && vn == 44) operators = 4;
        else if (flags == 2 && vn == 40) operators = 2;
        else if (flags == 2 && vn == 60) operators = 4;
        if (operators) {
            uint8_t vm35[3 + 7 * 4]{};
            vm35[0] = voice[1];
            vm35[1] = voice[2];
            vm35[2] = voice[3];
            for (int op = 0; op < operators; ++op) {
                const size_t src = 4 + size_t(op) * 10;
                const size_t dst = 3 + size_t(op) * 7;
                vm35[dst]     = voice[src];
                vm35[dst + 1] = voice[src + 1];
                vm35[dst + 2] = voice[src + 2];
                vm35[dst + 3] = voice[src + 3];
                vm35[dst + 4] = voice[src + 4];
                vm35[dst + 5] = voice[src + 9];
                vm35[dst + 6] = voice[src + 6];
            }
            applyVm35Body(vm35, 3 + size_t(operators) * 7, operators, out.patch);
            for (int op = 0; op < operators; ++op) {
                const size_t src = 4 + size_t(op) * 10;
                auto& target = out.patch.ops[op];
                target.rateLowBits = voice[src + 5] & 15;
                target.fixedFrequency = (voice[src] & 4) != 0;
                target.fixedBlock = (voice[src + 7] >> 2) & 7;
                target.fixedFnum = uint16_t(((voice[src + 7] & 3) << 8) | voice[src + 8]);
            }
            if (flags == 2) readAnalogLite(voice + 4 + operators * 10, out.al);
            if (out.key.bankMSB == 125 && vm35[0] <= 127) out.fixedFmNote = vm35[0];
            out.valid = true;
            return out;
        }

        // An observed MA-7 WT+AL+Pitch-EG image adds nine bytes after the
        // 34-byte WT+AL body. Only its all-zero extension is supported here;
        // nonzero pitch envelopes must not silently become a static patch.
        const bool neutralPitchEg = flags == 7 && vn == 43 &&
            std::all_of(voice + 34, voice + 43, [](uint8_t b) { return b == 0; });
        const bool wt = (flags == 1 && vn == 18) || (flags == 3 && vn == 34) || neutralPitchEg;
        if (wt) {
            uint8_t vm35[16]{};
            std::copy(voice + 1, voice + 10, vm35);
            std::copy(voice + 11, voice + 17, vm35 + 9);
            vm35[15] = voice[(flags == 1) ? 17 : 33];
            out.isPcm = true;
            out.pcm.voiceWaveOnly = true;
            out.valid = applyPcmBody(vm35, sizeof(vm35), out);
            // Expanded MA-7 WT AEG: RR/SR/DR/AR are 5-bit values, with
            // their low bits in voice[10], not extra high bits. Retaining
            // these avoids rounding every odd rate down to a legacy rate.
            out.pcm.env.rateLowBits = voice[10] & 0x0f;
            if (flags != 1) readAnalogLite(voice + 17, out.al);
            return out;
        }
        return out;
    }

    // MA-3 / MA-5 long form: 43 79 06|07 7F 01 [bank...] body
    if (n >= 11 && p[1] == 0x79 && (p[2] == 0x06 || p[2] == 0x07) && p[3] == 0x7f && p[4] == 0x01) {
        out.key.bankMSB = p[5]; out.key.bankLSB = p[6]; out.key.pc = p[7];
        out.key.drumNote = p[8];
        int voiceType = p[9];
        // Type 2 is Analog Lite, NOT PCM. Unknown voice bodies must not be
        // interpreted as PCM sample offsets and envelopes.
        if (voiceType > 1) return out;
        if (voiceType == 1) {                        // PCM (sampled) voice
            out.isPcm = true;
            // User instrument RAM and Mwa audio samples have separate IDs.
            // Retain the pre-existing legacy bank-0 fixture/compatibility path.
            out.pcm.voiceWaveOnly = p[5] == 124 || p[5] == 125;
            const uint8_t* b = p + 10; size_t bn = n - 10;
            // MA-3 PCM uses the same 7-bit transport packing as FM. Reading
            // its masks as parameters corrupted Fs, TL, envelopes and WaveID.
            std::vector<uint8_t> unpacked;
            if (p[2] == 0x06) {
                unpacked = unpackMa3Bytes(b, bn, 32);
                b = unpacked.data(); bn = unpacked.size();
            }
            out.valid = applyPcmBody(b, bn, out);
            return out;
        }
        const uint8_t* body = p + 10;
        size_t bn = n - 10;
        // we need the algorithm to know the op count. in the MA-5 DIRECT form the
        // alg lives in global body byte 2. in the MA-3 PACKED form byte 2 is still
        // PANPOT and the alg sits in byte 3 (its low 3 bits survive the bit-steal,
        // per vm35fm.go); peek the right byte or we truncate 4-op voices to 2.
        size_t algByte = (p[2] == 0x06) ? 3 : 2;
        int alg = (bn > algByte) ? (body[algByte] & 0x07) : 0;
        int ops = opCountFromAlg(alg);
        if (p[2] == 0x06) applyMa3Packed(body, bn, ops, out.patch);   // MA-3 packed
        else              applyVm35Body(body, bn, ops, out.patch);    // MA-5 direct
        // Global DrumKey is the oscillator pitch, not the header's mapped key.
        // MA-3's carrier precedes it; the sounding key itself is seven-bit.
        const size_t drumKeyByte = p[2] == 0x06 ? 1 : 0;
        if (out.key.bankMSB == 125 && bn > drumKeyByte && body[drumKeyByte] <= 127)
            out.fixedFmNote = body[drumKeyByte];
        out.valid = true;
        return out;
    }

    // MA-5 short form: 43 05 01 [bankLSB] [pc] body
    if (n >= 6 && p[1] == 0x05 && p[2] == 0x01) {
        out.key.bankMSB = 0; out.key.bankLSB = p[3]; out.key.pc = p[4];
        const uint8_t* body = p + 5; size_t bn = n - 5;
        int alg = (bn >= 3) ? (body[2] & 0x07) : 0;
        applyVm35Body(body, bn, opCountFromAlg(alg), out.patch);
        out.valid = true;
        return out;
    }

    // MA-1 / MA-2 (VMA) form. header per go-smaf vmapc.go: 43 03 [Enigma][Bank][PC]
    // then the voice body (2 global + 5 bytes/op). the note stream selects the
    // voice by (Bank, PC), so getting these offsets right is what makes the file's
    // real instruments bind instead of falling back to the gm approximation.
    if (n >= 7 && p[1] == 0x03) {
        out.key.bankMSB = 0;
        out.key.bankLSB = p[3] & 0x7f;    // Bank (bit7 = drum bank, masked off here)
        out.key.pc      = p[4];           // PC
        const uint8_t* g = p + 5; size_t gn = n - 5;      // body starts after the header
        if (gn >= 2) {
            int fb  = (g[0] >> 3) & 0x07;
            int alg = g[0] & 0x07;         // g[1] = the 0x01 enigma constant
            applyAlg(out.patch, alg);
            out.patch.feedback = uint8_t(fb);
            out.patch.lfo = uint8_t((g[0] >> 6) & 3);   // voice LFO rate
            int count = opCountFromAlg(alg);
            const uint8_t* op = g + 2;
            for (int i = 0; i < count; ++i) {
                if (size_t(op - g) + 5 > gn) break;
                FmOpPatch& o = out.patch.ops[i];
                // VMAFMOperator.Read (vmafm.go): +0 MULT|VIB(b3)|EGT(b2)|SUS(b1)|KSR(b0)
                o.multi = (op[0] >> 4) & 0x0f;
                o.vib   = (op[0] & 0x08) != 0;   // VIB  (bit3)
                bool egt = (op[0] & 0x04) != 0;  // EGT  (bit2)
                o.ksr   = (op[0] & 0x01);
                const uint8_t rawRr = (op[1] >> 4) & 0x0f;
                const bool sus = (op[0] & 0x02) != 0;
                o.rr = sus ? 6 : rawRr;
                o.dr = op[1] & 0x0f;
                o.ar = (op[2] >> 4) & 0x0f; o.sl = op[2] & 0x0f;
                o.tl = (op[3] >> 2) & 0x3f; o.ksl = op[3] & 0x03;
                // +4: DVB(b7-6) | DAM(b5-4) | AM(b3) | WS(b2-0)
                o.am   = (op[4] & 0x08) != 0;
                o.dvb  = uint8_t((op[4] >> 6) & 3);
                o.dam  = uint8_t((op[4] >> 4) & 3);
                o.wave = op[4] & 0x07;
                // MA-1/2 SUS changes release to rate 6 at the authored sound
                // length. Keep the raw RR for the damped pre-key-off stage;
                // only the release phase receives the SUS override.
                o.sr = egt ? 0 : rawRr;
                o.egType = true; // EGT already converted into SR above.
                if (i == 0) o.fb = uint8_t(fb);   // VMA feedback lands on op0
                op += 5;
            }
            out.valid = true;
        }
        return out;
    }

    return out;   // unknown yamaha sub-form
}

} // namespace fxchain::smaf
