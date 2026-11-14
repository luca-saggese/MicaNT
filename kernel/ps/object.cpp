#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/core/syscalls.hpp"
#include <new>
#include <cstring>

namespace micant::ps::detail {
namespace {
ob::ObjectType processType;
ob::ObjectType threadType;
EProcess* registeredProcesses{};

void deleteProcess(ob::ObjectHeader* header) {
    auto* process = header->as<EProcess>();
    if (process->getDirectoryTableBase() && process->closeAddressSpace() != NtStatus::Success)
        __builtin_trap();
    process->~EProcess();
}

void deleteThread(ob::ObjectHeader* header) {
    header->as<EThread>()->~EThread();
}

void initializeThreadType() noexcept {
    // On the bootstrap processor, object creation can be preempted by a
    // second caller. Publish the type ID only after the entire descriptor is
    // initialized, while excluding scheduling on this processor.
    ob::detail::HandleTableGuard guard;
    if (threadType.typeId == ob::ObjectTypeId::Thread) return;
    threadType.validAccessMask = 0x001fffff;
    threadType.genericMapping = {
        ob::STANDARD_RIGHTS_READ | 0x0008 | 0x0040,
        ob::STANDARD_RIGHTS_WRITE | 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020 | 0x0400,
        ob::STANDARD_RIGHTS_EXECUTE | ob::SYNCHRONIZE | 0x0800,
        0x001fffff
    };
    threadType.deleteProcedure = deleteThread;
    threadType.deferDelete = deferThreadObjectDelete;
    threadType.typeId = ob::ObjectTypeId::Thread;
}

void initializeProcessType() noexcept {
    ob::detail::HandleTableGuard guard;
    if (processType.typeId == ob::ObjectTypeId::Process) return;
    processType.validAccessMask = 0x001fffff;
    processType.genericMapping = {
        ob::STANDARD_RIGHTS_READ | 0x0400 | 0x0010,
        ob::STANDARD_RIGHTS_WRITE | 0x0080 | 0x0002 | 0x0008 | 0x0020 | 0x0040 | 0x0001 | 0x0100 | 0x0200 | 0x0800,
        ob::STANDARD_RIGHTS_EXECUTE | ob::SYNCHRONIZE,
        0x001fffff
    };
    processType.deleteProcedure = deleteProcess;
    processType.deferDelete = deferProcessObjectDelete;
    processType.typeId = ob::ObjectTypeId::Process;
}

void releaseObjectSecurity(void* value) noexcept {
    delete static_cast<se::FreestandingSecurityDescriptor*>(value);
}

NtStatus assignObjectSecurity(ob::ObjectHeader& header,
    const se::FreestandingSecurityDescriptor* security, uint32_t& desiredAccess,
    bool kernelAccess = false) noexcept {
    constexpr uint32_t maximumAllowed = 0x02000000;
    const bool maximum = (desiredAccess & maximumAllowed) != 0;
    const uint32_t requested = ob::MapGenericAccess(desiredAccess & ~maximumAllowed, header.type->genericMapping);
    if (requested & ~header.type->validAccessMask) return NtStatus::NotSupported;
    desiredAccess = requested;
    auto* current = ke::KeGetCurrentThread();
    auto* currentProcess = ke::KeGetCurrentProcess();
    const bool checkAccess = !kernelAccess && current && current->previousUserMode;
    // Early bootstrap can create kernel objects before a process token exists.
    // User callers always need a token-derived or explicit descriptor.
    if (!security && !currentProcess && !checkAccess) {
        if (maximum) desiredAccess = header.type->validAccessMask;
        return NtStatus::Success;
    }
    auto* copy = security ? new (std::nothrow_t{}) se::FreestandingSecurityDescriptor(*security)
                          : new (std::nothrow_t{}) se::FreestandingSecurityDescriptor{};
    if (!copy) return NtStatus::NoMemory;
    if (!security) copy->DaclPresent = false;
    se::FreestandingAccessToken *primary{}, *impersonation{};
    ob::ObjectHeader *primaryReference{}, *impersonationReference{};
    NtStatus status = NtStatus::Success;
    if (currentProcess)
        status = currentProcess->referencePrimaryToken(primary, primaryReference);
    if (status == NtStatus::Success && current && current->owningThread)
        status = current->owningThread->referenceImpersonationToken(impersonation, impersonationReference);
    const auto* token = se::SeGetEffectiveFreestandingToken(primary, impersonation);
    if (!security && !checkAccess && !token) {
        if (impersonationReference) se::SeDereferenceFreestandingToken(impersonationReference);
        if (primaryReference) se::SeDereferenceFreestandingToken(primaryReference);
        delete copy;
        if (maximum) desiredAccess = header.type->validAccessMask;
        return NtStatus::Success;
    }
    if (status == NtStatus::Success && checkAccess && copy->OwnerPresent) {
        bool validOwner = token && token->UserSid.equals(copy->Owner);
        if (token) {
            for (size_t i = 0; !validOwner && i < token->GroupCount && i < token->Groups.size(); ++i) {
                const auto& group = token->Groups[i];
                validOwner = (group.Attributes & 0x00000008) && !(group.Attributes & 0x00000010) &&
                             group.Sid.equals(copy->Owner);
            }
            if (se::SeTokenHasPrivilegeFreestanding(token, 18)) validOwner = true; // SeRestorePrivilege
        }
        if (!validOwner) status = NtStatus::InvalidOwner;
    }
    if (status == NtStatus::Success && !copy->OwnerPresent && token) {
        copy->Owner = token->DefaultOwner;
        copy->OwnerPresent = true;
    }
    if (status == NtStatus::Success && !copy->GroupPresent && token) {
        copy->Group = token->PrimaryGroup;
        copy->GroupPresent = true;
    }
    if (status == NtStatus::Success && !copy->DaclPresent) {
        if (!token || !token->DefaultDacl.DaclPresent) status = NtStatus::NotSupported;
        else {
            copy->Dacl = token->DefaultDacl.Dacl;
            copy->AceCount = token->DefaultDacl.AceCount;
            copy->DaclPresent = true;
            copy->DaclNull = token->DefaultDacl.DaclNull;
        }
    }
    if (status == NtStatus::Success)
        for (size_t i = 0; i < copy->AceCount; ++i)
            copy->Dacl[i].AccessMask = ob::MapGenericAccess(copy->Dacl[i].AccessMask, header.type->genericMapping);
    if (status == NtStatus::Success && checkAccess) {
        uint32_t granted{};
        if (maximum) {
            if (!se::SeMaximumAccessFreestanding(token, *copy, header.type->validAccessMask, granted) ||
                (requested & ~granted) || !granted) status = NtStatus::AccessDenied;
            else desiredAccess = granted;
        } else if (!se::SeAccessCheckFreestanding(token, *copy, requested, granted)) {
            status = NtStatus::AccessDenied;
        }
    } else if (status == NtStatus::Success && maximum) {
        desiredAccess = header.type->validAccessMask;
    }
    if (impersonationReference) se::SeDereferenceFreestandingToken(impersonationReference);
    if (primaryReference) se::SeDereferenceFreestandingToken(primaryReference);
    if (status != NtStatus::Success) { delete copy; return status; }
    header.securityDescriptor = copy;
    header.releaseSecurityDescriptor = releaseObjectSecurity;
    return NtStatus::Success;
}

NtStatus checkExistingObjectAccess(const ob::ObjectHeader& header, uint32_t desiredAccess,
                                   uint32_t& grantedAccess) noexcept {
    constexpr uint32_t maximumAllowed = 0x02000000;
    const bool maximum = (desiredAccess & maximumAllowed) != 0;
    const uint32_t requested = ob::MapGenericAccess(desiredAccess & ~maximumAllowed, header.type->genericMapping);
    if (requested & ~header.type->validAccessMask) return NtStatus::NotSupported;
    grantedAccess = requested;
    auto* current = ke::KeGetCurrentThread();
    if (!current || !current->previousUserMode) {
        if (maximum) grantedAccess = header.type->validAccessMask;
        return NtStatus::Success;
    }
    const auto* descriptor = static_cast<const se::FreestandingSecurityDescriptor*>(header.securityDescriptor);
    auto* currentProcess = ke::KeGetCurrentProcess();
    if (!currentProcess) return NtStatus::AccessDenied;
    se::FreestandingAccessToken *primary{}, *impersonation{};
    ob::ObjectHeader *primaryReference{}, *impersonationReference{};
    auto status = currentProcess->referencePrimaryToken(primary, primaryReference);
    if (status == NtStatus::Success && current->owningThread)
        status = current->owningThread->referenceImpersonationToken(impersonation, impersonationReference);
    if (status == NtStatus::Success) {
        const auto* token = se::SeGetEffectiveFreestandingToken(primary, impersonation);
        uint32_t granted{};
        // NtOpenProcess/NtOpenThread honor enabled SeDebugPrivilege before
        // consulting the object's DACL. The target's protected-process policy,
        // if introduced, must still be checked separately by its owner.
        if (se::SeTokenHasPrivilegeFreestanding(token, 20)) {
            grantedAccess = maximum ? header.type->validAccessMask : requested;
        } else if (!descriptor) {
            status = NtStatus::AccessDenied;
        } else if (maximum) {
            if (!se::SeMaximumAccessFreestanding(token, *descriptor, header.type->validAccessMask, granted) ||
                (requested & ~granted) || !granted) status = NtStatus::AccessDenied;
            else grantedAccess = granted;
        } else if (!se::SeAccessCheckFreestanding(token, *descriptor, requested, granted)) {
            status = NtStatus::AccessDenied;
        }
    }
    if (impersonationReference) se::SeDereferenceFreestandingToken(impersonationReference);
    if (primaryReference) se::SeDereferenceFreestandingToken(primaryReference);
    return status;
}
}

struct ProcessObjectFactory {
    static void bind(EProcess& process, ob::ObjectHeader* header) noexcept {
        process.objectHeader_ = header;
    }
    static void markSystem(EProcess& process) noexcept { process.systemProcess_ = true; }
    static NtStatus assignToken(EProcess& process, bool system) noexcept {
        se::FreestandingAccessToken* token{};
        ob::ObjectHeader* reference{};
        const auto status = se::SeCreateFreestandingPrimaryToken(system, token, reference);
        if (status != NtStatus::Success) return status;
        if (process.primaryTokenReference_) se::SeDereferenceFreestandingToken(process.primaryTokenReference_);
        process.primaryTokenReference_ = reference;
        process.primaryToken_ = token;
        return NtStatus::Success;
    }
    static NtStatus inheritToken(EProcess& child, const EProcess& parent) noexcept {
        if (!parent.primaryTokenReference_ || !parent.primaryToken_) return NtStatus::AccessDenied;
        se::FreestandingAccessToken* token{};
        ob::ObjectHeader* reference{};
        const auto status = se::SeDuplicateFreestandingPrimaryToken(*parent.primaryToken_, token, reference);
        if (status != NtStatus::Success) return status;
        if (child.primaryTokenReference_) se::SeDereferenceFreestandingToken(child.primaryTokenReference_);
        child.primaryTokenReference_ = reference;
        child.primaryToken_ = token;
        child.inheritedFromProcessId_ = parent.processId_;
        return NtStatus::Success;
    }
    static void markPublished(EProcess& process) noexcept {
        if (!process.cidRegistered_) {
            process.cidNext_ = registeredProcesses;
            registeredProcesses = &process;
            process.cidRegistered_ = true;
        }
        process.processPublished_ = true;
    }
    static void unregister(EProcess& process) noexcept {
        if (!process.cidRegistered_) return;
        auto** link = &registeredProcesses;
        while (*link && *link != &process) link = &(*link)->cidNext_;
        if (!*link) __builtin_trap();
        *link = process.cidNext_;
        process.cidNext_ = nullptr;
        process.cidRegistered_ = false;
    }
    static EProcess* find(Handle id) noexcept {
        for (auto* process = registeredProcesses; process; process = process->cidNext_)
            if (process->processId_ == id) return process;
        return nullptr;
    }
    static ob::ObjectHeader* header(EProcess& process) noexcept { return process.objectHeader_; }
    static const se::FreestandingSecurityDescriptor* defaultSecurity(EProcess& process) noexcept {
        return process.primaryToken_ ? &process.primaryToken_->DefaultDacl : nullptr;
    }
    template<typename T>
    static NtStatus capture(std::span<const T> source, std::unique_ptr<T[]>& storage) noexcept {
        if (source.empty()) return NtStatus::Success;
        if (source.size() > SIZE_MAX / sizeof(T) ||
            !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(source.data()),
                                              source.size_bytes(), false))
            return NtStatus::InvalidParameter;
        storage.reset(new (std::nothrow_t{}) T[source.size()]);
        if (!storage) return NtStatus::NoMemory;
        std::memcpy(storage.get(), source.data(), source.size_bytes());
        return NtStatus::Success;
    }
    static NtStatus captureInputs(EProcess& process, std::span<const BootstrapProcessImage> source,
                                  std::u16string_view imagePath, std::u16string_view commandLine,
                                  std::array<BootstrapProcessImage, 16>& captured,
                                  std::u16string_view& capturedPath,
                                  std::u16string_view& capturedCommand) noexcept {
        process.ownedImageInputs_.reset(new (std::nothrow_t{}) EProcess::OwnedImageInput[16]);
        if (!process.ownedImageInputs_) return NtStatus::NoMemory;
        for (size_t i = 0; i < source.size(); ++i) {
            auto& storage = process.ownedImageInputs_[i];
            auto status = capture(std::span{source[i].bytes}, storage.bytes);
            if (status != NtStatus::Success) return status;
            status = capture(std::span{source[i].name.data(), source[i].name.size()}, storage.name);
            if (status != NtStatus::Success) return status;
            status = capture(std::span{source[i].path.data(), source[i].path.size()}, storage.path);
            if (status != NtStatus::Success) return status;
            captured[i] = {{storage.name.get(), source[i].name.size()},
                           {storage.bytes.get(), source[i].bytes.size()},
                           {storage.path.get(), source[i].path.size()}};
        }
        auto status = capture(std::span{imagePath.data(), imagePath.size()}, process.ownedImagePath_);
        if (status != NtStatus::Success) return status;
        status = capture(std::span{commandLine.data(), commandLine.size()}, process.ownedCommandLine_);
        if (status != NtStatus::Success) return status;
        capturedPath = {process.ownedImagePath_.get(), imagePath.size()};
        capturedCommand = {process.ownedCommandLine_.get(), commandLine.size()};
        return NtStatus::Success;
    }
};

