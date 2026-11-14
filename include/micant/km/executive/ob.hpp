#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>
#include <atomic>
#include <functional>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <new>
#include <limits>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"

namespace micant::ob {

// ============================================================================
// 1. Standard NT Object Attributes & Access Rights
// ============================================================================
using micant::OBJ_INHERIT;
using micant::OBJ_PROTECT_CLOSE;
using micant::OBJ_AUDIT_OBJECT_CLOSE;
using micant::OBJ_PERMANENT;
using micant::OBJ_EXCLUSIVE;
using micant::OBJ_CASE_INSENSITIVE;
using micant::OBJ_OPENIF;
using micant::OBJ_OPENLINK;
using micant::OBJ_KERNEL_HANDLE;
using micant::OBJ_FORCE_ACCESS_CHECK;
inline constexpr uint32_t OBJ_VALID_ATTRIBUTES   = 0x000007F2L;
inline constexpr uint32_t OB_VALID_HANDLE_ATTRIBUTES = OBJ_PROTECT_CLOSE | OBJ_INHERIT | OBJ_AUDIT_OBJECT_CLOSE;

// Standard Access Rights (clean-room NT Win32 mapping)
inline constexpr uint32_t DELETE                  = 0x00010000L;
inline constexpr uint32_t READ_CONTROL            = 0x00020000L;
inline constexpr uint32_t WRITE_DAC               = 0x00040000L;
inline constexpr uint32_t WRITE_OWNER             = 0x00080000L;
inline constexpr uint32_t SYNCHRONIZE             = 0x00100000L;
inline constexpr uint32_t STANDARD_RIGHTS_REQUIRED= 0x000F0000L;
inline constexpr uint32_t STANDARD_RIGHTS_READ    = READ_CONTROL;
inline constexpr uint32_t STANDARD_RIGHTS_WRITE   = READ_CONTROL;
inline constexpr uint32_t STANDARD_RIGHTS_EXECUTE = READ_CONTROL;
inline constexpr uint32_t STANDARD_RIGHTS_ALL     = 0x001F0000L;

inline constexpr uint32_t GENERIC_READ            = 0x80000000L;
inline constexpr uint32_t GENERIC_WRITE           = 0x40000000L;
inline constexpr uint32_t GENERIC_EXECUTE         = 0x20000000L;
inline constexpr uint32_t GENERIC_ALL             = 0x10000000L;
inline constexpr uint32_t MAXIMUM_ALLOWED         = 0x02000000L;

using KPROCESSOR_MODE = uint8_t;
inline constexpr KPROCESSOR_MODE KernelMode = 0;
inline constexpr KPROCESSOR_MODE UserMode = 1;

using OBJECT_ATTRIBUTES = ObjectAttributes;
using POBJECT_ATTRIBUTES = ObjectAttributes*;

inline constexpr size_t OB_MAX_SYMLINK_RECURSION = 32;

struct GENERIC_MAPPING {
    uint32_t genericRead{0};
    uint32_t genericWrite{0};
    uint32_t genericExecute{0};
    uint32_t genericAll{0};
};

enum class ObjectTypeId : uint32_t {
    Type,
    Directory,
    SymbolicLink,
    Device,
    Driver,
    Process,
    Thread,
    Section,
    File,
    Event,
    Mutant,
    Semaphore,
    Key,
    IoCompletion,
    Job,
    Timer,
    BlockDevice,
    Token
};

struct ObjectHeader;
struct ObjectType;

// Object Manager Procedural Callbacks
using OB_DUMP_METHOD = void (*)(ObjectHeader* object);
using OB_OPEN_METHOD = NtStatus (*)(ObjectHeader* object, uint32_t accessMask);
using OB_CLOSE_METHOD = void (*)(ObjectHeader* object, Handle handle);
using OB_DELETE_METHOD = void (*)(ObjectHeader* object);
using OB_PARSE_METHOD = NtStatus (*)(
    ObjectHeader* parseObject,
    const ObjectType* objectType,
    std::wstring_view remainingPath,
    ObjectHeader** outFoundObject
);

using OB_ACCESS_CHECK_PROC = NtStatus (*)(
    void* securityDescriptor,
    uint32_t desiredAccess,
    uint32_t* grantedAccess
);

inline OB_ACCESS_CHECK_PROC g_ObpAccessCheckProcedure = nullptr;

inline void ObSetAccessCheckProcedure(OB_ACCESS_CHECK_PROC proc) noexcept {
    g_ObpAccessCheckProcedure = proc;
}

// ============================================================================
// 2. Dave Cutler Object Header Substructures
// ============================================================================
struct OBJECT_HEADER_NAME_INFO {
    void* directory{nullptr}; // Pointer to DirectoryObject
    std::wstring name;
    uint32_t queryReferences{0};
};

struct OBJECT_HEADER_CREATOR_INFO {
    uint32_t creatorProcessId{0};
    uint32_t creatorThreadId{0};
    uint64_t createTime{0};
};

struct OBJECT_HEADER_QUOTA_INFO {
    uint32_t pagedPoolCharge{0};
    uint32_t nonPagedPoolCharge{0};
    uint32_t securityDescriptorCharge{0};
};

struct OBJECT_HEADER_HANDLE_INFO {
    uint32_t handleCount{0};
    uint32_t singleHandle{0};
};

// ============================================================================
// 3. Object Manager Diagnostic Telemetry
// ============================================================================
struct OB_TELEMETRY {
    size_t totalObjectsCreated{0};
    size_t totalObjectsDestroyed{0};
    size_t totalHandlesOpened{0};
    size_t totalHandlesClosed{0};
    size_t totalHandlesDuplicated{0};
    size_t totalSymlinksResolved{0};
    size_t totalSymlinkRecursionsHit{0};
    size_t totalSecurityChecksPassed{0};
    size_t totalSecurityChecksDenied{0};
    size_t totalDirectoryInserts{0};
    size_t totalDirectoryLookups{0};
    size_t totalParseProcedureCalls{0};
    size_t totalTemporaryObjectsUnlinked{0};
};

inline OB_TELEMETRY g_ObTelemetry{};
inline std::mutex g_ObTelemetryMutex{};

inline OB_TELEMETRY ObQueryTelemetry() {
    std::lock_guard<std::mutex> lk(g_ObTelemetryMutex);
    return g_ObTelemetry;
}

inline void ObResetTelemetryForTesting() {
    std::lock_guard<std::mutex> lk(g_ObTelemetryMutex);
    g_ObTelemetry = OB_TELEMETRY{};
}

// ============================================================================
// 4. Object Type & Object Header
// ============================================================================

/**
 * @brief Representation of an Object Type in the Object Manager.
 */
[[nodiscard]] inline uint32_t MapGenericAccess(uint32_t access, const GENERIC_MAPPING& mapping) noexcept {
    uint32_t mapped = access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
    if (access & GENERIC_READ) mapped |= mapping.genericRead;
    if (access & GENERIC_WRITE) mapped |= mapping.genericWrite;
    if (access & GENERIC_EXECUTE) mapped |= mapping.genericExecute;
    if (access & GENERIC_ALL) mapped |= mapping.genericAll;
    return mapped;
}

struct ObjectType {
    std::wstring typeName;
    ObjectTypeId typeId{ObjectTypeId::Type};
    std::atomic<uint32_t> totalNumberOfObjects{0};
    std::atomic<uint32_t> totalNumberOfHandles{0};
    uint32_t validAccessMask{0x001FFFFF};
    GENERIC_MAPPING genericMapping{
        STANDARD_RIGHTS_READ | 0x0001,
        STANDARD_RIGHTS_WRITE | 0x0002,
        STANDARD_RIGHTS_EXECUTE | 0x0004,
        STANDARD_RIGHTS_ALL | 0x000F
    };

