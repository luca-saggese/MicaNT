# KERNEL32 API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
The Kernel32 image currently exports 147 names. That count is not a claim that
all Windows 11 exports have been inventoried.

## This implementation batch

| Export | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `GetCurrentProcessId` | `processthreadsapi.h` | IMPLEMENTED | Reads the calling process ID from the current TEB. Returns zero only if the TEB is unavailable; it no longer substitutes a fixed PID. |
| `GetCurrentThreadId` | `processthreadsapi.h` | IMPLEMENTED | Reads the calling thread ID from the current TEB. Returns zero only if the TEB is unavailable; it no longer substitutes a fixed TID. |
| `CreateEventW`, `CreateEventExW` | `synchapi.h` | PARTIAL | Unnamed events use native event handles through NTDLL. Named events and security attributes return `ERROR_CALL_NOT_IMPLEMENTED`. |
| `SetEvent`, `ResetEvent`, `WaitForSingleObject`, `WaitForSingleObjectEx`, `CloseHandle` | `synchapi.h`, `handleapi.h` | PARTIAL | Dispatch to native syscalls. `WaitForMultipleObjectsEx` supports one handle; larger sets fail with `ERROR_INVALID_PARAMETER`. |
| `VirtualAlloc`, `VirtualFree` | `memoryapi.h` | PARTIAL | Use native process-memory syscalls for the current process and return mapped errors. |
| `LoadLibraryW`, `LoadLibraryExW`, `FreeLibrary` | `libloaderapi.h` | UNSUPPORTED | Return failure until loader reference counting and module loading/unloading are implemented. |
| `SwitchToThread` | `processthreadsapi.h` | IMPLEMENTED | Uses `NtYieldExecution` and reports whether the kernel scheduled another thread. |
| `SetThreadDescription` | `processthreadsapi.h` | UNSUPPORTED | Returns `E_NOTIMPL`. |

The direct staging source name `tools/dlls/kernel32.cpp` now includes the same
per-DLL ABI implementation used by `kernel32_boot`. Stage builds link the
NTDLL and UCRT import libraries and compile the stack probe and freestanding
memory support, removing the old duplicate implementation that fabricated
standard handles and used incomplete syscall argument lists.

## Coverage ledger

| DLL | Reference exports | Current exports | Documented | Implemented in batch | Runtime verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `kernel32.dll` — current image | TBD | 147 | 16 | 12 | 0 | 4 | TBD |

Both the CMake target and an equivalent direct stage compile were linked. The
export count was read from the generated PE. Runtime behavior is not counted
as verified here; the complete Windows export inventory remains pending.

## Sources

- [Microsoft Learn: GetCurrentProcessId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocessid)
- [Microsoft Learn: GetCurrentThreadId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadid)
- [Microsoft Learn: QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter)
