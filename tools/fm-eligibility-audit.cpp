// SPDX-License-Identifier: Apache-2.0
// Host-only live FM inventory. It does not select a production render mode.
#include "fm-parallel-pair-probe.h"
#include "smaf_file.h"
#include "smaf_voice.h"
#include "score_audit.h"
#include <map>
#define private public
#include "ma_player.h"
#undef private
#include "fm-oversampling-model.h"
#include <bit>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
namespace eligibility {
using namespace fxchain::smaf;
constexpr uint32_t rate=44100;
constexpr std::array<int,7> factors{1,2,4,8,16,32,0};
constexpr std::array<const char*,5> reasons{"eligible","four_operator","additive","non_sine","feedback"};
struct Cache {double index=-1,carrier=-1,modulator=-1;fm_model::Tail tail{};int factor=0;};
using CacheBank=std::array<std::array<Cache,2>,MaPlayer::kPoolSize>;
struct Snapshot {uint64_t active=0,eligible=0,channels=0,muted=0,lfo=0,released=0,pcm=0,atr=0,recorded=0,sineGraphs=0,pairs=0,pairMuted=0,pairLfo=0;
    std::array<uint64_t,8> graph{};std::array<uint64_t,5> reason{};std::array<uint64_t,7> factor{},pairFactor{};double maxIndex=0,maxCarrier=0,maxModulator=0;};
int reason(const FmVoice& v){if(v.fourOp_)return 1;if(v.algo_!=0)return 2;
    if(v.patch_.ops[0].wave!=0||v.patch_.ops[1].wave!=0)return 3;
    if(v.opFb_[0]!=0||v.opFb_[1]!=0)return 4;return 0;}
const Cache& project(const FmVoice& v,int mod,Cache& c){
    const bool silentCarrier=v.ops_[mod+1].silentForCurrentNote();
    const double x=silentCarrier||v.ops_[mod].silentForCurrentNote()?0:6.283185307179586*std::abs(v.modDepth_*v.ops_[mod].tlGain_*v.ops_[mod].kslGain_);
    const auto frequency=[&](int op){const auto&o=v.ops_[op];return std::abs(o.phaseInc_)*(1+std::abs(o.vibAmt_*v.vibratoScale_));};
    const double carrier=silentCarrier?0:frequency(mod+1),modulator=silentCarrier?0:frequency(mod);
    if(c.index!=x){c.index=x;c.tail=fm_model::sidebandTail(x);c.carrier=c.modulator=-1;}
    if(c.carrier!=carrier||c.modulator!=modulator){c.carrier=carrier;c.modulator=modulator;c.factor=fm_model::factor(carrier,modulator,c.tail.order);}
    return c;
}
Snapshot snapshot(const MaPlayer& p,CacheBank& cache){
    Snapshot s;std::array<bool,128> channels{};
    for(size_t i=0;i<p.pool_.size();++i){const auto& v=p.pool_[i];if(!v.active())continue;++s.active;
        const int used=v.algo_<2?2:4;bool graph=(v.fourOp_==(used==4));
        for(int op=0;op<used;++op)graph=graph&&v.patch_.ops[size_t(op)].wave==0&&v.opFb_[op]==0;
        if(graph){++s.sineGraphs;++s.graph[v.algo_];}
        if(fm_pairs::eligible(v)){++s.pairs;s.pairMuted+=v.muted();
            bool lfo=false;for(int op=0;op<4;++op)lfo=lfo||v.ops_[op].usesLfo();s.pairLfo+=lfo;
            const auto&a=project(v,0,cache[i][0]);const auto&b=project(v,2,cache[i][1]);
            const int f=a.factor==0||b.factor==0?0:std::max(a.factor,b.factor);
            for(size_t k=0;k<factors.size();++k)if(f==factors[k])++s.pairFactor[k];
        }
        const int kind=reason(v);++s.reason[size_t(kind)];if(kind!=0)continue;
        if(v.channel<0||v.channel>=128)throw std::runtime_error("active FM channel out of bounds");
        ++s.eligible;channels[size_t(v.channel)]=true;s.muted+=v.muted();s.released+=v.released();
        s.lfo+=v.ops_[0].usesLfo()||v.ops_[1].usesLfo();
        const auto& c=project(v,0,cache[i][0]);
        for(size_t k=0;k<factors.size();++k)if(c.factor==factors[k])++s.factor[k];
        s.maxIndex=std::max(s.maxIndex,c.index);s.maxCarrier=std::max(s.maxCarrier,c.carrier);s.maxModulator=std::max(s.maxModulator,c.modulator);
    }
    for(bool b:channels)s.channels+=b;
    for(const auto&v:p.pcmPool_)if(v.active){if(v.keyNote<0)++s.atr;else++s.pcm;}
    for(const auto&v:p.audioPool_)s.recorded+=v.active;return s;
}
struct Report {uint64_t frames=0,peakFm=0,peakEligible=0,peakChannels=0,peakNative=0,peakSineGraphs=0,peakPairs=0;
    Snapshot total{};bool finished=false,capReached=false,scanComplete=false,tailLimit=false;uint64_t fmNotes=0,stolenFm=0;};
class Reference {
    MaPlayer& player_;
    uint64_t cap_,compared_=0;
    std::array<float,1024> buffer_{};
    int count_=0,index_=0;
public:
    Reference(MaPlayer& p,uint64_t cap):player_(p),cap_(cap){}
    void sample(const std::array<float,2>& output){
        if(index_==count_){count_=player_.render(buffer_.data(),int(std::min<uint64_t>(512,cap_-compared_)));index_=0;}
        if(count_<=0)throw std::runtime_error("observation extended the normal render");
        for(int channel=0;channel<2;++channel)if(!std::isfinite(output[size_t(channel)])||
            std::bit_cast<uint32_t>(output[size_t(channel)])!=std::bit_cast<uint32_t>(buffer_[size_t(2*index_+channel)]))
                throw std::runtime_error("observation changed a rendered sample");
        ++index_;++compared_;
    }
    void finish(bool observedFinished){
        if(index_!=count_)throw std::runtime_error("observation shortened the normal render");
        if(compared_<cap_&&!player_.finished()&&player_.render(buffer_.data(),int(std::min<uint64_t>(512,cap_-compared_)))!=0)
            throw std::runtime_error("observation shortened the normal render");
        if(observedFinished!=player_.finished())throw std::runtime_error("observation changed end state");
    }
};
Report observe(MaPlayer& p,uint64_t cap,Reference& reference){
    Report r;CacheBank cache{};
    while(r.frames<cap&&!p.finished()&&p.cursor_<p.endSample_){
        // Match the public render's due-event loop, then inspect BEFORE tick:
        // single-frame voices and controller changes must not be missed.
        while(p.nextEvent_<p.events_.size()&&p.events_[p.nextEvent_].sample<=p.cursor_){p.fireEvent_(p.events_[p.nextEvent_]);++p.nextEvent_;}
        const auto s=snapshot(p,cache);std::array<float,2> output{};
        if(p.render(output.data(),1)!=1)break;
        reference.sample(output);++r.frames;
        r.peakFm=std::max(r.peakFm,s.active);r.peakEligible=std::max(r.peakEligible,s.eligible);r.peakChannels=std::max(r.peakChannels,s.channels);
        r.peakPairs=std::max(r.peakPairs,s.pairs);r.peakSineGraphs=std::max(r.peakSineGraphs,s.sineGraphs);r.peakNative=std::max(r.peakNative,s.pcm+s.atr+s.recorded);
        auto&t=r.total;t.active+=s.active;t.eligible+=s.eligible;t.channels+=s.channels;t.muted+=s.muted;t.lfo+=s.lfo;t.released+=s.released;t.pcm+=s.pcm;t.atr+=s.atr;t.recorded+=s.recorded;t.sineGraphs+=s.sineGraphs;t.pairs+=s.pairs;t.pairMuted+=s.pairMuted;t.pairLfo+=s.pairLfo;
        for(size_t i=0;i<t.graph.size();++i)t.graph[i]+=s.graph[i];
        for(size_t i=0;i<t.reason.size();++i)t.reason[i]+=s.reason[i];for(size_t i=0;i<t.factor.size();++i){t.factor[i]+=s.factor[i];t.pairFactor[i]+=s.pairFactor[i];}
        t.maxIndex=std::max(t.maxIndex,s.maxIndex);t.maxCarrier=std::max(t.maxCarrier,s.maxCarrier);t.maxModulator=std::max(t.maxModulator,s.maxModulator);
    }
    // If the bound was reached exactly, do not execute a boundary event.
    r.finished=p.finished();r.capReached=r.frames==cap&&!r.finished;r.scanComplete=p.scoreAudit().scanComplete;
    r.tailLimit=p.diagnostics().tailLimitReached;r.fmNotes=p.diagnostics().fmNotes;r.stolenFm=p.diagnostics().stolenFm;return r;
}
Report measure(MaPlayer& observed,MaPlayer& baseline,uint64_t cap){
    if(cap==0||cap>uint64_t(rate)*600)throw std::runtime_error("invalid observation cap");
    Reference reference(baseline,cap);auto r=observe(observed,cap,reference);reference.finish(r.finished);
    if(observed.diagnostics().fmNotes!=baseline.diagnostics().fmNotes||observed.diagnostics().stolenFm!=baseline.diagnostics().stolenFm)
        throw std::runtime_error("observation changed note allocation");return r;
}
void write(const Report&r,std::ostream& out){const auto&t=r.total;out<<std::setprecision(12)<<"{\"schema_version\":1,\"sample_rate\":44100,\"scope\":\"Live voice inventory; coefficient-tail model for steady two-sine chains and parallel pairs only, not dynamic EG/LFO/filter error, listening or handset calibration\",\"bessel_l1_tail_target\":"<<fm_model::tailTarget
    <<",\"render_matches_reference\":true,\"frames\":"<<r.frames<<",\"finished\":"<<(r.finished?"true":"false")<<",\"cap_reached\":"<<(r.capReached?"true":"false")<<",\"scan_complete\":"<<(r.scanComplete?"true":"false")<<",\"tail_limit\":"<<(r.tailLimit?"true":"false")
    <<",\"peak_active_fm\":"<<r.peakFm<<",\"peak_eligible_fm\":"<<r.peakEligible<<",\"peak_eligible_channels\":"<<r.peakChannels<<",\"peak_native_sources\":"<<r.peakNative
    <<",\"peak_sine_graph_fm\":"<<r.peakSineGraphs<<",\"sine_graph_no_feedback_voice_frames\":"<<t.sineGraphs
    <<",\"peak_parallel_pair_fm\":"<<r.peakPairs<<",\"parallel_pair_candidate_voice_frames\":"<<t.pairs<<",\"parallel_pair_currently_muted_voice_frames\":"<<t.pairMuted<<",\"parallel_pair_lfo_voice_frames\":"<<t.pairLfo
    <<",\"fm_notes\":"<<r.fmNotes<<",\"stolen_fm\":"<<r.stolenFm<<",\"active_fm_voice_frames\":"<<t.active<<",\"eligible_voice_frames\":"<<t.eligible<<",\"eligible_channel_frames\":"<<t.channels
    <<",\"eligible_currently_muted_voice_frames\":"<<t.muted<<",\"eligible_lfo_voice_frames\":"<<t.lfo<<",\"eligible_released_voice_frames\":"<<t.released<<",\"keyed_pcm_voice_frames\":"<<t.pcm<<",\"independent_atr_voice_frames\":"<<t.atr<<",\"recorded_voice_frames\":"<<t.recorded
    <<",\"maximum_pm_index_upper\":"<<t.maxIndex<<",\"maximum_carrier_upper_cycles\":"<<t.maxCarrier<<",\"maximum_modulator_upper_cycles\":"<<t.maxModulator<<",\"reason_voice_frames\":{";
    for(size_t i=0;i<reasons.size();++i){if(i)out<<',';out<<'"'<<reasons[i]<<"\":"<<t.reason[i];}out<<"},\"sine_graph_algorithm_voice_frames\":{";
    for(size_t i=0;i<t.graph.size();++i){if(i)out<<',';out<<'"'<<i<<"\":"<<t.graph[i];}out<<"},\"steady_model_factor_voice_frames\":{";
    for(size_t i=0;i<factors.size();++i){if(i)out<<',';out<<'"';if(factors[i])out<<factors[i];else out<<"beyond32";out<<"\":"<<t.factor[i];}out<<"},\"parallel_pair_model_factor_voice_frames\":{";
    for(size_t i=0;i<factors.size();++i){if(i)out<<',';out<<'"';if(factors[i])out<<factors[i];else out<<"beyond32";out<<"\":"<<t.pairFactor[i];}out<<"}}\n";out.flush();if(!out)throw std::runtime_error("cannot write report");
}
std::vector<uint8_t> read(const char* path){std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("cannot open input");const auto n=in.tellg();
    if(n<12||n>16*1024*1024)throw std::runtime_error("input must be between12 bytes and16MiB");std::vector<uint8_t>b(static_cast<size_t>(n));in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(b.data()),n)||in.peek()!=std::char_traits<char>::eof()||in.bad())throw std::runtime_error("input changed during read");return b;}
} // namespace eligibility
#ifndef FM_ELIGIBILITY_AUDIT_NO_MAIN
int main(int argc,char**argv){using namespace eligibility;
    if(argc==2&&std::string_view(argv[1])=="--help"){std::cout<<"Usage: fm-eligibility-audit <input.mmf> [max-seconds]\nLive FM inventory, verified against normal rendering; JSON contains no filenames or embedded metadata.\n";return 0;}
    if(argc<2||argc>3||argv[1][0]=='-'){std::cerr<<"Usage: fm-eligibility-audit <input.mmf> [max-seconds]\n";return 2;}
    uint64_t cap=uint64_t(rate)*600;if(argc==3){char* end=nullptr;errno=0;const double seconds=std::strtod(argv[2],&end);
        if(errno||end==argv[2]||*end||!std::isfinite(seconds)||seconds<=0||seconds>600||seconds*rate<.5){std::cerr<<"Invalid max-seconds\n";return 2;}cap=uint64_t(std::llround(seconds*rate));}
    try{const auto bytes=read(argv[1]);SmafFile file;if(!file.parse(bytes.data(),bytes.size()))throw std::runtime_error("invalid SMAF input");MaPlayer observed,baseline;
        if(!observed.init(file,rate)||!baseline.init(file,rate))throw std::runtime_error("no playable track");write(measure(observed,baseline,cap),std::cout);return 0;}
    catch(const std::exception&e){std::cerr<<"fm-eligibility-audit: "<<e.what()<<'\n';return 1;}}
#endif
