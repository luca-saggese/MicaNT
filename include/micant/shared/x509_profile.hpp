#pragma once

// Mode-neutral bounded X.509 DER profile shared by Ring 0 and Ring 3.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include "micant/shared/bounded_containers.hpp"
#include <string_view>
#include <utility>
#include "micant/shared/crypto_primitives.hpp"
#include "micant/shared/ecdsa_p256.hpp"

namespace micant::shared::crypto {

// ============================================================================
// 1. ASN.1 DER Universal Tags & OID Definitions
// ============================================================================

namespace asn1 {

inline constexpr uint8_t TAG_BOOLEAN           = 0x01;
inline constexpr uint8_t TAG_INTEGER           = 0x02;
inline constexpr uint8_t TAG_BIT_STRING         = 0x03;
inline constexpr uint8_t TAG_OCTET_STRING       = 0x04;
inline constexpr uint8_t TAG_NULL               = 0x05;
inline constexpr uint8_t TAG_OID                = 0x06;
inline constexpr uint8_t TAG_UTF8_STRING        = 0x0C;
inline constexpr uint8_t TAG_PRINTABLE_STRING   = 0x13;
inline constexpr uint8_t TAG_IA5_STRING         = 0x16;
inline constexpr uint8_t TAG_UTC_TIME           = 0x17;
inline constexpr uint8_t TAG_GENERALIZED_TIME   = 0x18;
inline constexpr uint8_t TAG_SEQUENCE           = 0x30;
inline constexpr uint8_t TAG_SET                = 0x31;
inline constexpr uint8_t TAG_CONTEXT_0          = 0xA0;
inline constexpr uint8_t TAG_CONTEXT_3          = 0xA3;

// Helper to write ASN.1 length
template <typename Buffer>
inline void writeLength(Buffer& buf, size_t len) {
    if (len < 128) {
        buf.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFF) {
        buf.push_back(0x81);
        buf.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        buf.push_back(0x82);
        buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        buf.push_back(0x84);
        buf.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(len & 0xFF));
    }
}

// Helper to write TLV element
template <typename Buffer>
inline void writeTLV(Buffer& buf, uint8_t tag, std::span<const uint8_t> value) {
    buf.push_back(tag);
    writeLength(buf, value.size());
    buf.insert(buf.end(), value.begin(), value.end());
}

template <typename Buffer>
inline void writeSequence(Buffer& buf, std::span<const uint8_t> inner) {
    writeTLV(buf, TAG_SEQUENCE, inner);
}

template <typename Buffer>
inline void writeInteger(Buffer& buf, uint64_t val) {
    micant::shared::BoundedVector<uint8_t> bytes;
    for (int i = 7; i >= 0; --i) {
        uint8_t b = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        if (!bytes.empty() || b != 0) bytes.push_back(b);
    }
    if (bytes.empty()) bytes.push_back(0);
    // If MSB is set, prepend 0x00 to denote positive signed integer
    if (bytes[0] & 0x80) bytes.insert(bytes.begin(), 0x00);
    writeTLV(buf, TAG_INTEGER, bytes);
}

template <typename Buffer>
inline void writeOid(Buffer& buf, std::span<const uint8_t> oidBytes) {
    writeTLV(buf, TAG_OID, oidBytes);
}

template <typename Buffer>
inline void writePrintableString(Buffer& buf, std::string_view str) {
    writeTLV(buf, TAG_PRINTABLE_STRING, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(str.data()), str.size()));
}

template <typename Buffer>
inline void writeUtcTime(Buffer& buf, std::string_view utcStr) {
    writeTLV(buf, TAG_UTC_TIME, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(utcStr.data()), utcStr.size()));
}

struct DerElement {
    uint8_t tag{};
    std::span<const uint8_t> value{};
    std::span<const uint8_t> encoded{};
};

