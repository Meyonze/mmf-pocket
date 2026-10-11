// SPDX-License-Identifier: Apache-2.0
#pragma once
namespace mobileEosClockRegression {
using Bytes=std::vector<uint8_t>;
SmafFile score(const Bytes& bytes,int voice=0){return ma7DataRegression::score(2,bytes,voice);}
void clock() {
    for(int voice:{0,1,2})for(int delay:{0,3,10,100,127,128,1000}) {
        Bytes prefix={0,0x90,60,100,3},actual=prefix,oracle=prefix;
        auto d=scoreLengthRegression::vlq(delay);
        scoreLengthRegression::append(actual,d);scoreLengthRegression::append(actual,{0xff,0x2f,0});
        scoreLengthRegression::append(oracle,d);scoreLengthRegression::append(oracle,{0xff,0});
        auto f=score(actual,voice),ref=score(oracle,voice);
        check(renderAll(f)==renderAll(ref),"Mobile EOS retains its clock like independent NOP at stream boundary");
        MaPlayer p;check(p.init(f,8000)&&p.scoreEndSamples()==uint64_t(std::max(3,delay))*80,
            "Mobile EOS endpoint matches authored duration and remaining generated gate");
        check(p.scoreAudit().scanComplete,"valid EOS preserves complete coverage");
        auto first=ma7DataRegression::rerender(p,31);p.seekToStart();
        check(first==ma7DataRegression::rerender(p,1024),"EOS clock survives seek/block changes");
        check(renderAll(channelModeRegression::compressed(f))==renderAll(ref),"compressed Mobile EOS clock agrees with plain NOP oracle");
        auto suffix=f;suffix.tracks[0].sequenceData.insert(suffix.tracks[0].sequenceData.end(),{0,0x90,67,127,100});
        check(renderAll(suffix)==renderAll(ref),"timed EOS still ignores fake suffix notes");
        auto other=score({0,0x91,67,80,30,40,0xff,0},voice);other.tracks[0].trackNumber=1;
        f.tracks.push_back(other.tracks[0]);ref.tracks.push_back(other.tracks[0]);
        check(renderAll(f)==renderAll(ref),"EOS clock never mutes independent track");
    }
    // Preserve existing gate/release policy. This does NOT implement the
    // manufacturer's recommended chronological per-sequence hard mute.
    for(int voice:{0,1,2}) {
        auto f=score({0,0x90,60,100,100,10,0xff,0x2f,0},voice);
        auto ref=score({0,0x90,60,100,100,10,0xff,0},voice);
        check(renderAll(f)==renderAll(ref),"gate beyond EOS retains existing natural release policy");
    }
    auto held=score({0,0x90,60,100,3});
    setVoice(held,{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
        8,0x50,0xf0,0xfc,0,0x10,0,8,0x50,0xf0,0,0,0x10,0});
    auto ref=held;auto delay=scoreLengthRegression::vlq(1200);
    scoreLengthRegression::append(held.tracks[0].sequenceData,delay);
    scoreLengthRegression::append(ref.tracks[0].sequenceData,delay);
    scoreLengthRegression::append(held.tracks[0].sequenceData,{0xff,0x2f,0});
    scoreLengthRegression::append(ref.tracks[0].sequenceData,{0xff,0});
    check(renderAll(held)==renderAll(ref),"EOS retains authored horizon for XOF safety fade without guessing a new mute law");
    for(int ticks:{0,100,72000}) {
        auto seq=scoreLengthRegression::vlq(ticks);scoreLengthRegression::append(seq,{0xff,0x2f,0});
        MaPlayer p;check(p.init(score(seq),100),"EOS-only matches existing NOP-only initialization policy");
        auto audio=ma7DataRegression::rerender(p,31);
        check(audio.size()==size_t(std::min(ticks,60000))*2,"EOS-only retains silence and600-second safety cap");
        check(!ma7DataRegression::audible(audio),"EOS-only cannot allocate audible voices");
    }
}
void unchanged() {
    for(int voice:{0,1,2})for(int value=0;value<256;++value) {
        auto prefix=score({0,0x90,60,100,3},voice),broken=prefix;
        auto& s=broken.tracks[0].sequenceData;
        s.insert(s.end(),{100,0xff,0x2f});
        if(value) s.push_back(uint8_t(value)); // value0 means missing final byte
        else {check(renderAll(broken)==renderAll(prefix),"missing EOS cannot create a new clock");continue;}
        s.insert(s.end(),{0,0x90,67,127,100});
        check(renderAll(broken)==renderAll(prefix),"nonzero EOS cannot create a new clock or decode its suffix");
        MaPlayer p;check(p.init(broken,8000)&&!p.scoreAudit().scanComplete,"broken EOS remains audited");
    }
    for(int format:{0}) {
        auto prefix=scoreNopRegression::score(format,scoreNopRegression::note(format)),f=prefix;
        auto eos=Bytes{0,0,0,0};
        scoreLengthRegression::append(f.tracks[0].sequenceData,eos);
        check(renderAll(f)==renderAll(prefix),"HPS EOS policy unchanged; MA7 clock has its independent oracle");
    }
}
}
void checkMobileEosClock(){mobileEosClockRegression::clock();mobileEosClockRegression::unchanged();}
