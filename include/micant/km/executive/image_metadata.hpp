#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace micant::ps::detail {

// The public x64 winternl.h layouts, with virtual addresses represented as
// integers so the buffers can be constructed in the owning address space.
struct UserUnicodeString {
    uint16_t length{};
    uint16_t maximumLength{};
    uint32_t padding{};
    uint64_t buffer{};
};

struct UserProcessParameters {
    uint8_t reserved1[16]{};
    uint64_t reserved2[10]{};
    UserUnicodeString imagePathName{};
    UserUnicodeString commandLine{};
};

static_assert(sizeof(UserUnicodeString) == 16);
static_assert(offsetof(UserProcessParameters, imagePathName) == 0x60);
static_assert(offsetof(UserProcessParameters, commandLine) == 0x70);

class UserMetadataWriter {
public:
    UserMetadataWriter(std::span<uint8_t> storage, uintptr_t userBase, size_t offset) noexcept
        : storage_(storage), userBase_(userBase), offset_(offset) {}

    [[nodiscard]] bool append(std::u16string_view text, UserUnicodeString& result) noexcept {
        if (text.empty()) {
            result = {};
            return true;
        }
        constexpr size_t maxCharacters = (std::numeric_limits<uint16_t>::max() - sizeof(char16_t)) / sizeof(char16_t);
        if (text.size() > maxCharacters || offset_ > storage_.size()) return false;
        const size_t bytes = text.size() * sizeof(char16_t);
        const size_t capacity = bytes + sizeof(char16_t);
        if (capacity > storage_.size() - offset_ || offset_ > UINTPTR_MAX - userBase_ ||
            capacity > UINTPTR_MAX - (userBase_ + offset_)) return false;

        std::memcpy(storage_.data() + offset_, text.data(), bytes);
        const char16_t terminator = 0;
        std::memcpy(storage_.data() + offset_ + bytes, &terminator, sizeof(terminator));
        result = {static_cast<uint16_t>(bytes), static_cast<uint16_t>(capacity), 0, userBase_ + offset_};
        offset_ += capacity;
        return true;
    }

    [[nodiscard]] size_t offset() const noexcept { return offset_; }

private:
    std::span<uint8_t> storage_;
    uintptr_t userBase_;
    size_t offset_;
};

} // namespace micant::ps::detail