// Strict, bounded DER TLV reader: rejects indefinite/non-minimal lengths and
// high-tag-number forms not needed by the supported X.509 profile.
inline bool readDerElement(std::span<const uint8_t> input, size_t& cursor,
                           DerElement& element) noexcept {
    if (cursor > input.size() || input.size() - cursor < 2) return false;
    const size_t begin = cursor;
    const uint8_t tag = input[cursor++];
    if ((tag & 0x1f) == 0x1f) return false;
    const uint8_t firstLength = input[cursor++];
    size_t length = 0;
    if ((firstLength & 0x80) == 0) {
        length = firstLength;
    } else {
        const size_t count = firstLength & 0x7f;
        if (count == 0 || count > sizeof(size_t) || count > input.size() - cursor || input[cursor] == 0)
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (length > (SIZE_MAX - input[cursor]) / 256) return false;
            length = length * 256 + input[cursor++];
        }
        if (length < 128) return false;
    }
    if (length > input.size() - cursor) return false;
    element.tag = tag;
    element.value = input.subspan(cursor, length);
    cursor += length;
    element.encoded = input.subspan(begin, cursor - begin);
    return true;
}

inline bool oidEquals(std::span<const uint8_t> oid, std::span<const uint8_t> expected) noexcept {
    return oid.size() == expected.size() && micant::shared::boundedMemoryCompare(oid.data(), expected.data(), oid.size()) == 0;
}

inline bool readObjectIdentifier(std::span<const uint8_t> encoded, micant::shared::BoundedVector<uint8_t>& oid) {
    size_t cursor = 0;
    DerElement value{};
    if (!readDerElement(encoded, cursor, value) || value.tag != TAG_OID || cursor != encoded.size() ||
        value.value.empty()) return false;
    // Each base-128 subidentifier must terminate and must not have a leading
    // zero continuation octet (non-minimal DER OID representation).
    bool atSubidentifierStart = true;
    for (uint8_t byte : value.value) {
        if (atSubidentifierStart && byte == 0x80) return false;
        atSubidentifierStart = (byte & 0x80) == 0;
    }
    if (!atSubidentifierStart) return false;
    oid.assign(value.value.begin(), value.value.end());
    return true;
}

inline bool readCommonName(std::span<const uint8_t> name, micant::shared::AnsiString& commonName) {
    static constexpr uint8_t commonNameOid[] = {0x55, 0x04, 0x03};
    size_t cursor = 0;
    DerElement sequence{};
    if (!readDerElement(name, cursor, sequence) || sequence.tag != TAG_SEQUENCE || cursor != name.size())
        return false;
    size_t rdnCursor = 0;
    while (rdnCursor < sequence.value.size()) {
        DerElement rdn{};
        if (!readDerElement(sequence.value, rdnCursor, rdn) || rdn.tag != TAG_SET) return false;
        size_t atvCursor = 0;
        while (atvCursor < rdn.value.size()) {
            DerElement atv{};
            if (!readDerElement(rdn.value, atvCursor, atv) || atv.tag != TAG_SEQUENCE) return false;
            size_t fieldCursor = 0;
            DerElement oid{}, value{};
            if (!readDerElement(atv.value, fieldCursor, oid) || oid.tag != TAG_OID ||
                !readDerElement(atv.value, fieldCursor, value) || fieldCursor != atv.value.size()) return false;
            if (oid.value.size() == sizeof(commonNameOid) &&
                micant::shared::boundedMemoryCompare(oid.value.data(), commonNameOid, sizeof(commonNameOid)) == 0) {
                if (value.tag != 0x0c && value.tag != 0x13 && value.tag != 0x16 && value.tag != 0x14)
                    return false;
                for (uint8_t ch : value.value) if (ch == 0 || ch >= 0x80) return false;
                commonName.assign(reinterpret_cast<const char*>(value.value.data()), value.value.size());
            }
        }
    }
    return true;
}

inline micant::shared::AnsiString formatIpv4Address(std::span<const uint8_t> bytes) {
    micant::shared::AnsiString result;
    if (bytes.size() != 4) return result;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) result.push_back('.');
        char digits[3]{};
        size_t used = 0;
        unsigned value = bytes[i];
        do { digits[used++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        while (used) result.push_back(digits[--used]);
    }
    return result;
}

} // namespace asn1

// ============================================================================
// 2. Sovereign X.509 Certificate Representation
// ============================================================================

