// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic multi-MTR source fixtures. No hardware mixing-law claim.
namespace ma7AudioSourceRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& more){out.insert(out.end(),more.begin(),more.end());}
struct Pair { SmafFile actual,oracle; };
Pair pair(int channel=1,int track=201) {
    auto first=ma7PanNext::audio(0,0),second=ma7PanNext::audio(0,channel);
    first.tracks[0].trackNumber=200;second.tracks[0].trackNumber=track;
    // Distinct, quiet samples keep the existing limiter inactive.
    for(int which=0;which<2;++which) {
        auto& data=(which?second:first).tracks[0].waves[0].data;data.clear();
        for(int i=0;i<4000;++i){int16_t x=int16_t(800*std::sin(i*(which?0.17:0.06)));
            data.push_back(uint8_t(uint16_t(x)>>8));data.push_back(uint8_t(x));}
    }
    const uint8_t cc=uint8_t(0x30|channel),on=uint8_t(0x10|channel);
    first.tracks[0].sequenceData={0,0x30,0,125,0,0x30,32,0,0,0x10,0,127,20,30,0xff,0};
    second.tracks[0].sequenceData={0,cc,0,125,0,cc,32,0,5,on,0,127,20,25,0xff,0};
    Pair p{first,first};p.actual.tracks.push_back(second.tracks[0]);
    auto wave=second.tracks[0].waves[0];wave.number=2;p.oracle.tracks[0].waves.push_back(wave);
    // Independent one-MTR oracle uses two distinct Wave IDs, not provenance.
    p.oracle.tracks[0].sequenceData={0,0x30,0,125,0,0x30,32,0,0,cc,0,125,0,cc,32,0,
        0,0x10,0,127,20,5,on,1,127,20,25,0xff,0};return p;
}
void sources() {
    for(int ch:{0,1,15,16,31})for(int track:{0,1,201,255}) {
        // Compact status bank bit7 maps channels16..31.
        int local=ch&15;uint8_t bank=ch>=16?128:0;
        auto p=pair(local,track);
        if(bank){auto& seq=p.actual.tracks[1].sequenceData;seq[1]|=bank;seq[5]|=bank;seq[9]|=bank;
            auto& ref=p.oracle.tracks[0].sequenceData;ref[9]|=bank;ref[13]|=bank;ref[22]|=bank;}
        const auto expected=renderAll(p.oracle);
        check(ma7DataRegression::audible(expected)&&renderAll(p.actual)==expected,
              "MA7 same Wave ID in different MTRs uses the originating note's sample");
        std::reverse(p.actual.tracks.begin(),p.actual.tracks.end());
        check(renderAll(p.actual)==expected,"source selection independent of MTR storage order");
        MaPlayer player;check(player.init(p.actual,8000),"source fixture initializes");
        auto first=ma7PanNext::drain(player,31);player.seekToStart();
        check(first==ma7PanNext::drain(player,1024),"source provenance survives seek/render blocks");
        check(player.diagnostics().audioNotes==2&&player.diagnostics().missingAudio==0,
              "source oracle actually starts two recorded Audio voices");
    }
}
void missing() {
    for(int which:{0,1}) {
        auto p=pair();p.actual.tracks[which].waves.clear();
        p.oracle.tracks[0].waves.erase(p.oracle.tracks[0].waves.begin()+which);
        const auto expected=renderAll(p.oracle);
        check(ma7DataRegression::audible(expected)&&renderAll(p.actual)==expected,
              "missing own MTR wave must not borrow another MTR's same-number sample");
        MaPlayer player;check(player.init(p.actual,8000),"missing source retains other track");
        ma7PanNext::drain(player);check(player.diagnostics().missingAudio==1&&
            player.diagnostics().audioNotes==1,"missing wave is diagnosed while other MTR still sounds");
        std::reverse(p.actual.tracks.begin(),p.actual.tracks.end());
        check(renderAll(p.actual)==expected,"missing source isolation survives reversed MTR order");
    }
}
void pan() {
    auto p=pair();appendExclusive(p.actual,ma7PanNext::message(0,0,0));
    auto& second=p.actual.tracks[1];
    auto sf=fixture(3);sf.tracks[0]=second;appendExclusive(sf,ma7PanNext::message(0,0,127));second=sf.tracks[0];
    appendExclusive(p.oracle,ma7PanNext::message(0,0,0));appendExclusive(p.oracle,ma7PanNext::message(1,0,127));
    check(renderAll(p.actual)==renderAll(p.oracle),"each MTR's own future Audio uses its own native pan");
    auto& seq=p.actual.tracks[0].sequenceData;seq.resize(seq.size()-3);
    append(seq,ma7PanNext::timed(10,ma7PanNext::message(0,0,64)));append(seq,{20,0xff,0});
    auto& ref=p.oracle.tracks[0].sequenceData;ref.resize(ref.size()-3);
    append(ref,ma7PanNext::timed(5,ma7PanNext::message(0,0,64)));append(ref,{20,0xff,0});
    check(renderAll(p.actual)==renderAll(p.oracle),"live native pan stays scoped to originating MTR despite same Wave ID");
    // Second source retriggers after the first source's live pan update.
    auto& seq2=p.actual.tracks[1].sequenceData;seq2.resize(seq2.size()-3);
    append(seq2,{10,0x11,0,127,10,15,0xff,0});
    ref.resize(ref.size()-3);append(ref,{5,0x11,1,127,10,15,0xff,0});
    check(renderAll(p.actual)==renderAll(p.oracle),"later other-MTR note retains own override after foreign live pan");
    seq.resize(seq.size()-3);append(seq,ma7PanNext::timed(6,ma7PanNext::message(0,0,0)));append(seq,{14,0xff,0});
    ref.resize(ref.size()-3);append(ref,ma7PanNext::timed(1,ma7PanNext::message(0,0,0)));append(ref,{14,0xff,0});
    check(renderAll(p.actual)==renderAll(p.oracle),"foreign live pan cannot retarget slot now stolen by another MTR");
    MaPlayer player;check(player.init(p.actual,8000),"source reuse initializes");ma7PanNext::drain(player);
    check(player.diagnostics().audioNotes==3&&player.diagnostics().stolenAudio==1,
          "source provenance follows actual reused recorded-Audio slot");
    player.seekToStart();check(ma7PanNext::drain(player,1)==renderAll(p.oracle),"native source and live state restore on seek");
}
void unchanged() {
    ma7PanNext::pan();ma7PanNext::invalid();ma7PanNext::reuse();
    checkMobileMeta();checkAtrPan();
}
}
void checkMa7AudioSources(){ma7AudioSourceRegression::sources();ma7AudioSourceRegression::missing();ma7AudioSourceRegression::pan();}
