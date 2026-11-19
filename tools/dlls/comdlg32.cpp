#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cderr.h>
#include <cwchar>
#include <cstdint>
#include <iterator>
#include <memory>

namespace {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
    defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
extern "C" __declspec(dllimport) void WINAPI MicaWin32kDebugInputTrace(
    uint32_t, uint32_t, uint32_t);
uint32_t g_dialogTraceStage = 200;
#endif

// CommDlgExtendedError reports the most recent failure in this DLL. The
// current single-process loader has no thread-local runtime contract, so this
// state is process-wide until the native thread-local storage path is wired.
DWORD g_extended_error = 0;

extern "C" __declspec(dllimport) int WINAPI MultiByteToWideChar(
    UINT codePage, DWORD flags, const char* input, int inputCount,
    wchar_t* output, int outputCount);
extern "C" __declspec(dllimport) int WINAPI WideCharToMultiByte(
    UINT codePage, DWORD flags, const wchar_t* input, int inputCount,
    char* output, int outputCount, const char* defaultCharacter,
    BOOL* usedDefaultCharacter);

constexpr UINT kAnsiCodePage = 0;
BOOL runFileDialog(OPENFILENAMEW* ofn, bool save);

bool convertAnsi(const char* source, wchar_t* destination, int capacity) {
    if (!source || !destination || capacity <= 0) return false;
    const int count = MultiByteToWideChar(kAnsiCodePage, 0, source, -1,
                                           destination, capacity);
    return count > 0;
}

bool convertWide(const wchar_t* source, char* destination, int capacity) {
    if (!source || !destination || capacity <= 0) return false;
    const int count = WideCharToMultiByte(kAnsiCodePage, 0, source, -1,
                                           destination, capacity, nullptr, nullptr);
    return count > 0;
}

WORD ansiOffset(const wchar_t* text, WORD wideOffset) {
    if (!text || !wideOffset) return 0;
    size_t wideCount = 0;
    size_t byteCount = 0;
    while (text[wideCount] && wideCount < wideOffset) {
        wchar_t character[2]{text[wideCount++], 0};
        char encoded[8]{};
        const int count = WideCharToMultiByte(kAnsiCodePage, 0, character, -1,
                                               encoded, sizeof(encoded), nullptr, nullptr);
        if (count <= 1) continue;
        byteCount += static_cast<size_t>(count - 1);
    }
    return byteCount <= UINT16_MAX ? static_cast<WORD>(byteCount) : 0;
}

bool convertAnsiMulti(const char* source, wchar_t* destination, int capacity) {
    if (!source || !destination || capacity <= 0) return false;
    size_t length = 0;
    while (length < 8191 && !(source[length] == '\0' && source[length + 1] == '\0'))
        ++length;
    if (length >= 8191 || length + 2 > static_cast<size_t>(capacity)) return false;
    return MultiByteToWideChar(kAnsiCodePage, 0, source, static_cast<int>(length + 2),
                               destination, capacity) > 0;
}

bool runAnsiFileDialog(OPENFILENAMEA* input, bool save) {
    g_extended_error = 0;
    if (!input || input->lStructSize != sizeof(*input)) {
        g_extended_error = input ? CDERR_STRUCTSIZE : CDERR_DIALOGFAILURE;
        return FALSE;
    }
    if (!input->lpstrFile || !input->nMaxFile) {
        g_extended_error = CDERR_STRUCTSIZE;
        return FALSE;
    }
    // The ANSI hook ABI carries ANSI notifications and cannot be forwarded to
    // the Unicode implementation without translating every notification.
    if (input->Flags & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE |
                        OFN_ENABLETEMPLATEHANDLE)) {
        g_extended_error = (input->Flags & OFN_ENABLEHOOK) ? CDERR_NOHOOK : CDERR_NOTEMPLATE;
        return FALSE;
    }
    if (input->Flags & OFN_ALLOWMULTISELECT) {
        g_extended_error = CDERR_DIALOGFAILURE;
        return FALSE;
    }

    wchar_t file[32768]{};
    wchar_t dialogTitle[MAX_PATH]{};
    wchar_t fileTitle[MAX_PATH]{};
    wchar_t initialDir[32768]{};
    wchar_t defaultExt[64]{};
    wchar_t filter[8192]{};
    wchar_t customFilter[8192]{};
    if (input->lpstrFile && input->nMaxFile &&
        !convertAnsi(input->lpstrFile, file, static_cast<int>(std::size(file)))) {
        g_extended_error = FNERR_INVALIDFILENAME;
        return FALSE;
    }
    if (input->lpstrTitle && !convertAnsi(input->lpstrTitle, dialogTitle,
                                          static_cast<int>(std::size(dialogTitle)))) {
        g_extended_error = CDERR_INITIALIZATION;
        return FALSE;
    }
    if (input->lpstrInitialDir && !convertAnsi(input->lpstrInitialDir, initialDir,
                                               static_cast<int>(std::size(initialDir)))) {
        g_extended_error = CDERR_INITIALIZATION;
        return FALSE;
    }
    if (input->lpstrDefExt && !convertAnsi(input->lpstrDefExt, defaultExt,
                                           static_cast<int>(std::size(defaultExt)))) {
        g_extended_error = CDERR_INITIALIZATION;
        return FALSE;
    }
    if (input->lpstrFilter && !convertAnsiMulti(input->lpstrFilter, filter,
                                                static_cast<int>(std::size(filter)))) {
        g_extended_error = CDERR_INITIALIZATION;
        return FALSE;
    }
    if (input->lpstrCustomFilter && !convertAnsiMulti(input->lpstrCustomFilter, customFilter,
                                                      static_cast<int>(std::size(customFilter)))) {
        g_extended_error = CDERR_INITIALIZATION;
        return FALSE;
    }

