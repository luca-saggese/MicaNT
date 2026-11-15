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
micant::NtStatus waitForCompositorUpdate(uint64_t observedSequence,
                                         uint64_t timeout100ns = 0) noexcept;
bool dispatchMouseInput(const MicaWin32kInputEvent& event) noexcept;
bool dispatchKeyboardInput(const MicaWin32kKeyboardEvent& event) noexcept;
micant::NtStatus setCursorImage(const MicaWin32kCursorImage& image,
                                uint64_t ownerProcessId) noexcept;
micant::NtStatus setThemeSnapshot(const MicaThemeSnapshot& snapshot) noexcept;
micant::NtStatus getThemeSnapshot(MicaThemeSnapshot& snapshot) noexcept;

} // namespace micant::win32k::freestanding
