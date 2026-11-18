/**
 * @file cmd_terminal_app.cpp
 * @brief Small Ring 3 graphical terminal frontend for LuceOS CMD.
 *
 * This file owns the terminal window, screen buffer, keyboard editing and the
 * Ring 3 frontend connection to the native command engine.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>
#define MICANT_WIN32K_IMPORTS
#include "micant/shared/win32k_client.h"
#include "micant/shared/theme_snapshot.h"
#include "cmd/luce_cmd_bridge.h"

extern "C" {
__declspec(dllimport) uint16_t RegisterClassExW(const MicaWin32kWindowClassEx*);
__declspec(dllimport) void* CreateWindowExW(uint32_t, const wchar_t*, const wchar_t*,
    uint32_t, int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*);
__declspec(dllimport) int32_t ShowWindow(void*, int32_t);
__declspec(dllimport) int32_t UpdateWindow(void*);
__declspec(dllimport) int32_t DestroyWindow(void*);
__declspec(dllimport) int32_t InvalidateRect(void*, const MicaWin32kRect*, int32_t);
__declspec(dllimport) void* BeginPaint(void*, void*);
__declspec(dllimport) int32_t EndPaint(void*, const void*);
__declspec(dllimport) intptr_t DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t);
__declspec(dllimport) int32_t GetMessageW(MicaWin32kMessage*, void*, uint32_t, uint32_t);
__declspec(dllimport) int32_t TranslateMessage(const MicaWin32kMessage*);
__declspec(dllimport) intptr_t DispatchMessageW(const MicaWin32kMessage*);
__declspec(dllimport) void PostQuitMessage(int32_t);
__declspec(dllimport) int32_t GetClientRect(void*, MicaWin32kRect*);
__declspec(dllimport) int32_t SetWindowTextW(void*, const wchar_t*);

__declspec(dllimport) void* CreateSolidBrush(uint32_t);
__declspec(dllimport) int32_t FillRect(void*, const MicaWin32kRect*, void*);
__declspec(dllimport) int32_t DeleteObject(void*);
__declspec(dllimport) void* CreateFontW(int32_t, int32_t, int32_t, int32_t,
    int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, const wchar_t*);
__declspec(dllimport) void* SelectObject(void*, void*);
__declspec(dllimport) uint32_t SetTextColor(void*, uint32_t);
__declspec(dllimport) int32_t SetBkMode(void*, int32_t);
__declspec(dllimport) int32_t TextOutW(void*, int32_t, int32_t, const wchar_t*, int32_t);
}

namespace {
constexpr uint32_t WM_CREATE = 0x0001;
constexpr uint32_t WM_DESTROY = 0x0002;
constexpr uint32_t WM_SIZE = 0x0005;
constexpr uint32_t WM_PAINT = 0x000F;
constexpr uint32_t WM_CLOSE = 0x0010;
constexpr uint32_t WM_KEYDOWN = 0x0100;
constexpr uint32_t WM_CHAR = 0x0102;
constexpr uint32_t WM_MOUSEWHEEL = 0x020A;
constexpr uint32_t WM_THEMECHANGED = 0x031A;
constexpr uint32_t VK_UP = 0x26;
constexpr uint32_t VK_DOWN = 0x28;
constexpr uint32_t VK_LEFT = 0x25;
constexpr uint32_t VK_RIGHT = 0x27;
constexpr uint32_t VK_PRIOR = 0x21;
constexpr uint32_t VK_NEXT = 0x22;
constexpr uint32_t WsOverlappedWindow = 0x00CF0000U;
constexpr uint32_t WsVisible = 0x10000000U;
constexpr int32_t FontFixedPitch = 0x01;
constexpr int32_t Transparent = 1;
constexpr uint32_t MaxRows = 200;
constexpr uint32_t MaxColumns = 256;
constexpr uint32_t MaxInput = 240;
constexpr uint32_t HistoryCount = 32;
constexpr uint8_t DefaultTextAttribute = 0x07;
constexpr uint8_t ErrorTextAttribute = 0x0C;
constexpr int32_t CellWidth = 9;
constexpr int32_t CellHeight = 18;
constexpr int32_t LeftMargin = 10;
constexpr int32_t TopMargin = 8;

struct PaintStruct {
    void* hdc{};
    int32_t erase{};
    MicaWin32kRect paintRect{};
    int32_t restore{};
    int32_t incremental{};
    uint8_t reserved[32]{};
};

wchar_t s_rows[MaxRows][MaxColumns]{};
uint8_t s_cellAttributes[MaxRows][MaxColumns]{};
uint32_t s_rowCount{};
uint32_t s_firstRow{};
wchar_t s_input[MaxInput]{};
uint32_t s_inputLength{};
uint32_t s_caret{};
wchar_t s_history[HistoryCount][MaxInput]{};
wchar_t s_historyDraft[MaxInput]{};
uint32_t s_historyDraftLength{};
uint32_t s_historyCount{};
uint32_t s_historyNext{};
int32_t s_historyCursor{-1};
uint32_t s_visibleRows{24};
uint32_t s_lineLength{};
uint32_t s_promptLength{};
void* s_window{};
void* s_font{};
bool s_cmdInitialized{};
bool s_modalInput{};
bool s_modalInputReady{};
uint32_t s_modalInputCapacity{};
uint32_t s_modalInputLength{};
bool s_modalKeyOnly{};
wchar_t s_modalBuffer[MaxInput]{};
uint32_t s_backgroundColorRef{0x00F3F3F3u};
uint32_t s_foregroundColorRef{0x001E1E1Eu};

uint32_t CompositeColor(uint32_t color, uint32_t under) noexcept {
    const uint32_t alpha = color >> 24;
    const uint32_t inverse = 255u - alpha;
    const auto channel = [&](uint32_t shift) noexcept {
        const uint32_t foreground = (color >> shift) & 0xFFu;
        const uint32_t background = (under >> shift) & 0xFFu;
        return (foreground * alpha + background * inverse + 127u) / 255u;
    };
    return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

uint32_t ToColorRef(uint32_t argb, uint32_t fallbackColorRef) noexcept {
    const uint32_t fallbackRgb = ((fallbackColorRef & 0xFFu) << 16) |
        (fallbackColorRef & 0x00FF00u) | ((fallbackColorRef >> 16) & 0xFFu);
    const uint32_t rgb = CompositeColor(argb, fallbackRgb);
    return ((rgb & 0xFFu) << 16) | (rgb & 0x00FF00u) | ((rgb >> 16) & 0xFFu);
}

void RefreshThemeColors() noexcept {
    constexpr uint32_t BaseBackgroundColorRef = 0x00F3F3F3u;
    constexpr uint32_t BaseForegroundColorRef = 0x001E1E1Eu;
    uint32_t background = BaseBackgroundColorRef;
    uint32_t foreground = BaseForegroundColorRef;
    MicaThemeSnapshot snapshot{};
    if (MicaWin32kGetThemeSnapshot(&snapshot) &&
        MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot))) {
        constexpr uint32_t WindowNormalStyleIndex = 14u * MICANT_THEME_STATE_COUNT;
        const auto& style = snapshot.styles[WindowNormalStyleIndex];
        if (style.flags & MICANT_THEME_HAS_BACKGROUND)
            background = ToColorRef(style.background, BaseBackgroundColorRef);
        if (style.flags & MICANT_THEME_HAS_FOREGROUND)
            foreground = ToColorRef(style.foreground, BaseForegroundColorRef);
    }
    s_backgroundColorRef = background;
    s_foregroundColorRef = foreground;
}

void Invalidate() noexcept {
    if (s_window) (void)InvalidateRect(s_window, nullptr, 1);
}

void ScrollToBottom() noexcept {
    s_firstRow = s_rowCount > s_visibleRows ? s_rowCount - s_visibleRows : 0;
}

void AppendLine(const wchar_t* text, uint32_t length) noexcept {
    if (s_rowCount == MaxRows) {
        for (uint32_t row = 1; row < MaxRows; ++row) {
            for (uint32_t col = 0; col < MaxColumns; ++col) {
                s_rows[row - 1][col] = s_rows[row][col];
                s_cellAttributes[row - 1][col] = s_cellAttributes[row][col];
            }
        }
        --s_rowCount;
        if (s_firstRow) --s_firstRow;
    }
    wchar_t* target = s_rows[s_rowCount++];
    for (uint32_t col = 0; col < MaxColumns; ++col) {
        target[col] = L' ';
        s_cellAttributes[s_rowCount - 1][col] = DefaultTextAttribute;
    }
    if (length > MaxColumns) length = MaxColumns;
    for (uint32_t col = 0; col < length; ++col) target[col] = text[col];
    s_lineLength = length;
    s_firstRow = s_rowCount > s_visibleRows ? s_rowCount - s_visibleRows : 0;
}

void StartLine() noexcept {
    AppendLine(nullptr, 0);
}

void ClearScreen() noexcept {
    for (uint32_t row = 0; row < s_rowCount; ++row)
        for (uint32_t col = 0; col < MaxColumns; ++col) {
            s_rows[row][col] = L' ';
            s_cellAttributes[row][col] = DefaultTextAttribute;
        }
    s_rowCount = 0;
    s_firstRow = 0;
    s_lineLength = 0;
    s_promptLength = 0;
}

uint32_t InputCapacity() noexcept {
    const uint32_t rowCapacity = s_promptLength < MaxColumns
        ? MaxColumns - s_promptLength : 0;
    const uint32_t configuredCapacity = MaxInput - 1;
    return rowCapacity < configuredCapacity ? rowCapacity : configuredCapacity;
}

void AppendOutput(std::wstring_view text, uint8_t attribute = DefaultTextAttribute) noexcept {
    if (!s_rowCount) StartLine();
    for (wchar_t character : text) {
        // Native output uses CRLF. Keeping CR as a no-op preserves the line
        // just written; clearing the row here erased every completed line.
        if (character == L'\r') {
            continue;
        } else if (character == L'\n') {
            StartLine();
        } else if (character == L'\f') {
            ClearScreen();
            StartLine();
        } else if (character == L'\t') {
            const uint32_t spaces = 4u - (s_lineLength & 3u);
            for (uint32_t i = 0; i < spaces; ++i) {
                if (s_lineLength == MaxColumns) StartLine();
                s_rows[s_rowCount - 1][s_lineLength] = L' ';
                s_cellAttributes[s_rowCount - 1][s_lineLength++] = attribute;
            }
        } else {
            if (s_lineLength == MaxColumns) StartLine();
            s_rows[s_rowCount - 1][s_lineLength] = character;
            s_cellAttributes[s_rowCount - 1][s_lineLength++] = attribute;
        }
    }
    if (s_window) (void)InvalidateRect(s_window, nullptr, 1);
}

int32_t __stdcall TerminalWriteCallback(void*, const wchar_t* text,
                                        uint32_t length) noexcept;

int32_t __stdcall TerminalWriteCallback(void* stream, const wchar_t* text,
                                        uint32_t length) noexcept {
    if (!text && length) return 0;
    const uint8_t attribute = static_cast<uint8_t>(LuceCmdGetStreamAttribute(stream));
    AppendOutput(std::wstring_view(text ? text : L"", length), attribute);
    return static_cast<int32_t>(length);
}

void AppendPrompt() noexcept {
    LuceCmdPrintPrompt();
    s_promptLength = s_lineLength;
}

void ShowInitializationFailure() noexcept {
    AppendOutput(L"Luce CMD initialization failed.\r\n", ErrorTextAttribute);
}

void RememberCommand() noexcept {
    if (!s_inputLength) return;
    wchar_t* entry = s_history[s_historyNext];
    for (uint32_t i = 0; i <= s_inputLength; ++i) entry[i] = s_input[i];
    s_historyNext = (s_historyNext + 1) % HistoryCount;
    if (s_historyCount < HistoryCount) ++s_historyCount;
}

bool IsHighSurrogate(wchar_t value) noexcept;
bool IsLowSurrogate(wchar_t value) noexcept;

void LoadHistory(int32_t direction) noexcept {
    if (!s_historyCount) return;
    if (s_historyCursor < 0) {
        s_historyDraftLength = s_inputLength;
        for (uint32_t i = 0; i <= s_inputLength; ++i) s_historyDraft[i] = s_input[i];
        s_historyCursor = static_cast<int32_t>(s_historyCount);
    }
    const int32_t oldest = static_cast<int32_t>(s_historyCount);
    int32_t next = s_historyCursor + direction;
    if (next < 0) next = 0;
    if (next > oldest) next = oldest;
    s_historyCursor = next;
    if (next == oldest) {
        s_inputLength = s_historyDraftLength;
        for (uint32_t i = 0; i <= s_inputLength; ++i) s_input[i] = s_historyDraft[i];
    } else {
        const uint32_t index = (s_historyNext + HistoryCount - s_historyCount +
            static_cast<uint32_t>(next)) % HistoryCount;
        uint32_t i = 0;
        const uint32_t limit = InputCapacity();
        while (i < limit && i + 1 < MaxInput && s_history[index][i]) {
            s_input[i] = s_history[index][i];
            ++i;
        }
        if (i && IsHighSurrogate(s_input[i - 1])) --i;
        s_input[i] = 0;
        s_inputLength = i;
    }
    s_caret = s_inputLength;
}

bool SubmitInput() noexcept {
    RememberCommand();
    if (s_rowCount) {
        wchar_t* submitted = s_rows[s_rowCount - 1];
        uint32_t count = s_inputLength;
        if (count > MaxColumns - s_promptLength) count = MaxColumns - s_promptLength;
        for (uint32_t i = 0; i < count; ++i) {
            submitted[s_promptLength + i] = s_input[i];
            s_cellAttributes[s_rowCount - 1][s_promptLength + i] = DefaultTextAttribute;
        }
        s_lineLength = s_promptLength + count;
    }
    wchar_t commandBuffer[MaxInput]{};
    for (uint32_t i = 0; i < s_inputLength; ++i) commandBuffer[i] = s_input[i];
    s_inputLength = 0;
    s_caret = 0;
    s_input[0] = 0;
    s_historyCursor = -1;
    s_promptLength = 0;
    StartLine();
    if (s_cmdInitialized) {
        (void)LuceCmdExecuteLine(commandBuffer);
        size_t token{};
        while (commandBuffer[token] == L' ' || commandBuffer[token] == L'\t') ++token;
        constexpr wchar_t exitCommand[] = L"exit";
        bool explicitExit = true;
        for (size_t index = 0; index < 4; ++index) {
            wchar_t value = commandBuffer[token + index];
            if (value >= L'A' && value <= L'Z') value += L'a' - L'A';
            if (value != exitCommand[index]) { explicitExit = false; break; }
        }
        const wchar_t afterExit = commandBuffer[token + 4];
        explicitExit &= afterExit == 0 || afterExit == L' ' || afterExit == L'\t';
        if (explicitExit) {
            if (s_window) (void)DestroyWindow(s_window);
            return false;
        }
    } else {
        ShowInitializationFailure();
    }
    if (s_lineLength) StartLine();
    AppendPrompt();
    Invalidate();
    return true;
}

void DrawWide(void* dc, int32_t x, int32_t y, const wchar_t* text, uint32_t count) noexcept {
    if (count) (void)TextOutW(dc, x, y, text, static_cast<int32_t>(count));
}

uint32_t ConsoleColorRef(uint8_t index, bool foreground) noexcept {
    if (foreground && index == 7) return s_foregroundColorRef;
    if (!foreground && index == 0) return s_backgroundColorRef;
    constexpr uint32_t Palette[16]{
        0x00000000u, 0x00000080u, 0x00008000u, 0x00008080u,
        0x00800000u, 0x00800080u, 0x00808000u, 0x00C0C0C0u,
        0x00808080u, 0x000000FFu, 0x0000FF00u, 0x0000FFFFu,
        0x00FF0000u, 0x00FF00FFu, 0x00FFFF00u, 0x00FFFFFFu,
    };
    return Palette[index & 0x0Fu];
}

void DrawAttributedCells(void* dc, uint32_t row, uint32_t start, uint32_t count,
                         int32_t x, int32_t y) noexcept {
    if (row >= MaxRows || start >= MaxColumns) return;
    if (count > MaxColumns - start) count = MaxColumns - start;
    uint32_t offset = 0;
    while (offset < count) {
        const uint8_t attribute = s_cellAttributes[row][start + offset];
        uint32_t runLength = 1;
        while (offset + runLength < count &&
               s_cellAttributes[row][start + offset + runLength] == attribute)
            ++runLength;
        const int32_t runX = x + static_cast<int32_t>(offset) * CellWidth;
        const uint8_t backgroundIndex = static_cast<uint8_t>(attribute >> 4);
        if (backgroundIndex) {
            const MicaWin32kRect runBounds{runX, y,
                runX + static_cast<int32_t>(runLength) * CellWidth, y + CellHeight};
            void* brush = CreateSolidBrush(ConsoleColorRef(backgroundIndex, false));
            if (brush) {
                (void)FillRect(dc, &runBounds, brush);
                (void)DeleteObject(brush);
            }
        }
        (void)SetTextColor(dc, ConsoleColorRef(attribute & 0x0Fu, true));
        DrawWide(dc, runX, y, s_rows[row] + start + offset, runLength);
        offset += runLength;
    }
}

bool IsHighSurrogate(wchar_t value) noexcept {
    return value >= 0xD800 && value <= 0xDBFF;
}

bool IsLowSurrogate(wchar_t value) noexcept {
    return value >= 0xDC00 && value <= 0xDFFF;
}

void PaintTerminal(void* hwnd) noexcept {
    PaintStruct paint{};
    void* dc = BeginPaint(hwnd, &paint);
    if (!dc) return;
    MicaWin32kRect client{};
    (void)GetClientRect(hwnd, &client);
    const int32_t width = client.right - client.left;
    const int32_t height = client.bottom - client.top;
    const MicaWin32kRect bounds{0, 0, width, height};
    RefreshThemeColors();
    void* background = CreateSolidBrush(s_backgroundColorRef);
    if (background) {
        (void)FillRect(dc, &bounds, background);
        (void)DeleteObject(background);
    }
    if (s_font) (void)SelectObject(dc, s_font);
    (void)SetBkMode(dc, Transparent);
    (void)SetTextColor(dc, s_foregroundColorRef);
    s_visibleRows = height > TopMargin
        ? static_cast<uint32_t>((height - TopMargin) / CellHeight) : 1;
    if (!s_visibleRows) s_visibleRows = 1;
    if (s_rowCount <= s_visibleRows) s_firstRow = 0;
    else if (s_firstRow + s_visibleRows > s_rowCount)
        s_firstRow = s_rowCount - s_visibleRows;
    const uint32_t totalCells = width > LeftMargin
        ? static_cast<uint32_t>((width - LeftMargin) / CellWidth) : 0;
    const uint32_t promptBudget = totalCells > 20 ? totalCells - 20
        : (totalCells > 1 ? totalCells - 1 : 0);
    const uint32_t promptCells = s_promptLength < promptBudget
        ? s_promptLength : promptBudget;
    const uint32_t promptStart = s_promptLength - promptCells;
    for (uint32_t visible = 0; visible < s_visibleRows; ++visible) {
        const uint32_t row = s_firstRow + visible;
        if (row >= s_rowCount) break;
        const int32_t y = TopMargin + static_cast<int32_t>(visible) * CellHeight;
        if (row + 1 == s_rowCount && s_promptLength) {
            // Keep the tail of long current-directory prompts visible so the
            // edit area remains reachable in a narrow or resized window.
            DrawAttributedCells(dc, row, promptStart, promptCells, LeftMargin, y);
        } else {
            DrawAttributedCells(dc, row, 0, MaxColumns, LeftMargin, y);
        }
    }
    if (s_rowCount) {
        const uint32_t promptRow = s_rowCount - 1;
        if (promptRow >= s_firstRow && promptRow < s_firstRow + s_visibleRows) {
            const int32_t y = TopMargin + static_cast<int32_t>(promptRow - s_firstRow) * CellHeight;
            const uint32_t promptX = promptCells * CellWidth;
            const uint32_t inputCells = totalCells > promptCells ? totalCells - promptCells : 0;
            const uint32_t inputVisibleCells = inputCells ? inputCells - 1 : 0;
            uint32_t firstInputCell = s_caret > inputVisibleCells
                ? s_caret - inputVisibleCells : 0;
            if (firstInputCell && firstInputCell < s_inputLength &&
                IsLowSurrogate(s_input[firstInputCell]) &&
                IsHighSurrogate(s_input[firstInputCell - 1])) --firstInputCell;
            if (firstInputCell + inputVisibleCells > s_inputLength &&
                s_inputLength > inputVisibleCells)
                firstInputCell = s_inputLength - inputVisibleCells;
            uint32_t shown = inputVisibleCells && s_inputLength > firstInputCell
                ? (s_inputLength - firstInputCell < inputVisibleCells
                    ? s_inputLength - firstInputCell : inputVisibleCells) : 0;
            if (shown && firstInputCell + shown < s_inputLength &&
                IsHighSurrogate(s_input[firstInputCell + shown - 1])) --shown;
            (void)SetTextColor(dc, s_foregroundColorRef);
            DrawWide(dc, LeftMargin + static_cast<int32_t>(promptX), y,
                     s_input + firstInputCell, shown);
            const wchar_t caret = L'_';
            const uint32_t caretCell = s_caret >= firstInputCell
                ? s_caret - firstInputCell : 0;
            if (inputCells)
                DrawWide(dc, LeftMargin + static_cast<int32_t>(promptX + caretCell * CellWidth),
                         y, &caret, 1);
        }
    }
    (void)EndPaint(hwnd, &paint);
}

intptr_t TerminalWindowProc(void* hwnd, uint32_t message,
                            uintptr_t wParam, intptr_t lParam) noexcept {
    switch (message) {
    case WM_CREATE:
        Invalidate();
        return 0;
    case WM_SIZE: {
        const uint32_t height = static_cast<uint32_t>(static_cast<uint16_t>(
            static_cast<uintptr_t>(lParam) >> 16));
        s_visibleRows = height > TopMargin
            ? (height - TopMargin) / CellHeight : 1;
        Invalidate();
        return 0;
    }
    case WM_THEMECHANGED:
        RefreshThemeColors();
        Invalidate();
        return 0;
    case WM_CHAR: {
        const wchar_t character = static_cast<wchar_t>(wParam);
        if (s_modalInput) {
            if (character == L'\r' || character == L'\n') {
                if (!s_modalKeyOnly && s_rowCount) {
                    wchar_t* submitted = s_rows[s_rowCount - 1];
                    const uint32_t count = s_inputLength < MaxColumns - s_promptLength
                        ? s_inputLength : MaxColumns - s_promptLength;
                    for (uint32_t i = 0; i < count; ++i) {
                        submitted[s_promptLength + i] = s_input[i];
                        s_cellAttributes[s_rowCount - 1][s_promptLength + i] =
                            DefaultTextAttribute;
                    }
                    s_lineLength = s_promptLength + count;
                    StartLine();
                }
                if (s_modalKeyOnly && s_inputLength == 0) {
                    s_input[0] = character;
                    s_inputLength = 1;
                }
                s_modalInputLength = s_inputLength;
                for (uint32_t i = 0; i < s_modalInputLength && i < MaxInput; ++i)
                    s_modalBuffer[i] = s_input[i];
                s_modalInputReady = true;
                s_input[0] = 0;
                s_inputLength = s_caret = 0;
                s_promptLength = 0;
                Invalidate();
                return 0;
            }
            if (character == L'\b') {
                if (s_caret) {
                    for (uint32_t i = s_caret; i < s_inputLength; ++i)
                        s_input[i - 1] = s_input[i];
                    --s_caret;
                    --s_inputLength;
                    s_input[s_inputLength] = 0;
                }
            } else if (character >= L' ' && character != 0x7f &&
                       s_inputLength + 1 < s_modalInputCapacity &&
                       s_inputLength + 1 < MaxInput) {
                s_input[s_inputLength++] = character;
                s_input[s_inputLength] = 0;
                s_caret = s_inputLength;
                if (s_modalKeyOnly) {
                    s_modalInputLength = s_inputLength;
                    for (uint32_t i = 0; i < s_modalInputLength && i < MaxInput; ++i)
                        s_modalBuffer[i] = s_input[i];
                    s_modalInputReady = true;
                    s_input[0] = 0;
                    s_inputLength = s_caret = 0;
                }
            }
            Invalidate();
            return 0;
        }
        if (character == L'\r' || character == L'\n') (void)SubmitInput();
        else if (character == L'\b') {
            if (s_caret) {
                uint32_t remove = 1;
                if (s_caret >= 2 && IsLowSurrogate(s_input[s_caret - 1]) &&
                    IsHighSurrogate(s_input[s_caret - 2])) remove = 2;
                for (uint32_t i = s_caret; i < s_inputLength; ++i)
                    s_input[i - remove] = s_input[i];
                s_caret -= remove;
                s_inputLength -= remove;
                s_input[s_inputLength] = 0;
            }
        } else if (character >= L' ' && character != 0x7f &&
                   !(IsLowSurrogate(character) &&
                     (!s_caret || !IsHighSurrogate(s_input[s_caret - 1]))) &&
                   !(IsHighSurrogate(character) &&
                     InputCapacity() - s_inputLength < 2) &&
                   s_inputLength < InputCapacity()) {
            for (uint32_t i = s_inputLength; i > s_caret; --i) s_input[i] = s_input[i - 1];
            s_input[s_caret++] = character;
            ++s_inputLength;
            s_input[s_inputLength] = 0;
        }
        if (character != L'\r' && character != L'\n') s_historyCursor = -1;
        ScrollToBottom();
        Invalidate();
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_LEFT && s_caret) {
            if (s_caret >= 2 && IsLowSurrogate(s_input[s_caret - 1]) &&
                IsHighSurrogate(s_input[s_caret - 2])) s_caret -= 2;
            else --s_caret;
        } else if (wParam == VK_RIGHT && s_caret < s_inputLength) {
            if (IsHighSurrogate(s_input[s_caret]) && s_caret + 1 < s_inputLength &&
                IsLowSurrogate(s_input[s_caret + 1])) s_caret += 2;
            else ++s_caret;
        }
        else if (wParam == VK_UP) LoadHistory(-1);
        else if (wParam == VK_DOWN) LoadHistory(1);
        else if (wParam == VK_PRIOR) {
            s_firstRow = s_firstRow > s_visibleRows ? s_firstRow - s_visibleRows : 0;
        } else if (wParam == VK_NEXT) {
            const uint32_t last = s_rowCount > s_visibleRows ? s_rowCount - s_visibleRows : 0;
            s_firstRow = s_firstRow + s_visibleRows < last ? s_firstRow + s_visibleRows : last;
        } else return DefWindowProcW(hwnd, message, wParam, lParam);
        if (wParam != VK_PRIOR && wParam != VK_NEXT) ScrollToBottom();
        Invalidate();
        return 0;
    case WM_MOUSEWHEEL: {
        const int16_t delta = static_cast<int16_t>((static_cast<uintptr_t>(wParam) >> 16) & 0xffffu);
        if (!delta) return 0;
        const uint32_t step = 3u;
        if (delta > 0) s_firstRow = s_firstRow > step ? s_firstRow - step : 0;
        else {
            const uint32_t last = s_rowCount > s_visibleRows ? s_rowCount - s_visibleRows : 0;
            s_firstRow = s_firstRow + step < last ? s_firstRow + step : last;
        }
        Invalidate();
        return 0;
    }
    case WM_PAINT:
        PaintTerminal(hwnd);
        return 0;
    case WM_CLOSE:
        (void)DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
} // namespace

extern "C" int32_t __stdcall LuceCmdReadInput(wchar_t* buffer,
                                               uint32_t capacity,
                                               int32_t keyOnly) {
    if (!buffer || !capacity || !s_window || s_modalInput)
        return 0;

    s_modalInput = true;
    s_modalInputReady = false;
    s_modalInputLength = 0;
    s_modalInputCapacity = capacity;
    s_modalKeyOnly = keyOnly != 0;
    s_inputLength = 0;
    s_caret = 0;
    s_input[0] = 0;
    s_promptLength = s_lineLength;
    Invalidate();

    while (!s_modalInputReady) {
        MicaWin32kMessage message{};
        const int32_t result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            s_modalInput = false;
            s_promptLength = 0;
            buffer[0] = 0;
            return 0;
        }
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }

    uint32_t copied = s_modalInputLength;
    const uint32_t limit = keyOnly ? capacity : capacity - 1;
    if (copied > limit) copied = limit;
    for (uint32_t i = 0; i < copied; ++i) buffer[i] = s_modalBuffer[i];
    if (!keyOnly) buffer[copied] = 0;
    s_modalInput = false;
    s_modalInputReady = false;
    s_modalKeyOnly = false;
    s_modalInputLength = 0;
    s_promptLength = 0;
    Invalidate();
    return static_cast<int32_t>(copied);
}

extern "C" int32_t LuceCmdSetTerminalTitle(const wchar_t* title) {
    return s_window && title ? SetWindowTextW(s_window, title) : 0;
}

extern "C" void _start(const char* = nullptr) {
    constexpr wchar_t ClassName[] = L"LuceOSCmdTerminalWindow";
    MicaWin32kWindowClassEx windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.wndProc = reinterpret_cast<void*>(TerminalWindowProc);
    windowClass.className = ClassName;
    if (!RegisterClassExW(&windowClass)) return;
    s_font = CreateFontW(-16, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 4,
                         FontFixedPitch, L"Consolas");
    s_window = CreateWindowExW(0, ClassName, L"LuceOS Command Prompt",
        WsOverlappedWindow | WsVisible, 120, 90, 760, 500,
        nullptr, nullptr, nullptr, nullptr);
    if (!s_window) {
        if (s_font) {
            (void)DeleteObject(s_font);
            s_font = nullptr;
        }
        return;
    }
    (void)ShowWindow(s_window, 5);
    (void)UpdateWindow(s_window);
    s_cmdInitialized = LuceCmdInitialize(TerminalWriteCallback) != 0;
    if (!s_cmdInitialized) ShowInitializationFailure();
    AppendPrompt();
    Invalidate();
    for (;;) {
        MicaWin32kMessage message{};
        const int32_t result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) break;
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
    LuceCmdShutdown();
    s_cmdInitialized = false;
    if (s_font) {
        (void)DeleteObject(s_font);
        s_font = nullptr;
    }
}
