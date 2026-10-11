// SPDX-License-Identifier: Apache-2.0
// Synthetic coverage fixtures; no copyrighted score data.
void checkScoreAudit() {
    using D = ScoreAudit::Domain;
    using S = ScoreAudit::Status;
    ScoreAudit inventory;
    inventory.record(D::Controller,S::Unsupported,2,0,1,77,90,300);
    inventory.record(D::Controller,S::Unsupported,2,0,1,77,20,100);
    check(inventory.groups.size()==1 && inventory.groups[0].count==2 &&
          inventory.groups[0].firstSample==100 && inventory.groups[0].lastSample==300 &&
          inventory.groups[0].minValue==20 && inventory.groups[0].maxValue==90,
          "audit groups by meaning, not values or timestamps");
    for(int i=0;i<300;++i) inventory.record(D::Controller,S::Unsupported,2,1,i,88,i,0);
    check(inventory.groups.size()==ScoreAudit::kMaxGroups && inventory.overflowOccurrences==45 &&
          inventory.totals[size_t(S::Unsupported)]==302 && inventory.scanComplete,
          "audit inventory bound exposes overflow without losing total counts");
    inventory.record(D::Controller,S::Unsupported,2,0,1,77,100,400);
    check(inventory.groups[0].count==3,"existing group still updates after capacity");
    inventory.totals[size_t(S::Unsupported)]=UINT64_MAX;
    inventory.record(D::Controller,S::Unsupported,2,0,1,77,100,400);
    check(inventory.totals[size_t(S::Unsupported)]==UINT64_MAX,"audit counters saturate");

    MaPlayer p;
    auto late=fixture();
    // 12,000 ticks: late controls cannot be missed by a first-ten-second render.
    late.tracks[0].sequenceData={0,0x90,60,100,10,0xdd,0x60,0xb0,77,30,
        1,0xb0,77,90,0,0xb1,77,40,0,0xb0,1,64,0,0xb0,15,12,
        0,0xb0,101,0,0,0xb0,100,0,0,0xb0,6,12,0,0xff,0x2f,0};
    check(p.init(late,8000),"late audit fixture initializes");
    auto snapshot=p.scoreAudit();
    bool foundLate=false;
    for(const auto& g:snapshot.groups)
        if(g.domain==D::Controller && g.opcode==77 && g.channel==0)
            foundLate=g.count==2 && g.firstSample>80000 && g.lastSample>g.firstSample &&
                g.minValue==30 && g.maxValue==90 && g.status==S::Unsupported;
    check(foundLate && snapshot.totals[size_t(S::Unsupported)]==3 &&
          snapshot.totals[size_t(S::Unapplied)]==1 &&
          snapshot.totals[size_t(S::Compatibility)]==1 &&
          snapshot.totals[size_t(S::Conditional)]==1 && snapshot.scanComplete,
          "whole score distinguishes dropped, parsed-unapplied, empirical and conditional controls");
    float block[514]; p.render(block,257);p.seekToStart();
    check(p.scoreAudit().totals==snapshot.totals &&
          p.scoreAudit().groups.size()==snapshot.groups.size() &&
          p.scoreAudit().groups[0].lastSample==snapshot.groups[0].lastSample,
          "audit is unchanged by render and seek");
    auto ma7=fixture(3);
    ma7.tracks[0].sequenceData={0,0x10,60,100,10,0,0x30,1,64,0,0xb0,64,127,
                              0,0x30,15,12,0,0xff,0x2f,0};
    check(p.init(ma7,8000) && p.scoreAudit().totals[size_t(S::Unapplied)]==0 &&
          p.scoreAudit().totals[size_t(S::Unsupported)]==1 &&
          p.scoreAudit().totals[size_t(S::Compatibility)]==0,
          "MA7 routing and high-bank hold support are distinct from Mobile");
    auto reserved=fixture();
    reserved.tracks[0].sequenceData.insert(reserved.tracks[0].sequenceData.begin(),
                                          {0,0xa0,0,0,0,0xd0,0});
    check(p.init(reserved,8000) && p.scoreAudit().totals[size_t(S::Reserved)]==2 &&
          p.scoreAudit().scanComplete,"reserved widths are intentional skips, not failures");
    for(const auto& tail:std::vector<std::vector<uint8_t>>{
            {0,0xb0,7},{0,0x90,60},{0,0xe0,0},{0,0xf0,10,0x43},
            {0,0xff,0x2f},{0x80},{0,0x90,60,100,0x80},
            {0x80,0x80,0x80,0x80,0x80}}) {
        auto broken=fixture();broken.tracks[0].sequenceData=tail;
        p.init(broken,8000);
        check(!p.scoreAudit().scanComplete && p.scoreAudit().totals[size_t(S::Malformed)]>0,
              "truncated sequence/VLQ never reports complete coverage");
    }
    auto brokenSetup=fixture();brokenSetup.tracks[0].setupData={0xf0,10,0x43};
    check(p.init(brokenSetup,8000) && !p.scoreAudit().scanComplete,
          "setup truncation is visible without breaking previously playable score");
    auto huffman=fixture(1);huffman.tracks[0].sequenceData={0,0,0,10,0};
    p.init(huffman,8000);
    check(!p.scoreAudit().scanComplete && p.scoreAudit().totals[size_t(S::Malformed)]>0,
          "failed/partial Huffman inflation is incomplete coverage");
    p.init(fixture(4),8000);
    check(!p.scoreAudit().scanComplete && p.scoreAudit().groups.size()==1,
          "failed init replaces previous audit rather than leaking it");
    check(p.init(fixture(),8000) && p.scoreAudit().scanComplete && p.scoreAudit().groups.empty(),
          "successful init clears previous warnings");

    auto original=fixture(),ignored=original;
    ignored.tracks[0].sequenceData.insert(ignored.tracks[0].sequenceData.begin(),
                                        {0,0xb0,77,64,0,0xb0,1,64});
    check(renderAll(original)==renderAll(ignored),"inventory does not apply previously ignored controls");
    auto hps=fixture(0);hps.tracks[0].sequenceData={0,0,0x23,0,0,0x33,10,0,0x21,10,0,0,0,0};
    check(p.init(hps,8000) && p.scoreAudit().totals[size_t(S::Unapplied)]==2 &&
          p.scoreAudit().totals[size_t(S::Unsupported)]==0,"HPS short and long modulation gaps are parsed but explicitly unapplied");

    // The dormant fixed block/FNUM must not be confused with enabled fixed-F.
    std::vector<uint8_t> voice{0x43,0x79,8,0x7f,0x21,124,1,2,0,127,0,0,1,0};
    for(int i=0;i<2;++i)voice.insert(voice.end(),{0,0x85,0x64,0,0,0,0,0x0e,0xca,0x10});
    auto def=fixture(3);setVoice(def,voice);check(p.init(def,8000),"voice audit fixture initializes");
    check(p.scoreAudit().totals[size_t(S::Unapplied)]==0,"dormant fixed-frequency registers are not warnings");
    for(int userWave:{15,23,31}) {
        auto userWaveVoice=voice;
        userWaveVoice[20]=uint8_t(userWave<<3);
        userWaveVoice[30]=uint8_t(userWave<<3);
        auto userWaveFile=fixture(3);setVoice(userWaveFile,userWaveVoice);
        check(p.init(userWaveFile,8000) && p.scoreAudit().scanComplete &&
              p.scoreAudit().totals[size_t(S::Unsupported)]==2 &&
              p.scoreAudit().totals[size_t(S::Reserved)]==0,
              "user SRAM wave definitions are unsupported bindings, not reserved fields");
        int waveformGroups=0;
        for(const auto& group:p.scoreAudit().groups)
            if(group.domain==D::VoiceDefinition && group.opcode>=0x130 && group.opcode<=0x133) {
                ++waveformGroups;
                check(group.status==S::Unsupported && group.minValue==userWave &&
                      group.maxValue==userWave,"user wave diagnostic retains raw wave selector");
            }
        check(waveformGroups==2,"both operator user-wave support gaps retained independently");
        const auto snapshot=p.scoreAudit().totals;p.seekToStart();
        check(p.scoreAudit().totals==snapshot,"seek retains user-wave support inventory");
    }
    auto zeroAl=voice;zeroAl[10]=2;zeroAl.insert(zeroAl.end(),16,0);
    setVoice(def,zeroAl);check(p.init(def,8000),"zero AL register definition initializes");
    check(p.scoreAudit().totals[size_t(S::Unapplied)]==1 &&
          p.scoreAudit().totals[size_t(S::Reserved)]==0,
          "uncalibrated zero AL registers do not imply hardware bypass or reserved data");
    voice[14]|=4;voice[10]=2;
    voice.insert(voice.end(),{0xff,0xbb,0x1f,0xf8,0x19,0x6d,0x16,0xc8,0x1e,0x1b,0x1f,0xf8,0x99,0x8f,0x83,0x80});
    setVoice(def,voice);check(p.init(def,8000),"unsupported-field definition remains playable");
    check(p.scoreAudit().totals[size_t(S::Unapplied)]==2,"enabled fixed-F and AL definitions inventoried");

    auto capped=fixture();capped.tracks[0].sequenceData.clear();
    for(int i=0;i<200001;++i)capped.tracks[0].sequenceData.insert(
        capped.tracks[0].sequenceData.end(),{0,0x90,60,100,1});
    check(p.init(capped,8000) && !p.scoreAudit().scanComplete &&
          p.scoreAudit().totals[size_t(S::ResourceLimit)]==1,
          "event safety ceiling cannot silently claim a complete score");
    auto voiceCap=fixture(3);voice[10]=0;voice.resize(34);
    for(int i=0;i<2050;++i)appendExclusive(voiceCap,voice);
    check(p.init(voiceCap,8000) && !p.scoreAudit().scanComplete &&
          p.scoreAudit().totals[size_t(S::ResourceLimit)]==2,
          "voice definition ceiling is diagnosed without changing playback policy");
    auto waveCap=fixture(3);auto& waveSeq=waveCap.tracks[0].sequenceData;waveSeq.clear();
    for(int i=0;i<514;++i)waveSeq.insert(waveSeq.end(),
        {0,0xf0,9,0x43,0x79,8,0x7f,0x23,1,3,0,0xf7});
    waveSeq.insert(waveSeq.end(),{0,0x10,60,100,10,0,0xff,0x2f,0});
    check(p.init(waveCap,8000) && !p.scoreAudit().scanComplete &&
          p.scoreAudit().totals[size_t(S::ResourceLimit)]==2 &&
          p.scoreAudit().totals[size_t(S::Unsupported)]==0,
          "inline wave ceiling does not invent unknown-voice warnings");
}
