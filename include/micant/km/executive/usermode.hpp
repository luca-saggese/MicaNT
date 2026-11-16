#pragma once

/**
 * @file usermode.hpp
 * @brief Horizon 121: Sovereign User-Mode Process & Thread Management, PEB/TEB Architecture,
 *        MSR_GS_BASE/SWAPGS Privilege Bridge & System Service Dispatch Table (SSDT) (Unit #93).
 *
 * Implements the canonical Windows NT 64-bit user-mode environment:
 * - Process Environment Block (PEB64) with OS version 10.0.26100, ProcessParameters, Heaps
 * - Thread Environment Block (TEB64) with GS:[0x30] Self pointer, GS:[0x60] PEB pointer, LastError
 * - MSR_GS_BASE (0xC0000101) & MSR_KERNEL_GS_BASE (0xC0000102) hardware SWAPGS model
 * - System Service Descriptor Table (SSDT / KiServiceTable) with fast syscall dispatch
 * - Multi-process management for smss.exe (PID 368), csrss.exe (PID 480), and cmd.exe (PID 1024)
 * - Safe memory probing (ProbeForRead / ProbeForWrite) across userland page boundaries
 *
 * Clean-room ISO C++23 implementation. Zero proprietary or decompiled code.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <array>
#include <span>
#include <memory>
#include <new>

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/hal/cpu.hpp"
#include "micant/km/hal/bootvid.hpp"
#include "micant/shared/boot_driver.hpp"
#include "micant/shared/user_graphics.hpp"
#include "micant/um/native/pe.hpp"

namespace micant::um {

// Canonical x86-64 User-Mode Virtual Address Space Boundaries
inline constexpr uint64_t MM_HIGHEST_USER_ADDRESS = 0x00007FFFFFFFFFFFULL;
inline constexpr uint64_t MM_LOWEST_USER_ADDRESS  = 0x0000000000010000ULL; // 64KB Null Allocation Trap

static inline micant::NtStatus ProbeUserMemory(const void* addr, size_t len, uint32_t alignment = 1) noexcept {
    if (!addr || len == 0) return micant::NtStatus::Success;
    uint64_t va = reinterpret_cast<uint64_t>(addr);
    if (alignment > 1 && (va & (alignment - 1)) != 0) {
        return micant::NtStatus::DatatypeMisalignment;
    }
    if (va < MM_LOWEST_USER_ADDRESS) {
        return micant::NtStatus::AccessViolation;
    }
    if (va + len < va || va + len > MM_HIGHEST_USER_ADDRESS) {
        return micant::NtStatus::AccessViolation;
    }
    return micant::NtStatus::Success;
}

} // namespace micant::um

namespace micant::um {

// ============================================================================
// 1. Canonical Windows NT 64-bit PEB & TEB Data Structures
// ============================================================================

#pragma pack(push, 1)

/**
 * @brief Process Environment Block (PEB64).
 * Mapped into user space and referenced via TEB64->ProcessEnvironmentBlock (GS:[0x60]).
 */
struct Peb64 {
    uint8_t  inheritedAddressSpace{0};
    uint8_t  readImageFileExecOptions{0};
    uint8_t  beingDebugged{0};
    uint8_t  bitField{0};
    uint32_t mutant{0};
    uint64_t imageBaseAddress{0x00007FF600000000ULL};
    uint64_t ldr{0};
    uint64_t processParameters{0};
    uint64_t subSystemData{0};
    uint64_t processHeap{0};
    uint64_t fastPebLock{0};
    uint64_t atlThunkSListPtr{0};
    uint64_t ifeoKey{0};
    uint32_t crossProcessFlags{0};
    uint32_t userSharedInfoPtr{0};
    uint32_t systemReserved{0};
    uint32_t atlThunkSListPtr32{0};
    uint64_t apiSetMap{0};
    uint32_t tlsExpansionCounter{0};
    uint32_t tlsBitmap{0};
    uint32_t tlsBitmapBits[2]{0, 0};
    uint64_t readOnlySharedMemoryBase{0};
    uint64_t sharedData{0};
    uint64_t readOnlyStaticServerData{0};
    uint64_t ansiCodePageData{0};
    uint64_t oemCodePageData{0};
    uint64_t unicodeCaseTableData{0};
    uint32_t numberOfProcessors{8}; // 4C/8T Xeon E3-1270 v6
    uint32_t ntGlobalFlag{0};
    uint64_t criticalSectionTimeout{0};
    uint64_t heapSegmentReserve{0};
    uint64_t heapSegmentCommit{0};
    uint64_t heapDeCommitTotalFreeThreshold{0};
    uint64_t heapDeCommitFreeBlockThreshold{0};
    uint32_t numberOfHeaps{0};
    uint32_t maximumNumberOfHeaps{16};
    uint64_t processHeaps{0};
    uint64_t gdiSharedHandleTable{0};
    uint64_t processStarterHelper{0};
    uint32_t gdiDCAttributeList{0};
    uint64_t loaderLock{0};
    uint32_t osMajorVersion{10};
    uint32_t osMinorVersion{0};
    uint16_t osBuildNumber{26100}; // Windows 11 24H2 baseline
    uint16_t osCsdVersion{0};
    uint32_t osPlatformId{2};      // VER_PLATFORM_WIN32_NT
    uint32_t imageSubsystem{3};    // IMAGE_SUBSYSTEM_WINDOWS_CUI
    uint32_t imageSubsystemMajorVersion{10};
    uint32_t imageSubsystemMinorVersion{0};
    uint64_t activeProcessAffinityMask{0xFF}; // 8 logical threads
    uint32_t gdiHandleBuffer[30]{0};
    uint64_t postProcessInitRoutine{0};
    uint64_t tlsExpansionBitmap{0};
    uint32_t tlsExpansionBitmapBits[32]{0};
    uint32_t sessionId{1};
};

/**
 * @brief Thread Environment Block (TEB64).
 * Mapped into user space at GS:[0x00].
 * GS:[0x30] = self pointer to TEB64.
 * GS:[0x60] = pointer to PEB64.
 * GS:[0x68] = last error value.
 */
struct Teb64 {
    // NT_TIB (0x00 - 0x38)
    uint64_t exceptionList{0};
    uint64_t stackBase{0};
    uint64_t stackLimit{0};
    uint64_t subSystemTib{0};
    uint64_t fiberData{0};
    uint64_t arbitraryUserPointer{0};
    uint64_t self{0}; // Offset 0x30: Must point to this Teb64 instance

    // Environment & IDs (0x38 - 0x60)
    uint64_t environmentPointer{0};
    uint64_t uniqueProcessId{0};
    uint64_t uniqueThreadId{0};
    uint64_t activeRpcHandle{0};
    uint64_t threadLocalStoragePointer{0};

    // PEB & Error (0x60 - 0x70)
    uint64_t processEnvironmentBlock{0}; // Offset 0x60: Must point to Peb64
    uint32_t lastErrorValue{0};          // Offset 0x68: Win32 SetLastError / GetLastError
    uint32_t countOfOwnedCriticalSections{0};
    uint64_t csrClientThread{0};
    uint64_t win32ThreadInfo{0};
    uint32_t user32Reserved[26]{0};
    uint32_t userReserved[5]{0};
    uint64_t wow32Reserved{0};
    uint32_t currentLocale{0x0409}; // en-US
    uint32_t fpSoftwareStatusRegister{0};
    uint64_t reservedForDebuggerInstrumentation[16]{0};
    uint8_t reservedToDeallocationStack[0x1478 - 0x18C]{};
    uint64_t deallocationStack{0};
    uint64_t tlsSlots[64]{};
    uint8_t reservedToGuaranteedStackBytes[0x1748 - 0x1680]{};
    uint32_t guaranteedStackBytes{0};
    uint8_t reservedToTlsExpansionSlots[0x1780 - 0x174C]{};
    uint64_t tlsExpansionSlots{0};
};

static_assert(offsetof(Teb64, processEnvironmentBlock) == 0x60);
static_assert(offsetof(Teb64, uniqueProcessId) == 0x40);
static_assert(offsetof(Teb64, uniqueThreadId) == 0x48);
static_assert(offsetof(Teb64, lastErrorValue) == 0x68);
static_assert(offsetof(Teb64, deallocationStack) == 0x1478);
static_assert(offsetof(Teb64, tlsSlots) == 0x1480);
static_assert(offsetof(Teb64, guaranteedStackBytes) == 0x1748);
static_assert(offsetof(Teb64, tlsExpansionSlots) == 0x1780);

#pragma pack(pop)

// ============================================================================
// 2. User-Mode Process Context & Lifecycle State
// ============================================================================

// Canonical Virtual Memory Protection Constants (Win32 / NT)
inline constexpr uint32_t MM_PAGE_NOACCESS          = 0x01;
inline constexpr uint32_t MM_PAGE_READONLY          = 0x02;
inline constexpr uint32_t MM_PAGE_READWRITE         = 0x04;
inline constexpr uint32_t MM_PAGE_WRITECOPY         = 0x08;
inline constexpr uint32_t MM_PAGE_EXECUTE           = 0x10;
inline constexpr uint32_t MM_PAGE_EXECUTE_READ      = 0x20;
inline constexpr uint32_t MM_PAGE_EXECUTE_READWRITE = 0x40;
inline constexpr uint32_t MM_PAGE_EXECUTE_WRITECOPY = 0x80;
inline constexpr uint32_t MM_PAGE_GUARD             = 0x100;
inline constexpr uint32_t MM_PAGE_NOCACHE           = 0x200;
inline constexpr uint32_t MM_PAGE_WRITECOMBINE      = 0x400;

// Canonical Section Allocation Attributes (Win32 / NT)
inline constexpr uint32_t MM_SEC_FILE           = 0x00800000;
inline constexpr uint32_t MM_SEC_IMAGE          = 0x01000000;
inline constexpr uint32_t MM_SEC_RESERVE        = 0x04000000;
inline constexpr uint32_t MM_SEC_COMMIT         = 0x08000000;
inline constexpr uint32_t MM_SEC_NOCACHE        = 0x10000000;
inline constexpr uint32_t MM_SEC_WRITECOMBINE   = 0x40000000;

struct VadEntry64 {
    uint64_t startVa{0};
    uint64_t endVa{0};
    uint32_t protection{0};
    bool     committed{false};
    bool     isGuard{false};
    bool     isCopyOnWrite{false};
    bool     isStandby{false};
    bool     isModified{false};
    uint64_t sectionHandle{0};
    uint64_t sectionOffset{0};
    char     name[32]{};
};

struct UserSectionDescriptor64 {
    uint64_t handle{0};
    size_t   size{0};
    uint32_t allocationAttributes{0};
    uint32_t pageProtection{0};
    std::array<uint8_t, 65536> backingData{};
    size_t   backingDataSize{0};
    char     name[64]{};
    uint32_t referenceCount{0};
};

enum class UserProcessState : uint32_t {
    Ready,
    Running,
    Terminated,
    Faulted
};

struct UserProcessContext64 {
    uint32_t         pid{0};
    uint32_t         parentPid{0};
    char             imageName[32]{};
    uint64_t         codeBaseVa{0};
    size_t           codeSize{0};
    uint64_t         stackTopVa{0};
    size_t           stackSize{0};
    uint64_t         pebVa{0};
    uint64_t         tebVa{0};
    Peb64            peb{};
    Teb64            teb{};
    UserProcessState state{UserProcessState::Ready};
    micant::NtStatus exitStatus{micant::NtStatus::Success};
    uint64_t         syscallCount{0};
    uint64_t         lastSyscallNumber{0};
    uint64_t         lastSyscallArg1{0};
    uint64_t         lastSyscallArg2{0};
    uint64_t         verifiedCpl{0};
    uint64_t         verifiedUserCs{0};
    bool             pebVerifiedByR3{false};
    bool             tebVerifiedByR3{false};

    // Horizon 148: Per-Process CR3 Hardware Isolation & Virtual Address Descriptors (VAD)
    uint64_t         directoryTableBase{0}; // Physical CR3 PML4 base
    std::array<uint64_t, 512> pml4{};      // PML4 Page Directory entries
    static constexpr size_t MAX_VADS = 32;
    std::array<VadEntry64, MAX_VADS> vads{};
    size_t           vadCount{0};

    // Telemetry & Diagnostics
    uint64_t         pageFaultCount{0};
    uint64_t         demandPagesCommitted{0};
    uint64_t         stackGuardFaultsResolved{0};
    uint64_t         accessViolationsTrapped{0};

    // Horizon 149: Copy-on-Write, Section Views & Working Set Metrics
    uint64_t         copyOnWriteFaultsResolved{0};
    uint64_t         softPageFaultsResolved{0};
    uint64_t         hardPageFaultsResolved{0};
    size_t           workingSetSize{0};     // Current physical resident pages in bytes
    size_t           peakWorkingSetSize{0}; // Peak resident working set size in bytes

    bool addVad(
        uint64_t startVa,
        uint64_t endVa,
        uint32_t protection,
        bool committed,
        bool isGuard,
        const char* vadName,
        bool isCopyOnWrite = false,
        uint64_t sectionHandle = 0,
        uint64_t sectionOffset = 0
    ) noexcept {
        if (vadCount >= MAX_VADS || startVa >= endVa) return false;
        VadEntry64& v = vads[vadCount++];
        v.startVa = startVa;
        v.endVa = endVa;
        v.protection = protection;
        v.committed = committed;
        v.isGuard = isGuard;
        v.isCopyOnWrite = isCopyOnWrite;
        v.isStandby = false;
        v.isModified = false;
        v.sectionHandle = sectionHandle;
        v.sectionOffset = sectionOffset;
        size_t i = 0;
        for (; i + 1 < sizeof(v.name) && vadName && vadName[i] != '\0'; ++i) {
            v.name[i] = vadName[i];
        }
        v.name[i] = '\0';

        if (committed) {
            workingSetSize += (endVa - startVa);
            if (workingSetSize > peakWorkingSetSize) {
                peakWorkingSetSize = workingSetSize;
            }
        }
        return true;
    }

    [[nodiscard]] VadEntry64* findVad(uint64_t va) noexcept {
        for (size_t i = 0; i < vadCount; ++i) {
            if (va >= vads[i].startVa && va < vads[i].endVa) {
                return &vads[i];
            }
        }
        return nullptr;
    }

    [[nodiscard]] const VadEntry64* findVad(uint64_t va) const noexcept {
        for (size_t i = 0; i < vadCount; ++i) {
            if (va >= vads[i].startVa && va < vads[i].endVa) {
                return &vads[i];
            }
        }
        return nullptr;
    }
};

// ----------------------------------------------------------------------------
// Win32k GDI / Framebuffer Graphics System Service Structures (Ring 3 <-> Ring 0)
// ----------------------------------------------------------------------------

// ============================================================================
// 3. System Service Descriptor Table (SSDT) & Fast Syscall Engine
// ============================================================================

inline constexpr uint32_t SSN_NT_TERMINATE_PROCESS        = 0x0001; // Process Exit
inline constexpr uint32_t SSN_NT_QUERY_SYSTEM_INFORMATION = 0x0011; // Hardware / CPU / Memory telemetry
inline constexpr uint32_t SSN_NT_READ_FILE                = 0x0023; // VFS Read
inline constexpr uint32_t SSN_NT_WRITE_FILE               = 0x0025; // Console / Serial Print
inline constexpr uint32_t SSN_NT_ALLOCATE_VIRTUAL_MEMORY  = 0x0055; // Userland Heap / Stack expansion
inline constexpr uint32_t SSN_NT_CLOSE                    = 0x001E; // Handle close
inline constexpr uint32_t SSN_NT_YIELD_EXECUTION          = 0x0046; // Cooperative CPU yield
inline constexpr uint32_t SSN_NT_CREATE_SECTION           = 0x004A; // ZwCreateSection
inline constexpr uint32_t SSN_NT_MAP_VIEW_OF_SECTION      = 0x0028; // ZwMapViewOfSection
inline constexpr uint32_t SSN_NT_UNMAP_VIEW_OF_SECTION    = 0x002A; // ZwUnmapViewOfSection
inline constexpr uint32_t SSN_NT_EMPTY_WORKING_SET        = 0x008F; // ZwEmptyWorkingSet
inline constexpr uint32_t SSN_NT_GDI_GET_VIDEO_INFO       = 0x0080; // Query screen dimensions & framebuffer format
inline constexpr uint32_t SSN_NT_GDI_PRESENT_SURFACE      = 0x0081; // Scanline burst blit to physical VRAM
inline constexpr uint32_t SSN_NT_GDI_PRESENT_COMPOSITION  = 0x0085; // Ordered independent desktop surfaces

struct SsdtEntry {
    uint32_t    ssn{0};
    const char* name{""};
    uint32_t    argCount{0};
    uint64_t    invocationCount{0};
};

class SsdtTable {
private:
    std::array<SsdtEntry, 32> m_entries{};
    size_t m_entryCount{0};

public:
    constexpr SsdtTable() noexcept {
        addEntry(SSN_NT_TERMINATE_PROCESS, "NtTerminateProcess", 2);
        addEntry(SSN_NT_QUERY_SYSTEM_INFORMATION, "NtQuerySystemInformation", 4);
        addEntry(SSN_NT_READ_FILE, "NtReadFile", 9);
        addEntry(SSN_NT_WRITE_FILE, "NtWriteFile", 9);
        addEntry(SSN_NT_ALLOCATE_VIRTUAL_MEMORY, "NtAllocateVirtualMemory", 6);
        addEntry(SSN_NT_CLOSE, "NtClose", 1);
        addEntry(SSN_NT_YIELD_EXECUTION, "NtYieldExecution", 0);
        addEntry(SSN_NT_CREATE_SECTION, "NtCreateSection", 7);
        addEntry(SSN_NT_MAP_VIEW_OF_SECTION, "NtMapViewOfSection", 10);
        addEntry(SSN_NT_UNMAP_VIEW_OF_SECTION, "NtUnmapViewOfSection", 2);
        addEntry(SSN_NT_EMPTY_WORKING_SET, "NtEmptyWorkingSet", 1);
        addEntry(SSN_NT_GDI_GET_VIDEO_INFO, "NtGdiGetVideoInfo", 1);
        addEntry(SSN_NT_GDI_PRESENT_SURFACE, "NtGdiPresentSurface", 1);
        addEntry(SSN_NT_GDI_PRESENT_COMPOSITION, "NtGdiPresentComposition", 1);
    }

