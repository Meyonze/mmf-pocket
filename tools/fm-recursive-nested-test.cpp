// SPDX-License-Identifier: Apache-2.0
// Host recurrence accuracy, all source factors, native clocks and filtered sound.
#define FM_STRONG_NESTED_TEST_NO_MAIN
#include "fm-strong-nested-test.cpp"
#include "fm-recursive-nested-probe.h"
#include "fm-nested-density-plan.h"
#include <memory>
namespace recursive_test {
using player_sideband::require;
void phases() {
    double maximum=0;size_t points=0;
    for(int p=0;p<=256;++p)for(int d=-32;d<=32;++d) {
        const double phase=2*std::numbers::pi*p/256,step=.2*d/32;
        std::array<double,127> values;
        require(fm_recursive::linearSines(phase,step,std::span<double>(values)),"valid recurrence rejected");
        for(size_t k=0;k<values.size();++k) {
            const long double angle=static_cast<long double>(phase)+(k+1)*static_cast<long double>(step);
            const double error=double(std::abs(static_cast<long double>(values[k])-std::sin(angle)));
            maximum=std::max(maximum,error);require(error<5e-13,"recurrence phase-grid error too large");++points;
        }
    }
    for(auto [phase,step]:std::array<std::pair<double,double>,5>{{
        {0,std::nextafter(.2,1.)},{0,-std::nextafter(.2,1.)},{-1,0},
        {0,std::numeric_limits<double>::infinity()},{std::numeric_limits<double>::quiet_NaN(),0}}}) {
        std::array<float,128> values;values.fill(123);
        require(!fm_recursive::linearSines(phase,step,std::span<float>(values.data(),127))&&
                std::all_of(values.begin(),values.end(),[](float v){return v==123;}),"bad recurrence changed output");
    }
    std::array<float,128> values;values.fill(123);
    require(!fm_recursive::linearSines(0,0,std::span<float>(values))&&values[0]==123,"oversize recurrence accepted");
    std::cout<<points<<" recurrence phase points PASS; max double error "<<maximum<<'\n';
}
void plans() {
    bool unproven=false,f16=false,f32=false,f64=false,f128=false;int count=0;
    for(const nested::Case c:std::array<nested::Case,6>{{
        {84,3,2,5,18,18},{96,7,3,5,18,18},{84,3,2,5,12,12},
        {96,7,3,5,12,12},{84,1,1,1,0,0},{96,3,2,5,0,0}}}) {
        const nested::Model model(c);const double base=440*std::pow(2.,double(c.note-69)/12)/44100;
        const double beta=nested::tau*4*std::pow(10.,-.75*c.innerTl/20);
        const fm_nested_plan::Spectrum s{beta,beta,base*c.inner,base*c.middle,base*c.carrier,base/2,std::pow(10.,-.75*18/20),base};
        const auto p=fm_nested_plan::choose(s,1e-4);
        unproven=unproven||p.factor==0;f16=f16||p.factor==16;f32=f32||p.factor==32;f64=f64||p.factor==64;f128=f128||p.factor==128;
        if(p.factor) {
            double actual=0;for(auto [h,v]:model.harmonics)if(base*(h/1000.)>=p.factor-.5)actual+=std::abs(v);
            require(actual<=p.tail+1e-12&&actual<=1e-4+1e-12,"planned alias tail exceeded independent coefficient sum");
        }
        const auto lattice=fm_nested_plan::choose(s,1e-4,128,fm_nested_plan::Method::KnownLattice);
        if(lattice.factor){double actual=0;for(auto [h,v]:model.harmonics)if(base*(h/1000.)>=lattice.factor-.5)actual+=std::abs(v);
            require(actual<=lattice.tail+1e-12&&actual<=1e-4+1e-12,"known-lattice plan exceeded independent coefficients");}
        for(int cap:{16,32,64,128}) {
            const auto limited=fm_nested_plan::choose(s,1e-4,cap);
            require(limited.factor==0||limited.factor<=cap,"stationary planner exceeded caller density limit");
            const auto tighter=fm_nested_plan::choose(s,1e-6,cap);
            require((tighter.factor==0?256:tighter.factor)>=(limited.factor==0?256:limited.factor),"tighter alias budget reduced planned density");
        }
        std::cout<<"stationary plan note="<<c.note<<" inner="<<c.inner<<" TL="<<c.innerTl<<" factor="<<p.factor<<" tail="<<p.tail<<'\n';++count;
    }
    require(unproven&&f16&&f32&&f64&&f128,"stationary study lost its distinct density/unproven cases");
    for(double bad:{0.,-1.,std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;try{(void)fm_nested_plan::choose({1,1,.1,.2,.3,.1},bad);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid alias-tail budget accepted");
    }
    std::cout<<count<<" stationary density plans, coefficient-tail/cap/budget checks PASS\n";
}
void independentBounds() {
    int identities=0,weighted=0;
    for(double x:{0.,1.,3.164,8.917433096091973,25.132741228718345}) {
        const auto c=fm_bessel::coefficients(x,256);
        for(double s:{.001,.01,.1,.3}) {
            double sum=c[0]*c[0];for(int k=1;k<=256;++k)sum+=2*c[size_t(k)]*c[size_t(k)]*std::cosh(2*k*s);
            const double expected=std::cyl_bessel_i(0.,2*x*std::sinh(s));
            require(std::abs(sum/expected-1)<2e-12,"weighted Bessel energy differs from Graf identity");++identities;
        }
        for(double a:{0.,.03,.1})for(double d:{.003,.03,.3}) {
            double sum=std::abs(c[0]);for(int k=1;k<=256;++k)sum+=2*std::abs(c[size_t(k)])*std::exp(k*a);
            const double bound=std::sqrt(2/std::tanh(d))*std::exp(x*std::sinh(a+d));
            require(sum<=bound*(1+1e-12),"weighted Bessel L1 exceeds Cauchy bound");++weighted;
        }
    }
    int tails=0;
    for(bool swap:{false,true}) {
        const double b1=swap?3.164:8.917433096091973,b2=swap?8.917433096091973:3.164;
        const double fi=std::sqrt(2.)/10,fm=std::sqrt(3.)/10,fc=std::sqrt(5.)/10;
        const auto outer=fm_bessel::coefficients(b2,64);std::array<double,4> sums{};
        for(int n=-64;n<=64;++n) {
            const auto inner=fm_bessel::coefficients(n*b1,1024);
            for(int k=-1024;k<=1024;++k) {
                const double term=std::abs(fm_bessel::signedOrder(outer,n)*fm_bessel::signedOrder(inner,k));
                const double frequency=std::abs(fc+n*fm+k*fi);
                for(int i=0;i<4;++i)if(frequency>=(16<<i)-.5)sums[size_t(i)]+=term;
            }
        }
        const fm_nested_plan::Spectrum spec{b1,b2,fi,fm,fc,0};
        const auto plan=fm_nested_plan::choose(spec,1e-4);
        require(plan.factor!=0,"independent-frequency fixture has no bounded density");
        for(int i=0;i<4;++i) {
            const double bound=fm_nested_tail::independentBound(b1,b2,fi,fm,fc,(16<<i)-.5);
            require(sums[size_t(i)]<=bound+1e-12,"non-lattice coefficient tail exceeds independent bound");++tails;
            if(plan.factor==(16<<i))require(sums[size_t(i)]<=1e-4+1e-12,"non-lattice plan exceeded alias budget");
        }
    }
    std::cout<<identities<<" weighted Bessel identities, "<<weighted<<" Cauchy checks and "<<tails<<" non-lattice tail sums PASS\n";
    bool rejected=false;
    try{(void)fm_nested_tail::independentBound(33,1,.1,.2,.3,15.5);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"out-of-range independent bound accepted");
}
std::vector<double> render(const nested::Case& c,int factor,int gate=-1,bool release=false) {
    FmVoice v;auto patch=nested::patch(c);if(gate>=0)for(auto& op:patch.ops)op.rr=15;
    const double hz=440*std::pow(2.,double(c.note-69)/12);v.setSampleRate(44100);v.noteOn(patch,hz,1);
    std::unique_ptr<fxchain::smaf::sine_output::Bus> ordinary;
    std::unique_ptr<fm_high_density::Bus> high;
    if(factor<=32)ordinary=std::make_unique<fxchain::smaf::sine_output::Bus>(factor);
    else high=std::make_unique<fm_high_density::Bus>(factor);
    std::vector<float> source(size_t(factor),0);
    std::vector<fxchain::smaf::sine_output::Stereo> samples(size_t(factor),fxchain::smaf::sine_output::Stereo{});
    const float a=float(1-std::exp(-2*3.14159265358979*10000/44100));float lp1=0,lp2=0;
    std::vector<double> out;const int length=gate<0?nested::warmup+72+nested::frames:gate+320;
    for(int n=0;n<length;++n) {
        if(gate>=0){if(n==17)v.setPitch(hz*std::pow(2.,2./12));if(n==65)v.setPitch(hz);
            if(n==gate){if(release)v.noteOff(false);else v.active_=false;}}
        fm_recursive::frame(v,source);
        for(int k=0;k<factor;++k)samples[size_t(k)]={source[size_t(k)]*.32f*(c.ma7?1:2),0};
        const auto x=ordinary?ordinary->frame(samples,{}):high->frame(samples);
        lp1+=a*(float(x.left)-lp1);lp2+=a*(lp1-lp2);
        if(gate>=0||n>=nested::warmup+72)out.push_back(lp2);
    }
    if(gate>=0)require(!v.active_&&std::abs(out.back())<1e-7,"recursive release tail failed to drain");
    return out;
}
void spectra() {
    int count=0;double maximum=0;
    for(const nested::Case c:std::array<nested::Case,8>{{
        {84,3,2,5,18,18},{96,7,3,5,18,18},{84,3,2,5,12,12},{96,7,3,5,12,12},
        {84,1,1,1,0,0},{96,3,2,5,0,0},{96,7,3,5,24,24},{96,11,13,14,0,0,true}}}) {
        const nested::Model model(c);
        for(int factor:{2,4,8,16,32,64,128}) {
            const double error=player_sideband::error(recursive_test::render(c,factor),model.reference(factor));
            maximum=std::max(maximum,error);require(error<2e-6,"recursive source differs from Bessel/FIR reference");++count;
        }
    }
    std::cout<<count<<" recursive finite-FIR spectral policies PASS; max RMS "<<maximum<<'\n';
}
void transients() {
    int count=0;double maximum=0,peak=0;
    for(const nested::Case c:std::array<nested::Case,3>{{{96,7,3,5,12,12},{84,1,1,1,0,0},{96,11,13,14,0,0,true}}})
    for(int factor:{16,32,64,128})for(int gate:{1,17,441})for(bool release:{false,true}) {
        const auto expected=nested::transientReference(c,factor,gate,release),actual=recursive_test::render(c,factor,gate,release);
        const double error=player_sideband::error(actual,expected);maximum=std::max(maximum,error);
        double p=0;for(size_t j=0;j<actual.size();++j)p=std::max(p,std::abs(actual[j]-expected[j]));peak=std::max(peak,p);
        require(error<4e-7&&p<1e-6,"recursive transient differs from dense FIR reference");++count;
    }
    std::cout<<count<<" recursive dense-FIR gate/pitch/release cases PASS; max RMS "<<maximum<<" peak "<<peak<<'\n';
}
void clocks() {
    int count=0;double maximum=0;
    for(int rate:{8000,44100})for(int factor:{2,4,8,16,32,64,128})for(int profile:{0,1,2})for(int stopped=-1;stopped<4;++stopped) {
        nested::Case c{96,7,3,5,12,12};c.ma7=profile==2;auto p=nested::patch(c);p.modernTimbre=profile!=0;
        for(auto& op:p.ops){op.am=op.vib=true;op.dam=op.dvb=2;op.holdEnabled=true;}
        if(stopped>=0){auto& op=p.ops[size_t(stopped)];op.ar=0;op.vib=stopped%2!=0;op.rateLowBits=profile==2?8:0;}
        FmVoice v,scalar;for(auto* x:{&v,&scalar}){x->setSampleRate(rate);x->noteOn(p,317,.63f);}
        std::vector<float> a(size_t(factor),0),b(a);
        for(int n=0;n<320;++n) {
            for(auto* x:{&v,&scalar}) {
                if(n==16)x->setVibratoScale(10000);if(n==32)x->setPitch(rate*.71);
                if(n==64){x->setPitch(rate*.013);x->setVibratoScale(1);}
                if(n==96)x->setVolume(0);if(n==128)x->setVolume(1e-12f);
                if(n==160)x->setVolume(.71f);if(n==192)x->noteOff(true);
                if(n==224)x->setHold(false);if(n==256)x->noteOn(p,419,.51f);
            }
            const auto r=fm_recursive::frame(v,a),s=fm_nested::frame<true,128>(scalar,b);
            require(std::bit_cast<uint32_t>(a.back())==std::bit_cast<uint32_t>(b.back())&&
                    std::bit_cast<uint32_t>(r.reconstructedEndpoint)==std::bit_cast<uint32_t>(s.reconstructedEndpoint),"recursive native endpoint changed");
            require(v.active_==scalar.active_&&v.released_==scalar.released_&&v.recentLevel_==scalar.recentLevel_&&
                    v.lfoPhase_==scalar.lfoPhase_&&v.lfoSin_==scalar.lfoSin_,"recursive voice state changed");
            for(int j=0;j<4;++j){const auto& x=v.ops_[j];const auto& y=scalar.ops_[j];
                require(x.phase_==y.phase_&&x.env_.phase_==y.env_.phase_&&x.env_.level_==y.env_.level_&&
                        x.env_.pendingKeyOff_==y.env_.pendingKeyOff_&&v.fbMem_[j][0]==scalar.fbMem_[j][0]&&
                        v.fbMem_[j][1]==scalar.fbMem_[j][1],"recursive operator clock changed");}
            for(size_t k=0;k<a.size();++k){require(std::isfinite(a[k])&&std::isfinite(b[k]),"recursive nonfinite sample");
                maximum=std::max(maximum,std::abs(double(a[k])-b[k]));}
        }++count;
    }
    require(maximum<2e-6,"recursive intermediate difference too large");
    for(int factor:{0,1,3,256}) {
        FmVoice v;v.setSampleRate(44100);v.noteOn(nested::patch(nested::cases.back()),317,1);auto original=v;
        std::vector<float> out(256,123);bool rejected=false;
        try{(void)fm_recursive::frame(v,std::span<float>(out.data(),size_t(factor)));}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&std::all_of(out.begin(),out.end(),[](float x){return x==123;})&&v.tick()==original.tick(),"bad recursive factor changed output/state");
    }
    for(bool feedback:{false,true}) {
        auto patch=nested::patch(nested::cases.back());if(feedback)patch.ops[1].fb=3;else patch.ops[2].wave=6;
        FmVoice v;v.setSampleRate(44100);v.noteOn(patch,317,1);auto original=v;std::array<float,128> output;output.fill(123);bool rejected=false;
        try{(void)fm_recursive::frame(v,output);}catch(const std::runtime_error&){rejected=true;}
        require(rejected&&std::all_of(output.begin(),output.end(),[](float x){return x==123;})&&v.tick()==original.tick(),"unsafe recursive shape changed output/state");
    }
    std::cout<<count<<" recursive native-clock/fallback/Hold/retrigger cases PASS; raw scalar max difference "<<maximum<<'\n';
}
}
int main(){try{recursive_test::phases();recursive_test::plans();recursive_test::independentBounds();recursive_test::clocks();recursive_test::spectra();recursive_test::transients();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
