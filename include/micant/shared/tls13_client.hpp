#pragma once

// Mode-neutral clean-room TLS 1.3 client profile (RFC 8446).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#if !defined(MICANT_FREESTANDING)
#include <vector>
#endif
#include "micant/shared/bounded_containers.hpp"
#include <string_view>
#include <utility>
#include "micant/shared/crypto_primitives.hpp"
#include "micant/shared/x509_profile.hpp"

namespace micant::shared::tls13 {
using namespace ::micant::shared::crypto;

// ============================================================================
// 1. RFC 8446 Protocol Constants
// ============================================================================

inline constexpr uint8_t CONTENT_TYPE_CHANGE_CIPHER_SPEC = 20; // 0x14
inline constexpr uint8_t CONTENT_TYPE_ALERT              = 21; // 0x15
inline constexpr uint8_t CONTENT_TYPE_HANDSHAKE          = 22; // 0x16
inline constexpr uint8_t CONTENT_TYPE_APPLICATION_DATA   = 23; // 0x17

inline constexpr uint8_t HANDSHAKE_CLIENT_HELLO          = 1;
inline constexpr uint8_t HANDSHAKE_SERVER_HELLO          = 2;
inline constexpr uint8_t HANDSHAKE_ENCRYPTED_EXTENSIONS  = 8;
inline constexpr uint8_t HANDSHAKE_CERTIFICATE           = 11;
inline constexpr uint8_t HANDSHAKE_CERTIFICATE_VERIFY    = 15;
inline constexpr uint8_t HANDSHAKE_FINISHED              = 20;

inline constexpr uint16_t CIPHER_SUITE_AES_128_GCM_SHA256 = 0x1301;
inline constexpr uint16_t CIPHER_SUITE_AES_256_GCM_SHA384 = 0x1302;
inline constexpr uint16_t CIPHER_SUITE_CHACHA20_POLY1305  = 0x1303;

inline constexpr uint16_t EXT_SERVER_NAME               = 0x0000;
inline constexpr uint16_t EXT_SUPPORTED_GROUPS          = 0x000A;
inline constexpr uint16_t EXT_SIGNATURE_ALGORITHMS      = 0x000D;
inline constexpr uint16_t EXT_ALPN                      = 0x0010;
inline constexpr uint16_t EXT_SUPPORTED_VERSIONS        = 0x002B;
inline constexpr uint16_t EXT_KEY_SHARE                 = 0x0033;

inline constexpr uint16_t GROUP_X25519                  = 0x001D;
inline constexpr uint16_t GROUP_SECP256R1               = 0x0017;
inline constexpr uint16_t TLS_VERSION_1_3               = 0x0304;

inline bool tlsConstantTimeEqual(std::span<const uint8_t> left,
                                 std::span<const uint8_t> right) noexcept {
    if (left.size() != right.size()) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

inline bool tlsIsIpv4Literal(std::string_view name) noexcept {
    if (name.empty()) return false;
    unsigned octets = 0, value = 0, digits = 0;
    for (char c : name) {
        if (c == '.') {
            if (!digits || value > 255 || ++octets > 3) return false;
            value = 0; digits = 0; continue;
        }
        if (c < '0' || c > '9' || ++digits > 3) return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    return octets == 3 && digits && value <= 255;
}

inline bool tlsLooksLikeIpv4(std::string_view name) noexcept {
    bool dot = false;
    if (name.empty()) return false;
    for (char c : name) {
        if (c == '.') { dot = true; continue; }
        if (c < '0' || c > '9') return false;
    }
    return dot;
}

inline bool tlsIsValidDnsName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 253) return false;
    size_t labelLength = 0;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '.') {
            if (labelLength == 0 || labelLength > 63 || name[i - 1] == '-') return false;
            labelLength = 0;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
        if (labelLength == 0 && c == '-') return false;
        ++labelLength;
    }
    return labelLength > 0 && labelLength <= 63 && name.back() != '-';
}

// Bounded wire readers shared by the handshake parser. TLS records are capped
// at the TLS 1.3 ciphertext limit; lengths are checked before any subspan.
class Tls13WireReader {
public:
    explicit Tls13WireReader(std::span<const uint8_t> bytes) noexcept : bytes_(bytes) {}
    bool u8(uint8_t& value) noexcept {
        if (position_ == bytes_.size()) return false;
        value = bytes_[position_++]; return true;
    }
    bool u16(uint16_t& value) noexcept {
        if (bytes_.size() - position_ < 2) return false;
        value = (static_cast<uint16_t>(bytes_[position_]) << 8) | bytes_[position_ + 1];
        position_ += 2; return true;
    }
    bool u24(uint32_t& value) noexcept {
        if (bytes_.size() - position_ < 3) return false;
        value = (static_cast<uint32_t>(bytes_[position_]) << 16) |
            (static_cast<uint32_t>(bytes_[position_ + 1]) << 8) | bytes_[position_ + 2];
        position_ += 3; return true;
    }
    bool take(size_t length, std::span<const uint8_t>& value) noexcept {
        if (length > bytes_.size() - position_) return false;
        value = bytes_.subspan(position_, length); position_ += length; return true;
    }
    [[nodiscard]] size_t remaining() const noexcept { return bytes_.size() - position_; }
private:
    std::span<const uint8_t> bytes_;
    size_t position_{0};
};

struct Tls13RecordView { uint8_t contentType{}; std::span<const uint8_t> payload{}; };
inline bool parseTls13Record(std::span<const uint8_t> wire, Tls13RecordView& record) noexcept {
    if (wire.size() < 5) return false;
    const uint8_t type = wire[0];
    const uint16_t version = (static_cast<uint16_t>(wire[1]) << 8) | wire[2];
    const size_t length = (static_cast<size_t>(wire[3]) << 8) | wire[4];
    if ((type != CONTENT_TYPE_CHANGE_CIPHER_SPEC && type != CONTENT_TYPE_ALERT &&
         type != CONTENT_TYPE_HANDSHAKE && type != CONTENT_TYPE_APPLICATION_DATA) ||
        version != 0x0303 || length > 16384 + 256 || wire.size() != length + 5) return false;
    record.contentType = type;
    record.payload = wire.subspan(5, length);
    return true;
}

struct Tls13HandshakeView { uint8_t type{}; std::span<const uint8_t> body{}; };
inline bool parseTls13HandshakeMessages(std::span<const uint8_t> bytes,
                                        micant::shared::BoundedVector<Tls13HandshakeView>& messages,
                                        size_t maximumMessages = 32) {
    if (bytes.size() > 1024 * 1024 || maximumMessages == 0 || maximumMessages > 32) return false;
    micant::shared::BoundedVector<Tls13HandshakeView> parsed;
    Tls13WireReader reader(bytes);
    while (reader.remaining()) {
        uint8_t type = 0; uint32_t length = 0; std::span<const uint8_t> body{};
        if (parsed.size() == maximumMessages || !reader.u8(type) || !reader.u24(length) ||
            length > 1024 * 1024 || !reader.take(length, body)) return false;
        parsed.push_back({type, body});
        if (!parsed.good()) return false;
    }
    messages = std::move(parsed);
    return true;
}

