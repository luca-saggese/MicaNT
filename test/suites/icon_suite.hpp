#pragma once

#include "micant/um/subsystems/imaging/ico_decoder.hpp"
#include "micant/um/native/pe.hpp"
#include "micant/um/subsystems/winuser/icon_handle_table.hpp"
#include "micant/um/subsystems/winuser/cursor_image.hpp"
#include "micant/um/subsystems/winuser/ani_image.hpp"
#include "micant/km/win32k/freestanding.hpp"
#include "micant/shared/pixel_composite.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

inline void Test_IcoDecoder_PngDibAndMalformedInputs() {
    using namespace micant::um::imaging;
    const ImageAllocator allocator{
        nullptr,
        [](void*, size_t bytes) noexcept -> void* { return std::malloc(bytes); },
        [](void*, void* address, size_t) noexcept { std::free(address); },
    };
    const std::vector<uint8_t> pngIcon{
        0,0,1,0,1,0, 1,1,0,0,1,0,32,0, 70,0,0,0,22,0,0,0,
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,13,73,68,65,84,120,156,99,96,96,96,96,0,0,0,5,0,1,165,246,69,64,
        0,0,0,0,73,69,78,68,174,66,96,130
    };
    IconDirectory directory{};
    TEST_ASSERT(ParseIconDirectory(pngIcon, directory) == ImageStatus::Success,
                 "valid PNG ICO directory is accepted");
    TEST_ASSERT(directory.count == 1, "ICO directory count is preserved");
    IconDirectoryEntry entry{};
    TEST_ASSERT(directory.entry(0, entry) && entry.width == 1 && entry.height == 1,
                 "ICO dimensions are decoded explicitly");
    ImageFrame pngFrame{};
    TEST_ASSERT(DecodeIcoImage(directory.image(0), entry, pngFrame, allocator) == ImageStatus::Success,
                 "embedded PNG icon decodes");
    TEST_ASSERT(pngFrame.width == 1 && pngFrame.height == 1 && pngFrame.sourceHadAlpha,
                 "PNG icon dimensions and alpha are retained");
    ImageFrame scaledPng{};
    TEST_ASSERT(ScaleIconFrame(pngFrame, 2, 2, scaledPng, allocator) == ImageStatus::Success &&
                scaledPng.width == 2 && scaledPng.height == 2 && scaledPng.pixels.size() == 16,
                "icon scaling publishes a bounded premultiplied frame");

    std::array<uint8_t, 70> dibIcon{};
    dibIcon[2] = 1; dibIcon[4] = 1;
    dibIcon[6] = 1; dibIcon[7] = 1; dibIcon[10] = 1;
    dibIcon[12] = 32; dibIcon[14] = 48; dibIcon[18] = 22;
    dibIcon[22] = 40; dibIcon[26] = 1; dibIcon[30] = 2; dibIcon[34] = 1;
    dibIcon[36] = 32;
    dibIcon[62] = 0x20; dibIcon[63] = 0x40; dibIcon[64] = 0x60; dibIcon[65] = 0x80;
    IconDirectory dibDirectory{};
    TEST_ASSERT(ParseIconDirectory(dibIcon, dibDirectory) == ImageStatus::Success,
                 "valid DIB ICO directory is accepted");
    IconDirectoryEntry dibEntry{};
    TEST_ASSERT(dibDirectory.entry(0, dibEntry), "DIB ICO entry is readable");
    ImageFrame dibFrame{};
    TEST_ASSERT(DecodeIcoImage(dibDirectory.image(0), dibEntry, dibFrame, allocator) == ImageStatus::Success,
                 "classic 32-bit DIB icon decodes");
    TEST_ASSERT(dibFrame.pixels[0] == 16 && dibFrame.pixels[1] == 32 &&
                 dibFrame.pixels[2] == 48 && dibFrame.pixels[3] == 128,
                 "DIB channels are premultiplied into BGRA");

    auto malformed = pngIcon;
    malformed[18] = 0xff;
    TEST_ASSERT(ParseIconDirectory(malformed, directory) == ImageStatus::MalformedData,
                 "ICO payload escaping the input is rejected");
}

inline void Test_MiniMouseCursorAssets();