    OPENFILENAMEW wide{};
    wide.lStructSize = sizeof(wide);
    wide.hwndOwner = input->hwndOwner;
    wide.hInstance = input->hInstance;
    wide.lpstrFilter = input->lpstrFilter ? filter : nullptr;
    wide.lpstrCustomFilter = input->lpstrCustomFilter ? customFilter : nullptr;
    wide.nMaxCustFilter = input->nMaxCustFilter ? static_cast<DWORD>(std::size(customFilter)) : 0;
    wide.nFilterIndex = input->nFilterIndex;
    wide.lpstrFile = file;
    wide.nMaxFile = static_cast<DWORD>(std::size(file));
    wide.lpstrFileTitle = input->lpstrFileTitle ? fileTitle : nullptr;
    wide.nMaxFileTitle = input->lpstrFileTitle ? static_cast<DWORD>(std::size(fileTitle)) : 0;
    wide.lpstrInitialDir = input->lpstrInitialDir ? initialDir : nullptr;
    wide.lpstrTitle = input->lpstrTitle ? dialogTitle : nullptr;
    wide.Flags = input->Flags;
    wide.nFileOffset = input->nFileOffset;
    wide.nFileExtension = input->nFileExtension;
    wide.lpstrDefExt = input->lpstrDefExt ? defaultExt : nullptr;
    wide.lCustData = input->lCustData;
    wide.lpfnHook = nullptr;
    wide.lpTemplateName = nullptr;
    if (!runFileDialog(&wide, save)) return FALSE;

    if (!convertWide(file, input->lpstrFile, static_cast<int>(input->nMaxFile)) ||
        (input->lpstrFileTitle && input->nMaxFileTitle &&
         !convertWide(fileTitle, input->lpstrFileTitle, input->nMaxFileTitle))) {
        input->lpstrFile[0] = '\0';
        g_extended_error = FNERR_BUFFERTOOSMALL;
        return FALSE;
    }
    input->nFileOffset = ansiOffset(file, wide.nFileOffset);
    input->nFileExtension = wide.nFileExtension
        ? static_cast<WORD>(ansiOffset(file, wide.nFileExtension - 1) + 1) : 0;
    input->nFilterIndex = wide.nFilterIndex;
    return TRUE;
}

BOOL fail_dialog() {
    g_extended_error = CDERR_DIALOGFAILURE;
    return FALSE;
}

constexpr wchar_t kDialogClass[] = L"LuceOS.CommonFileDialog";
constexpr wchar_t kFileViewClass[] = L"LuceOS.CommonDialogFileView";
constexpr int kPathEdit = 100;
constexpr int kNameEdit = 101;
constexpr int kUpButton = 102;
constexpr int kFilterButton = 103;
constexpr int kPlaceHistory = 104;
constexpr int kPlaceDesktop = 105;
constexpr int kPlaceDocuments = 106;
constexpr int kPlaceComputer = 107;
constexpr int kPlaceNetwork = 108;
constexpr int kFileView = 109;
constexpr int kNameHeader = 110;
constexpr int kFileSlots = 18;
constexpr int kFilterSlots = 8;
constexpr DWORD kDirectoryAttribute = 0x10;
constexpr DWORD kInvalidAttributes = 0xFFFFFFFFu;

struct FileRow {
    wchar_t name[MAX_PATH]{};
    HICON icon{};
    bool directory{};
};

struct FileDialogState {
    HWND window{};
    HWND owner{};
    HWND pathEdit{};
    HWND pathLabel{};
    HWND upButton{};
    HWND nameEdit{};
    HWND nameLabel{};
    HWND openButton{};
    HWND cancelButton{};
    HWND fileTypeLabel{};
    HWND filterControl{};
    HWND fileView{};
    HWND nameHeader{};
    HWND placeButtons[5]{};
    OPENFILENAMEW* ofn{};
    wchar_t directory[32768]{};
    wchar_t pattern[260]{L"*.*"};
    wchar_t filterNames[kFilterSlots][128]{};
    wchar_t filterPatterns[kFilterSlots][260]{};
    FileRow rows[kFileSlots]{};
    size_t rowCount{};
    size_t selectedRow{};
    size_t filterCount{};
    size_t filterIndex{};
    bool save{};
    bool done{};
    bool accepted{};
    bool hasSelection{};
};

void refreshRows(FileDialogState& state);
void acceptSelection(FileDialogState& state);
size_t boundedLength(const wchar_t* text, size_t maximum);
bool joinPath(const wchar_t* directory, const wchar_t* leaf,
              wchar_t* output, size_t capacity);
bool setDirectory(FileDialogState& state, const wchar_t* path);

FileDialogState* g_activeDialog{};
ATOM g_dialogClassAtom{};
ATOM g_fileViewClassAtom{};
struct HeapFreeDeleter {
    void operator()(wchar_t* value) const noexcept {
        if (value) (void)HeapFree(GetProcessHeap(), 0, value);
    }
};
using HeapWideBuffer = std::unique_ptr<wchar_t, HeapFreeDeleter>;

HeapWideBuffer allocateWideBuffer(size_t characters) {
    return HeapWideBuffer(static_cast<wchar_t*>(HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, characters * sizeof(wchar_t))));
}

void debugDialog(const wchar_t* stage) {
    OutputDebugStringW(stage);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
    defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
    uint32_t hash = 2166136261u, length{};
    while (stage && stage[length] && length < 256) {
        hash = (hash ^ static_cast<uint16_t>(stage[length++])) * 16777619u;
    }
    MicaWin32kDebugInputTrace(g_dialogTraceStage++, hash, length);
#endif
}

void fillFileViewRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (!brush) return;
    (void)FillRect(dc, &rect, brush);
    (void)DeleteObject(brush);
}

void releaseFileIcons(FileDialogState& state) {
    for (auto& row : state.rows) {
        if (row.icon) DestroyIcon(row.icon);
        row = {};
    }
    state.rowCount = 0;
    state.hasSelection = false;
}