struct Tls13ServerHelloView {
    std::array<uint8_t, 32> random{};
    std::array<uint8_t, 32> x25519KeyShare{};
    uint16_t cipherSuite{};
};

inline bool parseTls13ServerHello(std::span<const uint8_t> body,
                                  Tls13ServerHelloView& serverHello) noexcept {
    Tls13WireReader reader(body);
    uint16_t legacyVersion = 0, cipherSuite = 0;
    uint8_t sessionIdLength = 0, compression = 0;
    std::span<const uint8_t> random{}, sessionId{};
    uint16_t extensionsLength = 0;
    std::span<const uint8_t> extensions{};
    if (!reader.u16(legacyVersion) || legacyVersion != 0x0303 || !reader.take(32, random) ||
        !reader.u8(sessionIdLength) || sessionIdLength != 0 || !reader.take(sessionIdLength, sessionId) ||
        !reader.u16(cipherSuite) || cipherSuite != CIPHER_SUITE_AES_128_GCM_SHA256 ||
        !reader.u8(compression) || compression != 0 || !reader.u16(extensionsLength) ||
        !reader.take(extensionsLength, extensions) || reader.remaining()) return false;
    // HelloRetryRequest is deliberately unsupported in this bounded profile.
    static constexpr uint8_t helloRetryRequestRandom[32] = {
        0xcf,0x21,0xad,0x74,0xe5,0x9a,0x61,0x11,0xbe,0x1d,0x8c,0x02,0x1e,0x65,0xb8,0x91,
        0xc2,0xa2,0x11,0x16,0x7a,0xbb,0x8c,0x5e,0x07,0x9e,0x09,0xe2,0xc8,0xa8,0x33,0x9c
    };
    if (micant::shared::boundedMemoryCompare(random.data(), helloRetryRequestRandom, random.size()) == 0) return false;
    bool versionSeen = false, keyShareSeen = false;
    Tls13WireReader extensionReader(extensions);
    std::array<uint16_t, 16> seen{};
    size_t seenCount = 0;
    Tls13ServerHelloView parsed{};
    parsed.cipherSuite = cipherSuite;
    micant::shared::boundedMemoryCopy(parsed.random.data(), random.data(), random.size());
    while (extensionReader.remaining()) {
        uint16_t type = 0, length = 0;
        std::span<const uint8_t> value{};
        if (!extensionReader.u16(type) || !extensionReader.u16(length) ||
            !extensionReader.take(length, value) || seenCount == seen.size()) return false;
        for (size_t i = 0; i < seenCount; ++i) if (seen[i] == type) return false;
        seen[seenCount++] = type;
        Tls13WireReader valueReader(value);
        if (type == EXT_SUPPORTED_VERSIONS) {
            uint16_t selected = 0;
            if (versionSeen || !valueReader.u16(selected) || valueReader.remaining() || selected != TLS_VERSION_1_3)
                return false;
            versionSeen = true;
        } else if (type == EXT_KEY_SHARE) {
            uint16_t group = 0, keyLength = 0;
            std::span<const uint8_t> key{};
            if (keyShareSeen || !valueReader.u16(group) || group != GROUP_X25519 ||
                !valueReader.u16(keyLength) || keyLength != 32 || !valueReader.take(keyLength, key) ||
                valueReader.remaining()) return false;
            micant::shared::boundedMemoryCopy(parsed.x25519KeyShare.data(), key.data(), key.size());
            keyShareSeen = true;
        } else {
            // This client offered no PSK or extension that permits an unsolicited
            // ServerHello response. Unknown/HRR-only extensions fail closed.
            return false;
        }
    }
    if (!versionSeen || !keyShareSeen) return false;
    serverHello = parsed;
    return true;
}

inline bool parseTls13CertificateMessage(std::span<const uint8_t> body,
                                         micant::shared::BoundedVector<X509Certificate>& certificates) {
    Tls13WireReader reader(body);
    uint8_t requestContextLength = 0;
    std::span<const uint8_t> requestContext{};
    uint32_t listLength = 0;
    if (!reader.u8(requestContextLength) || !reader.take(requestContextLength, requestContext) ||
        requestContextLength != 0 || !reader.u24(listLength) || listLength != reader.remaining() || listLength == 0)
        return false;
    std::span<const uint8_t> list{};
    if (!reader.take(listLength, list)) return false;
    Tls13WireReader entries(list);
    micant::shared::BoundedVector<X509Certificate> parsed;
    while (entries.remaining()) {
        if (parsed.size() == 8) return false;
        uint32_t derLength = 0;
        std::span<const uint8_t> der{};
        uint16_t extensionsLength = 0;
        std::span<const uint8_t> extensions{};
        if (!entries.u24(derLength) || derLength == 0 || derLength > 1024 * 1024 ||
            !entries.take(derLength, der) || !entries.u16(extensionsLength) ||
            !entries.take(extensionsLength, extensions) || !extensions.empty()) return false;
        X509Certificate certificate{};
        if (!X509Certificate::parseDer(der, certificate)) return false;
        parsed.push_back(std::move(certificate));
        if (!parsed.good()) return false;
    }
    if (parsed.empty() || !parsed.good()) return false;
    certificates = std::move(parsed);
    return true;
}

inline bool verifyTls13ServerCertificateVerify(const X509Certificate& certificate,
                                                uint16_t signatureScheme,
                                                std::span<const uint8_t> signature,
                                                std::span<const uint8_t, 32> transcriptHash) {
    // TLS 1.3 CertificateVerify uses 64 spaces, the role-specific context,
    // one zero separator, and the transcript hash (RFC 8446 section 4.4.3).
    static constexpr std::string_view context = "TLS 1.3, server CertificateVerify";
    if (certificate.publicKey.empty()) return false;
    std::array<uint8_t, 64 + context.size() + 1 + 32> input{};
    std::fill_n(input.begin(), 64, static_cast<uint8_t>(0x20));
    micant::shared::boundedMemoryCopy(input.data() + 64, context.data(), context.size());
    input[64 + context.size()] = 0;
    micant::shared::boundedMemoryCopy(input.data() + 64 + context.size() + 1,
                transcriptHash.data(), transcriptHash.size());
    static constexpr uint8_t rsaOid[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
    static constexpr uint8_t ecOid[] = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
    static constexpr uint8_t p256Oid[] = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07};
    if (signatureScheme == 0x0804 && asn1::oidEquals(certificate.publicKeyAlgorithmOidDer, rsaOid))
        return RsaSha256Verifier::verifyPss(certificate.publicKey, input, signature);
    if (signatureScheme == 0x0403 && asn1::oidEquals(certificate.publicKeyAlgorithmOidDer, ecOid) &&
        asn1::oidEquals(certificate.publicKeyCurveOidDer, p256Oid))
        return EcdsaP256Sha256Verifier::verify(certificate.publicKey, input, signature);
    return false;
}

