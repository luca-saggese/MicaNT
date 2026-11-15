#pragma once

// Process-local HICON object storage. Handles are opaque generation-tagged
// values; pixel storage remains immutable after publication and stays alive
// while a draw/copy lease references it.
#include "micant/um/subsystems/imaging/image_decoder.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <span>

namespace micant::um::winuser {

using HICON = void*;

struct IconBitmapView {
    uint32_t width{};
    uint32_t height{};
    uint32_t stride{};
    std::span<const uint8_t> pixels{};
    uint16_t hotX{};
    uint16_t hotY{};
    bool cursor{};
};

class IconHandleTable {
public:
    static constexpr size_t Capacity = 255;

    class Lease {
    public:
        Lease() noexcept = default;
        ~Lease() { reset(); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept { take(other); }
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) { reset(); take(other); }
            return *this;
        }
        [[nodiscard]] explicit operator bool() const noexcept { return owner_ != nullptr; }
        [[nodiscard]] const IconBitmapView& bitmap() const noexcept { return view_; }
        [[nodiscard]] bool shared() const noexcept { return shared_; }
    private:
        friend class IconHandleTable;
        Lease(IconHandleTable* owner, uint16_t slot, uint64_t generation,
              IconBitmapView view, bool shared) noexcept
            : owner_(owner), slot_(slot), generation_(generation), view_(view), shared_(shared) {}
        void reset() noexcept {
            if (owner_) owner_->release(slot_, generation_);
            owner_ = nullptr; view_ = {}; shared_ = false;
        }
        void take(Lease& other) noexcept {
            owner_ = std::exchange(other.owner_, nullptr);
            slot_ = other.slot_; generation_ = other.generation_;
            view_ = other.view_; shared_ = other.shared_;
            other.view_ = {}; other.shared_ = false;
        }
        IconHandleTable* owner_{};
        uint16_t slot_{};
        uint64_t generation_{};
        IconBitmapView view_{};
        bool shared_{};
    };

    [[nodiscard]] HICON create(imaging::ImageFrame&& frame,
                               const imaging::ImageAllocator& allocator,
                               bool shared = false) noexcept {
        return createObject(std::move(frame), allocator, shared, false, 0, 0);
    }

    [[nodiscard]] HICON createCursor(imaging::ImageFrame&& frame,
                                     const imaging::ImageAllocator& allocator,
                                     uint16_t hotX, uint16_t hotY,
                                     bool shared = false) noexcept {
        return createObject(std::move(frame), allocator, shared, true, hotX, hotY);
    }

private:
    [[nodiscard]] HICON createObject(imaging::ImageFrame&& frame,
                                     const imaging::ImageAllocator& allocator,
                                     bool shared, bool cursor,
                                     uint16_t hotX, uint16_t hotY) noexcept {
        if (!validFrame(frame) || !allocator.deallocate) return nullptr;
        if (cursor && (hotX >= frame.width || hotY >= frame.height)) return nullptr;
        size_t bytes{};
        imaging::ImageAllocator owner{};
        uint8_t* pixels = frame.pixels.releaseOwnership(bytes, owner);
        if (!pixels || !bytes || owner.deallocate != allocator.deallocate ||
            owner.context != allocator.context) {
            if (pixels && owner.deallocate) owner.deallocate(owner.context, pixels, bytes);
            return nullptr;
        }
        lock();
        for (uint16_t i = 0; i < Capacity; ++i) {
            Record& record = records_[i];
            if (record.active || record.references || record.pixels) continue;
            uint64_t generation = record.generation + 1;
            if (!generation || generation > GenerationMask) generation = 1;
            record.generation = generation;
            record.width = frame.width; record.height = frame.height;
            record.stride = frame.stride; record.pixelBytes = bytes;
            record.hotX = hotX; record.hotY = hotY; record.cursor = cursor;
            record.pixels = pixels; record.allocator = owner;
            record.active = true; record.shared = shared; record.retired = false;
            unlock();
            return encode(i, generation);
        }
        unlock();
        owner.deallocate(owner.context, pixels, bytes);
        return nullptr;
    }

public:

    [[nodiscard]] bool acquire(HICON handle, Lease& result) noexcept {
        return acquireKind(handle, result, false);
    }

    [[nodiscard]] bool acquireCursor(HICON handle, Lease& result) noexcept {
        return acquireKind(handle, result, true);
    }

private:
    [[nodiscard]] bool acquireKind(HICON handle, Lease& result, bool cursor) noexcept {
        result = {};
        uint16_t slot{}; uint64_t generation{};
        if (!decode(handle, slot, generation)) return false;
        lock();
        Record& record = records_[slot];
        if (!record.active || record.generation != generation || !record.pixels ||
            record.cursor != cursor ||
            record.references == UINT32_MAX) {
            unlock(); return false;
        }
        ++record.references;
        result = Lease(this, slot, generation,
            {record.width, record.height, record.stride,
             std::span<const uint8_t>(record.pixels, record.pixelBytes),
             record.hotX, record.hotY, record.cursor}, record.shared);
        unlock();
        return true;
    }

public:

