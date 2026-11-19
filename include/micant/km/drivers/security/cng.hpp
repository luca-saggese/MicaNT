#pragma once
/**
 * @file cng.hpp
 * @brief Horizon 155: Sovereign Cryptography Next Generation (CNG) Kernel Driver (cng.sys),
 *        Core Cryptographic Primitives (SHA-256, SHA-384, HMAC, HKDF, AES-GCM, X25519)
 *        & Kernel-Mode IOCTL Dispatch Interface (\Device\Cng).
 * 
 * Target Architecture: Supermicro X11SSH-F (Intel Xeon E3-1270 v6 @ 3.80GHz, 32GB DDR4 ECC).
 * Clean-Room Standard: 17 U.S.C. § 1201(f) & Google LLC v. Oracle America, Inc. (593 U.S. 1).
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <memory>
#include <mutex>
#include <optional>
#include <algorithm>

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/crypto_primitives.hpp"
#include "micant/um/servers/scm.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::km::crypto {

using micant::shared::crypto::Sha1;
using micant::shared::crypto::Sha256;
using micant::shared::crypto::Sha384;
using micant::shared::crypto::Sha512;
using micant::shared::crypto::RsaSha256Verifier;
using micant::shared::crypto::HmacSha256;
using micant::shared::crypto::HkdfSha256;
using micant::shared::crypto::AesCore;
using micant::shared::crypto::AesGcm;
using micant::shared::crypto::X25519;
using micant::shared::crypto::CryptoRng;

// ============================================================================
// Standard Windows CNG Kernel Constants & Types
// ============================================================================

using BCRYPT_HANDLE      = void*;
using BCRYPT_ALG_HANDLE  = void*;
using BCRYPT_KEY_HANDLE  = void*;
using BCRYPT_HASH_HANDLE = void*;
using BCRYPT_SECRET_HANDLE = void*;

using micant::BCryptBuffer;
using micant::BCryptBufferDesc;

#ifndef BCRYPT_KDF_RAW_SECRET
inline constexpr const wchar_t* BCRYPT_KDF_RAW_SECRET = L"TRUNCATE";
inline constexpr const wchar_t* BCRYPT_KDF_HASH       = L"HASH";
inline constexpr const wchar_t* BCRYPT_KDF_HMAC       = L"HMAC";
inline constexpr const wchar_t* BCRYPT_KDF_TLS_PRF    = L"TLS_PRF";
#endif

inline constexpr const wchar_t* BCRYPT_SHA1_ALGORITHM        = L"SHA1";
inline constexpr const wchar_t* BCRYPT_SHA256_ALGORITHM      = L"SHA256";
inline constexpr const wchar_t* BCRYPT_SHA384_ALGORITHM      = L"SHA384";
inline constexpr const wchar_t* BCRYPT_SHA512_ALGORITHM      = L"SHA512";
inline constexpr const wchar_t* BCRYPT_AES_ALGORITHM         = L"AES";
inline constexpr const wchar_t* BCRYPT_RNG_ALGORITHM         = L"RNG";
inline constexpr const wchar_t* BCRYPT_ECDH_P256_ALGORITHM   = L"ECDH_P256";
inline constexpr const wchar_t* BCRYPT_X25519_ALGORITHM      = L"X25519";
inline constexpr const wchar_t* BCRYPT_HMAC_SHA256_ALGORITHM = L"HMAC-SHA256";

inline constexpr const wchar_t* BCRYPT_CHAINING_MODE         = L"ChainingMode";
inline constexpr const wchar_t* BCRYPT_CHAIN_MODE_GCM        = L"ChainingModeGCM";
inline constexpr const wchar_t* BCRYPT_CHAIN_MODE_CBC        = L"ChainingModeCBC";
inline constexpr const wchar_t* BCRYPT_CHAIN_MODE_ECB        = L"ChainingModeECB";

inline constexpr uint32_t BCRYPT_ALG_HANDLE_HMAC_FLAG        = 0x00000008;

// CNG Kernel IOCTL Codes (\Device\Cng)
inline constexpr uint32_t IOCTL_CNG_OPEN_ALGORITHM_PROVIDER  = 0x00390000;
inline constexpr uint32_t IOCTL_CNG_CREATE_HASH              = 0x00390004;
inline constexpr uint32_t IOCTL_CNG_HASH_DATA                = 0x00390008;
inline constexpr uint32_t IOCTL_CNG_FINISH_HASH              = 0x0039000C;
inline constexpr uint32_t IOCTL_CNG_ENCRYPT                  = 0x00390010;
inline constexpr uint32_t IOCTL_CNG_DECRYPT                  = 0x00390014;
inline constexpr uint32_t IOCTL_CNG_GEN_RANDOM               = 0x00390018;
inline constexpr uint32_t IOCTL_CNG_KEY_DERIVATION           = 0x0039001C;

// Standard CNG Status Codes
inline constexpr NTSTATUS STATUS_AUTH_TAG_NEEDS_VERIFY         = static_cast<NTSTATUS>(0xC000A002);
inline constexpr NTSTATUS STATUS_INVALID_BUFFER_SIZE           = static_cast<NTSTATUS>(0xC0000206);
inline constexpr NTSTATUS STATUS_INVALID_DEVICE_REQUEST         = static_cast<NTSTATUS>(0xC0000010);

// ============================================================================
// 8. Sovereign Cryptography Next Generation Kernel Driver (cng.sys)
// ============================================================================

class CngKernelDriver {
public:
    static CngKernelDriver& get() noexcept {
        static CngKernelDriver instance;
        return instance;
    }

    NTSTATUS dispatchIoctl(uint32_t ioctlCode, std::span<const uint8_t> inBuf, std::vector<uint8_t>& outBuf) {
        std::lock_guard<std::mutex> lock(mutex_);
        totalIoctls_++;

        switch (ioctlCode) {
            case IOCTL_CNG_OPEN_ALGORITHM_PROVIDER: {
                if (inBuf.empty()) return STATUS_INVALID_PARAMETER;
                // Return provider handle (mock handle 0x1000)
                outBuf.resize(sizeof(uint64_t));
                uint64_t h = 0x1000;
                std::memcpy(outBuf.data(), &h, sizeof(h));
                return STATUS_SUCCESS;
            }
            case IOCTL_CNG_HASH_DATA: {
                if (inBuf.empty()) return STATUS_INVALID_PARAMETER;
                if (inBuf.size() > 1 && inBuf[0] == 0x01) {
                    auto digest = Sha1::hash(inBuf.subspan(1));
                    outBuf.assign(digest.begin(), digest.end());
                } else if (inBuf.size() > 1 && inBuf[0] == 0x03) {
                    auto digest = Sha384::hash(inBuf.subspan(1));
                    outBuf.assign(digest.begin(), digest.end());
                } else if (inBuf.size() > 1 && inBuf[0] == 0x05) {
                    auto digest = Sha512::hash(inBuf.subspan(1));
                    outBuf.assign(digest.begin(), digest.end());
                } else {
                    auto digest = Sha256::hash(inBuf);
                    outBuf.assign(digest.begin(), digest.end());
                }
                hashesComputed_++;
                return STATUS_SUCCESS;
            }
            case IOCTL_CNG_ENCRYPT: {
                // Input format: Key(32) || Nonce(12) || AAD_Len(4) || AAD || Plaintext
                if (inBuf.size() < 48) return STATUS_INVALID_BUFFER_SIZE;
                std::span<const uint8_t> key(inBuf.data(), 32);
                std::span<const uint8_t> nonce(inBuf.data() + 32, 12);
                uint32_t aadLen = (static_cast<uint32_t>(inBuf[44]) << 24) |
                                  (static_cast<uint32_t>(inBuf[45]) << 16) |
                                  (static_cast<uint32_t>(inBuf[46]) << 8)  |
                                  (static_cast<uint32_t>(inBuf[47]));
                if (inBuf.size() < 48 + aadLen) return STATUS_INVALID_PARAMETER;
                std::span<const uint8_t> aad(inBuf.data() + 48, aadLen);
                std::span<const uint8_t> pt(inBuf.data() + 48 + aadLen, inBuf.size() - (48 + aadLen));

                micant::shared::BoundedVector<uint8_t> ct;
                std::array<uint8_t, 16> tag{};
                if (!AesGcm::encrypt(key, nonce, aad, pt, ct, tag)) {
                    return STATUS_UNSUCCESSFUL;
                }

                outBuf.resize(ct.size() + 16);
                std::memcpy(outBuf.data(), ct.data(), ct.size());
                std::memcpy(outBuf.data() + ct.size(), tag.data(), 16);
                encryptionsPerformed_++;
                return STATUS_SUCCESS;
            }
            case IOCTL_CNG_DECRYPT: {
                // Input format: Key(32) || Nonce(12) || Tag(16) || AAD_Len(4) || AAD || Ciphertext
                if (inBuf.size() < 64) return STATUS_INVALID_BUFFER_SIZE;
                std::span<const uint8_t> key(inBuf.data(), 32);
                std::span<const uint8_t> nonce(inBuf.data() + 32, 12);
                std::span<const uint8_t> tag(inBuf.data() + 44, 16);
                uint32_t aadLen = (static_cast<uint32_t>(inBuf[60]) << 24) |
                                  (static_cast<uint32_t>(inBuf[61]) << 16) |
                                  (static_cast<uint32_t>(inBuf[62]) << 8)  |
                                  (static_cast<uint32_t>(inBuf[63]));
                if (inBuf.size() < 64 + aadLen) return STATUS_INVALID_PARAMETER;
                std::span<const uint8_t> aad(inBuf.data() + 64, aadLen);
                std::span<const uint8_t> ct(inBuf.data() + 64 + aadLen, inBuf.size() - (64 + aadLen));

                micant::shared::BoundedVector<uint8_t> pt;
                if (!AesGcm::decrypt(key, nonce, aad, ct, tag, pt)) {
                    return STATUS_AUTH_TAG_NEEDS_VERIFY;
                }

                outBuf.assign(pt.begin(), pt.end());
                decryptionsPerformed_++;
                return STATUS_SUCCESS;
            }
            case IOCTL_CNG_GEN_RANDOM: {
                uint32_t reqBytes = inBuf.empty() ? 32 : inBuf[0];
                if (reqBytes == 0) reqBytes = 32;
                outBuf.resize(reqBytes);
                if (!CryptoRng::generate(outBuf)) { outBuf.clear(); return STATUS_NOT_SUPPORTED; }
                randomBytesGenerated_ += reqBytes;
                return STATUS_SUCCESS;
            }
            case IOCTL_CNG_KEY_DERIVATION: {
                // HKDF-Extract-and-Expand
                if (inBuf.size() < 32) return STATUS_INVALID_PARAMETER;
                std::span<const uint8_t> ikm(inBuf.data(), 32);
                auto prk = HkdfSha256::extract({}, ikm);
                std::string_view info = "MicaNT-CNG-DerivedKey";
                auto okm = HkdfSha256::expand(prk, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(info.data()), info.size()), 32);
                outBuf = std::move(okm);
                return STATUS_SUCCESS;
            }
            default:
                return STATUS_INVALID_DEVICE_REQUEST;
        }
    }

    struct DriverTelemetry {
        uint64_t totalIoctls{0};
        uint64_t hashesComputed{0};
        uint64_t encryptionsPerformed{0};
        uint64_t decryptionsPerformed{0};
        uint64_t randomBytesGenerated{0};
    };

    [[nodiscard]] DriverTelemetry getTelemetry() const noexcept {
        return {
            totalIoctls_,
            hashesComputed_,
            encryptionsPerformed_,
            decryptionsPerformed_,
            randomBytesGenerated_
        };
    }

    void resetForTest() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        totalIoctls_ = 0;
        hashesComputed_ = 0;
        encryptionsPerformed_ = 0;
        decryptionsPerformed_ = 0;
        randomBytesGenerated_ = 0;
    }

private:
    CngKernelDriver() = default;
    std::mutex mutex_;
    uint64_t totalIoctls_{0};
    uint64_t hashesComputed_{0};
    uint64_t encryptionsPerformed_{0};
    uint64_t decryptionsPerformed_{0};
    uint64_t randomBytesGenerated_{0};
};

// ============================================================================
// 9. Kernel C ABI Exports for bcrypt.dll / cng.sys
// ============================================================================

inline NTSTATUS WINAPI BCryptGenRandom(
    BCRYPT_ALG_HANDLE /*hAlgorithm*/,
    uint8_t* pbBuffer,
    uint32_t cbBuffer,
    uint32_t /*dwFlags*/) noexcept
{
    if (!pbBuffer || cbBuffer == 0) return STATUS_INVALID_PARAMETER;
    if (!CryptoRng::generate(std::span<uint8_t>(pbBuffer, cbBuffer))) return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptCreateHash(
    BCRYPT_ALG_HANDLE /*hAlgorithm*/,
    BCRYPT_HASH_HANDLE* phHash,
    uint8_t* /*pbHashObject*/,
    uint32_t /*cbHashObject*/,
    uint8_t* /*pbSecret*/,
    uint32_t /*cbSecret*/,
    uint32_t /*dwFlags*/) noexcept
{
    if (!phHash) return STATUS_INVALID_PARAMETER;
    auto* ctx = new Sha256::Context();
    Sha256::init(*ctx);
    *phHash = reinterpret_cast<BCRYPT_HASH_HANDLE>(ctx);
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptHashData(
    BCRYPT_HASH_HANDLE hHash,
    uint8_t* pbInput,
    uint32_t cbInput,
    uint32_t /*dwFlags*/) noexcept
{
    if (!hHash || !pbInput) return STATUS_INVALID_PARAMETER;
    auto* ctx = reinterpret_cast<Sha256::Context*>(hHash);
    Sha256::update(*ctx, std::span<const uint8_t>(pbInput, cbInput));
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptFinishHash(
    BCRYPT_HASH_HANDLE hHash,
    uint8_t* pbOutput,
    uint32_t cbOutput,
    uint32_t /*dwFlags*/) noexcept
{
    if (!hHash || !pbOutput || cbOutput < 32) return STATUS_INVALID_PARAMETER;
    auto* ctx = reinterpret_cast<Sha256::Context*>(hHash);
    Sha256::final(*ctx, std::span<uint8_t, 32>(pbOutput, 32));
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptDestroyHash(BCRYPT_HASH_HANDLE hHash) noexcept {
    if (!hHash) return STATUS_INVALID_PARAMETER;
    auto* ctx = reinterpret_cast<Sha256::Context*>(hHash);
    delete ctx;
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptDuplicateHash(
    BCRYPT_HASH_HANDLE hHash,
    BCRYPT_HASH_HANDLE* phNewHash,
    [[maybe_unused]] uint8_t* pbHashObject,
    [[maybe_unused]] uint32_t cbHashObject,
    [[maybe_unused]] uint32_t dwFlags) noexcept {
    if (!hHash || !phNewHash) return STATUS_INVALID_PARAMETER;
    auto* src = reinterpret_cast<Sha256::Context*>(hHash);
    auto* dup = new Sha256::Context(*src);
    *phNewHash = reinterpret_cast<BCRYPT_HASH_HANDLE>(dup);
    return STATUS_SUCCESS;
}

struct KernelKeyObject {
    std::vector<uint8_t> secret;
    std::wstring alg;
};

struct KernelSecretObject {
    std::vector<uint8_t> sharedSecret;
};

inline std::mutex s_cngKeyLock;
inline std::unordered_map<void*, std::shared_ptr<KernelKeyObject>> s_cngKeys;
inline std::unordered_map<void*, std::shared_ptr<KernelSecretObject>> s_cngSecrets;
inline uintptr_t s_nextCngHandle = 0x6000;

inline NTSTATUS WINAPI BCryptDestroyKey(BCRYPT_KEY_HANDLE hKey) noexcept {
    if (!hKey) return STATUS_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    return s_cngKeys.erase(hKey) > 0 ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

inline NTSTATUS WINAPI BCryptExportKey(
    BCRYPT_KEY_HANDLE hKey,
    [[maybe_unused]] BCRYPT_KEY_HANDLE hExportKey,
    [[maybe_unused]] const wchar_t* pszBlobType,
    uint8_t* pbOutput,
    uint32_t cbOutput,
    uint32_t* pcbResult,
    [[maybe_unused]] uint32_t dwFlags) noexcept
{
    if (!hKey || !pcbResult) return STATUS_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    auto it = s_cngKeys.find(hKey);
    if (it == s_cngKeys.end()) return STATUS_INVALID_HANDLE;

    *pcbResult = static_cast<uint32_t>(it->second->secret.size());
    if (!pbOutput) return STATUS_SUCCESS;
    if (cbOutput < it->second->secret.size()) return STATUS_BUFFER_TOO_SMALL;

    std::memcpy(pbOutput, it->second->secret.data(), it->second->secret.size());
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptImportKey(
    [[maybe_unused]] BCRYPT_ALG_HANDLE hAlgorithm,
    [[maybe_unused]] BCRYPT_KEY_HANDLE hImportKey,
    [[maybe_unused]] const wchar_t* pszBlobType,
    BCRYPT_KEY_HANDLE* phKey,
    [[maybe_unused]] uint8_t* pbKeyObject,
    [[maybe_unused]] uint32_t cbKeyObject,
    const uint8_t* pbInput,
    uint32_t cbInput,
    [[maybe_unused]] uint32_t dwFlags) noexcept
{
    if (!phKey || !pbInput || cbInput == 0) return STATUS_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    auto keyObj = std::make_shared<KernelKeyObject>();
    keyObj->secret.assign(pbInput, pbInput + cbInput);
    keyObj->alg = L"AES";
    void* handle = reinterpret_cast<void*>(s_nextCngHandle++);
    s_cngKeys[handle] = keyObj;
    *phKey = handle;
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptSecretAgreement(
    BCRYPT_KEY_HANDLE hPrivKey,
    BCRYPT_KEY_HANDLE hPubKey,
    BCRYPT_SECRET_HANDLE* phAgreedSecret,
    [[maybe_unused]] uint32_t dwFlags) noexcept
{
    if (!hPrivKey || !hPubKey || !phAgreedSecret) return STATUS_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    auto itPriv = s_cngKeys.find(hPrivKey);
    auto itPub = s_cngKeys.find(hPubKey);
    if (itPriv == s_cngKeys.end() || itPub == s_cngKeys.end()) return STATUS_INVALID_HANDLE;

    auto secretObj = std::make_shared<KernelSecretObject>();
    const auto& s1 = itPriv->second->secret;
    const auto& s2 = itPub->second->secret;
    std::vector<uint8_t> combined;
    if (s1 < s2) {
        combined.insert(combined.end(), s1.begin(), s1.end());
        combined.insert(combined.end(), s2.begin(), s2.end());
    } else {
        combined.insert(combined.end(), s2.begin(), s2.end());
        combined.insert(combined.end(), s1.begin(), s1.end());
    }
    auto h = Sha256::hash(combined);
    secretObj->sharedSecret.assign(h.begin(), h.end());

    void* handle = reinterpret_cast<void*>(s_nextCngHandle++);
    s_cngSecrets[handle] = secretObj;
    *phAgreedSecret = handle;
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptDestroySecret(BCRYPT_SECRET_HANDLE hSecret) noexcept {
    if (!hSecret) return STATUS_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    return s_cngSecrets.erase(hSecret) > 0 ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

inline NTSTATUS WINAPI BCryptDeriveKey(
    BCRYPT_SECRET_HANDLE hSharedSecret,
    const wchar_t* pwszKDF,
    [[maybe_unused]] BCryptBufferDesc* pParameterList,
    uint8_t* pbDerivedKey,
    uint32_t cbDerivedKey,
    uint32_t* pcbResult,
    [[maybe_unused]] uint32_t dwFlags) noexcept
{
    if (!hSharedSecret || !pcbResult) return STATUS_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    auto it = s_cngSecrets.find(hSharedSecret);
    if (it == s_cngSecrets.end()) return STATUS_INVALID_HANDLE;

    const auto& sec = it->second->sharedSecret;
    *pcbResult = cbDerivedKey;
    if (!pbDerivedKey) return STATUS_SUCCESS;

    if (pwszKDF == nullptr || std::wcscmp(pwszKDF, BCRYPT_KDF_RAW_SECRET) == 0) {
        size_t copyLen = std::min(static_cast<size_t>(cbDerivedKey), sec.size());
        std::memcpy(pbDerivedKey, sec.data(), copyLen);
        if (cbDerivedKey > copyLen) {
            std::memset(pbDerivedKey + copyLen, 0, cbDerivedKey - copyLen);
        }
        return STATUS_SUCCESS;
    }

    if (std::wcscmp(pwszKDF, BCRYPT_KDF_HASH) == 0) {
        auto h = Sha256::hash(sec);
        size_t copyLen = std::min(static_cast<size_t>(cbDerivedKey), h.size());
        std::memcpy(pbDerivedKey, h.data(), copyLen);
        if (cbDerivedKey > copyLen) {
            std::memset(pbDerivedKey + copyLen, 0, cbDerivedKey - copyLen);
        }
        return STATUS_SUCCESS;
    }

    if (std::wcscmp(pwszKDF, BCRYPT_KDF_HMAC) == 0) {
        auto h = HmacSha256::compute(sec, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("MicaNT_KDF_HMAC"), 15));
        size_t copyLen = std::min(static_cast<size_t>(cbDerivedKey), h.size());
        std::memcpy(pbDerivedKey, h.data(), copyLen);
        if (cbDerivedKey > copyLen) {
            std::memset(pbDerivedKey + copyLen, 0, cbDerivedKey - copyLen);
        }
        return STATUS_SUCCESS;
    }

    size_t copyLen = std::min(static_cast<size_t>(cbDerivedKey), sec.size());
    std::memcpy(pbDerivedKey, sec.data(), copyLen);
    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptDeriveKeyPBKDF2(
    [[maybe_unused]] BCRYPT_ALG_HANDLE hPrf,
    const uint8_t* pbPassword,
    uint32_t cbPassword,
    const uint8_t* pbSalt,
    uint32_t cbSalt,
    uint64_t cIterations,
    uint8_t* pbDerivedKey,
    uint32_t cbDerivedKey,
    [[maybe_unused]] uint32_t dwFlags
) noexcept {
    if (!pbPassword || !pbSalt || !pbDerivedKey || cbDerivedKey == 0 || cIterations == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    std::span<const uint8_t> keySpan(pbPassword, cbPassword);
    uint32_t blocksNeeded = (cbDerivedKey + 31) / 32;
    uint32_t written = 0;

    for (uint32_t block = 1; block <= blocksNeeded; ++block) {
        std::vector<uint8_t> saltBlock(pbSalt, pbSalt + cbSalt);
        saltBlock.push_back(static_cast<uint8_t>((block >> 24) & 0xFF));
        saltBlock.push_back(static_cast<uint8_t>((block >> 16) & 0xFF));
        saltBlock.push_back(static_cast<uint8_t>((block >> 8) & 0xFF));
        saltBlock.push_back(static_cast<uint8_t>(block & 0xFF));

        auto uPrev = HmacSha256::compute(keySpan, saltBlock);
        std::array<uint8_t, 32> uSum = uPrev;

        for (uint64_t iter = 1; iter < cIterations; ++iter) {
            uPrev = HmacSha256::compute(keySpan, uPrev);
            for (size_t i = 0; i < 32; ++i) {
                uSum[i] ^= uPrev[i];
            }
        }

        uint32_t toCopy = std::min<uint32_t>(32, cbDerivedKey - written);
        std::memcpy(pbDerivedKey + written, uSum.data(), toCopy);
        written += toCopy;
    }

    return STATUS_SUCCESS;
}

inline NTSTATUS WINAPI BCryptKeyDerivation(
    BCRYPT_KEY_HANDLE hKey,
    BCryptBufferDesc* pParameterList,
    uint8_t* pbDerivedKey,
    ULONG cbDerivedKey,
    ULONG* pcbResult,
    [[maybe_unused]] ULONG dwFlags
) noexcept {
    if (!hKey || !pbDerivedKey || cbDerivedKey == 0) return STATUS_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(s_cngKeyLock);
    auto it = s_cngKeys.find(hKey);
    if (it == s_cngKeys.end()) return STATUS_INVALID_HANDLE;

    std::vector<uint8_t> saltOrContext;
    if (pParameterList && pParameterList->pBuffers) {
        for (ULONG i = 0; i < pParameterList->cBuffers; ++i) {
            if (pParameterList->pBuffers[i].pvBuffer && pParameterList->pBuffers[i].cbBuffer > 0) {
                const auto* p = reinterpret_cast<const uint8_t*>(pParameterList->pBuffers[i].pvBuffer);
                saltOrContext.insert(saltOrContext.end(), p, p + pParameterList->pBuffers[i].cbBuffer);
            }
        }
    }
    if (saltOrContext.empty()) {
        saltOrContext.push_back(0x01);
    }

    std::span<const uint8_t> keySpan(it->second->secret.data(), it->second->secret.size());
    auto hash = HmacSha256::compute(keySpan, saltOrContext);

    uint32_t copyLen = std::min<uint32_t>(cbDerivedKey, 32);
    std::memcpy(pbDerivedKey, hash.data(), copyLen);
    if (cbDerivedKey > copyLen) {
        std::memset(pbDerivedKey + copyLen, 0, cbDerivedKey - copyLen);
    }
    if (pcbResult) *pcbResult = copyLen;
    return STATUS_SUCCESS;
}

// ============================================================================
// 10. SCM Driver & Module Registration for cng.sys
// ============================================================================

inline void InitializeCngKernelDriver() {
    // 1. SCM Registration for cng.sys (Boot-Start Kernel Driver)
    auto& scm = scm::ServiceControlManager::get();
    auto cngSvc = std::make_shared<scm::ServiceRecord>();
    cngSvc->serviceName   = L"cng";
    cngSvc->displayName   = L"Cryptography Next Generation Driver";
    cngSvc->serviceType   = scm::SERVICE_KERNEL_DRIVER;
    cngSvc->startType     = scm::SERVICE_BOOT_START;
    cngSvc->errorControl  = scm::SERVICE_ERROR_CRITICAL;
    cngSvc->binaryPath    = L"C:\\Windows\\System32\\drivers\\cng.sys";
    cngSvc->status.dwCurrentState = scm::SERVICE_RUNNING;
    scm.registerServiceRecord(cngSvc);

    // 2. SCM Registration for CryptSvc (Cryptographic Services)
    auto cryptSvc = std::make_shared<scm::ServiceRecord>();
    cryptSvc->serviceName = L"CryptSvc";
    cryptSvc->displayName = L"Cryptographic Services";
    cryptSvc->serviceType = scm::SERVICE_WIN32_SHARE_PROCESS;
    cryptSvc->startType   = scm::SERVICE_AUTO_START;
    cryptSvc->errorControl = scm::SERVICE_ERROR_NORMAL;
    cryptSvc->binaryPath  = L"C:\\Windows\\System32\\svchost.exe -k LocalServiceCrypto";
    cryptSvc->status.dwCurrentState = scm::SERVICE_RUNNING;
    scm.registerServiceRecord(cryptSvc);

    // 3. Register Module in VersionDatabase
    auto& verDb = version::VersionDatabase::Instance();
    verDb.RegisterModule("cng.sys", "10.0.26100.5900", "MicaNT Cryptography Next Generation Driver");
    verDb.RegisterModule("bcrypt.dll", "10.0.26100.5900", "MicaNT Windows Cryptographic Primitives Library");

    // 4. Register Exports in JanusLDR
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("cng.sys", "BCryptGenRandom", reinterpret_cast<void*>(BCryptGenRandom));
    ldr.registerExport("cng.sys", "BCryptCreateHash", reinterpret_cast<void*>(BCryptCreateHash));
    ldr.registerExport("cng.sys", "BCryptHashData", reinterpret_cast<void*>(BCryptHashData));
    ldr.registerExport("cng.sys", "BCryptFinishHash", reinterpret_cast<void*>(BCryptFinishHash));
    ldr.registerExport("cng.sys", "BCryptDestroyHash", reinterpret_cast<void*>(BCryptDestroyHash));
    ldr.registerExport("cng.sys", "BCryptDuplicateHash", reinterpret_cast<void*>(BCryptDuplicateHash));
    ldr.registerExport("cng.sys", "BCryptDestroyKey", reinterpret_cast<void*>(BCryptDestroyKey));
    ldr.registerExport("cng.sys", "BCryptExportKey", reinterpret_cast<void*>(BCryptExportKey));
    ldr.registerExport("cng.sys", "BCryptImportKey", reinterpret_cast<void*>(BCryptImportKey));
    ldr.registerExport("cng.sys", "BCryptSecretAgreement", reinterpret_cast<void*>(BCryptSecretAgreement));
    ldr.registerExport("cng.sys", "BCryptDestroySecret", reinterpret_cast<void*>(BCryptDestroySecret));
    ldr.registerExport("cng.sys", "BCryptDeriveKey", reinterpret_cast<void*>(BCryptDeriveKey));
    ldr.registerExport("cng.sys", "BCryptDeriveKeyPBKDF2", reinterpret_cast<void*>(BCryptDeriveKeyPBKDF2));
    ldr.registerExport("cng.sys", "BCryptKeyDerivation", reinterpret_cast<void*>(BCryptKeyDerivation));

    ldr.registerExport("bcrypt.dll", "BCryptSecretAgreement", reinterpret_cast<void*>(BCryptSecretAgreement));
    ldr.registerExport("bcrypt.dll", "BCryptDestroySecret", reinterpret_cast<void*>(BCryptDestroySecret));
    ldr.registerExport("bcrypt.dll", "BCryptDeriveKey", reinterpret_cast<void*>(BCryptDeriveKey));
    ldr.registerExport("bcrypt.dll", "BCryptDeriveKeyPBKDF2", reinterpret_cast<void*>(BCryptDeriveKeyPBKDF2));
    ldr.registerExport("bcrypt.dll", "BCryptKeyDerivation", reinterpret_cast<void*>(BCryptKeyDerivation));
}

} // namespace micant::km::crypto
