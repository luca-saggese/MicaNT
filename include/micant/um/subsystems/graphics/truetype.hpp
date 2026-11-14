#pragma once

// Bounded, allocation-free reader for the TrueType outline subset used by the
// initial user-mode font path. The view borrows its byte span; callers keep
// that storage alive for the lifetime of the view.
#include <cstddef>
#include <cstdint>

namespace micant::um::graphics::truetype {

struct FaceMetrics {
    uint16_t unitsPerEm{};
    int16_t ascender{};
    int16_t descender{};
    int16_t lineGap{};
    uint16_t glyphCount{};
    uint16_t horizontalMetricCount{};
    uint16_t weightClass{};
    bool useTypographicMetrics{};
};

struct GlyphMetrics {
    uint16_t advanceWidth{};
    int16_t leftSideBearing{};
};

class FaceView {
public:
    [[nodiscard]] bool parse(const void* bytes, size_t length) noexcept {
        data_ = static_cast<const uint8_t*>(bytes);
        length_ = length;
        tableCount_ = 0;
        if (!data_ || length < 12 || length > MaxFileBytes || be32(0) != 0x00010000u)
            return fail();
        const uint16_t count = be16(4);
        if (!count || count > MaxTables || !range(12, static_cast<size_t>(count) * 16))
            return fail();
        const uint64_t directoryEnd = 12 + static_cast<uint64_t>(count) * 16;
        tableCount_ = count;
        for (uint16_t i = 0; i < count; ++i) {
            const size_t p = 12 + static_cast<size_t>(i) * 16;
            Table current{be32(p), be32(p + 8), be32(p + 12)};
            if (!range(current.offset, current.length) ||
                (current.length && current.offset < directoryEnd)) return fail();
            for (uint16_t j = 0; j < i; ++j) {
                const auto& prior = tables_[j];
                if (current.tag == prior.tag) return fail();
                const uint64_t aEnd = static_cast<uint64_t>(current.offset) + current.length;
                const uint64_t bEnd = static_cast<uint64_t>(prior.offset) + prior.length;
                if (current.length && prior.length && current.offset < bEnd && prior.offset < aEnd)
                    return fail();
            }
            tables_[i] = current;
        }

        const Table* head = find(TagHead);
        const Table* maxp = find(TagMaxp);
        const Table* hhea = find(TagHhea);
        const Table* hmtx = find(TagHmtx);
        const Table* cmap = find(TagCmap);
        const Table* loca = find(TagLoca);
        const Table* glyf = find(TagGlyf);
        const Table* os2 = find(TagOs2);
        if (!head || head->length < 54 || !maxp || maxp->length < 6 ||
            !hhea || hhea->length < 36 || !hmtx || !cmap || !loca || !glyf)
            return fail();

        metrics_.unitsPerEm = be16(head->offset + 18);
        locaFormat_ = bes16(head->offset + 50);
        metrics_.glyphCount = be16(maxp->offset + 4);
        metrics_.ascender = bes16(hhea->offset + 4);
        metrics_.descender = bes16(hhea->offset + 6);
        metrics_.lineGap = bes16(hhea->offset + 8);
        metrics_.horizontalMetricCount = be16(hhea->offset + 34);
        metrics_.weightClass = os2 && os2->length >= 6 ? be16(os2->offset + 4) : 400;
        metrics_.useTypographicMetrics = os2 && os2->length >= 64 &&
            (be16(os2->offset + 62) & 0x0080u) != 0;
        if (metrics_.useTypographicMetrics && os2->length >= 74) {
            metrics_.ascender = bes16(os2->offset + 68);
            metrics_.descender = bes16(os2->offset + 70);
            metrics_.lineGap = bes16(os2->offset + 72);
        }
        if (metrics_.unitsPerEm < 16 || metrics_.unitsPerEm > 16384 ||
            !metrics_.glyphCount || metrics_.glyphCount > MaxGlyphs ||
            !metrics_.horizontalMetricCount ||
            metrics_.horizontalMetricCount > metrics_.glyphCount ||
            (locaFormat_ != 0 && locaFormat_ != 1)) return fail();
        if (static_cast<uint64_t>(metrics_.horizontalMetricCount) * 4 +
                static_cast<uint64_t>(metrics_.glyphCount - metrics_.horizontalMetricCount) * 2 > hmtx->length ||
            static_cast<uint64_t>(metrics_.glyphCount + 1) * (locaFormat_ ? 4 : 2) > loca->length)
            return fail();
        hmtxOffset_ = hmtx->offset;
        locaOffset_ = loca->offset;
        glyfOffset_ = glyf->offset;
        glyfLength_ = glyf->length;
        if (!parseCmap(*cmap)) return fail();
        valid_ = true;
        return true;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const FaceMetrics& metrics() const noexcept { return metrics_; }

    [[nodiscard]] uint16_t glyphIndex(uint32_t scalar) const noexcept {
        if (!valid_ || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return 0;
        if (cmapFormat_ == 12) {
            uint32_t lo = 0, hi = cmapGroups_;
            while (lo < hi) {
                const uint32_t mid = lo + (hi - lo) / 2;
                const size_t p = cmapOffset_ + 16 + static_cast<size_t>(mid) * 12;
                const uint32_t first = be32(p), last = be32(p + 4);
                if (scalar < first) hi = mid;
                else if (scalar > last) lo = mid + 1;
                else {
                    const uint64_t glyph = static_cast<uint64_t>(be32(p + 8)) + scalar - first;
                    return glyph < metrics_.glyphCount ? static_cast<uint16_t>(glyph) : 0;
                }
            }
            return 0;
        }
        if (scalar > 0xFFFF) return 0;
        const uint16_t segCount = cmapSegments_;
        const size_t endBase = cmapOffset_ + 14;
        const size_t startBase = endBase + static_cast<size_t>(segCount) * 2 + 2;
        const size_t deltaBase = startBase + static_cast<size_t>(segCount) * 2;
        const size_t rangeBase = deltaBase + static_cast<size_t>(segCount) * 2;
        for (uint16_t i = 0; i < segCount; ++i) {
            const uint16_t end = be16(endBase + static_cast<size_t>(i) * 2);
            if (scalar > end) continue;
            const uint16_t start = be16(startBase + static_cast<size_t>(i) * 2);
            if (scalar < start) return 0;
            const int16_t delta = bes16(deltaBase + static_cast<size_t>(i) * 2);
            const uint16_t range = be16(rangeBase + static_cast<size_t>(i) * 2);
            if (!range) return static_cast<uint16_t>((scalar + delta) & 0xFFFFu);
            const size_t word = rangeBase + static_cast<size_t>(i) * 2;
            const uint64_t glyphPos = static_cast<uint64_t>(word) + range +
                                      static_cast<uint64_t>(scalar - start) * 2;
            if (glyphPos + 2 > length_) return 0;
            const uint16_t glyph = be16(static_cast<size_t>(glyphPos));
            return glyph ? static_cast<uint16_t>((glyph + delta) & 0xFFFFu) : 0;
        }
        return 0;
    }

    [[nodiscard]] bool glyphMetrics(uint16_t glyph, GlyphMetrics& result) const noexcept {
        if (!valid_ || glyph >= metrics_.glyphCount) return false;
        const size_t metric = glyph < metrics_.horizontalMetricCount
            ? hmtxOffset_ + static_cast<size_t>(glyph) * 4
            : hmtxOffset_ + static_cast<size_t>(metrics_.horizontalMetricCount - 1) * 4 +
                static_cast<size_t>(glyph - metrics_.horizontalMetricCount + 1) * 2;
        if (glyph < metrics_.horizontalMetricCount) {
            result.advanceWidth = be16(metric);
            result.leftSideBearing = bes16(metric + 2);
        } else {
            result.advanceWidth = be16(hmtxOffset_ + static_cast<size_t>(metrics_.horizontalMetricCount - 1) * 4);
            result.leftSideBearing = bes16(metric);
        }
        return true;
    }

    [[nodiscard]] bool glyphData(uint16_t glyph, const uint8_t*& data, size_t& length) const noexcept {
        if (!valid_ || glyph >= metrics_.glyphCount) return false;
        const size_t entry = locaOffset_ + static_cast<size_t>(glyph) * (locaFormat_ ? 4 : 2);
        const uint32_t begin = locaFormat_ ? be32(entry) : static_cast<uint32_t>(be16(entry)) * 2;
        const uint32_t end = locaFormat_ ? be32(entry + 4) : static_cast<uint32_t>(be16(entry + 2)) * 2;
        if (begin > end || end > glyfLength_) return false;
        data = data_ + glyfOffset_ + begin;
        length = end - begin;
        return true;
    }

    [[nodiscard]] bool glyphBounds(uint16_t glyph, int16_t& xMin, int16_t& yMin,
                                   int16_t& xMax, int16_t& yMax) const noexcept {
        const uint8_t* bytes{}; size_t size{};
        if (!glyphData(glyph, bytes, size) || size < 10) return false;
        const size_t offset = static_cast<size_t>(bytes - data_);
        xMin = bes16(offset + 2); yMin = bes16(offset + 4);
        xMax = bes16(offset + 6); yMax = bes16(offset + 8);
        return xMin <= xMax && yMin <= yMax;
    }

private:
    static constexpr size_t MaxFileBytes = 16 * 1024 * 1024;
    static constexpr uint16_t MaxTables = 128;
    static constexpr uint16_t MaxGlyphs = 65535;
    static constexpr uint32_t TagHead = 0x68656164, TagMaxp = 0x6D617870;
    static constexpr uint32_t TagHhea = 0x68686561, TagHmtx = 0x686D7478;
    static constexpr uint32_t TagCmap = 0x636D6170, TagLoca = 0x6C6F6361;
    static constexpr uint32_t TagGlyf = 0x676C7966, TagOs2 = 0x4F532F32;
    struct Table { uint32_t tag{}, offset{}, length{}; };

    [[nodiscard]] bool fail() noexcept { valid_ = false; return false; }
    [[nodiscard]] bool range(size_t offset, size_t size) const noexcept {
        return offset <= length_ && size <= length_ - offset;
    }
    [[nodiscard]] uint16_t be16(size_t p) const noexcept {
        return static_cast<uint16_t>((static_cast<uint16_t>(data_[p]) << 8) | data_[p + 1]);
    }
    [[nodiscard]] int16_t bes16(size_t p) const noexcept { return static_cast<int16_t>(be16(p)); }
    [[nodiscard]] uint32_t be32(size_t p) const noexcept {
        return (static_cast<uint32_t>(data_[p]) << 24) | (static_cast<uint32_t>(data_[p + 1]) << 16) |
               (static_cast<uint32_t>(data_[p + 2]) << 8) | data_[p + 3];
    }
    [[nodiscard]] const Table* find(uint32_t tag) const noexcept {
        for (uint16_t i = 0; i < tableCount_; ++i) if (tables_[i].tag == tag) return &tables_[i];
        return nullptr;
    }
    [[nodiscard]] bool parseCmap(const Table& table) noexcept {
        if (table.length < 4 || be16(table.offset) != 0) return false;
        const uint16_t count = be16(table.offset + 2);
        if (!count || count > 256 || static_cast<uint64_t>(count) * 8 + 4 > table.length) return false;
        uint32_t format12 = 0, format4 = 0;
        uint16_t groups = 0, segments = 0;
        for (uint16_t i = 0; i < count; ++i) {
            const size_t record = table.offset + 4 + static_cast<size_t>(i) * 8;
            const uint16_t platform = be16(record), encoding = be16(record + 2);
            const uint32_t rel = be32(record + 4);
            if (rel > table.length || table.length - rel < 2) return false;
            const size_t sub = table.offset + rel;
            const uint16_t format = be16(sub);
            if (format == 12 && platform == 3 && encoding == 10) {
                if (table.length - rel < 16) return false;
                const uint32_t size = be32(sub + 4), n = be32(sub + 12);
                if (size < 16 || size > table.length - rel || n > 0xFFFF ||
                    static_cast<uint64_t>(n) * 12 + 16 > size) return false;
                format12 = static_cast<uint32_t>(sub); groups = static_cast<uint16_t>(n);
            } else if (format == 4 && platform == 3 && (encoding == 1 || encoding == 10)) {
                if (table.length - rel < 16) return false;
                const uint16_t size = be16(sub + 2), segCountX2 = be16(sub + 6);
                if (size < 16 || size > table.length - rel || !segCountX2 || (segCountX2 & 1)) return false;
                const uint16_t n = segCountX2 / 2;
                if (static_cast<uint64_t>(n) * 8 + 16 > size) return false;
                format4 = static_cast<uint32_t>(sub); segments = n;
            }
        }
        if (format12) { cmapFormat_ = 12; cmapOffset_ = format12; cmapGroups_ = groups; }
        else if (format4) { cmapFormat_ = 4; cmapOffset_ = format4; cmapSegments_ = segments; }
        else return false;
        return true;
    }

    const uint8_t* data_{};
    size_t length_{};
    Table tables_[MaxTables]{};
    uint16_t tableCount_{};
    FaceMetrics metrics_{};
    uint32_t hmtxOffset_{}, locaOffset_{}, glyfOffset_{}, glyfLength_{}, cmapOffset_{};
    uint16_t cmapFormat_{}, cmapSegments_{}, cmapGroups_{};
    int16_t locaFormat_{};
    bool valid_{};
};

} // namespace micant::um::graphics::truetype
