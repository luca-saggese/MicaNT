#pragma once

#include "micant/um/subsystems/theme/native_theme.hpp"
#include "micant/shared/theme_preferences.h"

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

inline void Test_ThemePreferences_VersionedRecordValidation() {
    MicaThemePreferences defaults{};
    MicaThemePreferencesInitializeDefaults(&defaults);
    TEST_ASSERT(MicaThemePreferencesIsValid(&defaults) &&
                defaults.themeId == MICANT_THEME_PREFERENCE_THEME_WHITESUR &&
                defaults.mode == MICANT_THEME_PREFERENCE_MODE_LIGHT &&
                defaults.accentId == MICANT_THEME_PREFERENCE_ACCENT_DEFAULT &&
                defaults.dayStartMinute == 360 && defaults.nightStartMinute == 1080,
                "theme preferences initialize to a valid per-user default record");

    auto selected = defaults;
    selected.mode = MICANT_THEME_PREFERENCE_MODE_AUTO;
    selected.accentId = MICANT_THEME_PREFERENCE_ACCENT_PURPLE;
    selected.effects = MICANT_THEME_PREFERENCE_EFFECT_TRANSPARENCY |
        MICANT_THEME_PREFERENCE_EFFECT_ROUNDED_CORNERS |
        MICANT_THEME_PREFERENCE_EFFECT_SHADOWS |
        MICANT_THEME_PREFERENCE_EFFECT_BLUR |
        MICANT_THEME_PREFERENCE_EFFECT_REDUCED_MOTION;
    std::array<uint8_t, sizeof(selected) + 1> unaligned{};
    std::memcpy(unaligned.data() + 1, &selected, sizeof(selected));
    MicaThemePreferences parsed{};
    TEST_ASSERT(MicaThemePreferencesParse(unaligned.data() + 1, sizeof(selected), &parsed) &&
                parsed.mode == MICANT_THEME_PREFERENCE_MODE_AUTO &&
                parsed.accentId == MICANT_THEME_PREFERENCE_ACCENT_PURPLE &&
                parsed.effects == MICANT_THEME_PREFERENCE_KNOWN_EFFECTS,
                "versioned preferences parse the supported mode, accent and effect flags");

    auto rejected = selected;
    rejected.magic ^= 1u;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject an invalid signature");
    rejected = selected;
    ++rejected.version;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject an unknown version");
    rejected = selected;
    rejected.byteSize--;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject a mismatched record size");
    rejected = selected;
    rejected.themeId = 2;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject an unknown packaged theme");
    rejected = selected;
    rejected.mode = 3;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject an unknown theme mode");
    rejected = selected;
    rejected.accentId = 2;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject an unavailable accent ID");
    rejected = selected;
    rejected.effects |= 1u << 31;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject unknown effect flags");
    rejected = selected;
    rejected.dayStartMinute = 1440;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject an out-of-day schedule value");
    rejected = selected;
    rejected.nightStartMinute = rejected.dayStartMinute;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject identical day and night boundaries");
    rejected = selected;
    rejected.reserved = 1;
    TEST_ASSERT(!MicaThemePreferencesIsValid(&rejected), "preferences reject nonzero reserved fields");
    TEST_ASSERT(!MicaThemePreferencesParse(&selected, sizeof(selected) - 1, &parsed) &&
                !MicaThemePreferencesParse(&selected, sizeof(selected) + 1, &parsed),
                "preferences reject truncated and trailing data");

    auto preserved = defaults;
    rejected = selected;
    rejected.effects |= 1u << 31;
    TEST_ASSERT(!MicaThemePreferencesParse(&rejected, sizeof(rejected), &preserved) &&
                std::memcmp(&preserved, &defaults, sizeof(defaults)) == 0,
                "failed preference parsing leaves the caller's last valid record intact");
}