    OB_DUMP_METHOD dumpProcedure{nullptr};
    OB_OPEN_METHOD openProcedure{nullptr};
    OB_CLOSE_METHOD closeProcedure{nullptr};
    OB_DELETE_METHOD deleteProcedure{nullptr};
#if defined(MICANT_FREESTANDING)
    // A type may transfer a zero-reference header to a passive cleanup worker.
    bool (*deferDelete)(ObjectHeader*) noexcept{nullptr};
#endif
    OB_PARSE_METHOD parseProcedure{nullptr};
};

/**
 * @brief Canonical Object Header prepended to every allocated kernel object body.
 */
struct ObjectHeader {
    std::atomic<int32_t> pointerCount{1};
    std::atomic<int32_t> handleCount{0};
    const ObjectType* type{nullptr};
    std::wstring objectName;
    void* parentDirectory{nullptr}; // Pointer to DirectoryObject
    uint32_t attributes{0};
    void* securityDescriptor{nullptr};

    // Dave Cutler Header Info extensions
    OBJECT_HEADER_NAME_INFO nameInfo{};
    OBJECT_HEADER_CREATOR_INFO creatorInfo{};
    OBJECT_HEADER_QUOTA_INFO quotaInfo{};
    OBJECT_HEADER_HANDLE_INFO handleInfo{};
    bool dynamicallyAllocated{false};
#if defined(MICANT_FREESTANDING)
    ObjectHeader* deferredDeleteNext{nullptr};
    void (*releaseSecurityDescriptor)(void*) noexcept{nullptr};
#endif

    [[nodiscard]] void* getBody() noexcept {
        return reinterpret_cast<void*>(this + 1);
    }

    template<typename T>
    [[nodiscard]] T* as() noexcept {
        return reinterpret_cast<T*>(getBody());
    }

