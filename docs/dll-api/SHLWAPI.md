# SHLWAPI API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This batch covers the 12 ANSI/Unicode path exports currently present in
MicaNT; it is not a full versioned Shlwapi export inventory.

| Export family | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `PathFindFileNameA/W` | `shlwapi.h` | IMPLEMENTED | Returns the input pointer advanced past the last slash, backslash, or drive separator; null input returns null. |
| `PathFindExtensionA/W` | `shlwapi.h` | IMPLEMENTED | Returns the final filename extension or the terminating null; dots in parent path components are ignored. |
| `PathCombineA/W` | `shlwapi.h` | IMPLEMENTED | Combines into a caller-provided `MAX_PATH` buffer, bounds-checks, and returns null on overflow. |
| `PathRemoveFileSpecA/W` | `shlwapi.h` | IMPLEMENTED | Removes the final component in place and preserves a drive root. |
| `PathFileExistsA/W` | `shlwapi.h` | UNSUPPORTED | Returns `FALSE`; the file-information service is not connected. |
| `PathIsDirectoryA/W` | `shlwapi.h` | UNSUPPORTED | Returns `FALSE`; the file-information service is not connected. |

The implementation uses exact Win32 calling conventions and separate A/W
exports. The `PathCombine` caller must provide a `MAX_PATH` destination as
required by the documented API; no larger destination length is available in
that ABI.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `shlwapi.dll` — documented exports in this batch | TBD | 12 | 12 | 8 | 0 | 0 | 4 | TBD |

## Sources

- [Microsoft Learn: PathCombineA](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathcombinea)
- [Microsoft Learn: PathFileExistsW](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfileexistsw)
- [Microsoft Learn: PathFindExtensionA](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfindextensiona)
- [Microsoft Learn: PathFindFileNameA](https://learn.microsoft.com/en-us/windows/desktop/api/shlwapi/nf-shlwapi-pathfindfilenamea)
- [Microsoft Learn: PathRemoveFileSpecW](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremovefilespecw)
