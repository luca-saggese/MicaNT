#include "micant/km/core/ntos_internal.hpp"
#include "micant/km/core/dispatcher.hpp"
#include "micant/km/core/sync.hpp"
#include "micant/km/core/timer.hpp"

namespace micant::sys {

void SyscallDispatcher::initializeStandardTable() {
    internal::HalInitializeExecutiveSubsystem();
    internal::ExInitializeExecutiveSubsystem();
    internal::PnpInitializeExecutiveSubsystem();
    internal::PoInitializeExecutiveSubsystem();
    internal::CcInitializeExecutiveCache();
    internal::FsRtlInitializeExecutiveSubsystem();
    internal::DbgkInitializeExecutiveSubsystem();
    internal::KtmInitializeExecutiveSubsystem();
    internal::VfInitializeExecutiveSubsystem();
    internal::ArbInitializeExecutiveSubsystem();
    internal::WmiInitializeExecutiveSubsystem();
    internal::PnpBootInitializeExecutiveSubsystem();
    internal::PnpResilienceInitializeExecutiveSubsystem();
    internal::PoFxWheaInitializeExecutiveSubsystem();
    internal::UmdfMsixInitializeExecutiveSubsystem();
    internal::ZeroBsodInitializeExecutiveSubsystem();
    internal::LiveHotSwapInitializeExecutiveSubsystem();
    internal::SysInfoTaskmgrInitializeExecutiveSubsystem();
    internal::QuotaGovInitializeExecutiveSubsystem();
    internal::MftZipUiInitializeExecutiveSubsystem();
    internal::SrmCiInitializeExecutiveSubsystem();
    internal::AfdWskInitializeExecutiveSubsystem();
    internal::KuserSealInitializeExecutiveSubsystem();
    internal::Ring1VfsInitializeExecutiveSubsystem();
    internal::Ring1DxgkInitializeExecutiveSubsystem();
    internal::Ring1AudioInitializeExecutiveSubsystem();
    internal::Ring1SmssInitializeExecutiveSubsystem();
    internal::Ring2WudfInitializeExecutiveSubsystem();
    internal::Ring2LsaInitializeExecutiveSubsystem();
    internal::Ring2ScmInitializeExecutiveSubsystem();
    internal::Ring3BaseInitializeExecutiveSubsystem();
    internal::Ring3ShellInitializeExecutiveSubsystem();
    internal::Ring3AppsInitializeExecutiveSubsystem();
    internal::Ring3PrismX3D12InitializeExecutiveSubsystem();
    internal::Ring3DirectStorageD2DInitializeExecutiveSubsystem();
    internal::Ring3PrismAudioMfInitializeExecutiveSubsystem();
    internal::Ring3WinsockWinHttpTlsInitializeExecutiveSubsystem();
    internal::Ring3ComRpcShellInitializeExecutiveSubsystem();
    internal::Ring3BcryptCrypt32WinTrustInitializeExecutiveSubsystem();
    internal::Ring3FiberNetAppsInitializeExecutiveSubsystem();
    internal::Ring3WorkstationAppsInitializeExecutiveSubsystem();
    internal::Ring3Wsl2HyperVClosureInitializeExecutiveSubsystem();
    internal::PrismXDirectXHeadersKmtInitializeExecutiveSubsystem();
    internal::PrismXDxMathTexMeshInitializeExecutiveSubsystem();
    internal::PrismXD3D12TranslationOn12InitializeExecutiveSubsystem();
    internal::PrismXDxcSm68PixInitializeExecutiveSubsystem();
    internal::PrismXGDeflateGpuInitializeExecutiveSubsystem();
    internal::PrismXDirectMLSuperResInitializeExecutiveSubsystem();
    internal::PrismXEnhancedBarriersWorkGraphsInitializeExecutiveSubsystem();
    internal::PrismXVulkanOn12VideoAv1InitializeExecutiveSubsystem();
    internal::PrismXDredShaderCacheZeroTdrInitializeExecutiveSubsystem();
    internal::PrismXLive3DSceneFrameGraphInitializeExecutiveSubsystem();
    internal::PrismXGopScanoutFramePacingInitializeExecutiveSubsystem();
    internal::PrismXGameBenchmarkProtonSealInitializeExecutiveSubsystem();
    internal::PrismXDirectSrVrsReBarGamingInitializeExecutiveSubsystem();
    internal::PrismXExecuteIndirectSamplerFeedbackInitializeExecutiveSubsystem();
    internal::PrismXGameInputPixClassicGamingSealInitializeExecutiveSubsystem();
    internal::PrismXHlslFxcDxbcReflectionCompilerInitializeExecutiveSubsystem();
    internal::PrismXDxilSm69Wave32SimdVmInitializeExecutiveSubsystem();
    internal::PrismXSpirv16DxilCrossTranslatorSealInitializeExecutiveSubsystem();
    internal::Ring0AntiCheatEacBattlEyeCallbacksInitializeExecutiveSubsystem();
    internal::Ring0VanguardRicochetHvciIommuShieldInitializeExecutiveSubsystem();
    internal::Horizon100GamingDrmHwidVaultSealInitializeExecutiveSubsystem();
    internal::PrismXDxgi16ComHierarchyInitializeExecutiveSubsystem();
    internal::PrismXD3D12VTableOptions21InitializeExecutiveSubsystem();
    internal::PrismXDirectXSystem32DllRouterInitializeExecutiveSubsystem();
    internal::PrismXD3D9ExComVTableAbiInitializeExecutiveSubsystem();
    internal::PrismXXAudio2AndDirectInput8ComAbiInitializeExecutiveSubsystem();
    internal::PrismXD2D1DWriteDCompElevenDllSealInitializeExecutiveSubsystem();
    internal::PrismXMediaFoundationCutsceneAbiInitializeExecutiveSubsystem();
    internal::PrismXDXCoreComAbiInitializeExecutiveSubsystem();
    internal::PrismXVulkan1OpenGL32SixteenDllSealInitializeExecutiveSubsystem();

    // 1. NtAllocateVirtualMemory (SSN: 0x0018)
    registerSyscall(SSN_NtAllocateVirtualMemory, "NtAllocateVirtualMemory", 6, [](const SyscallFrame& f) -> NtStatus {
        Handle proc = static_cast<Handle>(f.arg1);
        auto* baseAddr = reinterpret_cast<uintptr_t*>(f.arg2);
        uintptr_t zeroBits = f.arg3;
        auto* regSize = reinterpret_cast<size_t*>(f.arg4);

        uint32_t allocType = 0;
        uint32_t protect = 0;
        if (f.stackArgs && f.stackArgCount >= 2) {
            allocType = static_cast<uint32_t>(f.stackArgs[0]);
            protect = static_cast<uint32_t>(f.stackArgs[1]);
        }

        return NtAllocateVirtualMemory(proc, baseAddr, zeroBits, regSize, allocType, protect);
    });

    // 2. NtFreeVirtualMemory (SSN: 0x001E)
    registerSyscall(SSN_NtFreeVirtualMemory, "NtFreeVirtualMemory", 4, [](const SyscallFrame& f) -> NtStatus {
        Handle proc = static_cast<Handle>(f.arg1);
        auto* baseAddr = reinterpret_cast<uintptr_t*>(f.arg2);
        auto* regSize = reinterpret_cast<size_t*>(f.arg3);
        uint32_t freeType = static_cast<uint32_t>(f.arg4);
        return NtFreeVirtualMemory(proc, baseAddr, regSize, freeType);
    });

    // 3. NtClose (SSN: 0x000F)
    registerSyscall(SSN_NtClose, "NtClose", 1, [](const SyscallFrame& f) -> NtStatus {
        return NtClose(static_cast<Handle>(f.arg1));
    });

    // 4. NtTerminateProcess (SSN: 0x002C)
    registerSyscall(SSN_NtTerminateProcess, "NtTerminateProcess", 2, [](const SyscallFrame& f) -> NtStatus {
        Handle proc = static_cast<Handle>(f.arg1);
        NtStatus exitStatus = static_cast<NtStatus>(f.arg2);
        return NtTerminateProcess(proc, exitStatus);
    });

    // 5. NtWaitForSingleObject (SSN: 0x0004)
    registerSyscall(SSN_NtWaitForSingleObject, "NtWaitForSingleObject", 3, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        bool alertable = f.arg2 != 0;
        auto* timeout = reinterpret_cast<LargeInteger*>(f.arg3);
        return NtWaitForSingleObject(h, alertable, timeout);
    });

