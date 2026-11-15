// Bounded freestanding DirectWrite font collection backed by the Ring 3 TTF
// manager. Text formatting/layout interfaces remain explicitly unsupported.
#define MICANT_DWRITE_ABI_ONLY
#include "micant/um/subsystems/graphics/dwrite.hpp"
#include "micant/um/subsystems/graphics/font_manager.hpp"
#include <new>

void operator delete(void*) noexcept {}

namespace micant::dwrite {
namespace {

constexpr uint32_t FamilyCount = 6;
constexpr uint32_t VariantCount = 5;
constexpr uint32_t MaxLayoutText = 256;
constexpr const char16_t* FamilyUtf16[FamilyCount] = {
    u"Segoe UI", u"Arial", u"Times New Roman", u"Courier New", u"Calibri",
    u"DejaVu Sans"
};
constexpr const wchar_t* FamilyWide[FamilyCount] = {
    L"Segoe UI", L"Arial", L"Times New Roman", L"Courier New", L"Calibri",
    L"DejaVu Sans"
};
constexpr const wchar_t* VariantNames[VariantCount] = {
    L"Regular", L"Semibold", L"Bold", L"Italic", L"Bold Italic"
};

// Static COM objects avoid a hosted allocator in the native DLL. Construction
// happens lazily in caller-owned Ring 3 address space on first acquisition.
template<typename T>
struct ObjectSlot {
    alignas(T) uint8_t storage[sizeof(T)];
    uint32_t state;

    template<typename... Args>
    T* get(Args... args) noexcept {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&state, &expected, 1, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            T* object = ::new (static_cast<void*>(storage)) T(args...);
            __atomic_store_n(&state, 2, __ATOMIC_RELEASE);
            return object;
        }
        while (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != 2) {}
        return reinterpret_cast<T*>(storage);
    }

