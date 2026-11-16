#pragma once

// WhiteSur caption PNGs live in a separate bounded image set. Missing files
// are valid and select the compositor's native glyph fallback.
#include "micant/shared/theme_snapshot.h"
#include "micant/um/subsystems/imaging/bmp_png_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace micant::um::theme {

inline void LoadWhiteSurCaptionImages(MicaThemeCaptionImageSet& imageSet,
                                      const wchar_t* profileDirectory,
                                      bool dark,
                                      size_t imageLimit = MICANT_THEME_CAPTION_IMAGE_COUNT) noexcept {
    using namespace micant::um::imaging;
    if (!profileDirectory) return;
    imageSet = {};
    static constexpr const wchar_t* names[] = {
        L"close", L"minimize", L"maximize", L"restore"
    };
    static constexpr const wchar_t* suffixes[] = {
        L"", L"-hover", L"-active", L"-backdrop", L"-backdrop-hover"
    };
    const auto allocator = NativeImageAllocator();
    ImageLimits limits{};
    limits.maxEncodedBytes = 8 * 1024;
    limits.maxPixels = MICANT_THEME_CAPTION_IMAGE_PIXELS;
    limits.maxDecodedBytes = 4096;
    limits.maxIntermediateBytes = 16 * 1024;
    imageLimit = std::min(imageLimit,
        static_cast<size_t>(MICANT_THEME_CAPTION_IMAGE_COUNT));
    for (size_t imageIndex = 0; imageIndex < imageLimit; ++imageIndex) {
            const size_t button = imageIndex / std::size(suffixes);
            const size_t state = imageIndex % std::size(suffixes);
            wchar_t path[512]{};
            size_t cursor = 0;
            while (profileDirectory[cursor] && cursor + 1 < std::size(path)) {
                path[cursor] = profileDirectory[cursor];
                ++cursor;
            }
            if (profileDirectory[cursor]) continue;
            const auto append = [&](const wchar_t* value) noexcept {
                size_t i = 0;
                while (value[i]) {
                    if (cursor + 1 >= std::size(path)) return false;
                    path[cursor++] = value[i++];
                }
                path[cursor] = L'\0';
                return true;
            };
            if (!append(L"\\windows-assets\\titlebutton-") ||
                !append(names[button]) || !append(suffixes[state]) ||
                (dark && !append(L"-dark")) || !append(L".png")) continue;
            HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) continue;
            LARGE_INTEGER fileSize{};
            std::array<uint8_t, 8 * 1024> encoded{};
            DWORD bytesRead = 0;
            const bool readOk = GetFileSizeEx(file, &fileSize) &&
                fileSize.QuadPart > 0 && fileSize.QuadPart <= encoded.size() &&
                ReadFile(file, encoded.data(), static_cast<DWORD>(fileSize.QuadPart),
                         &bytesRead, nullptr) && bytesRead == fileSize.QuadPart;
            CloseHandle(file);
            if (!readOk) continue;
            ImageFrame decoded{};
            if (DecodePng(std::span<const uint8_t>(encoded.data(), bytesRead), decoded,
                          allocator, limits) != ImageStatus::Success ||
                decoded.sourceFormat != ImageFormat::Png ||
                decoded.format != PixelFormat::Bgra8Premultiplied ||
                decoded.sourceColor != ImageColorModel::Rgb ||
                decoded.bitsPerChannel != 8 || decoded.orientation != 1 ||
                decoded.frameCount != 1 || !decoded.sourceHadAlpha ||
                !decoded.width || !decoded.height ||
                decoded.width > MICANT_THEME_CAPTION_IMAGE_SIZE ||
                decoded.height > MICANT_THEME_CAPTION_IMAGE_SIZE ||
                decoded.stride < decoded.width * 4u ||
                decoded.pixels.size() != static_cast<size_t>(decoded.stride) * decoded.height)
                continue;
            auto& target = imageSet.images[imageIndex];
            target = {};
            target.width = static_cast<uint16_t>(decoded.width);
            target.height = static_cast<uint16_t>(decoded.height);
            target.stride = MICANT_THEME_CAPTION_IMAGE_SIZE;
            auto* targetPixels = imageSet.pixels[imageIndex];
            bool validAlpha = true;
            for (uint32_t y = 0; y < decoded.height; ++y) {
                const auto* sourceRow = reinterpret_cast<const uint32_t*>(
                    decoded.pixels.data() + static_cast<size_t>(y) * decoded.stride);
                for (uint32_t x = 0; x < decoded.width; ++x) {
                    const uint32_t pixel = sourceRow[x];
                    const uint32_t alpha = pixel >> 24;
                    if (((pixel >> 16) & 0xFFu) > alpha ||
                        ((pixel >> 8) & 0xFFu) > alpha || (pixel & 0xFFu) > alpha)
                        validAlpha = false;
                }
            }
            if (!validAlpha) {
                target = {};
                continue;
            }
            for (uint32_t y = 0; y < decoded.height; ++y)
                std::memcpy(targetPixels + static_cast<size_t>(y) *
                                MICANT_THEME_CAPTION_IMAGE_SIZE,
                    decoded.pixels.data() + static_cast<size_t>(y) * decoded.stride,
                    static_cast<size_t>(decoded.width) * sizeof(uint32_t));
    }
}

} // namespace micant::um::theme
