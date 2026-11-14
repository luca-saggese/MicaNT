#pragma once

#include "micant/um/subsystems/imaging/ico_decoder.hpp"
#include <cstdlib>
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