    constexpr void addEntry(uint32_t ssn, const char* name, uint32_t argCount) noexcept {
        if (m_entryCount < m_entries.size()) {
            m_entries[m_entryCount++] = SsdtEntry{ssn, name, argCount, 0};
        }
    }

    [[nodiscard]] const SsdtEntry* find(uint32_t ssn) const noexcept {
        for (size_t i = 0; i < m_entryCount; ++i) {
            if (m_entries[i].ssn == ssn) {
                return &m_entries[i];
            }
        }
        return nullptr;
    }

    void recordInvocation(uint32_t ssn) noexcept {
        for (size_t i = 0; i < m_entryCount; ++i) {
            if (m_entries[i].ssn == ssn) {
                m_entries[i].invocationCount++;
                return;
            }
        }
    }

    [[nodiscard]] size_t getCount() const noexcept { return m_entryCount; }
    [[nodiscard]] const SsdtEntry& getEntry(size_t idx) const noexcept { return m_entries[idx]; }
};

// ============================================================================
// 4. Sovereign User-Mode Executive Manager (UserProcessManager64)
// ============================================================================

class UserProcessManager64 {
private:
    static constexpr size_t MAX_PROCESSES = 16;
    std::array<UserProcessContext64, MAX_PROCESSES> m_processes{};
    size_t    m_processCount{0};
    uint32_t  m_activePid{0};
    SsdtTable m_ssdt{};
    uint64_t  m_totalSyscallsDispatched{0};
    uint64_t  m_totalProcessesSpawned{0};
    uint64_t  m_totalProcessesTerminated{0};
    bootvid::BootVideoDriver* m_videoDriver{nullptr};
    boot_driver::DisplayDevice* m_displayDevice{nullptr};
    uint64_t  m_totalFramesPresented{0};
    uint64_t  m_totalFramesPresentedByDevice{0};
    uint64_t  m_totalPixelsBlitted{0};
    uint64_t  m_totalPageFaults{0};
    uint64_t  m_totalDemandPagesCommitted{0};
    uint64_t  m_totalStackGuardResolutions{0};
    uint64_t  m_totalAccessViolations{0};

    // Horizon 149: Sovereign User-Mode Section Objects & Working Set Telemetry
    static constexpr size_t MAX_USER_SECTIONS = 32;
    std::array<UserSectionDescriptor64, MAX_USER_SECTIONS> m_userSections{};
    size_t    m_userSectionCount{0};
    uint64_t  m_nextSectionHandle{0x1000};
    uint64_t  m_totalSectionsCreated{0};
    uint64_t  m_totalSectionViewsMapped{0};
    uint64_t  m_totalSectionViewsUnmapped{0};
    uint64_t  m_totalCopyOnWriteFaults{0};
    uint64_t  m_totalSoftPageFaults{0};
    uint64_t  m_totalHardPageFaults{0};
    uint64_t  m_totalWorkingSetTrims{0};

public:
    static UserProcessManager64& get() noexcept {
        static UserProcessManager64 s_instance;
        return s_instance;
    }

    UserProcessManager64() noexcept {
        reset();
    }

    void reset(bool populateSystem = true) noexcept {
        m_processes.fill(UserProcessContext64{});
        m_processCount = 0;
        m_activePid = populateSystem ? 4 : 0;
        m_totalSyscallsDispatched = 0;
        m_totalProcessesSpawned = 0;
        m_totalProcessesTerminated = 0;
        m_totalFramesPresented = 0;
        m_totalPixelsBlitted = 0;
        m_totalPageFaults = 0;
        m_totalDemandPagesCommitted = 0;
        m_totalStackGuardResolutions = 0;
        m_totalAccessViolations = 0;

        m_userSections.fill(UserSectionDescriptor64{});
        m_userSectionCount = 0;
        m_nextSectionHandle = 0x1000;
        m_totalSectionsCreated = 0;
        m_totalSectionViewsMapped = 0;
        m_totalSectionViewsUnmapped = 0;
        m_totalCopyOnWriteFaults = 0;
        m_totalSoftPageFaults = 0;
        m_totalHardPageFaults = 0;
        m_totalWorkingSetTrims = 0;

        if (populateSystem) {
            // Core NT Executive Processes
            (void)createProcess("System Idle", 0, 0, 0, 0, 0, 0, 0, 0);
            (void)createProcess("System", 4, 0, 0xFFFFF80000000000ULL, 0x100000, 0, 0, 0, 0);
        }
    }

    [[nodiscard]] UserProcessContext64* createProcess(
        const char* imageName,
        uint32_t pid,
        uint32_t parentPid,
        uint64_t codeVa,
        size_t codeSize,
        uint64_t stackTopVa,
        size_t stackSize,
        uint64_t pebVa,
        uint64_t tebVa
    ) noexcept {
        if (m_processCount >= MAX_PROCESSES) return nullptr;

        UserProcessContext64& proc = m_processes[m_processCount++];
        proc.pid = pid;
        proc.parentPid = parentPid;
        proc.codeBaseVa = codeVa;
        proc.codeSize = codeSize;
        proc.stackTopVa = stackTopVa;
        proc.stackSize = stackSize;
        proc.pebVa = pebVa;
        proc.tebVa = tebVa;
        proc.state = UserProcessState::Ready;
        proc.exitStatus = micant::NtStatus::Success;
        proc.syscallCount = 0;

        // Copy Image Name
        size_t i = 0;
        for (; i + 1 < sizeof(proc.imageName) && imageName && imageName[i] != '\0'; ++i) {
            proc.imageName[i] = imageName[i];
        }
        proc.imageName[i] = '\0';

        // 1. Initialize PEB64
        proc.peb = Peb64{};
        proc.peb.imageBaseAddress = codeVa;
        proc.peb.numberOfProcessors = 8; // Xeon E3-1270 v6
        proc.peb.osMajorVersion = 10;
        proc.peb.osMinorVersion = 0;
        proc.peb.osBuildNumber = 26100;
        proc.peb.sessionId = (pid == 368) ? 0 : 1; // smss.exe is Session 0, interactive apps Session 1

        // 2. Initialize TEB64
        proc.teb = Teb64{};
        proc.teb.stackBase = stackTopVa;
        proc.teb.stackLimit = stackTopVa - stackSize;
        proc.teb.self = tebVa; // Canonical GS:[0x30] self pointer
        proc.teb.uniqueProcessId = pid;
        proc.teb.uniqueThreadId = pid + 1;
        proc.teb.processEnvironmentBlock = pebVa; // Canonical GS:[0x60] PEB pointer
        proc.teb.lastErrorValue = 0;

        // 3. Horizon 148: Hardware CR3 Page Directory & Address Space Initialization
        proc.directoryTableBase = 0x100000ULL + static_cast<uint64_t>(pid) * 0x1000ULL;
        proc.pml4.fill(0);

        // Mirror Canonical NT High-Half Kernel PML4 entries with Supervisor bit (U/S = 0)
        // High-half kernel entry (Slot 496: 0xFFFFF80000000000)
        proc.pml4[496] = 0x0000000000102003ULL; // P=1, R/W=1, U/S=0 (Supervisor)
        // High-half HAL entry (Slot 510: 0xFFFFFF0000000000)
        proc.pml4[510] = 0x0000000000104003ULL; // P=1, R/W=1, U/S=0 (Supervisor)
        // High-half legacy entry (Slot 511: 0xFFFFFF8000000000)
        proc.pml4[511] = 0x0000000000106003ULL; // P=1, R/W=1, U/S=0 (Supervisor)

        // Lower-half user space PML4 entry (Slot 0: 0x0000000000000000 - 0x0000007FFFFFFFFF)
        // User-mode accessible (U/S = 1, R/W = 1, P = 1)
        proc.pml4[0] = (0x0000000000200007ULL + static_cast<uint64_t>(pid) * 0x1000ULL);

        // Register Default Process Virtual Address Descriptors (VAD)
        if (codeVa != 0 && codeSize > 0) {
            proc.addVad(codeVa, codeVa + codeSize, MM_PAGE_EXECUTE_READ, true, false, ".text");
        }
        if (stackTopVa != 0 && stackSize > 0x1000) {
            uint64_t stackBottom = stackTopVa - stackSize;
            proc.addVad(stackBottom, stackBottom + 0x1000, MM_PAGE_GUARD | MM_PAGE_READWRITE, false, true, "StackGuard");
            proc.addVad(stackBottom + 0x1000, stackTopVa, MM_PAGE_READWRITE, true, false, "UserStack");
        } else if (stackTopVa != 0 && stackSize > 0) {
            proc.addVad(stackTopVa - stackSize, stackTopVa, MM_PAGE_READWRITE, true, false, "UserStack");
        }
        if (pebVa != 0) {
            proc.addVad(pebVa, pebVa + 0x1000, MM_PAGE_READWRITE, true, false, "PEB");
        }
        if (tebVa != 0) {
            proc.addVad(tebVa, tebVa + 0x1000, MM_PAGE_READWRITE, true, false, "TEB");
        }

        m_totalProcessesSpawned++;
        return &proc;
    }

    [[nodiscard]] UserProcessContext64* findProcess(uint32_t pid) noexcept {
        for (size_t i = 0; i < m_processCount; ++i) {
            if (m_processes[i].pid == pid) {
                return &m_processes[i];
            }
        }
        return nullptr;
    }

    [[nodiscard]] const UserProcessContext64* getProcess(size_t index) const noexcept {
        if (index < m_processCount) return &m_processes[index];
        return nullptr;
    }

    [[nodiscard]] size_t getProcessCount() const noexcept { return m_processCount; }
    [[nodiscard]] uint32_t getActivePid() const noexcept { return m_activePid; }
    void setActivePid(uint32_t pid) noexcept { m_activePid = pid; }

    /**
     * @brief Central System Call Dispatcher.
     * Invoked from KiSystemCall64Entry when userland issues 'syscall'.
     */
    micant::NtStatus dispatchSyscall(
        uint64_t ssn,
        uint64_t arg1,
        uint64_t arg2,
        uint64_t arg3,
        uint64_t arg4,
        uint64_t cpl,
        uint64_t userCs
    ) noexcept {
        m_totalSyscallsDispatched++;
        m_ssdt.recordInvocation(static_cast<uint32_t>(ssn));

        UserProcessContext64* activeProc = findProcess(m_activePid);
        if (activeProc) {
            activeProc->syscallCount++;
            activeProc->lastSyscallNumber = ssn;
            activeProc->lastSyscallArg1 = arg1;
            activeProc->lastSyscallArg2 = arg2;
            activeProc->verifiedCpl = cpl;
            activeProc->verifiedUserCs = userCs;
            activeProc->state = UserProcessState::Running;
        }

        switch (ssn) {
            case SSN_NT_QUERY_SYSTEM_INFORMATION: {
                // arg1: SystemInformationClass, arg2: SystemInformation buffer pointer, arg3: bufferLength, arg4: returnLength ptr
                if (activeProc) {
                    activeProc->tebVerifiedByR3 = true;
                    activeProc->pebVerifiedByR3 = true;
                }
                // Memory probe check
                if (arg2 != 0) {
                    micant::NtStatus probeStat = ProbeUserMemory(
                        reinterpret_cast<void*>(arg2),
                        static_cast<size_t>(arg3),
                        sizeof(uint32_t)
                    );
                    if (probeStat != micant::NtStatus::Success) {
                        return probeStat;
                    }
                }
                return micant::NtStatus::Success;
            }

            case SSN_NT_WRITE_FILE: {
                // arg1: FileHandle/Sink (1 = stdout), arg2: Buffer pointer, arg3: ByteCount
                if (arg2 != 0 && arg3 > 0) {
                    micant::NtStatus probeStat = ProbeUserMemory(
                        reinterpret_cast<const void*>(arg2),
                        static_cast<size_t>(arg3),
                        1
                    );
                    if (probeStat != micant::NtStatus::Success) {
                        return probeStat;
                    }
                }
                return micant::NtStatus::Success;
            }

            case SSN_NT_TERMINATE_PROCESS: {
                // arg1: ProcessHandle (0 = current), arg2: ExitStatus
                if (activeProc) {
                    activeProc->state = UserProcessState::Terminated;
                    activeProc->exitStatus = static_cast<micant::NtStatus>(arg2);
                    m_totalProcessesTerminated++;
                }
                return micant::NtStatus::Success;
            }

            case SSN_NT_ALLOCATE_VIRTUAL_MEMORY: {
                // arg1: ProcessHandle, arg2: BaseAddress ptr, arg3: ZeroBits, arg4: RegionSize ptr
                return micant::NtStatus::Success;
            }

            case SSN_NT_CLOSE: {
                return micant::NtStatus::Success;
            }

            case SSN_NT_YIELD_EXECUTION: {
                return micant::NtStatus::Success;
            }

            case SSN_NT_CREATE_SECTION: {
                if (arg1 != 0) {
                    uint64_t handle = 0;
                    size_t maxSz = (arg4 > 0) ? static_cast<size_t>(arg4) : 0x10000;
                    micant::NtStatus st = createSection(handle, maxSz, MM_SEC_COMMIT, MM_PAGE_READWRITE, "UserSection");
                    if (NT_SUCCESS(st)) {
                        *reinterpret_cast<uint64_t*>(arg1) = handle;
                    }
                    return st;
                }
                return micant::NtStatus::InvalidParameter;
            }

            case SSN_NT_MAP_VIEW_OF_SECTION: {
                if (arg1 != 0 && arg3 != 0) {
                    uint64_t baseVa = *reinterpret_cast<uint64_t*>(arg3);
                    size_t viewSz = (arg4 != 0) ? *reinterpret_cast<size_t*>(arg4) : 0x10000;
                    micant::NtStatus st = mapViewOfSection(arg1, m_activePid, baseVa, 0, 0, viewSz, MM_PAGE_READWRITE, "MappedView");
                    if (NT_SUCCESS(st)) {
                        *reinterpret_cast<uint64_t*>(arg3) = baseVa;
                    }
                    return st;
                }
                return micant::NtStatus::InvalidParameter;
            }

            case SSN_NT_UNMAP_VIEW_OF_SECTION: {
                return unmapViewOfSection(m_activePid, arg2);
            }

            case SSN_NT_EMPTY_WORKING_SET: {
                return trimProcessWorkingSet(m_activePid);
            }

            case SSN_NT_GDI_GET_VIDEO_INFO: {
                // arg1: pointer to UserVideoInfo64
                if (arg1 == 0) return micant::NtStatus::InvalidParameter;
                micant::NtStatus probe = ProbeUserMemory(reinterpret_cast<void*>(arg1), sizeof(UserVideoInfo64), sizeof(uint32_t));
                if (probe != micant::NtStatus::Success) return probe;

                auto* info = reinterpret_cast<UserVideoInfo64*>(arg1);
                if (m_displayDevice && m_displayDevice->PresentFrame) {
                    info->width = m_displayDevice->Width;
                    info->height = m_displayDevice->Height;
                    info->pitch = m_displayDevice->Pitch;
                    info->bpp = 32;
                    info->pixelFormat = m_displayDevice->PixelFormat;
                    const auto backend = static_cast<boot_driver::DisplayBackend>(
                        __atomic_load_n(reinterpret_cast<const uint32_t*>(
                            &m_displayDevice->Backend), __ATOMIC_ACQUIRE));
                    info->transportBackend = backend == boot_driver::DisplayBackend::Virtio2D
                        ? static_cast<uint32_t>(DisplayTransportBackend::Virtio2D)
                        : (backend == boot_driver::DisplayBackend::Virgl ||
                           backend == boot_driver::DisplayBackend::VirglAccelerated)
                            ? static_cast<uint32_t>(DisplayTransportBackend::Virgl)
                            : backend == boot_driver::DisplayBackend::Gop
                                ? static_cast<uint32_t>(DisplayTransportBackend::Gop)
                                : static_cast<uint32_t>(DisplayTransportBackend::Unknown);
                    info->compositionBackend = backend == boot_driver::DisplayBackend::VirglAccelerated
                        ? static_cast<uint32_t>(DisplayCompositionBackend::VirglAccelerated)
                        : backend == boot_driver::DisplayBackend::Virgl
                            ? static_cast<uint32_t>(DisplayCompositionBackend::VirglAvailable)
                            : static_cast<uint32_t>(DisplayCompositionBackend::Software);
                    info->physicalBase = 0; // Device scanout is not CPU-addressable framebuffer memory.
                } else if (m_videoDriver && m_videoDriver->isInitialized()) {
                    info->width = m_videoDriver->getWidth();
                    info->height = m_videoDriver->getHeight();
                    info->pitch = m_videoDriver->getPitch();
                    info->bpp = 32;
                    info->pixelFormat = m_videoDriver->getPixelFormat();
                    info->transportBackend = static_cast<uint32_t>(DisplayTransportBackend::Unknown);
                    info->compositionBackend = static_cast<uint32_t>(DisplayCompositionBackend::Software);
                    info->physicalBase = reinterpret_cast<uint64_t>(m_videoDriver->getRawBuffer());
                } else {
                    info->width = 1024;
                    info->height = 768;
                    info->pitch = 1024;
                    info->bpp = 32;
                    info->pixelFormat = 1;
                    info->transportBackend = static_cast<uint32_t>(DisplayTransportBackend::Unknown);
                    info->compositionBackend = static_cast<uint32_t>(DisplayCompositionBackend::Software);
                    info->physicalBase = 0;
                }
                return micant::NtStatus::Success;
            }

            case SSN_NT_GDI_PRESENT_SURFACE: {
                // arg1: pointer to UserPresentSurfaceParams64
                if (arg1 == 0) return micant::NtStatus::InvalidParameter;
                micant::NtStatus probe = ProbeUserMemory(reinterpret_cast<void*>(arg1), sizeof(UserPresentSurfaceParams64), sizeof(uint32_t));
                if (probe != micant::NtStatus::Success) return probe;

                auto* params = reinterpret_cast<UserPresentSurfaceParams64*>(arg1);
                if (params->surfaceBufferVa == 0 || params->width == 0 || params->height == 0) {
                    return micant::NtStatus::InvalidParameter;
                }

                uint32_t sp = (params->pitch > 0) ? params->pitch : params->width;
                size_t bufferBytes = static_cast<size_t>(sp) * params->height * sizeof(uint32_t);
                micant::NtStatus bufProbe = ProbeUserMemory(reinterpret_cast<void*>(params->surfaceBufferVa), bufferBytes, sizeof(uint32_t));
                if (bufProbe != micant::NtStatus::Success) return bufProbe;

                const bool presentedByDevice = m_displayDevice && m_displayDevice->PresentFrame &&
                    m_displayDevice->PresentFrame(m_displayDevice->Context,
                        reinterpret_cast<const uint32_t*>(params->surfaceBufferVa),
                        params->width, params->height, params->pitch ? params->pitch : params->width,
                        static_cast<int32_t>(params->dstX), static_cast<int32_t>(params->dstY));
                if (presentedByDevice) ++m_totalFramesPresentedByDevice;
                if (!presentedByDevice && m_videoDriver && m_videoDriver->isInitialized()) {
                    m_videoDriver->blit(
                        params->dstX,
                        params->dstY,
                        params->width,
                        params->height,
                        reinterpret_cast<const uint32_t*>(params->surfaceBufferVa),
                        params->pitch
                    );
                }
                m_totalFramesPresented++;
                m_totalPixelsBlitted += (static_cast<uint64_t>(params->width) * params->height);
                return micant::NtStatus::Success;
            }

            case SSN_NT_GDI_PRESENT_COMPOSITION: {
                if (arg1 == 0) return micant::NtStatus::InvalidParameter;
                const auto probe = ProbeUserMemory(reinterpret_cast<void*>(arg1),
                    sizeof(UserPresentCompositionParams64), alignof(uint32_t));
                if (probe != micant::NtStatus::Success) return probe;
                const auto* params = reinterpret_cast<const UserPresentCompositionParams64*>(arg1);
                if (!params->baseSurfaceBufferVa || !params->width || !params->height ||
                    params->width > 8192 || params->height > 8192 || params->pitch > 8192 ||
                    params->pitch < params->width || params->layerCount > 64 ||
                    (params->layerCount && !params->layersVa)) {
                    return micant::NtStatus::InvalidParameter;
                }
                const uint64_t baseBytes = static_cast<uint64_t>(params->pitch) * params->height * sizeof(uint32_t);
                if (baseBytes > SIZE_MAX) return micant::NtStatus::InvalidParameter;
                auto status = ProbeUserMemory(reinterpret_cast<void*>(params->baseSurfaceBufferVa),
                    static_cast<size_t>(baseBytes), alignof(uint32_t));
                if (status != micant::NtStatus::Success) return status;
                size_t layerBytes = static_cast<size_t>(params->layerCount) * sizeof(boot_driver::CompositionLayer);
                if (params->layerCount) {
                    status = ProbeUserMemory(reinterpret_cast<void*>(params->layersVa), layerBytes,
                        alignof(boot_driver::CompositionLayer));
                    if (status != micant::NtStatus::Success) return status;
                }
                auto* layers = reinterpret_cast<const boot_driver::CompositionLayer*>(params->layersVa);
                for (uint32_t i = 0; i < params->layerCount; ++i) {
                    const auto& layer = layers[i];
                    if (!layer.Pixels || !layer.Width || !layer.Height || layer.Width > 8192 ||
                        layer.Height > 8192 || layer.Pitch > 8192 || layer.Pitch < layer.Width)
                        return micant::NtStatus::InvalidParameter;
                    const uint64_t bytes = static_cast<uint64_t>(layer.Pitch) * layer.Height * sizeof(uint32_t);
                    if (bytes > SIZE_MAX) return micant::NtStatus::InvalidParameter;
                    status = ProbeUserMemory(const_cast<uint32_t*>(layer.Pixels),
                        static_cast<size_t>(bytes), alignof(uint32_t));
                    if (status != micant::NtStatus::Success) return status;
                }
                if (!m_displayDevice || !m_displayDevice->PresentComposition)
                    return micant::NtStatus::NotImplemented;
                if (!m_displayDevice->PresentComposition(m_displayDevice->Context,
                        reinterpret_cast<const uint32_t*>(params->baseSurfaceBufferVa),
                        params->width, params->height, params->pitch, layers, params->layerCount))
                    return micant::NtStatus::DeviceNotReady;
                ++m_totalFramesPresented;
                ++m_totalFramesPresentedByDevice;
                m_totalPixelsBlitted += static_cast<uint64_t>(params->width) * params->height;
                return micant::NtStatus::Success;
            }

            default:
                return micant::NtStatus::NotImplemented;
        }
    }