    template<typename... Args>
    T* create(Args... args) noexcept {
        uint32_t expected = 0;
        if (!__atomic_compare_exchange_n(&state, &expected, 1, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return nullptr;
        T* object = ::new (static_cast<void*>(storage)) T(this, args...);
        __atomic_store_n(&state, 2, __ATOMIC_RELEASE);
        return object;
    }

    void destroy(T* object) noexcept {
        uint32_t expected = 2;
        if (!__atomic_compare_exchange_n(&state, &expected, 3, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
        object->~T();
        __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
    }
};

template<typename T>
uint32_t addRef(uint32_t& value) noexcept {
    (void)sizeof(T);
    return __atomic_add_fetch(&value, 1u, __ATOMIC_RELAXED);
}
uint32_t releaseRef(uint32_t& value) noexcept {
    uint32_t current = __atomic_load_n(&value, __ATOMIC_RELAXED);
    while (current && !__atomic_compare_exchange_n(&value, &current, current - 1,
            false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {}
    return current ? current - 1 : 0;
}

bool equalFamily(const wchar_t* left, const wchar_t* right) noexcept {
    if (!left || !right) return false;
    while (*left && *right) {
        wchar_t a = *left++, b = *right++;
        if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
        if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
        if (a != b) return false;
    }
    return *left == 0 && *right == 0;
}

DWRITE_FONT_WEIGHT weightFor(uint32_t variant) noexcept {
    return variant == 1 ? DWRITE_FONT_WEIGHT_SEMI_BOLD :
        ((variant == 2 || variant == 4) ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
}
DWRITE_FONT_STYLE styleFor(uint32_t variant) noexcept {
    return variant >= 3 ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

class LocalizedStrings final : public IDWriteLocalizedStrings {
public:
    LocalizedStrings(const wchar_t* text, const wchar_t* locale) noexcept
        : text_(text), locale_(locale) {}
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteLocalizedStrings)
            return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteLocalizedStrings*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteLocalizedStrings>(refs_); }
    uint32_t __stdcall Release() override { return releaseRef(refs_); }
    uint32_t __stdcall GetCount() override { return 1; }
    int32_t __stdcall FindLocaleName(const wchar_t* locale, uint32_t* index, int32_t* exists) override {
        if (!index || !exists) return ole32::E_POINTER;
        *index = 0; *exists = !locale || equalFamily(locale, locale_); return ole32::S_OK;
    }
    int32_t __stdcall GetLocaleNameLength(uint32_t index, uint32_t* length) override {
        if (!length) return ole32::E_POINTER;
        if (index) return ole32::E_INVALIDARG;
        *length = lengthOf(locale_); return ole32::S_OK;
    }
    int32_t __stdcall GetLocaleName(uint32_t index, wchar_t* out, uint32_t size) override {
        if (!out) return ole32::E_POINTER;
        if (index) return ole32::E_INVALIDARG;
        return copy(locale_, out, size);
    }
    int32_t __stdcall GetStringLength(uint32_t index, uint32_t* length) override {
        if (!length) return ole32::E_POINTER;
        if (index) return ole32::E_INVALIDARG;
        *length = lengthOf(text_); return ole32::S_OK;
    }
    int32_t __stdcall GetString(uint32_t index, wchar_t* out, uint32_t size) override {
        if (!out) return ole32::E_POINTER;
        if (index) return ole32::E_INVALIDARG;
        return copy(text_, out, size);
    }
private:
    static uint32_t lengthOf(const wchar_t* value) noexcept {
        uint32_t length = 0; while (value && value[length]) ++length; return length;
    }
    static int32_t copy(const wchar_t* value, wchar_t* out, uint32_t size) noexcept {
        if (!size) return ole32::E_NOT_SUFFICIENT_BUFFER;
        uint32_t i = 0;
        while (value && value[i] && i + 1 < size) { out[i] = value[i]; ++i; }
        out[i] = 0;
        return value && value[i] ? ole32::E_NOT_SUFFICIENT_BUFFER : ole32::S_OK;
    }
    uint32_t refs_{1};
    const wchar_t* text_{};
    const wchar_t* locale_{};
};

class Font;
class TextFormat;
class TextLayout;
class Face final : public IDWriteFontFace {
public:
    explicit Face(Font* font) noexcept : font_(font) {}
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteFontFace) return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteFontFace*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteFontFace>(refs_); }
    uint32_t __stdcall Release() override { return releaseRef(refs_); }
    uint32_t __stdcall GetIndex() override { return 0; }
    DWRITE_FONT_SIMULATIONS __stdcall GetSimulations() override { return DWRITE_FONT_SIMULATIONS_NONE; }
    int32_t __stdcall IsSymbolFont() override { return 0; }
    void __stdcall GetMetrics(DWRITE_FONT_METRICS* metrics) override;
    uint16_t __stdcall GetGlyphCount() override;
    int32_t __stdcall GetDesignGlyphMetrics(const uint16_t*, uint32_t count,
            DWRITE_GLYPH_METRICS* metrics, int32_t) override {
        if (count && !metrics) return ole32::E_POINTER;
        return ole32::E_NOTIMPL;
    }
    int32_t __stdcall GetGlyphIndices(const uint32_t* codePoints, uint32_t count,
            uint16_t* glyphs) override;
private:
    uint32_t refs_{1};
    Font* font_{};
};

class Font final : public IDWriteFont {
public:
    Font(uint32_t family, uint32_t variant) noexcept : family_(family), variant_(variant) {}
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteFont) return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteFont*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteFont>(refs_); }
    uint32_t __stdcall Release() override { return releaseRef(refs_); }
    DWRITE_FONT_WEIGHT __stdcall GetWeight() override { return weightFor(variant_); }
    DWRITE_FONT_STYLE __stdcall GetStyle() override { return styleFor(variant_); }
    DWRITE_FONT_STRETCH __stdcall GetStretch() override { return DWRITE_FONT_STRETCH_NORMAL; }
    DWRITE_FONT_SIMULATIONS __stdcall GetSimulations() override { return DWRITE_FONT_SIMULATIONS_NONE; }
    int32_t __stdcall IsSymbolFont() override { return 0; }
    int32_t __stdcall GetFaceNames(IDWriteLocalizedStrings** names) override {
        if (!names) return ole32::E_POINTER;
        *names = nameSlots[variant_].get(VariantNames[variant_], L"en-us");
        (*names)->AddRef(); return ole32::S_OK;
    }
    void __stdcall GetMetrics(DWRITE_FONT_METRICS* metrics) override {
        if (!metrics) return;
        *metrics = {};
        if (!ensure()) return;
        fillMetrics(manager_, *metrics);
    }
    int32_t __stdcall CreateFontFace(IDWriteFontFace** out) override;
    bool ensure() noexcept {
        uint32_t state = __atomic_load_n(&loadState_, __ATOMIC_ACQUIRE);
        if (state == 2) return true;
        if (state == 3) return false;
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&loadState_, &expected, 1, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            micant::um::graphics::LogicalFontRequest request{};
            request.requestedFamily = FamilyUtf16[family_];
            request.weight = static_cast<uint16_t>(weightFor(variant_));
            request.italic = styleFor(variant_) != DWRITE_FONT_STYLE_NORMAL;
            request.pointSize = 9; request.dpi = 96;
            const bool loaded = manager_.initializeForMetrics(request);
            __atomic_store_n(&loadState_, loaded ? 2u : 3u, __ATOMIC_RELEASE);
            return loaded;
        }
        while ((state = __atomic_load_n(&loadState_, __ATOMIC_ACQUIRE)) == 1) {}
        return state == 2;
    }
    micant::um::graphics::FontManager& manager() noexcept { return manager_; }
private:
    static void fillMetrics(micant::um::graphics::FontManager& manager,
                            DWRITE_FONT_METRICS& output) noexcept {
        const auto& m = manager.metrics();
        output.designUnitsPerEm = m.unitsPerEm;
        output.ascent = m.ascender > 0 ? static_cast<uint16_t>(m.ascender) : 0;
        output.descent = m.descender < 0 ? static_cast<uint16_t>(-m.descender) : 0;
        output.lineGap = m.lineGap;
        output.capHeight = m.capHeight > 0 ? static_cast<uint16_t>(m.capHeight) : 0;
        output.xHeight = m.xHeight > 0 ? static_cast<uint16_t>(m.xHeight) : 0;
        output.underlinePosition = m.underlinePosition;
        output.underlineThickness = m.underlineThickness > 0 ? static_cast<uint16_t>(m.underlineThickness) : 0;
        output.strikethroughPosition = m.strikethroughPosition;
        output.strikethroughThickness = m.strikethroughThickness > 0 ? static_cast<uint16_t>(m.strikethroughThickness) : 0;
    }
    uint32_t refs_{1};
    uint32_t family_{};
    uint32_t variant_{};
    uint32_t loadState_{}; // 0 unloaded, 1 loading, 2 loaded, 3 failed
    micant::um::graphics::FontManager manager_{};
    static ObjectSlot<LocalizedStrings> nameSlots[VariantCount];
    friend class Face;
};
ObjectSlot<LocalizedStrings> Font::nameSlots[VariantCount]{};

void Face::GetMetrics(DWRITE_FONT_METRICS* metrics) {
    if (!metrics) return;
    *metrics = {};
    if (font_ && font_->ensure()) Font::fillMetrics(font_->manager(), *metrics);
}
uint16_t Face::GetGlyphCount() { return font_ && font_->ensure() ? font_->manager().glyphCount() : 0; }
int32_t Face::GetGlyphIndices(const uint32_t* codePoints, uint32_t count, uint16_t* glyphs) {
    if (count && (!codePoints || !glyphs)) return ole32::E_POINTER;
    if (!font_ || !font_->ensure()) return ole32::E_FAIL;
    for (uint32_t i = 0; i < count; ++i) glyphs[i] = font_->manager().glyphIndex(codePoints[i]);
    return ole32::S_OK;
}
ObjectSlot<Face> faceSlots[FamilyCount * VariantCount]{};
ObjectSlot<Font> fontSlots[FamilyCount * VariantCount]{};
ObjectSlot<LocalizedStrings> familyNameSlots[FamilyCount]{};
int32_t Font::CreateFontFace(IDWriteFontFace** out) {
    if (!out) return ole32::E_POINTER;
    *out = nullptr;
    if (!ensure()) return ole32::E_FAIL;
    Face* face = faceSlots[family_ * VariantCount + variant_].get(this);
    *out = face; face->AddRef(); return ole32::S_OK;
}

class Family final : public IDWriteFontFamily {
public:
    explicit Family(uint32_t index) noexcept : index_(index) {}
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteFontFamily) return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteFontFamily*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteFontFamily>(refs_); }
    uint32_t __stdcall Release() override { return releaseRef(refs_); }
    uint32_t __stdcall GetFontCount() override { return index_ == 0 ? 3 : 4; }
    int32_t __stdcall GetFont(uint32_t index, IDWriteFont** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        const uint32_t count = GetFontCount();
        if (index >= count) return ole32::E_INVALIDARG;
        const uint32_t variant = index_ == 0 ? index : (index == 0 ? 0 : index + 1);
        Font* font = fontSlots[index_ * VariantCount + variant].get(index_, variant);
        *out = font; font->AddRef(); return ole32::S_OK;
    }
    int32_t __stdcall GetFamilyNames(IDWriteLocalizedStrings** names) override {
        if (!names) return ole32::E_POINTER;
        *names = familyNameSlots[index_].get(FamilyWide[index_], L"en-us");
        (*names)->AddRef(); return ole32::S_OK;
    }
private:
    uint32_t refs_{1};
    uint32_t index_{};
};
ObjectSlot<Family> familySlots[FamilyCount]{};

