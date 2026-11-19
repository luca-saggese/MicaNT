#pragma once
/**
 * @file winnt_shell32_dbghelp_version_40dll_iat_seal.hpp
 * @brief Horizon 115 (Kernel Unit #87): Sovereign WinNT Core User-Mode System32 DLLs
 *        (shell32.dll, shlwapi.dll, dbghelp.dll & version.dll) Official PE32+ Exports,
 *        Standard PE32+ .exe Import Address Table (IMAGE_IMPORT_DESCRIPTOR / IAT) Snapper,
 *        and Complete 40-DLL System32 Ecosystem Seal (20 WinNT Core + 20 PrismX DirectX).
 */

#include <cstdint>
#include <cstring>

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/um/subsystems/winbase/winnt_kernel32_advapi32_system32_abi.hpp"
#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#include "micant/um/subsystems/winuser/winnt_ole32_dwmapi_winmm_system32_seal.hpp"
#include "micant/um/subsystems/winbase/winnt_ntdll_ws2_iphlpapi_winhttp_abi.hpp"
#include "micant/um/subsystems/winbase/winnt_bcrypt_crypt32_secur32_wintrust_abi.hpp"

namespace micant::um::winnt_shell32_dbghelp_version_40dll_seal {

inline constexpr uint32_t SYMOPT_UNDNAME_OFFICIAL    = 0x00000002U;
inline constexpr uint32_t SYMOPT_LOAD_LINES_OFFICIAL = 0x00000010U;

struct WinNtFixedFileInfo {
    uint32_t dwSignature{0xFEEF04BD};
    uint32_t dwStrucVersion{0x00010000};
    uint32_t dwFileVersionMS{0x000A0000}; // 10.0
    uint32_t dwFileVersionLS{0x65F40000}; // 26100.0
    uint32_t dwProductVersionMS{0x000A0000};
    uint32_t dwProductVersionLS{0x65F40000};
    uint32_t dwFileFlagsMask{0x3F};
    uint32_t dwFileFlags{0};
    uint32_t dwFileOS{0x00040004};        // VOS_NT_WINDOWS32
    uint32_t dwFileType{0x00000002};      // VFT_DLL
    uint32_t dwFileSubtype{0};
    uint32_t dwFileDateMS{0};
    uint32_t dwFileDateLS{0};
};

// Standard PE32+ Import Descriptor & 64-bit Thunk Data (.idata)
struct WinNtImageImportByName {
    uint16_t Hint{0};
    char     Name[64]{};
};

struct WinNtImageThunkData64 {
    union {
        uint64_t ForwarderString;
        uint64_t Function;
        uint64_t Ordinal;
        const WinNtImageImportByName* AddressOfData;
    } u1{};
};

struct WinNtImageImportDescriptor {
    const char*                  DllName{nullptr};
    const WinNtImageThunkData64* OriginalFirstThunk{nullptr}; // INT
    uint32_t                     TimeDateStamp{0};
    uint32_t                     ForwarderChain{0};
    WinNtImageThunkData64*       FirstThunk{nullptr};         // IAT
};

// --- 1. shell32.dll official exports (8 functions) ---
int32_t   Shell32_SHGetKnownFolderPath(const void* rfid, uint32_t dwFlags, void* hToken, wchar_t** ppszPath) noexcept;
int32_t   Shell32_SHGetFolderPathW(void* hwnd, int32_t csidl, void* hToken, uint32_t dwFlags, wchar_t* pszPath) noexcept;
wchar_t** Shell32_CommandLineToArgvW(const wchar_t* lpCmdLine, int32_t* pNumArgs) noexcept;
int32_t   Shell32_ShellExecuteExW(void* pExecInfo) noexcept;
void*     Shell32_ShellExecuteW(void* hwnd, const wchar_t* lpOperation, const wchar_t* lpFile, const wchar_t* lpParameters, const wchar_t* lpDirectory, int32_t nShowCmd) noexcept;
uintptr_t Shell32_SHGetFileInfoW(const wchar_t* pszPath, uint32_t dwFileAttributes, void* psfi, uint32_t cbFileInfo, uint32_t uFlags) noexcept;
int32_t   Shell32_Shell_NotifyIconW(uint32_t dwMessage, void* lpData) noexcept;
int32_t   Shell32_IsUserAnAdmin() noexcept;

// --- 2. shlwapi.dll official exports (8 functions) ---
int32_t        Shlwapi_PathFileExistsW(const wchar_t* pszPath) noexcept;
wchar_t*       Shlwapi_PathCombineW(wchar_t* pszDest, const wchar_t* pszDir, const wchar_t* pszFile) noexcept;
int32_t        Shlwapi_PathAppendW(wchar_t* pszPath, const wchar_t* pszMore) noexcept;
int32_t        Shlwapi_PathRemoveFileSpecW(wchar_t* pszPath) noexcept;
const wchar_t* Shlwapi_PathFindExtensionW(const wchar_t* pszPath) noexcept;
const wchar_t* Shlwapi_PathFindFileNameW(const wchar_t* pszPath) noexcept;
int32_t        Shlwapi_StrCmpIW(const wchar_t* psz1, const wchar_t* psz2) noexcept;
const wchar_t* Shlwapi_StrStrIW(const wchar_t* lpFirst, const wchar_t* lpSrch) noexcept;

// --- 3. dbghelp.dll official exports (8 functions) ---
int32_t  DbgHelp_SymInitialize(void* hProcess, const char* UserSearchPath, int32_t fInvadeProcess) noexcept;
int32_t  DbgHelp_SymCleanup(void* hProcess) noexcept;
uint32_t DbgHelp_SymSetOptions(uint32_t SymOptions) noexcept;
uint32_t DbgHelp_SymGetOptions() noexcept;
int32_t  DbgHelp_SymFromAddr(void* hProcess, uint64_t Address, uint64_t* Displacement, void* Symbol) noexcept;
int32_t  DbgHelp_SymGetLineFromAddr64(void* hProcess, uint64_t qwAddr, uint32_t* pdwDisplacement, void* Line64) noexcept;
int32_t  DbgHelp_StackWalk64(uint32_t MachineType, void* hProcess, void* hThread, void* StackFrame, void* ContextRecord, void* ReadMemoryRoutine, void* FunctionTableAccessRoutine, void* GetModuleBaseRoutine, void* TranslateAddress) noexcept;
int32_t  DbgHelp_MiniDumpWriteDump(void* hProcess, uint32_t ProcessId, void* hFile, uint32_t DumpType, const void* ExceptionParam, const void* UserStreamParam, const void* CallbackParam) noexcept;

// --- 4. version.dll official exports (5 functions) ---
uint32_t Version_GetFileVersionInfoSizeW(const wchar_t* lptstrFilename, uint32_t* lpdwHandle) noexcept;
int32_t  Version_GetFileVersionInfoW(const wchar_t* lptstrFilename, uint32_t dwHandle, uint32_t dwLen, void* lpData) noexcept;
int32_t  Version_VerQueryValueW(const void* pBlock, const wchar_t* lpSubBlock, void** lplpBuffer, uint32_t* puLen) noexcept;
uint32_t Version_GetFileVersionInfoSizeExW(uint32_t dwFlags, const wchar_t* lpwstrFilename, uint32_t* lpdwHandle) noexcept;
int32_t  Version_GetFileVersionInfoExW(uint32_t dwFlags, const wchar_t* lpwstrFilename, uint32_t dwHandle, uint32_t dwLen, void* lpData) noexcept;

// --- 5. Standard PE32+ .exe Import Address Table (IAT) Snapper ---
[[nodiscard]] void* ResolveWinNtCoreSystem32Export(const char* dllName, const char* funcName) noexcept;
[[nodiscard]] bool  SnapPe32PlusImportDescriptorTable(const WinNtImageImportDescriptor* descriptors, size_t count, uint32_t* outSnappedFunctions) noexcept;

struct Horizon115System32Complete40DllIatSealReport {
    uint32_t shell32ExportCount{0};
    uint32_t shlwapiExportCount{0};
    uint32_t dbghelpExportCount{0};
    uint32_t versionExportCount{0};
    uint32_t winntCoreSystem32DllCount{0};     // 20 Core WinNT System32 DLLs
    uint32_t prismxDirectXSystem32DllCount{0}; // 20 DirectX/Multimedia System32 DLLs
    uint32_t totalSystem32PeDllCount{0};       // 40 Total System32 PE32+ DLLs
    uint32_t totalWinNtCoreExports{0};         // Kernel-owned and validated PE exports
    uint32_t snappedPe32PlusIatEntries{0};
    uint32_t totalHorizons{115};
    uint32_t totalKernelUnits{87};
    uint32_t totalTestSuites{331};
    uint32_t prismxStandaloneSuites{41};
    bool     shellKnownFolderAndPathVerified{false};
    bool     dbghelpSymbolAndVersionVerified{false};
    bool     pe32PlusExecutableIatSnappedAndExecuted{false};
    bool     all40System32DllsSealed{false};
};

class WinNtShell32DbgHelpVersion40DllSealCoordinator {
public:
    static WinNtShell32DbgHelpVersion40DllSealCoordinator& instance() noexcept {
        static WinNtShell32DbgHelpVersion40DllSealCoordinator s_inst;
        return s_inst;
    }

    void initialize() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    [[nodiscard]] Horizon115System32Complete40DllIatSealReport runComplete40DllIatSealAudit() noexcept;

private:
    WinNtShell32DbgHelpVersion40DllSealCoordinator() = default;
    bool m_initialized{false};
    Horizon115System32Complete40DllIatSealReport m_cachedReport{};
};

void InitializeWinNtShell32DbgHelpVersion40DllSealSubsystem() noexcept;

} // namespace micant::um::winnt_shell32_dbghelp_version_40dll_seal
