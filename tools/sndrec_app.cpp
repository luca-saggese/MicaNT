/**
 * @file sndrec_app.cpp
 * Luce OS Ring 3 Sound Recorder Utility (sndrec32) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Sound Recorder with green oscilloscope waveform display to GOP VRAM,
 * writes audio stream telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 400;
constexpr uint32_t WIN_H = 220;
alignas(4096) uint32_t s_sndBuffer[WIN_W * WIN_H];

void RenderSoundRecorderWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_sndBuffer, WIN_W, WIN_H, "Sound - Sound Recorder", "File  Edit  Effects  Help");

    // 2. Oscilloscope / Waveform Visualizer Display (Y=52 to 118)
    const uint32_t oscX = 16;
    const uint32_t oscY = 52;
    const uint32_t oscW = WIN_W - 32;
    const uint32_t oscH = 66;

    // Pure black CRT scope background
    FillRect(s_sndBuffer, WIN_W, oscX, oscY, oscW, oscH, PackBGRA(4, 8, 12));
    DrawRect(s_sndBuffer, WIN_W, oscX, oscY, oscW, oscH, PackBGRA(30, 50, 75), 1);

    // Center baseline grid line
    const uint32_t midY = oscY + oscH / 2;
    DrawLine(s_sndBuffer, WIN_W, oscX, midY, oscX + oscW, midY, PackBGRA(10, 40, 20));

    // Green audio waveform simulation (Audio sine wave + harmonics)
    for (uint32_t x = 0; x < oscW - 1; ++x) {
        int32_t amp = 0;
        if (x > 40 && x < oscW - 40) {
            amp = static_cast<int32_t>(18 * ((x % 24 < 12) ? 1 : -1) * ((x % 7 == 0) ? 1 : 0));
            if (amp == 0) amp = static_cast<int32_t>(12 * ((x % 16 < 8) ? 1 : -1));
        }
        int32_t y1 = midY + amp;
        int32_t y2 = midY - amp;
        DrawLine(s_sndBuffer, WIN_W, oscX + x, y1, oscX + x, y2, PackBGRA(0, 255, 100));
    }

    // 3. Audio Time Position & Length
    DrawString(s_sndBuffer, WIN_W, 20, 126, "Position:", PackBGRA(160, 180, 210));
    DrawString(s_sndBuffer, WIN_W, 80, 126, "0.00 sec.", PackBGRA(255, 255, 255));

    DrawString(s_sndBuffer, WIN_W, WIN_W - 150, 126, "Length:", PackBGRA(160, 180, 210));
    DrawString(s_sndBuffer, WIN_W, WIN_W - 90, 126, "60.00 sec.", PackBGRA(255, 255, 255));

    // Progress slider bar
    FillRect(s_sndBuffer, WIN_W, 20, 142, WIN_W - 40, 6, PackBGRA(12, 16, 24));
    DrawRect(s_sndBuffer, WIN_W, 20, 142, WIN_W - 40, 6, PackBGRA(40, 56, 80), 1);
    // Slider thumb
    FillRect(s_sndBuffer, WIN_W, 20, 139, 8, 12, PackBGRA(0, 180, 255));

    // 4. Transport Control Buttons (Bottom Row)
    const uint32_t btnY = WIN_H - 46;
    const uint32_t btnW = 54;
    const uint32_t btnH = 26;

    // [<<] Seek to Start
    FillRect(s_sndBuffer, WIN_W, 20, btnY, btnW, btnH, PackBGRA(30, 42, 60));
    DrawRect(s_sndBuffer, WIN_W, 20, btnY, btnW, btnH, PackBGRA(70, 90, 120), 1);
    DrawString(s_sndBuffer, WIN_W, 36, btnY + 7, "|<", PackBGRA(220, 235, 255));

    // [>>] Seek to End
    FillRect(s_sndBuffer, WIN_W, 80, btnY, btnW, btnH, PackBGRA(30, 42, 60));
    DrawRect(s_sndBuffer, WIN_W, 80, btnY, btnW, btnH, PackBGRA(70, 90, 120), 1);
    DrawString(s_sndBuffer, WIN_W, 96, btnY + 7, ">|", PackBGRA(220, 235, 255));

    // [Play] >
    FillRect(s_sndBuffer, WIN_W, 140, btnY, btnW, btnH, PackBGRA(0, 120, 215));
    DrawRect(s_sndBuffer, WIN_W, 140, btnY, btnW, btnH, PackBGRA(255, 255, 255), 1);
    DrawString(s_sndBuffer, WIN_W, 162, btnY + 7, ">", PackBGRA(255, 255, 255));

    // [Stop] []
    FillRect(s_sndBuffer, WIN_W, 200, btnY, btnW, btnH, PackBGRA(30, 42, 60));
    DrawRect(s_sndBuffer, WIN_W, 200, btnY, btnW, btnH, PackBGRA(70, 90, 120), 1);
    DrawString(s_sndBuffer, WIN_W, 222, btnY + 7, "[]", PackBGRA(220, 235, 255));

    // [Record] (Red dot)
    FillRect(s_sndBuffer, WIN_W, 260, btnY, btnW, btnH, PackBGRA(30, 42, 60));
    DrawRect(s_sndBuffer, WIN_W, 260, btnY, btnW, btnH, PackBGRA(70, 90, 120), 1);
    DrawCircle(s_sndBuffer, WIN_W, 287, btnY + 13, 6, PackBGRA(255, 50, 50));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sound Recorder Subsystem [sndrec32.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Audio Engine Subsystem : Sovereign WaveRT / XAudio2 Pipeline\n"
        "Sample Rate            : 48,000 Hz (48 kHz High-Definition Audio)\n"
        "Bit Depth / Channels   : 24-bit Stereo PCM (2 channels, 4 bytes/frame)\n"
        "Hardware Interface     : Intel High Definition Audio / Realtek ALC888S\n"
        "Status: Presenting sndrec32.exe window (400x220 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSoundRecorderWindow();
    PresentWindow(s_sndBuffer, WIN_W, WIN_H);

    PrintStr("[SndRec] Waveform oscilloscope presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
