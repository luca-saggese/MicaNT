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
    int32_t __stdcall CreateTextFormat(const wchar_t*, IDWriteFontCollection*, DWRITE_FONT_WEIGHT,
            DWRITE_FONT_STYLE, DWRITE_FONT_STRETCH, float, const wchar_t*, IDWriteTextFormat** out) override {
        if (!out) return ole32::E_POINTER; *out = nullptr; return ole32::E_NOTIMPL;
    }
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
    int32_t __stdcall CreateTextLayout(const wchar_t*, uint32_t, IDWriteTextFormat*, float, float,
            IDWriteTextLayout** out) override {
        if (!out) return ole32::E_POINTER; *out = nullptr; return ole32::E_NOTIMPL;
    }
private:
    uint32_t refs_{1};
};
ObjectSlot<Factory> factorySlots[2]{};

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

extern "C" __declspec(dllexport) int32_t __stdcall DWriteCreateFactory(
        micant::dwrite::DWRITE_FACTORY_TYPE type,
        const micant::ole32::IID& iid,
        micant::ole32::IUnknown** out) {
    return micant::dwrite::DWriteCreateFactory(type, iid, out);
}
