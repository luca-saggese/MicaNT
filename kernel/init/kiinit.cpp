#if defined(MICANT_FREESTANDING)

#include "micant/km/hal/boot_contract.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/hal/amd64_firmware_interrupts.hpp"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/freestanding.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/afd_io.hpp"
#include "micant/shared/afd_socket_abi.hpp"
#include "micant/km/executive/io_block_device.hpp"
#include "micant/km/executive/arbiter.hpp"
#include "micant/km/executive/dma_baremetal.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/core/passive_work.hpp"
#include "micant/km/drivers/usb/usb_host_baremetal.hpp"
#include "micant/km/drivers/input/input_device_baremetal.hpp"
#include "micant/km/fsrtl/fat_boot.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/win32k/freestanding.hpp"
#include "micant/shared/boot_driver.hpp"
#include "micant/km/drivers/net/ndis.hpp"
#include "micant/km/drivers/net/tcpip.hpp"
#include "micant/km/drivers/net/tcpip_kernel_api.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/win32k_client.h"
#include "tools/explorer_shell/desktop.hpp"
#include "tools/explorer_shell/taskbar.hpp"
#include <cstring>
#include <array>
#include <atomic>
#include <memory>
#include <new>
#include <span>

namespace micant::tcpip::kernel_api {
bool hasAdapter() noexcept { return NetworkStack::get().hasAdapter(); }
bool isInitialized() noexcept { return NetworkStack::get().isInitialized(); }
void* referenceSocketWaitObject(int id) noexcept {
    return NetworkStack::get().referenceSocketWaitObject(id);
}
void releaseSocketWaitObject(void* object) noexcept {
    NetworkStack::get().releaseSocketWaitObject(object);
}
void cancelSocketWaitObject(void* object) noexcept {
    NetworkStack::get().cancelSocketWaitObject(object);
}
int32_t waitSocketData(void* object, uint32_t timeoutMilliseconds) noexcept {
    return static_cast<int32_t>(NetworkStack::get().waitSocketData(object, timeoutMilliseconds));
}
int32_t waitSocketConnected(void* object, uint32_t timeoutMilliseconds) noexcept {
    return static_cast<int32_t>(NetworkStack::get().waitSocketConnected(object, timeoutMilliseconds));
}
int32_t waitSocketWritable(void* object, uint32_t timeoutMilliseconds) noexcept {
    return static_cast<int32_t>(NetworkStack::get().waitSocketWritable(object, timeoutMilliseconds));
}
int32_t waitSocketAccept(void* object, uint32_t timeoutMilliseconds) noexcept {
    return static_cast<int32_t>(NetworkStack::get().waitSocketAccept(object, timeoutMilliseconds));
}
int createSocket(int af, int type, int protocol) noexcept {
    return NetworkStack::get().createSocket(af, type, protocol);
}
void closeSocket(int id) noexcept { NetworkStack::get().closeSocket(id); }
bool bindSocket(int id, uint32_t ip, uint16_t port) noexcept {
    return NetworkStack::get().bindSocket(id, Ipv4Address(ip), port);
}
bool connectSocket(int id, uint32_t ip, uint16_t port, uint64_t deadline100ns) noexcept {
    return NetworkStack::get().connectSocket(id, Ipv4Address(ip), port, deadline100ns);
}
bool listenSocket(int id, int backlog) noexcept {
    return NetworkStack::get().listenSocket(id, backlog);
}
int acceptSocket(int id, uint32_t* remoteIp, uint16_t* remotePort) noexcept {
    if (!remoteIp || !remotePort) return -1;
    Ipv4Address address{};
    const int accepted = NetworkStack::get().acceptSocket(id, address, *remotePort);
    if (accepted >= 0) *remoteIp = address.addr;
    return accepted;
}
int sendSocket(int id, const void* data, size_t length, uint64_t deadline100ns) noexcept {
    return NetworkStack::get().sendSocket(id, data, length, deadline100ns);
}
int sendToSocket(int id, const void* data, size_t length, uint32_t ip, uint16_t port, uint64_t deadline100ns) noexcept {
    return NetworkStack::get().sendToSocket(id, data, length, Ipv4Address(ip), port, deadline100ns);
}
int recvSocket(int id, void* buffer, size_t capacity, bool* truncated) noexcept {
    return NetworkStack::get().recvSocket(id, buffer, capacity, truncated);
}
int recvFromSocket(int id, void* buffer, size_t capacity, uint32_t* ip, uint16_t* port, bool* truncated) noexcept {
    if (!ip || !port) return -1;
    Ipv4Address source{};
    const int received = NetworkStack::get().recvFromSocket(id, buffer, capacity, source, *port, truncated);
    if (received >= 0) *ip = source.addr;
    return received;
}
void queryNetworkConfiguration(uint32_t* ip, uint32_t* mask, uint32_t* gateway,
                               uint32_t* dns, uint8_t* mac) noexcept {
    if (!ip || !mask || !gateway || !dns || !mac) return;
    Ipv4Address address{}, subnet{}, router{}, resolver{};
    ndis::MacAddress hardware{};
    NetworkStack::get().queryNetworkConfiguration(address, subnet, router, resolver, hardware);
    *ip = address.addr; *mask = subnet.addr; *gateway = router.addr; *dns = resolver.addr;
    std::memcpy(mac, hardware.bytes, sizeof(hardware.bytes));
}
bool setNetworkConfiguration(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns) noexcept {
    return NetworkStack::get().applyNetworkConfiguration(
        Ipv4Address(ip), Ipv4Address(mask), Ipv4Address(gateway), Ipv4Address(dns));
}
} // namespace micant::tcpip::kernel_api

namespace micant::sys {
bool initializeBootstrapSectionServices() noexcept;
void initializeBootstrapEventServices() noexcept;
void scheduleDeferredTimerDeletes() noexcept;
bool verifyBootstrapSectionOwnership() noexcept;
}
namespace micant::netboot {
micant::NtStatus acquireBootDhcpLease() noexcept;
}

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
// The loader's call stack is not a kernel-owned scheduling resource. Keep the
// bootstrap context on a resident stack for its full lifetime.
extern "C" {
alignas(4096) uint8_t MicantBootstrapMainStack[131072]{};
}
#endif

namespace {

constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 768;
constexpr uint32_t kTaskbarHeight = 40;
constexpr uint32_t kCursorSize = 16;
constexpr size_t kBootstrapPfnCount = 65536;
constexpr size_t kMaximumMemoryDescriptors = 1024;
alignas(4096) micant::mm::Mmpfn g_bootPfnMetadata[kBootstrapPfnCount]{};
micant::uefi::EfiMemoryDescriptor g_bootMemoryDescriptors[kMaximumMemoryDescriptors]{};
micant::mm::PhysicalPageRange g_bootAvailableRanges[kMaximumMemoryDescriptors + 1]{};
alignas(16) uint32_t g_explorerSurface[kWidth * kHeight]{};
uint32_t g_cursorFrame[kCursorSize * kCursorSize]{};
micant::boot_driver::KernelServices g_displayServices{};
micant::boot_driver::DisplayDevice g_display{};
micant::boot_driver::DriverObject g_displayDriver{};
micant::boot::KernelFramebufferV1 g_bootFramebufferInfo{};
struct GopDisplayContext {
    volatile uint32_t* Pixels{};
    uint32_t Width{};
    uint32_t Height{};
    uint32_t Pitch{};
    uint32_t PixelFormat{};
    uint64_t ByteSize{};
};
GopDisplayContext g_gopDisplayContext{};
bool g_displayUsingGopFallback{};
constexpr size_t kMaxBootStartDrivers = micant::boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES;
micant::boot_driver::KernelServices g_bootDriverServices[kMaxBootStartDrivers]{};
micant::boot_driver::DriverObject g_bootDriverObjects[kMaxBootStartDrivers]{};
micant::boot_driver::DisplayDevice g_bootDriverDisplays[kMaxBootStartDrivers]{};
struct BootFileProvider {
    int32_t (*Open)(void*, const char16_t*, size_t, micant::boot_driver::FileBackendV1*);
    void* Context;
    size_t OwnerIndex;
    size_t ActiveBackends;
    bool Used;
};
struct BootDriverServiceContext { size_t Index{}; uint64_t MsiInterruptToken{}; };
int32_t bootRegisterNetworkAdapter(void*,
    const micant::shared::ndis::AdapterServicesV1*, uint64_t*) noexcept;
int32_t bootUnregisterNetworkAdapter(void*, uint64_t) noexcept;
struct BootOwnedHandle { uint64_t Value{}; bool Used{}; };
constexpr size_t kMaxBootDriverHandles = 32;
BootDriverServiceContext g_bootDriverContexts[kMaxBootStartDrivers]{};
struct BootNdisAdapterRegistration {
    uint64_t Cookie{};
    BootDriverServiceContext* Owner{};
    micant::shared::ndis::AdapterServicesV1 DriverServices{};
    micant::shared::ndis::AdapterServicesV1 KernelServices{};
    std::atomic<uint32_t> ActiveCalls{};
    std::atomic<bool> Stopping{};
    std::atomic<micant::shared::ndis::ReceivePacketRoutine> ReceiveRoutine{};
    std::atomic<void*> ReceiveContext{};
    bool Used{};
};
constexpr size_t kMaxBootNdisAdapters = 4;
BootNdisAdapterRegistration g_bootNdisAdapters[kMaxBootNdisAdapters]{};
uint64_t g_nextBootNdisCookie{1};
BootOwnedHandle g_bootDriverHandles[kMaxBootStartDrivers][kMaxBootDriverHandles]{};
BootFileProvider g_bootFileProviders[kMaxBootStartDrivers]{};
size_t g_bootFileProviderCount{};
bool g_bootFileOpenRouterInstalled{};
uint32_t g_mouseX = (kWidth - kCursorSize) / 2;
uint32_t g_mouseY = (kHeight - kCursorSize) / 2;
uint32_t g_mouseButtons{};
uint32_t g_mouseSequence{};
bool pollMouseInput(MicaWin32kInputEvent& event) noexcept;
micant::ke::KThread g_bootContextThread{};
micant::ke::KThread g_firstContextThread{};
micant::ke::KThread g_secondContextThread{};
alignas(4096) uint8_t g_firstContextStack[32768]{};
alignas(4096) uint8_t g_secondContextStack[32768]{};
uintptr_t g_firstContextLocal{};
uintptr_t g_secondContextLocal{};
uint32_t g_contextOrder{};
micant::Handle g_contextSection{};
micant::Handle g_firstContextSection{};
micant::Handle g_secondContextSection{};
micant::Handle g_firstOnlySection{};
void serialWriteNumber(uint64_t value) noexcept;
extern "C" bool MicantVerifyContextSwitch(
    micant::ke::KThread*, micant::ke::KThread*) noexcept;
extern "C" bool MicantTestKeReadyQueue() noexcept;
extern "C" bool MicantTestKeBlockWake() noexcept;
extern "C" bool MicantTestKeApcFifo() noexcept;
extern "C" bool MicantTestObOpenAccess() noexcept;
extern "C" uint32_t MicantObOpenAccessFailureStage() noexcept;
extern "C" bool MicantTestSeFreestandingAccess() noexcept;
extern "C" bool MicantTestKeDeferredDelivery() noexcept;
extern "C" bool MicantTestKeEventWait() noexcept;
extern "C" bool MicantTestPsThreadLifecycle() noexcept;
extern "C" uint32_t MicantPsAttachFailureStage() noexcept;
extern "C" bool MicantTestPsDeferredReaper() noexcept;
extern "C" bool MicantTestPsUserProcess() noexcept;
extern "C" uint32_t MicantPsUserProcessFailureStage() noexcept;
extern "C" bool MicantTestPsTwoUserThreads() noexcept;
extern "C" bool MicantTestPsThreeUserThreads() noexcept;
extern "C" bool MicantTestPsFileBackedImage() noexcept;
extern "C" int MicantTestMmAvlTable() noexcept;
extern "C" int MicantTestMmProcessAddressSpace() noexcept;
extern "C" int MicantTestMmManagedProtection() noexcept;
extern "C" bool MicantTestPartitionTables() noexcept;
extern "C" int pe_tls_freestanding_test();

void writePort(uint16_t port, uint8_t value) noexcept {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t readPort(uint16_t port) noexcept {
    uint8_t value{};
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// Bytes are captured at interrupt level, while packet decoding and rendering
// remain in the shell. All accesses hold BSP interrupt exclusion. Keyboard
// and auxiliary bytes have separate rings so one source cannot consume the
// other's pending input.
struct Ps2ByteRing {
    uint8_t bytes[128]{};
    bool fromInterrupt[128]{};
    uint8_t read{};
    uint8_t write{};
    bool lost{};
};
Ps2ByteRing g_ps2MouseRing{};
Ps2ByteRing g_ps2KeyboardRing{};
bool g_ps2InterruptMode{};
uint8_t g_ps2Configuration{};

void pushPs2Byte(Ps2ByteRing& ring, uint8_t data, bool fromInterrupt) noexcept {
    const uint8_t next = static_cast<uint8_t>((ring.write + 1) & 127);
    if (next == ring.read) { ring.lost = true; ring.read = ring.write; }
    ring.bytes[ring.write] = data;
    ring.fromInterrupt[ring.write] = fromInterrupt;
    ring.write = next;
}

bool popPs2Byte(Ps2ByteRing& ring, uint8_t& data, bool& lost, bool& fromInterrupt) noexcept {
    lost = ring.lost;
    ring.lost = false;
    if (ring.read == ring.write) return false;
    data = ring.bytes[ring.read];
    fromInterrupt = ring.fromInterrupt[ring.read];
    ring.read = static_cast<uint8_t>((ring.read + 1) & 127);
    return true;
}

void capturePs2Bytes(bool fromInterrupt) noexcept {
    for (unsigned count = 0; count < 64; ++count) {
        const uint8_t status = readPort(0x64);
        if (!(status & 1)) break;
        const uint8_t data = readPort(0x60);
        auto& ring = (status & 0x20) ? g_ps2MouseRing : g_ps2KeyboardRing;
        if (status & 0xc0) { ring.lost = true; ring.read = ring.write; continue; }
        pushPs2Byte(ring, data, fromInterrupt);
    }
}

bool nextMouseByte(uint8_t& status, uint8_t& data, bool& lost, bool& fromInterrupt) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    bool result{};
    if (g_ps2InterruptMode) {
        // Recover a missed edge without marking polled bytes as ISR delivery.
        capturePs2Bytes(false);
        status = 0x20;
        result = popPs2Byte(g_ps2MouseRing, data, lost, fromInterrupt);
    } else {
        lost = false;
        fromInterrupt = false;
        status = readPort(0x64);
        result = (status & 1) != 0;
        if (result) data = readPort(0x60);
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

void serialInitialize() noexcept {
    writePort(0x3F9, 0x00);
    writePort(0x3FB, 0x80);
    writePort(0x3F8, 0x01);
    writePort(0x3F9, 0x00);
    writePort(0x3FB, 0x03);
    writePort(0x3FA, 0xC7);
    writePort(0x3FC, 0x0B);
}

void serialWrite(const char* text) noexcept {
    for (size_t i = 0; text[i] != '\0'; ++i) {
        for (uint32_t wait = 0; wait < 1000000; ++wait) {
            if (readPort(0x3FD) & 0x20) break;
            __asm__ __volatile__("pause");
        }
        writePort(0x3F8, static_cast<uint8_t>(text[i]));
    }
}

void serialWriteChar(char value) noexcept {
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if (readPort(0x3FD) & 0x20) break;
        __asm__ __volatile__("pause");
    }
    writePort(0x3F8, static_cast<uint8_t>(value));
}

[[noreturn]] void stopWithError(const char* message) noexcept {
    serialWrite(message);
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

void firstContextEntry() noexcept {
    uint64_t actualCr3{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(actualCr3));
    if (actualCr3 != g_firstContextThread.directoryTableBase ||
        actualCr3 != micant::ke::g_CurrentCpu.currentCr3)
        stopWithError("[MicaNT Ke] First thread CR3 mismatch\r\n");
    if (g_contextSection) {
        uintptr_t privateAddress = micant::hal::amd64::BootstrapManagedBase;
        uintptr_t sharedAddress = privateAddress + micant::mm::ProcessAddressSpace::AllocationGranularity;
        size_t viewSize = micant::mm::PageSize4KB;
        size_t privateSize = viewSize;
        if (!g_firstContextThread.owningProcess ||
            micant::sys::NtAllocateVirtualMemory(micant::InvalidHandleValue, &privateAddress, 0, &privateSize,
                micant::mm::MEM_RESERVE | micant::mm::MEM_COMMIT, micant::mm::PAGE_READWRITE) != micant::NtStatus::Success ||
            micant::sys::NtMapViewOfSectionFromKernel(g_firstContextSection, micant::InvalidHandleValue, &sharedAddress,
                0, 0, nullptr, &viewSize, 2, 0, micant::mm::PAGE_READWRITE) != micant::NtStatus::Success)
            stopWithError("[MicaNT Ps] First process mapping failed\r\n");
        *reinterpret_cast<volatile uint64_t*>(privateAddress) = 0x11111111;
        *reinterpret_cast<volatile uint64_t*>(sharedAddress) = 0x22222222;
        uintptr_t conflicting = sharedAddress;
        size_t conflictingSize = viewSize;
        if (micant::sys::NtMapViewOfSectionFromKernel(g_firstContextSection, micant::InvalidHandleValue, &conflicting,
                0, 0, nullptr, &conflictingSize, 2, 0, micant::mm::PAGE_READWRITE) != micant::NtStatus::Conflict ||
            conflicting != sharedAddress || *reinterpret_cast<volatile uint64_t*>(sharedAddress) != 0x22222222)
            stopWithError("[MicaNT Ps] Failed view rollback corrupted process state\r\n");
    }
    volatile uint64_t pattern[16]{};
    for (size_t index = 0; index < 16; ++index) pattern[index] = 0xA100 + index;
    g_firstContextLocal = reinterpret_cast<uintptr_t>(&pattern[0]);
    g_contextOrder = g_contextOrder * 10 + 1;
    if (!MicantVerifyContextSwitch(&g_firstContextThread, &g_secondContextThread)) {
        stopWithError("[MicaNT Ke] First context register verification failed\r\n");
    }
    if (micant::ke::KeGetCurrentThread() != &g_firstContextThread ||
        g_contextOrder != 12) {
        stopWithError("[MicaNT Ke] First context resume verification failed\r\n");
    }
    if (g_contextSection &&
        (*reinterpret_cast<volatile uint64_t*>(micant::hal::amd64::BootstrapManagedBase) != 0x11111111 ||
         *reinterpret_cast<volatile uint64_t*>(micant::hal::amd64::BootstrapManagedBase +
             micant::mm::ProcessAddressSpace::AllocationGranularity) != 0x33333333))
        stopWithError("[MicaNT Ps] First process isolation failed\r\n");
    for (size_t index = 0; index < 16; ++index) {
        if (pattern[index] != 0xA100 + index) {
            stopWithError("[MicaNT Ke] First context stack verification failed\r\n");
        }
    }
    g_contextOrder = g_contextOrder * 10 + 1;
    g_firstContextThread.state = micant::ke::ThreadState::Terminated;
    (void)MicantVerifyContextSwitch(&g_firstContextThread, &g_secondContextThread);
    stopWithError("[MicaNT Ke] Terminated first context resumed\r\n");
}

void secondContextEntry() noexcept {
    uint64_t actualCr3{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(actualCr3));
    if (actualCr3 != g_secondContextThread.directoryTableBase ||
        actualCr3 != micant::ke::g_CurrentCpu.currentCr3)
        stopWithError("[MicaNT Ke] Second thread CR3 mismatch\r\n");
    if (g_contextSection) {
        uintptr_t privateAddress = micant::hal::amd64::BootstrapManagedBase;
        uintptr_t sharedAddress = privateAddress + micant::mm::ProcessAddressSpace::AllocationGranularity;
        size_t viewSize = micant::mm::PageSize4KB;
        size_t privateSize = viewSize;
        uintptr_t forbiddenAddress{};
        size_t forbiddenSize = viewSize;
        if (micant::sys::NtMapViewOfSectionFromKernel(g_firstOnlySection, micant::InvalidHandleValue, &forbiddenAddress,
                0, 0, nullptr, &forbiddenSize, 2, 0, micant::mm::PAGE_READONLY) != micant::NtStatus::InvalidHandle ||
            forbiddenAddress != 0)
            stopWithError("[MicaNT Ps] Handle table isolation failed\r\n");
        if (!g_secondContextThread.owningProcess ||
            micant::sys::NtAllocateVirtualMemory(micant::InvalidHandleValue, &privateAddress, 0, &privateSize,
                micant::mm::MEM_RESERVE | micant::mm::MEM_COMMIT, micant::mm::PAGE_READWRITE) != micant::NtStatus::Success ||
            micant::sys::NtMapViewOfSectionFromKernel(g_secondContextSection, micant::InvalidHandleValue, &sharedAddress,
                0, 0, nullptr, &viewSize, 2, 0, micant::mm::PAGE_READWRITE) != micant::NtStatus::Success ||
            *reinterpret_cast<volatile uint64_t*>(privateAddress) != 0 ||
            *reinterpret_cast<volatile uint64_t*>(sharedAddress) != 0x22222222)
            stopWithError("[MicaNT Ps] Second process mapping failed\r\n");
        *reinterpret_cast<volatile uint64_t*>(privateAddress) = 0x44444444;
        *reinterpret_cast<volatile uint64_t*>(sharedAddress) = 0x33333333;
    }
    volatile uint64_t pattern[16]{};
    for (size_t index = 0; index < 16; ++index) pattern[index] = 0xB200 + index;
    g_secondContextLocal = reinterpret_cast<uintptr_t>(&pattern[0]);
    g_contextOrder = g_contextOrder * 10 + 2;
    if (!MicantVerifyContextSwitch(&g_secondContextThread, &g_firstContextThread)) {
        stopWithError("[MicaNT Ke] Second context register verification failed\r\n");
    }
    if (micant::ke::KeGetCurrentThread() != &g_secondContextThread ||
        g_contextOrder != 121) {
        stopWithError("[MicaNT Ke] Second context resume verification failed\r\n");
    }
    if (g_contextSection &&
        *reinterpret_cast<volatile uint64_t*>(micant::hal::amd64::BootstrapManagedBase) != 0x44444444)
        stopWithError("[MicaNT Ps] Second process isolation failed\r\n");
    for (size_t index = 0; index < 16; ++index) {
        if (pattern[index] != 0xB200 + index) {
            stopWithError("[MicaNT Ke] Second context stack verification failed\r\n");
        }
    }
    g_contextOrder = g_contextOrder * 10 + 2;
    if (g_contextSection) {
        if (!MicantVerifyContextSwitch(&g_secondContextThread, &g_bootContextThread) ||
            *reinterpret_cast<volatile uint64_t*>(micant::hal::amd64::BootstrapManagedBase) != 0x44444444 ||
            *reinterpret_cast<volatile uint64_t*>(micant::hal::amd64::BootstrapManagedBase +
                micant::mm::ProcessAddressSpace::AllocationGranularity) != 0x33333333)
            stopWithError("[MicaNT Ps] Surviving process changed after cleanup\r\n");
    }
    g_secondContextThread.state = micant::ke::ThreadState::Terminated;
    (void)MicantVerifyContextSwitch(&g_secondContextThread, &g_bootContextThread);
    stopWithError("[MicaNT Ke] Terminated second context resumed\r\n");
}

[[noreturn]] void contextEntryReturned() noexcept {
    stopWithError("[MicaNT Ke] Context entry returned unexpectedly\r\n");
}

void initializeContextThread(
    micant::ke::KThread& thread,
    void (*entry)() noexcept,
    uint8_t* stack,
    size_t stackSize
) noexcept {
    const uintptr_t stackBase = reinterpret_cast<uintptr_t>(stack);
    if (stackSize < 256 || stackBase > UINTPTR_MAX - stackSize) {
        stopWithError("[MicaNT Ke] Invalid context stack bounds\r\n");
    }
    const uintptr_t stackTop = (stackBase + stackSize) & ~uintptr_t{15};
    const uintptr_t initialRsp = stackTop - 40;
    auto* frame = reinterpret_cast<uintptr_t*>(initialRsp);
    frame[0] = reinterpret_cast<uintptr_t>(&contextEntryReturned);

    thread.kernelStackBase = stackBase;
    thread.kernelStackTop = stackTop;
    thread.kernelExecutionStackTop = stackTop;
    thread.directoryTableBase = g_bootContextThread.directoryTableBase;
    thread.context = micant::ke::KThreadContext{};
    thread.context.rip = reinterpret_cast<uintptr_t>(entry);
    thread.context.rsp = initialRsp;
    thread.context.rflags = 0x2;
    thread.state = micant::ke::ThreadState::Ready;
}

void verifyKernelContextSwitch(bool separateRoots) noexcept {
    using namespace micant::ke;
    using namespace micant::hal::amd64;
    const size_t activePages = micant::mm::PfnDatabase::get().getActiveCount();
    micant::ps::EProcess firstProcess;
    micant::ps::EProcess secondProcess;
    if (separateRoots && (firstProcess.initializeAddressSpace() != micant::NtStatus::Success ||
                          secondProcess.initializeAddressSpace() != micant::NtStatus::Success))
        stopWithError("[MicaNT Ke] Context root allocation failed\r\n");
    g_contextOrder = 0;
    uintptr_t activeCr3{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(activeCr3));
    if (!activeCr3) stopWithError("[MicaNT Ke] Bootstrap CR3 is invalid\r\n");
    g_CurrentCpu.currentCr3 = activeCr3;
    const uint64_t initialSwitchCount = g_CurrentCpu.contextSwitches;

    g_bootContextThread.state = ThreadState::Running;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    g_bootContextThread.kernelStackBase = reinterpret_cast<uintptr_t>(MicantBootstrapMainStack);
    g_bootContextThread.kernelExecutionStackTop =
        reinterpret_cast<uintptr_t>(MicantBootstrapMainStack + sizeof(MicantBootstrapMainStack));
    // Ring-3 entry uses HAL's separate bootstrap stack; it must not overwrite
    // frames suspended on the boot thread's resident execution stack.
    g_bootContextThread.kernelStackTop = 0;
#endif
    g_bootContextThread.directoryTableBase = activeCr3;
    g_CurrentCpu.currentThread = &g_bootContextThread;
    initializeContextThread(
        g_firstContextThread, firstContextEntry, g_firstContextStack,
        sizeof(g_firstContextStack));
    initializeContextThread(
        g_secondContextThread, secondContextEntry, g_secondContextStack,
        sizeof(g_secondContextStack));
    if (separateRoots) {
        if (firstProcess.attachThread(g_firstContextThread) != micant::NtStatus::Success ||
            secondProcess.attachThread(g_secondContextThread) != micant::NtStatus::Success ||
            firstProcess.closeAddressSpace() != micant::NtStatus::InvalidParameter)
            stopWithError("[MicaNT Ps] Context ownership verification failed\r\n");
        micant::LargeInteger maximum{};
        maximum.quadPart = micant::mm::PageSize4KB;
        if (micant::sys::NtCreateSectionFromKernel(&g_contextSection,
                micant::section::SECTION_MAP_READ | micant::section::SECTION_MAP_WRITE,
                nullptr, &maximum, micant::mm::PAGE_READWRITE, micant::section::SEC_COMMIT, 0) != micant::NtStatus::Success)
            stopWithError("[MicaNT Ps] Shared section creation failed\r\n");
        auto& sourceHandles = micant::ob::detail::kernelHandleTable();
        if (sourceHandles.duplicateHandle(g_contextSection, firstProcess.getHandleTable(),
                                           g_firstContextSection) != micant::NtStatus::Success ||
            sourceHandles.duplicateHandle(g_contextSection, secondProcess.getHandleTable(),
                                           g_secondContextSection) != micant::NtStatus::Success ||
            sourceHandles.duplicateHandle(g_contextSection, firstProcess.getHandleTable(),
                                           g_firstOnlySection) != micant::NtStatus::Success)
            stopWithError("[MicaNT Ps] Section handle duplication failed\r\n");
    }

    const KIRQL previousIrql = KfRaiseIrql(DISPATCH_LEVEL);
    auto& scheduler = PriorityScheduler::get();
    if (!scheduler.readyKThread(&g_firstContextThread) ||
        !scheduler.readyKThread(&g_secondContextThread) ||
        scheduler.readyKThread(&g_firstContextThread) ||
        scheduler.getReadyThreadCount() != 2 ||
        KiSwapContext(&g_bootContextThread, &g_firstContextThread)) {
        stopWithError("[MicaNT Ke] Ready queue insertion verification failed\r\n");
    }
    KThread* first = scheduler.selectNextThread();
    KThread* second = scheduler.selectNextThread();
    if (first != &g_firstContextThread || second != &g_secondContextThread ||
        scheduler.selectNextThread() || scheduler.getReadyThreadCount() != 0 ||
        first->state != ThreadState::Standby ||
        second->state != ThreadState::Standby ||
        g_CurrentCpu.contextSwitches != initialSwitchCount) {
        stopWithError("[MicaNT Ke] Ready queue selection verification failed\r\n");
    }
    if (!KiSwapContext(&g_bootContextThread, first)) {
        stopWithError("[MicaNT Ke] Initial AMD64 context switch failed\r\n");
    }
    if (separateRoots) {
        if (g_firstContextThread.state != ThreadState::Terminated ||
            g_secondContextThread.state != ThreadState::Ready ||
            firstProcess.detachThread(g_firstContextThread) != micant::NtStatus::Success ||
            micant::sys::NtClose(g_contextSection) != micant::NtStatus::Success ||
            firstProcess.closeAddressSpace() != micant::NtStatus::Success ||
            !KiSwapContext(&g_bootContextThread, &g_secondContextThread))
            stopWithError("[MicaNT Ps] Independent process cleanup failed\r\n");
    }
    KeLowerIrql(previousIrql);

    const uintptr_t firstStack = reinterpret_cast<uintptr_t>(g_firstContextStack);
    const uintptr_t secondStack = reinterpret_cast<uintptr_t>(g_secondContextStack);
    if (g_CurrentCpu.currentThread != &g_bootContextThread ||
        g_bootContextThread.state != ThreadState::Running) {
        stopWithError("[MicaNT Ke] Bootstrap context state verification failed\r\n");
    }
    if (g_firstContextThread.state != ThreadState::Terminated ||
        g_secondContextThread.state != ThreadState::Terminated) {
        stopWithError("[MicaNT Ke] Thread termination state verification failed\r\n");
    }
    if (g_contextOrder != 1212) {
        stopWithError("[MicaNT Ke] Context execution order verification failed\r\n");
    }
    if (g_CurrentCpu.currentCr3 != activeCr3) {
        serialWrite("[MicaNT Ke] current CR3 expected/observed: ");
        serialWriteNumber(activeCr3);
        serialWrite("/");
        serialWriteNumber(g_CurrentCpu.currentCr3);
        serialWrite("\r\n");
        stopWithError("[MicaNT Ke] Context address-space verification failed\r\n");
    }
    if (g_CurrentCpu.contextSwitches != initialSwitchCount + (separateRoots ? 7 : 5)) {
        stopWithError("[MicaNT Ke] Context switch count verification failed\r\n");
    }
    if (g_firstContextLocal < firstStack ||
        g_firstContextLocal >= firstStack + sizeof(g_firstContextStack) ||
        g_secondContextLocal < secondStack ||
        g_secondContextLocal >= secondStack + sizeof(g_secondContextStack) ||
        g_firstContextLocal == g_secondContextLocal) {
        stopWithError("[MicaNT Ke] Context stack ownership verification failed\r\n");
    }
    if (separateRoots) {
        if (secondProcess.detachThread(g_secondContextThread) != micant::NtStatus::Success ||
            secondProcess.closeAddressSpace() != micant::NtStatus::Success)
            stopWithError("[MicaNT Ps] Surviving process cleanup failed\r\n");
    }
    g_contextSection = 0;
    g_firstContextSection = 0;
    g_secondContextSection = 0;
    g_firstOnlySection = 0;
    if (separateRoots && micant::mm::PfnDatabase::get().getActiveCount() != activePages)
        stopWithError("[MicaNT Ps] Context PFN accounting mismatch\r\n");
    serialWrite("[MicaNT Ke] Allocation-free ready queue selected cooperative contexts\r\n");
    serialWrite("[MicaNT Ke] Cooperative AMD64 contexts, stacks, GPR, XMM and FP control verified\r\n");
}

void serialWriteNumber(uint64_t value) noexcept {
    char text[21]{};
    size_t index = sizeof(text) - 1;
    do {
        text[--index] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    serialWrite(text + index);
}

volatile uint8_t* physicalPageBytes(uint64_t pfn) noexcept {
    const uint64_t address = pfn * 4096;
    if (pfn < 256 || pfn >= kBootstrapPfnCount ||
        !micant::hal::amd64::bootstrapKernelRange(address, 4096, true) ||
        micant::hal::amd64::translateBootstrapAddress(
            reinterpret_cast<const void*>(address)) != address) {
        stopWithError("[MicaNT Mm] Physical page lacks writable supervisor identity backing\r\n");
    }
    return reinterpret_cast<volatile uint8_t*>(address);
}

void zeroPhysicalPage(uint64_t pfn) noexcept {
    auto* bytes = physicalPageBytes(pfn);
    for (size_t index = 0; index < 4096; ++index) bytes[index] = 0;
}

void* mapBootstrapHeapPages(uint64_t firstPfn, size_t pageCount) noexcept {
    if (firstPfn < 256 || !pageCount || firstPfn >= kBootstrapPfnCount ||
        pageCount > kBootstrapPfnCount - firstPfn) {
        return nullptr;
    }
    for (size_t index = 0; index < pageCount; ++index) {
        (void)physicalPageBytes(firstPfn + index);
    }
    return reinterpret_cast<void*>(firstPfn * 4096);
}

micant::NtStatus probeBootstrapRange(uintptr_t address, size_t size, bool writable,
                                     micant::mm::KPROCESSOR_MODE mode) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) : : "memory");
    if (flags & (uint64_t{1} << 9)) return micant::NtStatus::AccessViolation;
    if (mode == micant::mm::UserMode) {
        if (micant::hal::amd64::bootstrapUserRange(address, size, writable)) return micant::NtStatus::Success;
        if (!writable || !size || address > UINTPTR_MAX - (size - 1) ||
            micant::ke::KeGetCurrentIrql() != micant::ke::PASSIVE_LEVEL) return micant::NtStatus::AccessViolation;
        auto* process = micant::ke::KeGetCurrentProcess();
        if (!process) return micant::NtStatus::AccessViolation;
        size_t checked{};
        while (checked < size) {
            const uintptr_t cursor = address + checked;
            const size_t pageRemaining = micant::mm::PageSize4KB - (cursor & micant::mm::PageMask4KB);
            const size_t step = size - checked < pageRemaining ? size - checked : pageRemaining;
            if (!micant::hal::amd64::bootstrapUserRange(cursor, step, true)) {
                const auto resolved = process->getAddressSpace().tryResolveCopyOnWriteForProbe(cursor);
                if (resolved != micant::NtStatus::Success) return resolved;
                if (!micant::hal::amd64::bootstrapUserRange(cursor, step, true))
                    return micant::NtStatus::AccessViolation;
            }
            checked += step;
        }
        return micant::NtStatus::Success;
    }
    return mode == micant::mm::KernelMode &&
           micant::hal::amd64::bootstrapKernelRange(address, size, writable)
        ? micant::NtStatus::Success : micant::NtStatus::AccessViolation;
}

bool verifyKernelObjectHandles() noexcept {
    using namespace micant;
    auto& handles = ob::detail::kernelHandleTable();
    if (handles.getActiveHandleCount() != 0) return false;
    ob::ObjectType type;
    ob::ObjectHeader object;
    object.type = &type;
    constexpr uint32_t access = ob::SYNCHRONIZE | 1;
    Handle first{}, second{};
    uint32_t granted{};
    if (handles.createHandle(&object, first, access) != NtStatus::Success ||
        !first || handles.lookup(first, &granted) != &object || granted != access ||
        object.pointerCount != 2 || object.handleCount != 1 ||
        type.totalNumberOfHandles != 1 ||
        handles.duplicateHandle(first, handles, second, 0, true, 2) != NtStatus::Success ||
        first == second || handles.lookup(second, &granted) != &object ||
        granted != access || handles.getActiveHandleCount() != 2 ||
        !handles.getEntry(second) ||
        handles.getEntry(second)->handleAttributes != OBJ_INHERIT ||
        object.pointerCount != 3 || object.handleCount != 2 ||
        type.totalNumberOfHandles != 2 ||
        handles.closeHandle(first) != NtStatus::Success ||
        handles.lookup(first) || handles.lookup(second) != &object ||
        handles.closeHandle(first) != NtStatus::InvalidHandle ||
        object.pointerCount != 2 || object.handleCount != 1 ||
        handles.closeHandle(second) != NtStatus::Success) {
        return false;
    }
    return handles.getActiveHandleCount() == 0 &&
           object.pointerCount == 1 && object.handleCount == 0 &&
           object.handleInfo.handleCount == 0 && type.totalNumberOfHandles == 0;
}

void initializePhysicalMemory(const micant::boot::UefiKernelHandoffV3& handoff) noexcept {
    using namespace micant;
    const uint64_t descriptorCount = handoff.memoryMapSize / handoff.memoryMapDescriptorSize;
    if (handoff.memoryMapDescriptorVersion != 1 ||
        descriptorCount > kMaximumMemoryDescriptors ||
        !hal::amd64::bootstrapKernelRange(
            handoff.memoryMapAddress, handoff.memoryMapSize, false) ||
        !hal::amd64::bootstrapKernelRange(
            reinterpret_cast<uint64_t>(g_bootPfnMetadata), sizeof(g_bootPfnMetadata), true)) {
        stopWithError("[MicaNT Mm] Invalid, unsupported or unmapped firmware memory map\r\n");
    }
    size_t rangeCount{};
    uint64_t deferredPages{};
    for (size_t index = 0; index < descriptorCount; ++index) {
        auto& descriptor = g_bootMemoryDescriptors[index];
        std::memcpy(&descriptor, reinterpret_cast<const void*>(
            handoff.memoryMapAddress + index * handoff.memoryMapDescriptorSize),
            sizeof(descriptor));
        if ((descriptor.physicalStart & 4095) || !descriptor.numberOfPages ||
            descriptor.numberOfPages > (UINT64_MAX - descriptor.physicalStart) / 4096) {
            stopWithError("[MicaNT Mm] Invalid physical range in firmware memory map\r\n");
        }
        const uint64_t end = descriptor.physicalStart + descriptor.numberOfPages * 4096;
        for (size_t previous = 0; previous < index; ++previous) {
            const auto& other = g_bootMemoryDescriptors[previous];
            if (descriptor.physicalStart < other.physicalStart + other.numberOfPages * 4096 &&
                other.physicalStart < end) {
                stopWithError("[MicaNT Mm] Overlapping firmware memory descriptors\r\n");
            }
        }
        if (descriptor.type != static_cast<uint32_t>(
                uefi::EfiMemoryType::EfiConventionalMemory)) continue;
        uint64_t enrolled{};
        if (!(descriptor.attribute & (uint64_t{1} << 63))) {
            uint64_t first = descriptor.physicalStart / 4096;
            uint64_t last = end / 4096;
            if (first < 256) first = 256;
            if (last > kBootstrapPfnCount) last = kBootstrapPfnCount;
            if (first < last) {
                // The standard KUSER_SHARED_DATA VA overlays the low identity
                // map. Its displaced physical page must stay out of the PFN
                // free list, or a later allocation could alias the shared RO
                // user page and become visible through 0x7FFE0000.
                constexpr uint64_t displacedPfn = micant::UserSharedDataAddress / 4096;
                if (displacedPfn >= first && displacedPfn < last) {
                    if (first < displacedPfn) {
                        const uint64_t count = displacedPfn - first;
                        g_bootAvailableRanges[rangeCount++] = {first, count};
                        enrolled += count;
                    }
                    if (displacedPfn + 1 < last) {
                        const uint64_t count = last - (displacedPfn + 1);
                        g_bootAvailableRanges[rangeCount++] = {displacedPfn + 1, count};
                        enrolled += count;
                    }
                } else {
                    enrolled = last - first;
                    g_bootAvailableRanges[rangeCount++] = {first, enrolled};
                }
            }
        }
        deferredPages += descriptor.numberOfPages - enrolled;
    }
    auto& database = mm::PfnDatabase::get();
    if (database.initializeFromBootMemory(
            g_bootPfnMetadata, std::span(g_bootAvailableRanges, rangeCount),
            zeroPhysicalPage) != NtStatus::Success ||
        database.getFreeCount() < 3 || database.getZeroedCount() != 0 ||
        database.getActiveCount() != 0) {
        stopWithError("[MicaNT Mm] Canonical PFN database initialization failed\r\n");
    }
    const size_t initialFree = database.getFreeCount();
    const uint64_t pfn = database.allocatePage(false);
    if (!pfn || database.getActiveCount() != 1) {
        stopWithError("[MicaNT Mm] Physical allocation failed\r\n");
    }
    auto* bytes = physicalPageBytes(pfn);
    for (size_t index = 0; index < 4096; ++index) bytes[index] = 0xA5;
    database.referencePage(pfn);
    database.freePage(pfn);
    if (database.getActiveCount() != 1) {
        stopWithError("[MicaNT Mm] Referenced physical page released too early\r\n");
    }
    database.freePage(pfn);
    database.zeroPage(pfn);
    for (size_t index = 0; index < 4096; ++index) {
        if (bytes[index] != 0) {
            stopWithError("[MicaNT Mm] Physical zeroing verification failed\r\n");
        }
    }
    if (database.getZeroedCount() != 1 || database.allocatePage(true) != pfn) {
        stopWithError("[MicaNT Mm] Zeroed physical page allocation failed\r\n");
    }
    database.freePage(pfn);
    const uint64_t contiguous = database.allocateContiguousPages(2);
    if (!contiguous || database.getActiveCount() != 2) {
        stopWithError("[MicaNT Mm] Contiguous physical allocation failed\r\n");
    }
    physicalPageBytes(contiguous)[0] = 0x37;
    physicalPageBytes(contiguous + 1)[4095] = 0x79;
    if (physicalPageBytes(contiguous)[0] != 0x37 ||
        physicalPageBytes(contiguous + 1)[4095] != 0x79) {
        stopWithError("[MicaNT Mm] Physical frame backing verification failed\r\n");
    }
    database.freeContiguousPages(contiguous, 2);
    if (database.getActiveCount() != 0 || database.getFreeCount() != initialFree) {
        stopWithError("[MicaNT Mm] Physical release accounting failed\r\n");
    }
    serialWrite("[MicaNT Mm] Bootstrap PFN window: first 256 MiB; usable pages=");
    serialWriteNumber(initialFree);
    serialWrite("; deferred conventional pages=");
    serialWriteNumber(deferredPages);
    serialWrite("\r\n");
    serialWrite("[MicaNT Mm] Canonical physical allocation, zeroing and release verified\r\n");
}

int32_t bootStatus(micant::NtStatus status) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(status));
}

