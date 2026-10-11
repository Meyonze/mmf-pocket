// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic timing/terminator fixtures, not copyrighted song data.
namespace scoreNopRegression {
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,const Bytes& in){out.insert(out.end(),in.begin(),in.end());}
Bytes duration(int format,uint32_t value) {
    if(format!=0)return scoreLengthRegression::vlq(value);
    check(value<=16511,"synthetic HPS duration within two-byte range");
    if(value<128)return {uint8_t(value)};
    value-=128;return {uint8_t(128|(value>>7)),uint8_t(value&127)};
}
Bytes nop(int format,uint32_t ticks) {auto b=duration(format,ticks);append(b,{0xff,0});return b;}
Bytes control(int format,uint32_t ticks) {
    auto b=duration(format,ticks);
    // Independent no-change channel pan keeps the same sound but has always
    // been scheduled. It is the terminal-time oracle, not another NOP.
    append(b,format==0?Bytes{0,0x3a,64}:Bytes{uint8_t(format==3?0x30:0xb0),10,64});return b;
}
SmafFile score(int format,const Bytes& sequence,bool pcm=false) {
    auto f=fixture(format);f.tracks[0].sequenceData=sequence;
    if(pcm)addPcmFixture(f,1,15);
    return f;
}
Bytes note(int format,int key=60,int gate=3) {
    if(format==0) {
        int note=key%12,octave=(key-36)/12;
        if(note==0){note=12;--octave;} // specification C encoding, identical MIDI pitch
        check(octave>=0&&octave<4,"synthetic HPS key within legal four-octave range");
        return {0,uint8_t((octave<<4)|note),uint8_t(gate)};
    }
    return {0,uint8_t(format==3?0x10:0x90),uint8_t(key),100,uint8_t(gate)};
}
std::vector<float> drain(MaPlayer& player,int block=257) {
    std::vector<float> out,buffer(size_t(block)*2);int count;
    while((count=player.render(buffer.data(),block))>0) {
        out.insert(out.end(),buffer.begin(),buffer.begin()+2*count);
        check(out.size()<=8000*2*600,"NOP render stays inside600-second ceiling");
    }
    return out;
}
void timeline() {
    for(int format:{0,2,3})for(bool pcm:{false,true}) {
        auto prefix=note(format);auto actual=score(format,prefix,pcm),oracle=actual;
        append(actual.tracks[0].sequenceData,nop(format,100));
        append(oracle.tracks[0].sequenceData,control(format,100));
        const auto expected=renderAll(oracle),audio=renderAll(actual);
        check(audio==expected,"late score NOP preserves time exactly like independent no-change control");
        const auto without=renderAll(score(format,prefix,pcm));
        check(audio.size()>=without.size()&&std::equal(without.begin(),without.end(),audio.begin()),
              "NOP cannot modify sound/filter/decay before the natural end");
        if(pcm)check(audio.size()==8000*2,"fast-release WT oracle really retains silence until1000ms");
        MaPlayer p;check(p.init(actual,8000)&&p.scoreEndSamples()==8000,"NOP endpoint preserves exact10ms clock");
        auto first=drain(p,1);p.seekToStart();check(first==drain(p,1024),"NOP seek and block invariance");
        if(format==2)check(renderAll(channelModeRegression::compressed(actual))==audio,
                           "packed Mobile NOP transport preserves the same timeline");
        auto split=score(format,prefix,pcm);append(split.tracks[0].sequenceData,nop(format,40));
        append(split.tracks[0].sequenceData,nop(format,60));
        check(renderAll(split)==audio,"split NOP durations add without resetting sound state");
        auto same=score(format,prefix,pcm);
        same.tracks[0].sequenceData.insert(same.tracks[0].sequenceData.begin(),{0,0xff,0});
        append(same.tracks[0].sequenceData,nop(format,0));
        check(renderAll(same)==without,"zero-time NOP is inert around NoteOn and gate scheduling");
        auto extended=note(format);append(extended,nop(format,3));
        check(renderAll(score(format,extended,pcm))==without,"same-time NOP and gate do not alter ordering or release");
        auto after=note(format);append(after,nop(format,5));auto next=note(format,64);append(after,next);
        auto unsplit=note(format);auto direct=note(format,64);direct[0]=5;append(unsplit,direct);
        check(renderAll(score(format,after,pcm))==renderAll(score(format,unsplit,pcm)),
              "NOP duration belongs to the following note; it cannot reset the channel clock");
    }
    // A nondecaying XOF patch makes the safety-horizon bug audible. NOP must
    // match an existing inert-control horizon, not retune or kill the voice.
    for(int format:{2,3}) {
        auto f=score(format,note(format));
        Bytes voice{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
            8,0x50,0xf0,0xfc,0,0x10,0,8,0x50,0xf0,0,0,0x10,0};setVoice(f,voice);
        auto oracle=f;append(f.tracks[0].sequenceData,nop(format,1200));
        append(oracle.tracks[0].sequenceData,control(format,1200));
        check(renderAll(f)==renderAll(oracle),"late NOP uses authored horizon for held/XOF safety without a new mute law");
    }
}
void eos() {
    for(int format:{2,3})for(bool pcm:{false,true}) {
        const auto prefix=score(format,note(format),pcm);const auto expected=renderAll(prefix);
        for(int value=1;value<256;++value) {
            auto broken=prefix;append(broken.tracks[0].sequenceData,{10,0xff,0x2f,uint8_t(value)});
            append(broken.tracks[0].sequenceData,note(format,67));
            check(renderAll(broken)==expected,"bad EOS cannot mute prefix or decode fake suffix notes");
            MaPlayer p;check(p.init(broken,8000)&&!p.scoreAudit().scanComplete&&
                p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]==1,
                "nonzero EOS byte audited malformed exactly once");
            if(format==2&&value==128) {
                const auto packed=channelModeRegression::compressed(broken);
                check(renderAll(packed)==expected,"broken compressed EOS preserves prefix");
                check(p.init(packed,8000)&&!p.scoreAudit().scanComplete,"compressed EOS audit retained");
            }
        }
        auto missing=prefix;append(missing.tracks[0].sequenceData,{10,0xff,0x2f});
        MaPlayer p;check(p.init(missing,8000)&&!p.scoreAudit().scanComplete&&
                        p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]==1,
                        "missing final EOS byte cannot be silently accepted");
        check(drain(p)==expected,"missing EOS retains previous natural decay");
        p.seekToStart();check(drain(p,31)==expected,"broken EOS deterministic on seek");
        auto other=score(format,note(format,67),pcm);other.tracks[0].trackNumber=1;
        if(format==3)other.tracks[0].sequenceData[1]|=1; // separate existing global channel
        auto mixed=missing,oracle=prefix;mixed.tracks.push_back(other.tracks[0]);oracle.tracks.push_back(other.tracks[0]);
        check(renderAll(mixed)==renderAll(oracle),"broken EOS cannot stop an independent track");
        auto valid=prefix;append(valid.tracks[0].sequenceData,{10,0xff,0x2f,0});
        append(valid.tracks[0].sequenceData,note(format,67));
        auto validExpected=prefix;
        append(validExpected.tracks[0].sequenceData,{10,0xff,0});
        check(renderAll(valid)==renderAll(validExpected),"valid EOS retains release policy, Mobile/MA7 clock, and skips suffix");
        check(p.init(valid,8000)&&p.scoreAudit().scanComplete,"valid EOS remains complete coverage");
        if(format==2)check(renderAll(channelModeRegression::compressed(valid))==renderAll(validExpected),
                           "valid compressed EOS clock matches explicit timed NOP");
    }
    // HPS four-zero EOS and ATR source-specific end logic are deliberately
    // unchanged. Do not import the Mobile three-byte terminator grammar.
    auto hps=score(0,note(0)),valid=hps;append(valid.tracks[0].sequenceData,{0,0,0,0});
    append(valid.tracks[0].sequenceData,note(0,67));
    check(renderAll(valid)==renderAll(hps),"HPS EOS behavior remains unchanged");
}
void limits() {
    for(int format:{0,2,3}) {
        auto f=score(format,nop(format,100));MaPlayer p;
        check(p.init(f,8000)&&p.scoreAudit().scanComplete,"NOP-only input follows existing control-only policy");
        check(drain(p).size()==16000&&p.diagnostics().fmNotes+p.diagnostics().pcmNotes+p.diagnostics().audioNotes==0,
              "NOP-only input retains authored silence without allocating sound");
        check(p.init(score(format,nop(format,0)),8000)&&drain(p).empty(),"zero-time NOP-only ends immediately");
        Bytes sequence;for(int i=0;i<400001;++i)append(sequence,nop(format,0));
        check(p.init(score(format,sequence),8000)&&!p.scoreAudit().scanComplete&&
              p.scoreAudit().totals[size_t(ScoreAudit::Status::ResourceLimit)]==1&&drain(p).empty(),
              "explicit NOP flood is bounded by decoded-event cap and audited once");
        // Use a low synthetic output rate for the hard-cap test; no long WAV.
        sequence.clear();for(int i=0;i<6;++i)append(sequence,nop(format,12000));
        check(p.init(score(format,sequence),100)&&p.scoreEndSamples()==72000&&p.totalSamples()==60000,
              "large NOP clock retains authored endpoint and existing600-second ceiling");
        std::array<float,512> buffer{};int total=0,count;
        while((count=p.render(buffer.data(),256))>0){total+=count;for(float x:buffer)check(x==0,"capped NOP remains silent");}
        check(total==60000&&p.finished(),"NOP-only cannot extend rendering beyond hard cap");
    }
}
}
void checkScoreNops(){scoreNopRegression::timeline();scoreNopRegression::eos();scoreNopRegression::limits();}