class Collection final : public IDWriteFontCollection {
public:
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteFontCollection) return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteFontCollection*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteFontCollection>(refs_); }
    uint32_t __stdcall Release() override { return releaseRef(refs_); }
    uint32_t __stdcall GetFontFamilyCount() override { return FamilyCount; }
    int32_t __stdcall GetFontFamily(uint32_t index, IDWriteFontFamily** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (index >= FamilyCount) return ole32::E_INVALIDARG;
        Family* family = familySlots[index].get(index);
        *out = family; family->AddRef(); return ole32::S_OK;
    }
    int32_t __stdcall FindFamilyName(const wchar_t* name, uint32_t* index, int32_t* exists) override {
        if (!name || !index || !exists) return ole32::E_POINTER;
        *exists = 0; *index = 0;
        for (uint32_t i = 0; i < FamilyCount; ++i) {
            if (equalFamily(name, FamilyWide[i])) { *index = i; *exists = 1; break; }
        }
        return ole32::S_OK;
    }
private:
    uint32_t refs_{1};
};
ObjectSlot<Collection> collectionSlot{};

class Factory final : public IDWriteFactory {
public:
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteFactory) return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteFactory*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteFactory>(refs_); }
    uint32_t __stdcall Release() override { return releaseRef(refs_); }
    int32_t __stdcall GetSystemFontCollection(IDWriteFontCollection** out, int32_t) override {
        if (!out) return ole32::E_POINTER;
        Collection* collection = collectionSlot.get(); *out = collection; collection->AddRef(); return ole32::S_OK;
    }
    int32_t __stdcall CreateTextFormat(const wchar_t* family, IDWriteFontCollection* collection,
            DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,
            float size, const wchar_t* locale, IDWriteTextFormat** out) override;
    int32_t __stdcall CreateTypography(IDWriteTypography** out) override {
        if (!out) return ole32::E_POINTER; *out = nullptr; return ole32::E_NOTIMPL;
    }
    int32_t __stdcall CreateRenderingParams(IDWriteRenderingParams** out) override {
        if (!out) return ole32::E_POINTER; *out = nullptr; return ole32::E_NOTIMPL;
    }
    int32_t __stdcall CreateCustomRenderingParams(float, float, float, DWRITE_PIXEL_GEOMETRY,
            DWRITE_RENDERING_MODE, IDWriteRenderingParams** out) override {
        if (!out) return ole32::E_POINTER; *out = nullptr; return ole32::E_NOTIMPL;
    }
    int32_t __stdcall CreateTextLayout(const wchar_t* text, uint32_t length,
            IDWriteTextFormat* format, float width, float height,
            IDWriteTextLayout** out) override;
