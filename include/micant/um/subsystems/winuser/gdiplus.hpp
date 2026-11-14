#pragma once
#include "micant/shared/ntdef.hpp"
#include "micant/um/subsystems/com/ole32.hpp"
#include "micant/um/subsystems/com/oleaut32.hpp"
#include "micant/um/native/ldr.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#include "micant/um/subsystems/winuser/gdi32.hpp"
#include "micant/um/subsystems/imaging/codec_registry.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/imaging/native_image_save.hpp"
#include "micant/um/subsystems/wic/wic_runtime.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <functional>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace micant::gdiplus {

// ============================================================================
// 1. GDI+ Status Codes & Types
// ============================================================================

enum Status {
    Ok                          = 0,
    GenericError                = 1,
    InvalidParameter            = 2,
    OutOfMemory                 = 3,
    ObjectBusy                  = 4,
    InsufficientBuffer          = 5,
    NotImplemented              = 6,
    Win32Error                  = 7,
    WrongState                  = 8,
    Aborted                     = 9,
    FileNotFound                = 10,
    ValueOverflow               = 11,
    AccessDenied                = 12,
    UnknownImageFormat          = 13,
    PropertyNotFound            = 14,
    PropertyNotSupported        = 15,
    ProfileNotFound             = 16
};

using GpStatus = Status;
using ARGB     = uint32_t;
using REAL     = float;

enum Unit {
    UnitWorld       = 0,
    UnitDisplay     = 1,
    UnitPixel       = 2,
    UnitPoint       = 3,
    UnitInch        = 4,
    UnitDocument    = 5,
    UnitMillimeter  = 6
};

enum SmoothingMode {
    SmoothingModeInvalid     = -1,
    SmoothingModeDefault     = 0,
    SmoothingModeHighSpeed   = 1,
    SmoothingModeHighQuality = 2,
    SmoothingModeNone        = 3,
    SmoothingModeAntiAlias   = 4
};

enum CompositingMode {
    CompositingModeSourceOver    = 0,
    CompositingModeSourceCopy    = 1
};

enum CompositingQuality {
    CompositingQualityInvalid      = -1,
    CompositingQualityDefault      = 0,
    CompositingQualityHighSpeed    = 1,
    CompositingQualityHighQuality  = 2,
    CompositingQualityGammaCorrected = 3,
    CompositingQualityAssumeLinear = 4
};

enum InterpolationMode {
    InterpolationModeInvalid             = -1,
    InterpolationModeDefault             = 0,
    InterpolationModeLowQuality          = 1,
    InterpolationModeHighQuality         = 2,
    InterpolationModeBilinear            = 3,
    InterpolationModeBicubic             = 4,
    InterpolationModeNearestNeighbor     = 5,
    InterpolationModeHighQualityBilinear = 6,
    InterpolationModeHighQualityBicubic  = 7
};

enum PixelOffsetMode {
    PixelOffsetModeInvalid     = -1,
    PixelOffsetModeDefault     = 0,
    PixelOffsetModeHighSpeed   = 1,
    PixelOffsetModeHighQuality = 2,
    PixelOffsetModeNone        = 3,
    PixelOffsetModeHalf        = 4
};

enum DashStyle {
    DashStyleSolid        = 0,
    DashStyleDash         = 1,
    DashStyleDot          = 2,
    DashStyleDashDot      = 3,
    DashStyleDashDotDot   = 4,
    DashStyleCustom       = 5
};

enum LineCap {
    LineCapFlat             = 0,
    LineCapSquare           = 1,
    LineCapRound            = 2,
    LineCapTriangle         = 3,
    LineCapNoAnchor         = 0x10,
    LineCapSquareAnchor     = 0x11,
    LineCapRoundAnchor      = 0x12,
    LineCapDiamondAnchor    = 0x13,
    LineCapArrowAnchor      = 0x14,
    LineCapCustom           = 0xFF
};

enum LineJoin {
    LineJoinMiter           = 0,
    LineJoinBevel           = 1,
    LineJoinRound           = 2,
    LineJoinMiterClipped    = 3
};

enum BrushType {
    BrushTypeSolidColor     = 0,
    BrushTypeHatchFill       = 1,
    BrushTypeTextureFill     = 2,
    BrushTypePathGradient    = 3,
    BrushTypeLinearGradient  = 4
};

enum LinearGradientMode {
    LinearGradientModeHorizontal        = 0,
    LinearGradientModeVertical          = 1,
    LinearGradientModeForwardDiagonal   = 2,
    LinearGradientModeBackwardDiagonal  = 3
};

enum FillMode {
    FillModeAlternate   = 0,
    FillModeWinding     = 1
};

enum MatrixOrder {
    MatrixOrderPrepend  = 0,
    MatrixOrderAppend   = 1
};

enum CombineMode {
    CombineModeReplace      = 0,
    CombineModeIntersect    = 1,
    CombineModeUnion        = 2,
    CombineModeXor          = 3,
    CombineModeExclude      = 4,
    CombineModeComplement   = 5
};

enum PixelFormat {
    PixelFormatUndefined       = 0,
    PixelFormatDontCare        = 0,
    PixelFormat1bppIndexed     = 0x00030101,
    PixelFormat4bppIndexed     = 0x00030402,
    PixelFormat8bppIndexed     = 0x00030803,
    PixelFormat16bppGrayScale  = 0x00101004,
    PixelFormat16bppRGB555     = 0x00021005,
    PixelFormat16bppRGB565     = 0x00021006,
    PixelFormat16bppARGB1555   = 0x00061007,
    PixelFormat24bppRGB        = 0x00021808,
    PixelFormat32bppRGB        = 0x00022009,
    PixelFormat32bppARGB       = 0x0026200A,
    PixelFormat32bppPARGB      = 0x000E200B
};

enum ImageType {
    ImageTypeUnknown    = 0,
    ImageTypeBitmap     = 1,
    ImageTypeMetafile   = 2
};

enum ImageLockMode {
    ImageLockModeRead           = 0x0001,
    ImageLockModeWrite          = 0x0002,
    ImageLockModeUserInputBuf   = 0x0004
};

// ============================================================================
// 2. Geometric Primitives & Color
// ============================================================================

struct Point {
    int32_t X{ 0 };
    int32_t Y{ 0 };

    Point() = default;
    Point(int32_t x, int32_t y) : X(x), Y(y) {}
};

struct PointF {
    float X{ 0.0f };
    float Y{ 0.0f };

    PointF() = default;
    PointF(float x, float y) : X(x), Y(y) {}
};

struct Size {
    int32_t Width{ 0 };
    int32_t Height{ 0 };

    Size() = default;
    Size(int32_t w, int32_t h) : Width(w), Height(h) {}
};

struct SizeF {
    float Width{ 0.0f };
    float Height{ 0.0f };

    SizeF() = default;
    SizeF(float w, float h) : Width(w), Height(h) {}
};

struct Rect {
    int32_t X{ 0 };
    int32_t Y{ 0 };
    int32_t Width{ 0 };
    int32_t Height{ 0 };

    Rect() = default;
    Rect(int32_t x, int32_t y, int32_t w, int32_t h)
        : X(x), Y(y), Width(w), Height(h) {}

    int32_t GetLeft() const { return X; }
    int32_t GetTop() const { return Y; }
    int32_t GetRight() const { return X + Width; }
    int32_t GetBottom() const { return Y + Height; }
    bool IsEmptyArea() const { return (Width <= 0 || Height <= 0); }
};

struct RectF {
    float X{ 0.0f };
    float Y{ 0.0f };
    float Width{ 0.0f };
    float Height{ 0.0f };

    RectF() = default;
    RectF(float x, float y, float w, float h)
        : X(x), Y(y), Width(w), Height(h) {}

    float GetLeft() const { return X; }
    float GetTop() const { return Y; }
    float GetRight() const { return X + Width; }
    float GetBottom() const { return Y + Height; }
    bool IsEmptyArea() const { return (Width <= 0.0f || Height <= 0.0f); }
};

class Color {
private:
    ARGB m_value{ 0xFF000000 };

public:
    Color() = default;
    explicit Color(ARGB val) : m_value(val) {}
    Color(uint8_t r, uint8_t g, uint8_t b)
        : m_value(MakeARGB(255, r, g, b)) {}
    Color(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
        : m_value(MakeARGB(a, r, g, b)) {}

    static ARGB MakeARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(b);
    }

    uint8_t GetA() const { return static_cast<uint8_t>((m_value >> 24) & 0xFF); }
    uint8_t GetR() const { return static_cast<uint8_t>((m_value >> 16) & 0xFF); }
    uint8_t GetG() const { return static_cast<uint8_t>((m_value >> 8) & 0xFF); }
    uint8_t GetB() const { return static_cast<uint8_t>(m_value & 0xFF); }
    ARGB GetValue() const { return m_value; }
    void SetValue(ARGB val) { m_value = val; }

    uint32_t ToCOLORREF() const {
        return (static_cast<uint32_t>(GetR())) |
               (static_cast<uint32_t>(GetG()) << 8) |
               (static_cast<uint32_t>(GetB()) << 16);
    }

    static Color Black()       { return Color(0xFF000000); }
    static Color White()       { return Color(0xFFFFFFFF); }
    static Color Red()         { return Color(0xFFFF0000); }
    static Color Green()       { return Color(0xFF00FF00); }
    static Color Blue()        { return Color(0xFF0000FF); }
    static Color Yellow()      { return Color(0xFFFFFF00); }
    static Color Cyan()        { return Color(0xFF00FFFF); }
    static Color Magenta()     { return Color(0xFFFF00FF); }
    static Color Transparent() { return Color(0x00000000); }
};

// ============================================================================
// 3. GDI+ Startup & Lifecycle
// ============================================================================

enum DebugEventLevel {
    DebugEventLevelFatal    = 0,
    DebugEventLevelWarning  = 1
};

using DebugEventProc = void (__stdcall *)(DebugEventLevel level, const char* message);

struct GdiplusStartupInput {
    uint32_t GdiplusVersion{ 1 };
    DebugEventProc DebugEventCallback{ nullptr };
    int32_t SuppressBackgroundThread{ 0 };
    int32_t SuppressExternalCodecs{ 0 };

    GdiplusStartupInput(DebugEventProc debugCallback = nullptr,
                        int32_t suppressBackgroundThread = 0,
                        int32_t suppressExternalCodecs = 0)
        : DebugEventCallback(debugCallback),
          SuppressBackgroundThread(suppressBackgroundThread),
          SuppressExternalCodecs(suppressExternalCodecs) {}
};

struct GdiplusStartupOutput {
    void* NotificationHook{ nullptr };
    void* NotificationUnhook{ nullptr };
};

inline std::atomic<uint64_t> g_gdiplusTokenCounter{ 100 };
inline std::atomic<bool>     g_gdiplusInitialized{ false };

inline Status __stdcall GdiplusStartup(uintptr_t* token, const GdiplusStartupInput* input, GdiplusStartupOutput* output) {
    if (!token) return InvalidParameter;
    (void)input;
    if (output) {
        output->NotificationHook = nullptr;
        output->NotificationUnhook = nullptr;
    }
    *token = g_gdiplusTokenCounter.fetch_add(1);
    g_gdiplusInitialized.store(true);
    return Ok;
}

inline void __stdcall GdiplusShutdown(uintptr_t token) {
    (void)token;
    g_gdiplusInitialized.store(false);
}

// ============================================================================
// 4. Matrix & Affine Transformations (GpMatrix / Matrix)
// ============================================================================

class Matrix {
public:
    float m[6]{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }; // m11, m12, m21, m22, dx, dy

    Matrix() = default;
    Matrix(float m11, float m12, float m21, float m22, float dx, float dy) {
        m[0] = m11; m[1] = m12;
        m[2] = m21; m[3] = m22;
        m[4] = dx;  m[5] = dy;
    }

    Status GetElements(float* outM) const {
        if (!outM) return InvalidParameter;
        std::memcpy(outM, m, sizeof(m));
        return Ok;
    }

    Status SetElements(float m11, float m12, float m21, float m22, float dx, float dy) {
        m[0] = m11; m[1] = m12;
        m[2] = m21; m[3] = m22;
        m[4] = dx;  m[5] = dy;
        return Ok;
    }

    bool IsIdentity() const {
        return (m[0] == 1.0f && m[1] == 0.0f &&
                m[2] == 0.0f && m[3] == 1.0f &&
                m[4] == 0.0f && m[5] == 0.0f);
    }

    Status Reset() {
        m[0] = 1.0f; m[1] = 0.0f;
        m[2] = 0.0f; m[3] = 1.0f;
        m[4] = 0.0f; m[5] = 0.0f;
        return Ok;
    }

    void reset() noexcept { Reset(); }

    Status Translate(float offsetX, float offsetY, MatrixOrder order = MatrixOrderPrepend) {
        translate(offsetX, offsetY, order);
        return Ok;
    }

    void translate(float dx, float dy, MatrixOrder order = MatrixOrderPrepend) noexcept {
        if (order == MatrixOrderPrepend) {
            m[4] += dx * m[0] + dy * m[2];
            m[5] += dx * m[1] + dy * m[3];
        } else {
            m[4] += dx;
            m[5] += dy;
        }
    }

    Status Scale(float scaleX, float scaleY, MatrixOrder order = MatrixOrderPrepend) {
        scale(scaleX, scaleY, order);
        return Ok;
    }

    void scale(float sx, float sy, MatrixOrder order = MatrixOrderPrepend) noexcept {
        m[0] *= sx; m[1] *= sx;
        m[2] *= sy; m[3] *= sy;
    }

    Status Rotate(float angleDeg, MatrixOrder order = MatrixOrderPrepend) {
        rotate(angleDeg, order);
        return Ok;
    }

    void rotate(float angle, MatrixOrder order = MatrixOrderPrepend) noexcept {
        float rad = angle * 3.14159265358979323846f / 180.0f;
        float c = std::cos(rad);
        float s = std::sin(rad);
        float nm0 = m[0] * c + m[2] * s;
        float nm1 = m[1] * c + m[3] * s;
        float nm2 = m[0] * -s + m[2] * c;
        float nm3 = m[1] * -s + m[3] * c;
        m[0] = nm0; m[1] = nm1;
        m[2] = nm2; m[3] = nm3;
    }

    Status Multiply(const Matrix* other, MatrixOrder order = MatrixOrderPrepend) {
        if (!other) return InvalidParameter;
        const float* o = other->m;
        if (order == MatrixOrderPrepend) {
            float n11 = o[0] * m[0] + o[1] * m[2];
            float n12 = o[0] * m[1] + o[1] * m[3];
            float n21 = o[2] * m[0] + o[3] * m[2];
            float n22 = o[2] * m[1] + o[3] * m[3];
            float ndx = o[4] * m[0] + o[5] * m[2] + m[4];
            float ndy = o[4] * m[1] + o[5] * m[3] + m[5];
            m[0] = n11; m[1] = n12; m[2] = n21; m[3] = n22; m[4] = ndx; m[5] = ndy;
        } else {
            float n11 = m[0] * o[0] + m[1] * o[2];
            float n12 = m[0] * o[1] + m[1] * o[3];
            float n21 = m[2] * o[0] + m[3] * o[2];
            float n22 = m[2] * o[1] + m[3] * o[3];
            float ndx = m[4] * o[0] + m[5] * o[2] + o[4];
            float ndy = m[4] * o[1] + m[5] * o[3] + o[5];
            m[0] = n11; m[1] = n12; m[2] = n21; m[3] = n22; m[4] = ndx; m[5] = ndy;
        }
        return Ok;
    }

    Status Invert() noexcept {
        return invert() ? Ok : GenericError;
    }

    bool invert() noexcept {
        float det = m[0] * m[3] - m[1] * m[2];
        if (std::abs(det) < 1e-6f) return false;
        float invDet = 1.0f / det;
        float nm0 =  m[3] * invDet;
        float nm1 = -m[1] * invDet;
        float nm2 = -m[2] * invDet;
        float nm3 =  m[0] * invDet;
        float ndx = (m[2] * m[5] - m[3] * m[4]) * invDet;
        float ndy = (m[1] * m[4] - m[0] * m[5]) * invDet;
        m[0] = nm0; m[1] = nm1;
        m[2] = nm2; m[3] = nm3;
        m[4] = ndx; m[5] = ndy;
        return true;
    }

