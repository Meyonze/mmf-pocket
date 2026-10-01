// SPDX-License-Identifier: Apache-2.0
// Synthetic fixtures only. No copyrighted ringtone data is embedded.
#include "ma_player.h"
#include "smaf_voice.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fxchain::smaf;

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
float advance(FmEnvelope& env, int count) {
    float value = 0;
    for (int i = 0; i < count; ++i) value = env.advance();
    return value;
}
SmafFile fixture(int format = 2) {
    SmafFile f;
    TrackChunk t;
    t.trackNumber = 0; t.formatType = format;
    t.durationTimeBase = t.gateTimeBase = 0x10;
    t.sequenceData = {0,0x90,60,100,10, 10,0xff,0x2f,0};
    f.tracks.push_back(t);
    return f;
}
float firstBlockPeak(const SmafFile& file) {
    MaPlayer p;
    check(p.init(file, 8000), "peak fixture initializes");
    float samples[512 * 2];
    int count = p.render(samples, 512);
    float peak = 0;
    for (int i = 0; i < count * 2; ++i) peak = std::max(peak, std::fabs(samples[i]));
    return peak;
}

std::vector<float> renderAll(const SmafFile& file, int block = 257) {
    MaPlayer player;
    check(player.init(file, 8000), "comparison fixture initializes");
    std::vector<float> result, buffer(block * 2);
    int n;
    while ((n = player.render(buffer.data(), block)) > 0) {
        result.insert(result.end(), buffer.begin(), buffer.begin() + n * 2);
        check(result.size() <= 8000 * 2 * 600, "comparison terminates");
    }
    check(player.finished(), "finished reflects natural or safety termination");
    return result;
}

void appendExclusive(SmafFile& file, const std::vector<uint8_t>& voice) {
    auto& setup = file.tracks[0].setupData;
    setup.push_back(0xf0);
    uint32_t length = uint32_t(voice.size() + 1);
    std::vector<uint8_t> vlq{uint8_t(length & 127)};
    while ((length >>= 7)) vlq.insert(vlq.begin(), uint8_t(0x80 | (length & 127)));
    setup.insert(setup.end(), vlq.begin(), vlq.end());
    setup.insert(setup.end(), voice.begin(), voice.end());
    setup.push_back(0xf7);
}
void setVoice(SmafFile& file, const std::vector<uint8_t>& voice) {
    file.tracks[0].setupData.clear();
    appendExclusive(file,voice);
}
std::vector<uint8_t> packMa3(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    for (size_t i=0;i<raw.size();) {
        size_t mask = out.size(); out.push_back(0);
        for (int b=6;b>=0 && i<raw.size();--b,++i) {
            out[mask] |= uint8_t((raw[i] >> 7) << b);
            out.push_back(raw[i] & 127);
        }
    }
    return out;
}

void addPcmFixture(SmafFile& file, int waveId = 1, uint8_t rr = 9) {
    // Synthetic looping PCM, 8 kHz, neutral envelope, RR=9, WaveID=1.
    setVoice(file, {0x43,0x79,0x07,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,uint8_t(rr<<4),0xf0,0,0,0,0,0,0,0,80,1,0,0,0});
    WaveData wave;
    wave.number = waveId; wave.adpcm = false; wave.bitsPerSample = 16;
    wave.samplingRate = 8000;
    for (int i = 0; i < 80; ++i) {
        uint16_t sample = uint16_t(int16_t(4000 * std::sin(i * 6.283185307179586 / 40)));
        wave.data.push_back(uint8_t(sample >> 8)); wave.data.push_back(uint8_t(sample));
    }
    file.tracks[0].waves.push_back(wave);
}

void checkPcmModulation() {
    for(int i=0;i<100000;++i) {
        const double phase=double(i)/100000;
        check(std::fabs(pcmModulationSine(phase)-std::sin(6.283185307179586*phase))<3e-7,
              "lightweight PCM LFO stays within sine interpolation error bound");
    }
    auto base = fixture(); addPcmFixture(base);
    base.tracks[0].sequenceData = {0,0xb0,7,127,0,0x90,60,100,100,100,0xff,0x2f,0};
    std::vector<uint8_t> image = {0x43,0x79,7,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,1};
    setVoice(base,image);
    const auto plain = renderAll(base);
    auto inactive=base;
    image[13]=0xc0; image[18]=0x66; setVoice(inactive,image);
    check(renderAll(inactive)==plain,"PCM LFO/depth inert when EAM/EVB disabled");
    std::vector<std::vector<float>> rates;
    for(int rate=0;rate<4;++rate) {
        image[13]=uint8_t(rate<<6); image[18]=0x77;
        auto parsed=parseVoiceExclusive(image.data(),image.size());
        check(parsed.valid && parsed.pcm.lfo==rate && parsed.pcm.env.am &&
              parsed.pcm.env.dam==3 && parsed.pcm.env.vib && parsed.pcm.env.dvb==3,
              "PCM LFO and modulation byte fields");
        auto direct=base;setVoice(direct,image);
        auto samples=renderAll(direct);rates.push_back(samples);
        check(samples!=plain,"authored PCM modulation changes output");
        check(samples==renderAll(direct,1) && samples==renderAll(direct,512),
              "PCM modulation sample clock independent of render chunks");
        for(float s:samples) check(std::isfinite(s),"modulated PCM is finite");
        std::vector<uint8_t> packed(image.begin(),image.begin()+10);packed[2]=6;
        auto payload=packMa3({image.begin()+10,image.end()});
        packed.insert(packed.end(),payload.begin(),payload.end());
        auto transport=base;setVoice(transport,packed);
        check(samples==renderAll(transport),"packed MA3 and MA5 PCM LFO agree");
        // Expanded MA7 WT inserts its rate-low byte after modulation byte8.
        std::vector<uint8_t> wt{0x43,0x79,8,0x7f,0x21,0,0,0,0,0,1};
        wt.insert(wt.end(),image.begin()+10,image.begin()+19);wt.push_back(0);
        wt.insert(wt.end(),image.begin()+19,image.end());
        parsed=parseVoiceExclusive(wt.data(),wt.size());
        check(parsed.valid && parsed.pcm.lfo==rate && parsed.pcm.env.am &&
              parsed.pcm.env.dvb==3,"MA7 WT modulation survives expanded field fold");
        // WT AM uses documented modern depths for both transport versions.
        auto expanded=base;setVoice(expanded,wt);
        // Supply instrument RAM rather than aliasing its ID to stream Mwa.
        std::vector<uint8_t> bulk{0x43,0x79,8,0x7f,0x23,1,2};
        auto quantized=direct;
        auto& reference=quantized.tracks[0].waves[0].data;
        for(size_t i=0;i<reference.size();i+=2) {
            int16_t value=int16_t((reference[i]<<8)|reference[i+1]);
            int sample=value/256;
            bulk.push_back(uint8_t(sample+128));
            uint16_t decoded=uint16_t(int16_t(sample*256));
            reference[i]=uint8_t(decoded>>8);reference[i+1]=uint8_t(decoded);
        }
        appendExclusive(expanded,bulk);
        check(renderAll(quantized)==renderAll(expanded),"MA7 and VM35 share PCM modulation interpretation");
        MaPlayer p;check(p.init(direct,8000),"modulation seek initializes");
        std::vector<float> replay,buf(512*2);int n;
        while((n=p.render(buf.data(),512))>0) {}
        p.seekToStart();while((n=p.render(buf.data(),512))>0)
            replay.insert(replay.end(),buf.begin(),buf.begin()+n*2);
        check(replay==samples,"PCM modulation phase resets on seek");
        check(p.init(base,8000),"plain PCM initializes after modulated PCM");
        replay.clear();while((n=p.render(buf.data(),512))>0)
            replay.insert(replay.end(),buf.begin(),buf.begin()+n*2);
        check(replay==plain,"fresh init clears PCM modulation in reused slots");
    }
    for(int i=0;i<4;++i) for(int j=0;j<i;++j)
        check(rates[i]!=rates[j],"four PCM LFO speeds are distinct");
    std::vector<float> lastAm, lastVib;
    for(int depth=0;depth<4;++depth) {
        image[13]=0x80; image[18]=uint8_t(0x10|(depth<<5));
        auto amOnly=base;setVoice(amOnly,image);
        // A constant source isolates gain modulation from phase modulation.
        auto& constant=amOnly.tracks[0].waves[0].data;
        for(size_t i=0;i<constant.size();i+=2) {constant[i]=0x10;constant[i+1]=0;}
        auto sound=renderAll(amOnly);
        if(depth) for(size_t i=1000;i<8000;++i)
            check(sound[i]<=lastAm[i],"each DAM step increases only attenuation");
        lastAm=sound;
        image[18]=uint8_t(1|(depth<<1));auto vibOnly=base;setVoice(vibOnly,image);
        sound=renderAll(vibOnly);
        check(sound!=plain && (depth==0 || sound!=lastVib),
              "EVB alone and each DVB depth change pitched PCM");
        lastVib=sound;
    }
    auto bent=base;image[18]=0x77;setVoice(bent,image);
    bent.tracks[0].sequenceData.insert(bent.tracks[0].sequenceData.begin()+9,{20,0xe0,0,96});
    check(renderAll(bent,13)==renderAll(bent,511),"live bend and vibrato do not accumulate by chunk");
}

void checkMa7ExpandedMetadata() {
    std::vector<uint8_t> image{0x43,0x79,8,0x7f,0x21,124,1,2,0,127,0,0,1,0};
    for(int i=0;i<2;++i) image.insert(image.end(),{0,0x85,0x64,0,0,0,0,0x0e,0xca,0x10});
    for(int bits=0;bits<16;++bits) {
        image[19]=uint8_t(0xf0|bits);image[29]=uint8_t(bits^15);
        auto v=parseVoiceExclusive(image.data(),image.size());
        check(v.valid && v.patch.ops[0].rateLowBits==bits &&
              v.patch.ops[1].rateLowBits==(bits^15),"MA7 FM operator rate bits are independent");
        check(v.patch.ops[0].ar==6 && v.patch.ops[0].dr==5 && v.patch.ops[0].sr==0 &&
              v.patch.ops[0].rr==8 && v.patch.ops[0].fixedBlock==3 &&
              v.patch.ops[0].fixedFnum==714 && !v.patch.ops[0].fixedFrequency,
              "MA7 extra FM fields preserve nibbles and retain dormant fixed register");
    }
    image[14]|=4;auto v=parseVoiceExclusive(image.data(),image.size());
    check(v.patch.ops[0].fixedFrequency && !v.patch.ops[1].fixedFrequency,
          "fixed-frequency flag is per operator, not guessed from nonzero FNUM");
    const std::vector<uint8_t> tail{0xff,0xbb,0x1f,0xf8,0x19,0x6d,0x16,0xc8,
        0x1e,0x1b,0x1f,0xf8,0x99,0x8f,0x83,0x80};
    image[10]=2;image.insert(image.end(),tail.begin(),tail.end());
    v=parseVoiceExclusive(image.data(),image.size());
    check(v.valid && v.al.present && v.al.resonance==31 && v.al.depth==5 &&
          v.al.mode==1 && v.al.reset && v.al.frequency==3 && v.al.xof && v.al.sus &&
          v.al.keyFollow && v.al.vsl && v.al.cutoff==std::array<uint16_t,5>{8184,6509,5832,7707,8184} &&
          v.al.rates==std::array<uint8_t,4>{25,15,3,0},"MA7 AL tail field isolation");
    auto without=image;without[10]=0;without.resize(34);
    auto a=fixture(),b=fixture();setVoice(a,image);setVoice(b,without);
    a.tracks[0].sequenceData.insert(a.tracks[0].sequenceData.begin(),{0,0xb0,0,124,0,0xb0,32,1,0,0xc0,2});
    b.tracks[0].sequenceData=a.tracks[0].sequenceData;
    check(renderAll(a)==renderAll(b),"retained AL/fixed metadata does not apply speculative DSP");
    for(size_t n=11;n<image.size();++n)
        check(!parseVoiceExclusive(image.data(),n).valid,"truncated expanded AL/FM layout rejected");
}