struct ThreadObjectFactory {
    static void bind(EThread& thread, ob::ObjectHeader* header) noexcept {
        thread.objectHeader_ = header;
        thread.objectOwned_ = true;
    }
    static ob::ObjectHeader* header(EThread& thread) noexcept { return thread.objectHeader_; }
};

namespace {
EProcess* systemProcess{};
ob::ObjectHeader* systemProcessReference{};

NtStatus getSystemProcess(EProcess*& process) noexcept {
    if (systemProcess) { process = systemProcess; return NtStatus::Success; }
    initializeProcessType();
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &processType, nullptr, sizeof(EProcess), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) EProcess{};
    ProcessObjectFactory::bind(*created, header);
    ProcessObjectFactory::markSystem(*created);
    status = ProcessObjectFactory::assignToken(*created, true);
    if (status != NtStatus::Success) { ob::ObDereferenceObject(header); return status; }
    uint32_t systemAccess{};
    status = assignObjectSecurity(*header, ProcessObjectFactory::defaultSecurity(*created), systemAccess, true);
    if (status != NtStatus::Success) { ob::ObDereferenceObject(header); return status; }
    status = created->initializeAddressSpace();
    if (status != NtStatus::Success) { ob::ObDereferenceObject(header); return status; }
    {
        ob::detail::HandleTableGuard guard;
        ProcessObjectFactory::markPublished(*created);
    }
    // The system process and its initial reference live for the kernel lifetime.
    systemProcessReference = header;
    systemProcess = created;
    process = created;
    return NtStatus::Success;
}

