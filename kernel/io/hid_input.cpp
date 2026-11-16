#include "micant/km/drivers/input/kbdclass_contract.hpp"
#include "micant/km/drivers/input/mouclass_contract.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/drivers/input/input_device_baremetal.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/io_file.hpp"
#include <cstring>
#include <new>

extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;

namespace micant::km::hid {
namespace {
ke::SpinLock lock{};
KEYBOARD_INPUT_DATA keyboard[256]{};
MOUSE_INPUT_DATA mouse[256]{};
size_t keyHead{}, keyCount{}, mouseHead{}, mouseCount{};
sync::detail::BootstrapEvent keyboardReady{true, false};
sync::detail::BootstrapEvent mouseReady{true, false};
ob::ObjectType deviceType{};
ob::ObjectHeader* keyboardReference{};
ob::ObjectHeader* mouseReference{};
io::DeviceObject* keyboardDevice{};
io::DeviceObject* mouseDevice{};
struct InputFileContext { io::DeviceObject* device{}; std::atomic<bool> closed{false}; };
io::AsyncRead* asyncHead{};
io::AsyncRead* asyncTail{};
bool validateQueueState(size_t& head, size_t& count, const char* name,
                        const char* stage) noexcept {
    if (head < 256 && count <= 256) return true;
    MicantNetbootSerialWrite("[MicaNT HID] Corrupt input queue state queue=");
    MicantNetbootSerialWrite(name);
    MicantNetbootSerialWrite(" stage=");
    MicantNetbootSerialWrite(stage);
    MicantNetbootSerialWrite(" head=");
    MicantNetbootSerialWriteNumber(head);
    MicantNetbootSerialWrite(" count=");
    MicantNetbootSerialWriteNumber(count);
    MicantNetbootSerialWrite("\r\n");
    head = 0;
    count = 0;
    return false;
}
bool validateInputQueues(const char* stage) noexcept {
    const bool keyboardValid = validateQueueState(keyHead, keyCount, "keyboard", stage);
    const bool mouseValid = validateQueueState(mouseHead, mouseCount, "mouse", stage);
    return keyboardValid && mouseValid;
}
void drainAsyncReads() noexcept {
    (void)validateInputQueues("async-drain entry");
    io::AsyncRead* previous = nullptr;
    auto* request = asyncHead;
    while (request) {
        auto* next = request->driverNext;
        auto* file = static_cast<InputFileContext*>(request->file->context());
        const bool keyboardFile = file->device == keyboardDevice;
        const size_t recordSize = keyboardFile ? sizeof(KEYBOARD_INPUT_DATA) : sizeof(MOUSE_INPUT_DATA);
        NtStatus status = NtStatus::Pending;
        size_t count = 0;
        if (file->closed.load(std::memory_order_acquire)) status = NtStatus::Cancelled;
        else if (!request->length) status = NtStatus::Success;
        else if (request->length < recordSize) status = NtStatus::BufferTooSmall;
        else if (keyboardFile ? keyCount != 0 : mouseCount != 0) {
            while (request->length - count >= recordSize) {
                if (keyboardFile) {
                    if (!keyCount) break;
                    std::memcpy(request->buffer + count, &keyboard[keyHead], recordSize);
                    keyHead = (keyHead + 1) % 256; --keyCount;
                } else {
                    if (!mouseCount) break;
                    std::memcpy(request->buffer + count, &mouse[mouseHead], recordSize);
                    mouseHead = (mouseHead + 1) % 256; --mouseCount;
                }
                count += recordSize;
            }
            status = NtStatus::Success;
        }
        if (status != NtStatus::Pending) {
            if (previous) previous->driverNext = next; else asyncHead = next;
            if (asyncTail == request) asyncTail = previous;
            request->driverNext = nullptr;
            io::IoCompleteAsyncRead(request, status, count);
            (void)validateInputQueues("async-drain completion");
        } else previous = request;
        request = next;
    }
}
NtStatus submitAsyncRead(void* context, io::AsyncRead* request) noexcept {
    auto* file = static_cast<InputFileContext*>(context);
    if (!file || !request) return NtStatus::InvalidParameter;
    ke::SpinLockGuard guard(lock);
    request->driverNext = nullptr;
    if (asyncTail) asyncTail->driverNext = request; else asyncHead = request;
    asyncTail = request;
    drainAsyncReads();
    return NtStatus::Pending;
}
bool cancelAsyncRead(void* context, io::AsyncRead* request) noexcept {
    auto* file = static_cast<InputFileContext*>(context);
    if (!file || !request) return false;
    ke::SpinLockGuard guard(lock);
    io::AsyncRead* previous = nullptr;
    for (auto* current = asyncHead; current; current = current->driverNext) {
        if (current == request && current->file->context() == file) {
            if (previous) previous->driverNext = current->driverNext; else asyncHead = current->driverNext;
            if (asyncTail == current) asyncTail = previous;
            current->driverNext = nullptr;
            io::IoCompleteAsyncRead(current, NtStatus::Cancelled, 0);
            return true;
        }
        previous = current;
    }
    return false;
}

void deleteDevice(ob::ObjectHeader* header) { header->as<io::DeviceObject>()->~DeviceObject(); }
void releaseInputFile(void* context) noexcept { delete static_cast<InputFileContext*>(context); }
void closeInputFile(void* context) noexcept {
    auto* file = static_cast<InputFileContext*>(context);
    if (!file) return;
    ke::SpinLockGuard guard(lock);
    file->closed.store(true, std::memory_order_release);
    drainAsyncReads();
    (void)(file->device == keyboardDevice ? keyboardReady : mouseReady).set();
}
void wakeInputRead(void* context) noexcept {
    auto* file = static_cast<InputFileContext*>(context);
    if (!file) return;
    ke::SpinLockGuard guard(lock);
    (void)(file->device == keyboardDevice ? keyboardReady : mouseReady).set();
}
NtStatus readInput(void* context, uint64_t, void* output, size_t length, size_t* done) noexcept {
    auto* file = static_cast<InputFileContext*>(context);
    if (!file || !output || !done || (file->device != keyboardDevice && file->device != mouseDevice))
        return NtStatus::InvalidParameter;
    auto* device = file->device;
    *done = 0;
    const size_t recordSize = device == keyboardDevice ? sizeof(KEYBOARD_INPUT_DATA) : sizeof(MOUSE_INPUT_DATA);
    if (length < recordSize) return NtStatus::BufferTooSmall;
    auto* bytes = static_cast<uint8_t*>(output);
    auto& ready = device == keyboardDevice ? keyboardReady : mouseReady;
    for (;;) {
        {
            ke::SpinLockGuard guard(lock);
            (void)validateInputQueues("synchronous read");
            if (file->closed.load(std::memory_order_acquire) || io::IoCurrentReadCancelled()) return NtStatus::Cancelled;
            while (length - *done >= recordSize) {
                if (device == keyboardDevice) {
                    if (!keyCount) break;
                    std::memcpy(bytes + *done, &keyboard[keyHead], recordSize);
                    keyHead = (keyHead + 1) % 256; --keyCount;
                } else {
                    if (!mouseCount) break;
                    std::memcpy(bytes + *done, &mouse[mouseHead], recordSize);
                    mouseHead = (mouseHead + 1) % 256; --mouseCount;
                }
                *done += recordSize;
            }
            if ((device == keyboardDevice ? keyCount : mouseCount) == 0) (void)ready.reset();
        }
        if (*done) return NtStatus::Success;
        const auto status = ready.wait();
        if (status != NtStatus::Success) return status;
    }
}
}

#if defined(MICANT_BOOTSTRAP_CMD_TEST)
void DebugValidateInputQueues(const char* stage) noexcept {
    ke::SpinLockGuard guard(lock);
    (void)validateInputQueues(stage);
}
#endif

NtStatus initializeInputDevices() noexcept {
    if (keyboardReference || mouseReference) return NtStatus::Conflict;
    deviceType.typeId = ob::ObjectTypeId::Device;
    deviceType.deleteProcedure = deleteDevice;
    void* body{};
    auto status = ob::ObCreateObject(ob::KernelMode, &deviceType, nullptr,
                                     sizeof(io::DeviceObject), &body, &keyboardReference);
    if (status != NtStatus::Success) return status;
    keyboardDevice = new (body) io::DeviceObject{};
    keyboardDevice->deviceType = static_cast<io::DeviceType>(0x0b);
    body = nullptr;
    status = ob::ObCreateObject(ob::KernelMode, &deviceType, nullptr,
                                sizeof(io::DeviceObject), &body, &mouseReference);
    if (status != NtStatus::Success) {
        ob::ObDereferenceObject(keyboardReference);
        keyboardReference = nullptr; keyboardDevice = nullptr;
        return status;
    }
    mouseDevice = new (body) io::DeviceObject{};
    mouseDevice->deviceType = static_cast<io::DeviceType>(0x0f);
    return NtStatus::Success;
}
bool isInputDeviceName(std::u16string_view name) noexcept {
    return name == u"\\Device\\KeyboardClass0" || name == u"\\Device\\PointerClass0";
}
NtStatus openInputDevice(Handle* handle, uint32_t access, std::u16string_view name, bool synchronous) noexcept {
    auto* device = name == u"\\Device\\KeyboardClass0" ? keyboardDevice :
                   name == u"\\Device\\PointerClass0" ? mouseDevice : nullptr;
    if (!device) return NtStatus::ObjectNameNotFound;
    auto* context = new (std::nothrow_t{}) InputFileContext{};
    if (!context) return NtStatus::NoMemory;
    context->device = device;
    io::FileBackend backend{context, 0, name, readInput, releaseInputFile, true, closeInputFile, wakeInputRead, submitAsyncRead, cancelAsyncRead, synchronous};
    return io::IoCreateDeviceFile(handle, access, device, backend);
}
void WINAPI KeyboardClassServiceCallback(io::DeviceObject*, KEYBOARD_INPUT_DATA* first,
                                          KEYBOARD_INPUT_DATA* end, uint32_t* consumed) {
    if (consumed) *consumed = 0;
    if (!first || !end || end < first) return;
    uint32_t count = 0;
    {
        ke::SpinLockGuard guard(lock);
        (void)validateInputQueues("keyboard callback entry");
        if (static_cast<size_t>(end - first) > 256 - keyCount) return;
        while (first != end && keyCount < 256) {
            keyboard[(keyHead + keyCount++) % 256] = *first++;
            ++count;
        }
        (void)validateInputQueues("keyboard callback write");
        drainAsyncReads();
    }
    if (consumed) *consumed = count;
    if (count) (void)keyboardReady.set();
}
void WINAPI MouseClassServiceCallback(io::DeviceObject*, MOUSE_INPUT_DATA* first,
                                      MOUSE_INPUT_DATA* end, uint32_t* consumed) {
    if (consumed) *consumed = 0;
    if (!first || !end || end < first) return;
    uint32_t count = 0;
    {
        ke::SpinLockGuard guard(lock);
        (void)validateInputQueues("mouse callback entry");
        if (static_cast<size_t>(end - first) > 256 - mouseCount) return;
        while (first != end && mouseCount < 256) {
            mouse[(mouseHead + mouseCount++) % 256] = *first++;
            ++count;
        }
        (void)validateInputQueues("mouse callback write");
        drainAsyncReads();
    }
    if (consumed) *consumed = count;
    if (count) (void)mouseReady.set();
}
bool KbdClassPopInputData(KEYBOARD_INPUT_DATA& result) {
    ke::SpinLockGuard guard(lock);
    if (!validateQueueState(keyHead, keyCount, "keyboard", "pop")) return false;
    if (!keyCount) return false;
    result = keyboard[keyHead]; keyHead = (keyHead + 1) % 256; --keyCount; return true;
}
bool MouClassPopInputData(MOUSE_INPUT_DATA& result) {
    ke::SpinLockGuard guard(lock);
    if (!validateQueueState(mouseHead, mouseCount, "mouse", "pop")) return false;
    if (!mouseCount) return false;
    result = mouse[mouseHead]; mouseHead = (mouseHead + 1) % 256; --mouseCount; return true;
}
size_t KbdClassQueueDepth() { ke::SpinLockGuard guard(lock); return keyCount; }
size_t MouClassQueueDepth() { ke::SpinLockGuard guard(lock); return mouseCount; }
}
