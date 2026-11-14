#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include "micant/shared/native_nt_api.hpp"
#include "micant/um/subsystems/graphics/font_catalog.hpp"
#include "micant/um/subsystems/graphics/truetype.hpp"

namespace micant::um::graphics {

struct TextExtentPixels { int32_t width{}; int32_t height{}; };
struct FontMetricsPixels {
    int32_t ascent{};
    int32_t descent{};
    int32_t lineGap{};
    int32_t lineHeight{};
    int32_t averageAdvance{};
    int32_t maximumAdvance{};
};

// Per-process Ring 3 font collection for the installed system faces. Font
// bytes, parser views and scratch memory remain owned by this manager.
class FontManager {
public:
    [[nodiscard]] bool initialize(const LogicalFontRequest& request) noexcept {
        return initializeInternal(request, true);
    }

    // Parse and retain only the selected face's metrics. Font enumeration does
    // not need a fallback face or raster scratch workspace for each style.
    [[nodiscard]] bool initializeForMetrics(const LogicalFontRequest& request) noexcept {
        return initializeInternal(request, false);
    }

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] const FontAssetChoice& selectedAsset() const noexcept { return asset_; }
    [[nodiscard]] const truetype::FaceMetrics& metrics() const noexcept { return primary_.metrics(); }

    [[nodiscard]] bool pixelMetrics(FontMetricsPixels& result) const noexcept {
        result = {};
        if (!initialized_) return false;
        const uint32_t pxEm = truetype::pixelsFromPoints(asset_.pointSize, asset_.dpi);
        const auto& m = primary_.metrics();
        if (!pxEm || !m.unitsPerEm) return false;
        result.ascent = scale(m.ascender, pxEm, m.unitsPerEm);
        result.descent = -scale(m.descender, pxEm, m.unitsPerEm);
        result.lineGap = scale(m.lineGap, pxEm, m.unitsPerEm);
        result.lineHeight = result.ascent + result.descent + result.lineGap;
        if (result.lineHeight <= 0) result.lineHeight = result.ascent + result.descent;
        uint32_t totalAdvance = 0;
        uint32_t maximumAdvance = 0;
        uint32_t count = 0;
        for (uint32_t scalar = 32; scalar <= 126; ++scalar) {
            const auto* face = &primary_;
            uint16_t glyph = face->glyphIndex(scalar);
            truetype::GlyphMetrics gm{};
            if (!glyph && fallback_.valid()) {
                face = &fallback_;
                glyph = face->glyphIndex(scalar);
            }
            if (!glyph || !face->glyphMetrics(glyph, gm)) continue;
            const int32_t advance = scale(gm.advanceWidth, pxEm, face->metrics().unitsPerEm);
            if (advance < 0) return false;
            totalAdvance += static_cast<uint32_t>(advance);
            if (static_cast<uint32_t>(advance) > maximumAdvance)
                maximumAdvance = static_cast<uint32_t>(advance);
            ++count;
        }
        if (!count || maximumAdvance > INT32_MAX) return false;
        result.averageAdvance = static_cast<int32_t>((totalAdvance + count / 2) / count);
        result.maximumAdvance = static_cast<int32_t>(maximumAdvance);
        return result.lineHeight > 0;
    }