private:
    uint32_t refs_{1};
};
ObjectSlot<Factory> factorySlots[2]{};

bool finitePositive(float value, float maximum) noexcept {
    return value > 0.0f && value <= maximum && value == value;
}

int familyIndexFor(const wchar_t* family) noexcept {
    if (!family) return -1;
    for (uint32_t i = 0; i < FamilyCount; ++i)
        if (equalFamily(family, FamilyWide[i])) return static_cast<int>(i);
    return -1;
}

bool copyWide(const wchar_t* source, wchar_t* output, uint32_t capacity) noexcept {
    if (!source || !output || capacity < 2) return false;
    uint32_t length = 0;
    while (source[length]) {
        if (length + 1 >= capacity || static_cast<uint32_t>(source[length]) > 0xffffu)
            return false;
        output[length] = source[length];
        ++length;
    }
    output[length] = 0;
    return true;
}

bool validLocale(const wchar_t* locale) noexcept {
    if (!locale) return true;
    for (uint32_t i = 0; locale[i]; ++i)
        if (locale[i] < 0x20 || locale[i] > 0x7e || i >= 31) return false;
    return true;
}

uint16_t pointSizeForDip(float dip) noexcept {
    float points = dip * 0.75f + 0.5f;
    if (points < 1.0f) return 1;
    if (points > 384.0f) return 384;
    return static_cast<uint16_t>(points);
}

class TextFormat final : public IDWriteTextFormat {
public:
    TextFormat(ObjectSlot<TextFormat>* slot, const wchar_t* family, DWRITE_FONT_WEIGHT weight,
            DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, float size,
            const wchar_t* locale) noexcept
        : slot_(slot), weight_(weight), style_(style), stretch_(stretch), size_(size) {
        familyIndex_ = familyIndexFor(family);
        valid_ = familyIndex_ >= 0 && copyWide(family, family_, 64) &&
            copyWide(locale ? locale : L"en-us", locale_, 32) &&
            weight >= 1 && weight <= 1000 &&
            style >= DWRITE_FONT_STYLE_NORMAL && style <= DWRITE_FONT_STYLE_ITALIC &&
            stretch >= DWRITE_FONT_STRETCH_ULTRA_CONDENSED &&
            stretch <= DWRITE_FONT_STRETCH_ULTRA_EXPANDED && finitePositive(size, 512.0f) &&
            validLocale(locale);
        if (valid_) valid_ = initializeFont();
    }
    ~TextFormat() { manager_.shutdown(); }
    bool valid() const noexcept { return valid_; }
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid != ole32::IID_IUnknown && iid != IID_IDWriteTextFormat) return ole32::E_NOINTERFACE;
        *out = static_cast<IDWriteTextFormat*>(this); AddRef(); return ole32::S_OK;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteTextFormat>(refs_); }
    uint32_t __stdcall Release() override {
        const uint32_t remaining = releaseRef(refs_);
        if (!remaining) slot_->destroy(this);
        return remaining;
    }
    int32_t __stdcall SetTextAlignment(DWRITE_TEXT_ALIGNMENT value) override {
        if (value < DWRITE_TEXT_ALIGNMENT_LEADING || value > DWRITE_TEXT_ALIGNMENT_JUSTIFIED)
            return ole32::E_INVALIDARG;
        textAlignment_ = value; return ole32::S_OK;
    }
    int32_t __stdcall SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT value) override {
        if (value < DWRITE_PARAGRAPH_ALIGNMENT_NEAR || value > DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
            return ole32::E_INVALIDARG;
        paragraphAlignment_ = value; return ole32::S_OK;
    }
    int32_t __stdcall SetWordWrapping(DWRITE_WORD_WRAPPING value) override {
        if (value != DWRITE_WORD_WRAPPING_WRAP && value != DWRITE_WORD_WRAPPING_NO_WRAP)
            return ole32::E_NOTIMPL;
        wrapping_ = value; return ole32::S_OK;
    }
    DWRITE_TEXT_ALIGNMENT __stdcall GetTextAlignment() override { return textAlignment_; }
    DWRITE_PARAGRAPH_ALIGNMENT __stdcall GetParagraphAlignment() override { return paragraphAlignment_; }
    DWRITE_WORD_WRAPPING __stdcall GetWordWrapping() override { return wrapping_; }
    uint32_t __stdcall GetFontFamilyNameLength() override { return lengthOf(family_); }
    int32_t __stdcall GetFontFamilyName(wchar_t* out, uint32_t size) override {
        return copyBuffer(family_, out, size);
    }
    DWRITE_FONT_WEIGHT __stdcall GetFontWeight() override { return weight_; }
    DWRITE_FONT_STYLE __stdcall GetFontStyle() override { return style_; }
    DWRITE_FONT_STRETCH __stdcall GetFontStretch() override { return stretch_; }
    float __stdcall GetFontSize() override { return size_; }
    uint32_t __stdcall GetLocaleNameLength() override { return lengthOf(locale_); }
    int32_t __stdcall GetLocaleName(wchar_t* out, uint32_t size) override {
        return copyBuffer(locale_, out, size);
    }
    bool initializeFont() noexcept {
        if (familyIndex_ < 0) return false;
        micant::um::graphics::LogicalFontRequest request{};
        request.requestedFamily = FamilyUtf16[static_cast<uint32_t>(familyIndex_)];
        request.weight = static_cast<uint16_t>(weight_);
        request.italic = style_ != DWRITE_FONT_STYLE_NORMAL;
        request.pointSize = pointSizeForDip(size_);
        request.dpi = 96;
        return manager_.initializeForMetrics(request);
    }
    static uint32_t lengthOf(const wchar_t* text) noexcept {
        uint32_t n = 0; while (text && text[n]) ++n; return n;
    }
    static int32_t copyBuffer(const wchar_t* text, wchar_t* out, uint32_t size) noexcept {
        if (!out) return ole32::E_POINTER;
        const uint32_t length = lengthOf(text);
        if (size <= length) return ole32::E_NOT_SUFFICIENT_BUFFER;
        for (uint32_t i = 0; i <= length; ++i) out[i] = text[i];
        return ole32::S_OK;
    }
    ObjectSlot<TextFormat>* slot_{};
    uint32_t refs_{1};
    int32_t familyIndex_{-1};
    wchar_t family_[64]{};
    wchar_t locale_[32]{};
    DWRITE_FONT_WEIGHT weight_{};
    DWRITE_FONT_STYLE style_{};
    DWRITE_FONT_STRETCH stretch_{};
    float size_{};
    DWRITE_TEXT_ALIGNMENT textAlignment_{DWRITE_TEXT_ALIGNMENT_LEADING};
    DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment_{DWRITE_PARAGRAPH_ALIGNMENT_NEAR};
    DWRITE_WORD_WRAPPING wrapping_{DWRITE_WORD_WRAPPING_WRAP};
    bool valid_{};
    micant::um::graphics::FontManager manager_{};
};
ObjectSlot<TextFormat> formatSlots[16]{};

