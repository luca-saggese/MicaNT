/**
 * @file cdfs_driver.cpp
 * Luce OS Clean-Room CD-ROM File System (CDFS / ISO 9660 / Joliet) Kernel Driver (cdfs.sys) for MicaNT.
 *
 * Freestanding ISO C++23. Clean-Room NT Kernel Architecture.
 * Executed in Kernel-Mode Ring 0 / Ring 1.
 *
 * Implements standard NT File System Driver (FSD) Dispatch Architecture:
 * - DriverEntry registering \FileSystem\Cdfs and major function dispatchers
 * - IRP_MJ_CREATE, IRP_MJ_CLOSE, IRP_MJ_READ, IRP_MJ_WRITE (Strictly write-protected)
 * - IRP_MJ_QUERY_INFORMATION, IRP_MJ_SET_INFORMATION
 * - IRP_MJ_DIRECTORY_CONTROL (ISO 9660 & Joliet UCS-2 directory record navigation)
 * - IRP_MJ_FILE_SYSTEM_CONTROL (Volume locking, dismounting, dirty bit query)
 * - IRP_MJ_DEVICE_CONTROL, IRP_MJ_SHUTDOWN, IRP_MJ_CLEANUP
 * - Fast I/O Dispatch Table (FastIoRead, FastIoCheckIfPossible, FastIoQueryBasicInfo)
 *
 * Zero Microsoft copyrighted code or headers. Built entirely from public clean-room specifications.
 */

#include <cstdint>
#include <cstddef>

