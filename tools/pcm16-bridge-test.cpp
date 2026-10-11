// SPDX-License-Identifier: Apache-2.0
// Host-only actual bridge test with minimal JNI callbacks, not a Java/Android VM.
#include <jni.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>
#include "ma_fm_core.h"
#include "smaf_voice.h"
#include "score_audit.h"
#define private public
#include "ma_player.h"
#undef private
#ifndef MMF_PCM16_BRIDGE_FILE
#define MMF_PCM16_BRIDGE_FILE "../app/src/main/cpp/native_bridge.cpp"
#endif
#include MMF_PCM16_BRIDGE_FILE
namespace bridgeTest {
using Bytes=std::vector<uint8_t>;
void require(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
void append(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
Bytes chunk(const char* id,const Bytes& body,int last=-1){
    Bytes b(id,id+4);if(last>=0)b[3]=uint8_t(last);uint32_t n=uint32_t(body.size());
    b.insert(b.end(),{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)});append(b,body);return b;
}
Bytes fixture(int kind){
    Bytes body;
    if(kind==0){
        Bytes score{2,0,0x10,0x10};score.resize(20,0);
        append(score,chunk("Mtsq",{0,0x90,60,100,10,10,0xff,0x2f,0}));append(body,chunk("MTR0",score));
    }else{
        Bytes atr{0,0,uint8_t(kind==1?1:0x11),uint8_t(kind==1?0x10:0),0,0};
        append(atr,chunk("Atsq",{0,1,100}));Bytes pcm;
        for(int i=0;i<(kind==1?800:400);++i)pcm.push_back(kind==1?uint8_t(int8_t(30*std::sin(i*.2))):uint8_t((i*73+19)&255));
        append(atr,chunk("Awa0",pcm,1));append(body,chunk("ATR0",atr));
    }
    auto out=chunk("MMMD",body);out.insert(out.end(),{0,0});return out;
}
int oracle(float s){
    double x=std::clamp(double(s),-1.0,1.0)*32767,lo=std::floor(x),f=x-lo;
    return int(f<.5?lo:f>.5?lo+1:x<0?lo:lo+1);
}
jsize JNICALL length(JNIEnv*,jarray a){return jsize(reinterpret_cast<std::vector<jshort>*>(a)->size());}
int writes=0;
void JNICALL setShorts(JNIEnv*,jshortArray a,jsize start,jsize n,const jshort* in){
    auto& v=*reinterpret_cast<std::vector<jshort>*>(a);require(start>=0&&n>=0&&size_t(start+n)<=v.size(),"JNI write bounds");
    std::copy(in,in+n,v.begin()+start);++writes;
}
jboolean JNICALL exception(JNIEnv*){return JNI_FALSE;}
std::vector<int16_t> expected(const Bytes& bytes,bool phone){
    fxchain::smaf::MaPlayer p;require(initializePlayer(bytes.data(),bytes.size(),p).empty(),"reference renderer initializes");
    PhoneSpeakerFilter filter;float b[2048];int n;std::vector<int16_t> out;
    while((n=p.render(b,1024))>0){if(phone)filter.process(b,n);for(int i=0;i<n*2;++i)out.push_back(int16_t(oracle(b[i])));}
    return out;
}
void paths(const std::filesystem::path& folder){
    JNINativeInterface_ table{};table.GetArrayLength=length;table.SetShortArrayRegion=setShorts;table.ExceptionCheck=exception;
    JNIEnv env{};env.functions=&table;
    for(int kind=0;kind<3;++kind)for(bool phone:{false,true}){
        auto bytes=fixture(kind);const auto oraclePcm=expected(bytes,phone);
        const auto path=folder/("synthetic-"+std::to_string(kind)+(phone?"-phone.wav":"-normal.wav"));
        require(render(bytes.data(),bytes.size(),path.string().c_str(),phone).empty(),"actual WAV renderer succeeds");
        std::ifstream wav(path,std::ios::binary);Bytes saved{std::istreambuf_iterator<char>(wav),{}};
        require(saved.size()==44+oraclePcm.size()*2,"WAV header/data length exact");
        require(std::string(saved.begin(),saved.begin()+4)=="RIFF"&&std::string(saved.begin()+8,saved.begin()+12)=="WAVE","WAV header identity");
        uint32_t size=0;for(int i=0;i<4;++i)size|=uint32_t(saved[40+i])<<(i*8);
        require(size==oraclePcm.size()*2&&saved[22]==2&&saved[34]==16,"WAV length/stereo/bitdepth");
        std::vector<int16_t> actual;
        for(size_t i=44;i<saved.size();i+=2){uint16_t u=uint16_t(saved[i])|(uint16_t(saved[i+1])<<8);actual.push_back(std::bit_cast<int16_t>(u));}
        require(actual==oraclePcm,"WAV samples agree with independent nearest-PCM oracle");
        for(int capacity:{2,63,2048}){
            RenderSession session;require(initializePlayer(bytes.data(),bytes.size(),session.player).empty(),"actual session initializes");
            session.phoneSpeakerMode=phone;std::vector<jshort> target(size_t(capacity),-12345);std::vector<int16_t> streamed;
            while(true){
                std::fill(target.begin(),target.end(),-12345);
                int n=Java_app_mmfpocket_player_NativeMmfRenderer_renderSession(&env,nullptr,reinterpret_cast<jlong>(&session),reinterpret_cast<jshortArray>(&target));
                require(n>=0&&n*2<=capacity,"actual JNI session count bounded");if(n==0)break;
                require(std::all_of(target.begin()+n*2,target.end(),[](jshort x){return x==-12345;}),"JNI unused suffix untouched");
                streamed.insert(streamed.end(),target.begin(),target.begin()+n*2);
            }
            require(streamed==actual,"actual JNI1/31/1024-frame output exactly matches WAV/phone PCM bytes");
            require(Java_app_mmfpocket_player_NativeMmfRenderer_renderSession(&env,nullptr,reinterpret_cast<jlong>(&session),reinterpret_cast<jshortArray>(&target))==0,"actual JNI repeated EOF stable");
        }
    }
    RenderSession invalid;auto bytes=fixture(0);
    require(initializePlayer(bytes.data(),bytes.size(),invalid.player).empty(),"nonfinite test initializes");
    invalid.player.lp1L_=std::numeric_limits<float>::quiet_NaN();std::vector<jshort> target(2048,-12345);const int priorWrites=writes;
    require(Java_app_mmfpocket_player_NativeMmfRenderer_renderSession(&env,nullptr,reinterpret_cast<jlong>(&invalid),reinterpret_cast<jshortArray>(&target))==-1,"nonfinite session fails explicitly");
    require(writes==priorWrites&&std::all_of(target.begin(),target.end(),[](jshort x){return x==-12345;}),"nonfinite block never writes undefined integer output to Java");
    require(Java_app_mmfpocket_player_NativeMmfRenderer_renderSession(&env,nullptr,0,reinterpret_cast<jshortArray>(&target))==-1,"null session rejected");
}
Bytes quietFixture(int kind,int volume){
    Bytes body;
    if(kind<2){
        const int format=kind==0?2:3;
        Bytes score{uint8_t(format),0,0x10,0x10};score.resize(format==3?36:20,0);
        Bytes sequence{0,0xb0,7,uint8_t(volume)};
        if(kind==0)append(sequence,{0,0xb0,11,4});
        append(sequence,{0,0x90,60,100,10,10,0xff,0x2f,0});
        append(score,chunk("Mtsq",sequence));append(body,chunk("MTR0",score));
    }else{
        Bytes atr{0,0,uint8_t(kind==2?1:0x11),uint8_t(kind==2?0x10:0),0,0};
        append(atr,chunk("Atsq",{0,0,0x37,uint8_t(volume),0,1,100}));
        Bytes pcm;
        for(int i=0;i<(kind==2?800:400);++i)
            pcm.push_back(kind==2?uint8_t(int8_t(30*std::sin(i*.2))):uint8_t((i*73+19)&255));
        append(atr,chunk("Awa0",pcm,1));append(body,chunk("ATR0",atr));
    }
    auto out=chunk("MMMD",body);out.insert(out.end(),{0,0});return out;
}
void quietPaths(const std::filesystem::path& folder){
    JNINativeInterface_ table{};table.GetArrayLength=length;table.SetShortArrayRegion=setShorts;table.ExceptionCheck=exception;
    JNIEnv env{};env.functions=&table;
    size_t quietRepresentable=0,pcmSilent=0,normalRepresentable=0;
    for(int kind=0;kind<4;++kind)for(int volume:{0,1,2,3,4,6,16,127})for(bool phone:{false,true}){
        const auto bytes=quietFixture(kind,volume);const auto oraclePcm=expected(bytes,phone);
        require(!oraclePcm.empty(),"quiet fixture retains authored/source duration");
        const bool hasPcm=std::any_of(oraclePcm.begin(),oraclePcm.end(),[](int16_t s){return s!=0;});
        fxchain::smaf::MaPlayer referencePlayer;
        require(initializePlayer(bytes.data(),bytes.size(),referencePlayer).empty(),"quiet float reference initializes");
        PhoneSpeakerFilter filter;float b[2048],peak=0;int n;
        while((n=referencePlayer.render(b,1024))>0){
            if(phone)filter.process(b,n);
            for(int i=0;i<n*2;++i)peak=std::max(peak,std::fabs(b[i]));
        }
        if(!hasPcm)++pcmSilent;
        else if(peak<0.0001f)++quietRepresentable;
        else ++normalRepresentable;
        const auto path=folder/("quiet-"+std::to_string(kind)+"-"+std::to_string(volume)+(phone?"-phone.wav":"-normal.wav"));
        const auto error=render(bytes.data(),bytes.size(),path.string().c_str(),phone);
        if(hasPcm){
            require(error.empty(),"representable quiet PCM must not be rejected as silent WAV");
            std::ifstream wav(path,std::ios::binary);Bytes saved{std::istreambuf_iterator<char>(wav),{}};
            require(saved.size()==44+oraclePcm.size()*2,"quiet WAV exact duration, no gain/tail changes");
            uint32_t dataBytes=0;for(int i=0;i<4;++i)dataBytes|=uint32_t(saved[40+i])<<(8*i);
            require(dataBytes==oraclePcm.size()*2,"quiet WAV finalized length");
            for(size_t i=0;i<oraclePcm.size();++i){
                uint16_t u=uint16_t(saved[44+i*2])|(uint16_t(saved[45+i*2])<<8);
                require(std::bit_cast<int16_t>(u)==oraclePcm[i],"quiet WAV preserves independent PCM oracle without normalization");
            }
        }else{
            require(error=="音声データを生成できませんでした","all-zero quantized PCM still rejects as silent WAV");
        }
        for(int capacity:{2,63,2048}){
            RenderSession session;require(initializePlayer(bytes.data(),bytes.size(),session.player).empty(),"quiet session initializes");
            session.phoneSpeakerMode=phone;std::vector<jshort> target(size_t(capacity),-12345);std::vector<int16_t> streamed;
            while(true){
                std::fill(target.begin(),target.end(),-12345);
                n=Java_app_mmfpocket_player_NativeMmfRenderer_renderSession(&env,nullptr,reinterpret_cast<jlong>(&session),reinterpret_cast<jshortArray>(&target));
                require(n>=0&&n*2<=capacity,"quiet JNI count bounded");
                require(std::all_of(target.begin()+n*2,target.end(),[](jshort s){return s==-12345;}),"quiet JNI suffix untouched including EOF");
                if(n==0)break;
                streamed.insert(streamed.end(),target.begin(),target.begin()+n*2);
            }
            require(streamed==oraclePcm,"quiet JNI preserves exact oracle PCM including silence");
        }
        std::cout<<"quiet kind="<<kind<<" volume="<<volume<<" phone="<<phone<<" float_peak="<<peak
                 <<" pcm_nonzero="<<hasPcm<<" frames="<<oraclePcm.size()/2<<" wav="<<(hasPcm?"OK":"silent-rejected")<<'\n';
    }
    require(quietRepresentable>0&&pcmSilent>0&&normalRepresentable>0,"quiet regression covers representable sub-threshold, quantized silence and ordinary output");
    std::cout<<"Quiet bridge PASS: "<<quietRepresentable<<" sub-threshold nonzero, "<<pcmSilent<<" quantized silence, "
             <<normalRepresentable<<" ordinary cases; no normalization, WAV/JNI1/31/1024-frame oracle exact.\n";
}
enum class OutputFault { None, InitialHeader, FirstPayload, SecondPayload,
                         FinalSeek, FinalHeader, FinalFlush, Close };
class FaultBuffer final : public std::streambuf {
public:
    explicit FaultBuffer(OutputFault fault):fault_(fault){}
    std::vector<char> data;
    size_t payloadWrites=0,seekCalls=0,syncCalls=0;
protected:
    std::streamsize xsputn(const char* s,std::streamsize n) override {
        std::streamsize accepted=n;
        if(!rewriting_&&position_<44&&fault_==OutputFault::InitialHeader)
            accepted=std::min(n,std::streamsize(position_<16?16-position_:0));
        else if(!rewriting_&&position_>=44){
            ++payloadWrites;
            if((fault_==OutputFault::FirstPayload&&payloadWrites==1)||
               (fault_==OutputFault::SecondPayload&&payloadWrites==2))accepted=n-1;
        }else if(rewriting_&&fault_==OutputFault::FinalHeader)accepted=n-1;
        data.resize(std::max(data.size(),position_+size_t(accepted)));
        std::copy(s,s+accepted,data.begin()+position_);position_+=size_t(accepted);
        return accepted;
    }
    int_type overflow(int_type c) override {
        if(traits_type::eq_int_type(c,traits_type::eof()))return traits_type::not_eof(c);
        const char value=traits_type::to_char_type(c);
        return xsputn(&value,1)==1?c:traits_type::eof();
    }
    pos_type seekoff(off_type offset,std::ios_base::seekdir direction,std::ios_base::openmode mode) override {
        ++seekCalls;
        if(direction!=std::ios_base::beg||offset!=0||!(mode&std::ios_base::out)||fault_==OutputFault::FinalSeek)
            return pos_type(off_type(-1));
        position_=0;rewriting_=true;return pos_type(0);
    }
    int sync() override {++syncCalls;return fault_==OutputFault::FinalFlush?-1:0;}
private:
    OutputFault fault_;size_t position_=0;bool rewriting_=false;
};
class FaultOutput final : public std::ostream {
public:
    explicit FaultOutput(OutputFault fault):std::ostream(nullptr),buffer(fault),fault_(fault){rdbuf(&buffer);clear();}
    FaultBuffer buffer;bool closed=false;
    void close(){closed=true;if(fault_==OutputFault::Close)setstate(std::ios_base::badbit);}
private:
    OutputFault fault_;
};
void outputFaults(const std::filesystem::path& folder){
    const auto bytes=fixture(0);
    const size_t fullFrames=expected(bytes,false).size()/2;
    require(fullFrames>2048,"storage-fault fixture spans multiple PCM blocks");
    // Start with final-seek failure to independently demonstrate the original
    // writer's false success using its unchanged body extracted to an ostream.
    for(OutputFault fault:{OutputFault::FinalSeek,OutputFault::FinalHeader,OutputFault::FinalFlush,
                           OutputFault::Close,OutputFault::InitialHeader,OutputFault::FirstPayload,OutputFault::SecondPayload}){
        fxchain::smaf::MaPlayer player;require(initializePlayer(bytes.data(),bytes.size(),player).empty(),"fault player initializes");
        FaultOutput output(fault);
        const auto error=renderAndCloseWav(player,output,false);
        require(error=="一時音声ファイルの書き込みに失敗しました","output failure must never report WAV success");
        const uint64_t expectedFrames=fault==OutputFault::InitialHeader?0:
            fault==OutputFault::FirstPayload?1024:fault==OutputFault::SecondPayload?2048:fullFrames;
        require(player.cursor_==expectedFrames,"write failure aborts without rendering remaining song");
        if(fault==OutputFault::Close)require(output.closed,"close failure tested after actual close call");
        else require(!output.closed,"failed stream is never treated as successfully finalized file");
    }
    for(bool phone:{false,true}){
        fxchain::smaf::MaPlayer player;require(initializePlayer(bytes.data(),bytes.size(),player).empty(),"healthy output player initializes");
        FaultOutput output(OutputFault::None);
        require(renderAndCloseWav(player,output,phone).empty(),"healthy stream output succeeds");
        require(output.closed&&output.buffer.syncCalls==1&&output.buffer.seekCalls==1,"healthy WAV explicitly finalizes, flushes and closes once");
        const auto path=folder/(phone?"synthetic-0-phone.wav":"synthetic-0-normal.wav");
        require(render(bytes.data(),bytes.size(),path.string().c_str(),phone).empty(),"actual healthy file writer succeeds");
        std::ifstream input(path,std::ios::binary);std::vector<char> file{std::istreambuf_iterator<char>(input),{}};
        require(file==output.buffer.data,"successful stream and actual filesystem WAV bytes exact");
    }
    require(render(bytes.data(),bytes.size(),(folder/"missing-parent"/"not-created.wav").string().c_str(),false)=="一時音声ファイルを作成できません","actual uncreatable path fails explicitly");
    std::cout<<"Storage bridge PASS: initial header, first/second PCM block, final seek/header/flush/close faults; immediate abort; healthy output bytes exact. Host fault injection, not device/disk-full test.\n";
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;bridgeTest::paths(std::filesystem::path(argv[1]));
    bridgeTest::quietPaths(std::filesystem::path(argv[1]));
    bridgeTest::outputFaults(std::filesystem::path(argv[1]));
    std::cout<<"Actual bridge PASS: WAV/JNI PCM exact, FM/ATR PCM8/ADPCM, phone off/on,1/31/1024-frame blocks, odd capacity, EOF and nonfinite rejection. Host JNI callbacks only; no Android device/VM.\n";
    return 0;
}
