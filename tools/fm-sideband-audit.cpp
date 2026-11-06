// SPDX-License-Identifier: Apache-2.0
// Steady sine PM oracle: software alias measurement, not handset calibration.
#include "fm-native-clock-probe.h"
#include "fm-causal-decimator.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>
using namespace fxchain::smaf;
namespace sideband {
constexpr double tau=2*std::numbers::pi,rate=44100;
constexpr int frames=4096,maxOrder=96;
using Complex=std::complex<double>;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::vector<Complex> transform(std::vector<Complex> x,bool inverse) {
    const size_t n=x.size();require(n>=2&&(n&(n-1))==0,"FFT size must be a power of two");
    for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}
    for(size_t length=2;length<=n;length*=2){const Complex step=std::polar(1.,(inverse?1:-1)*tau/length);
        for(size_t begin=0;begin<n;begin+=length){Complex phase=1;
            for(size_t j=0;j<length/2;++j){const auto even=x[begin+j],odd=x[begin+j+length/2]*phase;
                x[begin+j]=even+odd;x[begin+j+length/2]=even-odd;phase*=step;}}}
    if(inverse)for(auto& v:x)v/=double(n);
    return x;
}
struct Case {bool vm35;int base,carrier,modulator,tl;};
// Deliberately frozen independent policy values; do not read private core state.
double depth(const Case& c){return c.vm35?4.:.42;}
double amplitude(const Case& c){return std::pow(10.,-.75*c.tl/20.);}
double index(const Case& c){return tau*depth(c)*amplitude(c);}
std::vector<double> coefficients(double x) {
    std::vector<double> values(maxOrder+1);
    for(int n=0;n<=maxOrder;++n)values[n]=x==0?(n==0?1.:0.):std::cyl_bessel_j(double(n),x);
    return values;
}
double coefficient(const std::vector<double>& b,int n){return b[std::abs(n)]*(n<0&&(std::abs(n)&1)?-1:1);}
std::vector<double> reference(const Case& c,bool bounded,double offset=0.) {
    const auto b=coefficients(index(c));std::vector<double> out(frames,0);
    for(int n=-maxOrder;n<=maxOrder;++n){const int bin=c.base*(c.carrier+n*c.modulator);
        if(bounded&&std::abs(bin)>=frames/2)continue;
        const double gain=.7*coefficient(b,n);if(gain==0)continue;
        for(int i=0;i<frames;++i)out[i]+=gain*std::sin(tau*double(bin)*(double(i+1)+offset)/frames);
    }
    return out;
}
std::vector<double> render(const Case& c,int factor,int outputFrames=frames) {
    FmVoicePatch patch;patch.modernTimbre=true;patch.algorithm=0;patch.fourOp=false;
    for(int i=0;i<2;++i){auto& p=patch.ops[i];p.wave=0;p.ar=15;p.dr=p.sr=p.sl=0;p.tl=uint8_t(i==0?c.tl:0);
        p.multi=uint8_t(i==0?c.modulator:c.carrier);p.ma35Multipliers=c.vm35;}
    FmVoice voice;voice.setSampleRate(rate*factor);voice.setVolume(1);voice.noteOn(patch,rate*c.base/frames,1);
    std::vector<double> signal(size_t(outputFrames)*size_t(factor));for(auto& x:signal)x=voice.tick();return signal;
}
std::vector<double> idealDecimate(const std::vector<double>& input,int factor) {
    require(input.size()==size_t(frames*factor),"oversampled input size mismatch");
    std::vector<Complex> values(input.begin(),input.end());auto bins=transform(values,false);
    // Periodic, zero-phase, ideal offline low-pass; no causal/device claim.
    for(size_t k=frames/2;k<=input.size()-frames/2;++k)bins[k]=0;
    const auto filtered=transform(bins,true);std::vector<double> out(frames);
    for(int i=0;i<frames;++i){const auto x=filtered[size_t((i+1)*factor-1)];
        require(std::abs(x.imag())<1e-8,"ideal low-pass result is real");out[i]=x.real();}
    return out;
}
std::vector<double> renderNativeClock(const Case& c,int factor,int outputFrames) {
    FmVoicePatch patch;patch.modernTimbre=true;patch.algorithm=0;patch.fourOp=false;
    for(int i=0;i<2;++i){auto& p=patch.ops[i];p.wave=0;p.ar=15;p.dr=p.sr=p.sl=0;p.tl=uint8_t(i==0?c.tl:0);
        p.multi=uint8_t(i==0?c.modulator:c.carrier);p.ma35Multipliers=c.vm35;}
    FmVoice voice;voice.setSampleRate(rate);voice.setVolume(1);voice.noteOn(patch,rate*c.base/frames,1);
    std::vector<double> signal(size_t(outputFrames)*size_t(factor));std::array<float,32> sub{};
    for(int n=0;n<outputFrames;++n){const auto r=fm_probe::frame(voice,std::span<float>(sub.data(),size_t(factor)));
        require(r.reconstructedEndpoint==r.native,"aligned source endpoint mismatch");
        for(int j=0;j<factor;++j)signal[size_t(n*factor+j)]=sub[size_t(j)];}
    return signal;
}
std::vector<double> filteredEquation(const Case& c,const fm_causal::Config& config,int warmup,int padding=0) {
    const auto b=coefficients(index(c));std::vector<double> out(frames,0.);
    for(int n=-maxOrder;n<=maxOrder;++n) {
        const int bin=c.base*(c.carrier+n*c.modulator);
        const auto h=fm_causal::response(config,tau*bin/(frames*config.factor));
        const double gain=.7*coefficient(b,n);
        for(int i=0;i<frames;++i)out[i]+=gain*std::imag(h*std::polar(1.,tau*bin*double(i+1+warmup-padding)/frames));
    }
    return out;
}
double rms(const std::vector<double>& x){double sum=0;for(auto v:x){require(std::isfinite(v),"nonfinite sample");sum+=v*v;}return std::sqrt(sum/x.size());}
double error(const std::vector<double>& a,const std::vector<double>& b){require(a.size()==b.size(),"metric size mismatch");double sum=0;
    for(size_t i=0;i<a.size();++i){const double d=a[i]-b[i];require(std::isfinite(d),"nonfinite error");sum+=d*d;}return std::sqrt(sum/a.size());}