    void setVideoDriver(bootvid::BootVideoDriver* driver) noexcept { m_videoDriver = driver; }
    void setDisplayDevice(boot_driver::DisplayDevice* device) noexcept { m_displayDevice = device; }
    [[nodiscard]] boot_driver::DisplayDevice* getDisplayDevice() const noexcept { return m_displayDevice; }
    [[nodiscard]] bootvid::BootVideoDriver* getVideoDriver() const noexcept { return m_videoDriver; }
    [[nodiscard]] uint64_t getTotalFramesPresented() const noexcept { return m_totalFramesPresented; }
    [[nodiscard]] uint64_t getTotalFramesPresentedByDevice() const noexcept { return m_totalFramesPresentedByDevice; }
    [[nodiscard]] uint64_t getTotalPixelsBlitted() const noexcept { return m_totalPixelsBlitted; }

    [[nodiscard]] const SsdtTable& getSsdt() const noexcept { return m_ssdt; }
    [[nodiscard]] uint64_t getTotalSyscallsDispatched() const noexcept { return m_totalSyscallsDispatched; }
    [[nodiscard]] uint64_t getTotalProcessesSpawned() const noexcept { return m_totalProcessesSpawned; }
    [[nodiscard]] uint64_t getTotalProcessesTerminated() const noexcept { return m_totalProcessesTerminated; }

    /**
     * @brief Horizon 148 & 149: Hardware Interrupt / Exception #PF (Vector 14) Page Fault Engine.
     * Evaluates VAD tree, resolves Copy-on-Write (CoW), soft page faults (Standby list),
     * demand paging, guard page stack expansion, or raises STATUS_ACCESS_VIOLATION.
     */
    micant::NtStatus resolvePageFault(uint32_t pid, uint64_t faultVa, uint32_t errorCode) noexcept {
        m_totalPageFaults++;
        UserProcessContext64* proc = findProcess(pid);
        if (!proc) {
            m_totalAccessViolations++;
            return micant::NtStatus::AccessViolation;
        }

        proc->pageFaultCount++;
        VadEntry64* vad = proc->findVad(faultVa);
        if (!vad) {
            // Unmapped memory address space
            proc->accessViolationsTrapped++;
            m_totalAccessViolations++;
            return micant::NtStatus::AccessViolation;
        }

        bool isWrite = (errorCode & 0x02) != 0;

        // Horizon 149: Copy-on-Write Resolution (MM_PAGE_WRITECOPY / MM_PAGE_EXECUTE_WRITECOPY)
        if (vad->isCopyOnWrite) {
            if (isWrite) {
                vad->isCopyOnWrite = false;
                vad->protection = (vad->protection == MM_PAGE_EXECUTE_WRITECOPY) 
                    ? MM_PAGE_EXECUTE_READWRITE 
                    : MM_PAGE_READWRITE;
                vad->isModified = true;
                proc->copyOnWriteFaultsResolved++;
                m_totalCopyOnWriteFaults++;
                return micant::NtStatus::Success;
            }
            // Read access to CoW is permitted without splitting
            return micant::NtStatus::Success;
        }

        // Horizon 149: Soft Page Fault Resolution (Standby List restoration)
        if (vad->isStandby) {
            vad->isStandby = false;
            size_t pageBytes = (vad->endVa > vad->startVa) ? (vad->endVa - vad->startVa) : 0x1000;
            proc->workingSetSize += pageBytes;
            if (proc->workingSetSize > proc->peakWorkingSetSize) {
                proc->peakWorkingSetSize = proc->workingSetSize;
            }
            proc->softPageFaultsResolved++;
            m_totalSoftPageFaults++;
            return micant::NtStatus::Success;
        }

        // Check write permission violation on read-only / execute-only memory
        if (isWrite && !(vad->protection & (MM_PAGE_READWRITE | MM_PAGE_EXECUTE_READWRITE))) {
            proc->accessViolationsTrapped++;
            m_totalAccessViolations++;
            return micant::NtStatus::AccessViolation;
        }

        // Check if hitting Guard Page (Stack Expansion)
        if (vad->isGuard) {
            vad->isGuard = false;
            vad->committed = true;
            vad->protection = MM_PAGE_READWRITE;
            // Expand stack downwards if address space allows
            if (vad->startVa >= 0x1000) {
                proc->addVad(vad->startVa - 0x1000, vad->startVa, MM_PAGE_GUARD | MM_PAGE_READWRITE, false, true, "StackGuard");
            }
            proc->stackGuardFaultsResolved++;
            m_totalStackGuardResolutions++;
            return micant::NtStatus::Success;
        }

        // Demand Paging (Uncommitted heap or data page fault)
        if (!vad->committed) {
            vad->committed = true;
            proc->demandPagesCommitted++;
            m_totalDemandPagesCommitted++;
            return micant::NtStatus::Success;
        }

        return micant::NtStatus::Success;
    }

    micant::NtStatus createSection(
        uint64_t& outHandle,
        size_t maximumSize,
        uint32_t allocationAttributes,
        uint32_t pageProtection,
        const char* sectionName = nullptr
    ) noexcept {
        if (maximumSize == 0) return micant::NtStatus::InvalidParameter;
        if (m_userSectionCount >= MAX_USER_SECTIONS) return micant::NtStatus::InsufficientResources;

        UserSectionDescriptor64& sec = m_userSections[m_userSectionCount++];
        outHandle = m_nextSectionHandle;
        m_nextSectionHandle += 4;

        sec.handle = outHandle;
        sec.size = maximumSize;
        sec.allocationAttributes = allocationAttributes;
        sec.pageProtection = pageProtection;
        sec.backingDataSize = std::min(maximumSize, sec.backingData.size());
        sec.backingData.fill(0);
        sec.referenceCount = 1;

        if (sectionName) {
            size_t i = 0;
            for (; i + 1 < sizeof(sec.name) && sectionName[i] != '\0'; ++i) {
                sec.name[i] = sectionName[i];
            }
            sec.name[i] = '\0';
        }

        m_totalSectionsCreated++;
        return micant::NtStatus::Success;
    }

    [[nodiscard]] UserSectionDescriptor64* findSection(uint64_t handle) noexcept {
        for (size_t i = 0; i < m_userSectionCount; ++i) {
            if (m_userSections[i].handle == handle) {
                return &m_userSections[i];
            }
        }
        return nullptr;
    }

    micant::NtStatus mapViewOfSection(
        uint64_t sectionHandle,
        uint32_t pid,
        uint64_t& baseAddress,
        size_t commitSize,
        uint64_t sectionOffset,
        size_t viewSize,
        uint32_t win32Protect,
        const char* viewName = nullptr
    ) noexcept {
        UserSectionDescriptor64* sec = findSection(sectionHandle);
        if (!sec) return micant::NtStatus::InvalidHandle;

        UserProcessContext64* proc = findProcess(pid);
        if (!proc) return micant::NtStatus::ProcessIsTerminating;

        if (viewSize == 0 || (sectionOffset + viewSize) > sec->size) {
            viewSize = (sec->size > sectionOffset) ? (sec->size - sectionOffset) : 0x1000;
        }

        if (baseAddress == 0) {
            baseAddress = 0x00007FF640000000ULL + static_cast<uint64_t>(pid) * 0x10000000ULL + static_cast<uint64_t>(proc->vadCount) * 0x100000ULL;
        }

        uint32_t effectiveProtect = (win32Protect != 0) ? win32Protect : sec->pageProtection;
        bool isCow = (effectiveProtect == MM_PAGE_WRITECOPY || effectiveProtect == MM_PAGE_EXECUTE_WRITECOPY);

        bool added = proc->addVad(
            baseAddress,
            baseAddress + viewSize,
            effectiveProtect,
            true,  // committed
            false, // isGuard
            viewName ? viewName : (sec->name[0] ? sec->name : "SectionView"),
            isCow,
            sectionHandle,
            sectionOffset
        );

        if (!added) return micant::NtStatus::InsufficientResources;

        sec->referenceCount++;
        m_totalSectionViewsMapped++;
        return micant::NtStatus::Success;
    }

    micant::NtStatus unmapViewOfSection(uint32_t pid, uint64_t baseAddress) noexcept {
        UserProcessContext64* proc = findProcess(pid);
        if (!proc) return micant::NtStatus::ProcessIsTerminating;

        for (size_t i = 0; i < proc->vadCount; ++i) {
            if (proc->vads[i].startVa == baseAddress) {
                uint64_t secHandle = proc->vads[i].sectionHandle;
                size_t regionSize = proc->vads[i].endVa - proc->vads[i].startVa;
                if (proc->workingSetSize >= regionSize) {
                    proc->workingSetSize -= regionSize;
                } else {
                    proc->workingSetSize = 0;
                }

                // Shift remaining VADs
                for (size_t j = i; j + 1 < proc->vadCount; ++j) {
                    proc->vads[j] = proc->vads[j + 1];
                }
                proc->vadCount--;

                if (secHandle != 0) {
                    auto* sec = findSection(secHandle);
                    if (sec && sec->referenceCount > 0) {
                        sec->referenceCount--;
                    }
                }

                m_totalSectionViewsUnmapped++;
                return micant::NtStatus::Success;
            }
        }
        return micant::NtStatus::NotFound;
    }

    micant::NtStatus trimProcessWorkingSet(uint32_t pid) noexcept {
        UserProcessContext64* proc = findProcess(pid);
        if (!proc) return micant::NtStatus::ProcessIsTerminating;

        size_t pagesTrimmed = 0;
        for (size_t i = 0; i < proc->vadCount; ++i) {
            if (proc->vads[i].committed && !proc->vads[i].isGuard && !proc->vads[i].isStandby) {
                proc->vads[i].isStandby = true;
                pagesTrimmed += (proc->vads[i].endVa - proc->vads[i].startVa);
            }
        }

        if (proc->workingSetSize >= pagesTrimmed) {
            proc->workingSetSize -= pagesTrimmed;
        } else {
            proc->workingSetSize = 0;
        }

        m_totalWorkingSetTrims++;
        return micant::NtStatus::Success;
    }

    [[nodiscard]] size_t getUserSectionCount() const noexcept { return m_userSectionCount; }
    [[nodiscard]] uint64_t getTotalSectionsCreated() const noexcept { return m_totalSectionsCreated; }
    [[nodiscard]] uint64_t getTotalSectionViewsMapped() const noexcept { return m_totalSectionViewsMapped; }
    [[nodiscard]] uint64_t getTotalSectionViewsUnmapped() const noexcept { return m_totalSectionViewsUnmapped; }
    [[nodiscard]] uint64_t getTotalCopyOnWriteFaults() const noexcept { return m_totalCopyOnWriteFaults; }
    [[nodiscard]] uint64_t getTotalSoftPageFaults() const noexcept { return m_totalSoftPageFaults; }
    [[nodiscard]] uint64_t getTotalHardPageFaults() const noexcept { return m_totalHardPageFaults; }
    [[nodiscard]] uint64_t getTotalWorkingSetTrims() const noexcept { return m_totalWorkingSetTrims; }
    [[nodiscard]] uint64_t getTotalPageFaults() const noexcept { return m_totalPageFaults; }
    [[nodiscard]] uint64_t getTotalDemandPagesCommitted() const noexcept { return m_totalDemandPagesCommitted; }
    [[nodiscard]] uint64_t getTotalStackGuardResolutions() const noexcept { return m_totalStackGuardResolutions; }
    [[nodiscard]] uint64_t getTotalAccessViolations() const noexcept { return m_totalAccessViolations; }
};

// ============================================================================
// 4. Horizon 122: Sovereign Ring 3 User Trap Frame, Thread Context & Scheduler
// ============================================================================

#pragma pack(push, 1)

/**
 * @brief Canonical 64-bit User-Mode Trap Frame (KTRAP_FRAME).
 * Preserves hardware and software execution context across privilege transitions
 * and cooperative / preemptive thread context switches.
 */
struct UserTrapFrame64 {
    // General Purpose Registers (GPRs)
    uint64_t rax{0};
    uint64_t rbx{0};
    uint64_t rcx{0};
    uint64_t rdx{0};
    uint64_t rsi{0};
    uint64_t rdi{0};
    uint64_t rbp{0};
    uint64_t r8{0};
    uint64_t r9{0};
    uint64_t r10{0};
    uint64_t r11{0};
    uint64_t r12{0};
    uint64_t r13{0};
    uint64_t r14{0};
    uint64_t r15{0};

    // Hardware IRETQ context
    uint64_t rip{0};
    uint64_t cs{0x0033};
    uint64_t rflags{0x0202};
    uint64_t rsp{0};
    uint64_t ss{0x002B};

    // Segment registers
    uint16_t ds{0x002B};
    uint16_t es{0x002B};
    uint16_t fs{0x0053};
    uint16_t gs{0x002B};
    uint32_t padding{0};

    // GS Base addresses
    uint64_t userGsBase{0};    // MSR 0xC0000101 (TEB64 address)
    uint64_t kernelGsBase{0};  // MSR 0xC0000102 (KPCR address)
};

#pragma pack(pop)

/**
 * @brief User-mode thread execution states.
 */
enum class UserThreadState : uint8_t {
    Initialized,
    Ready,
    Running,
    Waiting,
    Yielded,
    Terminated
};

/**
 * @brief Sovereign User-Mode Thread Execution Context (ETHREAD/KTHREAD Ring 3).
 */
