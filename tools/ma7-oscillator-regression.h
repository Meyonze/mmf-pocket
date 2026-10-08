#pragma once
// Nominal manufacturer-labelled ratios, not a cycle-accurate MA7 phase model.
void checkMa7OscillatorRatios() {
    static constexpr double ratios[16]={0.5,1,2,3,4,5,6,7,8,9,10,1.414,12,1.498,0.891,15};
    FmOpPatch plain{};plain.multi=1;plain.tl=0;plain.ar=15;plain.dr=0;plain.sr=0;
    plain.sl=0;plain.wave=0;
    for(int index=0;index<16;++index) {
        auto native=plain;native.multi=uint8_t(index);native.ma7Multipliers=true;
        FmOperator actual, reference, oldProfile, oldReference;
        actual.configure(native,8000,0,true);reference.configure(plain,8000,0,true);
        actual.noteOn(123);reference.noteOn(123*ratios[index]);
        oldProfile.configure(native,8000);native.ma7Multipliers=false;
        oldReference.configure(native,8000);oldProfile.noteOn(123);oldReference.noteOn(123);
        float peak=0;
        for(int sample=0;sample<2048;++sample) {
            const float value=actual.tick(0);peak=std::max(peak,std::fabs(value));
            check(std::isfinite(value) && std::fabs(value-reference.tick(0))<1e-6f,
                  "MA7 multiplier follows manufacturer nominal ratio, including fractional slots");
            check(oldProfile.tick(0)==oldReference.tick(0),"HandyPhone profile does not acquire MA7 fractional mapping");
        }
        check(peak>.1f,"all MA7 nominal multiplier selections remain audible");
    }
    std::vector<uint8_t> base{0x43,0x79,8,0x7f,0x21,0,0,0,0,0,0,0,1,0};
    const std::vector<uint8_t> op{0,0xf0,0xf0,0xfc,0,0,0,0,0,0x10};
    base.insert(base.end(),op.begin(),op.end());
    auto carrier=op;carrier[3]=0;carrier[6]=16<<3;
    base.insert(base.end(),carrier.begin(),carrier.end());
    for(int index=0;index<16;++index) for(int dt=0;dt<16;++dt) {
        auto image=base;image[33]=uint8_t((index<<4)|dt);
        auto parsed=parseVoiceExclusive(image.data(),image.size());
        check(parsed.valid && parsed.patch.ops[1].ma7Multipliers &&
              !parsed.patch.ops[1].ma35Multipliers && parsed.patch.ops[1].multi==index &&
              parsed.patch.ops[1].ma7Detune==dt,"expanded MA7 MULT and four raw DT bits stay independent");
        check(parsed.patch.ops[1].dt==(dt&7),"uncalibrated MA7 detune DSP is not silently changed");
    }
    for(int index:{11,13,14}) {
        auto image=base;image[33]=uint8_t(index<<4);
        auto native=fixture(3);setVoice(native,image);
        native.tracks[0].sequenceData={0,0x10,60,100,10,10,0xff,0x2f,0};
        const auto sound=renderAll(native);
        check(sound==renderAll(native,1) && sound==renderAll(native,1024),"MA7 fractional ratio is block independent");
        // Score format is not the voice family: the same native expanded tone
        // remains MA7 when carried by Mobile events (channel volume differs).
        auto parsed=parseVoiceExclusive(image.data(),image.size());
        check(parsed.patch.ops[1].ma7Multipliers,"native tone family remains explicit independent of sequence format");
        auto wrong=image;wrong[2]=7; // unknown old-family command, must not bind as MA7
        auto rejected=parseVoiceExclusive(wrong.data(),wrong.size());
        check(!rejected.valid,"expanded body is not guessed as another command family");
    }
    check(!FmVoicePatch::romApprox(0)->ops[0].ma7Multipliers &&
          !FmVoicePatch::gmModernApprox(0).ops[0].ma7Multipliers,
          "original ROM and GM recipes do not receive MA7 hardware multiplier semantics");
}
