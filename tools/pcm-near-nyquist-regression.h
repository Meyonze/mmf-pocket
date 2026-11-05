// SPDX-License-Identifier: Apache-2.0
// Original near-native decimation signals and independent windowed-sinc oracle.
#pragma once
namespace pcm_near_nyquist_regression {
inline void run() {
    using namespace fxchain::smaf;
    constexpr double amplitude=12000.0/32768;
    size_t cases=0;
    for(double rate:{1.01,1.02,1.05,1.08})for(double frequency:{0.125,0.25,0.4,0.499})
    for(double offset:{0.0,0.25,0.5}) {
        std::vector<int16_t> pcm(12000);
        for(size_t i=0;i<pcm.size();++i)
            pcm[i]=int16_t(std::lround(12000*std::sin(2*pi*frequency*i)));
        double energy=0,targetEnergy=0;
        for(unsigned n=0;n<8192;++n) {
            const double position=128+offset+n*rate;const size_t index=size_t(position);
            const double actual=pcm_detail::interpolatePcm(pcm.data(),pcm.size(),index,
                float(position-index),false,0,0,true,rate);
            const double target=amplitude*std::sin(2*pi*frequency*position);
            require(std::isfinite(actual),"near-native reconstruction remains finite");
            energy+=actual*actual;targetEnergy+=target*target;
        }
        const double gain=std::sqrt(energy/targetEnergy);
        if(frequency==0.499) {
            const double limit=rate==1.01?0.60:rate==1.02?0.45:rate==1.05?0.10:0.13;
            require(gain<limit,"near-native above-Nyquist input has bounded folded output power");
        } else require(std::abs(gain-1)<0.02,"near-native passband amplitude stays within two percent");
        ++cases;
    }
    std::vector<int16_t> pcm(512);
    for(size_t i=0;i<pcm.size();++i)pcm[i]=int16_t(int((i*7919+317)%60001)-30000);
    for(double rate:{64.0/62,64.0/61})for(double phase:{0.0,0.125,0.5,0.875,1.0}) {
        const double actual=pcm_detail::interpolatePcm(pcm.data(),pcm.size(),128,
            float(phase),false,0,0,true,rate);
        const double expected=reference(pcm,128+phase,rate,64);
        require(std::abs(actual-expected)<3e-6,"compact near-native kernel matches independent64-tap FIR on exact rows");
    }
    for(double boundary:{1.0,1.02,1.05,1.1})for(float phase:{0.0f,0.25f,0.5f,1.0f}) {
        const float a=pcm_detail::interpolatePcm(pcm.data(),pcm.size(),128,phase,false,0,0,true,boundary-1e-7);
        const float b=pcm_detail::interpolatePcm(pcm.data(),pcm.size(),128,phase,false,0,0,true,boundary+1e-7);
        require(std::abs(a-b)<1e-5,"near-native kernel transitions are continuous");
    }
    std::cout<<"Near-native anti-alias PASS: "<<cases<<" sine/rate/phase cases; independent FIR and transition boundaries\n";
}
} // namespace pcm_near_nyquist_regression
