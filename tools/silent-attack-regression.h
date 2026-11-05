// SPDX-License-Identifier: Apache-2.0
#pragma once
// Exact stopped-attack EOF fixtures. No silence threshold or slot retirement.
namespace silentAttackRegression {
using Bytes = std::vector<uint8_t>;
constexpr unsigned carriers[8] = {2,3,15,8,8,10,9,13};

void configuredAttack() {
    for (int rate : {8000,44100,48000}) for (int offset = 0; offset <= 15; ++offset) {
        for (int low : {0,1,2,4,7,8,15}) {
            FmOpPatch patch; patch.ar=0; patch.rateLowBits=uint8_t(low); patch.ksr=1; patch.xof=true;
            FmEnvelope envelope; envelope.configure(patch,rate,offset); envelope.keyOn();
            const bool stopped=(low&8)==0;
            check(envelope.silentForCurrentNote()==stopped,"configured attack respects KSR and MA7 attack low bit");
            for (int i=0;i<64;++i) {
                const float value=envelope.advance();
                check(stopped ? value==0 : value>0,"only a genuinely stopped attack remains exactly zero");
            }
            check(envelope.silentForCurrentNote()==stopped,"other MA7 rate bits cannot change stopped attack");
        }
    }
    FmOpPatch slow; slow.ar=1;
    FmEnvelope envelope; envelope.configure(slow,48000); envelope.keyOn();
    check(!envelope.silentForCurrentNote(),"initial level zero does not silence a slow nonzero attack");
    check(envelope.advance()>0,"slow nonzero attack actually progresses");
}

FmVoicePatch patch(int algorithm,bool stopped) {
    FmVoicePatch result; result.algorithm=uint8_t(algorithm); result.fourOp=algorithm>=2;
    result.modernTimbre=true;
    for (int i=0;i<(result.fourOp?4:2);++i) {
        auto& op=result.ops[i];
        op.ar=stopped&&(carriers[algorithm]&(1u<<i))?0:15;
        op.xof=true; op.ksr=1; op.fb=7; op.wave=0;
    }
    return result;
}

void topologyAndFeedback() {
    for (int algorithm=0;algorithm<8;++algorithm) {
        const auto silent=patch(algorithm,true);
        FmVoice voice; voice.setSampleRate(8000); voice.noteOn(silent,220,1);
        check(voice.active()&&voice.muted(),"all stopped carriers are silent without retiring their slot");
        for (int i=0;i<128;++i) check(voice.tick()==0,"active modulators and feedback cannot bypass zero carrier gain");
        voice.setPitch(440); voice.setVibratoScale(8); voice.setVolume(0); voice.setVolume(1);
        voice.noteOff();
        check(voice.active()&&voice.muted(),"EOF query does not alter XOF, pitch, gain or allocation state");

        int first=0; while (!(carriers[algorithm]&(1u<<first))) ++first;
        auto mixed=silent; mixed.ops[first].ar=15; mixed.ops[first].xof=false; mixed.ops[first].rr=15;
        voice.noteOn(mixed,220,1);
        check(!voice.muted(),"one live carrier prevents EOF mute even before its first attack sample");
        float peak=0;
        for (int i=0;i<128;++i) peak=std::max(peak,std::fabs(voice.tick()));
        check(peak>0,"mixed-carrier fixture is audible");
        voice.noteOff();
        for (int i=0;i<128;++i) voice.tick();
        check(voice.muted(),"finished carrier plus stopped attacks cannot produce a later voice tail");
        for (int i=0;i<16;++i) check(voice.tick()==0,"finished carrier feedback memory adds no output tail");

        // Reuse a voice with nonzero feedback history, then compare a second
        // audible retrigger with a fresh voice. No old filter/history may leak.
        voice.noteOn(silent,220,1);
        for (int i=0;i<128;++i) check(voice.tick()==0,"silent retrigger clears prior audible feedback");
        FmVoice fresh; fresh.setSampleRate(8000);
        voice.noteOn(mixed,220,1); fresh.noteOn(mixed,220,1);
        for (int i=0;i<128;++i) check(voice.tick()==fresh.tick(),"audible retrigger matches fresh feedback state");

        auto odd=silent; odd.ops[first].rateLowBits=8;
        voice.noteOn(odd,220,1);
        check(!voice.muted(),"MA7 raw AR0 with attack low bit remains potentially audible");
        peak=0; for (int i=0;i<128;++i) peak=std::max(peak,std::fabs(voice.tick()));
        check(peak>0,"MA7 minimum odd attack produces output");

        auto onlyModulators=patch(algorithm,false);
        for (int i=0;i<(onlyModulators.fourOp?4:2);++i)
            if (!(carriers[algorithm]&(1u<<i))) onlyModulators.ops[i].ar=0;
        voice.noteOn(onlyModulators,220,1);
        check(!voice.muted(),"silent modulators do not mute live carriers");
        voice.setVolume(1e-12f);
        check(!voice.muted(),"arbitrarily small nonzero gain is not exact silence");
        peak=0; for (int i=0;i<128;++i) peak=std::max(peak,std::fabs(voice.tick()));
        check(peak>0&&peak<1e-7f,"tiny nonzero voice is protected below the filter-drain threshold");
    }
}

Bytes tone(int algorithm,bool stopped,int program=0,bool xof=true,int waveform=0) {
    Bytes result{0x43,0x79,7,0x7f,1,0,0,uint8_t(program),0,0,0,1,uint8_t(algorithm)};
    for (int i=0;i<(algorithm<2?2:4);++i)
        result.insert(result.end(),{uint8_t(xof?9:1),0xf0,
            uint8_t(stopped&&(carriers[algorithm]&(1u<<i))?0:0xf0),0,0,0x10,uint8_t(waveform<<3)});
    const auto decoded=parseVoiceExclusive(result.data(),result.size());
    check(decoded.valid,"silent-attack test voice is a valid embedded tone");
    return result;
}

SmafFile score(int algorithm,int mode=0,bool xof=true) {
    auto file=fixture(); setVoice(file,tone(algorithm,true,0,xof));
    appendExclusive(file,tone(algorithm,false,1,false));
    auto& s=file.tracks[0].sequenceData; s={0,0x90,84,100,10};
    if (mode==0) s.insert(s.end(),{20,0xff,0x2f,0});
    if (mode==1) s.insert(s.end(),{60,0xff,0x2f,0});
    if (mode==2) s.insert(s.end(),{100,0xc0,1,0,0x90,72,100,10,20,0xff,0x2f,0});
    if (mode==3) s.insert(s.end(),{20,0xb0,7,127,20,0xe0,0,96,20,0xff,0x2f,0});
    return file;
}

std::vector<float> drain(MaPlayer& player,int block=257) {
    std::vector<float> result,buffer(size_t(block)*2); int count;
    while ((count=player.render(buffer.data(),block))>0) {
        for (int i=0;i<count*2;++i) check(std::isfinite(buffer[size_t(i)]),"finite stopped-attack fixture output");
        result.insert(result.end(),buffer.begin(),buffer.begin()+count*2);
        check(result.size()<2000000,"stopped-attack test stays bounded");
    }
    check(player.finished()&&player.render(buffer.data(),block)==0,"stopped-attack EOF remains ended");
    return result;
}

void endpointAndEvents() {
    for (int algorithm=0;algorithm<8;++algorithm) {
        MaPlayer player; check(player.init(score(algorithm),8000),"stopped-attack EOF fixture initializes");
        const auto output=drain(player);
        check(output.size()==1600*2&&std::all_of(output.begin(),output.end(),[](float x){return x==0;}),
              "all eight topologies preserve200ms score then end exact silence");
        check(player.diagnostics().fmNotes==1&&player.diagnostics().fallbackNotes==0
              &&!player.diagnostics().tailLimitReached,"embedded stopped attack ends without safety tail");
    }
    for (int mode=0;mode<=3;++mode) {
        const auto file=score(1,mode);
        const auto expected=renderAll(score(1,mode,false)); // same zeros, ordinary gate can retire
        for (int block : {1,257,1024}) {
            MaPlayer player; check(player.init(file,8000),"future-event fixture initializes");
            const auto output=drain(player,block);
            check(output==expected&&!player.diagnostics().tailLimitReached,
                  "EOF optimization preserves rests, future controls and later audible notes");
            check(output.size()/2==player.scoreEndSamples(),"all authored time is retained");
            if (mode==2) check(std::any_of(output.begin()+16000,output.end(),[](float x){return x!=0;}),
                              "late audible note really survives initial permanent silence");
            if (block==257) {
                player.seekToStart();
                check(drain(player,block)==expected,"seek resets EOF and stopped-attack state");
            }
        }
    }
}

void releaseAndFilterDrain() {
    auto actual=fixture(); setVoice(actual,tone(1,true));
    const auto audible=tone(1,false,1,false,14); appendExclusive(actual,audible);
    actual.tracks[0].sequenceData={0,0x90,45,100,10,0,0xc1,1,0,0x91,45,100,10,10,0xff,0x2f,0};
    auto reference=actual;
    reference.tracks[0].sequenceData={0,0xc1,1,0,0x91,45,100,10,10,0xff,0x2f,0};
    MaPlayer a,b; check(a.init(actual,48000)&&b.init(reference,48000),"audible-release fixtures initialize");
    const auto expected=drain(b),output=drain(a);
    check(output==expected&&!a.diagnostics().tailLimitReached,"silent companion does not truncate audible release or shared LP drain");
    auto parsed=parseVoiceExclusive(audible.data(),audible.size()); parsed.patch.modernTimbre=true;
    FmVoice source; source.setSampleRate(48000); source.noteOn(parsed.patch,110,1);
    size_t sourceFrames=4800; for (size_t i=0;i<sourceFrames;++i) source.tick(); source.noteOff();
    while (source.active()) {source.tick();++sourceFrames;check(sourceFrames<5280,"reference release is bounded");}
    check(output.size()/2>sourceFrames&&output.size()/2<5280,"shared LP history actually drains after the carrier ends");
}

void quietAndAllocation() {
    auto quiet=fixture(); auto image=tone(1,false);
    image[16]=image[23]=0xfc; // TL63 on both live carriers
    setVoice(quiet,image);
    quiet.tracks[0].sequenceData={0,0xb0,7,1,0,0xb0,11,1,0,0x90,60,1,10,20,0xff,0x2f,0};
    MaPlayer player; check(player.init(quiet,8000),"tiny nonzero fixture initializes");
    const auto output=drain(player); float peak=0;
    for (float value:output) peak=std::max(peak,std::fabs(value));
    check(peak>0&&peak<1e-7f&&output.size()/2==81600&&player.diagnostics().tailLimitReached,
          "tiny nonzero XOF tail retains existing duration despite sub-threshold output");

    auto full=fixture(); setVoice(full,tone(1,false,0,false)); appendExclusive(full,tone(1,true,1));
    auto& s=full.tracks[0].sequenceData; s={0,0x90,60,25,100,0,0xc1,1};
    for (int i=0;i<31;++i) s.insert(s.end(),{0,0x91,uint8_t(65+i),25,100});
    s.insert(s.end(),{20,0x92,72,25,20,100,0xff,0x2f,0});
    check(player.init(full,8000),"allocation-preservation fixture initializes"); drain(player);
    check(player.diagnostics().fmNotes==33&&player.diagnostics().stolenFm==1
          &&player.diagnostics().stolenHeldFm==0,
          "silent attacks retain occupied slots but are reclaimed before sounding held notes under pressure");
}

void test() { configuredAttack(); topologyAndFeedback(); endpointAndEvents(); releaseAndFilterDrain(); quietAndAllocation(); }
} // namespace silentAttackRegression
