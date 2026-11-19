# NTDLL API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100). The
official `winternl.h` API index is the public declaration baseline for this
initial NTDLL pass. The index is not a complete inventory of every private
`ntdll.dll` export; ordinals and undocumented exports remain unclaimed until
compared against a versioned PE export table.

## This implementation batch

| Export | ABI/source | LuceOS state | Behavior |
| --- | --- | --- | --- |
| `NtOpenFile` | Microsoft `winternl.h`; six parameters | IMPLEMENTED | Uses the existing `NtCreateFile` service with `FILE_OPEN`; the caller owns and closes a successful handle. |
| `NtNotifyChangeMultipleKeys` | Microsoft `winternl.h`; twelve parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; optional I/O status block receives the same status and zero information. |
| `NtQueryMultipleValueKey` | Microsoft `winternl.h`; six parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; does not claim a registry query occurred. |
| `NtSetInformationKey` | Microsoft `winternl.h`; four parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; no key state is changed. |
| `NtRenameKey` | Microsoft `winternl.h`; two parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; no key is renamed. |
| `NtFsControlFile` | Microsoft `winternl.h`; ten parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; optional I/O status block receives the failure. |
| `NtSetInformationThread` | Microsoft `winternl.h`; four parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; no thread state is changed. |
| `NtQueryObject` | Microsoft `winternl.h`; five parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output memory is not reported as populated. |
| `NtQueryTimerResolution` | public WDK declaration; three output pointers | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; outputs are left unchanged. |
| `NtQueryVolumeInformationFile` | public WDK declaration; five parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; optional I/O status block receives the failure. |
| `NtSetInformationProcess` | public WDK declaration; four parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; no process state is changed. |
| `NtSetVolumeInformationFile` | public WDK declaration; five parameters | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; optional I/O status block receives the failure. |
| `RtlNtStatusToDosError` | Microsoft `winternl.h`; NTSTATUS to Win32 error | PARTIAL | Maps the statuses used by the current profile; unrecognized statuses map to `ERROR_MR_MID_NOT_FOUND`. |
| `RtlInitString` | Microsoft `winternl.h`; counted ANSI string | IMPLEMENTED | Initializes lengths and borrows the source buffer; caller retains source ownership. |
| `RtlInitAnsiString` | Microsoft `winternl.h`; counted ANSI string | IMPLEMENTED | Alias behavior of `RtlInitString`; no allocation. |
| `RtlInitStringEx` | public NTDLL declaration; checked ANSI string | IMPLEMENTED | Initializes the borrowed string or returns `STATUS_NAME_TOO_LONG`. |
| `RtlInitAnsiStringEx` | public NTDLL declaration; checked ANSI string | IMPLEMENTED | Checked alias of `RtlInitStringEx`. |
| `RtlAnsiStringToUnicodeString` | Microsoft `winternl.h`; counted string conversion | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output and ownership are unchanged. |
| `RtlUnicodeStringToAnsiString` | Microsoft `winternl.h`; counted string conversion | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output and ownership are unchanged. |
| `RtlUnicodeStringToOemString` | Microsoft `winternl.h`; OEM code-page conversion | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output and ownership are unchanged. |
| `RtlUnicodeToMultiByteSize` | Microsoft `winternl.h`; output byte count | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output count is unchanged. |
| `RtlCharToInteger` | Microsoft `winternl.h`; string-to-integer parser | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; Windows parsing edge cases have not been verified. |
| `RtlLocalTimeToSystemTime` | Microsoft `winternl.h`; local-time conversion | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output time is unchanged. |
| `RtlTimeToSecondsSince1970` | Microsoft `winternl.h`; timestamp conversion | IMPLEMENTED | Converts valid 1601-based 100-nanosecond system time to Unix seconds; returns `FALSE` outside the documented range. |
| `RtlConvertSidToUnicodeString` | Microsoft `winternl.h`; SID formatting | UNSUPPORTED | Returns `STATUS_NOT_SUPPORTED`; output string is unchanged. |

All entries are C-linkage AMD64 exports. Microsoft does not publish stable
ordinals for this API set in the cited declaration pages, so this batch records
no ordinal. `STATUS_NOT_SUPPORTED` is returned as an NTSTATUS; no C++ exception
crosses the DLL ABI. These are exported functions and are included in the
`ntdll_boot` build.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ntdll.dll` — Microsoft Winternl/WDK documented subset | TBD | 25 in this batch | 25 in this batch | 6 in this batch | 1 in this batch | 0 runtime contracts | 18 in this batch | TBD |

The full Windows-version export inventory and complete per-DLL totals are
pending. “Verified” counts runtime contract tests, not compilation. This batch
has been compile-checked only until an NTDLL syscall integration test exercises
each status and output-buffer rule.

## Sources

- [Microsoft Learn: Winternl.h API index](https://learn.microsoft.com/en-us/windows/win32/api/winternl/)
- [Microsoft Learn: NtOpenFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntopenfile)
- [Microsoft Learn: NtQueryInformationProcess](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess)
- [Microsoft Learn: Windows 11 release information](https://learn.microsoft.com/en-us/windows/release-health/windows11-release-information)
- [ReactOS NTDLL export specification](https://github.com/reactos/reactos/blob/master/dll/ntdll/def/ntdll.spec) — secondary comparison source; not used to assert Windows ordinals.
