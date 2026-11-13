#include "micant/km/core/syscalls.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/drivers/net/tcpip.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/system_time.hpp"

#include <array>
#include <memory>
#include <new>

extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;

namespace micant::netboot {
namespace {
void logStatus(const char* stage, NtStatus status) noexcept {
    if (status == NtStatus::Success) return;
    MicantNetbootSerialWrite("[MicaNT DHCP] ");
    MicantNetbootSerialWrite(stage);
    MicantNetbootSerialWrite(" status=");
    MicantNetbootSerialWriteNumber(static_cast<uint32_t>(status));
    MicantNetbootSerialWrite("\r\n");
}

struct NativeImage {
    std::unique_ptr<uint8_t[]> bytes;
    size_t size{};
    std::array<char16_t, 256> path{};
    size_t pathLength{};
};

struct ScopedHandle {
    Handle value{};
    ~ScopedHandle() {
        if (value && sys::NtClose(value) != NtStatus::Success) __builtin_trap();
    }
};

bool systemPath(const char16_t* leaf, std::array<char16_t, 256>& path,
                size_t& length) noexcept {
    char16_t directory[256]{};
    size_t directoryLength{};
    if (io::IoQuerySystemDirectory(directory, std::size(directory), directoryLength) != NtStatus::Success ||
        !directoryLength || directoryLength >= std::size(directory)) return false;
    size_t used{};
    for (; used < directoryLength && directory[used]; ++used) {
        if (used + 1 >= path.size()) return false;
        path[used] = directory[used];
    }
    if (!used) return false;
    if (path[used - 1] != u'\\') {
        if (used + 1 >= path.size()) return false;
        path[used++] = u'\\';
    }
    for (size_t i = 0; leaf[i]; ++i) {
        if (used + 1 >= path.size()) return false;
        path[used++] = leaf[i];
    }
    path[used] = 0;
    length = used;
    return true;
}

bool networkTestEnabled() noexcept {
    std::array<char16_t, 256> path{};
    size_t length{};
    if (!systemPath(u"m10-network-test.flag", path, length)) return false;
    ScopedHandle marker{};
    return io::IoOpenBackendFile(&marker.value, 1, {path.data(), length}) == NtStatus::Success;
}

void seedConfiguredNetworkTestUtc() noexcept {
    uint64_t current{};
    if (hal::amd64::queryBootstrapSystemTime(current) ==
        hal::amd64::BootstrapClockStatus::Success) return;
    // qemu.py configures -rtc base=utc for the M10 integration profile. Only
    // that explicit fixture marker permits interpreting its CMOS value as UTC.
    shared::time::SystemTimeFields rtc{};
    if (hal::amd64::readBootstrapRtc(rtc) != hal::amd64::BootstrapRtcStatus::Success) return;
    uint64_t utc100ns{};
    if (!shared::time::toSystemTime100ns(rtc, utc100ns)) return;
    (void)hal::amd64::initializeBootstrapSystemTime(utc100ns);
}

NtStatus readSystemImage(const char16_t* leaf, NativeImage& image) noexcept {
    if (!systemPath(leaf, image.path, image.pathLength)) return NtStatus::InvalidParameter;
    ScopedHandle file{};
    auto status = io::IoOpenBackendFile(&file.value, 1,
        {image.path.data(), image.pathLength});
    if (status != NtStatus::Success) return status;
    io::FileObject* object{};
    ob::ObjectHeader* reference{};
    status = io::IoReferenceFileHandle(file.value, 1, object, reference);
    if (status != NtStatus::Success) return status;
    const uint64_t fileSize = object->size();
    ob::ObDereferenceObject(reference);
    constexpr uint64_t MaximumImageSize = 16 * 1024 * 1024;
    if (!fileSize || fileSize > MaximumImageSize || fileSize > SIZE_MAX)
        return NtStatus::InvalidImageFormat;
    image.size = static_cast<size_t>(fileSize);
    image.bytes.reset(new (std::nothrow_t{}) uint8_t[image.size]);
    if (!image.bytes) return NtStatus::NoMemory;
    size_t bytesRead{};
    status = io::IoReadFileHandle(file.value, false, 0, image.bytes.get(), image.size, &bytesRead);
    if (status != NtStatus::Success) return status;
    return bytesRead == image.size ? NtStatus::Success : NtStatus::EndOfFile;
}
}

NtStatus acquireBootDhcpLease() noexcept {
    auto& network = tcpip::NetworkStack::get();
    if (!network.isInitialized() || !network.hasAdapter()) return NtStatus::DeviceNotReady;
    const bool networkTest = networkTestEnabled();
    if (networkTest) seedConfiguredNetworkTestUtc();

    NativeImage executable{}, ntdll{}, winsock{}, dnsapi{};
    auto status = readSystemImage(u"svchost.exe", executable);
    logStatus("svchost image", status);
    if (status == NtStatus::Success) status = readSystemImage(u"ntdll.dll", ntdll);
    logStatus("ntdll image", status);
    if (status == NtStatus::Success) status = readSystemImage(u"ws2_32.dll", winsock);
    logStatus("ws2_32 image", status);
    if (status == NtStatus::Success) status = readSystemImage(u"dnsapi.dll", dnsapi);
    logStatus("dnsapi image", status);
    if (status != NtStatus::Success) return status;

    const std::u16string_view executablePath(executable.path.data(), executable.pathLength);
    const std::u16string_view ntdllPath(ntdll.path.data(), ntdll.pathLength);
    const std::u16string_view winsockPath(winsock.path.data(), winsock.pathLength);
    const std::u16string_view dnsapiPath(dnsapi.path.data(), dnsapi.pathLength);
    const ps::detail::BootstrapProcessImage images[]{
        {"svchost.exe", {executable.bytes.get(), executable.size}, executablePath},
        {"ntdll.dll", {ntdll.bytes.get(), ntdll.size}, ntdllPath},
        {"ws2_32.dll", {winsock.bytes.get(), winsock.size}, winsockPath},
        {"dnsapi.dll", {dnsapi.bytes.get(), dnsapi.size}, dnsapiPath},
    };
    static constexpr char16_t normalCommandLine[] = u"svchost.exe -k netsvcs";
    static constexpr char16_t testCommandLine[] = u"svchost.exe -k netsvcs -m10-test";
    const auto commandLine = networkTest
        ? std::u16string_view(testCommandLine, std::size(testCommandLine) - 1)
        : std::u16string_view(normalCommandLine, std::size(normalCommandLine) - 1);
    ScopedHandle process{}, thread{};
    status = ps::detail::createBootstrapUserProcess(images, executablePath,
        commandLine, 32768, 32768, true,
        ob::SYNCHRONIZE | 0x0400u, ob::SYNCHRONIZE,
        process.value, thread.value, true);
    logStatus("service process creation", status);
    if (status != NtStatus::Success) return status;

    LargeInteger timeout{};
    timeout.quadPart = -250'000'000; // DHCP retry budget plus DNS query and service teardown.
    status = sys::NtWaitForSingleObject(process.value, false, &timeout);
    logStatus("service process wait", status);
    if (status != NtStatus::Success) {
        (void)sys::NtTerminateProcess(process.value, status);
        return status;
    }
    ps::EProcess* serviceProcess{};
    ob::ObjectHeader* serviceReference{};
    status = ps::detail::referenceProcessHandle(process.value, 0x0400u,
        serviceProcess, serviceReference);
    if (status != NtStatus::Success) return status;
    const auto serviceExitStatus = serviceProcess->exitStatus();
    ob::ObDereferenceObject(serviceReference);
    logStatus("service process exit", serviceExitStatus);
    if (serviceExitStatus != NtStatus::Success) return serviceExitStatus;

    tcpip::Ipv4Address ip{}, mask{}, gateway{}, dns{};
    ndis::MacAddress mac{};
    network.queryNetworkConfiguration(ip, mask, gateway, dns, mac);
    if (ip.isZero() || mask.isZero() || mac.isZero()) return NtStatus::Unsuccessful;

    if (networkTest) {
        NativeImage probe{};
        status = readSystemImage(u"network-probe.exe", probe);
        if (status != NtStatus::Success) return status;
        const std::u16string_view probePath(probe.path.data(), probe.pathLength);
        const ps::detail::BootstrapProcessImage probeImages[]{
            {"network-probe.exe", {probe.bytes.get(), probe.size}, probePath},
            {"ntdll.dll", {ntdll.bytes.get(), ntdll.size}, ntdllPath},
            {"ws2_32.dll", {winsock.bytes.get(), winsock.size}, winsockPath},
        };
        static constexpr char16_t probeCommandLine[] =
            u"network-probe.exe 10.0.2.2 18080";
        ScopedHandle probeProcess{}, probeThread{};
        status = ps::detail::createBootstrapUserProcess(probeImages, probePath,
            {probeCommandLine, std::size(probeCommandLine) - 1}, 32768, 32768, true,
            ob::SYNCHRONIZE, ob::SYNCHRONIZE, probeProcess.value, probeThread.value, true);
        if (status != NtStatus::Success) return status;
        LargeInteger probeTimeout{};
        probeTimeout.quadPart = -300'000'000; // 30 seconds for TCP connect/exchange and accept.
        status = sys::NtWaitForSingleObject(probeProcess.value, false, &probeTimeout);
        logStatus("network probe wait", status);
        if (status != NtStatus::Success) {
            (void)sys::NtTerminateProcess(probeProcess.value, status);
            return status;
        }

        NativeImage httpsProbe{};
        status = readSystemImage(u"https-probe.exe", httpsProbe);
        NativeImage crypt32{}, winhttp{};
        if (status == NtStatus::Success) status = readSystemImage(u"crypt32.dll", crypt32);
        if (status == NtStatus::Success) status = readSystemImage(u"winhttp.dll", winhttp);
        if (status != NtStatus::Success) return status;
        const std::u16string_view httpsPath(httpsProbe.path.data(), httpsProbe.pathLength);
        const std::u16string_view crypt32Path(crypt32.path.data(), crypt32.pathLength);
        const std::u16string_view winhttpPath(winhttp.path.data(), winhttp.pathLength);
        const ps::detail::BootstrapProcessImage httpsImages[]{
            {"https-probe.exe", {httpsProbe.bytes.get(), httpsProbe.size}, httpsPath},
            {"ntdll.dll", {ntdll.bytes.get(), ntdll.size}, ntdllPath},
            {"ws2_32.dll", {winsock.bytes.get(), winsock.size}, winsockPath},
            {"dnsapi.dll", {dnsapi.bytes.get(), dnsapi.size}, dnsapiPath},
            {"crypt32.dll", {crypt32.bytes.get(), crypt32.size}, crypt32Path},
            {"winhttp.dll", {winhttp.bytes.get(), winhttp.size}, winhttpPath},
        };
        static constexpr char16_t httpsCommandLine[] = u"https-probe.exe";
        ScopedHandle httpsProcess{}, httpsThread{};
        status = ps::detail::createBootstrapUserProcess(httpsImages, httpsPath,
            {httpsCommandLine, std::size(httpsCommandLine) - 1}, 32768, 32768, true,
            ob::SYNCHRONIZE, ob::SYNCHRONIZE, httpsProcess.value, httpsThread.value, true);
        if (status != NtStatus::Success) return status;
        LargeInteger httpsTimeout{};
        httpsTimeout.quadPart = -600'000'000; // 60 seconds including certificate rejection leg.
        status = sys::NtWaitForSingleObject(httpsProcess.value, false, &httpsTimeout);
        if (status != NtStatus::Success) {
            (void)sys::NtTerminateProcess(httpsProcess.value, status);
            return status;
        }
    }
    return NtStatus::Success;
}
}