struct UserThreadContext64 {
    uint32_t tid{0};
    uint32_t parentPid{0};
    uint8_t  priority{16}; // Normal priority (0-31)
    uint8_t  quantum{6};   // Default quantum ticks
    uint8_t  quantumRemaining{6};
    UserThreadState state{UserThreadState::Initialized};
    UserTrapFrame64 trapFrame{};
    uint64_t userStackTop{0};
    uint64_t kernelStackTop{0};
    uint64_t tebAddress{0};
    uint64_t contextSwitches{0};
    uint64_t totalYields{0};
    micant::NtStatus exitStatus{micant::NtStatus::Success};
    char name[32]{};
};

/**
 * @brief Sovereign Multi-Process / Multi-Thread Cooperative Scheduler (Unit #94).
 * Coordinates execution across multiple Ring 3 userland processes and threads.
 */
class UserThreadScheduler64 {
private:
    static constexpr size_t MAX_USER_THREADS = 16;
    UserThreadContext64 m_threads[MAX_USER_THREADS]{};
    size_t m_threadCount{0};
    uint32_t m_activeTid{0};
    uint64_t m_totalContextSwitches{0};
    uint64_t m_totalYields{0};
    uint64_t m_totalQuantumPreemptions{0};
    uint64_t m_totalAddressSpaceSwitches{0};
    uint64_t m_activeCr3{0x104000ULL}; // Initial System CR3

    UserThreadScheduler64() = default;

public:
    static UserThreadScheduler64& get() noexcept {
        static UserThreadScheduler64 instance;
        return instance;
    }

    void reset(bool populateSystem = true) noexcept {
        m_threadCount = 0;
        m_activeTid = populateSystem ? 4 : 0;
        m_totalContextSwitches = 0;
        m_totalYields = 0;
        m_totalQuantumPreemptions = 0;
        m_totalAddressSpaceSwitches = 0;
        m_activeCr3 = populateSystem ? 0x104000ULL : 0;
        for (size_t i = 0; i < MAX_USER_THREADS; ++i) {
            m_threads[i] = UserThreadContext64{};
        }
        if (populateSystem) {
            createThread(0, 0, 0, 0, 0, 0, "IdleThread");
            createThread(4, 4, 16, 0xFFFFF80000000000ULL, 0, 0, "SystemWorker");
        }
    }

    bool createThread(
        uint32_t tid,
        uint32_t parentPid,
        uint8_t  priority,
        uint64_t userRip,
        uint64_t userRsp,
        uint64_t tebAddress,
        const char* name
    ) noexcept {
        if (m_threadCount >= MAX_USER_THREADS) return false;

        // Check if thread already exists
        for (size_t i = 0; i < m_threadCount; ++i) {
            if (m_threads[i].tid == tid) return false;
        }

        UserThreadContext64& t = m_threads[m_threadCount++];
        t.tid = tid;
        t.parentPid = parentPid;
        t.priority = priority;
        t.quantum = 6;
        t.quantumRemaining = 6;
        t.state = UserThreadState::Ready;
        t.userStackTop = userRsp;
        t.tebAddress = tebAddress;

        // Initialize Trap Frame for Ring 3 entry
        t.trapFrame.rip = userRip;
        t.trapFrame.cs = 0x0033;
        t.trapFrame.rflags = 0x0202;
        t.trapFrame.rsp = userRsp;
        t.trapFrame.ss = 0x002B;
        t.trapFrame.userGsBase = tebAddress;

        if (name) {
            size_t len = 0;
            while (name[len] && len < sizeof(t.name) - 1) {
                t.name[len] = name[len];
                len++;
            }
            t.name[len] = '\0';
        }

        if (m_activeTid == 0) {
            m_activeTid = tid;
            t.state = UserThreadState::Running;
            auto* proc = UserProcessManager64::get().findProcess(parentPid);
            if (proc && proc->directoryTableBase != 0) {
                m_activeCr3 = proc->directoryTableBase;
            }
        }

        return true;
    }

    [[nodiscard]] UserThreadContext64* findThread(uint32_t tid) noexcept {
        for (size_t i = 0; i < m_threadCount; ++i) {
            if (m_threads[i].tid == tid) return &m_threads[i];
        }
        return nullptr;
    }

    [[nodiscard]] const UserThreadContext64* getThread(size_t index) const noexcept {
        if (index < m_threadCount) return &m_threads[index];
        return nullptr;
    }

    [[nodiscard]] size_t getThreadCount() const noexcept { return m_threadCount; }
    [[nodiscard]] uint32_t getActiveTid() const noexcept { return m_activeTid; }
    [[nodiscard]] uint64_t getTotalContextSwitches() const noexcept { return m_totalContextSwitches; }
    [[nodiscard]] uint64_t getTotalYields() const noexcept { return m_totalYields; }
    [[nodiscard]] uint64_t getTotalQuantumPreemptions() const noexcept { return m_totalQuantumPreemptions; }
    [[nodiscard]] uint64_t getTotalAddressSpaceSwitches() const noexcept { return m_totalAddressSpaceSwitches; }
    [[nodiscard]] uint64_t getActiveCr3() const noexcept { return m_activeCr3; }

    /**
     * @brief Horizon 148: Canonical NT 32-Queue Priority Thread Selection (0-31).
     * Searches from highest priority (31 Real-Time) down to 0 (Idle).
     */
    [[nodiscard]] uint32_t selectNextThread() noexcept {
        for (int prio = 31; prio >= 0; --prio) {
            size_t startIdx = 0;
            for (size_t i = 0; i < m_threadCount; ++i) {
                if (m_threads[i].tid == m_activeTid) {
                    startIdx = (i + 1) % m_threadCount;
                    break;
                }
            }

            for (size_t step = 0; step < m_threadCount; ++step) {
                size_t idx = (startIdx + step) % m_threadCount;
                if (m_threads[idx].priority == static_cast<uint8_t>(prio) &&
                    (m_threads[idx].state == UserThreadState::Ready ||
                     m_threads[idx].state == UserThreadState::Yielded)) {
                    return m_threads[idx].tid;
                }
            }
        }
        return m_activeTid;
    }

    /**
     * @brief Horizon 148: Hardware Timer Preemption & Quantum Decay Engine.
     * Decrements remaining quantum; upon exhaustion, invokes selectNextThread()
     * and performs a preemptive context switch.
     */
    void tickQuantum(uint8_t elapsedTicks = 1) noexcept {
        UserThreadContext64* current = findThread(m_activeTid);
        if (!current || current->state != UserThreadState::Running) return;

        if (current->quantumRemaining > elapsedTicks) {
            current->quantumRemaining -= elapsedTicks;
        } else {
            // Quantum exhaustion -> Hardware Preemption
            m_totalQuantumPreemptions++;
            current->quantumRemaining = current->quantum;
            current->state = UserThreadState::Ready;

            uint32_t nextTid = selectNextThread();
            if (nextTid != m_activeTid) {
                switchContext(nextTid);
            } else {
                current->state = UserThreadState::Running;
            }
        }
    }

    /**
     * @brief Switch execution context to target thread and swap CR3 address space if parent PID changes.
     */
    bool switchContext(uint32_t targetTid) noexcept {
        if (targetTid == m_activeTid) return true;

        UserThreadContext64* current = findThread(m_activeTid);
        UserThreadContext64* target = findThread(targetTid);

        if (!target || target->state == UserThreadState::Terminated) {
            return false;
        }

        if (current && current->state == UserThreadState::Running) {
            current->state = UserThreadState::Ready;
        }

        // Horizon 148: Hardware CR3 Page Directory Switching across different address spaces
        if (!current || current->parentPid != target->parentPid) {
            auto* targetProc = UserProcessManager64::get().findProcess(target->parentPid);
            if (targetProc && targetProc->directoryTableBase != 0) {
                m_activeCr3 = targetProc->directoryTableBase;
                m_totalAddressSpaceSwitches++;
            }
        }

        target->state = UserThreadState::Running;
        target->contextSwitches++;
        m_activeTid = targetTid;
        m_totalContextSwitches++;

        // Synchronize UserProcessManager active PID
        UserProcessManager64::get().setActivePid(target->parentPid);
        return true;
    }

    /**
     * @brief Yield execution to next ready thread (SSN_NT_YIELD_EXECUTION).
     */
    uint32_t yieldCurrentThread() noexcept {
        m_totalYields++;
        UserThreadContext64* current = findThread(m_activeTid);
        if (current) {
            current->totalYields++;
            current->state = UserThreadState::Yielded;
        }

        uint32_t nextTid = selectNextThread();
        if (nextTid != m_activeTid) {
            switchContext(nextTid);
            return nextTid;
        }

        if (current && current->state != UserThreadState::Terminated) {
            current->state = UserThreadState::Running;
        }
        return m_activeTid;
    }

    /**
     * @brief Terminate current thread.
     */
    bool terminateCurrentThread(micant::NtStatus exitStatus) noexcept {
        UserThreadContext64* current = findThread(m_activeTid);
        if (!current) return false;

        current->state = UserThreadState::Terminated;
        current->exitStatus = exitStatus;

        uint32_t nextTid = selectNextThread();
        if (nextTid != m_activeTid && nextTid != 0) {
            switchContext(nextTid);
            return true;
        }

        m_activeTid = 0;
        return true;
    }
};

// ============================================================================
// 5. Horizon 123: Sovereign PE32+ Userland Binary Loader & Executive Bridge (Unit #95)
// ============================================================================

struct UserLoadedImageInfo {
    uint64_t imageBaseVa{0};
    uint64_t entryPointVa{0};
    uint64_t sizeOfImage{0};
    uint32_t numberOfSections{0};
    uint32_t boundImportsCount{0};
    uint32_t relocationsAppliedCount{0};
    uint32_t pid{0};
    uint32_t tid{0};
    char     imageName[32]{};
    bool     isRelocated{false};
    bool     headersValid{false};
    bool     sectionsMapped{false};
};

// Borrows boot-owned files and storage; never installs synthetic import targets.
class NativeUserImageSet64 {
public:
    using ApiSetHostResolver = NtStatus (*)(void* context, std::string_view contract,
                                            std::string_view importingModule,
                                            std::string_view& host) noexcept;
    struct Module {
        std::string_view name{};
        pe::PeImageView64 view{};
        std::span<uint8_t> image{};
        uint64_t base{};
        std::unique_ptr<uint8_t[]> metadata{};
        [[nodiscard]] std::span<const uint8_t> immutableImage() const noexcept {
            return {metadata.get(), image.size()};
        }
    };
    struct BindingFailure {
        std::string_view library{};
        std::string_view symbol{};
        std::string_view importer{};
        uint16_t ordinal{};
    };
    struct ForwarderFailure {
        std::string_view library{};
        std::string_view symbol{};
        std::string_view importer{};
        NtStatus status{NtStatus::Success};
    };

    void setApiSetHostResolver(void* context, ApiSetHostResolver resolver) noexcept {
        apiSetContext_ = context;
        apiSetResolver_ = resolver;
    }

    // Adopt pages already cloned by Mm without remapping or rebinding them.
    // The caller owns the copied file and name for this set's lifetime.
    NtStatus addClonedModule(std::string_view name, std::span<const uint8_t> file,
                             std::span<uint8_t> image, uint64_t base) noexcept {
        if (bound_ || count_ == modules_.size() || name.empty()) return NtStatus::InvalidParameter;
        Module candidate;
        candidate.name = name;
        const auto status = candidate.view.inspect(file);
        if (status != NtStatus::Success) return status;
        const size_t size = candidate.view.headers().optionalHeader.sizeOfImage;
        if (!base || image.size() != size || reinterpret_cast<uintptr_t>(image.data()) != base)
            return NtStatus::InvalidImageFormat;
        candidate.image = image;
        candidate.base = base;
        candidate.metadata.reset(new (std::nothrow_t{}) uint8_t[size]);
        if (!candidate.metadata) return NtStatus::NoMemory;
        const auto mapStatus = candidate.view.map({candidate.metadata.get(), size}, base);
        if (mapStatus != NtStatus::Success) return mapStatus;
        modules_[count_++] = std::move(candidate);
        return NtStatus::Success;
    }

    void finishClonedModules() noexcept { bound_ = true; }

    NtStatus replaceClonedModule(size_t index, std::string_view name,
                                 std::span<const uint8_t> file,
                                 std::span<uint8_t> image, uint64_t base) noexcept {
        if (!bound_ || index >= count_ || name.empty() || !base) return NtStatus::InvalidParameter;
        Module replacement;
        replacement.name = name;
        auto status = replacement.view.inspect(file);
        if (status != NtStatus::Success) return status;
        if (replacement.view.headers().optionalHeader.sizeOfImage != image.size() ||
            reinterpret_cast<uintptr_t>(image.data()) != base)
            return NtStatus::InvalidImageFormat;
        replacement.image = image;
        replacement.base = base;
        for (size_t other = 0; other < count_; ++other) {
            if (other == index || !equal(name, modules_[other].name, true)) continue;
            return NtStatus::ObjectNameCollision;
        }
        replacement.metadata.reset(new (std::nothrow_t{}) uint8_t[image.size()]);
        if (!replacement.metadata) return NtStatus::NoMemory;
        status = replacement.view.map({replacement.metadata.get(), image.size()}, base);
        if (status != NtStatus::Success) return status;
        modules_[index] = std::move(replacement);
        bound_ = false;
        return NtStatus::Success;
    }

    NtStatus bindModule(size_t index) noexcept {
        if (bound_ || index >= count_) return NtStatus::InvalidParameter;
        failure_ = {};
        auto& current = modules_[index];
        for (unsigned pass = 0; pass < 2; ++pass) {
            const auto status = current.view.bindImports(current.image,
                [&](std::string_view library, std::string_view name,
                    uint16_t ordinal, uint64_t& address) {
                    const NtStatus result = resolve(library, name, ordinal, address,
                                                    nullptr, current.name);
                    if (result != NtStatus::Success) failure_ = {library, name, current.name, ordinal};
                    return result;
                }, pass == 1, current.immutableImage());
            if (status != NtStatus::Success) return status;
        }
        bound_ = true;
        return NtStatus::Success;
    }

    NtStatus addModule(std::string_view name, std::span<const uint8_t> file,
                       std::span<uint8_t> storage, uint64_t base) noexcept {
        if (bound_) return NtStatus::InvalidParameter;
        if (count_ == modules_.size()) return NtStatus::InsufficientResources;
        if (name.empty() || base < MM_LOWEST_USER_ADDRESS || base % 65536) {
            return NtStatus::InvalidParameter;
        }
        Module candidate;
        candidate.name = name;
        NtStatus status = candidate.view.inspect(file);
        if (status != NtStatus::Success) return status;
        const uint64_t size = candidate.view.headers().optionalHeader.sizeOfImage;
        if (base > MM_HIGHEST_USER_ADDRESS || size > MM_HIGHEST_USER_ADDRESS - base + 1) {
            return NtStatus::InvalidParameter;
        }
        if (storage.size() < size) return NtStatus::BufferTooSmall;
        for (size_t index = 0; index < count_; ++index) {
            const auto& other = modules_[index];
            if (equal(name, other.name, true)) return NtStatus::ObjectNameCollision;
            if (base < other.base + other.image.size() && other.base < base + size) {
                return NtStatus::Conflict;
            }
            const auto first = reinterpret_cast<uintptr_t>(storage.data());
            const auto second = reinterpret_cast<uintptr_t>(other.image.data());
            if (first >= second ? first - second < other.image.size()
                                : second - first < size) {
                return NtStatus::Conflict;
            }
        }
        status = candidate.view.map(storage, base);
        if (status != NtStatus::Success) return status;
        candidate.image = storage.first(static_cast<size_t>(size));
        candidate.base = base;
        candidate.metadata.reset(new (std::nothrow_t{}) uint8_t[static_cast<size_t>(size)]);
        if (!candidate.metadata) return NtStatus::NoMemory;
        status = candidate.view.map({candidate.metadata.get(), static_cast<size_t>(size)}, base);
        if (status != NtStatus::Success) return status;
        modules_[count_++] = std::move(candidate);
        bound_ = false;
        return NtStatus::Success;
    }