void fullVolume(SmafFile& file,int channel=0) {
    const bool ma7=file.tracks[0].formatType==3;
    const uint8_t cc=uint8_t(ma7?(0x30|(channel&15)|(channel>=16?128:0)):(0xb0|(channel&15)));
    file.tracks[0].sequenceData.insert(file.tracks[0].sequenceData.begin(),{0,cc,7,127});
}

void checkMa7ControllerCurve() {
    auto ma7=fixture(3), legacy=fixture(2);
    // The fixture's MIDI-looking status uses MA7 channel16; no channel status
    // is supplied, so controller mode must still be set for all32 channels.
    const float old=firstBlockPeak(legacy), now=firstBlockPeak(ma7);
    check(std::fabs(now/old-100.0f/127.0f)<1e-5,"MA7 default100 uses squared amplitude");
    auto explicitDefault=ma7;
    explicitDefault.tracks[0].sequenceData.insert(explicitDefault.tracks[0].sequenceData.begin(),{0,0xb0,7,100});
    check(renderAll(ma7)==renderAll(explicitDefault),"MA7 default100 equals explicit CC7=100");
    auto loud=ma7, quiet=ma7, expression=ma7;
    fullVolume(loud,16); fullVolume(quiet,16); fullVolume(expression,16);
    quiet.tracks[0].sequenceData.insert(quiet.tracks[0].sequenceData.begin()+4,{0,0xb0,7,50});
    expression.tracks[0].sequenceData.insert(expression.tracks[0].sequenceData.begin()+4,{0,0xb0,11,28});
    check(std::fabs(firstBlockPeak(quiet)/firstBlockPeak(loud)-std::pow(50.0f/127,2))<1e-5,
          "MA7 CC7 curve is squared, not arbitrary attenuation");
    check(std::fabs(firstBlockPeak(expression)/firstBlockPeak(loud)-std::pow(28.0f/127,2))<1e-5,
          "MA7 CC11 curve follows the authored fade");
    auto pcm=ma7; addPcmFixture(pcm);
    pcm.tracks[0].sequenceData={0,0xb0,7,127,0,0x90,60,100,20,5,0xb0,11,28,15,0xff,0x2f,0};
    auto midi=pcm; midi.tracks[0].formatType=2;
    // Use linear CC11=127 before the update; first50ms must match. After the
    // update MA7 applies one extra28/127, including to an already-held PCM.
    auto actual=renderAll(pcm), linear=renderAll(midi);
    check(actual.size()==linear.size(),"gain curve leaves PCM duration unchanged");
    for(size_t i=0;i<800;++i)check(actual[i]==linear[i],"MA7 full gain unchanged before CC11 update");
    // Output low-pass has memory: compare after it has settled, not at boundary.
    for(size_t i=1600;i<2000;++i)
        check(std::fabs(actual[i]-linear[i]*28.0f/127)<1e-6,"CC11 squared gain updates a held PCM note");
    MaPlayer player;check(player.init(pcm,8000),"MA7 curve seek initializes");
    std::vector<float> buffer(512*2), first, replay;int n;
    while((n=player.render(buffer.data(),512))>0)first.insert(first.end(),buffer.begin(),buffer.begin()+n*2);
    player.seekToStart();
    while((n=player.render(buffer.data(),512))>0)replay.insert(replay.end(),buffer.begin(),buffer.begin()+n*2);
    check(first==replay,"MA7 curve mode and default survive seek");
    check(player.init(ma7,8000),"MA7 default-only seek initializes");
    first.clear(); replay.clear();
    while((n=player.render(buffer.data(),512))>0)first.insert(first.end(),buffer.begin(),buffer.begin()+n*2);
    player.seekToStart();
    while((n=player.render(buffer.data(),512))>0)replay.insert(replay.end(),buffer.begin(),buffer.begin()+n*2);
    check(first==replay,"MA7 squared default survives seek without explicit CC7");
    check(player.init(legacy,8000),"legacy init after MA7");
    player.render(buffer.data(),512);
    float peak=0;for(float x:buffer)peak=std::max(peak,std::fabs(x));
    check(std::fabs(peak-old)<1e-7,"fresh legacy init clears MA7 curve mode");
}

void checkNoCareDrumFallback() {
    auto drum=fixture(), melodic=fixture();
    drum.tracks[0].sequenceData={0,0xb0,0,125,0,0xc0,2,0,0x90,90,100,20,40,0xff,0x2f,0};
    melodic.tracks[0].sequenceData=drum.tracks[0].sequenceData;
    melodic.tracks[0].sequenceData[3]=124;
    const auto percussion=renderAll(drum);
    auto marked=drum; marked.tracks[0].channelStatus={3};
    check(percussion==renderAll(marked),"bank125 no-care fallback is the same as explicit rhythm");
    check(percussion!=renderAll(melodic),"missing bank125 drum is not substituted with melodic piano");
    auto rom=drum;
    setVoice(rom,{0x43,0x79,7,0x7f,1,125,0,2,90,1,
        0x32,0xc8,0,0,8,0xf3,0xb1,16,0,0,0,13,249,21,219,0x86});
    check(renderAll(rom)!=percussion,"known ROM crash uses waveform identity, not the mapped high-key hat");
    auto quietRom=rom; quietRom.tracks[0].setupData[19]=uint8_t(40<<2);
    check(renderAll(rom)==renderAll(quietRom),"replacement does not transfer uncalibrated source PCM TL");
}

void checkModernTimbres() {
    auto synth=[](FmVoicePatch patch) {
        FmVoice v; v.setSampleRate(44100); v.noteOn(patch,261.625565,0.5f);
        std::vector<float> out(8192);
        for(auto& x:out) { x=v.tick(); check(std::isfinite(x),"modern FM remains finite"); }
        return out;
    };
    auto legacy=FmVoicePatch::defaultPatch(), modern=legacy;
    modern.modernTimbre=true;
    check(synth(legacy)==synth(modern),"FB0/no AM modern profile retains original math");
    for(int fb=1;fb<=7;++fb) {
        legacy.ops[0].fb=uint8_t(fb); modern=legacy; modern.modernTimbre=true;
        check((synth(legacy)==synth(modern))==(fb==3),
              "feedback3 coincides; other levels use distinct trial mapping");
    }
    for(int dam=0;dam<4;++dam) {
        legacy.ops[0].fb=0; legacy.ops[1].am=true; legacy.ops[1].dam=uint8_t(dam);
        modern=legacy; modern.modernTimbre=true;
        check(synth(legacy)!=synth(modern),"modern tremolo uses documented endpoint depth");
    }
    check(FmVoicePatch::romApprox(-1)==nullptr && FmVoicePatch::romApprox(29)==nullptr,
          "unclassified ROM IDs are not guessed");
    for(int id=0;id<29;++id) {
        auto* p=FmVoicePatch::romApprox(id);
        check(p && p->modernTimbre,"ROM identity has original modern trial patch");
        auto sound=synth(*p); float peak=0;
        for(float s:sound) peak=std::max(peak,std::fabs(s));
        check(peak>0.00001f,"every ROM replacement is audible");
    }
    check(synth(*FmVoicePatch::romApprox(6))!=synth(*FmVoicePatch::romApprox(3)),
          "crash and closed hi-hat no longer share one voice");
    check(synth(*FmVoicePatch::romApprox(22))!=synth(FmVoicePatch::gmModernApprox(19)),
          "piano ROM wave is not program19 organ");
    check(synth(*FmVoicePatch::romApprox(26))!=synth(FmVoicePatch::gmModernApprox(11)),
          "string ROM wave is not program11 vibraphone");
    auto rom=fixture();
    rom.tracks[0].sequenceData={0,0xb0,0,125,0,0xc0,2,0,0x90,90,100,20,40,0xff,0x2f,0};
    std::vector<uint8_t> voice={0x43,0x79,7,0x7f,1,125,0,2,90,1,
        0x32,0xc8,0,0,8,0xf3,0xb1,16,0,0,0,13,249,21,219,0x86};
    setVoice(rom,voice);
    auto mapped=rom; voice[8]=17; setVoice(mapped,voice); mapped.tracks[0].sequenceData[9]=17;
    check(renderAll(rom)==renderAll(mapped),"ROM drum sounding pitch does not follow mapped key address");
    check(renderAll(rom,1)==renderAll(rom,257) && renderAll(rom,1024)==renderAll(rom),
          "modern ROM routing remains block independent");
    MaPlayer player; check(player.init(rom,8000),"modern seek fixture initializes");
    float a[2048],b[2048]; const int n=player.render(a,1024);
    player.seekToStart(); check(player.render(b,1024)==n && std::equal(a,a+n*2,b),
          "seek preserves modern profile and ROM base tuning");
    auto bend=rom;
    bend.tracks[0].sequenceData={0,0xb0,0,125,0,0xc0,2,0,0xe0,0,80,0,0x90,90,100,20,
        5,0xe0,0,48,35,0xff,0x2f,0};
    check(renderAll(bend,1)==renderAll(bend),"ROM substitute pitch bend remains block independent");
}

