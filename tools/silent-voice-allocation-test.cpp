// SPDX-License-Identifier: Apache-2.0
// Dense original fixtures: reclaim stopped envelopes before audible held notes.
#include "ma_player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
using namespace fxchain::smaf;
using Bytes=std::vector<uint8_t>;
void require(bool value,const char* message) {
    if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
void exclusive(SmafFile& file,const Bytes& voice) {
    auto& out=file.tracks[0].setupData;
    out.push_back(0xf0);out.push_back(uint8_t(voice.size()+1));
    out.insert(out.end(),voice.begin(),voice.end());out.push_back(0xf7);
}
SmafFile base(bool pcm,int format,int silentWave=0) {
    SmafFile file;TrackChunk track;track.formatType=uint8_t(format);track.trackNumber=0;
    track.durationTimeBase=track.gateTimeBase=0x10;file.tracks.push_back(track);
    Bytes voice;
    if(pcm) {
        voice={0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0xf0,0xf0,0,0,0,0,0,0,0,80,1};
        WaveData wave;wave.number=1;wave.bitsPerSample=16;wave.samplingRate=8000;
        for(int i=0;i<80;++i) {
            const auto v=uint16_t(int16_t(std::lround(4000*std::sin(6.283185307179586*i/40))));
            wave.data.push_back(uint8_t(v>>8));wave.data.push_back(uint8_t(v));
        }
        file.tracks[0].waves.push_back(wave);
    } else {
        voice={0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,
               0,0xf0,0,0xfc,0,0x10,0,0,0xf0,0xf0,0x30,0,0x10,0};
    }
    const auto audible=voice;
    exclusive(file,voice);
    voice[7]=1;voice[pcm?16:22]=silentWave?0xf0:0;
    if(silentWave)voice[26]=uint8_t(silentWave<<3); // Permanent-zero patch, independently triggered PC1.
    exclusive(file,voice);
    voice=audible;voice[7]=2;voice[pcm?15:21]=0x80;
    exclusive(file,voice); // Audibly ringing release tail.
    return file;
}
int capacity(bool pcm,int format){return pcm&&format==2?16:32;}
SmafFile dense(bool pcm,int format,bool high,bool stopped,bool future,bool released=false,int silentWave=0) {
    auto file=base(pcm,format,silentWave);using Event=std::pair<int,Bytes>;std::vector<Event> events;
    const auto status=[&](int kind,int channel){return uint8_t(format==2
        ? (kind==1?0x90:kind==3?0xb0:0xc0)+channel
        : kind*16+(channel&15)+(channel>=16?128:0));};
    const int silentChannel=high?17:1,nextChannel=high?18:2;
    const auto cc=[&](int at,int channel,int controller,int value){events.push_back({at,{status(3,channel),uint8_t(controller),uint8_t(value)}});};
    for(int c:{0,silentChannel,nextChannel,3}){cc(0,c,7,32);cc(0,c,11,127);}
    if(released)events.push_back({0,{status(4,3),2}});
    for(int i=0;i<capacity(pcm,format)-1;++i) {
        const bool tail=released&&i==capacity(pcm,format)-2;
        events.push_back({0,{status(1,tail?3:0),60,100,uint8_t(tail?1:20)}});
    }
    events.push_back({0,{status(4,silentChannel),1}});
    if(stopped)events.push_back({0,{status(1,silentChannel),60,100,3}});
    events.push_back({2,{status(4,silentChannel),0}});
    // Same trigger key and channel: the reclaimed note's old key-off must not
    // cut this longer replacement at tick3. A future steal must retain RR order.
    events.push_back({2,{status(1,silentChannel),60,100,20}});
    if(future)events.push_back({4,{status(1,nextChannel),67,100,15}});
    events.push_back({30,{0xff,0x2f,0}});
    std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b){return a.first<b.first;});
    int previous=0;auto& out=file.tracks[0].sequenceData;
    for(const auto& [at,data]:events){out.push_back(uint8_t(at-previous));out.insert(out.end(),data.begin(),data.end());previous=at;}
    return file;
}
std::vector<float> drain(MaPlayer& p,int block) {
    std::vector<float> result,buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0) {
        result.insert(result.end(),buffer.begin(),buffer.begin()+n*2);
        require(result.size()<8000*2*12,"dense fixture stays bounded");
    }
    require(p.finished()&&!p.diagnostics().tailLimitReached,"dense fixture ends naturally");
    return result;
}
void allocation() {
    unsigned cases=0;
    for(bool pcm:{false,true})for(int format:{2,3})for(bool high:{false,true}) {
        if(high&&format!=3)continue;
        for(bool future:{false,true})for(bool released:{false,true}) {
            MaPlayer reference;require(reference.init(dense(pcm,format,high,false,future,released),8000),"reference initializes");
            const auto expected=drain(reference,257);
            require(std::any_of(expected.begin(),expected.end(),[](float x){return std::abs(x)>1e-5f;}),"reference sounds");
            for(int block:{1,257,1024}) {
                MaPlayer actual;require(actual.init(dense(pcm,format,high,true,future,released),8000),"dense fixture initializes");
                require(drain(actual,block)==expected,"stopped envelope must be reclaimed before sounding held voices");
                const auto checkDiagnostics=[&]{const auto& d=actual.diagnostics();
                    require(d.fmNotes+d.pcmNotes==unsigned(capacity(pcm,format)+1+int(future)),"stopped note still registers normally");
                    require(d.stolenFm+d.stolenPcm==unsigned(1+int(future)),"occupied-slot reuse remains a counted steal");
                    require(d.stolenHeldFm+d.stolenHeldPcm==unsigned(future&&!released),"silent reuse preserves release-first policy and future held-steal order");
                    require(d.fallbackNotes==0&&d.suppressedSilentNotes==0,"all embedded notes retain tone routing");};
                checkDiagnostics();
                if(block==257){actual.seekToStart();require(drain(actual,block)==expected,"seek reproduces allocation and note-off IDs");checkDiagnostics();}
                ++cases;
            }
        }
    }
    std::cout<<"Silent-slot allocation PASS: "<<cases<<" dense cases, released/held protection, future steals, same-key note-offs, blocks and seeks\n";
}
void envelopeCertificate() {
    constexpr unsigned carriers[]={2,3,15,8,8,10,9,13};
    for(bool modern:{false,true})for(int algorithm=0;algorithm<8;++algorithm) {
        FmVoicePatch patch;patch.algorithm=uint8_t(algorithm);patch.fourOp=algorithm>=2;patch.modernTimbre=modern;
        for(int i=0;i<(patch.fourOp?4:2);++i) {
            auto& op=patch.ops[i];op.ar=carriers[algorithm]&(1u<<i)?0:15;
            op.xof=true;op.fb=7;op.wave=0;op.ksr=1;
        }
        FmVoice voice;voice.setSampleRate(8000);voice.noteOn(patch,220,1);
        require(voice.active()&&voice.silentForCurrentNote(),"all output envelopes stopped before first tick");
        for(int i=0;i<128;++i)require(voice.tick()==0,"modulation/feedback cannot bypass stopped carriers");
        voice.noteOff(true);voice.setHold(false);voice.setPitch(440);voice.setVolume(0);voice.setVolume(1);voice.setVibratoScale(8);
        require(voice.silentForCurrentNote(),"pitch, Hold, XOF, gain and vibrato cannot revive stopped envelopes");
        for(int carrier=0;carrier<(patch.fourOp?4:2);++carrier)if(carriers[algorithm]&(1u<<carrier)) {
            auto live=patch;live.ops[carrier].ar=15;
            voice.noteOn(live,220,1);voice.setVolume(0);
            require(!voice.silentForCurrentNote(),"any live carrier at zero channel gain remains protected");
            voice.setVolume(1e-12f);require(!voice.silentForCurrentNote(),"no nonzero gain threshold permits stealing");
            voice.setVolume(1);voice.setPitch(5000);
            require(!voice.silentForCurrentNote(),"Nyquist-suppressed live envelope remains protected");
            voice.setPitch(220);float peak=0;
            for(int i=0;i<128;++i)peak=std::max(peak,std::abs(voice.tick()));
            require(peak>0,"protected carrier revives after pitch change");
            live=patch;live.ops[carrier].rateLowBits=8;
            voice.noteOn(live,220,1);
            require(!voice.silentForCurrentNote(),"MA7 AR0 attack low bit prevents permanent-silence certificate");
            peak=0;for(int i=0;i<128;++i)peak=std::max(peak,std::abs(voice.tick()));
            require(peak>0,"minimum nonzero attack actually sounds");
        }
    }
    FmVoicePatch inconsistent;inconsistent.algorithm=4;inconsistent.fourOp=false;
    for(auto& op:inconsistent.ops)op.ar=0;
    FmVoice voice;voice.noteOn(inconsistent,220,1);
    require(!voice.silentForCurrentNote(),"incomplete public graph is never certified silent");
    std::cout<<"Permanent-silence certificate PASS: all algorithms, individual carriers and reversible suppression\n";
}
SmafFile reversible(bool pcm,int format,bool nyquist,int controller) {
    auto file=base(pcm,format);using Event=std::pair<int,Bytes>;std::vector<Event> events;
    const auto status=[&](int kind,int channel){return uint8_t(format==2
        ? (kind==1?0x90:kind==3?0xb0:kind==4?0xc0:0xe0)+channel
        : kind*16+channel);};
    for(int c:{0,1,2}){
        events.push_back({0,{status(3,c),7,32}});events.push_back({0,{status(3,c),11,127}});
        events.push_back({0,{status(3,c),10,uint8_t(c==1?127:0)}});
    }
    for(int i=0;i<capacity(pcm,format)-1;++i)events.push_back({0,{status(1,0),60,100,20}});
    if(!nyquist)events.push_back({0,{status(3,1),uint8_t(controller),0}});
    events.push_back({0,{status(1,1),uint8_t(nyquist?108:60),100,20}});
    events.push_back({2,{status(1,2),60,100,20}});
    if(nyquist)events.push_back({3,{status(6,1),0,0}}); // Full negative default two-semitone bend.
    else events.push_back({3,{status(3,1),uint8_t(controller),uint8_t(controller==7?32:127)}});
    events.push_back({30,{0xff,0x2f,0}});
    int previous=0;auto& out=file.tracks[0].sequenceData;
    std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b){return a.first<b.first;});
    for(const auto& [at,data]:events){out.push_back(uint8_t(at-previous));out.insert(out.end(),data.begin(),data.end());previous=at;}
    return file;
}
void reversibleSuppression() {
    for(bool pcm:{false,true})for(int format:{2,3})for(int controller:{7,11})for(bool nyquist:{false,true}) {
        if(nyquist&&(pcm||controller!=7))continue;
        const auto source=reversible(pcm,format,nyquist,controller);
        std::vector<float> expected;
        for(int block:{1,257,1024}) {
            MaPlayer player;require(player.init(source,8000),"reversible suppression fixture initializes");
            const auto audio=drain(player,block);const auto& d=player.diagnostics();
            require(d.stolenHeldFm+d.stolenHeldPcm==1,"temporarily silent last slot must not replace round-robin audible victim");
            float before=0,after=0;
            for(size_t frame=0;frame<audio.size()/2;++frame) {
                if(frame<240)before=std::max(before,std::abs(audio[frame*2+1]));
                if(frame>=320&&frame<1200)after=std::max(after,std::abs(audio[frame*2+1]));
            }
            require(before==0&&after>1e-6f,"right-panned protected note returns after future controller/bend");
            if(expected.empty())expected=audio;else require(audio==expected,"reversible suppression is block invariant");
            if(block==257){player.seekToStart();require(drain(player,block)==expected,"seek preserves reversible suppression");}
        }
    }
    std::cout<<"Reversible suppression PASS: FM/WT volume/expression and FM Nyquist revival under pool pressure\n";
}