struct X509Certificate {
    uint64_t serialNumber{1};
    micant::shared::AnsiString subjectCommonName;
    micant::shared::AnsiString issuerCommonName;
    micant::shared::AnsiString validNotBefore; // YYMMDDHHMMSSZ
    micant::shared::AnsiString validNotAfter;
    micant::shared::BoundedVector<uint8_t> issuerNameDer;
    micant::shared::BoundedVector<uint8_t> subjectNameDer;
    micant::shared::BoundedVector<micant::shared::AnsiString> dnsNames;
    micant::shared::BoundedVector<micant::shared::AnsiString> ipAddresses;
    micant::shared::BoundedVector<uint8_t> publicKey;
    micant::shared::BoundedVector<uint8_t> signature;
    micant::shared::BoundedVector<uint8_t> signatureAlgorithmOidDer;
    micant::shared::BoundedVector<uint8_t> publicKeyAlgorithmOidDer;
    micant::shared::BoundedVector<uint8_t> publicKeyCurveOidDer;
    micant::shared::BoundedVector<uint8_t> tbsCertificateDer;
    micant::shared::BoundedVector<uint8_t> rawDer;
    std::array<uint8_t, 32> fingerprintSha256{};
    bool isCertificateAuthority{false};
    bool hasKeyUsage{false};
    bool canSignCertificates{false};
    bool canSignData{false};
    bool hasExtendedKeyUsage{false};
    bool allowsTlsServerAuthentication{false};
    bool hasSubjectAltName{false};
    int32_t pathLengthConstraint{-1};

    [[nodiscard]] bool isValidAtUnixSeconds(int64_t now) const noexcept {
        if (now < 0) return false;
        auto parseTime = [](std::string_view text, int64_t& value) noexcept {
            const bool utc = text.size() == 13;
            if ((!utc && text.size() != 15) || text.back() != 'Z') return false;
            const size_t yearDigits = utc ? 2 : 4;
            unsigned fields[6]{};
            size_t position = 0;
            for (size_t field = 0; field < 6; ++field) {
                const size_t digits = field == 0 ? yearDigits : 2;
                for (size_t i = 0; i < digits; ++i) {
                    const char c = text[position++];
                    if (c < '0' || c > '9') return false;
                    fields[field] = fields[field] * 10 + static_cast<unsigned>(c - '0');
                }
            }
            if (utc) fields[0] += fields[0] >= 50 ? 1900 : 2000;
            const unsigned year = fields[0], month = fields[1], day = fields[2];
            const unsigned hour = fields[3], minute = fields[4], second = fields[5];
            if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
            const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
            constexpr unsigned monthDays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
            const unsigned maxDay = monthDays[month - 1] + (month == 2 && leap ? 1u : 0u);
            if (day < 1 || day > maxDay) return false;
            int adjustedYear = static_cast<int>(year) - (month <= 2 ? 1 : 0);
            const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
            const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
            const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
            const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
            const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
            const int64_t days = static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
            value = days * 86400 + static_cast<int64_t>(hour) * 3600 + minute * 60 + second;
            return true;
        };
        int64_t notBefore{}, notAfter{};
        return parseTime(validNotBefore, notBefore) && parseTime(validNotAfter, notAfter) &&
            notBefore <= now && now <= notAfter;
    }

    // Parse a bounded X.509 DER certificate profile used by the TLS verifier.
    // Unsupported critical extensions and malformed/non-minimal DER fail.
    static bool parseDer(std::span<const uint8_t> der, X509Certificate& output) {
        if (der.empty() || der.size() > 1024 * 1024) return false;
        X509Certificate parsed{};
        size_t outerCursor = 0;
        asn1::DerElement outer{};
        if (!asn1::readDerElement(der, outerCursor, outer) || outer.tag != asn1::TAG_SEQUENCE ||
            outerCursor != der.size()) return false;
        size_t certificateCursor = 0;
        asn1::DerElement tbs{}, outerSignatureAlgorithm{}, signatureBits{};
        if (!asn1::readDerElement(outer.value, certificateCursor, tbs) || tbs.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(outer.value, certificateCursor, outerSignatureAlgorithm) ||
                outerSignatureAlgorithm.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(outer.value, certificateCursor, signatureBits) ||
                signatureBits.tag != asn1::TAG_BIT_STRING || certificateCursor != outer.value.size() ||
            signatureBits.value.size() < 2 || signatureBits.value[0] != 0) return false;

        auto readAlgorithm = [](const asn1::DerElement& algorithm, micant::shared::BoundedVector<uint8_t>& oid,
                                micant::shared::BoundedVector<uint8_t>* curve = nullptr) {
            size_t cursor = 0;
            asn1::DerElement oidElement{};
            if (!asn1::readDerElement(algorithm.value, cursor, oidElement) || oidElement.tag != asn1::TAG_OID ||
                oidElement.value.empty()) return false;
            oid.assign(oidElement.value.begin(), oidElement.value.end());
            if (cursor < algorithm.value.size()) {
                asn1::DerElement parameters{};
                if (!asn1::readDerElement(algorithm.value, cursor, parameters) || cursor != algorithm.value.size())
                    return false;
                if (curve && parameters.tag == asn1::TAG_OID && !parameters.value.empty())
                    curve->assign(parameters.value.begin(), parameters.value.end());
                else if (parameters.tag != asn1::TAG_NULL || !parameters.value.empty())
                    return false;
                static constexpr uint8_t ecdsaSha256[] = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02};
                if (!curve && asn1::oidEquals(oid, ecdsaSha256)) return false;
            }
            return cursor == algorithm.value.size();
        };
        micant::shared::BoundedVector<uint8_t> outerSignatureOid;
        if (!readAlgorithm(outerSignatureAlgorithm, outerSignatureOid)) return false;