void checkMa7PitchRange() {
    auto near = [](const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size()!=b.size()) return false;
        for(size_t i=0;i<a.size();++i) if(std::fabs(a[i]-b[i])>1e-5f) return false;
        return true;
    };
    auto make = [](int ch, int range, int bendMsb, int note, bool pcm) {
        auto f=fixture(3); if(pcm)addPcmFixture(f);
        const uint8_t bank=ch>=16?128:0;
        const uint8_t cc=uint8_t(bank|0x30|(ch&15));
        const uint8_t pb=uint8_t(bank|0x60|(ch&15));
        const uint8_t on=uint8_t(bank|0x10|(ch&15));
        f.tracks[0].sequenceData={0,cc,7,127,0,cc,15,uint8_t(range),
            0,pb,0,uint8_t(bendMsb),0,on,uint8_t(note),100,40,40,0xff,0x2f,0};
        return f;
    };
    for(bool pcm:{false,true}) for(int ch:{0,16,31}) {
        auto up=make(ch,24,96,60,pcm), octave=make(ch,2,64,72,pcm);
        check(near(renderAll(up),renderAll(octave)),"MA7 +/-24 range gives +12 at half bend, both channel banks / FM and WT");
        check(near(renderAll(make(ch,24,32,60,pcm)),renderAll(make(ch,2,64,48,pcm))),
              "MA7 negative half bend gives -12");
        check(renderAll(make(ch,0,96,60,pcm))==renderAll(make(ch,0,64,60,pcm)),"zero range disables bend");
        for(int invalid:{25,127})
            check(renderAll(make(ch,invalid,96,60,pcm))==renderAll(make(ch,2,96,60,pcm)),
                  "out-of-range MA7 sensitivity is ignored");
        auto defaultRange=make(ch,2,96,60,pcm);
        defaultRange.tracks[0].sequenceData.erase(defaultRange.tracks[0].sequenceData.begin()+4,
                                                defaultRange.tracks[0].sequenceData.begin()+8);
        check(renderAll(defaultRange)==renderAll(make(ch,2,96,60,pcm)),"missing CC15 retains two-semitone default");
    }
    // Sensitivity can change after an initial bend and during a held note.
    auto held=make(0,2,96,60,true);
    held.tracks[0].sequenceData.insert(held.tracks[0].sequenceData.begin()+17,{10,0x30,15,24});
    held.tracks[0].sequenceData[21]=30; // total score length unchanged
    auto old=renderAll(make(0,2,96,60,true)), changed=renderAll(held);
    check(changed.size()==old.size(),"live range update preserves gates/duration");
    check(std::equal(old.begin(),old.begin()+1600,changed.begin()),"live range update does not change earlier audio");
    check(changed!=old,"live range updates a held WT voice");
    auto first=changed;
    MaPlayer player; check(player.init(held,8000),"range seek initializes");
    float buffer[1024]; while(player.render(buffer,512)>0){}
    player.seekToStart(); std::vector<float> replay; int n;
    while((n=player.render(buffer,512))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(first==replay,"seek clears live bend and restores default sensitivity before replay");
    check(renderAll(held,1)==renderAll(held,1024),"range update is render-block invariant");
    auto legacy=fixture(); addPcmFixture(legacy);
    legacy.tracks[0].sequenceData={0,0xb0,15,24,0,0xe0,0,96,0,0x90,60,100,40,40,0xff,0x2f,0};
    auto legacyDefault=legacy; legacyDefault.tracks[0].sequenceData[3]=2;
    check(renderAll(legacy)==renderAll(legacyDefault),"MA3/5 CC15 meaning is unchanged");
    check(player.init(legacy,8000),"legacy fresh init after wide MA7 range");
    replay.clear(); while((n=player.render(buffer,512))>0)replay.insert(replay.end(),buffer,buffer+n*2);
    check(replay==renderAll(legacy),"fresh legacy init clears MA7 range state");
    auto isolated=make(0,2,96,60,true);
    isolated.tracks[0].sequenceData.insert(isolated.tracks[0].sequenceData.begin(),{0,0xbf,15,24});
    check(renderAll(isolated)==renderAll(make(0,2,96,60,true)),"sensitivity belongs to one channel, not a global range");
    auto heldFm=held;
    heldFm.tracks[0].setupData.clear(); heldFm.tracks[0].waves.clear();
    auto oldFm=renderAll(make(0,2,96,60,false)), changedFm=renderAll(heldFm);
    check(std::equal(oldFm.begin(),oldFm.begin()+1600,changedFm.begin()) && changedFm!=oldFm,
          "live sensitivity updates a held FM voice at the event boundary");
}

void checkMobileRpn() {
    auto make=[](bool ma7,int range,const std::vector<uint8_t>& extra={}) {
        auto f=fixture(ma7?3:2);
        std::vector<uint8_t> q={0,uint8_t(ma7?0x30:0xb0),7,127};
        if(ma7) q.insert(q.end(),{0,0x30,15,uint8_t(range)});
        else q.insert(q.end(),{0,0xb0,101,0,0,0xb0,100,0,0,0xb0,6,uint8_t(range)});
        q.insert(q.end(),extra.begin(),extra.end());
        q.insert(q.end(),{0,uint8_t(ma7?0x60:0xe0),0,96,
            0,uint8_t(ma7?0x10:0x90),60,100,20,40,0xff,0x2f,0});
        f.tracks[0].sequenceData=q; return f;
    };
    auto near=[](const std::vector<float>& a,const std::vector<float>& b) {
        if(a.size()!=b.size()) return false;
        for(size_t i=0;i<a.size();++i) if(std::fabs(a[i]-b[i])>1e-6f) return false;
        return true;
    };
    for(int range:{0,1,2,4,8,24}) {
        const auto a=renderAll(make(false,range));
        check(near(a,renderAll(make(true,range))),"Mobile RPN and MA7 CC15 select equivalent bend ranges");
        check(a==renderAll(make(false,range),1),"RPN range conversion is block independent");
    }
    auto wt=make(false,8),wtReference=make(true,8);
    addPcmFixture(wt); addPcmFixture(wtReference);
    check(near(renderAll(wt),renderAll(wtReference)),"Mobile RPN also adjusts authored WT playback rate");
    auto held=make(false,2);
    auto& heldSeq=held.tracks[0].sequenceData;
    heldSeq.erase(heldSeq.end()-4,heldSeq.end());
    heldSeq.insert(heldSeq.end(),{5,0xb0,6,8,35,0xff,0x2f,0});
    check(renderAll(held,1)==renderAll(held),"held FM sensitivity update is block independent");
    addPcmFixture(held);
    check(renderAll(held,1)==renderAll(held),"held WT sensitivity update is block independent");
    auto normal=make(false,2);
    auto null=make(false,2,{0,0xb0,101,127,0,0xb0,100,127,0,0xb0,6,8});
    check(renderAll(normal)==renderAll(null),"null RPN does not change bend range");
    auto other=make(false,2,{0,0xb0,100,1,0,0xb0,6,8});
    check(renderAll(normal)==renderAll(other),"other RPN does not change bend range");
    auto nrpn=make(false,2,{0,0xb0,99,0,0,0xb0,98,0,0,0xb0,6,8});
    check(renderAll(normal)==renderAll(nrpn),"NRPN cancels RPN Data Entry targeting");
    auto isolated=make(false,2,{0,0xb1,101,0,0,0xb1,100,0,0,0xb1,6,8});
    check(renderAll(normal)==renderAll(isolated),"RPN selection is per channel");
    auto cents=make(false,2,{0,0xb0,38,50});
    check(renderAll(normal)!=renderAll(cents),"RPN Data Entry LSB applies cents");
    check(renderAll(normal)==renderAll(make(false,2,{0,0xb0,38,127})),"invalid bend cents are ignored");
    check(renderAll(normal)==renderAll(make(false,2,{0,0xb0,6,127})),"unsupported bend range is ignored");
    MaPlayer p; check(p.init(cents,8000),"RPN seek fixture initializes");
    float a[2048],b[2048]; int n=p.render(a,1024); p.seekToStart();
    check(p.render(b,1024)==n && std::equal(a,a+n*2,b),"seek reconstructs RPN and cents");
    auto unselected=make(false,2); auto& q=unselected.tracks[0].sequenceData;
    q.erase(q.begin()+4,q.begin()+16); // remove RPN selector and data, keep default range
    check(p.init(unselected,8000),"fresh RPN init succeeds"); n=p.render(a,1024);
    MaPlayer fresh; check(fresh.init(unselected,8000),"clean RPN fixture succeeds");
    check(fresh.render(b,1024)==n && std::equal(a,a+n*2,b),"fresh init clears previous RPN/cents");
}

