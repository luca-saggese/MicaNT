# VERSION API status — AMD64

The DLL exposes the 16 A/W version helper exports declared by the local
Windows SDK `winver.h` profile. Reference export coverage is still pending a
versioned Windows AMD64 export inventory; the count below is limited to this
profile's declaration set.

The file-backed resource reader and legacy version file installer are not
connected. Each export therefore reports failure instead of returning
fabricated version data. `VerQueryValueA/W` clear both output parameters;
`GetFileVersionInfoSize*` clears the reserved handle output; and
`VerLanguageNameA/W` clear the first character when a writable buffer is
provided. Failure APIs set `ERROR_CALL_NOT_IMPLEMENTED`.

| DLL | Export reference | Declared | Documented | Implemented | Verified | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `version.dll` — current declaration profile | TBD | 16 | 16 | 0 | 0 | TBD |

All 16 declared entry points are explicit unsupported stubs. The count does
not claim full Windows 11 export coverage. The build target links against the
canonical Kernel32 `SetLastError` export.
