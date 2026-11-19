# OLEAUT32 API status — AMD64

Reference profile: Windows 11, version 24H2, AMD64 (OS build family 26100).
This batch covers the eight Automation exports currently present in the
legacy loader bridge. It is not a full versioned OleAut32 export inventory.

| Export | Microsoft declaration | State | Behavior |
| --- | --- | --- | --- |
| `SysAllocString`, `SysAllocStringLen` | `oleauto.h` | UNSUPPORTED | Return null; no BSTR allocation is reported as successful. |
| `SysFreeString` | `oleauto.h` | UNSUPPORTED | No-op because this DLL does not allocate BSTRs; freeing externally owned BSTR values is unsupported. |
| `SysStringLen`, `SysStringByteLen` | `oleauto.h` | IMPLEMENTED | Read the documented four-byte byte-length prefix; null BSTR returns zero. Non-null input must be a valid BSTR. |
| `VariantInit` | `oleauto.h` | IMPLEMENTED | Initializes a non-null VARIANTARG to `VT_EMPTY`. |
| `VariantClear` | `oleauto.h` | PARTIAL | Clears `VT_EMPTY` and `VT_NULL`; returns `E_NOTIMPL` without mutation for other variant types. Null returns `E_INVALIDARG`. |
| `VariantCopy` | `oleauto.h` | UNSUPPORTED | Returns `E_NOTIMPL`; destination is unchanged. |

The exports are built as a separate freestanding `oleaut32_boot` DLL and staged
as `oleaut32.dll`. String allocation and resource-owning variant operations
need a compatible user-mode allocator before they can be supported.

## Coverage ledger

| DLL | Reference exports | Declared | Documented | Implemented | Partial | Verified | Unsupported | Missing |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `oleaut32.dll` — exports in current bridge | TBD | 8 | 8 | 3 | 1 | 0 | 4 | TBD |

## Sources

- [Microsoft Learn: OleAut32 API index](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/)
- [Microsoft Learn: SysAllocString](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-sysallocstring)
- [Microsoft Learn: VariantInit](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-variantinit)
- [Microsoft Learn: VariantClear](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-variantclear)
