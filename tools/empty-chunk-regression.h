// SPDX-License-Identifier: Apache-2.0
#pragma once
// Original binary-container fixtures; no private media or manufacturer assets.
namespace emptyChunkRegression {
using namespace atrFormatRegression;

Bytes audioBody() {
    Bytes body{0,0,1,0x10,0,0};
    append(body,chunk("Atsq",{0,1,100,0,0,0,0}));
    append(body,chunk("Awa0",source(),1));
    return body;
}
Bytes scoreBody() {
    Bytes body{2,0,0x10,0x10};body.resize(20,0);
    append(body,chunk("Mtsq",{0,0x90,60,100,10,10,0xff,0x2f,0}));
    return body;
}
void assertEquivalent(const Bytes& changed,const Bytes& reference,const char* message) {
    const auto file=parse(changed), expected=parse(reference);
    check(file.tracks.size()==expected.tracks.size(),message);
    for(size_t i=0;i<file.tracks.size();++i) {
        const auto& a=file.tracks[i];const auto& b=expected.tracks[i];
        check(a.sequenceData==b.sequenceData && a.setupData==b.setupData,message);
        // An empty Awa retains its declared identity, but has no samples.
        // Compare all nonempty sources rather than rejecting that metadata.
        std::vector<const WaveData*> aw,bw;
        for(const auto& wave:a.waves)if(!wave.data.empty())aw.push_back(&wave);
        for(const auto& wave:b.waves)if(!wave.data.empty())bw.push_back(&wave);
        check(aw.size()==bw.size(),message);
        for(size_t j=0;j<aw.size();++j)
            check(aw[j]->number==bw[j]->number && aw[j]->data==bw[j]->data &&
                  aw[j]->formatByte==bw[j]->formatByte &&
                  aw[j]->samplingRate==bw[j]->samplingRate,message);
    }
    const auto samples=renderAll(expected);
    bool audible=false;for(float sample:samples)if(sample!=0)audible=true;
    check(audible,"empty-chunk reference actually sounds");
    check(renderAll(file)==samples,message);
    check(renderAll(file,1)==samples && renderAll(file,1024)==samples,
          "empty-chunk recovery is render-block invariant");
    MaPlayer player;check(player.init(file,8000),"empty-chunk seek initializes");
    float block[514];int count;while(player.render(block,257)>0){}
    player.seekToStart();std::vector<float> replay;
    while((count=player.render(block,257))>0)replay.insert(replay.end(),block,block+count*2);
    check(replay==samples,"empty-chunk seek retains recovered sequence/waves");
}
void testTrackChunks() {
    for(bool audio:{true,false}) {
        const auto original=audio?audioBody():scoreBody();
        const size_t header=audio?6:20;
        const char* track=audio?"ATR0":"MTR0";
        const auto reference=chunk(track,original);
        for(const char* id:audio?std::vector<const char*>{"Awa0","Atsu","JUNK"}
                               :std::vector<const char*>{"Mtsu","Mtsp","JUNK"}) {
            for(int repeat:{1,2,8}) {
                Bytes changed(original.begin(),original.begin()+header);
                for(int i=0;i<repeat;++i)append(changed,chunk(id,{}));
                changed.insert(changed.end(),original.begin()+header,original.end());
                assertEquivalent(chunk(track,changed),reference,
                                 "empty track chunk must not consume the next chunk's first byte");
            }
        }
        // Empty sequence may occur before a later sequence chunk; retain the
        // existing last-definition policy, without skipping that later body.
        Bytes changed(original.begin(),original.begin()+header);
        append(changed,chunk(audio?"Atsq":"Mtsq",{}));
        changed.insert(changed.end(),original.begin()+header,original.end());
        assertEquivalent(chunk(track,changed),reference,"empty sequence cannot hide a later valid sequence");
        changed=original;append(changed,chunk("JUNK",{}));
        assertEquivalent(chunk(track,changed),reference,"empty chunk at track end is bounded and harmless");
        if(audio) {
            changed=Bytes(original.begin(),original.begin()+header);
            append(changed,chunk("Atsq",{0,1,100,0,0,0,0}));
            append(changed,chunk("Awa0",{},2));
            append(changed,chunk("Awa0",source(),1));
            // Empty wave2 remains represented, but must not swallow wave1.
            auto f=parse(chunk(track,changed));
            check(f.tracks[0].waves.size()==2 && f.tracks[0].waves[1].data==source(),
                  "empty Awa followed by valid Awa retains both chunk identities");
            check(renderAll(f)==renderAll(parse(reference)),"valid Awa after empty Awa plays normally");
        }
    }
}
void testContainer() {
    Bytes reference=chunk("MTR0",scoreBody());
    append(reference,chunk("ATR0",audioBody()));
    for(const char* id:{"CNTI","OPDA","JUNK"})for(int repeat:{1,2,8}) {
        Bytes changed;for(int i=0;i<repeat;++i)append(changed,chunk(id,{}));
        append(changed,reference);
        assertEquivalent(changed,reference,"empty outer chunk cannot hide playable tracks");
    }
    Bytes changed=chunk("MTR0",scoreBody());append(changed,chunk("JUNK",{}));
    append(changed,chunk("ATR0",audioBody()));
    assertEquivalent(changed,reference,"empty outer chunk between score and audio retains both");
    changed=reference;append(changed,chunk("JUNK",{}));
    assertEquivalent(changed,reference,"empty outer chunk at end terminates without overread");
    // Unknown nonempty chunks already advance by header+body; keep that rule.
    changed=chunk("JUNK",{1,2,3});append(changed,reference);
    assertEquivalent(changed,reference,"nonempty unknown chunk skip remains unchanged");
    // Long runs remain bounded by existing chunk ceilings; no infinite loop.
    Bytes track{0,0,1,0x10,0,0};
    for(int i=0;i<9000;++i)append(track,chunk("JUNK",{}));
    auto f=parse(chunk("ATR0",track));
    check(f.tracks.size()==1 && f.tracks[0].waves.empty() && f.tracks[0].sequenceData.empty(),
          "empty-chunk run obeys track resource limits");
    // A partial next header must never manufacture a playable child.
    for(int length=0;length<8;++length) {
        track=Bytes{0,0,1,0x10,0,0};append(track,chunk("JUNK",{}));
        const auto next=chunk("Atsq",{0,1,100});
        track.insert(track.end(),next.begin(),next.begin()+length);
        f=parse(chunk("ATR0",track));
        check(f.tracks[0].sequenceData.empty(),"partial child header remains unread");
    }
}
}
void checkEmptyChunks() {
    emptyChunkRegression::testTrackChunks();
    emptyChunkRegression::testContainer();
}
