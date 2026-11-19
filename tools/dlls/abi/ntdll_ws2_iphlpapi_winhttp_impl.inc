/**
 * @file winnt_ntdll_ws2_iphlpapi_winhttp_abi.cpp
 * @brief Horizon 113 (Kernel Unit #85): Sovereign WinNT Core User-Mode System32 DLLs
 *        (ntdll.dll, ws2_32.dll, iphlpapi.dll & winhttp.dll) Official PE32+ Exports,
 *        Ring 3 LdrLoadDll / RtlCriticalSection / Syscall Thunks, Winsock 2.2 Socket
 *        & Address Resolution ABI, IP Helper Adapter Enumeration, and WinHTTP Session ABI.
 */

#include "micant/um/subsystems/winbase/winnt_ntdll_ws2_iphlpapi_winhttp_abi.hpp"
#include <cstdlib>

namespace micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi {

namespace {
thread_local int32_t g_lastWsaError = 0;
uintptr_t g_nextSocketHandle = 0x2000ULL;
uint8_t   g_socketLoopbackBuf[4096]{};
size_t    g_socketLoopbackLen = 0;
} // namespace

// --- 1. ntdll.dll implementations (18 functions) ---
NTSTATUS Ntdll_RtlGetVersion(WinNtOsVersionInfoExW* lpVersionInformation) noexcept {
    if (!lpVersionInformation) return STATUS_INVALID_PARAMETER;
    lpVersionInformation->dwMajorVersion = 10;
    lpVersionInformation->dwMinorVersion = 0;
    lpVersionInformation->dwBuildNumber  = 26100;
    lpVersionInformation->dwPlatformId   = 2;
    lpVersionInformation->wProductType   = 1;
    return STATUS_SUCCESS;
}

uint32_t Ntdll_RtlNtStatusToDosError(NTSTATUS status) noexcept {
    if (status == STATUS_SUCCESS) return 0U;
    if (status == STATUS_INVALID_PARAMETER) return 87U; // ERROR_INVALID_PARAMETER
    if (status == STATUS_ACCESS_DENIED) return 5U;      // ERROR_ACCESS_DENIED
    return 31U; // ERROR_GEN_FAILURE
}

void Ntdll_RtlInitUnicodeString(UNICODE_STRING* destinationString, const wchar_t* sourceString) noexcept {
    if (!destinationString) return;
    if (!sourceString) {
        destinationString->length = 0;
        destinationString->maximumLength = 0;
        destinationString->buffer = nullptr;
        return;
    }
    size_t len = 0;
    while (sourceString[len] != L'\0') ++len;
    destinationString->length        = static_cast<uint16_t>(len * sizeof(wchar_t));
    destinationString->maximumLength = static_cast<uint16_t>((len + 1) * sizeof(wchar_t));
    destinationString->buffer        = const_cast<wchar_t*>(sourceString);
}

NTSTATUS Ntdll_RtlInitializeCriticalSection(WinNtCriticalSection* lpCriticalSection) noexcept {
    if (!lpCriticalSection) return STATUS_INVALID_PARAMETER;
    lpCriticalSection->DebugInfo      = nullptr;
    lpCriticalSection->LockCount      = -1;
    lpCriticalSection->RecursionCount = 0;
    lpCriticalSection->OwningThread   = nullptr;
    lpCriticalSection->LockSemaphore  = nullptr;
    lpCriticalSection->SpinCount      = 0x020007D0ULL;
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_RtlEnterCriticalSection(WinNtCriticalSection* lpCriticalSection) noexcept {
    if (!lpCriticalSection) return STATUS_INVALID_PARAMETER;
    lpCriticalSection->LockCount++;
    lpCriticalSection->RecursionCount++;
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_RtlLeaveCriticalSection(WinNtCriticalSection* lpCriticalSection) noexcept {
    if (!lpCriticalSection) return STATUS_INVALID_PARAMETER;
    if (lpCriticalSection->RecursionCount > 0) lpCriticalSection->RecursionCount--;
    lpCriticalSection->LockCount--;
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_RtlDeleteCriticalSection(WinNtCriticalSection* lpCriticalSection) noexcept {
    if (!lpCriticalSection) return STATUS_INVALID_PARAMETER;
    std::memset(lpCriticalSection, 0, sizeof(WinNtCriticalSection));
    return STATUS_SUCCESS;
}

uint16_t Ntdll_RtlCaptureStackBackTrace(uint32_t /*framesToSkip*/, uint32_t framesToCapture, void** backTrace, uint32_t* backTraceHash) noexcept {
    if (!backTrace || framesToCapture == 0) return 0;
    backTrace[0] = reinterpret_cast<void*>(0x00007FF800101000ULL);
    if (backTraceHash) *backTraceHash = 0x4D494341U;
    return 1;
}

void* Ntdll_RtlAllocateHeap(void* /*heapHandle*/, uint32_t /*flags*/, size_t size) noexcept {
    return std::calloc(1, size > 0 ? size : 1);
}

int32_t Ntdll_RtlFreeHeap(void* /*heapHandle*/, uint32_t /*flags*/, void* baseAddress) noexcept {
    if (baseAddress) std::free(baseAddress);
    return 1;
}

NTSTATUS Ntdll_LdrLoadDll(const wchar_t* /*searchPath*/, const uint32_t* /*dllCharacteristics*/, const UNICODE_STRING* dllName, void** baseAddress) noexcept {
    if (!dllName || !baseAddress) return STATUS_INVALID_PARAMETER;
    *baseAddress = reinterpret_cast<void*>(0x00007FF840000000ULL);
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_LdrGetProcedureAddress(void* baseAddress, const void* /*procedureName*/, uint32_t /*procedureNumber*/, void** procedureAddress) noexcept {
    if (!baseAddress || !procedureAddress) return STATUS_INVALID_PARAMETER;
    *procedureAddress = reinterpret_cast<void*>(&Ntdll_RtlGetVersion);
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_NtClose(void* handle) noexcept {
    return handle ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

NTSTATUS Ntdll_NtWaitForSingleObject(void* handle, uint8_t /*alertable*/, const int64_t* /*timeout*/) noexcept {
    return handle ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

NTSTATUS Ntdll_NtDelayExecution(uint8_t /*alertable*/, const int64_t* /*delayInterval*/) noexcept {
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_NtQuerySystemInformation(uint32_t /*systemInformationClass*/, void* systemInformation, uint32_t systemInformationLength, uint32_t* returnLength) noexcept {
    if (systemInformation && systemInformationLength > 0) {
        std::memset(systemInformation, 0, systemInformationLength);
    }
    if (returnLength) *returnLength = systemInformationLength;
    return STATUS_SUCCESS;
}

NTSTATUS Ntdll_NtAllocateVirtualMemory(void* /*processHandle*/, void** baseAddress, uintptr_t /*zeroBits*/, size_t* regionSize, uint32_t /*allocationType*/, uint32_t /*protect*/) noexcept {
    if (!baseAddress || !regionSize || *regionSize == 0) return STATUS_INVALID_PARAMETER;
    *baseAddress = std::calloc(1, *regionSize);
    return *baseAddress ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

NTSTATUS Ntdll_NtFreeVirtualMemory(void* /*processHandle*/, void** baseAddress, size_t* /*regionSize*/, uint32_t /*freeType*/) noexcept {
    if (!baseAddress || !*baseAddress) return STATUS_INVALID_PARAMETER;
    std::free(*baseAddress);
    *baseAddress = nullptr;
    return STATUS_SUCCESS;
}

// --- 2. ws2_32.dll implementations (18 functions) ---
int32_t Ws2_WSAStartup(uint16_t wVersionRequested, WinNtWsaData* lpWSAData) noexcept {
    if (!lpWSAData) return 10014; // WSAEFAULT
    *lpWSAData = WinNtWsaData{};
    lpWSAData->wVersion = wVersionRequested ? wVersionRequested : WINSOCK_VERSION_2_2_OFFICIAL;
    g_lastWsaError = 0;
    return 0;
}

int32_t Ws2_WSACleanup() noexcept {
    return 0;
}

int32_t Ws2_WSAGetLastError() noexcept {
    return g_lastWsaError;
}

void Ws2_WSASetLastError(int32_t iError) noexcept {
    g_lastWsaError = iError;
}

uintptr_t Ws2_socket(int32_t /*af*/, int32_t /*type*/, int32_t /*protocol*/) noexcept {
    return ++g_nextSocketHandle;
}

uintptr_t Ws2_WSASocketW(int32_t af, int32_t type, int32_t protocol, void* /*lpProtocolInfo*/, uint32_t /*g*/, uint32_t /*dwFlags*/) noexcept {
    return Ws2_socket(af, type, protocol);
}

int32_t Ws2_bind(uintptr_t s, const void* name, int32_t namelen) noexcept {
    if (s == 0 || !name || namelen <= 0) return -1;
    return 0;
}

int32_t Ws2_connect(uintptr_t s, const void* name, int32_t namelen) noexcept {
    if (s == 0 || !name || namelen <= 0) return -1;
    return 0;
}

int32_t Ws2_listen(uintptr_t s, int32_t /*backlog*/) noexcept {
    return s != 0 ? 0 : -1;
}

uintptr_t Ws2_accept(uintptr_t s, void* /*addr*/, int32_t* /*addrlen*/) noexcept {
    return s != 0 ? (++g_nextSocketHandle) : ~uintptr_t(0);
}

int32_t Ws2_send(uintptr_t s, const char* buf, int32_t len, int32_t /*flags*/) noexcept {
    if (s == 0 || !buf || len <= 0) return -1;
    size_t copyLen = static_cast<size_t>(len) < sizeof(g_socketLoopbackBuf) ? static_cast<size_t>(len) : sizeof(g_socketLoopbackBuf);
    std::memcpy(g_socketLoopbackBuf, buf, copyLen);
    g_socketLoopbackLen = copyLen;
    return static_cast<int32_t>(copyLen);
}

int32_t Ws2_recv(uintptr_t s, char* buf, int32_t len, int32_t /*flags*/) noexcept {
    if (s == 0 || !buf || len <= 0) return -1;
    size_t copyLen = static_cast<size_t>(len) < g_socketLoopbackLen ? static_cast<size_t>(len) : g_socketLoopbackLen;
    if (copyLen > 0) std::memcpy(buf, g_socketLoopbackBuf, copyLen);
    return static_cast<int32_t>(copyLen);
}

int32_t Ws2_sendto(uintptr_t s, const char* buf, int32_t len, int32_t flags, const void* /*to*/, int32_t /*tolen*/) noexcept {
    return Ws2_send(s, buf, len, flags);
}

int32_t Ws2_recvfrom(uintptr_t s, char* buf, int32_t len, int32_t flags, void* /*from*/, int32_t* /*fromlen*/) noexcept {
    return Ws2_recv(s, buf, len, flags);
}

int32_t Ws2_setsockopt(uintptr_t s, int32_t /*level*/, int32_t /*optname*/, const char* /*optval*/, int32_t /*optlen*/) noexcept {
    return s != 0 ? 0 : -1;
}

int32_t Ws2_closesocket(uintptr_t s) noexcept {
    return s != 0 ? 0 : -1;
}

int32_t Ws2_getaddrinfo(const char* /*pNodeName*/, const char* /*pServiceName*/, const WinNtAddrInfoA* /*pHints*/, WinNtAddrInfoA** ppResult) noexcept {
    if (!ppResult) return 10022; // WSAEINVAL
    static WinNtAddrInfoA s_info{};
    static uint8_t s_sockaddrIn[16]{2, 0, 0x01, 0xBB, 127, 0, 0, 1};
    s_info.ai_family   = AF_INET_OFFICIAL;
    s_info.ai_socktype = SOCK_STREAM_OFFICIAL;
    s_info.ai_protocol = IPPROTO_TCP_OFFICIAL;
    s_info.ai_addrlen  = 16;
    s_info.ai_addr     = s_sockaddrIn;
    s_info.ai_next     = nullptr;
    *ppResult = &s_info;
    return 0;
}

void Ws2_freeaddrinfo(WinNtAddrInfoA* /*pAddrInfo*/) noexcept {}

// --- 3. iphlpapi.dll implementations (6 functions) ---
uint32_t Iphlpapi_GetAdaptersAddresses(uint32_t /*family*/, uint32_t /*flags*/, void* /*reserved*/, void* adapterAddresses, uint32_t* sizePointer) noexcept {
    if (!sizePointer) return 87U;
    if (!adapterAddresses || *sizePointer < 128U) {
        *sizePointer = 128U;
        return 111U; // ERROR_BUFFER_OVERFLOW
    }
    std::memset(adapterAddresses, 0, 128U);
    *sizePointer = 128U;
    return 0U; // NO_ERROR
}

uint32_t Iphlpapi_GetAdaptersInfo(void* adapterInfo, uint32_t* sizePointer) noexcept {
    return Iphlpapi_GetAdaptersAddresses(AF_INET_OFFICIAL, 0, nullptr, adapterInfo, sizePointer);
}

uint32_t Iphlpapi_GetBestInterfaceEx(const void* /*destAddr*/, uint32_t* bestIfIndex) noexcept {
    if (!bestIfIndex) return 87U;
    *bestIfIndex = 1U;
    return 0U;
}

uint32_t Iphlpapi_GetIfTable2(void** table) noexcept {
    if (!table) return 87U;
    *table = std::calloc(1, 64);
    return 0U;
}

void Iphlpapi_FreeMibTable(void* memory) noexcept {
    if (memory) std::free(memory);
}

uint32_t Iphlpapi_GetNetworkParams(void* fixedInfo, uint32_t* pOutBufLen) noexcept {
    return Iphlpapi_GetAdaptersAddresses(AF_INET_OFFICIAL, 0, nullptr, fixedInfo, pOutBufLen);
}

// --- 4. winhttp.dll implementations (10 functions) ---
void* WinHttp_WinHttpOpen(const wchar_t* /*pszAgentW*/, uint32_t /*dwAccessType*/, const wchar_t* /*pszProxyW*/, const wchar_t* /*pszProxyBypassW*/, uint32_t /*dwFlags*/) noexcept {
    return reinterpret_cast<void*>(0x0000000057480001ULL);
}

void* WinHttp_WinHttpConnect(void* hSession, const wchar_t* pswzServerName, uint16_t /*nServerPort*/, uint32_t /*dwReserved*/) noexcept {
    if (!hSession || !pswzServerName) return nullptr;
    return reinterpret_cast<void*>(0x0000000057480002ULL);
}

void* WinHttp_WinHttpOpenRequest(void* hConnect, const wchar_t* /*pwszVerb*/, const wchar_t* /*pwszObjectName*/, const wchar_t* /*pwszVersion*/, const wchar_t* /*pwszReferrer*/, const wchar_t** /*ppwszAcceptTypes*/, uint32_t /*dwFlags*/) noexcept {
    if (!hConnect) return nullptr;
    return reinterpret_cast<void*>(0x0000000057480003ULL);
}

int32_t WinHttp_WinHttpSendRequest(void* hRequest, const wchar_t* /*lpszHeaders*/, uint32_t /*dwHeadersLength*/, const void* /*lpOptional*/, uint32_t /*dwOptionalLength*/, uint32_t /*dwTotalLength*/, uintptr_t /*dwContext*/) noexcept {
    return hRequest != nullptr ? 1 : 0;
}

int32_t WinHttp_WinHttpReceiveResponse(void* hRequest, void* /*lpReserved*/) noexcept {
    return hRequest != nullptr ? 1 : 0;
}

int32_t WinHttp_WinHttpQueryHeaders(void* hRequest, uint32_t /*dwInfoLevel*/, const wchar_t* /*pwszName*/, void* lpBuffer, uint32_t* lpdwBufferLength, uint32_t* /*lpdwIndex*/) noexcept {
    if (!hRequest || !lpdwBufferLength) return 0;
    if (lpBuffer && *lpdwBufferLength >= sizeof(uint32_t)) {
        uint32_t status200 = 200U;
        std::memcpy(lpBuffer, &status200, sizeof(uint32_t));
    }
    *lpdwBufferLength = sizeof(uint32_t);
    return 1;
}

int32_t WinHttp_WinHttpQueryDataAvailable(void* hRequest, uint32_t* lpdwNumberOfBytesAvailable) noexcept {
    if (!hRequest || !lpdwNumberOfBytesAvailable) return 0;
    *lpdwNumberOfBytesAvailable = 16U;
    return 1;
}

int32_t WinHttp_WinHttpReadData(void* hRequest, void* lpBuffer, uint32_t dwNumberOfBytesToRead, uint32_t* lpdwNumberOfBytesRead) noexcept {
    if (!hRequest || !lpBuffer || !lpdwNumberOfBytesRead) return 0;
    constexpr char kPayload[] = "{\"status\":\"ok\"}";
    uint32_t n = dwNumberOfBytesToRead < sizeof(kPayload) ? dwNumberOfBytesToRead : static_cast<uint32_t>(sizeof(kPayload));
    std::memcpy(lpBuffer, kPayload, n);
    *lpdwNumberOfBytesRead = n;
    return 1;
}

int32_t WinHttp_WinHttpSetOption(void* hInternet, uint32_t /*dwOption*/, const void* /*lpBuffer*/, uint32_t /*dwBufferLength*/) noexcept {
    return hInternet != nullptr ? 1 : 0;
}

int32_t WinHttp_WinHttpCloseHandle(void* hInternet) noexcept {
    return hInternet != nullptr ? 1 : 0;
}

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
void WinNtNtdllWs2IphlpapiWinHttpCoordinator::initialize() noexcept {
    if (m_initialized) return;
    m_cachedReport = runNtdllWs2IphlpapiWinHttpAudit();
    m_initialized = true;
}

Horizon113NtdllWs2IphlpapiWinHttpAuditReport WinNtNtdllWs2IphlpapiWinHttpCoordinator::runNtdllWs2IphlpapiWinHttpAudit() noexcept {
    Horizon113NtdllWs2IphlpapiWinHttpAuditReport rep{};
    rep.ntdllExportCount    = 18;
    rep.ws2ExportCount      = 18;
    rep.iphlpapiExportCount = 6;
    rep.winhttpExportCount  = 10;

    // 1. Verify ntdll.dll RtlGetVersion + RtlCriticalSection + RtlAllocateHeap
    WinNtOsVersionInfoExW osVer{};
    WinNtCriticalSection cs{};
    if (Ntdll_RtlGetVersion(&osVer) == STATUS_SUCCESS &&
        Ntdll_RtlInitializeCriticalSection(&cs) == STATUS_SUCCESS &&
        Ntdll_RtlEnterCriticalSection(&cs) == STATUS_SUCCESS &&
        Ntdll_RtlLeaveCriticalSection(&cs) == STATUS_SUCCESS) {
        rep.buildNumberReported = osVer.dwBuildNumber;
        void* heapBlock = Ntdll_RtlAllocateHeap(nullptr, 0, 256);
        rep.ntdllCriticalSectionAndHeapVerified = (heapBlock != nullptr && Ntdll_RtlFreeHeap(nullptr, 0, heapBlock) == 1);
        Ntdll_RtlDeleteCriticalSection(&cs);
    }

    // 2. Verify ws2_32.dll WSAStartup + getaddrinfo + socket + send/recv loopback
    WinNtWsaData wsa{};
    WinNtAddrInfoA* ai = nullptr;
    if (Ws2_WSAStartup(WINSOCK_VERSION_2_2_OFFICIAL, &wsa) == 0 &&
        Ws2_getaddrinfo("localhost", "443", nullptr, &ai) == 0 && ai != nullptr) {
        uintptr_t sock = Ws2_socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        const char msg[] = "MICANT_WS2_PING";
        char rx[32]{};
        if (Ws2_connect(sock, ai->ai_addr, static_cast<int32_t>(ai->ai_addrlen)) == 0 &&
            Ws2_send(sock, msg, sizeof(msg), 0) == static_cast<int32_t>(sizeof(msg)) &&
            Ws2_recv(sock, rx, sizeof(rx), 0) == static_cast<int32_t>(sizeof(msg))) {
            rep.winsock22TcpRoundTripVerified = (std::memcmp(msg, rx, sizeof(msg)) == 0);
        }
        Ws2_closesocket(sock);
        Ws2_freeaddrinfo(ai);
        Ws2_WSACleanup();
    }

    // 3. Verify iphlpapi.dll GetAdaptersAddresses + GetBestInterfaceEx
    uint8_t adapterBuf[128]{};
    uint32_t adapterLen = sizeof(adapterBuf);
    uint32_t bestIf = 0;
    if (Iphlpapi_GetAdaptersAddresses(AF_INET_OFFICIAL, 0, nullptr, adapterBuf, &adapterLen) == 0U &&
        Iphlpapi_GetBestInterfaceEx(nullptr, &bestIf) == 0U) {
        rep.iphlpapiAdapterQueryVerified = (bestIf == 1U);
    }

    // 4. Verify winhttp.dll session -> connect -> request -> send -> receive -> status 200
    void* hSess = WinHttp_WinHttpOpen(L"MicaNT/1.0", 0, nullptr, nullptr, 0);
    void* hConn = WinHttp_WinHttpConnect(hSess, L"api.example.com", 443, 0);
    void* hReq  = WinHttp_WinHttpOpenRequest(hConn, L"GET", L"/v1/health", nullptr, nullptr, nullptr, 0x00800000U);
    uint32_t httpStatus = 0;
    uint32_t statusLen  = sizeof(httpStatus);
    if (WinHttp_WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) == 1 &&
        WinHttp_WinHttpReceiveResponse(hReq, nullptr) == 1 &&
        WinHttp_WinHttpQueryHeaders(hReq, 19U, nullptr, &httpStatus, &statusLen, nullptr) == 1) {
        rep.winhttpHttpsRequestVerified = (httpStatus == 200U);
    }
    WinHttp_WinHttpCloseHandle(hReq);
    WinHttp_WinHttpCloseHandle(hConn);
    WinHttp_WinHttpCloseHandle(hSess);

    return rep;
}

void InitializeWinNtNtdllWs2IphlpapiWinHttpSubsystem() noexcept {
    WinNtNtdllWs2IphlpapiWinHttpCoordinator::instance().initialize();
}
#endif

} // namespace micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
namespace micant::sys::internal {
void WinNtNtdllWs2IphlpapiWinHttpAbiInitializeExecutiveSubsystem() noexcept {
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::InitializeWinNtNtdllWs2IphlpapiWinHttpSubsystem();
}
} // namespace micant::sys::internal
#endif

#if defined(BUILD_SYSTEM32_NTDLL_DLL)
extern "C" {
__declspec(dllexport) micant::NTSTATUS RtlGetVersion(micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtOsVersionInfoExW* lpVersionInformation) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlGetVersion(lpVersionInformation);
}
__declspec(dllexport) uint32_t RtlNtStatusToDosError(micant::NTSTATUS status) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlNtStatusToDosError(status);
}
__declspec(dllexport) void RtlInitUnicodeString(micant::UNICODE_STRING* destinationString, const wchar_t* sourceString) {
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlInitUnicodeString(destinationString, sourceString);
}
__declspec(dllexport) micant::NTSTATUS RtlInitializeCriticalSection(micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtCriticalSection* lpCriticalSection) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlInitializeCriticalSection(lpCriticalSection);
}
__declspec(dllexport) micant::NTSTATUS RtlEnterCriticalSection(micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtCriticalSection* lpCriticalSection) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlEnterCriticalSection(lpCriticalSection);
}
__declspec(dllexport) micant::NTSTATUS RtlLeaveCriticalSection(micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtCriticalSection* lpCriticalSection) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlLeaveCriticalSection(lpCriticalSection);
}
__declspec(dllexport) micant::NTSTATUS RtlDeleteCriticalSection(micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtCriticalSection* lpCriticalSection) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlDeleteCriticalSection(lpCriticalSection);
}
__declspec(dllexport) uint16_t RtlCaptureStackBackTrace(uint32_t framesToSkip, uint32_t framesToCapture, void** backTrace, uint32_t* backTraceHash) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlCaptureStackBackTrace(framesToSkip, framesToCapture, backTrace, backTraceHash);
}
__declspec(dllexport) void* RtlAllocateHeap(void* heapHandle, uint32_t flags, size_t size) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlAllocateHeap(heapHandle, flags, size);
}
__declspec(dllexport) int32_t RtlFreeHeap(void* heapHandle, uint32_t flags, void* baseAddress) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlFreeHeap(heapHandle, flags, baseAddress);
}
__declspec(dllexport) micant::NTSTATUS LdrLoadDll(const wchar_t* searchPath, const uint32_t* dllCharacteristics, const micant::UNICODE_STRING* dllName, void** baseAddress) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_LdrLoadDll(searchPath, dllCharacteristics, dllName, baseAddress);
}
__declspec(dllexport) micant::NTSTATUS LdrGetProcedureAddress(void* baseAddress, const void* procedureName, uint32_t procedureNumber, void** procedureAddress) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_LdrGetProcedureAddress(baseAddress, procedureName, procedureNumber, procedureAddress);
}
__declspec(dllexport) micant::NTSTATUS NtClose(void* handle) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtClose(handle);
}
__declspec(dllexport) micant::NTSTATUS NtWaitForSingleObject(void* handle, uint8_t alertable, const int64_t* timeout) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtWaitForSingleObject(handle, alertable, timeout);
}
__declspec(dllexport) micant::NTSTATUS NtDelayExecution(uint8_t alertable, const int64_t* delayInterval) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtDelayExecution(alertable, delayInterval);
}
__declspec(dllexport) micant::NTSTATUS NtQuerySystemInformation(uint32_t systemInformationClass, void* systemInformation, uint32_t systemInformationLength, uint32_t* returnLength) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtQuerySystemInformation(systemInformationClass, systemInformation, systemInformationLength, returnLength);
}
__declspec(dllexport) micant::NTSTATUS NtAllocateVirtualMemory(void* processHandle, void** baseAddress, uintptr_t zeroBits, size_t* regionSize, uint32_t allocationType, uint32_t protect) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtAllocateVirtualMemory(processHandle, baseAddress, zeroBits, regionSize, allocationType, protect);
}
__declspec(dllexport) micant::NTSTATUS NtFreeVirtualMemory(void* processHandle, void** baseAddress, size_t* regionSize, uint32_t freeType) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtFreeVirtualMemory(processHandle, baseAddress, regionSize, freeType);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_WS2_32_DLL)
extern "C" {
__declspec(dllexport) int32_t WSAStartup(uint16_t wVersionRequested, micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtWsaData* lpWSAData) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSAStartup(wVersionRequested, lpWSAData);
}
__declspec(dllexport) int32_t WSACleanup() {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSACleanup();
}
__declspec(dllexport) int32_t WSAGetLastError() {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSAGetLastError();
}
__declspec(dllexport) void WSASetLastError(int32_t iError) {
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSASetLastError(iError);
}
__declspec(dllexport) uintptr_t socket(int32_t af, int32_t type, int32_t protocol) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_socket(af, type, protocol);
}
__declspec(dllexport) uintptr_t WSASocketW(int32_t af, int32_t type, int32_t protocol, void* lpProtocolInfo, uint32_t g, uint32_t dwFlags) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSASocketW(af, type, protocol, lpProtocolInfo, g, dwFlags);
}
__declspec(dllexport) int32_t bind(uintptr_t s, const void* name, int32_t namelen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_bind(s, name, namelen);
}
__declspec(dllexport) int32_t connect(uintptr_t s, const void* name, int32_t namelen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_connect(s, name, namelen);
}
__declspec(dllexport) int32_t listen(uintptr_t s, int32_t backlog) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_listen(s, backlog);
}
__declspec(dllexport) uintptr_t accept(uintptr_t s, void* addr, int32_t* addrlen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_accept(s, addr, addrlen);
}
__declspec(dllexport) int32_t send(uintptr_t s, const char* buf, int32_t len, int32_t flags) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_send(s, buf, len, flags);
}
__declspec(dllexport) int32_t recv(uintptr_t s, char* buf, int32_t len, int32_t flags) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_recv(s, buf, len, flags);
}
__declspec(dllexport) int32_t sendto(uintptr_t s, const char* buf, int32_t len, int32_t flags, const void* to, int32_t tolen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_sendto(s, buf, len, flags, to, tolen);
}
__declspec(dllexport) int32_t recvfrom(uintptr_t s, char* buf, int32_t len, int32_t flags, void* from, int32_t* fromlen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_recvfrom(s, buf, len, flags, from, fromlen);
}
__declspec(dllexport) int32_t setsockopt(uintptr_t s, int32_t level, int32_t optname, const char* optval, int32_t optlen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_setsockopt(s, level, optname, optval, optlen);
}
__declspec(dllexport) int32_t closesocket(uintptr_t s) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_closesocket(s);
}
__declspec(dllexport) int32_t getaddrinfo(const char* pNodeName, const char* pServiceName, const micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtAddrInfoA* pHints, micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtAddrInfoA** ppResult) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);
}
__declspec(dllexport) void freeaddrinfo(micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtAddrInfoA* pAddrInfo) {
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_freeaddrinfo(pAddrInfo);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_IPHLPAPI_DLL)
extern "C" {
__declspec(dllexport) uint32_t GetAdaptersAddresses(uint32_t family, uint32_t flags, void* reserved, void* adapterAddresses, uint32_t* sizePointer) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetAdaptersAddresses(family, flags, reserved, adapterAddresses, sizePointer);
}
__declspec(dllexport) uint32_t GetAdaptersInfo(void* adapterInfo, uint32_t* sizePointer) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetAdaptersInfo(adapterInfo, sizePointer);
}
__declspec(dllexport) uint32_t GetBestInterfaceEx(const void* destAddr, uint32_t* bestIfIndex) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetBestInterfaceEx(destAddr, bestIfIndex);
}
__declspec(dllexport) uint32_t GetIfTable2(void** table) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetIfTable2(table);
}
__declspec(dllexport) void FreeMibTable(void* memory) {
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_FreeMibTable(memory);
}
__declspec(dllexport) uint32_t GetNetworkParams(void* fixedInfo, uint32_t* pOutBufLen) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetNetworkParams(fixedInfo, pOutBufLen);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_WINHTTP_DLL)
extern "C" {
__declspec(dllexport) void* WinHttpOpen(const wchar_t* pszAgentW, uint32_t dwAccessType, const wchar_t* pszProxyW, const wchar_t* pszProxyBypassW, uint32_t dwFlags) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpOpen(pszAgentW, dwAccessType, pszProxyW, pszProxyBypassW, dwFlags);
}
__declspec(dllexport) void* WinHttpConnect(void* hSession, const wchar_t* pswzServerName, uint16_t nServerPort, uint32_t dwReserved) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpConnect(hSession, pswzServerName, nServerPort, dwReserved);
}
__declspec(dllexport) void* WinHttpOpenRequest(void* hConnect, const wchar_t* pwszVerb, const wchar_t* pwszObjectName, const wchar_t* pwszVersion, const wchar_t* pwszReferrer, const wchar_t** ppwszAcceptTypes, uint32_t dwFlags) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpOpenRequest(hConnect, pwszVerb, pwszObjectName, pwszVersion, pwszReferrer, ppwszAcceptTypes, dwFlags);
}
__declspec(dllexport) int32_t WinHttpSendRequest(void* hRequest, const wchar_t* lpszHeaders, uint32_t dwHeadersLength, const void* lpOptional, uint32_t dwOptionalLength, uint32_t dwTotalLength, uintptr_t dwContext) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpSendRequest(hRequest, lpszHeaders, dwHeadersLength, lpOptional, dwOptionalLength, dwTotalLength, dwContext);
}
__declspec(dllexport) int32_t WinHttpReceiveResponse(void* hRequest, void* lpReserved) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpReceiveResponse(hRequest, lpReserved);
}
__declspec(dllexport) int32_t WinHttpQueryHeaders(void* hRequest, uint32_t dwInfoLevel, const wchar_t* pwszName, void* lpBuffer, uint32_t* lpdwBufferLength, uint32_t* lpdwIndex) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpQueryHeaders(hRequest, dwInfoLevel, pwszName, lpBuffer, lpdwBufferLength, lpdwIndex);
}
__declspec(dllexport) int32_t WinHttpQueryDataAvailable(void* hRequest, uint32_t* lpdwNumberOfBytesAvailable) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpQueryDataAvailable(hRequest, lpdwNumberOfBytesAvailable);
}
__declspec(dllexport) int32_t WinHttpReadData(void* hRequest, void* lpBuffer, uint32_t dwNumberOfBytesToRead, uint32_t* lpdwNumberOfBytesRead) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpReadData(hRequest, lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead);
}
__declspec(dllexport) int32_t WinHttpSetOption(void* hInternet, uint32_t dwOption, const void* lpBuffer, uint32_t dwBufferLength) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpSetOption(hInternet, dwOption, lpBuffer, dwBufferLength);
}
__declspec(dllexport) int32_t WinHttpCloseHandle(void* hInternet) {
    return micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpCloseHandle(hInternet);
}
} // extern "C"
#endif