inline void Test_GtkCss_BoundedRulesAndHexColors() {
    using namespace micant::um::theme;
    CompiledCss compiled{};
    CssParseError error{};
    constexpr std::string_view css =
        "/* base */ button.primary:hover { color: #abc; background-color: #102030ff; "
        "border-radius: 6px; padding: 2px 4px; }"
        "button:disabled { color: #1234; }";
    TEST_ASSERT(compileGtkCss(css, compiled, &error) == CssStatus::Success,
                "bounded GTK CSS rules compile");
    TEST_ASSERT(compiled.rules.size() == 2 && compiled.rules[0].declarations.size() == 4,
                "rules and declarations are retained in source order");
    TEST_ASSERT(compiled.rules[0].selector.specificity == 21 &&
                std::string_view(compiled.rules[0].selector.node.data()) == "button" &&
                std::string_view(compiled.rules[0].selector.klass.data()) == "primary" &&
                std::string_view(compiled.rules[0].selector.state.data()) == "hover",
                "node, class and state selector parts are parsed");
    CssColor color{};
    TEST_ASSERT(parseHexColor("#abc", color) && color == (CssColor{170, 187, 204, 255}),
                "short hexadecimal RGB color expands correctly");
    TEST_ASSERT(parseHexColor("#1234", color) && color == (CssColor{17, 34, 51, 68}),
                "short hexadecimal RGBA color expands correctly");
    TEST_ASSERT(parseHexColor("#102030ff", color) && color == (CssColor{16, 32, 48, 255}),
                "long hexadecimal RGBA color parses correctly");
    TEST_ASSERT(!parseHexColor("#12xz00", color), "invalid hexadecimal color is rejected");
}

inline void Test_GtkCss_GroupedSelectorsAndColorDefinitions() {
    using namespace micant::um::theme;
    CompiledCss compiled{};
    constexpr std::string_view css =
        "@define-color accent mix(#fff, #0860f2, 0.8);"
        "@keyframes ignored { from { opacity: 0; } to { opacity: 1; } }"
        "button.primary:hover, entry:focus, label { color: @accent; box-shadow: none; }";
    TEST_ASSERT(compileGtkCss(css, compiled) == CssStatus::Success,
                "bounded color definition and keyframe syntax are accepted");
    TEST_ASSERT(compiled.colorDefinitions.size() == 1 &&
                compiled.colorDefinitions[0].first == "accent" &&
                compiled.colorDefinitions[0].second == "mix(#fff, #0860f2, 0.8)",
                "color definitions preserve expressions for the color compiler");
    TEST_ASSERT(compiled.omittedKeyframeBlocks == 1,
                "unsupported animation is explicitly counted for fallback reporting");
    TEST_ASSERT(compiled.rules.size() == 3 && compiled.compiledDeclarationCount == 6,
                "comma-separated selectors expand while sharing declarations semantically");
    TEST_ASSERT(std::string_view(compiled.rules[1].selector.raw.data()) == "entry:focus" &&
                std::string_view(compiled.rules[2].selector.raw.data()) == "label",
                "each grouped selector is retained in source order");
}

inline void Test_GtkCss_FailurePreservesOutputAndReportsUnsupportedSyntax() {
    using namespace micant::um::theme;
    CompiledCss compiled{};
    TEST_ASSERT(compileGtkCss("button { color: #fff; }", compiled) == CssStatus::Success,
                "valid source initializes output");
    const size_t oldCount = compiled.rules.size();
    CssParseError error{};
    TEST_ASSERT(compileGtkCss("@import url(https://example.invalid/theme.css);", compiled, &error) ==
                    CssStatus::Unsupported,
                "at-rules and remote imports fail explicitly");
    TEST_ASSERT(compiled.rules.size() == oldCount,
                "failed compilation does not replace the caller's compiled theme");
    TEST_ASSERT(compileGtkCss("button:imaginary { color: #fff; }", compiled) == CssStatus::Unsupported,
                "unknown pseudo states fail explicitly");
    TEST_ASSERT(compileGtkCss("button { custom-property: value; }", compiled) == CssStatus::Success &&
                compiled.rules[0].declarations.size() == 1,
                "syntactically valid declarations remain available for capability resolution");
    TEST_ASSERT(compileGtkCss("button { color #fff; }", compiled, &error) == CssStatus::Malformed &&
                    error.offset > 0,
                "malformed declarations report a source offset");
    TEST_ASSERT(compileGtkCss("", compiled) == CssStatus::EmptyInput,
                "empty input is not treated as a valid theme");
}