    [[nodiscard]] bool measureUtf8(const char* text, TextExtentPixels& extent) const noexcept {
        extent = {};
        if (!initialized_ || !text) return false;
        constexpr size_t MaxTextBytes = 4096;
        const uint32_t pxEm = truetype::pixelsFromPoints(asset_.pointSize, asset_.dpi);
        const auto& m = primary_.metrics();
        if (!pxEm || !m.unitsPerEm) return false;
        int32_t pen = 0, maxWidth = 0;
        uint32_t lines = 1;
        FontMetricsPixels projectedMetrics{};
        if (!pixelMetrics(projectedMetrics)) return false;
        const int32_t effectiveLineHeight = projectedMetrics.lineHeight;
        if (effectiveLineHeight <= 0) return false;
        const truetype::FaceView* previousFace = nullptr;
        uint16_t previousGlyph = 0;
        size_t cursor = 0;
        while (text[cursor]) {
            if (cursor >= MaxTextBytes) return false;
            uint32_t scalar{};
            const size_t consumed = decodeUtf8(text + cursor, scalar);
            if (!consumed) return false;
            cursor += consumed;
            if (scalar == '\n') {
                if (pen > maxWidth) maxWidth = pen;
                pen = 0; ++lines; previousFace = nullptr; previousGlyph = 0;
                continue;
            }
            uint32_t baseScalar = scalar, markScalar = 0;
            decomposeAccent(scalar, baseScalar, markScalar);
            const truetype::FaceView* face = &primary_;
            uint16_t glyph = face->glyphIndex(baseScalar);
            truetype::GlyphMetrics gm{};
            if ((!glyph || !face->glyphMetrics(glyph, gm)) && fallback_.valid()) {
                face = &fallback_;
                glyph = face->glyphIndex(baseScalar);
            }
            if (!glyph || !face->glyphMetrics(glyph, gm)) return false;
            if (previousGlyph && previousFace == face) {
                const int32_t kern = scale(face->kerning(previousGlyph, glyph), pxEm,
                                           face->metrics().unitsPerEm);
                if ((kern < 0 && pen < -kern) || (kern > 0 && pen > INT32_MAX - kern))
                    return false;
                pen += kern;
            }
            const int32_t advance = scale(gm.advanceWidth, pxEm, face->metrics().unitsPerEm);
            if (advance < 0 || pen > INT32_MAX - advance) return false;
            pen += advance;
            previousFace = face;
            previousGlyph = glyph;
            (void)markScalar;
        }
        if (pen > maxWidth) maxWidth = pen;
        if (lines > static_cast<uint32_t>(INT32_MAX / effectiveLineHeight)) return false;
        extent = {maxWidth, static_cast<int32_t>(lines) * effectiveLineHeight};
        return true;
    }

    void shutdown() noexcept {
        initialized_ = false;
        release(primaryBytes_);
        release(fallbackBytes_);
        release(workspace_);
        workspaceSize_ = 0;
        primary_ = {};
        fallback_ = {};
        asset_ = {};
    }

    // Draw a UTF-8 string onto an owned BGRA8 buffer. Input and output are
    // clipped to the supplied dimensions; y is the top of the em box.
    [[nodiscard]] bool drawUtf8(uint32_t* pixels, uint32_t width, uint32_t height,
            int32_t x, int32_t y, const char* text, uint32_t color) noexcept {
        if (!initialized_ || !workspace_ || !pixels || !width || !height || !text || width > 16384 || height > 16384)
            return false;
        constexpr uint32_t MaxTextBytes = 4096;
        const uint32_t pxEm = truetype::pixelsFromPoints(asset_.pointSize, asset_.dpi);
        if (!pxEm) return false;
        const auto& primaryMetrics = primary_.metrics();
        int32_t baseline = y + scale(primaryMetrics.ascender, pxEm, primaryMetrics.unitsPerEm);
        int32_t lineHeight = scale(static_cast<int32_t>(primaryMetrics.ascender) -
            primaryMetrics.descender + primaryMetrics.lineGap, pxEm, primaryMetrics.unitsPerEm);
        if (lineHeight <= 0) lineHeight = scale(
            static_cast<int32_t>(primaryMetrics.ascender) - primaryMetrics.descender,
            pxEm, primaryMetrics.unitsPerEm);
        if (lineHeight <= 0) return false;
        int32_t pen = x;
        const truetype::FaceView* previousFace = nullptr;
        uint16_t previousGlyph = 0;
        size_t cursor = 0;
        uint8_t mask[GlyphMaskSize * GlyphMaskSize]{};
        while (text[cursor]) {
            if (cursor >= MaxTextBytes) return false;
            uint32_t scalar{};
            const size_t consumed = decodeUtf8(text + cursor, scalar);
            if (!consumed) return false;
            cursor += consumed;
            if (scalar == '\n') {
                pen = x;
                if (baseline > INT32_MAX - lineHeight) return false;
                baseline += lineHeight;
                previousFace = nullptr;
                previousGlyph = 0;
                continue;
            }
            uint32_t baseScalar = scalar, markScalar = 0;
            decomposeAccent(scalar, baseScalar, markScalar);
            const truetype::FaceView* selected = &primary_;
            uint16_t glyph = selected->glyphIndex(baseScalar);
            truetype::GlyphMetrics gm{};
            if (!glyph && fallback_.valid()) {
                selected = &fallback_;
                glyph = selected->glyphIndex(baseScalar);
            }
            if (!glyph || !selected->glyphMetrics(glyph, gm)) return false;
            if (previousGlyph && previousFace == selected) {
                const int32_t kern = scale(selected->kerning(previousGlyph, glyph), pxEm,
                                           selected->metrics().unitsPerEm);
                pen += kern;
            }
            bool painted = drawGlyph(*selected, glyph, pxEm, pen, baseline, pixels,
                                     width, height, color, mask);
            if (!painted && selected == &primary_ && fallback_.valid()) {
                const uint16_t fallbackGlyph = fallback_.glyphIndex(baseScalar);
                truetype::GlyphMetrics fallbackMetrics{};
                if (fallbackGlyph && fallback_.glyphMetrics(fallbackGlyph, fallbackMetrics) &&
                    drawGlyph(fallback_, fallbackGlyph, pxEm, pen, baseline, pixels,
                              width, height, color, mask)) {
                    selected = &fallback_;
                    glyph = fallbackGlyph;
                    gm = fallbackMetrics;
                    painted = true;
                }
            }
            if (!painted) return false;
            if (markScalar && !drawCombiningMark(*selected, glyph, markScalar, pxEm,
                    pen, baseline, pixels, width, height, color, mask)) return false;
            const int32_t advance = scale(gm.advanceWidth, pxEm, selected->metrics().unitsPerEm);
            if (advance < 0 || pen > INT32_MAX - advance) return false;
            pen += advance;
            previousFace = selected;
            previousGlyph = glyph;
        }
        return true;
    }

private:
    [[nodiscard]] bool initializeInternal(const LogicalFontRequest& request,
                                          bool rasterRequired) noexcept {
        if (initialized_ && (!rasterRequired || workspace_)) return true;
        if (initialized_) shutdown();
        asset_ = resolveFontAsset(request);
        size_t primaryLength{};
        if (!loadFile(asset_.fileName, primaryBytes_, primaryLength) ||
            !primary_.parse(primaryBytes_, primaryLength)) {
            release(primaryBytes_);
            return false;
        }
        if (rasterRequired) {
            size_t fallbackLength{};
            if (loadFile(asset_.unicodeFallbackFile, fallbackBytes_, fallbackLength)) {
                if (!fallback_.parse(fallbackBytes_, fallbackLength)) release(fallbackBytes_);
            }
            workspaceSize_ = truetype::FaceView::rasterWorkspaceBytes();
            if (native_nt::NtAllocateVirtualMemory(static_cast<Handle>(-1), &workspace_, 0,
                    &workspaceSize_, 0x3000, 0x04) != NtStatus::Success || !workspace_) {
                release(primaryBytes_);
                release(fallbackBytes_);
                workspace_ = nullptr; workspaceSize_ = 0;
                primary_ = {};
                fallback_ = {};
                return false;
            }
        }
        initialized_ = true;
        return true;
    }
    static constexpr uint32_t GlyphMaskSize = 64;
    static constexpr uint32_t MaxFontBytes = 16 * 1024 * 1024;

