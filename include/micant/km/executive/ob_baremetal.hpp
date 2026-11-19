// ============================================================================
// MicaNT: Sovereign Bare-Metal NT Object Manager
// (ob_baremetal.hpp)
//
// Description:
//   Clean-room freestanding NT Object Manager for Ring 0 bare-metal silicon.
//   Maintains the canonical NT hierarchical object namespace (\Device,
//   \DosDevices, \Driver, \BaseNamedObjects), symbolic link resolution,
//   device object registration, and handle table references.
//
// Clean-Room ISO C++23. Zero proprietary headers or decompiled code.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/dos_device_map.hpp"
#include "micant/shared/ntstatus.hpp"

namespace micant::km::ob_baremetal {

// --- Canonical NT Device Types ---
inline constexpr uint32_t FILE_DEVICE_DISK           = 0x00000007;
inline constexpr uint32_t FILE_DEVICE_KEYBOARD       = 0x0000000B;
inline constexpr uint32_t FILE_DEVICE_NETWORK        = 0x00000012;
inline constexpr uint32_t FILE_DEVICE_PHYSICAL_MEM   = 0x00000018;
inline constexpr uint32_t FILE_DEVICE_SCREEN         = 0x0000001C;
inline constexpr uint32_t FILE_DEVICE_NULL           = 0x00000015;
inline constexpr uint32_t FILE_DEVICE_FILE_SYSTEM    = 0x00000009;

// --- Object Types ---
enum class ObjectType : uint8_t {
    Unknown       = 0,
    Directory     = 1,
    SymbolicLink  = 2,
    Device        = 3,
    Driver        = 4,
    File          = 5,
    Process       = 6,
    Thread        = 7,
    Section       = 8,
    Event         = 9
};

// --- String Helpers (Freestanding) ---
static inline void copyStr(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || maxLen == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < maxLen) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static inline void concatStr(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || !src || maxLen == 0) return;
    size_t dLen = 0;
    while (dst[dLen] && dLen < maxLen) ++dLen;
    size_t sIdx = 0;
    while (src[sIdx] && dLen + 1 < maxLen) {
        dst[dLen++] = src[sIdx++];
    }
    dst[dLen] = '\0';
}

static inline bool strEqual(const char* a, const char* b) noexcept {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return *a == *b;
}

static inline bool strStartsWith(const char* str, const char* prefix) noexcept {
    if (!str || !prefix) return false;
    while (*prefix) {
        char cs = *str++;
        char cp = *prefix++;
        if (cs >= 'A' && cs <= 'Z') cs += 32;
        if (cp >= 'A' && cp <= 'Z') cp += 32;
        if (cs != cp) return false;
    }
    return true;
}

// --- Object Header & Data Payloads ---
struct ObjectHeader {
    ObjectType type{ObjectType::Unknown};
    uint32_t   referenceCount{1};
    uint32_t   handleCount{0};
    char       name[64]{};
    char       path[128]{};
    void*      body{nullptr};
};

struct DevicePayload {
    uint32_t deviceType{0};
    uint32_t characteristics{0};
    void*    driverObject{nullptr};
    void*    deviceExtension{nullptr};
};

struct SymlinkPayload {
    char targetPath[128]{};
};

// --- Freestanding Namespace Storage ---
inline constexpr size_t MAX_OBJECTS = 64;

class BareMetalObjectManager {
private:
    ObjectHeader   m_objects[MAX_OBJECTS]{};
    DevicePayload  m_devices[MAX_OBJECTS]{};
    SymlinkPayload m_symlinks[MAX_OBJECTS]{};
    size_t         m_objectCount{0};
    bool           m_initialized{false};

public:
    static BareMetalObjectManager& get() noexcept {
        static BareMetalObjectManager instance;
        return instance;
    }

    bool isInitialized() const noexcept { return m_initialized; }
    size_t getObjectCount() const noexcept { return m_objectCount; }

    bool createDirectory(const char* path) noexcept {
        if (!path || m_objectCount >= MAX_OBJECTS) return false;
        // Check if already exists
        for (size_t i = 0; i < m_objectCount; ++i) {
            if (strEqual(m_objects[i].path, path)) return true;
        }

        auto& obj = m_objects[m_objectCount++];
        obj.type = ObjectType::Directory;
        obj.referenceCount = 1;
        copyStr(obj.path, path, sizeof(obj.path));

        // Extract base name
        const char* lastSlash = path;
        for (const char* p = path; *p; ++p) {
            if (*p == '\\' || *p == '/') lastSlash = p + 1;
        }
        copyStr(obj.name, lastSlash, sizeof(obj.name));
        return true;
    }

