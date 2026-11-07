// SPDX-License-Identifier: Apache-2.0
// Stopped attacks reduce selected four-operator graphs to a single PM pair.
#include <optional>
#include <map>
#include "fm-native-clock-probe.h"
#include "ma_sine_output.h"
#include "smaf_file.h"
#include "smaf_voice.h"
#include "score_audit.h"
#define private public
#include "ma_player.h"
#undef private
#define FM_ALGORITHM7_TEST_NO_MAIN
#include "fm-algorithm7-test.cpp"
#include "ma_sine_density.h"

namespace reduced {
using namespace player_sideband;

int stopped(int graph) { return graph == 3 ? 2 : 1; }
int modulator(int graph) { return graph == 3 ? 0 : 2; }

SmafFile fixture(const Case& c, int graph, bool stoppedAttack = true) {
    auto f = player_sideband::fixture(c);
    std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,uint8_t(graph)};
    for (int j = 0; j < 4; ++j) {
        const int multiple = j == 3 ? c.carrier : j == modulator(graph) ? c.modulator : 1;
        const int tl = j == modulator(graph) ? c.tl : j == 0 && graph == 6 ? 18 : 0;
        const int ar = stoppedAttack && j == stopped(graph) ? 0 : 15;
        v.insert(v.end(), {0,0xa0,uint8_t(ar << 4),uint8_t(tl << 2),0,uint8_t(multiple << 4),0});
    }
    auto& t = f.tracks[0];
    t.setupData = {0xf0,uint8_t(v.size()+1)};
    t.setupData.insert(t.setupData.end(),v.begin(),v.end());
    t.setupData.push_back(0xf7);
    return f;
}

std::vector<double> renderReduced(const Case& c, int graph, int factor,
                                  FmSubframeMath math, bool adaptive = false) {
    MaPlayer p;
    require(p.init(fixture(c,graph),rate,false,{factor,math,adaptive}),"reduced fixture init");
    const int delay = factor ? 72 : 0;
    std::vector<float> all(size_t(warmup+delay+frames)*2);
    require(p.render(all.data(),warmup+delay+frames)==warmup+delay+frames,"reduced window ended early");
    require(p.diagnostics().fmNotes==1&&p.diagnostics().fallbackNotes==0&&p.diagnostics().limitedFrames==0,
            "reduced fixture routing/limiter");
    std::vector<double> out(frames);
    for (int n=0;n<frames;++n) {
        out[size_t(n)] = all[size_t(n+warmup+delay)*2];
        require(all[size_t(n+warmup+delay)*2+1]==0,"reduced hard pan");
    }
    return out;
}

// Frozen single-pair Bessel reference; graph6 adds its independent direct sine.
std::vector<double> reference(const Case& c, int graph, int factor, bool ideal) {
    auto out = Model(c).reference(factor,ideal);
    if (graph != 6) return out;
    for (auto& x : out) x *= .5;
    const double f = 440*std::pow(2.,double(c.note-69)/12)/rate;
    const double omega = tau*f;
    const double a = float(1-std::exp(-2*3.14159265358979*10000/rate));
    const auto lp = a/(Complex(1)-std::polar(1-a,-omega));
    Complex h = lp*lp;
    if (!ideal && factor>1) {
        const auto config = fm_causal::alignedConfig(factor);
        h *= fm_causal::response(config,omega/factor)*
             std::polar(1.,omega*fm_causal::Decimator(config).delayFrames());
    }
    const double gain = .5*.7*double(.32f)*2*std::pow(10.,-.75*18/20);
    auto phase = std::polar(1.,omega*(warmup+1));
    for (auto& value : out) { value += gain*std::imag(h*phase); phase *= std::polar(1.,omega); }
    return out;
}

