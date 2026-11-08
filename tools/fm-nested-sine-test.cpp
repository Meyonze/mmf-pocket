// SPDX-License-Identifier: Apache-2.0
// Nested graph6 source and player against an independent double-Bessel model.
#include "fm-nested-sine-probe.h"
#define FM_ALGORITHM7_TEST_NO_MAIN
#include "fm-algorithm7-test.cpp"
#include "ma_sine_density.h"
#include "ma_sine_output.h"
#include "fm-bessel-reference.h"
#include "fm-high-density-probe.h"
#include <iomanip>
#include <map>

namespace nested {
using namespace player_sideband;
struct Case { int note,inner,middle,carrier; int innerTl=24,middleTl=24; bool ma7=false; };
constexpr std::array<Case,4> cases{{{72,3,2,5},{84,3,2,5},{96,3,2,5},{96,7,3,5}}};
double ratio(int code,bool ma7) {
    // Frozen multiplier tables, independent of configured operator state.
    constexpr double vm35[]={.5,1,2,3,4,5,6,7,8,9,10,10,12,12,15,15};
    constexpr double expanded[]={.5,1,2,3,4,5,6,7,8,9,10,1.414,12,1.498,.891,15};
    return ma7?expanded[code]:vm35[code];
}
SmafFile fixture(const Case& c) {
    auto f=player_sideband::fixture({c.note,c.carrier,c.inner,24});
    std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,6};
    const int mult[]={1,c.inner,c.middle,c.carrier},tl[]={18,c.innerTl,c.middleTl,0};
    for(int i=0;i<4;++i)v.insert(v.end(),{0,0xa0,0xf0,uint8_t(tl[i]<<2),0,uint8_t(mult[i]<<4),0});
    if(c.ma7) {
        v={0x43,0x79,8,0x7f,0x21,0,0,0,0,0,0,0,0x79,6};
        for(int i=0;i<4;++i)v.insert(v.end(),{0,0xa0,0xf0,uint8_t(tl[i]<<2),0,0,0,0,0,uint8_t(mult[i]<<4)});
        auto& t=f.tracks[0];t.formatType=3;t.channelStatus.assign(32,0x81);t.sequenceData.clear();
        for(auto [cc,value]:std::array<std::pair<int,int>,6>{{{0,0},{32,0},{7,127},{11,127},{10,0},{64,0}}})
            t.sequenceData.insert(t.sequenceData.end(),{0,0x30,uint8_t(cc),uint8_t(value)});
        t.sequenceData.insert(t.sequenceData.end(),{0,0x40,0,0,0x10,uint8_t(c.note),127,0x82,0x2c,0x83,0x10,0xff,0x2f,0});
    }
    auto& t=f.tracks[0];t.setupData={0xf0,uint8_t(v.size()+1)};
    t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
    return f;
}
FmVoicePatch patch(const Case& c) {
    FmVoicePatch p;p.fourOp=true;p.algorithm=6;p.modernTimbre=true;
    const int mult[]={1,c.inner,c.middle,c.carrier},tl[]={18,c.innerTl,c.middleTl,0};
    for(int j=0;j<4;++j) {
        auto& o=p.ops[size_t(j)];o.ar=15;o.dr=o.sr=o.sl=0;o.rr=10;
        o.multi=uint8_t(mult[j]);o.tl=uint8_t(tl[j]);o.ma35Multipliers=true;o.ma7Multipliers=c.ma7;
    }
    return p;
}
struct Model {
    Case c;
    std::map<int,double> harmonics;
    explicit Model(Case value):c(value) {
        // sin(Ct+b2*sin(Mt+b1*sin(It))) expands as
        // sum J_n(b2) J_k(n*b1) sin((C+n*M+k*I)t).
        const double depth=c.ma7?.42:4;
        const double beta=tau*depth*std::pow(10.,-.75*std::min(c.innerTl,c.middleTl)/20);
        const double base=440*std::pow(2.,double(c.note-69)/12)/rate;
        const double inner=ratio(c.inner,c.ma7),middle=ratio(c.middle,c.ma7),carrier=ratio(c.carrier,c.ma7);
        const double b1=base*inner>=.5?0:tau*depth*std::pow(10.,-.75*c.innerTl/20);
        const double b2=base*middle>=.5?0:tau*depth*std::pow(10.,-.75*c.middleTl/20);
        int N=32,K=256;
        while(sine_density::tailBound(beta,N+1)>1e-18&&N<128)N+=16;
        while(std::exp(beta)*sine_density::tailBound(N*beta,K+1)>1e-18&&K<16384)K*=2;
        // |J_n|<=I_n and sum I_n(beta)=exp(beta) also bound the inner error.
        const double tail=sine_density::tailBound(beta,N+1)+
            std::exp(beta)*sine_density::tailBound(N*beta,K+1);
        require(tail<1e-15,"nested Bessel truncation too large");
        const auto outer=fm_bessel::coefficients(b2,N);
        if(base*carrier<.5)for(int n=-N;n<=N;++n) {
          const auto innerTerms=fm_bessel::coefficients(n*b1,K);
          const double outerTerm=fm_bessel::signedOrder(outer,n);
          for(int k=-K;k<=K;++k) {
            // All encoded ratios have at most three decimal places.
            const int h=int(std::lround(carrier*1000))+n*int(std::lround(middle*1000))+k*int(std::lround(inner*1000));
            const double v=outerTerm*fm_bessel::signedOrder(innerTerms,k);
            if(h)harmonics[std::abs(h)]+=h<0?-v:v;
          }
        }
        const double direct=base<.5?std::pow(10.,-.75*18/20):0;harmonics[1000]+=direct;
        double discarded=0;
        for(auto it=harmonics.begin();it!=harmonics.end();) {
            if(std::abs(it->second)<1e-15){discarded+=std::abs(it->second);it=harmonics.erase(it);}
            else ++it;
        }
        require(discarded<1e-12,"nested coefficient pruning error too large");
        for(int j=0;j<128;++j) {
            const long double t=j*.372143L;long double sum=0;
            for(auto [h,v]:harmonics)sum+=v*std::sin((h/1000.L)*t);
            const long double expected=direct*std::sin(t)+
                (base*carrier<.5?std::sin(carrier*t+b2*std::sin(middle*t+b1*std::sin(inner*t))):0);
            require(std::abs(sum-expected)<2e-12,"double-Bessel signs/frequencies disagree with nested sine");
        }
    }
    std::vector<double> reference(int factor,bool ideal=false) const {
        std::vector<double> out(frames);
        const double base=440*std::pow(2.,double(c.note-69)/12)/rate;
        const double a=float(1-std::exp(-2*3.14159265358979*10000/rate));
        const auto config=factor>32?fm_high_density::config(factor):factor>1?fm_causal::alignedConfig(factor):fm_causal::Config{1,{}};
        for(auto [k,v]:harmonics) {
            const double f=base*(k/1000.);if(ideal&&f>=.5)continue;
            const double omega=tau*f;const auto lp=a/(Complex(1)-std::polar(1-a,-omega));
            Complex h=lp*lp;
            if(!ideal&&factor>1)h*=fm_causal::response(config,omega/factor)*
                std::polar(1.,omega*fm_high_density::delayFrames(config));
            auto ph=std::polar(1.,omega*(warmup+1));const auto step=std::polar(1.,omega);
            for(auto& x:out){x+=.5*.7*double(.32f)*(c.ma7?1:2)*v*std::imag(h*ph);ph*=step;}
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
        for(int k=0;k<factor;++k)high[size_t(k)]={source[size_t(k)]*.32f*(c.ma7?1:2),0};
        const auto x=bus.frame(std::span<const sine_output::Stereo>(high.data(),size_t(factor)),{});
        lp1+=a*(float(x.left)-lp1);lp2+=a*(lp1-lp2);
        require(std::isfinite(lp2)&&std::abs(lp2)<.92f,"prototype linear reference outside limiter domain");
        if(n>=warmup+72)out.push_back(lp2);
    }
    return out;
}

void spectra() {
    size_t policies=0;double maxError=0,maxNativeError=0;
    std::vector<Case> study;
    for(int note:{48,72,84,96,108,120})for(auto m:std::array<std::array<int,3>,3>{{{1,1,1},{3,2,5},{7,3,5}}})
    for(auto tl:std::array<std::pair<int,int>,4>{{{24,24},{24,36},{36,24},{63,63}}})study.push_back({note,m[0],m[1],m[2],tl.first,tl.second});
    for(int note:{124,125,127})study.push_back({note,2,2,2,24,24});
    for(int note:{48,84,96,120})for(auto m:std::array<std::array<int,3>,6>{{{1,1,1},{3,2,5},{7,3,5},{0,11,13},{11,13,14},{14,15,0}}})
    for(auto tl:std::array<std::pair<int,int>,3>{{{0,0},{12,24},{63,63}}})study.push_back({note,m[0],m[1],m[2],tl.first,tl.second,true});
    for(int note:{124,125,127})study.push_back({note,2,2,2,0,0,true});
    for(const auto& c:study) {
        const Model model(c);MaPlayer p,app,explicit16;
        require(p.init(fixture(c),rate)&&app.init(fixture(c),rate,false,{8,FmSubframeMath::Hermite,true}),"nested fixture init");
        require(explicit16.init(fixture(c),rate,false,{16,FmSubframeMath::Hermite}),"nested explicit16 init");
        std::vector<float> native(size_t(warmup+frames)*2),aligned(native.size()+144),fixed(aligned.size());
        require(p.render(native.data(),warmup+frames)==warmup+frames&&
                app.render(aligned.data(),warmup+frames+72)==warmup+frames+72&&
                explicit16.render(fixed.data(),warmup+frames+72)==warmup+frames+72,"nested native windows");
        require(aligned==fixed,"weak nested app policy must select16x");
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
            if(c.note==96&&c.inner==7&&c.innerTl==24&&c.middleTl==24&&factor>=8)
                std::cout<<"nested high-PM factor"<<factor<<" ideal RMS "<<error(fast,model.reference(0,true))<<'\n';
        }
    }
    std::cout<<policies<<" nested source spectral policies PASS; native max RMS "<<maxNativeError<<"; filtered max RMS "<<maxError<<'\n';
}


