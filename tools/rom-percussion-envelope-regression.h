// SPDX-License-Identifier: Apache-2.0
#pragma once
namespace romPercussionEnvelope {
// The envelope component remains explicitly opt-in. Normal score playback
// preserves pre-r92 substitute balance; genuine PCM source validation is pending.
void test() {
    // The unwrapped recipe is an independent excitation oracle. The authored
    // envelope must attenuate its final output, never its FM modulation inputs.
    int cases=0;
    for(int wave=0;wave<22;++wave)for(int tl:{0,12,14,63})for(bool xof:{false,true}) {
        auto base=*FmVoicePatch::romApprox(wave);
        auto wrapped=base;wrapped.outputEnvelopeEnabled=true;
        auto& e=wrapped.outputEnvelope;
        e.ar=10;e.dr=8;e.sl=3;e.sr=4;e.rr=7;e.tl=uint8_t(tl);
        e.xof=xof;e.holdEnabled=true;
        FmVoice reference,actual;reference.setSampleRate(8000);actual.setSampleRate(8000);
        reference.noteOn(base,220,.7f);actual.noteOn(wrapped,220,.7f);
        FmEnvelope envelope;envelope.configure(e,8000);envelope.keyOn();
        const float gain=float(std::pow(10.,-.75*tl/20));
        for(int frame=0;frame<2048;++frame) {
            if(frame==50){reference.setVolume(.4f);actual.setVolume(.4f);}
            if(frame==100){reference.noteOff(true);actual.noteOff(true);envelope.keyOff(true);}
            if(frame==200){reference.setHold(false);actual.setHold(false);envelope.setHold(false);}
            const float env=envelope.advance()*gain;
            const float expected=reference.tick()*env;
            check(std::fabs(actual.tick()-expected)<1e-7f,"ROM envelope follows authored TL/EG through volume/Hold/XOF");
        }
        // Allocation of a regular FM note must discard a prior PCM envelope.
        actual.noteOn(base,220,.7f);reference.noteOn(base,220,.7f);
        for(int n=0;n<32;++n)check(actual.tick()==reference.tick(),"output envelope does not leak across voice reuse");
        FmVoice fresh;fresh.setSampleRate(8000);
        actual.setVolume(1);actual.noteOn(wrapped,220,.7f);fresh.noteOn(wrapped,220,.7f);
        for(int n=0;n<32;++n)check(actual.tick()==fresh.tick(),"wrapped retrigger starts a fresh authored envelope");
        ++cases;
    }
    for(int lowBit:{0,8}) {
        auto patch=*FmVoicePatch::romApprox(2);patch.outputEnvelopeEnabled=true;
        patch.outputEnvelope.ar=0;patch.outputEnvelope.rateLowBits=uint8_t(lowBit);
        FmVoice voice;voice.setSampleRate(8000);voice.noteOn(patch,220,1);
        check(voice.silentForCurrentNote()==(lowBit==0),"only configured zero attack is permanently silent");
        check(voice.muted()==(lowBit==0),"MA7 slow attack is not culled as silence");
        ++cases;
    }
    // Every supported subframe path applies the same native-clock envelope
    // once, to all interior samples and the endpoint, without altering PM.
    for(bool nested:{false,true})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})
    for(int factor:{2,8,16,32,64,128}) {
        if((nested&&factor<16)||(!nested&&factor>32))continue;
        auto base=FmVoicePatch::defaultPatch();base.modernTimbre=true;
        if(nested){base.fourOp=true;base.algorithm=6;for(auto& op:base.ops){op=FmOpPatch{};op.ma35Multipliers=true;op.tl=24;}}
        auto wrapped=base;wrapped.outputEnvelopeEnabled=true;
        wrapped.outputEnvelope.tl=12;wrapped.outputEnvelope.ar=10;
        FmEnvelope env;env.configure(wrapped.outputEnvelope,8000);env.keyOn();
        FmVoice reference,actual;reference.setSampleRate(8000);actual.setSampleRate(8000);
        reference.noteOn(base,220,.7f);actual.noteOn(wrapped,220,.7f);
        for(int frame=0;frame<256;++frame){float a[128],b[128];
            const float gain=env.advance()*float(std::pow(10.,-.75*12/20));
            check(nested?reference.tickNestedSubframes(a,factor,math):reference.tickSineSubframes(a,factor,math),"source subframes supported");
            check(nested?actual.tickNestedSubframes(b,factor,math):actual.tickSineSubframes(b,factor,math),"wrapped subframes supported");
            for(int sub=0;sub<factor;++sub)check(std::fabs(b[sub]-a[sub]*gain)<1e-7f,"subframes retain one authored output envelope clock");
        }
        ++cases;
    }
    for(int format:{2,3})for(int wave:{1,2,4,5,6,22,26,63})for(bool rom:{false,true}) {
        auto file=customDrumFallback::score(format,124,60,rom,wave,false);
        auto withLevel=[&](int tl,int ar){auto f=file;
            std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,124,0,0,60,1,
                0x1f,0x40,0,0,0x08,0x90,uint8_t(ar<<4),uint8_t(tl<<2),0,0,0,0,0,0,80,uint8_t((rom?128:0)|wave)};
            setVoice(f,voice);return f;};
        const auto loud=withLevel(0,15),quiet=withLevel(12,15),stopped=withLevel(0,0);
        check(renderAll(loud)==renderAll(quiet)&&renderAll(loud)==renderAll(stopped),
              "default substitutes retain the pre-r92 balance until genuine PCM sources are available");
        if(rom&&wave<22){
            const auto expected=renderAll(quiet);
            for(int block:{1,1024})check(renderAll(quiet,block)==expected,"ROM envelope independent of output block size");
            MaPlayer player;check(player.init(quiet,8000),"wrapped seek initializes");
            check(silentAttackRegression::drain(player,257)==expected,"initial wrapped stream matches");
            player.seekToStart();check(silentAttackRegression::drain(player,257)==expected,"wrapped seek restarts authored envelope");
        }
        ++cases;
    }
    std::cout<<"ROM percussion authored envelope checks: "<<cases<<" PASS\n";
}
}
