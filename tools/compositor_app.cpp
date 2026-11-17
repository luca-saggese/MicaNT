#include "micant/shared/compositor_surface.h"
#include "micant/shared/font8x8.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/pixel_composite.h"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/shared/system_information.h"
#include "liquid_glass_effect.hpp"

#include <stddef.h>
#include <stdint.h>

#ifndef MICANT_COMPOSITOR_LOCAL_STATS
#define MICANT_COMPOSITOR_LOCAL_STATS 0
#endif

// Set to 0 (or pass -DMICANT_COMPOSITOR_DEBUG_HUD=0) to disable the HUD.
#ifndef MICANT_COMPOSITOR_DEBUG_HUD
#define MICANT_COMPOSITOR_DEBUG_HUD 1
#endif

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<volatile uint8_t*>(destination);
    for (size_t index = 0; index < count; ++index)
        bytes[index] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<volatile uint8_t*>(destination);
    const auto* input = static_cast<const volatile uint8_t*>(source);
    for (size_t index = 0; index < count; ++index) output[index] = input[index];
    return destination;
}

namespace {

uint32_t* g_frame{};
micant::compositor::glass::Cache g_liquidGlassCache{};
constexpr int64_t kWindowEffectRadiusLimit = 32;
constexpr int64_t kWindowBlurRadius = 2;
constexpr size_t kDamageRegionComplexityBudget = 16;
uint32_t g_windowCornerBackdrop[4][kWindowEffectRadiusLimit *
                                   kWindowEffectRadiusLimit]{};
uint32_t* g_windowBlurBackdrop{};
uint16_t* g_windowBlurRowSums{};
uint32_t g_displayWidth{};
uint32_t g_displayHeight{};

struct LocalPerformanceStats {
    uint64_t composeMilliseconds{};
    uint64_t presentMilliseconds{};
    uint64_t fullRedraws{};
    uint64_t partialRedraws{};
    uint64_t pixelsComposited{};
    uint64_t bytesTransferred{};
    uint64_t surfaceCount{};
    uint64_t frames{};
    uint64_t wakeups{};
    uint64_t failedPresents{};
    uint64_t lastReportMilliseconds{};
};
LocalPerformanceStats g_localPerformanceStats{};

bool allocateZeroed(size_t bytes, void*& allocation) noexcept {
    allocation = nullptr;
    if (!bytes) return false;
    size_t regionSize = bytes;
    return micant::native_nt::NtAllocateVirtualMemory(
        static_cast<micant::Handle>(-1), &allocation, 0, &regionSize,
        0x3000u, 0x04u) == micant::NtStatus::Success && allocation;
}
void releaseAllocation(void* allocation) noexcept {
    if (!allocation) return;
    size_t regionSize{};
    void* base = allocation;
    (void)micant::native_nt::NtFreeVirtualMemory(
        static_cast<micant::Handle>(-1), &base, &regionSize, 0x8000u);
}
MicaCompositorMetadataStore g_metadataSnapshot{};
MicaCompositorSurfaceInfo* g_snapshotSurfaces{};
size_t g_snapshotSurfaceCapacity{};
MicaCompositorDirtyRect* g_snapshotDirtyRegions{};
size_t g_snapshotDirtyCapacity{};
bool g_presentFailureReported{};
bool g_hasPresentedFrame{};
uint64_t g_lastThemeGeneration{};

struct ScreenRect {
    int64_t left{}, top{}, right{}, bottom{};
};

ScreenRect g_composeClip{};
uint8_t g_windowDrawOpacity{255};
constexpr uint64_t kWindowAnimationDurationMs = 240;
constexpr uint64_t kAnimationFrameInterval100ns = 160000;

struct AnimationFrame {
    bool active{};
    int32_t x{}, y{};
    uint32_t width{}, height{};
    ScreenRect damage[2]{};
    size_t damageCount{};
};

struct AnimationRuntime {
    uint64_t window{}, generation{}, startMilliseconds{};
    ScreenRect lastBounds{};
    bool active{}, hasLastBounds{};
};

struct OrderedSurface {
    const MicaCompositorSurfaceInfo* info{};
    size_t index{};
};
AnimationRuntime* g_animationRuntime{};
AnimationFrame* g_animationFrames{};
size_t g_animationCapacity{};
OrderedSurface* g_orderedSurfaces{};
size_t g_orderedSurfaceCapacity{};
MicaCompositorMetadataStore g_effectProbeMetadata{};
MicaCompositorSurfaceInfo g_effectProbeSurfaces[2]{};
AnimationFrame g_effectProbeAnimations[2]{};
uint32_t g_effectProbePixels[128u * 128u + 64u * 64u]{};

bool ensureAnimationCapacity(size_t required) noexcept {
    if (required <= g_animationCapacity) return true;
    if (required > SIZE_MAX / sizeof(AnimationRuntime) ||
        required > SIZE_MAX / sizeof(AnimationFrame)) return false;
    void* runtimes{};
    void* frames{};
    if (!allocateZeroed(required * sizeof(AnimationRuntime), runtimes) ||
        !allocateZeroed(required * sizeof(AnimationFrame), frames)) {
        releaseAllocation(runtimes);
        releaseAllocation(frames);
        return false;
    }
    auto* newRuntimes = static_cast<AnimationRuntime*>(runtimes);
    auto* newFrames = static_cast<AnimationFrame*>(frames);
    for (size_t index = 0; index < g_animationCapacity; ++index)
        newRuntimes[index] = g_animationRuntime[index];
    releaseAllocation(g_animationRuntime);
    releaseAllocation(g_animationFrames);
    g_animationRuntime = newRuntimes;
    g_animationFrames = newFrames;
    g_animationCapacity = required;
    return true;
}

bool ensureOrderedSurfaceCapacity(size_t required) noexcept {
    if (required <= g_orderedSurfaceCapacity) return true;
    if (required > SIZE_MAX / sizeof(OrderedSurface)) return false;
    void* replacement{};
    if (!allocateZeroed(required * sizeof(OrderedSurface), replacement)) return false;
    releaseAllocation(g_orderedSurfaces);
    g_orderedSurfaces = static_cast<OrderedSurface*>(replacement);
    g_orderedSurfaceCapacity = required;
    return true;
}

uint64_t animationClockMilliseconds() noexcept {
    const auto* shared = reinterpret_cast<volatile const micant::KUserSharedData*>(
        micant::UserSharedDataAddress);
    return __atomic_load_n(&shared->tickCountQuad, __ATOMIC_ACQUIRE);
}

ScreenRect surfaceBounds(const MicaCompositorSurfaceInfo& surface) noexcept {
    return {surface.x, surface.y,
        static_cast<int64_t>(surface.x) + surface.width,
        static_cast<int64_t>(surface.y) + surface.height};
}

void addAnimationDamage(AnimationFrame& frame, ScreenRect bounds) noexcept {
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) return;
    if (frame.damageCount < sizeof(frame.damage) / sizeof(frame.damage[0]))
        frame.damage[frame.damageCount++] = bounds;
    else {
        auto& merged = frame.damage[0];
        merged.left = merged.left < bounds.left ? merged.left : bounds.left;
        merged.top = merged.top < bounds.top ? merged.top : bounds.top;
        merged.right = merged.right > bounds.right ? merged.right : bounds.right;
        merged.bottom = merged.bottom > bounds.bottom ? merged.bottom : bounds.bottom;
        frame.damageCount = 1;
    }
}

void buildAnimationFrames(const MicaCompositorMetadataStore& metadata,
                          uint64_t nowMilliseconds,
                          AnimationFrame* frames,
                          uint32_t screenWidth,
                          uint32_t screenHeight) noexcept {
    const size_t surfaceCount = metadata.surfaceEntryCount;
    for (size_t index = 0; index < surfaceCount; ++index) {
        auto& frame = frames[index];
        frame = {};
        auto& runtime = g_animationRuntime[index];
        const auto& surface = metadata.surfaces[index];
        const bool windowPresent = surface.window &&
            (surface.flags & MICANT_COMPOSITOR_SURFACE_ACTIVE);
        if (!windowPresent || runtime.window != surface.window) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime = {};
        }
        if (!windowPresent ||
            !(surface.animationFlags & MICANT_COMPOSITOR_ANIMATION_ACTIVE)) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime.active = false;
            continue;
        }

        const auto& target = surface.animationTarget;
        if (target.left < 0 || target.top < 0 ||
            target.right > static_cast<int32_t>(screenWidth) ||
            target.bottom > static_cast<int32_t>(screenHeight) ||
            target.left >= target.right || target.top >= target.bottom ||
            !surface.width || !surface.height) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime.window = surface.window;
            runtime.generation = surface.animationGeneration;
            runtime.active = false;
            continue;
        }

        if (runtime.window != surface.window ||
            runtime.generation != surface.animationGeneration) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime = {};
            runtime.window = surface.window;
            runtime.generation = surface.animationGeneration;
            runtime.startMilliseconds = nowMilliseconds;
            runtime.active = true;
        }
        if (!runtime.active) continue;

        const bool minimizing =
            (surface.animationFlags & MICANT_COMPOSITOR_ANIMATION_MINIMIZE) != 0;
        const ScreenRect windowRect = surfaceBounds(surface);
        const ScreenRect targetRect{target.left, target.top, target.right, target.bottom};
        const ScreenRect from = minimizing ? windowRect : targetRect;
        const ScreenRect to = minimizing ? targetRect : windowRect;
        const uint64_t elapsed = nowMilliseconds >= runtime.startMilliseconds
            ? nowMilliseconds - runtime.startMilliseconds : 0;
        const double linear = elapsed >= kWindowAnimationDurationMs ? 1.0 :
            static_cast<double>(elapsed) / kWindowAnimationDurationMs;
        const double eased = linear * linear * (3.0 - 2.0 * linear);
        const auto lerp = [eased](int64_t a, int64_t b) noexcept {
            return a + static_cast<int64_t>((b - a) * eased);
        };

        if (elapsed >= kWindowAnimationDurationMs) {
            if (runtime.hasLastBounds) addAnimationDamage(frame, runtime.lastBounds);
            addAnimationDamage(frame, to);
            runtime.active = false;
            runtime.hasLastBounds = false;
            continue;
        }

        const int64_t fromWidth = from.right - from.left;
        const int64_t toWidth = to.right - to.left;
        const int64_t fromHeight = from.bottom - from.top;
        const int64_t toHeight = to.bottom - to.top;
        const int64_t curveDelta = to.left + toWidth / 2 - from.left - fromWidth / 2;
        const int64_t curve = static_cast<int64_t>(
            static_cast<double>(curveDelta) * 0.10 * 4.0 * eased * (1.0 - eased));
        const int64_t swoop = static_cast<int64_t>(-18.0 * 4.0 * eased * (1.0 - eased));
        const int64_t left = lerp(from.left, to.left) + curve;
        const int64_t top = lerp(from.top, to.top) + swoop;
        const int64_t width = lerp(fromWidth, toWidth);
        const int64_t height = lerp(fromHeight, toHeight);
        frame.active = true;
        frame.x = static_cast<int32_t>(left);
        frame.y = static_cast<int32_t>(top);
        frame.width = static_cast<uint32_t>(width > 1 ? width : 1);
        frame.height = static_cast<uint32_t>(height > 1 ? height : 1);
        if (runtime.hasLastBounds) addAnimationDamage(frame, runtime.lastBounds);
        const ScreenRect current{left, top, left + width, top + height};
        addAnimationDamage(frame, current);
        runtime.lastBounds = current;
        runtime.hasLastBounds = true;
    }
    for (size_t index = surfaceCount;
         index < g_animationCapacity; ++index) {
        auto& frame = frames[index];
        auto& runtime = g_animationRuntime[index];
        frame = {};
        if (runtime.active && runtime.hasLastBounds)
            addAnimationDamage(frame, runtime.lastBounds);
        runtime = {};
    }
}

void print(const char* text, size_t length) noexcept {
    (void)micant::sys::invokeUserService(0x25,
        reinterpret_cast<uint64_t>(text), length);
}

uint64_t performanceMilliseconds() noexcept {
    micant::LargeInteger counter{}, frequency{};
    if (micant::native_nt::NtQueryPerformanceCounter(&counter, &frequency) !=
            micant::NtStatus::Success || counter.quadPart < 0 ||
        frequency.quadPart <= 0) return 0;
    const uint64_t ticks = static_cast<uint64_t>(counter.quadPart);
    const uint64_t rate = static_cast<uint64_t>(frequency.quadPart);
    return (ticks / rate) * 1000u + ((ticks % rate) * 1000u) / rate;
}

#if MICANT_COMPOSITOR_DEBUG_HUD
void appendPerformanceValue(char*& cursor, uint64_t value) noexcept;
constexpr uint32_t kSystemProcessorPerformanceInformation = 8;
constexpr uint64_t kNtQuerySystemInformationService = 0x36;
constexpr uint32_t kHudWidth = 400;
constexpr uint32_t kHudHeight = 48;
constexpr uint64_t kHudRefreshMilliseconds = 250;
uint64_t g_hudLastRefresh{};
uint64_t g_hudLastFpsSample{};
uint64_t g_hudFrames{};
uint64_t g_hudFps{};
uint64_t g_hudCpuKernel{}, g_hudCpuUser{}, g_hudCpuIdle{};
uint32_t g_hudCpuPercent{};
bool g_hudCpuValid{};
uint64_t g_hudMemoryBytes{};
bool g_hudMemoryValid{}, g_hudCursorValid{};
int32_t g_hudMouseX{}, g_hudMouseY{};

