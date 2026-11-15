#pragma once

#include "micant/um/subsystems/theme/gtk_css.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace micant::um::theme {

struct CssAccentOverride {
    CssColor source{8, 96, 242, 255};
    CssColor replacement{};
    bool enabled{};
};

inline bool parseCssNumber(std::string_view text, double& value, bool& percent) noexcept {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    percent = !text.empty() && text.back() == '%';
    if (percent) text.remove_suffix(1);
    if (text.empty()) return false;
    size_t i = 0;
    bool negative = false;
    if (text[i] == '+' || text[i] == '-') { negative = text[i] == '-'; ++i; }
    double whole = 0.0;
    bool digit = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        whole = whole * 10.0 + (text[i++] - '0');
        if (whole > 1000000.0) return false;
        digit = true;
    }
    double fraction = 0.0;
    double scale = 1.0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (scale < 1000000.0) { scale *= 10.0; fraction = fraction * 10.0 + (text[i] - '0'); }
            ++i; digit = true;
        }
    }
    if (!digit || i != text.size()) return false;
    value = whole + fraction / scale;
    if (negative) value = -value;
    return true;
}

inline bool splitCssArguments(std::string_view text, std::array<std::string_view, 4>& args,
                              size_t& count) noexcept {
    count = 0;
    size_t begin = 0;
    size_t depth = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const char c = i == text.size() ? ',' : text[i];
        if (c == '(') ++depth;
        else if (c == ')') { if (!depth) return false; --depth; }
        else if (c == ',' && depth == 0) {
            if (count == args.size()) return false;
            size_t first = begin, last = i;
            while (first < last && isSpace(text[first])) ++first;
            while (last > first && isSpace(text[last - 1])) --last;
            if (first == last) return false;
            args[count++] = text.substr(first, last - first);
            begin = i + 1;
        }
    }
    return depth == 0;
}

inline CssColor shadeCssColor(CssColor color, double factor) noexcept {
    const double red = color.red / 255.0, green = color.green / 255.0, blue = color.blue / 255.0;
    const double maximum = std::max(red, std::max(green, blue));
    const double minimum = std::min(red, std::min(green, blue));
    const double lightness = (maximum + minimum) * 0.5;
    const double delta = maximum - minimum;
    double hue = 0.0, saturation = 0.0;
    if (delta != 0.0) {
        saturation = delta / (1.0 - (lightness < 0.5 ? 2.0 * lightness : 2.0 - 2.0 * lightness));
        if (maximum == red) hue = (green - blue) / delta + (green < blue ? 6.0 : 0.0);
        else if (maximum == green) hue = (blue - red) / delta + 2.0;
        else hue = (red - green) / delta + 4.0;
        hue *= 60.0;
    }
    factor = std::clamp(factor, 0.0, 2.0);
    const double shadedLightness = factor <= 1.0 ? lightness * factor :
                                   1.0 - (1.0 - lightness) * (2.0 - factor);
    const double chroma = (1.0 - (shadedLightness < 0.5 ? 2.0 * shadedLightness :
                                  2.0 - 2.0 * shadedLightness)) * saturation;
    const double sector = hue / 60.0;
    const int sectorIndex = static_cast<int>(sector);
    const double x = chroma * (1.0 - ((sectorIndex & 1) ? sector - sectorIndex : 1.0 - (sector - sectorIndex)));
    double r{}, g{}, b{};
    switch (sectorIndex % 6) {
        case 0: r = chroma; g = x; break;
        case 1: r = x; g = chroma; break;
        case 2: g = chroma; b = x; break;
        case 3: g = x; b = chroma; break;
        case 4: r = x; b = chroma; break;
        default: r = chroma; b = x; break;
    }
    const double match = shadedLightness - chroma * 0.5;
    auto channel = [match](double value) noexcept {
        return static_cast<uint8_t>(std::clamp(value + match, 0.0, 1.0) * 255.0 + 0.5);
    };
    return {channel(r), channel(g), channel(b), color.alpha};
}