struct LayoutLine {
    uint32_t length{};
    uint32_t trailingWhitespace{};
    uint32_t newlineLength{};
    float width{};
    float widthIncludingTrailingWhitespace{};
};

bool nextScalar(const wchar_t* text, uint32_t length, uint32_t& cursor,
                uint32_t& scalar) noexcept {
    if (cursor >= length) return false;
    const uint32_t first = static_cast<uint16_t>(text[cursor++]);
    if (first >= 0xd800 && first <= 0xdbff) {
        if (cursor >= length) return false;
        const uint32_t second = static_cast<uint16_t>(text[cursor++]);
        if (second < 0xdc00 || second > 0xdfff) return false;
        scalar = 0x10000 + ((first - 0xd800) << 10) + second - 0xdc00;
        return true;
    }
    if (first >= 0xdc00 && first <= 0xdfff) return false;
    scalar = first;
    return true;
}

bool appendUtf8(char* output, size_t capacity, size_t& used, uint32_t scalar) noexcept {
    uint8_t bytes[4]{}; size_t count = 0;
    if (scalar <= 0x7f) { bytes[0] = static_cast<uint8_t>(scalar); count = 1; }
    else if (scalar <= 0x7ff) {
        bytes[0] = static_cast<uint8_t>(0xc0 | (scalar >> 6));
        bytes[1] = static_cast<uint8_t>(0x80 | (scalar & 0x3f)); count = 2;
    } else if (scalar <= 0xffff) {
        bytes[0] = static_cast<uint8_t>(0xe0 | (scalar >> 12));
        bytes[1] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3f));
        bytes[2] = static_cast<uint8_t>(0x80 | (scalar & 0x3f)); count = 3;
    } else if (scalar <= 0x10ffff) {
        bytes[0] = static_cast<uint8_t>(0xf0 | (scalar >> 18));
        bytes[1] = static_cast<uint8_t>(0x80 | ((scalar >> 12) & 0x3f));
        bytes[2] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3f));
        bytes[3] = static_cast<uint8_t>(0x80 | (scalar & 0x3f)); count = 4;
    } else return false;
    if (used + count >= capacity) return false;
    for (size_t i = 0; i < count; ++i) output[used++] = static_cast<char>(bytes[i]);
    output[used] = 0;
    return true;
}

