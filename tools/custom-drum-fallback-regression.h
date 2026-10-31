// SPDX-License-Identifier: Apache-2.0
// Original MA5 long-form voice/bulk bytes; no injected player metadata or ROM.
namespace customDrumFallback {
SmafFile score(int format,int bank,int drumNote,bool rom,int waveId,bool present,int note=60) {
    auto file=fixture(format);
    file.tracks[0].channelStatus={0x81}; // melody, KeyControl ON
    file.tracks[0].sequenceData={0,0xb0,0,uint8_t(bank),0,0xc0,0,
        0,0x90,uint8_t(note),100,10,10,0xff,0x2f,0};
    const std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,uint8_t(bank),0,0,uint8_t(drumNote),1,
        0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,uint8_t((rom?128:0)|waveId)};
    const auto parsed=parseVoiceExclusive(voice.data(),voice.size());
    check(parsed.valid&&parsed.isPcm&&parsed.key.drumNote==drumNote,
          "custom drum uses a supported parsed PCM definition");
    setVoice(file,voice);
    if(present) {
        std::vector<uint8_t> bulk={0x43,0x79,7,0x7f,3,uint8_t(waveId),2};
        for(int i=0;i<80;++i)bulk.push_back(uint8_t(128+int(40*std::sin(i*6.283185307179586/20))));
        appendExclusive(file,bulk);
    }
    return file;
}
void checkRoutes() {
    for(int format:{2,3})for(int bank:{0,124}) {
        for(bool rom:{false,true}) {
            auto custom=score(format,bank,60,rom,63,false);
            auto rhythm=custom;rhythm.tracks[0].channelStatus={0x83};
            auto melodic=score(format,bank,0,rom,63,false);
            const auto actual=renderAll(custom);
            check(actual==renderAll(rhythm),"missing custom drum wave retains percussion without a rhythm channel flag");
            check(actual!=renderAll(melodic),"missing custom drum wave cannot become a melodic piano");
            check(std::any_of(actual.begin(),actual.end(),[](float x){return std::fabs(x)>1e-4f;}),
                "custom drum fallback produces nonzero output");
            auto unmatched=score(format,bank,61,rom,63,false);
            auto absent=unmatched;absent.tracks[0].setupData.clear();
            check(renderAll(unmatched)==renderAll(absent),"unmatched drum key does not change generic melodic fallback");
        }
        for(int wave:{0,22,26}) {
            auto custom=score(format,bank,60,true,wave,false);
            auto melodic=score(format,bank,0,true,wave,false);
            auto rhythm=custom;rhythm.tracks[0].channelStatus={0x83};
            check(renderAll(custom)==renderAll(melodic)&&renderAll(custom)==renderAll(rhythm),
                "known ROM recipe keeps priority over drum/melody classification");
        }
        auto bound=score(format,bank,60,false,1,true);
        auto upper=score(format,bank,72,false,1,true,72);
        check(renderAll(bound)==renderAll(upper),"bound custom RAM drum keeps native sample rate at different trigger keys");
        auto melody=score(format,bank,0,false,1,true);
        auto melodyUpper=score(format,bank,0,false,1,true,72);
        check(renderAll(melody)!=renderAll(melodyUpper),"bound melodic RAM still follows score pitch");
    }
}
} // namespace customDrumFallback
