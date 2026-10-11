// SPDX-License-Identifier: Apache-2.0
// Original synthetic fixtures only; no song-specific synthesis rules.
namespace ma7DataRegression {
using Bytes = std::vector<uint8_t>;
void append(Bytes& out, const Bytes& more) { out.insert(out.end(),more.begin(),more.end()); }
SmafFile score(int format, const Bytes& bytes, int voice=0) {
    auto f=fixture(format); f.tracks[0].sequenceData=bytes;
    if(voice==1) setVoice(f,protocolRegression::tone());
    if(voice==2) addPcmFixture(f,1,15);
    if(voice==3) setVoice(f,{0x43,0x79,7,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,0,0,uint8_t(28<<2),0,0,0,0,0,0,0,0});
    return f;
}
bool audible(const std::vector<float>& audio) {
    return std::any_of(audio.begin(),audio.end(),[](float x){return std::fabs(x)>1e-12f;});
}
std::vector<float> rerender(MaPlayer& p, int block) {
    std::vector<float> out,buffer(size_t(block)*2); int n;
    while((n=p.render(buffer.data(),block))>0) {
        out.insert(out.end(),buffer.begin(),buffer.begin()+n*2);
        check(out.size()<=8000*2*600,"data fixture bounded");
    }
    return out;
}
void velocity() {
    for(int voice=0;voice<4;++voice) for(int high:{0,0x80}) {
        // Explicit velocity1 and its compressed reuse, with live gain changes.
        Bytes seq={0,uint8_t(0x30|high),7,32,0,uint8_t(0x30|high),11,32,
            0,uint8_t(0x10|high),60,1,30,5,uint8_t(0x30|high),7,127,
            0,uint8_t(0x30|high),11,127,5,uint8_t(high),64,20,30,0xff,0x2f,0};
        auto f=score(3,seq,voice);
        auto audio=renderAll(f,31);
        check(!audible(audio),"MA7 explicit/running velocity1 stays exactly mute after live gain changes");
        MaPlayer p;check(p.init(f,8000),"mute initializes");
        auto first=rerender(p,257);p.seekToStart();
        check(first==rerender(p,1),"mute seek and render block invariance");
        check(p.diagnostics().fmNotes+p.diagnostics().pcmNotes==2,
              "muted MA7 events retain existing voice allocation policy");
        check(p.scoreEndSamples()==3200,"muted notes retain30-tick gates and later40-tick authored MA7 EOS endpoint");
        seq={0,uint8_t(0x10|high),60,2,10,10,0xff,0x2f,0};
        check(audible(renderAll(score(3,seq,voice))),"MA7 velocity2 remains audible");
        seq[3]=0;check(!audible(renderAll(score(3,seq,voice))),"MA7 zero remains silent");
        auto legacy=score(2,{0,0x90,60,1,10,10,0xff,0x2f,0},voice);
        check(audible(renderAll(legacy)),"legacy Mobile velocity1 retains squared curve");
    }
    // MA7 initializes overlapping global channels in this mixed-format fixture.
    // The mute exception must follow each decoded event, not channel flags.
    auto legacy=score(2,{0,0x90,60,1,10,10,0xff,0x2f,0},1);
    auto mixed=legacy;auto ma7=fixture(3);ma7.tracks[0].sequenceData={0,0xff,0x2f,0};
    mixed.tracks.push_back(ma7.tracks[0]);
    check(audible(renderAll(mixed)),"MA7 track cannot mute Mobile velocity1 on a shared channel");
    check(audible(renderAll(atrPanRegression::hps({0,0x21,1,10}))),
          "HPS velocity1 also retains legacy gain");
    // Valid velocities remain squared, including ordinary MA7 authored FM/WT.
    for(int voice:{1,2}) {
        auto a=score(3,{0,0x10,60,32,10,10,0xff,0x2f,0},voice),b=a;
        b.tracks[0].sequenceData[3]=64;
        check(std::fabs(firstBlockPeak(a)/firstBlockPeak(b)-0.25f)<1e-5,
              "MA7 valid velocity curve unchanged");
    }
}
void invalidData() {
    // An independent audible oracle, not only an audit-counter expectation:
    // 128 must not wrap to Program0 and change the next voiced melody.
    auto poisoned=score(3,{0,0x40,40,0,0x10,60,100,10,
                          10,0x40,128,0,0,64,10,10,0xff,0x2f,0});
    auto prior=score(3,{0,0x40,40,0,0x10,60,100,10,
                       10,0xff,0,0,0,64,10,10,0xff,0x2f,0});
    check(audible(renderAll(prior))&&renderAll(poisoned)==renderAll(prior),
          "invalid MA7 Program cannot wrap and replace the next audible instrument");
    // Known field widths allow recovery without treating data bytes as statuses.
    for(int voice:{0,1,2}) for(int high:{0,0x80}) for(int raw=128;raw<=255;++raw) {
        uint8_t r=uint8_t(raw),cc=uint8_t(0x30|high),pc=uint8_t(0x40|high),
                bend=uint8_t(0x60|high),note=uint8_t(0x10|high);
        for(const auto& bad:std::vector<Bytes>{{0,pc,r},{0,bend,r,72},{0,bend,0,r},
                {0,cc,0,r},{0,cc,32,r},{0,cc,7,r},{0,cc,10,r},{0,cc,11,r},
                {0,cc,1,r},{0,cc,15,r},{0,cc,r,0}}) {
            Bytes prefix={0,pc,0,0,cc,7,50,0,cc,11,70,0,cc,10,25,
                          0,bend,0,72,0,note,60,100,30};
            Bytes candidate=prefix;append(candidate,{5});
            // Replace the invalid command by a timed, explicitly inert NOP.
            candidate.back()=bad[0]+5;candidate.insert(candidate.end(),bad.begin()+1,bad.end());
            append(candidate,{5,uint8_t(high),64,15,30,0xff,0x2f,0});
            Bytes expected=prefix;append(expected,{5,0xff,0,5,uint8_t(high),64,15,30,0xff,0x2f,0});
            auto f=score(3,candidate,voice),ref=score(3,expected,voice);
            check(renderAll(f)==renderAll(ref),"invalid MA7 control/patch/bend preserves previous live state and timing");
            MaPlayer p;check(p.init(f,8000),"invalid field score initializes");
            check(!p.scoreAudit().scanComplete && p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]==1,
                  "invalid MA7 field is audited once despite successful recovery");
        }
        for(const auto& bad:std::vector<Bytes>{{5,note,r,1,10},{5,note,60,r,10},
                                             {5,uint8_t(high),r,10}}) {
            Bytes prefix={0,note,60,64,3},candidate=prefix,expected=prefix;
            append(candidate,bad);append(expected,{5,0xff,0});
            append(candidate,{10,uint8_t(high),64,10,20,0xff,0x2f,0});
            append(expected,{10,uint8_t(high),64,10,20,0xff,0x2f,0});
            check(renderAll(score(3,candidate,voice))==renderAll(score(3,expected,voice)),
                  "bad note/velocity is inert and cannot poison remembered velocity");
        }
    }
    // Partial fixed fields remain bounded and cannot produce a new note.
    for(const auto& tail:std::vector<Bytes>{{0,0x40},{0,0x60,0},{0,0x30,7},
                                           {0,0x10,60},{0,0x10,60,128}}) {
        auto f=score(3,tail);MaPlayer p;p.init(f,8000);
        check(!p.scoreAudit().scanComplete && !audible(rerender(p,257)),"partial MA7 fields fail safely");
    }
    // Valid normal keys are not narrowed to the authoring tool's 114-key range.
    for(int key:{0,114,115,127}) {
        auto f=score(3,{0,0x10,uint8_t(key),64,10,10,0xff,0x2f,0});MaPlayer p;
        check(p.init(f,8000)&&p.scoreAudit().scanComplete,"valid 7-bit keys retain compatibility");
    }
}
void vlq() {
    for(int format:{2,3}) {
        const uint8_t note=format==3?0x10:0x90;
        for(const auto& bad:std::vector<Bytes>{{0x81},{0x80,0x80,0x80,0x80,0x80},
                              {0x80,0x80,0x80,0x80,0x80,0, note,60,100,10}}) {
            Bytes gate={0,note,60,100};append(gate,bad);
            auto f=score(format,gate);MaPlayer p;p.init(f,8000);
            check(!p.scoreAudit().scanComplete&&!audible(rerender(p,257)),
                  "unterminated gate cannot create a fake note");
            for(const auto& prefix:std::vector<Bytes>{{},{0,0xf0}}) {
                Bytes seq=prefix;append(seq,bad);f=score(format,seq);p.init(f,8000);
                check(!p.scoreAudit().scanComplete&&!audible(rerender(p,31)),
                      "unterminated duration/exclusive stops current track");
            }
            if(format==3) {
                Bytes seq={0,0xff,1};append(seq,bad);f=score(format,seq);p.init(f,8000);
                check(!p.scoreAudit().scanComplete,"unterminated MA7 meta length is visible");
            }
            f=score(format,{0,note,60,100,10,10,0xff,0x2f,0});
            f.tracks[0].setupData={0xf0};append(f.tracks[0].setupData,bad);p.init(f,8000);
            check(!p.scoreAudit().scanComplete&&audible(rerender(p,257)),
                  "broken setup length preserves independent normal score");
        }
        // Preserve accepted terminal 5-byte VLQs (including leading zeros).
        auto ref=score(format,{0,note,60,100,10,10,0xff,0x2f,0});
        auto valid=score(format,{0x80,0x80,0x80,0x80,0,note,60,100,
                               0x80,0x80,0x80,0x80,10,10,0xff,0x2f,0});
        check(renderAll(valid)==renderAll(ref),"valid five-byte VLQs unchanged");
        auto broken=score(format,{0,note,60,100,10,10,note,64,100,0x81});
        check(renderAll(broken)==renderAll(ref),"broken later gate preserves normal prefix");
        auto other=score(format,{0,note,67,100,10,10,0xff,0x2f,0});other.tracks[0].trackNumber=1;
        auto expected=ref;expected.tracks.push_back(other.tracks[0]);
        broken.tracks.push_back(other.tracks[0]);
        check(renderAll(broken)==renderAll(expected),"broken length cannot stop other tracks");
        MaPlayer p;p.init(broken,8000);auto a=rerender(p,31);p.seekToStart();
        check(a==rerender(p,257),"recovered prefix deterministic after seek");
    }
}
}
void checkMa7Data() {
    ma7DataRegression::velocity();ma7DataRegression::invalidData();ma7DataRegression::vlq();
}
