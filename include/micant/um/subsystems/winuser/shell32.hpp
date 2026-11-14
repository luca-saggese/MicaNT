// ============================================================================
// MicaNT: Win32 Shell API Subsystem (shell32.dll & shlwapi.dll)
//
// Modern Clean-Room Implementation in pure ISO C++23.
// Provides Windows Shell Folders, CSIDL & KNOWNFOLDERID namespaces,
// ShellExecute process activation, CommandLineToArgvW tokenization,
// System Tray Notification Icons (Shell_NotifyIcon), and Lightweight Path APIs.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cstdarg>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winuser/user32.hpp"
#include "micant/km/drivers/fs/fs.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::shell32 {

// ============================================================================
// 1. Data Types & Shell Constants
// ============================================================================

using HRESULT = int32_t;
using HWND    = win32::HWND;
using HICON   = win32::HICON;
using HINSTANCE = win32::HINSTANCE;
using HKEY    = win32::HKEY;
using DWORD   = win32::DWORD;
using BOOL    = win32::BOOL;
using UINT    = win32::UINT;
using WORD    = win32::WORD;
using ULONG   = uint32_t;
using DWORD_PTR = uintptr_t;
using ULONG_PTR = uintptr_t;
using LPCWSTR = const wchar_t*;
using LPCSTR  = const char*;
using HANDLE  = win32::HANDLE;
using LPWSTR  = wchar_t*;
using LPSTR   = char*;
using PCWSTR  = const wchar_t*;
using PCSTR   = const char*;
using PWSTR   = wchar_t*;
using PSTR    = char*;

inline constexpr HRESULT S_OK    = 0;
inline constexpr HRESULT S_FALSE = 1;
inline constexpr HRESULT E_FAIL  = static_cast<HRESULT>(0x80004005);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057);

// CSIDL (Constant Special Item ID List) Values
inline constexpr int CSIDL_DESKTOP                 = 0x0000;
inline constexpr int CSIDL_INTERNET                = 0x0001;
inline constexpr int CSIDL_PROGRAMS                = 0x0002;
inline constexpr int CSIDL_CONTROLS                = 0x0003;
inline constexpr int CSIDL_PRINTERS                = 0x0004;
inline constexpr int CSIDL_PERSONAL                = 0x0005; // Documents
inline constexpr int CSIDL_FAVORITES               = 0x0006;
inline constexpr int CSIDL_STARTUP                 = 0x0007;
inline constexpr int CSIDL_RECENT                  = 0x0008;
inline constexpr int CSIDL_SENDTO                  = 0x0009;
inline constexpr int CSIDL_BITBUCKET               = 0x000A; // Recycle Bin
inline constexpr int CSIDL_STARTMENU               = 0x000B;
inline constexpr int CSIDL_MYDOCUMENTS             = CSIDL_PERSONAL;
inline constexpr int CSIDL_MYMUSIC                 = 0x000D;
inline constexpr int CSIDL_MYVIDEO                 = 0x000E;
inline constexpr int CSIDL_DESKTOPDIRECTORY        = 0x0010;
inline constexpr int CSIDL_DRIVES                  = 0x0011;
inline constexpr int CSIDL_NETWORK                 = 0x0012;
inline constexpr int CSIDL_NETHOOD                 = 0x0013;
inline constexpr int CSIDL_FONTS                   = 0x0014;
inline constexpr int CSIDL_TEMPLATES               = 0x0015;
inline constexpr int CSIDL_COMMON_STARTMENU        = 0x0016;
inline constexpr int CSIDL_COMMON_PROGRAMS         = 0x0017;
inline constexpr int CSIDL_COMMON_STARTUP          = 0x0018;
inline constexpr int CSIDL_COMMON_DESKTOPDIRECTORY = 0x0019;
inline constexpr int CSIDL_APPDATA                 = 0x001A;
inline constexpr int CSIDL_PRINTHOOD               = 0x001B;
inline constexpr int CSIDL_LOCAL_APPDATA           = 0x001C;
inline constexpr int CSIDL_COMMON_FAVORITES        = 0x001F;
inline constexpr int CSIDL_INTERNET_CACHE          = 0x0020;
inline constexpr int CSIDL_COOKIES                 = 0x0021;
inline constexpr int CSIDL_HISTORY                 = 0x0022;
inline constexpr int CSIDL_COMMON_APPDATA          = 0x0023;
inline constexpr int CSIDL_WINDOWS                 = 0x0024;
inline constexpr int CSIDL_SYSTEM                  = 0x0025;
inline constexpr int CSIDL_PROGRAM_FILES           = 0x0026;
inline constexpr int CSIDL_MYPICTURES              = 0x0027;
inline constexpr int CSIDL_PROFILE                 = 0x0028;
inline constexpr int CSIDL_SYSTEMX86               = 0x0029;
inline constexpr int CSIDL_PROGRAM_FILESX86        = 0x002A;
inline constexpr int CSIDL_PROGRAM_FILES_COMMON    = 0x002B;
inline constexpr int CSIDL_PROGRAM_FILES_COMMONX86 = 0x002C;
inline constexpr int CSIDL_COMMON_TEMPLATES        = 0x002D;
inline constexpr int CSIDL_COMMON_DOCUMENTS        = 0x002E;
inline constexpr int CSIDL_COMMON_ADMINTOOLS       = 0x002F;
inline constexpr int CSIDL_ADMINTOOLS              = 0x0030;

inline constexpr int CSIDL_FLAG_CREATE             = 0x8000;
inline constexpr int CSIDL_FLAG_DONT_VERIFY        = 0x4000;
inline constexpr int CSIDL_FLAG_MASK               = 0xFF00;

// Known Folder GUIDs (KNOWNFOLDERID)
inline const micant::GUID FOLDERID_Desktop = {
    0xB4BFCC3A, 0xDB2C, 0x424C, { 0xB0, 0x29, 0x7F, 0xE9, 0x9A, 0x87, 0xC6, 0x41 }
};
inline const micant::GUID FOLDERID_Documents = {
    0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 }
};
inline const micant::GUID FOLDERID_Downloads = {
    0x374DE290, 0x123F, 0x4565, { 0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B }
};
inline const micant::GUID FOLDERID_Music = {
    0x4BD8D570, 0x509E, 0x49E5, { 0xAB, 0x78, 0x02, 0x54, 0x33, 0x29, 0xB2, 0xD0 }
};
inline const micant::GUID FOLDERID_Pictures = {
    0x33E28130, 0x4E1E, 0x4676, { 0x83, 0x5A, 0x98, 0x39, 0x5C, 0x3B, 0xC3, 0xBB }
};
inline const micant::GUID FOLDERID_Videos = {
    0x1898EB52, 0x7147, 0x49FA, { 0x9F, 0x50, 0x74, 0x5D, 0x00, 0xE5, 0xDF, 0x0C }
};
inline const micant::GUID FOLDERID_ProgramFiles = {
    0x905E63B6, 0xC1BF, 0x494E, { 0xB2, 0x9C, 0x65, 0xB7, 0x32, 0xD3, 0xD2, 0x1A }
};
inline const micant::GUID FOLDERID_ProgramFilesX86 = {
    0x7C5A40EF, 0xA0FB, 0x4BFC, { 0x87, 0x4A, 0xC0, 0xF2, 0xE0, 0xB9, 0xFA, 0x8E }
};
inline const micant::GUID FOLDERID_Windows = {
    0xF38BF404, 0x1D43, 0x42F2, { 0x93, 0x05, 0x67, 0xDE, 0x0B, 0x28, 0xFC, 0x23 }
};
inline const micant::GUID FOLDERID_System = {
    0x1AC14E77, 0x02E7, 0x4E5D, { 0xB7, 0x44, 0x2E, 0xB1, 0xAE, 0x51, 0x98, 0xB7 }
};
inline const micant::GUID FOLDERID_Profile = {
    0x5E6C858F, 0x0E22, 0x4760, { 0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73 }
};
inline const micant::GUID FOLDERID_LocalAppData = {
    0xF1B32785, 0x6FBA, 0x4FCF, { 0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91 }
};
inline const micant::GUID FOLDERID_RoamingAppData = {
    0x3EB685FD, 0x984F, 0x4E40, { 0xB0, 0x96, 0x18, 0x09, 0xC7, 0x15, 0xF5, 0xF8 }
};

using REFKNOWNFOLDERID = const micant::GUID&;

// Known Folder Flags
inline constexpr DWORD KF_FLAG_DEFAULT         = 0x00000000;
inline constexpr DWORD KF_FLAG_CREATE          = 0x00008000;
inline constexpr DWORD KF_FLAG_DONT_VERIFY     = 0x00004000;
inline constexpr DWORD KF_FLAG_DONT_UNEXPAND   = 0x00002000;
inline constexpr DWORD KF_FLAG_NO_ALIAS        = 0x00001000;
inline constexpr DWORD KF_FLAG_INIT            = 0x00000800;
inline constexpr DWORD KF_FLAG_DEFAULT_PATH    = 0x00000400;

// ShellExecute Mask Flags
inline constexpr ULONG SEE_MASK_DEFAULT        = 0x00000000;
inline constexpr ULONG SEE_MASK_CLASSNAME      = 0x00000001;
inline constexpr ULONG SEE_MASK_CLASSKEY       = 0x00000003;
inline constexpr ULONG SEE_MASK_IDLIST         = 0x00000004;
inline constexpr ULONG SEE_MASK_INVOKEIDLIST   = 0x0000000C;
inline constexpr ULONG SEE_MASK_ICON           = 0x00000010;
inline constexpr ULONG SEE_MASK_HOTKEY         = 0x00000020;
inline constexpr ULONG SEE_MASK_NOCLOSEPROCESS = 0x00000040;
inline constexpr ULONG SEE_MASK_CONNECTNETDRV  = 0x00000080;
inline constexpr ULONG SEE_MASK_NOASYNC        = 0x00000100;
inline constexpr ULONG SEE_MASK_FLAG_NO_UI     = 0x00000400;
inline constexpr ULONG SEE_MASK_UNICODE        = 0x00004000;
inline constexpr ULONG SEE_MASK_NO_CONSOLE     = 0x00008000;

// ShellExecute Structures
struct SHELLEXECUTEINFOW {
    DWORD     cbSize{sizeof(SHELLEXECUTEINFOW)};
    ULONG     fMask{0};
    HWND      hwnd{nullptr};
    LPCWSTR   lpVerb{nullptr};
    LPCWSTR   lpFile{nullptr};
    LPCWSTR   lpParameters{nullptr};
    LPCWSTR   lpDirectory{nullptr};
    int       nShow{1}; // SW_SHOWNORMAL
    HINSTANCE hInstApp{nullptr};
    void*     lpIDList{nullptr};
    LPCWSTR   lpClass{nullptr};
    HKEY      hkeyClass{nullptr};
    DWORD     dwHotKey{0};
    union {
        win32::HANDLE hIcon{nullptr};
        win32::HANDLE hMonitor;
    };
    win32::HANDLE hProcess{nullptr};
};

struct SHELLEXECUTEINFOA {
    DWORD     cbSize{sizeof(SHELLEXECUTEINFOA)};
    ULONG     fMask{0};
    HWND      hwnd{nullptr};
    LPCSTR    lpVerb{nullptr};
    LPCSTR    lpFile{nullptr};
    LPCSTR    lpParameters{nullptr};
    LPCSTR    lpDirectory{nullptr};
    int       nShow{1};
    HINSTANCE hInstApp{nullptr};
    void*     lpIDList{nullptr};
    LPCSTR    lpClass{nullptr};
    HKEY      hkeyClass{nullptr};
    DWORD     dwHotKey{0};
    union {
        win32::HANDLE hIcon{nullptr};
        win32::HANDLE hMonitor;
    };
    win32::HANDLE hProcess{nullptr};
};

// System Tray Notification Icon Messages & Flags
inline constexpr DWORD NIM_ADD        = 0x00000000;
inline constexpr DWORD NIM_MODIFY     = 0x00000001;
inline constexpr DWORD NIM_DELETE     = 0x00000002;
inline constexpr DWORD NIM_SETFOCUS   = 0x00000003;
inline constexpr DWORD NIM_SETVERSION = 0x00000004;

inline constexpr UINT NIF_MESSAGE  = 0x00000001;
inline constexpr UINT NIF_ICON     = 0x00000002;
inline constexpr UINT NIF_TIP      = 0x00000004;
inline constexpr UINT NIF_STATE    = 0x00000008;
inline constexpr UINT NIF_INFO     = 0x00000010;
inline constexpr UINT NIF_GUID     = 0x00000020;
inline constexpr UINT NIF_REALTIME = 0x00000040;
inline constexpr UINT NIF_SHOWTIP  = 0x00000080;

