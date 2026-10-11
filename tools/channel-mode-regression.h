// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic Mobile CC123 fixtures; no private songs or ROM data.
namespace channelModeRegression {
SmafFile compressed(SmafFile file) {
    const auto plain=file.tracks[0].sequenceData;
    // A complete balanced 256-symbol Okumura tree. Synthetic test transport,
    // not a production compressor: each symbol uses its eight-bit value code.
    const uint32_t length=uint32_t(plain.size());
    std::vector<uint8_t> packed{uint8_t(length>>24),uint8_t(length>>16),uint8_t(length>>8),uint8_t(length)};
    size_t bit=32;
    auto emit=[&](int value) {
        if(bit/8>=packed.size())packed.push_back(0);
        packed[bit/8]|=uint8_t((value&1)<<(7-(bit&7)));++bit;
    };
    auto byte=[&](int value){for(int i=7;i>=0;--i)emit(value>>i);};
    auto tree=[&](auto&& self,int first,int count)->void {
        if(count==1){emit(0);byte(first);return;}
        emit(1);self(self,first,count/2);self(self,first+count/2,count/2);
    };
    tree(tree,0,256);for(uint8_t value:plain)byte(value);
    check(smafHuffmanInflate(packed.data(),packed.size())==plain,"synthetic compressed score roundtrips");
    file.tracks[0].formatType=1;file.tracks[0].sequenceData=std::move(packed);
    return file;
}
SmafFile instrument(bool pcm, bool sus=false, bool xof=false) {
    auto file=fixture();
    const uint8_t flags=uint8_t((sus?2:0)|(xof?8:0));
    if(pcm) {
        addPcmFixture(file,1,9);
        setVoice(file,{0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,flags,0x90,0xf0,0,0,0,0,0,0,0,80,1});
    } else {
        auto voice=protocolRegression::tone();
        for(int op=0;op<2;++op) {voice[13+op*7]=flags;voice[14+op*7]=0x90;}
        setVoice(file,voice);
    }
    return file;
}
void testInstrument(bool pcm) {
    for(int behavior=0;behavior<3;++behavior) {
        auto actual=instrument(pcm,behavior==1,behavior==2);
        // Two overlapping same-pitch keys, another pitch, and an independent
        // channel. Only the three channel0 keys are released by All Notes Off.
        actual.tracks[0].sequenceData={0,0xb0,64,127,
            0,0x90,60,100,100,5,0x90,60,100,100,5,0x90,64,100,100,
            0,0x91,67,100,100,10,0xb0,123,0,20,0xb0,64,0,80,0xff,0x2f,0};
        auto reference=actual;
        reference.tracks[0].sequenceData[8]=20;
        reference.tracks[0].sequenceData[13]=15;
        reference.tracks[0].sequenceData[18]=10;
        const auto expected=renderAll(reference);
        check(renderAll(actual)==expected,"Mobile CC123 equals all channel gate releases including Hold/SUS/XOF");
        check(renderAll(compressed(actual))==expected,"compressed Mobile CC123 matches uncompressed transport");
        auto noStop=actual;noStop.tracks[0].sequenceData[26]=77;
        if(behavior!=2)check(renderAll(noStop)!=expected,"Mobile CC123 actually shortens held notes");
        auto repeated=actual;
        repeated.tracks[0].sequenceData.insert(repeated.tracks[0].sequenceData.begin()+28,
            {1,0xb0,123,0,1,0xb0,123,0});
        repeated.tracks[0].sequenceData[36]=18;
        check(renderAll(repeated)==expected,"repeated CC123 does not rearm held/released keys");
        check(renderAll(actual,1)==expected && renderAll(actual,1024)==expected,
            "Mobile CC123 is independent of render block size");
        MaPlayer player;check(player.init(actual,8000),"CC123 seek initializes");
        float block[514];while(player.render(block,257)>0){}
        player.seekToStart();std::vector<float> replay;int n;
        while((n=player.render(block,257))>0)replay.insert(replay.end(),block,block+2*n);
        check(replay==expected,"CC123 seek restores key identities and hold state");
    }
    auto actual=instrument(pcm);
    actual.tracks[0].sequenceData={0,0x90,60,100,100,20,0xb0,123,0,
        5,0x90,60,100,80,95,0xff,0x2f,0};
    auto reference=actual;reference.tracks[0].sequenceData[4]=20;
    check(renderAll(actual)==renderAll(reference),"CC123 allows new notes and old gates cannot stop replacement keys");
    auto noStop=actual;noStop.tracks[0].sequenceData[7]=77;
    for(int value:{1,127,128,255}) {
        auto invalid=actual;invalid.tracks[0].sequenceData[8]=uint8_t(value);
        check(renderAll(invalid)==renderAll(noStop),"nonzero CC123 values are not executed");
    }
    auto ma7=actual;ma7.tracks[0].formatType=3;
    ma7.tracks[0].sequenceData={0,0x10,60,100,100,20,0x30,123,0,
        5,0x10,60,100,80,95,0xff,0x2f,0};
    auto ma7Ignored=ma7;ma7Ignored.tracks[0].sequenceData[7]=77;
    check(renderAll(ma7)==renderAll(ma7Ignored),"unconfirmed compact MA7 CC123 remains unchanged");
    // Same-sample channel command order stays authored; it cannot kill a note
    // decoded after the command, but does release one decoded before it.
    auto before=instrument(pcm), after=before;
    before.tracks[0].sequenceData={0,0xb0,123,0,0,0x90,60,100,100,120,0xff,0x2f,0};
    after.tracks[0].sequenceData={0,0x90,60,100,100,0,0xb0,123,0,120,0xff,0x2f,0};
    auto plain=before;plain.tracks[0].sequenceData[2]=77;
    check(renderAll(before)==renderAll(plain),"CC123 before equal-time NoteOn cannot stop future key");
    check(renderAll(after)!=renderAll(before),"CC123 after equal-time NoteOn releases that key");
}
void testAudio() {
    auto actual=fixture();
    WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=8;wave.samplingRate=8000;
    for(int i=0;i<16000;++i)wave.data.push_back(uint8_t(128+60*std::sin(i*0.1)));
    actual.tracks[0].waves.push_back(wave);
    actual.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,
        0,0x90,0,127,100,20,0xb0,123,0,100,0xb0,10,64,0,0xff,0x2f,0};
    auto reference=actual;reference.tracks[0].sequenceData[12]=20;
    const auto expected=renderAll(reference);
    check(renderAll(actual)==expected,"CC123 releases recorded Audio using its normal gate fade");
    // 1ms timebase: a repeated command and the original gate both land inside
    // the 2ms Audio fade, and must not restart it.
    actual.tracks[0].durationTimeBase=actual.tracks[0].gateTimeBase=0;
    actual.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,
        0,0x90,0,127,21,20,0xb0,123,0,1,0xb0,123,0,99,0xb0,10,64,0,0xff,0x2f,0};
    reference=actual;reference.tracks[0].sequenceData[12]=20;
    check(renderAll(actual)==renderAll(reference),"CC123 and later original Audio gate cannot restart a stop fade");
}
void testAudit() {
    auto file=fixture();
    file.tracks[0].sequenceData={0,0xb0,123,0,0,0xb0,123,1,0,0xb0,123,128,0,0xff,0x2f,0};
    MaPlayer player;check(player.init(file,8000),"CC123 audit initializes");
    using D=ScoreAudit::Domain;using S=ScoreAudit::Status;
    bool routed=false,invalid=false,malformed=false;
    for(const auto& g:player.scoreAudit().groups)if(g.domain==D::Controller && g.opcode==123) {
        if(g.status==S::Routed)routed=g.count==1 && g.minValue==0 && g.maxValue==0;
        if(g.status==S::Unapplied)invalid=g.count==1 && g.minValue==1 && g.maxValue==1;
        if(g.status==S::Malformed)malformed=g.count==1 && g.minValue==128 && g.maxValue==128;
    }
    check(routed && invalid && malformed && !player.scoreAudit().scanComplete,
          "CC123 audit distinguishes valid zero, semantic nonzero and malformed7bit values");
}
void testAtr() {
    auto file=fixture();
    TrackChunk control=file.tracks[0];control.trackNumber=7;
    // Mobile base112 + channel8 targets ATR's global channel120 deliberately.
    control.sequenceData={20,0xb8,123,0,100,0xb8,10,64,0,0xff,0x2f,0};
    file.tracks.push_back(control);
    TrackChunk atr;atr.isAudioTrack=true;atr.trackNumber=0;atr.formatType=0;
    // Keep ATR sounding through the controller test using its authored gate.
    atr.durationTimeBase=atr.gateTimeBase=0x10;
    atr.sequenceData={0,1,0x80,72}; // no explicit early EOS mute
    WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=8;wave.samplingRate=8000;
    for(int i=0;i<16000;++i)wave.data.push_back(uint8_t(128+60*std::sin(i*0.1)));
    atr.waves.push_back(wave);file.tracks.push_back(atr);
    auto reference=file;reference.tracks[1].sequenceData[2]=77;
    check(renderAll(file)==renderAll(reference),"CC123 never treats ATR one-shots as keyed WT voices");
    MaPlayer player;check(player.init(file,8000),"ATR exclusion initializes");
    float block[514];while(player.render(block,257)>0){}
    check(player.diagnostics().pcmNotes==1,"ATR exclusion fixture really triggers its wave");
}
}
void checkChannelMode() {
    channelModeRegression::testInstrument(false);
    channelModeRegression::testInstrument(true);
    channelModeRegression::testAudio();
    channelModeRegression::testAudit();
    channelModeRegression::testAtr();
}
