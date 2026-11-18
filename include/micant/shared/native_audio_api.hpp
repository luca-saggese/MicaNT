#pragma once

#include <cstddef>
#include <cstdint>

// Mode-neutral bootstrap PCM contract used by Ring 3 and the canonical HDA
// endpoint. Writes contain interleaved signed PCM frames; callers never pass
// device addresses or kernel pointers.
namespace micant::shared::audio {

inline constexpr char16_t HdaRenderDeviceName[] = u"\\Device\\HdaAudio0";
inline constexpr uint32_t SampleRate = 48000;
inline constexpr uint16_t Channels = 2;
inline constexpr uint16_t BitsPerSample = 16;
inline constexpr size_t BytesPerFrame = Channels * (BitsPerSample / 8);
inline constexpr size_t MaximumWriteBytes = 64 * 1024;

} // namespace micant::shared::audio
