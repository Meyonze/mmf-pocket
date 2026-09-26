// SPDX-License-Identifier: Apache-2.0

#include <jni.h>

#include "ma_player.h"
#include "smaf_file.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 44100;
constexpr int kBlockFrames = 1024;

class PhoneSpeakerFilter {
public:
    void process(float* samples, int frames) {
        constexpr float dt = 1.0f / static_cast<float>(kSampleRate);
        // Small-handset-speaker coloration: mono, narrow response and mild
        // saturation. Avoid sample-rate reduction: it adds synthetic grit that
        // was not a stable characteristic of the handset speaker itself.
        // This models the speaker path only; it does not pretend to provide a
        // proprietary handset ROM instrument bank.
        constexpr float highPassHz = 420.0f;
        constexpr float lowPassHz = 3600.0f;
        constexpr float pi = 3.14159265358979323846f;
        constexpr float highRc = 1.0f / (2.0f * pi * highPassHz);
        constexpr float lowRc = 1.0f / (2.0f * pi * lowPassHz);
        constexpr float highAlpha = highRc / (highRc + dt);
        constexpr float lowAlpha = dt / (lowRc + dt);
        constexpr float drive = 1.55f;
        const float driveScale = 0.86f / std::tanh(drive);

        for (int frame = 0; frame < frames; ++frame) {
            const int index = frame * 2;
            const float mono = (samples[index] + samples[index + 1]) * 0.5f;
            highPass_ = highAlpha * (highPass_ + mono - previousInput_);
            previousInput_ = mono;
            lowPass_ += lowAlpha * (highPass_ - lowPass_);
            const float speaker = std::tanh(lowPass_ * drive) * driveScale;
            samples[index] = speaker;
            samples[index + 1] = speaker;
        }
    }

private:
    float previousInput_ = 0.0f;
    float highPass_ = 0.0f;
    float lowPass_ = 0.0f;
};

struct RenderSession {
    fxchain::smaf::MaPlayer player;
    PhoneSpeakerFilter phoneSpeaker;
    bool phoneSpeakerMode = false;
};

std::string initializePlayer(const uint8_t* bytes, size_t size,
                             fxchain::smaf::MaPlayer& player) {
    if (size < 12) return "MMFファイルが空か、短すぎます";

    fxchain::smaf::SmafFile file;
    if (!file.parse(bytes, size)) return "有効なSMAF/MMFファイルではありません";

    if (!player.init(file, kSampleRate)) return "再生可能なトラックがありません";
    return {};
}

void write16(std::ostream& stream, uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    stream.write(bytes, sizeof(bytes));
}

void write32(std::ostream& stream, uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    stream.write(bytes, sizeof(bytes));
}

void writeHeader(std::ostream& stream, uint32_t dataBytes) {
    stream.write("RIFF", 4);
    write32(stream, 36 + dataBytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    write32(stream, 16);
    write16(stream, 1);
    write16(stream, 2);
    write32(stream, kSampleRate);
    write32(stream, kSampleRate * 4);
    write16(stream, 4);
    write16(stream, 16);
    stream.write("data", 4);
    write32(stream, dataBytes);
}

std::string render(const uint8_t* bytes, size_t size, const char* outputPath,
                   bool phoneSpeakerMode) {
    fxchain::smaf::MaPlayer player;
    std::string initError = initializePlayer(bytes, size, player);
    if (!initError.empty()) return initError;

    std::fstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) return "一時音声ファイルを作成できません";
    writeHeader(output, 0);

    float samples[kBlockFrames * 2];
    std::array<char, kBlockFrames * 4> pcmBytes{};
    PhoneSpeakerFilter phoneSpeaker;
    uint64_t framesWritten = 0;
    float peak = 0.0f;
    while (true) {
        int count = player.render(samples, kBlockFrames);
        if (count <= 0) break;
        if (phoneSpeakerMode) phoneSpeaker.process(samples, count);
        for (int i = 0; i < count * 2; ++i) {
            float sample = std::clamp(samples[i], -1.0f, 1.0f);
            peak = std::max(peak, sample < 0.0f ? -sample : sample);
            int value = static_cast<int>(sample * 32767.0f);
            uint16_t encoded = static_cast<uint16_t>(static_cast<int16_t>(value));
            pcmBytes[i * 2] = static_cast<char>(encoded & 0xff);
            pcmBytes[i * 2 + 1] = static_cast<char>((encoded >> 8) & 0xff);
        }
        // One write per render block avoids millions of two-byte stream calls
        // on long songs while preserving the exact little-endian PCM bytes.
        output.write(pcmBytes.data(), static_cast<std::streamsize>(count) * 4);
        framesWritten += static_cast<uint64_t>(count);
    }

    if (!output.good()) return "一時音声ファイルの書き込みに失敗しました";
    if (framesWritten == 0 || peak < 0.0001f) return "音声データを生成できませんでした";
    if (framesWritten > UINT32_MAX / 4) return "曲が長すぎます";

    output.seekp(0, std::ios::beg);
    writeHeader(output, static_cast<uint32_t>(framesWritten * 4));
    output.close();
    return {};
}

jstring toJavaString(JNIEnv* env, const std::string& text) {
    return env->NewStringUTF(text.c_str());
}

bool copyJavaBytes(JNIEnv* env, jbyteArray source, std::vector<uint8_t>& destination) {
    if (source == nullptr) return false;
    jsize size = env->GetArrayLength(source);
    destination.resize(static_cast<size_t>(size));
    if (size > 0) {
        env->GetByteArrayRegion(source, 0, size,
                                reinterpret_cast<jbyte*>(destination.data()));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
    }
    return true;
}