void refreshHudMetrics(uint64_t now) noexcept {
    if (g_hudLastFpsSample && now > g_hudLastFpsSample) {
        g_hudFps = (g_hudFrames * 1000u) / (now - g_hudLastFpsSample);
        g_hudFrames = 0;
    }
    g_hudLastFpsSample = now;

    micant::SystemProcessorPerformanceInformation cpu{};
    const auto cpuStatus = static_cast<uint32_t>(micant::sys::invokeUserService4(
        kNtQuerySystemInformationService, kSystemProcessorPerformanceInformation,
        reinterpret_cast<uint64_t>(&cpu), sizeof(cpu), 0));
    if (cpuStatus == 0 && cpu.KernelTime >= 0 && cpu.UserTime >= 0 && cpu.IdleTime >= 0) {
        const uint64_t kernel = static_cast<uint64_t>(cpu.KernelTime);
        const uint64_t user = static_cast<uint64_t>(cpu.UserTime);
        const uint64_t idle = static_cast<uint64_t>(cpu.IdleTime);
        if ((g_hudCpuKernel || g_hudCpuUser) && kernel >= g_hudCpuKernel &&
            user >= g_hudCpuUser && idle >= g_hudCpuIdle) {
            const uint64_t kernelDelta = kernel - g_hudCpuKernel;
            const uint64_t userDelta = user - g_hudCpuUser;
            const uint64_t idleDelta = idle - g_hudCpuIdle;
            const uint64_t total = kernelDelta + userDelta;
            const uint64_t busy = total >= idleDelta ? total - idleDelta : 0;
            g_hudCpuPercent = total ? static_cast<uint32_t>(
                (busy * 100u + total / 2u) / total) : 0;
        }
        g_hudCpuKernel = kernel; g_hudCpuUser = user; g_hudCpuIdle = idle;
        g_hudCpuValid = true;
    } else {
        g_hudCpuValid = false;
    }
}

void hudAppend(char*& cursor, uint64_t value) noexcept {
    appendPerformanceValue(cursor, value);
}

void drawHud(uint64_t totalMemoryBytes, bool memoryValid,
             bool cursorValid, int32_t mouseX, int32_t mouseY) noexcept {
    const uint32_t width = g_displayWidth < kHudWidth ? g_displayWidth : kHudWidth;
    const uint32_t height = g_displayHeight < kHudHeight ? g_displayHeight : kHudHeight;
    if (!width || !height) return;
    const uint32_t left = g_displayWidth - width;
    constexpr uint32_t background = 0xE8202733u;
    constexpr uint32_t foreground = 0xFFF3F6FAu;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            g_frame[static_cast<size_t>(y) * g_displayWidth + left + x] = background;

    char line[96]{};
    char* cursor = line;
    const auto append = [&cursor](const char* text) noexcept {
        while (*text) *cursor++ = *text++;
    };
    append("FPS "); hudAppend(cursor, g_hudFps);
    append("   CPU ");
    if (g_hudCpuValid) { hudAppend(cursor, g_hudCpuPercent); append("%"); }
    else append("N/A");
    *cursor = 0;
    auto drawText = [&](const char* text, uint32_t x, uint32_t y) noexcept {
        for (; *text; ++text, x += 8) {
            const unsigned char ch = static_cast<unsigned char>(*text);
            if (ch < 32 || ch > 126) continue;
            const uint8_t* glyph = micant::shared::FONT_8X8[ch - 32];
            for (uint32_t row = 0; row < 8 && y + row < height; ++row)
                for (uint32_t col = 0; col < 8 && x + col < width; ++col)
                    if (glyph[row] & (0x80u >> col))
                        g_frame[static_cast<size_t>(y + row) * g_displayWidth + left + x + col] = foreground;
        }
    };
    drawText(line, 8, 7);
    cursor = line;
    append("RAM TOTAL ");
    if (memoryValid) {
        hudAppend(cursor, totalMemoryBytes / (1024u * 1024u));
        append(" MB");
    } else append("N/A");
    append("   MOUSE ");
    if (cursorValid) {
        if (mouseX < 0) *cursor++ = '-';
        hudAppend(cursor, static_cast<uint64_t>(mouseX < 0 ? -static_cast<int64_t>(mouseX) : mouseX));
        append(",");
        if (mouseY < 0) *cursor++ = '-';
        hudAppend(cursor, static_cast<uint64_t>(mouseY < 0 ? -static_cast<int64_t>(mouseY) : mouseY));
    } else append("N/A");
    *cursor = 0;
    drawText(line, 8, 27);
}

void updateHud(uint64_t now) noexcept {
    refreshHudMetrics(now);
    micant::SystemMemoryAccountingInformation memory{};
    const auto memoryStatus = static_cast<uint32_t>(micant::sys::invokeUserService4(
        kNtQuerySystemInformationService, micant::SystemMemoryAccountingInformationClass,
        reinterpret_cast<uint64_t>(&memory), sizeof(memory), 0));
    const bool memoryValid = memoryStatus == 0 &&
        memory.version == micant::SystemMemoryAccountingVersion &&
        (memory.availability & micant::SystemMemoryManagedAvailable) &&
        memory.managedPages <= UINT64_MAX / 4096u;
    MicaWin32kRequest request{};
    request.size = sizeof(request);
    request.version = MICANT_WIN32K_REQUEST_VERSION;
    request.operation = MICANT_WIN32K_OP_GET_CURSOR_POS;
    const bool cursorValid = micant::sys::invokeUserService(0x86,
        reinterpret_cast<uint64_t>(&request)) == 0 && request.result != 0;
    g_hudMemoryBytes = memory.managedPages * 4096u;
    g_hudMemoryValid = memoryValid;
    g_hudCursorValid = cursorValid;
    g_hudMouseX = request.rect.left;
    g_hudMouseY = request.rect.top;
    drawHud(g_hudMemoryBytes, g_hudMemoryValid, g_hudCursorValid,
            g_hudMouseX, g_hudMouseY);
    g_hudLastRefresh = now;
}
#endif

void appendPerformanceValue(char*& cursor, uint64_t value) noexcept {
    char digits[20]{};
    size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count) *cursor++ = digits[--count];
}

void reportLocalPerformanceStats() noexcept {
#if MICANT_COMPOSITOR_LOCAL_STATS
    auto& stats = g_localPerformanceStats;
    const uint64_t now = performanceMilliseconds();
    if (!now || !stats.surfaceCount || (stats.lastReportMilliseconds &&
                 now - stats.lastReportMilliseconds < 5000u)) return;
    stats.lastReportMilliseconds = now;
    char line[320]{};
    char* cursor = line;
    const auto append = [&cursor](const char* text) noexcept {
        while (*text) *cursor++ = *text++;
    };
    append("[MicaNT Compositor Stats] compose_ms=");
    appendPerformanceValue(cursor, stats.composeMilliseconds);
    append(" present_ms="); appendPerformanceValue(cursor, stats.presentMilliseconds);
    append(" full="); appendPerformanceValue(cursor, stats.fullRedraws);
    append(" partial="); appendPerformanceValue(cursor, stats.partialRedraws);
    append(" pixels="); appendPerformanceValue(cursor, stats.pixelsComposited);
    append(" bytes="); appendPerformanceValue(cursor, stats.bytesTransferred);
    append(" surfaces="); appendPerformanceValue(cursor, stats.surfaceCount);
    append(" frames="); appendPerformanceValue(cursor, stats.frames);
    append(" wakes="); appendPerformanceValue(cursor, stats.wakeups);
    append(" failed_present="); appendPerformanceValue(cursor, stats.failedPresents);
    append("\r\n");
    print(line, static_cast<size_t>(cursor - line));
    stats.composeMilliseconds = 0;
    stats.presentMilliseconds = 0;
    stats.fullRedraws = 0;
    stats.partialRedraws = 0;
    stats.pixelsComposited = 0;
    stats.bytesTransferred = 0;
    stats.frames = 0;
    stats.wakeups = 0;
    stats.failedPresents = 0;
#endif
}

uint32_t blend(uint32_t source, uint32_t destination) noexcept {
    const uint32_t alpha = source >> 24;
    if (alpha == 255) return source;
    if (!alpha) return destination;
    const uint32_t inverse = 255 - alpha;
    const uint32_t red = (((source >> 16) & 0xFFu) * alpha +
                          ((destination >> 16) & 0xFFu) * inverse + 127) / 255;
    const uint32_t green = (((source >> 8) & 0xFFu) * alpha +
                            ((destination >> 8) & 0xFFu) * inverse + 127) / 255;
    const uint32_t blue = ((source & 0xFFu) * alpha +
                           (destination & 0xFFu) * inverse + 127) / 255;
    const uint32_t outAlpha = alpha + (((destination >> 24) * inverse + 127) / 255);
    return (outAlpha << 24) | (red << 16) | (green << 8) | blue;
}


void fillRect(int64_t left, int64_t top, int64_t right, int64_t bottom,
              uint32_t color, uint32_t width, uint32_t height) noexcept {
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > width ? width : right;
    bottom = bottom > height ? height : bottom;
    left = left < g_composeClip.left ? g_composeClip.left : left;
    top = top < g_composeClip.top ? g_composeClip.top : top;
    right = right > g_composeClip.right ? g_composeClip.right : right;
    bottom = bottom > g_composeClip.bottom ? g_composeClip.bottom : bottom;
    if (left >= right || top >= bottom) return;
    const size_t rowWidth = static_cast<size_t>(right - left);
    for (int64_t y = top; y < bottom; ++y) {
        auto* row = g_frame + static_cast<size_t>(y) * width +
            static_cast<size_t>(left);
        const uint32_t colorAlpha = color >> 24;
        const uint32_t alpha = (colorAlpha * g_windowDrawOpacity + 127u) / 255u;
        if (alpha == 255) {
            for (size_t x = 0; x < rowWidth; ++x) row[x] = color;
        } else if (alpha) {
            const uint32_t source = (alpha << 24) | (color & 0x00FFFFFFu);
            for (size_t x = 0; x < rowWidth; ++x)
                row[x] = blend(source, row[x]);
        }
    }
}

uint32_t applyPremultipliedOpacity(uint32_t pixel, uint8_t opacity) noexcept {
    const auto scale = [opacity](uint32_t channel) noexcept {
        return (channel * opacity + 127u) / 255u;
    };
    return (scale(pixel >> 24) << 24) |
        (scale((pixel >> 16) & 0xFFu) << 16) |
        (scale((pixel >> 8) & 0xFFu) << 8) |
        scale(pixel & 0xFFu);
}

void captureWindowBlurBackdrop(uint32_t width, uint32_t height) noexcept {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t index = 0; index < pixelCount; ++index)
        g_windowBlurBackdrop[index] = g_frame[index];
}

