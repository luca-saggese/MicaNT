#pragma once
/**
 * @file winnt_user32_gdi32_imm32_system32_abi.hpp
 * @brief Horizon 111 (Kernel Unit #83): Sovereign WinNT Core User-Mode System32 DLLs
 *        (user32.dll, gdi32.dll & imm32.dll) Official PE32+ Exports, Win32 HWND/HDC/HMONITOR
 *        Creation, Non-Blocking PeekMessageW Game Loop, 8000Hz WM_INPUT Raw Input,
 *        GDI PixelFormat / SwapBuffers Interop, and IMM32 Full-Screen Game IME Control.
 */

#include <cstdint>
#include <cstring>

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/um/subsystems/winbase/winnt_kernel32_advapi32_system32_abi.hpp"

namespace micant::um::winnt_user32_gdi32_imm32_abi {

inline constexpr uint32_t WS_OVERLAPPEDWINDOW_OFFICIAL = 0x00CF0000U;
inline constexpr uint32_t WS_POPUP_OFFICIAL            = 0x80000000U;
inline constexpr uint32_t WS_VISIBLE_OFFICIAL          = 0x10000000U;
inline constexpr uint32_t PM_NOREMOVE_OFFICIAL         = 0x0000U;
inline constexpr uint32_t PM_REMOVE_OFFICIAL           = 0x0001U;

inline constexpr uint32_t WM_NULL_OFFICIAL             = 0x0000U;
inline constexpr uint32_t WM_CREATE_OFFICIAL           = 0x0001U;
inline constexpr uint32_t WM_DESTROY_OFFICIAL          = 0x0002U;
inline constexpr uint32_t WM_SIZE_OFFICIAL             = 0x0005U;
inline constexpr uint32_t WM_PAINT_OFFICIAL            = 0x000FU;
inline constexpr uint32_t WM_QUIT_OFFICIAL             = 0x0012U;
inline constexpr uint32_t WM_INPUT_OFFICIAL            = 0x00FFU;
inline constexpr uint32_t WM_KEYDOWN_OFFICIAL          = 0x0100U;
inline constexpr uint32_t WM_GETICON_OFFICIAL          = 0x007FU;
inline constexpr uint32_t WM_SETICON_OFFICIAL          = 0x0080U;
inline constexpr uint32_t LR_LOADFROMFILE_OFFICIAL     = 0x0010U;
inline constexpr uint32_t LR_SHARED_OFFICIAL           = 0x8000U;
inline constexpr uint32_t IMAGE_ICON_OFFICIAL          = 1U;

inline constexpr uint32_t RID_INPUT_OFFICIAL           = 0x10000003U;
inline constexpr uint32_t RIM_TYPEMOUSE_OFFICIAL       = 0U;
inline constexpr uint32_t MONITOR_DEFAULTTONEAREST_OFF = 2U;

struct WinNtRect {
    int32_t left{0};
    int32_t top{0};
    int32_t right{3840};
    int32_t bottom{2160};
};

struct WinNtPoint {
    int32_t x{0};
    int32_t y{0};
};

struct WinNtMsg {
    void*      hwnd{nullptr};
    uint32_t   message{0};
    uintptr_t  wParam{0};
    intptr_t   lParam{0};
    uint32_t   time{0};
    WinNtPoint pt{};
};

struct WinNtPaintStruct {
    void* hdc{nullptr};
    int32_t fErase{0};
    WinNtRect rcPaint{};
    int32_t fRestore{0};
    int32_t fIncUpdate{0};
    uint8_t rgbReserved[32]{};
};

struct WinNtRawInputDevice {
    uint16_t usUsagePage{0x01}; // HID_USAGE_PAGE_GENERIC
    uint16_t usUsage{0x02};     // HID_USAGE_GENERIC_MOUSE
    uint32_t dwFlags{0};
    void*    hwndTarget{nullptr};
};

struct WinNtRawInputHeader {
    uint32_t dwType{RIM_TYPEMOUSE_OFFICIAL};
    uint32_t dwSize{48};
    void*    hDevice{nullptr};
    uintptr_t wParam{0};
};

struct WinNtRawMouse {
    uint16_t usFlags{0};
    uint16_t usButtonFlags{0};
    uint16_t usButtonData{0};
    uint32_t ulRawButtons{0};
    int32_t  lLastX{12};
    int32_t  lLastY{-8};
    uint32_t ulExtraInformation{0};
};

struct WinNtRawInputPacket {
    WinNtRawInputHeader header{};
    WinNtRawMouse       mouse{};
};

struct Horizon111User32Gdi32Imm32AuditReport {
    uint32_t user32ExportCount{0};
    uint32_t gdi32ExportCount{0};
    uint32_t imm32ExportCount{0};
    void*    createdHwnd{nullptr};
    void*    createdHdc{nullptr};
    void*    associatedHMonitor{nullptr};
    int32_t  clientWidth{0};
    int32_t  clientHeight{0};
    bool     peekMessagePumpVerified{false};
    bool     rawInput8000HzMouseVerified{false};
    bool     gdiPixelFormatSwapBuffersVerified{false};
    bool     imm32GameImeDisableVerified{false};
};

class WinNtUser32Gdi32Imm32Coordinator {
public:
    static WinNtUser32Gdi32Imm32Coordinator& instance() noexcept {
        static WinNtUser32Gdi32Imm32Coordinator s_inst;
        return s_inst;
    }

    void initialize() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    [[nodiscard]] Horizon111User32Gdi32Imm32AuditReport runUser32Gdi32Imm32Audit() noexcept;

private:
    WinNtUser32Gdi32Imm32Coordinator() = default;
    bool m_initialized{false};
    Horizon111User32Gdi32Imm32AuditReport m_cachedReport{};
};

void InitializeWinNtUser32Gdi32Imm32Subsystem() noexcept;

} // namespace micant::um::winnt_user32_gdi32_imm32_abi
