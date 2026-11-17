/**
 * @file bootx64.cpp
 * @brief MicaNT Bare-Metal UEFI Bootloader (bootx64.efi).
 *
 * Implements the clean-room Phase 4 UEFI entry point, GOP framebuffer discovery,
 * ACPI 2.0 table resolution, physical memory mapping, boot splash display,
 * and transition to the MicaNT Kernel via the Loader Parameter Block (LPB).
 *
 * Reference: UEFI Specification 2.10, Sections 4, 7, 12.
 */
#include "micant/km/hal/uefi.hpp"
#include "micant/km/hal/boot.hpp"
#include "micant/km/hal/boot_contract.hpp"
#include "micant/km/hal/bootvid.hpp"
#include "micant/km/hal/cpu.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/executive/mm.hpp"
#include "micant/km/hal/highhalf.hpp"
#include "micant/km/hal/idt.hpp"
#include "micant/km/hal/smp.hpp"
#include "micant/km/executive/ntoskrnl_loader.hpp"
#include "../test/unmodified_fixture.hpp"
#include <cstring>
#include <iterator>
#include <new>

extern "C" void ___chkstk_ms() {}
extern "C" void __chkstk() {}

namespace micant::bootloader {

// UEFI PCI I/O protocol prefix used only before ExitBootServices. The BAR
// resource buffers belong to firmware and are freed after copying the POD.
struct FirmwarePciIo {
    void *pollMem, *pollIo, *memRead, *memWrite, *ioRead, *ioWrite, *pciRead, *pciWrite;
    void *copyMem, *map, *unmap, *allocateBuffer, *freeBuffer, *flush;
    uefi::EfiStatus (*getLocation)(FirmwarePciIo*, uint64_t*, uint64_t*, uint64_t*, uint64_t*);
    void* attributes;
    uefi::EfiStatus (*getBarAttributes)(FirmwarePciIo*, uint8_t, uint64_t*, void**);
};
static constexpr uefi::EfiGuid kPciIoGuid{0x4cf5b200, 0x68b8, 0x4ca5,
    {0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a}};
static_assert(offsetof(FirmwarePciIo, getLocation) == 112);
static_assert(offsetof(FirmwarePciIo, getBarAttributes) == 128);

static inline void outb(uint16_t port, uint8_t val) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}

[[noreturn]] static inline void HalpColdResetServer() noexcept {
    outb(0x64, 0xFE);
    outb(0xCF9, 0x02);
    for (int i = 0; i < 50000; ++i) { __asm__ __volatile__("pause"); }
    outb(0xCF9, 0x06);
    for (int i = 0; i < 50000; ++i) { __asm__ __volatile__("pause"); }
    outb(0xCF9, 0x0E);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("hlt");
#endif
    }
}

static inline uint8_t inb(uint16_t port) noexcept {
    uint8_t ret = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
#endif
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
#endif
}

static inline uint32_t inl(uint16_t port) noexcept {
    uint32_t ret = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
#endif
    return ret;
}

static inline uint64_t rdtsc() noexcept {
    uint32_t low = 0, high = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
#endif
    return (static_cast<uint64_t>(high) << 32) | low;
}

static inline uint64_t rdmsr(uint32_t msr) noexcept {
    uint32_t low = 0, high = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
#endif
    return (static_cast<uint64_t>(high) << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t val) noexcept {
    uint32_t low = static_cast<uint32_t>(val & 0xFFFFFFFFULL);
    uint32_t high = static_cast<uint32_t>(val >> 32);
#if defined(__x86_64__)
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
#endif
}

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
#else
    eax = ebx = ecx = edx = 0;
#endif
}

static inline void getCpuBrandString(char brand[49]) noexcept {
    uint32_t* p = reinterpret_cast<uint32_t*>(brand);
    for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; ++leaf) {
        cpuid(leaf, 0, p[0], p[1], p[2], p[3]);
        p += 4;
    }
    brand[48] = '\0';
    int start = 0;
    while (brand[start] == ' ') start++;
    if (start > 0) {
        int i = 0;
        while (brand[start]) brand[i++] = brand[start++];
        brand[i] = '\0';
    }
}

