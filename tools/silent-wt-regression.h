// SPDX-License-Identifier: Apache-2.0
#pragma once
// Instrument WT stopped attacks: exact silence must preserve authored events.
namespace silentWtRegression {
using Bytes=std::vector<uint8_t>;
Bytes tone(bool loop,bool xof,int program=0,bool stopped=true) {
    Bytes v{0x43,0x79,7,0x7f,1,0,0,uint8_t(program),0,1,
        0x1f,0x40,0,0,uint8_t(xof?8:0),0xf0,uint8_t(stopped?0:0xf0),0,0,
        0,0,0,uint8_t(loop?0:80),0,80,1,0,0,0};
    auto p=parseVoiceExclusive(v.data(),v.size());
    check(p.valid&&p.isPcm&&p.pcm.loop==loop&&p.pcm.env.xof==xof
          &&p.pcm.env.ar==(stopped?0:15)&&p.pcm.env.rateLowBits==0
          &&p.pcm.waveId==1&&p.pcm.fs==8000&&p.pcm.endPt==80,
          "WT stopped-attack fixture fields decode exactly");
    return v;
}
SmafFile score(bool loop=true,bool xof=true,bool mute=false) {
    auto f=fixture(); addPcmFixture(f); setVoice(f,tone(loop,xof));
    check(std::any_of(f.tracks[0].waves[0].data.begin(),f.tracks[0].waves[0].data.end(),
                     [](uint8_t x){return x!=0;}),"silent WT fixture has nonzero source samples");
    auto& s=f.tracks[0].sequenceData; s={0,0x90,60,100,10};
    if(mute)s.insert(s.end(),{10,0xb0,7,0,10,0xff,0x2f,0});
    else s.insert(s.end(),{20,0xff,0x2f,0});
    return f;
}
void matrix() {
    for(bool loop:{false,true})for(bool xof:{false,true})for(bool mute:{false,true}) {
        const auto f=score(loop,xof,mute);
        std::vector<float> expected(1600*2,0);
        for(int block:{1,257,1024}) {
            MaPlayer p; check(p.init(f,8000),"WT matrix initializes");
            check(silentAttackRegression::drain(p,block)==expected&&p.scoreEndSamples()==1600,
                  "all eight WT loop/XOF/Volume0 cases end at exact authored 1600 frames");
            auto d=p.diagnostics();
            check(d.pcmNotes==1&&d.fmNotes==0&&d.audioNotes==0&&d.fallbackNotes==0&&!d.tailLimitReached,
                  "silent instrument WT uses PCM path without safety tail");
            if(block==257) {p.seekToStart();check(silentAttackRegression::drain(p,block)==expected,
                "WT stopped-attack seek restores exact endpoint and output");}
        }
    }
}
void futureEvents() {
    for(bool later:{false,true}) {
        auto f=score(); appendExclusive(f,tone(true,false,1,false));
        auto& s=f.tracks[0].sequenceData;
        s={0,0x90,60,100,10};
        if(later)s.insert(s.end(),{100,0xc1,1,0,0x91,60,100,10,20,0xff,0x2f,0});
        else s.insert(s.end(),{60,0xff,0x2f,0});
        auto reference=f;
        reference.tracks[0].sequenceData.erase(reference.tracks[0].sequenceData.begin(),
                                               reference.tracks[0].sequenceData.begin()+5);
        const auto expected=renderAll(reference);
        for(int block:{1,257,1024}) {
            MaPlayer p;check(p.init(f,8000),"WT future-event fixture initializes");
            const auto actual=silentAttackRegression::drain(p,block);
            check(actual==expected&&!p.diagnostics().tailLimitReached,
                  "silent WT companion preserves trailing rests and later audible instrument output");
            check(actual.size()/2>=p.scoreEndSamples(),"WT authored duration is retained");
            if(later)check(std::any_of(actual.begin()+16000,actual.end(),[](float x){return x!=0;}),
                           "later WT note actually produces output");
            if(block==257){p.seekToStart();check(silentAttackRegression::drain(p,block)==expected,
                                               "WT seek preserves later events");}
        }
    }
}
void nonzeroAttacks() {
    for(bool odd:{false,true}) {
        auto f=score(); Bytes v;
        if(odd) {
            v={0x43,0x79,8,0x7f,0x21,0,0,0,0,0,1,
               0x1f,0x40,0,0,8,0xf0,0,0,0,8,0,0,0,0,0,80,1};
            auto p=parseVoiceExclusive(v.data(),v.size());
            check(p.valid&&p.isPcm&&p.pcm.env.ar==0&&p.pcm.env.rateLowBits==8
                  &&p.pcm.env.xof&&p.pcm.loop&&p.pcm.waveId==1&&p.pcm.fs==8000,
                  "MA7 WT raw AR0 retains its nonzero attack low bit");
        } else {
            v=tone(true,true,0,false);v[17]=0xfc;
            auto p=parseVoiceExclusive(v.data(),v.size());
            check(p.valid&&p.isPcm&&p.pcm.env.ar==15&&p.pcm.env.tl==63,
                  "tiny WT fixture retains a live attack and maximum attenuation");
            f.tracks[0].sequenceData.insert(f.tracks[0].sequenceData.begin(),
                {0,0xb0,7,1,0,0xb0,11,1});
            f.tracks[0].sequenceData[11]=1; // note velocity
        }
        setVoice(f,v);
        if(odd) {
            // MA7 WT binds instrument RAM, never a same-ID stream wave.
            Bytes bulk{0x43,0x79,8,0x7f,0x23,1,2};
            const auto& source=f.tracks[0].waves[0].data;
            for(size_t i=0;i<source.size();i+=2) {
                int16_t sample=int16_t((uint16_t(source[i])<<8)|source[i+1]);
                bulk.push_back(uint8_t(sample/256+128));
            }
            appendExclusive(f,bulk);
        }
        MaPlayer p;check(p.init(f,8000),"nonzero WT fixture initializes");
        auto out=silentAttackRegression::drain(p);float peak=0;
        for(float x:out)peak=std::max(peak,std::fabs(x));
        check(peak>0&&out.size()/2==81600&&p.diagnostics().tailLimitReached
              &&p.diagnostics().pcmNotes==1&&p.diagnostics().audioNotes==0,
              "nonzero WT attack retains existing XOF safety-tail duration");
        if(!odd)check(peak<1e-7f,"tiny nonzero WT remains protected below filter drain floor");
    }
}
void test(){matrix();futureEvents();nonzeroAttacks();}
} // namespace silentWtRegression