    NtStatus bindAll() noexcept {
        if (bound_) return NtStatus::Success;
        failure_ = {};
        if (!count_) return NtStatus::InvalidParameter;
        // Validate the entire closure before overwriting an IAT used as its INT.
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (size_t index = 0; index < count_; ++index) {
                auto& current = modules_[index];
                const NtStatus status = current.view.bindImports(current.image,
                    [&](std::string_view library, std::string_view name,
                        uint16_t ordinal, uint64_t& address) {
                        const NtStatus result = resolve(library, name, ordinal, address,
                                                        nullptr, current.name);
                        if (result != NtStatus::Success) failure_ = {library, name, current.name, ordinal};
                        return result;
                    }, pass == 1, current.immutableImage());
                if (status != NtStatus::Success) return status;
            }
        }
        bound_ = true;
        return NtStatus::Success;
    }

    NtStatus resolve(std::string_view library, std::string_view name,
                     uint16_t ordinal, uint64_t& address,
                     std::string_view* missingForwarderLibrary = nullptr,
                     std::string_view importingModule = {}) const noexcept {
        address = 0;
        if (missingForwarderLibrary) *missingForwarderLibrary = {};
        struct Visited {
            size_t module{};
            std::string_view symbol{};
            uint16_t ordinal{};
        };
        std::array<Visited, 64> visited{};
        size_t depth{};
        size_t apiSetDepth{};
        bool followedForwarder = false;
        for (;;) {
            if (isApiSetName(library)) {
                if (!apiSetResolver_ || apiSetDepth == 16) return NtStatus::NotSupported;
                std::string_view host;
                const auto apiSetStatus = apiSetResolver_(apiSetContext_, library,
                                                          importingModule, host);
                if (apiSetStatus != NtStatus::Success) return apiSetStatus;
                if (host.empty() || equal(host, library, true)) return NtStatus::NotSupported;
                library = host;
                ++apiSetDepth;
                continue;
            }
            const size_t index = findModule(library);
            if (index == count_) {
                if (followedForwarder && missingForwarderLibrary)
                    *missingForwarderLibrary = library;
                return NtStatus::NoSuchFile;
            }
            for (size_t previous = 0; previous < depth; ++previous) {
                const auto& key = visited[previous];
                if (key.module == index && equal(key.symbol, name) && key.ordinal == ordinal) {
                    return NtStatus::CircularReference;
                }
            }
            if (depth == visited.size()) return NtStatus::NotSupported;
            visited[depth++] = {index, name, ordinal};
            const auto& current = modules_[index];
            pe::PeImageView64::ExportTarget target;
            const NtStatus status = current.view.findExport(current.immutableImage(), current.base,
                                                           name, ordinal, target);
            if (status != NtStatus::Success) return status;
            if (target.address) {
                address = target.address;
                return NtStatus::Success;
            }
            const auto forwarder = target.forwarder;
            size_t separator = forwarder.size();
            for (size_t byte = 0; byte < forwarder.size(); ++byte) {
                if (forwarder[byte] == '.') separator = byte;
            }
            if (separator == 0 || separator >= forwarder.size() - 1) {
                return NtStatus::InvalidImageFormat;
            }
            library = {forwarder.data(), separator};
            name = {forwarder.data() + separator + 1, forwarder.size() - separator - 1};
            // API-set aliases are selected for the module containing the
            // forwarder, not the original importer's module. Keep that
            // context while following an export chain.
            importingModule = current.name;
            followedForwarder = true;
            ordinal = 0;
            if (name[0] == '#') {
                uint32_t value{};
                if (name.size() == 1) return NtStatus::InvalidImageFormat;
                for (size_t digit = 1; digit < name.size(); ++digit) {
                    if (name[digit] < '0' || name[digit] > '9') {
                        return NtStatus::InvalidImageFormat;
                    }
                    value = value * 10 + static_cast<uint32_t>(name[digit] - '0');
                    if (value > UINT16_MAX) return NtStatus::InvalidImageFormat;
                }
                ordinal = static_cast<uint16_t>(value);
                name = {};
            }
        }
    }

    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] bool containsModule(std::string_view name) const noexcept {
        return findModule(name) != count_;
    }
    template <typename Visitor>
    NtStatus forEachMissingForwarderLibrary(Visitor&& visitor,
                                            size_t firstModule = 0,
                                            size_t lastModule = SIZE_MAX,
                                            ForwarderFailure* failure = nullptr) const noexcept {
        if (failure) *failure = {};
        if (firstModule > count_ || lastModule < firstModule) return NtStatus::InvalidParameter;
        const size_t endModule = lastModule < count_ ? lastModule : count_;
        for (size_t moduleIndex = firstModule; moduleIndex < endModule; ++moduleIndex) {
            const auto& current = modules_[moduleIndex];
            const auto status = current.view.forEachImportSymbol(current.immutableImage(),
                [&](std::string_view library, std::string_view name,
                    uint16_t ordinal) noexcept -> NtStatus {
                    uint64_t address{};
                    std::string_view missingLibrary;
                    const auto resolveStatus = resolve(library, name, ordinal, address,
                                                       &missingLibrary, current.name);
                    if (resolveStatus == NtStatus::Success) return NtStatus::Success;
                    if (resolveStatus != NtStatus::NoSuchFile || missingLibrary.empty()) {
                        if (failure) *failure = {library, name, current.name, resolveStatus};
                        return resolveStatus;
                    }
                    const auto visitorStatus = visitor(missingLibrary, current.name);
                    if (visitorStatus != NtStatus::Success && failure)
                        *failure = {missingLibrary, name, current.name, visitorStatus};
                    return visitorStatus;
                });
            if (status != NtStatus::Success) return status;
        }
        return NtStatus::Success;
    }
    [[nodiscard]] const Module* module(size_t index) const noexcept {
        return index < count_ ? &modules_[index] : nullptr;
    }
    [[nodiscard]] bool importsBound() const noexcept { return bound_; }
    [[nodiscard]] const BindingFailure& bindingFailure() const noexcept { return failure_; }

private:
    static bool isApiSetName(std::string_view name) noexcept {
        if (name.size() < 4) return false;
        const auto starts = [&](std::string_view prefix) noexcept {
            return equal(name.substr(0, prefix.size()), prefix, true);
        };
        return starts("api-") || starts("ext-");
    }
    static char lower(char character) noexcept {
        return character >= 'A' && character <= 'Z'
            ? static_cast<char>(character + ('a' - 'A')) : character;
    }
    static bool equal(std::string_view first, std::string_view second,
                      bool insensitive = false) noexcept {
        if (first.size() != second.size()) return false;
        for (size_t byte = 0; byte < first.size(); ++byte) {
            if (insensitive ? lower(first[byte]) != lower(second[byte])
                            : first[byte] != second[byte]) return false;
        }
        return true;
    }
    [[nodiscard]] size_t findModule(std::string_view name) const noexcept {
        for (size_t index = 0; index < count_; ++index) {
            const auto candidate = modules_[index].name;
            if (equal(name, candidate, true) ||
                (candidate.size() == name.size() + 4 &&
                 equal({candidate.data(), name.size()}, name, true) &&
                 equal({candidate.data() + name.size(), 4}, {".dll", 4}, true))) {
                return index;
            }
        }
        return count_;
    }
    std::array<Module, 16> modules_{};
    size_t count_{};
    bool bound_{};
    BindingFailure failure_{};
    void* apiSetContext_{};
    ApiSetHostResolver apiSetResolver_{};
};

class UserImageLoader64 {
private:
    struct ImageExportDirectory64 {
        uint32_t characteristics, timeDateStamp;
        uint16_t majorVersion, minorVersion;
        uint32_t name, ordinalBase, numberOfFunctions, numberOfNames;
        uint32_t addressOfFunctions, addressOfNames, addressOfNameOrdinals;
    };

    static inline bool streq(const char* a, const char* b) noexcept {
        if (!a || !b) return false;
        while (*a && *b) {
            if (*a != *b) return false;
            a++; b++;
        }
        return (*a == *b);
    }

    static inline bool strcaseeq(const char* a, const char* b) noexcept {
        if (!a || !b) return false;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + ('a' - 'A')) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + ('a' - 'A')) : *b;
            if (ca != cb) return false;
            a++; b++;
        }
        return (*a == *b);
    }

    static bool hasTerminator(const uint8_t* image, size_t imageSize, uint32_t rva) noexcept {
        if (!image || rva >= imageSize) return false;
        for (size_t i=rva; i<imageSize; ++i) if (image[i] == 0) return true;
        return false;
    }

    static bool findExportRva(uint8_t* image, size_t imageSize, const char* symbol,
                              uint16_t ordinal, uint32_t& outRva) noexcept {
        if (!image || imageSize < sizeof(pe::ImageDosHeader)) return false;
        const auto* dos=reinterpret_cast<const pe::ImageDosHeader*>(image);
        if (dos->e_magic != pe::DOS_MAGIC || dos->e_lfanew < 0 ||
            static_cast<size_t>(dos->e_lfanew)+sizeof(pe::ImageNtHeaders64)>imageSize) return false;
        auto* nt=reinterpret_cast<pe::ImageNtHeaders64*>(image+dos->e_lfanew);
        if (nt->signature!=pe::NT_SIGNATURE || nt->optionalHeader.numberOfRvaAndSizes<=pe::IMAGE_DIRECTORY_ENTRY_EXPORT) return false;
        const auto& dir=nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.virtualAddress || dir.size<sizeof(ImageExportDirectory64) ||
            dir.virtualAddress>imageSize || dir.size>imageSize-dir.virtualAddress) return false;
        const auto* exports=reinterpret_cast<const ImageExportDirectory64*>(image+dir.virtualAddress);
        if (exports->numberOfFunctions>imageSize/sizeof(uint32_t) ||
            exports->addressOfFunctions>imageSize ||
            static_cast<size_t>(exports->numberOfFunctions)*sizeof(uint32_t)>imageSize-exports->addressOfFunctions) return false;
        const auto* functions=reinterpret_cast<const uint32_t*>(image+exports->addressOfFunctions);
        uint32_t functionIndex=UINT32_MAX;
        if (ordinal) {
            if (ordinal<exports->ordinalBase) return false;
            functionIndex=ordinal-exports->ordinalBase;
        } else {
            if (!symbol || exports->numberOfNames>imageSize/sizeof(uint32_t) ||
                exports->addressOfNames>imageSize ||
                static_cast<size_t>(exports->numberOfNames)*sizeof(uint32_t)>imageSize-exports->addressOfNames ||
                exports->addressOfNameOrdinals>imageSize ||
                static_cast<size_t>(exports->numberOfNames)*sizeof(uint16_t)>imageSize-exports->addressOfNameOrdinals) return false;
            const auto* names=reinterpret_cast<const uint32_t*>(image+exports->addressOfNames);
            const auto* ordinals=reinterpret_cast<const uint16_t*>(image+exports->addressOfNameOrdinals);
            for (uint32_t i=0;i<exports->numberOfNames;++i) {
                if (names[i]>=imageSize || !hasTerminator(image,imageSize,names[i])) continue;
                if (streq(reinterpret_cast<const char*>(image+names[i]),symbol)) {
                    functionIndex=ordinals[i];
                    break;
                }
            }
        }
        if (functionIndex>=exports->numberOfFunctions) return false;
        outRva=functions[functionIndex];
        // Forwarded exports point back into the export directory and require
        // another module-resolution pass, so reject them at this layer.
        return outRva<imageSize && !(outRva>=dir.virtualAddress && outRva<dir.virtualAddress+dir.size);
    }

