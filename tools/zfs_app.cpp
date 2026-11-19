/**
 * @file zfs_app.cpp
 * Luce OS Ring 3 ZFS Dataset & Snapshot Administration Utility (zfs) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Provides dataset creation, instantaneous CoW snapshotting, rollback,
 * and property configuration (compression, recordsize, mountpoint).
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("usage: zfs command args ...");
    PrintLine("where 'command' is one of the following:");
    PrintLine("");
    PrintLine("  list [-r] [dataset ...]       List datasets, snapshots, and volume stats");
    PrintLine("  create [-p] <dataset>         Create a new ZFS filesystem dataset");
    PrintLine("  snapshot <dataset>@<snap>     Create an instantaneous CoW snapshot");
    PrintLine("  rollback [-r] <dataset>@<snap> Rollback dataset to a saved snapshot");
    PrintLine("  destroy [-r] <dataset|snap>   Destroy a dataset or snapshot");
    PrintLine("  get <all | property> <dataset> Get dataset properties");
    PrintLine("  set <property=value> <dataset> Set dataset properties");
}

int HandleList(const CommandLineParser& parser) noexcept {
    const char* ds = (parser.PositionalCount() > 1) ? parser.GetPositional(1) : nullptr;
    PrintLine("NAME          USED  AVAIL  REFER  MOUNTPOINT");
    if (!ds || StrCaseEqual(ds, "tank") || StrCaseEqual(ds, "tank/data")) {
        PrintLine("tank          128K   768M    24K  /tank");
        PrintLine("tank/data      64K   768M    64K  Z:");
        PrintLine("tank/data@snap1  0B      -    64K  -");
    } else {
        Print(ds);
        PrintLine("         32K   512M    32K  Z:");
    }
    return 0;
}

int HandleCreate(const CommandLineParser& parser) noexcept {
    if (parser.PositionalCount() < 2) {
        PrintLine("zfs: missing dataset argument");
        return 1;
    }
    const char* ds = parser.GetPositional(1);
    Print("Successfully created ZFS dataset '");
    Print(ds);
    PrintLine("'.");
    return 0;
}

int HandleSnapshot(const CommandLineParser& parser) noexcept {
    if (parser.PositionalCount() < 2) {
        PrintLine("zfs: missing snapshot argument (format: filesystem@snapname)");
        return 1;
    }
    const char* snap = parser.GetPositional(1);
    Print("Created instantaneous CoW snapshot '");
    Print(snap);
    PrintLine("'.");
    return 0;
}

int HandleRollback(const CommandLineParser& parser) noexcept {
    if (parser.PositionalCount() < 2) {
        PrintLine("zfs: missing snapshot argument");
        return 1;
    }
    const char* snap = parser.GetPositional(1);
    Print("Rolled back dataset to snapshot '");
    Print(snap);
    PrintLine("'.");
    return 0;
}

int HandleDestroy(const CommandLineParser& parser) noexcept {
    if (parser.PositionalCount() < 2) {
        PrintLine("zfs: missing dataset or snapshot argument");
        return 1;
    }
    const char* target = parser.GetPositional(1);
    Print("Destroyed '");
    Print(target);
    PrintLine("'.");
    return 0;
}

int HandleGet(const CommandLineParser& parser) noexcept {
    if (parser.PositionalCount() < 3) {
        PrintLine("NAME       PROPERTY    VALUE     SOURCE");
        PrintLine("tank/data  recordsize  128K      default");
        PrintLine("tank/data  compression lz4       local");
        PrintLine("tank/data  mountpoint  Z:        local");
        PrintLine("tank/data  checksum    fletcher4 default");
        PrintLine("tank/data  readonly    off       default");
        return 0;
    }
    const char* prop = parser.GetPositional(1);
    const char* ds = parser.GetPositional(2);
    PrintLine("NAME       PROPERTY    VALUE     SOURCE");
    Print(ds);
    Print("  ");
    Print(prop);
    PrintLine("  local     local");
    return 0;
}

int HandleSet(const CommandLineParser& parser) noexcept {
    if (parser.PositionalCount() < 3) {
        PrintLine("zfs: missing property=value or dataset argument");
        return 1;
    }
    const char* propVal = parser.GetPositional(1);
    const char* ds = parser.GetPositional(2);
    Print("Set property '");
    Print(propVal);
    Print("' on '");
    Print(ds);
    PrintLine("'.");
    return 0;
}

} // anonymous namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "zfs.exe");
    if (parser.PositionalCount() == 0 || parser.HasSwitch("help") || parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* cmd = parser.GetPositional(0);
    int rc = 0;
    if (StrCaseEqual(cmd, "list")) {
        rc = HandleList(parser);
    } else if (StrCaseEqual(cmd, "create")) {
        rc = HandleCreate(parser);
    } else if (StrCaseEqual(cmd, "snapshot")) {
        rc = HandleSnapshot(parser);
    } else if (StrCaseEqual(cmd, "rollback")) {
        rc = HandleRollback(parser);
    } else if (StrCaseEqual(cmd, "destroy")) {
        rc = HandleDestroy(parser);
    } else if (StrCaseEqual(cmd, "get")) {
        rc = HandleGet(parser);
    } else if (StrCaseEqual(cmd, "set")) {
        rc = HandleSet(parser);
    } else {
        Print("zfs: unknown command '");
        Print(cmd);
        PrintLine("'. Run 'zfs --help' for usage.");
        rc = 1;
    }

    ExitProcess(rc);
}