    // 6. NtQuerySystemInformation (SSN: 0x0036)
    registerSyscall(SSN_NtQuerySystemInformation, "NtQuerySystemInformation", 4, [](const SyscallFrame& f) -> NtStatus {
        uint32_t infoClass = static_cast<uint32_t>(f.arg1);
        void* info = reinterpret_cast<void*>(f.arg2);
        uint32_t infoLen = static_cast<uint32_t>(f.arg3);
        auto* retLen = reinterpret_cast<uint32_t*>(f.arg4);
        return NtQuerySystemInformation(infoClass, info, infoLen, retLen);
    });

    // 7. NtCreateEvent (SSN: 0x0048)
    registerSyscall(SSN_NtCreateEvent, "NtCreateEvent", 5, [](const SyscallFrame& f) -> NtStatus {
        auto* outHandle = reinterpret_cast<Handle*>(f.arg1);
        if (!outHandle) return NtStatus::InvalidParameter;

        sync::EventType evType = (f.arg3 == 0) ? sync::EventType::NotificationEvent : sync::EventType::SynchronizationEvent;
        bool initialState = (f.arg4 != 0);

        auto ev = std::make_shared<sync::EventObject>(evType, initialState);
        Handle h = sync::DispatcherRegistry::get().registerObject(ev);
        *outHandle = h;
        return NtStatus::Success;
    });

