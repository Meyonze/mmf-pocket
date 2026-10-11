// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original independent feedback/topology fixtures. No ROM or music data.
namespace fmFeedbackRegression {
constexpr double pi=3.14159265358979323846;
double nominalCycles(int setting) {
    // MA5 manual1.33 p63: values are radians, not cycles or linear levels.
    const double radians=setting==0?0:std::ldexp(pi,setting-5);
    return radians/(2*pi);
}
FmVoicePatch patchFor(int algorithm,int feedback,int profile) {
    FmVoicePatch p;
    p.algorithm=uint8_t(algorithm);p.fourOp=algorithm>=2;
    p.modernTimbre=profile!=0;
    for(auto& op:p.ops) {
        op.ar=15;op.dr=0;op.sr=0;op.rr=9;op.sl=0;op.multi=1;
        op.tl=12;op.ksr=0;op.ksl=0;op.vib=false;op.am=false;
        op.ma35Multipliers=profile==2;op.ma7Multipliers=profile==3;
    }
    p.ops[0].fb=uint8_t(feedback);
    if(algorithm==2||algorithm==5)p.ops[2].fb=uint8_t(7-feedback);
    return p;
}
std::vector<float> synth(const FmVoicePatch& p,int rate=8000) {
    FmVoice voice;voice.setSampleRate(rate);voice.noteOn(p,220,0.5f);
    std::vector<float> result(2048);
    for(auto& x:result){x=voice.tick();check(std::isfinite(x),"feedback output finite");}
    return result;
}
std::vector<float> independent(const FmVoicePatch& p,int rate) {
    std::array<FmOperator,4> ops;
    const int count=p.fourOp?4:2;
    // Fixed220Hz yields approximate BLOCK3, KSR0 offset1 in the existing EG
    // model. Reuse operators, not FmVoice's feedback/routing implementation.
    for(int i=0;i<count;++i){ops[i].configure(p.ops[i],rate,1,p.modernTimbre);ops[i].noteOn(220);}
    std::array<std::array<float,2>,4> history{};
    const double depth=p.modernTimbre&&p.ops[0].ma35Multipliers?4.0:0.42;
    auto step=[&](int op,double incoming) {
        const int setting=p.ops[op].fb;
        const double amount=p.modernTimbre?nominalCycles(setting):double(setting)/24.0;
        const double feedback=(double(history[op][0])+history[op][1])*0.5*amount;
        const float value=ops[op].tick(incoming+feedback);
        history[op][1]=history[op][0];history[op][0]=value;return double(value);
    };
    std::vector<float> result(2048);
    for(auto& x:result) {
        double a,b,c,d,value=0;
        switch(p.algorithm) {
            case 0:a=step(0,0);value=step(1,a*depth);break;
            case 1:a=step(0,0);b=step(1,0);value=(a+b)*0.5;break;
            case 2:a=step(0,0);b=step(1,0);c=step(2,0);d=step(3,0);value=(a+b+c+d)*0.35;break;
            case 3:a=step(0,0);b=step(1,0);c=step(2,b*depth);value=step(3,(a+c)*depth);break;
            case 4:a=step(0,0);b=step(1,a*depth);c=step(2,b*depth);value=step(3,c*depth);break;
            case 5:a=step(0,0);b=step(1,a*depth);c=step(2,0);d=step(3,c*depth);value=(b+d)*0.5;break;
            case 6:a=step(0,0);b=step(1,0);c=step(2,b*depth);d=step(3,c*depth);value=(a+d)*0.5;break;
            case 7:a=step(0,0);b=step(1,0);c=step(2,b*depth);d=step(3,0);value=(a+c+d)*0.4;break;
        }
        x=float(value*0.5f*1.0f*0.7);
    }
    return result;
}
void numerical() {
    for(int rate:{8000,44100,48000})for(int profile:{0,1,2,3})
        for(int algorithm=0;algorithm<8;++algorithm)for(int feedback=0;feedback<8;++feedback) {
            const auto p=patchFor(algorithm,feedback,profile);
            const auto actual=synth(p,rate),expected=independent(p,rate);
            for(size_t i=0;i<actual.size();++i)
                check(actual[i]==expected[i],"feedback/topology agrees sample-exactly with independently assembled nominal-radian oracle");
        }
}
void directOutputLoops() {
    // A carrier exclusion mask would wrongly erase the documented FB(1)+2
    // / FB(1)+2+FB(3)+4 / FB(1)+... loops (MA5 manual p62 diagrams).
    constexpr unsigned carriers[8]={2,3,15,8,8,10,9,13};
    for(int algorithm:{1,2,6,7})for(int feedback=1;feedback<=7;++feedback) {
        auto p=patchFor(algorithm,feedback,2),wrong=p;
        for(int i=0;i<(p.fourOp?4:2);++i)if(carriers[algorithm]&(1u<<i))wrong.ops[i].fb=0;
        check(synth(p)!=synth(wrong),"direct-output feedback loops must not be removed by carrier mask");
    }
}
void reuse() {
    check(synth(patchFor(0,5,0))!=synth(patchFor(0,5,1))&&
          synth(patchFor(0,5,1))!=synth(patchFor(0,5,2))&&
          synth(patchFor(0,5,2))!=synth(patchFor(0,5,3)),
          "reuse fixture observably distinguishes legacy feedback and VM35 phase depth");
    for(int profile:{0,1,2,3}) {
        FmVoice reused;reused.setSampleRate(8000);
        const auto previous=patchFor(2,7,2),next=patchFor(0,5,profile);
        reused.noteOn(previous,220,0.5f);for(int i=0;i<512;++i)reused.tick();
        reused.noteOff();for(int i=0;i<256;++i)reused.tick();
        reused.noteOn(next,220,0.5f);const auto expected=synth(next);
        for(float sample:expected)check(reused.tick()==sample,"feedback history and synthesis family reset on allocation/retrigger");
    }
}
}
void checkFmFeedback(){fmFeedbackRegression::numerical();fmFeedbackRegression::directOutputLoops();fmFeedbackRegression::reuse();}
