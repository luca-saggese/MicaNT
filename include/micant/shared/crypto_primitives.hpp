#pragma once

// Mode-neutral AMD64 cryptographic primitives shared by kernel providers and
// native Ring 3 consumers. Algorithms follow the public FIPS/RFC specifications.
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

namespace micant::shared::crypto {

// ============================================================================
// 1. Clean-Room SHA-1 (FIPS 180-1) Implementation
// ============================================================================

class Sha1 {
public:
    static constexpr size_t DIGEST_SIZE = 20;
    static constexpr size_t BLOCK_SIZE  = 64;

    struct Context {
        uint32_t state[5]{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        uint64_t count{0};
        uint8_t buffer[64]{};
    };

    static void init(Context& ctx) noexcept {
        ctx.state[0] = 0x67452301;
        ctx.state[1] = 0xEFCDAB89;
        ctx.state[2] = 0x98BADCFE;
        ctx.state[3] = 0x10325476;
        ctx.state[4] = 0xC3D2E1F0;
        ctx.count = 0;
        micant::shared::boundedMemorySet(ctx.buffer, 0, sizeof(ctx.buffer));
    }

    static void update(Context& ctx, std::span<const uint8_t> data) noexcept {
        size_t index = static_cast<size_t>(ctx.count % BLOCK_SIZE);
        ctx.count += data.size();
        size_t partLen = BLOCK_SIZE - index;
        size_t i = 0;

        if (data.size() >= partLen) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], data.data(), partLen);
            transform(ctx.state, ctx.buffer);
            for (i = partLen; i + BLOCK_SIZE <= data.size(); i += BLOCK_SIZE) {
                transform(ctx.state, &data[i]);
            }
            index = 0;
        }
        if (i < data.size()) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], &data[i], data.size() - i);
        }
    }

    static void finish(Context& ctx, std::span<uint8_t, DIGEST_SIZE> digest) noexcept {
        uint8_t finalCount[8]{};
        uint64_t bitCount = ctx.count * 8;
        for (int i = 0; i < 8; ++i) {
            finalCount[i] = static_cast<uint8_t>((bitCount >> ((7 - i) * 8)) & 0xFF);
        }

        size_t index = static_cast<size_t>(ctx.count % BLOCK_SIZE);
        size_t padLen = (index < 56) ? (56 - index) : (120 - index);
        static const uint8_t PADDING[64] = { 0x80 };
        update(ctx, std::span<const uint8_t>(PADDING, padLen));
        update(ctx, std::span<const uint8_t>(finalCount, 8));

        for (size_t i = 0; i < 5; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8)  & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xFF);
        }
    }

    static std::array<uint8_t, DIGEST_SIZE> hash(std::span<const uint8_t> data) noexcept {
        Context ctx;
        init(ctx);
        update(ctx, data);
        std::array<uint8_t, DIGEST_SIZE> digest{};
        finish(ctx, digest);
        return digest;
    }

private:
    static inline uint32_t rol(uint32_t val, int bits) noexcept {
        return (val << bits) | (val >> (32 - bits));
    }

    static void transform(uint32_t state[5], const uint8_t block[64]) noexcept {
        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
};

// ============================================================================
// 2. Clean-Room SHA-256 (FIPS 180-4) Implementation
// ============================================================================

class Sha256 {
public:
    static constexpr size_t DIGEST_SIZE = 32;
    static constexpr size_t BLOCK_SIZE  = 64;

    struct Context {
        uint32_t state[8];
        uint64_t count;
        uint8_t  buffer[64];
    };

    static void init(Context& ctx) noexcept {
        ctx.state[0] = 0x6a09e667;
        ctx.state[1] = 0xbb67ae85;
        ctx.state[2] = 0x3c6ef372;
        ctx.state[3] = 0xa54ff53a;
        ctx.state[4] = 0x510e527f;
        ctx.state[5] = 0x9b05688c;
        ctx.state[6] = 0x1f83d9ab;
        ctx.state[7] = 0x5be0cd19;
        ctx.count    = 0;
    }

    static void update(Context& ctx, std::span<const uint8_t> data) noexcept {
        size_t index = static_cast<size_t>(ctx.count & 63);
        ctx.count += data.size();

        size_t partLen = 64 - index;
        size_t i = 0;

        if (data.size() >= partLen) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], data.data(), partLen);
            transform(ctx.state, ctx.buffer);
            for (i = partLen; i + 63 < data.size(); i += 64) {
                transform(ctx.state, &data[i]);
            }
            index = 0;
        }

        if (i < data.size()) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], &data[i], data.size() - i);
        }
    }

    static void final(Context& ctx, std::span<uint8_t, 32> digest) noexcept {
        uint64_t totalBits = ctx.count * 8;
        uint8_t bits[8];
        for (int i = 0; i < 8; ++i) {
            bits[7 - i] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
        }

        size_t index = static_cast<size_t>(ctx.count & 63);
        size_t padLen = (index < 56) ? (56 - index) : (120 - index);

        static const uint8_t PADDING[64] = { 0x80 };
        update(ctx, std::span<const uint8_t>(PADDING, padLen));
        update(ctx, std::span<const uint8_t>(bits, 8));

        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xFF);
        }
    }

    static std::array<uint8_t, 32> hash(std::span<const uint8_t> data) noexcept {
        Context ctx;
        init(ctx);
        update(ctx, data);
        std::array<uint8_t, 32> out{};
        final(ctx, std::span<uint8_t, 32>(out.data(), 32));
        return out;
    }

private:
    static constexpr uint32_t rotr(uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }
    static constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) noexcept { return (x & y) ^ (~x & z); }
    static constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) noexcept { return (x & y) ^ (x & z) ^ (y & z); }
    static constexpr uint32_t sigma0(uint32_t x) noexcept { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    static constexpr uint32_t sigma1(uint32_t x) noexcept { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    static constexpr uint32_t gamma0(uint32_t x) noexcept { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    static constexpr uint32_t gamma1(uint32_t x) noexcept { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

    static void transform(uint32_t state[8], const uint8_t block[64]) noexcept {
        static constexpr uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = sigma0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
};

// ============================================================================
// 2. Clean-Room SHA-384 (FIPS 180-4) Implementation
// ============================================================================

class Sha384 {
public:
    static constexpr size_t DIGEST_SIZE = 48;
    static constexpr size_t BLOCK_SIZE  = 128;

    struct Context {
        uint64_t state[8];
        uint64_t count;
        uint8_t  buffer[128];
    };

    static void init(Context& ctx) noexcept {
        ctx.state[0] = 0xcbbb9d5dc1059ed8ULL;
        ctx.state[1] = 0x629a292a367cd507ULL;
        ctx.state[2] = 0x9159015a3070dd17ULL;
        ctx.state[3] = 0x152fecd8f70e5939ULL;
        ctx.state[4] = 0x67332667ffc00b31ULL;
        ctx.state[5] = 0x8eb44a8768581511ULL;
        ctx.state[6] = 0xdb0c2e0d64f98fa7ULL;
        ctx.state[7] = 0x47b5481dbefa4fa4ULL;
        ctx.count    = 0;
    }

    static void update(Context& ctx, std::span<const uint8_t> data) noexcept {
        size_t index = static_cast<size_t>(ctx.count & 127);
        ctx.count += data.size();

        size_t partLen = 128 - index;
        size_t i = 0;

        if (data.size() >= partLen) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], data.data(), partLen);
            transform(ctx.state, ctx.buffer);
            for (i = partLen; i + 127 < data.size(); i += 128) {
                transform(ctx.state, &data[i]);
            }
            index = 0;
        }

        if (i < data.size()) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], &data[i], data.size() - i);
        }
    }

    static void final(Context& ctx, std::span<uint8_t, 48> digest) noexcept {
        uint64_t totalBits = ctx.count * 8;
        uint8_t bits[16] = {0};
        for (int i = 0; i < 8; ++i) {
            bits[15 - i] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
        }

        size_t index = static_cast<size_t>(ctx.count & 127);
        size_t padLen = (index < 112) ? (112 - index) : (240 - index);

        static const uint8_t PADDING[128] = { 0x80 };
        update(ctx, std::span<const uint8_t>(PADDING, padLen));
        update(ctx, std::span<const uint8_t>(bits, 16));

        for (int i = 0; i < 6; ++i) {
            for (int b = 0; b < 8; ++b) {
                digest[i * 8 + b] = static_cast<uint8_t>((ctx.state[i] >> ((7 - b) * 8)) & 0xFF);
            }
        }
    }

    static std::array<uint8_t, 48> hash(std::span<const uint8_t> data) noexcept {
        Context ctx;
        init(ctx);
        update(ctx, data);
        std::array<uint8_t, 48> out{};
        final(ctx, std::span<uint8_t, 48>(out.data(), 48));
        return out;
    }

