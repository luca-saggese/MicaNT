#pragma once

// A bounded, deliberately small GTK CSS import surface. This is a Ring 3
// compiler input format; compiled rules contain no GTK/runtime dependencies.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace micant::um::theme {

inline constexpr size_t kMaxCssBytes = 1024 * 1024;
inline constexpr size_t kMaxRules = 8192;
inline constexpr size_t kMaxSelectorsPerRule = 256;
inline constexpr size_t kMaxDeclarationsPerRule = 64;
inline constexpr size_t kMaxCompiledDeclarations = 32768;
inline constexpr size_t kMaxSelectorBytes = 256;
inline constexpr size_t kMaxDefinitions = 128;

enum class CssStatus : uint8_t {
    Success,
    EmptyInput,
    InputTooLarge,
    Malformed,
    Unsupported,
    LimitExceeded,
};

struct CssColor {
    uint8_t red{}, green{}, blue{}, alpha{255};
    friend constexpr bool operator==(CssColor, CssColor) = default;
};

struct CssSelector {
    std::array<char, 48> node{};
    std::array<char, 48> klass{};
    std::array<char, 24> state{};
    std::array<char, kMaxSelectorBytes + 1> raw{};
    uint16_t rawLength{};
    uint16_t specificity{};
};

struct CssDeclaration {
    std::array<char, 40> property{};
    std::array<char, 256> value{};
};

struct CssRule {
    CssSelector selector{};
    std::vector<CssDeclaration> declarations;
    uint16_t sourceOrder{};
};

struct CompiledCss {
    std::vector<CssRule> rules;
    size_t compiledDeclarationCount{};
    std::vector<std::pair<std::string, std::string>> colorDefinitions;
    uint32_t omittedKeyframeBlocks{};
};

struct CssParseError {
    CssStatus status{CssStatus::Success};
    size_t offset{};
};

constexpr bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

constexpr char asciiLower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline bool equalAscii(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (asciiLower(left[i]) != asciiLower(right[i])) return false;
    return true;
}

class CssParser {
public:
    explicit CssParser(std::string_view input) noexcept : input_(input) {}

    CssStatus parse(CompiledCss& output, CssParseError* error = nullptr) {
        if (input_.empty()) return fail(CssStatus::EmptyInput, error);
        if (input_.size() > kMaxCssBytes) return fail(CssStatus::InputTooLarge, error);

        CompiledCss candidate{};
        while (true) {
            skipTrivia();
            if (atEnd()) break;
            if (input_[position_] == '@') {
                const auto atStatus = parseAtRule(candidate);
                if (atStatus != CssStatus::Success) return fail(atStatus, error);
                continue;
            }

            // Keep the selector scratch space off the thread stack. The full
            // selector record is sizeable and this parser runs on a small
            // user thread during compositor startup.
            std::vector<CssSelector> selectors;
            size_t selectorCount{};
            const auto selectorStatus = parseSelectorGroup(selectors, selectorCount);
            if (selectorStatus != CssStatus::Success)
                return fail(selectorStatus, error);
            skipSpace();
            if (!consume('{')) return fail(CssStatus::Malformed, error);

            CssRule rule{};
            skipSpace();
            while (!atEnd() && input_[position_] != '}') {
                if (rule.declarations.size() == kMaxDeclarationsPerRule)
                    return fail(CssStatus::LimitExceeded, error);
                CssDeclaration declaration{};
                if (!parseIdentifier(declaration.property.data(), declaration.property.size()))
                    return fail(CssStatus::Malformed, error);
                skipSpace();
                if (!consume(':')) return fail(CssStatus::Malformed, error);
                skipSpace();
                if (!parseValue(declaration.value.data(), declaration.value.size()))
                    return fail(CssStatus::Malformed, error);
                rule.declarations.push_back(declaration);
                skipSpace();
                if (atEnd()) return fail(CssStatus::Malformed, error);
                if (input_[position_] == ';') {
                    ++position_;
                    skipSpace();
                } else if (input_[position_] != '}') {
                    return fail(CssStatus::Malformed, error);
                }
            }
            if (!consume('}')) return fail(CssStatus::Malformed, error);
            for (size_t i = 0; i < selectorCount; ++i) {
                if (candidate.rules.size() == kMaxRules)
                    return fail(CssStatus::LimitExceeded, error);
                CssRule expanded = rule;
                if (expanded.declarations.size() > kMaxCompiledDeclarations - candidate.compiledDeclarationCount)
                    return fail(CssStatus::LimitExceeded, error);
                candidate.compiledDeclarationCount += expanded.declarations.size();
                expanded.selector = selectors[i];
                expanded.sourceOrder = static_cast<uint16_t>(candidate.rules.size());
                candidate.rules.push_back(expanded);
            }
        }
        output = std::move(candidate);
        if (error) *error = {};
        return CssStatus::Success;
    }

private:
    std::string_view input_;
    size_t position_{};