void applyWindowBackdropBlur(int64_t left, int64_t top,
                             int64_t right, int64_t bottom,
                             uint8_t opacity, uint32_t width,
                             uint32_t height) noexcept {
    left = left < g_composeClip.left ? g_composeClip.left : left;
    top = top < g_composeClip.top ? g_composeClip.top : top;
    right = right > g_composeClip.right ? g_composeClip.right : right;
    bottom = bottom > g_composeClip.bottom ? g_composeClip.bottom : bottom;
    if (left >= right || top >= bottom || !opacity || opacity == 255) return;

    const auto captureHorizontal = [&](int64_t screenY, size_t rowSlot) noexcept {
        if (screenY < 0) screenY = 0;
        if (screenY >= height) screenY = static_cast<int64_t>(height) - 1;
        for (int64_t x = left; x < right; ++x) {
            uint32_t red = 0, green = 0, blue = 0;
            for (int64_t dx = -kWindowBlurRadius; dx <= kWindowBlurRadius; ++dx) {
                int64_t sampleX = x + dx;
                if (sampleX < 0) sampleX = 0;
                if (sampleX >= width) sampleX = static_cast<int64_t>(width) - 1;
                const uint32_t pixel = g_windowBlurBackdrop[
                    static_cast<size_t>(screenY) * width +
                    static_cast<size_t>(sampleX)];
                red += (pixel >> 16) & 0xFFu;
                green += (pixel >> 8) & 0xFFu;
                blue += pixel & 0xFFu;
            }
            auto* row = g_windowBlurRowSums +
                (rowSlot * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            row[0] = static_cast<uint16_t>(red);
            row[1] = static_cast<uint16_t>(green);
            row[2] = static_cast<uint16_t>(blue);
        }
    };

    constexpr size_t kernel = static_cast<size_t>(kWindowBlurRadius * 2 + 1);
    size_t head = 0;
    for (size_t row = 0; row < kernel; ++row)
        captureHorizontal(top - kWindowBlurRadius + static_cast<int64_t>(row), row);

    for (int64_t y = top; y < bottom; ++y) {
        for (int64_t x = left; x < right; ++x) {
            uint32_t red = 0, green = 0, blue = 0;
            for (size_t row = 0; row < kernel; ++row) {
                const auto* sums = g_windowBlurRowSums +
                    (((head + row) % kernel) * static_cast<size_t>(width) +
                     static_cast<size_t>(x)) * 3;
                red += sums[0];
                green += sums[1];
                blue += sums[2];
            }
            constexpr uint32_t sampleCount = static_cast<uint32_t>(kernel * kernel);
            const uint32_t blurred = 0xFF000000u |
                (((red + sampleCount / 2) / sampleCount) << 16) |
                (((green + sampleCount / 2) / sampleCount) << 8) |
                ((blue + sampleCount / 2) / sampleCount);
            const size_t destination = static_cast<size_t>(y) * width +
                static_cast<size_t>(x);
            const uint32_t tint = (static_cast<uint32_t>(opacity) << 24) |
                (blurred & 0x00FFFFFFu);
            g_frame[destination] = blend(tint, g_frame[destination]);
        }
        if (y + 1 < bottom) {
            head = (head + 1) % kernel;
            captureHorizontal(y + kWindowBlurRadius + 1,
                              (head + kernel - 1) % kernel);
        }
    }
}

void blendWindowShadow(int64_t left, int64_t top, int64_t right, int64_t bottom,
                       int64_t radius, uint32_t width, uint32_t height) noexcept {
    const int64_t shadowExtent = radius;
    const auto integerSqrt = [](uint64_t value) noexcept {
        uint64_t result = 0;
        uint64_t bit = uint64_t{1} << 62;
        while (bit > value) bit >>= 2;
        while (bit) {
            if (value >= result + bit) {
                value -= result + bit;
                result = (result >> 1) + bit;
            } else result >>= 1;
            bit >>= 2;
        }
        return static_cast<int64_t>(result);
    };
    // Signed distance to the rounded rectangle, in half-pixel units. The
    // rounded silhouette now controls the shadow at the corners as well as
    // along the straight edges.
    for (int64_t y = top - shadowExtent;
         y < bottom + shadowExtent; ++y) {
        if (y < g_composeClip.top || y >= g_composeClip.bottom ||
            y < 0 || y >= height) continue;
        for (int64_t x = left - shadowExtent;
             x < right + shadowExtent; ++x) {
            if (x < g_composeClip.left || x >= g_composeClip.right ||
                x < 0 || x >= width) continue;
            const int64_t qx = (2 * x + 1 < left + right
                ? left + right - (2 * x + 1) : (2 * x + 1) - left - right) -
                (right - left - 2 * radius);
            const int64_t qy = (2 * y + 1 < top + bottom
                ? top + bottom - (2 * y + 1) : (2 * y + 1) - top - bottom) -
                (bottom - top - 2 * radius);
            const int64_t outsideX = std::max<int64_t>(qx, 0);
            const int64_t outsideY = std::max<int64_t>(qy, 0);
            const int64_t signedDistance2 =
                integerSqrt(static_cast<uint64_t>(outsideX * outsideX +
                                                   outsideY * outsideY)) +
                std::min<int64_t>(std::max(qx, qy), 0) - 2 * radius;
            if (signedDistance2 <= 0 || signedDistance2 >
                2 * shadowExtent) continue;
            const uint32_t alpha = signedDistance2 <= 2 ? 42u :
                signedDistance2 <= 5 ? 34u : signedDistance2 <= 8 ? 26u :
                signedDistance2 <= 11 ? 18u : 10u;
            const size_t destination = static_cast<size_t>(y) * width +
                static_cast<size_t>(x);
            g_frame[destination] = blend(alpha << 24, g_frame[destination]);
        }
    }
}

void saveWindowCornerBackdrop(int64_t left, int64_t top,
                              int64_t right, int64_t bottom,
                              int64_t radius, uint32_t width,
                              uint32_t height) noexcept {
    for (int64_t y = 0; y < radius; ++y) {
        for (int64_t x = 0; x < radius; ++x) {
            const size_t offset = static_cast<size_t>(
                y * kWindowEffectRadiusLimit + x);
            const int64_t px[4]{left + x, right - radius + x,
                                left + x, right - radius + x};
            const int64_t py[4]{top + y, top + y,
                                bottom - radius + y, bottom - radius + y};
            for (size_t corner = 0; corner < 4; ++corner) {
                g_windowCornerBackdrop[corner][offset] =
                    px[corner] >= g_composeClip.left &&
                    px[corner] < g_composeClip.right &&
                    py[corner] >= g_composeClip.top &&
                    py[corner] < g_composeClip.bottom &&
                    px[corner] >= 0 && px[corner] < width &&
                    py[corner] >= 0 && py[corner] < height
                    ? g_frame[static_cast<size_t>(py[corner]) * width +
                              static_cast<size_t>(px[corner])] : 0;
            }
        }
    }
}

void applyWindowCornerMask(int64_t left, int64_t top,
                           int64_t right, int64_t bottom,
                           int64_t radius, uint32_t width,
                           uint32_t height) noexcept {
    for (int64_t y = 0; y < radius; ++y) {
        for (int64_t x = 0; x < radius; ++x) {
            const size_t offset = static_cast<size_t>(
                y * kWindowEffectRadiusLimit + x);
            const int64_t px[4]{left + x, right - radius + x,
                                left + x, right - radius + x};
            const int64_t py[4]{top + y, top + y,
                                bottom - radius + y, bottom - radius + y};
            for (size_t corner = 0; corner < 4; ++corner) {
                if (px[corner] < g_composeClip.left ||
                    px[corner] >= g_composeClip.right ||
                    py[corner] < g_composeClip.top ||
                    py[corner] >= g_composeClip.bottom ||
                    px[corner] < 0 || px[corner] >= width ||
                    py[corner] < 0 || py[corner] >= height) continue;
                uint32_t covered = 0;
                const int64_t centerX = (corner == 0 || corner == 2)
                    ? radius : 0;
                const int64_t centerY = corner < 2 ? radius : 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const int64_t sampleX = 4 * x + sx;
                        const int64_t sampleY = 4 * y + sy;
                        const int64_t dx = sampleX - 4 * centerX;
                        const int64_t dy = sampleY - 4 * centerY;
                        if (dx * dx + dy * dy <= 16 * radius * radius)
                            ++covered;
                    }
                }
                if (covered == 16) continue;
                const size_t destination = static_cast<size_t>(py[corner]) * width +
                    static_cast<size_t>(px[corner]);
                if (!covered) {
                    g_frame[destination] = g_windowCornerBackdrop[corner][offset];
                    continue;
                }
                const uint32_t foreground = g_frame[destination];
                const uint32_t backdrop = g_windowCornerBackdrop[corner][offset];
                uint32_t result = 0;
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    const uint32_t shift = channel * 8;
                    const uint32_t a = (foreground >> shift) & 0xFFu;
                    const uint32_t b = (backdrop >> shift) & 0xFFu;
                    result |= ((a * covered + b * (16u - covered) + 8u) / 16u)
                        << shift;
                }
                g_frame[destination] = result;
            }
        }
    }
}

void drawIconPixel(int64_t x, int64_t y, uint32_t color,
                   uint32_t width, uint32_t height) noexcept {
    fillRect(x, y, x + 2, y + 2, color, width, height);
}

void drawControlIcon(uint8_t kind, bool maximized, int64_t left,
                     int64_t top, uint32_t color,
                     uint32_t width, uint32_t height) noexcept {
    if (kind == 1) { // Minimize: a crisp vector bar.
        fillRect(left + 8, top + 15, left + 18, top + 17, color, width, height);
    } else if (kind == 2 && !maximized) { // Maximize: single outlined window.
        fillRect(left + 7, top + 6, left + 17, top + 8, color, width, height);
        fillRect(left + 7, top + 8, left + 9, top + 17, color, width, height);
        fillRect(left + 15, top + 8, left + 17, top + 17, color, width, height);
        fillRect(left + 7, top + 15, left + 17, top + 17, color, width, height);
    } else if (kind == 2) { // Restore: two offset, overlapping window outlines.
        fillRect(left + 9, top + 5, left + 17, top + 7, color, width, height);
        fillRect(left + 15, top + 7, left + 17, top + 14, color, width, height);
        fillRect(left + 6, top + 8, left + 14, top + 10, color, width, height);
        fillRect(left + 6, top + 10, left + 8, top + 17, color, width, height);
        fillRect(left + 12, top + 10, left + 14, top + 17, color, width, height);
        fillRect(left + 6, top + 15, left + 14, top + 17, color, width, height);
    } else if (kind == 3) { // Close: a hand-drawn, two-pixel diagonal cross.
        for (int64_t step = 0; step < 10; ++step) {
            drawIconPixel(left + 9 + step, top + 6 + step, color, width, height);
            drawIconPixel(left + 18 - step, top + 6 + step, color, width, height);
        }
    }
}

bool requiredPixelBytes(uint32_t width, uint32_t height, uint32_t stride,
                        uint64_t& bytes) noexcept {
    bytes = 0;
    if (!width || !height || stride < width) return false;
    constexpr uint64_t maxPixels = UINT64_MAX / sizeof(uint32_t);
    const uint64_t rows = static_cast<uint64_t>(height - 1);
    if (rows && stride > (maxPixels - width) / rows) return false;
    bytes = (rows * stride + width) * sizeof(uint32_t);
    return true;
}

