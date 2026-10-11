// SPDX-License-Identifier: Apache-2.0
// Host-only scheduled tone-path inventory. Never a listening/voice-lifetime test.
#include "smaf_file.h"
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <array>
#include <cstdint>
#include <map>
#include <vector>
#define private public
#include "ma_player.h"
#undef private
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
using namespace fxchain::smaf;
namespace fs=std::filesystem;
namespace toneUsage {
struct Gaps { uint64_t al=0,fixed=0,dt=0,sram=0; };
struct Usage {
    bool initialized=false,complete=false;
    uint64_t definitions=0,requestedDefs=0,boundDefs=0,notes=0,afterBound=0;
    uint64_t zeroVelocity=0,immutableSilent=0,fm=0,wt=0,audio=0,atr=0,noInstance=0;
    uint64_t fallback=0,romSource=0,ramSource=0,missingAudio=0,epZero=0;
    Gaps requested,bound;
};
void gap(const ParsedVoice& v,Gaps& g) {
    g.al+=v.al.present;
    bool fixed=false,dt=false,sram=false;
    if(!v.isPcm)for(int i=0;i<(v.patch.fourOp?4:2);++i) {
        const auto& o=v.patch.ops[size_t(i)];fixed|=o.fixedFrequency;
        dt|=o.ma7Multipliers&&o.ma7Detune!=0;
        sram|=o.wave==15||o.wave==23||o.wave==31;
    }
    g.fixed+=fixed;g.dt+=dt;g.sram+=sram;
}
Usage measure(MaPlayer& p,bool initialized) {
    Usage u;u.initialized=initialized;u.complete=p.scoreAudit().scanComplete;
    u.definitions=p.voiceTable_.size();if(!initialized)return u;
    std::set<size_t> requests,bound;
    const uint64_t ceiling=std::min(p.totalSamples(),uint64_t(p.rate_)*600);
    for(size_t index=0;index<p.events_.size();++index) {
        const auto& e=p.events_[index];
        if(e.sample>=ceiling){++u.afterBound;continue;}
        const bool note=e.type==MaPlayer::Ev::NoteOn||e.type==MaPlayer::Ev::Ma7NoteOn;
        const auto& c=p.chans_[e.ch&127];
        const bool ma7=e.type==MaPlayer::Ev::Ma7NoteOn;
        const bool silent=note&&ma7&&(e.b==1||(c.bankMsb==121&&c.bankLsb==6));
        const int source=ma7?e.sourceTrack:c.streamTrack;
        // Same routing guard, not a wave-existence guess. This branch never
        // resolves an instrument, even if its wave is missing or muted.
        const bool recording=note&&source>=0&&c.bankMsb==125&&c.bankLsb<=9&&c.program<=9
            &&(e.a<=12||(e.a>=92&&e.a<=110));
        const ParsedVoice* selected=note&&!recording&&e.b>0?p.resolveVoice_(e.ch,e.a):nullptr;
        const auto before=p.diagnostics();
        p.cursor_=e.sample;p.fireEvent_(e);p.nextEvent_=index+1;
        const auto after=p.diagnostics();
        const auto fm=after.fmNotes-before.fmNotes,pcm=after.pcmNotes-before.pcmNotes;
        const auto audio=after.audioNotes-before.audioNotes,fb=after.fallbackNotes-before.fallbackNotes;
        u.fm+=fm;u.audio+=audio;u.fallback+=fb;u.missingAudio+=after.missingAudio-before.missingAudio;
        if(e.type==MaPlayer::Ev::WaveOn){u.atr+=pcm;continue;}
        if(!note)continue;
        ++u.notes;u.zeroVelocity+=e.b<=0;u.immutableSilent+=silent&&e.b>0;
        u.wt+=pcm-audio;
        if(fm+pcm==0) {
            ++u.noInstance;
            // Only successful no-pronunciation with EP0: missing RAM/ROM
            // instead increments FM/fallback before reaching this branch.
            if(selected&&selected->isPcm&&selected->pcm.endPt==0&&e.b>0&&!silent)++u.epZero;
        }
        if(recording||!selected||e.b<=0||silent)continue;
        const size_t id=size_t(selected-p.voiceTable_.data());requests.insert(id);
        // Requested fields are counted only for instantiated non-permanently
        // silent paths. Intentional EP0 remains a request, not an audible gap.
        if(fm+pcm>0)gap(*selected,u.requested);
        if(fb) {if(selected->isPcm){if(selected->pcm.rom)u.romSource+=fb;else u.ramSource+=fb;}continue;}
        if((fm&&!selected->isPcm)||(pcm>audio&&selected->isPcm)) {
            bound.insert(id);gap(*selected,u.bound);
        }
    }
    u.requestedDefs=requests.size();u.boundDefs=bound.size();return u;
}
Usage measure(const SmafFile& f){MaPlayer p;const bool ok=p.init(f,44100);return measure(p,ok);}
int audit(const fs::path& root,const fs::path& report);
void require(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
SmafFile fixture(int note=60) {
    SmafFile f;TrackChunk t;t.formatType=2;t.trackNumber=0;
    t.durationTimeBase=t.gateTimeBase=0;t.channelStatus.resize(16);
    t.sequenceData={0,0x90,uint8_t(note),100,10};
    // Original complete MA5 short2-op image; tests below inject metadata to
    // test inventory classification, not claim new wire-format knowledge.
    std::vector<uint8_t> v{0x43,5,1,0,0,60,1,0};
    for(int i=0;i<2;++i)v.insert(v.end(),{0,0x90,0xf0,0,0,0x10,0});
    t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
    f.tracks.push_back(t);return f;
}
void selfTest() {
    auto f=fixture();MaPlayer p;require(p.init(f,44100),"usage fixture initializes");
    require(p.voiceTable_.size()==1,"one parsed definition");
    auto& v=p.voiceTable_[0];v.al.present=true;v.patch.ops[0].fixedFrequency=true;
    v.patch.ops[0].ma7Multipliers=true;v.patch.ops[0].ma7Detune=1;v.patch.ops[0].wave=15;
    auto unused=v;unused.key.pc=1;p.voiceTable_.push_back(unused);
    auto u=measure(p,true);
    require(u.definitions==2&&u.requestedDefs==1&&u.boundDefs==1&&u.fm==1&&u.fallback==0,"unused definition not counted as selected");
    require(u.requested.al==1&&u.bound.fixed==1&&u.bound.dt==1&&u.bound.sram==1,"bound FM gaps classified by used operator count");
    p.seekToStart();require(measure(p,true).boundDefs==u.boundDefs,"seek resets diagnostic replay");
    MaPlayer count;require(count.init(f,44100),"operator fixture initializes");count.voiceTable_[0].patch.ops[3].wave=15;
    require(measure(count,true).bound.sram==0,"unused operators in2-op patch cannot create gaps");
    auto fmOnly=f;fmOnly.tracks[0].setupData.clear();u=measure(fmOnly);
    require(u.fm==1&&u.fallback==1&&u.romSource==0&&u.ramSource==0,"generic fallback is not all ROM fallback");
    for(bool rom:{false,true})for(bool present:{false,true})for(int ep:{0,40}) {
        MaPlayer pcm;require(pcm.init(f,44100),"PCM classification initializes");
        auto& patch=pcm.voiceTable_[0];patch.isPcm=true;patch.pcm.rom=rom;patch.pcm.voiceWaveOnly=true;
        patch.pcm.waveId=1;patch.pcm.endPt=ep;patch.pcm.env.ar=15;patch.al.present=true;
        if(present)pcm.voiceWaveBank_[1].pcm.assign(80,4000);
        u=measure(pcm,true);
        if(rom||!present)require(u.fm==1&&u.fallback==1&&u.bound.al==0&&u.requested.al==1&&u.romSource==rom&&u.ramSource==!rom,"requested PCM metadata differs from fallback synthesis");
        else if(ep==0)require(u.epZero==1&&u.fm==0&&u.wt==0&&u.requested.al==0,"EP0 success has no instance; gap is not assumed");
        else require(u.wt==1&&u.bound.al==1&&u.fallback==0,"bound RAM WT classification");
    }
    for(bool present:{false,true}) {
        MaPlayer audio;auto file=fixture(0);
        if(present){WaveData w;w.number=1;w.samplingRate=8000;w.bitsPerSample=8;w.signedPcm8=true;w.adpcm=false;w.data.assign(80,16);file.tracks[0].waves.push_back(w);}
        require(audio.init(file,44100),"recording branch initializes");audio.chans_[0].bankMsb=125;
        audio.voiceTable_[0].key.bankMSB=125;audio.voiceTable_[0].al.present=true;
        require(audio.resolveVoice_(0,0)!=nullptr,"unused conflicting instrument genuinely resolves before branch");
        u=measure(audio,true);require(u.requestedDefs==0&&u.requested.al==0&&u.boundDefs==0&&u.fallback==0,"recording precedence never counts conflicting instrument");
        require(u.audio==present&&u.missingAudio==!present,"recording missing wave never becomes fallback FM");
    }
    for(bool bankMute:{false,true}) {
        MaPlayer mute;require(mute.init(f,44100),"mute initializes");mute.events_[0].type=MaPlayer::Ev::Ma7NoteOn;
        mute.events_[0].b=bankMute?100:1;
        if(bankMute){mute.chans_[0].bankMsb=121;mute.chans_[0].bankLsb=6;mute.voiceTable_[0].key.bankMSB=121;mute.voiceTable_[0].key.bankLSB=6;}
        mute.voiceTable_[0].al.present=true;u=measure(mute,true);
        require(u.immutableSilent==1&&u.fm==1&&u.boundDefs==0&&u.bound.al==0,"immutable MA7 silence still fires but cannot count as gap use");
    }
    MaPlayer zero;require(zero.init(f,44100),"zero volume initializes");zero.chans_[0].volume=0;
    require(measure(zero,true).boundDefs==1,"channel volume zero is not permanent silence");
    MaPlayer zeroVelocity;require(zeroVelocity.init(f,44100),"zero velocity initializes");
    zeroVelocity.events_[0].b=0;u=measure(zeroVelocity,true);
    require(u.zeroVelocity==1&&u.noInstance==1&&u.requestedDefs==0&&u.fm==0&&u.wt==0,"zero velocity cannot become an instrument request");
    MaPlayer timed;require(timed.init(f,44100),"timed fixture initializes");
    auto late=timed.voiceTable_[0];late.sequenced=true;late.activated=false;late.activationSample=441;late.al.present=true;timed.voiceTable_.push_back(late);
    auto noteEvent=timed.events_[0];noteEvent.sample=441;noteEvent.noteId=999;
    timed.events_={timed.events_[0],{441,MaPlayer::Ev::VoiceActivation,0,1,0},noteEvent};
    u=measure(timed,true);require(u.fm==2&&u.boundDefs==2&&u.bound.al==1,"same-key timed activation and tied event order retain distinct definitions");
    MaPlayer limit;require(limit.init(f,44100),"bound fixture initializes");limit.events_.push_back({uint64_t(44100)*600,MaPlayer::Ev::NoteOn,0,60,100,999});
    u=measure(limit,true);require(u.fm==1&&u.afterBound==1,"events beyond playback safety bound are explicit exclusions");
    // Original generated sentinel, not music. Output must never truncate any
    // input, including a hard link or an input discovered inside a folder.
    const auto temporary=fs::temp_directory_path()/(
        "mmf-tone-usage-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(fs::create_directory(temporary),"create unique safety fixture directory");
    const auto input=temporary/"original.mmf",alias=temporary/"report.tsv";
    const std::string sentinel="Original synthetic input must remain intact";
    {std::ofstream out(input,std::ios::binary);out<<sentinel;require(bool(out),"write original sentinel");}
    auto intact=[&]{std::ifstream in(input,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{})==sentinel;};
    require(audit(input,input)!=0&&intact(),"same-file report refuses to truncate input");
    require(audit(temporary,input)!=0&&intact(),"folder-discovered input is protected");
    fs::create_hard_link(input,alias);
    require(audit(input,alias)!=0&&intact(),"hard-linked output refuses to truncate input");
    fs::remove(alias);fs::remove(input);fs::remove(temporary);
    std::cout<<"Scheduled tone usage tests PASS\n";
}
std::string utf8(const fs::path& p){const auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
int audit(const fs::path& root,const fs::path& report) {
    std::vector<fs::path> paths;
    if(fs::is_regular_file(root))paths.push_back(root);
    else for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file()) {
        auto ext=e.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(ext==".mmf")paths.push_back(e.path());
    }
    std::sort(paths.begin(),paths.end());
    if(paths.empty()){std::cerr<<"No MMF input files found\n";return 3;}
    // Check identity before opening any destructive output handle. equivalent
    // handles case aliases, symbolic links and hard links, unlike string paths.
    if(fs::exists(report))for(const auto& path:paths)if(fs::equivalent(path,report)) {
        std::cerr<<"Report path identifies an input file; refusing overwrite\n";return 2;
    }
    std::ofstream out(report,std::ios::binary|std::ios::trunc);if(!out)return 2;
    out<<"file\tinitialized\tcomplete\tdefinitions\trequested_defs\tbound_defs\tnotes\tafter_bound\tzero_velocity\timmutable_silent\tfm\twt\trecorded\tatr\tno_instance\tfallback\trom_source_fallback\tram_source_fallback\tmissing_recorded\tep_zero\trequested_al\tbound_al\trequested_fixed\tbound_fixed\trequested_dt\tbound_dt\trequested_sram\tbound_sram\n";
    for(const auto& path:paths) {
        if(fs::file_size(path)>(16u<<20))return 3;
        std::ifstream in(path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};SmafFile f;
        if(!in||!f.parse(bytes.data(),bytes.size()))return 3;const auto u=measure(f);
        auto name=utf8(fs::is_regular_file(root)?path.filename():path.lexically_relative(root));for(char& c:name)if(c=='\t'||c=='\r'||c=='\n')c=' ';
        out<<name<<'\t'<<u.initialized<<'\t'<<u.complete;
        for(auto x:{u.definitions,u.requestedDefs,u.boundDefs,u.notes,u.afterBound,u.zeroVelocity,u.immutableSilent,u.fm,u.wt,u.audio,u.atr,u.noInstance,u.fallback,u.romSource,u.ramSource,u.missingAudio,u.epZero,u.requested.al,u.bound.al,u.requested.fixed,u.bound.fixed,u.requested.dt,u.bound.dt,u.requested.sram,u.bound.sram})out<<'\t'<<x;
        out<<'\n';if(!u.initialized||!u.complete)return 4;
    }
    out.flush();std::cout<<"Scheduled tone inventory files="<<paths.size()<<"; routing only, no waveform/lifetime/listening claim.\n";return out?0:5;
}
} // namespace toneUsage
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    if(argc==2&&std::wstring(argv[1])==L"--self-test"){toneUsage::selfTest();return 0;}
#else
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--self-test"){toneUsage::selfTest();return 0;}
#endif
    if(argc!=3){std::cerr<<"usage: tone-usage-audit <MMF-or-folder> <local-report.tsv> | --self-test\n";return 2;}
    try{return toneUsage::audit(fs::path(argv[1]),fs::path(argv[2]));}catch(...){std::cerr<<"tone usage inventory failed\n";return 2;}
}
