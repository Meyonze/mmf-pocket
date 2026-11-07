// SPDX-License-Identifier: Apache-2.0
// Decode a Yamaha SMAF ringtone to a PCM16 stereo WAV, without Qt.
// Usage: smaf-render tune.mmf out.wav [seconds] (default 30, maximum 600)
// Build with the CMakeLists in the library root.

#include "smaf_file.h"
#include "ma_player.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace fxchain::smaf;

static std::vector<uint8_t> readAll(const char* path) {
    constexpr std::streamoff limit = 16 * 1024 * 1024;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open input");
    const auto size = f.tellg();
    if (size < 12 || size > limit)
        throw std::runtime_error("input must be between 12 bytes and 16 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("cannot read complete input");
    if (f.peek() != std::char_traits<char>::eof() || f.bad())
        throw std::runtime_error("input changed or failed while reading");
    return bytes;
}

static int16_t pcm16(float value) {
    if (!std::isfinite(value))
        throw std::runtime_error("renderer produced a nonfinite sample");
    return static_cast<int16_t>(std::lround(std::clamp(double(value), -1.0, 1.0) * 32767.0));
}

static void writeHeader(std::ostream& f, uint32_t frames, uint32_t rate) {
    auto w32 = [&](uint32_t v) {
        const char bytes[] = {char(v & 255), char((v >> 8) & 255),
                              char((v >> 16) & 255), char((v >> 24) & 255)};
        f.write(bytes, 4);
    };
    auto w16 = [&](uint16_t v) {
        const char bytes[] = {char(v & 255), char((v >> 8) & 255)};
        f.write(bytes, 2);
    };
    // The 600-second limit at 44100 Hz bounds all RIFF sizes below UINT32_MAX.
    const uint32_t size = frames * 4;
    f.write("RIFF", 4); w32(36 + size); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
    f.write("data", 4); w32(size);
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 6) {
        std::fprintf(stderr, "usage: smaf-render <tune.mmf> <out.wav> [seconds] [--experimental-sine-factor=1|2|4|8|16|32] [--experimental-sine-math=exact|hermite]\n");
        return 2;
    }
    double seconds = 30.0;
    int option = 3;
    if (argc > 3 && !std::string_view(argv[3]).starts_with("--")) {
        char* end = nullptr;
        errno = 0;
        seconds = std::strtod(argv[3], &end);
        if (end == argv[3] || *end || errno == ERANGE ||
            !std::isfinite(seconds) || seconds <= 0 || seconds > 600) {
            std::fprintf(stderr, "seconds must be a finite number greater than 0 and at most 600\n");
            return 2;
        }
        ++option;
    }
    SineRenderOptions sine;
    bool factorSeen = false, mathSeen = false;
    for (; option < argc; ++option) {
        std::string_view arg(argv[option]);
        if (arg.starts_with("--experimental-sine-factor=") && !factorSeen) {
            auto value = arg.substr(std::string_view("--experimental-sine-factor=").size());
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), sine.factor);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                sine.factor < 1 || sine.factor > 32 || (sine.factor & (sine.factor - 1)) != 0) {
                std::fprintf(stderr, "experimental sine factor must be1/2/4/8/16/32\n"); return 2;
            }
            factorSeen = true;
        } else if (arg.starts_with("--experimental-sine-math=") && !mathSeen) {
            auto value = arg.substr(std::string_view("--experimental-sine-math=").size());
            if (value == "exact") sine.math = FmSubframeMath::Exact;
            else if (value == "hermite") sine.math = FmSubframeMath::Hermite;
            else { std::fprintf(stderr, "experimental sine math must be exact/hermite\n"); return 2; }
            mathSeen = true;
        } else { std::fprintf(stderr, "unknown or duplicate renderer option\n"); return 2; }
    }
    if (mathSeen && !factorSeen) {
        std::fprintf(stderr, "experimental sine math requires an explicit factor\n"); return 2;
    }
    constexpr uint32_t rate = 44100;
    try {
        auto bytes = readAll(argv[1]);
        SmafFile file;
        if (!file.parse(bytes.data(), bytes.size()))
            throw std::runtime_error("not a valid SMAF file");
        std::printf("SMAF: title='%s' vendor='%s' tracks=%zu\n",
                    file.title.c_str(), file.vendor.c_str(), file.tracks.size());
        MaPlayer player;
        if (!player.init(file, rate, false, sine)) throw std::runtime_error("no playable track");

        std::error_code aliasError;
        if (std::filesystem::equivalent(argv[1], argv[2], aliasError))
            throw std::runtime_error("input and output must be different files");
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open output");
        writeHeader(out, 0, rate);
        if (!out) throw std::runtime_error("cannot write WAV header");
        const int total = static_cast<int>(seconds * rate);
        float buf[1024 * 2];
        char encoded[1024 * 4];
        int done = 0;
        double peak = 0;
        bool audible = false;
        while (done < total) {
            const int want = std::min(total - done, 1024);
            const int got = player.render(buf, want);
            if (got < 0 || got > want) throw std::runtime_error("invalid rendered frame count");
            if (got == 0) break;
            for (int i = 0; i < got * 2; ++i) {
                const int16_t sample = pcm16(buf[i]);
                audible = audible || sample != 0;
                peak = std::max(peak, std::abs(double(buf[i])));
                const uint16_t bits = static_cast<uint16_t>(sample);
                encoded[2 * i] = char(bits & 255);
                encoded[2 * i + 1] = char((bits >> 8) & 255);
            }
            if (!out.write(encoded, got * 4)) throw std::runtime_error("cannot write output samples");
            done += got;
        }
        out.seekp(0);
        writeHeader(out, static_cast<uint32_t>(done), rate);
        out.flush();
        if (!out) throw std::runtime_error("cannot finalize WAV output");
        out.close();
        if (!out) throw std::runtime_error("cannot close WAV output");
        std::printf("wrote %s (%d frames, peak %.6f)\n", argv[2], done, peak);
        return audible ? 0 : 3;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