struct NOTIFYICONDATAW {
    DWORD        cbSize{sizeof(NOTIFYICONDATAW)};
    HWND         hWnd{nullptr};
    UINT         uID{0};
    UINT         uFlags{0};
    UINT         uCallbackMessage{0};
    HICON        hIcon{nullptr};
    wchar_t      szTip[128]{};
    DWORD        dwState{0};
    DWORD        dwStateMask{0};
    wchar_t      szInfo[256]{};
    union {
        UINT     uTimeout{0};
        UINT     uVersion;
    };
    wchar_t      szInfoTitle[64]{};
    DWORD        dwInfoFlags{0};
    micant::GUID guidItem{};
    HICON        hBalloonIcon{nullptr};
};

struct NOTIFYICONDATAA {
    DWORD        cbSize{sizeof(NOTIFYICONDATAA)};
    HWND         hWnd{nullptr};
    UINT         uID{0};
    UINT         uFlags{0};
    UINT         uCallbackMessage{0};
    HICON        hIcon{nullptr};
    char         szTip[128]{};
    DWORD        dwState{0};
    DWORD        dwStateMask{0};
    char         szInfo[256]{};
    union {
        UINT     uTimeout{0};
        UINT     uVersion;
    };
    char         szInfoTitle[64]{};
    DWORD        dwInfoFlags{0};
    micant::GUID guidItem{};
    HICON        hBalloonIcon{nullptr};
};

// Shell File Information Flags & Structures
inline constexpr UINT SHGFI_ICON              = 0x000000100;
inline constexpr UINT SHGFI_DISPLAYNAME       = 0x000000200;
inline constexpr UINT SHGFI_TYPENAME          = 0x000000400;
inline constexpr UINT SHGFI_ATTRIBUTES        = 0x000000800;
inline constexpr UINT SHGFI_ICONLOCATION      = 0x000001000;
inline constexpr UINT SHGFI_EXETYPE           = 0x000002000;
inline constexpr UINT SHGFI_SYSICONINDEX      = 0x000004000;
inline constexpr UINT SHGFI_LINKOVERLAY       = 0x000008000;
inline constexpr UINT SHGFI_SELECTED          = 0x000010000;
inline constexpr UINT SHGFI_ATTR_SPECIFIED    = 0x000020000;
inline constexpr UINT SHGFI_LARGEICON         = 0x000000000;
inline constexpr UINT SHGFI_SMALLICON         = 0x000000001;
inline constexpr UINT SHGFI_OPENICON          = 0x000000002;
inline constexpr UINT SHGFI_SHELLICONSIZE     = 0x000000004;
inline constexpr UINT SHGFI_PIDL              = 0x000000008;
inline constexpr UINT SHGFI_USEFILEATTRIBUTES = 0x000000010;

struct SHFILEINFOW {
    HICON   hIcon{nullptr};
    int     iIcon{0};
    DWORD   dwAttributes{0};
    wchar_t szDisplayName[260]{};
    wchar_t szTypeName[80]{};
};

struct SHFILEINFOA {
    HICON   hIcon{nullptr};
    int     iIcon{0};
    DWORD   dwAttributes{0};
    char    szDisplayName[260]{};
    char    szTypeName[80]{};
};

// ============================================================================
// 2. Shell Lightweight Path APIs (shlwapi.dll)
// ============================================================================

inline BOOL PathFileExistsW(LPCWSTR pszPath) noexcept {
    if (!pszPath || !*pszPath) return win32::FALSE;
    return (kernel32::GetFileAttributesW(pszPath) != win32::INVALID_FILE_ATTRIBUTES) ? win32::TRUE : win32::FALSE;
}

inline BOOL PathFileExistsA(LPCSTR pszPath) noexcept {
    if (!pszPath || !*pszPath) return win32::FALSE;
    return (kernel32::GetFileAttributesA(pszPath) != win32::INVALID_FILE_ATTRIBUTES) ? win32::TRUE : win32::FALSE;
}

inline LPWSTR PathCombineW(LPWSTR pszDest, LPCWSTR pszDir, LPCWSTR pszFile) noexcept {
    if (!pszDest) return nullptr;
    std::wstring combined;
    if (pszDir && *pszDir) {
        combined = pszDir;
        if (!combined.ends_with(L'\\') && !combined.ends_with(L'/')) {
            combined.push_back(L'\\');
        }
    }
    if (pszFile && *pszFile) {
        if (*pszFile == L'\\' || *pszFile == L'/') {
            combined = pszFile;
        } else {
            combined += pszFile;
        }
    }
    std::wcsncpy(pszDest, combined.c_str(), 260);
    pszDest[259] = L'\0';
    return pszDest;
}

inline LPSTR PathCombineA(LPSTR pszDest, LPCSTR pszDir, LPCSTR pszFile) noexcept {
    if (!pszDest) return nullptr;
    std::string combined;
    if (pszDir && *pszDir) {
        combined = pszDir;
        if (!combined.ends_with('\\') && !combined.ends_with('/')) {
            combined.push_back('\\');
        }
    }
    if (pszFile && *pszFile) {
        if (*pszFile == '\\' || *pszFile == '/') {
            combined = pszFile;
        } else {
            combined += pszFile;
        }
    }
    std::strncpy(pszDest, combined.c_str(), 260);
    pszDest[259] = '\0';
    return pszDest;
}

inline LPCWSTR PathFindFileNameW(LPCWSTR pszPath) noexcept {
    if (!pszPath) return nullptr;
    const wchar_t* last = pszPath;
    for (const wchar_t* p = pszPath; *p; ++p) {
        if (*p == L'\\' || *p == L'/' || *p == L':') {
            last = p + 1;
        }
    }
    return last;
}

inline LPCSTR PathFindFileNameA(LPCSTR pszPath) noexcept {
    if (!pszPath) return nullptr;
    const char* last = pszPath;
    for (const char* p = pszPath; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':') {
            last = p + 1;
        }
    }
    return last;
}

inline LPCWSTR PathFindExtensionW(LPCWSTR pszPath) noexcept {
    if (!pszPath) return nullptr;
    const wchar_t* dot = nullptr;
    for (const wchar_t* p = pszPath; *p; ++p) {
        if (*p == L'.') {
            dot = p;
        } else if (*p == L'\\' || *p == L'/' || *p == L':') {
            dot = nullptr;
        }
    }
    return dot ? dot : (pszPath + std::wcslen(pszPath));
}

inline LPCSTR PathFindExtensionA(LPCSTR pszPath) noexcept {
    if (!pszPath) return nullptr;
    const char* dot = nullptr;
    for (const char* p = pszPath; *p; ++p) {
        if (*p == '.') {
            dot = p;
        } else if (*p == '\\' || *p == '/' || *p == ':') {
            dot = nullptr;
        }
    }
    return dot ? dot : (pszPath + std::strlen(pszPath));
}

inline BOOL PathRemoveFileSpecW(LPWSTR pszPath) noexcept {
    if (!pszPath || !*pszPath) return win32::FALSE;
    wchar_t* last = nullptr;
    for (wchar_t* p = pszPath; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            last = p;
        }
    }
    if (last) {
        if (last == pszPath) {
            *(last + 1) = L'\0';
        } else {
            *last = L'\0';
        }
        return win32::TRUE;
    }
    return win32::FALSE;
}

inline BOOL PathRemoveFileSpecA(LPSTR pszPath) noexcept {
    if (!pszPath || !*pszPath) return win32::FALSE;
    char* last = nullptr;
    for (char* p = pszPath; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            last = p;
        }
    }
    if (last) {
        if (last == pszPath) {
            *(last + 1) = '\0';
        } else {
            *last = '\0';
        }
        return win32::TRUE;
    }
    return win32::FALSE;
}

inline LPWSTR PathAddBackslashW(LPWSTR pszPath) noexcept {
    if (!pszPath) return nullptr;
    size_t len = std::wcslen(pszPath);
    if (len > 0 && pszPath[len - 1] != L'\\' && pszPath[len - 1] != L'/' && len + 1 < 260) {
        pszPath[len] = L'\\';
        pszPath[len + 1] = L'\0';
        return pszPath + len + 1;
    }
    return pszPath + len;
}

inline LPSTR PathAddBackslashA(LPSTR pszPath) noexcept {
    if (!pszPath) return nullptr;
    size_t len = std::strlen(pszPath);
    if (len > 0 && pszPath[len - 1] != '\\' && pszPath[len - 1] != '/' && len + 1 < 260) {
        pszPath[len] = '\\';
        pszPath[len + 1] = '\0';
        return pszPath + len + 1;
    }
    return pszPath + len;
}

inline BOOL PathIsRelativeW(LPCWSTR pszPath) noexcept {
    if (!pszPath || !*pszPath) return win32::TRUE;
    if (pszPath[0] == L'\\' || pszPath[0] == L'/') return win32::FALSE;
    if (std::iswalpha(pszPath[0]) && pszPath[1] == L':') return win32::FALSE;
    return win32::TRUE;
}

inline BOOL PathIsRelativeA(LPCSTR pszPath) noexcept {
    if (!pszPath || !*pszPath) return win32::TRUE;
    if (pszPath[0] == '\\' || pszPath[0] == '/') return win32::FALSE;
    if (std::isalpha(static_cast<unsigned char>(pszPath[0])) && pszPath[1] == ':') return win32::FALSE;
    return win32::TRUE;
}

inline BOOL PathIsDirectoryW(LPCWSTR pszPath) noexcept {
    DWORD attr = kernel32::GetFileAttributesW(pszPath);
    if (attr == win32::INVALID_FILE_ATTRIBUTES) return win32::FALSE;
    return (attr & win32::FILE_ATTRIBUTE_DIRECTORY) ? win32::TRUE : win32::FALSE;
}

inline BOOL PathIsDirectoryA(LPCSTR pszPath) noexcept {
    DWORD attr = kernel32::GetFileAttributesA(pszPath);
    if (attr == win32::INVALID_FILE_ATTRIBUTES) return win32::FALSE;
    return (attr & win32::FILE_ATTRIBUTE_DIRECTORY) ? win32::TRUE : win32::FALSE;
}

// Case-insensitive sub-string and string comparison
inline PCWSTR StrStrIW(PCWSTR pszFirst, PCWSTR pszSrch) noexcept {
    if (!pszFirst || !pszSrch) return nullptr;
    if (!*pszSrch) return pszFirst;
    size_t lenSrch = std::wcslen(pszSrch);
    for (const wchar_t* p = pszFirst; *p; ++p) {
        if (_wcsnicmp(p, pszSrch, lenSrch) == 0) {
            return p;
        }
    }
    return nullptr;
}

inline PCSTR StrStrIA(PCSTR pszFirst, PCSTR pszSrch) noexcept {
    if (!pszFirst || !pszSrch) return nullptr;
    if (!*pszSrch) return pszFirst;
    size_t lenSrch = std::strlen(pszSrch);
    for (const char* p = pszFirst; *p; ++p) {
        if (_strnicmp(p, pszSrch, lenSrch) == 0) {
            return p;
        }
    }
    return nullptr;
}

inline int StrCmpIW(PCWSTR psz1, PCWSTR psz2) noexcept {
    if (!psz1 && !psz2) return 0;
    if (!psz1) return -1;
    if (!psz2) return 1;
    return _wcsicmp(psz1, psz2);
}

inline int StrCmpIA(PCSTR psz1, PCSTR psz2) noexcept {
    if (!psz1 && !psz2) return 0;
    if (!psz1) return -1;
    if (!psz2) return 1;
    return _stricmp(psz1, psz2);
}

// ============================================================================
// 3. Known Folders & Shell Directory Engine
// ============================================================================

class KnownFolderManager {
private:
    std::mutex m_mutex;
    std::unordered_map<int, std::wstring> m_csidlPaths;

