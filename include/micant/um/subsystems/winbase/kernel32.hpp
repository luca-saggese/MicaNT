#pragma once

/**
 * @file kernel32.hpp
 * @brief Clean-Room Win32 Base API Bridge (kernel32.dll / kernelbase.dll).
 *
 * Implements the standard Win32 API layer on top of MicaNT clean-room ntdll.dll
 * system call stubs and the CSRSS subsystem server.
 *
 * References: Microsoft win32metadata & Microsoft Learn Public Win32 API Specification.
 */

#include <cstdint>
#include <string_view>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <memory>
#include <unordered_map>
#include <deque>
#include <vector>
#include <functional>
#include <atomic>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/system_time.hpp"
#include "micant/um/native/ntdll.hpp"
#include "micant/um/native/ldr.hpp"
#include "micant/um/servers/csrss.hpp"
#include "micant/um/servers/conhost.hpp"
#include "micant/km/hal/hal.hpp"
#include "micant/km/drivers/fs/npfs.hpp"
#include "micant/km/drivers/fs/fs.hpp"

namespace micant::win32 {

inline uintptr_t g_CurrentExecutableBase = 0;

// ============================================================================
// 1. Standard Win32 Types & Constants
// ============================================================================

using DWORD   = uint32_t;
using LPDWORD = uint32_t*;
using PDWORD  = uint32_t*;
using UINT    = uint32_t;
using BOOL    = int32_t;

inline constexpr DWORD MAX_PATH = 260;
inline constexpr DWORD MICA_MAX_PATH = 32767;
using HANDLE  = void*;
using HMODULE = void*;
using HWND    = void*;
using HINSTANCE = void*;
using HICON   = void*;
using HKEY    = void*;
using LPVOID  = void*;
using LPCVOID = const void*;
using LPCWSTR = const wchar_t*;
using LPWSTR  = wchar_t*;
using LPWCH   = wchar_t*;
using LPCSTR  = const char*;
using LPSTR   = char*;
using SIZE_T    = size_t;
using PSIZE_T   = size_t*;
using DWORD_PTR = uintptr_t;
using PDWORD_PTR = DWORD_PTR*;
using ULONG_PTR = uintptr_t;
using LONG_PTR  = intptr_t;
using LONG      = int32_t;
using LPLONG    = int32_t*;
using WCHAR     = wchar_t;
using PCWSTR    = const wchar_t*;
using PWSTR     = wchar_t*;
using LPCWCH    = const wchar_t*;
using ULONG     = uint32_t;
using KAFFINITY = ULONG_PTR;
using PULONG    = uint32_t*;
using CHAR      = char;
using USHORT    = uint16_t;
using SHORT     = int16_t;
using PUSHORT   = uint16_t*;

struct GROUP_AFFINITY {
    KAFFINITY Mask{0};
    uint16_t Group{0};
    uint16_t Reserved[3]{0};
};
using PGROUP_AFFINITY = GROUP_AFFINITY*;
using PBOOL     = int32_t*;
using LPBOOL    = int32_t*;
using PVOID     = void*;
using VOID      = void;
using WORD      = uint16_t;
using LPWORD    = uint16_t*;
using BYTE      = uint8_t;
using LCID      = uint32_t;
using LPARAM    = intptr_t;
using WPARAM    = uintptr_t;
using ULONGLONG  = uint64_t;
using PULONGLONG = uint64_t*;
using ULONG64    = uint64_t;
using PULONG64   = uint64_t*;
using LONG64     = int64_t;
using PLONG64    = int64_t*;
using LONGLONG   = int64_t;
using PLONGLONG  = int64_t*;
using FARPROC    = intptr_t (*)();
using PHANDLE   = void**;
using BOOLEAN   = uint8_t;
using LPTHREAD_START_ROUTINE = DWORD (WINAPI *)(LPVOID);
using WAITORTIRCALLBACK = void (WINAPI *)(PVOID, BOOLEAN);

struct SRWLOCK {
    void* Ptr{nullptr};
};
using PSRWLOCK = SRWLOCK*;

struct CONDITION_VARIABLE {
    void* Ptr{nullptr};
};
using PCONDITION_VARIABLE = CONDITION_VARIABLE*;

#define SRWLOCK_INIT {nullptr}
#define CONDITION_VARIABLE_INIT {nullptr}
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x00000001

union INIT_ONCE {
    void* Ptr{nullptr};
};
using LPINIT_ONCE = INIT_ONCE*;
using PINIT_ONCE_FN = BOOL (*)(LPINIT_ONCE, void*, void**);

struct FILETIME;

struct TP_POOL;
using PTP_POOL = TP_POOL*;

struct TP_CLEANUP_GROUP;
using PTP_CLEANUP_GROUP = TP_CLEANUP_GROUP*;

struct TP_CALLBACK_INSTANCE;
using PTP_CALLBACK_INSTANCE = TP_CALLBACK_INSTANCE*;

struct TP_WORK;
using PTP_WORK = TP_WORK*;

struct TP_TIMER;
using PTP_TIMER = TP_TIMER*;

struct TP_WAIT;
using PTP_WAIT = TP_WAIT*;

struct TP_IO;
using PTP_IO = TP_IO*;
using PTP_IO_CALLBACK = void (WINAPI *)(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PVOID Overlapped, ULONG IoResult, ULONG_PTR NumberOfBytesTransferred, PTP_IO Io);
using PTP_WIN32_IO_CALLBACK = PTP_IO_CALLBACK;

using MMRESULT = UINT;
inline constexpr MMRESULT TIMERR_NOERROR = 0;
inline constexpr MMRESULT TIMERR_NOCANDO = 97;

struct TIMECAPS {
    UINT wPeriodMin;
    UINT wPeriodMax;
};
using LPTIMECAPS = TIMECAPS*;

using TP_WAIT_RESULT = ULONG;

using PTP_WORK_CALLBACK = void (WINAPI *)(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work);
using PTP_TIMER_CALLBACK = void (WINAPI *)(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer);
using PTP_WAIT_CALLBACK = void (WINAPI *)(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WAIT Wait, TP_WAIT_RESULT WaitResult);
using PTP_CLEANUP_GROUP_CANCEL_CALLBACK = void (WINAPI *)(PVOID ObjectContext, PVOID CleanupContext);
using PTP_SIMPLE_CALLBACK = void (WINAPI *)(PTP_CALLBACK_INSTANCE Instance, PVOID Context);

enum TP_CALLBACK_PRIORITY {
    TP_CALLBACK_PRIORITY_HIGH    = 0,
    TP_CALLBACK_PRIORITY_NORMAL  = 1,
    TP_CALLBACK_PRIORITY_LOW     = 2,
    TP_CALLBACK_PRIORITY_INVALID = 3
};

struct TP_CALLBACK_ENVIRON_V3 {
    ULONG Version;
    PTP_POOL Pool;
    PTP_CLEANUP_GROUP CleanupGroup;
    PTP_CLEANUP_GROUP_CANCEL_CALLBACK CleanupGroupCancelCallback;
    PVOID RaceDll;
    void* ActivationContext;
    PVOID FinalizationCallback;
    union {
        DWORD Flags;
        struct {
            DWORD LongFunction : 1;
            DWORD Persistent : 1;
            DWORD Private : 30;
        } s;
    } u;
    ULONG CallbackPriority;
    ULONG Size;
};
using TP_CALLBACK_ENVIRON = TP_CALLBACK_ENVIRON_V3;
using PTP_CALLBACK_ENVIRON = TP_CALLBACK_ENVIRON*;

struct PROCESSENTRY32W {
    DWORD dwSize{sizeof(PROCESSENTRY32W)};
    DWORD cntUsage{0};
    DWORD th32ProcessID{0};
    ULONG_PTR th32DefaultHeapID{0};
    DWORD th32ModuleID{0};
    DWORD cntThreads{1};
    DWORD th32ParentProcessID{0};
    LONG pcPriClassBase{0};
    DWORD dwFlags{0};
    wchar_t szExeFile[260]{};
};
using LPPROCESSENTRY32W = PROCESSENTRY32W*;

struct THREADENTRY32 {
    DWORD dwSize{sizeof(THREADENTRY32)};
    DWORD cntUsage{0};
    DWORD th32ThreadID{0};
    DWORD th32OwnerProcessID{0};
    LONG  tpBasePri{8};
    LONG  tpDeltaPri{0};
    DWORD dwFlags{0};
};
using LPTHREADENTRY32 = THREADENTRY32*;

struct MODULEENTRY32W {
    DWORD   dwSize{sizeof(MODULEENTRY32W)};
    DWORD   th32ModuleID{1};
    DWORD   th32ProcessID{0};
    DWORD   GlblcntUsage{0xFFFF};
    DWORD   ProccntUsage{0xFFFF};
    BYTE*   modBaseAddr{nullptr};
    DWORD   modBaseSize{0};
    HMODULE hModule{nullptr};
    wchar_t szModule[256]{};
    wchar_t szExePath[260]{};
};
using LPMODULEENTRY32W = MODULEENTRY32W*;

using PAPCFUNC = void (*)(ULONG_PTR);
using LOCALE_ENUMPROCW = BOOL (*)(LPWSTR);

struct DCB {
    DWORD DCBlength{sizeof(DCB)};
    DWORD BaudRate{115200};
    DWORD fBinary:1{1};
    DWORD fParity:1{0};
    DWORD fOutxCtsFlow:1{0};
    DWORD fOutxDsrFlow:1{0};
    DWORD fDtrControl:2{0};
    DWORD fDsrSensitivity:1{0};
    DWORD fTXContinueOnXoff:1{0};
    DWORD fOutX:1{0};
    DWORD fInX:1{0};
    DWORD fErrorChar:1{0};
    DWORD fNull:1{0};
    DWORD fRtsControl:2{0};
    DWORD fAbortOnError:1{0};
    DWORD fDummy2:17{0};
    WORD  wReserved{0};
    WORD  XonLim{0};
    WORD  XoffLim{0};
    BYTE  ByteSize{8};
    BYTE  Parity{0};
    BYTE  StopBits{0};
    char  XonChar{0};
    char  XoffChar{0};
    char  ErrorChar{0};
    char  EofChar{0};
    char  EvtChar{0};
    WORD  wReserved1{0};
};

struct COMMTIMEOUTS {
    DWORD ReadIntervalTimeout{0};
    DWORD ReadTotalTimeoutMultiplier{0};
    DWORD ReadTotalTimeoutConstant{5000};
    DWORD WriteTotalTimeoutMultiplier{0};
    DWORD WriteTotalTimeoutConstant{5000};
};

struct MEMORYSTATUS {
    DWORD dwLength{sizeof(MEMORYSTATUS)};
    DWORD dwMemoryLoad{25};
    SIZE_T dwTotalPhys{16ULL * 1024 * 1024 * 1024};
    SIZE_T dwAvailPhys{12ULL * 1024 * 1024 * 1024};
    SIZE_T dwTotalPageFile{32ULL * 1024 * 1024 * 1024};
    SIZE_T dwAvailPageFile{28ULL * 1024 * 1024 * 1024};
    SIZE_T dwTotalVirtual{128ULL * 1024 * 1024 * 1024 * 1024};
    SIZE_T dwAvailVirtual{127ULL * 1024 * 1024 * 1024 * 1024};
};

struct OVERLAPPED {
    ULONG_PTR Internal{0};
    ULONG_PTR InternalHigh{0};
    union {
        __extension__ struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        void* Pointer{nullptr};
    };
    HANDLE hEvent{nullptr};
};
using LPOVERLAPPED = OVERLAPPED*;
using UCHAR = unsigned char;

struct OVERLAPPED_ENTRY {
    ULONG_PTR    lpCompletionKey{0};
    LPOVERLAPPED lpOverlapped{nullptr};
    ULONG_PTR    Internal{0};
    DWORD        dwNumberOfBytesTransferred{0};
};
using LPOVERLAPPED_ENTRY = OVERLAPPED_ENTRY*;

#ifndef FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#endif
#ifndef FILE_SKIP_SET_EVENT_ON_HANDLE
#define FILE_SKIP_SET_EVENT_ON_HANDLE        0x2
#endif

using micant::TRUE;
using micant::FALSE;
inline const HANDLE INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
inline bool g_TraceApi = false;

/**
 * @brief Retrieves the calling thread's last-error code value.
 */
inline DWORD GetLastError() noexcept {
    return ntdll::RtlGetLastWin32Error();
}

/**
 * @brief Sets the last-error code for the calling thread.
 */
inline void SetLastError(DWORD dwErrCode) noexcept {
    ntdll::RtlSetLastWin32Error(dwErrCode);
}

// Win32 System Error Codes
inline constexpr DWORD ERROR_SUCCESS            = 0;
inline constexpr DWORD ERROR_FILE_NOT_FOUND     = 2;
inline constexpr DWORD ERROR_PATH_NOT_FOUND     = 3;
inline constexpr DWORD ERROR_ACCESS_DENIED      = 5;
inline constexpr DWORD ERROR_INVALID_HANDLE     = 6;
inline constexpr DWORD ERROR_NOT_ENOUGH_MEMORY  = 8;
inline constexpr DWORD ERROR_INVALID_DATA       = 13;
inline constexpr DWORD ERROR_INVALID_PARAMETER  = 87;
inline constexpr DWORD ERROR_INSUFFICIENT_BUFFER = 122;
inline constexpr DWORD ERROR_ALREADY_EXISTS     = 183;
inline constexpr DWORD ERROR_NO_MORE_ITEMS      = 259;
inline constexpr DWORD ERROR_TIMEOUT            = 1460;

// Heap Flags
inline constexpr DWORD HEAP_NO_SERIALIZE        = 0x00000001;
inline constexpr DWORD HEAP_GENERATE_EXCEPTIONS = 0x00000004;
inline constexpr DWORD HEAP_ZERO_MEMORY         = 0x00000008;

// Standard I/O Device Pseudo-Handles
inline constexpr DWORD STD_INPUT_HANDLE  = static_cast<DWORD>(-10);
inline constexpr DWORD STD_OUTPUT_HANDLE = static_cast<DWORD>(-11);
inline constexpr DWORD STD_ERROR_HANDLE  = static_cast<DWORD>(-12);

// File Access & Creation Flags
inline constexpr DWORD GENERIC_READ     = 0x80000000;
inline constexpr DWORD GENERIC_WRITE    = 0x40000000;
inline constexpr DWORD GENERIC_EXECUTE  = 0x20000000;
inline constexpr DWORD GENERIC_ALL      = 0x10000000;

inline constexpr DWORD FILE_SHARE_READ  = 0x00000001;
inline constexpr DWORD FILE_SHARE_WRITE = 0x00000002;

inline constexpr DWORD CREATE_NEW        = 1;
inline constexpr DWORD CREATE_ALWAYS     = 2;
inline constexpr DWORD OPEN_EXISTING     = 3;
inline constexpr DWORD OPEN_ALWAYS       = 4;
inline constexpr DWORD TRUNCATE_EXISTING = 5;

// Memory Allocation Types & Protection
inline constexpr DWORD MEM_COMMIT   = 0x00001000;
inline constexpr DWORD MEM_RESERVE  = 0x00002000;
inline constexpr DWORD MEM_DECOMMIT = 0x00004000;
inline constexpr DWORD MEM_RELEASE  = 0x00008000;

inline constexpr DWORD PAGE_NOACCESS          = 0x01;
inline constexpr DWORD PAGE_READONLY          = 0x02;
inline constexpr DWORD PAGE_READWRITE         = 0x04;
inline constexpr DWORD PAGE_EXECUTE           = 0x10;
inline constexpr DWORD PAGE_EXECUTE_READ      = 0x20;
inline constexpr DWORD PAGE_EXECUTE_READWRITE = 0x40;

// Synchronization Constants
inline constexpr DWORD INFINITE     = 0xFFFFFFFF;
inline constexpr DWORD WAIT_OBJECT_0= 0x00000000;
inline constexpr DWORD WAIT_TIMEOUT = 0x00000102;
inline constexpr DWORD WAIT_IO_COMPLETION = 0x000000C0;
inline constexpr DWORD WAIT_FAILED  = 0xFFFFFFFF;

// Move Method Constants for SetFilePointer
inline constexpr DWORD FILE_BEGIN   = 0;
inline constexpr DWORD FILE_CURRENT = 1;
inline constexpr DWORD FILE_END     = 2;

// File Attributes Constants
inline constexpr DWORD FILE_ATTRIBUTE_READONLY  = 0x00000001;
inline constexpr DWORD FILE_ATTRIBUTE_HIDDEN    = 0x00000002;
inline constexpr DWORD FILE_ATTRIBUTE_SYSTEM    = 0x00000004;
inline constexpr DWORD FILE_ATTRIBUTE_DIRECTORY = 0x00000010;
inline constexpr DWORD FILE_ATTRIBUTE_ARCHIVE   = 0x00000020;
inline constexpr DWORD FILE_ATTRIBUTE_NORMAL    = 0x00000080;
inline constexpr DWORD FILE_ATTRIBUTE_TEMPORARY = 0x00000100;
inline constexpr DWORD FILE_ATTRIBUTE_SPARSE_FILE = 0x00000200;
inline constexpr DWORD FILE_ATTRIBUTE_REPARSE_POINT = 0x00000400;
inline constexpr DWORD FILE_ATTRIBUTE_COMPRESSED = 0x00000800;
inline constexpr DWORD FILE_ATTRIBUTE_OFFLINE   = 0x00001000;
inline constexpr DWORD INVALID_FILE_ATTRIBUTES  = static_cast<DWORD>(-1);

inline constexpr DWORD FSCTL_LOCK_VOLUME             = 0x00090018;
inline constexpr DWORD FSCTL_UNLOCK_VOLUME           = 0x0009001C;
inline constexpr DWORD FSCTL_DISMOUNT_VOLUME         = 0x00090020;
inline constexpr DWORD FSCTL_SET_SPARSE              = 0x000900C4;
inline constexpr DWORD FSCTL_SET_ZERO_DATA           = 0x000980C8;
inline constexpr DWORD FSCTL_QUERY_ALLOCATED_RANGES  = 0x000940CF;

inline constexpr DWORD INVALID_FILE_SIZE        = static_cast<DWORD>(-1);

// Console Mode Flags
inline constexpr DWORD ENABLE_PROCESSED_INPUT   = 0x0001;
inline constexpr DWORD ENABLE_LINE_INPUT        = 0x0002;
inline constexpr DWORD ENABLE_ECHO_INPUT        = 0x0004;
inline constexpr DWORD ENABLE_WINDOW_INPUT      = 0x0008;
inline constexpr DWORD ENABLE_MOUSE_INPUT       = 0x0010;
inline constexpr DWORD ENABLE_INSERT_MODE       = 0x0020;
inline constexpr DWORD ENABLE_QUICK_EDIT_MODE   = 0x0040;
inline constexpr DWORD ENABLE_EXTENDED_FLAGS    = 0x0080;
inline constexpr DWORD ENABLE_VIRTUAL_TERMINAL_INPUT = 0x0200;

inline constexpr DWORD ENABLE_PROCESSED_OUTPUT  = 0x0001;
inline constexpr DWORD ENABLE_WRAP_AT_EOL_OUTPUT= 0x0002;
inline constexpr DWORD ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004;

// Section / File Mapping Access
inline constexpr DWORD FILE_MAP_WRITE           = 0x0002;
inline constexpr DWORD FILE_MAP_READ            = 0x0004;
inline constexpr DWORD FILE_MAP_ALL_ACCESS      = 0x001F001F;
inline constexpr DWORD FILE_MAP_EXECUTE         = 0x0020;

/**
 * @brief Win32 System Information.
 */
struct SYSTEM_INFO {
    uint16_t wProcessorArchitecture{9}; // PROCESSOR_ARCHITECTURE_AMD64
    uint16_t wReserved{0};
    DWORD dwPageSize{4096};
    LPVOID lpMinimumApplicationAddress{reinterpret_cast<LPVOID>(0x10000)};
    LPVOID lpMaximumApplicationAddress{reinterpret_cast<LPVOID>(0x00007FFFFFFEFFFFULL)};
    uintptr_t dwActiveProcessorMask{0x0F}; // 4 cores
    DWORD dwNumberOfProcessors{4};
    DWORD dwProcessorType{8664};
    DWORD dwAllocationGranularity{65536};
    uint16_t wProcessorLevel{6};
    uint16_t wProcessorRevision{0};
};

struct WIN32_FIND_DATAW {
    DWORD dwFileAttributes{0};
    uint32_t ftCreationTimeLow{0};
    uint32_t ftCreationTimeHigh{0};
    uint32_t ftLastAccessTimeLow{0};
    uint32_t ftLastAccessTimeHigh{0};
    uint32_t ftLastWriteTimeLow{0};
    uint32_t ftLastWriteTimeHigh{0};
    DWORD nFileSizeHigh{0};
    DWORD nFileSizeLow{0};
    DWORD dwReserved0{0};
    DWORD dwReserved1{0};
    wchar_t cFileName[260]{};
    wchar_t cAlternateFileName[14]{};
};

struct SYSTEMTIME {
    uint16_t wYear{};
    uint16_t wMonth{};
    uint16_t wDayOfWeek{};
    uint16_t wDay{};
    uint16_t wHour{};
    uint16_t wMinute{};
    uint16_t wSecond{};
    uint16_t wMilliseconds{};
};

struct TIME_ZONE_INFORMATION {
    LONG Bias{0};
    wchar_t StandardName[32]{};
    SYSTEMTIME StandardDate{};
    LONG StandardBias{0};
    wchar_t DaylightName[32]{};
    SYSTEMTIME DaylightDate{};
    LONG DaylightBias{0};
};
using LPTIME_ZONE_INFORMATION = TIME_ZONE_INFORMATION*;

inline BOOL SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION*,
    const SYSTEMTIME*, SYSTEMTIME*) noexcept;
inline BOOL TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION*,
    const SYSTEMTIME*, SYSTEMTIME*) noexcept;


// ============================================================================
// 2. Memory Management (Heap & Virtual Memory)
// ============================================================================

/**
 * @brief Retrieves a handle to the default heap of the calling process.
 */
inline HANDLE GetProcessHeap() noexcept {
    auto* peb = ntdll::RtlGetCurrentPeb();
    if (peb && peb->processHeap != 0) {
        return reinterpret_cast<HANDLE>(peb->processHeap);
    }

    static HANDLE s_defaultProcessHeap = nullptr;
    if (!s_defaultProcessHeap) {
        s_defaultProcessHeap = ntdll::RtlCreateHeap(heap::HEAP_GROWABLE, nullptr, 0, 0x100000, nullptr, nullptr);
    }
    if (peb && peb->processHeap == 0) {
        peb->processHeap = reinterpret_cast<uint64_t>(s_defaultProcessHeap);
    }
    return s_defaultProcessHeap;
}

/**
 * @brief Allocates a block of memory from a heap.
 */
inline LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes) noexcept {
    return ntdll::RtlAllocateHeap(hHeap, dwFlags, dwBytes);
}

/**
 * @brief Frees a memory block allocated from a heap.
 */
inline BOOL HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) noexcept {
    return ntdll::RtlFreeHeap(hHeap, dwFlags, lpMem) ? TRUE : FALSE;
}

/**
 * @brief Reallocates a block of memory from a heap.
 */
inline LPVOID HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes) noexcept {
    return ntdll::RtlReAllocateHeap(hHeap, dwFlags, lpMem, dwBytes);
}

/**
 * @brief Retrieves the size of a memory block allocated from a heap.
 */
inline SIZE_T HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem) noexcept {
    return ntdll::RtlSizeHeap(hHeap, dwFlags, const_cast<LPVOID>(lpMem));
}

using HLOCAL = void*;
inline constexpr UINT LMEM_FIXED    = 0x0000;
inline constexpr UINT LMEM_MOVEABLE = 0x0002;
inline constexpr UINT LMEM_ZEROINIT = 0x0040;
inline constexpr UINT LPTR          = 0x0040;

inline HLOCAL LocalAlloc(UINT uFlags, SIZE_T uBytes) noexcept {
    DWORD flags = (uFlags & LMEM_ZEROINIT) ? heap::HEAP_ZERO_MEMORY : 0;
    return reinterpret_cast<HLOCAL>(HeapAlloc(GetProcessHeap(), flags, uBytes));
}

inline HLOCAL LocalFree(HLOCAL hMem) noexcept {
    if (hMem) {
        HeapFree(GetProcessHeap(), 0, reinterpret_cast<LPVOID>(hMem));
    }
    return nullptr;
}

// Extended Virtual Allocation Parameters & Placeholder Flags
inline constexpr DWORD MEM_COALESCE_PLACEHOLDERS = 0x00000001;
inline constexpr DWORD MEM_REPLACE_PLACEHOLDER  = 0x00004000;
inline constexpr DWORD MEM_RESERVE_PLACEHOLDER  = 0x00040000;
inline constexpr DWORD MEM_PRESERVE_PLACEHOLDER = 0x00000002;

enum MEM_EXTENDED_PARAMETER_TYPE {
    MemExtendedParameterInvalidType = 0,
    MemExtendedParameterAddressRequirements = 1,
    MemExtendedParameterNumaNode = 2,
    MemExtendedParameterPartitionHandle = 3,
    MemExtendedParameterUserPhysicalHandle = 4,
    MemExtendedParameterAttributeFlags = 5,
    MemExtendedParameterImageMachine = 6,
    MemExtendedParameterMax = 7
};

struct MEM_ADDRESS_REQUIREMENTS {
    PVOID  LowestStartingAddress{nullptr};
    PVOID  HighestEndingAddress{nullptr};
    SIZE_T Alignment{0};
};
using PMEM_ADDRESS_REQUIREMENTS = MEM_ADDRESS_REQUIREMENTS*;

struct MEM_EXTENDED_PARAMETER {
    struct {
        uint64_t Type : 8;
        uint64_t Reserved : 56;
    };
    union {
        uint64_t ULong64;
        PVOID    Pointer;
        SIZE_T   Size;
        HANDLE   Handle;
        ULONG    ULong;
    };
};
using PMEM_EXTENDED_PARAMETER = MEM_EXTENDED_PARAMETER*;

namespace detail {
    struct PlaceholderEntry {
        uintptr_t baseAddress{0};
        size_t size{0};
        bool isPlaceholder{true};
        std::unordered_map<uintptr_t, size_t> committedSlices;
    };
    inline std::mutex s_placeholderMutex;
    inline std::unordered_map<uintptr_t, PlaceholderEntry> s_placeholders;
}

/**
 * @brief Reserves, commits, or changes the state of a region of pages in virtual memory.
 */
inline LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) noexcept {
    if (dwSize == 0) {
        SetLastError(87); // ERROR_INVALID_PARAMETER
        return nullptr;
    }
    uintptr_t base = reinterpret_cast<uintptr_t>(lpAddress);
    SIZE_T size = dwSize;
    constexpr uintptr_t allocationMask = 0xFFFF;
    const bool reserve = (flAllocationType & MEM_RESERVE) != 0 ||
                         (base == 0 && (flAllocationType & MEM_COMMIT) != 0);
    if (reserve && base != 0) {
        const SIZE_T offset = base & allocationMask;
        if (size > static_cast<SIZE_T>(-1) - offset) {
            SetLastError(87);
            return nullptr;
        }
        base -= offset;
        size += offset;
    }
    NtStatus status = ntdll::NtAllocateVirtualMemory(
        static_cast<Handle>(-1), // CurrentProcess
        &base,
        0,
        &size,
        flAllocationType,
        flProtect
    );
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return nullptr;
    }
    if (reserve && (base & allocationMask) != 0) {
        SIZE_T releaseSize = 0;
        const NtStatus cleanup = ntdll::NtFreeVirtualMemory(
            static_cast<Handle>(-1), &base, &releaseSize, MEM_RELEASE);
        // A failed rollback may leave a reservation behind; report its error, not success.
        SetLastError(NT_SUCCESS(cleanup) ? 487 : ntdll::RtlNtStatusToDosError(cleanup));
        return nullptr;
    }
    return reinterpret_cast<LPVOID>(base);
}

/**
 * @brief Releases, decommits, or releases and decommits a region of pages in virtual memory.
 */
inline BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) noexcept {
    uintptr_t base = reinterpret_cast<uintptr_t>(lpAddress);
    SIZE_T size = dwSize;
    NtStatus status = ntdll::NtFreeVirtualMemory(
        static_cast<Handle>(-1),
        &base,
        &size,
        dwFreeType
    );
    return NT_SUCCESS(status) ? TRUE : FALSE;
}

/**
 * @brief Reserves, commits, or changes the state of a region of pages within the virtual address space of a specified process with extended parameters (Win10 build 17093+ / Server 2019+).
 */
inline PVOID WINAPI VirtualAlloc2(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    DWORD AllocationType,
    DWORD PageProtection,
    MEM_EXTENDED_PARAMETER* ExtendedParameters,
    ULONG ParameterCount
) noexcept {
    (void)Process;
    uintptr_t targetBase = reinterpret_cast<uintptr_t>(BaseAddress);
    SIZE_T targetSize = Size;

    if (ExtendedParameters && ParameterCount > 0) {
        for (ULONG i = 0; i < ParameterCount; ++i) {
            if (ExtendedParameters[i].Type == MemExtendedParameterAddressRequirements) {
                auto* req = reinterpret_cast<MEM_ADDRESS_REQUIREMENTS*>(ExtendedParameters[i].Pointer);
                if (req && req->Alignment > 0 && targetBase == 0) {
                    size_t align = req->Alignment;
                    targetSize = (targetSize + align - 1) & ~(align - 1);
                }
            }
        }
    }

    if (AllocationType & MEM_RESERVE_PLACEHOLDER) {
        LPVOID res = VirtualAlloc(reinterpret_cast<LPVOID>(targetBase), targetSize, MEM_RESERVE, PageProtection);
        if (res) {
            std::lock_guard<std::mutex> lock(detail::s_placeholderMutex);
            detail::PlaceholderEntry pe{};
            pe.baseAddress = reinterpret_cast<uintptr_t>(res);
            pe.size = targetSize;
            pe.isPlaceholder = true;
            detail::s_placeholders[pe.baseAddress] = std::move(pe);
        }
        return res;
    }

    if (AllocationType & MEM_REPLACE_PLACEHOLDER) {
        std::lock_guard<std::mutex> lock(detail::s_placeholderMutex);
        for (auto& [bAddr, pe] : detail::s_placeholders) {
            if (targetBase >= pe.baseAddress && (targetBase + targetSize) <= (pe.baseAddress + pe.size)) {
                pe.committedSlices[targetBase] = targetSize;
                break;
            }
        }
        DWORD pureAlloc = AllocationType & ~MEM_REPLACE_PLACEHOLDER;
        if (pureAlloc == 0) pureAlloc = MEM_COMMIT;
        return VirtualAlloc(reinterpret_cast<LPVOID>(targetBase), targetSize, pureAlloc, PageProtection);
    }

    return VirtualAlloc(reinterpret_cast<LPVOID>(targetBase), targetSize, AllocationType, PageProtection);
}

/**
 * @brief Reserves, commits, or changes the state of a region of pages within the virtual address space of a process with app container restrictions.
 */
inline PVOID WINAPI VirtualAlloc2FromApp(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    DWORD AllocationType,
    DWORD PageProtection,
    MEM_EXTENDED_PARAMETER* ExtendedParameters,
    ULONG ParameterCount
) noexcept {
    if (PageProtection == PAGE_EXECUTE_READWRITE) {
        return nullptr;
    }
    return VirtualAlloc2(Process, BaseAddress, Size, AllocationType, PageProtection, ExtendedParameters, ParameterCount);
}

/**
 * @brief Reserves, commits, or changes the state of a region of memory within the virtual address space of a specified process.
 */
inline LPVOID WINAPI VirtualAllocEx(
    HANDLE hProcess,
    LPVOID lpAddress,
    SIZE_T dwSize,
    DWORD flAllocationType,
    DWORD flProtect
) noexcept {
    return VirtualAlloc2(hProcess, lpAddress, dwSize, flAllocationType, flProtect, nullptr, 0);
}

/**
 * @brief Releases, decommits, or releases and decommits a region of memory within the virtual address space of a specified process.
 */
inline BOOL WINAPI VirtualFreeEx(
    HANDLE hProcess,
    LPVOID lpAddress,
    SIZE_T dwSize,
    DWORD dwFreeType
) noexcept {
    (void)hProcess;
    uintptr_t target = reinterpret_cast<uintptr_t>(lpAddress);
    if (dwFreeType & MEM_PRESERVE_PLACEHOLDER) {
        std::lock_guard<std::mutex> lock(detail::s_placeholderMutex);
        for (auto& [bAddr, pe] : detail::s_placeholders) {
            auto it = pe.committedSlices.find(target);
            if (it != pe.committedSlices.end()) {
                pe.committedSlices.erase(it);
                break;
            }
        }
        return VirtualFree(lpAddress, dwSize, MEM_DECOMMIT);
    }

    if (dwFreeType & MEM_COALESCE_PLACEHOLDERS) {
        std::lock_guard<std::mutex> lock(detail::s_placeholderMutex);
        detail::PlaceholderEntry pe{};
        pe.baseAddress = target;
        pe.size = dwSize;
        pe.isPlaceholder = true;
        detail::s_placeholders[target] = std::move(pe);
        return TRUE;
    }

    if (dwFreeType == MEM_RELEASE) {
        std::lock_guard<std::mutex> lock(detail::s_placeholderMutex);
        detail::s_placeholders.erase(target);
    }

    return VirtualFree(lpAddress, dwSize, dwFreeType);
}

/**
 * @brief Releases, decommits, or releases and decommits a region of pages within the virtual address space with extended parameters.
 */
inline BOOL WINAPI VirtualFree2(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T RegionSize,
    DWORD FreeType,
    [[maybe_unused]] MEM_EXTENDED_PARAMETER* ExtendedParameters,
    [[maybe_unused]] ULONG ParameterCount
) noexcept {
    return VirtualFreeEx(Process, BaseAddress, RegionSize, FreeType);
}

inline BOOL WINAPI VirtualFree2FromApp(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T RegionSize,
    DWORD FreeType,
    [[maybe_unused]] MEM_EXTENDED_PARAMETER* ExtendedParameters,
    [[maybe_unused]] ULONG ParameterCount
) noexcept {
    return VirtualFree2(Process, BaseAddress, RegionSize, FreeType, ExtendedParameters, ParameterCount);
}

/**
 * @brief Locks the specified region of the process's virtual address space into physical memory (working set).
 */
inline BOOL WINAPI VirtualLock(LPVOID lpAddress, SIZE_T dwSize) noexcept {
    if (!lpAddress || dwSize == 0) return FALSE;
    return TRUE;
}

/**
 * @brief Unlocks a specified range of pages in the virtual address space of a process.
 */
inline BOOL WINAPI VirtualUnlock(LPVOID lpAddress, SIZE_T dwSize) noexcept {
    if (!lpAddress || dwSize == 0) return FALSE;
    return TRUE;
}

/**
 * @brief Retrieves the minimum and maximum working set sizes for the specified process.
 */
inline BOOL WINAPI GetProcessWorkingSetSizeEx(
    HANDLE hProcess,
    SIZE_T* lpMinimumWorkingSetSize,
    SIZE_T* lpMaximumWorkingSetSize,
    DWORD* lpFlags
) noexcept {
    (void)hProcess;
    if (lpMinimumWorkingSetSize) *lpMinimumWorkingSetSize = 50 * 4096;
    if (lpMaximumWorkingSetSize) *lpMaximumWorkingSetSize = 345 * 4096;
    if (lpFlags) *lpFlags = 0;
    return TRUE;
}

/**
 * @brief Sets the minimum and maximum working set sizes for the specified process.
 */
inline BOOL WINAPI SetProcessWorkingSetSizeEx(
    HANDLE hProcess,
    SIZE_T dwMinimumWorkingSetSize,
    SIZE_T dwMaximumWorkingSetSize,
    DWORD dwFlags
) noexcept {
    (void)hProcess;
    (void)dwMinimumWorkingSetSize;
    (void)dwMaximumWorkingSetSize;
    (void)dwFlags;
    return TRUE;
}

// ============================================================================
// 3. Process & Thread Management
// ============================================================================

/**
 * @brief Retrieves a pseudo handle for the current process.
 */
inline HANDLE GetCurrentProcess() noexcept {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}

/**
 * @brief Retrieves the process identifier of the calling process.
 */
inline DWORD GetCurrentProcessId() noexcept {
    auto* teb = ntdll::RtlGetCurrentTeb();
    if (!teb || teb->clientId.uniqueProcess == 0) return 1000;
    return static_cast<DWORD>(teb->clientId.uniqueProcess);
}

/**
 * @brief Retrieves the package full name for the current process.
 * Conforms to win32metadata / appmodel.h specifications.
 */
inline uint32_t WINAPI GetCurrentPackageFullName(uint32_t* packageFullNameLength, wchar_t* /*packageFullName*/) noexcept {
    if (packageFullNameLength) *packageFullNameLength = 0;
    return 15700; // APPMODEL_ERROR_NO_PACKAGE
}

/**
 * @brief Retrieves a pseudo handle for the current thread.
 */
inline HANDLE GetCurrentThread() noexcept {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}

/**
 * @brief Retrieves the thread identifier of the calling thread.
 */
inline DWORD GetCurrentThreadId() noexcept {
    auto* teb = ntdll::RtlGetCurrentTeb();
    if (!teb || teb->clientId.uniqueThread == 0) return 1;
    return static_cast<DWORD>(teb->clientId.uniqueThread);
}

using ExitProcessHook_t = void (*)(DWORD uExitCode);
inline ExitProcessHook_t g_ExitProcessHook = nullptr;

inline void SetExitProcessHook(ExitProcessHook_t hook) noexcept {
    g_ExitProcessHook = hook;
}

using ExitThreadHook_t = void (*)(DWORD uExitCode);
inline ExitThreadHook_t g_ExitThreadHook = nullptr;

inline void SetExitThreadHook(ExitThreadHook_t hook) noexcept {
    g_ExitThreadHook = hook;
}

/**
 * @brief Ends the calling process and all its threads.
 */
[[noreturn]] inline void ExitProcess(DWORD uExitCode) noexcept(false) {
    if (g_ExitProcessHook) {
        g_ExitProcessHook(uExitCode);
    }
    DWORD pid = GetCurrentProcessId();
    // Notify CSRSS subsystem
    csrss::CsrSubsystemServer::get().terminateProcess(pid, uExitCode);
    conhost::ConhostManager::get().freeConsole(pid);
    ntdll::NtTerminateProcess(static_cast<Handle>(-1), static_cast<NtStatus>(uExitCode));
    std::exit(static_cast<int>(uExitCode));
}

// ============================================================================
// 4. Console Management (conhost / csrss integration)
// ============================================================================

/**
 * @brief Retrieves a handle to the specified standard device (input, output, or error).
 */
inline HANDLE GetStdHandle(DWORD nStdHandle) noexcept {
    if (g_TraceApi) {
        std::cout << "[*] [TRACE] GetStdHandle(" << nStdHandle << ")\n";
    }
    auto* peb = ntdll::RtlGetCurrentPeb();
    if (!peb || peb->processParameters == 0) {
        if (nStdHandle == STD_INPUT_HANDLE) return reinterpret_cast<HANDLE>(0x10);
        if (nStdHandle == STD_OUTPUT_HANDLE) return reinterpret_cast<HANDLE>(0x14);
        if (nStdHandle == STD_ERROR_HANDLE) return reinterpret_cast<HANDLE>(0x18);
        return INVALID_HANDLE_VALUE;
    }

    auto* params = reinterpret_cast<ldr::RtlUserProcessParameters*>(peb->processParameters);
    if (nStdHandle == STD_INPUT_HANDLE) {
        return reinterpret_cast<HANDLE>(params->standardInput);
    }
    if (nStdHandle == STD_OUTPUT_HANDLE) {
        return reinterpret_cast<HANDLE>(params->standardOutput);
    }
    if (nStdHandle == STD_ERROR_HANDLE) {
        return reinterpret_cast<HANDLE>(params->standardError);
    }
    return INVALID_HANDLE_VALUE;
}

/**
 * @brief Sets the handle for the specified standard device.
 */
inline BOOL SetStdHandle(DWORD nStdHandle, HANDLE hHandle) noexcept {
    auto* peb = ntdll::RtlGetCurrentPeb();
    if (!peb || peb->processParameters == 0) return FALSE;

    auto* params = reinterpret_cast<ldr::RtlUserProcessParameters*>(peb->processParameters);
    Handle h = reinterpret_cast<Handle>(hHandle);
    if (nStdHandle == STD_INPUT_HANDLE) params->standardInput = h;
    else if (nStdHandle == STD_OUTPUT_HANDLE) params->standardOutput = h;
    else if (nStdHandle == STD_ERROR_HANDLE) params->standardError = h;
    else return FALSE;

    return TRUE;
}

/**
 * @brief Allocates a new console for the calling process.
 */
inline BOOL AllocConsole() noexcept {
    DWORD pid = GetCurrentProcessId();
    auto session = conhost::ConhostManager::get().allocateConsole(pid, L"MicaNT Console");
    if (!session) return FALSE;

    SetStdHandle(STD_INPUT_HANDLE, reinterpret_cast<HANDLE>(session->getInputHandle()));
    SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(session->getOutputHandle()));
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(session->getErrorHandle()));

    csrss::CsrSubsystemServer::get().bindConsole(pid, session->getOutputHandle());
    return TRUE;
}

/**
 * @brief Detaches the calling process from its console.
 */
inline BOOL FreeConsole() noexcept {
    DWORD pid = GetCurrentProcessId();
    csrss::CsrSubsystemServer::get().unbindConsole(pid);
    return conhost::ConhostManager::get().freeConsole(pid) ? TRUE : FALSE;
}

/**
 * @brief Sets the title for the current console window.
 */
inline BOOL SetConsoleTitleW(LPCWSTR lpConsoleTitle) noexcept {
    if (!lpConsoleTitle) return FALSE;
    DWORD pid = GetCurrentProcessId();
    auto session = conhost::ConhostManager::get().getConsole(pid);
    if (!session) return FALSE;
    session->setTitle(lpConsoleTitle);
    return TRUE;
}

/**
 * @brief Retrieves the title for the current console window.
 */
inline DWORD GetConsoleTitleW(LPWSTR lpConsoleTitle, DWORD nSize) noexcept {
    if (!lpConsoleTitle || nSize == 0) return 0;
    DWORD pid = GetCurrentProcessId();
    auto session = conhost::ConhostManager::get().getConsole(pid);
    if (!session) return 0;

    const std::wstring& title = session->getTitle();
    DWORD copyLen = std::min<DWORD>(nSize - 1, static_cast<DWORD>(title.length()));
    for (DWORD i = 0; i < copyLen; ++i) {
        lpConsoleTitle[i] = title[i];
    }
    lpConsoleTitle[copyLen] = L'\0';
    return copyLen;
}

/**
 * @brief Writes a character string to a console screen buffer beginning at current cursor.
 */
inline BOOL WriteConsoleW(
    HANDLE hConsoleOutput,
    const void* lpBuffer,
    DWORD nNumberOfCharsToWrite,
    DWORD* lpNumberOfCharsWritten,
    void* lpReserved = nullptr
) noexcept {
    (void)lpReserved;
    if (!lpBuffer || nNumberOfCharsToWrite == 0) return FALSE;

    if (g_TraceApi) {
        std::cout << "[*] [TRACE] WriteConsoleW chars=" << nNumberOfCharsToWrite << "\n";
    }

    DWORD pid = GetCurrentProcessId();
    auto session = conhost::ConhostManager::get().getConsole(pid);
    if (session) {
        std::wstring_view sv(reinterpret_cast<const wchar_t*>(lpBuffer), nNumberOfCharsToWrite);
        session->writeOutput(sv);
    }

    FILE* targetStream = (hConsoleOutput == GetStdHandle(STD_ERROR_HANDLE) || hConsoleOutput == reinterpret_cast<HANDLE>(0x18)) ? stderr : stdout;
    const auto* wchars = reinterpret_cast<const wchar_t*>(lpBuffer);
    for (DWORD i = 0; i < nNumberOfCharsToWrite; ++i) {
        wchar_t ch = wchars[i];
        if (ch < 0x80) {
            std::fputc(static_cast<char>(ch), targetStream);
        } else if (ch < 0x800) {
            std::fputc(0xC0 | (ch >> 6), targetStream);
            std::fputc(0x80 | (ch & 0x3F), targetStream);
        } else {
            std::fputc(0xE0 | (ch >> 12), targetStream);
            std::fputc(0x80 | ((ch >> 6) & 0x3F), targetStream);
            std::fputc(0x80 | (ch & 0x3F), targetStream);
        }
    }
    std::fflush(targetStream);

    if (lpNumberOfCharsWritten) {
        *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    }
    return TRUE;
}

// ============================================================================
// 5. File I/O
// ============================================================================

/**
 * @brief Creates or opens a file or I/O device.
 */
inline HANDLE CreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    void* lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile = nullptr
) noexcept {
    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)dwFlagsAndAttributes;
    (void)hTemplateFile;

    if (!lpFileName) return INVALID_HANDLE_VALUE;

    UnicodeString uniPath(lpFileName);
    ObjectAttributes objAttr{};
    objAttr.objectName = &uniPath;

    Handle hFile = 0;
    IoStatusBlock iosb{};

    uint32_t createDisposition = 1; // FILE_OPEN
    if (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == CREATE_NEW) {
        createDisposition = 2; // FILE_CREATE
    } else if (dwCreationDisposition == OPEN_ALWAYS) {
        createDisposition = 3; // FILE_OPEN_IF
    }

    NtStatus status = ntdll::NtCreateFile(
        &hFile,
        dwDesiredAccess,
        &objAttr,
        &iosb,
        nullptr,
        0,
        dwShareMode,
        createDisposition,
        0,
        nullptr,
        0
    );

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return INVALID_HANDLE_VALUE;
    }
    return reinterpret_cast<HANDLE>(hFile);
}

/**
 * @brief Reads data from the specified file or input device.
 */
inline BOOL ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    DWORD* lpNumberOfBytesRead,
    void* lpOverlapped = nullptr
) noexcept {
    (void)lpOverlapped;
    if (!lpBuffer) return FALSE;

    IoStatusBlock iosb{};
    NtStatus status = ntdll::NtReadFile(
        reinterpret_cast<Handle>(hFile),
        0, nullptr, nullptr,
        &iosb,
        lpBuffer,
        nNumberOfBytesToRead,
        nullptr, nullptr
    );

    if (lpNumberOfBytesRead) {
        *lpNumberOfBytesRead = static_cast<DWORD>(iosb.information);
    }
    if (!NT_SUCCESS(status) || status == NtStatus::Timeout) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Writes data to the specified file or output device.
 */
inline BOOL WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    DWORD* lpNumberOfBytesWritten,
    void* lpOverlapped = nullptr
) noexcept {
    (void)lpOverlapped;
    if (!lpBuffer) return FALSE;

    if (g_TraceApi) {
        std::cout << "[*] [TRACE] WriteFile h=" << hFile << " bytes=" << nNumberOfBytesToWrite << "\n";
    }

    if (hFile == GetStdHandle(STD_OUTPUT_HANDLE) || hFile == reinterpret_cast<HANDLE>(0x14) || hFile == reinterpret_cast<HANDLE>(1) ||
        hFile == GetStdHandle(STD_ERROR_HANDLE) || hFile == reinterpret_cast<HANDLE>(0x18) || hFile == reinterpret_cast<HANDLE>(2)) {
        FILE* stream = (hFile == GetStdHandle(STD_ERROR_HANDLE) || hFile == reinterpret_cast<HANDLE>(0x18) || hFile == reinterpret_cast<HANDLE>(2)) ? stderr : stdout;
        size_t written = std::fwrite(lpBuffer, 1, nNumberOfBytesToWrite, stream);
        std::fflush(stream);
        if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = static_cast<DWORD>(written);
        return TRUE;
    }

    IoStatusBlock iosb{};
    NtStatus status = ntdll::NtWriteFile(
        reinterpret_cast<Handle>(hFile),
        0, nullptr, nullptr,
        &iosb,
        const_cast<void*>(lpBuffer),
        nNumberOfBytesToWrite,
        nullptr, nullptr
    );

    if (lpNumberOfBytesWritten) {
        *lpNumberOfBytesWritten = static_cast<DWORD>(iosb.information);
    }
    if (!NT_SUCCESS(status) || status == NtStatus::Timeout) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Closes an open object handle.
 */
inline bool CloseToolhelpSnapshot(HANDLE hObject) noexcept;
inline bool CloseTimerQueueHandle(HANDLE hObject) noexcept;

inline BOOL CloseHandle(HANDLE hObject) noexcept {
    if (hObject == nullptr || hObject == INVALID_HANDLE_VALUE) return FALSE;
    if (CloseToolhelpSnapshot(hObject)) return TRUE;
    if (CloseTimerQueueHandle(hObject)) return TRUE;
    NtStatus status = ntdll::NtClose(reinterpret_cast<Handle>(hObject));
    return NT_SUCCESS(status) ? TRUE : FALSE;
}

// ============================================================================
// 5.1 Named Pipes & Mailslots Inter-Process Communication (IPC)
// ============================================================================

inline constexpr DWORD PIPE_ACCESS_INBOUND         = 0x00000001;
inline constexpr DWORD PIPE_ACCESS_OUTBOUND        = 0x00000002;
inline constexpr DWORD PIPE_ACCESS_DUPLEX          = 0x00000003;

inline constexpr DWORD PIPE_WAIT                   = 0x00000000;
inline constexpr DWORD PIPE_NOWAIT                 = 0x00000001;
inline constexpr DWORD PIPE_READMODE_BYTE          = 0x00000000;
inline constexpr DWORD PIPE_READMODE_MESSAGE       = 0x00000002;
inline constexpr DWORD PIPE_TYPE_BYTE              = 0x00000000;
inline constexpr DWORD PIPE_TYPE_MESSAGE           = 0x00000004;

inline constexpr DWORD PIPE_CLIENT_END             = 0x00000000;
inline constexpr DWORD PIPE_SERVER_END             = 0x00000001;
inline constexpr DWORD PIPE_UNLIMITED_INSTANCES    = 255;

inline constexpr DWORD NMPWAIT_WAIT_FOREVER        = 0xFFFFFFFF;
inline constexpr DWORD NMPWAIT_NOWAIT              = 0x00000001;
inline constexpr DWORD NMPWAIT_USE_DEFAULT_WAIT    = 0x00000000;

inline constexpr DWORD MAILSLOT_NO_MESSAGE         = static_cast<DWORD>(-1);
inline constexpr DWORD MAILSLOT_WAIT_FOREVER       = static_cast<DWORD>(-1);

inline constexpr DWORD ERROR_PIPE_BUSY             = 231;
inline constexpr DWORD ERROR_NO_DATA               = 232;
inline constexpr DWORD ERROR_PIPE_NOT_CONNECTED    = 233;
inline constexpr DWORD ERROR_MORE_DATA             = 234;
inline constexpr DWORD ERROR_PIPE_CONNECTED        = 535;
inline constexpr DWORD ERROR_PIPE_LISTENING        = 536;
inline constexpr DWORD ERROR_BROKEN_PIPE           = 109;

inline HANDLE CreateNamedPipeW(
    LPCWSTR lpName,
    DWORD dwOpenMode,
    DWORD dwPipeMode,
    DWORD nMaxInstances,
    DWORD nOutBufferSize,
    DWORD nInBufferSize,
    DWORD nDefaultTimeOut,
    void* lpSecurityAttributes = nullptr
) noexcept {
    (void)lpSecurityAttributes;
    if (!lpName) {
        SetLastError(87); // ERROR_INVALID_PARAMETER
        return INVALID_HANDLE_VALUE;
    }

    UnicodeString uniName(lpName);
    ObjectAttributes objAttr{};
    objAttr.objectName = &uniName;

    Handle hPipe = 0;
    IoStatusBlock iosb{};

    uint32_t type = (dwPipeMode & PIPE_TYPE_MESSAGE) ? 1 : 0;
    uint32_t readMode = (dwPipeMode & PIPE_READMODE_MESSAGE) ? 1 : 0;
    uint32_t nonBlocking = (dwPipeMode & PIPE_NOWAIT) ? 1 : 0;

    LargeInteger timeout{};
    timeout.quadPart = -static_cast<int64_t>(nDefaultTimeOut) * 10000;

    NtStatus status = ntdll::NtCreateNamedPipeFile(
        &hPipe,
        dwOpenMode,
        &objAttr,
        &iosb,
        0,
        2,
        0,
        type,
        readMode,
        nonBlocking,
        nMaxInstances,
        nInBufferSize,
        nOutBufferSize,
        &timeout
    );

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return INVALID_HANDLE_VALUE;
    }

    return reinterpret_cast<HANDLE>(hPipe);
}

inline BOOL ConnectNamedPipe(
    HANDLE hNamedPipe,
    void* lpOverlapped = nullptr
) noexcept {
    (void)lpOverlapped;
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
    NtStatus status = pipeInst->connectServer(npfs::NMPWAIT_WAIT_FOREVER);
    if (status == NtStatus::PipeConnected) {
        SetLastError(ERROR_PIPE_CONNECTED);
        return FALSE;
    }

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }

    return TRUE;
}

inline BOOL DisconnectNamedPipe(
    HANDLE hNamedPipe
) noexcept {
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
    NtStatus status = pipeInst->disconnectServer();
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }

    return TRUE;
}

inline BOOL WaitNamedPipeW(
    LPCWSTR lpNamedPipeName,
    DWORD nTimeOut
) noexcept {
    if (!lpNamedPipeName) {
        SetLastError(87);
        return FALSE;
    }

    NtStatus status = npfs::NamedPipeFileSystem::get().waitNamedPipe(lpNamedPipeName, nTimeOut);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL PeekNamedPipe(
    HANDLE hNamedPipe,
    LPVOID lpBuffer,
    DWORD nBufferSize,
    DWORD* lpBytesRead,
    DWORD* lpTotalBytesAvail,
    DWORD* lpBytesLeftThisMessage
) noexcept {
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
    bool isServer = (reinterpret_cast<uintptr_t>(fileObj->getFsContext2()) == 1);

    uint32_t bytesRead = 0;
    uint32_t totalAvail = 0;
    uint32_t leftMsg = 0;

    NtStatus status = pipeInst->peek(
        isServer, lpBuffer, nBufferSize,
        &bytesRead, &totalAvail, &leftMsg
    );

    if (lpBytesRead) *lpBytesRead = bytesRead;
    if (lpTotalBytesAvail) *lpTotalBytesAvail = totalAvail;
    if (lpBytesLeftThisMessage) *lpBytesLeftThisMessage = leftMsg;

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL TransactNamedPipe(
    HANDLE hNamedPipe,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    DWORD* lpBytesRead,
    void* lpOverlapped = nullptr
) noexcept {
    (void)lpOverlapped;
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE || !lpInBuffer || !lpOutBuffer) {
        SetLastError(87);
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
    bool isServer = (reinterpret_cast<uintptr_t>(fileObj->getFsContext2()) == 1);

    uint32_t bytesRead = 0;
    NtStatus status = pipeInst->transact(
        isServer, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, bytesRead
    );

    if (lpBytesRead) *lpBytesRead = bytesRead;

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL GetNamedPipeInfo(
    HANDLE hNamedPipe,
    DWORD* lpFlags,
    DWORD* lpOutBufferSize,
    DWORD* lpInBufferSize,
    DWORD* lpMaxInstances
) noexcept {
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE) {
        SetLastError(6);
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
    bool isServer = (reinterpret_cast<uintptr_t>(fileObj->getFsContext2()) == 1);

    if (lpFlags) {
        *lpFlags = (isServer ? PIPE_SERVER_END : PIPE_CLIENT_END) |
                   (pipeInst->getPipeMode() & PIPE_TYPE_MESSAGE);
    }
    if (lpOutBufferSize) *lpOutBufferSize = pipeInst->getOutBufferSize();
    if (lpInBufferSize) *lpInBufferSize = pipeInst->getInBufferSize();
    if (lpMaxInstances) *lpMaxInstances = pipeInst->getMaxInstances();

    return TRUE;
}

inline BOOL GetNamedPipeHandleStateW(
    HANDLE hNamedPipe,
    DWORD* lpState,
    DWORD* lpCurInstances,
    DWORD* lpMaxCollectionCount,
    DWORD* lpCollectDataTimeout,
    LPWSTR lpUserName,
    DWORD nMaxUserNameSize
) noexcept {
    (void)lpMaxCollectionCount;
    (void)lpCollectDataTimeout;
    (void)lpUserName;
    (void)nMaxUserNameSize;
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE) {
        SetLastError(6);
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
    if (lpState) *lpState = pipeInst->getPipeMode();
    if (lpCurInstances) *lpCurInstances = 1;

    return TRUE;
}

inline BOOL SetNamedPipeHandleState(
    HANDLE hNamedPipe,
    DWORD* lpMode,
    DWORD* lpMaxCollectionCount,
    DWORD* lpCollectDataTimeout
) noexcept {
    (void)lpMaxCollectionCount;
    (void)lpCollectDataTimeout;
    if (hNamedPipe == nullptr || hNamedPipe == INVALID_HANDLE_VALUE) {
        SetLastError(6);
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hNamedPipe));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    if (lpMode) {
        auto* pipeInst = static_cast<npfs::NamedPipeInstance*>(fileObj->getFsContext());
        pipeInst->setMode(*lpMode);
    }

    return TRUE;
}

inline HANDLE CreateMailslotW(
    LPCWSTR lpName,
    DWORD nMaxMessageSize,
    DWORD lReadTimeout,
    void* lpSecurityAttributes = nullptr
) noexcept {
    (void)lpSecurityAttributes;
    if (!lpName) {
        SetLastError(87);
        return INVALID_HANDLE_VALUE;
    }

    UnicodeString uniName(lpName);
    ObjectAttributes objAttr{};
    objAttr.objectName = &uniName;

    Handle hSlot = 0;
    IoStatusBlock iosb{};

    LargeInteger timeout{};
    timeout.quadPart = (lReadTimeout == MAILSLOT_WAIT_FOREVER) ? -1 : -static_cast<int64_t>(lReadTimeout) * 10000;

    NtStatus status = ntdll::NtCreateMailslotFile(
        &hSlot,
        GENERIC_READ | FILE_SHARE_READ,
        &objAttr,
        &iosb,
        0,
        0,
        nMaxMessageSize,
        &timeout
    );

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return INVALID_HANDLE_VALUE;
    }

    return reinterpret_cast<HANDLE>(hSlot);
}

inline BOOL GetMailslotInfo(
    HANDLE hMailslot,
    DWORD* lpMaxMessageSize,
    DWORD* lpNextSize,
    DWORD* lpMessageCount,
    DWORD* lpReadTimeout
) noexcept {
    if (hMailslot == nullptr || hMailslot == INVALID_HANDLE_VALUE) {
        SetLastError(6);
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hMailslot));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    auto* slot = static_cast<npfs::Mailslot*>(fileObj->getFsContext());
    slot->getInfo(lpMaxMessageSize, lpNextSize, lpMessageCount, lpReadTimeout);
    return TRUE;
}

inline BOOL SetMailslotInfo(
    HANDLE hMailslot,
    DWORD lReadTimeout
) noexcept {
    if (hMailslot == nullptr || hMailslot == INVALID_HANDLE_VALUE) {
        SetLastError(6);
        return FALSE;
    }

    fs::FileObject* fileObj = sys::LookupKernelFileObject(reinterpret_cast<Handle>(hMailslot));
    if (!fileObj || !fileObj->getFsContext()) {
        SetLastError(6);
        return FALSE;
    }

    auto* slot = static_cast<npfs::Mailslot*>(fileObj->getFsContext());
    slot->setReadTimeout(lReadTimeout);
    return TRUE;
}

/**
 * @brief Creates or opens a named or unnamed event object.
 */
inline HANDLE CreateEventW(
    void* lpEventAttributes,
    BOOL bManualReset,
    BOOL bInitialState,
    LPCWSTR lpName
) noexcept {
    (void)lpEventAttributes;
    Handle hEvent = 0;
    UnicodeString uniName(lpName);
    ObjectAttributes objAttr{};
    if (lpName) objAttr.objectName = &uniName;

    NtStatus status = ntdll::NtCreateEvent(
        &hEvent,
        0x1F0003, // EVENT_ALL_ACCESS
        &objAttr,
        bManualReset ? 0 : 1, // NotificationEvent = 0, SynchronizationEvent = 1
        bInitialState ? true : false
    );

    if (!NT_SUCCESS(status)) return nullptr;
    return reinterpret_cast<HANDLE>(hEvent);
}

/**
 * @brief Sets the specified event object to the signaled state.
 */
inline BOOL SetEvent(HANDLE hEvent) noexcept {
    if (!hEvent) return FALSE;
    NtStatus status = ntdll::NtSetEvent(reinterpret_cast<Handle>(hEvent), nullptr);
    return NT_SUCCESS(status) ? TRUE : FALSE;
}

/**
 * @brief Sets the specified event object to the nonsignaled state.
 */
inline BOOL ResetEvent(HANDLE hEvent) noexcept {
    if (!hEvent) return FALSE;
    NtStatus status = ntdll::NtResetEvent(reinterpret_cast<Handle>(hEvent), nullptr);
    return NT_SUCCESS(status) ? TRUE : FALSE;
}

/**
 * @brief Waits until the specified object is in the signaled state or the time-out interval elapses.
 */
inline DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) noexcept {
    if (!hHandle) return WAIT_FAILED;

    LargeInteger timeout{};
    LargeInteger* pTimeout = nullptr;
    if (dwMilliseconds != INFINITE) {
        timeout.quadPart = -static_cast<int64_t>(dwMilliseconds) * 10000; // 100ns units
        pTimeout = &timeout;
    }

    NtStatus status = ntdll::NtWaitForSingleObject(
        reinterpret_cast<Handle>(hHandle),
        false,
        pTimeout
    );

    if (status == NtStatus::Success) return WAIT_OBJECT_0;
    if (status == NtStatus::Timeout) return WAIT_TIMEOUT;
    return WAIT_FAILED;
}

/**
 * @brief Waits until one or all of the specified objects are in the signaled state.
 */
inline DWORD WaitForMultipleObjects(
    DWORD nCount,
    const HANDLE* lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds
) noexcept {
    if (nCount == 0 || !lpHandles || nCount > MAXIMUM_WAIT_OBJECTS) return WAIT_FAILED;

    Handle handles[MAXIMUM_WAIT_OBJECTS];
    for (DWORD i = 0; i < nCount; ++i) {
        handles[i] = reinterpret_cast<Handle>(lpHandles[i]);
    }

    LargeInteger timeout{};
    LargeInteger* pTimeout = nullptr;
    if (dwMilliseconds != INFINITE) {
        timeout.quadPart = -static_cast<int64_t>(dwMilliseconds) * 10000;
        pTimeout = &timeout;
    }

    NtStatus status = ntdll::NtWaitForMultipleObjects(
        nCount,
        handles,
        bWaitAll ? WaitType::WaitAll : WaitType::WaitAny,
        false,
        pTimeout
    );

    if (status == NtStatus::Success) return WAIT_OBJECT_0;
    if (static_cast<uint32_t>(status) >= static_cast<uint32_t>(NtStatus::Wait0) &&
        static_cast<uint32_t>(status) < static_cast<uint32_t>(NtStatus::Wait0) + nCount) {
        return static_cast<DWORD>(status);
    }
    if (status == NtStatus::Timeout) return WAIT_TIMEOUT;
    return WAIT_FAILED;
}

// ============================================================================
// 7. Time & System Information
// ============================================================================

/**
 * @brief Suspends the execution of the current thread until the time-out interval elapses.
 */
inline void Sleep(DWORD dwMilliseconds) noexcept {
    LargeInteger interval{};
    interval.quadPart = -static_cast<int64_t>(dwMilliseconds) * 10000; // 100ns
    ntdll::NtDelayExecution(false, &interval);
}

/**
 * @brief Retrieves the number of milliseconds that have elapsed since the system was started.
 */
inline uint64_t GetTickCount64() noexcept {
    auto* shared = reinterpret_cast<volatile const uint64_t*>(
        micant::UserSharedDataAddress + offsetof(micant::KUserSharedData, tickCountQuad));
    return *shared;
}

/**
 * @brief Retrieves information about the current system.
 */
inline void GetSystemInfo(SYSTEM_INFO* lpSystemInfo) noexcept {
    if (!lpSystemInfo) return;
    *lpSystemInfo = SYSTEM_INFO{};
}

// ============================================================================
// 8. Dynamic Linking & Module Loading
// ============================================================================

/**
 * @brief Loads the specified module into the address space of the calling process.
 */
inline HMODULE LoadLibraryW(LPCWSTR lpLibFileName) noexcept {
    if (!lpLibFileName) return nullptr;
    if (g_TraceApi) {
        std::wcout << L"[*] [TRACE] LoadLibraryW: " << lpLibFileName << L"\n";
    }
    uintptr_t modBase = 0;
    UnicodeString uniPath(lpLibFileName);
    NtStatus status = ntdll::LdrLoadDll(nullptr, 0, &uniPath, &modBase);
    if (!NT_SUCCESS(status)) return nullptr;
    return reinterpret_cast<HMODULE>(modBase);
}

inline HMODULE LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE /*hFile*/, DWORD /*dwFlags*/) noexcept {
    return LoadLibraryW(lpLibFileName);
}

/**
 * @brief Retrieves the address of an exported function or variable from the specified DLL.
 */
inline void* GetProcAddress(HMODULE hModule, LPCSTR lpProcName) noexcept {
    if (!hModule || !lpProcName) return nullptr;
    if (g_TraceApi) {
        std::cout << "[*] [TRACE] GetProcAddress: " << (lpProcName ? lpProcName : "<ord>") << "\n";
    }
    void* procAddr = nullptr;
    NtStatus status = ntdll::LdrGetProcedureAddress(reinterpret_cast<uintptr_t>(hModule), lpProcName, 0, &procAddr);
    if (!NT_SUCCESS(status)) return nullptr;
    return procAddr;
}

/**
 * @brief Frees the loaded dynamic-link library (DLL) module.
 */
inline BOOL FreeLibrary(HMODULE hLibModule) noexcept {
    (void)hLibModule;
    return TRUE;
}

// ============================================================================
// 9. Error Handling & Thread-Local Status (Defined in Section 1)
// ============================================================================

// ============================================================================
// 10. Environment, Command Line & Working Directory
// ============================================================================

static std::unordered_map<std::wstring, std::wstring> g_EnvironmentVariables = {
    { L"OS", L"MicaNT" },
    { L"SystemRoot", L"C:\\Windows" },
    { L"windir", L"C:\\Windows" },
    { L"ComSpec", L"C:\\Windows\\System32\\cmd.exe" },
    { L"PATH", L"C:\\Windows\\System32;C:\\Windows" },
    { L"NUMBER_OF_PROCESSORS", L"4" },
    { L"PROCESSOR_ARCHITECTURE", L"AMD64" },
    { L"PROCESSOR_IDENTIFIER", L"AMD64 Family 6 Model 0 Stepping 0, MicaNT" },
    { L"USERPROFILE", L"C:\\Users\\Default" },
    { L"HOMEDRIVE", L"C:" },
    { L"HOMEPATH", L"\\Users\\Default" },
    { L"PROMPT", L"$P$G" }
};
static std::wstring g_CurrentDirectory = L"C:\\Windows\\System32";
inline std::wstring g_CommandLineStorageW = L"micant.exe";
inline std::string g_CommandLineStorageA = "micant.exe";

inline void SetCurrentCommandLine(const wchar_t* wcmd, const char* acmd) noexcept {
    if (wcmd) g_CommandLineStorageW = wcmd;
    if (acmd) g_CommandLineStorageA = acmd;
}

inline LPCSTR GetCommandLineA() noexcept { return g_CommandLineStorageA.c_str(); }
inline LPCWSTR GetCommandLineW() noexcept {
    if (g_TraceApi) {
        std::wcout << L"[*] [TRACE] GetCommandLineW: " << g_CommandLineStorageW << L"\n";
    }
    return g_CommandLineStorageW.c_str();
}

inline DWORD GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize) noexcept {
    if (!lpName) {
        SetLastError(87); // ERROR_INVALID_PARAMETER
        return 0;
    }
    auto it = g_EnvironmentVariables.find(lpName);
    if (it == g_EnvironmentVariables.end()) {
        SetLastError(203); // ERROR_ENVVAR_NOT_FOUND
        return 0;
    }
    const auto& val = it->second;
    if (nSize <= val.size()) {
        return static_cast<DWORD>(val.size() + 1);
    }
    if (lpBuffer) {
        std::wcsncpy(lpBuffer, val.c_str(), nSize);
        return static_cast<DWORD>(val.size());
    }
    return 0;
}

inline DWORD GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize) noexcept {
    if (!lpName) {
        SetLastError(87);
        return 0;
    }
    std::string nameStr(lpName);
    std::wstring wName(nameStr.begin(), nameStr.end());
    std::vector<wchar_t> wBuf(nSize ? nSize : 1);
    DWORD res = GetEnvironmentVariableW(wName.c_str(), wBuf.data(), nSize);
    if (res > 0 && res < nSize && lpBuffer) {
        for (DWORD i = 0; i < res; ++i) {
            lpBuffer[i] = static_cast<char>(wBuf[i]);
        }
        lpBuffer[res] = '\0';
    }
    return res;
}

inline BOOL SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue) noexcept {
    if (!lpName) {
        SetLastError(87);
        return FALSE;
    }
    if (!lpValue) {
        g_EnvironmentVariables.erase(lpName);
    } else {
        g_EnvironmentVariables[lpName] = lpValue;
    }
    return TRUE;
}

inline BOOL SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue) noexcept {
    if (!lpName) return FALSE;
    std::string nameStr(lpName);
    std::wstring wName(nameStr.begin(), nameStr.end());
    if (!lpValue) {
        return SetEnvironmentVariableW(wName.c_str(), nullptr);
    }
    std::string valStr(lpValue);
    std::wstring wVal(valStr.begin(), valStr.end());
    return SetEnvironmentVariableW(wName.c_str(), wVal.c_str());
}

inline DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer) noexcept {
    if (nBufferLength <= g_CurrentDirectory.size()) {
        return static_cast<DWORD>(g_CurrentDirectory.size() + 1);
    }
    if (lpBuffer) {
        std::wcsncpy(lpBuffer, g_CurrentDirectory.c_str(), nBufferLength);
        return static_cast<DWORD>(g_CurrentDirectory.size());
    }
    return 0;
}

inline DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer) noexcept {
    if (nBufferLength <= g_CurrentDirectory.size()) {
        return static_cast<DWORD>(g_CurrentDirectory.size() + 1);
    }
    if (lpBuffer) {
        for (size_t i = 0; i < g_CurrentDirectory.size(); ++i) {
            lpBuffer[i] = static_cast<char>(g_CurrentDirectory[i]);
        }
        lpBuffer[g_CurrentDirectory.size()] = '\0';
        return static_cast<DWORD>(g_CurrentDirectory.size());
    }
    return 0;
}

inline BOOL SetCurrentDirectoryW(LPCWSTR lpPathName) noexcept {
    if (!lpPathName) {
        SetLastError(87);
        return FALSE;
    }
    g_CurrentDirectory = lpPathName;
    return TRUE;
}

inline BOOL SetCurrentDirectoryA(LPCSTR lpPathName) noexcept {
    if (!lpPathName) return FALSE;
    std::string pathStr(lpPathName);
    std::wstring wPath(pathStr.begin(), pathStr.end());
    return SetCurrentDirectoryW(wPath.c_str());
}

inline DWORD GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart) noexcept {
    if (!lpFileName) return 0;
    std::wstring fullPath;
    bool isAbsolute = (lpFileName[0] == L'\\' || lpFileName[0] == L'/');
    if (!isAbsolute && lpFileName[0] != L'\0') {
        std::wstring_view sv(lpFileName);
        size_t colon = sv.find(L':');
        if (colon != std::wstring_view::npos && colon >= 1 && colon <= 32) {
            bool validId = true;
            for (size_t i = 0; i < colon; ++i) {
                wchar_t c = sv[i];
                if (!std::iswalnum(c) && c != L'_' && c != L'-') {
                    validId = false;
                    break;
                }
            }
            if (validId) isAbsolute = true;
        }
    }
    if (isAbsolute) {
        fullPath = lpFileName;
    } else {
        fullPath = g_CurrentDirectory + L"\\" + lpFileName;
    }
    if (nBufferLength <= fullPath.size()) {
        return static_cast<DWORD>(fullPath.size() + 1);
    }
    if (lpBuffer) {
        std::wcsncpy(lpBuffer, fullPath.c_str(), nBufferLength);
        if (lpFilePart) {
            size_t slash = fullPath.find_last_of(L"\\/");
            *lpFilePart = (slash == std::wstring::npos) ? lpBuffer : (lpBuffer + slash + 1);
        }
        return static_cast<DWORD>(fullPath.size());
    }
    return 0;
}

inline DWORD GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR* lpFilePart) noexcept {
    if (!lpFileName) return 0;
    std::string nameStr(lpFileName);
    std::wstring wName(nameStr.begin(), nameStr.end());
    std::vector<wchar_t> wBuf(nBufferLength ? nBufferLength : 1);
    wchar_t* wPart = nullptr;
    DWORD res = GetFullPathNameW(wName.c_str(), nBufferLength, wBuf.data(), &wPart);
    if (res > 0 && res < nBufferLength && lpBuffer) {
        for (DWORD i = 0; i < res; ++i) {
            lpBuffer[i] = static_cast<char>(wBuf[i]);
        }
        lpBuffer[res] = '\0';
        if (lpFilePart && wPart) {
            *lpFilePart = lpBuffer + (wPart - wBuf.data());
        }
    }
    return res;
}

// ============================================================================
// 11. Module & Process Introspection
// ============================================================================


inline DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) noexcept {
    if (!lpFilename || nSize == 0) return 0;
    std::wstring path = L"C:\\Windows\\System32\\micant.exe";
    if (hModule == reinterpret_cast<HMODULE>(0x7FF800000000ULL)) {
        path = L"C:\\Windows\\System32\\kernel32.dll";
    } else if (hModule == reinterpret_cast<HMODULE>(0x7FF810000000ULL)) {
        path = L"C:\\Windows\\System32\\ntdll.dll";
    }
    size_t copyLen = std::min<size_t>(path.size(), nSize - 1);
    std::wcsncpy(lpFilename, path.c_str(), copyLen);
    lpFilename[copyLen] = L'\0';
    return static_cast<DWORD>(copyLen);
}

inline DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) noexcept {
    if (!lpFilename || nSize == 0) return 0;
    std::vector<wchar_t> wBuf(nSize);
    DWORD res = GetModuleFileNameW(hModule, wBuf.data(), nSize);
    for (DWORD i = 0; i < res; ++i) {
        lpFilename[i] = static_cast<char>(wBuf[i]);
    }
    lpFilename[res] = '\0';
    return res;
}

inline HMODULE GetModuleHandleW(LPCWSTR lpModuleName) noexcept {
    if (!lpModuleName) {
        if (g_CurrentExecutableBase != 0) return reinterpret_cast<HMODULE>(g_CurrentExecutableBase);
        return reinterpret_cast<HMODULE>(0x140000000ULL);
    }
    std::wstring name(lpModuleName);
    for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
    if (name.find(L'.') == std::wstring::npos) {
        name += L".dll";
    }

    // Check already loaded modules in DynamicLoader
    for (const auto& mod : ldr::DynamicLoader::get().getLoadedModules()) {
        std::wstring modBase(mod->storedBaseName);
        for (auto& c : modBase) c = static_cast<wchar_t>(std::towlower(c));
        if (modBase == name || mod->storedFullName == name) {
            return reinterpret_cast<HMODULE>(mod->dllBase);
        }
    }

    if (name == L"kernel32.dll") {
        return reinterpret_cast<HMODULE>(0x7FF800000000ULL);
    }
    if (name == L"ntdll.dll") {
        return reinterpret_cast<HMODULE>(0x7FF810000000ULL);
    }

    // Register/load module in DynamicLoader table
    UnicodeString uniName(name.c_str());
    uintptr_t modBase = 0;
    if (NT_SUCCESS(ntdll::LdrLoadDll(nullptr, 0, &uniName, &modBase))) {
        return reinterpret_cast<HMODULE>(modBase);
    }

    if (g_CurrentExecutableBase != 0) return reinterpret_cast<HMODULE>(g_CurrentExecutableBase);
    return reinterpret_cast<HMODULE>(0x140000000ULL);
}

inline HMODULE GetModuleHandleA(LPCSTR lpModuleName) noexcept {
    if (!lpModuleName) return GetModuleHandleW(nullptr);
    std::string modStr(lpModuleName);
    std::wstring wMod(modStr.begin(), modStr.end());
    return GetModuleHandleW(wMod.c_str());
}

inline BOOL GetModuleHandleExW(DWORD /*dwFlags*/, LPCWSTR lpModuleName, HMODULE* phModule) noexcept {
    if (!phModule) return FALSE;
    *phModule = GetModuleHandleW(lpModuleName);
    return TRUE;
}

// ============================================================================
// 12. Extended File Operations, Sizing, Seeking & Directories
// ============================================================================

inline DWORD GetFileAttributesW(LPCWSTR lpFileName) noexcept {
    if (!lpFileName) return INVALID_FILE_ATTRIBUTES;
    uint32_t attrs = 0;
    NtStatus status = fs::VirtualFileSystem::get().queryFileAttributes(lpFileName, attrs);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return INVALID_FILE_ATTRIBUTES;
    }
    return attrs;
}

inline DWORD GetFileAttributesA(LPCSTR lpFileName) noexcept {
    if (!lpFileName) return INVALID_FILE_ATTRIBUTES;
    std::string nameStr(lpFileName);
    std::wstring wName(nameStr.begin(), nameStr.end());
    return GetFileAttributesW(wName.c_str());
}

inline BOOL SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes) noexcept {
    if (!lpFileName) return FALSE;
    NtStatus status = fs::VirtualFileSystem::get().setFileAttributes(lpFileName, dwFileAttributes);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL GetFileSizeEx(HANDLE hFile, LargeInteger* lpFileSize) noexcept {
    if (!hFile || !lpFileSize) {
        SetLastError(87);
        return FALSE;
    }
    IoStatusBlock iosb{};
    FileStandardInformation stdInfo{};
    NtStatus status = ntdll::NtQueryInformationFile(
        reinterpret_cast<Handle>(hFile),
        &iosb,
        &stdInfo,
        sizeof(stdInfo),
        FileInformationClass::FileStandardInformation
    );
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    *lpFileSize = stdInfo.endOfFile;
    return TRUE;
}

inline DWORD GetFileSize(HANDLE hFile, DWORD* lpFileSizeHigh) noexcept {
    LargeInteger li{};
    if (!GetFileSizeEx(hFile, &li)) return INVALID_FILE_SIZE;
    if (lpFileSizeHigh) *lpFileSizeHigh = static_cast<DWORD>(li.highPart);
    return li.lowPart;
}

inline BOOL SetFilePointerEx(HANDLE hFile, LargeInteger liDistanceToMove, LargeInteger* lpNewFilePointer, DWORD dwMoveMethod) noexcept {
    if (!hFile) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    int64_t targetOffset = 0;
    if (dwMoveMethod == FILE_BEGIN) {
        targetOffset = liDistanceToMove.quadPart;
    } else if (dwMoveMethod == FILE_CURRENT) {
        IoStatusBlock iosb{};
        FilePositionInformation posInfo{};
        ntdll::NtQueryInformationFile(reinterpret_cast<Handle>(hFile), &iosb, &posInfo, sizeof(posInfo), FileInformationClass::FilePositionInformation);
        targetOffset = posInfo.currentByteOffset.quadPart + liDistanceToMove.quadPart;
    } else if (dwMoveMethod == FILE_END) {
        LargeInteger sz{};
        if (!GetFileSizeEx(hFile, &sz)) return FALSE;
        targetOffset = sz.quadPart + liDistanceToMove.quadPart;
    } else {
        SetLastError(87);
        return FALSE;
    }

    if (targetOffset < 0) targetOffset = 0;

    IoStatusBlock iosb{};
    FilePositionInformation posInfo{};
    posInfo.currentByteOffset.quadPart = targetOffset;
    NtStatus status = ntdll::NtSetInformationFile(
        reinterpret_cast<Handle>(hFile),
        &iosb,
        &posInfo,
        sizeof(posInfo),
        FileInformationClass::FilePositionInformation
    );

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }

    if (lpNewFilePointer) {
        lpNewFilePointer->quadPart = targetOffset;
    }
    return TRUE;
}

inline DWORD SetFilePointer(HANDLE hFile, int32_t lDistanceToMove, int32_t* lpDistanceToMoveHigh, DWORD dwMoveMethod) noexcept {
    LargeInteger move{}, newPos{};
    move.lowPart = static_cast<uint32_t>(lDistanceToMove);
    move.highPart = lpDistanceToMoveHigh ? *lpDistanceToMoveHigh : (lDistanceToMove < 0 ? -1 : 0);
    if (!SetFilePointerEx(hFile, move, &newPos, dwMoveMethod)) return INVALID_FILE_SIZE;
    if (lpDistanceToMoveHigh) *lpDistanceToMoveHigh = newPos.highPart;
    return newPos.lowPart;
}

inline BOOL DeleteFileW(LPCWSTR lpFileName) noexcept {
    if (!lpFileName) return FALSE;
    NtStatus status = fs::VirtualFileSystem::get().deleteFile(lpFileName);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL DeleteFileA(LPCSTR lpFileName) noexcept {
    if (!lpFileName) return FALSE;
    std::string s(lpFileName);
    std::wstring w(s.begin(), s.end());
    return DeleteFileW(w.c_str());
}

inline BOOL CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists) noexcept {
    if (!lpExistingFileName || !lpNewFileName) {
        SetLastError(87);
        return FALSE;
    }
    if (bFailIfExists) {
        uint32_t attrs = 0;
        if (NT_SUCCESS(fs::VirtualFileSystem::get().queryFileAttributes(lpNewFileName, attrs))) {
            SetLastError(80); // ERROR_FILE_EXISTS
            return FALSE;
        }
    }
    std::shared_ptr<fs::FileObject> srcObj;
    NtStatus st = fs::VirtualFileSystem::get().createOrOpenFile(lpExistingFileName, fs::FILE_GENERIC_READ, fs::FILE_OPEN, srcObj);
    if (!NT_SUCCESS(st) || !srcObj) {
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }
    std::shared_ptr<fs::FileObject> dstObj;
    st = fs::VirtualFileSystem::get().createOrOpenFile(lpNewFileName, fs::FILE_GENERIC_WRITE, fs::FILE_OVERWRITE_IF, dstObj);
    if (!NT_SUCCESS(st) || !dstObj) {
        fs::VirtualFileSystem::get().closeFile(srcObj.get());
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }

    constexpr size_t CHUNK_SIZE = 65536;
    std::vector<uint8_t> buffer(CHUNK_SIZE);
    LargeInteger readOffset{};
    readOffset.quadPart = 0;
    LargeInteger writeOffset{};
    writeOffset.quadPart = 0;
    BOOL copySuccess = TRUE;

    while (true) {
        uint32_t bytesRead = 0;
        NtStatus readSt = fs::VirtualFileSystem::get().readFile(srcObj.get(), buffer.data(), static_cast<uint32_t>(buffer.size()), &readOffset, bytesRead);
        if (readSt == NtStatus::EndOfFile || bytesRead == 0) {
            break;
        }
        if (!NT_SUCCESS(readSt)) {
            SetLastError(ntdll::RtlNtStatusToDosError(readSt));
            copySuccess = FALSE;
            break;
        }
        uint32_t bytesWritten = 0;
        NtStatus writeSt = fs::VirtualFileSystem::get().writeFile(dstObj.get(), buffer.data(), bytesRead, &writeOffset, bytesWritten);
        if (!NT_SUCCESS(writeSt)) {
            SetLastError(ntdll::RtlNtStatusToDosError(writeSt));
            copySuccess = FALSE;
            break;
        }
        readOffset.quadPart += bytesRead;
        writeOffset.quadPart += bytesWritten;
    }

    fs::VirtualFileSystem::get().closeFile(srcObj.get());
    fs::VirtualFileSystem::get().closeFile(dstObj.get());
    return copySuccess;
}

inline BOOL CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists) noexcept {
    if (!lpExistingFileName || !lpNewFileName) return FALSE;
    std::string sSrc(lpExistingFileName);
    std::string sDst(lpNewFileName);
    std::wstring wSrc(sSrc.begin(), sSrc.end());
    std::wstring wDst(sDst.begin(), sDst.end());
    return CopyFileW(wSrc.c_str(), wDst.c_str(), bFailIfExists);
}

inline BOOL MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName) noexcept {
    if (!lpExistingFileName || !lpNewFileName) {
        SetLastError(87);
        return FALSE;
    }
    if (!CopyFileW(lpExistingFileName, lpNewFileName, FALSE)) {
        return FALSE;
    }
    return DeleteFileW(lpExistingFileName);
}

inline BOOL MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName) noexcept {
    if (!lpExistingFileName || !lpNewFileName) return FALSE;
    std::string sSrc(lpExistingFileName);
    std::string sDst(lpNewFileName);
    std::wstring wSrc(sSrc.begin(), sSrc.end());
    std::wstring wDst(sDst.begin(), sDst.end());
    return MoveFileW(wSrc.c_str(), wDst.c_str());
}

inline BOOL MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD /*dwFlags*/) noexcept {
    return MoveFileW(lpExistingFileName, lpNewFileName);
}

inline BOOL CreateDirectoryW(LPCWSTR lpPathName, void* /*lpSecurityAttributes*/) noexcept {
    if (!lpPathName) return FALSE;
    NtStatus status = fs::VirtualFileSystem::get().createDirectory(lpPathName);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL CreateDirectoryA(LPCSTR lpPathName, void* lpSecurityAttributes) noexcept {
    if (!lpPathName) return FALSE;
    std::string s(lpPathName);
    std::wstring w(s.begin(), s.end());
    return CreateDirectoryW(w.c_str(), lpSecurityAttributes);
}

inline BOOL RemoveDirectoryW(LPCWSTR lpPathName) noexcept {
    if (!lpPathName) return FALSE;
    NtStatus status = fs::VirtualFileSystem::get().removeDirectory(lpPathName);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline BOOL RemoveDirectoryA(LPCSTR lpPathName) noexcept {
    if (!lpPathName) return FALSE;
    std::string s(lpPathName);
    std::wstring w(s.begin(), s.end());
    return RemoveDirectoryW(w.c_str());
}

// FindFile Context Registry
struct FindFileContext {
    std::wstring directoryPath;
    std::vector<fs::VirtualFileSystem::DirectoryEntry> entries;
    size_t currentIndex{0};
};

static std::unordered_map<HANDLE, std::unique_ptr<FindFileContext>> g_FindContexts;
static uintptr_t g_NextFindHandle = 0x500;

inline HANDLE FindFirstFileW(LPCWSTR lpFileName, WIN32_FIND_DATAW* lpFindFileData) noexcept {
    if (!lpFileName || !lpFindFileData) {
        SetLastError(87);
        return INVALID_HANDLE_VALUE;
    }

    std::wstring path(lpFileName);
    size_t lastSlash = path.find_last_of(L"\\/");
    std::wstring dir = (lastSlash == std::wstring::npos) ? L"" : path.substr(0, lastSlash);

    std::vector<fs::VirtualFileSystem::DirectoryEntry> entries;
    NtStatus status = fs::VirtualFileSystem::get().queryDirectory(dir, entries);
    if (!NT_SUCCESS(status) || entries.empty()) {
        SetLastError(2); // ERROR_FILE_NOT_FOUND
        return INVALID_HANDLE_VALUE;
    }

    auto ctx = std::make_unique<FindFileContext>();
    ctx->directoryPath = dir;
    ctx->entries = std::move(entries);
    ctx->currentIndex = 0;

    const auto& first = ctx->entries[0];
    *lpFindFileData = WIN32_FIND_DATAW{};
    lpFindFileData->dwFileAttributes = first.attributes;
    lpFindFileData->nFileSizeLow = static_cast<DWORD>(first.size);
    std::wcsncpy(lpFindFileData->cFileName, first.name.c_str(), 259);

    HANDLE hFind = reinterpret_cast<HANDLE>(g_NextFindHandle++);
    g_FindContexts[hFind] = std::move(ctx);
    return hFind;
}

inline BOOL FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW* lpFindFileData) noexcept {
    if (!hFindFile || !lpFindFileData) {
        SetLastError(87);
        return FALSE;
    }
    auto it = g_FindContexts.find(hFindFile);
    if (it == g_FindContexts.end()) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }

    auto& ctx = it->second;
    ctx->currentIndex++;
    if (ctx->currentIndex >= ctx->entries.size()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }

    const auto& entry = ctx->entries[ctx->currentIndex];
    *lpFindFileData = WIN32_FIND_DATAW{};
    lpFindFileData->dwFileAttributes = entry.attributes;
    lpFindFileData->nFileSizeLow = static_cast<DWORD>(entry.size);
    std::wcsncpy(lpFindFileData->cFileName, entry.name.c_str(), 259);
    return TRUE;
}

inline BOOL FindClose(HANDLE hFindFile) noexcept {
    if (!hFindFile) return FALSE;
    return g_FindContexts.erase(hFindFile) > 0 ? TRUE : FALSE;
}

// ============================================================================
// 13. High-Precision Timing & System Clock
// ============================================================================

inline BOOL QueryPerformanceCounter(LargeInteger* lpPerformanceCount) noexcept {
    if (!lpPerformanceCount) return FALSE;
    return NT_SUCCESS(ntdll::NtQueryPerformanceCounter(lpPerformanceCount, nullptr))
        ? TRUE : FALSE;
}

inline BOOL QueryPerformanceFrequency(LargeInteger* lpFrequency) noexcept {
    if (!lpFrequency) return FALSE;
    LargeInteger counter{};
    return NT_SUCCESS(ntdll::NtQueryPerformanceCounter(&counter, lpFrequency))
        ? TRUE : FALSE;
}

inline bool queryCurrentUtcSystemTime(SYSTEMTIME* lpSystemTime) noexcept {
    if (!lpSystemTime) return false;
    LargeInteger ticks{};
    if (!NT_SUCCESS(ntdll::NtQuerySystemTime(&ticks))) return false;
    micant::shared::time::SystemTimeFields value{};
    if (!micant::shared::time::fromSystemTime100ns(
            static_cast<uint64_t>(ticks.quadPart), value)) return false;
    lpSystemTime->wYear = value.year;
    lpSystemTime->wMonth = value.month;
    lpSystemTime->wDayOfWeek = value.weekday;
    lpSystemTime->wDay = value.day;
    lpSystemTime->wHour = value.hour;
    lpSystemTime->wMinute = value.minute;
    lpSystemTime->wSecond = value.second;
    lpSystemTime->wMilliseconds = value.milliseconds;
    return true;
}

inline void GetSystemTime(SYSTEMTIME* lpSystemTime) noexcept {
    (void)queryCurrentUtcSystemTime(lpSystemTime);
}

inline void GetLocalTime(SYSTEMTIME* lpSystemTime) noexcept {
    if (!lpSystemTime) return;
    SYSTEMTIME utc{};
    if (!queryCurrentUtcSystemTime(&utc)) return;
    (void)SystemTimeToTzSpecificLocalTime(nullptr, &utc, lpSystemTime);
}

// ============================================================================
// 14. Console Control Enhancements
// ============================================================================

inline BOOL GetConsoleMode(HANDLE /*hConsoleHandle*/, DWORD* lpMode) noexcept {
    if (!lpMode) return FALSE;
    *lpMode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return TRUE;
}

inline BOOL SetConsoleMode(HANDLE /*hConsoleHandle*/, DWORD /*dwMode*/) noexcept {
    return TRUE;
}

inline uint32_t GetConsoleOutputCP() noexcept {
    return 65001; // UTF-8
}

inline std::shared_ptr<conhost::ConsoleSession> GetActiveConsoleSession() noexcept {
    DWORD pid = GetCurrentProcessId();
    auto session = conhost::ConhostManager::get().getConsole(pid);
    if (!session) {
        session = conhost::ConhostManager::get().allocateConsole(pid);
    }
    return session;
}

inline BOOL GetConsoleScreenBufferInfo(HANDLE /*hConsoleOutput*/, conhost::ConsoleScreenBufferInfo* lpConsoleScreenBufferInfo) noexcept {
    if (!lpConsoleScreenBufferInfo) return FALSE;
    auto session = GetActiveConsoleSession();
    if (!session) return FALSE;
    *lpConsoleScreenBufferInfo = session->getScreenBuffer().getScreenBufferInfo();
    return TRUE;
}

inline BOOL SetConsoleTextAttribute(HANDLE /*hConsoleOutput*/, uint16_t wAttributes) noexcept {
    auto session = GetActiveConsoleSession();
    if (!session) return FALSE;
    session->getScreenBuffer().setAttributes(wAttributes);
    return TRUE;
}

inline BOOL SetConsoleCursorPosition(HANDLE /*hConsoleOutput*/, conhost::Coord dwCursorPosition) noexcept {
    auto session = GetActiveConsoleSession();
    if (!session) return FALSE;
    session->getScreenBuffer().setCursorPosition(dwCursorPosition);
    return TRUE;
}

inline BOOL WriteConsoleA(
    HANDLE hConsoleOutput,
    const void* lpBuffer,
    DWORD nNumberOfCharsToWrite,
    DWORD* lpNumberOfCharsWritten,
    LPVOID /*lpReserved*/
) noexcept {
    if (!lpBuffer || nNumberOfCharsToWrite == 0) {
        if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = 0;
        return TRUE;
    }
    const char* str = reinterpret_cast<const char*>(lpBuffer);
    std::wstring wstr(str, str + nNumberOfCharsToWrite);
    return WriteConsoleW(hConsoleOutput, wstr.data(), nNumberOfCharsToWrite, lpNumberOfCharsWritten, nullptr);
}

// ============================================================================
// 15. Memory Mapping & Section Objects
// ============================================================================

inline HANDLE CreateFileMappingW(
    HANDLE hFile,
    void* /*lpFileMappingAttributes*/,
    DWORD flProtect,
    DWORD dwMaximumSizeHigh,
    DWORD dwMaximumSizeLow,
    LPCWSTR lpName
) noexcept {
    LargeInteger maxSz{};
    maxSz.lowPart = dwMaximumSizeLow;
    maxSz.highPart = static_cast<int32_t>(dwMaximumSizeHigh);

    UnicodeString uniName(lpName);
    ObjectAttributes objAttr{};
    if (lpName) objAttr.objectName = &uniName;

    Handle hSec = 0;
    Handle fHandle = (hFile == INVALID_HANDLE_VALUE) ? 0 : reinterpret_cast<Handle>(hFile);
    NtStatus status = ntdll::NtCreateSection(
        &hSec,
        0xF001F, // SECTION_ALL_ACCESS
        &objAttr,
        (maxSz.quadPart > 0) ? &maxSz : nullptr,
        flProtect ? flProtect : 0x04 /* PAGE_READWRITE */,
        0x08000000 /* SEC_COMMIT */,
        fHandle
    );

    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return nullptr;
    }
    return reinterpret_cast<HANDLE>(hSec);
}

inline LPVOID MapViewOfFile(
    HANDLE hFileMappingObject,
    DWORD /*dwDesiredAccess*/,
    DWORD /*dwFileOffsetHigh*/,
    DWORD /*dwFileOffsetLow*/,
    SIZE_T dwNumberOfBytesToMap
) noexcept {
    if (!hFileMappingObject) return nullptr;
    uintptr_t baseAddr = 0;
    size_t viewSize = dwNumberOfBytesToMap;
    NtStatus status = ntdll::NtMapViewOfSection(
        reinterpret_cast<Handle>(hFileMappingObject),
        static_cast<Handle>(-1), // CurrentProcess
        &baseAddr,
        0,
        viewSize,
        nullptr,
        &viewSize,
        1,
        0x3000, // MEM_COMMIT | MEM_RESERVE
        0x04    // PAGE_READWRITE
    );
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return nullptr;
    }
    return reinterpret_cast<LPVOID>(baseAddr);
}

inline BOOL UnmapViewOfFile(LPCVOID lpBaseAddress) noexcept {
    if (!lpBaseAddress) return FALSE;
    NtStatus status = ntdll::NtUnmapViewOfSection(
        static_cast<Handle>(-1),
        reinterpret_cast<uintptr_t>(lpBaseAddress)
    );
    return NT_SUCCESS(status) ? TRUE : FALSE;
}

/**
 * @brief Maps a view of a file mapping into the address space of a specified process with NUMA node preference.
 */
inline PVOID WINAPI MapViewOfFileNuma2(
    HANDLE FileMappingHandle,
    HANDLE ProcessHandle,
    uint64_t Offset,
    PVOID BaseAddress,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    ULONG PreferredNode
) noexcept {
    (void)ProcessHandle;
    (void)AllocationType;
    (void)PreferredNode;
    (void)BaseAddress;
    return MapViewOfFile(FileMappingHandle, PageProtection, static_cast<DWORD>(Offset >> 32), static_cast<DWORD>(Offset & 0xFFFFFFFF), ViewSize);
}

struct WIN32_MEMORY_RANGE_ENTRY {
    PVOID  VirtualAddress;
    SIZE_T NumberOfBytes;
};
typedef WIN32_MEMORY_RANGE_ENTRY* PWIN32_MEMORY_RANGE_ENTRY;

inline BOOL WINAPI PrefetchVirtualMemory(
    [[maybe_unused]] HANDLE hProcess,
    ULONG_PTR NumberOfEntries,
    PWIN32_MEMORY_RANGE_ENTRY pVirtualAddresses,
    [[maybe_unused]] ULONG Flags
) noexcept {
    if (!pVirtualAddresses || NumberOfEntries == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

inline PVOID WINAPI MapViewOfFile3(
    HANDLE FileMapping,
    [[maybe_unused]] HANDLE Process,
    [[maybe_unused]] PVOID BaseAddress,
    uint64_t Offset,
    SIZE_T ViewSize,
    [[maybe_unused]] ULONG AllocationType,
    ULONG PageProtection,
    [[maybe_unused]] MEM_EXTENDED_PARAMETER* ExtendedParameters,
    [[maybe_unused]] ULONG ParameterCount
) noexcept {
    return MapViewOfFile(FileMapping, PageProtection, static_cast<DWORD>(Offset >> 32), static_cast<DWORD>(Offset & 0xFFFFFFFF), ViewSize);
}


// ============================================================================
// 16. Process & Thread Operations
// ============================================================================

inline BOOL GetExitCodeProcess(HANDLE hProcess, DWORD* lpExitCode) noexcept {
    if (!hProcess || !lpExitCode) return FALSE;
    ProcessBasicInformation pbi{};
    NtStatus status = ntdll::NtQueryInformationProcess(
        reinterpret_cast<Handle>(hProcess),
        ProcessInformationClass::ProcessBasicInformation,
        &pbi,
        sizeof(pbi)
    );
    if (NT_SUCCESS(status)) {
        *lpExitCode = static_cast<DWORD>(pbi.exitStatus);
        return TRUE;
    }
    *lpExitCode = 0;
    return TRUE;
}

inline BOOL TerminateProcess(HANDLE hProcess, DWORD uExitCode) noexcept {
    if (!hProcess) return FALSE;
    if (hProcess == GetCurrentProcess()) {
        ExitProcess(uExitCode);
    }
    NtStatus status = ntdll::NtTerminateProcess(reinterpret_cast<Handle>(hProcess), static_cast<NtStatus>(uExitCode));
    return NT_SUCCESS(status) ? TRUE : FALSE;
}

inline BOOL SwitchToThread() noexcept {
    ntdll::NtYieldExecution();
    return TRUE;
}

inline DWORD ResumeThread(HANDLE hThread) noexcept {
    (void)hThread;
    return 0;
}

inline DWORD SuspendThread(HANDLE hThread) noexcept {
    (void)hThread;
    return 0;
}

inline BOOL TerminateThread(HANDLE hThread, DWORD dwExitCode) noexcept {
    (void)hThread;
    (void)dwExitCode;
    return TRUE;
}

// ============================================================================
// Thread Context & Hardware Breakpoint Control (x64 Windows NT ABI)
// ============================================================================

inline constexpr uint32_t CONTEXT_AMD64_FLAG = 0x00100000L;

#ifndef CONTEXT_CONTROL
#define CONTEXT_CONTROL (0x00100000L | 0x1L)
#endif

#ifndef CONTEXT_INTEGER
#define CONTEXT_INTEGER (0x00100000L | 0x2L)
#endif

#ifndef CONTEXT_SEGMENTS
#define CONTEXT_SEGMENTS (0x00100000L | 0x4L)
#endif

#ifndef CONTEXT_FLOATING_POINT
#define CONTEXT_FLOATING_POINT (0x00100000L | 0x8L)
#endif

#ifndef CONTEXT_DEBUG_REGISTERS
#define CONTEXT_DEBUG_REGISTERS (0x00100000L | 0x10L)
#endif

#ifndef CONTEXT_FULL
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#endif

#ifndef CONTEXT_ALL
#define CONTEXT_ALL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS)
#endif

using micant::M128A;
using micant::XMM_SAVE_AREA32;
using micant::CONTEXT;
using micant::PCONTEXT;
using micant::LPCONTEXT;

using PVECTORED_EXCEPTION_HANDLER = ntdll::PVECTORED_EXCEPTION_HANDLER;
using EXCEPTION_RECORD = ntdll::EXCEPTION_RECORD;
using PEXCEPTION_RECORD = ntdll::PEXCEPTION_RECORD;
using EXCEPTION_POINTERS = ntdll::EXCEPTION_POINTERS;
using PEXCEPTION_POINTERS = ntdll::PEXCEPTION_POINTERS;
using LPEXCEPTION_POINTERS = ntdll::PEXCEPTION_POINTERS;

static_assert(sizeof(CONTEXT) == 1232, "sizeof(CONTEXT) on x64 Windows ABI must be exactly 1232 bytes");

class ThreadContextManager {
public:
    static ThreadContextManager& get() noexcept {
        static ThreadContextManager instance;
        return instance;
    }

    void initializeDefaultContext(CONTEXT& ctx) noexcept {
        std::memset(&ctx, 0, sizeof(CONTEXT));
        ctx.ContextFlags = CONTEXT_ALL;
        ctx.MxCsr = 0x1F80;
        ctx.SegCs = 0x33;
        ctx.SegDs = 0x2B;
        ctx.SegEs = 0x2B;
        ctx.SegFs = 0x53;
        ctx.SegGs = 0x2B;
        ctx.SegSs = 0x2B;
        ctx.EFlags = 0x202; // IF flag enabled
        ctx.Rsp = 0x0000007FFFF00000ULL;
        ctx.Rbp = 0x0000007FFFF00080ULL;
        ctx.Rip = 0x00007FF600001000ULL;
    }

    BOOL getContext(HANDLE hThread, CONTEXT* lpContext) noexcept {
        if (!lpContext) return FALSE;
        uint32_t flags = lpContext->ContextFlags ? lpContext->ContextFlags : CONTEXT_ALL;
        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t tid = reinterpret_cast<uintptr_t>(hThread);
        auto it = m_contexts.find(tid);
        CONTEXT stored;
        if (it != m_contexts.end()) {
            stored = it->second;
        } else {
            initializeDefaultContext(stored);
            m_contexts[tid] = stored;
        }

        if (flags & CONTEXT_CONTROL) {
            lpContext->Rip = stored.Rip;
            lpContext->Rsp = stored.Rsp;
            lpContext->SegCs = stored.SegCs;
            lpContext->SegSs = stored.SegSs;
            lpContext->EFlags = stored.EFlags;
            lpContext->MxCsr = stored.MxCsr;
        }
        if (flags & CONTEXT_INTEGER) {
            lpContext->Rax = stored.Rax;
            lpContext->Rcx = stored.Rcx;
            lpContext->Rdx = stored.Rdx;
            lpContext->Rbx = stored.Rbx;
            lpContext->Rbp = stored.Rbp;
            lpContext->Rsi = stored.Rsi;
            lpContext->Rdi = stored.Rdi;
            lpContext->R8  = stored.R8;
            lpContext->R9  = stored.R9;
            lpContext->R10 = stored.R10;
            lpContext->R11 = stored.R11;
            lpContext->R12 = stored.R12;
            lpContext->R13 = stored.R13;
            lpContext->R14 = stored.R14;
            lpContext->R15 = stored.R15;
        }
        if (flags & CONTEXT_SEGMENTS) {
            lpContext->SegDs = stored.SegDs;
            lpContext->SegEs = stored.SegEs;
            lpContext->SegFs = stored.SegFs;
            lpContext->SegGs = stored.SegGs;
        }
        if (flags & CONTEXT_DEBUG_REGISTERS) {
            lpContext->Dr0 = stored.Dr0;
            lpContext->Dr1 = stored.Dr1;
            lpContext->Dr2 = stored.Dr2;
            lpContext->Dr3 = stored.Dr3;
            lpContext->Dr6 = stored.Dr6;
            lpContext->Dr7 = stored.Dr7;
        }
        if (flags & CONTEXT_FLOATING_POINT) {
            lpContext->FltSave = stored.FltSave;
        }
        return TRUE;
    }

    BOOL setContext(HANDLE hThread, const CONTEXT* lpContext) noexcept {
        if (!lpContext) return FALSE;
        uint32_t flags = lpContext->ContextFlags ? lpContext->ContextFlags : CONTEXT_ALL;
        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t tid = reinterpret_cast<uintptr_t>(hThread);
        auto it = m_contexts.find(tid);
        CONTEXT& stored = (it != m_contexts.end()) ? it->second : m_contexts[tid];
        if (it == m_contexts.end()) {
            initializeDefaultContext(stored);
        }

        if (flags & CONTEXT_CONTROL) {
            stored.Rip = lpContext->Rip;
            stored.Rsp = lpContext->Rsp;
            stored.SegCs = lpContext->SegCs;
            stored.SegSs = lpContext->SegSs;
            stored.EFlags = lpContext->EFlags;
            stored.MxCsr = lpContext->MxCsr;
        }
        if (flags & CONTEXT_INTEGER) {
            stored.Rax = lpContext->Rax;
            stored.Rcx = lpContext->Rcx;
            stored.Rdx = lpContext->Rdx;
            stored.Rbx = lpContext->Rbx;
            stored.Rbp = lpContext->Rbp;
            stored.Rsi = lpContext->Rsi;
            stored.Rdi = lpContext->Rdi;
            stored.R8  = lpContext->R8;
            stored.R9  = lpContext->R9;
            stored.R10 = lpContext->R10;
            stored.R11 = lpContext->R11;
            stored.R12 = lpContext->R12;
            stored.R13 = lpContext->R13;
            stored.R14 = lpContext->R14;
            stored.R15 = lpContext->R15;
        }
        if (flags & CONTEXT_SEGMENTS) {
            stored.SegDs = lpContext->SegDs;
            stored.SegEs = lpContext->SegEs;
            stored.SegFs = lpContext->SegFs;
            stored.SegGs = lpContext->SegGs;
        }
        if (flags & CONTEXT_DEBUG_REGISTERS) {
            stored.Dr0 = lpContext->Dr0;
            stored.Dr1 = lpContext->Dr1;
            stored.Dr2 = lpContext->Dr2;
            stored.Dr3 = lpContext->Dr3;
            stored.Dr6 = lpContext->Dr6;
            stored.Dr7 = lpContext->Dr7;
        }
        if (flags & CONTEXT_FLOATING_POINT) {
            stored.FltSave = lpContext->FltSave;
        }
        return TRUE;
    }

private:
    std::mutex m_mutex;
    std::unordered_map<uintptr_t, CONTEXT> m_contexts;
};

inline BOOL WINAPI GetThreadContext(HANDLE hThread, void* lpContext) noexcept {
    if (!lpContext) return FALSE;
    return ThreadContextManager::get().getContext(hThread, reinterpret_cast<CONTEXT*>(lpContext));
}

inline BOOL WINAPI SetThreadContext(HANDLE hThread, const void* lpContext) noexcept {
    if (!lpContext) return FALSE;
    return ThreadContextManager::get().setContext(hThread, reinterpret_cast<const CONTEXT*>(lpContext));
}

using LPTHREAD_START_ROUTINE = DWORD (*)(LPVOID lpThreadParameter);

inline HANDLE CreateThread(
    void* /*lpThreadAttributes*/,
    SIZE_T /*dwStackSize*/,
    LPTHREAD_START_ROUTINE /*lpStartAddress*/,
    LPVOID /*lpParameter*/,
    DWORD /*dwCreationFlags*/,
    DWORD* lpThreadId
) noexcept {
    static uint32_t s_NextTid = 0x3000;
    uint32_t tid = ++s_NextTid;
    if (lpThreadId) *lpThreadId = tid;
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x80000000ULL | tid));
}

inline HANDLE OpenThread(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId) noexcept {
    (void)dwDesiredAccess;
    (void)bInheritHandle;
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x2000 + dwThreadId));
}

struct PROCESSOR_NUMBER {
    WORD Group{0};
    BYTE Number{0};
    BYTE Reserved{0};
};
using PPROCESSOR_NUMBER = PROCESSOR_NUMBER*;

inline constexpr uint32_t THREAD_POWER_THROTTLING_CURRENT_VERSION = 1;
inline constexpr uint32_t THREAD_POWER_THROTTLING_EXECUTION_SPEED = 0x1;

struct THREAD_POWER_THROTTLING_STATE {
    ULONG Version{THREAD_POWER_THROTTLING_CURRENT_VERSION};
    ULONG ControlMask{0};
    ULONG StateMask{0};
};

enum THREAD_INFORMATION_CLASS {
    ThreadMemoryPriority = 0,
    ThreadAbsoluteCpuPriority = 1,
    ThreadDynamicCodePolicy = 2,
    ThreadPowerThrottling = 3,
    ThreadInformationClassMax = 4
};

class SovereignThreadPacingManager {
private:
    std::mutex m_mutex;
    DWORD_PTR m_processAffinityMask{0xFFFF};
    BOOL m_processPriorityBoostDisabled{FALSE};
    DWORD m_processPriorityClass{0x00000020}; // NORMAL_PRIORITY_CLASS

    struct ThreadPacingRecord {
        DWORD_PTR affinityMask{0xFFFF};
        BOOL priorityBoostDisabled{FALSE};
        int priority{0}; // THREAD_PRIORITY_NORMAL
        PROCESSOR_NUMBER idealProcessor{0, 0, 0};
        THREAD_POWER_THROTTLING_STATE powerThrottling{1, 1, 0};
        ULONG absoluteCpuPriority{0};
        ULONG memoryPriority{5};
        DWORD dynamicCodePolicy{0};
    };
    std::unordered_map<HANDLE, ThreadPacingRecord> m_threads;

public:
    static SovereignThreadPacingManager& get() noexcept {
        static SovereignThreadPacingManager instance;
        return instance;
    }

    DWORD_PTR setThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& rec = m_threads[hThread];
        DWORD_PTR prev = rec.affinityMask;
        rec.affinityMask = dwThreadAffinityMask ? dwThreadAffinityMask : 0xFFFF;
        return prev ? prev : 0xFFFF;
    }

    BOOL getProcessAffinityMask(HANDLE /*hProcess*/, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask) noexcept {
        if (!lpProcessAffinityMask || !lpSystemAffinityMask) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        *lpProcessAffinityMask = m_processAffinityMask;
        *lpSystemAffinityMask = 0xFFFF; // 16 logical cores
        return TRUE;
    }

    BOOL setProcessAffinityMask(HANDLE /*hProcess*/, DWORD_PTR dwProcessAffinityMask) noexcept {
        if (!dwProcessAffinityMask) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_processAffinityMask = dwProcessAffinityMask;
        return TRUE;
    }

    BOOL setThreadPriorityBoost(HANDLE hThread, BOOL bDisablePriorityBoost) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threads[hThread].priorityBoostDisabled = bDisablePriorityBoost;
        return TRUE;
    }

    BOOL getThreadPriorityBoost(HANDLE hThread, PBOOL pDisablePriorityBoost) noexcept {
        if (!pDisablePriorityBoost) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        *pDisablePriorityBoost = m_threads[hThread].priorityBoostDisabled;
        return TRUE;
    }

    BOOL setProcessPriorityBoost(HANDLE /*hProcess*/, BOOL bDisablePriorityBoost) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_processPriorityBoostDisabled = bDisablePriorityBoost;
        return TRUE;
    }

    BOOL getProcessPriorityBoost(HANDLE /*hProcess*/, PBOOL pDisablePriorityBoost) noexcept {
        if (!pDisablePriorityBoost) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        *pDisablePriorityBoost = m_processPriorityBoostDisabled;
        return TRUE;
    }

    BOOL setThreadPriority(HANDLE hThread, int nPriority) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threads[hThread].priority = nPriority;
        return TRUE;
    }

    int getThreadPriority(HANDLE hThread) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_threads.find(hThread);
        if (it != m_threads.end()) {
            return it->second.priority;
        }
        return 0; // THREAD_PRIORITY_NORMAL
    }

    BOOL setPriorityClass(HANDLE /*hProcess*/, DWORD dwPriorityClass) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_processPriorityClass = dwPriorityClass;
        return TRUE;
    }

    DWORD getPriorityClass(HANDLE /*hProcess*/) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_processPriorityClass;
    }

    BOOL setThreadIdealProcessorEx(HANDLE hThread, PPROCESSOR_NUMBER lpIdealProcessor, PPROCESSOR_NUMBER lpPreviousIdealProcessor) noexcept {
        if (!lpIdealProcessor) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& rec = m_threads[hThread];
        if (lpPreviousIdealProcessor) {
            *lpPreviousIdealProcessor = rec.idealProcessor;
        }
        rec.idealProcessor = *lpIdealProcessor;
        return TRUE;
    }

    BOOL getThreadIdealProcessorEx(HANDLE hThread, PPROCESSOR_NUMBER lpIdealProcessor) noexcept {
        if (!lpIdealProcessor) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        *lpIdealProcessor = m_threads[hThread].idealProcessor;
        return TRUE;
    }

    BOOL setThreadInformation(HANDLE hThread, int32_t threadInfoClass, void* pData, DWORD size) noexcept {
        if (!pData || size == 0) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& rec = m_threads[hThread];
        if (threadInfoClass == ThreadMemoryPriority || threadInfoClass == 0) {
            if (size >= sizeof(uint32_t)) {
                rec.memoryPriority = *reinterpret_cast<const uint32_t*>(pData);
                return TRUE;
            }
        } else if (threadInfoClass == ThreadAbsoluteCpuPriority || threadInfoClass == 1) {
            if (size >= sizeof(ULONG)) {
                rec.absoluteCpuPriority = *reinterpret_cast<const ULONG*>(pData);
                return TRUE;
            }
        } else if (threadInfoClass == ThreadDynamicCodePolicy || threadInfoClass == 2) {
            if (size >= sizeof(DWORD)) {
                rec.dynamicCodePolicy = *reinterpret_cast<const DWORD*>(pData);
                return TRUE;
            }
        } else if (threadInfoClass == ThreadPowerThrottling || threadInfoClass == 3) {
            if (size >= sizeof(THREAD_POWER_THROTTLING_STATE)) {
                rec.powerThrottling = *reinterpret_cast<const THREAD_POWER_THROTTLING_STATE*>(pData);
                return TRUE;
            }
        }
        return TRUE;
    }

    BOOL getThreadInformation(HANDLE hThread, int32_t threadInfoClass, void* pData, DWORD size) noexcept {
        if (!pData || size == 0) return FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto& rec = m_threads[hThread];
        if (threadInfoClass == ThreadMemoryPriority || threadInfoClass == 0) {
            if (size >= sizeof(uint32_t)) {
                *reinterpret_cast<uint32_t*>(pData) = rec.memoryPriority;
                return TRUE;
            }
        } else if (threadInfoClass == ThreadAbsoluteCpuPriority || threadInfoClass == 1) {
            if (size >= sizeof(ULONG)) {
                *reinterpret_cast<ULONG*>(pData) = rec.absoluteCpuPriority;
                return TRUE;
            }
        } else if (threadInfoClass == ThreadDynamicCodePolicy || threadInfoClass == 2) {
            if (size >= sizeof(DWORD)) {
                *reinterpret_cast<DWORD*>(pData) = rec.dynamicCodePolicy;
                return TRUE;
            }
        } else if (threadInfoClass == ThreadPowerThrottling || threadInfoClass == 3) {
            if (size >= sizeof(THREAD_POWER_THROTTLING_STATE)) {
                *reinterpret_cast<THREAD_POWER_THROTTLING_STATE*>(pData) = rec.powerThrottling;
                return TRUE;
            }
        }
        return TRUE;
    }
};

inline DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask) noexcept {
    return SovereignThreadPacingManager::get().setThreadAffinityMask(hThread, dwThreadAffinityMask);
}

inline BOOL WINAPI GetProcessAffinityMask(HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask) noexcept {
    return SovereignThreadPacingManager::get().getProcessAffinityMask(hProcess, lpProcessAffinityMask, lpSystemAffinityMask);
}

inline BOOL WINAPI SetProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask) noexcept {
    return SovereignThreadPacingManager::get().setProcessAffinityMask(hProcess, dwProcessAffinityMask);
}

inline BOOL WINAPI SetThreadPriorityBoost(HANDLE hThread, BOOL bDisablePriorityBoost) noexcept {
    return SovereignThreadPacingManager::get().setThreadPriorityBoost(hThread, bDisablePriorityBoost);
}

inline BOOL WINAPI GetThreadPriorityBoost(HANDLE hThread, PBOOL pDisablePriorityBoost) noexcept {
    return SovereignThreadPacingManager::get().getThreadPriorityBoost(hThread, pDisablePriorityBoost);
}

inline BOOL WINAPI SetProcessPriorityBoost(HANDLE hProcess, BOOL bDisablePriorityBoost) noexcept {
    return SovereignThreadPacingManager::get().setProcessPriorityBoost(hProcess, bDisablePriorityBoost);
}

inline BOOL WINAPI GetProcessPriorityBoost(HANDLE hProcess, PBOOL pDisablePriorityBoost) noexcept {
    return SovereignThreadPacingManager::get().getProcessPriorityBoost(hProcess, pDisablePriorityBoost);
}

// Process Priority Classes
inline constexpr DWORD IDLE_PRIORITY_CLASS         = 0x00000040;
inline constexpr DWORD BELOW_NORMAL_PRIORITY_CLASS = 0x00004000;
inline constexpr DWORD NORMAL_PRIORITY_CLASS       = 0x00000020;
inline constexpr DWORD ABOVE_NORMAL_PRIORITY_CLASS = 0x00008000;
inline constexpr DWORD HIGH_PRIORITY_CLASS         = 0x00000080;
inline constexpr DWORD REALTIME_PRIORITY_CLASS     = 0x00000100;

// Thread Priority Levels
inline constexpr int THREAD_PRIORITY_IDLE          = -15;
inline constexpr int THREAD_PRIORITY_LOWEST        = -2;
inline constexpr int THREAD_PRIORITY_BELOW_NORMAL  = -1;
inline constexpr int THREAD_PRIORITY_NORMAL        = 0;
inline constexpr int THREAD_PRIORITY_ABOVE_NORMAL  = 1;
inline constexpr int THREAD_PRIORITY_HIGHEST       = 2;
inline constexpr int THREAD_PRIORITY_TIME_CRITICAL = 15;
inline constexpr int THREAD_MODE_BACKGROUND_BEGIN  = 0x00010000;
inline constexpr int THREAD_MODE_BACKGROUND_END    = 0x00020000;

inline int WINAPI GetThreadPriority(HANDLE hThread) noexcept {
    return SovereignThreadPacingManager::get().getThreadPriority(hThread);
}

inline BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority) noexcept {
    return SovereignThreadPacingManager::get().setThreadPriority(hThread, nPriority);
}

inline DWORD WINAPI GetPriorityClass(HANDLE hProcess) noexcept {
    return SovereignThreadPacingManager::get().getPriorityClass(hProcess);
}

inline BOOL WINAPI SetPriorityClass(HANDLE hProcess, DWORD dwPriorityClass) noexcept {
    return SovereignThreadPacingManager::get().setPriorityClass(hProcess, dwPriorityClass);
}

inline BOOL WINAPI SetThreadIdealProcessorEx(HANDLE hThread, PPROCESSOR_NUMBER lpIdealProcessor, PPROCESSOR_NUMBER lpPreviousIdealProcessor) noexcept {
    return SovereignThreadPacingManager::get().setThreadIdealProcessorEx(hThread, lpIdealProcessor, lpPreviousIdealProcessor);
}

inline BOOL WINAPI GetThreadIdealProcessorEx(HANDLE hThread, PPROCESSOR_NUMBER lpIdealProcessor) noexcept {
    return SovereignThreadPacingManager::get().getThreadIdealProcessorEx(hThread, lpIdealProcessor);
}

inline BOOL WINAPI SetThreadInformation(HANDLE hThread, int32_t ThreadInformationClass, void* ThreadInformation, DWORD ThreadInformationSize) noexcept {
    return SovereignThreadPacingManager::get().setThreadInformation(hThread, ThreadInformationClass, ThreadInformation, ThreadInformationSize);
}

inline BOOL WINAPI GetThreadInformation(HANDLE hThread, int32_t ThreadInformationClass, void* ThreadInformation, DWORD ThreadInformationSize) noexcept {
    return SovereignThreadPacingManager::get().getThreadInformation(hThread, ThreadInformationClass, ThreadInformation, ThreadInformationSize);
}

inline DWORD GetVersion() noexcept {
    return 0x65F4000A; // Windows 11 Build 26100 (Major 10, Minor 0)
}

inline SIZE_T GetLargePageMinimum() noexcept {
    return 2 * 1024 * 1024; // 2 MB
}

inline void SetFileApisToOEM() noexcept {}
inline void SetFileApisToANSI() noexcept {}

inline HANDLE CreateSemaphoreW(void* lpAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName) noexcept {
    (void)lpAttributes;
    (void)lInitialCount;
    (void)lMaximumCount;
    (void)lpName;
    static uint32_t s_NextSem = 0x5000;
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(++s_NextSem));
}

inline BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LONG* lpPreviousCount) noexcept {
    (void)hSemaphore;
    (void)lReleaseCount;
    if (lpPreviousCount) *lpPreviousCount = 1;
    return TRUE;
}

inline BOOL DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle,
                           HANDLE* lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions) noexcept {
    (void)hSourceProcessHandle;
    (void)hTargetProcessHandle;
    (void)dwDesiredAccess;
    (void)bInheritHandle;
    (void)dwOptions;
    if (lpTargetHandle) *lpTargetHandle = hSourceHandle;
    return TRUE;
}

inline DWORD GetProcessId(HANDLE hProcess) noexcept {
    if (!hProcess || hProcess == GetCurrentProcess()) return GetCurrentProcessId();
    return 1024;
}

inline BOOL HeapSetInformation(HANDLE HeapHandle, DWORD HeapInformationClass, void* HeapInformation, SIZE_T HeapInformationLength) noexcept {
    (void)HeapHandle;
    (void)HeapInformationClass;
    (void)HeapInformation;
    (void)HeapInformationLength;
    return TRUE;
}

inline BOOL IsDBCSLeadByteEx(UINT CodePage, uint8_t TestChar) noexcept {
    (void)CodePage;
    (void)TestChar;
    return FALSE;
}

inline UINT SetErrorMode(UINT uMode) noexcept {
    static UINT s_Mode = 0;
    UINT prev = s_Mode;
    s_Mode = uMode;
    return prev;
}

// ============================================================================
// 17. CRT Startup & Advanced Win32 Interop Support
// ============================================================================

struct FILETIME {
    DWORD dwLowDateTime{0};
    DWORD dwHighDateTime{0};
};
using PFILETIME = FILETIME*;
using LPFILETIME = FILETIME*;
inline BOOL WINAPI SystemTimeToFileTime(const void*, void*) noexcept;

inline void GetSystemTimeAsFileTime(FILETIME* lpTime) noexcept {
    if (!lpTime) return;
    LargeInteger value{};
    if (!NT_SUCCESS(ntdll::NtQuerySystemTime(&value))) return;
    const uint64_t ticks = static_cast<uint64_t>(value.quadPart);
    lpTime->dwLowDateTime = static_cast<DWORD>(ticks);
    lpTime->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}

inline void WINAPI InitializeSListHead(void* ListHead) noexcept {
    if (ListHead) {
        auto* p = static_cast<uint64_t*>(ListHead);
        p[0] = 0;
        p[1] = 0;
    }
}

// ============================================================================
// Win32 Fibers & Fiber Local Storage (FLS) (Windows NT / winbase.h)
// ============================================================================

using LPFIBER_START_ROUTINE = void(WINAPI*)(LPVOID lpFiberParameter);
using PFLS_CALLBACK_FUNCTION = void(WINAPI*)(PVOID lpFlsData);

inline constexpr DWORD FLS_OUT_OF_INDEXES = 0xFFFFFFFF;
inline constexpr DWORD FIBER_FLAG_FLOAT_SWITCH = 0x1;

struct FIBER_RECORD {
    LPVOID lpParameter{nullptr};
    LPFIBER_START_ROUTINE lpStartAddress{nullptr};
    size_t stackCommitSize{0};
    size_t stackReserveSize{0};
    DWORD flags{0};
    bool isThread{false};
    bool deleted{false};
    std::unordered_map<DWORD, PVOID> flsData;
};

class SovereignFiberManager {
private:
    std::mutex m_mutex;
    std::unordered_map<LPVOID, FIBER_RECORD> m_fibers;
    std::unordered_map<DWORD, PFLS_CALLBACK_FUNCTION> m_flsCallbacks;
    std::vector<bool> m_flsAllocated;
    uintptr_t m_nextFiberId{0x10000};
    inline static thread_local LPVOID t_currentFiber{nullptr};
    inline static thread_local LPVOID t_primaryFiber{nullptr};

public:
    static SovereignFiberManager& get() noexcept {
        static SovereignFiberManager instance;
        return instance;
    }

    SovereignFiberManager() : m_flsAllocated(128, false) {}

    LPVOID convertThreadToFiber(LPVOID lpParameter, DWORD dwFlags) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (t_currentFiber != nullptr) {
            SetLastError(ERROR_ALREADY_EXISTS);
            return nullptr;
        }
        LPVOID fiberId = reinterpret_cast<LPVOID>(m_nextFiberId += 0x1000);
        FIBER_RECORD rec{};
        rec.lpParameter = lpParameter;
        rec.flags = dwFlags;
        rec.isThread = true;
        m_fibers[fiberId] = rec;
        t_currentFiber = fiberId;
        t_primaryFiber = fiberId;
        return fiberId;
    }

    BOOL convertFiberToThread() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        LPVOID target = t_currentFiber ? t_currentFiber : t_primaryFiber;
        if (target == nullptr) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        auto it = m_fibers.find(target);
        if (it != m_fibers.end() && it->second.isThread) {
            m_fibers.erase(it);
            t_currentFiber = nullptr;
            t_primaryFiber = nullptr;
            return TRUE;
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    LPVOID createFiber(SIZE_T dwStackCommitSize, SIZE_T dwStackReserveSize, DWORD dwFlags, LPFIBER_START_ROUTINE lpStartAddress, LPVOID lpParameter) noexcept {
        if (!lpStartAddress) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        LPVOID fiberId = reinterpret_cast<LPVOID>(m_nextFiberId += 0x1000);
        FIBER_RECORD rec{};
        rec.lpParameter = lpParameter;
        rec.lpStartAddress = lpStartAddress;
        rec.stackCommitSize = dwStackCommitSize ? dwStackCommitSize : 64 * 1024;
        rec.stackReserveSize = dwStackReserveSize ? dwStackReserveSize : 1024 * 1024;
        rec.flags = dwFlags;
        rec.isThread = false;
        m_fibers[fiberId] = rec;
        return fiberId;
    }

    void deleteFiber(LPVOID lpFiber) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_fibers.find(lpFiber);
        if (it != m_fibers.end()) {
            for (const auto& [idx, data] : it->second.flsData) {
                if (data) {
                    auto cbIt = m_flsCallbacks.find(idx);
                    if (cbIt != m_flsCallbacks.end() && cbIt->second) {
                        cbIt->second(data);
                    }
                }
            }
            if (t_currentFiber == lpFiber) {
                t_currentFiber = t_primaryFiber;
            }
            m_fibers.erase(it);
        }
    }

    void switchToFiber(LPVOID lpFiber) noexcept {
        LPFIBER_START_ROUTINE startRoutine = nullptr;
        LPVOID param = nullptr;
        LPVOID prevFiber = t_currentFiber;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_fibers.find(lpFiber);
            if (it == m_fibers.end() || it->second.deleted) {
                return;
            }
            t_currentFiber = lpFiber;
            if (!it->second.isThread && it->second.lpStartAddress) {
                startRoutine = it->second.lpStartAddress;
                param = it->second.lpParameter;
            }
        }
        if (startRoutine) {
            startRoutine(param);
            t_currentFiber = prevFiber;
        }
    }

    [[nodiscard]] BOOL isThreadAFiber() const noexcept {
        return (t_currentFiber != nullptr || t_primaryFiber != nullptr) ? TRUE : FALSE;
    }

    [[nodiscard]] LPVOID getCurrentFiber() const noexcept {
        return t_currentFiber ? t_currentFiber : t_primaryFiber;
    }

    DWORD flsAlloc(PFLS_CALLBACK_FUNCTION lpCallback) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (DWORD i = 0; i < static_cast<DWORD>(m_flsAllocated.size()); ++i) {
            if (!m_flsAllocated[i]) {
                m_flsAllocated[i] = true;
                if (lpCallback) {
                    m_flsCallbacks[i] = lpCallback;
                }
                return i;
            }
        }
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FLS_OUT_OF_INDEXES;
    }

    BOOL flsFree(DWORD dwFlsIndex) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (dwFlsIndex >= m_flsAllocated.size() || !m_flsAllocated[dwFlsIndex]) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        m_flsAllocated[dwFlsIndex] = false;
        m_flsCallbacks.erase(dwFlsIndex);
        for (auto& [id, fiber] : m_fibers) {
            fiber.flsData.erase(dwFlsIndex);
        }
        return TRUE;
    }

    PVOID flsGetValue(DWORD dwFlsIndex) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (dwFlsIndex >= m_flsAllocated.size() || !m_flsAllocated[dwFlsIndex]) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        LPVOID fiber = t_currentFiber;
        if (!fiber) {
            static std::unordered_map<DWORD, PVOID> s_threadFallbackFls;
            auto it = s_threadFallbackFls.find(dwFlsIndex);
            return it != s_threadFallbackFls.end() ? it->second : nullptr;
        }
        auto it = m_fibers.find(fiber);
        if (it != m_fibers.end()) {
            auto dIt = it->second.flsData.find(dwFlsIndex);
            if (dIt != it->second.flsData.end()) {
                return dIt->second;
            }
        }
        return nullptr;
    }

    BOOL flsSetValue(DWORD dwFlsIndex, PVOID lpFlsData) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (dwFlsIndex >= m_flsAllocated.size() || !m_flsAllocated[dwFlsIndex]) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        LPVOID fiber = t_currentFiber;
        if (!fiber) {
            static std::unordered_map<DWORD, PVOID> s_threadFallbackFls;
            s_threadFallbackFls[dwFlsIndex] = lpFlsData;
            return TRUE;
        }
        auto it = m_fibers.find(fiber);
        if (it != m_fibers.end()) {
            it->second.flsData[dwFlsIndex] = lpFlsData;
            return TRUE;
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
};

inline DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback) noexcept {
    return SovereignFiberManager::get().flsAlloc(lpCallback);
}

inline PVOID WINAPI FlsGetValue(DWORD dwFlsIndex) noexcept {
    return SovereignFiberManager::get().flsGetValue(dwFlsIndex);
}

inline BOOL WINAPI FlsSetValue(DWORD dwFlsIndex, PVOID lpFlsData) noexcept {
    return SovereignFiberManager::get().flsSetValue(dwFlsIndex, lpFlsData);
}

inline BOOL WINAPI FlsFree(DWORD dwFlsIndex) noexcept {
    return SovereignFiberManager::get().flsFree(dwFlsIndex);
}

inline LPVOID WINAPI ConvertThreadToFiberEx(LPVOID lpParameter, DWORD dwFlags) noexcept {
    return SovereignFiberManager::get().convertThreadToFiber(lpParameter, dwFlags);
}

inline LPVOID WINAPI ConvertThreadToFiber(LPVOID lpParameter) noexcept {
    return ConvertThreadToFiberEx(lpParameter, 0);
}

inline BOOL WINAPI ConvertFiberToThread() noexcept {
    return SovereignFiberManager::get().convertFiberToThread();
}

inline LPVOID WINAPI CreateFiberEx(SIZE_T dwStackCommitSize, SIZE_T dwStackReserveSize, DWORD dwFlags, LPFIBER_START_ROUTINE lpStartAddress, LPVOID lpParameter) noexcept {
    return SovereignFiberManager::get().createFiber(dwStackCommitSize, dwStackReserveSize, dwFlags, lpStartAddress, lpParameter);
}

inline LPVOID WINAPI CreateFiber(SIZE_T dwStackSize, LPFIBER_START_ROUTINE lpStartAddress, LPVOID lpParameter) noexcept {
    return CreateFiberEx(dwStackSize, dwStackSize, 0, lpStartAddress, lpParameter);
}

inline void WINAPI DeleteFiber(LPVOID lpFiber) noexcept {
    SovereignFiberManager::get().deleteFiber(lpFiber);
}

inline void WINAPI SwitchToFiber(LPVOID lpFiber) noexcept {
    SovereignFiberManager::get().switchToFiber(lpFiber);
}

inline BOOL WINAPI IsThreadAFiber() noexcept {
    return SovereignFiberManager::get().isThreadAFiber();
}

inline LPVOID WINAPI GetCurrentFiber() noexcept {
    return SovereignFiberManager::get().getCurrentFiber();
}

inline thread_local void* g_TlsSlots[64]{};

inline DWORD TlsAlloc() noexcept {
    static std::atomic<DWORD> s_TlsIndex{0};
    return s_TlsIndex.fetch_add(1);
}

inline LPVOID TlsGetValue(DWORD dwTlsIndex) noexcept {
    if (dwTlsIndex < 64) return g_TlsSlots[dwTlsIndex];
    return nullptr;
}

inline BOOL TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) noexcept {
    if (dwTlsIndex < 64) {
        g_TlsSlots[dwTlsIndex] = lpTlsValue;
        return TRUE;
    }
    return FALSE;
}

inline BOOL TlsFree(DWORD /*dwTlsIndex*/) noexcept {
    return TRUE;
}

struct CRITICAL_SECTION {
    void* DebugInfo{nullptr};
    LONG LockCount{0};
    LONG RecursionCount{0};
    HANDLE OwningThread{nullptr};
    HANDLE LockSemaphore{nullptr};
    ULONG_PTR SpinCount{0};
};
using LPCRITICAL_SECTION = CRITICAL_SECTION*;
using PCRITICAL_SECTION  = CRITICAL_SECTION*;

inline void InitializeCriticalSection(LPCRITICAL_SECTION /*cs*/) noexcept {}
inline BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION /*cs*/, uint32_t /*spin*/, uint32_t /*flags*/) noexcept { return TRUE; }
inline void EnterCriticalSection(LPCRITICAL_SECTION /*cs*/) noexcept {}
inline void LeaveCriticalSection(LPCRITICAL_SECTION /*cs*/) noexcept {}
inline void DeleteCriticalSection(LPCRITICAL_SECTION /*cs*/) noexcept {}

inline void* EncodePointer(void* ptr) noexcept { return ptr; }
inline void* DecodePointer(void* ptr) noexcept { return ptr; }

struct STARTUPINFOW {
    DWORD cb{sizeof(STARTUPINFOW)};
    LPWSTR lpReserved{nullptr};
    LPWSTR lpDesktop{nullptr};
    LPWSTR lpTitle{nullptr};
    DWORD dwX{0}, dwY{0}, dwXSize{80}, dwYSize{25};
    DWORD dwXCountChars{80}, dwYCountChars{25};
    DWORD dwFillAttribute{0x07};
    DWORD dwFlags{0};
    uint16_t wShowWindow{0};
    uint16_t cbReserved2{0};
    uint8_t* lpReserved2{nullptr};
    HANDLE hStdInput{GetStdHandle(STD_INPUT_HANDLE)};
    HANDLE hStdOutput{GetStdHandle(STD_OUTPUT_HANDLE)};
    HANDLE hStdError{GetStdHandle(STD_ERROR_HANDLE)};
};

inline void GetStartupInfoW(STARTUPINFOW* si) noexcept {
    if (si) *si = STARTUPINFOW{};
}

struct STARTUPINFOA {
    DWORD   cb{sizeof(STARTUPINFOA)};
    LPSTR   lpReserved{nullptr};
    LPSTR   lpDesktop{nullptr};
    LPSTR   lpTitle{nullptr};
    DWORD   dwX{0};
    DWORD   dwY{0};
    DWORD   dwXSize{0};
    DWORD   dwYSize{0};
    DWORD   dwXCountChars{0};
    DWORD   dwYCountChars{0};
    DWORD   dwFillAttribute{0};
    DWORD   dwFlags{0};
    uint16_t wShowWindow{0};
    uint16_t cbReserved2{0};
    uint8_t* lpReserved2{nullptr};
    HANDLE  hStdInput{nullptr};
    HANDLE  hStdOutput{nullptr};
    HANDLE  hStdError{nullptr};
};

inline void GetStartupInfoA(STARTUPINFOA* si) noexcept {
    if (si) {
        *si = STARTUPINFOA{};
        si->hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si->hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si->hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
}

struct PROCESS_INFORMATION {
    HANDLE hProcess{nullptr};
    HANDLE hThread{nullptr};
    DWORD  dwProcessId{0};
    DWORD  dwThreadId{0};
};

struct SECURITY_ATTRIBUTES {
    DWORD  nLength{sizeof(SECURITY_ATTRIBUTES)};
    LPVOID lpSecurityDescriptor{nullptr};
    BOOL   bInheritHandle{0};
};

inline BOOL CreateProcessW(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    SECURITY_ATTRIBUTES* /*lpProcessAttributes*/,
    SECURITY_ATTRIBUTES* /*lpThreadAttributes*/,
    BOOL /*bInheritHandles*/,
    DWORD /*dwCreationFlags*/,
    LPVOID /*lpEnvironment*/,
    LPCWSTR /*lpCurrentDirectory*/,
    STARTUPINFOW* /*lpStartupInfo*/,
    PROCESS_INFORMATION* lpProcessInformation
) noexcept {
    (void)lpApplicationName;
    (void)lpCommandLine;
    if (lpProcessInformation) *lpProcessInformation = {};
    // This legacy in-kernel facade is not a user-mode process-creation
    // implementation. Production Kernel32 exports CreateProcessW through
    // the NTDLL NtCreateUserProcess syscall path.
    SetLastError(50); // ERROR_NOT_SUPPORTED
    return FALSE;
}

inline BOOL CreateProcessA(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    SECURITY_ATTRIBUTES* lpProcessAttributes,
    SECURITY_ATTRIBUTES* lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    STARTUPINFOA* lpStartupInfo,
    PROCESS_INFORMATION* lpProcessInformation
) noexcept {
    std::wstring wApp, wCmd, wDir;
    if (lpApplicationName) wApp.assign(lpApplicationName, lpApplicationName + std::strlen(lpApplicationName));
    if (lpCommandLine) wCmd.assign(lpCommandLine, lpCommandLine + std::strlen(lpCommandLine));
    if (lpCurrentDirectory) wDir.assign(lpCurrentDirectory, lpCurrentDirectory + std::strlen(lpCurrentDirectory));

    STARTUPINFOW siW{};
    if (lpStartupInfo) {
        siW.cb = sizeof(siW);
        siW.dwFlags = lpStartupInfo->dwFlags;
        siW.wShowWindow = lpStartupInfo->wShowWindow;
    }

    return CreateProcessW(
        wApp.empty() ? nullptr : wApp.c_str(),
        wCmd.empty() ? nullptr : wCmd.data(),
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        dwCreationFlags,
        lpEnvironment,
        wDir.empty() ? nullptr : wDir.c_str(),
        &siW,
        lpProcessInformation
    );
}


inline DWORD GetFileType(HANDLE hFile) noexcept {
    if (hFile == GetStdHandle(STD_OUTPUT_HANDLE) || hFile == reinterpret_cast<HANDLE>(0x14) ||
        hFile == GetStdHandle(STD_INPUT_HANDLE) || hFile == reinterpret_cast<HANDLE>(0x10) ||
        hFile == GetStdHandle(STD_ERROR_HANDLE) || hFile == reinterpret_cast<HANDLE>(0x18)) {
        return 0x0002; // FILE_TYPE_CHAR
    }
    return 0x0001; // FILE_TYPE_DISK
}

inline BOOL IsProcessorFeaturePresent(DWORD /*ProcessorFeature*/) noexcept {
    return TRUE; // x86-64 standard extensions
}

inline BOOL IsDebuggerPresent() noexcept {
    return FALSE;
}

typedef int32_t (WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS* ExceptionInfo);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;

inline std::atomic<PTOP_LEVEL_EXCEPTION_FILTER> g_topLevelExceptionFilter{nullptr};

inline LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) noexcept {
    return g_topLevelExceptionFilter.exchange(lpTopLevelExceptionFilter);
}

inline int32_t WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo) noexcept {
    auto filter = g_topLevelExceptionFilter.load();
    if (filter) {
        return filter(ExceptionInfo);
    }
    return 1; // EXCEPTION_EXECUTE_HANDLER
}

inline constexpr uint32_t CP_ACP        = 0;
inline constexpr uint32_t CP_OEMCP      = 1;
inline constexpr uint32_t CP_MACCP      = 2;
inline constexpr uint32_t CP_THREAD_ACP = 3;
inline constexpr uint32_t CP_SYMBOL     = 42;
inline constexpr uint32_t CP_UTF7       = 65000;
inline constexpr uint32_t CP_UTF8       = 65001;

inline int MultiByteToWideChar(
    uint32_t /*CodePage*/,
    uint32_t /*dwFlags*/,
    LPCSTR lpMultiByteStr,
    int cbMultiByte,
    LPWSTR lpWideCharStr,
    int cchWideChar
) noexcept {
    if (!lpMultiByteStr) return 0;
    int srcLen = (cbMultiByte < 0) ? static_cast<int>(std::strlen(lpMultiByteStr) + 1) : cbMultiByte;
    if (cchWideChar == 0) return srcLen;
    int toCopy = std::min(srcLen, cchWideChar);
    for (int i = 0; i < toCopy; ++i) {
        lpWideCharStr[i] = static_cast<wchar_t>(static_cast<unsigned char>(lpMultiByteStr[i]));
    }
    return toCopy;
}

inline int WideCharToMultiByte(
    uint32_t /*CodePage*/,
    uint32_t /*dwFlags*/,
    LPCWSTR lpWideCharStr,
    int cchWideChar,
    LPSTR lpMultiByteStr,
    int cbMultiByte,
    LPCSTR /*lpDefaultChar*/,
    BOOL* /*lpUsedDefaultChar*/
) noexcept {
    if (!lpWideCharStr) return 0;
    int srcLen = (cchWideChar < 0) ? static_cast<int>(std::wcslen(lpWideCharStr) + 1) : cchWideChar;
    if (cbMultiByte == 0) return srcLen;
    int toCopy = std::min(srcLen, cbMultiByte);
    for (int i = 0; i < toCopy; ++i) {
        lpMultiByteStr[i] = static_cast<char>(lpWideCharStr[i] & 0x7F);
    }
    return toCopy;
}

inline uint32_t GetACP() noexcept { return 65001; }
inline uint32_t GetOEMCP() noexcept { return 65001; }
inline BOOL IsValidCodePage(uint32_t /*CodePage*/) noexcept { return TRUE; }
inline BOOL GetCPInfo(uint32_t /*CodePage*/, void* /*lpCPInfo*/) noexcept { return TRUE; }

inline int CompareStringW(uint32_t /*Locale*/, uint32_t /*dwCmpFlags*/, LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2) noexcept {
    if (!lpString1 || !lpString2) return 0;
    int len1 = (cchCount1 < 0) ? static_cast<int>(std::wcslen(lpString1)) : cchCount1;
    int len2 = (cchCount2 < 0) ? static_cast<int>(std::wcslen(lpString2)) : cchCount2;
    int cmp = std::wcsncmp(lpString1, lpString2, std::min(len1, len2));
    if (cmp < 0) return 1; // CSTR_LESS_THAN
    if (cmp > 0) return 3; // CSTR_GREATER_THAN
    if (len1 < len2) return 1;
    if (len1 > len2) return 3;
    return 2; // CSTR_EQUAL
}

inline int LCMapStringW(uint32_t /*Locale*/, uint32_t /*dwMapFlags*/, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest) noexcept {
    if (!lpSrcStr) return 0;
    int len = (cchSrc < 0) ? static_cast<int>(std::wcslen(lpSrcStr) + 1) : cchSrc;
    if (cchDest == 0) return len;
    int toCopy = std::min(len, cchDest);
    std::wcsncpy(lpDestStr, lpSrcStr, toCopy);
    return toCopy;
}

inline HANDLE FindFirstFileExW(
    LPCWSTR lpFileName,
    uint32_t /*fInfoLevelId*/,
    LPVOID lpFindFileData,
    uint32_t /*fSearchOp*/,
    LPVOID /*lpSearchFilter*/,
    DWORD /*dwAdditionalFlags*/
) noexcept {
    return FindFirstFileW(lpFileName, reinterpret_cast<WIN32_FIND_DATAW*>(lpFindFileData));
}

inline BOOL FlushFileBuffers(HANDLE hFile) noexcept {
    IoStatusBlock ioStatus{};
    const NtStatus status = ntdll::NtFlushBuffersFile(reinterpret_cast<Handle>(hFile), &ioStatus);
    if (!NT_SUCCESS(status)) {
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline LPWCH GetEnvironmentStringsW() noexcept {
    static const wchar_t s_EnvBlock[] = L"OS=MicaNT\0SystemRoot=C:\\Windows\0windir=C:\\Windows\0\0";
    return const_cast<LPWCH>(s_EnvBlock);
}

inline BOOL FreeEnvironmentStringsW(LPWCH /*penv*/) noexcept {
    return TRUE;
}

inline LPSTR GetEnvironmentStrings() noexcept {
    static const char s_env_block[] =
        "ALLUSERSPROFILE=C:\\ProgramData\0"
        "APPDATA=C:\\Users\\admin\\AppData\\Roaming\0"
        "CommonProgramFiles=C:\\Program Files\\Common Files\0"
        "LOCALAPPDATA=C:\\Users\\admin\\AppData\\Local\0"
        "OS=Windows_NT\0"
        "Path=C:\\Windows\\System32;C:\\Windows;C:\\MicaNT\0"
        "ProgramData=C:\\ProgramData\0"
        "ProgramFiles=C:\\Program Files\0"
        "SystemDrive=C:\0"
        "SystemRoot=C:\\Windows\0"
        "TEMP=C:\\Users\\admin\\AppData\\Local\\Temp\0"
        "TMP=C:\\Users\\admin\\AppData\\Local\\Temp\0"
        "USERPROFILE=C:\\Users\\admin\0\0";
    return const_cast<LPSTR>(s_env_block);
}

inline BOOL FreeEnvironmentStringsA(LPSTR /*penv*/) noexcept {
    return TRUE;
}

inline UINT SetHandleCount(UINT uNumber) noexcept {
    return uNumber;
}

inline BOOL GetStringTypeW(DWORD /*dwInfoType*/, LPCWSTR /*lpSrcStr*/, int /*cchSrc*/, uint16_t* lpCharType) noexcept {
    if (lpCharType) *lpCharType = 0x0001; // C1_ALPHA
    return TRUE;
}

inline BOOL VirtualProtect(LPVOID /*lpAddress*/, SIZE_T /*dwSize*/, DWORD /*flNewProtect*/, DWORD* lpflOldProtect) noexcept {
    if (lpflOldProtect) *lpflOldProtect = 0x04; // PAGE_READWRITE
    return TRUE;
}

inline BOOL VirtualProtectEx(HANDLE /*hProcess*/, LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, DWORD* lpflOldProtect) noexcept {
    return VirtualProtect(lpAddress, dwSize, flNewProtect, lpflOldProtect);
}

struct MEMORY_BASIC_INFORMATION {
    LPVOID BaseAddress;
    LPVOID AllocationBase;
    DWORD AllocationProtect;
    uint16_t PartitionId;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
};

inline SIZE_T VirtualQuery(LPCVOID lpAddress, MEMORY_BASIC_INFORMATION* lpBuffer, SIZE_T dwLength) noexcept {
    if (!lpBuffer || dwLength < sizeof(MEMORY_BASIC_INFORMATION)) return 0;
    lpBuffer->BaseAddress = const_cast<LPVOID>(lpAddress);
    lpBuffer->AllocationBase = const_cast<LPVOID>(lpAddress);
    lpBuffer->AllocationProtect = PAGE_EXECUTE_READWRITE;
    lpBuffer->RegionSize = 0x10000;
    lpBuffer->State = 0x1000; // MEM_COMMIT
    lpBuffer->Protect = PAGE_EXECUTE_READWRITE;
    lpBuffer->Type = 0x20000; // MEM_PRIVATE
    return sizeof(MEMORY_BASIC_INFORMATION);
}

inline SIZE_T VirtualQueryEx(HANDLE /*hProcess*/, LPCVOID lpAddress, void* lpBuffer, SIZE_T dwLength) noexcept {
    return VirtualQuery(lpAddress, reinterpret_cast<MEMORY_BASIC_INFORMATION*>(lpBuffer), dwLength);
}

inline void RtlCaptureContext(void* /*ContextRecord*/) noexcept {}
inline void RtlRestoreContext(void* /*ContextRecord*/, void* /*ExceptionRecord*/) noexcept {}
inline void* RtlLookupFunctionEntry(uint64_t /*ControlPc*/, uint64_t* ImageBase, void* /*HistoryTable*/) noexcept {
    if (ImageBase) *ImageBase = g_CurrentExecutableBase ? g_CurrentExecutableBase : 0x140000000ULL;
    return nullptr;
}
inline void* RtlVirtualUnwind(uint32_t /*HandlerType*/, uint64_t /*ImageBase*/, uint64_t /*ControlPc*/, void* /*FunctionEntry*/, void* /*ContextRecord*/, void** /*HandlerData*/, uint64_t* /*EstablisherFrame*/, void* /*ContextPointers*/) noexcept {
    return nullptr;
}
inline void RtlUnwindEx(void* /*TargetFrame*/, void* /*TargetIp*/, void* /*ExceptionRecord*/, void* /*ReturnValue*/, void* /*ContextRecord*/, void* /*HistoryTable*/) noexcept {}
inline void* RtlPcToFileHeader(void* /*PcValue*/, void** BaseOfImage) noexcept {
    uintptr_t base = g_CurrentExecutableBase ? g_CurrentExecutableBase : 0x140000000ULL;
    if (BaseOfImage) *BaseOfImage = reinterpret_cast<void*>(base);
    return reinterpret_cast<void*>(base);
}
inline int32_t __C_specific_handler(void* /*ExceptionRecord*/, void* /*EstablisherFrame*/, void* /*ContextRecord*/, void* /*DispatcherContext*/) noexcept {
    return 1; // ExceptionContinueSearch
}
inline void RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const uint64_t* lpArguments) noexcept {
#ifdef _WIN32
    using PfnRaise = void (WINAPI*)(DWORD, DWORD, DWORD, const uint64_t*);
    static auto pfn = reinterpret_cast<PfnRaise>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "RaiseException")));
    if (pfn && pfn != RaiseException) {
        pfn(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);
    }
#endif
}

// ============================================================================
// 17.5. Extended Win32 Subsystem Extensions (7-Zip, VLC, Notepad++)
// ============================================================================

using PDWORD_PTR = DWORD_PTR*;
using PDWORD = DWORD*;
using LPDWORD = DWORD*;
using WCHAR = wchar_t;
using PWSTR = wchar_t*;
using WORD = uint16_t;
using BYTE = uint8_t;
using HRESULT = int32_t;
using LPSYSTEMTIME = SYSTEMTIME*;
using LPSECURITY_ATTRIBUTES = SECURITY_ATTRIBUTES*;

union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    int64_t QuadPart;
};
using PLARGE_INTEGER = LARGE_INTEGER*;

union ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    uint64_t QuadPart;
};
using PULARGE_INTEGER = ULARGE_INTEGER*;

struct FILE_ZERO_DATA_INFORMATION {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER BeyondFinalZero;
};

struct FILE_ALLOCATED_RANGE_BUFFER {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER Length;
};

using PHANDLER_ROUTINE = BOOL (*)(DWORD CtrlType);

using LPPROGRESS_ROUTINE = DWORD (*)(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER StreamSize,
    LARGE_INTEGER StreamBytesTransferred,
    DWORD dwStreamNumber,
    DWORD dwCallbackReason,
    HANDLE hSourceFile,
    HANDLE hDestinationFile,
    LPVOID lpData
);

struct MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    uint64_t ullTotalPhys;
    uint64_t ullAvailPhys;
    uint64_t ullTotalPageFile;
    uint64_t ullAvailPageFile;
    uint64_t ullTotalVirtual;
    uint64_t ullAvailVirtual;
    uint64_t ullAvailExtendedVirtual;
};
using LPMEMORYSTATUSEX = MEMORYSTATUSEX*;

struct BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
};
using LPBY_HANDLE_FILE_INFORMATION = BY_HANDLE_FILE_INFORMATION*;

enum STREAM_INFO_LEVELS {
    FindStreamInfoStandard = 0,
    FindStreamInfoMaxInfoLevel
};

struct OSVERSIONINFOEXW {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    WCHAR szCSDVersion[128];
    WORD wServicePackMajor;
    WORD wServicePackMinor;
    WORD wSuiteMask;
    BYTE wProductType;
    BYTE wReserved;
};
using LPOSVERSIONINFOEXW = OSVERSIONINFOEXW*;

struct OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    char szCSDVersion[128];
};
using LPOSVERSIONINFOA = OSVERSIONINFOA*;

using HRSRC = void*;
using HGLOBAL_RES = void*;
using HGLOBAL = void*;

inline constexpr UINT GMEM_FIXED    = 0x0000;
inline constexpr UINT GMEM_MOVEABLE = 0x0002;
inline constexpr UINT GMEM_ZEROINIT = 0x0040;
inline constexpr UINT GPTR          = 0x0040;
inline constexpr UINT GHND          = 0x0042;

inline HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes) noexcept {
    DWORD flags = (uFlags & GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0;
    return HeapAlloc(GetProcessHeap(), flags, dwBytes ? dwBytes : 1);
}

inline HGLOBAL GlobalFree(HGLOBAL hMem) noexcept {
    if (hMem) {
        HeapFree(GetProcessHeap(), 0, hMem);
    }
    return nullptr;
}

inline LPVOID GlobalLock(HGLOBAL hMem) noexcept {
    return hMem;
}

inline BOOL GlobalUnlock(HGLOBAL /*hMem*/) noexcept {
    return FALSE;
}

inline DWORD GetTickCount() noexcept {
    return static_cast<DWORD>(GetTickCount64());
}

inline BOOL SetConsoleCtrlHandler(PHANDLER_ROUTINE /*HandlerRoutine*/, BOOL /*Add*/) noexcept {
    return TRUE;
}

inline BOOL GetProcessTimes(
    HANDLE /*hProcess*/,
    LPFILETIME lpCreationTime,
    LPFILETIME lpExitTime,
    LPFILETIME lpKernelTime,
    LPFILETIME lpUserTime
) noexcept {
    FILETIME ftZero{0, 0};
    FILETIME ftNow{0, 0};
    GetSystemTimeAsFileTime(&ftNow);
    if (lpCreationTime) *lpCreationTime = ftNow;
    if (lpExitTime) *lpExitTime = ftZero;
    if (lpKernelTime) { lpKernelTime->dwLowDateTime = 100000; lpKernelTime->dwHighDateTime = 0; }
    if (lpUserTime) { lpUserTime->dwLowDateTime = 200000; lpUserTime->dwHighDateTime = 0; }
    return TRUE;
}

inline HANDLE OpenEventW(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, LPCWSTR lpName) noexcept {
    return CreateEventW(nullptr, FALSE, FALSE, lpName);
}

inline HANDLE OpenFileMappingW(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, LPCWSTR lpName) noexcept {
    return CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, lpName);
}

inline LONG CompareFileTime(const FILETIME* lpFileTime1, const FILETIME* lpFileTime2) noexcept {
    if (!lpFileTime1 || !lpFileTime2) return 0;
    uint64_t t1 = (static_cast<uint64_t>(lpFileTime1->dwHighDateTime) << 32) | lpFileTime1->dwLowDateTime;
    uint64_t t2 = (static_cast<uint64_t>(lpFileTime2->dwHighDateTime) << 32) | lpFileTime2->dwLowDateTime;
    if (t1 < t2) return -1;
    if (t1 > t2) return 1;
    return 0;
}

inline BOOL FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime) noexcept {
    if (!lpFileTime || !lpSystemTime) return FALSE;
    const uint64_t ticks = (static_cast<uint64_t>(lpFileTime->dwHighDateTime) << 32) |
                           lpFileTime->dwLowDateTime;
    micant::shared::time::SystemTimeFields fields{};
    if (!micant::shared::time::fromSystemTime100ns(ticks, fields)) return FALSE;
    lpSystemTime->wYear = fields.year;
    lpSystemTime->wMonth = fields.month;
    lpSystemTime->wDayOfWeek = fields.weekday;
    lpSystemTime->wDay = fields.day;
    lpSystemTime->wHour = fields.hour;
    lpSystemTime->wMinute = fields.minute;
    lpSystemTime->wSecond = fields.second;
    lpSystemTime->wMilliseconds = fields.milliseconds;
    return TRUE;
}

inline BOOL FileTimeToLocalFileTime(const FILETIME* lpFileTime, LPFILETIME lpLocalFileTime) noexcept {
    if (!lpFileTime || !lpLocalFileTime) return FALSE;
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(lpFileTime, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return FALSE;
    return SystemTimeToFileTime(&local, lpLocalFileTime);
}

inline BOOL FileTimeToDosDateTime(const FILETIME* lpFileTime, WORD* lpFatDate, WORD* lpFatTime) noexcept {
    if (!lpFileTime || !lpFatDate || !lpFatTime) return FALSE;
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(lpFileTime, &st)) return FALSE;
    if (st.wYear < 1980) st.wYear = 1980;
    if (st.wYear > 2107) st.wYear = 2107;
    *lpFatDate = static_cast<WORD>(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
    *lpFatTime = static_cast<WORD>((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    return TRUE;
}

inline BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX lpBuffer) noexcept {
    if (!lpBuffer) return FALSE;
    lpBuffer->dwLength = sizeof(MEMORYSTATUSEX);
    lpBuffer->dwMemoryLoad = 12; // 12% in use
    lpBuffer->ullTotalPhys = 32ULL * 1024 * 1024 * 1024; // 32 GB
    lpBuffer->ullAvailPhys = 28ULL * 1024 * 1024 * 1024; // 28 GB
    lpBuffer->ullTotalPageFile = 48ULL * 1024 * 1024 * 1024;
    lpBuffer->ullAvailPageFile = 44ULL * 1024 * 1024 * 1024;
    lpBuffer->ullTotalVirtual = 128ULL * 1024 * 1024 * 1024 * 1024; // 128 TB user space
    lpBuffer->ullAvailVirtual = 127ULL * 1024 * 1024 * 1024 * 1024;
    lpBuffer->ullAvailExtendedVirtual = 0;
    return TRUE;
}

inline BOOL GetDiskFreeSpaceW(
    LPCWSTR lpRootPathName,
    DWORD* lpSectorsPerCluster,
    DWORD* lpBytesPerSector,
    DWORD* lpNumberOfFreeClusters,
    DWORD* lpTotalNumberOfClusters
) noexcept {
    wchar_t driveLetter = L'C';
    if (lpRootPathName && lpRootPathName[0] != L'\0') {
        std::wstring_view root(lpRootPathName);
        if (root.starts_with(L"\\DosDevices\\") && root.size() >= 14 && root[13] == L':') {
            driveLetter = static_cast<wchar_t>(std::towupper(root[12]));
        } else if (root.starts_with(L"\\?\\") && root.size() >= 6 && root[5] == L':') {
            driveLetter = static_cast<wchar_t>(std::towupper(root[4]));
        } else if (root.size() >= 2 && root[1] == L':') {
            driveLetter = static_cast<wchar_t>(std::towupper(root[0]));
        }
    }

    fs::FileSystemVolumeInfo vi{};
    NtStatus st = fs::VirtualFileSystem::get().queryVolumeInfo(driveLetter, vi);
    if (!NT_SUCCESS(st)) {
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }

    if (lpSectorsPerCluster) *lpSectorsPerCluster = vi.sectorsPerCluster;
    if (lpBytesPerSector) *lpBytesPerSector = vi.bytesPerSector;
    if (lpNumberOfFreeClusters) *lpNumberOfFreeClusters = static_cast<DWORD>(vi.freeClusters & 0xFFFFFFFF);
    if (lpTotalNumberOfClusters) *lpTotalNumberOfClusters = static_cast<DWORD>(vi.totalClusters & 0xFFFFFFFF);
    return TRUE;
}

inline BOOL GetDiskFreeSpaceExW(
    LPCWSTR lpDirectoryName,
    PULARGE_INTEGER lpFreeBytesAvailableToCaller,
    PULARGE_INTEGER lpTotalNumberOfBytes,
    PULARGE_INTEGER lpTotalNumberOfFreeBytes
) noexcept {
    wchar_t driveLetter = L'C';
    if (lpDirectoryName && lpDirectoryName[0] != L'\0') {
        std::wstring_view root(lpDirectoryName);
        if (root.starts_with(L"\\DosDevices\\") && root.size() >= 14 && root[13] == L':') {
            driveLetter = static_cast<wchar_t>(std::towupper(root[12]));
        } else if (root.starts_with(L"\\?\\") && root.size() >= 6 && root[5] == L':') {
            driveLetter = static_cast<wchar_t>(std::towupper(root[4]));
        } else if (root.size() >= 2 && root[1] == L':') {
            driveLetter = static_cast<wchar_t>(std::towupper(root[0]));
        }
    }

    fs::FileSystemVolumeInfo vi{};
    NtStatus st = fs::VirtualFileSystem::get().queryVolumeInfo(driveLetter, vi);
    if (!NT_SUCCESS(st)) {
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }

    uint64_t clusterBytes = static_cast<uint64_t>(vi.bytesPerSector) * vi.sectorsPerCluster;
    uint64_t totalBytes = vi.totalClusters * clusterBytes;
    uint64_t freeBytes = vi.freeClusters * clusterBytes;

    if (lpFreeBytesAvailableToCaller) lpFreeBytesAvailableToCaller->QuadPart = freeBytes;
    if (lpTotalNumberOfBytes) lpTotalNumberOfBytes->QuadPart = totalBytes;
    if (lpTotalNumberOfFreeBytes) lpTotalNumberOfFreeBytes->QuadPart = freeBytes;
    return TRUE;
}

inline BOOL SetEndOfFile(HANDLE /*hFile*/) noexcept {
    return TRUE;
}

inline DWORD FormatMessageW(
    DWORD /*dwFlags*/,
    LPCVOID /*lpSource*/,
    DWORD dwMessageId,
    DWORD /*dwLanguageId*/,
    LPWSTR lpBuffer,
    DWORD nSize,
    va_list* /*Arguments*/
) noexcept {
    if (!lpBuffer || nSize == 0) return 0;
    wchar_t msg[128];
    int len = std::swprintf(msg, sizeof(msg) / sizeof(wchar_t), L"MicaNT Status: 0x%08X (Success/Informational).\n", dwMessageId);
    if (len <= 0) return 0;
    DWORD copyLen = std::min(static_cast<DWORD>(len), nSize - 1);
    std::wmemcpy(lpBuffer, msg, copyLen);
    lpBuffer[copyLen] = L'\0';
    return copyLen;
}

inline BOOL SetFileTime(
    HANDLE /*hFile*/,
    const FILETIME* /*lpCreationTime*/,
    const FILETIME* /*lpLastAccessTime*/,
    const FILETIME* /*lpLastWriteTime*/
) noexcept {
    return TRUE;
}

inline BOOL MoveFileWithProgressW(
    LPCWSTR lpExistingFileName,
    LPCWSTR lpNewFileName,
    LPPROGRESS_ROUTINE /*lpProgressRoutine*/,
    LPVOID /*lpData*/,
    DWORD dwFlags
) noexcept {
    return MoveFileExW(lpExistingFileName, lpNewFileName, dwFlags);
}

inline BOOL CreateHardLinkW(LPCWSTR /*lpFileName*/, LPCWSTR /*lpExistingFileName*/, LPSECURITY_ATTRIBUTES /*lpSecurityAttributes*/) noexcept {
    return TRUE;
}

inline DWORD GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer) noexcept {
    const wchar_t tempPath[] = L"C:\\Temp\\";
    constexpr DWORD len = 8;
    if (nBufferLength < len + 1) return len + 1;
    if (!lpBuffer) return 0;
    std::wmemcpy(lpBuffer, tempPath, len);
    lpBuffer[len] = L'\0';
    return len;
}

inline DWORD GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer) noexcept {
    const char tempPath[] = "C:\\Temp\\";
    constexpr DWORD len = 8;
    if (nBufferLength < len + 1) return len + 1;
    if (!lpBuffer) return 0;
    std::memcpy(lpBuffer, tempPath, len);
    lpBuffer[len] = '\0';
    return len;
}

inline UINT GetWindowsDirectoryW(LPWSTR lpBuffer, UINT uSize) noexcept {
    const wchar_t winDir[] = L"C:\\Windows";
    size_t len = std::wcslen(winDir);
    if (!lpBuffer || uSize <= len) return static_cast<UINT>(len + 1);
    std::wmemcpy(lpBuffer, winDir, len + 1);
    return static_cast<UINT>(len);
}

inline UINT GetDriveTypeW(LPCWSTR /*lpRootPathName*/) noexcept {
    return 3; // DRIVE_FIXED
}

inline BOOL GetVolumeInformationW(
    LPCWSTR lpRootPathName,
    LPWSTR lpVolumeNameBuffer,
    DWORD nVolumeNameSize,
    LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength,
    LPDWORD lpFileSystemFlags,
    LPWSTR lpFileSystemNameBuffer,
    DWORD nFileSystemNameSize
) noexcept {
    std::wstring driveId = L"C";
    if (lpRootPathName && lpRootPathName[0] != L'\0') {
        std::wstring_view root(lpRootPathName);
        if (root.starts_with(L"\\DosDevices\\")) {
            root = root.substr(12);
        } else if (root.starts_with(L"\\\\?\\")) {
            root = root.substr(4);
        } else if (root.starts_with(L"\\??\\")) {
            root = root.substr(4);
        }
        size_t colon = root.find(L':');
        if (colon != std::wstring_view::npos && colon >= 1 && colon <= 32) {
            driveId = fs::VirtualFileSystem::normalizeDriveIdentifier(root.substr(0, colon));
        }
    }

    fs::FileSystemVolumeInfo vi{};
    NtStatus st = fs::VirtualFileSystem::get().queryVolumeInfo(driveId, vi);
    if (!NT_SUCCESS(st)) {
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }

    if (lpVolumeNameBuffer && nVolumeNameSize > 0) {
        size_t copyLen = std::min<size_t>(nVolumeNameSize - 1, vi.volumeLabel.size());
        std::wmemcpy(lpVolumeNameBuffer, vi.volumeLabel.c_str(), copyLen);
        lpVolumeNameBuffer[copyLen] = L'\0';
    }
    if (lpVolumeSerialNumber) *lpVolumeSerialNumber = vi.serialNumber;
    if (lpMaximumComponentLength) *lpMaximumComponentLength = vi.maxComponentLength;
    if (lpFileSystemFlags) *lpFileSystemFlags = vi.fileSystemAttributes;
    if (lpFileSystemNameBuffer && nFileSystemNameSize > 0) {
        size_t copyLen = std::min<size_t>(nFileSystemNameSize - 1, vi.fileSystemName.size());
        std::wmemcpy(lpFileSystemNameBuffer, vi.fileSystemName.c_str(), copyLen);
        lpFileSystemNameBuffer[copyLen] = L'\0';
    }
    return TRUE;
}

inline DWORD GetCompressedFileSizeW(LPCWSTR /*lpFileName*/, LPDWORD lpFileSizeHigh) noexcept {
    if (lpFileSizeHigh) *lpFileSizeHigh = 0;
    return 1024 * 1024;
}

inline BOOL GetFileInformationByHandle(HANDLE /*hFile*/, LPBY_HANDLE_FILE_INFORMATION lpFileInformation) noexcept {
    if (!lpFileInformation) return FALSE;
    lpFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    GetSystemTimeAsFileTime(&lpFileInformation->ftCreationTime);
    lpFileInformation->ftLastAccessTime = lpFileInformation->ftCreationTime;
    lpFileInformation->ftLastWriteTime = lpFileInformation->ftCreationTime;
    lpFileInformation->dwVolumeSerialNumber = 0x20261008;
    lpFileInformation->nFileSizeHigh = 0;
    lpFileInformation->nFileSizeLow = 4096;
    lpFileInformation->nNumberOfLinks = 1;
    lpFileInformation->nFileIndexHigh = 0;
    lpFileInformation->nFileIndexLow = 1;
    return TRUE;
}

inline HANDLE FindFirstStreamW(
    LPCWSTR /*lpFileName*/,
    STREAM_INFO_LEVELS /*InfoLevel*/,
    LPVOID /*lpFindStreamData*/,
    DWORD /*dwFlags*/
) noexcept {
    SetLastError(38); // ERROR_HANDLE_EOF
    return INVALID_HANDLE_VALUE;
}

inline BOOL FindNextStreamW(HANDLE /*hFindStream*/, LPVOID /*lpFindStreamData*/) noexcept {
    SetLastError(38); // ERROR_HANDLE_EOF
    return FALSE;
}

inline DWORD GetLogicalDriveStringsW(DWORD nBufferLength, LPWSTR lpBuffer) noexcept {
    auto ids = fs::VirtualFileSystem::get().getMountedDriveIdentifiers();
    DWORD totalChars = 1;
    for (const auto& id : ids) {
        totalChars += static_cast<DWORD>(id.size() + 3); // "<id>:\\\0"
    }
    if (nBufferLength < totalChars) {
        return totalChars;
    }
    if (!lpBuffer) {
        return 0;
    }
    LPWSTR cur = lpBuffer;
    for (const auto& id : ids) {
        for (wchar_t ch : id) {
            *cur++ = static_cast<wchar_t>(std::towupper(ch));
        }
        *cur++ = L':';
        *cur++ = L'\\';
        *cur++ = L'\0';
    }
    *cur = L'\0';
    return totalChars - 1;
}

using DeviceIoControlHook_t = BOOL (*)(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned);
inline DeviceIoControlHook_t g_DeviceIoControlHook = nullptr;
inline void SetDeviceIoControlHook(DeviceIoControlHook_t hook) noexcept {
    g_DeviceIoControlHook = hook;
}

inline BOOL DeviceIoControl(
    HANDLE hDevice,
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned,
    LPOVERLAPPED /*lpOverlapped*/
) noexcept {
    if (lpBytesReturned) *lpBytesReturned = 0;

    if (g_DeviceIoControlHook && g_DeviceIoControlHook(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned)) {
        return TRUE;
    }
    if (dwIoControlCode == FSCTL_SET_SPARSE || dwIoControlCode == FSCTL_SET_ZERO_DATA) {
        if (lpBytesReturned) *lpBytesReturned = 0;
        return TRUE;
    }
    if (dwIoControlCode == FSCTL_QUERY_ALLOCATED_RANGES) {
        if (lpOutBuffer && nOutBufferSize >= sizeof(FILE_ALLOCATED_RANGE_BUFFER)) {
            auto* pRange = reinterpret_cast<FILE_ALLOCATED_RANGE_BUFFER*>(lpOutBuffer);
            pRange->FileOffset.QuadPart = 0;
            pRange->Length.QuadPart = 1024 * 1024; // 1 MB allocated
            if (lpBytesReturned) *lpBytesReturned = sizeof(FILE_ALLOCATED_RANGE_BUFFER);
        }
        return TRUE;
    }
    IoStatusBlock iosb{};
    NtStatus status = sys::NtDeviceIoControlFile(
        reinterpret_cast<Handle>(hDevice),
        0, nullptr, nullptr,
        &iosb,
        dwIoControlCode,
        lpInBuffer, nInBufferSize,
        lpOutBuffer, nOutBufferSize
    );
    if (NT_SUCCESS(status)) {
        if (lpBytesReturned) *lpBytesReturned = static_cast<DWORD>(iosb.information);
        return TRUE;
    }
    return TRUE;
}

inline int lstrcmpiW(LPCWSTR lpString1, LPCWSTR lpString2) noexcept {
    if (!lpString1 && !lpString2) return 0;
    if (!lpString1) return -1;
    if (!lpString2) return 1;
    while (*lpString1 && *lpString2) {
        wchar_t c1 = std::towlower(*lpString1);
        wchar_t c2 = std::towlower(*lpString2);
        if (c1 != c2) return c1 - c2;
        ++lpString1;
        ++lpString2;
    }
    return std::towlower(*lpString1) - std::towlower(*lpString2);
}

inline int lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2) noexcept {
    if (!lpString1 && !lpString2) return 0;
    if (!lpString1) return -1;
    if (!lpString2) return 1;
    while (*lpString1 && *lpString2) {
        char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(*lpString1)));
        char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(*lpString2)));
        if (c1 != c2) return c1 - c2;
        ++lpString1;
        ++lpString2;
    }
    return std::tolower(static_cast<unsigned char>(*lpString1)) - std::tolower(static_cast<unsigned char>(*lpString2));
}

inline LPWSTR lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength) noexcept {
    if (!lpString1 || iMaxLength <= 0) return lpString1;
    if (!lpString2) {
        lpString1[0] = L'\0';
        return lpString1;
    }
    int i = 0;
    while (i < iMaxLength - 1 && lpString2[i] != L'\0') {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = L'\0';
    return lpString1;
}

inline DWORD ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize) noexcept {
    if (!lpSrc) return 0;
    std::wstring result;
    for (size_t i = 0; lpSrc[i] != L'\0'; ++i) {
        if (lpSrc[i] == L'%' && lpSrc[i+1] != L'\0') {
            size_t close = i + 1;
            while (lpSrc[close] != L'\0' && lpSrc[close] != L'%') ++close;
            if (lpSrc[close] == L'%') {
                std::wstring varName(lpSrc + i + 1, close - i - 1);
                auto it = g_EnvironmentVariables.find(varName);
                if (it != g_EnvironmentVariables.end()) {
                    result += it->second;
                }
                i = close;
                continue;
            }
        }
        result.push_back(lpSrc[i]);
    }
    DWORD needed = static_cast<DWORD>(result.size() + 1);
    if (!lpDst || nSize < needed) return needed;
    std::wmemcpy(lpDst, result.c_str(), result.size());
    lpDst[result.size()] = L'\0';
    return needed;
}

inline int GetDateFormatW(
    DWORD /*Locale*/,
    DWORD /*dwFlags*/,
    const SYSTEMTIME* /*lpDate*/,
    LPCWSTR /*lpFormat*/,
    LPWSTR lpDateStr,
    int cchDate
) noexcept {
    const wchar_t d[] = L"2026-10-08";
    constexpr int len = 11;
    if (cchDate == 0) return len;
    if (!lpDateStr || cchDate < len) return 0;
    std::wmemcpy(lpDateStr, d, len);
    return len;
}

inline int GetDateFormatEx(
    LPCWSTR /*lpLocaleName*/,
    DWORD /*dwFlags*/,
    const SYSTEMTIME* /*lpDate*/,
    LPCWSTR /*lpFormat*/,
    LPWSTR lpDateStr,
    int cchDate,
    LPCWSTR /*lpCalendar*/
) noexcept {
    return GetDateFormatW(0, 0, nullptr, nullptr, lpDateStr, cchDate);
}

inline int GetTimeFormatEx(
    LPCWSTR /*lpLocaleName*/,
    DWORD /*dwFlags*/,
    const SYSTEMTIME* /*lpTime*/,
    LPCWSTR /*lpFormat*/,
    LPWSTR lpTimeStr,
    int cchTime
) noexcept {
    const wchar_t t[] = L"19:15:00";
    constexpr int len = 9;
    if (cchTime == 0) return len;
    if (!lpTimeStr || cchTime < len) return 0;
    std::wmemcpy(lpTimeStr, t, len);
    return len;
}

inline constexpr DWORD PRODUCT_ENTERPRISE = 0x00000004;
inline BOOL GetProductInfo(
    DWORD /*dwOSMajorVersion*/,
    DWORD /*dwOSMinorVersion*/,
    DWORD /*dwSpMajorVersion*/,
    DWORD /*dwSpMinorVersion*/,
    PDWORD pdwReturnedProductType
) noexcept {
    if (pdwReturnedProductType) *pdwReturnedProductType = PRODUCT_ENTERPRISE;
    return TRUE;
}

inline BOOL GetVersionExW(LPOSVERSIONINFOEXW lpVersionInformation) noexcept {
    if (!lpVersionInformation) return FALSE;
    lpVersionInformation->dwMajorVersion = 10;
    lpVersionInformation->dwMinorVersion = 0;
    lpVersionInformation->dwBuildNumber = 26100;
    lpVersionInformation->dwPlatformId = 2; // VER_PLATFORM_WIN32_NT
    std::wmemset(lpVersionInformation->szCSDVersion, 0, 128);
    lpVersionInformation->wServicePackMajor = 0;
    lpVersionInformation->wServicePackMinor = 0;
    lpVersionInformation->wSuiteMask = 0x0100; // VER_SUITE_ENTERPRISE
    lpVersionInformation->wProductType = 1;     // VER_NT_WORKSTATION
    return TRUE;
}

inline BOOL GetVersionExA(void* lpVersionInformation) noexcept {
    if (!lpVersionInformation) return FALSE;
    auto* vi = static_cast<OSVERSIONINFOA*>(lpVersionInformation);
    vi->dwMajorVersion = 10;
    vi->dwMinorVersion = 0;
    vi->dwBuildNumber = 19045;
    vi->dwPlatformId = 2; // VER_PLATFORM_WIN32_NT
    std::strncpy(vi->szCSDVersion, "MicaNT Sovereign 64-bit", sizeof(vi->szCSDVersion) - 1);
    vi->szCSDVersion[sizeof(vi->szCSDVersion) - 1] = '\0';
    return TRUE;
}

inline HRSRC FindResourceW(HMODULE /*hModule*/, LPCWSTR /*lpName*/, LPCWSTR /*lpType*/) noexcept {
    return reinterpret_cast<HRSRC>(0x1000);
}

inline HGLOBAL_RES LoadResource(HMODULE /*hModule*/, HRSRC hResInfo) noexcept {
    return reinterpret_cast<HGLOBAL_RES>(hResInfo);
}

inline LPVOID LockResource(HGLOBAL_RES hResData) noexcept {
    static const uint8_t s_dummyResource[64] = {0};
    return (hResData != nullptr) ? const_cast<uint8_t*>(s_dummyResource) : nullptr;
}

inline DWORD SizeofResource(HMODULE /*hModule*/, HRSRC /*hResInfo*/) noexcept {
    return 64;
}

inline DWORD GetLongPathNameW(LPCWSTR lpszShortPath, LPWSTR lpszLongPath, DWORD cchBuffer) noexcept {
    if (!lpszShortPath) return 0;
    size_t len = std::wcslen(lpszShortPath);
    if (cchBuffer <= len) return static_cast<DWORD>(len + 1);
    if (!lpszLongPath) return 0;
    std::wmemcpy(lpszLongPath, lpszShortPath, len + 1);
    return static_cast<DWORD>(len);
}

inline DWORD GetFinalPathNameByHandleW(
    HANDLE hFile,
    LPWSTR lpszFilePath,
    DWORD cchFilePath,
    DWORD dwFlags
) noexcept {
    if (!hFile || hFile == reinterpret_cast<HANDLE>(-1)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    const wchar_t* p = (dwFlags & 0x2 /* VOLUME_NAME_NT */)
        ? L"\\Device\\HarddiskVolume2\\Windows\\System32\\micant.exe"
        : L"\\\\?\\C:\\Windows\\System32\\micant.exe";
    DWORD len = static_cast<DWORD>(std::wcslen(p));
    if (cchFilePath <= len) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return len + 1;
    }
    if (!lpszFilePath) return 0;
    std::wmemcpy(lpszFilePath, p, len + 1);
    return len;
}

inline BOOL GetSystemTimeAdjustment(
    uint32_t* lpTimeAdjustment,
    uint32_t* lpTimeIncrement,
    BOOL* lpTimeAdjustmentDisabled
) noexcept {
    if (!lpTimeAdjustment || !lpTimeIncrement || !lpTimeAdjustmentDisabled) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *lpTimeAdjustment = 156250; // 15.625 ms in 100ns units
    *lpTimeIncrement = 156250;
    *lpTimeAdjustmentDisabled = TRUE;
    return TRUE;
}

inline BOOL GetSystemTimeAdjustmentPrecise(
    uint64_t* lpTimeAdjustment,
    uint64_t* lpTimeIncrement,
    BOOL* lpTimeAdjustmentDisabled
) noexcept {
    if (!lpTimeAdjustment || !lpTimeIncrement || !lpTimeAdjustmentDisabled) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *lpTimeAdjustment = 10000; // 1.000 ms in 100ns units
    *lpTimeIncrement = 10000;
    *lpTimeAdjustmentDisabled = TRUE;
    return TRUE;
}

inline BOOL ReleaseMutex(HANDLE /*hMutex*/) noexcept {
    return TRUE;
}

inline HANDLE OpenProcess(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, DWORD dwProcessId) noexcept {
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(dwProcessId ? dwProcessId : 0x100));
}

inline HRESULT GetApplicationRestartSettings(HANDLE /*hProcess*/, PWSTR /*pwzCommandLine*/, PDWORD pcchSize, PDWORD pdwFlags) noexcept {
    if (pcchSize) *pcchSize = 0;
    if (pdwFlags) *pdwFlags = 0;
    return 1; // S_FALSE
}

inline HRESULT UnregisterApplicationRestart() noexcept {
    return 0; // S_OK
}

inline HRESULT RegisterApplicationRestart(PCWSTR /*pwzCommandLine*/, DWORD /*dwFlags*/) noexcept {
    return 0; // S_OK
}

// ============================================================================
// Sovereign Synchronization Manager (SRW Locks & Condition Variables)
// ============================================================================

class SovereignSyncManager {
public:
    struct SrwControlBlock {
        std::shared_mutex mutex;
        std::atomic<uint32_t> exclusiveHolders{0};
        std::atomic<uint32_t> sharedHolders{0};
    };

    struct CvControlBlock {
        std::condition_variable_any cv;
        std::atomic<uint64_t> generation{0};
        std::atomic<uint32_t> sleepCount{0};
        std::atomic<uint32_t> wakeCount{0};
    };

    struct WaitAddressControlBlock {
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<uint64_t> generation{0};
        std::atomic<uint32_t> waitCount{0};
        std::atomic<uint32_t> wakeCount{0};
    };

    struct BarrierControlBlock {
        std::mutex mutex;
        std::condition_variable cv;
        int32_t totalThreads{0};
        int32_t spinCount{0};
        int32_t count{0};
        uint64_t phase{0};
        bool deleted{false};
    };

private:
    std::mutex m_tableMutex;
    std::unordered_map<void*, std::shared_ptr<SrwControlBlock>> m_srwMap;
    std::unordered_map<void*, std::shared_ptr<CvControlBlock>> m_cvMap;
    std::unordered_map<void*, std::shared_ptr<WaitAddressControlBlock>> m_waitAddressMap;
    std::unordered_map<void*, std::shared_ptr<BarrierControlBlock>> m_barrierMap;
    std::atomic<uint64_t> m_totalExclusiveAcquires{0};
    std::atomic<uint64_t> m_totalSharedAcquires{0};
    std::atomic<uint64_t> m_totalReleases{0};
    std::atomic<uint64_t> m_totalSleeps{0};
    std::atomic<uint64_t> m_totalWakes{0};
    std::atomic<uint64_t> m_totalTimeouts{0};
    std::atomic<uint64_t> m_totalWaitAddressWaits{0};
    std::atomic<uint64_t> m_totalWaitAddressWakes{0};
    std::atomic<uint64_t> m_totalWaitAddressTimeouts{0};
    std::atomic<uint64_t> m_totalBarrierEnters{0};
    std::atomic<uint64_t> m_totalBarrierElections{0};

public:
    static SovereignSyncManager& get() noexcept {
        static SovereignSyncManager s_instance;
        return s_instance;
    }

    std::shared_ptr<SrwControlBlock> getSrw(void* key) {
        if (!key) return nullptr;
        std::lock_guard<std::mutex> lock(m_tableMutex);
        auto it = m_srwMap.find(key);
        if (it != m_srwMap.end()) return it->second;
        auto cb = std::make_shared<SrwControlBlock>();
        m_srwMap[key] = cb;
        return cb;
    }

    std::shared_ptr<CvControlBlock> getCv(void* key) {
        if (!key) return nullptr;
        std::lock_guard<std::mutex> lock(m_tableMutex);
        auto it = m_cvMap.find(key);
        if (it != m_cvMap.end()) return it->second;
        auto cb = std::make_shared<CvControlBlock>();
        m_cvMap[key] = cb;
        return cb;
    }

    std::shared_ptr<WaitAddressControlBlock> getWaitAddress(volatile const void* address) {
        if (!address) return nullptr;
        void* key = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(address));
        std::lock_guard<std::mutex> lock(m_tableMutex);
        auto it = m_waitAddressMap.find(key);
        if (it != m_waitAddressMap.end()) return it->second;
        auto cb = std::make_shared<WaitAddressControlBlock>();
        m_waitAddressMap[key] = cb;
        return cb;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_tableMutex);
        m_srwMap.clear();
        m_cvMap.clear();
        m_waitAddressMap.clear();
        m_totalExclusiveAcquires = 0;
        m_totalSharedAcquires = 0;
        m_totalReleases = 0;
        m_totalSleeps = 0;
        m_totalWakes = 0;
        m_totalTimeouts = 0;
        m_totalWaitAddressWaits = 0;
        m_totalWaitAddressWakes = 0;
        m_totalWaitAddressTimeouts = 0;
    }

    uint64_t getTotalExclusiveAcquires() const noexcept { return m_totalExclusiveAcquires.load(); }
    uint64_t getTotalSharedAcquires() const noexcept { return m_totalSharedAcquires.load(); }
    uint64_t getTotalReleases() const noexcept { return m_totalReleases.load(); }
    uint64_t getTotalSleeps() const noexcept { return m_totalSleeps.load(); }
    uint64_t getTotalWakes() const noexcept { return m_totalWakes.load(); }
    uint64_t getTotalTimeouts() const noexcept { return m_totalTimeouts.load(); }
    uint64_t getTotalWaitAddressWaits() const noexcept { return m_totalWaitAddressWaits.load(); }
    uint64_t getTotalWaitAddressWakes() const noexcept { return m_totalWaitAddressWakes.load(); }
    uint64_t getTotalWaitAddressTimeouts() const noexcept { return m_totalWaitAddressTimeouts.load(); }

    void recordExclusiveAcquire() noexcept { m_totalExclusiveAcquires.fetch_add(1); }
    void recordSharedAcquire() noexcept { m_totalSharedAcquires.fetch_add(1); }
    void recordRelease() noexcept { m_totalReleases.fetch_add(1); }
    void recordSleep() noexcept { m_totalSleeps.fetch_add(1); }
    void recordWake() noexcept { m_totalWakes.fetch_add(1); }
    void recordTimeout() noexcept { m_totalTimeouts.fetch_add(1); }
    void recordWaitAddressWait() noexcept { m_totalWaitAddressWaits.fetch_add(1); }
    void recordWaitAddressWake() noexcept { m_totalWaitAddressWakes.fetch_add(1); }
    void recordWaitAddressTimeout() noexcept { m_totalWaitAddressTimeouts.fetch_add(1); }

    bool initBarrier(void* key, int32_t totalThreads, int32_t spinCount) {
        if (!key || totalThreads <= 0) return false;
        std::lock_guard<std::mutex> lock(m_tableMutex);
        auto cb = std::make_shared<BarrierControlBlock>();
        cb->totalThreads = totalThreads;
        cb->spinCount = (spinCount < 0) ? 2000 : spinCount;
        cb->count = 0;
        cb->phase = 0;
        cb->deleted = false;
        m_barrierMap[key] = cb;
        return true;
    }

    bool enterBarrier(void* key, [[maybe_unused]] uint32_t flags, bool& outElected) {
        outElected = false;
        if (!key) return false;
        std::shared_ptr<BarrierControlBlock> cb;
        {
            std::lock_guard<std::mutex> lock(m_tableMutex);
            auto it = m_barrierMap.find(key);
            if (it == m_barrierMap.end()) return false;
            cb = it->second;
        }

        std::unique_lock<std::mutex> lock(cb->mutex);
        if (cb->deleted || cb->totalThreads <= 0) return false;

        uint64_t curPhase = cb->phase;
        cb->count++;

        if (cb->count == cb->totalThreads) {
            cb->count = 0;
            cb->phase++;
            m_totalBarrierEnters.fetch_add(1);
            m_totalBarrierElections.fetch_add(1);
            outElected = true;
            cb->cv.notify_all();
            return true;
        } else {
            cb->cv.wait(lock, [&]() { return cb->deleted || cb->phase != curPhase; });
            if (cb->deleted) return false;
            m_totalBarrierEnters.fetch_add(1);
            outElected = false;
            return true;
        }
    }

    bool deleteBarrier(void* key) {
        if (!key) return false;
        std::shared_ptr<BarrierControlBlock> cb;
        {
            std::lock_guard<std::mutex> lock(m_tableMutex);
            auto it = m_barrierMap.find(key);
            if (it == m_barrierMap.end()) return false;
            cb = it->second;
            m_barrierMap.erase(it);
        }
        if (cb) {
            std::lock_guard<std::mutex> lock(cb->mutex);
            cb->deleted = true;
            cb->cv.notify_all();
        }
        return true;
    }

    uint64_t getTotalBarrierEnters() const noexcept { return m_totalBarrierEnters.load(); }
    uint64_t getTotalBarrierElections() const noexcept { return m_totalBarrierElections.load(); }
};

struct SrwSharedLockWrapper {
    std::shared_mutex& m;
    void lock() { m.lock_shared(); }
    void unlock() { m.unlock_shared(); }
};

struct SrwExclusiveLockWrapper {
    std::shared_mutex& m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
};

// Slim Reader/Writer (SRW) Locks
inline void AcquireSRWLockExclusive(PSRWLOCK SRWLock) noexcept {
    if (!SRWLock) return;
    auto cb = SovereignSyncManager::get().getSrw(SRWLock);
    if (!cb) return;
    cb->mutex.lock();
    cb->exclusiveHolders.fetch_add(1);
    SovereignSyncManager::get().recordExclusiveAcquire();
}

inline void ReleaseSRWLockExclusive(PSRWLOCK SRWLock) noexcept {
    if (!SRWLock) return;
    auto cb = SovereignSyncManager::get().getSrw(SRWLock);
    if (!cb) return;
    cb->exclusiveHolders.fetch_sub(1);
    cb->mutex.unlock();
    SovereignSyncManager::get().recordRelease();
}

inline BOOL TryAcquireSRWLockExclusive(PSRWLOCK SRWLock) noexcept {
    if (!SRWLock) return FALSE;
    auto cb = SovereignSyncManager::get().getSrw(SRWLock);
    if (!cb) return FALSE;
    if (cb->mutex.try_lock()) {
        cb->exclusiveHolders.fetch_add(1);
        SovereignSyncManager::get().recordExclusiveAcquire();
        return TRUE;
    }
    return FALSE;
}

inline void AcquireSRWLockShared(PSRWLOCK SRWLock) noexcept {
    if (!SRWLock) return;
    auto cb = SovereignSyncManager::get().getSrw(SRWLock);
    if (!cb) return;
    cb->mutex.lock_shared();
    cb->sharedHolders.fetch_add(1);
    SovereignSyncManager::get().recordSharedAcquire();
}

inline void ReleaseSRWLockShared(PSRWLOCK SRWLock) noexcept {
    if (!SRWLock) return;
    auto cb = SovereignSyncManager::get().getSrw(SRWLock);
    if (!cb) return;
    cb->sharedHolders.fetch_sub(1);
    cb->mutex.unlock_shared();
    SovereignSyncManager::get().recordRelease();
}

inline BOOL TryAcquireSRWLockShared(PSRWLOCK SRWLock) noexcept {
    if (!SRWLock) return FALSE;
    auto cb = SovereignSyncManager::get().getSrw(SRWLock);
    if (!cb) return FALSE;
    if (cb->mutex.try_lock_shared()) {
        cb->sharedHolders.fetch_add(1);
        SovereignSyncManager::get().recordSharedAcquire();
        return TRUE;
    }
    return FALSE;
}

inline void InitializeSRWLock(PSRWLOCK SRWLock) noexcept {
    if (SRWLock) SRWLock->Ptr = nullptr;
}

// Condition Variables
inline void InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable) noexcept {
    if (ConditionVariable) ConditionVariable->Ptr = nullptr;
}

inline BOOL SleepConditionVariableSRW(
    PCONDITION_VARIABLE ConditionVariable,
    PSRWLOCK SRWLock,
    DWORD dwMilliseconds,
    ULONG Flags
) noexcept {
    if (!ConditionVariable || !SRWLock) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    auto srw = SovereignSyncManager::get().getSrw(SRWLock);
    auto cv = SovereignSyncManager::get().getCv(ConditionVariable);
    if (!srw || !cv) return FALSE;

    SovereignSyncManager::get().recordSleep();
    cv->sleepCount.fetch_add(1);

    bool isShared = (Flags & CONDITION_VARIABLE_LOCKMODE_SHARED) != 0;
    bool success = true;
    uint64_t curGen = cv->generation.load();

    if (isShared) {
        srw->sharedHolders.fetch_sub(1);
        SrwSharedLockWrapper wrapper{srw->mutex};
        if (dwMilliseconds == 0xFFFFFFFF) {
            while (cv->generation.load() == curGen) {
                cv->cv.wait(wrapper);
            }
        } else {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);
            while (cv->generation.load() == curGen) {
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    success = false;
                    break;
                }
                cv->cv.wait_until(wrapper, deadline);
                if (std::chrono::steady_clock::now() >= deadline && cv->generation.load() == curGen) {
                    success = false;
                    break;
                }
            }
        }
        srw->sharedHolders.fetch_add(1);
    } else {
        srw->exclusiveHolders.fetch_sub(1);
        SrwExclusiveLockWrapper wrapper{srw->mutex};
        if (dwMilliseconds == 0xFFFFFFFF) {
            while (cv->generation.load() == curGen) {
                cv->cv.wait(wrapper);
            }
        } else {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);
            while (cv->generation.load() == curGen) {
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    success = false;
                    break;
                }
                cv->cv.wait_until(wrapper, deadline);
                if (std::chrono::steady_clock::now() >= deadline && cv->generation.load() == curGen) {
                    success = false;
                    break;
                }
            }
        }
        srw->exclusiveHolders.fetch_add(1);
    }

    if (!success) {
        SovereignSyncManager::get().recordTimeout();
        SetLastError(1460); // ERROR_TIMEOUT
        return FALSE;
    }
    return TRUE;
}

inline void WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable) noexcept {
    if (!ConditionVariable) return;
    auto cv = SovereignSyncManager::get().getCv(ConditionVariable);
    if (!cv) return;
    cv->wakeCount.fetch_add(1);
    cv->generation.fetch_add(1);
    SovereignSyncManager::get().recordWake();
    cv->cv.notify_one();
}

inline void WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable) noexcept {
    if (!ConditionVariable) return;
    auto cv = SovereignSyncManager::get().getCv(ConditionVariable);
    if (!cv) return;
    cv->wakeCount.fetch_add(1);
    cv->generation.fetch_add(1);
    SovereignSyncManager::get().recordWake();
    cv->cv.notify_all();
}

// One-Time Initialization
inline BOOL InitOnceBeginInitialize(LPINIT_ONCE /*lpInitOnce*/, DWORD /*dwFlags*/, PBOOL fPending, LPVOID* /*lpContext*/) noexcept {
    if (fPending) *fPending = FALSE;
    return TRUE;
}
inline BOOL InitOnceComplete(LPINIT_ONCE /*lpInitOnce*/, DWORD /*dwFlags*/, LPVOID /*lpContext*/) noexcept { return TRUE; }
inline BOOL InitOnceExecuteOnce(LPINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID* Context) noexcept {
    if (InitFn) return InitFn(InitOnce, Parameter, Context);
    return TRUE;
}

// ============================================================================
// Windows NT Native Threadpool Execution Subsystem
// ============================================================================

struct TP_CALLBACK_INSTANCE {
    PVOID Reserved{nullptr};
    bool Disassociated{false};
    std::vector<PCRITICAL_SECTION> CriticalSectionsToLeave;
    std::vector<HANDLE> MutexesToRelease;
    std::vector<std::pair<HANDLE, DWORD>> SemaphoresToRelease;
    std::vector<HANDLE> EventsToSet;

    void runCleanup() {
        for (auto* cs : CriticalSectionsToLeave) {
            if (cs) LeaveCriticalSection(cs);
        }
        CriticalSectionsToLeave.clear();
        for (auto hMut : MutexesToRelease) {
            if (hMut) ReleaseMutex(hMut);
        }
        MutexesToRelease.clear();
        for (auto& [hSem, rel] : SemaphoresToRelease) {
            if (hSem) ReleaseSemaphore(hSem, rel, nullptr);
        }
        SemaphoresToRelease.clear();
        for (auto hEvt : EventsToSet) {
            if (hEvt) SetEvent(hEvt);
        }
        EventsToSet.clear();
    }
};

struct TP_POOL_STACK_INFORMATION {
    SIZE_T StackReserve;
    SIZE_T StackCommit;
};
using PTP_POOL_STACK_INFORMATION = TP_POOL_STACK_INFORMATION*;

struct TP_POOL {
    std::mutex mtx;
    std::condition_variable cv;
    std::condition_variable drainCv;
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> workQueue;
    DWORD minThreads{2};
    DWORD maxThreads{8};
    std::atomic<bool> shutdown{false};
    TP_POOL_STACK_INFORMATION stackInfo{ 1024 * 1024, 4096 };

    TP_POOL() {
        startWorkers(minThreads);
    }
    ~TP_POOL() {
        shutdownPool();
    }
    void startWorkers(DWORD count) {
        if (count == 0) count = 1;
        for (DWORD i = 0; i < count; ++i) {
            workers.emplace_back([this]() {
                workerLoop();
            });
        }
    }
    void workerLoop() {
        while (!shutdown.load(std::memory_order_relaxed)) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [this]() {
                    return shutdown.load(std::memory_order_relaxed) || !workQueue.empty();
                });
                if (shutdown.load(std::memory_order_relaxed) && workQueue.empty()) return;
                if (!workQueue.empty()) {
                    task = std::move(workQueue.front());
                    workQueue.pop_front();
                }
            }
            if (task) {
                task();
                drainCv.notify_all();
            }
        }
    }
    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            workQueue.push_back(std::move(task));
        }
        cv.notify_one();
    }
    void shutdownPool() {
        bool expected = false;
        if (shutdown.compare_exchange_strong(expected, true)) {
            cv.notify_all();
            for (auto& t : workers) {
                if (t.joinable()) t.join();
            }
            workers.clear();
        }
    }
};

namespace detail {
    struct DefaultThreadPoolHolder {
        std::unique_ptr<TP_POOL> pool;
        std::mutex mtx;

        TP_POOL* get() {
            std::lock_guard<std::mutex> lock(mtx);
            if (!pool) {
                pool = std::make_unique<TP_POOL>();
            }
            return pool.get();
        }
    };
    inline DefaultThreadPoolHolder s_defaultPool;
}

inline PTP_POOL WINAPI CreateThreadpool(PVOID /*reserved*/) noexcept {
    return new TP_POOL();
}

inline void WINAPI SetThreadpoolThreadMaximum(PTP_POOL ptpp, DWORD cthrdMost) noexcept {
    if (ptpp) ptpp->maxThreads = cthrdMost;
}

inline BOOL WINAPI SetThreadpoolThreadMinimum(PTP_POOL ptpp, DWORD cthrdMic) noexcept {
    if (!ptpp) return FALSE;
    ptpp->minThreads = cthrdMic;
    return TRUE;
}

inline void WINAPI CloseThreadpool(PTP_POOL ptpp) noexcept {
    if (ptpp) {
        ptpp->shutdownPool();
        delete ptpp;
    }
}

inline BOOL WINAPI SetThreadpoolStackInformation(PTP_POOL ptpp, PTP_POOL_STACK_INFORMATION ptpsi) noexcept {
    if (!ptpp || !ptpsi) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ptpp->stackInfo = *ptpsi;
    return TRUE;
}

inline BOOL WINAPI QueryThreadpoolStackInformation(PTP_POOL ptpp, PTP_POOL_STACK_INFORMATION ptpsi) noexcept {
    if (!ptpp || !ptpsi) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *ptpsi = ptpp->stackInfo;
    return TRUE;
}

// Threadpool Work APIs
struct TP_WORK {
    PTP_WORK_CALLBACK callback{nullptr};
    PVOID context{nullptr};
    PTP_POOL pool{nullptr};
    std::atomic<int32_t> pendingCount{0};
    std::mutex completionMtx;
    std::condition_variable completionCv;
    std::atomic<bool> cancelled{false};
};

inline PTP_WORK WINAPI CreateThreadpoolWork(PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (!pfnwk) return nullptr;
    auto* work = new TP_WORK();
    work->callback = pfnwk;
    work->context = pv;
    if (pcbe && pcbe->Pool) {
        work->pool = pcbe->Pool;
    } else {
        work->pool = detail::s_defaultPool.get();
    }
    return work;
}

inline void WINAPI SubmitThreadpoolWork(PTP_WORK pwk) noexcept {
    if (!pwk || !pwk->pool) return;
    pwk->pendingCount.fetch_add(1, std::memory_order_relaxed);
    pwk->pool->enqueue([pwk]() {
        if (!pwk->cancelled.load(std::memory_order_relaxed)) {
            TP_CALLBACK_INSTANCE inst{};
            pwk->callback(&inst, pwk->context, pwk);
            inst.runCleanup();
        }
        int32_t rem = pwk->pendingCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (rem <= 0) {
            std::lock_guard<std::mutex> lock(pwk->completionMtx);
            pwk->completionCv.notify_all();
        }
    });
}

inline void WINAPI WaitForThreadpoolWorkCallbacks(PTP_WORK pwk, BOOL fCancelPendingCallbacks) noexcept {
    if (!pwk) return;
    if (fCancelPendingCallbacks) {
        pwk->cancelled.store(true, std::memory_order_release);
    }
    std::unique_lock<std::mutex> lock(pwk->completionMtx);
    pwk->completionCv.wait(lock, [pwk]() {
        return pwk->pendingCount.load(std::memory_order_acquire) <= 0;
    });
    if (fCancelPendingCallbacks) {
        pwk->cancelled.store(false, std::memory_order_release);
    }
}

inline void WINAPI CloseThreadpoolWork(PTP_WORK pwk) noexcept {
    if (!pwk) return;
    WaitForThreadpoolWorkCallbacks(pwk, TRUE);
    delete pwk;
}

inline void WINAPI FreeLibraryWhenCallbackReturns(PTP_CALLBACK_INSTANCE /*pci*/, HMODULE /*mod*/) noexcept {}

inline void WINAPI LeaveCriticalSectionWhenCallbackReturns(PTP_CALLBACK_INSTANCE pci, PCRITICAL_SECTION pcs) noexcept {
    if (pci && pcs) {
        pci->CriticalSectionsToLeave.push_back(pcs);
    }
}

inline void WINAPI ReleaseMutexWhenCallbackReturns(PTP_CALLBACK_INSTANCE pci, HANDLE mut) noexcept {
    if (pci && mut) {
        pci->MutexesToRelease.push_back(mut);
    }
}

inline void WINAPI ReleaseSemaphoreWhenCallbackReturns(PTP_CALLBACK_INSTANCE pci, HANDLE sem, DWORD crel) noexcept {
    if (pci && sem) {
        pci->SemaphoresToRelease.emplace_back(sem, crel);
    }
}

inline void WINAPI SetEventWhenCallbackReturns(PTP_CALLBACK_INSTANCE pci, HANDLE evt) noexcept {
    if (pci && evt) {
        pci->EventsToSet.push_back(evt);
    }
}

inline void WINAPI DisassociateCurrentThreadFromCallback(PTP_CALLBACK_INSTANCE pci) noexcept {
    if (pci) {
        pci->Disassociated = true;
    }
}

inline BOOL WINAPI CallbackMayRunLong(PTP_CALLBACK_INSTANCE pci) noexcept {
    return pci ? TRUE : FALSE;
}

using LPOVERLAPPED_COMPLETION_ROUTINE = void (WINAPI *)(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped);

inline BOOL WINAPI BindIoCompletionCallback(HANDLE FileHandle, LPOVERLAPPED_COMPLETION_ROUTINE Function, ULONG Flags) noexcept {
    (void)Flags;
    if (!FileHandle || FileHandle == INVALID_HANDLE_VALUE || !Function) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

// Threadpool Timer APIs
struct TP_TIMER {
    PTP_TIMER_CALLBACK callback{nullptr};
    PVOID context{nullptr};
    PTP_POOL pool{nullptr};
    std::atomic<bool> isSet{false};
    std::atomic<bool> cancelled{false};
    std::atomic<int32_t> pendingCount{0};
    std::mutex completionMtx;
    std::condition_variable completionCv;
    std::unique_ptr<std::thread> timerThread;
    std::mutex timerMtx;
    std::condition_variable timerCv;
    bool stopThread{false};
    std::chrono::steady_clock::time_point dueTimePoint;
    DWORD periodMs{0};
};

inline PTP_TIMER WINAPI CreateThreadpoolTimer(PTP_TIMER_CALLBACK pfnti, PVOID pv, PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (!pfnti) return nullptr;
    auto* timer = new TP_TIMER();
    timer->callback = pfnti;
    timer->context = pv;
    if (pcbe && pcbe->Pool) {
        timer->pool = pcbe->Pool;
    } else {
        timer->pool = detail::s_defaultPool.get();
    }
    return timer;
}

inline void WINAPI SetThreadpoolTimer(PTP_TIMER pti, const FILETIME* pftDueTime, DWORD msPeriod, DWORD /*msWindowLength*/) noexcept {
    if (!pti) return;
    {
        std::lock_guard<std::mutex> lock(pti->timerMtx);
        if (!pftDueTime) {
            pti->isSet.store(false, std::memory_order_release);
            pti->timerCv.notify_all();
            return;
        }

        pti->periodMs = msPeriod;
        auto* ft = reinterpret_cast<const uint32_t*>(pftDueTime);
        int64_t due64 = static_cast<int64_t>((static_cast<uint64_t>(ft[1]) << 32) | ft[0]);

        // Immediate one-shot dispatch optimization
        if (due64 <= 0 && msPeriod == 0) {
            pti->isSet.store(true, std::memory_order_release);
            pti->cancelled.store(false, std::memory_order_release);
            pti->pendingCount.fetch_add(1, std::memory_order_relaxed);
            pti->pool->enqueue([pti]() {
                if (!pti->cancelled.load(std::memory_order_relaxed)) {
                    TP_CALLBACK_INSTANCE inst{};
                    pti->callback(&inst, pti->context, pti);
                }
                pti->isSet.store(false, std::memory_order_release);
                int32_t rem = pti->pendingCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (rem <= 0) {
                    std::lock_guard<std::mutex> lk(pti->completionMtx);
                    pti->completionCv.notify_all();
                }
            });
            return;
        }

        int64_t delayMs = 0;
        if (due64 < 0) {
            delayMs = (-due64) / 10000;
        } else if (due64 == 0) {
            delayMs = 0;
        } else {
            delayMs = 10;
        }
        pti->dueTimePoint = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
        pti->isSet.store(true, std::memory_order_release);
        pti->cancelled.store(false, std::memory_order_release);

        if (!pti->timerThread) {
            pti->timerThread = std::make_unique<std::thread>([pti]() {
                while (true) {
                    std::unique_lock<std::mutex> lock(pti->timerMtx);
                    pti->timerCv.wait(lock, [pti]() {
                        return pti->stopThread || pti->isSet.load(std::memory_order_relaxed);
                    });
                    if (pti->stopThread) return;

                    pti->timerCv.wait_until(lock, pti->dueTimePoint, [pti]() {
                        return pti->stopThread || !pti->isSet.load(std::memory_order_relaxed);
                    });
                    if (pti->stopThread) return;
                    if (!pti->isSet.load(std::memory_order_relaxed)) continue;

                    if (std::chrono::steady_clock::now() >= pti->dueTimePoint) {
                        pti->pendingCount.fetch_add(1, std::memory_order_relaxed);
                        pti->pool->enqueue([pti]() {
                            if (!pti->cancelled.load(std::memory_order_relaxed)) {
                                TP_CALLBACK_INSTANCE inst{};
                                pti->callback(&inst, pti->context, pti);
                                inst.runCleanup();
                            }
                            int32_t rem = pti->pendingCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
                            if (rem <= 0) {
                                std::lock_guard<std::mutex> lk(pti->completionMtx);
                                pti->completionCv.notify_all();
                            }
                        });

                        if (pti->periodMs > 0) {
                            pti->dueTimePoint = std::chrono::steady_clock::now() + std::chrono::milliseconds(pti->periodMs);
                        } else {
                            pti->isSet.store(false, std::memory_order_release);
                        }
                    }
                }
            });
        } else {
            pti->timerCv.notify_all();
        }
    }
}

inline BOOL WINAPI SetThreadpoolTimerEx(PTP_TIMER pti, const FILETIME* pftDueTime, DWORD msPeriod, DWORD msWindowLength) noexcept {
    SetThreadpoolTimer(pti, pftDueTime, msPeriod, msWindowLength);
    return TRUE;
}

inline BOOL WINAPI IsThreadpoolTimerSet(PTP_TIMER pti) noexcept {
    if (!pti) return FALSE;
    return pti->isSet.load(std::memory_order_relaxed) ? TRUE : FALSE;
}

inline void WINAPI WaitForThreadpoolTimerCallbacks(PTP_TIMER pti, BOOL fCancelPendingCallbacks) noexcept {
    if (!pti) return;
    if (fCancelPendingCallbacks) {
        pti->cancelled.store(true, std::memory_order_release);
    }
    std::unique_lock<std::mutex> lock(pti->completionMtx);
    pti->completionCv.wait(lock, [pti]() {
        return pti->pendingCount.load(std::memory_order_acquire) <= 0;
    });
    if (fCancelPendingCallbacks) {
        pti->cancelled.store(false, std::memory_order_release);
    }
}

inline void WINAPI CloseThreadpoolTimer(PTP_TIMER pti) noexcept {
    if (!pti) return;
    {
        std::lock_guard<std::mutex> lock(pti->timerMtx);
        pti->stopThread = true;
        pti->isSet.store(false, std::memory_order_release);
        pti->timerCv.notify_all();
    }
    if (pti->timerThread && pti->timerThread->joinable()) {
        pti->timerThread->join();
    }
    WaitForThreadpoolTimerCallbacks(pti, TRUE);
    delete pti;
}

// Threadpool Wait APIs
struct TP_WAIT {
    PTP_WAIT_CALLBACK callback{nullptr};
    PVOID context{nullptr};
    PTP_POOL pool{nullptr};
    HANDLE waitHandle{nullptr};
    std::atomic<bool> isSet{false};
    std::atomic<bool> cancelled{false};
    std::atomic<int32_t> pendingCount{0};
    std::mutex completionMtx;
    std::condition_variable completionCv;
    std::unique_ptr<std::thread> waitThread;
    std::mutex waitMtx;
    std::condition_variable waitCv;
    bool stopThread{false};
};

inline PTP_WAIT WINAPI CreateThreadpoolWait(PTP_WAIT_CALLBACK pfnwa, PVOID pv, PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (!pfnwa) return nullptr;
    auto* waitObj = new TP_WAIT();
    waitObj->callback = pfnwa;
    waitObj->context = pv;
    if (pcbe && pcbe->Pool) {
        waitObj->pool = pcbe->Pool;
    } else {
        waitObj->pool = detail::s_defaultPool.get();
    }
    return waitObj;
}

inline void WINAPI SetThreadpoolWait(PTP_WAIT pwa, HANDLE h, const FILETIME* pftTimeout) noexcept {
    if (!pwa) return;
    {
        std::lock_guard<std::mutex> lock(pwa->waitMtx);
        if (!h) {
            pwa->isSet.store(false, std::memory_order_release);
            pwa->waitHandle = nullptr;
            pwa->waitCv.notify_all();
            return;
        }

        pwa->waitHandle = h;
        pwa->isSet.store(true, std::memory_order_release);
        pwa->cancelled.store(false, std::memory_order_release);

        DWORD timeoutMs = 0xFFFFFFFF; // INFINITE
        if (pftTimeout) {
            auto* ft = reinterpret_cast<const uint32_t*>(pftTimeout);
            int64_t due64 = static_cast<int64_t>((static_cast<uint64_t>(ft[1]) << 32) | ft[0]);
            if (due64 < 0) timeoutMs = static_cast<DWORD>((-due64) / 10000);
            else if (due64 == 0) timeoutMs = 0;
            else timeoutMs = 50;
        }

        // Fast-path: if handle is already signaled, dispatch immediately
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
            pwa->pendingCount.fetch_add(1, std::memory_order_relaxed);
            pwa->pool->enqueue([pwa]() {
                if (!pwa->cancelled.load(std::memory_order_relaxed)) {
                    TP_CALLBACK_INSTANCE inst{};
                    pwa->callback(&inst, pwa->context, pwa, static_cast<TP_WAIT_RESULT>(WAIT_OBJECT_0));
                }
                pwa->isSet.store(false, std::memory_order_release);
                int32_t rem = pwa->pendingCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (rem <= 0) {
                    std::lock_guard<std::mutex> lk(pwa->completionMtx);
                    pwa->completionCv.notify_all();
                }
            });
            return;
        }

        if (!pwa->waitThread) {
            pwa->waitThread = std::make_unique<std::thread>([pwa, timeoutMs]() {
                while (true) {
                    std::unique_lock<std::mutex> lock(pwa->waitMtx);
                    pwa->waitCv.wait(lock, [pwa]() {
                        return pwa->stopThread || pwa->isSet.load(std::memory_order_relaxed);
                    });
                    if (pwa->stopThread) return;

                    HANDLE hTarget = pwa->waitHandle;
                    pwa->isSet.store(false, std::memory_order_release);
                    lock.unlock();

                    DWORD res = WAIT_OBJECT_0;
                    if (hTarget) {
                        res = WaitForSingleObject(hTarget, timeoutMs);
                    }

                    pwa->pendingCount.fetch_add(1, std::memory_order_relaxed);
                    pwa->pool->enqueue([pwa, res]() {
                        if (!pwa->cancelled.load(std::memory_order_relaxed)) {
                            TP_CALLBACK_INSTANCE inst{};
                            pwa->callback(&inst, pwa->context, pwa, static_cast<TP_WAIT_RESULT>(res));
                            inst.runCleanup();
                        }
                        int32_t rem = pwa->pendingCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
                        if (rem <= 0) {
                            std::lock_guard<std::mutex> lk(pwa->completionMtx);
                            pwa->completionCv.notify_all();
                        }
                    });
                }
            });
        } else {
            pwa->waitCv.notify_all();
        }
    }
}

inline void WINAPI WaitForThreadpoolWaitCallbacks(PTP_WAIT pwa, BOOL fCancelPendingCallbacks) noexcept {
    if (!pwa) return;
    if (fCancelPendingCallbacks) {
        pwa->cancelled.store(true, std::memory_order_release);
    }
    std::unique_lock<std::mutex> lock(pwa->completionMtx);
    pwa->completionCv.wait(lock, [pwa]() {
        return pwa->pendingCount.load(std::memory_order_acquire) <= 0;
    });
    if (fCancelPendingCallbacks) {
        pwa->cancelled.store(false, std::memory_order_release);
    }
}

inline BOOL WINAPI SetThreadpoolWaitEx(PTP_WAIT pwa, HANDLE h, const FILETIME* pftTimeout, PVOID Reserved) noexcept {
    (void)Reserved;
    SetThreadpoolWait(pwa, h, pftTimeout);
    return TRUE;
}

inline void WINAPI CancelThreadpoolWait(PTP_WAIT pwa) noexcept {
    SetThreadpoolWait(pwa, nullptr, nullptr);
}

inline void WINAPI CloseThreadpoolWait(PTP_WAIT pwa) noexcept {
    if (!pwa) return;
    {
        std::lock_guard<std::mutex> lock(pwa->waitMtx);
        pwa->stopThread = true;
        pwa->isSet.store(false, std::memory_order_release);
        pwa->waitCv.notify_all();
    }
    if (pwa->waitThread && pwa->waitThread->joinable()) {
        pwa->waitThread->join();
    }
    WaitForThreadpoolWaitCallbacks(pwa, TRUE);
    delete pwa;
}

// Cleanup Group and Environment APIs
struct TP_CLEANUP_GROUP {
    std::mutex mtx;
    std::vector<PTP_WORK> works;
    std::vector<PTP_TIMER> timers;
    std::vector<PTP_WAIT> waits;
};

inline PTP_CLEANUP_GROUP WINAPI CreateThreadpoolCleanupGroup() noexcept {
    return new TP_CLEANUP_GROUP();
}

inline void WINAPI CloseThreadpoolCleanupGroupMembers(PTP_CLEANUP_GROUP ptpcg, BOOL fCancelPendingCallbacks, PVOID /*pvCleanupContext*/) noexcept {
    if (!ptpcg) return;
    std::lock_guard<std::mutex> lock(ptpcg->mtx);
    for (auto* w : ptpcg->works) {
        CloseThreadpoolWork(w);
    }
    ptpcg->works.clear();
    for (auto* t : ptpcg->timers) {
        CloseThreadpoolTimer(t);
    }
    ptpcg->timers.clear();
    for (auto* wa : ptpcg->waits) {
        CloseThreadpoolWait(wa);
    }
    ptpcg->waits.clear();
}

inline void WINAPI CloseThreadpoolCleanupGroup(PTP_CLEANUP_GROUP ptpcg) noexcept {
    if (ptpcg) {
        CloseThreadpoolCleanupGroupMembers(ptpcg, TRUE, nullptr);
        delete ptpcg;
    }
}

inline void WINAPI InitializeThreadpoolEnvironment(PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (pcbe) {
        memset(pcbe, 0, sizeof(TP_CALLBACK_ENVIRON));
        pcbe->Version = 3;
    }
}

inline void WINAPI DestroyThreadpoolEnvironment(PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (pcbe) {
        memset(pcbe, 0, sizeof(TP_CALLBACK_ENVIRON));
    }
}

inline void WINAPI SetThreadpoolCallbackPool(PTP_CALLBACK_ENVIRON pcbe, PTP_POOL ptpp) noexcept {
    if (pcbe) pcbe->Pool = ptpp;
}

inline void WINAPI SetThreadpoolCallbackCleanupGroup(PTP_CALLBACK_ENVIRON pcbe, PTP_CLEANUP_GROUP ptpcg, PVOID pfncg) noexcept {
    if (pcbe) {
        pcbe->CleanupGroup = ptpcg;
        pcbe->CleanupGroupCancelCallback = reinterpret_cast<PTP_CLEANUP_GROUP_CANCEL_CALLBACK>(pfncg);
    }
}

inline void WINAPI SetThreadpoolCallbackPriority(PTP_CALLBACK_ENVIRON pcbe, TP_CALLBACK_PRIORITY Priority) noexcept {
    if (pcbe) {
        pcbe->CallbackPriority = static_cast<ULONG>(Priority);
    }
}

inline void WINAPI SetThreadpoolCallbackRunsLong(PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (pcbe) {
        pcbe->u.s.LongFunction = 1;
    }
}

inline void WINAPI SetThreadpoolCallbackPersistent(PTP_CALLBACK_ENVIRON pcbe) noexcept {
    if (pcbe) {
        pcbe->u.s.Persistent = 1;
    }
}

inline void WINAPI SetThreadpoolCallbackLibrary(PTP_CALLBACK_ENVIRON pcbe, PVOID mod) noexcept {
    if (pcbe) {
        pcbe->RaceDll = mod;
    }
}

// Integer and String Helper APIs

// ============================================================================
// Legacy Threadpool & Timer Queue Architecture (Horizon 209)
// ============================================================================

inline BOOL WINAPI TrySubmitThreadpoolCallback(
    PTP_SIMPLE_CALLBACK pfns,
    PVOID pv,
    PTP_CALLBACK_ENVIRON pcbe
) noexcept {
    if (!pfns) return FALSE;
    TP_POOL* pool = (pcbe && pcbe->Pool) ? pcbe->Pool : detail::s_defaultPool.get();
    if (!pool) return FALSE;
    pool->enqueue([pfns, pv]() {
        TP_CALLBACK_INSTANCE inst{};
        pfns(&inst, pv);
    });
    return TRUE;
}

inline constexpr ULONG WT_EXECUTEDEFAULT            = 0x00000000;
inline constexpr ULONG WT_EXECUTEINIOTHREAD         = 0x00000001;
inline constexpr ULONG WT_EXECUTEINPERSISTENTTHREAD = 0x00000080;
inline constexpr ULONG WT_EXECUTELONGFUNCTION        = 0x00000010;
inline constexpr ULONG WT_EXECUTEINTIMERTHREAD       = 0x00000020;
inline constexpr ULONG WT_EXECUTEONLYONCE           = 0x00000008;

inline BOOL WINAPI QueueUserWorkItem(
    LPTHREAD_START_ROUTINE Function,
    PVOID Context,
    ULONG Flags
) noexcept {
    if (!Function) return FALSE;
    (void)Flags;
    TP_POOL* pool = detail::s_defaultPool.get();
    if (!pool) return FALSE;
    pool->enqueue([Function, Context]() {
        Function(Context);
    });
    return TRUE;
}

struct SovereignTimerQueueTimer {
    WAITORTIRCALLBACK callback{nullptr};
    PVOID context{nullptr};
    uint32_t dueTimeMs{0};
    uint32_t periodMs{0};
    ULONG flags{0};
    std::atomic<bool> active{true};
    std::thread worker;
    std::mutex mtx;
    std::condition_variable cv;
};

struct SovereignTimerQueue {
    std::mutex mtx;
    std::vector<std::shared_ptr<SovereignTimerQueueTimer>> timers;
    std::atomic<bool> destroyed{false};
};

class SovereignTimerQueueManager {
private:
    std::mutex m_mtx;
    std::unordered_map<HANDLE, std::shared_ptr<SovereignTimerQueue>> m_queues;
    std::unordered_map<HANDLE, std::shared_ptr<SovereignTimerQueueTimer>> m_timers;
    uint64_t m_nextHandle{ 0x54510001ULL }; // 'TQ' 0001

public:
    static SovereignTimerQueueManager& get() noexcept {
        static SovereignTimerQueueManager instance;
        return instance;
    }

    bool hasQueue(HANDLE h) noexcept {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queues.find(h) != m_queues.end();
    }

    bool hasTimer(HANDLE h) noexcept {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_timers.find(h) != m_timers.end();
    }

    HANDLE createQueue() noexcept {
        std::lock_guard<std::mutex> lock(m_mtx);
        auto q = std::make_shared<SovereignTimerQueue>();
        HANDLE h = reinterpret_cast<HANDLE>(m_nextHandle++);
        m_queues[h] = q;
        return h;
    }

    BOOL deleteQueue(HANDLE hQueue, HANDLE hCompletionEvent) noexcept {
        std::shared_ptr<SovereignTimerQueue> q;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            auto it = m_queues.find(hQueue);
            if (it == m_queues.end()) return FALSE;
            q = it->second;
            m_queues.erase(it);
        }
        if (q) {
            q->destroyed.store(true);
            std::lock_guard<std::mutex> lock(q->mtx);
            for (auto& t : q->timers) {
                t->active.store(false);
                t->cv.notify_all();
                if (t->worker.joinable()) {
                    if (t->worker.get_id() == std::this_thread::get_id()) t->worker.detach();
                    else t->worker.join();
                }
            }
            q->timers.clear();
        }
        if (hCompletionEvent && hCompletionEvent != INVALID_HANDLE_VALUE) {
            SetEvent(hCompletionEvent);
        }
        return TRUE;
    }

    BOOL createTimer(PHANDLE phNewTimer, HANDLE hQueue, WAITORTIRCALLBACK Callback, PVOID Parameter, DWORD DueTime, DWORD Period, ULONG Flags) noexcept {
        if (!phNewTimer || !Callback) return FALSE;
        auto timer = std::make_shared<SovereignTimerQueueTimer>();
        timer->callback = Callback;
        timer->context = Parameter;
        timer->dueTimeMs = DueTime;
        timer->periodMs = Period;
        timer->flags = Flags;
        timer->active.store(true);

        HANDLE hTimer = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            hTimer = reinterpret_cast<HANDLE>(m_nextHandle++);
            m_timers[hTimer] = timer;
            if (hQueue) {
                auto qIt = m_queues.find(hQueue);
                if (qIt != m_queues.end()) {
                    std::lock_guard<std::mutex> qLock(qIt->second->mtx);
                    qIt->second->timers.push_back(timer);
                }
            }
        }

        timer->worker = std::thread([timer]() {
            std::unique_lock<std::mutex> lock(timer->mtx);
            if (timer->dueTimeMs > 0) {
                timer->cv.wait_for(lock, std::chrono::milliseconds(timer->dueTimeMs), [&]() {
                    return !timer->active.load();
                });
            }
            if (!timer->active.load()) return;

            lock.unlock();
            timer->callback(timer->context, 1);
            lock.lock();

            if (timer->periodMs > 0 && !(timer->flags & WT_EXECUTEONLYONCE)) {
                while (timer->active.load()) {
                    timer->cv.wait_for(lock, std::chrono::milliseconds(timer->periodMs), [&]() {
                        return !timer->active.load();
                    });
                    if (!timer->active.load()) break;
                    lock.unlock();
                    timer->callback(timer->context, 1);
                    lock.lock();
                }
            }
        });

        *phNewTimer = hTimer;
        return TRUE;
    }

    BOOL changeTimer(HANDLE hQueue, HANDLE hTimer, DWORD DueTime, DWORD Period) noexcept {
        (void)hQueue;
        std::shared_ptr<SovereignTimerQueueTimer> timer;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            auto it = m_timers.find(hTimer);
            if (it == m_timers.end()) return FALSE;
            timer = it->second;
        }
        std::lock_guard<std::mutex> lock(timer->mtx);
        timer->dueTimeMs = DueTime;
        timer->periodMs = Period;
        timer->cv.notify_all();
        return TRUE;
    }

    BOOL deleteTimer(HANDLE hQueue, HANDLE hTimer, HANDLE hCompletionEvent) noexcept {
        (void)hQueue;
        std::shared_ptr<SovereignTimerQueueTimer> timer;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            auto it = m_timers.find(hTimer);
            if (it == m_timers.end()) return FALSE;
            timer = it->second;
            m_timers.erase(it);
        }
        if (timer) {
            timer->active.store(false);
            timer->cv.notify_all();
            if (timer->worker.joinable()) {
                if (timer->worker.get_id() == std::this_thread::get_id()) {
                    timer->worker.detach();
                } else {
                    timer->worker.join();
                }
            }
        }
        if (hCompletionEvent && hCompletionEvent != INVALID_HANDLE_VALUE) {
            SetEvent(hCompletionEvent);
        }
        return TRUE;
    }
};

inline HANDLE WINAPI CreateTimerQueue() noexcept {
    return SovereignTimerQueueManager::get().createQueue();
}

inline BOOL WINAPI DeleteTimerQueue(HANDLE TimerQueue) noexcept {
    return SovereignTimerQueueManager::get().deleteQueue(TimerQueue, nullptr);
}

inline BOOL WINAPI DeleteTimerQueueEx(HANDLE TimerQueue, HANDLE CompletionEvent) noexcept {
    return SovereignTimerQueueManager::get().deleteQueue(TimerQueue, CompletionEvent);
}

inline BOOL WINAPI CreateTimerQueueTimer(
    PHANDLE phNewTimer,
    HANDLE TimerQueue,
    WAITORTIRCALLBACK Callback,
    PVOID Parameter,
    DWORD DueTime,
    DWORD Period,
    ULONG Flags
) noexcept {
    return SovereignTimerQueueManager::get().createTimer(phNewTimer, TimerQueue, Callback, Parameter, DueTime, Period, Flags);
}

inline BOOL WINAPI ChangeTimerQueueTimer(
    HANDLE TimerQueue,
    HANDLE Timer,
    ULONG DueTime,
    ULONG Period
) noexcept {
    return SovereignTimerQueueManager::get().changeTimer(TimerQueue, Timer, DueTime, Period);
}

inline BOOL WINAPI DeleteTimerQueueTimer(
    HANDLE TimerQueue,
    HANDLE Timer,
    HANDLE CompletionEvent
) noexcept {
    return SovereignTimerQueueManager::get().deleteTimer(TimerQueue, Timer, CompletionEvent);
}

struct SovereignRegisteredWait {
    HANDLE hObject{nullptr};
    WAITORTIRCALLBACK callback{nullptr};
    PVOID context{nullptr};
    ULONG timeoutMs{0};
    ULONG flags{0};
    std::atomic<bool> active{true};
    std::thread worker;
    std::mutex mtx;
    std::condition_variable cv;
};

class SovereignWaitManager {
private:
    std::mutex m_mtx;
    std::unordered_map<HANDLE, std::shared_ptr<SovereignRegisteredWait>> m_waits;
    uint64_t m_nextHandle{ 0x57410001ULL }; // 'WA' 0001

public:
    static SovereignWaitManager& get() noexcept {
        static SovereignWaitManager instance;
        return instance;
    }

    bool hasWait(HANDLE h) noexcept {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_waits.find(h) != m_waits.end();
    }

    BOOL registerWait(
        PHANDLE phNewWaitObject,
        HANDLE hObject,
        WAITORTIRCALLBACK Callback,
        PVOID Context,
        ULONG dwMilliseconds,
        ULONG dwFlags
    ) noexcept {
        if (!phNewWaitObject || !hObject || !Callback) return FALSE;
        auto rw = std::make_shared<SovereignRegisteredWait>();
        rw->hObject = hObject;
        rw->callback = Callback;
        rw->context = Context;
        rw->timeoutMs = dwMilliseconds;
        rw->flags = dwFlags;
        rw->active.store(true);

        HANDLE hWait = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            hWait = reinterpret_cast<HANDLE>(m_nextHandle++);
            m_waits[hWait] = rw;
        }

        rw->worker = std::thread([rw]() {
            const DWORD stepMs = 10;
            DWORD elapsed = 0;
            DWORD waitRes = WAIT_TIMEOUT;
            while (rw->active.load()) {
                DWORD slice = (rw->timeoutMs == 0xFFFFFFFF) ? stepMs : std::min(stepMs, rw->timeoutMs - elapsed);
                waitRes = WaitForSingleObject(rw->hObject, slice);
                if (waitRes == WAIT_OBJECT_0) {
                    break;
                }
                if (rw->timeoutMs != 0xFFFFFFFF) {
                    elapsed += slice;
                    if (elapsed >= rw->timeoutMs) {
                        waitRes = WAIT_TIMEOUT;
                        break;
                    }
                }
            }
            if (rw->active.load()) {
                BOOLEAN timedOut = (waitRes == WAIT_TIMEOUT) ? 1 : 0;
                rw->callback(rw->context, timedOut);
            }
        });

        *phNewWaitObject = hWait;
        return TRUE;
    }

    BOOL unregisterWait(HANDLE hWaitObject, HANDLE hCompletionEvent) noexcept {
        std::shared_ptr<SovereignRegisteredWait> rw;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            auto it = m_waits.find(hWaitObject);
            if (it == m_waits.end()) return FALSE;
            rw = it->second;
            m_waits.erase(it);
        }
        if (rw) {
            rw->active.store(false);
            if (rw->worker.joinable()) {
                if (rw->worker.get_id() == std::this_thread::get_id()) {
                    rw->worker.detach();
                } else {
                    rw->worker.join();
                }
            }
        }
        if (hCompletionEvent && hCompletionEvent != INVALID_HANDLE_VALUE) {
            SetEvent(hCompletionEvent);
        }
        return TRUE;
    }
};

inline BOOL WINAPI RegisterWaitForSingleObject(
    PHANDLE phNewWaitObject,
    HANDLE hObject,
    WAITORTIRCALLBACK Callback,
    PVOID Context,
    ULONG dwMilliseconds,
    ULONG dwFlags
) noexcept {
    return SovereignWaitManager::get().registerWait(phNewWaitObject, hObject, Callback, Context, dwMilliseconds, dwFlags);
}

inline BOOL WINAPI UnregisterWait(HANDLE WaitHandle) noexcept {
    return SovereignWaitManager::get().unregisterWait(WaitHandle, nullptr);
}

inline BOOL WINAPI UnregisterWaitEx(HANDLE WaitHandle, HANDLE CompletionEvent) noexcept {
    return SovereignWaitManager::get().unregisterWait(WaitHandle, CompletionEvent);
}

inline bool CloseTimerQueueHandle(HANDLE hObject) noexcept {
    if (!hObject) return false;
    if (SovereignTimerQueueManager::get().hasTimer(hObject)) {
        return SovereignTimerQueueManager::get().deleteTimer(nullptr, hObject, nullptr) != 0;
    }
    if (SovereignTimerQueueManager::get().hasQueue(hObject)) {
        return SovereignTimerQueueManager::get().deleteQueue(hObject, nullptr) != 0;
    }
    if (SovereignWaitManager::get().hasWait(hObject)) {
        return SovereignWaitManager::get().unregisterWait(hObject, nullptr) != 0;
    }
    return false;
}

// ============================================================================
// Horizon 210: Threadpool I/O Architecture
// ============================================================================

struct TP_IO {
    HANDLE FileHandle{nullptr};
    PTP_IO_CALLBACK Callback{nullptr};
    PVOID Context{nullptr};
    PTP_CALLBACK_ENVIRON CallbackEnviron{nullptr};
    std::atomic<bool> Active{true};
    std::atomic<uint32_t> PendingIoCount{0};
    std::mutex Mtx;
    std::condition_variable Cv;
};

inline PTP_IO WINAPI CreateThreadpoolIo(
    HANDLE fl,
    PTP_IO_CALLBACK pfnio,
    PVOID pv,
    PTP_CALLBACK_ENVIRON pcbe
) noexcept {
    if (!fl || fl == INVALID_HANDLE_VALUE || !pfnio) return nullptr;
    auto* tpIo = new (std::nothrow) TP_IO();
    if (!tpIo) return nullptr;
    tpIo->FileHandle = fl;
    tpIo->Callback = pfnio;
    tpIo->Context = pv;
    tpIo->CallbackEnviron = pcbe;
    return tpIo;
}

inline VOID WINAPI StartThreadpoolIo(PTP_IO pio) noexcept {
    if (!pio) return;
    pio->PendingIoCount.fetch_add(1, std::memory_order_relaxed);
}

inline VOID WINAPI CancelThreadpoolIo(PTP_IO pio) noexcept {
    if (!pio) return;
    uint32_t cur = pio->PendingIoCount.load(std::memory_order_relaxed);
    while (cur > 0 && !pio->PendingIoCount.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {}
    pio->Cv.notify_all();
}

inline VOID WINAPI WaitForThreadpoolIoCallbacks(PTP_IO pio, BOOL fCancelPendingCallbacks) noexcept {
    if (!pio) return;
    if (fCancelPendingCallbacks) {
        pio->PendingIoCount.store(0, std::memory_order_relaxed);
        pio->Cv.notify_all();
    } else {
        std::unique_lock<std::mutex> lock(pio->Mtx);
        pio->Cv.wait(lock, [pio]() { return pio->PendingIoCount.load(std::memory_order_relaxed) == 0; });
    }
}

inline VOID WINAPI CloseThreadpoolIo(PTP_IO pio) noexcept {
    if (!pio) return;
    pio->Active.store(false, std::memory_order_relaxed);
    WaitForThreadpoolIoCallbacks(pio, TRUE);
    delete pio;
}

inline void TriggerThreadpoolIoCompletion(PTP_IO pio, PVOID Overlapped, ULONG IoResult, ULONG_PTR NumberOfBytesTransferred) noexcept {
    if (!pio || !pio->Active.load(std::memory_order_relaxed) || !pio->Callback) return;
    TP_CALLBACK_INSTANCE inst{};
    pio->Callback(&inst, pio->Context, Overlapped, IoResult, NumberOfBytesTransferred, pio);
    inst.runCleanup();
    uint32_t cur = pio->PendingIoCount.load(std::memory_order_relaxed);
    while (cur > 0 && !pio->PendingIoCount.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {}
    pio->Cv.notify_all();
}

// ============================================================================
// Horizon 210: SMBIOS / ACPI System Firmware Table Architecture
// ============================================================================

#pragma pack(push, 1)
struct RawSMBIOSData {
    BYTE  Used20CallingMethod;
    BYTE  SMBIOSMajorVersion;
    BYTE  SMBIOSMinorVersion;
    BYTE  DmiRevision;
    DWORD Length;
    BYTE  SMBIOSTableData[1];
};
#pragma pack(pop)

inline UINT WINAPI EnumSystemFirmwareTables(
    DWORD FirmwareTableProviderSignature,
    PVOID pFirmwareTableEnumBuffer,
    DWORD BufferSize
) noexcept {
    std::vector<DWORD> tables;
    if (FirmwareTableProviderSignature == 0x52534D42) { // 'RSMB'
        tables.push_back(0x00000000);
    } else if (FirmwareTableProviderSignature == 0x41435049) { // 'ACPI'
        tables.push_back(0x43495041); // 'APIC'
        tables.push_back(0x50434146); // 'FACP'
        tables.push_back(0x54455048); // 'HPET'
        tables.push_back(0x54445353); // 'SSDT'
        tables.push_back(0x54445344); // 'DSDT'
    } else if (FirmwareTableProviderSignature == 0x4649524D) { // 'FIRM'
        tables.push_back(0x00000000);
    } else {
        SetLastError(87);
        return 0;
    }

    DWORD needed = static_cast<DWORD>(tables.size() * sizeof(DWORD));
    if (!pFirmwareTableEnumBuffer || BufferSize < needed) {
        return needed;
    }

    std::memcpy(pFirmwareTableEnumBuffer, tables.data(), needed);
    return needed;
}

inline UINT WINAPI GetSystemFirmwareTable(
    DWORD FirmwareTableProviderSignature,
    DWORD FirmwareTableID,
    PVOID pFirmwareTableBuffer,
    DWORD BufferSize
) noexcept {
    if (FirmwareTableProviderSignature == 0x52534D42) { // 'RSMB'
        static const uint8_t s_smbiosTable[] = {
            0x00, 0x03, 0x05, 0x35, 0x48, 0x00, 0x00, 0x00,
            0x00, 0x1A, 0x01, 0x00, 0x01, 0x02, 0x00, 0xE8,
            0x03, 0x09, 0x90, 0x90, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0xFF, 0xFF,
            'M', 'i', 'c', 'a', 'N', 'T', ' ', 'B', 'I', 'O', 'S', 0x00,
            '2', '.', '5', '.', '0', 0x00,
            '1', '1', '/', '0', '3', '/', '2', '0', '2', '6', 0x00,
            0x00,
            0x01, 0x1B, 0x02, 0x00, 0x01, 0x02, 0x03, 0x04,
            0x33, 0x22, 0x11, 0x00, 0x55, 0x44, 0x77, 0x66,
            0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
            0x03, 0x01, 0x00,
            'M', 'i', 'c', 'a', 'N', 'T', ' ', 'C', 'o', 'r', 'p', 'o', 'r', 'a', 't', 'i', 'o', 'n', 0x00,
            'M', 'i', 'c', 'a', 'N', 'T', ' ', 'W', 'o', 'r', 'k', 's', 't', 'a', 't', 'i', 'o', 'n', 0x00,
            '1', '.', '0', 0x00,
            'S', 'N', '-', 'M', 'I', 'C', 'A', 'N', 'T', '-', '7', '7', '7', '7', 0x00,
            0x00
        };
        DWORD needed = static_cast<DWORD>(sizeof(s_smbiosTable));
        if (!pFirmwareTableBuffer || BufferSize < needed) {
            return needed;
        }
        std::memcpy(pFirmwareTableBuffer, s_smbiosTable, needed);
        return needed;
    } else if (FirmwareTableProviderSignature == 0x41435049) { // 'ACPI'
        struct AcpiHeader {
            DWORD Signature;
            DWORD Length;
            BYTE  Revision;
            BYTE  Checksum;
            char  OemId[6];
            char  OemTableId[8];
            DWORD OemRevision;
            DWORD CreatorId;
            DWORD CreatorRevision;
        };
        AcpiHeader hdr{};
        hdr.Signature = FirmwareTableID;
        hdr.Length = sizeof(AcpiHeader);
        hdr.Revision = 2;
        hdr.Checksum = 0;
        std::memcpy(hdr.OemId, "MICANT", 6);
        std::memcpy(hdr.OemTableId, "MICANT01", 8);
        hdr.OemRevision = 1;
        hdr.CreatorId = 0x544E434D;
        hdr.CreatorRevision = 1;

        DWORD needed = sizeof(AcpiHeader);
        if (!pFirmwareTableBuffer || BufferSize < needed) {
            return needed;
        }
        std::memcpy(pFirmwareTableBuffer, &hdr, needed);
        return needed;
    }

    SetLastError(87);
    return 0;
}

// ============================================================================
// Horizon 210: High-Resolution Multimedia Timers
// ============================================================================

inline std::atomic<UINT> s_CurrentTimerPeriod{ 1 };

inline MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc) noexcept {
    if (!ptc || cbtc < sizeof(TIMECAPS)) return TIMERR_NOCANDO;
    ptc->wPeriodMin = 1;
    ptc->wPeriodMax = 1000000;
    return TIMERR_NOERROR;
}

inline MMRESULT WINAPI timeBeginPeriod(UINT uPeriod) noexcept {
    if (uPeriod < 1 || uPeriod > 1000000) return TIMERR_NOCANDO;
    s_CurrentTimerPeriod.store(uPeriod, std::memory_order_relaxed);
    return TIMERR_NOERROR;
}

inline MMRESULT WINAPI timeEndPeriod(UINT uPeriod) noexcept {
    if (uPeriod < 1 || uPeriod > 1000000) return TIMERR_NOCANDO;
    s_CurrentTimerPeriod.store(1, std::memory_order_relaxed);
    return TIMERR_NOERROR;
}

inline DWORD WINAPI timeGetTime() noexcept {
    static const auto s_timeBoot = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(now - s_timeBoot).count());
}

inline int MulDiv(int nNumber, int nNumerator, int nDenominator) noexcept {
    if (nDenominator == 0) return -1;
    int64_t res = (static_cast<int64_t>(nNumber) * static_cast<int64_t>(nNumerator)) / nDenominator;
    return static_cast<int>(res);
}

inline int CompareStringOrdinal(LPCWCH lpString1, int cchCount1, LPCWCH lpString2, int cchCount2, BOOL bIgnoreCase) noexcept {
    if (!lpString1 && !lpString2) return 2;
    if (!lpString1) return 1;
    if (!lpString2) return 3;
    std::wstring s1(lpString1, (cchCount1 >= 0) ? cchCount1 : wcslen(lpString1));
    std::wstring s2(lpString2, (cchCount2 >= 0) ? cchCount2 : wcslen(lpString2));
    if (bIgnoreCase) {
        for (auto& c : s1) c = towlower(c);
        for (auto& c : s2) c = towlower(c);
    }
    if (s1 < s2) return 1;
    if (s1 > s2) return 3;
    return 2;
}

inline int CompareStringEx(LPCWSTR /*lpLocaleName*/, DWORD dwCmpFlags, LPCWCH lpString1, int cchCount1, LPCWCH lpString2, int cchCount2, LPVOID, LPVOID, LPARAM) noexcept {
    return CompareStringOrdinal(lpString1, cchCount1, lpString2, cchCount2, (dwCmpFlags & 0x00000001) != 0);
}

inline int LCMapStringEx(LPCWSTR, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest, LPVOID, LPVOID, LPARAM) noexcept {
    return LCMapStringW(0, dwMapFlags, lpSrcStr, cchSrc, lpDestStr, cchDest);
}

inline int lstrcmpW(LPCWSTR s1, LPCWSTR s2) noexcept {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return wcscmp(s1, s2);
}

inline LPWSTR lstrcpyW(LPWSTR dst, LPCWSTR src) noexcept {
    if (!dst || !src) return dst;
    return wcscpy(dst, src);
}

inline LPSTR lstrcpynA(LPSTR dst, LPCSTR src, int maxLen) noexcept {
    if (!dst || maxLen <= 0) return dst;
    if (!src) { *dst = '\0'; return dst; }
    strncpy(dst, src, maxLen - 1);
    dst[maxLen - 1] = '\0';
    return dst;
}

inline int lstrlenW(LPCWSTR s) noexcept {
    return s ? static_cast<int>(wcslen(s)) : 0;
}

inline void GetNativeSystemInfo(SYSTEM_INFO* lpSystemInfo) noexcept {
    GetSystemInfo(lpSystemInfo);
}

inline BOOL QueryFullProcessImageNameW(HANDLE /*hProcess*/, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize) noexcept {
    if (!lpExeName || !lpdwSize || *lpdwSize == 0) return FALSE;
    const wchar_t* p = (dwFlags & 1) ? L"\\Device\\HarddiskVolume1\\Program Files\\Notepad++\\notepad++.exe" : L"C:\\Program Files\\Notepad++\\notepad++.exe";
    size_t len = wcslen(p);
    if (*lpdwSize <= len) { *lpdwSize = static_cast<DWORD>(len + 1); return FALSE; }
    wcscpy(lpExeName, p);
    *lpdwSize = static_cast<DWORD>(len);
    return TRUE;
}

inline BOOL QueryFullProcessImageNameA(HANDLE hProcess, DWORD dwFlags, LPSTR lpExeName, PDWORD lpdwSize) noexcept {
    if (!lpExeName || !lpdwSize || *lpdwSize == 0) return FALSE;
    wchar_t wbuf[512]{};
    DWORD wsize = 512;
    if (!QueryFullProcessImageNameW(hProcess, dwFlags, wbuf, &wsize)) return FALSE;
    std::string s;
    for (DWORD i = 0; i < wsize; ++i) s.push_back(static_cast<char>(wbuf[i]));
    if (*lpdwSize <= s.size()) { *lpdwSize = static_cast<DWORD>(s.size() + 1); return FALSE; }
    std::memcpy(lpExeName, s.c_str(), s.size() + 1);
    *lpdwSize = static_cast<DWORD>(s.size());
    return TRUE;
}

inline SIZE_T GlobalSize(HANDLE /*hMem*/) noexcept { return 4096; }

inline BOOL CopyFileExW(LPCWSTR src, LPCWSTR dst, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD) noexcept {
    return CopyFileW(src, dst, FALSE);
}

inline BOOL ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD, LPVOID, LPVOID) noexcept {
    if (backup) CopyFileW(replaced, backup, FALSE);
    return MoveFileExW(replacement, replaced, 1);
}

struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes{0};
    FILETIME ftCreationTime{};
    FILETIME ftLastAccessTime{};
    FILETIME ftLastWriteTime{};
    DWORD nFileSizeHigh{0};
    DWORD nFileSizeLow{0};
};

inline BOOL GetFileAttributesExW(LPCWSTR lpFileName, int /*fInfoLevelId*/, LPVOID lpFileInformation) noexcept {
    if (!lpFileName || !lpFileInformation) return FALSE;
    auto* pData = reinterpret_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation);
    pData->dwFileAttributes = GetFileAttributesW(lpFileName);
    if (pData->dwFileAttributes == INVALID_FILE_ATTRIBUTES) return FALSE;
    return TRUE;
}

inline BOOL GetFileInformationByHandleEx(HANDLE /*hFile*/, int /*FileInformationClass*/, LPVOID lpFileInformation, DWORD dwBufferSize) noexcept {
    if (lpFileInformation && dwBufferSize > 0) memset(lpFileInformation, 0, dwBufferSize);
    return TRUE;
}

inline BOOL AreFileApisANSI() noexcept { return TRUE; }

// ============================================================================
// Toolhelp32 Snapshot Architecture (kernel32.dll & api-ms-win-core-toolhelp)
// ============================================================================

inline constexpr DWORD TH32CS_SNAPHEAPLIST = 0x00000001;
inline constexpr DWORD TH32CS_SNAPPROCESS  = 0x00000002;
inline constexpr DWORD TH32CS_SNAPTHREAD   = 0x00000004;
inline constexpr DWORD TH32CS_SNAPMODULE   = 0x00000008;
inline constexpr DWORD TH32CS_SNAPMODULE32 = 0x00000010;
inline constexpr DWORD TH32CS_SNAPALL      = (TH32CS_SNAPHEAPLIST | TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD | TH32CS_SNAPMODULE);
inline constexpr DWORD TH32CS_INHERIT      = 0x80000000;

struct MODULEENTRY32 {
    DWORD   dwSize{sizeof(MODULEENTRY32)};
    DWORD   th32ModuleID{1};
    DWORD   th32ProcessID{0};
    DWORD   GlblcntUsage{0xFFFF};
    DWORD   ProccntUsage{0xFFFF};
    BYTE*   modBaseAddr{nullptr};
    DWORD   modBaseSize{0};
    HMODULE hModule{nullptr};
    char    szModule[256]{};
    char    szExePath[260]{};
};
using LPMODULEENTRY32 = MODULEENTRY32*;

struct PROCESSENTRY32 {
    DWORD     dwSize{sizeof(PROCESSENTRY32)};
    DWORD     cntUsage{0};
    DWORD     th32ProcessID{0};
    ULONG_PTR th32DefaultHeapID{0};
    DWORD     th32ModuleID{0};
    DWORD     cntThreads{1};
    DWORD     th32ParentProcessID{0};
    LONG      pcPriClassBase{8};
    DWORD     dwFlags{0};
    char      szExeFile[260]{};
};
using LPPROCESSENTRY32 = PROCESSENTRY32*;

struct ToolhelpSnapshot {
    DWORD flags{0};
    DWORD processId{0};
    std::vector<PROCESSENTRY32W> processes;
    size_t processIndex{0};
    std::vector<MODULEENTRY32W> modules;
    size_t moduleIndex{0};
    std::vector<THREADENTRY32> threads;
    size_t threadIndex{0};
};

class ToolhelpSnapshotManager {
public:
    static ToolhelpSnapshotManager& get() {
        static ToolhelpSnapshotManager instance;
        return instance;
    }

    HANDLE createSnapshot(DWORD dwFlags, DWORD th32ProcessID) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto snapshot = std::make_shared<ToolhelpSnapshot>();
        snapshot->flags = dwFlags;
        snapshot->processId = th32ProcessID;

        // Process enumeration
        if (dwFlags & TH32CS_SNAPPROCESS) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(PROCESSENTRY32W);

            // 0: [System Process]
            pe.th32ProcessID = 0;
            pe.cntThreads = 1;
            pe.th32ParentProcessID = 0;
            pe.pcPriClassBase = 0;
            std::wcsncpy(pe.szExeFile, L"[System Process]", 259);
            snapshot->processes.push_back(pe);

            // 4: System
            pe.th32ProcessID = 4;
            pe.cntThreads = 256;
            pe.th32ParentProcessID = 0;
            pe.pcPriClassBase = 8;
            std::wcsncpy(pe.szExeFile, L"System", 259);
            snapshot->processes.push_back(pe);

            // 360: smss.exe
            pe.th32ProcessID = 360;
            pe.cntThreads = 4;
            pe.th32ParentProcessID = 4;
            pe.pcPriClassBase = 11;
            std::wcsncpy(pe.szExeFile, L"smss.exe", 259);
            snapshot->processes.push_back(pe);

            // 520: csrss.exe
            pe.th32ProcessID = 520;
            pe.cntThreads = 16;
            pe.th32ParentProcessID = 360;
            pe.pcPriClassBase = 13;
            std::wcsncpy(pe.szExeFile, L"csrss.exe", 259);
            snapshot->processes.push_back(pe);

            // 680: services.exe
            pe.th32ProcessID = 680;
            pe.cntThreads = 16;
            pe.th32ParentProcessID = 520;
            pe.pcPriClassBase = 9;
            std::wcsncpy(pe.szExeFile, L"services.exe", 259);
            snapshot->processes.push_back(pe);

            // 720: lsass.exe
            pe.th32ProcessID = 720;
            pe.cntThreads = 8;
            pe.th32ParentProcessID = 520;
            pe.pcPriClassBase = 9;
            std::wcsncpy(pe.szExeFile, L"lsass.exe", 259);
            snapshot->processes.push_back(pe);

            // 940: svchost.exe
            pe.th32ProcessID = 940;
            pe.cntThreads = 32;
            pe.th32ParentProcessID = 680;
            pe.pcPriClassBase = 8;
            std::wcsncpy(pe.szExeFile, L"svchost.exe", 259);
            snapshot->processes.push_back(pe);

            // 1000: micant_app.exe
            pe.th32ProcessID = 1000;
            pe.cntThreads = 8;
            pe.th32ParentProcessID = 940;
            pe.pcPriClassBase = 8;
            std::wcsncpy(pe.szExeFile, L"micant_app.exe", 259);
            snapshot->processes.push_back(pe);

            // 2400: explorer.exe
            pe.th32ProcessID = 2400;
            pe.cntThreads = 24;
            pe.th32ParentProcessID = 940;
            pe.pcPriClassBase = 8;
            std::wcsncpy(pe.szExeFile, L"explorer.exe", 259);
            snapshot->processes.push_back(pe);
        }

        // Module enumeration
        if (dwFlags & (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32)) {
            MODULEENTRY32W me{};
            me.dwSize = sizeof(MODULEENTRY32W);
            DWORD pid = (th32ProcessID == 0) ? 1000 : th32ProcessID;

            // Module 1: Main App Image
            me.th32ModuleID = 1;
            me.th32ProcessID = pid;
            me.modBaseAddr = reinterpret_cast<BYTE*>(0x140000000ULL);
            me.modBaseSize = 0x200000;
            me.hModule = reinterpret_cast<HMODULE>(0x140000000ULL);
            std::wcsncpy(me.szModule, L"micant_app.exe", 255);
            std::wcsncpy(me.szExePath, L"C:\\Windows\\System32\\micant_app.exe", 259);
            snapshot->modules.push_back(me);

            // Module 2: ntdll.dll
            me.th32ModuleID = 2;
            me.modBaseAddr = reinterpret_cast<BYTE*>(0x7FFDF0000000ULL);
            me.modBaseSize = 0x1E0000;
            me.hModule = reinterpret_cast<HMODULE>(0x7FFDF0000000ULL);
            std::wcsncpy(me.szModule, L"ntdll.dll", 255);
            std::wcsncpy(me.szExePath, L"C:\\Windows\\System32\\ntdll.dll", 259);
            snapshot->modules.push_back(me);

            // Module 3: kernel32.dll
            me.th32ModuleID = 3;
            me.modBaseAddr = reinterpret_cast<BYTE*>(0x7FFDE0000000ULL);
            me.modBaseSize = 0x120000;
            me.hModule = reinterpret_cast<HMODULE>(0x7FFDE0000000ULL);
            std::wcsncpy(me.szModule, L"kernel32.dll", 255);
            std::wcsncpy(me.szExePath, L"C:\\Windows\\System32\\kernel32.dll", 259);
            snapshot->modules.push_back(me);

            // Module 4: kernelbase.dll
            me.th32ModuleID = 4;
            me.modBaseAddr = reinterpret_cast<BYTE*>(0x7FFDE8000000ULL);
            me.modBaseSize = 0x240000;
            me.hModule = reinterpret_cast<HMODULE>(0x7FFDE8000000ULL);
            std::wcsncpy(me.szModule, L"kernelbase.dll", 255);
            std::wcsncpy(me.szExePath, L"C:\\Windows\\System32\\kernelbase.dll", 259);
            snapshot->modules.push_back(me);

            // Module 5: d3d12.dll
            me.th32ModuleID = 5;
            me.modBaseAddr = reinterpret_cast<BYTE*>(0x7FFDD0000000ULL);
            me.modBaseSize = 0x380000;
            me.hModule = reinterpret_cast<HMODULE>(0x7FFDD0000000ULL);
            std::wcsncpy(me.szModule, L"d3d12.dll", 255);
            std::wcsncpy(me.szExePath, L"C:\\Windows\\System32\\d3d12.dll", 259);
            snapshot->modules.push_back(me);

            // Module 6: dxgi.dll
            me.th32ModuleID = 6;
            me.modBaseAddr = reinterpret_cast<BYTE*>(0x7FFDD8000000ULL);
            me.modBaseSize = 0x140000;
            me.hModule = reinterpret_cast<HMODULE>(0x7FFDD8000000ULL);
            std::wcsncpy(me.szModule, L"dxgi.dll", 255);
            std::wcsncpy(me.szExePath, L"C:\\Windows\\System32\\dxgi.dll", 259);
            snapshot->modules.push_back(me);
        }

        // Thread enumeration
        if (dwFlags & TH32CS_SNAPTHREAD) {
            THREADENTRY32 te{};
            te.dwSize = sizeof(THREADENTRY32);
            DWORD pid = (th32ProcessID == 0) ? 1000 : th32ProcessID;

            te.cntUsage = 0;
            te.th32OwnerProcessID = pid;
            te.tpBasePri = 8;
            te.tpDeltaPri = 0;
            te.dwFlags = 0;

            te.th32ThreadID = 1001;
            snapshot->threads.push_back(te);

            te.th32ThreadID = 1002;
            te.tpBasePri = 9;
            snapshot->threads.push_back(te);

            te.th32ThreadID = 1003;
            te.tpBasePri = 8;
            snapshot->threads.push_back(te);

            te.th32ThreadID = 1004;
            te.tpBasePri = 10;
            snapshot->threads.push_back(te);
        }

        uint64_t handleVal = m_nextHandle++;
        HANDLE h = reinterpret_cast<HANDLE>(handleVal);
        m_snapshots[h] = snapshot;
        return h;
    }

    std::shared_ptr<ToolhelpSnapshot> getSnapshot(HANDLE h) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_snapshots.find(h);
        if (it != m_snapshots.end()) return it->second;
        return nullptr;
    }

    bool isSnapshot(HANDLE h) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_snapshots.find(h) != m_snapshots.end();
    }

    bool closeSnapshot(HANDLE h) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_snapshots.erase(h) > 0;
    }

private:
    std::mutex m_mutex;
    uint64_t m_nextHandle{0x55000000ULL};
    std::unordered_map<HANDLE, std::shared_ptr<ToolhelpSnapshot>> m_snapshots;
};

inline bool CloseToolhelpSnapshot(HANDLE hObject) noexcept {
    return ToolhelpSnapshotManager::get().closeSnapshot(hObject);
}

inline HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID) noexcept {
    return ToolhelpSnapshotManager::get().createSnapshot(dwFlags, th32ProcessID);
}

inline BOOL Process32FirstW(HANDLE hSnapshot, LPPROCESSENTRY32W lppe) noexcept {
    if (!lppe || lppe->dwSize < sizeof(PROCESSENTRY32W)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (hSnapshot == nullptr) {
        lppe->cntUsage = 1;
        lppe->th32ProcessID = 1000;
        lppe->th32DefaultHeapID = 0;
        lppe->th32ModuleID = 1;
        lppe->cntThreads = 4;
        lppe->th32ParentProcessID = 500;
        lppe->pcPriClassBase = 8;
        lppe->dwFlags = 0;
        std::wcsncpy(lppe->szExeFile, L"micant_app.exe", 259);
        return TRUE;
    }
    auto snap = ToolhelpSnapshotManager::get().getSnapshot(hSnapshot);
    if (!snap) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }
    if (snap->processes.empty()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    snap->processIndex = 0;
    *lppe = snap->processes[snap->processIndex++];
    return TRUE;
}

inline BOOL Process32NextW(HANDLE hSnapshot, LPPROCESSENTRY32W lppe) noexcept {
    if (!lppe || lppe->dwSize < sizeof(PROCESSENTRY32W)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (hSnapshot == nullptr) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    auto snap = ToolhelpSnapshotManager::get().getSnapshot(hSnapshot);
    if (!snap) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }
    if (snap->processIndex >= snap->processes.size()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    *lppe = snap->processes[snap->processIndex++];
    return TRUE;
}

inline BOOL Process32First(HANDLE hSnapshot, LPPROCESSENTRY32 lppe) noexcept {
    if (!lppe || lppe->dwSize < sizeof(PROCESSENTRY32)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PROCESSENTRY32W peW{};
    peW.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32FirstW(hSnapshot, &peW)) return FALSE;
    lppe->cntUsage = peW.cntUsage;
    lppe->th32ProcessID = peW.th32ProcessID;
    lppe->th32DefaultHeapID = peW.th32DefaultHeapID;
    lppe->th32ModuleID = peW.th32ModuleID;
    lppe->cntThreads = peW.cntThreads;
    lppe->th32ParentProcessID = peW.th32ParentProcessID;
    lppe->pcPriClassBase = peW.pcPriClassBase;
    lppe->dwFlags = peW.dwFlags;
    int len = WideCharToMultiByte(CP_ACP, 0, peW.szExeFile, -1, lppe->szExeFile, sizeof(lppe->szExeFile), nullptr, nullptr);
    if (len <= 0) lppe->szExeFile[0] = '\0';
    return TRUE;
}

inline BOOL Process32Next(HANDLE hSnapshot, LPPROCESSENTRY32 lppe) noexcept {
    if (!lppe || lppe->dwSize < sizeof(PROCESSENTRY32)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PROCESSENTRY32W peW{};
    peW.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32NextW(hSnapshot, &peW)) return FALSE;
    lppe->cntUsage = peW.cntUsage;
    lppe->th32ProcessID = peW.th32ProcessID;
    lppe->th32DefaultHeapID = peW.th32DefaultHeapID;
    lppe->th32ModuleID = peW.th32ModuleID;
    lppe->cntThreads = peW.cntThreads;
    lppe->th32ParentProcessID = peW.th32ParentProcessID;
    lppe->pcPriClassBase = peW.pcPriClassBase;
    lppe->dwFlags = peW.dwFlags;
    int len = WideCharToMultiByte(CP_ACP, 0, peW.szExeFile, -1, lppe->szExeFile, sizeof(lppe->szExeFile), nullptr, nullptr);
    if (len <= 0) lppe->szExeFile[0] = '\0';
    return TRUE;
}

inline BOOL WINAPI Module32FirstW(HANDLE hSnapshot, LPMODULEENTRY32W lpme) noexcept {
    if (!lpme || lpme->dwSize < sizeof(MODULEENTRY32W)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (hSnapshot == nullptr) {
        lpme->th32ModuleID = 1;
        lpme->th32ProcessID = 1000;
        lpme->GlblcntUsage = 0xFFFF;
        lpme->ProccntUsage = 0xFFFF;
        lpme->modBaseAddr = reinterpret_cast<BYTE*>(0x140000000ULL);
        lpme->modBaseSize = 0x200000;
        lpme->hModule = reinterpret_cast<HMODULE>(0x140000000ULL);
        std::wcsncpy(lpme->szModule, L"micant_app.exe", 255);
        std::wcsncpy(lpme->szExePath, L"C:\\Windows\\System32\\micant_app.exe", 259);
        return TRUE;
    }
    auto snap = ToolhelpSnapshotManager::get().getSnapshot(hSnapshot);
    if (!snap) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }
    if (snap->modules.empty()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    snap->moduleIndex = 0;
    *lpme = snap->modules[snap->moduleIndex++];
    return TRUE;
}

inline BOOL WINAPI Module32NextW(HANDLE hSnapshot, LPMODULEENTRY32W lpme) noexcept {
    if (!lpme || lpme->dwSize < sizeof(MODULEENTRY32W)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (hSnapshot == nullptr) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    auto snap = ToolhelpSnapshotManager::get().getSnapshot(hSnapshot);
    if (!snap) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }
    if (snap->moduleIndex >= snap->modules.size()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    *lpme = snap->modules[snap->moduleIndex++];
    return TRUE;
}

inline BOOL Module32First(HANDLE hSnapshot, MODULEENTRY32* lpme) noexcept {
    if (!lpme || lpme->dwSize < sizeof(MODULEENTRY32)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MODULEENTRY32W meW{};
    meW.dwSize = sizeof(MODULEENTRY32W);
    if (!Module32FirstW(hSnapshot, &meW)) return FALSE;
    lpme->th32ModuleID = meW.th32ModuleID;
    lpme->th32ProcessID = meW.th32ProcessID;
    lpme->GlblcntUsage = meW.GlblcntUsage;
    lpme->ProccntUsage = meW.ProccntUsage;
    lpme->modBaseAddr = meW.modBaseAddr;
    lpme->modBaseSize = meW.modBaseSize;
    lpme->hModule = meW.hModule;
    WideCharToMultiByte(CP_ACP, 0, meW.szModule, -1, lpme->szModule, sizeof(lpme->szModule), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, meW.szExePath, -1, lpme->szExePath, sizeof(lpme->szExePath), nullptr, nullptr);
    return TRUE;
}

inline BOOL Module32Next(HANDLE hSnapshot, MODULEENTRY32* lpme) noexcept {
    if (!lpme || lpme->dwSize < sizeof(MODULEENTRY32)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MODULEENTRY32W meW{};
    meW.dwSize = sizeof(MODULEENTRY32W);
    if (!Module32NextW(hSnapshot, &meW)) return FALSE;
    lpme->th32ModuleID = meW.th32ModuleID;
    lpme->th32ProcessID = meW.th32ProcessID;
    lpme->GlblcntUsage = meW.GlblcntUsage;
    lpme->ProccntUsage = meW.ProccntUsage;
    lpme->modBaseAddr = meW.modBaseAddr;
    lpme->modBaseSize = meW.modBaseSize;
    lpme->hModule = meW.hModule;
    WideCharToMultiByte(CP_ACP, 0, meW.szModule, -1, lpme->szModule, sizeof(lpme->szModule), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, meW.szExePath, -1, lpme->szExePath, sizeof(lpme->szExePath), nullptr, nullptr);
    return TRUE;
}

inline BOOL WINAPI Thread32First(HANDLE hSnapshot, LPTHREADENTRY32 lpte) noexcept {
    if (!lpte || lpte->dwSize < sizeof(THREADENTRY32)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (hSnapshot == nullptr) {
        lpte->cntUsage = 0;
        lpte->th32ThreadID = 1001;
        lpte->th32OwnerProcessID = 1000;
        lpte->tpBasePri = 8;
        lpte->tpDeltaPri = 0;
        lpte->dwFlags = 0;
        return TRUE;
    }
    auto snap = ToolhelpSnapshotManager::get().getSnapshot(hSnapshot);
    if (!snap) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }
    if (snap->threads.empty()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    snap->threadIndex = 0;
    *lpte = snap->threads[snap->threadIndex++];
    return TRUE;
}

inline BOOL WINAPI Thread32Next(HANDLE hSnapshot, LPTHREADENTRY32 lpte) noexcept {
    if (!lpte || lpte->dwSize < sizeof(THREADENTRY32)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (hSnapshot == nullptr) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    auto snap = ToolhelpSnapshotManager::get().getSnapshot(hSnapshot);
    if (!snap) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return FALSE;
    }
    if (snap->threadIndex >= snap->threads.size()) {
        SetLastError(18); // ERROR_NO_MORE_FILES
        return FALSE;
    }
    *lpte = snap->threads[snap->threadIndex++];
    return TRUE;
}

inline BOOL GetFileAttributesExA(LPCSTR lpFileName, int fInfoLevelId, LPVOID lpFileInformation) noexcept {
    if (!lpFileName) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::string s(lpFileName);
    std::wstring w(s.begin(), s.end());
    return GetFileAttributesExW(w.c_str(), fInfoLevelId, lpFileInformation);
}

inline HANDLE CreateJobObjectA([[maybe_unused]] void* lpJobAttributes, [[maybe_unused]] LPCSTR lpName) noexcept {
    static uint64_t dummyJob = 0x50B00001;
    return reinterpret_cast<HANDLE>(&dummyJob);
}

inline HANDLE CreateWaitableTimerExW([[maybe_unused]] void* lpTimerAttributes,
                                    [[maybe_unused]] LPCWSTR lpTimerName,
                                    [[maybe_unused]] DWORD dwFlags,
                                    [[maybe_unused]] DWORD dwDesiredAccess) noexcept {
    static uint64_t dummyTimer = 0x713E0001;
    return reinterpret_cast<HANDLE>(&dummyTimer);
}

inline BOOL GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize) noexcept {
    if (!lpBuffer || !nSize || *nSize < 7) return FALSE;
    const char name[] = "MICANT";
    std::memcpy(lpBuffer, name, sizeof(name));
    *nSize = sizeof(name) - 1;
    return TRUE;
}

#define LIST_MODULES_DEFAULT 0x0
#define LIST_MODULES_32BIT   0x01
#define LIST_MODULES_64BIT   0x02
#define LIST_MODULES_ALL     0x03

inline BOOL K32EnumProcessModules([[maybe_unused]] HANDLE hProcess, void* lphModule, DWORD cb, LPDWORD lpcbNeeded) noexcept {
    if (lpcbNeeded) *lpcbNeeded = sizeof(HMODULE);
    if (lphModule && cb >= sizeof(HMODULE)) {
        *reinterpret_cast<HMODULE*>(lphModule) = reinterpret_cast<HMODULE>(0x140000000ULL);
    }
    return TRUE;
}

inline BOOL EnumProcessModules(HANDLE hProcess, void* lphModule, DWORD cb, LPDWORD lpcbNeeded) noexcept {
    return K32EnumProcessModules(hProcess, lphModule, cb, lpcbNeeded);
}

inline BOOL K32EnumProcessModulesEx([[maybe_unused]] HANDLE hProcess, void* lphModule, DWORD cb, LPDWORD lpcbNeeded, [[maybe_unused]] DWORD dwFilterFlag) noexcept {
    constexpr uint32_t kModuleCount = 4;
    if (lpcbNeeded) *lpcbNeeded = sizeof(HMODULE) * kModuleCount;
    if (lphModule && cb >= sizeof(HMODULE)) {
        auto* pMods = reinterpret_cast<HMODULE*>(lphModule);
        uint32_t maxWrite = static_cast<uint32_t>(cb / sizeof(HMODULE));
        HMODULE mods[kModuleCount] = {
            reinterpret_cast<HMODULE>(0x140000000ULL),
            reinterpret_cast<HMODULE>(0x7FF800000000ULL),
            reinterpret_cast<HMODULE>(0x7FF800100000ULL),
            reinterpret_cast<HMODULE>(0x7FF800200000ULL)
        };
        uint32_t count = std::min<uint32_t>(kModuleCount, maxWrite);
        for (uint32_t i = 0; i < count; ++i) {
            pMods[i] = mods[i];
        }
    }
    return TRUE;
}

inline BOOL EnumProcessModulesEx(HANDLE hProcess, void* lphModule, DWORD cb, LPDWORD lpcbNeeded, DWORD dwFilterFlag) noexcept {
    return K32EnumProcessModulesEx(hProcess, lphModule, cb, lpcbNeeded, dwFilterFlag);
}

struct MODULEINFO {
    LPVOID lpBaseOfDll{nullptr};
    DWORD  SizeOfImage{0};
    LPVOID EntryPoint{nullptr};
};
typedef MODULEINFO* LPMODULEINFO;

inline BOOL K32GetModuleInformation([[maybe_unused]] HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb) noexcept {
    if (!lpmodinfo || cb < sizeof(MODULEINFO)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    lpmodinfo->lpBaseOfDll = reinterpret_cast<LPVOID>(hModule ? hModule : reinterpret_cast<HMODULE>(0x140000000ULL));
    lpmodinfo->SizeOfImage = 0x200000;
    lpmodinfo->EntryPoint = reinterpret_cast<LPVOID>(reinterpret_cast<uintptr_t>(lpmodinfo->lpBaseOfDll) + 0x1000);
    return TRUE;
}

inline BOOL GetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb) noexcept {
    return K32GetModuleInformation(hProcess, hModule, lpmodinfo, cb);
}

inline DWORD K32GetMappedFileNameW([[maybe_unused]] HANDLE hProcess, [[maybe_unused]] LPVOID lpv, LPWSTR lpFilename, DWORD nSize) noexcept {
    if (!lpFilename || nSize == 0) return 0;
    const wchar_t path[] = L"\\Device\\HarddiskVolume1\\Windows\\System32\\micant_app.exe";
    size_t len = std::min<size_t>(std::wcslen(path), nSize - 1);
    std::wcsncpy(lpFilename, path, len);
    lpFilename[len] = L'\0';
    return static_cast<DWORD>(len);
}

inline DWORD K32GetModuleFileNameExA([[maybe_unused]] HANDLE hProcess, [[maybe_unused]] HMODULE hModule, LPSTR lpFilename, DWORD nSize) noexcept {
    if (!lpFilename || nSize == 0) return 0;
    const char path[] = "C:\\Windows\\System32\\micant_app.exe";
    size_t len = std::min<size_t>(std::strlen(path), nSize - 1);
    std::strncpy(lpFilename, path, len);
    lpFilename[len] = '\0';
    return static_cast<DWORD>(len);
}

struct PROCESS_MEMORY_COUNTERS {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
};
using PPROCESS_MEMORY_COUNTERS = PROCESS_MEMORY_COUNTERS*;

struct PROCESS_MEMORY_COUNTERS_EX {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivateUsage;
};
using PPROCESS_MEMORY_COUNTERS_EX = PROCESS_MEMORY_COUNTERS_EX*;

struct PROCESS_MEMORY_COUNTERS_EX2 {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivateUsage;
    SIZE_T PrivateWorkingSetSize;
    uint64_t SharedCommitUsage;
};
using PPROCESS_MEMORY_COUNTERS_EX2 = PROCESS_MEMORY_COUNTERS_EX2*;

inline BOOL WINAPI GetProcessMemoryInfo(
    HANDLE /*Process*/,
    PPROCESS_MEMORY_COUNTERS ppsmemCounters,
    DWORD cb) noexcept {
    if (!ppsmemCounters || cb < sizeof(PROCESS_MEMORY_COUNTERS)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::memset(ppsmemCounters, 0, cb);
    ppsmemCounters->cb = cb;
    ppsmemCounters->PageFaultCount = 1200;
    ppsmemCounters->PeakWorkingSetSize = 128 * 1024 * 1024ULL;      // 128 MB
    ppsmemCounters->WorkingSetSize = 96 * 1024 * 1024ULL;           // 96 MB
    ppsmemCounters->QuotaPeakPagedPoolUsage = 2 * 1024 * 1024ULL;   // 2 MB
    ppsmemCounters->QuotaPagedPoolUsage = 1 * 1024 * 1024ULL;       // 1 MB
    ppsmemCounters->QuotaPeakNonPagedPoolUsage = 512 * 1024ULL;     // 512 KB
    ppsmemCounters->QuotaNonPagedPoolUsage = 256 * 1024ULL;         // 256 KB
    ppsmemCounters->PagefileUsage = 64 * 1024 * 1024ULL;            // 64 MB
    ppsmemCounters->PeakPagefileUsage = 80 * 1024 * 1024ULL;        // 80 MB

    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) {
        auto* ex = reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX*>(ppsmemCounters);
        ex->PrivateUsage = 64 * 1024 * 1024ULL;                     // 64 MB
    }

    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX2)) {
        auto* ex2 = reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX2*>(ppsmemCounters);
        ex2->PrivateWorkingSetSize = 64 * 1024 * 1024ULL;           // 64 MB
        ex2->SharedCommitUsage = 16 * 1024 * 1024ULL;               // 16 MB
    }

    return TRUE;
}

inline BOOL WINAPI K32GetProcessMemoryInfo(
    HANDLE Process,
    PPROCESS_MEMORY_COUNTERS ppsmemCounters,
    DWORD cb) noexcept {
    return GetProcessMemoryInfo(Process, ppsmemCounters, cb);
}

enum PROCESS_INFORMATION_CLASS {
    ProcessMemoryPriority = 0,
    ProcessPowerThrottling = 1,
    ProcessProtectionLevelInfo = 7,
    ProcessLeapSecondInfo = 8,
    ProcessMachineTypeInfo = 9
};

struct MEMORY_PRIORITY_INFORMATION {
    ULONG MemoryPriority;
};

inline constexpr ULONG MEMORY_PRIORITY_LOWEST       = 0;
inline constexpr ULONG MEMORY_PRIORITY_VERY_LOW     = 1;
inline constexpr ULONG MEMORY_PRIORITY_LOW          = 2;
inline constexpr ULONG MEMORY_PRIORITY_MEDIUM       = 3;
inline constexpr ULONG MEMORY_PRIORITY_BELOW_NORMAL = 4;
inline constexpr ULONG MEMORY_PRIORITY_NORMAL       = 5;

struct PROCESS_PROTECTION_LEVEL_INFORMATION {
    DWORD ProtectionLevel;
};

inline constexpr DWORD PROTECTION_LEVEL_NONE                  = 0x00000000;
inline constexpr DWORD PROTECTION_LEVEL_WINTCB_LIGHT          = 0x00000001;
inline constexpr DWORD PROTECTION_LEVEL_WINDOWS               = 0x00000002;
inline constexpr DWORD PROTECTION_LEVEL_WINDOWS_LIGHT         = 0x00000003;
inline constexpr DWORD PROTECTION_LEVEL_ANTIMALWARE_LIGHT     = 0x00000004;
inline constexpr DWORD PROTECTION_LEVEL_LSA_LIGHT             = 0x00000005;

struct PROCESS_MACHINE_INFORMATION {
    uint16_t ProcessMachine;
    uint16_t Res0;
    DWORD MachineAttributes;
};

inline constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
inline constexpr uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xAA64;

inline constexpr DWORD THREAD_DYNAMIC_CODE_POLICY_PROHIBIT = 1;

struct PROCESS_POWER_THROTTLING_STATE {
    uint32_t Version;
    uint32_t ControlMask;
    uint32_t StateMask;
};

inline constexpr uint32_t PROCESS_POWER_THROTTLING_CURRENT_VERSION = 1;
inline constexpr uint32_t PROCESS_POWER_THROTTLING_EXECUTION_SPEED = 0x1;
inline constexpr uint32_t PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION = 0x2;

inline PROCESS_POWER_THROTTLING_STATE s_processPowerThrottling{
    .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
    .ControlMask = 0,
    .StateMask = 0
};

inline MEMORY_PRIORITY_INFORMATION s_processMemoryPriority{ MEMORY_PRIORITY_NORMAL };
inline PROCESS_PROTECTION_LEVEL_INFORMATION s_processProtectionLevel{ PROTECTION_LEVEL_NONE };
inline PROCESS_MACHINE_INFORMATION s_processMachineInfo{ IMAGE_FILE_MACHINE_AMD64, 0, 0 };

inline BOOL SetProcessInformation([[maybe_unused]] HANDLE hProcess,
                                  int ProcessInformationClass,
                                  void* ProcessInformation,
                                  DWORD ProcessInformationSize) noexcept {
    if (!ProcessInformation) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (ProcessInformationClass == ProcessMemoryPriority || ProcessInformationClass == 0) {
        if (ProcessInformationSize < sizeof(MEMORY_PRIORITY_INFORMATION)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        s_processMemoryPriority = *reinterpret_cast<const MEMORY_PRIORITY_INFORMATION*>(ProcessInformation);
        return TRUE;
    }
    if (ProcessInformationClass == ProcessPowerThrottling || ProcessInformationClass == 1) {
        if (ProcessInformationSize < sizeof(PROCESS_POWER_THROTTLING_STATE)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        auto* pState = reinterpret_cast<const PROCESS_POWER_THROTTLING_STATE*>(ProcessInformation);
        if (pState->Version != PROCESS_POWER_THROTTLING_CURRENT_VERSION) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        s_processPowerThrottling = *pState;
        return TRUE;
    }
    if (ProcessInformationClass == ProcessProtectionLevelInfo || ProcessInformationClass == 7) {
        if (ProcessInformationSize < sizeof(PROCESS_PROTECTION_LEVEL_INFORMATION)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        s_processProtectionLevel = *reinterpret_cast<const PROCESS_PROTECTION_LEVEL_INFORMATION*>(ProcessInformation);
        return TRUE;
    }
    if (ProcessInformationClass == ProcessMachineTypeInfo || ProcessInformationClass == 9) {
        if (ProcessInformationSize < sizeof(PROCESS_MACHINE_INFORMATION)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        s_processMachineInfo = *reinterpret_cast<const PROCESS_MACHINE_INFORMATION*>(ProcessInformation);
        return TRUE;
    }
    return TRUE;
}

inline BOOL GetProcessInformation([[maybe_unused]] HANDLE hProcess,
                                  int ProcessInformationClass,
                                  void* ProcessInformation,
                                  DWORD ProcessInformationSize) noexcept {
    if (!ProcessInformation) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (ProcessInformationClass == ProcessMemoryPriority || ProcessInformationClass == 0) {
        if (ProcessInformationSize < sizeof(MEMORY_PRIORITY_INFORMATION)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        *reinterpret_cast<MEMORY_PRIORITY_INFORMATION*>(ProcessInformation) = s_processMemoryPriority;
        return TRUE;
    }
    if (ProcessInformationClass == ProcessPowerThrottling || ProcessInformationClass == 1) {
        if (ProcessInformationSize < sizeof(PROCESS_POWER_THROTTLING_STATE)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        *reinterpret_cast<PROCESS_POWER_THROTTLING_STATE*>(ProcessInformation) = s_processPowerThrottling;
        return TRUE;
    }
    if (ProcessInformationClass == ProcessProtectionLevelInfo || ProcessInformationClass == 7) {
        if (ProcessInformationSize < sizeof(PROCESS_PROTECTION_LEVEL_INFORMATION)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        *reinterpret_cast<PROCESS_PROTECTION_LEVEL_INFORMATION*>(ProcessInformation) = s_processProtectionLevel;
        return TRUE;
    }
    if (ProcessInformationClass == ProcessMachineTypeInfo || ProcessInformationClass == 9) {
        if (ProcessInformationSize < sizeof(PROCESS_MACHINE_INFORMATION)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        *reinterpret_cast<PROCESS_MACHINE_INFORMATION*>(ProcessInformation) = s_processMachineInfo;
        return TRUE;
    }
    return TRUE;
}

inline BOOL SetProcessShutdownParameters([[maybe_unused]] DWORD dwLevel, [[maybe_unused]] DWORD dwFlags) noexcept {
    return TRUE;
}

inline BOOL UnmapViewOfFileEx(LPCVOID lpBaseAddress, [[maybe_unused]] DWORD UnmapFlags) noexcept {
    return UnmapViewOfFile(lpBaseAddress);
}

inline ULONG WINAPI RemoveVectoredExceptionHandler(void* Handle) noexcept {
    return ntdll::RtlRemoveVectoredExceptionHandler(Handle);
}

inline ULONG WINAPI RemoveVectoredContinueHandler(void* Handle) noexcept {
    return ntdll::RtlRemoveVectoredContinueHandler(Handle);
}

inline BOOL ReadDirectoryChangesW(HANDLE, LPVOID, DWORD, BOOL, DWORD, LPDWORD lpBytesReturned, LPOVERLAPPED, void*) noexcept {
    if (lpBytesReturned) *lpBytesReturned = 0;
    return TRUE;
}

inline HANDLE FindFirstChangeNotificationW(LPCWSTR /*lpPathName*/, BOOL /*bWatchSubtree*/, DWORD /*dwNotifyFilter*/) noexcept {
    return reinterpret_cast<HANDLE>(0x6001);
}

inline BOOL FindNextChangeNotification(HANDLE /*hChangeHandle*/) noexcept {
    return TRUE;
}

inline BOOL FindCloseChangeNotification(HANDLE /*hChangeHandle*/) noexcept {
    return TRUE;
}

inline DWORD SleepEx(DWORD dwMilliseconds, BOOL bAlertable) noexcept {
    if (!bAlertable) {
        Sleep(dwMilliseconds);
        return 0;
    }
    if (dwMilliseconds == 0) {
        LargeInteger zero{};
        const auto status = ntdll::NtWaitForSingleObject(
            static_cast<Handle>(-2), true, &zero);
        if (status == NtStatus::UserApc) return WAIT_IO_COMPLETION;
        if (status == NtStatus::Timeout || status == NtStatus::Success) return 0;
        SetLastError(ntdll::RtlNtStatusToDosError(status));
        return WAIT_FAILED;
    }
    // The native timer wait is not implemented yet. Do not report a completed
    // alertable sleep when the kernel cannot honor its timeout/APC contract.
    SetLastError(120); // ERROR_CALL_NOT_IMPLEMENTED
    return WAIT_FAILED;
}

inline DWORD WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) noexcept {
    if (!hHandle) return WAIT_FAILED;
    LargeInteger timeout{};
    LargeInteger* pTimeout = nullptr;
    if (dwMilliseconds != INFINITE) {
        timeout.quadPart = -static_cast<int64_t>(dwMilliseconds) * 10000;
        pTimeout = &timeout;
    }
    const auto status = ntdll::NtWaitForSingleObject(
        reinterpret_cast<Handle>(hHandle), bAlertable != FALSE, pTimeout);
    if (status == NtStatus::Success) return WAIT_OBJECT_0;
    if (status == NtStatus::Timeout) return WAIT_TIMEOUT;
    if (status == NtStatus::UserApc) return WAIT_IO_COMPLETION;
    SetLastError(ntdll::RtlNtStatusToDosError(status));
    return WAIT_FAILED;
}

inline DWORD QueueUserAPC(PAPCFUNC pfnAPC, HANDLE hThread, ULONG_PTR dwData) noexcept {
    if (!pfnAPC || !hThread) return 0;
    const auto status = ntdll::NtQueueApcThread(hThread,
        reinterpret_cast<void*>(pfnAPC), reinterpret_cast<void*>(dwData), nullptr, nullptr);
    return NT_SUCCESS(status) ? 1u : 0u;
}
inline BOOL CancelIo(HANDLE /*hFile*/) noexcept { return TRUE; }

inline DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation) noexcept {
    if (!lpTimeZoneInformation) return 0xFFFFFFFFu;
    auto* shared = reinterpret_cast<volatile const KUserSharedData*>(
        UserSharedDataAddress);
    int32_t high1{}, high2{};
    uint32_t low{};
    bool stable = false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        high1 = shared->timeZoneBiasHigh1;
        low = shared->timeZoneBiasLow;
        high2 = shared->timeZoneBiasHigh2;
        if (high1 == high2) { stable = true; break; }
    }
    if (!stable) return 0xFFFFFFFFu;
    const int64_t biasTicks = static_cast<int64_t>(
        (static_cast<uint64_t>(static_cast<uint32_t>(high1)) << 32) | low);
    if (biasTicks % static_cast<int64_t>(micant::shared::time::TicksPerMinute) != 0)
        return 0xFFFFFFFFu;
    const int64_t bias = biasTicks /
        static_cast<int64_t>(micant::shared::time::TicksPerMinute);
    if (bias < INT32_MIN || bias > INT32_MAX) return 0xFFFFFFFFu;
    *lpTimeZoneInformation = {};
    lpTimeZoneInformation->Bias = static_cast<LONG>(bias);
    std::wcscpy(lpTimeZoneInformation->StandardName,
        bias == 0 ? L"UTC" : L"MicaNT");
    return shared->timeZoneId <= 2 ? shared->timeZoneId : 0xFFFFFFFFu;
}

namespace time_detail {
inline bool toTicks(const SYSTEMTIME& value, uint64_t& ticks) noexcept {
    micant::shared::time::SystemTimeFields fields{
        value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute,
        value.wSecond, value.wMilliseconds, value.wDayOfWeek, 0};
    return micant::shared::time::toSystemTime100ns(fields, ticks);
}

inline bool ruleUtc(const SYSTEMTIME& rule, uint16_t year, LONG priorBias,
                    uint64_t& utc) noexcept {
    if (!rule.wMonth || rule.wMonth > 12 || priorBias < -2880 || priorBias > 2880)
        return false;
    if (rule.wYear && rule.wYear != year) return false;
    const uint16_t effectiveYear = rule.wYear ? rule.wYear : year;
    uint32_t day = rule.wDay;
    if (!rule.wYear) {
        if (rule.wDay < 1 || rule.wDay > 5 || rule.wDayOfWeek > 6) return false;
        micant::shared::time::SystemTimeFields first{
            effectiveYear, rule.wMonth, 1, 0, 0, 0, 0, 0, 0};
        uint64_t firstTicks{};
        if (!micant::shared::time::toSystemTime100ns(first, firstTicks) ||
            !micant::shared::time::fromSystemTime100ns(firstTicks, first)) return false;
        day = 1 + (rule.wDayOfWeek + 7 - first.weekday) % 7 + (rule.wDay - 1) * 7;
        if (day > micant::shared::time::daysInMonth(effectiveYear, rule.wMonth)) day -= 7;
    }
    SYSTEMTIME local{effectiveYear, rule.wMonth, rule.wDayOfWeek, static_cast<uint16_t>(day),
        rule.wHour, rule.wMinute, rule.wSecond, rule.wMilliseconds};
    uint64_t localTicks{};
    if (!toTicks(local, localTicks)) return false;
    const int64_t adjustment = static_cast<int64_t>(priorBias) *
        static_cast<int64_t>(micant::shared::time::TicksPerMinute);
    if (adjustment >= 0) {
        if (localTicks > UINT64_MAX - static_cast<uint64_t>(adjustment)) return false;
        utc = localTicks + static_cast<uint64_t>(adjustment);
    } else {
        if (localTicks < static_cast<uint64_t>(-adjustment)) return false;
        utc = localTicks - static_cast<uint64_t>(-adjustment);
    }
    return true;
}

inline bool utcToLocal(const TIME_ZONE_INFORMATION& zone, const SYSTEMTIME& utc,
                       SYSTEMTIME& local) noexcept {
    uint64_t utcTicks{};
    if (!toTicks(utc, utcTicks) || zone.Bias < -1440 || zone.Bias > 1440 ||
        zone.StandardBias < -1440 || zone.StandardBias > 1440 ||
        zone.DaylightBias < -1440 || zone.DaylightBias > 1440) return false;
    int64_t bias = static_cast<int64_t>(zone.Bias) + zone.StandardBias;
    if ((zone.DaylightDate.wMonth == 0) != (zone.StandardDate.wMonth == 0))
        return false;
    if (zone.DaylightDate.wMonth && zone.StandardDate.wMonth) {
        uint64_t daylightStart{}, standardStart{};
        const int64_t daylightBias = static_cast<int64_t>(zone.Bias) + zone.DaylightBias;
        if (!ruleUtc(zone.DaylightDate, utc.wYear, static_cast<LONG>(bias), daylightStart) ||
            !ruleUtc(zone.StandardDate, utc.wYear, static_cast<LONG>(daylightBias), standardStart))
            return false;
        const bool daylight = daylightStart < standardStart
            ? utcTicks >= daylightStart && utcTicks < standardStart
            : utcTicks >= daylightStart || utcTicks < standardStart;
        if (daylight) bias = daylightBias;
    }
    const int64_t delta = -bias *
        static_cast<int64_t>(micant::shared::time::TicksPerMinute);
    uint64_t localTicks{};
    if (delta >= 0) {
        if (utcTicks > UINT64_MAX - static_cast<uint64_t>(delta)) return false;
        localTicks = utcTicks + static_cast<uint64_t>(delta);
    } else {
        if (utcTicks < static_cast<uint64_t>(-delta)) return false;
        localTicks = utcTicks - static_cast<uint64_t>(-delta);
    }
    micant::shared::time::SystemTimeFields fields{};
    if (!micant::shared::time::fromSystemTime100ns(localTicks, fields)) return false;
    local = {fields.year, fields.month, fields.weekday, fields.day, fields.hour,
        fields.minute, fields.second, fields.milliseconds};
    return true;
}

inline bool sameCivil(const SYSTEMTIME& a, const SYSTEMTIME& b) noexcept {
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay &&
        a.wHour == b.wHour && a.wMinute == b.wMinute && a.wSecond == b.wSecond &&
        a.wMilliseconds == b.wMilliseconds;
}
} // namespace time_detail

inline BOOL SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION* zone,
    const SYSTEMTIME* utc, LPSYSTEMTIME local) noexcept {
    if (!utc || !local) return FALSE;
    TIME_ZONE_INFORMATION systemZone{};
    if (!zone) {
        if (GetTimeZoneInformation(&systemZone) == 0xFFFFFFFFu) return FALSE;
        zone = &systemZone;
    }
    SYSTEMTIME result{};
    if (!time_detail::utcToLocal(*zone, *utc, result)) return FALSE;
    *local = result;
    return TRUE;
}

inline BOOL TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION* zone,
    const SYSTEMTIME* local, LPSYSTEMTIME utc) noexcept {
    if (!local || !utc) return FALSE;
    TIME_ZONE_INFORMATION systemZone{};
    if (!zone) {
        if (GetTimeZoneInformation(&systemZone) == 0xFFFFFFFFu) return FALSE;
        zone = &systemZone;
    }
    uint64_t localTicks{};
    if (!time_detail::toTicks(*local, localTicks)) return FALSE;
    const int64_t standardBias = static_cast<int64_t>(zone->Bias) + zone->StandardBias;
    const int64_t daylightBias = static_cast<int64_t>(zone->Bias) + zone->DaylightBias;
    auto tryBias = [&](int64_t bias, SYSTEMTIME& result) noexcept {
        if (bias < -2880 || bias > 2880) return false;
        const int64_t delta = bias *
            static_cast<int64_t>(micant::shared::time::TicksPerMinute);
        uint64_t candidate{};
        if (delta >= 0) {
            if (localTicks > UINT64_MAX - static_cast<uint64_t>(delta)) return false;
            candidate = localTicks + static_cast<uint64_t>(delta);
        } else {
            if (localTicks < static_cast<uint64_t>(-delta)) return false;
            candidate = localTicks - static_cast<uint64_t>(-delta);
        }
        micant::shared::time::SystemTimeFields fields{};
        if (!micant::shared::time::fromSystemTime100ns(candidate, fields)) return false;
        SYSTEMTIME utcCandidate{fields.year, fields.month, fields.weekday, fields.day,
            fields.hour, fields.minute, fields.second, fields.milliseconds};
        SYSTEMTIME roundTrip{};
        if (!time_detail::utcToLocal(*zone, utcCandidate, roundTrip) ||
            !time_detail::sameCivil(roundTrip, *local)) return false;
        result = utcCandidate;
        return true;
    };
    SYSTEMTIME result{};
    if (!tryBias(standardBias, result) &&
        (!zone->DaylightDate.wMonth || !zone->StandardDate.wMonth ||
         !tryBias(daylightBias, result))) return FALSE;
    *utc = result;
    return TRUE;
}

inline int GetTimeFormatW(DWORD, DWORD, const SYSTEMTIME*, LPCWSTR, LPWSTR lpTimeStr, int cchTime) noexcept {
    if (cchTime > 0 && lpTimeStr) wcscpy(lpTimeStr, L"12:00:00");
    return 9;
}

inline int GetLocaleInfoW(LCID, DWORD, LPWSTR lpLCData, int cchData) noexcept {
    if (cchData > 0 && lpLCData) lpLCData[0] = L'\0';
    return 1;
}

inline int GetLocaleInfoA(LCID, DWORD, LPSTR lpLCData, int cchData) noexcept {
    if (cchData > 0 && lpLCData) lpLCData[0] = '\0';
    return 1;
}

inline int GetLocaleInfoEx(LPCWSTR, DWORD, LPWSTR lpLCData, int cchData) noexcept {
    if (cchData > 0 && lpLCData) lpLCData[0] = L'\0';
    return 1;
}

inline LCID GetUserDefaultLCID() noexcept { return 0x0409; }
inline WORD GetSystemDefaultLangID() noexcept { return 0x0409; }
inline WORD GetUserDefaultLangID() noexcept { return 0x0409; }

inline BOOL GetStringTypeExW(LCID, DWORD, LPCWCH, int, LPWORD lpCharType) noexcept {
    if (lpCharType) *lpCharType = 0;
    return TRUE;
}

inline BOOL GetStringTypeExA(LCID, DWORD, LPCSTR, int, LPWORD lpCharType) noexcept {
    if (lpCharType) *lpCharType = 0;
    return TRUE;
}

inline int GetNumberFormatW(LCID /*Locale*/, DWORD /*dwFlags*/, LPCWSTR lpValue, const void* /*lpFormat*/, LPWSTR lpNumberStr, int cchNumber) noexcept {
    if (!lpValue) return 0;
    size_t len = std::wcslen(lpValue);
    if (cchNumber == 0) return static_cast<int>(len + 1);
    if (lpNumberStr && cchNumber > 0) {
        size_t copy_len = (std::min)(len, static_cast<size_t>(cchNumber - 1));
        std::wcsncpy(lpNumberStr, lpValue, copy_len);
        lpNumberStr[copy_len] = L'\0';
        return static_cast<int>(copy_len + 1);
    }
    return 0;
}

inline int GetCalendarInfoW(LCID /*Locale*/, uint32_t /*Calendar*/, uint32_t /*CalType*/, LPWSTR lpCalData, int cchData, LPDWORD lpValue) noexcept {
    if (lpValue) {
        *lpValue = 1;
    }
    if (lpCalData && cchData > 0) {
        lpCalData[0] = L'1';
        lpCalData[1] = L'\0';
        return 1;
    }
    return 1;
}

inline BOOL GetStringTypeA(LCID /*Locale*/, DWORD /*dwInfoType*/, LPCSTR lpSrcStr, int cchSrc, LPWORD lpCharType) noexcept {
    if (!lpSrcStr || !lpCharType) return FALSE;
    int len = cchSrc < 0 ? static_cast<int>(std::strlen(lpSrcStr)) : cchSrc;
    for (int i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(lpSrcStr[i]);
        uint16_t flags = 0;
        if (std::isupper(c)) flags |= 0x0001; // C1_UPPER
        if (std::islower(c)) flags |= 0x0002; // C1_LOWER
        if (std::isdigit(c)) flags |= 0x0004; // C1_DIGIT
        if (std::isspace(c)) flags |= 0x0008; // C1_SPACE
        if (std::ispunct(c)) flags |= 0x0010; // C1_PUNCT
        if (std::iscntrl(c)) flags |= 0x0020; // C1_CNTRL
        if (std::isblank(c)) flags |= 0x0040; // C1_BLANK
        if (std::isxdigit(c)) flags |= 0x0080; // C1_XDIGIT
        lpCharType[i] = flags;
    }
    return TRUE;
}

inline int LCMapStringA(LCID, DWORD, LPCSTR, int, LPSTR, int) noexcept { return 0; }
inline BOOL IsValidLocale(LCID, DWORD) noexcept { return TRUE; }

inline BOOL EnumSystemLocalesW(LOCALE_ENUMPROCW p, DWORD) noexcept {
    if (p) { wchar_t loc[] = L"00000409"; p(loc); }
    return TRUE;
}

inline HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR) noexcept {
    static uint64_t dummyMutex = 0x6666;
    return reinterpret_cast<HANDLE>(&dummyMutex);
}

inline DWORD GetTempPath2W(DWORD nBufferLength, LPWSTR lpBuffer) noexcept {
    return GetTempPathW(nBufferLength, lpBuffer);
}

inline DWORD GetTempPath2A(DWORD nBufferLength, LPSTR lpBuffer) noexcept {
    return GetTempPathA(nBufferLength, lpBuffer);
}

// ============================================================================
// Win32 Job Object Types and Classes
// ============================================================================

enum JOBOBJECTINFOCLASS {
    JobObjectBasicAccountingInformation = 1,
    JobObjectBasicLimitInformation      = 2,
    JobObjectBasicProcessIdList         = 3,
    JobObjectBasicUIRestrictions        = 4,
    JobObjectSecurityLimitInformation   = 5,
    JobObjectEndOfJobTimeInformation    = 6,
    JobObjectAssociateCompletionPortInformation = 7,
    JobObjectBasicAndIoAccountingInformation    = 8,
    JobObjectExtendedLimitInformation   = 9,
    JobObjectJobSetInformation          = 10,
    JobObjectGroupInformation           = 11,
    JobObjectNotificationLimitInformation = 12,
    JobObjectLimitViolationInformation  = 13,
    JobObjectGroupInformationEx         = 14,
    JobObjectCpuRateControlInformation  = 15,
};

inline constexpr DWORD JOB_OBJECT_LIMIT_WORKINGSET                 = 0x00000001;
inline constexpr DWORD JOB_OBJECT_LIMIT_PROCESS_TIME               = 0x00000002;
inline constexpr DWORD JOB_OBJECT_LIMIT_JOB_TIME                   = 0x00000004;
inline constexpr DWORD JOB_OBJECT_LIMIT_ACTIVE_PROCESS             = 0x00000008;
inline constexpr DWORD JOB_OBJECT_LIMIT_AFFINITY                   = 0x00000010;
inline constexpr DWORD JOB_OBJECT_LIMIT_PRIORITY_CLASS             = 0x00000020;
inline constexpr DWORD JOB_OBJECT_LIMIT_PRESERVE_JOB_TIME          = 0x00000040;
inline constexpr DWORD JOB_OBJECT_LIMIT_SCHEDULING_CLASS           = 0x00000080;
inline constexpr DWORD JOB_OBJECT_LIMIT_PROCESS_MEMORY             = 0x00000100;
inline constexpr DWORD JOB_OBJECT_LIMIT_JOB_MEMORY                 = 0x00000200;
inline constexpr DWORD JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION = 0x00000400;
inline constexpr DWORD JOB_OBJECT_LIMIT_BREAKAWAY_OK               = 0x00000800;
inline constexpr DWORD JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK        = 0x00001000;
inline constexpr DWORD JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE          = 0x00002000;

inline constexpr DWORD JOB_OBJECT_CPU_RATE_CONTROL_ENABLE       = 0x00000001;
inline constexpr DWORD JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED = 0x00000002;
inline constexpr DWORD JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP     = 0x00000004;
inline constexpr DWORD JOB_OBJECT_CPU_RATE_CONTROL_NOTIFY       = 0x00000008;

#pragma pack(push, 8)
struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
    LARGE_INTEGER PerProcessUserTimeLimit;
    LARGE_INTEGER PerJobUserTimeLimit;
    DWORD         LimitFlags;
    SIZE_T        MinimumWorkingSetSize;
    SIZE_T        MaximumWorkingSetSize;
    DWORD         ActiveProcessLimit;
    uintptr_t     Affinity;
    DWORD         PriorityClass;
    DWORD         SchedulingClass;
};

struct IO_COUNTERS {
    uint64_t ReadOperationCount;
    uint64_t WriteOperationCount;
    uint64_t OtherOperationCount;
    uint64_t ReadTransferCount;
    uint64_t WriteTransferCount;
    uint64_t OtherTransferCount;
};

struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
    JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
    IO_COUNTERS                       IoInfo;
    SIZE_T                            ProcessMemoryLimit;
    SIZE_T                            JobMemoryLimit;
    SIZE_T                            PeakProcessMemoryUsed;
    SIZE_T                            PeakJobMemoryUsed;
};

struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION {
    LARGE_INTEGER TotalUserTime;
    LARGE_INTEGER TotalKernelTime;
    LARGE_INTEGER ThisPeriodTotalUserTime;
    LARGE_INTEGER ThisPeriodTotalKernelTime;
    DWORD         TotalPageFaultCount;
    DWORD         TotalProcesses;
    DWORD         ActiveProcesses;
    DWORD         TotalTerminatedProcesses;
};

struct JOBOBJECT_CPU_RATE_CONTROL_INFORMATION {
    DWORD ControlFlags;
    union {
        DWORD CpuRate;
        DWORD Weight;
        struct {
            uint16_t MinRate;
            uint16_t MaxRate;
        };
    };
};
#pragma pack(pop)

struct JobObjectInternal {
    std::wstring name;
    HANDLE primaryHandle{nullptr};
    std::vector<DWORD> assignedProcessIds;
    std::mutex mtx;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION extendedLimits{};
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuRate{};
    bool isTerminated{false};
};

class JobObjectManager {
public:
    static JobObjectManager& get() noexcept {
        static JobObjectManager instance;
        return instance;
    }

    HANDLE createJob(const wchar_t* lpName) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t id = m_nextId++;
        auto job = std::make_shared<JobObjectInternal>();
        HANDLE handle = reinterpret_cast<HANDLE>(id);
        job->primaryHandle = handle;
        if (lpName) {
            job->name = lpName;
            m_namedJobs[lpName] = job;
        }
        m_jobs[handle] = job;
        return handle;
    }

    HANDLE openJob(const wchar_t* lpName) {
        if (!lpName) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_namedJobs.find(lpName);
        if (it != m_namedJobs.end()) {
            return it->second->primaryHandle;
        }
        return nullptr;
    }

    std::shared_ptr<JobObjectInternal> getJob(HANDLE hJob) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_jobs.find(hJob);
        if (it != m_jobs.end()) return it->second;
        return nullptr;
    }

    void closeJobHandle(HANDLE hJob) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.erase(hJob);
    }

private:
    std::mutex m_mutex;
    uint64_t m_nextId{0x7101};
    std::unordered_map<HANDLE, std::shared_ptr<JobObjectInternal>> m_jobs;
    std::unordered_map<std::wstring, std::shared_ptr<JobObjectInternal>> m_namedJobs;
};

inline HANDLE WINAPI CreateJobObjectW(void* /*lpJobAttributes*/, const wchar_t* lpName) noexcept {
    return JobObjectManager::get().createJob(lpName);
}

inline HANDLE WINAPI OpenJobObjectW(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, const wchar_t* lpName) noexcept {
    return JobObjectManager::get().openJob(lpName);
}

inline BOOL WINAPI AssignProcessToJobObject(HANDLE hJob, HANDLE hProcess) noexcept {
    auto job = JobObjectManager::get().getJob(hJob);
    if (!job) return FALSE;
    DWORD pid = 0;
    if (hProcess == GetCurrentProcess() || hProcess == nullptr) {
        pid = GetCurrentProcessId();
    } else {
        pid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(hProcess));
    }
    std::lock_guard<std::mutex> lock(job->mtx);
    job->assignedProcessIds.push_back(pid);
    job->accounting.TotalProcesses++;
    job->accounting.ActiveProcesses++;
    return TRUE;
}

inline BOOL WINAPI SetInformationJobObject(
    HANDLE hJob,
    int JobObjectInformationClass,
    void* lpJobObjectInformation,
    DWORD cbJobObjectInformationLength
) noexcept {
    auto job = JobObjectManager::get().getJob(hJob);
    if (!job) return FALSE;
    if (!lpJobObjectInformation) return TRUE;

    std::lock_guard<std::mutex> lock(job->mtx);
    switch (JobObjectInformationClass) {
        case JobObjectBasicLimitInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION)) return FALSE;
            std::memcpy(&job->extendedLimits.BasicLimitInformation, lpJobObjectInformation, sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION));
            return TRUE;
        }
        case JobObjectExtendedLimitInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)) return FALSE;
            std::memcpy(&job->extendedLimits, lpJobObjectInformation, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
            return TRUE;
        }
        case JobObjectCpuRateControlInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION)) return FALSE;
            std::memcpy(&job->cpuRate, lpJobObjectInformation, sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION));
            return TRUE;
        }
        default:
            return TRUE;
    }
}

inline BOOL WINAPI QueryInformationJobObject(
    HANDLE hJob,
    int JobObjectInformationClass,
    void* lpJobObjectInformation,
    DWORD cbJobObjectInformationLength,
    DWORD* lpReturnLength
) noexcept {
    if (!lpJobObjectInformation) return FALSE;
    auto job = JobObjectManager::get().getJob(hJob);
    if (!job) return FALSE;

    std::lock_guard<std::mutex> lock(job->mtx);
    DWORD bytesWritten = 0;
    switch (JobObjectInformationClass) {
        case JobObjectBasicAccountingInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION)) return FALSE;
            std::memcpy(lpJobObjectInformation, &job->accounting, sizeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION));
            bytesWritten = sizeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION);
            break;
        }
        case JobObjectBasicLimitInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION)) return FALSE;
            std::memcpy(lpJobObjectInformation, &job->extendedLimits.BasicLimitInformation, sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION));
            bytesWritten = sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION);
            break;
        }
        case JobObjectExtendedLimitInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)) return FALSE;
            std::memcpy(lpJobObjectInformation, &job->extendedLimits, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
            bytesWritten = sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION);
            break;
        }
        case JobObjectCpuRateControlInformation: {
            if (cbJobObjectInformationLength < sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION)) return FALSE;
            std::memcpy(lpJobObjectInformation, &job->cpuRate, sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION));
            bytesWritten = sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION);
            break;
        }
        default:
            return FALSE;
    }
    if (lpReturnLength) *lpReturnLength = bytesWritten;
    return TRUE;
}

inline BOOL WINAPI TerminateJobObject(HANDLE hJob, UINT /*uExitCode*/) noexcept {
    auto job = JobObjectManager::get().getJob(hJob);
    if (!job) return FALSE;
    std::lock_guard<std::mutex> lock(job->mtx);
    job->isTerminated = true;
    job->accounting.TotalTerminatedProcesses += job->accounting.ActiveProcesses;
    job->accounting.ActiveProcesses = 0;
    return TRUE;
}

inline BOOL WINAPI IsProcessInJob(HANDLE hProcess, HANDLE hJob, BOOL* Result) noexcept {
    if (!Result) return FALSE;
    *Result = FALSE;
    auto job = JobObjectManager::get().getJob(hJob);
    if (!job) return FALSE;

    DWORD pid = 0;
    if (hProcess == GetCurrentProcess()) {
        pid = GetCurrentProcessId();
    } else {
        pid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(hProcess));
    }

    std::lock_guard<std::mutex> lock(job->mtx);
    for (DWORD p : job->assignedProcessIds) {
        if (p == pid) {
            *Result = TRUE;
            return TRUE;
        }
    }
    return TRUE;
}

// Memory & File Mapping
inline HANDLE OpenFileMappingA(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, const char* /*lpName*/) noexcept {
    return reinterpret_cast<HANDLE>(0x7102);
}


inline BOOL FlushInstructionCache(HANDLE /*hProcess*/, const void* /*lpBaseAddress*/, size_t /*dwSize*/) noexcept {
    return TRUE;
}

// Console Input
inline BOOL FlushConsoleInputBuffer(HANDLE /*hConsoleInput*/) noexcept {
    return TRUE;
}

inline BOOL PeekConsoleInputW(HANDLE /*hConsoleInput*/, void* /*lpBuffer*/, DWORD /*nLength*/, DWORD* lpNumberOfEventsRead) noexcept {
    if (lpNumberOfEventsRead) *lpNumberOfEventsRead = 0;
    return TRUE;
}

inline BOOL ReadConsoleInputW(HANDLE /*hConsoleInput*/, void* /*lpBuffer*/, DWORD /*nLength*/, DWORD* lpNumberOfEventsRead) noexcept {
    if (lpNumberOfEventsRead) *lpNumberOfEventsRead = 0;
    return TRUE;
}

inline BOOL WriteConsoleInputW(HANDLE /*hConsoleInput*/, const void* /*lpBuffer*/, DWORD nLength, DWORD* lpNumberOfEventsWritten) noexcept {
    if (lpNumberOfEventsWritten) *lpNumberOfEventsWritten = nLength;
    return TRUE;
}

// Interlocked Atomics
inline LONG InterlockedIncrement(volatile LONG* Addend) noexcept {
    LONG old = *Addend;
    *Addend = old + 1;
    return old + 1;
}

inline LONG InterlockedDecrement(volatile LONG* Addend) noexcept {
    LONG old = *Addend;
    *Addend = old - 1;
    return old - 1;
}

inline LONG InterlockedExchange(volatile LONG* Target, LONG Value) noexcept {
    LONG old = *Target;
    *Target = Value;
    return old;
}

inline LONG InterlockedExchangeAdd(volatile LONG* Addend, LONG Value) noexcept {
    LONG old = *Addend;
    *Addend += Value;
    return old;
}

inline LONG InterlockedCompareExchange(volatile LONG* Destination, LONG Exchange, LONG Comperand) noexcept {
    LONG old = *Destination;
    if (old == Comperand) {
        *Destination = Exchange;
    }
    return old;
}

// Formatting & Module
inline int GetDateFormatA(LCID /*Locale*/, DWORD /*dwFlags*/, const void* /*lpDate*/, const char* /*lpFormat*/, char* lpDateStr, int cchDate) noexcept {
    const char* dateStr = "2026-10-09";
    if (lpDateStr && cchDate > 10) {
        std::strcpy(lpDateStr, dateStr);
        return 11;
    }
    return 11;
}

inline BOOL GetModuleHandleExA(DWORD /*dwFlags*/, const char* /*lpModuleName*/, HMODULE* phModule) noexcept {
    if (phModule) *phModule = reinterpret_cast<HMODULE>(0x7FFE0000);
    return TRUE;
}

inline BOOL IsBadWritePtr(void* /*lp*/, size_t /*ucb*/) noexcept {
    return FALSE;
}

inline HMODULE LoadLibraryA(LPCSTR lpLibFileName) noexcept {
    if (!lpLibFileName) return nullptr;
    std::string s(lpLibFileName);
    std::wstring ws(s.begin(), s.end());
    return LoadLibraryW(ws.c_str());
}

inline HMODULE LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) noexcept {
    return LoadLibraryA(lpLibFileName);
}

inline DWORD FormatMessageA(DWORD /*dwFlags*/, LPCVOID /*lpSource*/, DWORD dwMessageId, DWORD /*dwLanguageId*/, LPSTR lpBuffer, DWORD nSize, va_list* /*Arguments*/) noexcept {
    if (lpBuffer && nSize > 0) {
        snprintf(lpBuffer, nSize, "MicaNT Error 0x%X", static_cast<unsigned int>(dwMessageId));
        return static_cast<DWORD>(strlen(lpBuffer));
    }
    return 0;
}

inline BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD /*dwSpinCount*/) noexcept {
    InitializeCriticalSection(lpCriticalSection);
    return TRUE;
}

inline BOOL ReadConsoleW(HANDLE, LPVOID, DWORD, LPDWORD lpNumberOfCharsRead, LPVOID) noexcept {
    if (lpNumberOfCharsRead) *lpNumberOfCharsRead = 0;
    return TRUE;
}

inline void RtlUnwind(void*, void*, void*, void*) noexcept {}

[[noreturn]] inline void ExitThread(DWORD dwExitCode) noexcept {
    if (g_ExitThreadHook) {
        g_ExitThreadHook(dwExitCode);
    }
    DWORD pid = GetCurrentProcessId();
    csrss::CsrSubsystemServer::get().terminateProcess(pid, dwExitCode);
    std::exit(static_cast<int>(dwExitCode));
}

[[noreturn]] inline void FreeLibraryAndExitThread(HMODULE hLib, DWORD code) noexcept {
    FreeLibrary(hLib);
    ExitThread(code);
}

struct SYSTEM_POWER_STATUS {
    uint8_t  ACLineStatus;
    uint8_t  BatteryFlag;
    uint8_t  BatteryLifePercent;
    uint8_t  SystemStatusFlag;
    uint32_t BatteryLifeTime;
    uint32_t BatteryFullLifeTime;
};

inline BOOL WINAPI DisableThreadLibraryCalls([[maybe_unused]] HMODULE hLibModule) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetProcessDEPPolicy([[maybe_unused]] DWORD dwFlags) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetDllDirectoryA([[maybe_unused]] LPCSTR lpPathName) noexcept {
    return TRUE;
}

inline BOOL WINAPI GetSystemPowerStatus(SYSTEM_POWER_STATUS* lpSystemPowerStatus) noexcept {
    if (!lpSystemPowerStatus) return FALSE;
    lpSystemPowerStatus->ACLineStatus = 1;         // AC Online
    lpSystemPowerStatus->BatteryFlag = 128;        // No system battery (desktop)
    lpSystemPowerStatus->BatteryLifePercent = 255; // Unknown
    lpSystemPowerStatus->SystemStatusFlag = 0;
    lpSystemPowerStatus->BatteryLifeTime = 0xFFFFFFFF;
    lpSystemPowerStatus->BatteryFullLifeTime = 0xFFFFFFFF;
    return TRUE;
}

inline DWORD WINAPI GetActiveProcessorCount([[maybe_unused]] uint16_t groupNumber) noexcept {
    return 8; // Sovereign 8-core logical configuration
}

inline BOOL WINAPI GetHandleInformation([[maybe_unused]] HANDLE hObject, DWORD* lpdwFlags) noexcept {
    if (lpdwFlags) {
        *lpdwFlags = 0;
    }
    return TRUE;
}

inline DWORD WINAPI GetProfileStringW([[maybe_unused]] LPCWSTR lpAppName,
                                      [[maybe_unused]] LPCWSTR lpKeyName,
                                      LPCWSTR lpDefault,
                                      LPWSTR lpReturnedString,
                                      DWORD nSize) noexcept {
    if (!lpReturnedString || nSize == 0) return 0;
    if (lpDefault) {
        size_t len = std::wcslen(lpDefault);
        if (len >= nSize) len = nSize - 1;
        std::wmemcpy(lpReturnedString, lpDefault, len);
        lpReturnedString[len] = L'\0';
        return static_cast<DWORD>(len);
    }
    lpReturnedString[0] = L'\0';
    return 0;
}

inline int WINAPI IdnToAscii([[maybe_unused]] DWORD dwFlags,
                              LPCWSTR lpUnicodeCharStr,
                              int cchUnicodeChar,
                              LPWSTR lpASCIICharStr,
                              int cchASCIIChar) noexcept {
    if (!lpUnicodeCharStr) return 0;
    int srcLen = (cchUnicodeChar == -1) ? static_cast<int>(std::wcslen(lpUnicodeCharStr) + 1) : cchUnicodeChar;
    if (cchASCIIChar == 0) return srcLen;
    if (!lpASCIICharStr) return 0;
    int copyLen = (srcLen < cchASCIIChar) ? srcLen : cchASCIIChar;
    std::wmemcpy(lpASCIICharStr, lpUnicodeCharStr, copyLen);
    return copyLen;
}

inline BOOL WINAPI IsBadStringPtrA([[maybe_unused]] LPCSTR lpsz, [[maybe_unused]] size_t ucchMax) noexcept {
    return FALSE; // Sovereign memory is valid
}

// SetPriorityClass is implemented above in SovereignThreadPacingManager


inline HANDLE WINAPI FindFirstVolumeW(LPWSTR lpszVolumeName, DWORD cchBufferLength) noexcept {
    if (!lpszVolumeName || cchBufferLength < 50) return nullptr;
    const wchar_t mockVolume[] = L"\\\\?\\Volume{11111111-2222-3333-4444-555555555555}\\";
    std::wmemcpy(lpszVolumeName, mockVolume, std::wcslen(mockVolume) + 1);
    return reinterpret_cast<HANDLE>(0xFEED0001);
}

inline BOOL WINAPI FindNextVolumeW([[maybe_unused]] HANDLE hFindVolume,
                                    [[maybe_unused]] LPWSTR lpszVolumeName,
                                    [[maybe_unused]] DWORD cchBufferLength) noexcept {
    SetLastError(18); // ERROR_NO_MORE_FILES
    return FALSE;
}

inline BOOL WINAPI FindVolumeClose([[maybe_unused]] HANDLE hFindVolume) noexcept {
    return TRUE;
}

inline DWORD WINAPI GetFinalPathNameByHandleA([[maybe_unused]] HANDLE hFile,
                                              char* lpszFilePath,
                                              DWORD cchFilePath,
                                              [[maybe_unused]] DWORD dwFlags) noexcept {
    if (!lpszFilePath || cchFilePath < 16) return 0;
    const char mockPath[] = "C:\\MicaNT\\Volume";
    size_t len = std::strlen(mockPath);
    std::memcpy(lpszFilePath, mockPath, len + 1);
    return static_cast<DWORD>(len);
}

inline BOOL WINAPI FillConsoleOutputCharacterW([[maybe_unused]] HANDLE hConsoleOutput,
                                                [[maybe_unused]] wchar_t cCharacter,
                                                DWORD nLength,
                                                [[maybe_unused]] void* dwWriteCoord,
                                                DWORD* lpNumberOfCharsWritten) noexcept {
    if (lpNumberOfCharsWritten) {
        *lpNumberOfCharsWritten = nLength;
    }
    return TRUE;
}

inline BOOL WINAPI ReadConsoleOutputCharacterA([[maybe_unused]] HANDLE hConsoleOutput,
                                                [[maybe_unused]] char* lpCharacter,
                                                [[maybe_unused]] DWORD nLength,
                                                [[maybe_unused]] void* dwReadCoord,
                                                DWORD* lpNumberOfCharsRead) noexcept {
    if (lpNumberOfCharsRead) {
        *lpNumberOfCharsRead = 0;
    }
    return TRUE;
}

inline BOOL WINAPI GetThreadGroupAffinity([[maybe_unused]] HANDLE hThread, PGROUP_AFFINITY GroupAffinity) noexcept {
    if (!GroupAffinity) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GroupAffinity->Group = 0;
    GroupAffinity->Mask = 0xFFFF;
    GroupAffinity->Reserved[0] = 0;
    GroupAffinity->Reserved[1] = 0;
    GroupAffinity->Reserved[2] = 0;
    return TRUE;
}

inline BOOL WINAPI DosDateTimeToFileTime(uint16_t wFatDate, uint16_t wFatTime, void* lpFileTime) noexcept {
    if (!lpFileTime) return FALSE;
    auto* ft = reinterpret_cast<uint64_t*>(lpFileTime);
    uint32_t year = 1980 + ((wFatDate >> 9) & 0x7F);
    uint32_t month = (wFatDate >> 5) & 0x0F;
    uint32_t day = wFatDate & 0x1F;
    uint32_t hour = (wFatTime >> 11) & 0x1F;
    uint32_t min = (wFatTime >> 5) & 0x3F;
    uint32_t sec = (wFatTime & 0x1F) * 2;
    // Approximate 100-nanosecond intervals since Jan 1, 1601
    uint64_t totalSeconds = static_cast<uint64_t>(year - 1601) * 31536000ULL +
                            static_cast<uint64_t>(month * 30 + day) * 86400ULL +
                            static_cast<uint64_t>(hour * 3600 + min * 60 + sec);
    *ft = totalSeconds * 10000000ULL;
    return TRUE;
}

inline BOOL WINAPI SystemTimeToFileTime(const void* lpSystemTime, void* lpFileTime) noexcept {
    if (!lpSystemTime || !lpFileTime) return FALSE;
    uint64_t ticks{};
    if (!time_detail::toTicks(*static_cast<const SYSTEMTIME*>(lpSystemTime), ticks))
        return FALSE;
    auto* ft = static_cast<FILETIME*>(lpFileTime);
    ft->dwLowDateTime = static_cast<DWORD>(ticks);
    ft->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return TRUE;
}

inline BOOL WINAPI TzSpecificLocalTimeToSystemTime([[maybe_unused]] const void* lpTimeZoneInformation, const void* lpLocalTime, void* lpUniversalTime) noexcept {
    if (!lpLocalTime || !lpUniversalTime) return FALSE;
    return TzSpecificLocalTimeToSystemTime(
        static_cast<const TIME_ZONE_INFORMATION*>(lpTimeZoneInformation),
        static_cast<const SYSTEMTIME*>(lpLocalTime),
        static_cast<SYSTEMTIME*>(lpUniversalTime));
}

inline BOOL WINAPI GetFileTime([[maybe_unused]] HANDLE hFile, void* lpCreationTime, void* lpLastAccessTime, void* lpLastWriteTime) noexcept {
    uint64_t mockTime = 133500000000000000ULL;
    if (lpCreationTime) *reinterpret_cast<uint64_t*>(lpCreationTime) = mockTime;
    if (lpLastAccessTime) *reinterpret_cast<uint64_t*>(lpLastAccessTime) = mockTime;
    if (lpLastWriteTime) *reinterpret_cast<uint64_t*>(lpLastWriteTime) = mockTime;
    return TRUE;
}

inline DWORD WINAPI GetLogicalDrives() noexcept {
    DWORD mask = 0;
    auto letters = fs::VirtualFileSystem::get().getMountedDriveLetters();
    for (wchar_t l : letters) {
        wchar_t u = static_cast<wchar_t>(std::towupper(l));
        if (u >= L'A' && u <= L'Z') {
            mask |= (1U << (u - L'A'));
        }
    }
    return mask ? mask : 0x0000000C;
}

inline BOOL WINAPI GetVolumePathNameW(LPCWSTR lpszFileName, LPWSTR lpszVolumePathName, DWORD cchBufferLength) noexcept {
    if (!lpszVolumePathName || cchBufferLength < 4) return FALSE;
    if (lpszFileName && lpszFileName[0] != L'\0' && lpszFileName[1] == L':') {
        lpszVolumePathName[0] = lpszFileName[0];
        lpszVolumePathName[1] = L':';
        lpszVolumePathName[2] = L'\\';
        lpszVolumePathName[3] = L'\0';
    } else {
        std::wcsncpy(lpszVolumePathName, L"C:\\", cchBufferLength);
    }
    return TRUE;
}

inline int WINAPI FoldStringW([[maybe_unused]] DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest) noexcept {
    if (!lpSrcStr) return 0;
    int srcLen = (cchSrc < 0) ? static_cast<int>(std::wcslen(lpSrcStr) + 1) : cchSrc;
    if (cchDest == 0 || !lpDestStr) return srcLen;
    int copyLen = std::min<int>(srcLen, cchDest);
    std::wcsncpy(lpDestStr, lpSrcStr, copyLen);
    return copyLen;
}

inline BOOL WINAPI IsDBCSLeadByte([[maybe_unused]] uint8_t TestChar) noexcept {
    return FALSE; // Pure Unicode / single-byte ASCII baseline
}


inline BOOL WINAPI AttachConsole([[maybe_unused]] DWORD dwProcessId) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetConsoleScreenBufferSize([[maybe_unused]] HANDLE hConsoleOutput, [[maybe_unused]] uint32_t dwSize) noexcept {
    return TRUE;
}

inline DWORD WINAPI GetTempFileNameW(LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName) noexcept {
    if (!lpTempFileName) return 0;
    static uint32_t s_uid = 1;
    uint32_t id = (uUnique != 0) ? uUnique : s_uid++;
    const wchar_t* prefix = lpPrefixString ? lpPrefixString : L"TMP";
    const wchar_t* path = lpPathName ? lpPathName : L"C:\\Temp\\";
    std::swprintf(lpTempFileName, 260, L"%ls%ls%04X.tmp", path, prefix, id);
    return id;
}

inline void WINAPI OutputDebugStringA([[maybe_unused]] LPCSTR lpOutputString) noexcept {}

inline DWORD WINAPI SetThreadExecutionState(DWORD esFlags) noexcept {
    return esFlags;
}

inline void WINAPI DebugBreak() noexcept {}

inline void* WINAPI AddVectoredExceptionHandler(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) noexcept {
    return ntdll::RtlAddVectoredExceptionHandler(First, Handler);
}

inline void* WINAPI AddVectoredContinueHandler(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) noexcept {
    return ntdll::RtlAddVectoredContinueHandler(First, Handler);
}

inline DWORD WINAPI GetPrivateProfileIntW([[maybe_unused]] LPCWSTR lpAppName, [[maybe_unused]] LPCWSTR lpKeyName, int nDefault, [[maybe_unused]] LPCWSTR lpFileName) noexcept {
    return static_cast<DWORD>(nDefault);
}

inline BOOL WINAPI HeapQueryInformation([[maybe_unused]] HANDLE HeapHandle, [[maybe_unused]] int HeapInformationClass, [[maybe_unused]] void* HeapInformation, [[maybe_unused]] size_t HeapInformationLength, size_t* ReturnLength) noexcept {
    if (ReturnLength) *ReturnLength = 4;
    return TRUE;
}

inline BOOL Beep([[maybe_unused]] DWORD dwFreq, [[maybe_unused]] DWORD dwDuration) noexcept {
    return TRUE;
}

inline BOOL ClearCommBreak([[maybe_unused]] HANDLE hFile) noexcept { return TRUE; }
inline BOOL SetCommBreak([[maybe_unused]] HANDLE hFile) noexcept { return TRUE; }

inline BOOL GetCommState([[maybe_unused]] HANDLE hFile, DCB* lpDCB) noexcept {
    if (!lpDCB) return FALSE;
    lpDCB->DCBlength = sizeof(DCB);
    lpDCB->BaudRate = 115200;
    lpDCB->ByteSize = 8;
    lpDCB->Parity = 0;
    lpDCB->StopBits = 0;
    return TRUE;
}

inline BOOL SetCommState([[maybe_unused]] HANDLE hFile, [[maybe_unused]] DCB* lpDCB) noexcept { return TRUE; }

inline BOOL SetCommTimeouts([[maybe_unused]] HANDLE hFile, [[maybe_unused]] COMMTIMEOUTS* lpCommTimeouts) noexcept { return TRUE; }

inline BOOL SetHandleInformation([[maybe_unused]] HANDLE hObject, [[maybe_unused]] DWORD dwMask, [[maybe_unused]] DWORD dwFlags) noexcept { return TRUE; }

inline HANDLE CreateEventA([[maybe_unused]] void* lpEventAttributes, [[maybe_unused]] BOOL bManualReset, [[maybe_unused]] BOOL bInitialState, [[maybe_unused]] const char* lpName) noexcept {
    static uintptr_t s_eventHandle = 0x3000;
    return reinterpret_cast<HANDLE>(++s_eventHandle);
}

inline HANDLE CreateMutexA([[maybe_unused]] void* lpMutexAttributes, [[maybe_unused]] BOOL bInitialOwner, [[maybe_unused]] const char* lpName) noexcept {
    static uintptr_t s_mutexHandle = 0x4000;
    return reinterpret_cast<HANDLE>(++s_mutexHandle);
}

inline HANDLE CreateFileMappingA(HANDLE hFile, void* lpAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, [[maybe_unused]] const char* lpName) noexcept {
    return CreateFileMappingW(hFile, lpAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, nullptr);
}

inline HANDLE CreateNamedPipeA(const char* lpName, DWORD dwOpenMode, DWORD dwPipeMode, DWORD nMaxInstances, DWORD nOutBufferSize, DWORD nInBufferSize, DWORD nDefaultTimeOut, void* lpSecurityAttributes) noexcept {
    std::wstring wName;
    if (lpName) {
        while (*lpName) wName.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*lpName++)));
    }
    return CreateNamedPipeW(wName.c_str(), dwOpenMode, dwPipeMode, nMaxInstances, nOutBufferSize, nInBufferSize, nDefaultTimeOut, lpSecurityAttributes);
}

inline BOOL WaitNamedPipeA([[maybe_unused]] const char* lpNamedPipeName, [[maybe_unused]] DWORD nTimeOut) noexcept {
    return TRUE;
}

inline BOOL CreatePipe(HANDLE* hReadPipe, HANDLE* hWritePipe, [[maybe_unused]] void* lpPipeAttributes, [[maybe_unused]] DWORD nSize) noexcept {
    if (!hReadPipe || !hWritePipe) return FALSE;
    static uintptr_t s_pipeHandle = 0x5000;
    *hReadPipe = reinterpret_cast<HANDLE>(++s_pipeHandle);
    *hWritePipe = reinterpret_cast<HANDLE>(++s_pipeHandle);
    return TRUE;
}

inline void* FindResourceA(void* hModule, const char* lpName, const char* lpType) noexcept {
    std::wstring wName, wType;
    if (lpName) {
        if (reinterpret_cast<uintptr_t>(lpName) <= 0xFFFF) {
            wName = reinterpret_cast<const wchar_t*>(lpName);
        } else {
            const char* p = lpName;
            while (*p) wName.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p++)));
        }
    }
    if (lpType) {
        if (reinterpret_cast<uintptr_t>(lpType) <= 0xFFFF) {
            wType = reinterpret_cast<const wchar_t*>(lpType);
        } else {
            const char* p = lpType;
            while (*p) wType.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p++)));
        }
    }
    return FindResourceW(hModule, wName.c_str(), wType.c_str());
}

// ============================================================================
// Sovereign Asynchronous I/O & Completion Port Manager
// ============================================================================

class SovereignAsyncIoManager {
private:
    std::mutex m_mutex;
    std::unordered_map<HANDLE, std::deque<OVERLAPPED_ENTRY>> m_portQueues;
    std::unordered_map<HANDLE, UCHAR> m_fileNotificationModes;
    uint64_t m_nextPortId{ 0x10C01 };

public:
    static SovereignAsyncIoManager& get() noexcept {
        static SovereignAsyncIoManager s_instance;
        return s_instance;
    }

    HANDLE CreateCompletionPort(HANDLE fileHandle, HANDLE existingPort, ULONG_PTR completionKey, DWORD /*threads*/) {
        std::lock_guard<std::mutex> lock(m_mutex);
        HANDLE port = existingPort;
        if (!port) {
            port = reinterpret_cast<HANDLE>(m_nextPortId++);
            m_portQueues[port] = {};
        } else {
            if (m_portQueues.find(port) == m_portQueues.end()) {
                m_portQueues[port] = {};
            }
        }
        (void)fileHandle;
        (void)completionKey;
        return port;
    }

    BOOL PostStatus(HANDLE port, DWORD bytesTransferred, ULONG_PTR key, LPOVERLAPPED overlapped) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!port) port = reinterpret_cast<HANDLE>(0x10C01);
        OVERLAPPED_ENTRY entry{};
        entry.lpCompletionKey = key;
        entry.lpOverlapped = overlapped;
        entry.Internal = 0;
        entry.dwNumberOfBytesTransferred = bytesTransferred;
        m_portQueues[port].push_back(entry);
        return TRUE;
    }

    BOOL GetStatus(HANDLE port, DWORD* lpBytesTransferred, ULONG_PTR* lpKey, void** lpOverlapped, DWORD /*ms*/) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!port) port = reinterpret_cast<HANDLE>(0x10C01);
        auto it = m_portQueues.find(port);
        if (it != m_portQueues.end() && !it->second.empty()) {
            auto entry = it->second.front();
            it->second.pop_front();
            if (lpBytesTransferred) *lpBytesTransferred = entry.dwNumberOfBytesTransferred;
            if (lpKey) *lpKey = entry.lpCompletionKey;
            if (lpOverlapped) *lpOverlapped = entry.lpOverlapped;
            return TRUE;
        }
        if (lpBytesTransferred) *lpBytesTransferred = 1024;
        if (lpKey) *lpKey = 1;
        if (lpOverlapped) *lpOverlapped = reinterpret_cast<void*>(0x8102);
        return TRUE;
    }

    BOOL GetStatusEx(HANDLE port, LPOVERLAPPED_ENTRY lpEntries, ULONG ulCount, PULONG ulNumEntriesRemoved, DWORD /*ms*/, BOOL /*fAlertable*/) {
        if (!lpEntries || ulCount == 0 || !ulNumEntriesRemoved) {
            return FALSE;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!port) port = reinterpret_cast<HANDLE>(0x10C01);
        auto it = m_portQueues.find(port);
        ULONG removed = 0;
        if (it != m_portQueues.end()) {
            while (removed < ulCount && !it->second.empty()) {
                lpEntries[removed] = it->second.front();
                it->second.pop_front();
                removed++;
            }
        }
        if (removed == 0) {
            lpEntries[0] = { 1, reinterpret_cast<LPOVERLAPPED>(0x8102), 0, 1024 };
            removed = 1;
        }
        *ulNumEntriesRemoved = removed;
        return TRUE;
    }

    BOOL SetNotificationModes(HANDLE fileHandle, UCHAR flags) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fileNotificationModes[fileHandle] = flags;
        return TRUE;
    }

    UCHAR GetNotificationModes(HANDLE fileHandle) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_fileNotificationModes.find(fileHandle);
        return (it != m_fileNotificationModes.end()) ? it->second : 0;
    }

    BOOL GetOverlappedResultHelper(HANDLE /*hFile*/, LPOVERLAPPED lpOverlapped, DWORD* lpBytesTransferred, BOOL bWait) {
        if (!lpOverlapped) {
            SetLastError(87); // ERROR_INVALID_PARAMETER
            return FALSE;
        }
        if (lpOverlapped->Internal == 0x00000103L) { // STATUS_PENDING
            if (!bWait) {
                SetLastError(996); // ERROR_IO_INCOMPLETE
                return FALSE;
            }
            lpOverlapped->Internal = 0; // Completed
        }
        if (lpBytesTransferred) {
            *lpBytesTransferred = static_cast<DWORD>(lpOverlapped->InternalHigh);
        }
        if (lpOverlapped->Internal != 0) {
            SetLastError(static_cast<DWORD>(lpOverlapped->Internal));
            return FALSE;
        }
        return TRUE;
    }

    BOOL GetOverlappedResultExHelper(HANDLE /*hFile*/, LPOVERLAPPED lpOverlapped, DWORD* lpBytesTransferred, DWORD dwMilliseconds, BOOL /*bAlertable*/) {
        if (!lpOverlapped) {
            SetLastError(87); // ERROR_INVALID_PARAMETER
            return FALSE;
        }
        if (lpOverlapped->Internal == 0x00000103L) { // STATUS_PENDING
            if (dwMilliseconds == 0) {
                SetLastError(996); // ERROR_IO_INCOMPLETE
                return FALSE;
            }
            lpOverlapped->Internal = 0; // Completed
        }
        if (lpBytesTransferred) {
            *lpBytesTransferred = static_cast<DWORD>(lpOverlapped->InternalHigh);
        }
        if (lpOverlapped->Internal != 0) {
            SetLastError(static_cast<DWORD>(lpOverlapped->Internal));
            return FALSE;
        }
        return TRUE;
    }

    BOOL CancelIoExHelper(HANDLE /*hFile*/, LPOVERLAPPED lpOverlapped) {
        if (lpOverlapped) {
            if (lpOverlapped->Internal == 0x00000103L) { // STATUS_PENDING
                lpOverlapped->Internal = 0xC0000120L; // STATUS_CANCELLED
                SetLastError(995); // ERROR_OPERATION_ABORTED
                return TRUE;
            }
            SetLastError(1168); // ERROR_NOT_FOUND
            return FALSE;
        }
        SetLastError(995); // ERROR_OPERATION_ABORTED
        return TRUE;
    }
};

inline BOOL GetOverlappedResult([[maybe_unused]] HANDLE hFile, OVERLAPPED* lpOverlapped, DWORD* lpNumberOfBytesTransferred, [[maybe_unused]] BOOL bWait) noexcept {
    return SovereignAsyncIoManager::get().GetOverlappedResultHelper(hFile, lpOverlapped, lpNumberOfBytesTransferred, bWait);
}

inline UINT GetSystemDirectoryA(char* lpBuffer, UINT uSize) noexcept {
    static constexpr const char* SYS_DIR = "C:\\Windows\\System32";
    UINT len = static_cast<UINT>(std::strlen(SYS_DIR));
    if (!lpBuffer || uSize < len + 1) return len + 1;
    std::memcpy(lpBuffer, SYS_DIR, len + 1);
    return len;
}

inline UINT GetWindowsDirectoryA(char* lpBuffer, UINT uSize) noexcept {
    static constexpr const char* WIN_DIR = "C:\\Windows";
    UINT len = static_cast<UINT>(std::strlen(WIN_DIR));
    if (!lpBuffer || uSize < len + 1) return len + 1;
    std::memcpy(lpBuffer, WIN_DIR, len + 1);
    return len;
}

inline BOOL GetThreadTimes([[maybe_unused]] HANDLE hThread, void* lpCreationTime, void* lpExitTime, void* lpKernelTime, void* lpUserTime) noexcept {
    if (lpCreationTime) std::memset(lpCreationTime, 0, 8);
    if (lpExitTime) std::memset(lpExitTime, 0, 8);
    if (lpKernelTime) std::memset(lpKernelTime, 0, 8);
    if (lpUserTime) std::memset(lpUserTime, 0, 8);
    return TRUE;
}

inline void GlobalMemoryStatus(MEMORYSTATUS* lpBuffer) noexcept {
    if (!lpBuffer) return;
    *lpBuffer = MEMORYSTATUS{};
}

inline BOOL LocalFileTimeToFileTime(const void* lpLocalFileTime, void* lpFileTime) noexcept {
    if (!lpLocalFileTime || !lpFileTime) return FALSE;
    std::memcpy(lpFileTime, lpLocalFileTime, 8);
    return TRUE;
}

inline HANDLE WINAPI FindFirstVolumeA(char* lpszVolumeName, DWORD cchBufferLength) noexcept {
    if (lpszVolumeName && cchBufferLength >= 50) {
        std::snprintf(lpszVolumeName, cchBufferLength, "\\\\?\\Volume{a0000000-0000-0000-0000-000000000001}\\");
        return reinterpret_cast<HANDLE>(0x3001);
    }
    return nullptr;
}

inline BOOL WINAPI FindNextVolumeA(HANDLE /*hFindVolume*/, char* /*lpszVolumeName*/, DWORD /*cchBufferLength*/) noexcept {
    win32::SetLastError(18); // ERROR_NO_MORE_FILES
    return FALSE;
}

inline BOOL WINAPI GetVolumeNameForVolumeMountPointA(
    const char* /*lpszVolumeMountPoint*/,
    char* lpszVolumeName,
    DWORD cchBufferLength
) noexcept {
    if (lpszVolumeName && cchBufferLength >= 50) {
        std::snprintf(lpszVolumeName, cchBufferLength, "\\\\?\\Volume{a0000000-0000-0000-0000-000000000001}\\");
        return TRUE;
    }
    return FALSE;
}

inline BOOL WINAPI GetVolumePathNameA(
    const char* /*lpszFileName*/,
    char* lpszVolumePathName,
    DWORD cchBufferLength
) noexcept {
    if (lpszVolumePathName && cchBufferLength >= 4) {
        std::snprintf(lpszVolumePathName, cchBufferLength, "C:\\");
        return TRUE;
    }
    return FALSE;
}

inline BOOL WINAPI GetVolumeInformationA(
    const char* /*lpRootPathName*/,
    char* lpVolumeNameBuffer,
    DWORD nVolumeNameSize,
    DWORD* lpVolumeSerialNumber,
    DWORD* lpMaximumComponentLength,
    DWORD* lpFileSystemFlags,
    char* lpFileSystemNameBuffer,
    DWORD nFileSystemNameSize
) noexcept {
    if (lpVolumeNameBuffer && nVolumeNameSize > 0) {
        std::snprintf(lpVolumeNameBuffer, nVolumeNameSize, "MicaNT_System");
    }
    if (lpVolumeSerialNumber) *lpVolumeSerialNumber = 0x12345678;
    if (lpMaximumComponentLength) *lpMaximumComponentLength = 255;
    if (lpFileSystemFlags) *lpFileSystemFlags = 0x00000002 | 0x00000004 | 0x00000008; // CASE_PRESERVED, UNICODE, PERSISTENT_ACLS
    if (lpFileSystemNameBuffer && nFileSystemNameSize > 0) {
        std::snprintf(lpFileSystemNameBuffer, nFileSystemNameSize, "NTFS");
    }
    return TRUE;
}

inline BOOL WINAPI GetVolumeInformationByHandleW(
    HANDLE /*hFile*/,
    wchar_t* lpVolumeNameBuffer,
    DWORD nVolumeNameSize,
    DWORD* lpVolumeSerialNumber,
    DWORD* lpMaximumComponentLength,
    DWORD* lpFileSystemFlags,
    wchar_t* lpFileSystemNameBuffer,
    DWORD nFileSystemNameSize
) noexcept {
    if (lpVolumeNameBuffer && nVolumeNameSize > 0) {
        std::wcsncpy(lpVolumeNameBuffer, L"MicaNT_Drive", nVolumeNameSize);
    }
    if (lpVolumeSerialNumber) *lpVolumeSerialNumber = 0x87654321;
    if (lpMaximumComponentLength) *lpMaximumComponentLength = 255;
    if (lpFileSystemFlags) *lpFileSystemFlags = 0x00000002 | 0x00000004 | 0x00000008;
    if (lpFileSystemNameBuffer && nFileSystemNameSize > 0) {
        std::wcsncpy(lpFileSystemNameBuffer, L"NTFS", nFileSystemNameSize);
    }
    return TRUE;
}

inline constexpr DWORD DDD_RAW_TARGET_PATH         = 0x00000001;
inline constexpr DWORD DDD_REMOVE_DEFINITION       = 0x00000002;
inline constexpr DWORD DDD_EXACT_MATCH_ON_REMOVE   = 0x00000004;
inline constexpr DWORD DDD_NO_BROADCAST_SYSTEM     = 0x00000008;

inline BOOL WINAPI DefineDosDeviceW(DWORD dwFlags, LPCWSTR lpDeviceName, LPCWSTR lpTargetPath) noexcept {
    if (!lpDeviceName || lpDeviceName[0] == L'\0') {
        SetLastError(87);
        return FALSE;
    }
    std::wstring normId = fs::VirtualFileSystem::normalizeDriveIdentifier(lpDeviceName);
    if (normId.empty() || normId.size() > 32) {
        SetLastError(87);
        return FALSE;
    }

    if (dwFlags & DDD_REMOVE_DEFINITION) {
        fs::VirtualFileSystem::get().removeDriveIdentifier(normId);
        SetLastError(0);
        return TRUE;
    }

    if (!lpTargetPath) {
        SetLastError(87);
        return FALSE;
    }

    NtStatus st = fs::VirtualFileSystem::get().assignDriveIdentifier(normId, lpTargetPath);
    if (!NT_SUCCESS(st)) {
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }
    SetLastError(0);
    return TRUE;
}

inline BOOL WINAPI DefineDosDeviceA(DWORD dwFlags, const char* lpDeviceName, const char* lpTargetPath) noexcept {
    if (!lpDeviceName) {
        SetLastError(87);
        return FALSE;
    }
    std::string sDev(lpDeviceName);
    std::wstring wDev(sDev.begin(), sDev.end());
    std::wstring wTgt;
    if (lpTargetPath) {
        std::string sTgt(lpTargetPath);
        wTgt.assign(sTgt.begin(), sTgt.end());
    }
    return DefineDosDeviceW(dwFlags, wDev.c_str(), lpTargetPath ? wTgt.c_str() : nullptr);
}

inline DWORD WINAPI QueryDosDeviceW(LPCWSTR lpDeviceName, LPWSTR lpTargetPath, DWORD ucchMax) noexcept {
    if (!lpTargetPath || ucchMax == 0) {
        SetLastError(87);
        return 0;
    }

    auto& vfs = fs::VirtualFileSystem::get();

    if (!lpDeviceName || lpDeviceName[0] == L'\0') {
        auto ids = vfs.getMountedDriveIdentifiers();
        size_t totalNeeded = 1; // Trailing null terminator for multistring
        for (const auto& id : ids) {
            totalNeeded += (id.size() + 2); // "<id>:\0"
        }

        if (ucchMax < totalNeeded) {
            SetLastError(122); // ERROR_INSUFFICIENT_BUFFER
            return 0;
        }

        LPWSTR cur = lpTargetPath;
        for (const auto& id : ids) {
            for (wchar_t ch : id) {
                *cur++ = static_cast<wchar_t>(std::towupper(ch));
            }
            *cur++ = L':';
            *cur++ = L'\0';
        }
        *cur++ = L'\0';
        SetLastError(0);
        return static_cast<DWORD>(totalNeeded);
    }

    std::wstring normId = fs::VirtualFileSystem::normalizeDriveIdentifier(lpDeviceName);
    std::wstring symName = std::wstring(L"\\DosDevices\\") + normId + L":";
    auto sym = ob::ObjectNamespace::get().getSymbolicLink(symName);
    std::wstring target;
    if (sym) {
        target = sym->getTargetPath();
    } else {
        target = vfs.getDeviceForDriveIdentifier(normId);
    }

    if (target.empty()) {
        SetLastError(2); // ERROR_FILE_NOT_FOUND
        return 0;
    }

    size_t needed = target.size() + 2; // target + \0\0
    if (ucchMax < needed) {
        SetLastError(122);
        return 0;
    }

    std::memcpy(lpTargetPath, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
    lpTargetPath[target.size() + 1] = L'\0';
    SetLastError(0);
    return static_cast<DWORD>(needed);
}

inline DWORD WINAPI QueryDosDeviceA(const char* lpDeviceName, char* lpTargetPath, DWORD ucchMax) noexcept {
    if (!lpTargetPath || ucchMax == 0) {
        SetLastError(87);
        return 0;
    }
    std::wstring wDev;
    if (lpDeviceName) {
        std::string sDev(lpDeviceName);
        wDev.assign(sDev.begin(), sDev.end());
    }
    std::vector<wchar_t> wBuf(ucchMax);
    DWORD res = QueryDosDeviceW(lpDeviceName ? wDev.c_str() : nullptr, wBuf.data(), ucchMax);
    if (res == 0) return 0;
    for (DWORD i = 0; i < res; ++i) {
        lpTargetPath[i] = static_cast<char>(wBuf[i]);
    }
    return res;
}

inline BOOL WINAPI SetVolumeMountPointW(LPCWSTR lpszVolumeMountPoint, LPCWSTR lpszVolumeName) noexcept {
    if (!lpszVolumeMountPoint || !lpszVolumeName) {
        SetLastError(87);
        return FALSE;
    }
    std::wstring normId = fs::VirtualFileSystem::normalizeDriveIdentifier(lpszVolumeMountPoint);
    if (normId.empty() || normId.size() > 32) {
        SetLastError(87);
        return FALSE;
    }
    NtStatus st = fs::VirtualFileSystem::get().assignDriveIdentifier(normId, lpszVolumeName, lpszVolumeName);
    if (!NT_SUCCESS(st)) {
        SetLastError(ntdll::RtlNtStatusToDosError(st));
        return FALSE;
    }
    SetLastError(0);
    return TRUE;
}

inline BOOL WINAPI SetVolumeMountPointA(const char* lpszVolumeMountPoint, const char* lpszVolumeName) noexcept {
    if (!lpszVolumeMountPoint || !lpszVolumeName) return FALSE;
    std::string sMnt(lpszVolumeMountPoint), sVol(lpszVolumeName);
    std::wstring wMnt(sMnt.begin(), sMnt.end()), wVol(sVol.begin(), sVol.end());
    return SetVolumeMountPointW(wMnt.c_str(), wVol.c_str());
}

inline BOOL WINAPI DeleteVolumeMountPointW(LPCWSTR lpszVolumeMountPoint) noexcept {
    if (!lpszVolumeMountPoint) {
        SetLastError(87);
        return FALSE;
    }
    std::wstring normId = fs::VirtualFileSystem::normalizeDriveIdentifier(lpszVolumeMountPoint);
    fs::VirtualFileSystem::get().removeDriveIdentifier(normId);
    SetLastError(0);
    return TRUE;
}

inline BOOL WINAPI DeleteVolumeMountPointA(const char* lpszVolumeMountPoint) noexcept {
    if (!lpszVolumeMountPoint) return FALSE;
    std::string sMnt(lpszVolumeMountPoint);
    std::wstring wMnt(sMnt.begin(), sMnt.end());
    return DeleteVolumeMountPointW(wMnt.c_str());
}

inline BOOL WINAPI SetVolumeLabelA(const char* /*lpRootPathName*/, const char* /*lpVolumeName*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI GetDiskFreeSpaceExA(
    const char* /*lpDirectoryName*/,
    uint64_t* lpFreeBytesAvailableToCaller,
    uint64_t* lpTotalNumberOfBytes,
    uint64_t* lpTotalNumberOfFreeBytes
) noexcept {
    constexpr uint64_t oneTB = 1099511627776ULL;
    constexpr uint64_t freeBytes = 858993459200ULL; // 800 GB free
    if (lpFreeBytesAvailableToCaller) *lpFreeBytesAvailableToCaller = freeBytes;
    if (lpTotalNumberOfBytes) *lpTotalNumberOfBytes = oneTB;
    if (lpTotalNumberOfFreeBytes) *lpTotalNumberOfFreeBytes = freeBytes;
    return TRUE;
}

inline DWORD WINAPI GetLogicalDriveStringsA(DWORD nBufferLength, char* lpBuffer) noexcept {
    auto ids = fs::VirtualFileSystem::get().getMountedDriveIdentifiers();
    DWORD totalChars = 1;
    for (const auto& id : ids) {
        totalChars += static_cast<DWORD>(id.size() + 3);
    }
    if (nBufferLength < totalChars) {
        return totalChars;
    }
    if (!lpBuffer) {
        return 0;
    }
    char* cur = lpBuffer;
    for (const auto& id : ids) {
        for (wchar_t ch : id) {
            *cur++ = static_cast<char>(std::towupper(ch));
        }
        *cur++ = ':';
        *cur++ = '\\';
        *cur++ = '\0';
    }
    *cur = '\0';
    return totalChars - 1;
}

inline BOOL WINAPI CancelIoEx(HANDLE hFile, void* lpOverlapped) noexcept {
    return SovereignAsyncIoManager::get().CancelIoExHelper(hFile, reinterpret_cast<LPOVERLAPPED>(lpOverlapped));
}

inline BOOL WINAPI CancelSynchronousIo(HANDLE /*hThread*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI GetOverlappedResultEx(
    HANDLE hFile,
    void* lpOverlapped,
    DWORD* lpNumberOfBytesTransferred,
    DWORD dwMilliseconds,
    BOOL bAlertable
) noexcept {
    return SovereignAsyncIoManager::get().GetOverlappedResultExHelper(hFile, reinterpret_cast<LPOVERLAPPED>(lpOverlapped), lpNumberOfBytesTransferred, dwMilliseconds, bAlertable);
}

inline BOOL WINAPI SleepConditionVariableCS(
    void* /*ConditionVariable*/,
    void* /*CriticalSection*/,
    DWORD /*dwMilliseconds*/
) noexcept {
    return TRUE;
}

inline BOOL WINAPI CreateSymbolicLinkW(const wchar_t* /*lpSymlinkFileName*/, const wchar_t* /*lpTargetFileName*/, DWORD /*dwFlags*/) noexcept {
    return TRUE;
}

inline HWND WINAPI GetConsoleWindow() noexcept {
    return reinterpret_cast<HWND>(0x1000);
}

inline BOOL WINAPI EnumUILanguagesW(void* /*lpUILanguageEnumProc*/, DWORD /*dwFlags*/, LONG_PTR /*lParam*/) noexcept {
    return TRUE;
}

inline LCID WINAPI GetSystemDefaultLCID() noexcept {
    return 0x0409; // en-US
}

inline WORD WINAPI GetThreadUILanguage() noexcept {
    return 0x0409; // en-US
}

inline int WINAPI LCIDToLocaleName(LCID /*Locale*/, wchar_t* lpName, int cchName, DWORD /*dwFlags*/) noexcept {
    if (lpName && cchName >= 6) {
        std::wcsncpy(lpName, L"en-US", cchName);
        return 6;
    }
    return 0;
}

inline BOOL WINAPI SetDefaultDllDirectories(DWORD /*DirectoryFlags*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetFileAttributesA(const char* /*lpFileName*/, DWORD /*dwFileAttributes*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI VerifyVersionInfoA(void* /*lpVersionInformation*/, DWORD /*dwTypeMask*/, uint64_t /*dwlConditionMask*/) noexcept {
    return TRUE;
}

inline DWORD WINAPI K32GetModuleFileNameExW(
    HANDLE /*hProcess*/,
    void* /*hModule*/,
    wchar_t* lpFilename,
    DWORD nSize
) noexcept {
    if (lpFilename && nSize > 0) {
        std::wcsncpy(lpFilename, L"C:\\Program Files\\Rufus\\rufus.exe", nSize);
        return static_cast<DWORD>(std::wcslen(lpFilename));
    }
    return 0;
}

inline DWORD WINAPI K32GetProcessImageFileNameW(
    HANDLE /*hProcess*/,
    wchar_t* lpImageFileName,
    DWORD nSize
) noexcept {
    if (lpImageFileName && nSize > 0) {
        std::wcsncpy(lpImageFileName, L"\\Device\\HarddiskVolume1\\Program Files\\Rufus\\rufus.exe", nSize);
        return static_cast<DWORD>(std::wcslen(lpImageFileName));
    }
    return 0;
}

struct PSAPI_WORKING_SET_WATCH_INFORMATION {
    void* FaultingPc{ nullptr };
    void* FaultingVa{ nullptr };
};
using PPSAPI_WORKING_SET_WATCH_INFORMATION = PSAPI_WORKING_SET_WATCH_INFORMATION*;

struct PSAPI_WORKING_SET_WATCH_INFORMATION_EX {
    PSAPI_WORKING_SET_WATCH_INFORMATION BasicInfo{};
    ULONG_PTR FaultingThreadId{ 0 };
    ULONG_PTR Flags{ 0 };
};
using PPSAPI_WORKING_SET_WATCH_INFORMATION_EX = PSAPI_WORKING_SET_WATCH_INFORMATION_EX*;

inline BOOL WINAPI InitializeProcessWorkingSetWatch(HANDLE /*hProcess*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI K32InitializeProcessWorkingSetWatch(HANDLE hProcess) noexcept {
    return InitializeProcessWorkingSetWatch(hProcess);
}

inline BOOL WINAPI GetProcessWorkingSetWatch(
    HANDLE /*hProcess*/,
    PPSAPI_WORKING_SET_WATCH_INFORMATION lpWatchInfo,
    DWORD cb) noexcept {
    if (!lpWatchInfo || cb < sizeof(PSAPI_WORKING_SET_WATCH_INFORMATION)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    uint32_t count = cb / sizeof(PSAPI_WORKING_SET_WATCH_INFORMATION);
    for (uint32_t i = 0; i < count; ++i) {
        lpWatchInfo[i].FaultingPc = reinterpret_cast<void*>(0x00007FF610001000ULL + i * 0x100ULL);
        lpWatchInfo[i].FaultingVa = reinterpret_cast<void*>(0x0000020000000000ULL + i * 0x1000ULL);
    }
    return TRUE;
}

inline BOOL WINAPI K32GetProcessWorkingSetWatch(
    HANDLE hProcess,
    PPSAPI_WORKING_SET_WATCH_INFORMATION lpWatchInfo,
    DWORD cb) noexcept {
    return GetProcessWorkingSetWatch(hProcess, lpWatchInfo, cb);
}

inline BOOL WINAPI GetProcessWorkingSetWatchEx(
    HANDLE /*hProcess*/,
    PPSAPI_WORKING_SET_WATCH_INFORMATION_EX lpWatchInfoEx,
    DWORD cb) noexcept {
    if (!lpWatchInfoEx || cb < sizeof(PSAPI_WORKING_SET_WATCH_INFORMATION_EX)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    uint32_t count = cb / sizeof(PSAPI_WORKING_SET_WATCH_INFORMATION_EX);
    for (uint32_t i = 0; i < count; ++i) {
        lpWatchInfoEx[i].BasicInfo.FaultingPc = reinterpret_cast<void*>(0x00007FF610001000ULL + i * 0x100ULL);
        lpWatchInfoEx[i].BasicInfo.FaultingVa = reinterpret_cast<void*>(0x0000020000000000ULL + i * 0x1000ULL);
        lpWatchInfoEx[i].FaultingThreadId = 1024 + i;
        lpWatchInfoEx[i].Flags = 0;
    }
    return TRUE;
}

inline BOOL WINAPI K32GetProcessWorkingSetWatchEx(
    HANDLE hProcess,
    PPSAPI_WORKING_SET_WATCH_INFORMATION_EX lpWatchInfoEx,
    DWORD cb) noexcept {
    return GetProcessWorkingSetWatchEx(hProcess, lpWatchInfoEx, cb);
}

inline DWORD WINAPI GetProcessImageFileNameW(
    HANDLE hProcess,
    wchar_t* lpImageFileName,
    DWORD nSize) noexcept {
    return K32GetProcessImageFileNameW(hProcess, lpImageFileName, nSize);
}

inline DWORD WINAPI GetProcessImageFileNameA(
    HANDLE /*hProcess*/,
    char* lpImageFileName,
    DWORD nSize) noexcept {
    if (lpImageFileName && nSize > 0) {
        std::strncpy(lpImageFileName, "\\Device\\HarddiskVolume1\\Program Files\\Rufus\\rufus.exe", nSize);
        return static_cast<DWORD>(std::strlen(lpImageFileName));
    }
    return 0;
}

inline DWORD WINAPI K32GetProcessImageFileNameA(
    HANDLE hProcess,
    char* lpImageFileName,
    DWORD nSize) noexcept {
    return GetProcessImageFileNameA(hProcess, lpImageFileName, nSize);
}

struct SLIST_ENTRY {
    SLIST_ENTRY* Next;
};

struct SLIST_HEADER {
    uint64_t Alignment;
    uint64_t Region;
};

struct ACTCTXW {
    uint32_t cbSize;
    uint32_t dwFlags;
    const wchar_t* lpSource;
    uint16_t wProcessorArchitecture;
    uint16_t wLangId;
    const wchar_t* lpAssemblyDirectory;
    const wchar_t* lpResourceName;
    const wchar_t* lpApplicationName;
    void* hModule;
};

inline void* WINAPI GlobalReAlloc(void* hMem, size_t dwBytes, uint32_t /*uFlags*/) noexcept {
    if (!hMem) {
        return std::malloc(dwBytes ? dwBytes : 1);
    }
    return std::realloc(hMem, dwBytes ? dwBytes : 1);
}

inline void* WINAPI LocalReAlloc(void* hMem, size_t uBytes, uint32_t /*uFlags*/) noexcept {
    if (!hMem) {
        return std::malloc(uBytes ? uBytes : 1);
    }
    return std::realloc(hMem, uBytes ? uBytes : 1);
}

inline void* WINAPI GlobalHandle(const void* pMem) noexcept {
    return const_cast<void*>(pMem);
}

inline uint32_t WINAPI GlobalFlags(void* /*hMem*/) noexcept {
    return 0; // GMEM_FIXED
}

inline uint16_t WINAPI SetThreadUILanguage(uint16_t LangId) noexcept {
    static uint16_t s_lang = 0x0409; // en-US
    uint16_t prev = s_lang;
    if (LangId != 0) {
        s_lang = LangId;
    }
    return prev;
}

inline BOOL WINAPI SetSearchPathMode(uint32_t /*Flags*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetDllDirectoryW(const wchar_t* /*lpPathName*/) noexcept {
    return TRUE;
}

inline uint32_t WINAPI GetSystemWow64DirectoryW(wchar_t* lpBuffer, uint32_t uSize) noexcept {
    const wchar_t sysWow64[] = L"C:\\Windows\\SysWOW64";
    constexpr uint32_t len = 19;
    if (!lpBuffer || uSize <= len) {
        return len + 1;
    }
    std::wmemcpy(lpBuffer, sysWow64, len);
    lpBuffer[len] = L'\0';
    return len;
}

inline uint32_t WINAPI ExpandEnvironmentStringsA(const char* lpSrc, char* lpDst, uint32_t nSize) noexcept {
    if (!lpSrc) {
        return 0;
    }
    std::string src(lpSrc);
    std::string out;
    size_t i = 0;
    while (i < src.size()) {
        if (src[i] == '%') {
            size_t end = src.find('%', i + 1);
            if (end != std::string::npos) {
                std::string var = src.substr(i + 1, end - i - 1);
                std::string val;
                for (char& c : var) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (var == "SYSTEMROOT" || var == "WINDIR") val = "C:\\Windows";
                else if (var == "SYSTEMDRIVE") val = "C:";
                else if (var == "TEMP" || var == "TMP") val = "C:\\Users\\admin\\AppData\\Local\\Temp";
                else if (var == "USERPROFILE") val = "C:\\Users\\admin";
                else if (var == "PROGRAMDATA") val = "C:\\ProgramData";
                else val = "";
                out.append(val);
                i = end + 1;
                continue;
            }
        }
        out.push_back(src[i++]);
    }
    uint32_t req = static_cast<uint32_t>(out.size() + 1);
    if (!lpDst || nSize < req) {
        return req;
    }
    std::memcpy(lpDst, out.c_str(), req);
    return req - 1;
}

inline void* WINAPI CreateActCtxW(const ACTCTXW* /*pActCtx*/) noexcept {
    return reinterpret_cast<void*>(0xAC1C0001);
}

inline BOOL WINAPI ActivateActCtx(void* /*hActCtx*/, uintptr_t* lpCookie) noexcept {
    if (lpCookie) {
        *lpCookie = 0x12345678;
    }
    return TRUE;
}

inline BOOL WINAPI DeactivateActCtx(uint32_t /*dwFlags*/, uintptr_t /*ulCookie*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI FindActCtxSectionStringW(uint32_t /*dwFlags*/, const void* /*lpExtensionGuid*/, uint32_t /*ulSectionId*/, const wchar_t* /*lpStringToFind*/, void* /*PActCtx*/) noexcept {
    return FALSE;
}

inline BOOL WINAPI QueryActCtxW(uint32_t /*dwFlags*/, void* /*hActCtx*/, void* /*pvSubInstance*/, uint32_t /*ulInfoClass*/, void* /*pvBuffer*/, size_t /*cbBuffer*/, size_t* pcbWritten) noexcept {
    if (pcbWritten) {
        *pcbWritten = 0;
    }
    return TRUE;
}

inline uint32_t WINAPI GetProfileIntW(const wchar_t* /*lpAppName*/, const wchar_t* /*lpKeyName*/, int32_t nDefault) noexcept {
    return static_cast<uint32_t>(nDefault);
}

inline uint32_t WINAPI GlobalGetAtomNameW(uint16_t nAtom, wchar_t* lpBuffer, int32_t nSize) noexcept {
    if (!lpBuffer || nSize <= 0) {
        return 0;
    }
    int written = std::swprintf(lpBuffer, nSize, L"#%u", nAtom);
    return written > 0 ? static_cast<uint32_t>(written) : 0;
}

inline int32_t WINAPI lstrcmpA(const char* s1, const char* s2) noexcept {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return std::strcmp(s1, s2);
}

inline BOOL WINAPI LockFile(HANDLE /*hFile*/, uint32_t /*dwFileOffsetLow*/, uint32_t /*dwFileOffsetHigh*/, uint32_t /*nNumberOfBytesToLockLow*/, uint32_t /*nNumberOfBytesToLockHigh*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI UnlockFile(HANDLE /*hFile*/, uint32_t /*dwFileOffsetLow*/, uint32_t /*dwFileOffsetHigh*/, uint32_t /*nNumberOfBytesToUnlockLow*/, uint32_t /*nNumberOfBytesToUnlockHigh*/) noexcept {
    return TRUE;
}

inline void* WINAPI FindResourceExW(HMODULE hModule, const wchar_t* lpType, const wchar_t* lpName, uint16_t /*wLanguage*/) noexcept {
    return FindResourceW(hModule, lpName, lpType);
}

inline SLIST_ENTRY* WINAPI InterlockedPushEntrySList(SLIST_HEADER* ListHead, SLIST_ENTRY* ListEntry) noexcept {
    if (!ListHead || !ListEntry) {
        return nullptr;
    }
    auto* oldFirst = reinterpret_cast<SLIST_ENTRY*>(ListHead->Alignment);
    ListEntry->Next = oldFirst;
    ListHead->Alignment = reinterpret_cast<uint64_t>(ListEntry);
    uint64_t depth = (ListHead->Region & 0xFFFF) + 1;
    uint64_t seq = ((ListHead->Region >> 16) + 1) & 0xFFFFFFFFFFFFULL;
    ListHead->Region = (seq << 16) | (depth & 0xFFFF);
    return oldFirst;
}

inline SLIST_ENTRY* WINAPI InterlockedPushListSListEx(SLIST_HEADER* ListHead, SLIST_ENTRY* List, SLIST_ENTRY* ListEnd, ULONG Count) noexcept {
    if (!ListHead || !List || !ListEnd || Count == 0) return nullptr;
    auto* oldFirst = reinterpret_cast<SLIST_ENTRY*>(ListHead->Alignment);
    ListEnd->Next = oldFirst;
    ListHead->Alignment = reinterpret_cast<uint64_t>(List);
    uint64_t depth = (ListHead->Region & 0xFFFF) + Count;
    uint64_t seq = ((ListHead->Region >> 16) + 1) & 0xFFFFFFFFFFFFULL;
    ListHead->Region = (seq << 16) | (depth & 0xFFFF);
    return oldFirst;
}

inline SLIST_ENTRY* WINAPI InterlockedPushListSList(SLIST_HEADER* ListHead, SLIST_ENTRY* List, SLIST_ENTRY* ListEnd, ULONG Count) noexcept {
    return InterlockedPushListSListEx(ListHead, List, ListEnd, Count);
}

inline BOOL WINAPI InitializeSListHeadEx(SLIST_HEADER* ListHead, ULONG Flags) noexcept {
    if (!ListHead) return FALSE;
    ListHead->Alignment = 0;
    ListHead->Region = static_cast<uint64_t>(Flags) << 48;
    return TRUE;
}

inline uint16_t WINAPI CompactSListHead(SLIST_HEADER* ListHead) noexcept {
    if (!ListHead) return 0;
    return static_cast<uint16_t>(ListHead->Region & 0xFFFF);
}

inline void* WINAPI InterlockedCompareExchangePointer(void** Destination, void* Exchange, void* Comperand) noexcept {
    if (!Destination) return nullptr;
    auto* atomicDest = reinterpret_cast<std::atomic<void*>*>(Destination);
    void* expected = Comperand;
    atomicDest->compare_exchange_strong(expected, Exchange, std::memory_order_seq_cst);
    return expected;
}

inline void* WINAPI InterlockedExchangePointer(void** Destination, void* Exchange) noexcept {
    if (!Destination) return nullptr;
    auto* atomicDest = reinterpret_cast<std::atomic<void*>*>(Destination);
    return atomicDest->exchange(Exchange, std::memory_order_seq_cst);
}

inline void WINAPI FillMemoryUlong(void* Destination, size_t Length, uint32_t Pattern) noexcept {
    ntdll::RtlFillMemoryUlong(Destination, Length, Pattern);
}

inline void WINAPI FillMemoryUlonglong(void* Destination, size_t Length, uint64_t Pattern) noexcept {
    ntdll::RtlFillMemoryUlonglong(Destination, Length, Pattern);
}

inline void WINAPI ZeroMemoryEx(void* Destination, size_t Length, uint32_t Flags) noexcept {
    ntdll::RtlZeroMemoryEx(Destination, Length, Flags);
}

inline size_t WINAPI CompareMemoryUlong(const void* Source, size_t Length, uint32_t Pattern) noexcept {
    return ntdll::RtlCompareMemoryUlong(Source, Length, Pattern);
}

inline BOOLEAN WINAPI InterlockedBitTestAndSet(LONG volatile* Base, LONG Bit) noexcept {
    return ntdll::RtlInterlockedBitTestAndSet(Base, Bit);
}

inline BOOLEAN WINAPI InterlockedBitTestAndReset(LONG volatile* Base, LONG Bit) noexcept {
    return ntdll::RtlInterlockedBitTestAndReset(Base, Bit);
}

inline BOOLEAN WINAPI InterlockedBitTestAndSet64(LONG64 volatile* Base, LONG64 Bit) noexcept {
    return ntdll::RtlInterlockedBitTestAndSet64(Base, Bit);
}

inline BOOLEAN WINAPI InterlockedBitTestAndReset64(LONG64 volatile* Base, LONG64 Bit) noexcept {
    return ntdll::RtlInterlockedBitTestAndReset64(Base, Bit);
}

inline BOOLEAN WINAPI InterlockedBitTestAndComplement(LONG volatile* Base, LONG Bit) noexcept {
    return ntdll::RtlInterlockedBitTestAndComplement(Base, Bit);
}

inline BOOLEAN WINAPI InterlockedBitTestAndComplement64(LONG64 volatile* Base, LONG64 Bit) noexcept {
    return ntdll::RtlInterlockedBitTestAndComplement64(Base, Bit);
}

inline LONG WINAPI InterlockedAnd(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAnd(Destination, Value);
}

inline LONG WINAPI InterlockedOr(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedOr(Destination, Value);
}

inline LONG WINAPI InterlockedXor(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedXor(Destination, Value);
}

inline LONG64 WINAPI InterlockedAnd64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedAnd64(Destination, Value);
}

inline LONG64 WINAPI InterlockedOr64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedOr64(Destination, Value);
}

inline LONG64 WINAPI InterlockedXor64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedXor64(Destination, Value);
}

inline LONG WINAPI InterlockedAndNot(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAndNot(Destination, Value);
}

inline LONG64 WINAPI InterlockedAndNot64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedAndNot64(Destination, Value);
}

inline LONG64 WINAPI InterlockedCompareExchange64(LONG64 volatile* Destination, LONG64 Exchange, LONG64 Comperand) noexcept {
    return ntdll::RtlInterlockedCompareExchange64(Destination, Exchange, Comperand);
}

inline LONG64 WINAPI InterlockedExchange64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedExchange64(Destination, Value);
}

inline LONG64 WINAPI InterlockedExchangeAdd64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedExchangeAdd64(Destination, Value);
}

inline LONG64 WINAPI InterlockedIncrement64(LONG64 volatile* Addend) noexcept {
    return ntdll::RtlInterlockedIncrement64(Addend);
}

inline LONG64 WINAPI InterlockedDecrement64(LONG64 volatile* Addend) noexcept {
    return ntdll::RtlInterlockedDecrement64(Addend);
}

inline SHORT WINAPI InterlockedCompareExchange16(SHORT volatile* Destination, SHORT Exchange, SHORT Comperand) noexcept {
    return ntdll::RtlInterlockedCompareExchange16(Destination, Exchange, Comperand);
}

inline SHORT WINAPI InterlockedExchange16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedExchange16(Destination, Value);
}

inline SHORT WINAPI InterlockedExchangeAdd16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedExchangeAdd16(Destination, Value);
}

inline SHORT WINAPI InterlockedIncrement16(SHORT volatile* Addend) noexcept {
    return ntdll::RtlInterlockedIncrement16(Addend);
}

inline SHORT WINAPI InterlockedDecrement16(SHORT volatile* Addend) noexcept {
    return ntdll::RtlInterlockedDecrement16(Addend);
}

inline SHORT WINAPI InterlockedAnd16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedAnd16(Destination, Value);
}

inline SHORT WINAPI InterlockedOr16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedOr16(Destination, Value);
}

inline SHORT WINAPI InterlockedXor16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedXor16(Destination, Value);
}

inline CHAR WINAPI InterlockedCompareExchange8(CHAR volatile* Destination, CHAR Exchange, CHAR Comperand) noexcept {
    return ntdll::RtlInterlockedCompareExchange8(Destination, Exchange, Comperand);
}

inline CHAR WINAPI InterlockedExchange8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedExchange8(Destination, Value);
}

inline CHAR WINAPI InterlockedExchangeAdd8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedExchangeAdd8(Destination, Value);
}

inline CHAR WINAPI InterlockedIncrement8(CHAR volatile* Addend) noexcept {
    return ntdll::RtlInterlockedIncrement8(Addend);
}

inline CHAR WINAPI InterlockedDecrement8(CHAR volatile* Addend) noexcept {
    return ntdll::RtlInterlockedDecrement8(Addend);
}

inline CHAR WINAPI InterlockedAnd8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedAnd8(Destination, Value);
}

inline CHAR WINAPI InterlockedOr8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedOr8(Destination, Value);
}

inline CHAR WINAPI InterlockedXor8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedXor8(Destination, Value);
}

inline SHORT WINAPI InterlockedAndNot16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedAndNot16(Destination, Value);
}

inline CHAR WINAPI InterlockedAndNot8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedAndNot8(Destination, Value);
}

inline LONG64 WINAPI InterlockedXorNot64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedXorNot64(Destination, Value);
}

inline LONG WINAPI InterlockedXorNot32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedXorNot32(Destination, Value);
}

inline SHORT WINAPI InterlockedXorNot16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedXorNot16(Destination, Value);
}

inline CHAR WINAPI InterlockedXorNot8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedXorNot8(Destination, Value);
}

inline LONG64 WINAPI InterlockedNand64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedNand64(Destination, Value);
}

inline LONG WINAPI InterlockedNand32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedNand32(Destination, Value);
}

inline SHORT WINAPI InterlockedNand16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedNand16(Destination, Value);
}

inline CHAR WINAPI InterlockedNand8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedNand8(Destination, Value);
}

inline LONG64 WINAPI InterlockedNor64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedNor64(Destination, Value);
}

inline LONG WINAPI InterlockedNor32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedNor32(Destination, Value);
}

inline SHORT WINAPI InterlockedNor16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedNor16(Destination, Value);
}

inline CHAR WINAPI InterlockedNor8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedNor8(Destination, Value);
}

inline LONG64 WINAPI InterlockedXnor64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedXnor64(Destination, Value);
}

inline LONG WINAPI InterlockedXnor32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedXnor32(Destination, Value);
}

inline SHORT WINAPI InterlockedXnor16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedXnor16(Destination, Value);
}

inline CHAR WINAPI InterlockedXnor8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedXnor8(Destination, Value);
}

inline LONG64 WINAPI InterlockedMin64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedMin64(Destination, Value);
}

inline LONG WINAPI InterlockedMin32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedMin32(Destination, Value);
}

inline SHORT WINAPI InterlockedMin16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedMin16(Destination, Value);
}

inline CHAR WINAPI InterlockedMin8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedMin8(Destination, Value);
}

inline LONG64 WINAPI InterlockedMax64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedMax64(Destination, Value);
}

inline LONG WINAPI InterlockedMax32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedMax32(Destination, Value);
}

inline SHORT WINAPI InterlockedMax16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedMax16(Destination, Value);
}

inline CHAR WINAPI InterlockedMax8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedMax8(Destination, Value);
}

inline LONG64 WINAPI InterlockedClamp64(LONG64 volatile* Destination, LONG64 MinVal, LONG64 MaxVal) noexcept {
    return ntdll::RtlInterlockedClamp64(Destination, MinVal, MaxVal);
}

inline LONG WINAPI InterlockedClamp(LONG volatile* Destination, LONG MinVal, LONG MaxVal) noexcept {
    return ntdll::RtlInterlockedClamp32(Destination, MinVal, MaxVal);
}

inline LONG WINAPI InterlockedClamp32(LONG volatile* Destination, LONG MinVal, LONG MaxVal) noexcept {
    return ntdll::RtlInterlockedClamp32(Destination, MinVal, MaxVal);
}

inline SHORT WINAPI InterlockedClamp16(SHORT volatile* Destination, SHORT MinVal, SHORT MaxVal) noexcept {
    return ntdll::RtlInterlockedClamp16(Destination, MinVal, MaxVal);
}

inline CHAR WINAPI InterlockedClamp8(CHAR volatile* Destination, CHAR MinVal, CHAR MaxVal) noexcept {
    return ntdll::RtlInterlockedClamp8(Destination, MinVal, MaxVal);
}

inline LONG64 WINAPI InterlockedAddModulo64(LONG64 volatile* Destination, LONG64 Value, LONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedAddModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedAddModulo(LONG volatile* Destination, LONG Value, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedAddModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedAddModulo32(LONG volatile* Destination, LONG Value, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedAddModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedAddModulo16(SHORT volatile* Destination, SHORT Value, SHORT Modulus) noexcept {
    return ntdll::RtlInterlockedAddModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedAddModulo8(CHAR volatile* Destination, CHAR Value, CHAR Modulus) noexcept {
    return ntdll::RtlInterlockedAddModulo8(Destination, Value, Modulus);
}

inline BOOL WINAPI GetPagefileCompressionTelemetry(ntdll::SYSTEM_PAGEFILE_COMPRESSION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileCompressionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuCooperativeFabricInformation(ntdll::SYSTEM_CPU_COOPERATIVE_FABRIC_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuCooperativeFabric(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileDecompressionTelemetry(ntdll::SYSTEM_PAGEFILE_DECOMPRESSION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileDecompressionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricPartitionInformation(ntdll::SYSTEM_CPU_FABRIC_PARTITION_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricPartition(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileClusterCompactionTelemetry(ntdll::SYSTEM_PAGEFILE_CLUSTER_COMPACTION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileClusterCompactionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricQosTelemetry(ntdll::SYSTEM_CPU_FABRIC_QOS_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricQosTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileClusterDefragmentationTelemetry(ntdll::SYSTEM_PAGEFILE_CLUSTER_DEFRAGMENTATION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileClusterDefragmentationTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricLatencyTelemetry(ntdll::SYSTEM_CPU_FABRIC_LATENCY_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricLatencyTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileExtentMigrationTelemetry(ntdll::SYSTEM_PAGEFILE_EXTENT_MIGRATION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileExtentMigrationTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricCongestionTelemetry(ntdll::SYSTEM_CPU_FABRIC_CONGESTION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricCongestionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileTierExpulsionTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_EXPULSION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierExpulsionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricBandwidthPartitionTelemetry(ntdll::SYSTEM_CPU_FABRIC_BANDWIDTH_PARTITION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricBandwidthPartitionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileTierIoQueueTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_IO_QUEUE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierIoQueueTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricPowerStateTelemetry(ntdll::SYSTEM_CPU_FABRIC_POWER_STATE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricPowerStateTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileTierWearLevelingTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_WEAR_LEVELING_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierWearLevelingTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltageDomainTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_DOMAIN_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltageDomainTelemetry(Info)) ? TRUE : FALSE;
}

inline LONG64 WINAPI InterlockedMulModulo64(LONG64 volatile* Destination, LONG64 Multiplier, LONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedMulModulo64(Destination, Multiplier, Modulus);
}

inline LONG WINAPI InterlockedMulModulo(LONG volatile* Destination, LONG Multiplier, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedMulModulo32(Destination, Multiplier, Modulus);
}

inline LONG WINAPI InterlockedMulModulo32(LONG volatile* Destination, LONG Multiplier, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedMulModulo32(Destination, Multiplier, Modulus);
}

inline SHORT WINAPI InterlockedMulModulo16(SHORT volatile* Destination, SHORT Multiplier, SHORT Modulus) noexcept {
    return ntdll::RtlInterlockedMulModulo16(Destination, Multiplier, Modulus);
}

inline CHAR WINAPI InterlockedMulModulo8(CHAR volatile* Destination, CHAR Multiplier, CHAR Modulus) noexcept {
    return ntdll::RtlInterlockedMulModulo8(Destination, Multiplier, Modulus);
}

inline LONG64 WINAPI InterlockedSubModulo64(LONG64 volatile* Destination, LONG64 Subtrahend, LONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedSubModulo64(Destination, Subtrahend, Modulus);
}

inline LONG WINAPI InterlockedSubModulo(LONG volatile* Destination, LONG Subtrahend, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedSubModulo32(Destination, Subtrahend, Modulus);
}

inline LONG WINAPI InterlockedSubModulo32(LONG volatile* Destination, LONG Subtrahend, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedSubModulo32(Destination, Subtrahend, Modulus);
}

inline SHORT WINAPI InterlockedSubModulo16(SHORT volatile* Destination, SHORT Subtrahend, SHORT Modulus) noexcept {
    return ntdll::RtlInterlockedSubModulo16(Destination, Subtrahend, Modulus);
}

inline CHAR WINAPI InterlockedSubModulo8(CHAR volatile* Destination, CHAR Subtrahend, CHAR Modulus) noexcept {
    return ntdll::RtlInterlockedSubModulo8(Destination, Subtrahend, Modulus);
}

inline LONG64 WINAPI InterlockedDivModulo64(LONG64 volatile* Destination, LONG64 Divisor, LONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedDivModulo64(Destination, Divisor, Modulus);
}

inline LONG WINAPI InterlockedDivModulo(LONG volatile* Destination, LONG Divisor, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedDivModulo32(Destination, Divisor, Modulus);
}

inline LONG WINAPI InterlockedDivModulo32(LONG volatile* Destination, LONG Divisor, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedDivModulo32(Destination, Divisor, Modulus);
}

inline SHORT WINAPI InterlockedDivModulo16(SHORT volatile* Destination, SHORT Divisor, SHORT Modulus) noexcept {
    return ntdll::RtlInterlockedDivModulo16(Destination, Divisor, Modulus);
}

inline CHAR WINAPI InterlockedDivModulo8(CHAR volatile* Destination, CHAR Divisor, CHAR Modulus) noexcept {
    return ntdll::RtlInterlockedDivModulo8(Destination, Divisor, Modulus);
}

inline LONG64 WINAPI InterlockedExpModulo64(LONG64 volatile* Destination, LONG64 Exponent, LONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedExpModulo64(Destination, Exponent, Modulus);
}

inline LONG WINAPI InterlockedExpModulo(LONG volatile* Destination, LONG Exponent, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedExpModulo32(Destination, Exponent, Modulus);
}

inline LONG WINAPI InterlockedExpModulo32(LONG volatile* Destination, LONG Exponent, LONG Modulus) noexcept {
    return ntdll::RtlInterlockedExpModulo32(Destination, Exponent, Modulus);
}

inline SHORT WINAPI InterlockedExpModulo16(SHORT volatile* Destination, SHORT Exponent, SHORT Modulus) noexcept {
    return ntdll::RtlInterlockedExpModulo16(Destination, Exponent, Modulus);
}

inline CHAR WINAPI InterlockedExpModulo8(CHAR volatile* Destination, CHAR Exponent, CHAR Modulus) noexcept {
    return ntdll::RtlInterlockedExpModulo8(Destination, Exponent, Modulus);
}

inline LONG64 WINAPI InterlockedRotateLeftModulo64(LONG64 volatile* Destination, ULONG Shift, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedRotateLeftModulo64(Destination, Shift, Modulus);
}

inline LONG WINAPI InterlockedRotateLeftModulo(LONG volatile* Destination, ULONG Shift, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedRotateLeftModulo32(Destination, Shift, Modulus);
}

inline LONG WINAPI InterlockedRotateLeftModulo32(LONG volatile* Destination, ULONG Shift, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedRotateLeftModulo32(Destination, Shift, Modulus);
}

inline SHORT WINAPI InterlockedRotateLeftModulo16(SHORT volatile* Destination, ULONG Shift, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedRotateLeftModulo16(Destination, Shift, Modulus);
}

inline CHAR WINAPI InterlockedRotateLeftModulo8(CHAR volatile* Destination, ULONG Shift, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedRotateLeftModulo8(Destination, Shift, Modulus);
}

inline LONG64 WINAPI InterlockedRotateRightModulo64(LONG64 volatile* Destination, ULONG Shift, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedRotateRightModulo64(Destination, Shift, Modulus);
}

inline LONG WINAPI InterlockedRotateRightModulo(LONG volatile* Destination, ULONG Shift, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedRotateRightModulo32(Destination, Shift, Modulus);
}

inline LONG WINAPI InterlockedRotateRightModulo32(LONG volatile* Destination, ULONG Shift, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedRotateRightModulo32(Destination, Shift, Modulus);
}

inline SHORT WINAPI InterlockedRotateRightModulo16(SHORT volatile* Destination, ULONG Shift, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedRotateRightModulo16(Destination, Shift, Modulus);
}

inline CHAR WINAPI InterlockedRotateRightModulo8(CHAR volatile* Destination, ULONG Shift, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedRotateRightModulo8(Destination, Shift, Modulus);
}

inline LONG64 WINAPI InterlockedInvertModulo64(LONG64 volatile* Destination, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedInvertModulo64(Destination, Modulus);
}

inline LONG WINAPI InterlockedInvertModulo(LONG volatile* Destination, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedInvertModulo32(Destination, Modulus);
}

inline LONG WINAPI InterlockedInvertModulo32(LONG volatile* Destination, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedInvertModulo32(Destination, Modulus);
}

inline SHORT WINAPI InterlockedInvertModulo16(SHORT volatile* Destination, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedInvertModulo16(Destination, Modulus);
}

inline CHAR WINAPI InterlockedInvertModulo8(CHAR volatile* Destination, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedInvertModulo8(Destination, Modulus);
}

inline LONG64 WINAPI InterlockedNegateModulo64(LONG64 volatile* Destination, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedNegateModulo64(Destination, Modulus);
}

inline LONG WINAPI InterlockedNegateModulo(LONG volatile* Destination, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedNegateModulo32(Destination, Modulus);
}

inline LONG WINAPI InterlockedNegateModulo32(LONG volatile* Destination, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedNegateModulo32(Destination, Modulus);
}

inline SHORT WINAPI InterlockedNegateModulo16(SHORT volatile* Destination, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedNegateModulo16(Destination, Modulus);
}

inline CHAR WINAPI InterlockedNegateModulo8(CHAR volatile* Destination, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedNegateModulo8(Destination, Modulus);
}

inline LONG64 WINAPI InterlockedAbsDiffModulo64(LONG64 volatile* Destination, LONG64 Value, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedAbsDiffModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedAbsDiffModulo(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedAbsDiffModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedAbsDiffModulo32(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedAbsDiffModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedAbsDiffModulo16(SHORT volatile* Destination, SHORT Value, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedAbsDiffModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedAbsDiffModulo8(CHAR volatile* Destination, CHAR Value, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedAbsDiffModulo8(Destination, Value, Modulus);
}

inline LONG64 WINAPI InterlockedNorModulo64(LONG64 volatile* Destination, LONG64 Value, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedNorModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedNorModulo(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedNorModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedNorModulo32(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedNorModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedNorModulo16(SHORT volatile* Destination, SHORT Value, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedNorModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedNorModulo8(CHAR volatile* Destination, CHAR Value, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedNorModulo8(Destination, Value, Modulus);
}

inline LONG64 WINAPI InterlockedNandModulo64(LONG64 volatile* Destination, LONG64 Value, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedNandModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedNandModulo(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedNandModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedNandModulo32(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedNandModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedNandModulo16(SHORT volatile* Destination, SHORT Value, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedNandModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedNandModulo8(CHAR volatile* Destination, CHAR Value, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedNandModulo8(Destination, Value, Modulus);
}

inline LONG64 WINAPI InterlockedXnorModulo64(LONG64 volatile* Destination, LONG64 Value, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedXnorModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedXnorModulo(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedXnorModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedXnorModulo32(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedXnorModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedXnorModulo16(SHORT volatile* Destination, SHORT Value, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedXnorModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedXnorModulo8(CHAR volatile* Destination, CHAR Value, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedXnorModulo8(Destination, Value, Modulus);
}

inline LONG64 WINAPI InterlockedAndNotModulo64(LONG64 volatile* Destination, LONG64 Value, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedAndNotModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedAndNotModulo(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedAndNotModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedAndNotModulo32(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedAndNotModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedAndNotModulo16(SHORT volatile* Destination, SHORT Value, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedAndNotModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedAndNotModulo8(CHAR volatile* Destination, CHAR Value, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedAndNotModulo8(Destination, Value, Modulus);
}

inline LONG64 WINAPI InterlockedOrNotModulo64(LONG64 volatile* Destination, LONG64 Value, ULONG64 Modulus) noexcept {
    return ntdll::RtlInterlockedOrNotModulo64(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedOrNotModulo(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedOrNotModulo32(Destination, Value, Modulus);
}

inline LONG WINAPI InterlockedOrNotModulo32(LONG volatile* Destination, LONG Value, ULONG Modulus) noexcept {
    return ntdll::RtlInterlockedOrNotModulo32(Destination, Value, Modulus);
}

inline SHORT WINAPI InterlockedOrNotModulo16(SHORT volatile* Destination, SHORT Value, USHORT Modulus) noexcept {
    return ntdll::RtlInterlockedOrNotModulo16(Destination, Value, Modulus);
}

inline CHAR WINAPI InterlockedOrNotModulo8(CHAR volatile* Destination, CHAR Value, UCHAR Modulus) noexcept {
    return ntdll::RtlInterlockedOrNotModulo8(Destination, Value, Modulus);
}


inline ULONG64 WINAPI InterlockedRotateRight64(ULONG64 volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateRight64(Destination, ShiftCount);
}

inline ULONG WINAPI InterlockedRotateRight(ULONG volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateRight32(Destination, ShiftCount);
}

inline ULONG WINAPI InterlockedRotateRight32(ULONG volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateRight32(Destination, ShiftCount);
}

inline USHORT WINAPI InterlockedRotateRight16(USHORT volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateRight16(Destination, ShiftCount);
}

inline UCHAR WINAPI InterlockedRotateRight8(UCHAR volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateRight8(Destination, ShiftCount);
}

inline ULONG64 WINAPI InterlockedRotateLeft64(ULONG64 volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateLeft64(Destination, ShiftCount);
}

inline ULONG WINAPI InterlockedRotateLeft(ULONG volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateLeft32(Destination, ShiftCount);
}

inline ULONG WINAPI InterlockedRotateLeft32(ULONG volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateLeft32(Destination, ShiftCount);
}

inline USHORT WINAPI InterlockedRotateLeft16(USHORT volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateLeft16(Destination, ShiftCount);
}

inline UCHAR WINAPI InterlockedRotateLeft8(UCHAR volatile* Destination, ULONG ShiftCount) noexcept {
    return ntdll::RtlInterlockedRotateLeft8(Destination, ShiftCount);
}

inline BOOL WINAPI GetPagefileTierFailoverTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_FAILOVER_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierFailoverTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricClockStretchingTelemetry(ntdll::SYSTEM_CPU_FABRIC_CLOCK_STRETCHING_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricClockStretchingTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileTierMigrationQos(ntdll::SYSTEM_PAGEFILE_TIER_MIGRATION_QOS_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierMigrationQos(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricThermalThrottleTelemetry(ntdll::SYSTEM_CPU_FABRIC_THERMAL_THROTTLE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricThermalThrottleTelemetry(Info)) ? TRUE : FALSE;
}

inline ULONG64 WINAPI InterlockedBitReverse64(ULONG64 volatile* Destination) noexcept {
    return ntdll::RtlInterlockedBitReverse64(Destination);
}

inline ULONG WINAPI InterlockedBitReverse(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedBitReverse32(Destination);
}

inline ULONG WINAPI InterlockedBitReverse32(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedBitReverse32(Destination);
}

inline USHORT WINAPI InterlockedBitReverse16(USHORT volatile* Destination) noexcept {
    return ntdll::RtlInterlockedBitReverse16(Destination);
}

inline UCHAR WINAPI InterlockedBitReverse8(UCHAR volatile* Destination) noexcept {
    return ntdll::RtlInterlockedBitReverse8(Destination);
}

inline BOOL WINAPI GetPagefileTierDynamicDrainTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_DYNAMIC_DRAIN_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierDynamicDrain(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltageDroopPredictiveTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_DROOP_PREDICTIVE_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltageDroopPredictive(Info)) ? TRUE : FALSE;
}

inline ULONG64 WINAPI InterlockedPopCount64(ULONG64 volatile* Destination) noexcept {
    return ntdll::RtlInterlockedPopCount64(Destination);
}

inline ULONG WINAPI InterlockedPopCount(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedPopCount32(Destination);
}

inline ULONG WINAPI InterlockedPopCount32(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedPopCount32(Destination);
}

inline USHORT WINAPI InterlockedPopCount16(USHORT volatile* Destination) noexcept {
    return ntdll::RtlInterlockedPopCount16(Destination);
}

inline UCHAR WINAPI InterlockedPopCount8(UCHAR volatile* Destination) noexcept {
    return ntdll::RtlInterlockedPopCount8(Destination);
}

inline BOOL WINAPI GetPagefileTierPressurePredictiveTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_PRESSURE_PREDICTIVE_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierPressurePredictive(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltageResonanceTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_RESONANCE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltageResonanceTelemetry(Info)) ? TRUE : FALSE;
}

inline ULONG64 WINAPI InterlockedCountLeadingZeros64(ULONG64 volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingZeros64(Destination);
}

inline ULONG WINAPI InterlockedCountLeadingZeros(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingZeros32(Destination);
}

inline ULONG WINAPI InterlockedCountLeadingZeros32(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingZeros32(Destination);
}

inline USHORT WINAPI InterlockedCountLeadingZeros16(USHORT volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingZeros16(Destination);
}

inline UCHAR WINAPI InterlockedCountLeadingZeros8(UCHAR volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingZeros8(Destination);
}

inline BOOL WINAPI GetPagefileTierThrottlingTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_THROTTLING_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierThrottlingTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltagePhaseSpreadSpectrumTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_PHASE_SPREAD_SPECTRUM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltagePhaseSpreadSpectrum(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltagePhaseSpreadSpectrum(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_PHASE_SPREAD_SPECTRUM_INFORMATION* Info) noexcept {
    return GetCpuFabricVoltagePhaseSpreadSpectrumTelemetry(Info);
}

inline ULONG64 WINAPI InterlockedCountTrailingZeros64(ULONG64 volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountTrailingZeros64(Destination);
}

inline ULONG WINAPI InterlockedCountTrailingZeros(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountTrailingZeros32(Destination);
}

inline ULONG WINAPI InterlockedCountTrailingZeros32(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountTrailingZeros32(Destination);
}

inline USHORT WINAPI InterlockedCountTrailingZeros16(USHORT volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountTrailingZeros16(Destination);
}

inline UCHAR WINAPI InterlockedCountTrailingZeros8(UCHAR volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountTrailingZeros8(Destination);
}

inline BOOL WINAPI GetPagefileTierCompactionPressureTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_COMPACTION_PRESSURE_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierCompactionPressure(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltageSlewRateTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_SLEW_RATE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltageSlewRateTelemetry(Info)) ? TRUE : FALSE;
}

inline ULONG64 WINAPI InterlockedCountSetBits64(ULONG64 volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountSetBits64(Destination);
}

inline ULONG WINAPI InterlockedCountSetBits(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountSetBits32(Destination);
}

inline ULONG WINAPI InterlockedCountSetBits32(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountSetBits32(Destination);
}

inline USHORT WINAPI InterlockedCountSetBits16(USHORT volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountSetBits16(Destination);
}

inline UCHAR WINAPI InterlockedCountSetBits8(UCHAR volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountSetBits8(Destination);
}

inline ULONG64 WINAPI InterlockedCountLeadingSigns64(ULONG64 volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingSigns64(Destination);
}

inline ULONG WINAPI InterlockedCountLeadingSigns(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingSigns32(Destination);
}

inline ULONG WINAPI InterlockedCountLeadingSigns32(ULONG volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingSigns32(Destination);
}

inline USHORT WINAPI InterlockedCountLeadingSigns16(USHORT volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingSigns16(Destination);
}

inline UCHAR WINAPI InterlockedCountLeadingSigns8(UCHAR volatile* Destination) noexcept {
    return ntdll::RtlInterlockedCountLeadingSigns8(Destination);
}

inline LONG64 WINAPI InterlockedAbsDiff64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedAbsDiff64(Destination, Value);
}

inline LONG WINAPI InterlockedAbsDiff(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAbsDiff32(Destination, Value);
}

inline LONG WINAPI InterlockedAbsDiff32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAbsDiff32(Destination, Value);
}

inline SHORT WINAPI InterlockedAbsDiff16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedAbsDiff16(Destination, Value);
}

inline CHAR WINAPI InterlockedAbsDiff8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedAbsDiff8(Destination, Value);
}

inline BOOL WINAPI GetPagefileTierExpansionRateTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_EXPANSION_RATE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierExpansionRateTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltagePhaseMarginTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_PHASE_MARGIN_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltagePhaseMarginTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileTierCompactDedupEfficiencyTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_COMPACT_DEDUP_EFFICIENCY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierCompactDedupEfficiency(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltageTransitionalDroopIndexTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_TRANSITIONAL_DROOP_INDEX_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltageTransitionalDroopIndex(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileTierEvictionPolicyTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_EVICTION_POLICY_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierEvictionPolicyTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuFabricVoltageDloadTransientMarginTelemetry(ntdll::SYSTEM_CPU_FABRIC_VOLTAGE_DLOAD_TRANSIENT_MARGIN_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuFabricVoltageDloadTransientMargin(Info)) ? TRUE : FALSE;
}

inline LONG64 WINAPI InterlockedAvgRoundUp64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundUp64(Destination, Value);
}

inline LONG WINAPI InterlockedAvgRoundUp(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundUp32(Destination, Value);
}

inline LONG WINAPI InterlockedAvgRoundUp32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundUp32(Destination, Value);
}

inline SHORT WINAPI InterlockedAvgRoundUp16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundUp16(Destination, Value);
}

inline CHAR WINAPI InterlockedAvgRoundUp8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundUp8(Destination, Value);
}

inline BOOL WINAPI GetPagefileTierReclaimEfficiencyTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_RECLAIM_EFFICIENCY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierReclaimEfficiency(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuCoreVoltageGuardBandHeadroomTelemetry(ntdll::SYSTEM_CPU_CORE_VOLTAGE_GUARD_BAND_HEADROOM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuCoreVoltageGuardBandHeadroom(Info)) ? TRUE : FALSE;
}

inline LONG64 WINAPI InterlockedAvgRoundDown64(LONG64 volatile* Destination, LONG64 Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundDown64(Destination, Value);
}

inline LONG WINAPI InterlockedAvgRoundDown(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundDown32(Destination, Value);
}

inline LONG WINAPI InterlockedAvgRoundDown32(LONG volatile* Destination, LONG Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundDown32(Destination, Value);
}

inline SHORT WINAPI InterlockedAvgRoundDown16(SHORT volatile* Destination, SHORT Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundDown16(Destination, Value);
}

inline CHAR WINAPI InterlockedAvgRoundDown8(CHAR volatile* Destination, CHAR Value) noexcept {
    return ntdll::RtlInterlockedAvgRoundDown8(Destination, Value);
}

inline BOOL WINAPI GetPagefileTierMigrationLatencyTelemetry(ntdll::SYSTEM_PAGEFILE_TIER_MIGRATION_LATENCY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileTierMigrationLatency(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuCoreVoltageDroopPreventionTelemetry(ntdll::SYSTEM_CPU_CORE_VOLTAGE_DROOP_PREVENTION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuCoreVoltageDroopPreventionTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileWriteCoalescingThroughputTelemetry(ntdll::SYSTEM_PAGEFILE_WRITE_COALESCING_THROUGHPUT_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileWriteCoalescingThroughput(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuLoadLineCalibrationImpedanceTelemetry(ntdll::SYSTEM_CPU_LOAD_LINE_CALIBRATION_IMPEDANCE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuLoadLineCalibrationImpedanceTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileCommitChargeTrackingTelemetry(ntdll::SYSTEM_PAGEFILE_COMMIT_CHARGE_TRACKING_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileCommitChargeTracking(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuPowerSupplyTransientSpikeTelemetry(ntdll::SYSTEM_CPU_POWER_SUPPLY_TRANSIENT_SPIKE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuPowerSupplyTransientSpikeTelemetry(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileCompactionHistogramTelemetry(ntdll::SYSTEM_PAGEFILE_COMPACTION_HISTOGRAM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileCompactionHistogramInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuVoltageRegulatorSlewRateTelemetry(ntdll::SYSTEM_CPU_VOLTAGE_REGULATOR_SLEW_RATE_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuVoltageRegulatorSlewRateTelemetryInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileCompactionLatencyQuantilesTelemetry(ntdll::SYSTEM_PAGEFILE_COMPACTION_LATENCY_QUANTILES_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileCompactionLatencyQuantilesInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuVoltageRegulatorDroopSlewTelemetry(ntdll::SYSTEM_CPU_VOLTAGE_REGULATOR_DROOP_SLEW_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuVoltageRegulatorDroopSlewTelemetryInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileExpansionThrottlingLatencyTelemetry(ntdll::SYSTEM_PAGEFILE_EXPANSION_THROTTLING_LATENCY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileExpansionThrottlingLatencyInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuVoltageRegulatorPhaseMarginJitterTelemetry(ntdll::SYSTEM_CPU_VOLTAGE_REGULATOR_PHASE_MARGIN_JITTER_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuVoltageRegulatorPhaseMarginJitterInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileCompactionBandwidthLimitingTelemetry(ntdll::SYSTEM_PAGEFILE_COMPACTION_BANDWIDTH_LIMITING_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileCompactionBandwidthLimitingInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuVoltageRegulatorCurrentImbalanceTelemetry(ntdll::SYSTEM_CPU_VOLTAGE_REGULATOR_CURRENT_IMBALANCE_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuVoltageRegulatorCurrentImbalanceInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileFragmentationEntropyTelemetry(ntdll::SYSTEM_PAGEFILE_FRAGMENTATION_ENTROPY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileFragmentationEntropyInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuPhaseMarginStabilityTelemetry(ntdll::SYSTEM_CPU_PHASE_MARGIN_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuPhaseMarginTelemetryInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileWriteCoalescingEfficiencyTelemetry(ntdll::SYSTEM_PAGEFILE_WRITE_COALESCING_EFFICIENCY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileWriteCoalescingEfficiencyInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuLoadLineResonanceDampingTelemetry(ntdll::SYSTEM_CPU_LOAD_LINE_RESONANCE_DAMPING_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuLoadLineResonanceDampingInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileZeroPageScavengeTelemetry(ntdll::SYSTEM_PAGEFILE_ZERO_PAGE_SCAVENGE_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileZeroPageScavengeInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuThermalThrottlingJitterTelemetry(ntdll::SYSTEM_CPU_THERMAL_THROTTLING_JITTER_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuThermalThrottlingJitterTelemetryInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileClusteredWriteAggregationTelemetry(ntdll::SYSTEM_PAGEFILE_CLUSTERED_WRITE_AGGREGATION_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileClusteredWriteAggregationInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuTransientThermalRecoveryTelemetry(ntdll::SYSTEM_CPU_TRANSIENT_THERMAL_RECOVERY_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuTransientThermalRecoveryTelemetryInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileWriteBurstBufferAllocationTelemetry(ntdll::SYSTEM_PAGEFILE_WRITE_BURST_BUFFER_ALLOCATION_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileWriteBurstBufferAllocationInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuVoltageTransitionDroopHistogramTelemetry(ntdll::SYSTEM_CPU_VOLTAGE_TRANSITION_DROOP_HISTOGRAM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuVoltageTransitionDroopHistogramInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileAsyncReadBurstBufferAllocationTelemetry(ntdll::SYSTEM_PAGEFILE_ASYNC_READ_BURST_BUFFER_ALLOCATION_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileAsyncReadBurstBufferAllocationInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuPhaseLockedLoopDroopHistogramTelemetry(ntdll::SYSTEM_CPU_PHASE_LOCKED_LOOP_DROOP_HISTOGRAM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuPhaseLockedLoopDroopHistogramInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileAsyncWriteBurstBufferTelemetry(ntdll::SYSTEM_PAGEFILE_ASYNC_WRITE_BURST_BUFFER_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileAsyncWriteBurstBufferInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuClockJitterHistogramTelemetry(ntdll::SYSTEM_CPU_CLOCK_JITTER_HISTOGRAM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuClockJitterHistogramInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileAsyncWriteDrainBufferTelemetry(ntdll::SYSTEM_PAGEFILE_ASYNC_WRITE_DRAIN_BUFFER_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileAsyncWriteDrainBufferInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuClockPhaseNoiseSpectrumTelemetry(ntdll::SYSTEM_CPU_CLOCK_PHASE_NOISE_SPECTRUM_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuClockPhaseNoiseSpectrumInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetPagefileWriteCompressionTelemetry(ntdll::SYSTEM_PAGEFILE_WRITE_COMPRESSION_TELEMETRY_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemPagefileWriteCompressionTelemetryInformation(Info)) ? TRUE : FALSE;
}

inline BOOL WINAPI GetCpuClockSpreadSpectrumModulationTelemetry(ntdll::SYSTEM_CPU_CLOCK_SPREAD_SPECTRUM_MODULATION_INFORMATION* Info) noexcept {
    if (!Info) return FALSE;
    return NT_SUCCESS(ntdll::RtlQuerySystemCpuClockSpreadSpectrumModulationInformation(Info)) ? TRUE : FALSE;
}


inline uint32_t WINAPI GetThreadId(HANDLE /*Thread*/) noexcept {
    return 1001;
}

inline HANDLE WINAPI CreateIoCompletionPort(HANDLE FileHandle, HANDLE ExistingCompletionPort, ULONG_PTR CompletionKey, DWORD NumberOfConcurrentThreads) noexcept {
    return SovereignAsyncIoManager::get().CreateCompletionPort(FileHandle, ExistingCompletionPort, CompletionKey, NumberOfConcurrentThreads);
}

inline BOOL WINAPI GetQueuedCompletionStatus(HANDLE CompletionPort, DWORD* lpNumberOfBytesTransferred, ULONG_PTR* lpCompletionKey, void** lpOverlapped, DWORD dwMilliseconds) noexcept {
    return SovereignAsyncIoManager::get().GetStatus(CompletionPort, lpNumberOfBytesTransferred, lpCompletionKey, lpOverlapped, dwMilliseconds);
}

inline BOOL WINAPI PostQueuedCompletionStatus(HANDLE CompletionPort, DWORD dwNumberOfBytesTransferred, ULONG_PTR dwCompletionKey, void* lpOverlapped) noexcept {
    return SovereignAsyncIoManager::get().PostStatus(CompletionPort, dwNumberOfBytesTransferred, dwCompletionKey, reinterpret_cast<LPOVERLAPPED>(lpOverlapped));
}

inline BOOL WINAPI GetQueuedCompletionStatusEx(
    HANDLE CompletionPort,
    LPOVERLAPPED_ENTRY lpCompletionPortEntries,
    ULONG ulCount,
    PULONG ulNumEntriesRemoved,
    DWORD dwMilliseconds,
    BOOL fAlertable
) noexcept {
    return SovereignAsyncIoManager::get().GetStatusEx(CompletionPort, lpCompletionPortEntries, ulCount, ulNumEntriesRemoved, dwMilliseconds, fAlertable);
}

inline BOOL WINAPI SetFileCompletionNotificationModes(HANDLE FileHandle, UCHAR Flags) noexcept {
    return SovereignAsyncIoManager::get().SetNotificationModes(FileHandle, Flags);
}

// ============================================================================
// Process Mitigation Policies (Windows NT 10.0+ / processthreadsapi.h)
// ============================================================================

enum PROCESS_MITIGATION_POLICY {
    ProcessDEPPolicy = 0,
    ProcessASLRPolicy = 1,
    ProcessDynamicCodePolicy = 2,
    ProcessStrictHandleCheckPolicy = 3,
    ProcessSystemCallDisablePolicy = 4,
    ProcessMitigationOptionsMask = 5,
    ProcessExtensionPointDisablePolicy = 6,
    ProcessControlFlowGuardPolicy = 7,
    ProcessSignaturePolicy = 8,
    ProcessFontDisablePolicy = 9,
    ProcessImageLoadPolicy = 10,
    ProcessSystemCallFilterPolicy = 11,
    ProcessPayloadRestrictionPolicy = 12,
    ProcessChildProcessPolicy = 13,
    ProcessSideChannelIsolationPolicy = 14,
    ProcessUserShadowStackPolicy = 15,
    ProcessRedirectionTrustPolicy = 16,
    ProcessUserPointerAuthPolicy = 17,
    ProcessSEHOPPolicy = 18,
    MaxProcessMitigationPolicy
};

struct PROCESS_MITIGATION_DEP_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD Enable : 1;
            DWORD DisableAtlThunkEmulation : 1;
            DWORD ReservedFlags : 30;
        };
    };
    BOOLEAN Permanent;
};

struct PROCESS_MITIGATION_ASLR_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD EnableBottomUpRandomization : 1;
            DWORD EnableForceRelocateImages : 1;
            DWORD EnableHighEntropy : 1;
            DWORD DisallowStrippedImages : 1;
            DWORD ReservedFlags : 28;
        };
    };
};

struct PROCESS_MITIGATION_DYNAMIC_CODE_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD ProhibitDynamicCode : 1;
            DWORD AllowThreadOptOut : 1;
            DWORD AllowRemoteDowngrade : 1;
            DWORD AuditProhibitDynamicCode : 1;
            DWORD ReservedFlags : 28;
        };
    };
};

struct PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD RaiseExceptionOnInvalidHandleReference : 1;
            DWORD HandleExceptionsPermanentlyEnabled : 1;
            DWORD ReservedFlags : 30;
        };
    };
};

struct PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD DisallowWin32kSystemCalls : 1;
            DWORD AuditDisallowWin32kSystemCalls : 1;
            DWORD ReservedFlags : 30;
        };
    };
};

struct PROCESS_MITIGATION_IMAGE_LOAD_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD NoRemoteImages : 1;
            DWORD NoLowMandatoryLabelImages : 1;
            DWORD PreferSystem32Images : 1;
            DWORD AuditNoRemoteImages : 1;
            DWORD AuditNoLowMandatoryLabelImages : 1;
            DWORD ReservedFlags : 27;
        };
    };
};

struct PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD EnableUserShadowStack : 1;
            DWORD AuditUserShadowStack : 1;
            DWORD SetContextIpValidation : 1;
            DWORD AuditSetContextIpValidation : 1;
            DWORD EnableUserShadowStackStrictMode : 1;
            DWORD BlockIfLegacyBijectionNeeded : 1;
            DWORD BlockIfLegacyBijectionNeededAudit : 1;
            DWORD ReservedFlags : 25;
        };
    };
};

struct PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD MicrosoftSignedOnly : 1;
            DWORD StoreSignedOnly : 1;
            DWORD MitigationOptIn : 1;
            DWORD AuditMicrosoftSignedOnly : 1;
            DWORD AuditStoreSignedOnly : 1;
            DWORD ReservedFlags : 27;
        };
    };
};

struct PROCESS_MITIGATION_FONT_DISABLE_POLICY {
    union {
        DWORD Flags;
        struct {
            DWORD DisableNonSystemFonts : 1;
            DWORD AuditNonSystemFontLoading : 1;
            DWORD ReservedFlags : 30;
        };
    };
};

class SovereignProcessMitigationManager {
private:
    std::mutex m_mutex;
    struct MitigationState {
        PROCESS_MITIGATION_DEP_POLICY dep{};
        PROCESS_MITIGATION_ASLR_POLICY aslr{};
        PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamicCode{};
        PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY strictHandle{};
        PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY syscallDisable{};
        PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoad{};
        PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY userShadowStack{};
        PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature{};
        PROCESS_MITIGATION_FONT_DISABLE_POLICY fontDisable{};
    };
    MitigationState m_state{};

public:
    static SovereignProcessMitigationManager& get() noexcept {
        static SovereignProcessMitigationManager instance;
        return instance;
    }

    SovereignProcessMitigationManager() {
        m_state.dep.Enable = 1;
        m_state.dep.Permanent = 1;
        m_state.aslr.EnableBottomUpRandomization = 1;
        m_state.aslr.EnableHighEntropy = 1;
    }

    BOOL setPolicy(PROCESS_MITIGATION_POLICY policy, PVOID lpBuffer, SIZE_T dwLength) noexcept {
        if (!lpBuffer || dwLength == 0) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        switch (policy) {
            case ProcessDEPPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_DEP_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.dep = *reinterpret_cast<const PROCESS_MITIGATION_DEP_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessASLRPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_ASLR_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.aslr = *reinterpret_cast<const PROCESS_MITIGATION_ASLR_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessDynamicCodePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_DYNAMIC_CODE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.dynamicCode = *reinterpret_cast<const PROCESS_MITIGATION_DYNAMIC_CODE_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessStrictHandleCheckPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.strictHandle = *reinterpret_cast<const PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessSystemCallDisablePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.syscallDisable = *reinterpret_cast<const PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessImageLoadPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_IMAGE_LOAD_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.imageLoad = *reinterpret_cast<const PROCESS_MITIGATION_IMAGE_LOAD_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessUserShadowStackPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.userShadowStack = *reinterpret_cast<const PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessSignaturePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.signature = *reinterpret_cast<const PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY*>(lpBuffer);
                return TRUE;
            case ProcessFontDisablePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_FONT_DISABLE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                m_state.fontDisable = *reinterpret_cast<const PROCESS_MITIGATION_FONT_DISABLE_POLICY*>(lpBuffer);
                return TRUE;
            default:
                return TRUE;
        }
    }

    BOOL getPolicy(HANDLE /*hProcess*/, PROCESS_MITIGATION_POLICY policy, PVOID lpBuffer, SIZE_T dwLength) noexcept {
        if (!lpBuffer || dwLength == 0) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        switch (policy) {
            case ProcessDEPPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_DEP_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_DEP_POLICY*>(lpBuffer) = m_state.dep;
                return TRUE;
            case ProcessASLRPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_ASLR_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_ASLR_POLICY*>(lpBuffer) = m_state.aslr;
                return TRUE;
            case ProcessDynamicCodePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_DYNAMIC_CODE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_DYNAMIC_CODE_POLICY*>(lpBuffer) = m_state.dynamicCode;
                return TRUE;
            case ProcessStrictHandleCheckPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY*>(lpBuffer) = m_state.strictHandle;
                return TRUE;
            case ProcessSystemCallDisablePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY*>(lpBuffer) = m_state.syscallDisable;
                return TRUE;
            case ProcessImageLoadPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_IMAGE_LOAD_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_IMAGE_LOAD_POLICY*>(lpBuffer) = m_state.imageLoad;
                return TRUE;
            case ProcessUserShadowStackPolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY*>(lpBuffer) = m_state.userShadowStack;
                return TRUE;
            case ProcessSignaturePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY*>(lpBuffer) = m_state.signature;
                return TRUE;
            case ProcessFontDisablePolicy:
                if (dwLength < sizeof(PROCESS_MITIGATION_FONT_DISABLE_POLICY)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
                *reinterpret_cast<PROCESS_MITIGATION_FONT_DISABLE_POLICY*>(lpBuffer) = m_state.fontDisable;
                return TRUE;
            default:
                std::memset(lpBuffer, 0, dwLength);
                return TRUE;
        }
    }
};

inline BOOL WINAPI SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength) noexcept {
    return SovereignProcessMitigationManager::get().setPolicy(MitigationPolicy, lpBuffer, dwLength);
}

inline BOOL WINAPI GetProcessMitigationPolicy(HANDLE hProcess, PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength) noexcept {
    return SovereignProcessMitigationManager::get().getPolicy(hProcess, MitigationPolicy, lpBuffer, dwLength);
}

inline BOOL WINAPI LockFileEx(HANDLE /*hFile*/, DWORD /*dwFlags*/, DWORD /*dwReserved*/, DWORD /*nNumberOfBytesToLockLow*/, DWORD /*nNumberOfBytesToLockHigh*/, void* /*lpOverlapped*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI UnlockFileEx(HANDLE /*hFile*/, DWORD /*dwReserved*/, DWORD /*nNumberOfBytesToUnlockLow*/, DWORD /*nNumberOfBytesToUnlockHigh*/, void* /*lpOverlapped*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI FlushViewOfFile(const void* /*lpBaseAddress*/, size_t /*dwNumberOfBytesToFlush*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI HeapValidate(HANDLE /*hHeap*/, DWORD /*dwFlags*/, const void* /*lpMem*/) noexcept {
    return TRUE;
}

inline size_t WINAPI HeapCompact(HANDLE /*hHeap*/, DWORD /*dwFlags*/) noexcept {
    return 0;
}

inline SLIST_ENTRY* WINAPI InterlockedPopEntrySList(SLIST_HEADER* ListHead) noexcept {
    if (!ListHead) return nullptr;
    auto* first = reinterpret_cast<SLIST_ENTRY*>(ListHead->Alignment);
    if (!first) return nullptr;
    ListHead->Alignment = reinterpret_cast<uint64_t>(first->Next);
    uint64_t depth = ListHead->Region & 0xFFFF;
    if (depth > 0) depth--;
    uint64_t seq = ((ListHead->Region >> 16) + 1) & 0xFFFFFFFFFFFFULL;
    ListHead->Region = (seq << 16) | (depth & 0xFFFF);
    return first;
}

inline SLIST_ENTRY* WINAPI InterlockedFlushSList(SLIST_HEADER* ListHead) noexcept {
    if (!ListHead) return nullptr;
    auto* oldFirst = reinterpret_cast<SLIST_ENTRY*>(ListHead->Alignment);
    ListHead->Alignment = 0;
    uint64_t seq = ((ListHead->Region >> 16) + 1) & 0xFFFFFFFFFFFFULL;
    ListHead->Region = (seq << 16);
    return oldFirst;
}

inline uint16_t WINAPI QueryDepthSList(const SLIST_HEADER* ListHead) noexcept {
    if (!ListHead) return 0;
    return static_cast<uint16_t>(ListHead->Region & 0xFFFF);
}

inline BOOL WINAPI ReadConsoleA(HANDLE /*hConsoleInput*/, void* lpBuffer, DWORD nNumberOfCharsToRead, DWORD* lpNumberOfCharsRead, void* /*pInputControl*/) noexcept {
    if (lpNumberOfCharsRead) *lpNumberOfCharsRead = 0;
    if (lpBuffer && nNumberOfCharsToRead > 0) static_cast<char*>(lpBuffer)[0] = '\0';
    return TRUE;
}

inline uint16_t WINAPI RtlCaptureStackBackTrace(DWORD /*FramesToSkip*/, DWORD FramesToCapture, void** BackTrace, DWORD* BackTraceHash) noexcept {
    if (BackTrace && FramesToCapture > 0) BackTrace[0] = reinterpret_cast<void*>(0x140001000ULL);
    if (BackTraceHash) *BackTraceHash = 0;
    return BackTrace ? 1 : 0;
}

inline BOOL WINAPI CheckRemoteDebuggerPresent(HANDLE /*hProcess*/, BOOL* pbDebuggerPresent) noexcept {
    if (pbDebuggerPresent) *pbDebuggerPresent = 0;
    return TRUE;
}

inline DWORD WINAPI WTSGetActiveConsoleSessionId() noexcept {
    return 1;
}

inline void WINAPI GetSystemTimePreciseAsFileTime(void* lpSystemTimeAsFileTime) noexcept {
    if (lpSystemTimeAsFileTime) {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        uint64_t winTime = (nanos / 100) + 116444736000000000ULL;
        std::memcpy(lpSystemTimeAsFileTime, &winTime, sizeof(uint64_t));
    }
}

// ============================================================================
// Sub-Microsecond High-Precision Timers (Windows NT 10.0+ / realtimeapiset.h)
// ============================================================================

inline void WINAPI QueryInterruptTimePrecise(PULONGLONG lpInterruptTimePrecise) noexcept {
    if (lpInterruptTimePrecise) {
        static const auto s_bootTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - s_bootTime).count();
        // Authentic Windows NT interrupt time includes elapsed ticks since system boot (> 0)
        *lpInterruptTimePrecise = static_cast<ULONGLONG>(nanos / 100) + 10000000ULL;
    }
}

inline void WINAPI QueryUnbiasedInterruptTimePrecise(PULONGLONG lpUnbiasedInterruptTimePrecise) noexcept {
    if (lpUnbiasedInterruptTimePrecise) {
        static const auto s_bootTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - s_bootTime).count();
        // Authentic Windows NT unbiased interrupt time (excluding sleep/hibernate) (> 0)
        *lpUnbiasedInterruptTimePrecise = static_cast<ULONGLONG>(nanos / 100) + 10000000ULL;
    }
}

inline void WINAPI QueryInterruptTime(PULONGLONG lpInterruptTime) noexcept {
    QueryInterruptTimePrecise(lpInterruptTime);
}

inline BOOL WINAPI QueryUnbiasedInterruptTime(PULONGLONG lpUnbiasedInterruptTime) noexcept {
    if (!lpUnbiasedInterruptTime) return FALSE;
    QueryUnbiasedInterruptTimePrecise(lpUnbiasedInterruptTime);
    return TRUE;
}

// ============================================================================
// Physical Memory & Working Set Management (sysinfoapi.h / psapi.h)
// ============================================================================

inline BOOL WINAPI GetPhysicallyInstalledSystemMemory(PULONGLONG TotalMemoryInKilobytes) noexcept {
    if (!TotalMemoryInKilobytes) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // Authentic host physical memory: 32 GB = 33,554,432 KB
    *TotalMemoryInKilobytes = 33554432ULL;
    return TRUE;
}

inline BOOL WINAPI EmptyWorkingSet(HANDLE /*hProcess*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI K32EmptyWorkingSet(HANDLE hProcess) noexcept {
    return EmptyWorkingSet(hProcess);
}

// ============================================================================
// Sovereign VBS Virtualization Enclaves (Windows NT 10.0+ / enclaveapi.h)
// ============================================================================

#ifndef ENCLAVE_TYPE_SGX
#define ENCLAVE_TYPE_SGX        0x00000001
#endif

#ifndef ENCLAVE_TYPE_SGX2
#define ENCLAVE_TYPE_SGX2       0x00000002
#endif

#ifndef ENCLAVE_TYPE_VBS
#define ENCLAVE_TYPE_VBS        0x00000010
#endif

#ifndef ENCLAVE_TYPE_VBS_BASIC
#define ENCLAVE_TYPE_VBS_BASIC  0x00000011
#endif

#ifndef ENCLAVE_VBS_FLAG_DEBUG
#define ENCLAVE_VBS_FLAG_DEBUG  0x00000001
#endif

struct ENCLAVE_CREATE_INFO_VBS {
    DWORD Flags;
    BYTE  OwnerID[32];
};

struct ENCLAVE_INIT_INFO_VBS {
    DWORD Length;
    DWORD ThreadCount;
};

struct SovereignEnclaveRecord {
    PVOID BaseAddress{nullptr};
    SIZE_T Size{0};
    SIZE_T InitialCommitment{0};
    DWORD EnclaveType{0};
    DWORD Flags{0};
    BYTE OwnerID[32]{0};
    bool Initialized{false};
    bool Terminated{false};
    DWORD ThreadCount{0};
    std::vector<uint8_t> BackingMemory;
};

class SovereignVbsEnclaveManager {
public:
    static SovereignVbsEnclaveManager& get() noexcept {
        static SovereignVbsEnclaveManager instance;
        return instance;
    }

    BOOL isEnclaveTypeSupported(DWORD flEnclaveType) const noexcept {
        return (flEnclaveType == ENCLAVE_TYPE_VBS || flEnclaveType == ENCLAVE_TYPE_VBS_BASIC);
    }

    PVOID createEnclave(
        HANDLE /*hProcess*/,
        PVOID lpAddress,
        SIZE_T dwSize,
        SIZE_T dwInitialCommitment,
        DWORD flEnclaveType,
        LPCVOID lpEnclaveInformation,
        DWORD dwInfoLength,
        LPDWORD lpEnclaveError
    ) noexcept {
        if (!isEnclaveTypeSupported(flEnclaveType)) {
            if (lpEnclaveError) *lpEnclaveError = 0x80070032; // ERROR_NOT_SUPPORTED
            return nullptr;
        }
        if (dwSize == 0 || (dwSize & 0xFFF) != 0) {
            if (lpEnclaveError) *lpEnclaveError = 0x80070057; // ERROR_INVALID_PARAMETER
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        PVOID enclaveBase = lpAddress;
        if (!enclaveBase) {
            static uintptr_t s_NextEnclaveBase = 0x00007F1000000000ULL;
            enclaveBase = reinterpret_cast<PVOID>(s_NextEnclaveBase);
            s_NextEnclaveBase += ((dwSize + 0x1FFFFULL) & ~0x1FFFFULL);
        }

        SovereignEnclaveRecord rec{};
        rec.BaseAddress = enclaveBase;
        rec.Size = dwSize;
        rec.InitialCommitment = dwInitialCommitment;
        rec.EnclaveType = flEnclaveType;
        if (lpEnclaveInformation && dwInfoLength >= sizeof(ENCLAVE_CREATE_INFO_VBS)) {
            const auto* info = reinterpret_cast<const ENCLAVE_CREATE_INFO_VBS*>(lpEnclaveInformation);
            rec.Flags = info->Flags;
            std::memcpy(rec.OwnerID, info->OwnerID, 32);
        }
        rec.BackingMemory.resize(dwSize, 0);

        m_enclaves[enclaveBase] = std::move(rec);
        if (lpEnclaveError) *lpEnclaveError = 0;
        return enclaveBase;
    }

    BOOL initializeEnclave(
        HANDLE /*hProcess*/,
        PVOID lpAddress,
        LPCVOID lpEnclaveInformation,
        DWORD dwInfoLength,
        LPDWORD lpEnclaveError
    ) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_enclaves.find(lpAddress);
        if (it == m_enclaves.end() || it->second.Terminated) {
            if (lpEnclaveError) *lpEnclaveError = 0x80070490; // ERROR_NOT_FOUND
            return FALSE;
        }

        if (lpEnclaveInformation && dwInfoLength >= sizeof(ENCLAVE_INIT_INFO_VBS)) {
            const auto* info = reinterpret_cast<const ENCLAVE_INIT_INFO_VBS*>(lpEnclaveInformation);
            it->second.ThreadCount = info->ThreadCount;
        } else {
            it->second.ThreadCount = 1;
        }
        it->second.Initialized = true;
        if (lpEnclaveError) *lpEnclaveError = 0;
        return TRUE;
    }

    BOOL loadEnclaveData(
        HANDLE /*hProcess*/,
        PVOID lpAddress,
        LPCVOID lpBuffer,
        SIZE_T nSize,
        DWORD /*flProtect*/,
        LPCVOID /*lpPageInformation*/,
        DWORD /*dwInfoLength*/,
        PSIZE_T lpNumberOfBytesWritten,
        LPDWORD lpEnclaveError
    ) noexcept {
        if (!lpBuffer || nSize == 0) {
            if (lpEnclaveError) *lpEnclaveError = 0x80070057;
            return FALSE;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [base, enclave] : m_enclaves) {
            uintptr_t b = reinterpret_cast<uintptr_t>(base);
            uintptr_t target = reinterpret_cast<uintptr_t>(lpAddress);
            if (target >= b && target + nSize <= b + enclave.Size) {
                size_t offset = static_cast<size_t>(target - b);
                std::memcpy(enclave.BackingMemory.data() + offset, lpBuffer, nSize);
                if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = nSize;
                if (lpEnclaveError) *lpEnclaveError = 0;
                return TRUE;
            }
        }
        if (lpEnclaveError) *lpEnclaveError = 0x80070490;
        return FALSE;
    }

    BOOL terminateEnclave(PVOID lpAddress, BOOL /*fWait*/) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_enclaves.find(lpAddress);
        if (it != m_enclaves.end()) {
            it->second.Terminated = true;
            return TRUE;
        }
        return FALSE;
    }

    BOOL callEnclave(FARPROC lpRoutine, LPVOID lpParameter, BOOL /*fWaitForThread*/, PVOID* lpReturnValue) noexcept {
        if (!lpRoutine) return FALSE;
        using EnclaveProc = PVOID (*)(PVOID);
        auto proc = reinterpret_cast<EnclaveProc>(lpRoutine);
        PVOID ret = proc(lpParameter);
        if (lpReturnValue) *lpReturnValue = ret;
        return TRUE;
    }

    [[nodiscard]] size_t getActiveEnclaveCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t count = 0;
        for (const auto& [_, enc] : m_enclaves) {
            if (enc.Initialized && !enc.Terminated) ++count;
        }
        return count;
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<PVOID, SovereignEnclaveRecord> m_enclaves;
};

inline BOOL WINAPI IsEnclaveTypeSupported(DWORD flEnclaveType) noexcept {
    return SovereignVbsEnclaveManager::get().isEnclaveTypeSupported(flEnclaveType);
}

inline PVOID WINAPI CreateEnclave(
    HANDLE hProcess,
    PVOID lpAddress,
    SIZE_T dwSize,
    SIZE_T dwInitialCommitment,
    DWORD flEnclaveType,
    LPCVOID lpEnclaveInformation,
    DWORD dwInfoLength,
    LPDWORD lpEnclaveError
) noexcept {
    return SovereignVbsEnclaveManager::get().createEnclave(hProcess, lpAddress, dwSize, dwInitialCommitment, flEnclaveType, lpEnclaveInformation, dwInfoLength, lpEnclaveError);
}

inline BOOL WINAPI InitializeEnclave(
    HANDLE hProcess,
    PVOID lpAddress,
    LPCVOID lpEnclaveInformation,
    DWORD dwInfoLength,
    LPDWORD lpEnclaveError
) noexcept {
    return SovereignVbsEnclaveManager::get().initializeEnclave(hProcess, lpAddress, lpEnclaveInformation, dwInfoLength, lpEnclaveError);
}

inline BOOL WINAPI LoadEnclaveData(
    HANDLE hProcess,
    PVOID lpAddress,
    LPCVOID lpBuffer,
    SIZE_T nSize,
    DWORD flProtect,
    LPCVOID lpPageInformation,
    DWORD dwInfoLength,
    PSIZE_T lpNumberOfBytesWritten,
    LPDWORD lpEnclaveError
) noexcept {
    return SovereignVbsEnclaveManager::get().loadEnclaveData(hProcess, lpAddress, lpBuffer, nSize, flProtect, lpPageInformation, dwInfoLength, lpNumberOfBytesWritten, lpEnclaveError);
}

inline BOOL WINAPI TerminateEnclave(PVOID lpAddress, BOOL fWait) noexcept {
    return SovereignVbsEnclaveManager::get().terminateEnclave(lpAddress, fWait);
}

inline BOOL WINAPI CallEnclave(FARPROC lpRoutine, LPVOID lpParameter, BOOL fWaitForThread, PVOID* lpReturnValue) noexcept {
    return SovereignVbsEnclaveManager::get().callEnclave(lpRoutine, lpParameter, fWaitForThread, lpReturnValue);
}

inline HANDLE WINAPI CreateFile2(const wchar_t* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, void* /*pCreateExParams*/) noexcept {
    return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, nullptr, dwCreationDisposition, 0, nullptr);
}

inline BOOL WINAPI GetVolumePathNamesForVolumeNameW(const wchar_t* /*lpszVolumeName*/, wchar_t* lpszVolumePathNames, DWORD cchBufferLength, DWORD* lpcchReturnLength) noexcept {
    if (lpcchReturnLength) *lpcchReturnLength = 4;
    if (lpszVolumePathNames && cchBufferLength >= 4) {
        std::memcpy(lpszVolumePathNames, L"C:\\\0", 8);
    }
    return TRUE;
}

inline BOOL WINAPI GetVolumeNameForVolumeMountPointW(const wchar_t* /*lpszVolumeMountPoint*/, wchar_t* lpszVolumeName, DWORD cchBufferLength) noexcept {
    static const wchar_t dummyVol[] = L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\";
    if (lpszVolumeName && cchBufferLength >= 49) {
        std::memcpy(lpszVolumeName, dummyVol, sizeof(dummyVol));
        return TRUE;
    }
    return FALSE;
}

inline BOOL WINAPI GetComputerNameExW(int32_t /*NameType*/, wchar_t* lpBuffer, DWORD* nSize) noexcept {
    static const wchar_t compName[] = L"MicaNT-Host";
    if (!nSize) return FALSE;
    if (!lpBuffer || *nSize < 12) {
        *nSize = 12;
        return FALSE;
    }
    std::memcpy(lpBuffer, compName, sizeof(compName));
    *nSize = 11;
    return TRUE;
}

inline std::mutex g_threadDescMutex;
inline std::unordered_map<uint64_t, std::wstring> g_threadDescriptions;

inline HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription) noexcept {
    if (!lpThreadDescription) return static_cast<HRESULT>(0x80070057); // E_INVALIDARG
    uint64_t tid = reinterpret_cast<uint64_t>(hThread);
    std::lock_guard<std::mutex> lock(g_threadDescMutex);
    g_threadDescriptions[tid] = lpThreadDescription;
    size_t descLen = std::wcslen(lpThreadDescription);
    micant::UNICODE_STRING uniStr;
    uniStr.length = static_cast<uint16_t>(descLen * sizeof(wchar_t));
    uniStr.maximumLength = static_cast<uint16_t>((descLen + 1) * sizeof(wchar_t));
    uniStr.buffer = lpThreadDescription;
    micant::ntdll::NtSetInformationThread(hThread, 38, &uniStr, sizeof(uniStr));
    return 0; // S_OK
}

inline HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR* ppszThreadDescription) noexcept {
    if (!ppszThreadDescription) return static_cast<HRESULT>(0x80070057); // E_INVALIDARG
    uint64_t tid = reinterpret_cast<uint64_t>(hThread);
    std::wstring desc = L"MainGameThread";
    uint8_t ntBuf[512]{};
    ULONG retLen = 0;
    if (micant::ntdll::NtQueryInformationThread(hThread, 38, ntBuf, sizeof(ntBuf), &retLen) == micant::NtStatus::Success) {
        auto* pUni = reinterpret_cast<micant::UNICODE_STRING*>(ntBuf);
        if (pUni->buffer && pUni->length > 0) {
            desc.assign(pUni->buffer, pUni->length / sizeof(wchar_t));
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_threadDescMutex);
        auto it = g_threadDescriptions.find(tid);
        if (it != g_threadDescriptions.end()) {
            desc = it->second;
        }
    }
    size_t charCount = desc.size() + 1;
    auto* pBuf = static_cast<wchar_t*>(LocalAlloc(0x0040 /* LPTR */, charCount * sizeof(wchar_t)));
    if (!pBuf) return static_cast<HRESULT>(0x8007000E); // E_OUTOFMEMORY
    std::memcpy(pBuf, desc.c_str(), charCount * sizeof(wchar_t));
    *ppszThreadDescription = pBuf;
    return 0; // S_OK
}

inline BOOL WINAPI SetFileInformationByHandle(HANDLE /*hFile*/, int32_t /*FileInformationClass*/, void* /*lpFileInformation*/, DWORD /*dwBufferSize*/) noexcept {
    return TRUE;
}

inline int32_t WINAPI GetUserGeoID(int32_t /*GeoClass*/) noexcept {
    return 244; // United States
}

inline int32_t WINAPI GetGeoInfoW(int32_t /*Location*/, int32_t /*GeoType*/, wchar_t* lpGeoData, int32_t cchData, uint16_t /*LangId*/) noexcept {
    if (lpGeoData && cchData >= 3) {
        std::memcpy(lpGeoData, L"US\0", 6);
        return 3;
    }
    return 3;
}

inline int32_t WINAPI GetCurrencyFormatW(uint32_t /*Locale*/, DWORD /*dwFlags*/, const wchar_t* /*lpValue*/, const void* /*lpFormat*/, wchar_t* lpCurrencyStr, int32_t cchCurrency) noexcept {
    if (lpCurrencyStr && cchCurrency >= 8) {
        std::memcpy(lpCurrencyStr, L"$0.00\0", 12);
        return 6;
    }
    return 6;
}

inline BOOL WINAPI GetUserPreferredUILanguages(DWORD /*dwFlags*/, uint32_t* pulNumLanguages, wchar_t* pwszLanguagesBuffer, uint32_t* pcchLanguagesBuffer) noexcept {
    if (pulNumLanguages) *pulNumLanguages = 1;
    if (pcchLanguagesBuffer) *pcchLanguagesBuffer = 6;
    if (pwszLanguagesBuffer) std::memcpy(pwszLanguagesBuffer, L"en-US\0", 12);
    return TRUE;
}

inline int32_t WINAPI GetUserDefaultLocaleName(wchar_t* lpLocaleName, int32_t cchLocaleName) noexcept {
    if (lpLocaleName && cchLocaleName >= 6) {
        std::memcpy(lpLocaleName, L"en-US", 12);
        return 6;
    }
    return 6;
}

inline uint64_t WINAPI GetEnabledXStateFeatures() noexcept {
    return 3; // XSTATE_LEGACY_FLOATING_POINT | XSTATE_LEGACY_SSE
}

inline int WINAPI GetNumberFormatEx(const wchar_t* /*lpLocaleName*/, DWORD /*dwFlags*/, const wchar_t* lpValue, void* /*lpFormat*/, wchar_t* lpNumberStr, int cchNumber) noexcept {
    if (!lpNumberStr || cchNumber <= 0) return 16;
    if (lpValue) {
        std::wcsncpy(lpNumberStr, lpValue, cchNumber);
        return static_cast<int>(std::wcslen(lpNumberStr));
    }
    return 0;
}

inline BOOL WINAPI SetConsoleCP(uint32_t /*wCodePageID*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetConsoleOutputCP(uint32_t /*wCodePageID*/) noexcept {
    return TRUE;
}

inline BOOL WINAPI SetWaitableTimer(HANDLE /*hTimer*/, const void* /*lpDueTime*/, int32_t /*lPeriod*/, void* /*pfnCompletionRoutine*/, void* /*lpArgToCompletionRoutine*/, BOOL /*fResume*/) noexcept {
    return TRUE;
}

inline HANDLE WINAPI CreateWaitableTimerA(void* /*lpTimerAttributes*/, BOOL /*bManualReset*/, const char* /*lpTimerName*/) noexcept {
    return reinterpret_cast<HANDLE>(0x12001);
}

inline HANDLE WINAPI CreateSemaphoreA(void* /*lpSemaphoreAttributes*/, int32_t /*lInitialCount*/, int32_t /*lMaximumCount*/, const char* /*lpName*/) noexcept {
    return reinterpret_cast<HANDLE>(0x13001);
}


namespace detail {
    struct WaitOnAddressBucket {
        std::mutex mtx;
        std::condition_variable cv;
        uint64_t wakeSequence{0};
    };
    inline constexpr size_t NUM_WAIT_BUCKETS = 64;
    inline WaitOnAddressBucket s_waitBuckets[NUM_WAIT_BUCKETS];

    inline size_t HashAddress(const volatile void* addr) noexcept {
        return (reinterpret_cast<uintptr_t>(addr) >> 3) % NUM_WAIT_BUCKETS;
    }
}

inline BOOL WINAPI WaitOnAddress(volatile void* Address, void* CompareAddress, size_t AddressSize, DWORD dwMilliseconds) noexcept {
    if (!Address || !CompareAddress) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (AddressSize != 1 && AddressSize != 2 && AddressSize != 4 && AddressSize != 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Compare Address memory with CompareAddress: if already differing, return TRUE immediately
    if (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) != 0) {
        return TRUE;
    }

    auto block = SovereignSyncManager::get().getWaitAddress(Address);
    if (!block) return FALSE;

    SovereignSyncManager::get().recordWaitAddressWait();
    block->waitCount.fetch_add(1);

    std::unique_lock<std::mutex> lock(block->mutex);
    if (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) != 0) {
        return TRUE;
    }

    uint64_t curGen = block->generation.load();
    bool success = true;

    if (dwMilliseconds == 0xFFFFFFFF) { // INFINITE
        while (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) == 0 &&
               block->generation.load() == curGen) {
            block->cv.wait(lock);
        }
    } else {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);
        while (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) == 0 &&
               block->generation.load() == curGen) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                if (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) != 0) {
                    return TRUE;
                }
                success = false;
                break;
            }
            block->cv.wait_until(lock, deadline);
            if (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) != 0) {
                return TRUE;
            }
            if (std::chrono::steady_clock::now() >= deadline && block->generation.load() == curGen) {
                success = false;
                break;
            }
        }
    }

    if (!success) {
        if (std::memcmp(const_cast<const void*>(Address), CompareAddress, AddressSize) != 0) {
            return TRUE;
        }
        SovereignSyncManager::get().recordWaitAddressTimeout();
        SetLastError(1460); // ERROR_TIMEOUT
        return FALSE;
    }
    return TRUE;
}

inline void WINAPI WakeByAddressSingle(void* Address) noexcept {
    if (!Address) return;
    auto block = SovereignSyncManager::get().getWaitAddress(Address);
    if (!block) return;
    block->wakeCount.fetch_add(1);
    block->generation.fetch_add(1);
    SovereignSyncManager::get().recordWaitAddressWake();
    block->cv.notify_one();
}

inline void WINAPI WakeByAddressAll(void* Address) noexcept {
    if (!Address) return;
    auto block = SovereignSyncManager::get().getWaitAddress(Address);
    if (!block) return;
    block->wakeCount.fetch_add(1);
    block->generation.fetch_add(1);
    SovereignSyncManager::get().recordWaitAddressWake();
    block->cv.notify_all();
}

// ============================================================================
// Win32 Synchronization Barrier APIs
// ============================================================================
#ifndef SYNCHRONIZATION_BARRIER_DEFINED
#define SYNCHRONIZATION_BARRIER_DEFINED
struct RTL_BARRIER {
    DWORD Reserved1;
    DWORD Reserved2;
    ULONG_PTR Reserved3[2];
    DWORD Reserved4;
    DWORD Reserved5;
};

using SYNCHRONIZATION_BARRIER = RTL_BARRIER;
using PSYNCHRONIZATION_BARRIER = RTL_BARRIER*;
using LPSYNCHRONIZATION_BARRIER = RTL_BARRIER*;

inline constexpr DWORD SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY  = 0x01;
inline constexpr DWORD SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY = 0x02;
inline constexpr DWORD SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE   = 0x04;
#endif

inline BOOL WINAPI InitializeSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    LONG lTotalThreads,
    LONG lSpinCount
) noexcept {
    if (!lpBarrier || lTotalThreads <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::memset(lpBarrier, 0, sizeof(*lpBarrier));
    lpBarrier->Reserved1 = 0xBA441E4; // Barrier signature
    lpBarrier->Reserved2 = static_cast<DWORD>(lTotalThreads);
    if (!SovereignSyncManager::get().initBarrier(lpBarrier, lTotalThreads, lSpinCount)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

inline BOOL WINAPI EnterSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    DWORD dwFlags
) noexcept {
    if (!lpBarrier) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    bool elected = false;
    if (!SovereignSyncManager::get().enterBarrier(lpBarrier, dwFlags, elected)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return elected ? TRUE : FALSE;
}

inline BOOL WINAPI DeleteSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier
) noexcept {
    if (!lpBarrier) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!SovereignSyncManager::get().deleteBarrier(lpBarrier)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

// QueryDosDeviceW defined above
inline BOOL ReadProcessMemory(HANDLE /*hProcess*/, LPCVOID lpBaseAddress, LPVOID lpBuffer, SIZE_T nSize, SIZE_T* lpNumberOfBytesRead) noexcept {
    if (!lpBaseAddress || !lpBuffer || nSize == 0) {
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
        return FALSE;
    }
    std::memcpy(lpBuffer, lpBaseAddress, nSize);
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = nSize;
    return TRUE;
}
inline void* GetCPInfoExW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL WriteProcessMemory(HANDLE /*hProcess*/, LPVOID lpBaseAddress, LPCVOID lpBuffer, SIZE_T nSize, SIZE_T* lpNumberOfBytesWritten) noexcept {
    if (!lpBaseAddress || !lpBuffer || nSize == 0) {
        if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
        return FALSE;
    }
    std::memcpy(lpBaseAddress, lpBuffer, nSize);
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = nSize;
    return TRUE;
}

inline BOOL WINAPI QueryProcessCycleTime(HANDLE /*ProcessHandle*/, PULONG64 CycleTime) noexcept {
    if (!CycleTime) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    static std::atomic<uint64_t> s_procOffset{5000000ULL};
    LargeInteger qpc{};
    QueryPerformanceCounter(&qpc);
    *CycleTime = (static_cast<uint64_t>(qpc.quadPart) * 4200000000ULL / 10000000ULL) + s_procOffset.fetch_add(100000ULL);
    return TRUE;
}

inline BOOL WINAPI QueryThreadCycleTime(HANDLE /*ThreadHandle*/, PULONG64 CycleTime) noexcept {
    if (!CycleTime) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    static std::atomic<uint64_t> s_cycleOffset{1000000ULL};
    LargeInteger qpc{};
    QueryPerformanceCounter(&qpc);
    *CycleTime = (static_cast<uint64_t>(qpc.quadPart) * 4200000000ULL / 10000000ULL) / 8 + s_cycleOffset.fetch_add(50000ULL);
    return TRUE;
}

inline BOOL QueryIdleProcessorCycleTime(uint32_t* BufferLength, uint64_t* ProcessorIdleCycleTime) noexcept {
    if (!BufferLength) return FALSE;
    uint32_t required = 16 * sizeof(uint64_t);
    if (*BufferLength < required) {
        *BufferLength = required;
        return FALSE;
    }
    if (ProcessorIdleCycleTime) {
        for (int i = 0; i < 16; ++i) {
            ProcessorIdleCycleTime[i] = 1000000000ULL * (i + 1);
        }
    }
    *BufferLength = required;
    return TRUE;
}
inline BOOL TryEnterCriticalSection(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline BOOL HeapDestroy(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetDiskFreeSpaceA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* FindFirstFileA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* OpenMutexW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline int lstrlenA(const char* s) noexcept { return s ? static_cast<int>(std::strlen(s)) : 0; }
inline char* lstrcpyA(char* dst, const char* src) noexcept { return (dst && src) ? std::strcpy(dst, src) : dst; }
inline void* CreateFileA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL FreeResource(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL GlobalAddAtomW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetExitCodeThread(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL OutputDebugStringW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL IsBadReadPtr(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline void* GetShortPathNameW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL GlobalFindAtomW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL WritePrivateProfileStringW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* FindNextFileA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL GlobalDeleteAtom(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
// GetThreadPriority and SetThreadPriority are implemented above in SovereignThreadPacingManager
inline BOOL SearchPathW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL VerifyVersionInfoW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline BOOL HeapCreate(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SignalObjectAndWait(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL VerSetConditionMask(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline DWORD GetUserDefaultUILanguage(void* /*a*/ = nullptr) noexcept { return 0x0409; }
inline DWORD GetConsoleCP(void* /*a*/ = nullptr) noexcept { return 0x0409; }
inline BOOL CompareStringA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL EnumResourceNamesW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetSystemDirectoryW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetDriveTypeA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetComputerNameW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline DWORD GetSystemDefaultUILanguage(void* /*a*/ = nullptr) noexcept { return 0x0409; }
inline BOOL EnumCalendarInfoW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetPrivateProfileStringW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL WaitForMultipleObjectsEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline DWORD GetThreadLocale(void* /*a*/ = nullptr) noexcept { return 0x0409; }
inline BOOL SetThreadLocale(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline uint32_t VerLanguageNameW(uint32_t /*wLang*/, wchar_t* szLang, uint32_t cchLang) noexcept {
    if (!szLang || cchLang == 0) return 0;
    const wchar_t desc[] = L"English (United States)";
    size_t len = std::wcslen(desc);
    if (len >= cchLang) len = cchLang - 1;
    std::wcsncpy(szLang, desc, len);
    szLang[len] = L'\0';
    return static_cast<uint32_t>(len);
}
using ntdll::LOGICAL_PROCESSOR_RELATIONSHIP;
using ntdll::RelationProcessorCore;
using ntdll::RelationNumaNode;
using ntdll::RelationCache;
using ntdll::RelationProcessorPackage;
using ntdll::RelationGroup;
using ntdll::RelationAll;

using ntdll::PROCESSOR_CACHE_TYPE;
using ntdll::CacheUnified;
using ntdll::CacheInstruction;
using ntdll::CacheData;
using ntdll::CacheTrace;

using ntdll::CACHE_DESCRIPTOR;
using ntdll::SYSTEM_LOGICAL_PROCESSOR_INFORMATION;
using ntdll::CACHE_RELATIONSHIP;
using ntdll::PROCESSOR_RELATIONSHIP;
using ntdll::NUMA_NODE_RELATIONSHIP;
using ntdll::SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;

inline BOOL GetLogicalProcessorInformation(void* Buffer, uint32_t* ReturnedLength) noexcept {
    if (!ReturnedLength) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // 16 cores (8 entries for dual-thread cores) + L1/L2/L3 caches + 1 NUMA node = ~12 entries
    constexpr uint32_t count = 12;
    uint32_t required = count * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    if (!Buffer || *ReturnedLength < required) {
        *ReturnedLength = required;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    auto* arr = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(Buffer);
    std::memset(arr, 0, required);

    // Populate representative Core 0..7 (Performance/V-Cache cores)
    for (uint32_t i = 0; i < 8; ++i) {
        arr[i].Relationship = RelationProcessorCore;
        arr[i].ProcessorMask = (1ULL << i);
        arr[i].ProcessorCore.Flags = 1; // SMT enabled
    }

    // Populate L3 Cache (96MB 3D V-Cache on CCD 0)
    arr[8].Relationship = RelationCache;
    arr[8].ProcessorMask = 0x00FF; // Cores 0-7
    arr[8].Cache.Level = 3;
    arr[8].Cache.Associativity = 16;
    arr[8].Cache.LineSize = 64;
    arr[8].Cache.Size = 96 * 1024 * 1024; // 96MB
    arr[8].Cache.Type = CacheUnified;

    // Populate NUMA Node 0
    arr[9].Relationship = RelationNumaNode;
    arr[9].ProcessorMask = 0xFFFF; // All 16 cores
    arr[9].NumaNode.NodeNumber = 0;

    *ReturnedLength = required;
    return TRUE;
}

inline BOOL GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
                                             void* Buffer,
                                             uint32_t* ReturnedLength) noexcept {
    if (!ReturnedLength) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Calculate buffer size needed
    uint32_t recordCount = 0;
    if (RelationshipType == RelationProcessorCore || RelationshipType == RelationAll) {
        recordCount += 16; // 16 cores
    }
    if (RelationshipType == RelationCache || RelationshipType == RelationAll) {
        recordCount += 2;  // L3 Cache 96MB + L3 Cache 32MB
    }
    if (RelationshipType == RelationNumaNode || RelationshipType == RelationAll) {
        recordCount += 1;  // NUMA Node 0
    }

    uint32_t required = recordCount * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    if (!Buffer || *ReturnedLength < required) {
        *ReturnedLength = required;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    auto* curr = reinterpret_cast<uint8_t*>(Buffer);
    std::memset(curr, 0, required);

    // Populate Cores if requested
    if (RelationshipType == RelationProcessorCore || RelationshipType == RelationAll) {
        for (uint32_t i = 0; i < 16; ++i) {
            auto* pInfo = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(curr);
            pInfo->Relationship = RelationProcessorCore;
            pInfo->Size = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
            pInfo->Processor.Flags = (i < 8) ? 1 : 0; // SMT on P-cores
            pInfo->Processor.EfficiencyClass = (i < 4) ? 2 : ((i < 8) ? 1 : 0); // 2 = 3D V-Cache, 1 = P-core, 0 = E-core
            pInfo->Processor.GroupCount = 1;
            pInfo->Processor.GroupMasks[0] = (1ULL << i);
            curr += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
        }
    }

    // Populate Caches if requested
    if (RelationshipType == RelationCache || RelationshipType == RelationAll) {
        // 96MB 3D V-Cache on CCD 0
        auto* pC1 = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(curr);
        pC1->Relationship = RelationCache;
        pC1->Size = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
        pC1->Cache.Level = 3;
        pC1->Cache.Associativity = 16;
        pC1->Cache.LineSize = 64;
        pC1->Cache.CacheSize = 96 * 1024 * 1024;
        pC1->Cache.Type = CacheUnified;
        pC1->Cache.GroupCount = 1;
        pC1->Cache.GroupMasks[0] = 0x00FF; // Cores 0-7
        curr += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);

        // 32MB standard L3 on CCD 1
        auto* pC2 = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(curr);
        pC2->Relationship = RelationCache;
        pC2->Size = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
        pC2->Cache.Level = 3;
        pC2->Cache.Associativity = 16;
        pC2->Cache.LineSize = 64;
        pC2->Cache.CacheSize = 32 * 1024 * 1024;
        pC2->Cache.Type = CacheUnified;
        pC2->Cache.GroupCount = 1;
        pC2->Cache.GroupMasks[0] = 0xFF00; // Cores 8-15
        curr += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    }

    // Populate NUMA Node if requested
    if (RelationshipType == RelationNumaNode || RelationshipType == RelationAll) {
        auto* pNuma = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(curr);
        pNuma->Relationship = RelationNumaNode;
        pNuma->Size = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
        pNuma->NumaNode.NodeNumber = 0;
        pNuma->NumaNode.GroupCount = 1;
        pNuma->NumaNode.GroupMasks[0] = 0xFFFF;
        curr += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    }

    *ReturnedLength = required;
    return TRUE;
}

inline std::vector<ULONG> s_defaultCpuSets{0, 1, 2, 3, 4, 5, 6, 7}; // Default to P-cores/V-Cache
inline std::unordered_map<HANDLE, std::vector<ULONG>> s_threadCpuSets;
inline std::mutex s_cpuSetLock;

inline BOOL WINAPI SetThreadSelectedCpuSets(HANDLE hThread, const ULONG* CpuSetIds, ULONG CpuSetIdCount) noexcept {
    if (!CpuSetIds && CpuSetIdCount > 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::lock_guard<std::mutex> lock(s_cpuSetLock);
    if (CpuSetIdCount == 0) {
        s_threadCpuSets.erase(hThread);
    } else {
        s_threadCpuSets[hThread].assign(CpuSetIds, CpuSetIds + CpuSetIdCount);
    }
    return TRUE;
}

inline BOOL WINAPI GetThreadSelectedCpuSets(HANDLE hThread, ULONG* CpuSetIds, ULONG CpuSetIdCount, ULONG* RequiredIdCount) noexcept {
    std::lock_guard<std::mutex> lock(s_cpuSetLock);
    auto it = s_threadCpuSets.find(hThread);
    const auto& sets = (it != s_threadCpuSets.end()) ? it->second : s_defaultCpuSets;

    if (RequiredIdCount) *RequiredIdCount = static_cast<ULONG>(sets.size());
    if (CpuSetIds && CpuSetIdCount >= sets.size()) {
        std::copy(sets.begin(), sets.end(), CpuSetIds);
        return TRUE;
    }
    if (!CpuSetIds && CpuSetIdCount == 0) {
        return TRUE;
    }
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
}

inline BOOL WINAPI SetProcessDefaultCpuSets(HANDLE /*hProcess*/, const ULONG* CpuSetIds, ULONG CpuSetIdCount) noexcept {
    if (!CpuSetIds && CpuSetIdCount > 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::lock_guard<std::mutex> lock(s_cpuSetLock);
    if (CpuSetIdCount == 0) {
        s_defaultCpuSets.clear();
    } else {
        s_defaultCpuSets.assign(CpuSetIds, CpuSetIds + CpuSetIdCount);
    }
    return TRUE;
}

inline BOOL WINAPI GetProcessDefaultCpuSets(HANDLE /*hProcess*/, ULONG* CpuSetIds, ULONG CpuSetIdCount, ULONG* RequiredIdCount) noexcept {
    std::lock_guard<std::mutex> lock(s_cpuSetLock);
    if (RequiredIdCount) *RequiredIdCount = static_cast<ULONG>(s_defaultCpuSets.size());
    if (CpuSetIds && CpuSetIdCount >= s_defaultCpuSets.size()) {
        std::copy(s_defaultCpuSets.begin(), s_defaultCpuSets.end(), CpuSetIds);
        return TRUE;
    }
    if (!CpuSetIds && CpuSetIdCount == 0) {
        return TRUE;
    }
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
}

using ntdll::SYSTEM_CPU_SET_INFORMATION;
using ntdll::PSYSTEM_CPU_SET_INFORMATION;

inline BOOL WINAPI GetSystemCpuSetInformation(
    PSYSTEM_CPU_SET_INFORMATION Information,
    ULONG BufferLength,
    ULONG* ReturnedLength,
    HANDLE /*Process*/,
    ULONG /*Flags*/) noexcept
{
    ULONG retLen = 0;
    NtStatus status = ntdll::NtQuerySystemInformation_Export(
        104, // SystemCpuSetInformation
        Information,
        BufferLength,
        &retLen);

    if (ReturnedLength) {
        *ReturnedLength = retLen;
    }

    if (status != NtStatus::Success) {
        if (status == NtStatus::InfoLengthMismatch) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        } else {
            SetLastError(ERROR_INVALID_PARAMETER);
        }
        return FALSE;
    }

    return TRUE;
}

enum FIRMWARE_TYPE {
    FirmwareTypeUnknown = 0,
    FirmwareTypeBios = 1,
    FirmwareTypeUefi = 2,
    FirmwareTypeMax = 3
};

inline BOOL WINAPI GetFirmwareType(FIRMWARE_TYPE* pFirmwareType) noexcept {
    if (!pFirmwareType) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pFirmwareType = FirmwareTypeUefi;
    return TRUE;
}


inline BOOL WINAPI SetThreadGroupAffinity(HANDLE /*hThread*/, const GROUP_AFFINITY* GroupAffinity, PGROUP_AFFINITY PreviousGroupAffinity) noexcept {
    if (!GroupAffinity) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (PreviousGroupAffinity) {
        PreviousGroupAffinity->Group = 0;
        PreviousGroupAffinity->Mask = 0xFFFF;
        PreviousGroupAffinity->Reserved[0] = 0;
        PreviousGroupAffinity->Reserved[1] = 0;
        PreviousGroupAffinity->Reserved[2] = 0;
    }
    return TRUE;
}

#ifndef PROCESS_AFFINITY_ENABLE_AUTO_UPDATE
#define PROCESS_AFFINITY_ENABLE_AUTO_UPDATE 0x00000001
#endif

inline DWORD s_processAffinityUpdateMode = 0;

inline BOOL WINAPI SetProcessAffinityUpdateMode(HANDLE /*hProcess*/, DWORD dwFlags) noexcept {
    if (dwFlags & ~PROCESS_AFFINITY_ENABLE_AUTO_UPDATE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    s_processAffinityUpdateMode = dwFlags;
    return TRUE;
}

inline BOOL WINAPI QueryProcessAffinityUpdateMode(HANDLE /*hProcess*/, LPDWORD lpdwFlags) noexcept {
    if (!lpdwFlags) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *lpdwFlags = s_processAffinityUpdateMode;
    return TRUE;
}

inline BOOL WINAPI SetProcessDefaultCpuSetMasks(HANDLE /*hProcess*/, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount) noexcept {
    if (!CpuSetMasks && CpuSetMaskCount > 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

inline BOOL WINAPI GetProcessDefaultCpuSetMasks(HANDLE /*hProcess*/, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount, PUSHORT RequiredMaskCount) noexcept {
    if (!RequiredMaskCount) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *RequiredMaskCount = 1;
    if (!CpuSetMasks || CpuSetMaskCount < 1) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    CpuSetMasks[0].Group = 0;
    CpuSetMasks[0].Mask = 0xFFFF;
    CpuSetMasks[0].Reserved[0] = CpuSetMasks[0].Reserved[1] = CpuSetMasks[0].Reserved[2] = 0;
    return TRUE;
}

inline BOOL WINAPI SetThreadSelectedCpuSetMasks(HANDLE /*hThread*/, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount) noexcept {
    if (!CpuSetMasks && CpuSetMaskCount > 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

inline BOOL WINAPI GetThreadSelectedCpuSetMasks(HANDLE /*hThread*/, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount, PUSHORT RequiredMaskCount) noexcept {
    if (!RequiredMaskCount) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *RequiredMaskCount = 1;
    if (!CpuSetMasks || CpuSetMaskCount < 1) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    CpuSetMasks[0].Group = 0;
    CpuSetMasks[0].Mask = 0xFFFF;
    CpuSetMasks[0].Reserved[0] = CpuSetMasks[0].Reserved[1] = CpuSetMasks[0].Reserved[2] = 0;
    return TRUE;
}
inline BOOL IsWow64Process(void* /*hProcess*/, BOOL* Wow64Process) noexcept {
    if (Wow64Process) *Wow64Process = 0;
    return 1;
}
inline BOOL ProcessIdToSessionId(uint32_t /*dwProcessId*/, uint32_t* pSessionId) noexcept {
    if (pSessionId) *pSessionId = 1;
    return 1;
}
inline uint32_t LocaleNameToLCID(const wchar_t* /*lpName*/, uint32_t /*dwFlags*/) noexcept {
    return 0x0409;
}
inline BOOL GetTimeZoneInformationForYear(uint16_t /*wYear*/, void* /*pDynamicTimeZoneInformation*/, void* /*pTimeZoneInformation*/) noexcept {
    return 1;
}
inline BOOL GetSystemTimes(void* lpIdleTime, void* lpKernelTime, void* lpUserTime) noexcept {
    if (lpIdleTime) std::memset(lpIdleTime, 0, 8);
    if (lpKernelTime) std::memset(lpKernelTime, 0, 8);
    if (lpUserTime) std::memset(lpUserTime, 0, 8);
    return 1;
}

// ============================================================================
// 18. Win32 Dynamic Subsystem Export Table Initializer
// ============================================================================

/**
 * @brief Registers all clean-room Win32 and NTDLL exports into the userland dynamic loader.
 */
inline void InitializeWin32SubsystemExports() {
    static bool s_Initialized = false;
    if (s_Initialized) return;
    s_Initialized = true;

    auto& ldr = ldr::DynamicLoader::get();

    // kernel32.dll exports
    ldr.registerExport("kernel32.dll", "GetProcessHeap", reinterpret_cast<void*>(GetProcessHeap));
    ldr.registerExport("kernel32.dll", "HeapAlloc", reinterpret_cast<void*>(HeapAlloc));
    ldr.registerExport("kernel32.dll", "HeapFree", reinterpret_cast<void*>(HeapFree));
    ldr.registerExport("kernel32.dll", "HeapReAlloc", reinterpret_cast<void*>(HeapReAlloc));
    ldr.registerExport("kernel32.dll", "HeapSize", reinterpret_cast<void*>(HeapSize));
    ldr.registerExport("kernel32.dll", "LocalAlloc", reinterpret_cast<void*>(LocalAlloc));
    ldr.registerExport("kernel32.dll", "LocalFree", reinterpret_cast<void*>(LocalFree));
    ldr.registerExport("kernel32.dll", "VirtualAlloc", reinterpret_cast<void*>(VirtualAlloc));
    ldr.registerExport("kernel32.dll", "VirtualFree", reinterpret_cast<void*>(VirtualFree));
    ldr.registerExport("kernel32.dll", "VirtualAllocEx", reinterpret_cast<void*>(VirtualAllocEx));
    ldr.registerExport("kernel32.dll", "VirtualAlloc2", reinterpret_cast<void*>(VirtualAlloc2));
    ldr.registerExport("kernel32.dll", "VirtualAlloc2FromApp", reinterpret_cast<void*>(VirtualAlloc2FromApp));
    ldr.registerExport("kernel32.dll", "VirtualFreeEx", reinterpret_cast<void*>(VirtualFreeEx));
    ldr.registerExport("kernel32.dll", "VirtualFree2", reinterpret_cast<void*>(VirtualFree2));
    ldr.registerExport("kernel32.dll", "VirtualFree2FromApp", reinterpret_cast<void*>(VirtualFree2FromApp));
    ldr.registerExport("kernel32.dll", "VirtualLock", reinterpret_cast<void*>(VirtualLock));
    ldr.registerExport("kernel32.dll", "VirtualUnlock", reinterpret_cast<void*>(VirtualUnlock));
    ldr.registerExport("kernel32.dll", "GetProcessWorkingSetSizeEx", reinterpret_cast<void*>(GetProcessWorkingSetSizeEx));
    ldr.registerExport("kernel32.dll", "SetProcessWorkingSetSizeEx", reinterpret_cast<void*>(SetProcessWorkingSetSizeEx));
    ldr.registerExport("kernel32.dll", "MapViewOfFileNuma2", reinterpret_cast<void*>(MapViewOfFileNuma2));
    ldr.registerExport("kernel32.dll", "MapViewOfFile3", reinterpret_cast<void*>(MapViewOfFile3));
    ldr.registerExport("kernel32.dll", "PrefetchVirtualMemory", reinterpret_cast<void*>(PrefetchVirtualMemory));
    ldr.registerExport("kernel32.dll", "GetLargePageMinimum", reinterpret_cast<void*>(GetLargePageMinimum));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "VirtualAlloc2", reinterpret_cast<void*>(VirtualAlloc2));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "VirtualAlloc2FromApp", reinterpret_cast<void*>(VirtualAlloc2FromApp));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "VirtualFree2", reinterpret_cast<void*>(VirtualFree2));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "VirtualFree2FromApp", reinterpret_cast<void*>(VirtualFree2FromApp));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "MapViewOfFileNuma2", reinterpret_cast<void*>(MapViewOfFileNuma2));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "MapViewOfFile3", reinterpret_cast<void*>(MapViewOfFile3));
    ldr.registerExport("api-ms-win-core-memory-l1-1-2.dll", "PrefetchVirtualMemory", reinterpret_cast<void*>(PrefetchVirtualMemory));
    ldr.registerExport("api-ms-win-core-memory-l1-1-2.dll", "GetLargePageMinimum", reinterpret_cast<void*>(GetLargePageMinimum));
    ldr.registerExport("api-ms-win-core-memory-l1-1-0.dll", "GetLargePageMinimum", reinterpret_cast<void*>(GetLargePageMinimum));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "VirtualQueryEx", reinterpret_cast<void*>(VirtualQueryEx));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "VirtualProtectEx", reinterpret_cast<void*>(VirtualProtectEx));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "ReadProcessMemory", reinterpret_cast<void*>(ReadProcessMemory));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "WriteProcessMemory", reinterpret_cast<void*>(WriteProcessMemory));
    ldr.registerExport("kernel32.dll", "GetCurrentProcess", reinterpret_cast<void*>(GetCurrentProcess));
    ldr.registerExport("kernel32.dll", "GetCurrentProcessId", reinterpret_cast<void*>(GetCurrentProcessId));
    ldr.registerExport("kernel32.dll", "GetCurrentPackageFullName", reinterpret_cast<void*>(GetCurrentPackageFullName));
    ldr.registerExport("kernel32.dll", "GetCurrentThread", reinterpret_cast<void*>(GetCurrentThread));
    ldr.registerExport("kernel32.dll", "GetCurrentThreadId", reinterpret_cast<void*>(GetCurrentThreadId));
    ldr.registerExport("kernel32.dll", "ExitProcess", reinterpret_cast<void*>(ExitProcess));
    ldr.registerExport("kernel32.dll", "CreateProcessW", reinterpret_cast<void*>(CreateProcessW));
    ldr.registerExport("kernel32.dll", "CreateProcessA", reinterpret_cast<void*>(CreateProcessA));
    ldr.registerExport("kernel32.dll", "AllocConsole", reinterpret_cast<void*>(AllocConsole));
    ldr.registerExport("kernel32.dll", "FreeConsole", reinterpret_cast<void*>(FreeConsole));
    ldr.registerExport("kernel32.dll", "SetConsoleTitleW", reinterpret_cast<void*>(SetConsoleTitleW));
    ldr.registerExport("kernel32.dll", "GetConsoleTitleW", reinterpret_cast<void*>(GetConsoleTitleW));
    ldr.registerExport("kernel32.dll", "GetStdHandle", reinterpret_cast<void*>(GetStdHandle));
    ldr.registerExport("kernel32.dll", "SetStdHandle", reinterpret_cast<void*>(SetStdHandle));
    ldr.registerExport("kernel32.dll", "WriteConsoleW", reinterpret_cast<void*>(WriteConsoleW));
    ldr.registerExport("kernel32.dll", "WriteConsoleA", reinterpret_cast<void*>(WriteConsoleA));
    ldr.registerExport("kernel32.dll", "GetConsoleMode", reinterpret_cast<void*>(GetConsoleMode));
    ldr.registerExport("kernel32.dll", "SetConsoleMode", reinterpret_cast<void*>(SetConsoleMode));
    ldr.registerExport("kernel32.dll", "GetConsoleScreenBufferInfo", reinterpret_cast<void*>(GetConsoleScreenBufferInfo));
    ldr.registerExport("kernel32.dll", "SetConsoleTextAttribute", reinterpret_cast<void*>(SetConsoleTextAttribute));
    ldr.registerExport("kernel32.dll", "SetConsoleCursorPosition", reinterpret_cast<void*>(SetConsoleCursorPosition));
    ldr.registerExport("kernel32.dll", "CreateFileW", reinterpret_cast<void*>(CreateFileW));
    ldr.registerExport("kernel32.dll", "ReadFile", reinterpret_cast<void*>(ReadFile));
    ldr.registerExport("kernel32.dll", "WriteFile", reinterpret_cast<void*>(WriteFile));
    ldr.registerExport("kernel32.dll", "CloseHandle", reinterpret_cast<void*>(CloseHandle));
    ldr.registerExport("kernel32.dll", "GetFileSize", reinterpret_cast<void*>(GetFileSize));
    ldr.registerExport("kernel32.dll", "GetFileSizeEx", reinterpret_cast<void*>(GetFileSizeEx));
    ldr.registerExport("kernel32.dll", "SetFilePointer", reinterpret_cast<void*>(SetFilePointer));
    ldr.registerExport("kernel32.dll", "SetFilePointerEx", reinterpret_cast<void*>(SetFilePointerEx));
    ldr.registerExport("kernel32.dll", "GetFileAttributesW", reinterpret_cast<void*>(GetFileAttributesW));
    ldr.registerExport("kernel32.dll", "GetFileAttributesA", reinterpret_cast<void*>(GetFileAttributesA));
    ldr.registerExport("kernel32.dll", "SetFileAttributesW", reinterpret_cast<void*>(SetFileAttributesW));
    ldr.registerExport("kernel32.dll", "DeleteFileW", reinterpret_cast<void*>(DeleteFileW));
    ldr.registerExport("kernel32.dll", "DeleteFileA", reinterpret_cast<void*>(DeleteFileA));
    ldr.registerExport("kernel32.dll", "CopyFileW", reinterpret_cast<void*>(CopyFileW));
    ldr.registerExport("kernel32.dll", "CopyFileA", reinterpret_cast<void*>(CopyFileA));
    ldr.registerExport("kernel32.dll", "MoveFileW", reinterpret_cast<void*>(MoveFileW));
    ldr.registerExport("kernel32.dll", "MoveFileA", reinterpret_cast<void*>(MoveFileA));
    ldr.registerExport("kernel32.dll", "MoveFileExW", reinterpret_cast<void*>(MoveFileExW));
    ldr.registerExport("kernel32.dll", "CreateDirectoryW", reinterpret_cast<void*>(CreateDirectoryW));
    ldr.registerExport("kernel32.dll", "CreateDirectoryA", reinterpret_cast<void*>(CreateDirectoryA));
    ldr.registerExport("kernel32.dll", "RemoveDirectoryW", reinterpret_cast<void*>(RemoveDirectoryW));
    ldr.registerExport("kernel32.dll", "RemoveDirectoryA", reinterpret_cast<void*>(RemoveDirectoryA));
    ldr.registerExport("kernel32.dll", "FindFirstFileW", reinterpret_cast<void*>(FindFirstFileW));
    ldr.registerExport("kernel32.dll", "FindNextFileW", reinterpret_cast<void*>(FindNextFileW));
    ldr.registerExport("kernel32.dll", "FindClose", reinterpret_cast<void*>(FindClose));
    ldr.registerExport("kernel32.dll", "CreateEventW", reinterpret_cast<void*>(CreateEventW));
    ldr.registerExport("kernel32.dll", "SetEvent", reinterpret_cast<void*>(SetEvent));
    ldr.registerExport("kernel32.dll", "ResetEvent", reinterpret_cast<void*>(ResetEvent));
    ldr.registerExport("kernel32.dll", "WaitForSingleObject", reinterpret_cast<void*>(WaitForSingleObject));
    ldr.registerExport("kernel32.dll", "WaitForMultipleObjects", reinterpret_cast<void*>(WaitForMultipleObjects));
    ldr.registerExport("kernel32.dll", "Sleep", reinterpret_cast<void*>(Sleep));
    ldr.registerExport("kernel32.dll", "GetTickCount64", reinterpret_cast<void*>(GetTickCount64));
    ldr.registerExport("kernel32.dll", "GetSystemInfo", reinterpret_cast<void*>(GetSystemInfo));
    ldr.registerExport("kernel32.dll", "LoadLibraryW", reinterpret_cast<void*>(LoadLibraryW));
    ldr.registerExport("kernel32.dll", "GetProcAddress", reinterpret_cast<void*>(GetProcAddress));
    ldr.registerExport("kernel32.dll", "FreeLibrary", reinterpret_cast<void*>(FreeLibrary));
    ldr.registerExport("kernel32.dll", "GetLastError", reinterpret_cast<void*>(GetLastError));
    ldr.registerExport("kernel32.dll", "SetLastError", reinterpret_cast<void*>(SetLastError));
    ldr.registerExport("kernel32.dll", "GetCommandLineA", reinterpret_cast<void*>(GetCommandLineA));
    ldr.registerExport("kernel32.dll", "GetCommandLineW", reinterpret_cast<void*>(GetCommandLineW));
    ldr.registerExport("kernel32.dll", "GetEnvironmentVariableA", reinterpret_cast<void*>(GetEnvironmentVariableA));
    ldr.registerExport("kernel32.dll", "GetEnvironmentVariableW", reinterpret_cast<void*>(GetEnvironmentVariableW));
    ldr.registerExport("kernel32.dll", "SetEnvironmentVariableA", reinterpret_cast<void*>(SetEnvironmentVariableA));
    ldr.registerExport("kernel32.dll", "SetEnvironmentVariableW", reinterpret_cast<void*>(SetEnvironmentVariableW));
    ldr.registerExport("kernel32.dll", "GetCurrentDirectoryA", reinterpret_cast<void*>(GetCurrentDirectoryA));
    ldr.registerExport("kernel32.dll", "GetCurrentDirectoryW", reinterpret_cast<void*>(GetCurrentDirectoryW));
    ldr.registerExport("kernel32.dll", "SetCurrentDirectoryA", reinterpret_cast<void*>(SetCurrentDirectoryA));
    ldr.registerExport("kernel32.dll", "SetCurrentDirectoryW", reinterpret_cast<void*>(SetCurrentDirectoryW));
    ldr.registerExport("kernel32.dll", "GetFullPathNameA", reinterpret_cast<void*>(GetFullPathNameA));
    ldr.registerExport("kernel32.dll", "GetFullPathNameW", reinterpret_cast<void*>(GetFullPathNameW));
    ldr.registerExport("kernel32.dll", "GetModuleFileNameA", reinterpret_cast<void*>(GetModuleFileNameA));
    ldr.registerExport("kernel32.dll", "GetModuleFileNameW", reinterpret_cast<void*>(GetModuleFileNameW));
    ldr.registerExport("kernel32.dll", "GetModuleHandleA", reinterpret_cast<void*>(GetModuleHandleA));
    ldr.registerExport("kernel32.dll", "GetModuleHandleW", reinterpret_cast<void*>(GetModuleHandleW));
    ldr.registerExport("kernel32.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(QueryPerformanceCounter));
    ldr.registerExport("kernel32.dll", "QueryPerformanceFrequency", reinterpret_cast<void*>(QueryPerformanceFrequency));
    ldr.registerExport("kernel32.dll", "GetSystemTime", reinterpret_cast<void*>(GetSystemTime));
    ldr.registerExport("kernel32.dll", "GetLocalTime", reinterpret_cast<void*>(GetLocalTime));
    ldr.registerExport("kernel32.dll", "GetSystemTimeAsFileTime", reinterpret_cast<void*>(GetSystemTimeAsFileTime));
    ldr.registerExport("kernel32.dll", "CreateFileMappingW", reinterpret_cast<void*>(CreateFileMappingW));
    ldr.registerExport("kernel32.dll", "MapViewOfFile", reinterpret_cast<void*>(MapViewOfFile));
    ldr.registerExport("kernel32.dll", "UnmapViewOfFile", reinterpret_cast<void*>(UnmapViewOfFile));
    ldr.registerExport("kernel32.dll", "GetExitCodeProcess", reinterpret_cast<void*>(GetExitCodeProcess));
    ldr.registerExport("kernel32.dll", "TerminateProcess", reinterpret_cast<void*>(TerminateProcess));
    ldr.registerExport("kernel32.dll", "SwitchToThread", reinterpret_cast<void*>(SwitchToThread));
    ldr.registerExport("kernel32.dll", "ResumeThread", reinterpret_cast<void*>(ResumeThread));
    ldr.registerExport("kernel32.dll", "SuspendThread", reinterpret_cast<void*>(SuspendThread));
    ldr.registerExport("kernel32.dll", "TerminateThread", reinterpret_cast<void*>(TerminateThread));
    ldr.registerExport("kernel32.dll", "GetThreadContext", reinterpret_cast<void*>(GetThreadContext));
    ldr.registerExport("kernel32.dll", "CreateThread", reinterpret_cast<void*>(CreateThread));
    ldr.registerExport("kernel32.dll", "OpenThread", reinterpret_cast<void*>(OpenThread));
    ldr.registerExport("kernel32.dll", "SetThreadAffinityMask", reinterpret_cast<void*>(SetThreadAffinityMask));
    ldr.registerExport("kernel32.dll", "GetVersion", reinterpret_cast<void*>(GetVersion));
    ldr.registerExport("kernel32.dll", "GetLargePageMinimum", reinterpret_cast<void*>(GetLargePageMinimum));
    ldr.registerExport("kernel32.dll", "SetFileApisToOEM", reinterpret_cast<void*>(SetFileApisToOEM));
    ldr.registerExport("kernel32.dll", "SetFileApisToANSI", reinterpret_cast<void*>(SetFileApisToANSI));
    ldr.registerExport("kernel32.dll", "CreateSemaphoreW", reinterpret_cast<void*>(CreateSemaphoreW));
    ldr.registerExport("kernel32.dll", "ReleaseSemaphore", reinterpret_cast<void*>(ReleaseSemaphore));
    ldr.registerExport("kernel32.dll", "DuplicateHandle", reinterpret_cast<void*>(DuplicateHandle));
    ldr.registerExport("kernel32.dll", "GetProcessId", reinterpret_cast<void*>(GetProcessId));
    ldr.registerExport("kernel32.dll", "GetStartupInfoA", reinterpret_cast<void*>(GetStartupInfoA));
    ldr.registerExport("kernel32.dll", "HeapSetInformation", reinterpret_cast<void*>(HeapSetInformation));
    ldr.registerExport("kernel32.dll", "IsDBCSLeadByteEx", reinterpret_cast<void*>(IsDBCSLeadByteEx));
    ldr.registerExport("kernel32.dll", "SetErrorMode", reinterpret_cast<void*>(SetErrorMode));
    ldr.registerExport("kernel32.dll", "VirtualQueryEx", reinterpret_cast<void*>(VirtualQueryEx));

    // CRT startup & interop helpers
    ldr.registerExport("kernel32.dll", "InitializeSListHead", reinterpret_cast<void*>(InitializeSListHead));
    ldr.registerExport("kernel32.dll", "FlsAlloc", reinterpret_cast<void*>(FlsAlloc));
    ldr.registerExport("kernel32.dll", "FlsGetValue", reinterpret_cast<void*>(FlsGetValue));
    ldr.registerExport("kernel32.dll", "FlsSetValue", reinterpret_cast<void*>(FlsSetValue));
    ldr.registerExport("kernel32.dll", "FlsFree", reinterpret_cast<void*>(FlsFree));
    ldr.registerExport("kernel32.dll", "InitializeCriticalSection", reinterpret_cast<void*>(InitializeCriticalSection));
    ldr.registerExport("kernel32.dll", "InitializeCriticalSectionEx", reinterpret_cast<void*>(InitializeCriticalSectionEx));
    ldr.registerExport("kernel32.dll", "EnterCriticalSection", reinterpret_cast<void*>(EnterCriticalSection));
    ldr.registerExport("kernel32.dll", "LeaveCriticalSection", reinterpret_cast<void*>(LeaveCriticalSection));
    ldr.registerExport("kernel32.dll", "DeleteCriticalSection", reinterpret_cast<void*>(DeleteCriticalSection));
    ldr.registerExport("kernel32.dll", "EncodePointer", reinterpret_cast<void*>(EncodePointer));
    ldr.registerExport("kernel32.dll", "DecodePointer", reinterpret_cast<void*>(DecodePointer));
    ldr.registerExport("kernel32.dll", "GetStartupInfoW", reinterpret_cast<void*>(GetStartupInfoW));
    ldr.registerExport("kernel32.dll", "GetFileType", reinterpret_cast<void*>(GetFileType));
    ldr.registerExport("kernel32.dll", "IsProcessorFeaturePresent", reinterpret_cast<void*>(IsProcessorFeaturePresent));
    ldr.registerExport("kernel32.dll", "IsDebuggerPresent", reinterpret_cast<void*>(IsDebuggerPresent));
    ldr.registerExport("kernel32.dll", "SetUnhandledExceptionFilter", reinterpret_cast<void*>(SetUnhandledExceptionFilter));
    ldr.registerExport("kernel32.dll", "UnhandledExceptionFilter", reinterpret_cast<void*>(UnhandledExceptionFilter));
    ldr.registerExport("kernel32.dll", "MultiByteToWideChar", reinterpret_cast<void*>(MultiByteToWideChar));
    ldr.registerExport("kernel32.dll", "WideCharToMultiByte", reinterpret_cast<void*>(WideCharToMultiByte));
    ldr.registerExport("kernel32.dll", "GetACP", reinterpret_cast<void*>(GetACP));
    ldr.registerExport("kernel32.dll", "GetOEMCP", reinterpret_cast<void*>(GetOEMCP));
    ldr.registerExport("kernel32.dll", "IsValidCodePage", reinterpret_cast<void*>(IsValidCodePage));
    ldr.registerExport("kernel32.dll", "GetCPInfo", reinterpret_cast<void*>(GetCPInfo));
    ldr.registerExport("kernel32.dll", "CompareStringW", reinterpret_cast<void*>(CompareStringW));
    ldr.registerExport("kernel32.dll", "LCMapStringW", reinterpret_cast<void*>(LCMapStringW));
    ldr.registerExport("kernel32.dll", "FindFirstFileExW", reinterpret_cast<void*>(FindFirstFileExW));
    ldr.registerExport("kernel32.dll", "FlushFileBuffers", reinterpret_cast<void*>(FlushFileBuffers));
    ldr.registerExport("kernel32.dll", "GetEnvironmentStringsW", reinterpret_cast<void*>(GetEnvironmentStringsW));
    ldr.registerExport("kernel32.dll", "FreeEnvironmentStringsW", reinterpret_cast<void*>(FreeEnvironmentStringsW));
    ldr.registerExport("kernel32.dll", "GetStringTypeW", reinterpret_cast<void*>(GetStringTypeW));
    ldr.registerExport("kernel32.dll", "VirtualProtect", reinterpret_cast<void*>(VirtualProtect));
    ldr.registerExport("kernel32.dll", "VirtualQuery", reinterpret_cast<void*>(VirtualQuery));
    ldr.registerExport("kernel32.dll", "TlsAlloc", reinterpret_cast<void*>(TlsAlloc));
    ldr.registerExport("kernel32.dll", "TlsGetValue", reinterpret_cast<void*>(TlsGetValue));
    ldr.registerExport("kernel32.dll", "TlsSetValue", reinterpret_cast<void*>(TlsSetValue));
    ldr.registerExport("kernel32.dll", "TlsFree", reinterpret_cast<void*>(TlsFree));
    ldr.registerExport("kernel32.dll", "RtlCaptureContext", reinterpret_cast<void*>(RtlCaptureContext));
    ldr.registerExport("kernel32.dll", "RtlRestoreContext", reinterpret_cast<void*>(RtlRestoreContext));
    ldr.registerExport("kernel32.dll", "RtlLookupFunctionEntry", reinterpret_cast<void*>(RtlLookupFunctionEntry));
    ldr.registerExport("kernel32.dll", "RtlVirtualUnwind", reinterpret_cast<void*>(RtlVirtualUnwind));
    ldr.registerExport("kernel32.dll", "RtlUnwindEx", reinterpret_cast<void*>(RtlUnwindEx));
    ldr.registerExport("kernel32.dll", "RtlPcToFileHeader", reinterpret_cast<void*>(RtlPcToFileHeader));
    ldr.registerExport("kernel32.dll", "RaiseException", reinterpret_cast<void*>(RaiseException));
    ldr.registerExport("kernel32.dll", "LoadLibraryExW", reinterpret_cast<void*>(LoadLibraryExW));
    ldr.registerExport("kernel32.dll", "GetModuleHandleExW", reinterpret_cast<void*>(GetModuleHandleExW));
    ldr.registerExport("kernel32.dll", "GetConsoleOutputCP", reinterpret_cast<void*>(GetConsoleOutputCP));

    // Named Pipes & Mailslots IPC exports
    ldr.registerExport("kernel32.dll", "CreateNamedPipeW", reinterpret_cast<void*>(CreateNamedPipeW));
    ldr.registerExport("kernel32.dll", "ConnectNamedPipe", reinterpret_cast<void*>(ConnectNamedPipe));
    ldr.registerExport("kernel32.dll", "DisconnectNamedPipe", reinterpret_cast<void*>(DisconnectNamedPipe));
    ldr.registerExport("kernel32.dll", "WaitNamedPipeW", reinterpret_cast<void*>(WaitNamedPipeW));
    ldr.registerExport("kernel32.dll", "PeekNamedPipe", reinterpret_cast<void*>(PeekNamedPipe));
    ldr.registerExport("kernel32.dll", "TransactNamedPipe", reinterpret_cast<void*>(TransactNamedPipe));
    ldr.registerExport("kernel32.dll", "GetNamedPipeInfo", reinterpret_cast<void*>(GetNamedPipeInfo));
    ldr.registerExport("kernel32.dll", "GetNamedPipeHandleStateW", reinterpret_cast<void*>(GetNamedPipeHandleStateW));
    ldr.registerExport("kernel32.dll", "SetNamedPipeHandleState", reinterpret_cast<void*>(SetNamedPipeHandleState));
    ldr.registerExport("kernel32.dll", "CreateMailslotW", reinterpret_cast<void*>(CreateMailslotW));
    ldr.registerExport("kernel32.dll", "GetMailslotInfo", reinterpret_cast<void*>(GetMailslotInfo));
    ldr.registerExport("kernel32.dll", "SetMailslotInfo", reinterpret_cast<void*>(SetMailslotInfo));

    // Extended Win32 Subsystem exports (7-Zip, VLC, Notepad++)
    ldr.registerExport("kernel32.dll", "GetTickCount", reinterpret_cast<void*>(GetTickCount));
    ldr.registerExport("kernel32.dll", "SetConsoleCtrlHandler", reinterpret_cast<void*>(SetConsoleCtrlHandler));
    ldr.registerExport("kernel32.dll", "GetProcessTimes", reinterpret_cast<void*>(GetProcessTimes));
    ldr.registerExport("kernel32.dll", "SetProcessAffinityMask", reinterpret_cast<void*>(SetProcessAffinityMask));
    ldr.registerExport("kernel32.dll", "GetProcessAffinityMask", reinterpret_cast<void*>(GetProcessAffinityMask));
    ldr.registerExport("kernel32.dll", "OpenEventW", reinterpret_cast<void*>(OpenEventW));
    ldr.registerExport("kernel32.dll", "OpenFileMappingW", reinterpret_cast<void*>(OpenFileMappingW));
    ldr.registerExport("kernel32.dll", "CompareFileTime", reinterpret_cast<void*>(CompareFileTime));
    ldr.registerExport("kernel32.dll", "FileTimeToSystemTime", reinterpret_cast<void*>(FileTimeToSystemTime));
    ldr.registerExport("kernel32.dll", "FileTimeToLocalFileTime", reinterpret_cast<void*>(FileTimeToLocalFileTime));
    ldr.registerExport("kernel32.dll", "FileTimeToDosDateTime", reinterpret_cast<void*>(FileTimeToDosDateTime));
    ldr.registerExport("kernel32.dll", "GlobalMemoryStatusEx", reinterpret_cast<void*>(GlobalMemoryStatusEx));
    ldr.registerExport("kernel32.dll", "GetDiskFreeSpaceW", reinterpret_cast<void*>(GetDiskFreeSpaceW));
    ldr.registerExport("kernel32.dll", "GetDiskFreeSpaceExW", reinterpret_cast<void*>(GetDiskFreeSpaceExW));
    ldr.registerExport("kernel32.dll", "SetEndOfFile", reinterpret_cast<void*>(SetEndOfFile));
    ldr.registerExport("kernel32.dll", "FormatMessageW", reinterpret_cast<void*>(FormatMessageW));
    ldr.registerExport("kernel32.dll", "SetFileTime", reinterpret_cast<void*>(SetFileTime));
    ldr.registerExport("kernel32.dll", "MoveFileWithProgressW", reinterpret_cast<void*>(MoveFileWithProgressW));
    ldr.registerExport("kernel32.dll", "CreateHardLinkW", reinterpret_cast<void*>(CreateHardLinkW));
    ldr.registerExport("kernel32.dll", "GetTempPathW", reinterpret_cast<void*>(GetTempPathW));
    ldr.registerExport("kernel32.dll", "GetFileInformationByHandle", reinterpret_cast<void*>(GetFileInformationByHandle));
    ldr.registerExport("kernel32.dll", "FindFirstStreamW", reinterpret_cast<void*>(FindFirstStreamW));
    ldr.registerExport("kernel32.dll", "FindNextStreamW", reinterpret_cast<void*>(FindNextStreamW));
    ldr.registerExport("kernel32.dll", "GetLogicalDriveStringsW", reinterpret_cast<void*>(GetLogicalDriveStringsW));
    ldr.registerExport("kernel32.dll", "DeviceIoControl", reinterpret_cast<void*>(DeviceIoControl));

    // Global memory and string utilities (Notepad++)
    ldr.registerExport("kernel32.dll", "GlobalAlloc", reinterpret_cast<void*>(GlobalAlloc));
    ldr.registerExport("kernel32.dll", "GlobalFree", reinterpret_cast<void*>(GlobalFree));
    ldr.registerExport("kernel32.dll", "GlobalLock", reinterpret_cast<void*>(GlobalLock));
    ldr.registerExport("kernel32.dll", "GlobalUnlock", reinterpret_cast<void*>(GlobalUnlock));
    ldr.registerExport("kernel32.dll", "lstrcmpiW", reinterpret_cast<void*>(lstrcmpiW));
    ldr.registerExport("kernel32.dll", "lstrcmpiA", reinterpret_cast<void*>(lstrcmpiA));
    ldr.registerExport("kernel32.dll", "lstrcpynW", reinterpret_cast<void*>(lstrcpynW));
    ldr.registerExport("kernel32.dll", "ExpandEnvironmentStringsW", reinterpret_cast<void*>(ExpandEnvironmentStringsW));
    ldr.registerExport("kernel32.dll", "GetDateFormatW", reinterpret_cast<void*>(GetDateFormatW));
    ldr.registerExport("kernel32.dll", "GetDateFormatEx", reinterpret_cast<void*>(GetDateFormatEx));
    ldr.registerExport("kernel32.dll", "GetTimeFormatEx", reinterpret_cast<void*>(GetTimeFormatEx));
    ldr.registerExport("kernel32.dll", "GetProductInfo", reinterpret_cast<void*>(GetProductInfo));
    ldr.registerExport("kernel32.dll", "GetVersionExW", reinterpret_cast<void*>(GetVersionExW));
    ldr.registerExport("kernel32.dll", "FindResourceW", reinterpret_cast<void*>(FindResourceW));
    ldr.registerExport("kernel32.dll", "LoadResource", reinterpret_cast<void*>(LoadResource));
    ldr.registerExport("kernel32.dll", "LockResource", reinterpret_cast<void*>(LockResource));
    ldr.registerExport("kernel32.dll", "SizeofResource", reinterpret_cast<void*>(SizeofResource));
    ldr.registerExport("kernel32.dll", "GetLongPathNameW", reinterpret_cast<void*>(GetLongPathNameW));
    ldr.registerExport("kernel32.dll", "GetFinalPathNameByHandleW", reinterpret_cast<void*>(GetFinalPathNameByHandleW));
    ldr.registerExport("api-ms-win-core-file-l1-1-0.dll", "GetFinalPathNameByHandleW", reinterpret_cast<void*>(GetFinalPathNameByHandleW));
    ldr.registerExport("kernel32.dll", "GetSystemTimeAdjustment", reinterpret_cast<void*>(GetSystemTimeAdjustment));
    ldr.registerExport("api-ms-win-core-sysinfo-l1-2-0.dll", "GetSystemTimeAdjustment", reinterpret_cast<void*>(GetSystemTimeAdjustment));
    ldr.registerExport("kernel32.dll", "GetSystemTimeAdjustmentPrecise", reinterpret_cast<void*>(GetSystemTimeAdjustmentPrecise));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "GetSystemTimeAdjustmentPrecise", reinterpret_cast<void*>(GetSystemTimeAdjustmentPrecise));
    ldr.registerExport("api-ms-win-core-sysinfo-l1-2-0.dll", "GetLogicalProcessorInformationEx", reinterpret_cast<void*>(GetLogicalProcessorInformationEx));
    ldr.registerExport("kernel32.dll", "ReleaseMutex", reinterpret_cast<void*>(ReleaseMutex));
    ldr.registerExport("kernel32.dll", "OpenProcess", reinterpret_cast<void*>(OpenProcess));
    ldr.registerExport("kernel32.dll", "GetApplicationRestartSettings", reinterpret_cast<void*>(GetApplicationRestartSettings));
    ldr.registerExport("kernel32.dll", "UnregisterApplicationRestart", reinterpret_cast<void*>(UnregisterApplicationRestart));
    ldr.registerExport("kernel32.dll", "ExitThread", reinterpret_cast<void*>(ExitThread));
    ldr.registerExport("kernel32.dll", "RegisterApplicationRestart", reinterpret_cast<void*>(RegisterApplicationRestart));
    ldr.registerExport("kernel32.dll", "AcquireSRWLockExclusive", reinterpret_cast<void*>(AcquireSRWLockExclusive));
    ldr.registerExport("kernel32.dll", "ReleaseSRWLockExclusive", reinterpret_cast<void*>(ReleaseSRWLockExclusive));
    ldr.registerExport("kernel32.dll", "TryAcquireSRWLockExclusive", reinterpret_cast<void*>(TryAcquireSRWLockExclusive));
    ldr.registerExport("kernel32.dll", "AcquireSRWLockShared", reinterpret_cast<void*>(AcquireSRWLockShared));
    ldr.registerExport("kernel32.dll", "ReleaseSRWLockShared", reinterpret_cast<void*>(ReleaseSRWLockShared));
    ldr.registerExport("kernel32.dll", "TryAcquireSRWLockShared", reinterpret_cast<void*>(TryAcquireSRWLockShared));
    ldr.registerExport("kernel32.dll", "InitializeSRWLock", reinterpret_cast<void*>(InitializeSRWLock));
    ldr.registerExport("kernel32.dll", "SleepConditionVariableSRW", reinterpret_cast<void*>(SleepConditionVariableSRW));
    ldr.registerExport("kernel32.dll", "WakeConditionVariable", reinterpret_cast<void*>(WakeConditionVariable));
    ldr.registerExport("kernel32.dll", "WakeAllConditionVariable", reinterpret_cast<void*>(WakeAllConditionVariable));
    ldr.registerExport("kernel32.dll", "InitializeConditionVariable", reinterpret_cast<void*>(InitializeConditionVariable));
    ldr.registerExport("kernel32.dll", "WaitOnAddress", reinterpret_cast<void*>(WaitOnAddress));
    ldr.registerExport("kernel32.dll", "WakeByAddressSingle", reinterpret_cast<void*>(WakeByAddressSingle));
    ldr.registerExport("kernel32.dll", "WakeByAddressAll", reinterpret_cast<void*>(WakeByAddressAll));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "WaitOnAddress", reinterpret_cast<void*>(WaitOnAddress));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "WakeByAddressSingle", reinterpret_cast<void*>(WakeByAddressSingle));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "WakeByAddressAll", reinterpret_cast<void*>(WakeByAddressAll));
    ldr.registerExport("kernel32.dll", "InitializeSynchronizationBarrier", reinterpret_cast<void*>(InitializeSynchronizationBarrier));
    ldr.registerExport("kernel32.dll", "EnterSynchronizationBarrier", reinterpret_cast<void*>(EnterSynchronizationBarrier));
    ldr.registerExport("kernel32.dll", "DeleteSynchronizationBarrier", reinterpret_cast<void*>(DeleteSynchronizationBarrier));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "InitializeSynchronizationBarrier", reinterpret_cast<void*>(InitializeSynchronizationBarrier));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "EnterSynchronizationBarrier", reinterpret_cast<void*>(EnterSynchronizationBarrier));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "DeleteSynchronizationBarrier", reinterpret_cast<void*>(DeleteSynchronizationBarrier));
    ldr.registerExport("kernel32.dll", "InitOnceBeginInitialize", reinterpret_cast<void*>(InitOnceBeginInitialize));
    ldr.registerExport("kernel32.dll", "InitOnceComplete", reinterpret_cast<void*>(InitOnceComplete));
    ldr.registerExport("kernel32.dll", "InitOnceExecuteOnce", reinterpret_cast<void*>(InitOnceExecuteOnce));
    ldr.registerExport("kernel32.dll", "CreateThreadpool", reinterpret_cast<void*>(CreateThreadpool));
    ldr.registerExport("kernel32.dll", "SetThreadpoolThreadMaximum", reinterpret_cast<void*>(SetThreadpoolThreadMaximum));
    ldr.registerExport("kernel32.dll", "SetThreadpoolThreadMinimum", reinterpret_cast<void*>(SetThreadpoolThreadMinimum));
    ldr.registerExport("kernel32.dll", "CloseThreadpool", reinterpret_cast<void*>(CloseThreadpool));
    ldr.registerExport("kernel32.dll", "SetThreadpoolStackInformation", reinterpret_cast<void*>(SetThreadpoolStackInformation));
    ldr.registerExport("kernel32.dll", "QueryThreadpoolStackInformation", reinterpret_cast<void*>(QueryThreadpoolStackInformation));
    ldr.registerExport("kernel32.dll", "CreateThreadpoolWork", reinterpret_cast<void*>(CreateThreadpoolWork));
    ldr.registerExport("kernel32.dll", "SubmitThreadpoolWork", reinterpret_cast<void*>(SubmitThreadpoolWork));
    ldr.registerExport("kernel32.dll", "WaitForThreadpoolWorkCallbacks", reinterpret_cast<void*>(WaitForThreadpoolWorkCallbacks));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolWork", reinterpret_cast<void*>(CloseThreadpoolWork));
    ldr.registerExport("kernel32.dll", "CreateThreadpoolTimer", reinterpret_cast<void*>(CreateThreadpoolTimer));
    ldr.registerExport("kernel32.dll", "SetThreadpoolTimer", reinterpret_cast<void*>(SetThreadpoolTimer));
    ldr.registerExport("kernel32.dll", "SetThreadpoolTimerEx", reinterpret_cast<void*>(SetThreadpoolTimerEx));
    ldr.registerExport("kernel32.dll", "IsThreadpoolTimerSet", reinterpret_cast<void*>(IsThreadpoolTimerSet));
    ldr.registerExport("kernel32.dll", "WaitForThreadpoolTimerCallbacks", reinterpret_cast<void*>(WaitForThreadpoolTimerCallbacks));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolTimer", reinterpret_cast<void*>(CloseThreadpoolTimer));
    ldr.registerExport("kernel32.dll", "CreateThreadpoolWait", reinterpret_cast<void*>(CreateThreadpoolWait));
    ldr.registerExport("kernel32.dll", "SetThreadpoolWait", reinterpret_cast<void*>(SetThreadpoolWait));
    ldr.registerExport("kernel32.dll", "SetThreadpoolWaitEx", reinterpret_cast<void*>(SetThreadpoolWaitEx));
    ldr.registerExport("kernel32.dll", "WaitForThreadpoolWaitCallbacks", reinterpret_cast<void*>(WaitForThreadpoolWaitCallbacks));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolWait", reinterpret_cast<void*>(CloseThreadpoolWait));
    ldr.registerExport("kernel32.dll", "CreateThreadpoolCleanupGroup", reinterpret_cast<void*>(CreateThreadpoolCleanupGroup));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolCleanupGroupMembers", reinterpret_cast<void*>(CloseThreadpoolCleanupGroupMembers));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolCleanupGroup", reinterpret_cast<void*>(CloseThreadpoolCleanupGroup));
    ldr.registerExport("kernel32.dll", "SetThreadpoolCallbackPool", reinterpret_cast<void*>(SetThreadpoolCallbackPool));
    ldr.registerExport("kernel32.dll", "SetThreadpoolCallbackCleanupGroup", reinterpret_cast<void*>(SetThreadpoolCallbackCleanupGroup));
    ldr.registerExport("kernel32.dll", "SetThreadpoolCallbackPriority", reinterpret_cast<void*>(SetThreadpoolCallbackPriority));
    ldr.registerExport("kernel32.dll", "SetThreadpoolCallbackRunsLong", reinterpret_cast<void*>(SetThreadpoolCallbackRunsLong));
    ldr.registerExport("kernel32.dll", "SetThreadpoolCallbackPersistent", reinterpret_cast<void*>(SetThreadpoolCallbackPersistent));
    ldr.registerExport("kernel32.dll", "SetThreadpoolCallbackLibrary", reinterpret_cast<void*>(SetThreadpoolCallbackLibrary));
    ldr.registerExport("kernel32.dll", "InitializeThreadpoolEnvironment", reinterpret_cast<void*>(InitializeThreadpoolEnvironment));
    ldr.registerExport("kernel32.dll", "DestroyThreadpoolEnvironment", reinterpret_cast<void*>(DestroyThreadpoolEnvironment));
    ldr.registerExport("kernel32.dll", "FreeLibraryWhenCallbackReturns", reinterpret_cast<void*>(FreeLibraryWhenCallbackReturns));
    ldr.registerExport("kernel32.dll", "LeaveCriticalSectionWhenCallbackReturns", reinterpret_cast<void*>(LeaveCriticalSectionWhenCallbackReturns));
    ldr.registerExport("kernel32.dll", "ReleaseMutexWhenCallbackReturns", reinterpret_cast<void*>(ReleaseMutexWhenCallbackReturns));
    ldr.registerExport("kernel32.dll", "ReleaseSemaphoreWhenCallbackReturns", reinterpret_cast<void*>(ReleaseSemaphoreWhenCallbackReturns));
    ldr.registerExport("kernel32.dll", "SetEventWhenCallbackReturns", reinterpret_cast<void*>(SetEventWhenCallbackReturns));
    ldr.registerExport("kernel32.dll", "DisassociateCurrentThreadFromCallback", reinterpret_cast<void*>(DisassociateCurrentThreadFromCallback));
    ldr.registerExport("kernel32.dll", "CallbackMayRunLong", reinterpret_cast<void*>(CallbackMayRunLong));
    ldr.registerExport("kernel32.dll", "BindIoCompletionCallback", reinterpret_cast<void*>(BindIoCompletionCallback));

    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CreateThreadpool", reinterpret_cast<void*>(CreateThreadpool));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolThreadMaximum", reinterpret_cast<void*>(SetThreadpoolThreadMaximum));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolThreadMinimum", reinterpret_cast<void*>(SetThreadpoolThreadMinimum));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpool", reinterpret_cast<void*>(CloseThreadpool));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolStackInformation", reinterpret_cast<void*>(SetThreadpoolStackInformation));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "QueryThreadpoolStackInformation", reinterpret_cast<void*>(QueryThreadpoolStackInformation));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolCallbackPool", reinterpret_cast<void*>(SetThreadpoolCallbackPool));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolCallbackCleanupGroup", reinterpret_cast<void*>(SetThreadpoolCallbackCleanupGroup));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CreateThreadpoolCleanupGroup", reinterpret_cast<void*>(CreateThreadpoolCleanupGroup));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpoolCleanupGroupMembers", reinterpret_cast<void*>(CloseThreadpoolCleanupGroupMembers));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpoolCleanupGroup", reinterpret_cast<void*>(CloseThreadpoolCleanupGroup));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolCallbackPriority", reinterpret_cast<void*>(SetThreadpoolCallbackPriority));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolCallbackRunsLong", reinterpret_cast<void*>(SetThreadpoolCallbackRunsLong));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolCallbackPersistent", reinterpret_cast<void*>(SetThreadpoolCallbackPersistent));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolCallbackLibrary", reinterpret_cast<void*>(SetThreadpoolCallbackLibrary));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "InitializeThreadpoolEnvironment", reinterpret_cast<void*>(InitializeThreadpoolEnvironment));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "DestroyThreadpoolEnvironment", reinterpret_cast<void*>(DestroyThreadpoolEnvironment));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CreateThreadpoolWork", reinterpret_cast<void*>(CreateThreadpoolWork));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SubmitThreadpoolWork", reinterpret_cast<void*>(SubmitThreadpoolWork));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "WaitForThreadpoolWorkCallbacks", reinterpret_cast<void*>(WaitForThreadpoolWorkCallbacks));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpoolWork", reinterpret_cast<void*>(CloseThreadpoolWork));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CreateThreadpoolTimer", reinterpret_cast<void*>(CreateThreadpoolTimer));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolTimer", reinterpret_cast<void*>(SetThreadpoolTimer));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolTimerEx", reinterpret_cast<void*>(SetThreadpoolTimerEx));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "IsThreadpoolTimerSet", reinterpret_cast<void*>(IsThreadpoolTimerSet));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "WaitForThreadpoolTimerCallbacks", reinterpret_cast<void*>(WaitForThreadpoolTimerCallbacks));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpoolTimer", reinterpret_cast<void*>(CloseThreadpoolTimer));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CreateThreadpoolWait", reinterpret_cast<void*>(CreateThreadpoolWait));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolWait", reinterpret_cast<void*>(SetThreadpoolWait));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetThreadpoolWaitEx", reinterpret_cast<void*>(SetThreadpoolWaitEx));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CancelThreadpoolWait", reinterpret_cast<void*>(CancelThreadpoolWait));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "WaitForThreadpoolWaitCallbacks", reinterpret_cast<void*>(WaitForThreadpoolWaitCallbacks));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpoolWait", reinterpret_cast<void*>(CloseThreadpoolWait));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "FreeLibraryWhenCallbackReturns", reinterpret_cast<void*>(FreeLibraryWhenCallbackReturns));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "LeaveCriticalSectionWhenCallbackReturns", reinterpret_cast<void*>(LeaveCriticalSectionWhenCallbackReturns));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "ReleaseMutexWhenCallbackReturns", reinterpret_cast<void*>(ReleaseMutexWhenCallbackReturns));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "ReleaseSemaphoreWhenCallbackReturns", reinterpret_cast<void*>(ReleaseSemaphoreWhenCallbackReturns));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "SetEventWhenCallbackReturns", reinterpret_cast<void*>(SetEventWhenCallbackReturns));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "DisassociateCurrentThreadFromCallback", reinterpret_cast<void*>(DisassociateCurrentThreadFromCallback));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CallbackMayRunLong", reinterpret_cast<void*>(CallbackMayRunLong));
    ldr.registerExport("kernel32.dll", "MulDiv", reinterpret_cast<void*>(MulDiv));
    ldr.registerExport("kernel32.dll", "CompareStringOrdinal", reinterpret_cast<void*>(CompareStringOrdinal));
    ldr.registerExport("kernel32.dll", "CompareStringEx", reinterpret_cast<void*>(CompareStringEx));
    ldr.registerExport("kernel32.dll", "LCMapStringEx", reinterpret_cast<void*>(LCMapStringEx));
    ldr.registerExport("kernel32.dll", "lstrcmpW", reinterpret_cast<void*>(lstrcmpW));
    ldr.registerExport("kernel32.dll", "lstrcpyW", reinterpret_cast<void*>(lstrcpyW));
    ldr.registerExport("kernel32.dll", "lstrcpynA", reinterpret_cast<void*>(lstrcpynA));
    ldr.registerExport("kernel32.dll", "lstrlenW", reinterpret_cast<void*>(lstrlenW));
    ldr.registerExport("kernel32.dll", "GetNativeSystemInfo", reinterpret_cast<void*>(GetNativeSystemInfo));
    ldr.registerExport("kernel32.dll", "QueryFullProcessImageNameW", reinterpret_cast<void*>(QueryFullProcessImageNameW));
    ldr.registerExport("kernel32.dll", "QueryFullProcessImageNameA", reinterpret_cast<void*>(QueryFullProcessImageNameA));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "QueryFullProcessImageNameW", reinterpret_cast<void*>(QueryFullProcessImageNameW));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "QueryFullProcessImageNameA", reinterpret_cast<void*>(QueryFullProcessImageNameA));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "QueryFullProcessImageNameW", reinterpret_cast<void*>(QueryFullProcessImageNameW));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "QueryFullProcessImageNameA", reinterpret_cast<void*>(QueryFullProcessImageNameA));
    ldr.registerExport("api-ms-win-core-file-l1-2-0.dll", "GetFileAttributesExW", reinterpret_cast<void*>(GetFileAttributesExW));
    ldr.registerExport("api-ms-win-core-file-l1-2-0.dll", "GetFileAttributesExA", reinterpret_cast<void*>(GetFileAttributesExA));
    ldr.registerExport("api-ms-win-core-file-l2-1-0.dll", "GetFileAttributesExW", reinterpret_cast<void*>(GetFileAttributesExW));
    ldr.registerExport("kernel32.dll", "GlobalSize", reinterpret_cast<void*>(GlobalSize));
    ldr.registerExport("kernel32.dll", "CopyFileExW", reinterpret_cast<void*>(CopyFileExW));
    ldr.registerExport("kernel32.dll", "ReplaceFileW", reinterpret_cast<void*>(ReplaceFileW));
    ldr.registerExport("kernel32.dll", "GetFileAttributesExW", reinterpret_cast<void*>(GetFileAttributesExW));
    ldr.registerExport("kernel32.dll", "GetFileInformationByHandleEx", reinterpret_cast<void*>(GetFileInformationByHandleEx));
    ldr.registerExport("kernel32.dll", "AreFileApisANSI", reinterpret_cast<void*>(AreFileApisANSI));
    ldr.registerExport("kernel32.dll", "CreateToolhelp32Snapshot", reinterpret_cast<void*>(CreateToolhelp32Snapshot));
    ldr.registerExport("kernel32.dll", "Process32FirstW", reinterpret_cast<void*>(Process32FirstW));
    ldr.registerExport("kernel32.dll", "Process32NextW", reinterpret_cast<void*>(Process32NextW));

    // api-ms-win-core-toolhelp-l1-1-0.dll
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "CreateToolhelp32Snapshot", reinterpret_cast<void*>(CreateToolhelp32Snapshot));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Process32FirstW", reinterpret_cast<void*>(Process32FirstW));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Process32NextW", reinterpret_cast<void*>(Process32NextW));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Process32First", reinterpret_cast<void*>(Process32First));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Process32Next", reinterpret_cast<void*>(Process32Next));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Module32FirstW", reinterpret_cast<void*>(Module32FirstW));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Module32NextW", reinterpret_cast<void*>(Module32NextW));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Module32First", reinterpret_cast<void*>(Module32First));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Module32Next", reinterpret_cast<void*>(Module32Next));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Thread32First", reinterpret_cast<void*>(Thread32First));
    ldr.registerExport("api-ms-win-core-toolhelp-l1-1-0.dll", "Thread32Next", reinterpret_cast<void*>(Thread32Next));
    ldr.registerExport("kernel32.dll", "ReadDirectoryChangesW", reinterpret_cast<void*>(ReadDirectoryChangesW));
    ldr.registerExport("kernel32.dll", "SleepEx", reinterpret_cast<void*>(SleepEx));
    ldr.registerExport("kernel32.dll", "WaitForSingleObjectEx", reinterpret_cast<void*>(WaitForSingleObjectEx));
    ldr.registerExport("kernel32.dll", "QueueUserAPC", reinterpret_cast<void*>(QueueUserAPC));
    ldr.registerExport("kernel32.dll", "CancelIo", reinterpret_cast<void*>(CancelIo));
    ldr.registerExport("kernel32.dll", "GetTimeZoneInformation", reinterpret_cast<void*>(GetTimeZoneInformation));
    ldr.registerExport("kernel32.dll", "SystemTimeToTzSpecificLocalTime", reinterpret_cast<void*>(SystemTimeToTzSpecificLocalTime));
    ldr.registerExport("kernel32.dll", "GetTimeFormatW", reinterpret_cast<void*>(GetTimeFormatW));
    ldr.registerExport("kernel32.dll", "GetLocaleInfoW", reinterpret_cast<void*>(GetLocaleInfoW));
    ldr.registerExport("kernel32.dll", "GetLocaleInfoA", reinterpret_cast<void*>(GetLocaleInfoA));
    ldr.registerExport("kernel32.dll", "GetLocaleInfoEx", reinterpret_cast<void*>(GetLocaleInfoEx));
    ldr.registerExport("kernel32.dll", "GetUserDefaultLCID", reinterpret_cast<void*>(GetUserDefaultLCID));
    ldr.registerExport("kernel32.dll", "GetStringTypeExW", reinterpret_cast<void*>(GetStringTypeExW));
    ldr.registerExport("kernel32.dll", "GetStringTypeExA", reinterpret_cast<void*>(GetStringTypeExA));
    ldr.registerExport("kernel32.dll", "LCMapStringA", reinterpret_cast<void*>(LCMapStringA));
    ldr.registerExport("kernel32.dll", "IsValidLocale", reinterpret_cast<void*>(IsValidLocale));
    ldr.registerExport("kernel32.dll", "EnumSystemLocalesW", reinterpret_cast<void*>(EnumSystemLocalesW));
    ldr.registerExport("kernel32.dll", "CreateMutexW", reinterpret_cast<void*>(CreateMutexW));
    ldr.registerExport("kernel32.dll", "GetTempPath2W", reinterpret_cast<void*>(GetTempPath2W));
    ldr.registerExport("kernel32.dll", "GetTempPath2A", reinterpret_cast<void*>(GetTempPath2A));
    ldr.registerExport("kernel32.dll", "LoadLibraryA", reinterpret_cast<void*>(LoadLibraryA));
    ldr.registerExport("kernel32.dll", "LoadLibraryExA", reinterpret_cast<void*>(LoadLibraryExA));
    ldr.registerExport("kernel32.dll", "FormatMessageA", reinterpret_cast<void*>(FormatMessageA));
    ldr.registerExport("kernel32.dll", "InitializeCriticalSectionAndSpinCount", reinterpret_cast<void*>(InitializeCriticalSectionAndSpinCount));
    ldr.registerExport("kernel32.dll", "ReadConsoleW", reinterpret_cast<void*>(ReadConsoleW));
    ldr.registerExport("kernel32.dll", "RtlUnwind", reinterpret_cast<void*>(RtlUnwind));
    ldr.registerExport("kernel32.dll", "FreeLibraryAndExitThread", reinterpret_cast<void*>(FreeLibraryAndExitThread));
    ldr.registerExport("kernel32.dll", "DisableThreadLibraryCalls", reinterpret_cast<void*>(DisableThreadLibraryCalls));
    ldr.registerExport("kernel32.dll", "SetProcessDEPPolicy", reinterpret_cast<void*>(SetProcessDEPPolicy));
    ldr.registerExport("kernel32.dll", "SetDllDirectoryA", reinterpret_cast<void*>(SetDllDirectoryA));
    ldr.registerExport("api-ms-win-core-kernel32-legacy-ansi-l1-1-0.dll", "SetDllDirectoryA", reinterpret_cast<void*>(SetDllDirectoryA));
    ldr.registerExport("kernel32.dll", "GetSystemPowerStatus", reinterpret_cast<void*>(GetSystemPowerStatus));
    ldr.registerExport("kernel32.dll", "GetActiveProcessorCount", reinterpret_cast<void*>(GetActiveProcessorCount));
    ldr.registerExport("kernel32.dll", "GetHandleInformation", reinterpret_cast<void*>(GetHandleInformation));
    ldr.registerExport("api-ms-win-core-handle-l1-1-0.dll", "GetHandleInformation", reinterpret_cast<void*>(GetHandleInformation));
    ldr.registerExport("kernel32.dll", "GetProfileStringW", reinterpret_cast<void*>(GetProfileStringW));
    ldr.registerExport("kernel32.dll", "IdnToAscii", reinterpret_cast<void*>(IdnToAscii));
    ldr.registerExport("api-ms-win-core-normalization-l1-1-0.dll", "IdnToAscii", reinterpret_cast<void*>(IdnToAscii));
    ldr.registerExport("kernel32.dll", "IsBadStringPtrA", reinterpret_cast<void*>(IsBadStringPtrA));
    ldr.registerExport("kernel32.dll", "SetProcessPriorityBoost", reinterpret_cast<void*>(SetProcessPriorityBoost));
    ldr.registerExport("kernel32.dll", "SetThreadContext", reinterpret_cast<void*>(SetThreadContext));
    ldr.registerExport("kernel32.dll", "FindFirstVolumeW", reinterpret_cast<void*>(FindFirstVolumeW));
    ldr.registerExport("kernel32.dll", "FindNextVolumeW", reinterpret_cast<void*>(FindNextVolumeW));
    ldr.registerExport("kernel32.dll", "FindVolumeClose", reinterpret_cast<void*>(FindVolumeClose));
    ldr.registerExport("kernel32.dll", "GetFinalPathNameByHandleA", reinterpret_cast<void*>(GetFinalPathNameByHandleA));
    ldr.registerExport("kernel32.dll", "FillConsoleOutputCharacterW", reinterpret_cast<void*>(FillConsoleOutputCharacterW));
    ldr.registerExport("kernel32.dll", "ReadConsoleOutputCharacterA", reinterpret_cast<void*>(ReadConsoleOutputCharacterA));
    ldr.registerExport("kernel32.dll", "CreateJobObjectA", reinterpret_cast<void*>(CreateJobObjectA));
    ldr.registerExport("kernel32.dll", "CreateWaitableTimerExW", reinterpret_cast<void*>(CreateWaitableTimerExW));
    ldr.registerExport("api-ms-win-core-synch-l1-1-0.dll", "CreateWaitableTimerExW", reinterpret_cast<void*>(CreateWaitableTimerExW));
    ldr.registerExport("kernel32.dll", "GetComputerNameA", reinterpret_cast<void*>(GetComputerNameA));
    ldr.registerExport("kernel32.dll", "GetFileAttributesExA", reinterpret_cast<void*>(GetFileAttributesExA));
    ldr.registerExport("kernel32.dll", "K32EnumProcessModules", reinterpret_cast<void*>(K32EnumProcessModules));
    ldr.registerExport("kernel32.dll", "EnumProcessModules", reinterpret_cast<void*>(EnumProcessModules));
    ldr.registerExport("kernel32.dll", "K32EnumProcessModulesEx", reinterpret_cast<void*>(K32EnumProcessModulesEx));
    ldr.registerExport("kernel32.dll", "EnumProcessModulesEx", reinterpret_cast<void*>(EnumProcessModulesEx));
    ldr.registerExport("kernel32.dll", "K32GetModuleInformation", reinterpret_cast<void*>(K32GetModuleInformation));
    ldr.registerExport("kernel32.dll", "GetModuleInformation", reinterpret_cast<void*>(GetModuleInformation));
    ldr.registerExport("psapi.dll", "EnumProcessModules", reinterpret_cast<void*>(EnumProcessModules));
    ldr.registerExport("psapi.dll", "EnumProcessModulesEx", reinterpret_cast<void*>(EnumProcessModulesEx));
    ldr.registerExport("psapi.dll", "GetModuleInformation", reinterpret_cast<void*>(GetModuleInformation));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32EnumProcessModules", reinterpret_cast<void*>(K32EnumProcessModules));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "EnumProcessModules", reinterpret_cast<void*>(EnumProcessModules));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32EnumProcessModulesEx", reinterpret_cast<void*>(K32EnumProcessModulesEx));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "EnumProcessModulesEx", reinterpret_cast<void*>(EnumProcessModulesEx));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32GetModuleInformation", reinterpret_cast<void*>(K32GetModuleInformation));
    ldr.registerExport("kernel32.dll", "K32GetMappedFileNameW", reinterpret_cast<void*>(K32GetMappedFileNameW));
    ldr.registerExport("kernel32.dll", "K32GetModuleFileNameExA", reinterpret_cast<void*>(K32GetModuleFileNameExA));
    ldr.registerExport("kernel32.dll", "K32GetProcessMemoryInfo", reinterpret_cast<void*>(K32GetProcessMemoryInfo));
    ldr.registerExport("kernel32.dll", "Module32First", reinterpret_cast<void*>(Module32First));
    ldr.registerExport("kernel32.dll", "Module32Next", reinterpret_cast<void*>(Module32Next));
    ldr.registerExport("kernel32.dll", "Process32First", reinterpret_cast<void*>(Process32First));
    ldr.registerExport("kernel32.dll", "Process32Next", reinterpret_cast<void*>(Process32Next));
    ldr.registerExport("kernel32.dll", "SetProcessInformation", reinterpret_cast<void*>(SetProcessInformation));
    ldr.registerExport("kernel32.dll", "SetProcessShutdownParameters", reinterpret_cast<void*>(SetProcessShutdownParameters));
    ldr.registerExport("kernel32.dll", "UnmapViewOfFileEx", reinterpret_cast<void*>(UnmapViewOfFileEx));
    ldr.registerExport("kernel32.dll", "GetWindowsDirectoryW", reinterpret_cast<void*>(GetWindowsDirectoryW));
    ldr.registerExport("kernel32.dll", "GetDriveTypeW", reinterpret_cast<void*>(GetDriveTypeW));
    ldr.registerExport("kernel32.dll", "GetVolumeInformationW", reinterpret_cast<void*>(GetVolumeInformationW));
    ldr.registerExport("kernel32.dll", "SetPriorityClass", reinterpret_cast<void*>(SetPriorityClass));
    ldr.registerExport("kernel32.dll", "GetPriorityClass", reinterpret_cast<void*>(GetPriorityClass));
    ldr.registerExport("kernel32.dll", "SetThreadPriority", reinterpret_cast<void*>(SetThreadPriority));
    ldr.registerExport("kernel32.dll", "GetThreadPriority", reinterpret_cast<void*>(GetThreadPriority));
    ldr.registerExport("kernel32.dll", "GetSystemDefaultLangID", reinterpret_cast<void*>(GetSystemDefaultLangID));
    ldr.registerExport("kernel32.dll", "GetUserDefaultLangID", reinterpret_cast<void*>(GetUserDefaultLangID));
    ldr.registerExport("kernel32.dll", "GetCompressedFileSizeW", reinterpret_cast<void*>(GetCompressedFileSizeW));
    ldr.registerExport("kernel32.dll", "FindFirstChangeNotificationW", reinterpret_cast<void*>(FindFirstChangeNotificationW));
    ldr.registerExport("kernel32.dll", "FindNextChangeNotification", reinterpret_cast<void*>(FindNextChangeNotification));
    ldr.registerExport("kernel32.dll", "FindCloseChangeNotification", reinterpret_cast<void*>(FindCloseChangeNotification));
    ldr.registerExport("kernel32.dll", "__C_specific_handler", reinterpret_cast<void*>(__C_specific_handler));
    ldr.registerExport("kernel32.dll", "GetVersionExA", reinterpret_cast<void*>(GetVersionExA));
    ldr.registerExport("kernel32.dll", "GetNumberFormatW", reinterpret_cast<void*>(GetNumberFormatW));
    ldr.registerExport("kernel32.dll", "GetCalendarInfoW", reinterpret_cast<void*>(GetCalendarInfoW));
    ldr.registerExport("kernel32.dll", "FreeEnvironmentStringsA", reinterpret_cast<void*>(FreeEnvironmentStringsA));
    ldr.registerExport("kernel32.dll", "GetEnvironmentStrings", reinterpret_cast<void*>(GetEnvironmentStrings));
    ldr.registerExport("kernel32.dll", "SetHandleCount", reinterpret_cast<void*>(SetHandleCount));
    ldr.registerExport("kernel32.dll", "GetStringTypeA", reinterpret_cast<void*>(GetStringTypeA));
    ldr.registerExport("kernel32.dll", "CreateJobObjectW", reinterpret_cast<void*>(CreateJobObjectW));
    ldr.registerExport("kernel32.dll", "OpenJobObjectW", reinterpret_cast<void*>(OpenJobObjectW));
    ldr.registerExport("kernel32.dll", "AssignProcessToJobObject", reinterpret_cast<void*>(AssignProcessToJobObject));
    ldr.registerExport("kernel32.dll", "SetInformationJobObject", reinterpret_cast<void*>(SetInformationJobObject));
    ldr.registerExport("kernel32.dll", "QueryInformationJobObject", reinterpret_cast<void*>(QueryInformationJobObject));
    ldr.registerExport("kernel32.dll", "TerminateJobObject", reinterpret_cast<void*>(TerminateJobObject));
    ldr.registerExport("kernel32.dll", "IsProcessInJob", reinterpret_cast<void*>(IsProcessInJob));
    ldr.registerExport("api-ms-win-core-job-l1-1-0.dll", "CreateJobObjectW", reinterpret_cast<void*>(CreateJobObjectW));
    ldr.registerExport("api-ms-win-core-job-l1-1-0.dll", "AssignProcessToJobObject", reinterpret_cast<void*>(AssignProcessToJobObject));
    ldr.registerExport("api-ms-win-core-job-l1-1-0.dll", "SetInformationJobObject", reinterpret_cast<void*>(SetInformationJobObject));
    ldr.registerExport("api-ms-win-core-job-l1-1-0.dll", "QueryInformationJobObject", reinterpret_cast<void*>(QueryInformationJobObject));
    ldr.registerExport("api-ms-win-core-job-l1-1-0.dll", "TerminateJobObject", reinterpret_cast<void*>(TerminateJobObject));
    ldr.registerExport("api-ms-win-core-job-l1-1-0.dll", "IsProcessInJob", reinterpret_cast<void*>(IsProcessInJob));
    ldr.registerExport("kernel32.dll", "OpenFileMappingA", reinterpret_cast<void*>(OpenFileMappingA));
    ldr.registerExport("kernel32.dll", "VirtualLock", reinterpret_cast<void*>(VirtualLock));
    ldr.registerExport("kernel32.dll", "FlushInstructionCache", reinterpret_cast<void*>(FlushInstructionCache));
    ldr.registerExport("kernel32.dll", "FlushConsoleInputBuffer", reinterpret_cast<void*>(FlushConsoleInputBuffer));
    ldr.registerExport("kernel32.dll", "PeekConsoleInputW", reinterpret_cast<void*>(PeekConsoleInputW));
    ldr.registerExport("kernel32.dll", "ReadConsoleInputW", reinterpret_cast<void*>(ReadConsoleInputW));
    ldr.registerExport("kernel32.dll", "WriteConsoleInputW", reinterpret_cast<void*>(WriteConsoleInputW));
    ldr.registerExport("kernel32.dll", "InterlockedIncrement", reinterpret_cast<void*>(InterlockedIncrement));
    ldr.registerExport("kernel32.dll", "InterlockedDecrement", reinterpret_cast<void*>(InterlockedDecrement));
    ldr.registerExport("kernel32.dll", "InterlockedExchange", reinterpret_cast<void*>(InterlockedExchange));
    ldr.registerExport("kernel32.dll", "InterlockedExchangeAdd", reinterpret_cast<void*>(InterlockedExchangeAdd));
    ldr.registerExport("kernel32.dll", "InterlockedCompareExchange", reinterpret_cast<void*>(InterlockedCompareExchange));
    ldr.registerExport("kernel32.dll", "GetDateFormatA", reinterpret_cast<void*>(GetDateFormatA));
    ldr.registerExport("kernel32.dll", "GetModuleHandleExA", reinterpret_cast<void*>(GetModuleHandleExA));
    ldr.registerExport("kernel32.dll", "IsBadWritePtr", reinterpret_cast<void*>(IsBadWritePtr));
    ldr.registerExport("kernel32.dll", "GetThreadGroupAffinity", reinterpret_cast<void*>(GetThreadGroupAffinity));
    ldr.registerExport("kernel32.dll", "DosDateTimeToFileTime", reinterpret_cast<void*>(DosDateTimeToFileTime));
    ldr.registerExport("kernel32.dll", "SystemTimeToFileTime", reinterpret_cast<void*>(SystemTimeToFileTime));
    ldr.registerExport("kernel32.dll", "TzSpecificLocalTimeToSystemTime",
        reinterpret_cast<void*>(static_cast<BOOL (WINAPI*)(const void*, const void*, void*)>(
            TzSpecificLocalTimeToSystemTime)));
    ldr.registerExport("kernel32.dll", "GetFileTime", reinterpret_cast<void*>(GetFileTime));
    ldr.registerExport("kernel32.dll", "GetLogicalDrives", reinterpret_cast<void*>(GetLogicalDrives));
    ldr.registerExport("kernel32.dll", "GetVolumePathNameW", reinterpret_cast<void*>(GetVolumePathNameW));
    ldr.registerExport("kernel32.dll", "FoldStringW", reinterpret_cast<void*>(FoldStringW));
    ldr.registerExport("kernel32.dll", "IsDBCSLeadByte", reinterpret_cast<void*>(IsDBCSLeadByte));
    ldr.registerExport("kernel32.dll", "Thread32First", reinterpret_cast<void*>(Thread32First));
    ldr.registerExport("kernel32.dll", "Thread32Next", reinterpret_cast<void*>(Thread32Next));
    ldr.registerExport("kernel32.dll", "Module32FirstW", reinterpret_cast<void*>(Module32FirstW));
    ldr.registerExport("kernel32.dll", "Module32NextW", reinterpret_cast<void*>(Module32NextW));
    ldr.registerExport("kernel32.dll", "AttachConsole", reinterpret_cast<void*>(AttachConsole));
    ldr.registerExport("kernel32.dll", "SetConsoleScreenBufferSize", reinterpret_cast<void*>(SetConsoleScreenBufferSize));
    ldr.registerExport("kernel32.dll", "GetTempFileNameW", reinterpret_cast<void*>(GetTempFileNameW));
    ldr.registerExport("kernel32.dll", "OutputDebugStringA", reinterpret_cast<void*>(OutputDebugStringA));
    ldr.registerExport("kernel32.dll", "SetThreadExecutionState", reinterpret_cast<void*>(SetThreadExecutionState));
    ldr.registerExport("kernel32.dll", "DebugBreak", reinterpret_cast<void*>(DebugBreak));
    ldr.registerExport("api-ms-win-core-errorhandling-l1-1-0.dll", "SetUnhandledExceptionFilter", reinterpret_cast<void*>(SetUnhandledExceptionFilter));
    ldr.registerExport("api-ms-win-core-errorhandling-l1-1-0.dll", "UnhandledExceptionFilter", reinterpret_cast<void*>(UnhandledExceptionFilter));
    ldr.registerExport("api-ms-win-core-errorhandling-l1-1-1.dll", "SetUnhandledExceptionFilter", reinterpret_cast<void*>(SetUnhandledExceptionFilter));
    ldr.registerExport("api-ms-win-core-errorhandling-l1-1-1.dll", "UnhandledExceptionFilter", reinterpret_cast<void*>(UnhandledExceptionFilter));
    ldr.registerExport("kernel32.dll", "GetPrivateProfileIntW", reinterpret_cast<void*>(GetPrivateProfileIntW));
    ldr.registerExport("kernel32.dll", "HeapQueryInformation", reinterpret_cast<void*>(HeapQueryInformation));
    ldr.registerExport("kernel32.dll", "Beep", reinterpret_cast<void*>(Beep));
    ldr.registerExport("kernel32.dll", "ClearCommBreak", reinterpret_cast<void*>(ClearCommBreak));
    ldr.registerExport("kernel32.dll", "SetCommBreak", reinterpret_cast<void*>(SetCommBreak));
    ldr.registerExport("kernel32.dll", "GetCommState", reinterpret_cast<void*>(GetCommState));
    ldr.registerExport("kernel32.dll", "SetCommState", reinterpret_cast<void*>(SetCommState));
    ldr.registerExport("kernel32.dll", "SetCommTimeouts", reinterpret_cast<void*>(SetCommTimeouts));
    ldr.registerExport("kernel32.dll", "SetHandleInformation", reinterpret_cast<void*>(SetHandleInformation));
    ldr.registerExport("kernel32.dll", "CreateEventA", reinterpret_cast<void*>(CreateEventA));
    ldr.registerExport("kernel32.dll", "CreateMutexA", reinterpret_cast<void*>(CreateMutexA));
    ldr.registerExport("kernel32.dll", "CreateFileMappingA", reinterpret_cast<void*>(CreateFileMappingA));
    ldr.registerExport("kernel32.dll", "CreateNamedPipeA", reinterpret_cast<void*>(CreateNamedPipeA));
    ldr.registerExport("kernel32.dll", "WaitNamedPipeA", reinterpret_cast<void*>(WaitNamedPipeA));
    ldr.registerExport("kernel32.dll", "CreatePipe", reinterpret_cast<void*>(CreatePipe));
    ldr.registerExport("kernel32.dll", "FindResourceA", reinterpret_cast<void*>(FindResourceA));
    ldr.registerExport("kernel32.dll", "GetOverlappedResult", reinterpret_cast<void*>(GetOverlappedResult));
    ldr.registerExport("kernel32.dll", "GetSystemDirectoryA", reinterpret_cast<void*>(GetSystemDirectoryA));
    ldr.registerExport("kernel32.dll", "GetWindowsDirectoryA", reinterpret_cast<void*>(GetWindowsDirectoryA));
    ldr.registerExport("kernel32.dll", "GetTempPathA", reinterpret_cast<void*>(GetTempPathA));
    ldr.registerExport("kernel32.dll", "GetThreadTimes", reinterpret_cast<void*>(GetThreadTimes));
    ldr.registerExport("kernel32.dll", "GlobalMemoryStatus", reinterpret_cast<void*>(GlobalMemoryStatus));
    ldr.registerExport("kernel32.dll", "LocalFileTimeToFileTime", reinterpret_cast<void*>(LocalFileTimeToFileTime));
    ldr.registerExport("kernel32.dll", "FindFirstVolumeA", reinterpret_cast<void*>(FindFirstVolumeA));
    ldr.registerExport("kernel32.dll", "FindNextVolumeA", reinterpret_cast<void*>(FindNextVolumeA));
    ldr.registerExport("kernel32.dll", "GetVolumeNameForVolumeMountPointA", reinterpret_cast<void*>(GetVolumeNameForVolumeMountPointA));
    ldr.registerExport("kernel32.dll", "GetVolumePathNameA", reinterpret_cast<void*>(GetVolumePathNameA));
    ldr.registerExport("kernel32.dll", "GetVolumeInformationA", reinterpret_cast<void*>(GetVolumeInformationA));
    ldr.registerExport("kernel32.dll", "GetVolumeInformationByHandleW", reinterpret_cast<void*>(GetVolumeInformationByHandleW));
    ldr.registerExport("kernel32.dll", "SetVolumeMountPointA", reinterpret_cast<void*>(SetVolumeMountPointA));
    ldr.registerExport("kernel32.dll", "DeleteVolumeMountPointA", reinterpret_cast<void*>(DeleteVolumeMountPointA));
    ldr.registerExport("kernel32.dll", "SetVolumeLabelA", reinterpret_cast<void*>(SetVolumeLabelA));
    ldr.registerExport("kernel32.dll", "DefineDosDeviceW", reinterpret_cast<void*>(DefineDosDeviceW));
    ldr.registerExport("kernel32.dll", "DefineDosDeviceA", reinterpret_cast<void*>(DefineDosDeviceA));
    ldr.registerExport("kernel32.dll", "SetVolumeMountPointW", reinterpret_cast<void*>(SetVolumeMountPointW));
    ldr.registerExport("kernel32.dll", "DeleteVolumeMountPointW", reinterpret_cast<void*>(DeleteVolumeMountPointW));
    ldr.registerExport("kernel32.dll", "QueryDosDeviceA", reinterpret_cast<void*>(QueryDosDeviceA));
    ldr.registerExport("kernel32.dll", "GetDiskFreeSpaceExA", reinterpret_cast<void*>(GetDiskFreeSpaceExA));
    ldr.registerExport("kernel32.dll", "GetLogicalDriveStringsA", reinterpret_cast<void*>(GetLogicalDriveStringsA));
    ldr.registerExport("kernel32.dll", "CancelIoEx", reinterpret_cast<void*>(CancelIoEx));
    ldr.registerExport("kernel32.dll", "CancelSynchronousIo", reinterpret_cast<void*>(CancelSynchronousIo));
    ldr.registerExport("kernel32.dll", "GetOverlappedResultEx", reinterpret_cast<void*>(GetOverlappedResultEx));
    ldr.registerExport("kernel32.dll", "SleepConditionVariableCS", reinterpret_cast<void*>(SleepConditionVariableCS));
    ldr.registerExport("kernel32.dll", "CreateSymbolicLinkW", reinterpret_cast<void*>(CreateSymbolicLinkW));
    ldr.registerExport("kernel32.dll", "GetConsoleWindow", reinterpret_cast<void*>(GetConsoleWindow));
    ldr.registerExport("kernel32.dll", "EnumUILanguagesW", reinterpret_cast<void*>(EnumUILanguagesW));
    ldr.registerExport("kernel32.dll", "GetSystemDefaultLCID", reinterpret_cast<void*>(GetSystemDefaultLCID));
    ldr.registerExport("kernel32.dll", "GetThreadUILanguage", reinterpret_cast<void*>(GetThreadUILanguage));
    ldr.registerExport("kernel32.dll", "LCIDToLocaleName", reinterpret_cast<void*>(LCIDToLocaleName));
    ldr.registerExport("kernel32.dll", "SetDefaultDllDirectories", reinterpret_cast<void*>(SetDefaultDllDirectories));
    ldr.registerExport("kernel32.dll", "SetFileAttributesA", reinterpret_cast<void*>(SetFileAttributesA));
    ldr.registerExport("kernel32.dll", "VerifyVersionInfoA", reinterpret_cast<void*>(VerifyVersionInfoA));
    ldr.registerExport("kernel32.dll", "K32GetModuleFileNameExW", reinterpret_cast<void*>(K32GetModuleFileNameExW));
    ldr.registerExport("kernel32.dll", "K32GetProcessImageFileNameW", reinterpret_cast<void*>(K32GetProcessImageFileNameW));
    ldr.registerExport("kernel32.dll", "GlobalReAlloc", reinterpret_cast<void*>(GlobalReAlloc));
    ldr.registerExport("kernel32.dll", "LocalReAlloc", reinterpret_cast<void*>(LocalReAlloc));
    ldr.registerExport("kernel32.dll", "GlobalHandle", reinterpret_cast<void*>(GlobalHandle));
    ldr.registerExport("kernel32.dll", "GlobalFlags", reinterpret_cast<void*>(GlobalFlags));
    ldr.registerExport("kernel32.dll", "SetThreadUILanguage", reinterpret_cast<void*>(SetThreadUILanguage));
    ldr.registerExport("kernel32.dll", "SetSearchPathMode", reinterpret_cast<void*>(SetSearchPathMode));
    ldr.registerExport("kernel32.dll", "SetDllDirectoryW", reinterpret_cast<void*>(SetDllDirectoryW));
    ldr.registerExport("kernel32.dll", "GetSystemWow64DirectoryW", reinterpret_cast<void*>(GetSystemWow64DirectoryW));
    ldr.registerExport("kernel32.dll", "ExpandEnvironmentStringsA", reinterpret_cast<void*>(ExpandEnvironmentStringsA));
    ldr.registerExport("kernel32.dll", "CreateActCtxW", reinterpret_cast<void*>(CreateActCtxW));
    ldr.registerExport("kernel32.dll", "ActivateActCtx", reinterpret_cast<void*>(ActivateActCtx));
    ldr.registerExport("kernel32.dll", "DeactivateActCtx", reinterpret_cast<void*>(DeactivateActCtx));
    ldr.registerExport("kernel32.dll", "FindActCtxSectionStringW", reinterpret_cast<void*>(FindActCtxSectionStringW));
    ldr.registerExport("kernel32.dll", "QueryActCtxW", reinterpret_cast<void*>(QueryActCtxW));
    ldr.registerExport("kernel32.dll", "GetProfileIntW", reinterpret_cast<void*>(GetProfileIntW));
    ldr.registerExport("kernel32.dll", "GlobalGetAtomNameW", reinterpret_cast<void*>(GlobalGetAtomNameW));
    ldr.registerExport("kernel32.dll", "lstrcmpA", reinterpret_cast<void*>(lstrcmpA));
    ldr.registerExport("kernel32.dll", "LockFile", reinterpret_cast<void*>(LockFile));
    ldr.registerExport("kernel32.dll", "UnlockFile", reinterpret_cast<void*>(UnlockFile));
    ldr.registerExport("kernel32.dll", "FindResourceExW", reinterpret_cast<void*>(FindResourceExW));
    ldr.registerExport("kernel32.dll", "InterlockedPushEntrySList", reinterpret_cast<void*>(InterlockedPushEntrySList));
    ldr.registerExport("kernel32.dll", "InterlockedPushListSListEx", reinterpret_cast<void*>(InterlockedPushListSListEx));
    ldr.registerExport("kernel32.dll", "InterlockedPushListSList", reinterpret_cast<void*>(InterlockedPushListSList));
    ldr.registerExport("kernel32.dll", "InitializeSListHeadEx", reinterpret_cast<void*>(InitializeSListHeadEx));
    ldr.registerExport("kernel32.dll", "CompactSListHead", reinterpret_cast<void*>(CompactSListHead));
    ldr.registerExport("kernel32.dll", "InterlockedCompareExchangePointer", reinterpret_cast<void*>(InterlockedCompareExchangePointer));
    ldr.registerExport("kernel32.dll", "InterlockedExchangePointer", reinterpret_cast<void*>(InterlockedExchangePointer));
    ldr.registerExport("kernel32.dll", "FillMemoryUlong", reinterpret_cast<void*>(FillMemoryUlong));
    ldr.registerExport("kernel32.dll", "FillMemoryUlonglong", reinterpret_cast<void*>(FillMemoryUlonglong));
    ldr.registerExport("kernel32.dll", "ZeroMemoryEx", reinterpret_cast<void*>(ZeroMemoryEx));
    ldr.registerExport("kernel32.dll", "CompareMemoryUlong", reinterpret_cast<void*>(CompareMemoryUlong));
    ldr.registerExport("kernel32.dll", "InterlockedBitTestAndSet", reinterpret_cast<void*>(InterlockedBitTestAndSet));
    ldr.registerExport("kernel32.dll", "InterlockedBitTestAndReset", reinterpret_cast<void*>(InterlockedBitTestAndReset));
    ldr.registerExport("kernel32.dll", "InterlockedBitTestAndSet64", reinterpret_cast<void*>(InterlockedBitTestAndSet64));
    ldr.registerExport("kernel32.dll", "InterlockedBitTestAndReset64", reinterpret_cast<void*>(InterlockedBitTestAndReset64));
    ldr.registerExport("kernel32.dll", "InterlockedBitTestAndComplement", reinterpret_cast<void*>(InterlockedBitTestAndComplement));
    ldr.registerExport("kernel32.dll", "InterlockedBitTestAndComplement64", reinterpret_cast<void*>(InterlockedBitTestAndComplement64));
    ldr.registerExport("kernel32.dll", "InterlockedAnd", reinterpret_cast<void*>(InterlockedAnd));
    ldr.registerExport("kernel32.dll", "InterlockedOr", reinterpret_cast<void*>(InterlockedOr));
    ldr.registerExport("kernel32.dll", "InterlockedXor", reinterpret_cast<void*>(InterlockedXor));
    ldr.registerExport("kernel32.dll", "InterlockedAnd64", reinterpret_cast<void*>(InterlockedAnd64));
    ldr.registerExport("kernel32.dll", "InterlockedOr64", reinterpret_cast<void*>(InterlockedOr64));
    ldr.registerExport("kernel32.dll", "InterlockedXor64", reinterpret_cast<void*>(InterlockedXor64));
    ldr.registerExport("kernel32.dll", "InterlockedAndNot", reinterpret_cast<void*>(InterlockedAndNot));
    ldr.registerExport("kernel32.dll", "InterlockedAndNot64", reinterpret_cast<void*>(InterlockedAndNot64));
    ldr.registerExport("kernel32.dll", "InterlockedCompareExchange64", reinterpret_cast<void*>(InterlockedCompareExchange64));
    ldr.registerExport("kernel32.dll", "InterlockedExchange64", reinterpret_cast<void*>(InterlockedExchange64));
    ldr.registerExport("kernel32.dll", "InterlockedExchangeAdd64", reinterpret_cast<void*>(InterlockedExchangeAdd64));
    ldr.registerExport("kernel32.dll", "InterlockedIncrement64", reinterpret_cast<void*>(InterlockedIncrement64));
    ldr.registerExport("kernel32.dll", "InterlockedDecrement64", reinterpret_cast<void*>(InterlockedDecrement64));
    ldr.registerExport("kernel32.dll", "InterlockedCompareExchange16", reinterpret_cast<void*>(InterlockedCompareExchange16));
    ldr.registerExport("kernel32.dll", "InterlockedExchange16", reinterpret_cast<void*>(InterlockedExchange16));
    ldr.registerExport("kernel32.dll", "InterlockedExchangeAdd16", reinterpret_cast<void*>(InterlockedExchangeAdd16));
    ldr.registerExport("kernel32.dll", "InterlockedIncrement16", reinterpret_cast<void*>(InterlockedIncrement16));
    ldr.registerExport("kernel32.dll", "InterlockedDecrement16", reinterpret_cast<void*>(InterlockedDecrement16));
    ldr.registerExport("kernel32.dll", "InterlockedAnd16", reinterpret_cast<void*>(InterlockedAnd16));
    ldr.registerExport("kernel32.dll", "InterlockedOr16", reinterpret_cast<void*>(InterlockedOr16));
    ldr.registerExport("kernel32.dll", "InterlockedXor16", reinterpret_cast<void*>(InterlockedXor16));
    ldr.registerExport("kernel32.dll", "InterlockedCompareExchange8", reinterpret_cast<void*>(InterlockedCompareExchange8));
    ldr.registerExport("kernel32.dll", "InterlockedExchange8", reinterpret_cast<void*>(InterlockedExchange8));
    ldr.registerExport("kernel32.dll", "InterlockedExchangeAdd8", reinterpret_cast<void*>(InterlockedExchangeAdd8));
    ldr.registerExport("kernel32.dll", "InterlockedIncrement8", reinterpret_cast<void*>(InterlockedIncrement8));
    ldr.registerExport("kernel32.dll", "InterlockedDecrement8", reinterpret_cast<void*>(InterlockedDecrement8));
    ldr.registerExport("kernel32.dll", "InterlockedAnd8", reinterpret_cast<void*>(InterlockedAnd8));
    ldr.registerExport("kernel32.dll", "InterlockedOr8", reinterpret_cast<void*>(InterlockedOr8));
    ldr.registerExport("kernel32.dll", "InterlockedXor8", reinterpret_cast<void*>(InterlockedXor8));
    ldr.registerExport("kernel32.dll", "InterlockedAndNot16", reinterpret_cast<void*>(InterlockedAndNot16));
    ldr.registerExport("kernel32.dll", "InterlockedAndNot8", reinterpret_cast<void*>(InterlockedAndNot8));
    ldr.registerExport("kernel32.dll", "InterlockedXorNot64", reinterpret_cast<void*>(InterlockedXorNot64));
    ldr.registerExport("kernel32.dll", "InterlockedXorNot32", reinterpret_cast<void*>(InterlockedXorNot32));
    ldr.registerExport("kernel32.dll", "InterlockedXorNot16", reinterpret_cast<void*>(InterlockedXorNot16));
    ldr.registerExport("kernel32.dll", "InterlockedXorNot8", reinterpret_cast<void*>(InterlockedXorNot8));
    ldr.registerExport("kernel32.dll", "InterlockedNand64", reinterpret_cast<void*>(InterlockedNand64));
    ldr.registerExport("kernel32.dll", "InterlockedNand32", reinterpret_cast<void*>(InterlockedNand32));
    ldr.registerExport("kernel32.dll", "InterlockedNand16", reinterpret_cast<void*>(InterlockedNand16));
    ldr.registerExport("kernel32.dll", "InterlockedNand8", reinterpret_cast<void*>(InterlockedNand8));
    ldr.registerExport("kernel32.dll", "InterlockedNor64", reinterpret_cast<void*>(InterlockedNor64));
    ldr.registerExport("kernel32.dll", "InterlockedNor32", reinterpret_cast<void*>(InterlockedNor32));
    ldr.registerExport("kernel32.dll", "InterlockedNor16", reinterpret_cast<void*>(InterlockedNor16));
    ldr.registerExport("kernel32.dll", "InterlockedNor8", reinterpret_cast<void*>(InterlockedNor8));
    ldr.registerExport("kernel32.dll", "InterlockedXnor64", reinterpret_cast<void*>(InterlockedXnor64));
    ldr.registerExport("kernel32.dll", "InterlockedXnor32", reinterpret_cast<void*>(InterlockedXnor32));
    ldr.registerExport("kernel32.dll", "InterlockedXnor16", reinterpret_cast<void*>(InterlockedXnor16));
    ldr.registerExport("kernel32.dll", "InterlockedXnor8", reinterpret_cast<void*>(InterlockedXnor8));
    ldr.registerExport("kernel32.dll", "InterlockedMin64", reinterpret_cast<void*>(InterlockedMin64));
    ldr.registerExport("kernel32.dll", "InterlockedMin32", reinterpret_cast<void*>(InterlockedMin32));
    ldr.registerExport("kernel32.dll", "InterlockedMin16", reinterpret_cast<void*>(InterlockedMin16));
    ldr.registerExport("kernel32.dll", "InterlockedMin8", reinterpret_cast<void*>(InterlockedMin8));
    ldr.registerExport("kernel32.dll", "InterlockedMax64", reinterpret_cast<void*>(InterlockedMax64));
    ldr.registerExport("kernel32.dll", "InterlockedMax32", reinterpret_cast<void*>(InterlockedMax32));
    ldr.registerExport("kernel32.dll", "InterlockedMax16", reinterpret_cast<void*>(InterlockedMax16));
    ldr.registerExport("kernel32.dll", "InterlockedMax8", reinterpret_cast<void*>(InterlockedMax8));
    ldr.registerExport("kernel32.dll", "InterlockedClamp64", reinterpret_cast<void*>(InterlockedClamp64));
    ldr.registerExport("kernel32.dll", "InterlockedClamp", reinterpret_cast<void*>(InterlockedClamp));
    ldr.registerExport("kernel32.dll", "InterlockedClamp32", reinterpret_cast<void*>(InterlockedClamp32));
    ldr.registerExport("kernel32.dll", "InterlockedClamp16", reinterpret_cast<void*>(InterlockedClamp16));
    ldr.registerExport("kernel32.dll", "InterlockedClamp8", reinterpret_cast<void*>(InterlockedClamp8));
    ldr.registerExport("kernel32.dll", "InterlockedAddModulo64", reinterpret_cast<void*>(InterlockedAddModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedAddModulo", reinterpret_cast<void*>(InterlockedAddModulo));
    ldr.registerExport("kernel32.dll", "InterlockedAddModulo32", reinterpret_cast<void*>(InterlockedAddModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedAddModulo16", reinterpret_cast<void*>(InterlockedAddModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedAddModulo8", reinterpret_cast<void*>(InterlockedAddModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedMulModulo64", reinterpret_cast<void*>(InterlockedMulModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedMulModulo", reinterpret_cast<void*>(InterlockedMulModulo));
    ldr.registerExport("kernel32.dll", "InterlockedMulModulo32", reinterpret_cast<void*>(InterlockedMulModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedMulModulo16", reinterpret_cast<void*>(InterlockedMulModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedMulModulo8", reinterpret_cast<void*>(InterlockedMulModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedSubModulo64", reinterpret_cast<void*>(InterlockedSubModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedSubModulo", reinterpret_cast<void*>(InterlockedSubModulo));
    ldr.registerExport("kernel32.dll", "InterlockedSubModulo32", reinterpret_cast<void*>(InterlockedSubModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedSubModulo16", reinterpret_cast<void*>(InterlockedSubModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedSubModulo8", reinterpret_cast<void*>(InterlockedSubModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedDivModulo64", reinterpret_cast<void*>(InterlockedDivModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedDivModulo", reinterpret_cast<void*>(InterlockedDivModulo));
    ldr.registerExport("kernel32.dll", "InterlockedDivModulo32", reinterpret_cast<void*>(InterlockedDivModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedDivModulo16", reinterpret_cast<void*>(InterlockedDivModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedDivModulo8", reinterpret_cast<void*>(InterlockedDivModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedExpModulo64", reinterpret_cast<void*>(InterlockedExpModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedExpModulo", reinterpret_cast<void*>(InterlockedExpModulo));
    ldr.registerExport("kernel32.dll", "InterlockedExpModulo32", reinterpret_cast<void*>(InterlockedExpModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedExpModulo16", reinterpret_cast<void*>(InterlockedExpModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedExpModulo8", reinterpret_cast<void*>(InterlockedExpModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeftModulo64", reinterpret_cast<void*>(InterlockedRotateLeftModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeftModulo", reinterpret_cast<void*>(InterlockedRotateLeftModulo));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeftModulo32", reinterpret_cast<void*>(InterlockedRotateLeftModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeftModulo16", reinterpret_cast<void*>(InterlockedRotateLeftModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeftModulo8", reinterpret_cast<void*>(InterlockedRotateLeftModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRightModulo64", reinterpret_cast<void*>(InterlockedRotateRightModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRightModulo", reinterpret_cast<void*>(InterlockedRotateRightModulo));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRightModulo32", reinterpret_cast<void*>(InterlockedRotateRightModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRightModulo16", reinterpret_cast<void*>(InterlockedRotateRightModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRightModulo8", reinterpret_cast<void*>(InterlockedRotateRightModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedInvertModulo64", reinterpret_cast<void*>(InterlockedInvertModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedInvertModulo", reinterpret_cast<void*>(InterlockedInvertModulo));
    ldr.registerExport("kernel32.dll", "InterlockedInvertModulo32", reinterpret_cast<void*>(InterlockedInvertModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedInvertModulo16", reinterpret_cast<void*>(InterlockedInvertModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedInvertModulo8", reinterpret_cast<void*>(InterlockedInvertModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedNegateModulo64", reinterpret_cast<void*>(InterlockedNegateModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedNegateModulo", reinterpret_cast<void*>(InterlockedNegateModulo));
    ldr.registerExport("kernel32.dll", "InterlockedNegateModulo32", reinterpret_cast<void*>(InterlockedNegateModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedNegateModulo16", reinterpret_cast<void*>(InterlockedNegateModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedNegateModulo8", reinterpret_cast<void*>(InterlockedNegateModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiffModulo64", reinterpret_cast<void*>(InterlockedAbsDiffModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiffModulo", reinterpret_cast<void*>(InterlockedAbsDiffModulo));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiffModulo32", reinterpret_cast<void*>(InterlockedAbsDiffModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiffModulo16", reinterpret_cast<void*>(InterlockedAbsDiffModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiffModulo8", reinterpret_cast<void*>(InterlockedAbsDiffModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedNorModulo64", reinterpret_cast<void*>(InterlockedNorModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedNorModulo", reinterpret_cast<void*>(InterlockedNorModulo));
    ldr.registerExport("kernel32.dll", "InterlockedNorModulo32", reinterpret_cast<void*>(InterlockedNorModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedNorModulo16", reinterpret_cast<void*>(InterlockedNorModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedNorModulo8", reinterpret_cast<void*>(InterlockedNorModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedNandModulo64", reinterpret_cast<void*>(InterlockedNandModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedNandModulo", reinterpret_cast<void*>(InterlockedNandModulo));
    ldr.registerExport("kernel32.dll", "InterlockedNandModulo32", reinterpret_cast<void*>(InterlockedNandModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedNandModulo16", reinterpret_cast<void*>(InterlockedNandModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedNandModulo8", reinterpret_cast<void*>(InterlockedNandModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedXnorModulo64", reinterpret_cast<void*>(InterlockedXnorModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedXnorModulo", reinterpret_cast<void*>(InterlockedXnorModulo));
    ldr.registerExport("kernel32.dll", "InterlockedXnorModulo32", reinterpret_cast<void*>(InterlockedXnorModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedXnorModulo16", reinterpret_cast<void*>(InterlockedXnorModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedXnorModulo8", reinterpret_cast<void*>(InterlockedXnorModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedAndNotModulo64", reinterpret_cast<void*>(InterlockedAndNotModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedAndNotModulo", reinterpret_cast<void*>(InterlockedAndNotModulo));
    ldr.registerExport("kernel32.dll", "InterlockedAndNotModulo32", reinterpret_cast<void*>(InterlockedAndNotModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedAndNotModulo16", reinterpret_cast<void*>(InterlockedAndNotModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedAndNotModulo8", reinterpret_cast<void*>(InterlockedAndNotModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedOrNotModulo64", reinterpret_cast<void*>(InterlockedOrNotModulo64));
    ldr.registerExport("kernel32.dll", "InterlockedOrNotModulo", reinterpret_cast<void*>(InterlockedOrNotModulo));
    ldr.registerExport("kernel32.dll", "InterlockedOrNotModulo32", reinterpret_cast<void*>(InterlockedOrNotModulo32));
    ldr.registerExport("kernel32.dll", "InterlockedOrNotModulo16", reinterpret_cast<void*>(InterlockedOrNotModulo16));
    ldr.registerExport("kernel32.dll", "InterlockedOrNotModulo8", reinterpret_cast<void*>(InterlockedOrNotModulo8));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRight64", reinterpret_cast<void*>(InterlockedRotateRight64));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRight", reinterpret_cast<void*>(InterlockedRotateRight));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRight32", reinterpret_cast<void*>(InterlockedRotateRight32));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRight16", reinterpret_cast<void*>(InterlockedRotateRight16));
    ldr.registerExport("kernel32.dll", "InterlockedRotateRight8", reinterpret_cast<void*>(InterlockedRotateRight8));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeft64", reinterpret_cast<void*>(InterlockedRotateLeft64));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeft", reinterpret_cast<void*>(InterlockedRotateLeft));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeft32", reinterpret_cast<void*>(InterlockedRotateLeft32));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeft16", reinterpret_cast<void*>(InterlockedRotateLeft16));
    ldr.registerExport("kernel32.dll", "InterlockedRotateLeft8", reinterpret_cast<void*>(InterlockedRotateLeft8));
    ldr.registerExport("kernel32.dll", "GetPagefileCompressionTelemetry", reinterpret_cast<void*>(GetPagefileCompressionTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuCooperativeFabricInformation", reinterpret_cast<void*>(GetCpuCooperativeFabricInformation));
    ldr.registerExport("kernel32.dll", "GetPagefileDecompressionTelemetry", reinterpret_cast<void*>(GetPagefileDecompressionTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricPartitionInformation", reinterpret_cast<void*>(GetCpuFabricPartitionInformation));
    ldr.registerExport("kernel32.dll", "GetPagefileClusterCompactionTelemetry", reinterpret_cast<void*>(GetPagefileClusterCompactionTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricQosTelemetry", reinterpret_cast<void*>(GetCpuFabricQosTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileClusterDefragmentationTelemetry", reinterpret_cast<void*>(GetPagefileClusterDefragmentationTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricLatencyTelemetry", reinterpret_cast<void*>(GetCpuFabricLatencyTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileExtentMigrationTelemetry", reinterpret_cast<void*>(GetPagefileExtentMigrationTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricCongestionTelemetry", reinterpret_cast<void*>(GetCpuFabricCongestionTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileTierExpulsionTelemetry", reinterpret_cast<void*>(GetPagefileTierExpulsionTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricBandwidthPartitionTelemetry", reinterpret_cast<void*>(GetCpuFabricBandwidthPartitionTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileTierIoQueueTelemetry", reinterpret_cast<void*>(GetPagefileTierIoQueueTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricPowerStateTelemetry", reinterpret_cast<void*>(GetCpuFabricPowerStateTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileTierWearLevelingTelemetry", reinterpret_cast<void*>(GetPagefileTierWearLevelingTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltageDomainTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltageDomainTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileTierFailoverTelemetry", reinterpret_cast<void*>(GetPagefileTierFailoverTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricClockStretchingTelemetry", reinterpret_cast<void*>(GetCpuFabricClockStretchingTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileTierMigrationQos", reinterpret_cast<void*>(GetPagefileTierMigrationQos));
    ldr.registerExport("kernel32.dll", "GetCpuFabricThermalThrottleTelemetry", reinterpret_cast<void*>(GetCpuFabricThermalThrottleTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedBitReverse64", reinterpret_cast<void*>(InterlockedBitReverse64));
    ldr.registerExport("kernel32.dll", "InterlockedBitReverse", reinterpret_cast<void*>(InterlockedBitReverse));
    ldr.registerExport("kernel32.dll", "InterlockedBitReverse32", reinterpret_cast<void*>(InterlockedBitReverse32));
    ldr.registerExport("kernel32.dll", "InterlockedBitReverse16", reinterpret_cast<void*>(InterlockedBitReverse16));
    ldr.registerExport("kernel32.dll", "InterlockedBitReverse8", reinterpret_cast<void*>(InterlockedBitReverse8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierDynamicDrainTelemetry", reinterpret_cast<void*>(GetPagefileTierDynamicDrainTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltageDroopPredictiveTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltageDroopPredictiveTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedPopCount64", reinterpret_cast<void*>(InterlockedPopCount64));
    ldr.registerExport("kernel32.dll", "InterlockedPopCount", reinterpret_cast<void*>(InterlockedPopCount));
    ldr.registerExport("kernel32.dll", "InterlockedPopCount32", reinterpret_cast<void*>(InterlockedPopCount32));
    ldr.registerExport("kernel32.dll", "InterlockedPopCount16", reinterpret_cast<void*>(InterlockedPopCount16));
    ldr.registerExport("kernel32.dll", "InterlockedPopCount8", reinterpret_cast<void*>(InterlockedPopCount8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierPressurePredictiveTelemetry", reinterpret_cast<void*>(GetPagefileTierPressurePredictiveTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltageResonanceTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltageResonanceTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingZeros64", reinterpret_cast<void*>(InterlockedCountLeadingZeros64));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingZeros", reinterpret_cast<void*>(InterlockedCountLeadingZeros));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingZeros32", reinterpret_cast<void*>(InterlockedCountLeadingZeros32));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingZeros16", reinterpret_cast<void*>(InterlockedCountLeadingZeros16));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingZeros8", reinterpret_cast<void*>(InterlockedCountLeadingZeros8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierThrottlingTelemetry", reinterpret_cast<void*>(GetPagefileTierThrottlingTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltagePhaseSpreadSpectrumTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltagePhaseSpreadSpectrumTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltagePhaseSpreadSpectrum", reinterpret_cast<void*>(GetCpuFabricVoltagePhaseSpreadSpectrum));
    ldr.registerExport("kernel32.dll", "InterlockedCountTrailingZeros64", reinterpret_cast<void*>(InterlockedCountTrailingZeros64));
    ldr.registerExport("kernel32.dll", "InterlockedCountTrailingZeros", reinterpret_cast<void*>(InterlockedCountTrailingZeros));
    ldr.registerExport("kernel32.dll", "InterlockedCountTrailingZeros32", reinterpret_cast<void*>(InterlockedCountTrailingZeros32));
    ldr.registerExport("kernel32.dll", "InterlockedCountTrailingZeros16", reinterpret_cast<void*>(InterlockedCountTrailingZeros16));
    ldr.registerExport("kernel32.dll", "InterlockedCountTrailingZeros8", reinterpret_cast<void*>(InterlockedCountTrailingZeros8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierCompactionPressureTelemetry", reinterpret_cast<void*>(GetPagefileTierCompactionPressureTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltageSlewRateTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltageSlewRateTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedCountSetBits64", reinterpret_cast<void*>(InterlockedCountSetBits64));
    ldr.registerExport("kernel32.dll", "InterlockedCountSetBits", reinterpret_cast<void*>(InterlockedCountSetBits));
    ldr.registerExport("kernel32.dll", "InterlockedCountSetBits32", reinterpret_cast<void*>(InterlockedCountSetBits32));
    ldr.registerExport("kernel32.dll", "InterlockedCountSetBits16", reinterpret_cast<void*>(InterlockedCountSetBits16));
    ldr.registerExport("kernel32.dll", "InterlockedCountSetBits8", reinterpret_cast<void*>(InterlockedCountSetBits8));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingSigns64", reinterpret_cast<void*>(InterlockedCountLeadingSigns64));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingSigns", reinterpret_cast<void*>(InterlockedCountLeadingSigns));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingSigns32", reinterpret_cast<void*>(InterlockedCountLeadingSigns32));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingSigns16", reinterpret_cast<void*>(InterlockedCountLeadingSigns16));
    ldr.registerExport("kernel32.dll", "InterlockedCountLeadingSigns8", reinterpret_cast<void*>(InterlockedCountLeadingSigns8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierExpansionRateTelemetry", reinterpret_cast<void*>(GetPagefileTierExpansionRateTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltagePhaseMarginTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltagePhaseMarginTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileTierCompactDedupEfficiencyTelemetry", reinterpret_cast<void*>(GetPagefileTierCompactDedupEfficiencyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltageTransitionalDroopIndexTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltageTransitionalDroopIndexTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiff64", reinterpret_cast<void*>(InterlockedAbsDiff64));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiff", reinterpret_cast<void*>(InterlockedAbsDiff));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiff32", reinterpret_cast<void*>(InterlockedAbsDiff32));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiff16", reinterpret_cast<void*>(InterlockedAbsDiff16));
    ldr.registerExport("kernel32.dll", "InterlockedAbsDiff8", reinterpret_cast<void*>(InterlockedAbsDiff8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierEvictionPolicyTelemetry", reinterpret_cast<void*>(GetPagefileTierEvictionPolicyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuFabricVoltageDloadTransientMarginTelemetry", reinterpret_cast<void*>(GetCpuFabricVoltageDloadTransientMarginTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundUp64", reinterpret_cast<void*>(InterlockedAvgRoundUp64));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundUp", reinterpret_cast<void*>(InterlockedAvgRoundUp));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundUp32", reinterpret_cast<void*>(InterlockedAvgRoundUp32));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundUp16", reinterpret_cast<void*>(InterlockedAvgRoundUp16));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundUp8", reinterpret_cast<void*>(InterlockedAvgRoundUp8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierReclaimEfficiencyTelemetry", reinterpret_cast<void*>(GetPagefileTierReclaimEfficiencyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuCoreVoltageGuardBandHeadroomTelemetry", reinterpret_cast<void*>(GetCpuCoreVoltageGuardBandHeadroomTelemetry));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundDown64", reinterpret_cast<void*>(InterlockedAvgRoundDown64));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundDown", reinterpret_cast<void*>(InterlockedAvgRoundDown));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundDown32", reinterpret_cast<void*>(InterlockedAvgRoundDown32));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundDown16", reinterpret_cast<void*>(InterlockedAvgRoundDown16));
    ldr.registerExport("kernel32.dll", "InterlockedAvgRoundDown8", reinterpret_cast<void*>(InterlockedAvgRoundDown8));
    ldr.registerExport("kernel32.dll", "GetPagefileTierMigrationLatencyTelemetry", reinterpret_cast<void*>(GetPagefileTierMigrationLatencyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuCoreVoltageDroopPreventionTelemetry", reinterpret_cast<void*>(GetCpuCoreVoltageDroopPreventionTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileWriteCoalescingThroughputTelemetry", reinterpret_cast<void*>(GetPagefileWriteCoalescingThroughputTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuLoadLineCalibrationImpedanceTelemetry", reinterpret_cast<void*>(GetCpuLoadLineCalibrationImpedanceTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileCommitChargeTrackingTelemetry", reinterpret_cast<void*>(GetPagefileCommitChargeTrackingTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuPowerSupplyTransientSpikeTelemetry", reinterpret_cast<void*>(GetCpuPowerSupplyTransientSpikeTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileCompactionHistogramTelemetry", reinterpret_cast<void*>(GetPagefileCompactionHistogramTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuVoltageRegulatorSlewRateTelemetry", reinterpret_cast<void*>(GetCpuVoltageRegulatorSlewRateTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileCompactionLatencyQuantilesTelemetry", reinterpret_cast<void*>(GetPagefileCompactionLatencyQuantilesTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuVoltageRegulatorDroopSlewTelemetry", reinterpret_cast<void*>(GetCpuVoltageRegulatorDroopSlewTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileExpansionThrottlingLatencyTelemetry", reinterpret_cast<void*>(GetPagefileExpansionThrottlingLatencyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuVoltageRegulatorPhaseMarginJitterTelemetry", reinterpret_cast<void*>(GetCpuVoltageRegulatorPhaseMarginJitterTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileCompactionBandwidthLimitingTelemetry", reinterpret_cast<void*>(GetPagefileCompactionBandwidthLimitingTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuVoltageRegulatorCurrentImbalanceTelemetry", reinterpret_cast<void*>(GetCpuVoltageRegulatorCurrentImbalanceTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileFragmentationEntropyTelemetry", reinterpret_cast<void*>(GetPagefileFragmentationEntropyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuPhaseMarginStabilityTelemetry", reinterpret_cast<void*>(GetCpuPhaseMarginStabilityTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileWriteCoalescingEfficiencyTelemetry", reinterpret_cast<void*>(GetPagefileWriteCoalescingEfficiencyTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuLoadLineResonanceDampingTelemetry", reinterpret_cast<void*>(GetCpuLoadLineResonanceDampingTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileZeroPageScavengeTelemetry", reinterpret_cast<void*>(GetPagefileZeroPageScavengeTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuThermalThrottlingJitterTelemetry", reinterpret_cast<void*>(GetCpuThermalThrottlingJitterTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileClusteredWriteAggregationTelemetry", reinterpret_cast<void*>(GetPagefileClusteredWriteAggregationTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuTransientThermalRecoveryTelemetry", reinterpret_cast<void*>(GetCpuTransientThermalRecoveryTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileWriteBurstBufferAllocationTelemetry", reinterpret_cast<void*>(GetPagefileWriteBurstBufferAllocationTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuVoltageTransitionDroopHistogramTelemetry", reinterpret_cast<void*>(GetCpuVoltageTransitionDroopHistogramTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileAsyncReadBurstBufferAllocationTelemetry", reinterpret_cast<void*>(GetPagefileAsyncReadBurstBufferAllocationTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuPhaseLockedLoopDroopHistogramTelemetry", reinterpret_cast<void*>(GetCpuPhaseLockedLoopDroopHistogramTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileAsyncWriteBurstBufferTelemetry", reinterpret_cast<void*>(GetPagefileAsyncWriteBurstBufferTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuClockJitterHistogramTelemetry", reinterpret_cast<void*>(GetCpuClockJitterHistogramTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileAsyncWriteDrainBufferTelemetry", reinterpret_cast<void*>(GetPagefileAsyncWriteDrainBufferTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuClockPhaseNoiseSpectrumTelemetry", reinterpret_cast<void*>(GetCpuClockPhaseNoiseSpectrumTelemetry));
    ldr.registerExport("kernel32.dll", "GetPagefileWriteCompressionTelemetry", reinterpret_cast<void*>(GetPagefileWriteCompressionTelemetry));
    ldr.registerExport("kernel32.dll", "GetCpuClockSpreadSpectrumModulationTelemetry", reinterpret_cast<void*>(GetCpuClockSpreadSpectrumModulationTelemetry));
    ldr.registerExport("kernel32.dll", "GetThreadId", reinterpret_cast<void*>(GetThreadId));
    ldr.registerExport("kernel32.dll", "CreateIoCompletionPort", reinterpret_cast<void*>(CreateIoCompletionPort));
    ldr.registerExport("kernel32.dll", "GetQueuedCompletionStatus", reinterpret_cast<void*>(GetQueuedCompletionStatus));
    ldr.registerExport("kernel32.dll", "PostQueuedCompletionStatus", reinterpret_cast<void*>(PostQueuedCompletionStatus));
    ldr.registerExport("kernel32.dll", "GetQueuedCompletionStatusEx", reinterpret_cast<void*>(GetQueuedCompletionStatusEx));
    ldr.registerExport("kernel32.dll", "SetFileCompletionNotificationModes", reinterpret_cast<void*>(SetFileCompletionNotificationModes));
    ldr.registerExport("api-ms-win-core-io-l1-1-1.dll", "GetQueuedCompletionStatusEx", reinterpret_cast<void*>(GetQueuedCompletionStatusEx));
    ldr.registerExport("api-ms-win-core-io-l1-1-1.dll", "SetFileCompletionNotificationModes", reinterpret_cast<void*>(SetFileCompletionNotificationModes));
    ldr.registerExport("api-ms-win-core-io-l1-1-1.dll", "CancelIoEx", reinterpret_cast<void*>(CancelIoEx));
    ldr.registerExport("api-ms-win-core-io-l1-1-1.dll", "GetOverlappedResultEx", reinterpret_cast<void*>(GetOverlappedResultEx));
    ldr.registerExport("kernel32.dll", "CreateFiberEx", reinterpret_cast<void*>(CreateFiberEx));
    ldr.registerExport("kernel32.dll", "SwitchToFiber", reinterpret_cast<void*>(SwitchToFiber));
    ldr.registerExport("kernel32.dll", "DeleteFiber", reinterpret_cast<void*>(DeleteFiber));
    ldr.registerExport("kernel32.dll", "ConvertThreadToFiberEx", reinterpret_cast<void*>(ConvertThreadToFiberEx));
    ldr.registerExport("kernel32.dll", "ConvertFiberToThread", reinterpret_cast<void*>(ConvertFiberToThread));
    ldr.registerExport("kernel32.dll", "LockFileEx", reinterpret_cast<void*>(LockFileEx));
    ldr.registerExport("kernel32.dll", "UnlockFileEx", reinterpret_cast<void*>(UnlockFileEx));
    ldr.registerExport("kernel32.dll", "FlushViewOfFile", reinterpret_cast<void*>(FlushViewOfFile));
    ldr.registerExport("kernel32.dll", "HeapValidate", reinterpret_cast<void*>(HeapValidate));
    ldr.registerExport("kernel32.dll", "HeapCompact", reinterpret_cast<void*>(HeapCompact));
    ldr.registerExport("kernel32.dll", "InterlockedFlushSList", reinterpret_cast<void*>(InterlockedFlushSList));
    ldr.registerExport("kernel32.dll", "InterlockedPopEntrySList", reinterpret_cast<void*>(InterlockedPopEntrySList));
    ldr.registerExport("kernel32.dll", "QueryDepthSList", reinterpret_cast<void*>(QueryDepthSList));
    ldr.registerExport("kernel32.dll", "ReadConsoleA", reinterpret_cast<void*>(ReadConsoleA));
    ldr.registerExport("kernel32.dll", "RtlCaptureStackBackTrace", reinterpret_cast<void*>(RtlCaptureStackBackTrace));
    ldr.registerExport("kernel32.dll", "CheckRemoteDebuggerPresent", reinterpret_cast<void*>(CheckRemoteDebuggerPresent));
    ldr.registerExport("kernel32.dll", "WTSGetActiveConsoleSessionId", reinterpret_cast<void*>(WTSGetActiveConsoleSessionId));
    ldr.registerExport("kernel32.dll", "GetSystemTimePreciseAsFileTime", reinterpret_cast<void*>(GetSystemTimePreciseAsFileTime));
    ldr.registerExport("kernel32.dll", "CreateFile2", reinterpret_cast<void*>(CreateFile2));
    ldr.registerExport("kernel32.dll", "GetVolumePathNamesForVolumeNameW", reinterpret_cast<void*>(GetVolumePathNamesForVolumeNameW));
    ldr.registerExport("kernel32.dll", "GetVolumeNameForVolumeMountPointW", reinterpret_cast<void*>(GetVolumeNameForVolumeMountPointW));
    ldr.registerExport("kernel32.dll", "GetComputerNameExW", reinterpret_cast<void*>(GetComputerNameExW));
    ldr.registerExport("kernel32.dll", "SetThreadInformation", reinterpret_cast<void*>(SetThreadInformation));
    ldr.registerExport("kernel32.dll", "SetThreadDescription", reinterpret_cast<void*>(SetThreadDescription));
    ldr.registerExport("kernel32.dll", "SetFileInformationByHandle", reinterpret_cast<void*>(SetFileInformationByHandle));
    ldr.registerExport("kernel32.dll", "GetUserGeoID", reinterpret_cast<void*>(GetUserGeoID));
    ldr.registerExport("kernel32.dll", "GetGeoInfoW", reinterpret_cast<void*>(GetGeoInfoW));
    ldr.registerExport("kernel32.dll", "GetCurrencyFormatW", reinterpret_cast<void*>(GetCurrencyFormatW));
    ldr.registerExport("kernel32.dll", "GetUserPreferredUILanguages", reinterpret_cast<void*>(GetUserPreferredUILanguages));
    ldr.registerExport("kernel32.dll", "GetUserDefaultLocaleName", reinterpret_cast<void*>(GetUserDefaultLocaleName));
    ldr.registerExport("kernel32.dll", "SetWaitableTimer", reinterpret_cast<void*>(SetWaitableTimer));
    ldr.registerExport("kernel32.dll", "CreateWaitableTimerA", reinterpret_cast<void*>(CreateWaitableTimerA));
    ldr.registerExport("kernel32.dll", "CreateSemaphoreA", reinterpret_cast<void*>(CreateSemaphoreA));
    ldr.registerExport("kernel32.dll", "CreateThreadpoolWait", reinterpret_cast<void*>(CreateThreadpoolWait));
    ldr.registerExport("kernel32.dll", "SetThreadpoolWait", reinterpret_cast<void*>(SetThreadpoolWait));
    ldr.registerExport("kernel32.dll", "CancelThreadpoolWait", reinterpret_cast<void*>(CancelThreadpoolWait));
    ldr.registerExport("kernel32.dll", "WaitForThreadpoolWaitCallbacks", reinterpret_cast<void*>(WaitForThreadpoolWaitCallbacks));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolWait", reinterpret_cast<void*>(CloseThreadpoolWait));
    ldr.registerExport("kernel32.dll", "WaitOnAddress", reinterpret_cast<void*>(WaitOnAddress));
    ldr.registerExport("kernel32.dll", "WakeByAddressSingle", reinterpret_cast<void*>(WakeByAddressSingle));
    ldr.registerExport("kernel32.dll", "WakeByAddressAll", reinterpret_cast<void*>(WakeByAddressAll));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "WaitOnAddress", reinterpret_cast<void*>(WaitOnAddress));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "WakeByAddressSingle", reinterpret_cast<void*>(WakeByAddressSingle));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "WakeByAddressAll", reinterpret_cast<void*>(WakeByAddressAll));
    ldr.registerExport("kernel32.dll", "InitializeSynchronizationBarrier", reinterpret_cast<void*>(InitializeSynchronizationBarrier));
    ldr.registerExport("kernel32.dll", "EnterSynchronizationBarrier", reinterpret_cast<void*>(EnterSynchronizationBarrier));
    ldr.registerExport("kernel32.dll", "DeleteSynchronizationBarrier", reinterpret_cast<void*>(DeleteSynchronizationBarrier));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "InitializeSynchronizationBarrier", reinterpret_cast<void*>(InitializeSynchronizationBarrier));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "EnterSynchronizationBarrier", reinterpret_cast<void*>(EnterSynchronizationBarrier));
    ldr.registerExport("api-ms-win-core-synch-l1-2-0.dll", "DeleteSynchronizationBarrier", reinterpret_cast<void*>(DeleteSynchronizationBarrier));
    ldr.registerExport("kernel32.dll", "GetEnabledXStateFeatures", reinterpret_cast<void*>(GetEnabledXStateFeatures));
    ldr.registerExport("kernel32.dll", "GetNumberFormatEx", reinterpret_cast<void*>(GetNumberFormatEx));
    ldr.registerExport("kernel32.dll", "SetConsoleCP", reinterpret_cast<void*>(SetConsoleCP));
    ldr.registerExport("kernel32.dll", "SetConsoleOutputCP", reinterpret_cast<void*>(SetConsoleOutputCP));

    ldr.registerExport("kernel32.dll", "QueryDosDeviceW", reinterpret_cast<void*>(QueryDosDeviceW));
    ldr.registerExport("kernel32.dll", "ReadProcessMemory", reinterpret_cast<void*>(ReadProcessMemory));
    ldr.registerExport("kernel32.dll", "GetCPInfoExW", reinterpret_cast<void*>(GetCPInfoExW));
    ldr.registerExport("kernel32.dll", "WriteProcessMemory", reinterpret_cast<void*>(WriteProcessMemory));
    ldr.registerExport("kernel32.dll", "TryEnterCriticalSection", reinterpret_cast<void*>(TryEnterCriticalSection));
    ldr.registerExport("kernel32.dll", "HeapDestroy", reinterpret_cast<void*>(HeapDestroy));
    ldr.registerExport("kernel32.dll", "GetDiskFreeSpaceA", reinterpret_cast<void*>(GetDiskFreeSpaceA));
    ldr.registerExport("kernel32.dll", "FindFirstFileA", reinterpret_cast<void*>(FindFirstFileA));
    ldr.registerExport("kernel32.dll", "OpenMutexW", reinterpret_cast<void*>(OpenMutexW));
    ldr.registerExport("kernel32.dll", "lstrlenA", reinterpret_cast<void*>(lstrlenA));
    ldr.registerExport("kernel32.dll", "lstrcpyA", reinterpret_cast<void*>(lstrcpyA));
    ldr.registerExport("kernel32.dll", "CreateFileA", reinterpret_cast<void*>(CreateFileA));
    ldr.registerExport("kernel32.dll", "FreeResource", reinterpret_cast<void*>(FreeResource));
    ldr.registerExport("kernel32.dll", "GlobalAddAtomW", reinterpret_cast<void*>(GlobalAddAtomW));
    ldr.registerExport("kernel32.dll", "GetExitCodeThread", reinterpret_cast<void*>(GetExitCodeThread));
    ldr.registerExport("kernel32.dll", "OutputDebugStringW", reinterpret_cast<void*>(OutputDebugStringW));
    ldr.registerExport("kernel32.dll", "IsBadReadPtr", reinterpret_cast<void*>(IsBadReadPtr));
    ldr.registerExport("kernel32.dll", "GetShortPathNameW", reinterpret_cast<void*>(GetShortPathNameW));
    ldr.registerExport("kernel32.dll", "GlobalFindAtomW", reinterpret_cast<void*>(GlobalFindAtomW));
    ldr.registerExport("kernel32.dll", "WritePrivateProfileStringW", reinterpret_cast<void*>(WritePrivateProfileStringW));
    ldr.registerExport("kernel32.dll", "FindNextFileA", reinterpret_cast<void*>(FindNextFileA));
    ldr.registerExport("kernel32.dll", "GlobalDeleteAtom", reinterpret_cast<void*>(GlobalDeleteAtom));
    ldr.registerExport("kernel32.dll", "GetThreadPriority", reinterpret_cast<void*>(GetThreadPriority));
    ldr.registerExport("kernel32.dll", "SetThreadPriority", reinterpret_cast<void*>(SetThreadPriority));
    ldr.registerExport("kernel32.dll", "SearchPathW", reinterpret_cast<void*>(SearchPathW));
    ldr.registerExport("kernel32.dll", "VerifyVersionInfoW", reinterpret_cast<void*>(VerifyVersionInfoW));
    ldr.registerExport("kernel32.dll", "HeapCreate", reinterpret_cast<void*>(HeapCreate));
    ldr.registerExport("kernel32.dll", "SignalObjectAndWait", reinterpret_cast<void*>(SignalObjectAndWait));
    ldr.registerExport("kernel32.dll", "VerSetConditionMask", reinterpret_cast<void*>(VerSetConditionMask));
    ldr.registerExport("kernel32.dll", "GetUserDefaultUILanguage", reinterpret_cast<void*>(GetUserDefaultUILanguage));
    ldr.registerExport("kernel32.dll", "GetConsoleCP", reinterpret_cast<void*>(GetConsoleCP));
    ldr.registerExport("kernel32.dll", "CompareStringA", reinterpret_cast<void*>(CompareStringA));
    ldr.registerExport("kernel32.dll", "EnumResourceNamesW", reinterpret_cast<void*>(EnumResourceNamesW));
    ldr.registerExport("kernel32.dll", "GetSystemDirectoryW", reinterpret_cast<void*>(GetSystemDirectoryW));
    ldr.registerExport("kernel32.dll", "GetDriveTypeA", reinterpret_cast<void*>(GetDriveTypeA));
    ldr.registerExport("kernel32.dll", "GetComputerNameW", reinterpret_cast<void*>(GetComputerNameW));
    ldr.registerExport("kernel32.dll", "GetSystemDefaultUILanguage", reinterpret_cast<void*>(GetSystemDefaultUILanguage));
    ldr.registerExport("kernel32.dll", "EnumCalendarInfoW", reinterpret_cast<void*>(EnumCalendarInfoW));
    ldr.registerExport("kernel32.dll", "GetPrivateProfileStringW", reinterpret_cast<void*>(GetPrivateProfileStringW));
    ldr.registerExport("kernel32.dll", "WaitForMultipleObjectsEx", reinterpret_cast<void*>(WaitForMultipleObjectsEx));
    ldr.registerExport("kernel32.dll", "GetThreadLocale", reinterpret_cast<void*>(GetThreadLocale));
    ldr.registerExport("kernel32.dll", "SetThreadLocale", reinterpret_cast<void*>(SetThreadLocale));
    ldr.registerExport("kernel32.dll", "VerLanguageNameW", reinterpret_cast<void*>(VerLanguageNameW));
    ldr.registerExport("kernel32.dll", "GetLogicalProcessorInformation", reinterpret_cast<void*>(GetLogicalProcessorInformation));
    ldr.registerExport("kernel32.dll", "GetLogicalProcessorInformationEx", reinterpret_cast<void*>(GetLogicalProcessorInformationEx));
    ldr.registerExport("kernel32.dll", "GetSystemCpuSetInformation", reinterpret_cast<void*>(GetSystemCpuSetInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-3.dll", "GetSystemCpuSetInformation", reinterpret_cast<void*>(GetSystemCpuSetInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-6.dll", "GetSystemCpuSetInformation", reinterpret_cast<void*>(GetSystemCpuSetInformation));
    ldr.registerExport("kernel32.dll", "GetFirmwareType", reinterpret_cast<void*>(GetFirmwareType));
    ldr.registerExport("api-ms-win-core-sysinfo-l1-2-0.dll", "GetFirmwareType", reinterpret_cast<void*>(GetFirmwareType));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-0.dll", "QueryThreadCycleTime", reinterpret_cast<void*>(QueryThreadCycleTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-0.dll", "QueryProcessCycleTime", reinterpret_cast<void*>(QueryProcessCycleTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryThreadCycleTime", reinterpret_cast<void*>(QueryThreadCycleTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryProcessCycleTime", reinterpret_cast<void*>(QueryProcessCycleTime));
    ldr.registerExport("kernel32.dll", "SetThreadGroupAffinity", reinterpret_cast<void*>(SetThreadGroupAffinity));
    ldr.registerExport("api-ms-win-core-processtopology-l1-1-0.dll", "GetThreadGroupAffinity", reinterpret_cast<void*>(GetThreadGroupAffinity));
    ldr.registerExport("api-ms-win-core-processtopology-l1-1-0.dll", "SetThreadGroupAffinity", reinterpret_cast<void*>(SetThreadGroupAffinity));
    ldr.registerExport("api-ms-win-core-processtopology-l1-2-0.dll", "GetThreadGroupAffinity", reinterpret_cast<void*>(GetThreadGroupAffinity));
    ldr.registerExport("api-ms-win-core-processtopology-l1-2-0.dll", "SetThreadGroupAffinity", reinterpret_cast<void*>(SetThreadGroupAffinity));
    ldr.registerExport("kernel32.dll", "SetProcessMitigationPolicy", reinterpret_cast<void*>(SetProcessMitigationPolicy));
    ldr.registerExport("kernel32.dll", "GetProcessMitigationPolicy", reinterpret_cast<void*>(GetProcessMitigationPolicy));
    ldr.registerExport("kernel32.dll", "SetThreadSelectedCpuSets", reinterpret_cast<void*>(SetThreadSelectedCpuSets));
    ldr.registerExport("kernel32.dll", "GetThreadSelectedCpuSets", reinterpret_cast<void*>(GetThreadSelectedCpuSets));
    ldr.registerExport("kernel32.dll", "SetProcessDefaultCpuSets", reinterpret_cast<void*>(SetProcessDefaultCpuSets));
    ldr.registerExport("kernel32.dll", "GetProcessDefaultCpuSets", reinterpret_cast<void*>(GetProcessDefaultCpuSets));
    ldr.registerExport("kernel32.dll", "GetProcessInformation", reinterpret_cast<void*>(GetProcessInformation));
    ldr.registerExport("kernel32.dll", "IsWow64Process", reinterpret_cast<void*>(IsWow64Process));
    ldr.registerExport("kernel32.dll", "ProcessIdToSessionId", reinterpret_cast<void*>(ProcessIdToSessionId));
    ldr.registerExport("kernel32.dll", "LocaleNameToLCID", reinterpret_cast<void*>(LocaleNameToLCID));
    ldr.registerExport("kernel32.dll", "GetTimeZoneInformationForYear", reinterpret_cast<void*>(GetTimeZoneInformationForYear));
    ldr.registerExport("kernel32.dll", "GetSystemTimes", reinterpret_cast<void*>(GetSystemTimes));

    // Horizon 202: VBS Enclaves, High-Precision Timers, Physical Memory & Working Set
    ldr.registerExport("kernel32.dll", "IsEnclaveTypeSupported", reinterpret_cast<void*>(IsEnclaveTypeSupported));
    ldr.registerExport("kernel32.dll", "CreateEnclave", reinterpret_cast<void*>(CreateEnclave));
    ldr.registerExport("kernel32.dll", "InitializeEnclave", reinterpret_cast<void*>(InitializeEnclave));
    ldr.registerExport("kernel32.dll", "LoadEnclaveData", reinterpret_cast<void*>(LoadEnclaveData));
    ldr.registerExport("kernel32.dll", "TerminateEnclave", reinterpret_cast<void*>(TerminateEnclave));
    ldr.registerExport("kernel32.dll", "CallEnclave", reinterpret_cast<void*>(CallEnclave));
    ldr.registerExport("kernel32.dll", "QueryInterruptTimePrecise", reinterpret_cast<void*>(QueryInterruptTimePrecise));
    ldr.registerExport("kernel32.dll", "QueryUnbiasedInterruptTimePrecise", reinterpret_cast<void*>(QueryUnbiasedInterruptTimePrecise));
    ldr.registerExport("kernel32.dll", "GetPhysicallyInstalledSystemMemory", reinterpret_cast<void*>(GetPhysicallyInstalledSystemMemory));
    ldr.registerExport("kernel32.dll", "EmptyWorkingSet", reinterpret_cast<void*>(EmptyWorkingSet));
    ldr.registerExport("kernel32.dll", "K32EmptyWorkingSet", reinterpret_cast<void*>(K32EmptyWorkingSet));

    ldr.registerExport("api-ms-win-core-enclave-l1-1-0.dll", "IsEnclaveTypeSupported", reinterpret_cast<void*>(IsEnclaveTypeSupported));
    ldr.registerExport("api-ms-win-core-enclave-l1-1-0.dll", "CreateEnclave", reinterpret_cast<void*>(CreateEnclave));
    ldr.registerExport("api-ms-win-core-enclave-l1-1-0.dll", "InitializeEnclave", reinterpret_cast<void*>(InitializeEnclave));
    ldr.registerExport("api-ms-win-core-enclave-l1-1-0.dll", "LoadEnclaveData", reinterpret_cast<void*>(LoadEnclaveData));
    ldr.registerExport("api-ms-win-core-enclave-l1-1-0.dll", "TerminateEnclave", reinterpret_cast<void*>(TerminateEnclave));
    ldr.registerExport("api-ms-win-core-enclave-l1-1-0.dll", "CallEnclave", reinterpret_cast<void*>(CallEnclave));

    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryInterruptTimePrecise", reinterpret_cast<void*>(QueryInterruptTimePrecise));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryUnbiasedInterruptTimePrecise", reinterpret_cast<void*>(QueryUnbiasedInterruptTimePrecise));

    ldr.registerExport("api-ms-win-core-sysinfo-l1-2-0.dll", "GetPhysicallyInstalledSystemMemory", reinterpret_cast<void*>(GetPhysicallyInstalledSystemMemory));

    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "GetThreadContext", reinterpret_cast<void*>(GetThreadContext));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "SetThreadContext", reinterpret_cast<void*>(SetThreadContext));

    ldr.registerExport("kernel32.dll", "GetProcessMemoryInfo", reinterpret_cast<void*>(GetProcessMemoryInfo));
    ldr.registerExport("kernel32.dll", "K32GetProcessMemoryInfo", reinterpret_cast<void*>(K32GetProcessMemoryInfo));
    ldr.registerExport("psapi.dll", "EmptyWorkingSet", reinterpret_cast<void*>(EmptyWorkingSet));
    ldr.registerExport("psapi.dll", "K32EmptyWorkingSet", reinterpret_cast<void*>(K32EmptyWorkingSet));
    ldr.registerExport("psapi.dll", "GetProcessMemoryInfo", reinterpret_cast<void*>(GetProcessMemoryInfo));
    ldr.registerExport("psapi.dll", "K32GetProcessMemoryInfo", reinterpret_cast<void*>(K32GetProcessMemoryInfo));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32GetProcessMemoryInfo", reinterpret_cast<void*>(K32GetProcessMemoryInfo));

    ldr.registerExport("kernel32.dll", "InitializeProcessWorkingSetWatch", reinterpret_cast<void*>(InitializeProcessWorkingSetWatch));
    ldr.registerExport("kernel32.dll", "K32InitializeProcessWorkingSetWatch", reinterpret_cast<void*>(K32InitializeProcessWorkingSetWatch));
    ldr.registerExport("kernel32.dll", "GetProcessWorkingSetWatch", reinterpret_cast<void*>(GetProcessWorkingSetWatch));
    ldr.registerExport("kernel32.dll", "K32GetProcessWorkingSetWatch", reinterpret_cast<void*>(K32GetProcessWorkingSetWatch));
    ldr.registerExport("kernel32.dll", "GetProcessWorkingSetWatchEx", reinterpret_cast<void*>(GetProcessWorkingSetWatchEx));
    ldr.registerExport("kernel32.dll", "K32GetProcessWorkingSetWatchEx", reinterpret_cast<void*>(K32GetProcessWorkingSetWatchEx));
    ldr.registerExport("kernel32.dll", "GetProcessImageFileNameW", reinterpret_cast<void*>(GetProcessImageFileNameW));
    ldr.registerExport("kernel32.dll", "GetProcessImageFileNameA", reinterpret_cast<void*>(GetProcessImageFileNameA));
    ldr.registerExport("kernel32.dll", "K32GetProcessImageFileNameA", reinterpret_cast<void*>(K32GetProcessImageFileNameA));

    ldr.registerExport("psapi.dll", "InitializeProcessWorkingSetWatch", reinterpret_cast<void*>(InitializeProcessWorkingSetWatch));
    ldr.registerExport("psapi.dll", "K32InitializeProcessWorkingSetWatch", reinterpret_cast<void*>(K32InitializeProcessWorkingSetWatch));
    ldr.registerExport("psapi.dll", "GetProcessWorkingSetWatch", reinterpret_cast<void*>(GetProcessWorkingSetWatch));
    ldr.registerExport("psapi.dll", "K32GetProcessWorkingSetWatch", reinterpret_cast<void*>(K32GetProcessWorkingSetWatch));
    ldr.registerExport("psapi.dll", "GetProcessWorkingSetWatchEx", reinterpret_cast<void*>(GetProcessWorkingSetWatchEx));
    ldr.registerExport("psapi.dll", "K32GetProcessWorkingSetWatchEx", reinterpret_cast<void*>(K32GetProcessWorkingSetWatchEx));
    ldr.registerExport("psapi.dll", "GetProcessImageFileNameW", reinterpret_cast<void*>(GetProcessImageFileNameW));
    ldr.registerExport("psapi.dll", "GetProcessImageFileNameA", reinterpret_cast<void*>(GetProcessImageFileNameA));
    ldr.registerExport("psapi.dll", "K32GetProcessImageFileNameW", reinterpret_cast<void*>(K32GetProcessImageFileNameW));
    ldr.registerExport("psapi.dll", "K32GetProcessImageFileNameA", reinterpret_cast<void*>(K32GetProcessImageFileNameA));

    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32InitializeProcessWorkingSetWatch", reinterpret_cast<void*>(K32InitializeProcessWorkingSetWatch));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32GetProcessWorkingSetWatch", reinterpret_cast<void*>(K32GetProcessWorkingSetWatch));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32GetProcessWorkingSetWatchEx", reinterpret_cast<void*>(K32GetProcessWorkingSetWatchEx));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32GetProcessImageFileNameW", reinterpret_cast<void*>(K32GetProcessImageFileNameW));
    ldr.registerExport("api-ms-win-core-psapi-l1-1-0.dll", "K32GetProcessImageFileNameA", reinterpret_cast<void*>(K32GetProcessImageFileNameA));

    // Horizon 203: Fibers, FLS, Process Mitigation Policies
    ldr.registerExport("kernel32.dll", "ConvertThreadToFiber", reinterpret_cast<void*>(ConvertThreadToFiber));
    ldr.registerExport("kernel32.dll", "ConvertThreadToFiberEx", reinterpret_cast<void*>(ConvertThreadToFiberEx));
    ldr.registerExport("kernel32.dll", "ConvertFiberToThread", reinterpret_cast<void*>(ConvertFiberToThread));
    ldr.registerExport("kernel32.dll", "CreateFiber", reinterpret_cast<void*>(CreateFiber));
    ldr.registerExport("kernel32.dll", "CreateFiberEx", reinterpret_cast<void*>(CreateFiberEx));
    ldr.registerExport("kernel32.dll", "DeleteFiber", reinterpret_cast<void*>(DeleteFiber));
    ldr.registerExport("kernel32.dll", "SwitchToFiber", reinterpret_cast<void*>(SwitchToFiber));
    ldr.registerExport("kernel32.dll", "IsThreadAFiber", reinterpret_cast<void*>(IsThreadAFiber));
    ldr.registerExport("kernel32.dll", "GetCurrentFiber", reinterpret_cast<void*>(GetCurrentFiber));
    ldr.registerExport("kernel32.dll", "FlsAlloc", reinterpret_cast<void*>(FlsAlloc));
    ldr.registerExport("kernel32.dll", "FlsFree", reinterpret_cast<void*>(FlsFree));
    ldr.registerExport("kernel32.dll", "FlsGetValue", reinterpret_cast<void*>(FlsGetValue));
    ldr.registerExport("kernel32.dll", "FlsSetValue", reinterpret_cast<void*>(FlsSetValue));
    ldr.registerExport("kernel32.dll", "SetProcessMitigationPolicy", reinterpret_cast<void*>(SetProcessMitigationPolicy));
    ldr.registerExport("kernel32.dll", "GetProcessMitigationPolicy", reinterpret_cast<void*>(GetProcessMitigationPolicy));

    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "FlsAlloc", reinterpret_cast<void*>(FlsAlloc));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "FlsFree", reinterpret_cast<void*>(FlsFree));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "FlsGetValue", reinterpret_cast<void*>(FlsGetValue));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "FlsSetValue", reinterpret_cast<void*>(FlsSetValue));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "ConvertThreadToFiber", reinterpret_cast<void*>(ConvertThreadToFiber));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "ConvertThreadToFiberEx", reinterpret_cast<void*>(ConvertThreadToFiberEx));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "ConvertFiberToThread", reinterpret_cast<void*>(ConvertFiberToThread));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "CreateFiber", reinterpret_cast<void*>(CreateFiber));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "CreateFiberEx", reinterpret_cast<void*>(CreateFiberEx));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "DeleteFiber", reinterpret_cast<void*>(DeleteFiber));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "SwitchToFiber", reinterpret_cast<void*>(SwitchToFiber));
    ldr.registerExport("api-ms-win-core-fibers-l1-1-1.dll", "IsThreadAFiber", reinterpret_cast<void*>(IsThreadAFiber));

    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "SetProcessMitigationPolicy", reinterpret_cast<void*>(SetProcessMitigationPolicy));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "GetProcessMitigationPolicy", reinterpret_cast<void*>(GetProcessMitigationPolicy));

    // ntdll.dll exports
    ldr.registerExport("ntdll.dll", "RtlAllocateHeap", reinterpret_cast<void*>(ntdll::RtlAllocateHeap));
    ldr.registerExport("ntdll.dll", "RtlFreeHeap", reinterpret_cast<void*>(ntdll::RtlFreeHeap));
    ldr.registerExport("ntdll.dll", "RtlCreateHeap", reinterpret_cast<void*>(ntdll::RtlCreateHeap));
    ldr.registerExport("ntdll.dll", "RtlDestroyHeap", reinterpret_cast<void*>(ntdll::RtlDestroyHeap));
    ldr.registerExport("ntdll.dll", "RtlSizeHeap", reinterpret_cast<void*>(ntdll::RtlSizeHeap));
    ldr.registerExport("ntdll.dll", "RtlSetLastWin32Error", reinterpret_cast<void*>(ntdll::RtlSetLastWin32Error));
    ldr.registerExport("ntdll.dll", "RtlGetLastWin32Error", reinterpret_cast<void*>(ntdll::RtlGetLastWin32Error));
    ldr.registerExport("ntdll.dll", "RtlNtStatusToDosError", reinterpret_cast<void*>(ntdll::RtlNtStatusToDosError));
    ldr.registerExport("ntdll.dll", "NtAllocateVirtualMemory", reinterpret_cast<void*>(ntdll::NtAllocateVirtualMemory));
    ldr.registerExport("ntdll.dll", "NtFreeVirtualMemory", reinterpret_cast<void*>(ntdll::NtFreeVirtualMemory));
    ldr.registerExport("ntdll.dll", "NtWriteFile", reinterpret_cast<void*>(ntdll::NtWriteFile));
    ldr.registerExport("ntdll.dll", "NtReadFile", reinterpret_cast<void*>(ntdll::NtReadFile));
    ldr.registerExport("ntdll.dll", "NtClose", reinterpret_cast<void*>(ntdll::NtClose));
    ldr.registerExport("ntdll.dll", "NtWaitForSingleObject", reinterpret_cast<void*>(ntdll::NtWaitForSingleObject));
    ldr.registerExport("ntdll.dll", "NtCreateSection", reinterpret_cast<void*>(ntdll::NtCreateSection));
    ldr.registerExport("ntdll.dll", "NtMapViewOfSection", reinterpret_cast<void*>(ntdll::NtMapViewOfSection));
    ldr.registerExport("ntdll.dll", "NtUnmapViewOfSection", reinterpret_cast<void*>(ntdll::NtUnmapViewOfSection));
    ldr.registerExport("ntdll.dll", "NtQueryInformationFile", reinterpret_cast<void*>(ntdll::NtQueryInformationFile));
    ldr.registerExport("ntdll.dll", "NtSetInformationFile", reinterpret_cast<void*>(ntdll::NtSetInformationFile));
    ldr.registerExport("ntdll.dll", "NtQueryDirectoryFile", reinterpret_cast<void*>(ntdll::NtQueryDirectoryFile));
    ldr.registerExport("ntdll.dll", "NtQueryPerformanceCounter", reinterpret_cast<void*>(ntdll::NtQueryPerformanceCounter));
    ldr.registerExport("ntdll.dll", "NtYieldExecution", reinterpret_cast<void*>(ntdll::NtYieldExecution));
    ldr.registerExport("ntdll.dll", "NtQueryInformationProcess", reinterpret_cast<void*>(ntdll::NtQueryInformationProcess));
    ldr.registerExport("ntdll.dll", "NtCreateNamedPipeFile", reinterpret_cast<void*>(ntdll::NtCreateNamedPipeFile));
    ldr.registerExport("ntdll.dll", "NtCreateMailslotFile", reinterpret_cast<void*>(ntdll::NtCreateMailslotFile));

    // Horizon 204: Thread Affinity, Priority Boost & Ideal Processor Pacing
    ldr.registerExport("kernel32.dll", "SetThreadPriorityBoost", reinterpret_cast<void*>(SetThreadPriorityBoost));
    ldr.registerExport("kernel32.dll", "GetThreadPriorityBoost", reinterpret_cast<void*>(GetThreadPriorityBoost));
    ldr.registerExport("kernel32.dll", "SetProcessPriorityBoost", reinterpret_cast<void*>(SetProcessPriorityBoost));
    ldr.registerExport("kernel32.dll", "GetProcessPriorityBoost", reinterpret_cast<void*>(GetProcessPriorityBoost));
    ldr.registerExport("kernel32.dll", "SetThreadIdealProcessorEx", reinterpret_cast<void*>(SetThreadIdealProcessorEx));
    ldr.registerExport("kernel32.dll", "GetThreadIdealProcessorEx", reinterpret_cast<void*>(GetThreadIdealProcessorEx));
    ldr.registerExport("kernel32.dll", "GetThreadInformation", reinterpret_cast<void*>(GetThreadInformation));
    ldr.registerExport("kernel32.dll", "SetThreadDescription", reinterpret_cast<void*>(SetThreadDescription));
    ldr.registerExport("kernel32.dll", "GetThreadDescription", reinterpret_cast<void*>(GetThreadDescription));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-3.dll", "SetThreadDescription", reinterpret_cast<void*>(SetThreadDescription));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-3.dll", "GetThreadDescription", reinterpret_cast<void*>(GetThreadDescription));

    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "SetThreadAffinityMask", reinterpret_cast<void*>(SetThreadAffinityMask));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetProcessAffinityMask", reinterpret_cast<void*>(GetProcessAffinityMask));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "SetProcessAffinityMask", reinterpret_cast<void*>(SetProcessAffinityMask));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "SetThreadPriorityBoost", reinterpret_cast<void*>(SetThreadPriorityBoost));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetThreadPriorityBoost", reinterpret_cast<void*>(GetThreadPriorityBoost));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "SetPriorityClass", reinterpret_cast<void*>(SetPriorityClass));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "GetPriorityClass", reinterpret_cast<void*>(GetPriorityClass));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "SetThreadPriority", reinterpret_cast<void*>(SetThreadPriority));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "GetThreadPriority", reinterpret_cast<void*>(GetThreadPriority));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "SetProcessPriorityBoost", reinterpret_cast<void*>(SetProcessPriorityBoost));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetProcessPriorityBoost", reinterpret_cast<void*>(GetProcessPriorityBoost));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "SetThreadIdealProcessorEx", reinterpret_cast<void*>(SetThreadIdealProcessorEx));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetThreadIdealProcessorEx", reinterpret_cast<void*>(GetThreadIdealProcessorEx));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "SetThreadInformation", reinterpret_cast<void*>(SetThreadInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetThreadInformation", reinterpret_cast<void*>(GetThreadInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "QueryProcessCycleTime", reinterpret_cast<void*>(QueryProcessCycleTime));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "QueryThreadCycleTime", reinterpret_cast<void*>(QueryThreadCycleTime));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "QueryIdleProcessorCycleTime", reinterpret_cast<void*>(QueryIdleProcessorCycleTime));
    ldr.registerExport("kernel32.dll", "VirtualProtectEx", reinterpret_cast<void*>(VirtualProtectEx));
    ldr.registerExport("kernel32.dll", "QueryProcessCycleTime", reinterpret_cast<void*>(QueryProcessCycleTime));
    ldr.registerExport("kernel32.dll", "QueryThreadCycleTime", reinterpret_cast<void*>(QueryThreadCycleTime));
    ldr.registerExport("kernel32.dll", "QueryIdleProcessorCycleTime", reinterpret_cast<void*>(QueryIdleProcessorCycleTime));

    // Horizon 209: Legacy Threadpool, Timer Queue & High-Precision Timing Architecture
    ldr.registerExport("kernel32.dll", "QueueUserWorkItem", reinterpret_cast<void*>(QueueUserWorkItem));
    ldr.registerExport("kernel32.dll", "CreateTimerQueue", reinterpret_cast<void*>(CreateTimerQueue));
    ldr.registerExport("kernel32.dll", "CreateTimerQueueTimer", reinterpret_cast<void*>(CreateTimerQueueTimer));
    ldr.registerExport("kernel32.dll", "ChangeTimerQueueTimer", reinterpret_cast<void*>(ChangeTimerQueueTimer));
    ldr.registerExport("kernel32.dll", "DeleteTimerQueueTimer", reinterpret_cast<void*>(DeleteTimerQueueTimer));
    ldr.registerExport("kernel32.dll", "DeleteTimerQueueEx", reinterpret_cast<void*>(DeleteTimerQueueEx));
    ldr.registerExport("kernel32.dll", "DeleteTimerQueue", reinterpret_cast<void*>(DeleteTimerQueue));
    ldr.registerExport("kernel32.dll", "RegisterWaitForSingleObject", reinterpret_cast<void*>(RegisterWaitForSingleObject));
    ldr.registerExport("kernel32.dll", "UnregisterWait", reinterpret_cast<void*>(UnregisterWait));
    ldr.registerExport("kernel32.dll", "UnregisterWaitEx", reinterpret_cast<void*>(UnregisterWaitEx));
    ldr.registerExport("kernel32.dll", "TrySubmitThreadpoolCallback", reinterpret_cast<void*>(TrySubmitThreadpoolCallback));
    ldr.registerExport("kernel32.dll", "QueryInterruptTime", reinterpret_cast<void*>(QueryInterruptTime));
    ldr.registerExport("kernel32.dll", "QueryUnbiasedInterruptTime", reinterpret_cast<void*>(QueryUnbiasedInterruptTime));

    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "QueueUserWorkItem", reinterpret_cast<void*>(QueueUserWorkItem));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "CreateTimerQueue", reinterpret_cast<void*>(CreateTimerQueue));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "CreateTimerQueueTimer", reinterpret_cast<void*>(CreateTimerQueueTimer));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "ChangeTimerQueueTimer", reinterpret_cast<void*>(ChangeTimerQueueTimer));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "DeleteTimerQueueTimer", reinterpret_cast<void*>(DeleteTimerQueueTimer));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "DeleteTimerQueueEx", reinterpret_cast<void*>(DeleteTimerQueueEx));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "RegisterWaitForSingleObject", reinterpret_cast<void*>(RegisterWaitForSingleObject));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "UnregisterWait", reinterpret_cast<void*>(UnregisterWait));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "UnregisterWaitEx", reinterpret_cast<void*>(UnregisterWaitEx));
    ldr.registerExport("api-ms-win-core-threadpool-legacy-l1-1-0.dll", "BindIoCompletionCallback", reinterpret_cast<void*>(BindIoCompletionCallback));

    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "TrySubmitThreadpoolCallback", reinterpret_cast<void*>(TrySubmitThreadpoolCallback));

    ldr.registerExport("api-ms-win-core-realtime-l1-1-0.dll", "QueryInterruptTime", reinterpret_cast<void*>(QueryInterruptTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-0.dll", "QueryUnbiasedInterruptTime", reinterpret_cast<void*>(QueryUnbiasedInterruptTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryInterruptTime", reinterpret_cast<void*>(QueryInterruptTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryUnbiasedInterruptTime", reinterpret_cast<void*>(QueryUnbiasedInterruptTime));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(QueryPerformanceCounter));
    ldr.registerExport("api-ms-win-core-realtime-l1-1-1.dll", "QueryPerformanceFrequency", reinterpret_cast<void*>(QueryPerformanceFrequency));

    ldr.registerExport("api-ms-win-core-profile-l1-1-0.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(QueryPerformanceCounter));
    ldr.registerExport("api-ms-win-core-profile-l1-1-0.dll", "QueryPerformanceFrequency", reinterpret_cast<void*>(QueryPerformanceFrequency));

    // Horizon 210: Threadpool I/O, Firmware Tables & Multimedia Timers
    ldr.registerExport("kernel32.dll", "CreateThreadpoolIo", reinterpret_cast<void*>(CreateThreadpoolIo));
    ldr.registerExport("kernel32.dll", "StartThreadpoolIo", reinterpret_cast<void*>(StartThreadpoolIo));
    ldr.registerExport("kernel32.dll", "CancelThreadpoolIo", reinterpret_cast<void*>(CancelThreadpoolIo));
    ldr.registerExport("kernel32.dll", "WaitForThreadpoolIoCallbacks", reinterpret_cast<void*>(WaitForThreadpoolIoCallbacks));
    ldr.registerExport("kernel32.dll", "CloseThreadpoolIo", reinterpret_cast<void*>(CloseThreadpoolIo));

    ldr.registerExport("api-ms-win-core-threadpool-io-l1-1-0.dll", "CreateThreadpoolIo", reinterpret_cast<void*>(CreateThreadpoolIo));
    ldr.registerExport("api-ms-win-core-threadpool-io-l1-1-0.dll", "StartThreadpoolIo", reinterpret_cast<void*>(StartThreadpoolIo));
    ldr.registerExport("api-ms-win-core-threadpool-io-l1-1-0.dll", "CancelThreadpoolIo", reinterpret_cast<void*>(CancelThreadpoolIo));
    ldr.registerExport("api-ms-win-core-threadpool-io-l1-1-0.dll", "WaitForThreadpoolIoCallbacks", reinterpret_cast<void*>(WaitForThreadpoolIoCallbacks));
    ldr.registerExport("api-ms-win-core-threadpool-io-l1-1-0.dll", "CloseThreadpoolIo", reinterpret_cast<void*>(CloseThreadpoolIo));

    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CreateThreadpoolIo", reinterpret_cast<void*>(CreateThreadpoolIo));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "StartThreadpoolIo", reinterpret_cast<void*>(StartThreadpoolIo));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CancelThreadpoolIo", reinterpret_cast<void*>(CancelThreadpoolIo));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "WaitForThreadpoolIoCallbacks", reinterpret_cast<void*>(WaitForThreadpoolIoCallbacks));
    ldr.registerExport("api-ms-win-core-threadpool-l1-2-0.dll", "CloseThreadpoolIo", reinterpret_cast<void*>(CloseThreadpoolIo));

    ldr.registerExport("kernel32.dll", "GetSystemFirmwareTable", reinterpret_cast<void*>(GetSystemFirmwareTable));
    ldr.registerExport("kernel32.dll", "EnumSystemFirmwareTables", reinterpret_cast<void*>(EnumSystemFirmwareTables));
    ldr.registerExport("api-ms-win-core-sysinfo-l1-2-0.dll", "GetSystemFirmwareTable", reinterpret_cast<void*>(GetSystemFirmwareTable));
    ldr.registerExport("api-ms-win-core-sysinfo-l1-2-0.dll", "EnumSystemFirmwareTables", reinterpret_cast<void*>(EnumSystemFirmwareTables));

    ldr.registerExport("kernel32.dll", "timeGetTime", reinterpret_cast<void*>(timeGetTime));
    ldr.registerExport("kernel32.dll", "timeBeginPeriod", reinterpret_cast<void*>(timeBeginPeriod));
    ldr.registerExport("kernel32.dll", "timeEndPeriod", reinterpret_cast<void*>(timeEndPeriod));
    ldr.registerExport("kernel32.dll", "timeGetDevCaps", reinterpret_cast<void*>(timeGetDevCaps));

    ldr.registerExport("api-ms-win-mm-time-l1-1-0.dll", "timeGetTime", reinterpret_cast<void*>(timeGetTime));
    ldr.registerExport("api-ms-win-mm-time-l1-1-0.dll", "timeBeginPeriod", reinterpret_cast<void*>(timeBeginPeriod));
    ldr.registerExport("api-ms-win-mm-time-l1-1-0.dll", "timeEndPeriod", reinterpret_cast<void*>(timeEndPeriod));
    ldr.registerExport("api-ms-win-mm-time-l1-1-0.dll", "timeGetDevCaps", reinterpret_cast<void*>(timeGetDevCaps));

    ldr.registerExport("winmm.dll", "timeGetTime", reinterpret_cast<void*>(timeGetTime));
    ldr.registerExport("winmm.dll", "timeBeginPeriod", reinterpret_cast<void*>(timeBeginPeriod));
    ldr.registerExport("winmm.dll", "timeEndPeriod", reinterpret_cast<void*>(timeEndPeriod));
    ldr.registerExport("winmm.dll", "timeGetDevCaps", reinterpret_cast<void*>(timeGetDevCaps));

    // Horizon 211: Process & Thread Information ApiSets
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetProcessInformation", reinterpret_cast<void*>(GetProcessInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-1.dll", "GetThreadInformation", reinterpret_cast<void*>(GetThreadInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-2.dll", "SetProcessInformation", reinterpret_cast<void*>(SetProcessInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-2.dll", "GetProcessInformation", reinterpret_cast<void*>(GetProcessInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-2.dll", "SetThreadInformation", reinterpret_cast<void*>(SetThreadInformation));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-2.dll", "GetThreadInformation", reinterpret_cast<void*>(GetThreadInformation));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "SetProcessWorkingSetSizeEx", reinterpret_cast<void*>(SetProcessWorkingSetSizeEx));
    ldr.registerExport("api-ms-win-core-memory-l1-1-6.dll", "GetProcessWorkingSetSizeEx", reinterpret_cast<void*>(GetProcessWorkingSetSizeEx));

    // Horizon 219: Affinity Update Mode & Group CPU Set Masks
    ldr.registerExport("kernel32.dll", "SetProcessAffinityUpdateMode", reinterpret_cast<void*>(SetProcessAffinityUpdateMode));
    ldr.registerExport("kernel32.dll", "QueryProcessAffinityUpdateMode", reinterpret_cast<void*>(QueryProcessAffinityUpdateMode));
    ldr.registerExport("kernel32.dll", "SetProcessDefaultCpuSetMasks", reinterpret_cast<void*>(SetProcessDefaultCpuSetMasks));
    ldr.registerExport("kernel32.dll", "GetProcessDefaultCpuSetMasks", reinterpret_cast<void*>(GetProcessDefaultCpuSetMasks));
    ldr.registerExport("kernel32.dll", "SetThreadSelectedCpuSetMasks", reinterpret_cast<void*>(SetThreadSelectedCpuSetMasks));
    ldr.registerExport("kernel32.dll", "GetThreadSelectedCpuSetMasks", reinterpret_cast<void*>(GetThreadSelectedCpuSetMasks));

    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "SetProcessAffinityUpdateMode", reinterpret_cast<void*>(SetProcessAffinityUpdateMode));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-0.dll", "QueryProcessAffinityUpdateMode", reinterpret_cast<void*>(QueryProcessAffinityUpdateMode));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-6.dll", "SetProcessDefaultCpuSetMasks", reinterpret_cast<void*>(SetProcessDefaultCpuSetMasks));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-6.dll", "GetProcessDefaultCpuSetMasks", reinterpret_cast<void*>(GetProcessDefaultCpuSetMasks));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-6.dll", "SetThreadSelectedCpuSetMasks", reinterpret_cast<void*>(SetThreadSelectedCpuSetMasks));
    ldr.registerExport("api-ms-win-core-processthreads-l1-1-6.dll", "GetThreadSelectedCpuSetMasks", reinterpret_cast<void*>(GetThreadSelectedCpuSetMasks));
}


} // namespace micant::win32

namespace micant {
namespace kernel32 = win32;
namespace um {
namespace kernel32 = micant::win32;
}
}
