#pragma once

#include "micant/um/subsystems/theme/gtk_css.hpp"

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