template<bool Fast> std::vector<double> transientRender(const Case& c,int factor,int gate,bool release) {
    auto p=patch(c);for(auto& o:p.ops)o.rr=15;
    const double initial=440*std::pow(2.,double(c.note-69)/12);
    FmVoice v;v.setSampleRate(rate);v.noteOn(p,initial,1);
    sine_output::Bus bus(factor);std::array<float,32> source{};std::array<sine_output::Stereo,32> high{};
    const float a=float(1-std::exp(-2*3.14159265358979*10000/rate));float lp1=0,lp2=0;
    std::vector<double> out;
    for(int n=0;n<gate+320;++n) {
        if(n==17)v.setPitch(initial*std::pow(2.,2./12));if(n==65)v.setPitch(initial);
        if(n==gate){if(release)v.noteOff(false);else v.active_=false;}
        fm_nested::frame<Fast>(v,std::span<float>(source.data(),size_t(factor)));
        for(int k=0;k<factor;++k)high[size_t(k)]={source[size_t(k)]*.32f*(c.ma7?1:2),0};
        const auto x=bus.frame(std::span<const sine_output::Stereo>(high.data(),size_t(factor)),{});
        lp1+=a*(float(x.left)-lp1);lp2+=a*(lp1-lp2);out.push_back(lp2);
    }
    require(!v.active_&&bus.pendingFrames()==0&&std::abs(out.back())<1e-7,"nested source/FIR tail failed to drain");
    return out;
}