inline bool resolveCssColor(std::string_view expression,
                            const CompiledCss& source,
                            CssColor currentColor,
                            CssColor& output,
                            unsigned depth = 0,
                            CssAccentOverride accent = {}) noexcept {
    if (depth > 8) return false;
    while (!expression.empty() && isSpace(expression.front())) expression.remove_prefix(1);
    while (!expression.empty() && isSpace(expression.back())) expression.remove_suffix(1);
    if (parseHexColor(expression, output)) {
        if (accent.enabled && output.red == accent.source.red &&
            output.green == accent.source.green && output.blue == accent.source.blue) {
            const uint8_t originalAlpha = output.alpha;
            output = accent.replacement;
            output.alpha = originalAlpha;
        }
        return true;
    }
    if (equalAscii(expression, "currentColor")) { output = currentColor; return true; }
    if (equalAscii(expression, "transparent")) { output = {0, 0, 0, 0}; return true; }
    if (equalAscii(expression, "black")) { output = {0, 0, 0, 255}; return true; }
    if (equalAscii(expression, "white")) { output = {255, 255, 255, 255}; return true; }
    if (!expression.empty() && expression.front() == '@') {
        const auto name = expression.substr(1);
        if (equalAscii(name, "selected_bg_color") || equalAscii(name, "theme_selected_bg_color") ||
            equalAscii(name, "primary_color") || equalAscii(name, "accent_bg_color") ||
            equalAscii(name, "accent_color")) {
            if (accent.enabled) { output = accent.replacement; return true; }
        }
        for (const auto& definition : source.colorDefinitions) {
            if (!equalAscii(name, definition.first)) continue;
            return resolveCssColor(definition.second, source, currentColor, output, depth + 1, accent);
        }
        return false;
    }
    const size_t open = expression.find('(');
    if (open == std::string_view::npos || expression.back() != ')') return false;
    const auto function = expression.substr(0, open);
    const auto body = expression.substr(open + 1, expression.size() - open - 2);
    std::array<std::string_view, 4> args{};
    size_t count{};
    if (!splitCssArguments(body, args, count)) return false;

    if (equalAscii(function, "rgb") || equalAscii(function, "rgba")) {
        if ((equalAscii(function, "rgb") && count != 3) ||
            (equalAscii(function, "rgba") && count != 4)) return false;
        uint8_t channels[4]{0, 0, 0, 255};
        for (size_t channel = 0; channel < count; ++channel) {
            double number{}; bool percent{};
            if (!parseCssNumber(args[channel], number, percent)) return false;
            const double maximum = channel == 3 ? 1.0 : (percent ? 100.0 : 255.0);
            number = std::clamp(number, 0.0, maximum);
            if (channel == 3) channels[channel] = static_cast<uint8_t>(number * 255.0 + 0.5);
            else if (percent) channels[channel] = static_cast<uint8_t>(number * 2.55 + 0.5);
            else channels[channel] = static_cast<uint8_t>(number + 0.5);
        }
        output = {channels[0], channels[1], channels[2], channels[3]};
        return true;
    }

    CssColor first{};
    if (equalAscii(function, "alpha")) {
        if (count != 2 || !resolveCssColor(args[0], source, currentColor, first, depth + 1, accent)) return false;
        double factor{}; bool percent{};
        if (!parseCssNumber(args[1], factor, percent) || percent) return false;
        factor = std::clamp(factor, 0.0, 1.0);
        first.alpha = static_cast<uint8_t>(first.alpha * factor + 0.5);
        output = first;
        return true;
    }
    if (equalAscii(function, "mix")) {
        CssColor second{};
        if (count != 3 || !resolveCssColor(args[0], source, currentColor, first, depth + 1, accent) ||
            !resolveCssColor(args[1], source, currentColor, second, depth + 1, accent)) return false;
        double factor{}; bool percent{};
        if (!parseCssNumber(args[2], factor, percent) || percent || factor < 0.0 || factor > 1.0) return false;
        auto blend = [factor](uint8_t a, uint8_t b) noexcept {
            return static_cast<uint8_t>(a * (1.0 - factor) + b * factor + 0.5);
        };
        output = {blend(first.red, second.red), blend(first.green, second.green),
                  blend(first.blue, second.blue), blend(first.alpha, second.alpha)};
        return true;
    }
    if (equalAscii(function, "shade") || equalAscii(function, "lighter") ||
        equalAscii(function, "darker")) {
        const bool simple = !equalAscii(function, "shade");
        if ((simple && count != 1) || (!simple && count != 2) ||
            !resolveCssColor(args[0], source, currentColor, first, depth + 1, accent)) return false;
        double factor = equalAscii(function, "lighter") ? 1.2 : 0.8;
        if (!simple) {
            bool percent{};
            if (!parseCssNumber(args[1], factor, percent) || percent || factor < 0.0 || factor > 2.0)
                return false;
        }
        output = shadeCssColor(first, factor);
        return true;
    }
    return false;
}

