# ReactOS CMD Compatibility Status

## Binary under test

- Source: official ReactOS x64 BuildBot image `0.4.17-dev-1343`, revision `8fe3cee05606aecd9442674e40038649f048d10d`.
- `cmd.exe`: AMD64 PE32+ CUI, SHA-256 `bccc6bc1ba6013384f22809a627f14c368d2e636508db8a46a507e53c89e06e9`.
- Only the original `cmd.exe` is staged in `/private/tmp/reactos-apps-flat`; no ReactOS DLL/runtime is staged.

## Current QEMU result

QEMU was run with the original PE after Explorer using the isolated compatibility checkout and QMP-enabled boot path. Explorer and the CPL3 surface started, then the native image loader rejected CMD because its Kernel32 import `WriteConsoleW` was not exported:

```text
[MicaNT ReactOSApps] Launching original AMD64 PE after Explorer
[MicaNT Ps] Forwarder resolve rejected importer=cmd.exe library=kernel32.dll symbol=WriteConsoleW status=3221225594
[MicaNT Ps] SEC_IMAGE initialization forwarder loading failed status=3221225594
[MicaNT ReactOSApps] Original AMD64 PE launch failed
```

Evidence is in `build/reactos-cmd-qemu-evidence-1795007623700609000/serial.log` and `cpl3-surface.ppm` in the isolated checkout. The image shows the desktop; it is not evidence of a running CMD window. CMD has not reached its entry point, accepted input, or passed cleanup/exit checks.

The previous actual loader stop was `FlushFileBuffers`. The Kernel32 wrapper now calls the canonical NTDLL `NtFlushBuffersFile`, preserves NT failure status through `GetLastError`, and its export was verified in the built `kernel32.dll`. The next QEMU run advanced past that import and stopped at `WriteConsoleW`.

## Import closure

Static comparison of CMD's 130 Kernel32, 8 Advapi32, 74 MSVCRT, and 3 NTDLL imports against the currently built DLL exports finds:

- Kernel32: 80 imported names remain unavailable. The first blocker in the original PE's binding order is `WriteConsoleW`; the set also includes console input/screen-buffer calls, file and directory operations, process/handle calls, locale/time calls, and `CreatePipe`.
- Advapi32: all 8 imported names have exports. This proves export resolution only; registry behavior still requires runtime verification against the NT registry owner.
- MSVCRT: all 74 imported names have exports. This proves export resolution only; the FILE/pipe functions currently have explicit unsupported failure behavior and do not satisfy CMD's interactive runtime.
- NTDLL: `RtlIsDosDeviceName_U`, `RtlAssert`, and `vDbgPrintExWithPrefix` remain missing.
- User32: the delayed `MessageBeep` import remains missing.

`GetStdHandle`, `SetStdHandle`, `GetCommandLineW`, `GetEnvironmentStringsW`, and `FreeEnvironmentStringsW` have since been added to the canonical Kernel32 DLL source and their exports verified in the isolated build. They have not yet been exercised by CMD in QEMU because import binding stops earlier at `WriteConsoleW`.

## Exact console dependency

CMD initializes three standard streams at startup and enters interactive input through the Windows console input and screen-buffer APIs. Its executable imports `WriteConsoleW`, `ReadConsoleInputW`, `WriteConsoleOutputCharacterW`, `GetConsoleScreenBufferInfo`, console mode/cursor/attribute APIs, aliases, and console lifetime/control APIs. The process-parameter ABI has standard-handle fields, but the production user-mode Kernel32 DLL has no connected console server/device path that creates those handles, services screen-buffer operations, or delivers QMP keyboard events.

The existing `ConhostManager`/`ConsoleSession` abstraction is an in-process C++ manager, not a process-shared NT console service or object-handle implementation; its header also crosses the Ring 3 boundary through a private kernel boot-video include. Linking that abstraction into Kernel32 would violate the repository boundary and would not provide real handle lifetime, console input, or window interaction. The next correct dependency is a canonical Ring 3 console service and native IPC/handle path, integrated with the existing desktop/window input route. Until that exists, a `WriteConsoleW` export that reports success without changing a console screen buffer would be a false positive.

## Resume point

1. Design the console handle/service contract in the canonical Ring 3 boundary and connect it to QMP-delivered keyboard input and the existing compositor/window route.
2. Implement `WriteConsoleW` and the next imported console APIs against that service, including handle validation, buffer state, input delivery, failure paths, and cleanup.
3. Continue resolving CMD's remaining imports in actual PE binding order, implementing them in their canonical owners; rerun QEMU after each integration boundary.
4. Require a visible CMD window, real command interaction, and successful process/console cleanup before marking any compatibility level above load.

No CMD compatibility level is currently achieved: PE dependency binding fails before image initialization and entry-point execution.
