#include "micant/shared/native_audio_api.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/user_syscall.hpp"

#include <cstdint>
#include <cstddef>

extern "C" __attribute__((noinline)) void* memcpy(void* destination,
                                                    const void* source,
                                                    size_t bytes) {
    auto* output = static_cast<volatile uint8_t*>(destination);
    const auto* input = static_cast<const volatile uint8_t*>(source);
    for (size_t index = 0; index < bytes; ++index) output[index] = input[index];
    return destination;
}

namespace {
using namespace micant;
constexpr Handle kCurrentProcessPseudoHandle = static_cast<Handle>(-1);
constexpr Handle kStandardOutput = static_cast<Handle>(-11);
constexpr uint32_t kFileReadData = 0x0001;
constexpr uint32_t kFileOpen = 1;
constexpr uint32_t kFrames = 4096;
constexpr uint32_t kPacketBytes = sizeof(shared::audio::CapturePacketHeader) +
    kFrames * shared::audio::BytesPerFrame;
alignas(16) uint8_t capture[kPacketBytes]{};

void print(const char* message, uint32_t length) noexcept {
    IoStatusBlock ignored{};
    (void)native_nt::NtWriteFile(kStandardOutput, 0, nullptr, nullptr, &ignored,
                                  message, length, nullptr, nullptr);
}
[[noreturn]] void exitProcess(NtStatus status) noexcept {
    (void)sys::invokeUserService(0x01, kCurrentProcessPseudoHandle,
                                 static_cast<uint32_t>(status));
    for (;;) __asm__ __volatile__("pause" : : : "memory");
}
void printNumber(uint64_t value) noexcept {
    char text[24]{};
    size_t count{};
    do { text[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    for (size_t i = 0; i < count / 2; ++i) {
        const char temporary = text[i]; text[i] = text[count - i - 1]; text[count - i - 1] = temporary;
    }
    print(text, static_cast<uint32_t>(count));
}
}

extern "C" void _start() noexcept {
    UnicodeString deviceName(reinterpret_cast<const wchar_t*>(shared::audio::HdaCaptureDeviceName));
    ObjectAttributes attributes{};
    attributes.objectName = &deviceName;
    attributes.attributes = OBJ_CASE_INSENSITIVE;
    Handle device{};
    IoStatusBlock ioStatus{};
    const auto openStatus = native_nt::NtCreateFile(
        &device, kFileReadData, &attributes, &ioStatus, nullptr, 0, 0,
        kFileOpen, 0, nullptr, 0);
    if (openStatus != NtStatus::Success || ioStatus.status != NtStatus::Success)
        exitProcess(openStatus == NtStatus::Success ? ioStatus.status : openStatus);

    ioStatus = {};
    const auto rollbackStatus = native_nt::NtReadFile(
        device, 0, nullptr, nullptr, &ioStatus, capture, sizeof(capture), nullptr, nullptr);
    if (rollbackStatus != NtStatus::DeviceNotReady || ioStatus.status != NtStatus::DeviceNotReady ||
        ioStatus.information != 0) {
        (void)native_nt::NtClose(device);
        exitProcess(rollbackStatus == NtStatus::Success ? ioStatus.status : rollbackStatus);
    }
    constexpr char rollbackMarker[] = "[MicaNT Audio] Ring 3 observed the injected capture setup failure\r\n";
    print(rollbackMarker, sizeof(rollbackMarker) - 1);

    uint64_t lastFrame{}, lastTimestamp{};
    uint32_t capabilityFlags{};
    for (uint32_t submission = 0; submission < 2; ++submission) {
        ioStatus = {};
        const auto readStatus = native_nt::NtReadFile(
            device, 0, nullptr, nullptr, &ioStatus, capture, sizeof(capture), nullptr, nullptr);
        if (readStatus != NtStatus::Success || ioStatus.status != NtStatus::Success ||
            ioStatus.information != sizeof(capture)) {
            constexpr char errorMarker[] = "[MicaNT Audio] Ring 3 ADC read failed NTSTATUS=";
            print(errorMarker, sizeof(errorMarker) - 1);
            printNumber(static_cast<uint32_t>(readStatus == NtStatus::Success ? ioStatus.status : readStatus));
            constexpr char newline[] = "\r\n";
            print(newline, sizeof(newline) - 1);
            (void)native_nt::NtClose(device);
            exitProcess(readStatus == NtStatus::Success ? ioStatus.status : readStatus);
        }
        shared::audio::CapturePacketHeader packet{};
        const volatile auto* packetBytes = reinterpret_cast<const volatile uint8_t*>(capture);
        volatile auto* packetOutput = reinterpret_cast<volatile uint8_t*>(&packet);
        for (size_t index = 0; index < sizeof(packet); ++index)
            packetOutput[index] = packetBytes[index];
        if (packet.pcmBytes != kFrames * shared::audio::BytesPerFrame ||
            (packet.flags & ~shared::audio::CaptureKnownFlags) != 0 ||
            (submission && (packet.flags != capabilityFlags ||
                            packet.firstFrame != lastFrame + kFrames ||
                            packet.firstFramePerformanceCounter <= lastTimestamp))) {
            (void)native_nt::NtClose(device);
            exitProcess(NtStatus::DeviceDataError);
        }
        lastFrame = packet.firstFrame;
        lastTimestamp = packet.firstFramePerformanceCounter;
        capabilityFlags = packet.flags;
    }
    (void)native_nt::NtClose(device);
    constexpr char marker[] = "[MicaNT Audio] Ring 3 ADC DMA packets and timestamps received; input gain control=";
    print(marker, sizeof(marker) - 1);
    printNumber((capabilityFlags & shared::audio::CaptureFlagInputGainControl) != 0);
    constexpr char muteMarker[] = " mute control=";
    print(muteMarker, sizeof(muteMarker) - 1);
    printNumber((capabilityFlags & shared::audio::CaptureFlagInputMuteControl) != 0);
    constexpr char sourceMarker[] = "; source samples not asserted\r\n";
    print(sourceMarker, sizeof(sourceMarker) - 1);
    exitProcess(NtStatus::Success);
}
