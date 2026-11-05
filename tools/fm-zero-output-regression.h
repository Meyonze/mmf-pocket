// SPDX-License-Identifier: Apache-2.0
#pragma once
// Exact current-control silence, not quiet samples or slot retirement.
namespace fmZeroOutputRegression {
using namespace silentAttackRegression;

FmVoicePatch voice(int algorithm,int reason) {
    auto p=patch(algorithm,false);
    for(int i=0;i<(p.fourOp?4:2);++i) {
        p.ops[i].multi=1;
        if(carriers[algorithm]&(1u<<i)) {
            if(reason==0)p.ops[i].multi=15;
            else p.ops[i].wave=uint8_t(reason);
        }
    }
    return p;
}

void carriersAndControls() {
    for(int algorithm=0;algorithm<8;++algorithm)for(int reason:{0,15,23,31}) {
        const auto p=voice(algorithm,reason);
        FmVoice v,unqueried;
        v.setSampleRate(8000);unqueried.setSampleRate(8000);
        v.noteOn(p,440,1);unqueried.noteOn(p,440,1);
        check(v.active()&&v.muted(),"zero-output carriers are recognized while slots stay live");
        for(int i=0;i<257;++i) {
            check(v.muted(),"silence query is stable through live modulation and feedback");
            check(v.tick()==unqueried.tick(),"silence query does not change oscillator or feedback state");
        }
        v.noteOff();check(v.active()&&v.muted(),"XOF zero-output carrier is not retired by the query");
        if(reason==0) {
            v.setPitch(110);unqueried.setPitch(110);
            check(!v.muted(),"pitch can revive Nyquist-suppressed carriers without a new note");
            float peak=0;
            for(int i=0;i<128;++i) {
                const float s=v.tick();
                check(s==unqueried.tick(),"pitch revival preserves integrated phase and envelope");
                peak=std::max(peak,std::fabs(s));
            }
            check(peak>0,"revived carriers actually produce nonzero output");
        }
        for(int live=0;live<(p.fourOp?4:2);++live)if(carriers[algorithm]&(1u<<live)) {
            auto mixed=p;mixed.ops[live].multi=1;mixed.ops[live].wave=0;
            v.noteOn(mixed,440,1);v.setVolume(1e-12f);
            check(!v.muted(),"every individual live carrier protects a quiet tail");
            float peak=0;for(int i=0;i<128;++i)peak=std::max(peak,std::fabs(v.tick()));
            check(peak>0&&peak<1e-7f,"tiny nonzero carrier is not treated as exact silence");
            v.setVolume(1);
        }
    }
    // Instantaneous waveform zeros do not prove silence throughout a future
    // vibrato cycle. Legacy retains its nominal-frequency suppression policy.
    for(bool modern:{false,true}) {
    auto p=voice(1,0);p.modernTimbre=modern;
    for(auto& op:p.ops){op.multi=1;op.wave=25;op.vib=true;op.dvb=3;}
    FmVoice v;v.setSampleRate(8000);v.noteOn(p,3990,1);v.setVibratoScale(8);
    bool zero=false,nonzero=false;
    for(int i=0;i<1000;++i){check(!v.muted(),"local vibrato or a zero sample cannot prove silence");
        const auto s=v.tick();zero=zero||s==0;nonzero=nonzero||s!=0;}
    check(zero&&nonzero,"vibrato control fixture spans zero and nonzero waveform regions");
    v.setPitch(4000);check(v.muted()==!modern,"modern vibrato can return below nominal Nyquist");
    bool recovered=false;
    for(int i=0;i<8000;++i) {
        const float s=v.tick();recovered=recovered||s!=0;
        if(!modern)check(s==0,"legacy vibrato retains nominal Nyquist suppression");
    }
    check(recovered==modern,"modern vibrato actually resumes within its LFO cycle");
    v.setVibratoScale(0);check(v.muted(),"CC1 OFF makes nominal Nyquist silence permanent");
    v.setPitch(3990);check(!v.muted(),"later base pitch clears suppression");
    }
}

void outputGraphAndSelectors() {
    for(bool modern:{false,true}) {
        for(int algorithm=0;algorithm<8;++algorithm)for(int slot=0;slot<(algorithm<2?2:4);++slot) {
            auto p=patch(algorithm,false);p.modernTimbre=modern;
            for(auto& op:p.ops){op.wave=15;op.multi=1;}
            p.ops[slot].wave=0;
            FmVoice v;v.setSampleRate(8000);v.noteOn(p,440,1);
            const bool muted=v.muted();bool output=false;
            for(int i=0;i<128;++i)output=output||v.tick()!=0;
            // Observe the synthesis graph instead of consulting carrier masks.
            check(muted==!output,"every isolated operator agrees with the actual output graph");
        }
        for(int wave=0;wave<256;++wave) {
            auto p=patch(1,false);p.modernTimbre=modern;
            for(auto& op:p.ops){op.wave=uint8_t(wave);op.multi=1;}
            FmVoice v;v.setSampleRate(8000);v.noteOn(p,440,1);
            const bool muted=v.muted();bool output=false;
            for(int i=0;i<128;++i)output=output||v.tick()!=0;
            check(muted==!output,"all waveform selectors and masked aliases agree with generated output");
        }
    }
}

Bytes image(int algorithm,int reason,int program=0,bool xof=true) {
    auto v=tone(algorithm,false,program,xof);
    for(int i=0;i<(algorithm<2?2:4);++i)if(carriers[algorithm]&(1u<<i)) {
        if(reason==0)v[13+i*7+5]=0x40;
        else v[13+i*7+6]=uint8_t(reason<<3);
    }
    auto p=parseVoiceExclusive(v.data(),v.size());
    check(p.valid&&!p.isPcm,"zero-output fixture contains a complete supported FM image");
    return v;
}

SmafFile score(int algorithm,int reason,int mode=0) {
    auto f=fixture();setVoice(f,image(algorithm,reason));
    appendExclusive(f,tone(algorithm,false,1,false));
    auto& s=f.tracks[0].sequenceData;s={0,0x90,84,100,10};
    if(mode==0)s.insert(s.end(),{20,0xff,0x2f,0});
    if(mode==1)s.insert(s.end(),{60,0xff,0x2f,0});
    if(mode==2)s.insert(s.end(),{100,0xc1,1,0,0x91,60,100,10,20,0xff,0x2f,0});
    // Keep the same note alive through a later full negative bend (-2 semitones).
    if(mode==3)s={0,0x90,84,100,120,100,0xe0,0,0,30,0xff,0x2f,0};
    return f;
}

void endpointsAndFutureEvents() {
    for(int algorithm=0;algorithm<8;++algorithm)for(int reason:{0,15,23,31}) {
        const auto f=score(algorithm,reason);
        for(int block:{1,257,1024}) {
            MaPlayer p;check(p.init(f,8000),"zero-output score initializes");
            const auto out=drain(p,block);
            check(out.size()==3200&&std::all_of(out.begin(),out.end(),[](float x){return x==0;}),
                  "all topologies end exact silence at authored200ms without ten-second tail");
            check(p.diagnostics().fmNotes==1&&!p.diagnostics().fallbackNotes&&!p.diagnostics().tailLimitReached,
                  "embedded zero-output FM keeps its route and avoids the safety limit");
            if(block==257){p.seekToStart();check(drain(p,block)==out,"zero-output seek preserves endpoint");}
        }
    }
    for(int reason:{0,15,23,31})for(int mode:{1,2}) {
        const auto f=score(1,reason,mode);
        auto reference=f;
        // Remove only the silent note; retain the complete authored timeline.
        auto& s=reference.tracks[0].sequenceData;s.erase(s.begin(),s.begin()+5);
        const auto expected=renderAll(reference);
        for(int block:{1,257,1024}) {
            MaPlayer p;check(p.init(f,8000),"future-event zero-output score initializes");
            const auto out=drain(p,block);
            check(out==expected&&!p.diagnostics().tailLimitReached,"later rests and notes survive exact-zero companions");
            if(mode==2)check(std::any_of(out.begin()+16000,out.end(),[](float x){return x!=0;}),
                             "later audible note actually survives");
            if(block==257){p.seekToStart();check(drain(p,block)==out,"future-note seek is exact");}
        }
    }
    const auto f=score(1,0,3);
    std::vector<float> expected;
    for(int block:{1,257,1024}) {
        MaPlayer p;check(p.init(f,8000),"pitch-revival score initializes");
        const auto out=drain(p,block);
        check(std::all_of(out.begin(),out.begin()+16000,[](float x){return x==0;})
              &&std::any_of(out.begin()+16000,out.end(),[](float x){return x!=0;}),
              "live note remains silent before bend and resumes afterwards");
        check(p.diagnostics().tailLimitReached,"revived nondecaying XOF carrier retains normal safety tail");
        if(expected.empty())expected=out;else check(out==expected,"pitch revival is block independent");
        if(block==257){p.seekToStart();check(drain(p,block)==out,"pitch-revival seek is exact");}
    }
}

void filterDrain() {
    for(int reason:{0,15,23,31}) {
        auto f=score(1,reason);
        appendExclusive(f,tone(1,false,1,false,14));
        f.tracks[0].sequenceData={0,0x90,84,100,10,0,0xc1,1,0,0x91,45,100,10,10,0xff,0x2f,0};
        auto reference=f;
        reference.tracks[0].sequenceData.erase(reference.tracks[0].sequenceData.begin(),reference.tracks[0].sequenceData.begin()+5);
        const auto expected=renderAll(reference);
        check(expected.size()/2>800&&expected.size()/2<1600,"audible reference has a bounded release and filter tail");
        for(int block:{1,257,1024}) {
            MaPlayer p;check(p.init(f,8000),"shared filter fixture initializes");
            check(drain(p,block)==expected&&!p.diagnostics().tailLimitReached,
                  "silent companion preserves every sample of audible release and LP drain");
        }
    }
    auto bent=fixture();setVoice(bent,image(1,0));
    bent.tracks[0].sequenceData={0,0x90,83,100,10,10,0xe0,127,127,0,0xff,0x2f,0};
    auto off=bent;off.tracks[0].sequenceData={0,0x90,83,100,10,10,0xb0,120,0,0,0xff,0x2f,0};
    const auto reference=renderAll(off);
    check(std::any_of(reference.begin(),reference.end(),[](float x){return x!=0;})
          &&reference.size()/2>800,"bend-at-end fixture really excites the shared filter");
    for(int block:{1,257,1024}) {
        MaPlayer p;check(p.init(bent,8000),"bend-at-end score initializes");
        const auto output=drain(p,block);
        check(output==reference&&!p.diagnostics().tailLimitReached,
              "final bend to Nyquist suppression preserves exact All Sound Off filter drain");
        if(block==257){p.seekToStart();check(drain(p,block)==reference,"bend-at-end seek is exact");}
    }
}

void test(){carriersAndControls();outputGraphAndSelectors();endpointsAndFutureEvents();filterDrain();}
} // namespace fmZeroOutputRegression
