// Native DOOMGeneric PCM output through LuceOS's established Ring 3 HDA file
// contract. Upstream engine notices remain in third_party/doomgeneric/LICENSE.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "micant/shared/native_audio_api.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/user_syscall.hpp"

extern "C" {
#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
}

extern "C" {
int use_libsamplerate{};
float libsamplerate_scale{0.65f};
void DG_MixMusic(int32_t* left, int32_t* right, size_t frames) noexcept;
}

namespace {
using namespace micant;
constexpr uint32_t kOutputRate = shared::audio::SampleRate;
constexpr uint32_t kTicksPerSecond = 35;
constexpr size_t kMaximumSliceFrames = (kOutputRate + kTicksPerSecond - 1) / kTicksPerSecond;
constexpr size_t kMaximumSoundChannels = 8;
constexpr size_t kMaximumCachedSounds = 256;
constexpr size_t kMaximumCachedBytes = 4 * 1024 * 1024;
constexpr uint32_t kFileWriteData = 0x0002;
constexpr uint32_t kFileOpen = 1;

struct CachedSound {
    int16_t* samples{};
    uint32_t frames{};
    uint32_t rate{};
};
struct PlayingSound {
    CachedSound* sound{};
    uint64_t positionQ32{};
    uint64_t stepQ32{};
    uint32_t volume{};
    uint32_t separation{127};
};

Handle g_device{};
PlayingSound g_channels[kMaximumSoundChannels]{};
CachedSound* g_cache[kMaximumCachedSounds]{};
size_t g_cacheCount{};
size_t g_cacheBytes{};
uint32_t g_frameRemainder{};
alignas(16) int32_t g_left[kMaximumSliceFrames]{};
alignas(16) int32_t g_right[kMaximumSliceFrames]{};
alignas(16) int16_t g_pcm[kMaximumSliceFrames * 2]{};
bool g_initialized{};
bool g_submissionReported{};
uint32_t g_updateCount{};

void report(const char* message) noexcept {
    if (!message) return;
    IoStatusBlock io{};
    (void)native_nt::NtWriteFile(static_cast<Handle>(-11), 0, nullptr, nullptr,
                                 &io, message,
                                 static_cast<uint32_t>(std::strlen(message)),
                                 nullptr, nullptr);
}

int getSoundLumpNumber(sfxinfo_t* sound) noexcept {
    if (!sound) return -1;
    if (sound->link) sound = sound->link;
    char name[9]{};
    const char* dehName = DEH_String(sound->name);
    size_t offset{};
    if (!dehName) return -1;
    if (sound->name[0] == 0) return -1;
    // The DOOM WAD convention uses DS-prefixed sound names.
    name[offset++] = 'd'; name[offset++] = 's';
    for (; offset < sizeof(name) - 1 && dehName[offset - 2]; ++offset) {
        char value = dehName[offset - 2];
        if (value >= 'a' && value <= 'z') value = static_cast<char>(value - ('a' - 'A'));
        name[offset] = value;
    }
    return W_CheckNumForName(name);
}

CachedSound* cacheSound(sfxinfo_t* info) noexcept {
    if (!info) return nullptr;
    if (info->driver_data) return static_cast<CachedSound*>(info->driver_data);
    if (info->lumpnum < 0) info->lumpnum = getSoundLumpNumber(info);
    if (info->lumpnum < 0 || g_cacheCount >= kMaximumCachedSounds) return nullptr;

    const size_t lumpLength = W_LumpLength(static_cast<unsigned int>(info->lumpnum));
    if (lumpLength < 64 || lumpLength > 512 * 1024) return nullptr;
    auto* lump = static_cast<const uint8_t*>(W_CacheLumpNum(info->lumpnum, PU_STATIC));
    if (!lump) return nullptr;
    const uint16_t rate = static_cast<uint16_t>(lump[2] | (uint16_t{lump[3]} << 8));
    const uint32_t declared = uint32_t{lump[4]} | (uint32_t{lump[5]} << 8) |
                             (uint32_t{lump[6]} << 16) | (uint32_t{lump[7]} << 24);
    if (lump[0] != 0x03 || lump[1] != 0 || rate < 4000 || rate > 48000 ||
        declared > lumpLength - 8 || declared <= 48 || declared < 32) {
        W_ReleaseLumpNum(info->lumpnum);
        return nullptr;
    }
    const uint32_t frames = declared - 32;
    const size_t sampleBytes = static_cast<size_t>(frames) * sizeof(int16_t);
    if (sampleBytes > kMaximumCachedBytes - g_cacheBytes) {
        W_ReleaseLumpNum(info->lumpnum);
        return nullptr;
    }
    auto* cached = static_cast<CachedSound*>(std::malloc(sizeof(CachedSound)));
    auto* samples = static_cast<int16_t*>(std::malloc(sampleBytes));
    if (!cached || !samples) {
        std::free(cached);
        std::free(samples);
        W_ReleaseLumpNum(info->lumpnum);
        return nullptr;
    }
    // DMX reserves 16 leading and 16 trailing sample bytes.
    const uint8_t* source = lump + 16;
    for (uint32_t i = 0; i < frames; ++i)
        samples[i] = static_cast<int16_t>((static_cast<int32_t>(source[i]) - 128) * 256);
    W_ReleaseLumpNum(info->lumpnum);
    *cached = {samples, frames, rate};
    g_cache[g_cacheCount++] = cached;
    g_cacheBytes += sampleBytes;
    info->driver_data = cached;
    return cached;
}

boolean initializeSound(boolean) noexcept {
    if (M_CheckParm("-nosound") || M_CheckParm("-nosfx")) return false;
    UnicodeString deviceName(reinterpret_cast<const wchar_t*>(shared::audio::HdaRenderDeviceName));
    ObjectAttributes attributes{};
    attributes.objectName = &deviceName;
    attributes.attributes = OBJ_CASE_INSENSITIVE;
    IoStatusBlock io{};
    const NtStatus status = native_nt::NtCreateFile(
        &g_device, kFileWriteData, &attributes, &io, nullptr, 0, 0,
        kFileOpen, 0, nullptr, 0);
    if (status != NtStatus::Success || io.status != NtStatus::Success) {
        g_device = 0;
        report("[MicaNT DoomAudio] HDA render endpoint open failed\r\n");
        return false;
    }
    std::memset(g_channels, 0, sizeof(g_channels));
    g_frameRemainder = 0;
    g_submissionReported = false;
    g_updateCount = 0;
    g_initialized = true;
    report("[MicaNT DoomAudio] HDA render endpoint opened\r\n");
    return true;
}

void shutdownSound() noexcept {
    if (g_device) (void)native_nt::NtClose(g_device);
    g_device = 0;
    for (size_t i = 0; i < g_cacheCount; ++i) {
        if (!g_cache[i]) continue;
        std::free(g_cache[i]->samples);
        std::free(g_cache[i]);
        g_cache[i] = nullptr;
    }
    g_cacheCount = g_cacheBytes = 0;
    std::memset(g_channels, 0, sizeof(g_channels));
    g_initialized = false;
}

int getSfxLumpNum(sfxinfo_t* sound) noexcept { return getSoundLumpNumber(sound); }

void updateSound() noexcept {
    if (!g_initialized || !g_device) return;
    if (++g_updateCount == 70)
        report("[MicaNT DoomAudio] Doom sound update loop is running\r\n");
    g_frameRemainder += kOutputRate;
    const size_t frames = g_frameRemainder / kTicksPerSecond;
    g_frameRemainder %= kTicksPerSecond;
    if (frames == 0 || frames > kMaximumSliceFrames) return;
    std::memset(g_left, 0, frames * sizeof(g_left[0]));
    std::memset(g_right, 0, frames * sizeof(g_right[0]));
    for (auto& channel : g_channels) {
        auto* sound = channel.sound;
        if (!sound || !channel.stepQ32) continue;
        for (size_t frame = 0; frame < frames; ++frame) {
            const uint64_t index64 = channel.positionQ32 >> 32;
            if (index64 >= sound->frames) { channel.sound = nullptr; break; }
            const uint32_t index = static_cast<uint32_t>(index64);
            const uint32_t next = index + 1 < sound->frames ? index + 1 : index;
            const uint32_t fraction = static_cast<uint32_t>(channel.positionQ32);
            const int64_t a = sound->samples[index];
            const int64_t b = sound->samples[next];
            const int32_t sample = static_cast<int32_t>(a + ((b - a) * fraction >> 32));
            const int32_t scaled = sample * static_cast<int32_t>(channel.volume) / 127;
            const uint32_t leftPan = channel.separation > 127
                ? 254 - channel.separation : 127;
            const uint32_t rightPan = channel.separation < 127
                ? channel.separation : 127;
            g_left[frame] += scaled * static_cast<int32_t>(leftPan) / 127;
            g_right[frame] += scaled * static_cast<int32_t>(rightPan) / 127;
            channel.positionQ32 += channel.stepQ32;
        }
    }
    DG_MixMusic(g_left, g_right, frames);
    for (size_t frame = 0; frame < frames; ++frame) {
        const size_t index = frame * 2;
        g_pcm[index] = static_cast<int16_t>(g_left[frame] > 32767 ? 32767 :
            g_left[frame] < -32768 ? -32768 : g_left[frame]);
        g_pcm[index + 1] = static_cast<int16_t>(g_right[frame] > 32767 ? 32767 :
            g_right[frame] < -32768 ? -32768 : g_right[frame]);
    }
    const uint32_t bytes = static_cast<uint32_t>(frames * shared::audio::BytesPerFrame);
    IoStatusBlock io{};
    const NtStatus status = native_nt::NtWriteFile(
        g_device, 0, nullptr, nullptr, &io, g_pcm, bytes, nullptr, nullptr);
    if (!g_submissionReported && status == NtStatus::Success &&
        io.status == NtStatus::Success && io.information == bytes) {
        constexpr char marker[] = "[MicaNT DoomAudio] HDA PCM submitted\r\n";
        IoStatusBlock consoleIo{};
        (void)native_nt::NtWriteFile(static_cast<Handle>(-11), 0, nullptr, nullptr,
                                     &consoleIo, marker, sizeof(marker) - 1, nullptr, nullptr);
        g_submissionReported = true;
    }
}

void updateSoundParams(int handle, int volume, int separation) noexcept {
    if (handle < 0 || static_cast<size_t>(handle) >= kMaximumSoundChannels) return;
    auto& channel = g_channels[handle];
    channel.volume = static_cast<uint32_t>(volume < 0 ? 0 : volume > 127 ? 127 : volume);
    channel.separation = static_cast<uint32_t>(separation < 0 ? 0 : separation > 254 ? 254 : separation);
}

int startSound(sfxinfo_t* info, int handle, int volume, int separation) noexcept {
    if (!g_initialized || handle < 0 || static_cast<size_t>(handle) >= kMaximumSoundChannels)
        return -1;
    CachedSound* sound = cacheSound(info);
    if (!sound) {
        report("[MicaNT DoomAudio] SFX WAD sample unavailable\r\n");
        return -1;
    }
    auto& channel = g_channels[handle];
    channel.sound = sound;
    channel.positionQ32 = 0;
    channel.stepQ32 = (static_cast<uint64_t>(sound->rate) << 32) / kOutputRate;
    updateSoundParams(handle, volume, separation);
    if (!g_submissionReported)
        report("[MicaNT DoomAudio] SFX playback started\r\n");
    return handle;
}

void stopSound(int handle) noexcept {
    if (handle >= 0 && static_cast<size_t>(handle) < kMaximumSoundChannels)
        g_channels[handle].sound = nullptr;
}

boolean soundIsPlaying(int handle) noexcept {
    if (handle < 0 || static_cast<size_t>(handle) >= kMaximumSoundChannels) return false;
    const auto& channel = g_channels[handle];
    return channel.sound && (channel.positionQ32 >> 32) < channel.sound->frames;
}

void precacheSounds(sfxinfo_t* sounds, int count) noexcept {
    if (!sounds || count < 0) return;
    for (int i = 0; i < count; ++i) (void)cacheSound(&sounds[i]);
}

snddevice_t g_soundDevices[]{SNDDEVICE_SB};
} // namespace

extern "C" sound_module_t DG_sound_module{
    g_soundDevices, 1, initializeSound, shutdownSound, getSfxLumpNum,
    updateSound, updateSoundParams, startSound, stopSound, soundIsPlaying, precacheSounds,
};