bool composeRegion(const MicaCompositorMetadataStore& metadata,
                   ScreenRect clip,
                   const AnimationFrame* animationFrames,
                   const uint32_t* decorPixels) noexcept {
    (void)decorPixels;
    const uint32_t width = metadata.screenWidth;
    const uint32_t height = metadata.screenHeight;
    if (!width || !height || width > SIZE_MAX / height ||
        (metadata.surfaceEntryCount && (!metadata.surfaces || !animationFrames)) ||
        !ensureOrderedSurfaceCapacity(metadata.surfaceEntryCount) ||
        clip.left < 0 || clip.top < 0 || clip.right > width || clip.bottom > height ||
        clip.left >= clip.right || clip.top >= clip.bottom) return false;
    g_composeClip = clip;
    g_windowDrawOpacity = 255;

    const bool themeValid = MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme));
    const auto* normalHeaderbar = themeValid
        ? &metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT] : nullptr;
    const auto* inactiveHeaderbar = themeValid
        ? &metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT + 7u] : nullptr;
    const auto* normalButton = themeValid ? &metadata.theme.styles[0] : nullptr;
    const auto* windowStyle = themeValid
        ? &metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT] : nullptr;
    const bool blurRequested = windowStyle &&
        (windowStyle->flags & MICANT_THEME_HAS_BACKDROP_BLUR) &&
        (windowStyle->flags & MICANT_THEME_HAS_OPACITY) &&
        windowStyle->opacity > 0 && windowStyle->opacity < 255;
    if (blurRequested && (clip.left != 0 || clip.top != 0 ||
        clip.right != width || clip.bottom != height)) return false;

    fillRect(clip.left, clip.top, clip.right, clip.bottom, 0xFF1C3A5Cu,
             width, height);

    auto* ordered = g_orderedSurfaces;
    constexpr uint32_t kWsChild = 0x40000000u;
    size_t count = 0;
    for (size_t index = 0; index < metadata.surfaceEntryCount; ++index) {
        const auto& surface = metadata.surfaces[index];
        if ((surface.flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                              MICANT_COMPOSITOR_SURFACE_VISIBLE)) !=
                (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                 MICANT_COMPOSITOR_SURFACE_VISIBLE) ||
            ((surface.flags & MICANT_COMPOSITOR_SURFACE_ICONIC) &&
             !animationFrames[index].active) ||
            !surface.window || !surface.width || !surface.height ||
            !surface.surfaceAddress || !surface.surfaceSizeBytes || !surface.pitch)
            continue;
        size_t insertion = count;
        while (insertion && ordered[insertion - 1].info->zOrder > surface.zOrder) {
            ordered[insertion] = ordered[insertion - 1];
            --insertion;
        }
        ordered[insertion] = {&surface, index};
        ++count;
    }

    for (size_t index = 0; index < count; ++index) {
        const auto& surface = *ordered[index].info;
        const auto& animation = animationFrames[ordered[index].index];
        constexpr uint32_t kWsBorder = 0x00800000u;
        constexpr uint32_t kWsDlgFrame = 0x00400000u;
        constexpr uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
        constexpr uint32_t kWsThickFrame = 0x00040000u;
        const bool topLevel = (surface.style & kWsChild) == 0;
        // Child HWND pixels with a parent are folded into the top-level
        // window's client pass below. Parentless child-style probe surfaces
        // remain ordinary layers for the compositor self-check.
        if (!topLevel && surface.parentWindow) continue;
        const bool validWindowStyle = topLevel && windowStyle && windowStyle->flags;
        const int64_t themedFrame = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
            windowStyle->borderWidth >= 1 && windowStyle->borderWidth <= 16
                ? windowStyle->borderWidth : 0;
        const int64_t sourceFrame = !topLevel ? 0 :
            ((surface.style & kWsThickFrame) ? std::max<int64_t>(themedFrame, 5) :
             ((surface.style & (kWsBorder | kWsDlgFrame))
                ? (themedFrame ? themedFrame : 1) : 0));
        const int64_t sourceCaption = topLevel && (surface.style & kWsCaption)
            ? (normalHeaderbar && (normalHeaderbar->flags & MICANT_THEME_HAS_MIN_HEIGHT) &&
               normalHeaderbar->minHeight >= 24 && normalHeaderbar->minHeight <= 96
                ? normalHeaderbar->minHeight : 28)
            : 0;
        const int64_t windowLeft = animation.active ? animation.x : surface.x;
        const int64_t windowTop = animation.active ? animation.y : surface.y;
        const int64_t windowWidth = animation.active ? animation.width : surface.width;
        const int64_t windowHeight = animation.active ? animation.height : surface.height;
        const int64_t windowRight = windowLeft + windowWidth;
        const int64_t windowBottom = windowTop + windowHeight;
        const int64_t frame = animation.active && surface.width
            ? (sourceFrame * windowWidth + surface.width / 2) / surface.width
            : sourceFrame;
        const int64_t caption = animation.active && surface.height
            ? (sourceCaption * windowHeight + surface.height / 2) / surface.height
            : sourceCaption;
        const int64_t left = windowLeft + frame;
        const int64_t top = windowTop + frame + caption;
        const int64_t clientWidth = windowWidth - frame * 2;
        const int64_t clientHeight = windowHeight - frame * 2 - caption;
        const int64_t sourceClientWidth = surface.width - sourceFrame * 2;
        const int64_t sourceClientHeight = surface.height - sourceFrame * 2 - sourceCaption;
        const uint64_t surfacePixels = surface.surfaceSizeBytes / sizeof(uint32_t);
        if (clientWidth <= 0 || clientHeight <= 0 || sourceClientWidth <= 0 ||
            sourceClientHeight <= 0 || surface.surfaceSourceX > surface.pitch ||
            static_cast<uint64_t>(sourceClientWidth) >
                surface.pitch - surface.surfaceSourceX ||
            static_cast<uint64_t>(surface.surfaceSourceY) >
                (surfacePixels / surface.pitch)) continue;
        const uint64_t sourceStart = static_cast<uint64_t>(surface.surfaceSourceY) *
            surface.pitch + surface.surfaceSourceX;
        const uint64_t sourceRows = static_cast<uint64_t>(sourceClientHeight - 1) *
            surface.pitch;
        if (sourceRows > surfacePixels || sourceStart > surfacePixels - sourceRows ||
            static_cast<uint64_t>(sourceClientWidth) >
                surfacePixels - sourceStart - sourceRows) continue;
        const auto* surfacePixelsBase = reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(surface.surfaceAddress));

        const uint8_t opacity = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_OPACITY)
            ? windowStyle->opacity : 255u;
        const bool blurWindow = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_BACKDROP_BLUR) &&
            (windowStyle->flags & MICANT_THEME_HAS_OPACITY) &&
            opacity > 0 && opacity < 255;
        int64_t radius = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_RADIUS)
            ? windowStyle->borderRadius : 0;
        // Base remains square; themed corner radius comes from the active
        // headerbar style when the window style does not specify one.
        const bool hasThemedHeaderbar = themeValid && normalHeaderbar &&
            (normalHeaderbar->flags & MICANT_THEME_HAS_BACKGROUND);
        if (!radius && hasThemedHeaderbar &&
            !(surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED))
            radius = normalHeaderbar->borderRadius;
        if (surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED) radius = 0;
        if (!topLevel || !frame) radius = 0;
        if (radius > kWindowEffectRadiusLimit) radius = kWindowEffectRadiusLimit;
        if (radius * 2 > windowWidth) radius = windowWidth / 2;
        if (radius * 2 > windowHeight) radius = windowHeight / 2;
        if (blurWindow) captureWindowBlurBackdrop(width, height);
        if (radius > 0) {
            blendWindowShadow(windowLeft, windowTop, windowRight, windowBottom,
                              radius, width, height);
            saveWindowCornerBackdrop(windowLeft, windowTop, windowRight,
                                     windowBottom, radius, width, height);
        }
        g_windowDrawOpacity = opacity;

        if (frame || caption) {
            const bool foreground = (surface.flags & MICANT_COMPOSITOR_SURFACE_FOREGROUND) != 0;
            const auto* headerbarStyle = foreground ? normalHeaderbar : inactiveHeaderbar;
            if (!headerbarStyle || !headerbarStyle->flags) headerbarStyle = normalHeaderbar;
            const auto* captionBackgroundStyle = headerbarStyle &&
                (headerbarStyle->flags & MICANT_THEME_HAS_BACKGROUND)
                    ? headerbarStyle : normalHeaderbar;
            const auto* captionForegroundStyle = headerbarStyle &&
                (headerbarStyle->flags & MICANT_THEME_HAS_FOREGROUND)
                    ? headerbarStyle : normalHeaderbar;
            const auto* captionBorderStyle = headerbarStyle &&
                (headerbarStyle->flags & MICANT_THEME_HAS_BORDER)
                    ? headerbarStyle : normalHeaderbar;
            uint32_t frameColor = foreground ? 0xFF334B66u : 0xFF778492u;
            uint32_t captionColor = foreground ? 0xFF245987u : 0xFF596775u;
            uint32_t titleColor = foreground ? 0xFFF5F8FCu : 0xFFDCE3EAu;
            uint32_t buttonColor = foreground ? 0xFF376D9Au : 0xFF75818Cu;
            uint32_t closeColor = foreground ? 0xFFC84753u : 0xFF916A70u;
            const uint32_t bodyColor = validWindowStyle &&
                (windowStyle->flags & MICANT_THEME_HAS_BACKGROUND)
                ? windowStyle->background : 0xFFFFFFFFu;
            if (captionBackgroundStyle &&
                (captionBackgroundStyle->flags & MICANT_THEME_HAS_BACKGROUND)) {
                captionColor = captionBackgroundStyle->background;
                const uint32_t captionLuma =
                    (((captionColor >> 16) & 0xFFu) * 299u +
                     ((captionColor >> 8) & 0xFFu) * 587u +
                     (captionColor & 0xFFu) * 114u) / 1000u;
                frameColor = (validWindowStyle &&
                    (windowStyle->flags & MICANT_THEME_HAS_BORDER))
                    ? windowStyle->border
                    : (captionBorderStyle &&
                    (captionBorderStyle->flags & MICANT_THEME_HAS_BORDER))
                    ? captionBorderStyle->border
                    : blend((captionLuma >= 128u ? 31u : 32u) << 24,
                            bodyColor);
                buttonColor = captionColor;
                closeColor = captionColor;
            }
            if (captionForegroundStyle &&
                (captionForegroundStyle->flags & MICANT_THEME_HAS_FOREGROUND))
                titleColor = captionForegroundStyle->foreground;
            if (normalButton && (normalButton->flags & MICANT_THEME_HAS_BACKGROUND)) {
                buttonColor = normalButton->background;
                closeColor = buttonColor;
            }
            if (frame) {
                const bool hasWindowStrokeWidth = validWindowStyle &&
                    (windowStyle->flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
                    windowStyle->borderWidth >= 1 && windowStyle->borderWidth <= 16;
                const bool hasHeaderbarStrokeWidth = captionBorderStyle &&
                    (captionBorderStyle->flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
                    captionBorderStyle->borderWidth >= 1 &&
                    captionBorderStyle->borderWidth <= 16;
                const int64_t stroke = std::min<int64_t>(frame,
                    hasWindowStrokeWidth ? windowStyle->borderWidth :
                    hasHeaderbarStrokeWidth ? captionBorderStyle->borderWidth : 1);
                // The resize inset remains five pixels, but only the theme's
                // border width is drawn as a stroke. Its remaining area joins
                // the titlebar or client fill instead of becoming a dark slab.
                fillRect(windowLeft, windowTop, windowRight,
                         windowTop + frame, captionColor, width, height);
                fillRect(windowLeft, windowBottom - frame, windowRight,
                         windowBottom, bodyColor, width, height);
                fillRect(windowLeft, windowTop + frame, windowLeft + frame,
                         windowBottom - frame, bodyColor, width, height);
                fillRect(windowRight - frame, windowTop + frame, windowRight,
                         windowBottom - frame, bodyColor, width, height);
                // Resolve translucent theme borders over their adjacent themed
                // surface before drawing the visible stroke.
                const uint32_t bodyStroke = (frameColor & 0xFF000000u) == 0xFF000000u
                    ? frameColor : blend(frameColor, bodyColor);
                const uint32_t captionStroke =
                    (frameColor & 0xFF000000u) == 0xFF000000u
                    ? frameColor : blend(frameColor, captionColor);
                fillRect(windowLeft, windowTop, windowRight,
                         windowTop + stroke, captionStroke, width, height);
                fillRect(windowLeft, windowBottom - stroke, windowRight,
                         windowBottom, bodyStroke, width, height);
                fillRect(windowLeft, windowTop + stroke, windowLeft + stroke,
                         windowBottom - stroke, bodyStroke, width, height);
                fillRect(windowRight - stroke, windowTop + stroke, windowRight,
                         windowBottom - stroke, bodyStroke, width, height);
            }
            if (caption) {
                fillRect(windowLeft + frame, windowTop + frame,
                         windowRight - frame, windowTop + frame + caption,
                         captionColor, width, height);

                const int64_t closeLeft = windowRight - frame - 30;
                const int64_t buttonTop = windowTop + frame + 3;
                const int64_t buttonBottom = windowTop + frame + caption - 3;
                const auto drawCaptionButton = [&](uint8_t button,
                    int64_t buttonLeft, int64_t buttonRight) noexcept {
                    const bool pressed =
                        (surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_PRESSED) &&
                        ((surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_MASK) >>
                         MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_SHIFT) == button;
                    const bool hovered =
                        (surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_HOVER) &&
                        ((surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_MASK) >>
                         MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_SHIFT) == button;
                    const bool inactive = !foreground;
                    const uint32_t imageState = pressed ? 2u : hovered
                        ? (inactive ? 4u : 1u) : (inactive ? 3u : 0u);
                    const uint32_t imageIndex = (button == 3 ? 0u :
                        button == 1 ? 5u :
                        ((surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED) ? 15u : 10u)) +
                        imageState;
                    bool drewImage = false;
                    const MicaThemeCaptionImage* captionImage = imageIndex <
                        MICANT_THEME_CAPTION_IMAGE_COUNT
                            && metadata.captionImageSet.generation ==
                                metadata.themeGeneration
                            ? &metadata.captionImageSet.images[imageIndex] : nullptr;
                    const bool hasCaptionImage = captionImage && captionImage->width &&
                        captionImage->height &&
                        captionImage->stride >= captionImage->width &&
                        captionImage->width <= MICANT_THEME_CAPTION_IMAGE_SIZE &&
                        captionImage->height <= MICANT_THEME_CAPTION_IMAGE_SIZE;
                    const uint32_t state = pressed ? 2u : hovered ? 1u :
                        foreground ? 0u : 7u;
                    const auto* buttonStyle = themeValid
                        ? &metadata.theme.styles[state] : nullptr;
                    if (!buttonStyle || !buttonStyle->flags) buttonStyle = normalButton;
                    const auto applyOpacity = [&](uint32_t color) noexcept {
                        uint32_t alpha = color >> 24;
                        if (buttonStyle &&
                            (buttonStyle->flags & MICANT_THEME_HAS_OPACITY))
                            alpha = (alpha * buttonStyle->opacity + 127u) / 255u;
                        return (alpha << 24) | (color & 0x00FFFFFFu);
                    };
                    uint32_t background = button == 3 ? closeColor : buttonColor;
                    if (buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_BACKGROUND))
                        background = blend(applyOpacity(buttonStyle->background),
                                           captionColor);
                    uint32_t glyph = titleColor;
                    if (buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_FOREGROUND))
                        glyph = blend(applyOpacity(buttonStyle->foreground),
                                       captionColor);
                    if (!hasCaptionImage && buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_BORDER)) {
                        const uint32_t borderColor = blend(
                            applyOpacity(buttonStyle->border), background);
                        fillRect(buttonLeft, buttonTop, buttonRight, buttonBottom,
                                 borderColor, width, height);
                        fillRect(buttonLeft + 1, buttonTop + 1, buttonRight - 1,
                                 buttonBottom - 1, background, width, height);
                    } else if (!hasCaptionImage) {
                        fillRect(buttonLeft, buttonTop, buttonRight, buttonBottom,
                                 background, width, height);
                    }
                    if (hasCaptionImage) {
                        const int64_t imageLeft = buttonLeft +
                            (buttonRight - buttonLeft - captionImage->width) / 2;
                        const int64_t imageTop = windowTop + frame +
                            (caption - captionImage->height) / 2;
                        for (uint32_t row = 0; row < captionImage->height; ++row) {
                            const int64_t y = imageTop + row;
                            if (y < g_composeClip.top || y >= g_composeClip.bottom ||
                                y < 0 || y >= height) continue;
                            for (uint32_t column = 0; column < captionImage->width; ++column) {
                                const int64_t x = imageLeft + column;
                                if (x < g_composeClip.left || x >= g_composeClip.right ||
                                    x < 0 || x >= width) continue;
                                const uint32_t source = applyPremultipliedOpacity(
                                    metadata.captionImageSet.pixels[imageIndex][
                                        static_cast<size_t>(row) * captionImage->stride + column],
                                    opacity);
                                if (source >> 24) {
                                    const size_t destination = static_cast<size_t>(y) * width +
                                        static_cast<size_t>(x);
                                    g_frame[destination] =
                                        micant::shared::graphics::PremultipliedSourceOver(
                                            source, g_frame[destination]);
                                }
                            }
                        }
                        drewImage = true;
                    }
                    return drewImage ? 0u : glyph;
                };
                if (caption >= 12 && windowWidth >= 96) {
                    const uint32_t minimizeGlyph = drawCaptionButton(
                        1, closeLeft - 58, closeLeft - 30);
                    const uint32_t maximizeGlyph = drawCaptionButton(
                        2, closeLeft - 29, closeLeft - 1);
                    const uint32_t closeGlyph = drawCaptionButton(
                        3, closeLeft, windowRight - frame - 2);

                    const int64_t iconTop = windowTop + frame + (caption - 22) / 2;
                    const bool maximized = (surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED) != 0;
                    if (minimizeGlyph) drawControlIcon(1, false, closeLeft - 58, iconTop,
                                                       minimizeGlyph, width, height);
                    if (maximizeGlyph) drawControlIcon(2, maximized, closeLeft - 29, iconTop,
                                                       maximizeGlyph, width, height);
                    if (closeGlyph) drawControlIcon(3, false, closeLeft, iconTop,
                                                   closeGlyph, width, height);

                    const int64_t titleLimit = closeLeft - 66;
                    uint64_t iconEnd{};
                    const bool validIcon = surface.iconGeneration && surface.iconWidth &&
                        requiredPixelBytes(surface.iconWidth, surface.iconHeight,
                            surface.iconStride, iconEnd) && surface.iconAddress &&
                        iconEnd <= surface.iconSizeBytes;
                    const auto* iconPixels = validIcon
                        ? reinterpret_cast<const uint32_t*>(surface.iconAddress) : nullptr;
                    int64_t titleX = windowLeft + frame + 9;
                    if (validIcon) {
                        const int64_t iconLeft = titleX;
                        const int64_t iconTop = windowTop + frame +
                            (caption - static_cast<int64_t>(surface.iconHeight)) / 2;
                        for (uint32_t row = 0; row < surface.iconHeight; ++row) {
                            const int64_t y = iconTop + row;
                            if (y < g_composeClip.top || y >= g_composeClip.bottom ||
                                y < 0 || y >= height) continue;
                            for (uint32_t column = 0; column < surface.iconWidth; ++column) {
                                const int64_t x = iconLeft + column;
                                if (x >= titleLimit || x < g_composeClip.left ||
                                    x >= g_composeClip.right || x < 0 || x >= width) continue;
                                const uint32_t source = iconPixels[static_cast<size_t>(row) * surface.iconStride + column];
                                const uint32_t sourceAlpha = source >> 24;
                                const uint32_t effectiveAlpha =
                                    (sourceAlpha * opacity + 127u) / 255u;
                                if (effectiveAlpha)
                                    g_frame[static_cast<size_t>(y) * width +
                                        static_cast<size_t>(x)] = blend(
                                            (effectiveAlpha << 24) |
                                                (source & 0x00FFFFFFu),
                                            g_frame[static_cast<size_t>(y) * width +
                                                static_cast<size_t>(x)]);
                            }
                        }
                        titleX += surface.iconWidth + 8;
                    }

                    uint64_t titleEnd{};
                    const bool validTitle = surface.titleGeneration && surface.titleWidth &&
                        requiredPixelBytes(surface.titleWidth, surface.titleHeight,
                            surface.titleStride, titleEnd) && surface.titleAddress &&
                        titleEnd <= surface.titleSizeBytes;
                    const auto* titlePixels = validTitle
                        ? reinterpret_cast<const uint32_t*>(surface.titleAddress) : nullptr;
                    if (validTitle) {
                        const int64_t titleTop = windowTop + frame +
                            (caption - static_cast<int64_t>(surface.titleHeight)) / 2;
                        for (uint32_t row = 0; row < surface.titleHeight; ++row) {
                            const int64_t y = titleTop + row;
                            if (y < g_composeClip.top || y >= g_composeClip.bottom ||
                                y < 0 || y >= height) continue;
                            for (uint32_t column = 0; column < surface.titleWidth; ++column) {
                                const int64_t x = titleX + column;
                                if (x >= titleLimit) break;
                                if (x < g_composeClip.left || x >= g_composeClip.right ||
                                    x < 0 || x >= width) continue;
                                const uint32_t coverage = titlePixels[static_cast<size_t>(row) * surface.titleStride + column] >> 24;
                                if (!coverage) continue;
                                const uint32_t effectiveCoverage =
                                    (coverage * opacity + 127u) / 255u;
                                const uint32_t glyph = (effectiveCoverage << 24) |
                                    (titleColor & 0x00FFFFFFu);
                                const size_t destination = static_cast<size_t>(y) * width +
                                    static_cast<size_t>(x);
                                g_frame[destination] = blend(glyph, g_frame[destination]);
                            }
                        }
                    }
                }
            }
        }

        const int64_t clipLeft = left < clip.left ? clip.left : left;
        const int64_t clipTop = top < clip.top ? clip.top : top;
        const int64_t clientRight = left + clientWidth;
        const int64_t clientBottom = top + clientHeight;
        const int64_t right = clientRight < clip.right ? clientRight : clip.right;
        const int64_t bottom = clientBottom < clip.bottom ? clientBottom : clip.bottom;
        if (right <= clipLeft || bottom <= clipTop) continue;
        const bool hasClientBackground = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_BACKGROUND);
        const bool glassWindow = micant::compositor::glass::IsValid(
            surface.liquidGlass, surface.width, surface.height);
        // The card is rendered from the already-composed wallpaper/backdrop.
        // Filling its host window first makes the shader refract a flat white
        // client area and also leaves an opaque square around the rounded card.
        if (!glassWindow && (hasClientBackground || !blurWindow)) {
            const uint32_t clientBackground = hasClientBackground
                ? windowStyle->background : 0xFFFFFFFFu;
            fillRect(left, top, clientRight, clientBottom,
                     clientBackground, width, height);
        }
        if (blurWindow)
            applyWindowBackdropBlur(left, top, clientRight, clientBottom,
                                    opacity, width, height);
        if (glassWindow) {
            micant::compositor::glass::Render(g_liquidGlassCache, g_frame,
                width, height, surface.window, metadata.frameSequence,
                static_cast<int32_t>(windowLeft), static_cast<int32_t>(windowTop),
                surface.liquidGlass);
            micant::compositor::glass::Composite(g_liquidGlassCache, g_frame,
                width, height, {static_cast<int32_t>(clipLeft), static_cast<int32_t>(clipTop),
                    static_cast<int32_t>(right), static_cast<int32_t>(bottom)});
        }
        for (int64_t y = clipTop; y < bottom; ++y) {
            const size_t sourceY = animation.active
                ? static_cast<size_t>(y - top) * static_cast<size_t>(sourceClientHeight) /
                    static_cast<size_t>(clientHeight)
                : static_cast<size_t>(y - top);
            const size_t sourceRow = static_cast<size_t>(surface.surfaceSourceY) *
                surface.pitch + surface.surfaceSourceX + sourceY * surface.pitch;
            const size_t pixelCount = static_cast<size_t>(right - clipLeft);
            uint32_t* destination = g_frame + static_cast<size_t>(y) * width +
                static_cast<size_t>(clipLeft);
            if (!animation.active) {
                const size_t sourceX = static_cast<size_t>(clipLeft - left);
                const uint32_t* source = surfacePixelsBase + sourceRow + sourceX;
                size_t x = 0;
                while (x < pixelCount) {
                    if ((source[x] >> 24) == 255u && opacity == 255u) {
                        const size_t runStart = x++;
                        while (x < pixelCount && (source[x] >> 24) == 255u) ++x;
                        for (size_t copy = runStart; copy < x; ++copy)
                            destination[copy] = source[copy];
                    } else {
                        uint32_t pixel = applyPremultipliedOpacity(source[x], opacity);
                        if (pixel >> 24)
                            destination[x] = micant::shared::graphics::PremultipliedSourceOver(
                                pixel, destination[x]);
                        ++x;
                    }
                }
            } else {
                for (size_t x = 0; x < pixelCount; ++x) {
                    const int64_t localX = clipLeft + static_cast<int64_t>(x) - left;
                    const size_t sourceX = static_cast<size_t>(localX) *
                        static_cast<size_t>(sourceClientWidth) /
                        static_cast<size_t>(clientWidth);
                    const uint32_t source = applyPremultipliedOpacity(
                        surfacePixelsBase[sourceRow + sourceX], opacity);
                    if (source >> 24)
                        destination[x] = micant::shared::graphics::PremultipliedSourceOver(
                            source, destination[x]);
                }
            }
        }
        const int64_t parentClientRight = left + clientWidth;
        const int64_t parentClientBottom = top + clientHeight;
        for (size_t childIndex = 0; childIndex < count; ++childIndex) {
            const auto& child = *ordered[childIndex].info;
            if (!(child.style & kWsChild) || !child.parentWindow ||
                child.ownerProcessId != surface.ownerProcessId ||
                !(child.flags & MICANT_COMPOSITOR_SURFACE_VISIBLE)) continue;
            const MicaCompositorSurfaceInfo* ancestor = &child;
            for (size_t depth = 0; ancestor && ancestor->parentWindow &&
                 depth < metadata.surfaceEntryCount; ++depth) {
                const uint64_t parentHandle = ancestor->parentWindow;
                ancestor = nullptr;
                for (size_t lookup = 0; lookup < count; ++lookup) {
                    if (ordered[lookup].info->window == parentHandle) {
                        ancestor = ordered[lookup].info;
                        break;
                    }
                }
            }
            if (!ancestor || ancestor->window != surface.window) continue;
            const auto* childPixelsBase = reinterpret_cast<const uint32_t*>(
                static_cast<uintptr_t>(child.surfaceAddress));
            const uint64_t childCapacity = child.surfaceSizeBytes / sizeof(uint32_t);
            if (!childPixelsBase || !child.pitch || !child.width || !child.height ||
                child.surfaceSourceX > child.pitch ||
                static_cast<uint64_t>(child.width) > child.pitch - child.surfaceSourceX ||
                static_cast<uint64_t>(child.surfaceSourceY) > childCapacity / child.pitch)
                continue;
            const uint64_t childStart = static_cast<uint64_t>(child.surfaceSourceY) *
                child.pitch + child.surfaceSourceX;
            const uint64_t childRows = static_cast<uint64_t>(child.height - 1) * child.pitch;
            if (childRows > childCapacity || childStart > childCapacity - childRows ||
                static_cast<uint64_t>(child.width) >
                    childCapacity - childStart - childRows) continue;
            const int64_t childLeft = child.x;
            const int64_t childTop = child.y;
            const int64_t childRight = childLeft + child.width;
            const int64_t childBottom = childTop + child.height;
            const int64_t drawLeft = std::max({childLeft, left, clip.left});
            const int64_t drawTop = std::max({childTop, top, clip.top});
            const int64_t drawRight = std::min({childRight, parentClientRight, clip.right});
            const int64_t drawBottom = std::min({childBottom, parentClientBottom, clip.bottom});
            if (drawRight <= drawLeft || drawBottom <= drawTop) continue;
            for (int64_t y = drawTop; y < drawBottom; ++y) {
                const size_t sourceRow = static_cast<size_t>(child.surfaceSourceY) *
                    child.pitch + child.surfaceSourceX +
                    static_cast<size_t>(y - childTop) * child.pitch;
                uint32_t* destination = g_frame + static_cast<size_t>(y) * width +
                    static_cast<size_t>(drawLeft);
                for (int64_t x = drawLeft; x < drawRight; ++x) {
                    const uint32_t source = childPixelsBase[sourceRow +
                        static_cast<size_t>(x - childLeft)];
                    if (source >> 24)
                        destination[x - drawLeft] =
                            micant::shared::graphics::PremultipliedSourceOver(
                                source, destination[x - drawLeft]);
                }
            }
        }
        if (radius > 0)
            applyWindowCornerMask(windowLeft, windowTop, windowRight, windowBottom,
                                  radius, width, height);
        g_windowDrawOpacity = 255;
    }

    const auto& cursor = metadata.cursor;
    uint64_t cursorBytes{};
    const bool cursorPixelsValid = requiredPixelBytes(cursor.width, cursor.height,
        cursor.pitch, cursorBytes) && cursor.pixelsAddress &&
        cursorBytes <= cursor.pixelSizeBytes;
    if ((cursor.flags & (MICANT_COMPOSITOR_CURSOR_ACTIVE | MICANT_COMPOSITOR_CURSOR_VISIBLE)) ==
            (MICANT_COMPOSITOR_CURSOR_ACTIVE | MICANT_COMPOSITOR_CURSOR_VISIBLE) &&
        cursorPixelsValid &&
        cursor.hotX < cursor.width && cursor.hotY < cursor.height) {
        const auto* cursorPixels = reinterpret_cast<const uint32_t*>(
            cursor.pixelsAddress);
        const int64_t originX = static_cast<int64_t>(cursor.x) - cursor.hotX;
        const int64_t originY = static_cast<int64_t>(cursor.y) - cursor.hotY;
        for (uint32_t sy = 0; sy < cursor.height; ++sy) {
            const int64_t dy = originY + sy;
            if (dy < clip.top || dy >= clip.bottom) continue;
            for (uint32_t sx = 0; sx < cursor.width; ++sx) {
                const int64_t dx = originX + sx;
                if (dx < clip.left || dx >= clip.right) continue;
                const uint32_t source = cursorPixels[
                    static_cast<size_t>(sy) * cursor.pitch + sx];
                const size_t destination = static_cast<size_t>(dy) * width + static_cast<size_t>(dx);
                g_frame[destination] = micant::shared::graphics::PremultipliedSourceOver(
                    source, g_frame[destination]);
            }
        }
    }

    return true;
}