        size_t tbsCursor = 0;
        asn1::DerElement field{};
        uint64_t version = 0;
        if (!asn1::readDerElement(tbs.value, tbsCursor, field)) return false;
        if (field.tag == asn1::TAG_CONTEXT_0) {
            size_t versionCursor = 0;
            asn1::DerElement integer{};
            if (!asn1::readDerElement(field.value, versionCursor, integer) || integer.tag != asn1::TAG_INTEGER ||
                versionCursor != field.value.size() || integer.value.empty() || integer.value.size() > 1 ||
                (integer.value[0] & 0x80)) return false;
            version = integer.value[0];
            if (version > 2 || !asn1::readDerElement(tbs.value, tbsCursor, field)) return false;
        }
        if (field.tag != asn1::TAG_INTEGER || field.value.empty() || field.value.size() > 20 ||
            (field.value[0] & 0x80) || (field.value.size() > 1 && field.value[0] == 0 && !(field.value[1] & 0x80)))
            return false;
        parsed.serialNumber = 0;
        const size_t serialStart = field.value.size() > 8 ? field.value.size() - 8 : 0;
        for (size_t i = serialStart; i < field.value.size(); ++i)
            parsed.serialNumber = (parsed.serialNumber << 8) | field.value[i];
        asn1::DerElement tbsSignatureAlgorithm{}, issuer{}, validity{}, subject{}, subjectPublicKeyInfo{};
        if (!asn1::readDerElement(tbs.value, tbsCursor, tbsSignatureAlgorithm) ||
            tbsSignatureAlgorithm.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(tbs.value, tbsCursor, issuer) || issuer.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(tbs.value, tbsCursor, validity) || validity.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(tbs.value, tbsCursor, subject) || subject.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(tbs.value, tbsCursor, subjectPublicKeyInfo) ||
                subjectPublicKeyInfo.tag != asn1::TAG_SEQUENCE) return false;
        micant::shared::BoundedVector<uint8_t> tbsSignatureOid;
        if (!readAlgorithm(tbsSignatureAlgorithm, tbsSignatureOid) || tbsSignatureOid != outerSignatureOid)
            return false;
        parsed.signatureAlgorithmOidDer = std::move(outerSignatureOid);
        if (!asn1::readCommonName(issuer.encoded, parsed.issuerCommonName) ||
            !asn1::readCommonName(subject.encoded, parsed.subjectCommonName)) return false;
        parsed.issuerNameDer.assign(issuer.encoded.begin(), issuer.encoded.end());
        parsed.subjectNameDer.assign(subject.encoded.begin(), subject.encoded.end());