    KnownFolderManager() {
        // Initialize canonical MicaNT NT 10.0 userland layout
        m_csidlPaths[CSIDL_WINDOWS]                 = L"C:\\Windows";
        m_csidlPaths[CSIDL_SYSTEM]                  = L"C:\\Windows\\System32";
        m_csidlPaths[CSIDL_SYSTEMX86]               = L"C:\\Windows\\SysWOW64";
        m_csidlPaths[CSIDL_FONTS]                   = L"C:\\Windows\\Fonts";
        m_csidlPaths[CSIDL_PROGRAM_FILES]           = L"C:\\Program Files";
        m_csidlPaths[CSIDL_PROGRAM_FILESX86]        = L"C:\\Program Files (x86)";
        m_csidlPaths[CSIDL_PROGRAM_FILES_COMMON]    = L"C:\\Program Files\\Common Files";
        m_csidlPaths[CSIDL_PROGRAM_FILES_COMMONX86] = L"C:\\Program Files (x86)\\Common Files";
        m_csidlPaths[CSIDL_COMMON_APPDATA]          = L"C:\\ProgramData";
        m_csidlPaths[CSIDL_PROFILE]                 = L"C:\\Users\\admin";
        m_csidlPaths[CSIDL_DESKTOP]                 = L"C:\\Users\\admin\\Desktop";
        m_csidlPaths[CSIDL_DESKTOPDIRECTORY]        = L"C:\\Users\\admin\\Desktop";
        m_csidlPaths[CSIDL_PERSONAL]                = L"C:\\Users\\admin\\Documents";
        m_csidlPaths[CSIDL_MYDOCUMENTS]             = L"C:\\Users\\admin\\Documents";
        m_csidlPaths[CSIDL_MYMUSIC]                 = L"C:\\Users\\admin\\Music";
        m_csidlPaths[CSIDL_MYPICTURES]              = L"C:\\Users\\admin\\Pictures";
        m_csidlPaths[CSIDL_MYVIDEO]                 = L"C:\\Users\\admin\\Videos";
        m_csidlPaths[CSIDL_APPDATA]                 = L"C:\\Users\\admin\\AppData\\Roaming";
        m_csidlPaths[CSIDL_LOCAL_APPDATA]           = L"C:\\Users\\admin\\AppData\\Local";
        m_csidlPaths[CSIDL_STARTMENU]               = L"C:\\Users\\admin\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu";
        m_csidlPaths[CSIDL_PROGRAMS]                = L"C:\\Users\\admin\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs";
        m_csidlPaths[CSIDL_STARTUP]                 = L"C:\\Users\\admin\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
        m_csidlPaths[CSIDL_RECENT]                  = L"C:\\Users\\admin\\AppData\\Roaming\\Microsoft\\Windows\\Recent";
        m_csidlPaths[CSIDL_SENDTO]                  = L"C:\\Users\\admin\\AppData\\Roaming\\Microsoft\\Windows\\SendTo";
        m_csidlPaths[CSIDL_FAVORITES]               = L"C:\\Users\\admin\\Favorites";
        m_csidlPaths[CSIDL_COMMON_STARTMENU]        = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu";
        m_csidlPaths[CSIDL_COMMON_PROGRAMS]         = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
        m_csidlPaths[CSIDL_COMMON_STARTUP]          = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
        m_csidlPaths[CSIDL_COMMON_DESKTOPDIRECTORY] = L"C:\\Users\\Public\\Desktop";
        m_csidlPaths[CSIDL_COMMON_DOCUMENTS]        = L"C:\\Users\\Public\\Documents";
    }

public:
    static KnownFolderManager& get() {
        static KnownFolderManager instance;
        return instance;
    }

    HRESULT getFolderPath(int csidl, bool create, std::wstring& outPath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int folderId = csidl & ~CSIDL_FLAG_MASK;

        auto it = m_csidlPaths.find(folderId);
        if (it == m_csidlPaths.end()) {
            return E_FAIL;
        }

        outPath = it->second;

        if (create) {
            fs::VirtualFileSystem::get().createDirectory(outPath);
        }

        return S_OK;
    }

    HRESULT getKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, std::wstring& outPath) {
        bool create = (dwFlags & KF_FLAG_CREATE) != 0;

        if (rfid == FOLDERID_Desktop)            return getFolderPath(CSIDL_DESKTOP, create, outPath);
        if (rfid == FOLDERID_Documents)          return getFolderPath(CSIDL_PERSONAL, create, outPath);
        if (rfid == FOLDERID_Downloads) {
            outPath = L"C:\\Users\\admin\\Downloads";
            if (create) fs::VirtualFileSystem::get().createDirectory(outPath);
            return S_OK;
        }
        if (rfid == FOLDERID_Music)              return getFolderPath(CSIDL_MYMUSIC, create, outPath);
        if (rfid == FOLDERID_Pictures)           return getFolderPath(CSIDL_MYPICTURES, create, outPath);
        if (rfid == FOLDERID_Videos)             return getFolderPath(CSIDL_MYVIDEO, create, outPath);
        if (rfid == FOLDERID_ProgramFiles)       return getFolderPath(CSIDL_PROGRAM_FILES, create, outPath);
        if (rfid == FOLDERID_ProgramFilesX86)    return getFolderPath(CSIDL_PROGRAM_FILESX86, create, outPath);
        if (rfid == FOLDERID_Windows)            return getFolderPath(CSIDL_WINDOWS, create, outPath);
        if (rfid == FOLDERID_System)             return getFolderPath(CSIDL_SYSTEM, create, outPath);
        if (rfid == FOLDERID_Profile)            return getFolderPath(CSIDL_PROFILE, create, outPath);
        if (rfid == FOLDERID_LocalAppData)       return getFolderPath(CSIDL_LOCAL_APPDATA, create, outPath);
        if (rfid == FOLDERID_RoamingAppData)     return getFolderPath(CSIDL_APPDATA, create, outPath);

        return E_FAIL;
    }
};

inline HRESULT SHGetFolderPathW(
    HWND /*hwndOwner*/,
    int nFolder,
    win32::HANDLE /*hToken*/,
    DWORD /*dwFlags*/,
    LPWSTR pszPath
) noexcept {
    if (!pszPath) return E_INVALIDARG;
    bool create = (nFolder & CSIDL_FLAG_CREATE) != 0;
    std::wstring path;
    HRESULT hr = KnownFolderManager::get().getFolderPath(nFolder, create, path);
    if (hr == S_OK) {
        std::wcsncpy(pszPath, path.c_str(), 260);
        pszPath[259] = L'\0';
    }
    return hr;
}

inline HRESULT SHGetFolderPathA(
    HWND hwndOwner,
    int nFolder,
    win32::HANDLE hToken,
    DWORD dwFlags,
    LPSTR pszPath
) noexcept {
    if (!pszPath) return E_INVALIDARG;
    wchar_t wbuf[260]{};
    HRESULT hr = SHGetFolderPathW(hwndOwner, nFolder, hToken, dwFlags, wbuf);
    if (hr == S_OK) {
        for (int i = 0; i < 260; ++i) {
            pszPath[i] = static_cast<char>(wbuf[i] & 0x7F);
            if (wbuf[i] == L'\0') break;
        }
    }
    return hr;
}

inline BOOL SHGetSpecialFolderPathW(
    HWND hwndOwner,
    LPWSTR lpszPath,
    int csidl,
    BOOL fCreate
) noexcept {
    int flags = csidl | (fCreate ? CSIDL_FLAG_CREATE : 0);
    return (SHGetFolderPathW(hwndOwner, flags, nullptr, 0, lpszPath) == S_OK) ? win32::TRUE : win32::FALSE;
}

inline BOOL SHGetSpecialFolderPathA(
    HWND hwndOwner,
    LPSTR lpszPath,
    int csidl,
    BOOL fCreate
) noexcept {
    int flags = csidl | (fCreate ? CSIDL_FLAG_CREATE : 0);
    return (SHGetFolderPathA(hwndOwner, flags, nullptr, 0, lpszPath) == S_OK) ? win32::TRUE : win32::FALSE;
}

inline HRESULT SHGetKnownFolderPath(
    REFKNOWNFOLDERID rfid,
    DWORD dwFlags,
    win32::HANDLE /*hToken*/,
    PWSTR* ppszPath
) noexcept {
    if (!ppszPath) return E_INVALIDARG;
    *ppszPath = nullptr;

    std::wstring path;
    HRESULT hr = KnownFolderManager::get().getKnownFolderPath(rfid, dwFlags, path);
    if (hr != S_OK) return hr;

    size_t byteCount = (path.size() + 1) * sizeof(wchar_t);
    wchar_t* mem = static_cast<wchar_t*>(kernel32::LocalAlloc(kernel32::LPTR, byteCount));
    if (!mem) return static_cast<HRESULT>(0x8007000E); // E_OUTOFMEMORY

    std::memcpy(mem, path.c_str(), byteCount);
    *ppszPath = mem;
    return S_OK;
}

inline int WINAPI SHCreateDirectoryExA(HWND /*hwnd*/, const char* /*pszPath*/, void* /*psa*/) noexcept {
    return 0; // ERROR_SUCCESS
}

inline int WINAPI SHCreateDirectoryExW(HWND /*hwnd*/, const wchar_t* /*pszPath*/, void* /*psa*/) noexcept {
    return 0; // ERROR_SUCCESS
}

inline ULONG WINAPI SHChangeNotifyRegister_Ordinal2(
    HWND /*hwnd*/,
    int /*fSources*/,
    int32_t /*fEvents*/,
    uint32_t /*wMsg*/,
    int /*cItems*/,
    const void* /*pItems*/
) noexcept {
    return 1;
}

inline BOOL WINAPI SHChangeNotifyDeregister_Ordinal4(ULONG /*ulID*/) noexcept {
    return win32::TRUE;
}

inline int wnsprintfW(wchar_t* lpOut, int cchLimitIn, const wchar_t* lpFmt, ...) noexcept {
    if (!lpOut || cchLimitIn <= 0 || !lpFmt) return -1;
    std::wstring normFmt;
    for (size_t i = 0; lpFmt[i] != L'\0'; ++i) {
        if (lpFmt[i] == L'%' && lpFmt[i + 1] == L's') {
            normFmt += L"%ls";
            ++i;
        } else {
            normFmt += lpFmt[i];
        }
    }
    va_list args;
    va_start(args, lpFmt);
    int written = std::vswprintf(lpOut, static_cast<size_t>(cchLimitIn), normFmt.c_str(), args);
    va_end(args);
    return written;
}

// ============================================================================
// 4. Command Line Parser (CommandLineToArgvW)
// ============================================================================

inline LPWSTR* CommandLineToArgvW(LPCWSTR lpCmdLine, int* pNumArgs) noexcept {
    if (win32::g_TraceApi) {
        std::wcout << L"[*] [TRACE] CommandLineToArgvW: " << (lpCmdLine ? lpCmdLine : L"null") << L"\n";
    }
    if (!pNumArgs) return nullptr;
    *pNumArgs = 0;
    if (!lpCmdLine) return nullptr;

    std::vector<std::wstring> args;
    const wchar_t* p = lpCmdLine;

    while (*p) {
        while (*p && (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n')) {
            ++p;
        }
        if (!*p) break;

        std::wstring arg;
        bool inQuotes = false;

        while (*p) {
            if (*p == L' ' || *p == L'\t') {
                if (!inQuotes) break;
                arg.push_back(*p++);
            } else if (*p == L'"') {
                inQuotes = !inQuotes;
                ++p;
            } else if (*p == L'\\') {
                size_t backslashes = 0;
                while (*p == L'\\') {
                    ++backslashes;
                    ++p;
                }
                if (*p == L'"') {
                    arg.append(backslashes / 2, L'\\');
                    if (backslashes % 2 != 0) {
                        arg.push_back(L'"');
                        ++p;
                    }
                } else {
                    arg.append(backslashes, L'\\');
                }
            } else {
                arg.push_back(*p++);
            }
        }
        args.push_back(std::move(arg));
    }

    if (args.empty()) {
        *pNumArgs = 0;
        return nullptr;
    }

    size_t numArgs = args.size();
    size_t totalChars = 0;
    for (const auto& a : args) {
        totalChars += a.size() + 1;
    }

    size_t totalBytes = ((numArgs + 1) * sizeof(LPWSTR)) + (totalChars * sizeof(wchar_t));
    uint8_t* block = static_cast<uint8_t*>(kernel32::LocalAlloc(kernel32::LPTR, totalBytes));
    if (!block) return nullptr;

    LPWSTR* argv = reinterpret_cast<LPWSTR*>(block);
    wchar_t* strCursor = reinterpret_cast<wchar_t*>(block + ((numArgs + 1) * sizeof(LPWSTR)));

    for (size_t i = 0; i < numArgs; ++i) {
        argv[i] = strCursor;
        std::memcpy(strCursor, args[i].c_str(), (args[i].size() + 1) * sizeof(wchar_t));
        strCursor += args[i].size() + 1;
    }
    argv[numArgs] = nullptr;

    *pNumArgs = static_cast<int>(numArgs);
    return argv;
}

// ============================================================================
// 5. System Tray Notification Icons (Shell_NotifyIcon)
// ============================================================================

struct TrayNotificationItem {
    HWND         hWnd{nullptr};
    UINT         uID{0};
    UINT         uFlags{0};
    UINT         uCallbackMessage{0};
    HICON        hIcon{nullptr};
    std::wstring szTip;
    std::wstring szInfo;
    std::wstring szInfoTitle;
    micant::GUID guidItem{};
};

class TrayNotificationManager {
private:
    std::mutex m_mutex;
    std::vector<TrayNotificationItem> m_icons;

    TrayNotificationManager() = default;

public:
    static TrayNotificationManager& get() {
        static TrayNotificationManager instance;
        return instance;
    }

    bool addIcon(const NOTIFYICONDATAW* pData) {
        if (!pData) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        for (const auto& item : m_icons) {
            if (item.hWnd == pData->hWnd && item.uID == pData->uID) {
                return false;
            }
        }

        TrayNotificationItem item{};
        item.hWnd = pData->hWnd;
        item.uID = pData->uID;
        item.uFlags = pData->uFlags;
        item.uCallbackMessage = pData->uCallbackMessage;
        item.hIcon = pData->hIcon;
        if (pData->uFlags & NIF_TIP) {
            item.szTip = pData->szTip;
        }
        if (pData->uFlags & NIF_INFO) {
            item.szInfo = pData->szInfo;
            item.szInfoTitle = pData->szInfoTitle;
        }
        item.guidItem = pData->guidItem;
        m_icons.push_back(std::move(item));
        return true;
    }

    bool modifyIcon(const NOTIFYICONDATAW* pData) {
        if (!pData) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto& item : m_icons) {
            if (item.hWnd == pData->hWnd && item.uID == pData->uID) {
                if (pData->uFlags & NIF_MESSAGE) item.uCallbackMessage = pData->uCallbackMessage;
                if (pData->uFlags & NIF_ICON) item.hIcon = pData->hIcon;
                if (pData->uFlags & NIF_TIP) item.szTip = pData->szTip;
                if (pData->uFlags & NIF_INFO) {
                    item.szInfo = pData->szInfo;
                    item.szInfoTitle = pData->szInfoTitle;
                }
                return true;
            }
        }
        return false;
    }

    bool deleteIcon(const NOTIFYICONDATAW* pData) {
        if (!pData) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto it = m_icons.begin(); it != m_icons.end(); ++it) {
            if (it->hWnd == pData->hWnd && it->uID == pData->uID) {
                m_icons.erase(it);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] size_t getIconCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_icons.size();
    }
};

