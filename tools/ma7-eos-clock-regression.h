// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original MA7 timed terminal fixtures; never song-specific sound changes.
namespace ma7EosClockRegression {
using Bytes=std::vector<uint8_t>;
using scoreLengthRegression::append;
SmafFile score(const Bytes& bytes,int voice=0){return ma7DataRegression::score(3,bytes,voice);}
void clock() {
    for(int voice:{0,1,2})for(int channel:{0,15,16,31})for(int delay:{0,3,10,100,127,128,1000}) {
        uint8_t on=uint8_t(0x10|(channel&15)|(channel>=16?0x80:0));
        Bytes actual={0,on,60,100,3},oracle=actual;
        auto d=scoreLengthRegression::vlq(delay);
        append(actual,d);append(actual,{0xff,0x2f,0});append(oracle,d);append(oracle,{0xff,0});
        auto f=score(actual,voice),ref=score(oracle,voice);
        check(renderAll(f)==renderAll(ref),"MA7 terminal Duration matches authored same-time NOP plus EOF");
        MaPlayer p;check(p.init(f,8000)&&p.scoreAudit().scanComplete&&
            p.scoreEndSamples()==uint64_t(std::max(3,delay))*80,"MA7 terminal preserves exact endpoint and remaining gate");
        auto first=ma7DataRegression::rerender(p,31);p.seekToStart();
        check(first==ma7DataRegression::rerender(p,1024),"MA7 EOS seek/block invariance");
        auto suffix=f;append(suffix.tracks[0].sequenceData,{0,on,67,127,100});
        check(renderAll(suffix)==renderAll(ref),"MA7 timed terminator still ignores suffix notes");
        auto other=score({0,0x11,67,80,30,40,0xff,0},voice);other.tracks[0].trackNumber=1;
        f.tracks.push_back(other.tracks[0]);ref.tracks.push_back(other.tracks[0]);
        check(renderAll(f)==renderAll(ref),"MA7 EOS cannot mute independent MTR and latest timeline wins");
    }
    for(int voice:{0,1,2}) {
        auto f=score({0,0x10,60,100,100,10,0xff,0x2f,0},voice);
        auto ref=score({0,0x10,60,100,100,10,0xff,0},voice);
        check(renderAll(f)==renderAll(ref),"MA7 gate beyond EOS retains natural release, not hard mute");
    }
    auto held=score({0,0x10,60,100,3});
    setVoice(held,{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
        8,0x50,0xf0,0xfc,0,0x10,0,8,0x50,0xf0,0,0,0x10,0});
    auto ref=held;auto d=scoreLengthRegression::vlq(1200);
    append(held.tracks[0].sequenceData,d);append(ref.tracks[0].sequenceData,d);
    append(held.tracks[0].sequenceData,{0xff,0x2f,0});append(ref.tracks[0].sequenceData,{0xff,0});
    check(renderAll(held)==renderAll(ref),"MA7 XOF safety fade follows same NOP horizon without retuning envelopes");
    for(int ticks:{0,100,72000}) {
        auto seq=scoreLengthRegression::vlq(ticks);append(seq,{0xff,0x2f,0});
        MaPlayer p;check(p.init(score(seq),100),"MA7 terminal-only timeline initializes like NOP-only");
        auto audio=ma7DataRegression::rerender(p,31);
        check(audio.size()==size_t(std::min(ticks,60000))*2&&!ma7DataRegression::audible(audio),
              "MA7 terminal-only retains silence and600-second cap without voice allocation");
    }
    auto recording=ma7PanNext::audio(0,0);
    recording.tracks[0].sequenceData={0,0x30,0,125,0,0x30,32,0,0,0x10,0,127,100,10,0xff,0x2f,0};
    ref=recording;ref.tracks[0].sequenceData={0,0x30,0,125,0,0x30,32,0,0,0x10,0,127,100,10,0xff,0};
    check(renderAll(recording)==renderAll(ref),"MA7 EOS cannot prematurely end recorded Audio gate");
    auto atr=atrFormatRegression::handBuilt(0,1,8000,0x10,0x10,atrGateRegression::tone(16000),{0,1,0x80,72});
    auto f=score({0,0x10,60,100,3,100,0xff,0x2f,0}),oracle=score({0,0x10,60,100,3,100,0xff,0});
    f.tracks.push_back(atr.tracks[0]);oracle.tracks.push_back(atr.tracks[0]);
    check(renderAll(f)==renderAll(oracle),"MA7 EOS retains independent ATR sample/gate clocks");
    // The added terminal occupies one slot, like a normal NOP. Exact resource
    // boundary remains complete; a stream beyond the existing cap is diagnosed.
    Bytes many;many.reserve(1200003);for(int i=0;i<399999;++i)append(many,{0,0xff,0});
    auto capped=many;append(many,{0,0xff,0x2f,0});append(capped,{0,0xff,0,0,0xff,0x2f,0});
    MaPlayer p;check(p.init(score(many),100)&&p.scoreAudit().scanComplete,"MA7 EOS at exact400000-event limit stays complete");
    check(p.init(score(capped),100)&&!p.scoreAudit().scanComplete&&
        p.scoreAudit().totals[size_t(ScoreAudit::Status::ResourceLimit)]==1,"MA7 beyond event limit cannot claim unseen EOS coverage");
}
void unchanged() {
    for(int voice:{0,1,2})for(int value=0;value<256;++value) {
        auto prefix=score({0,0x10,60,100,3},voice),broken=prefix;
        append(broken.tracks[0].sequenceData,{100,0xff,0x2f});
        if(value)append(broken.tracks[0].sequenceData,{uint8_t(value),0,0x10,67,127,100});
        check(renderAll(broken)==renderAll(prefix),"MA7 missing/nonzero EOS cannot add clock or reinterpret suffix");
        MaPlayer p;check(p.init(broken,8000)&&!p.scoreAudit().scanComplete&&
            p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]==1,"MA7 malformed EOS retains previous diagnostics");
    }
    mobileEosClockRegression::clock(); // Mobile plain/packed policy remains unchanged
    auto f=scoreNopRegression::score(0,scoreNopRegression::note(0)),ref=f;
    append(f.tracks[0].sequenceData,{0,0,0,0});
    check(renderAll(f)==renderAll(ref),"HPS EOS does not borrow timed MA7 grammar");
    // Do not invoke checkScoreNops here: its newly updated valid MA7 EOS
    // oracle intentionally fails in the frozen engine, not in this exclusion.
    for(int voice:{0,1,2}) {
        auto f=score({0,0x10,60,100,30,5,0xff,0x51,3,7,0xa1,0x20,
                      0,0x10,64,100,10,40,0xff,0},voice);
        auto oracle=score({0,0x10,60,100,30,5,0xff,0,0,0x10,64,100,10,40,0xff,0},voice);
        MaPlayer p;check(renderAll(f)==renderAll(oracle)&&p.init(f,8000)&&p.scoreAudit().scanComplete,
              "MA7 VLQ-framed meta compatibility is outside terminal clock repair");
    }
}
}
void checkMa7EosClock(){ma7EosClockRegression::clock();ma7EosClockRegression::unchanged();}
