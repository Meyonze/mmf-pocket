// SPDX-License-Identifier: Apache-2.0
// Host-only listening prototype. Does not change the Android renderer.
#define RENDER_CONTRIBUTION_AUDIT_NO_MAIN
#include "render-contribution-audit.cpp"
#include <complex>
#include <filesystem>
#include <numbers>

namespace preview {
struct Biquad {
 double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
 void configure(double cutoff,double rate,double q) {
  const double k=std::tan(std::numbers::pi*cutoff/rate),n=1/(1+k/q+k*k);
  b0=k*k*n;b1=2*b0;b2=b0;a1=2*(k*k-1)*n;a2=(1-k/q+k*k)*n;
 }
 double tick(double x){const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
};
struct Channel {
 int sourceRate=0;
 Biquad left[2],right[2];
 void configure(int fs) {
  contribution::require(fs>=1500&&fs<=48000,"invalid source PCM rate");
  contribution::require(!sourceRate||sourceRate==fs,"preview requires one PCM rate per channel");
  if(sourceRate)return;sourceRate=fs;
  if(fs>=44100)return;
  constexpr double q[2]={.541196100146197,1.306562964876377};
  for(int i=0;i<2;++i){left[i].configure(fs*.5,44100,q[i]);right[i].configure(fs*.5,44100,q[i]);}
 }
 void filter(double& l,double& r){if(!sourceRate||sourceRate>=44100)return;
  for(int i=0;i<2;++i){l=left[i].tick(l);r=right[i].tick(r);}}
};
class Wave {
 std::ofstream stream;uint32_t frames=0;
 void word(uint32_t x,int n){for(int i=0;i<n;++i)stream.put(char((x>>(8*i))&255));}
 void header(){stream.seekp(0);stream.write("RIFF",4);word(36+frames*4,4);stream.write("WAVEfmt ",8);word(16,4);word(1,2);word(2,2);word(44100,4);word(176400,4);word(4,2);word(16,2);stream.write("data",4);word(frames*4,4);}
public:
 explicit Wave(const std::filesystem::path& path):stream(path,std::ios::binary){contribution::require(bool(stream),"cannot create preview");header();}
 void frame(double l,double r){for(double x:{l,r}){contribution::require(std::isfinite(x),"nonfinite preview");int y=int(std::round(std::clamp(x,-1.,1.)*32768));word(uint16_t(std::clamp(y,-32768,32767)),2);}++frames;}
 void close(){header();stream.flush();contribution::require(bool(stream),"preview write failed");}
};
struct Sink final:contribution::Observer {
 Wave original,filtered;std::map<contribution::Key,Channel> channels;
 double l=0,r=0,limiter=0;const double release=std::exp(-1./(.15*44100));
 explicit Sink(const std::filesystem::path& dir):original(dir/"A-current-r92.wav"),filtered(dir/"B-source-bandwidth-trial.wav"){}
 void note(const contribution::Key& key,const fxchain::smaf::ParsedVoice* voice)override{
  if(voice&&voice->valid&&voice->isPcm&&voice->pcm.rom&&voice->pcm.waveId>=0&&voice->pcm.waveId<22)channels[key].configure(voice->pcm.fs);
  else contribution::require(channels.find(key)==channels.end(),"preview requires stable source types per channel");
 }
 void part(const contribution::Key& key,double x,double y)override{auto i=channels.find(key);if(i!=channels.end())i->second.filter(x,y);l+=x;r+=y;}
 void frame(float x,float y,float fade)override{
  original.frame(x,y);limiter=std::max({std::abs(l),std::abs(r),limiter*release});
  const double gain=limiter>.92?.92/limiter:1;filtered.frame(l*gain*fade,r*gain*fade);l=r=0;
 }
};
void selfTest(){
 constexpr int count=8192;int cases=0;
 for(int fs:{1500,8000,16000,32000})for(double relative:{.1,.5,1.,1.5,2.}){
  const int bin=int(std::round(fs*.5*relative/44100*count));if(bin<1||bin>=count/2)continue;
  Channel c;c.configure(fs);std::complex<double> measured{};
  for(int n=0;n<count*2;++n){const double phase=2*std::numbers::pi*bin*n/count;double l=std::cos(phase),r=l;c.filter(l,r);
   contribution::require(l==r,"left/right prototype filters differ");
   if(n>=count)measured+=l*std::polar(1.,-phase);
  }
  const double ratio=std::tan(std::numbers::pi*bin/count)/std::tan(std::numbers::pi*(fs*.5)/44100);
  const double expected=1/std::sqrt(1+std::pow(ratio,8));
  contribution::require(std::abs(2*std::abs(measured)/count-expected)<1e-9,"prototype differs from independent Butterworth response");++cases;
 }
 Channel bypass;bypass.configure(48000);double l=.123,r=-.456;bypass.filter(l,r);
 contribution::require(l==.123&&r==-.456,"native bandwidth bypass changes samples");
 Channel state;state.configure(8000);l=r=1;state.filter(l,r);auto reference=state;state.configure(8000);
 for(int n=0;n<32;++n){double a=0,b=0,x=0,y=0;state.filter(a,b);reference.filter(x,y);contribution::require(a==x&&b==y,"same-rate note unexpectedly resets filter history");}
 bool rejected=false;try{state.configure(16000);}catch(...){rejected=true;}
 contribution::require(rejected,"mixed-rate channel must not silently use wrong filter");
 std::cout<<cases<<" independent filter responses, bypass and stable channel state PASS\n";
}
}
int main(int argc,char**argv){
 if(argc==2&&std::string_view(argv[1])=="--self-test"){try{preview::selfTest();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
 if(argc!=4){std::cerr<<"usage: percussion-bandwidth-preview <input.mmf> <new-output-directory> <seconds<=60>\n";return 2;}
 try{
  char* end=nullptr;const double seconds=std::strtod(argv[3],&end);
  contribution::require(end!=argv[3]&&!*end&&std::isfinite(seconds)&&seconds>0&&seconds<=60,"invalid preview duration");
  const std::filesystem::path dir=argv[2];contribution::require(std::filesystem::create_directory(dir),"output directory must be new");
  const auto bytes=renderPressure::read(argv[1]);fxchain::smaf::SmafFile file;
  contribution::require(file.parse(bytes.data(),bytes.size()),"invalid MMF");
  fxchain::smaf::MaPlayer player,reference;const fxchain::smaf::SineRenderOptions options{8,fxchain::smaf::FmSubframeMath::Hermite,true};
  contribution::require(player.init(file,44100,false,options)&&reference.init(file,44100,false,options),"player initialization failed");
  preview::Sink sink(dir);
  const auto report=contribution::analyzePlayers(player,reference,uint64_t(seconds*44100),512,&sink);
  sink.original.close();sink.filtered.close();std::ofstream out(dir/"reference-check.json");contribution::write(out,report);
  std::cout<<"Read-only bandwidth preview complete; A matches the r92 renderer, B is a listening experiment.\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