    [[nodiscard]] static int32_t scale(int32_t value, uint32_t pxEm, uint32_t units) noexcept {
        if (!units) return 0;
        const int64_t product = static_cast<int64_t>(value) * pxEm;
        return static_cast<int32_t>(product / units);
    }

    static void release(void*& memory) noexcept {
        if (!memory) return;
        size_t size = 0;
        (void)native_nt::NtFreeVirtualMemory(static_cast<Handle>(-1), &memory, &size, 0x8000);
        memory = nullptr;
    }

    static void release(uint8_t*& memory) noexcept {
        void* allocation = memory;
        release(allocation);
        memory = nullptr;
    }

    [[nodiscard]] static bool loadFile(const char16_t* filename, void*& memory,
                                       size_t& fileLength) noexcept {
        memory = nullptr; fileLength = 0;
        if (!filename) return false;
        constexpr char16_t folder[] = u"\\System\\Fonts\\";
        wchar_t path[320]{};
        const auto* shared = reinterpret_cast<const KUserSharedData*>(UserSharedDataAddress);
        size_t rootLength = 0, nameLength = 0;
        while (rootLength < std::size(shared->ntSystemRoot) && shared->ntSystemRoot[rootLength]) ++rootLength;
        while (filename[nameLength]) { if (++nameLength >= std::size(path)) return false; }
        if (rootLength < 3 || rootLength + std::size(folder) + nameLength > std::size(path)) return false;
        for (size_t i = 0; i < rootLength; ++i) path[i] = static_cast<wchar_t>(shared->ntSystemRoot[i]);
        for (size_t i = 0; i + 1 < std::size(folder); ++i)
            path[rootLength + i] = static_cast<wchar_t>(folder[i]);
        for (size_t i = 0; i <= nameLength; ++i)
            path[rootLength + std::size(folder) - 1 + i] = static_cast<wchar_t>(filename[i]);
        UnicodeString name{path};
        ObjectAttributes attributes{};
        attributes.objectName = &name;
        attributes.attributes = OBJ_CASE_INSENSITIVE;
        Handle file{};
        IoStatusBlock io{};
        if (native_nt::NtCreateFile(&file, 0x00100081u, &attributes, &io, nullptr,
                0, 7, 1, 0x20, nullptr, 0) != NtStatus::Success) return false;
        FileStandardInformation info{};
        if (native_nt::NtQueryInformationFile(file, &io, &info, sizeof(info),
                FileInformationClass::FileStandardInformation) != NtStatus::Success ||
            info.endOfFile.QuadPart <= 0 || info.endOfFile.QuadPart > MaxFontBytes) {
            (void)native_nt::NtClose(file);
            return false;
        }
        fileLength = static_cast<size_t>(info.endOfFile.QuadPart);
        size_t allocationSize = fileLength;
        if (native_nt::NtAllocateVirtualMemory(static_cast<Handle>(-1), &memory, 0,
                &allocationSize, 0x3000, 0x04) != NtStatus::Success || !memory) {
            (void)native_nt::NtClose(file);
            fileLength = 0;
            return false;
        }
        size_t completed = 0;
        while (completed < fileLength) {
            const uint32_t request = static_cast<uint32_t>((fileLength - completed) > 64 * 1024
                ? 64 * 1024 : fileLength - completed);
            LargeInteger offset{};
            offset.QuadPart = static_cast<int64_t>(completed);
            const auto status = native_nt::NtReadFile(file, 0, nullptr, nullptr, &io,
                static_cast<uint8_t*>(memory) + completed, request, &offset, nullptr);
            if (status != NtStatus::Success || !io.Information || io.Information > request) {
                release(memory);
                fileLength = 0;
                (void)native_nt::NtClose(file);
                return false;
            }
            completed += static_cast<size_t>(io.Information);
        }
        (void)native_nt::NtClose(file);
        return true;
    }