    void addReference() noexcept {
        int32_t count = pointerCount.load(std::memory_order_relaxed);
        while (count > 0 && count < INT32_MAX) {
            if (pointerCount.compare_exchange_weak(count, count + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) return;
        }
        __builtin_trap();
    }

    // Used by weak object registries while their lock/exclusion is held.
    // A zero-reference object may be awaiting deferred deletion and must
    // never be resurrected.
    [[nodiscard]] bool tryAddReference() noexcept {
        int32_t count = pointerCount.load(std::memory_order_relaxed);
        while (count > 0 && count < INT32_MAX) {
            if (pointerCount.compare_exchange_weak(count, count + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) return true;
        }
        return false;
    }

    bool releaseReference() noexcept {
        int32_t count = pointerCount.load(std::memory_order_relaxed);
        while (count > 0) {
            if (pointerCount.compare_exchange_weak(count, count - 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) return count == 1;
        }
        __builtin_trap();
    }

    [[nodiscard]] bool isPermanent() const noexcept {
        return (attributes & OBJ_PERMANENT) != 0;
    }

    [[nodiscard]] bool isExclusive() const noexcept {
        return (attributes & OBJ_EXCLUSIVE) != 0;
    }
};

// ============================================================================
// 5. Directory Object with NT 37-Bucket Hash Chains
// ============================================================================
#if !defined(MICANT_FREESTANDING)
inline constexpr size_t OB_HASH_BUCKETS = 37;
inline std::recursive_mutex g_ObjectNamespaceMutex;

inline uint32_t ObpHashObjectName(std::wstring_view name) noexcept {
    uint32_t hash = 0;
    for (wchar_t ch : name) {
        wchar_t upper = (ch >= L'a' && ch <= L'z') ? static_cast<wchar_t>(ch - 32) : ch;
        hash = (hash * 37) + static_cast<uint32_t>(upper);
    }
    return hash % OB_HASH_BUCKETS;
}

/**
 * @brief Directory Object representing folders in the NT Object Namespace.
 * Implements Dave Cutler's 37-bucket hash chain directory structure.
 */
class DirectoryObject {
public:
    explicit DirectoryObject(std::wstring name) : name_(std::move(name)) {}

    NtStatus insertObject(std::wstring_view name, ObjectHeader* object) {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        if (!object) return NtStatus::InvalidParameter;
        std::wstring key(name);

        uint32_t bucket = ObpHashObjectName(key);
        for (auto* existing : buckets_[bucket]) {
            if (equalCaseInsensitive(existing->objectName, key)) {
                return NtStatus::ObjectNameCollision;
            }
        }

        object->addReference();
        object->objectName = key;
        object->nameInfo.name = key;
        object->parentDirectory = this;
        object->nameInfo.directory = this;
        buckets_[bucket].push_back(object);
        allEntries_[key] = object;

        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalDirectoryInserts++;
        }

        return NtStatus::Success;
    }

    bool removeObject(std::wstring_view name) {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        std::wstring key(name);
        uint32_t bucket = ObpHashObjectName(key);
        auto& chain = buckets_[bucket];
        for (auto it = chain.begin(); it != chain.end(); ++it) {
            if (equalCaseInsensitive((*it)->objectName, key)) {
                (*it)->parentDirectory = nullptr;
                (*it)->nameInfo.directory = nullptr;
                (*it)->releaseReference();
                chain.erase(it);
                allEntries_.erase(key);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] ObjectHeader* lookup(std::wstring_view name) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalDirectoryLookups++;
        }
        uint32_t bucket = ObpHashObjectName(name);
        for (auto* obj : buckets_[bucket]) {
            if (equalCaseInsensitive(obj->objectName, name)) {
                return obj;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::wstring& getName() const noexcept { return name_; }
    [[nodiscard]] size_t getEntryCount() const noexcept { return allEntries_.size(); }
    [[nodiscard]] const std::unordered_map<std::wstring, ObjectHeader*>& getEntries() const noexcept {
        return allEntries_;
    }

private:
    static bool equalCaseInsensitive(std::wstring_view s1, std::wstring_view s2) noexcept {
        if (s1.size() != s2.size()) return false;
        for (size_t i = 0; i < s1.size(); ++i) {
            wchar_t c1 = (s1[i] >= L'a' && s1[i] <= L'z') ? static_cast<wchar_t>(s1[i] - 32) : s1[i];
            wchar_t c2 = (s2[i] >= L'a' && s2[i] <= L'z') ? static_cast<wchar_t>(s2[i] - 32) : s2[i];
            if (c1 != c2) return false;
        }
        return true;
    }

    std::wstring name_;
    std::array<std::vector<ObjectHeader*>, OB_HASH_BUCKETS> buckets_;
    std::unordered_map<std::wstring, ObjectHeader*> allEntries_;
};
#endif

// ============================================================================
// 6. Multi-Level Handle Table Subsystem (EX_HANDLE_TABLE)
// ============================================================================
struct HandleTableEntry {
    ObjectHeader* object{nullptr};
    uint32_t grantedAccess{0};
    uint32_t handleAttributes{0};

    [[nodiscard]] bool isValid() const noexcept {
        return object != nullptr;
    }
};

inline void ObDereferenceObject(ObjectHeader* object);
class HandleTable;
inline NtStatus ObCloseHandle(Handle handle, HandleTable& table);
inline NtStatus ObFinishClose(ObjectHeader* object, Handle handle);

/**
 * @brief Per-Process Handle Table.
 * Maps opaque userland `Handle` (values 4, 8, 12...) to ObjectHeader pointers with access control.
 */
namespace detail {
// The native dispatcher is currently single-BSP. Exclude preemption while a
// table entry and its owning reference are changed as one transaction.
#if !defined(MICANT_FREESTANDING)
inline std::recursive_mutex g_HandleTableMutex;
#endif
class HandleTableGuard {
public:
    HandleTableGuard() noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags_) : : "memory");
#elif !defined(MICANT_FREESTANDING)
        g_HandleTableMutex.lock();
#endif
    }
    ~HandleTableGuard() {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        if (flags_ & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#elif !defined(MICANT_FREESTANDING)
        g_HandleTableMutex.unlock();
#endif
    }
    HandleTableGuard(const HandleTableGuard&) = delete;
    HandleTableGuard& operator=(const HandleTableGuard&) = delete;
private:
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags_{};
#endif
};
}

class HandleTable {
public:
    static constexpr size_t MaxHandles = 4096;

    HandleTable() {
#if !defined(MICANT_FREESTANDING)
        table_.resize(MaxHandles);
#endif
    }

    NtStatus createHandle(
        ObjectHeader* object,
        Handle& outHandle,
        uint32_t grantedAccess = STANDARD_RIGHTS_ALL,
        uint32_t attributes = 0
    ) {
        outHandle = 0;
        if (!object || (attributes & ~OB_VALID_HANDLE_ATTRIBUTES)) return NtStatus::InvalidParameter;
        if (object->type && (grantedAccess & ~object->type->validAccessMask))
            return NtStatus::AccessDenied;
        detail::HandleTableGuard guard;
        if (closing_) return NtStatus::ProcessIsTerminating;

        // NT Handles start at 4 and increment by 4
        for (size_t i = 1; i < table_.size(); ++i) {
            if (!table_[i].isValid()) {
                object->addReference();
                object->handleCount.fetch_add(1, std::memory_order_relaxed);
                object->handleInfo.handleCount = static_cast<uint32_t>(object->handleCount.load(std::memory_order_relaxed));
                if (object->type) {
                    const_cast<ObjectType*>(object->type)->totalNumberOfHandles.fetch_add(1, std::memory_order_relaxed);
                }

                table_[i].object = object;
                table_[i].grantedAccess = grantedAccess;
                table_[i].handleAttributes = attributes;

                outHandle = static_cast<Handle>(i * 4);
                return NtStatus::Success;
            }
        }
        return NtStatus::NoMemory;
    }

    // Borrowed view: caller must hold BSP exclusion and the owning table
    // must remain alive. Use referenceHandle when the result may outlive it.
    [[nodiscard]] ObjectHeader* lookup(Handle handle, uint32_t* outGrantedAccess = nullptr) const {
        size_t index = static_cast<size_t>(handle) / 4;
        if (index == 0 || index >= table_.size() || !table_[index].isValid()) {
            return nullptr;
        }
        if (outGrantedAccess) {
            *outGrantedAccess = table_[index].grantedAccess;
        }
        return table_[index].object;
    }

    // Borrowed entry; caller must serialize against table mutation.
    [[nodiscard]] const HandleTableEntry* getEntry(Handle handle) const {
        size_t index = static_cast<size_t>(handle) / 4;
        if (index == 0 || index >= table_.size() || !table_[index].isValid()) {
            return nullptr;
        }
        return &table_[index];
    }

    NtStatus setHandleAttributes(Handle handle, uint32_t attributes) noexcept {
        if (attributes & ~OB_VALID_HANDLE_ATTRIBUTES) return NtStatus::InvalidParameter;
        detail::HandleTableGuard guard;
        const size_t index = static_cast<size_t>(handle) / 4;
        if (index == 0 || index >= table_.size() || !table_[index].isValid()) return NtStatus::InvalidHandle;
        table_[index].handleAttributes = attributes;
        return NtStatus::Success;
    }

    // Return a retained object acquired atomically with the table lookup.
    NtStatus referenceHandle(Handle handle, uint32_t desiredAccess, const ObjectType* expectedType,
                             ObjectHeader** outObject, uint32_t* outGrantedAccess = nullptr) const {
        if (!outObject) return NtStatus::InvalidParameter;
        *outObject = nullptr;
        detail::HandleTableGuard guard;
        uint32_t granted{};
        auto* object = lookup(handle, &granted);
        if (!object) return NtStatus::InvalidHandle;
        if (object->type) desiredAccess = MapGenericAccess(desiredAccess, object->type->genericMapping);
        if ((desiredAccess & MAXIMUM_ALLOWED) ||
            (desiredAccess && (granted & desiredAccess) != desiredAccess)) return NtStatus::AccessDenied;
        if (expectedType && object->type != expectedType) return NtStatus::ObjectTypeMismatch;
        object->addReference();
        if (outGrantedAccess) *outGrantedAccess = granted;
        *outObject = object;
        return NtStatus::Success;
    }

    // Remove an entry while retaining its former handle reference. The caller
    // must dereference after callbacks and after leaving the table exclusion.
    [[nodiscard]] ObjectHeader* takeHandle(Handle handle) noexcept {
        detail::HandleTableGuard guard;
        return takeHandleLocked(handle);
    }

    NtStatus closeHandle(Handle handle) {
        ObjectHeader* object{};
        {
            detail::HandleTableGuard guard;
            const size_t index = static_cast<size_t>(handle) / 4;
            if (index == 0 || index >= table_.size() || !table_[index].isValid())
                return NtStatus::InvalidHandle;
            if (table_[index].handleAttributes & OBJ_PROTECT_CLOSE)
                return NtStatus::HandleNotClosable;
            object = takeHandleLocked(handle);
        }
        return ObFinishClose(object, handle);
    }

    NtStatus duplicateHandle(
        Handle sourceHandle, HandleTable& targetTable, Handle& targetHandle,
        uint32_t desiredAccess, bool inherit = false, uint32_t options = 0,
        uint32_t targetAttributes = UINT32_MAX
    ) {
        targetHandle = 0;
        if (!(options & 4) && targetAttributes != UINT32_MAX &&
            (targetAttributes & ~OB_VALID_HANDLE_ATTRIBUTES))
            return NtStatus::InvalidParameter;
        ObjectHeader* removed{};
        ObjectHeader* duplicated{};
        uint32_t duplicatedAccess{};
        if (options & ~uint32_t{7}) {
            // DUPLICATE_CLOSE_SOURCE applies even when duplication fails.
            if (options & 1) {
                detail::HandleTableGuard guard;
                removed = takeHandleLocked(sourceHandle);
            }
            if (removed) (void)ObFinishClose(removed, sourceHandle);
            return NtStatus::InvalidParameter;
        }
        NtStatus status = NtStatus::InvalidHandle;
        {
            detail::HandleTableGuard guard;
            uint32_t granted{};
            auto* object = lookup(sourceHandle, &granted);
            if (object) {
                const auto* sourceEntry = getEntry(sourceHandle);
                if (!sourceEntry) __builtin_trap();
                // DUPLICATE_SAME_ACCESS makes DesiredAccess informationally
                // irrelevant: preserve the source grant exactly, including
                // when the caller supplied bits that are not valid for this
                // object type. Otherwise map generic rights before validating
                // the requested subset against the source grant.
                const bool sameAccess = (options & 2) != 0;
                const uint32_t requested = sameAccess ? 0 :
                    (object->type ? MapGenericAccess(desiredAccess, object->type->genericMapping)
                                  : desiredAccess);
                const bool maximumAllowed = (requested & MAXIMUM_ALLOWED) != 0;
                const uint32_t required = requested & ~MAXIMUM_ALLOWED;
                const uint32_t access = sameAccess ? granted : maximumAllowed ? granted : requested;
                const uint32_t attributes = (options & 4) ? sourceEntry->handleAttributes :
                    (targetAttributes != UINT32_MAX ? targetAttributes : (inherit ? OBJ_INHERIT : 0));
                if (!sameAccess && ((required & ~granted) != 0 || (access & ~granted) != 0))
                    status = NtStatus::AccessDenied;
                else {
                    status = targetTable.createHandle(object, targetHandle, access, attributes);
                    if (status == NtStatus::Success) {
                        duplicated = object;
                        duplicatedAccess = access;
                    }
                }
                if (options & 1) removed = takeHandleLocked(sourceHandle);
            }
        }
        if (status == NtStatus::Success && duplicated && duplicated->type &&
            duplicated->type->openProcedure) {
            status = duplicated->type->openProcedure(duplicated, duplicatedAccess);
            if (status != NtStatus::Success) {
                auto* failed = targetTable.takeHandle(targetHandle);
                if (!failed) __builtin_trap();
                const auto closeStatus = ObFinishClose(failed, targetHandle);
                if (closeStatus != NtStatus::Success) __builtin_trap();
                targetHandle = 0;
            }
        }
        if (removed) (void)ObFinishClose(removed, sourceHandle);
        return status;
    }

    // Internal convenience: callers omitting an access mask explicitly ask
    // for a same-access duplicate. An explicit zero mask grants no rights.
    NtStatus duplicateHandle(Handle sourceHandle, HandleTable& targetTable,
                             Handle& targetHandle) {
        return duplicateHandle(sourceHandle, targetTable, targetHandle, 0, false, 2);
    }

    [[nodiscard]] size_t getActiveHandleCount() const noexcept {
        detail::HandleTableGuard guard;
        size_t count = 0;
        for (size_t i = 1; i < table_.size(); ++i) {
            if (table_[i].isValid()) ++count;
        }
        return count;
    }

    // Preserve handle values in a newly created process. Only inheritable
    // entries are copied, and each copied entry owns its own Ob reference.
    // The target table must remain unpublished until this transaction returns.
    NtStatus inheritHandlesFrom(const HandleTable& source) noexcept {
        if (&source == this) return NtStatus::InvalidParameter;
        {
            detail::HandleTableGuard guard;
            if (closing_ || source.closing_ || table_.size() != source.table_.size())
                return NtStatus::InvalidParameter;
            for (size_t i = 1; i < table_.size(); ++i)
                if (table_[i].isValid()) return NtStatus::InvalidParameter;
            for (size_t i = 1; i < table_.size(); ++i) {
                const auto& entry = source.table_[i];
                if (!entry.isValid() || !(entry.handleAttributes & OBJ_INHERIT)) continue;
                auto* object = entry.object;
                object->addReference();
                object->handleCount.fetch_add(1, std::memory_order_relaxed);
                object->handleInfo.handleCount = static_cast<uint32_t>(object->handleCount.load(std::memory_order_relaxed));
                if (object->type)
                    const_cast<ObjectType*>(object->type)->totalNumberOfHandles.fetch_add(1, std::memory_order_relaxed);
                table_[i] = entry;
            }
        }

        for (size_t i = 1; i < table_.size(); ++i) {
            const Handle handle = static_cast<Handle>(i * 4);
            const auto* entry = getEntry(handle);
            if (!entry || !entry->object->type || !entry->object->type->openProcedure) continue;
            const NtStatus status = entry->object->type->openProcedure(entry->object, entry->grantedAccess);
            if (NT_SUCCESS(status)) continue;
            for (size_t rollback = 1; rollback < table_.size(); ++rollback) {
                const Handle rollbackHandle = static_cast<Handle>(rollback * 4);
                if (auto* object = takeHandle(rollbackHandle))
                    (void)ObFinishClose(object, rollbackHandle);
            }
            return status;
        }
        return NtStatus::Success;
    }

    void closeAllHandles() {
        {
            detail::HandleTableGuard guard;
            closing_ = true;
        }
        for (size_t i = 1; i < table_.size(); ++i) {
            ObjectHeader* object{};
            const Handle handle = static_cast<Handle>(i * 4);
            {
                detail::HandleTableGuard guard;
                object = takeHandleLocked(handle);
            }
            if (object) (void)ObFinishClose(object, handle);
        }
    }

private:
    bool closing_{false};
    ObjectHeader* takeHandleLocked(Handle handle) noexcept {
        const size_t index = static_cast<size_t>(handle) / 4;
        if (index == 0 || index >= table_.size() || !table_[index].isValid()) return nullptr;
        auto* object = table_[index].object;
        table_[index] = HandleTableEntry{};
        object->handleCount.fetch_sub(1, std::memory_order_relaxed);
        object->handleInfo.handleCount = static_cast<uint32_t>(object->handleCount.load(std::memory_order_relaxed));
        if (object->type)
            const_cast<ObjectType*>(object->type)->totalNumberOfHandles.fetch_sub(1, std::memory_order_relaxed);
        return object;
    }
#if defined(MICANT_FREESTANDING)
    std::array<HandleTableEntry, MaxHandles> table_{};
#else
    std::vector<HandleTableEntry> table_;
#endif
};

namespace detail {
// Borrow the canonical kernel table; process tables remain owned by Ps.
HandleTable& kernelHandleTable() noexcept;
#if defined(MICANT_FREESTANDING)
HandleTable& currentHandleTable() noexcept;
// Kernel handles use an internal tag and are unavailable to user-mode calls.
[[nodiscard]] Handle encodeKernelHandle(Handle handle) noexcept;
NtStatus resolveHandle(Handle handle, HandleTable*& table, Handle& untagged,
                       bool kernelAccess = false) noexcept;
#endif
}

// ============================================================================
// 7. Symbolic Link Object
// ============================================================================
#if !defined(MICANT_FREESTANDING)
class SymbolicLinkObject {
public:
    explicit SymbolicLinkObject(std::wstring targetPath) : targetPath_(std::move(targetPath)) {}
    [[nodiscard]] const std::wstring& getTargetPath() const noexcept { return targetPath_; }
    void setTargetPath(std::wstring targetPath) noexcept { targetPath_ = std::move(targetPath); }

private:
    std::wstring targetPath_;
};

// ============================================================================
// 8. Global Object Namespace & Hierarchical Resolver (ObpLookupObjectName)
// ============================================================================
class ObjectNamespace {
public:
    static ObjectNamespace& get() {
        static ObjectNamespace instance;
        return instance;
    }

    void initializeRoot() {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        if (initialized_) return;
        initialized_ = true;

        directories_[L"\\"] = std::make_shared<DirectoryObject>(L"\\");
        createDirectory(L"\\Device");
        createDirectory(L"\\Driver");
        createDirectory(L"\\DosDevices");
        createDirectory(L"\\KernelObjects");
        createDirectory(L"\\BaseNamedObjects");
        createDirectory(L"\\RPC Control");
        createDirectory(L"\\Sessions");
        createDirectory(L"\\ObjectTypes");

        // Register default NT Object Types in \ObjectTypes
        registerType(L"Type", ObjectTypeId::Type);
        registerType(L"Directory", ObjectTypeId::Directory);
        registerType(L"SymbolicLink", ObjectTypeId::SymbolicLink);
        registerType(L"Device", ObjectTypeId::Device);
        registerType(L"Driver", ObjectTypeId::Driver);
        registerType(L"Process", ObjectTypeId::Process);
        registerType(L"Thread", ObjectTypeId::Thread);
        registerType(L"Section", ObjectTypeId::Section);
        registerType(L"File", ObjectTypeId::File);
        registerType(L"Event", ObjectTypeId::Event);
        registerType(L"Mutant", ObjectTypeId::Mutant);
        registerType(L"Semaphore", ObjectTypeId::Semaphore);
        registerType(L"Key", ObjectTypeId::Key);
        registerType(L"IoCompletion", ObjectTypeId::IoCompletion);
        registerType(L"Job", ObjectTypeId::Job);
        registerType(L"Timer", ObjectTypeId::Timer);

        // Canonical Windows NT Symbolic Links
        createSymbolicLink(L"\\DosDevices\\NUL", L"\\Device\\Null");
        createSymbolicLink(L"\\DosDevices\\CON", L"\\Device\\Console");
        createSymbolicLink(L"\\DosDevices\\PIPE", L"\\Device\\NamedPipe");
        createSymbolicLink(L"\\DosDevices\\MAILSLOT", L"\\Device\\Mailslot");
        createSymbolicLink(L"\\DosDevices\\PhysicalDrive0", L"\\Device\\Harddisk0");
        createSymbolicLink(L"\\Device\\HarddiskVolume1", L"\\Device\\Harddisk0\\Partition1");
        createSymbolicLink(L"\\??", L"\\DosDevices");
        createSymbolicLink(L"\\GLOBAL??", L"\\DosDevices");
    }

    bool registerType(std::wstring_view typeName, ObjectTypeId id) {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        std::wstring name(typeName);
        if (types_.contains(name)) return true;

        auto type = std::make_unique<ObjectType>();
        type->typeName = name;
        type->typeId = id;
        types_[name] = std::move(type);
        return true;
    }

    [[nodiscard]] const ObjectType* getType(std::wstring_view typeName) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        auto it = types_.find(std::wstring(typeName));
        if (it != types_.end()) return it->second.get();
        return nullptr;
    }

    bool createDirectory(std::wstring_view fullPath) {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        std::wstring path(fullPath);
        if (directories_.contains(path)) return true;

        // Ensure parent directory exists
        size_t lastSlash = path.rfind(L'\\');
        if (lastSlash != std::wstring::npos && lastSlash > 0) {
            std::wstring parent = path.substr(0, lastSlash);
            createDirectory(parent);
        }

        directories_[path] = std::make_shared<DirectoryObject>(path);
        return true;
    }

    bool createSymbolicLink(std::wstring_view linkPath, std::wstring_view targetPath) {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        symbolicLinks_[std::wstring(linkPath)] = std::make_shared<SymbolicLinkObject>(std::wstring(targetPath));
        return true;
    }

    bool removeSymbolicLink(std::wstring_view linkPath) {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        return symbolicLinks_.erase(std::wstring(linkPath)) > 0;
    }

    [[nodiscard]] std::shared_ptr<SymbolicLinkObject> getSymbolicLink(std::wstring_view linkPath) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        auto it = symbolicLinks_.find(std::wstring(linkPath));
        if (it != symbolicLinks_.end()) return it->second;
        return nullptr;
    }

    /**
     * @brief Normalizes NT device prefixes (\??\, \GLOBAL??\, and \\.\ -> \DosDevices\).
     */
    [[nodiscard]] std::wstring normalizePrefix(std::wstring_view path) const {
        std::wstring current(path);
        if (current.starts_with(L"\\??\\")) {
            current = L"\\DosDevices\\" + current.substr(4);
        } else if (current.starts_with(L"\\GLOBAL??\\")) {
            current = L"\\DosDevices\\" + current.substr(10);
        } else if (current.starts_with(L"\\\\.\\")) {
            current = L"\\DosDevices\\" + current.substr(4);
        }
        return current;
    }

    /**
     * @brief Resolves symbolic links recursively with loop detection and depth bounds.
     */
    [[nodiscard]] std::wstring resolvePath(std::wstring_view path, NtStatus* outStatus = nullptr) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        std::wstring current = normalizePrefix(path);
        std::vector<std::wstring> visited;
        visited.push_back(current);

        for (size_t hop = 0; hop < OB_MAX_SYMLINK_RECURSION; ++hop) {
            // 1. Exact match
            auto it = symbolicLinks_.find(current);
            if (it != symbolicLinks_.end()) {
                std::wstring next = it->second->getTargetPath();
                {
                    std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
                    g_ObTelemetry.totalSymlinksResolved++;
                }
                if (std::find(visited.begin(), visited.end(), next) != visited.end()) {
                    if (outStatus) *outStatus = NtStatus::CircularReference;
                    std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
                    g_ObTelemetry.totalSymlinkRecursionsHit++;
                    return next;
                }
                visited.push_back(next);
                current = next;
                continue;
            }

            // 2. Longest prefix match
            bool matched = false;
            size_t longestMatch = 0;
            std::wstring bestReplacement;

            for (const auto& [link, target] : symbolicLinks_) {
                if (current.starts_with(link) && link.length() > longestMatch) {
                    if (current.length() == link.length() || current[link.length()] == L'\\') {
                        longestMatch = link.length();
                        bestReplacement = target->getTargetPath() + current.substr(link.length());
                        matched = true;
                    }
                }
            }

            if (matched) {
                {
                    std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
                    g_ObTelemetry.totalSymlinksResolved++;
                }
                if (std::find(visited.begin(), visited.end(), bestReplacement) != visited.end()) {
                    if (outStatus) *outStatus = NtStatus::CircularReference;
                    std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
                    g_ObTelemetry.totalSymlinkRecursionsHit++;
                    return bestReplacement;
                }
                visited.push_back(bestReplacement);
                current = bestReplacement;
                continue;
            }

            break; // No more symbolic links to resolve
        }

        if (visited.size() > OB_MAX_SYMLINK_RECURSION) {
            if (outStatus) *outStatus = NtStatus::LinkEvaluationCapExceeded;
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalSymlinkRecursionsHit++;
        } else if (outStatus) {
            *outStatus = NtStatus::Success;
        }

        return current;
    }

