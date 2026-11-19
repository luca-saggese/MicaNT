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

The legacy dynamic-loader bridge has a wider declaration surface than the
standalone DLL. Its formerly synthetic success paths now return `E_NOTIMPL`
for unsupported bind contexts, drag/drop, proxy security, class factories,
clipboard, OLE menu descriptors, COM marshaling, ProgID lookup, and WinRT
activation/string operations. Output interface slots are cleared where the
ABI defines an output pointer. `CoCreateGuid` no longer emits predictable
fabricated identifiers. `IsEqualGUID` compares all GUID fields. These bridge
exports remain a separate work item for ABI-by-ABI audit; this does not claim
full Ole32 or WinRT coverage.

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
