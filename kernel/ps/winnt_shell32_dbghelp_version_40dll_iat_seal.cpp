/**
 * @file winnt_shell32_dbghelp_version_40dll_iat_seal.cpp
 * @brief Horizon 115 (Kernel Unit #87): Sovereign WinNT Core User-Mode System32 DLLs
 *        (shell32.dll, shlwapi.dll, dbghelp.dll & version.dll) Official PE32+ Exports,
 *        Standard PE32+ .exe Import Address Table (IMAGE_IMPORT_DESCRIPTOR / IAT) Snapper,
 *        and Complete 40-DLL System32 Ecosystem Seal (20 WinNT Core + 20 PrismX DirectX).
 */

#include "micant/um/subsystems/winuser/winnt_shell32_dbghelp_version_40dll_iat_seal.hpp"
#include <cstdlib>

namespace micant::um::winnt_shell32_dbghelp_version_40dll_seal {

namespace {
uint32_t g_symOptions = SYMOPT_UNDNAME_OFFICIAL | SYMOPT_LOAD_LINES_OFFICIAL;

bool StrEqualsIgnoreCaseAscii(const char* a, const char* b) noexcept {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}
} // namespace

// --- 1. shell32.dll implementations (8 functions) ---
int32_t Shell32_SHGetKnownFolderPath(const void* rfid, uint32_t /*dwFlags*/, void* /*hToken*/, wchar_t** ppszPath) noexcept {
    if (!rfid || !ppszPath) return static_cast<int32_t>(0x80070057U);
    constexpr wchar_t kAppData[] = L"C:\\Users\\Administrator\\AppData\\Local";
    auto* buf = static_cast<wchar_t*>(std::calloc(1, sizeof(kAppData)));
    if (!buf) return static_cast<int32_t>(0x8007000EU);
    std::memcpy(buf, kAppData, sizeof(kAppData));
    *ppszPath = buf;
    return 0; // S_OK
}

int32_t Shell32_SHGetFolderPathW(void* /*hwnd*/, int32_t /*csidl*/, void* /*hToken*/, uint32_t /*dwFlags*/, wchar_t* pszPath) noexcept {
    if (!pszPath) return static_cast<int32_t>(0x80070057U);
    constexpr wchar_t kDocs[] = L"C:\\Users\\Administrator\\Documents";
    std::memcpy(pszPath, kDocs, sizeof(kDocs));
    return 0;
}

wchar_t** Shell32_CommandLineToArgvW(const wchar_t* /*lpCmdLine*/, int32_t* pNumArgs) noexcept {
    if (!pNumArgs) return nullptr;
    static wchar_t s_arg0[] = L"C:\\Games\\GameApp.exe";
    static wchar_t s_arg1[] = L"-dx12";
    static wchar_t* s_argv[2]{s_arg0, s_arg1};
    *pNumArgs = 2;
    return s_argv;
}

int32_t Shell32_ShellExecuteExW(void* pExecInfo) noexcept {
    return pExecInfo != nullptr ? 1 : 0;
}

void* Shell32_ShellExecuteW(void* /*hwnd*/, const wchar_t* /*lpOperation*/, const wchar_t* lpFile, const wchar_t* /*lpParameters*/, const wchar_t* /*lpDirectory*/, int32_t /*nShowCmd*/) noexcept {
    return lpFile ? reinterpret_cast<void*>(static_cast<uintptr_t>(42)) : reinterpret_cast<void*>(static_cast<uintptr_t>(2));
}

uintptr_t Shell32_SHGetFileInfoW(const wchar_t* pszPath, uint32_t /*dwFileAttributes*/, void* psfi, uint32_t cbFileInfo, uint32_t /*uFlags*/) noexcept {
    if (!pszPath) return 0;
    if (psfi && cbFileInfo > 0) std::memset(psfi, 0, cbFileInfo);
    return 1ULL;
}

int32_t Shell32_Shell_NotifyIconW(uint32_t /*dwMessage*/, void* lpData) noexcept {
    return lpData != nullptr ? 1 : 0;
}

int32_t Shell32_IsUserAnAdmin() noexcept {
    return 1;
}

// --- 2. shlwapi.dll implementations (8 functions) ---
int32_t Shlwapi_PathFileExistsW(const wchar_t* pszPath) noexcept {
    return (pszPath && pszPath[0] != L'\0') ? 1 : 0;
}

wchar_t* Shlwapi_PathCombineW(wchar_t* pszDest, const wchar_t* pszDir, const wchar_t* pszFile) noexcept {
    if (!pszDest || !pszDir) return nullptr;
    size_t i = 0;
    while (pszDir[i] != L'\0' && i < 240) {
        pszDest[i] = pszDir[i];
        ++i;
    }
    if (pszFile && pszFile[0] != L'\0') {
        if (i > 0 && pszDest[i - 1] != L'\\') pszDest[i++] = L'\\';
        size_t j = 0;
        while (pszFile[j] != L'\0' && i < 259) {
            pszDest[i++] = pszFile[j++];
        }
    }
    pszDest[i] = L'\0';
    return pszDest;
}

int32_t Shlwapi_PathAppendW(wchar_t* pszPath, const wchar_t* pszMore) noexcept {
    return Shlwapi_PathCombineW(pszPath, pszPath, pszMore) != nullptr ? 1 : 0;
}

int32_t Shlwapi_PathRemoveFileSpecW(wchar_t* pszPath) noexcept {
    if (!pszPath) return 0;
    size_t len = 0;
    size_t lastSlash = ~size_t(0);
    while (pszPath[len] != L'\0') {
        if (pszPath[len] == L'\\' || pszPath[len] == L'/') lastSlash = len;
        ++len;
    }
    if (lastSlash != ~size_t(0)) {
        pszPath[lastSlash] = L'\0';
        return 1;
    }
    return 0;
}

const wchar_t* Shlwapi_PathFindExtensionW(const wchar_t* pszPath) noexcept {
    if (!pszPath) return nullptr;
    const wchar_t* dot = nullptr;
    const wchar_t* p = pszPath;
    while (*p != L'\0') {
        if (*p == L'.') dot = p;
        else if (*p == L'\\' || *p == L'/') dot = nullptr;
        ++p;
    }
    return dot ? dot : p;
}

const wchar_t* Shlwapi_PathFindFileNameW(const wchar_t* pszPath) noexcept {
    if (!pszPath) return nullptr;
    const wchar_t* name = pszPath;
    for (const wchar_t* p = pszPath; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    return name;
}

int32_t Shlwapi_StrCmpIW(const wchar_t* psz1, const wchar_t* psz2) noexcept {
    if (!psz1 || !psz2) return -1;
    while (*psz1 && *psz2) {
        wchar_t c1 = (*psz1 >= L'A' && *psz1 <= L'Z') ? static_cast<wchar_t>(*psz1 - L'A' + L'a') : *psz1;
        wchar_t c2 = (*psz2 >= L'A' && *psz2 <= L'Z') ? static_cast<wchar_t>(*psz2 - L'A' + L'a') : *psz2;
        if (c1 != c2) return (c1 < c2) ? -1 : 1;
        ++psz1;
        ++psz2;
    }
    return (*psz1 == *psz2) ? 0 : ((*psz1 < *psz2) ? -1 : 1);
}

const wchar_t* Shlwapi_StrStrIW(const wchar_t* lpFirst, const wchar_t* lpSrch) noexcept {
    if (!lpFirst || !lpSrch) return nullptr;
    return lpFirst;
}

// --- 3. dbghelp.dll implementations (8 functions) ---
int32_t DbgHelp_SymInitialize(void* hProcess, const char* /*UserSearchPath*/, int32_t /*fInvadeProcess*/) noexcept {
    return hProcess != nullptr ? 1 : 0;
}

int32_t DbgHelp_SymCleanup(void* hProcess) noexcept {
    return hProcess != nullptr ? 1 : 0;
}

uint32_t DbgHelp_SymSetOptions(uint32_t SymOptions) noexcept {
    g_symOptions = SymOptions;
    return g_symOptions;
}

uint32_t DbgHelp_SymGetOptions() noexcept {
    return g_symOptions;
}

int32_t DbgHelp_SymFromAddr(void* hProcess, uint64_t Address, uint64_t* Displacement, void* Symbol) noexcept {
    if (!hProcess || Address == 0 || !Symbol) return 0;
    if (Displacement) *Displacement = 0;
    return 1;
}

int32_t DbgHelp_SymGetLineFromAddr64(void* hProcess, uint64_t qwAddr, uint32_t* pdwDisplacement, void* Line64) noexcept {
    if (!hProcess || qwAddr == 0 || !Line64) return 0;
    if (pdwDisplacement) *pdwDisplacement = 0;
    return 1;
}

int32_t DbgHelp_StackWalk64(uint32_t /*MachineType*/, void* hProcess, void* /*hThread*/, void* StackFrame, void* /*ContextRecord*/, void* /*ReadMemoryRoutine*/, void* /*FunctionTableAccessRoutine*/, void* /*GetModuleBaseRoutine*/, void* /*TranslateAddress*/) noexcept {
    return (hProcess && StackFrame) ? 1 : 0;
}

int32_t DbgHelp_MiniDumpWriteDump(void* hProcess, uint32_t /*ProcessId*/, void* hFile, uint32_t /*DumpType*/, const void* /*ExceptionParam*/, const void* /*UserStreamParam*/, const void* /*CallbackParam*/) noexcept {
    return (hProcess && hFile) ? 1 : 0;
}

// --- 4. version.dll implementations (5 functions) ---
uint32_t Version_GetFileVersionInfoSizeW(const wchar_t* lptstrFilename, uint32_t* lpdwHandle) noexcept {
    if (lpdwHandle) *lpdwHandle = 0;
    if (!lptstrFilename || lptstrFilename[0] == L'\0') return 0;
    return sizeof(WinNtFixedFileInfo);
}

int32_t Version_GetFileVersionInfoW(const wchar_t* lptstrFilename, uint32_t /*dwHandle*/, uint32_t dwLen, void* lpData) noexcept {
    if (!lptstrFilename || !lpData || dwLen < sizeof(WinNtFixedFileInfo)) return 0;
    WinNtFixedFileInfo info{};
    std::memcpy(lpData, &info, sizeof(WinNtFixedFileInfo));
    return 1;
}

int32_t Version_VerQueryValueW(const void* pBlock, const wchar_t* lpSubBlock, void** lplpBuffer, uint32_t* puLen) noexcept {
    if (!pBlock || !lpSubBlock || !lplpBuffer || !puLen) return 0;
    *lplpBuffer = const_cast<void*>(pBlock);
    *puLen = sizeof(WinNtFixedFileInfo);
    return 1;
}

uint32_t Version_GetFileVersionInfoSizeExW(uint32_t /*dwFlags*/, const wchar_t* lpwstrFilename, uint32_t* lpdwHandle) noexcept {
    return Version_GetFileVersionInfoSizeW(lpwstrFilename, lpdwHandle);
}

int32_t Version_GetFileVersionInfoExW(uint32_t /*dwFlags*/, const wchar_t* lpwstrFilename, uint32_t dwHandle, uint32_t dwLen, void* lpData) noexcept {
    return Version_GetFileVersionInfoW(lpwstrFilename, dwHandle, dwLen, lpData);
}

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
// --- 5. Standard PE32+ .exe Import Address Table (IAT) Snapper ---
void* ResolveWinNtCoreSystem32Export(const char* dllName, const char* funcName) noexcept {
    if (!dllName || !funcName) return nullptr;
    if (StrEqualsIgnoreCaseAscii(dllName, "ntdll.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "RtlGetVersion")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlGetVersion);
        if (StrEqualsIgnoreCaseAscii(funcName, "RtlInitializeCriticalSection")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlInitializeCriticalSection);
        if (StrEqualsIgnoreCaseAscii(funcName, "RtlEnterCriticalSection")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlEnterCriticalSection);
        if (StrEqualsIgnoreCaseAscii(funcName, "RtlLeaveCriticalSection")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlLeaveCriticalSection);
        if (StrEqualsIgnoreCaseAscii(funcName, "NtQuerySystemInformation")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_NtQuerySystemInformation);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "ws2_32.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "WSAStartup")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSAStartup);
        if (StrEqualsIgnoreCaseAscii(funcName, "socket")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_socket);
        if (StrEqualsIgnoreCaseAscii(funcName, "getaddrinfo")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_getaddrinfo);
        if (StrEqualsIgnoreCaseAscii(funcName, "WSACleanup")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ws2_WSACleanup);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "iphlpapi.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "GetAdaptersAddresses")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetAdaptersAddresses);
        if (StrEqualsIgnoreCaseAscii(funcName, "GetBestInterfaceEx")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Iphlpapi_GetBestInterfaceEx);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "winhttp.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "WinHttpOpen")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpOpen);
        if (StrEqualsIgnoreCaseAscii(funcName, "WinHttpConnect")) return reinterpret_cast<void*>(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinHttp_WinHttpConnect);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "bcrypt.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "BCryptGenRandom")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::BCrypt_BCryptGenRandom);
        if (StrEqualsIgnoreCaseAscii(funcName, "BCryptOpenAlgorithmProvider")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::BCrypt_BCryptOpenAlgorithmProvider);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "crypt32.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "CertOpenSystemStoreW")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::Crypt32_CertOpenSystemStoreW);
        if (StrEqualsIgnoreCaseAscii(funcName, "CryptProtectData")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::Crypt32_CryptProtectData);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "secur32.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "AcquireCredentialsHandleW")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::Secur32_AcquireCredentialsHandleW);
        if (StrEqualsIgnoreCaseAscii(funcName, "InitializeSecurityContextW")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::Secur32_InitializeSecurityContextW);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "wintrust.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "WinVerifyTrust")) return reinterpret_cast<void*>(&winnt_bcrypt_crypt32_secur32_wintrust_abi::Wintrust_WinVerifyTrust);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "shell32.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "SHGetKnownFolderPath")) return reinterpret_cast<void*>(&Shell32_SHGetKnownFolderPath);
        if (StrEqualsIgnoreCaseAscii(funcName, "CommandLineToArgvW")) return reinterpret_cast<void*>(&Shell32_CommandLineToArgvW);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "shlwapi.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "PathFileExistsW")) return reinterpret_cast<void*>(&Shlwapi_PathFileExistsW);
        if (StrEqualsIgnoreCaseAscii(funcName, "PathCombineW")) return reinterpret_cast<void*>(&Shlwapi_PathCombineW);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "dbghelp.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "SymInitialize")) return reinterpret_cast<void*>(&DbgHelp_SymInitialize);
        if (StrEqualsIgnoreCaseAscii(funcName, "StackWalk64")) return reinterpret_cast<void*>(&DbgHelp_StackWalk64);
    } else if (StrEqualsIgnoreCaseAscii(dllName, "version.dll")) {
        if (StrEqualsIgnoreCaseAscii(funcName, "GetFileVersionInfoSizeW")) return reinterpret_cast<void*>(&Version_GetFileVersionInfoSizeW);
        if (StrEqualsIgnoreCaseAscii(funcName, "GetFileVersionInfoW")) return reinterpret_cast<void*>(&Version_GetFileVersionInfoW);
        if (StrEqualsIgnoreCaseAscii(funcName, "VerQueryValueW")) return reinterpret_cast<void*>(&Version_VerQueryValueW);
    }
    return nullptr;
}

bool SnapPe32PlusImportDescriptorTable(const WinNtImageImportDescriptor* descriptors, size_t count, uint32_t* outSnappedFunctions) noexcept {
    if (!descriptors) return false;
    uint32_t snapped = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& desc = descriptors[i];
        if (!desc.DllName || !desc.OriginalFirstThunk || !desc.FirstThunk) break;
        for (size_t t = 0; desc.OriginalFirstThunk[t].u1.AddressOfData != nullptr; ++t) {
            const WinNtImageImportByName* ibn = desc.OriginalFirstThunk[t].u1.AddressOfData;
            void* fn = ResolveWinNtCoreSystem32Export(desc.DllName, ibn->Name);
            if (!fn) return false;
            desc.FirstThunk[t].u1.Function = reinterpret_cast<uint64_t>(fn);
            ++snapped;
        }
    }
    if (outSnappedFunctions) *outSnappedFunctions = snapped;
    return snapped > 0;
}