private:
    static constexpr uint64_t rotr(uint64_t x, int n) noexcept { return (x >> n) | (x << (64 - n)); }
    static constexpr uint64_t ch(uint64_t x, uint64_t y, uint64_t z) noexcept { return (x & y) ^ (~x & z); }
    static constexpr uint64_t maj(uint64_t x, uint64_t y, uint64_t z) noexcept { return (x & y) ^ (x & z) ^ (y & z); }
    static constexpr uint64_t sigma0(uint64_t x) noexcept { return rotr(x, 28) ^ rotr(x, 34) ^ rotr(x, 39); }
    static constexpr uint64_t sigma1(uint64_t x) noexcept { return rotr(x, 14) ^ rotr(x, 18) ^ rotr(x, 41); }
    static constexpr uint64_t gamma0(uint64_t x) noexcept { return rotr(x, 1) ^ rotr(x, 8) ^ (x >> 7); }
    static constexpr uint64_t gamma1(uint64_t x) noexcept { return rotr(x, 19) ^ rotr(x, 61) ^ (x >> 6); }

    static void transform(uint64_t state[8], const uint8_t block[128]) noexcept {
        static constexpr uint64_t K[80] = {
            0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
            0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
            0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
            0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
            0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
            0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
            0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
            0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
            0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
            0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
            0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
            0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
            0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
            0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
            0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
            0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
            0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
            0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
            0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
            0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
        };

        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int b = 0; b < 8; ++b) {
                w[i] = (w[i] << 8) | block[i * 8 + b];
            }
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint64_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 80; ++i) {
            uint64_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint64_t t2 = sigma0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
};

// ============================================================================
// ============================================================================
// 2b. Clean-Room SHA-512 (FIPS 180-4) Implementation
// ============================================================================

class Sha512 {
public:
    static constexpr size_t DIGEST_SIZE = 64;
    static constexpr size_t BLOCK_SIZE  = 128;

    struct Context {
        uint64_t state[8];
        uint64_t count;
        uint8_t  buffer[128];
    };

    static void init(Context& ctx) noexcept {
        ctx.state[0] = 0x6a09e667f3bcc908ULL;
        ctx.state[1] = 0xbb67ae8584caa73bULL;
        ctx.state[2] = 0x3c6ef372fe94f82bULL;
        ctx.state[3] = 0xa54ff53a5f1d36f1ULL;
        ctx.state[4] = 0x510e527fade682d1ULL;
        ctx.state[5] = 0x9b05688c2b3e6c1fULL;
        ctx.state[6] = 0x1f83d9abfb41bd6bULL;
        ctx.state[7] = 0x5be0cd19137e2179ULL;
        ctx.count    = 0;
    }

    static void update(Context& ctx, std::span<const uint8_t> data) noexcept {
        size_t index = static_cast<size_t>(ctx.count & 127);
        ctx.count += data.size();

        size_t partLen = 128 - index;
        size_t i = 0;

        if (data.size() >= partLen) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], data.data(), partLen);
            transform(ctx.state, ctx.buffer);
            for (i = partLen; i + 127 < data.size(); i += 128) {
                transform(ctx.state, &data[i]);
            }
            index = 0;
        }

        if (i < data.size()) {
            micant::shared::boundedMemoryCopy(&ctx.buffer[index], &data[i], data.size() - i);
        }
    }

    static void final(Context& ctx, std::span<uint8_t, 64> digest) noexcept {
        uint64_t totalBits = ctx.count * 8;
        uint8_t bits[16] = {0};
        for (int i = 0; i < 8; ++i) {
            bits[15 - i] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
        }

        size_t index = static_cast<size_t>(ctx.count & 127);
        size_t padLen = (index < 112) ? (112 - index) : (240 - index);

        static const uint8_t PADDING[128] = { 0x80 };
        update(ctx, std::span<const uint8_t>(PADDING, padLen));
        update(ctx, std::span<const uint8_t>(bits, 16));

        for (int i = 0; i < 8; ++i) {
            for (int b = 0; b < 8; ++b) {
                digest[i * 8 + b] = static_cast<uint8_t>((ctx.state[i] >> ((7 - b) * 8)) & 0xFF);
            }
        }
    }

    static std::array<uint8_t, 64> hash(std::span<const uint8_t> data) noexcept {
        Context ctx;
        init(ctx);
        update(ctx, data);
        std::array<uint8_t, 64> out{};
        final(ctx, std::span<uint8_t, 64>(out.data(), 64));
        return out;
    }

private:
    static constexpr uint64_t rotr(uint64_t x, int n) noexcept { return (x >> n) | (x << (64 - n)); }
    static constexpr uint64_t ch(uint64_t x, uint64_t y, uint64_t z) noexcept { return (x & y) ^ (~x & z); }
    static constexpr uint64_t maj(uint64_t x, uint64_t y, uint64_t z) noexcept { return (x & y) ^ (x & z) ^ (y & z); }
    static constexpr uint64_t sigma0(uint64_t x) noexcept { return rotr(x, 28) ^ rotr(x, 34) ^ rotr(x, 39); }
    static constexpr uint64_t sigma1(uint64_t x) noexcept { return rotr(x, 14) ^ rotr(x, 18) ^ rotr(x, 41); }
    static constexpr uint64_t gamma0(uint64_t x) noexcept { return rotr(x, 1) ^ rotr(x, 8) ^ (x >> 7); }
    static constexpr uint64_t gamma1(uint64_t x) noexcept { return rotr(x, 19) ^ rotr(x, 61) ^ (x >> 6); }

    static void transform(uint64_t state[8], const uint8_t block[128]) noexcept {
        static constexpr uint64_t K[80] = {
            0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
            0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
            0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
            0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
            0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
            0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
            0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
            0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
            0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
            0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
            0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
            0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
            0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
            0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
            0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
            0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
            0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
            0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
            0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
            0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
        };

        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int b = 0; b < 8; ++b) {
                w[i] = (w[i] << 8) | block[i * 8 + b];
            }
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint64_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 80; ++i) {
            uint64_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint64_t t2 = sigma0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
};

