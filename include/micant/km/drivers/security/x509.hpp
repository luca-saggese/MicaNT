#pragma once
/**
 * @file x509.hpp
 * @brief Horizon 155: Sovereign X.509 Certificate Architecture, ASN.1 DER Parser/Serializer,
 *        Machine Identity Generation (CN=MicaNT-Server-147.135.1.229) & System Certificate Store (crypt32.dll).
 * 
 * Target Architecture: Supermicro X11SSH-F (Intel Xeon E3-1270 v6 @ 3.80GHz, 32GB DDR4 ECC).
 * Clean-Room Standard: 17 U.S.C. § 1201(f) & Google LLC v. Oracle America, Inc. (593 U.S. 1).
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <cctype>

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/drivers/security/cng.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/shared/x509_profile.hpp"

namespace micant::km::crypto {

using micant::shared::crypto::X509Certificate;
namespace asn1 = micant::shared::crypto::asn1;

// ============================================================================
// 3. Sovereign Self-Signed Machine Certificate Generator
// ============================================================================

class MachineCertificateGenerator {
public:
    static X509Certificate generateSelfSignedMachineCert(
        std::string_view commonName = "MicaNT-Server-147.135.1.229",
        std::string_view ipAddress  = "147.135.1.229")
    {
        X509Certificate cert;
        cert.serialNumber = 0x202610290001ULL;
        cert.subjectCommonName = commonName;
        cert.issuerCommonName  = commonName;
        cert.validNotBefore    = "261029000000Z"; // 2026-10-29
        cert.validNotAfter     = "361029000000Z"; // 2036-10-29 (10-year validity)
        cert.dnsNames.push_back("localhost");
        cert.dnsNames.push_back("micant.local");
        cert.dnsNames.push_back("server.micant.local");
        cert.ipAddresses.push_back(std::string(ipAddress));
        cert.ipAddresses.push_back("127.0.0.1");

        // Generate 32-byte public key (e.g. X25519)
        cert.publicKey.resize(32);
        for (size_t i = 0; i < 32; ++i) {
            cert.publicKey[i] = static_cast<uint8_t>((0x42 + i * 7) & 0xFF);
        }

        // Construct ASN.1 DER TBSCertificate (To Be Signed Certificate)
        std::vector<uint8_t> tbs;

        // 1. Version [0] EXPLICIT v3 (value = 2)
        std::vector<uint8_t> verInner;
        asn1::writeInteger(verInner, 2);
        asn1::writeTLV(tbs, asn1::TAG_CONTEXT_0, verInner);

        // 2. Serial Number
        asn1::writeInteger(tbs, cert.serialNumber);

        // 3. Signature Algorithm (sha256WithRSAEncryption: 1.2.840.113549.1.1.11)
        static const uint8_t sha256RsaOid[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B };
        std::vector<uint8_t> sigAlgInner;
        asn1::writeOid(sigAlgInner, sha256RsaOid);
        sigAlgInner.push_back(asn1::TAG_NULL);
        sigAlgInner.push_back(0);
        asn1::writeSequence(tbs, sigAlgInner);

        // 4. Issuer (CN = commonName)
        std::vector<uint8_t> issuerRdn;
        static const uint8_t cnOid[] = { 0x55, 0x04, 0x03 }; // 2.5.4.3
        std::vector<uint8_t> atvInner;
        asn1::writeOid(atvInner, cnOid);
        asn1::writePrintableString(atvInner, cert.issuerCommonName);
        std::vector<uint8_t> atvSeq;
        asn1::writeSequence(atvSeq, atvInner);
        asn1::writeTLV(issuerRdn, asn1::TAG_SET, atvSeq);
        asn1::writeSequence(tbs, issuerRdn);

        // 5. Validity
        std::vector<uint8_t> valSeq;
        asn1::writeUtcTime(valSeq, cert.validNotBefore);
        asn1::writeUtcTime(valSeq, cert.validNotAfter);
        asn1::writeSequence(tbs, valSeq);

        // 6. Subject (CN = commonName)
        asn1::writeSequence(tbs, issuerRdn);

        // 7. SubjectPublicKeyInfo
        std::vector<uint8_t> spki;
        std::vector<uint8_t> pkAlg;
        static const uint8_t rsaOid[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 };
        asn1::writeOid(pkAlg, rsaOid);
        pkAlg.push_back(asn1::TAG_NULL);
        pkAlg.push_back(0);
        asn1::writeSequence(spki, pkAlg);
        std::vector<uint8_t> bitStr;
        bitStr.push_back(0x00); // 0 unused bits
        bitStr.insert(bitStr.end(), cert.publicKey.begin(), cert.publicKey.end());
        asn1::writeTLV(spki, asn1::TAG_BIT_STRING, bitStr);
        asn1::writeSequence(tbs, spki);

        // Compute SHA-256 over TBS for mock signature & fingerprint
        cert.fingerprintSha256 = Sha256::hash(tbs);
        cert.signature.assign(cert.fingerprintSha256.begin(), cert.fingerprintSha256.end());

        // Full Certificate Sequence = Sequence { TBSCertificate, AlgorithmIdentifier, BitString(Signature) }
        std::vector<uint8_t> certDer;
        std::vector<uint8_t> fullInner;
        asn1::writeSequence(fullInner, tbs);
        asn1::writeSequence(fullInner, sigAlgInner);
        std::vector<uint8_t> sigBitStr;
        sigBitStr.push_back(0x00);
        sigBitStr.insert(sigBitStr.end(), cert.signature.begin(), cert.signature.end());
        asn1::writeTLV(fullInner, asn1::TAG_BIT_STRING, sigBitStr);
        asn1::writeSequence(certDer, fullInner);

        cert.rawDer.assign(certDer.begin(), certDer.end());
        return cert;
    }
};

// ============================================================================
// 4. Sovereign System Certificate Store Registry (crypt32.dll)
// ============================================================================

class SovereignCertStoreRegistry {
public:
    static SovereignCertStoreRegistry& get() noexcept {
        static SovereignCertStoreRegistry instance;
        return instance;
    }

    void initializeDefaultStores() {
        std::lock_guard<std::mutex> lock(mutex_);
        initializeDefaultStoresLocked();
    }

    bool addCertificate(std::string_view storeName, const X509Certificate& cert) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (storeName == "MY") {
            myStore_.push_back(cert);
            return true;
        } else if (storeName == "ROOT") {
            rootStore_.push_back(cert);
            return true;
        } else if (storeName == "CA") {
            caStore_.push_back(cert);
            return true;
        }
        return false;
    }

    std::optional<X509Certificate> findCertificateBySubject(std::string_view storeName, std::string_view subject) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* targetStore = getStoreByName(storeName);
        if (!targetStore) return std::nullopt;

        for (const auto& c : *targetStore) {
            if (c.subjectCommonName == subject) return c;
        }
        return std::nullopt;
    }

    std::optional<X509Certificate> findCertificateByHost(std::string_view host) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& c : myStore_) {
            if (c.matchesHost(host)) return c;
        }
        for (const auto& c : rootStore_) {
            if (c.matchesHost(host)) return c;
        }
        return std::nullopt;
    }

    [[nodiscard]] size_t getStoreCount(std::string_view storeName) const {
        const auto* targetStore = getStoreByName(storeName);
        return targetStore ? targetStore->size() : 0;
    }

    bool verifyCertificateChain(const X509Certificate& cert) {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)cert;
        // This certificate model has no remote DER parser or issuer-signature
        // verifier. A matching subject/fingerprint is not proof of a valid
        // chain, so it must never authorize TLS or code-signing decisions.
        return false;
    }

    // Validate an ordered TLS server chain against explicitly installed ROOT
    // certificates. `nowUnixSeconds` must come from the kernel's authoritative
    // UTC clock; no local wall-clock fallback is permitted here.
    bool verifyTlsServerChain(std::span<const X509Certificate> presented,
                              std::string_view hostname,
                              int64_t nowUnixSeconds) {
        std::vector<X509Certificate> roots;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            roots = rootStore_;
        }
        return micant::shared::crypto::verifyTlsServerCertificateChain(
            presented, roots, hostname, nowUnixSeconds);
    }

    bool verifyTlsServerChainAtSystemTime(std::span<const X509Certificate> presented,
                                          std::string_view hostname) {
        uint64_t systemTime100ns = 0;
        if (!ke::KeQuerySystemTime100ns(systemTime100ns) ||
            systemTime100ns < 116444736000000000ULL) return false;
        const uint64_t unixSeconds = (systemTime100ns - 116444736000000000ULL) / 10000000ULL;
        if (unixSeconds > static_cast<uint64_t>(INT64_MAX)) return false;
        return verifyTlsServerChain(presented, hostname, static_cast<int64_t>(unixSeconds));
    }

    void resetForTest() {
        std::lock_guard<std::mutex> lock(mutex_);
        myStore_.clear();
        rootStore_.clear();
        caStore_.clear();
        initialized_ = false;
        initializeDefaultStoresLocked();
    }

private:
    SovereignCertStoreRegistry() { initializeDefaultStores(); }

    void initializeDefaultStoresLocked() {
        if (initialized_) return;
        // No built-in self-signed placeholder is a system trust anchor.
        initialized_ = true;
    }

    const std::vector<X509Certificate>* getStoreByName(std::string_view name) const {
        if (name == "MY") return &myStore_;
        if (name == "ROOT") return &rootStore_;
        if (name == "CA") return &caStore_;
        return nullptr;
    }

    std::mutex mutex_;
    bool initialized_{false};
    std::vector<X509Certificate> myStore_;
    std::vector<X509Certificate> rootStore_;
    std::vector<X509Certificate> caStore_;
};

// ============================================================================
// 5. SCM Driver & Module Registration for crypt32.dll
// ============================================================================

inline void InitializeCrypt32Subsystem() {
    SovereignCertStoreRegistry::get().initializeDefaultStores();

    // 1. Version Database Registration
    auto& verDb = version::VersionDatabase::Instance();
    verDb.RegisterModule("crypt32.dll", "10.0.26100.5900", "MicaNT Crypto API32 Subsystem");

    // 2. Register Exports in JanusLDR
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("crypt32.dll", "CertOpenSystemStoreW", reinterpret_cast<void*>(+[]() { return reinterpret_cast<void*>(0x2001); }));
}

} // namespace micant::km::crypto