NtStatus createSystemThreadHandle(EProcess& process, EThread::SystemStartRoutine start,
                                  void* argument, uint32_t access, Handle& handle,
                                  EThread*& thread,
                                  const se::FreestandingSecurityDescriptor* security) noexcept {
    // Keep membership, lifecycle pin and handle publication indivisible on
    // the BSP, matching the ordinary thread creation path.
    ob::detail::HandleTableGuard guard;
    initializeThreadType();
    thread = nullptr;
    uint32_t mapped = ob::MapGenericAccess(access, threadType.genericMapping);
    if (mapped & ~(threadType.validAccessMask | uint32_t{0x02000000})) return NtStatus::NotImplemented;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &threadType, nullptr, sizeof(EThread), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) EThread{};
    status = assignObjectSecurity(*header, security, mapped, true);
    if (status == NtStatus::Success)
        status = created->initializeSystem(process, 65536, start, argument);
    if (status == NtStatus::Success) {
        header->addReference();
        ThreadObjectFactory::bind(*created, header);
        Handle raw{};
        status = ob::detail::kernelHandleTable().createHandle(header, raw, mapped);
        if (status == NtStatus::Success) {
            handle = ob::detail::encodeKernelHandle(raw);
            thread = created;
        } else if (created->reap() != NtStatus::Success) created->deferFailedCreationReap();
    }
    ob::ObDereferenceObject(header);
    return status;
}
}