void checkFmDrumKeys() {
    auto makeVoice=[](int trigger, int sounding, int bank=125) {
        return std::vector<uint8_t>{0x43,0x79,7,0x7f,1,uint8_t(bank),0,10,
            uint8_t(bank==125?trigger:0),0, uint8_t(sounding),1,0,
            0,0x90,0xf0,0xfc,0,0x10,0, 0,0x90,0xf0,0,0,0x10,0};
    };
    auto makeScore=[&](int trigger,int sounding,int bank=125) {
        auto f=fixture();
        setVoice(f,makeVoice(trigger,sounding,bank));
        f.tracks[0].sequenceData={0,0xb0,0,uint8_t(bank),0,0xc0,10,
            0,0x90,uint8_t(trigger),100,20,40,0xff,0x2f,0};
        fullVolume(f);
        return f;
    };
    auto a=makeScore(42,81), b=makeScore(31,81), reference=makeScore(81,0,124);
    const auto expected=renderAll(reference);
    check(firstBlockPeak(reference)>0.001f,"fixed FM drum comparison is audible");
    check(renderAll(a)==expected && renderAll(b)==expected,
          "custom FM mapped keys sound at the separate authored DrumKey");
    auto direct=makeVoice(42,81);
    auto v=parseVoiceExclusive(direct.data(),direct.size());
    check(v.valid && v.key.drumNote==42 && v.fixedFmNote==81,
          "MA5 retains both mapped key and sounding key");
    check(renderAll(makeScore(42,0))==renderAll(makeScore(0,81,124)),
          "DrumKey zero is valid, not missing");
    check(renderAll(makeScore(60,0,124))==renderAll(makeScore(60,81,124)),
          "melodic global key does not replace sequence pitch");
    // Independent inverse of the MA3 carrier placement (not PCM transport).
    std::vector<uint8_t> raw(36,0), packed(direct.begin(),direct.begin()+10);
    packed[2]=6;
    const uint8_t* g=direct.data()+10;
    raw[1]=g[0]; raw[2]=g[1]&127; raw[3]=g[2]&127;
    raw[0]|=uint8_t(((g[1]&128)>>2)|((g[2]&128)>>3));
    for(int op=0;op<2;++op) {
        const auto* p=g+3+op*7;
        for(int i=0;i<4;++i) {
            raw[4+op*8+i]=p[i]&127;
            raw[op*8]|=uint8_t((p[i]>>7)<<(3-i));
        }
        raw[9+op*8]=p[4]; raw[10+op*8]=p[5]&127; raw[11+op*8]=p[6]&127;
        raw[8+op*8]|=uint8_t(((p[5]&128)>>2)|((p[6]&128)>>3));
    }
    packed.insert(packed.end(),raw.begin(),raw.end());
    v=parseVoiceExclusive(packed.data(),packed.size());
    check(v.valid && v.fixedFmNote==81 && v.key.drumNote==42,
          "MA3 packed sounding key skips the global carrier");
    auto ma3=a; setVoice(ma3,packed);
    check(renderAll(ma3)==expected,"MA3 and MA5 fixed-key drum render identically");
    // MA7 expanded FM body uses the same sounding key, but different op stride.
    std::vector<uint8_t> expanded{0x43,0x79,8,0x7f,0x21,125,0,10,42,0,0,g[0],g[1],g[2]};
    for(int op=0;op<2;++op) {
        const auto* p=g+3+op*7;
        expanded.insert(expanded.end(),{p[0],p[1],p[2],p[3],p[4],0,p[6],0,0,p[5]});
    }
    v=parseVoiceExclusive(expanded.data(),expanded.size());
    check(v.valid && v.fixedFmNote==81,"MA7 expanded FM drum preserves sounding key");
    auto ma7=a; ma7.tracks[0].formatType=3; setVoice(ma7,expanded);
    ma7.tracks[0].sequenceData={0,0x30,0,125,0,0x40,10,0,0x10,42,100,20,40,0xff,0x2f,0};
    fullVolume(ma7);
    check(renderAll(ma7)==expected,"MA7 fixed-key FM matches direct voice");
    // Different mapped keys can overlap at one sounding pitch. Their gates
    // must remain independent, just like two instances of the same mapped key.
    auto overlap=a, same=a;
    appendExclusive(overlap,makeVoice(31,81));
    overlap.tracks[0].sequenceData={0,0xb0,0,125,0,0xc0,10,
        0,0x90,42,100,10,5,0x90,31,100,20,30,0xff,0x2f,0};
    same.tracks[0].sequenceData=overlap.tracks[0].sequenceData;
    same.tracks[0].sequenceData[14]=42;
    check(renderAll(overlap)==renderAll(same),"fixed sounding pitch does not merge mapped-key gates");
    check(renderAll(overlap,1)==renderAll(overlap),"fixed FM drum is render-block independent");
}

