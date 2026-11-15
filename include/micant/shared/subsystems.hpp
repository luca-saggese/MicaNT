// ============================================================================
// MicaNT - Sovereign Windows NT Operating System Kernel & Subsystems
// Canonical Win32 & NT Subsystem Initialization Dispatcher
// (include/micant/subsystems.hpp)
// ============================================================================

#pragma once
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winuser/user32.hpp"
#include "micant/um/subsystems/winuser/gdi32.hpp"
#include "micant/um/subsystems/winuser/gdiplus.hpp"
#include "micant/um/subsystems/winbase/advapi32.hpp"
#include "micant/um/subsystems/winuser/shell32.hpp"
#include "micant/um/subsystems/winuser/comctl32.hpp"
#include "micant/um/subsystems/winuser/comdlg32.hpp"
#include "micant/um/subsystems/net/sensapi.hpp"
#include "micant/ucrt/msvcrt.hpp"
#include "micant/um/subsystems/mgmt/msi.hpp"
#include "micant/um/subsystems/security/sspi.hpp"
#include "micant/um/subsystems/security/crypt32.hpp"
#include "micant/um/subsystems/net/ws2_32.hpp"
#include "micant/um/subsystems/com/ole32.hpp"
#include "micant/um/subsystems/com/oleaut32.hpp"
#include "micant/um/subsystems/winuser/uxtheme_exports.hpp"
#include "micant/um/subsystems/com/propsys.hpp"
#include "micant/um/subsystems/net/urlmon.hpp"
#include "micant/um/subsystems/net/wininet.hpp"
#include "micant/um/subsystems/winuser/winspool.hpp"
#include "micant/um/subsystems/winuser/uiautomation.hpp"
#include "micant/um/subsystems/winuser/tsf.hpp"
#include "micant/um/native/ldr.hpp"
#include "micant/um/subsystems/net/mpr.hpp"
#include "micant/um/subsystems/winbase/virtdisk.hpp"
#include "micant/um/subsystems/security/wintrust.hpp"
#include "micant/um/subsystems/winbase/setupapi.hpp"
#include "micant/um/native/ntdll.hpp"
#include "micant/um/subsystems/security/aclui.hpp"
#include "micant/um/subsystems/winuser/winsta.hpp"
#include "micant/ucrt/icuuc.hpp"
#include "micant/um/subsystems/security/authz.hpp"
#include "micant/um/subsystems/mgmt/userenv.hpp"
#include "micant/um/subsystems/net/winhttp.hpp"
#include "micant/um/subsystems/winuser/imm32.hpp"
#include "micant/um/subsystems/winbase/powrprof.hpp"
#include "micant/um/subsystems/mgmt/dbgeng.hpp"
#include "micant/um/subsystems/com/winrt.hpp"
#include "micant/um/subsystems/net/iphlpapi.hpp"
#include "micant/um/subsystems/security/cipherksp.hpp"
#include "micant/um/subsystems/graphics/prism3d12.hpp"
#include "micant/um/subsystems/winuser/dwmapi.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#include "micant/um/subsystems/mgmt/dbghelp.hpp"
#include "micant/um/subsystems/media/winmm.hpp"
#include "micant/um/subsystems/winbase/cfgmgr32.hpp"

namespace micant::subsystems {

inline void InitializeAllSubsystemExports() noexcept {
    sensapi::InitializeSensApiSubsystemExports();
    comdlg32::InitializeComDlg32SubsystemExports();
    comctl32::InitializeComCtl32SubsystemExports();
    gdi32::InitializeGdi32SubsystemExports();
    user32::InitializeUser32SubsystemExports();
    advapi32::InitializeAdvapi32SubsystemExports();
    shell32::InitializeShell32SubsystemExports();
    win32::InitializeWin32SubsystemExports();
    msvcrt::InitializeMsvcrtSubsystemExports();
    msi::InitializeMsiSubsystemExports();
    sspi::InitializeSspiSubsystemExports();
    crypt32::InitializeCrypt32SubsystemExports();
    ws2_32::InitializeWs2_32SubsystemExports();
    ole32::InitializeOle32SubsystemExports();
    oleaut32::InitializeOleAut32SubsystemExports();
    propsys::InitializePropSysSubsystemExports();
    uxtheme::InitializeUxThemeSubsystemExports();
    urlmon::InitializeUrlMonSubsystemExports();
    wininet::InitializeWinINetSubsystemExports();
    winspool::InitializePrintSpoolerSubsystemExports();
    uiautomation::InitializeUIAutomationSubsystemExports();
    tsf::InitializeTextServicesExports();
    mpr::InitializeMprSubsystemExports();
    setupapi::InitializeSetupApiSubsystemExports();
    iphlpapi::InitializeIpHlpApiSubsystemExports();
    crypto::InitializeBCryptSubsystemExports();
    prism3d12::InitializePrism3D12SubsystemExports();
    icuuc::InitializeIcuucSubsystemExports();
    authz::InitializeAuthzSubsystemExports();
    userenv::InitializeUserenvSubsystemExports();
    winhttp::InitializeWinHttpSubsystemExports();
    imm32::InitializeImm32SubsystemExports();
    powrprof::InitializePowrProfSubsystemExports();
    dbgeng::InitializeDbgEngSubsystemExports();
    winrt::InitializeWinRTSubsystemExports();
    gdiplus::InitializeGdiPlusExports();
    virtdisk::InitializeVirtualDiskSubsystemExports();
    wintrust::InitializeWinTrustSubsystemExports();
    ntdll::InitializeNtdllSubsystemExports();
    aclui::InitializeAcluiSubsystemExports();
    winsta::InitializeWinStaSubsystemExports();
    dwm::InitializeDWMSubsystemExports();
    version::InitializeVersionExports();
    dbghelp::InitializeDbgHelpExports();
    winmm::InitializeWinMMExports();
    cfgmgr32::InitializeCfgMgr32SubsystemExports();
}

} // namespace micant::subsystems
