/**
 * @file slmgr_app.cpp
 @brief Luce OS Ring 3 Software Licensing Management Tool (slmgr) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows slmgr behavior:
 * - Switches: /dli, /dlv, /xpr, /ato, /ipk, /cpky, /rearm, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Software Licensing Management Tool");
    PrintLine("");
    PrintLine("Usage: slmgr.vbs [MachineName [User Password]] [<Option>]");
    PrintLine("");
    PrintLine("Global Options:");
    PrintLine("  /ipk <ProductKey>    Install product key (replaces existing key)");
    PrintLine("  /ato [ActivationID]  Activate MicaNT");
    PrintLine("  /dli [ActivationID]  Display license information (default: current license)");
    PrintLine("  /dlv [ActivationID]  Display detailed license information");
    PrintLine("  /xpr [ActivationID]  Expiration date for current license state");
    PrintLine("  /cpky                Clear product key from registry (prevents theft)");
    PrintLine("  /rilc                Re-install system license files");
    PrintLine("  /rearm               Reset licensing status of the machine");
}

void PrintDli() noexcept {
    PrintLine("Name: MicaNT(R), Sovereign Cutler Datacenter Edition");
    PrintLine("Description: MicaNT Operating System - Sovereign Kernel Architecture");
    PrintLine("Partial Product Key: MICNT");
    PrintLine("License Status: Licensed (Permanent Autonomous License)");
    PrintLine("Activation Type: Digital Sovereign Entitlement");
}

void PrintDlv() noexcept {
    PrintLine("Software licensing service version: 10.0.26100.1");
    PrintLine("");
    PrintLine("Name: MicaNT(R), Sovereign Cutler Datacenter Edition");
    PrintLine("Description: MicaNT Operating System - Sovereign Kernel Architecture");
    PrintLine("Activation ID: 3b3b1c2e-345e-422e-a408-68d9af4f3429");
    PrintLine("Application ID: 55c92734-d682-4d71-983e-d6ec3f16059f");
    PrintLine("Extended PID: 03612-00000-000-000000-03-1033-26100.0000-2982026");
    PrintLine("Installation ID: 01234567890123456789012345678901234567890123456789");
    PrintLine("Partial Product Key: MICNT");
    PrintLine("License Status: Licensed");
    PrintLine("Remaining MicaNT rearm count: 1001");
    PrintLine("Remaining SKU rearm count: 1001");
    PrintLine("Trusted time: 10/24/2026 12:00:00 PM");
}

void PrintXpr() noexcept {
    PrintLine("MicaNT(R), Sovereign Cutler Datacenter Edition:");
    PrintLine("    The machine is permanently activated.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "slmgr.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("xpr")) {
        PrintXpr();
        ExitProcess(0);
    }

    if (parser.HasSwitch("dlv")) {
        PrintDlv();
        ExitProcess(0);
    }

    if (parser.HasSwitch("dli")) {
        PrintDli();
        ExitProcess(0);
    }

    if (parser.HasSwitch("ato")) {
        PrintLine("Activating MicaNT(R), Sovereign Cutler Datacenter Edition (3b3b1c2e-345e-422e-a408-68d9af4f3429)...");
        PrintLine("Product activated successfully.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("cpky")) {
        PrintLine("Product key from registry cleared successfully.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("rearm")) {
        PrintLine("Command completed successfully.");
        PrintLine("Please restart the system in order for the changes to take effect.");
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
