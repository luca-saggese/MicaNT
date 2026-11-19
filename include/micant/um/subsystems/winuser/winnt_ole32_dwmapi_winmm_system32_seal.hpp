#pragma once
/**
 * @file winnt_ole32_dwmapi_winmm_system32_seal.hpp
 * @brief Horizon 112 (Kernel Unit #84): Sovereign WinNT Core User-Mode System32 DLLs
 *        (ole32.dll, dwmapi.dll & winmm.dll) Official PE32+ Exports, COM Apartment &
 *        CoCreateInstance Activation, DWM DirectFlip / VSync Composition (DwmFlush /
 *        DwmExtendFrameIntoClientArea), WinMM 1ms Scheduler Timer (timeBeginPeriod),
 *        and Complete 24-DLL System32 Ecosystem Seal (8 WinNT Core + 16 DirectX/Media).
 */

#include <cstdint>
#include <cstring>

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"

namespace micant::um::winnt_ole32_dwmapi_winmm_seal {

inline constexpr uint32_t COINIT_MULTITHREADED_OFFICIAL     = 0x0U;
inline constexpr uint32_t COINIT_APARTMENTTHREADED_OFFICIAL = 0x2U;
inline constexpr uint32_t CLSCTX_INPROC_SERVER_OFFICIAL     = 0x1U;
inline constexpr uint32_t CLSCTX_ALL_OFFICIAL               = 0x17U;

inline constexpr uint32_t TIMERR_NOERROR_OFFICIAL           = 0U;
inline constexpr uint32_t DWMWA_USE_IMMERSIVE_DARK_MODE_OFF = 20U;
inline constexpr uint32_t DWMWA_WINDOW_CORNER_PREF_OFF      = 33U;
inline constexpr uint32_t DWMWA_SYSTEMBACKDROP_TYPE_OFF     = 38U;

struct WinNtMargins {
    int32_t cxLeftWidth{-1};
    int32_t cxRightWidth{-1};
    int32_t cyTopHeight{-1};
    int32_t cyBottomHeight{-1};
};

struct WinNtTimeCaps {
    uint32_t wPeriodMin{1};
    uint32_t wPeriodMax{1000000};
};

struct Horizon112System32Complete24DllSealReport {
    uint32_t ole32ExportCount{0};
    uint32_t dwmapiExportCount{0};
    uint32_t winmmExportCount{0};
    uint32_t winntCoreSystem32DllCount{0};     // 8: kernel32, advapi32, user32, gdi32, imm32, ole32, dwmapi, winmm
    uint32_t prismxDirectXSystem32DllCount{0}; // 16: dxgi, d3d12, d3d11, d3d9, fxc47, xinput1_4, xaudio2_9, dinput8, d2d1, dwrite, dcomp, mfplat, mfreadwrite, dxcore, vulkan-1, opengl32
    uint32_t totalSystem32PeDllCount{0};       // 24 total PE32+ .dll binaries
    uint32_t kernelOwnedWinNtExports{0};
    uint32_t totalHorizons{0};                 // 112
    uint32_t totalKernelUnits{0};              // 84
    uint32_t totalTestSuites{0};               // 328
    uint32_t prismxStandaloneSuites{0};        // 40
    bool     comApartmentAndInstanceVerified{false};
    bool     dwmCompositionAndFlushVerified{false};
    bool     winmm1msTimerResolutionVerified{false};
    bool     all24System32DllsSealed{false};
};

class WinNtOle32DwmapiWinmmSealCoordinator {
public:
    static WinNtOle32DwmapiWinmmSealCoordinator& instance() noexcept {
        static WinNtOle32DwmapiWinmmSealCoordinator s_inst;
        return s_inst;
    }

    void initialize() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    [[nodiscard]] Horizon112System32Complete24DllSealReport runComplete24DllSealAudit() noexcept;

private:
    WinNtOle32DwmapiWinmmSealCoordinator() = default;
    bool m_initialized{false};
    Horizon112System32Complete24DllSealReport m_cachedReport{};
};

void InitializeWinNtOle32DwmapiWinmmSealSubsystem() noexcept;

} // namespace micant::um::winnt_ole32_dwmapi_winmm_seal
