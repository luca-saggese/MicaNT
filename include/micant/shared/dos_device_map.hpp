#pragma once

#include <array>

namespace micant::shared {
struct DosDeviceMapping {
    const char* driveName;
    const char* targetName;
};

// Boot volumes registered by the current canonical bare-metal object/I/O
// initialization. The same table drives Object Manager links and QueryDosDevice.
inline constexpr std::array<DosDeviceMapping, 4> BareMetalDosDeviceMappings{{
    {"C:", "\\Device\\Harddisk0\\Partition2"},
    {"D:", "\\Device\\Harddisk0\\Partition3"},
    {"E:", "\\Device\\Harddisk1\\Partition2"},
    {"Z:", "\\Device\\Harddisk0\\Partition1"},
}};
}
