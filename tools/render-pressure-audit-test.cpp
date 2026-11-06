// SPDX-License-Identifier: Apache-2.0
#define RENDER_PRESSURE_AUDIT_NO_MAIN
#include "render-pressure-audit.cpp"
#include <sstream>
using namespace renderPressure;
void require(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
SmafFile fixture() {
    SmafFile file;TrackChunk track;track.formatType=2;track.durationTimeBase=track.gateTimeBase=0x10;
    const std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,0,0,0,0,0,0,0x79,0,
         0,0xf0,0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,0};
    track.setupData={0xf0,uint8_t(voice.size()+1)};
    track.setupData.insert(track.setupData.end(),voice.begin(),voice.end());track.setupData.push_back(0xf7);
    auto& s=track.sequenceData;s={0,0xb0,7,127,0,0xb0,11,127};
    for(int i=0;i<36;++i)s.insert(s.end(),{0,0x90,60,127,20});
    s.insert(s.end(),{11,0xc1,1,0,0x91,67,100,5,19,0xff,0x2f,0});
    file.tracks.push_back(track);return file;
}
void metricOracle() {
    Window w;w.add(1,-1);w.add(3,1);
    require(w.frames==2&&w.meanL==2&&w.meanR==0&&w.peak==3,"independent peak/DC oracle");
    require(std::abs(w.rms()-std::sqrt(3.))<1e-14&&w.acRms()==1,"independent RMS/AC-RMS oracle");
    Window quiet;const float d=std::ldexp(1.f,-20);quiet.add(.5f,.5f);quiet.add(.5f+d,.5f+d);
    require(quiet.acRms()==double(d)/2,"tiny AC on DC remains numerically measurable");
    bool rejected=false;try{w.add(NAN,0);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"nonfinite output is rejected");
}
void reportOracle() {
    const auto file=fixture();const auto reference=analyze(file,sampleRate*2,257);
    require(reference.finished&&!reference.tailLimit,"original fixture completes naturally");
    require(reference.total[0]==37&&reference.total[2]==1,"actual embedded/fallback notes counted");
    require(reference.total[3]==5&&reference.total[5]==5,"all held-voice steals counted");
    require(reference.total[11]>0,"dense original score actually activates limiter");
    require(reference.windows.size()>=3,"multiple time windows rendered");
    require(reference.windows[0].delta[0]==36&&reference.windows[0].delta[2]==0,"first window holds original simultaneous notes");
    require(reference.windows[1].delta[0]==1&&reference.windows[1].delta[2]==1,"110ms fallback stays in second window");
    Counters sum{};uint64_t frames=0;
    for(const auto& w:reference.windows){require(w.first==frames&&w.frames<=windowFrames,"windows partition rendered frames");frames+=w.frames;
        for(size_t i=0;i<sum.size();++i)sum[i]+=w.delta[i];}
    require(frames==reference.frames&&sum==reference.total,"window counters and frames sum exactly to totals");
    std::ostringstream expected;write(expected,reference);
    for(int block:{1,1024}){std::ostringstream actual;write(actual,analyze(file,sampleRate*2,block));
        require(actual.str()==expected.str(),"complete report is block invariant");}
    const auto capped=analyze(file,sampleRate/4);
    require(!capped.finished&&capped.frames==sampleRate/4,"cap is explicit without invented EOF");
    require(capped.windows.back().frames==sampleRate/4-2*windowFrames,"partial final window retains exact sample count");
    MaPlayer direct;require(direct.init(file,sampleRate),"manual render oracle initializes");
    std::array<float,2048> buffer{};uint64_t at=0;size_t index=0;Window measured;
    while(int got=direct.render(buffer.data(),1024))for(int i=0;i<got;++i){
        measured.add(buffer[2*i],buffer[2*i+1]);++at;
        if(measured.frames==windowFrames){const auto& w=reference.windows[index++];
            require(measured.peak==w.peak&&measured.rms()==w.rms()&&measured.acRms()==w.acRms(),"windows match independently partitioned actual float stream");
            measured=Window{};}}
    require(at==reference.frames&&counters(direct.diagnostics())==reference.total,"audit retains actual renderer endpoint and diagnostics");
}
void limiterSeverityOracle() {
    struct Case {int voices,sample,category;};
    for(const auto c:std::array<Case,5>{{{4,8192,0},{20,8192,1},{24,8192,2},{32,12288,3},{32,32767,4}}}) {
        SmafFile file;TrackChunk track;track.formatType=3;track.durationTimeBase=track.gateTimeBase=0x10;
        const std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,0,0,0,0,1,
            0x1f,0x40,0,0,0,0xf0,0xf0,0,0,0,0,0,0,0,80,1};
        track.setupData={0xf0,uint8_t(voice.size()+1)};track.setupData.insert(track.setupData.end(),voice.begin(),voice.end());track.setupData.push_back(0xf7);
        WaveData wave;wave.number=1;wave.adpcm=false;wave.bitsPerSample=16;wave.samplingRate=8000;
        for(int i=0;i<80;++i){wave.data.push_back(uint8_t(c.sample>>8));wave.data.push_back(uint8_t(c.sample));}track.waves.push_back(wave);
        track.sequenceData={0,0x30,7,127,0,0x30,11,127};
        for(int i=0;i<c.voices;++i)track.sequenceData.insert(track.sequenceData.end(),{0,0x10,60,127,100});
        track.sequenceData.insert(track.sequenceData.end(),{120,0xff,0x2f,0});file.tracks.push_back(track);
        // Independent steady-state DC gain: N * sample/32768 * mix gain *
        // the MA7 cosine/sine pan law. Warmup lets both output poles settle.
        const double prePeak=c.voices*(double(c.sample)/32768)*.32*std::sin(1.5707963267948966*64/127.);
        const double gain=std::min(1.,.92/prePeak);
        require((gain<1)==(c.category>=1)&&(gain<=std::pow(10.,-3./20))==(c.category>=2)
            &&(gain<=std::pow(10.,-6./20))==(c.category>=3)&&(gain<=std::pow(10.,-12./20))==(c.category>=4),"analytical steady gain spans distinct severity bands");
        MaPlayer player;require(player.init(file,8000),"limiter severity fixture initializes");std::array<float,2048> buffer{};
        require(player.render(buffer.data(),1024)==1024,"limiter DC fixture warms up");const auto before=player.diagnostics();
        require(player.render(buffer.data(),256)==256,"limiter DC measurement window renders");const auto after=player.diagnostics();
        require(after.limitedFrames-before.limitedFrames==uint64_t(c.category>=1?256:0),"steady limiter activation matches analytical gain");
        require(after.limited3DbFrames-before.limited3DbFrames==uint64_t(c.category>=2?256:0),"3dB threshold matches independent constant-source oracle");
        require(after.limited6DbFrames-before.limited6DbFrames==uint64_t(c.category>=3?256:0),"6dB threshold matches independent constant-source oracle");
        require(after.limited12DbFrames-before.limited12DbFrames==uint64_t(c.category>=4?256:0),"12dB threshold matches independent constant-source oracle");
        player.seekToStart();const auto reset=player.diagnostics();require(reset.limitedFrames==0&&reset.limited3DbFrames==0&&reset.limited6DbFrames==0&&reset.limited12DbFrames==0,"seek resets every severity count");
    }
}
int main(){limiterSeverityOracle();metricOracle();reportOracle();std::cout<<"Render-pressure audit PASS: metric oracles, dense limiter/fallback/steals, time windows, caps and block invariance\n";}