inline void Test_CurDecoder_PngHotspotAndMalformedInput() {
    using namespace micant::um::imaging;
    using namespace micant::um::winuser;
    const ImageAllocator allocator{
        nullptr,
        [](void*, size_t bytes) noexcept -> void* { return std::malloc(bytes); },
        [](void*, void* address, size_t) noexcept { std::free(address); },
    };
    std::vector<uint8_t> cur{
        0,0,2,0,1,0, 1,1,0,0,0,0,0,0,70,0,0,0,22,0,0,0,
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,13,73,68,65,84,120,156,99,96,96,96,96,0,0,0,5,0,1,165,246,69,64,
        0,0,0,0,73,69,78,68,174,66,96,130
    };
    CursorImage image{};
    TEST_ASSERT(DecodeCursorImage(cur, 32, 32, image, allocator) == ImageStatus::Success,
                "CUR directory decodes embedded PNG payload");
    TEST_ASSERT(image.image.width == 1 && image.image.height == 1 &&
                image.hotX == 0 && image.hotY == 0,
                "CUR preserves its hotspot and decoded dimensions");
    std::vector<uint8_t> dibCur(70, 0);
    dibCur[2] = 2; dibCur[4] = 1;
    dibCur[6] = 1; dibCur[7] = 1; dibCur[14] = 48; dibCur[18] = 22;
    dibCur[22] = 40; dibCur[26] = 1; dibCur[30] = 2; dibCur[34] = 1;
    dibCur[36] = 32;
    dibCur[62] = 0x20; dibCur[63] = 0x40; dibCur[64] = 0x60; dibCur[65] = 0x80;
    TEST_ASSERT(DecodeCursorImage(dibCur, 1, 1, image, allocator) == ImageStatus::Success &&
                image.image.pixels[0] == 16 && image.image.pixels[1] == 32 &&
                image.image.pixels[2] == 48 && image.image.pixels[3] == 128,
                "CUR DIB decoding reuses the premultiplied icon image decoder");
    auto badType = cur; badType[2] = 1;
    TEST_ASSERT(DecodeCursorImage(badType, 32, 32, image, allocator) == ImageStatus::MalformedData,
                "ICO input is not accepted as a cursor");
    auto badHotspot = cur; badHotspot[10] = 2;
    TEST_ASSERT(DecodeCursorImage(badHotspot, 32, 32, image, allocator) == ImageStatus::MalformedData,
                "hotspot outside the selected image is rejected");
    auto truncated = cur; truncated.pop_back();
    TEST_ASSERT(DecodeCursorImage(truncated, 32, 32, image, allocator) != ImageStatus::Success,
                "truncated CUR payload fails without publishing an image");
    Test_MiniMouseCursorAssets();
}

