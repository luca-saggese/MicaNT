/**
 * @file mmsys_app.cpp
 @brief Luce OS Ring 3 Sound & Audio Applet (mmsys.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Sound Control window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes audio endpoint telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 480;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_soundBuffer[WIN_W * WIN_H];

void RenderSoundWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_soundBuffer, WIN_W, WIN_H, "Sound", nullptr);

    // 2. Tabs Row: [Playback] [Recording] [Sounds] [Communications]
    const uint32_t tabY = 36;
    FillRect(s_soundBuffer, WIN_W, 20, tabY, 80, 22, PackBGRA(30, 42, 60));
    DrawRect(s_soundBuffer, WIN_W, 20, tabY, 80, 22, PackBGRA(0, 150, 255), 1);
    DrawString(s_soundBuffer, WIN_W, 28, tabY + 5, "Playback", PackBGRA(255, 255, 255));

    DrawString(s_soundBuffer, WIN_W, 115, tabY + 5, "Recording", PackBGRA(160, 180, 210));
    DrawString(s_soundBuffer, WIN_W, 210, tabY + 5, "Sounds", PackBGRA(160, 180, 210));
    DrawString(s_soundBuffer, WIN_W, 280, tabY + 5, "Communications", PackBGRA(160, 180, 210));

    FillRect(s_soundBuffer, WIN_W, 20, tabY + 22, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 3. Audio Endpoints List
    DrawString(s_soundBuffer, WIN_W, 20, 68, "Select a playback device below to modify its settings:", PackBGRA(220, 230, 245));

    FillRect(s_soundBuffer, WIN_W, 20, 88, WIN_W - 40, 170, PackBGRA(10, 14, 22));
    DrawRect(s_soundBuffer, WIN_W, 20, 88, WIN_W - 40, 170, PackBGRA(35, 48, 70), 1);

    // Item 1: Realtek High Definition Audio (Default Device)
    FillRect(s_soundBuffer, WIN_W, 22, 90, WIN_W - 44, 46, PackBGRA(0, 100, 180));
    // Speaker icon
    DrawCircle(s_soundBuffer, WIN_W, 44, 113, 12, PackBGRA(255, 255, 255));
    // Green checkmark badge
    DrawCircle(s_soundBuffer, WIN_W, 52, 122, 5, PackBGRA(0, 200, 100));
    DrawString(s_soundBuffer, WIN_W, 66, 98, "Speakers", PackBGRA(255, 255, 255));
    DrawString(s_soundBuffer, WIN_W, 66, 114, "Realtek High Definition Audio | Default Device", PackBGRA(200, 220, 255));

    // VU Volume Meter simulation on right
    for (int bar = 0; bar < 6; ++bar) {
        FillRect(s_soundBuffer, WIN_W, WIN_W - 65, 122 - bar * 4, 16, 2, (bar < 4) ? PackBGRA(0, 230, 100) : PackBGRA(255, 180, 0));
    }

    // Item 2: Intel Display Audio (HDMI)
    DrawCircle(s_soundBuffer, WIN_W, 44, 160, 12, PackBGRA(40, 56, 80));
    DrawString(s_soundBuffer, WIN_W, 66, 148, "Realtek Digital Output", PackBGRA(220, 230, 245));
    DrawString(s_soundBuffer, WIN_W, 66, 164, "Optical SPDIF Interface | Ready", PackBGRA(140, 160, 190));

    // Item 3: Sovereign WaveRT Virtual Endpoint
    DrawCircle(s_soundBuffer, WIN_W, 44, 210, 12, PackBGRA(40, 56, 80));
    DrawString(s_soundBuffer, WIN_W, 66, 198, "Prism Audio WaveRT Studio Bus", PackBGRA(220, 230, 245));
    DrawString(s_soundBuffer, WIN_W, 66, 214, "48000Hz 32-bit Float | 1333 us Buffer", PackBGRA(100, 240, 160));

    // 4. Action Buttons below list
    const uint32_t actY = 270;
    // Configure
    FillRect(s_soundBuffer, WIN_W, 20, actY, 80, 24, PackBGRA(30, 42, 60));
    DrawRect(s_soundBuffer, WIN_W, 20, actY, 80, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_soundBuffer, WIN_W, 28, actY + 6, "Configure", PackBGRA(200, 215, 235));

    // Set Default
    FillRect(s_soundBuffer, WIN_W, 260, actY, 90, 24, PackBGRA(30, 42, 60));
    DrawRect(s_soundBuffer, WIN_W, 260, actY, 90, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_soundBuffer, WIN_W, 270, actY + 6, "Set Default", PackBGRA(200, 215, 235));

    // Properties
    FillRect(s_soundBuffer, WIN_W, 360, actY, 80, 24, PackBGRA(30, 42, 60));
    DrawRect(s_soundBuffer, WIN_W, 360, actY, 80, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_soundBuffer, WIN_W, 372, actY + 6, "Properties", PackBGRA(200, 215, 235));

    // 5. Dialog Close Buttons
    const uint32_t dlgY = WIN_H - 40;
    FillRect(s_soundBuffer, WIN_W, WIN_W - 170, dlgY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_soundBuffer, WIN_W, WIN_W - 170, dlgY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_soundBuffer, WIN_W, WIN_W - 150, dlgY + 7, "OK", PackBGRA(255, 255, 255));

    FillRect(s_soundBuffer, WIN_W, WIN_W - 90, dlgY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_soundBuffer, WIN_W, WIN_W - 90, dlgY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_soundBuffer, WIN_W, WIN_W - 76, dlgY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sound Control Applet [mmsys.cpl / mmsys.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Audio Engine Subsystem : Sovereign WaveRT / XAudio2 Pipeline\n"
        "Active Audio Endpoints :\n"
        "  1. Speakers (Realtek High Definition Audio)       [DEFAULT] 48000Hz 24-bit\n"
        "  2. Realtek Digital Output (Optical SPDIF)         [READY]   44100Hz 16-bit\n"
        "  3. Prism Audio WaveRT Studio Bus                 [ACTIVE]  48000Hz 32-bit Float\n\n"
        "Audio Buffer Latency   : 1,333 microseconds (64 frames @ 48kHz, 0 underruns)\n"
        "Status: Presenting mmsys.cpl window (480x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSoundWindow();
    PresentWindow(s_soundBuffer, WIN_W, WIN_H);

    PrintStr("[MMSys] Audio endpoints presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
