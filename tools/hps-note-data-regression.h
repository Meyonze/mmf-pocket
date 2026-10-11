// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original HPS NoteNumber fixtures, no private score or ROM data.
namespace hpsNoteDataRegression {
using Bytes=std::vector<uint8_t>;
using hpsMetaRegression::score;
void append(Bytes& out,const Bytes& more){out.insert(out.end(),more.begin(),more.end());}
bool forbidden(int code){return (code&15)==0 || (code&15)>=13;}
void rejected() {
    for(int voice:{0,1,2})for(int code=1;code<255;++code) {
        if(!forbidden(code))continue; //00 is a control prefix,FF a system prefix
        const int channel=code>>6;
        for(int gate:{1,127,128,16511}) {
            Bytes seq={0,uint8_t((channel<<6)|0x21),30,5,uint8_t(code)};
            append(seq,atrPanRegression::length(gate));
            append(seq,{5,uint8_t((channel<<6)|0x24),20});
            Bytes ref={0,uint8_t((channel<<6)|0x21),30,5,0xff,0,
                       5,uint8_t((channel<<6)|0x24),20};
            auto actual=score(seq,voice),oracle=score(ref,voice);
            const auto expected=renderAll(oracle);
            check(ma7DataRegression::audible(expected)&&renderAll(actual)==expected,
                  "forbidden HPS NoteNumber neither sounds/retriggers nor leaves a generated gate; following time remains");
            MaPlayer p;check(p.init(actual,8000),"forbidden-note prefix stays playable");
            const auto& a=p.scoreAudit();
            check(a.scanComplete&&a.totals[size_t(ScoreAudit::Status::Reserved)]==1&&
                  a.totals[size_t(ScoreAudit::Status::Malformed)]==0,
                  "known-width forbidden NoteNumber is reserved, not a lost boundary");
            bool found=false;for(const auto& g:a.groups)
                if(g.domain==ScoreAudit::Domain::Sequence&&g.opcode==0x105)
                    found=g.channel==channel&&g.minValue==(code&15)&&g.maxValue==(code&15)&&g.firstSample==400;
            check(found,"forbidden-note audit retains field value/channel/time");
            if(gate==128) {
                auto first=ma7DataRegression::rerender(p,31);p.seekToStart();
                check(first==ma7DataRegression::rerender(p,1024),"forbidden-note seek/block invariance");
                auto independent=score({0,0x27,40},voice);independent.tracks[0].trackNumber=1;
                actual.tracks.push_back(independent.tracks[0]);oracle.tracks.push_back(independent.tracks[0]);
                check(renderAll(actual)==renderAll(oracle),"forbidden note cannot stop an independent HPS track");
            }
        }
    }
    for(int code:{0x10,0x2d,0x6e,0xfe}) {
        auto f=score({20,uint8_t(code),100}),oracle=score({20,0xff,0});
        MaPlayer p;check(p.init(f,8000)&&p.scoreEndSamples()==1600,
                        "forbidden-only note retains duration, not forbidden100-tick gate");
        auto audio=ma7DataRegression::rerender(p,257);
        check(audio==renderAll(oracle)&&!ma7DataRegression::audible(audio)&&
              p.diagnostics().fmNotes+p.diagnostics().pcmNotes+p.diagnostics().audioNotes==0,
              "forbidden-only note is a silent timeline without voice allocation");
    }
}
void unchanged() {
    // Every192 legal channel/octave/key encoding remains playable; C is12,
    // never0. Default HPS mono and paired gate policies are not retuned.
    for(int voice:{0,1,2})for(int channel=0;channel<4;++channel)for(int octave=0;octave<4;++octave)
        for(int note=1;note<=12;++note) {
            auto f=score({0,uint8_t((channel<<6)|(octave<<4)|note),3},voice);
            MaPlayer p;check(p.init(f,8000)&&p.scoreAudit().scanComplete&&
                p.scoreAudit().totals[size_t(ScoreAudit::Status::Reserved)]==0,
                "every legal HPS key remains unmodified and unaudited");
            check(ma7DataRegression::audible(ma7DataRegression::rerender(p,257)),
                  "legal HPS notes including highest channel/octave C remain audible");
        }
    // Fixed-width validation must not borrow score NoteNumber rules for ATR
    // six-bit WaveID. Includes wave IDs whose low nibble is forbidden in HPS.
    for(int wave:{13,14,15,16,29,30,31,32,45,46,47,48,61,62}) {
        auto f=atrFormatRegression::handBuilt(0,1,8000,0,0,atrGateRegression::tone(400),
                                             {0,uint8_t(wave),50});
        f.tracks[0].waves[0].number=wave;
        auto ref=atrFormatRegression::handBuilt(0,1,8000,0,0,atrGateRegression::tone(400),{0,1,50});
        check(renderAll(f)==renderAll(ref),"ATR legal WaveID is not clipped as HPS forbidden key");
    }
    // Keep the existing zero-gate compatibility policy and sentinel grammars.
    auto f=score({0,0x21,30,5,0x24,0,5,0x27,20});
    auto ref=score({0,0x21,30,10,0x27,20});
    check(renderAll(f)==renderAll(ref),"HPS zero gate stays skipped while following duration accumulates");
    hpsMetaRegression::unchanged();
}
}
void checkHpsNoteData(){hpsNoteDataRegression::rejected();hpsNoteDataRegression::unchanged();}