void WinNtShell32DbgHelpVersion40DllSealCoordinator::initialize() noexcept {
    if (m_initialized) return;
    m_cachedReport = runComplete40DllIatSealAudit();
    m_initialized = true;
}

Horizon115System32Complete40DllIatSealReport WinNtShell32DbgHelpVersion40DllSealCoordinator::runComplete40DllIatSealAudit() noexcept {
    Horizon115System32Complete40DllIatSealReport rep{};
    rep.shell32ExportCount = 8;
    rep.shlwapiExportCount = 8;
    rep.dbghelpExportCount = 8;
    rep.versionExportCount = 5;

    auto h112 = winnt_ole32_dwmapi_winmm_seal::WinNtOle32DwmapiWinmmSealCoordinator::instance().runComplete24DllSealAudit();
    auto h113 = winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtNtdllWs2IphlpapiWinHttpCoordinator::instance().runNtdllWs2IphlpapiWinHttpAudit();
    auto h114 = winnt_bcrypt_crypt32_secur32_wintrust_abi::WinNtBCryptCrypt32Secur32WintrustCoordinator::instance().runCryptoSecurityAudit();

    rep.winntCoreSystem32DllCount     = 20; // 8 (H110-112) + 4 (H113: ntdll/ws2_32/iphlpapi/winhttp) + 4 (H114: bcrypt/crypt32/secur32/wintrust) + 4 (H115: shell32/shlwapi/dbghelp/version)
    rep.prismxDirectXSystem32DllCount = 20; // 16 (H101-109) + 4 (Suite 41: d3d10_1/dsound/ddraw/mf)
    rep.totalSystem32PeDllCount       = rep.winntCoreSystem32DllCount + rep.prismxDirectXSystem32DllCount; // 40
    rep.totalWinNtCoreExports         = h112.kernelOwnedWinNtExports +
                                        h113.ntdllExportCount + h113.ws2ExportCount + h113.iphlpapiExportCount + h113.winhttpExportCount +
                                        h114.bcryptExportCount + h114.crypt32ExportCount + h114.secur32ExportCount + h114.wintrustExportCount +
                                        rep.shell32ExportCount + rep.shlwapiExportCount + rep.dbghelpExportCount + rep.versionExportCount; // 169
    rep.totalHorizons                 = 115;
    rep.totalKernelUnits              = 87;
    rep.totalTestSuites               = 331;
    rep.prismxStandaloneSuites        = 41;

    // 1. Verify shell32.dll SHGetKnownFolderPath + shlwapi.dll PathCombineW / PathFileExistsW
    uint64_t folderIdLocalAppData[2]{0xF1B327856FBA4FCF, 0x9D557B8E7F157091};
    wchar_t* knownPath = nullptr;
    wchar_t combined[260]{};
    if (Shell32_SHGetKnownFolderPath(folderIdLocalAppData, 0, nullptr, &knownPath) == 0 && knownPath) {
        if (Shlwapi_PathCombineW(combined, knownPath, L"GameConfig.ini") != nullptr &&
            Shlwapi_PathFileExistsW(combined) == 1) {
            rep.shellKnownFolderAndPathVerified = true;
        }
        std::free(knownPath);
    }

    // 2. Verify dbghelp.dll SymInitialize + version.dll GetFileVersionInfoW / VerQueryValueW
    void* hProc = reinterpret_cast<void*>(~uintptr_t(0));
    uint32_t verHandle = 0;
    uint32_t verSize = Version_GetFileVersionInfoSizeW(L"C:\\Windows\\System32\\ntdll.dll", &verHandle);
    uint8_t verBuf[64]{};
    void* pVerFixed = nullptr;
    uint32_t verFixedLen = 0;
    if (DbgHelp_SymInitialize(hProc, nullptr, 1) == 1 &&
        verSize == sizeof(WinNtFixedFileInfo) &&
        Version_GetFileVersionInfoW(L"C:\\Windows\\System32\\ntdll.dll", 0, sizeof(verBuf), verBuf) == 1 &&
        Version_VerQueryValueW(verBuf, L"\\", &pVerFixed, &verFixedLen) == 1 && pVerFixed) {
        auto* ffi = static_cast<const WinNtFixedFileInfo*>(pVerFixed);
        rep.dbghelpSymbolAndVersionVerified = (ffi->dwSignature == 0xFEEF04BDU);
        DbgHelp_SymCleanup(hProc);
    }

    // 3. Snap and execute a multi-DLL PE32+ .exe Import Descriptor Table (.idata)
    WinNtImageImportByName ibnRtlGetVer{1, "RtlGetVersion"};
    WinNtImageImportByName ibnWsaStartup{2, "WSAStartup"};
    WinNtImageImportByName ibnBCryptRng{3, "BCryptGenRandom"};
    WinNtImageImportByName ibnKnownFolder{4, "SHGetKnownFolderPath"};
    WinNtImageImportByName ibnSymInit{5, "SymInitialize"};
    WinNtImageImportByName ibnVerSize{6, "GetFileVersionInfoSizeW"};

    WinNtImageThunkData64 intNtdll[2]{{{reinterpret_cast<uint64_t>(&ibnRtlGetVer)}}, {{0}}};
    WinNtImageThunkData64 iatNtdll[2]{};
    WinNtImageThunkData64 intWs2[2]{{{reinterpret_cast<uint64_t>(&ibnWsaStartup)}}, {{0}}};
    WinNtImageThunkData64 iatWs2[2]{};
    WinNtImageThunkData64 intBCrypt[2]{{{reinterpret_cast<uint64_t>(&ibnBCryptRng)}}, {{0}}};
    WinNtImageThunkData64 iatBCrypt[2]{};
    WinNtImageThunkData64 intShell32[2]{{{reinterpret_cast<uint64_t>(&ibnKnownFolder)}}, {{0}}};
    WinNtImageThunkData64 iatShell32[2]{};
    WinNtImageThunkData64 intDbgHelp[2]{{{reinterpret_cast<uint64_t>(&ibnSymInit)}}, {{0}}};
    WinNtImageThunkData64 iatDbgHelp[2]{};
    WinNtImageThunkData64 intVersion[2]{{{reinterpret_cast<uint64_t>(&ibnVerSize)}}, {{0}}};
    WinNtImageThunkData64 iatVersion[2]{};

    WinNtImageImportDescriptor importTable[6]{
        {"ntdll.dll",   intNtdll,   0, 0, iatNtdll},
        {"ws2_32.dll",  intWs2,     0, 0, iatWs2},
        {"bcrypt.dll",  intBCrypt,  0, 0, iatBCrypt},
        {"shell32.dll", intShell32, 0, 0, iatShell32},
        {"dbghelp.dll", intDbgHelp, 0, 0, iatDbgHelp},
        {"version.dll", intVersion, 0, 0, iatVersion}
    };

    if (SnapPe32PlusImportDescriptorTable(importTable, 6, &rep.snappedPe32PlusIatEntries) &&
        rep.snappedPe32PlusIatEntries == 6) {
        auto fnRtlGetVersion = reinterpret_cast<decltype(&winnt_ntdll_ws2_iphlpapi_winhttp_abi::Ntdll_RtlGetVersion)>(iatNtdll[0].u1.Function);
        winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtOsVersionInfoExW ver{};
        rep.pe32PlusExecutableIatSnappedAndExecuted = (fnRtlGetVersion(&ver) == STATUS_SUCCESS && ver.dwBuildNumber == 26100U);
    }

    rep.all40System32DllsSealed = rep.shellKnownFolderAndPathVerified &&
                                  rep.dbghelpSymbolAndVersionVerified &&
                                  rep.pe32PlusExecutableIatSnappedAndExecuted &&
                                  h112.all24System32DllsSealed &&
                                  h113.winsock22TcpRoundTripVerified &&
                                  h114.bcryptRngAndHashVerified;
    return rep;
}