// ============================================================================
// 2. TLS 1.3 Key Schedule Derivation Engine
// ============================================================================

struct TrafficKeys {
    std::array<uint8_t, 16> key{};
    std::array<uint8_t, 12> iv{};
};

class KeySchedule {
public:
    static bool deriveSecret(
        std::span<const uint8_t> secret,
        std::string_view label,
        std::span<const uint8_t> transcriptHash,
        std::array<uint8_t, 32>& out)
    {
        auto derived = HkdfSha256::expandLabel(secret, label, transcriptHash, 32);
        if (!derived.good() || derived.size() != out.size()) return false;
        micant::shared::boundedMemoryCopy(out.data(), derived.data(), 32);
        return true;
    }

    static bool deriveTrafficKeys(std::span<const uint8_t> trafficSecret, TrafficKeys& tk) {
        auto keyBytes = HkdfSha256::expandLabel(trafficSecret, "key", {}, 16);
        auto ivBytes  = HkdfSha256::expandLabel(trafficSecret, "iv", {}, 12);
        if (!keyBytes.good() || keyBytes.size() != tk.key.size() ||
            !ivBytes.good() || ivBytes.size() != tk.iv.size()) return false;
        micant::shared::boundedMemoryCopy(tk.key.data(), keyBytes.data(), 16);
        micant::shared::boundedMemoryCopy(tk.iv.data(), ivBytes.data(), 12);
        return true;
    }

    static bool computeFinishedVerifyData(
        std::span<const uint8_t> baseKey,
        std::span<const uint8_t> transcriptHash,
        std::array<uint8_t, 32>& output)
    {
        auto finishedKey = HkdfSha256::expandLabel(baseKey, "finished", {}, 32);
        if (!finishedKey.good() || finishedKey.size() != 32) return false;
        output = HmacSha256::compute(finishedKey, transcriptHash);
        return true;
    }
};

// ============================================================================
// 3. TLS 1.3 Encrypted Record Framing Layer
// ============================================================================

class RecordLayer {
public:
    static std::array<uint8_t, 12> constructNonce(const std::array<uint8_t, 12>& iv, uint64_t seqNum) noexcept {
        std::array<uint8_t, 12> nonce = iv;
        for (int i = 0; i < 8; ++i) {
            nonce[11 - i] ^= static_cast<uint8_t>((seqNum >> (i * 8)) & 0xFF);
        }
        return nonce;
    }

    static micant::shared::BoundedVector<uint8_t> wrapRecord(
        const TrafficKeys& keys,
        uint64_t seqNum,
        uint8_t innerContentType,
        std::span<const uint8_t> plaintextPayload)
    {
        if (plaintextPayload.size() > 16384 ||
            (innerContentType != CONTENT_TYPE_ALERT && innerContentType != CONTENT_TYPE_HANDSHAKE &&
             innerContentType != CONTENT_TYPE_APPLICATION_DATA)) return {};
        // Inner Plaintext = Payload || ContentType
        micant::shared::BoundedVector<uint8_t> inner(plaintextPayload.begin(), plaintextPayload.end());
        inner.push_back(innerContentType);
        if (!inner.good()) return {};

        auto nonce = constructNonce(keys.iv, seqNum);

        // Record header (5 bytes): Type(0x17), LegacyVersion(0x0303), Length(inner.size() + 16)
        uint16_t ciphertextLen = static_cast<uint16_t>(inner.size() + 16);
        std::array<uint8_t, 5> header = {
            CONTENT_TYPE_APPLICATION_DATA,
            0x03, 0x03,
            static_cast<uint8_t>((ciphertextLen >> 8) & 0xFF),
            static_cast<uint8_t>(ciphertextLen & 0xFF)
        };

        // AAD for TLS 1.3 is the 5-byte record header
        micant::shared::BoundedVector<uint8_t> ciphertext;
        std::array<uint8_t, 16> tag{};
        if (!AesGcm::encrypt(keys.key, nonce, header, inner, ciphertext, tag)) return {};
        if (!ciphertext.good()) return {};

        // Emit Wire Record: Header || Ciphertext || Tag
        micant::shared::BoundedVector<uint8_t> record;
        record.insert(record.end(), header.begin(), header.end());
        record.insert(record.end(), ciphertext.begin(), ciphertext.end());
        record.insert(record.end(), tag.begin(), tag.end());
        if (!record.good()) return {};
        return record;
    }

    static bool unwrapRecord(
        const TrafficKeys& keys,
        uint64_t seqNum,
        std::span<const uint8_t> wireRecord,
        uint8_t& outContentType,
        micant::shared::BoundedVector<uint8_t>& outPlaintext)
    {
        if (wireRecord.size() < 5 + 16 + 1 || wireRecord.size() > 5 + 16384 + 256) return false;

        std::array<uint8_t, 5> header;
        micant::shared::boundedMemoryCopy(header.data(), wireRecord.data(), 5);

        uint16_t len = (static_cast<uint16_t>(header[3]) << 8) | header[4];
        if (wireRecord.size() != 5 + len || len < 17 ||
            header[0] != CONTENT_TYPE_APPLICATION_DATA || header[1] != 0x03 || header[2] != 0x03)
            return false;

        std::span<const uint8_t> ct(wireRecord.data() + 5, len - 16);
        std::span<const uint8_t> tag(wireRecord.data() + 5 + len - 16, 16);

        auto nonce = constructNonce(keys.iv, seqNum);
        micant::shared::BoundedVector<uint8_t> inner;
        if (!AesGcm::decrypt(keys.key, nonce, header, ct, tag, inner)) {
            return false;
        }

        // Inner plaintext ends with content type preceded by zero or more padding bytes
        while (!inner.empty() && inner.back() == 0) {
            inner.pop_back();
        }
        if (inner.empty()) return false;

        outContentType = inner.back();
        if (outContentType != CONTENT_TYPE_ALERT && outContentType != CONTENT_TYPE_HANDSHAKE &&
            outContentType != CONTENT_TYPE_APPLICATION_DATA) return false;
        inner.pop_back();
        if (inner.size() > 16384) return false;
        outPlaintext = std::move(inner);
        return true;
    }
};

// ============================================================================
// 4. Sovereign TLS 1.3 Handshake Session State Machine
// ============================================================================

enum class Tls13State {
    Initial,
    WaitClientHello,
    WaitFinished,
    Established,
    Closed,
    Failed
};

