// Bounded MUS event playback with a small General MIDI-style software synth.
// Music and effects are mixed together before their shared HDA render write.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "micant/shared/native_audio_api.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/user_syscall.hpp"

extern "C" {
#include "doomtype.h"
#include "i_sound.h"
}

namespace {
using namespace micant;
constexpr uint32_t kSampleRate = shared::audio::SampleRate;
constexpr uint32_t kMusTicksPerSecond = 140;
constexpr size_t kMaximumSongBytes = 512 * 1024;
constexpr size_t kMaximumMusicEvents = 16384;
constexpr size_t kMusicVoiceCount = 32;
constexpr size_t kWaveTableSize = 1024;
constexpr uint32_t kEnvelopeOne = 65536;

enum class EventType : uint8_t {
    NoteOff, NoteOn, Pitch, Program, Volume, Pan, Expression,
    Sustain, AllNotesOff, AllSoundOff, ResetControllers,
};

struct MusicEvent {
    uint64_t frame{};
    uint8_t channel{};
    uint8_t type{};
    uint8_t first{};
    uint8_t second{};
};

struct Song {
    MusicEvent* events{};
    uint32_t eventCount{};
    uint64_t durationFrames{};
};

struct MusicChannel {
    uint8_t program{};
    uint8_t volume{127};
    uint8_t expression{127};
    uint8_t pan{64};
    uint16_t pitch{8192};
    bool sustain{};
};

struct MusicVoice {
    uint64_t phase{};
    uint64_t phaseStep{};
    uint32_t envelope{};
    uint32_t noise{1};
    uint32_t age{};
    uint8_t channel{};
    uint8_t note{};
    uint8_t velocity{};
    uint8_t waveform{};
    bool active{};
    bool keyDown{};
    bool releasing{};
    bool percussion{};
};

MusicChannel g_channels[16]{};
MusicVoice g_voices[kMusicVoiceCount]{};
uint64_t g_noteSteps[128]{};
int16_t g_waveTables[4][kWaveTableSize]{};
Song* g_currentSong{};
uint32_t g_eventIndex{};
uint64_t g_musicFrame{};
uint32_t g_voiceAge{};
uint32_t g_musicVolume{96};
bool g_musicReady{};
bool g_musicPlaying{};
bool g_musicLooping{};
bool g_musicPaused{};
bool g_registeredReported{};
bool g_playbackReported{};

void report(const char* message) noexcept {
    if (!message) return;
    IoStatusBlock io{};
    (void)native_nt::NtWriteFile(static_cast<Handle>(-11), 0, nullptr, nullptr,
        &io, message, static_cast<uint32_t>(std::strlen(message)), nullptr, nullptr);
}

uint16_t readLe16(const uint8_t* data) noexcept {
    return static_cast<uint16_t>(data[0] | (uint16_t{data[1]} << 8));
}

uint32_t readBe32(const uint8_t* data) noexcept {
    return (uint32_t{data[0]} << 24) | (uint32_t{data[1]} << 16) |
           (uint32_t{data[2]} << 8) | data[3];
}

bool appendEvent(Song* song, uint32_t& count, uint64_t tick, uint8_t channel,
                 EventType type, uint8_t first = 0, uint8_t second = 0) noexcept {
    if (count >= kMaximumMusicEvents) return false;
    auto& event = song->events[count++];
    event.frame = tick * kSampleRate / kMusTicksPerSecond;
    event.channel = channel;
    event.type = static_cast<uint8_t>(type);
    event.first = first;
    event.second = second;
    return true;
}

Song* parseSong(const void* input, size_t length) noexcept {
    if (!input || length < 16 || length > kMaximumSongBytes) return nullptr;
    const auto* data = static_cast<const uint8_t*>(input);
    if (length >= 22 && std::memcmp(data, "MThd", 4) == 0) {
        const uint32_t headerLength = readBe32(data + 4);
        const uint16_t trackCount = static_cast<uint16_t>((data[10] << 8) | data[11]);
        const uint16_t division = static_cast<uint16_t>((data[12] << 8) | data[13]);
        if (headerLength < 6 || headerLength > length - 8 || !trackCount ||
            trackCount > 64 || !division || (division & 0x8000)) return nullptr;
        auto* song = static_cast<Song*>(std::calloc(1, sizeof(Song)));
        if (!song) return nullptr;
        song->events = static_cast<MusicEvent*>(std::malloc(kMaximumMusicEvents * sizeof(MusicEvent)));
        if (!song->events) { std::free(song); return nullptr; }
        uint32_t count{};
        size_t position = 8 + headerLength;
        uint64_t maxTick{};
        bool valid = true;
        for (uint16_t track = 0; track < trackCount && valid; ++track) {
            if (position > length || length - position < 8 || std::memcmp(data + position, "MTrk", 4)) {
                valid = false; break;
            }
            const uint32_t trackLength = readBe32(data + position + 4);
            position += 8;
            if (trackLength > length - position) { valid = false; break; }
            const size_t trackEnd = position + trackLength;
            uint64_t tick{};
            uint8_t runningStatus{};
            while (position < trackEnd && valid) {
                uint64_t delta{};
                uint32_t shiftCount{};
                uint8_t value{};
                do {
                    if (position >= trackEnd || shiftCount++ >= 5) { valid = false; break; }
                    value = data[position++];
                    delta = (delta << 7) | (value & 0x7f);
                } while (value & 0x80);
                if (!valid || tick > UINT64_MAX - delta) { valid = false; break; }
                tick += delta;
                if (tick > maxTick) maxTick = tick;
                if (position >= trackEnd) { valid = false; break; }
                uint8_t status = data[position++];
                if (status < 0x80) {
                    if (runningStatus < 0x80 || runningStatus >= 0xf0) { valid = false; break; }
                    --position;
                    status = runningStatus;
                } else if (status < 0xf0) {
                    runningStatus = status;
                }
                if (status == 0xff) {
                    if (position >= trackEnd) { valid = false; break; }
                    const uint8_t meta = data[position++];
                    uint32_t size{}; uint32_t bytes{};
                    do {
                        if (position >= trackEnd || bytes++ >= 5) { valid = false; break; }
                        value = data[position++]; size = (size << 7) | (value & 0x7f);
                    } while (value & 0x80);
                    if (!valid || size > trackEnd - position) { valid = false; break; }
                    position += size;
                    if (meta == 0x2f) break;
                    continue;
                }
                if (status == 0xf0 || status == 0xf7) {
                    uint32_t size{}; uint32_t bytes{};
                    do {
                        if (position >= trackEnd || bytes++ >= 5) { valid = false; break; }
                        value = data[position++]; size = (size << 7) | (value & 0x7f);
                    } while (value & 0x80);
                    if (!valid || size > trackEnd - position) { valid = false; break; }
                    position += size; continue;
                }
                const uint8_t kind = status & 0xf0;
                const uint8_t channel = status & 0x0f;
                const uint8_t needed = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
                if (position + needed > trackEnd) { valid = false; break; }
                uint8_t a = data[position++];
                uint8_t b = needed == 2 ? data[position++] : 0;
                EventType eventType{};
                bool keep = true;
                switch (kind) {
                case 0x80: eventType = EventType::NoteOff; break;
                case 0x90: eventType = b ? EventType::NoteOn : EventType::NoteOff; break;
                case 0xb0:
                    if (a == 7) eventType = EventType::Volume;
                    else if (a == 10) eventType = EventType::Pan;
                    else if (a == 11) eventType = EventType::Expression;
                    else if (a == 64) eventType = EventType::Sustain;
                    else if (a == 120) eventType = EventType::AllSoundOff;
                    else if (a == 121) eventType = EventType::ResetControllers;
                    else if (a == 123) eventType = EventType::AllNotesOff;
                    else keep = false;
                    break;
                case 0xc0: eventType = EventType::Program; b = 0; break;
                case 0xe0: eventType = EventType::Pitch; a = static_cast<uint8_t>(((b << 7) | a) >> 6); b = 0; break;
                default: keep = false; break;
                }
                if (keep) {
                    if (count >= kMaximumMusicEvents) { valid = false; break; }
                    song->events[count++] = {tick, channel, static_cast<uint8_t>(eventType),
                                             a, b};
                }
            }
            position = trackEnd;
        }
        if (!valid || !count) { std::free(song->events); std::free(song); return nullptr; }
        std::sort(song->events, song->events + count, [](const MusicEvent& a, const MusicEvent& b) {
            return a.frame < b.frame;
        });
        for (uint32_t i = 0; i < count; ++i)
            song->events[i].frame = song->events[i].frame * kSampleRate / (uint64_t{division} * 2);
        song->eventCount = count;
        song->durationFrames = maxTick * kSampleRate / (uint64_t{division} * 2);
        if (!song->durationFrames) song->durationFrames = 1;
        return song;
    }
    if (data[0] != 'M' || data[1] != 'U' || data[2] != 'S' || data[3] != 0x1a)
        return nullptr;
    const size_t scoreLength = readLe16(data + 4);
    const size_t scoreStart = readLe16(data + 6);
    if (scoreStart < 16 || scoreStart > length || scoreLength > length - scoreStart)
        return nullptr;
    const size_t scoreEnd = scoreStart + scoreLength;

    auto* song = static_cast<Song*>(std::calloc(1, sizeof(Song)));
    if (!song) return nullptr;
    song->events = static_cast<MusicEvent*>(
        std::malloc(kMaximumMusicEvents * sizeof(MusicEvent)));
    if (!song->events) { std::free(song); return nullptr; }

    uint8_t velocities[16];
    std::memset(velocities, 127, sizeof(velocities));
    uint32_t count{};
    uint64_t tick{};
    size_t position = scoreStart;
    bool ended = false;
    while (position < scoreEnd && !ended) {
        bool lastInGroup = false;
        while (position < scoreEnd && !lastInGroup && !ended) {
            const uint8_t descriptor = data[position++];
            const uint8_t channel = descriptor & 0x0f;
            lastInGroup = (descriptor & 0x80) != 0;
            const uint8_t type = descriptor & 0x70;
            auto readByte = [&](uint8_t& value) noexcept {
                if (position >= scoreEnd) return false;
                value = data[position++];
                return true;
            };
            uint8_t a{}, b{};
            bool valid = true;
            switch (type) {
            case 0x00:
                valid = readByte(a) && appendEvent(song, count, tick, channel,
                    EventType::NoteOff, a & 0x7f);
                break;
            case 0x10:
                valid = readByte(a);
                if (valid && (a & 0x80)) {
                    valid = readByte(velocities[channel]);
                    velocities[channel] &= 0x7f;
                }
                if (valid)
                    valid = appendEvent(song, count, tick, channel, EventType::NoteOn,
                                        a & 0x7f, velocities[channel]);
                break;
            case 0x20:
                valid = readByte(a) && appendEvent(song, count, tick, channel,
                    EventType::Pitch, static_cast<uint8_t>(a));
                break;
            case 0x30:
                valid = readByte(a);
                if (valid) {
                    if (a == 10) valid = appendEvent(song, count, tick, channel, EventType::AllSoundOff);
                    else if (a == 11) valid = appendEvent(song, count, tick, channel, EventType::ResetControllers);
                    else if (a == 13) valid = appendEvent(song, count, tick, channel, EventType::AllNotesOff);
                    else valid = a == 12 || a == 14;
                }
                break;
            case 0x40:
                valid = readByte(a) && readByte(b);
                if (valid) {
                    switch (a) {
                    case 0: valid = appendEvent(song, count, tick, channel, EventType::Program, b & 0x7f); break;
                    case 3: valid = appendEvent(song, count, tick, channel, EventType::Volume, b & 0x7f); break;
                    case 4: valid = appendEvent(song, count, tick, channel, EventType::Pan, b & 0x7f); break;
                    case 5: valid = appendEvent(song, count, tick, channel, EventType::Expression, b & 0x7f); break;
                    case 8: valid = appendEvent(song, count, tick, channel, EventType::Sustain, b != 0); break;
                    default: valid = a <= 9; break; // Bank/modulation/reverb/chorus are safely ignored.
                    }
                }
                break;
            case 0x60:
                ended = true;
                break;
            default:
                valid = false;
                break;
            }
            if (!valid) {
                std::free(song->events);
                std::free(song);
                return nullptr;
            }
        }
        if (ended) break;
        if (lastInGroup) {
            uint64_t delay{};
            uint32_t bytesRead{};
            uint8_t value{};
            do {
                if (position >= scoreEnd || bytesRead++ >= 5) {
                    std::free(song->events);
                    std::free(song);
                    return nullptr;
                }
                value = data[position++];
                if (delay > (UINT64_MAX >> 7)) {
                    std::free(song->events);
                    std::free(song);
                    return nullptr;
                }
                delay = (delay << 7) | (value & 0x7f);
            } while (value & 0x80);
            if (tick > UINT64_MAX - delay) {
                std::free(song->events);
                std::free(song);
                return nullptr;
            }
            tick += delay;
        }
    }
    if (!ended || count == 0) {
        std::free(song->events);
        std::free(song);
        return nullptr;
    }
    song->eventCount = count;
    song->durationFrames = tick * kSampleRate / kMusTicksPerSecond;
    if (!song->durationFrames) song->durationFrames = 1;
    return song;
}

void initializeSynthesis() noexcept {
    constexpr double tau = 6.28318530717958647692;
    constexpr double semitoneRatio = 1.05946309435929526456;
    for (size_t index = 0; index < kWaveTableSize; ++index) {
        (void)tau;
        const int32_t phase = static_cast<int32_t>(index);
        const int32_t triangle = phase < 256 ? phase * 43 :
            phase < 768 ? 21930 - (phase - 256) * 43 : -21930 + (phase - 768) * 43;
        const int32_t square = phase < 512 ? 11000 : -11000;
        const int32_t stepped = ((phase * 11000 / 256) % 22000) - 11000;
        const int32_t pulse = (phase % 341) < 170 ? 9000 : -9000;
        g_waveTables[0][index] = static_cast<int16_t>(triangle);
        g_waveTables[1][index] = static_cast<int16_t>(square);
        g_waveTables[2][index] = static_cast<int16_t>(stepped);
        g_waveTables[3][index] = static_cast<int16_t>(pulse);
    }
    double frequency = 440.0;
    g_noteSteps[69] = static_cast<uint64_t>(frequency * 4294967296.0 / kSampleRate);
    for (int note = 70; note < 128; ++note) {
        frequency *= semitoneRatio;
        g_noteSteps[note] = static_cast<uint64_t>(frequency * 4294967296.0 / kSampleRate);
    }
    frequency = 440.0;
    for (int note = 68; note >= 0; --note) {
        frequency /= semitoneRatio;
        g_noteSteps[note] = static_cast<uint64_t>(frequency * 4294967296.0 / kSampleRate);
    }
}

uint8_t selectWaveform(uint8_t program) noexcept {
    if (program < 8) return 1;             // Piano family.
    if (program < 16) return 0;            // Chromatic percussion.
    if (program < 24) return 2;            // Organ family.
    if (program < 40) return 3;            // Guitar and bass.
    if (program < 56) return 1;            // Strings and ensemble.
    if (program < 64) return 3;            // Brass.
    if (program < 80) return 2;            // Reed and pipe.
    return static_cast<uint8_t>((program / 8) % 4);
}

uint64_t calculateStep(uint8_t note, uint16_t pitch) noexcept {
    const int32_t bendQ16 = 65536 +
        (static_cast<int32_t>(pitch) - 8192) * 151 / 32768;
    return g_noteSteps[note] * static_cast<uint32_t>(bendQ16) / 65536;
}

void resetPlaybackChannels() noexcept {
    for (auto& channel : g_channels) channel = MusicChannel{};
    for (auto& voice : g_voices) voice = MusicVoice{};
    g_voiceAge = 0;
}

void releaseChannel(uint8_t channel, bool immediate) noexcept {
    for (auto& voice : g_voices) {
        if (!voice.active || voice.channel != channel) continue;
        voice.keyDown = false;
        if (immediate) voice.active = false;
        else voice.releasing = true;
    }
}

void applyEvent(const MusicEvent& event) noexcept {
    auto& channel = g_channels[event.channel];
    const auto type = static_cast<EventType>(event.type);
    switch (type) {
    case EventType::NoteOff:
        for (auto& voice : g_voices) {
            if (!voice.active || voice.channel != event.channel || voice.note != event.first)
                continue;
            voice.keyDown = false;
            if (!channel.sustain) voice.releasing = true;
        }
        break;
    case EventType::NoteOn: {
        if (!event.second) {
            MusicEvent noteOff = event;
            noteOff.type = static_cast<uint8_t>(EventType::NoteOff);
            applyEvent(noteOff);
            break;
        }
        MusicVoice* voice{};
        for (auto& candidate : g_voices) {
            if (candidate.active && candidate.channel == event.channel &&
                candidate.note == event.first) { voice = &candidate; break; }
        }
        if (!voice) {
            for (auto& candidate : g_voices)
                if (!candidate.active) { voice = &candidate; break; }
        }
        if (!voice) {
            voice = &g_voices[0];
            for (auto& candidate : g_voices)
                if (candidate.releasing && (!voice->releasing || candidate.age < voice->age))
                    voice = &candidate;
                else if (!voice->releasing && candidate.age < voice->age)
                    voice = &candidate;
        }
        *voice = MusicVoice{};
        voice->active = true;
        voice->keyDown = true;
        voice->channel = event.channel;
        voice->note = event.first;
        voice->velocity = event.second;
        voice->waveform = selectWaveform(channel.program);
        voice->phaseStep = calculateStep(voice->note, channel.pitch);
        voice->age = ++g_voiceAge;
        voice->percussion = event.channel == 15 || event.channel == 9;
        voice->noise = 0x9e3779b9u ^ (uint32_t{event.first} << 16) ^ g_voiceAge;
        break;
    }
    case EventType::Pitch:
        channel.pitch = static_cast<uint16_t>(event.first * 64u);
        for (auto& voice : g_voices)
            if (voice.active && voice.channel == event.channel)
                voice.phaseStep = calculateStep(voice.note, channel.pitch);
        break;
    case EventType::Program:
        channel.program = event.first;
        break;
    case EventType::Volume:
        channel.volume = event.first;
        break;
    case EventType::Pan:
        channel.pan = event.first;
        break;
    case EventType::Expression:
        channel.expression = event.first;
        break;
    case EventType::Sustain:
        channel.sustain = event.first != 0;
        if (!channel.sustain)
            for (auto& voice : g_voices)
                if (voice.active && voice.channel == event.channel && !voice.keyDown)
                    voice.releasing = true;
        break;
    case EventType::AllNotesOff:
        releaseChannel(event.channel, false);
        break;
    case EventType::AllSoundOff:
        releaseChannel(event.channel, true);
        break;
    case EventType::ResetControllers:
        channel.volume = channel.expression = 127;
        channel.pan = 64;
        channel.pitch = 8192;
        channel.sustain = false;
        break;
    }
}

void stopPlayback() noexcept {
    g_musicPlaying = false;
    g_musicPaused = false;
    g_currentSong = nullptr;
    g_eventIndex = 0;
    g_musicFrame = 0;
    resetPlaybackChannels();
}

boolean initializeMusic() noexcept {
    if (!g_musicReady) initializeSynthesis();
    g_musicReady = true;
    return true;
}

void shutdownMusic() noexcept { stopPlayback(); }
void setMusicVolume(int volume) noexcept {
    g_musicVolume = static_cast<uint32_t>(volume < 0 ? 0 : volume > 127 ? 127 : volume);
}
void pauseMusic() noexcept { g_musicPaused = true; }
void resumeMusic() noexcept { g_musicPaused = false; }

void* registerSong(void* data, int length) noexcept {
    if (!g_musicReady || length <= 0) return nullptr;
    auto* song = parseSong(data, static_cast<size_t>(length));
    if (!song) {
        if (!g_registeredReported) {
            report("[MicaNT DoomMusic] MUS track rejected as malformed or unsupported\r\n");
            g_registeredReported = true;
        }
        return nullptr;
    }
    if (!g_registeredReported) {
        report("[MicaNT DoomMusic] MUS track registered\r\n");
        g_registeredReported = true;
    }
    return song;
}

void unregisterSong(void* handle) noexcept {
    auto* song = static_cast<Song*>(handle);
    if (!song) return;
    if (song == g_currentSong) stopPlayback();
    std::free(song->events);
    std::free(song);
}

void playSong(void* handle, boolean looping) noexcept {
    auto* song = static_cast<Song*>(handle);
    if (!song || !g_musicReady) return;
    resetPlaybackChannels();
    g_currentSong = song;
    g_eventIndex = 0;
    g_musicFrame = 0;
    g_musicLooping = looping != 0;
    g_musicPaused = false;
    g_musicPlaying = true;
    if (!g_playbackReported) {
        report("[MicaNT DoomMusic] MUS playback started\r\n");
        g_playbackReported = true;
    }
}

void stopSong() noexcept {
    g_musicPlaying = false;
    g_musicPaused = false;
    g_currentSong = nullptr;
    g_eventIndex = 0;
    g_musicFrame = 0;
    resetPlaybackChannels();
}

boolean musicIsPlaying() noexcept { return g_musicPlaying; }
void pollMusic() noexcept {}

snddevice_t g_musicDevices[]{SNDDEVICE_GENMIDI, SNDDEVICE_SB, SNDDEVICE_ADLIB};
} // namespace