inline void Test_MiniMouseCursorAssets() {
    using namespace micant::um::imaging;
    using namespace micant::um::winuser;
    const ImageAllocator allocator{
        nullptr,
        [](void*, size_t bytes) noexcept -> void* { return std::malloc(bytes); },
        [](void*, void* address, size_t) noexcept { std::free(address); },
    };
    constexpr const char* curNames[]{
        "arrow", "beam", "cross", "ew", "helpsel", "link", "move",
        "nesw", "ns", "nwse", "pen", "unavail", "up",
    };
    for (const char* name : curNames) {
        std::ifstream file(std::string("System/Cursors/MiniMouse/mini_mouse_") + name + ".cur",
                           std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        CursorImage image{};
        TEST_ASSERT(file.good() || file.eof(), "MiniMouse CUR fixture opens from system assets");
        TEST_ASSERT(DecodeCursorImage(bytes, 0, 0, image, allocator) == ImageStatus::Success,
                    "MiniMouse CUR fixture decodes through production cursor decoder");
        TEST_ASSERT(image.hotX < image.image.width && image.hotY < image.image.height,
                    "MiniMouse CUR hotspot remains inside its decoded frame");
    }
    for (const char* name : {"busy", "working"}) {
        std::ifstream file(std::string("System/Cursors/MiniMouse/mini_mouse_") + name + ".ani",
                           std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        AniFileView animation{};
        TEST_ASSERT(file.good() || file.eof(), "MiniMouse ANI fixture opens from system assets");
        TEST_ASSERT(ParseAniFile(bytes, animation) == AniStatus::Success && animation.stepCount,
                    "MiniMouse ANI fixture has a valid frame sequence and timing");
    }
}

inline void Test_AniParser_SequenceRatesAndUnknownChunks() {
    using namespace micant::um::winuser;
    std::vector<uint8_t> ani(12, 0);
    auto tag = [&](const char* value) { for (size_t i = 0; i < 4; ++i) ani.push_back(value[i]); };
    auto u32 = [&](uint32_t value) { for (unsigned i = 0; i < 4; ++i) ani.push_back(uint8_t(value >> (i * 8))); };
    auto chunk = [&](const char* name, const std::vector<uint8_t>& payload) {
        tag(name); u32(static_cast<uint32_t>(payload.size()));
        ani.insert(ani.end(), payload.begin(), payload.end());
        if (payload.size() & 1) ani.push_back(0);
    };
    std::vector<uint8_t> header(36, 0);
    auto set32 = [&](std::vector<uint8_t>& data, size_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) data[at+i] = uint8_t(value >> (i*8));
    };
    set32(header,0,36); set32(header,4,2); set32(header,8,3);
    set32(header,28,6); set32(header,32,3);
    chunk("anih", header);
    chunk("JUNK", {1,2,3});
    chunk("rate", {4,0,0,0,5,0,0,0,6,0,0,0});
    chunk("seq ", {1,0,0,0,0,0,0,0,1,0,0,0});
    std::vector<uint8_t> frames{'f','r','a','m'};
    const std::vector<uint8_t> cur{0,0,2,0,1,0,1,1,0,0,0,0,0,0,1,0,0,0,22,0,0,0,0x89,0x50,0};
    auto frameChunk = [&](std::vector<uint8_t>& target) {
        target.insert(target.end(), {'i','c','o','n'});
        const uint32_t n = static_cast<uint32_t>(cur.size());
        for (unsigned i = 0; i < 4; ++i) target.push_back(uint8_t(n >> (i*8)));
        target.insert(target.end(), cur.begin(), cur.end());
        if (cur.size() & 1) target.push_back(0);
    };
    frameChunk(frames); frameChunk(frames);
    chunk("LIST", frames);
    ani[0]='R'; ani[1]='I'; ani[2]='F'; ani[3]='F';
    const uint32_t riffSize = static_cast<uint32_t>(ani.size()-8);
    for (unsigned i=0;i<4;++i) ani[4+i]=uint8_t(riffSize>>(i*8));
    ani[8]='A'; ani[9]='C'; ani[10]='O'; ani[11]='N';
    AniFileView parsed{};
    TEST_ASSERT(ParseAniFile(ani, parsed) == AniStatus::Success,
                "ANI accepts bounded frames, sequence, rates, and unknown chunks");
    TEST_ASSERT(parsed.frameCount == 2 && parsed.stepCount == 3 &&
                parsed.steps[0].frameIndex == 1 && parsed.steps[0].durationJiffies == 4 &&
                parsed.steps[1].frameIndex == 0 && parsed.steps[2].durationJiffies == 6,
                "ANI sequence indices and per-step rates are preserved");
    auto invalid = ani;
    // First sequence index is at the start of the seq payload.
    size_t seqOffset = 0;
    for (size_t i = 0; i + 8 < invalid.size(); ++i)
        if (invalid[i]=='s' && invalid[i+1]=='e' && invalid[i+2]=='q' && invalid[i+3]==' ') {
            seqOffset = i + 8; break;
        }
    if (seqOffset) invalid[seqOffset] = 9;
    TEST_ASSERT(ParseAniFile(invalid, parsed) != AniStatus::Success,
                "ANI rejects a sequence reference beyond available frames");
}

inline void Test_PeResources_BoundedTypeNameLanguageLookup() {
    using namespace micant;
    const um::imaging::ImageAllocator allocator{
        nullptr,
        [](void*, size_t bytes) noexcept -> void* { return std::malloc(bytes); },
        [](void*, void* address, size_t) noexcept { std::free(address); },
    };
    std::vector<uint8_t> pe(0x400, 0);
    const auto put16 = [&](size_t offset, uint16_t value) {
        pe[offset] = static_cast<uint8_t>(value);
        pe[offset + 1] = static_cast<uint8_t>(value >> 8);
    };
    const auto put32 = [&](size_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) pe[offset + i] = static_cast<uint8_t>(value >> (i * 8));
    };
    const auto put64 = [&](size_t offset, uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) pe[offset + i] = static_cast<uint8_t>(value >> (i * 8));
    };
    put16(0, 0x5a4d); put32(0x3c, 0x80); put32(0x80, 0x4550);
    put16(0x84, 0x8664); put16(0x86, 1); put16(0x94, 0xf0);
    const size_t optional = 0x98;
    put16(optional, 0x20b); put64(optional + 24, 0x140000000ull);
    put32(optional + 32, 0x1000); put32(optional + 36, 0x200);
    put32(optional + 56, 0x2000); put32(optional + 60, 0x200);
    put32(optional + 108, 16);
    put32(optional + 112 + pe::IMAGE_DIRECTORY_ENTRY_RESOURCE * 8, 0x1000);
    put32(optional + 116 + pe::IMAGE_DIRECTORY_ENTRY_RESOURCE * 8, 0x100);
    const size_t section = 0x188;
    put32(section + 8, 0x200); put32(section + 12, 0x1000);
    put32(section + 16, 0x200); put32(section + 20, 0x200);
    put32(section + 36, pe::IMAGE_SCN_MEM_READ);

    // root: RT_GROUP_ICON (14) -> group name (101) -> language 1033 -> data
    const size_t root = 0x200;
    put16(root + 14, 2);
    put32(root + 16, 3); put32(root + 20, 0x80000080u);
    put32(root + 24, 14); put32(root + 28, 0x80000020u);
    put16(root + 0x20 + 14, 2); put32(root + 0x20 + 16, 101); put32(root + 0x20 + 20, 0x80000040u);
    put32(root + 0x20 + 24, 202); put32(root + 0x20 + 28, 0x80000040u);
    put16(root + 0x40 + 14, 1); put32(root + 0x40 + 16, 1033); put32(root + 0x40 + 20, 0x60);
    put32(root + 0x60, 0x10c0); put32(root + 0x64, 4); put32(root + 0x68, 1200);
    pe[0x2c0] = 0xde; pe[0x2c1] = 0xad; pe[0x2c2] = 0xbe; pe[0x2c3] = 0xef;
    put16(root + 0x80 + 14, 1); put32(root + 0x80 + 16, 1); put32(root + 0x80 + 20, 0x800000a0u);
    put16(root + 0xa0 + 14, 1); put32(root + 0xa0 + 16, 1033); put32(root + 0xa0 + 20, 0xc0);
    put32(root + 0xc0, 0x10e4); put32(root + 0xc4, 48);
    put32(root + 0x60, 0x10d0); put32(root + 0x64, 20);
    pe[0x2d0] = 0; pe[0x2d2] = 1; pe[0x2d4] = 1;
    pe[0x2d6] = 1; pe[0x2d7] = 1; pe[0x2da] = 1; pe[0x2dc] = 32;
    pe[0x2de] = 48; pe[0x2e2] = 1;
    // One-pixel 32bpp icon DIB: header + XOR BGRA + zeroed AND mask.
    put32(0x2e4, 40); put32(0x2e8, 1); put32(0x2ec, 2);
    put16(0x2f0, 1); put16(0x2f2, 32); put32(0x2f4, 0);
    pe[0x30c] = 0x11; pe[0x30d] = 0x22; pe[0x30e] = 0x33; pe[0x30f] = 0xff;

    pe::PeImageView64 image;
    TEST_ASSERT(image.inspect(pe) == NtStatus::Success, "synthetic PE fixture validates");
    pe::PeResourceView resources(image);
    std::array<uint16_t, 4> iconGroupIds{};
    size_t iconGroupCount{};
    TEST_ASSERT(resources.numericNames({14, {}, false}, iconGroupIds, iconGroupCount) == NtStatus::Success &&
                iconGroupCount == 2 && iconGroupIds[0] == 101 && iconGroupIds[1] == 202,
                "PE resource enumeration preserves numeric icon group ID order");
    std::array<uint16_t, 1> tooSmallGroupBuffer{};
    TEST_ASSERT(resources.numericNames({14, {}, false}, tooSmallGroupBuffer, iconGroupCount) ==
                    NtStatus::BufferTooSmall && iconGroupCount == 2,
                "PE resource enumeration reports required capacity without truncating silently");
    pe::PeResourceData result{};
    TEST_ASSERT(resources.find({14, {}, false}, {101, {}, false}, 1033, result) == NtStatus::Success,
                 "numeric type/name and exact language resolve");
    TEST_ASSERT(result.language == 1033 && result.codePage == 1200 && result.bytes.size() == 20 &&
                result.bytes[2] == 1 && result.bytes[4] == 1,
                "resource returns bounded file-backed bytes and metadata");
    pe::PeGroupIconView icons(resources);
    pe::PeIconResource icon{};
    TEST_ASSERT(icons.findIcon(101, 1033, 16, 16, 32, icon) == NtStatus::Success,
                "RT_GROUP_ICON resolves its RT_ICON resource ID");
    TEST_ASSERT(icon.entry.resourceId == 1 && icon.entry.width == 1 && icon.resource.bytes.size() == 48 &&
                icon.resource.bytes[0] == 40 && icon.resource.language == 1033,
                "icon variant metadata and payload are returned");
    um::imaging::ImageFrame peIcon{};
    TEST_ASSERT(um::imaging::DecodePeGroupIcon(pe, 101, 1033, 1, 1, peIcon, allocator) ==
                    um::imaging::ImageStatus::Success &&
                peIcon.width == 1 && peIcon.pixels[0] == 0x11 && peIcon.pixels[1] == 0x22 &&
                peIcon.pixels[2] == 0x33 && peIcon.pixels[3] == 0xff,
                "PE group traversal decodes a file-backed RT_ICON without executing the PE");
    TEST_ASSERT(resources.find({14, {}, false}, {102, {}, false}, 1033, result) == NtStatus::NotFound,
                 "missing resource is reported without synthetic data");
    put32(root + 0x60, 0x1200);
    pe::PeImageView64 tailImage;
    TEST_ASSERT(tailImage.inspect(pe) == NtStatus::Success, "PE remains structurally valid with bad resource RVA");
    pe::PeResourceView tailResources(tailImage);
    TEST_ASSERT(tailResources.find({14, {}, false}, {101, {}, false}, 1033, result) == NtStatus::InvalidImageFormat,
                 "resource in zero-filled virtual tail is rejected");
}