static inline void getShortCpuBrand(char* out, size_t outSize, const char* fullBrand) noexcept {
    if (!out || outSize == 0) return;
    if (!fullBrand || !fullBrand[0]) {
        const char def[] = "Xeon E3-1270 v6";
        size_t i = 0;
        for (; def[i] && i + 1 < outSize; ++i) out[i] = def[i];
        out[i] = '\0';
        return;
    }
    const char* p = fullBrand;
    if (p[0] == 'I' && p[1] == 'n' && p[2] == 't' && p[3] == 'e' && p[4] == 'l') {
        p += 5;
        if (p[0] == '(' && p[1] == 'R' && p[2] == ')') p += 3;
        while (*p == ' ') p++;
    }
    if (p[0] == 'X' && p[1] == 'e' && p[2] == 'o' && p[3] == 'n') {
        size_t o = 0;
        out[o++] = 'X'; out[o++] = 'e'; out[o++] = 'o'; out[o++] = 'n'; out[o++] = ' ';
        p += 4;
        if (p[0] == '(' && p[1] == 'R' && p[2] == ')') p += 3;
        while (*p == ' ') p++;
        if (p[0] == 'C' && p[1] == 'P' && p[2] == 'U' && p[3] == ' ') p += 4;
        while (*p == ' ') p++;
        while (*p && *p != '@' && o + 1 < outSize) {
            out[o++] = *p++;
        }
        while (o > 0 && out[o - 1] == ' ') o--;
        out[o] = '\0';
        return;
    }
    size_t o = 0;
    while (*p && *p != '@' && o + 1 < outSize && o < 20) {
        out[o++] = *p++;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = '\0';
}

static inline void getCleanBiosVersion(char* out, size_t outSize, const char* fullBios) noexcept {
    if (!out || outSize == 0) return;
    if (!fullBios || !fullBios[0]) {
        const char def[] = "03.01.5049";
        size_t i = 0;
        for (; def[i] && i + 1 < outSize; ++i) out[i] = def[i];
        out[i] = '\0';
        return;
    }
    const char* p = fullBios;
    for (size_t i = 0; fullBios[i] && fullBios[i + 4]; ++i) {
        if (fullBios[i] == '.' && fullBios[i + 1] == '8' && fullBios[i + 2] == '6' && fullBios[i + 3] == 'B' && fullBios[i + 4] == '.') {
            p = &fullBios[i + 5];
            break;
        }
    }
    size_t dots = 0;
    size_t o = 0;
    while (*p && o + 1 < outSize) {
        if (*p == '.') {
            dots++;
            if (dots >= 3) break;
        }
        out[o++] = *p++;
    }
    while (o > 0 && (out[o - 1] == '.' || out[o - 1] == ' ')) o--;
    out[o] = '\0';
}

static inline bool isIntelDtsSupported() noexcept {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(0, 0, eax, ebx, ecx, edx);
    if (ebx != 0x756e6547 || edx != 0x49656e69 || ecx != 0x6c65746e) {
        return false; // Not "GenuineIntel"
    }
    if (eax >= 6) {
        cpuid(6, 0, eax, ebx, ecx, edx);
        return (eax & 1) != 0; // Digital thermal sensor capability
    }
    return false;
}

static inline uint32_t getCpuTemperatureC(uint32_t /*ignored*/ = 0) noexcept {
    // Sovereign Real Physical Silicon Reading from Intel Xeon DTS (Digital Thermal Sensor)
    if (!isIntelDtsSupported()) return 0;
    uint64_t target = rdmsr(0x1A2); // IA32_TEMPERATURE_TARGET
    uint32_t tjMax = static_cast<uint32_t>((target >> 16) & 0xFF);
    if (tjMax == 0 || tjMax > 125) tjMax = 100;

    uint64_t therm = rdmsr(0x19C); // IA32_THERM_STATUS
    if (!(therm & (1ULL << 31))) { // Bit 31: Reading Valid
        return 0;
    }
    uint32_t delta = static_cast<uint32_t>((therm >> 16) & 0x7F);
    if (delta >= tjMax) return 0;
    return tjMax - delta;
}

static inline bool isIntelMperfSupported() noexcept {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(0, 0, eax, ebx, ecx, edx);
    if (ebx != 0x756e6547 || edx != 0x49656e69 || ecx != 0x6c65746e) {
        return false; // Not "GenuineIntel"
    }
    if (eax >= 6) {
        cpuid(6, 0, eax, ebx, ecx, edx);
        return (ecx & 1) != 0; // Hardware Coordination Feedback (IA32_MPERF/IA32_APERF MSR 0xE7/0xE8)
    }
    return false;
}

static inline uint64_t getMperf() noexcept {
    return rdmsr(0xE7); // IA32_MPERF: C0 Active State Cycle Counter
}

static inline uint64_t getAperf() noexcept {
    return rdmsr(0xE8); // IA32_APERF: C0 Actual Frequency Cycle Counter
}

static inline size_t formatUint(char* buf, uint64_t val) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    char tmp[24];
    size_t pos = 0;
    while (val > 0) {
        tmp[pos++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    for (size_t i = 0; i < pos; ++i) {
        buf[i] = tmp[pos - 1 - i];
    }
    buf[pos] = '\0';
    return pos;
}

static inline size_t formatHex32(char* buf, uint32_t val) {
    buf[0] = '0';
    buf[1] = 'x';
    const char hexChars[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) {
        buf[2 + (7 - i)] = hexChars[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    return 10;
}

static inline size_t formatHex16(char* buf, uint16_t val) noexcept {
    const char hexChars[] = "0123456789ABCDEF";
    buf[0] = hexChars[(val >> 12) & 0xF];
    buf[1] = hexChars[(val >> 8) & 0xF];
    buf[2] = hexChars[(val >> 4) & 0xF];
    buf[3] = hexChars[val & 0xF];
    buf[4] = '\0';
    return 4;
}

static inline void copyStr(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || maxLen == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i < maxLen - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static inline void concatStr(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || !src || maxLen == 0) return;
    size_t dLen = 0;
    while (dst[dLen] && dLen < maxLen - 1) {
        ++dLen;
    }
    size_t sIdx = 0;
    while (src[sIdx] && (dLen + sIdx) < maxLen - 1) {
        dst[dLen + sIdx] = src[sIdx];
        ++sIdx;
    }
    dst[dLen + sIdx] = '\0';
}

static inline bool hasInvariantTsc() noexcept {
    uint32_t eax = 0x80000000u, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000007u) return false;
    eax = 0x80000007u;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 8)) != 0;
}

static inline uint64_t readSerializedTsc() noexcept {
    uint32_t low{}, high{};
    __asm__ __volatile__("lfence; rdtsc; lfence"
                         : "=a"(low), "=d"(high) : : "memory");
    return (static_cast<uint64_t>(high) << 32) | low;
}

static inline uint64_t measureTscFrequencyHz(uefi::EfiBootServices* bs) noexcept {
    if (!bs || !bs->stall || !hasInvariantTsc()) return 0;
    constexpr uint64_t maximumFrequencyHz = 10'000'000'000ULL;
    constexpr uint64_t minimumFrequencyHz = 1'000'000;
    constexpr uint32_t intervalMicroseconds = 10'000;
    uint64_t samples[5]{};
    for (auto& sample : samples) {
        const uint64_t start = readSerializedTsc();
        if (bs->stall(intervalMicroseconds) != uefi::EFI_SUCCESS) return 0;
        const uint64_t end = readSerializedTsc();
        if (end <= start) return 0;
        const uint64_t delta = end - start;
        if (delta > UINT64_MAX / 100) return 0;
        sample = delta * 100; // 10 ms interval.
        if (sample < minimumFrequencyHz || sample > maximumFrequencyHz) return 0;
    }
    for (size_t i = 1; i < 5; ++i) {
        const uint64_t value = samples[i];
        size_t j = i;
        while (j && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }
    const uint64_t median = samples[2];
    if (!median || samples[3] - samples[1] > median / 20) return 0;
    return median;
}

/**
 * @brief Freestanding 16550A UART Driver for Bare-Metal COM1 (0x3F8) / IPMI Serial-over-LAN.
 */
struct UartSerial {
    static constexpr uint16_t COM1 = 0x3F8;

    static void init() noexcept {
        outb(COM1 + 1, 0x00);    // Disable all interrupts
        outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
        outb(COM1 + 0, 0x01);    // Set divisor to 1 (115200 baud)
        outb(COM1 + 1, 0x00);
        outb(COM1 + 3, 0x03);    // 8 bits, no parity, 1 stop bit (8N1)
        outb(COM1 + 2, 0xC7);    // Enable FIFO, clear TX/RX queues, 14-byte threshold
        outb(COM1 + 4, 0x0B);    // Auxiliary output 2, RTS, DTR
    }

    static bool isTransmitEmpty() noexcept {
        return (inb(COM1 + 5) & 0x20) != 0;
    }

    static void putChar(char c) noexcept {
        if (c == '\n') putChar('\r');
        uint32_t timeout = 50000;
        while (!isTransmitEmpty() && --timeout) {}
        if (timeout > 0) {
            outb(COM1, static_cast<uint8_t>(c));
        }
    }

    static void printChar(char c) noexcept {
        putChar(c);
    }

    static void print(const char* str) noexcept {
        while (*str) {
            putChar(*str++);
        }
    }

    static void printDec(uint32_t val) noexcept {
        if (val == 0) {
            putChar('0');
            return;
        }
        char buf[12];
        int pos = 0;
        while (val > 0) {
            buf[pos++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
        for (int i = pos - 1; i >= 0; --i) {
            putChar(buf[i]);
        }
    }

    static void printInt(int32_t val) noexcept {
        if (val < 0) {
            putChar('-');
            printDec(static_cast<uint32_t>(-val));
        } else {
            printDec(static_cast<uint32_t>(val));
        }
    }

    static void printHex64(uint64_t val) noexcept {
        const char hex[] = "0123456789ABCDEF";
        for (int i = 60; i >= 0; i -= 4) {
            putChar(hex[(val >> i) & 0xF]);
        }
    }

    static void printHex32(uint32_t val) noexcept {
        const char hex[] = "0123456789ABCDEF";
        for (int i = 28; i >= 0; i -= 4) {
            putChar(hex[(val >> i) & 0xF]);
        }
    }

    static void printHex16(uint16_t val) noexcept {
        const char hex[] = "0123456789ABCDEF";
        for (int i = 12; i >= 0; i -= 4) {
            putChar(hex[(val >> i) & 0xF]);
        }
    }

    static bool hasChar() noexcept {
        return (inb(COM1 + 5) & 0x01) != 0;
    }

    static bool tryGetChar(char& c) noexcept {
        if (hasChar()) {
            c = static_cast<char>(inb(COM1));
            return true;
        }
        return false;
    }
};

/**
 * @brief 64-Entry Fast Sine Table (-127 to +127) for Freestanding 3D Graphics.
 */
static constexpr int8_t s_sinTable64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};

inline int32_t fastSin(uint32_t angle) noexcept {
    return s_sinTable64[angle & 63];
}

inline int32_t fastCos(uint32_t angle) noexcept {
    return s_sinTable64[(angle + 16) & 63];
}

/**
 * @brief Freestanding 12x19 Interactive Mouse Cursor with background save/restore.
 */
struct InteractiveCursor {
    static constexpr uint32_t CURSOR_WIDTH = 12;
    static constexpr uint32_t CURSOR_HEIGHT = 19;
    static constexpr const char* CURSOR_BITMAP[19] = {
        "X           ",
        "XX          ",
        "X.X         ",
        "X..X        ",
        "X...X       ",
        "X....X      ",
        "X.....X     ",
        "X......X    ",
        "X.......X   ",
        "X........X  ",
        "X.....XXXXX ",
        "X..X..X     ",
        "X.X X..X    ",
        "XX   X..X   ",
        "X     X..X  ",
        "      X..X  ",
        "       XX   ",
        "            ",
        "            "
    };

    int32_t currentX{720};
    int32_t currentY{360};
    bool isDrawn{false};
    uint32_t savedDwords[CURSOR_HEIGHT][CURSOR_WIDTH]{};

    void erase(bootvid::BootVideoDriver& driver) {
        if (!isDrawn) return;
        uint32_t* fb = driver.getRawBuffer();
        if (!fb) return;
        uint32_t w = driver.getWidth();
        uint32_t h = driver.getHeight();
        uint32_t pitch = driver.getPitch();

        for (uint32_t r = 0; r < CURSOR_HEIGHT; ++r) {
            int32_t py = currentY + static_cast<int32_t>(r);
            if (py < 0 || py >= static_cast<int32_t>(h)) continue;
            for (uint32_t c = 0; c < CURSOR_WIDTH; ++c) {
                int32_t px = currentX + static_cast<int32_t>(c);
                if (px < 0 || px >= static_cast<int32_t>(w)) continue;
                if (CURSOR_BITMAP[r][c] != ' ') {
                    fb[py * pitch + px] = savedDwords[r][c];
                }
            }
        }
        isDrawn = false;
    }

    void draw(bootvid::BootVideoDriver& driver, int32_t newX, int32_t newY) {
        if (isDrawn) erase(driver);
        uint32_t* fb = driver.getRawBuffer();
        if (!fb) return;
        uint32_t w = driver.getWidth();
        uint32_t h = driver.getHeight();
        uint32_t pitch = driver.getPitch();

        if (newX < 0) newX = 0;
        if (newY < 0) newY = 0;
        if (newX + static_cast<int32_t>(CURSOR_WIDTH) > static_cast<int32_t>(w)) {
            newX = static_cast<int32_t>(w) - static_cast<int32_t>(CURSOR_WIDTH);
        }
        if (newY + static_cast<int32_t>(CURSOR_HEIGHT) > static_cast<int32_t>(h)) {
            newY = static_cast<int32_t>(h) - static_cast<int32_t>(CURSOR_HEIGHT);
        }
        currentX = newX;
        currentY = newY;

        // 1. Save underlying raw dwords directly
        for (uint32_t r = 0; r < CURSOR_HEIGHT; ++r) {
            int32_t py = currentY + static_cast<int32_t>(r);
            if (py < 0 || py >= static_cast<int32_t>(h)) continue;
            for (uint32_t c = 0; c < CURSOR_WIDTH; ++c) {
                int32_t px = currentX + static_cast<int32_t>(c);
                if (px >= 0 && px < static_cast<int32_t>(w)) {
                    savedDwords[r][c] = fb[py * pitch + px];
                }
            }
        }

        // 2. Render cursor arrow bitmap directly as 32-bit dwords
        constexpr uint32_t BLACK_PIXEL = 0xFF000000;
        constexpr uint32_t WHITE_PIXEL = 0xFFFFFFFF;

        for (uint32_t r = 0; r < CURSOR_HEIGHT; ++r) {
            int32_t py = currentY + static_cast<int32_t>(r);
            if (py < 0 || py >= static_cast<int32_t>(h)) continue;
            for (uint32_t c = 0; c < CURSOR_WIDTH; ++c) {
                int32_t px = currentX + static_cast<int32_t>(c);
                if (px < 0 || px >= static_cast<int32_t>(w)) continue;
                char ch = CURSOR_BITMAP[r][c];
                if (ch == 'X') {
                    fb[py * pitch + px] = BLACK_PIXEL;
                } else if (ch == '.') {
                    fb[py * pitch + px] = WHITE_PIXEL;
                }
            }
        }
        isDrawn = true;
    }
};

static inline void trimTrailingSpaces(char* s) noexcept {
    if (!s) return;
    size_t len = 0;
    while (s[len]) ++len;
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

/**
 * @brief Sovereign Real Hardware Silicon & Firmware Telemetry Discovery.
 *
 * Reads 100% genuine silicon metrics from CPUID, Time Stamp Counter (TSC),
 * System Management BIOS (SMBIOS Type 0, 1, 2, 4, 17), Advanced Configuration
 * and Power Interface (ACPI 2.0+ XSDT/RSDT & MADT), and PCI Configuration Space.
 */
struct RealHardwareTelemetry {
    // 1. CPU
    char cpuBrand[49]{};
    uint32_t cpuMeasuredMHz{0};
    uint8_t cpuPhysicalCores{0};
    uint8_t cpuLogicalThreads{0};
    char cpuSocket[16]{};
    uint32_t cpuTempC{0};

    // 2. Motherboard & BIOS (from SMBIOS)
    char boardVendor[32]{};
    char boardProduct[32]{};
    char biosVendor[32]{};
    char biosVersion[40]{};
    char biosDate[16]{};

    // 3. Memory
    uint64_t ramTotalMB{0};
    uint64_t ramFreeMB{0};
    uint64_t ramUsedMB{0};
    struct DimmSlot {
        uint32_t sizeMB{0};
        uint16_t speedMTs{0};
        char locator[16]{};
        char manufacturer[16]{};
    } dimms[4]{};
    size_t dimmCount{0};

    // 4. Physical Storage: Intel C230/C236 SATA AHCI Controller & Ports
    struct AhciPortInfo {
        bool present{false};
        uint8_t speedGbps{0}; // 1.5, 3.0, 6.0
        uint32_t signature{0};
        char type[16]{};
    };
    bool ahciFound{false};
    uint32_t ahciBar5{0};
    uint32_t ahciVersion{0};
    uint32_t ahciPortsImplemented{0};
    AhciPortInfo ahciPorts[8]{};
    size_t ahciDriveCount{0};

    // 5. Physical Storage: UEFI Block I/O Drives & Sectors
    struct BlockDeviceInfo {
        uint32_t mediaId{0};
        bool isPartition{false};
        uint32_t blockSize{512};
        uint64_t totalBlocks{0};
        uint64_t sizeGB{0};
        bool gptValid{false};
        uint8_t gptGuid[16]{};
        bool isExtFs{false};
        char extFsVolumeName[18]{};
    };
    BlockDeviceInfo blockDevices[8]{};
    size_t blockDeviceCount{0};
    size_t physicalDiskCount{0};

    // 6. Physical PCI Devices (from ports 0xCF8/0xCFC)
    struct PciDev {
        uint8_t bus{0};
        uint8_t dev{0};
        uint8_t func{0};
        uint16_t vendorId{0};
        uint16_t deviceId{0};
        uint8_t classCode{0};
        uint8_t subclass{0};
        char name[36]{};
    } pciDevices[64]{};
    size_t pciDeviceCount{0};

    // 7. Local APIC & IO-APIC (Ring 0 Telemetry)
    uint64_t lapicBaseAddr{0xFEE00000ULL};
    bool lapicEnabled{false};
    uint32_t lapicId{0};
    uint32_t lapicVersion{0};
    uint32_t ioApicAddress{0xFEC00000};

    // 8. ACPI Tables
    char acpiTables[16][8]{};
    size_t acpiTableCount{0};
    uint8_t acpiMadtLocalApics{0};
    uint64_t hpetAddress{0xFED00000};
    uint64_t mcfgAddress{0xE0000000};

    void probeLapicRing0() noexcept {
        uint64_t apicBase = rdmsr(0x1B); // IA32_APIC_BASE MSR
        lapicEnabled = (apicBase & (1ULL << 11)) != 0;
        uint64_t base = apicBase & 0xFFFFF000ULL;
        if (base) lapicBaseAddr = base;

        // Query LAPIC MMIO register 0x020 (ID) and 0x030 (Version)
        volatile const uint32_t* lapic = reinterpret_cast<volatile const uint32_t*>(static_cast<uintptr_t>(lapicBaseAddr));
        lapicId = (lapic[0x020 / 4] >> 24) & 0xFF;
        lapicVersion = lapic[0x030 / 4] & 0xFF;
    }

    static uint32_t pciRead32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) noexcept {
        uint32_t address = (1u << 31) | (static_cast<uint32_t>(bus) << 16) |
                           (static_cast<uint32_t>(dev) << 11) |
                           (static_cast<uint32_t>(func) << 8) | (reg & 0xFC);
        outl(0xCF8, address);
        return inl(0xCFC);
    }

    void probeCpuTopology() noexcept {
        uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
        cpuid(1, 0, eax, ebx, ecx, edx);
        uint8_t logical = static_cast<uint8_t>((ebx >> 16) & 0xFF);
        if (logical > 0) cpuLogicalThreads = logical;

        cpuid(4, 0, eax, ebx, ecx, edx);
        uint8_t physical = static_cast<uint8_t>(((eax >> 26) & 0x3F) + 1);
        if (physical > 0) cpuPhysicalCores = physical;
    }

    void probeAhci(uint8_t bus, uint8_t dev, uint8_t func) noexcept {
        uint32_t bar5 = pciRead32(bus, dev, func, 0x24);
        if ((bar5 & 1) != 0) return; // Must be memory-mapped BAR
        uint32_t abarAddr = bar5 & ~0xFULL;
        if (abarAddr == 0) return;

        ahciFound = true;
        ahciBar5 = abarAddr;

        volatile const uint32_t* abar = reinterpret_cast<volatile const uint32_t*>(static_cast<uintptr_t>(abarAddr));
        ahciVersion = abar[4]; // Offset 0x10 = VS
        ahciPortsImplemented = abar[3]; // Offset 0x0C = PI
        ahciDriveCount = 0;

        for (uint8_t p = 0; p < 8; ++p) {
            if (ahciPortsImplemented & (1u << p)) {
                volatile const uint32_t* pRegs = reinterpret_cast<volatile const uint32_t*>(
                    reinterpret_cast<uintptr_t>(abar) + 0x100 + p * 0x80
                );
                uint32_t sig = pRegs[9];   // Offset 0x24 (SIG)
                uint32_t ssts = pRegs[10]; // Offset 0x28 (SSTS)
                uint8_t det = static_cast<uint8_t>(ssts & 0x0F);
                uint8_t spd = static_cast<uint8_t>((ssts >> 4) & 0x0F);

                if (det == 3) {
                    auto& port = ahciPorts[p];
                    port.present = true;
                    port.speedGbps = (spd == 3) ? 6 : (spd == 2 ? 3 : (spd == 1 ? 1 : 0));
                    port.signature = sig;
                    if (sig == 0x00000101) {
                        copyStr(port.type, "SATA HDD", sizeof(port.type));
                    } else if (sig == 0xEB140101) {
                        copyStr(port.type, "SATAPI", sizeof(port.type));
                    } else {
                        copyStr(port.type, "SATA Dev", sizeof(port.type));
                    }
                    ahciDriveCount++;
                }
            }
        }
    }

    void probeBlockIo(uefi::EfiBootServices* bs) noexcept {
        if (!bs || !bs->locateHandleBuffer || !bs->handleProtocol) return;
        blockDeviceCount = 0;
        physicalDiskCount = 0;
        uint64_t handleCount = 0;
        uefi::EfiHandle* handles = nullptr;
        uefi::EfiStatus status = bs->locateHandleBuffer(
            2, // ByProtocol
            &uefi::EFI_BLOCK_IO_PROTOCOL_GUID,
            nullptr,
            &handleCount,
            &handles
        );
        if (status != uefi::EFI_SUCCESS || !handles || handleCount == 0) return;

        alignas(512) static uint8_t sectorBuf[512];

        for (uint64_t i = 0; i < handleCount && blockDeviceCount < 8; ++i) {
            uefi::EfiBlockIoProtocol* bio = nullptr;
            if (bs->handleProtocol(handles[i], &uefi::EFI_BLOCK_IO_PROTOCOL_GUID, reinterpret_cast<void**>(&bio)) == uefi::EFI_SUCCESS && bio && bio->media) {
                auto& dev = blockDevices[blockDeviceCount++];
                dev.mediaId = bio->media->mediaId;
                dev.isPartition = bio->media->logicalPartition;
                dev.blockSize = bio->media->blockSize ? bio->media->blockSize : 512;
                dev.totalBlocks = bio->media->lastBlock + 1;
                dev.sizeGB = (dev.totalBlocks * dev.blockSize) / (1000ULL * 1000 * 1000);

                if (!dev.isPartition) {
                    physicalDiskCount++;
                    // Probe LBA 1 (GPT Header)
                    if (bio->readBlocks && dev.totalBlocks > 1 && dev.blockSize == 512) {
                        for (size_t b = 0; b < 512; ++b) sectorBuf[b] = 0;
                        if (bio->readBlocks(bio, dev.mediaId, 1, 512, sectorBuf) == uefi::EFI_SUCCESS) {
                            if (sectorBuf[0] == 'E' && sectorBuf[1] == 'F' && sectorBuf[2] == 'I' && sectorBuf[3] == ' ' &&
                                sectorBuf[4] == 'P' && sectorBuf[5] == 'A' && sectorBuf[6] == 'R' && sectorBuf[7] == 'T') {
                                dev.gptValid = true;
                                for (int g = 0; g < 16; ++g) {
                                    dev.gptGuid[g] = sectorBuf[56 + g];
                                }
                            }
                        }
                    }
                } else {
                    // Probe Partition LBA 2 (Offset 1024 bytes) for Linux Ext2/3/4 Superblock (0xEF53)
                    if (bio->readBlocks && dev.totalBlocks > 2 && dev.blockSize == 512) {
                        for (size_t b = 0; b < 512; ++b) sectorBuf[b] = 0;
                        if (bio->readBlocks(bio, dev.mediaId, 2, 512, sectorBuf) == uefi::EFI_SUCCESS) {
                            uint16_t magic = sectorBuf[56] | (static_cast<uint16_t>(sectorBuf[57]) << 8);
                            if (magic == 0xEF53) {
                                dev.isExtFs = true;
                                for (int n = 0; n < 16; ++n) {
                                    char c = static_cast<char>(sectorBuf[120 + n]);
                                    dev.extFsVolumeName[n] = (c >= 32 && c <= 126) ? c : '\0';
                                }
                                dev.extFsVolumeName[16] = '\0';
                            }
                        }
                    }
                }
            }
        }
        if (bs->freePool) {
            bs->freePool(handles);
        }
    }

    void scanPciBus() noexcept {
        pciDeviceCount = 0;
        for (uint8_t bus = 0; bus < 16 && pciDeviceCount < 64; ++bus) {
            for (uint8_t dev = 0; dev < 32 && pciDeviceCount < 64; ++dev) {
                uint32_t id0 = pciRead32(bus, dev, 0, 0x00);
                uint16_t v0 = static_cast<uint16_t>(id0 & 0xFFFF);
                if (v0 == 0xFFFF || v0 == 0x0000) continue;

                uint32_t hdr = pciRead32(bus, dev, 0, 0x0C);
                uint8_t headerType = static_cast<uint8_t>((hdr >> 16) & 0xFF);
                uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;

                for (uint8_t func = 0; func < maxFunc && pciDeviceCount < 64; ++func) {
                    uint32_t id = pciRead32(bus, dev, func, 0x00);
                    uint16_t vendor = static_cast<uint16_t>(id & 0xFFFF);
                    uint16_t device = static_cast<uint16_t>((id >> 16) & 0xFFFF);
                    if (vendor == 0xFFFF || vendor == 0x0000) continue;

                    uint32_t classReg = pciRead32(bus, dev, func, 0x08);
                    uint8_t cCode = static_cast<uint8_t>((classReg >> 24) & 0xFF);
                    uint8_t sCode = static_cast<uint8_t>((classReg >> 16) & 0xFF);

                    auto& d = pciDevices[pciDeviceCount++];
                    d.bus = bus; d.dev = dev; d.func = func;
                    d.vendorId = vendor; d.deviceId = device;
                    d.classCode = cCode; d.subclass = sCode;

                    if (cCode == 0x01 && sCode == 0x06) {
                        probeAhci(bus, dev, func);
                    }

                    if (vendor == 0x8086) {
                        if (device == 0x5918 || device == 0x1918 || device == 0x190f) {
                            copyStr(d.name, "Intel Xeon Host Bridge", sizeof(d.name));
                        } else if (device == 0x1901) {
                            copyStr(d.name, "Intel PCIe x16 Root Port", sizeof(d.name));
                        } else if (device == 0xa12f) {
                            copyStr(d.name, "Intel USB 3.0 xHCI", sizeof(d.name));
                        } else if (device == 0xa102) {
                            copyStr(d.name, "Intel C230 SATA AHCI", sizeof(d.name));
                        } else if (device == 0xa149) {
                            copyStr(d.name, "Intel C236 LPC/eSPI Bridge", sizeof(d.name));
                        } else if (device == 0x1533) {
                            copyStr(d.name, "Intel I210 Gigabit LAN", sizeof(d.name));
                        } else if (device == 0x15b7) {
                            copyStr(d.name, "Intel I219-LM Gigabit LAN", sizeof(d.name));
                        } else if (cCode == 0x06 && sCode == 0x04) {
                            copyStr(d.name, "Intel PCIe Root Port", sizeof(d.name));
                        } else if (cCode == 0x06) {
                            copyStr(d.name, "Intel Chipset Bridge", sizeof(d.name));
                        } else {
                            copyStr(d.name, "Intel Silicon Device", sizeof(d.name));
                        }
                    } else if (vendor == 0x1A03) {
                        copyStr(d.name, "ASPEED AST2400/2500 Video", sizeof(d.name));
                    } else if (cCode == 0x01 && sCode == 0x08) {
                        copyStr(d.name, "NVMe Express Controller", sizeof(d.name));
                    } else if (cCode == 0x01 && sCode == 0x06) {
                        copyStr(d.name, "SATA AHCI Controller", sizeof(d.name));
                    } else if (cCode == 0x02) {
                        copyStr(d.name, "Ethernet Network Controller", sizeof(d.name));
                    } else if (cCode == 0x03) {
                        copyStr(d.name, "VGA Display Controller", sizeof(d.name));
                    } else if (cCode == 0x0C && sCode == 0x03) {
                        copyStr(d.name, "USB Host Controller", sizeof(d.name));
                    } else {
                        copyStr(d.name, "PCI System Device", sizeof(d.name));
                    }
                }
            }
        }
    }

    static const char* getSmbiosString(const uint8_t* p, uint8_t idx) noexcept {
        if (idx == 0) return "";
        uint8_t len = p[1];
        const char* s = reinterpret_cast<const char*>(p + len);
        while (--idx > 0 && *s) {
            while (*s) ++s;
            ++s;
        }
        return s;
    }

    void parseSmbios(uefi::EfiSystemTable* st) noexcept {
        if (!st || !st->configurationTable) return;

        const uint8_t* tablePtr = nullptr;
        uint32_t tableLen = 0;

        for (uint64_t i = 0; i < st->numberOfTableEntries; ++i) {
            const auto& entry = st->configurationTable[i];
            if (entry.vendorGuid == uefi::SMBIOS3_TABLE_GUID && entry.vendorTable) {
                const auto* eps = reinterpret_cast<const uint8_t*>(entry.vendorTable);
                if (eps[0] == '_' && eps[1] == 'S' && eps[2] == 'M' && eps[3] == '3' && eps[4] == '_') {
                    tableLen = *reinterpret_cast<const uint32_t*>(eps + 0x0C);
                    tablePtr = *reinterpret_cast<const uint8_t* const*>(eps + 0x10);
                    break;
                }
            } else if (entry.vendorGuid == uefi::SMBIOS_TABLE_GUID && entry.vendorTable && !tablePtr) {
                const auto* eps = reinterpret_cast<const uint8_t*>(entry.vendorTable);
                if (eps[0] == '_' && eps[1] == 'S' && eps[2] == 'M' && eps[3] == '_') {
                    tableLen = *reinterpret_cast<const uint16_t*>(eps + 0x16);
                    uint32_t addr = *reinterpret_cast<const uint32_t*>(eps + 0x18);
                    tablePtr = reinterpret_cast<const uint8_t*>(static_cast<uint64_t>(addr));
                }
            }
        }

        if (!tablePtr || tableLen == 0) return;

        const uint8_t* p = tablePtr;
        const uint8_t* end = tablePtr + tableLen;

        while (p + 4 < end) {
            uint8_t type = p[0];
            uint8_t len = p[1];
            if (len < 4) break;
            if (type == 127) break;

            if (type == 0 && len >= 0x12) {
                copyStr(biosVendor, getSmbiosString(p, p[4]), sizeof(biosVendor));
                copyStr(biosVersion, getSmbiosString(p, p[5]), sizeof(biosVersion));
                copyStr(biosDate, getSmbiosString(p, p[8]), sizeof(biosDate));
                trimTrailingSpaces(biosVendor);
                trimTrailingSpaces(biosVersion);
                trimTrailingSpaces(biosDate);
            } else if (type == 1 && len >= 0x08) {
                copyStr(boardVendor, getSmbiosString(p, p[4]), sizeof(boardVendor));
                copyStr(boardProduct, getSmbiosString(p, p[5]), sizeof(boardProduct));
                trimTrailingSpaces(boardVendor);
                trimTrailingSpaces(boardProduct);
            } else if (type == 2 && len >= 0x08) {
                if (boardVendor[0] == '\0') {
                    copyStr(boardVendor, getSmbiosString(p, p[4]), sizeof(boardVendor));
                    trimTrailingSpaces(boardVendor);
                }
                if (boardProduct[0] == '\0') {
                    copyStr(boardProduct, getSmbiosString(p, p[5]), sizeof(boardProduct));
                    trimTrailingSpaces(boardProduct);
                }
            } else if (type == 4 && len >= 0x20) {
                copyStr(cpuSocket, getSmbiosString(p, p[4]), sizeof(cpuSocket));
                trimTrailingSpaces(cpuSocket);
                if (len >= 0x25) {
                    if (p[0x23] > 0) cpuPhysicalCores = p[0x23];
                    if (p[0x25] > 0) cpuLogicalThreads = p[0x25];
                }
            } else if (type == 17 && len >= 0x1C && dimmCount < 4) {
                uint16_t sizeVal = *reinterpret_cast<const uint16_t*>(p + 0x0C);
                if (sizeVal > 0 && sizeVal != 0xFFFF) {
                    auto& dimm = dimms[dimmCount++];
                    dimm.sizeMB = (sizeVal & 0x8000) ? (sizeVal & 0x7FFF) / 1024 : sizeVal;
                    if (len >= 0x17) {
                        dimm.speedMTs = *reinterpret_cast<const uint16_t*>(p + 0x15);
                    }
                    copyStr(dimm.locator, getSmbiosString(p, p[0x10]), sizeof(dimm.locator));
                    trimTrailingSpaces(dimm.locator);
                    if (len >= 0x1B) {
                        copyStr(dimm.manufacturer, getSmbiosString(p, p[0x17]), sizeof(dimm.manufacturer));
                        trimTrailingSpaces(dimm.manufacturer);
                    }
                }
            }

            p += len;
            while (p + 1 < end && !(p[0] == 0 && p[1] == 0)) {
                ++p;
            }
            p += 2;
        }
    }

    void parseAcpi(uefi::EfiSystemTable* st) noexcept {
        acpiTableCount = 0;
        acpiMadtLocalApics = 0;
        hpetAddress = 0xFED00000ULL;
        mcfgAddress = 0xE0000000ULL;

        const uint8_t* rsdpPtr = nullptr;

        // 1. Pass 1: Look for ACPI 2.0+ (XSDT) table GUID in UEFI configuration tables
        if (st && st->configurationTable) {
            for (uint64_t i = 0; i < st->numberOfTableEntries; ++i) {
                const auto& entry = st->configurationTable[i];
                if (entry.vendorGuid == uefi::ACPI_20_TABLE_GUID && entry.vendorTable) {
                    rsdpPtr = reinterpret_cast<const uint8_t*>(entry.vendorTable);
                    break;
                }
            }
            // Pass 2: If no ACPI 2.0 GUID found, look for ACPI 1.0 (RSDT) GUID
            if (!rsdpPtr) {
                for (uint64_t i = 0; i < st->numberOfTableEntries; ++i) {
                    const auto& entry = st->configurationTable[i];
                    if (entry.vendorGuid == uefi::ACPI_10_TABLE_GUID && entry.vendorTable) {
                        rsdpPtr = reinterpret_cast<const uint8_t*>(entry.vendorTable);
                        break;
                    }
                }
            }
        }

        // 2. Physical Memory Scan Fallback (Ring 0 / Long Mode physical address space)
        // If UEFI configuration table failed to provide a valid pointer, scan EBDA & BIOS ROM
        if (!rsdpPtr || rsdpPtr[0] != 'R' || rsdpPtr[1] != 'S' || rsdpPtr[2] != 'D' || rsdpPtr[3] != ' ') {
            // Scan EBDA: read segment from physical address 0x40E
            uint16_t ebdaSeg = *reinterpret_cast<const volatile uint16_t*>(0x40E);
            uint64_t ebdaBase = static_cast<uint64_t>(ebdaSeg) << 4;
            if (ebdaBase >= 0x80000 && ebdaBase < 0xA0000) {
                for (uint64_t addr = ebdaBase; addr < ebdaBase + 1024; addr += 16) {
                    const auto* p = reinterpret_cast<const uint8_t*>(addr);
                    if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
                        p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
                        rsdpPtr = p;
                        break;
                    }
                }
            }
            // Scan BIOS ROM Area (0xE0000 to 0xFFFFF)
            if (!rsdpPtr) {
                for (uint64_t addr = 0xE0000; addr < 0x100000; addr += 16) {
                    const auto* p = reinterpret_cast<const uint8_t*>(addr);
                    if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
                        p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
                        rsdpPtr = p;
                        break;
                    }
                }
            }
        }

        if (!rsdpPtr) {
            UartSerial::print("[MicaNT ACPI] ERROR: RSDP not found in UEFI config tables or BIOS memory!\r\n");
            return;
        }

        if (rsdpPtr[0] != 'R' || rsdpPtr[1] != 'S' || rsdpPtr[2] != 'D' || rsdpPtr[3] != ' ' ||
            rsdpPtr[4] != 'P' || rsdpPtr[5] != 'T' || rsdpPtr[6] != 'R' || rsdpPtr[7] != ' ') {
            UartSerial::print("[MicaNT ACPI] ERROR: Invalid RSDP signature!\r\n");
            return;
        }

        // Verify ACPI 1.0 checksum (first 20 bytes)
        uint8_t csum20 = 0;
        for (int i = 0; i < 20; ++i) csum20 = static_cast<uint8_t>(csum20 + rsdpPtr[i]);
        if (csum20 != 0) {
            UartSerial::print("[MicaNT ACPI] WARNING: RSDP 20-byte checksum mismatch!\r\n");
        }

        uint8_t rev = rsdpPtr[15];
        UartSerial::print("[MicaNT ACPI] RSDP found at 0x");
        char hbuf[18];
        formatHex32(hbuf, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rsdpPtr)));
        UartSerial::print(hbuf);
        UartSerial::print(" | Revision: ");
        UartSerial::printDec(rev);
        UartSerial::print("\r\n");

        auto parseTableAt = [&](const uint8_t* tbl) {
            if (!tbl) return;
            for (int c = 0; c < 4; ++c) {
                if (tbl[c] < 32 || tbl[c] > 126) return; // Printable ASCII only
            }
            if (acpiTableCount < 16) {
                for (size_t existing = 0; existing < acpiTableCount; ++existing) {
                    if (acpiTables[existing][0] == static_cast<char>(tbl[0]) &&
                        acpiTables[existing][1] == static_cast<char>(tbl[1]) &&
                        acpiTables[existing][2] == static_cast<char>(tbl[2]) &&
                        acpiTables[existing][3] == static_cast<char>(tbl[3])) {
                        return; // Avoid duplicates
                    }
                }

                for (int c = 0; c < 4; ++c) acpiTables[acpiTableCount][c] = static_cast<char>(tbl[c]);
                acpiTables[acpiTableCount][4] = '\0';

                UartSerial::print("  -> ACPI Table ");
                UartSerial::printDec(static_cast<uint32_t>(acpiTableCount));
                UartSerial::print(": [");
                UartSerial::print(acpiTables[acpiTableCount]);
                UartSerial::print("] @ 0x");
                formatHex32(hbuf, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tbl)));
                UartSerial::print(hbuf);
                UartSerial::print("\r\n");

                if (tbl[0] == 'A' && tbl[1] == 'P' && tbl[2] == 'I' && tbl[3] == 'C') {
                    // MADT table
                    uint32_t tLen = *reinterpret_cast<const uint32_t*>(tbl + 4);
                    uint32_t lapicPhys = *reinterpret_cast<const uint32_t*>(tbl + 0x24);
                    if (lapicPhys) lapicBaseAddr = lapicPhys;

                    const uint8_t* ent = tbl + 0x2C;
                    const uint8_t* entEnd = tbl + tLen;
                    while (ent + 2 <= entEnd) {
                        uint8_t eType = ent[0];
                        uint8_t eLen = ent[1];
                        if (eLen < 2) break;
                        if (eType == 0 && eLen >= 8) { // Processor Local APIC
                            uint32_t flags = *reinterpret_cast<const uint32_t*>(ent + 4);
                            if (flags & 1) acpiMadtLocalApics++;
                        } else if (eType == 9 && eLen >= 16) { // Processor Local x2APIC
                            uint32_t flags = *reinterpret_cast<const uint32_t*>(ent + 8);
                            if (flags & 1) acpiMadtLocalApics++;
                        } else if (eType == 1 && eLen >= 12) { // I/O APIC
                            ioApicAddress = *reinterpret_cast<const uint32_t*>(ent + 4);
                        }
                        ent += eLen;
                    }
                } else if (tbl[0] == 'H' && tbl[1] == 'P' && tbl[2] == 'E' && tbl[3] == 'T') {
                    hpetAddress = *reinterpret_cast<const uint64_t*>(tbl + 0x2C + 4);
                } else if (tbl[0] == 'M' && tbl[1] == 'C' && tbl[2] == 'F' && tbl[3] == 'G') {
                    mcfgAddress = *reinterpret_cast<const uint64_t*>(tbl + 0x2C);
                }
                acpiTableCount++;
            }
        };

        // 3. Try XSDT (64-bit pointers) first if rev >= 2
        if (rev >= 2) {
            uint64_t xsdtAddr = *reinterpret_cast<const uint64_t*>(rsdpPtr + 24);
            if (xsdtAddr) {
                const auto* xsdt = reinterpret_cast<const uint8_t*>(xsdtAddr);
                if (xsdt[0] == 'X' && xsdt[1] == 'S' && xsdt[2] == 'D' && xsdt[3] == 'T') {
                    uint32_t len = *reinterpret_cast<const uint32_t*>(xsdt + 4);
                    size_t count = (len > 36) ? (len - 36) / 8 : 0;
                    UartSerial::print("[MicaNT ACPI] Parsing XSDT @ 0x");
                    formatHex32(hbuf, static_cast<uint32_t>(xsdtAddr));
                    UartSerial::print(hbuf);
                    UartSerial::print(" (");
                    UartSerial::printDec(static_cast<uint32_t>(count));
                    UartSerial::print(" tables)\r\n");

                    for (size_t k = 0; k < count && acpiTableCount < 16; ++k) {
                        uint64_t tableAddr = *reinterpret_cast<const uint64_t*>(xsdt + 36 + k * 8);
                        if (tableAddr) {
                            parseTableAt(reinterpret_cast<const uint8_t*>(tableAddr));
                        }
                    }
                }
            }
        }

        // 4. Fallback: Parse RSDT (32-bit pointers) if XSDT yielded 0 tables
        if (acpiTableCount == 0) {
            uint32_t rsdtAddr = *reinterpret_cast<const uint32_t*>(rsdpPtr + 16);
            if (rsdtAddr) {
                const auto* rsdt = reinterpret_cast<const uint8_t*>(static_cast<uint64_t>(rsdtAddr));
                if (rsdt[0] == 'R' && rsdt[1] == 'S' && rsdt[2] == 'D' && rsdt[3] == 'T') {
                    uint32_t len = *reinterpret_cast<const uint32_t*>(rsdt + 4);
                    size_t count = (len > 36) ? (len - 36) / 4 : 0;
                    UartSerial::print("[MicaNT ACPI] Parsing RSDT @ 0x");
                    formatHex32(hbuf, rsdtAddr);
                    UartSerial::print(hbuf);
                    UartSerial::print(" (");
                    UartSerial::printDec(static_cast<uint32_t>(count));
                    UartSerial::print(" tables)\r\n");

                    for (size_t k = 0; k < count && acpiTableCount < 16; ++k) {
                        uint32_t tableAddr = *reinterpret_cast<const uint32_t*>(rsdt + 36 + k * 4);
                        if (tableAddr) {
                            parseTableAt(reinterpret_cast<const uint8_t*>(static_cast<uint64_t>(tableAddr)));
                        }
                    }
                }
            }
        }
    }

    void logToSerial() const noexcept {
        UartSerial::print("\r\n=======================================================\r\n");
        UartSerial::print("[MicaNT Hardware] Motherboard: ");
        UartSerial::print(boardVendor);
        UartSerial::print(" ");
        UartSerial::print(boardProduct);
        UartSerial::print("\r\n[MicaNT Hardware] BIOS: ");
        UartSerial::print(biosVendor);
        UartSerial::print(" ");
        UartSerial::print(biosVersion);
        UartSerial::print(" (");
        UartSerial::print(biosDate);
        UartSerial::print(")\r\n");

        UartSerial::print("[MicaNT Hardware] CPU: ");
        UartSerial::print(cpuBrand);
        UartSerial::print(" | Measured TSC: ");
        UartSerial::printDec(cpuMeasuredMHz);
        UartSerial::print(" MHz\r\n");

        UartSerial::print("[MicaNT Hardware] Topology: ");
        UartSerial::printDec(cpuPhysicalCores);
        UartSerial::print(" Physical Cores, ");
        UartSerial::printDec(cpuLogicalThreads);
        UartSerial::print(" Logical Threads (MADT APICs: ");
        UartSerial::printDec(acpiMadtLocalApics);
        UartSerial::print(")\r\n");

        UartSerial::print("[MicaNT Hardware] LAPIC: Base=0x");
        char hbufLapic[12];
        formatHex32(hbufLapic, static_cast<uint32_t>(lapicBaseAddr));
        UartSerial::print(hbufLapic);
        UartSerial::print(" (Enabled: ");
        UartSerial::print(lapicEnabled ? "YES" : "NO");
        UartSerial::print(", ID=");
        UartSerial::printDec(lapicId);
        UartSerial::print(", Version=0x");
        char hbufVer[6];
        formatHex16(hbufVer, static_cast<uint16_t>(lapicVersion));
        UartSerial::print(hbufVer);
        UartSerial::print(") | IO-APIC: 0x");
        formatHex32(hbufLapic, ioApicAddress);
        UartSerial::print(hbufLapic);
        UartSerial::print("\r\n");

        UartSerial::print("[MicaNT Hardware] RAM: ");
        UartSerial::printDec(static_cast<uint32_t>(ramTotalMB));
        UartSerial::print(" MB Total, ");
        UartSerial::printDec(static_cast<uint32_t>(ramFreeMB));
        UartSerial::print(" MB Free (");
        UartSerial::printDec(static_cast<uint32_t>(dimmCount));
        UartSerial::print(" DIMMs)\r\n");

        UartSerial::print("[MicaNT Hardware] ACPI Tables (");
        UartSerial::printDec(static_cast<uint32_t>(acpiTableCount));
        UartSerial::print("): ");
        for (size_t i = 0; i < acpiTableCount; ++i) {
            UartSerial::print(acpiTables[i]);
            UartSerial::print(" ");
        }
        UartSerial::print("\r\n");

        UartSerial::print("[MicaNT Hardware] Physical PCI Devices (");
        UartSerial::printDec(static_cast<uint32_t>(pciDeviceCount));
        UartSerial::print("):\r\n");
        for (size_t i = 0; i < pciDeviceCount; ++i) {
            const auto& d = pciDevices[i];
            UartSerial::print("  -> ");
            UartSerial::print(d.bus < 10 ? "0" : "");
            UartSerial::printDec(d.bus);
            UartSerial::print(":");
            UartSerial::print(d.dev < 10 ? "0" : "");
            UartSerial::printDec(d.dev);
            UartSerial::print(".");
            UartSerial::printDec(d.func);
            UartSerial::print(" [");
            char hbuf[8];
            formatHex16(hbuf, d.vendorId);
            UartSerial::print(hbuf);
            UartSerial::print(":");
            formatHex16(hbuf, d.deviceId);
            UartSerial::print(hbuf);
            UartSerial::print("] ");
            UartSerial::print(d.name);
            UartSerial::print("\r\n");
        }

        if (ahciFound) {
            UartSerial::print("[MicaNT Hardware] AHCI SATA Controller: BAR5=0x");
            char hbuf[12];
            formatHex32(hbuf, ahciBar5);
            UartSerial::print(hbuf);
            UartSerial::print(" | Version: 0x");
            formatHex32(hbuf, ahciVersion);
            UartSerial::print(hbuf);
            UartSerial::print(" | Ports Implemented: 0x");
            formatHex32(hbuf, ahciPortsImplemented);
            UartSerial::print(hbuf);
            UartSerial::print("\r\n");
            for (uint8_t p = 0; p < 8; ++p) {
                if (ahciPorts[p].present) {
                    UartSerial::print("  -> AHCI Port ");
                    UartSerial::printDec(p);
                    UartSerial::print(": ");
                    UartSerial::print(ahciPorts[p].type);
                    UartSerial::print(" @ ");
                    UartSerial::printDec(ahciPorts[p].speedGbps);
                    UartSerial::print(".0 Gbps (SIG=0x");
                    formatHex32(hbuf, ahciPorts[p].signature);
                    UartSerial::print(hbuf);
                    UartSerial::print(")\r\n");
                }
            }
        }

        if (blockDeviceCount > 0) {
            UartSerial::print("[MicaNT Hardware] UEFI Block I/O: ");
            UartSerial::printDec(static_cast<uint32_t>(physicalDiskCount));
            UartSerial::print(" Physical Disks (");
            UartSerial::printDec(static_cast<uint32_t>(blockDeviceCount));
            UartSerial::print(" Total Handles):\r\n");
            for (size_t b = 0; b < blockDeviceCount; ++b) {
                const auto& bd = blockDevices[b];
                UartSerial::print("  -> Block Device ");
                UartSerial::printDec(static_cast<uint32_t>(b));
                UartSerial::print(": MediaId=");
                UartSerial::printDec(bd.mediaId);
                UartSerial::print(bd.isPartition ? " [Partition]" : " [Physical Disk]");
                UartSerial::print(" Size=");
                UartSerial::printDec(static_cast<uint32_t>(bd.sizeGB));
                UartSerial::print(" GB (Sectors: ");
                UartSerial::printDec(static_cast<uint32_t>(bd.totalBlocks));
                UartSerial::print(", BlockSize: ");
                UartSerial::printDec(bd.blockSize);
                UartSerial::print(")");
                if (bd.gptValid) {
                    UartSerial::print(" [GPT Header Verified OK]");
                }
                if (bd.isExtFs) {
                    UartSerial::print(" [Ext4 0xEF53 '");
                    UartSerial::print(bd.extFsVolumeName);
                    UartSerial::print("' Auto-Mounted Read-Only as E:\\]");
                }
                UartSerial::print("\r\n");
            }
        }

        UartSerial::print("[MicaNT Hardware] Intel DTS On-Die Core Temperature: ");
        UartSerial::printDec(cpuTempC);
        UartSerial::print(" C\r\n");
        UartSerial::print("=======================================================\r\n");
    }
};