    // 8. NtSetEvent (SSN: 0x004E)
    registerSyscall(SSN_NtSetEvent, "NtSetEvent", 2, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto ev = sync::DispatcherRegistry::get().lookupAs<sync::EventObject>(h);
        if (!ev) return NtStatus::InvalidHandle;
        ev->set();
        return NtStatus::Success;
    });

    // 9. NtResetEvent (SSN: 0x004F)
    registerSyscall(SSN_NtResetEvent, "NtResetEvent", 2, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto ev = sync::DispatcherRegistry::get().lookupAs<sync::EventObject>(h);
        if (!ev) return NtStatus::InvalidHandle;
        ev->reset();
        return NtStatus::Success;
    });

    // 10. NtCreateMutant (SSN: 0x00B2)
    registerSyscall(SSN_NtCreateMutant, "NtCreateMutant", 4, [](const SyscallFrame& f) -> NtStatus {
        auto* outHandle = reinterpret_cast<Handle*>(f.arg1);
        if (!outHandle) return NtStatus::InvalidParameter;

        bool initialOwner = (f.arg3 != 0);
        auto mut = std::make_shared<sync::MutantObject>(initialOwner);
        Handle h = sync::DispatcherRegistry::get().registerObject(mut);
        *outHandle = h;
        return NtStatus::Success;
    });

    // 11. NtReleaseMutant (SSN: 0x001D)
    registerSyscall(SSN_NtReleaseMutant, "NtReleaseMutant", 2, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto mut = sync::DispatcherRegistry::get().lookupAs<sync::MutantObject>(h);
        if (!mut) return NtStatus::InvalidHandle;
        bool ok = mut->release(1);
        return ok ? NtStatus::Success : NtStatus::Unsuccessful;
    });

    // 12-14. I/O Completion Port System Services (kernel/io/iomgr.cpp)
    registerSyscall(SSN_NtCreateIoCompletion, "NtCreateIoCompletion", 4, internal::NtCreateIoCompletionSys);
    registerSyscall(SSN_NtSetIoCompletion, "NtSetIoCompletion", 5, internal::NtSetIoCompletionSys);
    registerSyscall(SSN_NtRemoveIoCompletion, "NtRemoveIoCompletion", 5, internal::NtRemoveIoCompletionSys);

    // 15-17. Configuration Manager Registry System Services (kernel/cm/ntreg.cpp)
    registerSyscall(SSN_NtOpenKey, "NtOpenKey", 3, internal::NtOpenKeySys);
    registerSyscall(SSN_NtQueryValueKey, "NtQueryValueKey", 6, internal::NtQueryValueKeySys);
    registerSyscall(SSN_NtSetValueKey, "NtSetValueKey", 6, internal::NtSetValueKeySys);

    // 18-19. Security Reference Monitor System Services (kernel/se/ntsec.cpp)
    registerSyscall(SSN_NtOpenProcessToken, "NtOpenProcessToken", 3, internal::NtOpenProcessTokenSys);
    registerSyscall(SSN_NtAccessCheck, "NtAccessCheck", 5, internal::NtAccessCheckSys);

    // The hosted LPC adapter is not linked into the freestanding Executive;
    // until its Ke wait and Ob ownership path is native, leave these syscalls
    // unregistered so the canonical dispatcher returns NotImplemented.
#if !defined(MICANT_FREESTANDING)
    registerSyscall(SSN_NtCreatePort, "NtCreatePort", 4, internal::NtCreatePortSys);
    registerSyscall(SSN_NtConnectPort, "NtConnectPort", 4, internal::NtConnectPortSys);
    registerSyscall(SSN_NtRequestWaitReplyPort, "NtRequestWaitReplyPort", 3, internal::NtRequestWaitReplyPortSys);
