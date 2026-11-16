#pragma once

#if defined(MICANT_FREESTANDING)
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/se.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/core/sync.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/shared/context.hpp"
#include "micant/um/native/pe.hpp"
#include <array>
#include <memory>
#include <string_view>

namespace micant::ke { struct KThread; }
namespace micant::um { class NativeUserImageSet64; }
namespace micant::section { class SectionObject; }
namespace micant::ps {
namespace detail { struct ProcessObjectFactory; struct ThreadObjectFactory; struct ThreadReaper; }
// Process-owned bootstrap address space. Lifecycle operations require the
// single BSP with interrupts disabled; this is not yet NtCreateProcessEx.
class EProcess {
public:
    EProcess();
    ~EProcess();
    EProcess(const EProcess&) = delete;
    EProcess& operator=(const EProcess&) = delete;
    NtStatus initializeAddressSpace() noexcept;
    NtStatus closeAddressSpace() noexcept;
    NtStatus cloneNativeMetadata(const EProcess& source) noexcept;
    NtStatus attachThread(ke::KThread& thread) noexcept;
    NtStatus detachThread(ke::KThread& thread) noexcept;
    NtStatus referenceAddressSpaceForAttach(uintptr_t& directoryTableBase,
                                            ob::ObjectHeader*& reference) noexcept;
    void releaseAddressSpaceAttachReference(ob::ObjectHeader* reference) noexcept;
    // Bounded current-process termination; successful termination does not
    // return. The owning context performs deferred cleanup after switch.
    NtStatus terminateCurrent(NtStatus exitStatus) noexcept;
    NtStatus terminateOther(NtStatus exitStatus) noexcept;
    NtStatus terminateOtherThreads(NtStatus exitStatus) noexcept;
    // Internal BSP image loader; caller holds the active process root and
    // supplies kernel-resident inputs, which are captured by the process.
    NtStatus loadNativeImage(std::string_view name, std::span<const uint8_t> file,
                             std::u16string_view fullPath = {}) noexcept;
    // The supplied strings are borrowed until image finalization.
    NtStatus setNativeProcessParameters(std::u16string_view imagePath,
                                        std::u16string_view commandLine,
                                        std::u16string_view currentDirectory = {}) noexcept;
    NtStatus finalizeNativeImages() noexcept;
    NtStatus initializeMainImageFromSection(section::SectionObject& image,
                                            ob::ObjectHeader* sectionReference) noexcept;
    NtStatus replaceMainImageFromSection(section::SectionObject& image,
                                         ob::ObjectHeader* sectionReference) noexcept;
    [[nodiscard]] const um::NativeUserImageSet64* nativeImages() const noexcept { return images_.get(); }
    [[nodiscard]] mm::ProcessAddressSpace& getAddressSpace() noexcept { return addressSpace_; }
    [[nodiscard]] ob::HandleTable& getHandleTable() noexcept { return *handles_; }
    [[nodiscard]] uintptr_t getDirectoryTableBase() const noexcept { return root_.pfn << mm::PageShift4KB; }
    [[nodiscard]] Handle processId() const noexcept { return processId_; }
    [[nodiscard]] Handle inheritedFromProcessId() const noexcept { return inheritedFromProcessId_; }
    [[nodiscard]] std::u16string_view processImageName() const noexcept {
        return {processImageName_, processImageNameLength_};
    }
    [[nodiscard]] uintptr_t userPebAddress() const noexcept { return userPeb_; }
    [[nodiscard]] uintptr_t userProcessParametersAddress() const noexcept { return userProcessParameters_; }
    [[nodiscard]] bool nativeImagesFinalized() const noexcept { return imagesFinalized_; }
    [[nodiscard]] std::span<const pe::PeImageView64::TlsLayout> userTlsLayouts() const noexcept {
        return {userTlsLayouts_.get(), userTlsCount_};
    }
    [[nodiscard]] bool isTerminating() const noexcept { return terminating_; }
    [[nodiscard]] bool isSystemProcess() const noexcept { return systemProcess_; }
    [[nodiscard]] NtStatus referencePrimaryToken(se::FreestandingAccessToken*& token,
                                                  ob::ObjectHeader*& reference) noexcept {
        token = nullptr; reference = nullptr;
        ob::detail::HandleTableGuard guard;
        if (!primaryToken_ || !primaryTokenReference_) return NtStatus::AccessDenied;
        primaryTokenReference_->addReference();
        token = primaryToken_; reference = primaryTokenReference_;
        return NtStatus::Success;
    }
    [[nodiscard]] NtStatus exitStatus() const noexcept { return exitStatus_; }
    [[nodiscard]] size_t activeThreadCount() const noexcept;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    // The Ob reference held by a waiter keeps this resident until wait returns.
    [[nodiscard]] sync::detail::BootstrapEvent& terminationEvent() noexcept { return terminationEvent_; }
#endif
private:
    friend struct detail::ProcessObjectFactory;
    friend struct detail::ThreadReaper;
    friend class EThread;
    void captureProcessImageName(std::string_view name) noexcept;
    void onThreadTermination(NtStatus exitStatus) noexcept;
    NtStatus requestTermination(NtStatus exitStatus, ke::KThread* callingThread,
                                bool terminateProcess = true) noexcept;
    ob::ObjectHeader* objectHeader_{nullptr};
    ob::ObjectHeader* primaryTokenReference_{nullptr};
    se::FreestandingAccessToken* primaryToken_{nullptr};
    hal::amd64::ProcessPagingRoot root_{};
    Handle processId_{0};
    Handle inheritedFromProcessId_{0};
    // Bounded basename captured from the selected executable image.
    char16_t processImageName_[128]{};
    uint16_t processImageNameLength_{0};
    // Weak CID registry link; removal precedes final Ob deletion.
    EProcess* cidNext_{nullptr};
    bool cidRegistered_{false};
    mm::ProcessAddressSpace addressSpace_{};
    std::unique_ptr<ob::HandleTable> handles_{};
    size_t attachedThreads_{0};
    size_t activeAttachReferences_{0};
    ke::KThread* threads_{nullptr};
    bool terminating_{false};
    bool processPublished_{false};
    bool addressSpaceClosing_{false};
    bool synchronousCleanupRetry_{false};
    EProcess* deferredNext_{nullptr};
    bool deferredQueued_{false};
    // The reaper owns its queue reference while cleanup is in flight.
    bool deferredProcessing_{false};
    bool systemProcess_{false};
    NtStatus exitStatus_{NtStatus::Pending};
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    sync::detail::BootstrapEvent terminationEvent_{true};
#endif
    std::unique_ptr<um::NativeUserImageSet64> images_{};
    struct OwnedImageInput {
        std::unique_ptr<uint8_t[]> bytes;
        std::unique_ptr<char[]> name;
        std::unique_ptr<char16_t[]> path;
    };
    std::unique_ptr<OwnedImageInput[]> ownedImageInputs_{};
    std::unique_ptr<char16_t[]> ownedImagePath_{};
    std::unique_ptr<char16_t[]> ownedCommandLine_{};
    std::unique_ptr<char16_t[]> ownedCurrentDirectory_{};
    bool imagesFinalized_{false};
    bool imageStartupCallbacksStarted_{false};
    uintptr_t userPeb_{0};
    uintptr_t userLoaderData_{0};
    uintptr_t userProcessParameters_{0};
    // Read-only user mapping of the validated file-backed API-set namespace.
    // The address-space VAD owns its pages; this field tracks the PEB target.
    uintptr_t userApiSetMap_{0};
    size_t userApiSetMapSize_{0};
    std::array<std::u16string_view, 16> nativeImagePaths_{};
    std::u16string_view processImagePath_{};
    std::u16string_view processCommandLine_{};
    std::u16string_view processCurrentDirectory_{};
    bool processParametersConfigured_{false};
    bool imageSectionBacked_{false};
    std::unique_ptr<pe::PeImageView64::TlsLayout[]> userTlsLayouts_{};
    size_t userTlsCount_{0};
};

// Kernel-internal issuance entry point. It derives authority from the current
// process primary token; callers cannot nominate a different issuer token.
[[nodiscard]] NtStatus PsCreateFreestandingLogonToken(
    const se::FreestandingLogonTokenRequest& request,
    se::FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept;
// Kernel-internal primary-token assignment for a live referenced process.
// The current process must hold SeAssignPrimaryTokenPrivilege.
[[nodiscard]] NtStatus PsAssignFreestandingPrimaryToken(
    EProcess& target, ob::ObjectHeader* targetReference,
    se::FreestandingAccessToken& token, ob::ObjectHeader* tokenReference) noexcept;

// Public WDK kernel entry points; kernel handles are never exposed to Ring 3.
extern "C" __declspec(dllexport) NtStatus PsCreateSystemThread(Handle* threadHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle processHandle, ClientId* clientId,
    void (*startRoutine)(void*), void* startContext) noexcept;
extern "C" __declspec(dllexport) NtStatus PsTerminateSystemThread(NtStatus exitStatus) noexcept;

// BSP bootstrap thread lifecycle; kernel stack and dispatcher context remain
// owned until the stopped thread is reaped from another context.
class EThread {
public:
    using StartRoutine = void (*)(void*) noexcept;
    using SystemStartRoutine = void (*)(void*);
    EThread() = default;
    ~EThread();
    EThread(const EThread&) = delete;
    EThread& operator=(const EThread&) = delete;
    NtStatus initialize(EProcess& process, size_t stackSize, StartRoutine start, void* argument) noexcept;
    NtStatus initializeSystem(EProcess& process, size_t stackSize, SystemStartRoutine start, void* argument) noexcept;
    NtStatus terminateSystemThread(NtStatus status) noexcept;
    // BSP-only, with the process root active. Static TLS callbacks are not yet
    // supported; the image and TLS layouts must remain valid during setup.
    NtStatus initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                            size_t userStackSize, const pe::PeImageView64::TlsLayout* tls,
                            bool interruptible = false) noexcept;
    NtStatus initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                            size_t userStackSize, bool interruptible = false) noexcept;
    NtStatus initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                            size_t reserveSize, size_t commitSize, bool interruptible) noexcept;
    NtStatus ready() noexcept;
    NtStatus suspendInitially() noexcept;
    NtStatus resumeInitial(uint32_t& previousCount) noexcept;
    // Roll back a thread that has not begun executing. BSP only, with
    // interrupts disabled; running and waiting threads cannot be cancelled.
    // A queued APC requires asynchronous target-context rundown before reap.
    NtStatus cancelBeforeStart() noexcept;
    NtStatus rollbackBeforeStart() noexcept;
    // Retain an unpublished, never-started object for a deferred reap when
    // immediate rollback encounters a transient cleanup failure.
    void deferFailedCreationReap() noexcept;
    // Successful current-thread exit never returns; stack release is deferred.
    NtStatus exitCurrent(NtStatus status) noexcept;
    // Request target-context termination on the bootstrap processor. A thread
    // blocked in the canonical Ke event wait may be woken and terminated
    // after the wait syscall releases its object reference; other wait engines
    // remain unsupported.
    NtStatus terminateOther(NtStatus status) noexcept;
    NtStatus reap() noexcept;
    [[nodiscard]] ke::KThread* kernelThread() noexcept { return thread_.get(); }
    [[nodiscard]] NtStatus referenceImpersonationToken(se::FreestandingAccessToken*& token,
                                                        ob::ObjectHeader*& reference) noexcept {
        token = nullptr; reference = nullptr;
        ob::detail::HandleTableGuard guard;
        if (!impersonationToken_) return NtStatus::Success;
        if (!impersonationTokenReference_) return NtStatus::Unsuccessful;
        impersonationTokenReference_->addReference();
        token = impersonationToken_; reference = impersonationTokenReference_;
        return NtStatus::Success;
    }
    [[nodiscard]] NtStatus setImpersonationToken(se::FreestandingAccessToken* token,
        ob::ObjectHeader* reference, const se::FreestandingAccessToken* callerToken,
        uint32_t impersonationLevel) noexcept {
        if ((token == nullptr) != (reference == nullptr))
            return NtStatus::InvalidParameter;
        if (token && (token->IsPrimary || impersonationLevel < 2 || impersonationLevel > 3 ||
            !se::SeIsFreestandingTokenObject(token, reference))) return NtStatus::InvalidParameter;
        if (token && !se::SeCanImpersonateFreestanding(callerToken, token)) return NtStatus::PrivilegeNotHeld;
        if (reference) reference->addReference();
        ob::ObjectHeader* old{};
        {
            ob::detail::HandleTableGuard guard;
            old = impersonationTokenReference_;
            impersonationTokenReference_ = reference;
            impersonationToken_ = token;
        }
        if (old) se::SeDereferenceFreestandingToken(old);
        return NtStatus::Success;
    }
    [[nodiscard]] NtStatus exitStatus() const noexcept { return exitStatus_; }
    [[nodiscard]] Handle threadId() const noexcept { return thread_ ? thread_->tid : 0; }
    [[nodiscard]] uintptr_t userStackPointer() const noexcept { return userStackPointer_; }
    [[nodiscard]] bool isSystemThread() const noexcept { return systemThread_; }
    [[nodiscard]] uintptr_t userTebAddress() const noexcept { return userTeb_; }
    [[nodiscard]] Handle ownerProcessId() const noexcept { return process_ ? process_->processId() : 0; }
    // Caller handles the trap frame and passes a fault from this thread's
    // current process; no user exception is dispatched by this helper.
    NtStatus resolveUserStackGuard(uintptr_t address) noexcept;
    // Build the AMD64 user exception frame on the faulting thread's stack.
    // The HAL owns trap-frame restoration; this method only validates and
    // redirects the saved user return frame to NTDLL's exception dispatcher.
    NtStatus prepareUserException(uint32_t exceptionCode, uint32_t exceptionVector,
                                  uintptr_t address,
                                  uint64_t* hardwareFrame, uint64_t* savedRegisters,
                                  const void* floatingState,
                                  const void* capturedRecords = nullptr,
                                  size_t capturedRecordCount = 0,
                                  uintptr_t deliveryStackTop = 0,
                                  const hal::amd64::UserSegmentState* savedSegments = nullptr) noexcept;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    NtStatus prepareUserApc(const ke::UserApcDelivery* deliveries, size_t deliveryCount,
                            uint64_t* hardwareFrame, uint64_t* savedRegisters,
                            const void* floatingState) noexcept;
