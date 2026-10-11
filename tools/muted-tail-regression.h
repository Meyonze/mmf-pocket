// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original synthetic fixtures. No threshold-based voice pruning or EG retune.
namespace mutedTailRegression {
SmafFile held(bool pcm) {
    auto f=fixture();
    if(pcm)addPcmFixture(f,1,0);
    else setVoice(f,{0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0,8,0x50,0xf0,0xfc,0,0x10,0,8,0x50,0xf0,0,0,0x10,0});
    // Audible 0..100ms; gate requests keyoff but the selected patch persists.
    f.tracks[0].sequenceData={0,0x90,60,100,10,10,0xb0,7,0,10,0xff,0x2f,0};
    return f;
}
std::vector<float> drain(MaPlayer& p,int block=257) {
    std::vector<float> out,b(size_t(block)*2);int n;
    while((n=p.render(b.data(),block))>0)out.insert(out.end(),b.begin(),b.begin()+2*n);
    check(p.finished(),"muted-tail renderer finished");
    check(p.render(b.data(),block)==0,"muted-tail EOF remains ended");return out;
}
void test() {
    for(bool pcm:{false,true})for(int cc:{7,11}){
        auto f=held(pcm);f.tracks[0].sequenceData[7]=uint8_t(cc);
        MaPlayer p;check(p.init(f,8000),"zero-gain tail fixture initializes");
        const auto a=drain(p);
        check(a.size()/2>=1600&&a.size()/2<1700,"zero-gain tail ends after authored EOS and filter drain, not10s cap");
        check(std::any_of(a.begin(),a.end(),[](float s){return std::fabs(s)>1e-4f;}),"muted-tail fixture actually sounds before authored mute");
        check(!p.diagnostics().tailLimitReached,"zero-gain tail does not hit safety limit");
        check(pcm?p.diagnostics().pcmNotes==1:p.diagnostics().fmNotes==1,"muted-tail fixture takes intended FM/WT path");
        p.seekToStart();check(drain(p,1)==a,"zero-gain tail is seek and block invariant");
        check(renderAll(f,1024)==a,"zero-gain tail large-block invariant");
        auto later=f;
        later.tracks[0].sequenceData={0,0x90,60,100,10,10,0xb0,uint8_t(cc),0,
            10,0xb0,uint8_t(cc),127,10,0xff,0x2f,0};
        const auto revived=renderAll(later);
        check(revived.size()/2==82400,"later controller must preserve and revive the released tail");
        check(std::any_of(revived.begin()+3200,revived.begin()+4000,[](float s){return std::fabs(s)>1e-4f;}),"unmuted held tail sounds again after controller");
        auto quiet=f;quiet.tracks[0].sequenceData[8]=1;
        check(renderAll(quiet).size()/2==81600,"nonzero quiet tail is not truncated by a level threshold");
        auto rest=f;rest.tracks[0].sequenceData[9]=30;
        check(renderAll(rest).size()/2>=3200,"authored final rest preserved even when all voices muted");
    }
    // A generated ATR gate is not an authored controller. Muted source time
    // must still be retained, even when no gain update remains in the score.
    auto atr=atrFormatRegression::handBuilt(0,1,8000,0,0,atrGateRegression::tone(800),
        {0,0,0x37,0,0,1,100});
    check(renderAll(atr).size()/2==800,"muted ATR keeps its source/gate endpoint, not immediate EOF");
    auto revivedAtr=atr;
    revivedAtr.tracks[0].sequenceData={0,0,0x37,0,0,1,100,20,0,0x37,127};
    const auto atrOut=renderAll(revivedAtr);
    check(std::any_of(atrOut.begin()+320,atrOut.begin()+800,[](float s){return std::fabs(s)>1e-4f;}),
          "later ATR gain control revives the existing recording");
    check(renderAll(revivedAtr,1)==atrOut&&renderAll(revivedAtr,1024)==atrOut,
          "muted/unmuted ATR keeps render-block invariance");
}
} // namespace mutedTailRegression
