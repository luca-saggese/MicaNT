#include "micant/km/core/ntos_internal.hpp"
#if !defined(MICANT_FREESTANDING)
#include "micant/km/executive/cm.hpp"
#include <unordered_map>
#include <memory>
#include <cstring>
#else
#include <array>
#include <cstring>
#endif

#if defined(MICANT_FREESTANDING)
extern "C" size_t wcslen(const wchar_t* value) {
    const auto* bytes = reinterpret_cast<const volatile uint8_t*>(value);
    size_t length{};
    while (bytes[length * sizeof(wchar_t)] || bytes[length * sizeof(wchar_t) + 1]) ++length;
    return length;
}
#endif

namespace micant::sys::internal {

#if defined(MICANT_FREESTANDING)
namespace {
struct BootstrapRegistryKey { Handle handle{}; bool active{}; };
std::array<BootstrapRegistryKey, 32> g_bootstrapRegistryKeys{};

bool registryNameEquals(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        auto upper = [](wchar_t c) { return c >= L'a' && c <= L'z' ? c - 32 : c; };
        if (upper(left[i]) != upper(right[i])) return false;
    }
    return true;
}

bool registryValue(std::wstring_view name, const wchar_t*& value) noexcept {
    if (registryNameEquals(name, L"ProductName")) value = L"LuceOS";
    else if (registryNameEquals(name, L"CurrentVersion")) value = L"10.0";
    else if (registryNameEquals(name, L"CurrentBuildNumber")) value = L"26100";
    else return false;
    return true;
}
}

bool CmCloseKeyHandle(Handle handle) {
    for (auto& key : g_bootstrapRegistryKeys) {
        if (key.active && key.handle == handle) { key = {}; return true; }
    }
    return false;
}

NtStatus NtOpenKeySys(const SyscallFrame& frame) {
    auto* outHandle = reinterpret_cast<Handle*>(frame.arg1);
    auto* attributes = reinterpret_cast<const ObjectAttributes*>(frame.arg3);
    if (!outHandle || !attributes || !attributes->objectName ||
        attributes->rootDirectory || attributes->objectName->view().empty())
        return NtStatus::InvalidParameter;
    if ((frame.arg2 & 1u) == 0) return NtStatus::AccessDenied; // KEY_QUERY_VALUE
    constexpr std::wstring_view currentVersionPath =
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    if (!registryNameEquals(attributes->objectName->view(), currentVersionPath))
        return NtStatus::ObjectNameNotFound;
    for (auto& key : g_bootstrapRegistryKeys) {
        if (key.active) continue;
        const Handle handle = ObAllocateExecutiveHandle();
        if (!handle) return NtStatus::InsufficientResources;
        key = {handle, true};
        *outHandle = handle;
        return NtStatus::Success;
    }
    return NtStatus::InsufficientResources;
}

NtStatus NtQueryValueKeySys(const SyscallFrame& frame) {
    if (frame.arg3 != 2) return NtStatus::InvalidParameter; // KeyValuePartialInformation
    bool validHandle = false;
    for (const auto& key : g_bootstrapRegistryKeys)
        validHandle |= key.active && key.handle == static_cast<Handle>(frame.arg1);
    if (!validHandle) return NtStatus::InvalidHandle;
    auto* name = reinterpret_cast<const UnicodeString*>(frame.arg2);
    if (!name) return NtStatus::InvalidParameter;
    const wchar_t* value{};
    if (!registryValue(name->view(), value)) return NtStatus::ObjectNameNotFound;
    if (!frame.stackArgs || frame.stackArgCount < 2) return NtStatus::InvalidParameter;
    const size_t capacity = static_cast<size_t>(frame.stackArgs[0]);
    auto* resultLength = reinterpret_cast<size_t*>(frame.stackArgs[1]);
    struct PartialInformation { uint32_t titleIndex, type, dataLength; };
    size_t chars{};
    const auto* valueBytes = reinterpret_cast<const volatile uint8_t*>(value);
    while (valueBytes[chars * sizeof(wchar_t)] ||
           valueBytes[chars * sizeof(wchar_t) + 1]) ++chars;
    const size_t dataLength = (chars + 1) * sizeof(wchar_t);
    const size_t required = sizeof(PartialInformation) + dataLength;
    if (resultLength) *resultLength = required;
    if (!frame.arg4 || capacity < required) return NtStatus::BufferTooSmall;
    const PartialInformation header{0, 1, static_cast<uint32_t>(dataLength)};
    std::memcpy(reinterpret_cast<void*>(frame.arg4), &header, sizeof(header));
    std::memcpy(reinterpret_cast<uint8_t*>(frame.arg4) + sizeof(header), value, dataLength);
    return NtStatus::Success;
}

