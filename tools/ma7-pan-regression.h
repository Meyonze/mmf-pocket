#pragma once
// Independent mathematical reference to MA7 SMAF guideline8.3.5. Synthetic
// low-level signals avoid the limiter; this is not a handset loudness test.
void checkMa7ChannelPan() {
    auto make=[](int format,int family,int pan,int channel=0) {
        auto f=fixture(format);auto& t=f.tracks[0];
        const int bank=(format==3 && channel>=16)?128:0;
        const uint8_t cc=uint8_t((format==3?0x30:0xb0)+(channel&15)+bank);
        const uint8_t on=uint8_t((format==3?0x10:0x90)+(channel&15)+bank);
        if(family==1)addPcmFixture(f);
        if(family==2) {
            WaveData w;w.number=1;w.samplingRate=8000;w.adpcm=false;w.bitsPerSample=8;
            for(int i=0;i<1600;++i)w.data.push_back(uint8_t(128+int(8*std::sin(i*.1))));
            t.waves.push_back(w);
        }
        t.sequenceData={0,cc,7,127};
        if(family==2)t.sequenceData.insert(t.sequenceData.end(),{0,cc,0,125});
        if(pan>=0)t.sequenceData.insert(t.sequenceData.end(),{0,cc,10,uint8_t(pan)});
        t.sequenceData.insert(t.sequenceData.end(),{0,on,uint8_t(family==2?0:60),50,40,40,0xff,0x2f,0});
        return f;
    };
    auto equivalent=[](const std::vector<float>& a,const std::vector<float>& ref,int pan) {
        if(a.size()>ref.size()+128 || ref.size()>a.size()+128)return false;
        const double angle=1.5707963267948966*double(pan)/127;
        const float gains[2]={pan==127?0.0f:float(std::cos(angle)),pan==0?0.0f:float(std::sin(angle))};
        for(size_t i=0;i<std::max(a.size(),ref.size());++i) {
            const float av=i<a.size()?a[i]:0;
            const float bv=i<ref.size()?ref[i]*gains[i&1]:0;
            if(!std::isfinite(av) || std::fabs(av-bv)>1e-6f)return false;
        }
        return true;
    };
    for(int family:{0,1,2}) {
        const auto ref=renderAll(make(2,family,-1));
        for(int pan=0;pan<128;++pan) {
            const auto f=make(3,family,pan);
            const auto sound=renderAll(f);
            check(equivalent(sound,ref,pan),"all128 MA7 channel pan values follow independent cosine/sine reference for FM/WT/Audio");
            if(pan==0 || pan==127)for(size_t i=size_t(pan==0?1:0);i<sound.size();i+=2)
                check(sound[i]==0,"MA7 hard pan silences opposite channel exactly");
        }
        const auto defaultFile=make(3,family,-1),center=make(3,family,64);
        check(renderAll(defaultFile)==renderAll(center),"MA7 default center64 equals explicit center including its small specified asymmetry");
        const auto high=make(3,family,32,16);
        check(renderAll(high)==renderAll(make(3,family,32)),"MA7 pan isolates both channel banks consistently");
        check(renderAll(high,1)==renderAll(high,1024),"MA7 pan is block independent");
        MaPlayer p;check(p.init(high,8000),"MA7 pan seek initializes");float buffer[514];
        while(p.render(buffer,257)>0){}p.seekToStart();std::vector<float> replay;int n;
        while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
        check(replay==renderAll(high),"MA7 pan seek resets gain snapshots");
        check(p.init(make(2,family,-1),8000),"legacy pan reinit");replay.clear();
        while((n=p.render(buffer,257))>0)replay.insert(replay.end(),buffer,buffer+n*2);
        check(replay==ref,"MA7 gain snapshots cannot leak into legacy init");
        auto live=make(3,family,0);
        live.tracks[0].sequenceData.resize(live.tracks[0].sequenceData.size()-4);
        live.tracks[0].sequenceData.insert(live.tracks[0].sequenceData.end(),{10,0x30,10,127,30,0xff,0x2f,0});
        const auto held=renderAll(live);
        float left=0,right=0;
        for(size_t i=1920;i<std::min<size_t>(held.size(),3000);i+=2){left+=std::fabs(held[i]);right+=std::fabs(held[i+1]);}
        check(right>.001f && left<1e-5f,"live pan updates held FM/WT/Audio without retrigger after filter settles");
        auto isolated=live;const size_t event=isolated.tracks[0].sequenceData.size()-7;
        isolated.tracks[0].sequenceData[event]=0x31;
        check(renderAll(isolated)==renderAll(make(3,family,0)),"pan controller cannot affect another channel");
    }
    auto locked=make(3,1,0);locked.tracks[0].setupData[14]=uint8_t((15<<3)|1);
    auto lockedRight=locked;
    for(size_t i=0;i+3<lockedRight.tracks[0].sequenceData.size();++i)
        if(lockedRight.tracks[0].sequenceData[i]==0x30 && lockedRight.tracks[0].sequenceData[i+1]==10)
            lockedRight.tracks[0].sequenceData[i+2]=127;
    check(renderAll(locked)==renderAll(lockedRight),"authored5-bit WT pan PE ignores calibrated channel pan");
}