// ============================================================================
// Pre-loaded System Images & FAT32 Staging Buffers
// ============================================================================
alignas(4096) static uint8_t s_userPeFileBuffer[16777216]; // 16 MB
static size_t s_userPeFileSize = 0;
static bool s_userPeFileFromDisk = false;
static micant::um::UserLoadedImageInfo s_loadedPeInfo{};

alignas(4096) static uint8_t s_ntoskrnlFileBuffer[16777216]; // 16 MB
alignas(4096) static uint8_t s_ntoskrnlImageBuffer[201326592]; // 192 MB Kernel Image Buffer (kernel image includes userland heaps)
static size_t s_ntoskrnlFileSize = 0;
static bool s_ntoskrnlLoadedFromDisk = false;
static uint64_t s_ntoskrnlEntryPoint = 0;
static micant::km::executive::NtoskrnlLoadedImageInfo s_loadedNtoskrnlInfo{};

constexpr size_t kMaxBootOemDriverImages = micant::boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES;
constexpr size_t kMaxBootOemDriverImageBytes = 16777216;
alignas(4096) static uint8_t s_bootOemDriverFileBuffer[kMaxBootOemDriverImageBytes];
alignas(4096) static uint8_t
    s_bootOemDriverImageBuffers[kMaxBootOemDriverImages][kMaxBootOemDriverImageBytes];

