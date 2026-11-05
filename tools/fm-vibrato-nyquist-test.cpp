// SPDX-License-Identifier: Apache-2.0
// Original sine/integrated-phase references; no handset recording or patch.
#include "ma_player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;
void require(bool ok,const char* message) {
    if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
void operators() {
    constexpr double depths[]={0.00196,0.00387,0.00774,0.01548};
    unsigned checked=0, suppressed=0, recovered=0;
    for(bool modern:{false,true})for(bool vib:{false,true})for(double rate:{8000.0,44100.0})
    for(int depth=0;depth<4;++depth)for(double scale:{0.0,1.0,2.0,4.0,8.0})
    for(double fraction:{0.49,0.5,0.51,0.6}) {
        FmOpPatch patch;patch.vib=vib;patch.dvb=uint8_t(depth);patch.rr=15;
        FmOperator op;op.configure(patch,rate,0,modern);
        const double hz=rate*fraction, base=hz/rate;
        op.noteOn(hz);double phase=0;
        for(int frame=0;frame<256;++frame) {
            const double lfo=std::sin(6.283185307179586*(frame+1)/61.0);
            const double inc=base*(1+(vib?depths[depth]*lfo*scale:0));
            phase+=inc;if(phase>=1)phase-=std::floor(phase);
            const bool silent=(modern&&vib?inc:base)>=0.5;
            const float actual=op.tick(0,lfo,scale);
            if(silent){require(actual==0,"instantaneous modern Nyquist suppression is exact");++suppressed;}
            else {
                if(std::abs(actual-std::sin(6.283185307179586*phase))>=2e-6)
                    std::cerr<<"case modern="<<modern<<" vib="<<vib<<" depth="<<depth
                             <<" scale="<<scale<<" nominal="<<base<<" increment="<<inc<<'\n';
                require(std::abs(actual-std::sin(6.283185307179586*phase))<2e-6,
                    "in-band vibrato follows integrated phase, including recovery from nominal suppression");
                recovered+=base>=0.5;
            }
            ++checked;
        }
        if(depth==3&&scale==8&&fraction==0.6) {
            op.noteOff();for(int frame=0;frame<int(rate/8);++frame)op.tick(0,0,scale);
            require(op.finished(),"vibrating high operator still completes release");
        }
    }
    require(suppressed>0&&recovered>0,"test exercises silence and nominally high-note recovery");
    std::cout<<"Vibrato operator reference PASS: "<<checked<<" samples\n";
}
void certificates() {
    for(bool modern:{false,true})for(int depth=0;depth<4;++depth)
    for(double scale:{0.0,1.0,2.0,4.0,8.0})for(double fraction:{0.49,0.5,0.51,0.6}) {
        FmVoicePatch patch;patch.modernTimbre=modern;patch.ops[0].ar=0;
        patch.ops[1].vib=true;patch.ops[1].dvb=uint8_t(depth);
        FmVoice voice;voice.setSampleRate(8000);voice.noteOn(patch,8000*fraction,1);
        voice.setVibratoScale(scale);
        constexpr double depths[]={0.00196,0.00387,0.00774,0.01548};
        const double base=(8000*fraction)/8000;
        const bool silent=base>=0.5&&(!modern||base*(1-depths[depth]*scale)>=0.5);
        require(voice.muted()==silent,"EOF silence certificate includes future intrinsic vibrato and current CC1 scale");
    }
}
SmafFile score(bool vibrato) {
    SmafFile f;TrackChunk t;t.formatType=3;t.trackNumber=0;
    t.durationTimeBase=t.gateTimeBase=0x10;
    // VM35 sine carrier with a stopped modulator, slow release and EVB3.
    std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,
        0,0xf0,0,0xfc,0,0x10,0,0,0x70,0xf0,0,uint8_t(vibrato?7:0),0x10,0};
    t.setupData={0xf0,uint8_t(voice.size()+1)};
    t.setupData.insert(t.setupData.end(),voice.begin(),voice.end());t.setupData.push_back(0xf7);
    t.sequenceData={0,0x30,1,127,0,0x10,108,100,10,10,0xff,0x2f,0};
    f.tracks.push_back(t);return f;
}
std::vector<float> drain(MaPlayer& p,int block) {
    std::vector<float> audio,buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0) {
        audio.insert(audio.end(),buffer.begin(),buffer.begin()+n*2);
        require(audio.size()<=8000*2*12,"vibrato tail stays bounded");
    }
    require(p.finished(),"vibrato score finishes");return audio;
}
void player() {
    MaPlayer reference;require(reference.init(score(true),8000),"high-vibrato score initializes");
    const auto expected=drain(reference,257);
    require(!reference.diagnostics().tailLimitReached,"in-band vibrato release ends naturally");
    require(reference.scoreEndSamples()==800,"authored high-vibrato endpoint remains100ms");
    require(expected.size()>800*2,"future in-band vibrato must retain release after score EOF");
    require(std::any_of(expected.begin()+800*2,expected.end(),[](float x){return std::abs(x)>1e-5f;}),
        "high carrier can become audible after the final controller and gate");
    for(int block:{1,1024}) {
        MaPlayer p;require(p.init(score(true),8000),"block fixture initializes");
        require(drain(p,block)==expected,"dynamic guard and future-vibrato EOF are block invariant");
        p.seekToStart();require(drain(p,257)==expected,"seek resets vibrato phase and silence policy");
    }
    MaPlayer silent;require(silent.init(score(false),8000),"non-vibrating high score initializes");
    const auto zero=drain(silent,257);
    require(zero.size()==800*2&&std::all_of(zero.begin(),zero.end(),[](float x){return x==0;}),
        "non-vibrating high note retains exact silent authored endpoint");
}
int main(){operators();certificates();player();std::cout<<"Vibrato Nyquist and EOF tests PASS\n";}