inline void Test_NativeTheme_ColorExpressionsAndControlCascade() {
    using namespace micant::um::theme;
    constexpr std::string_view css =
        "@define-color accent #0860f2;"
        "button { color: #242424; background-color: white; border: 1px solid; "
        "border-radius: 6px; padding: 0 8px; min-height: 24px; }"
        "button:hover { background-color: #f7f7f7; }"
        "button:active { color: white; background-color: @accent; border-color: @accent; }"
        "button:disabled { color: rgba(36, 36, 36, 0.45); }";
    CompiledCss parsed{};
    NativeTheme theme{};
    TEST_ASSERT(compileGtkCss(css, parsed) == CssStatus::Success && buildNativeTheme(parsed, theme),
                "GTK declarations compile to a GTK-independent native theme table");
    const auto& normal = theme.style(NativeControl::Button, NativeVisualState::Normal);
    const auto& hover = theme.style(NativeControl::Button, NativeVisualState::Hover);
    const auto& pressed = theme.style(NativeControl::Button, NativeVisualState::Pressed);
    const auto& disabled = theme.style(NativeControl::Button, NativeVisualState::Disabled);
    TEST_ASSERT(normal.hasBackground && normal.background == (CssColor{255, 255, 255, 255}) &&
                normal.hasBorderWidth && normal.borderWidth == 1 && normal.hasBorderRadius &&
                normal.borderRadius == 6 && normal.padding[1] == 8 && normal.minHeight == 24,
                "normal button style resolves colors and dimensions");
    TEST_ASSERT(hover.background == (CssColor{247, 247, 247, 255}),
                "hover style overrides only properties declared for that state");
    TEST_ASSERT(pressed.background == (CssColor{8, 96, 242, 255}) &&
                pressed.foreground == (CssColor{255, 255, 255, 255}),
                "pressed style resolves a GTK color token");
    TEST_ASSERT(disabled.foreground == (CssColor{36, 36, 36, 115}) &&
                disabled.background == normal.background,
                "disabled alpha color resolves while unspecified state values inherit normal");

    CssColor color{};
    TEST_ASSERT(resolveCssColor("mix(white, @accent, 0.75)", parsed, {}, color) &&
                color == (CssColor{70, 136, 245, 255}),
                "nested mix expression follows GTK factor ordering");
    TEST_ASSERT(resolveCssColor("alpha(rgba(10, 20, 30, 0.5), 0.4)", parsed, {}, color) &&
                color.alpha == 51,
                "alpha expression multiplies input opacity");
    TEST_ASSERT(resolveCssColor("shade(#808080, 0.5)", parsed, {}, color) &&
                color == (CssColor{64, 64, 64, 255}),
                "GTK shade scales HSL lightness");
    TEST_ASSERT(resolveCssColor("lighter(#808080)", parsed, {}, color) &&
                color.red > 128 && color.green == color.red && color.blue == color.red,
                "GTK lighter expression raises HSL lightness");
    TEST_ASSERT(resolveCssColor("darker(#808080)", parsed, {}, color) &&
                color.red < 128 && color.green == color.red && color.blue == color.red,
                "GTK darker expression lowers HSL lightness");
    TEST_ASSERT(resolveCssColor("shade(#ff0000, 0.5)", parsed, {}, color) &&
                color == (CssColor{128, 0, 0, 255}),
                "GTK shade preserves saturated hue while reducing lightness");
    TEST_ASSERT(resolveCssColor("shade(#ff0000, 1.5)", parsed, {}, color) &&
                color == (CssColor{255, 128, 128, 255}),
                "GTK shade raises lightness without losing saturated hue");
}