void unboundWaveCertificate() {
    constexpr unsigned carriers[]={2,3,15,8,8,10,9,13};
    for(bool modern:{false,true})for(int algorithm=0;algorithm<8;++algorithm)for(int wave:{15,23,31,47,55,63}) {
        FmVoicePatch patch;patch.modernTimbre=modern;patch.algorithm=uint8_t(algorithm);patch.fourOp=algorithm>=2;
        const int count=patch.fourOp?4:2;
        for(int i=0;i<count;++i){patch.ops[i].ar=15;patch.ops[i].wave=uint8_t(carriers[algorithm]&(1u<<i)?wave:0);patch.ops[i].fb=7;}
        FmVoice voice;voice.setSampleRate(8000);voice.noteOn(patch,220,1);
        require(voice.silentForCurrentNote(),"unbound carrier waveforms cannot revive during this note");
        voice.setPitch(440);voice.setVibratoScale(8);voice.setVolume(0);voice.setVolume(1);voice.noteOff(true);
        for(int i=0;i<128;++i)require(voice.tick()==0&&voice.silentForCurrentNote(),"live modulation, feedback, pitch and Hold cannot bypass zero carrier waveforms");
        for(int live=0;live<count;++live)if(carriers[algorithm]&(1u<<live)){
            auto mixed=patch;mixed.ops[live].wave=0;
            voice.noteOn(mixed,220,1);require(!voice.silentForCurrentNote(),"one sounding carrier protects mixed unbound graphs");
            float peak=0;for(int i=0;i<128;++i)peak=std::max(peak,std::abs(voice.tick()));
            require(peak>0,"protected mixed graph actually sounds");
        }
    }
    std::cout<<"Unbound waveform certificate PASS: all algorithms, legacy/modern, masked selectors and live-carrier guards\n";
}
void unboundWaveAllocation() {
    unsigned cases=0;
    for(int format:{2,3})for(int wave:{15,23,31})for(bool future:{false,true})for(bool released:{false,true}) {
        MaPlayer reference;require(reference.init(dense(false,format,false,false,future,released),8000),"unbound-wave reference initializes");
        const auto expected=drain(reference,257);
        for(int block:{1,257,1024}){
            MaPlayer actual;require(actual.init(dense(false,format,false,true,future,released,wave),8000),"unbound-wave pressure initializes");
            require(drain(actual,block)==expected,"unbound user-wave slots must not displace audible held notes or release tails");
            const auto& d=actual.diagnostics();require(d.fmNotes==unsigned(33+int(future))&&d.suppressedSilentNotes==0,"unbound waveform still registers its note");
            require(d.stolenHeldFm==unsigned(future&&!released),"unbound waveform reclamation preserves held round-robin state");
            if(block==257){actual.seekToStart();require(drain(actual,block)==expected,"unbound waveform reclamation survives seek");}
            ++cases;
        }
    }
    std::cout<<"Unbound waveform allocation PASS: "<<cases<<" cases\n";
}
void zeroPcmAllocation() {
    for(int format:{2,3})for(bool future:{false,true})for(bool released:{false,true}) {
        auto fixture=[&](bool includeSilent){auto f=dense(true,format,false,includeSilent,future,released);
            auto& setup=f.tracks[0].setupData;const size_t second=2+setup[1];
            setup[second+2+16]=0xf0;setup[second+2+25]=2;
            auto zero=f.tracks[0].waves[0];zero.number=2;zero.adpcm=false;std::fill(zero.data.begin(),zero.data.end(),0);f.tracks[0].waves.push_back(zero);return f;};
        MaPlayer reference;require(reference.init(fixture(false),8000),"zero PCM reference initializes");const auto expected=drain(reference,257);
        for(int block:{1,257,1024}){MaPlayer actual;require(actual.init(fixture(true),8000),"zero PCM pressure initializes");
            require(drain(actual,block)==expected,"whole-zero PCM must not displace audible held notes or release tails");
            require(actual.diagnostics().stolenHeldPcm==unsigned(future&&!released),"zero PCM preserves held round-robin order");
            if(block==257){actual.seekToStart();require(drain(actual,block)==expected,"zero PCM reclaim survives seek");}}
    }
    for(int format:{2,3}) {
        auto file=dense(true,format,false,true,false,false);
        auto& setup=file.tracks[0].setupData;const size_t second=2+setup[1];
        setup[second+2+16]=0xf0;setup[second+2+25]=2;
        auto tiny=file.tracks[0].waves[0];tiny.number=2;tiny.adpcm=false;std::fill(tiny.data.begin(),tiny.data.end(),0);tiny.data.back()=1;
        file.tracks[0].waves.push_back(tiny);
        MaPlayer actual;require(actual.init(file,8000),"tiny nonzero PCM pressure initializes");drain(actual,257);
        require(actual.diagnostics().stolenHeldPcm==1,"one nonzero PCM sample protects the slot without a silence threshold");
    }
    std::cout<<"Whole-zero PCM allocation PASS:24 cases\n";
}
int main(){zeroPcmAllocation();unboundWaveCertificate();unboundWaveAllocation();allocation();envelopeCertificate();reversibleSuppression();}