// Bounded RSA public-key verification for X.509 and TLS 1.3. Supports
// RSA-2048 through RSA-4096 with exponent 65537, SHA-256 PKCS#1 v1.5
// certificate signatures, and SHA-256 RSA-PSS TLS CertificateVerify.
class RsaSha256Verifier {
    static constexpr size_t MaxLimbs = 128;
    struct Number { std::array<uint32_t, MaxLimbs> limbs{}; };

public:
    static bool verifyPkcs1(std::span<const uint8_t> publicKeyDer,
                            std::span<const uint8_t> message,
                            std::span<const uint8_t> signature) {
        Number modulus{}, encoded{};
        size_t limbCount{}, keyBytes{}, modulusBits{};
        uint32_t exponent{};
        if (!parsePublicKey(publicKeyDer, modulus, limbCount, keyBytes, modulusBits, exponent) ||
            signature.size() != keyBytes || !toNumber(signature, limbCount, encoded) ||
            compare(encoded, modulus, limbCount) >= 0) return false;
        Number decoded{};
        if (!modularExponent(encoded, exponent, modulus, limbCount, decoded)) return false;
        std::array<uint8_t, 512> em{};
        fromNumber(decoded, limbCount, std::span<uint8_t>(em.data(), keyBytes));
        static constexpr uint8_t sha256DigestInfoPrefix[] = {
            0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
        };
        const auto digest = Sha256::hash(message);
        constexpr size_t tail = sizeof(sha256DigestInfoPrefix) + 32;
        if (keyBytes < tail + 11 || em[0] != 0 || em[1] != 1) return false;
        size_t separator = 2;
        while (separator < keyBytes && em[separator] == 0xff) ++separator;
        if (separator < 10 || separator >= keyBytes || em[separator++] != 0 || keyBytes - separator != tail)
            return false;
        if (micant::shared::boundedMemoryCompare(em.data() + separator, sha256DigestInfoPrefix, sizeof(sha256DigestInfoPrefix)) != 0)
            return false;
        return equalConstantTime(std::span<const uint8_t>(em.data() + separator + sizeof(sha256DigestInfoPrefix), 32),
                                 digest);
    }

    static bool verifyPss(std::span<const uint8_t> publicKeyDer,
                          std::span<const uint8_t> message,
                          std::span<const uint8_t> signature) {
        Number modulus{}, encoded{};
        size_t limbCount{}, keyBytes{}, modulusBits{};
        uint32_t exponent{};
        if (!parsePublicKey(publicKeyDer, modulus, limbCount, keyBytes, modulusBits, exponent) ||
            signature.size() != keyBytes || !toNumber(signature, limbCount, encoded) ||
            compare(encoded, modulus, limbCount) >= 0) return false;
        Number decoded{};
        if (!modularExponent(encoded, exponent, modulus, limbCount, decoded)) return false;
        const size_t emBits = modulusBits - 1;
        const size_t emLen = (emBits + 7) / 8;
        constexpr size_t hashBytes = 32;
        if (emLen < hashBytes * 2 + 2 || emLen > keyBytes) return false;
        std::array<uint8_t, 512> encodedMessage{};
        fromNumber(decoded, limbCount, std::span<uint8_t>(encodedMessage.data(), keyBytes));
        const size_t leadingBytes = keyBytes - emLen;
        for (size_t i = 0; i < leadingBytes; ++i) if (encodedMessage[i] != 0) return false;
        const auto em = std::span<const uint8_t>(encodedMessage.data() + leadingBytes, emLen);
        if (em.back() != 0xbc) return false;
        const size_t dbLength = emLen - hashBytes - 1;
        const auto maskedDb = em.first(dbLength);
        const auto h = em.subspan(dbLength, hashBytes);
        const unsigned unusedBits = static_cast<unsigned>(8 * emLen - emBits);
        if (unusedBits && (maskedDb[0] >> (8 - unusedBits)) != 0) return false;
        std::array<uint8_t, 512> db{};
        if (!mgf1(h, std::span<uint8_t>(db.data(), dbLength))) return false;
        for (size_t i = 0; i < dbLength; ++i) db[i] ^= maskedDb[i];
        if (unusedBits) db[0] &= static_cast<uint8_t>(0xff >> unusedBits);
        const size_t saltOffset = dbLength - hashBytes - 1;
        for (size_t i = 0; i < saltOffset; ++i) if (db[i] != 0) return false;
        if (db[saltOffset] != 1) return false;
        const auto messageHash = Sha256::hash(message);
        std::array<uint8_t, 8 + hashBytes * 2> hashInput{};
        micant::shared::boundedMemoryCopy(hashInput.data() + 8, messageHash.data(), hashBytes);
        micant::shared::boundedMemoryCopy(hashInput.data() + 8 + hashBytes, db.data() + saltOffset + 1, hashBytes);
        const auto expected = Sha256::hash(hashInput);
        return equalConstantTime(h, expected);
    }

private:
    static bool readDer(std::span<const uint8_t> in, size_t& cursor, uint8_t tag,
                        std::span<const uint8_t>& value) noexcept {
        if (cursor > in.size() || in.size() - cursor < 2 || in[cursor++] != tag) return false;
        const uint8_t first = in[cursor++];
        size_t length = 0;
        if ((first & 0x80) == 0) length = first;
        else {
            const size_t n = first & 0x7f;
            if (!n || n > sizeof(size_t) || n > in.size() - cursor || in[cursor] == 0) return false;
            for (size_t i = 0; i < n; ++i) {
                if (length > (SIZE_MAX - in[cursor]) / 256) return false;
                length = length * 256 + in[cursor++];
            }
            if (length < 128) return false;
        }
        if (length > in.size() - cursor) return false;
        value = in.subspan(cursor, length);
        cursor += length;
        return true;
    }

    static bool parsePositiveInteger(std::span<const uint8_t> bytes, std::span<const uint8_t>& out) noexcept {
        if (bytes.empty() || (bytes[0] & 0x80) ||
            (bytes.size() > 1 && bytes[0] == 0 && !(bytes[1] & 0x80))) return false;
        if (bytes[0] == 0) bytes = bytes.subspan(1);
        if (bytes.empty()) return false;
        out = bytes;
        return true;
    }

    static bool parsePublicKey(std::span<const uint8_t> der, Number& modulus,
        size_t& limbCount, size_t& keyBytes, size_t& modulusBits, uint32_t& exponent) noexcept {
        size_t cursor = 0;
        std::span<const uint8_t> sequence{};
        if (!readDer(der, cursor, 0x30, sequence) || cursor != der.size()) return false;
        size_t field = 0;
        std::span<const uint8_t> modulusInteger{}, exponentInteger{}, modulusBytes{}, exponentBytes{};
        if (!readDer(sequence, field, 0x02, modulusInteger) || !parsePositiveInteger(modulusInteger, modulusBytes) ||
            !readDer(sequence, field, 0x02, exponentInteger) || !parsePositiveInteger(exponentInteger, exponentBytes) ||
            field != sequence.size() || modulusBytes.size() < 256 || modulusBytes.size() > 512 ||
            exponentBytes.size() > 4) return false;
        exponent = 0;
        for (uint8_t byte : exponentBytes) exponent = (exponent << 8) | byte;
        // This implementation deliberately supports the common bounded
        // exponent only; other exponents are rejected rather than delegated to
        // an unbounded big-number path.
        if (exponent != 65537 || (modulusBytes.back() & 1) == 0) return false;
        keyBytes = modulusBytes.size();
        limbCount = (keyBytes + 3) / 4;
        if (limbCount > MaxLimbs) return false;
        modulusBits = 0;
        uint8_t top = modulusBytes.front();
        while (top) { ++modulusBits; top >>= 1; }
        modulusBits += (keyBytes - 1) * 8;
        if (modulusBits < 2048) return false;
        return toNumber(modulusBytes, limbCount, modulus);
    }

    static bool toNumber(std::span<const uint8_t> bytes, size_t limbCount, Number& out) noexcept {
        if (bytes.size() > limbCount * 4) return false;
        out = {};
        for (size_t i = 0; i < bytes.size(); ++i) {
            const size_t reverse = bytes.size() - 1 - i;
            out.limbs[i / 4] |= static_cast<uint32_t>(bytes[reverse]) << ((i % 4) * 8);
        }
        return true;
    }

    static void fromNumber(const Number& value, size_t limbCount, std::span<uint8_t> out) noexcept {
        for (size_t i = 0; i < out.size(); ++i) {
            const size_t reverse = out.size() - 1 - i;
            out[reverse] = static_cast<uint8_t>(value.limbs[i / 4] >> ((i % 4) * 8));
        }
        (void)limbCount;
    }