class Tls13ServerSession {
public:
    Tls13ServerSession() {
        // Generate Server Ephemeral X25519 Key Pair
        entropyAvailable_ = CryptoRng::generate(serverPrivateKey_);
        if (!entropyAvailable_) { state_ = Tls13State::Failed; return; }
        X25519::generatePublicKey(serverPublicKey_.data(), serverPrivateKey_.data());
        entropyAvailable_ = CryptoRng::generate(serverRandom_);
        if (!entropyAvailable_) state_ = Tls13State::Failed;
    }

    // Process Inbound ClientHello wire buffer
    bool processClientHello(std::span<const uint8_t> chData, micant::shared::BoundedVector<uint8_t>& serverHandshakeOut) {
        // No server certificate/signing key is attached and the previous demo
        // path skipped client Finished verification. Never emit an unauthenticated
        // handshake as if it had established TLS.
        (void)chData;
        serverHandshakeOut.clear();
        state_ = Tls13State::Failed;
        return false;
    }
#if !defined(MICANT_FREESTANDING)
    bool processClientHello(std::span<const uint8_t> chData, std::vector<uint8_t>& serverHandshakeOut) {
        micant::shared::BoundedVector<uint8_t> bounded;
        const bool result = processClientHello(chData, bounded);
        serverHandshakeOut.assign(bounded.begin(), bounded.end());
        return result;
    }
#endif

    micant::shared::BoundedVector<uint8_t> encryptAppData(std::span<const uint8_t> plaintext) {
        if (state_ != Tls13State::Established) return {};
        return RecordLayer::wrapRecord(serverAppKeys_, serverSeq_++, CONTENT_TYPE_APPLICATION_DATA, plaintext);
    }

    // Decrypt Application Data (HTTP requests, console input)
    bool decryptAppData(std::span<const uint8_t> wireRecord, micant::shared::BoundedVector<uint8_t>& plaintext) {
        if (state_ != Tls13State::Established) return false;
        uint8_t contentType = 0;
        if (!RecordLayer::unwrapRecord(clientAppKeys_, clientSeq_++, wireRecord, contentType, plaintext)) {
            return false;
        }
        return contentType == CONTENT_TYPE_APPLICATION_DATA;
    }
#if !defined(MICANT_FREESTANDING)
    bool decryptAppData(std::span<const uint8_t> wireRecord, std::vector<uint8_t>& plaintext) {
        micant::shared::BoundedVector<uint8_t> bounded;
        const bool result = decryptAppData(wireRecord, bounded);
        plaintext.assign(bounded.begin(), bounded.end());
        return result;
    }
#endif

    [[nodiscard]] bool isEstablished() const noexcept { return state_ == Tls13State::Established; }
    [[nodiscard]] uint16_t getNegotiatedCipherSuite() const noexcept { return CIPHER_SUITE_AES_128_GCM_SHA256; }
    [[nodiscard]] const std::array<uint8_t, 32>& getSharedSecret() const noexcept { return sharedSecret_; }
    [[nodiscard]] const std::array<uint8_t, 32>& getServerPublicKey() const noexcept { return serverPublicKey_; }
    [[nodiscard]] const std::array<uint8_t, 32>& getClientPublicKey() const noexcept { return clientPublicKey_; }
    [[nodiscard]] const TrafficKeys& getClientAppKeys() const noexcept { return clientAppKeys_; }
    [[nodiscard]] const TrafficKeys& getServerAppKeys() const noexcept { return serverAppKeys_; }
    [[nodiscard]] const std::array<uint8_t, 32>& getTranscriptHash() const noexcept { return transcriptHash_; }

private:
    bool entropyAvailable_{false};
    Tls13State state_{Tls13State::Initial};
    std::array<uint8_t, 32> clientRandom_{};
    std::array<uint8_t, 32> serverRandom_{};
    std::array<uint8_t, 32> clientPublicKey_{};
    std::array<uint8_t, 32> serverPrivateKey_{};
    std::array<uint8_t, 32> serverPublicKey_{};
    std::array<uint8_t, 32> sharedSecret_{};
    std::array<uint8_t, 32> transcriptHash_{};

    std::array<uint8_t, 32> handshakeSecret_{};
    std::array<uint8_t, 32> clientHsSecret_{};
    std::array<uint8_t, 32> serverHsSecret_{};
    TrafficKeys clientHsKeys_{};
    TrafficKeys serverHsKeys_{};

    std::array<uint8_t, 32> masterSecret_{};
    std::array<uint8_t, 32> clientAppSecret_{};
    std::array<uint8_t, 32> serverAppSecret_{};
    TrafficKeys clientAppKeys_{};
    TrafficKeys serverAppKeys_{};

    uint64_t clientSeq_{0};
    uint64_t serverSeq_{0};
};

// ============================================================================
// 5. Sovereign TLS 1.3 Client Session
// ============================================================================

class Tls13ClientSession {
public:
    Tls13ClientSession() {
        entropyAvailable_ = CryptoRng::generate(clientPrivateKey_);
        if (!entropyAvailable_) { failed_ = true; return; }
        X25519::generatePublicKey(clientPublicKey_.data(), clientPrivateKey_.data());
        entropyAvailable_ = CryptoRng::generate(clientRandom_);
        if (!entropyAvailable_) failed_ = true;
    }
    ~Tls13ClientSession() { clearSecrets(); }
    Tls13ClientSession(const Tls13ClientSession&) = delete;
    Tls13ClientSession& operator=(const Tls13ClientSession&) = delete;

