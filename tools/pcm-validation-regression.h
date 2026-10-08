#pragma once
// Original synthetic definition-validation fixtures; no song data.
namespace pcmValidationRegression {
std::vector<uint8_t> direct(int fs=8000) {
    return {0x43,0x79,7,0x7f,1,0,0,0,0,1,uint8_t(fs>>8),uint8_t(fs),
        0,0,0,0xf0,0xf0,0,0,0,0,0,0,0,80,1};
}
std::vector<uint8_t> expanded(int fs,int flags) {
    auto raw=direct(fs);
    std::vector<uint8_t> image{0x43,0x79,8,0x7f,0x21,0,0,0,0,0,uint8_t(flags)};
    image.insert(image.end(),raw.begin()+10,raw.begin()+19);
    image.push_back(0); // MA7 AEG low bits
    image.insert(image.end(),raw.begin()+19,raw.begin()+25);
    if(flags!=1) image.insert(image.end(),16,0); // AL tail, before WaveID
    image.push_back(1);
    if(flags==7) image.insert(image.end(),9,0); // neutral Pitch-EG
    return image;
}
std::vector<uint8_t> packed(int fs) {
    auto raw=direct(fs);raw[2]=6;
    std::vector<uint8_t> image(raw.begin(),raw.begin()+10);
    auto body=packMa3(std::vector<uint8_t>(raw.begin()+10,raw.end()));
    image.insert(image.end(),body.begin(),body.end());return image;
}
void appendTimed(std::vector<uint8_t>& seq,int delay,const std::vector<uint8_t>& image) {
    seq.insert(seq.end(),{uint8_t(delay),0xf0,uint8_t(image.size()+1)});
    seq.insert(seq.end(),image.begin(),image.end());seq.push_back(0xf7);
}
void run() {
    using D=ScoreAudit::Domain;using S=ScoreAudit::Status;
    std::vector<std::vector<uint8_t>> invalid;
    for(int fs:{0,1,1499,48001,65535}) {
        for(const auto& image:{direct(fs),packed(fs),expanded(fs,1),expanded(fs,3),expanded(fs,7)}) {
            const auto parsed=parseVoiceExclusive(image.data(),image.size());
            check(parsed.isPcm && !parsed.valid,"PCM kind is not proof of a valid definition");
            invalid.push_back(image);
        }
    }
    auto full=direct();
    for(size_t length=11;length<full.size();++length) {
        auto partial=full;partial.resize(length);
        check(parseVoiceExclusive(partial.data(),partial.size()).isPcm &&
              !parseVoiceExclusive(partial.data(),partial.size()).valid,"short direct PCM bodies stay invalid");
        invalid.push_back(partial);
    }
    for(int fs:{1500,8000,48000})
        for(const auto& image:{direct(fs),packed(fs),expanded(fs,1),expanded(fs,3),expanded(fs,7)}) {
            auto parsed=parseVoiceExclusive(image.data(),image.size());
            check(parsed.valid && parsed.isPcm && parsed.pcm.fs==fs,
                  "valid direct/packed/expanded/AL/neutral-PEG PCM retains Fs");
        }
    for(int format:{2,3}) for(bool pcm:{false,true}) {
        auto reference=fixture(format);
        std::vector<uint8_t> good;
        if(pcm) {addPcmFixture(reference);good=direct();setVoice(reference,good);}
        else {good=protocolRegression::tone();setVoice(reference,good);}
        uint8_t note=format==3?0x10:0x90;
        reference.tracks[0].sequenceData={0,note,60,100,10,20,note,64,100,10,10,0xff,0x2f,0};
        const auto expected=renderAll(reference);
        auto noVoice=reference;noVoice.tracks[0].setupData.clear();
        const auto fallback=renderAll(noVoice);
        for(const auto& bad:invalid) {
            auto setup=reference;setVoice(setup,bad);appendExclusive(setup,good);
            check(renderAll(setup)==expected,"invalid PCM setup cannot mask a valid later FM/PCM voice");
            auto timed=reference;
            timed.tracks[0].sequenceData={0,note,60,100,10};
            appendTimed(timed.tracks[0].sequenceData,20,bad);
            timed.tracks[0].sequenceData.insert(timed.tracks[0].sequenceData.end(),
                {0,note,64,100,10,10,0xff,0x2f,0});
            check(renderAll(timed)==expected,"invalid inline PCM cannot replace a working FM/PCM voice");
            MaPlayer p;check(p.init(timed,8000),"invalid PCM inventory initializes");
            int rejected=0,routed=0;
            for(const auto& group:p.scoreAudit().groups) {
                if(group.domain==D::Setup && group.status==S::Unsupported) rejected+=int(group.count);
                if(group.domain==D::VoiceDefinition && group.opcode==0 && group.status==S::Routed) routed+=int(group.count);
            }
            check(rejected==1 && routed==1,"invalid PCM is reported rejected, not routed as a valid voice");
            float block[514];while(p.render(block,257)>0){}
            p.seekToStart();std::vector<float> replay;int n;
            while((n=p.render(block,257))>0) replay.insert(replay.end(),block,block+2*n);
            check(replay==expected,"seek cannot activate an invalid PCM replacement");
            check(renderAll(timed,1)==renderAll(timed,1024),"invalid PCM rejection is block independent");
            auto standalone=noVoice;setVoice(standalone,bad);
            check(renderAll(standalone)==fallback,"invalid PCM alone preserves ordinary missing-voice fallback");
        }
    }
    // Reject invalid definitions, not valid shapes with unavailable ROM waves.
    for(int format:{2,3}) {
        auto file=fixture(format);auto rom=direct();rom.back()=0x80;
        setVoice(file,rom);uint8_t note=format==3?0x10:0x90;
        file.tracks[0].sequenceData={0,note,60,100,10,10,0xff,0x2f,0};
        MaPlayer p;check(p.init(file,8000),"valid ROM PCM initializes");
        check(p.scoreAudit().totals[size_t(S::Compatibility)]==1,"valid ROM PCM compatibility metadata retained");
        float block[514];while(p.render(block,257)>0){}
        check(p.diagnostics().fallbackNotes==1 && p.diagnostics().fmNotes==1,
              "valid unavailable ROM PCM keeps existing audible substitute");
    }
}
}
void checkPcmDefinitionValidation(){pcmValidationRegression::run();}
