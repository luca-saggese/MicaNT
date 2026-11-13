#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::shared::native_process {

struct UnicodeString64 {
    uint16_t Length{};
    uint16_t MaximumLength{};
    uint32_t Padding{};
    const char16_t* Buffer{};
};

struct ProcessParametersPrefix64 {
    uint8_t Reserved[0x70]{};
    UnicodeString64 CommandLine{};
};

struct PebPrefix64 {
    uint8_t Reserved[0x20]{};
    ProcessParametersPrefix64* ProcessParameters{};
};

static_assert(sizeof(UnicodeString64) == 16);
static_assert(offsetof(ProcessParametersPrefix64, CommandLine) == 0x70);
static_assert(offsetof(PebPrefix64, ProcessParameters) == 0x20);

inline bool currentCommandLine(UnicodeString64& commandLine) noexcept {
    commandLine = {};
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x60) return false;
    const auto* peb = *reinterpret_cast<const PebPrefix64* const*>(teb + 0x60);
    if (!peb || peb->ProcessParameters == nullptr) return false;
    commandLine = peb->ProcessParameters->CommandLine;
    return commandLine.Buffer != nullptr && commandLine.Length != 0 &&
        (commandLine.Length & 1) == 0 && commandLine.Length <= commandLine.MaximumLength;
}

inline bool currentCommandLineAscii(char* destination, size_t capacity) noexcept {
    if (!destination || !capacity) return false;
    destination[0] = '\0';
    UnicodeString64 commandLine{};
    if (!currentCommandLine(commandLine)) return false;
    const size_t characters = commandLine.Length / sizeof(char16_t);
    if (characters >= capacity) return false;
    for (size_t i = 0; i < characters; ++i) {
        const char16_t value = commandLine.Buffer[i];
        if (value > 0x7f) return false;
        destination[i] = static_cast<char>(value);
    }
    destination[characters] = '\0';
    return true;
}

} // namespace micant::shared::native_process