void checkPackedPcm() {
    // Independent small known packed vector: high bits at first/last positions.
    const uint8_t encoded[] = {0x41,0,1,2,3,4,5,127};
    check(unpackMa3Bytes(encoded,8,7)==std::vector<uint8_t>({128,1,2,3,4,5,255}), "MA3 high-bit transport");
    check(unpackMa3Bytes(encoded,8,6).empty(), "MA3 output allocation bound");
    const uint8_t malformed[] = {0,128};
    check(unpackMa3Bytes(malformed,2,7).empty(), "MA3 rejects non-seven-bit data");
    check(unpackMa3Bytes(encoded,1,7).empty(), "MA3 rejects trailing mask");

    auto direct = fixture(), packed = fixture();
    std::vector<uint8_t> pcm = {0x43,0x79,7,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,80,1};
    setVoice(direct,pcm);
    std::vector<uint8_t> sysex(pcm.begin(),pcm.begin()+10);
    sysex[2]=6;
    auto body = packMa3({pcm.begin()+10,pcm.end()});
    sysex.insert(sysex.end(),body.begin(),body.end());
    const auto parsed = parseVoiceExclusive(sysex.data(),sysex.size());
    check(parsed.valid && parsed.isPcm && parsed.pcm.fs==8000 && parsed.pcm.env.rr==9 &&
          parsed.pcm.env.ar==15 && parsed.pcm.waveId==1 && !parsed.pcm.rom && parsed.pcm.loop,
          "packed PCM Fs/envelope/WaveID/ROM/loop fields");
    setVoice(packed,sysex);
    WaveData wave; wave.number=1; wave.samplingRate=12000; // Fs MUST come from voice (8000)
    wave.data.assign(40,0x81); // only synthesized codec bytes
    direct.tracks[0].waves.push_back(wave);
    std::vector<uint8_t> bulk{0x43,0x79,6,0x7f,3,1,0};
    auto data = packMa3(wave.data);
    bulk.insert(bulk.end(),data.begin(),data.end());
    appendExclusive(packed,bulk);
    check(renderAll(direct)==renderAll(packed), "bulk voice wave equals embedded ADPCM with authored Fs");
    MaPlayer player; check(player.init(packed,8000), "bulk fixture initializes");
    float samples[512*2]; player.render(samples,512);
    check(player.diagnostics().pcmNotes==1 && player.diagnostics().fallbackNotes==0,
          "bulk wave actually renders PCM, never substitutes FM");
    pcm[25] |= 0x80; // same ID, but ROM selected
    setVoice(direct,pcm);
    check(player.init(direct,8000), "ROM collision fixture initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0 && player.diagnostics().fallbackNotes==1,
          "ROM selector never aliases a RAM wave with same ID");
    // Unrecognized bulk codec is ignored, not decoded into noise.
    setVoice(packed,sysex); bulk[6]=3; appendExclusive(packed,bulk);
    check(player.init(packed,8000), "unknown bulk codec fixture initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0, "unknown bulk codec not guessed");
    // MA-3 unsigned PCM8 uses the same seven-bit transport, NOT ADPCM.
    pcm[25] &= 0x7f;
    setVoice(direct,pcm); setVoice(packed,sysex);
    direct.tracks[0].waves[0].adpcm=false;
    direct.tracks[0].waves[0].bitsPerSample=8;
    direct.tracks[0].waves[0].data={128,129,255,0,127,64,192,128};
    bulk={0x43,0x79,6,0x7f,3,1,2};
    data=packMa3(direct.tracks[0].waves[0].data);
    bulk.insert(bulk.end(),data.begin(),data.end()); appendExclusive(packed,bulk);
    check(renderAll(direct)==renderAll(packed), "MA3 codec2 PCM8 matches centered direct waveform");
    // Official custom banks never bind instrument IDs to recorded Audio Mwa.
    pcm[5]=124; setVoice(direct,pcm);
    direct.tracks[0].sequenceData.insert(direct.tracks[0].sequenceData.begin(),{0,0xb0,0,124});
    player.init(direct,8000); player.render(samples,512);
    check(player.diagnostics().pcmNotes==0 && player.diagnostics().fallbackNotes==1,
          "MA3 custom instrument does not alias recorded Audio");
    sysex[5]=124; setVoice(packed,sysex); appendExclusive(packed,bulk);
    packed.tracks[0].sequenceData=direct.tracks[0].sequenceData;
    player.init(packed,8000); player.render(samples,512);
    check(player.diagnostics().pcmNotes==1, "MA3 custom codec2 uses voice RAM");
}

void checkAudioStreams() {
    auto make = [](int note=0, int format=2) {
        auto f=fixture(format);
        auto& t=f.tracks[0];
        uint8_t cc=format==3?0x30:0xb0, on=format==3?0x10:0x90;
        t.sequenceData={0,cc,0,125,0,cc,32,0,0,on,uint8_t(note),127,20,20,0xff,0x2f,0};
        WaveData w; w.number=note<=12?note+1:note-92+14;
        w.adpcm=false; w.bitsPerSample=8; w.samplingRate=16000;
        for(int i=0;i<6400;++i) w.data.push_back(uint8_t(128+60*std::sin(i*0.1)));
        t.waves.push_back(w);
        return f;
    };
    auto base=make();
    auto samples=renderAll(base);
    check(samples==renderAll(base,1) && samples==renderAll(base,1024), "Audio block-size invariant");
    check(samples==renderAll(make(92)) && samples==renderAll(make(110)), "Audio note-to-wave ID mapping, not pitched playback");
    check(samples==renderAll(make(0,3)), "Audio Mobile/MA7 equivalence");
    auto native=base;
    native.tracks[0].waves[0].samplingRate=8000;
    native.tracks[0].waves[0].data.clear();
    for(size_t i=0;i<base.tracks[0].waves[0].data.size();i+=2)
        native.tracks[0].waves[0].data.push_back(base.tracks[0].waves[0].data[i]);
    check(samples==renderAll(native), "Audio native Fs resampling, not assumed 8kHz");
    check(samples.size()<2000*2,"Audio gate cuts recorded sample before its natural end");
    // Neither volume, expression nor pitch bend changes the recorded Audio.
    auto controls=base;
    controls.tracks[0].sequenceData={0,0xb0,0,125,0,0xb0,32,0,0,0xb0,7,0,
        0,0xb0,11,0,0,0xe0,127,127,0,0x90,0,127,20,20,0xff,0x2f,0};
    check(samples==renderAll(controls), "Audio ignores instrument volume/expression/bend");
    auto ma7Controls=make(0,3);
    ma7Controls.tracks[0].sequenceData={0,0x30,0,125,0,0x30,32,0,
        0,0x30,15,24,0,0x60,0,96,0,0x10,0,127,20,
        5,0x30,15,5,15,0xff,0x2f,0};
    check(samples==renderAll(ma7Controls),"MA7 recorded Audio ignores initial/live pitch sensitivity and bend");
    auto quiet=base; quiet.tracks[0].sequenceData[11]=64;
    check(std::fabs(firstBlockPeak(quiet)/firstBlockPeak(base)-std::pow(64.0f/127,2))<1e-5,
          "Audio squared velocity");
    quiet.tracks[0].sequenceData[11]=1;
    check(firstBlockPeak(quiet)==0, "Audio velocity 1 muted");
    auto signed8=base, signed16=base;
    signed8.tracks[0].waves[0].signedPcm8=true;
    for(auto& b:signed8.tracks[0].waves[0].data) b=uint8_t(int(b)-128);
    auto& w=signed16.tracks[0].waves[0]; w.bitsPerSample=16; w.data.clear();
    for(uint8_t b:base.tracks[0].waves[0].data) { w.data.push_back(uint8_t(int(b)-128)); w.data.push_back(0); }
    check(samples==renderAll(signed8) && samples==renderAll(signed16), "Audio signed/unsigned PCM centering");
    MaPlayer p; float buf[2048];
    check(p.init(base,8000), "Audio initializes"); p.render(buf,1024);
    std::vector<float> first(buf,buf+2048); p.seekToStart(); p.render(buf,1024);
    check(first==std::vector<float>(buf,buf+2048) && p.diagnostics().audioNotes==1,
          "Audio seek preserves source-track association");
    auto absent=base; absent.tracks[0].waves.clear(); p.init(absent,8000); p.render(buf,1024);
    check(p.diagnostics().missingAudio==1 && p.diagnostics().fallbackNotes==0 && firstBlockPeak(absent)==0,
          "missing Audio is never substituted by FM");
    auto ordinary=make(13); p.init(ordinary,8000); p.render(buf,1024);
    check(p.diagnostics().audioNotes==0 && p.diagnostics().fmNotes==1, "ordinary drum range remains instrumental");
    ordinary=make(91); p.init(ordinary,8000); p.render(buf,1024);
    check(p.diagnostics().audioNotes==0, "upper drum boundary remains instrumental");
    ordinary=base; ordinary.tracks[0].sequenceData[7]=10; p.init(ordinary,8000); p.render(buf,1024);
    check(p.diagnostics().audioNotes==0, "Audio bank LSB guard");
    auto pan=controls; pan.tracks[0].sequenceData.insert(pan.tracks[0].sequenceData.begin(),{0,0xb0,10,0});
    auto stereo=renderAll(pan); float lp=0,rp=0;
    for(size_t i=0;i<stereo.size();i+=2) {lp=std::max(lp,std::fabs(stereo[i]));rp=std::max(rp,std::fabs(stereo[i+1]));}
    check(lp>0.01f && rp==0, "Audio follows channel pan");
    auto overlap=base;
    // Old key-off at 10 ticks cannot stop replacement instance at 5 ticks.
    overlap.tracks[0].sequenceData={0,0xb0,0,125,0,0x90,0,127,10,
        5,0x90,0,127,40,0,0x90,0,127,40,40,0xff,0x2f,0};
    p.init(overlap,8000); p.render(buf,1024);
    check(p.diagnostics().audioNotes==3 && p.diagnostics().stolenAudio==1 &&
          p.diagnostics().stolenPcm==0, "Audio owns exactly two separate slots");
    stereo=renderAll(overlap);
    float late=0; for(size_t i=1800;i<std::min<size_t>(2500,stereo.size());++i) late=std::max(late,std::fabs(stereo[i]));
    check(late>0.01f, "stale Audio gate does not stop replacement note");
    // The same Mwa ID in another MTR must not replace this track's waveform.
    auto multi=base; auto second=make(); second.tracks[0].trackNumber=1;
    second.tracks[0].waves[0].data.assign(6400,128);
    multi.tracks.push_back(second.tracks[0]);
    check(renderAll(multi)==samples, "Mwa IDs scoped per MTR");
}

std::vector<uint8_t> chunkBytes(const std::vector<uint8_t>& id, const std::vector<uint8_t>& body) {
    auto bytes=id; uint32_t n=uint32_t(body.size());
    for(int shift=24;shift>=0;shift-=8) bytes.push_back(uint8_t(n>>shift));
    bytes.insert(bytes.end(),body.begin(),body.end()); return bytes;
}
void checkMwaHeaders() {
    auto parse = [](std::vector<uint8_t> wave) {
        auto mwa=chunkBytes({'M','w','a',1},wave);
        auto mtsp=chunkBytes({'M','t','s','p'},mwa);
        std::vector<uint8_t> track(20); track[0]=2;
        track.insert(track.end(),mtsp.begin(),mtsp.end());
        auto mtr=chunkBytes({'M','T','R',0},track);
        auto bytes=chunkBytes({'M','M','M','D'},mtr);
        SmafFile f; check(f.parse(bytes.data(),bytes.size()), "synthetic Mwa container parses");
        check(f.tracks.size()==1,"synthetic MTR exists"); return f;
    };
    for(uint8_t type:{uint8_t(0x20),uint8_t(0x01),uint8_t(0x11),uint8_t(0x03)}) {
        auto f=parse({type,0x3e,0x80,0x80,0x00});
        check(f.tracks[0].waves.size()==1,"supported Mwa codec");
        const auto& w=f.tracks[0].waves[0];
        check(w.samplingRate==16000 && w.data==std::vector<uint8_t>({0x80,0}),
              "Mwa BE16 frequency and exact three-byte payload offset");
        check(w.adpcm==(type==0x20) && w.signedPcm8==(type==0x01||type==0x03),"Mwa codec flags");
    }
    for(auto wave:std::vector<std::vector<uint8_t>>{{0x20,0x3e},{0xa0,0x3e,0x80,0},
        {0x30,0x3e,0x80,0},{0x03,0x3e,0x80,0},{0x20,0,1,0},{0x20,0xff,0xff,0}})
        check(parse(wave).tracks[0].waves.empty(),"unsupported/truncated Mwa rejected");
}

void checkPcmControllers() {
    for (int cc : {7,11}) {
        auto f=fixture(); addPcmFixture(f);
        f.tracks[0].sequenceData={0,0x90,60,100,40, 10,0xb0,uint8_t(cc),0, 30,0xff,0x2f,0};
        auto samples=renderAll(f);
        for(size_t i=1000*2;i<samples.size();++i)
            check(std::fabs(samples[i])<1e-7, "held PCM follows volume/expression changes");
    }
    auto panned=fixture(); addPcmFixture(panned);
    panned.tracks[0].sequenceData={0,0x90,60,100,40, 10,0xb0,10,0, 30,0xff,0x2f,0};
    auto samples=renderAll(panned);
    float leftPeak=0;
    for(size_t i=1000;i<2000;++i) {
        leftPeak=std::max(leftPeak,std::fabs(samples[i*2]));
        check(std::fabs(samples[i*2+1])<1e-7, "held PCM follows pan changes");
    }
    check(leftPeak>0.001f,"panned PCM remains audible");
    auto bendBefore=fixture(),bendDuring=fixture(); addPcmFixture(bendBefore); addPcmFixture(bendDuring);
    bendBefore.tracks[0].sequenceData={0,0xe0,0,96, 0,0x90,60,100,40, 40,0xff,0x2f,0};
    bendDuring.tracks[0].sequenceData={0,0x90,60,100,40, 10,0xe0,0,96, 30,0xff,0x2f,0};
    auto unbent=fixture(); addPcmFixture(unbent);
    unbent.tracks[0].sequenceData={0,0x90,60,100,40,40,0xff,0x2f,0};
    check(renderAll(bendBefore)!=renderAll(unbent), "PCM applies initial pitch bend");
    check(renderAll(bendDuring)!=renderAll(unbent), "PCM applies live pitch bend");
}

void checkGateIdentity() {
    // The younger note ends BEFORE the older one. A key-only/FIFO match is
    // insufficient; each gate must release its own note instance, including
    // while an older instance is still releasing.
    auto same = fixture(), separate = fixture();
    same.tracks[0].sequenceData = {
        0,0x90,60,60,40, 5,0x90,60,100,10, 15,0x90,60,80,10,
        20,0xff,0x2f,0};
    separate.tracks[0].sequenceData = same.tracks[0].sequenceData;
    separate.tracks[0].sequenceData[6] = 0x91;
    separate.tracks[0].sequenceData[11] = 0x92;
    for (bool pcm : {false, true}) {
        if (pcm) { addPcmFixture(same); addPcmFixture(separate); }
        const auto actual = renderAll(same), expected = renderAll(separate);
        check(actual.size() == expected.size(), "same-key gates retain independent release duration");
        for (size_t i = 0; i < actual.size(); ++i)
            check(std::fabs(actual[i] - expected[i]) < 1e-6,
                  "same-key FM/PCM gates match independent-channel rendering");
        check(renderAll(same,1) == actual, "render independent of buffer size");
    }
}

void checkMa7Sequ() {
    // Format 3 encodes channels 0..15 with the Mobile event classes minus
    // 0x80, and channels 16..31 with the familiar MIDI-looking statuses.
    auto mobile = fixture(2), lowBank = fixture(3), highBank = fixture(3);
    mobile.tracks[0].sequenceData = {
        0,0xc0,5, 0,0xb0,7,127, 0,0xe0,0,64,
        0,0x90,60,80,10, 10,0x80,62,10, 10,0xff,0x2f,0};
    lowBank.tracks[0].sequenceData = {
        0,0x40,5, 0,0x30,7,127, 0,0x60,0,64,
        0,0x10,60,80,10, 10,0x00,62,10, 10,0xff,0x2f,0};
    highBank.tracks[0].sequenceData = mobile.tracks[0].sequenceData;
    check(renderAll(lowBank) == renderAll(mobile), "MA7 low channel bank event mapping");
    check(renderAll(highBank) == renderAll(mobile), "MA7 high channel bank event mapping");
}

void checkMa7Voices() {
    std::vector<uint8_t> fm = {
        0x43,0x79,0x08,0x7f,0x21, 0x7c,1,2,0,60,
        0, 0,1,0, // flags, KeyNumber, Pan/BO, LFO/PE/ALG
        0xa1,0xbc,0xd4,0xfc,0x31,0,0x15,0,0,0x67,
        0x01,0x22,0xf3,0x40,0x00,0,0x08,0,0,0x10};
    check(fm.size()==34, "MA7 synthetic 2-op image size");
    auto v=parseVoiceExclusive(fm.data(),fm.size());
    check(v.valid && !v.isPcm && v.key.bankMSB==0x7c && v.key.bankLSB==1 &&
          v.key.pc==2 && v.key.drumNote==0 && v.keyHigh==60,
          "MA7 FM key and split mapping");
    check(v.patch.ops[0].sr==10 && v.patch.ops[0].ksr==1 &&
          v.patch.ops[0].rr==11 && v.patch.ops[0].dr==12 &&
          v.patch.ops[0].ar==13 && v.patch.ops[0].sl==4 &&
          v.patch.ops[0].multi==6 && v.patch.ops[0].dt==7 &&
          v.patch.ops[0].wave==2 && v.patch.ops[0].fb==5,
          "MA7 expanded operator folds to VM35");

    std::vector<uint8_t> wt = {
        0x43,0x79,0x08,0x7f,0x21, 0x7d,0,0,40,0,
        1, 0x1f,0x40,0,0,0,0x90,0xf0,0,0,
        0,0,0,0,0,0,80,1};
    check(wt.size()==28, "MA7 synthetic WT image size");
    v=parseVoiceExclusive(wt.data(),wt.size());
    check(v.valid && v.isPcm && v.key.bankMSB==0x7d && v.key.drumNote==40 &&
          v.pcm.fs==8000 && v.pcm.env.rr==9 && v.pcm.env.ar==15 &&
          v.pcm.waveId==1 && !v.pcm.rom,
          "MA7 WT folds to VM35 PCM voice");
}

void checkMa7WaveRam() {
    // All data is synthetic. A Format-3 WT selects its own RAM namespace.
    auto legacy = fixture(), ma7 = fixture(3);
    addPcmFixture(legacy);
    std::vector<uint8_t> wt{
        0x43,0x79,8,0x7f,0x21,0,0,0,0,0,
        1,0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,0,80,1};
    setVoice(ma7,wt);
    ma7.tracks[0].sequenceData = {0,0x10,60,100,10,10,0xff,0x2f,0};
    fullVolume(ma7); fullVolume(legacy);
    // The same ID in audio-stream RAM is deliberately unrelated.
    ma7.tracks[0].waves = legacy.tracks[0].waves;
    MaPlayer player;
    float samples[512*2];
    check(player.init(ma7,8000), "MA7 stream collision initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0 && player.diagnostics().fallbackNotes==1,
          "MA7 missing voice RAM never aliases stream RAM");

    std::vector<uint8_t> bulk{0x43,0x79,8,0x7f,0x23,1,2};
    auto& wave = legacy.tracks[0].waves[0];
    wave.data.clear();
    for (int i=0;i<80;++i) {
        const uint8_t sample = uint8_t(128 + int(15 * std::sin(i * 6.283185307179586 / 40)));
        bulk.push_back(sample);
        const uint16_t signedSample = uint16_t(int16_t((int(sample)-128)*256));
        wave.data.push_back(uint8_t(signedSample>>8));
        wave.data.push_back(uint8_t(signedSample));
    }
    appendExclusive(ma7,bulk);
    check(renderAll(ma7)==renderAll(legacy), "MA7 native unsigned PCM8 agrees with PCM16 reference");
    check(renderAll(ma7,31)==renderAll(ma7,512), "MA7 wave rendering independent of output block size");
    check(player.init(ma7,8000), "MA7 PCM8 initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==1 && player.diagnostics().fallbackNotes==0,
          "MA7 PCM8 binds custom instrument, not fallback");

    auto chord = ma7, legacyChord = legacy;
    chord.tracks[0].sequenceData.clear(); legacyChord.tracks[0].sequenceData.clear();
    for (int i=0;i<24;++i) {
        const std::vector<uint8_t> note{0,0x10,uint8_t(48+i),100,100};
        chord.tracks[0].sequenceData.insert(chord.tracks[0].sequenceData.end(),note.begin(),note.end());
        auto oldNote=note; oldNote[1]=0x90;
        legacyChord.tracks[0].sequenceData.insert(legacyChord.tracks[0].sequenceData.end(),oldNote.begin(),oldNote.end());
    }
    for (auto* f : {&chord,&legacyChord})
        f->tracks[0].sequenceData.insert(f->tracks[0].sequenceData.end(),{100,0xff,0x2f,0});
    check(player.init(chord,8000), "MA7 24-note chord initializes");
    player.render(samples,32);
    check(player.diagnostics().pcmNotes==24 && player.diagnostics().stolenHeldPcm==0,
          "MA7 uses its 32 PCM slots");
    check(player.init(legacyChord,8000), "legacy chord after MA7 initializes");
    player.render(samples,32);
    check(player.diagnostics().stolenHeldPcm==8, "reinitialization retains legacy 16-slot policy");

    // AL adds sixteen bytes before WaveID; neutral Pitch-EG adds nine AFTER it.
    auto expanded = wt;
    expanded[10]=7;
    expanded.insert(expanded.end()-1,16,0);
    expanded.insert(expanded.end(),9,0);
    check(expanded.size()==53, "MA7 neutral Pitch-EG image length");
    auto voice = parseVoiceExclusive(expanded.data(),expanded.size());
    check(voice.valid && voice.pcm.waveId==1 && voice.pcm.voiceWaveOnly,
          "MA7 neutral Pitch-EG retains WaveID before extension");
    auto expandedFile = ma7;
    setVoice(expandedFile,expanded); appendExclusive(expandedFile,bulk);
    check(renderAll(expandedFile)==renderAll(ma7), "MA7 zero Pitch-EG preserves WT playback");
    expanded.back()=1;
    check(!parseVoiceExclusive(expanded.data(),expanded.size()).valid,
          "nonneutral Pitch-EG is not silently discarded");
    expanded.pop_back();
    check(!parseVoiceExclusive(expanded.data(),expanded.size()).valid,
          "truncated extended MA7 voice rejected");
    wt[10]=0x81;
    check(!parseVoiceExclusive(wt.data(),wt.size()).valid, "unknown MA7 flag bits rejected");
    wt[10]=1;

    wave.adpcm=true; wave.bitsPerSample=4; wave.data.assign(40,0x81);
    bulk.resize(7); bulk[6]=0;
    bulk.insert(bulk.end(),wave.data.begin(),wave.data.end());
    setVoice(ma7,wt); appendExclusive(ma7,bulk);
    check(renderAll(ma7)==renderAll(legacy), "MA7 raw ADPCM agrees with legacy decoder");

    setVoice(ma7,wt); bulk[6]=4; appendExclusive(ma7,bulk);
    check(player.init(ma7,8000), "MA7 unknown codec initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0, "MA7 unknown codec never decoded or stream-aliased");
    bulk[6]=0; bulk.resize(7+32769,0);
    setVoice(ma7,wt); appendExclusive(ma7,bulk);
    check(player.init(ma7,8000), "MA7 oversized wave initializes");
    player.render(samples,512);
    check(player.diagnostics().pcmNotes==0, "MA7 wave allocation bound");
}

void checkMa7SignedWaveRam() {
    // Every possible byte, including silence and both extrema. PCM8 signed
    // conversion is compared against existing unsigned8 and direct PCM16.
    auto file=fixture(3), direct=fixture(2);
    file.tracks[0].sequenceData={0,0x10,60,100,40,40,0xff,0x2f,0};
    direct.tracks[0].sequenceData={0,0x90,60,100,40,40,0xff,0x2f,0};
    fullVolume(file); fullVolume(direct);
    std::vector<uint8_t> wt{
        0x43,0x79,8,0x7f,0x21,0,0,0,0,0,
        1,0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,0,1,0,1};
    setVoice(file,wt);
    addPcmFixture(direct,1);
    auto& reference=direct.tracks[0].waves[0];
    reference.data.clear();
    // Legacy PCM body has the same 256-sample endpoint and loop.
    setVoice(direct,{0x43,0x79,7,0x7f,1,0,0,0,0,1,
        0x1f,0x40,0,0,0,0x90,0xf0,0,0,0,0,0,0,1,0,1});
    std::vector<uint8_t> signedBulk{0x43,0x79,8,0x7f,0x23,1,3};
    auto unsignedBulk=signedBulk; unsignedBulk[6]=2;
    for (int i=0;i<256;++i) {
        signedBulk.push_back(uint8_t(i));
        unsignedBulk.push_back(uint8_t(i^128));
        const uint16_t word=uint16_t(int16_t((i<128?i:i-256)*256));
        reference.data.push_back(uint8_t(word>>8)); reference.data.push_back(uint8_t(word));
    }
    appendExclusive(file,signedBulk);
    const auto expected=renderAll(direct);
    check(expected==renderAll(file),"MA7 signed PCM8 all byte values match PCM16 reference");
    auto unsignedFile=file; setVoice(unsignedFile,wt); appendExclusive(unsignedFile,unsignedBulk);
    check(expected==renderAll(unsignedFile),"MA7 signed PCM8 matches offset unsigned PCM8");
    check(expected==renderAll(file,1) && expected==renderAll(file,1024),
          "MA7 signed PCM8 output independent of block size");
    MaPlayer player; check(player.init(file,8000),"MA7 signed PCM8 initializes");
    std::vector<float> scratch(514), replay; int frames;
    player.render(scratch.data(),257); player.seekToStart();
    while((frames=player.render(scratch.data(),257))>0)
        replay.insert(replay.end(),scratch.begin(),scratch.begin()+frames*2);
    check(replay==expected,"MA7 signed PCM8 seek exactly repeats output");
    check(player.diagnostics().pcmNotes==1 && player.diagnostics().fallbackNotes==0,
          "MA7 signed PCM8 restores authored WT instead of fallback");
    signedBulk.resize(7+65536,0);
    auto maximum=file; setVoice(maximum,wt); appendExclusive(maximum,signedBulk);
    check(player.init(maximum,8000),"MA7 max signed wave initializes");
    player.render(scratch.data(),257);
    check(player.diagnostics().pcmNotes==1,"MA7 65536 signed samples accepted");
    signedBulk.push_back(0);
    auto oversized=file; setVoice(oversized,wt); appendExclusive(oversized,signedBulk);
    check(player.init(oversized,8000),"MA7 oversized signed wave initializes");
    player.render(scratch.data(),257);
    check(player.diagnostics().pcmNotes==0 && player.diagnostics().fallbackNotes==1,
          "MA7 signed PCM8 allocation bounded to 65536 samples");
    // Rejected overwrites leave a previous valid wave intact, rather than
    // clearing the bank or aliasing a same-ID audio stream.
    auto retained=file; appendExclusive(retained,signedBulk);
    check(renderAll(retained)==expected,"oversized signed replacement preserves prior wave");
    signedBulk.resize(7); auto empty=file; setVoice(empty,wt); appendExclusive(empty,signedBulk);
    check(player.init(empty,8000),"empty signed PCM8 initializes safely");
    player.render(scratch.data(),257);
    check(player.diagnostics().pcmNotes==0,"empty signed PCM8 has no waveform");
    check(player.init(direct,8000),"legacy after signed PCM8 reinitializes");
    replay.clear();
    while((frames=player.render(scratch.data(),257))>0)
        replay.insert(replay.end(),scratch.begin(),scratch.begin()+frames*2);
    check(replay==expected,"signed voice RAM cannot leak into subsequent legacy file");

    // An authored one-shot may end in a zero-only endpoint loop while a
    // very slow envelope is still active. Retire ONLY when every future
    // interpolation source sample is exactly zero; do not mute a quiet loop.
    auto silentWt=wt;
    silentWt[23]=0; silentWt[24]=254; // LP254, EP256
    silentWt[16]=0; // RR=0 makes the old zero loop wait for the safety cap
    auto signedSilent=signedBulk; signedSilent.resize(7+256);
    for(int i=0;i<256;++i) signedSilent[7+i]=i<254?uint8_t(25):uint8_t(0);
    auto unsignedSilent=signedSilent; unsignedSilent[6]=2;
    for(size_t i=7;i<unsignedSilent.size();++i) unsignedSilent[i]^=128;
    auto zeroLoop=file, oldZeroLoop=file;
    setVoice(zeroLoop,silentWt); appendExclusive(zeroLoop,signedSilent);
    setVoice(oldZeroLoop,silentWt); appendExclusive(oldZeroLoop,unsignedSilent);
    const auto shorter=renderAll(zeroLoop), longer=renderAll(oldZeroLoop);
    check(shorter.size()<longer.size() &&
          std::equal(shorter.begin(),shorter.end(),longer.begin()),
          "signed zero loop retires without altering any output sample");
    check(player.init(zeroLoop,8000),"signed zero loop initializes");
    while(player.render(scratch.data(),257)>0) {}
    check(!player.diagnostics().tailLimitReached,"signed silent loop cannot extend song to safety cap");
    signedSilent.back()=1; // any nonzero loop sample must preserve the tail
    unsignedSilent.back()=129;
    setVoice(zeroLoop,silentWt); appendExclusive(zeroLoop,signedSilent);
    setVoice(oldZeroLoop,silentWt); appendExclusive(oldZeroLoop,unsignedSilent);
    check(renderAll(zeroLoop)==renderAll(oldZeroLoop),
          "nonzero signed loop keeps exact unsigned-reference envelope/tail");
    auto overwrite=oldZeroLoop;
    setVoice(overwrite,silentWt);
    signedSilent.back()=0; appendExclusive(overwrite,signedSilent);
    unsignedSilent.back()=128; appendExclusive(overwrite,unsignedSilent);
    setVoice(oldZeroLoop,silentWt); appendExclusive(oldZeroLoop,unsignedSilent);
    check(renderAll(overwrite)==renderAll(oldZeroLoop),
          "unsigned overwrite clears new signed-only zero-loop retirement state");
}

void checkMa7WtRateBits() {
    // Synthetic WT image. Extension byte is after the original nine bytes,
    // not part of Fs, TL, loop coordinates or WaveID.
    std::vector<uint8_t> wt{
        0x43,0x79,8,0x7f,0x21,0,0,0,0,0,
        1,0x1f,0x40,0,0,0x30,0x85,0x64,0x28,0,
        0,0,0,0,0,0,80,1};
    for (int flags : {1,3,7}) {
        auto image=wt;
        image[10]=uint8_t(flags);
        if (flags!=1) image.insert(image.end()-1,16,0);
        if (flags==7) image.insert(image.end(),9,0);
        for (int bits=0;bits<16;++bits) {
            image[20]=uint8_t(bits);
            auto v=parseVoiceExclusive(image.data(),image.size());
            check(v.valid && v.isPcm && v.pcm.env.rateLowBits==bits,
                  "MA7 WT rate low bits retained in all supported shapes");
            check(v.pcm.env.ar==6 && v.pcm.env.dr==5 && v.pcm.env.sr==3 &&
                  v.pcm.env.rr==8 && v.pcm.env.sl==4 && v.pcm.env.tl==10 &&
                  v.pcm.fs==8000 && v.pcm.loopPt==0 && v.pcm.endPt==80 &&
                  v.pcm.waveId==1,
                  "MA7 rate bits do not move nibbles or adjacent fields");
        }
        image[20]=0xf0;
        check(parseVoiceExclusive(image.data(),image.size()).pcm.env.rateLowBits==0,
              "unrelated extension bits are not amplitude rates");
        auto shortImage=image; shortImage.pop_back();
        check(!parseVoiceExclusive(shortImage.data(),shortImage.size()).valid,
              "truncated MA7 rate image rejected");
        image.push_back(0);
        check(!parseVoiceExclusive(image.data(),image.size()).valid,
              "oversize MA7 rate image rejected");
    }

    // Intermediate MA-7 rates must be between adjacent legacy steps in the
    // related-chip approximation. Each phase consumes ONLY its own low bit.
    for (int bit=0;bit<4;++bit) {
        auto measure=[bit](int nibble,int bits) {
            FmOpPatch patch;
            patch.ar=15; patch.dr=0; patch.sr=0; patch.rr=0; patch.sl=0;
            if (bit==0) patch.rr=uint8_t(nibble);
            if (bit==1) patch.sr=uint8_t(nibble);
            if (bit==2) {patch.dr=uint8_t(nibble); patch.sl=15;}
            if (bit==3) patch.ar=uint8_t(nibble);
            patch.rateLowBits=uint8_t(bits);
            FmEnvelope env; env.configure(patch,8000); env.keyOn();
            if (bit!=3) advance(env,2); // instant attack then sustain/decay
            if (bit==0) env.keyOff();
            return advance(env,bit==3?64:1000);
        };
        float slow=measure(6,0), halfway=measure(6,1<<bit), fast=measure(7,0);
        check(bit==3 ? (slow<halfway && halfway<fast) : (slow>halfway && halfway>fast),
              "MA7 odd rate lies between adjacent legacy rates");
        for (int other=0;other<4;++other)
            if (other!=bit) check(measure(6,1<<other)==slow,
                                 "one MA7 rate bit cannot alter another phase");
        float stopped=measure(0,0), minimum=measure(0,1<<bit);
        check(bit==3 ? (stopped==0 && minimum>0) : (stopped==1 && minimum<1),
              "MA7 nibble-zero low-bit-one is rate1, not stopped");
        float maximum=measure(15,1<<bit);
        check(std::isfinite(maximum) && maximum>=0 && maximum<=1,
              "MA7 maximum odd rate bounded and finite");
    }
    FmOpPatch zero; zero.ar=0; zero.rateLowBits=0;
    FmEnvelope env; env.configure(zero,8000,15); env.keyOn();
    check(advance(env,8000)==0,"legacy rate0 remains stopped under key scaling");

    auto file=fixture(3);
    file.tracks[0].sequenceData={0,0x10,60,100,30,30,0xff,0x2f,0};
    fullVolume(file);
    wt[20]=0xf; setVoice(file,wt);
    std::vector<uint8_t> wave{0x43,0x79,8,0x7f,0x23,1,2};
    for (int i=0;i<80;++i)
        wave.push_back(uint8_t(128+int(15*std::sin(i*6.283185307179586/40))));
    appendExclusive(file,wave);
    const auto expected=renderAll(file);
    check(expected==renderAll(file,1) && expected==renderAll(file,1024),
          "MA7 odd WT rates independent of render block size");
    MaPlayer player; check(player.init(file,8000),"MA7 odd rate replay initializes");
    std::vector<float> scratch(257*2), replay;
    player.render(scratch.data(),257); player.seekToStart();
    int frames;
    while((frames=player.render(scratch.data(),257))>0)
        replay.insert(replay.end(),scratch.begin(),scratch.begin()+frames*2);
    check(replay==expected,"MA7 odd rates repeat exactly after seek");
    auto even=file; wt[20]=0; setVoice(even,wt); appendExclusive(even,wave);
    check(renderAll(even)!=expected,"MA7 nonzero rate bits affect actual WT rendering");
    check(player.init(even,8000),"fresh even-rate file after odd-rate init");
    replay.clear();
    while((frames=player.render(scratch.data(),257))>0)
        replay.insert(replay.end(),scratch.begin(),scratch.begin()+frames*2);
    check(replay==renderAll(even),"MA7 odd rate state cannot leak to a new file");
}

void checkAlgorithms() {
    // Independently assemble algorithm 3: op0 + (op1 -> op2) feeds op3.
    FmVoicePatch patch;
    patch.fourOp = true; patch.algorithm = 3;
    FmVoice voice;
    voice.setSampleRate(8000); voice.noteOn(patch, 220, 1);
    FmOperator ops[4];
    for (auto& op : ops) { op.configure(FmOpPatch{},8000,1); op.noteOn(220); }
    for (int i = 0; i < 100; ++i) {
        const float a = ops[0].tick(0), b = ops[1].tick(0);
        const float c = ops[2].tick(double(b) * 0.42);
        const float expected = float(ops[3].tick((double(a) + c) * 0.42) * 0.7);
        check(std::fabs(voice.tick() - expected) < 1e-6, "algorithm 3 routing");
    }
    // A held/XOF modulator cannot keep a silent voice occupying the pool once
    // every output carrier has finished. Test each topology's carrier set.
    constexpr unsigned carriers[8] = {2,3,15,8,8,10,9,13};
    for (int algo = 0; algo < 8; ++algo) {
        patch.algorithm = uint8_t(algo); patch.fourOp = algo >= 2;
        for (int op = 0; op < 4; ++op) {
            patch.ops[op] = FmOpPatch{};
            patch.ops[op].rr = 15;
            patch.ops[op].xof = !(carriers[algo] & (1u << op));
        }
        voice.noteOn(patch,220,1);
        for (int i = 0; i < 100; ++i) voice.tick();
        voice.noteOff();
        for (int i = 0; i < 8000; ++i) voice.tick();
        check(!voice.active(), "finished carriers retire voice despite held modulators");
    }
}

void checkVoiceStealing() {
    auto f = fixture();
    // The first note remains held while short notes fill the pool with tails.
    setVoice(f, {0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0, 0,0x20,0xf0,0xfc,0,0x10,0, 0,0x20,0xf0,0,0,0x10,0});
    auto& seq = f.tracks[0].sequenceData;
    seq = {0,0x90,60,100,120};
    for (int i = 0; i < 50; ++i) seq.insert(seq.end(), {1,0x91,64,40,1});
    seq.insert(seq.end(), {1,0xff,0x2f,0});
    MaPlayer player;
    check(player.init(f,8000), "stealing fixture initializes");
    std::vector<float> reference, buffer(512*2);
    int n;
    while ((n=player.render(buffer.data(),512))>0)
        reference.insert(reference.end(), buffer.begin(), buffer.begin()+n*2);
    check(player.diagnostics().stolenFm>0, "fixture exercises saturated FM pool");
    check(player.diagnostics().stolenHeldFm==0, "release tails stolen before held melody");
    player.seekToStart();
    size_t pos=0;
    while ((n=player.render(buffer.data(),512))>0) {
        check(pos+n*2<=reference.size(), "seek replay length");
        for (int i=0;i<n*2;++i) check(buffer[i]==reference[pos++], "seek stealing is deterministic");
    }
    check(pos==reference.size(), "seek replay complete");
    addPcmFixture(f,1,2);
    check(player.init(f,8000), "PCM stealing fixture initializes");
    while(player.render(buffer.data(),512)>0) {}
    check(player.diagnostics().stolenPcm>0, "fixture exercises saturated PCM pool");
    check(player.diagnostics().stolenHeldPcm==0, "PCM release tails stolen before held melody");
}

int main() {
    checkPcmModulation();
    checkMa7ExpandedMetadata();
    checkModernTimbres();
    checkMobileRpn();
    checkMa7PitchRange();
    checkMa7ControllerCurve();
    checkNoCareDrumFallback();
    checkFmDrumKeys();
    checkMa7Sequ();
    checkMa7Voices();
    checkMa7WaveRam();
    checkMa7SignedWaveRam();
    checkMa7WtRateBits();
    checkGateIdentity();
    checkAlgorithms();
    checkVoiceStealing();
    checkPackedPcm();
    checkAudioStreams();
    checkMwaHeaders();
    checkPcmControllers();
    FmOpPatch p;
    p.ar=15; p.dr=12; p.sl=4; p.sr=0; p.rr=12;
    FmEnvelope e;
    e.configure(p,44100); e.keyOn();
    float held = advance(e,44100);
    check(std::fabs(held - std::pow(10.0,-12.0/20.0)) < 1e-5, "SL attenuation");
    check(advance(e,44100*4) == held, "SR zero holds exactly");
    e.keyOff(); advance(e,44100);
    check(e.isFinished(), "normal keyoff releases");

    p.ar=0; e.configure(p,44100,15); e.keyOn();
    check(advance(e,44100)==0, "AR zero does not become instant attack under KSR");
    p.ar=15; p.dr=0; e.configure(p,44100); e.keyOn();
    check(advance(e,44100)==1, "DR zero freezes decay");
    p.dr=12; p.xof=true; e.configure(p,44100); e.keyOn();
    held=advance(e,44100); e.keyOff();
    check(advance(e,44100)==held, "XOF ignores keyoff even at SR zero");

    p.xof=false; p.dr=0; p.rr=8; e.configure(p,44100); e.keyOn();
    advance(e,10); e.keyOff();
    float a=advance(e,1000), b=advance(e,1000), c=advance(e,1000);
    check(a>b && b>c && std::fabs(b/a-c/b)<1e-5, "release is exponential");

    // MA-5 direct voice with AR=15 and SR=0 must hold, not select RR early.
    std::vector<uint8_t> voice={0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0, 0,0xc8,0xf4,0,0,0x10,0, 0,0xc8,0xf4,0,0,0x10,0};
    auto v=parseVoiceExclusive(voice.data(),voice.size());
    check(v.valid && v.patch.ops[0].egType && v.patch.ops[0].sr==0,
          "VM35 zero sustain rate preserved");

    // MA-1/2 SUS keeps the authored RR for the damped pre-key-off stage, then
    // changes release to rate 6 when the note's sound length ends.
    std::vector<uint8_t> vmaSus={0x43,0x03,0,0,0, 0,1,
        0x02,0x0f,0xf0,0,0, 0x02,0x0f,0xf0,0,0};
    auto vs=parseVoiceExclusive(vmaSus.data(),vmaSus.size());
    check(vs.valid && vs.patch.ops[0].sr==0 && vs.patch.ops[0].rr==6 &&
          vs.patch.ops[1].sr==0 && vs.patch.ops[1].rr==6,
          "VMA SUS applies bounded key-off release");
    vmaSus[7]=0; vmaSus[12]=0;
    auto vn=parseVoiceExclusive(vmaSus.data(),vmaSus.size());
    check(vn.valid && vn.patch.ops[0].rr==0 && vn.patch.ops[1].rr==0,
          "VMA without SUS preserves zero release rate");

    SmafFile hps;
    TrackChunk hpsTrack;
    hpsTrack.trackNumber=0; hpsTrack.formatType=0;
    hpsTrack.durationTimeBase=hpsTrack.gateTimeBase=0;
    hpsTrack.channelStatus={0,0};
    for (int i=0;i<40;++i) {
        hpsTrack.sequenceData.push_back(i==0 ? 0 : 2);
        hpsTrack.sequenceData.push_back(0x0c); // channel 0, C
        hpsTrack.sequenceData.push_back(1);
    }
    hpsTrack.sequenceData.insert(hpsTrack.sequenceData.end(),{0,0,0,0});
    hps.tracks.push_back(hpsTrack);
    appendExclusive(hps,vmaSus);
    MaPlayer hpsPlayer;
    check(hpsPlayer.init(hps,8000), "HandyPhone retrigger fixture initializes");
    float hpsBuffer[512*2];
    while(hpsPlayer.render(hpsBuffer,512)>0) {}
    check(hpsPlayer.diagnostics().fmNotes==40 && hpsPlayer.diagnostics().stolenFm==0,
          "HandyPhone part retriggers one voice without echo layering");

    // The optional compatibility filter is caller-gated. It removes only a
    // same-pitch program-81 note on another HandyPhone part 35..50 ms later.
    SmafFile slapback;
    for (int trackNumber=0;trackNumber<2;++trackNumber) {
        TrackChunk track;
        track.trackNumber=trackNumber; track.formatType=0;
        track.durationTimeBase=track.gateTimeBase=2; // 4 ms/tick
        track.channelStatus={0,0};
        track.sequenceData={0,0,0x30,81,
            uint8_t(trackNumber ? 10 : 0),0x0c,10, 0,0,0,0};
        slapback.tracks.push_back(track);
    }
    MaPlayer unfilteredSlapback;
    check(unfilteredSlapback.init(slapback,8000), "unfiltered slapback initializes");
    while(unfilteredSlapback.render(hpsBuffer,512)>0) {}
    check(unfilteredSlapback.diagnostics().fmNotes==2 &&
          unfilteredSlapback.diagnostics().suppressedHpsDuplicates==0,
          "slapback compatibility filter is off by default");
    MaPlayer filteredSlapback;
    check(filteredSlapback.init(slapback,8000,true), "filtered slapback initializes");
    while(filteredSlapback.render(hpsBuffer,512)>0) {}
    check(filteredSlapback.diagnostics().fmNotes==1 &&
          filteredSlapback.diagnostics().suppressedHpsDuplicates==1,
          "targeted HandyPhone slapback duplicate is suppressed");

    FmOperator op;
    p=FmOpPatch{}; p.dt=0; p.dr=0;
    op.configure(p,44100); op.noteOn(441);
    check(std::fabs(op.tick(0)-std::sin(2*3.141592653589793/100))<1e-6,
          "DT zero does not detune");
    p.dt=4; op.configure(p,44100); op.noteOn(441);
    check(std::fabs(op.tick(0)-std::sin(2*3.141592653589793/100))<1e-6,
          "DT negative zero also neutral");

    check(TrackChunk::timeBaseMs(0x10)==10 && TrackChunk::timeBaseMs(0x13)==50,
          "slow timebase range");
    MaPlayer player;
    auto f=fixture();
    check(player.init(f,8000), "synthetic score initializes");
    check(player.scoreEndSamples()==800, "gate and duration use 10ms ticks");
    check(player.totalSamples()==80800, "bounded ten-second release allowance");
    float buffer[512*2];
    int total=0, n;
    while((n=player.render(buffer,512))>0) {
        total+=n;
        check(total<=80800, "render ends within declared tail backstop");
        for(int i=0;i<n*2;++i) check(std::isfinite(buffer[i]), "finite output");
    }
    check(player.render(buffer,512)==0, "repeated render stays ended");
    check(player.finished(), "finished after natural end");
    player.seekToStart();
    check(!player.finished(), "seek resets finished flag");
    check(player.render(buffer,512)>0, "seek restarts");

    // Missing ROM waves use an independently voiced FM approximation. Its
    // levels are not calibrated against ROM PCM: do not apply a second,
    // unverified TL correction to the substitute (beta.6 regression).
    SmafFile plainFallback = fixture();
    SmafFile quietFallback = fixture();
    std::vector<uint8_t> pcmVoice = {
        0x43,0x79,0x07,0x7f,0x01, 0,0,0,0,1,
        0x1f,0x40,0,0,0,0,0, uint8_t(28 << 2), 0,0,0,0,0,0,0,0
    };
    quietFallback.tracks[0].setupData = {0xf0, uint8_t(pcmVoice.size() + 1)};
    quietFallback.tracks[0].setupData.insert(quietFallback.tracks[0].setupData.end(),
                                             pcmVoice.begin(), pcmVoice.end());
    quietFallback.tracks[0].setupData.push_back(0xf7);
    float plainPeak = firstBlockPeak(plainFallback);
    float quietPeak = firstBlockPeak(quietFallback);
    check(std::fabs(quietPeak - plainPeak) < 1e-6, "no uncalibrated ROM-to-FM level transfer");
    pcmVoice[9] = 2;
    auto unknown = parseVoiceExclusive(pcmVoice.data(), pcmVoice.size());
    check(!unknown.valid && !unknown.isPcm, "Analog Lite is not misparsed as PCM");

    auto missingWave = fixture();
    addPcmFixture(missingWave,2); // patch asks for 1, only unrelated 2 exists
    check(renderAll(missingWave) == renderAll(plainFallback), "missing wave never plays unrelated sample");

    auto quietPcm = fixture(), loudPcm = fixture();
    addPcmFixture(quietPcm); addPcmFixture(loudPcm);
    quietPcm.tracks[0].sequenceData[3] = 50;
    check(std::fabs(firstBlockPeak(quietPcm) / firstBlockPeak(loudPcm) - 0.25f) < 1e-5,
          "embedded PCM retains squared velocity");
    auto muted = fixture();
    muted.tracks[0].sequenceData = {0,0x90,60,0,10, 10,0x80,60,10, 10,0xff,0x2f,0};
    for (float sample : renderAll(muted)) check(sample == 0, "explicit and running zero velocity are silent");

    // Long natural release survives the previous arbitrary +1-second cutoff.
    auto longRelease = fixture();
    std::vector<uint8_t> longVoice = {0x43,0x79,0x07,0x7f,1,0,0,0,0,0,
        0,1,0, 8,0x50,0xf0,0xfc,0,0x10,0, 0,0x50,0xf0,0,0,0x10,0};
    setVoice(longRelease,longVoice);
    auto releaseAudio = renderAll(longRelease);
    check(releaseAudio.size() > 8800*2 && releaseAudio.size() < 80800*2,
          "natural release, not a one-second cutoff or full safety tail");
    check(std::fabs(releaseAudio.back()) < 1e-7, "natural ending drains output filter");
    longVoice[13] = 8; longVoice[20] = 8; // XOF with SR=0 on both ops: never ends
    setVoice(longRelease,longVoice);
    releaseAudio = renderAll(longRelease);
    check(releaseAudio.size() == 80800*2, "nondecaying patch reaches bounded safety tail");
    check(releaseAudio.back() == 0 && releaseAudio[releaseAudio.size()-2] == 0,
          "safety boundary fades to zero without abrupt cutoff");

    check(player.init(fixture(3),8000), "MA7 accepted by core");
    check(!player.init(fixture(4),8000), "unknown format rejected by core");

    // A long-gated event cannot keep conversion alive indefinitely.
    f.tracks[0].sequenceData={0,0x90,60,100,0xff,0xff,0x7f,0,0xff,0x2f,0};
    check(player.init(f,10), "long gate fixture initializes");
    total=0;
    while((n=player.render(buffer,512))>0) { total+=n; check(total<=6000,"hard cap"); }
    check(player.render(buffer,512)==0,"hard cap remains ended");
    check(player.finished(),"finished at hard cap even with active envelopes");
    std::cout << "Audio regression checks passed\n";
}