bool presentRegion(uint32_t screenWidth, ScreenRect clip) noexcept {
    micant::um::UserPresentSurfaceParams64 present{};
    present.surfaceBufferVa = reinterpret_cast<uint64_t>(
        g_frame + static_cast<size_t>(clip.top) * screenWidth + clip.left);
    present.dstX = static_cast<uint32_t>(clip.left);
    present.dstY = static_cast<uint32_t>(clip.top);
    present.width = static_cast<uint32_t>(clip.right - clip.left);
    present.height = static_cast<uint32_t>(clip.bottom - clip.top);
    present.pitch = screenWidth;
    const uint64_t status = micant::sys::invokeUserService(
        0x81, reinterpret_cast<uint64_t>(&present));
    if (status != 0) {
        if (!g_presentFailureReported) {
            constexpr char failed[] = "[MicaNT Compositor] Frame present failed\r\n";
            print(failed, sizeof(failed) - 1);
            g_presentFailureReported = true;
        }
        return false;
    }
    return true;
}

struct DamageSet {
    ScreenRect regions[kDamageRegionComplexityBudget]{};
    size_t count{};
};

#if MICANT_COMPOSITOR_LOCAL_STATS
uint64_t damagePixelCount(const DamageSet& damage) noexcept {
    uint64_t pixels = 0;
    for (size_t index = 0; index < damage.count; ++index) {
        const auto& region = damage.regions[index];
        if (region.right <= region.left || region.bottom <= region.top) continue;
        const uint64_t width = static_cast<uint64_t>(region.right - region.left);
        const uint64_t height = static_cast<uint64_t>(region.bottom - region.top);
        if (height && width > (UINT64_MAX - pixels) / height) return UINT64_MAX;
        pixels += width * height;
    }
    return pixels;
}
#endif