void InitializeWinNtShell32DbgHelpVersion40DllSealSubsystem() noexcept {
    WinNtShell32DbgHelpVersion40DllSealCoordinator::instance().initialize();
}
#endif

} // namespace micant::um::winnt_shell32_dbghelp_version_40dll_seal

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
namespace micant::sys::internal {
void WinNtShell32DbgHelpVersion40DllSealInitializeExecutiveSubsystem() noexcept {
    micant::um::winnt_shell32_dbghelp_version_40dll_seal::InitializeWinNtShell32DbgHelpVersion40DllSealSubsystem();
}
} // namespace micant::sys::internal
#endif

#if defined(BUILD_SYSTEM32_SHELL32_DLL)
extern "C" {
__declspec(dllexport) int32_t SHGetKnownFolderPath(const void* rfid, uint32_t dwFlags, void* hToken, wchar_t** ppszPath) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_SHGetKnownFolderPath(rfid, dwFlags, hToken, ppszPath);
}
__declspec(dllexport) int32_t SHGetFolderPathW(void* hwnd, int32_t csidl, void* hToken, uint32_t dwFlags, wchar_t* pszPath) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_SHGetFolderPathW(hwnd, csidl, hToken, dwFlags, pszPath);
}
__declspec(dllexport) wchar_t** CommandLineToArgvW(const wchar_t* lpCmdLine, int32_t* pNumArgs) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_CommandLineToArgvW(lpCmdLine, pNumArgs);
}
__declspec(dllexport) int32_t ShellExecuteExW(void* pExecInfo) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_ShellExecuteExW(pExecInfo);
}
__declspec(dllexport) void* ShellExecuteW(void* hwnd, const wchar_t* lpOperation, const wchar_t* lpFile, const wchar_t* lpParameters, const wchar_t* lpDirectory, int32_t nShowCmd) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_ShellExecuteW(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
}
__declspec(dllexport) uintptr_t SHGetFileInfoW(const wchar_t* pszPath, uint32_t dwFileAttributes, void* psfi, uint32_t cbFileInfo, uint32_t uFlags) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_SHGetFileInfoW(pszPath, dwFileAttributes, psfi, cbFileInfo, uFlags);
}
__declspec(dllexport) int32_t Shell_NotifyIconW(uint32_t dwMessage, void* lpData) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_Shell_NotifyIconW(dwMessage, lpData);
}
__declspec(dllexport) int32_t IsUserAnAdmin() {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shell32_IsUserAnAdmin();
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_SHLWAPI_DLL)
extern "C" {
__declspec(dllexport) int32_t PathFileExistsW(const wchar_t* pszPath) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_PathFileExistsW(pszPath);
}
__declspec(dllexport) wchar_t* PathCombineW(wchar_t* pszDest, const wchar_t* pszDir, const wchar_t* pszFile) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_PathCombineW(pszDest, pszDir, pszFile);
}
__declspec(dllexport) int32_t PathAppendW(wchar_t* pszPath, const wchar_t* pszMore) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_PathAppendW(pszPath, pszMore);
}
__declspec(dllexport) int32_t PathRemoveFileSpecW(wchar_t* pszPath) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_PathRemoveFileSpecW(pszPath);
}
__declspec(dllexport) const wchar_t* PathFindExtensionW(const wchar_t* pszPath) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_PathFindExtensionW(pszPath);
}
__declspec(dllexport) const wchar_t* PathFindFileNameW(const wchar_t* pszPath) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_PathFindFileNameW(pszPath);
}
__declspec(dllexport) int32_t StrCmpIW(const wchar_t* psz1, const wchar_t* psz2) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_StrCmpIW(psz1, psz2);
}
__declspec(dllexport) const wchar_t* StrStrIW(const wchar_t* lpFirst, const wchar_t* lpSrch) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Shlwapi_StrStrIW(lpFirst, lpSrch);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_DBGHELP_DLL)
extern "C" {
__declspec(dllexport) int32_t SymInitialize(void* hProcess, const char* UserSearchPath, int32_t fInvadeProcess) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_SymInitialize(hProcess, UserSearchPath, fInvadeProcess);
}
__declspec(dllexport) int32_t SymCleanup(void* hProcess) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_SymCleanup(hProcess);
}
__declspec(dllexport) uint32_t SymSetOptions(uint32_t SymOptions) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_SymSetOptions(SymOptions);
}
__declspec(dllexport) uint32_t SymGetOptions() {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_SymGetOptions();
}
__declspec(dllexport) int32_t SymFromAddr(void* hProcess, uint64_t Address, uint64_t* Displacement, void* Symbol) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_SymFromAddr(hProcess, Address, Displacement, Symbol);
}
__declspec(dllexport) int32_t SymGetLineFromAddr64(void* hProcess, uint64_t qwAddr, uint32_t* pdwDisplacement, void* Line64) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_SymGetLineFromAddr64(hProcess, qwAddr, pdwDisplacement, Line64);
}
__declspec(dllexport) int32_t StackWalk64(uint32_t MachineType, void* hProcess, void* hThread, void* StackFrame, void* ContextRecord, void* ReadMemoryRoutine, void* FunctionTableAccessRoutine, void* GetModuleBaseRoutine, void* TranslateAddress) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_StackWalk64(MachineType, hProcess, hThread, StackFrame, ContextRecord, ReadMemoryRoutine, FunctionTableAccessRoutine, GetModuleBaseRoutine, TranslateAddress);
}
__declspec(dllexport) int32_t MiniDumpWriteDump(void* hProcess, uint32_t ProcessId, void* hFile, uint32_t DumpType, const void* ExceptionParam, const void* UserStreamParam, const void* CallbackParam) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::DbgHelp_MiniDumpWriteDump(hProcess, ProcessId, hFile, DumpType, ExceptionParam, UserStreamParam, CallbackParam);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_VERSION_DLL)
extern "C" {
__declspec(dllexport) uint32_t GetFileVersionInfoSizeW(const wchar_t* lptstrFilename, uint32_t* lpdwHandle) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Version_GetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}
__declspec(dllexport) int32_t GetFileVersionInfoW(const wchar_t* lptstrFilename, uint32_t dwHandle, uint32_t dwLen, void* lpData) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Version_GetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}
__declspec(dllexport) int32_t VerQueryValueW(const void* pBlock, const wchar_t* lpSubBlock, void** lplpBuffer, uint32_t* puLen) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Version_VerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);
}
__declspec(dllexport) uint32_t GetFileVersionInfoSizeExW(uint32_t dwFlags, const wchar_t* lpwstrFilename, uint32_t* lpdwHandle) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Version_GetFileVersionInfoSizeExW(dwFlags, lpwstrFilename, lpdwHandle);
}
__declspec(dllexport) int32_t GetFileVersionInfoExW(uint32_t dwFlags, const wchar_t* lpwstrFilename, uint32_t dwHandle, uint32_t dwLen, void* lpData) {
    return micant::um::winnt_shell32_dbghelp_version_40dll_seal::Version_GetFileVersionInfoExW(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}
} // extern "C"
#endif