inline void Test_IconHandles_OwnershipCopyAndLeaseLifetime() {
    using namespace micant::um::imaging;
    using namespace micant::um::winuser;
    const ImageAllocator allocator{
        nullptr,
        [](void*, size_t bytes) noexcept -> void* { return std::malloc(bytes); },
        [](void*, void* address, size_t) noexcept { std::free(address); },
    };
    auto makeFrame = [&]() {
        ImageFrame frame{};
        frame.width = frame.height = 1; frame.stride = 4;
        frame.format = PixelFormat::Bgra8Premultiplied;
        if (frame.pixels.allocate(4, allocator)) {
            frame.pixels[0] = 10; frame.pixels[1] = 20;
            frame.pixels[2] = 30; frame.pixels[3] = 255;
        }
        return frame;
    };
    IconHandleTable& table = ProcessIconHandles();
    HICON shared = table.create(makeFrame(), allocator, true);
    TEST_ASSERT(shared, "shared icon gets an opaque validated handle");
    TEST_ASSERT(!table.destroy(shared), "DestroyIcon semantics reject shared icon destruction");
    HICON ownedCopy = table.copy(shared, allocator);
    TEST_ASSERT(ownedCopy && ownedCopy != shared, "CopyIcon produces an independent owned handle");
    IconHandleTable::Lease lease{};
    TEST_ASSERT(table.acquire(ownedCopy, lease), "owned icon handle can be referenced");
    TEST_ASSERT(table.destroy(ownedCopy), "owned icon is retired by DestroyIcon");
    IconHandleTable::Lease invalid{};
    TEST_ASSERT(!table.acquire(ownedCopy, invalid), "retired handle is no longer discoverable");
    TEST_ASSERT(lease.bitmap().pixels.size() == 4 && lease.bitmap().pixels[2] == 30,
                "active lease keeps immutable pixel storage alive after destroy");
    HICON scaledCopy = table.copyScaled(lease, 2, 2, allocator);
    TEST_ASSERT(scaledCopy && table.acquire(scaledCopy, invalid) &&
                invalid.bitmap().width == 2 && invalid.bitmap().height == 2,
                "CopyImage scaling creates a validated independent icon");
    invalid = {};
    TEST_ASSERT(table.destroy(scaledCopy), "scaled image copy has owned lifetime");
    lease = {};
    HICON windowIcon = table.create(makeFrame(), allocator, false);
    void* hwnd = reinterpret_cast<void*>(0x1234);
    TEST_ASSERT(windowIcon && ProcessWindowIcons().set(hwnd, 1, windowIcon) == nullptr,
                "window icon retains a valid owned HICON");
    TEST_ASSERT(table.destroy(windowIcon), "DestroyIcon retires an icon assigned to a window");
    IconHandleTable::Lease windowLease{};
    TEST_ASSERT(ProcessWindowIcons().acquire(windowIcon, windowLease) &&
                windowLease.bitmap().pixels[2] == 30,
                "window reference keeps assigned pixels available after caller destroy");
    windowLease = {};
    ProcessWindowIcons().remove(hwnd);
    TEST_ASSERT(table.releaseSharedAsset(shared), "owner can release shared icon at source teardown");
    TEST_ASSERT(table.ownedCount() == 0, "all owned icon records are reclaimed");
}

