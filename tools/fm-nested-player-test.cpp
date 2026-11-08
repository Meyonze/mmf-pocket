// SPDX-License-Identifier: Apache-2.0
// Explicit nested core/player route versus independent source and FIR oracles.
#include "fm-native-clock-probe.h"
#define RENDER_CONTRIBUTION_AUDIT_NO_MAIN
#include "render-contribution-audit.cpp"
#define FM_STRONG_NESTED_TEST_NO_MAIN
#include "fm-strong-nested-test.cpp"
#include "fm-recursive-nested-probe.h"
namespace nested_player {
using contribution::require;
SmafFile clockFile(){auto f=nested::fixture(nested::cases.back());f.tracks[0].sequenceData={0x83,0x10,0xff,0x2f,0};return f;}
void pcm(MaPlayer& p,int slot,int channel,int16_t value,bool atr=false,int track=-1) {
    static const std::array<int16_t,2> first{1000,1000},second{2000,2000};
    auto& v=p.pcmPool_[size_t(slot)];v=MaPlayer::PcmVoice{};v.pcm=value==1000?first.data():second.data();v.len=2;v.loop=true;v.loopEnd=2;v.active=true;v.channel=channel;v.keyNote=atr?-1:60;v.atrTrack=track;
    FmOpPatch flat;flat.ar=15;flat.dr=flat.sr=flat.sl=0;v.env.configure(flat,44100);v.env.keyOn();
}
std::vector<float> drain(MaPlayer& p,int block=257) {
    std::vector<float> out;std::vector<float> buffer(size_t(block)*2);int n;
    while((n=p.render(buffer.data(),block))>0){out.insert(out.end(),buffer.begin(),buffer.begin()+n*2);require(out.size()<44100*16,"nested player failed to reach EOF");}
    return out;
}
void buses() {
    using Stereo=sine_output::Stereo;
    for(int factor:{64,128}) {
        const auto h=strong_nested::denseKernel(factor);sine_output::Bus b(factor,true);
        require(b.tailBoundFrames()==144,"nested bus support length");
        std::vector<Stereo> input(size_t(factor),Stereo{});
        for(int n=0;n<400;++n) {
            std::fill(input.begin(),input.end(),Stereo{});
            if(n==0)input[0]={1,-.5};if(n==159)input.back()={1e-20,-.5e-20};
            const auto y=b.frame(input,n==3?Stereo{.25,-.125}:Stereo{});
            const int at=(n+1)*factor-1;double expected=at<int(h.size())?h[size_t(at)]:0;
            const int tiny=at-(160*factor-1);if(tiny>=0&&tiny<int(h.size()))expected+=1e-20*h[size_t(tiny)];
            require(std::abs(y.left-(expected+(n==75?.25:0)))<2e-14&&
                    std::abs(y.right-(-.5*expected+(n==75?-.125:0)))<2e-14,"nested bus impulse/tiny wake/native delay differs from dense FIR");
        }
        require(b.pendingFrames()==0,"nested finite FIR did not become idle");
        input[0]={1,1};(void)b.frame(input,{1,1});auto control=b;bool rejected=false;
        try{(void)b.frame(std::span<const Stereo>(input.data(),input.size()-1),{});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"nested bus accepted wrong block");auto x=b.frame(input,{}),y=control.frame(input,{});
        require(x.left==y.left&&x.right==y.right&&b.pendingFrames()==control.pendingFrames(),"nested bus rejection changed history");
        b.reset();std::fill(input.begin(),input.end(),Stereo{});
        for(int n=0;n<160;++n){auto zero=b.frame(input,{});require(zero.left==0&&zero.right==0&&b.pendingFrames()==0,"nested reset retained FIR/native history");}
    }
    std::cout<<"2 production high-density buses: dense impulse/tiny wake/native delay/drain/reset/rejection PASS\n";
}
void clocks() {
    int count=0;double maximum=0;
    for(int rate:{8000,44100})for(int factor:{16,32,64,128})for(bool ma7:{false,true})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite})for(int stopped=-1;stopped<4;++stopped) {
        auto patch=nested::patch({96,7,3,5,12,12,ma7});for(auto& op:patch.ops){op.am=op.vib=true;op.dam=op.dvb=2;op.holdEnabled=true;}
        if(stopped>=0){patch.ops[size_t(stopped)].ar=0;patch.ops[size_t(stopped)].rateLowBits=ma7?8:0;}
        FmVoice actual,oracle;for(auto* x:{&actual,&oracle}){x->setSampleRate(rate);x->noteOn(patch,317,.63f);}
        std::array<float,128> a{},b{};
        for(int n=0;n<320;++n) {
            for(auto* x:{&actual,&oracle}) {
                if(n==32)x->setPitch(rate*.71);if(n==64)x->setPitch(rate*.013);
                if(n==96)x->setVolume(0);if(n==128)x->setVolume(1e-12f);if(n==160)x->setVolume(.71f);
                if(n==192)x->noteOff(true);if(n==224)x->setHold(false);if(n==256)x->noteOn(patch,419,.51f);
            }
            require(actual.tickNestedSubframes(a.data(),factor,math),"nested core rejected coherent patch");
            const std::span<float> output(b.data(),size_t(factor));
            if(math==FmSubframeMath::Hermite)fm_recursive::frame(oracle,output);else fm_nested::frame<false,128>(oracle,output);
            for(int j=0;j<factor;++j){maximum=std::max(maximum,std::abs(double(a[size_t(j)])-b[size_t(j)]));require(std::bit_cast<uint32_t>(a[size_t(j)])==std::bit_cast<uint32_t>(b[size_t(j)]),"nested core differs from independent source prototype");}
            require(actual.active_==oracle.active_&&actual.released_==oracle.released_&&actual.recentLevel_==oracle.recentLevel_&&actual.lfoPhase_==oracle.lfoPhase_,"nested core changed native voice state");
            for(int j=0;j<4;++j)require(actual.ops_[j].phase_==oracle.ops_[j].phase_&&actual.ops_[j].env_.phase_==oracle.ops_[j].env_.phase_&&actual.ops_[j].env_.level_==oracle.ops_[j].env_.level_,"nested core changed operator clock");
        }++count;
    }
    for(int kind=0;kind<12;++kind) {
        auto patch=nested::patch({96,7,3,5,12,12});int factor=64;auto math=FmSubframeMath::Hermite;
        if(kind==0)patch.modernTimbre=false;if(kind==1)patch.ops[0].ma35Multipliers=false;if(kind==2)patch.algorithm=5;
        if(kind==3)patch.ops[1].wave=6;if(kind==4)patch.ops[2].fb=3;if(kind==5)patch.ops[1].fixedFrequency=true;
        if(kind==6)patch.ops[0].ma7Detune=1;if(kind==7)factor=8;if(kind==8)factor=256;if(kind==9)factor=0;
        if(kind==10)math=static_cast<FmSubframeMath>(99);
        FmVoice v;v.setSampleRate(44100);v.noteOn(patch,317,1);auto original=v;std::array<float,256> out;out.fill(123);
        require(!v.tickNestedSubframes(kind==11?nullptr:out.data(),factor,math)&&std::all_of(out.begin(),out.end(),[](float x){return x==123;})&&v.tick()==original.tick(),"nested core rejection changed output/state");
    }
    std::cout<<count<<" core clock/source cases bit-exact;12 atomic guards PASS; max delta "<<maximum<<'\n';
}
void spectra() {
    int count=0;double maximum=0;
    for(const nested::Case c:std::array<nested::Case,5>{{{96,7,3,5,12,12},{84,3,2,5,18,18},{96,3,2,5,0,0},{96,11,13,14,0,0,true},{127,2,2,2,0,0,true}}}) {
        const nested::Model model(c);
        for(int factor:{16,32,64,128})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite}) {
            MaPlayer p;require(p.init(nested::fixture(c),44100,false,{factor,math,false,true}),"nested public player init");
            std::vector<float> pcm(size_t(512+72+1024)*2);require(p.render(pcm.data(),512+72+1024)==512+72+1024,"nested steady window");
            std::vector<double> actual(1024);for(int n=0;n<1024;++n){actual[size_t(n)]=pcm[size_t(512+72+n)*2];require(pcm[size_t(512+72+n)*2+1]==0,"nested hard-left pan leaked");}
            const double e=player_sideband::error(actual,model.reference(factor));maximum=std::max(maximum,e);require(e<2e-6,"actual nested player differs from independent Bessel/FIR reference");
            require(p.renderLatencySamples()==72&&p.diagnostics().fallbackNotes==0&&p.diagnostics().fmNotes==1&&p.diagnostics().limitedFrames==0,"nested fixture routing/headroom");++count;
        }
    }
    std::cout<<count<<" actual-player independent spectral policies PASS; max RMS "<<maximum<<'\n';
}
void lifecycle() {
    int count=0;
    for(int factor:{16,32,64,128})for(auto math:{FmSubframeMath::Exact,FmSubframeMath::Hermite}) {
        auto file=nested::fixture({96,7,3,5,12,12});file.tracks[0].sequenceData.resize(27);
        file.tracks[0].sequenceData.insert(file.tracks[0].sequenceData.end(),{0,0x90,96,127,5,12,0xff,0x2f,0});MaPlayer p,reference;
        require(p.init(file,44100,false,{factor,math,false,true})&&reference.init(file,44100,false,{factor,math,false,true}),"nested lifecycle init");
        const auto out=drain(p,17);require(p.finished()&&!out.empty(),"nested natural EOF");p.seekToStart();require(drain(p,1024)==out&&drain(reference,1)==out,"nested block/seek history mismatch");
        p.seekToStart();require(!p.init(file,44100,false,{64,FmSubframeMath::Hermite}),"ordinary64 policy accepted");
        require(drain(p,31)==out,"invalid init destroyed existing nested policy/state");
        MaPlayer native;require(native.init(file,44100)&&p.init(file,44100)&&p.renderLatencySamples()==0&&drain(p,17)==drain(native,512),"reinit failed to restore ordinary renderer");
        for(int frames:{1,17,441}) {
            MaPlayer hard;require(hard.init(file,44100,false,{factor,math,false,true}),"nested hard cap init");hard.endSample_=uint64_t(frames);
            auto capped=drain(hard,31);require(capped.size()==size_t(frames+72)*2&&capped.back()==0&&capped[capped.size()-2]==0,"nested cap/fade/final-frame length");
        }
        MaPlayer a,b;require(a.init(clockFile(),44100,false,{factor,math,false,true})&&b.init(clockFile(),44100,false,{factor,math,false,true}),"nested mixed init");
        for(auto* x:{&a,&b}) {
            x->pool_[0].noteOn(nested::patch({96,7,3,5,12,12}),2093,.2f);x->pool_[0].channel=0;x->poolPanLeft_[0]=.7f;x->poolPanRight_[0]=.3f;
            pcm(*x,0,120,1000);pcm(*x,1,120,2000,true,7);
            x->events_.insert(x->events_.begin(),{{128,MaPlayer::Ev::Volume,0,0,0},{160,MaPlayer::Ev::Volume,0,127,0},{192,MaPlayer::Ev::AllSoundOff,0,0,0},{256,MaPlayer::Ev::AllSoundOff,120,0,0}});x->nonAtrGateEventEnd_=x->events_.size();
        }
        const auto report=contribution::analyzePlayers(a,b,768,17);const auto& fm=report.parts.at({0,-1,0});
        require(fm.sineFrames==192&&fm.densityFrames[size_t(std::countr_zero(unsigned(factor))-1)]==192&&report.sumErrorPeak<2e-5,"nested mixed counters/contribution sum");++count;
    }
    for(int factor:{0,1,8,256}){MaPlayer p;require(!p.init(nested::fixture(nested::cases.back()),44100,false,{factor,FmSubframeMath::Hermite,false,true}),"bad explicit nested policy accepted");}
    std::cout<<count<<" nested EOF/seek/block/hard-cap/mixed contribution cases PASS\n";
}
void nativeSources() {
    int count=0;
    for(int kind=0;kind<4;++kind) {
        SmafFile f;TrackChunk t;t.formatType=2;t.channelStatus.resize(16);t.durationTimeBase=t.gateTimeBase=0;
        if(kind==0) {
            t.sequenceData={0,0xb0,0,125,0,0xb0,7,127,0,0xb0,11,127,0,0x90,0,100,20,30,0xff,0x2f,0};
            WaveData w;w.number=1;w.samplingRate=8000;w.bitsPerSample=8;w.adpcm=false;
            for(int i=0;i<700;++i)w.data.push_back(uint8_t(128+24*std::sin(i*.17)));t.waves.push_back(w);
        }else if(kind==1) {
            t.isAudioTrack=true;t.formatType=0;t.durationTimeBase=t.gateTimeBase=0x13;t.sequenceData={0,1,100};
            WaveData w;w.number=1;w.samplingRate=8000;w.bitsPerSample=8;w.adpcm=false;w.signedPcm8=true;
            for(int i=0;i<600;++i)w.data.push_back(uint8_t(int8_t(20*std::sin(i*.11))));t.waves.push_back(w);
        }else if(kind==2) {
            t.formatType=3;t.channelStatus.resize(32);
            std::vector<uint8_t> v{0x43,0x79,7,0x7f,1,0,0,0,0,1,0x1f,0x40,0,0,0,0,0xf0,0,0,0,0,0,0,0,80,1};
            t.setupData={0xf0,uint8_t(v.size()+1)};t.setupData.insert(t.setupData.end(),v.begin(),v.end());t.setupData.push_back(0xf7);
            std::vector<uint8_t> w{0x43,0x79,7,0x7f,3,1,2};for(int i=0;i<80;++i)w.push_back(uint8_t(128+(i%8<4?16:-16)));
            t.setupData.push_back(0xf0);t.setupData.push_back(uint8_t(w.size()+1));t.setupData.insert(t.setupData.end(),w.begin(),w.end());t.setupData.push_back(0xf7);
            t.sequenceData={0,0x10,69,100,10,20,0xff,0};
        }else {
            f=nested::fixture({84,3,2,5,12,12});f.tracks[0].setupData[14]=7;
            f.tracks[0].sequenceData.resize(27);f.tracks[0].sequenceData.insert(f.tracks[0].sequenceData.end(),{0,0x90,84,127,5,12,0xff,0x2f,0});
        }
        if(kind!=3)f.tracks.push_back(t);
        MaPlayer native;if(!native.init(f,8000))throw std::runtime_error("native fallback fixture kind "+std::to_string(kind));const auto baseline=drain(native,31);
        for(int factor:{16,32,64,128}) {
            MaPlayer p;require(p.init(f,8000,false,{factor,FmSubframeMath::Hermite,false,true}),"nested fallback fixture init");const auto out=drain(p,17);
            require(out.size()==baseline.size()+144&&std::equal(baseline.begin(),baseline.end(),out.begin()+144)&&std::all_of(out.begin(),out.begin()+144,[](float x){return x==0;}),"native-only source changed beyond common72 delay");++count;
        }
    }
    std::cout<<count<<" recorded Audio/ATR/WT/unsupported-FM native fallback cases exact PASS\n";
}
}
int main(){try{nested_player::buses();nested_player::clocks();nested_player::spectra();nested_player::lifecycle();nested_player::nativeSources();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
