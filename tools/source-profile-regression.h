// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original fixtures: an inert overlapping MTR must not change a source voice.
namespace sourceProfileRegression {
SmafFile withInert(SmafFile file, int format, bool first) {
    TrackChunk t;
    t.trackNumber = format == 2 ? 0 : 1;
    t.formatType = uint8_t(format);
    t.durationTimeBase = t.gateTimeBase = 0x10;
    // Deliberately conflicting RHYTHM and Mobile KeyControl-OFF headers.
    t.channelStatus.assign(format == 0 ? 2 : 32, format == 0 ? 0x33 : 0x43);
    t.sequenceData = format == 0 ? std::vector<uint8_t>{0,0,0}
                                 : std::vector<uint8_t>{0,0xff,0x2f,0};
    if (first) file.tracks.insert(file.tracks.begin(),t);
    else file.tracks.push_back(t);
    return file;
}
void invariant(const SmafFile& source, int inertFormat, int& cases) {
    const auto expected = renderAll(source);
    check(std::any_of(expected.begin(),expected.end(),[](float x){return x!=0;}),
          "source-profile oracle contains audible output");
    for (bool first : {false,true}) for (int block : {1,257,1024}) {
        MaPlayer p;
        check(p.init(withInert(source,inertFormat,first),8000),"mixed profile initializes");
        const auto actual = silentAttackRegression::drain(p,block);
        check(actual == expected,"inert overlapping MTR preserves complete waveform and tail");
        if(block==257) {
            p.seekToStart();
            check(silentAttackRegression::drain(p,block)==expected,"source profiles survive seek");
        }
        ++cases;
    }
}
void test() {
    int cases=0;
    for(int channel=0;channel<4;++channel) for(bool rhythm:{false,true}) {
        SmafFile f; TrackChunk t;
        t.trackNumber=1;t.formatType=0;t.durationTimeBase=t.gateTimeBase=0x10;
        t.channelStatus.assign(2,rhythm?0x33:0);
        t.sequenceData={0,uint8_t(0x29|(channel<<6)),20,30,0,0,0};
        f.tracks.push_back(t);
        for(int inert:{2,3}) invariant(f,inert,cases);
    }
    // Embedded FM, WT with intrinsic EVB, and fallback FM. Live CC7/11/Pan
    // before and during the note exercise each active voice's own curve.
    for(int format:{1,2,3}) for(int sourceKind:{0,1,2}) {
        auto f = mobileModulationIsolation::score(sourceKind==1,0,0,1,true);
        if(sourceKind==2) f.tracks[0].setupData.clear();
        auto& t=f.tracks[0];t.channelStatus.assign(32,0);
        const uint8_t cc=format==3?0x34:0xb4, note=format==3?0x14:0x94;
        t.sequenceData={0,cc,7,107,0,cc,11,96,0,cc,10,42,
            0,note,60,100,80,10,cc,7,83,10,cc,11,67,10,cc,10,94,
            50,0xff,0x2f,0};
        if(format==1) f=channelModeRegression::compressed(f);
        else t.formatType=uint8_t(format);
        if(format==3) f.tracks[0].trackNumber=1;
        for(int inert: {0,2,3}) {
            if(inert==format || (format==1&&inert==2)) continue;
            invariant(f,inert,cases);
        }
    }
    // A valid MA7 modulation event may share a channel with a Mobile voice,
    // but must not replace that voice's intrinsic EVB with the MA7 wheel law.
    for(bool pcm:{false,true}) for(bool first:{false,true}) {
        auto f=mobileModulationIsolation::score(pcm,0,0,1,true);
        TrackChunk t; t.trackNumber=1;t.formatType=3;
        t.durationTimeBase=t.gateTimeBase=0x10;
        t.sequenceData={10,0x30,1,127,10,0x30,1,0,0,0xff,0x2f,0};
        if(first) f.tracks.insert(f.tracks.begin(),t); else f.tracks.push_back(t);
        auto nop=f;
        nop.tracks[first?0:1].sequenceData={10,0xff,0,10,0xff,0,0,0xff,0x2f,0};
        check(renderAll(f)==renderAll(nop),"MA7 CC1 leaves Mobile intrinsic vibrato intact");
        ++cases;
    }
    for(bool pcm:{false,true}) {
        auto shared=mobileModulationIsolation::score(pcm,0,0,1,true);
        auto& mobile=shared.tracks[0];
        mobile.channelStatus.assign(16,0);
        mobile.sequenceData={0,0xb4,7,107,0,0xb4,11,96,0,0xb4,10,42,
            1,0x94,60,100,80,9,0xb4,7,83,10,0xb4,11,67,10,0xb4,10,94,
            51,0xff,0x2f,0};
        auto ma7=mobile;ma7.formatType=3;ma7.trackNumber=1;
        ma7.sequenceData={0,0x34,7,107,0,0x34,11,96,0,0x34,10,42,
            1,0x14,67,90,80,24,0x34,1,127,56,0xff,0x2f,0};
        shared.tracks.push_back(ma7);
        auto separate=shared;
        separate.tracks[1].sequenceData={0,0x35,7,107,0,0x35,11,96,0,0x35,10,42,
            1,0x15,67,90,80,9,0x35,7,83,10,0x35,11,67,
            5,0x35,1,127,5,0x35,10,94,51,0xff,0x2f,0};
        const auto expected=renderAll(separate);
        for(int block:{1,257,1024}) {
            check(renderAll(shared,block)==expected,
                  "simultaneous mixed voices equal independent channels with duplicated controls");
            ++cases;
        }
    }
    std::cout << "Source-profile waveform/seek/block checks: " << cases << " PASS\n";
}
} // namespace sourceProfileRegression