inline void Test_CursorHandles_HotspotAndTypedOwnership() {
    using namespace micant::um::imaging;
    using namespace micant::um::winuser;
    const ImageAllocator allocator{
        nullptr,
        [](void*, size_t bytes) noexcept -> void* { return std::malloc(bytes); },
        [](void*, void* address, size_t) noexcept { std::free(address); },
    };
    ImageFrame frame{};
    frame.width = frame.height = 2; frame.stride = 8;
    frame.format = PixelFormat::Bgra8Premultiplied;
    TEST_ASSERT(frame.pixels.allocate(16, allocator), "cursor fixture storage allocates");
    HICON cursor = ProcessIconHandles().createCursor(std::move(frame), allocator, 1, 0);
    TEST_ASSERT(cursor != nullptr, "cursor handle stores validated hotspot metadata");
    IconHandleTable::Lease lease{};
    TEST_ASSERT(ProcessIconHandles().acquireCursor(cursor, lease) &&
                lease.bitmap().cursor && lease.bitmap().hotX == 1 && lease.bitmap().hotY == 0,
                "cursor lease exposes its hotspot and cursor object type");
    IconHandleTable::Lease iconLease{};
    TEST_ASSERT(!ProcessIconHandles().acquire(cursor, iconLease),
                "cursor handle cannot be consumed as an icon");
    HICON copy = ProcessIconHandles().copyCursor(cursor, allocator);
    TEST_ASSERT(copy && ProcessIconHandles().acquireCursor(copy, iconLease) &&
                iconLease.bitmap().hotX == 1 && iconLease.bitmap().width == 2,
                "cursor copy preserves bitmap and hotspot");
    iconLease = {};
    TEST_ASSERT(!ProcessIconHandles().destroy(cursor) &&
                ProcessIconHandles().destroyCursor(cursor) && ProcessIconHandles().destroyCursor(copy),
                "owned cursors retire through the shared USER bitmap lifetime table");
    lease = {};
    ImageFrame bad{}; bad.width = bad.height = 1; bad.stride = 4;
    TEST_ASSERT(bad.pixels.allocate(4, allocator), "invalid cursor fixture allocates");
    TEST_ASSERT(!ProcessIconHandles().createCursor(std::move(bad), allocator, 1, 0),
                "hotspot outside image bounds is rejected");
}