    bool atEnd() const noexcept { return position_ >= input_.size(); }
    bool consume(char c) noexcept {
        if (atEnd() || input_[position_] != c) return false;
        ++position_;
        return true;
    }
    void skipSpace() noexcept {
        while (!atEnd() && isSpace(input_[position_])) ++position_;
    }
    void skipTrivia() noexcept {
        for (;;) {
            skipSpace();
            if (position_ + 1 < input_.size() && input_[position_] == '/' && input_[position_ + 1] == '*') {
                position_ += 2;
                while (position_ + 1 < input_.size() &&
                       !(input_[position_] == '*' && input_[position_ + 1] == '/')) ++position_;
                if (position_ + 1 >= input_.size()) return;
                position_ += 2;
                continue;
            }
            return;
        }
    }
    bool parseIdentifier(char* destination, size_t capacity) noexcept {
        const size_t begin = position_;
        while (!atEnd()) {
            const char c = input_[position_];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_')) break;
            ++position_;
        }
        const size_t count = position_ - begin;
        if (!count || count >= capacity) return false;
        for (size_t i = 0; i < count; ++i) destination[i] = asciiLower(input_[begin + i]);
        destination[count] = '\0';
        return true;
    }
    CssStatus parseSelectorGroup(std::vector<CssSelector>& selectors,
                                 size_t& count) noexcept {
        count = 0;
        size_t segmentStart = position_;
        size_t parenDepth = 0;
        size_t bracketDepth = 0;
        for (; position_ < input_.size(); ++position_) {
            const char c = input_[position_];
            if (c == '(') ++parenDepth;
            else if (c == ')') { if (!parenDepth) return CssStatus::Malformed; --parenDepth; }
            else if (c == '[') ++bracketDepth;
            else if (c == ']') { if (!bracketDepth) return CssStatus::Malformed; --bracketDepth; }
            else if ((c == ',' || c == '{') && !parenDepth && !bracketDepth) {
                size_t first = segmentStart, last = position_;
                while (first < last && isSpace(input_[first])) ++first;
                while (last > first && isSpace(input_[last - 1])) --last;
                if (first == last) return CssStatus::Malformed;
                if (count == kMaxSelectorsPerRule) return CssStatus::LimitExceeded;
                selectors.emplace_back();
                const auto status = parseSelectorText(input_.substr(first, last - first), selectors.back());
                if (status != CssStatus::Success) return status;
                ++count;
                if (c == '{') return CssStatus::Success;
                segmentStart = position_ + 1;
            }
        }
        return CssStatus::Malformed;
    }

