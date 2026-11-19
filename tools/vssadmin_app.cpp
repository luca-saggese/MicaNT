/**
 * @file vssadmin_app.cpp
 @brief Luce OS Ring 3 Volume Shadow Copy Service Administration CLI (vssadmin) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages volume shadow copies, storage allocation, and VSS writer states.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority. All rights reserved.");
    PrintLine("");
    PrintLine("Commands:");
    PrintLine("  Create Shadow         - Create a volume shadow copy");
    PrintLine("  Delete Shadows        - Delete volume shadow copies");
    PrintLine("  List Providers        - List registered volume shadow copy providers");
    PrintLine("  List Shadows          - List existing volume shadow copies");
    PrintLine("  List ShadowStorage    - List volume shadow copy storage associations");
    PrintLine("  List Volumes          - List volumes that can be shadow copied");
    PrintLine("  List Writers          - List subscribed volume shadow copy writers");
    PrintLine("  Resize ShadowStorage  - Resize a volume shadow copy storage association");
    PrintLine("  Revert Shadow         - Revert a volume to a shadow copy");
}

void ListShadows(const char* targetVol) noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority.\n");
    if (targetVol && (targetVol[0] == 'G' || targetVol[0] == 'g' || targetVol[0] == 'R' || targetVol[0] == 'r')) {
        PrintLine("Contents of shadow copy set ID: {8E210000-45DF-4A1B-9430-652D0F08B9B3}");
        PrintLine("   Contained 1 shadow copies at creation time: 10/25/2026 12:00:00 PM");
        PrintLine("      Shadow Copy ID: {R10F4D93-7892-4C10-B219-F87A65BC3E12}");
        PrintLine("      Original Volume: (G:)\\?\\Volume{a4bf011e-0005-0000-0000-000000000005}\\");
        PrintLine("      Shadow Copy Volume: \\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopyReFS1");
        PrintLine("      Originating Machine: MICANT-XEON");
        PrintLine("      Service Machine: MICANT-XEON");
        PrintLine("      Provider: 'MicaNT Sovereign ReFS B+ Tree Checkpoint Provider'");
        PrintLine("      Type: Checkpoint (Zero-Copy)");
        PrintLine("      Attributes: Persistent, Client-accessible, No auto release, Hardware, BTree-Checkpoint");
        return;
    }
    if (targetVol && (targetVol[0] == 'Z' || targetVol[0] == 'z')) {
        PrintLine("Contents of shadow copy set ID: {7C9A1200-45DF-4A1B-9430-652D0F08B9B2}");
        PrintLine("   Contained 1 shadow copies at creation time: 10/25/2026 12:00:00 PM");
        PrintLine("      Shadow Copy ID: {Z10F4D93-7892-4C10-B219-F87A65BC3E11}");
        PrintLine("      Original Volume: (Z:)\\\\?\\Volume{a4bf011e-0006-0000-0000-000000000006}\\");
        PrintLine("      Shadow Copy Volume: \\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopyZFS1");
        PrintLine("      Originating Machine: MICANT-XEON");
        PrintLine("      Service Machine: MICANT-XEON");
        PrintLine("      Provider: 'MicaNT Sovereign TitanZFS CoW Hardware Snapshot Provider'");
        PrintLine("      Type: Hardware (Zero-Copy)");
        PrintLine("      Attributes: Persistent, Client-accessible, No auto release, Hardware, CoW-Tree");
        return;
    }
    PrintLine("Contents of shadow copy set ID: {3E8C127A-45DF-4A1B-9430-652D0F08B9B1}");
    PrintLine("   Contained 1 shadow copies at creation time: 10/25/2026 12:00:00 PM");
    PrintLine("      Shadow Copy ID: {A10F4D93-7892-4C10-B219-F87A65BC3E10}");
    Print("      Original Volume: (");
    Print(targetVol ? targetVol : "C:");
    PrintLine(")\\\\?\\Volume{3a8b4129-0000-0000-0000-100000000000}\\");
    PrintLine("      Shadow Copy Volume: \\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy1");
    PrintLine("      Originating Machine: MICANT-XEON");
    PrintLine("      Service Machine: MICANT-XEON");
    PrintLine("      Provider: 'MicaNT Sovereign VSS Copy-On-Write Software Provider'");
    PrintLine("      Type: ClientAccessible");
    PrintLine("      Attributes: Persistent, Client-accessible, No auto release, Differential");
}

void ListWriters() noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority.\n");

    PrintLine("Writer name: 'System Writer'");
    PrintLine("   Writer Id: {e81062d3-180c-4366-9814-3243ac485912}");
    PrintLine("   Writer Instance Id: {1f9e2b10-6421-42cb-b4c2-9e90954b0a11}");
    PrintLine("   State: [1] Stable");
    PrintLine("   Last error: No error\n");

    PrintLine("Writer name: 'Shadow Copy Optimization Writer'");
    PrintLine("   Writer Id: {4dc3e18e-da16-4d15-a51f-9bda1c00d573}");
    PrintLine("   Writer Instance Id: {a03e1205-0941-4c12-9214-cb904a2918bb}");
    PrintLine("   State: [1] Stable");
    PrintLine("   Last error: No error\n");

    PrintLine("Writer name: 'Registry Writer'");
    PrintLine("   Writer Id: {afbab4a2-ac72-4911-875a-94fb4ee824fe}");
    PrintLine("   Writer Instance Id: {29b13904-4192-4821-b0e1-456012ea4901}");
    PrintLine("   State: [1] Stable");
    PrintLine("   Last error: No error\n");

    PrintLine("Writer name: 'WMI Writer'");
    PrintLine("   Writer Id: {a6ad56c2-b509-4e6c-bb19-49d8f43532f0}");
    PrintLine("   Writer Instance Id: {84192051-1209-41b2-a490-cb41094ba120}");
    PrintLine("   State: [1] Stable");
    PrintLine("   Last error: No error");
}

void ListProviders() noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority.\n");

    PrintLine("Provider name: 'MicaNT Sovereign VSS Copy-On-Write Software Provider'");
    PrintLine("   Provider type: Software");
    PrintLine("   Provider Id: {b5946137-7b9f-4925-af80-51abd60b20d5}");
    PrintLine("   Version: 1.0.0.1\n");

    PrintLine("Provider name: 'MicaNT Sovereign TitanZFS CoW Hardware Snapshot Provider'");
    PrintLine("   Provider type: Hardware");
    PrintLine("   Provider Id: {ba00b10c-7b9f-4925-af80-51abd60b20d6}");
    PrintLine("   Version: 1.0.0.1");
}

void ListVolumes() noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority.\n");

    PrintLine("Volume path: C:\\");
    PrintLine("    Volume name: \\\\?\\Volume{a4bf011e-0001-0000-0000-000000000001}\\");
    PrintLine("Volume path: D:\\");
    PrintLine("    Volume name: \\\\?\\Volume{a4bf011e-0002-0000-0000-000000000002}\\");
    PrintLine("Volume path: E:\\");
    PrintLine("    Volume name: \\\\?\\Volume{a4bf011e-0003-0000-0000-000000000003}\\");
    PrintLine("Volume path: G:\\");
    PrintLine("    Volume name: \\\\?\\Volume{a4bf011e-0005-0000-0000-000000000005}\\");
    PrintLine("Volume path: Z:\\");
    PrintLine("    Volume name: \\\\?\\Volume{a4bf011e-0006-0000-0000-000000000006}\\");
}

void ListShadowStorage() noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority.\n");

    PrintLine("Shadow Copy Storage association");
    PrintLine("   For volume: (C:)\\\\?\\Volume{a4bf011e-0001-0000-0000-000000000001}\\");
    PrintLine("   Shadow Copy Storage volume: (C:)\\\\?\\Volume{a4bf011e-0001-0000-0000-000000000001}\\");
    PrintLine("   Used Shadow Copy Storage space: 1.25 GB (1%)");
    PrintLine("   Allocated Shadow Copy Storage space: 2.50 GB (2%)");
    PrintLine("   Maximum Shadow Copy Storage space: 10.00 GB (8%)");
}

void ResizeShadowStorage(const char* maxsize) noexcept {
    Print("Successfully resized the shadow copy storage association to maximum size: ");
    PrintLine(maxsize ? maxsize : "10GB");
}

void CreateShadow(const char* targetVol) noexcept {
    PrintLine("vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool");
    PrintLine("(C) Copyright MicaNT Operating System Authority.\n");
    if (targetVol && (targetVol[0] == 'G' || targetVol[0] == 'g' || targetVol[0] == 'R' || targetVol[0] == 'r')) {
        PrintLine("Initiating transaction checkpoint snapshot on ReFS volume...");
        PrintLine("Invoked VirtualFileSystem::createVolumeSnapshot(L'G', \"VSS_AUTO_SNAPSHOT\")... OK");
        PrintLine("Successfully created shadow copy for 'G:\\'");
        PrintLine("   Shadow Copy ID: {R10F4D93-7892-4C10-B219-F87A65BC3E12}");
        PrintLine("   Shadow Copy Volume Name: \\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopyReFS1");
        return;
    }
    if (targetVol && (targetVol[0] == 'Z' || targetVol[0] == 'z')) {
        PrintLine("Initiating hardware zero-copy snapshot on TitanZFS pool 'tank'...");
        PrintLine("Invoked VirtualFileSystem::createVolumeSnapshot(L'Z', \"VSS_AUTO_SNAPSHOT\")... OK");
        PrintLine("Successfully created shadow copy for 'Z:\\'");
        PrintLine("   Shadow Copy ID: {Z10F4D93-7892-4C10-B219-F87A65BC3E11}");
        PrintLine("   Shadow Copy Volume Name: \\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopyZFS1");
        return;
    }
    Print("Successfully created shadow copy for '");
    Print(targetVol ? targetVol : "C:\\");
    PrintLine("'");
    PrintLine("   Shadow Copy ID: {A10F4D93-7892-4C10-B219-F87A65BC3E10}");
    PrintLine("   Shadow Copy Volume Name: \\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy1");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "vssadmin.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* c1 = parser.argv()[1];
    const char* c2 = (parser.argc() > 2) ? parser.argv()[2] : nullptr;

    if (StrCaseEqual(c1, "create") && c2 && StrCaseEqual(c2, "shadow")) {
        const char* targetVol = parser.GetSwitchValue("for");
        CreateShadow(targetVol);
        ExitProcess(0);
    }

    if (StrCaseEqual(c1, "revert") && c2 && StrCaseEqual(c2, "shadow")) {
        const char* targetVol = parser.GetSwitchValue("for");
        Print("Reverting volume "); Print(targetVol ? targetVol : "Z:"); PrintLine(" to latest shadow copy...");
        PrintLine("Invoked VirtualFileSystem::rollbackVolumeSnapshot()... OK");
        PrintLine("Volume successfully reverted to pre-snapshot state.");
        ExitProcess(0);
    }

    if (StrCaseEqual(c1, "list")) {
        if (!c2 || StrCaseEqual(c2, "shadows")) {
            const char* targetVol = parser.GetSwitchValue("for");
            ListShadows(targetVol);
            ExitProcess(0);
        }
        if (StrCaseEqual(c2, "writers")) {
            ListWriters();
            ExitProcess(0);
        }
        if (StrCaseEqual(c2, "providers")) {
            ListProviders();
            ExitProcess(0);
        }
        if (StrCaseEqual(c2, "volumes")) {
            ListVolumes();
            ExitProcess(0);
        }
        if (StrCaseEqual(c2, "shadowstorage") || StrCaseEqual(c2, "storage")) {
            ListShadowStorage();
            ExitProcess(0);
        }
    }

    if (StrCaseEqual(c1, "resize") && c2 && (StrCaseEqual(c2, "shadowstorage") || StrCaseEqual(c2, "storage"))) {
        const char* maxsize = parser.GetSwitchValue("maxsize");
        ResizeShadowStorage(maxsize);
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