extern "C" music_module_t DG_music_module{
    g_musicDevices, 3, initializeMusic, shutdownMusic, setMusicVolume,
    pauseMusic, resumeMusic, registerSong, unregisterSong, playSong,
    stopSong, musicIsPlaying, pollMusic,
};

extern "C" void DG_MixMusic(int32_t* left, int32_t* right, size_t frames) noexcept {
    if (!left || !right || !g_musicReady || g_musicPaused) return;
    for (size_t frame = 0; frame < frames; ++frame) {
        if (g_musicPlaying && g_currentSong) {
            while (g_eventIndex < g_currentSong->eventCount &&
                   g_currentSong->events[g_eventIndex].frame <= g_musicFrame)
                applyEvent(g_currentSong->events[g_eventIndex++]);
            if (g_musicFrame >= g_currentSong->durationFrames) {
                if (g_musicLooping && g_currentSong->durationFrames) {
                    g_eventIndex = 0;
                    g_musicFrame = 0;
                    resetPlaybackChannels();
                    while (g_eventIndex < g_currentSong->eventCount &&
                           g_currentSong->events[g_eventIndex].frame == 0)
                        applyEvent(g_currentSong->events[g_eventIndex++]);
                } else {
                    g_musicPlaying = false;
                    for (auto& voice : g_voices)
                        if (voice.active) { voice.keyDown = false; voice.releasing = true; }
                }
            }
        }

        for (auto& voice : g_voices) {
            if (!voice.active) continue;
            if (voice.releasing) {
                const uint32_t release = voice.percussion ? 48u : 18u;
                if (voice.envelope <= release) {
                    voice = MusicVoice{};
                    continue;
                }
                voice.envelope -= release;
            } else if (voice.envelope < kEnvelopeOne) {
                voice.envelope = voice.envelope + 240u > kEnvelopeOne
                    ? kEnvelopeOne : voice.envelope + 240u;
            }

            int32_t sample{};
            if (voice.percussion) {
                uint32_t noise = voice.noise;
                noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                voice.noise = noise;
                sample = static_cast<int32_t>((noise >> 16) & 0xffffu) - 32768;
                sample = sample * 3800 / 32768;
                if (voice.note >= 42) sample /= 2;
            } else {
                const size_t index = static_cast<size_t>(voice.phase >> 22) & (kWaveTableSize - 1);
                sample = g_waveTables[voice.waveform][index];
                voice.phase += voice.phaseStep;
            }
            sample = static_cast<int32_t>(static_cast<int64_t>(sample) * voice.envelope / kEnvelopeOne);
            const auto& channel = g_channels[voice.channel];
            sample = sample * static_cast<int32_t>(voice.velocity) / 127;
            sample = sample * static_cast<int32_t>(channel.volume) / 127;
            sample = sample * static_cast<int32_t>(channel.expression) / 127;
            sample = sample * static_cast<int32_t>(g_musicVolume) / 127;
            left[frame] += sample * static_cast<int32_t>(127 - channel.pan) / 127;
            right[frame] += sample * static_cast<int32_t>(channel.pan) / 127;
        }
        if (g_currentSong)
            ++g_musicFrame;
    }
}