    [[nodiscard]] std::shared_ptr<DirectoryObject> getDirectory(std::wstring_view path) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        auto it = directories_.find(std::wstring(path));
        if (it != directories_.end()) return it->second;
        return nullptr;
    }

    /**
     * @brief Hierarchical object lookup across directories (ObpLookupObjectName) with parse procedure support.
     */
    [[nodiscard]] ObjectHeader* lookupObject(
        std::wstring_view fullPath,
        const ObjectType* expectedType = nullptr,
        NtStatus* outStatus = nullptr
    ) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        NtStatus st = NtStatus::Success;
        std::wstring resolved = resolvePath(fullPath, &st);
        if (outStatus) *outStatus = st;
        if (!NT_SUCCESS(st)) return nullptr;

        size_t lastSlash = resolved.rfind(L'\\');
        if (lastSlash == std::wstring::npos) return nullptr;

        std::wstring dirPath = (lastSlash == 0) ? L"\\" : resolved.substr(0, lastSlash);
        std::wstring objName = resolved.substr(lastSlash + 1);

        auto dir = getDirectory(dirPath);
        if (dir) {
            auto* obj = dir->lookup(objName);
            if (obj) return obj;
        }

        // Prefix parseProcedure traversal
        size_t pos = 1;
        while (pos < resolved.length()) {
            size_t nextSlash = resolved.find(L'\\', pos);
            std::wstring prefix = (nextSlash == std::wstring::npos) ? resolved : resolved.substr(0, nextSlash);
            size_t pSlash = prefix.rfind(L'\\');
            std::wstring pDir = (pSlash == 0) ? L"\\" : prefix.substr(0, pSlash);
            std::wstring pName = prefix.substr(pSlash + 1);

            auto pDirObj = getDirectory(pDir);
            if (pDirObj) {
                auto* intermediate = pDirObj->lookup(pName);
                if (intermediate && intermediate->type && intermediate->type->parseProcedure) {
                    std::wstring_view remaining = (nextSlash == std::wstring::npos) 
                        ? std::wstring_view{} 
                        : std::wstring_view(resolved).substr(nextSlash);
                    ObjectHeader* foundFromParse = nullptr;
                    {
                        std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
                        g_ObTelemetry.totalParseProcedureCalls++;
                    }
                    NtStatus parseSt = intermediate->type->parseProcedure(
                        intermediate, expectedType, remaining, &foundFromParse
                    );
                    if (outStatus) *outStatus = parseSt;
                    if (NT_SUCCESS(parseSt) && foundFromParse) {
                        return foundFromParse;
                    }
                }
            }

            if (nextSlash == std::wstring::npos) break;
            pos = nextSlash + 1;
        }

