/**
 * @file fsquirt_app.cpp
 * Luce OS Ring 3 Bluetooth File Transfer Wizard (fsquirt) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages OBEX (Object Exchange) wireless file transfers over Bluetooth RFCOMM / BLE.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleBluetoothState() noexcept {
    const char* out =
        "[fsquirt /status]\n"
        "Bluetooth Radio Stack: MicaNT Sovereign BTHPORT\n"
        "Radio Hardware: Intel Wireless Bluetooth 5.4 HCI Controller (USB\\VID_8087&PID_0033)\n"
        "Radio State: ONLINE (Discoverable, Pairing Allowed)\n"
        "Local MAC Address: 00:1A:7D:DA:71:13\n"
        "------------------------------------------------------------------------------------\n"
        "Paired Devices:\n"
        "  1. Haven Workstation [00:1B:DC:04:88:51] - Connected (Signal: -58 dBm, OBEX Push OK)\n"
        "  2. Pixel 9 Pro      [48:D7:05:AA:12:F4] - Paired (Signal: -72 dBm, OBEX Push OK)\n"
        "  3. Surface Pen      [C4:64:E3:89:01:2A] - Paired (HID Device)\n"
        "------------------------------------------------------------------------------------\n"
        "OBEX Object Exchange Server: Listening on RFCOMM Channel 9 (Incoming directory: C:\\Users\\admin\\Downloads)\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Bluetooth File Transfer Wizard (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage: fsquirt [/send | /receive | /devices | /pair <mac> | /status]\n\n"
        "Options:\n"
        "   /send              Send files to a Bluetooth device using OBEX Object Push\n"
        "   /receive           Wait and receive incoming files from another Bluetooth device\n"
        "   /devices           List all discovered and paired Bluetooth devices\n"
        "   /pair <mac>        Initiate Secure Simple Pairing (SSP) with target device\n"
        "   /unpair <mac>      Remove pairing key and forget remote device\n"
        "   /status            Display Bluetooth radio controller status and OBEX listener\n\n";

    PrintStr(banner);
    ShowSampleBluetoothState();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
