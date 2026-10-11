// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic Mobile CC121 fixtures; no private music or ROM data.
namespace resetControllersRegression {
using Bytes=std::vector<uint8_t>;
void cc(Bytes& s,int dt,int ch,int controller,int value) {
    s.insert(s.end(),{uint8_t(dt),uint8_t(0xb0|ch),uint8_t(controller),uint8_t(value)});
}
void bend(Bytes& s,int dt,int ch,int msb) {
    s.insert(s.end(),{uint8_t(dt),uint8_t(0xe0|ch),0,uint8_t(msb)});
}
void note(Bytes& s,int dt,int ch,int pitch,int velocity,int gate) {
    s.insert(s.end(),{uint8_t(dt),uint8_t(0x90|ch),uint8_t(pitch),uint8_t(velocity),uint8_t(gate)});
}
void reset(Bytes& s,int dt,int ch,bool oracle) {
    if(!oracle){cc(s,dt,ch,121,0);return;}
    // Independent oracle: authored ordinary controls at the same sample.
    cc(s,dt,ch,11,127);cc(s,0,ch,64,0);bend(s,0,ch,64);
    cc(s,0,ch,101,127);cc(s,0,ch,100,127);
}
void finish(Bytes& s,int dt=100) {
    cc(s,dt,0,77,0);s.insert(s.end(),{0,0xff,0x2f,0});
}
SmafFile stateFixture(bool pcm,int behavior,bool oracle) {
    auto file=channelModeRegression::instrument(pcm,behavior==1,behavior==2);
    auto& s=file.tracks[0].sequenceData;s.clear();
    cc(s,0,0,7,85);cc(s,0,0,10,12);cc(s,0,0,11,45);cc(s,0,0,64,127);
    cc(s,0,0,101,0);cc(s,0,0,100,0);cc(s,0,0,6,12);cc(s,0,0,38,50);
    bend(s,0,0,96);
    // A consumed key identity waiting for Hold, a key still down and another
    // channel with its own nondefault controls/pedal/bend must coexist.
    note(s,0,0,60,55,10);note(s,0,0,64,55,80);
    cc(s,0,1,11,50);cc(s,0,1,64,127);bend(s,0,1,80);note(s,0,1,67,55,50);
    reset(s,20,0,oracle);
    // Null selection must ignore Data Entry, preserving existing sensitivity.
    cc(s,0,0,6,2);cc(s,0,0,38,0);bend(s,5,0,96);
    // Independent explicit-velocity oracle for Mobile's p43 default64 reset.
    // Ordinary controller writes alone do not reset remembered score velocity.
    if(oracle)note(s,0,0,72,64,30);
    else s.insert(s.end(),{0,0x80,72,30});
    // Reselect RPN0 without writing it; bend still uses 12 semitones+50cents.
    cc(s,5,0,101,0);cc(s,0,0,100,0);bend(s,0,0,80);
    cc(s,50,1,64,0);finish(s);
    return file;
}
void testInstrument(bool pcm) {
    for(int behavior=0;behavior<3;++behavior) {
        auto actual=stateFixture(pcm,behavior,false),oracle=stateFixture(pcm,behavior,true);
        const auto expected=renderAll(oracle);
        check(renderAll(actual)==expected,"CC121 resets live controls/pedal without releasing pressed keys or changing another channel");
        check(renderAll(channelModeRegression::compressed(actual))==expected,"compressed Mobile CC121 matches uncompressed semantics");
        check(renderAll(actual,1)==expected && renderAll(actual,1024)==expected,"CC121 is render-block invariant");
        MaPlayer player;check(player.init(actual,8000),"CC121 replay initializes");
        float block[514];while(player.render(block,257)>0){}
        player.seekToStart();std::vector<float> replay;int n;
        while((n=player.render(block,257))>0)replay.insert(replay.end(),block,block+2*n);
        check(replay==expected,"CC121 seek restores pre-reset controllers and key identities");
    }
    // A fresh custom instrument verifies bank MSB/LSB, PC, volume and pan are
    // not accidentally reset. Both old and new notes retain authored EVB.
    auto actual=channelModeRegression::instrument(pcm),oracle=actual;
    auto custom=pcm?Bytes{0x43,0x79,7,0x7f,1,1,1,1,0,1,
        0x1f,0x40,0,0,0,0x90,0xf0,0,7,0,0,0,0,0,80,1}:protocolRegression::tone();
    custom[5]=custom[6]=custom[7]=1;
    if(!pcm) {
        custom[11]=3; // nonneutral LFO
        for(int op=0;op<2;++op){custom[14+op*7]=0x90;custom[17+op*7]=7;}
    }
    appendExclusive(actual,custom);appendExclusive(oracle,custom);
    for(bool ref:{false,true}) {
        auto& s=(ref?oracle:actual).tracks[0].sequenceData;s.clear();
        cc(s,0,0,0,1);cc(s,0,0,32,1);s.insert(s.end(),{0,0xc0,1});
        cc(s,0,0,7,80);cc(s,0,0,10,12);cc(s,0,0,11,40);cc(s,0,0,1,127);
        note(s,0,0,60,45,30);reset(s,10,0,ref);note(s,10,0,64,45,20);finish(s);
    }
    check(renderAll(actual)==renderAll(oracle),"CC121 preserves bank/PC/volume/pan and intrinsic voice vibrato for current/future keys");
    auto dry=actual;
    if(pcm)custom[18]=0;else custom[17]=custom[24]=0;
    setVoice(dry,custom);
    check(renderAll(actual)!=renderAll(dry),"CC121 intrinsic EVB fixture actually has audible voice vibrato");
    auto compat=channelModeRegression::instrument(pcm),compatOracle=compat;
    for(bool ref:{false,true}) {
        auto& s=(ref?compatOracle:compat).tracks[0].sequenceData;s.clear();
        cc(s,0,0,15,12);bend(s,0,0,96);note(s,0,0,60,45,80);
        reset(s,20,0,ref);bend(s,10,0,96);note(s,0,0,64,45,30);finish(s);
    }
    check(renderAll(compat)==renderAll(compatOracle),"CC121 preserves existing compatibility CC15 sensitivity values");
    // Repeating reset at the same sample must be idempotent. Nonzero values
    // must not become accidental resets; MA7 compact remains unconfirmed.
    for(int value:{0,1,127,128,255}) {
        auto file=channelModeRegression::instrument(pcm,true),ref=file;
        file.tracks[0].sequenceData={0,0xb0,11,40,0,0xb0,64,127,0,0xe0,0,96,
            0,0x90,60,50,10,20,0xb0,121,uint8_t(value),0,0xb0,121,uint8_t(value),100,0xb0,77,0,0,0xff,0x2f,0};
        ref=file;ref.tracks[0].sequenceData[19]=ref.tracks[0].sequenceData[23]=77;
        if(value==0) {
            auto& s=ref.tracks[0].sequenceData;s.clear();
            cc(s,0,0,11,40);cc(s,0,0,64,127);bend(s,0,0,96);note(s,0,0,60,50,10);
            reset(s,20,0,true);finish(s);
        }
        check(renderAll(file)==renderAll(ref),"CC121 repetition and invalid-value boundaries");
    }
    auto ma7=channelModeRegression::instrument(pcm,true);ma7.tracks[0].formatType=3;
    ma7.tracks[0].sequenceData={0,0x30,11,40,0,0x10,60,50,100,20,0x30,121,0,100,0x30,77,0,0,0xff,0x2f,0};
    auto ignored=ma7;ignored.tracks[0].sequenceData[11]=77;
    check(renderAll(ma7)==renderAll(ignored),"unconfirmed MA7 compact CC121 stays unsupported");
    // Both authored same-sample orders, including pending old gates, must be
    // equivalent to the explicit controller oracle, not to a NoteOff command.
    for(bool resetFirst:{false,true}) {
        auto file=channelModeRegression::instrument(pcm,true),ref=file;
        for(bool isRef:{false,true}) {
            auto& s=(isRef?ref:file).tracks[0].sequenceData;s.clear();
            cc(s,0,0,11,40);cc(s,0,0,64,127);note(s,0,0,60,45,10);
            cc(s,20,0,77,0);
            if(resetFirst)reset(s,0,0,isRef);
            note(s,0,0,60,45,30);
            if(!resetFirst)reset(s,0,0,isRef);
            finish(s);
        }
        check(renderAll(file)==renderAll(ref),"equal-time reset/new-note order respects live keys and old pending gates");
    }
}
void testAudioAndAtr() {
    auto file=fixture();
    WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=8;wave.samplingRate=8000;
    for(int i=0;i<16000;++i)wave.data.push_back(uint8_t(128+60*std::sin(i*.1)));
    file.tracks[0].waves.push_back(wave);
    file.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,0,0x90,0,127,100,
        20,0xb0,121,0,100,0xb0,77,0,0,0xff,0x2f,0};
    auto ref=file;ref.tracks[0].sequenceData[15]=77;
    check(renderAll(file)==renderAll(ref),"CC121 does not stop recorded Audio or turn controller reset into all-notes-off");
    TrackChunk control=fixture().tracks[0];control.trackNumber=7;
    control.sequenceData={20,0xb8,121,0,100,0xb8,77,0,0,0xff,0x2f,0};
    file=fixture();file.tracks[0].sequenceData={0,0xff,0x2f,0};file.tracks.push_back(control);
    TrackChunk atr;atr.isAudioTrack=true;atr.trackNumber=0;atr.formatType=0;
    // Keep this controller-isolation source alive across both timed controls:
    // GateTime200 *10ms=2s (HPS encoding), rather than relying on ignored gates.
    atr.durationTimeBase=atr.gateTimeBase=0x10;
    atr.sequenceData={0,1,0x80,72};atr.waves.push_back(wave);
    file.tracks.push_back(atr);ref=file;ref.tracks[1].sequenceData[2]=77;
    check(renderAll(file)==renderAll(ref),"CC121 leaves independent ATR sources unchanged");
    MaPlayer player;check(player.init(file,8000),"CC121 ATR initializes");
    float buffer[514];while(player.render(buffer,257)>0){}
    check(player.diagnostics().pcmNotes==1,"CC121 ATR fixture really triggers its independent wave");
}
void testAudit() {
    auto file=fixture();file.tracks[0].sequenceData={0,0xb0,121,0,0,0xb0,121,1,0,0xb0,121,128,0,0xff,0x2f,0};
    MaPlayer player;check(player.init(file,8000),"CC121 audit initializes");
    using D=ScoreAudit::Domain;using S=ScoreAudit::Status;
    bool routed=false,rejected=false,malformed=false;
    for(const auto& g:player.scoreAudit().groups)if(g.domain==D::Controller && g.opcode==121) {
        if(g.status==S::Routed)routed=g.count==1 && g.minValue==0 && g.maxValue==0;
        if(g.status==S::Unapplied)rejected=g.count==1 && g.minValue==1 && g.maxValue==1;
        if(g.status==S::Malformed)malformed=g.count==1 && g.minValue==128 && g.maxValue==128;
    }
    check(routed && rejected && malformed && !player.scoreAudit().scanComplete,
          "CC121 audit distinguishes valid reset, semantic nonzero and malformed7bit values");
}
}
void checkResetControllers() {
    resetControllersRegression::testInstrument(false);
    resetControllersRegression::testInstrument(true);
    resetControllersRegression::testAudioAndAtr();
    resetControllersRegression::testAudit();
}
