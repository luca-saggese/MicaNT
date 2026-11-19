# KERNELBASE API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This batch covers the six exports currently declared by MicaNT, not the full
versioned KernelBase export inventory.

| Export | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `QueryPerformanceCounter`, `QueryPerformanceFrequency` | `profileapi.h` | IMPLEMENTED | Forward to `NtQueryPerformanceCounter` and report the native status as Win32 `BOOL`. |
| `OpenProcess` | `processthreadsapi.h` | IMPLEMENTED | Builds the documented process client ID and inheritance attribute, then calls `NtOpenProcess`; failure returns null. |
| `TerminateProcess` | `processthreadsapi.h` | IMPLEMENTED | Forwards the process handle and exit status to `NtTerminateProcess`. |
| `VirtualAllocEx` | `memoryapi.h` | IMPLEMENTED | Forwards address, size, allocation type, and protection to `NtAllocateVirtualMemory`; failure returns null. |
| `VirtualFreeEx` | `memoryapi.h` | IMPLEMENTED | Forwards address, region size, and free type to `NtFreeVirtualMemory`. |

All six APIs use typed Win32 AMD64 signatures. KernelBase reaches process,
performance-counter, and virtual-memory services through `ntdll_boot`, not
private kernel headers. These implementations compile and link; no runtime
behavior is counted as verified in this batch.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `kernelbase.dll` — exports in current image | TBD | 6 | 6 | 6 | 0 | 0 | 0 | TBD |

## Sources

- [Microsoft Learn: OpenProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess)
- [Microsoft Learn: TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)
- [Microsoft Learn: VirtualAllocEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualallocex)
- [Microsoft Learn: VirtualFreeEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfreeex)
- [Microsoft Learn: Process and thread functions](https://learn.microsoft.com/en-us/windows/win32/api/_processthreadsapi/)