extern "C" char* strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return nullptr;
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; ++haystack) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) {
                ++h;
                ++n;
            }
            if (!*n) return const_cast<char*>(haystack);
        }
    }
    return nullptr;
}

static size_t loadFileFromEfiSystemPartition(
    micant::uefi::EfiHandle imageHandle,
    micant::uefi::EfiBootServices* bs,
    const wchar_t* filePath,
    uint8_t* outBuffer,
    size_t maxBufferSize
) {
    if (!bs || !filePath || !outBuffer || maxBufferSize == 0) {
        return 0;
    }

    auto tryReadFile = [&](micant::uefi::EfiSimpleFileSystemProtocol* fs) -> size_t {
        if (!fs || !fs->openVolume) return 0;
        micant::uefi::EfiFileProtocol* rootDir = nullptr;
        if (fs->openVolume(fs, &rootDir) != micant::uefi::EFI_SUCCESS || !rootDir || !rootDir->open) {
            return 0;
        }

        const wchar_t* altPath = (filePath[0] == L'\\') ? (filePath + 1) : nullptr;
        const wchar_t* pathsToTry[2] = { filePath, altPath };

        for (const auto* pth : pathsToTry) {
            if (!pth) continue;
            micant::uefi::EfiFileProtocol* fileHandle = nullptr;
            if (rootDir->open(rootDir, &fileHandle, pth, micant::uefi::EFI_FILE_MODE_READ, 0) == micant::uefi::EFI_SUCCESS && fileHandle && fileHandle->read) {
                uint64_t readSize = maxBufferSize;
                micant::uefi::EfiStatus stRead = fileHandle->read(fileHandle, &readSize, outBuffer);
                if (fileHandle->close) fileHandle->close(fileHandle);
                if (rootDir->close) rootDir->close(rootDir);
                if (stRead == micant::uefi::EFI_SUCCESS && readSize > 0) {
                    return static_cast<size_t>(readSize);
                }
                return 0;
            }
        }
        if (rootDir->close) rootDir->close(rootDir);
        return 0;
    };

    // First attempt: Check the filesystem of the image handle itself
    if (bs->handleProtocol && imageHandle) {
        micant::uefi::EfiLoadedImageProtocol* loadedImage = nullptr;
        if (bs->handleProtocol(imageHandle, &micant::uefi::EFI_LOADED_IMAGE_PROTOCOL_GUID, reinterpret_cast<void**>(&loadedImage)) == micant::uefi::EFI_SUCCESS && loadedImage && loadedImage->deviceHandle) {
            micant::uefi::EfiSimpleFileSystemProtocol* fs = nullptr;
            if (bs->handleProtocol(loadedImage->deviceHandle, &micant::uefi::EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, reinterpret_cast<void**>(&fs)) == micant::uefi::EFI_SUCCESS && fs) {
                size_t sz = tryReadFile(fs);
                if (sz > 0) return sz;
            }
        }
    }

    // Second attempt: Probe ALL mounted UEFI filesystems across all drives
    if (bs->locateHandleBuffer) {
        micant::uefi::EfiHandle* handles = nullptr;
        uint64_t numHandles = 0;
        if (bs->locateHandleBuffer(micant::uefi::BY_PROTOCOL, &micant::uefi::EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, nullptr, &numHandles, &handles) == micant::uefi::EFI_SUCCESS && handles && numHandles > 0) {
            for (uint64_t i = 0; i < numHandles; ++i) {
                micant::uefi::EfiSimpleFileSystemProtocol* fs = nullptr;
                if (bs->handleProtocol(handles[i], &micant::uefi::EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, reinterpret_cast<void**>(&fs)) == micant::uefi::EFI_SUCCESS && fs) {
                    size_t sz = tryReadFile(fs);
                    if (sz > 0) {
                        return sz;
                    }
                }
            }
        }
    }

    return 0;
}

class UefiBootEngine {
private:
    uefi::EfiHandle m_imageHandle{nullptr};
    uefi::EfiSystemTable* m_systemTable{nullptr};
    uefi::EfiGraphicsOutputProtocol* m_gop{nullptr};
    boot::LoaderParameterBlock m_lpb{};
    boot::UefiKernelHandoffV8 m_handoff{};
    bootvid::BootVideoDriver m_videoDriver{};
    uefi::EfiTime m_bootTime{};
    bool m_hasBootTime{false};
    bool m_bootServicesExited{false};

    RealHardwareTelemetry m_hw{};
    char m_cpuBrand[49]{};
    uint32_t m_cpuFreqMHz{0};
    uint32_t m_cpuTempC{0};
    uint64_t m_totalRamMB{0};
    uint64_t m_freeRamMB{0};
    uint64_t m_usedRamMB{0};
    uint64_t m_fbBase{0xC0000000};
    uint64_t m_fbSize{16777216};

public:
    UefiBootEngine(uefi::EfiHandle imageHandle, uefi::EfiSystemTable* systemTable)
        : m_imageHandle(imageHandle), m_systemTable(systemTable) {}

    void setRealRamMetrics(uint64_t totalBytes, uint64_t freeBytes) noexcept {
        m_totalRamMB = totalBytes / (1024 * 1024);
        m_freeRamMB = freeBytes / (1024 * 1024);
        m_usedRamMB = (m_totalRamMB > m_freeRamMB) ? (m_totalRamMB - m_freeRamMB) : 0;
        m_hw.ramTotalMB = m_totalRamMB;
        m_hw.ramFreeMB = m_freeRamMB;
        m_hw.ramUsedMB = m_usedRamMB;
    }

    void detectSiliconTelemetry() noexcept {
        getCpuBrandString(m_hw.cpuBrand);
        copyStr(m_cpuBrand, m_hw.cpuBrand, sizeof(m_cpuBrand));

        m_hw.probeCpuTopology();
        m_hw.probeLapicRing0();

        if (m_systemTable && m_systemTable->bootServices) {
            const uint64_t tscFrequencyHz =
                measureTscFrequencyHz(m_systemTable->bootServices);
            m_hw.cpuMeasuredMHz = static_cast<uint32_t>(tscFrequencyHz / 1'000'000);
            m_cpuFreqMHz = m_hw.cpuMeasuredMHz;
            if (tscFrequencyHz) {
                using micant::shared::boot_time::BootClockFlagsV1;
                m_handoff.pci.clock.clockCalibration.tscFrequencyHz = tscFrequencyHz;
                m_handoff.pci.clock.clockCalibration.flags = BootClockFlagsV1::InvariantTsc;
            }
        }

        m_hw.scanPciBus();
        m_hw.parseSmbios(m_systemTable);
        m_hw.parseAcpi(m_systemTable);
        if (m_systemTable && m_systemTable->bootServices) {
            m_hw.probeBlockIo(m_systemTable->bootServices);
        }
        m_cpuTempC = getCpuTemperatureC();
        m_hw.cpuTempC = m_cpuTempC;

        m_hw.logToSerial();
    }

    /**
     * @brief Stage 1: Firmware Console Handshake & Hardware RTC Query
     */
    bool initConsole() {
        if (m_systemTable && m_systemTable->runtimeServices && m_systemTable->runtimeServices->getTime) {
            if (m_systemTable->runtimeServices->getTime(&m_bootTime, nullptr) == uefi::EFI_SUCCESS) {
                m_hasBootTime = true;
            }
        }
        if (!m_systemTable || !m_systemTable->conOut) {
            return false;
        }
        m_systemTable->conOut->outputString(
            m_systemTable->conOut,
            L"[MicaNT Boot] Bare-Metal UEFI Bootloader (ISO C++23)\r\n"
        );
        return true;
    }

    /**
     * @brief Stage 2: Locate UEFI Graphics Output Protocol (GOP) & Init Framebuffer
     */
    bool initGopFramebuffer() {
        if (!m_systemTable || !m_systemTable->bootServices) {
            return false;
        }

        void* gopInterface = nullptr;
        uefi::EfiStatus status = m_systemTable->bootServices->locateProtocol(
            &uefi::EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
            nullptr,
            &gopInterface
        );

        if (status != uefi::EFI_SUCCESS || !gopInterface) {
            if (m_systemTable->conOut) {
                m_systemTable->conOut->outputString(
                    m_systemTable->conOut,
                    L"[MicaNT Boot] WARNING: GOP protocol not found. Operating in headless mode.\r\n"
                );
            }
            return false;
        }

        m_gop = static_cast<uefi::EfiGraphicsOutputProtocol*>(gopInterface);

        if (m_gop && m_gop->queryMode && m_gop->setMode && m_gop->mode) {
            uint32_t maxMode = m_gop->mode->maxMode;
            uint32_t currentMode = m_gop->mode->mode;
            uint32_t bestMode = currentMode;
            uint32_t bestW = m_gop->mode->info ? m_gop->mode->info->horizontalResolution : 0;
            uint32_t bestH = m_gop->mode->info ? m_gop->mode->info->verticalResolution : 0;
            uint32_t requestedW = 0;
            uint32_t requestedH = 0;
            char displayConfig[128]{};
            const size_t displayConfigSize = loadFileFromEfiSystemPartition(
                m_imageHandle, m_systemTable->bootServices,
                L"\\EFI\\MicaNT\\boot.ini",
                reinterpret_cast<uint8_t*>(displayConfig), sizeof(displayConfig) - 1);
            if (displayConfigSize) {
                displayConfig[displayConfigSize] = '\0';
                const char* setting = strstr(displayConfig, "resolution=");
                if (setting) {
                    setting += sizeof("resolution=") - 1;
                    while (*setting >= '0' && *setting <= '9')
                        requestedW = requestedW * 10 + static_cast<uint32_t>(*setting++ - '0');
                    if (*setting == 'x' || *setting == 'X') {
                        ++setting;
                        while (*setting >= '0' && *setting <= '9')
                            requestedH = requestedH * 10 + static_cast<uint32_t>(*setting++ - '0');
                    }
                }
            }

            UartSerial::print("[MicaNT GOP] Probing GOP Modes (Total: ");
            UartSerial::printDec(maxMode);
            UartSerial::print(", Current: ");
            UartSerial::printDec(currentMode);
            UartSerial::print(" - ");
            UartSerial::printDec(bestW);
            UartSerial::print("x");
            UartSerial::printDec(bestH);
            UartSerial::print("):\r\n");

            for (uint32_t m = 0; m < maxMode; ++m) {
                uint64_t infoSize = sizeof(uefi::EfiGraphicsOutputModeInformation);
                uefi::EfiGraphicsOutputModeInformation* info = nullptr;
                if (m_gop->queryMode(m_gop, m, &infoSize, &info) == uefi::EFI_SUCCESS && info) {
                    UartSerial::print("  -> GOP Mode ");
                    UartSerial::printDec(m);
                    UartSerial::print(": ");
                    UartSerial::printDec(info->horizontalResolution);
                    UartSerial::print("x");
                    UartSerial::printDec(info->verticalResolution);
                    UartSerial::print(" (PPSL: ");
                    UartSerial::printDec(info->pixelsPerScanLine);
                    UartSerial::print(")\r\n");

                    if (requestedW && requestedH &&
                        info->horizontalResolution == requestedW &&
                        info->verticalResolution == requestedH) {
                        bestMode = m;
                        bestW = requestedW;
                        bestH = requestedH;
                    } else if (!requestedW && info->horizontalResolution == 1024 &&
                               info->verticalResolution == 768) {
                        bestMode = m;
                        bestW = 1024;
                        bestH = 768;
                    }

                    if (m_systemTable->bootServices && m_systemTable->bootServices->freePool) {
                        m_systemTable->bootServices->freePool(info);
                    }
                }
            }

            if (bestMode != currentMode) {
                UartSerial::print("[MicaNT GOP] Switching to Mode ");
                UartSerial::printDec(bestMode);
                UartSerial::print(" (");
                UartSerial::printDec(bestW);
                UartSerial::print("x");
                UartSerial::printDec(bestH);
                UartSerial::print(")...\r\n");
                m_gop->setMode(m_gop, bestMode);
            }
        }

        if (m_gop && m_gop->mode && m_gop->mode->info) {
            m_fbBase = m_gop->mode->frameBufferBase;
            m_fbSize = m_gop->mode->frameBufferSize;
            boot::FramebufferDescriptor desc{
                .physicalBase = m_gop->mode->frameBufferBase,
                .size = m_gop->mode->frameBufferSize,
                .width = m_gop->mode->info->horizontalResolution,
                .height = m_gop->mode->info->verticalResolution,
                .pixelsPerScanLine = m_gop->mode->info->pixelsPerScanLine,
                .pixelFormat = static_cast<uint32_t>(m_gop->mode->info->pixelFormat)
            };
            m_videoDriver.initialize(desc);
            return true;
        }

        return false;
    }