void addDamageRegion(DamageSet& damage, ScreenRect incoming,
                     uint32_t screenWidth, uint32_t screenHeight) noexcept {
    incoming.left = incoming.left < 0 ? 0 : incoming.left;
    incoming.top = incoming.top < 0 ? 0 : incoming.top;
    incoming.right = incoming.right > screenWidth ? screenWidth : incoming.right;
    incoming.bottom = incoming.bottom > screenHeight ? screenHeight : incoming.bottom;
    if (incoming.left >= incoming.right || incoming.top >= incoming.bottom) return;
    for (size_t index = 0; index < damage.count;) {
        auto& existing = damage.regions[index];
        const bool separated = incoming.right < existing.left ||
            existing.right < incoming.left || incoming.bottom < existing.top ||
            existing.bottom < incoming.top;
        if (separated) {
            ++index;
            continue;
        }
        incoming.left = incoming.left < existing.left ? incoming.left : existing.left;
        incoming.top = incoming.top < existing.top ? incoming.top : existing.top;
        incoming.right = incoming.right > existing.right ? incoming.right : existing.right;
        incoming.bottom = incoming.bottom > existing.bottom ? incoming.bottom : existing.bottom;
        damage.regions[index] = damage.regions[--damage.count];
        index = 0;
    }
    if (damage.count == kDamageRegionComplexityBudget) {
        damage.regions[0] = {0, 0, screenWidth, screenHeight};
        damage.count = 1;
        return;
    }
    damage.regions[damage.count++] = incoming;
}

bool collectDamage(const MicaCompositorMetadataStore& metadata,
                   DamageSet& damage) noexcept {
    const uint32_t screenWidth = metadata.screenWidth;
    const uint32_t screenHeight = metadata.screenHeight;
    const ScreenRect fullScreen{0, 0, screenWidth, screenHeight};
    const bool blurEnabled =
        MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme)) &&
        (metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].flags &
         MICANT_THEME_HAS_BACKDROP_BLUR) &&
        (metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].flags &
         MICANT_THEME_HAS_OPACITY) &&
        metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].opacity > 0 &&
        metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].opacity < 255;
    bool fullRedraw = blurEnabled || !g_hasPresentedFrame ||
        (metadata.dirtyFlags & MICANT_COMPOSITOR_DIRTY_FULL) != 0 ||
        (metadata.dirtyFlags & ~MICANT_COMPOSITOR_DIRTY_FULL) != 0 ||
        metadata.themeGeneration != g_lastThemeGeneration ||
        (metadata.dirtyRegionCount && (!metadata.dirtyRegions ||
            metadata.dirtyRegionCount > metadata.dirtyRegionArraySizeBytes /
                sizeof(MicaCompositorDirtyRect)));
    for (size_t index = 0; index < metadata.surfaceEntryCount; ++index) {
        const auto& surface = metadata.surfaces[index];
        if ((surface.flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                              MICANT_COMPOSITOR_SURFACE_VISIBLE)) ==
                (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                 MICANT_COMPOSITOR_SURFACE_VISIBLE) &&
            micant::compositor::glass::IsValid(surface.liquidGlass,
                                                surface.width, surface.height)) {
            fullRedraw = true;
            break;
        }
    }
    if (!fullRedraw) {
        for (size_t index = 0; index < metadata.dirtyRegionCount; ++index) {
            const auto& dirty = metadata.dirtyRegions[index];
            if (dirty.left < 0 || dirty.top < 0 ||
                dirty.right > static_cast<int32_t>(screenWidth) ||
                dirty.bottom > static_cast<int32_t>(screenHeight) ||
                dirty.left >= dirty.right || dirty.top >= dirty.bottom) {
                fullRedraw = true;
                break;
            }
        }
    }

    if (fullRedraw) {
        damage.regions[0] = fullScreen;
        damage.count = 1;
        return true;
    }

    damage.count = metadata.dirtyRegionCount;
    const bool themeValid =
        MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme));
    const auto& windowStyle =
        metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT];
    const auto& headerbarStyle =
        metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT];
    const bool shadowEnabled = themeValid &&
        (((windowStyle.flags & MICANT_THEME_HAS_RADIUS) &&
          windowStyle.borderRadius != 0) ||
         (headerbarStyle.flags & MICANT_THEME_HAS_BACKGROUND));
    const int64_t shadowExtent = shadowEnabled
        ? std::max<int64_t>(windowStyle.borderRadius,
            headerbarStyle.borderRadius) : 0;
    for (size_t index = 0; index < metadata.dirtyRegionCount; ++index) {
        const auto& dirty = metadata.dirtyRegions[index];
        const int64_t margin = shadowExtent;
        const ScreenRect region{
            dirty.left > margin ? dirty.left - margin : 0,
            dirty.top > margin ? dirty.top - margin : 0,
            dirty.right + margin < screenWidth
                ? dirty.right + margin : screenWidth,
            dirty.bottom + margin < screenHeight
                ? dirty.bottom + margin : screenHeight};
        damage.regions[index] = region;
    }
    return true;
}

bool composeDamageRegions(const MicaCompositorMetadataStore& metadata,
                          const AnimationFrame* frames,
                          const uint32_t* decorPixels,
                          const DamageSet& damage) noexcept {
    for (size_t index = 0; index < damage.count; ++index)
    if (!composeRegion(metadata, damage.regions[index], frames,
                           decorPixels))
            return false;
    return true;
}

bool presentDamage(uint32_t screenWidth, const DamageSet& damage) noexcept {
    if (!damage.count) return true;
    // Diagnostic: submit the persistent complete frame after recomposing only
    // the damaged regions to isolate the device's partial-present path.
    return presentRegion(screenWidth,
        {0, 0, static_cast<int32_t>(screenWidth),
         static_cast<int32_t>(g_displayHeight)});
}

