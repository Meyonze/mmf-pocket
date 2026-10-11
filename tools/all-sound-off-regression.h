// SPDX-License-Identifier: Apache-2.0
#pragma once
// Synthetic Mobile CC120 fixtures, included after channel-mode-regression.h.
namespace allSoundOffRegression {
bool audibleEqual(const std::vector<float>& a,const std::vector<float>& b) {
    for(size_t i=0;i<std::max(a.size(),b.size());++i) {
        const float av=i<a.size()?a[i]:0,bv=i<b.size()?b[i]:0;
        if(std::fabs(av-bv)>1e-7f)return false;
    }
    return true;
}
void testInstrument(bool pcm) {
    for(int behavior=0;behavior<3;++behavior)for(bool alreadyOff:{false,true}) {
        auto file=channelModeRegression::instrument(pcm,behavior==1,behavior==2);
        const uint8_t gate=alreadyOff?10:100;
        file.tracks[0].sequenceData={0,0xb0,64,127,
            0,0x90,60,60,gate,0,0x90,64,60,gate,0,0x91,67,60,100,
            20,0xb0,120,0,100,0xb0,10,64,0,0xff,0x2f,0};
        // For the XOF termination test do not start an intentionally immortal
        // note on the independent channel. Isolation is covered above/below.
        if(behavior==2)file.tracks[0].sequenceData[17]=0;
        // Independent channel remains sounding. Muting expression provides a
        // sample-level bus oracle without relying on the new stop command.
        auto muted=file;muted.tracks[0].sequenceData[21]=11;
        auto noStop=file;noStop.tracks[0].sequenceData[21]=77;
        const auto actual=renderAll(file);
        check(audibleEqual(actual,renderAll(muted)),"CC120 silences only its channel including release/Hold/XOF");
        check(!audibleEqual(actual,renderAll(noStop)),"CC120 really silences the authored live/release sound");
        check(actual==renderAll(channelModeRegression::compressed(file)),"compressed CC120 uses the same transport semantics");
        check(actual==renderAll(file,1) && actual==renderAll(file,1024),"CC120 render block invariant");
        auto repeated=file;
        repeated.tracks[0].sequenceData.insert(repeated.tracks[0].sequenceData.begin()+23,{1,0xb0,120,0});
        repeated.tracks[0].sequenceData[27]=99;
        check(actual==renderAll(repeated),"repeated All Sound Off is idempotent");
        MaPlayer player;check(player.init(file,8000),"CC120 seek initializes");
        float block[514];while(player.render(block,257)>0){}
        check(!player.diagnostics().tailLimitReached,"CC120 drains XOF/held sounds without a safety cutoff");
        player.seekToStart();std::vector<float> replay;int n;
        while((n=player.render(block,257))>0)replay.insert(replay.end(),block,block+n*2);
        check(replay==actual,"CC120 seek restores voice pools and channel state");
        for(int value:{1,127,128,255}) {
            auto invalid=file;invalid.tracks[0].sequenceData[22]=uint8_t(value);
            check(renderAll(invalid)==renderAll(noStop),"nonzero CC120 values are not executed");
        }
    }
    auto file=channelModeRegression::instrument(pcm,true);
    // Keep non-default program, bank, volume, expression, pan, bend sensitivity
    // and Hold. A fresh note after stopping must retain all of these settings.
    auto alternate=pcm?std::vector<uint8_t>{0x43,0x79,7,0x7f,1,1,0,1,0,1,
        0x1f,0x40,0,0,2,0x90,0xf0,0,0,0,0,0,0,0,80,1}:protocolRegression::tone();
    alternate[5]=1;alternate[7]=1;
    if(!pcm){alternate[13]=alternate[20]=2;alternate[14]=alternate[21]=0x90;alternate[26]=6<<3;}
    appendExclusive(file,alternate);
    file.tracks[0].sequenceData={0,0xb0,0,1,0,0xc0,1,0,0xb0,7,90,0,0xb0,11,80,
        0,0xb0,10,0,0,0xb0,101,0,0,0xb0,100,0,0,0xb0,6,12,
        0,0xe0,0,96,0,0xb0,64,127,0,0x90,60,40,100,
        20,0xb0,120,0,5,0x90,64,40,10,20,0xb0,64,0,75,0xb0,10,0,0,0xff,0x2f,0};
    auto oracle=file;oracle.tracks[0].sequenceData[42]=0; // first note silent
    oracle.tracks[0].sequenceData[46]=77; // no channel-reset command in oracle
    const auto actual=renderAll(file),expected=renderAll(oracle);
    check(actual.size()==expected.size(),"CC120 new-key oracle has the same event horizon");
    // Compare after the new onset250ms and the old note's shared-filter drain.
    for(size_t i=4800;i<actual.size();++i)
        check(std::fabs(actual[i]-expected[i])<1e-7f,"CC120 preserves channel state and replacement voice sample rate");
    auto ma7=channelModeRegression::instrument(pcm);
    ma7.tracks[0].formatType=3;
    ma7.tracks[0].sequenceData={0,0x10,60,100,100,20,0x30,120,0,100,0x30,10,64,0,0xff,0x2f,0};
    auto ignored=ma7;ignored.tracks[0].sequenceData[7]=77;
    check(renderAll(ma7)==renderAll(ignored),"unconfirmed compact MA7 CC120 stays unsupported");
    auto before=channelModeRegression::instrument(pcm),after=before;
    before.tracks[0].sequenceData={0,0xb0,120,0,0,0x90,60,100,100,120,0xb0,10,64,0,0xff,0x2f,0};
    after.tracks[0].sequenceData={0,0x90,60,100,100,0,0xb0,120,0,120,0xb0,10,64,0,0xff,0x2f,0};
    auto plain=before;plain.tracks[0].sequenceData[2]=77;
    check(renderAll(before)==renderAll(plain),"CC120 before equal-time new key cannot suppress it");
    for(float value:renderAll(after))check(value==0,"CC120 after equal-time new key stops it before mixing");
}
void testAudio() {
    auto file=fixture();
    WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=8;wave.samplingRate=8000;
    for(int i=0;i<16000;++i)wave.data.push_back(uint8_t(128+60*std::sin(i*0.1)));
    file.tracks[0].waves.push_back(wave);
    file.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,0,0x90,0,127,100,
        20,0xb0,120,0,100,0xb0,10,64,0,0xff,0x2f,0};
    auto natural=file;natural.tracks[0].waves[0].data.resize(1600);
    natural.tracks[0].sequenceData[15]=77;
    check(renderAll(file)==renderAll(natural),"CC120 stops Audio source at command sample without restarting its fade");
    // Stop a voice whose ordinary gate has already started the2ms fade.
    file.tracks[0].durationTimeBase=file.tracks[0].gateTimeBase=0;
    file.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,0,0x90,0,127,20,
        21,0xb0,120,0,99,0xb0,10,64,0,0xff,0x2f,0};
    auto noStop=file;noStop.tracks[0].sequenceData[15]=77;
    check(!audibleEqual(renderAll(file),renderAll(noStop)),"CC120 also stops already released Audio with consumed key ID");
}
void testAtrAndAudit() {
    auto file=fixture();
    TrackChunk control=file.tracks[0];control.trackNumber=7;
    control.sequenceData={20,0xb8,120,0,100,0xb8,10,64,0,0xff,0x2f,0};
    file.tracks.push_back(control);
    TrackChunk atr;atr.isAudioTrack=true;atr.trackNumber=0;atr.formatType=0;
    // Source remains alive at CC120; a1ms gate would no longer test isolation.
    atr.durationTimeBase=atr.gateTimeBase=0x10;
    atr.sequenceData={0,1,0x80,72}; // source-isolation fixture, no early EOS
    WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=8;wave.samplingRate=8000;
    for(int i=0;i<16000;++i)wave.data.push_back(uint8_t(128+60*std::sin(i*0.1)));
    atr.waves.push_back(wave);file.tracks.push_back(atr);
    auto ignored=file;ignored.tracks[1].sequenceData[2]=77;
    check(renderAll(file)==renderAll(ignored),"CC120 leaves independent ATR one-shots untouched");
    MaPlayer player;check(player.init(file,8000),"CC120 ATR initializes");
    float block[514];while(player.render(block,257)>0){}
    check(player.diagnostics().pcmNotes==1,"CC120 ATR fixture really triggers the independent source");
    auto audit=fixture();audit.tracks[0].sequenceData={0,0xb0,120,0,0,0xb0,120,1,0,0xb0,120,128,0,0xff,0x2f,0};
    check(player.init(audit,8000),"CC120 audit initializes");
    using D=ScoreAudit::Domain;using S=ScoreAudit::Status;
    bool routed=false,rejected=false,malformed=false;
    for(const auto& g:player.scoreAudit().groups)if(g.domain==D::Controller && g.opcode==120) {
        if(g.status==S::Routed)routed=g.count==1 && g.minValue==0 && g.maxValue==0;
        if(g.status==S::Unapplied)rejected=g.count==1 && g.minValue==1 && g.maxValue==1;
        if(g.status==S::Malformed)malformed=g.count==1 && g.minValue==128 && g.maxValue==128;
    }
    check(routed && rejected && malformed && !player.scoreAudit().scanComplete,
          "CC120 audit distinguishes valid stop, semantic nonzero and malformed7bit values");
}
}
void checkAllSoundOff() {
    allSoundOffRegression::testInstrument(false);
    allSoundOffRegression::testInstrument(true);
    allSoundOffRegression::testAudio();
    allSoundOffRegression::testAtrAndAudit();
}