class TextLayout final : public IDWriteTextLayout {
public:
    TextLayout(ObjectSlot<TextLayout>* slot, const wchar_t* text, uint32_t length,
            IDWriteTextFormat* format, float width, float height) noexcept
        : slot_(slot), length_(length), maxWidth_(width), maxHeight_(height) {
        valid_ = text && format && length <= MaxText && finitePositive(width, 16384.0f) &&
            finitePositive(height, 16384.0f);
        if (!valid_) return;
        for (uint32_t i = 0; i < length; ++i) text_[i] = text[i];
        text_[length] = 0;
        wchar_t name[64]{}, locale[32]{};
        if (format->GetFontFamilyName(name, 64) != ole32::S_OK ||
            format->GetLocaleName(locale, 32) != ole32::S_OK ||
            !copyWide(name, family_, 64) || !copyWide(locale, locale_, 32)) {
            valid_ = false; return;
        }
        familyIndex_ = familyIndexFor(family_);
        weight_ = format->GetFontWeight(); style_ = format->GetFontStyle();
        stretch_ = format->GetFontStretch(); size_ = format->GetFontSize();
        textAlignment_ = format->GetTextAlignment();
        paragraphAlignment_ = format->GetParagraphAlignment();
        wrapping_ = format->GetWordWrapping();
        valid_ = familyIndex_ >= 0 && finitePositive(size_, 512.0f) && validateUtf16() &&
            initializeFont();
    }
    ~TextLayout() { manager_.shutdown(); }
    bool valid() const noexcept { return valid_; }
    int32_t __stdcall QueryInterface(const ole32::IID& iid, void** out) override {
        if (!out) return ole32::E_POINTER;
        *out = nullptr;
        if (iid == IID_IDWriteTextLayout || iid == IID_IDWriteTextFormat || iid == ole32::IID_IUnknown) {
            *out = static_cast<IDWriteTextLayout*>(this); AddRef(); return ole32::S_OK;
        }
        return ole32::E_NOINTERFACE;
    }
    uint32_t __stdcall AddRef() override { return addRef<IDWriteTextLayout>(refs_); }
    uint32_t __stdcall Release() override {
        const uint32_t remaining = releaseRef(refs_);
        if (!remaining) slot_->destroy(this);
        return remaining;
    }
    int32_t __stdcall SetTextAlignment(DWRITE_TEXT_ALIGNMENT value) override {
        if (value < DWRITE_TEXT_ALIGNMENT_LEADING || value > DWRITE_TEXT_ALIGNMENT_JUSTIFIED)
            return ole32::E_INVALIDARG;
        textAlignment_ = value; return ole32::S_OK;
    }
    int32_t __stdcall SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT value) override {
        if (value < DWRITE_PARAGRAPH_ALIGNMENT_NEAR || value > DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
            return ole32::E_INVALIDARG;
        paragraphAlignment_ = value; return ole32::S_OK;
    }
    int32_t __stdcall SetWordWrapping(DWRITE_WORD_WRAPPING value) override {
        if (value != DWRITE_WORD_WRAPPING_WRAP && value != DWRITE_WORD_WRAPPING_NO_WRAP)
            return ole32::E_NOTIMPL;
        wrapping_ = value; return ole32::S_OK;
    }
    DWRITE_TEXT_ALIGNMENT __stdcall GetTextAlignment() override { return textAlignment_; }
    DWRITE_PARAGRAPH_ALIGNMENT __stdcall GetParagraphAlignment() override { return paragraphAlignment_; }
    DWRITE_WORD_WRAPPING __stdcall GetWordWrapping() override { return wrapping_; }
    uint32_t __stdcall GetFontFamilyNameLength() override { return TextFormat::lengthOf(family_); }
    int32_t __stdcall GetFontFamilyName(wchar_t* out, uint32_t size) override {
        return TextFormat::copyBuffer(family_, out, size);
    }
    DWRITE_FONT_WEIGHT __stdcall GetFontWeight() override { return weight_; }
    DWRITE_FONT_STYLE __stdcall GetFontStyle() override { return style_; }
    DWRITE_FONT_STRETCH __stdcall GetFontStretch() override { return stretch_; }
    float __stdcall GetFontSize() override { return size_; }
    uint32_t __stdcall GetLocaleNameLength() override { return TextFormat::lengthOf(locale_); }
    int32_t __stdcall GetLocaleName(wchar_t* out, uint32_t size) override {
        return TextFormat::copyBuffer(locale_, out, size);
    }
    int32_t __stdcall SetMaxWidth(float value) override {
        if (!finitePositive(value, 16384.0f)) return ole32::E_INVALIDARG;
        maxWidth_ = value; return ole32::S_OK;
    }
    int32_t __stdcall SetMaxHeight(float value) override {
        if (!finitePositive(value, 16384.0f)) return ole32::E_INVALIDARG;
        maxHeight_ = value; return ole32::S_OK;
    }
    float __stdcall GetMaxWidth() override { return maxWidth_; }
    float __stdcall GetMaxHeight() override { return maxHeight_; }
    int32_t __stdcall GetMetrics(DWRITE_TEXT_METRICS* metrics) override {
        if (!metrics) return ole32::E_POINTER;
        LayoutLine lines[MaxText + 1]{}; uint32_t count{}; float lineHeight{}, baseline{};
        const int32_t status = compute(lines, count, lineHeight, baseline);
        if (status != ole32::S_OK) return status;
        float width = 0, widthIncludingWhitespace = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (lines[i].width > width) width = lines[i].width;
            if (lines[i].widthIncludingTrailingWhitespace > widthIncludingWhitespace)
                widthIncludingWhitespace = lines[i].widthIncludingTrailingWhitespace;
        }
        const float height = lineHeight * static_cast<float>(count);
        if (height > maxHeight_) return ole32::E_NOTIMPL;
        *metrics = {};
        metrics->left = metrics->top = 0.0f;
        metrics->width = width;
        metrics->widthIncludingTrailingWhitespace = widthIncludingWhitespace;
        metrics->height = height; metrics->layoutWidth = maxWidth_; metrics->layoutHeight = maxHeight_;
        metrics->maxBidiReorderingDepth = 1; metrics->lineCount = count;
        return ole32::S_OK;
    }
    int32_t __stdcall GetLineMetrics(DWRITE_LINE_METRICS* output, uint32_t capacity,
                                     uint32_t* actual) override {
        if (!actual || (capacity && !output)) return ole32::E_POINTER;
        LayoutLine lines[MaxText + 1]{}; uint32_t count{}; float lineHeight{}, baseline{};
        const int32_t status = compute(lines, count, lineHeight, baseline);
        if (status != ole32::S_OK) return status;
        *actual = count;
        const uint32_t written = capacity < count ? capacity : count;
        for (uint32_t i = 0; i < written; ++i) {
            output[i].length = lines[i].length;
            output[i].trailingWhitespaceLength = lines[i].trailingWhitespace;
            output[i].newlineLength = lines[i].newlineLength;
            output[i].height = lineHeight; output[i].baseline = baseline;
            output[i].isTrimmed = 0;
        }
        return capacity < count ? ole32::E_NOT_SUFFICIENT_BUFFER : ole32::S_OK;
    }
    int32_t __stdcall GetClusterMetrics(DWRITE_CLUSTER_METRICS*, uint32_t,
                                        uint32_t* actual) override {
        if (!actual) return ole32::E_POINTER;
        *actual = 0; return ole32::E_NOTIMPL;
    }
    int32_t __stdcall SetFontWeight(DWRITE_FONT_WEIGHT value, DWRITE_TEXT_RANGE range) override {
        if (value < 1 || value > 1000) return ole32::E_INVALIDARG;
        return setFontRange(range, value, style_, size_);
    }
    int32_t __stdcall SetFontStyle(DWRITE_FONT_STYLE value, DWRITE_TEXT_RANGE range) override {
        if (value < DWRITE_FONT_STYLE_NORMAL || value > DWRITE_FONT_STYLE_ITALIC)
            return ole32::E_INVALIDARG;
        return setFontRange(range, weight_, value, size_);
    }
    int32_t __stdcall SetFontSize(float value, DWRITE_TEXT_RANGE range) override {
        if (!finitePositive(value, 512.0f)) return ole32::E_INVALIDARG;
        return setFontRange(range, weight_, style_, value);
    }
    int32_t __stdcall SetUnderline(int32_t, DWRITE_TEXT_RANGE) override { return ole32::E_NOTIMPL; }
    int32_t __stdcall SetStrikethrough(int32_t, DWRITE_TEXT_RANGE) override { return ole32::E_NOTIMPL; }