        return nullptr;
    }

    [[nodiscard]] ObjectHeader* lookupObjectReference(
        std::wstring_view fullPath,
        const ObjectType* expectedType = nullptr,
        NtStatus* outStatus = nullptr
    ) const {
        std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
        NtStatus status = NtStatus::Success;
        auto* object = lookupObject(fullPath, expectedType, &status);
        if (outStatus) *outStatus = status;
        if (!object) return nullptr;
        if (expectedType && object->type != expectedType) {
            if (outStatus) *outStatus = NtStatus::ObjectTypeMismatch;
            return nullptr;
        }
        if (!object->tryAddReference()) {
            if (outStatus) *outStatus = NtStatus::ObjectNameNotFound;
            return nullptr;
        }
        return object;
    }

    [[nodiscard]] size_t getDirectoryCount() const noexcept { return directories_.size(); }
    [[nodiscard]] size_t getSymbolicLinkCount() const noexcept { return symbolicLinks_.size(); }
    [[nodiscard]] size_t getTypeCount() const noexcept { return types_.size(); }

private:
    ObjectNamespace() = default;
    bool initialized_{false};
    std::unordered_map<std::wstring, std::unique_ptr<ObjectType>> types_;
    std::unordered_map<std::wstring, std::shared_ptr<DirectoryObject>> directories_;
    std::unordered_map<std::wstring, std::shared_ptr<SymbolicLinkObject>> symbolicLinks_;
};
#endif

