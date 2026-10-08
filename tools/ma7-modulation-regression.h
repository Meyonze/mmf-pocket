#pragma once
void checkMa7ModulationDepth() {
    // Independent phase reference verifies that channel depth scales vibrato
    // only. The intrinsic DVB/LFO calibration is the pre-existing approximation.
    for(double scale:{0.,1.,2.,4.,8.}) {
        FmOpPatch op;op.multi=1;op.tl=0;op.ar=15;op.sl=0;op.dr=op.sr=0;
        op.vib=true;op.dvb=0;op.ksl=0;op.wave=0;
        FmOperator actual;actual.configure(op,8000);actual.noteOn(123);
        double phase=0;
        for(int i=0;i<2048;++i) {
            const double lfo=std::sin(double(i)*.01);
            phase+=123.0/8000*(1+0.00196*lfo*scale);phase-=std::floor(phase);
            const float value=actual.tick(0,lfo,scale);
            check(std::fabs(value-std::sin(6.283185307179586*phase))<1e-6f,
                  "channel depth uses independently computed per-sample vibrato phase");
        }
    }
    auto make=[](bool pcm,int value,bool vib=true,bool am=false,int format=3) {
        auto f=fixture(format);
        if(pcm) {
            addPcmFixture(f);
            auto voice=std::vector<uint8_t>{0x43,0x79,7,0x7f,1,0,0,0,0,1,
                0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,1};
            voice[18]=uint8_t((vib?1|6:0)|(am?0x10|0x60:0));setVoice(f,voice);
        } else {
            auto voice=std::vector<uint8_t>{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
                0,0xf0,0xf0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,0};
            voice[24]=uint8_t((vib?1|6:0)|(am?0x10|0x60:0));setVoice(f,voice);
        }
        const uint8_t cc=format==3?0x30:0xb0,on=format==3?0x10:0x90;
        auto& s=f.tracks[0].sequenceData;s={0,cc,7,127};
        if(value>=0)s.insert(s.end(),{0,cc,1,uint8_t(value)});
        s.insert(s.end(),{0,on,60,100,80,80,0xff,0x2f,0});return f;
    };
    const std::array<int,9> values{0,1,31,32,63,64,95,96,127};
    for(bool pcm:{false,true}) {
        std::vector<std::vector<float>> sounds;
        for(int value:values) {
            auto f=make(pcm,value);sounds.push_back(renderAll(f));
            check(sounds.back()==renderAll(f,1) && sounds.back()==renderAll(f,1024),"MA7 channel vibrato is block independent");
            for(float x:sounds.back())check(std::isfinite(x),"allMA7 vibrato multipliers finite");
        }
        check(sounds[1]==sounds[2] && sounds[3]==sounds[4] && sounds[5]==sounds[6] && sounds[7]==sounds[8],
              "MA7 modulation32-value bucket boundaries follow manufacturer table");
        check(sounds[0]!=sounds[1] && sounds[1]!=sounds[3] && sounds[3]!=sounds[5] && sounds[5]!=sounds[7],
              "OFF and1/2/4/8 select distinct FM/WT vibrato depths");
        check(sounds[0]==renderAll(make(pcm,-1)) && sounds[0]==renderAll(make(pcm,127,false)),
              "MA7 default modulation0 is OFF and EVB-disabled voices never gain vibrato");
        check(renderAll(make(pcm,0,false,true))==renderAll(make(pcm,127,false,true)),
              "CC1 does not scale authored tremolo");
        check(renderAll(make(pcm,0,true,false,2))==renderAll(make(pcm,127,true,false,2)),
              "unverified MA3/5 modulation interpretation remains unchanged");
        auto live=make(pcm,0);auto& s=live.tracks[0].sequenceData;s.resize(s.size()-4);
        s.insert(s.end(),{20,0x30,1,127,20,0x30,1,0,40,0xff,0x2f,0});
        const auto changed=renderAll(live);
        check(std::equal(sounds[0].begin(),sounds[0].begin()+3200,changed.begin()),
              "live depth update cannot change audio preceding the event");
        check(changed!=sounds[0],"CC1 changes held FM/WT without allocating a new note");
        auto isolated=live;isolated.tracks[0].sequenceData[14]=0x31;isolated.tracks[0].sequenceData[18]=0x31;
        check(renderAll(isolated)==sounds[0],"channel vibrato updates cannot affect another channel");
        auto invalid=make(pcm,128);
        check(renderAll(invalid)==sounds[0],"invalid native CC1 value cannot enable modulation");
        MaPlayer p;check(p.init(live,8000),"modulation seek initializes");float b[514];
        while(p.render(b,257)>0){}p.seekToStart();std::vector<float> replay;int n;
        while((n=p.render(b,257))>0)replay.insert(replay.end(),b,b+n*2);
        check(replay==changed,"seek restores OFF then replays live channel modulation");
        check(p.init(make(pcm,-1),8000),"fresh init after amplified modulation");replay.clear();
        while((n=p.render(b,257))>0)replay.insert(replay.end(),b,b+n*2);
        check(replay==sounds[0],"fresh init cannot inherit channel vibrato depth");
    }
    auto audio=fixture(3);WaveData w;w.number=1;w.adpcm=false;w.bitsPerSample=8;w.samplingRate=8000;
    for(int i=0;i<6400;++i)w.data.push_back(uint8_t(128+int(12*std::sin(i*.1))));
    audio.tracks[0].waves.push_back(w);
    audio.tracks[0].sequenceData={0,0x30,0,125,0,0x10,0,127,80,80,0xff,0x2f,0};
    auto mod=audio;mod.tracks[0].sequenceData.insert(mod.tracks[0].sequenceData.begin(),{0,0x30,1,127});
    mod.tracks[0].sequenceData.insert(mod.tracks[0].sequenceData.end()-4,{20,0x30,1,0});
    mod.tracks[0].sequenceData[mod.tracks[0].sequenceData.size()-4]=60;
    check(renderAll(audio)==renderAll(mod),"recorded Audio ignores initial/live modulation depth");
}