public:
    static bool resolveExportRva(uint8_t* image, size_t imageSize, const char* symbol,
                                 uint32_t& outRva) noexcept {
        return findExportRva(image, imageSize, symbol, 0, outRva);
    }

    static bool imageImportsModule(const uint8_t* image, size_t imageSize,
                                   const char* moduleName) noexcept {
        if (!image || !moduleName || imageSize < sizeof(pe::ImageDosHeader)) return false;
        const auto* dos = reinterpret_cast<const pe::ImageDosHeader*>(image);
        if (dos->e_magic != pe::DOS_MAGIC || dos->e_lfanew < 0 ||
            static_cast<size_t>(dos->e_lfanew) + sizeof(pe::ImageNtHeaders64) > imageSize) return false;
        const auto* nt = reinterpret_cast<const pe::ImageNtHeaders64*>(image + dos->e_lfanew);
        if (nt->signature != pe::NT_SIGNATURE ||
            nt->optionalHeader.numberOfRvaAndSizes <= pe::IMAGE_DIRECTORY_ENTRY_IMPORT) return false;
        const auto& importDir = nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!importDir.virtualAddress || importDir.virtualAddress > imageSize ||
            importDir.size > imageSize - importDir.virtualAddress) return false;
        const auto* desc = reinterpret_cast<const pe::ImageImportDescriptor*>(image + importDir.virtualAddress);
        for (size_t d = 0; d < importDir.size / sizeof(pe::ImageImportDescriptor) && desc[d].name; ++d) {
            if (desc[d].name >= imageSize || !hasTerminator(image, imageSize, desc[d].name)) return false;
            const char* importedName = reinterpret_cast<const char*>(image + desc[d].name);
            if (strcaseeq(importedName, moduleName)) return true;
        }
        return false;
    }

    static bool bindImportsToMappedModule(uint8_t* image, size_t imageSize,
                                           const char* moduleName,
                                           uint8_t* moduleImage, size_t moduleSize,
                                           uint64_t moduleBaseVa,
                                           bool allowUnresolved = false) noexcept {
        if (!image || !moduleName || !moduleImage || imageSize<sizeof(pe::ImageDosHeader)) return false;
        const auto* dos=reinterpret_cast<const pe::ImageDosHeader*>(image);
        if (dos->e_magic!=pe::DOS_MAGIC || dos->e_lfanew<0 ||
            static_cast<size_t>(dos->e_lfanew)+sizeof(pe::ImageNtHeaders64)>imageSize) return false;
        auto* nt=reinterpret_cast<pe::ImageNtHeaders64*>(image+dos->e_lfanew);
        if (nt->signature!=pe::NT_SIGNATURE || nt->optionalHeader.numberOfRvaAndSizes<=pe::IMAGE_DIRECTORY_ENTRY_IMPORT) return true;
        const auto& importDir=nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!importDir.virtualAddress || importDir.virtualAddress>=imageSize || importDir.size>imageSize-importDir.virtualAddress) return true;
        auto* desc=reinterpret_cast<pe::ImageImportDescriptor*>(image+importDir.virtualAddress);
        for (size_t d=0; d<importDir.size/sizeof(pe::ImageImportDescriptor) && desc[d].name; ++d) {
            auto& entry=desc[d];
            if (entry.name>=imageSize || !hasTerminator(image,imageSize,entry.name)) return false;
            const char* importedName = reinterpret_cast<const char*>(image + entry.name);
            const bool matchesModule = strcaseeq(importedName, moduleName);
            if (!matchesModule) continue;
            if (!entry.originalFirstThunk) return false;
            const uint32_t thunkRva=entry.originalFirstThunk;
            if (thunkRva>=imageSize || entry.firstThunk>=imageSize) return false;
            const auto* names=reinterpret_cast<const uint64_t*>(image+thunkRva);
            size_t index=0;
            while (thunkRva+(index+1)*sizeof(uint64_t)<=imageSize && names[index]) {
                const uint64_t value=names[index];
                const uint32_t iatRva=entry.firstThunk+static_cast<uint32_t>(index*sizeof(uint64_t));
                if (iatRva>imageSize || sizeof(uint64_t)>imageSize-iatRva) return false;
                uint32_t exportRva=0;
                bool resolved=false;
                if (value&pe::IMAGE_ORDINAL_FLAG64) {
                    resolved=findExportRva(moduleImage,moduleSize,nullptr,static_cast<uint16_t>(value&0xFFFF),exportRva);
                } else {
                    const uint32_t nameRva=static_cast<uint32_t>(value&0xFFFFFFFF);
                    if (nameRva>imageSize || nameRva+sizeof(uint16_t)>=imageSize || !hasTerminator(image,imageSize,nameRva+sizeof(uint16_t))) return false;
                    const auto* byName=reinterpret_cast<const pe::ImageImportByName*>(image+nameRva);
                    resolved=findExportRva(moduleImage,moduleSize,byName->name,0,exportRva);
                }
                if (!resolved) {
                    if (!allowUnresolved) return false;
                    ++index;
                    continue;
                }
                *reinterpret_cast<uint64_t*>(image+iatRva)=moduleBaseVa+exportRva;
                ++index;
            }
        }
        // A caller binds a set of candidate modules; absence of this particular
        // dependency is not an error. Malformed imports and unresolved symbols
        // in a matching module still fail above.
        return true;
    }

    static micant::NtStatus loadAndMapImage(
        std::span<const uint8_t> rawPeBytes,
        uint8_t* destinationBuffer,
        size_t destinationBufferSize,
        uint64_t targetBaseVa,
        uint8_t* trampolineBuffer,
        size_t trampolineBufferSize,
        uint64_t trampolineBaseVa,
        uint64_t userStackTopVa,
        size_t userStackSize,
        uint64_t pebVa,
        uint64_t tebVa,
        uint32_t pid,
        uint32_t parentPid,
        const char* imageName,
        UserLoadedImageInfo& outInfo,
        uint64_t userHeapVa = 0,
        size_t userHeapSize = 0,
        const char* cmdLine = nullptr,
        bool registerAsProcess = true
    ) noexcept {
        outInfo = UserLoadedImageInfo{};
        outInfo.pid = pid;
        outInfo.tid = pid;
        if (imageName) {
            size_t n = 0;
            while (imageName[n] && n < sizeof(outInfo.imageName) - 1) {
                outInfo.imageName[n] = imageName[n];
                n++;
            }
            outInfo.imageName[n] = '\0';
        }

        // 1. Inspect DOS Header
        if (rawPeBytes.size() < sizeof(pe::ImageDosHeader)) {
            return micant::NtStatus::InvalidParameter;
        }
        const auto* dos = reinterpret_cast<const pe::ImageDosHeader*>(rawPeBytes.data());
        if (dos->e_magic != pe::DOS_MAGIC) {
            return micant::NtStatus::InvalidImageFormat;
        }
        if (dos->e_lfanew <= 0 || static_cast<size_t>(dos->e_lfanew) + sizeof(pe::ImageNtHeaders64) > rawPeBytes.size()) {
            return micant::NtStatus::InvalidImageFormat;
        }

        // 2. Inspect NT Headers (PE32+ 64-bit AMD64)
        const auto* nt = reinterpret_cast<const pe::ImageNtHeaders64*>(rawPeBytes.data() + dos->e_lfanew);
        if (nt->signature != pe::NT_SIGNATURE ||
            nt->fileHeader.machine != pe::MACHINE_AMD64 ||
            nt->optionalHeader.magic != pe::PE32PLUS_MAGIC) {
            return micant::NtStatus::InvalidImageFormat;
        }

        outInfo.headersValid = true;
        outInfo.sizeOfImage = nt->optionalHeader.sizeOfImage;
        outInfo.numberOfSections = nt->fileHeader.numberOfSections;

        // 3. Verify Destination Buffer
        if (!destinationBuffer || destinationBufferSize < nt->optionalHeader.sizeOfImage) {
            return micant::NtStatus::BufferTooSmall;
        }
        std::memset(destinationBuffer, 0, destinationBufferSize);

        // 4. Map Headers
        if (nt->optionalHeader.sizeOfHeaders > rawPeBytes.size() ||
            nt->optionalHeader.sizeOfHeaders > destinationBufferSize) {
            return micant::NtStatus::InvalidImageFormat;
        }
        std::memcpy(destinationBuffer, rawPeBytes.data(), nt->optionalHeader.sizeOfHeaders);

        // 5. Map Sections
        const auto* sections = reinterpret_cast<const pe::ImageSectionHeader*>(
            rawPeBytes.data() + dos->e_lfanew + sizeof(uint32_t) + sizeof(pe::ImageFileHeader) + nt->fileHeader.sizeOfOptionalHeader
        );

        for (uint16_t i = 0; i < nt->fileHeader.numberOfSections; ++i) {
            const auto& sec = sections[i];
            if (sec.virtualAddress + sec.sizeOfRawData > destinationBufferSize) {
                return micant::NtStatus::InvalidImageFormat;
            }
            if (sec.pointerToRawData + sec.sizeOfRawData > rawPeBytes.size()) {
                return micant::NtStatus::InvalidImageFormat;
            }
            if (sec.sizeOfRawData > 0) {
                std::memcpy(destinationBuffer + sec.virtualAddress,
                            rawPeBytes.data() + sec.pointerToRawData,
                            sec.sizeOfRawData);
            }
            if (sec.misc.virtualSize > sec.sizeOfRawData) {
                size_t bssLen = sec.misc.virtualSize - sec.sizeOfRawData;
                if (sec.virtualAddress + sec.sizeOfRawData + bssLen <= destinationBufferSize) {
                    std::memset(destinationBuffer + sec.virtualAddress + sec.sizeOfRawData, 0, bssLen);
                }
            }
        }
        outInfo.sectionsMapped = true;

        // 6. Base Relocations
        int64_t delta = static_cast<int64_t>(targetBaseVa) - static_cast<int64_t>(nt->optionalHeader.imageBase);
        if (delta != 0) {
            outInfo.isRelocated = true;
            if (nt->optionalHeader.numberOfRvaAndSizes > pe::IMAGE_DIRECTORY_ENTRY_BASERELOC) {
                const auto& relocDir = nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_BASERELOC];
                if (relocDir.virtualAddress != 0 && relocDir.size != 0 &&
                    relocDir.virtualAddress + relocDir.size <= destinationBufferSize) {
                    uint32_t offset = relocDir.virtualAddress;
                    uint32_t endOffset = relocDir.virtualAddress + relocDir.size;
                    while (offset < endOffset) {
                        const auto* block = reinterpret_cast<const pe::ImageBaseRelocation*>(destinationBuffer + offset);
                        if (block->sizeOfBlock < sizeof(pe::ImageBaseRelocation)) break;
                        uint32_t count = (block->sizeOfBlock - sizeof(pe::ImageBaseRelocation)) / sizeof(uint16_t);
                        const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
                        for (uint32_t e = 0; e < count; ++e) {
                            uint16_t type = entries[e] >> 12;
                            uint16_t off = entries[e] & 0x0FFF;
                            uint32_t rva = block->virtualAddress + off;
                            if (type == pe::IMAGE_REL_BASED_DIR64 && rva + sizeof(uint64_t) <= destinationBufferSize) {
                                *reinterpret_cast<uint64_t*>(destinationBuffer + rva) += delta;
                                outInfo.relocationsAppliedCount++;
                            } else if (type == pe::IMAGE_REL_BASED_HIGHLOW && rva + sizeof(uint32_t) <= destinationBufferSize) {
                                *reinterpret_cast<uint32_t*>(destinationBuffer + rva) += static_cast<uint32_t>(delta);
                                outInfo.relocationsAppliedCount++;
                            }
                        }
                        offset += block->sizeOfBlock;
                    }
                }
            }
        }

        // 7. Setup Fast Syscall Trampolines
        // Slot 0 (0x00): NtTerminateProcess / ExitProcess (SSN 0x01)
        // Slot 1 (0x20): NtQuerySystemInformation (SSN 0x11)
        // Slot 2 (0x40): NtReadFile / ReadFile (SSN 0x23)
        // Slot 3 (0x60): NtWriteFile / WriteFile (SSN 0x25)
        // Slot 4 (0x80): NtYieldExecution (SSN 0x46)
        // Slot 5 (0xA0): NtAllocateVirtualMemory (SSN 0x55)
        // Slot 6 (0xC0): NtClose / CloseHandle (SSN 0x1E)
        // Slot 7 (0xE0): GetStdHandle (returns 1 in RAX)
        // Slot 8 (0x100): GetTickCount64 (rdtsc + shl + or)
        if (trampolineBuffer && trampolineBufferSize >= 512) {
            std::memset(trampolineBuffer, 0xCC, trampolineBufferSize); // fill with INT3 break stubs

            // Slot 0 (0x00): ExitProcess / NtTerminateProcess (SSN 0x01)
            // 49 89 CA          mov %rcx, %r10 (Windows syscall arg1)
            // B8 01 00 00 00    mov $0x01, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s0[] = { 0x49, 0x89, 0xCA, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0x00, s0, sizeof(s0));

            // Slot 1 (0x20): NtQuerySystemInformation (SSN 0x11)
            // 49 89 CA          mov %rcx, %r10 (Windows syscall arg1)
            // B8 11 00 00 00    mov $0x11, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s1[] = { 0x49, 0x89, 0xCA, 0xB8, 0x11, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0x20, s1, sizeof(s1));

            // Slot 2 (0x40): NtReadFile / ReadFile (SSN 0x23)
            // 49 89 CA          mov %rcx, %r10 (Windows syscall arg1)
            // B8 23 00 00 00    mov $0x23, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s2[] = { 0x49, 0x89, 0xCA, 0xB8, 0x23, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0x40, s2, sizeof(s2));

            // Slot 3 (0x60): NtWriteFile / WriteFile (SSN 0x25)
            // 49 89 CA          mov %rcx, %r10 (Windows syscall arg1)
            // B8 25 00 00 00    mov $0x25, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s3[] = { 0x49, 0x89, 0xCA, 0xB8, 0x25, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0x60, s3, sizeof(s3));

            // Slot 4 (0x80): NtYieldExecution (SSN 0x46)
            // B8 46 00 00 00    mov $0x46, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s4[] = { 0xB8, 0x46, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0x80, s4, sizeof(s4));

            // Slot 5 (0xA0): NtAllocateVirtualMemory (SSN 0x55)
            // 49 89 CA          mov %rcx, %r10 (Windows syscall arg1)
            // B8 55 00 00 00    mov $0x55, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s5[] = { 0x49, 0x89, 0xCA, 0xB8, 0x55, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0xA0, s5, sizeof(s5));

            // Slot 6 (0xC0): NtClose / CloseHandle (SSN 0x1E)
            // 49 89 CA          mov %rcx, %r10 (Windows syscall arg1)
            // B8 1E 00 00 00    mov $0x1E, %eax
            // 0F 05             syscall
            // C3                ret
            const uint8_t s6[] = { 0x49, 0x89, 0xCA, 0xB8, 0x1E, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
            std::memcpy(trampolineBuffer + 0xC0, s6, sizeof(s6));

            // Slot 7 (0xE0): GetStdHandle (returns 1 in RAX)
            // B8 01 00 00 00    mov $1, %eax
            // C3                ret
            const uint8_t s7[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0xE0, s7, sizeof(s7));

            // Slot 8 (0x100): GetTickCount64 (rdtsc + shl + or)
            const uint8_t s8[] = { 0x0F, 0x31, 0x48, 0xC1, 0xE2, 0x20, 0x48, 0x09, 0xD0, 0xC3 };
            std::memcpy(trampolineBuffer + 0x100, s8, sizeof(s8));

            // Slot 9 (0x120): QueryPerformanceCounter
            const uint8_t s9[] = { 0x0F, 0x31, 0x48, 0xC1, 0xE2, 0x20, 0x48, 0x09, 0xD0, 0x48, 0x85, 0xC9, 0x74, 0x03, 0x48, 0x89, 0x01, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x120, s9, sizeof(s9));

            // Slot 10 (0x140): QueryPerformanceFrequency (10 MHz)
            const uint8_t s10[] = { 0x48, 0xB8, 0x80, 0x96, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x03, 0x48, 0x89, 0x01, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x140, s10, sizeof(s10));

            // Slot 11 (0x160): GetCurrentProcessId / GetCurrentThreadId (returns 1024)
            const uint8_t s11[] = { 0xB8, 0x00, 0x04, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x160, s11, sizeof(s11));

            // Slot 12 (0x180): GetLastError (returns 0)
            const uint8_t s12[] = { 0x31, 0xC0, 0xC3 };
            std::memcpy(trampolineBuffer + 0x180, s12, sizeof(s12));

            // Slot 13 (0x1A0): UniversalNoop / SetLastError / CritSec (ret)
            const uint8_t s13[] = { 0xC3 };
            std::memcpy(trampolineBuffer + 0x1A0, s13, sizeof(s13));

            // Slot 14 (0x1C0): GetModuleHandleW / GetModuleHandleA (returns 0x140000000 on NULL rcx)
            const uint8_t s14[] = { 0x48, 0x85, 0xC9, 0x75, 0x0B, 0x48, 0xB8, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0xC3, 0x31, 0xC0, 0xC3 };
            std::memcpy(trampolineBuffer + 0x1C0, s14, sizeof(s14));

            // Slot 15 (0x1E0): UniversalTrue / VirtualFree / SetEvent / ResetEvent (returns 1)
            const uint8_t s15[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x1E0, s15, sizeof(s15));

            // Slot 16 (0x200): UniversalSuccess / UniversalNull (returns 0 / S_OK)
            const uint8_t s16[] = { 0x31, 0xC0, 0xC3 };
            std::memcpy(trampolineBuffer + 0x200, s16, sizeof(s16));

            // Slot 17 (0x220): GetCurrentPackageFullName (writes 0, returns 15700)
            const uint8_t s17[] = { 0x48, 0x85, 0xC9, 0x74, 0x06, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x4C, 0x3D, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x220, s17, sizeof(s17));

            // Slot 18 (0x240): CreateEventW / CreateEventA (returns dummy handle 0x1000)
            const uint8_t s18[] = { 0xB8, 0x00, 0x10, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x240, s18, sizeof(s18));

            // Slot 19 (0x260): GetSystemTimeAsFileTime (writes timestamp to [rcx])
            const uint8_t s19[] = { 0x48, 0x85, 0xC9, 0x74, 0x10, 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xD9, 0x01, 0x48, 0x89, 0x01, 0xC3 };
            std::memcpy(trampolineBuffer + 0x260, s19, sizeof(s19));

            // Slot 20 (0x280): GetProcessHeap (returns non-null handle 0x10000)
            const uint8_t s20[] = { 0x48, 0xB8, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x280, s20, sizeof(s20));

            auto patchMovRdx = [](uint8_t* code, size_t len, uint64_t val) {
                for (size_t i = 0; i + 10 <= len; ++i) {
                    if (code[i] == 0x48 && code[i + 1] == 0xBA) {
                        std::memcpy(code + i + 2, &val, sizeof(val));
                        return;
                    }
                }
            };
            auto patchMovRax = [](uint8_t* code, size_t len, uint64_t val) {
                for (size_t i = 0; i + 10 <= len; ++i) {
                    if (code[i] == 0x48 && code[i + 1] == 0xB8) {
                        std::memcpy(code + i + 2, &val, sizeof(val));
                        return;
                    }
                }
            };

            // Slot 21 (0x2A0): HeapAlloc (lock xadd bump pointer at 0x3F8)
            uint64_t bumpAddr = trampolineBaseVa + 0x3F8;
            uint64_t mallocAddr = trampolineBaseVa + 0x400;
            uint64_t callocAddr = trampolineBaseVa + 0x440;
            uint8_t s21[27] = {
                0x49, 0x89, 0xC0,                         // mov %r8, %rax
                0x48, 0x83, 0xC0, 0x0F,                   // add $15, %rax
                0x48, 0x83, 0xE0, 0xF0,                   // and $-16, %rax
                0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov $bumpAddr, %rdx
                0xF0, 0x48, 0x0F, 0xC1, 0x02,             // lock xadd %rax, (%rdx)
                0xC3                                      // ret
            };
            patchMovRdx(s21, sizeof(s21), bumpAddr);
            std::memcpy(trampolineBuffer + 0x2A0, s21, sizeof(s21));

            // Slot 22 (0x2C0): GetCommandLineW (returns pointer to wide string at 0x3A0)
            uint64_t wcmdAddr = trampolineBaseVa + 0x3A0;
            uint8_t s22[11] = { 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            patchMovRax(s22, sizeof(s22), wcmdAddr);
            std::memcpy(trampolineBuffer + 0x2C0, s22, sizeof(s22));

            // Slot 23 (0x2E0): GetCommandLineA (returns pointer to ascii string at 0x360)
            uint64_t acmdAddr = trampolineBaseVa + 0x360;
            uint8_t s23[11] = { 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            patchMovRax(s23, sizeof(s23), acmdAddr);
            std::memcpy(trampolineBuffer + 0x2E0, s23, sizeof(s23));

            // Slot 24 (0x300): GetFileType (returns FILE_TYPE_CHAR = 2)
            const uint8_t s24[] = { 0xB8, 0x02, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x300, s24, sizeof(s24));

            // Slot 25 (0x320): GetConsoleMode (writes 0x07 to [rdx] if non-null, returns 1)
            const uint8_t s25[] = { 0x48, 0x85, 0xD2, 0x74, 0x06, 0xC7, 0x02, 0x07, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x320, s25, sizeof(s25));

            // Slot 26 (0x340): TlsAlloc (returns 1)
            const uint8_t s26[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x340, s26, sizeof(s26));

            // 0x360: ASCII command line string
            const char* effectiveCmd = (cmdLine && *cmdLine) ? cmdLine : (imageName ? imageName : "micant_app.exe");
            size_t cLen = 0;
            while (effectiveCmd[cLen] && cLen < 63) {
                trampolineBuffer[0x360 + cLen] = static_cast<uint8_t>(effectiveCmd[cLen]);
                cLen++;
            }
            trampolineBuffer[0x360 + cLen] = 0;


            // 0x3A0: Wide command line string
            size_t wLen = 0;
            while (effectiveCmd[wLen] && wLen < 63) {
                char16_t wc = static_cast<char16_t>(effectiveCmd[wLen]);
                std::memcpy(trampolineBuffer + 0x3A0 + wLen * 2, &wc, 2);
                wLen++;
            }
            char16_t zero16 = 0;
            std::memcpy(trampolineBuffer + 0x3A0 + wLen * 2, &zero16, 2);


            // Split the process heap evenly: lower 64 MB for UCRT and upper
            // 64 MB for the generic HeapAlloc trampolines.
            uint64_t initialBump = (userHeapVa != 0) ? userHeapVa + ((userHeapSize > 0x4000000) ? 0x4000000 : 0) :
                                   ((targetBaseVa == 0x140000000ULL) ? (targetBaseVa + 0x400000ULL) : (trampolineBaseVa + 0x1000ULL));
            std::memcpy(trampolineBuffer + 0x3F8, &initialBump, sizeof(initialBump));

            // HeapAlloc(heap, flags, bytes) adapts to calloc(1, bytes).
            uint8_t sHeapAlloc[] = {0x4C, 0x89, 0xC2, 0x48, 0xC7, 0xC1, 0x01, 0, 0, 0,
                                    0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
            std::memcpy(sHeapAlloc + 12, &callocAddr, sizeof(callocAddr));
            std::memcpy(trampolineBuffer + 0xF00, sHeapAlloc, sizeof(sHeapAlloc));
            // HeapReAlloc(heap, flags, ptr, bytes) adapts to realloc(ptr, bytes).
            // The loader patches the jump target to UCRT's export after mapping DLLs.
            uint8_t sHeapReAlloc[] = {0x4C, 0x89, 0xC1, 0x4C, 0x89, 0xCA,
                                       0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
            std::memcpy(trampolineBuffer + 0xF20, sHeapReAlloc, sizeof(sHeapReAlloc));

            // Slot 27 (0x400): malloc / _aligned_malloc (RCX = size)
            uint8_t s27[] = {
                0x48, 0x85, 0xC9,                         // test %rcx, %rcx
                0x75, 0x03,                               // jnz 1f (+3)
                0x48, 0xFF, 0xC1,                         // inc %rcx
                0x48, 0x89, 0xC8,                         // 1: mov %rcx, %rax
                0x48, 0x83, 0xC0, 0x0F,                   // add $15, %rax
                0x48, 0x83, 0xE0, 0xF0,                   // and $-16, %rax
                0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov $bumpAddr, %rdx
                0xF0, 0x48, 0x0F, 0xC1, 0x02,             // lock xadd %rax, (%rdx)
                0xC3                                      // ret
            };
            patchMovRdx(s27, sizeof(s27), bumpAddr);
            std::memcpy(trampolineBuffer + 0x400, s27, sizeof(s27));

            // Slot 28 (0x440): calloc (RCX = num, RDX = size)
            // Starts with REX.W imul per UCRT ABI & Suite 348 assertion; preserves %rdi
            uint8_t s28[] = {
                0x48, 0x0F, 0xAF, 0xCA,                   // imul %rdx, %rcx
                0x48, 0x85, 0xC9,                         // test %rcx, %rcx
                0x75, 0x03,                               // jnz 1f (+3)
                0x48, 0xFF, 0xC1,                         // inc %rcx
                0x48, 0x89, 0xC8,                         // 1: mov %rcx, %rax
                0x48, 0x83, 0xC0, 0x0F,                   // add $15, %rax
                0x48, 0x83, 0xE0, 0xF0,                   // and $-16, %rax
                0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov $bumpAddr, %rdx
                0xF0, 0x48, 0x0F, 0xC1, 0x02,             // lock xadd %rax, (%rdx)
                0x50,                                     // push %rax
                0x57,                                     // push %rdi
                0x48, 0x89, 0xC7,                         // mov %rax, %rdi
                0x31, 0xC0,                               // xor %eax, %eax
                0xFC,                                     // cld
                0xF3, 0xAA,                               // rep stosb
                0x5F,                                     // pop %rdi
                0x58,                                     // pop %rax
                0xC3                                      // ret
            };
            patchMovRdx(s28, sizeof(s28), bumpAddr);
            std::memcpy(trampolineBuffer + 0x440, s28, sizeof(s28));

            // Slot 29 (0x480): realloc (RCX = ptr, RDX = size)
            uint8_t s29[] = {
                0x48, 0x89, 0xD1,                         // mov %rdx, %rcx (new size)
                0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,     // mov malloc trampoline, %rax
                0xFF, 0xE0                                // jmp *%rax
            };
            std::memcpy(s29 + 5, &mallocAddr, sizeof(mallocAddr));
            std::memcpy(trampolineBuffer + 0x480, s29, sizeof(s29));

            // Slot 30 (0x4C0): _initterm (RCX = first, RDX = last)
            // 16-byte stack aligned per Win64 ABI
            const uint8_t s30[] = {
                0x48, 0x39, 0xD1,                         // 1: cmp %rdx, %rcx
                0x73, 0x1C,                               // jae 2f (+28)
                0x48, 0x8B, 0x01,                         // mov (%rcx), %rax
                0x48, 0x85, 0xC0,                         // test %rax, %rax
                0x74, 0x0E,                               // jz 3f (+14)
                0x51,                                     // push %rcx
                0x52,                                     // push %rdx
                0x48, 0x83, 0xEC, 0x28,                   // sub $40, %rsp (16-byte align)
                0xFF, 0xD0,                               // call *%rax
                0x48, 0x83, 0xC4, 0x28,                   // add $40, %rsp
                0x5A,                                     // pop %rdx
                0x59,                                     // pop %rcx
                0x48, 0x83, 0xC1, 0x08,                   // 3: add $8, %rcx
                0xEB, 0xDF,                               // jmp 1b (-33)
                0xC3                                      // 2: ret
            };
            std::memcpy(trampolineBuffer + 0x4C0, s30, sizeof(s30));

            // Slot 31 (0x500): _initterm_e (RCX = first, RDX = last)
            // 16-byte stack aligned per Win64 ABI
            const uint8_t s31[] = {
                0x48, 0x39, 0xD1,                         // 1: cmp %rdx, %rcx
                0x73, 0x20,                               // jae 2f (+32)
                0x48, 0x8B, 0x01,                         // mov (%rcx), %rax
                0x48, 0x85, 0xC0,                         // test %rax, %rax
                0x74, 0x12,                               // jz 3f (+18)
                0x51,                                     // push %rcx
                0x52,                                     // push %rdx
                0x48, 0x83, 0xEC, 0x28,                   // sub $40, %rsp (16-byte align)
                0xFF, 0xD0,                               // call *%rax
                0x48, 0x83, 0xC4, 0x28,                   // add $40, %rsp
                0x5A,                                     // pop %rdx
                0x59,                                     // pop %rcx
                0x85, 0xC0,                               // test %eax, %eax
                0x75, 0x09,                               // jne 4f (+9)
                0x48, 0x83, 0xC1, 0x08,                   // 3: add $8, %rcx
                0xEB, 0xDB,                               // jmp 1b (-37)
                0x31, 0xC0,                               // 2: xor %eax, %eax
                0xC3,                                     // ret
                0xC3                                      // 4: ret
            };
            std::memcpy(trampolineBuffer + 0x500, s31, sizeof(s31));

            // Slot 32 (0x540): memcpy / memmove (RCX = dst, RDX = src, R8 = count)
            // Starts with REX.W mov per Suite 348 assertion; preserves %rdi and %rsi per Win64 ABI
            const uint8_t s32[] = {
                0x48, 0x89, 0xC8,                         // mov %rcx, %rax (return dst)
                0x57,                                     // push %rdi
                0x56,                                     // push %rsi
                0x4C, 0x89, 0xC1,                         // mov %r8, %rcx (count)
                0x48, 0x89, 0xC7,                         // mov %rax, %rdi (dst)
                0x48, 0x89, 0xD6,                         // mov %rdx, %rsi (src)
                0xFC,                                     // cld
                0xF3, 0xA4,                               // rep movsb
                0x5E,                                     // pop %rsi
                0x5F,                                     // pop %rdi
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x540, s32, sizeof(s32));

            // Slot 33 (0x580): memset (RCX = dst, RDX = val, R8 = count)
            // Starts with REX.WB per Suite 348 assertion; preserves %rdi per Win64 ABI
            const uint8_t s33[] = {
                0x49, 0x89, 0xC9,                         // mov %rcx, %r9 (save dst)
                0x57,                                     // push %rdi
                0x4C, 0x89, 0xC1,                         // mov %r8, %rcx (count)
                0x4C, 0x89, 0xCF,                         // mov %r9, %rdi (dst)
                0x88, 0xD0,                               // mov %dl, %al (val)
                0xFC,                                     // cld
                0xF3, 0xAA,                               // rep stosb
                0x5F,                                     // pop %rdi
                0x4C, 0x89, 0xC8,                         // mov %r9, %rax (return dst)
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x580, s33, sizeof(s33));

            // Slot 34 (0x5C0): memcmp (RCX = s1, RDX = s2, R8 = count)
            const uint8_t s34[] = {
                0x31, 0xC0,                               // xor %eax, %eax
                0x4D, 0x85, 0xC0,                         // test %r8, %r8
                0x74, 0x18,                               // jz 2f (+24)
                0x44, 0x0F, 0xB6, 0x09,                   // 1: movzbl (%rcx), %r9d
                0x44, 0x0F, 0xB6, 0x12,                   // movzbl (%rdx), %r10d
                0x45, 0x39, 0xD1,                         // cmp %r10d, %r9d
                0x75, 0x0C,                               // jne 3f (+12)
                0x48, 0xFF, 0xC1,                         // inc %rcx
                0x48, 0xFF, 0xC2,                         // inc %rdx
                0x49, 0xFF, 0xC8,                         // dec %r8
                0x75, 0xE8,                               // jnz 1b (-24)
                0xC3,                                     // 2: ret
                0x45, 0x29, 0xD1,                         // 3: subl %r10d, %r9d
                0x44, 0x89, 0xC8,                         // mov %r9d, %eax
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x5C0, s34, sizeof(s34));

            // Slot 35 (0x600): strlen (RCX = str)
            const uint8_t s35[] = {
                0x48, 0x31, 0xC0,                         // xor %rax, %rax
                0x80, 0x3C, 0x01, 0x00,                   // 1: cmpb $0, (%rcx, %rax)
                0x74, 0x05,                               // jz 2f (+5)
                0x48, 0xFF, 0xC0,                         // inc %rax
                0xEB, 0xF5,                               // jmp 1b (-11)
                0xC3                                      // 2: ret
            };
            std::memcpy(trampolineBuffer + 0x600, s35, sizeof(s35));

            // Slot 36 (0x640): strcmp (RCX = s1, RDX = s2)
            const uint8_t s36[] = {
                0x0F, 0xB6, 0x01,                         // 1: movzbl (%rcx), %eax
                0x44, 0x0F, 0xB6, 0x02,                   // movzbl (%rdx), %r8d
                0x41, 0x38, 0xC0,                         // cmp %al, %r8b
                0x75, 0x0C,                               // jne 2f (+12)
                0x84, 0xC0,                               // test %al, %al
                0x74, 0x0C,                               // jz 3f (+12)
                0x48, 0xFF, 0xC1,                         // inc %rcx
                0x48, 0xFF, 0xC2,                         // inc %rdx
                0xEB, 0xE8,                               // jmp 1b (-24)
                0x44, 0x29, 0xC0,                         // 2: subl %r8d, %eax
                0xC3,                                     // ret
                0x31, 0xC0,                               // 3: xor %eax, %eax
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x640, s36, sizeof(s36));

            // Slot 37 (0x680): __p___argc
            uint64_t argcAddr = trampolineBaseVa + 0x700;
            uint8_t s37[11] = { 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            patchMovRax(s37, sizeof(s37), argcAddr);
            std::memcpy(trampolineBuffer + 0x680, s37, sizeof(s37));

            // Slot 38 (0x6C0): __p___argv
            uint64_t argvAddr = trampolineBaseVa + 0x708;
            uint8_t s38[11] = { 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            patchMovRax(s38, sizeof(s38), argvAddr);
            std::memcpy(trampolineBuffer + 0x6C0, s38, sizeof(s38));

            // Slot 39 (0x6E0): __p__environ / __p___initenv (returns pointer to environ pointer at 0x720)
            uint64_t envDataAddr = trampolineBaseVa + 0x720;
            uint8_t s39[11] = { 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            patchMovRax(s39, sizeof(s39), envDataAddr);
            std::memcpy(trampolineBuffer + 0x6E0, s39, sizeof(s39));

            // Slot 39b (0x6F0): __p__commode / __p__fmode / _errno / __sys_nerr (returns pointer to int=0 at 0x728)
            uint64_t commodeDataAddr = trampolineBaseVa + 0x728;
            uint8_t s39b[11] = { 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3 };
            patchMovRax(s39b, sizeof(s39b), commodeDataAddr);
            std::memcpy(trampolineBuffer + 0x6F0, s39b, sizeof(s39b));

            // Slot 40 (0x740): __acrt_iob_func (RCX = index)
            uint64_t streamsAddr = trampolineBaseVa + 0xD80;
            uint8_t s40[18] = {
                0x48, 0xC1, 0xE1, 0x06,                   // shl $6, %rcx (rcx * 64)
                0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov $streamsAddr, %rax
                0x48, 0x01, 0xC8,                         // add %rcx, %rax
                0xC3                                      // ret
            };
            patchMovRax(s40, sizeof(s40), streamsAddr);
            std::memcpy(trampolineBuffer + 0x740, s40, sizeof(s40));

            // Slot 41 (0x780): sqrtf / sqrt
            const uint8_t s41[] = { 0xF3, 0x0F, 0x51, 0xC0, 0xC3 };
            std::memcpy(trampolineBuffer + 0x780, s41, sizeof(s41));

            // Slot 42 (0x7C0): sinf / sin
            const uint8_t s42[] = {
                0x48, 0x83, 0xEC, 0x08,                   // sub $8, %rsp
                0xF3, 0x0F, 0x11, 0x04, 0x24,             // movss %xmm0, (%rsp)
                0xD9, 0x04, 0x24,                         // flds (%rsp)
                0xD9, 0xFE,                               // fsin
                0xD9, 0x1C, 0x24,                         // fstps (%rsp)
                0xF3, 0x0F, 0x10, 0x04, 0x24,             // movss (%rsp), %xmm0
                0x48, 0x83, 0xC4, 0x08,                   // add $8, %rsp
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x7C0, s42, sizeof(s42));

            // Slot 43 (0x800): cosf / cos
            const uint8_t s43[] = {
                0x48, 0x83, 0xEC, 0x08,                   // sub $8, %rsp
                0xF3, 0x0F, 0x11, 0x04, 0x24,             // movss %xmm0, (%rsp)
                0xD9, 0x04, 0x24,                         // flds (%rsp)
                0xD9, 0xFF,                               // fcos
                0xD9, 0x1C, 0x24,                         // fstps (%rsp)
                0xF3, 0x0F, 0x10, 0x04, 0x24,             // movss (%rsp), %xmm0
                0x48, 0x83, 0xC4, 0x08,                   // add $8, %rsp
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x800, s43, sizeof(s43));

            // Slot 43b (0x820): tanf / tan
            const uint8_t s43b[] = {
                0x48, 0x83, 0xEC, 0x08,                   // sub $8, %rsp
                0xF3, 0x0F, 0x11, 0x04, 0x24,             // movss %xmm0, (%rsp)
                0xD9, 0x04, 0x24,                         // flds (%rsp)
                0xD9, 0xF2,                               // fptan (pushes 1.0, then tan)
                0xDD, 0xD8,                               // fstp %st(0) (pop 1.0)
                0xD9, 0x1C, 0x24,                         // fstps (%rsp)
                0xF3, 0x0F, 0x10, 0x04, 0x24,             // movss (%rsp), %xmm0
                0x48, 0x83, 0xC4, 0x08,                   // add $8, %rsp
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x820, s43b, sizeof(s43b));

            // Slot 44 (0x840): round / roundf
            const uint8_t s44[] = { 0x66, 0x0F, 0x3A, 0x0A, 0xC0, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x840, s44, sizeof(s44));

            // Slot 45 (0x880): floorf / ceilf
            const uint8_t s45[] = { 0x66, 0x0F, 0x3A, 0x0A, 0xC0, 0x01, 0xC3 };
            std::memcpy(trampolineBuffer + 0x880, s45, sizeof(s45));

            // Slot 46 (0x8C0): fwrite (RCX = ptr, RDX = size, R8 = count, R9 = stream)
            // Starts with REX.WB per Suite 348 assertion
            const uint8_t s46[] = {
                0x49, 0x0F, 0xAF, 0xD0,                   // imul %r8, %rdx
                0x49, 0x89, 0xCA,                         // mov %rcx, %r10
                0xB8, 0x25, 0x00, 0x00, 0x00,             // mov $0x25, %eax
                0x0F, 0x05,                               // syscall
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x8C0, s46, sizeof(s46));

            // Slot 47 (0x8E0): puts / fputs / fputc / fputwc (RCX = str)
            const uint8_t s47[] = {
                0x49, 0x89, 0xCA,                         // mov %rcx, %r10 (syscall buffer)
                0x48, 0x89, 0xCA,                         // mov %rcx, %rdx (str -> %rdx)
                0x4D, 0x31, 0xC0,                         // xor %r8, %r8 (len = 0)
                0xB8, 0x25, 0x00, 0x00, 0x00,             // mov $0x25, %eax
                0x0F, 0x05,                               // syscall
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x8E0, s47, sizeof(s47));

            // Slot 48 (0x900): __pctype_func (returns pointer to s_pctypeTable at 0xA00)
            uint64_t pctypeAddr = trampolineBaseVa + 0xA00;
            uint8_t s48[] = {
                0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov $pctypeAddr, %rax
                0xC3                                                        // ret
            };
            patchMovRax(s48, sizeof(s48), pctypeAddr);
            std::memcpy(trampolineBuffer + 0x900, s48, sizeof(s48));

            // Slot 49 (0x920): localeconv (returns pointer to s_lconv at 0xC00)
            uint64_t lconvAddr = trampolineBaseVa + 0xC00;
            uint8_t s49[] = {
                0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov $lconvAddr, %rax
                0xC3                                                        // ret
            };
            patchMovRax(s49, sizeof(s49), lconvAddr);
            std::memcpy(trampolineBuffer + 0x920, s49, sizeof(s49));

            // Slot 50 (0x940): ___lc_codepage_func (returns 65001 = UTF-8 in EAX)
            const uint8_t s50[] = { 0xB8, 0xE9, 0xFD, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x940, s50, sizeof(s50));

            // Slot 51 (0x960): ___mb_cur_max_func (returns 4 in EAX)
            const uint8_t s51[] = { 0xB8, 0x04, 0x00, 0x00, 0x00, 0xC3 };
            std::memcpy(trampolineBuffer + 0x960, s51, sizeof(s51));

            // Slot 52 (0x980): __stdio_common_vfprintf / __stdio_common_vsprintf / printf (R8 = format string)
            const uint8_t s52[] = {
                0x4D, 0x89, 0xC2,                         // mov %r8, %r10 (format)
                0x4C, 0x89, 0xC2,                         // mov %r8, %rdx (format -> %rdx)
                0x4D, 0x31, 0xC0,                         // xor %r8, %r8 (len = 0)
                0xB8, 0x25, 0x00, 0x00, 0x00,             // mov $0x25, %eax
                0x0F, 0x05,                               // syscall
                0x31, 0xC0,                               // xor %eax, %eax
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x980, s52, sizeof(s52));

            // Slot 53 (0x9A0): InitOnceExecuteOnce (RCX = InitOnce, RDX = InitFn, R8 = Param, R9 = Context)
            const uint8_t s53[] = {
                0x48, 0x85, 0xC9,                         // test %rcx, %rcx
                0x74, 0x37,                               // jz fail (+55)
                0x48, 0x83, 0x39, 0x01,                   // cmpq $1, (%rcx)
                0x74, 0x2B,                               // je success (+43)
                0x48, 0x85, 0xD2,                         // test %rdx, %rdx
                0x74, 0x2C,                               // jz fail (+44)
                0x53,                                     // push %rbx
                0x56,                                     // push %rsi
                0x57,                                     // push %rdi
                0x51,                                     // push %rcx
                0x48, 0x83, 0xEC, 0x28,                   // sub $0x28, %rsp
                0x48, 0x89, 0xD0,                         // mov %rdx, %rax
                0x49, 0x89, 0xC2,                         // mov %r8, %rdx
                0x4D, 0x89, 0xC8,                         // mov %r9, %r8
                0xFF, 0xD0,                               // call *%rax
                0x48, 0x83, 0xC4, 0x28,                   // add $0x28, %rsp
                0x59,                                     // pop %rcx
                0x5F,                                     // pop %rdi
                0x5E,                                     // pop %rsi
                0x5B,                                     // pop %rbx
                0x85, 0xC0,                               // test %eax, %eax
                0x74, 0x0D,                               // jz fail (+13)
                0x48, 0xC7, 0x01, 0x01, 0x00, 0x00, 0x00, // movq $1, (%rcx)
                0xB8, 0x01, 0x00, 0x00, 0x00,             // mov $1, %eax
                0xC3,                                     // ret
                0x31, 0xC0,                               // xor %eax, %eax
                0xC3                                      // ret
            };
            std::memcpy(trampolineBuffer + 0x9A0, s53, sizeof(s53));

            // Populate pctype table at 0xA00 (256 uint16_t entries)
            uint16_t* pctype = reinterpret_cast<uint16_t*>(trampolineBuffer + 0xA00);
            for (int c = 0; c < 256; ++c) {
                uint16_t mask = 0;
                if (c >= 0x00 && c <= 0x1F) mask |= 0x20; // _CONTROL
                if (c == ' ' || (c >= 0x09 && c <= 0x0D)) mask |= 0x08; // _SPACE
                if (c == ' ') mask |= 0x40; // _BLANK
                if (c >= '0' && c <= '9') mask |= 0x04 | 0x80; // _DIGIT | _HEX
                if (c >= 'a' && c <= 'f') mask |= 0x02 | 0x80 | 0x0100; // _LOWER | _HEX | _ALPHA
                else if (c >= 'a' && c <= 'z') mask |= 0x02 | 0x0100; // _LOWER | _ALPHA
                if (c >= 'A' && c <= 'F') mask |= 0x01 | 0x80 | 0x0100; // _UPPER | _HEX | _ALPHA
                else if (c >= 'A' && c <= 'Z') mask |= 0x01 | 0x0100; // _UPPER | _ALPHA
                if ((c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
                    (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E)) mask |= 0x10; // _PUNCT
                pctype[c] = mask;
            }

            // Populate struct lconv and static string literals at 0xC00
            const char strDot[] = ".";
            const char strEmpty[] = "";
            const char16_t wstrDot[] = u".";
            const char16_t wstrEmpty[] = u"";
            std::memcpy(trampolineBuffer + 0xCA0, strDot, sizeof(strDot));
            std::memcpy(trampolineBuffer + 0xCA8, strEmpty, sizeof(strEmpty));
            std::memcpy(trampolineBuffer + 0xCB0, wstrDot, sizeof(wstrDot));
            std::memcpy(trampolineBuffer + 0xCB8, wstrEmpty, sizeof(wstrEmpty));

            uint64_t dotVa = trampolineBaseVa + 0xCA0;
            uint64_t emptyVa = trampolineBaseVa + 0xCA8;
            uint64_t wdotVa = trampolineBaseVa + 0xCB0;
            uint64_t wemptyVa = trampolineBaseVa + 0xCB8;

            std::memcpy(trampolineBuffer + 0xC00 + 0, &dotVa, sizeof(dotVa));
            std::memcpy(trampolineBuffer + 0xC00 + 8, &emptyVa, sizeof(emptyVa));
            for (size_t off = 16; off <= 72; off += 8) {
                std::memcpy(trampolineBuffer + 0xC00 + off, &emptyVa, sizeof(emptyVa));
            }
            std::memcpy(trampolineBuffer + 0xC00 + 88, &wdotVa, sizeof(wdotVa));
            std::memcpy(trampolineBuffer + 0xC00 + 96, &wemptyVa, sizeof(wemptyVa));
            for (size_t off = 104; off <= 144; off += 8) {
                std::memcpy(trampolineBuffer + 0xC00 + off, &wemptyVa, sizeof(wemptyVa));
            }

            // Runtime Data Area at 0x700
            int initArgc = 1;
            std::memcpy(trampolineBuffer + 0x700, &initArgc, sizeof(initArgc));

            uint64_t arg0Ptr = trampolineBaseVa + 0x360;
            uint64_t arg1Null = 0;
            std::memcpy(trampolineBuffer + 0x708, &arg0Ptr, sizeof(arg0Ptr));
            std::memcpy(trampolineBuffer + 0x710, &arg1Null, sizeof(arg1Null));

            // 0x720: __p__environ returns &environ. *(&environ) = 0x710 (pointer to NULL pointer)
            uint64_t envArrayPtr = trampolineBaseVa + 0x710;
            std::memcpy(trampolineBuffer + 0x720, &envArrayPtr, sizeof(envArrayPtr));

            // 0x728: __p__commode, __p__fmode, _errno point here (int = 0)
            int initZero = 0;
            std::memcpy(trampolineBuffer + 0x728, &initZero, sizeof(initZero));
            std::memcpy(trampolineBuffer + 0x72C, &initZero, sizeof(initZero));
            std::memcpy(trampolineBuffer + 0x730, &initZero, sizeof(initZero));

            // Zero-initialize stdio FILE streams at 0xD80 (3 * 64 bytes = 192 bytes, up to 0xE40)
            if (trampolineBufferSize >= 0xE40) {
                std::memset(trampolineBuffer + 0xD80, 0, 3 * 64);
            }
        }

        // 8. Parse and Bind Imports
        if (nt->optionalHeader.numberOfRvaAndSizes > pe::IMAGE_DIRECTORY_ENTRY_IMPORT) {
            const auto& importDir = nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (importDir.virtualAddress != 0 && importDir.size != 0 &&
                importDir.virtualAddress + importDir.size <= destinationBufferSize) {
                const auto* desc = reinterpret_cast<const pe::ImageImportDescriptor*>(destinationBuffer + importDir.virtualAddress);
                while (desc->name != 0 && desc->firstThunk != 0 &&
                       desc->name < destinationBufferSize && desc->firstThunk < destinationBufferSize) {
                    uint32_t thunkRva = desc->originalFirstThunk ? desc->originalFirstThunk : desc->firstThunk;
                    if (thunkRva >= destinationBufferSize) break;

                    const auto* thunkArray = reinterpret_cast<const uint64_t*>(destinationBuffer + thunkRva);
                    size_t idx = 0;
                    while (thunkRva + (idx + 1) * sizeof(uint64_t) <= destinationBufferSize && thunkArray[idx] != 0) {
                        uint64_t val = thunkArray[idx];
                        uint32_t iatRva = desc->firstThunk + static_cast<uint32_t>(idx * sizeof(uint64_t));
                        if (iatRva + sizeof(uint64_t) > destinationBufferSize) break;

                        uint64_t targetTrampoline = trampolineBaseVa + 0x200; // default to UniversalSuccess (returns 0/S_OK)

                        if (!(val & pe::IMAGE_ORDINAL_FLAG64)) {
                            uint32_t nameRva = static_cast<uint32_t>(val & 0xFFFFFFFF);
                            if (nameRva + 2 < destinationBufferSize) {
                                const auto* ibn = reinterpret_cast<const pe::ImageImportByName*>(destinationBuffer + nameRva);
                                const char* symName = ibn->name;

                                if (streq(symName, "ExitProcess") || streq(symName, "NtTerminateProcess") || streq(symName, "abort") || streq(symName, "_exit") || streq(symName, "exit")) {
                                    targetTrampoline = trampolineBaseVa + 0x00;
                                } else if (streq(symName, "NtQuerySystemInformation")) {
                                    targetTrampoline = trampolineBaseVa + 0x20;
                                } else if (streq(symName, "ReadFile") || streq(symName, "NtReadFile")) {
                                    targetTrampoline = trampolineBaseVa + 0x40;
                                } else if (streq(symName, "WriteFile") || streq(symName, "NtWriteFile")) {
                                    targetTrampoline = trampolineBaseVa + 0x60;
                                } else if (streq(symName, "NtYieldExecution") || streq(symName, "SwitchToThread") || streq(symName, "Sleep")) {
                                    targetTrampoline = trampolineBaseVa + 0x80;
                                } else if (streq(symName, "NtAllocateVirtualMemory") || streq(symName, "VirtualAlloc")) {
                                    targetTrampoline = trampolineBaseVa + 0xA0;
                                } else if (streq(symName, "CloseHandle") || streq(symName, "NtClose")) {
                                    targetTrampoline = trampolineBaseVa + 0xC0;
                                } else if (streq(symName, "GetStdHandle")) {
                                    targetTrampoline = trampolineBaseVa + 0xE0;
                                } else if (streq(symName, "GetTickCount64") || streq(symName, "GetTickCount")) {
                                    targetTrampoline = trampolineBaseVa + 0x100;
                                } else if (streq(symName, "QueryPerformanceCounter")) {
                                    targetTrampoline = trampolineBaseVa + 0x120;
                                } else if (streq(symName, "QueryPerformanceFrequency")) {
                                    targetTrampoline = trampolineBaseVa + 0x140;
                                } else if (streq(symName, "GetCurrentProcessId") || streq(symName, "GetCurrentThreadId") || streq(symName, "GetThreadId")) {
                                    targetTrampoline = trampolineBaseVa + 0x160;
                                } else if (streq(symName, "GetLastError")) {
                                    targetTrampoline = trampolineBaseVa + 0x180;
                                } else if (streq(symName, "SetLastError") || streq(symName, "InitializeCriticalSection") ||
                                           streq(symName, "DeleteCriticalSection") || streq(symName, "EnterCriticalSection") ||
                                           streq(symName, "LeaveCriticalSection") || streq(symName, "InitializeCriticalSectionAndSpinCount") ||
                                           streq(symName, "InitializeCriticalSectionEx") || streq(symName, "AcquireSRWLockExclusive") ||
                                           streq(symName, "ReleaseSRWLockExclusive") || streq(symName, "TryAcquireSRWLockExclusive") ||
                                           streq(symName, "TryEnterCriticalSection") || streq(symName, "WakeConditionVariable") ||
                                           streq(symName, "WakeAllConditionVariable") || streq(symName, "SleepConditionVariableSRW")) {
                                    targetTrampoline = trampolineBaseVa + 0x1A0;
                                } else if (streq(symName, "GetModuleHandleW") || streq(symName, "GetModuleHandleA") || streq(symName, "GetModuleHandleExW")) {
                                    targetTrampoline = trampolineBaseVa + 0x1C0;
                                } else if (streq(symName, "VirtualFree") || streq(symName, "SetEvent") || streq(symName, "ResetEvent") ||
                                           streq(symName, "TlsSetValue") || streq(symName, "TlsFree") || streq(symName, "IsNetworkAlive") ||
                                           streq(symName, "IsDestinationReachableW") || streq(symName, "IsProcessorFeaturePresent") ||
                                           streq(symName, "HeapFree") || streq(symName, "free") || streq(symName, "_aligned_free") ||
                                           streq(symName, "LocalFree") || streq(symName, "VirtualProtect")) {
                                    targetTrampoline = trampolineBaseVa + 0x1E0;
                                } else if (streq(symName, "GetCurrentPackageFullName")) {
                                    targetTrampoline = trampolineBaseVa + 0x220;
                                } else if (streq(symName, "CreateEventW") || streq(symName, "CreateEventA") || streq(symName, "CreateEventExW")) {
                                    targetTrampoline = trampolineBaseVa + 0x240;
                                } else if (streq(symName, "GetSystemTimeAsFileTime")) {
                                    targetTrampoline = trampolineBaseVa + 0x260;
                                } else if (streq(symName, "GetProcessHeap")) {
                                    targetTrampoline = trampolineBaseVa + 0x280;
                                } else if (streq(symName, "HeapAlloc")) {
                                    targetTrampoline = trampolineBaseVa + 0xF00;
                                } else if (streq(symName, "HeapReAlloc")) {
                                    targetTrampoline = trampolineBaseVa + 0xF20;
                                } else if (streq(symName, "GetCommandLineW")) {
                                    targetTrampoline = trampolineBaseVa + 0x2C0;
                                } else if (streq(symName, "GetCommandLineA")) {
                                    targetTrampoline = trampolineBaseVa + 0x2E0;
                                } else if (streq(symName, "GetFileType")) {
                                    targetTrampoline = trampolineBaseVa + 0x300;
                                } else if (streq(symName, "GetConsoleMode") || streq(symName, "GetConsoleCP") || streq(symName, "GetConsoleOutputCP")) {
                                    targetTrampoline = trampolineBaseVa + 0x320;
                                } else if (streq(symName, "TlsAlloc") || streq(symName, "FlsAlloc")) {
                                    targetTrampoline = trampolineBaseVa + 0x340;
                                } else if (streq(symName, "malloc") || streq(symName, "_aligned_malloc")) {
                                    targetTrampoline = trampolineBaseVa + 0x400;
                                } else if (streq(symName, "calloc")) {
                                    targetTrampoline = trampolineBaseVa + 0x440;
                                } else if (streq(symName, "realloc")) {
                                    targetTrampoline = trampolineBaseVa + 0x480;
                                } else if (streq(symName, "_initterm")) {
                                    targetTrampoline = trampolineBaseVa + 0x4C0;
                                } else if (streq(symName, "_initterm_e")) {
                                    targetTrampoline = trampolineBaseVa + 0x500;
                                } else if (streq(symName, "memcpy") || streq(symName, "memmove")) {
                                    targetTrampoline = trampolineBaseVa + 0x540;
                                } else if (streq(symName, "memset")) {
                                    targetTrampoline = trampolineBaseVa + 0x580;
                                } else if (streq(symName, "memcmp") || streq(symName, "memchr")) {
                                    targetTrampoline = trampolineBaseVa + 0x5C0;
                                } else if (streq(symName, "strlen") || streq(symName, "strnlen") || streq(symName, "wcslen") || streq(symName, "wcsnlen")) {
                                    targetTrampoline = trampolineBaseVa + 0x600;
                                } else if (streq(symName, "strcmp") || streq(symName, "strncmp") || streq(symName, "_strcoll_l") || streq(symName, "_wcscoll_l")) {
                                    targetTrampoline = trampolineBaseVa + 0x640;
                                } else if (streq(symName, "__p___argc")) {
                                    targetTrampoline = trampolineBaseVa + 0x680;
                                } else if (streq(symName, "__p___argv") || streq(symName, "__p___wargv")) {
                                    targetTrampoline = trampolineBaseVa + 0x6C0;
                                } else if (streq(symName, "__p__environ") || streq(symName, "__p___initenv")) {
                                    targetTrampoline = trampolineBaseVa + 0x6E0;
                                } else if (streq(symName, "__p__commode") || streq(symName, "__p__fmode") ||
                                           streq(symName, "_errno") || streq(symName, "__sys_nerr")) {
                                    targetTrampoline = trampolineBaseVa + 0x6F0;
                                } else if (streq(symName, "__acrt_iob_func")) {
                                    targetTrampoline = trampolineBaseVa + 0x740;
                                } else if (streq(symName, "sqrtf") || streq(symName, "sqrt")) {
                                    targetTrampoline = trampolineBaseVa + 0x780;
                                } else if (streq(symName, "sinf") || streq(symName, "sin")) {
                                    targetTrampoline = trampolineBaseVa + 0x7C0;
                                } else if (streq(symName, "cosf") || streq(symName, "cos")) {
                                    targetTrampoline = trampolineBaseVa + 0x800;
                                } else if (streq(symName, "tanf") || streq(symName, "tan")) {
                                    targetTrampoline = trampolineBaseVa + 0x820;
                                } else if (streq(symName, "round") || streq(symName, "roundf")) {
                                    targetTrampoline = trampolineBaseVa + 0x840;
                                } else if (streq(symName, "floorf") || streq(symName, "floor") || streq(symName, "ceilf") || streq(symName, "ceil")) {
                                    targetTrampoline = trampolineBaseVa + 0x880;
                                } else if (streq(symName, "fwrite")) {
                                    targetTrampoline = trampolineBaseVa + 0x8C0;
                                } else if (streq(symName, "puts") || streq(symName, "fputs") || streq(symName, "fputc") || streq(symName, "fputwc")) {
                                    targetTrampoline = trampolineBaseVa + 0x8E0;
                                } else if (streq(symName, "__pctype_func")) {
                                    targetTrampoline = trampolineBaseVa + 0x900;
                                } else if (streq(symName, "localeconv")) {
                                    targetTrampoline = trampolineBaseVa + 0x920;
                                } else if (streq(symName, "___lc_codepage_func")) {
                                    targetTrampoline = trampolineBaseVa + 0x940;
                                } else if (streq(symName, "___mb_cur_max_func")) {
                                    targetTrampoline = trampolineBaseVa + 0x960;
                                } else if (streq(symName, "__stdio_common_vfprintf") || streq(symName, "__stdio_common_vsprintf") ||
                                           streq(symName, "__stdio_common_vfwprintf") || streq(symName, "vfprintf") ||
                                           streq(symName, "printf")) {
                                    targetTrampoline = trampolineBaseVa + 0x980;
                                } else if (streq(symName, "InitOnceExecuteOnce")) {
                                    targetTrampoline = trampolineBaseVa + 0x9A0;
                                } else if (streq(symName, "GetActiveProcessorCount") || streq(symName, "_fileno") ||
                                           streq(symName, "TryAcquireSRWLockExclusive") || streq(symName, "TryEnterCriticalSection")) {
                                    targetTrampoline = trampolineBaseVa + 0x1E0;
                                } else if (streq(symName, "TlsGetValue") || streq(symName, "FlsGetValue") || streq(symName, "IsDebuggerPresent") ||
                                           streq(symName, "_set_app_type") || streq(symName, "_configure_narrow_argv") ||
                                           streq(symName, "_initialize_narrow_environment") || streq(symName, "_seh_filter_exe") ||
                                           streq(symName, "_crt_atexit") || streq(symName, "_set_invalid_parameter_handler") ||
                                           streq(symName, "_set_new_mode") || streq(symName, "fpreset") || streq(symName, "setvbuf") ||
                                           streq(symName, "setbuf") || streq(symName, "fflush") || streq(symName, "fclose") ||
                                           streq(symName, "_lock_file") || streq(symName, "_unlock_file") ||
                                           streq(symName, "SetUnhandledExceptionFilter") || streq(symName, "VirtualProtect") ||
                                           streq(symName, "VirtualQuery") || streq(symName, "setlocale") ||
                                           streq(symName, "_configthreadlocale") || streq(symName, "_create_locale") ||
                                           streq(symName, "_free_locale")) {
                                    targetTrampoline = trampolineBaseVa + 0x200;
                                }
                            }
                        }

                        *reinterpret_cast<uint64_t*>(destinationBuffer + iatRva) = targetTrampoline;
                        outInfo.boundImportsCount++;
                        idx++;
                    }
                    desc++;
                }
            }
        }

        // 9. Register Process & Initial Thread Context
        uint64_t entryPointVa = targetBaseVa + nt->optionalHeader.addressOfEntryPoint;
        outInfo.imageBaseVa = targetBaseVa;
        outInfo.entryPointVa = entryPointVa;

        if (registerAsProcess) {
            auto& pm = UserProcessManager64::get();
            auto* proc = pm.createProcess(imageName, pid, parentPid,
                                          targetBaseVa, nt->optionalHeader.sizeOfImage,
                                          userStackTopVa, userStackSize,
                                          pebVa, tebVa);
            if (proc) {
                proc->peb.imageBaseAddress = targetBaseVa;
                proc->peb.imageSubsystem = nt->optionalHeader.subsystem;
                proc->peb.imageSubsystemMajorVersion = nt->optionalHeader.majorSubsystemVersion;
                proc->peb.imageSubsystemMinorVersion = nt->optionalHeader.minorSubsystemVersion;
                proc->peb.osMajorVersion = 10;
                proc->peb.osMinorVersion = 0;
                proc->peb.osBuildNumber = 26100;
            }

            auto& sched = UserThreadScheduler64::get();
            sched.createThread(pid, pid, 16, entryPointVa, userStackTopVa, tebVa, imageName);
        }

        return micant::NtStatus::Success;
    }
};

} // namespace micant::um
