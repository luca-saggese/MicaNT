#pragma once

#include "micant/shared/ntstatus.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/shared/compositor_surface.h"

namespace micant::win32k::freestanding {

micant::NtStatus dispatchRequest(MicaWin32kRequest& request,
                                 uint64_t ownerProcessId) noexcept;
void releaseProcess(uint64_t ownerProcessId) noexcept;
void bindCompositorStore(MicaCompositorMetadataStore* store,
                         void (*publishCallback)() noexcept) noexcept;
void publishCompositorSurfaces() noexcept;
micant::NtStatus setWindowIconData(MicaWin32kRequest& request,
    uint64_t ownerProcessId, const uint32_t* pixels,
    uint32_t width, uint32_t height) noexcept;
micant::NtStatus setWindowTitleSurface(MicaWin32kRequest& request,
    uint64_t ownerProcessId, const uint32_t* pixels,
    uint32_t width, uint32_t height, uint32_t stride) noexcept;
micant::NtStatus waitForCompositorUpdate(uint64_t observedSequence,
                                         uint64_t timeout100ns = 0) noexcept;
bool dispatchMouseInput(const MicaWin32kInputEvent& event) noexcept;
void initializeCursorPosition(int32_t x, int32_t y) noexcept;
bool dispatchKeyboardInput(const MicaWin32kKeyboardEvent& event) noexcept;
micant::NtStatus setCursorImage(const MicaWin32kCursorImage& image,
                                uint64_t ownerProcessId) noexcept;
micant::NtStatus setThemeSnapshot(const MicaThemeSnapshot& snapshot,
    const MicaThemeCaptionImageSet* captionImages = nullptr) noexcept;
micant::NtStatus getThemeSnapshot(MicaThemeSnapshot& snapshot) noexcept;

} // namespace micant::win32k::freestanding
