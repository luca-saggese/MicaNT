#pragma once

// Small mode-neutral container layer for code shared with native Ring 3 DLLs.
// Native builds use bounded VM allocations through the NTDLL syscall boundary;
// hosted builds retain std::vector for isolated development tools.
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#if !defined(MICANT_NATIVE_USERMODE)
#include <string>
#include <vector>
#else
#include "micant/shared/native_nt_api.hpp"
#endif

namespace micant::shared {

inline void* boundedMemoryCopy(void* destination, const void* source, size_t count) noexcept {
    auto* out = static_cast<uint8_t*>(destination);
    const auto* in = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}
inline void* boundedMemorySet(void* destination, int value, size_t count) noexcept {
    auto* out = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(value);
    return destination;
}
inline int boundedMemoryCompare(const void* left, const void* right, size_t count) noexcept {
    const auto* a = static_cast<const uint8_t*>(left);
    const auto* b = static_cast<const uint8_t*>(right);
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return static_cast<int>(a[i]) - b[i];
    return 0;
}
inline char asciiLower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}
inline size_t boundedWideLength(const wchar_t* value, size_t maximum = 8192) noexcept {
    size_t length = 0;
    if (value) while (length <= maximum && value[length]) ++length;
    return length;
}
inline int boundedWideCompare(const wchar_t* left, const wchar_t* right) noexcept {
    while (*left && *left == *right) { ++left; ++right; }
    return static_cast<int>(*left) - static_cast<int>(*right);
}

#if !defined(MICANT_NATIVE_USERMODE)
template <class T>
class BoundedVector : public std::vector<T> {
    using Base = std::vector<T>;
public:
    using Base::Base;
    [[nodiscard]] bool good() const noexcept { return good_; }
    bool reserve(size_t count) noexcept {
        try { Base::reserve(count); return true; }
        catch (...) { good_ = false; return false; }
    }
    bool resize(size_t count) noexcept {
        try { Base::resize(count); return true; }
        catch (...) { good_ = false; return false; }
    }
    bool push_back(const T& value) { Base::push_back(value); return true; }
    bool push_back(T&& value) { Base::push_back(std::move(value)); return true; }
    friend bool operator==(const BoundedVector& left, const BoundedVector& right)
        requires requires(const T& a, const T& b) { a == b; } {
        if (left.size() != right.size()) return false;
        for (size_t i = 0; i < left.size(); ++i) if (!(left[i] == right[i])) return false;
        return true;
    }
    bool append(const T* values, size_t count) {
        if (!values && count) return false;
        if (!count) return true;
        try { Base::insert(Base::end(), values, values + count); return true; }
        catch (...) { good_ = false; return false; }
    }
private:
    bool good_{true};
};
#else
template <class T>
class BoundedVector {
public:
    using value_type = T;
    using size_type = size_t;
    using iterator = T*;
    using const_iterator = const T*;
    static constexpr size_t MaximumBytes = 16u * 1024u * 1024u;

    BoundedVector() noexcept = default;
    explicit BoundedVector(size_t count) noexcept { (void)resize(count); }
    BoundedVector(std::initializer_list<T> values) noexcept { assign(values.begin(), values.end()); }
    template <class It>
    BoundedVector(It first, It last) noexcept { assign(first, last); }
    BoundedVector(const BoundedVector& other) noexcept { assign(other.begin(), other.end()); }
    BoundedVector(BoundedVector&& other) noexcept { take(other); }
    ~BoundedVector() { reset(); }
    BoundedVector& operator=(const BoundedVector& other) noexcept {
        if (this != &other) { clear(); good_ = true; assign(other.begin(), other.end()); }
        return *this;
    }
    BoundedVector& operator=(BoundedVector&& other) noexcept {
        if (this != &other) { reset(); take(other); }
        return *this;
    }

