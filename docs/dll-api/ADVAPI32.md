# ADVAPI32 API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This ledger distinguishes the canonical CMake DLL from the direct stage path;
their source sets are not interchangeable.

## Canonical `advapi32_boot` path

| Export | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `RegOpenKeyExW` | `winreg.h` | IMPLEMENTED | Opens a registry key through `NtOpenKey`; maps native failures to Win32 status values. |
| `RegQueryValueExW` | `winreg.h` | IMPLEMENTED | Queries a value through `NtQueryValueKey`, reports required buffer size, and leaves undersized output data untouched. |
| `RegCloseKey` | `winreg.h` | IMPLEMENTED | Closes only a tracked key and reports invalid handles. |
| `RegGetValueW` | `winreg.h` | PARTIAL | Supports a value on the supplied open key with flags zero; non-empty subkeys and flags return `ERROR_CALL_NOT_IMPLEMENTED`. |
| `RegCreateKeyExW`, `RegSetValueExW`, `RegDeleteKeyW`, `RegEnumKeyExW`, `RegQueryInfoKeyW` | `winreg.h` | UNSUPPORTED | Return explicit Win32 failure; unsupported state changes or outputs are not fabricated. |
| `GetUserNameW` | `secext.h` | UNSUPPORTED | Returns `FALSE`, sets `ERROR_CALL_NOT_IMPLEMENTED`, and does not invent an account identity. |
| `SystemFunction036` | secondary/legacy name | UNSUPPORTED | Returns failure without writing predictable pseudo-random bytes. |

The canonical target is assembled from `tools/dlls/abi/advapi32_system32.cpp`
and the shared native ABI implementation. The direct `tools/dlls/advapi32.cpp`
stage path now has typed A/W registry, token, and SCM exports that all fail
explicitly because that standalone source has no subsystem connection. Its
BOOL APIs set `ERROR_CALL_NOT_IMPLEMENTED`; its registry APIs return that
status directly.

## Coverage ledger

| DLL/path | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Canonical `advapi32_boot` subset | TBD | 11 | 11 | 3 | 1 | 0 | 7 | TBD |
| Direct staging source subset | TBD | 16 | 16 | 0 | 0 | 0 | 16 | TBD |

The complete versioned Advapi32 export inventory remains pending. Native registry
operations are compile-checked but not counted as runtime-verified here.

## Sources

- [Microsoft Learn: Registry functions](https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-functions)
- [Microsoft Learn: RegOpenKeyExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw)
- [Microsoft Learn: RegQueryValueExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw)
- [Microsoft Learn: RegSetValueExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw)
- [Microsoft Learn: GetUserNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getusernamew)