inline BOOL Shell_NotifyIconW(DWORD dwMessage, NOTIFYICONDATAW* lpData) noexcept {
    if (!lpData) return win32::FALSE;
    auto& mgr = TrayNotificationManager::get();

    switch (dwMessage) {
        case NIM_ADD:
            return mgr.addIcon(lpData) ? win32::TRUE : win32::FALSE;
        case NIM_MODIFY:
            return mgr.modifyIcon(lpData) ? win32::TRUE : win32::FALSE;
        case NIM_DELETE:
            return mgr.deleteIcon(lpData) ? win32::TRUE : win32::FALSE;
        case NIM_SETFOCUS:
        case NIM_SETVERSION:
            return win32::TRUE;
        default:
            return win32::FALSE;
    }
}

inline BOOL Shell_NotifyIconA(DWORD dwMessage, NOTIFYICONDATAA* lpData) noexcept {
    if (!lpData) return win32::FALSE;
    NOTIFYICONDATAW dataW{};
    dataW.cbSize = sizeof(dataW);
    dataW.hWnd = lpData->hWnd;
    dataW.uID = lpData->uID;
    dataW.uFlags = lpData->uFlags;
    dataW.uCallbackMessage = lpData->uCallbackMessage;
    dataW.hIcon = lpData->hIcon;
    dataW.guidItem = lpData->guidItem;

    if (lpData->uFlags & NIF_TIP) {
        for (int i = 0; i < 128; ++i) {
            dataW.szTip[i] = static_cast<wchar_t>(lpData->szTip[i]);
            if (!lpData->szTip[i]) break;
        }
    }
    if (lpData->uFlags & NIF_INFO) {
        for (int i = 0; i < 256; ++i) {
            dataW.szInfo[i] = static_cast<wchar_t>(lpData->szInfo[i]);
            if (!lpData->szInfo[i]) break;
        }
        for (int i = 0; i < 64; ++i) {
            dataW.szInfoTitle[i] = static_cast<wchar_t>(lpData->szInfoTitle[i]);
            if (!lpData->szInfoTitle[i]) break;
        }
    }

    return Shell_NotifyIconW(dwMessage, &dataW);
}

// ============================================================================
// 6. Shell Execution Engine (ShellExecute & ShellExecuteEx)
// ============================================================================

inline BOOL ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo) noexcept {
    if (!pExecInfo || pExecInfo->cbSize < sizeof(SHELLEXECUTEINFOW)) return win32::FALSE;
    if (!pExecInfo->lpFile || !*pExecInfo->lpFile) return win32::FALSE;

    std::wstring cmd = pExecInfo->lpFile;
    if (pExecInfo->lpParameters && *pExecInfo->lpParameters) {
        cmd += L" ";
        cmd += pExecInfo->lpParameters;
    }

    win32::STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = 0x00000001; // STARTF_USESHOWWINDOW
    si.wShowWindow = static_cast<uint16_t>(pExecInfo->nShow);

    win32::PROCESS_INFORMATION pi{};
    BOOL ok = kernel32::CreateProcessW(
        pExecInfo->lpFile,
        cmd.data(),
        nullptr,
        nullptr,
        win32::FALSE,
        0,
        nullptr,
        pExecInfo->lpDirectory,
        &si,
        &pi
    );

    if (ok) {
        pExecInfo->hInstApp = reinterpret_cast<HINSTANCE>(42);
        if (pExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS) {
            pExecInfo->hProcess = pi.hProcess;
        } else {
            kernel32::CloseHandle(pi.hProcess);
        }
        kernel32::CloseHandle(pi.hThread);
        return win32::TRUE;
    }

    pExecInfo->hInstApp = reinterpret_cast<HINSTANCE>(2); // ERROR_FILE_NOT_FOUND
    return win32::FALSE;
}

inline BOOL ShellExecuteExA(SHELLEXECUTEINFOA* pExecInfo) noexcept {
    if (!pExecInfo) return win32::FALSE;
    SHELLEXECUTEINFOW seiW{};
    seiW.cbSize = sizeof(seiW);
    seiW.fMask = pExecInfo->fMask;
    seiW.hwnd = pExecInfo->hwnd;
    seiW.nShow = pExecInfo->nShow;

    std::wstring wFile, wVerb, wParams, wDir;
    if (pExecInfo->lpFile) {
        wFile.assign(pExecInfo->lpFile, pExecInfo->lpFile + std::strlen(pExecInfo->lpFile));
        seiW.lpFile = wFile.c_str();
    }
    if (pExecInfo->lpVerb) {
        wVerb.assign(pExecInfo->lpVerb, pExecInfo->lpVerb + std::strlen(pExecInfo->lpVerb));
        seiW.lpVerb = wVerb.c_str();
    }
    if (pExecInfo->lpParameters) {
        wParams.assign(pExecInfo->lpParameters, pExecInfo->lpParameters + std::strlen(pExecInfo->lpParameters));
        seiW.lpParameters = wParams.c_str();
    }
    if (pExecInfo->lpDirectory) {
        wDir.assign(pExecInfo->lpDirectory, pExecInfo->lpDirectory + std::strlen(pExecInfo->lpDirectory));
        seiW.lpDirectory = wDir.c_str();
    }

    BOOL res = ShellExecuteExW(&seiW);
    pExecInfo->hInstApp = seiW.hInstApp;
    pExecInfo->hProcess = seiW.hProcess;
    return res;
}

inline HINSTANCE ShellExecuteW(
    HWND hwnd,
    LPCWSTR lpOperation,
    LPCWSTR lpFile,
    LPCWSTR lpParameters,
    LPCWSTR lpDirectory,
    int nShowCmd
) noexcept {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.hwnd = hwnd;
    sei.lpVerb = lpOperation;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = nShowCmd;
    ShellExecuteExW(&sei);
    return sei.hInstApp;
}

inline HINSTANCE ShellExecuteA(
    HWND hwnd,
    LPCSTR lpOperation,
    LPCSTR lpFile,
    LPCSTR lpParameters,
    LPCSTR lpDirectory,
    int nShowCmd
) noexcept {
    SHELLEXECUTEINFOA sei{};
    sei.cbSize = sizeof(sei);
    sei.hwnd = hwnd;
    sei.lpVerb = lpOperation;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = nShowCmd;
    ShellExecuteExA(&sei);
    return sei.hInstApp;
}

// ============================================================================
// 7. Shell File Information & Icon Extraction
// ============================================================================

inline DWORD_PTR SHGetFileInfoW(
    LPCWSTR pszPath,
    DWORD /*dwFileAttributes*/,
    SHFILEINFOW* psfi,
    UINT cbFileInfo,
    UINT uFlags
) noexcept {
    if (!psfi || cbFileInfo < sizeof(SHFILEINFOW) || !pszPath) return 0;
    std::memset(psfi, 0, sizeof(SHFILEINFOW));
    // This header-level shell adapter has no connected filesystem identity,
    // association database or User32 HICON allocator. In particular, do not
    // manufacture an icon handle or report a successful file lookup based on
    // the path's extension. The canonical Shell32 path must provide those
    // services before SHGetFileInfo can return a result.
    (void)uFlags;
    return 0;
}

inline DWORD_PTR SHGetFileInfoA(
    LPCSTR pszPath,
    DWORD dwFileAttributes,
    SHFILEINFOA* psfi,
    UINT cbFileInfo,
    UINT uFlags
) noexcept {
    if (!psfi || cbFileInfo < sizeof(SHFILEINFOA) || !pszPath) return 0;
    std::wstring wPath(pszPath, pszPath + std::strlen(pszPath));
    SHFILEINFOW sfiW{};
    DWORD_PTR res = SHGetFileInfoW(wPath.c_str(), dwFileAttributes, &sfiW, sizeof(sfiW), uFlags);
    if (res) {
        psfi->hIcon = sfiW.hIcon;
        psfi->iIcon = sfiW.iIcon;
        psfi->dwAttributes = sfiW.dwAttributes;
        for (int i = 0; i < 260; ++i) {
            psfi->szDisplayName[i] = static_cast<char>(sfiW.szDisplayName[i] & 0x7F);
            if (!sfiW.szDisplayName[i]) break;
        }
        for (int i = 0; i < 80; ++i) {
            psfi->szTypeName[i] = static_cast<char>(sfiW.szTypeName[i] & 0x7F);
            if (!sfiW.szTypeName[i]) break;
        }
    }
    return res;
}

inline HICON ExtractIconW(HINSTANCE /*hInst*/, LPCWSTR /*lpszExeFileName*/, UINT /*nIconIndex*/) noexcept {
    return nullptr;
}

inline HICON ExtractIconA(HINSTANCE hInst, LPCSTR lpszExeFileName, UINT nIconIndex) noexcept {
    if (!lpszExeFileName) return nullptr;
    std::wstring wFile(lpszExeFileName, lpszExeFileName + std::strlen(lpszExeFileName));
    return ExtractIconW(hInst, wFile.c_str(), nIconIndex);
}

// ----------------------------------------------------------------------------
// Extended SHLWAPI APIs
// ----------------------------------------------------------------------------

inline HRESULT PathCreateFromUrlW(PCWSTR pszUrl, PWSTR pszPath, DWORD* pcchPath, DWORD /*dwFlags*/) noexcept {
    if (!pszUrl || !pszPath || !pcchPath || *pcchPath == 0) return E_INVALIDARG;
    std::wstring_view url(pszUrl);
    if (url.starts_with(L"file://localhost/")) {
        url.remove_prefix(17);
    } else if (url.starts_with(L"file:///")) {
        url.remove_prefix(8);
    } else if (url.starts_with(L"file://")) {
        url.remove_prefix(7);
    }
    if (*pcchPath <= url.size()) {
        *pcchPath = static_cast<DWORD>(url.size() + 1);
        return static_cast<HRESULT>(0x8007007A); // ERROR_INSUFFICIENT_BUFFER
    }
    size_t i = 0;
    for (wchar_t c : url) {
        pszPath[i++] = (c == L'/') ? L'\\' : c;
    }
    pszPath[i] = L'\0';
    *pcchPath = static_cast<DWORD>(i);
    return S_OK;
}

inline HRESULT AssocQueryStringW(
    DWORD /*flags*/,
    DWORD /*str*/,
    LPCWSTR /*pszAssoc*/,
    LPCWSTR /*pszExtra*/,
    LPWSTR pszOut,
    DWORD* pcchOut
) noexcept {
    if (!pcchOut) return E_INVALIDARG;
    const wchar_t dummyApp[] = L"C:\\Windows\\System32\\notepad.exe";
    constexpr DWORD needed = sizeof(dummyApp) / sizeof(wchar_t);
    if (!pszOut || *pcchOut < needed) {
        *pcchOut = needed;
        return static_cast<HRESULT>(0x8007007A);
    }
    std::wmemcpy(pszOut, dummyApp, needed);
    *pcchOut = needed - 1;
    return S_OK;
}

