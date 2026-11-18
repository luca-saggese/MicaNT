#pragma once

#include <cstdint>

namespace micant::km::audio::hda {

using Log = void (*)(const char*) noexcept;

// Boot-time A0.1 controller discovery/reset. Success means a real PCI HDA
// function was claimed, its measured BAR mapped, and GCTL reset completed.
// Codec discovery and PCM streaming are later gates and are not implied.
[[nodiscard]] bool initializeController(Log log) noexcept;

// A0.2 checked CORB/RIRB command transport and bounded codec topology census.
// It does not configure a playback route or publish a usable audio endpoint.
[[nodiscard]] bool discoverCodecTopology(Log log) noexcept;

} // namespace micant::km::audio::hda