    [[nodiscard]] bool good() const noexcept { return good_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] iterator begin() noexcept { return data_; }
    [[nodiscard]] const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data_; }
    [[nodiscard]] iterator end() noexcept { return data_ ? data_ + size_ : nullptr; }
    [[nodiscard]] const_iterator end() const noexcept { return data_ ? data_ + size_ : nullptr; }
    [[nodiscard]] const_iterator cend() const noexcept { return data_ ? data_ + size_ : nullptr; }
    T& operator[](size_t index) noexcept { return data_[index]; }
    const T& operator[](size_t index) const noexcept { return data_[index]; }
    T& front() noexcept { return data_[0]; }
    const T& front() const noexcept { return data_[0]; }
    T& back() noexcept { return data_[size_ - 1]; }
    const T& back() const noexcept { return data_[size_ - 1]; }
    friend bool operator==(const BoundedVector& left, const BoundedVector& right)
        requires requires(const T& a, const T& b) { a == b; } {
        if (left.size_ != right.size_) return false;
        for (size_t i = 0; i < left.size_; ++i) if (!(left.data_[i] == right.data_[i])) return false;
        return true;
    }

    bool reserve(size_t count) noexcept {
        if (!good_) return false;
        if (count > MaximumBytes / sizeof(T)) return fail();
        if (count <= capacity_) return true;
        static_assert(std::is_nothrow_move_constructible_v<T> ||
            std::is_nothrow_copy_constructible_v<T>,
            "freestanding bounded storage requires nonthrowing relocation");
        if constexpr (requires(const T& item) { item.good(); }) {
            for (size_t i = 0; i < size_; ++i) if (!data_[i].good()) return fail();
        }
        size_t next = capacity_ ? capacity_ : 8;
        while (next < count) {
            if (next > MaximumBytes / sizeof(T) / 2) { next = count; break; }
            next *= 2;
        }
        if (next > MaximumBytes / sizeof(T)) return fail();
        size_t bytes = next * sizeof(T);
        if (bytes > std::numeric_limits<size_t>::max() - 0xFFF) return fail();
        bytes = (bytes + 0xFFF) & ~size_t{0xFFF};
        void* base = nullptr;
        size_t region = bytes;
        constexpr uint32_t MEM_COMMIT_RESERVE = 0x3000;
        constexpr uint32_t PAGE_READWRITE = 0x04;
        if (native_nt::NtAllocateVirtualMemory(static_cast<Handle>(-1),
                &base, 0, &region, MEM_COMMIT_RESERVE, PAGE_READWRITE) != NtStatus::Success || !base)
            return fail();
        auto* replacement = static_cast<T*>(base);
        size_t constructed = 0;
        for (; constructed < size_; ++constructed) {
            ::new (static_cast<void*>(replacement + constructed)) T(std::move_if_noexcept(data_[constructed]));
            if constexpr (requires(const T& item) { item.good(); }) {
                if (!replacement[constructed].good()) {
                    destroyRange(replacement, constructed + 1);
                    void* failedBase = replacement;
                    size_t failedRegion = 0;
                    constexpr uint32_t MEM_RELEASE = 0x8000;
                    (void)native_nt::NtFreeVirtualMemory(static_cast<Handle>(-1),
                        &failedBase, &failedRegion, MEM_RELEASE);
                    return fail();
                }
            }
        }
        destroyRange(data_, size_);
        const bool oldReleased = release(data_);
        data_ = replacement;
        capacity_ = region / sizeof(T);
        return oldReleased ? true : fail();
    }

    bool resize(size_t count) noexcept {
        if (count < size_) { destroyRange(data_ + count, size_ - count); size_ = count; return true; }
        if (!reserve(count)) return false;
        size_t made = size_;
        for (; made < count; ++made) {
            if constexpr (std::is_nothrow_default_constructible_v<T>) ::new (data_ + made) T();
            else { good_ = false; break; }
        }
        size_ = made;
        return made == count;
    }
    void clear() noexcept { destroyRange(data_, size_); size_ = 0; }
    void pop_back() noexcept { if (size_) { data_[--size_].~T(); } }
    bool push_back(const T& value) noexcept {
        if constexpr (requires(const T& item) { item.good(); }) if (!value.good()) return fail();
        if (!reserve(size_ + 1)) return false;
        ::new (data_ + size_) T(value);
        if constexpr (requires(const T& item) { item.good(); }) {
            if (!data_[size_].good()) { data_[size_].~T(); return fail(); }
        }
        ++size_;
        return true;
    }
    bool push_back(T&& value) noexcept {
        if constexpr (requires(const T& item) { item.good(); }) if (!value.good()) return fail();
        if (!reserve(size_ + 1)) return false;
        ::new (data_ + size_) T(std::move(value));
        if constexpr (requires(const T& item) { item.good(); }) {
            if (!data_[size_].good()) { data_[size_].~T(); return fail(); }
        }
        ++size_;
        return true;
    }
    template <class... Args>
    T* emplace_back(Args&&... args) noexcept {
        if (!reserve(size_ + 1)) return nullptr;
        auto* place = ::new (data_ + size_) T(std::forward<Args>(args)...);
        if constexpr (requires(const T& item) { item.good(); })
            if (!place->good()) { place->~T(); good_ = false; return nullptr; }
        ++size_;
        return place;
    }
    template <class It>
    bool assign(It first, It last) noexcept {
        clear();
        size_t count = 0;
        for (auto it = first; it != last; ++it) {
            if (count == MaximumBytes / sizeof(T)) return fail();
            ++count;
        }
        if (!reserve(count)) return false;
        for (; first != last; ++first) {
            ::new (data_ + size_) T(*first);
            ++size_;
        }
        return true;
    }
    bool assign(const T* values, size_t count) noexcept {
        if (!values) { if (count) return fail(); clear(); return true; }
        return assign(values, values + count);
    }
    bool append(const T* values, size_t count) noexcept {
        if ((!values && count) || count > MaximumBytes / sizeof(T) - size_ || !reserve(size_ + count))
            return fail();
        for (size_t i = 0; i < count; ++i) ::new (data_ + size_++) T(values[i]);
        return true;
    }
    int compare(size_t position, size_t count, std::basic_string_view<T> other) const noexcept
        requires std::is_same_v<T, char> {
        if (position > size_) return 1;
        const auto left = std::basic_string_view<T>(data_ + position, (size_ - position < count) ? size_ - position : count);
        return left.compare(other);
    }
    template <class It>
    iterator insert(const_iterator position, It first, It last) noexcept {
        const size_t index = data_ ? static_cast<size_t>(position - data_) : 0;
        size_t count = 0;
        for (auto it = first; it != last; ++it) ++count;
        if (!count) return data_ ? data_ + index : nullptr;
        if (index > size_) { fail(); return end(); }
        if (count > MaximumBytes / sizeof(T) - size_ || !reserve(size_ + count)) return end();
        for (size_t i = size_; i > index; --i) {
            ::new (data_ + i + count - 1) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        size_ += count;
        for (size_t i = 0; i < count; ++i, ++first) ::new (data_ + index + i) T(*first);
        return data_ + index;
    }
    iterator insert(const_iterator position, const T& value) noexcept {
        const size_t index = data_ ? static_cast<size_t>(position - data_) : 0;
        if (index > size_) { fail(); return end(); }
        if constexpr (requires(const T& item) { item.good(); })
            if (!value.good()) { fail(); return end(); }
        if (!reserve(size_ + 1)) return end();
        for (size_t i = size_; i > index; --i) {
            ::new (data_ + i) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        ::new (data_ + index) T(value);
        ++size_;
        return data_ + index;
    }
    iterator erase(const_iterator first, const_iterator last) noexcept {
        const size_t from = static_cast<size_t>(first - data_);
        const size_t to = static_cast<size_t>(last - data_);
        if (from > to || to > size_) return end();
        const size_t removed = to - from;
        for (size_t i = from; i < to; ++i) data_[i].~T();
        for (size_t i = to; i < size_; ++i) {
            ::new (data_ + i - removed) T(std::move(data_[i]));
            data_[i].~T();
        }
        size_ -= removed;
        return data_ + from;
    }

private:
    T* data_{};
    size_t size_{};
    size_t capacity_{};
    bool good_{true};
    static void destroyRange(T* p, size_t count) noexcept {
        while (count) p[--count].~T();
    }
    static bool release(T* p) noexcept {
        if (!p) return true;
        size_t region = 0;
        void* base = p;
        constexpr uint32_t MEM_RELEASE = 0x8000;
        return native_nt::NtFreeVirtualMemory(static_cast<Handle>(-1),
            &base, &region, MEM_RELEASE) == NtStatus::Success;
    }
    bool fail() noexcept { good_ = false; return false; }
    void reset() noexcept {
        destroyRange(data_, size_);
        release(data_);
        data_ = nullptr; size_ = capacity_ = 0;
    }
    void take(BoundedVector& other) noexcept {
        data_ = other.data_; size_ = other.size_; capacity_ = other.capacity_; good_ = other.good_;
        other.data_ = nullptr; other.size_ = other.capacity_ = 0; other.good_ = true;
    }
};
#endif

template <class CharT>
class NativeBoundedString {
public:
    using value_type = CharT;
    using const_iterator = const CharT*;
    static constexpr size_t npos = static_cast<size_t>(-1);
    static constexpr size_t MaximumCharacters = sizeof(CharT) == 1
        ? 8u * 1024u * 1024u : 4u * 1024u * 1024u;

    NativeBoundedString() noexcept = default;
    NativeBoundedString(const CharT* text) noexcept { assign(text ? text : emptyText()); }
    NativeBoundedString(std::basic_string_view<CharT> text) noexcept { assign(text.begin(), text.end()); }
    NativeBoundedString(const CharT* text, size_t count) noexcept {
        if (text) assign(text, text + count); else clear();
    }
    template <class It>
    NativeBoundedString(It first, It last) noexcept { assign(first, last); }
    NativeBoundedString(const NativeBoundedString&) noexcept = default;
    NativeBoundedString(NativeBoundedString&&) noexcept = default;
    NativeBoundedString& operator=(const NativeBoundedString&) noexcept = default;
    NativeBoundedString& operator=(NativeBoundedString&&) noexcept = default;
    NativeBoundedString& operator=(const CharT* text) noexcept { assign(text ? text : emptyText()); return *this; }

    [[nodiscard]] bool good() const noexcept { return storage_.good(); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] size_t size() const noexcept { return storage_.empty() ? 0 : storage_.size() - 1; }
    [[nodiscard]] const CharT* data() const noexcept { return storage_.empty() ? emptyText() : storage_.data(); }
    [[nodiscard]] CharT* data() noexcept { return storage_.empty() ? emptyTextMutable() : storage_.data(); }
    [[nodiscard]] const CharT* c_str() const noexcept { return data(); }
    [[nodiscard]] CharT* begin() noexcept { return data(); }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] CharT* end() noexcept { return data() ? data() + size() : nullptr; }
    [[nodiscard]] const_iterator end() const noexcept { return data() ? data() + size() : nullptr; }
    CharT& operator[](size_t i) noexcept { return storage_[i]; }
    const CharT& operator[](size_t i) const noexcept { return storage_[i]; }
    CharT& front() noexcept { return storage_.front(); }
    const CharT& front() const noexcept { return storage_.front(); }
    [[nodiscard]] std::basic_string_view<CharT> view() const noexcept { return {data(), size()}; }
    operator std::basic_string_view<CharT>() const noexcept { return view(); }

    void clear() noexcept { storage_.clear(); }
    void reserve(size_t count) noexcept {
        if (count > MaximumCharacters) { markFailure(); return; }
        (void)storage_.reserve(count + 1);
    }
    void push_back(CharT ch) noexcept {
        if (size() == MaximumCharacters) { markFailure(); return; }
        if (!storage_.empty()) storage_.pop_back();
        (void)storage_.push_back(ch);
        (void)storage_.push_back(CharT{});
    }
    NativeBoundedString& append(std::basic_string_view<CharT> text) noexcept {
        if (text.size() > MaximumCharacters - size()) { markFailure(); return *this; }
        if (!storage_.empty()) storage_.pop_back();
        for (CharT ch : text) if (!storage_.push_back(ch)) break;
        (void)storage_.push_back(CharT{});
        return *this;
    }
    NativeBoundedString& append(const CharT* text, size_t count) noexcept {
        if (!text) return *this;
        return append(std::basic_string_view<CharT>(text, count));
    }
    NativeBoundedString& append(const CharT* text) noexcept {
        if (!text) return *this;
        size_t count{};
        while (count <= MaximumCharacters && text[count]) ++count;
        if (count > MaximumCharacters) { markFailure(); return *this; }
        return append(std::basic_string_view<CharT>(text, count));
    }
    NativeBoundedString& append(const NativeBoundedString& text) noexcept { return append(text.view()); }
    void assign(std::basic_string_view<CharT> text) noexcept {
        storage_.clear();
        if (text.empty()) return;
        if (text.size() > MaximumCharacters) { markFailure(); return; }
        (void)storage_.reserve(text.size() + 1);
        for (CharT ch : text) if (!storage_.push_back(ch)) break;
        (void)storage_.push_back(CharT{});
    }
    void assign(const CharT* text) noexcept {
        if (!text) { clear(); return; }
        size_t count = 0;
        while (count <= MaximumCharacters && text[count]) ++count;
        if (count > MaximumCharacters) { clear(); markFailure(); return; }
        assign(std::basic_string_view<CharT>(text, count));
    }
    void assign(const CharT* text, size_t count) noexcept {
        if (!text && count) { clear(); markFailure(); return; }
        assign(std::basic_string_view<CharT>(text ? text : emptyText(), count));
    }
    template <class It>
    void assign(It first, It last) noexcept {
        storage_.clear();
        size_t count = 0;
        for (auto it = first; it != last && count <= MaximumCharacters; ++it) ++count;
        if (count > MaximumCharacters) { markFailure(); return; }
        (void)storage_.reserve(count + 1);
        for (; first != last; ++first) if (!storage_.push_back(*first)) break;
        (void)storage_.push_back(CharT{});
    }
    [[nodiscard]] size_t find(std::basic_string_view<CharT> needle, size_t position = 0) const noexcept {
        if (position > size()) return npos;
        if (needle.empty()) return position;
        if (needle.size() > size() - position) return npos;
        for (size_t i = position; i <= size() - needle.size(); ++i) {
            size_t j = 0;
            while (j < needle.size() && data()[i + j] == needle[j]) ++j;
            if (j == needle.size()) return i;
        }
        return npos;
    }
    [[nodiscard]] size_t find(CharT ch, size_t position = 0) const noexcept {
        for (size_t i = position; i < size(); ++i) if (data()[i] == ch) return i;
        return npos;
    }
    [[nodiscard]] size_t find_first_not_of(std::basic_string_view<CharT> chars) const noexcept {
        for (size_t i = 0; i < size(); ++i) if (chars.find(data()[i]) == npos) return i;
        return npos;
    }
    [[nodiscard]] std::basic_string_view<CharT> substr(size_t pos, size_t count = npos) const noexcept {
        if (pos > size()) return {};
        const size_t available = size() - pos;
        return {data() + pos, count < available ? count : available};
    }
    [[nodiscard]] bool starts_with(std::basic_string_view<CharT> prefix) const noexcept { return view().starts_with(prefix); }
    [[nodiscard]] bool ends_with(std::basic_string_view<CharT> suffix) const noexcept { return view().ends_with(suffix); }
    friend bool operator==(const NativeBoundedString& a, std::basic_string_view<CharT> b) noexcept { return a.view() == b; }
    friend bool operator==(std::basic_string_view<CharT> a, const NativeBoundedString& b) noexcept { return a == b.view(); }
    friend bool operator==(const NativeBoundedString& a, const CharT* b) noexcept {
        if (!b) return a.empty();
        size_t count{};
        while (count <= MaximumCharacters && b[count]) ++count;
        return count <= MaximumCharacters && a.view() == std::basic_string_view<CharT>(b, count);
    }
    friend bool operator==(const CharT* a, const NativeBoundedString& b) noexcept { return b == a; }
    friend bool operator==(const NativeBoundedString& a, const NativeBoundedString& b) noexcept { return a.view() == b.view(); }

private:
    static CharT* emptyTextMutable() noexcept { static CharT text[1]{}; return text; }
    void markFailure() noexcept {
        (void)storage_.reserve(BoundedVector<CharT>::MaximumBytes / sizeof(CharT) + 1);
    }
    static const CharT* emptyText() noexcept { static constexpr CharT text[1]{}; return text; }
    BoundedVector<CharT> storage_;
};

#if defined(MICANT_NATIVE_USERMODE)
template <class CharT> using BoundedString = NativeBoundedString<CharT>;
#else
template <class CharT> using BoundedString = std::basic_string<CharT>;
#endif
using AnsiString = BoundedString<char>;
using UnicodeString = BoundedString<wchar_t>;

template <typename T>
[[nodiscard]] constexpr bool isContainerGood(const T& value) noexcept {
    if constexpr (requires { value.good(); }) return value.good();
    else return true;
}

} // namespace micant::shared