inline void Test_Win32kCursorPositionClipAndVisibilityState() {
    using namespace micant;
    constexpr uint64_t owner = 0x435552534f52ull;
    MicaWin32kRequest request{};
    request.operation = MICANT_WIN32K_OP_SET_CURSOR_CLIP;
    request.args[0] = 1;
    request.rect = {10, 20, 30, 40};
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success,
                "Win32k accepts a valid owner-scoped cursor clip rectangle");
    request = {};
    request.operation = MICANT_WIN32K_OP_SET_CURSOR_POS;
    request.args[0] = static_cast<uint64_t>(static_cast<int64_t>(-10));
    request.args[1] = 100;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success,
                "Win32k applies cursor position through the active clip");
    request = {};
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_POS;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success &&
                request.rect.left == 10 && request.rect.top == 39,
                "cursor position reports the clipped screen point");
    request = {};
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_CLIP;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success &&
                request.rect.left == 10 && request.rect.bottom == 40,
                "cursor clip query returns the canonical desktop bounds");
    MicaWin32kInputEvent event{100, -5, 0, 1};
    (void)win32k::freestanding::dispatchMouseInput(event);
    request = {};
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_POS;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success &&
                request.rect.left == 29 && request.rect.top == 20,
                "hardware mouse coordinates obey the shared clip");
    request = {};
    request.operation = MICANT_WIN32K_OP_SHOW_CURSOR;
    request.args[0] = 1;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success && request.result == 2,
                "ShowCursor increments canonical visibility state");
    request = {};
    request.operation = MICANT_WIN32K_OP_SHOW_CURSOR;
    request.args[0] = static_cast<uint64_t>(static_cast<int64_t>(-1));
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success && request.result == 1,
                "ShowCursor decrements canonical visibility state");
    MicaCompositorMetadataStore metadata{};
    win32k::freestanding::bindCompositorStore(&metadata, nullptr);
    MicaWin32kCursorImage image{};
    image.size = sizeof(image); image.version = MICANT_WIN32K_CURSOR_IMAGE_VERSION;
    image.cursorHandle = 0x435552534f52ull;
    image.width = image.height = image.pitch = 1;
    image.pixels[0] = 0x80800000u;
    TEST_ASSERT(win32k::freestanding::setCursorImage(image, owner) == NtStatus::Success,
                "Win32k accepts bounded cursor image data from its owner");
    auto invalidImage = image;
    invalidImage.pixels[0] = 0x808000FFu;
    TEST_ASSERT(win32k::freestanding::setCursorImage(invalidImage, owner) == NtStatus::InvalidParameter,
                "Win32k rejects non-premultiplied cursor pixels without replacing active state");
    request = {};
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_HANDLE;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success &&
                request.result == image.cursorHandle,
                "current process sees its active cursor handle");
    request = {};
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_HANDLE;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner + 1) == NtStatus::Success &&
                request.result == 0,
                "active cursor handles are not exposed to another process");
    win32k::freestanding::publishCompositorSurfaces();
    TEST_ASSERT(metadata.cursor.width == 1 && metadata.cursor.hotX == 0 &&
                metadata.cursor.pixels[0] == image.pixels[0] &&
                (metadata.cursor.flags & MICANT_COMPOSITOR_CURSOR_ACTIVE),
                "compositor metadata receives the validated cursor overlay image");
    win32k::freestanding::releaseProcess(owner);
    win32k::freestanding::publishCompositorSurfaces();
    request = {};
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_HANDLE;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success &&
                request.result == 0 && metadata.cursor.width == 16 &&
                (metadata.cursor.flags & MICANT_COMPOSITOR_CURSOR_ACTIVE),
                "process teardown releases its cursor and restores the safe arrow");
    request = {};
    request.operation = MICANT_WIN32K_OP_REGISTER_CLASS;
    request.className[0] = 'C'; request.className[1] = 'U'; request.className[2] = 'R';
    request.className[3] = 0; request.args[0] = 1;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success,
                "Win32k registers a cursor test window class");
    request = {};
    request.operation = MICANT_WIN32K_OP_CREATE_WINDOW;
    request.className[0] = 'C'; request.className[1] = 'U'; request.className[2] = 'R';
    request.className[3] = 0; request.title[0] = 'T'; request.title[1] = 0;
    request.args[1] = 0x10000000u; request.args[2] = 10; request.args[3] = 20;
    request.args[4] = 100; request.args[5] = 80;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success && request.result,
                "Win32k creates a visible cursor test window");
    const MicaWin32kInputEvent hover{15, 25, 0, 2};
    (void)win32k::freestanding::dispatchMouseInput(hover);
    request = {};
    request.operation = MICANT_WIN32K_OP_PEEK_MESSAGE;
    request.args[1] = request.args[2] = 0x0020;
    request.args[3] = 1;
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success &&
                request.result && request.messageId == 0x0020 && request.messageWParam &&
                (request.messageLParam & 0xFFFF) == 1,
                "mouse hit-test transition routes WM_SETCURSOR with HTCLIENT");
    request = {};
    request.operation = MICANT_WIN32K_OP_SET_CURSOR_CLIP;
    win32k::freestanding::releaseProcess(owner);
    TEST_ASSERT(win32k::freestanding::dispatchRequest(request, owner) == NtStatus::Success,
                "null ClipCursor restores the full desktop bounds");
}

inline void Test_IconDrawing_PremultipliedSourceOver() {
    using micant::shared::graphics::PremultipliedSourceOver;
    TEST_ASSERT(PremultipliedSourceOver(0x80800000u, 0xff0000ffu) == 0xff80007fu,
                "half-alpha premultiplied source blends over opaque destination");
    TEST_ASSERT(PremultipliedSourceOver(0x00000000u, 0x7f123456u) == 0x7f123456u,
                "transparent source preserves destination color and alpha");
    TEST_ASSERT(PremultipliedSourceOver(0xff102030u, 0x00405060u) == 0xff102030u,
                "opaque icon source replaces destination");
}
