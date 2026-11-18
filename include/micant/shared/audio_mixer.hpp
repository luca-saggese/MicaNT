#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>

// Bounded, allocation-free PCM mixer shared by native Ring 3 audio clients.
// It retains no client pointers: every input buffer is consumed synchronously
// for the duration of mixPcm16().
namespace micant::shared::audio {

inline constexpr uint32_t MixerMaximumVoices = 32;
inline constexpr uint32_t MixerMaximumFrames = 8192;

struct MixerVoice {
    const int16_t* samples{};
    size_t sampleCount{}; // Interleaved scalar samples, not frames.
    uint32_t sampleRate{};
    uint16_t channels{};
    float streamVolume{1.0f};
    float sessionVolume{1.0f};
    float pan{}; // -1 left, 0 center, +1 right.
    bool streamMuted{};
    bool sessionMuted{};
};

struct MixerControls {
    float masterVolume{1.0f};
    bool masterMuted{};
};

enum class MixerStatus : uint8_t {
    Success,
    InvalidArgument,
    UnsupportedFormat,
    InvalidControl,
};

// Mix signed 16-bit mono/stereo PCM into signed 16-bit interleaved stereo.
// Resampling is linear and bounded. Output is zero-filled first, so an empty
// voice list is a well-defined silence/underrun result.
inline MixerStatus mixPcm16(const MixerVoice* voices, uint32_t voiceCount,
                            uint32_t outputRate, uint32_t frameCount,
                            const MixerControls& controls, float* accumulator,
                            size_t accumulatorSamples, int16_t* output,
                            size_t outputSamples) noexcept {
    if (!output || outputRate == 0 || frameCount == 0 ||
        frameCount > MixerMaximumFrames ||
        static_cast<size_t>(frameCount) > std::numeric_limits<size_t>::max() / 2 ||
        outputSamples < static_cast<size_t>(frameCount) * 2 || !accumulator ||
        accumulatorSamples < static_cast<size_t>(frameCount) * 2 ||
        voiceCount > MixerMaximumVoices || (voiceCount != 0 && !voices))
        return MixerStatus::InvalidArgument;
    if (!std::isfinite(controls.masterVolume) || controls.masterVolume < 0.0f ||
        controls.masterVolume > 4.0f)
        return MixerStatus::InvalidControl;
    for (uint32_t voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex) {
        const auto& voice = voices[voiceIndex];
        if (!voice.samples || voice.sampleRate < 8000 || voice.sampleRate > 192000 ||
            (voice.channels != 1 && voice.channels != 2) ||
            voice.sampleCount % voice.channels != 0)
            return MixerStatus::UnsupportedFormat;
        if (!std::isfinite(voice.streamVolume) || !std::isfinite(voice.sessionVolume) ||
            !std::isfinite(voice.pan) || voice.streamVolume < 0.0f ||
            voice.streamVolume > 16.0f || voice.sessionVolume < 0.0f ||
            voice.sessionVolume > 16.0f || voice.pan < -1.0f || voice.pan > 1.0f)
            return MixerStatus::InvalidControl;
    }

    for (uint32_t i = 0; i < frameCount * 2; ++i) accumulator[i] = 0.0f;
    if (controls.masterMuted) {
        for (uint32_t i = 0; i < frameCount * 2; ++i) output[i] = 0;
        return MixerStatus::Success;
    }
    for (uint32_t voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex) {
        const auto& voice = voices[voiceIndex];
        if (voice.streamMuted || voice.sessionMuted || voice.sampleCount == 0) continue;

        const size_t sourceFrames = voice.sampleCount / voice.channels;
        const float gain = voice.streamVolume * voice.sessionVolume * controls.masterVolume;
        const float leftPan = voice.pan > 0.0f ? 1.0f - voice.pan : 1.0f;
        const float rightPan = voice.pan < 0.0f ? 1.0f + voice.pan : 1.0f;
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            const uint64_t position = static_cast<uint64_t>(frame) * voice.sampleRate;
            const size_t sourceFrame = static_cast<size_t>(position / outputRate);
            if (sourceFrame >= sourceFrames) break; // End-of-buffer is silence.
            const size_t nextFrame = sourceFrame + 1 < sourceFrames ? sourceFrame + 1 : sourceFrame;
            const float fraction = static_cast<float>(position % outputRate) /
                                   static_cast<float>(outputRate);
            const auto sampleAt = [&](size_t sourceIndex, uint16_t channel) noexcept {
                const uint16_t sourceChannel = voice.channels == 1 ? 0 : channel;
                const float a = static_cast<float>(voice.samples[sourceIndex * voice.channels + sourceChannel]);
                const float b = static_cast<float>(voice.samples[nextFrame * voice.channels + sourceChannel]);
                return (a + (b - a) * fraction) / 32768.0f;
            };
            const float left = sampleAt(sourceFrame, 0) * gain * leftPan;
            const float right = sampleAt(sourceFrame, 1) * gain * rightPan;
            const size_t out = static_cast<size_t>(frame) * 2;
            accumulator[out] += left;
            accumulator[out + 1] += right;
        }
    }
    const auto quantize = [](float sample) noexcept {
                if (sample >= 1.0f) return static_cast<int16_t>(32767);
                if (sample <= -1.0f) return static_cast<int16_t>(-32768);
                return static_cast<int16_t>(sample * 32768.0f);
    };
    for (uint32_t i = 0; i < frameCount * 2; ++i) {
        output[i] = quantize(accumulator[i]);
    }
    return MixerStatus::Success;
}

} // namespace micant::shared::audio