private:
    static constexpr uint32_t MaxText = MaxLayoutText;
    bool validateUtf16() const noexcept {
        uint32_t cursor = 0, scalar{};
        while (cursor < length_) if (!nextScalar(text_, length_, cursor, scalar)) return false;
        return true;
    }
    bool initializeFont() noexcept {
        micant::um::graphics::LogicalFontRequest request{};
        request.requestedFamily = FamilyUtf16[static_cast<uint32_t>(familyIndex_)];
        request.weight = static_cast<uint16_t>(weight_);
        request.italic = style_ != DWRITE_FONT_STYLE_NORMAL;
        request.pointSize = pointSizeForDip(size_); request.dpi = 96;
        return manager_.initializeForMetrics(request);
    }
    int32_t setFontRange(DWRITE_TEXT_RANGE range, DWRITE_FONT_WEIGHT weight,
                         DWRITE_FONT_STYLE style, float size) noexcept {
        if (range.startPosition > length_ || range.length > length_ - range.startPosition)
            return ole32::E_INVALIDARG;
        if (!range.length) return ole32::S_OK;
        if (range.startPosition != 0 || range.length != length_) return ole32::E_NOTIMPL;
        const DWRITE_FONT_WEIGHT oldWeight = weight_;
        const DWRITE_FONT_STYLE oldStyle = style_;
        const float oldSize = size_;
        weight_ = weight; style_ = style; size_ = size;
        manager_.shutdown();
        if (!initializeFont()) {
            weight_ = oldWeight; style_ = oldStyle; size_ = oldSize;
            (void)initializeFont();
            return ole32::E_FAIL;
        }
        return ole32::S_OK;
    }
    int32_t measureRange(uint32_t start, uint32_t end, float& width) noexcept {
        char utf8[4096]{}; size_t used = 0; uint32_t cursor = start, scalar{};
        while (cursor < end) {
            if (!nextScalar(text_, end, cursor, scalar) ||
                !appendUtf8(utf8, sizeof(utf8), used, scalar)) return ole32::E_INVALIDARG;
        }
        micant::um::graphics::TextExtentPixels extent{};
        if (!manager_.measureUtf8(utf8, extent)) return ole32::E_FAIL;
        const uint32_t points = pointSizeForDip(size_);
        const uint32_t pxEm = micant::um::graphics::truetype::pixelsFromPoints(points, 96);
        if (!pxEm) return ole32::E_FAIL;
        width = static_cast<float>(extent.width) * size_ / static_cast<float>(pxEm);
        return ole32::S_OK;
    }
    int32_t compute(LayoutLine* lines, uint32_t& count, float& lineHeight,
                    float& baseline) noexcept {
        count = 0;
        micant::um::graphics::FontMetricsPixels metrics{};
        if (!manager_.pixelMetrics(metrics)) return ole32::E_FAIL;
        const uint32_t points = pointSizeForDip(size_);
        const uint32_t pxEm = micant::um::graphics::truetype::pixelsFromPoints(points, 96);
        if (!pxEm || metrics.lineHeight <= 0) return ole32::E_FAIL;
        const float scale = size_ / static_cast<float>(pxEm);
        lineHeight = static_cast<float>(metrics.lineHeight) * scale;
        baseline = static_cast<float>(metrics.ascent) * scale;
        uint32_t start = 0;
        bool firstLine = true;
        while (start < length_ || firstLine) {
            firstLine = false;
            if (count >= MaxText + 1) return ole32::E_INVALIDARG;
            const uint32_t lineStart = start;
            uint32_t cursor = lineStart;
            float width = 0;
            while (cursor < length_ && text_[cursor] != L'\n' && text_[cursor] != L'\r') {
                uint32_t next = cursor, scalar{};
                if (!nextScalar(text_, length_, next, scalar)) return ole32::E_INVALIDARG;
                float candidateWidth{};
                const int32_t measured = measureRange(lineStart, next, candidateWidth);
                if (measured != ole32::S_OK) return measured;
                if (wrapping_ != DWRITE_WORD_WRAPPING_NO_WRAP &&
                    candidateWidth > maxWidth_ && cursor > start) break;
                cursor = next; width = candidateWidth;
                if (wrapping_ != DWRITE_WORD_WRAPPING_NO_WRAP && width > maxWidth_) break;
            }
            uint32_t newline = 0;
            if (cursor < length_ && (text_[cursor] == L'\n' || text_[cursor] == L'\r')) {
                newline = 1;
                if (text_[cursor] == L'\r' && cursor + 1 < length_ && text_[cursor + 1] == L'\n')
                    newline = 2;
            }
            uint32_t trailing = 0, trimCursor = cursor;
            for (; trimCursor > lineStart &&
                    (text_[trimCursor - 1] == L' ' || text_[trimCursor - 1] == L'\t'); --trimCursor)
                ++trailing;
            float contentWidth = width;
            if (trimCursor < cursor) {
                const int32_t measured = measureRange(lineStart, trimCursor, contentWidth);
                if (measured != ole32::S_OK) return measured;
            }
            lines[count++] = {cursor - lineStart + newline, trailing, newline,
                              contentWidth, width};
            start = cursor + newline;
            if (!newline && start == length_) break;
            if (!newline && cursor == lineStart) return ole32::E_FAIL;
            if (newline && start == length_) firstLine = true;
        }
        if (lineHeight * static_cast<float>(count) > maxHeight_) return ole32::E_NOTIMPL;
        return ole32::S_OK;
    }
    ObjectSlot<TextLayout>* slot_{};
    uint32_t refs_{1};
    wchar_t text_[MaxText + 1]{};
    uint32_t length_{};
    wchar_t family_[64]{};
    wchar_t locale_[32]{};
    int32_t familyIndex_{-1};
    DWRITE_FONT_WEIGHT weight_{};
    DWRITE_FONT_STYLE style_{};
    DWRITE_FONT_STRETCH stretch_{};
    float size_{};
    float maxWidth_{};
    float maxHeight_{};
    DWRITE_TEXT_ALIGNMENT textAlignment_{DWRITE_TEXT_ALIGNMENT_LEADING};
    DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment_{DWRITE_PARAGRAPH_ALIGNMENT_NEAR};
    DWRITE_WORD_WRAPPING wrapping_{DWRITE_WORD_WRAPPING_WRAP};
    bool valid_{};
    micant::um::graphics::FontManager manager_{};
};
ObjectSlot<TextLayout> layoutSlots[16]{};