    static int compare(const Number& a, const Number& b, size_t limbs) noexcept {
        for (size_t i = limbs; i > 0; --i) {
            if (a.limbs[i - 1] < b.limbs[i - 1]) return -1;
            if (a.limbs[i - 1] > b.limbs[i - 1]) return 1;
        }
        return 0;
    }

    static Number addMod(const Number& a, const Number& b, const Number& modulus, size_t limbs) noexcept {
        Number result{};
        uint64_t carry = 0;
        for (size_t i = 0; i < limbs; ++i) {
            const uint64_t sum = static_cast<uint64_t>(a.limbs[i]) + b.limbs[i] + carry;
            result.limbs[i] = static_cast<uint32_t>(sum);
            carry = sum >> 32;
        }
        if (carry || compare(result, modulus, limbs) >= 0) {
            uint64_t borrow = 0;
            for (size_t i = 0; i < limbs; ++i) {
                const uint64_t sub = static_cast<uint64_t>(modulus.limbs[i]) + borrow;
                const uint64_t current = result.limbs[i];
                result.limbs[i] = static_cast<uint32_t>(current - sub);
                borrow = current < sub;
            }
        }
        return result;
    }

    static Number multiplyMod(const Number& a, const Number& b, const Number& modulus, size_t limbs) noexcept {
        Number result{};
        for (size_t bit = limbs * 32; bit > 0; --bit) {
            result = addMod(result, result, modulus, limbs);
            const size_t index = bit - 1;
            if ((b.limbs[index / 32] >> (index % 32)) & 1)
                result = addMod(result, a, modulus, limbs);
        }
        return result;
    }

    static bool modularExponent(const Number& base, uint32_t exponent, const Number& modulus,
                                size_t limbs, Number& output) noexcept {
        if (!exponent || !limbs) return false;
        Number result{};
        result.limbs[0] = 1;
        size_t highestBit = 31;
        while (highestBit && ((exponent >> highestBit) & 1) == 0) --highestBit;
        for (size_t bit = highestBit + 1; bit > 0; --bit) {
            result = multiplyMod(result, result, modulus, limbs);
            if ((exponent >> (bit - 1)) & 1) result = multiplyMod(result, base, modulus, limbs);
        }
        output = result;
        return true;
    }

    static bool mgf1(std::span<const uint8_t> seed, std::span<uint8_t> mask) {
        std::array<uint8_t, 4> counter{};
        size_t offset = 0;
        uint32_t value = 0;
        while (offset < mask.size()) {
            counter[0] = static_cast<uint8_t>(value >> 24);
            counter[1] = static_cast<uint8_t>(value >> 16);
            counter[2] = static_cast<uint8_t>(value >> 8);
            counter[3] = static_cast<uint8_t>(value);
            micant::shared::BoundedVector<uint8_t> input(seed.begin(), seed.end());
            input.insert(input.end(), counter.begin(), counter.end());
            if (!input.good()) return false;
            const auto digest = Sha256::hash(input);
            const size_t count = std::min(digest.size(), mask.size() - offset);
            micant::shared::boundedMemoryCopy(mask.data() + offset, digest.data(), count);
            offset += count;
            if (value == UINT32_MAX && offset < mask.size()) return false;
            ++value;
        }
        return true;
    }

    static bool equalConstantTime(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
        if (a.size() != b.size()) return false;
        uint8_t difference = 0;
        for (size_t i = 0; i < a.size(); ++i) difference |= a[i] ^ b[i];
        return difference == 0;
    }
};

// 3. Clean-Room HMAC & HKDF (RFC 2104 / RFC 5869 / RFC 8446)
// ============================================================================

class HmacSha256 {
public:
    static constexpr size_t DIGEST_SIZE = 32;

    static std::array<uint8_t, 32> compute(std::span<const uint8_t> key, std::span<const uint8_t> data) noexcept {
        uint8_t k[64] = {0};
        if (key.size() > 64) {
            auto h = Sha256::hash(key);
            micant::shared::boundedMemoryCopy(k, h.data(), 32);
        } else {
            micant::shared::boundedMemoryCopy(k, key.data(), key.size());
        }

        uint8_t k_ipad[64];
        uint8_t k_opad[64];
        for (int i = 0; i < 64; ++i) {
            k_ipad[i] = k[i] ^ 0x36;
            k_opad[i] = k[i] ^ 0x5C;
        }

        Sha256::Context innerCtx;
        Sha256::init(innerCtx);
        Sha256::update(innerCtx, std::span<const uint8_t>(k_ipad, 64));
        Sha256::update(innerCtx, data);
        std::array<uint8_t, 32> innerHash{};
        Sha256::final(innerCtx, std::span<uint8_t, 32>(innerHash.data(), 32));

        Sha256::Context outerCtx;
        Sha256::init(outerCtx);
        Sha256::update(outerCtx, std::span<const uint8_t>(k_opad, 64));
        Sha256::update(outerCtx, std::span<const uint8_t>(innerHash.data(), 32));
        std::array<uint8_t, 32> outerHash{};
        Sha256::final(outerCtx, std::span<uint8_t, 32>(outerHash.data(), 32));
        auto result = outerHash;
        auto secureClear = [](void* memory, size_t size) noexcept {
            volatile uint8_t* p = static_cast<volatile uint8_t*>(memory);
            while (size--) *p++ = 0;
        };
        secureClear(k, sizeof(k));
        secureClear(k_ipad, sizeof(k_ipad));
        secureClear(k_opad, sizeof(k_opad));
        secureClear(innerHash.data(), innerHash.size());
        secureClear(outerHash.data(), outerHash.size());
        secureClear(&innerCtx, sizeof(innerCtx));
        secureClear(&outerCtx, sizeof(outerCtx));
        return result;
    }
};

// PBKDF2-HMAC-SHA-256 (RFC 8018). Bounds keep untrusted record parameters
// from turning account verification into an unbounded CPU/allocation request.
class Pbkdf2Sha256 {
public:
    static constexpr uint32_t MIN_ITERATIONS = 100'000;
    static constexpr uint32_t MAX_ITERATIONS = 2'000'000;
    static constexpr size_t MAX_OUTPUT_BYTES = 1024;

    static bool derive(std::span<const uint8_t> password,
                       std::span<const uint8_t> salt,
                       uint32_t iterations,
                       std::span<uint8_t> output) noexcept {
        if (password.empty() || salt.empty() || salt.size() > 64 || iterations < MIN_ITERATIONS ||
            iterations > MAX_ITERATIONS || output.empty() || output.size() > MAX_OUTPUT_BYTES ||
            output.size() > static_cast<size_t>(UINT32_MAX) * HmacSha256::DIGEST_SIZE) return false;

        std::array<uint8_t, 68> saltBlock{};
        std::array<uint8_t, 32> u{};
        std::array<uint8_t, 32> accumulator{};
        size_t offset = 0;
        uint32_t block = 1;
        while (offset < output.size()) {
            micant::shared::boundedMemoryCopy(saltBlock.data(), salt.data(), salt.size());
            const size_t blockOffset = salt.size();
            saltBlock[blockOffset] = static_cast<uint8_t>(block >> 24);
            saltBlock[blockOffset + 1] = static_cast<uint8_t>(block >> 16);
            saltBlock[blockOffset + 2] = static_cast<uint8_t>(block >> 8);
            saltBlock[blockOffset + 3] = static_cast<uint8_t>(block);
            u = HmacSha256::compute(password, std::span<const uint8_t>(saltBlock.data(), blockOffset + 4));
            accumulator = u;
            for (uint32_t iteration = 1; iteration < iterations; ++iteration) {
                u = HmacSha256::compute(password, u);
                for (size_t i = 0; i < accumulator.size(); ++i) accumulator[i] ^= u[i];
            }
            const size_t count = std::min(accumulator.size(), output.size() - offset);
            micant::shared::boundedMemoryCopy(output.data() + offset, accumulator.data(), count);
            offset += count;
            if (block == UINT32_MAX && offset < output.size()) return false;
            ++block;
        }
        volatile uint8_t* wipe = u.data();
        for (size_t i = 0; i < u.size(); ++i) wipe[i] = 0;
        wipe = accumulator.data();
        for (size_t i = 0; i < accumulator.size(); ++i) wipe[i] = 0;
        wipe = saltBlock.data();
        for (size_t i = 0; i < saltBlock.size(); ++i) wipe[i] = 0;
        return true;
    }
};

