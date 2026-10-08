#pragma once
// Synthetic bytes only. Explicit sample snapshots, not live hardware RAM writes.
void checkTimedWaves() {
    auto makeFixture=[](int flavor) { return fixture(flavor==5?2:flavor); };
    auto exclusive=[](std::vector<uint8_t>& seq, int delay, const std::vector<uint8_t>& image) {
        seq.push_back(uint8_t(delay)); seq.push_back(0xf0);
        uint32_t length=uint32_t(image.size()+1);
        std::vector<uint8_t> vlq{uint8_t(length&127)};
        while((length>>=7)) vlq.insert(vlq.begin(),uint8_t(128|(length&127)));
        seq.insert(seq.end(),vlq.begin(),vlq.end());
        seq.insert(seq.end(),image.begin(),image.end());seq.push_back(0xf7);
    };
    auto voice=[](int format,int pc,int id) {
        std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,124,0,uint8_t(pc),0,1,
            0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,uint8_t(id)};
        if(format==3) v={0x43,0x79,8,0x7f,0x21,124,0,uint8_t(pc),0,0,
            1,0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,0,80,uint8_t(id)};
        else if(format==5) v[2]=7;
        else {
            auto raw=std::vector<uint8_t>(v.begin()+10,v.end());v.resize(10);v[2]=6;
            auto packed=packMa3(raw);v.insert(v.end(),packed.begin(),packed.end());
        }
        return v;
    };
    auto wave=[](int format,int codec,int id,int variant,size_t bytes=80) {
        std::vector<uint8_t> v{0x43,0x79,uint8_t(format==3?8:format==5?7:6),0x7f,
            uint8_t(format==3?0x23:3),uint8_t(id),uint8_t(codec)};
        std::vector<uint8_t> raw(bytes);
        for(size_t i=0;i<bytes;++i) raw[i]=codec==0?uint8_t(variant==0?0x81:variant==1?0x29:0xd4):
            uint8_t((codec==3?0:128)+int((variant+1)*12*std::sin(double(i)*6.283185307179586/(20+variant*8))));
        if(format==2) raw=packMa3(raw);
        v.insert(v.end(),raw.begin(),raw.end());return v;
    };
    auto controllers=[](SmafFile& f,int channels) {
        auto& s=f.tracks[0].sequenceData;s.clear();
        for(int c=0;c<channels;++c) s.insert(s.end(),{0,uint8_t((f.tracks[0].formatType==3?0x30:0xb0)+c),0,124,
            0,uint8_t((f.tracks[0].formatType==3?0x40:0xc0)+c),uint8_t(c)});
    };
    auto note=[](SmafFile& f,int delay,int channel,int pitch,int gate) {
        auto& s=f.tracks[0].sequenceData;s.insert(s.end(),{uint8_t(delay),
            uint8_t((f.tracks[0].formatType==3?0x10:0x90)+channel),uint8_t(pitch),100,uint8_t(gate)});
    };
    auto eos=[](SmafFile& f) { auto& s=f.tracks[0].sequenceData;s.insert(s.end(),{30,0xff,0x2f,0}); };
    for(int format:{2,3,5}) for(int codec:{0,2,3}) {
        if(format!=3 && codec==3) continue;
        for(bool initial:{false,true}) for(int gate:{10,40}) {
            auto f=makeFixture(format),ref=makeFixture(format);
            setVoice(f,voice(format,0,1));setVoice(ref,voice(format,0,1));
            appendExclusive(ref,voice(format,1,2));appendExclusive(ref,voice(format,2,3));
            if(initial) {appendExclusive(f,wave(format,codec,1,0));appendExclusive(ref,wave(format,codec,1,0));}
            appendExclusive(ref,wave(format,codec,2,1));appendExclusive(ref,wave(format,codec,3,2));
            // Same-ID recorded Audio/Mwa must never supply custom instrument RAM.
            WaveData collision;collision.number=1;collision.adpcm=false;collision.bitsPerSample=8;
            collision.data.assign(80,255);f.tracks[0].waves.push_back(collision);ref.tracks[0].waves.push_back(collision);
            controllers(f,1);controllers(ref,3);
            note(f,0,0,60,gate);note(ref,0,0,60,gate);
            exclusive(f.tracks[0].sequenceData,20,wave(format,codec,1,1));
            note(f,0,0,64,gate);note(ref,20,1,64,gate);
            exclusive(f.tracks[0].sequenceData,20,wave(format,codec,1,2));
            note(f,0,0,67,10);note(ref,20,2,67,10);eos(f);eos(ref);
            const auto expected=renderAll(ref);
            check(renderAll(f)==expected,"inline waves affect future notes only, absent/initial/held/latest versions");
            check(renderAll(f,1)==expected && renderAll(f,512)==expected,"inline wave timing block independent");
            MaPlayer p;check(p.init(f,8000),"timed wave seek initializes");float b[514];
            while(p.render(b,257)>0){}p.seekToStart();std::vector<float> replay;int n;
            while((n=p.render(b,257))>0) replay.insert(replay.end(),b,b+n*2);
            check(replay==expected,"seek restores setup waves and replays versions deterministically");
        }
        auto ordered=makeFixture(format),orderedRef=makeFixture(format);
        setVoice(ordered,voice(format,0,1));setVoice(orderedRef,voice(format,0,1));
        appendExclusive(ordered,wave(format,codec,1,0));appendExclusive(orderedRef,wave(format,codec,1,0));
        appendExclusive(orderedRef,voice(format,1,2));appendExclusive(orderedRef,wave(format,codec,2,2));
        controllers(ordered,1);controllers(orderedRef,2);
        note(ordered,0,0,60,40);note(orderedRef,0,0,60,40);
        exclusive(ordered.tracks[0].sequenceData,0,wave(format,codec,1,1));
        exclusive(ordered.tracks[0].sequenceData,0,wave(format,codec,1,2));
        note(ordered,0,0,64,10);note(orderedRef,0,1,64,10);eos(ordered);eos(orderedRef);
        check(renderAll(ordered)==renderAll(orderedRef),"same-tick note before wave keeps old snapshot; latest definition wins");
        auto valid=makeFixture(format);setVoice(valid,voice(format,0,1));appendExclusive(valid,wave(format,codec,1,0));
        controllers(valid,1);note(valid,20,0,60,10);eos(valid);
        const auto preserved=renderAll(valid);
        for(int invalid:{0,1,2,3}) {
            auto bad=valid;controllers(bad,1);auto update=wave(format,codec,1,2);
            if(invalid==0) update[6]=4;
            if(invalid==1) update.resize(7);
            if(invalid==2) update=wave(format,codec,1,2,codec==0?32769:65537);
            if(invalid==3) {if(format==2) update[7]=128;else update[5]=128;}
            exclusive(bad.tracks[0].sequenceData,0,update);note(bad,20,0,60,10);eos(bad);
            check(renderAll(bad)==preserved,"malformed/unsupported/oversized wave cannot erase valid setup wave");
        }
    }
    // All three sequenced event kinds retain mutual order within a tick.
    // First note snapshots old RAM, then a new wave and a tone selecting that
    // wave activate, and the second note must bind both new definitions.
    for(int format:{2,3,5}) {
        auto f=makeFixture(format),ref=makeFixture(format);
        setVoice(f,voice(format,0,1));setVoice(ref,voice(format,0,1));
        appendExclusive(f,wave(format,2,1,0));appendExclusive(ref,wave(format,2,1,0));
        appendExclusive(ref,voice(format,1,2));appendExclusive(ref,wave(format,2,2,2));
        controllers(f,1);controllers(ref,2);note(f,0,0,60,40);note(ref,0,0,60,40);
        exclusive(f.tracks[0].sequenceData,0,wave(format,2,2,2));
        exclusive(f.tracks[0].sequenceData,0,voice(format,0,2));
        note(f,0,0,64,10);note(ref,0,1,64,10);eos(f);eos(ref);
        check(renderAll(f)==renderAll(ref),"wave/tone/note activations share stable same-tick stream order");
        check(renderAll(f,1)==renderAll(f,1024),"combined wave/tone activations are block independent");
    }
    // Strong aggregate guards: 512 small versions, or 128 maximum-size PCM8
    // versions (16 MiB). Rejected later versions must retain the accepted one.
    for(bool bytesGuard:{false,true}) {
        auto f=fixture(3),ref=fixture(3);setVoice(f,voice(3,0,1));setVoice(ref,voice(3,0,1));
        controllers(f,1);controllers(ref,1);
        const int accepted=bytesGuard?128:512;
        const auto kept=wave(3,2,1,1,bytesGuard?65536:80);
        for(int i=0;i<accepted;++i) exclusive(f.tracks[0].sequenceData,0,kept);
        exclusive(f.tracks[0].sequenceData,0,wave(3,2,1,2));
        appendExclusive(ref,kept);note(f,0,0,60,10);note(ref,0,0,60,10);eos(f);eos(ref);
        check(renderAll(f)==renderAll(ref),"inline wave aggregate byte/version flood guard preserves last accepted wave");
    }
    // Event ceiling reached before a wave decoder is called. Both streams end
    // at the same accepted controller and keep the same initial sample memory.
    auto flood=fixture(3),control=fixture(3);
    for(auto* f:{&flood,&control}) {
        setVoice(*f,voice(3,0,1));appendExclusive(*f,wave(3,2,1,0));controllers(*f,1);note(*f,0,0,60,10);
        for(int i=0;i<400000;++i) f->tracks[0].sequenceData.insert(f->tracks[0].sequenceData.end(),{0,0x30,1,0});
    }
    exclusive(flood.tracks[0].sequenceData,0,wave(3,2,1,2,65536));
    check(renderAll(flood)==renderAll(control),"event ceiling rejects inline wave before allocation without changing output");
}
