#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::um::graphics {

struct LogicalFontRequest {
    const char16_t* requestedFamily{};
    uint16_t weight{400};
    bool italic{};
    uint16_t pointSize{9};
    uint16_t dpi{96};
};

struct FontAssetChoice {
    const char16_t* fileName{};
    const char16_t* unicodeFallbackFile{u"DejaVuSans-Regular.ttf"};
    const char16_t* requestedFamily{};
    uint16_t pointSize{9};
    uint16_t dpi{96};
    bool italicAvailable{};
    bool usedFamilyFallback{};
};

namespace detail {
[[nodiscard]] constexpr char16_t foldAscii(char16_t value) noexcept {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

[[nodiscard]] constexpr bool equalAsciiInsensitive(const char16_t* left,
                                                    const char16_t* right) noexcept {
    if (!left || !right) return false;
    size_t i = 0;
    while (left[i] && right[i]) {
        if (foldAscii(left[i]) != foldAscii(right[i])) return false;
        ++i;
    }
    return left[i] == right[i];
}
}

// Resolves a requested Windows family to an open system substitute while the
// request itself remains intact for HFONT/GetTextMetrics compatibility.
[[nodiscard]] constexpr FontAssetChoice resolveFontAsset(
    const LogicalFontRequest& request) noexcept {
    const char16_t* family = request.requestedFamily;
    const bool heavy = request.weight >= 700;
    const bool semi = request.weight >= 600 && request.weight < 700;
    const bool italic = request.italic;
    const bool fallbackHeavy = heavy || semi;
    const char16_t* fallbackFile = italic
        ? (fallbackHeavy ? u"DejaVuSans-BoldItalic.ttf" : u"DejaVuSans-Italic.ttf")
        : (fallbackHeavy ? u"DejaVuSans-Bold.ttf" : u"DejaVuSans-Regular.ttf");
    const char16_t* file = nullptr;
    bool italicAvailable = false;
    bool known = true;
    if (detail::equalAsciiInsensitive(family, u"Segoe UI") ||
        detail::equalAsciiInsensitive(family, u"Segoe UI Semibold")) {
        file = semi || detail::equalAsciiInsensitive(family, u"Segoe UI Semibold")
            ? u"Selawik-Semibold.ttf" : (heavy ? u"Selawik-Bold.ttf" : u"Selawik-Regular.ttf");
        italicAvailable = false;
    } else if (detail::equalAsciiInsensitive(family, u"Arial")) {
        file = italic ? (heavy ? u"LiberationSans-BoldItalic.ttf" : u"LiberationSans-Italic.ttf")
                      : (heavy ? u"LiberationSans-Bold.ttf" : u"LiberationSans-Regular.ttf");
        italicAvailable = true;
    } else if (detail::equalAsciiInsensitive(family, u"Times New Roman")) {
        file = italic ? (heavy ? u"LiberationSerif-BoldItalic.ttf" : u"LiberationSerif-Italic.ttf")
                      : (heavy ? u"LiberationSerif-Bold.ttf" : u"LiberationSerif-Regular.ttf");
        italicAvailable = true;
    } else if (detail::equalAsciiInsensitive(family, u"Courier New")) {
        file = italic ? (heavy ? u"LiberationMono-BoldItalic.ttf" : u"LiberationMono-Italic.ttf")
                      : (heavy ? u"LiberationMono-Bold.ttf" : u"LiberationMono-Regular.ttf");
        italicAvailable = true;
    } else if (detail::equalAsciiInsensitive(family, u"Calibri")) {
        file = italic ? (heavy ? u"Carlito-BoldItalic.ttf" : u"Carlito-Italic.ttf")
                      : (heavy ? u"Carlito-Bold.ttf" : u"Carlito-Regular.ttf");
        italicAvailable = true;
    } else {
        known = false;
        file = italic ? (heavy ? u"DejaVuSans-BoldItalic.ttf" : u"DejaVuSans-Italic.ttf")
                      : (heavy ? u"DejaVuSans-Bold.ttf" : u"DejaVuSans-Regular.ttf");
        italicAvailable = true;
    }
    if (italic && !italicAvailable) {
        file = heavy ? u"DejaVuSans-BoldItalic.ttf" : u"DejaVuSans-Italic.ttf";
        italicAvailable = true;
    }
    return {file, fallbackFile, family, request.pointSize,
            request.dpi ? request.dpi : static_cast<uint16_t>(96), italicAvailable, !known};
}

struct UiFontDefaults {
    static constexpr uint16_t TextPoints = 9;
    static constexpr uint16_t MenuPoints = 9;
    static constexpr uint16_t ButtonPoints = 9;
    static constexpr uint16_t WindowTitlePoints = 9;
    static constexpr uint16_t LargeTitlePoints = 14;
    static constexpr uint16_t ConsolePoints = 10;
    static constexpr uint16_t DocumentPoints = 11;
    static constexpr uint16_t ReferenceDpi = 96;
};

} // namespace micant::um::graphics