void drawFileGlyph(HDC dc, int x, int y, bool directory) {
    if (directory) {
        fillFileViewRect(dc, {x + 1, y + 1, x + 9, y + 5}, RGB(189, 129, 23));
        fillFileViewRect(dc, {x + 2, y + 3, x + 19, y + 15}, RGB(232, 178, 54));
        fillFileViewRect(dc, {x + 3, y + 4, x + 18, y + 6}, RGB(255, 221, 112));
        fillFileViewRect(dc, {x + 2, y + 14, x + 19, y + 16}, RGB(178, 119, 24));
    } else {
        fillFileViewRect(dc, {x + 4, y + 1, x + 17, y + 17}, RGB(115, 115, 115));
        fillFileViewRect(dc, {x + 5, y + 2, x + 16, y + 16}, RGB(255, 255, 255));
        fillFileViewRect(dc, {x + 7, y + 7, x + 14, y + 8}, RGB(87, 127, 173));
        fillFileViewRect(dc, {x + 7, y + 10, x + 14, y + 11}, RGB(87, 127, 173));
        fillFileViewRect(dc, {x + 7, y + 13, x + 12, y + 14}, RGB(87, 127, 173));
    }
}

void selectFileRow(FileDialogState& state, size_t index) {
    if (index >= state.rowCount) return;
    state.selectedRow = index;
    state.hasSelection = true;
    if (!state.rows[index].directory)
        SetWindowTextW(state.nameEdit, state.rows[index].name);
    if (state.fileView) (void)InvalidateRect(state.fileView, nullptr, TRUE);
}

void activateFileRow(FileDialogState& state, size_t index) {
    if (index >= state.rowCount) return;
    selectFileRow(state, index);
    const FileRow& row = state.rows[index];
    if (row.directory) {
        auto next = allocateWideBuffer(32768);
        if (next && joinPath(state.directory, row.name, next.get(), 32768) &&
            setDirectory(state, next.get())) refreshRows(state);
        return;
    }
    acceptSelection(state);
}

LRESULT CALLBACK fileViewWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FileDialogState* state = g_activeDialog;
    if (!state || state->fileView != window)
        return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (!dc) return 0;
        RECT client{};
        GetClientRect(window, &client);
        fillFileViewRect(dc, client, RGB(255, 255, 255));
        fillFileViewRect(dc, {0, 0, client.right, 1}, RGB(64, 64, 64));
        fillFileViewRect(dc, {0, 0, 1, client.bottom}, RGB(64, 64, 64));
        fillFileViewRect(dc, {0, client.bottom - 1, client.right, client.bottom}, RGB(255, 255, 255));
        fillFileViewRect(dc, {client.right - 1, 0, client.right, client.bottom}, RGB(255, 255, 255));
        for (size_t i = 0; i < state->rowCount; ++i) {
            const size_t column = i / 10;
            const size_t line = i % 10;
            const int x = 8 + static_cast<int>(column) * 306;
            const int y = 38 + static_cast<int>(line) * 24;
            const bool selected = state->hasSelection && state->selectedRow == i;
            if (selected) fillFileViewRect(dc, {x, y, x + 300, y + 22}, RGB(49, 106, 197));
            if (!state->rows[i].icon ||
                !DrawIconEx(dc, x, y + 1, state->rows[i].icon, 16, 16, 0,
                            nullptr, DI_NORMAL))
                drawFileGlyph(dc, x, y + 1, state->rows[i].directory);
            const size_t length = boundedLength(state->rows[i].name,
                                                std::size(state->rows[i].name));
            if (length) {
                const COLORREF oldColor = SetTextColor(dc,
                    selected ? RGB(255, 255, 255) : RGB(0, 0, 0));
                const int oldMode = SetBkMode(dc, 1);
                (void)TextOutW(dc, x + 25, y + 2, state->rows[i].name,
                               static_cast<int>(length));
                (void)SetBkMode(dc, oldMode);
                (void)SetTextColor(dc, oldColor);
            }
        }
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
    defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
        MicaWin32kDebugInputTrace(250, static_cast<uint32_t>(state->rowCount),
                                  static_cast<uint32_t>(client.right));
#endif
        (void)EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK) {
        const int x = static_cast<int16_t>(LOWORD(lParam));
        const int y = static_cast<int16_t>(HIWORD(lParam));
        if (x >= 8 && y >= 38) {
            const size_t column = static_cast<size_t>(x - 8) / 306;
            const size_t line = static_cast<size_t>(y - 38) / 24;
            if (column < 2 && line < 10) {
                if (message == WM_LBUTTONDBLCLK)
                    activateFileRow(*state, column * 10 + line);
                else
                    selectFileRow(*state, column * 10 + line);
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

size_t boundedLength(const wchar_t* text, size_t maximum) {
    size_t length = 0;
    if (text) while (length < maximum && text[length]) ++length;
    return length;
}

bool copyText(wchar_t* destination, size_t capacity, const wchar_t* source) {
    if (!destination || !capacity || !source) return false;
    const size_t length = boundedLength(source, capacity);
    if (length >= capacity) { destination[0] = L'\0'; return false; }
    for (size_t i = 0; i <= length; ++i) destination[i] = source[i];
    return true;
}

bool isSeparator(wchar_t value) { return value == L'\\' || value == L'/'; }
wchar_t foldCase(wchar_t value) {
    return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A') : value;
}

bool joinPath(const wchar_t* directory, const wchar_t* leaf,
              wchar_t* output, size_t capacity) {
    if (!directory || !leaf || !output || !capacity) return false;
    const size_t dirLength = boundedLength(directory, capacity);
    const size_t leafLength = boundedLength(leaf, capacity);
    const bool separator = dirLength && !isSeparator(directory[dirLength - 1]);
    const size_t needed = dirLength + (separator ? 1u : 0u) + leafLength + 1u;
    if (dirLength >= capacity || leafLength >= capacity || needed > capacity) return false;
    size_t at = 0;
    for (size_t i = 0; i < dirLength; ++i) output[at++] = directory[i];
    if (separator) output[at++] = L'\\';
    for (size_t i = 0; i < leafLength; ++i) output[at++] = leaf[i];
    output[at] = L'\0';
    return true;
}

bool isAbsolutePath(const wchar_t* path) {
    return path && ((path[0] && isSeparator(path[0])) ||
        (path[0] && path[1] == L':' && isSeparator(path[2])));
}

bool setDirectory(FileDialogState& state, const wchar_t* path) {
    constexpr size_t kPathCapacity = 32768;
    constexpr size_t kPathBytes = kPathCapacity * sizeof(wchar_t);
    auto* full = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kPathBytes));
    auto* combined = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kPathBytes));
    if (!full || !combined) {
        if (full) HeapFree(GetProcessHeap(), 0, full);
        if (combined) HeapFree(GetProcessHeap(), 0, combined);
        return false;
    }
    if (!path || !*path) {
        HeapFree(GetProcessHeap(), 0, full); HeapFree(GetProcessHeap(), 0, combined);
        return false;
    }
    if (isAbsolutePath(path)) {
        if (!GetFullPathNameW(path, static_cast<DWORD>(kPathCapacity), full, nullptr)) {
            HeapFree(GetProcessHeap(), 0, full); HeapFree(GetProcessHeap(), 0, combined);
            return false;
        }
    } else {
        if (!joinPath(state.directory, path, combined, kPathCapacity) ||
            !GetFullPathNameW(combined, static_cast<DWORD>(kPathCapacity), full, nullptr)) {
            HeapFree(GetProcessHeap(), 0, full); HeapFree(GetProcessHeap(), 0, combined);
            return false;
        }
    }
    const DWORD attributes = GetFileAttributesW(full);
    const bool valid = attributes != kInvalidAttributes && (attributes & kDirectoryAttribute);
    const bool copied = valid && copyText(state.directory, std::size(state.directory), full);
    HeapFree(GetProcessHeap(), 0, full); HeapFree(GetProcessHeap(), 0, combined);
    return copied;
}

