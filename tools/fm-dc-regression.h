// SPDX-License-Identifier: Apache-2.0
void checkFmDcBlocker() {
    for(double rate:{8000.,44100.,48000.}) {
        FmDcBlocker actual;actual.configure(rate);
        double prevX=0,prevY=0;
        const double pole=std::exp(-6.283185307179586*3/rate),g=(1+pole)/2;
        for(int i=0;i<int(rate);++i) {
            const float input=float(.2+.1*std::sin(6.283185307179586*123*i/rate));
            const double reference=g*(input-prevX)+pole*prevY;
            check(actual.process(input)==float(reference),"FM DC recurrence matches independent scalar reference");
            prevX=input;prevY=reference;
        }
        FmDcBlocker constant;constant.configure(rate);float y=0;
        for(int i=0;i<int(rate);++i)y=constant.process(.2f);
        check(std::fabs(y)<1e-7,"constant FM bias is rejected, not normalized into audible output");
        check(!constant.settled(),"nonzero input history cannot be mistaken for a drained tail");
        for(int i=0;i<int(rate);++i)y=constant.process(0);
        check(constant.settled() && std::fabs(y)<1e-7,"FM DC history drains after biased note ends");
        constant.reset();check(constant.process(0)==0 && constant.settled(),"DC reset clears both histories");
        for(double hz:{20.,50.,1000.}) {
            FmDcBlocker tone;tone.configure(rate);double inputPower=0,outputPower=0;
            for(int i=0;i<int(rate*2);++i) {
                const float input=float(std::sin(6.283185307179586*hz*i/rate));
                const float output=tone.process(input);
                if(i>=int(rate)){inputPower+=double(input)*input;outputPower+=double(output)*output;}
            }
            const double attenuation=10*std::log10(outputPower/inputPower);
            check(attenuation>-.1 && attenuation<.0001,"3Hz DC removal retains audible bass within0.1dB");
        }
    }
    // The beta.13 default-conditioning experiment is withdrawn: retaining a
    // low DC mean alone does not establish perceptually better mix balance.
    // Keep standalone filter tests above for future controlled experiments.
    auto fm=fixture();
    auto voice=std::vector<uint8_t>{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
        0,0xf0,0xf0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,8};
    setVoice(fm,voice);fm.tracks[0].sequenceData={0,0x90,60,100,100,100,0xff,0x2f,0};
    const auto output=renderAll(fm);
    check(output==renderAll(fm,1) && output==renderAll(fm,1024),"restored FM output is block independent");
    double mean=0;for(size_t i=4000*2;i<8000*2;++i)mean+=output[i];mean/=8000;
    check(mean>.005,"default FM output no longer applies rejected DC conditioning");
    auto mixed=fm;TrackChunk hps;hps.trackNumber=1;hps.formatType=0;
    hps.durationTimeBase=hps.gateTimeBase=0x10;mixed.tracks.push_back(hps);
    const auto bypass=renderAll(mixed);
    double bypassMean=0;
    for(size_t i=4000*2;i<8000*2;++i)bypassMean+=bypass[i];bypassMean/=8000;
    check(bypassMean>.005,"mixed HPS/modern scores conservatively retain legacy FM output bias");
    check(output==bypass,"empty HPS track no longer changes modern FM conditioning policy");
    check(output.size()<8000*2*12 && std::fabs(output.back())<1e-7,
          "restored FM output ends naturally without a cut-off residual");
    MaPlayer p;check(p.init(fm,8000),"DC replay initializes");float block[514];
    while(p.render(block,257)>0){}p.seekToStart();std::vector<float> replay;int n;
    while((n=p.render(block,257))>0)replay.insert(replay.end(),block,block+n*2);
    check(replay==output,"seek restores rejected-filter-free output deterministically");
    check(p.init(fm,8000),"DC second init initializes");replay.clear();
    while((n=p.render(block,257))>0)replay.insert(replay.end(),block,block+n*2);
    check(replay==output,"init resets restored output deterministically");
}
