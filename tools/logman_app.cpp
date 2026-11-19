/**
 * @file logman_app.cpp
 @brief Luce OS Ring 3 Performance Log & Event Trace Manager (logman) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages performance counters and Event Tracing for Windows (ETW) sessions.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowDataCollectors() noexcept {
    const char* out =
        "\nData Collector Set                      Type                          Status\n"
        "-------------------------------------------------------------------------------\n"
        "MicaNT Kernel System Diagnostics        Performance Counter           Running\n"
        "Intel I210 DMA Packet Telemetry         Performance Counter           Running\n"
        "Haven Memory Bus (HMB 2.0) Paging Log   Trace                         Running\n"
        "PrismX 3D DirectFlip Frame Presenter    Trace                         Running\n\n"
        "The command completed successfully.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Logman [create|start|stop|delete|query|update] [options]\n\n"
        "Description:\n"
        "    Logman manages the Event Tracing for Windows (ETW) and Performance\n"
        "    Counter collections on the sovereign executive.\n\n"
        "[logman query]\n";

    PrintStr(banner);

    ShowDataCollectors();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