std::vector<double> transientReference(const Case& c,int factor,int gate,bool release) {
    const int length=gate+320;
    const double initial=440*std::pow(2.,double(c.note-69)/12)/rate;
    const double depth=c.ma7?.42:4,direct=std::pow(10.,-.75*18/20);
    const double b1=tau*depth*std::pow(10.,-.75*c.innerTl/20),b2=tau*depth*std::pow(10.,-.75*c.middleTl/20);
    // Frozen RR15 software law: all effective rates>=60 use2.63ms to -96dB.
    // No envelope parameters or levels are read from a synthesized voice.
    const double decay=std::exp(-11.052408446/(.00263*rate));
    std::vector<double> raw(size_t(length)*factor);double phase=0,env=1;
    for(int n=0;n<length;++n) {
        const double increment=initial*(n>=17&&n<65?std::pow(2.,2./12):1);
        if(n>=gate){if(release){env*=decay;if(env<=.000015849)env=0;}else env=0;}
        const double g=float(env);
        for(int sub=1;sub<=factor;++sub) {
            const double t=tau*(phase+increment*double(sub)/factor);
            const double wave=direct*std::sin(t)+std::sin(ratio(c.carrier,c.ma7)*t+b2*g*
                std::sin(ratio(c.middle,c.ma7)*t+b1*g*std::sin(ratio(c.inner,c.ma7)*t)));
            raw[size_t(n)*factor+sub-1]=.5*.7*double(.32f)*(c.ma7?1:2)*g*wave;
        }
        phase+=increment;
    }
    // Dense effective FIR constructed separately from the production halfbands.
    std::vector<double> h{1};int stride=1;
    const auto config=factor>32?fm_high_density::config(factor):fm_causal::alignedConfig(factor);
    for(int taps:config.taps) {
        const auto stage=fm_causal::kernel(taps);
        std::vector<double> next(h.size()+(stage.size()-1)*stride);
        for(size_t i=0;i<h.size();++i)for(size_t j=0;j<stage.size();++j)next[i+j*stride]+=h[i]*stage[j];
        h=std::move(next);stride*=2;
    }
    const int padding=72-int((h.size()-1)/(2*factor));
    require(padding>=0,"nested reference invalid FIR padding");
    const float a=float(1-std::exp(-2*3.14159265358979*10000/rate));float lp1=0,lp2=0;
    std::vector<double> out;
    for(int n=0;n<length;++n) {
        const int64_t at=int64_t(n+1-padding)*factor-1;double sum=0;
        for(size_t k=0;k<h.size();++k){const int64_t j=at-int64_t(k);if(j>=0&&j<int64_t(raw.size()))sum+=h[k]*raw[size_t(j)];}
        lp1+=a*(float(sum)-lp1);lp2+=a*(lp1-lp2);out.push_back(lp2);
    }
    return out;
}