bool wildcardMatch(const wchar_t* pattern, const wchar_t* name) {
    // DOS file dialogs commonly use * and ?; this bounded matcher supports
    // semicolon-separated patterns through the caller's pattern selection.
    const wchar_t* star = nullptr;
    const wchar_t* retry = nullptr;
    while (*name) {
        if (*pattern == L'?' || foldCase(*pattern) == foldCase(*name)) {
            ++pattern; ++name;
        } else if (*pattern == L'*') {
            star = pattern++;
            retry = name;
        } else if (star) {
            pattern = star + 1;
            name = ++retry;
        } else return false;
    }
    while (*pattern == L'*') ++pattern;
    return *pattern == L'\0';
}

bool matchesFilter(const wchar_t* patterns, const wchar_t* name) {
    wchar_t one[260]{};
    size_t count = 0;
    for (size_t i = 0; patterns[i] && i < 4096; ++i) {
        if (patterns[i] == L';' || patterns[i] == L'\0') {
            one[count] = L'\0';
            if (count && wildcardMatch(one, name)) return true;
            count = 0;
        } else if (count + 1 < std::size(one)) one[count++] = patterns[i];
    }
    one[count] = L'\0';
    return count && wildcardMatch(one, name);
}

void notifyHook(FileDialogState& state, UINT message, WPARAM wParam, LPARAM lParam) {
    if (state.ofn && (state.ofn->Flags & OFN_ENABLEHOOK) && state.ofn->lpfnHook)
        (void)state.ofn->lpfnHook(state.window, message, wParam, lParam);
}

void refreshRows(FileDialogState& state) {
    if (!state.window) return;
    releaseFileIcons(state);
    SetWindowTextW(state.pathEdit, state.directory);

    WIN32_FIND_DATAW data{};
    constexpr size_t kPathCapacity = 32768;
    auto* search = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        kPathCapacity * sizeof(wchar_t)));
    if (!search) return;
    if (!joinPath(state.directory, L"*", search, kPathCapacity)) {
        HeapFree(GetProcessHeap(), 0, search); return;
    }
    HANDLE find = FindFirstFileW(search, &data);
    HeapFree(GetProcessHeap(), 0, search);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (!data.cFileName[0] ||
            (data.cFileName[0] == L'.' &&
             (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2]))))
            continue;
        const bool directory = (data.dwFileAttributes & kDirectoryAttribute) != 0;
        if (!directory && !matchesFilter(state.pattern, data.cFileName)) continue;
        if (state.rowCount >= kFileSlots) break;
        FileRow& row = state.rows[state.rowCount];
        if (!copyText(row.name, std::size(row.name), data.cFileName)) continue;
        bool duplicate{};
        for (size_t previous = 0; previous < state.rowCount; ++previous) {
            const wchar_t* left = state.rows[previous].name;
            const wchar_t* right = row.name;
            while (*left && *right && foldCase(*left) == foldCase(*right)) {
                ++left; ++right;
            }
            if (!*left && !*right) { duplicate = true; break; }
        }
        if (duplicate) continue;
        row.directory = directory;
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
    defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
        uint32_t nameHash = 2166136261u;
        for (size_t i = 0; row.name[i]; ++i)
            nameHash = (nameHash ^ static_cast<uint16_t>(row.name[i])) * 16777619u;
        MicaWin32kDebugInputTrace(251, static_cast<uint32_t>(state.rowCount), nameHash);
#endif
        wchar_t fullPath[32768]{};
        SHFILEINFOW shellInfo{};
        if (joinPath(state.directory, row.name, fullPath, std::size(fullPath))) {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
    defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
            MicaWin32kDebugInputTrace(252, static_cast<uint32_t>(state.rowCount),
                                      data.dwFileAttributes);
#endif
            const DWORD_PTR iconResult = SHGetFileInfoW(fullPath, data.dwFileAttributes, &shellInfo,
                sizeof(shellInfo), SHGFI_ICON | SHGFI_SMALLICON |
                    SHGFI_USEFILEATTRIBUTES);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
    defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
            MicaWin32kDebugInputTrace(253, static_cast<uint32_t>(state.rowCount),
                                      iconResult ? 1u : 0u);
#endif
            if (iconResult) row.icon = shellInfo.hIcon;
        }
        if (state.rowCount < kFileSlots) ++state.rowCount;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (state.fileView) (void)InvalidateRect(state.fileView, nullptr, TRUE);
}

bool moveToParent(FileDialogState& state) {
    wchar_t parent[32768]{};
    if (!copyText(parent, std::size(parent), state.directory)) return false;
    size_t length = boundedLength(parent, std::size(parent));
    while (length > 3 && isSeparator(parent[length - 1])) parent[--length] = 0;
    while (length && !isSeparator(parent[length - 1])) parent[--length] = 0;
    if (!length) return false;
    if (!setDirectory(state, parent)) return false;
    refreshRows(state);
    return true;
}

