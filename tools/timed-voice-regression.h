#pragma once
// Synthetic fixtures; no song or manufacturer assets.
void checkTimedVoices() {
    const std::vector<uint8_t> original{0x43,0x79,7,0x7f,1,0,0,0,0,0,
        0,1,0,0,0xf0,0xf0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,0};
    auto changed=original;changed.back()=6<<3;
    auto append=[&](std::vector<uint8_t>& seq,int duration,const std::vector<uint8_t>& image) {
        seq.insert(seq.end(),{uint8_t(duration),0xf0,uint8_t(image.size()+1)});
        seq.insert(seq.end(),image.begin(),image.end());seq.push_back(0xf7);
    };
    for(int format:{2,3}) for(bool initial:{false,true}) for(int firstGate:{10,40}) {
        auto f=fixture(format), reference=fixture(format);
        if(initial) {setVoice(f,original);setVoice(reference,original);}
        auto refVoice=changed;refVoice[7]=1;appendExclusive(reference,refVoice);
        const uint8_t noteStatus=format==3?0x10:0x90;
        const uint8_t pcStatus=format==3?0x40:0xc0;
        f.tracks[0].sequenceData={0,noteStatus,60,100,uint8_t(firstGate)};
        append(f.tracks[0].sequenceData,20,changed);
        f.tracks[0].sequenceData.insert(f.tracks[0].sequenceData.end(),
            {0,noteStatus,64,100,10,30,0xff,0x2f,0});
        reference.tracks[0].sequenceData={0,noteStatus,60,100,uint8_t(firstGate),
            20,pcStatus,1,0,noteStatus,64,100,10,30,0xff,0x2f,0};
        const auto actual=renderAll(f);
        check(actual==renderAll(reference),"inline voice starts at authored time and changes only new notes");
        check(actual==renderAll(f,1) && actual==renderAll(f,512),"inline tone timing is render-block independent");
        MaPlayer p;check(p.init(f,8000),"inline voice seek initializes");
        float buf[512*2];while(p.render(buf,512)>0) {}
        p.seekToStart();std::vector<float> replay;int n;
        while((n=p.render(buf,512))>0) replay.insert(replay.end(),buf,buf+n*2);
        check(actual==replay,"seek does not activate future inline voice prematurely");
        auto newer=changed;newer.back()=16<<3;
        auto latestRef=newer;latestRef[7]=2;appendExclusive(reference,latestRef);
        auto& sequence=f.tracks[0].sequenceData;
        sequence.resize(sequence.size()-4);
        append(sequence,20,newer);
        sequence.insert(sequence.end(),{0,noteStatus,67,100,10,10,0xff,0x2f,0});
        auto& refSeq=reference.tracks[0].sequenceData;refSeq.resize(refSeq.size()-4);
        refSeq.insert(refSeq.end(),{20,pcStatus,2,0,noteStatus,67,100,10,10,0xff,0x2f,0});
        check(renderAll(f)==renderAll(reference),"later same-slot inline definition supersedes earlier one");
        auto duplicate=fixture(format);setVoice(duplicate,original);
        duplicate.tracks[0].sequenceData.clear();
        append(duplicate.tracks[0].sequenceData,0,changed);
        append(duplicate.tracks[0].sequenceData,0,newer);
        duplicate.tracks[0].sequenceData.insert(duplicate.tracks[0].sequenceData.end(),
            {0,noteStatus,60,100,10,10,0xff,0x2f,0});
        auto onlyNew=fixture(format);setVoice(onlyNew,newer);
        onlyNew.tracks[0].sequenceData={0,noteStatus,60,100,10,10,0xff,0x2f,0};
        check(renderAll(duplicate)==renderAll(onlyNew),"same-time inline definitions use last stream definition");

        // An inline tone AFTER the note in the same tick must not change it.
        auto ordered=fixture(format);setVoice(ordered,original);
        ordered.tracks[0].sequenceData={0,noteStatus,60,100,10};
        append(ordered.tracks[0].sequenceData,0,newer);
        ordered.tracks[0].sequenceData.insert(ordered.tracks[0].sequenceData.end(),
            {0,noteStatus,64,100,10,10,0xff,0x2f,0});
        auto orderedRef=fixture(format);setVoice(orderedRef,original);
        appendExclusive(orderedRef,latestRef);
        orderedRef.tracks[0].sequenceData={0,noteStatus,60,100,10,
            0,uint8_t(noteStatus+1),64,100,10,10,0xff,0x2f,0};
        // Select PC2 on the other channel before either note; independent
        // channels avoid changing the first note's program selection.
        orderedRef.tracks[0].sequenceData.insert(orderedRef.tracks[0].sequenceData.begin(),
            {0,uint8_t(pcStatus+1),2});
        check(renderAll(ordered)==renderAll(orderedRef),"same-tick tone does not retroactively change preceding note");
        check(renderAll(ordered,1)==renderAll(ordered,512),"same-tick tone order is block independent");
    }

    // Split boundaries are independent slots; redefining one must not erase
    // the other. Initial setup's split-selection policy is preserved.
    auto expanded=[](const std::vector<uint8_t>& source,int high) {
        std::vector<uint8_t> image{0x43,0x79,8,0x7f,0x21,0,0,source[7],0,uint8_t(high),0};
        image.insert(image.end(),source.begin()+10,source.begin()+13);
        for(int op=0;op<2;++op) {
            const size_t pos=13+size_t(op)*7;
            image.insert(image.end(),source.begin()+pos,source.begin()+pos+5);
            image.push_back(0);image.push_back(source[pos+6]);
            image.push_back(0);image.push_back(0);image.push_back(source[pos+5]);
        }
        return image;
    };
    auto split=fixture(3), splitRef=fixture(3);
    auto low=expanded(original,60), high=expanded(changed,127), newLow=expanded(changed,60);
    setVoice(split,low);appendExclusive(split,high);
    // Keep the reference in the SAME expanded MA7 FM family. VM35 tones now
    // intentionally use a different PM scale; program selection is the oracle.
    setVoice(splitRef,expanded(original,127));auto wide=changed;wide[7]=1;
    appendExclusive(splitRef,expanded(wide,127));
    split.tracks[0].sequenceData={0,0x10,60,100,10,0,0x11,64,100,10};
    append(split.tracks[0].sequenceData,20,newLow);
    split.tracks[0].sequenceData.insert(split.tracks[0].sequenceData.end(),
        {0,0x10,60,100,10,0,0x11,64,100,10,10,0xff,0x2f,0});
    splitRef.tracks[0].sequenceData={0,0x41,1,0,0x10,60,100,10,0,0x11,64,100,10,
        20,0x40,1,0,0x10,60,100,10,0,0x11,64,100,10,10,0xff,0x2f,0};
    check(renderAll(split)==renderAll(splitRef),"inline narrow split replaces only that boundary, preserving wide split");
    auto newHigh=expanded(original,127);
    split.tracks[0].sequenceData.resize(split.tracks[0].sequenceData.size()-4);
    append(split.tracks[0].sequenceData,20,newHigh);
    split.tracks[0].sequenceData.insert(split.tracks[0].sequenceData.end(),
        {0,0x10,60,100,10,0,0x11,64,100,10,10,0xff,0x2f,0});
    splitRef.tracks[0].sequenceData.resize(splitRef.tracks[0].sequenceData.size()-4);
    splitRef.tracks[0].sequenceData.insert(splitRef.tracks[0].sequenceData.end(),
        {20,0x41,0,0,0x10,60,100,10,0,0x11,64,100,10,10,0xff,0x2f,0});
    check(renderAll(split)==renderAll(splitRef),"inline wide split does not override separately authored narrow slot");

    // A tone redefinition changes future allocations, never the PE/SUS flags
    // snapshotted by an already-held voice. Compare with independent programs.
    auto held=fixture(), heldRef=fixture();
    auto oldFlags=original;oldFlags[11]=1;oldFlags[12]=32;
    oldFlags[13]=2;oldFlags[20]=2;
    auto newFlags=changed;newFlags[11]=uint8_t(1|(31<<3));newFlags[12]=32;
    setVoice(held,oldFlags);setVoice(heldRef,oldFlags);
    auto newFlagsRef=newFlags;newFlagsRef[7]=1;appendExclusive(heldRef,newFlagsRef);
    held.tracks[0].sequenceData={0,0xb0,64,64,0,0x90,60,100,10};
    append(held.tracks[0].sequenceData,20,newFlags);
    held.tracks[0].sequenceData.insert(held.tracks[0].sequenceData.end(),
        {0,0x90,64,100,10,5,0xb0,10,0,15,0xb0,64,0,20,0xff,0x2f,0});
    heldRef.tracks[0].sequenceData={0,0xb0,64,64,0,0x90,60,100,10,
        20,0xc0,1,0,0x90,64,100,10,5,0xb0,10,0,15,0xb0,64,0,20,0xff,0x2f,0};
    check(renderAll(held)==renderAll(heldRef),"held tone retains PE/SUS while later notes take new flags");
    check(renderAll(held,1)==renderAll(held,1024),"combined inline PE/SUS behavior is block independent");
}