#endif

    // 23. NtCreateFile (SSN: 0x0055)
    registerSyscall(SSN_NtCreateFile, "NtCreateFile", 11, [](const SyscallFrame& f) -> NtStatus {
        auto* fileHandle = reinterpret_cast<Handle*>(f.arg1);
        uint32_t desiredAccess = static_cast<uint32_t>(f.arg2);
        auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
        auto* iosb = reinterpret_cast<IoStatusBlock*>(f.arg4);

        LargeInteger* allocSize = nullptr;
        uint32_t fileAttr = 0;
        uint32_t share = 0;
        uint32_t disposition = 1; // FILE_OPEN
        uint32_t options = 0;
        void* eaBuf = nullptr;
        uint32_t eaLen = 0;

        if (f.stackArgs && f.stackArgCount >= 4) {
            allocSize = reinterpret_cast<LargeInteger*>(f.stackArgs[0]);
            fileAttr = static_cast<uint32_t>(f.stackArgs[1]);
            share = static_cast<uint32_t>(f.stackArgs[2]);
            disposition = static_cast<uint32_t>(f.stackArgs[3]);
            if (f.stackArgCount >= 5) options = static_cast<uint32_t>(f.stackArgs[4]);
            if (f.stackArgCount >= 6) eaBuf = reinterpret_cast<void*>(f.stackArgs[5]);
            if (f.stackArgCount >= 7) eaLen = static_cast<uint32_t>(f.stackArgs[6]);
        }

        return NtCreateFile(fileHandle, desiredAccess, objAttr, iosb, allocSize, fileAttr, share, disposition, options, eaBuf, eaLen);
    });

    // 24. NtOpenFile (SSN: 0x0033)
    registerSyscall(SSN_NtOpenFile, "NtOpenFile", 6, [](const SyscallFrame& f) -> NtStatus {
        auto* fileHandle = reinterpret_cast<Handle*>(f.arg1);
        uint32_t desiredAccess = static_cast<uint32_t>(f.arg2);
        auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
        auto* iosb = reinterpret_cast<IoStatusBlock*>(f.arg4);

        uint32_t share = 0;
        uint32_t options = 0;
        if (f.stackArgs && f.stackArgCount >= 2) {
            share = static_cast<uint32_t>(f.stackArgs[0]);
            options = static_cast<uint32_t>(f.stackArgs[1]);
        }

        return NtOpenFile(fileHandle, desiredAccess, objAttr, iosb, share, options);
    });

    // 25. NtReadFile (SSN: 0x0006)
    registerSyscall(SSN_NtReadFile, "NtReadFile", 9, [](const SyscallFrame& f) -> NtStatus {
        Handle fileHandle = static_cast<Handle>(f.arg1);
        Handle event = static_cast<Handle>(f.arg2);
        void* apcRoutine = reinterpret_cast<void*>(f.arg3);
        void* apcContext = reinterpret_cast<void*>(f.arg4);

        IoStatusBlock* iosb = nullptr;
        void* buffer = nullptr;
        uint32_t length = 0;
        LargeInteger* byteOffset = nullptr;
        uint32_t* key = nullptr;

        if (f.stackArgs && f.stackArgCount >= 3) {
            iosb = reinterpret_cast<IoStatusBlock*>(f.stackArgs[0]);
            buffer = reinterpret_cast<void*>(f.stackArgs[1]);
            length = static_cast<uint32_t>(f.stackArgs[2]);
            if (f.stackArgCount >= 4) byteOffset = reinterpret_cast<LargeInteger*>(f.stackArgs[3]);
            if (f.stackArgCount >= 5) key = reinterpret_cast<uint32_t*>(f.stackArgs[4]);
        }

        return NtReadFile(fileHandle, event, apcRoutine, apcContext, iosb, buffer, length, byteOffset, key);
    });

    // 26. NtWriteFile (SSN: 0x0008)
    registerSyscall(SSN_NtWriteFile, "NtWriteFile", 9, [](const SyscallFrame& f) -> NtStatus {
        Handle fileHandle = static_cast<Handle>(f.arg1);
        Handle event = static_cast<Handle>(f.arg2);
        void* apcRoutine = reinterpret_cast<void*>(f.arg3);
        void* apcContext = reinterpret_cast<void*>(f.arg4);

        IoStatusBlock* iosb = nullptr;
        const void* buffer = nullptr;
        uint32_t length = 0;
        LargeInteger* byteOffset = nullptr;
        uint32_t* key = nullptr;

        if (f.stackArgs && f.stackArgCount >= 3) {
            iosb = reinterpret_cast<IoStatusBlock*>(f.stackArgs[0]);
            buffer = reinterpret_cast<const void*>(f.stackArgs[1]);
            length = static_cast<uint32_t>(f.stackArgs[2]);
            if (f.stackArgCount >= 4) byteOffset = reinterpret_cast<LargeInteger*>(f.stackArgs[3]);
            if (f.stackArgCount >= 5) key = reinterpret_cast<uint32_t*>(f.stackArgs[4]);
        }

        return NtWriteFile(fileHandle, event, apcRoutine, apcContext, iosb, buffer, length, byteOffset, key);
    });

    registerSyscall(SSN_NtFlushBuffersFile, "NtFlushBuffersFile", 2,
        [](const SyscallFrame& f) -> NtStatus {
            return NtFlushBuffersFile(static_cast<Handle>(f.arg1),
                reinterpret_cast<IoStatusBlock*>(f.arg2));
        });

    // 27. NtDeviceIoControlFile (SSN: 0x0007)
    registerSyscall(SSN_NtDeviceIoControlFile, "NtDeviceIoControlFile", 10, [](const SyscallFrame& f) -> NtStatus {
        Handle fileHandle = static_cast<Handle>(f.arg1);
        Handle event = static_cast<Handle>(f.arg2);
        void* apcRoutine = reinterpret_cast<void*>(f.arg3);
        void* apcContext = reinterpret_cast<void*>(f.arg4);

        IoStatusBlock* iosb = nullptr;
        uint32_t ioControlCode = 0;
        const void* inBuf = nullptr;
        uint32_t inLen = 0;
        void* outBuf = nullptr;
        uint32_t outLen = 0;

        if (f.stackArgs && f.stackArgCount >= 2) {
            iosb = reinterpret_cast<IoStatusBlock*>(f.stackArgs[0]);
            ioControlCode = static_cast<uint32_t>(f.stackArgs[1]);
            if (f.stackArgCount >= 3) inBuf = reinterpret_cast<const void*>(f.stackArgs[2]);
            if (f.stackArgCount >= 4) inLen = static_cast<uint32_t>(f.stackArgs[3]);
            if (f.stackArgCount >= 5) outBuf = reinterpret_cast<void*>(f.stackArgs[4]);
            if (f.stackArgCount >= 6) outLen = static_cast<uint32_t>(f.stackArgs[5]);
        }

        return NtDeviceIoControlFile(fileHandle, event, apcRoutine, apcContext, iosb, ioControlCode, inBuf, inLen, outBuf, outLen);
    });

    // 28. NtWaitForMultipleObjects (SSN: 0x005A)
    registerSyscall(SSN_NtWaitForMultipleObjects, "NtWaitForMultipleObjects", 5, [](const SyscallFrame& f) -> NtStatus {
        uint32_t count = static_cast<uint32_t>(f.arg1);
        auto* handles = reinterpret_cast<const Handle*>(f.arg2);
        WaitType waitType = static_cast<WaitType>(f.arg3);
        bool alertable = f.arg4 != 0;
        LargeInteger* timeout = nullptr;
        if (f.stackArgs && f.stackArgCount >= 1) {
            timeout = reinterpret_cast<LargeInteger*>(f.stackArgs[0]);
        }
        return NtWaitForMultipleObjects(count, handles, waitType, alertable, timeout);
    });

    // 29. NtDelayExecution (SSN: 0x0034)
    registerSyscall(SSN_NtDelayExecution, "NtDelayExecution", 2, [](const SyscallFrame& f) -> NtStatus {
        bool alertable = f.arg1 != 0;
        auto* interval = reinterpret_cast<const LargeInteger*>(f.arg2);
        return NtDelayExecution(alertable, interval);
    });

    // 30. NtCreateTimer (SSN: 0x0057)
    registerSyscall(SSN_NtCreateTimer, "NtCreateTimer", 4, [](const SyscallFrame& f) -> NtStatus {
        auto* outHandle = reinterpret_cast<Handle*>(f.arg1);
        uint32_t desiredAccess = static_cast<uint32_t>(f.arg2);
        auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
        uint32_t timerType = static_cast<uint32_t>(f.arg4);
        return NtCreateTimer(outHandle, desiredAccess, objAttr, timerType);
    });

    // 31. NtSetTimer (SSN: 0x0078)
    registerSyscall(SSN_NtSetTimer, "NtSetTimer", 7, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto* dueTime = reinterpret_cast<LargeInteger*>(f.arg2);
        void* apcRoutine = reinterpret_cast<void*>(f.arg3);
        void* apcContext = reinterpret_cast<void*>(f.arg4);
        bool resumeTimer = false;
        uint32_t period = 0;
        bool* previousState = nullptr;
        if (f.stackArgs && f.stackArgCount >= 1) resumeTimer = f.stackArgs[0] != 0;
        if (f.stackArgs && f.stackArgCount >= 2) period = static_cast<uint32_t>(f.stackArgs[1]);
        if (f.stackArgs && f.stackArgCount >= 3) previousState = reinterpret_cast<bool*>(f.stackArgs[2]);
        return NtSetTimer(h, dueTime, apcRoutine, apcContext, resumeTimer, period, previousState);
    });

    // 32. NtCancelTimer (SSN: 0x0077)
    registerSyscall(SSN_NtCancelTimer, "NtCancelTimer", 2, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto* currentSignaledState = reinterpret_cast<bool*>(f.arg2);
        return NtCancelTimer(h, currentSignaledState);
    });

    // 33. NtShutdownSystem (SSN: 0x0118)
    registerSyscall(SSN_NtShutdownSystem, "NtShutdownSystem", 1, [](const SyscallFrame& f) -> NtStatus {
        uint32_t action = static_cast<uint32_t>(f.arg1);
        return NtShutdownSystem(action);
    });

    // 34. NtCreateSection (SSN: 0x004A)
    registerSyscall(SSN_NtCreateSection, "NtCreateSection", 7, [](const SyscallFrame& f) -> NtStatus {
        auto* sectionHandle = reinterpret_cast<Handle*>(f.arg1);
        uint32_t desiredAccess = static_cast<uint32_t>(f.arg2);
        auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
        auto* maximumSize = reinterpret_cast<LargeInteger*>(f.arg4);
        uint32_t prot = 0, allocAttr = 0;
        Handle fileHandle = 0;
        if (f.stackArgs && f.stackArgCount >= 1) prot = static_cast<uint32_t>(f.stackArgs[0]);
        if (f.stackArgs && f.stackArgCount >= 2) allocAttr = static_cast<uint32_t>(f.stackArgs[1]);
        if (f.stackArgs && f.stackArgCount >= 3) fileHandle = static_cast<Handle>(f.stackArgs[2]);
        return NtCreateSection(sectionHandle, desiredAccess, objAttr, maximumSize, prot, allocAttr, fileHandle);
    });

    // 35. NtMapViewOfSection (SSN: 0x0028)
    registerSyscall(SSN_NtMapViewOfSection, "NtMapViewOfSection", 10, [](const SyscallFrame& f) -> NtStatus {
        Handle sec = static_cast<Handle>(f.arg1);
        Handle proc = static_cast<Handle>(f.arg2);
        auto* baseAddr = reinterpret_cast<uintptr_t*>(f.arg3);
        uintptr_t zeroBits = f.arg4;
        size_t commitSize = 0;
        LargeInteger* secOffset = nullptr;
        size_t* viewSize = nullptr;
        uint32_t inheritDisp = 0, allocType = 0, win32Protect = 0;
        if (f.stackArgs && f.stackArgCount >= 1) commitSize = static_cast<size_t>(f.stackArgs[0]);
        if (f.stackArgs && f.stackArgCount >= 2) secOffset = reinterpret_cast<LargeInteger*>(f.stackArgs[1]);
        if (f.stackArgs && f.stackArgCount >= 3) viewSize = reinterpret_cast<size_t*>(f.stackArgs[2]);
        if (f.stackArgs && f.stackArgCount >= 4) inheritDisp = static_cast<uint32_t>(f.stackArgs[3]);
        if (f.stackArgs && f.stackArgCount >= 5) allocType = static_cast<uint32_t>(f.stackArgs[4]);
        if (f.stackArgs && f.stackArgCount >= 6) win32Protect = static_cast<uint32_t>(f.stackArgs[5]);
        return NtMapViewOfSection(sec, proc, baseAddr, zeroBits, commitSize, secOffset, viewSize, inheritDisp, allocType, win32Protect);
    });

    // 36. NtUnmapViewOfSection (SSN: 0x002A)
    registerSyscall(SSN_NtUnmapViewOfSection, "NtUnmapViewOfSection", 2, [](const SyscallFrame& f) -> NtStatus {
        Handle proc = static_cast<Handle>(f.arg1);
        uintptr_t baseAddr = f.arg2;
        return NtUnmapViewOfSection(proc, baseAddr);
    });

    // 37. NtQueryInformationFile (SSN: 0x0011)
    registerSyscall(SSN_NtQueryInformationFile, "NtQueryInformationFile", 5, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto* iosb = reinterpret_cast<IoStatusBlock*>(f.arg2);
        void* info = reinterpret_cast<void*>(f.arg3);
        uint32_t len = static_cast<uint32_t>(f.arg4);
        FileInformationClass infoClass = FileInformationClass::FileStandardInformation;
        if (f.stackArgs && f.stackArgCount >= 1) infoClass = static_cast<FileInformationClass>(f.stackArgs[0]);
        return NtQueryInformationFile(h, iosb, info, len, infoClass);
    });

    // 38. NtSetInformationFile (SSN: 0x0027)
    registerSyscall(SSN_NtSetInformationFile, "NtSetInformationFile", 5, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        auto* iosb = reinterpret_cast<IoStatusBlock*>(f.arg2);
        const void* info = reinterpret_cast<const void*>(f.arg3);
        uint32_t len = static_cast<uint32_t>(f.arg4);
        FileInformationClass infoClass = FileInformationClass::FilePositionInformation;
        if (f.stackArgs && f.stackArgCount >= 1) infoClass = static_cast<FileInformationClass>(f.stackArgs[0]);
        return NtSetInformationFile(h, iosb, info, len, infoClass);
    });

    // 39. NtQueryDirectoryFile (SSN: 0x0035)
    registerSyscall(SSN_NtQueryDirectoryFile, "NtQueryDirectoryFile", 11, [](const SyscallFrame& f) -> NtStatus {
        Handle h = static_cast<Handle>(f.arg1);
        Handle ev = static_cast<Handle>(f.arg2);
        void* apc = reinterpret_cast<void*>(f.arg3);
        void* apcCtxt = reinterpret_cast<void*>(f.arg4);
        IoStatusBlock* iosb = nullptr;
        void* info = nullptr;
        uint32_t len = 0;
        FileInformationClass infoClass = FileInformationClass::FileBothDirectoryInformation;
        bool single = false, restart = false;
        UnicodeString* fn = nullptr;
        if (f.stackArgs && f.stackArgCount >= 1) iosb = reinterpret_cast<IoStatusBlock*>(f.stackArgs[0]);
        if (f.stackArgs && f.stackArgCount >= 2) info = reinterpret_cast<void*>(f.stackArgs[1]);
        if (f.stackArgs && f.stackArgCount >= 3) len = static_cast<uint32_t>(f.stackArgs[2]);
        if (f.stackArgs && f.stackArgCount >= 4) infoClass = static_cast<FileInformationClass>(f.stackArgs[3]);
        if (f.stackArgs && f.stackArgCount >= 5) single = f.stackArgs[4] != 0;
        if (f.stackArgs && f.stackArgCount >= 6) fn = reinterpret_cast<UnicodeString*>(f.stackArgs[5]);
        if (f.stackArgs && f.stackArgCount >= 7) restart = f.stackArgs[6] != 0;
        return NtQueryDirectoryFile(h, ev, apc, apcCtxt, iosb, info, len, infoClass, single, fn, restart);
    });

    // 40. NtQueryPerformanceCounter (SSN: 0x0031)
    registerSyscall(SSN_NtQueryPerformanceCounter, "NtQueryPerformanceCounter", 2, [](const SyscallFrame& f) -> NtStatus {
        auto* counter = reinterpret_cast<LargeInteger*>(f.arg1);
        auto* freq = reinterpret_cast<LargeInteger*>(f.arg2);
        return NtQueryPerformanceCounter(counter, freq);
    });

    registerSyscall(SSN_NtQuerySystemTime, "NtQuerySystemTime", 1,
        [](const SyscallFrame& f) -> NtStatus {
            return NtQuerySystemTime(reinterpret_cast<LargeInteger*>(f.arg1));
        });

    // 41. NtYieldExecution (SSN: 0x0046)
    registerSyscall(SSN_NtYieldExecution, "NtYieldExecution", 0, [](const SyscallFrame& /*f*/) -> NtStatus {
        return NtYieldExecution();
    });

    // 42. NtQueryInformationProcess (SSN: 0x0019)
    registerSyscall(SSN_NtQueryInformationProcess, "NtQueryInformationProcess", 5, [](const SyscallFrame& f) -> NtStatus {
        Handle proc = static_cast<Handle>(f.arg1);
        ProcessInformationClass pClass = static_cast<ProcessInformationClass>(f.arg2);
        void* info = reinterpret_cast<void*>(f.arg3);
        uint32_t len = static_cast<uint32_t>(f.arg4);
        uint32_t* retLen = nullptr;
        if (f.stackArgs && f.stackArgCount >= 1) retLen = reinterpret_cast<uint32_t*>(f.stackArgs[0]);
        return NtQueryInformationProcess(proc, pClass, info, len, retLen);
    });

    registerSyscall(SSN_NtQueryInformationThread, "NtQueryInformationThread", 5,
        [](const SyscallFrame& f) -> NtStatus {
            auto* returnLength = f.stackArgs && f.stackArgCount >= 1
                ? reinterpret_cast<uint32_t*>(f.stackArgs[0]) : nullptr;
            return NtQueryInformationThread(static_cast<Handle>(f.arg1),
                static_cast<ThreadInformationClass>(f.arg2), reinterpret_cast<void*>(f.arg3),
                static_cast<uint32_t>(f.arg4), returnLength);
        });
    registerSyscall(SSN_NtAlertThread, "NtAlertThread", 1,
        [](const SyscallFrame& f) -> NtStatus {
            return NtAlertThread(static_cast<Handle>(f.arg1));
        });
    registerSyscall(SSN_NtTestAlert, "NtTestAlert", 0,
        [](const SyscallFrame&) -> NtStatus { return NtTestAlert(); });

    // 43. NtCreateNamedPipeFile (SSN: 0x0091)
    registerSyscall(SSN_NtCreateNamedPipeFile, "NtCreateNamedPipeFile", 14, [](const SyscallFrame& f) -> NtStatus {
        auto* fileHandle = reinterpret_cast<Handle*>(f.arg1);
        uint32_t desiredAccess = static_cast<uint32_t>(f.arg2);
        auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
        auto* iosb = reinterpret_cast<IoStatusBlock*>(f.arg4);
        uint32_t shareAccess = 0, createDisposition = 0, createOptions = 0;
        uint32_t namedPipeType = 0, readMode = 0, completionMode = 0, maxInstances = 0;
        uint32_t inQuota = 0, outQuota = 0;
        LargeInteger* defTimeout = nullptr;

        if (f.stackArgs && f.stackArgCount >= 1) shareAccess = static_cast<uint32_t>(f.stackArgs[0]);
        if (f.stackArgs && f.stackArgCount >= 2) createDisposition = static_cast<uint32_t>(f.stackArgs[1]);
        if (f.stackArgs && f.stackArgCount >= 3) createOptions = static_cast<uint32_t>(f.stackArgs[2]);
        if (f.stackArgs && f.stackArgCount >= 4) namedPipeType = static_cast<uint32_t>(f.stackArgs[3]);
        if (f.stackArgs && f.stackArgCount >= 5) readMode = static_cast<uint32_t>(f.stackArgs[4]);
        if (f.stackArgs && f.stackArgCount >= 6) completionMode = static_cast<uint32_t>(f.stackArgs[5]);
        if (f.stackArgs && f.stackArgCount >= 7) maxInstances = static_cast<uint32_t>(f.stackArgs[6]);
        if (f.stackArgs && f.stackArgCount >= 8) inQuota = static_cast<uint32_t>(f.stackArgs[7]);
        if (f.stackArgs && f.stackArgCount >= 9) outQuota = static_cast<uint32_t>(f.stackArgs[8]);
        if (f.stackArgs && f.stackArgCount >= 10) defTimeout = reinterpret_cast<LargeInteger*>(f.stackArgs[9]);

        return NtCreateNamedPipeFile(
            fileHandle, desiredAccess, objAttr, iosb,
            shareAccess, createDisposition, createOptions,
            namedPipeType, readMode, completionMode,
            maxInstances, inQuota, outQuota, defTimeout
        );
    });

    // 44. NtCreateMailslotFile (SSN: 0x0092)
    registerSyscall(SSN_NtCreateMailslotFile, "NtCreateMailslotFile", 8, [](const SyscallFrame& f) -> NtStatus {
        auto* fileHandle = reinterpret_cast<Handle*>(f.arg1);
        uint32_t desiredAccess = static_cast<uint32_t>(f.arg2);
        auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
        auto* iosb = reinterpret_cast<IoStatusBlock*>(f.arg4);
        uint32_t createOptions = 0, mailslotQuota = 0, maxMessageSize = 0;
        LargeInteger* readTimeout = nullptr;

        if (f.stackArgs && f.stackArgCount >= 1) createOptions = static_cast<uint32_t>(f.stackArgs[0]);
        if (f.stackArgs && f.stackArgCount >= 2) mailslotQuota = static_cast<uint32_t>(f.stackArgs[1]);
        if (f.stackArgs && f.stackArgCount >= 3) maxMessageSize = static_cast<uint32_t>(f.stackArgs[2]);
        if (f.stackArgs && f.stackArgCount >= 4) readTimeout = reinterpret_cast<LargeInteger*>(f.stackArgs[3]);

        return NtCreateMailslotFile(
            fileHandle, desiredAccess, objAttr, iosb,
            createOptions, mailslotQuota, maxMessageSize, readTimeout
        );
    });

    setShadowDispatcher(DispatchWin32kShadowSyscall);
}

} // namespace micant::sys
