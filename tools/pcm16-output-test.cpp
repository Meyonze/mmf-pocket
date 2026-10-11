// SPDX-License-Identifier: Apache-2.0
// Original numeric oracle: no recordings, vendor soundbank or Java runtime.
#include "../app/src/main/cpp/pcm16_output.h"
#include <bit>
#include <cfenv>
#include <cstdlib>
#include <iostream>
#include <limits>
void requirePcm16(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
int oraclePcm16(float sample) {
    const double x=std::clamp(double(sample),-1.0,1.0)*32767;
    // Independent distance comparison, not the implementation's lround.
    const double lower=std::floor(x),fraction=x-lower;
    return int(fraction<.5?lower:fraction>.5?lower+1:x<0?lower:lower+1);
}
void checkPcm16(float sample) {
    int16_t out=1234;requirePcm16(mmfpocket::audio::encodePcm16(sample,out),"finite sample accepted");
    requirePcm16(out==oraclePcm16(sample),"nearest integer oracle");
    const double scaled=std::clamp(double(sample),-1.0,1.0)*32767;
    requirePcm16(std::fabs(double(out)-scaled)<=.5,"at most half-LSB quantization error");
    const int old=int(std::clamp(sample,-1.0f,1.0f)*32767.0f);
    requirePcm16(std::abs(int(out)-old)<=1,"legacy change bounded to1LSB");
    int16_t neg=0;requirePcm16(mmfpocket::audio::encodePcm16(-sample,neg)&&int(neg)==-int(out),"odd symmetric output");
}
int main(){
    for(float x:{-100.0f,-1.0f,-.5f,-0.0f,0.0f,.5f,1.0f,100.0f})checkPcm16(x);
    for(int k=-32767;k<32767;++k){
        float x=float((double(k)+.5)/32767);
        checkPcm16(x);checkPcm16(std::nextafter(x,-std::numeric_limits<float>::infinity()));
        checkPcm16(std::nextafter(x,std::numeric_limits<float>::infinity()));
    }
    uint32_t bits=0x31415926;double newEnergy=0,oldEnergy=0;
    for(int i=0;i<100000;++i){
        bits=bits*1664525u+1013904223u;const float x=float(int32_t(bits))/2147483648.0f;
        checkPcm16(x);int16_t out;mmfpocket::audio::encodePcm16(x,out);
        const double exact=double(x)*32767,newError=out-exact;
        const double oldError=int(x*32767.0f)-exact;
        newEnergy+=newError*newError;oldEnergy+=oldError*oldError;
    }
    requirePcm16(newEnergy<oldEnergy*.3,"nearest integer reduces measured quantization MSE");
    for(float x:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}){
        int16_t out=1234;requirePcm16(!mmfpocket::audio::encodePcm16(x,out)&&out==1234,"reject nonfinite without invalid integer cast");
    }
    for(float x:{std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::max()})checkPcm16(x);
    const int original=std::fegetround();
    for(int mode:{FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO}){
        requirePcm16(std::fesetround(mode)==0,"set host rounding mode");
        for(float x:{-.5f,.5f,-.000123f,.000123f})checkPcm16(x);
    }
    requirePcm16(std::fesetround(original)==0,"restore host rounding mode");
    std::cout<<"PCM16 PASS: independent oracle, all half-step neighbors, deterministic rounding/silence/clip/nonfinite.\n"
             <<"Quantization RMS LSB: old="<<std::sqrt(oldEnergy/100000)<<" nearest="<<std::sqrt(newEnergy/100000)
             <<" improvement_dB="<<10*std::log10(oldEnergy/newEnergy)<<'\n';
}
