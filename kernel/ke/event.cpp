#include "micant/km/core/sync.hpp"
#include "micant/km/core/timer.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include <new>

namespace micant::sys {
namespace {
constexpr uint32_t EventModifyState = 0x0002;
constexpr uint32_t EventAllAccess = 0x001F0003;
ob::ObjectType eventType;
ob::ObjectType timerType;
ob::ObjectType mutantType;
using Event = sync::detail::BootstrapEvent;

struct NamedMutantEntry {
    ob::ObjectHeader* object{};
    char16_t name[128]{};
};
std::array<NamedMutantEntry, 64> namedMutants{};

bool equalMutantName(const char16_t* left, const char16_t* right) noexcept {
    for (size_t i = 0; i < 128; ++i) {
        auto fold = [](char16_t c) { return c >= u'a' && c <= u'z' ? c - 32 : c; };
        if (fold(left[i]) != fold(right[i])) return false;
        if (!left[i]) return true;
    }
    return false;
}

ob::ObjectHeader* findNamedMutant(const char16_t* name) noexcept {
    ob::detail::HandleTableGuard guard;
    for (auto& entry : namedMutants) {
        if (entry.object && equalMutantName(entry.name, name) && entry.object->tryAddReference())
            return entry.object;
    }
    return nullptr;
}

NtStatus publishNamedMutant(const char16_t* name, ob::ObjectHeader* object) noexcept {
    ob::detail::HandleTableGuard guard;
    NamedMutantEntry* freeEntry{};
    for (auto& entry : namedMutants) {
        if (entry.object && equalMutantName(entry.name, name)) return NtStatus::ObjectNameCollision;
        if (!entry.object && !freeEntry) freeEntry = &entry;
    }
    if (!freeEntry) return NtStatus::InsufficientResources;
    size_t i{};
    do { freeEntry->name[i] = name[i]; } while (name[i] && ++i < 127);
    freeEntry->name[i] = 0;
    object->addReference(); // The canonical object-name index owns a reference.
    freeEntry->object = object;
    return NtStatus::Success;
}

void unpublishNamedMutant(ob::ObjectHeader* object) noexcept {
    bool release{};
    {
        ob::detail::HandleTableGuard guard;
        for (auto& entry : namedMutants) {
            if (entry.object == object) {
                entry.object = nullptr;
                entry.name[0] = 0;
                release = true;
                break;
            }
        }
    }
    if (release) ob::ObDereferenceObject(object);
}

void closeMutant(ob::ObjectHeader* object, Handle) {
    if (object->handleCount.load(std::memory_order_relaxed) == 0) unpublishNamedMutant(object);
}

void deleteEvent(ob::ObjectHeader* header) { header->as<Event>()->~Event(); }
void deleteTimer(ob::ObjectHeader* header) {
    auto* timer = header->as<timer::KTIMER>();
    if (!ke::clock_timer::cancelAndRemoveDpc(timer->deadline)) __builtin_trap();
    timer->~KTIMER();
}
void deleteMutant(ob::ObjectHeader* header) {
    unpublishNamedMutant(header);
    header->as<sync::detail::BootstrapMutant>()->~BootstrapMutant();
}
}

#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
NtStatus NtDelayExecution(bool alertable, const LargeInteger* interval) {
    auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    if (!interval) return NtStatus::AccessViolation;
    LargeInteger captured{};
    auto status = ex::ProbeForRead(interval, sizeof(captured), alignof(LargeInteger), mode);
    if (status != NtStatus::Success) return status;
    status = ex::ExSafeCopyFromUser(&captured, interval, sizeof(captured), mode);
    if (status != NtStatus::Success) return status;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceState;

    uint64_t delay100ns{};
    if (captured.quadPart < 0) {
        delay100ns = static_cast<uint64_t>(-(captured.quadPart + 1)) + 1;
    } else if (captured.quadPart > 0) {
        uint64_t now{};
        if (!ke::KeQuerySystemTime100ns(now)) return NtStatus::DeviceNotReady;
        const auto absolute = static_cast<uint64_t>(captured.quadPart);
        delay100ns = absolute > now ? absolute - now : 0;
    }
    if (!delay100ns) {
        if (!current) return NtStatus::InvalidDeviceState;
        return ke::KeTestAlertThread(mode == mm::UserMode ? ke::UserMode : ke::KernelMode)
            ? NtStatus::Alerted : NtStatus::Success;
    }
    if (!current || current->eventWaitInProgress) return NtStatus::InvalidParameter;
    current->eventWaitInProgress = true;
    current->eventWaitUserMode = current->previousUserMode;
    current->eventWaitCanTerminate = current->eventWaitUserMode &&
        ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL;
    sync::detail::BootstrapEvent delayEvent(false, false);
    status = delayEvent.wait(false, alertable, delay100ns);
    const bool terminate = current->terminationRequested && current->eventWaitCanTerminate &&
        current->serviceTermination;
    auto terminationRoutine = current->serviceTermination;
    void* terminationContext = current->terminationContext;
    current->eventWaitInProgress = false;
    current->eventWaitUserMode = false;
    current->eventWaitCanTerminate = false;
    if (terminate) terminationRoutine(terminationContext);
    return status == NtStatus::Timeout ? NtStatus::Success : status;
}

NtStatus NtAlertThread(Handle threadHandle) {
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceState;
    ps::EThread* target{};
    ob::ObjectHeader* reference{};
    ke::KThread* kernelThread{};
    const auto currentPseudoHandle = threadHandle == static_cast<Handle>(-2);
    const auto status = currentPseudoHandle ? NtStatus::Success
        : ps::detail::referenceThreadHandle(threadHandle, 0x0004, target, reference);
    if (status != NtStatus::Success) return status;
    kernelThread = currentPseudoHandle ? ke::KeGetCurrentThread() : target->kernelThread();
    if (!kernelThread) {
        if (reference) ob::ObDereferenceObject(reference);
        return NtStatus::ThreadIsTerminating;
    }
    {
        ob::detail::HandleTableGuard guard;
        kernelThread->alerted[ke::UserMode] = true;
        if (kernelThread->alertable && kernelThread->eventWaitUserMode && kernelThread->eventWaitObject) {
            auto* event = static_cast<sync::detail::BootstrapEvent*>(kernelThread->eventWaitObject);
            (void)event->cancelWaitForAlert(*kernelThread);
        }
    }
    if (reference) ob::ObDereferenceObject(reference);
    return NtStatus::Success;
}

NtStatus NtTestAlert() {
    return ke::KeTestAlertThread(ke::UserMode) ? NtStatus::Alerted : NtStatus::Success;
}
#endif
namespace {
ob::ObjectHeader* deferredTimers{};
ke::passive_work::Item timerDeleteWork{};
void finishDeferredTimers(void*) noexcept {
    for (;;) {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        auto* header = deferredTimers;
        if (header) deferredTimers = header->deferredDeleteNext;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        if (!header) return;
        header->deferredDeleteNext = nullptr;
        ob::ObFinishDelete(header);
    }
}
bool deferTimerDelete(ob::ObjectHeader* header) noexcept {
    if (ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL && !ke::g_CurrentCpu.dpcRoutineActive)
        return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    header->deferredDeleteNext = deferredTimers;
    deferredTimers = header;
    timerDeleteWork.routine = finishDeferredTimers;
    // A timer can lose its last reference after the clock starts but before
    // the passive worker exists. Retain the zero-reference header until the
    // worker is initialized rather than invoking a blocking delete at DPC or
    // trapping solely because the queue is not ready yet.
    if (ke::passive_work::detail::initialized && !timerDeleteWork.queued &&
        !ke::passive_work::queue(timerDeleteWork)) __builtin_trap();
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return true;
}
}

void scheduleDeferredTimerDeletes() noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (deferredTimers && ke::passive_work::detail::initialized && !timerDeleteWork.queued &&
        !ke::passive_work::queue(timerDeleteWork)) __builtin_trap();
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
}