    // Construct ClientHello wire packet
    micant::shared::BoundedVector<uint8_t> generateClientHello(std::string_view serverName = {}) {
        const bool ipLiteral = tlsIsIpv4Literal(serverName);
        if (failed_ || clientHelloCreated_ || !entropyAvailable_ || serverName.empty() ||
            (tlsLooksLikeIpv4(serverName) ? !ipLiteral : !tlsIsValidDnsName(serverName))) return {};
        micant::shared::BoundedVector<uint8_t> chBody;
        chBody.push_back(0x03); chBody.push_back(0x03); // Legacy version
        chBody.insert(chBody.end(), clientRandom_.begin(), clientRandom_.end());
        chBody.push_back(0); // Session ID len = 0

        // Cipher suites: TLS_AES_128_GCM_SHA256 (0x1301)
        chBody.push_back(0x00); chBody.push_back(0x02);
        chBody.push_back(0x13); chBody.push_back(0x01);

        // Compression: 1 byte (0x00 = null)
        chBody.push_back(0x01); chBody.push_back(0x00);

        // Extensions
        micant::shared::BoundedVector<uint8_t> exts;

        // SNI host_name is DNS-only; RFC 6066 forbids putting IP literals here.
        if (!ipLiteral) {
            micant::shared::BoundedVector<uint8_t> sniList;
            sniList.push_back(0x00);
            sniList.push_back(static_cast<uint8_t>((serverName.size() >> 8) & 0xFF));
            sniList.push_back(static_cast<uint8_t>(serverName.size() & 0xFF));
            sniList.insert(sniList.end(), serverName.begin(), serverName.end());
            if (!sniList.good()) return failClientHello();
            exts.push_back(0x00); exts.push_back(0x00);
            exts.push_back(static_cast<uint8_t>(((sniList.size() + 2) >> 8) & 0xFF));
            exts.push_back(static_cast<uint8_t>((sniList.size() + 2) & 0xFF));
            exts.push_back(static_cast<uint8_t>((sniList.size() >> 8) & 0xFF));
            exts.push_back(static_cast<uint8_t>(sniList.size() & 0xFF));
            exts.insert(exts.end(), sniList.begin(), sniList.end());
        }

        // 2. supported_versions (0x002B) = TLS 1.3 (0x0304)
        exts.push_back(0x00); exts.push_back(0x2B);
        exts.push_back(0x00); exts.push_back(0x03);
        exts.push_back(0x02); exts.push_back(0x03); exts.push_back(0x04);

        // 3. supported_groups = X25519. CertificateVerify supports RSA-PSS
        // and ECDSA P-256; RSA-PKCS1 is offered for certificate signatures.
        exts.push_back(0x00); exts.push_back(0x0A);
        exts.push_back(0x00); exts.push_back(0x04);
        exts.push_back(0x00); exts.push_back(0x02);
        exts.push_back(0x00); exts.push_back(0x1D);
        exts.push_back(0x00); exts.push_back(0x0D);
        exts.push_back(0x00); exts.push_back(0x08);
        exts.push_back(0x00); exts.push_back(0x06);
        exts.push_back(0x08); exts.push_back(0x04); // rsa_pss_rsae_sha256
        exts.push_back(0x04); exts.push_back(0x03); // ecdsa_secp256r1_sha256
        exts.push_back(0x04); exts.push_back(0x01); // rsa_pkcs1_sha256 (certificate signatures only)

        // 4. key_share (0x0033) containing X25519 client public key
        exts.push_back(0x00); exts.push_back(0x33);
        exts.push_back(0x00); exts.push_back(38);
        exts.push_back(0x00); exts.push_back(36);
        exts.push_back(0x00); exts.push_back(0x1D); // X25519
        exts.push_back(0x00); exts.push_back(32);
        exts.insert(exts.end(), clientPublicKey_.begin(), clientPublicKey_.end());

        chBody.push_back(static_cast<uint8_t>((exts.size() >> 8) & 0xFF));
        chBody.push_back(static_cast<uint8_t>(exts.size() & 0xFF));
        chBody.insert(chBody.end(), exts.begin(), exts.end());

        // Construct full Handshake record
        micant::shared::BoundedVector<uint8_t> chMsg;
        chMsg.push_back(HANDSHAKE_CLIENT_HELLO);
        chMsg.push_back(0);
        chMsg.push_back(static_cast<uint8_t>((chBody.size() >> 8) & 0xFF));
        chMsg.push_back(static_cast<uint8_t>(chBody.size() & 0xFF));
        chMsg.insert(chMsg.end(), chBody.begin(), chBody.end());

        micant::shared::BoundedVector<uint8_t> wireRecord;
        wireRecord.push_back(CONTENT_TYPE_HANDSHAKE);
        wireRecord.push_back(0x03); wireRecord.push_back(0x03);
        wireRecord.push_back(static_cast<uint8_t>((chMsg.size() >> 8) & 0xFF));
        wireRecord.push_back(static_cast<uint8_t>(chMsg.size() & 0xFF));
        wireRecord.insert(wireRecord.end(), chMsg.begin(), chMsg.end());

        lastClientHello_ = wireRecord;
        transcript_.assign(chMsg.begin(), chMsg.end());
        serverName_.assign(serverName);
        if (!chBody.good() || !exts.good() || !chMsg.good() || !wireRecord.good() ||
            !lastClientHello_.good() || !transcript_.good() ||
            !micant::shared::isContainerGood(serverName_))
            return failClientHello();
        clientHelloCreated_ = true;
        return wireRecord;
    }

