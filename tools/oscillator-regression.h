#pragma once
// Synthetic operator tests; no sampled sounds or third-party implementation.
void checkOscillatorCorrections() {
    FmOpPatch p{};p.ar=15;p.dr=0;p.sr=0;p.sl=0;p.tl=0;p.multi=1;p.wave=30;
    FmOperator modern, legacy;
    modern.configure(p,8000,0,true);legacy.configure(p,8000,0,false);
    modern.noteOn(1000);legacy.noteOn(1000);
    check(modern.tick(0)==1 && legacy.tick(0)==1,"WS30 first quarter is high in both profiles");
    check(modern.tick(0)==0 && legacy.tick(0)==1,"modern WS30 ends pulse at quarter cycle");
    check(modern.tick(0)==0 && legacy.tick(0)==1,"WS30 is not a duplicate of half-square");
    for(int wave=0;wave<32;++wave) if(wave!=30) {
        p.wave=uint8_t(wave);modern.configure(p,8000,0,true);legacy.configure(p,8000);
        modern.noteOn(137);legacy.noteOn(137);
        for(int i=0;i<512;++i) check(modern.tick(0)==legacy.tick(0),"other waveform math is unchanged");
    }
    const std::vector<uint8_t> base{0x43,0x79,7,0x7f,1,0,0,0,0,0,
        0,1,0,0,0xf0,0xf0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,0};
    for(auto pair:{std::pair<int,int>{11,10},{13,12},{14,15}}) {
        auto make=[&](int multiplier,bool packed=false) {
            auto f=fixture();auto image=base;image[25]=uint8_t(multiplier<<4);
            if(packed) {
                // FM bit-carriers have a fixed register layout, unlike PCM
                // transport's generic seven-byte group packing.
                image=vm35_modulation_test::packFm(image);
            }
            setVoice(f,image);f.tracks[0].sequenceData={0,0x90,60,100,10,10,0xff,0x2f,0};return f;
        };
        const auto actual=renderAll(make(pair.first));
        check(actual==renderAll(make(pair.second)),"VM35 MULT aliases match documented operator ratios");
        check(actual==renderAll(make(pair.first,true)),"MA3 packed MULT matches MA5 direct interpretation");
        check(actual==renderAll(make(pair.first),1),"multiplier correction is block independent");
        auto direct=base;direct[25]=uint8_t(pair.first<<4);
        auto parsed=parseVoiceExclusive(direct.data(),direct.size());
        check(parsed.valid && parsed.patch.ops[1].ma35Multipliers,"VM35 records its multiplier family");
        std::vector<uint8_t> shortForm{0x43,5,1,0,0};
        shortForm.insert(shortForm.end(),direct.begin()+10,direct.end());
        parsed=parseVoiceExclusive(shortForm.data(),shortForm.size());
        check(parsed.valid && parsed.patch.ops[1].ma35Multipliers,"short MA5 form uses VM35 multiplier family");
    }
    std::vector<uint8_t> expanded{0x43,0x79,8,0x7f,0x21,0,0,0,0,0,0,0,1,0};
    for(int op=0;op<2;++op) {
        const uint8_t* s=base.data()+13+op*7;
        expanded.insert(expanded.end(),{s[0],s[1],s[2],s[3],s[4],0,s[6],0,0,s[5]});
    }
    auto ma7=parseVoiceExclusive(expanded.data(),expanded.size());
    check(ma7.valid && !ma7.patch.ops[0].ma35Multipliers,"MA7 expanded multiplier transport is not VM35 aliases");
    check(!FmVoicePatch::gmModernApprox(0).ops[0].ma35Multipliers &&
          !FmVoicePatch::romApprox(0)->ops[0].ma35Multipliers,"original fallback recipes retain their intended ratios");
    const std::vector<uint8_t> vma{0x43,3,0,0,0,0,1,0,0x0f,0xf0,0,0,0,0x0f,0xf0,0,0};
    auto old=parseVoiceExclusive(vma.data(),vma.size());
    check(old.valid && !old.patch.ops[0].ma35Multipliers,"VMA multiplier interpretation remains separate");
    p.wave=0;p.multi=11;p.ma35Multipliers=true;
    legacy.configure(p,8000);p.ma35Multipliers=false;modern.configure(p,8000);
    legacy.noteOn(100);modern.noteOn(100);
    for(int i=0;i<512;++i) check(legacy.tick(0)==modern.tick(0),"HandyPhone profile preserves ratios even for embedded VM35 tones");
}
