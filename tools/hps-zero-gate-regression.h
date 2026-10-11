// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original transport fixtures only. No music/ROM or per-song sound rules.
namespace hpsZeroGateRegression {
using Bytes=std::vector<uint8_t>;
using hpsMetaRegression::score;
using S=ScoreAudit::Status;
void append(Bytes& out,const Bytes& more){out.insert(out.end(),more.begin(),more.end());}
void recovery() {
    // Include prohibited key nibbles too: zero-gate recovery takes precedence
    // over NoteNumber validation, with exactly one known-width diagnostic.
    for(int voice:{0,1,2})for(int code=1;code<255;++code) {
        const int ch=code>>6;
        Bytes prefix={0,uint8_t((ch<<6)|0x21),30};
        auto seq=prefix,ref=prefix;
        append(seq,{5,uint8_t(code),0,5,uint8_t((ch<<6)|0x24),20});
        append(ref,{5,0xff,0,5,uint8_t((ch<<6)|0x24),20});
        auto f=score(seq,voice),oracle=score(ref,voice);
        check(renderAll(f)==renderAll(oracle),
              "HPS zero gate does not sound/retrigger/release a prefix or shift its valid suffix");
        MaPlayer p;check(p.init(f,8000),"zero-gate prefix initializes");
        const auto& a=p.scoreAudit();
        check(a.scanComplete&&a.totals[size_t(S::Reserved)]==1&&
              a.totals[size_t(S::Malformed)]==0,
              "known-width zero gate has one reserved audit, not a lost boundary");
        bool found=false;for(const auto& g:a.groups)
            if(g.domain==ScoreAudit::Domain::Sequence&&g.opcode==0x106)
                found=g.channel==ch&&g.minValue==0&&g.maxValue==0&&g.firstSample==400;
        check(found,"zero gate diagnostic retains exact source channel/time and value");
    }
    for(int voice:{0,1,2})for(int delay:{0,3,10,100,127,128,1000,16511}) {
        auto seq=Bytes{0,0x21,3},ref=seq;
        append(seq,atrPanRegression::length(delay));append(seq,{0x24,0});
        append(ref,atrPanRegression::length(delay));append(ref,{0xff,0});
        auto f=score(seq,voice),oracle=score(ref,voice);
        check(renderAll(f)==renderAll(oracle),
              "trailing zero gate preserves authored duration exactly as independent NOP oracle");
        MaPlayer p;check(p.init(f,8000)&&p.scoreEndSamples()==uint64_t(std::max(delay,3))*80,
                        "zero gate adds only its Duration, not an invented gate");
        auto audio=ma7DataRegression::rerender(p,31);p.seekToStart();
        check(audio==ma7DataRegression::rerender(p,1024),"zero gate seek/block invariance");
        auto other=score({0,0x27,40},voice);other.tracks[0].trackNumber=1;
        f.tracks.push_back(other.tracks[0]);oracle.tracks.push_back(other.tracks[0]);
        check(renderAll(f)==renderAll(oracle),"zero gate cannot stop an independent track");
    }
}
void clockProbe() {
    auto f=score({0,0x21,3,100,0x24,0});
    MaPlayer p;check(p.init(f,8000)&&p.scoreEndSamples()==8000,
                    "zero gate terminal Duration was formerly lost");
}
void edges() {
    for(int delay:{0,1,128,16511}) {
        auto seq=atrPanRegression::length(delay),ref=seq;
        append(seq,{0x21,0});append(ref,{0xff,0});
        auto f=score(seq);MaPlayer p;check(p.init(f,8000),"zero-gate-only score is a silent clock");
        auto audio=ma7DataRegression::rerender(p,257);
        check(audio==renderAll(score(ref))&&!ma7DataRegression::audible(audio)&&
              p.diagnostics().fmNotes+p.diagnostics().pcmNotes+p.diagnostics().audioNotes==0,
              "zero gate alone cannot allocate or emit a voice");
        check(p.scoreEndSamples()==uint64_t(delay)*80&&!p.diagnostics().tailLimitReached,
              "silent clock ends naturally, even at zero time");
    }
    // A late zero gate can move the safety horizon of an existing XOF/held
    // voice. Compare the actual samples to an explicit NOP, not just lengths.
    auto held=score({0,0x21,3,100,0x24,0},1),oracle=score({0,0x21,3,100,0xff,0},1);
    auto tone=protocolRegression::tone();tone[13]|=8;tone[20]|=8;
    const auto parsed=parseVoiceExclusive(tone.data(),tone.size());
    check(parsed.valid&&parsed.patch.ops[0].xof&&parsed.patch.ops[1].xof,
          "held safety fixture really uses authored XOF on both operators");
    setVoice(held,tone);setVoice(oracle,tone);
    check(renderAll(held)==renderAll(oracle),"held/XOF safety horizon follows independent NOP");
    MaPlayer heldPlayer;check(heldPlayer.init(held,8000),"XOF fixture initializes");
    ma7DataRegression::rerender(heldPlayer,257);
    check(heldPlayer.diagnostics().tailLimitReached,"XOF oracle actually exercises moved safety fade");
    auto huge=atrPanRegression::length(16511);append(huge,{0x21,0});
    auto f=score(huge);f.tracks[0].durationTimeBase=0x13;
    MaPlayer p;check(p.init(f,8000)&&p.scoreEndSamples()>uint64_t(600*8000)&&
                    p.totalSamples()==uint64_t(600*8000),"zero gate preserves real clock plus existing600s cap");
    // New clock events consume the existing event budget; do not bypass it.
    Bytes many;many.reserve(1200003);
    for(int i=0;i<400000;++i)append(many,{0,0x21,0});
    check(p.init(score(many),8000)&&p.scoreAudit().totals[size_t(S::ResourceLimit)]==0,
          "exact zero-gate event limit accepted");
    append(many,{0,0x24,3});
    check(p.init(score(many),8000)&&p.scoreAudit().totals[size_t(S::ResourceLimit)]==1,
          "over-budget suffix is diagnosed, never sounded");
    auto audio=ma7DataRegression::rerender(p,257);
    check(!ma7DataRegression::audible(audio)&&p.diagnostics().fmNotes==0,
          "event ceiling cannot manufacture a musical suffix");
    auto other=score({0,0x27,3});other.tracks[0].trackNumber=1;
    f=score(many);f.tracks.push_back(other.tracks[0]);
    check(p.init(f,8000)&&p.scoreAudit().totals[size_t(S::ResourceLimit)]>0&&
          !ma7DataRegression::audible(ma7DataRegression::rerender(p,257)),
          "later tracks cannot bypass shared event ceiling");
}
void malformed() {
    for(int code:{0x21,0x2d,0x6c,0xfe})for(const auto& bad:std::vector<Bytes>{{},{0x80},{0x80,0x80}}) {
        Bytes prefix={0,0x21,3},seq=prefix;
        append(seq,{100,uint8_t(code)});append(seq,bad);
        MaPlayer p;check(p.init(score(seq),8000)&&p.scoreEndSamples()==240,
                        "malformed gate is not a valid zero and gets no fabricated clock");
        check(!p.scoreAudit().scanComplete&&p.scoreAudit().totals[size_t(S::Malformed)]==1&&
              p.scoreAudit().totals[size_t(S::Reserved)]==0,
              "malformed length has no second zero-gate/key diagnostic");
        check(ma7DataRegression::rerender(p,257)==renderAll(score(prefix)),
              "malformed gate preserves prefix release, rejecting fake suffix");
        check(p.init(score(prefix),8000)&&p.scoreAudit().scanComplete&&
              p.scoreAudit().totals[size_t(S::Malformed)]==0,"fresh init clears zero/malformed audit state");
    }
}
void unchanged() {
    hpsNoteDataRegression::unchanged();
    // ATR zero-gate triggers remain silently skipped: no HPS policy leakage.
    auto atr=atrFormatRegression::handBuilt(0,1,8000,0,0,atrGateRegression::tone(400),
                                           {0,1,20,100,2,0});
    auto ref=atr;ref.tracks[0].sequenceData={0,1,20};
    check(renderAll(atr)==renderAll(ref),"ATR zero gate/timeline compatibility unchanged");
    for(int format:{2,3}) {
        const uint8_t on=format==3?0x10:0x90,running=format==3?0:0x80;
        auto modern=ma7DataRegression::score(format,{0,on,60,100,3,5,on,64,32,0,5,running,67,20,20,0xff,0x2f,0});
        auto expected=ma7DataRegression::score(format,{0,on,60,100,3,10,on,67,32,20,20,0xff,0x2f,0});
        check(renderAll(modern)==renderAll(expected),"modern zero gate still commits remembered velocity without HPS audit");
        MaPlayer p;check(p.init(modern,8000)&&p.scoreAudit().scanComplete&&
            p.scoreAudit().totals[size_t(S::Reserved)]==0,"modern zero gate remains accepted");
    }
}
}
void checkHpsZeroGate(){hpsZeroGateRegression::clockProbe();hpsZeroGateRegression::recovery();
    hpsZeroGateRegression::edges();hpsZeroGateRegression::malformed();hpsZeroGateRegression::unchanged();}
