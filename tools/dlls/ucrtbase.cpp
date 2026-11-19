/**
 * @file ucrtbase.cpp
 * Luce OS Universal C Runtime DLL (ucrtbase.dll) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Real memory allocator, string primitives, and process termination.
 */

#include <cstdint>
#include <cstddef>

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept {
    return true;
}

// UCRT's heap occupies the lower half of the executive's 128 MB process heap.
// The generic HeapAlloc/malloc trampolines use the upper half, so both paths
// share the process PEB contract without overlapping allocations.
// The process loader reserves the first 64 KB for the PEB, TEB TLS vector,
// and the static TLS blocks shared by loaded DLLs.
static size_t s_ucrtOffset = 0x10000;

static uint8_t* processHeapBase() noexcept {
    uint64_t pebVa = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(pebVa));
#endif
    if (!pebVa) return nullptr;
    // Peb64::processHeap is at offset 0x28; 0x30 is FastPebLock.
    auto* processHeap = reinterpret_cast<uint64_t*>(pebVa + 0x28);
    return reinterpret_cast<uint8_t*>(*processHeap);
}

__declspec(dllexport) void* malloc(size_t size) noexcept {
    if (size == 0) size = 1;
    if (size > static_cast<size_t>(-1) - 31) return nullptr;
    const size_t aligned = (size + 15) & ~static_cast<size_t>(15);
    const size_t allocationSize = aligned + 16;
    size_t offset = allocationSize;
#if defined(__x86_64__)
    // These DLLs enter directly through DllMain, so CRT global constructors do
    // not run. The allocator state is constant-initialized in the PE image,
    // but seed it lazily as well in case the loader supplied a zeroed .data.
    uint64_t expected = 0;
    const uint64_t initialOffset = 0x10000;
    __asm__ __volatile__("lock cmpxchgq %2, %1"
                         : "+a"(expected), "+m"(s_ucrtOffset)
                         : "r"(initialOffset)
                         : "memory");
    __asm__ __volatile__("lock xaddq %0, %1" : "+r"(offset), "+m"(s_ucrtOffset) : : "memory");
#else
    offset = s_ucrtOffset;
    s_ucrtOffset += aligned;
#endif
    if (offset > 64 * 1024 * 1024 ||
        allocationSize > 64 * 1024 * 1024 - offset) {
        return nullptr;
    }
    uint8_t* base = processHeapBase();
    if (!base) return nullptr;
    uint8_t* allocation = base + offset;
    *reinterpret_cast<size_t*>(allocation) = size;
    *reinterpret_cast<size_t*>(allocation + sizeof(size_t)) = 0x4D4943414E54414CULL;
    return allocation + 16;
}

__declspec(dllexport) void free(void* /*ptr*/) noexcept {
    // Lookaside bump-pointer pool; no-op free
}

__declspec(dllexport) void* calloc(size_t num, size_t size) noexcept {
    if (size != 0 && num > static_cast<size_t>(-1) / size) return nullptr;
    size_t total = num * size;
    void* p = malloc(total);
    if (p) {
        auto* b = static_cast<char*>(p);
        for (size_t i = 0; i < total; ++i) b[i] = 0;
    }
    return p;
}

__declspec(dllexport) void* realloc(void* ptr, size_t size) noexcept {
    if (!ptr) return malloc(size);
    if (size == 0) return nullptr;
    void* newPtr = malloc(size);
    if (newPtr && ptr) {
        auto* d = static_cast<char*>(newPtr);
        auto* s = static_cast<const char*>(ptr);
        const auto* oldAllocation = reinterpret_cast<const uint8_t*>(ptr) - 16;
        const size_t oldSize = *reinterpret_cast<const size_t*>(oldAllocation);
        if (*reinterpret_cast<const size_t*>(oldAllocation + sizeof(size_t)) != 0x4D4943414E54414CULL)
            return nullptr;
        const size_t copyBytes = oldSize < size ? oldSize : size;
        for (size_t i = 0; i < copyBytes; ++i) d[i] = s[i];
    }
    return newPtr;
}

__declspec(dllexport) void* memcpy(void* dest, const void* src, size_t n) noexcept {
    auto d = static_cast<char*>(dest);
    auto s = static_cast<const char*>(src);
    while (n--) *d++ = *s++;
    return dest;
}

__declspec(dllexport) void* memset(void* dest, int c, size_t n) noexcept {
    auto d = static_cast<char*>(dest);
    while (n--) *d++ = static_cast<char>(c);
    return dest;
}

__declspec(dllexport) int memcmp(const void* s1, const void* s2, size_t n) noexcept {
    auto p1 = static_cast<const unsigned char*>(s1);
    auto p2 = static_cast<const unsigned char*>(s2);
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

__declspec(dllexport) size_t strlen(const char* s) noexcept {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

__declspec(dllexport) int strcmp(const char* s1, const char* s2) noexcept {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

__declspec(dllexport) void abort() noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__("movq $3, %%r10; movq $0x01, %%rax; syscall"
                         ::: "rax", "r10", "rcx", "r11");
#endif
    while (true) {}
}

__declspec(dllexport) void exit(int code) noexcept {
#if defined(__x86_64__)
    uint64_t c = static_cast<uint64_t>(code);
    __asm__ __volatile__("movq %0, %%r10; movq $0x01, %%rax; syscall"
                         :: "r"(c) : "rax", "r10", "rcx", "r11");
#endif
    while (true) {}
}

} // extern "C"
