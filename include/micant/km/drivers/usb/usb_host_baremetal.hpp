#pragma once
#include "micant/shared/usb_host_abi.hpp"
#include "micant/shared/usb_class_abi.hpp"
#include "micant/shared/ntstatus.hpp"
#include <cstdint>

namespace micant::usb::baremetal {
NtStatus registerHost(const void* owner, const shared::usb::HostServicesV1& services,
                      uint64_t& token) noexcept;
NtStatus startEnumeration() noexcept;
NtStatus stopEnumeration() noexcept;
NtStatus unregisterHost(const void* owner, uint64_t token) noexcept;
NtStatus registerClass(const void* owner, const shared::usb::ClassServicesV1& services,
                       uint64_t& token) noexcept;
NtStatus unregisterClass(const void* owner, uint64_t token) noexcept;
NtStatus unregisterClassesForOwner(const void* owner) noexcept;
}
