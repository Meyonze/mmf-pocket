// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original container-boundary fixtures. No proprietary sample data.
namespace waveBoundaryRegression {
using namespace atrFormatRegression;
Bytes wave(int id=1,int level=24) {
    Bytes data{1,0x1f,0x40}; // mono signed PCM8 at 8000 Hz
    for(int i=0;i<80;++i)data.push_back(uint8_t(int8_t(level*std::sin(i*.3))));
    return chunk("Mwa0",data,id);
}
Bytes track(const Bytes& waves) {
    Bytes body{2,0,0,0};body.resize(20,0);
    append(body,chunk("Mtsq",{0,0xb0,0,125,0,0x90,0,127,40,40,0xff,0x2f,0}));
    append(body,chunk("Mtsp",waves));return chunk("MTR0",body);
}
void same(const Bytes& actual,const Bytes& oracle,const char* label) {
    const auto a=parse(actual),b=parse(oracle);
    check(a.tracks[0].waves.size()==b.tracks[0].waves.size(),label);
    for(size_t i=0;i<a.tracks[0].waves.size();++i)
        check(a.tracks[0].waves[i].number==b.tracks[0].waves[i].number&&
            a.tracks[0].waves[i].data==b.tracks[0].waves[i].data,label);
    const auto expected=renderAll(b);
    check(ma7DataRegression::audible(expected),"wave-boundary oracle actually plays recorded audio");
    check(renderAll(a)==expected&&renderAll(a,1)==expected&&renderAll(a,1024)==expected,label);
    MaPlayer replay;check(replay.init(a,8000),"wave boundary seek initializes");
    check(ma7PanNext::drain(replay)==expected,"wave boundary first render matches");
    replay.seekToStart();check(ma7PanNext::drain(replay,31)==expected,"wave boundary replay matches");
}
void opaque() {
    const auto valid=wave();const auto expected=track(valid);
    for(const char* id:{"MspI","JUNK","Awa0"})for(bool before:{false,true}) {
        const auto opaque=chunk(id,wave(1,70));Bytes region;
        append(region,before?opaque:valid);append(region,before?valid:opaque);
        same(track(region),expected,"non-Mwa child payload cannot manufacture a score wave");
    }
    // Count chunks, not the bytes in one ordinary metadata payload. Preserve
    // the existing scan ceiling instead of increasing it to hide this bug.
    Bytes region=chunk("MspI",Bytes(12000,0x55));append(region,valid);
    same(track(region),expected,"large metadata child cannot consume the entire child scan budget");
    region=chunk("JUNK",{});append(region,chunk("MspI",{}));append(region,valid);
    same(track(region),expected,"empty non-wave child preserves the next real wave");
    // An invalid header keeps the pre-existing bounded byte recovery.
    region={0,0,0};append(region,valid);
    same(track(region),expected,"invalid leading bytes retain bounded recovery");
    // Oversized unknown child consumes its bounded remainder, not nested data.
    region=chunk("JUNK",wave());region[4]=region[5]=region[6]=region[7]=255;
    check(parse(track(region)).tracks[0].waves.empty(),"truncated unknown child does not expose a nested fake wave");
    for(int length=0;length<8;++length) {
        region=valid;const auto extra=chunk("JUNK",wave());
        region.insert(region.end(),extra.begin(),extra.begin()+length);
        same(track(region),expected,"partial next child header cannot expose a wave");
    }
}
void outerBound() {
    const auto whole=track(wave());
    // Root ends immediately after a complete MTR header. Its child claims
    // more bytes, which exist physically but are outside the parent's range.
    const size_t owned=8+20;
    Bytes rootBody(whole.begin(),whole.begin()+owned);
    auto bytes=chunk("MMMD",rootBody);
    bytes.insert(bytes.end(),whole.begin()+owned,whole.end());
    bytes.insert(bytes.end(),{0,0});
    SmafFile f;check(f.parse(bytes.data(),bytes.size()),"bounded empty track still parses");
    check(f.tracks.size()==1&&f.tracks[0].sequenceData.empty()&&f.tracks[0].waves.empty(),
        "oversized top-level child cannot consume bytes beyond declared MMMD boundary");
    MaPlayer p;check(!p.init(f,8000),"out-of-parent sequence is not made playable");
    // Properly declared files remain exactly equivalent; trailing bytes do
    // not extend the declared container even when they form valid chunks.
    const auto normal=parse(whole);bytes=chunk("MMMD",whole);
    append(bytes,chunk("MTR0",emptyChunkRegression::scoreBody(),2));
    check(f.parse(bytes.data(),bytes.size())&&f.tracks.size()==1,
        "valid chunks after declared root are not part of this file");
    check(renderAll(f)==renderAll(normal),"owned playback unaffected by unowned suffix");
}
}
void checkWaveBoundaries(){waveBoundaryRegression::opaque();waveBoundaryRegression::outerBound();}