    struct DesktopLayout {
        uint32_t colWidth;
        uint32_t rowHeight;
        uint32_t col1X;
        uint32_t col2X;
        uint32_t row1Y;
        uint32_t row2Y;
    };

    DesktopLayout getDesktopLayout() const noexcept {
        uint32_t width = m_videoDriver.getWidth();
        uint32_t height = m_videoDriver.getHeight();
        uint32_t marginX = 14;
        uint32_t gapX = 12;
        uint32_t colWidth = (width > (2 * marginX + gapX)) ? ((width - 2 * marginX - gapX) / 2) : 480;
        uint32_t col1X = marginX;
        uint32_t col2X = marginX + colWidth + gapX;

        uint32_t topBarH = 28;
        uint32_t taskbarH = 34;
        uint32_t marginY = 6;
        uint32_t gapY = 8;
        uint32_t totalAvailH = (height > (topBarH + taskbarH + 2 * marginY + gapY))
                               ? (height - topBarH - taskbarH - 2 * marginY - gapY) : 660;
        uint32_t rowHeight = totalAvailH / 2;
        uint32_t row1Y = topBarH + marginY;
        uint32_t row2Y = row1Y + rowHeight + gapY;

        return { colWidth, rowHeight, col1X, col2X, row1Y, row2Y };
    }

    /**
     * @brief Stage 3: Display Boot Splash
     */
    void displaySplash(std::string_view statusText, float progress, const bootvid::BmpImage* customLogo = nullptr) {
        if (m_videoDriver.isInitialized()) {
            m_videoDriver.renderBootSplash(statusText, progress, customLogo);
        }
    }

