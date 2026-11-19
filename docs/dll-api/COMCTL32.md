# COMCTL32 API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This batch records all 16 exports currently implemented in MicaNT's
`comctl32_boot` image. It is not a complete versioned Comctl32 export list.

| Export family | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `InitCommonControlsEx` | `commctrl.h` | PARTIAL | Uses the documented two-DWORD structure and registers MicaNT's progress, tab, and tree classes; unsupported class flags fail. |
| `InitCommonControls` | `commctrl.h` | PARTIAL | Requests the implemented progress, tab, and tree classes. |
| `ImageList_*` (10 exports) | `commctrl.h` | PARTIAL | Process-local image-list operations are implemented with bounded storage and GDI/User32 calls; see `tools/dlls/comctl32_icon.cpp`. |
| `CreatePropertySheetPageA/W` | `prsht.h` | UNSUPPORTED | Returns a null page handle; page structures and callbacks are not implemented. |
| `PropertySheetA/W` | `prsht.h` | UNSUPPORTED | Returns `-1`; no property-sheet window is created. |

All entry points use the AMD64 Win32 ABI. The property-sheet stubs do not
manufacture handles or claim a successful dialog. The common-control class
registration subset remains narrower than Microsoft's complete class set.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `comctl32.dll` — exports in current image | TBD | 16 | 16 | 0 | 12 | 0 | 4 | TBD |

The image-list and three class registration paths compile and link; runtime
contracts and full Comctl32 export coverage remain unverified.

## Sources

- [Microsoft Learn: InitCommonControlsEx](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-initcommoncontrolsex)
- [Microsoft Learn: INITCOMMONCONTROLSEX](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/ns-commctrl-initcommoncontrolsex)
- [Microsoft Learn: CreatePropertySheetPageW](https://learn.microsoft.com/en-us/windows/win32/api/prsht/nf-prsht-createpropertysheetpagew)
- [Microsoft Learn: Property sheets](https://learn.microsoft.com/en-us/windows/win32/controls/property-sheets)
