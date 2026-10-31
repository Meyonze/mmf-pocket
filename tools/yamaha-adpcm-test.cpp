// SPDX-License-Identifier: Apache-2.0
// Synthetic black-box interoperability vectors; no vendor ROM or decoder code.
#include "yamaha_adpcm.h"
#include <array>
#include <cstdlib>
#include <iostream>
using fxchain::smaf::YamahaAdpcm;
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
uint64_t fingerprint(const std::vector<int16_t>& pcm) {
    uint64_t hash = 14695981039346656037ull;
    for (int16_t sample : pcm) {
        const auto word = uint16_t(sample);
        for (unsigned shift : {0u, 8u}) { hash ^= (word >> shift) & 255; hash *= 1099511628211ull; }
    }
    return hash;
}
void vector(const std::vector<uint8_t>& input, uint64_t expected) {
    YamahaAdpcm decoder;
    const auto pcm = decoder.decodeAll(input.data(), input.size(), false);
    require(pcm.size() == input.size() * 2, "one PCM frame per ADPCM nibble");
    require(fingerprint(pcm) == expected, "decoded PCM matches frozen independent reference");
    require(pcm == decoder.decodeAll(input.data(), input.size(), false), "decodeAll resets prior state");
    decoder.reset();
    size_t frame = 0;
    for (uint8_t byte : input) {
        require(decoder.decodeNibble(byte & 15) == pcm[frame++], "incremental low nibble agrees");
        require(decoder.decodeNibble(byte >> 4) == pcm[frame++], "incremental high nibble agrees");
    }
    auto swapped = input;
    for (auto& byte : swapped) byte = uint8_t((byte << 4) | (byte >> 4));
    require(pcm == decoder.decodeAll(swapped.data(), swapped.size(), true),
            "high-nibble-first option changes transport order only");
}
int main() {
    // Reference PCM generated with FFmpeg 7.1.5's adpcm_yamaha WAV decoder.
    // WAV format tag 0x20, mono, 8 kHz, four bits/sample. Only output numbers
    // are retained; neither FFmpeg source nor copyrighted media is embedded.
    const std::array<uint8_t,8> codes{0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe};
    std::vector<uint8_t> all;
    for (int repeat = 0; repeat < 8; ++repeat) all.insert(all.end(), codes.begin(), codes.end());
    vector(all, 0xe79c4f34d6c5cb0eull);
    uint32_t state = 0x534d4146;
    std::vector<uint8_t> random(8192);
    for (auto& byte : random) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5; byte = uint8_t(state);
    }
    vector(random, 0x26b40bb2162337f9ull);
    vector(std::vector<uint8_t>(128, 0x77), 0xd13b2555b606d6c7ull);
    vector(std::vector<uint8_t>(128, 0xff), 0x010ef237989e2097ull);
    vector(std::vector<uint8_t>(128, 0x11), 0x201f9b474e5a29e1ull);
    vector(std::vector<uint8_t>(128, 0x99), 0x4fd52cad343c0226ull);
    // At minimum step127, a magnitude-one code is floor(3*127/8)=47,
    // not floor(127/8)+floor(127/4)=46. Sign is applied after rounding.
    YamahaAdpcm decoder;
    for (int n = 1; n <= 256; ++n)
        require(decoder.decodeNibble(1) == n * 47, "quiet positive codes do not accumulate truncation bias");
    decoder.reset();
    for (int n = 1; n <= 256; ++n)
        require(decoder.decodeNibble(9) == -n * 47, "quiet negative codes remain symmetric");
    require(decoder.decodeAll(nullptr, 0).empty(), "empty input returns no PCM");
    require(decoder.decodeNibble(1) == 47, "empty decode also resets the state");
    std::cout << "Yamaha ADPCM tests PASS: six independent reference vectors, order, reset and quiet-code bias\n";
}
