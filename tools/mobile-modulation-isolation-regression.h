// SPDX-License-Identifier: Apache-2.0
#pragma once
// Mobile CC1 stays unapplied even when another MTR initializes shared channels.
namespace mobileModulationIsolation {
SmafFile score(bool pcm,int order,int channel,int timing,bool nop,bool packed=false) {
    auto f=fixture();
    if(pcm) {
        addPcmFixture(f);
        std::vector<uint8_t> v={0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0xf0,0xf0,0,7,0,0,0,0,0,80,1};
        setVoice(f,v);
    } else {
        setVoice(f,{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,1,
            0,0xf0,0xf0,0,0,0x10,0,0,0xf0,0xf0,0,7,0x10,0});
    }
    auto& s=f.tracks[0].sequenceData;s={0,0xb0,7,127};
    auto cc=[&](int delta){
        if(nop)s.insert(s.end(),{uint8_t(delta),0xff,0});
        else s.insert(s.end(),{uint8_t(delta),uint8_t(0xb0|channel),1,127});
    };
    if(timing==0){cc(0);s.insert(s.end(),{0,0x90,60,100,80,80,0xff,0x2f,0});}
    else {s.insert(s.end(),{0,0x90,60,100,80});cc(timing==1?20:100);
        // End physically at the last control: a same-time EOS would hide
        // accidentally dropping the unapplied event's authored timestamp.
        if(timing==1)s.insert(s.end(),{60,0xff,0x2f,0});}
    if(packed)f=channelModeRegression::compressed(f);
    if(order) {
        auto ma7=fixture(3).tracks[0];ma7.trackNumber=1;ma7.sequenceData={0,0xff,0x2f,0};
        if(order==1)f.tracks.push_back(ma7);else f.tracks.insert(f.tracks.begin(),ma7);
    }
    return f;
}

void test() {
    for(bool pcm:{false,true})for(bool packed:{false,true})
    for(int order:{0,1,2})for(int channel:{0,1})for(int timing:{0,1,2}) {
        const auto f=score(pcm,order,channel,timing,false,packed);
        const auto expected=renderAll(score(pcm,order,channel,timing,true,packed));
        check(std::any_of(expected.begin(),expected.end(),[](float x){return x!=0;}),
              "Mobile modulation oracle contains actual embedded FM/WT audio");
        for(int block:{1,257,1024}) {
            MaPlayer p;check(p.init(f,8000),"Mobile modulation isolation fixture initializes");
            const auto actual=silentAttackRegression::drain(p,block);
            check(actual==expected,"unapplied Mobile CC1 equals timed NOP across shared MTR profiles");
            check(p.scoreAudit().scanComplete,"mixed modulation fixture decodes completely");
            bool unapplied=false;
            for(const auto& g:p.scoreAudit().groups)
                if(g.domain==ScoreAudit::Domain::Controller&&g.opcode==1)
                    unapplied=unapplied||(g.status==ScoreAudit::Status::Unapplied&&g.channel==channel);
            check(unapplied,"Mobile CC1 remains explicitly audited as unapplied");
            check((pcm?p.diagnostics().pcmNotes:p.diagnostics().fmNotes)==1
                  &&p.diagnostics().fallbackNotes==0,"embedded source route is preserved");
            if(timing==2)check(actual.size()==16000,"final ignored CC1 retains its1000ms endpoint");
            if(block==257){p.seekToStart();check(silentAttackRegression::drain(p,block)==expected,
                                              "modulation isolation is seek invariant");}
        }
    }
    // Pure Mobile intrinsic vibrato must not be switched off by this fix.
    for(bool pcm:{false,true}) {
        const auto vib=score(pcm,0,0,1,false);
        auto dry=vib;
        // Long-form exclusive payload starts after F0 and its one-byte length.
        dry.tracks[0].setupData[2+(pcm?18:24)]=0;
        check(renderAll(vib)!=renderAll(dry),"intrinsic Mobile EVB remains audible and unchanged");
    }
}
} // namespace mobileModulationIsolation
