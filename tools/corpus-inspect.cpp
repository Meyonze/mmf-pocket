// SPDX-License-Identifier: Apache-2.0
// Fast structural inventory for a private MMF corpus. No audio is rendered.

#include "smaf_file.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using fxchain::smaf::SmafFile;

namespace {

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

bool contains(const std::vector<uint8_t>& bytes, std::initializer_list<uint8_t> pattern) {
    return std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end()) != bytes.end();
}

std::string utf8(const fs::path& path) {
    auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

int run(const fs::path& root) {
    size_t files = 0, parsed = 0, playable = 0, knownVoice = 0;
    size_t ma35Long = 0, ma5Short = 0, ma12 = 0;
    std::map<std::string, size_t> trackKinds;
    std::vector<fs::path> unsupported;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mmf") continue;
        ++files;
        auto bytes = readFile(entry.path());
        bool longVoice = contains(bytes, {0x43, 0x79, 0x06, 0x7f, 0x01}) ||
                         contains(bytes, {0x43, 0x79, 0x07, 0x7f, 0x01});
        bool shortVoice = contains(bytes, {0x43, 0x05, 0x01});
        bool vmaVoice = contains(bytes, {0x43, 0x03});
        ma35Long += longVoice;
        ma5Short += shortVoice;
        ma12 += vmaVoice;
        knownVoice += longVoice || shortVoice || vmaVoice;

        SmafFile file;
        if (!file.parse(bytes.data(), bytes.size())) continue;
        ++parsed;
        if (!file.playableScoreTracks().empty()) ++playable;
        else unsupported.push_back(entry.path().lexically_relative(root));
        for (const auto& track : file.tracks) {
            std::string key;
            key += track.isAudioTrack ? 'A' : 'M';
            key += ":fmt=" + std::to_string(track.formatType);
            key += ":tb=" + std::to_string(track.durationTimeBase);
            key += "/" + std::to_string(track.gateTimeBase);
            ++trackKinds[key];
        }
    }

    std::cout << "files=" << files << " parsed=" << parsed << " playable=" << playable << '\n';
    std::cout << "voice_files known=" << knownVoice << " ma35_long=" << ma35Long
              << " ma5_short=" << ma5Short << " ma12=" << ma12 << '\n';
    for (const auto& [kind, count] : trackKinds) std::cout << count << '\t' << kind << '\n';
    for (const auto& path : unsupported) std::cout << "unsupported\t" << utf8(path) << '\n';
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    return run(fs::path(argv[1]));
}
#else
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    return run(fs::path(argv[1]));
}
#endif
