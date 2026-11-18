#include "micant/shared/native_audio_api.hpp"
#include "micant/shared/audio_mixer.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/user_syscall.hpp"

#include <cstdint>

namespace {
using namespace micant;
constexpr Handle kCurrentProcessPseudoHandle = static_cast<Handle>(-1);
constexpr Handle kStandardOutput = static_cast<Handle>(-11);
constexpr uint32_t kFileWriteData = 0x0002;
constexpr uint32_t kFileOpen = 1;
constexpr uint32_t kFrames = 4096;
alignas(16) int16_t sourceA[kFrames * 2]{};
alignas(16) int16_t sourceB[kFrames]{};
alignas(16) int16_t sourceC[kFrames * 2]{};
alignas(16) int16_t pcm[kFrames * 2]{};
alignas(16) float accumulator[kFrames * 2]{};

bool verifyMixerControls() noexcept {
    constexpr int16_t monoSample[] = {16384};
    constexpr int16_t loudSample[] = {30000, 30000};
    int16_t output[2]{};
    float scratch[2]{};
    const shared::audio::MixerControls defaults{};
    const shared::audio::MixerVoice leftPan{
        monoSample, 1, 48000, 1, 1.0f, 1.0f, -1.0f, false, false};
    if (shared::audio::mixPcm16(&leftPan, 1, 48000, 1, defaults,
            scratch, 2, output, 2) != shared::audio::MixerStatus::Success ||
        output[0] != 16384 || output[1] != 0)
        return false;

    auto muted = leftPan;
    muted.sessionMuted = true;
    if (shared::audio::mixPcm16(&muted, 1, 48000, 1, defaults,
            scratch, 2, output, 2) != shared::audio::MixerStatus::Success ||
        output[0] != 0 || output[1] != 0)
        return false;

    const shared::audio::MixerVoice clippingVoices[] = {
        {loudSample, 2, 48000, 2, 1.0f, 1.0f, 0.0f, false, false},
        {loudSample, 2, 48000, 2, 1.0f, 1.0f, 0.0f, false, false},
    };
    if (shared::audio::mixPcm16(clippingVoices, 2, 48000, 1, defaults,
            scratch, 2, output, 2) != shared::audio::MixerStatus::Success ||
        output[0] != 32767 || output[1] != 32767)
        return false;

    const shared::audio::MixerControls masterMute{1.0f, true};
    return shared::audio::mixPcm16(&leftPan, 1, 48000, 1, masterMute,
               scratch, 2, output, 2) == shared::audio::MixerStatus::Success &&
           output[0] == 0 && output[1] == 0;
}

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
}

extern "C" void _start() noexcept {
    if (!verifyMixerControls()) exitProcess(NtStatus::InvalidParameter);
    UnicodeString deviceName(reinterpret_cast<const wchar_t*>(shared::audio::HdaRenderDeviceName));
    ObjectAttributes attributes{};
    attributes.objectName = &deviceName;
    attributes.attributes = OBJ_CASE_INSENSITIVE;
    Handle device{};
    IoStatusBlock ioStatus{};
    const auto openStatus = native_nt::NtCreateFile(
        &device, kFileWriteData, &attributes, &ioStatus, nullptr, 0, 0,
        kFileOpen, 0, nullptr, 0);
    if (openStatus != NtStatus::Success || ioStatus.status != NtStatus::Success)
        exitProcess(openStatus == NtStatus::Success ? ioStatus.status : openStatus);

    // Three independent client streams exercise format conversion and mixing
    // before sending the resulting PCM through the real HDA endpoint.
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const auto triangle = [frame](uint32_t rate, uint32_t frequency) {
            const uint32_t phase = static_cast<uint32_t>(
                (static_cast<uint64_t>(frame) * frequency * 65536u / rate) & 0xffffu);
            const int32_t ramp = phase < 32768u ? static_cast<int32_t>(phase)
                                                : static_cast<int32_t>(65535u - phase);
            return static_cast<int16_t>((ramp - 16384) * 2);
        };
        const int16_t a = triangle(48000, 440);
        const int16_t b = triangle(44100, 660);
        const int16_t c = triangle(32000, 880);
        sourceA[frame * 2] = a; sourceA[frame * 2 + 1] = a;
        sourceB[frame] = b;
        sourceC[frame * 2] = c; sourceC[frame * 2 + 1] = c;
    }

    const shared::audio::MixerVoice voices[] = {
        {sourceA, kFrames * 2, 48000, 2, 0.125f, 1.0f, 0.0f, false, false},
        {sourceB, kFrames, 44100, 1, 0.125f, 1.0f, 0.0f, false, false},
        {sourceC, kFrames * 2, 32000, 2, 0.125f, 1.0f, 0.0f, false, false},
    };
    const auto mixStatus = shared::audio::mixPcm16(
        voices, 3, shared::audio::SampleRate, kFrames, {}, accumulator,
        kFrames * 2, pcm, kFrames * 2);
    if (mixStatus != shared::audio::MixerStatus::Success || pcm[0] != -12288) {
        (void)native_nt::NtClose(device);
        exitProcess(NtStatus::InvalidParameter);
    }

    constexpr uint32_t bytes = sizeof(pcm);
    for (uint32_t submission = 0; submission < 3; ++submission) {
        ioStatus = {};
        const auto writeStatus = native_nt::NtWriteFile(
            device, 0, nullptr, nullptr, &ioStatus, pcm, bytes, nullptr, nullptr);
        if (submission == 0) {
            if (writeStatus != NtStatus::DeviceNotReady ||
                ioStatus.status != NtStatus::DeviceNotReady || ioStatus.information != 0) {
                (void)native_nt::NtClose(device);
                exitProcess(writeStatus == NtStatus::Success ? ioStatus.status : writeStatus);
            }
            continue;
        }
        if (writeStatus != NtStatus::Success || ioStatus.status != NtStatus::Success ||
            ioStatus.information != bytes) {
            (void)native_nt::NtClose(device);
            exitProcess(writeStatus == NtStatus::Success ? ioStatus.status : writeStatus);
        }
    }

    (void)native_nt::NtClose(device);
    constexpr char marker[] = "[MicaNT Audio] Three-stream converted mix submitted to HDA\r\n";
    print(marker, sizeof(marker) - 1);
    exitProcess(NtStatus::Success);
}