bool navigatePlace(FileDialogState& state, int place) {
    if (place == kPlaceHistory) return (refreshRows(state), true);
    if (place == kPlaceComputer) {
        if (state.directory[0] && state.directory[1] == L':' &&
            isSeparator(state.directory[2])) {
            wchar_t root[] = {state.directory[0], L':', L'\\', 0};
            if (setDirectory(state, root)) {
                refreshRows(state);
                return true;
            }
        }
        return false;
    }
    if (place == kPlaceDesktop || place == kPlaceDocuments) {
        wchar_t profile[32768]{};
        if (!GetEnvironmentVariableW(L"USERPROFILE", profile,
                                     static_cast<DWORD>(std::size(profile))))
            return false;
        wchar_t target[32768]{};
        const wchar_t* suffix = place == kPlaceDesktop ? L"Desktop" : L"Documents";
        if (!joinPath(profile, suffix, target, std::size(target)) ||
            !setDirectory(state, target)) return false;
        refreshRows(state);
        return true;
    }
    return false;
}

void updateFilterLabel(FileDialogState& state) {
    if (!state.window || !state.filterCount) return;
    if (!state.filterControl) return;
    wchar_t label[256]{};
    const auto index = state.filterIndex % state.filterCount;
    const size_t nameLength = boundedLength(state.filterNames[index], 96);
    size_t at = 0;
    for (size_t i = 0; i < nameLength && at + 1 < std::size(label); ++i)
        label[at++] = state.filterNames[index][i];
    if (at + 3 < std::size(label)) {
        label[at++] = L' ';
        label[at++] = 0x25BE; // small down-pointing triangle
        label[at] = 0;
    }
    SetWindowTextW(state.filterControl, label);
}

void closeDialog(FileDialogState& state, bool accept) {
    state.accepted = accept;
    state.done = true;
    releaseFileIcons(state);
    if (state.window) DestroyWindow(state.window);
}