std::string detectFormat(const uint8_t* bytes, size_t size) {
    fxchain::smaf::SmafFile file;
    if (!file.parse(bytes, size)) return {};

    int highestScoreFormat = -1;
    bool hasAudioTrack = false;
    for (const fxchain::smaf::TrackChunk& track : file.tracks) {
        if (track.isAudioTrack) {
            hasAudioTrack = true;
        } else {
            highestScoreFormat = std::max(highestScoreFormat, track.formatType);
        }
    }

    switch (highestScoreFormat) {
    case 0x03: return "MA-7 \xC2\xB7 F3";
    case 0x02: return "MA-3/5 \xC2\xB7 F2";
    case 0x01: return "MA-3/5 \xC2\xB7 F1";
    case 0x00: return "MA-1/2 \xC2\xB7 F0";
    default: return hasAudioTrack ? "SMAF \xC2\xB7 Audio" : "SMAF";
    }
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_app_mmfpocket_player_NativeMmfRenderer_detectFormat(
        JNIEnv* env, jclass, jbyteArray mmfData) {
    if (mmfData == nullptr) return toJavaString(env, {});

    std::vector<uint8_t> bytes;
    if (!copyJavaBytes(env, mmfData, bytes)) return toJavaString(env, {});
    try {
        return toJavaString(env, detectFormat(bytes.data(), bytes.size()));
    } catch (...) {
        return toJavaString(env, {});
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_app_mmfpocket_player_NativeMmfRenderer_renderToWav(
        JNIEnv* env, jclass, jbyteArray mmfData, jstring outputPath,
        jboolean phoneSpeakerMode) {
    if (mmfData == nullptr || outputPath == nullptr) {
        return toJavaString(env, "入力データがありません");
    }

    std::vector<uint8_t> bytes;
    if (!copyJavaBytes(env, mmfData, bytes))
        return toJavaString(env, "MMFデータを読み込めません");

    const char* path = env->GetStringUTFChars(outputPath, nullptr);
    if (path == nullptr) return toJavaString(env, "一時ファイルのパスを取得できません");

    std::string result;
    try {
        result = render(bytes.data(), bytes.size(), path, phoneSpeakerMode == JNI_TRUE);
    } catch (const std::exception& error) {
        result = std::string("変換に失敗しました: ") + error.what();
    } catch (...) {
        result = "変換中に不明なエラーが発生しました";
    }
    env->ReleaseStringUTFChars(outputPath, path);
    return toJavaString(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_app_mmfpocket_player_NativeMmfRenderer_createSession(
        JNIEnv* env, jclass, jbyteArray mmfData, jboolean phoneSpeakerMode,
        jlongArray sessionInfo) {
    if (mmfData == nullptr || sessionInfo == nullptr || env->GetArrayLength(sessionInfo) < 3)
        return toJavaString(env, "入力データがありません");

    std::vector<uint8_t> bytes;
    if (!copyJavaBytes(env, mmfData, bytes))
        return toJavaString(env, "MMFデータを読み込めません");

    try {
        auto session = std::make_unique<RenderSession>();
        std::string error = initializePlayer(bytes.data(), bytes.size(), session->player);
        if (!error.empty()) return toJavaString(env, error);
        session->phoneSpeakerMode = phoneSpeakerMode == JNI_TRUE;
        const jlong values[3] = {
            reinterpret_cast<jlong>(session.get()),
            static_cast<jlong>(session->player.scoreEndSamples()),
            static_cast<jlong>(session->player.totalSamples())
        };
        env->SetLongArrayRegion(sessionInfo, 0, 3, values);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return toJavaString(env, "再生セッションを作成できません");
        }
        session.release();
        return toJavaString(env, {});
    } catch (const std::exception& error) {
        return toJavaString(env, std::string("変換に失敗しました: ") + error.what());
    } catch (...) {
        return toJavaString(env, "変換中に不明なエラーが発生しました");
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_app_mmfpocket_player_NativeMmfRenderer_renderSession(
        JNIEnv* env, jclass, jlong handle, jshortArray stereoPcm) {
    auto* session = reinterpret_cast<RenderSession*>(handle);
    if (session == nullptr || stereoPcm == nullptr) return -1;
    const jsize sampleCapacity = env->GetArrayLength(stereoPcm);
    const int frameCapacity = std::min<int>(kBlockFrames, sampleCapacity / 2);
    if (frameCapacity <= 0) return -1;

    try {
        std::array<float, kBlockFrames * 2> samples{};
        std::array<jshort, kBlockFrames * 2> pcm{};
        const int count = session->player.render(samples.data(), frameCapacity);
        if (count <= 0) return 0;
        if (session->phoneSpeakerMode) session->phoneSpeaker.process(samples.data(), count);
        for (int i = 0; i < count * 2; ++i) {
            const float sample = std::clamp(samples[i], -1.0f, 1.0f);
            pcm[i] = static_cast<jshort>(static_cast<int>(sample * 32767.0f));
        }
        env->SetShortArrayRegion(stereoPcm, 0, count * 2, pcm.data());
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return -1;
        }
        return count;
    } catch (...) {
        return -1;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_app_mmfpocket_player_NativeMmfRenderer_destroySession(
        JNIEnv*, jclass, jlong handle) {
    delete reinterpret_cast<RenderSession*>(handle);
}