    void captureBootTimeSample() noexcept {
        using micant::shared::boot_time::SampleSource;
        using micant::shared::boot_time::SampleStatus;
        using micant::shared::time::EfiAdjustForDaylight;
        using micant::shared::time::EfiInDaylight;
        using micant::shared::time::EfiUnspecifiedTimeZone;
        m_handoff.pci.clock.time.systemTime = {};
        m_hasBootTime = m_systemTable && m_systemTable->runtimeServices &&
            m_systemTable->runtimeServices->getTime &&
            m_systemTable->runtimeServices->getTime(&m_bootTime, nullptr) ==
                uefi::EFI_SUCCESS;
        if (!m_hasBootTime) return;

        m_handoff.pci.clock.time.systemTime.source = SampleSource::UefiRuntimeGetTime;
        m_handoff.pci.clock.time.systemTime.timeZoneMinutes = m_bootTime.TimeZone;
        m_handoff.pci.clock.time.systemTime.daylight = m_bootTime.Daylight;
        micant::shared::time::SystemTimeFields fields{
            m_bootTime.Year, m_bootTime.Month, m_bootTime.Day,
            m_bootTime.Hour, m_bootTime.Minute, m_bootTime.Second,
            static_cast<uint16_t>(m_bootTime.Nanosecond / 1'000'000), 0,
            static_cast<uint16_t>((m_bootTime.Nanosecond / 100) % 10'000)};
        const bool dateValid = m_bootTime.Year >= 1900 &&
            m_bootTime.Nanosecond < 1'000'000'000 &&
            micant::shared::time::isValid(fields);
        if ((m_bootTime.Daylight & ~(EfiAdjustForDaylight | EfiInDaylight)) != 0 ||
            !dateValid) {
            m_handoff.pci.clock.time.systemTime.status = SampleStatus::InvalidFirmwareData;
        } else if (static_cast<uint16_t>(m_bootTime.TimeZone) == EfiUnspecifiedTimeZone) {
            // EFI_TIME with an unspecified zone is local wall time. Preserve it
            // as such; never silently label it UTC in the kernel.
            m_handoff.pci.clock.time.systemTime.status = SampleStatus::LocalTimeZoneUnspecified;
        } else if (m_bootTime.TimeZone < -1440 || m_bootTime.TimeZone > 1440) {
            m_handoff.pci.clock.time.systemTime.status = SampleStatus::InvalidFirmwareData;
        } else {
            uint64_t utc{};
            if (!micant::shared::time::toUtc100ns(fields, m_bootTime.TimeZone, utc)) {
                m_handoff.pci.clock.time.systemTime.status = SampleStatus::InvalidFirmwareData;
            } else {
                m_handoff.pci.clock.time.systemTime.utcTime100ns = utc;
                m_handoff.pci.clock.time.systemTime.status = SampleStatus::UtcValid;
            }
        }
    }

    // Refresh immediately before ExitBootServices so the kernel receives the
    // latest firmware time instead of the earlier boot-menu timestamp.
    void refreshBootTimeSample() noexcept { captureBootTimeSample(); }

    /**
     * @brief Stage 4: Retrieve UEFI Memory Map and Build LPB
     */
    bool buildLoaderParameterBlock(std::span<const uefi::EfiMemoryDescriptor> memoryMap) {
        m_lpb = uefi::BuildLpbFromUefi(m_systemTable, m_gop, memoryMap);
        m_handoff.pci.clock.time.oem.legacy = uefi::BuildKernelHandoffFromUefi(m_systemTable, m_gop, memoryMap);
        m_handoff.pci.clock.time.oem.legacy.version = boot::UEFI_KERNEL_HANDOFF_VERSION_DYNAMIC_PFN;
        m_handoff.pci.clock.time.oem.legacy.size = sizeof(m_handoff);
        m_handoff.pci.clock.time.oem.moduleCapacity = boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES;
        captureBootTimeSample();
        return true;
    }

    void updateMemoryMap(const void* memoryMap, uint64_t memoryMapSize,
                         uint64_t descriptorSize, uint32_t descriptorVersion) noexcept {
        m_handoff.pci.clock.time.oem.legacy.memoryMapAddress = reinterpret_cast<uint64_t>(memoryMap);
        m_handoff.pci.clock.time.oem.legacy.memoryMapSize = memoryMapSize;
        m_handoff.pci.clock.time.oem.legacy.memoryMapDescriptorSize = static_cast<uint32_t>(descriptorSize);
        m_handoff.pci.clock.time.oem.legacy.memoryMapDescriptorVersion = descriptorVersion;
    }

    void setPfnBootstrapStorage(void* metadata, uint64_t metadataSize,
                                void* ranges, uint64_t rangeCapacity) noexcept {
        m_handoff.pfnMetadataAddress = reinterpret_cast<uint64_t>(metadata);
        m_handoff.pfnMetadataSize = metadataSize;
        m_handoff.availableRangesAddress = reinterpret_cast<uint64_t>(ranges);
        m_handoff.availableRangesCapacity = rangeCapacity;
    }

    bool addBootDriverModule(const boot::KernelBootModuleV1& module,
        const wchar_t* baseName, const wchar_t* fullName) {
        if (!baseName || !fullName || m_handoff.pci.clock.time.oem.moduleCount >= m_handoff.pci.clock.time.oem.moduleCapacity ||
            m_handoff.pci.clock.time.oem.moduleCount >= boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES ||
            module.image.base == 0 || module.image.size == 0 || module.image.entryPoint == 0)
            return false;
        const auto sameBaseName = [](const char* left, const char* right) noexcept {
            if (!left || !right) return false;
            while (*left && *right && *left == *right) { ++left; ++right; }
            return *left == *right;
        };
        for (uint32_t i = 0; i < m_handoff.pci.clock.time.oem.moduleCount; ++i) {
            if (sameBaseName(m_handoff.pci.clock.time.oem.modules[i].baseName, module.baseName))
                return false;
        }
        m_handoff.pci.clock.time.oem.modules[m_handoff.pci.clock.time.oem.moduleCount++] = module;
        m_lpb.bootModules.push_back({
            .baseDllName = baseName,
            .fullDllName = fullName,
            .dllBase = static_cast<uintptr_t>(module.image.base),
            .sizeOfImage = static_cast<size_t>(module.image.size),
            .entryPoint = static_cast<uintptr_t>(module.image.entryPoint),
            .moduleFlags = module.flags
        });
        return true;
    }

    void setUserImageFile(boot::KernelBootFileV1 image) noexcept {
        m_handoff.pci.clock.time.oem.legacy.userImageFile = image;
    }

    /**
     * @brief Stage 5: Exit Boot Services
     */
    uefi::EfiStatus exitBootServices(uint64_t mapKey) {
        if (!m_systemTable || !m_systemTable->bootServices || !m_systemTable->bootServices->exitBootServices) {
            return uefi::EFI_INVALID_PARAMETER;
        }

        uefi::EfiStatus status = m_systemTable->bootServices->exitBootServices(m_imageHandle, mapKey);
        if (status == uefi::EFI_SUCCESS) {
            m_bootServicesExited = true;
        }
        return status;
    }


    int runBootloaderHandoverMenu() {
        if (!m_videoDriver.isInitialized() || !m_systemTable || !m_systemTable->bootServices) {
            return 1;
        }

        uint32_t width = m_videoDriver.getWidth();
        uint32_t height = m_videoDriver.getHeight();

        // 1. Initial screen clear & styling
        m_videoDriver.fillRectangle(0, 0, width, height, bootvid::Color{10, 14, 22});

        // Top Header
        m_videoDriver.fillRectangle(0, 0, width, 36, bootvid::Color{18, 24, 38});
        m_videoDriver.fillRectangle(0, 35, width, 1, bootvid::Color{38, 54, 82});
        m_videoDriver.drawString(20, 10, "Project MicaNT UEFI Bootloader v1.0", bootvid::Color{0, 229, 255}, bootvid::Color{18, 24, 38}, 1);
        m_videoDriver.drawString(320, 10, "| Dave Cutler DEC Mica Edition | Bare-Metal Handover", bootvid::Color{170, 190, 215}, bootvid::Color{18, 24, 38}, 1);

        // Hardware Card
        uint32_t cardX = (width > 800) ? 60 : 20;
        uint32_t cardY = 55;
        uint32_t cardW = width - 2 * cardX;
        uint32_t cardH = 290;
        m_videoDriver.fillRectangle(cardX, cardY, cardW, cardH, bootvid::Color{16, 22, 34});
        m_videoDriver.drawRectangle(cardX, cardY, cardW, cardH, bootvid::Color{36, 52, 78});

        m_videoDriver.drawString(cardX + 20, cardY + 16, "DISCOVERED REAL SILICON & ARCHITECTURAL TOPOLOGY", bootvid::Color{255, 215, 0}, bootvid::Color{16, 22, 34}, 1);
        m_videoDriver.fillRectangle(cardX + 20, cardY + 34, cardW - 40, 1, bootvid::Color{30, 42, 64});

        // Telemetry details
        char lineBuf[128];
        // CPU
        copyStr(lineBuf, "CPU Model:      ", sizeof(lineBuf));
        {
            size_t len = strlen(lineBuf);
            copyStr(lineBuf + len, m_cpuBrand[0] ? m_cpuBrand : "Unavailable", sizeof(lineBuf) - len);
        }
        m_videoDriver.drawString(cardX + 24, cardY + 46, lineBuf, bootvid::Color{220, 235, 255}, bootvid::Color{16, 22, 34}, 1);

        // Display firmware-probed values only; unknown values remain explicit.
        char number[24];
        const auto appendNumber = [&lineBuf, &number](uint64_t value) noexcept {
            formatUint(number, value);
            concatStr(lineBuf, number, sizeof(lineBuf));
        };
        copyStr(lineBuf, "Topology:       ", sizeof(lineBuf));
        appendNumber(m_hw.cpuPhysicalCores);
        concatStr(lineBuf, " cores / ", sizeof(lineBuf));
        appendNumber(m_hw.cpuLogicalThreads);
        concatStr(lineBuf, " logical CPUs (CPUID)", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 70, lineBuf, bootvid::Color{220, 235, 255}, bootvid::Color{16, 22, 34}, 1);

        // Physical RAM
        copyStr(lineBuf, "Physical RAM:   ", sizeof(lineBuf));
        appendNumber(m_hw.ramTotalMB);
        concatStr(lineBuf, " MB total / ", sizeof(lineBuf));
        appendNumber(m_hw.ramFreeMB);
        concatStr(lineBuf, " MB available (UEFI map)", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 94, lineBuf, bootvid::Color{220, 235, 255}, bootvid::Color{16, 22, 34}, 1);

        // ACPI
        copyStr(lineBuf, "ACPI:           ", sizeof(lineBuf));
        appendNumber(m_hw.acpiTableCount);
        concatStr(lineBuf, " tables discovered", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 118, lineBuf, bootvid::Color{220, 235, 255}, bootvid::Color{16, 22, 34}, 1);

        // GOP Framebuffer
        copyStr(lineBuf, "GOP display:    ", sizeof(lineBuf));
        appendNumber(m_videoDriver.getWidth());
        concatStr(lineBuf, "x", sizeof(lineBuf));
        appendNumber(m_videoDriver.getHeight());
        concatStr(lineBuf, " framebuffer", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 142, lineBuf, bootvid::Color{220, 235, 255}, bootvid::Color{16, 22, 34}, 1);

        // Disks
        copyStr(lineBuf, "Storage:        ", sizeof(lineBuf));
        appendNumber(m_hw.ahciDriveCount);
        concatStr(lineBuf, " AHCI devices / ", sizeof(lineBuf));
        appendNumber(m_hw.physicalDiskCount);
        concatStr(lineBuf, " UEFI disks", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 166, lineBuf, bootvid::Color{220, 235, 255}, bootvid::Color{16, 22, 34}, 1);

        uint32_t extPartitions{};
        for (size_t index = 0; index < m_hw.blockDeviceCount; ++index)
            if (m_hw.blockDevices[index].isExtFs) ++extPartitions;
        copyStr(lineBuf, "Ext partitions: ", sizeof(lineBuf));
        appendNumber(extPartitions);
        concatStr(lineBuf, " detected by UEFI probe", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 190, lineBuf, bootvid::Color{100, 240, 160}, bootvid::Color{16, 22, 34}, 1);

        copyStr(lineBuf, "Boot manager:   MicaNT UEFI loader", sizeof(lineBuf));
        m_videoDriver.drawString(cardX + 24, cardY + 214, lineBuf, bootvid::Color{0, 210, 255}, bootvid::Color{16, 22, 34}, 1);

        // Handover Box (Boot Manager)
        uint32_t handY = cardY + cardH + 20;
        uint32_t handH = 175;
        m_videoDriver.fillRectangle(cardX, handY, cardW, handH, bootvid::Color{14, 18, 28});
        m_videoDriver.drawRectangle(cardX, handY, cardW, handH, bootvid::Color{0, 180, 220});

        m_videoDriver.drawString(cardX + 20, handY + 14, "MicaNT CUTLER EDITION - BARE-METAL BOOT MANAGER", bootvid::Color{0, 229, 255}, bootvid::Color{14, 18, 28}, 1);

        m_videoDriver.drawString(cardX + 24, handY + 40, "[1] Start MicaNT (UEFI boot volume)                           [1/b/Enter]", bootvid::Color{240, 245, 255}, bootvid::Color{14, 18, 28}, 1);
        m_videoDriver.drawString(cardX + 24, handY + 62, "[2] Return to UEFI Firmware                                    [2/s]", bootvid::Color{180, 200, 230}, bootvid::Color{14, 18, 28}, 1);
        m_videoDriver.drawString(cardX + 24, handY + 84, "[3] Reboot machine (platform reset port 0xCF9)                   [3/r]", bootvid::Color{255, 140, 140}, bootvid::Color{14, 18, 28}, 1);
        m_videoDriver.fillRectangle(cardX + 24, handY + 106, cardW - 48, 22, bootvid::Color{14, 18, 28});

        // COM1 announcement
        UartSerial::print("\r\n=======================================================\r\n");
        UartSerial::print("  MicaNT Cutler Edition - Bare-Metal Boot Manager\r\n");
        UartSerial::print("=======================================================\r\n");
        UartSerial::print("  [1] Start MicaNT (UEFI boot volume)\r\n");
        UartSerial::print("  [2] Return to UEFI Firmware\r\n");
        UartSerial::print("  [3] Cold Reboot Server (Port 0xCF9)\r\n");
        UartSerial::print("  Press [1] or Enter for MicaNT, [2] for Setup, [3] for Reboot.\r\n");

        int defaultChoice = 1;
        int totalCountdown = 5;

        // Probe for \EFI\MicaNT\boot.ini on the active ESP volume
        char iniBuf[512]{};
        size_t iniSz = loadFileFromEfiSystemPartition(
            m_imageHandle,
            m_systemTable->bootServices,
            L"\\EFI\\MicaNT\\boot.ini",
            reinterpret_cast<uint8_t*>(iniBuf),
            sizeof(iniBuf) - 1
        );
        if (iniSz == 0) {
            iniSz = loadFileFromEfiSystemPartition(
                m_imageHandle,
                m_systemTable->bootServices,
                L"EFI\\MicaNT\\boot.ini",
                reinterpret_cast<uint8_t*>(iniBuf),
                sizeof(iniBuf) - 1
            );
        }
        // If not found on image handle, probe all mounted UEFI filesystems.
        if (iniSz == 0 && m_systemTable->bootServices && m_systemTable->bootServices->locateHandleBuffer) {
            auto* bs = m_systemTable->bootServices;
            uefi::EfiHandle* handles = nullptr;
            uint64_t numHandles = 0;
            if (bs->locateHandleBuffer(uefi::BY_PROTOCOL, &uefi::EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, nullptr, &numHandles, &handles) == uefi::EFI_SUCCESS && handles) {
                const wchar_t* probePaths[] = {
                    L"\\EFI\\MicaNT\\boot.ini",
                    L"\\boot.ini",
                    L"boot.ini",
                    L"EFI\\MicaNT\\boot.ini"
                };
                for (uint64_t h = 0; h < numHandles && iniSz == 0; ++h) {
                    uefi::EfiSimpleFileSystemProtocol* fs = nullptr;
                    if (bs->handleProtocol(handles[h], &uefi::EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, reinterpret_cast<void**>(&fs)) != uefi::EFI_SUCCESS || !fs || !fs->openVolume) continue;
                    uefi::EfiFileProtocol* root = nullptr;
                    if (fs->openVolume(fs, &root) != uefi::EFI_SUCCESS || !root || !root->open) continue;

                    for (const auto* pth : probePaths) {
                        uefi::EfiFileProtocol* f = nullptr;
                        if (root->open(root, &f, pth, uefi::EFI_FILE_MODE_READ, 0) == uefi::EFI_SUCCESS && f && f->read) {
                            uint64_t sz = sizeof(iniBuf) - 1;
                            if (f->read(f, &sz, reinterpret_cast<uint8_t*>(iniBuf)) == uefi::EFI_SUCCESS && sz > 0) {
                                iniSz = static_cast<size_t>(sz);
                                iniBuf[iniSz] = '\0';
                            }
                            if (f->close) f->close(f);
                            if (iniSz > 0) break;
                        }
                    }
                    if (root->close) root->close(root);
                }
            }
        }
        if (iniSz > 0) {
            iniBuf[iniSz] = '\0';
            if (strstr(iniBuf, "timeout=0")) {
                totalCountdown = 0;
            } else if (strstr(iniBuf, "timeout=1")) {
                totalCountdown = 1;
            } else if (strstr(iniBuf, "timeout=3")) {
                totalCountdown = 3;
            } else if (strstr(iniBuf, "timeout=10")) {
                totalCountdown = 10;
            }

        }

        if (totalCountdown == 0) {
            UartSerial::print("[MicaNT Boot] Immediate boot requested via boot.ini. Option: ");
            UartSerial::printDec(static_cast<uint32_t>(defaultChoice));
            UartSerial::print("\r\n");
            return defaultChoice;
        }

        // Countdown loop
        for (int sec = totalCountdown; sec > 0; --sec) {
            // Update countdown on screen
            char cdBuf[64];
            copyStr(cdBuf, "Booting MicaNT Kernel on Bare-Metal Silicon in ", sizeof(cdBuf));
            size_t cLen = strlen(cdBuf);
            cdBuf[cLen++] = '0' + (char)sec;
            cdBuf[cLen++] = ' ';
            cdBuf[cLen++] = 's';
            cdBuf[cLen++] = 'e';
            cdBuf[cLen++] = 'c';
            cdBuf[cLen++] = 'o';
            cdBuf[cLen++] = 'n';
            cdBuf[cLen++] = 'd';
            cdBuf[cLen++] = 's';
            cdBuf[cLen++] = '.';
            cdBuf[cLen++] = '.';
            cdBuf[cLen++] = '.';
            cdBuf[cLen] = '\0';
            m_videoDriver.fillRectangle(cardX + 20, handY + 138, cardW - 40, 26, bootvid::Color{14, 18, 28});
            m_videoDriver.drawString(cardX + 24, handY + 142, cdBuf, bootvid::Color{255, 230, 80}, bootvid::Color{14, 18, 28}, 1);

            UartSerial::print("[MicaNT Boot] Booting kernel in ");
            UartSerial::printDec(static_cast<uint32_t>(sec));
            UartSerial::print("s...\r\n");

            // Sub-second poll loop (100 x 10ms = 1 sec)
            for (int tick = 0; tick < 100; ++tick) {
                // Check COM1
                char c = 0;
                if (UartSerial::tryGetChar(c)) {
                    if (c == '1' || c == '\r' || c == '\n' || c == 'b' || c == 'B' || c == 'k' || c == 'K') {
                        UartSerial::print("[MicaNT Boot] Option 1 (MicaNT) selected via COM1.\r\n");
                        return 1;
                    } else if (c == '2' || c == 's' || c == 'S') {
                        UartSerial::print("[MicaNT Boot] Option 2 (UEFI Setup) selected via COM1.\r\n");
                        return 2;
                    } else if (c == '3' || c == 'r' || c == 'R') {
                        UartSerial::print("[MicaNT Boot] Option 3 (Reboot) selected via COM1.\r\n");
                        return 3;
                    }
                }

                // Check UEFI Console Keyboard
                if (m_systemTable->conIn) {
                    uefi::EfiInputKey key{};
                    if (m_systemTable->conIn->readKeyStroke(m_systemTable->conIn, &key) == uefi::EFI_SUCCESS) {
                        if (key.unicodeChar == L'1' || key.unicodeChar == L'\r' || key.unicodeChar == L'b' || key.unicodeChar == L'B') {
                            UartSerial::print("[MicaNT Boot] Option 1 (MicaNT) selected via keyboard.\r\n");
                            return 1;
                        } else if (key.unicodeChar == L'2' || key.unicodeChar == L's' || key.unicodeChar == L'S') {
                            UartSerial::print("[MicaNT Boot] Option 2 (UEFI Setup) selected via keyboard.\r\n");
                            return 2;
                        } else if (key.unicodeChar == L'3' || key.unicodeChar == L'r' || key.unicodeChar == L'R') {
                            UartSerial::print("[MicaNT Boot] Option 3 (Reboot) selected via keyboard.\r\n");
                            return 3;
                        }
                    }
                }

                if (m_systemTable->bootServices->stall) {
                    m_systemTable->bootServices->stall(10000); // 10ms
                }
            }
        }

        UartSerial::print("[MicaNT Boot] Countdown elapsed. Proceeding to KiSystemStartup bare-metal handover.\r\n");
        showBootProgress("Countdown complete. Preparing kernel handoff...");
        return 1;
    }

    void showBootProgress(const char* message) noexcept {
        if (m_videoDriver.isInitialized() && message) {
            const uint32_t width = m_videoDriver.getWidth();
            const uint32_t height = m_videoDriver.getHeight();
            if (width >= 80 && height >= 32) {
                constexpr bootvid::Color background{10, 14, 22};
                m_videoDriver.fillRectangle(0, height - 30, width, 30, background);
                m_videoDriver.drawString(16, height - 22, message,
                    bootvid::Color{255, 230, 120}, background, 1);
            }
        }
        UartSerial::print("[MicaNT Boot] Stage: ");
        UartSerial::print(message ? message : "unknown");
        UartSerial::print("\r\n");
    }

    void collectFirmwarePciResources() noexcept {
        auto& handoff = m_handoff.pci;
        handoff.pciBarCount = 0;
        handoff.pciInventoryComplete = 0;
        auto* bs = m_systemTable ? m_systemTable->bootServices : nullptr;
        if (!bs || !bs->locateHandleBuffer || !bs->handleProtocol || !bs->freePool) return;
        uint64_t count{};
        uefi::EfiHandle* handles{};
        if (bs->locateHandleBuffer(uefi::BY_PROTOCOL, &kPciIoGuid, nullptr, &count, &handles) != uefi::EFI_SUCCESS ||
            !handles || count > 65536) return;
        bool complete = true;
        const char* incompleteReason = nullptr;
        const auto markIncomplete = [&complete, &incompleteReason](const char* reason) noexcept {
            complete = false;
            if (!incompleteReason) incompleteReason = reason;
        };
        const auto read64 = [](const uint8_t* p) noexcept {
            uint64_t value{}; std::memcpy(&value, p, sizeof(value)); return value;
        };
        for (uint64_t i = 0; i < count; ++i) {
            FirmwarePciIo* pci{};
            if (bs->handleProtocol(handles[i], &kPciIoGuid, reinterpret_cast<void**>(&pci)) != uefi::EFI_SUCCESS ||
                !pci || !pci->getLocation || !pci->getBarAttributes) {
                markIncomplete("PCI inventory: invalid PCI I/O protocol\r\n");
                continue;
            }
            uint64_t segment{}, bus{}, device{}, function{};
            if (pci->getLocation(pci, &segment, &bus, &device, &function) != uefi::EFI_SUCCESS ||
                segment || bus > 255 || device >= 32 || function >= 8) {
                markIncomplete("PCI inventory: invalid PCI location\r\n");
                continue;
            }
            for (uint8_t index = 0; index < 6; ++index) {
                void* allocation{};
                const auto status = pci->getBarAttributes(pci, index, nullptr, &allocation);
                if (status != uefi::EFI_SUCCESS) { if (allocation) (void)bs->freePool(allocation); continue; }
                if (!allocation) {
                    markIncomplete("PCI inventory: missing BAR descriptor\r\n");
                    continue;
                }
                const auto* descriptor = static_cast<const uint8_t*>(allocation);
                if (descriptor[0] == 0x79) {
                    if (bs->freePool(allocation) != uefi::EFI_SUCCESS)
                        markIncomplete("PCI inventory: BAR descriptor release failed\r\n");
                    continue;
                }
                if (descriptor[0] != 0x8a || descriptor[1] != 0x2b || descriptor[2] != 0) {
                    markIncomplete("PCI inventory: unsupported BAR descriptor\r\n");
                    if (bs->freePool(allocation) != uefi::EFI_SUCCESS)
                        markIncomplete("PCI inventory: BAR descriptor release failed\r\n");
                    continue;
                }
                // The bounded importer accepts one QWORD descriptor followed
                // by an ACPI end tag. Multiple ranges and translated buses
                // remain untrusted and keep the inventory incomplete.
                bool valid = descriptor[3] <= 1 && descriptor[46] == 0x79;
                const uint64_t start = read64(descriptor + 14), end = read64(descriptor + 22);
                const uint64_t translation = read64(descriptor + 30), length = read64(descriptor + 38);
                valid = valid && !translation && start && length && !(length & (length - 1)) &&
                    !(start & (length - 1)) && start <= UINT64_MAX - (length - 1) &&
                    end == length - 1;
                if (valid) {
                    for (uint32_t n = 0; n < handoff.pciBarCount; ++n) {
                        const auto& prior = handoff.pciBars[n];
                        if (prior.segment == segment && prior.bus == bus && prior.device == device &&
                            prior.function == function && prior.index == index) { valid = false; break; }
                    }
                }
                if (!valid) {
                    UartSerial::print("PCI inventory: invalid BAR range at ");
                    UartSerial::printDec(static_cast<uint32_t>(bus));
                    UartSerial::print(":");
                    UartSerial::printDec(static_cast<uint32_t>(device));
                    UartSerial::print(".");
                    UartSerial::printDec(static_cast<uint32_t>(function));
                    UartSerial::print(" BAR ");
                    UartSerial::printDec(index);
                    UartSerial::print(" start=0x");
                    UartSerial::printHex64(start);
                    UartSerial::print(" length=0x");
                    UartSerial::printHex64(length);
                    UartSerial::print(" end=0x");
                    UartSerial::printHex64(end);
                    UartSerial::print(" translation=0x");
                    UartSerial::printHex64(translation);
                    UartSerial::print("\r\n");
                    markIncomplete("PCI inventory: invalid or excessive BAR range\r\n");
                } else if (handoff.pciBarCount == boot::UEFI_KERNEL_HANDOFF_MAX_PCI_BARS) {
                    markIncomplete("PCI inventory: invalid or excessive BAR range\r\n");
                } else {
                    auto& bar = handoff.pciBars[handoff.pciBarCount++];
                    bar = {static_cast<uint16_t>(segment), static_cast<uint16_t>(bus),
                           static_cast<uint8_t>(device), static_cast<uint8_t>(function), index,
                           descriptor[3], start, length};
                    if (bus == 0 && device == 2 && function == 0) {
                        UartSerial::print("PCI BAR snapshot index/start/length=");
                        UartSerial::printDec(index);
                        UartSerial::print("/0x");
                        UartSerial::printHex64(start);
                        UartSerial::print("/0x");
                        UartSerial::printHex64(length);
                        UartSerial::print("\r\n");
                    }
                }
                if (bs->freePool(allocation) != uefi::EFI_SUCCESS)
                    markIncomplete("PCI inventory: BAR descriptor release failed\r\n");
            }
        }
        if (bs->freePool(handles) != uefi::EFI_SUCCESS)
            markIncomplete("PCI inventory: handle buffer release failed\r\n");
        handoff.pciInventoryComplete = complete ? 1 : 0;
        if (!complete && incompleteReason) UartSerial::print(incompleteReason);
    }

    [[nodiscard]] const boot::LoaderParameterBlock& getLpb() const noexcept { return m_lpb; }
    [[nodiscard]] boot::LoaderParameterBlock& getLpb() noexcept { return m_lpb; }
    [[nodiscard]] const boot::UefiKernelHandoffV8& getHandoff() const noexcept {
        return m_handoff;
    }
    [[nodiscard]] bootvid::BootVideoDriver& getVideoDriver() noexcept { return m_videoDriver; }
    [[nodiscard]] bool hasExitedBootServices() const noexcept { return m_bootServicesExited; }
};

static UefiBootEngine* s_engineInstance = nullptr;

} // namespace micant::bootloader

// Freestanding runtime allocator for bare-metal UEFI execution
alignas(16) static uint8_t s_uefiHeap[4 * 1024 * 1024]; // 4MB heap
static size_t s_uefiHeapOffset = 0;

void* operator new(size_t size) {
    size = (size + 15) & ~static_cast<size_t>(15);
    if (s_uefiHeapOffset + size > sizeof(s_uefiHeap)) {
        while (true) {
#if defined(__x86_64__)
            __asm__ __volatile__("hlt");
#endif
        }
    }
    void* ptr = &s_uefiHeap[s_uefiHeapOffset];
    s_uefiHeapOffset += size;
    return ptr;
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, size_t) noexcept {}
void operator delete[](void*, size_t) noexcept {}

void* operator new(size_t size, std::align_val_t) { return operator new(size); }
void* operator new[](size_t size, std::align_val_t) { return operator new(size); }
void operator delete(void*, std::align_val_t) noexcept {}
void operator delete[](void*, std::align_val_t) noexcept {}
void operator delete(void*, size_t, std::align_val_t) noexcept {}
void operator delete[](void*, size_t, std::align_val_t) noexcept {}

extern "C" void* malloc(size_t size) { return operator new(size); }
extern "C" void free(void*) {}
extern "C" int atexit(void (*)(void)) { return 0; }

namespace std {
inline namespace __1 {
    void __libcpp_verbose_abort(char const*, ...) noexcept {
        while (true) {
#if defined(__x86_64__)
            __asm__ __volatile__("hlt");
#endif
        }
    }
}
}

extern "C" {
int abs(int x) {
    return x < 0 ? -x : x;
}

size_t wcslen(const wchar_t* s) {
    size_t len = 0;
    while (*s++) ++len;
    return len;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) ++len;
    return len;
}

void* memcpy(void* dest, const void* src, size_t n) {
    auto d = static_cast<uint8_t*>(dest);
    auto s = static_cast<const uint8_t*>(src);
    while (n--) *d++ = *s++;
    return dest;
}

void* memset(void* dest, int c, size_t n) {
    auto d = static_cast<uint8_t*>(dest);
    while (n--) *d++ = static_cast<uint8_t>(c);
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    auto d = static_cast<uint8_t*>(dest);
    auto s = static_cast<const uint8_t*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int __cxa_guard_acquire(uint64_t* guard) {
    return (*reinterpret_cast<const uint8_t*>(guard) == 0);
}

void __cxa_guard_release(uint64_t* guard) {
    *reinterpret_cast<uint8_t*>(guard) = 1;
}

void __cxa_guard_abort(uint64_t*) {}
}

using micant::bootloader::loadFileFromEfiSystemPartition;


/**
 * @brief Standard UEFI PE32+ Application Entry Point (bootx64.efi)
 */
namespace {
alignas(16) uint8_t s_initialMemoryMapBuffer[65536];
uint8_t* s_memoryMapBuffer = s_initialMemoryMapBuffer;
uint64_t s_memoryMapCapacity = sizeof(s_initialMemoryMapBuffer);
bool s_memoryMapBufferFromPool{};

micant::uefi::EfiStatus getCompleteMemoryMap(
    micant::uefi::EfiBootServices* services, uint64_t& mapSize, uint64_t& mapKey,
    uint64_t& descriptorSize, uint32_t& descriptorVersion) noexcept {
    if (!services || !services->getMemoryMap) return micant::uefi::EFI_INVALID_PARAMETER;
    for (;;) {
        mapSize = s_memoryMapCapacity;
        const auto status = services->getMemoryMap(&mapSize,
            reinterpret_cast<micant::uefi::EfiMemoryDescriptor*>(s_memoryMapBuffer),
            &mapKey, &descriptorSize, &descriptorVersion);
        if (status == micant::uefi::EFI_SUCCESS) return status;
        if (status != micant::uefi::EFI_BUFFER_TOO_SMALL || !services->allocatePool ||
            mapSize > UINT64_MAX - 16 * sizeof(micant::uefi::EfiMemoryDescriptor)) return status;
        const uint64_t capacity = mapSize + 16 * sizeof(micant::uefi::EfiMemoryDescriptor);
        using AllocatePool = micant::uefi::EfiStatus (*)(micant::uefi::EfiMemoryType, uint64_t, void**);
        const auto allocate = reinterpret_cast<AllocatePool>(services->allocatePool);
        void* expanded{};
        const auto allocationStatus = allocate(micant::uefi::EfiMemoryType::EfiLoaderData,
                                                capacity, &expanded);
        if (allocationStatus != micant::uefi::EFI_SUCCESS || !expanded) return allocationStatus;
        if (s_memoryMapBufferFromPool && services->freePool)
            (void)services->freePool(s_memoryMapBuffer);
        s_memoryMapBuffer = static_cast<uint8_t*>(expanded);
        s_memoryMapCapacity = capacity;
        s_memoryMapBufferFromPool = true;
    }
}
}

extern "C" micant::uefi::EfiStatus EfiMain(
    micant::uefi::EfiHandle imageHandle,
    micant::uefi::EfiSystemTable* systemTable
) {
    if (!systemTable) {
        return micant::uefi::EFI_INVALID_PARAMETER;
    }

    // 0. Disable UEFI firmware watchdog timer so long-running test harness never reboots
    if (systemTable->bootServices && systemTable->bootServices->setWatchdogTimer) {
        auto setWd = reinterpret_cast<micant::uefi::EfiStatus (*)(uint64_t, uint64_t, uint64_t, const wchar_t*)>(
            systemTable->bootServices->setWatchdogTimer
        );
        setWd(0, 0, 0, nullptr);
    }

    micant::bootloader::UartSerial::init();
    micant::bootloader::UartSerial::print("\r\n[MicaNT COM1] EfiMain reached on physical x86_64 silicon!\r\n");

    micant::bootloader::UefiBootEngine engine(imageHandle, systemTable);

    // 1. Initial firmware console output
    engine.initConsole();

    // 2. Discover Real Silicon & Hardware Telemetry (CPUID, MSR DTS, SMBIOS, ACPI, AHCI, Block I/O)
    engine.detectSiliconTelemetry();

    // 3. Locate GOP & initialize linear framebuffer
    bool hasGop = engine.initGopFramebuffer();
    if (hasGop) {
        if (systemTable->conOut) {
            systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Located UEFI GOP Linear Framebuffer successfully.\r\n");
        }
        engine.displaySplash("Initializing MicaNT Core Subsystems...", 0.25f);
    } else {
        if (systemTable->conOut) {
            systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Operating in serial/console mode (no GOP).\r\n");
        }
    }

    // 3. Query physical memory map
    uint64_t memoryMapSize = s_memoryMapCapacity;
    uint64_t mapKey = 0;
    uint64_t descriptorSize = sizeof(micant::uefi::EfiMemoryDescriptor);
    uint32_t descriptorVersion = 1;

    micant::uefi::EfiStatus mapStatus = getCompleteMemoryMap(systemTable->bootServices,
        memoryMapSize, mapKey, descriptorSize, descriptorVersion);

    if (mapStatus == micant::uefi::EFI_SUCCESS) {
        if (systemTable->conOut) {
            systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Ingested physical memory map from UEFI firmware.\r\n");
        }

        // 4. Ingest memory map and build LOADER_PARAMETER_BLOCK
        if (hasGop) {
            engine.displaySplash("Building Loader Parameter Block (LPB)...", 0.65f);
        }

        if (descriptorSize >= sizeof(micant::uefi::EfiMemoryDescriptor)) {
            const size_t descCount = static_cast<size_t>(memoryMapSize / descriptorSize);
            void* normalizedStorage{};
            using AllocatePool = micant::uefi::EfiStatus (*)(micant::uefi::EfiMemoryType, uint64_t, void**);
            const auto allocatePool = reinterpret_cast<AllocatePool>(systemTable->bootServices->allocatePool);
            const uint64_t normalizedBytes = static_cast<uint64_t>(descCount) *
                sizeof(micant::uefi::EfiMemoryDescriptor);
            if (descCount && (!allocatePool || normalizedBytes / sizeof(micant::uefi::EfiMemoryDescriptor) != descCount ||
                allocatePool(micant::uefi::EfiMemoryType::EfiLoaderData,
                    normalizedBytes, &normalizedStorage) != micant::uefi::EFI_SUCCESS || !normalizedStorage)) {
                micant::bootloader::UartSerial::print("[MicaNT Boot] FATAL: cannot normalize complete UEFI memory map.\r\n");
                return micant::uefi::EFI_OUT_OF_RESOURCES;
            }
            auto* normalizedMap = static_cast<micant::uefi::EfiMemoryDescriptor*>(normalizedStorage);
            const size_t validCount = descCount;
            uint64_t totalRamBytes = 0;
            uint64_t freeRamBytes = 0;
            for (size_t i = 0; i < validCount; ++i) {
                const auto* descPtr = reinterpret_cast<const micant::uefi::EfiMemoryDescriptor*>(
                    s_memoryMapBuffer + (i * descriptorSize)
                );
                normalizedMap[i] = *descPtr;
                uint64_t bytes = descPtr->numberOfPages * 4096;
                if (descPtr->type == 7) { // EfiConventionalMemory (Free RAM)
                    freeRamBytes += bytes;
                    totalRamBytes += bytes;
                } else if ((descPtr->type >= 1 && descPtr->type <= 6) || descPtr->type == 9 || descPtr->type == 10 || descPtr->type == 14) {
                    totalRamBytes += bytes;
                }
            }
            engine.setRealRamMetrics(totalRamBytes, freeRamBytes);
            engine.detectSiliconTelemetry();
            engine.buildLoaderParameterBlock(std::span{normalizedMap, validCount});
            if (normalizedStorage && systemTable->bootServices->freePool)
                (void)systemTable->bootServices->freePool(normalizedStorage);
        }

        if (systemTable->conOut) {
            systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] LPB Built OK. Launching Desktop...\r\n");
        }
    }