#endif
    NtStatus configureUserStart(uintptr_t routine, uintptr_t argument) noexcept;
    [[nodiscard]] uintptr_t userTlsStorage() const noexcept { return userTls_ ? userTls_ + 0x100 : 0; }
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    [[nodiscard]] sync::detail::BootstrapEvent& terminationEvent() noexcept { return terminationEvent_; }
#endif
private:
    friend struct detail::ThreadObjectFactory;
    friend struct detail::ThreadReaper;
    friend class EProcess;
    static void startThread(void* argument) noexcept;
    static void signalTermination(void* argument) noexcept;
    static void serviceTermination(void* argument) noexcept;
    static void startUser(void* argument) noexcept;
    static void startSystem(void* argument) noexcept;
    NtStatus initializeUserWithTls(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                   size_t userStackSize, std::span<const pe::PeImageView64::TlsLayout> tls,
                                   bool interruptible, size_t commitSize = 0) noexcept;
    NtStatus releaseUserMemory() noexcept;
    EProcess* process_{nullptr};
    // A factory-owned reference pins an Ob thread until deferred reap.
    ob::ObjectHeader* objectHeader_{nullptr};
    ob::ObjectHeader* impersonationTokenReference_{nullptr};
    se::FreestandingAccessToken* impersonationToken_{nullptr};
    ob::ObjectHeader* reapProcessReference_{nullptr};
    // The ETHREAD keeps its creating process object alive until the thread
    // object itself is deleted, even after its execution resources are reaped.
    ob::ObjectHeader* retiredProcessReference_{nullptr};
    EThread* deferredNext_{nullptr};
    ke::KThread* retiredThreads_{nullptr};
    bool deferredQueued_{false};
    bool reapInProgress_{false};
    bool objectOwned_{false};
    bool systemThread_{false};
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    sync::detail::BootstrapEvent terminationEvent_{true};
#endif
    bool processDetached_{false};
    std::unique_ptr<ke::KThread> thread_{};
    std::unique_ptr<uint8_t[]> stack_{};
    StartRoutine start_{nullptr};
    void* argument_{nullptr};
    SystemStartRoutine systemStart_{nullptr};
    void* systemArgument_{nullptr};
    bool started_{false};
    NtStatus exitStatus_{NtStatus::Pending};
    NtStatus requestedExitStatus_{NtStatus::Success};
    uintptr_t userEntry_{0};
    uintptr_t userStartupEntry_{0};
    uintptr_t userStartupThunk_{0};
    bool userStartupCallbacks_{false};
    uintptr_t userStartRoutine_{0};
    uintptr_t userStartArgument_{0};
    uintptr_t userStack_{0};
    // Kernel-owned address of the next stack guard; never trust the writable TEB.
    uintptr_t userStackGuard_{0};
    uintptr_t userStackLimit_{0};
    uintptr_t userStackTop_{0};
    uintptr_t userStackPointer_{0};
    uintptr_t userTeb_{0};
    uintptr_t userTls_{0};
    bool userInterruptible_{false};
};
}
#else
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <algorithm>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/se.hpp"

