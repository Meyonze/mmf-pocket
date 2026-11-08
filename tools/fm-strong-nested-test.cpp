// SPDX-License-Identifier: Apache-2.0
// Host-only strong-PM reference and64/128x feasibility. No app policy changes.
#define FM_NESTED_SINE_TEST_NO_MAIN
#include "fm-nested-sine-test.cpp"
#include "fm-nested-tail-bound.h"

namespace strong_nested {
using player_sideband::require;
void besselReference() {
    // Frozen mpmath70-decimal-digit anchors; no Python dependency at runtime.
    struct Anchor { double x;int order;double expected; };
    for(auto a:std::array<Anchor,6>{{
        {8.917433096091973,0,-.069699919288000347812887632718015422},
        {8.917433096091973,17,.000097894437354309980843366819237630711},
        {100,128,.000000045943874113365107081146169060922669},
        {500,300,-.0029540008506893706682049772686207862},
        {500,1024,2.6188548783838222685756553722365384e-212},
        {1600,1600,.038244210879651316632886817936370535}}}) {
        const auto c=fm_bessel::coefficients(a.x,a.order);
        require(std::abs(c.back()-a.expected)<=std::abs(a.expected)*2e-12+1e-300,
                "Bessel reference differs from high-precision anchor");
    }
    int comparisons=0;
    for(double x:{.001,1.,3.164,8.917433096091973,25.132741228718345,100.,500.,1600.}) {
        const int order=int(std::ceil(x))+192;
        const auto c=fm_bessel::coefficients(x,order),negative=fm_bessel::coefficients(-x,order);
        double energy=0,even=0;
        for(int k=0;k<=order;++k) {
            require(negative[size_t(k)]==c[size_t(k)]*(k%2?-1:1),"Bessel negative argument sign");
            energy+=(k?2:1)*c[size_t(k)]*c[size_t(k)];
            if(k%2==0)even+=(k?2:1)*c[size_t(k)];
            if(k<128) {
                const double old=std::cyl_bessel_j(double(k),x);
                require(std::isfinite(old)&&std::abs(old-c[size_t(k)])<2e-12,
                        "Bessel reference differs in standard-library overlap");++comparisons;
            }
        }
        require(std::abs(energy-1)<2e-13&&std::abs(even-1)<2e-13,"Bessel normalization/energy incomplete");
    }
    for(double bad:{std::numeric_limits<double>::infinity(),8193.}) {
        bool rejected=false;try{(void)fm_bessel::coefficients(bad,8);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid Bessel domain accepted");
    }
    const auto tiny=fm_bessel::coefficients(1e-250,4);
    require(tiny[0]==1&&tiny[1]==5e-251&&tiny[2]==0,"tiny Bessel reference lost its leading term");
    std::cout<<"6 high-precision Bessel anchors and "<<comparisons<<" standard-library overlaps PASS\n";
}
void tailBounds() {
    int count=0;
    for(const nested::Case c:std::array<nested::Case,2>{{{96,7,3,5,12,24},{96,7,3,5,24,12}}}) {
        const nested::Model model(c);const double base=440*std::pow(2.,double(c.note-69)/12)/44100;
        for(int factor:{16,32,64,128}) {
            const double bound=fm_nested_tail::bound(nested::tau*4*std::pow(10.,-.75*c.innerTl/20),
                nested::tau*4*std::pow(10.,-.75*c.middleTl/20),base*c.inner,base*c.middle,base*c.carrier,
                base/2,factor-.5,std::pow(10.,-.75*18/20),base);
            double sum=0;for(auto [h,v]:model.harmonics)if(base*(h/1000.)>=factor-.5)sum+=std::abs(v);
            require(sum<=bound+1e-12,"unequal nested PM indices exceeded analytic tail");++count;
        }
    }
    bool rejected=false;
    try{(void)fm_nested_tail::bound(1,1,.1,.2,.3,.07,15.5);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"non-lattice nested bound accepted");
    std::cout<<count<<" unequal-index analytic tail comparisons and non-lattice rejection PASS\n";
}

std::vector<double> denseKernel(int factor) {
    std::vector<double> h{1};int stride=1;
    for(int taps:fm_high_density::config(factor).taps) {
        const auto stage=fm_causal::kernel(taps);std::vector<double> next(h.size()+(stage.size()-1)*stride);
        for(size_t i=0;i<h.size();++i)for(size_t j=0;j<stage.size();++j)next[i+j*stride]+=h[i]*stage[j];
        h=std::move(next);stride*=2;
    }
    return h;
}
void bus() {
    using fxchain::smaf::sine_output::Stereo;
    for(int factor:{64,128}) {
        const auto h=denseKernel(factor);
        require((h.size()-1)==size_t(144*factor),"high-density support/delay changed");
        fm_high_density::Bus b(factor);std::vector<Stereo> input(size_t(factor),Stereo{});
        for(int n=0;n<160;++n) {
            std::fill(input.begin(),input.end(),Stereo{});if(n==0)input[0]={1,-.5};
            const auto y=b.frame(input);const size_t at=size_t(n+1)*factor-1;
            const double expected=at<h.size()?h[at]:0;
            require(std::abs(y.left-expected)<2e-14&&std::abs(y.right+.5*expected)<2e-14,
                    "high-density impulse differs from dense convolution");
        }
        b.reset();std::fill(input.begin(),input.end(),Stereo{1,-.5});
        Stereo dc;for(int n=0;n<160;++n)dc=b.frame(input);
        require(std::abs(dc.left-1)<2e-13&&std::abs(dc.right+.5)<2e-13,"high-density DC gain");
        auto control=b;bool rejected=false;
        try{(void)b.frame(std::span<const Stereo>(input.data(),input.size()-1));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"wrong high-density bus block accepted");
        auto a=b.frame(input),c=control.frame(input);require(a.left==c.left&&a.right==c.right,"bad block changed bus history");
        b.reset();std::fill(input.begin(),input.end(),Stereo{});
        for(int n=0;n<160;++n){const auto y=b.frame(input);require(y.left==0&&y.right==0,"high-density reset leaked history");}
    }
    std::cout<<"64/128x dense impulse, DC, support, stereo, reset and rejection PASS\n";
}

template<bool Fast> std::vector<double> render(const nested::Case& c,int factor) {
    if(factor<=32)return nested::render<Fast>(c,factor);
    FmVoice v;v.setSampleRate(44100);v.noteOn(nested::patch(c),440*std::pow(2.,double(c.note-69)/12),1);
    fm_high_density::Bus bus(factor);std::vector<float> source(size_t(factor),0);
    std::vector<fxchain::smaf::sine_output::Stereo> high(size_t(factor),fxchain::smaf::sine_output::Stereo{});
    const float a=float(1-std::exp(-2*3.14159265358979*10000/44100));float lp1=0,lp2=0;
    std::vector<double> out;
    for(int n=0;n<nested::warmup+72+nested::frames;++n) {
        fm_nested::frame<Fast,128>(v,source);
        for(int k=0;k<factor;++k)high[size_t(k)]={source[size_t(k)]*.32f*(c.ma7?1:2),0};
        const auto x=bus.frame(high);lp1+=a*(float(x.left)-lp1);lp2+=a*(lp1-lp2);
        if(n>=nested::warmup+72)out.push_back(lp2);
    }
    return out;
}
void spectra() {
    int count=0;double maxError=0;
    for(const nested::Case c:std::array<nested::Case,6>{{
        {84,3,2,5,18,18},{96,7,3,5,18,18},{84,3,2,5,12,12},
        {96,7,3,5,12,12},{84,1,1,1,0,0},{96,3,2,5,0,0}}}) {
        const nested::Model model(c);const auto ideal=model.reference(0,true);
        std::cout<<"strong note="<<c.note<<" inner="<<c.inner<<" TL="<<c.innerTl
                 <<" native_ideal_RMS="<<player_sideband::error(model.reference(0),ideal);
        for(int factor:{16,32,64,128}) {
            const auto expected=model.reference(factor),exact=strong_nested::render<false>(c,factor),fast=strong_nested::render<true>(c,factor);
            for(const auto* actual:{&exact,&fast}) {
                const double e=player_sideband::error(*actual,expected);maxError=std::max(maxError,e);
                require(e<2e-6,"strong source differs from finite-FIR reference");++count;
            }
            std::cout<<" x"<<factor<<"="<<player_sideband::error(fast,ideal);
            const double base=440*std::pow(2.,double(c.note-69)/12)/44100;
            const double beta=nested::tau*4*std::pow(10.,-.75*c.innerTl/20);
            const double bound=fm_nested_tail::bound(beta,beta,base*c.inner,base*c.middle,base*c.carrier,
                base/2,factor-.5,std::pow(10.,-.75*18/20),base);
            double omitted=0;for(auto [h,v]:model.harmonics)if(base*(h/1000.)>=factor-.5)omitted+=std::abs(v);
            require(omitted<=bound+1e-12,"analytic nested tail misses double-Bessel coefficients");
            std::cout<<" tail_bound"<<factor<<"="<<bound;
        }
        std::cout<<'\n';
    }
    std::cout<<count<<" strong nested policies PASS; max equation RMS "<<maxError<<'\n';
}
void clocks() {
    int count=0;
    for(int rate:{8000,44100})for(int factor:{64,128})for(int profile:{0,1,2})for(bool fast:{false,true}) {
        nested::Case c{96,7,3,5,12,12};c.ma7=profile==2;auto p=nested::patch(c);p.modernTimbre=profile!=0;
        for(auto& o:p.ops){o.am=o.vib=true;o.dam=o.dvb=2;o.holdEnabled=true;}
        FmVoice v,original;for(auto* x:{&v,&original}){x->setSampleRate(rate);x->noteOn(p,317,.63f);}
        std::vector<float> output(size_t(factor),0);
        for(int n=0;n<320;++n) {
            for(auto* x:{&v,&original}) {
                if(n==32)x->setPitch(rate*.71);if(n==64)x->setPitch(rate*.013);
                if(n==96)x->setVolume(0);if(n==128)x->setVolume(1e-12f);
                if(n==160)x->setVolume(.71f);if(n==192)x->noteOff(true);
                if(n==224)x->setHold(false);if(n==256)x->noteOn(p,419,.51f);
            }
            const auto frame=fast?fm_nested::frame<true,128>(v,output):fm_nested::frame<false,128>(v,output);
            const float endpoint=original.tick();
            require(std::bit_cast<uint32_t>(output.back())==std::bit_cast<uint32_t>(endpoint)&&
                    std::bit_cast<uint32_t>(frame.reconstructedEndpoint)==std::bit_cast<uint32_t>(endpoint),"high-density native endpoint changed");
            require(v.active_==original.active_&&v.released_==original.released_&&v.recentLevel_==original.recentLevel_&&
                    v.lfoPhase_==original.lfoPhase_&&v.lfoSin_==original.lfoSin_,"high-density voice clock changed");
            for(int j=0;j<4;++j) {
                const auto& a=v.ops_[j];const auto& b=original.ops_[j];
                require(a.phase_==b.phase_&&a.env_.level_==b.env_.level_&&a.env_.phase_==b.env_.phase_&&
                        a.env_.pendingKeyOff_==b.env_.pendingKeyOff_&&v.fbMem_[j][0]==original.fbMem_[j][0]&&
                        v.fbMem_[j][1]==original.fbMem_[j][1],"high-density operator clock changed");
            }
            require(std::all_of(output.begin(),output.end(),[](float f){return std::isfinite(f);}),"high-density nonfinite source");
        }++count;
    }
    std::cout<<count<<" high-density native clock/endpoint/Hold/retrigger cases PASS\n";
    for(int factor:{0,1,3,256}) {
        FmVoice v;v.setSampleRate(44100);v.noteOn(nested::patch(nested::cases.back()),317,1);auto original=v;
        std::vector<float> output(256,123);bool rejected=false;
        try{(void)fm_nested::frame<true,128>(v,std::span<float>(output.data(),size_t(factor)));}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&std::all_of(output.begin(),output.end(),[](float x){return x==123;})&&v.tick()==original.tick(),"bad high-density source factor changed output/state");
    }
    FmVoice v;v.setSampleRate(44100);v.noteOn(nested::patch(nested::cases.back()),317,1);auto original=v;
    std::array<float,64> output;output.fill(123);
    require(!v.tickSineSubframes(output.data(),64)&&std::all_of(output.begin(),output.end(),[](float x){return x==123;})&&v.tick()==original.tick(),"production source64 limit changed");
}
template<bool Fast> std::vector<double> transient(const nested::Case& c,int factor,int gate,bool release) {
    auto p=nested::patch(c);for(auto& o:p.ops)o.rr=15;
    FmVoice v;v.setSampleRate(44100);const double hz=440*std::pow(2.,double(c.note-69)/12);v.noteOn(p,hz,1);
    fm_high_density::Bus bus(factor);std::vector<float> source(size_t(factor),0);
    std::vector<fxchain::smaf::sine_output::Stereo> high(size_t(factor),fxchain::smaf::sine_output::Stereo{});
    const float a=float(1-std::exp(-2*3.14159265358979*10000/44100));float lp1=0,lp2=0;std::vector<double> out;
    for(int n=0;n<gate+320;++n) {
        if(n==17)v.setPitch(hz*std::pow(2.,2./12));if(n==65)v.setPitch(hz);
        if(n==gate){if(release)v.noteOff(false);else v.active_=false;}
        fm_nested::frame<Fast,128>(v,source);
        for(int k=0;k<factor;++k)high[size_t(k)]={source[size_t(k)]*.32f*2,0};
        const auto x=bus.frame(high);lp1+=a*(float(x.left)-lp1);lp2+=a*(lp1-lp2);out.push_back(lp2);
    }
    require(!v.active_&&std::abs(out.back())<1e-7,"strong source/FIR tail failed to drain");return out;
}
void transients() {
    int count=0;double rms=0,peak=0;
    for(const nested::Case c:std::array<nested::Case,2>{{{96,7,3,5,12,12},{84,1,1,1,0,0}}})
    for(int factor:{64,128})for(int gate:{1,17,441})for(bool release:{false,true}) {
        const auto expected=nested::transientReference(c,factor,gate,release);
        const auto exact=strong_nested::transient<false>(c,factor,gate,release),fast=strong_nested::transient<true>(c,factor,gate,release);
        for(const auto* actual:{&exact,&fast}) {
            const double e=player_sideband::error(*actual,expected);rms=std::max(rms,e);
            double p=0;for(size_t j=0;j<actual->size();++j)p=std::max(p,std::abs((*actual)[j]-expected[j]));peak=std::max(peak,p);
            require(e<4e-7&&p<1e-6,"strong short-gate/pitch/release differs from dense FIR reference");++count;
        }
    }
    std::cout<<count<<" strong dense-FIR short-gate/pitch/release cases PASS; max RMS "<<rms<<" peak "<<peak<<'\n';
}
}
int main(){try{strong_nested::besselReference();strong_nested::tailBounds();strong_nested::bus();strong_nested::clocks();strong_nested::spectra();strong_nested::transients();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
