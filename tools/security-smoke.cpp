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