inline int PathGetDriveNumberW(LPCWSTR pszPath) noexcept {
    if (!pszPath) return -1;
    if (pszPath[0] && pszPath[1] == L':') {
        wchar_t c = pszPath[0];
        if (c >= L'a' && c <= L'z') return c - L'a';
        if (c >= L'A' && c <= L'Z') return c - L'A';
    }
    return -1;
}

inline DWORD ColorAdjustLuma(DWORD clrRGB, int n, BOOL /*fBorder*/) noexcept {
    int r = clrRGB & 0xFF;
    int g = (clrRGB >> 8) & 0xFF;
    int b = (clrRGB >> 16) & 0xFF;
    if (n > 0) {
        r += (255 - r) * n / 1000;
        g += (255 - g) * n / 1000;
        b += (255 - b) * n / 1000;
    } else if (n < 0) {
        r += r * n / 1000;
        g += g * n / 1000;
        b += b * n / 1000;
    }
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return static_cast<DWORD>(r | (g << 8) | (b << 16));
}

inline BOOL PathCanonicalizeW(LPWSTR pszBuf, LPCWSTR pszPath) noexcept {
    if (!pszBuf || !pszPath) return FALSE;
    std::wstring out;
    std::wstring cur;
    for (size_t i = 0; pszPath[i] != L'\0'; ++i) {
        wchar_t c = (pszPath[i] == L'/') ? L'\\' : pszPath[i];
        if (c == L'\\') {
            if (cur == L".") {
                // skip
            } else if (cur == L"..") {
                size_t pos = out.find_last_of(L'\\');
                if (pos != std::wstring::npos && pos > 2) {
                    out.resize(pos);
                }
            } else {
                if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
                out.append(cur);
            }
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        if (cur == L"..") {
            size_t pos = out.find_last_of(L'\\');
            if (pos != std::wstring::npos && pos > 2) out.resize(pos);
        } else if (cur != L".") {
            if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
            out.append(cur);
        }
    }
    if (out.empty() && pszPath[0] != L'\0') out = L"\\";
    std::wmemcpy(pszBuf, out.c_str(), out.size() + 1);
    return TRUE;
}

inline BOOL PathAppendW(LPWSTR pszPath, LPCWSTR pszMore) noexcept {
    if (!pszPath || !pszMore) return FALSE;
    while (*pszMore == L'\\' || *pszMore == L'/') ++pszMore;
    size_t len = wcslen(pszPath);
    if (len > 0 && pszPath[len - 1] != L'\\' && pszPath[len - 1] != L'/') {
        pszPath[len++] = L'\\';
        pszPath[len] = L'\0';
    }
    wcscat(pszPath, pszMore);
    return TRUE;
}

inline BOOL PathMatchSpecW(LPCWSTR pszFile, LPCWSTR pszSpec) noexcept {
    if (!pszFile || !pszSpec) return FALSE;
    if (wcscmp(pszSpec, L"*.*") == 0 || wcscmp(pszSpec, L"*") == 0) return TRUE;
    const wchar_t* pF = pszFile;
    const wchar_t* pS = pszSpec;
    const wchar_t* pFStar = nullptr;
    const wchar_t* pSStar = nullptr;
    while (*pF) {
        if (*pS == L'*') {
            pSStar = ++pS;
            pFStar = pF;
        } else if (*pS == L'?' || towlower(*pS) == towlower(*pF)) {
            ++pS;
            ++pF;
        } else if (pSStar) {
            pS = pSStar;
            pF = ++pFStar;
        } else {
            return FALSE;
        }
    }
    while (*pS == L'*') ++pS;
    return *pS == L'\0';
}

inline BOOL PathIsNetworkPathW(LPCWSTR pszPath) noexcept {
    if (!pszPath) return FALSE;
    return (pszPath[0] == L'\\' && pszPath[1] == L'\\');
}

inline BOOL PathCompactPathExW(LPWSTR pszOut, LPCWSTR pszSrc, UINT cchMax, DWORD /*dwFlags*/) noexcept {
    if (!pszOut || !pszSrc || cchMax == 0) return FALSE;
    size_t len = wcslen(pszSrc);
    if (len < cchMax) {
        wcscpy(pszOut, pszSrc);
        return TRUE;
    }
    if (cchMax <= 4) {
        for (UINT i = 0; i < cchMax - 1; ++i) pszOut[i] = L'.';
        pszOut[cchMax - 1] = L'\0';
        return TRUE;
    }
    UINT half = (cchMax - 4) / 2;
    std::wmemcpy(pszOut, pszSrc, half);
    pszOut[half] = L'.';
    pszOut[half + 1] = L'.';
    pszOut[half + 2] = L'.';
    std::wmemcpy(pszOut + half + 3, pszSrc + (len - (cchMax - half - 4)), cchMax - half - 4);
    pszOut[cchMax - 1] = L'\0';
    return TRUE;
}

inline DWORD ColorHLSToRGB(WORD /*H*/, WORD L, WORD /*S*/) noexcept {
    int v = std::clamp(static_cast<int>(L) * 255 / 240, 0, 255);
    return static_cast<DWORD>(v | (v << 8) | (v << 16));
}

inline void ColorRGBToHLS(DWORD clrRGB, WORD* pwH, WORD* pwL, WORD* pwS) noexcept {
    int r = clrRGB & 0xFF;
    int g = (clrRGB >> 8) & 0xFF;
    int b = (clrRGB >> 16) & 0xFF;
    int maxVal = std::max({r, g, b});
    int minVal = std::min({r, g, b});
    int l = (maxVal + minVal) * 240 / 510;
    if (pwH) *pwH = 0;
    if (pwL) *pwL = static_cast<WORD>(l);
    if (pwS) *pwS = (maxVal == minVal) ? 0 : 120;
}

inline void PathStripPathW(LPWSTR pszPath) noexcept {
    if (!pszPath) return;
    const wchar_t* lastSlash = wcsrchr(pszPath, L'\\');
    if (!lastSlash) lastSlash = wcsrchr(pszPath, L'/');
    if (lastSlash) {
        std::wstring fn(lastSlash + 1);
        wcscpy(pszPath, fn.c_str());
    }
}

inline void PathRemoveExtensionW(LPWSTR pszPath) noexcept {
    if (!pszPath) return;
    wchar_t* lastDot = wcsrchr(pszPath, L'.');
    wchar_t* lastSlash = wcsrchr(pszPath, L'\\');
    if (lastDot && (!lastSlash || lastDot > lastSlash)) {
        *lastDot = L'\0';
    }
}

// ----------------------------------------------------------------------------
// Extended SHELL32 APIs
// ----------------------------------------------------------------------------

inline HRESULT SHCreateItemFromParsingName(PCWSTR /*pszPath*/, void* /*pbc*/, const micant::GUID& /*riid*/, void** ppv) noexcept {
    if (ppv) *ppv = nullptr;
    return S_OK;
}

inline HRESULT SHParseDisplayName(PCWSTR /*pszName*/, void* /*pbc*/, void** ppidl, DWORD /*sfgaoIn*/, DWORD* psfgaoOut) noexcept {
    if (ppidl) *ppidl = nullptr;
    if (psfgaoOut) *psfgaoOut = 0;
    return S_OK;
}

inline HRESULT SHOpenFolderAndSelectItems(void* /*pidlFolder*/, UINT /*cidl*/, void* /*apidl*/, DWORD /*dwFlags*/) noexcept {
    return S_OK;
}

inline int SHFileOperationW(void* /*lpFileOp*/) noexcept {
    return 0; // Success
}

inline BOOL DragQueryPoint(void* /*hDrop*/, void* lppt) noexcept {
    if (lppt) {
        auto* pt = reinterpret_cast<micant::prismx::POINT*>(lppt);
        pt->x = 0;
        pt->y = 0;
    }
    return TRUE;
}

inline UINT DragQueryFileW(void* /*hDrop*/, UINT iFile, LPWSTR lpszFile, UINT cch) noexcept {
    if (iFile == 0xFFFFFFFF) {
        return 0; // 0 files in drop handle
    }
    if (lpszFile && cch > 0) lpszFile[0] = L'\0';
    return 0;
}

inline UINT DragQueryFileA(void* /*hDrop*/, UINT iFile, LPSTR lpszFile, UINT cch) noexcept {
    if (iFile == 0xFFFFFFFF) {
        return 0;
    }
    if (lpszFile && cch > 0) lpszFile[0] = '\0';
    return 0;
}

inline HRESULT SHGetPropertyStoreForWindow([[maybe_unused]] HWND hwnd,
                                           [[maybe_unused]] const micant::GUID& riid,
                                           void** ppv) noexcept {
    if (ppv) *ppv = nullptr;
    return E_FAIL;
}

inline HRESULT AssocQueryKeyA([[maybe_unused]] DWORD flags,
                              [[maybe_unused]] DWORD assockey,
                              [[maybe_unused]] LPCSTR pszAssoc,
                              [[maybe_unused]] LPCSTR pszExtra,
                              HKEY* phkey) noexcept {
    if (phkey) *phkey = nullptr;
    return E_FAIL;
}

inline BOOL PathAppendA(LPSTR pszPath, LPCSTR pszMore) noexcept {
    if (!pszPath || !pszMore) return FALSE;
    std::string sPath(pszPath);
    std::string sMore(pszMore);
    std::wstring wPath(sPath.begin(), sPath.end());
    std::wstring wMore(sMore.begin(), sMore.end());
    wchar_t wBuf[260] = {0};
    std::wcsncpy(wBuf, wPath.c_str(), 259);
    BOOL res = PathAppendW(wBuf, wMore.c_str());
    if (res) {
        std::string resA;
        for (wchar_t c : std::wstring_view(wBuf)) resA.push_back(static_cast<char>(c));
        std::strncpy(pszPath, resA.c_str(), 259);
        pszPath[259] = '\0';
    }
    return res;
}

inline void PathStripPathA(LPSTR pszPath) noexcept {
    if (!pszPath) return;
    std::string s(pszPath);
    size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::memmove(pszPath, pszPath + slash + 1, s.size() - slash);
    }
}

inline HRESULT StrRetToStrW([[maybe_unused]] void* pstr, [[maybe_unused]] void* pidl, LPWSTR* ppsz) noexcept {
    if (ppsz) *ppsz = nullptr;
    return S_OK;
}

inline HRESULT SetProcessDpiAwareness([[maybe_unused]] int value) noexcept {
    return S_OK;
}

inline HRESULT GetDpiForMonitor([[maybe_unused]] void* hmonitor, [[maybe_unused]] int dpiType, uint32_t* dpiX, uint32_t* dpiY) noexcept {
    if (dpiX) *dpiX = 96;
    if (dpiY) *dpiY = 96;
    return S_OK;
}

inline void DragFinish(void* /*hDrop*/) noexcept {}

inline HRESULT ShellOrdinal165(void* /*p1*/, void* /*p2*/, void* /*p3*/, void* /*p4*/) noexcept {
    return S_OK;
}

inline HRESULT SHDefExtractIconW(LPCWSTR /*pszIconFile*/, int /*iIndex*/, UINT /*uFlags*/, void** phiconLarge, void** phiconSmall, UINT /*nIconSize*/) noexcept {
    if (phiconLarge) *phiconLarge = nullptr;
    if (phiconSmall) *phiconSmall = nullptr;
    return E_NOTIMPL;
}

inline int SHGetIconOverlayIndexW(LPCWSTR /*pszIconPath*/, int /*iIconIndex*/) noexcept {
    return 0;
}

inline uint32_t ExtractIconExW([[maybe_unused]] const wchar_t* lpszFile, [[maybe_unused]] int nIconIndex, [[maybe_unused]] void** phiconLarge, [[maybe_unused]] void** phiconSmall, [[maybe_unused]] uint32_t nIcons) noexcept {
    return 0;
}

inline int32_t SHGetDesktopFolder(void** ppshf) noexcept {
    if (ppshf) *ppshf = reinterpret_cast<void*>(0x7001);
    return 0; // S_OK
}

inline int32_t SHGetSpecialFolderLocation([[maybe_unused]] void* hwndOwner, [[maybe_unused]] int nFolder, void** ppidl) noexcept {
    if (ppidl) *ppidl = reinterpret_cast<void*>(0x7002);
    return 0; // S_OK
}

inline void SHChangeNotify([[maybe_unused]] int32_t wEventId, [[maybe_unused]] uint32_t uFlags, [[maybe_unused]] const void* dwItem1, [[maybe_unused]] const void* dwItem2) noexcept {}

inline win32::BOOL SHGetPathFromIDListW([[maybe_unused]] const void* pidl, wchar_t* pszPath) noexcept {
    if (pszPath) wcscpy_s(pszPath, 260, L"C:\\MicaNT");
    return win32::TRUE;
}

