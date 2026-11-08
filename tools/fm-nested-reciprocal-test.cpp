// SPDX-License-Identifier: Apache-2.0
// Public nested source against the original division-based phase renderer.
#include "fm-recursive-nested-probe.h"
#include <bit>
#include <cfenv>
#include <iostream>
#include <string_view>
using namespace fxchain::smaf;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool equal(double a,double b){return a==b||(std::isnan(a)&&std::isnan(b));}
void referenceFrame(FmVoice& voice,float* output,int factor,FmSubframeMath math){
    const std::span<float> values(output,size_t(factor));
    if(math==FmSubframeMath::Hermite)fm_recursive::frame(voice,values);
    else fm_nested::frame<false,128>(voice,values);
}
int main(int argc,char** argv){try{
    const bool trace=argc==2&&std::string_view(argv[1])=="--trace";if(argc>1&&!trace)return 2;
    uint64_t hash=14695981039346656037ull;auto add=[&](uint64_t value){hash^=value;hash*=1099511628211ull;};
    void (*volatile reference)(FmVoice&,float*,int,FmSubframeMath)=referenceFrame;
    const int previous=std::fegetround();size_t cases=0,samples=0;
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        require(std::fesetround(mode)==0,"reciprocal rounding mode unavailable");
        for(int factor:{16,32,64,128}){
            const double minimum=std::numeric_limits<double>::min()*factor,maximum=std::numeric_limits<double>::max()/factor;
            std::vector<double> increments{0.,-0.,std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),
                std::numeric_limits<double>::min(),-std::numeric_limits<double>::min(),.013,-.013,.49,-.49,1e12,-1e12,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()};
            for(double bound:{minimum,maximum})for(double x:{std::nextafter(bound,0.),bound,std::nextafter(bound,std::numeric_limits<double>::infinity())}){
                increments.push_back(x);increments.push_back(-x);
            }
            for(bool ma7:{false,true})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})
            for(double increment:increments)for(double before:{0.,-0.,std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),.37,std::nextafter(1.,0.)}){
                FmVoicePatch patch;patch.fourOp=true;patch.algorithm=6;patch.modernTimbre=true;
                for(int j=0;j<4;++j){auto& op=patch.ops[j];op.ar=15;op.dr=op.sr=op.sl=0;op.multi=uint8_t(j+1);op.tl=uint8_t(j==3?0:12);op.ma35Multipliers=!ma7;op.ma7Multipliers=ma7;}
                FmVoice actual;actual.setSampleRate(44100);actual.noteOn(patch,317,.63f);actual.setVolume(.71f);
                for(auto& op:actual.ops_){op.phase_=before;op.phaseInc_=increment;op.cachedMute_=false;}
                auto expected=actual;std::array<float,128> a{},b{};
                require(actual.tickNestedSubframes(a.data(),factor,math),"valid reciprocal test source rejected");if(!trace)reference(expected,b.data(),factor,math);
                for(int n=0;n<factor;++n){
                    // Separate compiler contexts can canonicalize zero
                    // differently under downward rounding even in the old
                    // renderer. Frozen before/after traces also check signs.
                    const bool same=std::bit_cast<uint32_t>(a[n])==std::bit_cast<uint32_t>(b[n])||
                        (mode==FE_DOWNWARD&&a[n]==0&&b[n]==0)||(std::isnan(a[n])&&std::isnan(b[n]));
                    if(!trace&&!same)std::cerr<<"mode="<<mode<<" factor="<<factor<<" ma7="<<ma7<<" math="<<int(math)<<" increment="<<std::hexfloat<<increment<<" before="<<before<<" index="<<n<<" actual="<<a[n]<<" expected="<<b[n]<<std::defaultfloat<<"\n";
                    if(!trace)require(same,"reciprocal source differs from original division at an IEEE boundary");add(std::bit_cast<uint32_t>(a[n]));++samples;
                }
                if(!trace)require(actual.active_==expected.active_&&actual.released_==expected.released_&&actual.lfoPhase_==expected.lfoPhase_&&equal(actual.recentLevel_,expected.recentLevel_),"reciprocal native voice clocks differ");
                for(int j=0;j<4;++j){add(std::bit_cast<uint64_t>(actual.ops_[j].phase_));add(std::bit_cast<uint64_t>(actual.ops_[j].env_.level_));if(!trace)require(equal(actual.ops_[j].phase_,expected.ops_[j].phase_)&&actual.ops_[j].env_.level_==expected.ops_[j].env_.level_,"reciprocal native operator clocks differ");}
                ++cases;
            }
        }
    }
    require(std::fesetround(previous)==0,"reciprocal rounding mode restore failed");
    if(trace){std::cout<<cases<<" source trace cases,"<<samples<<" samples hash="<<hash<<"\n";return 0;}
    std::cout<<cases<<" reciprocal source cases,"<<samples<<" samples match original division (downward zero-sign compiler exception):4modes,VM35/MA7,Exact/Hermite,zero/subnormal/normal/large/nonfinite boundaries PASS hash="<<hash<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
