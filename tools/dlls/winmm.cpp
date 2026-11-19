/**
 * Minimal, native WinMM timer surface.
 * Audio APIs report unavailable until PCM playback is connected to the HDA
 * endpoint; this DLL never reports success without submitting audio.
 */
#include <cstdint>
#include <cstddef>

extern "C" __declspec(dllimport) uint32_t __stdcall GetTickCount() noexcept;

struct WinNtTimeCaps { uint32_t wPeriodMin, wPeriodMax; };

#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

extern "C" {
__declspec(dllexport) uint32_t timeBeginPeriod(uint32_t period) noexcept {
    return period ? 97u /* TIMERR_NOCANDO: no adjustable WinMM resolution backend */ : 97u;
}
__declspec(dllexport) uint32_t timeEndPeriod(uint32_t period) noexcept {
    return period ? 97u : 97u;
}
__declspec(dllexport) uint32_t timeGetTime() noexcept {
    return GetTickCount();
}
__declspec(dllexport) uint32_t timeGetDevCaps(
    WinNtTimeCaps* caps,
    uint32_t size) noexcept {
    if (!caps || size < sizeof(*caps)) return 97u;
    caps->wPeriodMin = 15;
    caps->wPeriodMax = 1000;
    return 0;
}
__declspec(dllexport) uint32_t timeSetEvent(uint32_t, uint32_t, void*, uintptr_t, uint32_t) noexcept {
    return 0;
}
__declspec(dllexport) uint32_t timeKillEvent(uint32_t timer) noexcept {
    return timer ? 97u : 97u;
}
__declspec(dllexport) int32_t PlaySoundW(const wchar_t*, void*, uint32_t) noexcept {
    return 0;
}
__declspec(dllexport) uint32_t waveOutGetNumDevs() noexcept {
    return 0;
}
}