class HkdfSha256 {
public:
    static std::array<uint8_t, 32> extract(std::span<const uint8_t> salt, std::span<const uint8_t> ikm) noexcept {
        if (salt.empty()) {
            static const uint8_t zeroSalt[32] = {0};
            return HmacSha256::compute(std::span<const uint8_t>(zeroSalt, 32), ikm);
        }
        return HmacSha256::compute(salt, ikm);
    }

    static micant::shared::BoundedVector<uint8_t> expand(std::span<const uint8_t> prk, std::span<const uint8_t> info, size_t length) {
        if (length > 255u * 32u) return {};
        micant::shared::BoundedVector<uint8_t> okm;
        if (!okm.reserve(length)) return {};

        std::array<uint8_t, 32> t{};
        size_t tLen = 0;
        uint8_t counter = 1;

        while (okm.size() < length) {
            micant::shared::BoundedVector<uint8_t> stepData;
            if (tLen > 0) {
                stepData.insert(stepData.end(), t.begin(), t.end());
            }
            stepData.insert(stepData.end(), info.begin(), info.end());
            stepData.push_back(counter);
            if (!stepData.good()) return {};

            t = HmacSha256::compute(prk, stepData);
            tLen = 32;

            size_t take = std::min(static_cast<size_t>(32), length - okm.size());
            okm.insert(okm.end(), t.begin(), t.begin() + take);
            if (!okm.good()) return {};
            counter++;
        }

        return okm;
    }

    // TLS 1.3 HKDF-Expand-Label (RFC 8446 Section 7.1)
    static micant::shared::BoundedVector<uint8_t> expandLabel(
        std::span<const uint8_t> secret,
        std::string_view label,
        std::span<const uint8_t> context,
        uint16_t length)
    {
        // HkdfLabel uses one-byte lengths, including the six-byte prefix.
        if (label.size() > 249 || context.size() > 255) return {};
        micant::shared::AnsiString fullLabel = "tls13 ";
        fullLabel.append(label);

        micant::shared::BoundedVector<uint8_t> hkdfLabel;
        hkdfLabel.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
        hkdfLabel.push_back(static_cast<uint8_t>(length & 0xFF));
        hkdfLabel.push_back(static_cast<uint8_t>(fullLabel.size()));
        hkdfLabel.insert(hkdfLabel.end(), fullLabel.begin(), fullLabel.end());
        hkdfLabel.push_back(static_cast<uint8_t>(context.size()));
        hkdfLabel.insert(hkdfLabel.end(), context.begin(), context.end());
        if (!hkdfLabel.good()) return {};

        return expand(secret, hkdfLabel, length);
    }
};

// ============================================================================
// 4. Clean-Room AES (Rijndael) & GCM Mode (NIST SP 800-38D / RFC 5116)
// ============================================================================

class AesCore {
public:
    static constexpr uint8_t sbox[256] = {
        0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
        0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
        0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
        0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
        0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
        0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
        0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
        0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
        0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
        0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
        0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
        0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
        0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
        0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
        0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
        0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
    };

    static constexpr uint8_t rcon[11] = {
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
    };

    static constexpr uint8_t xtime(uint8_t x) noexcept {
        return static_cast<uint8_t>((x << 1) ^ (((x >> 7) & 1) * 0x1b));
    }

    struct KeySchedule {
        uint32_t roundKeys[60]; // up to 14 rounds for AES-256
        int numRounds{10};      // 10 for 128, 14 for 256
    };

    static void expandKey(std::span<const uint8_t> key, KeySchedule& ks) noexcept {
        size_t nk = key.size() / 4;
        ks.numRounds = (nk == 4) ? 10 : 14;

        for (size_t i = 0; i < nk; ++i) {
            ks.roundKeys[i] = (static_cast<uint32_t>(key[4 * i]) << 24) |
                              (static_cast<uint32_t>(key[4 * i + 1]) << 16) |
                              (static_cast<uint32_t>(key[4 * i + 2]) << 8) |
                              (static_cast<uint32_t>(key[4 * i + 3]));
        }

        size_t totalWords = 4 * (static_cast<size_t>(ks.numRounds) + 1);
        for (size_t i = nk; i < totalWords; ++i) {
            uint32_t temp = ks.roundKeys[i - 1];
            if (i % nk == 0) {
                temp = subWord(rotWord(temp)) ^ (static_cast<uint32_t>(rcon[i / nk]) << 24);
            } else if (nk > 6 && (i % nk == 4)) {
                temp = subWord(temp);
            }
            ks.roundKeys[i] = ks.roundKeys[i - nk] ^ temp;
        }
    }

    static void encryptBlock(const KeySchedule& ks, const uint8_t in[16], uint8_t out[16]) noexcept {
        uint8_t state[4][4];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                state[r][c] = in[r + 4 * c];
            }
        }

        addRoundKey(state, ks.roundKeys, 0);

        for (int round = 1; round < ks.numRounds; ++round) {
            subBytes(state);
            shiftRows(state);
            mixColumns(state);
            addRoundKey(state, ks.roundKeys, round);
        }

        subBytes(state);
        shiftRows(state);
        addRoundKey(state, ks.roundKeys, ks.numRounds);

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out[r + 4 * c] = state[r][c];
            }
        }
    }