    bool createSymbolicLink(const char* linkPath, const char* targetPath) noexcept {
        if (!linkPath || !targetPath || m_objectCount >= MAX_OBJECTS) return false;
        for (size_t i = 0; i < m_objectCount; ++i) {
            if (strEqual(m_objects[i].path, linkPath)) return false;
        }

        size_t idx = m_objectCount++;
        auto& obj = m_objects[idx];
        obj.type = ObjectType::SymbolicLink;
        obj.referenceCount = 1;
        copyStr(obj.path, linkPath, sizeof(obj.path));

        const char* lastSlash = linkPath;
        for (const char* p = linkPath; *p; ++p) {
            if (*p == '\\' || *p == '/') lastSlash = p + 1;
        }
        copyStr(obj.name, lastSlash, sizeof(obj.name));

        copyStr(m_symlinks[idx].targetPath, targetPath, sizeof(m_symlinks[idx].targetPath));
        obj.body = &m_symlinks[idx];
        return true;
    }

    bool createDevice(const char* devicePath, uint32_t deviceType, DevicePayload** outDevice = nullptr) noexcept {
        if (!devicePath || m_objectCount >= MAX_OBJECTS) return false;
        for (size_t i = 0; i < m_objectCount; ++i) {
            if (strEqual(m_objects[i].path, devicePath)) return false;
        }

        size_t idx = m_objectCount++;
        auto& obj = m_objects[idx];
        obj.type = ObjectType::Device;
        obj.referenceCount = 1;
        copyStr(obj.path, devicePath, sizeof(obj.path));

        const char* lastSlash = devicePath;
        for (const char* p = devicePath; *p; ++p) {
            if (*p == '\\' || *p == '/') lastSlash = p + 1;
        }
        copyStr(obj.name, lastSlash, sizeof(obj.name));

        m_devices[idx].deviceType = deviceType;
        obj.body = &m_devices[idx];
        if (outDevice) *outDevice = &m_devices[idx];
        return true;
    }

    ObjectHeader* lookupObject(const char* path) noexcept {
        if (!path) return nullptr;
        for (size_t i = 0; i < m_objectCount; ++i) {
            if (strEqual(m_objects[i].path, path)) {
                return &m_objects[i];
            }
        }
        return nullptr;
    }

    bool resolvePath(const char* inPath, char* outResolved, size_t maxLen) noexcept {
        if (!inPath || !outResolved || maxLen == 0) return false;
        copyStr(outResolved, inPath, maxLen);

        // Check for symbolic link prefix (e.g. \DosDevices\C: -> \Device\Harddisk0\Partition1)
        for (size_t i = 0; i < m_objectCount; ++i) {
            if (m_objects[i].type == ObjectType::SymbolicLink) {
                size_t lLen = 0;
                while (m_objects[i].path[lLen]) ++lLen;
                if (strStartsWith(outResolved, m_objects[i].path)) {
                    auto* sym = static_cast<const SymlinkPayload*>(m_objects[i].body);
                    if (sym) {
                        char remainder[128]{};
                        copyStr(remainder, outResolved + lLen, sizeof(remainder));
                        copyStr(outResolved, sym->targetPath, maxLen);
                        concatStr(outResolved, remainder, maxLen);
                        return true;
                    }
                }
            }
        }
        return true;
    }