// ============================================================================
// 9. Sovereign Object Reference & Lifetime Management API (Ob*)
// ============================================================================

/**
 * @brief Canonical Object Creation (ObCreateObject).
 */
inline NtStatus ObCreateObject(
    KPROCESSOR_MODE /*accessMode*/,
    const ObjectType* objectType,
    const ObjectAttributes* objectAttributes,
    size_t bodySize,
    void** outObjectBody,
    ObjectHeader** outHeader = nullptr
) {
    if (!objectType || !outObjectBody) return NtStatus::InvalidParameter;

    if (bodySize > std::numeric_limits<size_t>::max() - sizeof(ObjectHeader))
        return NtStatus::InsufficientResources;
    size_t totalSize = sizeof(ObjectHeader) + bodySize;
    uint8_t* memory = new (std::nothrow_t{}) uint8_t[totalSize]();
    if (!memory) return NtStatus::InsufficientResources;

    auto* header = new (memory) ObjectHeader{};
    header->pointerCount.store(1, std::memory_order_relaxed);
    header->handleCount.store(0, std::memory_order_relaxed);
    header->type = objectType;
    header->dynamicallyAllocated = true;

#if defined(MICANT_FREESTANDING)
    if (objectAttributes) {
        header->~ObjectHeader();
        delete[] memory;
        return NtStatus::NotImplemented;
    }
#else
    if (objectAttributes) {
        header->attributes = objectAttributes->attributes;
        if (objectAttributes->objectName) {
            header->objectName = std::wstring(objectAttributes->objectName->view());
            header->nameInfo.name = header->objectName;
        }
        header->securityDescriptor = objectAttributes->securityDescriptor;
    }
#endif

#if !defined(MICANT_FREESTANDING)
    header->creatorInfo.createTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
#endif
    header->creatorInfo.creatorProcessId = 4; // System process
    header->creatorInfo.creatorThreadId = 1;

    header->quotaInfo.pagedPoolCharge = static_cast<uint32_t>(bodySize);
    header->quotaInfo.nonPagedPoolCharge = static_cast<uint32_t>(sizeof(ObjectHeader));

    const_cast<ObjectType*>(objectType)->totalNumberOfObjects.fetch_add(1, std::memory_order_relaxed);

#if !defined(MICANT_FREESTANDING)
    {
        std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
        g_ObTelemetry.totalObjectsCreated++;
    }
#endif

    *outObjectBody = header->getBody();
    if (outHeader) *outHeader = header;
    return NtStatus::Success;
}

/**
 * @brief Canonical Object Insertion into Directory & Handle Table (ObInsertObject).
 */
inline NtStatus ObInsertObject(
    ObjectHeader* object,
    const ObjectAttributes* objectAttributes,
    uint32_t desiredAccess,
    HandleTable* handleTable,
    Handle* outHandle
) {
    if (!object || ((handleTable == nullptr) != (outHandle == nullptr))) return NtStatus::InvalidParameter;
    if (outHandle) *outHandle = 0;
    if (objectAttributes && (objectAttributes->length != sizeof(ObjectAttributes) ||
        (objectAttributes->attributes & ~OBJ_VALID_ATTRIBUTES))) return NtStatus::InvalidParameter;

    uint32_t grantedAccess = object->type
        ? MapGenericAccess(desiredAccess, object->type->genericMapping) : desiredAccess;
    const uint32_t validAccessMask = object->type ? object->type->validAccessMask :
        std::numeric_limits<uint32_t>::max();
    if (grantedAccess & ~validAccessMask) return NtStatus::AccessDenied;

    // Security access check if OBJ_FORCE_ACCESS_CHECK
    if (objectAttributes && (objectAttributes->attributes & OBJ_FORCE_ACCESS_CHECK)) {
        if (!object->securityDescriptor || !g_ObpAccessCheckProcedure) return NtStatus::NotSupported;
        if (g_ObpAccessCheckProcedure) {
            uint32_t granted = 0;
            NtStatus chkSt = g_ObpAccessCheckProcedure(object->securityDescriptor, grantedAccess, &granted);
            if (!NT_SUCCESS(chkSt)) {
#if !defined(MICANT_FREESTANDING)
                std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
                g_ObTelemetry.totalSecurityChecksDenied++;
#endif
                return NtStatus::AccessDenied;
            }
            if ((grantedAccess & ~granted) || (granted & ~validAccessMask))
                return NtStatus::AccessDenied;
            grantedAccess = granted;
        }
#if !defined(MICANT_FREESTANDING)
        std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
        g_ObTelemetry.totalSecurityChecksPassed++;
#endif
    }

#if defined(MICANT_FREESTANDING)
    if (objectAttributes) return NtStatus::NotSupported;
#else
    std::unique_lock<std::recursive_mutex> namespaceLock(g_ObjectNamespaceMutex);
    bool insertedIntoNamespace = false;
    // Resolve directory and insert
    if (!object->objectName.empty()) {
        std::wstring fullPath = object->objectName;
        size_t lastSlash = fullPath.rfind(L'\\');
        std::wstring dirPath = (lastSlash == std::wstring::npos || lastSlash == 0) 
            ? L"\\" 
            : fullPath.substr(0, lastSlash);
        std::wstring name = (lastSlash == std::wstring::npos) 
            ? fullPath 
            : fullPath.substr(lastSlash + 1);

        auto dir = ObjectNamespace::get().getDirectory(dirPath);
        if (!dir) {
            ObjectNamespace::get().createDirectory(dirPath);
            dir = ObjectNamespace::get().getDirectory(dirPath);
        }

        if (dir) {
            NtStatus ins = dir->insertObject(name, object);
            if (!NT_SUCCESS(ins)) return ins;
            object->nameInfo.directory = dir.get();
            insertedIntoNamespace = true;
        }
    }
#endif

    // If handle requested, allocate it only after the namespace insertion has
    // succeeded. Any subsequent failure rolls both publications back.
    if (handleTable && outHandle) {
        const uint32_t handleAttributes = objectAttributes
            ? (objectAttributes->attributes & OBJ_INHERIT) : 0;
        NtStatus hst = handleTable->createHandle(object, *outHandle, grantedAccess, handleAttributes);
        if (!NT_SUCCESS(hst)) {
#if !defined(MICANT_FREESTANDING)
            if (insertedIntoNamespace)
                reinterpret_cast<DirectoryObject*>(object->parentDirectory)->removeObject(object->objectName);
#endif
            return hst;
        }

        if (object->type && object->type->openProcedure) {
#if !defined(MICANT_FREESTANDING)
            namespaceLock.unlock();
#endif
            hst = object->type->openProcedure(object, grantedAccess);
            if (!NT_SUCCESS(hst)) {
                // Rollback is an internal retirement, so OBJ_PROTECT_CLOSE
                // must not turn a failed open callback into a leaked handle.
                auto* failed = handleTable->takeHandle(*outHandle);
                if (!failed) __builtin_trap();
                const auto closeStatus = ObFinishClose(failed, *outHandle);
                if (!NT_SUCCESS(closeStatus)) __builtin_trap();
                *outHandle = 0;
#if !defined(MICANT_FREESTANDING)
                if (insertedIntoNamespace && object->parentDirectory)
                    reinterpret_cast<DirectoryObject*>(object->parentDirectory)->removeObject(object->objectName);
#endif
                return hst;
            }
        }
#if !defined(MICANT_FREESTANDING)
        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalHandlesOpened++;
        }
#endif
    }

    return NtStatus::Success;
}