enum class NativeControl : uint8_t {
    Button, Edit, CheckBox, Radio, ComboBox, Menu, Tab, Progress, ScrollBar,
    Slider, ToolBar, StatusBar, ToolTip, HeaderBar, Window, Count,
};

enum class NativeVisualState : uint8_t {
    Normal, Hover, Pressed, Focused, Disabled, Checked, Selected, Inactive, Count,
};

struct NativeControlStyle {
    CssColor foreground{};
    CssColor background{};
    CssColor border{};
    uint16_t borderRadius{};
    uint16_t borderWidth{};
    uint16_t minWidth{};
    uint16_t minHeight{};
    uint16_t padding[4]{}; // top, right, bottom, left
    uint16_t fontSize{};
    uint8_t opacity{255};
    bool hasForeground{}, hasBackground{}, hasBorder{};
    bool hasBorderRadius{}, hasBorderWidth{}, hasMinWidth{}, hasMinHeight{};
    bool hasPadding{}, hasFontSize{}, hasOpacity{};
};

struct NativeTheme {
    static constexpr size_t kEntryCount = static_cast<size_t>(NativeControl::Count) *
                                          static_cast<size_t>(NativeVisualState::Count);
    std::array<NativeControlStyle, kEntryCount> controls{};
    uint32_t unsupportedSimpleRules{};
    uint32_t unsupportedProperties{};
    CssAccentOverride accent{};

    NativeControlStyle& style(NativeControl control, NativeVisualState state) noexcept {
        return controls[static_cast<size_t>(control) * static_cast<size_t>(NativeVisualState::Count) +
                        static_cast<size_t>(state)];
    }
    const NativeControlStyle& style(NativeControl control, NativeVisualState state) const noexcept {
        return controls[static_cast<size_t>(control) * static_cast<size_t>(NativeVisualState::Count) +
                        static_cast<size_t>(state)];
    }
};

inline bool parseCssLength(std::string_view text, uint16_t& output) noexcept {
    if (text.size() >= 2 && equalAscii(text.substr(text.size() - 2), "px")) text.remove_suffix(2);
    double number{}; bool percent{};
    if (!parseCssNumber(text, number, percent) || percent || number < 0.0 || number > 65535.0) return false;
    output = static_cast<uint16_t>(number + 0.5);
    return true;
}

inline bool parseCssBorderWidth(std::string_view text, uint16_t& output) noexcept {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    if (equalAscii(text, "none")) { output = 0; return true; }
    const size_t end = text.find_first_of(" \t\r\n");
    if (end != std::string_view::npos) text = text.substr(0, end);
    return parseCssLength(text, output);
}