    CssStatus parseSelectorText(std::string_view text, CssSelector& selector) noexcept {
        if (text.empty() || text.size() > kMaxSelectorBytes) return CssStatus::LimitExceeded;
        for (char c : text) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || isSpace(c) || c == '-' || c == '_' ||
                  c == '.' || c == '#' || c == ':' || c == '>' || c == '+' ||
                  c == '~' || c == '*' || c == '[' || c == ']' || c == '(' ||
                  c == ')' || c == '=' || c == '\'' || c == '"'))
                return CssStatus::Unsupported;
            if (c == '.' || c == ':') selector.specificity = static_cast<uint16_t>(selector.specificity + 10);
            else if (c == '#') selector.specificity = static_cast<uint16_t>(selector.specificity + 100);
        }
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != ':') continue;
            size_t start = i + 1;
            if (start < text.size() && text[start] == ':') ++start;
            size_t end = start;
            while (end < text.size() && ((text[end] >= 'a' && text[end] <= 'z') ||
                   (text[end] >= 'A' && text[end] <= 'Z') || text[end] == '-')) ++end;
            if (end == start) return CssStatus::Malformed;
            const auto pseudo = text.substr(start, end - start);
            constexpr std::array<std::string_view, 18> supportedPseudos{
                "hover", "active", "checked", "selected", "disabled", "focus", "backdrop",
                "first-child", "last-child", "only-child", "not", "dir", "drop", "link",
                "visited", "indeterminate", "first", "last",};
            bool supported = false;
            for (auto item : supportedPseudos) if (equalAscii(pseudo, item)) { supported = true; break; }
            if (!supported) return CssStatus::Unsupported;
            i = end - 1;
        }
        for (size_t i = 0; i < text.size(); ++i) selector.raw[i] = text[i];
        selector.raw[text.size()] = '\0';
        selector.rawLength = static_cast<uint16_t>(text.size());

        // Keep simple-selector fields for early consumers; the raw selector is
        // authoritative for the full GTK selector resolver.
        size_t start = 0;
        while (start < text.size() && (isSpace(text[start]) || text[start] == '>' ||
               text[start] == '+' || text[start] == '~')) ++start;
        size_t end = start;
        while (end < text.size() && ((text[end] >= 'a' && text[end] <= 'z') ||
               (text[end] >= 'A' && text[end] <= 'Z') || text[end] == '-' || text[end] == '_')) ++end;
        if (end > start && end - start < selector.node.size()) {
            for (size_t i = 0; i < end - start; ++i) selector.node[i] = asciiLower(text[start + i]);
            selector.node[end - start] = '\0';
            selector.specificity = static_cast<uint16_t>(selector.specificity + 1);
        }
        auto copyAfter = [&](char marker, char* destination, size_t capacity) noexcept {
            for (size_t i = 0; i + 1 < text.size(); ++i) {
                if (text[i] != marker) continue;
                size_t finish = i + 1;
                while (finish < text.size() && ((text[finish] >= 'a' && text[finish] <= 'z') ||
                       (text[finish] >= 'A' && text[finish] <= 'Z') ||
                       (text[finish] >= '0' && text[finish] <= '9') || text[finish] == '-' || text[finish] == '_')) ++finish;
                const size_t count = finish - i - 1;
                if (!count || count >= capacity) continue;
                for (size_t j = 0; j < count; ++j) destination[j] = asciiLower(text[i + 1 + j]);
                destination[count] = '\0';
                return;
            }
        };
        copyAfter('.', selector.klass.data(), selector.klass.size());
        copyAfter(':', selector.state.data(), selector.state.size());
        return CssStatus::Success;
    }

    CssStatus parseAtRule(CompiledCss& output) {
        ++position_;
        std::array<char, 48> name{};
        if (!parseIdentifier(name.data(), name.size())) return CssStatus::Malformed;
        if (equalAscii(name.data(), "define-color")) {
            if (output.colorDefinitions.size() == kMaxDefinitions) return CssStatus::LimitExceeded;
            skipSpace();
            std::array<char, 48> variable{};
            if (!parseIdentifier(variable.data(), variable.size())) return CssStatus::Malformed;
            skipSpace();
            std::array<char, 256> expression{};
            if (!parseValue(expression.data(), expression.size()) || !consume(';')) return CssStatus::Malformed;
            output.colorDefinitions.emplace_back(variable.data(), expression.data());
            return CssStatus::Success;
        }
        if (equalAscii(name.data(), "keyframes")) {
            while (!atEnd() && input_[position_] != '{') ++position_;
            if (!consume('{')) return CssStatus::Malformed;
            size_t depth = 1;
            while (!atEnd() && depth) {
                if (input_[position_] == '{') ++depth;
                else if (input_[position_] == '}') --depth;
                ++position_;
            }
            if (depth) return CssStatus::Malformed;
            ++output.omittedKeyframeBlocks;
            return CssStatus::Success;
        }
        return CssStatus::Unsupported;
    }
    static bool supportedState(const char* value) noexcept {
        constexpr std::array<std::string_view, 7> states{
            "hover", "active", "checked", "selected", "disabled", "focus", "backdrop"};
        for (auto state : states) if (equalAscii(value, state)) return true;
        return false;
    }
    bool parseValue(char* destination, size_t capacity) noexcept {
        const size_t begin = position_;
        bool quoted = false;
        char quote = 0;
        while (!atEnd()) {
            const char c = input_[position_];
            if (quoted) {
                if (c == '\\' && position_ + 1 < input_.size()) { position_ += 2; continue; }
                if (c == quote) quoted = false;
            } else if (c == '\'' || c == '"') {
                quoted = true; quote = c;
            } else if (c == ';' || c == '}') {
                break;
            }
            ++position_;
        }
        if (quoted) return false;
        size_t first = begin, last = position_;
        while (first < last && isSpace(input_[first])) ++first;
        while (last > first && isSpace(input_[last - 1])) --last;
        const size_t count = last - first;
        if (!count || count >= capacity) return false;
        for (size_t i = 0; i < count; ++i) destination[i] = input_[first + i];
        destination[count] = '\0';
        return true;
    }
    CssStatus fail(CssStatus status, CssParseError* error) const noexcept {
        if (error) *error = {status, position_};
        return status;
    }
};

inline CssStatus compileGtkCss(std::string_view css, CompiledCss& output,
                               CssParseError* error = nullptr) {
    return CssParser(css).parse(output, error);
}

inline bool parseHexColor(std::string_view value, CssColor& out) noexcept {
    if (value.empty() || value[0] != '#') return false;
    auto nibble = [](char c) noexcept -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = asciiLower(c);
        return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    if (value.size() == 4 || value.size() == 5) {
        int n[4]{0, 0, 0, 15};
        for (size_t i = 1; i < value.size(); ++i) {
            n[i - 1] = nibble(value[i]);
            if (n[i - 1] < 0) return false;
        }
        out = {uint8_t(n[0] * 17), uint8_t(n[1] * 17), uint8_t(n[2] * 17), uint8_t(n[3] * 17)};
        return true;
    }
    if (value.size() != 7 && value.size() != 9) return false;
    uint8_t channels[4]{0, 0, 0, 255};
    for (size_t channel = 0; channel < (value.size() == 9 ? 4u : 3u); ++channel) {
        int hi = nibble(value[1 + channel * 2]);
        int lo = nibble(value[2 + channel * 2]);
        if (hi < 0 || lo < 0) return false;
        channels[channel] = static_cast<uint8_t>(hi * 16 + lo);
    }
    out = {channels[0], channels[1], channels[2], channels[3]};
    return true;
}

} // namespace micant::um::theme
