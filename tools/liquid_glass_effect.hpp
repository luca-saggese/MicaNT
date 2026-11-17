#pragma once

#include "micant/shared/liquid_glass.h"
#include <stdint.h>

namespace micant::compositor::glass {

struct Rect {
    int32_t left{}, top{}, right{}, bottom{};
};

struct Cache {
    static constexpr uint32_t MaxCapture = 384;
    static constexpr uint32_t MaxCard = 240;
    bool valid{};
    uint64_t window{};
    uint64_t sceneGeneration{};
    uint64_t descriptorGeneration{};
    int32_t screenLeft{}, screenTop{};
    uint32_t width{}, height{}, captureWidth{}, captureHeight{};
    uint32_t sampleWidth{}, sampleHeight{};
    float downsample{};
    uint32_t raw[MaxCapture * MaxCapture]{};
    uint32_t sample[MaxCapture * MaxCapture]{};
    uint32_t scratch[MaxCapture * MaxCapture]{};
    uint32_t output[MaxCard * MaxCard]{};
};

bool IsValid(const MicaLiquidGlassDescriptor& descriptor,
             uint32_t windowWidth, uint32_t windowHeight) noexcept;
bool NeedsRefresh(const Cache& cache, uint64_t window, uint64_t sceneGeneration,
                  int32_t screenLeft, int32_t screenTop,
                  const MicaLiquidGlassDescriptor& descriptor) noexcept;
void Render(Cache& cache, uint32_t* frame, uint32_t screenWidth,
            uint32_t screenHeight, uint64_t window, uint64_t sceneGeneration,
            int32_t windowLeft, int32_t windowTop,
            const MicaLiquidGlassDescriptor& descriptor) noexcept;
void Composite(const Cache& cache, uint32_t* frame, uint32_t screenWidth,
               uint32_t screenHeight, Rect clip) noexcept;

} // namespace micant::compositor::glass
