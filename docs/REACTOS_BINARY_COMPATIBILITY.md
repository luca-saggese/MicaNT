# ReactOS AMD64 Binary Compatibility Campaign

This campaign executes the original, unmodified ReactOS application PEs in LuceOS Ring 3. It does not install ReactOS DLLs into LuceOS or replace any `System32` application or DLL.

## Reproducible binary source

- Official ReactOS BuildBot x64 MSVC Debug image, revision `0.4.17-dev-1343`, source commit `8fe3cee05606aecd9442674e40038649f048d10d`.
- Archive: [reactos-bootcd-0.4.17-dev-1343-g8fe3cee-x64-msvc-win-dbg.7z](https://iso.reactos.org/bootcd/reactos-bootcd-0.4.17-dev-1343-g8fe3cee-x64-msvc-win-dbg.7z).
- Archive SHA-256: `42cbb141d7c66a2c2a273292232aec06d4d98c916d8471757fc22ab0beccf26e`.
- The archive and extracted application binaries remain outside the repository in `/private/tmp`. The repository tracks only verification metadata. The JSON manifest records every executable SHA-256, machine, subsystem, and imported DLL/symbol (including ordinal imports).

## Compatibility matrix

Levels are cumulative: Load (image and imports mapped), Initialize (startup and DLL initialization completes), Window (visible window), Functional (real keyboard/mouse operation and expected behavior), Stable (repeatable operation and clean process/resource teardown).

| Order | Application | PE verification | Load | Initialize | Window | Functional | Stable | Evidence / first incompatibility |
|---:|---|---|---|---|---|---|---|---|
| 1 | `winver.exe` | AMD64, GUI; SHA in manifest | Pass | Pass in earlier run; latest run blocked | Blocked | Blocked | Partial cleanup only | Earlier run `build/reactos-winver-qemu-evidence-1794998703304355000` resolved the 39 imports and reached `ShellAboutW`, but scanout did not change. Latest run `build/reactos-winver-qemu-evidence-1794999608824845000` hit an unhandled Ring 3 GP (vector 13, RIP `0x0000428000231218`, PID 113) while starting the PE. Generic CRT probe had produced a GP in the same launch phase in run `1794999382943651000`; its standalone assertions remain unverified. The immediate reusable blockers are exception-to-process termination and ordinary app window surface composition. |
| 2 | `notepad.exe` | AMD64, GUI; SHA in manifest | Blocked | Blocked | Blocked | Blocked | Blocked | QEMU run `build/reactos-notepad-qemu-evidence-1794999673099599000`: SEC_IMAGE dependency load stops at missing `comdlg32.dll` (`STATUS_OBJECT_NAME_NOT_FOUND`). A source file exists but exports unconditional success and is not linked; it is deliberately not used. Implement a real common file dialog path before retry. |
| 3 | `calc.exe` | AMD64, GUI; SHA in manifest | Blocked | Blocked | Blocked | Blocked | Blocked | QEMU run `build/reactos-calc-qemu-evidence-1794999757406687000`: after adding legacy `comctl32!InitCommonControls` in its canonical owner and rerunning, forwarder binding stops at missing `user32!GetKeyboardState` (`STATUS_PROCEDURE_NOT_FOUND`). Existing `GetAsyncKeyState` is not a substitute. |
| 4 | `winmine.exe` | AMD64, GUI; SHA in manifest | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | |
| 4 | `sol.exe` | AMD64, GUI; SHA in manifest | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | |
| 5 | `mspaint.exe` | AMD64, GUI; SHA in manifest | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | |
| 6 | `regedit.exe` | AMD64, GUI; SHA in manifest | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | |
| 7 | `taskmgr.exe` | AMD64, GUI; SHA in manifest | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | |
| 8 | `wordpad.exe` | AMD64, GUI; SHA in manifest | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | Pending QEMU | |

## QEMU evidence

Each app run must retain the serial log, QEMU log, scanout screenshots, and the kernel build manifest under its own `build/reactos-<app>-qemu-evidence-*` directory. Do not promote a level on the basis of static imports or a visible but unresponsive window. For `winver.exe`, QMP Enter reaches the ShellAbout window procedure and the process exits with its API return value 1; the desktop before/after screenshots remain effectively identical, so this is not a visible or functional winver result. The next concrete dependency is process-owned window surface composition for ordinary Ring 3 apps. The original first loader blocker (`advapi32.dll`) was resolved by linking the existing canonical implementation; later import blockers were addressed in their owning DLLs. The regression harness now checks that scanout changes after input instead of counting process exit alone.

## MSVCRT tranche

`build/system32/msvcrt.dll` is a real AMD64 PE DLL built by `msvcrt_boot` from the general-purpose `tools/dlls/msvcrt.cpp` and `tools/dlls/msvcrt.def`; its source contains no ReactOS or Winver name checks. The original PE's 16 named MSVCRT imports all resolve through the DLL, and the QEMU run reaches application startup and the `ShellAboutW` call. The DLL shares selected UCRT exports through PE forwarders (`malloc`, `free`, `realloc`, `memcpy`, `memset`, `_errno`, `_initterm`, `_initterm_e`, and `localeconv`) rather than defining second heaps, errno storage, or locale state.

The implemented MSVCRT-specific surface includes command-line argument parsing and the exported `_wcmdln` / `__winitenv` data variables, startup and exit-handler paths, `_fpreset`, signal registration, C-locale handling, and a bounded wide formatter for integer, pointer, string, and character conversions. Winver's original import resolution was observed in the earlier QEMU run. The separate generic call-level probe is built but did not pass: its process caused an unhandled Ring 3 GP before reporting an exit status, and automatic boot execution has been removed pending diagnosis. Thus allocator pairing, errno mutation, locale changes, and formatter edge cases remain unverified in QEMU.

`FILE*` and stream exports are not present in Winver's import table and are intentionally not exported by this tranche. Their MSVCRT stream object layout and ownership contract have not been established against UCRT, so programs requiring `_iob`, `fopen`/`fclose`, or related APIs remain unsupported. Floating-point `_vsnwprintf` conversions and wildcard expansion in `__wgetmainargs` also fail explicitly.
