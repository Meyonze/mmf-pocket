// SPDX-License-Identifier: Apache-2.0
// Host-side compatibility test. It renders to memory and never saves audio.

#include "ma_player.h"
#include "smaf_file.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using fxchain::smaf::MaPlayer;
using fxchain::smaf::SmafFile;
namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxInputBytes = 16u * 1024u * 1024u;
constexpr uint32_t kSampleRate = 44100;
uint64_t probeFrames = uint64_t(kSampleRate) * 2;

struct Result {
    fs::path path;
    std::string status;
    std::string detail;
    double seconds = 0.0;
    double probedSeconds = 0.0;
    float peak = 0.0f;
    std::string tracks;
    bool finished = false;
    MaPlayer::Diagnostics audio;
};

std::string pathUtf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::vector<uint8_t> readFile(const fs::path& path) {
    const uintmax_t size = fs::file_size(path);
    if (size > kMaxInputBytes) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

Result testOne(const fs::path& path) {
    Result result{path};
    try {
        const uintmax_t size = fs::file_size(path);
        if (size > kMaxInputBytes) {
            result.status = "too_large";
            return result;
        }
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.size() != size) {
            result.status = "read_error";
            return result;
        }

        SmafFile file;
        if (!file.parse(bytes.data(), bytes.size())) {
            result.status = "parse_error";
            return result;
        }

        {
            std::ostringstream description;
            for (size_t i = 0; i < file.tracks.size(); ++i) {
                const auto& track = file.tracks[i];
                if (i) description << ',';
                description << (track.isAudioTrack ? 'A' : 'M')
                            << track.trackNumber << ':' << track.formatType
                            << ':' << track.durationTimeBase
                            << '/' << track.gateTimeBase
                            << ':' << track.sequenceData.size()
                            << ':' << track.waves.size();
            }
            result.tracks = description.str();
        }

        MaPlayer player;
        if (!player.init(file, kSampleRate)) {
            result.status = "no_playable_track";
            return result;
        }

        result.seconds = double(player.totalSamples()) / kSampleRate;

        float buffer[1024 * 2];
        uint64_t frames = 0;
        while (frames < probeFrames) {
            int count = player.render(buffer, int(std::min<uint64_t>(1024, probeFrames - frames)));
            if (count <= 0) break;
            for (int i = 0; i < count * 2; ++i) {
                if (!std::isfinite(buffer[i])) {
                    result.status = "invalid_audio";
                    return result;
                }
                result.peak = std::max(result.peak, std::fabs(buffer[i]));
            }
            frames += uint64_t(count);
        }
        result.probedSeconds = double(frames) / kSampleRate;
        result.finished = player.finished();
        result.audio = player.diagnostics();
        result.status = result.peak >= 0.0001f ? "ok" : "ok_probe_silent";
    } catch (const std::exception& error) {
        result.status = "exception";
        result.detail = error.what();
    } catch (...) {
        result.status = "exception";
        result.detail = "unknown";
    }
    return result;
}

} // namespace

int run(const fs::path& root, const fs::path& reportPath) {
    std::ofstream reportFile;
    std::ostream* report = &std::cout;
    if (!reportPath.empty()) {
        reportFile.open(reportPath, std::ios::binary | std::ios::trunc);
        if (!reportFile) {
            std::cerr << "cannot open results file\n";
            return 2;
        }
        report = &reportFile;
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return char(std::tolower(value)); });
        if (extension == ".mmf") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    std::vector<Result> results(files.size());
    std::atomic_size_t next{0};
    const unsigned workers = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> threads;
    for (unsigned worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&] {
            while (true) {
                const size_t index = next.fetch_add(1);
                if (index >= files.size()) return;
                results[index] = testOne(files[index]);
            }
        });
    }
    for (std::thread& thread : threads) thread.join();

    size_t passed = 0;
    double totalSeconds = 0.0;
    *report << "status\tseconds\tprobed_seconds\tpeak\ttracks\tfile\tdetail"
            << "\tfinished\tfm_notes\tpcm_notes\tfallback_notes\tstolen_fm\tstolen_pcm\ttail_limit\tstolen_held_fm\tstolen_held_pcm\n";
    for (const Result& result : results) {
        if (result.status.rfind("ok", 0) == 0) ++passed;
        totalSeconds += result.seconds;
        *report << result.status << '\t' << std::fixed << std::setprecision(3)
                << result.seconds << '\t' << result.probedSeconds << '\t'
                << result.peak << '\t'
                << result.tracks << '\t'
                << pathUtf8(result.path.lexically_relative(root)) << '\t'
                << result.detail << '\t' << result.finished
                << '\t' << result.audio.fmNotes << '\t' << result.audio.pcmNotes
                << '\t' << result.audio.fallbackNotes << '\t' << result.audio.stolenFm
                << '\t' << result.audio.stolenPcm << '\t' << result.audio.tailLimitReached
                << '\t' << result.audio.stolenHeldFm
                << '\t' << result.audio.stolenHeldPcm << '\n';
    }
    std::cerr << "SUMMARY total=" << results.size()
              << " passed=" << passed
              << " failed=" << (results.size() - passed)
              << " scheduled_seconds=" << std::fixed << std::setprecision(1)
              << totalSeconds << '\n';
    return passed == results.size() ? 0 : 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: corpus-test <folder> [results.tsv] [probe-seconds: 1..602]\n";
        return 2;
    }
    if (argc == 4) {
        int seconds = std::stoi(argv[3]);
        if (seconds < 1 || seconds > 602) return 2;
        probeFrames = uint64_t(kSampleRate) * seconds;
    }
    return run(fs::path(argv[1]), argc >= 3 ? fs::path(argv[2]) : fs::path());
}
#else
int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: corpus-test <folder> [results.tsv] [probe-seconds: 1..602]\n";
        return 2;
    }
    if (argc == 4) {
        int seconds = std::stoi(argv[3]);
        if (seconds < 1 || seconds > 602) return 2;
        probeFrames = uint64_t(kSampleRate) * seconds;
    }
    return run(fs::path(argv[1]), argc >= 3 ? fs::path(argv[2]) : fs::path());
}
#endif