inline bool parseCssBox(std::string_view text, uint16_t (&sides)[4]) noexcept {
    std::array<std::string_view, 4> values{};
    size_t count = 0, begin = 0;
    while (begin < text.size()) {
        while (begin < text.size() && isSpace(text[begin])) ++begin;
        if (begin == text.size()) break;
        size_t end = begin;
        while (end < text.size() && !isSpace(text[end])) ++end;
        if (count == values.size()) return false;
        values[count++] = text.substr(begin, end - begin);
        begin = end;
    }
    if (!count) return false;
    uint16_t parsed[4]{};
    for (size_t i = 0; i < count; ++i) if (!parseCssLength(values[i], parsed[i])) return false;
    if (count == 1) sides[0] = sides[1] = sides[2] = sides[3] = parsed[0];
    else if (count == 2) { sides[0] = sides[2] = parsed[0]; sides[1] = sides[3] = parsed[1]; }
    else if (count == 3) { sides[0] = parsed[0]; sides[1] = sides[3] = parsed[1]; sides[2] = parsed[2]; }
    else for (size_t i = 0; i < 4; ++i) sides[i] = parsed[i];
    return true;
}

inline bool classifySimpleSelector(const CssSelector& selector, NativeControl& control,
                                   NativeVisualState& state) noexcept {
    const std::string_view raw(selector.raw.data(), selector.rawLength);
    if (raw.empty()) return false;
    for (char c : raw) if (isSpace(c) || c == '.' || c == '#' || c == '>' || c == '+' ||
                            c == '~' || c == '[' || c == ']' || c == '(' || c == '*') return false;
    size_t split = raw.find(':');
    const auto node = raw.substr(0, split);
    if (equalAscii(node, "button")) control = NativeControl::Button;
    else if (equalAscii(node, "entry")) control = NativeControl::Edit;
    else if (equalAscii(node, "checkbutton")) control = NativeControl::CheckBox;
    else if (equalAscii(node, "radiobutton")) control = NativeControl::Radio;
    else if (equalAscii(node, "combobox")) control = NativeControl::ComboBox;
    else if (equalAscii(node, "menu") || equalAscii(node, "menuitem")) control = NativeControl::Menu;
    else if (equalAscii(node, "tab")) control = NativeControl::Tab;
    else if (equalAscii(node, "progressbar")) control = NativeControl::Progress;
    else if (equalAscii(node, "scrollbar")) control = NativeControl::ScrollBar;
    else if (equalAscii(node, "scale")) control = NativeControl::Slider;
    else if (equalAscii(node, "toolbar")) control = NativeControl::ToolBar;
    else if (equalAscii(node, "statusbar")) control = NativeControl::StatusBar;
    else if (equalAscii(node, "tooltip")) control = NativeControl::ToolTip;
    else if (equalAscii(node, "headerbar")) control = NativeControl::HeaderBar;
    else if (equalAscii(node, "window")) control = NativeControl::Window;
    else return false;
    state = NativeVisualState::Normal;
    if (split == std::string_view::npos) return true;
    const auto pseudo = raw.substr(split + 1);
    if (equalAscii(pseudo, "hover")) state = NativeVisualState::Hover;
    else if (equalAscii(pseudo, "active")) state = NativeVisualState::Pressed;
    else if (equalAscii(pseudo, "checked")) state = NativeVisualState::Checked;
    else if (equalAscii(pseudo, "selected")) state = NativeVisualState::Selected;
    else if (equalAscii(pseudo, "disabled")) state = NativeVisualState::Disabled;
    else if (equalAscii(pseudo, "focus")) state = NativeVisualState::Focused;
    else if (equalAscii(pseudo, "backdrop")) state = NativeVisualState::Inactive;
    else return false;
    return true;
}

enum class NativeProperty : uint8_t {
    Foreground, Background, BorderColor, BorderWidth, Radius, MinWidth, MinHeight,
    Padding, FontSize, Opacity, Count,
};

struct CascadeRank {
    std::array<uint16_t, static_cast<size_t>(NativeProperty::Count)> specificity{};
    std::array<uint16_t, static_cast<size_t>(NativeProperty::Count)> order{};
};

