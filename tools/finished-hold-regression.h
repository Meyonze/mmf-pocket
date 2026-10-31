// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original state/allocation fixtures; no envelope timing or timbre retuning.
namespace finishedHoldRegression {
void envelopeState() {
    for (bool sustaining : {false, true}) {
        FmOpPatch patch;
        patch.holdEnabled = true;
        patch.egType = sustaining;
        patch.dr = sustaining ? 0 : 15;
        patch.sl = sustaining ? 0 : 4;
        patch.sr = patch.rr = 15;
        FmEnvelope actual, natural;
        actual.configure(patch, 8000); natural.configure(patch, 8000);
        actual.keyOn(); natural.keyOn();
        for (int i = 0; i < 2; ++i) check(actual.advance() == natural.advance(), "held envelope initial state");
        actual.keyOff(true);
        check(actual.keyOffDeferred(), "fixture defers keyoff before natural completion");
        for (int i = 0; i < 1000; ++i)
            check(actual.advance() == natural.advance(), "deferred keyoff preserves natural envelope samples");
        check(actual.isFinished() && !actual.keyOffDeferred(), "finished sustain/release clears deferred keyoff");
        actual.setHold(false);
        check(actual.isFinished() && actual.advance() == 0, "pedal release cannot revive finished envelope");
    }
}

void topologies() {
    constexpr unsigned carriers[8] = {2, 3, 15, 8, 8, 10, 9, 13};
    for (int algorithm = 0; algorithm < 8; ++algorithm) {
        FmVoicePatch patch;
        patch.algorithm = uint8_t(algorithm); patch.fourOp = algorithm >= 2;
        const int operators = patch.fourOp ? 4 : 2;
        int fast = -1, other = -1;
        for (int i = 0; i < operators; ++i) {
            patch.ops[i].holdEnabled = true;
            if (carriers[algorithm] & (1u << i)) {
                if (fast < 0) fast = i;
                else {
                    other = i;
                    patch.ops[i].holdEnabled = false;
                    patch.ops[i].rr = 8;
                }
            }
        }
        patch.ops[fast].sr = patch.ops[fast].rr = 15;
        auto referencePatch = patch;
        referencePatch.ops[fast].holdEnabled = false;
        FmVoice actual, reference;
        actual.setSampleRate(8000); reference.setSampleRate(8000);
        actual.noteOn(patch, 220, 1); reference.noteOn(referencePatch, 220, 1);
        for (int i = 0; i < 2; ++i) check(actual.tick() == reference.tick(), "mixed-carrier initial samples");
        actual.noteOff(true); reference.noteOff(true);
        check(!actual.released() && reference.released(), "live deferred carrier still protects voice");
        // At this point SR and RR are identical for the deferred carrier.
        // Its Hold flag changes classification, never the audible sequence.
        for (int i = 0; i < 1000; ++i)
            check(actual.tick() == reference.tick(), "natural held-carrier completion preserves output bits");
        check(actual.released() && reference.released(), "finished carrier no longer protects remaining release tail");
        check(actual.active() == (other >= 0), "only remaining live output carriers retain voice");
        actual.setHold(false); reference.setHold(false);
        for (int i = 0; i < 100; ++i)
            check(actual.tick() == reference.tick(), "later pedal release preserves completed-carrier behaviour");

        actual.noteOn(patch, 440, 0.5f); reference.noteOn(patch, 440, 0.5f);
        actual.setHold(false);
        check(!actual.released(), "retrigger clears completed voice release state");
        for (int i = 0; i < 1000; ++i)
            check(actual.tick() == reference.tick(), "old deferred gate cannot release replacement note");

        if (other >= 0) {
            patch.ops[other].holdEnabled = true;
            actual.noteOn(patch, 220, 1);
            actual.tick(); actual.tick(); actual.noteOff(true);
            for (int i = 0; i < 1000; ++i) actual.tick();
            check(actual.active() && !actual.released(), "another genuinely held carrier retains protection");
            actual.setHold(false);
            check(actual.released(), "pedal-off releases remaining live deferred carrier");
        }
    }
}

SmafFile allocationFixture(bool deferred, bool fullPool) {
    auto file = fixture();
    auto& track = file.tracks[0];
    track.durationTimeBase = track.gateTimeBase = 0; // independently known 1 ms ticks
    setVoice(file, protocolRegression::tone(1));
    auto mixed = protocolRegression::tone(1);
    mixed[7] = 1; // a separate program for the two-carrier release tail
    mixed[13] = uint8_t(0xf0 | (deferred ? 2 : 0)); // SR15, optional Hold response
    mixed[21] = 0x80; // other carrier: RR8, DR0, no Hold response
    const auto parsed = parseVoiceExclusive(mixed.data(), mixed.size());
    check(parsed.valid && parsed.patch.ops[0].sr == 15 && parsed.patch.ops[0].rr == 15
          && parsed.patch.ops[0].holdEnabled == deferred && parsed.patch.ops[1].rr == 8,
          "allocation fixture really defines a naturally finishing deferred carrier");
    appendExclusive(file, mixed);
    auto& sequence = track.sequenceData;
    sequence = {0,0xb1,64,127, 0,0x90,60,25,100, 0,0xc1,1, 0,0x91,64,25,1};
    if (fullPool) {
        for (int i = 0; i < 30; ++i)
            sequence.insert(sequence.end(), {0,0x90,uint8_t(65+i),25,100});
    }
    sequence.insert(sequence.end(), {20,0x92,72,25,20, 100,0xff,0x2f,0});
    return file;
}

std::vector<float> drain(MaPlayer& player, int block) {
    std::vector<float> result, buffer(size_t(block) * 2);
    int count;
    while ((count = player.render(buffer.data(), block)) > 0) {
        result.insert(result.end(), buffer.begin(), buffer.begin() + count * 2);
        check(result.size() < 40000, "finished-carrier allocation fixture ends naturally");
    }
    check(!player.diagnostics().tailLimitReached, "allocation fixture does not use safety cutoff");
    return result;
}

void allocation() {
    for (bool fullPool : {false, true}) {
        const auto actualFile = allocationFixture(true, fullPool);
        const auto referenceFile = allocationFixture(false, fullPool);
        const auto expected = renderAll(referenceFile);
        for (int block : {1, 257, 1024}) {
            MaPlayer player;
            check(player.init(actualFile, 8000), "finished-carrier pool fixture initializes");
            check(drain(player, block) == expected,
                  "completed held carrier allows tail-first steal without cutting unrelated held melody");
            check(player.diagnostics().stolenFm == (fullPool ? 1u : 0u)
                  && player.diagnostics().stolenHeldFm == 0,
                  "only released tail is stolen when the FM pool is full");
            if (block == 257) {
                player.seekToStart();
                check(drain(player, block) == expected && player.diagnostics().stolenHeldFm == 0,
                      "seek resets deferred completion and allocator state");
            }
        }
    }
}

void test() { envelopeState(); topologies(); allocation(); }
} // namespace finishedHoldRegression
