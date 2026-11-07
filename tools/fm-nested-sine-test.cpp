// SPDX-License-Identifier: Apache-2.0
// Host-only nested graph6 source against an independent double-Bessel model.
#include "fm-nested-sine-probe.h"
#define FM_ALGORITHM7_TEST_NO_MAIN
#include "fm-algorithm7-test.cpp"
#include "ma_sine_density.h"
#include "ma_sine_output.h"
#include <iomanip>
#include <map>

namespace nested {
using namespace player_sideband;
struct Case { int note,inner,middle,carrier; };
constexpr std::array<Case,4> cases{{{72,3,2,5},{84,3,2,5},{96,3,2,5},{96,7,3,5}}};
double bessel(int n,double x) {
    const int k=std::abs(n);
    return std::cyl_bessel_j(double(k),std::abs(x))*
        ((n<0&&(k&1))?-1:1)*((x<0&&(k&1))?-1:1);
}
SmafFile fixture(const Case& c) {
    auto f=player_sideband::fixture({c.note,c.carrier,c.inner,24});
    std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,6};
    const int mult[]={1,c.inner,c.middle,c.carrier},tl[]={18,24,24,0};
    for(int i=0;i<4;++i)v.insert(v.end(),{0,0xa0,0xf0,uint8_t(tl[i]<<2),0,uint8_t(mult[i]<<4),0});
    auto& t=f.tracks[0];t.setupData={0xf0,uint8_t(v.size()+1)};
    t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
    return f;
}
FmVoicePatch patch(const Case& c) {
    FmVoicePatch p;p.fourOp=true;p.algorithm=6;p.modernTimbre=true;
    const int mult[]={1,c.inner,c.middle,c.carrier},tl[]={18,24,24,0};
    for(int j=0;j<4;++j) {
        auto& o=p.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=10;
        o.multi=uint8_t(mult[j]);o.tl=uint8_t(tl[j]);o.ma35Multipliers=true;
    }
    return p;
}
struct Model {
    Case c;
    std::map<int,double> harmonics;
    explicit Model(Case value):c(value) {
        // sin(Ct+b2*sin(Mt+b1*sin(It))) expands as
        // sum J_n(b2) J_k(n*b1) sin((C+n*M+k*I)t).
        const double beta=tau*4*std::pow(10.,-.75*24/20);
        constexpr int N=32,K=256;
        // |J_n|<=I_n and sum I_n(beta)=exp(beta) also bound the inner error.
        const double tail=sine_density::tailBound(beta,N+1)+
            std::exp(beta)*sine_density::tailBound(N*beta,K+1);
        require(tail<1e-15,"nested Bessel truncation too large");
        for(int n=-N;n<=N;++n)for(int k=-K;k<=K;++k) {
            const int h=c.carrier+n*c.middle+k*c.inner;
            const double v=bessel(n,beta)*bessel(k,n*beta);
            if(h)harmonics[std::abs(h)]+=h<0?-v:v;
        }
        const double direct=std::pow(10.,-.75*18/20);harmonics[1]+=direct;
        double discarded=0;
        for(auto it=harmonics.begin();it!=harmonics.end();) {
            if(std::abs(it->second)<1e-15){discarded+=std::abs(it->second);it=harmonics.erase(it);}
            else ++it;
        }
        require(discarded<1e-12,"nested coefficient pruning error too large");
        for(int j=0;j<128;++j) {
            const double t=j*.372143;double sum=0;
            for(auto [h,v]:harmonics)sum+=v*std::sin(h*t);
            const double expected=direct*std::sin(t)+
                std::sin(c.carrier*t+beta*std::sin(c.middle*t+beta*std::sin(c.inner*t)));
            require(std::abs(sum-expected)<2e-12,"double-Bessel signs/frequencies disagree with nested sine");
        }
    }
    std::vector<double> reference(int factor,bool ideal=false) const {
        std::vector<double> out(frames);
        const double base=440*std::pow(2.,double(c.note-69)/12)/rate;
        const double a=float(1-std::exp(-2*3.14159265358979*10000/rate));
        const auto config=factor>1?fm_causal::alignedConfig(factor):fm_causal::Config{1,{}};
        for(auto [k,v]:harmonics) {
            const double f=base*k;if(ideal&&f>=.5)continue;
            const double omega=tau*f;const auto lp=a/(Complex(1)-std::polar(1-a,-omega));
            Complex h=lp*lp;
            if(!ideal&&factor>1)h*=fm_causal::response(config,omega/factor)*
                std::polar(1.,omega*fm_causal::Decimator(config).delayFrames());
            auto ph=std::polar(1.,omega*(warmup+1));const auto step=std::polar(1.,omega);
            for(auto& x:out){x+=.5*.7*double(.32f)*2*v*std::imag(h*ph);ph*=step;}
        }
        return out;
    }
};

