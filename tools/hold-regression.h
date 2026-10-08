#pragma once
// Synthetic Mobile Hold1 regression; included after fixture helper definitions.
void checkHold1() {
    FmOpPatch p{}; p.ar=15; p.dr=0; p.sr=0; p.rr=15; p.sl=0; p.holdEnabled=true;
    FmEnvelope held, reference;
    held.configure(p,8000); reference.configure(p,8000); held.keyOn(); reference.keyOn();
    advance(held,100); advance(reference,100);
    held.keyOff(true);
    check(held.keyOffDeferred(),"SUS defers gate keyoff while Hold1 is on");
    check(advance(held,100)==advance(reference,100),"deferred gate retains authored envelope");
    held.setHold(false); reference.keyOff();
    check(!held.keyOffDeferred() && advance(held,100)==advance(reference,100),"Hold1 OFF executes deferred gate");
    held.configure(p,8000); held.keyOn(); advance(held,100); held.setHold(false);
    check(advance(held,100)==1.0f,"pedal release does not release a key-down note");
    held.keyOff(true); held.keyOn(); held.setHold(false);
    check(!held.keyOffDeferred(),"keyOn clears deferred keyoff");
    p.xof=true; held.configure(p,8000); held.keyOn(); advance(held,100);
    held.keyOff(true); held.setHold(false);
    check(!held.keyOffDeferred() && advance(held,100)==1.0f,"XOF ignores keyoff including pedal release");

    auto patch=FmVoicePatch::defaultPatch(); patch.algorithm=1;
    for(auto& op:patch.ops) { op=p; op.xof=false; op.tl=0; op.holdEnabled=false; }
    patch.ops[1].holdEnabled=true;
    FmVoice voice; voice.setSampleRate(8000); voice.noteOn(patch,440,1);
    for(int i=0;i<100;++i) voice.tick();
    voice.noteOff(true);
    check(!voice.released(),"held carrier is not classified as a quiet release tail");
    for(int i=0;i<2000;++i) voice.tick();
    check(voice.active(),"mixed-SUS patch retains held carrier when other operator releases");
    voice.setHold(false);
    check(voice.released(),"pedal-off marks truly released FM voice");
    for(int i=0;i<2000;++i) voice.tick();
    check(!voice.active(),"pedal-off drains mixed-SUS voice");
    voice.noteOn(patch,440,1); voice.setHold(false);
    check(!voice.released(),"reallocated voice does not retain deferred release");
    voice.noteOff(true); voice.noteOn(patch,440,1); voice.setHold(false);
    check(!voice.released(),"stolen held FM slot clears the old pending gate");
    for(int i=0;i<2000;++i) voice.tick();
    check(voice.active(),"old pedal release cannot release replacement key-down voice");

    auto make=[](bool pcm,bool sus,int controller=64,int value=64,int gate=10,int offAt=30) {
        auto file=fixture();
        if(pcm) {
            addPcmFixture(file,1,15);
            std::vector<uint8_t> image={0x43,0x79,7,0x7f,1,0,0,0,0,1,
                0x1f,0x40,0,0,uint8_t(sus?2:0),0xf0,0xf0,0,0,0,0,0,0,0,80,1};
            setVoice(file,image);
        } else {
            setVoice(file,{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
                uint8_t(sus?2:0),0xf0,0xf0,0xfc,0,0x10,0,
                uint8_t(sus?2:0),0xf0,0xf0,0,0,0x10,0});
        }
        file.tracks[0].sequenceData={0,0xb0,uint8_t(controller),uint8_t(value),
            0,0x90,60,100,uint8_t(gate),uint8_t(offAt),0xb0,64,0,20,0xff,0x2f,0};
        return file;
    };
    for(bool pcm:{false,true}) {
        const auto normal=make(pcm,true,64,0);
        const auto sustained=make(pcm,true);
        check(renderAll(sustained)==renderAll(make(pcm,true,64,0,30)),"Mobile Hold1 equals extended gate for SUS FM/WT");
        check(renderAll(normal)==renderAll(make(pcm,true,64,63)),"Hold1 value63 is off");
        check(renderAll(sustained)==renderAll(make(pcm,true,64,127)),"Hold1 value127 is on");
        check(renderAll(normal)==renderAll(make(pcm,true,64,128)),"invalid controller value does not enable hold");
        check(renderAll(normal)!=renderAll(sustained),"Hold1 boundary64 changes SUS FM/WT");
        check(renderAll(make(pcm,false))==renderAll(make(pcm,false,64,0)),"SUS-disabled voice ignores Hold1 exactly");
        auto isolated=sustained; isolated.tracks[0].sequenceData[1]=0xb1;
        check(renderAll(isolated)==renderAll(normal),"Hold1 belongs only to its channel");
        check(renderAll(make(pcm,true,64,64,30,10))==renderAll(make(pcm,true,64,0,30,10)),"pedal-off before gate does not shorten note");
        check(renderAll(sustained,1)==renderAll(sustained,1024),"Hold1 render-block invariant");
        auto overlapping=sustained;
        overlapping.tracks[0].sequenceData={0,0xb0,64,64,0,0x90,60,100,10,
            5,0x90,60,100,30,15,0xb0,64,0,30,0xff,0x2f,0};
        auto extended=overlapping;
        extended.tracks[0].sequenceData={0,0xb0,64,0,0,0x90,60,100,20,
            5,0x90,60,100,30,15,0xb0,64,0,30,0xff,0x2f,0};
        check(renderAll(overlapping)==renderAll(extended),"same-pitch gates retain separate identities through pedal release");
        auto stolen=sustained, stolenReference=sustained;
        auto stealSequence=[&](bool hold) {
            std::vector<uint8_t> sequence={0,0xb0,64,uint8_t(hold?64:0)};
            const int slots=pcm?16:32;
            for(int i=0;i<slots;++i)
                sequence.insert(sequence.end(),{0,0x90,60,100,uint8_t(hold?10:30)});
            sequence.insert(sequence.end(),{20,0x90,64,100,50,10,0xb0,64,0,50,0xff,0x2f,0});
            return sequence;
        };
        stolen.tracks[0].sequenceData=stealSequence(true);
        stolenReference.tracks[0].sequenceData=stealSequence(false);
        check(renderAll(stolen)==renderAll(stolenReference),"stolen held FM/WT slots cannot inherit pending note identities");
        MaPlayer player; check(player.init(sustained,8000),"hold seek fixture initializes");
        auto drain=[&](){std::vector<float> result; float b[514]; int n;
            while((n=player.render(b,257))>0) result.insert(result.end(),b,b+n*2); return result;};
        auto original=drain(); player.seekToStart();
        check(original==drain(),"seek clears hold and deferred envelope state");
        check(player.init(normal,8000) && drain()==renderAll(normal),"init does not inherit prior Hold1");
        auto ma7=make(pcm,true); ma7.tracks[0].formatType=3;
        ma7.tracks[0].sequenceData={0,0x30,64,64,0,0x10,60,100,10,30,0x30,64,0,20,0xff,0x2f,0};
        auto ma7Ignored=ma7; ma7Ignored.tracks[0].sequenceData[3]=0;
        check(renderAll(ma7)==renderAll(ma7Ignored),"unconfirmed compact MA7 CC64 remains unchanged");
    }
    auto vma=std::vector<uint8_t>{0x43,3,0,0,0,0,1,2,0x0f,0xf0,0,0,2,0x0f,0xf0,0,0};
    auto legacy=parseVoiceExclusive(vma.data(),vma.size());
    check(legacy.valid && !legacy.patch.ops[0].holdEnabled && legacy.patch.ops[0].rr==6,
          "VMA SUS retains its legacy release-rate6 semantics");
}