private:
    // Compute the AES substitution in GF(2^8) without a secret-indexed
    // lookup table. The multiplication and exponentiation have fixed work
    // for every input, including zero.
    static constexpr uint8_t gfMultiply(uint8_t left, uint8_t right) noexcept {
        uint8_t result = 0;
        for (unsigned i = 0; i < 8; ++i) {
            const uint8_t mask = static_cast<uint8_t>(0u - (right & 1u));
            result ^= static_cast<uint8_t>(left & mask);
            left = xtime(left);
            right = static_cast<uint8_t>(right >> 1);
        }
        return result;
    }
    static constexpr uint8_t substitute(uint8_t input) noexcept {
        const uint8_t p2 = gfMultiply(input, input);
        const uint8_t p4 = gfMultiply(p2, p2);
        const uint8_t p8 = gfMultiply(p4, p4);
        const uint8_t p16 = gfMultiply(p8, p8);
        const uint8_t p32 = gfMultiply(p16, p16);
        const uint8_t p64 = gfMultiply(p32, p32);
        const uint8_t p128 = gfMultiply(p64, p64);
        uint8_t inverse = gfMultiply(p2, p4);
        inverse = gfMultiply(inverse, p8);
        inverse = gfMultiply(inverse, p16);
        inverse = gfMultiply(inverse, p32);
        inverse = gfMultiply(inverse, p64);
        inverse = gfMultiply(inverse, p128);
        const auto rotate = [](uint8_t x, unsigned amount) constexpr noexcept {
            return static_cast<uint8_t>((static_cast<unsigned>(x) << amount) |
                                        (static_cast<unsigned>(x) >> (8 - amount)));
        };
        return static_cast<uint8_t>(inverse ^ rotate(inverse, 1) ^ rotate(inverse, 2) ^
                                    rotate(inverse, 3) ^ rotate(inverse, 4) ^ 0x63u);
    }
    static constexpr uint32_t rotWord(uint32_t w) noexcept {
        return (w << 8) | (w >> 24);
    }

    static constexpr uint32_t subWord(uint32_t w) noexcept {
        return (static_cast<uint32_t>(substitute(static_cast<uint8_t>(w >> 24))) << 24) |
               (static_cast<uint32_t>(substitute(static_cast<uint8_t>(w >> 16))) << 16) |
               (static_cast<uint32_t>(substitute(static_cast<uint8_t>(w >> 8))) << 8) |
               static_cast<uint32_t>(substitute(static_cast<uint8_t>(w)));
    }

    static void subBytes(uint8_t state[4][4]) noexcept {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                state[r][c] = substitute(state[r][c]);
            }
        }
    }

    static void shiftRows(uint8_t state[4][4]) noexcept {
        uint8_t t;
        // row 1: shift left by 1
        t = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = t;
        // row 2: shift left by 2
        t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t;
        t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;
        // row 3: shift left by 3
        t = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = t;
    }

    static void mixColumns(uint8_t state[4][4]) noexcept {
        for (int c = 0; c < 4; ++c) {
            uint8_t a0 = state[0][c], a1 = state[1][c], a2 = state[2][c], a3 = state[3][c];
            uint8_t h = a0 ^ a1 ^ a2 ^ a3;
            state[0][c] ^= h ^ xtime(a0 ^ a1);
            state[1][c] ^= h ^ xtime(a1 ^ a2);
            state[2][c] ^= h ^ xtime(a2 ^ a3);
            state[3][c] ^= h ^ xtime(a3 ^ a0);
        }
    }

    static void addRoundKey(uint8_t state[4][4], const uint32_t* roundKeys, int round) noexcept {
        for (int c = 0; c < 4; ++c) {
            uint32_t rk = roundKeys[round * 4 + c];
            state[0][c] ^= static_cast<uint8_t>((rk >> 24) & 0xFF);
            state[1][c] ^= static_cast<uint8_t>((rk >> 16) & 0xFF);
            state[2][c] ^= static_cast<uint8_t>((rk >> 8) & 0xFF);
            state[3][c] ^= static_cast<uint8_t>(rk & 0xFF);
        }
    }
};

// ============================================================================
// 5. Clean-Room Galois/Counter Mode (AES-GCM) & GHASH
// ============================================================================

class AesGcm {
public:
    // GF(2^128) multiplication with reduction polynomial x^128 + x^7 + x^2 + x + 1
    static void ghashMultiply(uint8_t x[16], const uint8_t y[16]) noexcept {
        uint8_t z[16] = {0};
        uint8_t v[16];
        micant::shared::boundedMemoryCopy(v, y, 16);

        for (int i = 0; i < 16; ++i) {
            for (int bit = 7; bit >= 0; --bit) {
                const uint8_t selected = static_cast<uint8_t>(0u - ((x[i] >> bit) & 1u));
                for (int j = 0; j < 16; ++j) z[j] ^= static_cast<uint8_t>(v[j] & selected);
                const uint8_t reduction = static_cast<uint8_t>(0u - (v[15] & 1u));
                // Shift v right by 1
                for (int j = 15; j > 0; --j) {
                    v[j] = (v[j] >> 1) | ((v[j - 1] & 1) << 7);
                }
                v[0] >>= 1;
                v[0] ^= static_cast<uint8_t>(0xE1u & reduction); // R = 0xE100...
            }
        }
        micant::shared::boundedMemoryCopy(x, z, 16);
    }

    static void computeGhash(
        const uint8_t h[16],
        std::span<const uint8_t> aad,
        std::span<const uint8_t> cipherText,
        uint8_t tagOut[16]) noexcept
    {
        uint8_t y[16] = {0};

        // Process AAD blocks
        size_t aadBlocks = (aad.size() + 15) / 16;
        for (size_t b = 0; b < aadBlocks; ++b) {
            size_t off = b * 16;
            size_t len = std::min(static_cast<size_t>(16), aad.size() - off);
            for (size_t i = 0; i < len; ++i) y[i] ^= aad[off + i];
            ghashMultiply(y, h);
        }

        // Process CipherText blocks
        size_t ctBlocks = (cipherText.size() + 15) / 16;
        for (size_t b = 0; b < ctBlocks; ++b) {
            size_t off = b * 16;
            size_t len = std::min(static_cast<size_t>(16), cipherText.size() - off);
            for (size_t i = 0; i < len; ++i) y[i] ^= cipherText[off + i];
            ghashMultiply(y, h);
        }

        // Process bit lengths block
        uint64_t aadBits = static_cast<uint64_t>(aad.size()) * 8;
        uint64_t ctBits  = static_cast<uint64_t>(cipherText.size()) * 8;
        uint8_t lenBlock[16] = {0};
        for (int i = 0; i < 8; ++i) {
            lenBlock[7 - i]  = static_cast<uint8_t>((aadBits >> (i * 8)) & 0xFF);
            lenBlock[15 - i] = static_cast<uint8_t>((ctBits >> (i * 8)) & 0xFF);
        }
        for (int i = 0; i < 16; ++i) y[i] ^= lenBlock[i];
        ghashMultiply(y, h);

        micant::shared::boundedMemoryCopy(tagOut, y, 16);
    }

    static bool encrypt(
        std::span<const uint8_t> key,
        std::span<const uint8_t> nonce, // 12 bytes
        std::span<const uint8_t> aad,
        std::span<const uint8_t> plaintext,
        micant::shared::BoundedVector<uint8_t>& ciphertext,
        std::array<uint8_t, 16>& tag)
    {
        if (nonce.size() != 12 || (key.size() != 16 && key.size() != 32)) {
            return false;
        }

        AesCore::KeySchedule ks;
        AesCore::expandKey(key, ks);

        // Subkey H = AES_K(0^128)
        uint8_t zero[16] = {0};
        uint8_t h[16] = {0};
        AesCore::encryptBlock(ks, zero, h);

        // Counter block J0 = Nonce || 0x00000001
        uint8_t j0[16] = {0};
        micant::shared::boundedMemoryCopy(j0, nonce.data(), 12);
        j0[15] = 1;

        // Encrypt J0 for tag mask
        uint8_t j0Enc[16] = {0};
        AesCore::encryptBlock(ks, j0, j0Enc);

        // CTR Mode Encryption
        if (!ciphertext.resize(plaintext.size()) || !ciphertext.good()) return false;
        uint8_t ctr[16];
        micant::shared::boundedMemoryCopy(ctr, j0, 16);

        size_t blocks = (plaintext.size() + 15) / 16;
        for (size_t b = 0; b < blocks; ++b) {
            // Increment 32-bit counter
            uint32_t val = (static_cast<uint32_t>(ctr[12]) << 24) |
                           (static_cast<uint32_t>(ctr[13]) << 16) |
                           (static_cast<uint32_t>(ctr[14]) << 8)  |
                           (static_cast<uint32_t>(ctr[15]));
            val++;
            ctr[12] = static_cast<uint8_t>((val >> 24) & 0xFF);
            ctr[13] = static_cast<uint8_t>((val >> 16) & 0xFF);
            ctr[14] = static_cast<uint8_t>((val >> 8) & 0xFF);
            ctr[15] = static_cast<uint8_t>(val & 0xFF);

            uint8_t sBlock[16];
            AesCore::encryptBlock(ks, ctr, sBlock);

            size_t off = b * 16;
            size_t len = std::min(static_cast<size_t>(16), plaintext.size() - off);
            for (size_t i = 0; i < len; ++i) {
                ciphertext[off + i] = plaintext[off + i] ^ sBlock[i];
            }
        }

        // Compute GHASH tag
        uint8_t ghashTag[16];
        computeGhash(h, aad, ciphertext, ghashTag);

        for (int i = 0; i < 16; ++i) {
            tag[i] = ghashTag[i] ^ j0Enc[i];
        }

        return true;
    }

#if !defined(MICANT_FREESTANDING)
    static bool encrypt(std::span<const uint8_t> key, std::span<const uint8_t> nonce,
                        std::span<const uint8_t> aad, std::span<const uint8_t> plaintext,
                        std::vector<uint8_t>& ciphertext, std::array<uint8_t, 16>& tag) {
        micant::shared::BoundedVector<uint8_t> bounded;
        if (!encrypt(key, nonce, aad, plaintext, bounded, tag)) return false;
        ciphertext.assign(bounded.begin(), bounded.end());
        return true;
    }
#endif

