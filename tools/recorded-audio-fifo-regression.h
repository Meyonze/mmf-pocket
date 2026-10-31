// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic signals test the renderer's oldest-first Audio contract.
// No claim about measured handset voice allocation or synthesis calibration.
namespace recordedAudioFifoRegression {
struct Note { int id, at=0, gate=50, velocity=127, track=0; };
using Bytes=std::vector<uint8_t>;
void append(Bytes& out,std::initializer_list<uint8_t> more){out.insert(out.end(),more);}
int16_t source(int id,int frame) {
    return int16_t(800*std::sin((id+1)*0.031*frame)+70*(id+1));
}
void sample(Bytes& out,int16_t value) {
    out.push_back(uint8_t(uint16_t(value)>>8));out.push_back(uint8_t(value));
}
SmafFile score(int format,const std::vector<Note>& notes,int tracks=1,int shortWave=-1) {
    SmafFile f;
    for(int track=0;track<tracks;++track) {
        auto t=fixture(format).tracks[0];t.trackNumber=track;
        t.channelStatus.resize(format==3?32:16);
        uint8_t cc=format==3?0x30:0xb0,on=format==3?0x10:0x90;
        t.sequenceData={0,cc,0,125,0,cc,32,0};
        int previous=0;
        for(const auto& note:notes)if(note.track==track) {
            check(note.at>=previous,"FIFO fixture events are chronological within MTR");
            append(t.sequenceData,{uint8_t(note.at-previous),on,uint8_t(note.id),
                                  uint8_t(note.velocity),uint8_t(note.gate)});
            previous=note.at;
        }
        append(t.sequenceData,{uint8_t(60-previous),0xff,0x2f,0});
        for(int id=0;id<8;++id) {
            WaveData w;w.number=id+1;w.adpcm=false;w.bitsPerSample=16;w.samplingRate=8000;
            for(int n=0;n<(id==shortWave?80:6000);++n)sample(w.data,source(id,n));
            t.waves.push_back(w);
        }
        f.tracks.push_back(t);
    }
    return f;
}
std::vector<float> drain(MaPlayer& p,int block) {
    std::vector<float> out,buffer(2*block);int n;
    while((n=p.render(buffer.data(),block))>0){out.insert(out.end(),buffer.begin(),buffer.begin()+2*n);
        check(out.size()<20000,"FIFO fixture finishes within authored duration");}
    check(!p.diagnostics().tailLimitReached,"FIFO fixture ends naturally");return out;
}
void simultaneous() {
    for(int format:{2,3})for(int count:{4,5,8})for(bool multi:{false,true}) {
        std::vector<Note> notes;
        for(int j=0;j<count;++j)notes.push_back({j,0,j<count-2?j+1:30+10*(j-count+2),127,multi?j:0});
        // Remove only notes stolen before the first sample; independently
        // authored final two have no allocation contest and retain their gates.
        std::vector<Note> survivors(notes.end()-2,notes.end());
        auto actual=score(format,notes,multi?count:1);
        const auto expected=renderAll(score(format,survivors,multi?count:1));
        check(renderAll(actual)==expected,"same-timestamp Audio keeps last two successful triggers");
        MaPlayer p;check(p.init(actual,8000),"Audio FIFO replay initializes");
        check(drain(p,1)==expected,"same-timestamp FIFO is block independent");p.seekToStart();
        check(drain(p,1024)==expected,"FIFO seek resets allocation order and stale gates");
        check(p.diagnostics().audioNotes==uint64_t(count)&&p.diagnostics().stolenAudio==uint64_t(count-2)&&
              p.diagnostics().stolenPcm==0,"FIFO steals only the two-slot Audio pool");
    }
}
void mixedTimes() {
    for(int format:{2,3}) {
        // Track0 is decoded first but plays last. The early triple's first
        // trigger is stolen before output. Its last two are a two-note oracle.
        std::vector<Note> notes{{3,4,40,127,0},{0,0,1,127,1},{1,0,30,127,1},{2,0,40,127,1}};
        auto actual=score(format,notes,2);notes.erase(notes.begin()+1);
        const auto expected=renderAll(score(format,notes,2));
        check(renderAll(actual)==expected,"Audio FIFO follows playback order across MTR decode order");
        MaPlayer p;check(p.init(actual,8000),"mixed-time FIFO initializes");
        check(drain(p,31)==expected,"old stolen gate cannot stop replacement Audio");p.seekToStart();
        check(drain(p,257)==expected,"mixed-time FIFO seek preserves playback ordering");
    }
}
struct Span { int id,begin,end; };
void prefixOracle(int format,const std::vector<Note>& notes,const std::vector<Span>& spans,
                  int shortWave,uint64_t steals) {
    // Author the expected retained sources explicitly, then sum their integer
    // PCM into ONE independent recording. This bypasses allocation entirely.
    auto expectedFile=score(format,{{0}},1);
    auto& wave=expectedFile.tracks[0].waves[0];wave.data.clear();
    for(int frame=0;frame<6000;++frame) {
        int value=0;
        for(const auto& s:spans)if(frame>=s.begin&&frame<s.end)value+=source(s.id,frame-s.begin);
        sample(wave.data,int16_t(value));
    }
    MaPlayer p,reference;check(p.init(score(format,notes,1,shortWave),8000)&&reference.init(expectedFile,8000),
                             "FIFO independent waveform oracle initializes");
    std::vector<float> actual(2048),expected(2048);
    check(p.render(actual.data(),1024)==1024&&reference.render(expected.data(),1024)==1024,"FIFO oracle prefix length");
    for(size_t i=0;i<actual.size();++i)check(std::fabs(actual[i]-expected[i])<1e-7,
        "Audio retained sources match independently mixed recording");
    check(p.diagnostics().stolenAudio==steals&&p.diagnostics().limitedFrames==0,
          "Audio FIFO allocation count and quiet oracle signal");
}
void controls() {
    for(int format:{2,3}) {
        prefixOracle(format,{{0,0},{1,2},{2,4},{3,6}},
                     {{0,0,320},{1,160,480},{2,320,1024},{3,480,1024}},-1,2);
        // An expired short source frees a slot. Later stealing must select the
        // still-live old source, not the voice that reused the free slot.
        prefixOracle(format,{{0,0},{1,0},{2,2},{3,4}},
                     {{0,0,80},{1,0,320},{2,160,1024},{3,320,1024}},0,1);
        auto clean=score(format,{{0,0},{1,0},{2,4},{3,4}});
        auto ignored=score(format,{{0,0},{1,0},{9,2},{6,2,50,1},{2,4},{3,4}});
        MaPlayer p;check(p.init(ignored,8000),"ignored FIFO triggers initialize");
        check(drain(p,257)==renderAll(clean),"missing and muted Audio triggers do not displace live sources");
        check(p.diagnostics().audioNotes==4&&p.diagnostics().missingAudio==1&&p.diagnostics().stolenAudio==2,
              "Audio FIFO counts only successful allocation");
        // Reinitializing a used player restores the same complete result.
        check(p.init(clean,8000)&&drain(p,31)==renderAll(clean),"Audio FIFO init resets previous player ordering");
    }
}
}
void checkRecordedAudioFifo() {
    recordedAudioFifoRegression::simultaneous();
    recordedAudioFifoRegression::mixedTimes();
    recordedAudioFifoRegression::controls();
}