void selfTest() {
    std::vector<Complex> input={{1,2},{3,-4},{5,0},{-2,1},{1,1},{0,-2},{-1,2},{3,1}};
    const auto bins=transform(input,false),restored=transform(bins,true);
    for(size_t k=0;k<input.size();++k){Complex direct=0;for(size_t n=0;n<input.size();++n)direct+=input[n]*std::polar(1.,-tau*k*n/input.size());
        require(std::abs(bins[k]-direct)<1e-12&&std::abs(restored[k]-input[k])<1e-12,"FFT/direct DFT/inverse disagreement");}
    for(double x:{0.,1.,tau*.42,tau*4}){const auto b=coefficients(x);double power=b[0]*b[0];for(int n=1;n<=maxOrder;++n)power+=2*b[n]*b[n];
        require(std::abs(power-1)<1e-12,"Bessel coefficient energy is incomplete");
        for(int order:{0,1,2,7,31}){double numerical=0;constexpr int points=16384;
            for(int i=0;i<points;++i){const double theta=tau*(i+.5)/points;numerical+=std::cos(x*std::sin(theta)-order*theta)/points;}
            require(std::abs(numerical-b[order])<1e-12,"independent Bessel quadrature mismatch");}}
    auto b=coefficients(0);require(b[0]==1&&b[1]==0,"zero modulation coefficients");
    std::vector<double> pure(frames);for(int i=0;i<frames;++i)pure[i]=.7*std::sin(tau*34*(i+1)/frames);
    require(std::abs(rms(pure)-.7/std::sqrt(2.))<1e-12,"coherent RMS oracle mismatch");
    for(int factor:{2,4}){std::vector<double> higher(size_t(frames*factor));for(size_t i=0;i<higher.size();++i){
        higher[i]=.7*std::sin(tau*34*double(i+1)/higher.size())+.2*std::sin(tau*2300*double(i+1)/higher.size());}
        require(error(idealDecimate(higher,factor),pure)<1e-10,"ideal low-pass phase/alignment/gain mismatch");}
}
struct Design {fm_causal::Config config;int early,last;};
std::vector<Design> designs(int factor,bool aligned) {
    if(aligned){auto c=fm_causal::alignedConfig(factor);return {{c,c.taps.front(),c.taps.back()}};}
    std::vector<Design> out;
    for(auto sizes:std::vector<std::pair<int,int>>{{31,31},{63,63},{127,127},{31,255}})for(double f:{1.,.95})
        out.push_back({fm_causal::uniformConfig(factor,sizes.first,f,sizes.second),sizes.first,sizes.second});
    return out;
}
void alignmentFields(const Design& design,const fm_causal::Decimator& d) {
    std::cout<<",\"stage_taps\":[";bool first=true;for(int taps:design.config.taps){if(!first)std::cout<<',';first=false;std::cout<<taps;}
    std::cout<<"],\"common_delay_frames\":72,\"padding_frames\":"<<72-int(d.delayFrames())
        <<",\"pipeline_tail_bound_frames\":"<<d.supportBoundFrames()+72-int(d.delayFrames());
}
void causalReport(const Case& c,bool aligned=false) {
    constexpr int warmup=256;
    std::cout<<(aligned?",\"aligned_oversampling\":[":",\"causal_oversampling\":[");bool first=true;
    for(int factor:{2,4,8,16,32}) {
        const auto input=aligned?renderNativeClock(c,factor,warmup+frames):render(c,factor,warmup+frames);
        for(const auto& design:designs(factor,aligned)) {
            const auto& config=design.config;fm_causal::Decimator setup(config);
            const int padding=aligned?72-int(setup.delayFrames()):0;
            require(warmup>setup.supportBoundFrames()+padding,"insufficient causal warmup");
            auto all=fm_causal::stream(input,config);require(all.size()==size_t(warmup+frames),"causal frame count");
            if(padding){std::move_backward(all.begin(),all.end()-padding,all.end());std::fill_n(all.begin(),padding,0.);}
            std::vector<double> out(all.begin()+warmup,all.end());
            const auto exact=filteredEquation(c,config,warmup,padding);
            const auto band=reference(c,true,warmup-setup.delayFrames()-padding);
            const double equationError=error(out,exact);
            require(equationError<3e-6,"streaming FIR disagrees with independent sideband response");
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"factor\":"<<factor<<",\"early_taps\":"<<design.early<<",\"final_taps\":"<<design.last<<",\"stages\":"<<setup.stages()
                <<",\"last_nyquist_fraction\":"<<config.lastFraction<<",\"group_delay_frames\":"<<setup.delayFrames()<<",\"group_delay_ms\":"<<1000*setup.delayFrames()/rate
                <<",\"support_bound_frames\":"<<setup.supportBoundFrames()
                <<(aligned?",\"native_operator_ticks_per_output\":":",\"fm_operator_ticks_per_output\":")<<(aligned?2:2*factor)
                <<",\"fir_multiplies_per_output\":"<<setup.multipliesPerOutput()<<",\"rms\":"<<rms(out)
                <<",\"aligned_band_error_rms\":"<<error(out,band)<<",\"fir_equation_error_rms\":"<<equationError;
            if(aligned)alignmentFields(design,setup);std::cout<<'}';
        }
    }
    std::cout<<']';
}
void causalControls(bool aligned=false) {
    std::cout<<(aligned?"  \"aligned_warmup_frames\":256,\n  \"aligned_filter_controls\":[":"  \"causal_warmup_frames\":256,\n  \"causal_filter_controls\":[");bool first=true;
    for(int factor:{2,4,8,16,32})for(const auto& design:designs(factor,aligned)) {
        fm_causal::Decimator d(design.config);
        const std::vector<double> input(size_t((d.supportBoundFrames()+32)*factor),1.);
        const auto step=fm_causal::stream(input,design.config);
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"factor\":"<<factor<<",\"early_taps\":"<<design.early<<",\"final_taps\":"<<design.last<<",\"last_nyquist_fraction\":"<<design.config.lastFraction
            <<",\"group_delay_frames\":"<<d.delayFrames()<<",\"group_delay_ms\":"<<1000*d.delayFrames()/rate
            <<",\"support_bound_frames\":"<<d.supportBoundFrames()<<",\"fir_multiplies_per_output\":"<<d.multipliesPerOutput()
            <<",\"step_overshoot\":"<<std::max(0.,*std::max_element(step.begin(),step.end())-1)
            <<",\"step_undershoot\":"<<std::max(0.,-*std::min_element(step.begin(),step.end()))
            <<",\"step_final_error\":"<<std::abs(step.back()-1);
        for(int hz:{1000,20000,21800,24000}) {
            const auto gain=std::abs(fm_causal::response(design.config,tau*hz/(rate*factor)));
            std::cout<<",\"gain_"<<hz<<"hz_db\":"<<20*std::log10(std::max(gain,1e-300));
        }
        if(aligned)alignmentFields(design,d);std::cout<<'}';
    }
    std::cout<<"],\n";
}
void usage(std::ostream& out) {
    out<<"Usage: fm-sideband-audit [--self-test | --oversampling | --causal-oversampling | --aligned-oversampling]\n"
       <<"JSON for96 original steady sine-PM cases. Optional ideal or streaming FIR experiments; no device-latency claim.\n";
}
int run(bool experiments,bool causal=false,bool aligned=false) {
    selfTest();if(causal||aligned)fm_causal::selfTest();std::cout<<std::setprecision(12)<<"{\n  \"schema_version\":1,\n  \"sample_rate\":44100,\n  \"frames\":4096,\n"
        <<"  \"scope\":\"Steady two-sine phase modulation; no feedback, time-varying envelopes, handset, listening, device latency or power claim\",\n"
        <<"  \"reference\":\"Jacobi-Anger sidebands, orders -96..96; only abs(f)<output Nyquist retained; discarded power is not normalized\",\n"
        <<"  \"oversampling_scope\":\""<<((causal||aligned)?"Streaming FIR experiment; filter group delay and arithmetic counts, not full player latency":"Optional offline periodic ideal low-pass, not a causal player implementation")<<"\",\n";
    if(causal||aligned)causalControls(aligned);
    if(aligned)std::cout<<"  \"source_clock\":\"Native-frame EG/LFO/allocation updates with subframe sine evaluation\",\n";
    std::cout<<"  \"results\":[\n";
    bool first=true;
    for(bool vm35:{false,true})for(int base:{1,17,67,211})for(auto ratio:std::vector<std::pair<int,int>>{{2,3},{5,7},{7,9}})for(int tl:{0,12,24,36}) {
        Case c={vm35,base,ratio.first,ratio.second,tl};require(c.base*c.modulator<frames/2&&c.base*c.carrier<frames/2,"operator crosses Nyquist");
        const auto raw=render(c,1),full=reference(c,false),band=reference(c,true);
        require(error(raw,full)<3e-6,"public two-op voice disagrees with independent full-spectrum phase equation");
        if(!first)std::cout<<",\n";
        first=false;
        std::cout<<"    {\"profile\":\""<<(vm35?"vm35":"compatibility")<<"\",\"base_bin\":"<<base
            <<",\"carrier_multi\":"<<c.carrier<<",\"modulator_multi\":"<<c.modulator<<",\"modulator_tl\":"<<tl
            <<",\"modulation_index\":"<<index(c)<<",\"reference_rms\":"<<rms(band)<<",\"native_rms\":"<<rms(raw)
            <<",\"native_error_rms\":"<<error(raw,band)<<",\"full_equation_error_rms\":"<<error(raw,full);
        if(experiments&&base==211&&tl==0){std::cout<<",\"offline_oversampling\":[";bool firstFactor=true;
            for(int factor:{2,4,8,16,32}){const auto filtered=idealDecimate(render(c,factor),factor);if(!firstFactor)std::cout<<',';firstFactor=false;
                std::cout<<"{\"factor\":"<<factor<<",\"error_rms\":"<<error(filtered,band)<<",\"rms\":"<<rms(filtered)<<'}';}std::cout<<']';}
        if((causal||aligned)&&base==211&&(tl==0||tl==24))causalReport(c,aligned);
        std::cout<<'}';
    }
    std::cout<<"\n  ]\n}\n";std::cout.flush();require(bool(std::cout),"cannot write report");return 0;
}
} // namespace sideband
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--help"){sideband::usage(std::cout);return 0;}
    if(argc==2&&std::string(argv[1])=="--self-test"){sideband::selfTest();fm_causal::selfTest();std::cout<<"PASS: independent PM sidebands, quadrature, FFT, offline phase alignment and causal FIR controls\n";return 0;}
    if(argc>2||(argc==2&&std::string(argv[1])!="--oversampling"&&std::string(argv[1])!="--causal-oversampling"&&std::string(argv[1])!="--aligned-oversampling")){sideband::usage(std::cerr);return 2;}
    return sideband::run(argc==2&&std::string(argv[1])=="--oversampling",argc==2&&std::string(argv[1])=="--causal-oversampling",argc==2&&std::string(argv[1])=="--aligned-oversampling");
}catch(const std::exception& e){std::cerr<<"fm-sideband-audit: "<<e.what()<<'\n';return 1;}}