        size_t validityCursor = 0;
        asn1::DerElement notBefore{}, notAfter{};
        if (!asn1::readDerElement(validity.value, validityCursor, notBefore) ||
            !asn1::readDerElement(validity.value, validityCursor, notAfter) || validityCursor != validity.value.size())
            return false;
        auto copyTime = [](const asn1::DerElement& time, micant::shared::AnsiString& destination) {
            const size_t expected = time.tag == asn1::TAG_UTC_TIME ? 13 : time.tag == 0x18 ? 15 : 0;
            if (!expected || time.value.size() != expected || time.value.back() != 'Z') return false;
            for (size_t i = 0; i + 1 < time.value.size(); ++i)
                if (time.value[i] < '0' || time.value[i] > '9') return false;
            destination.assign(reinterpret_cast<const char*>(time.value.data()), time.value.size());
            return true;
        };
        if (!copyTime(notBefore, parsed.validNotBefore) || !copyTime(notAfter, parsed.validNotAfter)) return false;

        size_t spkiCursor = 0;
        asn1::DerElement publicKeyAlgorithm{}, publicKeyBits{};
        if (!asn1::readDerElement(subjectPublicKeyInfo.value, spkiCursor, publicKeyAlgorithm) ||
            publicKeyAlgorithm.tag != asn1::TAG_SEQUENCE ||
            !asn1::readDerElement(subjectPublicKeyInfo.value, spkiCursor, publicKeyBits) ||
                publicKeyBits.tag != asn1::TAG_BIT_STRING || publicKeyBits.value.size() < 2 ||
                publicKeyBits.value[0] != 0 || spkiCursor != subjectPublicKeyInfo.value.size() ||
            !readAlgorithm(publicKeyAlgorithm, parsed.publicKeyAlgorithmOidDer, &parsed.publicKeyCurveOidDer)) return false;
        parsed.publicKey.assign(publicKeyBits.value.begin() + 1, publicKeyBits.value.end());
        static constexpr uint8_t oidEcPublicKey[] = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
        static constexpr uint8_t oidP256[] = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07};
        static constexpr uint8_t oidRsaPublicKey[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
        if (asn1::oidEquals(parsed.publicKeyAlgorithmOidDer, oidEcPublicKey) &&
            (!asn1::oidEquals(parsed.publicKeyCurveOidDer, oidP256) ||
             parsed.publicKey.size() != 65 || parsed.publicKey[0] != 4)) return false;
        if (asn1::oidEquals(parsed.publicKeyAlgorithmOidDer, oidRsaPublicKey) &&
            !parsed.publicKeyCurveOidDer.empty()) return false;

        static constexpr uint8_t oidSubjectAltName[] = {0x55, 0x1d, 0x11};
        static constexpr uint8_t oidBasicConstraints[] = {0x55, 0x1d, 0x13};
        static constexpr uint8_t oidKeyUsage[] = {0x55, 0x1d, 0x0f};
        static constexpr uint8_t oidExtendedKeyUsage[] = {0x55, 0x1d, 0x25};
        static constexpr uint8_t oidServerAuth[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
        micant::shared::BoundedVector<micant::shared::BoundedVector<uint8_t>> seenExtensions;
        while (tbsCursor < tbs.value.size()) {
            asn1::DerElement extensionWrapper{};
            if (!asn1::readDerElement(tbs.value, tbsCursor, extensionWrapper) || extensionWrapper.tag != 0xa3 ||
                version != 2) return false;
            size_t explicitCursor = 0;
            asn1::DerElement extensions{};
            if (!asn1::readDerElement(extensionWrapper.value, explicitCursor, extensions) ||
                extensions.tag != asn1::TAG_SEQUENCE || explicitCursor != extensionWrapper.value.size()) return false;
            size_t extensionCursor = 0;
            while (extensionCursor < extensions.value.size()) {
                asn1::DerElement extension{};
                if (!asn1::readDerElement(extensions.value, extensionCursor, extension) ||
                    extension.tag != asn1::TAG_SEQUENCE) return false;
                size_t itemCursor = 0;
                asn1::DerElement oid{}, next{}, octets{};
                if (!asn1::readDerElement(extension.value, itemCursor, oid) || oid.tag != asn1::TAG_OID) return false;
                for (const auto& seen : seenExtensions)
                    if (asn1::oidEquals(oid.value, seen)) return false;
                seenExtensions.emplace_back(oid.value.begin(), oid.value.end());
                bool critical = false;
                if (itemCursor < extension.value.size()) {
                    size_t probe = itemCursor;
                    if (!asn1::readDerElement(extension.value, probe, next)) return false;
                    if (next.tag == asn1::TAG_BOOLEAN) {
                        if (next.value.size() != 1 || (next.value[0] != 0 && next.value[0] != 0xff)) return false;
                        critical = next.value[0] != 0;
                        itemCursor = probe;
                    }
                }
                if (!asn1::readDerElement(extension.value, itemCursor, octets) || octets.tag != asn1::TAG_OCTET_STRING ||
                    itemCursor != extension.value.size()) return false;
                const auto extensionOid = oid.value;
                size_t innerCursor = 0;
                asn1::DerElement inner{};
                if (asn1::oidEquals(extensionOid, oidSubjectAltName)) {
                    parsed.hasSubjectAltName = true;
                    if (!asn1::readDerElement(octets.value, innerCursor, inner) || inner.tag != asn1::TAG_SEQUENCE ||
                        innerCursor != octets.value.size()) return false;
                    size_t nameCursor = 0;
                    while (nameCursor < inner.value.size()) {
                        asn1::DerElement name{};
                        if (!asn1::readDerElement(inner.value, nameCursor, name)) return false;
                        if (name.tag == 0x82) {
                            if (name.value.empty()) return false;
                            for (uint8_t ch : name.value) if (ch == 0 || ch >= 0x80) return false;
                            micant::shared::AnsiString dns(reinterpret_cast<const char*>(name.value.data()), name.value.size());
                            if (!micant::shared::isContainerGood(dns)) return false;
                            const size_t wildcard = dns.find('*');
                            if (wildcard != micant::shared::AnsiString::npos &&
                                (wildcard != 0 || !dns.starts_with("*.") || dns.find('*', 1) != micant::shared::AnsiString::npos))
                                return false;
                            parsed.dnsNames.push_back(std::move(dns));
                        } else if (name.tag == 0x87) {
                            if (name.value.size() == 4) {
                                auto ipv4 = asn1::formatIpv4Address(name.value);
                                if (!micant::shared::isContainerGood(ipv4) || ipv4.empty() || !parsed.ipAddresses.push_back(std::move(ipv4)))
                                    return false;
                            }
                        }
                    }
                } else if (asn1::oidEquals(extensionOid, oidBasicConstraints)) {
                    if (!asn1::readDerElement(octets.value, innerCursor, inner) || inner.tag != asn1::TAG_SEQUENCE ||
                        innerCursor != octets.value.size()) return false;
                    size_t constraintsCursor = 0;
                    if (constraintsCursor < inner.value.size()) {
                        asn1::DerElement constraint{};
                        if (!asn1::readDerElement(inner.value, constraintsCursor, constraint)) return false;
                        if (constraint.tag == asn1::TAG_BOOLEAN) {
                            if (constraint.value.size() != 1 || (constraint.value[0] != 0 && constraint.value[0] != 0xff))
                                return false;
                            parsed.isCertificateAuthority = constraint.value[0] != 0;
                            if (constraintsCursor < inner.value.size()) {
                                if (!asn1::readDerElement(inner.value, constraintsCursor, constraint) ||
                                    constraint.tag != asn1::TAG_INTEGER || !parsed.isCertificateAuthority ||
                                    constraint.value.empty() || constraint.value.size() > 4 ||
                                    (constraint.value[0] & 0x80) ||
                                    (constraint.value.size() > 1 && constraint.value[0] == 0 &&
                                     !(constraint.value[1] & 0x80))) return false;
                                parsed.pathLengthConstraint = 0;
                                for (uint8_t value : constraint.value) {
                                    if (parsed.pathLengthConstraint > (INT32_MAX - value) / 256) return false;
                                    parsed.pathLengthConstraint = parsed.pathLengthConstraint * 256 + value;
                                }
                            }
                        } else return false;
                    }
                    if (constraintsCursor != inner.value.size()) return false;
                    if (parsed.isCertificateAuthority && !critical) return false;
                } else if (asn1::oidEquals(extensionOid, oidKeyUsage)) {
                    if (!asn1::readDerElement(octets.value, innerCursor, inner) || inner.tag != asn1::TAG_BIT_STRING ||
                        innerCursor != octets.value.size() || inner.value.size() < 2 || inner.value[0] > 7) return false;
                    const unsigned unusedBits = inner.value[0];
                    if (unusedBits && (inner.value.back() & ((1u << unusedBits) - 1u))) return false;
                    parsed.hasKeyUsage = true;
                    parsed.canSignCertificates = inner.value.size() > 1 && (inner.value[1] & 0x04) != 0;
                    parsed.canSignData = inner.value.size() > 1 && (inner.value[1] & 0x80) != 0;
                } else if (asn1::oidEquals(extensionOid, oidExtendedKeyUsage)) {
                    if (!asn1::readDerElement(octets.value, innerCursor, inner) || inner.tag != asn1::TAG_SEQUENCE ||
                        innerCursor != octets.value.size()) return false;
                    parsed.hasExtendedKeyUsage = true;
                    size_t usageCursor = 0;
                    while (usageCursor < inner.value.size()) {
                        asn1::DerElement usage{};
                        if (!asn1::readDerElement(inner.value, usageCursor, usage) || usage.tag != asn1::TAG_OID)
                            return false;
                        if (asn1::oidEquals(usage.value, oidServerAuth)) parsed.allowsTlsServerAuthentication = true;
                    }
                } else if (critical) {
                    return false;
                }
            }
        }
        if (signatureBits.value.size() < 2) return false;
        parsed.signature.assign(signatureBits.value.begin() + 1, signatureBits.value.end());
        parsed.tbsCertificateDer.assign(tbs.encoded.begin(), tbs.encoded.end());
        parsed.rawDer.assign(der.begin(), der.end());
        if (!parsed.issuerNameDer.good() || !parsed.subjectNameDer.good() ||
            !parsed.publicKey.good() || !parsed.signature.good() ||
            !parsed.signatureAlgorithmOidDer.good() || !parsed.publicKeyAlgorithmOidDer.good() ||
            !parsed.publicKeyCurveOidDer.good() ||
            !parsed.tbsCertificateDer.good() || !parsed.rawDer.good() ||
            !parsed.dnsNames.good() || !parsed.ipAddresses.good() || !seenExtensions.good() ||
            !micant::shared::isContainerGood(parsed.subjectCommonName) ||
            !micant::shared::isContainerGood(parsed.issuerCommonName) ||
            !micant::shared::isContainerGood(parsed.validNotBefore) ||
            !micant::shared::isContainerGood(parsed.validNotAfter)) return false;
        parsed.fingerprintSha256 = Sha256::hash(der);
        output = std::move(parsed);
        return true;
    }

    [[nodiscard]] bool matchesHost(std::string_view hostOrIp) const noexcept {
        if (hostOrIp.empty()) return false;
        const bool isIp = hostOrIp.find(':') != std::string_view::npos ||
            hostOrIp.find_first_not_of("0123456789.") == std::string_view::npos;
        const auto equalNoCase = [](std::string_view left, std::string_view right) noexcept {
            if (left.size() != right.size()) return false;
            for (size_t i = 0; i < left.size(); ++i)
                if (static_cast<unsigned char>(micant::shared::asciiLower(left[i])) !=
                    static_cast<unsigned char>(micant::shared::asciiLower(right[i]))) return false;
            return true;
        };
        if (isIp) {
            for (const auto& ip : ipAddresses) if (ip == hostOrIp) return true;
            return false;
        }
        const auto matchesDns = [&](std::string_view pattern) noexcept {
            if (pattern.starts_with("*.")) {
                const std::string_view suffix(pattern.data() + 1, pattern.size() - 1);
                if (hostOrIp.size() <= suffix.size() ||
                    !equalNoCase(std::string_view(hostOrIp.data() + hostOrIp.size() - suffix.size(),
                                                  suffix.size()), suffix)) return false;
                return std::string_view(hostOrIp.data(), hostOrIp.size() - suffix.size()).find('.') == std::string_view::npos;
            }
            return equalNoCase(pattern, hostOrIp);
        };
        if (!dnsNames.empty()) {
            for (const auto& dns : dnsNames) if (matchesDns(dns)) return true;
            return false;
        }
        return !hasSubjectAltName && matchesDns(subjectCommonName);
    }
};

