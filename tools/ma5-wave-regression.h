#pragma once
// Synthetic data: native MA5 wave transport must preserve every raw byte.
void checkMa5NativeWaves() {
    for(int codec:{0,2}) {
        auto raw=fixture(), packed=fixture();
        const std::vector<uint8_t> voice{0x43,0x79,7,0x7f,1,124,0,0,0,1,
            0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,1};
        setVoice(raw,voice);setVoice(packed,voice);
        std::vector<uint8_t> body(80);
        for(size_t i=0;i<body.size();++i) body[i]=uint8_t(i*71+128);
        body[3]=0xf7;body[4]=0x80;body[5]=0xff;
        std::vector<uint8_t> wave{0x43,0x79,7,0x7f,3,1,uint8_t(codec)};
        wave.insert(wave.end(),body.begin(),body.end());appendExclusive(raw,wave);
        wave.resize(7);wave[2]=6;
        auto transport=packMa3(body);wave.insert(wave.end(),transport.begin(),transport.end());
        appendExclusive(packed,wave);
        for(auto* f:{&raw,&packed}) {
            f->tracks[0].sequenceData={0,0xb0,0,124,0,0x90,60,100,40,40,0xff,0x2f,0};
            WaveData collision;collision.number=1;collision.adpcm=false;collision.bitsPerSample=8;
            collision.data.assign(80,128);f->tracks[0].waves.push_back(collision);
        }
        const auto expected=renderAll(packed);
        check(renderAll(raw)==expected,"MA5 raw ADPCM/unsigned PCM8 equals MA3 packed bytes, internal F7 preserved");
        MaPlayer p;check(p.init(raw,8000),"MA5 wave fixture initializes");float buffer[514];
        while(p.render(buffer,257)>0){}
        check(p.diagnostics().pcmNotes>0 && p.diagnostics().fallbackNotes==0,"MA5 uses real instrument RAM, not Mwa or FM substitute");
        for(int badCodec:{1,3,4,127}) {
            auto rejected=raw;
            std::vector<uint8_t> bad{0x43,0x79,7,0x7f,3,1,uint8_t(badCodec),255,0,255};
            appendExclusive(rejected,bad);
            check(renderAll(rejected)==expected,"unknown MA5 codecs preserve valid waveform");
        }
    }
}