    // Process Inbound ServerHandshake (ServerHello + EncryptedExtensions + Finished)
    bool processServerHandshake(std::span<const uint8_t> wireBytes,
                                std::span<const X509Certificate> trustRoots,
                                int64_t nowUnixSeconds) {
        if (failed_ || established_ || !clientHelloCreated_) return false;
        if (wireBytes.empty() || wireBytes.size() > 2 * 1024 * 1024 || nowUnixSeconds < 0)
            return failHandshake();
        size_t recordOffset = 0;
        while (recordOffset < wireBytes.size()) {
            if (wireBytes.size() - recordOffset < 5) return failHandshake();
            const size_t recordLength = (static_cast<size_t>(wireBytes[recordOffset + 3]) << 8) |
                wireBytes[recordOffset + 4];
            if (recordLength > 16384 + 256 || recordLength > wireBytes.size() - recordOffset - 5)
                return failHandshake();
            const auto wireRecord = wireBytes.subspan(recordOffset, recordLength + 5);
            Tls13RecordView record{};
            if (!parseTls13Record(wireRecord, record)) return failHandshake();
            recordOffset += recordLength + 5;

            // TLS 1.3 compatibility-mode CCS records can arrive at any
            // point after ClientHello and before the peer's Finished. They
            // are not handshake transcript bytes and must not change keys or
            // the encrypted-record sequence number.
            if (record.contentType == CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
                if (serverFinishedVerified_ || record.payload.size() != 1 || record.payload[0] != 1)
                    return failHandshake();
                continue;
            }

            if (!serverHelloSeen_) {
                if (record.contentType != CONTENT_TYPE_HANDSHAKE) return failHandshake();
                // A handshake message may span multiple TLSPlaintext records.
                // Keep its original wire bytes for the transcript until the
                // complete ServerHello has arrived.
                if (record.payload.size() > 1024 * 1024 - pendingHandshake_.size())
                    return failHandshake();
                pendingHandshake_.insert(pendingHandshake_.end(), record.payload.begin(), record.payload.end());
                if (!pendingHandshake_.good()) return failHandshake();
                if (pendingHandshake_.size() < 4) continue;
                const size_t serverHelloLength = 4 + (static_cast<size_t>(pendingHandshake_[1]) << 16) +
                    (static_cast<size_t>(pendingHandshake_[2]) << 8) + pendingHandshake_[3];
                if (serverHelloLength > 1024 * 1024) return failHandshake();
                if (pendingHandshake_.size() < serverHelloLength) continue;
                if (pendingHandshake_.size() != serverHelloLength) return failHandshake();
                micant::shared::BoundedVector<Tls13HandshakeView> messages;
                if (!parseTls13HandshakeMessages(pendingHandshake_, messages, 1) || messages.size() != 1 ||
                    messages[0].type != HANDSHAKE_SERVER_HELLO) return failHandshake();
                Tls13ServerHelloView serverHello{};
                if (!parseTls13ServerHello(messages[0].body, serverHello)) return failHandshake();
                serverRandom_ = serverHello.random;
                serverPublicKey_ = serverHello.x25519KeyShare;
                X25519::scalarMult(sharedSecret_.data(), clientPrivateKey_.data(), serverPublicKey_.data());
                uint8_t nonzero = 0;
                for (uint8_t byte : sharedSecret_) nonzero |= byte;
                if (!nonzero) return failHandshake();

                transcript_.insert(transcript_.end(), pendingHandshake_.begin(), pendingHandshake_.end());
                if (!transcript_.good()) return failHandshake();
                transcriptHash_ = Sha256::hash(transcript_);
                const auto emptyHash = Sha256::hash(std::span<const uint8_t>{});
                // With no PSK TLS 1.3 uses Hash.length zero octets as the
                // input key material, not an empty input to HKDF-Extract.
                const std::array<uint8_t, 32> noPsk{};
                const auto earlySecret = HkdfSha256::extract({}, noPsk);
                std::array<uint8_t, 32> derivedSecret{};
                if (!KeySchedule::deriveSecret(earlySecret, "derived", emptyHash, derivedSecret))
                    return failHandshake();
                handshakeSecret_ = HkdfSha256::extract(derivedSecret, sharedSecret_);
                if (!KeySchedule::deriveSecret(handshakeSecret_, "c hs traffic", transcriptHash_, clientHsSecret_) ||
                    !KeySchedule::deriveSecret(handshakeSecret_, "s hs traffic", transcriptHash_, serverHsSecret_) ||
                    !KeySchedule::deriveTrafficKeys(clientHsSecret_, clientHsKeys_) ||
                    !KeySchedule::deriveTrafficKeys(serverHsSecret_, serverHsKeys_)) return failHandshake();
                serverHelloSeen_ = true;
                pendingHandshake_.clear();
                continue;
            }

            if (record.contentType != CONTENT_TYPE_APPLICATION_DATA || serverFinishedVerified_)
                return failHandshake();
            uint8_t innerType = 0;
            micant::shared::BoundedVector<uint8_t> plaintext;
            if (serverHandshakeSeq_ == UINT64_MAX ||
                !RecordLayer::unwrapRecord(serverHsKeys_, serverHandshakeSeq_++, wireRecord, innerType, plaintext) ||
                innerType != CONTENT_TYPE_HANDSHAKE ||
                plaintext.size() > 1024 * 1024 - pendingHandshake_.size()) return failHandshake();
            pendingHandshake_.insert(pendingHandshake_.end(), plaintext.begin(), plaintext.end());
            if (!pendingHandshake_.good()) return failHandshake();

            size_t messageOffset = 0;
            while (pendingHandshake_.size() - messageOffset >= 4) {
                const uint32_t bodyLength = (static_cast<uint32_t>(pendingHandshake_[messageOffset + 1]) << 16) |
                    (static_cast<uint32_t>(pendingHandshake_[messageOffset + 2]) << 8) |
                    pendingHandshake_[messageOffset + 3];
                if (bodyLength > 1024 * 1024) return failHandshake();
                const size_t messageLength = static_cast<size_t>(bodyLength) + 4;
                if (messageLength > pendingHandshake_.size() - messageOffset) break;
                const auto message = std::span<const uint8_t>(pendingHandshake_.data() + messageOffset,
                                                               messageLength);
                const uint8_t type = message[0];
                const auto body = message.subspan(4);
                if (!encryptedExtensionsSeen_) {
                    if (type != HANDSHAKE_ENCRYPTED_EXTENSIONS) return failHandshake();
                    Tls13WireReader encryptedExtensions(body);
                    uint16_t extensionsLength{};
                    std::span<const uint8_t> extensions;
                    if (!encryptedExtensions.u16(extensionsLength) ||
                        !encryptedExtensions.take(extensionsLength, extensions) ||
                        encryptedExtensions.remaining()) return failHandshake();
                    Tls13WireReader extensionReader(extensions);
                    bool sawServerName = false;
                    bool sawSupportedGroups = false;
                    while (extensionReader.remaining()) {
                        uint16_t extensionType{}, extensionLength{};
                        std::span<const uint8_t> value;
                        if (!extensionReader.u16(extensionType) || !extensionReader.u16(extensionLength) ||
                            !extensionReader.take(extensionLength, value)) return failHandshake();
                        if (extensionType == 0) {
                            // server_name was offered only for a DNS host, and
                            // its acknowledgement has an empty payload.
                            if (sawServerName || tlsIsIpv4Literal(serverName_) || !value.empty())
                                return failHandshake();
                            sawServerName = true;
                        } else if (extensionType == 10) {
                            // The server may advertise its supported groups in
                            // response to the client's supported_groups list.
                            Tls13WireReader groups(value);
                            uint16_t length{};
                            std::span<const uint8_t> groupIds;
                            if (sawSupportedGroups || !groups.u16(length) || length < 2 ||
                                (length & 1) || !groups.take(length, groupIds) || groups.remaining())
                                return failHandshake();
                            sawSupportedGroups = true;
                        } else {
                            // Other responses were not offered by this client.
                            return failHandshake();
                        }
                    }
                    encryptedExtensionsSeen_ = true;
                } else if (!certificateSeen_) {
                    if (type != HANDSHAKE_CERTIFICATE ||
                        !parseTls13CertificateMessage(body, peerCertificates_) ||
                        !verifyTlsServerCertificateChain(peerCertificates_, trustRoots, serverName_, nowUnixSeconds))
                        return failHandshake();
                    certificateSeen_ = true;
                } else if (!certificateVerifySeen_) {
                    if (type != HANDSHAKE_CERTIFICATE_VERIFY || body.size() < 4) return failHandshake();
                    const uint16_t scheme = (static_cast<uint16_t>(body[0]) << 8) | body[1];
                    const uint16_t signatureLength = (static_cast<uint16_t>(body[2]) << 8) | body[3];
                    if (signatureLength != body.size() - 4 ||
                        !verifyTls13ServerCertificateVerify(peerCertificates_.front(), scheme,
                            body.subspan(4), transcriptHash_)) return failHandshake();
                    certificateVerifySeen_ = true;
                } else if (!serverFinishedVerified_) {
                    if (type != HANDSHAKE_FINISHED || body.size() != 32) return failHandshake();
                    const auto finishedKey = HkdfSha256::expandLabel(serverHsSecret_, "finished", {}, 32);
                    if (!finishedKey.good() || finishedKey.size() != 32) return failHandshake();
                    const auto expected = HmacSha256::compute(finishedKey, transcriptHash_);
                    if (!tlsConstantTimeEqual(std::span<const uint8_t>(expected), body)) return failHandshake();
                    serverFinishedVerified_ = true;
                } else return failHandshake();

                transcript_.insert(transcript_.end(), message.begin(), message.end());
                if (!transcript_.good()) return failHandshake();
                transcriptHash_ = Sha256::hash(transcript_);
                messageOffset += messageLength;
                if (serverFinishedVerified_ && messageOffset != pendingHandshake_.size()) return failHandshake();
            }
            if (messageOffset) pendingHandshake_.erase(pendingHandshake_.begin(),
                pendingHandshake_.begin() + static_cast<std::ptrdiff_t>(messageOffset));
            if (serverFinishedVerified_ && !pendingHandshake_.empty()) return failHandshake();
            if (serverFinishedVerified_ && recordOffset != wireBytes.size()) return failHandshake();
        }

        if (!serverFinishedVerified_) return false; // More records are needed; state is retained.
        const auto emptyHash = Sha256::hash(std::span<const uint8_t>{});
        std::array<uint8_t, 32> derivedSecret{};
        if (!KeySchedule::deriveSecret(handshakeSecret_, "derived", emptyHash, derivedSecret)) return failHandshake();
        std::array<uint8_t, 32> zeroInput{};
        masterSecret_ = HkdfSha256::extract(derivedSecret, zeroInput);
        if (!KeySchedule::deriveSecret(masterSecret_, "c ap traffic", transcriptHash_, clientAppSecret_) ||
            !KeySchedule::deriveSecret(masterSecret_, "s ap traffic", transcriptHash_, serverAppSecret_) ||
            !KeySchedule::deriveTrafficKeys(clientAppSecret_, clientAppKeys_) ||
            !KeySchedule::deriveTrafficKeys(serverAppSecret_, serverAppKeys_)) return failHandshake();
        serverSeq_ = 0;
        serverFinishedVerified_ = true;
        return true;
    }