template<bool Fast> std::vector<double> render(const Case& c,int factor) {
    FmVoice v;v.setSampleRate(rate);v.noteOn(patch(c),440*std::pow(2.,double(c.note-69)/12),1);
    sine_output::Bus bus(factor);
    std::array<float,32> source{};std::array<sine_output::Stereo,32> high{};
    const float a=float(1-std::exp(-2*3.14159265358979*10000/rate));float lp1=0,lp2=0;
    std::vector<double> out;
    for(int n=0;n<warmup+72+frames;++n) {
        fm_nested::frame<Fast>(v,std::span<float>(source.data(),size_t(factor)));
        for(int k=0;k<factor;++k)high[size_t(k)]={source[size_t(k)]*.32f*2,0};
        const auto x=bus.frame(std::span<const sine_output::Stereo>(high.data(),size_t(factor)),{});
        lp1+=a*(float(x.left)-lp1);lp2+=a*(lp1-lp2);
        require(std::isfinite(lp2)&&std::abs(lp2)<.92f,"prototype linear reference outside limiter domain");
        if(n>=warmup+72)out.push_back(lp2);
    }
    return out;
}

void spectra() {
    size_t policies=0;double maxError=0,maxNativeError=0;
    for(const auto& c:cases) {
        const Model model(c);MaPlayer p,app;
        require(p.init(fixture(c),rate)&&app.init(fixture(c),rate,false,{8,FmSubframeMath::Hermite,true}),"nested fixture init");
        std::vector<float> native(size_t(warmup+frames)*2),aligned(native.size()+144);
        require(p.render(native.data(),warmup+frames)==warmup+frames&&
                app.render(aligned.data(),warmup+frames+72)==warmup+frames+72,"nested native windows");
        require(std::equal(native.begin(),native.end(),aligned.begin()+144),"host prototype leaked into app routing");
        require(p.diagnostics().fmNotes==1&&p.diagnostics().fallbackNotes==0&&p.diagnostics().limitedFrames==0,"nested native fixture routing/limiter");
        std::vector<double> actual(frames);for(int n=0;n<frames;++n)actual[size_t(n)]=native[size_t(warmup+n)*2];
        maxNativeError=std::max(maxNativeError,error(actual,model.reference(0)));
        require(maxNativeError<4e-7,"nested Bessel model disagrees with actual public player");
        for(int factor:{2,4,8,16,32}) {
            const auto expected=model.reference(factor),exact=render<false>(c,factor),fast=render<true>(c,factor);
            for(const auto* v:{&exact,&fast}) {
                const double e=error(*v,expected);maxError=std::max(maxError,e);
                require(e<4e-7,"nested source/FIR differs from independent double-Bessel model");++policies;
            }
            require(error(exact,fast)<1e-7,"nested Hermite error too large");
            if(c.note==96&&c.inner==7&&factor>=8)
                std::cout<<"nested high-PM factor"<<factor<<" ideal RMS "<<error(fast,model.reference(0,true))<<'\n';
        }
    }
    std::cout<<policies<<" nested source spectral policies PASS; native max RMS "<<maxNativeError<<"; filtered max RMS "<<maxError<<'\n';
}

