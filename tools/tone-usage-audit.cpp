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
#include <compare>
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
struct Route {
    int channel=-1,source=-1,bankMsb=-1,bankLsb=-1,program=-1,note=-1;
    int definition=-1,wave=-1,modern=0;
    int waveSource=-1,legacyMwaTracks=0;
    std::string waveBinding="-";
    std::string reason,substitute="-";
    // Requested PCM metadata is not necessarily used by an FM substitute.
    int pcmTl=-1,pcmAr=-1,pcmDr=-1,pcmSl=-1,pcmSr=-1,pcmRr=-1;
    int pcmEnvelopeApplied=-1;
    uint64_t al=0,fixed=0,dt=0,sram=0;
    auto operator<=>(const Route&) const = default;
};
struct Occurrences { uint64_t count=0,first=0,last=0; };
struct Details {
    // Bound memory independently of the number of notes/unique bank changes.
    static constexpr size_t maxGroups=4096;
    std::map<Route,Occurrences> groups;
    uint64_t overflow=0;
    void add(const Route& route,uint64_t sample) {
        auto found=groups.find(route);
        if(found==groups.end()) {
            if(groups.size()>=maxGroups){++overflow;return;}
            found=groups.emplace(route,Occurrences{0,sample,sample}).first;
        }
        ++found->second.count;found->second.last=sample;
    }
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
Usage measure(MaPlayer& p,bool initialized,Details* details=nullptr) {
    Usage u;u.initialized=initialized;u.complete=p.scoreAudit().scanComplete;
    u.definitions=p.voiceTable_.size();if(!initialized)return u;
    std::set<size_t> requests,bound;
    // Source banks stay immutable during scheduled replay. Count once rather
    // than scanning every MTR again for each legacy WT note.
    std::map<int,int> legacyCandidates;
    if(details)for(const auto& entry:p.scoreWaveBanks_)
        for(size_t id=0;id<entry.second.size();++id)
            if(!entry.second[id].pcm.empty())++legacyCandidates[int(id)];
    const uint64_t ceiling=std::min(p.totalSamples(),uint64_t(p.rate_)*600);
    for(size_t index=0;index<p.events_.size();++index) {
        const auto& e=p.events_[index];
        if(e.sample>=ceiling){++u.afterBound;continue;}
        const bool note=e.type==MaPlayer::Ev::NoteOn||e.type==MaPlayer::Ev::Ma7NoteOn;
        const auto& c=p.chans_[e.ch&127];
        const bool ma7=e.type==MaPlayer::Ev::Ma7NoteOn;
        const bool modern=note&&(e.sourceProfile&1)?!e.handyPhoneNote:c.modernTimbre;
        const bool rhythm=note&&(e.sourceProfile&1)?(e.sourceProfile&2)!=0:c.rhythm;
        const bool silent=note&&ma7&&(e.b==1||(c.bankMsb==121&&c.bankLsb==6));
        const int source=e.sourceTrack>=0?e.sourceTrack:c.streamTrack;
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
        if(details&&(note||e.type==MaPlayer::Ev::WaveOn)) {
            Route r;r.channel=e.ch;r.source=source;r.bankMsb=c.bankMsb;r.bankLsb=c.bankLsb;
            r.program=c.program;r.note=e.a;r.modern=modern;
            if(selected) {
                r.definition=int(selected-p.voiceTable_.data());
                if(selected->isPcm) {
                    r.wave=selected->pcm.waveId;
                    const auto& env=selected->pcm.env;
                    r.pcmTl=env.tl;r.pcmAr=env.ar;r.pcmDr=env.dr;
                    r.pcmSl=env.sl;r.pcmSr=env.sr;r.pcmRr=env.rr;
                    r.pcmEnvelopeApplied=pcm>audio?1:0;
                }
                Gaps g;gap(*selected,g);r.al=g.al;r.fixed=g.fixed;r.dt=g.dt;r.sram=g.sram;
            }
            if(e.type==MaPlayer::Ev::WaveOn) {
                r.source=e.b;r.wave=e.a;r.bankMsb=r.bankLsb=r.program=r.note=-1;
                r.reason=pcm?"bound_atr":"missing_atr_wave";
            } else if(e.b<=0)r.reason="zero_velocity";
            else if(silent)r.reason="immutable_silent";
            else if(recording) {
                r.wave=e.a<=12?e.a+1:e.a-92+14;
                r.reason=audio?"bound_recorded":after.missingAudio>before.missingAudio
                    ?"missing_recorded_wave":"recorded_no_instance";
            } else if(fb) {
                r.reason=selected&&selected->isPcm
                    ?(selected->pcm.rom?"explicit_rom_wave":"missing_ram_wave")
                    :"no_matching_supported_voice";
                const bool romRecipe=selected&&selected->isPcm&&selected->pcm.rom
                    &&modern&&FmVoicePatch::romApprox(selected->pcm.waveId);
                const bool pcmDrum=selected&&selected->valid&&selected->isPcm
                    &&(selected->key.bankMSB==125||selected->key.drumNote!=0);
                r.substitute=romRecipe?"rom_recipe":pcmDrum||rhythm||c.drum||c.bankMsb==125
                    ?"drum_recipe":modern?"gm_modern_recipe":"gm_legacy_recipe";
            } else if(fm)r.reason="bound_fm";
            else if(pcm>audio)r.reason="bound_ram";
            else if(selected&&selected->isPcm&&selected->pcm.endPt==0)r.reason="ep_zero";
            else r.reason="no_instance";
            if(e.type==MaPlayer::Ev::WaveOn && pcm) {
                r.waveBinding="atr_awa";r.waveSource=e.b;
            } else if(recording && audio) {
                r.waveBinding="recorded_mwa";r.waveSource=source;
            } else if(selected&&selected->valid&&selected->isPcm&&e.b>0&&!silent) {
                if(selected->pcm.rom) r.waveBinding="rom_address";
                else {
                    const auto binding=p.instrumentWave_(selected->pcm);
                    r.waveBinding=!binding.sample?"missing_ram":binding.legacyMwa?"legacy_mwa":"instrument_ram";
                    r.waveSource=binding.sourceTrack;
                    const auto candidates=legacyCandidates.find(r.wave);
                    if(binding.legacyMwa&&candidates!=legacyCandidates.end())r.legacyMwaTracks=candidates->second;
                }
            }
            details->add(r,e.sample);
        }
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
int audit(const fs::path& root,const fs::path& report,bool details=false);
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
        patch.pcm.env.tl=28;patch.pcm.env.dr=4;patch.pcm.env.sl=7;
        patch.pcm.env.sr=2;patch.pcm.env.rr=9;
        if(present)pcm.voiceWaveBank_[1].pcm.assign(80,4000);
        Details detail;u=measure(pcm,true,&detail);
        const auto& r=detail.groups.begin()->first;
        require(r.pcmTl==28&&r.pcmAr==15&&r.pcmDr==4&&r.pcmSl==7&&r.pcmSr==2&&r.pcmRr==9,
                "balance detail retains all requested PCM level/envelope fields");
        require(r.pcmEnvelopeApplied==(!rom&&present&&ep>0?1:0),
                "only an instantiated WT voice applies its PCM envelope, not FM fallback or EP0");
        if(rom||!present) {
            require(u.fm==1&&u.fallback==1&&u.bound.al==0&&u.requested.al==1&&u.romSource==rom&&u.ramSource==!rom,"requested PCM metadata differs from fallback synthesis");
            require(r.reason==(rom?"explicit_rom_wave":"missing_ram_wave")&&r.wave==1&&r.definition==0,
                "PCM detail preserves selected definition/wave even at EP0");
        } else if(ep==0) {
            require(u.epZero==1&&u.fm==0&&u.wt==0&&u.requested.al==0,"EP0 success has no instance; gap is not assumed");
            require(r.reason=="ep_zero"&&r.substitute=="-","EP0 with bound wave is intentional no-pronunciation");
        } else {
            require(u.wt==1&&u.bound.al==1&&u.fallback==0,"bound RAM WT classification");
            require(r.reason=="bound_ram"&&r.substitute=="-","RAM detail has no substitute recipe");
        }
    }
    for(bool present:{false,true}) {
        MaPlayer audio;auto file=fixture(0);
        if(present){WaveData w;w.number=1;w.samplingRate=8000;w.bitsPerSample=8;w.signedPcm8=true;w.adpcm=false;w.data.assign(80,16);file.tracks[0].waves.push_back(w);}
        require(audio.init(file,44100),"recording branch initializes");audio.chans_[0].bankMsb=125;
        audio.voiceTable_[0].key.bankMSB=125;audio.voiceTable_[0].al.present=true;
        require(audio.resolveVoice_(0,0)!=nullptr,"unused conflicting instrument genuinely resolves before branch");
        Details detail;u=measure(audio,true,&detail);require(u.requestedDefs==0&&u.requested.al==0&&u.boundDefs==0&&u.fallback==0,"recording precedence never counts conflicting instrument");
        require(u.audio==present&&u.missingAudio==!present,"recording missing wave never becomes fallback FM");
        const auto& r=detail.groups.begin()->first;
        require(r.reason==(present?"bound_recorded":"missing_recorded_wave")&&r.definition==-1&&r.wave==1,
            "recorded detail retains source wave and never selects conflicting voice");
    }
    for(bool bankMute:{false,true}) {
        MaPlayer mute;require(mute.init(f,44100),"mute initializes");mute.events_[0].type=MaPlayer::Ev::Ma7NoteOn;
        mute.events_[0].b=bankMute?100:1;
        if(bankMute){mute.chans_[0].bankMsb=121;mute.chans_[0].bankLsb=6;mute.voiceTable_[0].key.bankMSB=121;mute.voiceTable_[0].key.bankLSB=6;}
        mute.voiceTable_[0].al.present=true;Details detail;u=measure(mute,true,&detail);
        require(u.immutableSilent==1&&u.fm==0&&u.wt==0&&u.noInstance==1&&u.boundDefs==0&&u.bound.al==0,
            "immutable MA7 silence retains a logical note without allocating a voice or counting gap use");
        require(mute.diagnostics().suppressedSilentNotes==1,"runtime counts suppressed immutable silence");
        require(detail.groups.begin()->first.reason=="immutable_silent","detail does not treat silent MA7 allocation as sounding FM");
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
    Details bounded;Route route;bounded.add(route,10);bounded.add(route,20);
    require(bounded.groups.begin()->second.count==2&&bounded.groups.begin()->second.first==10
        &&bounded.groups.begin()->second.last==20,"repeated route retains first/last and count");
    for(size_t i=1;i<=Details::maxGroups;++i){route.definition=int(i);bounded.add(route,30);}
    require(bounded.groups.size()==Details::maxGroups&&bounded.overflow==1,"detail memory limit reports excluded occurrences");
    // Mobile velocity1 also produces no recorded instance, but does not test
    // wave existence. It must not be mislabeled as missing recorded data.
    MaPlayer quietAudio;require(quietAudio.init(fixture(0),44100),"quiet recording initializes");
    quietAudio.chans_[0].bankMsb=125;quietAudio.events_[0].b=1;
    Details quiet;measure(quietAudio,true,&quiet);
    require(quiet.groups.size()==1&&quiet.groups.begin()->first.reason=="recorded_no_instance",
        "recorded velocity1 is not evidence of a missing wave");
    for(bool present:{false,true}) {
        MaPlayer atr;require(atr.init(f,44100),"ATR detail initializes");
        atr.events_={{0,MaPlayer::Ev::WaveOn,120,1,3}};
        if(present){atr.audioWaveBanks_[3].resize(2);atr.audioWaveBanks_[3][1].pcm.assign(80,4000);}
        Details detail;measure(atr,true,&detail);const auto& r=detail.groups.begin()->first;
        require(r.reason==(present?"bound_atr":"missing_atr_wave")&&r.source==3&&r.wave==1&&r.note==-1,
            "ATR reports its source/one-based wave, independently of score channel banks");
    }
    for(bool second:{false,true})for(bool reverse:{false,true})for(bool ram:{false,true}) {
        auto file=fixture();WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=8;
        wave.samplingRate=8000;wave.data.assign(80,150);file.tracks[0].waves.push_back(wave);
        if(second) {auto other=file.tracks[0];other.trackNumber=1;other.setupData.clear();
            other.sequenceData={0,0xff,0x2f,0};other.waves[0].data.assign(80,170);file.tracks.push_back(other);
            if(reverse)std::reverse(file.tracks.begin(),file.tracks.end());}
        MaPlayer player;require(player.init(file,8000),"wave provenance initializes");
        auto& voice=player.voiceTable_[0];voice.isPcm=true;voice.pcm.waveId=1;
        voice.pcm.voiceWaveOnly=false;voice.pcm.rom=false;voice.pcm.endPt=40;voice.pcm.env.ar=15;
        if(ram)player.voiceWaveBank_[1].pcm.assign(80,1234);
        Details details;const auto usage=measure(player,true,&details);const auto& route=details.groups.begin()->first;
        require(usage.wt==1&&usage.fallback==0,"wave provenance actually binds a WT voice");
        require(route.source==0,"originating note track is independent of selected legacy waveform");
        require(route.waveBinding==(ram?"instrument_ram":"legacy_mwa"),"provenance reports RAM precedence or legacy compatibility");
        require(route.waveSource==(ram?-1:second&&!reverse?1:0),"provenance identifies actual waveform owner");
        require(route.legacyMwaTracks==(ram?0:second?2:1),"provenance counts bound legacy candidate tracks");
    }
    auto profileDetail=[](const SmafFile& file,bool modern,const std::string& recipe) {
        MaPlayer player;require(player.init(file,8000),"mixed profile detail initializes");
        Details details;const auto usage=measure(player,true,&details);
        require(usage.fm==1&&usage.fallback==1&&details.groups.size()==1,"one mixed profile fallback route");
        const auto& route=details.groups.begin()->first;
        require(route.modern==modern&&route.substitute==recipe,
                "detail uses source note timbre and rhythm, matching synthesis");
    };
    for(bool rhythm:{false,true}) {
        SmafFile legacy;TrackChunk hps;hps.trackNumber=1;hps.formatType=0;
        hps.durationTimeBase=hps.gateTimeBase=0x10;
        hps.channelStatus.assign(2,rhythm?0x33:0);
        hps.sequenceData={0,0x29,20,30,0,0,0};legacy.tracks.push_back(hps);
        const std::string recipe=rhythm?"drum_recipe":"gm_legacy_recipe";
        profileDetail(legacy,false,recipe);
        for(int format:{2,3})for(bool first:{false,true}) {
            auto mixed=legacy;TrackChunk inert;inert.formatType=uint8_t(format);
            inert.durationTimeBase=inert.gateTimeBase=0x10;
            inert.channelStatus.assign(32,rhythm?0:3);
            inert.sequenceData={0,0xff,0x2f,0};
            if(first)mixed.tracks.insert(mixed.tracks.begin(),inert);else mixed.tracks.push_back(inert);
            profileDetail(mixed,false,recipe);
        }
    }
    for(int format:{2,3}) {
        auto modern=fixture();modern.tracks[0].setupData.clear();
        modern.tracks[0].formatType=uint8_t(format);
        modern.tracks[0].sequenceData={0,uint8_t(format==3?0x14:0x94),60,100,20,30,0xff,0x2f,0};
        TrackChunk hps;hps.trackNumber=1;hps.formatType=0;
        hps.durationTimeBase=hps.gateTimeBase=0x10;hps.channelStatus={0x33,0x33};
        hps.sequenceData={0,0,0,0};modern.tracks.push_back(hps);
        profileDetail(modern,true,"gm_modern_recipe");
    }
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
int audit(const fs::path& root,const fs::path& report,bool details) {
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
    if(details)out<<"file\tkind\tinitialized\tcomplete\tafter_bound\tdetail_overflow\tchannel\tsource_track\tbank_msb\tbank_lsb\tprogram\tnote\tdefinition\twave_id\tmodern\treason\tsubstitute\tcount\tfirst_seconds\tlast_seconds\trequested_al\trequested_fixed\trequested_dt\trequested_sram\twave_binding\twave_source_track\tlegacy_mwa_tracks\trequested_pcm_tl\trequested_pcm_ar\trequested_pcm_dr\trequested_pcm_sl\trequested_pcm_sr\trequested_pcm_rr\tpcm_envelope_applied\n";
    else out<<"file\tinitialized\tcomplete\tdefinitions\trequested_defs\tbound_defs\tnotes\tafter_bound\tzero_velocity\timmutable_silent\tfm\twt\trecorded\tatr\tno_instance\tfallback\trom_source_fallback\tram_source_fallback\tmissing_recorded\tep_zero\trequested_al\tbound_al\trequested_fixed\tbound_fixed\trequested_dt\tbound_dt\trequested_sram\tbound_sram\n";
    bool overflow=false;
    for(const auto& path:paths) {
        if(fs::file_size(path)>(16u<<20))return 3;
        std::ifstream in(path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};SmafFile f;
        if(!in||!f.parse(bytes.data(),bytes.size()))return 3;
        Details d;MaPlayer p;const bool initialized=p.init(f,44100);
        const auto u=measure(p,initialized,details?&d:nullptr);
        auto name=utf8(fs::is_regular_file(root)?path.filename():path.lexically_relative(root));for(char& c:name)if(c=='\t'||c=='\r'||c=='\n')c=' ';
        if(details) {
            auto prefix=[&](const char* kind){out<<name<<'\t'<<kind<<'\t'<<u.initialized<<'\t'
                <<u.complete<<'\t'<<u.afterBound<<'\t'<<d.overflow;};
            // Always emit a summary, including valid files with no note routes.
            prefix("summary");out<<"\t-1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\tsummary\t-\t"
                <<u.notes<<"\t0\t0\t0\t0\t0\t0\t-\t-1\t0\t-1\t-1\t-1\t-1\t-1\t-1\t-1\n";
            for(const auto& [r,n]:d.groups) {
                prefix("route");out<<'\t'<<r.channel<<'\t'<<r.source<<'\t'<<r.bankMsb<<'\t'<<r.bankLsb
                    <<'\t'<<r.program<<'\t'<<r.note<<'\t'<<r.definition<<'\t'<<r.wave<<'\t'<<r.modern
                    <<'\t'<<r.reason<<'\t'<<r.substitute<<'\t'<<n.count<<'\t'<<double(n.first)/44100
                    <<'\t'<<double(n.last)/44100<<'\t'<<r.al<<'\t'<<r.fixed<<'\t'<<r.dt<<'\t'<<r.sram
                    <<'\t'<<r.waveBinding<<'\t'<<r.waveSource<<'\t'<<r.legacyMwaTracks
                    <<'\t'<<r.pcmTl<<'\t'<<r.pcmAr<<'\t'<<r.pcmDr<<'\t'<<r.pcmSl
                    <<'\t'<<r.pcmSr<<'\t'<<r.pcmRr<<'\t'<<r.pcmEnvelopeApplied<<'\n';
            }
            overflow|=d.overflow!=0;
        } else {
        out<<name<<'\t'<<u.initialized<<'\t'<<u.complete;
        for(auto x:{u.definitions,u.requestedDefs,u.boundDefs,u.notes,u.afterBound,u.zeroVelocity,u.immutableSilent,u.fm,u.wt,u.audio,u.atr,u.noInstance,u.fallback,u.romSource,u.ramSource,u.missingAudio,u.epZero,u.requested.al,u.bound.al,u.requested.fixed,u.bound.fixed,u.requested.dt,u.bound.dt,u.requested.sram,u.bound.sram})out<<'\t'<<x;
        out<<'\n';
        }
        if(!u.initialized||!u.complete)return 4;
    }
    out.flush();std::cout<<"Scheduled tone inventory files="<<paths.size()<<"; routing only, no waveform/lifetime/listening claim.\n";
    if(!out)return 5;
    if(overflow){std::cerr<<"Detail group limit reached; report is incomplete\n";return 4;}
    return 0;
}
} // namespace toneUsage
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    if(argc==2&&std::wstring(argv[1])==L"--self-test"){toneUsage::selfTest();return 0;}
#else
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--self-test"){toneUsage::selfTest();return 0;}
#endif
    const bool details=argc==4&&fs::path(argv[1])==fs::path("--details");
    if(argc!=3&&!details){std::cerr<<"usage: tone-usage-audit [--details] <MMF-or-folder> <local-report.tsv> | --self-test\n";return 2;}
    try{return toneUsage::audit(fs::path(argv[details?2:1]),fs::path(argv[details?3:2]),details);}
    catch(...){std::cerr<<"tone usage inventory failed\n";return 2;}
}