    // Complete the client side of the handshake only after the server chain,
    // CertificateVerify and Finished have passed. The caller must transmit this
    // record before treating the connection as established.
    bool createClientFinished(micant::shared::BoundedVector<uint8_t>& wireRecord) {
        wireRecord.clear();
        if (failed_ || established_ || clientFinishedPending_ || !serverFinishedVerified_) return false;
        const auto finishedKey = HkdfSha256::expandLabel(clientHsSecret_, "finished", {}, 32);
        if (!finishedKey.good() || finishedKey.size() != 32) return failHandshake();
        const auto verifyData = HmacSha256::compute(finishedKey, transcriptHash_);
        micant::shared::BoundedVector<uint8_t> finishedMessage{HANDSHAKE_FINISHED, 0, 0, 32};
        finishedMessage.insert(finishedMessage.end(), verifyData.begin(), verifyData.end());
        if (!finishedMessage.good()) return failHandshake();
        wireRecord = RecordLayer::wrapRecord(clientHsKeys_, 0, CONTENT_TYPE_HANDSHAKE, finishedMessage);
        if (wireRecord.empty() || !wireRecord.good()) return failHandshake();
        clientFinishedPending_ = true;
        return true;
    }

    void confirmClientFinishedTransmitted(bool success) noexcept {
        if (!clientFinishedPending_ || failed_ || established_) return;
        clientFinishedPending_ = false;
        if (!success) { failHandshake(); return; }
        // No further handshake records are accepted after Finished; retain
        // only the application traffic secrets needed for KeyUpdate.
        eraseBytes(clientPrivateKey_.data(), clientPrivateKey_.size());
        eraseBytes(sharedSecret_.data(), sharedSecret_.size());
        eraseBytes(handshakeSecret_.data(), handshakeSecret_.size());
        eraseBytes(clientHsSecret_.data(), clientHsSecret_.size());
        eraseBytes(serverHsSecret_.data(), serverHsSecret_.size());
        eraseBytes(&clientHsKeys_, sizeof(clientHsKeys_));
        eraseBytes(&serverHsKeys_, sizeof(serverHsKeys_));
        eraseBytes(masterSecret_.data(), masterSecret_.size());
        established_ = true;
    }

    micant::shared::BoundedVector<uint8_t> encryptAppData(std::span<const uint8_t> plaintext) {
        if (!established_ || clientKeyUpdatePending_ || writeClosed_) return {};
        if (clientSeq_ == UINT64_MAX) { failHandshake(); return {}; }
        auto record = RecordLayer::wrapRecord(clientAppKeys_, clientSeq_++, CONTENT_TYPE_APPLICATION_DATA, plaintext);
        if (!record.good() || record.empty()) { failHandshake(); return {}; }
        return record;
    }

    micant::shared::BoundedVector<uint8_t> createCloseNotify() {
        if (!established_ || clientKeyUpdatePending_ || writeClosed_ || clientSeq_ == UINT64_MAX)
            return {};
        constexpr std::array<uint8_t, 2> alert{1, 0}; // warning, close_notify
        auto record = RecordLayer::wrapRecord(clientAppKeys_, clientSeq_++, CONTENT_TYPE_ALERT, alert);
        if (!record.good() || record.empty()) { failHandshake(); return {}; }
        writeClosed_ = true;
        return record;
    }

    bool decryptAppData(std::span<const uint8_t> wireRecord, micant::shared::BoundedVector<uint8_t>& plaintext) {
        uint8_t contentType = 0;
        if (!decryptRecord(wireRecord, contentType, plaintext)) return false;
        if (contentType != CONTENT_TYPE_APPLICATION_DATA) { failHandshake(); return false; }
        return true;
    }

    bool decryptRecord(std::span<const uint8_t> wireRecord, uint8_t& contentType,
                       micant::shared::BoundedVector<uint8_t>& plaintext) {
        if (!established_) return false;
        if (serverSeq_ == UINT64_MAX ||
            !RecordLayer::unwrapRecord(serverAppKeys_, serverSeq_++, wireRecord, contentType, plaintext)) {
            failHandshake();
            return false;
        }
        return true;
    }

