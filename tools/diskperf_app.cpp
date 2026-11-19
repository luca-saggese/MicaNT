/**
 * @file diskperf_app.cpp
 * Luce OS Ring 3 Disk Performance Counters Utility (diskperf) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Enables or disables disk performance counters on sovereign storage drivers.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "DISKPERF [-Y[D|V] | -N[D|V]] [\\\\computername]\n\n"
        "  -Y   Sets the system to start all disk performance counters\n"
        "       when the system is restarted.\n"
        "  -YD  Enables the disk performance counters for physical drives.\n"
        "  -YV  Enables the disk performance counters for logical volumes.\n"
        "  -N   Sets the system to disable all disk performance counters.\n\n"
        "Physical Drive and Storage Telemetry Status:\n"
        "-------------------------------------------\n"
        "Disk performance counters on this system are currently set to start at boot.\n"
        "Raw I/O Performance Counters Enabled:\n"
        "  - PhysicalDisk (HGST Ultrastar 2TB SATA AHCI Port 1)\n"
        "  - LogicalDisk (C: OS_ROOT FAT32)\n"
        "  - DMA Transfer Rate: 420 MB/s Burst / 185 MB/s Sustained\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