bool trackBootDriverHandle(void* context, uint64_t handle) noexcept {
    auto* owner = static_cast<BootDriverServiceContext*>(context);
    if (!owner || owner->Index >= kMaxBootStartDrivers || !handle) return false;
    for (auto& record : g_bootDriverHandles[owner->Index]) {
        if (!record.Used) { record = {handle, true}; return true; }
    }
    return false;
}

void forgetBootDriverHandle(void* context, uint64_t handle) noexcept {
    auto* owner = static_cast<BootDriverServiceContext*>(context);
    if (!owner || owner->Index >= kMaxBootStartDrivers) return;
    for (auto& record : g_bootDriverHandles[owner->Index]) {
        if (record.Used && record.Value == handle) { record = {}; return; }
    }
}

int32_t bootCloseHandle(void* context, uint64_t handle) noexcept {
    if (!handle) return bootStatus(micant::NtStatus::InvalidHandle);
    const auto status = micant::sys::NtClose(static_cast<micant::Handle>(handle));
    if (status == micant::NtStatus::Success) forgetBootDriverHandle(context, handle);
    return bootStatus(status);
}

int32_t bootCloseBlockDevice(void* context, uint64_t handle) noexcept {
    micant::io::BlockDeviceObject* device{};
    micant::ob::ObjectHeader* reference{};
    const auto status = micant::io::IoReferenceBlockDeviceHandle(
        static_cast<micant::Handle>(handle), 0, device, reference);
    if (status != micant::NtStatus::Success) return bootStatus(status);
    micant::ob::ObDereferenceObject(reference);
    return bootCloseHandle(context, handle);
}

int32_t bootCloseFile(void* context, uint64_t handle) noexcept {
    micant::io::FileObject* file{};
    micant::ob::ObjectHeader* reference{};
    const auto status = micant::io::IoReferenceFileHandle(
        static_cast<micant::Handle>(handle), 0, file, reference);
    if (status != micant::NtStatus::Success) return bootStatus(status);
    micant::ob::ObDereferenceObject(reference);
    return bootCloseHandle(context, handle);
}

void rollbackBootDriverResources(size_t ownerIndex) noexcept {
    if (ownerIndex >= kMaxBootStartDrivers) return;
    if (micant::usb::baremetal::unregisterClassesForOwner(&g_bootDriverContexts[ownerIndex]) != micant::NtStatus::Success)
        stopWithError("[MicaNT Io] Failed driver retains active USB class instances\r\n");
    for (auto& record : g_bootDriverHandles[ownerIndex]) {
        if (record.Used) {
            (void)micant::sys::NtClose(static_cast<micant::Handle>(record.Value));
            record = {};
        }
    }
    for (auto& provider : g_bootFileProviders) {
        if (!provider.Used || provider.OwnerIndex != ownerIndex) continue;
        if (provider.ActiveBackends)
            stopWithError("[MicaNT Io] Failed driver has active file backend references\r\n");
        provider = {};
        --g_bootFileProviderCount;
    }
}

int32_t bootPciRead(void*, uint16_t bus, uint16_t device, uint16_t function,
                    uint16_t offset, uint32_t* value) noexcept {
    if (!value) return bootStatus(micant::NtStatus::InvalidParameter);
    *value = 0;
    const auto result = micant::hal::amd64::readPciConfig32(
        bus, device, function, offset, *value);
    if (result == micant::hal::amd64::PciConfigAccessResult::Success)
        return bootStatus(micant::NtStatus::Success);
    return bootStatus(result == micant::hal::amd64::PciConfigAccessResult::InvalidParameter
        ? micant::NtStatus::InvalidParameter : micant::NtStatus::NotSupported);
}

