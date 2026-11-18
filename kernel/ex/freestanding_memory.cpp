#include <cstddef>
#include <cstdint>

// These byte-wise implementations are shared by the freestanding Executive
// runtime and standalone native .sys images. Volatile accesses prevent the
// compiler from lowering the loops back into calls to these same functions.
extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<volatile uint8_t*>(destination);
    for (size_t index = 0; index < count; ++index)
        bytes[index] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<volatile uint8_t*>(destination);
    const auto* input = static_cast<const volatile uint8_t*>(source);
    for (size_t index = 0; index < count; ++index) output[index] = input[index];
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, size_t count) {
    auto* output = static_cast<volatile uint8_t*>(destination);
    const auto* input = static_cast<const volatile uint8_t*>(source);
    const auto to = reinterpret_cast<uintptr_t>(destination);
    const auto from = reinterpret_cast<uintptr_t>(source);
    if (to < from || to - from >= count) {
        for (size_t index = 0; index < count; ++index) output[index] = input[index];
    } else {
        for (size_t index = count; index; --index) output[index - 1] = input[index - 1];
    }
    return destination;
}

extern "C" int memcmp(const void* first, const void* second, size_t count) {
    const auto* left = static_cast<const volatile uint8_t*>(first);
    const auto* right = static_cast<const volatile uint8_t*>(second);
    for (size_t index = 0; index < count; ++index) {
        if (left[index] != right[index])
            return static_cast<int>(left[index]) - right[index];
    }
    return 0;
}