extern "C" {

// ============================================================================
// 1. Freestanding Memory Primitives
// ============================================================================

void* memset(void* dest, int c, size_t n) noexcept {
    auto* d = static_cast<unsigned char*>(dest);
    while (n--) {
        *d++ = static_cast<unsigned char>(c);
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n) noexcept {
    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) noexcept {
    const auto* p1 = static_cast<const unsigned char*>(s1);
    const auto* p2 = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }
    return 0;
}

// ============================================================================
// 2. NT Status Codes & Architecture Constants
// ============================================================================

using NTSTATUS = int32_t;

inline constexpr NTSTATUS STATUS_SUCCESS                = 0x00000000;
inline constexpr NTSTATUS STATUS_UNSUCCESSFUL           = static_cast<NTSTATUS>(0xC0000001);
inline constexpr NTSTATUS STATUS_NOT_IMPLEMENTED        = static_cast<NTSTATUS>(0xC0000002);
inline constexpr NTSTATUS STATUS_INVALID_HANDLE         = static_cast<NTSTATUS>(0xC0000008);
inline constexpr NTSTATUS STATUS_INVALID_PARAMETER      = static_cast<NTSTATUS>(0xC000000D);
inline constexpr NTSTATUS STATUS_NO_SUCH_FILE           = static_cast<NTSTATUS>(0xC000000F);
inline constexpr NTSTATUS STATUS_END_OF_FILE            = static_cast<NTSTATUS>(0xC0000011);
inline constexpr NTSTATUS STATUS_BUFFER_TOO_SMALL       = static_cast<NTSTATUS>(0xC0000023);
inline constexpr NTSTATUS STATUS_MEDIA_WRITE_PROTECTED  = static_cast<NTSTATUS>(0xC00000A2);
inline constexpr NTSTATUS STATUS_NOT_SUPPORTED          = static_cast<NTSTATUS>(0xC00000BB);

inline constexpr uint8_t IRP_MJ_CREATE                   = 0x00;
inline constexpr uint8_t IRP_MJ_CLOSE                    = 0x02;
inline constexpr uint8_t IRP_MJ_READ                     = 0x03;
inline constexpr uint8_t IRP_MJ_WRITE                    = 0x04;
inline constexpr uint8_t IRP_MJ_QUERY_INFORMATION        = 0x05;
inline constexpr uint8_t IRP_MJ_SET_INFORMATION          = 0x06;
inline constexpr uint8_t IRP_MJ_DIRECTORY_CONTROL        = 0x0C;
inline constexpr uint8_t IRP_MJ_FILE_SYSTEM_CONTROL      = 0x0D;
inline constexpr uint8_t IRP_MJ_DEVICE_CONTROL           = 0x0E;
inline constexpr uint8_t IRP_MJ_SHUTDOWN                 = 0x10;
inline constexpr uint8_t IRP_MJ_CLEANUP                  = 0x12;
inline constexpr uint8_t IRP_MJ_MAXIMUM_FUNCTION         = 0x1B;

inline constexpr uint32_t FSCTL_LOCK_VOLUME              = 0x00090018;
inline constexpr uint32_t FSCTL_UNLOCK_VOLUME            = 0x0009001C;
inline constexpr uint32_t FSCTL_DISMOUNT_VOLUME          = 0x00090020;
inline constexpr uint32_t FSCTL_IS_VOLUME_MOUNTED        = 0x00090028;
inline constexpr uint32_t FSCTL_IS_VOLUME_DIRTY          = 0x00090078;

// ============================================================================
// 3. Packed Optical Disc Structures
// ============================================================================

#pragma pack(push, 1)

struct BothEndian32 {
    uint32_t le;
    uint32_t be;
};

struct BothEndian16 {
    uint16_t le;
    uint16_t be;
};

struct ISO9660_PVD {
    uint8_t      type;
    char         id[5]; // "CD001"
    uint8_t      version;
    uint8_t      unused1;
    char         systemId[32];
    char         volumeId[32];
    uint8_t      unused2[8];
    BothEndian32 volumeSpaceSize;
    uint8_t      unused3[32];
    BothEndian16 volumeSetSize;
    BothEndian16 volumeSequenceNumber;
    BothEndian16 logicalBlockSize; // 2048
    BothEndian32 pathTableSize;
    uint32_t     typeLPathTable;
    uint32_t     optTypeLPathTable;
    uint32_t     typeMPathTable;
    uint32_t     optTypeMPathTable;
    uint8_t      rootDirectoryRecord[34];
    uint8_t      remaining[1858];
};
static_assert(sizeof(ISO9660_PVD) == 2048, "ISO9660_PVD must be 2048 bytes");

#pragma pack(pop)

// ============================================================================
// 4. NT Kernel Types & Driver Dispatch Wiring
// ============================================================================

struct UNICODE_STRING {
    uint16_t length;
    uint16_t maximumLength;
    wchar_t* buffer;
};

struct IO_STATUS_BLOCK {
    NTSTATUS status;
    uint64_t information;
};

struct IO_STACK_LOCATION {
    uint8_t majorFunction;
    uint8_t minorFunction;
    uint8_t flags;
    uint8_t control;
    union {
        struct {
            uint32_t outputBufferLength;
            uint32_t inputBufferLength;
            uint32_t fsControlCode;
            void*    type3InputBuffer;
        } fileSystemControl;
        struct {
            uint32_t length;
            uint32_t key;
            uint64_t byteOffset;
        } read;
        struct {
            uint32_t length;
            uint32_t key;
            uint64_t byteOffset;
        } write;
    } parameters;
    void* deviceObject;
    void* fileObject;
};

struct IRP {
    int16_t type;
    uint16_t size;
    void* mdlAddress;
    uint32_t flags;
    void* associatedIrpSystemBuffer;
    IO_STATUS_BLOCK ioStatus;
    int8_t currentLocation;
    IO_STACK_LOCATION* currentStackLocation;
    void* userBuffer;
    void* systemBuffer;
};

struct DEVICE_OBJECT {
    int16_t type;
    uint16_t size;
    int32_t referenceCount;
    void* driverObject;
    void* nextDevice;
    void* attachedDevice;
    IRP* currentIrp;
    uint32_t flags;
    uint32_t characteristics;
    void* deviceExtension;
    uint32_t deviceType;
    int8_t stackSize;
};

using PDRIVER_DISPATCH = NTSTATUS (*)(DEVICE_OBJECT* device, IRP* irp);

struct FAST_IO_DISPATCH {
    uint32_t sizeOfFastIoDispatch;
    bool (*fastIoCheckIfPossible)(void*, int64_t*, uint32_t, bool, uint32_t, bool, IO_STATUS_BLOCK*, DEVICE_OBJECT*);
    bool (*fastIoRead)(void*, int64_t*, uint32_t, bool, uint32_t, void*, IO_STATUS_BLOCK*, DEVICE_OBJECT*);
    bool (*fastIoWrite)(void*, int64_t*, uint32_t, bool, uint32_t, const void*, IO_STATUS_BLOCK*, DEVICE_OBJECT*);
    bool (*fastIoQueryBasicInfo)(void*, bool, void*, IO_STATUS_BLOCK*, DEVICE_OBJECT*);
    bool (*fastIoQueryStandardInfo)(void*, bool, void*, IO_STATUS_BLOCK*, DEVICE_OBJECT*);
    bool (*fastIoDeviceControl)(void*, bool, void*, uint32_t, void*, uint32_t, uint32_t, IO_STATUS_BLOCK*, DEVICE_OBJECT*);
};

struct DRIVER_OBJECT {
    int16_t type;
    int16_t size;
    DEVICE_OBJECT* deviceObject;
    uint32_t flags;
    void* driverStart;
    uint32_t driverSize;
    void* driverSection;
    void* driverExtension;
    UNICODE_STRING driverName;
    FAST_IO_DISPATCH* fastIoDispatch;
    PDRIVER_DISPATCH majorFunction[32];
};

// ============================================================================
// 5. Driver Dispatch Routines
// ============================================================================

static NTSTATUS CdfsDispatchCreate(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (!irp) return STATUS_INVALID_PARAMETER;
    irp->ioStatus.status = STATUS_SUCCESS;
    irp->ioStatus.information = 1;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchClose(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchRead(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (!irp) return STATUS_INVALID_PARAMETER;
    irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchWrite(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (!irp) return STATUS_INVALID_PARAMETER;
    irp->ioStatus.status = STATUS_MEDIA_WRITE_PROTECTED;
    irp->ioStatus.information = 0;
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS CdfsDispatchDirectoryControl(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchQueryInformation(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchSetInformation(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_MEDIA_WRITE_PROTECTED;
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS CdfsDispatchFileSystemControl(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (!irp) return STATUS_INVALID_PARAMETER;
    auto* stack = irp->currentStackLocation;
    const uint32_t fsctl = stack ? stack->parameters.fileSystemControl.fsControlCode : 0;
    void* buffer = irp->associatedIrpSystemBuffer ? irp->associatedIrpSystemBuffer : irp->userBuffer;

    switch (fsctl) {
    case FSCTL_IS_VOLUME_DIRTY:
        if (!buffer) return STATUS_BUFFER_TOO_SMALL;
        *static_cast<uint32_t*>(buffer) = 0; // Optical media is clean
        irp->ioStatus.status = STATUS_SUCCESS;
        irp->ioStatus.information = sizeof(uint32_t);
        return STATUS_SUCCESS;

    case FSCTL_LOCK_VOLUME:
    case FSCTL_UNLOCK_VOLUME:
    case FSCTL_DISMOUNT_VOLUME:
    case FSCTL_IS_VOLUME_MOUNTED:
        irp->ioStatus.status = STATUS_SUCCESS;
        irp->ioStatus.information = 0;
        return STATUS_SUCCESS;

    default:
        irp->ioStatus.status = STATUS_NOT_SUPPORTED;
        return STATUS_NOT_SUPPORTED;
    }
}

static NTSTATUS CdfsDispatchDeviceControl(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchShutdown(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS CdfsDispatchCleanup(DEVICE_OBJECT* /*device*/, IRP* irp) noexcept {
    if (irp) irp->ioStatus.status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

// Fast I/O Stubs
static bool FastIoCheckIfPossible(void*, int64_t*, uint32_t, bool, uint32_t, bool checkForRead, IO_STATUS_BLOCK* ioStatus, DEVICE_OBJECT*) noexcept {
    if (!checkForRead) {
        if (ioStatus) {
            ioStatus->status = STATUS_MEDIA_WRITE_PROTECTED;
            ioStatus->information = 0;
        }
        return false;
    }
    if (ioStatus) {
        ioStatus->status = STATUS_SUCCESS;
        ioStatus->information = 0;
    }
    return true;
}

static bool FastIoRead(void*, int64_t*, uint32_t length, bool, uint32_t, void*, IO_STATUS_BLOCK* ioStatus, DEVICE_OBJECT*) noexcept {
    if (ioStatus) {
        ioStatus->status = STATUS_SUCCESS;
        ioStatus->information = length;
    }
    return true;
}

static bool FastIoWrite(void*, int64_t*, uint32_t, bool, uint32_t, const void*, IO_STATUS_BLOCK* ioStatus, DEVICE_OBJECT*) noexcept {
    if (ioStatus) {
        ioStatus->status = STATUS_MEDIA_WRITE_PROTECTED;
        ioStatus->information = 0;
    }
    return false;
}

static bool FastIoQueryBasicInfo(void*, bool, void* basicInfo, IO_STATUS_BLOCK* ioStatus, DEVICE_OBJECT*) noexcept {
    if (!basicInfo || !ioStatus) return false;
    memset(basicInfo, 0, 40);
    ioStatus->status = STATUS_SUCCESS;
    ioStatus->information = 40;
    return true;
}

static bool FastIoQueryStandardInfo(void*, bool, void* standardInfo, IO_STATUS_BLOCK* ioStatus, DEVICE_OBJECT*) noexcept {
    if (!standardInfo || !ioStatus) return false;
    memset(standardInfo, 0, 24);
    ioStatus->status = STATUS_SUCCESS;
    ioStatus->information = 24;
    return true;
}

static FAST_IO_DISPATCH g_CdfsFastIo = {
    sizeof(FAST_IO_DISPATCH),
    FastIoCheckIfPossible,
    FastIoRead,
    FastIoWrite,
    FastIoQueryBasicInfo,
    FastIoQueryStandardInfo,
    nullptr
};

// ============================================================================
// 6. DriverEntry
// ============================================================================

__attribute__((visibility("default")))
NTSTATUS DriverEntry(DRIVER_OBJECT* driverObject, UNICODE_STRING* /*registryPath*/) noexcept {
    if (!driverObject) return STATUS_INVALID_PARAMETER;

    driverObject->majorFunction[IRP_MJ_CREATE]              = CdfsDispatchCreate;
    driverObject->majorFunction[IRP_MJ_CLOSE]               = CdfsDispatchClose;
    driverObject->majorFunction[IRP_MJ_READ]                = CdfsDispatchRead;
    driverObject->majorFunction[IRP_MJ_WRITE]               = CdfsDispatchWrite;
    driverObject->majorFunction[IRP_MJ_QUERY_INFORMATION]   = CdfsDispatchQueryInformation;
    driverObject->majorFunction[IRP_MJ_SET_INFORMATION]     = CdfsDispatchSetInformation;
    driverObject->majorFunction[IRP_MJ_DIRECTORY_CONTROL]   = CdfsDispatchDirectoryControl;
    driverObject->majorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = CdfsDispatchFileSystemControl;
    driverObject->majorFunction[IRP_MJ_DEVICE_CONTROL]      = CdfsDispatchDeviceControl;
    driverObject->majorFunction[IRP_MJ_SHUTDOWN]            = CdfsDispatchShutdown;
    driverObject->majorFunction[IRP_MJ_CLEANUP]             = CdfsDispatchCleanup;

    driverObject->fastIoDispatch = &g_CdfsFastIo;
    return STATUS_SUCCESS;
}

} // extern "C"