    void dumpNamespace(char* buf, size_t maxLen) const noexcept {
        if (!buf || maxLen == 0) return;
        buf[0] = '\0';
        concatStr(buf, "Canonical NT Object Directory Namespace (\\):\r\n", maxLen);
        concatStr(buf, "-------------------------------------------------------\r\n", maxLen);

        for (size_t i = 0; i < m_objectCount; ++i) {
            const auto& obj = m_objects[i];
            const char* typeStr = "Unknown";
            switch (obj.type) {
                case ObjectType::Directory:    typeStr = "<DIR>   "; break;
                case ObjectType::SymbolicLink: typeStr = "<SYMLNK>"; break;
                case ObjectType::Device:       typeStr = "<DEVICE>"; break;
                case ObjectType::Driver:       typeStr = "<DRIVER>"; break;
                case ObjectType::File:         typeStr = "<FILE>  "; break;
                case ObjectType::Process:      typeStr = "<PROC>  "; break;
                case ObjectType::Thread:       typeStr = "<THREAD>"; break;
                case ObjectType::Section:      typeStr = "<SECT>  "; break;
                case ObjectType::Event:        typeStr = "<EVENT> "; break;
                default: break;
            }

            concatStr(buf, typeStr, maxLen);
            concatStr(buf, " ", maxLen);
            concatStr(buf, obj.path, maxLen);

            if (obj.type == ObjectType::SymbolicLink && obj.body) {
                auto* sym = static_cast<const SymlinkPayload*>(obj.body);
                concatStr(buf, " -> ", maxLen);
                concatStr(buf, sym->targetPath, maxLen);
            } else if (obj.type == ObjectType::Device && obj.body) {
                auto* dev = static_cast<const DevicePayload*>(obj.body);
                concatStr(buf, " [Type: 0x", maxLen);
                char h[12];
                // format hex
                uint32_t val = dev->deviceType;
                int p = 0;
                for (int s = 28; s >= 0; s -= 4) {
                    uint8_t nib = (val >> s) & 0xF;
                    h[p++] = (nib < 10) ? ('0' + nib) : ('A' + nib - 10);
                }
                h[p] = '\0';
                concatStr(buf, h, maxLen);
                concatStr(buf, "]", maxLen);
            }
            concatStr(buf, "\r\n", maxLen);
        }
    }

    void initialize() noexcept {
        if (m_initialized) return;

        // 1. Core NT Directory Hierarchy
        createDirectory("\\");
        createDirectory("\\Device");
        createDirectory("\\DosDevices");
        createDirectory("\\Driver");
        createDirectory("\\BaseNamedObjects");
        createDirectory("\\KernelObjects");

        // 2. Physical Storage & Volume Devices
        createDevice("\\Device\\Harddisk0", FILE_DEVICE_DISK);
        createDevice("\\Device\\Harddisk0\\Partition0", FILE_DEVICE_DISK); // MBR/GPT Raw
        createDevice("\\Device\\Harddisk0\\Partition1", FILE_DEVICE_DISK); // MICA_ESP 1GB FAT32
        createDevice("\\Device\\Harddisk0\\Partition2", FILE_DEVICE_DISK); // MICA_SYS 128GB FAT32
        createDevice("\\Device\\Harddisk0\\Partition3", FILE_DEVICE_DISK); // MICA_DATA 1.7TB FAT32

        createDevice("\\Device\\Harddisk1", FILE_DEVICE_DISK);
        createDevice("\\Device\\Harddisk1\\Partition1", FILE_DEVICE_DISK); // Ubuntu ESP
        createDevice("\\Device\\Harddisk1\\Partition2", FILE_DEVICE_DISK); // Ubuntu Boot Ext4
        createDevice("\\Device\\Harddisk1\\Partition3", FILE_DEVICE_DISK); // Ubuntu Root Ext4

        // 3. Symbolic Link Mappings (Win32 Drive Letters)
        for (const auto& mapping : micant::shared::BareMetalDosDeviceMappings) {
            char linkPath[32] = "\\DosDevices\\";
            size_t linkLength = sizeof("\\DosDevices\\") - 1;
            for (const char* cursor = mapping.driveName; *cursor; ++cursor)
                linkPath[linkLength++] = *cursor;
            linkPath[linkLength] = '\0';
            createSymbolicLink(linkPath, mapping.targetName);
        }

        // 4. Hardware Peripheral Devices
        createDevice("\\Device\\NetworkAdapter0", FILE_DEVICE_NETWORK);     // Intel I210-AT (8086:1533)
        createDevice("\\Device\\KeyboardClass0",  FILE_DEVICE_KEYBOARD);    // xHCI Virtual Keyboard
        createDevice("\\Device\\PhysicalMemory",  FILE_DEVICE_PHYSICAL_MEM); // 32 GB ECC DDR4
        createDevice("\\Device\\Video0",          FILE_DEVICE_SCREEN);        // GOP 1024x768 Framebuffer
        createDevice("\\Device\\Null",            FILE_DEVICE_NULL);          // Null Sink

        // 5. Driver Objects
        createDirectory("\\Driver\\Disk");
        createDirectory("\\Driver\\FastFAT");
        createDirectory("\\Driver\\ExtFS");
        createDirectory("\\Driver\\I210");
        createDirectory("\\Driver\\xHCI");
        createDirectory("\\Driver\\BootVid");

        m_initialized = true;
    }
};

} // namespace micant::km::ob_baremetal