    [[nodiscard]] bool cloneLease(const Lease& source, Lease& result) noexcept {
        result = {};
        if (source.owner_ != this || source.slot_ >= Capacity) return false;
        lock();
        Record& record = records_[source.slot_];
        if (record.generation != source.generation_ || !record.pixels ||
            !record.references || record.references == UINT32_MAX) {
            unlock(); return false;
        }
        ++record.references;
        result = Lease(this, source.slot_, source.generation_,
            {record.width, record.height, record.stride,
             std::span<const uint8_t>(record.pixels, record.pixelBytes),
             record.hotX, record.hotY, record.cursor}, record.shared);
        unlock();
        return true;
    }

    [[nodiscard]] bool destroy(HICON handle) noexcept {
        return destroyKind(handle, false);
    }

    [[nodiscard]] bool destroyCursor(HICON handle) noexcept {
        return destroyKind(handle, true);
    }

private:
    [[nodiscard]] bool destroyKind(HICON handle, bool cursor) noexcept {
        uint16_t slot{}; uint64_t generation{};
        if (!decode(handle, slot, generation)) return false;
        Allocation released{};
        lock();
        Record& record = records_[slot];
        if (!record.active || record.generation != generation || record.shared ||
            record.cursor != cursor) {
            unlock(); return false;
        }
        record.active = false; record.retired = true;
        if (!record.references) released = detach(record);
        unlock();
        free(released);
        return true;
    }

public:

    // Called by the owning module/cache when a shared icon source is retired.
    [[nodiscard]] bool releaseSharedAsset(HICON handle) noexcept {
        uint16_t slot{}; uint64_t generation{};
        if (!decode(handle, slot, generation)) return false;
        Allocation released{};
        lock();
        Record& record = records_[slot];
        if (!record.active || record.generation != generation || !record.shared) {
            unlock(); return false;
        }
        record.active = false; record.retired = true;
        if (!record.references) released = detach(record);
        unlock();
        free(released);
        return true;
    }

    [[nodiscard]] HICON copy(HICON handle, const imaging::ImageAllocator& allocator) noexcept {
        Lease source{};
        if (!acquire(handle, source)) return nullptr;
        return copyScaled(source, source.bitmap().width, source.bitmap().height, allocator);
    }

    [[nodiscard]] HICON copyCursor(HICON handle,
                                   const imaging::ImageAllocator& allocator) noexcept {
        Lease source{};
        if (!acquireCursor(handle, source)) return nullptr;
        const auto& view = source.bitmap();
        imaging::ImageFrame frame{};
        frame.width = view.width; frame.height = view.height; frame.stride = view.stride;
        frame.format = imaging::PixelFormat::Bgra8Premultiplied;
        if (!frame.pixels.allocate(view.pixels.size(), allocator)) return nullptr;
        std::memcpy(frame.pixels.data(), view.pixels.data(), view.pixels.size());
        return createCursor(std::move(frame), allocator, view.hotX, view.hotY, false);
    }

    [[nodiscard]] HICON copyScaled(const Lease& source, uint32_t width, uint32_t height,
                                   const imaging::ImageAllocator& allocator) noexcept {
        if (!source || !width || !height || width > 4096 || height > 4096 ||
            !allocator.allocate || !allocator.deallocate) return nullptr;
        const auto& view = source.bitmap();
        imaging::ImageFrame frame{};
        frame.width = view.width; frame.height = view.height; frame.stride = view.stride;
        frame.format = imaging::PixelFormat::Bgra8Premultiplied;
        if (!frame.pixels.allocate(view.pixels.size(), allocator)) return nullptr;
        std::memcpy(frame.pixels.data(), view.pixels.data(), view.pixels.size());
        if (width != view.width || height != view.height) {
            imaging::ImageFrame scaled{};
            if (imaging::ScaleIconFrame(frame, width, height, scaled, allocator) !=
                imaging::ImageStatus::Success) return nullptr;
            frame = std::move(scaled);
        }
        return create(std::move(frame), allocator, false);
    }

    [[nodiscard]] size_t ownedCount() noexcept {
        lock(); size_t count{};
        for (const auto& record : records_) if (record.active && !record.shared) ++count;
        unlock(); return count;
    }

private:
    static constexpr uint64_t HandleTag = 0x00007a0000000000ull;
    static constexpr uint64_t HandleTagMask = 0x00007f0000000000ull;
    static constexpr uint64_t GenerationMask = 0x00000000ffffffffull;
    struct Allocation { uint8_t* pixels{}; size_t bytes{}; imaging::ImageAllocator allocator{}; };
    struct Record {
        uint8_t* pixels{};
        size_t pixelBytes{};
        imaging::ImageAllocator allocator{};
        uint32_t width{}, height{}, stride{}, references{};
        uint16_t hotX{}, hotY{};
        uint64_t generation{};
        bool active{}, shared{}, retired{}, cursor{};
    };