/**
 * @brief Canonical Object Lookup by Name (ObReferenceObjectByName).
 */
inline NtStatus ObReferenceObjectByName(
    std::wstring_view fullPath,
    uint32_t /*attributes*/,
    const ObjectType* expectedType,
    KPROCESSOR_MODE /*accessMode*/,
    ObjectHeader** outObject
) {
#if defined(MICANT_FREESTANDING)
    (void)fullPath; (void)expectedType; (void)outObject;
    return NtStatus::NotSupported;
#else
    if (!outObject) return NtStatus::InvalidParameter;
    *outObject = nullptr;

    NtStatus st = NtStatus::Success;
    ObjectHeader* obj = ObjectNamespace::get().lookupObjectReference(fullPath, expectedType, &st);
    if (!obj) {
        return NT_SUCCESS(st) ? NtStatus::ObjectNameNotFound : st;
    }

    if (expectedType && obj->type != expectedType) {
        return NtStatus::ObjectTypeMismatch;
    }

    *outObject = obj;
    return NtStatus::Success;
#endif
}

/**
 * @brief Canonical Open Object by Name (ObOpenObjectByName).
 */
inline NtStatus ObOpenObjectByName(
    const ObjectAttributes* objectAttributes,
    const ObjectType* expectedType,
    KPROCESSOR_MODE accessMode,
    uint32_t desiredAccess,
    HandleTable& handleTable,
    Handle& outHandle
) {
#if defined(MICANT_FREESTANDING)
    (void)objectAttributes; (void)expectedType; (void)accessMode;
    (void)desiredAccess; (void)handleTable;
    outHandle = 0;
    return NtStatus::NotSupported;
#else
    outHandle = 0;
    if (!objectAttributes || objectAttributes->length != sizeof(ObjectAttributes) ||
        !objectAttributes->objectName || (objectAttributes->attributes & ~OBJ_VALID_ATTRIBUTES)) {
        return NtStatus::InvalidParameter;
    }
    if (accessMode != KernelMode && accessMode != UserMode) return NtStatus::InvalidParameter;
    if (accessMode == UserMode && (objectAttributes->attributes & OBJ_KERNEL_HANDLE))
        return NtStatus::InvalidParameter;
    if (objectAttributes->rootDirectory || objectAttributes->securityQualityOfService)
        return NtStatus::NotSupported;

    std::wstring path(objectAttributes->objectName->view());
    NtStatus lookupSt = NtStatus::Success;
    ObjectHeader* obj = ObjectNamespace::get().lookupObjectReference(path, expectedType, &lookupSt);
    if (!obj) {
        return NT_SUCCESS(lookupSt) ? NtStatus::ObjectNameNotFound : lookupSt;
    }
    struct LookupReference {
        ObjectHeader* object;
        ~LookupReference() { ObDereferenceObject(object); }
    } lookupReference{obj};

    if (expectedType && obj->type != expectedType) {
        return NtStatus::ObjectTypeMismatch;
    }

    const bool forceAccessCheck = (objectAttributes->attributes & OBJ_FORCE_ACCESS_CHECK) != 0;
    if (accessMode == UserMode && (!obj->securityDescriptor || !g_ObpAccessCheckProcedure))
        return NtStatus::AccessDenied;
    if (forceAccessCheck && (!obj->securityDescriptor || !g_ObpAccessCheckProcedure))
        return NtStatus::NotSupported;

    uint32_t requestedAccess = desiredAccess;
    uint32_t validAccessMask = std::numeric_limits<uint32_t>::max();
    if (obj->type) {
        requestedAccess = MapGenericAccess(desiredAccess, obj->type->genericMapping);
        validAccessMask = obj->type->validAccessMask;
    }
    const bool maximumAllowed = (requestedAccess & MAXIMUM_ALLOWED) != 0;
    const uint32_t requiredAccess = requestedAccess & ~MAXIMUM_ALLOWED;
    if (requiredAccess & ~validAccessMask) return NtStatus::AccessDenied;
    const bool hasAccessCheck = g_ObpAccessCheckProcedure && obj->securityDescriptor &&
        (accessMode == UserMode || forceAccessCheck);
    uint32_t grantedAccess = maximumAllowed && !hasAccessCheck
        ? validAccessMask : requestedAccess;
    // Access check
    if (hasAccessCheck) {
        uint32_t granted = 0;
        NtStatus chkSt = g_ObpAccessCheckProcedure(obj->securityDescriptor, requestedAccess, &granted);
        if (!NT_SUCCESS(chkSt)) {
#if !defined(MICANT_FREESTANDING)
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalSecurityChecksDenied++;
#endif
            return chkSt == NtStatus::AccessDenied ? chkSt : NtStatus::AccessDenied;
        }
        if ((requiredAccess & ~granted) != 0 || (granted & ~validAccessMask) != 0 ||
            (!maximumAllowed && (granted & ~requiredAccess) != 0)) return NtStatus::AccessDenied;
        grantedAccess = granted;
    }
#if !defined(MICANT_FREESTANDING)
    {
        std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
        g_ObTelemetry.totalSecurityChecksPassed++;
    }
#endif

    const uint32_t handleAttributes = objectAttributes->attributes & OBJ_INHERIT;
    NtStatus st = handleTable.createHandle(obj, outHandle, grantedAccess, handleAttributes);
    if (NT_SUCCESS(st)) {
        if (obj->type && obj->type->openProcedure) {
            st = obj->type->openProcedure(obj, grantedAccess);
            if (!NT_SUCCESS(st)) {
                if (auto* failedHandle = handleTable.takeHandle(outHandle))
                    (void)ObFinishClose(failedHandle, outHandle);
                outHandle = 0;
                return st;
            }
        }
#if !defined(MICANT_FREESTANDING)
        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalHandlesOpened++;
        }
#endif
    }
    return st;
#endif
}

/**
 * @brief Canonical Open Object by Pointer (ObOpenObjectByPointer).
 */
