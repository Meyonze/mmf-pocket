// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original mixed-grammar allocation fixtures; no song-specific synthesis.
namespace hpsNotePolicyRegression {
using Bytes=std::vector<uint8_t>;
SmafFile modern(int format,int channel,int voice,bool secondSounds=true) {
    Bytes seq;
    if(format==2) seq={0,uint8_t(0x90|channel),60,80,30,
        5,uint8_t(0x90|channel),64,80,uint8_t(secondSounds?30:0),40,0xff,0};
    else seq={0,uint8_t(0x10|channel),60,80,30,
        5,uint8_t(0x10|channel),64,80,uint8_t(secondSounds?30:0),40,0xff,0};
    return ma7DataRegression::score(format,seq,voice);
}
SmafFile addHps(SmafFile f,int channel,bool first) {
    // Distinct MTR numbers: HPS MTR1 maps to global4..7, modern MTR0.
    auto h=atrPanRegression::hps({0,0xff,0}).tracks[0];
    h.trackNumber=channel/4;
    if(first) f.tracks.insert(f.tracks.begin(),h);else f.tracks.push_back(h);
    return f;
}
void polyphony() {
    for(int format:{2,3})for(int channel:{4,5,6,7})for(int voice:{0,1})for(bool first:{false,true}) {
        auto ref=modern(format,channel,voice),mixed=addHps(ref,channel,first);
        auto expected=renderAll(ref);
        check(ma7DataRegression::audible(expected),"modern overlap oracle is audible");
        check(renderAll(mixed)==expected,"inert HPS track cannot force Mobile/MA7 overlapping FM notes to mono");
        // Equal-key overlaps also retain their independent paired gates.
        mixed.tracks[first?1:0].sequenceData[7]=60;
        ref.tracks[0].sequenceData[7]=60;
        check(renderAll(mixed)==renderAll(ref),"same-key modern overlaps retain independent gates with inert HPS");
        if(channel==7&&first) {
            MaPlayer p;check(p.init(mixed,8000),"mixed score initializes");
            auto a=ma7DataRegression::rerender(p,31);p.seekToStart();
            check(a==ma7DataRegression::rerender(p,1024),"mixed note policy survives seek and block boundaries");
            check(p.scoreAudit().scanComplete,"mixed valid grammar retains complete audit");
            if(format==2)check(renderAll(addHps(channelModeRegression::compressed(ref),channel,first))==renderAll(ref),
                "compressed Mobile retains polyphony with HPS track");
        }
    }
}
void unchanged() {
    for(int format:{2,3})for(int channel:{4,7})for(int voice:{0,1,2})for(bool first:{false,true}) {
        // One sounding note (the second has gate0): gain/patch/timing unchanged.
        auto ref=modern(format,channel,voice,false);
        check(renderAll(addHps(ref,channel,first))==renderAll(ref),"single sounding note and WT allocation are unchanged");
        if(voice==2) {
            ref=modern(format,channel,voice);
            check(renderAll(addHps(ref,channel,first))==renderAll(ref),"HPS FM mono policy never applies to modern WT pool");
        }
    }
    // Existing HPS mono retrigger is also checked against the frozen renderer
    // by the stage's separate bit-exact exclusion report.
    auto h=atrPanRegression::hps({0,0x21,100,5,0x24,20,40,0xff,0});
    MaPlayer p;check(p.init(h,8000),"HPS overlap initializes");
    auto a=ma7DataRegression::rerender(p,1);p.seekToStart();
    check(a==ma7DataRegression::rerender(p,1024),"HPS retrigger remains deterministic");
    // A modern track on a disjoint channel cannot change HPS allocation.
    auto mixed=h;auto inert=ma7DataRegression::score(2,{0,0xff,0});
    inert.tracks[0].trackNumber=1;mixed.tracks.push_back(inert.tracks[0]);
    check(renderAll(mixed)==renderAll(h),"independent modern track preserves HPS mono behavior");
}
}
void checkHpsNotePolicy(){hpsNotePolicyRegression::polyphony();hpsNotePolicyRegression::unchanged();}
