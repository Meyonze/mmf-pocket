// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original MA7 register images. No external music or injected engine state.
namespace ma7IncompleteFm {
std::vector<uint8_t> image(int algorithm, int operators, bool analogLite) {
    std::vector<uint8_t> voice{0x43,0x79,8,0x7f,0x21,124,0,0,0,127,
        uint8_t(analogLite?2:0),60,1,uint8_t(algorithm)};
    for(int i=0;i<operators;++i) {
        const std::vector<uint8_t> op{0,0x90,0xf0,uint8_t(i<2?48:0),0,0,0,0,0,uint8_t((i+1)<<4)};
        voice.insert(voice.end(),op.begin(),op.end());
    }
    if(analogLite)voice.insert(voice.end(),16,0);
    return voice;
}
void exclusive(std::vector<uint8_t>& bytes,const std::vector<uint8_t>& voice) {
    bytes.push_back(0xf0);bytes.push_back(uint8_t(voice.size()+1));
    bytes.insert(bytes.end(),voice.begin(),voice.end());bytes.push_back(0xf7);
}
SmafFile score(const std::vector<uint8_t>& good,const std::vector<uint8_t>& incomplete,int mode) {
    auto file=fixture(3);auto& track=file.tracks[0];
    // A malformed initial definition must not claim a key before the valid
    // setup voice. A malformed later definition must not displace it either.
    if(mode==1)exclusive(track.setupData,incomplete);
    exclusive(track.setupData,good);
    if(mode==2)exclusive(track.setupData,incomplete);
    track.sequenceData={0,0x30,0,124};
    if(mode==3) {track.sequenceData.push_back(0);exclusive(track.sequenceData,incomplete);}
    else track.sequenceData.insert(track.sequenceData.end(),{0,0xff,0});
    track.sequenceData.insert(track.sequenceData.end(),{0,0x10,60,100,20,20,0xff,0x2f,0});
    return file;
}
void checkIncompleteImages() {
    for(bool analogLite:{false,true}) {
        // Existing complete 4-operator containers may have unused registers.
        // This change only rejects a body missing operators the core will use.
        for(int algorithm=0;algorithm<2;++algorithm)for(int operators:{2,4}) {
            auto complete=image(algorithm,operators,analogLite);
            auto parsed=parseVoiceExclusive(complete.data(),complete.size());
            check(parsed.valid&&!parsed.patch.fourOp,"MA7 complete 2-op algorithm remains accepted in both container sizes");
        }
        for(int algorithm=2;algorithm<8;++algorithm) {
            auto good=image(algorithm,4,analogLite), incomplete=image(algorithm,2,analogLite);
            check(parseVoiceExclusive(good.data(),good.size()).valid,"MA7 complete 4-op FM/AL image remains accepted");
            check(!parseVoiceExclusive(incomplete.data(),incomplete.size()).valid,"MA7 4-op algorithm rejects an image with only two operator records");
            auto generic=score(good,incomplete,0);generic.tracks[0].setupData.clear();
            const auto genericExpected=renderAll(generic);
            for(bool inlineDefinition:{false,true}) {
                auto malformedOnly=score(good,incomplete,inlineDefinition?3:0);
                malformedOnly.tracks[0].setupData.clear();
                if(!inlineDefinition)exclusive(malformedOnly.tracks[0].setupData,incomplete);
                check(renderAll(malformedOnly)==genericExpected,"incomplete standalone MA7 image retains ordinary missing-voice fallback");
            }
            const auto expected=renderAll(score(good,incomplete,0));
            check(std::any_of(expected.begin(),expected.end(),[](float x){return std::fabs(x)>1e-4f;}),
                "MA7 complete reference voice is audible");
            for(int mode:{1,2,3}) {
                auto actual=score(good,incomplete,mode);
                check(renderAll(actual)==expected,"incomplete MA7 FM/AL definition cannot mask a complete voice in setup or sequence");
                check(renderAll(actual,1)==expected,"rejecting incomplete MA7 definitions is independent of render block size");
                MaPlayer player;check(player.init(actual,8000),"incomplete MA7 seek fixture initializes");
                float warmup[128];player.render(warmup,64);player.seekToStart();
                std::vector<float> replay,buffer(514);int count;
                while((count=player.render(buffer.data(),257))>0)
                    replay.insert(replay.end(),buffer.begin(),buffer.begin()+count*2);
                check(replay==expected,"seek does not reactivate rejected incomplete MA7 voices");
            }
        }
    }
}
} // namespace ma7IncompleteFm
