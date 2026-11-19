# IMM32 API status — AMD64

The current `imm32.dll` PE exports eight Imm32 entry points. They preserve
their public signatures and fail explicitly because MicaNT has no input method
manager or composition-context provider connected to User32/Win32k yet.

| Export | State | Failure behavior |
| --- | --- | --- |
| `ImmGetContext` | UNSUPPORTED | Returns `nullptr`; it does not manufacture an input-context handle. |
| `ImmReleaseContext` | UNSUPPORTED | Returns `FALSE`. |
| `ImmAssociateContext` | UNSUPPORTED | Returns `nullptr`; no association is changed. |
| `ImmAssociateContextEx` | UNSUPPORTED | Returns `FALSE`. |
| `ImmGetCompositionStringW` | UNSUPPORTED | Returns `IMM_ERROR_GENERAL`. |
| `ImmSetCompositionWindow` | UNSUPPORTED | Returns `FALSE`. |
| `ImmGetOpenStatus` | UNSUPPORTED | Returns `FALSE`. |
| `ImmSetOpenStatus` | UNSUPPORTED | Returns `FALSE`; it does not claim the IME was opened or closed. |

The module is build-integrated with Explorer. The exported surface remains a
small current MicaNT profile; this is not a claim of complete Windows IMM32
export coverage or runtime IME support.
