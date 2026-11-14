#pragma once

#include "micant/shared/ntstatus.hpp"
#include "micant/shared/win32k_client.h"

namespace micant::win32k::freestanding {

micant::NtStatus dispatchRequest(MicaWin32kRequest& request,
                                 uint64_t ownerProcessId) noexcept;
void releaseProcess(uint64_t ownerProcessId) noexcept;
bool dispatchMouseInput(const MicaWin32kInputEvent& event) noexcept;
bool dispatchKeyboardInput(const MicaWin32kKeyboardEvent& event) noexcept;

} // namespace micant::win32k::freestanding