    Status TransformPoints(PointF* pts, int32_t count) const {
        if (!pts || count <= 0) return InvalidParameter;
        transform(pts, count);
        return Ok;
    }

    void transform(PointF* pts, int count) const noexcept {
        if (!pts || count <= 0) return;
        for (int i = 0; i < count; ++i) {
            float x = pts[i].X;
            float y = pts[i].Y;
            pts[i].X = x * m[0] + y * m[2] + m[4];
            pts[i].Y = x * m[1] + y * m[3] + m[5];
        }
    }
};

using GpMatrix = Matrix;

// ============================================================================
// 5. Brushes (Brush, SolidBrush, LinearGradientBrush, HatchBrush)
// ============================================================================

class Brush {
public:
    int type{0}; // 0 = solid, 1 = hatch
    ARGB color{0xFF000000};
    ARGB backColor{0xFFFFFFFF};
    int hatchStyle{0};
    BrushType m_type{ BrushTypeSolidColor };

    Brush() = default;
    virtual ~Brush() = default;
    virtual Brush* Clone() const {
        return new Brush(*this);
    }
    BrushType GetType() const { return m_type; }
};

using GpBrush = Brush;

class SolidBrush : public Brush {
private:
    Color m_color{ Color::Black() };

public:
    SolidBrush() {
        m_type = BrushTypeSolidColor;
        type = 0;
        color = 0xFF000000;
    }
    explicit SolidBrush(const Color& c) : m_color(c) {
        m_type = BrushTypeSolidColor;
        type = 0;
        color = c.GetValue();
    }

    Brush* Clone() const override {
        auto* b = new SolidBrush(m_color);
        b->type = type;
        b->color = color;
        b->backColor = backColor;
        b->hatchStyle = hatchStyle;
        return b;
    }
    Status GetColor(Color* c) const { if (!c) return InvalidParameter; *c = m_color; return Ok; }
    Status SetColor(const Color& c) {
        m_color = c;
        color = c.GetValue();
        return Ok;
    }
};

using GpSolidFill = SolidBrush;

class HatchBrush : public Brush {
public:
    HatchBrush(int style, ARGB fore, ARGB back) {
        m_type = BrushTypeHatchFill;
        type = 1;
        hatchStyle = style;
        color = fore;
        backColor = back;
    }
    Brush* Clone() const override {
        auto* b = new HatchBrush(hatchStyle, color, backColor);
        return b;
    }
};

class LinearGradientBrush : public Brush {
private:
    PointF m_p1{ 0.0f, 0.0f };
    PointF m_p2{ 100.0f, 100.0f };
    Color m_c1{ Color::White() };
    Color m_c2{ Color::Black() };
    LinearGradientMode m_mode{ LinearGradientModeHorizontal };

public:
    LinearGradientBrush(const PointF& p1, const PointF& p2, const Color& c1, const Color& c2)
        : m_p1(p1), m_p2(p2), m_c1(c1), m_c2(c2) {
        m_type = BrushTypeLinearGradient;
    }

    LinearGradientBrush(const RectF& rect, const Color& c1, const Color& c2, LinearGradientMode mode)
        : m_p1(rect.X, rect.Y), m_p2(rect.X + rect.Width, rect.Y + rect.Height),
          m_c1(c1), m_c2(c2), m_mode(mode) {
        m_type = BrushTypeLinearGradient;
    }

    Brush* Clone() const override {
        return new LinearGradientBrush(m_p1, m_p2, m_c1, m_c2);
    }

    Status GetLinearColors(Color* colors) const {
        if (!colors) return InvalidParameter;
        colors[0] = m_c1;
        colors[1] = m_c2;
        return Ok;
    }

    Status SetLinearColors(const Color& c1, const Color& c2) {
        m_c1 = c1; m_c2 = c2;
        return Ok;
    }

    LinearGradientMode GetMode() const { return m_mode; }
};

using GpLineGradient = LinearGradientBrush;

// ============================================================================
// 6. Pens (Pen)
// ============================================================================

class Pen {
public:
    Color m_color{ Color::Black() };
    float m_width{ 1.0f };
    DashStyle m_dashStyle{ DashStyleSolid };
    LineCap m_startCap{ LineCapFlat };
    LineCap m_endCap{ LineCapFlat };
    LineJoin m_lineJoin{ LineJoinMiter };
    std::unique_ptr<Brush> m_brush;

    // Flat C API compatibility fields
    Brush brush;
    float width{ 1.0f };
    Unit unit{ UnitPixel };
    DashStyle dashStyle{ DashStyleSolid };

    Pen() : Pen(Color::Black(), 1.0f) {}

    Pen(const Color& color, float w = 1.0f)
        : m_color(color), m_width(std::max(0.0f, w)),
          width(std::max(0.0f, w)) {
        brush.color = color.GetValue();
        brush.type = 0;
        m_brush = std::make_unique<SolidBrush>(color);
    }

    explicit Pen(const Brush* b, float w = 1.0f, Unit u = UnitWorld)
        : m_width(std::max(0.0f, w)), width(std::max(0.0f, w)), unit(u) {
        if (b) {
            m_brush.reset(b->Clone());
            brush = *b;
            m_color = Color(b->color);
        } else {
            m_brush = std::make_unique<SolidBrush>(Color::Black());
        }
    }

    Pen(const Pen& other)
        : m_color(other.m_color), m_width(other.m_width),
          m_dashStyle(other.m_dashStyle), m_startCap(other.m_startCap),
          m_endCap(other.m_endCap), m_lineJoin(other.m_lineJoin),
          brush(other.brush), width(other.width), unit(other.unit), dashStyle(other.dashStyle) {
        if (other.m_brush) m_brush.reset(other.m_brush->Clone());
    }

    Pen& operator=(const Pen& other) {
        if (this != &other) {
            m_color = other.m_color;
            m_width = other.m_width;
            m_dashStyle = other.m_dashStyle;
            m_startCap = other.m_startCap;
            m_endCap = other.m_endCap;
            m_lineJoin = other.m_lineJoin;
            brush = other.brush;
            width = other.width;
            unit = other.unit;
            dashStyle = other.dashStyle;
            if (other.m_brush) m_brush.reset(other.m_brush->Clone());
        }
        return *this;
    }

    Pen* Clone() const { return new Pen(*this); }

    Status GetColor(Color* color) const {
        if (!color) return InvalidParameter;
        *color = m_color;
        return Ok;
    }

    Status SetColor(const Color& color) {
        m_color = color;
        this->brush.color = color.GetValue();
        m_brush = std::make_unique<SolidBrush>(color);
        return Ok;
    }

    float GetWidth() const { return width; }
    Status SetWidth(float w) {
        m_width = std::max(0.0f, w);
        this->width = m_width;
        return Ok;
    }

    DashStyle GetDashStyle() const { return dashStyle; }
    Status SetDashStyle(DashStyle style) {
        m_dashStyle = style;
        this->dashStyle = style;
        return Ok;
    }

    LineCap GetStartCap() const { return m_startCap; }
    LineCap GetEndCap() const { return m_endCap; }
    Status SetLineCap(LineCap startCap, LineCap endCap, LineCap) {
        m_startCap = startCap;
        m_endCap = endCap;
        return Ok;
    }

    LineJoin GetLineJoin() const { return m_lineJoin; }
    Status SetLineJoin(LineJoin join) { m_lineJoin = join; return Ok; }
};

using GpPen = Pen;

class Graphics;

// ============================================================================
// 7. GraphicsPath & Regions (GpPath, GpRegion)
// ============================================================================

enum PathPointType : uint8_t {
    PathPointTypeStart           = 0,
    PathPointTypeLine            = 1,
    PathPointTypeBezier          = 3,
    PathPointTypePathTypeMask    = 0x07,
    PathPointTypeDashMode        = 0x10,
    PathPointTypePathMarker      = 0x20,
    PathPointTypeCloseSubpath    = 0x80
};

class GraphicsPath {
public:
    FillMode fillMode{ FillModeAlternate };
    std::vector<PointF> points;
    std::vector<uint8_t> types;

    GraphicsPath(FillMode mode = FillModeAlternate) : fillMode(mode) {}

    GraphicsPath(const PointF* pts, const uint8_t* inTypes, int32_t count, FillMode mode = FillModeAlternate)
        : fillMode(mode) {
        if (pts && inTypes && count > 0) {
            points.assign(pts, pts + count);
            types.assign(inTypes, inTypes + count);
        }
    }

    GraphicsPath* Clone() const {
        auto* p = new GraphicsPath(fillMode);
        p->points = points;
        p->types = types;
        return p;
    }

    void reset() noexcept {
        points.clear();
        types.clear();
    }

    Status Reset() {
        reset();
        return Ok;
    }

    int32_t GetPointCount() const { return static_cast<int32_t>(points.size()); }
    FillMode GetFillMode() const { return fillMode; }
    Status SetFillMode(FillMode mode) { fillMode = mode; return Ok; }

    Status GetPathPoints(PointF* pts, int32_t count) const {
        if (!pts || count < static_cast<int32_t>(points.size())) return InvalidParameter;
        std::copy(points.begin(), points.end(), pts);
        return Ok;
    }

    Status GetPathTypes(uint8_t* outTypes, int32_t count) const {
        if (!outTypes || count < static_cast<int32_t>(types.size())) return InvalidParameter;
        std::copy(types.begin(), types.end(), outTypes);
        return Ok;
    }

    Status StartFigure() {
        return Ok;
    }

    Status CloseFigure() {
        if (!types.empty()) {
            types.back() |= PathPointTypeCloseSubpath;
        }
        return Ok;
    }

    Status AddLine(float x1, float y1, float x2, float y2) {
        if (points.empty() || (types.back() & PathPointTypeCloseSubpath)) {
            points.emplace_back(x1, y1);
            types.push_back(PathPointTypeStart);
        }
        points.emplace_back(x2, y2);
        types.push_back(PathPointTypeLine);
        return Ok;
    }

    Status AddRectangle(const RectF& rect) {
        StartFigure();
        AddLine(rect.X, rect.Y, rect.X + rect.Width, rect.Y);
        AddLine(rect.X + rect.Width, rect.Y, rect.X + rect.Width, rect.Y + rect.Height);
        AddLine(rect.X + rect.Width, rect.Y + rect.Height, rect.X, rect.Y + rect.Height);
        CloseFigure();
        return Ok;
    }

    Status AddEllipse(float x, float y, float width, float height) {
        const int n = 16;
        float rx = width * 0.5f;
        float ry = height * 0.5f;
        float cx = x + rx;
        float cy = y + ry;
        for (int i = 0; i <= n; ++i) {
            float rad = (i % n) * (2.0f * 3.1415926535f / n);
            float px = cx + rx * std::cos(rad);
            float py = cy + ry * std::sin(rad);
            if (i == 0) {
                points.emplace_back(px, py);
                types.push_back(PathPointTypeStart);
            } else if (i < n) {
                points.emplace_back(px, py);
                types.push_back(PathPointTypeLine);
            }
        }
        CloseFigure();
        return Ok;
    }

    Status AddPolygon(const PointF* pts, int32_t count) {
        if (!pts || count < 3) return InvalidParameter;
        points.emplace_back(pts[0]);
        types.push_back(PathPointTypeStart);
        for (int32_t i = 1; i < count; ++i) {
            points.emplace_back(pts[i]);
            types.push_back(PathPointTypeLine);
        }
        CloseFigure();
        return Ok;
    }

    Status Transform(const Matrix* matrix) {
        if (!matrix) return InvalidParameter;
        return matrix->TransformPoints(points.data(), static_cast<int32_t>(points.size()));
    }
};

using GpPath = GraphicsPath;

class Region {
public:
    RectF bounds{ 0.0f, 0.0f, 1000.0f, 1000.0f };
    RectF m_bounds{ 0, 0, 0, 0 };
    bool  m_isInfinite{ true };

    Region() {
        m_bounds = bounds;
        m_isInfinite = false;
    }
    explicit Region(const RectF& rect) : bounds(rect), m_bounds(rect), m_isInfinite(false) {}

    Region* Clone() const {
        auto* r = new Region(m_bounds);
        r->m_isInfinite = m_isInfinite;
        r->bounds = bounds;
        return r;
    }

    Status MakeInfinite() { m_isInfinite = true; m_bounds = { 0, 0, 0, 0 }; bounds = m_bounds; return Ok; }
    Status MakeEmpty()    { m_isInfinite = false; m_bounds = { 0, 0, 0, 0 }; bounds = m_bounds; return Ok; }
    bool IsInfinite(const Graphics*) const { return m_isInfinite; }
    bool IsEmpty(const Graphics*) const { return (!m_isInfinite && m_bounds.IsEmptyArea()); }

    Status GetBounds(RectF* rect, const Graphics*) const {
        if (!rect) return InvalidParameter;
        *rect = m_bounds;
        return Ok;
    }

    Status Intersect(const RectF& rect) {
        if (m_isInfinite) {
            m_bounds = rect;
            m_isInfinite = false;
        } else {
            float l = std::max(m_bounds.X, rect.X);
            float t = std::max(m_bounds.Y, rect.Y);
            float r = std::min(m_bounds.GetRight(), rect.GetRight());
            float b = std::min(m_bounds.GetBottom(), rect.GetBottom());
            if (r > l && b > t) {
                m_bounds = RectF(l, t, r - l, b - t);
            } else {
                MakeEmpty();
            }
        }
        return Ok;
    }

    Status Union(const RectF& rect) {
        if (m_isInfinite) return Ok;
        if (m_bounds.IsEmptyArea()) {
            m_bounds = rect;
            return Ok;
        }
        float l = std::min(m_bounds.X, rect.X);
        float t = std::min(m_bounds.Y, rect.Y);
        float r = std::max(m_bounds.GetRight(), rect.GetRight());
        float b = std::max(m_bounds.GetBottom(), rect.GetBottom());
        m_bounds = RectF(l, t, r - l, b - t);
        return Ok;
    }
};

using GpRegion = Region;

// ============================================================================
// 8. Image & Bitmap (GpImage, GpBitmap)
// ============================================================================

struct BitmapData {
    uint32_t    Width{ 0 };
    uint32_t    Height{ 0 };
    int32_t     Stride{ 0 };
    PixelFormat PixelFormat{ PixelFormat32bppARGB };
    void*       Scan0{ nullptr };
    uintptr_t   Reserved{ 0 };
};

