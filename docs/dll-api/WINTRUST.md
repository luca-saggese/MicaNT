# WINTRUST API status — AMD64

The current LuceOS `wintrust.dll` profile exports four trust and provider
helper entry points. Authenticode image hashing, trust-provider dispatch, and
verification-state ownership are not implemented. Valid verification requests
return `TRUST_E_PROVIDER_UNKNOWN`; malformed required inputs return
`TRUST_E_SUBJECT_FORM_UNKNOWN`. Provider helper lookups return `nullptr` and
never synthesize state or signer pointers.

| Export | State |
| --- | --- |
| `WinVerifyTrust` | UNSUPPORTED |
| `WinVerifyTrustEx` | UNSUPPORTED |
| `WTHelperProvDataFromStateData` | UNSUPPORTED |
| `WTHelperGetProvSignerFromChain` | UNSUPPORTED |

The Windows-version-specific reference export inventory is still pending. The
four names above describe the current LuceOS profile only.