NtStatus createThreadHandle(EProcess& process, size_t stackSize,
                            void (*start)(void*) noexcept, void* argument, uint32_t access,
                            Handle& handle, EThread*& thread,
                            const se::FreestandingSecurityDescriptor* security) noexcept {
    ob::detail::HandleTableGuard guard;
    initializeThreadType();
    thread = nullptr;
    access = ob::MapGenericAccess(access, threadType.genericMapping);
    if (access & ~threadType.validAccessMask) return NtStatus::NotImplemented;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &threadType, nullptr, sizeof(EThread), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) EThread{};
    status = assignObjectSecurity(*header, security, access, true);
    if (status == NtStatus::Success)
        status = created->initialize(process, stackSize, start, argument);
    if (status == NtStatus::Success) {
        header->addReference();
        ThreadObjectFactory::bind(*created, header);
        status = ob::detail::currentHandleTable().createHandle(header, handle, access);
        if (status == NtStatus::Success) thread = created;
        else if (created->reap() != NtStatus::Success) created->deferFailedCreationReap();
    }
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus createUserThreadHandle(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                size_t userStackSize, bool interruptible, uint32_t access,
                                Handle& handle, EThread*& thread, size_t commitSize,
                                uint32_t handleAttributes,
                                const se::FreestandingSecurityDescriptor* security) noexcept {
    ob::detail::HandleTableGuard guard;
    initializeThreadType();
    thread = nullptr;
    access = ob::MapGenericAccess(access, threadType.genericMapping);
    if (access & ~(threadType.validAccessMask | uint32_t{0x02000000})) return NtStatus::NotImplemented;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &threadType, nullptr, sizeof(EThread), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) EThread{};
    status = assignObjectSecurity(*header, security, access);
    if (status == NtStatus::Success)
        status = commitSize
            ? created->initializeUser(process, entry, kernelStackSize, userStackSize, commitSize, interruptible)
            : created->initializeUser(process, entry, kernelStackSize, userStackSize, interruptible);
    if (status != NtStatus::Success && created->kernelThread()) {
        // Initialization already attempted rollback. If a backend cleanup
        // failed, keep the partially initialized object alive for the same
        // deferred reaper rather than deleting its still-owned thread/stack.
        header->addReference();
        ThreadObjectFactory::bind(*created, header);
        created->deferFailedCreationReap();
    }
    if (status == NtStatus::Success) {
        header->addReference();
        ThreadObjectFactory::bind(*created, header);
        status = ob::detail::currentHandleTable().createHandle(header, handle, access, handleAttributes);
        if (status == NtStatus::Success) thread = created;
        else if (created->reap() != NtStatus::Success) created->deferFailedCreationReap();
    }
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus createProcessObject(EProcess*& process, ob::ObjectHeader*& reference,
    const se::FreestandingSecurityDescriptor* security, uint32_t desiredAccess,
    uint32_t* grantedAccess) noexcept {
    initializeProcessType();
    process = nullptr;
    reference = nullptr;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &processType, nullptr, sizeof(EProcess), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) EProcess{};
    ProcessObjectFactory::bind(*created, header);
    status = assignObjectSecurity(*header, security, desiredAccess);
    if (status == NtStatus::Success) status = ProcessObjectFactory::assignToken(*created, false);
    if (status == NtStatus::Success) status = created->initializeAddressSpace();
    if (status == NtStatus::Success) {
        process = created;
        reference = header;
        if (grantedAccess) *grantedAccess = desiredAccess;
        return NtStatus::Success;
    }
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus inheritProcessSecurityContext(EProcess& child, const EProcess& parent) noexcept {
    return ProcessObjectFactory::inheritToken(child, parent);
}

