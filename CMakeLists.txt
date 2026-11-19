cmake_minimum_required(VERSION 3.25)
project(MicaNT VERSION 0.1.0 LANGUAGES CXX C)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(MICANT_ENABLE_BAREMETAL "Build the AMD64 freestanding PE boot targets" OFF)
set(MICANT_BAREMETAL_KERNEL_NAME "ntoskrnl" CACHE STRING
    "Bare-metal kernel output name (keep ntoskrnl for the production profile)")
option(MICANT_BOOTSTRAP_RUNTIME_SELFTEST "Halt after kernel C++ lifecycle verification" OFF)
option(MICANT_BOOTSTRAP_TIMER_SELFTEST "Halt after isolated scheduler-clock tick verification" OFF)
option(MICANT_BOOTSTRAP_NETWORK_SELFTEST "Enable M10 receive-path fault injection" OFF)
option(MICANT_BOOTSTRAP_TASKMGR_SELFTEST "Launch taskmgr.exe from the mounted System32 image during QEMU verification" OFF)
option(MICANT_BOOTSTRAP_THEME_TEST "Launch theme_test.exe for QEMU theme verification" OFF)
option(MICANT_BOOTSTRAP_TEXTBOX_TEST "Launch the isolated STATIC textbox paint probe in QEMU" OFF)
option(MICANT_BOOTSTRAP_PROCESS_MEMORY_SELFTEST "Launch procdump.exe process-memory integration self-test" OFF)
option(MICANT_BOOTSTRAP_CRASH_DUMP_SELFTEST "Fault, persist, reboot and recover a LuceOS crash dump in QEMU" OFF)
option(MICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST "Exercise HDA PCI/MMIO rollback and reset in QEMU" OFF)
option(MICANT_BOOTSTRAP_AUDIO_RENDER_TEST "Launch a Ring 3 client and verify real HDA PCM DMA in QEMU" OFF)
option(MICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST "Launch a Ring 3 client and verify HDA ADC DMA in QEMU" OFF)
option(MICANT_BOOTSTRAP_CMD_TEST "Launch cmd.exe for QEMU terminal verification" OFF)
set(MICANT_BOOTSTRAP_REACTOS_APP "" CACHE STRING "Launch one verified original AMD64 ReactOS PE instead of Explorer")
set_property(CACHE MICANT_BOOTSTRAP_REACTOS_APP PROPERTY STRINGS "" winver notepad calc winmine sol mspaint regedit taskmgr wordpad cmd)
option(MICANT_COMPOSITOR_LOCAL_STATS "Emit aggregated compositor timing and repaint counters" OFF)
option(MICANT_KERNEL_PERF_DIAGNOSTICS "Emit aggregated 5-second Ke scheduler/accounting diagnostics" OFF)
option(MICANT_COMPOSITOR_DEBUG_HUD "Build the compositor debug HUD" OFF)

if (MICANT_BOOTSTRAP_RUNTIME_SELFTEST AND MICANT_BOOTSTRAP_TIMER_SELFTEST)
    message(FATAL_ERROR "Bootstrap runtime and timer self-tests are mutually exclusive")
endif()

if (MICANT_ENABLE_BAREMETAL)
    enable_language(RC)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

# Canonical Include Root (all headers addressed via micant/<layer>/<header>.hpp)
include_directories(
    include
)

# The imported driver models remain opt-in libraries/tests. They are deliberately
# not linked into ntoskrnl_core or ntoskrnl_core_baremetal.
set(MICANT_IMPORTED_DRIVERS
    acpi amx bluetooth bypassio cet cxl dsa graphics hdaudio hfi iommu ndis
    npu nvme pci pluton pmem qat rdma sriov tee ucsi usb usb4 wdf wifi)
foreach(_driver IN LISTS MICANT_IMPORTED_DRIVERS)
    add_subdirectory("drivers/${_driver}" EXCLUDE_FROM_ALL)
endforeach()

set(_micant_driver_test_targets)
foreach(_driver IN LISTS MICANT_IMPORTED_DRIVERS)
    list(APPEND _micant_driver_test_targets "test_${_driver}")