void clocks() {
    size_t count=0;
    for(int sampleRate:{8000,44100})for(int profile:{0,1,2})for(int factor:{2,4,8,16,32})
    for(bool fast:{false,true})for(int pattern=0;pattern<4;++pattern) {
        auto p=patch(cases[0]);p.modernTimbre=profile!=0;p.lfo=2;
        for(auto& o:p.ops){o.am=o.vib=true;o.dam=o.dvb=2;o.holdEnabled=true;o.ma35Multipliers=profile==2;if(pattern==1){o.ar=8;o.sr=3;}}
        if(pattern==2){p.ops[1].ar=0;p.ops[1].rateLowBits=8;}
        FmVoice v,original;for(auto* x:{&v,&original}){x->setSampleRate(sampleRate);x->noteOn(p,317,.63f);}
        require(!v.supportsSineSubframes(),"general nested graph unexpectedly enabled in production source");
        std::array<float,32> out{};
        for(int n=0;n<256;++n) {
            auto control=[&](FmVoice& x){if(n==0&&pattern==3)x.setVolume(1e-12f);if(n==32)x.setPitch(sampleRate*.71);if(n==64)x.setPitch(sampleRate*.013);if(n==96)x.setVolume(0);if(n==128)x.setVolume(.71f);if(n==160)x.noteOff(true);if(n==192)x.setHold(false);if(n==224)x.noteOn(p,419,.51f);};
            control(v);control(original);
            const auto r=fast?fm_nested::frame<true>(v,std::span<float>(out.data(),size_t(factor))):
                              fm_nested::frame<false>(v,std::span<float>(out.data(),size_t(factor)));
            const float endpoint=original.tick();
            require(std::bit_cast<uint32_t>(out[size_t(factor-1)])==std::bit_cast<uint32_t>(endpoint),"nested native endpoint changed");
            require(std::bit_cast<uint32_t>(r.reconstructedEndpoint)==std::bit_cast<uint32_t>(endpoint),"nested endpoint equation changed");
            require(v.active_==original.active_&&v.released_==original.released_&&v.recentLevel_==original.recentLevel_&&v.lfoPhase_==original.lfoPhase_&&v.lfoSin_==original.lfoSin_,"nested voice clock/feedback history changed");
            for(int j=0;j<4;++j){const auto& a=v.ops_[j];const auto& b=original.ops_[j];require(a.phase_==b.phase_&&a.env_.level_==b.env_.level_&&a.env_.phase_==b.env_.phase_&&a.env_.pendingKeyOff_==b.env_.pendingKeyOff_&&v.fbMem_[j][0]==original.fbMem_[j][0]&&v.fbMem_[j][1]==original.fbMem_[j][1],"nested operator clock/feedback changed");}
            for(int k=0;k<factor;++k)require(std::isfinite(out[size_t(k)]),"nested nonfinite sample");
        }++count;
    }
    for(int kind=0;kind<15;++kind) {
        auto p=patch(cases[0]);int factor=8;
        if(kind==0)p.fourOp=false;if(kind==1)p.algorithm=5;if(kind==2)p.feedback=3;
        if(kind>=3&&kind<7)p.ops[size_t(kind-3)].wave=6;
        if(kind>=7&&kind<11)p.ops[size_t(kind-7)].fb=3;
        if(kind==11)factor=1;if(kind==12)factor=3;if(kind==13)factor=64;if(kind==14)factor=0;
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,317,1);auto original=v;
        std::array<float,64> out;out.fill(123);bool rejected=false;
        try{(void)fm_nested::frame<true>(v,std::span<float>(out.data(),size_t(factor)));}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&std::all_of(out.begin(),out.end(),[](float x){return x==123;}),"nested rejection not atomic");
        require(v.tick()==original.tick(),"nested rejection advanced clock");
    }
    for(int factor:{8,16})for(bool fast:{false,true}) {
        auto p=patch(cases[0]);for(auto& o:p.ops)o.rr=15;
        FmVoice v,original;for(auto* x:{&v,&original}){x->setSampleRate(rate);x->noteOn(p,317,1);}
        std::array<float,32> out{};
        for(int n=0;n<512;++n) {
            if(n==32){v.noteOff(false);original.noteOff(false);}
            if(fast)fm_nested::frame<true>(v,std::span<float>(out.data(),size_t(factor)));
            else fm_nested::frame<false>(v,std::span<float>(out.data(),size_t(factor)));
            const float endpoint=original.tick();
            require(std::bit_cast<uint32_t>(out[size_t(factor-1)])==std::bit_cast<uint32_t>(endpoint)&&v.active_==original.active_,"nested retirement changed native lifetime");
        }
        require(!v.active_&&std::all_of(out.begin(),out.begin()+factor,[](float x){return x==0;}),"nested inactive source must emit zero");
        v.noteOn(p,419,1);require(fm_nested::eligible(v),"nested retrigger lost shape");
    }
    std::cout<<count<<" nested clock/Hold/retrigger cases,4 retirement cases and15 rejection guards PASS\n";
}
}
int main(){try{nested::clocks();nested::spectra();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