namespace micant::ps {

// ============================================================================
// 1. Thread Execution State & Security Impersonation Level
// ============================================================================
enum class ThreadState : uint32_t {
    Initialized,
    Ready,
    Running,
    Standby,
    Terminated,
    Waiting,
    Transition,
    DeferredReady
};

enum class SecurityImpersonationLevel : uint32_t {
    SecurityAnonymous      = 0,
    SecurityIdentification = 1,
    SecurityImpersonation  = 2,
    SecurityDelegation     = 3
};

// Canonical Windows NT Priority Levels
inline constexpr uint32_t LOW_PRIORITY          = 0;
inline constexpr uint32_t LOW_REALTIME_PRIORITY = 16;
inline constexpr uint32_t HIGH_PRIORITY         = 31;
inline constexpr uint32_t MAXIMUM_PRIORITY      = 32;

// Standard Dave Cutler Priority Boost Increments
inline constexpr uint32_t IO_NO_INCREMENT       = 0;
inline constexpr uint32_t IO_DISK_INCREMENT     = 1;
inline constexpr uint32_t IO_NETWORK_INCREMENT  = 2;
inline constexpr uint32_t IO_KEYBOARD_INCREMENT = 6;
inline constexpr uint32_t IO_MOUSE_INCREMENT    = 6;
inline constexpr uint32_t IO_SOUND_INCREMENT    = 8;

// Canonical Windows NT Job Object Limit Flags
inline constexpr uint32_t JOB_OBJECT_LIMIT_WORKINGSET                 = 0x00000001;
inline constexpr uint32_t JOB_OBJECT_LIMIT_PROCESS_TIME               = 0x00000002;
inline constexpr uint32_t JOB_OBJECT_LIMIT_JOB_TIME                   = 0x00000004;
inline constexpr uint32_t JOB_OBJECT_LIMIT_ACTIVE_PROCESS             = 0x00000008;
inline constexpr uint32_t JOB_OBJECT_LIMIT_AFFINITY                   = 0x00000010;
inline constexpr uint32_t JOB_OBJECT_LIMIT_PRIORITY_CLASS             = 0x00000020;
inline constexpr uint32_t JOB_OBJECT_LIMIT_PRESERVE_JOB_TIME          = 0x00000040;
inline constexpr uint32_t JOB_OBJECT_LIMIT_SCHEDULING_CLASS           = 0x00000080;
inline constexpr uint32_t JOB_OBJECT_LIMIT_PROCESS_MEMORY             = 0x00000100;
inline constexpr uint32_t JOB_OBJECT_LIMIT_JOB_MEMORY                 = 0x00000200;
inline constexpr uint32_t JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION = 0x00000400;
inline constexpr uint32_t JOB_OBJECT_LIMIT_BREAKAWAY_OK               = 0x00000800;
inline constexpr uint32_t JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK        = 0x00001000;
inline constexpr uint32_t JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE          = 0x00002000;

// ============================================================================
// 2. Diagnostic Telemetry Subsystem (PS_TELEMETRY)
// ============================================================================
struct PS_TELEMETRY {
    size_t totalProcessesCreated{0};
    size_t totalProcessesTerminated{0};
    size_t totalThreadsCreated{0};
    size_t totalThreadsTerminated{0};
    size_t totalJobsCreated{0};
    size_t totalProcessesAssignedToJobs{0};
    size_t totalProcessCallbacksInvoked{0};
    size_t totalThreadCallbacksInvoked{0};
    size_t totalQuotaViolations{0};
    size_t totalThreadPriorityBoosts{0};
    size_t totalThreadSuspensions{0};
    size_t totalThreadResumptions{0};
};

inline PS_TELEMETRY g_PsTelemetry{};
inline std::mutex g_PsTelemetryMutex{};

inline PS_TELEMETRY PsQueryTelemetry() {
    std::lock_guard<std::mutex> lock(g_PsTelemetryMutex);
    return g_PsTelemetry;
}

inline void PsResetTelemetryForTesting() {
    std::lock_guard<std::mutex> lock(g_PsTelemetryMutex);
    g_PsTelemetry = PS_TELEMETRY{};
}

// Forward Declarations
class ProcessManager;
class JobObject;
class EProcess;
class EThread;

/**
 * @brief x86-64 Machine Architecture Context Frame.
 */
struct ContextFrame {
    uint64_t rip{0};
    uint64_t rsp{0};
    uint64_t rflags{0x202}; // IF (Interrupt Flag) enabled
    uint64_t rax{0};
    uint64_t rcx{0};
    uint64_t rdx{0};
    uint64_t rbx{0};
    uint64_t rbp{0};
    uint64_t rsi{0};
    uint64_t rdi{0};
    uint64_t r8{0};
    uint64_t r9{0};
    uint64_t r10{0};
    uint64_t r11{0};
    uint64_t r12{0};
    uint64_t r13{0};
    uint64_t r14{0};
    uint64_t r15{0};
    uint16_t cs{0x33}; // User code segment (Ring 3 64-bit)
    uint16_t ss{0x2B}; // User data segment (Ring 3)
};

/**
 * @brief Process Environment Block (PEB).
 * Standard userland structure pointed to by TEB->ProcessEnvironmentBlock (GS:[0x60]).
 */
struct Peb {
    uint8_t  inheritedAddressSpace{0};
    uint8_t  readImageFileExecOptions{0};
    uint8_t  beingDebugged{0};
    uint8_t  bitField{0};
    uint64_t mutant{0};
    uint64_t imageBaseAddress{0};
    uint64_t ldr{0};
    uint64_t processParameters{0};
    uint64_t subSystemData{0};
    uint64_t processHeap{0};
    uint64_t fastPebLock{0};
    uint32_t numberOfProcessors{1};
    uint32_t ntGlobalFlag{0};
    uint16_t osMajorVersion{10};
    uint16_t osMinorVersion{0};
    uint16_t osBuildNumber{26100}; // Windows 11 base build
    uint16_t osCsdVersion{0};
    uint32_t osPlatformId{2};      // VER_PLATFORM_WIN32_NT
    uint32_t imageSubsystem{3};    // IMAGE_SUBSYSTEM_WINDOWS_CUI
    uint32_t imageSubsystemMajorVersion{10};
    uint32_t imageSubsystemMinorVersion{0};
    uint32_t sessionId{0};
};

/**
 * @brief Thread Environment Block (TEB).
 * Standard userland per-thread block mapped at GS:[0x30] on x86_64.
 */
struct Teb {
    struct {
        uint64_t exceptionList{0};
        uint64_t stackBase{0};
        uint64_t stackLimit{0};
        uint64_t subSystemTib{0};
        uint64_t fiberData{0};
        uint64_t arbitraryUserPointer{0};
        uint64_t self{0}; // Points to TEB itself (GS:[0x30])
    } ntTib;