void spectra() {
    size_t cases=0, adaptiveCases=0;
    double maxError=0;
    for (int graph : {3,4,6}) for (int note : {60,72,84,96})
    for (auto [carrier,mod] : std::array<std::pair<int,int>,2>{{{2,3},{5,7}}})
    for (int tl : {0,24,36}) {
        const Case c{note,carrier,mod,tl};
        const auto native = renderReduced(c,graph,0,FmSubframeMath::Exact);
        require(rms(native)>1e-6,"reduced fixture must be audible");
        for (int factor : {0,1,2,4,8,16,32}) {
            std::vector<double> exact;
            for (auto math : {FmSubframeMath::Exact,FmSubframeMath::Hermite}) {
                if (factor<2 && math==FmSubframeMath::Hermite) continue;
                const auto actual = renderReduced(c,graph,factor,math);
                if (math==FmSubframeMath::Exact) exact=actual;
                const double e = error(actual,reference(c,graph,factor,false));
                maxError = std::max(maxError,e);
                require(e<4e-7&&error(actual,exact)<1e-7,"reduced graph differs from independent PM/direct reference");
                if (factor==1) require(actual==native,"reduced native alignment changed");
                ++cases;
            }
        }
        const double base=440*std::pow(2.,double(note-69)/12)/rate;
        const int density=sine_density::needs16(tau*4*std::pow(10.,-.75*tl/20),base*carrier,base*mod)?16:8;
        const auto adaptive=renderReduced(c,graph,8,FmSubframeMath::Hermite,true);
        require(adaptive==renderReduced(c,graph,density,FmSubframeMath::Hermite),"reduced adaptive density selects wrong pair");
        if (note==96&&carrier==5&&tl==0) {
            const auto ideal=reference(c,graph,0,true);
            const double old=error(native,ideal), now=error(adaptive,ideal);
            require(now<old*.001,"reduced high-PM alias did not decrease");
            std::cout<<"graph"<<graph<<" note96 native ideal RMS="<<old<<" adaptive="<<now<<'\n';
        }
        ++adaptiveCases;
    }
    std::cout<<cases<<" reduced spectral policies and "<<adaptiveCases
             <<" adaptive pair selections PASS; max model RMS "<<maxError<<'\n';
}

void transients() {
    size_t cases=0;
    for (int graph : {3,4,6}) for (int factor : {8,16})
    for (int gate : {1,44,441}) for (bool hard : {false,true}) {
        const Case c{96,5,7,0};
        std::array<std::vector<double>,2> output;
        for (int i=0;i<2;++i) {
            auto f=i==0?fixture(c,graph):graph==6?algorithm7::fixture(c):player_sideband::fixture(c);
            if (i==1&&graph==6) {
                // Independent graph7 reference: direct0 + pair(1,2), direct3 off.
                auto& ar=f.tracks[0].setupData.at(2+13+3*7+2);
                require(ar==0xf0,"reference setup attack position changed"); ar=0;
            }
            MaPlayer p;
            require(p.init(f,rate,false,{factor,FmSubframeMath::Hermite}),"transient reference init");
            const auto events=p.events_;p.events_.clear();
            for(const auto& e:events)if(e.sample==0)p.events_.push_back(e);
            if(hard)p.events_.push_back({uint64_t(gate),MaPlayer::Ev::AllSoundOff,0,0,0});
            else {
                const auto off=std::find_if(events.begin(),events.end(),[](const auto& e){return e.type==MaPlayer::Ev::NoteOff;});
                require(off!=events.end(),"transient authored note-off missing");
                auto e=*off;e.sample=gate;p.events_.push_back(e);
            }
            const int length=gate+8192;
            p.events_.push_back({uint64_t(length),MaPlayer::Ev::ScoreNop,0,0,0});
            p.nonAtrGateEventEnd_=p.events_.size();p.endSample_=length+4096;p.scoreEndSample_=length;
            std::vector<float> data(size_t(length+72)*2);
            require(p.render(data.data(),length+72)==length+72,"transient window ended early");
            require(p.diagnostics().limitedFrames==0,"transient linear reference hit limiter");
            for(int n=0;n<length+72;++n)output[size_t(i)].push_back(double(data[size_t(n)*2])*(i==1&&graph==6?1.25:1));
        }
        require(error(output[0],output[1])<1e-7,"reduced onset/gate differs from independent supported graph");
        ++cases;
    }
    std::cout<<cases<<" onset/gate comparisons against independent graphs0/7 PASS\n";
}

