#pragma once
// Original synthetic fixtures: authored event order and complete FM bodies.
namespace protocolRegression {
std::vector<uint8_t> tone(int alg=1) {
    std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,uint8_t(alg)};
    for(int op=0;op<(alg<2?2:4);++op)
        v.insert(v.end(),{0,0xf0,0xf0,0,0,0x10,0});
    return v;
}
void testTiming() {
    for(int format:{2,3}) {
        uint8_t note=format==3?0x10:0x90, pc=format==3?0x40:0xc0;
        auto original=tone(), alternate=original;
        alternate[7]=1;alternate.back()=6<<3;
        for(int delay:{1,3,5,6}) {
            auto actual=fixture(format);setVoice(actual,original);appendExclusive(actual,alternate);
            actual.tracks[0].sequenceData={0,note,60,100,100,uint8_t(delay),pc,1,
                uint8_t(20-delay),note,64,100,30,30,0xff,0x2f,0};
            auto reference=actual;
            reference.tracks[0].sequenceData.insert(reference.tracks[0].sequenceData.begin(),{0,pc,0});
            check(renderAll(actual)==renderAll(reference),"future PC cannot select the first note's tone early");
            check(renderAll(actual,1)==renderAll(actual,512),"future PC is block independent");
        }
        const uint8_t cc=format==3?0x30:0xb0;
        for(int controller:{0,32}) {
            auto bankTone=tone();bankTone[controller==0?5:6]=1;
            bankTone.back()=6<<3;
            auto actual=fixture(format);setVoice(actual,original);appendExclusive(actual,bankTone);
            actual.tracks[0].sequenceData={0,pc,0,0,note,60,100,100,
                3,cc,uint8_t(controller),1,0,pc,0,17,note,64,100,30,30,0xff,0x2f,0};
            auto reference=actual;
            reference.tracks[0].sequenceData.insert(reference.tracks[0].sequenceData.begin(),
                {0,cc,uint8_t(controller),0});
            check(renderAll(actual)==renderAll(reference),"future bank change cannot alter earlier note selection");
        }
        auto actual=fixture(format);setVoice(actual,original);appendExclusive(actual,alternate);
        actual.tracks[0].sequenceData={0,note,60,100,30,0,pc,1,0,note,64,100,30,30,0xff,0x2f,0};
        auto reference=actual;
        reference.tracks[0].sequenceData={0,uint8_t(pc+1),1,0,note,60,100,30,
            0,uint8_t(note+1),64,100,30,30,0xff,0x2f,0};
        check(renderAll(actual)==renderAll(reference),"same-time Note/PC/Note preserves authored order");
        check(renderAll(actual,1)==renderAll(actual,1024),"same-time PC is block independent");
        MaPlayer player;check(player.init(actual,8000),"ordered seek initializes");
        float block[1024];while(player.render(block,512)>0){}
        player.seekToStart();std::vector<float> replay;int n;
        while((n=player.render(block,512))>0)replay.insert(replay.end(),block,block+2*n);
        check(replay==renderAll(actual),"seek preserves event order");
    }
    // Gate NoteOff is generated when its NoteOn is decoded. Preserve that
    // existing insertion tie-break rather than promoting a later pedal event.
    auto gated=fixture();auto sus=tone();sus[13]=sus[20]=2;
    setVoice(gated,sus);
    gated.tracks[0].sequenceData={0,0x90,60,100,10,10,0xb0,64,64,
        20,0xb0,64,0,20,0xff,0x2f,0};
    auto pedalLater=gated;
    pedalLater.tracks[0].sequenceData[5]=11;
    pedalLater.tracks[0].sequenceData[9]=19;
    check(renderAll(gated)==renderAll(pedalLater),"later equal-time Hold cannot precede generated gate-off");
    auto pedalEarlier=gated;
    pedalEarlier.tracks[0].sequenceData[5]=9;
    pedalEarlier.tracks[0].sequenceData[9]=21;
    check(renderAll(gated)!=renderAll(pedalEarlier),"Hold before gate-off defers release");
}
void testCompleteBodies() {
    for(int alg:{1,2}) {
        auto direct=tone(alg);
        auto shortForm=std::vector<uint8_t>{0x43,5,1,0,0};
        shortForm.insert(shortForm.end(),direct.begin()+10,direct.end());
        std::vector<uint8_t> packed{0x43,0x79,6,0x7f,1,0,0,0,0,0};
        packed.resize(10+4+8*(alg<2?2:4),0);packed[13]=uint8_t(alg);
        std::vector<uint8_t> vma{0x43,3,0,0,0};
        vma.resize(5+2+5*(alg<2?2:4),0);vma[5]=uint8_t(alg);vma[6]=1;
        for(const auto& image:{direct,shortForm,packed,vma}) {
            check(parseVoiceExclusive(image.data(),image.size()).valid,"complete FM body accepted");
            for(size_t length=0;length<image.size();++length)
                check(!parseVoiceExclusive(image.data(),length).valid,"every incomplete FM body rejected");
            auto extended=image;extended.push_back(0);
            check(parseVoiceExclusive(extended.data(),extended.size()).valid,"legacy trailing bytes remain compatible");
        }
    }
    for(int format:{2,3}) {
        auto reference=fixture(format);setVoice(reference,tone());
        const uint8_t note=format==3?0x10:0x90;
        reference.tracks[0].sequenceData={0,note,60,100,20,20,0xff,0x2f,0};
        const auto expected=renderAll(reference);
        auto incomplete=tone();incomplete.resize(14);
        auto setup=reference;appendExclusive(setup,incomplete);
        check(renderAll(setup)==expected,"incomplete setup cannot overwrite a complete tone");
        auto setupFirst=reference;setVoice(setupFirst,incomplete);
        appendExclusive(setupFirst,tone());
        check(renderAll(setupFirst)==expected,"incomplete earlier setup cannot mask a later complete tone");
        auto timed=reference;
        auto& seq=timed.tracks[0].sequenceData;
        seq.insert(seq.begin(),{0,0xf0,uint8_t(incomplete.size()+1)});
        seq.insert(seq.begin()+3,incomplete.begin(),incomplete.end());
        seq.insert(seq.begin()+3+incomplete.size(),0xf7);
        check(renderAll(timed)==expected,"incomplete inline tone cannot overwrite a complete tone");
    }
}
}
void checkProtocolSequencing(){protocolRegression::testTiming();protocolRegression::testCompleteBodies();}