NtStatus NtSetValueKeySys(const SyscallFrame&) { return NtStatus::NotSupported; }

#else
static std::unordered_map<Handle, std::shared_ptr<cm::KeyObject>> g_KernelKeys;

bool CmCloseKeyHandle(Handle h) {
    return g_KernelKeys.erase(h) > 0;
}

NtStatus NtOpenKeySys(const SyscallFrame& f) {
    auto* outHandle = reinterpret_cast<Handle*>(f.arg1);
    auto* objAttr = reinterpret_cast<ObjectAttributes*>(f.arg3);
    if (!outHandle || !objAttr || !objAttr->objectName) return NtStatus::InvalidParameter;

    auto keyObj = cm::ConfigurationManager::get().resolvePath(objAttr->objectName->view());
    if (!keyObj) return NtStatus::ObjectNameNotFound;

    Handle h = ObAllocateExecutiveHandle();
    g_KernelKeys[h] = keyObj;
    *outHandle = h;
    return NtStatus::Success;
}

NtStatus NtQueryValueKeySys(const SyscallFrame& f) {
    Handle h = static_cast<Handle>(f.arg1);
    auto* valName = reinterpret_cast<UnicodeString*>(f.arg2);
    void* outBuffer = reinterpret_cast<void*>(f.arg4);
    if (!valName || f.arg3 != 2) return NtStatus::InvalidParameter;

    auto it = g_KernelKeys.find(h);
    if (it == g_KernelKeys.end()) return NtStatus::InvalidHandle;

    const auto* val = it->second->getValue(valName->view());
    if (!val) return NtStatus::ObjectNameNotFound;

    size_t bufLen = 0;
    size_t* retLen = nullptr;
    if (f.stackArgs && f.stackArgCount >= 2) {
        bufLen = static_cast<size_t>(f.stackArgs[0]);
        retLen = reinterpret_cast<size_t*>(f.stackArgs[1]);
    }

    struct KeyValuePartialInformation {
        uint32_t titleIndex;
        uint32_t type;
        uint32_t dataLength;
    } header{0, static_cast<uint32_t>(val->type), static_cast<uint32_t>(val->data.size())};
    if (val->data.size() > UINT32_MAX - sizeof(header)) return NtStatus::InvalidParameter;
    const size_t required = sizeof(header) + val->data.size();
    if (retLen) *retLen = required;
    if (!outBuffer || bufLen < required) return NtStatus::BufferTooSmall;
    std::memcpy(outBuffer, &header, sizeof(header));
    if (!val->data.empty())
        std::memcpy(static_cast<uint8_t*>(outBuffer) + sizeof(header),
                    val->data.data(), val->data.size());
    return NtStatus::Success;
}

NtStatus NtSetValueKeySys(const SyscallFrame& f) {
    Handle h = static_cast<Handle>(f.arg1);
    auto* valName = reinterpret_cast<UnicodeString*>(f.arg2);
    uint32_t type = static_cast<uint32_t>(f.arg4);
    if (!valName) return NtStatus::InvalidParameter;

    auto it = g_KernelKeys.find(h);
    if (it == g_KernelKeys.end()) return NtStatus::InvalidHandle;

    const void* data = nullptr;
    uint32_t dataSize = 0;
    if (f.stackArgs && f.stackArgCount >= 2) {
        data = reinterpret_cast<const void*>(f.stackArgs[0]);
        dataSize = static_cast<uint32_t>(f.stackArgs[1]);
    }
    if (!data || dataSize == 0) return NtStatus::InvalidParameter;

    if (type == static_cast<uint32_t>(cm::RegType::Sz)) {
        std::wstring strVal(reinterpret_cast<const wchar_t*>(data), dataSize / sizeof(wchar_t));
        it->second->setValueString(valName->view(), strVal);
    } else if (type == static_cast<uint32_t>(cm::RegType::Dword) && dataSize >= sizeof(uint32_t)) {
        it->second->setValueDword(valName->view(), *reinterpret_cast<const uint32_t*>(data));
    } else if (type == static_cast<uint32_t>(cm::RegType::Qword) && dataSize >= sizeof(uint64_t)) {
        it->second->setValueQword(valName->view(), *reinterpret_cast<const uint64_t*>(data));
    }

    return NtStatus::Success;
}
#endif

} // namespace micant::sys::internal
