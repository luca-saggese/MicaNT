# OLE32 API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This batch covers the nine COM entry points currently declared by MicaNT; it
does not claim a full versioned Ole32 export inventory.

| Export | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `CoInitialize`, `CoInitializeEx` | `combaseapi.h` | UNSUPPORTED | Return `E_NOTIMPL`; no apartment state is recorded. |
| `CoUninitialize` | `combaseapi.h` | PARTIAL | No-op because initialization is unsupported; this cannot report failure through its `void` ABI. |
| `CoCreateInstance` | `combaseapi.h` | UNSUPPORTED | Clears a non-null output slot and returns `E_NOTIMPL`; no interface pointer is fabricated. |
| `CoTaskMemAlloc`, `CoTaskMemRealloc` | `combaseapi.h` | UNSUPPORTED | Return null; task-memory allocation is not connected to a compatible allocator. |
| `CoTaskMemFree` | `combaseapi.h` | PARTIAL | No-op; this DLL has not allocated task memory. Freeing pointers from an external allocator is unsupported. |
| `StringFromCLSID` | `combaseapi.h` | UNSUPPORTED | Clears a non-null output slot and returns `E_NOTIMPL`. |
| `CLSIDFromString` | `combaseapi.h` | UNSUPPORTED | Returns `E_NOTIMPL`; the caller's output GUID is unchanged. |

The typed entry points compile as a freestanding Ring 3 DLL target (`ole32_boot`).
No C++ exception crosses COM's HRESULT ABI. `E_NOTIMPL` is an explicit failure,
not a successful initialization or activation result.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ole32.dll` — documented exports in this batch | TBD | 9 | 9 | 0 | 2 | 0 | 7 | TBD |

The full versioned export inventory is pending. Compilation does not verify
COM apartment, memory ownership, or activation behavior at runtime.

## Sources

- [Microsoft Learn: CoInitializeEx](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex)
- [Microsoft Learn: CoCreateInstance](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreateinstance)
- [Microsoft Learn: CoTaskMemAlloc](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cotaskmemalloc)
- [Microsoft Learn: CoUninitialize](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-couninitialize)
- [Microsoft Learn: CLSIDFromString](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-clsidfromstring)