endforeach()
add_custom_target(micant_drivers_check
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^DriverTest_"
    DEPENDS ${_micant_driver_test_targets}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building and running the imported driver hosted checks")

# Explicit modular NT Kernel Executive & Microkernel source manifest.
# Historical sources under old/ are never kernel or driver build inputs.
set(NTOSKRNL_SUBSYSTEM_SOURCES
    kernel/ke/dispatcher.cpp
    kernel/ke/context_switch.cpp
    kernel/ke/process.cpp
    kernel/ke/wait.cpp
    kernel/ke/time.cpp
    kernel/win32k/win32k.cpp
    kernel/win32k/win32k_wddm.cpp
    kernel/mm/virtual.cpp
    kernel/io/iomgr.cpp
    kernel/ob/obref.cpp
    kernel/ps/psmgr.cpp
    kernel/cm/ntreg.cpp
    kernel/se/ntsec.cpp
    kernel/lpc/ntlpc.cpp
    kernel/cc/ccmgr.cpp
    kernel/fsrtl/fsrtlmgr.cpp
    kernel/fsrtl/extfs.cpp
    kernel/fsrtl/ntfs.cpp
    kernel/fsrtl/exfat.cpp
    kernel/fsrtl/cdfs.cpp
    kernel/fsrtl/refs.cpp
    kernel/fsrtl/zfs.cpp
    kernel/dbgk/dbgkobj.cpp
    kernel/tm/ktmmgr.cpp
    kernel/verifier/vfmgr.cpp
    kernel/arb/arbmgr.cpp
    kernel/wmi/wmimgr.cpp
    kernel/io/pnpboot.cpp
    kernel/io/pnpresilience.cpp
    kernel/ex/exmgr.cpp
    kernel/hal/halinit.cpp
    kernel/pnp/pnpmgr.cpp
    kernel/po/pomgr.cpp
    kernel/po/pofx_whea.cpp
    kernel/po/bms.cpp
    kernel/po/ppm.cpp
    kernel/po/thermal.cpp
    kernel/io/umdf_msix.cpp
    kernel/ke/zerobsod.cpp
    kernel/io/livehotswap.cpp
    kernel/ex/sysinfo_taskmgr.cpp
    kernel/ex/perf_taskmgr.cpp
    kernel/ps/quotagov.cpp
    kernel/fsrtl/mftzipui.cpp
    kernel/se/srm_ci.cpp
    kernel/io/afd_wsk.cpp
    kernel/ke/kuser_seal.cpp
    kernel/fsrtl/ring1_vfs.cpp
    kernel/io/ring1_dxgk.cpp
    kernel/io/ring1_audio.cpp
    kernel/io/hdaudio_baremetal.cpp
    kernel/ps/ring1_smss.cpp
    kernel/io/ring2_wudf.cpp
    kernel/se/ring2_lsa.cpp
    kernel/ps/ring2_scm.cpp
    kernel/ps/ring3_win32_base.cpp
    kernel/ps/ring3_desktop_shell.cpp
    kernel/ps/ring3_real_apps.cpp
    kernel/io/ring3_prismx_3d12.cpp
    kernel/io/ring3_directstorage_d2d.cpp
    kernel/io/ring3_prismaudio_mf.cpp
    kernel/io/ring3_winsock_winhttp_tls.cpp
    kernel/lpc/ring3_com_rpc_shell.cpp
    kernel/se/ring3_bcrypt_crypt32_wintrust.cpp
    kernel/io/ring3_fiber_net_apps.cpp
    kernel/ps/ring3_workstation_apps.cpp
    kernel/ps/ring3_wsl2_hyperv_closure.cpp
    kernel/io/prismx_directx_headers_kmt.cpp
    kernel/io/prismx_dxmath_dxtex_dxmesh.cpp
    kernel/io/prismx_d3d12_translation_on12.cpp
    kernel/io/prismx_dxc_sm68_pix.cpp
    kernel/io/prismx_directstorage_gdeflate.cpp
    kernel/io/prismx_directml_superres.cpp
    kernel/io/prismx_enhanced_barriers_workgraphs.cpp
    kernel/io/prismx_vulkanon12_video_av1.cpp
    kernel/io/prismx_dred_shadercache_zerotdr.cpp
    kernel/io/prismx_live_3d_scene_framegraph.cpp
    kernel/io/prismx_gop_scanout_frame_pacing.cpp
    kernel/io/prismx_game_benchmark_proton_seal.cpp
    kernel/io/prismx_directsr_vrs_rebar_gaming.cpp
    kernel/io/prismx_execute_indirect_sampler_feedback.cpp
    kernel/io/prismx_gameinput_pix_classic_gaming_seal.cpp
    kernel/io/prismx_hlsl_fxc_dxbc_reflection_compiler.cpp
    kernel/io/prismx_dxil_sm69_wave32_simd_vm.cpp
    kernel/io/prismx_spirv16_dxil_cross_translator_seal.cpp
    kernel/se/ring0_anticheat_eac_battleye_callbacks.cpp
    kernel/se/ring0_vanguard_ricochet_hvci_iommu_shield.cpp
    kernel/se/horizon100_gaming_drm_hwid_vault_seal.cpp
    kernel/io/prism_dxgi16_com_hierarchy.cpp
    kernel/io/prism_d3d12_vtable_options21.cpp
    kernel/io/prism_directx_system32_dll_router.cpp
    kernel/io/prism_d3d9ex_com_vtable_abi.cpp
    kernel/io/prism_xaudio2_dinput8_com_abi.cpp
    kernel/io/prism_d2d1_dwrite_dcomp_11dll_seal.cpp
    kernel/io/prism_mfplat_mfreadwrite_cutscene_abi.cpp
    kernel/io/prism_dxcore_adapter_factory_com_abi.cpp
    kernel/io/prism_vulkan1_opengl32_16dll_seal.cpp
    kernel/ps/winnt_shell32_dbghelp_version_40dll_iat_seal.cpp
    kernel/ps/winnt_ucrtbase_runtime_abi.cpp
    kernel/ps/winnt_vcruntime140_eh_abi.cpp
    kernel/ps/winnt_msvcp140_pdata_unwind_44dll_seal.cpp
)

add_library(ntoskrnl_core STATIC
    ${NTOSKRNL_SUBSYSTEM_SOURCES}
)

# Win32 user APIs are independent user-mode PE DLLs. Keep them out of
# ntoskrnl_core and compile each DLL from its own implementation file.
if(WIN32)
    # The window manager has one process-wide state owner. User32 resolves this
    # DLL at runtime so it never embeds a private copy of TitanWin32kSubsystem.
    add_library(micant_win32k SHARED kernel/ps/winnt_win32k_server.cpp)
    target_compile_definitions(micant_win32k PRIVATE
        BUILD_SYSTEM32_STANDALONE_DLL MICANT_WIN32K_EXPORTS MICANT_NATIVE_USERMODE)
    target_link_libraries(micant_win32k PRIVATE ntdll)
    if(MINGW)
        target_link_options(micant_win32k PRIVATE -static)
    endif()
    set_target_properties(micant_win32k PROPERTIES
        OUTPUT_NAME "win32k"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/system32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/system32")

    foreach(_dll IN ITEMS user32 gdi32 imm32 uxtheme)
        add_library(micant_${_dll} SHARED
            tools/dlls/${_dll}.cpp)
        add_dependencies(micant_${_dll} micant_win32k)
        target_compile_definitions(micant_${_dll} PRIVATE
            BUILD_SYSTEM32_STANDALONE_DLL MICANT_WIN32K_IMPORTS MICANT_NATIVE_USERMODE)
        if(_dll STREQUAL "user32")
            target_compile_definitions(micant_${_dll} PRIVATE BUILD_SYSTEM32_USER32_DLL)
        endif()
        target_link_libraries(micant_${_dll} PRIVATE micant_win32k ntdll)
        if(_dll STREQUAL "user32")
            target_link_libraries(micant_${_dll} PRIVATE micant_gdi32 micant_uxtheme)
        endif()
        if(MINGW)
            target_link_options(micant_${_dll} PRIVATE -static)
        endif()
        set_target_properties(micant_${_dll} PROPERTIES
            OUTPUT_NAME "${_dll}"
            PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/system32"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/system32")
    endforeach()
endif()

# Hosted initialization harness. This is not a freestanding kernel image.
add_executable(micant_kernel
    EXCLUDE_FROM_ALL
    kernel/init/kiinit.cpp
)
target_link_libraries(micant_kernel PRIVATE ntoskrnl_core)

target_compile_definitions(micant_kernel PRIVATE
    MICANT_VERSION_MAJOR=0
    MICANT_VERSION_MINOR=1
    MICANT_VERSION_PATCH=0
    _UNICODE
    UNICODE
)

# Compile check for the per-DLL ABI translation units. Each wrapper selects
# exactly one export family from its shared implementation fragment; keeping
# these in an object target checks every DLL without combining their exports
# into one image.
add_library(micant_tests OBJECT
    tools/dlls/abi/kernel32_system32.cpp
    tools/dlls/abi/advapi32_system32.cpp
    tools/dlls/abi/ntdll_system32.cpp
    tools/dlls/abi/ws2_32_system32.cpp
    tools/dlls/abi/iphlpapi_system32.cpp
    tools/dlls/abi/winhttp_system32.cpp
    tools/dlls/abi/ole32_system32.cpp
    tools/dlls/abi/dwmapi_system32.cpp
    tools/dlls/abi/winmm_system32.cpp
    tools/dlls/abi/bcrypt_system32.cpp
    tools/dlls/abi/crypt32_system32.cpp
    tools/dlls/abi/secur32_system32.cpp
    tools/dlls/abi/wintrust_system32.cpp
)
target_include_directories(micant_tests PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(micant_tests PRIVATE
    MICANT_NATIVE_USERMODE BUILD_SYSTEM32_STANDALONE_DLL)
set_source_files_properties(tools/dlls/abi/kernel32_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS "BUILD_SYSTEM32_KERNEL32_DLL;MICANT_FREESTANDING_KERNEL32")
set_source_files_properties(tools/dlls/abi/advapi32_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_ADVAPI32_DLL)
set_source_files_properties(tools/dlls/abi/ntdll_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_NTDLL_DLL)
set_source_files_properties(tools/dlls/abi/ws2_32_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_WS2_32_DLL)
set_source_files_properties(tools/dlls/abi/iphlpapi_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_IPHLPAPI_DLL)
set_source_files_properties(tools/dlls/abi/winhttp_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_WINHTTP_DLL)
set_source_files_properties(tools/dlls/abi/ole32_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_OLE32_DLL)
set_source_files_properties(tools/dlls/abi/dwmapi_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_DWMAPI_DLL)
set_source_files_properties(tools/dlls/abi/winmm_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_WINMM_DLL)
set_source_files_properties(tools/dlls/abi/bcrypt_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_BCRYPT_DLL)
set_source_files_properties(tools/dlls/abi/crypt32_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_CRYPT32_DLL)
set_source_files_properties(tools/dlls/abi/secur32_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_SECUR32_DLL)
set_source_files_properties(tools/dlls/abi/wintrust_system32.cpp PROPERTIES
    COMPILE_DEFINITIONS BUILD_SYSTEM32_WINTRUST_DLL)

# Historical broad host runner retained for targeted recovery; its removed
# AFD/RIO fixture and host service bindings are outside the DLL ABI compile gate.
add_executable(micant_legacy_tests EXCLUDE_FROM_ALL test/test_runner.cpp)
target_link_libraries(micant_legacy_tests PRIVATE ntoskrnl_core)
target_compile_definitions(micant_legacy_tests PRIVATE _UNICODE UNICODE)

# Standalone Ring 3 Userland Test Application
add_executable(userland_app
    EXCLUDE_FROM_ALL
    test/userland_app.cpp
)
target_link_libraries(userland_app PRIVATE ntoskrnl_core)

# Standalone Win32 Userland Test Application
add_executable(win32_app
    EXCLUDE_FROM_ALL
    test/win32_app.cpp
)
target_link_libraries(win32_app PRIVATE ntoskrnl_core)

# Standalone PrismX 3D & Vulkan / Direct3D 12 Hardware Verification Harness
add_executable(prismx_3d_cube_app
    EXCLUDE_FROM_ALL
    test/prismx_3d_cube_app.cpp
)
target_link_libraries(prismx_3d_cube_app PRIVATE ntoskrnl_core)
target_link_options(prismx_3d_cube_app PRIVATE -static)

# Standalone Unmodified Third-Party Windows Test Application (Zero MicaNT headers)
if (MSVC)
    add_executable(unmodified_sample
        test/unmodified_sample.cpp
    )
    target_compile_options(unmodified_sample PRIVATE /O2 /GS-)
    target_link_options(unmodified_sample PRIVATE /SUBSYSTEM:CONSOLE /NODEFAULTLIB /ENTRY:entry)
    target_link_libraries(unmodified_sample PRIVATE kernel32)
    set_target_properties(unmodified_sample PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# Bare-Metal UEFI Bootloader (bootx64)
add_library(bootx64 OBJECT
    boot/bootx64.cpp
)

if (MICANT_ENABLE_BAREMETAL)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "The freestanding AMD64 PE targets require a Clang/GNU cross-toolchain.")
    endif()

    set(_ntdll_probe_dll "${CMAKE_CURRENT_BINARY_DIR}/ntdll-probe.dll")
    set(_ntdll_probe_header "${CMAKE_CURRENT_BINARY_DIR}/ntdll-probe.inc")
    add_custom_command(OUTPUT "${_ntdll_probe_dll}"
        COMMAND "${CMAKE_CXX_COMPILER}" -std=c++23 -ffreestanding -fno-exceptions
            -fno-rtti -fno-stack-protector -shared -nostdlib -fuse-ld=lld -Wl,--entry=
            -Wl,--subsystem,native -Wl,--image-base,0x400000020000 -O2
            -I "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/tools/dlls/ntdll.cpp"
            "${CMAKE_SOURCE_DIR}/tools/dlls/native_stack_probe.cpp" -o "${_ntdll_probe_dll}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/dlls/ntdll.cpp"
            "${CMAKE_SOURCE_DIR}/tools/dlls/native_stack_probe.cpp"
            "${CMAKE_SOURCE_DIR}/include/micant/shared/context.hpp"
            "${CMAKE_SOURCE_DIR}/include/micant/shared/syscall_numbers.hpp" VERBATIM)
    add_custom_command(OUTPUT "${_ntdll_probe_header}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${_ntdll_probe_dll} -DOUTPUT=${_ntdll_probe_header}
            -P "${CMAKE_SOURCE_DIR}/scripts/embed_binary.cmake"
        DEPENDS "${_ntdll_probe_dll}" "${CMAKE_SOURCE_DIR}/scripts/embed_binary.cmake" VERBATIM)

    set(_ntdll_process_dll "${CMAKE_CURRENT_BINARY_DIR}/ntdll-process.dll")
    set(_ntdll_process_header "${CMAKE_CURRENT_BINARY_DIR}/ntdll-process.inc")
    add_custom_command(OUTPUT "${_ntdll_process_dll}"
        COMMAND "${CMAKE_CXX_COMPILER}" -std=c++23 -ffreestanding -fno-exceptions
            -fno-rtti -fno-stack-protector -shared -nostdlib -fuse-ld=lld -Wl,--entry=
            -Wl,--subsystem,native -Wl,--image-base,0x400020f00000 -O2
            -I "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/tools/dlls/ntdll.cpp"
            "${CMAKE_SOURCE_DIR}/tools/dlls/native_stack_probe.cpp" -o "${_ntdll_process_dll}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/dlls/ntdll.cpp"
            "${CMAKE_SOURCE_DIR}/tools/dlls/native_stack_probe.cpp"
            "${CMAKE_SOURCE_DIR}/include/micant/shared/context.hpp"
            "${CMAKE_SOURCE_DIR}/include/micant/shared/syscall_numbers.hpp" VERBATIM)
    add_custom_command(OUTPUT "${_ntdll_process_header}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${_ntdll_process_dll} -DOUTPUT=${_ntdll_process_header}
            -P "${CMAKE_SOURCE_DIR}/scripts/embed_binary.cmake"
        DEPENDS "${_ntdll_process_dll}" "${CMAKE_SOURCE_DIR}/scripts/embed_binary.cmake" VERBATIM)
    add_custom_target(ntdll_process_image DEPENDS "${_ntdll_process_dll}")

    add_executable(ntoskrnl_core_baremetal
        "${_ntdll_probe_header}"
        "${_ntdll_process_header}"
        kernel/init/kiinit.cpp
        kernel/init/network_bootstrap.cpp
        kernel/ke/context_switch.cpp
        kernel/ke/process.cpp
        kernel/ke/event.cpp
        kernel/ke/time.cpp
        kernel/cm/ntreg.cpp
        kernel/hal/amd64_bootstrap.cpp
        kernel/hal/amd64_firmware_interrupts.cpp
        kernel/win32k/freestanding.cpp
        kernel/ex/freestanding_memory.cpp
        kernel/ex/freestanding.cpp
        kernel/ex/perf_taskmgr.cpp
        kernel/ex/libcpp_support.cpp
        kernel/ob/obref.cpp
        kernel/io/file_object.cpp
        kernel/io/afd_wsk.cpp
        kernel/io/block_device.cpp
        kernel/io/hdaudio_baremetal.cpp
        kernel/io/usb_host.cpp
        kernel/io/hid_input.cpp
        kernel/fsrtl/fat_boot.cpp
        kernel/mm/virtual.cpp
        kernel/ps/address_space.cpp
        kernel/ps/apc.cpp
        kernel/ps/exception.cpp
        kernel/ps/psmgr.cpp
        kernel/ps/object.cpp
        kernel/ps/image.cpp
        kernel/se/freestanding.cpp
        test/kernel_context_canary.cpp
        test/kernel_ke_ready_queue_test.cpp
        test/kernel_ke_block_wake_test.cpp
        test/kernel_ke_apc_fifo_test.cpp
        test/kernel_ke_deferred_delivery_test.cpp
        test/kernel_ke_event_test.cpp
        test/kernel_ob_open_access_test.cpp
        test/kernel_se_access_test.cpp
        test/kernel_ps_token_test.cpp
        test/kernel_ps_thread_test.cpp
        test/kernel_ps_user_test.cpp
        test/kernel_ps_two_user_threads_test.cpp
        test/kernel_ps_file_image_test.cpp
        test/kernel_mm_avl_test.cpp
        test/kernel_mm_process_test.cpp
        test/kernel_partition_table_test.cpp
        test/pe_tls_freestanding_test.cpp
        tools/explorer_shell/desktop.cpp
        tools/explorer_shell/notification_area.cpp
        tools/explorer_shell/taskbar.cpp
    )
    target_include_directories(ntoskrnl_core_baremetal PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/tools"
        "${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_FREESTANDING MICANT_BAREMETAL)
    if (MICANT_KERNEL_PERF_DIAGNOSTICS)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_KERNEL_PERF_DIAGNOSTICS=1)
    endif()
    if (MICANT_BOOTSTRAP_RUNTIME_SELFTEST)
        target_sources(ntoskrnl_core_baremetal PRIVATE test/kernel_cxx_lifecycle_fixture.cpp)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_KERNEL_CXX_LIFECYCLE_SELFTEST)
    endif()
    if (MICANT_BOOTSTRAP_TIMER_SELFTEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_TIMER_SELFTEST)
    endif()
    if (MICANT_BOOTSTRAP_NETWORK_SELFTEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_NETWORK_SELFTEST)
    endif()
    if (MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    endif()
    if (MICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST)
    endif()
    if (MICANT_BOOTSTRAP_AUDIO_RENDER_TEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_AUDIO_RENDER_TEST)
    endif()
    if (MICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST)
    endif()
    target_compile_options(ntoskrnl_core_baremetal PRIVATE -fno-builtin)

    add_executable(bootx64_efi
        boot/bootx64.cpp
    )
    target_include_directories(bootx64_efi PRIVATE "${CMAKE_SOURCE_DIR}")

    add_library(win32k_boot SHARED EXCLUDE_FROM_ALL kernel/ps/winnt_win32k_server.cpp)
    target_compile_definitions(win32k_boot PRIVATE
        BUILD_SYSTEM32_STANDALONE_DLL MICANT_WIN32K_EXPORTS
    )
    target_compile_options(win32k_boot PRIVATE
        -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
        -ffunction-sections -fdata-sections -O2
    )
    target_link_options(win32k_boot PRIVATE -static -Wl,--gc-sections)
    target_link_libraries(win32k_boot PRIVATE ntdll_boot)
    set_target_properties(win32k_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "win32k"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    # LuceGlass carries only the private Liquid Glass descriptor contract.
    # Win32k validates and publishes it; the software effect runs in compositor.
    add_library(luceglass_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/luceglass.cpp tools/dlls/luceglass.def)
    target_include_directories(luceglass_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(luceglass_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(luceglass_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -O2)
    target_link_options(luceglass_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,--entry= -Wl,--subsystem,native)
    target_link_libraries(luceglass_boot PRIVATE win32k_boot ntdll_boot)
    set_target_properties(luceglass_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "luceglass"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports")

    foreach(_client IN ITEMS user32 gdi32 imm32)
        add_library(${_client}_boot SHARED EXCLUDE_FROM_ALL
            tools/dlls/${_client}.cpp
        )
        target_compile_definitions(${_client}_boot PRIVATE
            BUILD_SYSTEM32_STANDALONE_DLL
            MICANT_WIN32K_IMPORTS
        )
        if(_client STREQUAL "user32")
            target_compile_definitions(${_client}_boot PRIVATE
                BUILD_SYSTEM32_USER32_DLL)
        endif()
        if(_client STREQUAL "user32" OR _client STREQUAL "gdi32")
            target_compile_definitions(${_client}_boot PRIVATE MICANT_NATIVE_USERMODE)
        endif()
        target_compile_options(${_client}_boot PRIVATE
            -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
            -ffunction-sections -fdata-sections -O2
        )
        target_link_options(${_client}_boot PRIVATE -static -Wl,--gc-sections)
        target_link_libraries(${_client}_boot PRIVATE win32k_boot)
        set_target_properties(${_client}_boot PROPERTIES
            PREFIX "" OUTPUT_NAME "${_client}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
        )
    endforeach()

    # Shell32's icon extraction stays in Ring 3 and delegates HICON ownership
    # to User32. This target is intentionally separate from the historical
    # kernel API seal source.
    add_library(shell32_boot SHARED EXCLUDE_FROM_ALL tools/dlls/shell32.cpp
        tools/dlls/shell32_icon.cpp
        kernel/ex/freestanding_memory.cpp)
    target_include_directories(shell32_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(shell32_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(shell32_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -O2
    )
    target_link_options(shell32_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(shell32_boot PRIVATE user32_boot gdi32_boot ntdll_boot)
    set_target_properties(shell32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "shell32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    # Common-dialog exports are a separate Ring 3 DLL. Until the dialog server
    # is connected, these ABI-correct entry points report explicit failure.
    add_library(comdlg32_boot SHARED EXCLUDE_FROM_ALL tools/dlls/comdlg32.cpp)
    target_compile_options(comdlg32_boot PRIVATE
        -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -fno-use-cxa-atexit -fno-stack-protector -mno-red-zone -O2)
    target_link_options(comdlg32_boot PRIVATE
        -nostdlib -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase)
    set_target_properties(comdlg32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "comdlg32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports")

    # Core COM ABI exports are kept in the Ole32 user-mode DLL. COM apartment
    # state and class activation return E_NOTIMPL until their service exists.
    add_library(ole32_boot SHARED EXCLUDE_FROM_ALL tools/dlls/ole32.cpp)
    target_compile_options(ole32_boot PRIVATE
        -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -fno-use-cxa-atexit -fno-stack-protector -mno-red-zone -O2)
    target_link_options(ole32_boot PRIVATE
        -nostdlib -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase)
    set_target_properties(ole32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "ole32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports")

    # Comctl32 image lists are process-local Ring 3 objects. Bitmap/icon bytes
    # cross the existing GDI32/User32 DLL interfaces; allocation uses NTDLL.
    add_library(comctl32_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/comctl32.cpp tools/dlls/comctl32_icon.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(comctl32_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(comctl32_boot PRIVATE
        MICANT_NATIVE_USERMODE MICANT_WIN32K_IMPORTS)
    target_compile_options(comctl32_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -O2
    )
    target_link_options(comctl32_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(comctl32_boot PRIVATE
        gdi32_boot user32_boot uxtheme_boot ntdll_boot win32k_boot)
    set_target_properties(comctl32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "comctl32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    # UxTheme is a native Ring 3 client of the mode-neutral theme snapshot and
    # Win32k GDI surface ABI. Keep kernel Executive and User32 implementation
    # headers out of the DLL's transitive include closure.
    add_library(uxtheme_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/uxtheme.cpp tools/dlls/native_stack_probe.cpp
        kernel/ex/freestanding_memory.cpp tools/dlls/uxtheme.def)
    target_include_directories(uxtheme_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(uxtheme_boot PRIVATE MICANT_NATIVE_USERMODE MICANT_WIN32K_IMPORTS MICANT_UXTHEME_GDI_IMPORTS)
    target_compile_options(uxtheme_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -O2
    )
    target_link_options(uxtheme_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(uxtheme_boot PRIVATE win32k_boot gdi32_boot)
    set_target_properties(uxtheme_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "uxtheme"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_library(dwrite_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/dwrite.cpp tools/dlls/native_stack_probe.cpp
        kernel/ex/freestanding_memory.cpp)
    target_include_directories(dwrite_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(dwrite_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(dwrite_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -O2
    )
    target_link_options(dwrite_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(dwrite_boot PRIVATE ntdll_boot)
    set_target_properties(dwrite_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "dwrite"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    set(_explorer_cursor_resource_dir "${CMAKE_CURRENT_BINARY_DIR}/explorer-cursor-resources")
    set(_explorer_cursor_resource_rc "${_explorer_cursor_resource_dir}/cursor_resources.rc")
    set(_explorer_cursor_group "${_explorer_cursor_resource_dir}/cursor_group.bin")
    set(_explorer_cursor_payload "${_explorer_cursor_resource_dir}/cursor_payload.bin")
    add_custom_command(
        OUTPUT "${_explorer_cursor_resource_rc}" "${_explorer_cursor_group}"
               "${_explorer_cursor_payload}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/generate_cursor_resource.py"
                "${CMAKE_SOURCE_DIR}/System/Cursors/MiniMouse/mini_mouse_arrow.cur"
                "${_explorer_cursor_group}" "${_explorer_cursor_payload}"
                "${_explorer_cursor_resource_rc}"
        DEPENDS tools/generate_cursor_resource.py
                System/Cursors/MiniMouse/mini_mouse_arrow.cur
        VERBATIM
        COMMENT "Preparing Explorer PE group-cursor resource fixture")
    set_source_files_properties("${_explorer_cursor_resource_rc}" PROPERTIES GENERATED TRUE)

    add_executable(explorer_boot EXCLUDE_FROM_ALL
        tools/explorer_shell_app.cpp
        "${_explorer_cursor_resource_rc}"
        tools/explorer_shell/desktop.cpp
        tools/explorer_shell/taskbar.cpp
        tools/explorer_shell/start_menu.cpp
        tools/explorer_shell/notification_area.cpp
        tools/explorer_shell/task_switcher.cpp
        include/micant/um/subsystems/imaging/wallpaper.cpp
        kernel/ex/freestanding_memory.cpp
    )
    target_include_directories(explorer_boot PRIVATE "${CMAKE_SOURCE_DIR}/tools")
    target_compile_options(explorer_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(explorer_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
    )
    target_compile_definitions(explorer_boot PRIVATE MICANT_UXTHEME_IMPORTS)
    target_link_libraries(explorer_boot PRIVATE
        user32_boot gdi32_boot win32k_boot dwrite_boot uxtheme_boot comctl32_boot
        m)
    set_target_properties(explorer_boot PROPERTIES
        OUTPUT_NAME "explorer"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    add_executable(mac_shell_boot EXCLUDE_FROM_ALL tools/mac_shell_app.cpp
        include/micant/um/subsystems/imaging/wallpaper.cpp
        kernel/ex/freestanding_memory.cpp)
    target_include_directories(mac_shell_boot PRIVATE "${CMAKE_SOURCE_DIR}/tools")
    target_compile_options(mac_shell_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2)
    target_link_options(mac_shell_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start)
    target_link_libraries(mac_shell_boot PRIVATE
        user32_boot gdi32_boot win32k_boot kernel32_boot ntdll_boot luceglass_boot m)
    set_target_properties(mac_shell_boot PROPERTIES
        OUTPUT_NAME "mac_shell"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
    # Task Manager uses native User32/GDI32 exports for its HWND and paint loop;
    # the APIs reach the canonical Win32k server through the existing DLL path.
    add_executable(taskmgr_boot EXCLUDE_FROM_ALL tools/taskmgr_app.cpp)
    target_include_directories(taskmgr_boot PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/tools"
    )
    target_compile_options(taskmgr_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(taskmgr_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
    )
    target_link_libraries(taskmgr_boot PRIVATE user32_boot gdi32_boot uxtheme_boot)
    set_target_properties(taskmgr_boot PROPERTIES
        OUTPUT_NAME "taskmgr"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    # DOOM is built from the upstream doomgeneric core with a small LuceOS
    # Win32 platform backend. Keep the upstream GPL sources and notice intact;
    # the platform path uses the native User32/GDI32/Kernel32 DLLs.
    file(STRINGS "${CMAKE_SOURCE_DIR}/third_party/doomgeneric/doomgeneric/Makefile.win"
         _doomgeneric_manifest REGEX "^SRC_DOOM = ")
    string(REGEX REPLACE "^SRC_DOOM = " "" _doomgeneric_manifest "${_doomgeneric_manifest}")
    string(REPLACE ".o" ".c" _doomgeneric_manifest "${_doomgeneric_manifest}")
    string(REPLACE " " ";" _doomgeneric_sources "${_doomgeneric_manifest}")
    set(_doomgeneric_engine_sources)
    foreach(_doomgeneric_source IN LISTS _doomgeneric_sources)
        if (_doomgeneric_source AND
            NOT _doomgeneric_source STREQUAL "doomgeneric_win.c" AND
            NOT _doomgeneric_source STREQUAL "w_file_stdc.c")
            list(APPEND _doomgeneric_engine_sources
                "${CMAKE_SOURCE_DIR}/third_party/doomgeneric/doomgeneric/${_doomgeneric_source}")
        endif()
    endforeach()
    add_executable(doom_boot EXCLUDE_FROM_ALL
        ${_doomgeneric_engine_sources}
        tools/doom_platform.cpp
        tools/doom_audio.cpp
        tools/doom_crt.cpp
        tools/doom_wad_file.cpp
    )
    target_include_directories(doom_boot PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/doomgeneric/doomgeneric"
        "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_definitions(doom_boot PRIVATE
        _WIN32 WIN32_LEAN_AND_MEAN DOOMGENERIC_RESX=640 DOOMGENERIC_RESY=400
        MICANT_NATIVE_USERMODE MICANT_GDI32_IMPORTS FEATURE_SOUND MICANT_DOOM_NATIVE_AUDIO
    )
    target_compile_options(doom_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(doom_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
    )
    target_link_libraries(doom_boot PRIVATE
        user32_boot gdi32_boot kernel32_boot ucrtbase_boot ntdll_boot m
    )
    set_target_properties(doom_boot PROPERTIES
        OUTPUT_NAME "doom"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    # CMD is a separate Ring 3 PE. Its graphical frontend reaches Win32k and
    # GDI through the native client DLLs; its command engine uses NTDLL file
    # syscalls rather than kernel Executive headers.
    file(GLOB _micant_cmd_reactos_sources CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/cmd/reactos/*.cpp"
        "${CMAKE_SOURCE_DIR}/tools/cmd/conutils/*.cpp")
    add_library(cmd_reactos_core STATIC EXCLUDE_FROM_ALL
        ${_micant_cmd_reactos_sources})
    target_include_directories(cmd_reactos_core PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/tools/cmd"
        "${CMAKE_SOURCE_DIR}/tools/cmd/reactos"
        "${CMAKE_SOURCE_DIR}/tools/cmd/conutils")
    target_compile_definitions(cmd_reactos_core PRIVATE
        MICANT_NATIVE_USERMODE MICANT_CMD_GUI_TERMINAL
        UNICODE _UNICODE
        KERNEL_VERSION_STR="${PROJECT_VERSION}"
        KERNEL_VERSION_BUILD_STR="Luce")
    target_compile_options(cmd_reactos_core PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -mno-stack-arg-probe -ffunction-sections -fdata-sections -O2)

    add_executable(cmd_boot EXCLUDE_FROM_ALL
        tools/cmd_app.cpp tools/cmd/reactos/cmd.rc)
    target_include_directories(cmd_boot PRIVATE
        "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/tools"
        "${CMAKE_SOURCE_DIR}/tools/cmd/reactos")
    target_compile_definitions(cmd_boot PRIVATE
        MICANT_NATIVE_USERMODE MICANT_UXTHEME_IMPORTS
        LANGUAGE_IT_IT
        MICANT_PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MICANT_PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MICANT_PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    )
    target_compile_options(cmd_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe
        -ffunction-sections -fdata-sections -O2
    )
    target_link_options(cmd_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start -Wl,--gc-sections
        -Xlinker /STACK:1048576,131072
    )
    target_link_libraries(cmd_boot PRIVATE
        cmd_reactos_core user32_boot gdi32_boot win32k_boot ntdll_boot
        kernel32_boot advapi32_boot shell32_boot msvcrt_boot ucrtbase_boot)
    set_target_properties(cmd_boot PROPERTIES
        OUTPUT_NAME "cmd"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    add_executable(audio_probe_boot EXCLUDE_FROM_ALL tools/audio_probe_app.cpp)
    target_include_directories(audio_probe_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(audio_probe_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(audio_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(audio_probe_boot PRIVATE
        -nostdlib -Wl,--subsystem,console -Wl,-e,_start
    )
    target_link_libraries(audio_probe_boot PRIVATE ntdll_boot)
    set_target_properties(audio_probe_boot PROPERTIES
        OUTPUT_NAME "audio_probe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    add_executable(audio_capture_probe_boot EXCLUDE_FROM_ALL tools/audio_capture_probe_app.cpp)
    target_include_directories(audio_capture_probe_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(audio_capture_probe_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(audio_capture_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(audio_capture_probe_boot PRIVATE
        -nostdlib -Wl,--subsystem,console -Wl,-e,_start
    )
    target_link_libraries(audio_capture_probe_boot PRIVATE ntdll_boot)
    set_target_properties(audio_capture_probe_boot PROPERTIES
        OUTPUT_NAME "audio_capture_probe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    # Dedicated Ring 3 consumer for theme and common-control verification.
    # Keep these probes out of Explorer, which remains a normal shell client.
    add_executable(theme_test_boot EXCLUDE_FROM_ALL tools/theme_test_app.cpp)
    target_include_directories(theme_test_boot PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_options(theme_test_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_compile_definitions(theme_test_boot PRIVATE MICANT_UXTHEME_IMPORTS)
    target_link_options(theme_test_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
    )
    target_link_libraries(theme_test_boot PRIVATE
        kernel32_boot user32_boot gdi32_boot win32k_boot uxtheme_boot comctl32_boot ntdll_boot)
    set_target_properties(theme_test_boot PROPERTIES
        OUTPUT_NAME "theme_test"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    add_executable(textbox_probe_boot EXCLUDE_FROM_ALL tools/textbox_probe_app.cpp)
    target_include_directories(textbox_probe_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_options(textbox_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -mno-stack-arg-probe -O2)
    target_link_options(textbox_probe_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start)
    target_link_libraries(textbox_probe_boot PRIVATE
        kernel32_boot user32_boot gdi32_boot win32k_boot ntdll_boot)
    set_target_properties(textbox_probe_boot PROPERTIES
        OUTPUT_NAME "textbox_probe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

    # Native process memory report and dump utility. It reaches process and
    # virtual-memory services only through the existing NTDLL syscall ABI.
    add_executable(procdump_boot EXCLUDE_FROM_ALL tools/procdump_app.cpp)
    target_include_directories(procdump_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_options(procdump_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(procdump_boot PRIVATE
        -nostdlib -Wl,--subsystem,native -Wl,-e,_start
    )
    target_link_libraries(procdump_boot PRIVATE ntdll_boot)
    set_target_properties(procdump_boot PROPERTIES
        OUTPUT_NAME "procdump"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    add_executable(lucecrash_boot EXCLUDE_FROM_ALL tools/lucecrash_app.cpp)
    target_include_directories(lucecrash_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_options(lucecrash_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(lucecrash_boot PRIVATE
        -nostdlib -Wl,--subsystem,native -Wl,-e,_start
    )
    target_link_libraries(lucecrash_boot PRIVATE ntdll_boot)
    set_target_properties(lucecrash_boot PROPERTIES
        OUTPUT_NAME "lucecrash"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    add_executable(proctop_boot EXCLUDE_FROM_ALL tools/procdump_app.cpp)
    target_include_directories(proctop_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_options(proctop_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_compile_definitions(proctop_boot PRIVATE MICANT_PROCTOP_DEFAULT)
    target_link_options(proctop_boot PRIVATE
        -nostdlib -Wl,--subsystem,native -Wl,-e,_start
    )
    target_link_libraries(proctop_boot PRIVATE ntdll_boot)
    set_target_properties(proctop_boot PROPERTIES
        OUTPUT_NAME "proctop"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    if (MICANT_BOOTSTRAP_PROCESS_MEMORY_SELFTEST)
        target_compile_definitions(procdump_boot PRIVATE MICANT_PROCESS_MEMORY_SELFTEST)
        target_compile_definitions(procdump_boot PRIVATE MICANT_PROCESS_MEMORY_TEST_DESKTOP_TARGETS)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_PROCESS_MEMORY_SELFTEST)
        if (MICANT_BOOTSTRAP_THEME_TEST)
            target_compile_definitions(procdump_boot PRIVATE MICANT_PROCESS_MEMORY_TEST_THEME_TARGET)
        endif()
    endif()
    if (MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        target_compile_definitions(taskmgr_boot PRIVATE MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    endif()
    if (MICANT_BOOTSTRAP_THEME_TEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_THEME_TEST)
    endif()
    if (MICANT_BOOTSTRAP_TEXTBOX_TEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_TEXTBOX_TEST)
    endif()
    if (MICANT_BOOTSTRAP_CMD_TEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_CMD_TEST)
    endif()
    if (MICANT_BOOTSTRAP_REACTOS_APP)
        set(_micant_reactos_app_names winver notepad calc winmine sol mspaint regedit taskmgr wordpad cmd)
        if (NOT MICANT_BOOTSTRAP_REACTOS_APP IN_LIST _micant_reactos_app_names)
            message(FATAL_ERROR "Unsupported MICANT_BOOTSTRAP_REACTOS_APP: ${MICANT_BOOTSTRAP_REACTOS_APP}")
        endif()
        string(TOUPPER "${MICANT_BOOTSTRAP_REACTOS_APP}" _micant_reactos_app_upper)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE
            "MICANT_BOOTSTRAP_REACTOS_APP_${_micant_reactos_app_upper}")
        if (MICANT_BOOTSTRAP_REACTOS_APP STREQUAL "calc")
            target_compile_definitions(user32_boot PRIVATE
                MICANT_BOOTSTRAP_REACTOS_APP_CALC)
        endif()
    endif()
    if (MICANT_BOOTSTRAP_CRASH_DUMP_SELFTEST)
        target_compile_definitions(ntoskrnl_core_baremetal PRIVATE MICANT_BOOTSTRAP_CRASH_DUMP_SELFTEST)
    endif()
    add_executable(taskmgr_probe_boot EXCLUDE_FROM_ALL tools/taskmgr_probe_app.cpp)
    target_include_directories(taskmgr_probe_boot PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_options(taskmgr_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(taskmgr_probe_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
    )
    set_target_properties(taskmgr_probe_boot PROPERTIES
        OUTPUT_NAME "taskmgr_probe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    # The compositor is an independent, import-free Ring 3 image. Its shared
    # section views are installed by the kernel before either process starts.
    add_executable(compositor_boot EXCLUDE_FROM_ALL
        tools/compositor_app.cpp tools/liquid_glass_effect.cpp)
    target_compile_options(compositor_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_compile_definitions(compositor_boot PRIVATE MICANT_COMPOSITOR_DEBUG_HUD=$<BOOL:${MICANT_COMPOSITOR_DEBUG_HUD}>)
    target_compile_definitions(compositor_boot PRIVATE MICANT_NATIVE_USERMODE)
    if (MICANT_COMPOSITOR_LOCAL_STATS)
        target_compile_definitions(compositor_boot PRIVATE MICANT_COMPOSITOR_LOCAL_STATS=1)
    endif()
    target_link_options(compositor_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
    )
    set_target_properties(compositor_boot PROPERTIES
        OUTPUT_NAME "compositor"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    # Self-contained image for the file-backed SEC_IMAGE verification. It has
    # Native user image with explicit NTDLL imports and no DllMain.
    add_executable(file_image_boot EXCLUDE_FROM_ALL tools/file_image_app.cpp)
    target_compile_options(file_image_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2
    )
    target_link_options(file_image_boot PRIVATE
        -nostdlib -Wl,--subsystem,windows -Wl,-e,_start
        -Wl,--image-base,0x140000000 -Wl,--dynamicbase
    )
    set_target_properties(file_image_boot PROPERTIES
        OUTPUT_NAME "fileimage"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )
    add_dependencies(file_image_boot ntdll_process_image)
    # The Ps loader rejects a DLL with a DllMain entry point because DLL
    # initializers are not part of M4. Publish the entry-point-free NTDLL
    # build separately so the file-backed image path can be verified without
    # replacing the NTDLL used by the desktop path.
    add_custom_command(TARGET file_image_boot POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_SOURCE_DIR}/build/fileimage-system32"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/ntdll-process.dll"
            "${CMAKE_SOURCE_DIR}/build/fileimage-system32/ntdll.dll"
    )

    add_library(ntdll_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/ntdll.cpp
        tools/dlls/native_stack_probe.cpp
    )
    target_include_directories(ntdll_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_options(ntdll_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(ntdll_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,--entry= -Wl,--subsystem,native
        -Wl,--image-base,0x400020f00000 -Wl,--dynamicbase
    )
    set_target_properties(ntdll_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "ntdll"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )
    target_link_libraries(compositor_boot PRIVATE ntdll_boot)

    # User32 owns per-process icon pixels and file-backed icon loading through
    # the native file/memory system calls. Those operations remain in Ring 3.
    target_link_libraries(user32_boot PRIVATE ntdll_boot kernel32_boot gdi32_boot uxtheme_boot)
    target_compile_definitions(user32_boot PRIVATE MICANT_GDI32_IMPORTS MICANT_UXTHEME_IMPORTS)
    target_link_libraries(gdi32_boot PRIVATE ntdll_boot)

    add_library(ws2_32_boot SHARED EXCLUDE_FROM_ALL tools/dlls/ws2_32.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(ws2_32_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(ws2_32_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(ws2_32_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(ws2_32_boot PRIVATE
        -nostdlib -Wl,--entry,DllMain -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(ws2_32_boot PRIVATE ntdll_boot)
    set_target_properties(ws2_32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "ws2_32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    # The native process loader currently rejects DLL entrypoints because
    # DllMain/TLS attach dispatch is not implemented. This entrypoint-free
    # copy is for the bounded DHCP service host only.
    add_library(ws2_32_service_boot SHARED EXCLUDE_FROM_ALL tools/dlls/ws2_32.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(ws2_32_service_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(ws2_32_service_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(ws2_32_service_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(ws2_32_service_boot PRIVATE
        -nostdlib -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(ws2_32_service_boot PRIVATE ntdll_boot)
    set_target_properties(ws2_32_service_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "ws2_32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        ARCHIVE_OUTPUT_NAME "ws2_32_service"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_library(dnsapi_boot SHARED EXCLUDE_FROM_ALL tools/dlls/dnsapi.cpp
        tools/dlls/native_stack_probe.cpp)
    target_include_directories(dnsapi_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(dnsapi_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(dnsapi_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(dnsapi_boot PRIVATE
        -nostdlib -Wl,--entry,DllMain -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(dnsapi_boot PRIVATE ws2_32_boot ntdll_boot)
    set_target_properties(dnsapi_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "dnsapi"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    # Entry-point-free DNSAPI copy for the bounded native networking service.
    add_library(dnsapi_service_boot SHARED EXCLUDE_FROM_ALL tools/dlls/dnsapi.cpp
        tools/dlls/native_stack_probe.cpp)
    target_include_directories(dnsapi_service_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(dnsapi_service_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(dnsapi_service_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(dnsapi_service_boot PRIVATE
        -nostdlib -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(dnsapi_service_boot PRIVATE ws2_32_service_boot ntdll_boot)
    set_target_properties(dnsapi_service_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "dnsapi"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        ARCHIVE_OUTPUT_NAME "dnsapi_service"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_library(crypt32_boot SHARED EXCLUDE_FROM_ALL tools/dlls/crypt32.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(crypt32_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(crypt32_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(crypt32_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(crypt32_boot PRIVATE
        -nostdlib -Wl,--entry,DllMain -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(crypt32_boot PRIVATE ntdll_boot)
    set_target_properties(crypt32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "crypt32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_library(crypt32_service_boot SHARED EXCLUDE_FROM_ALL tools/dlls/crypt32.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(crypt32_service_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(crypt32_service_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(crypt32_service_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(crypt32_service_boot PRIVATE
        -nostdlib -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(crypt32_service_boot PRIVATE ntdll_boot)
    set_target_properties(crypt32_service_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "crypt32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        ARCHIVE_OUTPUT_NAME "crypt32_service"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_library(winhttp_boot SHARED EXCLUDE_FROM_ALL tools/dlls/winhttp.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(winhttp_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(winhttp_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(winhttp_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(winhttp_boot PRIVATE
        -nostdlib -Wl,--entry,DllMain -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(winhttp_boot PRIVATE ntdll_boot ws2_32_boot dnsapi_boot crypt32_boot)
    set_target_properties(winhttp_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "winhttp"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_library(winhttp_service_boot SHARED EXCLUDE_FROM_ALL tools/dlls/winhttp.cpp
        tools/dlls/native_stack_probe.cpp kernel/ex/freestanding_memory.cpp)
    target_include_directories(winhttp_service_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(winhttp_service_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(winhttp_service_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(winhttp_service_boot PRIVATE
        -nostdlib -Wl,--entry= -Wl,--subsystem,native -Wl,--dynamicbase
    )
    target_link_libraries(winhttp_service_boot PRIVATE ntdll_boot ws2_32_service_boot
        dnsapi_service_boot crypt32_service_boot)
    set_target_properties(winhttp_service_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "winhttp"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/network-system32"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/network-system32"
        ARCHIVE_OUTPUT_NAME "winhttp_service"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )

    add_executable(svchost_boot EXCLUDE_FROM_ALL tools/svchost_app.cpp tools/dlls/dhcpcsvc.cpp
        kernel/ex/freestanding_memory.cpp)
    target_include_directories(svchost_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(svchost_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(svchost_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(svchost_boot PRIVATE
        -nostdlib -Wl,--entry,_start -Wl,--subsystem,console -Wl,--dynamicbase
    )
    target_link_libraries(svchost_boot PRIVATE dnsapi_service_boot ws2_32_service_boot ntdll_boot)
    set_target_properties(svchost_boot PROPERTIES
        OUTPUT_NAME "svchost"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    add_executable(network_probe_boot EXCLUDE_FROM_ALL tools/network_probe_app.cpp)
    target_include_directories(network_probe_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(network_probe_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(network_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(network_probe_boot PRIVATE
        -nostdlib -Wl,--entry,_start -Wl,--subsystem,console -Wl,--dynamicbase
    )
    target_link_libraries(network_probe_boot PRIVATE ws2_32_service_boot ntdll_boot)
    set_target_properties(network_probe_boot PROPERTIES
        OUTPUT_NAME "network-probe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    add_executable(https_probe_boot EXCLUDE_FROM_ALL tools/https_probe_app.cpp
        tools/dlls/native_stack_probe.cpp)
    target_include_directories(https_probe_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(https_probe_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(https_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(https_probe_boot PRIVATE
        -nostdlib -Wl,--entry,_start -Wl,--subsystem,console -Wl,--dynamicbase
    )
    target_link_libraries(https_probe_boot PRIVATE winhttp_service_boot ntdll_boot)
    set_target_properties(https_probe_boot PROPERTIES
        OUTPUT_NAME "https-probe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    )

    add_library(kernel32_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/abi/kernel32_system32.cpp
        tools/dlls/native_stack_probe.cpp
        kernel/ex/freestanding_memory.cpp
    )
    target_compile_definitions(kernel32_boot PRIVATE
        MICANT_FREESTANDING_KERNEL32
        BUILD_SYSTEM32_KERNEL32_DLL
    )
    target_compile_options(kernel32_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-emulated-tls
        -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(kernel32_boot PRIVATE
        -nostdlib -Wl,--entry,DllMainCRTStartup
    )
    target_link_libraries(kernel32_boot PRIVATE ntdll_boot ucrtbase_boot)
    target_link_libraries(explorer_boot PRIVATE ntdll_boot)
    set_target_properties(kernel32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "kernel32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )
    add_library(advapi32_boot SHARED EXCLUDE_FROM_ALL
        tools/dlls/abi/advapi32_system32.cpp)
    target_include_directories(advapi32_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(advapi32_boot PRIVATE
        BUILD_SYSTEM32_STANDALONE_DLL BUILD_SYSTEM32_ADVAPI32_DLL MICANT_NATIVE_USERMODE)
    target_compile_options(advapi32_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-emulated-tls
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2)
    target_link_options(advapi32_boot PRIVATE -nostdlib -Wl,--entry,DllMainCRTStartup)
    target_link_libraries(advapi32_boot PRIVATE ntdll_boot)
    set_target_properties(advapi32_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "advapi32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports")
    add_library(ucrtbase_boot SHARED EXCLUDE_FROM_ALL
        kernel/ps/winnt_ucrtbase_runtime_abi.cpp
        kernel/ps/ucrt_native_heap_syscalls.cpp
    )
    target_compile_definitions(ucrtbase_boot PRIVATE MICANT_FREESTANDING_UCRT MICANT_NATIVE_USERMODE)
    target_compile_options(ucrtbase_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-emulated-tls
        -fno-stack-protector -mno-red-zone -O2
    )
    target_link_options(ucrtbase_boot PRIVATE
        -nostdlib -Wl,--entry,DllMainCRTStartup
    )
    target_link_libraries(ucrtbase_boot PRIVATE ntdll_boot)
    set_target_properties(ucrtbase_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "ucrtbase"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports"
    )
    add_library(msvcrt_boot SHARED EXCLUDE_FROM_ALL tools/dlls/msvcrt.cpp tools/dlls/msvcrt.def)
    target_include_directories(msvcrt_boot PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(msvcrt_boot PRIVATE MICANT_NATIVE_USERMODE)
    target_compile_options(msvcrt_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-emulated-tls
        -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -O2)
    target_link_options(msvcrt_boot PRIVATE -nostdlib -Wl,--entry,DllMainCRTStartup)
    target_link_libraries(msvcrt_boot PRIVATE ucrtbase_boot ntdll_boot kernel32_boot)
    set_target_properties(msvcrt_boot PROPERTIES
        PREFIX "" OUTPUT_NAME "msvcrt"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/system32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/boot-imports")
    add_executable(msvcrt_probe_boot EXCLUDE_FROM_ALL tools/msvcrt_probe.cpp)
    target_compile_options(msvcrt_probe_boot PRIVATE
        -ffreestanding -fno-builtin -fno-exceptions -fno-rtti
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector
        -mno-red-zone -mno-stack-arg-probe -O2)
    target_link_options(msvcrt_probe_boot PRIVATE
        -nostdlib -fuse-ld=lld -Wl,-e,MsvcrtProbeStartup -Wl,--subsystem,windows)
    target_link_libraries(msvcrt_probe_boot PRIVATE msvcrt_boot)
    set_target_properties(msvcrt_probe_boot PROPERTIES
        OUTPUT_NAME "msvcrt_probe" SUFFIX ".exe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
    add_custom_target(explorer_runtime_boot
        DEPENDS explorer_boot mac_shell_boot compositor_boot imm32_boot kernel32_boot advapi32_boot ntdll_boot ws2_32_boot dnsapi_boot crypt32_boot winhttp_boot svchost_boot ucrtbase_boot msvcrt_boot dwrite_boot uxtheme_boot luceglass_boot
    )

    add_executable(virtio_gpu_boot drivers/virtio_gpu/virtio_gpu.cpp)
    set_target_properties(virtio_gpu_boot PROPERTIES
        OUTPUT_NAME "virtio_gpu"
        SUFFIX ".sys"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build"
    )
    add_executable(ahci_oem_boot drivers/oem/ahci/ahci_file_provider.cpp
                                   kernel/ex/freestanding_memory.cpp)
    add_executable(hidusb_oem_boot drivers/oem/hidusb/hidusb_driver.cpp
                                   kernel/ex/freestanding_memory.cpp)
    add_executable(usbstor_oem_boot drivers/oem/usbstor/usbstor_driver.cpp
                                    kernel/ex/freestanding_memory.cpp)
    add_executable(xhci_oem_boot drivers/oem/xhci/xhci_driver.cpp
                                   kernel/ex/freestanding_memory.cpp)
    add_executable(ehci_oem_boot drivers/oem/ehci/ehci_driver.cpp
                                   kernel/ex/freestanding_memory.cpp)
    add_executable(i210_oem_boot drivers/oem/i210/i210_driver.cpp
                                   kernel/ex/freestanding_memory.cpp)
    add_executable(virtio_net_oem_boot drivers/oem/virtio_net/virtio_net_driver.cpp
                                       kernel/ex/freestanding_memory.cpp)
    set_target_properties(ahci_oem_boot PROPERTIES OUTPUT_NAME "storahci")
    set_target_properties(hidusb_oem_boot PROPERTIES OUTPUT_NAME "hidusb")
    set_target_properties(usbstor_oem_boot PROPERTIES OUTPUT_NAME "usbstor")
    set_target_properties(xhci_oem_boot PROPERTIES OUTPUT_NAME "usbxhci")
    set_target_properties(ehci_oem_boot PROPERTIES OUTPUT_NAME "ehci")
    set_target_properties(i210_oem_boot PROPERTIES OUTPUT_NAME "i210")
    set_target_properties(virtio_net_oem_boot PROPERTIES OUTPUT_NAME "virtio_net")
    foreach(_oem_driver_target IN ITEMS ahci_oem_boot hidusb_oem_boot usbstor_oem_boot xhci_oem_boot ehci_oem_boot i210_oem_boot
                                       virtio_net_oem_boot)
        set_target_properties(${_oem_driver_target} PROPERTIES
            SUFFIX ".sys"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build"
        )
    endforeach()
endif()

if(WIN32 OR MICANT_ENABLE_BAREMETAL)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(micant_user_mode_boundary_check)
    foreach(_user_mode_variant IN ITEMS
        micant_win32k micant_user32 micant_gdi32 micant_imm32
        kernel32_boot ntdll_boot ws2_32_boot dnsapi_boot svchost_boot ucrtbase_boot win32k_boot user32_boot gdi32_boot shell32_boot comctl32_boot dwrite_boot uxtheme_boot luceglass_boot
        imm32_boot explorer_boot mac_shell_boot explorer_runtime_boot
    )
        add_custom_target(micant_boundary_${_user_mode_variant}
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/check_user_mode_boundary.py"
                --root "${CMAKE_SOURCE_DIR}"
                --compiler "${CMAKE_CXX_COMPILER}"
                --target "${_user_mode_variant}"
            VERBATIM
        )
        add_dependencies(micant_user_mode_boundary_check
                         micant_boundary_${_user_mode_variant})
        if(TARGET ${_user_mode_variant})
            add_dependencies(${_user_mode_variant}
                             micant_boundary_${_user_mode_variant})
        endif()
    endforeach()
    if(TARGET explorer_runtime_boot)
        add_dependencies(explorer_runtime_boot micant_boundary_explorer_runtime_boot)
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ntoskrnl_core PRIVATE -Wall -Wextra -Wpedantic -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-unused-parameter -Wno-missing-designated-field-initializers -O2)
    target_compile_options(micant_kernel PRIVATE -Wall -Wextra -Wpedantic -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-unused-parameter -Wno-missing-designated-field-initializers -O2)
    target_compile_options(micant_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-unused-parameter -Wno-missing-designated-field-initializers -O2)
    target_compile_options(userland_app PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-designated-field-initializers -O2)
    target_compile_options(win32_app PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-designated-field-initializers -O2)
    target_compile_options(bootx64 PRIVATE -Wall -Wextra -Wpedantic -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-unused-parameter -Wno-missing-designated-field-initializers -O2)
    if (MICANT_ENABLE_BAREMETAL)
        foreach(_target IN ITEMS ntoskrnl_core_baremetal bootx64_efi virtio_gpu_boot
                                  ahci_oem_boot hidusb_oem_boot usbstor_oem_boot xhci_oem_boot ehci_oem_boot i210_oem_boot
                                  virtio_net_oem_boot)
            target_compile_options(${_target} PRIVATE
                -ffreestanding
                -fno-exceptions
                -fno-rtti
                -fno-threadsafe-statics
                -fno-stack-protector
                -mno-red-zone
                -ffunction-sections
                -fdata-sections
                -O2
            )
        endforeach()
        target_link_options(ntoskrnl_core_baremetal PRIVATE
            -nostdlib
            -Wl,--subsystem,native
            -Wl,-e,KiSystemStartup
            -Wl,--gc-sections
        )
        target_link_options(bootx64_efi PRIVATE
            -nostdlib
            -Wl,--subsystem,efi_application
            -Wl,-e,EfiMain
        )
        target_compile_options(virtio_gpu_boot PRIVATE -fno-builtin -mno-stack-arg-probe)
        target_link_options(virtio_gpu_boot PRIVATE
            -nostdlib -Wl,--subsystem,native -Wl,-e,DriverEntry
        )
        foreach(_oem_driver_target IN ITEMS ahci_oem_boot hidusb_oem_boot usbstor_oem_boot xhci_oem_boot ehci_oem_boot i210_oem_boot
                                            virtio_net_oem_boot)
            target_compile_options(${_oem_driver_target} PRIVATE -fno-builtin)
            target_link_options(${_oem_driver_target} PRIVATE
                -nostdlib -Wl,--subsystem,native -Wl,-e,DriverEntry
            )
        endforeach()
    endif()
elseif (MSVC)
    target_compile_options(ntoskrnl_core PRIVATE /W4 /permissive- /O2)
    target_compile_options(micant_kernel PRIVATE /W4 /permissive- /O2)
    target_compile_options(micant_tests PRIVATE /W4 /permissive- /O2)
    target_compile_options(userland_app PRIVATE /W4 /permissive- /O2)
    target_compile_options(win32_app PRIVATE /W4 /permissive- /O2)
    target_compile_options(bootx64 PRIVATE /W4 /permissive- /O2)
endif()

set_target_properties(micant_kernel micant_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host"
)
foreach(_config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_config}" _config_upper)
    set_target_properties(micant_kernel micant_tests PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${_config_upper}" "${CMAKE_BINARY_DIR}/host"
    )
endforeach()
set_target_properties(userland_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_target_properties(win32_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_target_properties(prismx_3d_cube_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if (MICANT_ENABLE_BAREMETAL)
    set_target_properties(ntoskrnl_core_baremetal PROPERTIES
        OUTPUT_NAME "${MICANT_BAREMETAL_KERNEL_NAME}"
        SUFFIX ".exe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/baremetal"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/baremetal"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/build/baremetal"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/build/baremetal"
    )
    set_target_properties(bootx64_efi PROPERTIES
        OUTPUT_NAME "BOOTX64"
        SUFFIX ".EFI"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/esp/EFI/BOOT"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/esp/EFI/BOOT"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/build/esp/EFI/BOOT"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/build/esp/EFI/BOOT"
    )
endif()