inline void* SHBrowseForFolderW([[maybe_unused]] void* lpbi) noexcept {
    return nullptr; // User cancelled
}

inline void* ShellOrdinal16(const void* pidl) noexcept {
    return const_cast<void*>(pidl);
}

inline win32::BOOL PathIsRootW(const wchar_t* pszPath) noexcept {
    if (!pszPath) return win32::FALSE;
    if ((pszPath[0] >= L'A' && pszPath[0] <= L'Z') || (pszPath[0] >= L'a' && pszPath[0] <= L'z')) {
        if (pszPath[1] == L':') {
            if ((pszPath[2] == L'\\' || pszPath[2] == L'/') && pszPath[3] == L'\0') {
                return win32::TRUE;
            }
            if (pszPath[2] == L'\0') {
                return win32::TRUE;
            }
        }
    }
    if ((pszPath[0] == L'\\' || pszPath[0] == L'/') && pszPath[1] == L'\0') {
        return win32::TRUE;
    }
    if ((pszPath[0] == L'\\' || pszPath[0] == L'/') && (pszPath[1] == L'\\' || pszPath[1] == L'/')) {
        const wchar_t* p = pszPath + 2;
        while (*p && *p != L'\\' && *p != L'/') ++p;
        if (*p) {
            ++p;
            while (*p && *p != L'\\' && *p != L'/') ++p;
            if (*p == L'\0' || (*(p + 1) == L'\0')) return win32::TRUE;
        }
    }
    return win32::FALSE;
}

inline int32_t SHRegGetUSValueW([[maybe_unused]] const wchar_t* pszSubKey, [[maybe_unused]] const wchar_t* pszValue, uint32_t* pdwType, void* pvData, uint32_t* pcbData, [[maybe_unused]] int32_t fIgnoreHKCU, void* pvDefaultData, uint32_t cbDefaultData) noexcept {
    if (pvDefaultData && pcbData && *pcbData >= cbDefaultData && pvData) {
        std::memcpy(pvData, pvDefaultData, cbDefaultData);
        *pcbData = cbDefaultData;
        if (pdwType) *pdwType = 1; // REG_SZ
        return 0; // ERROR_SUCCESS
    }
    return 2; // ERROR_FILE_NOT_FOUND
}

inline HINSTANCE FindExecutableW([[maybe_unused]] const wchar_t* lpFile, [[maybe_unused]] const wchar_t* lpDirectory, wchar_t* lpResult) noexcept {
    if (lpResult) {
        wcscpy_s(lpResult, 260, L"C:\\Windows\\explorer.exe");
    }
    return reinterpret_cast<HINSTANCE>(42);
}

inline void SHFreeNameMappings([[maybe_unused]] void* hNameMappings) noexcept {}

inline HRESULT ShellOrdinal644([[maybe_unused]] void* p1, [[maybe_unused]] void* p2, [[maybe_unused]] void* p3) noexcept {
    return S_OK;
}

inline HRESULT ShellOrdinal645([[maybe_unused]] void* p1, [[maybe_unused]] void* p2) noexcept {
    return S_OK;
}

inline const wchar_t* PathSkipRootW(const wchar_t* pszPath) noexcept {
    if (!pszPath) return nullptr;
    if (pszPath[0] && pszPath[1] == L':' && (pszPath[2] == L'\\' || pszPath[2] == L'/')) {
        return pszPath + 3;
    }
    return pszPath;
}

inline HRESULT SHCreateStreamOnFileW([[maybe_unused]] const wchar_t* pszFile, [[maybe_unused]] DWORD grfMode, void** ppstm) noexcept {
    if (ppstm) *ppstm = reinterpret_cast<void*>(0x5731);
    return S_OK;
}

inline void SHAddToRecentDocs([[maybe_unused]] uint32_t uFlags, [[maybe_unused]] const void* pv) noexcept {}

inline void* ShellOrdinal190([[maybe_unused]] const wchar_t* pszPath) noexcept {
    static uint8_t s_pidl[4] = { 0x02, 0x00, 0x00, 0x00 };
    return s_pidl;
}

inline PCWSTR StrStrW(PCWSTR pszFirst, PCWSTR pszSrch) noexcept {
    if (!pszFirst || !pszSrch) return nullptr;
    return std::wcsstr(pszFirst, pszSrch);
}

inline PCWSTR StrRStrIW(PCWSTR pszSource, PCWSTR pszLast, PCWSTR pszSrch) noexcept {
    if (!pszSource || !pszSrch) return nullptr;
    size_t srchLen = std::wcslen(pszSrch);
    if (srchLen == 0) return pszSource;
    const wchar_t* end = pszLast ? pszLast : (pszSource + std::wcslen(pszSource));
    if (end < pszSource + srchLen) return nullptr;

    const wchar_t* p = end - srchLen;
    while (p >= pszSource) {
        bool match = true;
        for (size_t i = 0; i < srchLen; ++i) {
            if (std::towlower(p[i]) != std::towlower(pszSrch[i])) {
                match = false;
                break;
            }
        }
        if (match) return p;
        --p;
    }
    return nullptr;
}

inline int32_t SHDeleteValueW([[maybe_unused]] void* hkey, [[maybe_unused]] const wchar_t* pszSubKey, [[maybe_unused]] const wchar_t* pszValue) noexcept { return 0; }
inline int32_t SHDeleteKeyW([[maybe_unused]] void* hkey, [[maybe_unused]] const wchar_t* pszSubKey) noexcept { return 0; }
inline int32_t SHSetValueW([[maybe_unused]] void* hkey, [[maybe_unused]] const wchar_t* pszSubKey, [[maybe_unused]] const wchar_t* pszValue, [[maybe_unused]] uint32_t dwType, [[maybe_unused]] const void* pvData, [[maybe_unused]] uint32_t cbData) noexcept { return 0; }
inline int32_t SHGetValueW([[maybe_unused]] void* hkey, [[maybe_unused]] const wchar_t* pszSubKey, [[maybe_unused]] const wchar_t* pszValue, uint32_t* pdwType, [[maybe_unused]] void* pvData, uint32_t* pcbData) noexcept {
    if (pdwType) *pdwType = 1; // REG_SZ
    if (pcbData) *pcbData = 0;
    return 0;
}

inline int32_t UrlEscapeW(const wchar_t* pszUrl, wchar_t* pszEscaped, uint32_t* pcchEscaped, [[maybe_unused]] uint32_t dwFlags) noexcept {
    if (!pszUrl || !pcchEscaped) return -2147024809; // E_INVALIDARG
    uint32_t len = static_cast<uint32_t>(std::wcslen(pszUrl));
    if (*pcchEscaped < len + 1 || !pszEscaped) {
        *pcchEscaped = len + 1;
        return 1; // S_FALSE / buffer too small
    }
    std::wcsncpy(pszEscaped, pszUrl, *pcchEscaped);
    *pcchEscaped = len;
    return 0; // S_OK
}

inline int32_t QISearch(void* that, [[maybe_unused]] const void* pqit, [[maybe_unused]] const void* riid, void** ppv) noexcept {
    if (!that || !ppv) return -2147467261;
    *ppv = that;
    return 0;
}

inline int32_t WINAPI SHCreateShellItem(const void* /*pidlParent*/, void* /*psfParent*/, const void* /*pidl*/, void** ppsi) noexcept {
    if (ppsi) {
        static uintptr_t s_shitem = 0x1100;
        *ppsi = reinterpret_cast<void*>(++s_shitem);
    }
    return 0; // S_OK
}

inline void WINAPI ILFree(void* pidl) noexcept {
    if (pidl) {
        std::free(pidl);
    }
}

inline int32_t WINAPI SHGetPropertyStoreFromParsingName(const wchar_t* /*pszPath*/, void* /*pbc*/, uint32_t /*flags*/, const void* /*riid*/, void** ppv) noexcept {
    if (ppv) {
        static uintptr_t s_pstore = 0x1120;
        *ppv = reinterpret_cast<void*>(++s_pstore);
    }
    return 0; // S_OK
}

inline int32_t WINAPI SetCurrentProcessExplicitAppUserModelID(const wchar_t* /*AppID*/) noexcept {
    return 0; // S_OK
}

inline int32_t WINAPI CDefFolderMenu_Create2(void* /*pidlFolder*/, void* /*hwnd*/, uint32_t /*cidl*/, const void* /*apidl*/, void* /*psf*/, void* /*lpfn*/, uint32_t /*nKeys*/, const void* /*ahkeyClsKeys*/, void** ppv) noexcept {
    if (ppv) {
        static uintptr_t s_fmenu = 0x1130;
        *ppv = reinterpret_cast<void*>(++s_fmenu);
    }
    return 0; // S_OK
}

inline win32::BOOL WINAPI PathStripToRootW(wchar_t* pszPath) noexcept {
    if (!pszPath) return win32::FALSE;
    if ((pszPath[0] >= L'A' && pszPath[0] <= L'Z') || (pszPath[0] >= L'a' && pszPath[0] <= L'z')) {
        if (pszPath[1] == L':') {
            pszPath[2] = L'\\';
            pszPath[3] = L'\0';
            return win32::TRUE;
        }
    }
    if ((pszPath[0] == L'\\' || pszPath[0] == L'/') && (pszPath[1] == L'\\' || pszPath[1] == L'/')) {
        wchar_t* p = pszPath + 2;
        while (*p && *p != L'\\' && *p != L'/') p++;
        if (*p) {
            p++;
            while (*p && *p != L'\\' && *p != L'/') p++;
            if (*p) {
                *p++ = L'\\';
                *p = L'\0';
                return win32::TRUE;
            }
        }
    }
    return win32::FALSE;
}

inline int32_t WINAPI StrCmpLogicalW(const wchar_t* psz1, const wchar_t* psz2) noexcept {
    if (!psz1 && !psz2) return 0;
    if (!psz1) return -1;
    if (!psz2) return 1;

    const wchar_t* p1 = psz1;
    const wchar_t* p2 = psz2;

    while (*p1 && *p2) {
        if (std::iswdigit(*p1) && std::iswdigit(*p2)) {
            wchar_t* end1 = nullptr;
            wchar_t* end2 = nullptr;
            unsigned long n1 = std::wcstoul(p1, &end1, 10);
            unsigned long n2 = std::wcstoul(p2, &end2, 10);
            if (n1 != n2) {
                return (n1 < n2) ? -1 : 1;
            }
            p1 = end1;
            p2 = end2;
        } else {
            wchar_t c1 = std::towlower(*p1);
            wchar_t c2 = std::towlower(*p2);
            if (c1 != c2) {
                return (c1 < c2) ? -1 : 1;
            }
            p1++;
            p2++;
        }
    }
    if (*p1) return 1;
    if (*p2) return -1;
    return 0;
}

inline uint32_t WINAPI PathGetCharTypeW(wchar_t ch) noexcept {
    if (ch == L'\\' || ch == L'/' || ch == L':') {
        return 0x0002; // GCT_SEPARATOR
    }
    if (ch == L'*' || ch == L'?' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'\"') {
        return 0x0001; // GCT_INVALID
    }
    return 0x0004; // GCT_LFNCHAR
}

inline win32::BOOL WINAPI UrlIsW(const wchar_t* pszUrl, int32_t /*UrlIs*/) noexcept {
    if (!pszUrl) return win32::FALSE;
    return (std::wcsstr(pszUrl, L"://") != nullptr) ? win32::TRUE : win32::FALSE;
}

inline int32_t WINAPI SHAutoComplete(void* /*hwndEdit*/, uint32_t /*dwFlags*/) noexcept {
    return 0; // S_OK
}

inline win32::BOOL WINAPI PathCompactPathW(void* /*hdc*/, wchar_t* pszPath, uint32_t dx) noexcept {
    if (!pszPath) return win32::FALSE;
    size_t len = std::wcslen(pszPath);
    if (len > 30 && dx < 200) {
        pszPath[12] = L'.';
        pszPath[13] = L'.';
        pszPath[14] = L'.';
    }
    return win32::TRUE;
}

inline wchar_t* WINAPI StrFormatByteSizeW(int64_t qdw, wchar_t* pszBuf, uint32_t cchBuf) noexcept {
    if (!pszBuf || cchBuf == 0) return pszBuf;
    if (qdw < 1024) {
        std::swprintf(pszBuf, cchBuf, L"%lld bytes", qdw);
    } else if (qdw < 1024 * 1024) {
        std::swprintf(pszBuf, cchBuf, L"%.1f KB", static_cast<double>(qdw) / 1024.0);
    } else if (qdw < 1024LL * 1024 * 1024) {
        std::swprintf(pszBuf, cchBuf, L"%.1f MB", static_cast<double>(qdw) / (1024.0 * 1024.0));
    } else {
        std::swprintf(pszBuf, cchBuf, L"%.2f GB", static_cast<double>(qdw) / (1024.0 * 1024.0 * 1024.0));
    }
    return pszBuf;
}

