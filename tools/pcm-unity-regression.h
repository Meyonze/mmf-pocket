// SPDX-License-Identifier: Apache-2.0
// Fractional source positions must not turn native-rate PCM into a low-pass.
#pragma once
namespace pcm_unity_regression {
inline void run() {
    using namespace fxchain::smaf;
    constexpr double amplitude=12000.0/32768.0;
    size_t checks=0;
    for(double frequency:{0.125,0.25,0.4}) {
        std::vector<int16_t> pcm(8192);
        for(size_t i=0;i<pcm.size();++i)
            pcm[i]=int16_t(std::lround(12000*std::sin(2*pi*frequency*i)));
        for(double phase:{0.0,0.25,0.5,0.75}) {
            MaPlayer::PcmVoice actual,legacy;
            actual.pcm=legacy.pcm=pcm.data();actual.len=legacy.len=pcm.size();
            actual.pos=legacy.pos=128+phase;actual.rate=legacy.rate=1;
            actual.active=legacy.active=actual.streamAudio=legacy.streamAudio=true;
            actual.qualityInterpolation=true;
            double energy=0,targetEnergy=0,error=0;
            for(unsigned n=0;n<2048;++n) {
                const double position=128+phase+n;
                const double target=amplitude*std::sin(2*pi*frequency*position);
                const size_t i=size_t(position);const float fraction=float(position-i);
                const float a=pcm[i]*(1.f/32768),b=pcm[i+1]*(1.f/32768);
                const float sample=actual.tick();
                require(legacy.tick()==a+(b-a)*fraction,"legacy native-rate PCM remains exactly linear");
                if(phase==0)require(sample==a,"integer native-rate PCM remains an exact source sample");
                energy+=sample*sample;targetEnergy+=target*target;error+=(sample-target)*(sample-target);
                ++checks;
            }
            require(std::abs(std::sqrt(energy/targetEnergy)-1)<0.002,
                    "fractional native-rate PCM retains analytic sine amplitude within 0.2 percent");
            require(std::sqrt(error/2048)<0.001,"fractional native-rate PCM matches analytic source phase");
            require(actual.pos==128+phase+2048,"fractional native-rate quality preserves the source clock");
        }
        for(float phase:{0.0f,0.25f,0.5f,1.0f})for(double boundary:{0.8,1.0,1.1}) {
            const float before=pcm_detail::interpolatePcm(pcm.data(),pcm.size(),128,phase,false,0,0,true,boundary-1e-7);
            const float after=pcm_detail::interpolatePcm(pcm.data(),pcm.size(),128,phase,false,0,0,true,boundary+1e-7);
            require(std::abs(before-after)<1e-5,"PCM quality transitions remain continuous around native rate");
        }
    }
    std::cout<<"Native-rate PCM fractional-delay PASS: "<<checks<<" source/legacy samples\n";
}
} // namespace pcm_unity_regression
