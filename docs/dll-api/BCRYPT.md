# BCRYPT API status — AMD64

The current LuceOS `bcrypt.dll` image contains 10 CNG entry points. Each
valid call that requires a CNG provider, cryptographic handle, random output,
hash, encryption, or key derivation returns `STATUS_NOT_SUPPORTED` until the
kernel entropy source and CNG primitive/handle ownership are connected.
Outputs that represent newly created handles are cleared; byte buffers are
left untouched; encryption output length is set to zero. Invalid required
arguments retain parameter/handle status errors.

| Export | State |
| --- | --- |
| `BCryptOpenAlgorithmProvider` | UNSUPPORTED |
| `BCryptCloseAlgorithmProvider` | UNSUPPORTED |
| `BCryptGenRandom` | UNSUPPORTED |
| `BCryptCreateHash` | UNSUPPORTED |
| `BCryptHashData` | UNSUPPORTED |
| `BCryptFinishHash` | UNSUPPORTED |
| `BCryptDestroyHash` | UNSUPPORTED |
| `BCryptEncrypt` | UNSUPPORTED |
| `BCryptDecrypt` | UNSUPPORTED |
| `BCryptDeriveKeyPBKDF2` | UNSUPPORTED |

The reference export inventory and its versioned count are pending. The 10
exports above describe only the current LuceOS profile; they do not claim full
Windows 11 `bcrypt.dll` coverage.