// Standard GDI+ Image Format GUIDs
inline constexpr GUID ImageFormatUndefined = { 0xb96b3ca9, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatMemoryBMP = { 0xb96b3caa, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatBMP       = { 0xb96b3cab, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatJPEG      = { 0xb96b3cae, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatPNG       = { 0xb96b3caf, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatGIF       = { 0xb96b3cb0, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatTIFF      = { 0xb96b3cb1, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID ImageFormatICO       = { 0xb96b3cb5, 0x0728, 0x11d3, { 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

// Standard GDI+ encoder CLSIDs (BMP, JPEG, and PNG are implemented here).
inline constexpr GUID EncoderClsidBmp  = { 0x557cf400, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID EncoderClsidJpeg = { 0x557cf401, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID EncoderClsidPng  = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
inline constexpr GUID EncoderQuality   = { 0x1d5be4b5, 0xfa4a, 0x452d, { 0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb } };

class Image {
protected:
    ImageType   m_type{ ImageTypeBitmap };
    uint32_t    m_width{ 0 };
    uint32_t    m_height{ 0 };
    PixelFormat m_pixelFormat{ PixelFormat32bppARGB };
    GUID        m_rawFormat{ ImageFormatBMP };

public:
    uint32_t width{ 800 };
    uint32_t height{ 600 };
    PixelFormat format{ PixelFormat32bppARGB };
    float hRes{ 96.0f };
    float vRes{ 96.0f };
    std::vector<uint8_t> pixelBuffer;

    virtual ~Image() = default;
    virtual Image* Clone() const = 0;

    ImageType GetType() const { return m_type; }
    uint32_t GetWidth() const { return width != 0 ? width : m_width; }
    uint32_t GetHeight() const { return height != 0 ? height : m_height; }
    PixelFormat GetPixelFormat() const { return format; }
    Status GetRawFormat(GUID* formatParam) const {
        if (!formatParam) return InvalidParameter;
        *formatParam = m_rawFormat;
        return Ok;
    }
};

using GpImage = Image;

class Bitmap : public Image {
private:
    std::vector<uint32_t> m_pixels; // 32-bpp ARGB buffer
    int32_t m_stride{ 0 };
    bool m_isLocked{ false };

public:
    Bitmap(uint32_t w = 800, uint32_t h = 600, PixelFormat fmt = PixelFormat32bppARGB) {
        m_type = ImageTypeBitmap;
        m_width = (w > 0) ? w : 1;
        m_height = (h > 0) ? h : 1;
        m_pixelFormat = fmt;
        width = m_width;
        height = m_height;
        format = m_pixelFormat;
        m_stride = width * 4;
        m_pixels.resize(width * height, 0x00000000);
        pixelBuffer.resize(width * height * 4, 0xFF);
        m_rawFormat = ImageFormatBMP;
    }

    Bitmap(uint32_t w, uint32_t h, int32_t stride, PixelFormat fmt, uint8_t* scan0) {
        m_type = ImageTypeBitmap;
        m_width = (w > 0) ? w : 1;
        m_height = (h > 0) ? h : 1;
        m_pixelFormat = fmt;
        width = m_width;
        height = m_height;
        format = m_pixelFormat;
        m_stride = (stride != 0) ? stride : static_cast<int32_t>(width * 4);
        m_pixels.resize(width * height);
        pixelBuffer.resize(width * height * 4, 0xFF);
        if (scan0) {
            std::memcpy(m_pixels.data(), scan0, width * height * 4);
            std::memcpy(pixelBuffer.data(), scan0, width * height * 4);
        } else {
            std::fill(m_pixels.begin(), m_pixels.end(), 0x00000000);
        }
        m_rawFormat = ImageFormatBMP;
    }

    Image* Clone() const override {
        auto* bmp = new Bitmap(m_width, m_height, m_pixelFormat);
        bmp->m_pixels = m_pixels;
        bmp->pixelBuffer = pixelBuffer;
        bmp->hRes = hRes;
        bmp->vRes = vRes;
        return bmp;
    }

    Status GetPixel(int32_t x, int32_t y, Color* color) const {
        if (!color || x < 0 || y < 0 || static_cast<uint32_t>(x) >= m_width || static_cast<uint32_t>(y) >= m_height) {
            return InvalidParameter;
        }
        *color = Color(m_pixels[y * m_width + x]);
        return Ok;
    }

    Status SetPixel(int32_t x, int32_t y, const Color& color) {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= m_width || static_cast<uint32_t>(y) >= m_height) {
            return InvalidParameter;
        }
        m_pixels[y * m_width + x] = color.GetValue();
        return Ok;
    }

    Status LockBits(const Rect* rect, uint32_t flags, PixelFormat format, BitmapData* lockedBitmapData) {
        (void)flags; (void)format;
        if (!lockedBitmapData) return InvalidParameter;
        if (m_isLocked) return WrongState;
        m_isLocked = true;

        lockedBitmapData->Width = rect ? rect->Width : m_width;
        lockedBitmapData->Height = rect ? rect->Height : m_height;
        lockedBitmapData->Stride = m_stride;
        lockedBitmapData->PixelFormat = m_pixelFormat;
        lockedBitmapData->Scan0 = m_pixels.data();
        return Ok;
    }

    Status UnlockBits(BitmapData* lockedBitmapData) {
        if (!lockedBitmapData || !m_isLocked) return WrongState;
        m_isLocked = false;
        return Ok;
    }

    uint32_t* GetPixelBuffer() { return m_pixels.data(); }
    const uint32_t* GetPixelBuffer() const { return m_pixels.data(); }
    void SetRawFormat(const GUID& value) noexcept { m_rawFormat = value; }
};

using GpBitmap = Bitmap;

// ============================================================================
// 9. Graphics Drawing Surface (GpGraphics / Graphics)
// ============================================================================

class Graphics {
public:
    void*           hdc{ nullptr };
    Matrix          transform;
    SmoothingMode   smoothing{ SmoothingModeDefault };
    InterpolationMode interpolation{ InterpolationModeDefault };
    CompositingQuality compositingQuality{ CompositingQualityDefault };
    CompositingMode compositingMode{ CompositingModeSourceOver };
    int             textRenderingHint{ 0 };
    Unit            pageUnit{ UnitPixel };
    Region          clip;
    Image*          targetImage{ nullptr };

private:
    gdi32::HDC      m_hdc{ nullptr };
    Bitmap*         m_targetBitmap{ nullptr };
    bool            m_ownsTargetBitmap{ false };
    SmoothingMode   m_smoothingMode{ SmoothingModeDefault };
    CompositingMode m_compositingMode{ CompositingModeSourceOver };
    Matrix          m_transform;
    Region          m_clip;

    void SetPixelInternal(int32_t x, int32_t y, const Color& color) {
        if (m_targetBitmap) {
            m_targetBitmap->SetPixel(x, y, color);
        } else if (m_hdc) {
            gdi32::SetPixel(m_hdc, x, y, color.ToCOLORREF());
        }
    }

public:
    Graphics() = default;

    explicit Graphics(gdi32::HDC hdcParam) : hdc(hdcParam), m_hdc(hdcParam) {}

    explicit Graphics(Image* image) : targetImage(image) {
        if (image && image->GetType() == ImageTypeBitmap) {
            m_targetBitmap = static_cast<Bitmap*>(image);
        }
    }

    ~Graphics() {
        if (m_ownsTargetBitmap && m_targetBitmap) delete m_targetBitmap;
    }

    static Graphics* FromHDC(gdi32::HDC hdc) { return new Graphics(hdc); }
    static Graphics* FromImage(Image* image) { return new Graphics(image); }

    SmoothingMode GetSmoothingMode() const { return smoothing; }
    Status SetSmoothingMode(SmoothingMode mode) {
        m_smoothingMode = mode;
        smoothing = mode;
        return Ok;
    }

    InterpolationMode GetInterpolationMode() const { return interpolation; }
    Status SetInterpolationMode(InterpolationMode mode) {
        interpolation = mode;
        return Ok;
    }

    CompositingQuality GetCompositingQuality() const { return compositingQuality; }
    Status SetCompositingQuality(CompositingQuality quality) {
        compositingQuality = quality;
        return Ok;
    }

    CompositingMode GetCompositingMode() const { return compositingMode; }
    Status SetCompositingMode(CompositingMode mode) {
        m_compositingMode = mode;
        compositingMode = mode;
        return Ok;
    }

    int GetTextRenderingHint() const { return textRenderingHint; }
    Status SetTextRenderingHint(int hint) {
        textRenderingHint = hint;
        return Ok;
    }

    Unit GetPageUnit() const { return pageUnit; }
    Status SetPageUnit(Unit unit) {
        pageUnit = unit;
        return Ok;
    }

    Status GetTransform(Matrix* matrix) const {
        if (!matrix) return InvalidParameter;
        *matrix = transform;
        return Ok;
    }

    Status SetTransform(const Matrix* matrix) {
        if (!matrix) return InvalidParameter;
        transform = *matrix;
        m_transform = *matrix;
        return Ok;
    }

    Status ResetTransform() {
        transform.reset();
        return m_transform.Reset();
    }

    Status Clear(const Color& color) {
        if (m_targetBitmap) {
            uint32_t* buf = m_targetBitmap->GetPixelBuffer();
            size_t count = m_targetBitmap->GetWidth() * m_targetBitmap->GetHeight();
            std::fill(buf, buf + count, color.GetValue());
            return Ok;
        }
        if (m_hdc) {
            gdi32::RECT r{ 0, 0, 1920, 1080 };
            gdi32::HBRUSH hBr = gdi32::CreateSolidBrush(color.ToCOLORREF());
            gdi32::FillRect(m_hdc, &r, hBr);
            gdi32::DeleteObject(hBr);
            return Ok;
        }
        return Ok;
    }

    Status DrawLine(const Pen* pen, float x1, float y1, float x2, float y2) {
        if (!pen) return InvalidParameter;
        Color c;
        pen->GetColor(&c);

        // Apply Transform
        PointF pts[2] = { { x1, y1 }, { x2, y2 } };
        m_transform.TransformPoints(pts, 2);

        int32_t ix1 = static_cast<int32_t>(std::round(pts[0].X));
        int32_t iy1 = static_cast<int32_t>(std::round(pts[0].Y));
        int32_t ix2 = static_cast<int32_t>(std::round(pts[1].X));
        int32_t iy2 = static_cast<int32_t>(std::round(pts[1].Y));

        // Bresenham's line algorithm
        int32_t dx = std::abs(ix2 - ix1);
        int32_t dy = std::abs(iy2 - iy1);
        int32_t sx = (ix1 < ix2) ? 1 : -1;
        int32_t sy = (iy1 < iy2) ? 1 : -1;
        int32_t err = dx - dy;

        while (true) {
            SetPixelInternal(ix1, iy1, c);
            if (ix1 == ix2 && iy1 == iy2) break;
            int32_t e2 = 2 * err;
            if (e2 > -dy) { err -= dy; ix1 += sx; }
            if (e2 < dx)  { err += dx; iy1 += sy; }
        }
        return Ok;
    }

    Status DrawRectangle(const Pen* pen, float x, float y, float width, float height) {
        DrawLine(pen, x, y, x + width, y);
        DrawLine(pen, x + width, y, x + width, y + height);
        DrawLine(pen, x + width, y + height, x, y + height);
        DrawLine(pen, x, y + height, x, y);
        return Ok;
    }

    Status FillRectangle(const Brush* brush, float x, float y, float width, float height) {
        if (!brush) return InvalidParameter;
        Color c = Color::Black();
        if (brush->GetType() == BrushTypeSolidColor) {
            static_cast<const SolidBrush*>(brush)->GetColor(&c);
        } else if (brush->GetType() == BrushTypeLinearGradient) {
            Color clrs[2];
            static_cast<const LinearGradientBrush*>(brush)->GetLinearColors(clrs);
            c = clrs[0];
        }

        int32_t ix = static_cast<int32_t>(x);
        int32_t iy = static_cast<int32_t>(y);
        int32_t iw = static_cast<int32_t>(width);
        int32_t ih = static_cast<int32_t>(height);

        for (int32_t py = iy; py < iy + ih; ++py) {
            for (int32_t px = ix; px < ix + iw; ++px) {
                SetPixelInternal(px, py, c);
            }
        }
        return Ok;
    }

    Status DrawEllipse(const Pen* pen, float x, float y, float width, float height) {
        if (!pen) return InvalidParameter;
        GraphicsPath path;
        path.AddEllipse(x, y, width, height);
        return DrawPath(pen, &path);
    }

    Status FillEllipse(const Brush* brush, float x, float y, float width, float height) {
        if (!brush) return InvalidParameter;
        Color c = Color::Black();
        if (brush->GetType() == BrushTypeSolidColor) {
            static_cast<const SolidBrush*>(brush)->GetColor(&c);
        }

        float rx = width * 0.5f;
        float ry = height * 0.5f;
        float cx = x + rx;
        float cy = y + ry;

        int32_t minX = static_cast<int32_t>(std::floor(x));
        int32_t maxX = static_cast<int32_t>(std::ceil(x + width));
        int32_t minY = static_cast<int32_t>(std::floor(y));
        int32_t maxY = static_cast<int32_t>(std::ceil(y + height));

        for (int32_t py = minY; py <= maxY; ++py) {
            for (int32_t px = minX; px <= maxX; ++px) {
                float dx = (px - cx) / rx;
                float dy = (py - cy) / ry;
                if (dx * dx + dy * dy <= 1.0f) {
                    SetPixelInternal(px, py, c);
                }
            }
        }
        return Ok;
    }

    Status DrawPath(const Pen* pen, const GraphicsPath* path) {
        if (!pen || !path) return InvalidParameter;
        int32_t count = path->GetPointCount();
        if (count < 2) return Ok;

        std::vector<PointF> pts(count);
        std::vector<uint8_t> types(count);
        path->GetPathPoints(pts.data(), count);
        path->GetPathTypes(types.data(), count);

        for (int32_t i = 1; i < count; ++i) {
            if ((types[i] & PathPointTypePathTypeMask) == PathPointTypeLine) {
                DrawLine(pen, pts[i - 1].X, pts[i - 1].Y, pts[i].X, pts[i].Y);
            }
        }
        return Ok;
    }

    Status DrawImage(Image* image, float x, float y) {
        if (!image) return InvalidParameter;
        if (image->GetType() == ImageTypeBitmap) {
            auto* bmp = static_cast<Bitmap*>(image);
            uint32_t w = bmp->GetWidth();
            uint32_t h = bmp->GetHeight();
            for (uint32_t py = 0; py < h; ++py) {
                for (uint32_t px = 0; px < w; ++px) {
                    Color c;
                    bmp->GetPixel(px, py, &c);
                    SetPixelInternal(static_cast<int32_t>(x + px), static_cast<int32_t>(y + py), c);
                }
            }
        }
        return Ok;
    }
};

using GpGraphics = Graphics;

// ----------------------------------------------------------------------------
// GDI+ Image Attributes, Fonts & String Formatting Types
// ----------------------------------------------------------------------------

struct ImageCodecInfo {
    uint8_t Clsid[16]{};
    uint8_t FormatID[16]{};
    const wchar_t* CodecName{ L"Built-in Software Codec" };
    const wchar_t* DllName{ nullptr };
    const wchar_t* FormatDescription{ L"PNG/JPEG/BMP/TIFF" };
    const wchar_t* FilenameExtension{ L"*.PNG;*.JPG;*.BMP" };
    const wchar_t* MimeType{ L"image/png" };
    uint32_t Flags{ 0 };
    uint32_t Version{ 1 };
    uint32_t SigCount{ 0 };
    uint32_t SigSize{ 0 };
    const uint8_t* SigPattern{ nullptr };
    const uint8_t* SigMask{ nullptr };
};

struct GpImageAttributes {
    int wrapMode{ 0 };
    ARGB color{ 0 };
};
using ImageAttributes = GpImageAttributes;

struct GpFontFamily {
    std::wstring name{ L"Segoe UI" };
};
using FontFamily = GpFontFamily;

struct GpFont {
    GpFontFamily family;
    float emSize{ 10.0f };
    int style{ 0 };
    Unit unit{ UnitPoint };
};
using Font = GpFont;

struct CharacterRange {
    int32_t First{ 0 };
    int32_t Length{ 0 };
};

struct GpStringFormat {
    int flags{ 0 };
    int align{ 0 };
    int lineAlign{ 0 };
    int trimming{ 0 };
    std::vector<CharacterRange> measurableRanges;
};
using StringFormat = GpStringFormat;

// ============================================================================
// 10. Windows Imaging Component (WIC) Foundation (windowscodecs.dll)
// ============================================================================

// WIC CLSIDs & IIDs
inline constexpr GUID CLSID_WICImagingFactory =
    { 0x317D06E8, 0x5F24, 0x433D, { 0xBD, 0xF7, 0x79, 0xCE, 0x68, 0xD8, 0xAB, 0xC2 } };

inline constexpr GUID IID_IWICImagingFactory =
    { 0xEC5EC888, 0xC19E, 0x4CF0, { 0xB3, 0x90, 0xDA, 0x60, 0x27, 0x93, 0x60, 0xB3 } };

inline constexpr GUID IID_IWICBitmapSource =
    { 0x00000120, 0xA8F2, 0x4877, { 0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94 } };

inline constexpr GUID IID_IWICBitmap =
    { 0x00000121, 0xA8F2, 0x4877, { 0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94 } };

inline constexpr GUID IID_IWICBitmapDecoder =
    { 0x9EDDE9E7, 0x8DEE, 0x47EA, { 0x99, 0xDF, 0xE6, 0xFA, 0xF2, 0xED, 0x44, 0xBF } };

inline constexpr GUID IID_IWICBitmapEncoder =
    { 0x00000103, 0xA8F2, 0x4877, { 0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94 } };

inline constexpr GUID IID_IWICFormatConverter =
    { 0x00000301, 0xA8F2, 0x4877, { 0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94 } };

// WIC Pixel Formats
inline constexpr GUID GUID_WICPixelFormat32bppPBGRA =
    { 0x6FDDC324, 0x4E03, 0x4BFE, { 0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x10 } };

inline constexpr GUID GUID_WICPixelFormat32bppRGBA =
    { 0xF5C7257D, 0x95D4, 0x498E, { 0xAB, 0x0E, 0x63, 0x4B, 0x77, 0x04, 0xBB, 0x11 } };

inline constexpr GUID GUID_WICPixelFormat24bppBGR =
    { 0x6FDDC324, 0x4E03, 0x4BFE, { 0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x0C } };

inline constexpr GUID GUID_WICPixelFormat8bppGray =
    { 0x6FDDC324, 0x4E03, 0x4BFE, { 0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x08 } };

struct IWICBitmapSource : public ole32::IUnknown {
    virtual int32_t __stdcall GetSize(uint32_t* puiWidth, uint32_t* puiHeight) = 0;
    virtual int32_t __stdcall GetPixelFormat(GUID* pPixelFormat) = 0;
    virtual int32_t __stdcall GetResolution(double* pDpiX, double* pDpiY) = 0;
    virtual int32_t __stdcall CopyPalette(void* pIPalette) = 0;
    virtual int32_t __stdcall CopyPixels(const void* prc, uint32_t cbStride, uint32_t cbBufferSize, uint8_t* pbBuffer) = 0;
};

struct IWICBitmap : public IWICBitmapSource {
    virtual int32_t __stdcall Lock(const void* prcLock, uint32_t flags, void** ppILock) = 0;
    virtual int32_t __stdcall SetPalette(void* pIPalette) = 0;
    virtual int32_t __stdcall SetResolution(double dpiX, double dpiY) = 0;
};

struct IWICFormatConverter : public IWICBitmapSource {
    virtual int32_t __stdcall Initialize(IWICBitmapSource* pISource, const GUID& dstFormat, int32_t dither, void* pIPalette, double alphaThresholdPercent, int32_t paletteTranslate) = 0;
    virtual int32_t __stdcall CanConvert(const GUID& srcPixelFormat, const GUID& dstPixelFormat, int32_t* pfCanConvert) = 0;
};

struct IWICBitmapDecoder : public ole32::IUnknown {
    virtual int32_t __stdcall QueryCapability(void* pIStream, uint32_t* pdwCapability) = 0;
    virtual int32_t __stdcall Initialize(void* pIStream, uint32_t cacheOptions) = 0;
    virtual int32_t __stdcall GetContainerFormat(GUID* pguidContainerFormat) = 0;
    virtual int32_t __stdcall GetDecoderInfo(void** ppIDecoderInfo) = 0;
    virtual int32_t __stdcall CopyPalette(void* pIPalette) = 0;
    virtual int32_t __stdcall GetMetadataQueryReader(void** ppIMetadataQueryReader) = 0;
    virtual int32_t __stdcall GetPreview(IWICBitmapSource** ppIBitmapSource) = 0;
    virtual int32_t __stdcall GetColorContexts(uint32_t cCount, void** ppIColorContexts, uint32_t* pcActualCount) = 0;
    virtual int32_t __stdcall GetThumbnail(IWICBitmapSource** ppIThumbnail) = 0;
    virtual int32_t __stdcall GetFrameCount(uint32_t* pCount) = 0;
    virtual int32_t __stdcall GetFrame(uint32_t index, void** ppIBitmapFrame) = 0;
};

struct IWICBitmapEncoder : public ole32::IUnknown {
    virtual int32_t __stdcall Initialize(void* pIStream, uint32_t cacheOption) = 0;
    virtual int32_t __stdcall GetContainerFormat(GUID* pguidContainerFormat) = 0;
    virtual int32_t __stdcall GetEncoderInfo(void** ppIEncoderInfo) = 0;
    virtual int32_t __stdcall SetColorContexts(uint32_t cCount, void** ppIColorContext) = 0;
    virtual int32_t __stdcall SetPalette(void* pIPalette) = 0;
    virtual int32_t __stdcall SetThumbnail(IWICBitmapSource* pIThumbnail) = 0;
    virtual int32_t __stdcall SetPreview(IWICBitmapSource* pIPreview) = 0;
    virtual int32_t __stdcall CreateNewFrame(void** ppIFrameEncode, void** ppIPropertyBag) = 0;
    virtual int32_t __stdcall Commit() = 0;
    virtual int32_t __stdcall GetMetadataQueryWriter(void** ppIMetadataQueryWriter) = 0;
};

struct IWICImagingFactory : public ole32::IUnknown {
    virtual int32_t __stdcall CreateDecoderFromFilename(const wchar_t* wzFilename, const GUID* pguidVendor, uint32_t dwDesiredAccess, uint32_t metadataOptions, IWICBitmapDecoder** ppIDecoder) = 0;
    virtual int32_t __stdcall CreateDecoderFromStream(void* pIStream, const GUID* pguidVendor, uint32_t metadataOptions, IWICBitmapDecoder** ppIDecoder) = 0;
    virtual int32_t __stdcall CreateDecoderFromFileHandle(uintptr_t hFile, const GUID* pguidVendor, uint32_t metadataOptions, IWICBitmapDecoder** ppIDecoder) = 0;
    virtual int32_t __stdcall CreateComponentInfo(const GUID& clsidComponent, void** ppIInfo) = 0;
    virtual int32_t __stdcall CreateDecoder(const GUID& guidContainerFormat, const GUID* pguidVendor, IWICBitmapDecoder** ppIDecoder) = 0;
    virtual int32_t __stdcall CreateEncoder(const GUID& guidContainerFormat, const GUID* pguidVendor, IWICBitmapEncoder** ppIEncoder) = 0;
    virtual int32_t __stdcall CreatePalette(void** ppIPalette) = 0;
    virtual int32_t __stdcall CreateFormatConverter(IWICFormatConverter** ppIFormatConverter) = 0;
    virtual int32_t __stdcall CreateBitmapScaler(void** ppIBitmapScaler) = 0;
    virtual int32_t __stdcall CreateBitmapClipper(void** ppIBitmapClipper) = 0;
    virtual int32_t __stdcall CreateBitmapFlipRotator(void** ppIBitmapFlipRotator) = 0;
    virtual int32_t __stdcall CreateStream(void** ppIWICStream) = 0;
    virtual int32_t __stdcall CreateColorContext(void** ppIColorContext) = 0;
    virtual int32_t __stdcall CreateColorTransformer(void** ppIColorTransform) = 0;
    virtual int32_t __stdcall CreateBitmap(uint32_t uiWidth, uint32_t uiHeight, const GUID& pixelFormat, uint32_t option, IWICBitmap** ppIBitmap) = 0;
    virtual int32_t __stdcall CreateBitmapFromSource(IWICBitmapSource* pIBitmapSource, uint32_t option, IWICBitmap** ppIBitmap) = 0;
    virtual int32_t __stdcall CreateBitmapFromSourceRect(IWICBitmapSource* pIBitmapSource, uint32_t x, uint32_t y, uint32_t width, uint32_t height, IWICBitmap** ppIBitmap) = 0;
    virtual int32_t __stdcall CreateBitmapFromMemory(uint32_t uiWidth, uint32_t uiHeight, const GUID& pixelFormat, uint32_t cbStride, uint32_t cbBufferSize, uint8_t* pbBuffer, IWICBitmap** ppIBitmap) = 0;
    virtual int32_t __stdcall CreateBitmapFromHBITMAP(void* hBitmap, void* hPalette, int32_t options, IWICBitmap** ppIBitmap) = 0;
    virtual int32_t __stdcall CreateBitmapFromHICON(void* hIcon, IWICBitmap** ppIBitmap) = 0;
    virtual int32_t __stdcall CreateComponentEnumerator(uint32_t componentTypes, uint32_t options, void** ppIEnumUnknown) = 0;
    virtual int32_t __stdcall CreateFastMetadataEncoderFromDecoder(IWICBitmapDecoder* pIDecoder, void** ppIFastEncoder) = 0;
    virtual int32_t __stdcall CreateFastMetadataEncoderFromFrameDecode(void* pIFrameDecoder, void** ppIFastEncoder) = 0;
    virtual int32_t __stdcall CreateQueryWriter(const GUID& guidMetadataFormat, const GUID* pguidVendor, void** ppIQueryWriter) = 0;
    virtual int32_t __stdcall CreateQueryWriterFromReader(void* pIQueryReader, const GUID* pguidVendor, void** ppIQueryWriter) = 0;
};

// ============================================================================
// 11. Concrete WIC Implementation (CWICBitmap, CWICImagingFactory)
// ============================================================================

class CWICBitmap : public IWICBitmap {
private:
    std::atomic<uint32_t> m_refCount{ 1 };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    GUID m_format{ GUID_WICPixelFormat32bppPBGRA };
    std::vector<uint8_t> m_buffer;
    uint32_t m_stride{ 0 };
    double m_dpiX{ 96.0 };
    double m_dpiY{ 96.0 };
    mutable std::mutex m_mutex;

public:
    CWICBitmap(uint32_t width, uint32_t height, const GUID& format, uint32_t stride = 0, const uint8_t* buffer = nullptr)
        : m_width(width), m_height(height), m_format(format) {
        m_stride = (stride != 0) ? stride : width * 4;
        m_buffer.resize(m_stride * height);
        if (buffer) {
            std::memcpy(m_buffer.data(), buffer, m_buffer.size());
        } else {
            std::fill(m_buffer.begin(), m_buffer.end(), 0);
        }
    }

    int32_t __stdcall QueryInterface(const GUID& riid, void** ppvObject) override {
        if (!ppvObject) return ole32::E_POINTER;
        if (riid == ole32::IID_IUnknown || riid == IID_IWICBitmapSource || riid == IID_IWICBitmap) {
            *ppvObject = static_cast<IWICBitmap*>(this);
            AddRef();
            return ole32::S_OK;
        }
        *ppvObject = nullptr;
        return ole32::E_NOINTERFACE;
    }

    uint32_t __stdcall AddRef() override { return m_refCount.fetch_add(1) + 1; }
    uint32_t __stdcall Release() override {
        uint32_t r = m_refCount.fetch_sub(1) - 1;
        if (r == 0) delete this;
        return r;
    }

    // IWICBitmapSource
    int32_t __stdcall GetSize(uint32_t* puiWidth, uint32_t* puiHeight) override {
        if (!puiWidth || !puiHeight) return ole32::E_POINTER;
        *puiWidth = m_width;
        *puiHeight = m_height;
        return ole32::S_OK;
    }

    int32_t __stdcall GetPixelFormat(GUID* pPixelFormat) override {
        if (!pPixelFormat) return ole32::E_POINTER;
        *pPixelFormat = m_format;
        return ole32::S_OK;
    }

    int32_t __stdcall GetResolution(double* pDpiX, double* pDpiY) override {
        if (!pDpiX || !pDpiY) return ole32::E_POINTER;
        *pDpiX = m_dpiX;
        *pDpiY = m_dpiY;
        return ole32::S_OK;
    }

    int32_t __stdcall CopyPalette(void*) override { return ole32::E_NOTIMPL; }

    int32_t __stdcall CopyPixels(const void* prc, uint32_t cbStride, uint32_t cbBufferSize, uint8_t* pbBuffer) override {
        (void)prc;
        if (!pbBuffer) return ole32::E_POINTER;
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t toCopy = std::min<uint32_t>(cbBufferSize, static_cast<uint32_t>(m_buffer.size()));
        (void)cbStride;
        std::memcpy(pbBuffer, m_buffer.data(), toCopy);
        return ole32::S_OK;
    }

    // IWICBitmap
    int32_t __stdcall Lock(const void*, uint32_t, void** ppILock) override {
        if (ppILock) *ppILock = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall SetPalette(void*) override { return ole32::S_OK; }

    int32_t __stdcall SetResolution(double dpiX, double dpiY) override {
        m_dpiX = dpiX;
        m_dpiY = dpiY;
        return ole32::S_OK;
    }
};

class CWICImagingFactory : public IWICImagingFactory {
private:
    std::atomic<uint32_t> m_refCount{ 1 };

public:
    CWICImagingFactory() = default;

    int32_t __stdcall QueryInterface(const GUID& riid, void** ppvObject) override {
        if (!ppvObject) return ole32::E_POINTER;
        if (riid == ole32::IID_IUnknown || riid == IID_IWICImagingFactory) {
            *ppvObject = static_cast<IWICImagingFactory*>(this);
            AddRef();
            return ole32::S_OK;
        }
        *ppvObject = nullptr;
        return ole32::E_NOINTERFACE;
    }

    uint32_t __stdcall AddRef() override { return m_refCount.fetch_add(1) + 1; }
    uint32_t __stdcall Release() override {
        uint32_t r = m_refCount.fetch_sub(1) - 1;
        if (r == 0) delete this;
        return r;
    }

    int32_t __stdcall CreateDecoderFromFilename(const wchar_t*, const GUID*, uint32_t, uint32_t, IWICBitmapDecoder** ppIDecoder) override {
        if (ppIDecoder) *ppIDecoder = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateDecoderFromStream(void*, const GUID*, uint32_t, IWICBitmapDecoder** ppIDecoder) override {
        if (ppIDecoder) *ppIDecoder = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateDecoderFromFileHandle(uintptr_t, const GUID*, uint32_t, IWICBitmapDecoder** ppIDecoder) override {
        if (ppIDecoder) *ppIDecoder = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateComponentInfo(const GUID&, void** ppIInfo) override {
        if (ppIInfo) *ppIInfo = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateDecoder(const GUID&, const GUID*, IWICBitmapDecoder** ppIDecoder) override {
        if (ppIDecoder) *ppIDecoder = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateEncoder(const GUID&, const GUID*, IWICBitmapEncoder** ppIEncoder) override {
        if (ppIEncoder) *ppIEncoder = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreatePalette(void** ppIPalette) override {
        if (ppIPalette) *ppIPalette = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateFormatConverter(IWICFormatConverter** ppIFormatConverter) override {
        if (ppIFormatConverter) *ppIFormatConverter = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateBitmapScaler(void** ppIBitmapScaler) override { if (ppIBitmapScaler) *ppIBitmapScaler = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateBitmapClipper(void** ppIBitmapClipper) override { if (ppIBitmapClipper) *ppIBitmapClipper = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateBitmapFlipRotator(void** ppIBitmapFlipRotator) override { if (ppIBitmapFlipRotator) *ppIBitmapFlipRotator = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateStream(void** ppIWICStream) override { if (ppIWICStream) *ppIWICStream = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateColorContext(void** ppIColorContext) override { if (ppIColorContext) *ppIColorContext = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateColorTransformer(void** ppIColorTransform) override { if (ppIColorTransform) *ppIColorTransform = nullptr; return ole32::E_NOTIMPL; }

    int32_t __stdcall CreateBitmap(uint32_t uiWidth, uint32_t uiHeight, const GUID& pixelFormat, uint32_t, IWICBitmap** ppIBitmap) override {
        if (!ppIBitmap) return ole32::E_POINTER;
        *ppIBitmap = new CWICBitmap(uiWidth, uiHeight, pixelFormat);
        return ole32::S_OK;
    }

    int32_t __stdcall CreateBitmapFromSource(IWICBitmapSource* pIBitmapSource, uint32_t, IWICBitmap** ppIBitmap) override {
        if (!pIBitmapSource || !ppIBitmap) return ole32::E_POINTER;
        uint32_t w = 0, h = 0;
        pIBitmapSource->GetSize(&w, &h);
        GUID fmt{};
        pIBitmapSource->GetPixelFormat(&fmt);
        *ppIBitmap = new CWICBitmap(w, h, fmt);
        return ole32::S_OK;
    }

    int32_t __stdcall CreateBitmapFromSourceRect(IWICBitmapSource*, uint32_t, uint32_t, uint32_t, uint32_t, IWICBitmap** ppIBitmap) override {
        if (ppIBitmap) *ppIBitmap = nullptr;
        return ole32::E_NOTIMPL;
    }

    int32_t __stdcall CreateBitmapFromMemory(uint32_t uiWidth, uint32_t uiHeight, const GUID& pixelFormat, uint32_t cbStride, uint32_t cbBufferSize, uint8_t* pbBuffer, IWICBitmap** ppIBitmap) override {
        (void)cbBufferSize;
        if (!ppIBitmap || !pbBuffer) return ole32::E_POINTER;
        *ppIBitmap = new CWICBitmap(uiWidth, uiHeight, pixelFormat, cbStride, pbBuffer);
        return ole32::S_OK;
    }

    int32_t __stdcall CreateBitmapFromHBITMAP(void*, void*, int32_t, IWICBitmap** ppIBitmap) override { if (ppIBitmap) *ppIBitmap = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateBitmapFromHICON(void*, IWICBitmap** ppIBitmap) override { if (ppIBitmap) *ppIBitmap = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateComponentEnumerator(uint32_t, uint32_t, void** ppIEnumUnknown) override { if (ppIEnumUnknown) *ppIEnumUnknown = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateFastMetadataEncoderFromDecoder(IWICBitmapDecoder*, void** ppIFastEncoder) override { if (ppIFastEncoder) *ppIFastEncoder = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateFastMetadataEncoderFromFrameDecode(void*, void** ppIFastEncoder) override { if (ppIFastEncoder) *ppIFastEncoder = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateQueryWriter(const GUID&, const GUID*, void** ppIQueryWriter) override { if (ppIQueryWriter) *ppIQueryWriter = nullptr; return ole32::E_NOTIMPL; }
    int32_t __stdcall CreateQueryWriterFromReader(void*, const GUID*, void** ppIQueryWriter) override { if (ppIQueryWriter) *ppIQueryWriter = nullptr; return ole32::E_NOTIMPL; }
};

// ============================================================================
// 12. Flat C API Function Exports (gdiplus.dll)
// ============================================================================

inline Status __stdcall GdipCreatePen1(ARGB color, float width, Unit, GpPen** pen) {
    if (!pen) return InvalidParameter;
    *pen = new Pen(Color(color), width);
    return Ok;
}

inline Status __stdcall GdipDeletePen(GpPen* pen) {
    if (!pen) return InvalidParameter;
    delete pen;
    return Ok;
}

inline Status __stdcall GdipCreateSolidFill(ARGB color, GpSolidFill** brush) {
    if (!brush) return InvalidParameter;
    *brush = new SolidBrush(Color(color));
    return Ok;
}

inline Status __stdcall GdipDeleteBrush(GpBrush* brush) {
    if (!brush) return InvalidParameter;
    delete brush;
    return Ok;
}

inline Status __stdcall GdipCreateFromHDC(gdi32::HDC hdc, GpGraphics** graphics) {
    if (!graphics) return InvalidParameter;
    *graphics = Graphics::FromHDC(hdc);
    return Ok;
}

inline Status __stdcall GdipDeleteGraphics(GpGraphics* graphics) {
    if (!graphics) return InvalidParameter;
    delete graphics;
    return Ok;
}

inline Status __stdcall GdipDrawLine(GpGraphics* graphics, GpPen* pen, float x1, float y1, float x2, float y2) {
    if (!graphics || !pen) return InvalidParameter;
    return graphics->DrawLine(pen, x1, y1, x2, y2);
}

inline Status __stdcall GdipFillRectangle(GpGraphics* graphics, GpBrush* brush, float x, float y, float width, float height) {
    if (!graphics || !brush) return InvalidParameter;
    return graphics->FillRectangle(brush, x, y, width, height);
}

inline Status __stdcall GdipCreateBitmapFromScan0(int32_t width, int32_t height, int32_t stride, PixelFormat format, uint8_t* scan0, GpBitmap** bitmap) {
    if (!bitmap || width <= 0 || height <= 0) return InvalidParameter;
    *bitmap = new Bitmap(width, height, stride, format, scan0);
    return Ok;
}

inline Status __stdcall GdipDisposeImage(GpImage* image) {
    if (!image) return InvalidParameter;
    delete image;
    return Ok;
}

inline int32_t __stdcall WICCreateImagingFactory_Proxy(uint32_t version, IWICImagingFactory** ppImagingFactory) {
    if (!ppImagingFactory) return ole32::E_POINTER;
    *ppImagingFactory = nullptr;
    micant::um::wic::IWICImagingFactory* factory{};
    const auto status = ::WICCreateImagingFactory_Proxy(version, &factory);
    if (status >= 0)
        *ppImagingFactory = reinterpret_cast<IWICImagingFactory*>(factory);
    return status;
}

inline int32_t __stdcall DllCanUnloadNow() {
    return ::DllCanUnloadNow();
}

// ----------------------------------------------------------------------------
// GDI+ Flat C API Exports (Memory, Matrix, Paths, Brushes, Regions, Graphics, Imaging, Typography)
// ----------------------------------------------------------------------------

inline void* __stdcall GdipAlloc(size_t size) noexcept {
    return std::malloc(size);
}

inline void __stdcall GdipFree(void* ptr) noexcept {
    std::free(ptr);
}

inline Status __stdcall GdipCreateMatrix(GpMatrix** matrix) noexcept {
    if (!matrix) return InvalidParameter;
    *matrix = new (std::nothrow) GpMatrix();
    return *matrix ? Ok : OutOfMemory;
}

inline Status __stdcall GdipDeleteMatrix(GpMatrix* matrix) noexcept {
    delete matrix;
    return Ok;
}

inline Status __stdcall GdipSetWorldTransform(GpGraphics* graphics, GpMatrix* matrix) noexcept {
    if (!graphics || !matrix) return InvalidParameter;
    graphics->transform = *matrix;
    return Ok;
}

inline Status __stdcall GdipResetWorldTransform(GpGraphics* graphics) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->transform.reset();
    return Ok;
}

inline Status __stdcall GdipTranslateWorldTransform(GpGraphics* graphics, float dx, float dy, MatrixOrder order) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->transform.translate(dx, dy, order);
    return Ok;
}

inline Status __stdcall GdipScaleWorldTransform(GpGraphics* graphics, float sx, float sy, MatrixOrder order) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->transform.scale(sx, sy, order);
    return Ok;
}

inline Status __stdcall GdipRotateWorldTransform(GpGraphics* graphics, float angle, MatrixOrder order) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->transform.rotate(angle, order);
    return Ok;
}

inline Status __stdcall GdipTranslateMatrix(GpMatrix* matrix, float offsetX, float offsetY, MatrixOrder order) noexcept {
    if (!matrix) return InvalidParameter;
    matrix->translate(offsetX, offsetY, order);
    return Ok;
}

inline Status __stdcall GdipScaleMatrix(GpMatrix* matrix, float scaleX, float scaleY, MatrixOrder order) noexcept {
    if (!matrix) return InvalidParameter;
    matrix->scale(scaleX, scaleY, order);
    return Ok;
}

inline Status __stdcall GdipRotateMatrix(GpMatrix* matrix, float angle, MatrixOrder order) noexcept {
    if (!matrix) return InvalidParameter;
    matrix->rotate(angle, order);
    return Ok;
}

inline Status __stdcall GdipInvertMatrix(GpMatrix* matrix) noexcept {
    if (!matrix) return InvalidParameter;
    return matrix->invert() ? Ok : GenericError;
}

inline Status __stdcall GdipTransformMatrixPoints(GpMatrix* matrix, PointF* pts, int count) noexcept {
    if (!matrix || !pts || count <= 0) return InvalidParameter;
    matrix->transform(pts, count);
    return Ok;
}

inline Status __stdcall GdipCreatePath(FillMode brushMode, GpPath** path) noexcept {
    if (!path) return InvalidParameter;
    *path = new (std::nothrow) GpPath();
    if (!*path) return OutOfMemory;
    (*path)->fillMode = brushMode;
    return Ok;
}

inline Status __stdcall GdipDeletePath(GpPath* path) noexcept {
    delete path;
    return Ok;
}

inline Status __stdcall GdipResetPath(GpPath* path) noexcept {
    if (!path) return InvalidParameter;
    path->reset();
    return Ok;
}

inline Status __stdcall GdipAddPathRectangleI(GpPath* path, int x, int y, int width, int height) noexcept {
    if (!path) return InvalidParameter;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fw = static_cast<float>(width);
    float fh = static_cast<float>(height);
    path->points.push_back({ fx, fy });
    path->points.push_back({ fx + fw, fy });
    path->points.push_back({ fx + fw, fy + fh });
    path->points.push_back({ fx, fy + fh });
    path->types.push_back(0); // Start
    path->types.push_back(1); // Line
    path->types.push_back(1); // Line
    path->types.push_back(1); // Line
    return Ok;
}

inline Status __stdcall GdipDrawPath(GpGraphics*, GpPen*, GpPath* path) noexcept {
    if (!path) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipFillPath(GpGraphics*, GpBrush*, GpPath* path) noexcept {
    if (!path) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipWindingModeOutline(GpPath* path, GpMatrix*, float) noexcept {
    if (!path) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipCreatePen2(GpBrush* brush, float width, Unit unit, GpPen** pen) noexcept {
    if (!pen) return InvalidParameter;
    *pen = new (std::nothrow) GpPen();
    if (!*pen) return OutOfMemory;
    if (brush) (*pen)->brush = *brush;
    (*pen)->width = width;
    (*pen)->unit = unit;
    return Ok;
}

inline Status __stdcall GdipSetPenDashStyle(GpPen* pen, DashStyle dashstyle) noexcept {
    if (!pen) return InvalidParameter;
    pen->dashStyle = dashstyle;
    return Ok;
}

inline Status __stdcall GdipSetSolidFillColor(GpBrush* brush, ARGB color) noexcept {
    if (!brush) return InvalidParameter;
    brush->color = color;
    return Ok;
}

inline Status __stdcall GdipCreateHatchBrush(int hatchstyle, ARGB forecol, ARGB backcol, GpBrush** brush) noexcept {
    if (!brush) return InvalidParameter;
    *brush = new (std::nothrow) HatchBrush(hatchstyle, forecol, backcol);
    if (!*brush) return OutOfMemory;
    return Ok;
}

inline Status __stdcall GdipCloneBrush(GpBrush* brush, GpBrush** cloneBrush) noexcept {
    if (!brush || !cloneBrush) return InvalidParameter;
    *cloneBrush = brush->Clone();
    return *cloneBrush ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCreateRegion(GpRegion** region) noexcept {
    if (!region) return InvalidParameter;
    *region = new (std::nothrow) GpRegion();
    return *region ? Ok : OutOfMemory;
}

inline Status __stdcall GdipDeleteRegion(GpRegion* region) noexcept {
    delete region;
    return Ok;
}

inline Status __stdcall GdipGetRegionBounds(GpRegion* region, GpGraphics*, RectF* gprect) noexcept {
    if (!region || !gprect) return InvalidParameter;
    *gprect = region->bounds;
    return Ok;
}

inline Status __stdcall GdipGetRegionHRgn(GpRegion*, GpGraphics*, void** hRgn) noexcept {
    if (!hRgn) return InvalidParameter;
    *hRgn = reinterpret_cast<void*>(0x8890);
    return Ok;
}

inline Status __stdcall GdipGetClip(GpGraphics* graphics, GpRegion* region) noexcept {
    if (!graphics || !region) return InvalidParameter;
    *region = graphics->clip;
    return Ok;
}

inline Status __stdcall GdipSetPageUnit(GpGraphics* graphics, Unit unit) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->pageUnit = unit;
    return Ok;
}

inline Status __stdcall GdipSetSmoothingMode(GpGraphics* graphics, SmoothingMode smoothingMode) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->smoothing = smoothingMode;
    return Ok;
}

inline Status __stdcall GdipSetInterpolationMode(GpGraphics* graphics, InterpolationMode interpolationMode) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->interpolation = interpolationMode;
    return Ok;
}

inline Status __stdcall GdipSetCompositingQuality(GpGraphics* graphics, CompositingQuality compositingQuality) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->compositingQuality = compositingQuality;
    return Ok;
}

inline Status __stdcall GdipSetCompositingMode(GpGraphics* graphics, CompositingMode compositingMode) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->compositingMode = compositingMode;
    return Ok;
}

inline Status __stdcall GdipSetTextRenderingHint(GpGraphics* graphics, int mode) noexcept {
    if (!graphics) return InvalidParameter;
    graphics->textRenderingHint = mode;
    return Ok;
}

inline Status __stdcall GdipGetDC(GpGraphics* graphics, void** hdc) noexcept {
    if (!graphics || !hdc) return InvalidParameter;
    *hdc = graphics->hdc ? graphics->hdc : reinterpret_cast<void*>(0x1100);
    return Ok;
}

inline Status __stdcall GdipReleaseDC(GpGraphics*, void*) noexcept {
    return Ok;
}

inline Status __stdcall GdipGetImageGraphicsContext(GpImage* image, GpGraphics** graphics) noexcept {
    if (!image || !graphics) return InvalidParameter;
    *graphics = new (std::nothrow) GpGraphics(image);
    if (!*graphics) return OutOfMemory;
    (*graphics)->targetImage = image;
    return Ok;
}

inline Status __stdcall GdipDrawLineI(GpGraphics*, GpPen*, int, int, int, int) noexcept {
    return Ok;
}

inline Status __stdcall GdipDrawRectangleI(GpGraphics*, GpPen*, int, int, int, int) noexcept {
    return Ok;
}

inline Status __stdcall GdipFillRectangleI(GpGraphics*, GpBrush*, int, int, int, int) noexcept {
    return Ok;
}

inline Status __stdcall GdipFillEllipseI(GpGraphics*, GpBrush*, int, int, int, int) noexcept {
    return Ok;
}

inline Status __stdcall GdipCreateBitmapFromGraphics(int width, int height, GpGraphics*, GpBitmap** bitmap) noexcept {
    if (!bitmap || width <= 0 || height <= 0) return InvalidParameter;
    *bitmap = new (std::nothrow) GpBitmap(width, height);
    return *bitmap ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCreateBitmapFromStream(void*, GpBitmap** bitmap) noexcept {
    if (!bitmap) return InvalidParameter;
    *bitmap = new (std::nothrow) GpBitmap(800, 600);
    return *bitmap ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCreateBitmapFromGdiDib(const void*, const void*, GpBitmap** bitmap) noexcept {
    if (!bitmap) return InvalidParameter;
    *bitmap = new (std::nothrow) GpBitmap(800, 600);
    return *bitmap ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCreateBitmapFromHBITMAP(void*, void*, GpBitmap** bitmap) noexcept {
    if (!bitmap) return InvalidParameter;
    *bitmap = new (std::nothrow) GpBitmap(800, 600);
    return *bitmap ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCreateHBITMAPFromBitmap(GpBitmap*, void** hbmReturn, ARGB) noexcept {
    if (!hbmReturn) return InvalidParameter;
    *hbmReturn = reinterpret_cast<void*>(0x8891);
    return Ok;
}

inline Status __stdcall GdipCloneBitmapAreaI(int, int, int width, int height, PixelFormat, GpBitmap* src, GpBitmap** dst) noexcept {
    if (!src || !dst || width <= 0 || height <= 0) return InvalidParameter;
    *dst = new (std::nothrow) GpBitmap(width, height);
    return *dst ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCloneImage(GpImage* image, GpImage** cloneImage) noexcept {
    if (!image || !cloneImage) return InvalidParameter;
    auto* bmp = new (std::nothrow) GpBitmap(image->width, image->height);
    if (!bmp) return OutOfMemory;
    *cloneImage = bmp;
    return Ok;
}

inline Status __stdcall GdipGetImageWidth(GpImage* image, uint32_t* width) noexcept {
    if (!image || !width) return InvalidParameter;
    *width = image->width;
    return Ok;
}

inline Status __stdcall GdipGetImageHeight(GpImage* image, uint32_t* height) noexcept {
    if (!image || !height) return InvalidParameter;
    *height = image->height;
    return Ok;
}

inline Status __stdcall GdipGetImagePixelFormat(GpImage* image, PixelFormat* format) noexcept {
    if (!image || !format) return InvalidParameter;
    *format = image->format;
    return Ok;
}

inline Status __stdcall GdipGetImageHorizontalResolution(GpImage* image, float* resolution) noexcept {
    if (!image || !resolution) return InvalidParameter;
    *resolution = image->hRes;
    return Ok;
}

inline Status __stdcall GdipBitmapSetResolution(GpBitmap* bitmap, float xdpi, float ydpi) noexcept {
    if (!bitmap) return InvalidParameter;
    bitmap->hRes = xdpi;
    bitmap->vRes = ydpi;
    return Ok;
}

inline Status __stdcall GdipBitmapLockBits(GpBitmap* bitmap, const Rect* rect, uint32_t, PixelFormat format, void* lockedBitmapData) noexcept {
    if (!bitmap || !lockedBitmapData) return InvalidParameter;
    auto* bdata = reinterpret_cast<BitmapData*>(lockedBitmapData);
    bdata->Width = rect ? rect->Width : bitmap->width;
    bdata->Height = rect ? rect->Height : bitmap->height;
    bdata->Stride = bdata->Width * 4;
    bdata->PixelFormat = format;
    bdata->Scan0 = bitmap->pixelBuffer.data();
    return Ok;
}

inline Status __stdcall GdipBitmapUnlockBits(GpBitmap* bitmap, void*) noexcept {
    if (!bitmap) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipDrawImageI(GpGraphics*, GpImage* image, int, int) noexcept {
    if (!image) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipDrawImageRectRect(GpGraphics*, GpImage* image, float, float, float, float, float, float, float, float, Unit, const void*, void*, void*) noexcept {
    if (!image) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipDrawImageRectRectI(GpGraphics*, GpImage* image, int, int, int, int, int, int, int, int, Unit, const void*, void*, void*) noexcept {
    if (!image) return InvalidParameter;
    return Ok;
}

inline Status __stdcall GdipImageRotateFlip(GpImage* image, int) noexcept {
    if (!image) return InvalidParameter;
    return Ok;
}

struct EncoderParameter {
    GUID Guid{};
    uint32_t NumberOfValues{};
    uint32_t Type{};
    void* Value{};
};
struct EncoderParameters { uint32_t Count{}; EncoderParameter Parameter[1]{}; };

inline Status GdipImageCodecStatus(um::imaging::ImageStatus status) noexcept {
    using um::imaging::ImageStatus;
    switch (status) {
        case ImageStatus::Success: return Ok;
        case ImageStatus::UnknownFormat: return UnknownImageFormat;
        case ImageStatus::UnsupportedFeature: return NotImplemented;
        case ImageStatus::ImageTooLarge: return ValueOverflow;
        case ImageStatus::OutOfMemory: return OutOfMemory;
        case ImageStatus::AccessDenied: return AccessDenied;
        case ImageStatus::Cancelled: return Aborted;
        case ImageStatus::MalformedData:
        case ImageStatus::TruncatedInput: return InvalidParameter;
        case ImageStatus::IoFailure: return Win32Error;
    }
    return GenericError;
}

inline bool GdipEncoderFormat(const GUID* clsid, um::imaging::ImageFormat& format) noexcept {
    if (!clsid) return false;
    if (*clsid == EncoderClsidBmp) format = um::imaging::ImageFormat::Bmp;
    else if (*clsid == EncoderClsidJpeg) format = um::imaging::ImageFormat::Jpeg;
    else if (*clsid == EncoderClsidPng) format = um::imaging::ImageFormat::Png;
    else return false;
    return true;
}

inline Status GdipBuildEncodeFrame(GpImage* image, um::imaging::ImageFrame& frame,
                                   const um::imaging::ImageAllocator& allocator) noexcept {
    if (!image || image->GetType() != ImageTypeBitmap) return NotImplemented;
    auto* bitmap = static_cast<Bitmap*>(image);
    const uint32_t width = bitmap->GetWidth(), height = bitmap->GetHeight();
    const uint64_t stride64 = static_cast<uint64_t>(width) * 4;
    if (!width || !height || stride64 > UINT32_MAX ||
        height > std::numeric_limits<uint64_t>::max() / stride64) return ValueOverflow;
    const uint64_t bytes64 = stride64 * height;
    if (bytes64 > SIZE_MAX ||
        bytes64 > 256ull * 1024 * 1024) return ValueOverflow;
    const auto* source = bitmap->GetPixelBuffer();
    if (!source) return GenericError;
    frame.width = width;
    frame.height = height;
    frame.stride = static_cast<uint32_t>(stride64);
    frame.sourceHadAlpha = bitmap->GetPixelFormat() != PixelFormat32bppRGB;
    if (!frame.pixels.allocate(static_cast<size_t>(bytes64), allocator)) return OutOfMemory;
    const size_t count = static_cast<size_t>(bytes64 / 4);
    if (bitmap->GetPixelFormat() != PixelFormat32bppARGB &&
        bitmap->GetPixelFormat() != PixelFormat32bppPARGB &&
        bitmap->GetPixelFormat() != PixelFormat32bppRGB) return NotImplemented;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t pixel = source[i];
        const uint8_t a = bitmap->GetPixelFormat() == PixelFormat32bppRGB ? 255 : static_cast<uint8_t>(pixel >> 24);
        uint8_t b = static_cast<uint8_t>(pixel), g = static_cast<uint8_t>(pixel >> 8), r = static_cast<uint8_t>(pixel >> 16);
        if (bitmap->GetPixelFormat() == PixelFormat32bppARGB) {
            b = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
            g = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
            r = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
        }
        frame.pixels[i * 4] = b; frame.pixels[i * 4 + 1] = g;
        frame.pixels[i * 4 + 2] = r; frame.pixels[i * 4 + 3] = a;
    }
    return Ok;
}

inline Status GdipGetEncodeOptions(const GUID* encoder, const EncoderParameters* params,
                                  um::imaging::ImageEncodeOptions& options) noexcept {
    if (!params) return Ok;
    if (!encoder || params->Count != 1 || encoder->operator==(EncoderClsidJpeg) == false)
        return NotImplemented;
    const auto& parameter = params->Parameter[0];
    if (!(parameter.Guid == EncoderQuality) || parameter.NumberOfValues != 1 ||
        parameter.Type != 4 || !parameter.Value) return InvalidParameter;
    const uint32_t quality = *static_cast<const uint32_t*>(parameter.Value);
    if (quality > 100) return InvalidParameter;
    options.quality = static_cast<uint8_t>(quality ? quality : 1);
    return Ok;
}

inline Status __stdcall GdipSaveImageToFile(GpImage* image, const wchar_t* filename,
                                            const GUID* clsidEncoder,
                                            const EncoderParameters* encoderParams) noexcept {
    if (!image || !filename || !*filename || !clsidEncoder) return InvalidParameter;
    um::imaging::ImageFormat format{};
    if (!GdipEncoderFormat(clsidEncoder, format)) return NotImplemented;
    um::imaging::ImageEncodeOptions options{};
    const Status paramStatus = GdipGetEncodeOptions(clsidEncoder, encoderParams, options);
    if (paramStatus != Ok) return paramStatus;
    um::imaging::ImageFrame frame{};
    const Status frameStatus = GdipBuildEncodeFrame(image, frame, um::imaging::NativeImageAllocator());
    if (frameStatus != Ok) return frameStatus;
    return GdipImageCodecStatus(um::imaging::EncodeImageFile(filename, frame, format,
        um::imaging::NativeImageAllocator(), options));
}

inline Status __stdcall GdipImageGetFrameCount(GpImage* image, const void*, uint32_t* count) noexcept {
    if (!image || !count) return InvalidParameter;
    *count = 1;
    return Ok;
}

inline Status __stdcall GdipImageSelectActiveFrame(GpImage* image, const void*, uint32_t frameIndex) noexcept {
    if (!image) return InvalidParameter;
    // The shared decoder currently materializes one still frame. Only that
    // frame is selectable; do not report success for an unrepresented frame.
    return frameIndex == 0 ? Ok : InvalidParameter;
}

inline Status __stdcall GdipGetPropertyItemSize(GpImage* image, uint32_t, uint32_t* size) noexcept {
    if (!image || !size) return InvalidParameter;
    *size = 0;
    return NotImplemented;
}

inline Status __stdcall GdipGetPropertyItem(GpImage* image, uint32_t, uint32_t propSize, void* buffer) noexcept {
    if (!image || !buffer || propSize == 0) return InvalidParameter;
    return NotImplemented;
}

inline Status __stdcall GdipSetPropertyItem(GpImage* image, const void*) noexcept {
    if (!image) return InvalidParameter;
    return NotImplemented;
}

inline Status __stdcall GdipGetImageEncodersSize(uint32_t* numEncoders, uint32_t* size) noexcept {
    if (!numEncoders || !size) return InvalidParameter;
    constexpr uint32_t count = 3;
    *numEncoders = count;
    *size = count * sizeof(ImageCodecInfo);
    return Ok;
}

inline Status __stdcall GdipGetImageEncoders(uint32_t numEncoders, uint32_t size, void* encoders) noexcept {
    constexpr uint32_t count = 3;
    if (!encoders || numEncoders < count || size < count * sizeof(ImageCodecInfo))
        return InvalidParameter;
    static constexpr wchar_t bmpName[] = L"BMP Encoder";
    static constexpr wchar_t jpegName[] = L"JPEG Encoder";
    static constexpr wchar_t pngName[] = L"PNG Encoder";
    static constexpr wchar_t bmpExt[] = L"*.BMP";
    static constexpr wchar_t jpegExt[] = L"*.JPG;*.JPEG";
    static constexpr wchar_t pngExt[] = L"*.PNG";
    static constexpr wchar_t bmpMime[] = L"image/bmp";
    static constexpr wchar_t jpegMime[] = L"image/jpeg";
    static constexpr wchar_t pngMime[] = L"image/png";
    const GUID* encodersByIndex[count]{&EncoderClsidBmp, &EncoderClsidJpeg, &EncoderClsidPng};
    const GUID* formats[count]{&ImageFormatBMP, &ImageFormatJPEG, &ImageFormatPNG};
    const wchar_t* names[count]{bmpName, jpegName, pngName};
    const wchar_t* extensions[count]{bmpExt, jpegExt, pngExt};
    const wchar_t* mimeTypes[count]{bmpMime, jpegMime, pngMime};
    auto* codecs = static_cast<ImageCodecInfo*>(encoders);
    for (uint32_t i = 0; i < count; ++i) {
        codecs[i] = ImageCodecInfo{};
        std::memcpy(codecs[i].Clsid, encodersByIndex[i], sizeof(GUID));
        std::memcpy(codecs[i].FormatID, formats[i], sizeof(GUID));
        codecs[i].CodecName = names[i];
        codecs[i].DllName = L"gdiplus.dll";
        codecs[i].FormatDescription = names[i];
        codecs[i].FilenameExtension = extensions[i];
        codecs[i].MimeType = mimeTypes[i];
        codecs[i].Flags = 1;
    }
    return Ok;
}

inline Status __stdcall GdipCreateImageAttributes(GpImageAttributes** imageattr) noexcept {
    if (!imageattr) return InvalidParameter;
    *imageattr = new (std::nothrow) GpImageAttributes();
    return *imageattr ? Ok : OutOfMemory;
}

inline Status __stdcall GdipDisposeImageAttributes(GpImageAttributes* imageattr) noexcept {
    delete imageattr;
    return Ok;
}

inline Status __stdcall GdipSetImageAttributesWrapMode(GpImageAttributes* imageattr, int wrap, ARGB color, int) noexcept {
    if (!imageattr) return InvalidParameter;
    imageattr->wrapMode = wrap;
    imageattr->color = color;
    return Ok;
}

inline Status __stdcall GdipCreateFontFamilyFromName(const wchar_t* name, void*, GpFontFamily** FontFamily) noexcept {
    if (!FontFamily) return InvalidParameter;
    *FontFamily = new (std::nothrow) GpFontFamily();
    if (!*FontFamily) return OutOfMemory;
    if (name) (*FontFamily)->name = name;
    return Ok;
}

inline Status __stdcall GdipDeleteFontFamily(GpFontFamily* FontFamily) noexcept {
    delete FontFamily;
    return Ok;
}

inline Status __stdcall GdipGetGenericFontFamilySansSerif(GpFontFamily** nativeFamily) noexcept {
    if (!nativeFamily) return InvalidParameter;
    *nativeFamily = new (std::nothrow) GpFontFamily();
    if (!*nativeFamily) return OutOfMemory;
    (*nativeFamily)->name = L"Segoe UI";
    return Ok;
}

inline Status __stdcall GdipGetFamilyName(GpFontFamily* family, wchar_t name[32], uint16_t) noexcept {
    if (!family || !name) return InvalidParameter;
    std::wcsncpy(name, family->name.c_str(), 31);
    name[31] = L'\0';
    return Ok;
}

inline Status __stdcall GdipCreateFont(const GpFontFamily* family, float emSize, int style, Unit unit, GpFont** font) noexcept {
    if (!font) return InvalidParameter;
    *font = new (std::nothrow) GpFont();
    if (!*font) return OutOfMemory;
    if (family) (*font)->family = *family;
    (*font)->emSize = emSize;
    (*font)->style = style;
    (*font)->unit = unit;
    return Ok;
}

inline Status __stdcall GdipCreateFontFromDC(void*, GpFont** font) noexcept {
    if (!font) return InvalidParameter;
    *font = new (std::nothrow) GpFont();
    return *font ? Ok : OutOfMemory;
}

inline Status __stdcall GdipCreateFontFromLogfontA(void*, const void* logfont, GpFont** font) noexcept {
    if (!font) return InvalidParameter;
    *font = new (std::nothrow) GpFont();
    if (!*font) return OutOfMemory;
    if (logfont) {
        const char* face = reinterpret_cast<const char*>(logfont) + 28;
        size_t len = std::strlen(face);
        (*font)->family.name.assign(face, face + len);
    }
    return Ok;
}

inline Status __stdcall GdipDeleteFont(GpFont* font) noexcept {
    delete font;
    return Ok;
}

inline Status __stdcall GdipGetFamily(GpFont* font, GpFontFamily** family) noexcept {
    if (!font || !family) return InvalidParameter;
    *family = new (std::nothrow) GpFontFamily(font->family);
    return *family ? Ok : OutOfMemory;
}

inline Status __stdcall GdipGetFontHeight(const GpFont* font, const GpGraphics*, float* height) noexcept {
    if (!font || !height) return InvalidParameter;
    *height = font->emSize * 1.25f;
    return Ok;
}

inline Status __stdcall GdipGetLogFontW(GpFont* font, GpGraphics*, void* logfontW) noexcept {
    if (!font || !logfontW) return InvalidParameter;
    std::memset(logfontW, 0, 92);
    auto* lf = reinterpret_cast<int32_t*>(logfontW);
    lf[0] = static_cast<int32_t>(font->emSize);
    wchar_t* face = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(logfontW) + 28);
    std::wcsncpy(face, font->family.name.c_str(), 31);
    face[31] = L'\0';
    return Ok;
}

inline Status __stdcall GdipCreateStringFormat(int formatAttributes, uint16_t, GpStringFormat** format) noexcept {
    if (!format) return InvalidParameter;
    *format = new (std::nothrow) GpStringFormat();
    if (!*format) return OutOfMemory;
    (*format)->flags = formatAttributes;
    return Ok;
}

inline Status __stdcall GdipCloneStringFormat(const GpStringFormat* format, GpStringFormat** newFormat) noexcept {
    if (!format || !newFormat) return InvalidParameter;
    *newFormat = new (std::nothrow) GpStringFormat(*format);
    return *newFormat ? Ok : OutOfMemory;
}

inline Status __stdcall GdipDeleteStringFormat(GpStringFormat* format) noexcept {
    delete format;
    return Ok;
}

inline Status __stdcall GdipStringFormatGetGenericDefault(GpStringFormat** format) noexcept {
    if (!format) return InvalidParameter;
    *format = new (std::nothrow) GpStringFormat();
    return *format ? Ok : OutOfMemory;
}

inline Status __stdcall GdipStringFormatGetGenericTypographic(GpStringFormat** format) noexcept {
    if (!format) return InvalidParameter;
    *format = new (std::nothrow) GpStringFormat();
    return *format ? Ok : OutOfMemory;
}

inline Status __stdcall GdipSetStringFormatFlags(GpStringFormat* format, int flags) noexcept {
    if (!format) return InvalidParameter;
    format->flags = flags;
    return Ok;
}

inline Status __stdcall GdipGetStringFormatFlags(const GpStringFormat* format, int* flags) noexcept {
    if (!format || !flags) return InvalidParameter;
    *flags = format->flags;
    return Ok;
}

inline Status __stdcall GdipSetStringFormatAlign(GpStringFormat* format, int align) noexcept {
    if (!format) return InvalidParameter;
    format->align = align;
    return Ok;
}

inline Status __stdcall GdipSetStringFormatLineAlign(GpStringFormat* format, int align) noexcept {
    if (!format) return InvalidParameter;
    format->lineAlign = align;
    return Ok;
}

inline Status __stdcall GdipSetStringFormatTrimming(GpStringFormat* format, int trimming) noexcept {
    if (!format) return InvalidParameter;
    format->trimming = trimming;
    return Ok;
}

inline Status __stdcall GdipSetStringFormatMeasurableCharacterRanges(GpStringFormat* format, int rangeCount, const void* ranges) noexcept {
    if (!format) return InvalidParameter;
    format->measurableRanges.clear();
    if (ranges && rangeCount > 0) {
        const auto* r = reinterpret_cast<const CharacterRange*>(ranges);
        format->measurableRanges.assign(r, r + rangeCount);
    }
    return Ok;
}

inline Status __stdcall GdipMeasureCharacterRanges(GpGraphics*, const wchar_t*, int, const GpFont*, const RectF* layoutRect, const GpStringFormat*, int rangeCount, GpRegion** regions) noexcept {
    if (!regions || rangeCount <= 0) return InvalidParameter;
    float rx = layoutRect ? layoutRect->X : 0.0f;
    float ry = layoutRect ? layoutRect->Y : 0.0f;
    for (int i = 0; i < rangeCount; ++i) {
        regions[i] = new (std::nothrow) GpRegion();
        if (regions[i]) {
            regions[i]->bounds = RectF{ rx + (i * 20.0f), ry, 20.0f, 16.0f };
        }
    }
    return Ok;
}

inline Status __stdcall GdipDrawString(GpGraphics*, const wchar_t*, int, const GpFont*, const RectF*, const GpStringFormat*, const GpBrush*) noexcept {
    return Ok;
}

inline Status __stdcall GdipMeasureString(GpGraphics*, const wchar_t* string, int length, const GpFont* font, const RectF*, const GpStringFormat*, RectF* boundingBox, int* codepointsFitted, int* linesFilled) noexcept {
    if (!string || !boundingBox) return InvalidParameter;
    int len = (length >= 0) ? length : static_cast<int>(std::wcslen(string));
    float em = font ? font->emSize : 12.0f;
    boundingBox->X = 0.0f;
    boundingBox->Y = 0.0f;
    boundingBox->Width = static_cast<float>(len) * (em * 0.6f);
    boundingBox->Height = em * 1.25f;
    if (codepointsFitted) *codepointsFitted = len;
    if (linesFilled) *linesFilled = 1;
    return Ok;
}

// ============================================================================
// 13. Dynamic Export Registration & Class Factory Wiring
// ============================================================================

template <typename T>
class CGdiPlusClassFactory : public ole32::IClassFactory {
private:
    std::atomic<uint32_t> m_refCount{ 1 };

public:
    int32_t __stdcall QueryInterface(const GUID& riid, void** ppvObject) override {
        if (!ppvObject) return ole32::E_POINTER;
        if (riid == ole32::IID_IUnknown || riid == ole32::IID_IClassFactory) {
            *ppvObject = static_cast<ole32::IClassFactory*>(this);
            AddRef();
            return ole32::S_OK;
        }
        *ppvObject = nullptr;
        return ole32::E_NOINTERFACE;
    }

    uint32_t __stdcall AddRef() override { return m_refCount.fetch_add(1) + 1; }
    uint32_t __stdcall Release() override {
        uint32_t r = m_refCount.fetch_sub(1) - 1;
        if (r == 0) delete this;
        return r;
    }

    int32_t __stdcall CreateInstance(ole32::IUnknown* pUnkOuter, const GUID& riid, void** ppvObject) override {
        if (pUnkOuter) return ole32::CLASS_E_NOAGGREGATION;
        auto* instance = new T();
        int32_t hr = instance->QueryInterface(riid, ppvObject);
        instance->Release();
        return hr;
    }

    int32_t __stdcall LockServer(int32_t) override { return ole32::S_OK; }
};

struct ColorPalette {
    uint32_t Flags{0};
    uint32_t Count{0};
    uint32_t Entries[1]{0};
};

inline int32_t GdipAddPathArcI(void* /*path*/, int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/, float /*startAngle*/, float /*sweepAngle*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipClosePathFigure(void* /*path*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipAddPathLineI(void* /*path*/, int32_t /*x1*/, int32_t /*y1*/, int32_t /*x2*/, int32_t /*y2*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipAddPathBezierI(void* /*path*/, int32_t /*x1*/, int32_t /*y1*/, int32_t /*x2*/, int32_t /*y2*/, int32_t /*x3*/, int32_t /*y3*/, int32_t /*x4*/, int32_t /*y4*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipStartPathFigure(void* /*path*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipDrawBezierI(void* /*graphics*/, void* /*pen*/, int32_t /*x1*/, int32_t /*y1*/, int32_t /*x2*/, int32_t /*y2*/, int32_t /*x3*/, int32_t /*y3*/, int32_t /*x4*/, int32_t /*y4*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipDrawImageRectI(void* /*graphics*/, void* /*image*/, int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/) noexcept {
    return 0; // Ok
}

inline int32_t GdipGetImagePalette(void* /*image*/, ColorPalette* palette, int32_t size) noexcept {
    if (palette && size >= static_cast<int32_t>(sizeof(ColorPalette))) {
        palette->Flags = 0;
        palette->Count = 1;
        palette->Entries[0] = 0xFF000000;
    }
    return 0; // Ok
}

inline int32_t GdipGetImagePaletteSize(void* /*image*/, int32_t* size) noexcept {
    if (size) {
        *size = sizeof(ColorPalette);
    }
    return 0; // Ok
}

inline void* GdipImageAllocate(void*, size_t bytes) noexcept { return std::malloc(bytes); }
inline void GdipImageFree(void*, void* address, size_t) noexcept { std::free(address); }

inline Status GdipCreateBitmapFromFile(const wchar_t* filename, void** bitmap) noexcept {
    if (!bitmap) return InvalidParameter;
    *bitmap = nullptr;
    if (!filename || !*filename) return InvalidParameter;

    using micant::win32::HANDLE;
    HANDLE file = micant::win32::CreateFileW(filename, micant::win32::GENERIC_READ,
        micant::win32::FILE_SHARE_READ, nullptr, micant::win32::OPEN_EXISTING, 0, nullptr);
    if (file == micant::win32::INVALID_HANDLE_VALUE) {
        const auto error = micant::win32::GetLastError();
        return error == 5 ? AccessDenied : (error == 2 || error == 3 ? FileNotFound : Win32Error);
    }
    struct FileCloser {
        HANDLE value;
        ~FileCloser() { (void)micant::win32::CloseHandle(value); }
    } closer{file};

    micant::LargeInteger fileSize{};
    if (!micant::win32::GetFileSizeEx(file, &fileSize) || fileSize.quadPart <= 0)
        return GenericError;
    um::imaging::ImageLimits limits{};
    if (static_cast<uint64_t>(fileSize.quadPart) > limits.maxEncodedBytes) return ValueOverflow;
    try {
        std::vector<uint8_t> encoded(static_cast<size_t>(fileSize.quadPart));
        uint32_t received{};
        if (!micant::win32::ReadFile(file, encoded.data(), static_cast<uint32_t>(encoded.size()),
                                     &received, nullptr) || received != encoded.size())
            return GenericError;
        um::imaging::ImageFrame decoded{};
        const um::imaging::ImageAllocator allocator{nullptr, &GdipImageAllocate, &GdipImageFree};
        if (encoded.size() > limits.maxIntermediateBytes) return ValueOverflow;
        limits.maxIntermediateBytes -= encoded.size();
        limits.maxDecodedBytes = std::min(limits.maxDecodedBytes, limits.maxIntermediateBytes / 3);
        const auto status = um::imaging::DecodeImage(encoded, decoded, allocator, limits);
        if (status != um::imaging::ImageStatus::Success) {
            switch (status) {
                case um::imaging::ImageStatus::UnknownFormat: return UnknownImageFormat;
                case um::imaging::ImageStatus::UnsupportedFeature: return NotImplemented;
                case um::imaging::ImageStatus::ImageTooLarge: return ValueOverflow;
                case um::imaging::ImageStatus::OutOfMemory: return OutOfMemory;
                default: return GenericError;
            }
        }
        auto* result = new Bitmap(decoded.width, decoded.height, PixelFormat32bppPARGB);
        if (result->pixelBuffer.size() != decoded.pixels.size()) {
            delete result;
            return ValueOverflow;
        }
        std::memcpy(result->pixelBuffer.data(), decoded.pixels.data(), decoded.pixels.size());
        std::memcpy(result->GetPixelBuffer(), decoded.pixels.data(), decoded.pixels.size());
        result->SetRawFormat(decoded.sourceFormat == um::imaging::ImageFormat::Jpeg ? ImageFormatJPEG :
                             decoded.sourceFormat == um::imaging::ImageFormat::Png ? ImageFormatPNG : ImageFormatBMP);
        *bitmap = result;
        return Ok;
    } catch (const std::bad_alloc&) {
        return OutOfMemory;
    } catch (...) {
        return GenericError;
    }
}

inline Status GdipLoadImageFromFile(const wchar_t* filename, GpImage** image) noexcept {
    if (!image) return InvalidParameter;
    *image = nullptr;
    void* bitmap{};
    const Status status = GdipCreateBitmapFromFile(filename, &bitmap);
    if (status == Ok) *image = static_cast<GpImage*>(bitmap);
    return status;
}

inline int32_t GdipSaveImageToStream(void* image, void* stream, const void* clsidEncoder,
                                    const void* encoderParams) noexcept {
    if (!image || !stream || !clsidEncoder) return InvalidParameter;
    um::imaging::ImageFormat format{};
    if (!GdipEncoderFormat(static_cast<const GUID*>(clsidEncoder), format)) return NotImplemented;
    um::imaging::ImageEncodeOptions options{};
    const Status params = GdipGetEncodeOptions(static_cast<const GUID*>(clsidEncoder),
        static_cast<const EncoderParameters*>(encoderParams), options);
    if (params != Ok) return params;
    um::imaging::ImageFrame frame{};
    const Status frameStatus = GdipBuildEncodeFrame(static_cast<GpImage*>(image), frame,
        um::imaging::NativeImageAllocator());
    if (frameStatus != Ok) return frameStatus;
    um::imaging::EncodedImage encoded{};
    const auto encodeStatus = um::imaging::EncodeImage(frame, format, encoded,
        um::imaging::NativeImageAllocator(), options);
    if (encodeStatus != um::imaging::ImageStatus::Success) return GdipImageCodecStatus(encodeStatus);
    auto* output = static_cast<ole32::IStream*>(stream);
    size_t writtenTotal = 0;
    while (writtenTotal < encoded.byteLength) {
        const uint32_t chunk = static_cast<uint32_t>(std::min<size_t>(
            encoded.byteLength - writtenTotal, UINT32_MAX));
        uint32_t written{};
        const auto hr = output->Write(encoded.bytes.data() + writtenTotal, chunk, &written);
        if (hr < 0 || !written || written > chunk) return Win32Error;
        writtenTotal += written;
    }
    return Ok;
}

inline int32_t GdipDrawLinesI(void* /*graphics*/, void* /*pen*/, const void* /*points*/, int32_t /*count*/) noexcept {
    return 0; // Ok
}

inline void InitializeGdiPlusExports() {
    auto& loader = ldr::DynamicLoader::get();

    // gdiplus.dll exports
    loader.registerExport("gdiplus.dll", "GdiplusStartup", reinterpret_cast<void*>(&GdiplusStartup));
    loader.registerExport("gdiplus.dll", "GdiplusShutdown", reinterpret_cast<void*>(&GdiplusShutdown));
    loader.registerExport("gdiplus.dll", "GdipAlloc", reinterpret_cast<void*>(&GdipAlloc));
    loader.registerExport("gdiplus.dll", "GdipFree", reinterpret_cast<void*>(&GdipFree));

    loader.registerExport("gdiplus.dll", "GdipCreateMatrix", reinterpret_cast<void*>(&GdipCreateMatrix));
    loader.registerExport("gdiplus.dll", "GdipDeleteMatrix", reinterpret_cast<void*>(&GdipDeleteMatrix));
    loader.registerExport("gdiplus.dll", "GdipSetWorldTransform", reinterpret_cast<void*>(&GdipSetWorldTransform));
    loader.registerExport("gdiplus.dll", "GdipResetWorldTransform", reinterpret_cast<void*>(&GdipResetWorldTransform));
    loader.registerExport("gdiplus.dll", "GdipTranslateWorldTransform", reinterpret_cast<void*>(&GdipTranslateWorldTransform));
    loader.registerExport("gdiplus.dll", "GdipScaleWorldTransform", reinterpret_cast<void*>(&GdipScaleWorldTransform));
    loader.registerExport("gdiplus.dll", "GdipRotateWorldTransform", reinterpret_cast<void*>(&GdipRotateWorldTransform));
    loader.registerExport("gdiplus.dll", "GdipTranslateMatrix", reinterpret_cast<void*>(&GdipTranslateMatrix));
    loader.registerExport("gdiplus.dll", "GdipScaleMatrix", reinterpret_cast<void*>(&GdipScaleMatrix));
    loader.registerExport("gdiplus.dll", "GdipRotateMatrix", reinterpret_cast<void*>(&GdipRotateMatrix));
    loader.registerExport("gdiplus.dll", "GdipInvertMatrix", reinterpret_cast<void*>(&GdipInvertMatrix));
    loader.registerExport("gdiplus.dll", "GdipTransformMatrixPoints", reinterpret_cast<void*>(&GdipTransformMatrixPoints));

    loader.registerExport("gdiplus.dll", "GdipCreatePath", reinterpret_cast<void*>(&GdipCreatePath));
    loader.registerExport("gdiplus.dll", "GdipDeletePath", reinterpret_cast<void*>(&GdipDeletePath));
    loader.registerExport("gdiplus.dll", "GdipResetPath", reinterpret_cast<void*>(&GdipResetPath));
    loader.registerExport("gdiplus.dll", "GdipAddPathRectangleI", reinterpret_cast<void*>(&GdipAddPathRectangleI));
    loader.registerExport("gdiplus.dll", "GdipDrawPath", reinterpret_cast<void*>(&GdipDrawPath));
    loader.registerExport("gdiplus.dll", "GdipFillPath", reinterpret_cast<void*>(&GdipFillPath));
    loader.registerExport("gdiplus.dll", "GdipWindingModeOutline", reinterpret_cast<void*>(&GdipWindingModeOutline));

    loader.registerExport("gdiplus.dll", "GdipCreatePen1", reinterpret_cast<void*>(&GdipCreatePen1));
    loader.registerExport("gdiplus.dll", "GdipCreatePen2", reinterpret_cast<void*>(&GdipCreatePen2));
    loader.registerExport("gdiplus.dll", "GdipDeletePen", reinterpret_cast<void*>(&GdipDeletePen));
    loader.registerExport("gdiplus.dll", "GdipSetPenDashStyle", reinterpret_cast<void*>(&GdipSetPenDashStyle));
    loader.registerExport("gdiplus.dll", "GdipCreateSolidFill", reinterpret_cast<void*>(&GdipCreateSolidFill));
    loader.registerExport("gdiplus.dll", "GdipSetSolidFillColor", reinterpret_cast<void*>(&GdipSetSolidFillColor));
    loader.registerExport("gdiplus.dll", "GdipCreateHatchBrush", reinterpret_cast<void*>(&GdipCreateHatchBrush));
    loader.registerExport("gdiplus.dll", "GdipCloneBrush", reinterpret_cast<void*>(&GdipCloneBrush));
    loader.registerExport("gdiplus.dll", "GdipDeleteBrush", reinterpret_cast<void*>(&GdipDeleteBrush));

    loader.registerExport("gdiplus.dll", "GdipCreateRegion", reinterpret_cast<void*>(&GdipCreateRegion));
    loader.registerExport("gdiplus.dll", "GdipDeleteRegion", reinterpret_cast<void*>(&GdipDeleteRegion));
    loader.registerExport("gdiplus.dll", "GdipGetRegionBounds", reinterpret_cast<void*>(&GdipGetRegionBounds));
    loader.registerExport("gdiplus.dll", "GdipGetRegionHRgn", reinterpret_cast<void*>(&GdipGetRegionHRgn));
    loader.registerExport("gdiplus.dll", "GdipGetClip", reinterpret_cast<void*>(&GdipGetClip));

    loader.registerExport("gdiplus.dll", "GdipCreateFromHDC", reinterpret_cast<void*>(&GdipCreateFromHDC));
    loader.registerExport("gdiplus.dll", "GdipDeleteGraphics", reinterpret_cast<void*>(&GdipDeleteGraphics));
    loader.registerExport("gdiplus.dll", "GdipSetPageUnit", reinterpret_cast<void*>(&GdipSetPageUnit));
    loader.registerExport("gdiplus.dll", "GdipSetSmoothingMode", reinterpret_cast<void*>(&GdipSetSmoothingMode));
    loader.registerExport("gdiplus.dll", "GdipSetInterpolationMode", reinterpret_cast<void*>(&GdipSetInterpolationMode));
    loader.registerExport("gdiplus.dll", "GdipSetCompositingQuality", reinterpret_cast<void*>(&GdipSetCompositingQuality));
    loader.registerExport("gdiplus.dll", "GdipSetCompositingMode", reinterpret_cast<void*>(&GdipSetCompositingMode));
    loader.registerExport("gdiplus.dll", "GdipSetTextRenderingHint", reinterpret_cast<void*>(&GdipSetTextRenderingHint));
    loader.registerExport("gdiplus.dll", "GdipGetDC", reinterpret_cast<void*>(&GdipGetDC));
    loader.registerExport("gdiplus.dll", "GdipReleaseDC", reinterpret_cast<void*>(&GdipReleaseDC));
    loader.registerExport("gdiplus.dll", "GdipGetImageGraphicsContext", reinterpret_cast<void*>(&GdipGetImageGraphicsContext));
    loader.registerExport("gdiplus.dll", "GdipDrawLine", reinterpret_cast<void*>(&GdipDrawLine));
    loader.registerExport("gdiplus.dll", "GdipDrawLineI", reinterpret_cast<void*>(&GdipDrawLineI));
    loader.registerExport("gdiplus.dll", "GdipDrawRectangleI", reinterpret_cast<void*>(&GdipDrawRectangleI));
    loader.registerExport("gdiplus.dll", "GdipFillRectangle", reinterpret_cast<void*>(&GdipFillRectangle));
    loader.registerExport("gdiplus.dll", "GdipFillRectangleI", reinterpret_cast<void*>(&GdipFillRectangleI));
    loader.registerExport("gdiplus.dll", "GdipFillEllipseI", reinterpret_cast<void*>(&GdipFillEllipseI));

    loader.registerExport("gdiplus.dll", "GdipCreateBitmapFromScan0", reinterpret_cast<void*>(&GdipCreateBitmapFromScan0));
    loader.registerExport("gdiplus.dll", "GdipCreateBitmapFromGraphics", reinterpret_cast<void*>(&GdipCreateBitmapFromGraphics));
    loader.registerExport("gdiplus.dll", "GdipCreateBitmapFromStream", reinterpret_cast<void*>(&GdipCreateBitmapFromStream));
    loader.registerExport("gdiplus.dll", "GdipCreateBitmapFromGdiDib", reinterpret_cast<void*>(&GdipCreateBitmapFromGdiDib));
    loader.registerExport("gdiplus.dll", "GdipCreateBitmapFromHBITMAP", reinterpret_cast<void*>(&GdipCreateBitmapFromHBITMAP));
    loader.registerExport("gdiplus.dll", "GdipCreateHBITMAPFromBitmap", reinterpret_cast<void*>(&GdipCreateHBITMAPFromBitmap));
    loader.registerExport("gdiplus.dll", "GdipCloneBitmapAreaI", reinterpret_cast<void*>(&GdipCloneBitmapAreaI));
    loader.registerExport("gdiplus.dll", "GdipCloneImage", reinterpret_cast<void*>(&GdipCloneImage));
    loader.registerExport("gdiplus.dll", "GdipDisposeImage", reinterpret_cast<void*>(&GdipDisposeImage));
    loader.registerExport("gdiplus.dll", "GdipGetImageWidth", reinterpret_cast<void*>(&GdipGetImageWidth));
    loader.registerExport("gdiplus.dll", "GdipGetImageHeight", reinterpret_cast<void*>(&GdipGetImageHeight));
    loader.registerExport("gdiplus.dll", "GdipLoadImageFromFile", reinterpret_cast<void*>(&GdipLoadImageFromFile));
    loader.registerExport("gdiplus.dll", "GdipGetImagePixelFormat", reinterpret_cast<void*>(&GdipGetImagePixelFormat));
    loader.registerExport("gdiplus.dll", "GdipGetImageHorizontalResolution", reinterpret_cast<void*>(&GdipGetImageHorizontalResolution));
    loader.registerExport("gdiplus.dll", "GdipBitmapSetResolution", reinterpret_cast<void*>(&GdipBitmapSetResolution));
    loader.registerExport("gdiplus.dll", "GdipBitmapLockBits", reinterpret_cast<void*>(&GdipBitmapLockBits));
    loader.registerExport("gdiplus.dll", "GdipBitmapUnlockBits", reinterpret_cast<void*>(&GdipBitmapUnlockBits));
    loader.registerExport("gdiplus.dll", "GdipDrawImageI", reinterpret_cast<void*>(&GdipDrawImageI));
    loader.registerExport("gdiplus.dll", "GdipDrawImageRectRect", reinterpret_cast<void*>(&GdipDrawImageRectRect));
    loader.registerExport("gdiplus.dll", "GdipDrawImageRectRectI", reinterpret_cast<void*>(&GdipDrawImageRectRectI));
    loader.registerExport("gdiplus.dll", "GdipImageRotateFlip", reinterpret_cast<void*>(&GdipImageRotateFlip));
    loader.registerExport("gdiplus.dll", "GdipSaveImageToFile", reinterpret_cast<void*>(&GdipSaveImageToFile));
    loader.registerExport("gdiplus.dll", "GdipImageGetFrameCount", reinterpret_cast<void*>(&GdipImageGetFrameCount));
    loader.registerExport("gdiplus.dll", "GdipImageSelectActiveFrame", reinterpret_cast<void*>(&GdipImageSelectActiveFrame));
    loader.registerExport("gdiplus.dll", "GdipGetPropertyItemSize", reinterpret_cast<void*>(&GdipGetPropertyItemSize));
    loader.registerExport("gdiplus.dll", "GdipGetPropertyItem", reinterpret_cast<void*>(&GdipGetPropertyItem));
    loader.registerExport("gdiplus.dll", "GdipSetPropertyItem", reinterpret_cast<void*>(&GdipSetPropertyItem));
    loader.registerExport("gdiplus.dll", "GdipGetImageEncodersSize", reinterpret_cast<void*>(&GdipGetImageEncodersSize));
    loader.registerExport("gdiplus.dll", "GdipGetImageEncoders", reinterpret_cast<void*>(&GdipGetImageEncoders));

    loader.registerExport("gdiplus.dll", "GdipCreateImageAttributes", reinterpret_cast<void*>(&GdipCreateImageAttributes));
    loader.registerExport("gdiplus.dll", "GdipDisposeImageAttributes", reinterpret_cast<void*>(&GdipDisposeImageAttributes));
    loader.registerExport("gdiplus.dll", "GdipSetImageAttributesWrapMode", reinterpret_cast<void*>(&GdipSetImageAttributesWrapMode));

    loader.registerExport("gdiplus.dll", "GdipCreateFontFamilyFromName", reinterpret_cast<void*>(&GdipCreateFontFamilyFromName));
    loader.registerExport("gdiplus.dll", "GdipDeleteFontFamily", reinterpret_cast<void*>(&GdipDeleteFontFamily));
    loader.registerExport("gdiplus.dll", "GdipGetGenericFontFamilySansSerif", reinterpret_cast<void*>(&GdipGetGenericFontFamilySansSerif));
    loader.registerExport("gdiplus.dll", "GdipGetFamilyName", reinterpret_cast<void*>(&GdipGetFamilyName));
    loader.registerExport("gdiplus.dll", "GdipCreateFont", reinterpret_cast<void*>(&GdipCreateFont));
    loader.registerExport("gdiplus.dll", "GdipCreateFontFromDC", reinterpret_cast<void*>(&GdipCreateFontFromDC));
    loader.registerExport("gdiplus.dll", "GdipCreateFontFromLogfontA", reinterpret_cast<void*>(&GdipCreateFontFromLogfontA));
    loader.registerExport("gdiplus.dll", "GdipDeleteFont", reinterpret_cast<void*>(&GdipDeleteFont));
    loader.registerExport("gdiplus.dll", "GdipGetFamily", reinterpret_cast<void*>(&GdipGetFamily));
    loader.registerExport("gdiplus.dll", "GdipGetFontHeight", reinterpret_cast<void*>(&GdipGetFontHeight));
    loader.registerExport("gdiplus.dll", "GdipGetLogFontW", reinterpret_cast<void*>(&GdipGetLogFontW));
    loader.registerExport("gdiplus.dll", "GdipCreateStringFormat", reinterpret_cast<void*>(&GdipCreateStringFormat));
    loader.registerExport("gdiplus.dll", "GdipCloneStringFormat", reinterpret_cast<void*>(&GdipCloneStringFormat));
    loader.registerExport("gdiplus.dll", "GdipDeleteStringFormat", reinterpret_cast<void*>(&GdipDeleteStringFormat));
    loader.registerExport("gdiplus.dll", "GdipStringFormatGetGenericDefault", reinterpret_cast<void*>(&GdipStringFormatGetGenericDefault));
    loader.registerExport("gdiplus.dll", "GdipStringFormatGetGenericTypographic", reinterpret_cast<void*>(&GdipStringFormatGetGenericTypographic));
    loader.registerExport("gdiplus.dll", "GdipSetStringFormatFlags", reinterpret_cast<void*>(&GdipSetStringFormatFlags));
    loader.registerExport("gdiplus.dll", "GdipGetStringFormatFlags", reinterpret_cast<void*>(&GdipGetStringFormatFlags));
    loader.registerExport("gdiplus.dll", "GdipSetStringFormatAlign", reinterpret_cast<void*>(&GdipSetStringFormatAlign));
    loader.registerExport("gdiplus.dll", "GdipSetStringFormatLineAlign", reinterpret_cast<void*>(&GdipSetStringFormatLineAlign));
    loader.registerExport("gdiplus.dll", "GdipSetStringFormatTrimming", reinterpret_cast<void*>(&GdipSetStringFormatTrimming));
    loader.registerExport("gdiplus.dll", "GdipSetStringFormatMeasurableCharacterRanges", reinterpret_cast<void*>(&GdipSetStringFormatMeasurableCharacterRanges));
    loader.registerExport("gdiplus.dll", "GdipMeasureCharacterRanges", reinterpret_cast<void*>(&GdipMeasureCharacterRanges));
    loader.registerExport("gdiplus.dll", "GdipDrawString", reinterpret_cast<void*>(&GdipDrawString));
    loader.registerExport("gdiplus.dll", "GdipMeasureString", reinterpret_cast<void*>(&GdipMeasureString));
    loader.registerExport("gdiplus.dll", "GdipAddPathArcI", reinterpret_cast<void*>(&GdipAddPathArcI));
    loader.registerExport("gdiplus.dll", "GdipClosePathFigure", reinterpret_cast<void*>(&GdipClosePathFigure));
    loader.registerExport("gdiplus.dll", "GdipAddPathLineI", reinterpret_cast<void*>(&GdipAddPathLineI));
    loader.registerExport("gdiplus.dll", "GdipAddPathBezierI", reinterpret_cast<void*>(&GdipAddPathBezierI));
    loader.registerExport("gdiplus.dll", "GdipStartPathFigure", reinterpret_cast<void*>(&GdipStartPathFigure));
    loader.registerExport("gdiplus.dll", "GdipDrawBezierI", reinterpret_cast<void*>(&GdipDrawBezierI));
    loader.registerExport("gdiplus.dll", "GdipDrawImageRectI", reinterpret_cast<void*>(&GdipDrawImageRectI));
    loader.registerExport("gdiplus.dll", "GdipGetImagePalette", reinterpret_cast<void*>(&GdipGetImagePalette));
    loader.registerExport("gdiplus.dll", "GdipGetImagePaletteSize", reinterpret_cast<void*>(&GdipGetImagePaletteSize));
    loader.registerExport("gdiplus.dll", "GdipCreateBitmapFromFile", reinterpret_cast<void*>(&GdipCreateBitmapFromFile));
    loader.registerExport("gdiplus.dll", "GdipSaveImageToStream", reinterpret_cast<void*>(&GdipSaveImageToStream));
    loader.registerExport("gdiplus.dll", "GdipDrawLinesI", reinterpret_cast<void*>(&GdipDrawLinesI));
    loader.registerExport("gdiplus.dll", "DllCanUnloadNow", reinterpret_cast<void*>(&DllCanUnloadNow));

    // windowscodecs.dll exports
    loader.registerExport("windowscodecs.dll", "WICCreateImagingFactory_Proxy", reinterpret_cast<void*>(&WICCreateImagingFactory_Proxy));
    loader.registerExport("windowscodecs.dll", "DllCanUnloadNow", reinterpret_cast<void*>(&DllCanUnloadNow));
    loader.registerExport("windowscodecs.dll", "WICConvertBitmapSource", reinterpret_cast<void*>(+[](const void* /*dstFormat*/, void* /*pISrc*/, void** ppIDst) noexcept -> int32_t {
        if (ppIDst) *ppIDst = reinterpret_cast<void*>(0xCAFE);
        return 0; // S_OK
    }));

    // COM Class Factory registration for WIC Imaging Factory
    auto& com = ole32::ComRuntime::get();
    uint32_t regCookie = 0;
    auto* wicFact = new CGdiPlusClassFactory<CWICImagingFactory>();
    com.RegisterClassObject(CLSID_WICImagingFactory, wicFact, 1, 1, &regCookie);
    wicFact->Release();
}

} // namespace micant::gdiplus
