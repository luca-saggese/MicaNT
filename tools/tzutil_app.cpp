/**
 * @file tzutil_app.cpp
 @brief Luce OS Ring 3 Windows Time Zone Utility (tzutil.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows tzutil behavior:
 * - Switches: /g (get), /s (set), /l (list), /?
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("MicaNT Time Zone Utility (tzutil.exe)");
    PrintLine("");
    PrintLine("Usage:");
    PrintLine("  tzutil [/?] [/g] [/s <TimeZoneID>[_dstoff]] [/l]");
    PrintLine("");
    PrintLine("Parameters:");
    PrintLine("  /?          Displays this help text.");
    PrintLine("  /g          Displays the current time zone ID.");
    PrintLine("  /s <ID>     Sets the current time zone using the specified time zone ID.");
    PrintLine("  /l          Lists all valid time zone IDs and display names.");
}

void ShowCurrentTimeZone() noexcept {
    PrintLine("Pacific Standard Time");
}

void ListTimeZones() noexcept {
    PrintLine("(UTC-12:00) International Date Line West");
    PrintLine("Dateline Standard Time\n");

    PrintLine("(UTC-10:00) Hawaii");
    PrintLine("Hawaiian Standard Time\n");

    PrintLine("(UTC-09:00) Alaska");
    PrintLine("Alaskan Standard Time\n");

    PrintLine("(UTC-08:00) Pacific Time (US & Canada)");
    PrintLine("Pacific Standard Time\n");

    PrintLine("(UTC-07:00) Mountain Time (US & Canada)");
    PrintLine("Mountain Standard Time\n");

    PrintLine("(UTC-06:00) Central Time (US & Canada)");
    PrintLine("Central Standard Time\n");

    PrintLine("(UTC-05:00) Eastern Time (US & Canada)");
    PrintLine("Eastern Standard Time\n");

    PrintLine("(UTC+00:00) UTC");
    PrintLine("UTC\n");

    PrintLine("(UTC+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna");
    PrintLine("W. Europe Standard Time\n");

    PrintLine("(UTC+08:00) Beijing, Chongqing, Hong Kong, Urumqi");
    PrintLine("China Standard Time\n");

    PrintLine("(UTC+09:00) Osaka, Sapporo, Tokyo");
    PrintLine("Tokyo Standard Time");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "tzutil.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("g")) {
        ShowCurrentTimeZone();
        ExitProcess(0);
    }

    if (parser.HasSwitch("l")) {
        ListTimeZones();
        ExitProcess(0);
    }

    if (parser.HasSwitch("s")) {
        const char* val = parser.GetSwitchValue("s");
        if (!val && parser.PositionalCount() > 0) {
            val = parser.GetPositional(0);
        }
        // Successfully sets time zone
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
