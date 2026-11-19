#include <cstddef>
#include <cstdint>

// Small user-mode string primitives used by std::string_view/char_traits in
// freestanding DLLs. Volatile reads prevent compiler loop-to-libcall folding.
extern "C" size_t strlen(const char* value) noexcept {
    const auto* p = reinterpret_cast<const volatile unsigned char*>(value);
    size_t count = 0;
    while (p[count]) ++count;
    return count;
}

extern "C" size_t wcslen(const wchar_t* value) noexcept {
    const volatile wchar_t* p = value;
    size_t count = 0;
    while (p[count]) ++count;
    return count;
}

extern "C" int wmemcmp(const wchar_t* left, const wchar_t* right, size_t count) noexcept {
    const volatile wchar_t* a = left;
    const volatile wchar_t* b = right;
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

extern "C" wchar_t* wmemchr(const wchar_t* memory, wchar_t value, size_t count) noexcept {
    const volatile wchar_t* p = memory;
    for (size_t i = 0; i < count; ++i)
        if (p[i] == value) return const_cast<wchar_t*>(memory + i);
    return nullptr;
}

extern "C" void* memchr(const void* memory, int character, size_t count) noexcept {
    const auto* p = static_cast<const volatile uint8_t*>(memory);
    const uint8_t wanted = static_cast<uint8_t>(character);
    for (size_t i = 0; i < count; ++i)
        if (p[i] == wanted) return const_cast<uint8_t*>(static_cast<const uint8_t*>(memory) + i);
    return nullptr;
}

// LLVM-MinGW's large-frame probe for freestanding user-mode DLLs.  It
// touches every intervening page so the kernel's moving stack guard can
// commit the frame.  RAX retains the requested size, as required by the ABI.
extern "C" __attribute__((naked)) void ___chkstk_ms() noexcept {
    __asm__ __volatile__(
        "leaq 8(%rsp), %r11\n"
        "movq %rax, %r10\n"
        "1:\n"
        "cmpq $4096, %r10\n"
        "jb 2f\n"
        "subq $4096, %r11\n"
        "testb $0, (%r11)\n"
        "subq $4096, %r10\n"
        "jmp 1b\n"
        "2:\n"
        "subq %r10, %r11\n"
        "testb $0, (%r11)\n"
        "retq\n");
}

#if defined(MICANT_EXPORT_KERNEL32_CHKSTK)
// Existing AMD64 PE applications may import the MSVC spelling from
// Kernel32. Reuse the same page-touch routine and publish the required
// external symbol only in the Kernel32 image.
extern "C" __declspec(dllexport) void Kernel32StackProbe()
    asm("__chkstk") __attribute__((alias("___chkstk_ms")));
#endif