    // 4a. Pre-load standalone NT Kernel Executive (ntoskrnl.exe) from FAT32 EFI System Partition
    if (systemTable->bootServices) {
        if (systemTable->conOut) {
            systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Probing FAT32 volume for \\EFI\\MicaNT\\ntoskrnl.exe...\r\n");
        }
        micant::bootloader::UartSerial::print("[MicaNT Boot] Probing FAT32 volume for \\EFI\\MicaNT\\ntoskrnl.exe...\r\n");

        size_t krnlBytes = loadFileFromEfiSystemPartition(
            imageHandle,
            systemTable->bootServices,
            L"\\EFI\\MicaNT\\ntoskrnl.exe",
            micant::bootloader::s_ntoskrnlFileBuffer,
            sizeof(micant::bootloader::s_ntoskrnlFileBuffer)
        );

        if (krnlBytes == 0) {
            krnlBytes = loadFileFromEfiSystemPartition(
                imageHandle,
                systemTable->bootServices,
                L"EFI\\MicaNT\\ntoskrnl.exe",
                micant::bootloader::s_ntoskrnlFileBuffer,
                sizeof(micant::bootloader::s_ntoskrnlFileBuffer)
            );
        }

        if (krnlBytes == 0) {
            krnlBytes = loadFileFromEfiSystemPartition(
                imageHandle,
                systemTable->bootServices,
                L"\\ntoskrnl.exe",
                micant::bootloader::s_ntoskrnlFileBuffer,
                sizeof(micant::bootloader::s_ntoskrnlFileBuffer)
            );
        }

        if (krnlBytes == 0) {
            krnlBytes = loadFileFromEfiSystemPartition(
                imageHandle,
                systemTable->bootServices,
                L"ntoskrnl.exe",
                micant::bootloader::s_ntoskrnlFileBuffer,
                sizeof(micant::bootloader::s_ntoskrnlFileBuffer)
            );
        }

        if (krnlBytes == 0) {
            krnlBytes = loadFileFromEfiSystemPartition(
                imageHandle,
                systemTable->bootServices,
                L"\\MicaNT\\System32\\ntoskrnl.exe",
                micant::bootloader::s_ntoskrnlFileBuffer,
                sizeof(micant::bootloader::s_ntoskrnlFileBuffer)
            );
        }

        if (krnlBytes > 0) {
            micant::bootloader::s_ntoskrnlFileSize = krnlBytes;
            std::span<const uint8_t> rawKrnl(micant::bootloader::s_ntoskrnlFileBuffer, krnlBytes);
            uint64_t targetBase = reinterpret_cast<uint64_t>(micant::bootloader::s_ntoskrnlImageBuffer);

            micant::NtStatus st = micant::km::executive::NtoskrnlImageLoader64::loadAndMapKernelImage(
                rawKrnl,
                micant::bootloader::s_ntoskrnlImageBuffer,
                sizeof(micant::bootloader::s_ntoskrnlImageBuffer),
                targetBase,
                micant::bootloader::s_loadedNtoskrnlInfo
            );

            if (st == micant::NtStatus::Success && micant::bootloader::s_loadedNtoskrnlInfo.headersValid) {
                micant::bootloader::s_ntoskrnlEntryPoint = micant::bootloader::s_loadedNtoskrnlInfo.entryPointVa;
                micant::bootloader::s_ntoskrnlLoadedFromDisk = true;
                if (systemTable->conOut) {
                    systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Standalone ntoskrnl.exe loaded & mapped successfully!\r\n");
                }
                micant::bootloader::UartSerial::print("[MicaNT Boot] Preloaded \\EFI\\MicaNT\\ntoskrnl.exe from FAT32 ESP volume: ");
                micant::bootloader::UartSerial::printDec(static_cast<uint32_t>(krnlBytes));
                micant::bootloader::UartSerial::print(" bytes. EntryPoint: 0x");
                micant::bootloader::UartSerial::printHex64(micant::bootloader::s_ntoskrnlEntryPoint);
                micant::bootloader::UartSerial::print("\r\n");
            } else {
                micant::bootloader::UartSerial::print("[MicaNT Boot] ERROR: loadAndMapKernelImage failed with status 0x");
                micant::bootloader::UartSerial::printHex32(static_cast<uint32_t>(st));
                micant::bootloader::UartSerial::print("\r\n");
                if (systemTable->conOut) {
                    systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] ERROR: loadAndMapKernelImage failed!\r\n");
                }
            }
        }
    }

    // Stage bounded, separate OEM .sys images into independent mapped-image
    // slots. Missing/invalid optional drivers are rolled back and skipped;
    // nothing is linked into ntoskrnl_core_baremetal.
    if (systemTable->bootServices) {
        struct OemDriverCandidate {
            const wchar_t* name;
            const wchar_t* ntPath;
            const wchar_t* fullPath;
            const wchar_t* fallbackPath;
            const char* baseName;
            uint32_t flags;
        };
        constexpr OemDriverCandidate candidates[] = {
            {L"virtio_gpu.sys", L"\\SystemRoot\\System32\\drivers\\virtio_gpu.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\virtio_gpu.sys",
             L"\\EFI\\MicaNT\\virtio_gpu.sys", "virtio_gpu.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER |
                 micant::boot::BOOT_MODULE_FLAG_DISPLAY_DRIVER},
            {L"storahci.sys", L"\\SystemRoot\\System32\\drivers\\storahci.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\storahci.sys",
             L"\\EFI\\MicaNT\\storahci.sys", "storahci.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
            {L"hidusb.sys", L"\\SystemRoot\\System32\\drivers\\hidusb.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\hidusb.sys",
             L"\\EFI\\MicaNT\\hidusb.sys", "hidusb.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
            {L"usbstor.sys", L"\\SystemRoot\\System32\\drivers\\usbstor.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\usbstor.sys",
             L"\\EFI\\MicaNT\\usbstor.sys", "usbstor.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
            {L"usbxhci.sys", L"\\SystemRoot\\System32\\drivers\\usbxhci.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\usbxhci.sys",
             L"\\EFI\\MicaNT\\usbxhci.sys", "usbxhci.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
            {L"ehci.sys", L"\\SystemRoot\\System32\\drivers\\ehci.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\ehci.sys",
             L"\\EFI\\MicaNT\\ehci.sys", "ehci.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
            {L"i210.sys", L"\\SystemRoot\\System32\\drivers\\i210.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\i210.sys",
             L"\\EFI\\MicaNT\\i210.sys", "i210.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
            {L"virtio_net.sys", L"\\SystemRoot\\System32\\drivers\\virtio_net.sys",
             L"\\EFI\\MicaNT\\System32\\drivers\\virtio_net.sys",
             L"\\EFI\\MicaNT\\virtio_net.sys", "virtio_net.sys",
             micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER},
        };
        static_assert(std::size(candidates) <= micant::boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES);
        size_t slot = 0;
        for (const auto& candidate : candidates) {
            if (slot >= micant::bootloader::kMaxBootOemDriverImages) break;
            auto* mappedImage = micant::bootloader::s_bootOemDriverImageBuffers[slot];
            std::memset(micant::bootloader::s_bootOemDriverFileBuffer, 0,
                        sizeof(micant::bootloader::s_bootOemDriverFileBuffer));
            size_t driverBytes = loadFileFromEfiSystemPartition(
                imageHandle, systemTable->bootServices, candidate.fullPath,
                micant::bootloader::s_bootOemDriverFileBuffer,
                sizeof(micant::bootloader::s_bootOemDriverFileBuffer));
            if (!driverBytes) {
                driverBytes = loadFileFromEfiSystemPartition(
                    imageHandle, systemTable->bootServices, candidate.fallbackPath,
                    micant::bootloader::s_bootOemDriverFileBuffer,
                    sizeof(micant::bootloader::s_bootOemDriverFileBuffer));
            }
            if (!driverBytes) continue;

            micant::km::executive::NtoskrnlLoadedImageInfo imageInfo{};
            const std::span<const uint8_t> rawDriver(
                micant::bootloader::s_bootOemDriverFileBuffer, driverBytes);
            const uint64_t targetBase = reinterpret_cast<uint64_t>(mappedImage);
            const micant::NtStatus status =
                micant::km::executive::NtoskrnlImageLoader64::loadAndMapKernelImage(
                    rawDriver, mappedImage, micant::bootloader::kMaxBootOemDriverImageBytes,
                    targetBase, imageInfo);
            if (status != micant::NtStatus::Success || !imageInfo.headersValid ||
                imageInfo.sizeOfImage == 0 ||
                imageInfo.sizeOfImage > micant::bootloader::kMaxBootOemDriverImageBytes ||
                imageInfo.entryPointVa < targetBase ||
                imageInfo.entryPointVa - targetBase >= imageInfo.sizeOfImage) {
                std::memset(mappedImage, 0, micant::bootloader::kMaxBootOemDriverImageBytes);
                micant::bootloader::UartSerial::print("[MicaNT Boot] Rejected OEM driver image: ");
                micant::bootloader::UartSerial::print(candidate.baseName);
                micant::bootloader::UartSerial::print("\r\n");
                continue;
            }

            micant::boot::KernelBootModuleV1 module{};
            module.image = {targetBase, imageInfo.sizeOfImage, imageInfo.entryPointVa};
            module.flags = candidate.flags;
            size_t nameOffset = 0;
            while (candidate.baseName[nameOffset] && nameOffset + 1 < sizeof(module.baseName)) {
                module.baseName[nameOffset] = candidate.baseName[nameOffset];
                ++nameOffset;
            }
            module.baseName[nameOffset] = '\0';
            if (!engine.addBootDriverModule(module, candidate.name, candidate.ntPath)) {
                std::memset(mappedImage, 0, micant::bootloader::kMaxBootOemDriverImageBytes);
                micant::bootloader::UartSerial::print("[MicaNT Boot] OEM module handoff capacity/name collision: ");
                micant::bootloader::UartSerial::print(candidate.baseName);
                micant::bootloader::UartSerial::print("\r\n");
                continue;
            }
            micant::bootloader::UartSerial::print("[MicaNT Boot] Staged boot-start .sys: ");
            micant::bootloader::UartSerial::print(candidate.baseName);
            micant::bootloader::UartSerial::print("\r\n");
            ++slot;
        }
    }

    // 4b. Pre-load standalone PE32+ userland binary (app.exe) from FAT32 EFI System Partition
    // 4b. Pre-load standalone PE32+ userland binary (surshell_app.exe / app.exe) from FAT32 EFI System Partition
    if (systemTable->bootServices) {
        struct ProbeTarget {
            const wchar_t* path1;
            const wchar_t* path2;
            const wchar_t* name;
        };
        const ProbeTarget targets[] = {
            { L"\\EFI\\MicaNT\\explorer.exe",     L"EFI\\MicaNT\\explorer.exe",     L"explorer.exe" },
            { L"\\EFI\\MicaNT\\app.exe",          L"EFI\\MicaNT\\app.exe",          L"app.exe" },
            { L"\\EFI\\MicaNT\\surshell_app.exe", L"EFI\\MicaNT\\surshell_app.exe", L"surshell_app.exe" }
        };

        size_t bytesRead = 0;
        const ProbeTarget* matchedTarget = nullptr;

        for (const auto& tgt : targets) {
            if (systemTable->conOut) {
                systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Probing FAT32 volume for ");
                systemTable->conOut->outputString(systemTable->conOut, const_cast<wchar_t*>(tgt.path1));
                systemTable->conOut->outputString(systemTable->conOut, L"...\r\n");
            }
            micant::bootloader::UartSerial::print("[MicaNT Boot] Probing FAT32 volume for ");
            for (const wchar_t* p = tgt.name; *p; ++p) micant::bootloader::UartSerial::printChar(static_cast<char>(*p));
            micant::bootloader::UartSerial::print("...\r\n");

            bytesRead = loadFileFromEfiSystemPartition(
                imageHandle,
                systemTable->bootServices,
                tgt.path1,
                micant::bootloader::s_userPeFileBuffer,
                sizeof(micant::bootloader::s_userPeFileBuffer)
            );
            if (bytesRead == 0) {
                bytesRead = loadFileFromEfiSystemPartition(
                    imageHandle,
                    systemTable->bootServices,
                    tgt.path2,
                    micant::bootloader::s_userPeFileBuffer,
                    sizeof(micant::bootloader::s_userPeFileBuffer)
                );
            }
            if (bytesRead > 0) {
                matchedTarget = &tgt;
                break;
            }
        }

        if (bytesRead > 0 && matchedTarget) {
            micant::bootloader::s_userPeFileSize = bytesRead;
            micant::bootloader::s_userPeFileFromDisk = true;
            if (systemTable->conOut) {
                systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Preloaded ");
                systemTable->conOut->outputString(systemTable->conOut, const_cast<wchar_t*>(matchedTarget->name));
                systemTable->conOut->outputString(systemTable->conOut, L" from FAT32 ESP volume successfully.\r\n");
            }
            micant::bootloader::UartSerial::print("[MicaNT Boot] Preloaded ");
            for (const wchar_t* p = matchedTarget->name; *p; ++p) micant::bootloader::UartSerial::printChar(static_cast<char>(*p));
            micant::bootloader::UartSerial::print(" from FAT32 ESP volume: ");
            micant::bootloader::UartSerial::printDec(static_cast<uint32_t>(bytesRead));
            micant::bootloader::UartSerial::print(" bytes.\r\n");

            engine.setUserImageFile({
                reinterpret_cast<uint64_t>(micant::bootloader::s_userPeFileBuffer),
                static_cast<uint64_t>(bytesRead),
            });
            engine.getLpb().bootModules.push_back({
                .baseDllName = matchedTarget->name,
                .fullDllName = matchedTarget->path1,
                .dllBase = reinterpret_cast<uint64_t>(micant::bootloader::s_userPeFileBuffer),
                .sizeOfImage = static_cast<uint32_t>(bytesRead),
                .entryPoint = 0
            });
        } else {
            std::memcpy(micant::bootloader::s_userPeFileBuffer, kUnmodifiedSampleBinary, sizeof(kUnmodifiedSampleBinary));
            micant::bootloader::s_userPeFileSize = sizeof(kUnmodifiedSampleBinary);
            micant::bootloader::s_userPeFileFromDisk = false;
            if (systemTable->conOut) {
                systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Using embedded clean-room 64-bit PE binary fixture (app.exe fallback).\r\n");
            }
            micant::bootloader::UartSerial::print("[MicaNT Boot] \\EFI\\MicaNT\\app.exe not found on disk; armed embedded 64-bit PE binary fixture (");
            micant::bootloader::UartSerial::printDec(static_cast<uint32_t>(sizeof(kUnmodifiedSampleBinary)));
            micant::bootloader::UartSerial::print(" bytes).\r\n");
        }
    }

    micant::bootloader::s_engineInstance = &engine;

    // 5. Bootloader Handover Menu & Countdown (3 seconds)
    int bootDecision = 1; // 1 = Boot Kernel, 2 = UEFI Setup, 3 = Reboot
    if (hasGop) {
        bootDecision = engine.runBootloaderHandoverMenu();
    }

    if (bootDecision == 2) {
        micant::bootloader::UartSerial::print("[MicaNT Boot] Option 2 selected: Returning to UEFI firmware...\r\n");
        if (systemTable->conOut) {
            systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Returning to UEFI firmware...\r\n");
        }
        return micant::uefi::EFI_SUCCESS;
    } else if (bootDecision == 3) {
        micant::bootloader::UartSerial::print("[MicaNT Boot] Option 3 selected: Cold rebooting bare-metal server...\r\n");
        if (systemTable->runtimeServices && systemTable->runtimeServices->resetSystem) {
            systemTable->runtimeServices->resetSystem(0, micant::uefi::EFI_SUCCESS, 0, nullptr);
        }
        micant::bootloader::HalpColdResetServer();
    }

    engine.showBootProgress("Countdown finished. Validating kernel image...");

    // 6. Verify standalone ntoskrnl.exe is mapped BEFORE terminating Boot Services
    if (!micant::bootloader::s_ntoskrnlLoadedFromDisk || micant::bootloader::s_ntoskrnlEntryPoint == 0) {
        micant::bootloader::UartSerial::print("[MicaNT Boot] FATAL: \\EFI\\MicaNT\\ntoskrnl.exe was not loaded from disk!\r\n");
        if (systemTable->conOut) {
            systemTable->conOut->outputString(
                systemTable->conOut,
                L"[MicaNT Boot] FATAL: modular ntoskrnl.exe not loaded; refusing fallback.\r\n"
            );
        }
        return micant::uefi::EFI_NOT_FOUND;
    }

    // 7. Clean exit from Boot Services: enter 64-bit Long Mode on bare metal
    // Keep driver ownership in the POD handoff, not in the bootloader's STL LPB.
    if (systemTable->conOut) {
        systemTable->conOut->outputString(systemTable->conOut, L"[MicaNT Boot] Exiting UEFI Boot Services and entering Long Mode...\r\n");
    }
    micant::bootloader::UartSerial::print("\r\n[MicaNT Boot] Calling ExitBootServices to terminate UEFI firmware...\r\n");

    engine.showBootProgress("Collecting UEFI device resources...");
    engine.collectFirmwarePciResources();
    // Reserve PFN metadata and a range array from UEFI before taking the final
    // map. Their sizes follow the highest conventional PFN and the map-buffer
    // descriptor capacity; both allocations then appear as LoaderData.
    memoryMapSize = s_memoryMapCapacity;
    mapStatus = getCompleteMemoryMap(systemTable->bootServices,
        memoryMapSize, mapKey, descriptorSize, descriptorVersion);
    if (mapStatus != micant::uefi::EFI_SUCCESS ||
        descriptorSize < sizeof(micant::uefi::EfiMemoryDescriptor) ||
        memoryMapSize % descriptorSize) {
        micant::bootloader::UartSerial::print("[MicaNT Boot] FATAL: memory map unavailable for PFN sizing.\r\n");
        return mapStatus;
    }
    engine.showBootProgress("Sizing physical-page metadata from UEFI memory map...");
    uint64_t highestReclaimablePfn{};
    const size_t descriptorCount = static_cast<size_t>(memoryMapSize / descriptorSize);
    for (size_t index = 0; index < descriptorCount; ++index) {
        micant::uefi::EfiMemoryDescriptor descriptor{};
        std::memcpy(&descriptor, s_memoryMapBuffer + index * descriptorSize, sizeof(descriptor));
        if (!micant::uefi::isReclaimableAfterExitBootServices(
                static_cast<micant::uefi::EfiMemoryType>(descriptor.type)) ||
            !descriptor.numberOfPages || descriptor.numberOfPages >
                (UINT64_MAX - descriptor.physicalStart) / 4096) continue;
        const uint64_t end = descriptor.physicalStart + descriptor.numberOfPages * 4096;
        const uint64_t lastPfn = end / 4096;
        if (lastPfn > highestReclaimablePfn) highestReclaimablePfn = lastPfn;
    }
    if (!highestReclaimablePfn || highestReclaimablePfn >
            UINT64_MAX / sizeof(micant::mm::Mmpfn)) {
        micant::bootloader::UartSerial::print("[MicaNT Boot] FATAL: PFN metadata size overflow.\r\n");
        return micant::uefi::EFI_OUT_OF_RESOURCES;
    }
    const uint64_t metadataBytes = highestReclaimablePfn * sizeof(micant::mm::Mmpfn);
    micant::bootloader::UartSerial::print("[MicaNT Boot] Dynamic PFN entries/bytes=");
    micant::bootloader::UartSerial::printHex64(highestReclaimablePfn);
    micant::bootloader::UartSerial::print("/");
    micant::bootloader::UartSerial::printHex64(metadataBytes);
    micant::bootloader::UartSerial::print("\r\n");
    const uint64_t rangeCapacity = s_memoryMapCapacity /
        sizeof(micant::uefi::EfiMemoryDescriptor) + 1;
    const uint64_t rangeBytes = rangeCapacity * sizeof(micant::mm::PhysicalPageRange);
    engine.showBootProgress("Allocating kernel physical-page metadata...");
    using AllocatePool = micant::uefi::EfiStatus (*)(micant::uefi::EfiMemoryType, uint64_t, void**);
    const auto allocatePool = reinterpret_cast<AllocatePool>(systemTable->bootServices->allocatePool);
    void* pfnMetadata{};
    void* pfnRanges{};
    if (!allocatePool || allocatePool(micant::uefi::EfiMemoryType::EfiLoaderData,
            metadataBytes, &pfnMetadata) != micant::uefi::EFI_SUCCESS || !pfnMetadata ||
        allocatePool(micant::uefi::EfiMemoryType::EfiLoaderData,
            rangeBytes, &pfnRanges) != micant::uefi::EFI_SUCCESS || !pfnRanges) {
        micant::bootloader::UartSerial::print("[MicaNT Boot] FATAL: dynamic PFN bootstrap allocation failed.\r\n");
        return micant::uefi::EFI_OUT_OF_RESOURCES;
    }
    engine.setPfnBootstrapStorage(pfnMetadata, metadataBytes, pfnRanges, rangeCapacity);
    memoryMapSize = s_memoryMapCapacity;
    mapStatus = getCompleteMemoryMap(systemTable->bootServices,
        memoryMapSize, mapKey, descriptorSize, descriptorVersion);
    if (mapStatus != micant::uefi::EFI_SUCCESS ||
        descriptorSize < sizeof(micant::uefi::EfiMemoryDescriptor)) {
        micant::bootloader::UartSerial::print(
            "[MicaNT Boot] FATAL: final UEFI memory map could not be acquired.\r\n"
        );
        return mapStatus;
    }
    engine.updateMemoryMap(s_memoryMapBuffer, memoryMapSize, descriptorSize,
                           descriptorVersion);

    engine.refreshBootTimeSample();
    engine.showBootProgress("Exiting UEFI services and entering the kernel...");
    micant::uefi::EfiStatus exitStatus = engine.exitBootServices(mapKey);
    if (exitStatus != micant::uefi::EFI_SUCCESS) {
        memoryMapSize = s_memoryMapCapacity;
        mapStatus = getCompleteMemoryMap(systemTable->bootServices,
            memoryMapSize, mapKey, descriptorSize, descriptorVersion);
        if (mapStatus != micant::uefi::EFI_SUCCESS ||
            descriptorSize < sizeof(micant::uefi::EfiMemoryDescriptor)) {
            micant::bootloader::UartSerial::print(
                "[MicaNT Boot] FATAL: UEFI memory map refresh failed.\r\n"
            );
            return mapStatus;
        }
        engine.updateMemoryMap(s_memoryMapBuffer, memoryMapSize, descriptorSize,
                               descriptorVersion);
        engine.refreshBootTimeSample();
        exitStatus = engine.exitBootServices(mapKey);
    }
    if (exitStatus != micant::uefi::EFI_SUCCESS) {
        micant::bootloader::UartSerial::print(
            "[MicaNT Boot] FATAL: ExitBootServices failed; kernel was not entered.\r\n"
        );
        return exitStatus;
    }

    micant::bootloader::UartSerial::print("[MicaNT Boot] ExitBootServices SUCCESS! CPU now in pure 64-bit Long Mode.\r\n");
    engine.showBootProgress("ExitBootServices succeeded. Entering kernel entry point...");
    micant::bootloader::UartSerial::print("[MicaNT Boot] Handing silicon control to ntoskrnl.exe!KiSystemStartup at 0x");
    micant::bootloader::UartSerial::printHex64(micant::bootloader::s_ntoskrnlEntryPoint);
    micant::bootloader::UartSerial::print("...\r\n\r\n");

    using PfnKiSystemStartup = micant::NtStatus (*)(
        const micant::boot::UefiKernelHandoffV3* handoff
    );
    auto pfnKernel = reinterpret_cast<PfnKiSystemStartup>(micant::bootloader::s_ntoskrnlEntryPoint);
    pfnKernel(&engine.getHandoff().pci.clock.time.oem.legacy);

    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("hlt");
#endif
    }

    return micant::uefi::EFI_SUCCESS;
}
