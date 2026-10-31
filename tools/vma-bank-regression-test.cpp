// SPDX-License-Identifier: Apache-2.0
// Original VMA/HPS bank-selection fixtures; no songs, ROM or external DSP.
#include "smaf_file.h"
#include "smaf_voice.h"
#include "ma_player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;
using Bytes = std::vector<uint8_t>;

void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void append(Bytes& target, const Bytes& bytes) {
    target.insert(target.end(), bytes.begin(), bytes.end());
}
Bytes voice(int bank, int pc, int waveform) {
    // VMA algorithm0: inactive modulator, held sine/alternate carrier.
    return {0x43,3,0,uint8_t(bank),uint8_t(pc),0,1,
            0x14,0xf0,0,0xfc,0, 0x14,0xf0,0xf0,0x30,uint8_t(waveform)};
}
Bytes exclusive(const Bytes& payload) {
    Bytes result{0xff,0xf0,uint8_t(payload.size()+1)};
    append(result,payload);result.push_back(0xf7);return result;
}
Bytes command(int delay, int opcode, int value, int channel=0) {
    return {uint8_t(delay),0,uint8_t((channel<<6)|0x30|opcode),uint8_t(value)};
}
Bytes note(int delay, int pitch=0x21, int gate=12) {
    return {uint8_t(delay),uint8_t(pitch),uint8_t(gate)};
}
SmafFile score(const std::vector<Bytes>& voices, const Bytes& sequence, int format=0) {
    TrackChunk t;t.formatType=format;t.trackNumber=0;
    t.durationTimeBase=t.gateTimeBase=0x10;t.sequenceData=sequence;
    t.channelStatus.resize(format==0?2:format==3?32:16);
    for (const auto& v:voices) {
        auto data=exclusive(v);
        if(format!=0)data.erase(data.begin());
        append(t.setupData,data);
    }
    SmafFile f;f.tracks.push_back(t);return f;
}
struct Result { std::vector<float> pcm; MaPlayer::Diagnostics diagnostics; };
Result drain(MaPlayer& player,int block=257) {
    Result result;std::vector<float> buffer(size_t(block)*2);int count;
    while((count=player.render(buffer.data(),block))>0) {
        for(int i=0;i<2*count;++i)require(std::isfinite(buffer[size_t(i)]),"finite output");
        result.pcm.insert(result.pcm.end(),buffer.begin(),buffer.begin()+2*count);
        require(result.pcm.size()<8000*2*5,"short fixture terminates");
    }
    result.diagnostics=player.diagnostics();
    require(!result.diagnostics.tailLimitReached,"natural release completes");
    return result;
}
Result render(const SmafFile& f,int block=257,bool complete=true) {
    MaPlayer p;require(p.init(f,8000),"fixture initializes");
    require(p.scoreAudit().scanComplete==complete,"expected transport coverage");
    return drain(p,block);
}
Bytes selectedNote(int bank,int pc=0) {
    auto s=command(0,1,bank);append(s,command(0,0,pc));
    append(s,note(0));append(s,{20,0xff,0});return s;
}
void allBankIdentities() {
    for(int bank=0;bank<256;++bank)for(int pc:{0,17,127}) {
        auto bytes=voice(bank,pc,0);auto parsed=parseVoiceExclusive(bytes.data(),bytes.size());
        require(parsed.valid&&parsed.key.bankMSB==0&&parsed.key.bankLSB==bank&&
                parsed.key.pc==pc&&parsed.key.drumNote==0,"VMA retains full Bank/PC without inventing mapped drum keys");
    }
}
void selectBothBanks() {
    for(int low:{0,1,63,127})for(int pc:{0,17,127}) {
        auto a=voice(low,pc,0),b=voice(low|128,pc,6);
        for(bool high:{false,true}) {
            auto sequence=selectedNote(low|(high?128:0),pc);
            const auto expected=render(score({high?b:a},sequence));
            require(expected.diagnostics.fmNotes==1&&expected.diagnostics.fallbackNotes==0,
                    "selected custom VMA voice binds");
            require(std::any_of(expected.pcm.begin(),expected.pcm.end(),[](float x){return x!=0;}),"oracle is audible");
            for(const auto& voices:{std::vector<Bytes>{a,b},std::vector<Bytes>{b,a}}) {
                const auto actual=render(score(voices,sequence));
                require(actual.pcm==expected.pcm&&actual.diagnostics.fallbackNotes==0,
                        "bit7 selects distinct custom voice independently of setup order");
            }
            const auto missing=render(score({high?a:b},sequence));
            const auto fallback=render(score({},sequence));
            require(missing.pcm==fallback.pcm&&missing.diagnostics.fallbackNotes==1,
                    "wrong-bank custom tone cannot mask existing drum/melody fallback");
        }
        require(render(score({a},selectedNote(low,pc))).pcm!=
                render(score({b},selectedNote(low|128,pc))).pcm,"distinct authored timbres make bank selection observable");
    }
}
void liveSelectionAndSeek() {
    const auto a=voice(0,7,0),b=voice(128,7,6),programB=voice(0,8,6);
    auto actual=command(0,0,7),reference=actual;
    append(actual,note(0,0x21,20));append(reference,note(0,0x21,20));
    append(actual,command(5,1,128));append(reference,command(5,0,8));
    append(actual,note(20,0x24,20));append(reference,note(20,0x24,20));
    append(actual,command(5,1,0));append(reference,command(5,0,7));
    append(actual,note(20,0x27,20));append(reference,note(20,0x27,20));
    append(actual,{30,0xff,0});append(reference,{30,0xff,0});
    auto f=score({a,b},actual);const auto expected=render(score({a,programB},reference));
    const auto output=render(f);
    require(output.pcm==expected.pcm&&output.diagnostics.fmNotes==3&&output.diagnostics.fallbackNotes==0,
            "live Bank changes affect future notes like independent programs; selecting low bank clears bit7");
    require(render(f,1).pcm==expected.pcm&&render(f,1024).pcm==expected.pcm,"bank selection is block independent");
    MaPlayer player;require(player.init(f,8000),"seek fixture initializes");
    require(drain(player).pcm==expected.pcm,"first playback matches");player.seekToStart();
    require(drain(player,31).pcm==expected.pcm,"seek clears bank selection and replays authored changes");
    // An inline high-bank tone cannot replace the independent low-bank slot.
    Bytes sequence=command(0,0,7);append(sequence,note(0));
    sequence.push_back(20);append(sequence,exclusive(b));append(sequence,note(0,0x24));
    append(sequence,command(20,1,128));append(sequence,note(0,0x27));append(sequence,{20,0xff,0});
    Bytes control=command(0,0,7);append(control,note(0));append(control,note(20,0x24));
    append(control,command(20,0,8));append(control,note(0,0x27));append(control,{20,0xff,0});
    const auto inlineResult=render(score({a},sequence));
    require(inlineResult.pcm==render(score({a,programB},control)).pcm&&inlineResult.diagnostics.fallbackNotes==0,
            "inline VMA bank128 activation cannot shadow bank0; subsequent high-bank note uses it");
}
void modernValidationUnchanged() {
    for(int format:{2,3}) {
        const uint8_t cc=format==3?0x30:0xb0,on=format==3?0x10:0x90;
        auto a=voice(1,0,0),b=voice(129,0,6);
        Bytes valid{0,cc,32,1,0,on,60,100,12,20,0xff,0x2f,0};
        const auto expected=render(score({a,b},valid,format));
        Bytes bad{0,cc,32,1,0,cc,32,129,0,on,60,100,12,20,0xff,0x2f,0};
        auto f=score({a,b},bad,format);MaPlayer p;require(p.init(f,8000),"malformed modern bank keeps valid prefix");
        require(p.scoreAudit().totals[size_t(ScoreAudit::Status::Malformed)]==1,
                "Mobile/MA7 continue rejecting bit7-set controller data");
        const auto output=drain(p);
        require(output.pcm==expected.pcm&&output.diagnostics.fallbackNotes==0,
                "invalid modern bank cannot choose high-bit VMA tone or change valid low bank");
    }
}
int main() {
    allBankIdentities();selectBothBanks();liveSelectionAndSeek();modernValidationUnchanged();
    std::cout<<"VMA bank identity/selection/activation/seek and modern validation passed\n";
}