void repaintNow(HWND control) {
    if (!control) return;
    (void)SetWindowPos(control, reinterpret_cast<HWND>(0), 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    (void)InvalidateRect(control, nullptr, TRUE);
    (void)UpdateWindow(control);
}

void acceptSelection(FileDialogState& state) {
    constexpr size_t kPathCapacity = 32768;
    auto requestedDirectory = allocateWideBuffer(kPathCapacity);
    auto path = allocateWideBuffer(kPathCapacity);
    auto parent = allocateWideBuffer(kPathCapacity);
    if (!requestedDirectory || !path || !parent) {
        MessageBoxW(state.window, L"Not enough memory to open the file dialog.",
            state.save ? L"Save As" : L"Open", MB_OK | MB_ICONERROR);
        return;
    }
    if (GetWindowTextW(state.pathEdit, requestedDirectory.get(),
            static_cast<int>(kPathCapacity)) > 0 &&
        wcscmp(requestedDirectory.get(), state.directory) != 0) {
        if (!setDirectory(state, requestedDirectory.get())) {
            MessageBoxW(state.window, L"The specified folder does not exist.",
                state.save ? L"Save As" : L"Open", MB_OK | MB_ICONERROR);
            return;
        }
    }
    wchar_t name[MAX_PATH]{};
    if (!GetWindowTextW(state.nameEdit, name, static_cast<int>(std::size(name))) || !name[0]) {
        debugDialog(L"comdlg:empty-name");
        SetFocus(state.nameEdit);
        return;
    }
    debugDialog(L"comdlg:name-read");
    if (isAbsolutePath(name)) {
        if (!GetFullPathNameW(name, static_cast<DWORD>(kPathCapacity), path.get(), nullptr)) return;
    } else if (!joinPath(state.directory, name, path.get(), kPathCapacity)) return;

    if (state.save && state.ofn->lpstrDefExt && *state.ofn->lpstrDefExt) {
        const size_t length = boundedLength(path.get(), kPathCapacity);
        size_t leaf = length;
        while (leaf && !isSeparator(path.get()[leaf - 1])) --leaf;
        bool hasExtension = false;
        for (size_t i = leaf; i < length; ++i) if (path.get()[i] == L'.') hasExtension = true;
        const size_t extensionLength = boundedLength(state.ofn->lpstrDefExt, 32);
        if (!hasExtension && length + extensionLength + 2 < kPathCapacity) {
            path.get()[length] = L'.';
            for (size_t i = 0; i <= extensionLength; ++i)
                path.get()[length + 1 + i] = state.ofn->lpstrDefExt[i];
        }
    }

    if (state.ofn->Flags & OFN_PATHMUSTEXIST) {
        if (!copyText(parent.get(), kPathCapacity, path.get())) return;
        size_t length = boundedLength(parent.get(), kPathCapacity);
        while (length && !isSeparator(parent.get()[length - 1])) --length;
        if (!length) return;
        if (length == 3 && parent.get()[1] == L':') parent.get()[length] = L'\0';
        else parent.get()[length - 1] = L'\0';
        const DWORD attributes = GetFileAttributesW(parent.get());
        #if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
            defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
        MicaWin32kDebugInputTrace(g_dialogTraceStage++, attributes, 0x50415448u);
        #endif
        if (attributes == kInvalidAttributes || !(attributes & kDirectoryAttribute)) {
            MessageBoxW(state.window, L"The specified path does not exist.",
                state.save ? L"Save As" : L"Open", MB_OK | MB_ICONERROR);
            return;
        }
    }

    DWORD attributes = GetFileAttributesW(path.get());
    debugDialog(L"comdlg:target-checked");
    if (!state.save && (state.ofn->Flags & OFN_FILEMUSTEXIST) &&
        (attributes == kInvalidAttributes || (attributes & kDirectoryAttribute))) {
        MessageBoxW(state.window, L"The selected file does not exist.", L"Open",
            MB_OK | MB_ICONERROR);
        return;
    }
    if (state.save && attributes != kInvalidAttributes &&
        !(attributes & kDirectoryAttribute) && (state.ofn->Flags & OFN_OVERWRITEPROMPT)) {
        if (MessageBoxW(state.window, L"This file already exists. Replace it?", L"Confirm Save As",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    }

    const size_t length = boundedLength(path.get(), kPathCapacity);
    if (length + 1 > state.ofn->nMaxFile || !state.ofn->lpstrFile) {
        g_extended_error = FNERR_BUFFERTOOSMALL;
        return;
    }
    for (size_t i = 0; i <= length; ++i) state.ofn->lpstrFile[i] = path.get()[i];
    state.ofn->nFileOffset = 0;
    for (size_t i = 0; i < length; ++i)
        if (isSeparator(path.get()[i])) state.ofn->nFileOffset = static_cast<WORD>(i + 1);
    state.ofn->nFileExtension = 0;
    for (size_t i = state.ofn->nFileOffset; i < length; ++i)
        if (path.get()[i] == L'.') { state.ofn->nFileExtension = static_cast<WORD>(i + 1); break; }
    if (state.ofn->lpstrFileTitle && state.ofn->nMaxFileTitle) {
        const wchar_t* title = path.get() + state.ofn->nFileOffset;
        copyText(state.ofn->lpstrFileTitle, state.ofn->nMaxFileTitle, title);
    }
    state.ofn->nFilterIndex = static_cast<DWORD>(state.filterIndex + 1);
    debugDialog(L"comdlg:accepted");
    if (state.ofn->Flags & OFN_ENABLEHOOK) {
        OFNOTIFYW notification{};
        notification.hdr.hwndFrom = state.window;
        notification.hdr.code = CDN_FILEOK;
        notifyHook(state, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&notification));
    }
    closeDialog(state, true);
}

LRESULT CALLBACK fileDialogWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FileDialogState* state = g_activeDialog;
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    if (!state->window && message == WM_NCCREATE) {
        state->window = window;
        return TRUE;
    }
    if (state->window != window) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_CLOSE) { closeDialog(*state, false); return 0; }
    if (message == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) { closeDialog(*state, false); return 0; }
        if (wParam == VK_RETURN) { acceptSelection(*state); return 0; }
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == IDOK) { debugDialog(L"comdlg:idok"); acceptSelection(*state); return 0; }
        if (id == IDCANCEL) { debugDialog(L"comdlg:cancel"); closeDialog(*state, false); return 0; }
        if (id == kUpButton && HIWORD(wParam) == BN_CLICKED) {
            (void)moveToParent(*state);
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED &&
            id >= kPlaceHistory && id <= kPlaceNetwork) {
            (void)navigatePlace(*state, id);
            return 0;
        }
        if (id == kFilterButton && HIWORD(wParam) == BN_CLICKED &&
            state->filterCount) {
            state->filterIndex = (state->filterIndex + 1) % state->filterCount;
            copyText(state->pattern, std::size(state->pattern),
                     state->filterPatterns[state->filterIndex]);
            updateFilterLabel(*state);
            refreshRows(*state);
            return 0;
        }
        if (id == kPathEdit && HIWORD(wParam) == EN_CHANGE) return 0;
    }
    if (message == WM_CREATE) {
        state->pathLabel = CreateWindowExW(0, L"STATIC", L"Look in:", WS_CHILD | WS_VISIBLE,
            78, 14, 64, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->directory,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            142, 10, 350, 26, window, reinterpret_cast<HMENU>(kPathEdit),
            GetModuleHandleW(nullptr), nullptr);
        state->upButton = CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_PUSHBUTTON, 502, 10, 48, 26, window,
            reinterpret_cast<HMENU>(kUpButton), GetModuleHandleW(nullptr), nullptr);
        // Classic Common Dialog places bar.
        CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE, 10, 50, 108, 300, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        state->placeButtons[0] = CreateWindowExW(0, L"BUTTON", L"History", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_PUSHBUTTON, 18, 58, 92, 56, window,
            reinterpret_cast<HMENU>(kPlaceHistory), GetModuleHandleW(nullptr), nullptr);
        state->placeButtons[1] = CreateWindowExW(0, L"BUTTON", L"Desktop", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_PUSHBUTTON, 18, 116, 92, 56, window,
            reinterpret_cast<HMENU>(kPlaceDesktop), GetModuleHandleW(nullptr), nullptr);
        state->placeButtons[2] = CreateWindowExW(0, L"BUTTON", L"My Documents", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_PUSHBUTTON, 18, 174, 92, 56, window,
            reinterpret_cast<HMENU>(kPlaceDocuments), GetModuleHandleW(nullptr), nullptr);
        state->placeButtons[3] = CreateWindowExW(0, L"BUTTON", L"My Computer", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_PUSHBUTTON, 18, 232, 92, 56, window,
            reinterpret_cast<HMENU>(kPlaceComputer), GetModuleHandleW(nullptr), nullptr);
        state->placeButtons[4] = CreateWindowExW(0, L"BUTTON", L"My Network Places",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            18, 290, 92, 56, window, reinterpret_cast<HMENU>(kPlaceNetwork),
            GetModuleHandleW(nullptr), nullptr);
        if (state->placeButtons[4]) EnableWindow(state->placeButtons[4], FALSE);
        HWND historyPlace = GetDlgItem(window, kPlaceHistory);
        if (historyPlace) EnableWindow(historyPlace, FALSE);

        state->fileView = CreateWindowExW(0, kFileViewClass, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 124, 50, 626, 300, window,
            reinterpret_cast<HMENU>(kFileView), GetModuleHandleW(nullptr), nullptr);
        state->nameHeader = CreateWindowExW(0, L"STATIC", L"Name",
            WS_CHILD | WS_VISIBLE, 132, 58, 600, 20, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        state->nameLabel = CreateWindowExW(0, L"STATIC", L"File name:", WS_CHILD | WS_VISIBLE,
            124, 359, 130, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->nameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            256, 355, 370, 26, window, reinterpret_cast<HMENU>(kNameEdit),
            GetModuleHandleW(nullptr), nullptr);
        state->openButton = CreateWindowExW(0, L"BUTTON", state->save ? L"Save" : L"Open",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            642, 355, 96, 28, window, reinterpret_cast<HMENU>(IDOK),
            GetModuleHandleW(nullptr), nullptr);
        state->cancelButton = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            642, 387, 96, 28, window, reinterpret_cast<HMENU>(IDCANCEL),
            GetModuleHandleW(nullptr), nullptr);
        state->fileTypeLabel = CreateWindowExW(0, L"STATIC", L"Files of type:", WS_CHILD | WS_VISIBLE,
            124, 391, 130, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->filterControl = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            256, 387, 370, 26, window, reinterpret_cast<HMENU>(kFilterButton),
            GetModuleHandleW(nullptr), nullptr);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