void initializeBootstrapEventServices() noexcept {
    eventType.typeId = ob::ObjectTypeId::Event;
    eventType.deleteProcedure = deleteEvent;
    timerType.typeId = ob::ObjectTypeId::Timer;
    timerType.deleteProcedure = deleteTimer;
    timerType.deferDelete = deferTimerDelete;
    mutantType.typeId = ob::ObjectTypeId::Mutant;
    mutantType.deleteProcedure = deleteMutant;
    mutantType.closeProcedure = closeMutant;
}

NtStatus NtCreateMutant(Handle* out, uint32_t access, ObjectAttributes* attributes,
                        bool initialOwner) {
    constexpr uint32_t MutantAllAccess = 0x001F0001u;
    if (!out) return NtStatus::InvalidParameter;
    auto* current = ke::KeGetCurrentThread();
    if (initialOwner && !current) return NtStatus::InvalidDeviceState;
    uint32_t handleAttributes{};
    char16_t capturedName[128]{};
    bool named{};
    if (attributes) {
        auto* caller = ke::KeGetCurrentThread();
        const auto mode = caller && caller->previousUserMode ? mm::UserMode : mm::KernelMode;
        ObjectAttributes capturedAttributes{};
        // NtCreateMutant is entered with a kernel-captured OBJECT_ATTRIBUTES
        // from the syscall dispatcher. Only its nested UNICODE_STRING remains
        // a user pointer and is probed below.
        capturedAttributes = *attributes;
        auto status = NtStatus::Success;
        if (capturedAttributes.length != sizeof(ObjectAttributes) || capturedAttributes.rootDirectory ||
            (capturedAttributes.attributes & ~(OBJ_INHERIT | OBJ_CASE_INSENSITIVE | OBJ_OPENIF)) ||
            capturedAttributes.securityDescriptor || capturedAttributes.securityQualityOfService)
            return NtStatus::NotImplemented;
        handleAttributes = capturedAttributes.attributes & OBJ_INHERIT;
        if (capturedAttributes.objectName) {
            UnicodeString nameInfo{};
            status = ex::ProbeForRead(capturedAttributes.objectName, sizeof(nameInfo), alignof(uint16_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyFromUser(&nameInfo, capturedAttributes.objectName, sizeof(nameInfo), mode);
            if (status != NtStatus::Success) return status;
            if (!nameInfo.buffer || !nameInfo.length || (nameInfo.length & 1) ||
                nameInfo.length >= sizeof(capturedName)) return NtStatus::ObjectNameInvalid;
            status = ex::ProbeForRead(nameInfo.buffer, nameInfo.length, alignof(char16_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyFromUser(capturedName, nameInfo.buffer, nameInfo.length, mode);
            if (status != NtStatus::Success) return status;
            const size_t chars = nameInfo.length / sizeof(char16_t);
            for (size_t i = 0; i < chars; ++i) {
                if (!capturedName[i] || capturedName[i] == u'\\' || capturedName[i] == u'/')
                    return NtStatus::ObjectNameInvalid;
            }
            capturedName[chars] = 0;
            named = true;
        }
    }
    if (access & ~MutantAllAccess) return NtStatus::NotImplemented;
    if (named) {
        if (auto* existing = findNamedMutant(capturedName)) {
            auto status = ob::detail::currentHandleTable().createHandle(existing, *out, access, handleAttributes);
            ob::ObDereferenceObject(existing);
            return status == NtStatus::Success ? NtStatus::ObjectNameExists : status;
        }
    }
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &mutantType, nullptr,
        sizeof(sync::detail::BootstrapMutant), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* mutant = new (body) sync::detail::BootstrapMutant(initialOwner);
    if (named) {
        status = publishNamedMutant(capturedName, header);
        if (status == NtStatus::ObjectNameCollision) {
            ob::ObDereferenceObject(header);
            return NtCreateMutant(out, access, attributes, initialOwner);
        }
        if (status != NtStatus::Success) {
            ob::ObDereferenceObject(header);
            return status;
        }
    }
    status = ob::detail::currentHandleTable().createHandle(header, *out, access,
        handleAttributes);
    if (status != NtStatus::Success && named) unpublishNamedMutant(header);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus NtReleaseMutant(Handle handle, int32_t* previousCount) {
    ob::ObjectHeader* header{};
    ob::HandleTable* table{};
    Handle raw{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, 0x0001u,
            &mutantType, &header);
    if (status != NtStatus::Success) return status;
    auto* mutant = header->as<sync::detail::BootstrapMutant>();
    status = mutant->release(previousCount);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus NtCreateTimer(Handle* out, uint32_t access, ObjectAttributes* attributes, uint32_t type) {
    constexpr uint32_t TimerAllAccess = 0x001F0003;
    if (!out || type > 1) return NtStatus::InvalidParameter;
    if (attributes) {
        if (attributes->length != sizeof(ObjectAttributes)) return NtStatus::InvalidParameter;
        if (attributes->rootDirectory || attributes->objectName || attributes->attributes ||
            attributes->securityDescriptor || attributes->securityQualityOfService)
            return NtStatus::NotImplemented;
    }
    if (access & ~TimerAllAccess) return NtStatus::NotImplemented;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &timerType, nullptr, sizeof(timer::KTIMER), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* object = new (body) timer::KTIMER{};
    timer::KeInitializeTimerEx(object, static_cast<timer::TimerType>(type));
    status = ob::detail::currentHandleTable().createHandle(header, *out, access);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus NtSetTimer(Handle handle, LargeInteger* dueTime, void* apc, void* context,
                    bool resume, uint32_t period, bool* previousState) {
    if (!dueTime) return NtStatus::InvalidParameter;
    if (apc || context || resume || period > INT32_MAX) return NtStatus::NotImplemented;
    ob::HandleTable* table{}; Handle raw{}; ob::ObjectHeader* header{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, 0x0002, &timerType, &header);
    if (status != NtStatus::Success) return status;
    auto* object = header->as<timer::KTIMER>();
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const bool priorState = timer::KeReadStateTimer(object);
    bool previousInsertion{};
    status = timer::KiSetTimerChecked(object, *dueTime, period, nullptr, previousInsertion);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (status == NtStatus::Success && previousState) *previousState = priorState;
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus NtCancelTimer(Handle handle, bool* currentState) {
    ob::HandleTable* table{}; Handle raw{}; ob::ObjectHeader* header{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, 0x0002, &timerType, &header);
    if (status != NtStatus::Success) return status;
    auto* object = header->as<timer::KTIMER>();
    if (currentState) *currentState = timer::KeReadStateTimer(object);
    (void)timer::KeCancelTimer(object);
    ob::ObDereferenceObject(header);
    return NtStatus::Success;
}

NtStatus referenceBootstrapEventForIo(Handle handle, ob::ObjectHeader*& header) noexcept {
    header = nullptr;
    ob::HandleTable* table{}; Handle raw{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, EventModifyState, &eventType, &header);
    return status;
}

uint32_t bootstrapEventObjectCount() noexcept {
    return eventType.totalNumberOfObjects.load(std::memory_order_relaxed);
}

NtStatus NtCreateEvent(Handle* out, uint32_t access, ObjectAttributes* attributes,
                       uint32_t type, bool initialState) {
    if (!out) return NtStatus::InvalidParameter;
    if (type > 1) return NtStatus::InvalidParameter4;
    uint32_t handleAttributes{};
    if (attributes) {
        if (attributes->length != sizeof(ObjectAttributes)) return NtStatus::InvalidParameter;
        if (attributes->rootDirectory || attributes->objectName ||
            (attributes->attributes & ~OBJ_INHERIT) ||
            attributes->securityDescriptor || attributes->securityQualityOfService)
            return NtStatus::NotImplemented;
        handleAttributes = attributes->attributes & OBJ_INHERIT;
    }
    if (access & ~EventAllAccess) return NtStatus::NotImplemented;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &eventType, nullptr, sizeof(Event), &body, &header);
    if (status != NtStatus::Success) return status;
    new (body) Event(type == 0, initialState);
    status = ob::detail::currentHandleTable().createHandle(header, *out, access, handleAttributes);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus NtSetEvent(Handle handle, int32_t* previousState) {
    ob::ObjectHeader* header{};
    ob::HandleTable* table{};
    Handle raw{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, EventModifyState, &eventType, &header);
    if (status != NtStatus::Success) return status;
    const bool previous = header->as<Event>()->set();
    if (previousState) *previousState = previous ? 1 : 0;
    ob::ObDereferenceObject(header);
    return NtStatus::Success;
}

NtStatus NtWaitForSingleObject(Handle handle, bool alertable, LargeInteger* timeout) {
    bool poll = false;
    uint64_t relativeTimeout = 0;
    uint64_t absoluteTimeout = 0;
    if (timeout) {
        auto* caller = ke::KeGetCurrentThread();
        const auto mode = caller && caller->previousUserMode ? mm::UserMode : mm::KernelMode;
        LargeInteger captured{};
        auto status = ex::ProbeForRead(timeout, sizeof(captured), alignof(LargeInteger), mode);
        if (status != NtStatus::Success) return status;
        status = ex::ExSafeCopyFromUser(&captured, timeout, sizeof(captured), mode);
        if (status != NtStatus::Success) return status;
        const int64_t due = captured.quadPart;
        if (due < 0) {
            // Avoid signed overflow for INT64_MIN.
            relativeTimeout = static_cast<uint64_t>(-(due + 1)) + 1;
        } else if (due == 0) {
            poll = true;
        } else {
            absoluteTimeout = static_cast<uint64_t>(due);
        }
    }
    ob::ObjectHeader* header{};
    NtStatus status{};
    if (handle == InvalidHandleValue) {
        ps::EProcess* process{};
        status = ps::detail::referenceCurrentProcess(process, header);
    } else if (handle == static_cast<Handle>(-2)) {
        ps::EThread* thread{};
        status = ps::detail::referenceCurrentThread(thread, header);
    } else {
        ob::HandleTable* table{};
        Handle raw{};
        status = ob::detail::resolveHandle(handle, table, raw);
        if (status == NtStatus::Success)
            status = ob::ObReferenceObjectByHandle(raw, *table, ob::SYNCHRONIZE, nullptr, &header);
    }
    if (status != NtStatus::Success) return status;
    Event* waitObject{};
    sync::detail::BootstrapMutant* waitMutant{};
    if (header->type == &eventType) {
        waitObject = header->as<Event>();
    } else if (header->type == &timerType) {
        waitObject = &header->as<timer::KTIMER>()->event;
    } else if (header->type == &mutantType) {
        waitMutant = header->as<sync::detail::BootstrapMutant>();
    } else if (ps::detail::isProcessObject(*header)) {
        waitObject = &header->as<ps::EProcess>()->terminationEvent();
    } else if (ps::detail::isThreadObject(*header)) {
        waitObject = &header->as<ps::EThread>()->terminationEvent();
    } else {
        ob::ObDereferenceObject(header);
        return NtStatus::ObjectTypeMismatch;
    }
    if (absoluteTimeout) {
        uint64_t now{};
        if (!ke::KeQuerySystemTime100ns(now)) {
            ob::ObDereferenceObject(header);
            return NtStatus::DeviceNotReady;
        }
        if (absoluteTimeout <= now) poll = true;
        else relativeTimeout = absoluteTimeout - now;
    }
    auto* current = ke::KeGetCurrentThread();
    if (!current || current->eventWaitInProgress) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidParameter;
    }
    current->eventWaitInProgress = true;
    current->eventWaitUserMode = current->previousUserMode;
    // Only the user syscall wait owns a termination return boundary. Kernel
    // callers may hold unrelated resources across this wait and must unwind
    // them before a pending termination can be serviced.
    current->eventWaitCanTerminate = current->eventWaitUserMode &&
        ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL;
    NtStatus result{};
    if (waitObject) {
        result = waitObject->wait(poll, alertable, relativeTimeout);
    } else {
        result = waitMutant->wait(poll, alertable, relativeTimeout);
    }
    // The wait object has unlinked its stack waiter before returning. Defer
    // termination until this syscall has also cleared the thread's wait
    // registration and released the Ob reference that pins the waited object.
    const bool serviceTermination = current->terminationRequested &&
        current->eventWaitCanTerminate && current->serviceTermination;
    auto terminate = current->serviceTermination;
    void* terminationContext = current->terminationContext;
    current->eventWaitInProgress = false;
    current->eventWaitUserMode = false;
    current->eventWaitCanTerminate = false;
    ob::ObDereferenceObject(header);
    if (serviceTermination) terminate(terminationContext);
    return result;
}
}
