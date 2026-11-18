#pragma once

#include <cstdint>
#include "micant/shared/ntdef.hpp"

namespace micant::km::audio::hda {

using Log = void (*)(const char*) noexcept;

// Boot-time A0.1 controller discovery/reset. Success means a real PCI HDA
// function was claimed, its measured BAR mapped, and GCTL reset completed.
// Codec discovery and PCM streaming are later gates and are not implied.
[[nodiscard]] bool initializeController(Log log) noexcept;

// A0.2 checked CORB/RIRB command transport and bounded codec topology census.
// It does not configure a playback route or publish a usable audio endpoint.
[[nodiscard]] bool discoverCodecTopology(Log log) noexcept;

// Opens the discovered render endpoint through the canonical Ob-owned file
// object path. Write access accepts bounded interleaved PCM only.
[[nodiscard]] NtStatus openAudioDevice(Handle* handle, uint32_t desiredAccess) noexcept;

// Opens the discovered capture endpoint through the canonical Ob-owned file
// object path. Reads return the bounded capture packet described by the shared ABI.
[[nodiscard]] NtStatus openCaptureDevice(Handle* handle, uint32_t desiredAccess) noexcept;

} // namespace micant::km::audio::hda