inline bool buildNativeTheme(const CompiledCss& css, NativeTheme& output,
                             CssColor currentColor = {36, 36, 36, 255},
                             CssAccentOverride accent = {}) {
    NativeTheme candidate{};
    candidate.accent = accent;
    std::array<CascadeRank, NativeTheme::kEntryCount> ranks{};
    for (const CssRule& rule : css.rules) {
        NativeControl control{};
        NativeVisualState state{};
        if (!classifySimpleSelector(rule.selector, control, state)) {
            ++candidate.unsupportedSimpleRules;
            continue;
        }
        const size_t entry = static_cast<size_t>(control) * static_cast<size_t>(NativeVisualState::Count) +
                             static_cast<size_t>(state);
        auto& style = candidate.controls[entry];
        auto& rank = ranks[entry];
        for (const CssDeclaration& declaration : rule.declarations) {
            const std::string_view property(declaration.property.data());
            const std::string_view value(declaration.value.data());
            NativeProperty kind{};
            bool recognized = true;
            if (equalAscii(property, "color")) kind = NativeProperty::Foreground;
            else if (equalAscii(property, "background-color")) kind = NativeProperty::Background;
            else if (equalAscii(property, "border-color")) kind = NativeProperty::BorderColor;
            else if (equalAscii(property, "border-width") || equalAscii(property, "border")) kind = NativeProperty::BorderWidth;
            else if (equalAscii(property, "border-radius")) kind = NativeProperty::Radius;
            else if (equalAscii(property, "min-width")) kind = NativeProperty::MinWidth;
            else if (equalAscii(property, "min-height")) kind = NativeProperty::MinHeight;
            else if (equalAscii(property, "padding")) kind = NativeProperty::Padding;
            else if (equalAscii(property, "font-size")) kind = NativeProperty::FontSize;
            else if (equalAscii(property, "opacity")) kind = NativeProperty::Opacity;
            else recognized = false;
            if (!recognized) { ++candidate.unsupportedProperties; continue; }
            const size_t propertyIndex = static_cast<size_t>(kind);
            if (rule.selector.specificity < rank.specificity[propertyIndex] ||
                (rule.selector.specificity == rank.specificity[propertyIndex] &&
                 rule.sourceOrder < rank.order[propertyIndex])) continue;

            bool valid = false;
            CssColor color{};
            uint16_t length{};
            switch (kind) {
                case NativeProperty::Foreground:
                    valid = resolveCssColor(value, css, style.hasForeground ? style.foreground : currentColor, color, 0, accent);
                    if (valid) { style.foreground = color; style.hasForeground = true; }
                    break;
                case NativeProperty::Background:
                    valid = resolveCssColor(value, css, style.hasForeground ? style.foreground : currentColor, color, 0, accent);
                    if (valid) { style.background = color; style.hasBackground = true; }
                    break;
                case NativeProperty::BorderColor:
                    valid = resolveCssColor(value, css, style.hasForeground ? style.foreground : currentColor, color, 0, accent);
                    if (valid) { style.border = color; style.hasBorder = true; }
                    break;
                case NativeProperty::BorderWidth:
                    valid = equalAscii(property, "border")
                        ? parseCssBorderWidth(value, length) : parseCssLength(value, length);
                    if (valid) { style.borderWidth = length; style.hasBorderWidth = true; }
                    break;
                case NativeProperty::Radius:
                    valid = parseCssLength(value, length);
                    if (valid) { style.borderRadius = length; style.hasBorderRadius = true; }
                    break;
                case NativeProperty::MinWidth:
                    valid = parseCssLength(value, length);
                    if (valid) { style.minWidth = length; style.hasMinWidth = true; }
                    break;
                case NativeProperty::MinHeight:
                    valid = parseCssLength(value, length);
                    if (valid) { style.minHeight = length; style.hasMinHeight = true; }
                    break;
                case NativeProperty::Padding:
                    valid = parseCssBox(value, style.padding);
                    if (valid) style.hasPadding = true;
                    break;
                case NativeProperty::FontSize:
                    valid = parseCssLength(value, length);
                    if (valid) { style.fontSize = length; style.hasFontSize = true; }
                    break;
                case NativeProperty::Opacity: {
                    double number{}; bool percent{};
                    valid = parseCssNumber(value, number, percent) && !percent;
                    if (valid) { style.opacity = static_cast<uint8_t>(std::clamp(number, 0.0, 1.0) * 255.0 + 0.5); style.hasOpacity = true; }
                    break;
                }
                case NativeProperty::Count: break;
            }
            if (!valid) { ++candidate.unsupportedProperties; continue; }
            rank.specificity[propertyIndex] = rule.selector.specificity;
            rank.order[propertyIndex] = rule.sourceOrder;
        }
    }

    // State-specific declarations inherit each property that was not supplied
    // by that state from the normal control style.
    for (size_t control = 0; control < static_cast<size_t>(NativeControl::Count); ++control) {
        const size_t base = control * static_cast<size_t>(NativeVisualState::Count);
        const NativeControlStyle normal = candidate.controls[base];
        for (size_t state = 1; state < static_cast<size_t>(NativeVisualState::Count); ++state) {
            auto& style = candidate.controls[base + state];
            if (!style.hasForeground && normal.hasForeground) { style.foreground = normal.foreground; style.hasForeground = true; }
            if (!style.hasBackground && normal.hasBackground) { style.background = normal.background; style.hasBackground = true; }
            if (!style.hasBorder && normal.hasBorder) { style.border = normal.border; style.hasBorder = true; }
            if (!style.hasBorderRadius && normal.hasBorderRadius) { style.borderRadius = normal.borderRadius; style.hasBorderRadius = true; }
            if (!style.hasBorderWidth && normal.hasBorderWidth) { style.borderWidth = normal.borderWidth; style.hasBorderWidth = true; }
            if (!style.hasMinWidth && normal.hasMinWidth) { style.minWidth = normal.minWidth; style.hasMinWidth = true; }
            if (!style.hasMinHeight && normal.hasMinHeight) { style.minHeight = normal.minHeight; style.hasMinHeight = true; }
            if (!style.hasPadding && normal.hasPadding) { std::copy(std::begin(normal.padding), std::end(normal.padding), std::begin(style.padding)); style.hasPadding = true; }
            if (!style.hasFontSize && normal.hasFontSize) { style.fontSize = normal.fontSize; style.hasFontSize = true; }
            if (!style.hasOpacity && normal.hasOpacity) { style.opacity = normal.opacity; style.hasOpacity = true; }
        }
    }

    auto channelLuminance = [](uint8_t channel) noexcept {
        const double value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 :
               ((value + 0.055) / 1.055) * ((value + 0.055) / 1.055) * ((value + 0.055) / 1.055);
    };
    auto luminance = [&](CssColor color) noexcept {
        return 0.2126 * channelLuminance(color.red) + 0.7152 * channelLuminance(color.green) +
               0.0722 * channelLuminance(color.blue);
    };
    auto contrast = [&](CssColor left, CssColor right) noexcept {
        const double a = luminance(left), b = luminance(right);
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    };
    for (size_t control = 0; control < static_cast<size_t>(NativeControl::Count); ++control) {
        for (NativeVisualState state : {NativeVisualState::Pressed, NativeVisualState::Checked,
                                        NativeVisualState::Selected}) {
            auto& style = candidate.controls[control * static_cast<size_t>(NativeVisualState::Count) +
                                             static_cast<size_t>(state)];
            if (!style.hasForeground || !style.hasBackground || contrast(style.foreground, style.background) >= 4.5)
                continue;
            const CssColor black{0, 0, 0, 255}, white{255, 255, 255, 255};
            style.foreground = contrast(black, style.background) >= contrast(white, style.background) ? black : white;
        }
    }
    output = candidate;
    return true;
}

} // namespace micant::um::theme
