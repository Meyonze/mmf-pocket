// SPDX-License-Identifier: Apache-2.0
// Host-only rendered mix diagnostics. No score/timbre metadata is exported.
#include "ma_player.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace renderPressure {
using namespace fxchain::smaf;
constexpr uint32_t sampleRate=44100,windowFrames=4410;
using Counters=std::array<uint64_t,16>;
constexpr std::array<std::string_view,16> names={"fm_notes","pcm_notes","fallback_notes",
    "stolen_fm","stolen_pcm","stolen_held_fm","stolen_held_pcm","suppressed_hps_duplicates",
    "audio_notes","missing_audio","stolen_audio","limited_frames","suppressed_silent_notes",
    "limited_3db_frames","limited_6db_frames","limited_12db_frames"};
Counters counters(const MaPlayer::Diagnostics& d) {
    return {d.fmNotes,d.pcmNotes,d.fallbackNotes,d.stolenFm,d.stolenPcm,
        d.stolenHeldFm,d.stolenHeldPcm,d.suppressedHpsDuplicates,d.audioNotes,
        d.missingAudio,d.stolenAudio,d.limitedFrames,d.suppressedSilentNotes,
        d.limited3DbFrames,d.limited6DbFrames,d.limited12DbFrames};
}
struct Window {
    uint64_t first=0,frames=0;
    double peak=0,squares=0,meanL=0,meanR=0,m2L=0,m2R=0;
    Counters delta{};
    void add(float left,float right) {
        if(!std::isfinite(left)||!std::isfinite(right))throw std::runtime_error("nonfinite rendered sample");
        ++frames;
        peak=std::max({peak,std::abs(double(left)),std::abs(double(right))});
        squares+=double(left)*left+double(right)*right;
        const double dl=left-meanL,dr=right-meanR;
        meanL+=dl/double(frames);meanR+=dr/double(frames);
        m2L+=dl*(left-meanL);m2R+=dr*(right-meanR);
    }
    double rms() const {return frames?std::sqrt(squares/(2*double(frames))):0;}
    double acRms() const {return frames?std::sqrt(std::max(0.,(m2L+m2R)/(2*double(frames)))):0;}
};
struct Report {
    uint64_t frames=0;
    bool finished=false,tailLimit=false;
    Counters total{};
    std::vector<Window> windows;
};
Report analyze(const SmafFile& file,uint64_t cap,int block=1024) {
    if(cap==0||cap>uint64_t(sampleRate)*600||block<1||block>1024)throw std::runtime_error("invalid render bound");
    MaPlayer player;
    if(!player.init(file,sampleRate))throw std::runtime_error("no playable track");
    Report result;std::array<float,2048> buffer{};Counters previous{};Window window;
    const auto finishWindow=[&]{
        const auto current=counters(player.diagnostics());
        for(size_t i=0;i<current.size();++i)window.delta[i]=current[i]-previous[i];
        if(window.frames||current!=previous)result.windows.push_back(window);
        previous=current;window=Window{};window.first=result.frames;
    };
    while(result.frames<cap) {
        const int want=int(std::min({uint64_t(block),cap-result.frames,uint64_t(windowFrames)-window.frames}));
        const int got=player.render(buffer.data(),want);
        if(got<0||got>want)throw std::runtime_error("invalid rendered frame count");
        for(int i=0;i<got;++i)window.add(buffer[2*i],buffer[2*i+1]);
        result.frames+=uint64_t(got);
        if(window.frames==windowFrames)finishWindow();
        if(got==0)break;
    }
    finishWindow();result.finished=player.finished();result.tailLimit=player.diagnostics().tailLimitReached;
    result.total=counters(player.diagnostics());return result;
}
void writeCounters(std::ostream& out,const Counters& counts) {
    out<<'{';for(size_t i=0;i<counts.size();++i){if(i)out<<',';out<<'"'<<names[i]<<"\":"<<counts[i];}out<<'}';
}
void write(std::ostream& out,const Report& r) {
    out<<std::setprecision(12)<<std::boolalpha
       <<"{\n  \"schema_version\":1,\n  \"scope\":\"Rendered software mix; counters are not hardware polyphony or per-part loudness\",\n"
       <<"  \"sample_rate\":"<<sampleRate<<",\n  \"window_frames\":"<<windowFrames
       <<",\n  \"frames\":"<<r.frames<<",\n  \"finished\":"<<r.finished
       <<",\n  \"cap_reached\":"<<!r.finished<<",\n  \"tail_limit_reached\":"<<r.tailLimit<<",\n  \"totals\":";
    writeCounters(out,r.total);out<<",\n  \"windows\":[\n";
    for(size_t i=0;i<r.windows.size();++i){const auto& w=r.windows[i];if(i)out<<",\n";
        out<<"    {\"start_frame\":"<<w.first<<",\"frames\":"<<w.frames
           <<",\"start_seconds\":"<<double(w.first)/sampleRate
           <<",\"end_seconds\":"<<double(w.first+w.frames)/sampleRate
           <<",\"peak\":"<<w.peak<<",\"rms\":"<<w.rms()<<",\"ac_rms\":"<<w.acRms()
           <<",\"dc_left\":"<<w.meanL<<",\"dc_right\":"<<w.meanR<<",\"counters\":";
        writeCounters(out,w.delta);out<<'}';
    }
    out<<"\n  ]\n}\n";out.flush();if(!out)throw std::runtime_error("cannot write JSON report");
}
std::vector<uint8_t> read(const char* path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot open input");
    const auto size=f.tellg();
    if(size<12||size>16*1024*1024)throw std::runtime_error("input must be between 12 bytes and 16 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(bytes.data()),size)||f.peek()!=std::char_traits<char>::eof()||f.bad())
        throw std::runtime_error("cannot read complete unchanged input");
    return bytes;
}
} // namespace renderPressure
#ifndef RENDER_PRESSURE_AUDIT_NO_MAIN
int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--help") {
        std::cout<<"Usage: render-pressure-audit <input.mmf> [max-seconds]\n"
            <<"JSON on stdout: 100ms windows of rendered mix, limiter and allocation counters.\n"
            <<"Default/maximum 600 seconds at 44100Hz. Input is read only; no metadata exported.\n";return 0;
    }
    if(argc<2||argc>3){std::cerr<<"Usage: render-pressure-audit <input.mmf> [max-seconds]\n";return 2;}
    double seconds=600;
    if(argc==3){char* end=nullptr;errno=0;seconds=std::strtod(argv[2],&end);
        if(end==argv[2]||*end||errno==ERANGE||!std::isfinite(seconds)||seconds<1./renderPressure::sampleRate||seconds>600){
            std::cerr<<"max-seconds must be finite, at least one sample and at most 600\n";return 2;}}
    try {
        auto bytes=renderPressure::read(argv[1]);fxchain::smaf::SmafFile file;
        if(!file.parse(bytes.data(),bytes.size()))throw std::runtime_error("not a valid SMAF file");
        const auto report=renderPressure::analyze(file,uint64_t(seconds*renderPressure::sampleRate));
        renderPressure::write(std::cout,report);return 0;
    }catch(const std::exception& e){std::cerr<<"render-pressure-audit: "<<e.what()<<'\n';return 1;}
}
#endif