    [[nodiscard]] static size_t decodeUtf8(const char* input, uint32_t& scalar) noexcept {
        const auto b0 = static_cast<uint8_t>(input[0]);
        if (b0 < 0x80) { scalar = b0; return 1; }
        uint32_t value{}; size_t count{};
        if (b0 >= 0xC2 && b0 <= 0xDF) { value = b0 & 0x1F; count = 2; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { value = b0 & 0x0F; count = 3; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { value = b0 & 0x07; count = 4; }
        else return 0;
        for (size_t i = 1; i < count; ++i) {
            const auto byte = static_cast<uint8_t>(input[i]);
            if (!byte || (byte & 0xC0) != 0x80) return 0;
            value = (value << 6) | (byte & 0x3F);
        }
        if ((count == 3 && value < 0x800) || (count == 4 && value < 0x10000) ||
            value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 0;
        scalar = value;
        return count;
    }

    static void decomposeAccent(uint32_t scalar, uint32_t& base, uint32_t& mark) noexcept {
        switch (scalar) {
        case 0x00E0: base = 'a'; mark = 0x0300; break;
        case 0x00E8: base = 'e'; mark = 0x0300; break;
        case 0x00E9: base = 'e'; mark = 0x0301; break;
        case 0x00EC: base = 'i'; mark = 0x0300; break;
        case 0x00F2: base = 'o'; mark = 0x0300; break;
        case 0x00F9: base = 'u'; mark = 0x0300; break;
        case 0x00C0: base = 'A'; mark = 0x0300; break;
        case 0x00C8: base = 'E'; mark = 0x0300; break;
        case 0x00C9: base = 'E'; mark = 0x0301; break;
        case 0x00CC: base = 'I'; mark = 0x0300; break;
        case 0x00D2: base = 'O'; mark = 0x0300; break;
        case 0x00D9: base = 'U'; mark = 0x0300; break;
        case 0x0108: base = 'C'; mark = 0x0302; break;
        case 0x0109: base = 'c'; mark = 0x0302; break;
        default: base = scalar; mark = 0; break;
        }
    }

    [[nodiscard]] bool drawGlyph(const truetype::FaceView& face, uint16_t glyph,
            uint32_t pxEm, int32_t pen, int32_t baseline, uint32_t* pixels,
            uint32_t width, uint32_t height, uint32_t color, uint8_t* mask) noexcept {
        const uint8_t* outline{}; size_t outlineSize{};
        if (!face.glyphData(glyph, outline, outlineSize)) return false;
        if (!outlineSize) return true;
        int16_t xMin{}, yMin{}, xMax{}, yMax{};
        if (!face.glyphBounds(glyph, xMin, yMin, xMax, yMax)) return false;
        for (size_t i = 0; i < GlyphMaskSize * GlyphMaskSize; ++i) mask[i] = 0;
        if (!face.rasterizeSimpleGlyph(glyph, pxEm, mask, GlyphMaskSize,
                GlyphMaskSize, GlyphMaskSize, workspace_, workspaceSize_)) return false;
        const int32_t left = pen + scale(xMin, pxEm, face.metrics().unitsPerEm);
        const int32_t top = baseline - scale(yMax, pxEm, face.metrics().unitsPerEm);
        for (uint32_t y = 0; y < GlyphMaskSize; ++y) {
            const int32_t dy = top + static_cast<int32_t>(y);
            if (dy < 0 || dy >= static_cast<int32_t>(height)) continue;
            for (uint32_t x = 0; x < GlyphMaskSize; ++x) {
                const int32_t dx = left + static_cast<int32_t>(x);
                if (dx < 0 || dx >= static_cast<int32_t>(width)) continue;
                const uint32_t alpha = mask[y * GlyphMaskSize + x];
                if (!alpha) continue;
                uint32_t& target = pixels[static_cast<size_t>(dy) * width + static_cast<uint32_t>(dx)];
                const uint32_t inverse = 255 - alpha;
                const uint32_t red = (((color >> 16) & 255) * alpha + ((target >> 16) & 255) * inverse + 127) / 255;
                const uint32_t green = (((color >> 8) & 255) * alpha + ((target >> 8) & 255) * inverse + 127) / 255;
                const uint32_t blue = ((color & 255) * alpha + (target & 255) * inverse + 127) / 255;
                target = 0xFF000000u | (red << 16) | (green << 8) | blue;
            }
        }
        return true;
    }

    [[nodiscard]] bool drawCombiningMark(const truetype::FaceView& baseFace, uint16_t baseGlyph,
            uint32_t markScalar, uint32_t pxEm, int32_t pen, int32_t baseline,
            uint32_t* pixels, uint32_t width, uint32_t height, uint32_t color,
            uint8_t* mask) noexcept {
        const truetype::FaceView* markFace = &baseFace;
        uint16_t markGlyph = markFace->glyphIndex(markScalar);
        if (!markGlyph && fallback_.valid()) { markFace = &fallback_; markGlyph = markFace->glyphIndex(markScalar); }
        int16_t bx0{}, by0{}, bx1{}, by1{}, mx0{}, my0{}, mx1{}, my1{};
        if (!markGlyph || !baseFace.glyphBounds(baseGlyph, bx0, by0, bx1, by1) ||
            !markFace->glyphBounds(markGlyph, mx0, my0, mx1, my1)) return false;
        const int32_t baseWidth = scale(bx1 - bx0, pxEm, baseFace.metrics().unitsPerEm);
        const int32_t markWidth = scale(mx1 - mx0, pxEm, markFace->metrics().unitsPerEm);
        int32_t markPen = pen + scale(bx0, pxEm, baseFace.metrics().unitsPerEm) +
                          (baseWidth - markWidth) / 2 - scale(mx0, pxEm, markFace->metrics().unitsPerEm);
        if (drawGlyph(*markFace, markGlyph, pxEm, markPen, baseline,
                      pixels, width, height, color, mask)) return true;
        if (markFace == &primary_ && fallback_.valid()) {
            markFace = &fallback_;
            markGlyph = markFace->glyphIndex(markScalar);
            if (!markGlyph || !markFace->glyphBounds(markGlyph, mx0, my0, mx1, my1)) return false;
            const int32_t fallbackWidth = scale(mx1 - mx0, pxEm, markFace->metrics().unitsPerEm);
            markPen = pen + scale(bx0, pxEm, baseFace.metrics().unitsPerEm) +
                      (baseWidth - fallbackWidth) / 2 - scale(mx0, pxEm, markFace->metrics().unitsPerEm);
            return drawGlyph(*markFace, markGlyph, pxEm, markPen, baseline,
                             pixels, width, height, color, mask);
        }
        return false;
    }

    FontAssetChoice asset_{};
    void* primaryBytes_{};
    void* fallbackBytes_{};
    size_t workspaceSize_{};
    void* workspace_{};
    truetype::FaceView primary_{};
    truetype::FaceView fallback_{};
    bool initialized_{};
};

} // namespace micant::um::graphics