    // The KeyUpdate message is authenticated with the previous receive key.
    // Its caller verifies record-boundary alignment before invoking this.
    // A requested reply is encrypted under the old send key and the send key
    // changes only after that complete record has actually been transmitted.
    bool processServerKeyUpdate(std::span<const uint8_t> body,
                                micant::shared::BoundedVector<uint8_t>& reply) {
        reply.clear();
        if (!established_ || clientKeyUpdatePending_ || body.size() != 1 || body[0] > 1)
            return failHandshake();
        auto nextServer = HkdfSha256::expandLabel(serverAppSecret_, "traffic upd", {}, 32);
        if (!nextServer.good() || nextServer.size() != 32) {
            eraseBytes(nextServer.data(), nextServer.size());
            return failHandshake();
        }
        std::array<uint8_t, 32> serverSecret{};
        micant::shared::boundedMemoryCopy(serverSecret.data(), nextServer.data(), serverSecret.size());
        eraseBytes(nextServer.data(), nextServer.size());
        TrafficKeys serverKeys{};
        const auto failUpdate = [&]() noexcept {
            eraseBytes(serverSecret.data(), serverSecret.size());
            eraseBytes(&serverKeys, sizeof(serverKeys));
            return failHandshake();
        };
        if (!KeySchedule::deriveTrafficKeys(serverSecret, serverKeys)) return failUpdate();
        if (body[0] == 1) {
            auto nextClient = HkdfSha256::expandLabel(clientAppSecret_, "traffic upd", {}, 32);
            if (!nextClient.good() || nextClient.size() != 32 || clientSeq_ == UINT64_MAX) {
                eraseBytes(nextClient.data(), nextClient.size());
                return failUpdate();
            }
            micant::shared::boundedMemoryCopy(pendingClientSecret_.data(), nextClient.data(), pendingClientSecret_.size());
            eraseBytes(nextClient.data(), nextClient.size());
            if (!KeySchedule::deriveTrafficKeys(pendingClientSecret_, pendingClientKeys_)) return failUpdate();
            constexpr std::array<uint8_t, 5> message{24, 0, 0, 1, 0};
            reply = RecordLayer::wrapRecord(clientAppKeys_, clientSeq_++, CONTENT_TYPE_HANDSHAKE, message);
            if (!reply.good() || reply.empty()) return failUpdate();
            clientKeyUpdatePending_ = true;
        }
        serverAppSecret_ = serverSecret;
        serverAppKeys_ = serverKeys;
        eraseBytes(serverSecret.data(), serverSecret.size());
        eraseBytes(&serverKeys, sizeof(serverKeys));
        serverSeq_ = 0;
        return true;
    }

    bool confirmKeyUpdateTransmitted(bool success) noexcept {
        if (!clientKeyUpdatePending_ || !established_) return false;
        if (!success) return failHandshake();
        clientAppSecret_ = pendingClientSecret_;
        clientAppKeys_ = pendingClientKeys_;
        eraseBytes(pendingClientSecret_.data(), pendingClientSecret_.size());
        eraseBytes(&pendingClientKeys_, sizeof(pendingClientKeys_));
        clientSeq_ = 0;
        clientKeyUpdatePending_ = false;
        return true;
    }

    [[nodiscard]] bool isEstablished() const noexcept { return established_; }
    [[nodiscard]] bool isFailed() const noexcept { return failed_; }
    [[nodiscard]] const std::array<uint8_t, 32>& getSharedSecret() const noexcept { return sharedSecret_; }
    [[nodiscard]] const std::array<uint8_t, 32>& getClientPublicKey() const noexcept { return clientPublicKey_; }
    [[nodiscard]] const TrafficKeys& getClientAppKeys() const noexcept { return clientAppKeys_; }
    [[nodiscard]] const TrafficKeys& getServerAppKeys() const noexcept { return serverAppKeys_; }

private:
    static void eraseBytes(void* memory, size_t size) noexcept {
        auto* bytes = static_cast<volatile uint8_t*>(memory);
        while (size--) *bytes++ = 0;
    }
    void clearSecrets() noexcept {
        eraseBytes(clientPrivateKey_.data(), clientPrivateKey_.size());
        eraseBytes(sharedSecret_.data(), sharedSecret_.size());
        eraseBytes(handshakeSecret_.data(), handshakeSecret_.size());
        eraseBytes(clientHsSecret_.data(), clientHsSecret_.size());
        eraseBytes(serverHsSecret_.data(), serverHsSecret_.size());
        eraseBytes(clientAppSecret_.data(), clientAppSecret_.size());
        eraseBytes(serverAppSecret_.data(), serverAppSecret_.size());
        eraseBytes(masterSecret_.data(), masterSecret_.size());
        eraseBytes(&clientHsKeys_, sizeof(clientHsKeys_));
        eraseBytes(&serverHsKeys_, sizeof(serverHsKeys_));
        eraseBytes(&clientAppKeys_, sizeof(clientAppKeys_));
        eraseBytes(&serverAppKeys_, sizeof(serverAppKeys_));
        eraseBytes(pendingClientSecret_.data(), pendingClientSecret_.size());
        eraseBytes(&pendingClientKeys_, sizeof(pendingClientKeys_));
    }
    bool failHandshake() noexcept {
        established_ = false;
        failed_ = true;
        clearSecrets();
        return false;
    }

    micant::shared::BoundedVector<uint8_t> failClientHello() noexcept {
        failHandshake();
        return {};
    }

    bool established_{false};
    bool entropyAvailable_{false};
    bool failed_{false};
    bool serverFinishedVerified_{false};
    bool clientFinishedPending_{false};
    std::array<uint8_t, 32> clientRandom_{};
    std::array<uint8_t, 32> serverRandom_{};
    std::array<uint8_t, 32> clientPrivateKey_{};
    std::array<uint8_t, 32> clientPublicKey_{};
    std::array<uint8_t, 32> serverPublicKey_{};
    std::array<uint8_t, 32> sharedSecret_{};
    std::array<uint8_t, 32> transcriptHash_{};
    micant::shared::BoundedVector<uint8_t> lastClientHello_;
    micant::shared::BoundedVector<uint8_t> transcript_;
    micant::shared::BoundedVector<uint8_t> pendingHandshake_;
    micant::shared::BoundedVector<X509Certificate> peerCertificates_;
    micant::shared::AnsiString serverName_;
    bool clientHelloCreated_{false};
    bool serverHelloSeen_{false};
    bool encryptedExtensionsSeen_{false};
    bool certificateSeen_{false};
    bool certificateVerifySeen_{false};
    uint64_t serverHandshakeSeq_{0};
    std::array<uint8_t, 32> handshakeSecret_{};
    std::array<uint8_t, 32> clientHsSecret_{};
    std::array<uint8_t, 32> serverHsSecret_{};
    TrafficKeys clientHsKeys_{};
    TrafficKeys serverHsKeys_{};
    std::array<uint8_t, 32> masterSecret_{};

    std::array<uint8_t, 32> clientAppSecret_{};
    std::array<uint8_t, 32> serverAppSecret_{};
    TrafficKeys clientAppKeys_{};
    TrafficKeys serverAppKeys_{};
    std::array<uint8_t, 32> pendingClientSecret_{};
    TrafficKeys pendingClientKeys_{};
    bool clientKeyUpdatePending_{};
    bool writeClosed_{};
    uint64_t clientSeq_{0};
    uint64_t serverSeq_{0};
};


} // namespace micant::shared::tls13
