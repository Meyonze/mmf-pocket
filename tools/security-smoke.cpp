// SPDX-License-Identifier: Apache-2.0

#include "ma_player.h"
#include "smaf_file.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using fxchain::smaf::MaPlayer;
using fxchain::smaf::SmafFile;
using fxchain::smaf::smafHuffmanInflate;

namespace {

void putBe32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    bytes[at] = uint8_t(value >> 24);
    bytes[at + 1] = uint8_t(value >> 16);
    bytes[at + 2] = uint8_t(value >> 8);
    bytes[at + 3] = uint8_t(value);
}

void exercise(const std::vector<uint8_t>& bytes) {
    SmafFile file;
    if (!file.parse(bytes.data(), bytes.size())) return;

    MaPlayer player;
    if (!player.init(file, 44100)) return;

    std::array<float, 2048> audio{};
    // A smoke test only needs to enter the event and synthesis paths. The
    // engine itself has a ten-minute absolute render cap.
    for (int block = 0; block < 16; ++block) {
        if (player.render(audio.data(), 1024) <= 0) break;
    }
}

} // namespace

int main() {
    exercise({});
    exercise(std::vector<uint8_t>(11, 0));

    std::mt19937 random(0x4d4d4650u);
    // Enter packed PCM/voice-wave paths directly; random container headers
    // alone rarely reach a valid setup exclusive. No real media is included.
    for (int iteration=0;iteration<512;++iteration) {
        std::vector<uint8_t> payload{0x43,0x79,6,0x7f,3,1,0};
        payload[6] = uint8_t(iteration % 4); // include packed PCM8 and unknown codecs
        size_t length=random()%96;
        for(size_t i=0;i<length;++i) payload.push_back(uint8_t(random() & 127));
        if (iteration%3==0 && payload.size()>7) payload.back()=0xff;
        fxchain::smaf::TrackChunk track;
        track.trackNumber=0; track.formatType=2;
        track.setupData={0xf0,uint8_t(payload.size()+1)};
        track.setupData.insert(track.setupData.end(),payload.begin(),payload.end());
        track.setupData.push_back(0xf7);
        track.sequenceData={0,0x90,60,100,10,10,0xff,0x2f,0};
        SmafFile file; file.tracks.push_back(track);
        MaPlayer player;
        if (player.init(file,8000)) { float audio[128*2]; player.render(audio,128); }
        payload[4]=1;
        if(payload.size()>9) payload[9]=1;
        (void)fxchain::smaf::parseVoiceExclusive(payload.data(),payload.size());
    }
    // MA-7 native eight-bit wave messages and exact voice-shape guards.
    for (int iteration=0;iteration<512;++iteration) {
        std::vector<uint8_t> payload{0x43,0x79,8,0x7f,0x23,uint8_t(random()%256),uint8_t(random()%4)};
        for (size_t i=0,length=random()%96;i<length;++i) payload.push_back(uint8_t(random()));
        fxchain::smaf::TrackChunk track;
        track.trackNumber=0; track.formatType=3;
        track.setupData={0xf0,uint8_t(payload.size()+1)};
        track.setupData.insert(track.setupData.end(),payload.begin(),payload.end());
        track.setupData.push_back(0xf7);
        track.sequenceData={0,0x10,60,100,10,10,0xff,0x2f,0};
        SmafFile file; file.tracks.push_back(track);
        MaPlayer player;
        if (player.init(file,8000)) { float audio[128*2]; player.render(audio,128); }
        payload[4]=0x21;
        for (size_t length=0;length<=payload.size();++length)
            (void)fxchain::smaf::parseVoiceExclusive(payload.data(),length);
    }
    constexpr std::array<std::array<uint8_t, 4>, 4> ids{{
        {{'C', 'N', 'T', 'I'}},
        {{'O', 'P', 'D', 'A'}},
        {{'M', 'T', 'R', 0}},
        {{'A', 'T', 'R', 0}},
    }};

    for (size_t iteration = 0; iteration < 3000; ++iteration) {
        const size_t size = 12 + (random() % 8192);
        std::vector<uint8_t> bytes(size);
        for (uint8_t& value : bytes) value = uint8_t(random());

        bytes[0] = 'M'; bytes[1] = 'M'; bytes[2] = 'M'; bytes[3] = 'D';
        putBe32(bytes, 4, uint32_t(size - 8));

        for (size_t at = 8; at + 8 <= size; at += 32) {
            const auto& id = ids[random() % ids.size()];
            std::copy(id.begin(), id.end(), bytes.begin() + at);
            // Mix valid, truncated and oversized chunk declarations.
            const uint32_t declared = (random() % 3 == 0)
                    ? uint32_t(random())
                    : uint32_t(random() % 96);
            putBe32(bytes, at + 4, declared);
        }
        exercise(bytes);

        if (size >= 4) {
            putBe32(bytes, 0, uint32_t(random() % (16u << 20)));
            (void)smafHuffmanInflate(bytes.data(), bytes.size());
        }
    }

    std::cout << "security smoke test passed\n";
    return 0;
}
