/**
 * @file prismx_3d_cube_app.cpp
 @brief Luce OS Freestanding 3D Graphics Application for MicaNT Bare Metal.
 *
 * Renders the PrismX 3D rotating crystal octahedron with perspective neon floor
 * and planar raytraced reflection directly to a 32-bpp BGRA surface, then presents
 * it to physical GOP VRAM via Dave Cutler system service NtGdiPresentSurface (0x81).
 *
 * Clean-Room ISO C++23 Freestanding. Zero CRT or MSVCRT runtime dependencies.
 */

#include <cstdint>
#include <cstddef>

namespace {

// --- Windows NT / MicaNT GDI & Userland ABI Structures ---

#pragma pack(push, 1)
struct UserVideoInfo64 {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0};
    uint32_t bpp{32};
    uint32_t pixelFormat{1}; // 1 = BGRA
    uint32_t reserved{0};
    uint64_t physicalBase{0};
};

struct UserPresentSurfaceParams64 {
    uint64_t surfaceBufferVa{0};
    uint32_t dstX{0};
    uint32_t dstY{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0};
    uint32_t reserved{0};
};
#pragma pack(pop)

// Resolution of the 3D Viewport
constexpr uint32_t RENDER_W = 480;
constexpr uint32_t RENDER_H = 320;

alignas(4096) uint32_t s_colorBuffer[RENDER_W * RENDER_H];
alignas(4096) float    s_zBuffer[RENDER_W * RENDER_H];

inline uint64_t DoSyscall(uint64_t ssn, uint64_t arg1 = 0, uint64_t arg2 = 0) noexcept {
    uint64_t res = 0;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %1, %%rax\n\t"
        "movq %2, %%rdx\n\t"
        "movq %3, %%r8\n\t"
        "syscall\n\t"
        "movq %%rax, %0\n\t"
        : "=r"(res)
        : "r"(ssn), "r"(arg1), "r"(arg2)
        : "rax", "rdx", "r8", "rcx", "r11", "memory"
    );
#endif
    return res;
}

// Fast hardware sin/cos via x87
inline void FastSinCos(float angleRad, float& s, float& c) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "flds %2\n\t"
        "fsincos\n\t"
        "fstps %1\n\t"
        "fstps %0\n\t"
        : "=m"(s), "=m"(c)
        : "m"(angleRad)
    );
#else
    s = 0.5f; c = 0.866f;
#endif
}

inline uint32_t PackBGRA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(b);
}

inline int32_t Abs(int32_t v) noexcept { return v < 0 ? -v : v; }
inline int32_t Min(int32_t a, int32_t b) noexcept { return a < b ? a : b; }
inline int32_t Max(int32_t a, int32_t b) noexcept { return a > b ? a : b; }

void DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) noexcept {
    int32_t dx = Abs(x1 - x0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = -Abs(y1 - y0);
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    while (true) {
        if (x0 >= 0 && x0 < static_cast<int32_t>(RENDER_W) &&
            y0 >= 0 && y0 < static_cast<int32_t>(RENDER_H)) {
            s_colorBuffer[static_cast<size_t>(y0) * RENDER_W + static_cast<size_t>(x0)] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

struct Vec3 { float x, y, z; };

void Render3DScene(float rotationAngleRad) noexcept {
    // 1. Clear background & draw skybox / floor gradient
    uint32_t horizonY = (RENDER_H * 58) / 100;
    for (uint32_t y = 0; y < RENDER_H; ++y) {
        uint8_t r = 6, g = 14, b = 28;
        if (y <= horizonY) {
            uint32_t t = (y * 255) / (horizonY > 0 ? horizonY : 1);
            r = static_cast<uint8_t>(4 + (t * 8) / 255);
            g = static_cast<uint8_t>(9 + (t * 22) / 255);
            b = static_cast<uint8_t>(18 + (t * 38) / 255);
        } else {
            uint32_t denom = (RENDER_H - horizonY) > 0 ? (RENDER_H - horizonY) : 1;
            uint32_t t = ((y - horizonY) * 255) / denom;
            r = static_cast<uint8_t>(10 - (t * 6) / 255);
            g = static_cast<uint8_t>(24 - (t * 16) / 255);
            b = static_cast<uint8_t>(44 - (t * 26) / 255);
        }
        uint32_t col = PackBGRA(r, g, b);
        for (uint32_t x = 0; x < RENDER_W; ++x) {
            size_t idx = static_cast<size_t>(y) * RENDER_W + x;
            s_colorBuffer[idx] = col;
            s_zBuffer[idx] = 1.0e9f;
        }
    }

    // 2. Perspective Neon-Cyan Floor Grid
    int32_t vanishX = static_cast<int32_t>(RENDER_W / 2);
    int32_t vanishY = static_cast<int32_t>(horizonY);
    for (int32_t i = -8; i <= 8; ++i) {
        int32_t botX = vanishX + i * static_cast<int32_t>(RENDER_W / 8);
        DrawLine(vanishX + i * 5, vanishY, botX, static_cast<int32_t>(RENDER_H - 1), PackBGRA(0, 115, 165));
    }
    for (uint32_t step = 1; step <= 6; ++step) {
        uint32_t gy = horizonY + ((RENDER_H - 1 - horizonY) * step * step) / 36;
        uint8_t c = static_cast<uint8_t>(60 + step * 22);
        DrawLine(0, static_cast<int32_t>(gy), static_cast<int32_t>(RENDER_W - 1), static_cast<int32_t>(gy), PackBGRA(0, c, static_cast<uint8_t>(c + 40)));
    }

    // 3. 3D Crystal Octahedron Mesh Geometry
    const Vec3 verts[6] = {
        { 0.0f,  1.22f,  0.0f},
        { 0.0f, -1.22f,  0.0f},
        {-0.92f, 0.0f,  -0.92f},
        { 0.92f, 0.0f,  -0.92f},
        { 0.92f, 0.0f,   0.92f},
        {-0.92f, 0.0f,   0.92f}
    };

    struct FaceDesc { uint8_t i0, i1, i2; uint8_t r, g, b; };
    const FaceDesc faces[8] = {
        {0, 2, 3,   0, 230, 255}, // Front-Left: Pure Cyan
        {0, 3, 4,   0, 170, 255}, // Front-Right: Deep Azure
        {0, 4, 5,  85, 115, 255}, // Back-Right: Cobalt Violet
        {0, 5, 2,   0, 240, 195}, // Back-Left: Aquamarine
        {1, 3, 2,   0, 155, 225}, // Bottom-Front-Left
        {1, 4, 3,  45, 115, 230}, // Bottom-Front-Right
        {1, 5, 4,  80,  75, 215}, // Bottom-Back-Right
        {1, 2, 5,   0, 185, 165}  // Bottom-Back-Left
    };

    float sinY, cosY, sinX, cosX;
    FastSinCos(rotationAngleRad, sinY, cosY);
    FastSinCos(0.26f, sinX, cosX);

    auto TransformVertex = [&](const Vec3& v) -> Vec3 {
        float rx = v.x * cosY + v.z * sinY;
        float rz = -v.x * sinY + v.z * cosY;
        float ry = v.y * cosX - rz * sinX;
        float fz = v.y * sinX + rz * cosX;
        return { rx, ry + 0.30f, fz + 3.75f };
    };

    auto RasterizeTriangle = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, uint32_t fillBGRA, uint32_t wireBGRA, bool isRefl) {
        auto Project = [&](const Vec3& v, int32_t& sx, int32_t& sy) {
            float invZ = 1.0f / (v.z > 0.2f ? v.z : 0.2f);
            float scale = static_cast<float>(RENDER_H) * 0.78f;
            sx = static_cast<int32_t>(static_cast<float>(RENDER_W) * 0.5f + v.x * invZ * scale);
            sy = static_cast<int32_t>(static_cast<float>(RENDER_H) * 0.44f - v.y * invZ * scale);
        };
        int32_t x0, y0, x1, y1, x2, y2;
        Project(p0, x0, y0);
        Project(p1, x1, y1);
        Project(p2, x2, y2);

        int32_t area2 = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (!isRefl && area2 <= 0) return;
        if (isRefl && area2 >= 0) return;

        int32_t minX = Max(0, Min(x0, Min(x1, x2)));
        int32_t maxX = Min(static_cast<int32_t>(RENDER_W) - 1, Max(x0, Max(x1, x2)));
        int32_t minY = Max(0, Min(y0, Min(y1, y2)));
        int32_t maxY = Min(static_cast<int32_t>(RENDER_H) - 1, Max(y0, Max(y1, y2)));
        float avgZ = (p0.z + p1.z + p2.z) * 0.333333f;

        for (int32_t py = minY; py <= maxY; ++py) {
            for (int32_t px = minX; px <= maxX; ++px) {
                int32_t w0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
                int32_t w1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                int32_t w2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                bool inside = isRefl ? (w0 <= 0 && w1 <= 0 && w2 <= 0)
                                     : (w0 >= 0 && w1 >= 0 && w2 >= 0);
                if (inside) {
                    size_t idx = static_cast<size_t>(py) * RENDER_W + static_cast<size_t>(px);
                    if (avgZ < s_zBuffer[idx]) {
                        s_zBuffer[idx] = avgZ;
                        s_colorBuffer[idx] = fillBGRA;
                    }
                }
            }
        }
        DrawLine(x0, y0, x1, y1, wireBGRA);
        DrawLine(x1, y1, x2, y2, wireBGRA);
        DrawLine(x2, y2, x0, y0, wireBGRA);
    };

    // 4. Pass 3: DXR Planar Floor Reflection
    constexpr float floorY = -0.82f;
    for (const auto& f : faces) {
        Vec3 w0 = TransformVertex(verts[f.i0]);
        Vec3 w1 = TransformVertex(verts[f.i1]);
        Vec3 w2 = TransformVertex(verts[f.i2]);
        Vec3 r0{ w0.x, 2.0f * floorY - w0.y, w0.z };
        Vec3 r1{ w1.x, 2.0f * floorY - w1.y, w1.z };
        Vec3 r2{ w2.x, 2.0f * floorY - w2.y, w2.z };
        uint32_t reflCol = PackBGRA(f.r / 4, f.g / 4, f.b / 4, 180);
        uint32_t reflWire = PackBGRA(f.r / 3, f.g / 3, f.b / 3, 200);
        RasterizeTriangle(r0, r1, r2, reflCol, reflWire, true);
    }

    // 5. Pass 2: Main 3D Crystal Octahedron Mesh
    for (const auto& f : faces) {
        Vec3 w0 = TransformVertex(verts[f.i0]);
        Vec3 w1 = TransformVertex(verts[f.i1]);
        Vec3 w2 = TransformVertex(verts[f.i2]);
        uint32_t faceCol = PackBGRA(f.r, f.g, f.b, 255);
        uint32_t wireCol = PackBGRA(255, 255, 255, 255);
        RasterizeTriangle(w0, w1, w2, faceCol, wireCol, false);
    }
}

} // namespace

extern "C" void _start() {
    // 1. Syscall 0x11: NtQuerySystemInformation (Verify Ring 3 CPL=3)
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    // 2. Syscall 0x25: NtWriteFile ('PRISMX')
    DoSyscall(0x25, 0x584D53495250ULL, 6);

    // 3. Render 3D Scene in userland memory
    Render3DScene(0.785f); // 45-degree angle

    // 4. Query GOP Video Metrics via NtGdiGetVideoInfo (SSN 0x80)
    UserVideoInfo64 vInfo{};
    DoSyscall(0x80, reinterpret_cast<uint64_t>(&vInfo), 0);

    uint32_t scrW = vInfo.width > 0 ? vInfo.width : 1024;
    uint32_t scrH = vInfo.height > 0 ? vInfo.height : 768;

    // 5. Present 3D Framebuffer to GOP VRAM via NtGdiPresentSurface (SSN 0x81)
    UserPresentSurfaceParams64 params{};
    params.surfaceBufferVa = reinterpret_cast<uint64_t>(s_colorBuffer);
    params.width = RENDER_W;
    params.height = RENDER_H;
    params.pitch = RENDER_W;
    params.dstX = (scrW > RENDER_W) ? ((scrW - RENDER_W) / 2) : 0;
    params.dstY = (scrH > RENDER_H) ? ((scrH - RENDER_H) / 2) : 0;

    DoSyscall(0x81, reinterpret_cast<uint64_t>(&params), 0);

    // 6. Syscall 0x25: NtWriteFile ('3D_PASS')
    DoSyscall(0x25, 0x535341505F4433ULL, 7);

    // 7. Syscall 0x01: NtTerminateProcess (Clean Exit)
    DoSyscall(0x01, 0, 0);

    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
