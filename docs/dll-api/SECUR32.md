# SECUR32 API status — AMD64

The current LuceOS `secur32.dll` profile exports eight SSPI/name lookup
entry points. No credential package, SSPI context provider, message
protection service, or authenticated identity-name resolver is connected.
Valid operations return `SEC_E_UNSUPPORTED_FUNCTION`; invalid required
handles retain `SEC_E_INVALID_HANDLE`. New credential/context outputs and
username output are cleared on failure. Existing security handles are not
modified when a release operation is unsupported.

| Export | State |
| --- | --- |
| `AcquireCredentialsHandleW` | UNSUPPORTED |
| `FreeCredentialsHandle` | UNSUPPORTED |
| `InitializeSecurityContextW` | UNSUPPORTED |
| `DeleteSecurityContext` | UNSUPPORTED |
| `QueryContextAttributesW` | UNSUPPORTED |
| `EncryptMessage` | UNSUPPORTED |
| `DecryptMessage` | UNSUPPORTED |
| `GetUserNameExW` | UNSUPPORTED |

The versioned reference export inventory has not yet been extracted, so these
eight names are the current LuceOS surface, not a completeness claim for
Windows 11 `secur32.dll`.