    static bool decrypt(
        std::span<const uint8_t> key,
        std::span<const uint8_t> nonce,
        std::span<const uint8_t> aad,
        std::span<const uint8_t> ciphertext,
        std::span<const uint8_t> tag,
        micant::shared::BoundedVector<uint8_t>& plaintext)
    {
        if (nonce.size() != 12 || tag.size() != 16 || (key.size() != 16 && key.size() != 32)) {
            return false;
        }

        AesCore::KeySchedule ks;
        AesCore::expandKey(key, ks);

        // Subkey H = AES_K(0^128)
        uint8_t zero[16] = {0};
        uint8_t h[16] = {0};
        AesCore::encryptBlock(ks, zero, h);

        // Counter block J0 = Nonce || 0x00000001
        uint8_t j0[16] = {0};
        micant::shared::boundedMemoryCopy(j0, nonce.data(), 12);
        j0[15] = 1;

        // Encrypt J0 for tag mask
        uint8_t j0Enc[16] = {0};
        AesCore::encryptBlock(ks, j0, j0Enc);

        // Compute expected GHASH tag
        uint8_t ghashTag[16];
        computeGhash(h, aad, ciphertext, ghashTag);

        uint8_t expectedTag[16];
        for (int i = 0; i < 16; ++i) {
            expectedTag[i] = ghashTag[i] ^ j0Enc[i];
        }

        // Constant-time tag check
        uint8_t diff = 0;
        for (int i = 0; i < 16; ++i) {
            diff |= (expectedTag[i] ^ tag[i]);
        }
        if (diff != 0) {
            return false; // Authentication tag mismatch!
        }

        // CTR Mode Decryption
        if (!plaintext.resize(ciphertext.size()) || !plaintext.good()) return false;
        uint8_t ctr[16];
        micant::shared::boundedMemoryCopy(ctr, j0, 16);

        size_t blocks = (ciphertext.size() + 15) / 16;
        for (size_t b = 0; b < blocks; ++b) {
            uint32_t val = (static_cast<uint32_t>(ctr[12]) << 24) |
                           (static_cast<uint32_t>(ctr[13]) << 16) |
                           (static_cast<uint32_t>(ctr[14]) << 8)  |
                           (static_cast<uint32_t>(ctr[15]));
            val++;
            ctr[12] = static_cast<uint8_t>((val >> 24) & 0xFF);
            ctr[13] = static_cast<uint8_t>((val >> 16) & 0xFF);
            ctr[14] = static_cast<uint8_t>((val >> 8) & 0xFF);
            ctr[15] = static_cast<uint8_t>(val & 0xFF);

            uint8_t sBlock[16];
            AesCore::encryptBlock(ks, ctr, sBlock);

            size_t off = b * 16;
            size_t len = std::min(static_cast<size_t>(16), ciphertext.size() - off);
            for (size_t i = 0; i < len; ++i) {
                plaintext[off + i] = ciphertext[off + i] ^ sBlock[i];
            }
        }

        return true;
    }

#if !defined(MICANT_FREESTANDING)
    static bool decrypt(std::span<const uint8_t> key, std::span<const uint8_t> nonce,
                        std::span<const uint8_t> aad, std::span<const uint8_t> ciphertext,
                        std::span<const uint8_t> tag, std::vector<uint8_t>& plaintext) {
        micant::shared::BoundedVector<uint8_t> bounded;
        if (!decrypt(key, nonce, aad, ciphertext, tag, bounded)) return false;
        plaintext.assign(bounded.begin(), bounded.end());
        return true;
    }
#endif
};

// ============================================================================
// 6. Clean-Room X25519 (RFC 7748) Scalar Multiplication on Curve25519
// ============================================================================

class X25519 {
public:
    using Fe = std::array<int64_t, 16>;

    static void feZero(Fe& h) noexcept { h.fill(0); }
    static void feOne(Fe& h) noexcept { h.fill(0); h[0] = 1; }

    static void feFromBytes(Fe& h, const uint8_t s[32]) noexcept {
        for (int i = 0; i < 16; ++i) {
            h[i] = static_cast<int64_t>(s[2 * i]) | (static_cast<int64_t>(s[2 * i + 1]) << 8);
        }
        h[15] &= 0x7fff;
    }

    static void feCarry(Fe& h) noexcept {
        for (int i = 0; i < 15; ++i) {
            int64_t carry = h[i] >> 16;
            // Carries may be negative after subtraction; left-shifting a
            // negative signed value is undefined in C++.
            h[i] -= carry * 65536;
            h[i + 1] += carry;
        }
        int64_t carry = h[15] >> 15;
        h[15] &= 0x7FFF;
        h[0] += carry * 19;

        for (int i = 0; i < 15; ++i) {
            int64_t c = h[i] >> 16;
            h[i] -= c * 65536;
            h[i + 1] += c;
        }
        int64_t c2 = h[15] >> 15;
        h[15] &= 0x7FFF;
        h[0] += c2 * 19;
    }

    static void feToBytes(uint8_t s[32], const Fe& h) noexcept {
        Fe t = h;
        feCarry(t);
        feCarry(t);

        // Strict canonical reduction modulo 2^255 - 19
        // p in limbs: [0xFFED, 0xFFFF, ..., 0x7FFF]
        Fe q;
        int64_t diff0 = t[0] - 0xFFED;
        uint64_t borrow = static_cast<uint64_t>(diff0) >> 63;
        q[0] = diff0 + static_cast<int64_t>(borrow * 0x10000u);
        for (int i = 1; i < 15; ++i) {
            const int64_t diff = t[i] - 0xFFFF - static_cast<int64_t>(borrow);
            borrow = static_cast<uint64_t>(diff) >> 63;
            q[i] = diff + static_cast<int64_t>(borrow * 0x10000u);
        }
        const int64_t diff15 = t[15] - 0x7FFF - static_cast<int64_t>(borrow);
        borrow = static_cast<uint64_t>(diff15) >> 63;
        q[15] = diff15 + static_cast<int64_t>(borrow * 0x8000u);
        const uint64_t selectReduced = uint64_t{0} - (borrow ^ 1u);
        for (int i = 0; i < 16; ++i) {
            const uint64_t current = static_cast<uint64_t>(t[i]);
            const uint64_t reduced = static_cast<uint64_t>(q[i]);
            t[i] = static_cast<int64_t>((current & ~selectReduced) | (reduced & selectReduced));
        }

        for (int i = 0; i < 16; ++i) {
            s[2 * i]     = static_cast<uint8_t>(t[i] & 0xFF);
            s[2 * i + 1] = static_cast<uint8_t>((t[i] >> 8) & 0xFF);
        }
    }

    static void feAdd(Fe& h, const Fe& f, const Fe& g) noexcept {
        for (int i = 0; i < 16; ++i) h[i] = f[i] + g[i];
    }

    static void feSub(Fe& h, const Fe& f, const Fe& g) noexcept {
        for (int i = 0; i < 16; ++i) h[i] = f[i] - g[i];
    }