    uint64_t environmentPointer{0};
    ClientId clientId{};
    uint64_t activeRpcHandle{0};
    uint64_t threadLocalStoragePointer{0};
    uint64_t processEnvironmentBlock{0}; // Pointer to PEB (GS:[0x60])
    uint32_t lastErrorValue{0};
    uint32_t countOfOwnedCriticalSections{0};
    uint32_t hardErrorMode{0};
};

/**
 * @brief Canonical Windows NT Process Quota Block (EPROCESS_QUOTA_BLOCK).
 */
struct ProcessQuotaBlock {
    size_t pagedPoolLimit{0};    // 0 = unlimited
    size_t pagedPoolUsage{0};
    size_t nonPagedPoolLimit{0}; // 0 = unlimited
    size_t nonPagedPoolUsage{0};
    size_t pagefileLimit{0};     // 0 = unlimited
    size_t pagefileUsage{0};
    size_t workingSetMinimum{20 * 4096};
    size_t workingSetMaximum{200 * 4096};
    size_t peakVirtualSize{0};
    size_t peakWorkingSetSize{0};

    [[nodiscard]] NtStatus chargePagedPool(size_t bytes) noexcept {
        if (pagedPoolLimit > 0 && pagedPoolUsage + bytes > pagedPoolLimit) {
            std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
            g_PsTelemetry.totalQuotaViolations++;
            return NtStatus::QuotaExceeded;
        }
        pagedPoolUsage += bytes;
        return NtStatus::Success;
    }

    void returnPagedPool(size_t bytes) noexcept {
        if (bytes <= pagedPoolUsage) pagedPoolUsage -= bytes;
        else pagedPoolUsage = 0;
    }

    [[nodiscard]] NtStatus chargeNonPagedPool(size_t bytes) noexcept {
        if (nonPagedPoolLimit > 0 && nonPagedPoolUsage + bytes > nonPagedPoolLimit) {
            std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
            g_PsTelemetry.totalQuotaViolations++;
            return NtStatus::QuotaExceeded;
        }
        nonPagedPoolUsage += bytes;
        return NtStatus::Success;
    }

    void returnNonPagedPool(size_t bytes) noexcept {
        if (bytes <= nonPagedPoolUsage) nonPagedPoolUsage -= bytes;
        else nonPagedPoolUsage = 0;
    }

    [[nodiscard]] NtStatus chargePagefile(size_t bytes) noexcept {
        if (pagefileLimit > 0 && pagefileUsage + bytes > pagefileLimit) {
            std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
            g_PsTelemetry.totalQuotaViolations++;
            return NtStatus::PagefileQuotaExceeded;
        }
        pagefileUsage += bytes;
        return NtStatus::Success;
    }

    void returnPagefile(size_t bytes) noexcept {
        if (bytes <= pagefileUsage) pagefileUsage -= bytes;
        else pagefileUsage = 0;
    }
};

/**
 * @brief Job Accounting and Limit Structures.
 */
struct JobBasicAccountingInformation {
    uint64_t totalUserTime{0};
    uint64_t totalKernelTime{0};
    uint64_t thisPeriodTotalUserTime{0};
    uint64_t thisPeriodTotalKernelTime{0};
    uint32_t totalPageFaultCount{0};
    uint32_t totalProcesses{0};
    uint32_t activeProcesses{0};
    uint32_t totalTerminatedProcesses{0};
};

struct JobBasicLimitInformation {
    int64_t  perProcessUserTimeLimit{0};
    int64_t  perJobUserTimeLimit{0};
    uint32_t limitFlags{0};
    size_t   minimumWorkingSetSize{0};
    size_t   maximumWorkingSetSize{0};
    uint32_t activeProcessLimit{0};
    uint64_t affinity{0};
    uint32_t priorityClass{0};
    uint32_t schedulingClass{0};
};

// ============================================================================
// 3. Executive Thread (ETHREAD)
// ============================================================================
class EThread {
public:
    EThread(Handle tid, EProcess* owner, uintptr_t entryPoint, uintptr_t stackBase, uintptr_t stackLimit)
        : tid_(tid), owner_(owner), stackBase_(stackBase), stackLimit_(stackLimit), state_(ThreadState::Initialized) {
        context_.rip = entryPoint;
        context_.rsp = stackBase - 0x28; // Standard 32-byte shadow space + return address align
        createTime_ = std::chrono::steady_clock::now();
    }