inline void Test_NativeTheme_WhiteSurProgressDescendantsMapToProgressStates() {
    using namespace micant::um::theme;
    constexpr std::string_view css =
        "progressbar trough { background-color: rgba(0, 0, 0, 0.12); }"
        "progressbar progress { background-color: #0860F2; border-radius: 6px; }";
    CompiledCss parsed{};
    NativeTheme theme{};
    TEST_ASSERT(compileGtkCss(css, parsed) == CssStatus::Success &&
                buildNativeTheme(parsed, theme),
                "the bounded WhiteSur Progress selector pair compiles");
    const auto& trough = theme.style(NativeControl::Progress, NativeVisualState::Normal);
    const auto& fill = theme.style(NativeControl::Progress, NativeVisualState::Selected);
    TEST_ASSERT(trough.hasBackground &&
                trough.background == (CssColor{0, 0, 0, 31}),
                "progressbar trough background maps to the normal state");
    TEST_ASSERT(fill.hasBackground &&
                fill.background == (CssColor{8, 96, 242, 255}) &&
                fill.hasBorderRadius && fill.borderRadius == 6,
                "progressbar progress background and radius map to selected state");
}

inline void Test_NativeTheme_FontPropertiesCompileToSnapshotMetadata() {
    using namespace micant::um::theme;
    constexpr std::string_view css =
        "button { font-family: 'Arial Black', sans-serif; font-family: 'Arial', sans-serif; font-size: 9pt; font-weight: 500; font-style: italic; }"
        "button:hover { font-weight: bold; font-style: normal; }";
    CompiledCss parsed{};
    NativeTheme theme{};
    TEST_ASSERT(compileGtkCss(css, parsed) == CssStatus::Success &&
                buildNativeTheme(parsed, theme),
                "supported CSS font properties compile to native styles");
    const auto& normal = theme.style(NativeControl::Button, NativeVisualState::Normal);
    const auto& hover = theme.style(NativeControl::Button, NativeVisualState::Hover);
    TEST_ASSERT(normal.hasFontFamily && std::string_view(normal.fontFamily) == "Arial" &&
                normal.hasFontSize && normal.fontSize == 12 && normal.hasFontWeight &&
                normal.fontWeight == 500 && normal.hasFontStyle && normal.fontItalic,
                "font family, point size, weight and style parse into the native style");
    TEST_ASSERT(hover.hasFontSize && hover.fontSize == 12 && hover.hasFontWeight &&
                hover.fontWeight == 700 && hover.hasFontStyle && !hover.fontItalic,
                "font properties inherit individually across visual states");
    const MicaThemeSnapshot snapshot = makeThemeSnapshot(theme);
    const auto& packed = snapshot.styles[0];
    TEST_ASSERT((packed.flags & (MICANT_THEME_HAS_FONT_SIZE |
                                 MICANT_THEME_HAS_FONT_WEIGHT |
                                 MICANT_THEME_HAS_FONT_STYLE |
                                 MICANT_THEME_HAS_FONT_FAMILY)) ==
                    (MICANT_THEME_HAS_FONT_SIZE |
                     MICANT_THEME_HAS_FONT_WEIGHT |
                     MICANT_THEME_HAS_FONT_STYLE |
                     MICANT_THEME_HAS_FONT_FAMILY) &&
                packed.fontSize == 12 && packed.fontWeight == 500 && packed.fontItalic == 1 &&
                std::string_view(packed.fontFamily) == "Arial" &&
                MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot)),
                "font metadata crosses the versioned shared snapshot ABI");
    TEST_ASSERT(compileGtkCss("button { font-style: oblique; }", parsed) == CssStatus::Success &&
                buildNativeTheme(parsed, theme) &&
                !theme.style(NativeControl::Button, NativeVisualState::Normal).hasFontStyle &&
                theme.unsupportedProperties == 1,
                "unsupported oblique fonts are counted without fabricated metadata");
    TEST_ASSERT(compileGtkCss("button { font-size: 9; }", parsed) == CssStatus::Success &&
                buildNativeTheme(parsed, theme) &&
                !theme.style(NativeControl::Button, NativeVisualState::Normal).hasFontSize &&
                theme.unsupportedProperties == 1,
                "unitless nonzero CSS font sizes remain unsupported");
}