int32_t Factory::CreateTextFormat(const wchar_t* family, IDWriteFontCollection* collection,
        DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,
        float size, const wchar_t* locale, IDWriteTextFormat** out) {
    if (!out) return ole32::E_POINTER;
    *out = nullptr;
    if (!family || !finitePositive(size, 512.0f) || !validLocale(locale))
        return ole32::E_INVALIDARG;
    if (collection) return ole32::E_NOTIMPL;
    for (auto& slot : formatSlots) {
        TextFormat* format = slot.create(family, weight, style, stretch, size, locale);
        if (!format) continue;
        if (!format->valid()) { slot.destroy(format); return ole32::E_INVALIDARG; }
        *out = format; return ole32::S_OK;
    }
    return ole32::E_OUTOFMEMORY;
}

int32_t Factory::CreateTextLayout(const wchar_t* text, uint32_t length,
        IDWriteTextFormat* format, float width, float height, IDWriteTextLayout** out) {
    if (!out) return ole32::E_POINTER;
    *out = nullptr;
    if (!text || !format || length > MaxLayoutText || !finitePositive(width, 16384.0f) ||
        !finitePositive(height, 16384.0f)) return ole32::E_INVALIDARG;
    for (auto& slot : layoutSlots) {
        TextLayout* layout = slot.create(text, length, format, width, height);
        if (!layout) continue;
        if (!layout->valid()) { slot.destroy(layout); return ole32::E_INVALIDARG; }
        *out = layout; return ole32::S_OK;
    }
    return ole32::E_OUTOFMEMORY;
}

} // namespace

int32_t __stdcall DWriteCreateFactory(DWRITE_FACTORY_TYPE type, const ole32::IID& iid,
                                      ole32::IUnknown** out) {
    if (!out) return ole32::E_POINTER;
    *out = nullptr;
    if (type != DWRITE_FACTORY_TYPE_SHARED && type != DWRITE_FACTORY_TYPE_ISOLATED)
        return ole32::E_INVALIDARG;
    if (iid != IID_IDWriteFactory && iid != ole32::IID_IUnknown) return ole32::E_NOINTERFACE;
    Factory* factory = factorySlots[type == DWRITE_FACTORY_TYPE_SHARED ? 0 : 1].get();
    *out = factory; factory->AddRef(); return ole32::S_OK;
}

} // namespace micant::dwrite

extern "C" int32_t __stdcall DWriteCreateFactory(
        micant::dwrite::DWRITE_FACTORY_TYPE type,
        const micant::ole32::IID& iid,
        micant::ole32::IUnknown** out) {
    return micant::dwrite::DWriteCreateFactory(type, iid, out);
}