    [[nodiscard]] Handle getTid() const noexcept { return tid_; }
    [[nodiscard]] EProcess* getOwnerProcess() const noexcept { return owner_; }
    [[nodiscard]] ThreadState getState() const noexcept { return state_; }
    void setState(ThreadState s) noexcept { state_ = s; }

    [[nodiscard]] ContextFrame& getContext() noexcept { return context_; }
    [[nodiscard]] const ContextFrame& getContext() const noexcept { return context_; }

    [[nodiscard]] uintptr_t getStackBase() const noexcept { return stackBase_; }
    [[nodiscard]] uintptr_t getStackLimit() const noexcept { return stackLimit_; }
    [[nodiscard]] uintptr_t getTebAddress() const noexcept { return tebAddress_; }
    void setTebAddress(uintptr_t addr) noexcept { tebAddress_ = addr; }

    [[nodiscard]] const Teb& getTeb() const noexcept { return teb_; }
    [[nodiscard]] Teb& getTeb() noexcept { return teb_; }
    void setTeb(const Teb& teb) noexcept { teb_ = teb; }

    [[nodiscard]] uint32_t getPriority() const noexcept { return priority_; }
    void setPriority(uint32_t p) noexcept { priority_ = p; dynamicPriority_ = p; }

    [[nodiscard]] uint32_t getDynamicPriority() const noexcept { return dynamicPriority_; }
    void setDynamicPriority(uint32_t p) noexcept { dynamicPriority_ = p; }

    [[nodiscard]] uint32_t getBasePriority() const noexcept { return basePriority_; }
    void setBasePriority(uint32_t p) noexcept { basePriority_ = p; priority_ = p; dynamicPriority_ = p; }

    void boostPriority(uint32_t increment) noexcept {
        if (basePriority_ >= LOW_REALTIME_PRIORITY) {
            return; // Real-time priorities are fixed and non-boostable
        }
        uint32_t boosted = dynamicPriority_ + increment;
        if (boosted >= LOW_REALTIME_PRIORITY) {
            boosted = LOW_REALTIME_PRIORITY - 1; // Clamped at 15 for dynamic threads
        }
        if (boosted > dynamicPriority_) {
            dynamicPriority_ = boosted;
            priority_ = boosted;
        }
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalThreadPriorityBoosts++;
    }

    void decayQuantum() noexcept {
        if (quantum_ > 0) {
            quantum_--;
        }
        if (quantum_ == 0) {
            quantum_ = 18; // Standard NT thread quantum reset
            if (dynamicPriority_ > basePriority_ && basePriority_ < LOW_REALTIME_PRIORITY) {
                dynamicPriority_--;
                priority_ = dynamicPriority_;
            }
        }
    }

    [[nodiscard]] uint32_t getQuantum() const noexcept { return quantum_; }
    void setQuantum(uint32_t q) noexcept { quantum_ = q; }

    [[nodiscard]] uint64_t getAffinityMask() const noexcept { return affinityMask_; }
    void setAffinityMask(uint64_t mask) noexcept { affinityMask_ = mask; }

    [[nodiscard]] int32_t getSuspendCount() const noexcept { return suspendCount_; }

    uint32_t suspend() noexcept {
        uint32_t prev = static_cast<uint32_t>(suspendCount_++);
        if (prev == 0 && state_ != ThreadState::Terminated) {
            state_ = ThreadState::Waiting;
        }
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalThreadSuspensions++;
        return prev;
    }

    uint32_t resume() noexcept {
        if (suspendCount_ <= 0) return 0;
        uint32_t prev = static_cast<uint32_t>(suspendCount_--);
        if (suspendCount_ == 0 && state_ == ThreadState::Waiting) {
            state_ = ThreadState::Ready;
        }
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalThreadResumptions++;
        return prev;
    }

    [[nodiscard]] uintptr_t getKernelStackBase() const noexcept { return kernelStackBase_; }
    [[nodiscard]] uintptr_t getKernelStackLimit() const noexcept { return kernelStackLimit_; }
    void setKernelStack(uintptr_t base, uintptr_t limit) noexcept {
        kernelStackBase_ = base;
        kernelStackLimit_ = limit;
    }

    [[nodiscard]] NtStatus getExitStatus() const noexcept { return exitStatus_; }
    void setExitStatus(NtStatus status) noexcept { exitStatus_ = status; }

    // Thread Impersonation
    void impersonateToken(std::shared_ptr<se::TokenObject> token, SecurityImpersonationLevel level) noexcept {
        impersonationToken_ = std::move(token);
        impersonationLevel_ = level;
    }

    void revertToSelf() noexcept {
        impersonationToken_.reset();
        impersonationLevel_ = SecurityImpersonationLevel::SecurityAnonymous;
    }

    [[nodiscard]] bool isImpersonating() const noexcept {
        return impersonationToken_ != nullptr;
    }

    [[nodiscard]] std::shared_ptr<se::TokenObject> getActiveToken();

    [[nodiscard]] SecurityImpersonationLevel getImpersonationLevel() const noexcept {
        return impersonationLevel_;
    }

private:
    Handle tid_{0};
    EProcess* owner_{nullptr};
    uintptr_t stackBase_{0};
    uintptr_t stackLimit_{0};
    uintptr_t tebAddress_{0};
    uintptr_t kernelStackBase_{0};
    uintptr_t kernelStackLimit_{0};
    ThreadState state_{ThreadState::Initialized};
    ContextFrame context_{};
    uint32_t priority_{8};
    uint32_t dynamicPriority_{8};
    uint32_t basePriority_{8};
    uint32_t quantum_{18};
    uint64_t affinityMask_{0xFFFFFFFFFFFFFFFFULL};
    int32_t suspendCount_{0};
    NtStatus exitStatus_{NtStatus::Success};
    std::shared_ptr<se::TokenObject> impersonationToken_;
    SecurityImpersonationLevel impersonationLevel_{SecurityImpersonationLevel::SecurityAnonymous};
    Teb teb_{};
    std::chrono::steady_clock::time_point createTime_;
};

// ============================================================================
// 4. Executive Process (EPROCESS)
// ============================================================================
class EProcess {
public:
    EProcess(Handle pid, std::wstring imageFileName, Handle parentPid = 0)
        : pid_(pid), parentPid_(parentPid), imageFileName_(std::move(imageFileName)),
          directoryTableBase_(0x1000 + (static_cast<uint64_t>(pid) * 0x1000)),
          exitStatus_(NtStatus::Success), terminated_(false) {
        createTime_ = std::chrono::steady_clock::now();
    }