BOOL runFileDialog(OPENFILENAMEW* ofn, bool save) {
    debugDialog(L"comdlg:entry");
    g_extended_error = 0;
    if (!ofn) {
        debugDialog(L"comdlg:null-structure");
        g_extended_error = CDERR_STRUCTSIZE;
        return FALSE;
    }
    #if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
        defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
    MicaWin32kDebugInputTrace(g_dialogTraceStage++, ofn->lStructSize, ofn->nMaxFile);
    #endif
    if (ofn->lStructSize != sizeof(*ofn)) {
        debugDialog(L"comdlg:bad-structure-size");
        g_extended_error = CDERR_STRUCTSIZE;
        return FALSE;
    }
    if (!ofn->lpstrFile || !ofn->nMaxFile) {
        debugDialog(L"comdlg:bad-file-buffer");
        g_extended_error = CDERR_STRUCTSIZE;
        return FALSE;
    }
    if ((ofn->Flags & OFN_ENABLEHOOK) && !ofn->lpfnHook) {
        debugDialog(L"comdlg:bad-hook");
        g_extended_error = CDERR_NOHOOK;
        return FALSE;
    }
    if ((ofn->Flags & (OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE)) &&
        !(ofn->Flags & OFN_ENABLEHOOK)) {
        debugDialog(L"comdlg:bad-template");
        g_extended_error = CDERR_NOTEMPLATE;
        return FALSE;
    }
    if (g_activeDialog) { debugDialog(L"comdlg:already-active"); g_extended_error = CDERR_DIALOGFAILURE; return FALSE; }

    auto* stateStorage = static_cast<FileDialogState*>(HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FileDialogState)));
    auto initial = allocateWideBuffer(32768);
    if (!stateStorage || !initial) {
        #if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE) || \
            defined(MICANT_BOOTSTRAP_REACTOS_APP_COMDLGTEST_TRACE)
        MicaWin32kDebugInputTrace(g_dialogTraceStage++, stateStorage ? 1u : 0u,
                                  initial ? 1u : 0u);
        #endif
        if (stateStorage) HeapFree(GetProcessHeap(), 0, stateStorage);
        g_extended_error = CDERR_DIALOGFAILURE;
        return FALSE;
    }
    debugDialog(L"comdlg:allocated");
    FileDialogState& state = *stateStorage;
    auto releaseStorage = [&]() {
        HeapFree(GetProcessHeap(), 0, stateStorage);
    };
    state.ofn = ofn;
    state.owner = ofn->hwndOwner;
    state.save = save;
    if (!GetCurrentDirectoryW(static_cast<DWORD>(std::size(state.directory)), state.directory)) {
        debugDialog(L"comdlg:no-current-directory");
        g_extended_error = CDERR_DIALOGFAILURE;
        releaseStorage();
        return FALSE;
    }
    debugDialog(L"comdlg:current-directory");
    if (ofn->lpstrInitialDir && *ofn->lpstrInitialDir)
        (void)setDirectory(state, ofn->lpstrInitialDir);

    // OPENFILENAME filters are alternating display/pattern strings. Keep the
    // selected filter visible and allow the user to cycle through its entries.
    if (ofn->lpstrFilter) {
        const wchar_t* item = ofn->lpstrFilter;
        while (*item && state.filterCount < kFilterSlots) {
            const size_t nameLength = boundedLength(item, 4096);
            if (nameLength >= 4096) break;
            const wchar_t* name = item;
            item += nameLength + 1;
            if (!*item) break;
            const wchar_t* pattern = item;
            const size_t patternLength = boundedLength(item, 4096);
            if (patternLength >= 4096) break;
            const size_t index = state.filterCount++;
            (void)copyText(state.filterNames[index], std::size(state.filterNames[index]), name);
            (void)copyText(state.filterPatterns[index],
                           std::size(state.filterPatterns[index]), pattern);
            item += patternLength + 1;
        }
    }
    if (state.filterCount) {
        state.filterIndex = ofn->nFilterIndex > 0 &&
            ofn->nFilterIndex <= state.filterCount ? ofn->nFilterIndex - 1 : 0;
        (void)copyText(state.pattern, std::size(state.pattern),
                       state.filterPatterns[state.filterIndex]);
    } else {
        state.filterCount = 1;
        (void)copyText(state.filterNames[0], std::size(state.filterNames[0]), L"All files");
        (void)copyText(state.filterPatterns[0], std::size(state.filterPatterns[0]), L"*.*");
    }

    if (!copyText(initial.get(), 32768, ofn->lpstrFile)) {
        g_extended_error = FNERR_INVALIDFILENAME;
        releaseStorage();
        return FALSE;
    }
    if (initial.get()[0]) {
        size_t length = boundedLength(initial.get(), 32768);
        size_t leaf = length;
        while (leaf && !isSeparator(initial.get()[leaf - 1])) --leaf;
        if (leaf && leaf < length) {
            initial.get()[length] = 0;
            initial.get()[leaf - 1] = 0;
            (void)setDirectory(state, initial.get());
            copyText(initial.get(), 32768, ofn->lpstrFile + leaf);
        } else if (initial.get()[0] == L'*' || initial.get()[0] == L'?') {
            copyText(state.pattern, std::size(state.pattern), initial.get());
            initial.get()[0] = 0;
        }
    }

    if (!g_dialogClassAtom) {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = fileDialogWndProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = kDialogClass;
        g_dialogClassAtom = RegisterClassExW(&cls);
        if (!g_dialogClassAtom) {
            g_extended_error = CDERR_REGISTERMSGFAIL;
            releaseStorage();
            return FALSE;
        }
    }
    if (!g_fileViewClassAtom) {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.style = 0x0008u; // CS_DBLCLKS
        cls.lpfnWndProc = fileViewWndProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.lpszClassName = kFileViewClass;
        g_fileViewClassAtom = RegisterClassExW(&cls);
        if (!g_fileViewClassAtom) {
            g_extended_error = CDERR_REGISTERMSGFAIL;
            releaseStorage();
            return FALSE;
        }
    }
    debugDialog(L"comdlg:class-registered");

    state.done = false;
    g_activeDialog = &state;
    if (state.owner) EnableWindow(state.owner, FALSE);
    const wchar_t* title = ofn->lpstrTitle && *ofn->lpstrTitle ? ofn->lpstrTitle :
        (save ? L"Save As" : L"Open");
    int x = 48, y = 48;
    if (state.owner) {
        RECT ownerRect{};
        if (GetWindowRect(state.owner, &ownerRect)) {
            x = ownerRect.left + ((ownerRect.right - ownerRect.left) - 760) / 2;
            y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - 490) / 2;
        }
    }
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    state.window = CreateWindowExW(WS_EX_DLGMODALFRAME, kDialogClass, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, 760, 490, state.owner, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    debugDialog(state.window ? L"comdlg:window-created" : L"comdlg:create-failed");
    if (!state.window) {
        if (state.owner) EnableWindow(state.owner, TRUE);
        g_activeDialog = nullptr;
        g_extended_error = CDERR_DIALOGFAILURE;
        releaseStorage();
        return FALSE;
    }
    if (initial.get()[0]) SetWindowTextW(state.nameEdit, initial.get());
    refreshRows(state);
    updateFilterLabel(state);
    debugDialog(L"comdlg:rows-refreshed");
    if (ofn->Flags & OFN_ENABLEHOOK) notifyHook(state, WM_INITDIALOG, 0, 0);
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);
    // Paint the file pane synchronously so it is not hidden behind the
    // bootstrap desktop's accumulated background WM_PAINT queue.
    if (state.fileView) UpdateWindow(state.fileView);
    repaintNow(state.nameHeader);
    repaintNow(state.fileView);
    repaintNow(state.pathLabel);
    repaintNow(state.pathEdit);
    repaintNow(state.upButton);
    for (HWND place : state.placeButtons) repaintNow(place);
    repaintNow(state.nameLabel);
    repaintNow(state.nameEdit);
    repaintNow(state.openButton);
    repaintNow(state.cancelButton);
    repaintNow(state.fileTypeLabel);
    repaintNow(state.filterControl);
    (void)SetForegroundWindow(state.window);
    SetFocus(state.nameEdit);
    debugDialog(L"comdlg:shown");
    while (!state.done) {
        MSG message{};
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) { state.done = true; break; }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            closeDialog(state, false);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            acceptSelection(state);
            continue;
        }
        debugDialog(L"comdlg:message");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(state.window)) DestroyWindow(state.window);
    if (state.owner) { EnableWindow(state.owner, TRUE); SetForegroundWindow(state.owner); }
    g_activeDialog = nullptr;
    const BOOL accepted = state.accepted ? TRUE : FALSE;
    releaseStorage();
    return accepted;
}