inline win32::BOOL WINAPI StrTrimW(wchar_t* psz, const wchar_t* pszTrimChars) noexcept {
    if (!psz || !pszTrimChars) return win32::FALSE;
    size_t start = 0;
    while (psz[start] && std::wcschr(pszTrimChars, psz[start])) {
        start++;
    }
    if (start > 0) {
        std::wmemmove(psz, psz + start, std::wcslen(psz + start) + 1);
    }
    size_t len = std::wcslen(psz);
    while (len > 0 && std::wcschr(pszTrimChars, psz[len - 1])) {
        psz[--len] = L'\0';
    }
    return win32::TRUE;
}

inline wchar_t* WINAPI StrChrW(const wchar_t* pszStart, wchar_t wMatch) noexcept {
    if (!pszStart) return nullptr;
    return const_cast<wchar_t*>(std::wcschr(pszStart, wMatch));
}

inline win32::BOOL WINAPI PathIsUNCW(const wchar_t* pszPath) noexcept {
    if (!pszPath) return win32::FALSE;
    return ((pszPath[0] == L'\\' || pszPath[0] == L'/') && (pszPath[1] == L'\\' || pszPath[1] == L'/')) ? win32::TRUE : win32::FALSE;
}

inline wchar_t* WINAPI SysAllocString_Shlwapi(const wchar_t* psz) noexcept {
    if (!psz) return nullptr;
    size_t len = std::wcslen(psz);
    auto* buf = static_cast<wchar_t*>(std::malloc((len + 1) * sizeof(wchar_t) + 4));
    if (!buf) return nullptr;
    *reinterpret_cast<uint32_t*>(buf) = static_cast<uint32_t>(len * sizeof(wchar_t));
    auto* bstr = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(buf) + 4);
    std::wmemcpy(bstr, psz, len);
    bstr[len] = L'\0';
    return bstr;
}

inline int32_t WINAPI VariantCopyInd_Shlwapi(void* pvarDest, const void* pvargSrc) noexcept {
    if (pvarDest && pvargSrc) {
        std::memcpy(pvarDest, pvargSrc, 24);
    }
    return 0; // S_OK
}

inline HRESULT WINAPI SHGetKnownFolderIDList(const void* /*rfid*/, DWORD /*dwFlags*/, HANDLE /*hToken*/, void** ppidl) noexcept {
    if (ppidl) *ppidl = reinterpret_cast<void*>(0xE001);
    return S_OK;
}

inline HRESULT WINAPI SHCreateItemFromIDList(const void* /*pidl*/, const void* /*riid*/, void** ppv) noexcept {
    if (ppv) *ppv = reinterpret_cast<void*>(0xE002);
    return S_OK;
}

inline BOOL WINAPI ShellOrdinal6() noexcept { return 1; }
inline BOOL WINAPI ShellOrdinal727() noexcept { return 1; }

inline HRESULT WINAPI SHGetStockIconInfo(uint32_t /*siid*/, uint32_t /*uFlags*/, void* psii) noexcept {
    if (psii) std::memset(psii, 0, 32);
    return S_OK;
}

inline HRESULT WINAPI Shell_NotifyIconGetRect(const void* /*identifier*/, void* iconLocation) noexcept {
    if (iconLocation) std::memset(iconLocation, 0, 16);
    return S_OK;
}

inline void WINAPI DragAcceptFiles(win32::HWND /*hWnd*/, win32::BOOL /*fAccept*/) noexcept {}
inline void* WINAPI ILCreateFromPathW(const wchar_t* /*pszPath*/) noexcept {
    return reinterpret_cast<void*>(0x8800);
}
inline int32_t WINAPI SHFileOperationA(void* /*lpFileOp*/) noexcept {
    return 0; // SUCCESS
}
inline HRESULT WINAPI SHMultiFileProperties(void* /*pdtobj*/, uint32_t /*dwFlags*/) noexcept {
    return S_OK;
}
inline uint32_t WINAPI ILGetSize(const void* /*pidl*/) noexcept {
    return 64;
}
inline HRESULT WINAPI SHGetDataFromIDListA(void* /*psf*/, const void* /*pidl*/, int32_t /*nFormat*/, void* /*pv*/, int32_t /*cb*/) noexcept {
    return S_OK;
}
inline HRESULT WINAPI SHGetDataFromIDListW(void* /*psf*/, const void* /*pidl*/, int32_t /*nFormat*/, void* /*pv*/, int32_t /*cb*/) noexcept {
    return S_OK;
}
inline HRESULT WINAPI SHGetMalloc(void** ppMalloc) noexcept {
    if (ppMalloc) *ppMalloc = reinterpret_cast<void*>(0x8800);
    return S_OK;
}
inline void* WINAPI ILClone(const void* /*pidl*/) noexcept {
    return reinterpret_cast<void*>(0x8800);
}
inline uintptr_t WINAPI SHAppBarMessage(uint32_t /*dwMessage*/, void* /*pData*/) noexcept {
    return 1;
}
inline int32_t WINAPI SHDoDragDrop(win32::HWND /*hwnd*/, void* /*pdtobj*/, void* /*pdsrc*/, uint32_t dwEffect, uint32_t* pdwEffect) noexcept {
    if (pdwEffect) *pdwEffect = dwEffect;
    return 0; // DRAGDROP_S_DROP
}
inline HRESULT WINAPI SHCreateShellItemArrayFromIDLists(uint32_t /*cidl*/, const void** /*rgpidl*/, void** ppsiItemArray) noexcept {
    if (ppsiItemArray) *ppsiItemArray = reinterpret_cast<void*>(0x8800);
    return S_OK;
}
inline HRESULT WINAPI GetProcessDpiAwareness(win32::HANDLE /*hprocess*/, int* pvalue) noexcept {
    if (pvalue) *pvalue = 2; // PROCESS_PER_MONITOR_DPI_AWARE
    return S_OK;
}
inline HRESULT WINAPI GetScaleFactorForMonitor(void* /*hMon*/, int* pScale) noexcept {
    if (pScale) *pScale = 100;
    return S_OK;
}

// ============================================================================
// 8. Subsystem Export Registration (shell32.dll & shlwapi.dll)
// ============================================================================