    static bool validFrame(const imaging::ImageFrame& frame) noexcept {
        if (!frame.width || !frame.height || frame.width > 4096 || frame.height > 4096 ||
            frame.stride != static_cast<uint64_t>(frame.width) * 4) return false;
        const uint64_t bytes = static_cast<uint64_t>(frame.stride) * frame.height;
        return bytes <= 64ull * 1024 * 1024 && frame.pixels.size() == bytes &&
               frame.format == imaging::PixelFormat::Bgra8Premultiplied;
    }
    static HICON encode(uint16_t slot, uint64_t generation) noexcept {
        const uint64_t value = HandleTag | ((generation & GenerationMask) << 8) | (slot + 1);
        return reinterpret_cast<HICON>(static_cast<uintptr_t>(value));
    }
    static bool decode(HICON handle, uint16_t& slot, uint64_t& generation) noexcept {
        const uint64_t value = reinterpret_cast<uintptr_t>(handle);
        const uint16_t encodedSlot = static_cast<uint16_t>(value & 0xff);
        if ((value & HandleTagMask) != HandleTag || !encodedSlot || encodedSlot > Capacity)
            return false;
        slot = encodedSlot - 1;
        generation = (value >> 8) & GenerationMask;
        return generation != 0;
    }
    void lock() noexcept { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() noexcept { lock_.clear(std::memory_order_release); }
    static Allocation detach(Record& record) noexcept {
        Allocation result{record.pixels, record.pixelBytes, record.allocator};
        record.pixels = nullptr; record.pixelBytes = 0; record.allocator = {};
        record.width = record.height = record.stride = 0;
        record.shared = record.retired = false;
        return result;
    }
    static void free(Allocation allocation) noexcept {
        if (allocation.pixels && allocation.allocator.deallocate)
            allocation.allocator.deallocate(allocation.allocator.context,
                                            allocation.pixels, allocation.bytes);
    }
    void release(uint16_t slot, uint64_t generation) noexcept {
        Allocation released{};
        lock();
        Record& record = records_[slot];
        if (record.generation == generation && record.references) {
            --record.references;
            if (!record.active && record.retired && !record.references) released = detach(record);
        }
        unlock();
        free(released);
    }

    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<Record, Capacity> records_{};
};

inline IconHandleTable g_processIconHandleTable{};
inline IconHandleTable& ProcessIconHandles() noexcept { return g_processIconHandleTable; }

class WindowIconBindings {
public:
    [[nodiscard]] HICON set(void* window, uint32_t type, HICON icon) noexcept {
        if (!window || type > 2) return nullptr;
        IconHandleTable::Lease incoming{};
        if (icon && !ProcessIconHandles().acquire(icon, incoming)) return nullptr;
        IconHandleTable::Lease previousLease{};
        HICON previous{};
        lock();
        Entry* entry = find(window);
        if (!entry && icon) entry = allocate(window);
        if (!entry) { unlock(); return nullptr; }
        previous = entry->handles[type];
        previousLease = std::move(entry->leases[type]);
        entry->handles[type] = icon;
        entry->leases[type] = std::move(incoming);
        if (!entry->handles[0] && !entry->handles[1] && !entry->handles[2]) entry->window = nullptr;
        unlock();
        return previous;
    }

    [[nodiscard]] HICON get(void* window, uint32_t type) noexcept {
        if (!window || type > 2) return nullptr;
        lock(); Entry* entry = find(window);
        HICON icon = entry ? entry->handles[type] : nullptr;
        unlock(); return icon;
    }

    [[nodiscard]] bool acquire(void* icon, IconHandleTable::Lease& lease) noexcept {
        if (!icon) return false;
        lease = {};
        lock();
        for (auto& entry : entries_) {
            if (!entry.window) continue;
            for (size_t i = 0; i < entry.handles.size(); ++i) {
                if (entry.handles[i] == icon && entry.leases[i] &&
                    ProcessIconHandles().cloneLease(entry.leases[i], lease)) {
                    unlock(); return true;
                }
            }
        }
        unlock(); return false;
    }

    void remove(void* window) noexcept {
        if (!window) return;
        std::array<IconHandleTable::Lease, 3> retired{};
        lock(); Entry* entry = find(window);
        if (entry) {
            for (size_t i = 0; i < retired.size(); ++i) {
                retired[i] = std::move(entry->leases[i]);
                entry->handles[i] = nullptr;
            }
            entry->window = nullptr;
        }
        unlock();
    }

private:
    struct Entry {
        void* window{};
        std::array<HICON, 3> handles{};
        std::array<IconHandleTable::Lease, 3> leases{};
    };
    void lock() noexcept { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() noexcept { lock_.clear(std::memory_order_release); }
    Entry* find(void* window) noexcept {
        for (auto& entry : entries_) if (entry.window == window) return &entry;
        return nullptr;
    }
    Entry* allocate(void* window) noexcept {
        for (auto& entry : entries_) {
            if (!entry.window) { entry.window = window; return &entry; }
        }
        return nullptr;
    }
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<Entry, 128> entries_{};
};

inline WindowIconBindings g_processWindowIcons{};
inline WindowIconBindings& ProcessWindowIcons() noexcept { return g_processWindowIcons; }

} // namespace micant::um::winuser
