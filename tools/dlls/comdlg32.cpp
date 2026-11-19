#include <windows.h>
#include <commdlg.h>
#include <cderr.h>
#include <cwchar>
#include <cstdint>
#include <iterator>
#include <memory>

namespace {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE)
extern "C" __declspec(dllimport) void WINAPI MicaWin32kDebugInputTrace(
    uint32_t, uint32_t, uint32_t);
uint32_t g_dialogTraceStage = 1;
#endif

// CommDlgExtendedError reports the most recent failure in this DLL. The
// current single-process loader has no thread-local runtime contract, so this
// state is process-wide until the native thread-local storage path is wired.
DWORD g_extended_error = 0;

BOOL fail_dialog() {
    g_extended_error = CDERR_DIALOGFAILURE;
    return FALSE;
}

constexpr wchar_t kDialogClass[] = L"LuceOS.CommonFileDialog";
constexpr int kPathEdit = 100;
constexpr int kNameEdit = 101;
constexpr int kFirstFile = 1000;
constexpr int kFileSlots = 18;
constexpr DWORD kDirectoryAttribute = 0x10;
constexpr DWORD kInvalidAttributes = 0xFFFFFFFFu;

struct FileRow {
    wchar_t name[MAX_PATH]{};
    bool directory{};
};

struct FileDialogState {
    HWND window{};
    HWND owner{};
    HWND pathEdit{};
    HWND nameEdit{};
    OPENFILENAMEW* ofn{};
    wchar_t directory[32768]{};
    wchar_t pattern[260]{L"*.*"};
    FileRow rows[kFileSlots]{};
    size_t rowCount{};
    bool save{};
    bool done{};
    bool accepted{};
};

FileDialogState* g_activeDialog{};
ATOM g_dialogClassAtom{};
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
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE)
    uint32_t hash = 2166136261u, length{};
    while (stage && stage[length] && length < 256) {
        hash = (hash ^ static_cast<uint16_t>(stage[length++])) * 16777619u;
    }
    MicaWin32kDebugInputTrace(g_dialogTraceStage++, hash, length);
#endif
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
    for (int i = 0; i < kFileSlots; ++i) {
        HWND old = GetDlgItem(state.window, kFirstFile + i);
        if (old) DestroyWindow(old);
        state.rows[i] = {};
    }
    state.rowCount = 0;
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
        row.directory = directory;
        wchar_t label[MAX_PATH + 4]{};
        copyText(label, std::size(label), row.name);
        if (directory) {
            const size_t length = boundedLength(label, std::size(label));
            if (length + 2 < std::size(label)) { label[length] = L'\\'; label[length + 1] = 0; }
        }
        const int y = 112 + static_cast<int>(state.rowCount) * 22;
        HWND item = CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_LEFT,
            24, y, 470, 21, state.window,
            reinterpret_cast<HMENU>(static_cast<uintptr_t>(kFirstFile + state.rowCount)),
            GetModuleHandleW(nullptr), nullptr);
        if (item) ++state.rowCount;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

void closeDialog(FileDialogState& state, bool accept) {
    state.accepted = accept;
    state.done = true;
    if (state.window) DestroyWindow(state.window);
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
        #if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE)
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
    if (state.ofn->lpstrCustomFilter && state.ofn->nMaxCustFilter)
        state.ofn->nFilterIndex = state.ofn->nFilterIndex ? state.ofn->nFilterIndex : 1;
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
        if (id == kPathEdit && HIWORD(wParam) == EN_CHANGE) return 0;
        if (id >= kFirstFile && id < kFirstFile + kFileSlots &&
            HIWORD(wParam) == BN_CLICKED) {
            const size_t index = static_cast<size_t>(id - kFirstFile);
            if (index >= state->rowCount) return 0;
            const FileRow& row = state->rows[index];
            if (row.directory) {
                auto next = allocateWideBuffer(32768);
                if (next && joinPath(state->directory, row.name, next.get(), 32768) &&
                    setDirectory(*state, next.get())) refreshRows(*state);
            } else SetWindowTextW(state->nameEdit, row.name);
            return 0;
        }
    }
    if (message == WM_CREATE) {
        state->pathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->directory,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            20, 30, 470, 24, window, reinterpret_cast<HMENU>(kPathEdit),
            GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"STATIC", L"Folder:", WS_CHILD | WS_VISIBLE,
            20, 10, 150, 18, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"STATIC", L"File name:", WS_CHILD | WS_VISIBLE,
            20, 518, 150, 18, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->nameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            20, 538, 470, 24, window, reinterpret_cast<HMENU>(kNameEdit),
            GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", state->save ? L"Save" : L"Open",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            336, 574, 74, 26, window, reinterpret_cast<HMENU>(IDOK),
            GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            416, 574, 74, 26, window, reinterpret_cast<HMENU>(IDCANCEL),
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
    #if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE)
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
        #if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD_DIALOG_TRACE)
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

    // OPENFILENAME filters are alternating display/pattern strings. Respect
    // the selected one-based index, with the first pattern as the fallback.
    if (ofn->lpstrFilter) {
        const wchar_t* item = ofn->lpstrFilter;
        DWORD index = ofn->nFilterIndex ? ofn->nFilterIndex : 1;
        for (DWORD n = 1; n <= index && *item; ++n) {
            item += boundedLength(item, 4096) + 1;
            if (!*item) break;
            if (n == index) { (void)copyText(state.pattern, std::size(state.pattern), item); break; }
            item += boundedLength(item, 4096) + 1;
        }
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
            x = ownerRect.left + ((ownerRect.right - ownerRect.left) - 540) / 2;
            y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - 650) / 2;
        }
    }
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    state.window = CreateWindowExW(WS_EX_DLGMODALFRAME, kDialogClass, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, 540, 650, state.owner, nullptr,
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
    debugDialog(L"comdlg:rows-refreshed");
    if (ofn->Flags & OFN_ENABLEHOOK) notifyHook(state, WM_INITDIALOG, 0, 0);
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);
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

__declspec(dllexport) BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW value) {
    return runFileDialog(value, false);
}

__declspec(dllexport) BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW value) {
    return runFileDialog(value, true);
}

__declspec(dllexport) short WINAPI GetFileTitleW(LPCWSTR path, LPWSTR title, WORD capacity) {
    return static_cast<short>(getFileTitle(path, title, capacity));
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