FmVoicePatch patch(int graph, int profile) {
    FmVoicePatch p;
    p.fourOp=true; p.algorithm=uint8_t(graph); p.modernTimbre=profile!=0; p.lfo=2;
    for (int j=0;j<4;++j) {
        auto& o=p.ops[size_t(j)];
        o.ar=15; o.dr=o.sr=o.sl=0; o.rr=15; o.multi=uint8_t(j+1);
        o.tl=uint8_t(j*6); o.am=o.vib=true; o.dam=o.dvb=2;
        o.holdEnabled=true; o.ma35Multipliers=profile==2;
    }
    p.ops[size_t(stopped(graph))].ar=0;
    return p;
}

void clocksAndGuards() {
    size_t cases=0, guards=0;
    for (int graph : {3,4,6}) for (int sampleRate : {8000,44100})
    for (int profile : {0,1,2}) for (int factor : {2,4,8,16,32})
    for (auto math : {FmSubframeMath::Exact,FmSubframeMath::Hermite}) {
        const auto p=patch(graph,profile);
        FmVoice v,original;
        for (auto* x : {&v,&original}) { x->setSampleRate(sampleRate); x->noteOn(p,317,.63f); }
        std::array<float,32> output{};
        for (int n=0;n<256;++n) {
            auto control=[&](FmVoice& x) {
                if(n==32)x.setPitch(sampleRate*.71); if(n==64)x.setPitch(sampleRate*.013);
                if(n==96)x.setVolume(0); if(n==128)x.setVolume(.71f);
                if(n==160)x.noteOff(true); if(n==192)x.setHold(false);
                if(n==224)x.noteOn(p,419,.51f);
            };
            control(v); control(original);
            require(v.tickSineSubframes(output.data(),factor,math),"reduced source rejected");
            const float endpoint=original.tick();
            require(std::bit_cast<uint32_t>(output[size_t(factor-1)])==std::bit_cast<uint32_t>(endpoint),"reduced endpoint changed");
            require(v.active_==original.active_&&v.released_==original.released_&&v.recentLevel_==original.recentLevel_&&
                    v.lfoPhase_==original.lfoPhase_&&v.lfoSin_==original.lfoSin_,"reduced voice clock changed");
            for (int j=0;j<4;++j) {
                const auto& a=v.ops_[j]; const auto& b=original.ops_[j];
                require(a.phase_==b.phase_&&a.env_.level_==b.env_.level_&&a.env_.phase_==b.env_.phase_&&
                        a.env_.pendingKeyOff_==b.env_.pendingKeyOff_,"reduced operator clock changed");
            }
            for (int k=0;k<factor;++k) require(std::isfinite(output[size_t(k)]),"reduced nonfinite output");
        }
        ++cases;
    }
    for (int graph : {3,4,6}) for (int kind=0;kind<12;++kind) {
        auto p=patch(graph,2);
        if(kind==0)p.fourOp=false;
        if(kind==1)p.ops[size_t(stopped(graph))].ar=1;
        if(kind==2)p.ops[size_t(stopped(graph))].rateLowBits=8;
        if(kind==3)p.feedback=3;
        if(kind>=4&&kind<8)p.ops[size_t(kind-4)].wave=6;
        if(kind>=8)p.ops[size_t(kind-8)].fb=3;
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,317,1);auto original=v;
        std::array<float,8> out;out.fill(123);
        require(!v.supportsSineSubframes()&&!v.tickSineSubframes(out.data(),8),"unsafe reduced shape admitted");
        for(float x:out)require(x==123,"reduced rejection touched output");
        require(v.tick()==original.tick(),"reduced rejection advanced clock");
        ++guards;
    }
    for (int graph : {3,4,6}) {
        auto p=patch(graph,2);p.ops[size_t(stopped(graph))].ar=15;
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,317,1);v.noteOff(false);
        for(int n=0;n<8192;++n)v.tick();
        require(!v.supportsSineSubframes(),"finished envelope enabled a mid-note route change");
        v.noteOn(patch(graph,2),317,1);
        require(v.supportsSineSubframes(),"retrigger did not refresh reduced eligibility");
        v.noteOn(p,317,1);
        require(!v.supportsSineSubframes(),"retrigger retained reduced eligibility");
    }
    std::cout<<cases<<" reduced clock/Hold/retrigger cases and "<<guards<<" unsafe-shape guards PASS\n";
}
}

int main() {
    try { reduced::clocksAndGuards(); reduced::spectra(); reduced::transients(); return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