inline void Test_WhiteSur_RealProfilesCompileToNativeButtonStyles() {
    using namespace micant::um::theme;
    auto compileProfile = [](const char* path, NativeTheme& theme,
                             CssAccentOverride accent = {}) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        std::string cssText((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CompiledCss css{};
        return compileGtkCss(cssText, css) == CssStatus::Success &&
               buildNativeTheme(css, theme, {36, 36, 36, 255}, accent);
    };
    NativeTheme light{}, dark{}, purple{}, green{};
    TEST_ASSERT(compileProfile("System/Themes/WhiteSur/Light/gtk.css", light),
                "packaged WhiteSur Light stylesheet parses and resolves");
    TEST_ASSERT(compileProfile("System/Themes/WhiteSur/Dark/gtk.css", dark),
                "packaged WhiteSur Dark stylesheet parses and resolves");
    const CssAccentOverride purpleAccent{{8, 96, 242, 255}, {154, 87, 163, 255}, true};
    const CssAccentOverride greenAccent{{8, 96, 242, 255}, {121, 183, 87, 255}, true};
    TEST_ASSERT(compileProfile("System/Themes/WhiteSur/Light/gtk.css", purple, purpleAccent) &&
                compileProfile("System/Themes/WhiteSur/Light/gtk.css", green, greenAccent),
                "semantic accent overrides compile without rewriting the source theme");
    const auto& lightButton = light.style(NativeControl::Button, NativeVisualState::Normal);
    const auto& lightHover = light.style(NativeControl::Button, NativeVisualState::Hover);
    const auto& lightPressed = light.style(NativeControl::Button, NativeVisualState::Pressed);
    const auto& darkButton = dark.style(NativeControl::Button, NativeVisualState::Normal);
    const auto lightSnapshot = makeThemeSnapshot(light);
    TEST_ASSERT(lightSnapshot.magic == MICANT_THEME_SNAPSHOT_MAGIC &&
                lightSnapshot.version == MICANT_THEME_SNAPSHOT_VERSION &&
                lightSnapshot.totalSize == sizeof(MicaThemeSnapshot) &&
                lightSnapshot.controlCount == MICANT_THEME_CONTROL_COUNT &&
                lightSnapshot.stateCount == MICANT_THEME_STATE_COUNT,
                "native style snapshot has a versioned fixed-size neutral ABI");
    TEST_ASSERT(MicaThemeSnapshotIsValid(&lightSnapshot, sizeof(lightSnapshot)),
                "native style snapshot passes shared ABI validation");
    auto malformedSnapshot = lightSnapshot;
    malformedSnapshot.styles[0].flags |= (1u << 31);
    TEST_ASSERT(!MicaThemeSnapshotIsValid(&malformedSnapshot, sizeof(malformedSnapshot)),
                "snapshot validation rejects unknown property flags");
    const auto& packedButton = lightSnapshot.styles[
        static_cast<size_t>(NativeControl::Button) * MICANT_THEME_STATE_COUNT];
    TEST_ASSERT((packedButton.flags & (MICANT_THEME_HAS_FOREGROUND |
                                      MICANT_THEME_HAS_BACKGROUND)) ==
                    (MICANT_THEME_HAS_FOREGROUND | MICANT_THEME_HAS_BACKGROUND) &&
                packedButton.background == 0xFFFFFFFFu,
                "native snapshot preserves resolved color values and property presence");
    TEST_ASSERT(lightButton.hasBackground && lightButton.background == (CssColor{255, 255, 255, 255}) &&
                lightHover.background == (CssColor{247, 247, 247, 255}) &&
                lightPressed.background == (CssColor{8, 96, 242, 255}),
                "WhiteSur Light button states resolve to the generated upstream palette");
    TEST_ASSERT(darkButton.hasBackground && darkButton.background == (CssColor{101, 101, 101, 255}) &&
                dark.style(NativeControl::Button, NativeVisualState::Pressed).background ==
                    (CssColor{8, 96, 242, 255}),
                "WhiteSur Dark button states resolve independently from the Light palette");
    TEST_ASSERT(purple.style(NativeControl::Button, NativeVisualState::Pressed).background ==
                    (CssColor{154, 87, 163, 255}) &&
                green.style(NativeControl::Button, NativeVisualState::Pressed).background ==
                    (CssColor{121, 183, 87, 255}) &&
                green.style(NativeControl::Button, NativeVisualState::Pressed).foreground ==
                    (CssColor{0, 0, 0, 255}),
                "WhiteSur semantic accents adapt with readable foreground contrast");
}
