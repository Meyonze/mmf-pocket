// SPDX-License-Identifier: Apache-2.0
// Public player + authored VM5 setup/score versus a frozen steady PM/FIR/LP model.
// No private access, songs, ROM samples, gain normalization or automatic policy.
#include "ma_player.h"
#include "fm-causal-decimator.h"
#include <iomanip>
#include <iostream>
#include <string>
using namespace fxchain::smaf;
namespace player_sideband {
constexpr double tau=2*std::numbers::pi;
constexpr int rate=44100,frames=1024,warmup=512,order=96;
using Complex=std::complex<double>;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Case {int note,carrier,modulator,tl;};
SmafFile fixture(const Case& c){
 SmafFile f;TrackChunk t;t.formatType=2;t.durationTimeBase=t.gateTimeBase=0x10;t.channelStatus.assign(16,0x81);
 std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0};
 for(int j=0;j<2;++j)v.insert(v.end(),{0,0xa0,0xf0,uint8_t((j==0?c.tl:0)<<2),0,uint8_t((j==0?c.modulator:c.carrier)<<4),0});
 t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
 for(auto [cc,value]:std::array<std::pair<int,int>,6>{{{0,0},{32,0},{7,127},{11,127},{10,0},{64,0}}})
  t.sequenceData.insert(t.sequenceData.end(),{0,0xb0,uint8_t(cc),uint8_t(value)});
 t.sequenceData.insert(t.sequenceData.end(),{0,0xc0,0,0,0x90,uint8_t(c.note),127,0x82,0x2c,0x83,0x10,0xff,0x2f,0});
 f.tracks.push_back(std::move(t));return f;
}
std::vector<double> render(const Case& c,int factor,FmSubframeMath math){
 MaPlayer p;require(p.init(fixture(c),rate,false,{factor,math}),"fixture init");
 require(p.scoreAudit().scanComplete,"fixture score/setup scan incomplete");
 const int latency=factor?72:0;require(p.renderLatencySamples()==latency,"unexpected player latency");
 std::vector<float> all(size_t(warmup+latency+frames)*2);
 require(p.render(all.data(),warmup+latency+frames)==warmup+latency+frames,"steady window ended early");
 const auto& d=p.diagnostics();require(d.fmNotes==1&&d.fallbackNotes==0&&d.pcmNotes==0&&d.audioNotes==0&&d.limitedFrames==0,"fixture did not use one bound, unlimited FM source");
 std::vector<double> out(frames);
 for(int n=0;n<frames;++n){out[size_t(n)]=all[size_t(n+warmup+latency)*2];require(all[size_t(n+warmup+latency)*2+1]==0,"hard-left fixture leaked right");}
 return out;
}
struct Model {
 double index,carrier,modulator;
 std::array<double,order+1> bessel{};
 explicit Model(const Case& c){
  // Frozen current VM35 policy, not values read back from engine internals.
  const double base=440*std::pow(2.,double(c.note-69)/12)/rate;
  carrier=base*c.carrier;modulator=base*c.modulator;
  index=tau*4*std::pow(10.,-.75*c.tl/20);
  double energy=0;
  for(int k=0;k<=order;++k){bessel[size_t(k)]=std::cyl_bessel_j(double(k),index);energy+=bessel[size_t(k)]*bessel[size_t(k)]*(k?2:1);}
  require(std::abs(energy-1)<1e-12,"Bessel energy incomplete");
  const double first=order+1,ratio=index/(2*(first+1));
  const double tail=2*std::exp(first*std::log(index/2)-std::lgamma(first+1))/(1-ratio);
  require(ratio<1&&tail<1e-12,"Bessel absolute coefficient tail too large");
 }
 std::vector<double> reference(int factor,bool ideal) const{
  const double a=float(1-std::exp(-2*3.14159265358979*10000/rate));
  // Frozen legacy linear hard-left pan gain2 (MA7 cosine pan is separate).
  const double gain=.7*double(.32f)*2;std::vector<double> out(frames);
  const auto config=factor>1?fm_causal::alignedConfig(factor):fm_causal::Config{1,{}};
  const double delay=factor>1?fm_causal::Decimator(config).delayFrames():0;
  for(int k=-order;k<=order;++k){const double f=carrier+k*modulator;
   if(ideal&&std::abs(f)>=.5)continue;
   const double omega=tau*f;
   const auto onePole=a/(Complex(1)-std::polar(1-a,-omega));Complex h=onePole*onePole;
   if(!ideal&&factor>1){
    h*=fm_causal::response(config,omega/factor)*std::polar(1.,omega*delay);
   }
   const double coefficient=bessel[size_t(std::abs(k))]*(k<0&&(std::abs(k)&1)?-1:1);
   auto phase=std::polar(1.,omega*(warmup+1));const auto step=std::polar(1.,omega);
   for(int n=0;n<frames;++n){out[size_t(n)]+=gain*coefficient*std::imag(h*phase);phase*=step;}
  }
  return out;
 }
};
double rms(const std::vector<double>& a){double sum=0;for(double v:a){require(std::isfinite(v),"nonfinite sample");sum+=v*v;}return std::sqrt(sum/a.size());}
double error(const std::vector<double>& a,const std::vector<double>& b){require(a.size()==b.size(),"sample count differs");double sum=0;for(size_t i=0;i<a.size();++i){const double d=a[i]-b[i];require(std::isfinite(d),"nonfinite error");sum+=d*d;}return std::sqrt(sum/a.size());}
void caseReport(const Case& c,bool report){
 Model model(c);const auto ideal=model.reference(0,true);const auto native=render(c,0,FmSubframeMath::Exact);
 const double nativeRms=rms(native);require(nativeRms>0,"fixture must be audible");
 if(report)std::cout<<"{\"note\":"<<c.note<<",\"carrier_multi\":"<<c.carrier<<",\"modulator_multi\":"<<c.modulator<<",\"modulator_tl\":"<<c.tl<<",\"index\":"<<model.index<<",\"ideal_band_rms\":"<<rms(ideal)<<",\"policies\":[";
 bool first=true;
 for(int factor:{0,1,2,4,8,16,32}){
  std::vector<double> exact;const auto expected=model.reference(factor,false);
  for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite}){
   if(factor<2&&math==FmSubframeMath::Hermite)continue;
   auto output=factor==0?native:render(c,factor,math);
   if(factor==1)require(output==native,"native alignment changes original steady samples");
   if(math==FmSubframeMath::Exact)exact=output;
   const double mathDelta=error(output,exact),equationError=error(output,expected);
   require(mathDelta<1e-7&&equationError<4e-7,"public player disagrees with frozen PM/FIR/LP model");
   if(report){if(!first)std::cout<<',';first=false;
    std::cout<<"{\"factor\":"<<factor<<",\"math\":\""<<(math==FmSubframeMath::Exact?"exact":"hermite")<<"\",\"latency_frames\":"<<(factor?72:0)<<",\"output_rms\":"<<rms(output)<<",\"full_equation_error_rms\":"<<equationError<<",\"ideal_band_error_rms\":"<<error(output,ideal)<<",\"math_delta_rms\":"<<mathDelta<<",\"rms_change_db\":"<<20*std::log10(rms(output)/nativeRms)<<'}';
   }
  }
 }
 if(report)std::cout<<"]}";
}
int run(){std::cout<<std::setprecision(12)<<"{\"schema_version\":1,\"sample_rate\":44100,\"frames\":1024,\"warmup_frames\":512,\"scope\":\"Original steady VM35 sine2/no-feedback public player study; frozen software PM/FIR/LP reference, no hardware fidelity, automatic policy or gain normalization\",\"results\":[";
 bool first=true;for(int note:{60,72,84,96})for(auto [carrier,modulator]:std::array<std::pair<int,int>,2>{{{2,3},{5,7}}})for(int tl:{0,24,36}){if(!first)std::cout<<',';first=false;caseReport({note,carrier,modulator,tl},true);}
 std::cout<<"]}\n";std::cout.flush();require(bool(std::cout),"report write failed");return 0;
}
}
#ifndef FM_PLAYER_SIDEBAND_AUDIT_NO_MAIN
int main(int argc,char** argv){try{
 if(argc==2&&std::string(argv[1])=="--help"){std::cout<<"Usage: fm-player-sideband-audit [--self-test]\n";return 0;}
 if(argc==2&&std::string(argv[1])=="--self-test"){player_sideband::caseReport({84,5,7,0},false);player_sideband::caseReport({96,5,7,24},false);std::cout<<"PASS: public player, fixed setup/score/gain, native alignment and independent PM/FIR/LP model\n";return 0;}
 if(argc!=1){std::cerr<<"Usage: fm-player-sideband-audit [--self-test]\n";return 2;}
 return player_sideband::run();
}catch(const std::exception& e){std::cerr<<"fm-player-sideband-audit: "<<e.what()<<'\n';return 1;}}
#endif