inline bool verifyTlsServerCertificateChain(
    std::span<const X509Certificate> presented,
    std::span<const X509Certificate> trustedRoots,
    std::string_view hostname,
    int64_t nowUnixSeconds) {
    if (presented.empty() || presented.size() > 8 || trustedRoots.empty() ||
        hostname.empty() || nowUnixSeconds < 0) return false;
    static constexpr uint8_t rsaEncryptionOid[] =
        {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
    static constexpr uint8_t sha256WithRsaOid[] =
        {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b};
    static constexpr uint8_t ecPublicKeyOid[] = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
    static constexpr uint8_t p256Oid[] = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07};
    static constexpr uint8_t ecdsaSha256Oid[] = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02};
    const auto valid = [&](const X509Certificate& cert) {
        return !cert.rawDer.empty() && !cert.tbsCertificateDer.empty() &&
            (asn1::oidEquals(cert.publicKeyAlgorithmOidDer, rsaEncryptionOid) ||
             (asn1::oidEquals(cert.publicKeyAlgorithmOidDer, ecPublicKeyOid) &&
              asn1::oidEquals(cert.publicKeyCurveOidDer, p256Oid))) &&
            cert.isValidAtUnixSeconds(nowUnixSeconds);
    };
    const auto issuedBy = [&](const X509Certificate& child, const X509Certificate& issuer) {
        if (!valid(child) || !valid(issuer) || child.issuerNameDer != issuer.subjectNameDer) return false;
        if (asn1::oidEquals(child.signatureAlgorithmOidDer, sha256WithRsaOid) &&
            asn1::oidEquals(issuer.publicKeyAlgorithmOidDer, rsaEncryptionOid))
            return RsaSha256Verifier::verifyPkcs1(issuer.publicKey, child.tbsCertificateDer, child.signature);
        if (asn1::oidEquals(child.signatureAlgorithmOidDer, ecdsaSha256Oid) &&
            asn1::oidEquals(issuer.publicKeyAlgorithmOidDer, ecPublicKeyOid) &&
            asn1::oidEquals(issuer.publicKeyCurveOidDer, p256Oid))
            return EcdsaP256Sha256Verifier::verify(issuer.publicKey, child.tbsCertificateDer, child.signature);
        return false;
    };
    const auto& leaf = presented.front();
    if (!valid(leaf) || !leaf.matchesHost(hostname) ||
        (leaf.hasExtendedKeyUsage && !leaf.allowsTlsServerAuthentication) ||
        (leaf.hasKeyUsage && !leaf.canSignData)) return false;

    const X509Certificate* child = &leaf;
    size_t caBelow = 0;
    for (size_t index = 1; index <= presented.size(); ++index) {
        if (index < presented.size()) {
            const auto& issuer = presented[index];
            const size_t pathBelow = caBelow +
                (child->isCertificateAuthority && child->subjectNameDer != child->issuerNameDer ? 1u : 0u);
            if (!issuer.isCertificateAuthority || (issuer.hasKeyUsage && !issuer.canSignCertificates) ||
                (issuer.hasExtendedKeyUsage && !issuer.allowsTlsServerAuthentication) ||
                (issuer.pathLengthConstraint >= 0 &&
                 pathBelow > static_cast<size_t>(issuer.pathLengthConstraint)) || !issuedBy(*child, issuer))
                return false;
            caBelow = pathBelow;
            child = &issuer;
            continue;
        }
        const auto trusted = std::find_if(trustedRoots.begin(), trustedRoots.end(), [&](const X509Certificate& root) {
            return !root.rawDer.empty() && root.rawDer == child->rawDer;
        });
        if (trusted != trustedRoots.end())
            return trusted->isCertificateAuthority &&
                (!trusted->hasKeyUsage || trusted->canSignCertificates) &&
                (!trusted->hasExtendedKeyUsage || trusted->allowsTlsServerAuthentication) &&
                issuedBy(*trusted, *trusted);
        for (const auto& root : trustedRoots) {
            const size_t pathBelow = caBelow +
                (child->isCertificateAuthority && child->subjectNameDer != child->issuerNameDer ? 1u : 0u);
            if (root.isCertificateAuthority && (!root.hasKeyUsage || root.canSignCertificates) &&
                (!root.hasExtendedKeyUsage || root.allowsTlsServerAuthentication) &&
                (root.pathLengthConstraint < 0 || pathBelow <= static_cast<size_t>(root.pathLengthConstraint)) &&
                issuedBy(*child, root)) return true;
        }
        return false;
    }
    return false;
}

} // namespace micant::shared::crypto