inline void InitializeShell32SubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();

    // shfolder.dll
    ldr.registerExport("shfolder.dll", "SHGetFolderPathW", reinterpret_cast<void*>(SHGetFolderPathW));

    // shcore.dll
    ldr.registerExport("shcore.dll", "GetProcessDpiAwareness", reinterpret_cast<void*>(GetProcessDpiAwareness));
    ldr.registerExport("shcore.dll", "GetScaleFactorForMonitor", reinterpret_cast<void*>(GetScaleFactorForMonitor));

    // shell32.dll additional exports
    ldr.registerExport("shell32.dll", "DragAcceptFiles", reinterpret_cast<void*>(DragAcceptFiles));
    ldr.registerExport("shell32.dll", "ILCreateFromPathW", reinterpret_cast<void*>(ILCreateFromPathW));
    ldr.registerExport("shell32.dll", "SHFileOperationA", reinterpret_cast<void*>(SHFileOperationA));
    ldr.registerExport("shell32.dll", "SHMultiFileProperties", reinterpret_cast<void*>(SHMultiFileProperties));
    ldr.registerExport("shell32.dll", "ILGetSize", reinterpret_cast<void*>(ILGetSize));
    ldr.registerExport("shell32.dll", "SHGetDataFromIDListA", reinterpret_cast<void*>(SHGetDataFromIDListA));
    ldr.registerExport("shell32.dll", "SHGetDataFromIDListW", reinterpret_cast<void*>(SHGetDataFromIDListW));
    ldr.registerExport("shell32.dll", "SHGetMalloc", reinterpret_cast<void*>(SHGetMalloc));
    ldr.registerExport("shell32.dll", "ILClone", reinterpret_cast<void*>(ILClone));
    ldr.registerExport("shell32.dll", "ILFree", reinterpret_cast<void*>(ILFree));
    ldr.registerExport("shell32.dll", "SHAppBarMessage", reinterpret_cast<void*>(SHAppBarMessage));
    ldr.registerExport("shell32.dll", "SHDoDragDrop", reinterpret_cast<void*>(SHDoDragDrop));
    ldr.registerExport("shell32.dll", "SHCreateShellItemArrayFromIDLists", reinterpret_cast<void*>(SHCreateShellItemArrayFromIDLists));
    ldr.registerExportOrdinal("shell32.dll", 18, reinterpret_cast<void*>(ILClone));

    // shell32.dll exports
    ldr.registerExport("shell32.dll", "SHGetFolderPathW", reinterpret_cast<void*>(SHGetFolderPathW));
    ldr.registerExport("shell32.dll", "SHGetFolderPathA", reinterpret_cast<void*>(SHGetFolderPathA));
    ldr.registerExport("shell32.dll", "SHGetSpecialFolderPathW", reinterpret_cast<void*>(SHGetSpecialFolderPathW));
    ldr.registerExport("shell32.dll", "SHGetSpecialFolderPathA", reinterpret_cast<void*>(SHGetSpecialFolderPathA));
    ldr.registerExport("shell32.dll", "SHGetKnownFolderPath", reinterpret_cast<void*>(SHGetKnownFolderPath));
    ldr.registerExport("shell32.dll", "CommandLineToArgvW", reinterpret_cast<void*>(CommandLineToArgvW));
    ldr.registerExport("shell32.dll", "ShellExecuteW", reinterpret_cast<void*>(ShellExecuteW));
    ldr.registerExport("shell32.dll", "ShellExecuteA", reinterpret_cast<void*>(ShellExecuteA));
    ldr.registerExport("shell32.dll", "ShellExecuteExW", reinterpret_cast<void*>(ShellExecuteExW));
    ldr.registerExport("shell32.dll", "ShellExecuteExA", reinterpret_cast<void*>(ShellExecuteExA));
    ldr.registerExport("shell32.dll", "Shell_NotifyIconW", reinterpret_cast<void*>(Shell_NotifyIconW));
    ldr.registerExport("shell32.dll", "Shell_NotifyIconA", reinterpret_cast<void*>(Shell_NotifyIconA));
    ldr.registerExport("shell32.dll", "SHGetFileInfoW", reinterpret_cast<void*>(SHGetFileInfoW));
    ldr.registerExport("shell32.dll", "SHGetFileInfoA", reinterpret_cast<void*>(SHGetFileInfoA));
    ldr.registerExport("shell32.dll", "ExtractIconW", reinterpret_cast<void*>(ExtractIconW));
    ldr.registerExport("shell32.dll", "ExtractIconA", reinterpret_cast<void*>(ExtractIconA));
    ldr.registerExport("shell32.dll", "SHCreateItemFromParsingName", reinterpret_cast<void*>(SHCreateItemFromParsingName));
    ldr.registerExport("shell32.dll", "SHParseDisplayName", reinterpret_cast<void*>(SHParseDisplayName));
    ldr.registerExport("shell32.dll", "SHOpenFolderAndSelectItems", reinterpret_cast<void*>(SHOpenFolderAndSelectItems));
    ldr.registerExport("shell32.dll", "SHFileOperationW", reinterpret_cast<void*>(SHFileOperationW));
    ldr.registerExport("shell32.dll", "DragQueryPoint", reinterpret_cast<void*>(DragQueryPoint));
    ldr.registerExport("shell32.dll", "DragQueryFileW", reinterpret_cast<void*>(DragQueryFileW));
    ldr.registerExport("shell32.dll", "DragQueryFileA", reinterpret_cast<void*>(DragQueryFileA));
    ldr.registerExport("shell32.dll", "SHGetPropertyStoreForWindow", reinterpret_cast<void*>(SHGetPropertyStoreForWindow));
    ldr.registerExport("shell32.dll", "DragFinish", reinterpret_cast<void*>(DragFinish));
    ldr.registerExport("shell32.dll", "SHDefExtractIconW", reinterpret_cast<void*>(SHDefExtractIconW));
    ldr.registerExport("shell32.dll", "SHGetIconOverlayIndexW", reinterpret_cast<void*>(SHGetIconOverlayIndexW));
    ldr.registerExport("shell32.dll", "ExtractIconExW", reinterpret_cast<void*>(ExtractIconExW));
    ldr.registerExport("shell32.dll", "SHGetDesktopFolder", reinterpret_cast<void*>(SHGetDesktopFolder));
    ldr.registerExport("shell32.dll", "SHGetSpecialFolderLocation", reinterpret_cast<void*>(SHGetSpecialFolderLocation));
    ldr.registerExport("shell32.dll", "SHChangeNotify", reinterpret_cast<void*>(SHChangeNotify));
    ldr.registerExport("shell32.dll", "SHGetPathFromIDListW", reinterpret_cast<void*>(SHGetPathFromIDListW));
    ldr.registerExport("shell32.dll", "SHBrowseForFolderW", reinterpret_cast<void*>(SHBrowseForFolderW));
    ldr.registerExportOrdinal("shell32.dll", 165, reinterpret_cast<void*>(ShellOrdinal165));
    ldr.registerExportOrdinal("shell32.dll", 16, reinterpret_cast<void*>(ShellOrdinal16));
    ldr.registerExport("shell32.dll", "FindExecutableW", reinterpret_cast<void*>(FindExecutableW));
    ldr.registerExport("shell32.dll", "SHFreeNameMappings", reinterpret_cast<void*>(SHFreeNameMappings));
    ldr.registerExportOrdinal("shell32.dll", 644, reinterpret_cast<void*>(ShellOrdinal644));
    ldr.registerExportOrdinal("shell32.dll", 645, reinterpret_cast<void*>(ShellOrdinal645));
    ldr.registerExport("shell32.dll", "SHAddToRecentDocs", reinterpret_cast<void*>(SHAddToRecentDocs));
    ldr.registerExportOrdinal("shell32.dll", 190, reinterpret_cast<void*>(ShellOrdinal190));
    ldr.registerExport("shell32.dll", "SHCreateDirectoryExA", reinterpret_cast<void*>(SHCreateDirectoryExA));
    ldr.registerExport("shell32.dll", "SHCreateDirectoryExW", reinterpret_cast<void*>(SHCreateDirectoryExW));
    ldr.registerExportOrdinal("shell32.dll", 2, reinterpret_cast<void*>(SHChangeNotifyRegister_Ordinal2));
    ldr.registerExportOrdinal("shell32.dll", 4, reinterpret_cast<void*>(SHChangeNotifyDeregister_Ordinal4));
    ldr.registerExport("shell32.dll", "SHCreateShellItem", reinterpret_cast<void*>(SHCreateShellItem));
    ldr.registerExport("shell32.dll", "ILFree", reinterpret_cast<void*>(ILFree));
    ldr.registerExportOrdinal("shell32.dll", 155, reinterpret_cast<void*>(ILFree));
    ldr.registerExport("shell32.dll", "SHGetPropertyStoreFromParsingName", reinterpret_cast<void*>(SHGetPropertyStoreFromParsingName));
    ldr.registerExport("shell32.dll", "SetCurrentProcessExplicitAppUserModelID", reinterpret_cast<void*>(SetCurrentProcessExplicitAppUserModelID));
    ldr.registerExport("shell32.dll", "CDefFolderMenu_Create2", reinterpret_cast<void*>(CDefFolderMenu_Create2));
    ldr.registerExportOrdinal("shell32.dll", 701, reinterpret_cast<void*>(CDefFolderMenu_Create2));
    ldr.registerExport("shell32.dll", "SHGetKnownFolderIDList", reinterpret_cast<void*>(SHGetKnownFolderIDList));
    ldr.registerExport("shell32.dll", "SHCreateItemFromIDList", reinterpret_cast<void*>(SHCreateItemFromIDList));
    ldr.registerExportOrdinal("shell32.dll", 6, reinterpret_cast<void*>(ShellOrdinal6));
    ldr.registerExportOrdinal("shell32.dll", 727, reinterpret_cast<void*>(ShellOrdinal727));
    ldr.registerExport("shell32.dll", "SHGetStockIconInfo", reinterpret_cast<void*>(SHGetStockIconInfo));
    ldr.registerExport("shell32.dll", "Shell_NotifyIconGetRect", reinterpret_cast<void*>(Shell_NotifyIconGetRect));

    // shlwapi.dll exports
    ldr.registerExport("shlwapi.dll", "wnsprintfW", reinterpret_cast<void*>(wnsprintfW));
    ldr.registerExport("shlwapi.dll", "PathSkipRootW", reinterpret_cast<void*>(PathSkipRootW));
    ldr.registerExport("shlwapi.dll", "SHCreateStreamOnFileW", reinterpret_cast<void*>(SHCreateStreamOnFileW));
    ldr.registerExport("shlwapi.dll", "PathIsRootW", reinterpret_cast<void*>(PathIsRootW));
    ldr.registerExport("shlwapi.dll", "SHRegGetUSValueW", reinterpret_cast<void*>(SHRegGetUSValueW));
    ldr.registerExport("shlwapi.dll", "PathFileExistsW", reinterpret_cast<void*>(PathFileExistsW));
    ldr.registerExport("shlwapi.dll", "PathFileExistsA", reinterpret_cast<void*>(PathFileExistsA));
    ldr.registerExport("shlwapi.dll", "PathCombineW", reinterpret_cast<void*>(PathCombineW));
    ldr.registerExport("shlwapi.dll", "PathCombineA", reinterpret_cast<void*>(PathCombineA));
    ldr.registerExport("shlwapi.dll", "PathFindFileNameW", reinterpret_cast<void*>(PathFindFileNameW));
    ldr.registerExport("shlwapi.dll", "PathFindFileNameA", reinterpret_cast<void*>(PathFindFileNameA));
    ldr.registerExport("shlwapi.dll", "PathFindExtensionW", reinterpret_cast<void*>(PathFindExtensionW));
    ldr.registerExport("shlwapi.dll", "PathFindExtensionA", reinterpret_cast<void*>(PathFindExtensionA));
    ldr.registerExport("shlwapi.dll", "PathRemoveFileSpecW", reinterpret_cast<void*>(PathRemoveFileSpecW));
    ldr.registerExport("shlwapi.dll", "PathRemoveFileSpecA", reinterpret_cast<void*>(PathRemoveFileSpecA));
    ldr.registerExport("shlwapi.dll", "PathAddBackslashW", reinterpret_cast<void*>(PathAddBackslashW));
    ldr.registerExport("shlwapi.dll", "PathAddBackslashA", reinterpret_cast<void*>(PathAddBackslashA));
    ldr.registerExport("shlwapi.dll", "PathIsRelativeW", reinterpret_cast<void*>(PathIsRelativeW));
    ldr.registerExport("shlwapi.dll", "PathIsRelativeA", reinterpret_cast<void*>(PathIsRelativeA));
    ldr.registerExport("shlwapi.dll", "PathIsDirectoryW", reinterpret_cast<void*>(PathIsDirectoryW));
    ldr.registerExport("shlwapi.dll", "PathIsDirectoryA", reinterpret_cast<void*>(PathIsDirectoryA));
    ldr.registerExport("shlwapi.dll", "StrStrW", reinterpret_cast<void*>(StrStrW));
    ldr.registerExport("shlwapi.dll", "StrStrIW", reinterpret_cast<void*>(StrStrIW));
    ldr.registerExport("shlwapi.dll", "StrRStrIW", reinterpret_cast<void*>(StrRStrIW));
    ldr.registerExport("shlwapi.dll", "StrStrIA", reinterpret_cast<void*>(StrStrIA));
    ldr.registerExport("shlwapi.dll", "StrCmpIW", reinterpret_cast<void*>(StrCmpIW));
    ldr.registerExport("shlwapi.dll", "StrCmpIA", reinterpret_cast<void*>(StrCmpIA));
    ldr.registerExport("shlwapi.dll", "PathCreateFromUrlW", reinterpret_cast<void*>(PathCreateFromUrlW));
    ldr.registerExport("shlwapi.dll", "AssocQueryStringW", reinterpret_cast<void*>(AssocQueryStringW));
    ldr.registerExport("shlwapi.dll", "PathGetDriveNumberW", reinterpret_cast<void*>(PathGetDriveNumberW));
    ldr.registerExport("shlwapi.dll", "ColorAdjustLuma", reinterpret_cast<void*>(ColorAdjustLuma));
    ldr.registerExport("shlwapi.dll", "PathCanonicalizeW", reinterpret_cast<void*>(PathCanonicalizeW));
    ldr.registerExport("shlwapi.dll", "PathAppendW", reinterpret_cast<void*>(PathAppendW));
    ldr.registerExport("shlwapi.dll", "PathMatchSpecW", reinterpret_cast<void*>(PathMatchSpecW));
    ldr.registerExport("shlwapi.dll", "PathIsNetworkPathW", reinterpret_cast<void*>(PathIsNetworkPathW));
    ldr.registerExport("shlwapi.dll", "PathCompactPathExW", reinterpret_cast<void*>(PathCompactPathExW));
    ldr.registerExport("shlwapi.dll", "ColorHLSToRGB", reinterpret_cast<void*>(ColorHLSToRGB));
    ldr.registerExport("shlwapi.dll", "ColorRGBToHLS", reinterpret_cast<void*>(ColorRGBToHLS));
    ldr.registerExport("shlwapi.dll", "PathStripPathW", reinterpret_cast<void*>(PathStripPathW));
    ldr.registerExport("shlwapi.dll", "PathRemoveExtensionW", reinterpret_cast<void*>(PathRemoveExtensionW));
    ldr.registerExport("shlwapi.dll", "AssocQueryKeyA", reinterpret_cast<void*>(AssocQueryKeyA));
    ldr.registerExport("shlwapi.dll", "PathAppendA", reinterpret_cast<void*>(PathAppendA));
    ldr.registerExport("shlwapi.dll", "PathStripPathA", reinterpret_cast<void*>(PathStripPathA));
    ldr.registerExport("shlwapi.dll", "StrRetToStrW", reinterpret_cast<void*>(StrRetToStrW));
    ldr.registerExport("shlwapi.dll", "SHDeleteValueW", reinterpret_cast<void*>(SHDeleteValueW));
    ldr.registerExport("shlwapi.dll", "SHDeleteKeyW", reinterpret_cast<void*>(SHDeleteKeyW));
    ldr.registerExport("shlwapi.dll", "SHSetValueW", reinterpret_cast<void*>(SHSetValueW));
    ldr.registerExport("shlwapi.dll", "SHGetValueW", reinterpret_cast<void*>(SHGetValueW));
    ldr.registerExport("shlwapi.dll", "UrlEscapeW", reinterpret_cast<void*>(UrlEscapeW));
    ldr.registerExport("shlwapi.dll", "QISearch", reinterpret_cast<void*>(QISearch));
    ldr.registerExportOrdinal("shlwapi.dll", 219, reinterpret_cast<void*>(QISearch));
    ldr.registerExport("shlwapi.dll", "PathStripToRootW", reinterpret_cast<void*>(PathStripToRootW));
    ldr.registerExport("shlwapi.dll", "StrCmpLogicalW", reinterpret_cast<void*>(StrCmpLogicalW));
    ldr.registerExport("shlwapi.dll", "PathGetCharTypeW", reinterpret_cast<void*>(PathGetCharTypeW));
    ldr.registerExport("shlwapi.dll", "UrlIsW", reinterpret_cast<void*>(UrlIsW));
    ldr.registerExport("shlwapi.dll", "SHAutoComplete", reinterpret_cast<void*>(SHAutoComplete));
    ldr.registerExport("shlwapi.dll", "PathCompactPathW", reinterpret_cast<void*>(PathCompactPathW));
    ldr.registerExport("shlwapi.dll", "StrFormatByteSizeW", reinterpret_cast<void*>(StrFormatByteSizeW));
    ldr.registerExport("shlwapi.dll", "SysAllocString", reinterpret_cast<void*>(SysAllocString_Shlwapi));
    ldr.registerExport("shlwapi.dll", "VariantCopyInd", reinterpret_cast<void*>(VariantCopyInd_Shlwapi));
    ldr.registerExportOrdinal("shlwapi.dll", 2, reinterpret_cast<void*>(SysAllocString_Shlwapi));
    ldr.registerExportOrdinal("shlwapi.dll", 12, reinterpret_cast<void*>(VariantCopyInd_Shlwapi));
    ldr.registerExport("shlwapi.dll", "StrTrimW", reinterpret_cast<void*>(StrTrimW));
    ldr.registerExport("shlwapi.dll", "StrChrW", reinterpret_cast<void*>(StrChrW));
    ldr.registerExport("shlwapi.dll", "PathIsUNCW", reinterpret_cast<void*>(PathIsUNCW));
    ldr.registerExport("api-ms-win-shcore-scaling-l1-1-1.dll", "SetProcessDpiAwareness", reinterpret_cast<void*>(SetProcessDpiAwareness));
    ldr.registerExport("api-ms-win-shcore-scaling-l1-1-1.dll", "GetDpiForMonitor", reinterpret_cast<void*>(GetDpiForMonitor));
    ldr.registerExport("shcore.dll", "GetDpiForMonitor", reinterpret_cast<void*>(GetDpiForMonitor));
    ldr.registerExport("api-ms-win-shcore-obsolete-l1-1-0.dll", "CommandLineToArgvW", reinterpret_cast<void*>(CommandLineToArgvW));
}

} // namespace micant::shell32
