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
    uint32_t MaximumLength{};
    uint32_t Length{};
    uint32_t Flags{};
    uint32_t DebugFlags{};
    uintptr_t ConsoleHandle{};
    uint32_t ConsoleFlags{};
    uint32_t PaddingBeforeStandardHandles{};
    uintptr_t StandardInput{};
    uintptr_t StandardOutput{};
    uintptr_t StandardError{};
    UnicodeString64 CurrentDirectoryDosPath{};
    uintptr_t CurrentDirectoryHandle{};
    UnicodeString64 DllPath{};
    UnicodeString64 ImagePathName{};
    UnicodeString64 CommandLine{};
    char16_t* Environment{};
};

struct PebPrefix64 {
    uint8_t Reserved[0x20]{};
    ProcessParametersPrefix64* ProcessParameters{};
};

static_assert(sizeof(UnicodeString64) == 16);
static_assert(offsetof(ProcessParametersPrefix64, CurrentDirectoryDosPath) == 0x38);
static_assert(offsetof(ProcessParametersPrefix64, DllPath) == 0x50);
static_assert(offsetof(ProcessParametersPrefix64, ImagePathName) == 0x60);
static_assert(offsetof(ProcessParametersPrefix64, CommandLine) == 0x70);
static_assert(offsetof(ProcessParametersPrefix64, Environment) == 0x80);
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

inline bool currentEnvironment(char16_t*& environment) noexcept {
    environment = nullptr;
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x60) return false;
    const auto* peb = *reinterpret_cast<const PebPrefix64* const*>(teb + 0x60);
    if (!peb || peb->ProcessParameters == nullptr) return false;
    environment = peb->ProcessParameters->Environment;
    return environment != nullptr;
}

inline bool currentImagePath(UnicodeString64& imagePath) noexcept {
    imagePath = {};
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x60) return false;
    const auto* peb = *reinterpret_cast<const PebPrefix64* const*>(teb + 0x60);
    if (!peb || peb->ProcessParameters == nullptr) return false;
    imagePath = peb->ProcessParameters->ImagePathName;
    return imagePath.Buffer != nullptr && imagePath.Length >= 2 * sizeof(char16_t) &&
        (imagePath.Length & 1) == 0 && imagePath.Length <= imagePath.MaximumLength;
}

inline bool currentDirectory(UnicodeString64& directory) noexcept {
    directory = {};
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x60) return false;
    const auto* peb = *reinterpret_cast<const PebPrefix64* const*>(teb + 0x60);
    if (!peb || peb->ProcessParameters == nullptr) return false;
    directory = peb->ProcessParameters->CurrentDirectoryDosPath;
    return directory.Buffer != nullptr && directory.Length >= 2 * sizeof(char16_t) &&
        (directory.Length & 1) == 0 && directory.Length <= directory.MaximumLength;
}

enum class CreateState : uint32_t {
    Initial = 0,
    FailOnFileOpen = 1,
    FailOnSectionCreate = 2,
    FailExeFormat = 3,
    FailMachineMismatch = 4,
    FailExeName = 5,
    Success = 6,
    Maximum = 7
};

struct ProcessCreateInfo64 {
    uint64_t Size{};
    CreateState State{CreateState::Initial};
    uint32_t Padding{};
    union {
        struct { uint32_t InitFlags{}; uint32_t AdditionalFileAccess{}; } Initial;
        struct { uint64_t FileHandle{}; } FailSection;
        struct { uint16_t DllCharacteristics{}; uint8_t Padding[6]{}; } ExeFormat;
        struct { uint64_t IfeoKey{}; } ExeName;
        struct {
            uint32_t OutputFlags{};
            uint32_t Padding{};
            uint64_t FileHandle{};
            uint64_t SectionHandle{};
            uint64_t UserProcessParametersNative{};
            uint32_t UserProcessParametersWow64{};
            uint32_t CurrentParameterFlags{};
            uint64_t PebAddressNative{};
            uint32_t PebAddressWow64{};
            uint32_t Padding2{};
            uint64_t ManifestAddress{};
            uint32_t ManifestSize{};
            uint32_t Padding3{};
        } Success;
    } Data{};
};

static_assert(offsetof(ProcessCreateInfo64, State) == 8);
static_assert(offsetof(ProcessCreateInfo64, Data) == 16);
static_assert(offsetof(ProcessCreateInfo64, Data.Success.FileHandle) == 24);
static_assert(sizeof(ProcessCreateInfo64) == 88);

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