inline NtStatus ObOpenObjectByPointer(
    ObjectHeader* object,
    uint32_t handleAttributes,
    uint32_t desiredAccess,
    const ObjectType* expectedType,
    HandleTable& handleTable,
    Handle& outHandle
) {
    outHandle = 0;
    if (!object) return NtStatus::InvalidParameter;
    if (handleAttributes & ~OB_VALID_HANDLE_ATTRIBUTES) return NtStatus::InvalidParameter;
    if (expectedType && object->type != expectedType) {
        return NtStatus::ObjectTypeMismatch;
    }

    uint32_t requestedAccess = desiredAccess;
    uint32_t validAccessMask = std::numeric_limits<uint32_t>::max();
    if (object->type) {
        requestedAccess = MapGenericAccess(desiredAccess, object->type->genericMapping);
        validAccessMask = object->type->validAccessMask;
    }
    const bool maximumAllowed = (requestedAccess & MAXIMUM_ALLOWED) != 0;
    const uint32_t requiredAccess = requestedAccess & ~MAXIMUM_ALLOWED;
    if (requiredAccess & ~validAccessMask) return NtStatus::AccessDenied;
    if (object->securityDescriptor && !g_ObpAccessCheckProcedure) return NtStatus::AccessDenied;
    const bool hasAccessCheck = g_ObpAccessCheckProcedure && object->securityDescriptor;
    uint32_t grantedAccess = maximumAllowed && !hasAccessCheck
        ? validAccessMask : requestedAccess;
    if (g_ObpAccessCheckProcedure && object->securityDescriptor) {
        uint32_t granted{};
        const NtStatus accessStatus = g_ObpAccessCheckProcedure(object->securityDescriptor, requestedAccess, &granted);
        if (!NT_SUCCESS(accessStatus) || (requiredAccess & ~granted) || (granted & ~validAccessMask) ||
            (!maximumAllowed && (granted & ~requiredAccess))) return NtStatus::AccessDenied;
        grantedAccess = granted;
    }
    NtStatus st = handleTable.createHandle(object, outHandle, grantedAccess, handleAttributes);
    if (NT_SUCCESS(st)) {
        if (object->type && object->type->openProcedure) {
            st = object->type->openProcedure(object, grantedAccess);
            if (!NT_SUCCESS(st)) {
                if (auto* failedHandle = handleTable.takeHandle(outHandle))
                    (void)ObFinishClose(failedHandle, outHandle);
                outHandle = 0;
                return st;
            }
        }
#if !defined(MICANT_FREESTANDING)
        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalHandlesOpened++;
        }
#endif
    }
    return st;
}

/**
 * @brief Canonical Reference Object by Handle (ObReferenceObjectByHandle).
 */
inline NtStatus ObReferenceObjectByHandle(
    Handle handle,
    const HandleTable& handleTable,
    uint32_t desiredAccess,
    const ObjectType* expectedType,
    ObjectHeader** outObject
) {
    if (!outObject) return NtStatus::InvalidParameter;
    *outObject = nullptr;

    return handleTable.referenceHandle(handle, desiredAccess, expectedType, outObject);
}

/**
 * @brief Canonical Dereference Object (ObDereferenceObject).
 */
inline void ObFinishDelete(ObjectHeader* object) {
        if (object->type && object->type->deleteProcedure) {
            object->type->deleteProcedure(object);
        }
#if defined(MICANT_FREESTANDING)
        if (object->releaseSecurityDescriptor)
            object->releaseSecurityDescriptor(object->securityDescriptor);
        object->securityDescriptor = nullptr;
        object->releaseSecurityDescriptor = nullptr;
#endif
        if (object->type) {
            const_cast<ObjectType*>(object->type)->totalNumberOfObjects.fetch_sub(1, std::memory_order_relaxed);
        }
#if !defined(MICANT_FREESTANDING)
        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalObjectsDestroyed++;
        }
#endif
        if (object->dynamicallyAllocated) {
            object->~ObjectHeader();
            delete[] reinterpret_cast<uint8_t*>(object);
        }
}

inline void ObDereferenceObject(ObjectHeader* object) {
    if (!object || !object->releaseReference()) return;
#if defined(MICANT_FREESTANDING)
    if (object->type && object->type->deferDelete && object->type->deferDelete(object)) return;
#endif
    ObFinishDelete(object);
}

/**
 * @brief Canonical Close Handle (ObCloseHandle).
 */
inline NtStatus ObFinishClose(ObjectHeader* object, Handle handle) {
    if (!object) return NtStatus::InvalidHandle;
    if (object->type && object->type->closeProcedure) object->type->closeProcedure(object, handle);
#if !defined(MICANT_FREESTANDING)
    std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
    {
        std::lock_guard<std::mutex> lock(g_ObTelemetryMutex);
        g_ObTelemetry.totalHandlesClosed++;
    }
    if (object->handleCount.load(std::memory_order_relaxed) == 0 && !object->isPermanent() &&
        object->parentDirectory) {
        auto* directory = reinterpret_cast<DirectoryObject*>(object->parentDirectory);
        directory->removeObject(object->objectName);
        object->parentDirectory = nullptr;
        object->nameInfo.directory = nullptr;
        std::lock_guard<std::mutex> lock(g_ObTelemetryMutex);
        g_ObTelemetry.totalTemporaryObjectsUnlinked++;
    }
#endif
    ObDereferenceObject(object);
    return NtStatus::Success;
}

inline NtStatus ObCloseHandle(Handle handle, HandleTable& handleTable) {
    // HandleTable owns the close policy (including OBJ_PROTECT_CLOSE) and
    // detaches the reference before invoking object callbacks.
    return handleTable.closeHandle(handle);
}

/**
 * @brief Canonical Duplicate Handle (ObDuplicateHandle).
 */
inline NtStatus ObDuplicateHandle(
    Handle sourceHandle,
    HandleTable& sourceTable,
    HandleTable& targetTable,
    Handle& targetHandle,
    uint32_t desiredAccess = 0,
    bool inherit = false,
    uint32_t options = 0
) {
    NtStatus st = sourceTable.duplicateHandle(sourceHandle, targetTable, targetHandle, desiredAccess, inherit, options);
    if (NT_SUCCESS(st)) {
#if !defined(MICANT_FREESTANDING)
        std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
        g_ObTelemetry.totalHandlesDuplicated++;
#endif
    }
    return st;
}

// Native NtDuplicateObject's table operation, split from process-handle
// resolution and user capture so it can be exercised by kernel callers too.
// The output is cleared before work and remains zero on every failure.
inline NtStatus ObDuplicateHandleEx(
    Handle sourceHandle,
    HandleTable& sourceTable,
    HandleTable& targetTable,
    Handle& targetHandle,
    uint32_t desiredAccess,
    uint32_t handleAttributes,
    uint32_t options
) noexcept {
    targetHandle = 0;
    if ((options & ~uint32_t{7}) != 0 ||
        (!(options & 4) && (handleAttributes & ~OB_VALID_HANDLE_ATTRIBUTES) != 0))
        return NtStatus::InvalidParameter;
    const bool sameAttributes = (options & 4) != 0;
    const bool inherit = sameAttributes ? false : (handleAttributes & OBJ_INHERIT) != 0;
    return sourceTable.duplicateHandle(sourceHandle, targetTable, targetHandle,
                                       desiredAccess, inherit, options, handleAttributes);
}

/**
 * @brief Canonical Make Temporary Object (ObMakeTemporaryObject).
 */
inline void ObMakeTemporaryObject(ObjectHeader* object) {
    if (!object) return;
#if !defined(MICANT_FREESTANDING)
    std::lock_guard<std::recursive_mutex> namespaceGuard(g_ObjectNamespaceMutex);
#endif
    object->attributes &= ~OBJ_PERMANENT;

#if !defined(MICANT_FREESTANDING)
    if (object->handleCount.load(std::memory_order_relaxed) == 0 && object->parentDirectory) {
        auto* dir = reinterpret_cast<DirectoryObject*>(object->parentDirectory);
        dir->removeObject(object->objectName);
        object->parentDirectory = nullptr;
        object->nameInfo.directory = nullptr;
        {
            std::lock_guard<std::mutex> tlk(g_ObTelemetryMutex);
            g_ObTelemetry.totalTemporaryObjectsUnlinked++;
        }
    }
#endif
}

} // namespace micant::ob