    [[nodiscard]] Handle getPid() const noexcept { return pid_; }
    [[nodiscard]] Handle getParentPid() const noexcept { return parentPid_; }
    void setParentPid(Handle parent) noexcept { parentPid_ = parent; }

    [[nodiscard]] const std::wstring& getImageFileName() const noexcept { return imageFileName_; }

    [[nodiscard]] mm::ProcessAddressSpace& getAddressSpace() noexcept { return addressSpace_; }
    [[nodiscard]] ob::HandleTable& getHandleTable() noexcept { return handleTable_; }

    [[nodiscard]] uintptr_t getDirectoryTableBase() const noexcept { return directoryTableBase_; }
    void setDirectoryTableBase(uintptr_t cr3) noexcept { directoryTableBase_ = cr3; }

    [[nodiscard]] uintptr_t getImageBase() const noexcept { return imageBase_; }
    void setImageBase(uintptr_t base) noexcept { imageBase_ = base; }

    [[nodiscard]] uintptr_t getEntryPoint() const noexcept { return entryPoint_; }
    void setEntryPoint(uintptr_t ep) noexcept { entryPoint_ = ep; }

    [[nodiscard]] uintptr_t getPebAddress() const noexcept { return pebAddress_; }
    void setPebAddress(uintptr_t addr) noexcept { pebAddress_ = addr; }

    [[nodiscard]] const Peb& getPeb() const noexcept { return peb_; }
    [[nodiscard]] Peb& getPeb() noexcept { return peb_; }
    void setPeb(const Peb& peb) noexcept { peb_ = peb; }

    [[nodiscard]] ProcessQuotaBlock& getQuotaBlock() noexcept { return quotaBlock_; }
    [[nodiscard]] const ProcessQuotaBlock& getQuotaBlock() const noexcept { return quotaBlock_; }
    void setQuotaBlock(const ProcessQuotaBlock& qb) noexcept { quotaBlock_ = qb; }

    [[nodiscard]] std::shared_ptr<JobObject> getJob() const noexcept;
    void setJob(std::shared_ptr<JobObject> job) noexcept;

    [[nodiscard]] uint64_t getAffinityMask() const noexcept { return affinityMask_; }
    void setAffinityMask(uint64_t mask) noexcept {
        affinityMask_ = mask;
        for (auto& t : threads_) {
            t->setAffinityMask(mask);
        }
    }

    void suspendProcess() noexcept {
        for (auto& t : threads_) {
            t->suspend();
        }
    }

    void resumeProcess() noexcept {
        for (auto& t : threads_) {
            t->resume();
        }
    }

    [[nodiscard]] bool isTerminated() const noexcept { return terminated_; }
    [[nodiscard]] NtStatus getExitStatus() const noexcept { return exitStatus_; }

    void terminate(NtStatus status) noexcept;

    std::shared_ptr<EThread> createThread(uintptr_t entryPoint, size_t stackSize = 1024 * 1024);

    [[nodiscard]] std::shared_ptr<se::TokenObject> getToken() const noexcept { return token_; }
    void setToken(std::shared_ptr<se::TokenObject> token) noexcept { token_ = std::move(token); }

    [[nodiscard]] const std::vector<std::shared_ptr<EThread>>& getThreads() const noexcept {
        return threads_;
    }

    [[nodiscard]] size_t getActiveThreadCount() const noexcept {
        size_t count = 0;
        for (const auto& t : threads_) {
            if (t->getState() != ThreadState::Terminated) ++count;
        }
        return count;
    }

private:
    Handle pid_{0};
    Handle parentPid_{0};
    std::wstring imageFileName_;
    uintptr_t directoryTableBase_{0x1000};
    mm::ProcessAddressSpace addressSpace_;
    ob::HandleTable handleTable_;
    std::shared_ptr<se::TokenObject> token_;
    uintptr_t imageBase_{0};
    uintptr_t entryPoint_{0};
    uintptr_t pebAddress_{0};
    Peb peb_{};
    ProcessQuotaBlock quotaBlock_{};
    std::weak_ptr<JobObject> job_;
    uint64_t affinityMask_{0xFFFFFFFFFFFFFFFFULL};
    NtStatus exitStatus_{NtStatus::Success};
    bool terminated_{false};
    std::vector<std::shared_ptr<EThread>> threads_;
    std::chrono::steady_clock::time_point createTime_;
};

inline std::shared_ptr<se::TokenObject> EThread::getActiveToken() {
    if (impersonationToken_) return impersonationToken_;
    if (owner_) return owner_->getToken();
    return nullptr;
}

// ============================================================================
// 5. Canonical Windows NT Job Object (EJOB)
// ============================================================================
class JobObject : public std::enable_shared_from_this<JobObject> {
public:
    explicit JobObject(std::wstring name = L"")
        : name_(std::move(name)) {
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalJobsCreated++;
    }

    [[nodiscard]] const std::wstring& getName() const noexcept { return name_; }

    void setLimits(const JobBasicLimitInformation& limits) noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        limits_ = limits;
    }

    [[nodiscard]] JobBasicLimitInformation getLimits() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return limits_;
    }

    [[nodiscard]] JobBasicAccountingInformation getAccounting() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return accounting_;
    }

    NtStatus assignProcess(std::shared_ptr<EProcess> process);

    NtStatus terminateJob(NtStatus exitStatus);

    [[nodiscard]] std::vector<std::shared_ptr<EProcess>> getAssignedProcesses() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return processes_;
    }

private:
    std::wstring name_;
    JobBasicLimitInformation limits_{};
    JobBasicAccountingInformation accounting_{};
    std::vector<std::shared_ptr<EProcess>> processes_;
    mutable std::recursive_mutex mutex_;
};

inline std::shared_ptr<JobObject> EProcess::getJob() const noexcept {
    return job_.lock();
}

inline void EProcess::setJob(std::shared_ptr<JobObject> job) noexcept {
    job_ = job;
}

// ============================================================================
// 6. Process & Thread Notification Callback Types
// ============================================================================
using PCREATE_PROCESS_NOTIFY_ROUTINE = void (*)(Handle parentId, Handle processId, bool create);
using PCREATE_THREAD_NOTIFY_ROUTINE = void (*)(Handle processId, Handle threadId, bool create);

struct PS_CREATE_NOTIFY_INFO {
    size_t size{sizeof(PS_CREATE_NOTIFY_INFO)};
    uint32_t flags{0};
    Handle parentProcessId{0};
    Handle creatingThreadId{0};
    std::wstring imageFileName;
    NtStatus creationStatus{NtStatus::Success};
};

using PCREATE_PROCESS_NOTIFY_ROUTINE_EX = void (*)(EProcess* process, Handle processId, PS_CREATE_NOTIFY_INFO* createInfo);

// ============================================================================
// 7. Process & Thread Manager Engine (Ps Subsystem Central Engine)
// ============================================================================
class ProcessManager {
public:
    static ProcessManager& get() {
        static ProcessManager instance;
        return instance;
    }