void transients() {
    size_t count=0;double maxRms=0,maxPeak=0;
    for(const Case c:std::array<Case,3>{{cases.back(),{96,7,3,5,0,0,true},{96,11,13,14,0,0,true}}})
    for(int factor:{8,16,32})for(int gate:{1,17,441})for(bool release:{false,true}) {
        const auto expected=transientReference(c,factor,gate,release);
        const auto exact=transientRender<false>(c,factor,gate,release),fast=transientRender<true>(c,factor,gate,release);
        for(const auto* actual:{&exact,&fast}) {
            const double e=error(*actual,expected);double peak=0;
            for(size_t j=0;j<actual->size();++j)peak=std::max(peak,std::abs((*actual)[j]-expected[j]));
            maxRms=std::max(maxRms,e);maxPeak=std::max(maxPeak,peak);
            require(e<4e-7&&peak<1e-6,"nested short-gate/pitch/release waveform differs from independent reference");++count;
        }
    }
    std::cout<<count<<" nested short-gate/pitch/release dense-FIR cases PASS; max RMS "<<maxRms<<"; peak "<<maxPeak<<'\n';
}

void clocks() {
    size_t count=0;
    for(int sampleRate:{8000,44100})for(int profile:{0,1,2,3})for(int factor:{2,4,8,16,32})
    for(bool fast:{false,true})for(int pattern=0;pattern<4;++pattern) {
        auto p=patch(cases[0]);p.modernTimbre=profile!=0;p.lfo=2;
        for(auto& o:p.ops){o.am=o.vib=true;o.dam=o.dvb=2;o.holdEnabled=true;o.ma35Multipliers=profile>=2;o.ma7Multipliers=profile==3;if(pattern==1){o.ar=8;o.sr=3;}}
        if(profile==3){p.ops[1].tl=p.ops[2].tl=0;p.ops[1].multi=11;p.ops[2].multi=13;p.ops[3].multi=14;}
        if(pattern==2){p.ops[1].ar=0;p.ops[1].rateLowBits=8;}
        FmVoice v,original;for(auto* x:{&v,&original}){x->setSampleRate(sampleRate);x->noteOn(p,317,.63f);}
        auto core=v;const bool production=profile>=2;
        require(v.supportsSineSubframes()==production,"weak nested profile scope changed");
        std::array<float,32> out{};
        for(int n=0;n<256;++n) {
            auto control=[&](FmVoice& x){if(n==0&&pattern==3)x.setVolume(1e-12f);if(n==32)x.setPitch(sampleRate*.71);if(n==64)x.setPitch(sampleRate*.013);if(n==96)x.setVolume(0);if(n==128)x.setVolume(.71f);if(n==160)x.noteOff(true);if(n==192)x.setHold(false);if(n==224)x.noteOn(p,419,.51f);};
            control(v);control(original);control(core);
            const auto r=fast?fm_nested::frame<true>(v,std::span<float>(out.data(),size_t(factor))):
                              fm_nested::frame<false>(v,std::span<float>(out.data(),size_t(factor)));
            if(production) {
                std::array<float,32> real{};require(core.sineDensityHint()==16&&core.tickSineSubframes(real.data(),factor,fast?FmSubframeMath::Hermite:FmSubframeMath::Exact),"production weak nested source rejected");
                for(int k=0;k<factor;++k)require(std::bit_cast<uint32_t>(real[size_t(k)])==std::bit_cast<uint32_t>(out[size_t(k)]),"production nested samples differ from prototype");
            }
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

void policies() {
    for(int tl1:{0,12,24,63})for(int tl2:{0,12,24,63}) {
        auto c=cases.back();c.ma7=true;auto p=patch(c);p.ops[1].tl=uint8_t(tl1);p.ops[2].tl=uint8_t(tl2);
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,2093,1);
        require(v.supportsSineSubframes()&&v.sineDensityHint()==16,"MA7 nested class must use16x");
        v.setPitch(523);require(v.sineDensityHint()==16,"MA7 nested density decreased during note");
    }
    for(int kind=0;kind<6;++kind) {
        auto c=cases.back();c.ma7=true;auto p=patch(c);
        if(kind==0)p.ops[0].ma7Multipliers=false;if(kind==1)p.modernTimbre=false;
        if(kind==2)p.ops[1].fixedFrequency=true;if(kind==3)p.ops[2].ma7Detune=1;
        if(kind==4)p.ops[2].ar=0;if(kind==5)p.ops[3].ar=0;
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,1046,1);
        require(!v.supportsSineSubframes()&&v.sineDensityHint()==0,"unsupported MA7 nested class admitted");
    }
    for(int tl1:{24,36,63})for(int tl2:{24,36,63}) {
        auto p=patch(cases.back());p.ops[1].tl=uint8_t(tl1);p.ops[2].tl=uint8_t(tl2);
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,2093,1);
        require(v.supportsSineSubframes()&&v.sineDensityHint()==16,"weak VM35 nested class must use16x");
        v.setPitch(523);require(v.sineDensityHint()==16,"nested density decreased during note");
    }
    for(int kind=0;kind<7;++kind) {
        auto p=patch(cases.back());
        if(kind==0)p.modernTimbre=false;if(kind==1)p.ops[1].ma35Multipliers=false;
        if(kind==2)p.ops[2].ma7Multipliers=true;if(kind==3)p.ops[1].tl=23;
        if(kind==4)p.ops[2].tl=23;if(kind==5)p.ops[3].ar=0;if(kind==6)p.ops[2].ar=0;
        FmVoice v;v.setSampleRate(rate);v.noteOn(p,1046,1);auto original=v;
        require(!v.supportsSineSubframes()&&v.sineDensityHint()==0,"unsupported nested class admitted");
        require(v.tick()==original.tick(),"nested policy query advanced clock");
    }
    auto p=patch(cases.back());p.ops[1].ar=0;
    FmVoice v;v.setSampleRate(rate);v.noteOn(p,1046,1);
    require(v.supportsSineSubframes()&&v.sineDensityHint()==8,"existing reduced graph6 density changed");
    std::cout<<"9 VM35 and16 MA7 TL policies,13 exclusions and existing reduced graph6 policy PASS\n";
}
}
#ifndef FM_NESTED_SINE_TEST_NO_MAIN
int main(){try{nested::policies();nested::clocks();nested::spectra();nested::transients();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