bool verifyWindowEffectsRasterizer() noexcept {
    auto& metadata = g_effectProbeMetadata;
    // These bounded probe buffers are static-storage objects and start zeroed.
    metadata.magic = MICANT_COMPOSITOR_STORE_MAGIC;
    metadata.version = MICANT_COMPOSITOR_STORE_VERSION;
    metadata.screenWidth = g_displayWidth;
    metadata.screenHeight = g_displayHeight;
    metadata.surfaceEntryCount = 2;
    metadata.surfaces = g_effectProbeSurfaces;
    metadata.surfaceArraySizeBytes = sizeof(g_effectProbeSurfaces);
    metadata.theme.magic = MICANT_THEME_SNAPSHOT_MAGIC;
    metadata.theme.version = MICANT_THEME_SNAPSHOT_VERSION;
    metadata.theme.headerSize = static_cast<uint16_t>(offsetof(MicaThemeSnapshot, styles));
    metadata.theme.totalSize = sizeof(MicaThemeSnapshot);
    metadata.theme.controlCount = MICANT_THEME_CONTROL_COUNT;
    metadata.theme.stateCount = MICANT_THEME_STATE_COUNT;
    auto& style = metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT];
    style.flags = MICANT_THEME_HAS_RADIUS | MICANT_THEME_HAS_OPACITY |
        MICANT_THEME_HAS_BACKDROP_BLUR;
    style.borderRadius = 8;
    style.opacity = 128;

    auto& underlay = g_effectProbeSurfaces[0];
    underlay = {};
    underlay.window = 1;
    underlay.x = 90;
    underlay.y = 90;
    underlay.width = underlay.height = 100;
    underlay.pitch = 100;
    underlay.surfaceAddress = reinterpret_cast<uintptr_t>(g_effectProbePixels);
    underlay.surfaceSizeBytes = sizeof(g_effectProbePixels);
    underlay.zOrder = 1;
    underlay.style = 0x40000000u; // child: no frame, no window effects.
    underlay.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
        MICANT_COMPOSITOR_SURFACE_VISIBLE;
    metadata.surfaces[0] = underlay;

    auto& surface = g_effectProbeSurfaces[1];
    surface = {};
    surface.window = 2;
    surface.x = 100;
    surface.y = 100;
    surface.width = surface.height = surface.pitch = 64;
    surface.surfaceSourceX = 0;
    surface.surfaceSourceY = 128u * 128u / surface.pitch;
    surface.surfaceAddress = reinterpret_cast<uintptr_t>(g_effectProbePixels);
    surface.surfaceSizeBytes = sizeof(g_effectProbePixels);
    surface.zOrder = 2;
    surface.style = 0x00040000u | 0x00800000u | 0x00400000u;
    surface.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
        MICANT_COMPOSITOR_SURFACE_VISIBLE |
        MICANT_COMPOSITOR_SURFACE_FOREGROUND;
    metadata.surfaces[1] = surface;
    for (auto& pixel : g_effectProbePixels) pixel = 0;
    for (uint32_t y = 0; y < 100; ++y)
        for (uint32_t x = 0; x < 100; ++x)
            g_effectProbePixels[static_cast<size_t>(y) * 100 + x] =
                ((x + y) & 1u) ? 0xFFFFFFFFu : 0xFF000000u;
    for (uint32_t pixel = 0; pixel < 64u * 64u; ++pixel)
        g_effectProbePixels[128u * 128u + pixel] = 0x80808080u;
    constexpr uint32_t background = 0xFF000000u;
    const ScreenRect clip{0, 0, g_displayWidth, g_displayHeight};
    if (!composeRegion(metadata, clip,
                       g_effectProbeAnimations, nullptr)) return false;
    const uint32_t blurredBackdrop = blend(0x807A7A7Au, background);
    const uint32_t expectedClient =
        micant::shared::graphics::PremultipliedSourceOver(
            0x40404040u, blurredBackdrop);
    const uint32_t unblurredClient =
        micant::shared::graphics::PremultipliedSourceOver(
            0x40404040u, background);
    const uint32_t expectedShadow = blend(0x2A000000u, 0xFFFFFFFFu);
    const size_t corner = 100u * g_displayWidth + 100u;
    const size_t client = 140u * g_displayWidth + 120u;
    const size_t shadow = 99u * g_displayWidth + 120u;
    if (g_frame[corner] != 0xFF000000u) {
        constexpr char message[] = "[MicaNT Compositor] Rounded corner pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }
    if (g_frame[client] != expectedClient || expectedClient == unblurredClient) {
        constexpr char message[] = "[MicaNT Compositor] Window opacity pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }
    if (g_frame[shadow] != expectedShadow) {
        constexpr char message[] = "[MicaNT Compositor] Window shadow pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }

    // An empty/invalid theme snapshot style retains the opaque square Base path.
    style.flags = 0;
    style.borderRadius = 0;
    style.opacity = 255;
    if (!composeRegion(metadata, clip,
                       g_effectProbeAnimations, nullptr)) return false;
    if (g_frame[corner] != 0xFF334B66u) {
        constexpr char message[] = "[MicaNT Compositor] Base window pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }

    // Verify that cursor movement can be repainted from only its old and new
    // bounds without leaving cursor pixels in the backing frame. Compare each
    // partial result with a fresh full composition of the same scene.
    metadata.cursor = {};
    auto& cursor = metadata.cursor;
    cursor.x = 130;
    cursor.y = 140;
    cursor.width = cursor.pitch = 7;
    cursor.height = 5;
    cursor.hotX = 2;
    cursor.hotY = 1;
    cursor.flags = MICANT_COMPOSITOR_CURSOR_ACTIVE |
        MICANT_COMPOSITOR_CURSOR_VISIBLE;
    cursor.pixelsAddress = reinterpret_cast<uintptr_t>(g_effectProbePixels);
    cursor.pixelSizeBytes = sizeof(g_effectProbePixels);
    for (uint32_t y = 0; y < cursor.height; ++y)
        for (uint32_t x = 0; x < cursor.width; ++x)
            g_effectProbePixels[static_cast<size_t>(y) * cursor.pitch + x] =
                (x == 0 || y == 0) ? 0xFF101010u : 0x80800000u;
    const auto frameMatches = [width = g_displayWidth, height = g_displayHeight]() noexcept {
        const size_t count = static_cast<size_t>(width) * height;
        for (size_t index = 0; index < count; ++index)
            if (g_frame[index] != g_windowBlurBackdrop[index]) return false;
        return true;
    };
    const ScreenRect fullScreen{0, 0, g_displayWidth, g_displayHeight};
    if (!composeRegion(metadata, fullScreen, g_effectProbeAnimations, nullptr))
        return false;
    constexpr int32_t firstX = 130;
    constexpr int32_t firstY = 140;
    constexpr int32_t secondX = 142;
    constexpr int32_t secondY = 151;
    cursor.x = secondX;
    cursor.y = secondY;
    DamageSet cursorDamage{};
    addDamageRegion(cursorDamage,
        {firstX - static_cast<int64_t>(cursor.hotX),
         firstY - static_cast<int64_t>(cursor.hotY),
         firstX - cursor.hotX + cursor.width,
         firstY - cursor.hotY + cursor.height}, g_displayWidth, g_displayHeight);
    addDamageRegion(cursorDamage,
        {secondX - static_cast<int64_t>(cursor.hotX),
         secondY - static_cast<int64_t>(cursor.hotY),
         secondX - cursor.hotX + cursor.width,
         secondY - cursor.hotY + cursor.height}, g_displayWidth, g_displayHeight);
    if (!composeDamageRegions(metadata, g_effectProbeAnimations, nullptr,
                              cursorDamage)) return false;
    for (size_t index = 0; index < static_cast<size_t>(g_displayWidth) * g_displayHeight;
         ++index) g_windowBlurBackdrop[index] = g_frame[index];
    if (!composeRegion(metadata, fullScreen, g_effectProbeAnimations, nullptr) ||
        !frameMatches()) {
        constexpr char message[] =
            "[MicaNT Compositor] Cursor partial repaint mismatch after move\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }

    cursor.x = firstX;
    cursor.y = firstY;
    cursorDamage = {};
    addDamageRegion(cursorDamage,
        {secondX - static_cast<int64_t>(cursor.hotX),
         secondY - static_cast<int64_t>(cursor.hotY),
         secondX - cursor.hotX + cursor.width,
         secondY - cursor.hotY + cursor.height}, g_displayWidth, g_displayHeight);
    addDamageRegion(cursorDamage,
        {firstX - static_cast<int64_t>(cursor.hotX),
         firstY - static_cast<int64_t>(cursor.hotY),
         firstX - cursor.hotX + cursor.width,
         firstY - cursor.hotY + cursor.height}, g_displayWidth, g_displayHeight);
    if (!composeDamageRegions(metadata, g_effectProbeAnimations, nullptr,
                              cursorDamage)) return false;
    for (size_t index = 0; index < static_cast<size_t>(g_displayWidth) * g_displayHeight;
         ++index) g_windowBlurBackdrop[index] = g_frame[index];
    if (!composeRegion(metadata, fullScreen, g_effectProbeAnimations, nullptr) ||
        !frameMatches()) {
        constexpr char message[] =
            "[MicaNT Compositor] Cursor partial repaint mismatch after return\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }
    constexpr char cursorPassed[] =
        "[MicaNT Compositor] Cursor old/new partial repaint matches full composition\r\n";
    print(cursorPassed, sizeof(cursorPassed) - 1);
    return true;
}

bool copyStableMetadata(const MicaCompositorMetadataStore* source,
                        MicaCompositorMetadataStore& destination,
                        uint64_t& sequence) noexcept {
    sequence = __atomic_load_n(&source->frameSequence, __ATOMIC_ACQUIRE);
    if (sequence & 1u) return false;
    const auto* src = reinterpret_cast<const volatile uint8_t*>(source);
    auto* dst = reinterpret_cast<uint8_t*>(&destination);
    const auto copyRange = [src, dst](size_t offset, size_t bytes) noexcept {
        for (size_t index = 0; index < bytes; ++index)
            dst[offset + index] = src[offset + index];
    };
    copyRange(0, sizeof(destination));
    const size_t surfaceBytes = static_cast<size_t>(destination.surfaceEntryCount) *
        sizeof(MicaCompositorSurfaceInfo);
    const size_t dirtyBytes = static_cast<size_t>(destination.dirtyRegionCount) *
        sizeof(MicaCompositorDirtyRect);
    if ((surfaceBytes && (!destination.surfaces ||
                          surfaceBytes > destination.surfaceArraySizeBytes)) ||
        (dirtyBytes && (!destination.dirtyRegions ||
                        dirtyBytes > destination.dirtyRegionArraySizeBytes)))
        return false;
    if (destination.surfaceEntryCount > g_snapshotSurfaceCapacity) {
        void* replacement{};
        if (!allocateZeroed(surfaceBytes, replacement)) return false;
        releaseAllocation(g_snapshotSurfaces);
        g_snapshotSurfaces = static_cast<MicaCompositorSurfaceInfo*>(replacement);
        g_snapshotSurfaceCapacity = destination.surfaceEntryCount;
    }
    if (destination.dirtyRegionCount > g_snapshotDirtyCapacity) {
        void* replacement{};
        if (!allocateZeroed(dirtyBytes, replacement)) return false;
        releaseAllocation(g_snapshotDirtyRegions);
        g_snapshotDirtyRegions = static_cast<MicaCompositorDirtyRect*>(replacement);
        g_snapshotDirtyCapacity = destination.dirtyRegionCount;
    }
    auto copyRemote = [](void* target, const volatile void* remote,
                         size_t bytes) noexcept {
        auto* output = static_cast<uint8_t*>(target);
        const auto* input = static_cast<const volatile uint8_t*>(remote);
        for (size_t index = 0; index < bytes; ++index) output[index] = input[index];
    };
    if (surfaceBytes)
        copyRemote(g_snapshotSurfaces, destination.surfaces, surfaceBytes);
    if (dirtyBytes)
        copyRemote(g_snapshotDirtyRegions, destination.dirtyRegions, dirtyBytes);
    if (__atomic_load_n(&source->frameSequence, __ATOMIC_ACQUIRE) != sequence)
        return false;
    destination.surfaces = g_snapshotSurfaces;
    destination.surfaceArraySizeBytes = g_snapshotSurfaceCapacity *
        sizeof(MicaCompositorSurfaceInfo);
    destination.dirtyRegions = g_snapshotDirtyRegions;
    destination.dirtyRegionArraySizeBytes = g_snapshotDirtyCapacity *
        sizeof(MicaCompositorDirtyRect);
    return true;
}

} // namespace

extern "C" void _start() noexcept {
    constexpr char startupMessage[] = "[MicaNT Compositor] Ring 3 compositor entered\r\n";
    print(startupMessage, sizeof(startupMessage) - 1);
    constexpr char effectPassed[] =
        "[MicaNT Compositor] Window opacity, radius, shadow and Base pixels verified\r\n";
    constexpr char effectFailed[] =
        "[MicaNT Compositor] Window software effect pixel probe failed\r\n";
    micant::um::UserVideoInfo64 displayInfo{};
    micant::um::UserCompositorViews64 compositorViews{};
    if (micant::sys::invokeUserService(0x80,
            reinterpret_cast<uint64_t>(&displayInfo)) != 0 ||
        !displayInfo.width || !displayInfo.height || displayInfo.pitch < displayInfo.width ||
        displayInfo.height > SIZE_MAX / displayInfo.width ||
        static_cast<size_t>(displayInfo.width) * displayInfo.height >
            SIZE_MAX / sizeof(uint32_t)) {
        constexpr char unavailable[] =
            "[MicaNT Compositor] Runtime display mode is invalid\r\n";
        print(unavailable, sizeof(unavailable) - 1);
        return;
    }
    if (micant::sys::invokeUserService(0x84,
            reinterpret_cast<uint64_t>(&compositorViews)) != 0 ||
        compositorViews.magic != 0x56434D4Du || compositorViews.version != 1 ||
        compositorViews.size != sizeof(compositorViews) ||
        !compositorViews.metadataAddress) {
        constexpr char unavailable[] =
            "[MicaNT Compositor] Runtime section mappings are unavailable\r\n";
        print(unavailable, sizeof(unavailable) - 1);
        return;
    }
    const auto* metadata = reinterpret_cast<const MicaCompositorMetadataStore*>(
        compositorViews.metadataAddress);
    const auto* decorPixels = static_cast<const uint32_t*>(nullptr);
    g_displayWidth = displayInfo.width;
    g_displayHeight = displayInfo.height;
    const size_t frameBytes = static_cast<size_t>(g_displayWidth) *
        g_displayHeight * sizeof(uint32_t);
    const size_t rowSumBytes = static_cast<size_t>(g_displayWidth) *
        static_cast<size_t>(kWindowBlurRadius * 2 + 1) * 3 * sizeof(uint16_t);
    void* frame{};
    void* backdrop{};
    void* rowSums{};
    if (!allocateZeroed(frameBytes, frame) ||
        !allocateZeroed(frameBytes, backdrop) ||
        !allocateZeroed(rowSumBytes, rowSums)) {
        constexpr char allocationFailed[] =
            "[MicaNT Compositor] Runtime frame allocation failed\r\n";
        print(allocationFailed, sizeof(allocationFailed) - 1);
        (void)micant::sys::invokeUserService(0x2C,
            static_cast<uint64_t>(-1), 0xC0000017u);
        return;
    }
    g_frame = static_cast<uint32_t*>(frame);
    g_windowBlurBackdrop = static_cast<uint32_t*>(backdrop);
    g_windowBlurRowSums = static_cast<uint16_t*>(rowSums);
    const bool effectsPassed = verifyWindowEffectsRasterizer();
    const char* effectMessage = effectsPassed ? effectPassed : effectFailed;
    const size_t effectMessageLength = effectsPassed
        ? sizeof(effectPassed) - 1 : sizeof(effectFailed) - 1;
    print(effectMessage, effectMessageLength);
    if (displayInfo.width && displayInfo.height) {
        const auto transport = static_cast<micant::um::DisplayTransportBackend>(
            displayInfo.transportBackend);
        const auto composition = static_cast<micant::um::DisplayCompositionBackend>(
            displayInfo.compositionBackend);
        if (transport == micant::um::DisplayTransportBackend::Virtio2D &&
            composition == micant::um::DisplayCompositionBackend::Software) {
            constexpr char message[] = "[MicaNT Compositor] Backend: SOFTWARE_COMPOSITOR + VIRTIO_2D_SCANOUT\r\n";
            print(message, sizeof(message) - 1);
        } else if (transport == micant::um::DisplayTransportBackend::Virgl &&
                   composition == micant::um::DisplayCompositionBackend::VirglAccelerated) {
            constexpr char message[] = "[MicaNT Compositor] Backend: VIRGL_ACCELERATED\r\n";
            print(message, sizeof(message) - 1);
        } else if (transport == micant::um::DisplayTransportBackend::Virgl) {
            constexpr char message[] = "[MicaNT Compositor] Backend: VirGL available; software composition active\r\n";
            print(message, sizeof(message) - 1);
        } else if (transport == micant::um::DisplayTransportBackend::Gop) {
            constexpr char message[] = "[MicaNT Compositor] Backend: GOP_FALLBACK + SOFTWARE_COMPOSITOR\r\n";
            print(message, sizeof(message) - 1);
        } else {
            constexpr char message[] = "[MicaNT Compositor] Backend query returned unknown transport\r\n";
            print(message, sizeof(message) - 1);
        }
    } else {
        constexpr char unavailable[] = "[MicaNT Compositor] Backend capability query failed\r\n";
        print(unavailable, sizeof(unavailable) - 1);
    }
    uint64_t previous = UINT64_MAX;
    uint64_t previousSurfaceSetFingerprint = UINT64_MAX;
    bool metadataObserved = false;
    bool invalidHeaderReported = false;
    bool firstPresentationReported = false;
    const auto waitForUpdate = [&](uint64_t observed,
                                   uint64_t timeout100ns = 0) noexcept {
        (void)micant::sys::invokeUserService(0x83, observed, timeout100ns);
#if MICANT_COMPOSITOR_LOCAL_STATS
        ++g_localPerformanceStats.wakeups;
        reportLocalPerformanceStats();
#endif
    };
#if MICANT_COMPOSITOR_LOCAL_STATS
    g_localPerformanceStats.lastReportMilliseconds = performanceMilliseconds();
#endif
    for (;;) {
        uint64_t sequence{};
        if (!copyStableMetadata(metadata, g_metadataSnapshot, sequence)) {
            waitForUpdate(sequence);
            continue;
        }
        const auto& snapshot = g_metadataSnapshot;
        if (snapshot.magic != MICANT_COMPOSITOR_STORE_MAGIC ||
            snapshot.version != MICANT_COMPOSITOR_STORE_VERSION) {
            if (!invalidHeaderReported) {
                constexpr char invalid[] = "[MicaNT Compositor] Shared metadata header invalid\r\n";
                print(invalid, sizeof(invalid) - 1);
                invalidHeaderReported = true;
            }
            waitForUpdate(sequence);
            continue;
        }
        if (!sequence || (sequence & 1u)) {
            waitForUpdate(sequence);
            continue;
        }
        if (snapshot.screenWidth != g_displayWidth ||
            snapshot.screenHeight != g_displayHeight) {
            waitForUpdate(sequence);
            continue;
        }
        if (!ensureAnimationCapacity(snapshot.surfaceEntryCount)) {
            waitForUpdate(sequence);
            continue;
        }
        auto* animationFrames = g_animationFrames;
        buildAnimationFrames(snapshot, animationClockMilliseconds(), animationFrames,
                             g_displayWidth, g_displayHeight);
        bool animationActive = false;
        bool animationDamagePending = false;
        for (size_t index = 0; index < snapshot.surfaceEntryCount; ++index) {
            const auto& frame = animationFrames[index];
            animationActive |= frame.active;
            animationDamagePending |= frame.damageCount != 0;
        }
        const bool newMetadata = sequence != previous;
        const uint64_t hudNow = performanceMilliseconds();
#if MICANT_COMPOSITOR_DEBUG_HUD
        const bool hudDue = !g_hudLastRefresh ||
            (hudNow >= g_hudLastRefresh &&
             hudNow - g_hudLastRefresh >= kHudRefreshMilliseconds);
#else
        constexpr bool hudDue = false;
#endif
        if (!newMetadata && !animationDamagePending && !hudDue) {
#if MICANT_COMPOSITOR_DEBUG_HUD
            const uint64_t remaining = kHudRefreshMilliseconds -
                (hudNow >= g_hudLastRefresh ? hudNow - g_hudLastRefresh : 0);
            waitForUpdate(sequence, remaining * 10000u);
#else
            waitForUpdate(sequence);
#endif
            continue;
        }
        bool surfaceWriteInProgress = false;
        {
            for (size_t index = 0; index < snapshot.surfaceEntryCount; ++index) {
                const auto& surface = snapshot.surfaces[index];
                if ((surface.flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                                      MICANT_COMPOSITOR_SURFACE_VISIBLE)) ==
                        (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                         MICANT_COMPOSITOR_SURFACE_VISIBLE) &&
                    (!(surface.flags & MICANT_COMPOSITOR_SURFACE_ICONIC) ||
                     (surface.animationFlags & MICANT_COMPOSITOR_ANIMATION_ACTIVE)) &&
                    surface.activeWriters) {
                    surfaceWriteInProgress = true;
                    break;
                }
            }
        }
        if (surfaceWriteInProgress) {
            waitForUpdate(sequence);
            continue;
        }
        if (!metadataObserved) {
            constexpr char observed[] = "[MicaNT Compositor] Shared frame metadata observed\r\n";
            print(observed, sizeof(observed) - 1);
            metadataObserved = true;
        }
        DamageSet damage{};
        bool composed = !newMetadata || collectDamage(snapshot, damage);
        uint64_t surfaceSetFingerprint = 1469598103934665603ull;
        const auto mixSurfaceIdentity = [&surfaceSetFingerprint](uint64_t value) noexcept {
            surfaceSetFingerprint ^= value;
            surfaceSetFingerprint *= 1099511628211ull;
        };
        for (size_t index = 0; index < snapshot.surfaceEntryCount; ++index) {
            const auto& surface = snapshot.surfaces[index];
            mixSurfaceIdentity(index);
            mixSurfaceIdentity(surface.window);
            mixSurfaceIdentity(surface.flags &
                (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                 MICANT_COMPOSITOR_SURFACE_VISIBLE |
                 MICANT_COMPOSITOR_SURFACE_ICONIC));
            mixSurfaceIdentity(surface.surfaceAddress);
            mixSurfaceIdentity(surface.surfaceSizeBytes);
            mixSurfaceIdentity((static_cast<uint64_t>(surface.width) << 32) |
                               surface.height);
            mixSurfaceIdentity((static_cast<uint64_t>(surface.pitch) << 32) |
                               surface.zOrder);
            mixSurfaceIdentity((static_cast<uint64_t>(surface.surfaceSourceX) << 32) |
                               surface.surfaceSourceY);
        }
        const bool surfaceSetChanged =
            surfaceSetFingerprint != previousSurfaceSetFingerprint;
        bool usedFullRedraw = false;
        for (size_t animationIndex = 0;
             animationIndex < snapshot.surfaceEntryCount; ++animationIndex) {
            const auto& frame = animationFrames[animationIndex];
            for (size_t index = 0; index < frame.damageCount; ++index)
                addDamageRegion(damage, frame.damage[index],
                                g_displayWidth, g_displayHeight);
        }
#if MICANT_COMPOSITOR_DEBUG_HUD
        if (hudDue) {
            const int32_t hudLeft = static_cast<int32_t>(g_displayWidth > kHudWidth
                ? g_displayWidth - kHudWidth : 0);
            addDamageRegion(damage,
                {hudLeft, 0, static_cast<int32_t>(g_displayWidth),
                 static_cast<int32_t>(g_displayHeight < kHudHeight
                    ? g_displayHeight : kHudHeight)},
                g_displayWidth, g_displayHeight);
            composed = true;
        }
#endif
        if (surfaceSetChanged) {
            damage.count = 1;
            damage.regions[0] = {0, 0, g_displayWidth, g_displayHeight};
            composed = true;
        }
        if (__atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) != sequence)
            continue;
        if (composed) {
#if MICANT_COMPOSITOR_LOCAL_STATS
            const uint64_t composeStart = performanceMilliseconds();
#endif
            composed = composeDamageRegions(snapshot, animationFrames,
                                             decorPixels, damage);
#if MICANT_COMPOSITOR_DEBUG_HUD
            if (composed) {
                if (hudDue) updateHud(hudNow);
                else drawHud(g_hudMemoryBytes, g_hudMemoryValid,
                            g_hudCursorValid, g_hudMouseX, g_hudMouseY);
            }
#endif
#if MICANT_COMPOSITOR_LOCAL_STATS
            const uint64_t composeEnd = performanceMilliseconds();
            if (composeEnd >= composeStart)
                g_localPerformanceStats.composeMilliseconds +=
                    composeEnd - composeStart;
#endif
            if (composed) {
#if MICANT_COMPOSITOR_LOCAL_STATS
                const uint64_t presentStart = performanceMilliseconds();
#endif
                composed = presentDamage(snapshot.screenWidth, damage);
#if MICANT_COMPOSITOR_LOCAL_STATS
                const uint64_t presentEnd = performanceMilliseconds();
                if (presentEnd >= presentStart)
                    g_localPerformanceStats.presentMilliseconds +=
                        presentEnd - presentStart;
                if (!composed) ++g_localPerformanceStats.failedPresents;
#endif
            }
        }
        if (!composed) {
            usedFullRedraw = true;
            const ScreenRect fullScreen{0, 0, g_displayWidth, g_displayHeight};
#if MICANT_COMPOSITOR_LOCAL_STATS
            const uint64_t composeStart = performanceMilliseconds();
#endif
            composed = composeRegion(snapshot, fullScreen, animationFrames,
                                     decorPixels);
#if MICANT_COMPOSITOR_DEBUG_HUD
            if (composed) {
                if (hudDue) updateHud(hudNow);
                drawHud(g_hudMemoryBytes, g_hudMemoryValid, g_hudCursorValid,
                        g_hudMouseX, g_hudMouseY);
            }
#endif
#if MICANT_COMPOSITOR_LOCAL_STATS
            const uint64_t composeEnd = performanceMilliseconds();
            if (composeEnd >= composeStart)
                g_localPerformanceStats.composeMilliseconds +=
                    composeEnd - composeStart;
#endif
            if (composed) {
#if MICANT_COMPOSITOR_LOCAL_STATS
                const uint64_t presentStart = performanceMilliseconds();
#endif
                composed = presentRegion(snapshot.screenWidth, fullScreen);
#if MICANT_COMPOSITOR_LOCAL_STATS
                const uint64_t presentEnd = performanceMilliseconds();
                if (presentEnd >= presentStart)
                    g_localPerformanceStats.presentMilliseconds +=
                        presentEnd - presentStart;
                if (!composed) ++g_localPerformanceStats.failedPresents;
#endif
            }
        }
        if (__atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) == sequence) {
            previous = sequence;
            if (composed)
                previousSurfaceSetFingerprint = surfaceSetFingerprint;
            if (composed && !firstPresentationReported) {
                constexpr char presented[] = "[MicaNT Compositor] First shared frame presented\r\n";
                print(presented, sizeof(presented) - 1);
                firstPresentationReported = true;
            }
            if (composed) {
#if MICANT_COMPOSITOR_DEBUG_HUD
                ++g_hudFrames;
#endif
#if MICANT_COMPOSITOR_LOCAL_STATS
                const uint64_t screenPixels =
                    static_cast<uint64_t>(g_displayWidth) * g_displayHeight;
                const uint64_t damagedPixels = usedFullRedraw
                    ? screenPixels : damagePixelCount(damage);
                const bool fullDamage = usedFullRedraw ||
                    (damage.count == 1 && damage.regions[0].left == 0 &&
                     damage.regions[0].top == 0 &&
                     damage.regions[0].right == g_displayWidth &&
                     damage.regions[0].bottom == g_displayHeight);
                if (fullDamage) ++g_localPerformanceStats.fullRedraws;
                else ++g_localPerformanceStats.partialRedraws;
                if (damagedPixels > UINT64_MAX -
                        g_localPerformanceStats.pixelsComposited)
                    g_localPerformanceStats.pixelsComposited = UINT64_MAX;
                else
                    g_localPerformanceStats.pixelsComposited += damagedPixels;
                const uint64_t transferredBytes = damagedPixels > UINT64_MAX / 4u
                    ? UINT64_MAX : damagedPixels * 4u;
                if (transferredBytes > UINT64_MAX -
                        g_localPerformanceStats.bytesTransferred)
                    g_localPerformanceStats.bytesTransferred = UINT64_MAX;
                else
                    g_localPerformanceStats.bytesTransferred += transferredBytes;
                uint64_t visibleSurfaceCount = 0;
                for (size_t index = 0; index < snapshot.surfaceEntryCount; ++index) {
                    const auto flags = snapshot.surfaces[index].flags;
                    if ((flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                                  MICANT_COMPOSITOR_SURFACE_VISIBLE)) ==
                            (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                             MICANT_COMPOSITOR_SURFACE_VISIBLE))
                        ++visibleSurfaceCount;
                }
                g_localPerformanceStats.surfaceCount = visibleSurfaceCount;
                ++g_localPerformanceStats.frames;
#endif
                auto* writableMetadata = const_cast<MicaCompositorMetadataStore*>(metadata);
                __atomic_store_n(&writableMetadata->compositorAckSequence,
                                 sequence, __ATOMIC_RELEASE);
                if (newMetadata)
                    (void)micant::sys::invokeUserService(0x85, sequence);
                g_hasPresentedFrame = true;
                g_lastThemeGeneration = snapshot.themeGeneration;
            }
        }
        if (animationActive &&
            __atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) == sequence)
            waitForUpdate(sequence, kAnimationFrameInterval100ns);
    }
}
