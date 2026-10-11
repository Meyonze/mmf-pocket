// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original binary-container ATR fixtures, never private song/ROM data.
namespace atrFormatRegression {
using Bytes=std::vector<uint8_t>;
Bytes chunk(const char* id,const Bytes& body,int number=-1) {
    Bytes out(id,id+4);if(number>=0)out[3]=uint8_t(number);
    const uint32_t n=uint32_t(body.size());
    out.insert(out.end(),{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)});
    out.insert(out.end(),body.begin(),body.end());return out;
}
void append(Bytes& out,const Bytes& in){out.insert(out.end(),in.begin(),in.end());}
Bytes atr(int track,int wave,int type,int bits,int tbD,int tbG,const Bytes& payload,
          const Bytes& sequence,bool waveFirst=false) {
    Bytes body{0,0,uint8_t(type),uint8_t(bits),uint8_t(tbD),uint8_t(tbG)};
    if(waveFirst)append(body,chunk("Awa0",payload,wave));
    append(body,chunk("Atsq",sequence));
    if(!waveFirst)append(body,chunk("Awa0",payload,wave));
    return chunk("ATR0",body,track);
}
Bytes silentScore(int tb=0x13) {
    Bytes body{2,0,uint8_t(tb),uint8_t(tb)};body.resize(20,0);
    append(body,chunk("Mtsq",{0,0xb0,77,0,0,0xff,0x2f,0}));return chunk("MTR0",body);
}
SmafFile parse(const Bytes& body) {
    auto bytes=chunk("MMMD",body);bytes.insert(bytes.end(),{0,0});
    SmafFile file;check(file.parse(bytes.data(),bytes.size()),"ATR container parses");return file;
}
Bytes source() {
    // Signed PCM8, deliberately non-header-like leading bytes and no DC bias.
    Bytes samples{0x7f,0x80};
    for(int i=2;i<80;++i)samples.push_back(uint8_t(int8_t(30*std::sin(i*.3))));
    return samples;
}
SmafFile handBuilt(int track,int wave,int fs,int tbD,int tbG,const Bytes& payload,
                  const Bytes& seq,bool adpcm=false) {
    SmafFile file;TrackChunk t;t.trackNumber=track;t.isAudioTrack=true;t.formatType=0;
    t.durationTimeBase=tbD;t.gateTimeBase=tbG;t.sequenceData=seq;
    WaveData w;w.number=wave;w.samplingRate=fs;w.adpcm=adpcm;
    w.bitsPerSample=adpcm?4:8;w.signedPcm8=true;w.data=payload;
    t.waves.push_back(w);file.tracks.push_back(t);return file;
}
void testHeaders() {
    const auto data=source();
    for(int code=0;code<5;++code)for(bool adpcm:{false,true}) {
        const Bytes seq{3,1,100}; // EOF, not an immediate explicit mute
        auto file=parse(atr(0,1,(adpcm?0x10:0)|code,adpcm?0:0x10,0,2,data,seq));
        const auto& t=file.tracks[0];check(t.waves.size()==1,"supported mono ATR has its raw wave");
        const auto& w=t.waves[0];
        static constexpr int rates[5]={4000,8000,11000,22050,44100};
        check(t.durationTimeBase==0 && t.gateTimeBase==2,"ATR preserves distinct D/G clock bytes");
        check(w.samplingRate==uint32_t(rates[code]) && w.adpcm==adpcm &&
              w.bitsPerSample==(adpcm?4:8) && w.channels==1,"ATR codec/rate/depth comes from track header");
        check(w.data==data,"Awa raw payload retains both leading bytes");
        if(!adpcm)check(w.signedPcm8,"ATR PCM8 is signed, independent of Mwa PCM8 policy");
        auto expected=handBuilt(0,1,rates[code],0,2,data,seq,adpcm);
        check(renderAll(file)==renderAll(expected),"parsed ATR renders its exact retained payload and declared rate");
        check(renderAll(file,1)==renderAll(file,1024),"ATR binary fixture is block invariant");
    }
    // A single ADPCM byte remains usable (old Awa path required at least2).
    auto shortWave=parse(atr(0,1,0x11,0,0,0,{0x17},{0,1,100}));
    check(shortWave.tracks[0].waves.size()==1 && shortWave.tracks[0].waves[0].data==Bytes{0x17},"one-byte ADPCM payload is not mistaken for a missing header");
    for(int length=0;length<6;++length) {
        auto bytes=chunk("MMMD",chunk("ATR0",Bytes(size_t(length),0)));
        bytes.insert(bytes.end(),{0,0});SmafFile f;
        check(!f.parse(bytes.data(),bytes.size()),"short ATR track header is rejected");
    }
    for(const auto invalid:{std::pair{0x81,0x10},std::pair{0x21,0},std::pair{0x31,0},
                           std::pair{0x15,0},std::pair{0x08,0x10},std::pair{0x01,0x20},
                           std::pair{0x01,0x30},std::pair{0x11,0x10},std::pair{0x01,0x11}}) {
        auto f=parse(atr(0,1,invalid.first,invalid.second,0,0,data,{0,1,100,0,0,0,0}));
        check(f.tracks[0].waves.empty(),"unknown/stereo/reserved ATR wave shapes are not guessed");
    }
    for(int field:{0,1}) {
        auto track=atr(0,1,1,0x10,0,0,data,{0,1,100,0,0,0,0});
        track[8+field]=2;
        MaPlayer player;check(!player.init(parse(track),8000),"unsupported ATR format/subsequence is not decoded as a score");
    }
}
void testPureAndClock() {
    const auto data=source();const Bytes seq{10,1,100};
    auto only=parse(atr(0,1,1,0x10,0,2,data,seq));
    auto mixed=only;mixed.tracks.push_back(fixture().tracks[0]);
    mixed.tracks.back().durationTimeBase=mixed.tracks.back().gateTimeBase=0x13;
    mixed.tracks.back().sequenceData={0,0xb0,77,0,0,0xff,0x2f,0};
    const auto expected=renderAll(only);
    check(expected==renderAll(mixed),"ATR1ms clock is not replaced with an unrelated score50ms clock");
    bool audible=false;for(float s:expected)if(s!=0)audible=true;
    check(audible,"pure ATR fixture actually sounds");
    MaPlayer player;check(player.init(only,8000),"pure ATR playback initializes");
    float block[514];while(player.render(block,257)>0){}
    check(player.diagnostics().fmNotes==0 && player.diagnostics().pcmNotes==1,"pure ATR is decoded once as audio, never as score notes");
    player.seekToStart();std::vector<float> replay;int n;
    while((n=player.render(block,257))>0)replay.insert(replay.end(),block,block+2*n);
    check(replay==expected,"ATR seek restarts its own timeline exactly");
    auto replacement=handBuilt(0,1,8000,0,0,Bytes(80,0),{0,1,100});
    check(player.init(replacement,8000),"ATR new-song init resets banks");
    while((n=player.render(block,257))>0)for(int i=0;i<n*2;++i)check(block[i]==0,"previous ATR bank/sample never leaks into new song");
}
void testWaveIdsAndBanks() {
    const auto data=source();
    auto reference=parse(atr(0,1,1,0x10,0,0,data,{0,1,100}));
    const auto expected=renderAll(reference);
    for(int id:{1,15,16,31,32,48,62}) {
        auto file=parse(atr(0,id,1,0x10,0,0,data,{0,uint8_t(id),100},true));
        check(renderAll(file)==expected,"ATR uses all six wave-ID bits, independent of chunk order");
    }
    for(int id:{0,63,64,255}) {
        auto f=parse(atr(0,id,1,0x10,0,0,data,{0,1,100,0,0,0,0}));
        check(f.tracks[0].waves.empty(),"reserved Awa chunk IDs are rejected");
    }
    auto invalidMessage=parse(atr(0,1,1,0x10,0,0,data,{0,63,100,0,0,0,0}));
    MaPlayer invalidPlayer;
    check(!invalidPlayer.init(invalidMessage,8000),"reserved wave message63 cannot trigger another sample");
    // Track1's silent wave with the same ID may not replace track0's wave.
    Bytes body;append(body,atr(0,1,1,0x10,0,0,data,{0,1,100}));
    append(body,atr(1,1,1,0x10,0,0,Bytes(80,0),{0,1,100}));
    auto two=parse(body);
    check(renderAll(two)==expected,"Awa IDs belong to each ATR track, not a global wave bank");
    // Score Mwa1 must not override Awa1 or vice versa. Both container orders.
    Bytes mtr{2,0,0,0};mtr.resize(20,0);
    append(mtr,chunk("Mtsq",{0,0xb0,77,0,0,0xff,0x2f,0}));
    Bytes mwa{1,0x1f,0x40};mwa.insert(mwa.end(),80,100);
    append(mtr,chunk("Mtsp",chunk("Mwa0",mwa,1)));
    for(bool first:{false,true}) {
        body.clear();if(first)append(body,chunk("MTR0",mtr));
        append(body,atr(0,1,1,0x10,0,0,data,{0,1,100}));
        if(!first)append(body,chunk("MTR0",mtr));
        check(renderAll(parse(body))==expected,"Mwa/Awa ID collision cannot substitute another source");
    }
    // Reverse direction: audible Mwa1 with a silent Awa1, both track orders.
    mtr=Bytes{2,0,0,0};mtr.resize(20,0);
    append(mtr,chunk("Mtsq",{0,0xb0,0,125,0,0xb0,32,0,0,0x90,0,127,100,100,0xff,0x2f,0}));
    append(mtr,chunk("Mtsp",chunk("Mwa0",mwa,1)));
    const auto scoreExpected=renderAll(parse(chunk("MTR0",mtr)));
    bool audible=false;for(float sample:scoreExpected)if(sample!=0)audible=true;
    check(audible,"reverse collision fixture actually sounds recorded Mwa");
    for(bool first:{false,true}) {
        body.clear();if(first)append(body,chunk("MTR0",mtr));
        append(body,atr(0,1,1,0x10,0,0,Bytes(80,0),{0,1,100}));
        if(!first)append(body,chunk("MTR0",mtr));
        check(renderAll(parse(body))==scoreExpected,"Awa cannot override sounding Mwa with the same ID");
    }
}
}
void checkAtrFormat() {
    atrFormatRegression::testHeaders();
    atrFormatRegression::testPureAndClock();
    atrFormatRegression::testWaveIdsAndBanks();
}