NtStatus publishProcessHandle(EProcess& process, ob::ObjectHeader* reference,
                              uint32_t access, uint32_t handleAttributes,
                              Handle& handle) noexcept {
    if (!reference || reference->type != &processType || reference->as<EProcess>() != &process)
        return NtStatus::InvalidParameter;
    access = ob::MapGenericAccess(access, processType.genericMapping);
    if (access & ~processType.validAccessMask) return NtStatus::NotImplemented;
    ob::detail::HandleTableGuard guard;
    const auto status = ob::detail::currentHandleTable().createHandle(reference, handle,
                                                                       access, handleAttributes);
    if (status == NtStatus::Success) ProcessObjectFactory::markPublished(process);
    return status;
}

void unregisterProcessObject(ob::ObjectHeader* header) noexcept {
    if (!header || header->type != &processType) return;
    ob::detail::HandleTableGuard guard;
    ProcessObjectFactory::unregister(*header->as<EProcess>());
}

NtStatus openProcessByClientId(const ClientId& clientId, uint32_t desiredAccess,
                               uint32_t attributes, Handle& handle) noexcept {
    handle = 0;
    ob::ObjectHeader* reference{};
    {
        ob::detail::HandleTableGuard guard;
        EProcess* process{};
        if (clientId.uniqueThread) {
            auto* thread = ke::detail::findClientThread(clientId.uniqueThread);
            if (thread && (!clientId.uniqueProcess || thread->pid == clientId.uniqueProcess))
                process = thread->owningProcess ? thread->owningProcess :
                    ProcessObjectFactory::find(thread->pid);
        } else if (clientId.uniqueProcess) {
            process = ProcessObjectFactory::find(clientId.uniqueProcess);
        }
        reference = process ? ProcessObjectFactory::header(*process) : nullptr;
        if (!reference || reference->type != &processType || !reference->tryAddReference())
            return NtStatus::InvalidCid;
    }
    uint32_t granted{};
    auto status = checkExistingObjectAccess(*reference, desiredAccess, granted);
    if (status == NtStatus::Success)
        status = ob::detail::currentHandleTable().createHandle(reference, handle, granted, attributes);
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus openThreadByClientId(const ClientId& clientId, uint32_t desiredAccess,
                              uint32_t attributes, Handle& handle) noexcept {
    handle = 0;
    if (!clientId.uniqueThread) return NtStatus::InvalidCid;
    ob::ObjectHeader* reference{};
    {
        ob::detail::HandleTableGuard guard;
        auto* thread = ke::detail::findClientThread(clientId.uniqueThread);
        if (thread && (!clientId.uniqueProcess || clientId.uniqueProcess == thread->pid) &&
            thread->owningThread)
            reference = ThreadObjectFactory::header(*thread->owningThread);
        if (!reference || reference->type != &threadType || !reference->tryAddReference())
            return NtStatus::InvalidCid;
    }
    uint32_t granted{};
    auto status = checkExistingObjectAccess(*reference, desiredAccess, granted);
    if (status == NtStatus::Success)
        status = ob::detail::currentHandleTable().createHandle(reference, handle, granted, attributes);
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus createProcessHandle(uint32_t access, Handle& handle, EProcess*& process,
                             uint32_t handleAttributes) noexcept {
    process = nullptr;
    ob::ObjectHeader* reference{};
    uint32_t grantedAccess{};
    auto status = createProcessObject(process, reference, nullptr, access, &grantedAccess);
    if (status != NtStatus::Success) return status;
    status = publishProcessHandle(*process, reference, grantedAccess, handleAttributes, handle);
    if (status != NtStatus::Success) process = nullptr;
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus createBootstrapUserProcess(std::span<const BootstrapProcessImage> images,
                                    std::u16string_view imagePath, std::u16string_view commandLine,
                                    size_t kernelStackSize, size_t userStackSize, bool interruptible,
                                    uint32_t processAccess, uint32_t threadAccess,
                                    Handle& processHandle, Handle& threadHandle,
                                    bool systemToken) noexcept {
    processHandle = threadHandle = 0;
    if (images.empty() || images.size() > 16 ||
        !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(images.data()),
                                          images.size_bytes(), false)) {
        return NtStatus::InvalidParameter;
    }
    uint64_t flags{}, previousRoot{};
    __asm__ __volatile__("pushfq; popq %0; cli; movq %%cr3, %1"
                         : "=r"(flags), "=r"(previousRoot) : : "memory");
    Handle processResult{}, threadResult{};
    EProcess* process{};
    EThread* thread{};
    auto status = createProcessHandle(processAccess, processResult, process);
    // Kernel-composed boot services can request the System identity explicitly;
    // ordinary bootstrap user processes keep the unprivileged construction token.
    if (status == NtStatus::Success && systemToken)
        status = ProcessObjectFactory::assignToken(*process, true);
    std::array<BootstrapProcessImage, 16> captured{};
    std::u16string_view capturedPath, capturedCommand;
    if (status == NtStatus::Success)
        status = ProcessObjectFactory::captureInputs(*process, images, imagePath, commandLine,
                                                     captured, capturedPath, capturedCommand);
    pe::PeImageView64 executable;
    if (status == NtStatus::Success && captured[0].name.empty()) status = NtStatus::InvalidParameter;
    if (status == NtStatus::Success) status = executable.inspect(captured[0].bytes);
    uintptr_t entry{};
    if (status == NtStatus::Success) {
        const auto& optional = executable.headers().optionalHeader;
        if (!optional.addressOfEntryPoint || optional.imageBase > UINTPTR_MAX - optional.addressOfEntryPoint)
            status = NtStatus::InvalidImageFormat;
        else entry = optional.imageBase + optional.addressOfEntryPoint;
    }
    bool targetActive = false;
    if (status == NtStatus::Success) {
        targetActive = hal::amd64::switchProcessPagingRootByPhysical(process->getDirectoryTableBase());
        if (!targetActive) status = NtStatus::Unsuccessful;
    }
    if (status == NtStatus::Success) {
        for (const auto& image : std::span{captured.data(), images.size()}) {
            status = process->loadNativeImage(image.name, image.bytes, image.path);
            if (status != NtStatus::Success) break;
        }
    }
    if (status == NtStatus::Success) {
        const auto* imageSet = process->nativeImages();
        const auto* mainImage = imageSet ? imageSet->module(0) : nullptr;
        const auto entryRva = executable.headers().optionalHeader.addressOfEntryPoint;
        if (!mainImage || entryRva >= mainImage->image.size() ||
            mainImage->base > UINTPTR_MAX - entryRva) {
            status = NtStatus::InvalidImageFormat;
        } else {
            // Relocatable bootstrap images may not reside at their PE preferred
            // base; enter at the actual mapped base plus the validated RVA.
            entry = mainImage->base + entryRva;
        }
    }
    if (status == NtStatus::Success) status = process->setNativeProcessParameters(capturedPath, capturedCommand);
    if (status == NtStatus::Success) status = process->finalizeNativeImages();
    if (status == NtStatus::Success) {
        status = createUserThreadHandle(*process, entry, kernelStackSize, userStackSize,
                                        interruptible, threadAccess, threadResult, thread);
    }
    if (targetActive && !hal::amd64::switchProcessPagingRootByPhysical(previousRoot)) __builtin_trap();
    if (status == NtStatus::Success) {
        const bool kuserReady = hal::amd64::bootstrapKuserSharedDataReady();
        if (kuserReady) {
            const uintptr_t attachRoot = hal::amd64::bootstrapKernelPagingRoot();
            const bool switchedForMapping = previousRoot != attachRoot;
            if ((switchedForMapping &&
                 !hal::amd64::switchProcessPagingRootByPhysical(attachRoot)) ||
                !hal::amd64::mapBootstrapKuserSharedDataForRoot(process->getDirectoryTableBase())) {
                status = NtStatus::Unsuccessful;
            }
            if (switchedForMapping &&
                !hal::amd64::switchProcessPagingRootByPhysical(previousRoot)) __builtin_trap();
        }
    }
    if (status == NtStatus::Success) {
        status = thread->ready();
    }
    if (status != NtStatus::Success) {
        if (thread) {
            if (thread->cancelBeforeStart() != NtStatus::Success) __builtin_trap();
            // cancelBeforeStart queues reaping, or schedules target-context
            // APC rundown. A failed immediate reap is retried there.
            (void)thread->reap();
            if (sys::NtClose(threadResult) != NtStatus::Success) __builtin_trap();
        }
        if (processResult && sys::NtClose(processResult) != NtStatus::Success) __builtin_trap();
    } else {
        processHandle = processResult;
        threadHandle = threadResult;
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return status;
}

NtStatus referenceProcessHandle(Handle handle, uint32_t access, EProcess*& process,
                                ob::ObjectHeader*& reference) noexcept {
    initializeProcessType();
    process = nullptr;
    reference = nullptr;
    ob::HandleTable* table{};
    Handle raw{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, access, &processType, &reference);
    if (status == NtStatus::Success) process = reference->as<EProcess>();
    return status;
}

NtStatus referenceThreadHandle(Handle handle, uint32_t access, EThread*& thread,
                               ob::ObjectHeader*& reference) noexcept {
    initializeThreadType();
    thread = nullptr;
    reference = nullptr;
    ob::HandleTable* table{};
    Handle raw{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, access, &threadType, &reference);
    if (status == NtStatus::Success) thread = reference->as<EThread>();
    return status;
}

NtStatus referenceCurrentProcess(EProcess*& process, ob::ObjectHeader*& reference) noexcept {
    ob::detail::HandleTableGuard guard;
    process = ke::KeGetCurrentProcess();
    reference = process ? ProcessObjectFactory::header(*process) : nullptr;
    if (!reference) { process = nullptr; return NtStatus::InvalidHandle; }
    reference->addReference();
    return NtStatus::Success;
}

NtStatus referenceCurrentThread(EThread*& thread, ob::ObjectHeader*& reference) noexcept {
    ob::detail::HandleTableGuard guard;
    auto* current = ke::KeGetCurrentThread();
    thread = current ? current->owningThread : nullptr;
    reference = thread ? ThreadObjectFactory::header(*thread) : nullptr;
    if (!reference) { thread = nullptr; return NtStatus::InvalidHandle; }
    reference->addReference();
    return NtStatus::Success;
}

bool isProcessObject(const ob::ObjectHeader& header) noexcept {
    return header.type == &processType;
}

bool isThreadObject(const ob::ObjectHeader& header) noexcept {
    return header.type == &threadType;
}

uint32_t bootstrapProcessObjectCount() noexcept {
    return processType.totalNumberOfObjects.load(std::memory_order_relaxed);
}

uint32_t bootstrapThreadObjectCount() noexcept {
    return threadType.totalNumberOfObjects.load(std::memory_order_relaxed);
}
}

namespace micant::ps {
extern "C" __declspec(dllexport) NtStatus PsCreateSystemThread(
    Handle* out, uint32_t access, ObjectAttributes* attributes, Handle processHandle,
    ClientId* clientId, void (*start)(void*), void* context) noexcept {
    if (!out || !start || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    se::FreestandingSecurityDescriptor capturedSecurity{};
    const se::FreestandingSecurityDescriptor* security{};
    if (attributes) {
        if (attributes->length != sizeof(ObjectAttributes)) return NtStatus::InvalidParameter;
        if (attributes->rootDirectory || attributes->objectName ||
            attributes->securityQualityOfService) return NtStatus::NotImplemented;
        if (attributes->attributes & ~OBJ_KERNEL_HANDLE) return NtStatus::InvalidParameter;
        if (attributes->securityDescriptor) {
            const auto capture = se::SeCaptureFreestandingSecurityDescriptor(
                attributes->securityDescriptor, mm::KernelMode, capturedSecurity);
            if (capture != NtStatus::Success) return capture;
            security = &capturedSecurity;
        }
    }
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    EProcess* process{};
    ob::ObjectHeader* reference{};
    NtStatus status{};
    if (!processHandle) {
        status = detail::getSystemProcess(process);
    } else if (processHandle == InvalidHandleValue) {
        process = ke::KeGetCurrentProcess();
        status = process ? NtStatus::Success : detail::getSystemProcess(process);
    } else {
        ob::HandleTable* table{};
        Handle raw{};
        status = ob::detail::resolveHandle(processHandle, table, raw, true);
        if (status == NtStatus::Success) {
            detail::initializeProcessType();
            // PsCreateSystemThread creates a thread inside the referenced
            // process, so the handle must grant PROCESS_CREATE_THREAD. Apply
            // the handle's granted-access mask even for kernel-mode callers;
            // a zero-access handle must not let a driver spawn threads.
            status = ob::ObReferenceObjectByHandle(raw, *table, 0x0002, &detail::processType, &reference);
            if (status == NtStatus::Success) process = reference->as<EProcess>();
        }
    }
    Handle handle{};
    EThread* thread{};
    if (status == NtStatus::Success && process->isTerminating()) status = NtStatus::ProcessIsTerminating;
    if (status == NtStatus::Success) status = detail::createSystemThreadHandle(*process, start, context, access, handle, thread, security);
    if (status == NtStatus::Success) {
        status = thread->ready();
        if (status != NtStatus::Success) {
            if (thread->cancelBeforeStart() != NtStatus::Success) __builtin_trap();
            // The lifecycle reference and reaper survive closing this handle.
            (void)thread->reap();
            ob::HandleTable* table{}; Handle raw{};
            if (ob::detail::resolveHandle(handle, table, raw, true) != NtStatus::Success ||
                table->closeHandle(raw) != NtStatus::Success) __builtin_trap();
        } else {
            if (clientId) *clientId = {process->processId(), thread->kernelThread()->tid};
            *out = handle;
        }
    }
    if (reference) ob::ObDereferenceObject(reference);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return status;
}
}