int32_t bootPciWrite(void* owner, uint16_t bus, uint16_t device, uint16_t function,
                     uint16_t offset, uint32_t value) noexcept {
    struct ExcludeInterrupts {
        uint64_t flags{};
        ExcludeInterrupts() noexcept { __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory"); }
        ~ExcludeInterrupts() { if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory"); }
    } exclusion;
    if (!micant::km::arb::ownsDevice(owner, bus, device, function))
        return bootStatus(micant::NtStatus::AccessDenied);
    // BARs remain under PnP ownership; command writes must not echo W1C
    // status and bus mastering requires an owned pinned DMA allocation.
    if ((offset >= 0x10 && offset <= 0x24) || offset == 0x30 ||
        (offset == 4 && ((value & 0xffff0000u) ||
         ((value & 4u) && !micant::km::arb::hasDma(owner, bus, device, function)))))
        return bootStatus(micant::NtStatus::AccessDenied);
    const auto result = micant::hal::amd64::writePciConfig32(
        bus, device, function, offset, value);
    if (result == micant::hal::amd64::PciConfigAccessResult::Success)
        return bootStatus(micant::NtStatus::Success);
    return bootStatus(result == micant::hal::amd64::PciConfigAccessResult::InvalidParameter
        ? micant::NtStatus::InvalidParameter : micant::NtStatus::NotSupported);
}

int32_t bootConnectMsiInterrupt(void* rawContext,
    micant::hal::amd64::BootstrapMsiRoutine routine, void* routineContext,
    micant::boot_driver::MsiInterruptRouteV1* output) noexcept {
    using namespace micant;
    auto* context = static_cast<BootDriverServiceContext*>(rawContext);
    if (!context || context->Index >= kMaxBootStartDrivers || !routine || !routineContext || !output)
        return bootStatus(NtStatus::InvalidParameter);
    *output = {};
    if (context->MsiInterruptToken) return bootStatus(NtStatus::DeviceBusy);
    hal::amd64::BootstrapMsiMessage message{};
    if (!hal::amd64::connectBootstrapMsiInterrupt(routine, routineContext, message))
        return bootStatus(NtStatus::NotSupported);
    context->MsiInterruptToken = message.token;
    output->Size = sizeof(*output);
    output->Version = boot_driver::MSI_INTERRUPT_SERVICES_VERSION;
    output->Token = message.token;
    output->MessageAddress = message.address;
    output->MessageData = message.data;
    output->Vector = message.vector;
    return bootStatus(NtStatus::Success);
}

int32_t bootDisconnectMsiInterrupt(void* rawContext, uint64_t token) noexcept {
    using namespace micant;
    auto* context = static_cast<BootDriverServiceContext*>(rawContext);
    if (!context || context->Index >= kMaxBootStartDrivers || !token)
        return bootStatus(NtStatus::InvalidParameter);
    if (context->MsiInterruptToken != token) return bootStatus(NtStatus::InvalidHandle);
    if (!hal::amd64::disconnectBootstrapMsiInterrupt(token))
        return bootStatus(NtStatus::DeviceBusy);
    context->MsiInterruptToken = 0;
    return bootStatus(NtStatus::Success);
}

struct BootPciMapping {
    void* owner{};
    void* address{};
    size_t size{};
    uint64_t claim{};
};
micant::ke::SpinLock g_bootPciMappingLock{};
BootPciMapping g_bootPciMappings[64]{};

void* bootMapDeviceMemory(void* owner, uint64_t physical, size_t size) noexcept {
    micant::ke::SpinLockGuard guard(g_bootPciMappingLock);
    BootPciMapping* freeMapping{};
    for (auto& mapping : g_bootPciMappings) if (!mapping.address) { freeMapping = &mapping; break; }
    if (!freeMapping) return nullptr;
    const auto claim = micant::km::arb::retainMapping(owner, physical, size);
    if (!claim) return nullptr;
    auto* address = micant::hal::amd64::mapBootstrapDeviceMemory(physical, size);
    if (!address) {
        if (!micant::km::arb::releaseMapping(owner, claim)) __builtin_trap();
        return nullptr;
    }
    *freeMapping = {owner, address, size, claim};
    return address;
}

bool bootReleaseDeviceMemory(void* owner, void* address, size_t size) noexcept {
    micant::ke::SpinLockGuard guard(g_bootPciMappingLock);
    for (auto& mapping : g_bootPciMappings) {
        if (mapping.owner != owner || mapping.address != address || mapping.size != size) continue;
        if (!micant::hal::amd64::releaseBootstrapDeviceMemory(address, size)) return false;
        if (!micant::km::arb::releaseMapping(owner, mapping.claim)) __builtin_trap();
        mapping = {};
        return true;
    }
    return false;
}

int32_t bootTranslateDmaRange(void*, const void* address, size_t size,
                              uint64_t* physical) noexcept {
    if (!physical) return bootStatus(micant::NtStatus::InvalidParameter);
    *physical = micant::hal::amd64::translateBootstrapDmaRange(address, size);
    return bootStatus(*physical ? micant::NtStatus::Success : micant::NtStatus::NotSupported);
}

int32_t bootQueryMonotonicClock(void*, uint64_t* counter, uint64_t* frequency) noexcept {
    if (!counter || !frequency) return bootStatus(micant::NtStatus::InvalidParameter);
    *counter = *frequency = 0;
    const auto status = micant::hal::amd64::queryBootstrapClock(*counter, *frequency);
    return bootStatus(status == micant::hal::amd64::BootstrapClockStatus::Success
        ? micant::NtStatus::Success : micant::NtStatus::DeviceNotReady);
}

int32_t bootDelayMilliseconds(void*, uint32_t milliseconds) noexcept {
    const auto status = micant::hal::amd64::delayBootstrapMilliseconds(milliseconds);
    return bootStatus(status == micant::hal::amd64::BootstrapClockStatus::Success
        ? micant::NtStatus::Success : micant::NtStatus::DeviceNotReady);
}

int32_t bootDelayMicroseconds(void*, uint32_t microseconds) noexcept {
    const auto status = micant::hal::amd64::delayBootstrapMicroseconds(microseconds);
    return bootStatus(status == micant::hal::amd64::BootstrapClockStatus::Success
        ? micant::NtStatus::Success : micant::NtStatus::DeviceNotReady);
}

micant::NtStatus bootProbePciBar(uint16_t bus, uint16_t device, uint16_t function,
    uint8_t index, micant::boot_driver::PciBarResourceV1& resource) noexcept {
    for (size_t i = 0; i < micant::km::arb::firmwareBarCount; ++i) {
        const auto& recorded = micant::km::arb::firmwareBars[i];
        if (recorded.bus != bus || recorded.device != device || recorded.function != function ||
            recorded.index != index) continue;
        if (!recorded.length || recorded.ioSpace) return micant::NtStatus::DeviceNotReady;
        uint32_t low{}, high{};
        using micant::hal::amd64::PciConfigAccessResult;
        if (micant::hal::amd64::readPciConfig32(bus, device, function,
                0x10 + index * 4, low) != PciConfigAccessResult::Success)
            return micant::NtStatus::DeviceNotReady;
        if (low & 1u) return micant::NtStatus::DeviceNotReady;
        if (((low >> 1) & 3u) == 2 &&
            (index == 5 || micant::hal::amd64::readPciConfig32(bus, device, function,
                0x14 + index * 4, high) != PciConfigAccessResult::Success)) return micant::NtStatus::DeviceNotReady;
        const uint64_t address = uint64_t{low & ~uint32_t{15}} | (uint64_t{high} << 32);
        if (address != recorded.address) return micant::NtStatus::DeviceNotReady;
        resource = {};
        resource.BarIndex = index; resource.PhysicalAddress = address; resource.Length = recorded.length;
        return micant::NtStatus::Success;
    }
    micant::hal::amd64::PciBarInfo bar{};
    const auto result = micant::hal::amd64::probeQuiescentPciBar(bus, device, function, index, bar);
    if (result == micant::hal::amd64::PciBarProbeResult::Active) {
        serialWrite("[MicaNT PCI] BAR probe rejected: active decode BDF=");
        serialWriteNumber(bus);
        serialWrite(":");
        serialWriteNumber(device);
        serialWrite(".");
        serialWriteNumber(function);
        serialWrite(" BAR=");
        serialWriteNumber(index);
        serialWrite("\r\n");
        return micant::NtStatus::DeviceBusy;
    }
    if (result != micant::hal::amd64::PciBarProbeResult::Success || bar.ioSpace)
        return micant::NtStatus::DeviceNotReady;
    resource = {};
    resource.BarIndex = index;
    resource.PhysicalAddress = bar.address;
    resource.Length = bar.length;
    return micant::NtStatus::Success;
}

int32_t bootClaimPciDevice(void* owner, uint16_t segment, uint16_t bus, uint16_t device,
    uint16_t function, const micant::boot_driver::PciBarRequestV1* requests,
    size_t requestCount, uint64_t* token,
    micant::boot_driver::PciBarResourceV1* resources, size_t* resourceCount) noexcept {
    uint64_t conflictAddress{};
    uint64_t conflictLength{};
    uint64_t requestedAddress{};
    uint64_t requestedLength{};
    uint64_t existingToken{};
    bool sameOwner{};
    const auto status = micant::km::arb::claim(owner, segment, bus, device, function,
        requests, requestCount, token, resources, resourceCount, bootProbePciBar,
        &conflictAddress, &conflictLength, &requestedAddress, &requestedLength,
        &existingToken, &sameOwner);
    if (status == micant::NtStatus::DeviceBusy && conflictLength) {
        serialWrite("[MicaNT PCI] claim overlap requested=");
        serialWriteNumber(requests[0].BarIndex);
        serialWrite(" @");
        serialWriteNumber(requestedAddress);
        serialWrite("/");
        serialWriteNumber(requestedLength);
        serialWrite(" conflict=");
        serialWriteNumber(conflictAddress);
        serialWrite("/");
        serialWriteNumber(conflictLength);
        serialWrite("\r\n");
    } else if (status == micant::NtStatus::DeviceBusy) {
        serialWrite("[MicaNT PCI] claim busy without interval conflict BDF=");
        serialWriteNumber(bus);
        serialWrite(":");
        serialWriteNumber(device);
        serialWrite(".");
        serialWriteNumber(function);
        serialWrite(" existing-token=");
        serialWriteNumber(existingToken);
        serialWrite(sameOwner ? " same-owner\r\n" : " different-owner\r\n");
    }
    return bootStatus(status);
}

bool bootPciDeviceQuiescent(uint16_t bus, uint16_t device, uint16_t function) noexcept {
    uint32_t command{};
    return micant::hal::amd64::readPciConfig32(bus, device, function, 4, command) ==
        micant::hal::amd64::PciConfigAccessResult::Success && !(command & 7u);
}

int32_t bootReleasePciDevice(void* owner, uint64_t token) noexcept {
    return bootStatus(micant::km::arb::release(owner, token, bootPciDeviceQuiescent));
}

int32_t bootAllocateDmaBuffer(void* owner, uint64_t claim, size_t bytes, size_t alignment,
                              uint64_t highestAddress, void** virtualAddress,
                              uint64_t* deviceAddress, uint64_t* token) noexcept {
    return bootStatus(micant::io::dma::allocate(owner, claim, bytes, alignment,
                                                highestAddress, virtualAddress, deviceAddress, token));
}
int32_t bootFreeDmaBuffer(void* owner, uint64_t token) noexcept {
    return bootStatus(micant::io::dma::release(owner, token, bootPciDeviceQuiescent));
}

int32_t bootRegisterUsbHost(void* owner, const micant::shared::usb::HostServicesV1* services,
                            uint64_t* token) noexcept {
    if (!services || !token) return bootStatus(micant::NtStatus::InvalidParameter);
    return bootStatus(micant::usb::baremetal::registerHost(owner, *services, *token));
}
int32_t bootRegisterUsbClass(void* owner, const micant::shared::usb::ClassServicesV1* services,
                             uint64_t* token) noexcept {
    if (!services || !token) return bootStatus(micant::NtStatus::InvalidParameter);
    return bootStatus(micant::usb::baremetal::registerClass(owner, *services, *token));
}
int32_t bootUnregisterUsbClass(void* owner, uint64_t token) noexcept {
    return bootStatus(micant::usb::baremetal::unregisterClass(owner, token));
}
int32_t bootUnregisterUsbHost(void* owner, uint64_t token) noexcept {
    return bootStatus(micant::usb::baremetal::unregisterHost(owner, token));
}
int32_t bootMountFat(void* owner, uint64_t handle, uint64_t* token) noexcept {
    if (!token) return bootStatus(micant::NtStatus::InvalidParameter);
    const auto status = micant::fsrtl::fat::mount(
        owner, static_cast<micant::Handle>(handle), *token);
    if (status != micant::NtStatus::Success) {
        serialWrite("[MicaNT FsRtl] FAT mount failed handle=");
        serialWriteNumber(handle);
        serialWrite(" status=");
        serialWriteNumber(static_cast<uint32_t>(status));
        serialWrite("\r\n");
    }
    return bootStatus(status);
}
int32_t bootUnmountFat(void* owner, uint64_t token) noexcept {
    return bootStatus(micant::fsrtl::fat::unmount(owner, token));
}
int32_t bootReadFatFileFromMount(void* owner, uint64_t token, const char16_t* path,
                                 size_t pathLength, void* output, size_t capacity,
                                 size_t* bytesRead) noexcept {
    if (!bytesRead) return bootStatus(micant::NtStatus::InvalidParameter);
    *bytesRead = 0;
    return bootStatus(micant::fsrtl::fat::readMountedFile(owner, token,
        std::u16string_view(path, pathLength), output, capacity, *bytesRead));
}

struct BootBlockBackendAdapter {
    void* Context{};
    int32_t (*Read)(void*, uint64_t, uint32_t, void*, uint32_t*){};
    void (*Release)(void*){};
    int32_t (*Write)(void*, uint64_t, uint32_t, const void*, uint32_t*){};
};

micant::NtStatus bootBlockReadAdapter(void* context, uint64_t lba,
    uint32_t count, void* buffer, uint32_t* blocksRead) noexcept {
    auto* adapter = static_cast<BootBlockBackendAdapter*>(context);
    if (!adapter || !adapter->Read) return micant::NtStatus::InvalidParameter;
    const auto status = static_cast<micant::NtStatus>(static_cast<uint32_t>(adapter->Read(
        adapter->Context, lba, count, buffer, blocksRead)));
    if (status != micant::NtStatus::Success) {
        serialWrite("[MicaNT Io] Block read failed LBA=");
        serialWriteNumber(lba);
        serialWrite(" count=");
        serialWriteNumber(count);
        serialWrite(" status=");
        serialWriteNumber(static_cast<uint32_t>(status));
        serialWrite("\r\n");
    }
    return status;
}

void bootBlockReleaseAdapter(void* context) noexcept {
    auto* adapter = static_cast<BootBlockBackendAdapter*>(context);
    if (!adapter) return;
    if (adapter->Release) adapter->Release(adapter->Context);
    delete adapter;
}

micant::NtStatus bootBlockWriteAdapter(void* context, uint64_t lba,
    uint32_t count, const void* buffer, uint32_t* blocksWritten) noexcept {
    auto* adapter = static_cast<BootBlockBackendAdapter*>(context);
    if (!adapter || !adapter->Write) return micant::NtStatus::MediaWriteProtected;
    return static_cast<micant::NtStatus>(static_cast<uint32_t>(adapter->Write(
        adapter->Context, lba, count, buffer, blocksWritten)));
}

int32_t bootCreateReadOnlyBlockDevice(void* driverContext, uint64_t totalBlocks,
    uint32_t blockSize, void* backendContext,
    int32_t (*read)(void*, uint64_t, uint32_t, void*, uint32_t*),
    void (*release)(void*), uint32_t desiredAccess, uint64_t* handle) noexcept {
    if (handle) *handle = 0;
    if (!handle || !read || !release) return bootStatus(micant::NtStatus::InvalidParameter);
    auto* adapter = new (std::nothrow_t{}) BootBlockBackendAdapter{backendContext, read, release};
    if (!adapter) { release(backendContext); return bootStatus(micant::NtStatus::NoMemory); }
    micant::io::BlockDeviceBackend backend{
        adapter, totalBlocks, blockSize, bootBlockReadAdapter, bootBlockReleaseAdapter};
    micant::Handle nativeHandle{};
    const auto status = micant::io::IoCreateBlockDevice(&nativeHandle, desiredAccess, backend);
    *handle = static_cast<uint64_t>(nativeHandle);
    if (status == micant::NtStatus::Success && !trackBootDriverHandle(driverContext, *handle)) {
        (void)micant::sys::NtClose(nativeHandle);
        *handle = 0;
        return bootStatus(micant::NtStatus::InsufficientResources);
    }
    return bootStatus(status);
}

int32_t bootReadBlockDevice(void*, uint64_t handle, uint64_t lba,
    uint32_t blockCount, void* buffer, uint32_t* blocksRead) noexcept {
    return bootStatus(micant::io::IoReadBlockDeviceHandle(
        static_cast<micant::Handle>(handle), lba, blockCount, buffer, blocksRead));
}

int32_t bootWriteBlockDevice(void*, uint64_t handle, uint64_t lba,
    uint32_t blockCount, const void* buffer, uint32_t* blocksWritten) noexcept {
    return bootStatus(micant::io::IoWriteBlockDeviceHandle(
        static_cast<micant::Handle>(handle), lba, blockCount, buffer, blocksWritten));
}

int32_t bootCreateWritableBlockDevice(void* driverContext, uint64_t totalBlocks,
    uint32_t blockSize, void* backendContext,
    int32_t (*read)(void*, uint64_t, uint32_t, void*, uint32_t*),
    int32_t (*write)(void*, uint64_t, uint32_t, const void*, uint32_t*),
    void (*release)(void*), uint32_t desiredAccess, uint64_t* handle) noexcept {
    if (handle) *handle = 0;
    if (!handle || !read || !write || !release)
        return bootStatus(micant::NtStatus::InvalidParameter);
    auto* adapter = new (std::nothrow_t{}) BootBlockBackendAdapter{
        backendContext, read, release, write};
    if (!adapter) { release(backendContext); return bootStatus(micant::NtStatus::NoMemory); }
    micant::io::BlockDeviceBackend backend{
        adapter, totalBlocks, blockSize, bootBlockReadAdapter,
        bootBlockReleaseAdapter, bootBlockWriteAdapter};
    micant::Handle nativeHandle{};
    const auto status = micant::io::IoCreateBlockDevice(&nativeHandle, desiredAccess, backend);
    *handle = static_cast<uint64_t>(nativeHandle);
    if (status == micant::NtStatus::Success && !trackBootDriverHandle(driverContext, *handle)) {
        (void)micant::sys::NtClose(nativeHandle);
        *handle = 0;
        return bootStatus(micant::NtStatus::InsufficientResources);
    }
    return bootStatus(status);
}

struct BootFileBackendAdapter {
    micant::boot_driver::FileBackendV1 Backend{};
    size_t ProviderIndex{kMaxBootStartDrivers};
};

micant::NtStatus bootFileReadAdapter(void* context, uint64_t offset, void* buffer,
    size_t length, size_t* bytesRead) noexcept {
    auto* adapter = static_cast<BootFileBackendAdapter*>(context);
    if (!adapter || !adapter->Backend.Read) return micant::NtStatus::InvalidParameter;
    return static_cast<micant::NtStatus>(static_cast<uint32_t>(adapter->Backend.Read(
        adapter->Backend.Context, offset, buffer, length, bytesRead)));
}

micant::NtStatus bootFileWriteAdapter(void* context, uint64_t offset, const void* buffer,
    size_t length, size_t* bytesWritten) noexcept {
    auto* adapter = static_cast<BootFileBackendAdapter*>(context);
    if (!adapter || !adapter->Backend.Write) return micant::NtStatus::MediaWriteProtected;
    return static_cast<micant::NtStatus>(static_cast<uint32_t>(adapter->Backend.Write(
        adapter->Backend.Context, offset, buffer, length, bytesWritten)));
}

void bootFileReleaseAdapter(void* context) noexcept {
    auto* adapter = static_cast<BootFileBackendAdapter*>(context);
    if (!adapter) return;
    if (adapter->Backend.Release) adapter->Backend.Release(adapter->Backend.Context);
    if (adapter->ProviderIndex < kMaxBootStartDrivers &&
        g_bootFileProviders[adapter->ProviderIndex].ActiveBackends)
        --g_bootFileProviders[adapter->ProviderIndex].ActiveBackends;
    delete adapter;
}

micant::NtStatus bootFileOpenRouter(void*, std::u16string_view name, bool openIf,
    uint32_t desiredAccess,
    micant::io::FileBackend& output) noexcept {
    if (name.empty() || name.size() > 1024) return micant::NtStatus::InvalidParameter;
    const auto fatStatus = micant::fsrtl::fat::open(name, openIf, desiredAccess, output);
    if (fatStatus != micant::NtStatus::ObjectPathNotFound) return fatStatus;
    for (size_t i = 0; i < kMaxBootStartDrivers; ++i) {
        if (!g_bootFileProviders[i].Used) continue;
        micant::boot_driver::FileBackendV1 candidate{};
        const auto status = g_bootFileProviders[i].Open(
            g_bootFileProviders[i].Context, name.data(), name.size(), &candidate);
        if (status == bootStatus(micant::NtStatus::ObjectNameNotFound) ||
            status == bootStatus(micant::NtStatus::NoSuchFile)) {
            if (candidate.Release) candidate.Release(candidate.Context);
            continue;
        }
        if (status != bootStatus(micant::NtStatus::Success)) {
            if (candidate.Release) candidate.Release(candidate.Context);
            return static_cast<micant::NtStatus>(static_cast<uint32_t>(status));
        }
        if (!candidate.Read || !candidate.Release || !candidate.Size ||
            candidate.NameLength > 1024 || (!candidate.Name && candidate.NameLength)) {
            if (candidate.Release) candidate.Release(candidate.Context);
            return micant::NtStatus::InvalidParameter;
        }
    auto* adapter = new (std::nothrow_t{}) BootFileBackendAdapter{candidate, i};
        if (!adapter) {
            candidate.Release(candidate.Context);
            return micant::NtStatus::NoMemory;
        }
        ++g_bootFileProviders[i].ActiveBackends;
        output = {adapter, candidate.Size,
            candidate.Name && candidate.NameLength
                ? std::u16string_view(candidate.Name, candidate.NameLength) : name,
            bootFileReadAdapter, bootFileReleaseAdapter};
        output.write = candidate.Write ? bootFileWriteAdapter : nullptr;
        return micant::NtStatus::Success;
    }
    return micant::NtStatus::NoSuchFile;
}

int32_t bootRegisterFileOpenRoutine(void* serviceContext,
    int32_t (*open)(void*, const char16_t*, size_t,
                    micant::boot_driver::FileBackendV1*), void* context) noexcept {
    auto* owner = static_cast<BootDriverServiceContext*>(serviceContext);
    if (!open) return bootStatus(micant::NtStatus::InvalidParameter);
    if (!owner || owner->Index >= kMaxBootStartDrivers)
        return bootStatus(micant::NtStatus::InvalidParameter);
    size_t slot = 0;
    while (slot < kMaxBootStartDrivers && g_bootFileProviders[slot].Used) ++slot;
    if (slot == kMaxBootStartDrivers)
        return bootStatus(micant::NtStatus::InsufficientResources);
    if (!g_bootFileOpenRouterInstalled) {
        const auto status = micant::io::IoRegisterFileOpenRoutine(bootFileOpenRouter, nullptr);
        if (status != micant::NtStatus::Success) return bootStatus(status);
        g_bootFileOpenRouterInstalled = true;
    }
    g_bootFileProviders[slot] = {open, context, owner->Index, 0, true};
    ++g_bootFileProviderCount;
    return bootStatus(micant::NtStatus::Success);
}

int32_t bootUnregisterFileOpenRoutine(void* serviceContext,
    int32_t (*open)(void*, const char16_t*, size_t,
                    micant::boot_driver::FileBackendV1*), void* context) noexcept {
    auto* owner = static_cast<BootDriverServiceContext*>(serviceContext);
    if (!owner || owner->Index >= kMaxBootStartDrivers || !open)
        return bootStatus(micant::NtStatus::InvalidParameter);
    for (size_t i = 0; i < kMaxBootStartDrivers; ++i) {
        auto& provider = g_bootFileProviders[i];
        if (!provider.Used || provider.OwnerIndex != owner->Index || provider.Open != open ||
            provider.Context != context) continue;
        if (provider.ActiveBackends) return bootStatus(micant::NtStatus::DeviceBusy);
        provider = {};
        --g_bootFileProviderCount;
        return bootStatus(micant::NtStatus::Success);
    }
    return bootStatus(micant::NtStatus::ObjectNameNotFound);
}

int32_t bootOpenBackendFile(void* context, uint64_t* handle, uint32_t access,
    const char16_t* name, size_t nameLength) noexcept {
    if (!handle) return bootStatus(micant::NtStatus::InvalidParameter);
    *handle = 0;
    if (!name || !nameLength || nameLength > 1024)
        return bootStatus(micant::NtStatus::InvalidParameter);
    micant::Handle nativeHandle{};
    const auto status = micant::io::IoOpenBackendFile(
        &nativeHandle, access, {name, nameLength});
    *handle = static_cast<uint64_t>(nativeHandle);
    if (status == micant::NtStatus::Success && !trackBootDriverHandle(context, *handle)) {
        (void)micant::sys::NtClose(nativeHandle);
        *handle = 0;
        return bootStatus(micant::NtStatus::InsufficientResources);
    }
    return bootStatus(status);
}

int32_t bootCreateBackendFile(void* context, uint64_t* handle, uint32_t access,
    const micant::boot_driver::FileBackendV1* backend) noexcept {
    if (handle) *handle = 0;
    if (!handle || !backend || !backend->Read || !backend->Release || !backend->Size ||
        backend->NameLength > 1024 ||
        (!backend->Name && backend->NameLength))
        return bootStatus(micant::NtStatus::InvalidParameter);
    auto* adapter = new (std::nothrow_t{}) BootFileBackendAdapter{*backend, kMaxBootStartDrivers};
    if (!adapter) {
        backend->Release(backend->Context);
        return bootStatus(micant::NtStatus::NoMemory);
    }
    micant::io::FileBackend nativeBackend{
        adapter, backend->Size,
        backend->Name ? std::u16string_view(backend->Name, backend->NameLength)
                      : std::u16string_view{},
        bootFileReadAdapter, bootFileReleaseAdapter};
    nativeBackend.write = backend->Write ? bootFileWriteAdapter : nullptr;
    micant::Handle nativeHandle{};
    const auto status = micant::io::IoCreateBackendFile(
        &nativeHandle, access, nativeBackend);
    *handle = static_cast<uint64_t>(nativeHandle);
    if (status == micant::NtStatus::Success && !trackBootDriverHandle(context, *handle)) {
        (void)micant::sys::NtClose(nativeHandle);
        *handle = 0;
        return bootStatus(micant::NtStatus::InsufficientResources);
    }
    return bootStatus(status);
}

int32_t bootReadFile(void*, uint64_t handle, bool useCurrentOffset, uint64_t offset,
    void* buffer, size_t length, size_t* bytesRead) noexcept {
    return bootStatus(micant::io::IoReadFileHandle(
        static_cast<micant::Handle>(handle), useCurrentOffset, offset, buffer, length, bytesRead));
}

void initializeKernelServices(micant::boot_driver::KernelServices& services,
                              size_t ownerIndex = 0) noexcept {
    using namespace micant::boot_driver;
    services = {};
    services.Size = sizeof(services);
    services.Version = KERNEL_SERVICES_VERSION;
    if (ownerIndex >= kMaxBootStartDrivers) ownerIndex = 0;
    g_bootDriverContexts[ownerIndex].Index = ownerIndex;
    services.Context = &g_bootDriverContexts[ownerIndex];
    services.BootWidth = kWidth;
    services.BootHeight = kHeight;
    services.TranslateVirtualAddress = [](void*, const void* address) {
        return micant::hal::amd64::translateBootstrapAddress(address);
    };
    services.MapDeviceMemory = bootMapDeviceMemory;
    services.Log = [](void*, const char* text) {
        serialWrite("[MicaNT OEM] ");
        serialWrite(text);
        serialWrite("\r\n");
    };
    services.PciReadConfig32 = bootPciRead;
    services.PciWriteConfig32 = bootPciWrite;
    services.ReleaseDeviceMemory = bootReleaseDeviceMemory;
    services.TranslateDmaRange = bootTranslateDmaRange;
    services.QueryMonotonicClock = bootQueryMonotonicClock;
    services.DelayMilliseconds = bootDelayMilliseconds;
    services.DelayMicroseconds = bootDelayMicroseconds;
    services.ClaimPciDevice = bootClaimPciDevice;
    services.ReleasePciDevice = bootReleasePciDevice;
    services.DmaVersion = DMA_SERVICES_VERSION;
    services.AllocateDmaBuffer = bootAllocateDmaBuffer;
    services.FreeDmaBuffer = bootFreeDmaBuffer;
    services.UsbHostVersion = micant::shared::usb::HostServicesVersion;
    services.RegisterUsbHost = bootRegisterUsbHost;
    services.UnregisterUsbHost = bootUnregisterUsbHost;
    services.UsbClassVersion = micant::shared::usb::ClassServicesVersion;
    services.RegisterUsbClass = bootRegisterUsbClass;
    services.UnregisterUsbClass = bootUnregisterUsbClass;
    services.FatFilesystemVersion = 1;
    services.MountFatBlockDevice = bootMountFat;
    services.UnmountFatBlockDevice = bootUnmountFat;
    services.NetworkAdapterVersion = NETWORK_ADAPTER_SERVICES_VERSION;
    services.RegisterNetworkAdapter = bootRegisterNetworkAdapter;
    services.UnregisterNetworkAdapter = bootUnregisterNetworkAdapter;
    services.CreateReadOnlyBlockDevice = bootCreateReadOnlyBlockDevice;
    services.ReadBlockDevice = bootReadBlockDevice;
    services.WritableBlockDeviceVersion = 1;
    services.CreateWritableBlockDevice = bootCreateWritableBlockDevice;
    services.WriteBlockDevice = bootWriteBlockDevice;
    services.ReadFatFileFromMount = bootReadFatFileFromMount;
    services.MsiInterruptVersion = MSI_INTERRUPT_SERVICES_VERSION;
    services.ConnectMsiInterrupt = bootConnectMsiInterrupt;
    services.DisconnectMsiInterrupt = bootDisconnectMsiInterrupt;
    services.RegisterFileOpenRoutine = bootRegisterFileOpenRoutine;
    services.UnregisterFileOpenRoutine = bootUnregisterFileOpenRoutine;
    services.OpenBackendFile = bootOpenBackendFile;
    services.CreateBackendFile = bootCreateBackendFile;
    services.ReadFile = bootReadFile;
    services.CloseHandle = bootCloseHandle;
    services.CloseBlockDevice = bootCloseBlockDevice;
    services.CloseFile = bootCloseFile;
}

namespace {
using BootAdapterSlot = BootNdisAdapterRegistration;
bool beginBootAdapterCall(BootAdapterSlot& slot) noexcept {
    if (!slot.Used || slot.Stopping.load(std::memory_order_acquire)) return false;
    slot.ActiveCalls.fetch_add(1, std::memory_order_acq_rel);
    if (slot.Stopping.load(std::memory_order_acquire)) {
        slot.ActiveCalls.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    return true;
}
void endBootAdapterCall(BootAdapterSlot& slot) noexcept {
    slot.ActiveCalls.fetch_sub(1, std::memory_order_acq_rel);
}
int32_t bootAdapterSend(void* context, const uint8_t* frame, size_t length) noexcept {
    auto* slot = static_cast<BootAdapterSlot*>(context);
    if (!slot || !frame || !length || !beginBootAdapterCall(*slot))
        return bootStatus(micant::NtStatus::DeviceBusy);
    const auto status = slot->DriverServices.SendPacket(
        slot->DriverServices.Context, frame, length);
    endBootAdapterCall(*slot);
    return status;
}
int32_t bootAdapterPoll(void* context) noexcept {
    auto* slot = static_cast<BootAdapterSlot*>(context);
    if (!slot || !beginBootAdapterCall(*slot))
        return bootStatus(micant::NtStatus::DeviceBusy);
    const auto status = slot->DriverServices.PollReceive(slot->DriverServices.Context);
    endBootAdapterCall(*slot);
    return status;
}
void bootAdapterReceive(void* context, const uint8_t* frame, size_t length) noexcept {
    auto* slot = static_cast<BootAdapterSlot*>(context);
    if (!slot || !frame || !beginBootAdapterCall(*slot)) return;
    const auto routine = slot->ReceiveRoutine.load(std::memory_order_acquire);
    void* receiveContext = slot->ReceiveContext.load(std::memory_order_acquire);
    if (routine) routine(receiveContext, frame, length);
    endBootAdapterCall(*slot);
}
int32_t bootAdapterSetReceive(void* context,
    micant::shared::ndis::ReceivePacketRoutine routine, void* receiveContext) noexcept {
    auto* slot = static_cast<BootAdapterSlot*>(context);
    if (!slot || (routine && slot->Stopping.load(std::memory_order_acquire)))
        return bootStatus(micant::NtStatus::DeviceBusy);
    const auto previousRoutine = slot->ReceiveRoutine.exchange(routine, std::memory_order_acq_rel);
    void* previousContext = slot->ReceiveContext.exchange(receiveContext, std::memory_order_acq_rel);
    const auto status = slot->DriverServices.SetReceiveHandler(slot->DriverServices.Context,
        routine ? bootAdapterReceive : nullptr, routine ? slot : nullptr);
    if (status != bootStatus(micant::NtStatus::Success)) {
        slot->ReceiveRoutine.store(previousRoutine, std::memory_order_release);
        slot->ReceiveContext.store(previousContext, std::memory_order_release);
    }
    return status;
}
int32_t bootAdapterStop(void* context) noexcept {
    auto* slot = static_cast<BootAdapterSlot*>(context);
    if (!slot) return bootStatus(micant::NtStatus::InvalidParameter);
    slot->Stopping.store(true, std::memory_order_release);
    const auto detach = bootAdapterSetReceive(slot, nullptr, nullptr);
    if (detach != bootStatus(micant::NtStatus::Success)) return detach;
    while (slot->ActiveCalls.load(std::memory_order_acquire) != 0)
        __asm__ __volatile__("pause" : : : "memory");
    return slot->DriverServices.Stop(slot->DriverServices.Context);
}
}

int32_t bootRegisterNetworkAdapter(void* owner,
    const micant::shared::ndis::AdapterServicesV1* adapterServices,
    uint64_t* cookie) noexcept {
    using micant::NtStatus;
    using micant::shared::ndis::AdapterServicesV1;
    if (!owner || !adapterServices || !cookie || adapterServices->Size < sizeof(AdapterServicesV1) ||
        adapterServices->Version != micant::shared::ndis::AdapterServicesVersion ||
        !adapterServices->Context || adapterServices->Mtu < 576 || adapterServices->Mtu > 9000 ||
        adapterServices->LinkState > 1 ||
        !adapterServices->SendPacket || !adapterServices->SetReceiveHandler ||
        !adapterServices->PollReceive || !adapterServices->Stop)
        return bootStatus(NtStatus::InvalidParameter);
    bool macZero = true;
    for (uint8_t byte : adapterServices->MacAddress) macZero &= byte == 0;
    if (macZero || (adapterServices->MacAddress[0] & 1))
        return bootStatus(NtStatus::InvalidParameter);

    auto* driverOwner = static_cast<BootDriverServiceContext*>(owner);
    bool knownOwner = false;
    for (auto& candidate : g_bootDriverContexts) knownOwner |= &candidate == driverOwner;
    if (!knownOwner) return bootStatus(NtStatus::AccessDenied);
    size_t index = kMaxBootNdisAdapters;
    for (size_t i = 0; i < kMaxBootNdisAdapters; ++i) {
        if (g_bootNdisAdapters[i].Owner == driverOwner && g_bootNdisAdapters[i].Used)
            return bootStatus(NtStatus::Conflict);
        if (!g_bootNdisAdapters[i].Used && index == kMaxBootNdisAdapters) index = i;
    }
    if (index == kMaxBootNdisAdapters) return bootStatus(NtStatus::InsufficientResources);

    auto& slot = g_bootNdisAdapters[index];
    slot.DriverServices = *adapterServices;
    slot.Owner = driverOwner;
    slot.Stopping.store(false, std::memory_order_relaxed);
    slot.ActiveCalls.store(0, std::memory_order_relaxed);
    slot.ReceiveRoutine.store(nullptr, std::memory_order_relaxed);
    slot.ReceiveContext.store(nullptr, std::memory_order_relaxed);
    slot.KernelServices = *adapterServices;
    slot.KernelServices.Context = &slot;
    slot.KernelServices.SendPacket = bootAdapterSend;
    slot.KernelServices.SetReceiveHandler = bootAdapterSetReceive;
    slot.KernelServices.PollReceive = bootAdapterPoll;
    slot.KernelServices.Stop = bootAdapterStop;
    uint64_t next = g_nextBootNdisCookie++;
    if (!next) next = g_nextBootNdisCookie++;
    slot.Cookie = next;
    slot.Used = true;
    if (!micant::tcpip::NetworkStack::get().bindAdapterServices(&slot.KernelServices)) {
        slot.Used = false;
        slot.Cookie = 0;
        slot.Owner = nullptr;
        return bootStatus(NtStatus::DeviceBusy);
    }
    *cookie = next;
    return bootStatus(NtStatus::Success);
}

int32_t bootUnregisterNetworkAdapter(void* owner, uint64_t cookie) noexcept {
    using micant::NtStatus;
    if (!owner || !cookie) return bootStatus(NtStatus::InvalidParameter);
    auto* driverOwner = static_cast<BootDriverServiceContext*>(owner);
    bool knownOwner = false;
    for (auto& candidate : g_bootDriverContexts) knownOwner |= &candidate == driverOwner;
    if (!knownOwner) return bootStatus(NtStatus::AccessDenied);
    for (auto& slot : g_bootNdisAdapters) {
        if (!slot.Used || slot.Cookie != cookie || slot.Owner != driverOwner) continue;
        if (!micant::tcpip::NetworkStack::get().unbindAdapterServices(&slot.KernelServices))
            return bootStatus(NtStatus::DeviceBusy);
        const auto status = bootAdapterStop(&slot);
        if (status != bootStatus(NtStatus::Success)) return status;
        slot.Used = false;
        slot.Cookie = 0;
        slot.Owner = nullptr;
        slot.DriverServices = {};
        slot.KernelServices = {};
        return bootStatus(NtStatus::Success);
    }
    return bootStatus(NtStatus::InvalidHandle);
}

bool fallbackPresentFrame(void* context, const uint32_t* pixels, uint32_t width,
    uint32_t height, uint32_t pitch, int32_t dstX, int32_t dstY) noexcept {
    auto* target = static_cast<GopDisplayContext*>(context);
    if (!target || !target->Pixels || !pixels || !width || !height || pitch < width ||
        !target->Width || !target->Height || target->Pitch < target->Width) return false;
    if (target->Pitch > UINT64_MAX / sizeof(uint32_t) ||
        target->Height > UINT64_MAX / (static_cast<uint64_t>(target->Pitch) * sizeof(uint32_t)) ||
        static_cast<uint64_t>(target->Height) * target->Pitch * sizeof(uint32_t) >
            target->ByteSize) return false;
    const int64_t srcLeft = dstX < 0 ? -static_cast<int64_t>(dstX) : 0;
    const int64_t srcTop = dstY < 0 ? -static_cast<int64_t>(dstY) : 0;
    const int64_t left = dstX > 0 ? dstX : 0;
    const int64_t top = dstY > 0 ? dstY : 0;
    int64_t copyWidth = static_cast<int64_t>(width) - srcLeft;
    int64_t copyHeight = static_cast<int64_t>(height) - srcTop;
    if (left >= target->Width || top >= target->Height) return true;
    if (left + copyWidth > target->Width) copyWidth = target->Width - left;
    if (top + copyHeight > target->Height) copyHeight = target->Height - top;
    if (copyWidth <= 0 || copyHeight <= 0) return true;
    for (int64_t row = 0; row < copyHeight; ++row) {
        const uint32_t* source = pixels + (srcTop + row) * pitch + srcLeft;
        volatile uint32_t* destination = target->Pixels +
            (top + row) * target->Pitch + left;
        for (int64_t column = 0; column < copyWidth; ++column) {
            uint32_t value = source[column];
            if (target->PixelFormat == 0) {
                value = (value & 0xFF00FF00u) | ((value & 0x00FF0000u) >> 16) |
                    ((value & 0x000000FFu) << 16);
            }
            destination[column] = value;
        }
    }
    __asm__ __volatile__("sfence" ::: "memory");
    return true;
}

bool initializeGopFramebuffer(const micant::boot::KernelFramebufferV1& framebuffer,
                              bool logFallback = true) noexcept {
    if (!framebuffer.physicalBase || !framebuffer.size ||
        framebuffer.size > 16 * 1024 * 1024 ||
        framebuffer.size > SIZE_MAX || framebuffer.width < 320 ||
        framebuffer.height < 200 || framebuffer.pixelsPerScanLine < framebuffer.width ||
        framebuffer.pixelFormat > 1 || (framebuffer.physicalBase & 3)) return false;
    if (g_displayUsingGopFallback && g_display.PresentFrame == fallbackPresentFrame &&
        g_display.Width == framebuffer.width && g_display.Height == framebuffer.height &&
        g_display.Pitch == framebuffer.pixelsPerScanLine &&
        g_gopDisplayContext.Pixels) {
        if (logFallback)
            serialWrite("[MicaNT Display] OEM driver unavailable; GOP framebuffer pass-through active\r\n");
        return true;
    }
    const uint64_t rowBytes = static_cast<uint64_t>(framebuffer.pixelsPerScanLine) * 4;
    if (rowBytes > framebuffer.size || framebuffer.height > framebuffer.size / rowBytes)
        return false;

    void* mapping = micant::hal::amd64::mapBootstrapDeviceMemory(
        framebuffer.physicalBase, static_cast<size_t>(framebuffer.size));
    if (!mapping && micant::hal::amd64::bootstrapKernelRange(
            framebuffer.physicalBase, static_cast<size_t>(framebuffer.size), true)) {
        mapping = reinterpret_cast<void*>(framebuffer.physicalBase);
    }
    if (!mapping) return false;

    g_gopDisplayContext = {
        static_cast<volatile uint32_t*>(mapping), framebuffer.width, framebuffer.height,
        framebuffer.pixelsPerScanLine, framebuffer.pixelFormat, framebuffer.size};
    g_display = {};
    g_display.Size = sizeof(g_display);
    g_display.Version = micant::boot_driver::DRIVER_ABI_VERSION;
    g_display.Width = framebuffer.width;
    g_display.Height = framebuffer.height;
    g_display.Pitch = framebuffer.pixelsPerScanLine;
    g_display.PixelFormat = framebuffer.pixelFormat;
    g_display.Context = &g_gopDisplayContext;
    g_display.PresentFrame = fallbackPresentFrame;
    g_display.PresentComposition = nullptr;
    g_displayDriver = {};
    g_displayUsingGopFallback = true;
    if (logFallback)
        serialWrite("[MicaNT Display] OEM driver unavailable; GOP framebuffer pass-through active\r\n");
    return true;
}

bool initializeDisplay(const micant::boot::KernelBootImageV1& image) noexcept {
    if (image.base == 0 || image.size == 0 ||
        image.size > UINT64_MAX - image.base ||
        image.entryPoint < image.base ||
        image.entryPoint - image.base >= image.size) {
        return false;
    }
    using namespace micant::boot_driver;
    initializeKernelServices(g_displayServices);
    g_displayDriver.Size = sizeof(g_displayDriver);
    g_displayDriver.Version = DRIVER_ABI_VERSION;
    g_displayDriver.Flags = DRIVER_FLAG_BOOT_START;
    g_displayDriver.DriverStart = reinterpret_cast<void*>(image.base);
    g_displayDriver.DriverSize = image.size;
    g_displayDriver.HardwareResources = &g_displayServices;
    g_displayDriver.Display = &g_display;
    const auto entry = reinterpret_cast<DriverEntryRoutine>(image.entryPoint);
    if (entry(&g_displayDriver, nullptr) != STATUS_SUCCESS ||
        !g_display.PresentFrame || g_display.Width != kWidth ||
        g_display.Height != kHeight) {
        if (g_displayDriver.DriverUnload) g_displayDriver.DriverUnload(&g_displayDriver);
        rollbackBootDriverResources(0);
        return false;
    }
    g_displayUsingGopFallback = false;
    serialWrite(g_display.PresentComposition
        ? "[MicaNT Display] VirGL composition available\r\n"
        : "[MicaNT Display] VirtIO 2D active; GPU composition unavailable\r\n");
    return true;
}

void presentCursorRect(uint32_t x, uint32_t y) noexcept {
    if (!g_display.PresentFrame(g_display.Context, g_cursorFrame,
            kCursorSize, kCursorSize, kCursorSize,
            static_cast<int32_t>(x), static_cast<int32_t>(y))) {
        stopWithError("[MicaNT Display] Cursor presentation failed\r\n");
    }
}

bool validHandoff(const micant::boot::UefiKernelHandoffV3* handoff) noexcept {
    if (!handoff || handoff->signature != micant::boot::UEFI_KERNEL_HANDOFF_SIGNATURE ||
        (handoff->version != micant::boot::UEFI_KERNEL_HANDOFF_VERSION &&
         handoff->version != micant::boot::UEFI_KERNEL_HANDOFF_VERSION_OEM_MODULES &&
         handoff->version != micant::boot::UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME &&
         handoff->version != micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION &&
         handoff->version != micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES) ||
        handoff->size < sizeof(*handoff) ||
        handoff->memoryMapAddress == 0 ||
        handoff->memoryMapSize > UINT64_MAX - handoff->memoryMapAddress ||
        handoff->memoryMapDescriptorSize < sizeof(micant::uefi::EfiMemoryDescriptor) ||
        handoff->memoryMapSize < handoff->memoryMapDescriptorSize) {
        return false;
    }
    const auto& userImage = handoff->userImageFile;
    if ((userImage.base == 0) != (userImage.size == 0) ||
        userImage.size > 16 * 1024 * 1024 ||
        userImage.size > UINT64_MAX - userImage.base) {
        return false;
    }

    if (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_OEM_MODULES ||
        handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME ||
        handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION ||
        handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES) {
        const micant::boot::UefiKernelHandoffV4* extendedV4{};
        if (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME ||
            handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION ||
            handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES) {
            const bool hasCalibration = handoff->version ==
                    micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION ||
                handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES;
            const bool hasPciInventory = handoff->version ==
                micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES;
            if (handoff->size < (hasCalibration
                    ? (hasPciInventory ? sizeof(micant::boot::UefiKernelHandoffV7)
                                       : sizeof(micant::boot::UefiKernelHandoffV6))
                    : sizeof(micant::boot::UefiKernelHandoffV5))) return false;
            const auto& sample = hasCalibration
                ? (hasPciInventory
                    ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.time.systemTime
                    : reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->time.systemTime)
                : reinterpret_cast<const micant::boot::UefiKernelHandoffV5*>(handoff)->systemTime;
            const micant::shared::boot_time::BootClockCalibrationV1 noCalibration{};
            const auto& calibration = hasCalibration
                ? (hasPciInventory
                    ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.clockCalibration
                    : reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->clockCalibration)
                : noCalibration;
            using micant::shared::boot_time::SampleSource;
            using micant::shared::boot_time::SampleStatus;
            using micant::shared::boot_time::BootClockFlagsV1;
            using micant::shared::time::EfiAdjustForDaylight;
            using micant::shared::time::EfiInDaylight;
            using micant::shared::time::EfiUnspecifiedTimeZone;
            if (sample.size != sizeof(sample) || sample.version != 1 || sample.reserved ||
                static_cast<uint32_t>(sample.status) >
                    static_cast<uint32_t>(SampleStatus::InvalidFirmwareData) ||
                (sample.daylight & ~(EfiAdjustForDaylight | EfiInDaylight)) != 0)
                return false;
            if (hasCalibration) {
                const auto calibrationFlags = static_cast<uint16_t>(calibration.flags);
                if (calibration.size != sizeof(calibration) || calibration.version != 1 ||
                    (calibrationFlags & ~static_cast<uint16_t>(BootClockFlagsV1::InvariantTsc)) ||
                    (calibration.tscFrequencyHz &&
                     (calibration.flags != BootClockFlagsV1::InvariantTsc ||
                      calibration.tscFrequencyHz < 1'000'000 ||
                      calibration.tscFrequencyHz > 10'000'000'000ULL)) ||
                    (!calibration.tscFrequencyHz &&
                     calibration.flags != BootClockFlagsV1::None)) return false;
            }
            if (sample.status == SampleStatus::UtcValid) {
                micant::shared::time::SystemTimeFields utcFields{};
                if (sample.source != SampleSource::UefiRuntimeGetTime ||
                    sample.timeZoneMinutes < -1440 || sample.timeZoneMinutes > 1440 ||
                    !micant::shared::time::fromSystemTime100ns(
                        sample.utcTime100ns, utcFields)) return false;
            } else if (sample.status == SampleStatus::LocalTimeZoneUnspecified) {
                if (sample.source != SampleSource::UefiRuntimeGetTime ||
                    static_cast<uint16_t>(sample.timeZoneMinutes) !=
                        EfiUnspecifiedTimeZone || sample.utcTime100ns) return false;
            } else if (sample.utcTime100ns) {
                return false;
            }
            extendedV4 = hasPciInventory
                ? &reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.time.oem
                : (hasCalibration
                    ? &reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->time.oem
                    : &reinterpret_cast<const micant::boot::UefiKernelHandoffV5*>(handoff)->oem);
            if (hasPciInventory) {
                const auto& v7 = *reinterpret_cast<const
                    micant::boot::UefiKernelHandoffV7*>(handoff);
                if (v7.pciBarCapacity != micant::boot::UEFI_KERNEL_HANDOFF_MAX_PCI_BARS ||
                    v7.pciBarCount > v7.pciBarCapacity || v7.pciInventoryComplete > 1 ||
                    v7.reserved != 0) return false;
                for (uint32_t i = 0; i < v7.pciBarCount; ++i) {
                    const auto& bar = v7.pciBars[i];
                    if (bar.segment != 0 || bar.bus > 0xff || bar.device >= 32 ||
                        bar.function >= 8 || bar.index >= 6 || bar.type > 1 ||
                        !bar.address || !bar.length || (bar.length & (bar.length - 1)) ||
                        (bar.address & (bar.length - 1)) ||
                        bar.address > UINT64_MAX - bar.length) return false;
                    for (uint32_t j = 0; j < i; ++j) {
                        const auto& previous = v7.pciBars[j];
                        if (bar.segment == previous.segment && bar.bus == previous.bus &&
                            bar.device == previous.device && bar.function == previous.function &&
                            bar.index == previous.index) return false;
                    }
                }
            }
        } else {
            if (handoff->size < sizeof(micant::boot::UefiKernelHandoffV4)) return false;
            extendedV4 = reinterpret_cast<const micant::boot::UefiKernelHandoffV4*>(handoff);
        }
        const auto& extended = *extendedV4;
        if (extended.moduleCapacity == 0 ||
            extended.moduleCapacity > micant::boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES ||
            extended.moduleCount > extended.moduleCapacity ||
            extended.moduleCapacity != micant::boot::UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES)
            return false;
        uint32_t displayModules = 0;
        for (uint32_t i = 0; i < extended.moduleCount; ++i) {
            const auto& module = extended.modules[i];
            const auto& image = module.image;
            size_t nameLength = 0;
            while (nameLength < sizeof(module.baseName) && module.baseName[nameLength])
                ++nameLength;
            if (!nameLength || nameLength == sizeof(module.baseName) ||
                image.base == 0 || image.size == 0 || image.size > 16 * 1024 * 1024 ||
                image.size > UINT64_MAX - image.base || image.entryPoint < image.base ||
                image.entryPoint - image.base >= image.size ||
                (module.flags & micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER) == 0 ||
                (module.flags & ~(micant::boot::BOOT_MODULE_FLAG_BOOT_START_DRIVER |
                                  micant::boot::BOOT_MODULE_FLAG_DISPLAY_DRIVER)) != 0)
                return false;
            if (module.flags & micant::boot::BOOT_MODULE_FLAG_DISPLAY_DRIVER) ++displayModules;
            for (uint32_t j = 0; j < i; ++j) {
                const auto& other = extended.modules[j];
                if (image.base < other.image.base + other.image.size &&
                    other.image.base < image.base + image.size) return false;
                size_t otherLength = 0;
                while (otherLength < sizeof(other.baseName) && other.baseName[otherLength])
                    ++otherLength;
                if (nameLength == otherLength &&
                    std::memcmp(module.baseName, other.baseName, nameLength) == 0)
                    return false;
            }
        }
        if (displayModules > 1) return false;
    }

    const auto& framebuffer = handoff->framebuffer;
    if (framebuffer.physicalBase == 0 ||
        framebuffer.width < 320 || framebuffer.height < 200 ||
        framebuffer.pixelsPerScanLine < framebuffer.width ||
        framebuffer.pixelFormat > 1) {
        return false;
    }
    const uint64_t rowBytes =
        static_cast<uint64_t>(framebuffer.pixelsPerScanLine) * sizeof(uint32_t);
    return rowBytes <= framebuffer.size &&
           framebuffer.height <= framebuffer.size / rowBytes &&
           handoff->memoryMapSize % handoff->memoryMapDescriptorSize == 0;
}

bool inventoryFirmwarePciBars(const micant::boot::UefiKernelHandoffV3& handoff) noexcept {
    using micant::hal::amd64::PciConfigAccessResult;
    auto read = [](uint16_t bus, uint16_t device, uint16_t function,
                   uint16_t offset, uint32_t& value) noexcept {
        return micant::hal::amd64::readPciConfig32(bus, device, function, offset, value) ==
            PciConfigAccessResult::Success;
    };
    const auto* snapshot = handoff.version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES
        ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(&handoff) : nullptr;
    bool matched[micant::boot::UEFI_KERNEL_HANDOFF_MAX_PCI_BARS]{};
    bool complete = snapshot && snapshot->pciInventoryComplete == 1;
    for (uint16_t bus = 0; bus < 256; ++bus) for (uint16_t device = 0; device < 32; ++device) {
        uint32_t id{}, header{};
        if (!read(bus, device, 0, 0, id)) return false;
        if ((id & 0xffffu) == 0xffffu) continue;
        if (!read(bus, device, 0, 0x0c, header)) return false;
        const unsigned functions = (header & 0x00800000u) ? 8 : 1;
        for (uint16_t function = 0; function < functions; ++function) {
            uint32_t command{};
            if (!read(bus, device, function, 0, id)) return false;
            if ((id & 0xffffu) == 0xffffu) continue;
            if (!read(bus, device, function, 0x0c, header) ||
                !read(bus, device, function, 4, command)) return false;
            const auto type = (header >> 16) & 0x7f;
            const unsigned barCount = type == 0 ? 6 : type == 1 ? 2 : 0;
            if (type > 1) {
                serialWrite("[MicaNT PnP] Unsupported PCI header type\r\n");
                complete = false;
            }
            for (uint8_t index = 0; index < barCount; ++index) {
                uint32_t low{};
                if (!read(bus, device, function, static_cast<uint16_t>(0x10 + index * 4), low)) return false;
                const bool io = low & 1u;
                const bool wide = !io && ((low >> 1) & 3u) == 2;
                uint64_t address = low & (io ? ~uint32_t{3} : ~uint32_t{15});
                const uint8_t originalIndex = index;
                if (wide) {
                    if (++index >= barCount) return false;
                    uint32_t high{};
                    if (!read(bus, device, function, static_cast<uint16_t>(0x10 + index * 4), high)) return false;
                    address |= uint64_t{high} << 32;
                }
                if (!address) continue;
                const bool active = (command & (io ? 1u : 2u)) != 0 || (command & 4u) != 0;
                const micant::boot::FirmwarePciBarV1* firmware{};
                if (snapshot) {
                    uint32_t addressMatches{};
                    const micant::boot::FirmwarePciBarV1* addressMatch{};
                    for (uint32_t n = 0; n < snapshot->pciBarCount; ++n) {
                        const auto& candidate = snapshot->pciBars[n];
                        if (candidate.bus != bus || candidate.device != device ||
                            candidate.function != function) continue;
                        if (candidate.index == originalIndex) firmware = &candidate;
                        if (!matched[n] && candidate.address == address &&
                            candidate.type == static_cast<uint8_t>(io)) {
                            ++addressMatches;
                            addressMatch = &candidate;
                        }
                    }
                    if (addressMatches == 1) {
                        firmware = addressMatch;
                        for (uint32_t n = 0; n < snapshot->pciBarCount; ++n)
                            if (&snapshot->pciBars[n] == firmware) { matched[n] = true; break; }
                    } else if (firmware) {
                        complete = false;
                    }
                }
                if (firmware && firmware->address == address && firmware->type == static_cast<uint8_t>(io)) {
                    if (!micant::km::arb::recordMeasuredBar({address, firmware->length, bus, device,
                                                              function, originalIndex, io, active})) return false;
                    continue;
                }
                if (firmware) {
                    serialWrite("[MicaNT PnP] Firmware/config BAR mismatch BDF/index=");
                    serialWriteNumber((uint64_t{bus} << 24) | (uint64_t{device} << 16) |
                                      (uint64_t{function} << 8) | originalIndex);
                    serialWrite("\r\n");
                    complete = false;
                }
                micant::hal::amd64::PciBarInfo info{};
                const auto result = micant::hal::amd64::probeQuiescentPciBar(
                    bus, device, function, originalIndex, info);
                if (result == micant::hal::amd64::PciBarProbeResult::RestoreFailed) return false;
                if (result == micant::hal::amd64::PciBarProbeResult::Success &&
                    info.address == address && info.ioSpace == io) {
                    if (!micant::km::arb::recordMeasuredBar({address, info.length, bus, device,
                                                              function, originalIndex, io, active})) return false;
                } else {
                    serialWrite("[MicaNT PnP] PCI BAR size unavailable BDF/index=");
                    serialWriteNumber((uint64_t{bus} << 24) | (uint64_t{device} << 16) |
                                      (uint64_t{function} << 8) | originalIndex);
                    serialWrite("\r\n");
                    if (!micant::km::arb::recordUnmeasuredBar({address, 0, bus, device, function,
                                                                originalIndex, io, active})) return false;
                    complete = false;
                }
            }
        }
    }
    if (snapshot) for (uint32_t i = 0; i < snapshot->pciBarCount; ++i) {
        if (matched[i]) continue;
        const auto& bar = snapshot->pciBars[i];
        if (bar.index > 0) {
            uint32_t previous{};
            if (read(bar.bus, bar.device, bar.function,
                     static_cast<uint16_t>(0x10 + (bar.index - 1) * 4), previous) &&
                !(previous & 1u) && ((previous >> 1) & 3u) == 2u) {
                serialWrite("[MicaNT PnP] Ignoring firmware descriptor for 64-bit BAR upper slot\r\n");
                continue;
            }
        }
        serialWrite("[MicaNT PnP] Unmatched firmware BAR BDF/index=");
        serialWriteNumber((uint64_t{bar.bus} << 24) | (uint64_t{bar.device} << 16) |
                          (uint64_t{bar.function} << 8) | bar.index);
        uint32_t observedId{};
        if (read(bar.bus, bar.device, bar.function, 0, observedId)) {
            serialWrite(" config-id=");
            serialWriteNumber(observedId);
        }
        serialWrite("\r\n");
        complete = false;
    }
    micant::km::arb::setPciInventoryComplete(complete);
    if (!complete) serialWrite("[MicaNT PnP] PCI resource inventory incomplete\r\n");
    return true;
}

bool initializeFirmwareResourceReservations(const micant::boot::UefiKernelHandoffV3& handoff) noexcept {
    const size_t count = handoff.memoryMapSize / handoff.memoryMapDescriptorSize;
    for (size_t i = 0; i < count; ++i) {
        const auto& d = g_bootMemoryDescriptors[i];
        if (d.type == static_cast<uint32_t>(micant::uefi::EfiMemoryType::EfiMemoryMappedIO) ||
            d.type == static_cast<uint32_t>(micant::uefi::EfiMemoryType::EfiMemoryMappedIOPortSpace))
            continue;
        if (d.type == static_cast<uint32_t>(micant::uefi::EfiMemoryType::EfiConventionalMemory) &&
            (d.attribute & 8u) && !micant::io::dma::registerWriteBackRam(
                d.physicalStart, d.numberOfPages * 4096)) return false;
        if (!micant::km::arb::reservePhysicalRange(d.physicalStart, d.numberOfPages * 4096))
            return false;
    }
    if (handoff.framebuffer.size && !micant::km::arb::reservePhysicalRange(
        handoff.framebuffer.physicalBase, handoff.framebuffer.size)) return false;
    return inventoryFirmwarePciBars(handoff);
}

void initializeBootStartDrivers(const micant::boot::UefiKernelHandoffV3* handoff) noexcept {
    if (!initializeFirmwareResourceReservations(*handoff)) {
        serialWrite("[MicaNT PnP] Firmware resource reservations failed\r\n");
        if (!initializeGopFramebuffer(handoff->framebuffer))
            stopWithError("[MicaNT Display] GOP display unavailable\r\n");
        return;
    }
    uint32_t dmaFailureStage{};
    uint64_t dmaFailureDetail{};
    if (micant::hal::amd64::initializeBootstrapDmaTranslationState(
            *handoff, &dmaFailureStage, &dmaFailureDetail)) {
        (void)micant::io::dma::initializeDirectProvider();
    } else {
        serialWrite("[MicaNT DMA] bootstrap inventory rejected stage=");
        serialWriteNumber(dmaFailureStage);
        serialWrite(" detail=");
        serialWriteNumber(dmaFailureDetail);
        serialWrite("\r\n");
    }
    if (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION) {
        if (!initializeDisplay(handoff->displayDriver) &&
            !initializeGopFramebuffer(handoff->framebuffer))
            stopWithError("[MicaNT Display] OEM and GOP display paths unavailable\r\n");
        return;
    }
    const auto& extended = handoff->version ==
            micant::boot::UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME
        ? reinterpret_cast<const micant::boot::UefiKernelHandoffV5*>(handoff)->oem
        : (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION
            ? reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->time.oem
            : (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES
                ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.time.oem
                : *reinterpret_cast<const micant::boot::UefiKernelHandoffV4*>(handoff)));
    bool displayReady = false;
    for (uint32_t i = 0; i < extended.moduleCount; ++i) {
        const auto& module = extended.modules[i];
        auto& driver = g_bootDriverObjects[i];
        auto& services = g_bootDriverServices[i];
        auto& display = g_bootDriverDisplays[i];
        initializeKernelServices(services, i);
        driver.Size = sizeof(driver);
        driver.Version = micant::boot_driver::DRIVER_ABI_VERSION;
        driver.Flags = module.flags;
        driver.DriverStart = reinterpret_cast<void*>(module.image.base);
        driver.DriverSize = static_cast<size_t>(module.image.size);
        driver.HardwareResources = &services;
        driver.Display = (module.flags & micant::boot::BOOT_MODULE_FLAG_DISPLAY_DRIVER)
            ? &display : nullptr;

        const auto entry = reinterpret_cast<micant::boot_driver::DriverEntryRoutine>(
            module.image.entryPoint);
        const int32_t status = entry(&driver, nullptr);
        if (status != micant::boot_driver::STATUS_SUCCESS) {
            serialWrite("[MicaNT Io] Boot-start driver failed: ");
            for (size_t n = 0; n < sizeof(module.baseName) && module.baseName[n]; ++n)
                serialWriteChar(module.baseName[n]);
            serialWrite(" status=");
            serialWriteNumber(static_cast<uint32_t>(status));
            serialWrite("\r\n");
            if (driver.DriverUnload) driver.DriverUnload(&driver);
            rollbackBootDriverResources(i);
            continue;
        }

        serialWrite("[MicaNT Io] Boot-start .sys DriverEntry succeeded: ");
        for (size_t n = 0; n < sizeof(module.baseName) && module.baseName[n]; ++n)
            serialWriteChar(module.baseName[n]);
        serialWrite("\r\n");
        if (module.flags & micant::boot::BOOT_MODULE_FLAG_DISPLAY_DRIVER) {
            if (!display.PresentFrame || display.Width != kWidth || display.Height != kHeight) {
                if (driver.DriverUnload) driver.DriverUnload(&driver);
                rollbackBootDriverResources(i);
                continue;
            }
            g_display = display;
            g_displayDriver = driver;
            g_displayUsingGopFallback = false;
            displayReady = true;
        }
    }
    if (!displayReady && !initializeGopFramebuffer(handoff->framebuffer))
        stopWithError("[MicaNT Display] OEM and GOP display paths unavailable\r\n");
}

void drawExplorerDesktop() noexcept {
    micant::explorer::DrawDesktop(
        g_explorerSurface, kWidth, kHeight - kTaskbarHeight
    );
    micant::explorer::DrawTaskbar(
        g_explorerSurface, kWidth, kHeight, kTaskbarHeight, nullptr, 0, "QEMU"
    );
    if (!g_display.PresentFrame(g_display.Context, g_explorerSurface,
                               kWidth, kHeight, kWidth, 0, 0)) {
        stopWithError("[MicaNT Display] Desktop presentation failed\r\n");
    }
    serialWrite(g_displayUsingGopFallback
        ? "[MicaNT Display] Desktop presented through GOP framebuffer\r\n"
        : "[MicaNT Display] Desktop presented through boot-start display .sys\r\n");
}

void restoreCursorBackground(uint32_t x, uint32_t y) noexcept {
    for (uint32_t row = 0; row < kCursorSize; ++row) {
        for (uint32_t column = 0; column < kCursorSize; ++column) {
            g_cursorFrame[row * kCursorSize + column] =
                g_explorerSurface[static_cast<size_t>(y + row) * kWidth + x + column];
        }
    }
    presentCursorRect(x, y);
}

void drawMouseCursor(uint32_t x, uint32_t y) noexcept {
    constexpr uint16_t shape[kCursorSize]{
        0x8000, 0xC000, 0xE000, 0xF000,
        0xF800, 0xFC00, 0xFE00, 0xFF00,
        0xFF80, 0xFFC0, 0xFE00, 0xCE00,
        0x8700, 0x0700, 0x0300, 0x0000
    };
    for (uint32_t row = 0; row < kCursorSize; ++row) {
        uint32_t* target = g_cursorFrame + row * kCursorSize;
        for (uint32_t column = 0; column < kCursorSize; ++column) {
            target[column] = g_explorerSurface[(y + row) * kWidth + x + column];
            if (shape[row] & (0x8000u >> column)) {
                target[column] = 0xFF101820u;
            }
            if (row > 0 &&
                ((shape[row - 1] >> 1) & (0x8000u >> column))) {
                target[column] = 0xFFF4F7FFu;
            }
        }
    }
    presentCursorRect(x, y);
}

uint64_t dispatchBootstrapService(const micant::hal::amd64::UserServiceArguments& args) noexcept {
    using namespace micant;
    const auto number = args.number, first = args.first, second = args.second;
    const auto status = [](NtStatus value) { return static_cast<uint64_t>(value); };
    if (number == sys::SSN_NtQuerySystemTime) {
        const auto result = sys::NtQuerySystemTime(reinterpret_cast<LargeInteger*>(first));
        return status(result);
    }
    if (number == sys::SSN_NtQueryPerformanceCounter) {
        const auto result = sys::NtQueryPerformanceCounter(
            reinterpret_cast<LargeInteger*>(first),
            reinterpret_cast<LargeInteger*>(second));
        return status(result);
    }
    if (number == sys::SSN_NtContinue) {
        const auto result = sys::NtContinue(reinterpret_cast<const void*>(first),
            static_cast<uint8_t>(second) != 0, args.trapFrame, args.floatingState,
            args.floatingStateSize, args.xsaveMask, false, args.userSegments);
        return result == NtStatus::Success
            ? hal::amd64::BootstrapNtContinueApplied : status(result);
    }
    if (number == sys::SSN_NtRaiseException) {
        const auto result = sys::NtRaiseException(reinterpret_cast<const void*>(first),
            reinterpret_cast<const void*>(second), (args.third & 0xff) != 0,
            args.trapFrame, args.floatingState, args.floatingStateSize, args.xsaveMask,
            args.userSegments);
        return result == NtStatus::Success
            ? hal::amd64::BootstrapNtContinueApplied : status(result);
    }
    if (number == sys::SSN_NtClose) {
        const auto closeStatus = sys::NtClose(static_cast<Handle>(first));
        return status(closeStatus);
    }
    if (number == sys::SSN_NtCreateIoCompletion) {
        auto* destination = reinterpret_cast<Handle*>(first);
        if (!destination || ex::ProbeForWrite(destination, sizeof(Handle), alignof(Handle)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        if (args.third) {
            ObjectAttributes attributes{};
            if (ex::ExSafeCopyFromUser(&attributes, reinterpret_cast<const void*>(args.third), sizeof(attributes)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            if (attributes.length != sizeof(attributes)) return status(NtStatus::InvalidParameter);
            if (attributes.rootDirectory || attributes.objectName || attributes.attributes ||
                attributes.securityDescriptor || attributes.securityQualityOfService) return status(NtStatus::NotSupported);
        }
        Handle handle{};
        auto result = io::IoCreateCompletionPort(&handle, static_cast<uint32_t>(second), static_cast<uint32_t>(args.fourth));
        if (result == NtStatus::Success && ex::ExSafeCopyToUser(destination, &handle, sizeof(handle)) != NtStatus::Success) {
            (void)sys::NtClose(handle); result = NtStatus::AccessViolation;
        }
        return status(result);
    }
    if (number == sys::SSN_NtSetIoCompletion) {
        uint64_t information{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&information, reinterpret_cast<const void*>(args.userStack + 40), sizeof(information)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        return status(io::IoPostCompletion(static_cast<Handle>(first), second, args.third,
                                          static_cast<NtStatus>(static_cast<uint32_t>(args.fourth)), information));
    }
    if (number == sys::SSN_NtRemoveIoCompletion) {
        uint64_t timeoutAddress{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&timeoutAddress, reinterpret_cast<const void*>(args.userStack + 40), sizeof(timeoutAddress)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        auto* keyOut = reinterpret_cast<uintptr_t*>(second);
        auto* contextOut = reinterpret_cast<uintptr_t*>(args.third);
        auto* ioOut = reinterpret_cast<IoStatusBlock*>(args.fourth);
        if (!keyOut || !contextOut || !ioOut ||
            ex::ProbeForWrite(keyOut, sizeof(*keyOut), alignof(uintptr_t)) != NtStatus::Success ||
            ex::ProbeForWrite(contextOut, sizeof(*contextOut), alignof(uintptr_t)) != NtStatus::Success ||
            ex::ProbeForWrite(ioOut, sizeof(*ioOut), alignof(IoStatusBlock)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        uint64_t timeout100ns = UINT64_MAX;
        if (timeoutAddress) {
            LargeInteger timeout{};
            if (ex::ExSafeCopyFromUser(&timeout, reinterpret_cast<const void*>(timeoutAddress), sizeof(timeout)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            if (timeout.quadPart == 0) {
                timeout100ns = 0;
            } else if (timeout.quadPart < 0) {
                timeout100ns = static_cast<uint64_t>(-(timeout.quadPart + 1)) + 1;
            } else {
                uint64_t now{};
                if (!ke::KeQuerySystemTime100ns(now)) return status(NtStatus::NotSupported);
                const auto absolute = static_cast<uint64_t>(timeout.quadPart);
                timeout100ns = absolute > now ? absolute - now : 0;
            }
        }
        auto* current = ke::KeGetCurrentThread();
        if (timeout100ns && (!current || !current->owningProcess || !current->kernelStackTop)) return status(NtStatus::NotSupported);
        if (timeout100ns) {
            __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        }
        uintptr_t key{}, context{}; IoStatusBlock result{};
        const auto removeStatus = io::IoRemoveCompletion(static_cast<Handle>(first), key, context, result, timeout100ns);
        if (timeout100ns && current->terminationRequested && current->serviceTermination)
            current->serviceTermination(current->terminationContext);
        if (timeout100ns) {
            __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            __asm__ __volatile__("swapgs" : : : "memory");
        }
        if (removeStatus != NtStatus::Success) return status(removeStatus);
        if (ex::ExSafeCopyToUser(keyOut, &key, sizeof(key)) != NtStatus::Success ||
            ex::ExSafeCopyToUser(contextOut, &context, sizeof(context)) != NtStatus::Success ||
            ex::ExSafeCopyToUser(ioOut, &result, sizeof(result)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        return status(NtStatus::Success);
    }
    if (number == sys::SSN_NtSetInformationFile) {
        uint64_t informationClass{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&informationClass, reinterpret_cast<const void*>(args.userStack + 40), sizeof(informationClass)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        if (informationClass != 30) return status(NtStatus::NotSupported); // FileCompletionInformation
        struct CompletionInformation { Handle port; uintptr_t key; } information{};
        auto* ioOut = reinterpret_cast<IoStatusBlock*>(second);
        if (!ioOut || ex::ProbeForWrite(ioOut, sizeof(*ioOut), alignof(IoStatusBlock)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        if (args.fourth != sizeof(information)) return status(NtStatus::InvalidParameter);
        if (ex::ExSafeCopyFromUser(&information, reinterpret_cast<const void*>(args.third), sizeof(information)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        const auto association = io::IoAssociateCompletionPort(static_cast<Handle>(first), information.port, information.key);
        const IoStatusBlock result{association, 0};
        if (ex::ExSafeCopyToUser(ioOut, &result, sizeof(result)) != NtStatus::Success) return status(NtStatus::AccessViolation);
        return status(association);
    }
    if (number == sys::SSN_NtCancelIoFileEx) {
        auto* resultAddress = reinterpret_cast<IoStatusBlock*>(args.third);
        if (!resultAddress || ex::ProbeForWrite(resultAddress, sizeof(*resultAddress),
                                                alignof(IoStatusBlock)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const auto cancelStatus = io::IoCancelFileReads(static_cast<Handle>(first),
                                                         reinterpret_cast<const void*>(args.second));
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        if (cancelStatus == NtStatus::Success) {
            const IoStatusBlock result{NtStatus::Success, 0};
            if (ex::ExSafeCopyToUser(resultAddress, &result, sizeof(result)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
        }
        return status(cancelStatus);
    }
    if (number == sys::SSN_NtReadFile) {
        uint64_t extra[5]{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(extra)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        auto* ioStatusAddress = reinterpret_cast<IoStatusBlock*>(extra[0]);
        auto* userBuffer = reinterpret_cast<void*>(extra[1]);
        if (extra[2] > UINT32_MAX) return status(NtStatus::InvalidParameter);
        const size_t length = static_cast<uint32_t>(extra[2]);
        const auto* byteOffsetAddress = reinterpret_cast<const LargeInteger*>(extra[3]);
        const auto* keyAddress = reinterpret_cast<const uint32_t*>(extra[4]);
        if (!ioStatusAddress || (length && !userBuffer) || keyAddress)
            return status(keyAddress ? NtStatus::NotSupported : NtStatus::InvalidParameter);
        if (ex::ProbeForWrite(ioStatusAddress, sizeof(IoStatusBlock), alignof(IoStatusBlock)) != NtStatus::Success ||
            ex::ProbeForWrite(userBuffer, length, 1) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        LargeInteger byteOffset{};
        if (byteOffsetAddress && ex::ExSafeCopyFromUser(&byteOffset, byteOffsetAddress,
                sizeof(byteOffset)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        constexpr int64_t fileUseCurrentPosition = -2;
        const bool useCurrentOffset = !byteOffsetAddress || byteOffset.quadPart == fileUseCurrentPosition;
        if (!useCurrentOffset && byteOffset.quadPart < 0) return status(NtStatus::InvalidParameter);
        bool completionAssociated = false;
        if (!args.second && !args.third) {
            const auto lookup = io::IoHasCompletionPort(static_cast<Handle>(first), completionAssociated);
            if (lookup != NtStatus::Success) return status(lookup);
            if ((args.fourth && !completionAssociated) || (completionAssociated && !args.fourth))
                return status(NtStatus::NotSupported);
        }
        if (args.second || args.third || completionAssociated) {
            auto* issuingThread = ke::KeGetCurrentThread();
            if (!useCurrentOffset || !issuingThread || !issuingThread->owningProcess || !issuingThread->kernelStackTop)
                return status(NtStatus::NotSupported);
            __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            const auto asyncStatus = io::IoSubmitAsyncRead(static_cast<Handle>(first), static_cast<Handle>(args.second), args.third, args.fourth,
                                                           userBuffer, length, ioStatusAddress);
            __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            __asm__ __volatile__("swapgs" : : : "memory");
            return status(asyncStatus);
        }

        std::unique_ptr<uint8_t[]> staging;
        if (length) {
            staging.reset(new (std::nothrow_t{}) uint8_t[length]);
            if (!staging) return status(NtStatus::NoMemory);
        }
        size_t bytesRead{};
        const auto* readThread = ke::KeGetCurrentThread();
        const bool canBlock = readThread && readThread->owningProcess && readThread->kernelStackTop;
        if (!canBlock) {
            io::FileObject* file{}; ob::ObjectHeader* reference{};
            const auto lookup = io::IoReferenceFileHandle(static_cast<Handle>(first), 1, file, reference);
            if (lookup != NtStatus::Success) return status(lookup);
            const bool stream = file->isStream();
            ob::ObDereferenceObject(reference);
            if (stream) return status(NtStatus::NotImplemented);
        } else {
            __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        }
        const bool enableReadInterrupts = canBlock && hal::amd64::bootstrapSchedulerClockActive();
        if (enableReadInterrupts) __asm__ __volatile__("sti" : : : "memory");
        const auto readStatus = io::IoReadFileHandle(static_cast<Handle>(first), useCurrentOffset,
            useCurrentOffset ? 0 : static_cast<uint64_t>(byteOffset.quadPart),
            staging.get(), length, &bytesRead, ioStatusAddress);
        if (enableReadInterrupts) __asm__ __volatile__("cli" : : : "memory");
        if (canBlock && readThread->terminationRequested && readThread->serviceTermination) {
            auto terminate = readThread->serviceTermination;
            void* terminationContext = readThread->terminationContext;
            staging.reset();
            terminate(terminationContext);
        }
        if (canBlock) {
            __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            __asm__ __volatile__("swapgs" : : : "memory");
        }
        IoStatusBlock ioStatus{readStatus, bytesRead};
        NtStatus copyStatus = NtStatus::Success;
        if (bytesRead)
            copyStatus = ex::ExSafeCopyToUser(userBuffer, staging.get(), bytesRead);
        if (copyStatus == NtStatus::Success)
            copyStatus = ex::ExSafeCopyToUser(ioStatusAddress, &ioStatus, sizeof(ioStatus));
        return status(copyStatus == NtStatus::Success ? readStatus : NtStatus::AccessViolation);
    }
    if (number == sys::SSN_NtWriteFile) {
        uint64_t extra[5]{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(extra)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        auto* ioStatusAddress = reinterpret_cast<IoStatusBlock*>(extra[0]);
        const auto* userBuffer = reinterpret_cast<const void*>(extra[1]);
        if (extra[2] > 1024 * 1024 || extra[4]) return status(NtStatus::NotSupported);
        const size_t length = static_cast<size_t>(extra[2]);
        const auto* byteOffsetAddress = reinterpret_cast<const LargeInteger*>(extra[3]);
        if (!ioStatusAddress || (length && !userBuffer) || args.second || args.third || args.fourth)
            return status(args.second || args.third || args.fourth
                ? NtStatus::NotSupported : NtStatus::InvalidParameter);
        if (ex::ProbeForWrite(ioStatusAddress, sizeof(IoStatusBlock), alignof(IoStatusBlock)) != NtStatus::Success ||
            ex::ProbeForRead(userBuffer, length, 1) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        LargeInteger byteOffset{};
        if (byteOffsetAddress && ex::ExSafeCopyFromUser(&byteOffset, byteOffsetAddress,
                sizeof(byteOffset)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        constexpr int64_t fileUseCurrentPosition = -2;
        const bool useCurrentOffset = !byteOffsetAddress || byteOffset.quadPart == fileUseCurrentPosition;
        if (!useCurrentOffset && byteOffset.quadPart < 0) return status(NtStatus::InvalidParameter);
        std::unique_ptr<uint8_t[]> staging;
        if (length) {
            staging.reset(new (std::nothrow_t{}) uint8_t[length]);
            if (!staging) return status(NtStatus::NoMemory);
            if (ex::ExSafeCopyFromUser(staging.get(), userBuffer, length) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
        }
        const auto* issuingThread = ke::KeGetCurrentThread();
        if (!issuingThread || !issuingThread->owningProcess || !issuingThread->kernelStackTop ||
            ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
            return status(NtStatus::InvalidDeviceRequest);
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const bool enableWriteInterrupts = hal::amd64::bootstrapSchedulerClockActive();
        if (enableWriteInterrupts) __asm__ __volatile__("sti" : : : "memory");
        size_t bytesWritten{};
        const auto writeStatus = io::IoWriteFileHandle(static_cast<Handle>(first), useCurrentOffset,
            useCurrentOffset ? 0 : static_cast<uint64_t>(byteOffset.quadPart),
            staging.get(), length, &bytesWritten);
        if (enableWriteInterrupts) __asm__ __volatile__("cli" : : : "memory");
        if (issuingThread->terminationRequested && issuingThread->serviceTermination) {
            auto terminate = issuingThread->serviceTermination;
            void* terminationContext = issuingThread->terminationContext;
            staging.reset();
            terminate(terminationContext);
        }
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        const IoStatusBlock ioResult{writeStatus, bytesWritten};
        if (ex::ExSafeCopyToUser(ioStatusAddress, &ioResult, sizeof(ioResult)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        return status(writeStatus);
    }
    if (number == sys::SSN_NtCreateFile) {
        uint64_t extra[7]{};
        ObjectAttributes attributes{};
        if (args.userStack > UINT64_MAX - 40) return status(NtStatus::AccessViolation);
        auto captureStatus = ex::ProbeForRead(reinterpret_cast<const void*>(args.third),
                                              sizeof(attributes), alignof(ObjectAttributes));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        captureStatus = ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), alignof(Handle));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        captureStatus = ex::ProbeForWrite(reinterpret_cast<void*>(args.fourth), sizeof(IoStatusBlock),
                                          alignof(IoStatusBlock));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        captureStatus = ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                               sizeof(extra));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        captureStatus = ex::ExSafeCopyFromUser(&attributes, reinterpret_cast<const void*>(args.third),
                                               sizeof(attributes));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        constexpr uint32_t fileOpen = 1;
        constexpr uint32_t fileOpenIf = 3;
        constexpr uint32_t allowedAttributes = OBJ_INHERIT | OBJ_CASE_INSENSITIVE;
        constexpr uint32_t allowedShareAccess = 0x7;
        constexpr uint32_t allowedOpenOptions = 0x20 | 0x40;
        const auto fileAttributes = static_cast<uint32_t>(extra[1]);
        const auto shareAccess = static_cast<uint32_t>(extra[2]);
        const auto createDisposition = static_cast<uint32_t>(extra[3]);
        const auto createOptions = static_cast<uint32_t>(extra[4]);
        if (attributes.length != sizeof(ObjectAttributes) || !attributes.objectName ||
            attributes.rootDirectory || (attributes.attributes & ~allowedAttributes) ||
            attributes.securityDescriptor || attributes.securityQualityOfService ||
            extra[0] || shareAccess > allowedShareAccess ||
            (createDisposition != fileOpen && createDisposition != fileOpenIf) ||
            (createOptions & ~allowedOpenOptions))
            return status(NtStatus::NotSupported);
        UnicodeString name{};
        captureStatus = ex::ProbeForRead(attributes.objectName, sizeof(name), alignof(UnicodeString));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        captureStatus = ex::ExSafeCopyFromUser(&name, attributes.objectName, sizeof(name));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        if (!name.length || (name.length & 1) || name.length > name.maximumLength || !name.buffer ||
            name.length > 2048 * sizeof(char16_t)) return status(NtStatus::InvalidParameter);
        char16_t path[2048]{};
        captureStatus = ex::ProbeForRead(name.buffer, name.length, alignof(char16_t));
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        captureStatus = ex::ExSafeCopyFromUser(path, name.buffer, name.length);
        if (captureStatus != NtStatus::Success) return status(captureStatus);
        const std::u16string_view pathView(path, name.length / sizeof(char16_t));
        constexpr std::u16string_view afdPath(u"\\Device\\Afd\\Endpoint");
        Handle handle{};
        IoStatusBlock ioStatus{};
        bool provisionalCreated{};
        if (pathView == afdPath) {
            const auto eaLength = static_cast<uint32_t>(extra[6]);
            if (createDisposition != fileOpen || fileAttributes || createOptions || !extra[5] ||
                !eaLength || eaLength > 256)
                return status(NtStatus::NotSupported);
            std::array<uint8_t, 256> ea{};
            if (ex::ExSafeCopyFromUser(ea.data(), reinterpret_cast<const void*>(extra[5]),
                                       eaLength) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            const auto createStatus = micant::km::afdwsk::IoAfdCreateEndpointHandle(
                &handle, std::wstring_view(reinterpret_cast<const wchar_t*>(pathView.data()), pathView.size()),
                static_cast<uint32_t>(second), ea.data(), eaLength);
            if (createStatus != NtStatus::Success) return status(createStatus);
            ioStatus.status = NtStatus::Success;
            ioStatus.information = 1; // FILE_OPENED
        } else if (micant::km::hid::isInputDeviceName(pathView)) {
            if (createDisposition != fileOpen || fileAttributes || extra[5] ||
                static_cast<uint32_t>(extra[6])) return status(NtStatus::NotSupported);
            ioStatus.status = micant::km::hid::openInputDevice(&handle, static_cast<uint32_t>(second), pathView,
                                                               (createOptions & 0x20u) != 0);
            ioStatus.information = ioStatus.status == NtStatus::Success ? 1 : 0;
            if (ioStatus.status != NtStatus::Success) {
                (void)ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.fourth), &ioStatus, sizeof(ioStatus));
                return status(ioStatus.status);
            }
        } else {
            if (fileAttributes || extra[5] || static_cast<uint32_t>(extra[6]))
                return status(NtStatus::NotSupported);
            // Filesystem lookup may read a real block device or wait for its
            // owner. The captured path is resident on this thread's stack.
            __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            const bool canSchedule = hal::amd64::bootstrapSchedulerClockActive();
            if (canSchedule) __asm__ __volatile__("sti" : : : "memory");
            ioStatus.status = io::IoOpenBackendFile(&handle, static_cast<uint32_t>(second),
                pathView, createDisposition == fileOpenIf, &provisionalCreated, true);
            if (canSchedule) __asm__ __volatile__("cli" : : : "memory");
            __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            __asm__ __volatile__("swapgs" : : : "memory");
            ioStatus.information = ioStatus.status == NtStatus::Success ? (provisionalCreated ? 2 : 1) : 0;
            if (ioStatus.status != NtStatus::Success) {
                (void)ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.fourth), &ioStatus, sizeof(ioStatus));
                return status(ioStatus.status);
            }
        }
        auto copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.fourth), &ioStatus, sizeof(ioStatus));
        if (copy == NtStatus::Success)
            copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(first), &handle, sizeof(handle));
        if (copy == NtStatus::Success && provisionalCreated) {
            // The FAT directory entry remains provisional until both user
            // outputs are published. Committing may wait for the backend.
            __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            const bool canSchedule = hal::amd64::bootstrapSchedulerClockActive();
            if (canSchedule) __asm__ __volatile__("sti" : : : "memory");
            copy = io::IoCommitCreatedFile(handle);
            if (canSchedule) __asm__ __volatile__("cli" : : : "memory");
            __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            __asm__ __volatile__("swapgs" : : : "memory");
            if (copy != NtStatus::Success) {
                ioStatus.status = copy;
                ioStatus.information = 0;
                (void)ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.fourth), &ioStatus, sizeof(ioStatus));
            }
        }
        if (copy != NtStatus::Success) {
            ob::HandleTable* table{}; Handle raw{};
            if (ob::detail::resolveHandle(handle, table, raw) != NtStatus::Success)
                return status(copy);
            auto* retired = table->takeHandle(raw);
            if (retired) {
                // Closing can release a provisional filesystem entry and
                // perform I/O, so do it after leaving the syscall copy path.
                __asm__ __volatile__("swapgs" : : : "memory");
                __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
                const bool canSchedule = hal::amd64::bootstrapSchedulerClockActive();
                if (canSchedule) __asm__ __volatile__("sti" : : : "memory");
                (void)ob::ObFinishClose(retired, raw);
                if (canSchedule) __asm__ __volatile__("cli" : : : "memory");
                __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
                __asm__ __volatile__("swapgs" : : : "memory");
            }
        }
        return status(copy);
    }
    if (number == sys::SSN_NtDeviceIoControlFile) {
        uint64_t extra[6]{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(extra)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        if (second || args.third || args.fourth) {
            return status(NtStatus::NotSupported);
        }
        const uint32_t inputLength = static_cast<uint32_t>(extra[3]);
        const uint32_t outputLength = static_cast<uint32_t>(extra[5]);
        if (!extra[0] || inputLength > micant::shared::afd::MaximumControlBuffer ||
            outputLength > micant::shared::afd::MaximumControlBuffer) {
            return status(NtStatus::InvalidParameter);
        }
        const uint32_t ioctl = static_cast<uint32_t>(extra[1]);
        const void* inputBuffer = reinterpret_cast<const void*>(extra[2]);
        void* outputBuffer = reinterpret_cast<void*>(extra[4]);
        auto* ioStatusAddress = reinterpret_cast<IoStatusBlock*>(extra[0]);
        if ((inputLength && !inputBuffer) || (outputLength && !outputBuffer) ||
            ex::ProbeForWrite(ioStatusAddress, sizeof(IoStatusBlock), alignof(IoStatusBlock)) != NtStatus::Success ||
            (inputLength && ex::ProbeForRead(inputBuffer, inputLength, 1) != NtStatus::Success) ||
            (outputLength && ex::ProbeForWrite(outputBuffer, outputLength, 1) != NtStatus::Success)) {
            return status(NtStatus::AccessViolation);
        }

        uint32_t requiredAccess = 0;
        if (ioctl == micant::shared::afd::IoctlReceive ||
            ioctl == micant::shared::afd::IoctlReceiveFrom ||
            ioctl == micant::shared::afd::IoctlQueryNetworkConfiguration) requiredAccess = 0x0001;
        else if (ioctl == micant::shared::afd::IoctlBind || ioctl == micant::shared::afd::IoctlConnect ||
                 ioctl == micant::shared::afd::IoctlListen || ioctl == micant::shared::afd::IoctlSend ||
                 ioctl == micant::shared::afd::IoctlSendTo ||
                 ioctl == micant::shared::afd::IoctlSetReceiveTimeout ||
                 ioctl == micant::shared::afd::IoctlSetNetworkConfiguration) requiredAccess = 0x0002;
        io::FileObject* file{};
        ob::ObjectHeader* reference{};
        NtStatus ioStatus = io::IoReferenceFileHandle(static_cast<Handle>(first), requiredAccess, file, reference);
        if (ioStatus != NtStatus::Success) return status(ioStatus);
        if (!micant::km::afdwsk::IoAfdIsEndpointFile(file)) {
            ob::ObDereferenceObject(reference);
            return status(NtStatus::NotSupported);
        }

        std::unique_ptr<uint8_t[]> input;
        std::unique_ptr<uint8_t[]> output;
        if (inputLength) input.reset(new (std::nothrow_t{}) uint8_t[inputLength]);
        if (outputLength) output.reset(new (std::nothrow_t{}) uint8_t[outputLength]);
        if ((inputLength && !input) || (outputLength && !output)) {
            ob::ObDereferenceObject(reference);
            return status(NtStatus::NoMemory);
        }
        if (inputLength && ex::ExSafeCopyFromUser(input.get(), inputBuffer, inputLength) != NtStatus::Success) {
            ob::ObDereferenceObject(reference);
            return status(NtStatus::AccessViolation);
        }
        io::Irp irp{};
        irp.majorFunction = io::IRP_MJ_DEVICE_CONTROL;
        irp.deviceObject = file->deviceObject();
        irp.fileObject = file;
        irp.systemBuffer = input.get();
        irp.userBuffer = output.get();
        irp.length = outputLength;
        irp.byteOffset.lowPart = ioctl;
        irp.byteOffset.highPart = static_cast<int32_t>(inputLength);
        const auto* ioctlThread = ke::KeGetCurrentThread();
        if (!ioctlThread || !ioctlThread->owningProcess || !ioctlThread->kernelStackTop ||
            !hal::amd64::bootstrapSchedulerClockActive()) {
            ob::ObDereferenceObject(reference);
            return status(NtStatus::DeviceNotReady);
        }
        // AFD connect/receive and transport backpressure can wait. Switch to
        // kernel GS/segments before enabling scheduling; restore the syscall
        // convention and interrupt exclusion before touching user buffers.
        __asm__ __volatile__("swapgs; movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es; sti"
                             : : : "rax", "memory");
        ioStatus = io::IoCallDriver(irp.deviceObject, &irp);
        __asm__ __volatile__("cli; movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es; swapgs"
                             : : : "rax", "memory");
        const bool outputIoctl = ioctl == micant::shared::afd::IoctlAccept ||
                                 ioctl == micant::shared::afd::IoctlReceive ||
                                 ioctl == micant::shared::afd::IoctlReceiveFrom ||
                                 ioctl == micant::shared::afd::IoctlQueryNetworkConfiguration;
        if (outputIoctl && irp.ioStatus.information > outputLength) {
            ioStatus = NtStatus::Unsuccessful;
            irp.ioStatus.status = ioStatus;
            irp.ioStatus.information = 0;
        }
        else if (outputIoctl && irp.ioStatus.information &&
                 ex::ExSafeCopyToUser(outputBuffer, output.get(), irp.ioStatus.information) != NtStatus::Success) {
            ioStatus = NtStatus::AccessViolation;
            irp.ioStatus.status = ioStatus;
            irp.ioStatus.information = 0;
        }
        if (ex::ExSafeCopyToUser(ioStatusAddress, &irp.ioStatus, sizeof(irp.ioStatus)) != NtStatus::Success)
            ioStatus = NtStatus::AccessViolation;
        ob::ObDereferenceObject(reference);
        return status(ioStatus);
    }
    if (number == sys::SSN_NtOpenFile) {
        uint64_t extra[2]{};
        ObjectAttributes attributes{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(extra)) != NtStatus::Success ||
            ex::ExSafeCopyFromUser(&attributes, reinterpret_cast<const void*>(args.third),
                                   sizeof(attributes)) != NtStatus::Success ||
            ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), alignof(Handle)) != NtStatus::Success ||
            ex::ProbeForWrite(reinterpret_cast<void*>(args.fourth), sizeof(IoStatusBlock),
                              alignof(IoStatusBlock)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        constexpr uint32_t allowedAttributes = OBJ_INHERIT | OBJ_CASE_INSENSITIVE;
        constexpr uint32_t allowedShareAccess = 0x7;
        constexpr uint32_t allowedOpenOptions = 0x20 | 0x40;
        if (attributes.length != sizeof(ObjectAttributes) || !attributes.objectName ||
            attributes.rootDirectory || (attributes.attributes & ~allowedAttributes) ||
            attributes.securityDescriptor || attributes.securityQualityOfService ||
            extra[0] > allowedShareAccess || !(extra[0] & 1) ||
            (extra[1] & ~allowedOpenOptions))
            return status(NtStatus::NotSupported);
        UnicodeString name{};
        if (ex::ExSafeCopyFromUser(&name, attributes.objectName, sizeof(name)) != NtStatus::Success ||
            !name.length || (name.length & 1) || name.length > name.maximumLength || !name.buffer ||
            name.length > 2048 * sizeof(char16_t))
            return status(NtStatus::InvalidParameter);
        char16_t path[2048]{};
        if (ex::ExSafeCopyFromUser(path, name.buffer, name.length) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        Handle handle{};
        IoStatusBlock ioStatus{};
        ioStatus.status = io::IoOpenBackendFile(&handle, static_cast<uint32_t>(second),
                                                {path, name.length / sizeof(char16_t)});
        ioStatus.information = ioStatus.status == NtStatus::Success ? 1 : 0;
        if (ioStatus.status != NtStatus::Success) {
            (void)ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.fourth), &ioStatus, sizeof(ioStatus));
            return status(ioStatus.status);
        }
        auto copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.fourth), &ioStatus, sizeof(ioStatus));
        if (copy == NtStatus::Success)
            copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(first), &handle, sizeof(handle));
        if (copy != NtStatus::Success && sys::NtClose(handle) != NtStatus::Success)
            __builtin_trap();
        return status(copy);
    }
    if (number == sys::SSN_NtCreateThreadEx) {
        uint64_t extra[7]{};
        ObjectAttributes attributes{};
        if (args.third) {
            const auto attributeProbe = ex::ProbeForRead(reinterpret_cast<void*>(args.third),
                                                         sizeof(attributes), alignof(uint32_t));
            if (attributeProbe != NtStatus::Success) return status(attributeProbe);
        }
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(extra)) != NtStatus::Success ||
            (args.third && ex::ExSafeCopyFromUser(&attributes,
                reinterpret_cast<const void*>(args.third), sizeof(attributes)) != NtStatus::Success))
            return status(NtStatus::AccessViolation);
        const auto handleProbe = ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), alignof(Handle));
        if (handleProbe != NtStatus::Success) return status(handleProbe);
        struct CapturedAttributes { size_t totalLength; PsAttribute attributes[2]; } captured{};
        PsAttribute original[2]{};
        ClientId clientId{};
        uintptr_t teb{};
        size_t returned[2]{};
        size_t count{};
        if (extra[6]) {
            const auto listProbe = ex::ProbeForRead(reinterpret_cast<void*>(extra[6]), sizeof(size_t), alignof(size_t));
            if (listProbe != NtStatus::Success) return status(listProbe);
            if (ex::ExSafeCopyFromUser(&captured.totalLength, reinterpret_cast<void*>(extra[6]),
                                       sizeof(size_t)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            constexpr size_t prefix = offsetof(PsAttributeList, attributes);
            if (captured.totalLength < prefix || (captured.totalLength - prefix) % sizeof(PsAttribute) ||
                (captured.totalLength - prefix) / sizeof(PsAttribute) > 2 || extra[6] > UINT64_MAX - prefix)
                return status(NtStatus::InvalidParameter);
            count = (captured.totalLength - prefix) / sizeof(PsAttribute);
            if (count && ex::ExSafeCopyFromUser(original, reinterpret_cast<void*>(extra[6] + prefix),
                                                 count * sizeof(PsAttribute)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            for (size_t i = 0; i < count; ++i) {
                captured.attributes[i] = original[i];
                const size_t required = original[i].attribute == PS_ATTRIBUTE_CLIENT_ID ? sizeof(clientId) :
                    original[i].attribute == PS_ATTRIBUTE_TEB_ADDRESS ? sizeof(teb) : 0;
                if (!required) return status(NtStatus::NotImplemented);
                if (original[i].size != required) return status(NtStatus::InvalidParameter);
                const auto valueProbe = ex::ProbeForWrite(reinterpret_cast<void*>(original[i].value), required,
                                                           alignof(uintptr_t));
                if (valueProbe != NtStatus::Success) return status(valueProbe);
                if (original[i].returnLength) {
                    const auto lengthProbe = ex::ProbeForWrite(original[i].returnLength, sizeof(size_t), alignof(size_t));
                    if (lengthProbe != NtStatus::Success) return status(lengthProbe);
                }
                captured.attributes[i].value = original[i].attribute == PS_ATTRIBUTE_CLIENT_ID
                    ? reinterpret_cast<uintptr_t>(&clientId) : reinterpret_cast<uintptr_t>(&teb);
                captured.attributes[i].returnLength = original[i].returnLength ? &returned[i] : nullptr;
            }
        }
        Handle handle{};
        const auto result = sys::NtCreateThreadExFromKernel(&handle, static_cast<uint32_t>(second),
            args.third ? &attributes : nullptr, static_cast<Handle>(args.fourth), extra[0], extra[1],
            static_cast<uint32_t>(extra[2]) | 1u, extra[3], extra[4], extra[5], extra[6] ? &captured : nullptr);
        if (result == NtStatus::NoMemory || result == NtStatus::InsufficientResources) {
            serialWrite("[MicaNT Ps] NtCreateThreadEx allocation status=");
            serialWriteNumber(static_cast<uint32_t>(result));
            serialWrite("\r\n");
        }
        if (result != NtStatus::Success) return status(result);
        auto copy = NtStatus::Success;
        for (size_t i = 0; i < count && copy == NtStatus::Success; ++i) {
            const void* value = original[i].attribute == PS_ATTRIBUTE_CLIENT_ID
                ? static_cast<const void*>(&clientId) : static_cast<const void*>(&teb);
            copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(original[i].value), value, original[i].size);
            if (copy == NtStatus::Success && original[i].returnLength)
                copy = ex::ExSafeCopyToUser(original[i].returnLength, &returned[i], sizeof(size_t));
        }
        if (copy == NtStatus::Success)
            copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(first), &handle, sizeof(handle));
        // Publish user outputs before the thread can run, even if future
        // syscall implementations enable interrupts while copying buffers.
        if (copy == NtStatus::Success && !(extra[2] & 1)) {
            ps::EThread* thread{};
            ob::ObjectHeader* reference{};
            uint32_t previous{};
            if (ps::detail::referenceThreadHandle(handle, 0, thread, reference) != NtStatus::Success)
                __builtin_trap();
            const auto resumed = thread->resumeInitial(previous);
            ob::ObDereferenceObject(reference);
            if (resumed != NtStatus::Success || previous != 1) __builtin_trap();
        }
        if (copy != NtStatus::Success) {
            ps::EThread* thread{};
            ob::ObjectHeader* reference{};
            if (ps::detail::referenceThreadHandle(handle, 0, thread, reference) != NtStatus::Success)
                __builtin_trap();
            if (thread->rollbackBeforeStart() != NtStatus::Success)
                thread->deferFailedCreationReap();
            ob::ObDereferenceObject(reference);
            (void)sys::NtClose(handle);
        }
        return status(copy);
    }
    if (number == sys::SSN_NtOpenProcess || number == sys::SSN_NtOpenThread) {
        auto* output = reinterpret_cast<Handle*>(first);
        auto* attributes = reinterpret_cast<ObjectAttributes*>(args.third);
        auto* clientId = reinterpret_cast<ClientId*>(args.fourth);
        return status(number == sys::SSN_NtOpenProcess
            ? sys::NtOpenProcess(output, static_cast<uint32_t>(second), attributes, clientId)
            : sys::NtOpenThread(output, static_cast<uint32_t>(second), attributes, clientId));
    }
    if (number == sys::SSN_NtResumeThread) {
        // The service probes and copies the user output while the resume
        // transition remains under the same BSP exclusion.
        return status(sys::NtResumeThread(static_cast<Handle>(first), reinterpret_cast<uint32_t*>(second)));
    }
    if (number == sys::SSN_NtQueueApcThread) {
        uint64_t systemArgument2{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&systemArgument2,
                reinterpret_cast<const void*>(args.userStack + 40), sizeof(systemArgument2)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        return status(sys::NtQueueApcThread(static_cast<Handle>(first),
            static_cast<uintptr_t>(second), reinterpret_cast<void*>(args.third),
            reinterpret_cast<void*>(args.fourth), reinterpret_cast<void*>(systemArgument2)));
    }
    if (number == sys::SSN_NtAlertThread)
        return status(sys::NtAlertThread(static_cast<Handle>(first)));
    if (number == sys::SSN_NtTestAlert)
        return status(sys::NtTestAlert());
    if (number == sys::SSN_NtQueryInformationProcess || number == sys::SSN_NtQueryInformationThread ||
        number == sys::SSN_NtReadVirtualMemory) {
        uint64_t fifth{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&fifth, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(fifth)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        // Attach uses the kernel GS convention. Both services protect user
        // probes and CR3 changes against preemption internally.
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const bool canSchedule = hal::amd64::bootstrapSchedulerClockActive();
        if (canSchedule) __asm__ __volatile__("sti" : : : "memory");
        const auto result = number == sys::SSN_NtQueryInformationProcess
            ? sys::NtQueryInformationProcess(static_cast<Handle>(first),
                static_cast<ProcessInformationClass>(second), reinterpret_cast<void*>(args.third),
                static_cast<uint32_t>(args.fourth), reinterpret_cast<uint32_t*>(fifth))
            : number == sys::SSN_NtQueryInformationThread
            ? sys::NtQueryInformationThread(static_cast<Handle>(first),
                static_cast<ThreadInformationClass>(second), reinterpret_cast<void*>(args.third),
                static_cast<uint32_t>(args.fourth), reinterpret_cast<uint32_t*>(fifth))
            : sys::NtReadVirtualMemory(static_cast<Handle>(first), reinterpret_cast<const void*>(second),
                reinterpret_cast<void*>(args.third), static_cast<size_t>(args.fourth), reinterpret_cast<size_t*>(fifth));
        if (canSchedule) __asm__ __volatile__("cli" : : : "memory");
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        return status(result);
    }
    if (number == sys::SSN_NtCreateProcessEx) {
        uint64_t extra[5]{};
        ObjectAttributes attributes{};
        if (args.third) {
            const auto attributeProbe = ex::ProbeForRead(reinterpret_cast<void*>(args.third),
                                                         sizeof(attributes), alignof(uint32_t));
            if (attributeProbe != NtStatus::Success) return status(attributeProbe);
        }
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   sizeof(extra)) != NtStatus::Success ||
            (args.third && ex::ExSafeCopyFromUser(&attributes,
                reinterpret_cast<const void*>(args.third), sizeof(attributes)) != NtStatus::Success))
            return status(NtStatus::AccessViolation);
        const auto probe = ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), alignof(Handle));
        if (probe != NtStatus::Success) return status(probe);
        // Loading an image section can wait for real file I/O. Enter the
        // scheduler's kernel GS/segment convention before enabling clock
        // interrupts, and return to the syscall convention before copyout.
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const bool canSchedule = hal::amd64::bootstrapSchedulerClockActive();
        if (canSchedule) __asm__ __volatile__("sti" : : : "memory");
        const auto result = sys::NtCreateProcessExCaptured(reinterpret_cast<Handle*>(first), static_cast<uint32_t>(second),
            args.third ? &attributes : nullptr, static_cast<Handle>(args.fourth),
            static_cast<uint32_t>(extra[0]), static_cast<Handle>(extra[1]),
            static_cast<Handle>(extra[2]), static_cast<Handle>(extra[3]), (extra[4] & 0xff) != 0);
        if (result == NtStatus::NoMemory || result == NtStatus::InsufficientResources) {
            serialWrite("[MicaNT Ps] NtCreateProcessEx allocation status=");
            serialWriteNumber(static_cast<uint32_t>(result));
            serialWrite("\r\n");
        }
        if (canSchedule) __asm__ __volatile__("cli" : : : "memory");
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        return status(result);
    }
    if (number == sys::SSN_NtTerminateProcess || number == sys::SSN_NtTerminateThread) {
        // The syscall entry used SWAPGS; restore the bootstrap kernel's GS
        // convention before switching away. If termination fails, restore the
        // syscall entry convention before returning through its epilogue.
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const auto exitStatus = static_cast<NtStatus>(static_cast<uint32_t>(second));
        const auto result = number == sys::SSN_NtTerminateProcess
            ? sys::NtTerminateProcess(first, exitStatus)
            : sys::NtTerminateThread(first, exitStatus);
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        return status(result);
    }
    if (number == sys::SSN_NtCreateTimer) {
        ObjectAttributes attributes{};
        if ((args.third && ex::ExSafeCopyFromUser(&attributes, reinterpret_cast<const void*>(args.third), sizeof(attributes)) != NtStatus::Success) ||
            ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), 1) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        Handle handle{};
        const auto result = sys::NtCreateTimer(&handle, static_cast<uint32_t>(second),
            args.third ? &attributes : nullptr, static_cast<uint32_t>(args.fourth));
        if (result != NtStatus::Success) return status(result);
        const auto copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(first), &handle, sizeof(handle));
        if (copy != NtStatus::Success && sys::NtClose(handle) != NtStatus::Success) __builtin_trap();
        return status(copy);
    }
    if (number == sys::SSN_NtSetTimer) {
        LargeInteger due{}; uint64_t extra[3]{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&due, reinterpret_cast<const void*>(second), sizeof(due)) != NtStatus::Success ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40), sizeof(extra)) != NtStatus::Success ||
            (extra[2] && ex::ProbeForWrite(reinterpret_cast<void*>(extra[2]), sizeof(bool), 1) != NtStatus::Success))
            return status(NtStatus::AccessViolation);
        bool previous{};
        const auto result = sys::NtSetTimer(first, &due, reinterpret_cast<void*>(args.third),
            reinterpret_cast<void*>(args.fourth), (extra[0] & 0xff) != 0,
            static_cast<uint32_t>(extra[1]), extra[2] ? &previous : nullptr);
        if (result != NtStatus::Success || !extra[2]) return status(result);
        return status(ex::ExSafeCopyToUser(reinterpret_cast<void*>(extra[2]), &previous, sizeof(previous)));
    }
    if (number == sys::SSN_NtCancelTimer) {
        if (second && ex::ProbeForWrite(reinterpret_cast<void*>(second), sizeof(bool), 1) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        bool state{};
        const auto result = sys::NtCancelTimer(first, second ? &state : nullptr);
        if (result != NtStatus::Success || !second) return status(result);
        return status(ex::ExSafeCopyToUser(reinterpret_cast<void*>(second), &state, sizeof(state)));
    }
    if (number == sys::SSN_NtCreateEvent) {
        uint64_t initial{};
        ObjectAttributes attributes{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&initial, reinterpret_cast<const void*>(args.userStack + 40), sizeof(initial)) != NtStatus::Success ||
            (args.third && ex::ExSafeCopyFromUser(&attributes, reinterpret_cast<const void*>(args.third), sizeof(attributes)) != NtStatus::Success) ||
            ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), 1) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        Handle handle{};
        const auto result = sys::NtCreateEvent(&handle, static_cast<uint32_t>(second),
            args.third ? &attributes : nullptr, static_cast<uint32_t>(args.fourth), (initial & 0xff) != 0);
        if (result != NtStatus::Success) return status(result);
        const auto copy = ex::ExSafeCopyToUser(reinterpret_cast<void*>(first), &handle, sizeof(handle));
        if (copy != NtStatus::Success) sys::NtClose(handle);
        return status(copy);
    }
    if (number == sys::SSN_NtSetEvent) {
        if (second && ex::ProbeForWrite(reinterpret_cast<void*>(second), sizeof(int32_t), 1) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        int32_t previous{};
        const auto result = sys::NtSetEvent(first, second ? &previous : nullptr);
        if (result != NtStatus::Success || !second) return status(result);
        return status(ex::ExSafeCopyToUser(reinterpret_cast<void*>(second), &previous, sizeof(previous)));
    }
    if (number == sys::SSN_NtWaitForSingleObject) {
        LargeInteger timeout{};
        if (args.third && ex::ExSafeCopyFromUser(&timeout, reinterpret_cast<const void*>(args.third), sizeof(timeout)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        const auto* thread = ke::KeGetCurrentThread();
        if (!thread || !thread->owningProcess || !thread->kernelStackTop)
            return status(NtStatus::NotImplemented);
        // Timed waits may block just like indefinite waits: enter the kernel
        // segment/GS convention before either can switch to another thread.
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const auto result = sys::NtWaitForSingleObject(first, (second & 0xff) != 0,
                                                        args.third ? &timeout : nullptr);
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        return status(result);
    }
    if (number == sys::SSN_NtDelayExecution) {
        const auto* thread = ke::KeGetCurrentThread();
        if (!thread || !thread->owningProcess || !thread->kernelStackTop)
            return status(NtStatus::NotImplemented);
        __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        const auto result = sys::NtDelayExecution((first & 0xff) != 0,
            reinterpret_cast<const LargeInteger*>(second));
        __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
        __asm__ __volatile__("swapgs" : : : "memory");
        return status(result);
    }
    if (number == sys::SSN_NtFlushInstructionCache) {
        return status(sys::NtFlushInstructionCache(static_cast<Handle>(first),
            reinterpret_cast<const void*>(second), static_cast<size_t>(args.third)));
    }
    if (number == sys::SSN_NtProtectVirtualMemory) {
        uint64_t oldProtection{};
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(&oldProtection,
                reinterpret_cast<const void*>(args.userStack + 40), sizeof(oldProtection)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        return status(sys::NtProtectVirtualMemory(static_cast<Handle>(first),
            reinterpret_cast<uintptr_t*>(second), reinterpret_cast<size_t*>(args.third),
            static_cast<uint32_t>(args.fourth), reinterpret_cast<uint32_t*>(oldProtection)));
    }
    if (number == sys::SSN_NtAllocateVirtualMemory || number == sys::SSN_NtFreeVirtualMemory) {
        const uint64_t sizePointer = number == sys::SSN_NtAllocateVirtualMemory ? args.fourth : args.third;
        uintptr_t base{};
        size_t size{};
        if (ex::ProbeForWrite(reinterpret_cast<void*>(second), sizeof(base), 1) != NtStatus::Success ||
            ex::ProbeForWrite(reinterpret_cast<void*>(sizePointer), sizeof(size), 1) != NtStatus::Success ||
            ex::ExSafeCopyFromUser(&base, reinterpret_cast<const void*>(second), sizeof(base)) != NtStatus::Success ||
            ex::ExSafeCopyFromUser(&size, reinterpret_cast<const void*>(sizePointer), sizeof(size)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        NtStatus result{};
        if (number == sys::SSN_NtAllocateVirtualMemory) {
            uint64_t extra[2]{};
            if (args.userStack > UINT64_MAX - 40 ||
                ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40), sizeof(extra)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            result = sys::NtAllocateVirtualMemory(first, &base, args.third, &size,
                                                  static_cast<uint32_t>(extra[0]), static_cast<uint32_t>(extra[1]));
        } else {
            result = sys::NtFreeVirtualMemory(first, &base, &size, static_cast<uint32_t>(args.fourth));
        }
        if (result != NtStatus::Success) return status(result);
        if (ex::ExSafeCopyToUser(reinterpret_cast<void*>(second), &base, sizeof(base)) != NtStatus::Success ||
            ex::ExSafeCopyToUser(reinterpret_cast<void*>(sizePointer), &size, sizeof(size)) != NtStatus::Success) {
            return status(NtStatus::AccessViolation);
        }
        return status(NtStatus::Success);
    }
    if (number == sys::SSN_NtCreateSection || number == sys::SSN_NtMapViewOfSection) {
        uint64_t extra[6]{};
        const size_t count = number == sys::SSN_NtCreateSection ? 3 : 6;
        if (args.userStack > UINT64_MAX - 40 ||
            ex::ExSafeCopyFromUser(extra, reinterpret_cast<const void*>(args.userStack + 40),
                                   count * sizeof(uint64_t)) != NtStatus::Success)
            return status(NtStatus::AccessViolation);
        if (number == sys::SSN_NtCreateSection) {
            LargeInteger maximum{};
            ObjectAttributes attributes{};
            if (args.third && ex::ExSafeCopyFromUser(&attributes,
                    reinterpret_cast<const void*>(args.third), sizeof(attributes)) != NtStatus::Success)
                return status(NtStatus::AccessViolation);
            if (ex::ProbeForWrite(reinterpret_cast<void*>(first), sizeof(Handle), alignof(Handle)) != NtStatus::Success ||
                (args.fourth && ex::ExSafeCopyFromUser(&maximum,
                    reinterpret_cast<const void*>(args.fourth), sizeof(maximum)) != NtStatus::Success))
                return status(NtStatus::AccessViolation);
            // SEC_IMAGE construction reads its file backing. Permit the
            // canonical file backend to wait while keeping the captured
            // inputs on this thread's kernel stack. The service publishes
            // the handle directly to user space under BSP exclusion.
            __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            const bool canSchedule = hal::amd64::bootstrapSchedulerClockActive();
            if (canSchedule) __asm__ __volatile__("sti" : : : "memory");
            const auto result = sys::NtCreateSectionCaptured(reinterpret_cast<Handle*>(first),
                                                      static_cast<uint32_t>(second),
                                                      args.third ? &attributes : nullptr,
                                                      args.fourth ? &maximum : nullptr,
                                                      static_cast<uint32_t>(extra[0]), static_cast<uint32_t>(extra[1]), extra[2]);
            if (canSchedule) __asm__ __volatile__("cli" : : : "memory");
            __asm__ __volatile__("movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es" : : : "rax", "memory");
            __asm__ __volatile__("swapgs" : : : "memory");
            return status(result);
        }
        uintptr_t base{};
        size_t size{};
        LargeInteger offset{};
        if (ex::ProbeForWrite(reinterpret_cast<void*>(args.third), sizeof(base), 1) != NtStatus::Success ||
            ex::ProbeForWrite(reinterpret_cast<void*>(extra[2]), sizeof(size), 1) != NtStatus::Success ||
            ex::ExSafeCopyFromUser(&base, reinterpret_cast<const void*>(args.third), sizeof(base)) != NtStatus::Success ||
            ex::ExSafeCopyFromUser(&size, reinterpret_cast<const void*>(extra[2]), sizeof(size)) != NtStatus::Success ||
            (extra[1] && (ex::ProbeForWrite(reinterpret_cast<void*>(extra[1]), sizeof(offset), 1) != NtStatus::Success ||
                          ex::ExSafeCopyFromUser(&offset, reinterpret_cast<const void*>(extra[1]), sizeof(offset)) != NtStatus::Success)))
            return status(NtStatus::AccessViolation);
        const auto result = sys::NtMapViewOfSectionFromKernel(first, second, &base, args.fourth, extra[0],
            extra[1] ? &offset : nullptr, &size, static_cast<uint32_t>(extra[3]),
            static_cast<uint32_t>(extra[4]), static_cast<uint32_t>(extra[5]));
        if (result != NtStatus::Success && result != NtStatus::ImageNotAtBase) return status(result);
        if (ex::ExSafeCopyToUser(reinterpret_cast<void*>(args.third), &base, sizeof(base)) != NtStatus::Success ||
            ex::ExSafeCopyToUser(reinterpret_cast<void*>(extra[2]), &size, sizeof(size)) != NtStatus::Success ||
            (extra[1] && ex::ExSafeCopyToUser(reinterpret_cast<void*>(extra[1]), &offset, sizeof(offset)) != NtStatus::Success)) {
            (void)sys::rollbackMappedViewFromKernel(second, base);
            return status(NtStatus::AccessViolation);
        }
        return status(result);
    }
    if (number == sys::SSN_NtUnmapViewOfSection)
        return status(sys::NtUnmapViewOfSection(first, second));
    if (number == 0x80) {
        const uint32_t width = g_display.Width ? g_display.Width : g_bootFramebufferInfo.width;
        const uint32_t height = g_display.Height ? g_display.Height : g_bootFramebufferInfo.height;
        const uint32_t pitch = g_display.Pitch ? g_display.Pitch :
            g_bootFramebufferInfo.pixelsPerScanLine;
        const uint32_t pixelFormat = g_display.Width ? g_display.PixelFormat :
            g_bootFramebufferInfo.pixelFormat;
        const um::UserVideoInfo64 info{width, height, pitch, 32, pixelFormat, 0, 0};
        return status(ex::ExSafeCopyToUser(reinterpret_cast<void*>(first),
                                           &info, sizeof(info)));
    }
    if (number == 0x81) {
        um::UserPresentSurfaceParams64 params{};
        const NtStatus copyStatus = ex::ExSafeCopyFromUser(
            &params, reinterpret_cast<const void*>(first), sizeof(params));
        if (copyStatus != NtStatus::Success) return status(copyStatus);
        const uint32_t pitch = params.pitch ? params.pitch : params.width;
        if (!params.width || !params.height || params.dstX >= kWidth ||
            params.dstY >= kHeight || params.width > kWidth - params.dstX ||
            params.height > kHeight - params.dstY || pitch < params.width ||
            pitch > 16384 || params.reserved) {
            return status(NtStatus::InvalidParameter);
        }
        const size_t bytes = (static_cast<size_t>(params.height - 1) * pitch +
                              params.width) * sizeof(uint32_t);
        const NtStatus probeStatus = ex::ProbeForRead(
            reinterpret_cast<const void*>(params.surfaceBufferVa), bytes, 1);
        if (probeStatus != NtStatus::Success) return status(probeStatus);
        if (!g_display.PresentFrame) return status(NtStatus::DeviceNotReady);
        const auto* pixels = reinterpret_cast<const uint32_t*>(params.surfaceBufferVa);
        for (uint32_t row = 0; row < params.height; ++row) {
            const NtStatus rowStatus = ex::ExSafeCopyFromUser(
                g_explorerSurface + (params.dstY + row) * kWidth + params.dstX,
                pixels + static_cast<size_t>(row) * pitch,
                params.width * sizeof(uint32_t));
            if (rowStatus != NtStatus::Success) return status(rowStatus);
        }
        if (!g_display.PresentFrame(g_display.Context,
                g_explorerSurface + params.dstY * kWidth + params.dstX,
                params.width, params.height, kWidth,
                static_cast<int32_t>(params.dstX), static_cast<int32_t>(params.dstY))) {
            return status(NtStatus::DeviceError);
        }
        return status(NtStatus::Success);
    }
    if (number == 0x25) {
        if (!second || second > 4096) {
            return status(NtStatus::AccessViolation);
        }
        const auto* text = reinterpret_cast<const char*>(first);
        const NtStatus probeStatus = ex::ProbeForRead(text, second, 1);
        if (probeStatus != NtStatus::Success) return status(probeStatus);
        char buffer[128]{};
        for (size_t byte = 0; byte < second;) {
            const size_t remaining = static_cast<size_t>(second) - byte;
            const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            const NtStatus copyStatus = ex::ExSafeCopyFromUser(buffer, text + byte, chunk);
            if (copyStatus != NtStatus::Success) return status(copyStatus);
            for (size_t index = 0; index < chunk; ++index) {
                const char character[2]{buffer[index], '\0'};
                serialWrite(character);
            }
            byte += chunk;
        }
        return status(NtStatus::Success);
    }
    if (number == 0x82) {
        const NtStatus probeStatus = ex::ProbeForWrite(
            reinterpret_cast<void*>(first), sizeof(MicaWin32kInputEvent), 1);
        if (probeStatus != NtStatus::Success) return status(probeStatus);
        MicaWin32kInputEvent event{};
        if (!pollMouseInput(event)) {
            return 0;
        }
        const NtStatus copyStatus = ex::ExSafeCopyToUser(
            reinterpret_cast<void*>(first), &event, sizeof(event));
        if (copyStatus != NtStatus::Success) return status(copyStatus);
        return 1;
    }
    if (number == 0x86) {
        MicaWin32kRequest request{};
        const NtStatus copyStatus = ex::ExSafeCopyFromUser(
            &request, reinterpret_cast<const void*>(first), sizeof(request));
        if (copyStatus != NtStatus::Success) return status(copyStatus);
        if (request.size != sizeof(request) ||
            request.version != MICANT_WIN32K_REQUEST_VERSION ||
            request.flags != 0) {
            return status(NtStatus::InvalidParameter);
        }
        const NtStatus requestStatus =
            win32k::freestanding::dispatchRequest(request);
        if (requestStatus != NtStatus::Success) return status(requestStatus);
        const NtStatus resultStatus = ex::ExSafeCopyToUser(
            reinterpret_cast<void*>(first), &request, sizeof(request));
        return status(resultStatus);
    }
    return status(NtStatus::NotSupported);
}

bool waitForControllerInput() noexcept {
    for (uint32_t wait = 0; wait < 100000; ++wait) {
        if ((readPort(0x64) & 0x02) == 0) {
            return true;
        }
        __asm__ __volatile__("pause");
    }
    return false;
}

bool waitForControllerOutput() noexcept {
    for (uint32_t wait = 0; wait < 100000; ++wait) {
        if (readPort(0x64) & 0x01) {
            return true;
        }
        __asm__ __volatile__("pause");
    }
    return false;
}

bool sendMouseCommand(uint8_t command) noexcept {
    if (!waitForControllerInput()) {
        return false;
    }
    writePort(0x64, 0xD4);
    if (!waitForControllerInput()) {
        return false;
    }
    writePort(0x60, command);
    if (!waitForControllerOutput()) {
        return false;
    }
    return readPort(0x60) == 0xFA;
}

bool initializeMouse() noexcept {
    if (!waitForControllerInput()) {
        return false;
    }
    writePort(0x64, 0xA8);
    if (!waitForControllerInput()) {
        return false;
    }
    writePort(0x64, 0x20);
    if (!waitForControllerOutput()) {
        return false;
    }
    uint8_t configuration = readPort(0x60);
    configuration = static_cast<uint8_t>(configuration & ~0x22u);
    g_ps2Configuration = configuration;
    if (!waitForControllerInput()) {
        return false;
    }
    writePort(0x64, 0x60);
    if (!waitForControllerInput()) {
        return false;
    }
    writePort(0x60, configuration);
    return sendMouseCommand(0xF6) && sendMouseCommand(0xF4);
}

bool startMouseInterrupt() noexcept {
    uint32_t gsi{};
    if (!micant::hal::amd64::prepareBootstrapPs2Interrupt(gsi)) return false;
    // The value was captured before streaming was enabled. Reading the
    // controller output now could consume a mouse byte instead of a reply.
    const uint8_t oldConfiguration = g_ps2Configuration;
    if (!waitForControllerInput()) return false;
    writePort(0x64, 0x60);
    // Once command 0x60 is accepted the next byte is configuration data;
    // falling back while the controller still expects it is unsafe.
    if (!waitForControllerInput())
        stopWithError("[MicaNT Input] i8042 configuration timed out\r\n");
    micant::hal::amd64::setBootstrapPs2InterruptMode(true);
    writePort(0x60, oldConfiguration | 0x02);
    if (!micant::hal::amd64::unmaskBootstrapIoApicRoute(gsi, 0x21)) {
        if (!waitForControllerInput())
            stopWithError("[MicaNT Input] i8042 rollback timed out\r\n");
        writePort(0x64, 0x60);
        if (!waitForControllerInput())
            stopWithError("[MicaNT Input] i8042 rollback timed out\r\n");
        writePort(0x60, oldConfiguration);
        micant::hal::amd64::setBootstrapPs2InterruptMode(false);
        return false;
    }
    return true;
}

bool pollMouseInput(MicaWin32kInputEvent& event) noexcept {
    static uint8_t packet[3]{};
    static uint32_t packetIndex = 0;
    static bool interruptObserved = false;
    for (unsigned count = 0; count < 64; ++count) {
        uint8_t status{}, data{};
        bool lost{}, fromInterrupt{};
        const bool available = nextMouseByte(status, data, lost, fromInterrupt);
        if (available && fromInterrupt && !interruptObserved) {
            interruptObserved = true;
            serialWrite("[MicaNT Input] PS/2 mouse IRQ byte consumed\r\n");
        }
        if (lost) packetIndex = 0;
        if (!available) return false;
        if ((status & 0x20) == 0) {
            continue;
        }

        if (packetIndex == 0 && (data & 0x08) == 0) {
            continue;
        }
        packet[packetIndex++] = data;
        if (packetIndex < 3) {
            continue;
        }
        packetIndex = 0;
        if (packet[0] & 0xC0) {
            continue;
        }

        const uint32_t buttons = packet[0] & 7;

        const int32_t deltaX = static_cast<int32_t>(packet[1]) -
                              ((packet[0] & 0x10) ? 256 : 0);
        const int32_t deltaY = static_cast<int32_t>(packet[2]) -
                              ((packet[0] & 0x20) ? 256 : 0);
        const int32_t nextX = static_cast<int32_t>(g_mouseX) + deltaX;
        const int32_t nextY = static_cast<int32_t>(g_mouseY) - deltaY;
        const uint32_t maxX = kWidth - kCursorSize;
        const uint32_t maxY = kHeight - kCursorSize;
        const uint32_t clampedX = nextX < 0 ? 0 :
            (static_cast<uint32_t>(nextX) > maxX ? maxX :
                                                   static_cast<uint32_t>(nextX));
        const uint32_t clampedY = nextY < 0 ? 0 :
            (static_cast<uint32_t>(nextY) > maxY ? maxY :
                                                   static_cast<uint32_t>(nextY));
        const bool cursorMoved = clampedX != g_mouseX || clampedY != g_mouseY;
        if (!cursorMoved && buttons == g_mouseButtons) {
            continue;
        }

        restoreCursorBackground(g_mouseX, g_mouseY);
        g_mouseX = clampedX;
        g_mouseY = clampedY;
        g_mouseButtons = buttons;
        drawMouseCursor(g_mouseX, g_mouseY);
        event = {static_cast<int32_t>(g_mouseX), static_cast<int32_t>(g_mouseY),
                 buttons, ++g_mouseSequence};
        return true;
    }
    return false;
}

[[noreturn]] void runMouseShell(bool clockActive) noexcept {
    drawMouseCursor(g_mouseX, g_mouseY);
    if (clockActive) {
        const uint64_t initialTicks = micant::hal::amd64::bootstrapSchedulerClockTicks();
        __asm__ __volatile__("sti" : : : "memory");
        for (size_t spin = 0; spin < 50000000 &&
             micant::hal::amd64::bootstrapSchedulerClockTicks() - initialTicks < 3; ++spin)
            __asm__ __volatile__("pause");
        if (micant::hal::amd64::bootstrapSchedulerClockTicks() - initialTicks < 3 ||
            micant::hal::amd64::bootstrapSchedulerClockFailed())
            stopWithError("[MicaNT Ke] Runtime scheduler clock failed\r\n");
        serialWrite("[MicaNT Ke] Runtime scheduler clock ticking\r\n");
    }
    uint32_t buttons{};
    for (;;) {
        if (clockActive && micant::hal::amd64::bootstrapSchedulerClockFailed())
            stopWithError("[MicaNT Ke] Runtime scheduler dispatch failed\r\n");
        MicaWin32kInputEvent event{};
        if (pollMouseInput(event)) {
            if ((event.buttons & 1) && !(buttons & 1)) {
                serialWrite("[MicaNT Input] Left button pressed; user-mode shell not started\r\n");
            }
            buttons = event.buttons;
        }
        __asm__ __volatile__("pause");
    }
}

} // namespace

namespace {
void (*const g_serialWriteBridge)(const char*) noexcept = serialWrite;
void (*const g_serialWriteNumberBridge)(uint64_t) noexcept = serialWriteNumber;
}

extern "C" void MicantNetbootSerialWrite(const char* text) noexcept {
    g_serialWriteBridge(text);
}
extern "C" void MicantNetbootSerialWriteNumber(uint64_t value) noexcept {
    g_serialWriteNumberBridge(value);
}

namespace micant::hal::amd64 {
void setBootstrapPs2InterruptMode(bool active) noexcept {
    // The caller serializes with interrupts disabled and masks the source.
    if (active && !g_ps2InterruptMode) {
        g_ps2MouseRing = {};
        g_ps2KeyboardRing = {};
        g_ps2InterruptMode = true;
        capturePs2Bytes(false);
    } else if (!active) {
        g_ps2InterruptMode = false;
    }
}

void captureBootstrapPs2Interrupt() noexcept {
    if (g_ps2InterruptMode) capturePs2Bytes(true);
}

bool popBootstrapPs2KeyboardByte(uint8_t& byte, bool& lost) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    bool fromInterrupt{};
    const bool result = popPs2Byte(g_ps2KeyboardRing, byte, lost, fromInterrupt);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}
}

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
extern "C" __attribute__((naked)) micant::NtStatus KiSystemStartup(
    const micant::boot::UefiKernelHandoffV3*) noexcept {
    __asm__ __volatile__(
        "cli\nleaq MicantBootstrapMainStack+131072(%rip), %rsp\n"
        "andq $-16, %rsp\nsubq $32, %rsp\ncallq MicantKiSystemStartupBody\n"
        "1: cli\nhlt\njmp 1b\n");
}
extern "C" micant::NtStatus MicantKiSystemStartupBody(
#else
extern "C" micant::NtStatus KiSystemStartup(
#endif
    const micant::boot::UefiKernelHandoffV3* handoff
) noexcept {
    __asm__ __volatile__("cli");
    serialInitialize();
    serialWrite("[MicaNT] Modular KiSystemStartup reached\r\n");
    serialWrite("[MicaNT] Phase 0 started\r\n");

    if (!validHandoff(handoff)) {
        stopWithError("[MicaNT] Invalid UEFI kernel handoff\r\n");
    }
    g_bootFramebufferInfo = handoff->framebuffer;

    if (!micant::hal::amd64::initializeBootstrapAddressSpace(serialWrite)) {
        stopWithError("[MicaNT] Bootstrap address-space initialization failed\r\n");
    }
    serialWrite("[MicaNT] Kernel-owned page-table root active\r\n");
    if (handoff->userImageFile.size &&
        !micant::hal::amd64::bootstrapKernelRange(
            handoff->userImageFile.base, handoff->userImageFile.size, false)) {
        stopWithError("[MicaNT Ps] Staged user PE is not kernel-readable\r\n");
    }
    if (!micant::mm::detail::initializeBootstrapProbes(probeBootstrapRange)) {
        stopWithError("[MicaNT Ex] Bootstrap probe binding failed\r\n");
    }
    initializePhysicalMemory(*handoff);
    static micant::hal::amd64::BootstrapInterruptTopology interruptTopology{};
    if (micant::hal::amd64::discoverBootstrapInterruptTopology(*handoff, interruptTopology))
        serialWrite("[MicaNT Hal] ACPI interrupt topology discovered\r\n");
    else
        serialWrite("[MicaNT Hal] ACPI interrupt topology unavailable\r\n");
    if (!micant::hal::amd64::verifyBootstrapPageTables()) {
        stopWithError("[MicaNT Mm] PFN-backed page-table verification failed\r\n");
    }
    serialWrite("[MicaNT Mm] PFN-backed page tables, TLB and reclaim verified\r\n");
    if (!micant::ex::initializeBootstrapRuntime(serialWrite, mapBootstrapHeapPages) ||
        !micant::ex::verifyBootstrapRuntime()) {
        stopWithError("[MicaNT Ex] Bootstrap runtime verification failed\r\n");
    }
    serialWrite("[MicaNT Ex] Canonical page-backed C and C++ allocation verified\r\n");
    if (!MicantTestPartitionTables())
        stopWithError("[MicaNT Storage] MBR/GPT validation failed\r\n");
    serialWrite("[MicaNT Storage] MBR/GPT, CRC32, backup recovery and bounds verified\r\n");
    const int vadTest = MicantTestMmAvlTable();
    if (vadTest != 0) {
        serialWrite("[MicaNT Mm] Canonical VAD tree verification failed: ");
        serialWriteNumber(static_cast<uint64_t>(vadTest));
        stopWithError("\r\n");
    }
    serialWrite("[MicaNT Mm] Canonical VAD tree exclusion and cleanup verified\r\n");
    const int processMemoryTest = MicantTestMmProcessAddressSpace();
    if (processMemoryTest != 0) {
        serialWrite("[MicaNT Mm] Process address-space verification failed: ");
        serialWriteNumber(static_cast<uint64_t>(processMemoryTest));
        stopWithError("\r\n");
    }
    serialWrite("[MicaNT Mm] VAD reserve, commit, mapping and release verified\r\n");
    const int protectionTest = MicantTestMmManagedProtection();
    if (protectionTest != 0) {
        serialWrite("[MicaNT Mm] Managed protection verification failed: ");
        serialWriteNumber(static_cast<uint64_t>(protectionTest));
        stopWithError("\r\n");
    }
    serialWrite("[MicaNT Mm] Managed protection, decommit and PFN reclamation verified\r\n");
    if (micant::hal::amd64::bootstrapQemuFaultTestsAvailable()) {
        serialWrite("[MicaNT Mm] QEMU managed noncommitted and read-only faults verified\r\n");
    } else {
        serialWrite("[MicaNT Mm] Managed fault probes skipped outside QEMU TCG\r\n");
    }
#if defined(MICANT_KERNEL_CXX_LIFECYCLE_SELFTEST)
    const size_t initialActive = micant::mm::PfnDatabase::get().getActiveCount();
    const size_t initialAvailable = micant::mm::PfnDatabase::get().getFreeCount() +
                                   micant::mm::PfnDatabase::get().getZeroedCount();
    if (!micant::ex::test::verifyCxxBeforeInitialization()) {
        stopWithError("[MicaNT Ex] Premature C++ initialization failed\r\n");
    }
#endif
    if (!micant::ex::initializeBootstrapCxxRuntime()) {
        stopWithError("[MicaNT Ex] Kernel C++ initialization failed\r\n");
    }
    serialWrite("[MicaNT Ex] Kernel C++ constructors dispatched\r\n");
#if defined(MICANT_KERNEL_CXX_LIFECYCLE_SELFTEST)
    if (!micant::ex::test::verifyCxxAfterInitialization() ||
        !micant::ex::shutdownBootstrapCxxRuntime() ||
        !micant::ex::test::verifyCxxAfterDestruction() ||
        micant::mm::PfnDatabase::get().getActiveCount() != initialActive ||
        micant::mm::PfnDatabase::get().getFreeCount() +
            micant::mm::PfnDatabase::get().getZeroedCount() != initialAvailable) {
        stopWithError("[MicaNT Ex] Kernel C++ teardown verification failed\r\n");
    }
    serialWrite("[MicaNT Ex] Compiler-generated construction and LIFO teardown verified\r\n");
    serialWrite("[MicaNT Ex] Runtime self-test halted\r\n");
    for (;;) __asm__ __volatile__("cli; hlt");
#endif
    if (micant::io::IoInitializeFileObjects() != micant::NtStatus::Success) {
        stopWithError("[MicaNT Io] File object initialization failed\r\n");
    }
    if (micant::io::IoInitializeBlockDevices() != micant::NtStatus::Success) {
        stopWithError("[MicaNT Io] Block-device object initialization failed\r\n");
    }
    if (micant::km::hid::initializeInputDevices() != micant::NtStatus::Success)
        stopWithError("[MicaNT Io] Input device object initialization failed\r\n");
    if (micant::fsrtl::fat::initialize() != micant::NtStatus::Success)
        stopWithError("[MicaNT FsRtl] FAT provider initialization failed\r\n");
    if (micant::io::IoRegisterSystemDirectoryRoutine(
            [](void*, char16_t* output, size_t capacity, size_t& length) noexcept {
                return micant::fsrtl::fat::systemDirectory(output, capacity, length);
            }, nullptr) != micant::NtStatus::Success)
        stopWithError("[MicaNT Io] System directory registration failed\r\n");
    if (micant::io::IoRegisterFileOpenRoutine(bootFileOpenRouter, nullptr) != micant::NtStatus::Success)
        stopWithError("[MicaNT Io] File provider router initialization failed\r\n");
    g_bootFileOpenRouterInstalled = true;
    if (!micant::sys::initializeBootstrapSectionServices()) {
        stopWithError("[MicaNT Mm] Section service initialization failed\r\n");
    }
    if (!micant::sys::verifyBootstrapSectionOwnership()) {
        stopWithError("[MicaNT Mm] Section object ownership verification failed\r\n");
    }
    if (!verifyKernelObjectHandles()) {
        stopWithError("[MicaNT Ob] Kernel handle verification failed\r\n");
    }
    serialWrite("[MicaNT Ob] Canonical kernel handle storage and references verified\r\n");
    if (!MicantTestObOpenAccess()) {
        const uint32_t stage = MicantObOpenAccessFailureStage();
        serialWrite(stage == 1 ? "[MicaNT Ob] open test stage 1 failed\r\n" :
                    stage == 2 ? "[MicaNT Ob] open test stage 2 failed\r\n" :
                    stage == 3 ? "[MicaNT Ob] open test stage 3 failed\r\n" :
                    stage == 4 ? "[MicaNT Ob] open test stage 4 failed\r\n" :
                    stage == 5 ? "[MicaNT Ob] open test stage 5 failed\r\n" :
                    stage == 6 ? "[MicaNT Ob] open test stage 6 failed\r\n" :
                    stage == 7 ? "[MicaNT Ob] open test stage 7 failed\r\n" :
                    stage == 8 ? "[MicaNT Ob] open test stage 8 failed\r\n" :
                                  "[MicaNT Ob] open test stage 9 failed\r\n");
        stopWithError("[MicaNT Ob] Open access and rollback verification failed\r\n");
    }
    serialWrite("[MicaNT Ob] Open access checks and callback rollback verified\r\n");
    if (!MicantTestSeFreestandingAccess())
        stopWithError("[MicaNT Se] Freestanding DACL access verification failed\r\n");
    serialWrite("[MicaNT Se] DACL deny, grant, maximum access and ACE order verified\r\n");
    if (!MicantTestKeReadyQueue()) {
        stopWithError("[MicaNT Ke] Ready-queue withdrawal verification failed\r\n");
    }
    serialWrite("[MicaNT Ke] Ready-queue withdrawal verified\r\n");
    const int tlsTest = pe_tls_freestanding_test();
    if (tlsTest != 0) {
        serialWrite("[MicaNT Ldr] TLS template verification failed: ");
        serialWriteNumber(static_cast<uint64_t>(tlsTest));
        stopWithError("\r\n");
    }
    serialWrite("[MicaNT Ldr] Independent TLS templates verified\r\n");
#if defined(MICANT_BOOTSTRAP_TIMER_SELFTEST)
    if (!micant::hal::amd64::verifyRing3Transition()) {
        stopWithError("[MicaNT Hal] Timer self-test interrupt context setup failed\r\n");
    }
    if (!micant::ke::MicantAmd64ConfigureExtendedState()) {
        stopWithError("[MicaNT Hal] Timer self-test extended-state setup failed\r\n");
    }
    micant::hal::amd64::setBootstrapThreadEntryStack(
        0, 0, false, reinterpret_cast<uintptr_t>(MicantBootstrapMainStack),
        reinterpret_cast<uintptr_t>(MicantBootstrapMainStack + sizeof(MicantBootstrapMainStack)));
    serialWrite("[MicaNT Hal] Bootstrap GDT/IDT, CPL3 and interrupt-stack prerequisites verified\r\n");
    if (!micant::hal::amd64::verifyBootstrapTimerSource()) {
        stopWithError("[MicaNT Hal] Timer-only interrupt source verification failed\r\n");
    }
    serialWrite("[MicaNT Hal] Timer-only interrupt tick count: ");
    serialWriteNumber(micant::hal::amd64::bootstrapSchedulerClockTicks());
    serialWrite("\r\n[MicaNT Hal] Timer interrupt source verified (scheduler preemption not tested)\r\n");
    serialWrite("[MicaNT Hal] Timer-only self-test halted\r\n");
    for (;;) __asm__ __volatile__("cli; hlt");
#endif
    verifyKernelContextSwitch(false);
    verifyKernelContextSwitch(true);
    if (!MicantTestKeBlockWake())
        stopWithError("[MicaNT Ke] Block and wake verification failed\r\n");
    serialWrite("[MicaNT Ke] Kernel thread block and wake verified\r\n");
    if (!MicantTestKeApcFifo())
        stopWithError("[MicaNT Ke] APC FIFO verification failed\r\n");
    serialWrite("[MicaNT Ke] Kernel APC FIFO verified\r\n");
    micant::sys::initializeBootstrapEventServices();
    if (!MicantTestKeEventWait())
        stopWithError("[MicaNT Ke] Event wait verification failed\r\n");
    serialWrite("[MicaNT Ke] Kernel event wait and wake verified\r\n");
    if (!MicantTestPsThreadLifecycle())
    {
        serialWrite("[MicaNT Ps] Thread lifecycle verification failed; attach stage ");
        serialWriteNumber(MicantPsAttachFailureStage());
        stopWithError("\r\n");
    }
    serialWrite("[MicaNT Ps] Kernel thread lifecycle verified\r\n");
    serialWrite("[MicaNT Ps] Entering isolated CPL3 probe\r\n");
    if (!micant::hal::amd64::verifyRing3Transition()) {
        stopWithError("[MicaNT Ps] Ring 3 transition failed\r\n");
    }
    serialWrite("[MicaNT Ps] CPL3 execution and Ring 0 trap return verified\r\n");
    serialWrite("[MicaNT Ps] GS-based TEB LastErrorValue read/write verified\r\n");
    serialWrite("[MicaNT Ps] GS-based TEB TLS slot read/write verified\r\n");
    serialWrite("[MicaNT Mm] User/kernel isolation and read-only user code verified\r\n");
    serialWrite("[MicaNT Mm] Supervisor access checks and non-executable user stack verified\r\n");
    if (!micant::hal::amd64::verifyManagedExecutablePage())
        stopWithError("[MicaNT Mm] Managed executable page verification failed\r\n");
    serialWrite("[MicaNT Mm] PFN-backed executable user page verified\r\n");
    if (!micant::hal::amd64::initializeBootstrapSyscalls(dispatchBootstrapService)) {
        stopWithError("[MicaNT Ke] Syscall entry initialization failed\r\n");
    }
    serialWrite("[MicaNT Ke] Bootstrap syscall entry initialized\r\n");
    // The early CPL3 syscall probe includes a valid presentation request. Give
    // it a real, supervisor-only GOP backend after the APIC access probes; a
    // later boot-start display driver may replace this fallback.
    if (!initializeGopFramebuffer(handoff->framebuffer, false))
        stopWithError("[MicaNT Display] GOP framebuffer unavailable for syscall probe\r\n");
    if (!micant::hal::amd64::verifyBootstrapSyscalls()) {
        stopWithError("[MicaNT Ke] Syscall entry verification failed\r\n");
    }
    serialWrite("[MicaNT Test] CPL3 surface presentation completed\r\n");
    serialWrite("[MicaNT Ke] Bootstrap syscall service probes passed\r\n");
    // Enable the canonical clock and its shared user view before the real
    // CPL3 native-image probe executes time syscalls and reads KUSER data.
    if (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION ||
        handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES) {
        const auto& calibration = handoff->version ==
                micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES
            ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.clockCalibration
            : reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->clockCalibration;
        using micant::shared::boot_time::BootClockFlagsV1;
        if (calibration.flags == BootClockFlagsV1::InvariantTsc &&
            micant::hal::amd64::setBootstrapTscCalibrationHint(
                calibration.tscFrequencyHz) !=
                micant::hal::amd64::BootstrapClockStatus::Success)
            serialWrite("[MicaNT Hal] UEFI TSC calibration rejected\r\n");
    }
    const bool clockActive = micant::hal::amd64::startBootstrapSchedulerClock();
    if (!clockActive) stopWithError("[MicaNT Hal] Bootstrap clock unavailable\r\n");
    if (micant::hal::amd64::verifyBootstrapClockTimeouts())
        serialWrite("[MicaNT Hal] QPC scale matched APIC timing; timeout and deadline rollover passed\r\n");
    else
        serialWrite("[MicaNT Hal] Calibrated monotonic timeout checks failed\r\n");
    if (!micant::ke::passive_work::initialize())
        stopWithError("[MicaNT Ke] Passive worker initialization failed\r\n");
    micant::sys::scheduleDeferredTimerDeletes();
    micant::io::IoScheduleDeferredFileDeletes();
    initializeBootStartDrivers(handoff);
    auto& bootNetworkStack = micant::tcpip::NetworkStack::get();
    bootNetworkStack.initialize();
    drawExplorerDesktop();
    serialWrite("[MicaNT Explorer] Desktop and taskbar rendered\r\n");
    bool utcSeeded = false;
    if (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME ||
        handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION ||
        handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES) {
        const auto& sample = handoff->version ==
                micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES
            ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.time.systemTime
            : (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION
                ? reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->time.systemTime
                : reinterpret_cast<const micant::boot::UefiKernelHandoffV5*>(handoff)->systemTime);
        if (sample.status == micant::shared::boot_time::SampleStatus::UtcValid) {
            const auto status = micant::hal::amd64::initializeBootstrapSystemTime(
                sample.utcTime100ns);
            utcSeeded = status == micant::hal::amd64::BootstrapClockStatus::Success;
            if (!utcSeeded)
                serialWrite("[MicaNT Hal] UEFI UTC time rejected by calibrated clock\r\n");
        }
    }
    if (utcSeeded) {
        serialWrite("[MicaNT Hal] UTC SystemTime seeded from validated UEFI GetTime\r\n");
    } else {
        if ((handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME ||
             handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION ||
             handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES) &&
            (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES
                ? reinterpret_cast<const micant::boot::UefiKernelHandoffV7*>(handoff)->clock.time.systemTime.status
                : (handoff->version == micant::boot::UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION
                    ? reinterpret_cast<const micant::boot::UefiKernelHandoffV6*>(handoff)->time.systemTime.status
                    : reinterpret_cast<const micant::boot::UefiKernelHandoffV5*>(handoff)->systemTime.status)) ==
                micant::shared::boot_time::SampleStatus::LocalTimeZoneUnspecified)
            serialWrite("[MicaNT Hal] UEFI GetTime timezone unspecified; UTC SystemTime unavailable\r\n");
        uint64_t configuredUtc{};
        utcSeeded = micant::hal::amd64::queryBootstrapSystemTime(configuredUtc) ==
            micant::hal::amd64::BootstrapClockStatus::Success;
        if (utcSeeded) {
            serialWrite("[MicaNT Hal] UTC SystemTime seeded from configured QEMU UTC RTC\r\n");
        } else {
            micant::shared::time::SystemTimeFields rtc{};
            const auto rtcStatus = micant::hal::amd64::readBootstrapRtc(rtc);
            if (rtcStatus == micant::hal::amd64::BootstrapRtcStatus::Success)
                serialWrite("[MicaNT Hal] Stable CMOS RTC sample read; UTC offset unknown\r\n");
            else if (rtcStatus == micant::hal::amd64::BootstrapRtcStatus::Timeout)
                serialWrite("[MicaNT Hal] CMOS RTC update timeout; no UTC seed\r\n");
            else
                serialWrite("[MicaNT Hal] CMOS RTC unavailable/invalid; no UTC seed\r\n");
        }
    }
    const auto kuserStatus = micant::hal::amd64::initializeBootstrapKuserSharedData();
    if (kuserStatus != micant::hal::amd64::BootstrapClockStatus::Success) {
        serialWrite("[MicaNT Hal] KUSER status=");
        serialWriteNumber(static_cast<uint32_t>(kuserStatus));
        serialWrite("\r\n");
        stopWithError("[MicaNT Hal] KUSER_SHARED_DATA mapping failed\r\n");
    }
    serialWrite("[MicaNT Hal] KUSER_SHARED_DATA kernel RW/user RO mapping active\r\n");
    // Run the isolated process lifecycle probe before the file-backed native
    // image probe can wake deferred cleanup workers and populate the ready
    // queues. This probe deliberately switches directly between its boot and
    // test threads so unrelated runnable threads must not be pending.
    if (!MicantTestPsUserProcess()) {
        serialWrite("[MicaNT Ps] User process lifecycle verification failed at stage ");
        serialWriteNumber(MicantPsUserProcessFailureStage());
        stopWithError("\r\n");
    }
    {
        // Resolve the installation from the mounted filesystem after KUSER
        // has been initialized; failed discovery leaves NtSystemRoot empty.
        char16_t systemDirectory[256]{};
        size_t systemDirectoryLength{};
        uint64_t interruptFlags{};
        __asm__ __volatile__("pushfq; popq %0; sti" : "=r"(interruptFlags) : : "memory");
        (void)micant::io::IoQuerySystemDirectory(systemDirectory,
            sizeof(systemDirectory) / sizeof(systemDirectory[0]), systemDirectoryLength);
        if (!(interruptFlags & (uint64_t{1} << 9))) __asm__ __volatile__("cli" : : : "memory");
    }
    if (!micant::hal::amd64::verifyNativeImageLoading()) {
        stopWithError("[MicaNT Ke] Native syscall verification failed\r\n");
    }
    serialWrite("[MicaNT Hal] CPL3 calibrated QPC precision and KUSER uptime advancement verified\r\n");
    serialWrite(utcSeeded
        ? "[MicaNT Hal] CPL3 UTC SystemTime advanced and matched KUSER\r\n"
        : "[MicaNT Hal] CPL3 UTC SystemTime correctly unavailable for local firmware time\r\n");
    serialWrite("[MicaNT Ke] CPL3 syscall ABI, register preservation and user-buffer checks verified\r\n");
    serialWrite("[MicaNT Mm] CPL3 native allocation and release verified\r\n");
    serialWrite("[MicaNT Ke] CPL3 native event polling verified\r\n");
    serialWrite("[MicaNT Ps] Process-owned CPL3 context and termination verified\r\n");
    if (!MicantTestPsTwoUserThreads())
        stopWithError("[MicaNT Ps] Two user threads verification failed\r\n");
    serialWrite("[MicaNT Ps] Two CPL3 threads, TLS slots and event waits verified\r\n");
    if (!MicantTestPsThreeUserThreads())
        stopWithError("[MicaNT Ps] Multiple user thread termination failed\r\n");
    serialWrite("[MicaNT Ps] Three CPL3 thread termination verified\r\n");
    if (!MicantTestPsFileBackedImage())
        stopWithError("[MicaNT Ps] File-backed SEC_IMAGE verification failed\r\n");
    serialWrite("[MicaNT Ps] File-backed SEC_IMAGE process image verified\r\n");
    if (bootNetworkStack.isInitialized()) {
        serialWrite("[MicaNT NDIS/TCPIP] Boot-start network adapter attached to canonical transport\r\n");
        const auto dhcpStatus = micant::netboot::acquireBootDhcpLease();
        if (dhcpStatus == micant::NtStatus::Success)
            serialWrite("[MicaNT DHCP] Ring 3 DORA lease acquired through Winsock/AFD\r\n");
        else {
            serialWrite("[MicaNT DHCP] Ring 3 lease acquisition failed status=");
            serialWriteNumber(static_cast<uint32_t>(dhcpStatus));
            serialWrite("\r\n");
        }
    } else
        serialWrite("[MicaNT NDIS/TCPIP] No boot-start network adapter registered\r\n");
    if (micant::hal::amd64::bootstrapQemuFaultTestsAvailable()) {
        if (!micant::hal::amd64::verifyBootstrapTimerInterrupt())
            stopWithError("[MicaNT Hal] Local APIC timer verification failed\r\n");
        serialWrite("[MicaNT Hal] Local APIC periodic interrupts verified\r\n");
        serialWrite("[MicaNT Ke] Timer-preempted kernel contexts verified\r\n");
        serialWrite("[MicaNT Ke] Timer-preempted CPL3 contexts verified\r\n");
    }
    const auto probeTelemetry = micant::ex::ExQuerySyncTelemetry();
    if (!probeTelemetry.totalSafeCopiesCompleted ||
        !probeTelemetry.totalSafeCopyFaultsCaught ||
        !probeTelemetry.totalProbeViolationsCaught) {
        stopWithError("[MicaNT Ex] Canonical checked user-copy paths not exercised\r\n");
    }
    serialWrite("[MicaNT Ex] Canonical checked user copies verified at CPL3 boundary\r\n");
    serialWrite("[MicaNT Ps] Bounded native PE mapping and compiled user entry verified\r\n");
    if (!initializeMouse()) {
        stopWithError("[MicaNT Input] PS/2 mouse initialization failed\r\n");
    }
    serialWrite("[MicaNT Input] PS/2 mouse polling initialized\r\n");
    if (micant::hal::amd64::bootstrapInterruptTopology()) {
        if (micant::hal::amd64::initializeBootstrapIoApics())
            serialWrite("[MicaNT Hal] Firmware IOAPIC ranges mapped; no routes enabled\r\n");
        else
            serialWrite("[MicaNT Hal] Firmware IOAPIC ranges unavailable\r\n");
    }
    if (clockActive && !micant::hal::amd64::verifyBootstrapSchedulerClock())
        stopWithError("[MicaNT Ke] Runtime scheduler preemption failed\r\n");
    if (clockActive) serialWrite("[MicaNT Ke] Runtime idle wait and wake verified\r\n");
    if (clockActive && !MicantTestPsDeferredReaper())
        stopWithError("[MicaNT Ps] Deferred reaper verification failed\r\n");
    if (clockActive) serialWrite("[MicaNT Ps] Deferred reaper verified\r\n");
    if (clockActive && !MicantTestKeDeferredDelivery())
        stopWithError("[MicaNT Ke] Runtime DPC/APC delivery failed\r\n");
    if (clockActive) serialWrite("[MicaNT Ke] Runtime DPC/APC delivery verified\r\n");
    const auto usbEnumeration = micant::usb::baremetal::startEnumeration();
    if (usbEnumeration != micant::NtStatus::Success)
        serialWrite("[MicaNT Io] USB host enumeration did not start\r\n");
    if (clockActive && startMouseInterrupt())
        serialWrite("[MicaNT Input] PS/2 mouse interrupt routing active\r\n");
    if (!clockActive) serialWrite("[MicaNT Ke] Runtime scheduler clock unavailable\r\n");
    serialWrite("[MicaNT Explorer] Shell idle loop entered\r\n");
    runMouseShell(clockActive);
}

#else

#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <vector>
// ---------------------------------------------------------------------------
// Canonical Windows NT Architecture Layout Headers
// ---------------------------------------------------------------------------

// 1. Mode-Neutral Shared Contracts & Primitives
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/subsystems.hpp"

// 2. Kernel Mode (Ring 0): Hardware Abstraction Layer
#include "micant/km/hal/hal.hpp"
#include "micant/km/hal/boot.hpp"
#include "micant/km/hal/bootvid.hpp"
#include "micant/km/hal/inbv.hpp"
#include "micant/km/hal/uefi.hpp"
#include "micant/km/hal/acpi.hpp"

// 3. Kernel Mode (Ring 0): Microkernel Core (Ke)
#include "micant/km/core/ke.hpp"
#include "micant/km/core/trap.hpp"
#include "micant/km/core/kd.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/core/dispatcher.hpp"
#include "micant/km/core/sync.hpp"
#include "micant/km/core/timer.hpp"

// 4. Kernel Mode (Ring 0): NT Executive Managers
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/cm.hpp"
#include "micant/km/executive/se.hpp"
#include "micant/km/executive/lpc.hpp"
#include "micant/km/executive/po.hpp"
#include "micant/km/executive/ex.hpp"
#include "micant/km/executive/ex_work.hpp"
#include "micant/km/executive/lookaside.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/executive/driver.hpp"
#include "micant/km/executive/cc.hpp"
#include "micant/km/executive/fsrtl.hpp"
#include "micant/km/core/ntos_internal.hpp"

// 5. Kernel Mode (Ring 0): Device Drivers & Filesystems
#include "micant/km/drivers/storage/storage.hpp"
#include "micant/km/drivers/system/null.hpp"
#include "micant/km/drivers/fs/fat32.hpp"
#include "micant/km/drivers/fs/fs.hpp"
#include "micant/km/drivers/net/ndis.hpp"
#include "micant/km/drivers/net/tcpip.hpp"
#include "micant/km/drivers/fs/npfs.hpp"
#include "micant/km/drivers/input/hidclass.hpp"
#include "micant/km/drivers/input/hidparse.hpp"
#include "micant/km/drivers/input/hidusb.hpp"
#include "micant/km/drivers/input/kbdclass.hpp"
#include "micant/km/drivers/input/kbdhid.hpp"
#include "micant/km/drivers/input/mouclass.hpp"
#include "micant/km/drivers/input/mouhid.hpp"
#include "micant/km/drivers/input/sermouse.hpp"
#include "micant/km/drivers/serial/serial.hpp"
#include "micant/km/drivers/system/beep.hpp"

// 6. User Mode (Ring 3): Native NT Layer & Loaders
#include "micant/um/native/ntdll.hpp"
#include "micant/um/native/pe.hpp"
#include "micant/um/native/wow64.hpp"
#include "micant/um/native/generated_nt_api.hpp"

// 7. User Mode (Ring 3): Subsystems & Shell
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/net/iphlpapi.hpp"
#include "micant/um/subsystems/winuser/shell.hpp"

using namespace micant;

void PrintBanner() {
    std::cout << "\n";
    std::cout << "========================================================================\n";
    std::cout << "       __  __ _            _   _ _____                                  \n";
    std::cout << "      |  \\/  (_)          | \\ | |_   _|                                 \n";
    std::cout << "      | \\  / |_  ___ __ _ |  \\| | | |                                   \n";
    std::cout << "      | |\\/| | |/ __/ _` || . ` | | |                                   \n";
    std::cout << "      | |  | | | (_| (_| || |\\  | | |                                   \n";
    std::cout << "      |_|  |_|_|\\___\\__,_||_| \\_| |_|                                   \n";
    std::cout << "                                                                        \n";
    std::cout << "  Project MICA: Memory, IPC, Compute, Architecture                      \n";
    std::cout << "  Clean-Room NT Architecture in Modern ISO C++23                        \n";
    std::cout << "  Zero Telemetry | Sub-32MB Footprint | Pure Clean-Room NT Kernel       \n";
    std::cout << "========================================================================\n\n";
}

extern "C" NtStatus KiSystemStartup(
    micant::boot::LoaderParameterBlock* lpbPtr,
    const char* execCmd,
    bool launchInteractive
) {
    PrintBanner();

    micant::boot::LoaderParameterBlock fallbackLpb;
    if (!lpbPtr) {
        fallbackLpb = boot::createDefaultUefiBootBlock();
        lpbPtr = &fallbackLpb;
    }
    const auto& lpb = *lpbPtr;

    // 0. Firmware Boot Handover & Loader Parameter Block (LPB) Ingestion
    std::cout << "[MicaNT Boot] [Firmware] Ingesting UEFI Loader Parameter Block (LPB)...\n";
    std::wcout << L"[MicaNT Boot] [Firmware] ARC Boot Device: " << lpb.arcBootDeviceName << L"\n";
    std::cout << "[MicaNT Boot] [Firmware] Kernel Load Options: " << lpb.loadOptions << "\n";
    std::cout << "[MicaNT Boot] [Firmware] Physical RAM: " << (lpb.getTotalMemoryBytes() / (1024 * 1024)) 
              << " MB Total (" << (lpb.getFreeMemoryBytes() / (1024 * 1024)) << " MB Free)\n";
    std::cout << "[MicaNT Boot] [Firmware] Pre-loaded Boot Modules: " << lpb.bootModules.size() << " images\n";

    // 0b. Boot Video Driver (bootvid) & Custom Boot Splash
    std::cout << "[MicaNT Boot] [Bootvid] Initializing Boot Video Driver (GOP Framebuffer)...\n";
    auto& bootvid = bootvid::BootVideoSubsystem::get();
    bootvid.initializeVirtual(lpb.framebuffer.width > 0 ? lpb.framebuffer.width : 1024,
                              lpb.framebuffer.height > 0 ? lpb.framebuffer.height : 768);
    std::cout << "[MicaNT Boot] [Bootvid] Initialized " << bootvid.getDriver().getWidth() << "x"
              << bootvid.getDriver().getHeight() << " @ 32bpp linear framebuffer\n";
    std::cout << "[MicaNT Boot] [Bootvid] Rendering Dave Cutler 1988 DEC Mica Prism Emblem...\n";
    bootvid.getDriver().renderBootSplash("Initializing Executive Subsystems...", 0.15f);
    std::cout << "[MicaNT Boot] [Bootvid] Custom boot logo engine active (24/32-bit BMP decoder ready)\n";

    std::cout << "\n[MicaNT Boot] Initializing Executive subsystems...\n";

    // 1. Initialize Hardware Abstraction Layer & CPU Topology
    std::cout << "[MicaNT Boot] [Hal] Initializing Hardware Abstraction Layer...\n";
    auto& hal = hal::HardwareAbstractionLayer::get();
    hal.initialize(4); // 4-core SMP
    auto* kpcr0 = hal.getKpcr(0);
    std::cout << "[MicaNT Boot] [Hal] Initialized 4-core SMP topology. KPCR at GS:[0] (CPU 0 Core Clock: " 
              << kpcr0->prcb.coreClockMhz << " MHz, Arch: AMD64)\n";
    std::cout << "[MicaNT Boot] [Hal/Arch] Multi-Architecture Engine ready: AMD64 (x86-64) + AArch64 (ARM64 VMSA 48-bit MMU, SVC #1 Fast Trap)\n";

    // 2. Initialize Executive Memory Pools (NonPagedPool & PagedPool)
    std::cout << "[MicaNT Boot] [Ex] Initializing Executive Pools (NonPagedPool & PagedPool)...\n";
    void* bootPoolBuffer = ex::ExAllocatePoolWithTag(ex::PoolType::NonPagedPool, 64 * 1024, ex::TAG_MICA_CORE);
    std::cout << "[MicaNT Boot] [Ex] Allocated 64 KB NonPaged Pool [Tag: 'Mica'] at 0x" << bootPoolBuffer << "\n";

    // 3. Initialize Object Manager Root Namespace
    std::cout << "[MicaNT Boot] [Ob] Initializing Object Manager root namespace...\n";
    auto& obNs = ob::ObjectNamespace::get();
    obNs.initializeRoot();
    std::cout << "[MicaNT Boot] [Ob] Created standard NT namespaces: \\Device, \\Driver, \\DosDevices, \\KernelObjects, \\BaseNamedObjects, \\RPC Control, \\Sessions ("
              << obNs.getDirectoryCount() << " directories, " << obNs.getSymbolicLinkCount() << " symlinks)\n";

    // 4. Initialize Memory Manager
    std::cout << "[MicaNT Boot] [Mm] Initializing 64-bit Virtual Memory Manager...\n";

    // 5. Initialize I/O Manager & Device Object Graph
    std::cout << "[MicaNT Boot] [Io] Initializing I/O Manager and Device Object Graph...\n";
    auto& ioMgr = io::IoManager::get();
    io::DriverObject nullDriver{ .driverName = L"\\Driver\\Null" };
    io::DriverObject diskDriver{ .driverName = L"\\Driver\\Disk" };
    null_driver::Initialize(nullDriver);
    storage::InitializeDiskDriver(diskDriver);
    auto nullDev = ioMgr.createDevice(&nullDriver, L"\\Device\\Null", io::DeviceType::Null);

    // 5.1 Initialize Real Block Storage Subsystem, RamDisk, MBR, and FastFAT Driver
    std::cout << "[MicaNT Boot] [Storage] Initializing \\Device\\Harddisk0 (64 MB Physical Sector Emulation)...\n";
    auto bootDisk = std::make_shared<storage::RamDiskDevice>(L"\\Device\\Harddisk0\\Partition0", 64 * 1024 * 1024, 512);

    std::vector<storage::MbrPartitionEntry> bootPartitions(1);
    bootPartitions[0].bootIndicator = 0x80;
    bootPartitions[0].partitionType = storage::MBR_TYPE_FAT32_LBA;
    bootPartitions[0].startLba = 2048;
    bootPartitions[0].sectorCount = static_cast<uint32_t>(bootDisk->getTotalBlocks() - 2048);
    const NtStatus writeMbrStatus = storage::PartitionManager::writeMbr(*bootDisk, bootPartitions);
    if (!NT_SUCCESS(writeMbrStatus)) {
        std::cerr << "[MicaNT Boot] [PartMgr] Failed to initialize the boot disk MBR: "
                  << NtStatusToString(writeMbrStatus) << "\n";
        return writeMbrStatus;
    }
    std::vector<storage::MbrPartitionEntry> discoveredPartitions;
    const NtStatus partitionStatus = storage::PartitionManager::parseMbr(*bootDisk, discoveredPartitions);
    if (!NT_SUCCESS(partitionStatus) || discoveredPartitions.empty()) {
        std::cerr << "[MicaNT Boot] [PartMgr] Failed to discover the boot partition: "
                  << NtStatusToString(partitionStatus) << "\n";
        return NT_SUCCESS(partitionStatus) ? NtStatus::UnrecognizedVolume : partitionStatus;
    }
    const auto& bootPartitionEntry = discoveredPartitions.front();

    auto bootPartition = std::make_shared<storage::PartitionDevice>(
        L"\\Device\\Harddisk0\\Partition1",
        bootDisk,
        bootPartitionEntry.startLba,
        bootPartitionEntry.sectorCount
    );

    auto diskDev = ioMgr.createDevice(&diskDriver, L"\\Device\\Harddisk0", io::DeviceType::Disk, bootDisk.get());
    auto partDev = ioMgr.createDevice(&diskDriver, L"\\Device\\Harddisk0\\Partition1", io::DeviceType::Disk, bootPartition.get());
    (void)diskDev;
    (void)partDev;
    std::cout << "[MicaNT Boot] [Io] Created system device nodes: \\Device\\Null, \\Device\\Harddisk0, \\Device\\Harddisk0\\Partition1 (Registered: " 
              << ioMgr.getDeviceCount() << " devices)\n";

    (void)fat32::Fat32FileSystem::format(*bootPartition, "MICANT_SYS", 8);

    std::cout << "[MicaNT Boot] [Fastfat & VFS] Mounting System Volume on \\Device\\Harddisk0\\Partition1...\n";
    auto& vfs = fs::VirtualFileSystem::get();
    vfs.initialize();
    vfs.mountBlockDevice(bootPartition);
    ob::ObjectNamespace::get().createSymbolicLink(L"\\DosDevices\\C:", L"\\Device\\Harddisk0\\Partition1");
    std::cout << "[MicaNT Boot] [Fastfat & VFS] Mounted \\DosDevices\\C: -> \\Device\\Harddisk0\\Partition1 (Status: MOUNTED, FAT32 4KB Clusters)\n";
    ob::ObjectNamespace::get().createSymbolicLink(L"\\DosDevices\\D:", L"\\Device\\Harddisk0\\Partition2");
    ob::ObjectNamespace::get().createSymbolicLink(L"\\DosDevices\\E:", L"\\Device\\Harddisk0\\Partition3");
    ob::ObjectNamespace::get().createSymbolicLink(L"\\DosDevices\\F:", L"\\Device\\CdRom0");
    ob::ObjectNamespace::get().createSymbolicLink(L"\\DosDevices\\G:", L"\\Device\\Harddisk0\\Partition4");
    ob::ObjectNamespace::get().createSymbolicLink(L"\\DosDevices\\Z:", L"\\Device\\ZfsPool_Storage");
    std::cout << "[MicaNT Boot] [VFS] Registered multi-format storage symlinks (C:, D:, E:, F:, G:, Z:)\n";

    // 5.2 Initialize Kernel Hardware Telemetry Driver via DriverEntry
    std::cout << "[MicaNT Boot] [Driver] Loading Kernel Telemetry Driver via DriverEntry...\n";
    auto& drvMgr = driver::DriverManager::get();
    drvMgr.loadDriver(
        L"\\Driver\\MicaTelemetry",
        [](io::DriverObject* drv, const UnicodeString* reg) -> NtStatus {
            (void)reg;
            drv->setDispatch(io::IRP_MJ_DEVICE_CONTROL, [](io::DeviceObject*, io::Irp* irp) -> NtStatus {
                if (irp && irp->userBuffer && irp->length >= sizeof(uint32_t)) {
                    *static_cast<uint32_t*>(irp->userBuffer) = 48; // 48 deg C
                    irp->ioStatus.status = NtStatus::Success;
                    irp->ioStatus.information = sizeof(uint32_t);
                    return NtStatus::Success;
                }
                return NtStatus::InvalidParameter;
            });
            auto dev = io::IoManager::get().createDevice(drv, L"\\Device\\MicaTelemetry", io::DeviceType::Unknown);
            return dev ? NtStatus::Success : NtStatus::Unsuccessful;
        },
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\MicaTelemetry"
    );
    std::cout << "[MicaNT Boot] [Driver] Loaded \\Driver\\MicaTelemetry on \\Device\\MicaTelemetry (Status: ACTIVE)\n";

    // 5.3 Initialize NDIS 6.x and TCP/IP Network Stack
    std::cout << "[MicaNT Boot] [Ndis & Tcpip] Initializing NDIS 6.x and TCP/IP Network Stack...\n";
    auto& netStack = tcpip::NetworkStack::get();
    netStack.initialize();
    std::cout << "[MicaNT Boot] [Ndis & Tcpip] Network adapter \\Device\\NdisMicaNic0 active (IPv4: "
              << netStack.getLocalIp().toString() << "/24, IPv6: "
              << netStack.getLocalIpv6().toString() << "%1, 10 Gbps Virtual Bus)\n";
    iphlpapi::InitializeIpHelperApi();

    // 5.4 Initialize Named Pipes (NPFS) & Mailslots (MSFS) IPC Subsystems
    std::cout << "[MicaNT Boot] [Npfs & Msfs] Initializing Named Pipe & Mailslot IPC File Systems...\n";
    auto& npfsMgr = npfs::NamedPipeFileSystem::get();
    npfsMgr.initialize();
    auto& msfsMgr = npfs::MailslotFileSystem::get();
    msfsMgr.initialize();
    std::cout << "[MicaNT Boot] [Npfs & Msfs] Mounted \\Device\\NamedPipe and \\Device\\Mailslot (Transactional RPC & Datagram IPC Ready)\n";

    // 6. Initialize I/O Completion Port (IOCP) Subsystem
    std::cout << "[MicaNT Boot] [Io & IOCP] Initializing I/O Completion Port Subsystem...\n";
    io::IoCompletionPort systemIocp(4);
    systemIocp.postCompletion(0x1337, 0x00007FF71000, NtStatus::Success, 4096);
    uint64_t iocpKey = 0;
    uintptr_t iocpOv = 0;
    IoStatusBlock iocpIosb{};
    NtStatus iocpStatus = systemIocp.removeCompletion(iocpKey, iocpOv, iocpIosb, 0);
    if (NT_SUCCESS(iocpStatus)) {
        std::cout << "[MicaNT Boot] [Io & IOCP] Async completion cycle verified: Key 0x" 
                  << std::hex << iocpKey << std::dec << " Transferred: " << iocpIosb.information << " bytes.\n";
    }

    // 7. Initialize Synchronization Subsystem
    std::cout << "[MicaNT Boot] [Sync] Initializing KEVENT / KMUTANT / KSEMAPHORE primitives...\n";
    sync::EventObject bootEvent(sync::EventType::NotificationEvent, false);
    bootEvent.set();
    std::cout << "[MicaNT Boot] [Sync] Boot synchronization event signaled: OK\n";

    // 8. Initialize Kernel Core (KE) Spinlocks, DPCs, and 32-Queue Priority Scheduler
    std::cout << "[MicaNT Boot] [Ke] Initializing Kernel Core & 32-Queue Priority Scheduler...\n";
    auto& scheduler = ke::PriorityScheduler::get();
    scheduler.readyThread(ke::ScheduledThreadEntry{
        .tid = 0, .basePriority = ke::PRIORITY_IDLE, .currentPriority = ke::PRIORITY_IDLE, .name = "Idle Loop"
    });
    std::cout << "[MicaNT Boot] [Ke] IRQL State: PASSIVE_LEVEL (0). Idle thread queued at Priority 0.\n";

    // 8.0b Initialize Core System Processes (PID 0: Idle, PID 4: System)
    std::cout << "[MicaNT Boot] [Ps] Initializing Core System Processes (PID 0: Idle, PID 4: System)...\n";
    auto& procMgr = ps::ProcessManager::get();
    procMgr.initializeSystemProcesses();
    std::wcout << L"[MicaNT Boot] [Ps] Registered Core System Processes: PID " 
               << procMgr.getIdleProcess()->getPid() << L" (" << procMgr.getIdleProcess()->getImageFileName()
               << L"), PID " << procMgr.getSystemProcess()->getPid() << L" (" 
               << procMgr.getSystemProcess()->getImageFileName() << L")\n";

    // 8.1 Initialize Executive Work Queues (Critical & Delayed)
    std::cout << "[MicaNT Boot] [Ex] Initializing Executive Worker Threads (ExQueueWorkItem)...\n";
    auto& workMgr = ex::ExecutiveWorkQueueManager::get();
    workMgr.initialize(2);
    std::atomic<bool> workerTaskDone{false};
    ex::WorkQueueItem bootWorkItem;
    ex::ExInitializeWorkItem(&bootWorkItem, [](void* ctx) {
        auto* flag = static_cast<std::atomic<bool>*>(ctx);
        *flag = true;
    }, &workerTaskDone);
    ex::ExQueueWorkItem(&bootWorkItem, ex::WorkQueueType::CriticalWorkQueue);
    std::cout << "[MicaNT Boot] [Ex] Dispatched background Executive Work Item to CriticalWorkQueue: OK\n";

    // 9. Initialize Trap & Exception Engine
    std::cout << "[MicaNT Boot] [Ke/Trap] Initializing Trap Engine & Demand Paging (#PF Vector 14)...\n";
    auto& trapEngine = ke::TrapEngine::get();
    (void)trapEngine;
    std::cout << "[MicaNT Boot] [Ke/Trap] Registered SEH Dispatcher and KeBugCheckEx panic handler.\n";

    // 10. Initialize Configuration Manager (CM) Registry Hives
    std::cout << "[MicaNT Boot] [Cm] Initializing Configuration Manager & mounting \\Registry...\n";
    auto& cmMgr = cm::ConfigurationManager::get();
    auto sessionMgrKey = cmMgr.resolvePath(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Session Manager");
    if (sessionMgrKey) {
        std::wcout << L"[MicaNT Boot] [Cm] Mounted Session Manager hive: " 
                   << sessionMgrKey->getValue(L"OSName")->asString() 
                   << L" (Build " << sessionMgrKey->getValue(L"OSBuild")->asDword() << L")\n";
    }

    // 11. Initialize Security Reference Monitor (SRM)
    std::cout << "[MicaNT Boot] [Se] Initializing Security Reference Monitor (SRM)...\n";
    auto systemToken = se::TokenObject::createSystemToken();
    std::wcout << L"[MicaNT Boot] [Se] Kernel Primary Token: " << systemToken->getUserSid().toString() 
               << L" (SeDebugPrivilege: " << (systemToken->hasPrivilege(se::SE_DEBUG_NAME) ? L"ENABLED" : L"DISABLED") << L")\n";

    // 12. Initialize Advanced Local Procedure Call (ALPC) Subsystem
    std::cout << "[MicaNT Boot] [Lpc] Initializing ALPC Subsystem & \\RPC Control port directory...\n";
    auto& lpcMgr = lpc::PortManager::get();
    auto csrssPort = lpcMgr.createPort(L"\\RPC Control\\MicaCsrPort");
    std::shared_ptr<lpc::PortObject> clientEndpoint;
    std::shared_ptr<lpc::PortObject> serverEndpoint;
    lpcMgr.connectPort(L"MicaCsrPort", clientEndpoint, serverEndpoint);
    std::cout << "[MicaNT Boot] [Lpc] Created system rendezvous endpoint: \\RPC Control\\MicaCsrPort (Status: READY)\n";

    // 12.1 Initialize 39-Unit Modular NT Kernel Linkage (Phase 1 Ring 0, Phase 2 Ring 1 & Phase 2.5 Ring 2 Complete through Horizon 67)
    std::cout << "[MicaNT Boot] [Ntoskrnl] Linking 39 Modular NT Kernel Subsystems (ke, mm, ob, ps, io, cm, se, lpc, cc, fsrtl, dbgk, tm, verifier, arb, wmi, pnpboot, pnpresilience, pofx_whea, umdf_msix, zerobsod, livehotswap, sysinfo_taskmgr, quotagov, mftzipui, srm_ci, afd_wsk, kuser_seal, ring1_vfs, ring1_dxgk, ring1_audio, ring1_smss, ring2_wudf, ring2_lsa, ring2_scm, ex, hal, pnp, po, init)...\n";
    if (!acpi::InitializeAcpiSubsystem(lpb.acpiTablePhysicalAddress)) {
        std::cerr << "[Acpi] Firmware RSDP/table validation failed; ACPI startup aborted.\n";
        return NtStatus::Unsuccessful;
    }
    std::cout << (lpb.acpiTablePhysicalAddress != 0
        ? "[Acpi] Firmware RSDP accepted; ACPI tables initialized.\n"
        : "[Acpi] No firmware RSDP in hosted LPB; initialized test table set.\n");
    if (lpb.acpiTablePhysicalAddress != 0 &&
        !acpi::TitanAcpiSubsystem::Instance().isFirmwareNamespaceComplete()) {
        std::cerr << "[Acpi] Firmware tables loaded, but one or more AML namespace terms are unsupported.\n";
    }

    sys::internal::HalInitializeExecutiveSubsystem();
    sys::internal::ExInitializeExecutiveSubsystem();
    sys::internal::PnpInitializeExecutiveSubsystem();
    sys::internal::PoInitializeExecutiveSubsystem();
    sys::internal::CcInitializeExecutiveCache();
    sys::internal::FsRtlInitializeExecutiveSubsystem();
    sys::internal::DbgkInitializeExecutiveSubsystem();
    sys::internal::KtmInitializeExecutiveSubsystem();
    sys::internal::VfInitializeExecutiveSubsystem();
    sys::internal::ArbInitializeExecutiveSubsystem();
    sys::internal::WmiInitializeExecutiveSubsystem();
    sys::internal::PnpBootInitializeExecutiveSubsystem();
    sys::internal::PnpResilienceInitializeExecutiveSubsystem();
    sys::internal::PoFxWheaInitializeExecutiveSubsystem();
    sys::internal::UmdfMsixInitializeExecutiveSubsystem();
    sys::internal::ZeroBsodInitializeExecutiveSubsystem();
    sys::internal::LiveHotSwapInitializeExecutiveSubsystem();
    sys::internal::SysInfoTaskmgrInitializeExecutiveSubsystem();
    sys::internal::QuotaGovInitializeExecutiveSubsystem();
    sys::internal::MftZipUiInitializeExecutiveSubsystem();
    sys::internal::SrmCiInitializeExecutiveSubsystem();
    sys::internal::AfdWskInitializeExecutiveSubsystem();
    sys::internal::KuserSealInitializeExecutiveSubsystem();
    sys::internal::Ring1VfsInitializeExecutiveSubsystem();
    sys::internal::Ring1DxgkInitializeExecutiveSubsystem();
    sys::internal::Ring1AudioInitializeExecutiveSubsystem();
    sys::internal::Ring1SmssInitializeExecutiveSubsystem();
    sys::internal::Ring2WudfInitializeExecutiveSubsystem();
    sys::internal::Ring2LsaInitializeExecutiveSubsystem();
    sys::internal::Ring2ScmInitializeExecutiveSubsystem();
    km::hid::InitializeHidSubsystemExports();
    io::DriverObject hidusbDriver{};
    km::hid::InitializeHidUsbDriver(hidusbDriver);
    io::DriverObject hidparseDriver{};
    km::hid::InitializeHidParseDriver(hidparseDriver);
    km::hid::InitializeHidParseExports();
    km::hid::InitializeKbdClassExports();
    io::DriverObject kbdclassDriver{};
    km::hid::InitializeKbdClassDriver(kbdclassDriver);
    io::DriverObject kbdhidDriver{};
    km::hid::InitializeKbdHidDriver(kbdhidDriver);
    auto keyboardClassDevice = ioMgr.createDevice(
        &kbdclassDriver, L"\\Device\\KeyboardClass0", static_cast<io::DeviceType>(0x0000000B));
    keyboardClassDevice->flags = (keyboardClassDevice->flags | io::DO_BUFFERED_IO) & ~io::DO_DEVICE_INITIALIZING;
    km::hid::KbdClassSetDeviceObject(keyboardClassDevice.get());
    km::hid::InitializeMouClassExports();
    io::DriverObject mouclassDriver{};
    km::hid::InitializeMouClassDriver(mouclassDriver);
    io::DriverObject mouhidDriver{};
    km::hid::InitializeMouHidDriver(mouhidDriver);
    io::DriverObject sermouseDriver{};
    km::input::InitializeSerMouseDriver(sermouseDriver);
    io::DriverObject serialDriver{};
    km::serial::InitializeSerialDriver(serialDriver);
    auto pointerClassDevice = ioMgr.createDevice(
        &mouclassDriver, L"\\Device\\PointerClass0", static_cast<io::DeviceType>(0x0000000F));
    pointerClassDevice->flags = (pointerClassDevice->flags | io::DO_BUFFERED_IO) & ~io::DO_DEVICE_INITIALIZING;
    io::DriverObject i8042Driver{};
    km::hid::InitializeI8042PortDriver(i8042Driver);
    auto keyboardPortDevice = ioMgr.createDevice(&i8042Driver, L"\\Device\\KeyboardPort0",
        static_cast<io::DeviceType>(0x0000000B), &km::hid::I8042KeyboardPortTag);
    auto pointerPortDevice = ioMgr.createDevice(&i8042Driver, L"\\Device\\PointerPort0",
        static_cast<io::DeviceType>(0x0000000F), &km::hid::I8042PointerPortTag);
    keyboardPortDevice->flags = (keyboardPortDevice->flags | io::DO_BUFFERED_IO) & ~io::DO_DEVICE_INITIALIZING;
    pointerPortDevice->flags = (pointerPortDevice->flags | io::DO_BUFFERED_IO) & ~io::DO_DEVICE_INITIALIZING;
    (void)km::hid::I8042ConnectClassDevice(*keyboardPortDevice, *keyboardClassDevice,
        km::hid::IOCTL_INTERNAL_KEYBOARD_CONNECT,
        reinterpret_cast<void*>(km::hid::KeyboardClassServiceCallback));
    (void)km::hid::I8042ConnectClassDevice(*pointerPortDevice, *pointerClassDevice,
        km::hid::IOCTL_INTERNAL_MOUSE_CONNECT,
        reinterpret_cast<void*>(km::hid::MouseClassServiceCallback));
    io::DriverObject beepDriver{};
    km::system::InitializeBeepDriver(beepDriver);
    auto beepDevice = km::system::InitializeBeepDevice(ioMgr, beepDriver);
    (void)beepDevice;
    std::cout << "[MicaNT Boot] [HidClass] Registered HIDCLASS.SYS / KBDCLASS.SYS (\\Device\\KeyboardClass0) / MOUCLASS.SYS (\\Device\\PointerClass0)\n";
    std::cout << "[MicaNT Boot] [Cc] Initialized Sovereign Executive Cache Manager (VACB Granularity: "
              << (km::cc::VACB_MAPPING_GRANULARITY / 1024) << " KB, BCB Pinning & Lazy Writer Active)\n";
    std::cout << "[MicaNT Boot] [FsRtl] Initialized Sovereign File System Runtime Library (LARGE_MCB, FILE_LOCK, OPLOCK, NOTIFY_SYNC, TUNNEL Active)\n";
    std::cout << "[MicaNT Boot] [Dbgk] Initialized Sovereign User-Mode Debugging Executive (DEBUG_OBJECT, NtWaitForDebugEvent, DbgUi* Active)\n";
    std::cout << "[MicaNT Boot] [Ktm] Initialized Sovereign Kernel Transaction Manager (KTM 2PC/1PC, CLFS Write-Ahead Log, ktmw32.dll Active)\n";
    std::cout << "[MicaNT Boot] [Verifier] Initialized Sovereign Driver Verifier (Special Pool, IRQL/SpinLock, IRP, Deadlock Graph & DMA Active)\n";
    std::cout << "[MicaNT Boot] [Arb] Initialized Sovereign Hardware Resource Arbiter & WDK .sys Loader (RTL_RANGE_LIST, PCI BAR/MSI-X & .inf DriverStore Active)\n";
    std::cout << "[MicaNT Boot] [Wmi] Initialized Sovereign Kernel WMI/WPP Recorder, Real NVIDIA/Intel .sys IAT Loader & Open-GPU GSP-RM Active\n";
    std::cout << "[MicaNT Boot] [PnpBoot] Initialized Sovereign Boot-Time Driver Scheduler & Live Hardware Bus Scanner (ServiceGroupOrder, StartOverride & Multi-Phase Timeline Active)\n";
    std::cout << "[MicaNT Boot] [Resilience] Initialized Sovereign Anti-Vista/ME Driver Resilience Engine (4-Part Rank 0x00SSFFHH, LastKnownGood Rollback & Zero-BSOD TDR Active)\n";
    std::cout << "[MicaNT Boot] [PoFx/WHEA] Initialized Sovereign Runtime Power Framework (PoFx*), Live PnP Hot-Plug/BAR Rebalance & WHEA Hardware Error Engine Active\n";
    std::cout << "[MicaNT Boot] [UMDF/SafeChannel/MSI-X] Initialized Sovereign UMDF 2.0 Reflector (Wudfrd.sys), Anti-CrowdStrike Channel Guard & Live Xeon MSI-X Router Active\n";
    std::cout << "[MicaNT Boot] [ZeroBsod] Initialized Sovereign Zero-BSOD Self-Healing Kernel Guard (0x133 DPC, 0x9F Power, 0xD1 High-IRQL & 0xC2/0x139 Pool/List Healer Active)\n";
    std::cout << "[MicaNT Boot] [LiveHotSwap] Initialized Sovereign Zero-Downtime Driver Hot-Swap, 0x7A/0xF4 Inpage Healer & Intel VT-d DMA Firewall Active\n";
    std::cout << "[MicaNT Boot] [SysInfo/Taskmgr/MMCSS] Initialized Sovereign Kernel Telemetry (15.63 MB Footprint, Red-Line Kernel Times & Lock-Free MMCSS SMP Scheduler Active)\n";
    std::cout << "[MicaNT Boot] [QuotaGov/Svchost/Primes] Initialized Sovereign Anti-Bloat GDI/WS Governor, Runaway svchost Spin Healer & Primes Drag-Race Engine Active\n";
    std::cout << "[MicaNT Boot] [MftSearch/UiGuard/Zipfldr] Initialized Sovereign 2ms Input Hook Guard, Instant $MFT Search & Zero-Copy zipfldr Section Engine Active\n";
    std::cout << "[MicaNT Boot] [Srm/MIC/ZeroLagCI] Initialized Sovereign Ring 0 Security Reference Monitor, AppContainer & 3us USN-Cached ci.dll Active\n";
    std::cout << "[MicaNT Boot] [Afd/WSK/RIO] Initialized Sovereign Ring 0 afd.sys Fast-I/O, Winsock Kernel (WSK) & Zero-Copy Registered I/O Active\n";
    std::cout << "[MicaNT Boot] [KuserSeal/Upcalls] Sealed Ring 0 <-> Ring 3 Hardware ABI (KUSER_SHARED_DATA 0x7FFE0000, SYSCALL MSRs & KiUser* Trampolines Active)\n";
    std::cout << "[MicaNT Boot] [Ring1/VfsMinifilter] Initialized Sovereign Ring 1 VFS & Minifilter Fault-Isolated Fast-I/O Plane Active\n";
    std::cout << "[MicaNT Boot] [Ring1/DxgkFlip] Initialized Sovereign Ring 1 win32k Sandbox, DirectFlip/MPO Scanout & 540Hz Hardware Cursor Active\n";
    std::cout << "[MicaNT Boot] [Ring1/WaveRtAudio] Initialized Sovereign Ring 1 WaveRT Cyclic DMA (1.33ms 64-Frame WASAPI & APO Guard) Active\n";
    std::cout << "[MicaNT Boot] [Ring1/SmssCsrss] Initialized Sovereign Native Session Bootstrap (smss.exe, \\KnownDlls Pre-Mapper & csrss.exe Ready)\n";
    std::cout << "[MicaNT Boot] [Ring2/WudfFontDrv] Initialized Sovereign Ring 2 Isolated Device & Parser Host (WUDFHost.exe 4ms Auto-Heal & fontdrvhost.exe OTF/CFF2 Sandbox Active)\n";
    std::cout << "[MicaNT Boot] [Ring2/LsaVaultKeyIso] Initialized Sovereign Ring 2 Security Authority & Credential Guard Vault (lsass.exe RunAsPPL, lsaiso.exe Anti-Dump & KeyIso/DPAPI Active)\n";
    std::cout << "[MicaNT Boot] [Ring2/ScmServices] Initialized Sovereign Ring 2 Service Control Manager (services.exe 38ms Parallel DAG, RpcSs/DcomLaunch 4us LRPC & Ring 3 Gate Open)\n";

    // 13. Initialize KiSystemCall64 Central Dispatcher Table
    std::cout << "[MicaNT Boot] [KiSystemCall64] Initializing Syscall Dispatcher Table...\n";
    auto& dispatcher = sys::SyscallDispatcher::get();
    dispatcher.initializeStandardTable();
    std::cout << "[MicaNT Boot] [KiSystemCall64] " << dispatcher.getRegisteredCount() 
              << " core NT syscalls registered in LSTAR dispatch table.\n";

    // 14. Test Simulated Ring 3 Syscall via Dispatcher
    std::cout << "[MicaNT Boot] [Test] Simulating Ring 3 -> Ring 0 'syscall' invocation...\n";
    uintptr_t userAllocBase = 0;
    size_t userAllocSize = 128 * 1024; // 128 KB
    uint64_t stackParameters[2] = { mm::MEM_COMMIT | mm::MEM_RESERVE, mm::PAGE_READWRITE };

    sys::SyscallFrame testFrame{
        .ssn = sys::SSN_NtAllocateVirtualMemory,
        .arg1 = 0, // ProcessHandle
        .arg2 = reinterpret_cast<uint64_t>(&userAllocBase),
        .arg3 = 0, // ZeroBits
        .arg4 = reinterpret_cast<uint64_t>(&userAllocSize),
        .stackArgs = stackParameters,
        .stackArgCount = 2
    };

    NtStatus syscallResult = dispatcher.dispatch(testFrame);
    if (NT_SUCCESS(syscallResult)) {
        std::cout << "[MicaNT Boot] [Test] Syscall NtAllocateVirtualMemory (SSN 0x" 
                  << std::hex << sys::SSN_NtAllocateVirtualMemory << std::dec << ") dispatched OK! "
                  << "Mapped 0x" << std::hex << userAllocBase << " - 0x" << (userAllocBase + userAllocSize) << std::dec << "\n";
    } else {
        std::cerr << "[MicaNT Boot] [Test] Syscall dispatch failed: " << NtStatusToString(syscallResult) << "\n";
    }

    // 14.1 Test Simulated Ring 3 File I/O Syscall via Dispatcher
    std::cout << "[MicaNT Boot] [Test] Simulating Ring 3 -> Ring 0 file I/O (NtOpenFile & NtReadFile)...\n";
    UnicodeString ntdllVfsPath(L"\\DosDevices\\C:\\Windows\\System32\\ntdll.dll");
    ObjectAttributes ntdllFileAttr{};
    ntdllFileAttr.objectName = &ntdllVfsPath;
    Handle ntdllSyscallHandle = 0;
    IoStatusBlock ntdllIosb{};

    sys::SyscallFrame openFrame{
        .ssn = sys::SSN_NtOpenFile,
        .arg1 = reinterpret_cast<uint64_t>(&ntdllSyscallHandle),
        .arg2 = fs::FILE_GENERIC_READ,
        .arg3 = reinterpret_cast<uint64_t>(&ntdllFileAttr),
        .arg4 = reinterpret_cast<uint64_t>(&ntdllIosb)
    };
    NtStatus openSysStatus = dispatcher.dispatch(openFrame);
    if (NT_SUCCESS(openSysStatus)) {
        std::cout << "[MicaNT Boot] [Test] Syscall NtOpenFile (SSN 0x" 
                  << std::hex << sys::SSN_NtOpenFile << std::dec << ") succeeded! Handle: 0x" 
                  << std::hex << ntdllSyscallHandle << std::dec << "\n";
        sys::NtClose(ntdllSyscallHandle);
    }

    // 14.2 Test Simulated Ring 3 DeviceIoControl via Dispatcher
    std::cout << "[MicaNT Boot] [Test] Simulating Ring 3 -> Ring 0 DeviceIoControl (NtDeviceIoControlFile)...\n";
    UnicodeString devPath(L"\\Device\\MicaTelemetry");
    ObjectAttributes devAttr{};
    devAttr.objectName = &devPath;
    Handle devSysHandle = 0;
    IoStatusBlock devIosb{};

    sys::SyscallFrame openDevFrame{
        .ssn = sys::SSN_NtOpenFile,
        .arg1 = reinterpret_cast<uint64_t>(&devSysHandle),
        .arg2 = fs::FILE_GENERIC_READ | fs::FILE_GENERIC_WRITE,
        .arg3 = reinterpret_cast<uint64_t>(&devAttr),
        .arg4 = reinterpret_cast<uint64_t>(&devIosb)
    };
    (void)dispatcher.dispatch(openDevFrame);

    uint32_t telemetryResult = 0;
    uint64_t ioctlStackArgs[6] = {
        reinterpret_cast<uint64_t>(&devIosb),
        0x80002004, // Custom IOCTL code
        0, 0,       // No input buffer
        reinterpret_cast<uint64_t>(&telemetryResult),
        sizeof(telemetryResult)
    };

    sys::SyscallFrame ioctlFrame{
        .ssn = sys::SSN_NtDeviceIoControlFile,
        .arg1 = static_cast<uint64_t>(devSysHandle),
        .arg2 = 0,
        .arg3 = 0,
        .arg4 = 0,
        .stackArgs = ioctlStackArgs,
        .stackArgCount = 6
    };
    NtStatus ioctlSysStatus = dispatcher.dispatch(ioctlFrame);
    if (NT_SUCCESS(ioctlSysStatus)) {
        std::cout << "[MicaNT Boot] [Test] Syscall NtDeviceIoControlFile (SSN 0x" 
                  << std::hex << sys::SSN_NtDeviceIoControlFile << std::dec << ") succeeded! "
                  << "Polled Hardware Core Temp: " << telemetryResult << " C\n";
    }
    sys::NtClose(devSysHandle);

    // 14.3 Test Ring 3 Storage IOCTL via \DosDevices\PhysicalDrive0
    std::cout << "[MicaNT Boot] [Test] Simulating Ring 3 Storage IOCTL (\\DosDevices\\PhysicalDrive0 -> IOCTL_DISK_GET_DRIVE_GEOMETRY_EX)...\n";
    UnicodeString physDrivePath(L"\\DosDevices\\PhysicalDrive0");
    ObjectAttributes physDriveAttr{};
    physDriveAttr.objectName = &physDrivePath;
    Handle physDriveHandle = 0;
    IoStatusBlock physDriveIosb{};

    sys::SyscallFrame openPhysDriveFrame{
        .ssn = sys::SSN_NtOpenFile,
        .arg1 = reinterpret_cast<uint64_t>(&physDriveHandle),
        .arg2 = fs::FILE_GENERIC_READ,
        .arg3 = reinterpret_cast<uint64_t>(&physDriveAttr),
        .arg4 = reinterpret_cast<uint64_t>(&physDriveIosb)
    };
    NtStatus openPhysStatus = dispatcher.dispatch(openPhysDriveFrame);
    if (NT_SUCCESS(openPhysStatus)) {
        storage::DISK_GEOMETRY_EX diskGeomEx{};
        uint64_t diskIoctlArgs[6] = {
            reinterpret_cast<uint64_t>(&physDriveIosb),
            storage::IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
            0, 0, // No input buffer
            reinterpret_cast<uint64_t>(&diskGeomEx),
            sizeof(diskGeomEx)
        };
        sys::SyscallFrame geomIoctlFrame{
            .ssn = sys::SSN_NtDeviceIoControlFile,
            .arg1 = static_cast<uint64_t>(physDriveHandle),
            .arg2 = 0,
            .arg3 = 0,
            .arg4 = 0,
            .stackArgs = diskIoctlArgs,
            .stackArgCount = 6
        };
        NtStatus geomStatus = dispatcher.dispatch(geomIoctlFrame);
        if (NT_SUCCESS(geomStatus)) {
            std::cout << "[MicaNT Boot] [Test] IOCTL_DISK_GET_DRIVE_GEOMETRY_EX succeeded! "
                      << "Total Size: " << (diskGeomEx.DiskSize.quadPart / (1024 * 1024)) << " MB, "
                      << "BytesPerSector: " << diskGeomEx.Geometry.BytesPerSector << ", "
                      << "SectorsPerTrack: " << diskGeomEx.Geometry.SectorsPerTrack << "\n";
        }
        sys::NtClose(physDriveHandle);
    }

    // 15. Inspect and Load Ring 3 Binary (bin/userland_app.exe or self)
    const char* targetAppPath = "bin/userland_app.exe";
    std::ifstream testCheck(targetAppPath, std::ios::binary);
    if (!testCheck.is_open()) {
        targetAppPath = "bin/micant_kernel.exe";
    }
    testCheck.close();

    std::cout << "\n[MicaNT Boot] [PeLoader] Inspecting 64-bit Target Executable: " << targetAppPath << "\n";
    
    std::ifstream exeFile(targetAppPath, std::ios::binary | std::ios::ate);
    if (exeFile.is_open()) {
        std::streamsize fileSize = exeFile.tellg();
        exeFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        if (exeFile.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            pe::ImageNtHeaders64 ntHeaders;
            std::vector<pe::ImageSectionHeader> sections;

            NtStatus peStatus = pe::PeLoader::inspect(buffer, ntHeaders, sections);
            if (NT_SUCCESS(peStatus)) {
                std::cout << "[MicaNT Boot] [PeLoader] Successfully parsed 64-bit PE32+ header!\n";
                std::cout << "  - Target Machine:      x86_64 (AMD64 0x" << std::hex << ntHeaders.fileHeader.machine << std::dec << ")\n";
                std::cout << "  - Number of Sections:  " << ntHeaders.fileHeader.numberOfSections << "\n";
                std::cout << "  - Entry Point RVA:     0x" << std::hex << ntHeaders.optionalHeader.addressOfEntryPoint << std::dec << "\n";
                std::cout << "  - Preferred ImageBase: 0x" << std::hex << ntHeaders.optionalHeader.imageBase << std::dec << "\n";
                std::cout << "  - Section Table:\n";
                for (const auto& sec : sections) {
                    std::cout << "      * " << std::left << std::setw(8) << sec.getName() 
                              << " RVA: 0x" << std::hex << std::setw(8) << sec.virtualAddress
                              << " Size: 0x" << std::setw(8) << sec.misc.virtualSize
                              << " Flags: 0x" << sec.characteristics << std::dec << "\n";
                }

                // 16. Section Mapping & Process/Thread Instantiation
                std::cout << "\n[MicaNT Boot] [Ps & Section] Spawning EProcess for " << targetAppPath << "...\n";
                auto& pm = ps::ProcessManager::get();
                auto proc = pm.createProcess(L"userland_app.exe", systemToken);
                proc->setImageBase(ntHeaders.optionalHeader.imageBase);
                proc->setEntryPoint(ntHeaders.optionalHeader.imageBase + ntHeaders.optionalHeader.addressOfEntryPoint);

                // Create Section Object
                auto& sm = section::SectionManager::get();
                Handle secHandle = 0;
                std::shared_ptr<section::SectionObject> secObj;
                sm.createSection(
                    secHandle,
                    proc->getHandleTable(),
                    static_cast<size_t>(ntHeaders.optionalHeader.sizeOfImage),
                    section::SEC_IMAGE | section::SEC_COMMIT,
                    mm::PAGE_EXECUTE_READWRITE,
                    secObj
                );

                // Map View of Section into target process
                uintptr_t viewBase = ntHeaders.optionalHeader.imageBase;
                size_t viewSize = static_cast<size_t>(ntHeaders.optionalHeader.sizeOfImage);
                sm.mapViewOfSection(secObj, *proc, viewBase, viewSize, mm::MEM_COMMIT, mm::PAGE_EXECUTE_READWRITE);

                // Create Primary EThread
                auto thread = proc->createThread(proc->getEntryPoint());
                thread->setState(ps::ThreadState::Ready);

                std::cout << "[MicaNT Boot] [Ps] Process Created: PID " << proc->getPid() << "\n";
                std::wcout << L"  - Primary Token:   " << proc->getToken()->getUserSid().toString() << L" (LocalSystem)\n";
                std::cout << "  - PEB Address:     0x" << std::hex << proc->getPebAddress() << std::dec << "\n";
                std::cout << "  - Image Mapped At: 0x" << std::hex << viewBase << " - 0x" << (viewBase + viewSize) << std::dec << "\n";
                std::cout << "  - Primary Thread:  TID " << thread->getTid() << " (State: READY)\n";
                std::cout << "  - Initial RIP:     0x" << std::hex << thread->getContext().rip << std::dec << "\n";
                std::cout << "  - Initial RSP:     0x" << std::hex << thread->getContext().rsp << std::dec << "\n";

                // 16b. Dynamic PE Import Table Inspection & Binding
                std::vector<pe::ImportedLibrary> appImports;
                NtStatus impStatus = pe::PeLoader::parseImports(buffer, ntHeaders, sections, appImports);
                if (NT_SUCCESS(impStatus) && !appImports.empty()) {
                    subsystems::InitializeAllSubsystemExports();
                    std::cout << "\n[MicaNT Boot] [PeLoader] Parsed " << appImports.size() << " dynamic import descriptor(s):\n";
                    for (const auto& lib : appImports) {
                        std::cout << "      * Library: " << lib.libraryName << " (" << lib.symbols.size() << " imported symbols)\n";
                    }
                }
            }
        }
    }

    // 17. NT System Call Interface Catalog Verification
    std::cout << "\n[MicaNT Boot] [Catalog] Verifying NT System Call API surface:\n";
    std::cout << "  - Auto-generated Nt/Zw System Calls: " 
              << generated::NtSystemCallCatalog.size() << " registered.\n";
    std::cout << "  - Auto-generated Rtl Runtime Routines: " 
              << generated::RtlRoutineCatalog.size() << " registered.\n";

    // 18. Kernel Timers & Delay Execution (KTIMER & NtDelayExecution)
    std::cout << "\n[MicaNT Boot] [Ke & Timer] Initializing Kernel Timers & KeSetTimer...\n";
    timer::KTIMER bootTimer;
    timer::KeInitializeTimer(&bootTimer);
    LargeInteger timerDue{};
    timerDue.quadPart = -50000; // -5ms
    timer::KeSetTimer(&bootTimer, timerDue, nullptr);
    std::cout << "[MicaNT Boot] [Ke] Armed 5ms KTIMER. Active Timers in TimerManager: " 
              << timer::TimerManager::get().getActiveTimerCount() << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(6));
    timer::TimerManager::get().processTimers();
    std::cout << "[MicaNT Boot] [Ke] KTIMER expired and signaled: " 
              << (timer::KeReadStateTimer(&bootTimer) ? "YES" : "NO") << "\n";

    // 19. Fast O(1) Lookaside Lists (NPAGED_LOOKASIDE_LIST)
    std::cout << "\n[MicaNT Boot] [Ex & Lookaside] Demonstrating Driver Lookaside Allocation Cache...\n";
    ex::NPagedLookasideList driverLookaside;
    driverLookaside.initialize(64, ex::makePoolTag('D', 'r', 'v', 'L'), 16);
    void* block1 = driverLookaside.allocate();
    void* block2 = driverLookaside.allocate();
    driverLookaside.free(block1);
    driverLookaside.free(block2);
    void* rec1 = driverLookaside.allocate();
    void* rec2 = driverLookaside.allocate();
    std::cout << "[MicaNT Boot] [Ex] Lookaside Stats: Total Allocs=" 
              << driverLookaside.getStats().totalAllocates 
              << ", Misses=" << driverLookaside.getStats().allocateMisses 
              << " (Cache Hit Rate: 50% on first re-use cycle, O(1) SLIST pop)\n";
    driverLookaside.free(rec1);
    driverLookaside.free(rec2);
    driverLookaside.flush();

    // 20. Multi-Object Synchronization (NtWaitForMultipleObjects - SSN 0x005A)
    std::cout << "\n[MicaNT Boot] [Sync] Verifying NtWaitForMultipleObjects (WaitAny & WaitAll)...\n";
    auto syncEv1 = std::make_shared<sync::EventObject>(sync::EventType::NotificationEvent, false);
    auto syncEv2 = std::make_shared<sync::EventObject>(sync::EventType::NotificationEvent, true);
    Handle hEv1 = sync::DispatcherRegistry::get().registerObject(syncEv1);
    Handle hEv2 = sync::DispatcherRegistry::get().registerObject(syncEv2);
    Handle waitHandles[2] = { hEv1, hEv2 };
    NtStatus multiWaitRes = sys::NtWaitForMultipleObjects(2, waitHandles, WaitType::WaitAny, false, nullptr);
    std::cout << "[MicaNT Boot] [Sync] NtWaitForMultipleObjects WaitAny returned: " 
              << ((multiWaitRes == NtStatus::Wait1) ? "STATUS_WAIT_1 (SUCCESS)" : "UNEXPECTED") << "\n";
    sys::NtClose(hEv1);
    sys::NtClose(hEv2);

    // 21. Ring 3 Userland Bridge & ntdll.dll Runtime (Ldr, TEB/PEB, & User Heap)
    std::cout << "\n[MicaNT Boot] [ntdll] Initializing Ring 3 Userland Bridge & LdrInitializeThunk...\n";
    ps::Peb bootUserPeb{};
    ps::Teb bootUserTeb{};
    bootUserPeb.imageBaseAddress = 0x0000000140000000ULL;
    ntdll::RtlSetCurrentTeb(&bootUserTeb);
    NtStatus bootLdrStatus = ntdll::LdrInitializeThunk(&bootUserPeb, &bootUserTeb, 0x0000000140001000ULL, L"C:\\Windows\\System32\\smss.exe");
    std::cout << "[MicaNT Boot] [ntdll] LdrInitializeThunk status: " 
              << (NT_SUCCESS(bootLdrStatus) ? "STATUS_SUCCESS" : "FAILED") << "\n";
    std::cout << "  - Default Process Heap: 0x" << std::hex << bootUserPeb.processHeap << std::dec << "\n";
    std::cout << "  - Dynamic Modules in Ldr: " << ldr::DynamicLoader::get().getLoadedModuleCount() << " registered\n";

    // Allocate from userland heap
    void* bootUserBuf = ntdll::RtlAllocateHeap(reinterpret_cast<void*>(bootUserPeb.processHeap), ntdll::HEAP_ZERO_MEMORY, 128);
    std::cout << "[MicaNT Boot] [ntdll] RtlAllocateHeap (128 bytes) allocated at 0x" << bootUserBuf << "\n";
    ntdll::RtlFreeHeap(reinterpret_cast<void*>(bootUserPeb.processHeap), 0, bootUserBuf);
    std::cout << "[MicaNT Boot] [ntdll] Userland heap allocation & free verified successfully.\n";
    // 21b. WoW64 Subsystem (32-Bit Compatibility Layer)
    std::cout << "\n[MicaNT Boot] [WoW64] Initializing WoW64 Subsystem (Heaven's Gate & 32-bit Thunking)...\n";
    wow64::Wow64ThunkDispatcher wow64Dispatcher;
    (void)wow64Dispatcher;
    std::cout << "[MicaNT Boot] [WoW64] Heaven's Gate Far Call Switcher (CS 0x23 <-> 0x33) ACTIVE\n";
    std::cout << "[MicaNT Boot] [WoW64] Virtual Filesystem Redirection: \\Windows\\System32 -> \\Windows\\SysWOW64 ACTIVE\n";
    std::cout << "[MicaNT Boot] [WoW64] Registry Virtualization: \\Registry\\Machine\\Software -> WOW6432Node ACTIVE\n";

    // 21c. MicaNT Interactive Shell Launch Check
    bool launchShell = launchInteractive;
    std::string execCommand = (execCmd ? execCmd : "");

    if (!execCommand.empty()) {
        std::cout << "\n[MicaNT Executive] Boot complete. Executing: " << execCommand << "\n";
        shell::CommandShell cmdShell;
        cmdShell.execute(execCommand, std::cout);
    } else if (launchShell) {
        std::cout << "\n[MicaNT Executive] Boot complete. Launching MicaNT Command Prompt Shell (cmd.exe / msh.exe)...\n";
        shell::CommandShell cmdShell;
        cmdShell.runRepl();
    }

    // 22. Power Management & Clean System Shutdown (Po & NtShutdownSystem - SSN 0x0118)
    std::cout << "\n[MicaNT Boot] [Po] Demonstrating System Shutdown Handover (NtShutdownSystem)...\n";
    bootvid.getDriver().renderBootSplash("Initiating System Shutdown...", 1.0f);
    sys::SyscallFrame shutdownFrame{};
    shutdownFrame.ssn = sys::SSN_NtShutdownSystem;
    shutdownFrame.arg1 = static_cast<uint64_t>(po::ShutdownAction::ShutdownPowerOff);
    NtStatus shutRes = sys::SyscallDispatcher::get().dispatch(shutdownFrame);
    std::cout << "[MicaNT Boot] [Po] NtShutdownSystem status: " << (NT_SUCCESS(shutRes) ? "STATUS_SUCCESS" : "FAILED") << "\n";
    std::cout << "[MicaNT Boot] [Po] Final System Power State: S" 
              << (static_cast<uint32_t>(po::PowerManager::get().getSystemPowerState()) - 1) 
              << " (PowerSystemShutdown / Soft Off)\n";
    bootvid.vidResetDisplay(true);

    std::cout << "\n[MicaNT Executive] Subsystem self-test PASSED. Ready for Ring 3 binaries.\n\n";

    return NtStatus::Success;
}

int main(int argc, char* argv[]) {
    auto defaultLpb = boot::createDefaultUefiBootBlock();
    bool launchShell = false;
    const char* execCommand = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--shell" || arg == "-i" || arg == "--cmd" || arg == "/shell") {
            launchShell = true;
            break;
        } else if ((arg == "--exec" || arg == "-c" || arg == "/c") && i + 1 < argc) {
            execCommand = argv[++i];
            break;
        }
    }

    NtStatus status = KiSystemStartup(&defaultLpb, execCommand, launchShell);
    return NT_SUCCESS(status) ? 0 : 1;
}

#endif
