#pragma once

#include <cstddef>
#include <cstdint>

// Mode-neutral bootstrap PCM contract used by Ring 3 and the canonical HDA
// endpoint. Writes contain interleaved signed PCM frames; callers never pass
// device addresses or kernel pointers.
namespace micant::shared::audio {

inline constexpr char16_t HdaRenderDeviceName[] = u"\\Device\\HdaAudio0";
inline constexpr char16_t HdaCaptureDeviceName[] = u"\\Device\\HdaCapture0";
inline constexpr uint32_t SampleRate = 48000;
inline constexpr uint16_t Channels = 2;
inline constexpr uint16_t BitsPerSample = 16;
inline constexpr size_t BytesPerFrame = Channels * (BitsPerSample / 8);
inline constexpr size_t MaximumWriteBytes = 64 * 1024;
inline constexpr size_t MaximumReadBytes = 64 * 1024;
inline constexpr uint32_t CaptureFlagInputGainControl = 1u << 0;
inline constexpr uint32_t CaptureFlagInputMuteControl = 1u << 1;
inline constexpr uint32_t CaptureKnownFlags =
    CaptureFlagInputGainControl | CaptureFlagInputMuteControl;

struct CapturePacketHeader {
    uint64_t firstFrame{};
    uint64_t firstFramePerformanceCounter{};
    uint32_t pcmBytes{};
    uint32_t flags{}; // Hardware-supported gain and mute controls, reported separately.
};
static_assert(sizeof(CapturePacketHeader) == 24);

} // namespace micant::shared::audio
