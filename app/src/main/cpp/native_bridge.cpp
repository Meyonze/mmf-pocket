// SPDX-License-Identifier: Apache-2.0

#include <jni.h>

#include "ma_player.h"
#include "smaf_file.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 44100;
constexpr int kBlockFrames = 1024;

class PhoneSpeakerFilter {
public:
    void process(float* samples, int frames) {
        constexpr float dt = 1.0f / static_cast<float>(kSampleRate);
        // Deliberately obvious small-handset-speaker coloration: mono, narrow
        // speech-band response, light sample-and-hold loss and soft saturation.
        // This models the speaker path only; it does not pretend to provide a
        // proprietary handset ROM instrument bank.
        constexpr float highPassHz = 480.0f;
        constexpr float lowPassHz = 3400.0f;
        constexpr float pi = 3.14159265358979323846f;
        constexpr float highRc = 1.0f / (2.0f * pi * highPassHz);
        constexpr float lowRc = 1.0f / (2.0f * pi * lowPassHz);
        constexpr float highAlpha = highRc / (highRc + dt);
        constexpr float lowAlpha = dt / (lowRc + dt);
        constexpr float drive = 2.8f;
        const float driveScale = 0.82f / std::tanh(drive);

        for (int frame = 0; frame < frames; ++frame) {
            const int index = frame * 2;
            const float mono = (samples[index] + samples[index + 1]) * 0.5f;
            // 22.05 kHz sample-and-hold makes the mode audible without adding
            // an artificial bit-crusher hiss.
            if ((sampleCounter_++ & 1u) == 0) heldInput_ = mono;
            highPass_ = highAlpha * (highPass_ + heldInput_ - previousInput_);
            previousInput_ = heldInput_;
            lowPass_ += lowAlpha * (highPass_ - lowPass_);
            const float speaker = std::tanh(lowPass_ * drive) * driveScale;
            samples[index] = speaker;
            samples[index + 1] = speaker;
        }
    }

private:
    float previousInput_ = 0.0f;
    float heldInput_ = 0.0f;
    float highPass_ = 0.0f;
    float lowPass_ = 0.0f;
    uint32_t sampleCounter_ = 0;
};

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
    if (size < 12) return "MMFファイルが空か、短すぎます";

    fxchain::smaf::SmafFile file;
    if (!file.parse(bytes, size)) return "有効なSMAF/MMFファイルではありません";

    const bool hasMa7Score = std::any_of(file.tracks.begin(), file.tracks.end(),
        [](const fxchain::smaf::TrackChunk& track) {
            return !track.isAudioTrack && track.formatType == 0x03;
        });
    if (hasMa7Score) return "MA-7形式（Format 3）は現在未対応です";

    fxchain::smaf::MaPlayer player;
    if (!player.init(file, kSampleRate)) return "再生可能なトラックがありません";

    std::fstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) return "一時音声ファイルを作成できません";
    writeHeader(output, 0);

    float samples[kBlockFrames * 2];
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
            write16(output, static_cast<uint16_t>(static_cast<int16_t>(value)));
        }
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

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_app_mmfpocket_player_NativeMmfRenderer_renderToWav(
        JNIEnv* env, jclass, jbyteArray mmfData, jstring outputPath,
        jboolean phoneSpeakerMode) {
    if (mmfData == nullptr || outputPath == nullptr) {
        return toJavaString(env, "入力データがありません");
    }

    jsize size = env->GetArrayLength(mmfData);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0) {
        env->GetByteArrayRegion(mmfData, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return toJavaString(env, "MMFデータを読み込めません");
        }
    }

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
