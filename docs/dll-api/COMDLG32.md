# COMDLG32 API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This file tracks the currently declared public Common Dialog exports in
MicaNT; it does not claim a full versioned export-table inventory.

## Current implementation batch

| Export family | ABI/source | State | Behavior |
| --- | --- | --- | --- |
| `GetOpenFileNameA/W`, `GetSaveFileNameA/W` | Microsoft `commdlg.h` | UNSUPPORTED | Return `FALSE`; `CommDlgExtendedError` reports `CDERR_DIALOGFAILURE`. No file selection or output-buffer mutation is claimed. |
| `ChooseColorA/W`, `ChooseFontA/W` | Microsoft `commdlg.h` | UNSUPPORTED | Return `FALSE`; report `CDERR_DIALOGFAILURE`. |
| `PrintDlgA/W` | Microsoft `commdlg.h` | UNSUPPORTED | Return `FALSE`; report `CDERR_DIALOGFAILURE`. |
| `PrintDlgExA/W` | Microsoft `commdlg.h` | UNSUPPORTED | Return `E_NOTIMPL`; record `CDERR_DIALOGFAILURE`. |
| `FindTextA/W`, `ReplaceTextA/W` | Microsoft `commdlg.h` | UNSUPPORTED | Return a null `HWND`; record `CDERR_DIALOGFAILURE`. |
| `CommDlgExtendedError` | Microsoft `commdlg.h` | PARTIAL | Returns and clears the recorded failure. Its storage is process-wide because MicaNT's user-mode thread-local storage contract is not connected here. |

The implementation exports typed ANSI and Unicode entry points with the
documented Win32 calling convention. Unsupported APIs fail explicitly; they do
not report cancellation or manufacture a successful selection/window handle.
The DLL is a standalone Ring 3 target (`comdlg32_boot`), not kernel code.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `comdlg32.dll` — documented exports in this batch | TBD | 17 | 17 | 0 | 1 | 0 | 16 | TBD |

Compilation is not a runtime contract test. Remaining public exports and a
versioned export inventory have not yet been enumerated.

## Sources

- [Microsoft Learn: CommDlg API index](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/)
- [Microsoft Learn: GetOpenFileNameW](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/nf-commdlg-getopenfilenamew)
- [Microsoft Learn: GetSaveFileNameW](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/nf-commdlg-getsavefilenamew)
- [Microsoft Learn: CommDlgExtendedError](https://learn.microsoft.com/en-us/windows/desktop/api/commdlg/nf-commdlg-commdlgextendederror)
- [Microsoft Learn: CHOOSECOLORW](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/ns-commdlg-choosecolorw)
