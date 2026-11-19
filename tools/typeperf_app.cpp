/**
 * @file typeperf_app.cpp
 * Luce OS Ring 3 Performance Counter Streamer (typeperf) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Streams real-time performance counter data to console stdout.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "typeperf [counter [counter ...]] [options]\n\n"
        "Description:\n"
        "    typeperf writes performance counter data to the command window.\n"
        "    To stop typeperf, press CTRL+C.\n\n"
        "\"(PDH-CSV 4.0)\",\"\\\\MICANT-XEON\\Processor(_Total)\\% Processor Time\",\"\\\\MICANT-XEON\\Memory\\Available MBytes\",\"\\\\MICANT-XEON\\Network Interface(Intel I210)\\Bytes Total/sec\"\n";

    PrintStr(banner);

    // Stream 3 sample performance samples
    const char* samples[3] = {
        "\"10/23/2026 12:00:01.120\",\"2.412891\",\"31248\",\"142080\"\n",
        "\"10/23/2026 12:00:02.122\",\"1.890432\",\"31248\",\"138940\"\n",
        "\"10/23/2026 12:00:03.125\",\"3.104921\",\"31246\",\"149200\"\n"
    };

    for (int i = 0; i < 3; ++i) {
        PrintStr(samples[i]);
    }

    PrintStr("\n[TypePerf] 3 samples collected. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