    std::shared_ptr<EProcess> createProcess(
        std::wstring_view imageName,
        std::shared_ptr<se::TokenObject> token = nullptr,
        Handle parentPid = 0
    ) {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        Handle pid = static_cast<Handle>(nextPid_++);

        // Check registered notify routines (Ex routines can inspect & veto creation)
        PS_CREATE_NOTIFY_INFO info{};
        info.size = sizeof(info);
        info.parentProcessId = parentPid;
        info.imageFileName = std::wstring(imageName);
        info.creationStatus = NtStatus::Success;

        for (auto* routineEx : createProcessNotifyRoutinesEx_) {
            if (routineEx) {
                routineEx(nullptr, pid, &info);
                {
                    std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
                    g_PsTelemetry.totalProcessCallbacksInvoked++;
                }
                if (!NT_SUCCESS(info.creationStatus)) {
                    return nullptr;
                }
            }
        }

        auto proc = std::make_shared<EProcess>(pid, std::wstring(imageName), parentPid);

        // Allocate and setup PEB at standard base
        uintptr_t pebAddr = 0x00007FFDF0000000ULL + (static_cast<uint64_t>(pid) * 0x10000);
        proc->getAddressSpace().allocate(pebAddr, sizeof(Peb), mm::MEM_COMMIT | mm::MEM_RESERVE, mm::PAGE_READWRITE);
        proc->setPebAddress(pebAddr);

        // Populate PEB defaults
        Peb userPeb{};
        userPeb.numberOfProcessors = 4;
        userPeb.osMajorVersion = 10;
        userPeb.osMinorVersion = 0;
        userPeb.osBuildNumber = 26100;
        userPeb.osPlatformId = 2;
        userPeb.imageSubsystem = 3;
        proc->setPeb(userPeb);

        if (token) {
            proc->setToken(std::move(token));
        } else {
            proc->setToken(se::TokenObject::createSystemToken());
        }

        processes_[pid] = proc;

        for (auto* routine : createProcessNotifyRoutines_) {
            if (routine) {
                routine(parentPid, pid, true);
                {
                    std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
                    g_PsTelemetry.totalProcessCallbacksInvoked++;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
            g_PsTelemetry.totalProcessesCreated++;
        }

        return proc;
    }

    void initializeSystemProcesses() {
        if (initializedSystem_) return;
        initializedSystem_ = true;

        // 1. PID 0: System Idle Process (Idle)
        idleProcess_ = std::make_shared<EProcess>(0, L"Idle");
        auto idleThread = idleProcess_->createThread(0);
        idleThread->setState(ThreadState::Running);
        idleThread->setBasePriority(0);
        processes_[0] = idleProcess_;

        // 2. PID 4: System Process (System / ntoskrnl.exe)
        systemProcess_ = std::make_shared<EProcess>(4, L"System");
        systemProcess_->setToken(se::TokenObject::createSystemToken());
        auto sysWorkerThread = systemProcess_->createThread(0);
        sysWorkerThread->setState(ThreadState::Ready);
        sysWorkerThread->setBasePriority(8);
        processes_[4] = systemProcess_;
    }

    [[nodiscard]] std::shared_ptr<EProcess> getIdleProcess() const noexcept {
        return idleProcess_;
    }

    [[nodiscard]] std::shared_ptr<EProcess> getSystemProcess() const noexcept {
        return systemProcess_;
    }

    [[nodiscard]] std::shared_ptr<EProcess> getProcess(Handle pid) const {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        auto it = processes_.find(pid);
        if (it != processes_.end()) return it->second;
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<EThread> getThread(Handle tid) const {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        for (const auto& [pid, proc] : processes_) {
            for (const auto& t : proc->getThreads()) {
                if (t->getTid() == tid) return t;
            }
        }
        return nullptr;
    }

    [[nodiscard]] size_t getActiveProcessCount() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        return processes_.size();
    }

    // Callbacks Management
    NtStatus registerProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE routine) {
        if (!routine) return NtStatus::InvalidParameter;
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        createProcessNotifyRoutines_.push_back(routine);
        return NtStatus::Success;
    }

    NtStatus unregisterProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE routine) {
        if (!routine) return NtStatus::InvalidParameter;
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        auto it = std::remove(createProcessNotifyRoutines_.begin(), createProcessNotifyRoutines_.end(), routine);
        if (it == createProcessNotifyRoutines_.end()) return NtStatus::ProcedureNotFound;
        createProcessNotifyRoutines_.erase(it, createProcessNotifyRoutines_.end());
        return NtStatus::Success;
    }

    NtStatus registerProcessNotifyRoutineEx(PCREATE_PROCESS_NOTIFY_ROUTINE_EX routine) {
        if (!routine) return NtStatus::InvalidParameter;
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        createProcessNotifyRoutinesEx_.push_back(routine);
        return NtStatus::Success;
    }

    NtStatus unregisterProcessNotifyRoutineEx(PCREATE_PROCESS_NOTIFY_ROUTINE_EX routine) {
        if (!routine) return NtStatus::InvalidParameter;
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        auto it = std::remove(createProcessNotifyRoutinesEx_.begin(), createProcessNotifyRoutinesEx_.end(), routine);
        if (it == createProcessNotifyRoutinesEx_.end()) return NtStatus::ProcedureNotFound;
        createProcessNotifyRoutinesEx_.erase(it, createProcessNotifyRoutinesEx_.end());
        return NtStatus::Success;
    }

    NtStatus registerThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine) {
        if (!routine) return NtStatus::InvalidParameter;
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        createThreadNotifyRoutines_.push_back(routine);
        return NtStatus::Success;
    }

    NtStatus unregisterThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine) {
        if (!routine) return NtStatus::InvalidParameter;
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        auto it = std::remove(createThreadNotifyRoutines_.begin(), createThreadNotifyRoutines_.end(), routine);
        if (it == createThreadNotifyRoutines_.end()) return NtStatus::ProcedureNotFound;
        createThreadNotifyRoutines_.erase(it, createThreadNotifyRoutines_.end());
        return NtStatus::Success;
    }

    void notifyProcessExit(Handle parentPid, Handle pid) {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        for (auto* routine : createProcessNotifyRoutines_) {
            if (routine) {
                routine(parentPid, pid, false);
                std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
                g_PsTelemetry.totalProcessCallbacksInvoked++;
            }
        }
    }

    void notifyThreadCreate(Handle pid, Handle tid) {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        for (auto* routine : createThreadNotifyRoutines_) {
            if (routine) {
                routine(pid, tid, true);
                std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
                g_PsTelemetry.totalThreadCallbacksInvoked++;
            }
        }
    }

    void notifyThreadExit(Handle pid, Handle tid) {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        for (auto* routine : createThreadNotifyRoutines_) {
            if (routine) {
                routine(pid, tid, false);
                std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
                g_PsTelemetry.totalThreadCallbacksInvoked++;
            }
        }
    }

    void resetForTesting() {
        std::lock_guard<std::recursive_mutex> lock(managerMutex_);
        processes_.clear();
        createProcessNotifyRoutines_.clear();
        createProcessNotifyRoutinesEx_.clear();
        createThreadNotifyRoutines_.clear();
        nextPid_ = 1000;
        initializedSystem_ = false;
        initializeSystemProcesses();
    }