    static void feMul(Fe& h, const Fe& f, const Fe& g) noexcept {
        int64_t t[31] = {0};
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                t[i + j] += f[i] * g[j];
            }
        }
        for (int i = 0; i < 15; ++i) {
            t[i] += t[i + 16] * 38;
        }
        for (int i = 0; i < 16; ++i) {
            h[i] = t[i];
        }
        feCarry(h);
        feCarry(h);
    }

    static void feSquare(Fe& h, const Fe& f) noexcept { feMul(h, f, f); }

    static void feInvert(Fe& out, const Fe& z) noexcept {
        Fe t0, t1, t2, t3;
        feSquare(t0, z);
        feSquare(t1, t0);
        feSquare(t1, t1);
        feMul(t1, z, t1);
        feMul(t0, t0, t1);
        feSquare(t2, t0);
        feMul(t1, t1, t2);
        feSquare(t2, t1);
        for (int i = 1; i < 5; ++i) feSquare(t2, t2);
        feMul(t1, t2, t1);
        feSquare(t2, t1);
        for (int i = 1; i < 10; ++i) feSquare(t2, t2);
        feMul(t2, t2, t1);
        feSquare(t3, t2);
        for (int i = 1; i < 20; ++i) feSquare(t3, t3);
        feMul(t2, t3, t2);
        feSquare(t2, t2);
        for (int i = 1; i < 10; ++i) feSquare(t2, t2);
        feMul(t1, t2, t1);
        feSquare(t2, t1);
        for (int i = 1; i < 50; ++i) feSquare(t2, t2);
        feMul(t2, t2, t1);
        feSquare(t3, t2);
        for (int i = 1; i < 100; ++i) feSquare(t3, t3);
        feMul(t2, t3, t2);
        feSquare(t2, t2);
        for (int i = 1; i < 50; ++i) feSquare(t2, t2);
        feMul(t1, t2, t1);
        feSquare(t1, t1);
        for (int i = 1; i < 5; ++i) feSquare(t1, t1);
        feMul(out, t1, t0);
    }

    static void cswap(Fe& a, Fe& b, int swap) noexcept {
        int64_t mask = -static_cast<int64_t>(swap);
        for (int i = 0; i < 16; ++i) {
            int64_t t = mask & (a[i] ^ b[i]);
            a[i] ^= t;
            b[i] ^= t;
        }
    }

    // RFC 7748 X25519 scalar multiplication: out = scalar * point
    static void scalarMult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) noexcept {
        uint8_t clamped[32];
        micant::shared::boundedMemoryCopy(clamped, scalar, 32);
        clamped[0]  &= 248;
        clamped[31] &= 127;
        clamped[31] |= 64;

        Fe x1;
        feFromBytes(x1, point);

        Fe x2, z2, x3, z3;
        feOne(x2);
        feZero(z2);
        x3 = x1;
        feOne(z3);

        int swap = 0;
        for (int pos = 254; pos >= 0; --pos) {
            int b = (clamped[pos / 8] >> (pos % 8)) & 1;
            swap ^= b;
            cswap(x2, x3, swap);
            cswap(z2, z3, swap);
            swap = b;

            Fe a, b_fe, c, d, e, aa, bb, da, cb;
            feAdd(a, x2, z2);
            feSquare(aa, a);
            feSub(b_fe, x2, z2);
            feSquare(bb, b_fe);
            feSub(e, aa, bb);
            feAdd(c, x3, z3);
            feSub(d, x3, z3);
            feMul(da, d, a);
            feMul(cb, c, b_fe);

            Fe da_cb_add, da_cb_sub;
            feAdd(da_cb_add, da, cb);
            feSquare(x3, da_cb_add);
            feSub(da_cb_sub, da, cb);
            feSquare(z3, da_cb_sub);
            feMul(z3, x1, z3);

            feMul(x2, aa, bb);
            Fe a24;
            feZero(a24);
            a24[0] = 121665; // (486662 - 2) / 4
            Fe a24_e;
            feMul(a24_e, a24, e);
            Fe aa_a24_e;
            feAdd(aa_a24_e, aa, a24_e);
            feMul(z2, e, aa_a24_e);
        }

        cswap(x2, x3, swap);
        cswap(z2, z3, swap);

        Fe z2_inv;
        feInvert(z2_inv, z2);
        Fe x_res;
        feMul(x_res, x2, z2_inv);
        feToBytes(out, x_res);
        volatile uint8_t* scalarBytes = clamped;
        for (size_t i = 0; i < sizeof(clamped); ++i) scalarBytes[i] = 0;
        for (Fe* field : {&x2, &z2, &x3, &z3, &z2_inv, &x_res}) {
            volatile int64_t* limbs = field->data();
            for (size_t i = 0; i < field->size(); ++i) limbs[i] = 0;
        }
    }

    // Generate public key from private key (point = 9)
    static void generatePublicKey(uint8_t publicKey[32], const uint8_t privateKey[32]) noexcept {
        static const uint8_t basePoint[32] = {
            9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        scalarMult(publicKey, privateKey, basePoint);
    }
};

// ============================================================================
// 7. Hardware & Software CSPRNG (BCryptGenRandom)
// ============================================================================

class CryptoRng {
public:
    // Use only AMD64 hardware entropy instructions. A clock-seeded software
    // PRNG is not a cryptographic source and must never seed TLS/CNG material.
    // Failure is explicit when the processor has no supported hardware source
    // or the source fails its bounded retry/continuous-output check.
    static bool generate(std::span<uint8_t> buffer) noexcept {
        if (buffer.empty()) return true;
#if defined(__x86_64__) || defined(_M_X64)
        uint32_t eax = 0, ebx{}, ecx{}, edx{};
        __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        const uint32_t maximumLeaf = eax;
        if (maximumLeaf < 1) { zero(buffer); return false; }
        eax = 1;
        __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        const bool hasRdrand = (ecx & (uint32_t{1} << 30)) != 0;
        bool hasRdseed = false;
        if (maximumLeaf >= 7) {
            eax = 7; ecx = 0;
            __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
            hasRdseed = (ebx & (uint32_t{1} << 18)) != 0;
        }
        if (!hasRdseed && !hasRdrand) { zero(buffer); return false; }

        uint64_t previous{};
        bool havePrevious = false;
        size_t offset = 0;
        while (offset < buffer.size()) {
            uint64_t value{};
            unsigned char success = 0;
            for (unsigned attempt = 0; attempt < 10 && !success; ++attempt) {
                if (hasRdseed)
                    __asm__ __volatile__("rdseed %0; setc %1" : "=r"(value), "=qm"(success) : : "cc");
                else
                    __asm__ __volatile__("rdrand %0; setc %1" : "=r"(value), "=qm"(success) : : "cc");
            }
            // RDSEED can temporarily exhaust its hardware seed queue. If the
            // processor also advertises RDRAND, use its hardware DRBG rather
            // than substituting a time-seeded or deterministic generator.
            if (!success && hasRdseed && hasRdrand) {
                for (unsigned attempt = 0; attempt < 10 && !success; ++attempt)
                    __asm__ __volatile__("rdrand %0; setc %1" : "=r"(value), "=qm"(success) : : "cc");
            }
            if (!success || (havePrevious && value == previous)) {
                zero(buffer);
                secureZero(value);
                return false;
            }
            previous = value;
            havePrevious = true;
            const size_t count = std::min(sizeof(value), buffer.size() - offset);
            for (size_t i = 0; i < count; ++i)
                buffer[offset + i] = static_cast<uint8_t>(value >> (i * 8));
            offset += count;
            secureZero(value);
        }
        secureZero(previous);
        return true;
#else
        zero(buffer);
        return false;
#endif
    }

private:
    static void zero(std::span<uint8_t> buffer) noexcept {
        volatile uint8_t* p = buffer.data();
        for (size_t i = 0; i < buffer.size(); ++i) p[i] = 0;
    }
    static void secureZero(uint64_t& value) noexcept {
        volatile uint64_t* p = &value;
        *p = 0;
    }
};


} // namespace micant::shared::crypto
