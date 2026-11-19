#!/usr/bin/env pwsh
<#
.SYNOPSIS
    CPU-Gentle Incremental Build Script for MicaNT (Ring 0 + Ring 1 + Ring 2 + Ring 3).
.DESCRIPTION
    1. Sets process priority to BelowNormal so compilation never pegs the host CPU or starves UI threads.
    2. Incrementally compiles only modified kernel/*.cpp units into build/clang_obj/*.o sequentially (max ~8% CPU on 12-thread Xeon).
    3. Links build/host/micant_kernel.exe, build/host/micant_tests.exe, and build/esp/EFI/BOOT/BOOTX64.EFI as requested.
#>

param(
    [switch]$Force,
    [switch]$SkipTests,
    [switch]$BuildEfi,
    [switch]$BuildKernel
)

$ErrorActionPreference = "Continue"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    $objDir = Join-Path $repoRoot "build\clang_obj"
    $binDir = Join-Path $repoRoot "bin"
    $hostDir = Join-Path $repoRoot "build\host"
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
    New-Item -ItemType Directory -Force -Path $hostDir | Out-Null

    $units = @(
        @{ Src = "kernel/ke/dispatcher.cpp";          Obj = "ke_dispatcher.o" },
        @{ Src = "kernel/ke/wait.cpp";                Obj = "ke_wait.o" },
        @{ Src = "kernel/hal/halinit.cpp";            Obj = "hal_halinit.o" },
        @{ Src = "kernel/mm/virtual.cpp";             Obj = "mm_virtual.o" },
        @{ Src = "kernel/ob/obref.cpp";               Obj = "ob_obref.o" },
        @{ Src = "kernel/ps/psmgr.cpp";               Obj = "ps_psmgr.o" },
        @{ Src = "kernel/io/iomgr.cpp";               Obj = "io_iomgr.o" },
        @{ Src = "kernel/se/ntsec.cpp";               Obj = "se_ntsec.o" },
        @{ Src = "kernel/cm/ntreg.cpp";               Obj = "cm_ntreg.o" },
        @{ Src = "kernel/lpc/ntlpc.cpp";              Obj = "lpc_ntlpc.o" },
        @{ Src = "kernel/ex/exmgr.cpp";               Obj = "ex_exmgr.o" },
        @{ Src = "kernel/cc/ccmgr.cpp";               Obj = "cc_ccmgr.o" },
        @{ Src = "kernel/fsrtl/fsrtlmgr.cpp";         Obj = "fsrtl_fsrtlmgr.o" },
        @{ Src = "kernel/pnp/pnpmgr.cpp";             Obj = "pnp_pnpmgr.o" },
        @{ Src = "kernel/po/pomgr.cpp";               Obj = "po_pomgr.o" },
        @{ Src = "kernel/wmi/wmimgr.cpp";             Obj = "wmi_wmimgr.o" },
        @{ Src = "kernel/dbgk/dbgkobj.cpp";           Obj = "dbgk_dbgkobj.o" },
        @{ Src = "kernel/tm/ktmmgr.cpp";              Obj = "tm_ktmmgr.o" },
        @{ Src = "kernel/verifier/vfmgr.cpp";         Obj = "verifier_vfmgr.o" },
        @{ Src = "kernel/arb/arbmgr.cpp";             Obj = "arb_arbmgr.o" },
        @{ Src = "kernel/io/pnpboot.cpp";             Obj = "io_pnpboot.o" },
        @{ Src = "kernel/io/pnpresilience.cpp";       Obj = "io_pnpresilience.o" },
        @{ Src = "kernel/po/pofx_whea.cpp";           Obj = "po_pofx_whea.o" },
        @{ Src = "kernel/po/bms.cpp";                 Obj = "po_bms.o" },
        @{ Src = "kernel/po/ppm.cpp";                 Obj = "po_ppm.o" },
        @{ Src = "kernel/po/thermal.cpp";             Obj = "po_thermal.o" },
        @{ Src = "kernel/io/umdf_msix.cpp";           Obj = "io_umdf_msix.o" },
        @{ Src = "kernel/ke/zerobsod.cpp";            Obj = "ke_zerobsod.o" },
        @{ Src = "kernel/io/livehotswap.cpp";         Obj = "io_livehotswap.o" },
        @{ Src = "kernel/ex/sysinfo_taskmgr.cpp";     Obj = "ex_sysinfo_taskmgr.o" },
        @{ Src = "kernel/ps/quotagov.cpp";            Obj = "ps_quotagov.o" },
        @{ Src = "kernel/fsrtl/mftzipui.cpp";         Obj = "fsrtl_mftzipui.o" },
        @{ Src = "kernel/se/srm_ci.cpp";              Obj = "se_srm_ci.o" },
        @{ Src = "kernel/io/afd_wsk.cpp";             Obj = "io_afd_wsk.o" },
        @{ Src = "kernel/ke/kuser_seal.cpp";          Obj = "ke_kuser_seal.o" },
        @{ Src = "kernel/fsrtl/ring1_vfs.cpp";        Obj = "fsrtl_ring1_vfs.o" },
        @{ Src = "kernel/io/ring1_dxgk.cpp";          Obj = "io_ring1_dxgk.o" },
        @{ Src = "kernel/io/ring1_audio.cpp";         Obj = "io_ring1_audio.o" },
        @{ Src = "kernel/ps/ring1_smss.cpp";          Obj = "ps_ring1_smss.o" },
        @{ Src = "kernel/io/ring2_wudf.cpp";          Obj = "io_ring2_wudf.o" },
        @{ Src = "kernel/se/ring2_lsa.cpp";           Obj = "se_ring2_lsa.o" },
        @{ Src = "kernel/ps/ring2_scm.cpp";           Obj = "ps_ring2_scm.o" },
        @{ Src = "kernel/ps/ring3_win32_base.cpp";       Obj = "ps_ring3_win32_base.o" },
        @{ Src = "kernel/ps/ring3_desktop_shell.cpp";    Obj = "ps_ring3_desktop_shell.o" },
        @{ Src = "kernel/ps/ring3_real_apps.cpp";        Obj = "ps_ring3_real_apps.o" },
        @{ Src = "kernel/io/ring3_prismx_3d12.cpp";                Obj = "io_ring3_prismx_3d12.o" },
        @{ Src = "kernel/io/ring3_directstorage_d2d.cpp";          Obj = "io_ring3_directstorage_d2d.o" },
        @{ Src = "kernel/io/ring3_prismaudio_mf.cpp";              Obj = "io_ring3_prismaudio_mf.o" },
        @{ Src = "kernel/io/ring3_winsock_winhttp_tls.cpp";        Obj = "io_ring3_winsock_winhttp_tls.o" },
        @{ Src = "kernel/lpc/ring3_com_rpc_shell.cpp";             Obj = "lpc_ring3_com_rpc_shell.o" },
        @{ Src = "kernel/se/ring3_bcrypt_crypt32_wintrust.cpp";    Obj = "se_ring3_bcrypt_crypt32_wintrust.o" },
        @{ Src = "kernel/io/ring3_fiber_net_apps.cpp";             Obj = "io_ring3_fiber_net_apps.o" },
        @{ Src = "kernel/ps/ring3_workstation_apps.cpp";           Obj = "ps_ring3_workstation_apps.o" },
        @{ Src = "kernel/ps/ring3_wsl2_hyperv_closure.cpp";        Obj = "ps_ring3_wsl2_hyperv_closure.o" },
        @{ Src = "kernel/io/prismx_directx_headers_kmt.cpp";       Obj = "io_prismx_directx_headers_kmt.o" },
        @{ Src = "kernel/io/prismx_dxmath_dxtex_dxmesh.cpp";       Obj = "io_prismx_dxmath_dxtex_dxmesh.o" },
        @{ Src = "kernel/io/prismx_d3d12_translation_on12.cpp";    Obj = "io_prismx_d3d12_translation_on12.o" },
        @{ Src = "kernel/io/prismx_dxc_sm68_pix.cpp";              Obj = "io_prismx_dxc_sm68_pix.o" },
        @{ Src = "kernel/io/prismx_directstorage_gdeflate.cpp";    Obj = "io_prismx_directstorage_gdeflate.o" },
        @{ Src = "kernel/io/prismx_directml_superres.cpp";         Obj = "io_prismx_directml_superres.o" },
        @{ Src = "kernel/io/prismx_enhanced_barriers_workgraphs.cpp"; Obj = "io_prismx_enhanced_barriers_workgraphs.o" },
        @{ Src = "kernel/io/prismx_vulkanon12_video_av1.cpp";         Obj = "io_prismx_vulkanon12_video_av1.o" },
        @{ Src = "kernel/io/prismx_dred_shadercache_zerotdr.cpp";     Obj = "io_prismx_dred_shadercache_zerotdr.o" },
        @{ Src = "kernel/io/prismx_live_3d_scene_framegraph.cpp";     Obj = "io_prismx_live_3d_scene_framegraph.o" },
        @{ Src = "kernel/io/prismx_gop_scanout_frame_pacing.cpp";     Obj = "io_prismx_gop_scanout_frame_pacing.o" },
        @{ Src = "kernel/io/prismx_game_benchmark_proton_seal.cpp";   Obj = "io_prismx_game_benchmark_proton_seal.o" },
        @{ Src = "kernel/io/prismx_directsr_vrs_rebar_gaming.cpp";    Obj = "io_prismx_directsr_vrs_rebar_gaming.o" },
        @{ Src = "kernel/io/prismx_execute_indirect_sampler_feedback.cpp"; Obj = "io_prismx_execute_indirect_sampler_feedback.o" },
        @{ Src = "kernel/io/prismx_gameinput_pix_classic_gaming_seal.cpp"; Obj = "io_prismx_gameinput_pix_classic_gaming_seal.o" },
        @{ Src = "kernel/io/prismx_hlsl_fxc_dxbc_reflection_compiler.cpp"; Obj = "io_prismx_hlsl_fxc_dxbc_reflection_compiler.o" },
        @{ Src = "kernel/io/prismx_dxil_sm69_wave32_simd_vm.cpp";           Obj = "io_prismx_dxil_sm69_wave32_simd_vm.o" },
        @{ Src = "kernel/io/prismx_spirv16_dxil_cross_translator_seal.cpp"; Obj = "io_prismx_spirv16_dxil_cross_translator_seal.o" },
        @{ Src = "kernel/se/ring0_anticheat_eac_battleye_callbacks.cpp";    Obj = "se_ring0_anticheat_eac_battleye_callbacks.o" },
        @{ Src = "kernel/se/ring0_vanguard_ricochet_hvci_iommu_shield.cpp"; Obj = "se_ring0_vanguard_ricochet_hvci_iommu_shield.o" },
        @{ Src = "kernel/se/horizon100_gaming_drm_hwid_vault_seal.cpp";     Obj = "se_horizon100_gaming_drm_hwid_vault_seal.o" },
        @{ Src = "kernel/io/prism_dxgi16_com_hierarchy.cpp";                Obj = "io_prism_dxgi16_com_hierarchy.o" },
        @{ Src = "kernel/io/prism_d3d12_vtable_options21.cpp";              Obj = "io_prism_d3d12_vtable_options21.o" },
        @{ Src = "kernel/io/prism_directx_system32_dll_router.cpp";         Obj = "io_prism_directx_system32_dll_router.o" },
        @{ Src = "kernel/io/prism_d3d9ex_com_vtable_abi.cpp";               Obj = "io_prism_d3d9ex_com_vtable_abi.o" },
        @{ Src = "kernel/io/prism_xaudio2_dinput8_com_abi.cpp";             Obj = "io_prism_xaudio2_dinput8_com_abi.o" },
        @{ Src = "kernel/io/prism_d2d1_dwrite_dcomp_11dll_seal.cpp";        Obj = "io_prism_d2d1_dwrite_dcomp_11dll_seal.o" },
        @{ Src = "kernel/io/prism_mfplat_mfreadwrite_cutscene_abi.cpp";     Obj = "io_prism_mfplat_mfreadwrite_cutscene_abi.o" },
        @{ Src = "kernel/io/prism_dxcore_adapter_factory_com_abi.cpp";      Obj = "io_prism_dxcore_adapter_factory_com_abi.o" },
        @{ Src = "kernel/io/prism_vulkan1_opengl32_16dll_seal.cpp";         Obj = "io_prism_vulkan1_opengl32_16dll_seal.o" },
        @{ Src = "kernel/ps/winnt_kernel32_advapi32_system32_abi.cpp";      Obj = "ps_winnt_kernel32_advapi32_system32_abi.o" },
        @{ Src = "kernel/ps/winnt_ole32_dwmapi_winmm_system32_seal.cpp";    Obj = "ps_winnt_ole32_dwmapi_winmm_system32_seal.o" },
        @{ Src = "kernel/ps/winnt_ntdll_ws2_iphlpapi_winhttp_abi.cpp";      Obj = "ps_winnt_ntdll_ws2_iphlpapi_winhttp_abi.o" },
        @{ Src = "kernel/se/winnt_bcrypt_crypt32_secur32_wintrust_abi.cpp"; Obj = "se_winnt_bcrypt_crypt32_secur32_wintrust_abi.o" },
        @{ Src = "kernel/ps/winnt_shell32_dbghelp_version_40dll_iat_seal.cpp"; Obj = "ps_winnt_shell32_dbghelp_version_40dll_iat_seal.o" },
        @{ Src = "kernel/ps/winnt_ucrtbase_runtime_abi.cpp";           Obj = "ps_winnt_ucrtbase_runtime_abi.o" },
        @{ Src = "kernel/ps/winnt_vcruntime140_eh_abi.cpp";            Obj = "ps_winnt_vcruntime140_eh_abi.o" },
        @{ Src = "kernel/ps/winnt_msvcp140_pdata_unwind_44dll_seal.cpp"; Obj = "ps_winnt_msvcp140_pdata_unwind_44dll_seal.o" },
        @{ Src = "kernel/fsrtl/extfs.cpp";                             Obj = "fsrtl_extfs.o" },
        @{ Src = "kernel/fsrtl/ntfs.cpp";                              Obj = "fsrtl_ntfs.o" },
        @{ Src = "kernel/fsrtl/exfat.cpp";                             Obj = "fsrtl_exfat.o" },
        @{ Src = "kernel/fsrtl/cdfs.cpp";                              Obj = "fsrtl_cdfs.o" },
        @{ Src = "kernel/fsrtl/refs.cpp";                              Obj = "fsrtl_refs.o" },
        @{ Src = "kernel/fsrtl/zfs.cpp";                               Obj = "fsrtl_zfs.o" }
    )

    $staleUnits = @()
    foreach ($u in $units) {
        $srcPath = Join-Path $repoRoot $u.Src
        $objPath = Join-Path $objDir $u.Obj
        if ($Force -or -not (Test-Path $objPath)) {
            $staleUnits += $u
        } else {
            $srcTime = (Get-Item $srcPath).LastWriteTimeUtc
            $objTime = (Get-Item $objPath).LastWriteTimeUtc
            if ($srcTime -gt $objTime) {
                $staleUnits += $u
            }
        }
    }

    Write-Host "[MicaNT Build] Incremental check: $($staleUnits.Count) / $($units.Count) kernel units need compilation (Single-Thread Sequential, Priority=BelowNormal)."
    foreach ($u in $staleUnits) {
        $src = $u.Src
        $obj = Join-Path "build/clang_obj" $u.Obj
        & clang++ -std=c++23 -O2 -Iinclude -Wno-unused-parameter -c $src -o $obj
        if ($LASTEXITCODE -ne 0) {
            throw "Failed compiling $src"
        }
    }

    $sys32Dir = Join-Path $binDir "system32"
    New-Item -ItemType Directory -Force -Path $sys32Dir | Out-Null
    $sys32Dlls = @(
        @{ Name = "kernel32.dll";       Src = "kernel/ps/winnt_kernel32_advapi32_system32_abi.cpp";         Def = "BUILD_SYSTEM32_KERNEL32_DLL" },
        @{ Name = "advapi32.dll";       Src = "kernel/ps/winnt_kernel32_advapi32_system32_abi.cpp";         Def = "BUILD_SYSTEM32_ADVAPI32_DLL" },
        @{ Name = "win32k.dll";          Src = "kernel/ps/winnt_win32k_server.cpp";                       Def = "MICANT_WIN32K_EXPORTS" },
        @{ Name = "gdi32.dll";          Src = "tools/dlls/gdi32.cpp";         Def = "BUILD_SYSTEM32_GDI32_DLL";  ImportWin32k = $true },
        @{ Name = "user32.dll";         Src = "tools/dlls/user32.cpp";        Def = "BUILD_SYSTEM32_USER32_DLL"; ImportWin32k = $true; ImportGdi32 = $true },
        @{ Name = "imm32.dll";          Src = "tools/dlls/imm32.cpp";         Def = "BUILD_SYSTEM32_IMM32_DLL";  ImportWin32k = $true },
        @{ Name = "ole32.dll";          Src = "kernel/ps/winnt_ole32_dwmapi_winmm_system32_seal.cpp";       Def = "BUILD_SYSTEM32_OLE32_DLL" },
        @{ Name = "dwmapi.dll";         Src = "kernel/ps/winnt_ole32_dwmapi_winmm_system32_seal.cpp";       Def = "BUILD_SYSTEM32_DWMAPI_DLL" },
        @{ Name = "winmm.dll";          Src = "kernel/ps/winnt_ole32_dwmapi_winmm_system32_seal.cpp";       Def = "BUILD_SYSTEM32_WINMM_DLL" },
        @{ Name = "ntdll.dll";          Src = "kernel/ps/winnt_ntdll_ws2_iphlpapi_winhttp_abi.cpp";         Def = "BUILD_SYSTEM32_NTDLL_DLL" },
        @{ Name = "ws2_32.dll";         Src = "kernel/ps/winnt_ntdll_ws2_iphlpapi_winhttp_abi.cpp";         Def = "BUILD_SYSTEM32_WS2_32_DLL" },
        @{ Name = "iphlpapi.dll";       Src = "kernel/ps/winnt_ntdll_ws2_iphlpapi_winhttp_abi.cpp";         Def = "BUILD_SYSTEM32_IPHLPAPI_DLL" },
        @{ Name = "winhttp.dll";        Src = "kernel/ps/winnt_ntdll_ws2_iphlpapi_winhttp_abi.cpp";         Def = "BUILD_SYSTEM32_WINHTTP_DLL" },
        @{ Name = "bcrypt.dll";         Src = "kernel/se/winnt_bcrypt_crypt32_secur32_wintrust_abi.cpp";    Def = "BUILD_SYSTEM32_BCRYPT_DLL" },
        @{ Name = "crypt32.dll";        Src = "kernel/se/winnt_bcrypt_crypt32_secur32_wintrust_abi.cpp";    Def = "BUILD_SYSTEM32_CRYPT32_DLL" },
        @{ Name = "secur32.dll";        Src = "kernel/se/winnt_bcrypt_crypt32_secur32_wintrust_abi.cpp";    Def = "BUILD_SYSTEM32_SECUR32_DLL" },
        @{ Name = "wintrust.dll";       Src = "kernel/se/winnt_bcrypt_crypt32_secur32_wintrust_abi.cpp";    Def = "BUILD_SYSTEM32_WINTRUST_DLL" },
        @{ Name = "shell32.dll";        Src = "kernel/ps/winnt_shell32_dbghelp_version_40dll_iat_seal.cpp"; Def = "BUILD_SYSTEM32_SHELL32_DLL" },
        @{ Name = "shlwapi.dll";        Src = "kernel/ps/winnt_shell32_dbghelp_version_40dll_iat_seal.cpp"; Def = "BUILD_SYSTEM32_SHLWAPI_DLL" },
        @{ Name = "dbghelp.dll";        Src = "kernel/ps/winnt_shell32_dbghelp_version_40dll_iat_seal.cpp"; Def = "BUILD_SYSTEM32_DBGHELP_DLL" },
        @{ Name = "version.dll";        Src = "kernel/ps/winnt_shell32_dbghelp_version_40dll_iat_seal.cpp"; Def = "BUILD_SYSTEM32_VERSION_DLL" },
        @{ Name = "ucrtbase.dll";       Src = "kernel/ps/winnt_ucrtbase_runtime_abi.cpp";            Def = "BUILD_SYSTEM32_UCRTBASE_DLL";      DefFile = "kernel/ps/ucrtbase.def" },
        @{ Name = "vcruntime140.dll";   Src = "kernel/ps/winnt_vcruntime140_eh_abi.cpp";             Def = "BUILD_SYSTEM32_VCRUNTIME140_DLL";  DefFile = "kernel/ps/vcruntime140.def" },
        @{ Name = "vcruntime140_1.dll"; Src = "kernel/ps/winnt_vcruntime140_eh_abi.cpp";             Def = "BUILD_SYSTEM32_VCRUNTIME140_1_DLL" },
        @{ Name = "msvcp140.dll";       Src = "kernel/ps/winnt_msvcp140_pdata_unwind_44dll_seal.cpp"; Def = "BUILD_SYSTEM32_MSVCP140_DLL" },
        @{ Name = "dxgi.dll";           Src = "kernel/io/prism_directx_system32_dll_router.cpp";       Def = "BUILD_SYSTEM32_DXGI_DLL" },
        @{ Name = "d3d12.dll";          Src = "kernel/io/prism_directx_system32_dll_router.cpp";       Def = "BUILD_SYSTEM32_D3D12_DLL" },
        @{ Name = "d3d11.dll";          Src = "kernel/io/prism_directx_system32_dll_router.cpp";       Def = "BUILD_SYSTEM32_D3D11_DLL" },
        @{ Name = "d3dcompiler_47.dll"; Src = "kernel/io/prism_directx_system32_dll_router.cpp";       Def = "BUILD_SYSTEM32_D3DCOMPILER_47_DLL" },
        @{ Name = "xinput1_4.dll";      Src = "kernel/io/prism_directx_system32_dll_router.cpp";       Def = "BUILD_SYSTEM32_XINPUT1_4_DLL" },
        @{ Name = "d3d9.dll";           Src = "kernel/io/prism_d3d9ex_com_vtable_abi.cpp";             Def = "BUILD_SYSTEM32_D3D9_DLL" },
        @{ Name = "xaudio2_9.dll";      Src = "kernel/io/prism_xaudio2_dinput8_com_abi.cpp";           Def = "BUILD_SYSTEM32_XAUDIO2_9_DLL" },
        @{ Name = "dinput8.dll";        Src = "kernel/io/prism_xaudio2_dinput8_com_abi.cpp";           Def = "BUILD_SYSTEM32_DINPUT8_DLL" },
        @{ Name = "d2d1.dll";           Src = "kernel/io/prism_d2d1_dwrite_dcomp_11dll_seal.cpp";      Def = "BUILD_SYSTEM32_D2D1_DLL" },
        @{ Name = "dwrite.dll";         Src = "kernel/io/prism_d2d1_dwrite_dcomp_11dll_seal.cpp";      Def = "BUILD_SYSTEM32_DWRITE_DLL" },
        @{ Name = "dcomp.dll";          Src = "kernel/io/prism_d2d1_dwrite_dcomp_11dll_seal.cpp";      Def = "BUILD_SYSTEM32_DCOMP_DLL" },
        @{ Name = "mfplat.dll";         Src = "kernel/io/prism_mfplat_mfreadwrite_cutscene_abi.cpp";   Def = "BUILD_SYSTEM32_MFPLAT_DLL" },
        @{ Name = "mfreadwrite.dll";    Src = "kernel/io/prism_mfplat_mfreadwrite_cutscene_abi.cpp";   Def = "BUILD_SYSTEM32_MFREADWRITE_DLL" },
        @{ Name = "dxcore.dll";         Src = "kernel/io/prism_dxcore_adapter_factory_com_abi.cpp";    Def = "BUILD_SYSTEM32_DXCORE_DLL" },
        @{ Name = "vulkan-1.dll";       Src = "kernel/io/prism_vulkan1_opengl32_16dll_seal.cpp";       Def = "BUILD_SYSTEM32_VULKAN_1_DLL" },
        @{ Name = "opengl32.dll";       Src = "kernel/io/prism_vulkan1_opengl32_16dll_seal.cpp";       Def = "BUILD_SYSTEM32_OPENGL32_DLL" }
    )
    $isMsvc = (clang++ -v 2>&1 | Select-String "Target:.*msvc") -ne $null
    $forceFlag = if ($isMsvc) { "-Wl,/FORCE:MULTIPLE" } else { "-Wl,--allow-multiple-definition" }
    foreach ($dll in $sys32Dlls) {
        $dllPath = Join-Path $sys32Dir $dll.Name
        $srcPath = Join-Path $repoRoot $dll.Src
        $win32kLibPath = Join-Path $sys32Dir $(if ($isMsvc) { "win32k.lib" } else { "libwin32k.dll.a" })
        $gdi32LibPath = Join-Path $sys32Dir $(if ($isMsvc) { "gdi32.lib" } else { "libgdi32.dll.a" })
        $staleWin32kImport = $dll.ImportWin32k -and (Test-Path $win32kLibPath) -and ((Get-Item $win32kLibPath).LastWriteTimeUtc -gt (Get-Item $dllPath -ErrorAction SilentlyContinue).LastWriteTimeUtc)
        $staleGdi32Import = $dll.ImportGdi32 -and (Test-Path $gdi32LibPath) -and ((Get-Item $gdi32LibPath).LastWriteTimeUtc -gt (Get-Item $dllPath -ErrorAction SilentlyContinue).LastWriteTimeUtc)
        if ($Force -or -not (Test-Path $dllPath) -or $staleWin32kImport -or $staleGdi32Import -or ((Get-Item $srcPath).LastWriteTimeUtc -gt (Get-Item $dllPath -ErrorAction SilentlyContinue).LastWriteTimeUtc)) {
            $extraArgs = @()
            $importDefinitions = @()
            if ($dll.Name -eq "win32k.dll") {
                if ($isMsvc) { $extraArgs += "-Wl,/IMPLIB:$win32kLibPath" }
                else { $extraArgs += "-Wl,--out-implib,$win32kLibPath" }
            } elseif ($dll.Name -eq "gdi32.dll") {
                if ($isMsvc) { $extraArgs += "-Wl,/IMPLIB:$gdi32LibPath" }
                else { $extraArgs += "-Wl,--out-implib,$gdi32LibPath" }
            } elseif ($dll.ImportWin32k) {
                $importDefinitions += "-DMICANT_WIN32K_IMPORTS"
                if ($isMsvc) { $extraArgs += $win32kLibPath }
                else { $extraArgs += @("-L$sys32Dir", "-lwin32k") }
            }
            if ($dll.ImportGdi32) {
                if ($isMsvc) { $extraArgs += $gdi32LibPath }
                else { $extraArgs += @("-L$sys32Dir", "-lgdi32") }
            }
            $runtimeFlags = @()
            if ($dll.Name -in @("win32k.dll", "user32.dll", "gdi32.dll", "imm32.dll") -and -not $isMsvc) { $runtimeFlags += "-static" }
            if ($dll.Name -in @("win32k.dll", "user32.dll", "gdi32.dll", "imm32.dll")) {
                $runtimeFlags += @("-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit", "-ffunction-sections", "-fdata-sections")
                if ($isMsvc) {
                    $extraArgs += @("-Wl,/ENTRY:DllMain", "-Wl,/OPT:REF")
                } else {
                    $extraArgs += @("-Wl,-e,DllMain", "-Wl,--gc-sections")
                }
            }
            if ($dll.DefFile) {
                if ($isMsvc) {
                    $extraArgs += "-Wl,/DEF:$((Join-Path $repoRoot $dll.DefFile).Replace('/', '\'))"
                } else {
                    $extraArgs += (Join-Path $repoRoot $dll.DefFile)
                }
            }
            & clang++ -std=c++23 -O2 -Iinclude -shared $forceFlag @runtimeFlags "-DBUILD_SYSTEM32_STANDALONE_DLL" "-D$($dll.Def)" @importDefinitions $dll.Src @extraArgs -o $dllPath
            if ($LASTEXITCODE -ne 0) { throw "Failed building System32 PE32+ DLL $($dll.Name)" }
        }
    }

    $allObjs = $units | ForEach-Object { Join-Path "build/clang_obj" $_.Obj }

    Write-Host "[MicaNT Build] Linking build/host/micant_kernel.exe..."
    & clang++ -std=c++23 -O2 -Iinclude kernel/init/kiinit.cpp @allObjs -o build/host/micant_kernel.exe
    if ($LASTEXITCODE -ne 0) { throw "Failed linking build/host/micant_kernel.exe" }

    if (-not $SkipTests) {
        Write-Host "[MicaNT Build] Linking build/host/micant_tests.exe..."
        & clang++ -std=c++23 -O2 -Iinclude -I. test/test_runner.cpp @allObjs -o build/host/micant_tests.exe
        if ($LASTEXITCODE -ne 0) { throw "Failed linking build/host/micant_tests.exe" }
    }

    if ($BuildKernel -or $BuildEfi) {
        Write-Host "[MicaNT Build] Building modular bare-metal kernel target..."
        & cmake --build build --target ntoskrnl_core_baremetal
        if ($LASTEXITCODE -ne 0) { throw "Failed building the modular ntoskrnl_core bare-metal target" }

        Write-Host "[MicaNT Build] Building standalone boot-start display driver (build/virtio_gpu.sys)..."
        & clang++ -target x86_64-w64-mingw32 -std=c++23 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -fno-builtin -mno-stack-arg-probe -mno-red-zone -O2 -Iinclude "-Wl,--subsystem,native" "-Wl,-e,DriverEntry" -nostdlib drivers/virtio_gpu/virtio_gpu.cpp -o build/virtio_gpu.sys
        if ($LASTEXITCODE -ne 0) { throw "Failed building virtio_gpu.sys" }
    }

    if ($BuildEfi) {
        Write-Host "[MicaNT Build] Building UEFI PE32+ Bootloader (build/esp/EFI/BOOT/BOOTX64.EFI)..."
        & clang++ -target x86_64-w64-mingw32 -std=c++23 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -mno-stack-arg-probe -mno-red-zone -O2 -Iinclude "-Wl,--subsystem,efi_application" "-Wl,-e,EfiMain" -nostdlib boot/bootx64.cpp -o build/esp/EFI/BOOT/BOOTX64.EFI
        if ($LASTEXITCODE -ne 0) { throw "Failed building BOOTX64.EFI" }

        $driverDir = Join-Path $repoRoot "build\esp\EFI\MicaNT\System32\drivers"
        New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
        Copy-Item -Force build/virtio_gpu.sys (Join-Path $driverDir "virtio_gpu.sys")
    }

    Write-Host "[MicaNT Build] SUCCESS: Incremental build complete with BelowNormal CPU priority."
} finally {
    Pop-Location
}