private:
    ProcessManager() : nextPid_(1000) {
        initializeSystemProcesses();
    }

    mutable std::recursive_mutex managerMutex_;
    bool initializedSystem_{false};
    uint32_t nextPid_;
    std::shared_ptr<EProcess> idleProcess_;
    std::shared_ptr<EProcess> systemProcess_;
    std::unordered_map<Handle, std::shared_ptr<EProcess>> processes_;
    std::vector<PCREATE_PROCESS_NOTIFY_ROUTINE> createProcessNotifyRoutines_;
    std::vector<PCREATE_PROCESS_NOTIFY_ROUTINE_EX> createProcessNotifyRoutinesEx_;
    std::vector<PCREATE_THREAD_NOTIFY_ROUTINE> createThreadNotifyRoutines_;
};

// Out-of-line EProcess thread creation & termination implementations
inline std::shared_ptr<EThread> EProcess::createThread(uintptr_t entryPoint, size_t stackSize) {
    static std::atomic<uint32_t> s_GlobalTid{1};
    Handle tid = static_cast<Handle>(s_GlobalTid.fetch_add(1, std::memory_order_relaxed));

    // Allocate stack in process address space
    uintptr_t stackBase = 0;
    addressSpace_.allocate(stackBase, stackSize, mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
    uintptr_t stackTop = stackBase + stackSize;

    // Allocate and setup TEB in process address space
    uintptr_t tebAddr = 0;
    addressSpace_.allocate(tebAddr, sizeof(Teb), mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);

    auto thread = std::make_shared<EThread>(tid, this, entryPoint, stackTop, stackBase);
    thread->setTebAddress(tebAddr);
    thread->setAffinityMask(affinityMask_);

    // Populate TEB structure
    Teb userTeb{};
    userTeb.ntTib.self = tebAddr;
    userTeb.ntTib.stackBase = stackTop;
    userTeb.ntTib.stackLimit = stackBase;
    userTeb.clientId.uniqueProcess = pid_;
    userTeb.clientId.uniqueThread = tid;
    userTeb.processEnvironmentBlock = pebAddress_;
    thread->setTeb(userTeb);

    threads_.push_back(thread);

    if (pid_ >= 1000) {
        ProcessManager::get().notifyThreadCreate(pid_, tid);
    }
    {
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalThreadsCreated++;
    }

    return thread;
}

inline void EProcess::terminate(NtStatus status) noexcept {
    exitStatus_ = status;
    terminated_ = true;
    for (auto& t : threads_) {
        if (t->getState() != ThreadState::Terminated) {
            t->setState(ThreadState::Terminated);
            t->setExitStatus(status);
            if (pid_ >= 1000) {
                ProcessManager::get().notifyThreadExit(pid_, t->getTid());
            }
            std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
            g_PsTelemetry.totalThreadsTerminated++;
        }
    }
    if (pid_ >= 1000) {
        ProcessManager::get().notifyProcessExit(parentPid_, pid_);
    }
    {
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalProcessesTerminated++;
    }
}

// Out-of-line JobObject implementations
inline NtStatus JobObject::assignProcess(std::shared_ptr<EProcess> process) {
    if (!process) return NtStatus::InvalidParameter;
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if ((limits_.limitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) && limits_.activeProcessLimit > 0) {
        if (accounting_.activeProcesses >= limits_.activeProcessLimit) {
            std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
            g_PsTelemetry.totalQuotaViolations++;
            return NtStatus::QuotaExceeded;
        }
    }

    if (limits_.limitFlags & JOB_OBJECT_LIMIT_AFFINITY) {
        process->setAffinityMask(limits_.affinity);
    }

    processes_.push_back(process);
    process->setJob(shared_from_this());
    accounting_.totalProcesses++;
    accounting_.activeProcesses++;

    {
        std::lock_guard<std::mutex> lk(g_PsTelemetryMutex);
        g_PsTelemetry.totalProcessesAssignedToJobs++;
    }

    return NtStatus::Success;
}

inline NtStatus JobObject::terminateJob(NtStatus exitStatus) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& proc : processes_) {
        if (!proc->isTerminated()) {
            proc->terminate(exitStatus);
            if (accounting_.activeProcesses > 0) accounting_.activeProcesses--;
            accounting_.totalTerminatedProcesses++;
        }
    }
    return NtStatus::Success;
}

// ============================================================================
// 8. Standard NT Executive Process & Thread APIs (Ps*)
// ============================================================================
inline NtStatus PsLookupProcessByProcessId(Handle pid, std::shared_ptr<EProcess>& outProcess) {
    outProcess = ProcessManager::get().getProcess(pid);
    if (!outProcess) return NtStatus::NoSuchProcess;
    return NtStatus::Success;
}

inline NtStatus PsLookupThreadByThreadId(Handle tid, std::shared_ptr<EThread>& outThread) {
    outThread = ProcessManager::get().getThread(tid);
    if (!outThread) return NtStatus::InvalidParameter;
    return NtStatus::Success;
}

inline Handle PsGetProcessId(const EProcess* process) {
    return process ? process->getPid() : 0;
}

inline Handle PsGetThreadId(const EThread* thread) {
    return thread ? thread->getTid() : 0;
}

inline std::wstring_view PsGetProcessImageFileName(const EProcess* process) {
    if (process) {
        return process->getImageFileName();
    }
    return std::wstring_view{};
}

inline NtStatus PsSetCreateProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE routine, bool remove = false) {
    if (remove) {
        return ProcessManager::get().unregisterProcessNotifyRoutine(routine);
    }
    return ProcessManager::get().registerProcessNotifyRoutine(routine);
}

inline NtStatus PsSetCreateProcessNotifyRoutineEx(PCREATE_PROCESS_NOTIFY_ROUTINE_EX routine, bool remove = false) {
    if (remove) {
        return ProcessManager::get().unregisterProcessNotifyRoutineEx(routine);
    }
    return ProcessManager::get().registerProcessNotifyRoutineEx(routine);
}

inline NtStatus PsSetCreateThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine, bool remove = false) {
    if (remove) {
        return ProcessManager::get().unregisterThreadNotifyRoutine(routine);
    }
    return ProcessManager::get().registerThreadNotifyRoutine(routine);
}

inline uint32_t PsSuspendThread(EThread* thread) {
    return thread ? thread->suspend() : 0;
}

inline uint32_t PsResumeThread(EThread* thread) {
    return thread ? thread->resume() : 0;
}

inline NtStatus PsCreateProcessEx(
    std::shared_ptr<EProcess>& outProcess,
    std::wstring_view imageName,
    std::shared_ptr<se::TokenObject> token = nullptr,
    Handle parentPid = 0,
    bool createSuspended = false
) {
    auto proc = ProcessManager::get().createProcess(imageName, std::move(token), parentPid);
    if (!proc) {
        return NtStatus::AccessDenied;
    }
    if (createSuspended) {
        proc->suspendProcess();
    }
    outProcess = proc;
    return NtStatus::Success;
}

} // namespace micant::ps
#endif
