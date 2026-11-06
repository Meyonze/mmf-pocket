// SPDX-License-Identifier: Apache-2.0
// Original synthetic samples. Verify source ownership and compatibility audio.
#include "smaf_file.h"
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <array>
#include <map>
#include <vector>
#define private public
#include "ma_player.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace fxchain::smaf;
void require(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
WaveData wave(int id,int part) {
    WaveData w;w.number=id;w.adpcm=false;w.bitsPerSample=8;w.samplingRate=8000;
    for(int i=0;i<80;++i)w.data.push_back(uint8_t(std::lround(128+24*std::sin(i*(part?.17:.31)))));
    return w;
}
TrackChunk track(int number,int part,int waveId=1) {
    TrackChunk t;t.trackNumber=number;t.formatType=2;t.durationTimeBase=t.gateTimeBase=0x10;
    t.waves.push_back(wave(waveId,part));
    std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,0,0,uint8_t(part),0,1,
        0x1f,0x40,0,0,0,0xf0,0xf0,0,0,0,0,0,0,0,80,uint8_t(waveId)};
    t.setupData={0xf0,uint8_t(voice.size()+1)};t.setupData.insert(t.setupData.end(),voice.begin(),voice.end());t.setupData.push_back(0xf7);
    t.sequenceData={0,0xc0,uint8_t(part),uint8_t(part?30:0),0x90,60,100,10,20,0xff,0x2f,0};
    return t;
}
std::vector<float> drain(MaPlayer& p,int block=257) {
    std::vector<float> out,b(size_t(block)*2);int n;
    while((n=p.render(b.data(),block))>0){require(out.size()<200000,"bounded synthetic score");out.insert(out.end(),b.begin(),b.begin()+2*n);}
    return out;
}
std::vector<float> render(const SmafFile& f){MaPlayer p;require(p.init(f,8000),"render initializes");return drain(p);}
int main() {
    int checks=0;
    for(int first:{0,1,31,127})for(int last:{2,16,128,255})for(bool reversed:{false,true}) {
        SmafFile f;f.tracks={track(first,0),track(last,1)};
        // Grow each owning source vector beyond its bound wave, including ID255.
        for(auto& t:f.tracks)for(int id:{2,15,31,63,127,255})t.waves.push_back(wave(id,0));
        auto atr=track(last,0);atr.isAudioTrack=true;atr.sequenceData.clear();atr.setupData.clear();f.tracks.push_back(atr);
        if(reversed)std::swap(f.tracks[0],f.tracks[1]);
        MaPlayer p;require(p.init(f,8000),"reference fixture initializes");
        require(p.scoreAudit().scanComplete,"ownership fixture decodes completely");
        PcmParams q;q.waveId=1;
        const int owner=reversed?first:last;
        const auto binding=p.instrumentWave_(q);
        require(binding.legacyMwa&&binding.sourceTrack==owner,"legacy last-score-track ownership is preserved");
        require(binding.sample==&p.scoreWaveBanks_.at(owner).at(1),"legacy selection aliases its owning sample");
        require(binding.sample->pcm.data()==p.scoreWaveBanks_.at(owner).at(1).pcm.data(),"PCM buffer is shared with the source bank");
        require(binding.sample!=&p.audioWaveBanks_.at(last).at(1),"ATR remains a distinct namespace");
        for(int id:{2,15,31,63,127,255}){q.waveId=id;const auto b=p.instrumentWave_(q);
            require(b.sample==&p.scoreWaveBanks_.at(owner).at(size_t(id)),"vector growth cannot stale a source reference");++checks;}
        q.waveId=1;q.voiceWaveOnly=true;require(!p.instrumentWave_(q).sample,"instrument-only RAM never borrows an Mwa");
        q.voiceWaveOnly=false;q.rom=true;require(!p.instrumentWave_(q).sample,"ROM never aliases an Mwa");
        q.rom=false;q.waveId=99;require(!p.instrumentWave_(q).sample,"missing ID never borrows another wave");
        q.waveId=1;p.voiceWaveBank_[1].pcm={1234};
        require(p.instrumentWave_(q).sample==&p.voiceWaveBank_[1]&&!p.instrumentWave_(q).legacyMwa,"instrument RAM retains precedence");
        p.voiceWaveVersions_.push_back(p.voiceWaveBank_[1]);p.voiceWaveVersions_.back().pcm={2345};p.activeVoiceWaveVersion_[1]=0;
        require(p.instrumentWave_(q).sample==&p.voiceWaveVersions_[0],"activated RAM version retains precedence");
        require(p.init(f,8000),"same player reinitializes");
        const auto expected=drain(p,257);p.seekToStart();require(drain(p,1)==expected,"seek preserves source audio");
        require(p.init(f,8000)&&drain(p,1024)==expected,"reinitialization preserves source audio");
        SmafFile empty;auto blank=track(first,0);blank.waves.clear();empty.tracks={blank};
        require(p.init(empty,8000)&&!p.instrumentWave_(q).sample,"reinitialization removes previous source references");
        checks+=10;
    }
    // Independent equivalent single-MTR oracle: both phrases use one wave.
    SmafFile shared;shared.tracks={track(0,0),track(1,1)};
    auto oracle=shared;oracle.tracks.resize(1);oracle.tracks[0].waves={wave(1,1)};
    oracle.tracks[0].sequenceData={0,0x90,60,100,10,30,0x90,60,100,10,20,0xff,0x2f,0};
    const auto expected=render(oracle);require(std::any_of(expected.begin(),expected.end(),[](float x){return x!=0;}),"compatibility oracle is audible");
    require(render(shared)==expected,"legacy waveform selection and audio stay unchanged");++checks;
    // A duplicate source track number also follows the final owned sample.
    shared.tracks[1].trackNumber=0;require(render(shared)==expected,"duplicate MTR number preserves legacy selection");++checks;
    std::cout<<"Wave reference ownership/lifetime checks: "<<checks<<" PASS\n";
}