int getFileTitle(const wchar_t* path, wchar_t* title, WORD capacity) {
    if (!path || !title || !capacity) return 1;
    const wchar_t* leaf = path;
    for (const wchar_t* p = path; *p; ++p) if (isSeparator(*p)) leaf = p + 1;
    const size_t length = boundedLength(leaf, capacity);
    if (length >= capacity) return static_cast<int>(length + 1);
    for (size_t i = 0; i <= length; ++i) title[i] = leaf[i];
    return 0;
}

} // namespace

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA value) {
    return runAnsiFileDialog(value, false);
}

__declspec(dllexport) BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW value) {
    return runFileDialog(value, false);
}

__declspec(dllexport) BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA value) {
    return runAnsiFileDialog(value, true);
}

__declspec(dllexport) BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW value) {
    return runFileDialog(value, true);
}

__declspec(dllexport) short WINAPI GetFileTitleW(LPCWSTR path, LPWSTR title, WORD capacity) {
    return static_cast<short>(getFileTitle(path, title, capacity));
}

__declspec(dllexport) short WINAPI GetFileTitleA(LPCSTR path, LPSTR title, WORD capacity) {
    if (!path || !title || !capacity) return 1;
    wchar_t widePath[32768]{};
    wchar_t wideTitle[32768]{};
    if (!convertAnsi(path, widePath, static_cast<int>(std::size(widePath)))) return 1;
    const int result = getFileTitle(widePath, wideTitle,
        static_cast<WORD>(std::size(wideTitle)));
    if (result) return static_cast<short>(result);
    if (!convertWide(wideTitle, title, capacity)) return 1;
    return 0;
}

__declspec(dllexport) BOOL WINAPI ChooseColorA(LPCHOOSECOLORA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseColorW(LPCHOOSECOLORW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseFontA(LPCHOOSEFONTA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseFontW(LPCHOOSEFONTW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI PrintDlgA(LPPRINTDLGA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI PrintDlgW(LPPRINTDLGW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI PageSetupDlgA(LPPAGESETUPDLGA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW) {
    return fail_dialog();
}

__declspec(dllexport) HRESULT WINAPI PrintDlgExA(LPPRINTDLGEXA) {
    g_extended_error = CDERR_DIALOGFAILURE;
    return E_NOTIMPL;
}

__declspec(dllexport) HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW) {
    g_extended_error = CDERR_DIALOGFAILURE;
    return E_NOTIMPL;
}

__declspec(dllexport) HWND WINAPI FindTextA(LPFINDREPLACEA) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) HWND WINAPI FindTextW(LPFINDREPLACEW) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) HWND WINAPI ReplaceTextA(LPFINDREPLACEA) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) HWND WINAPI ReplaceTextW(LPFINDREPLACEW) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) DWORD WINAPI CommDlgExtendedError() {
    return g_extended_error;
}

} // extern "C"
