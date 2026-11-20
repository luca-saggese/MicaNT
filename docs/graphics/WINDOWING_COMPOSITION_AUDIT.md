# Windowing and Composition Audit — DWM branch

**Snapshot:** local `DWM` branch at base commit
`89138cfee1008dd38f7f0352cfc058ff2121cc7c`, 2026-11-20. The worktree already contained
local modifications when this audit began; see the branch's initial status.
This is a source and build-graph inventory, not a claim of full runtime
acceptance. `build/qemu-cmake-mingw` was already incomplete (`rules.ninja`
missing); its failed auto-configuration remains in place. A fresh
`build/dwm-cmake-mingw` was configured and targets compiled as noted below.
The retained QEMU run at
`/private/tmp/micant-dwm-mycomputer-click-20261120` reached the desktop,
published and consumed the GTK theme, and captured the themed My Computer
window after a single click. The overall QEMU script then failed a later
production USER32 BUTTON acceptance check. An earlier run exposed and helped
locate a surface-bounds validator regression; the current run no longer logged
invalid surface metadata. No PE export dump or reference-Windows observation
has been performed.

## Scope and current outcome

The active design has several independent implementations with overlapping
names. `kernel/win32k/freestanding.cpp` is the canonical freestanding window
and compositor-metadata producer. `include/micant/um/subsystems/winuser/dwmapi.hpp`
contains a separate in-process `DwmCoordinator`, while
`tools/dlls/abi/ole32_dwmapi_winmm_impl.inc` supplies the standalone System32
`dwmapi.dll` exports. The boot path launches `compositor.exe` as a real Ps
image process and waits for its frame acknowledgement before Explorer startup
(`kernel/init/kiinit.cpp`: `launchCompositorProcess`,
`waitForCompositorPresentation`). `tools/compositor_app.cpp` contains the
separate composition/present loop. The public DWMAPI path is not connected to
that service, while the inline coordinator remains a separate state owner.

The first bounded migration slice is the System32 DWM ABI truthfulness
boundary: valid calls without a connected service query or frame fence now
fail explicitly rather than claiming compositor state/effects. This does not
implement composition or public DWM behavior generally.

## Active path and ownership inventory

| Path / symbols | Build or caller evidence | Current responsibility and finding | Intended owner / next step |
|---|---|---|---|
| `kernel/win32k/freestanding.cpp`: `dispatchRequest`, `publishCompositorSurfaces`, `waitForCompositorUpdate`, `releaseProcess` | Canonical kernel source; `CMakeLists.txt` includes it in kernel targets | Owns Win32k request state, process rundown and publishing of window metadata. It is kernel-side authority, not a user-mode compositor. | Retain HWND/window and access authority here; later expose validated, versioned scene events and surface handles. |
| `include/micant/shared/win32k_client.h` | Included by USER32-facing and kernel Win32k code | Mode-neutral Win32k request ABI. Callers marshal window operations into the kernel service. | Keep as an explicit syscall contract; audit all request sizes and user-pointer validation when adding scene operations. |
| `include/micant/shared/compositor_surface.h`: `MicaCompositorMetadataStore` v15 | Included by Win32k freestanding interfaces and implementation | Shared metadata contains pointer-shaped `uint64_t` addresses, process/window identifiers, mutable writer/frame fields, and arrays. This snapshot alone does not establish mapping validation or ownership. | Replace incrementally with generation-checked surface handles and an explicit producer/consumer protocol; preserve old consumer behind an adapter until parity. |
| `tools/dlls/user32.cpp` | Compiled into native user-mode `user32_boot`; CMake links `gdi32_boot`, `uxtheme_boot`, `ntdll_boot`, `kernel32_boot` | USER32 client implementation uses native NT and Win32k client interfaces. It also contains built-in control procedures/state. The canonical kernel remains HWND authority, but controls have local per-process client state. | Keep HWND ownership in Win32k; retain control procedure state in USER32 and connect invalidation/paint to owned surfaces. |
| `tools/dlls/gdi32.cpp` | `gdi32_boot` target; USER32 imports it | Ring 3 drawing APIs and resources. It must render into process-owned drawing targets; this audit has not traced all DC selection and pixel paths. | GDI32/graphics engine owns raster and text operations; map DC-to-surface lifetime and damage path before migration. |
| `tools/dlls/comctl32.cpp` | `comctl32_boot` links GDI32, USER32, UXTheme, NTDLL and `win32k_boot` | Common-control DLL. Built-in `BUTTON`/`EDIT` ownership must remain distinct from COMCTL32. | Keep common controls in COMCTL32; trace user32 built-in class dispatch and notifications independently. |
| `tools/dlls/uxtheme.cpp` | `uxtheme_boot` links Win32k, GDI32, Kernel32, NTDLL and UCRT | Theme query/drawing helper layer. The link graph indicates USER32/window-state coupling; full call graph not yet inspected. | UXTheme supplies theme parts/metrics; it must not own HWND state or composition. |
| `tools/dlls/abi/dwmapi_system32.cpp` | New freestanding `dwmapi_boot` target emits `build/system32/dwmapi.dll`; `stage_micant.py` maps/stages it | Own translation unit exports seven DWM functions. Valid calls return `E_NOTIMPL`; invalid required pointers return `E_INVALIDARG`. No service forwarding is present. | DWMAPI remains a client ABI. Add validated forwarding only after a compositor request is actually implemented and state lives in the window authority. |
| `include/micant/um/subsystems/winuser/dwmapi.hpp`: `DwmCoordinator`, inline C exports | Included from `shell.hpp`; shell command paths call DWM functions | A second in-process coordinator owns global window-property, thumbnail and frame state and exposes many inline APIs. It is not connected to the System32 DLL server contract. | Retire only after usage/call-graph proof; client calls should marshal to a single validated owner. |
| `include/micant/um/subsystems/graphics/dcomp.hpp` | Header declarations; focused syntax compile reported by implementation agent | Factory exports now null valid output pointers and return `E_NOTIMPL`, instead of fabricating COM devices or monotonically generated handles. | DComp creation/transaction APIs remain unimplemented; verify SDK identities/vtables and service before adding behavior. |
| `kernel/ps/winnt_win32k_server.cpp` | `win32k_boot`; `MicaWin32kCompositionRequest` export; native service syscall 0x86 | Bridge safely copies caller bytes with `NtReadVirtualMemory`, validates exact packet variants, then submits inline request bytes and returns the dispatcher NTSTATUS. This avoids passing raw caller pointers through the syscall. | Kernel dispatcher currently validates the request but returns `STATUS_NOT_SUPPORTED` for valid operations; bridge is transport/validation only, not operational composition. |
| `tools/explorer_shell/*`, `tools/explorer_shell_app.cpp` | Explorer boot target; `kiinit` launches compositor and waits for ack before Explorer | Explorer shell creates desktop/taskbar UI. It is a distinct process from the compositor; one QEMU boot reported initial scanout with Explorer visible. | Keep shell/client role; two independent application windows, input routing and DWM API IPC remain unproven. |
| `kernel/ps/ring3_desktop_shell.cpp`, the removed `ring3_winlogon_explorer_dwm.hpp`, and Suite 285 | Previously called by `ke::SyscallDispatcher::initializeStandardTable` and the legacy host runner | The path printed hard-coded Winlogon/Explorer/DWM PIDs, working set, GPU planes, latency and cursor rates. It was unrelated to the real compositor launch later in `kiinit.cpp`. | Removed the initialization call, fabricated report types and Suite 285 acceptance fixture. The source TU remains empty with an explanatory comment; no real Winlogon/session manager is claimed. |
| `drivers/virtio_gpu/virtio_gpu.cpp` and boot display path | `virtio_gpu_boot`; architecture docs describe bootstrap scanout | Display driver owns device/scanout and bounded present. This is not evidence of multi-window scene composition or hardware GPU blending. | Retain display ownership; future software compositor can submit actual composed frames through this driver. |
| `tools/compositor_app.cpp`, `compositor_boot` | `kiinit.cpp` launches the staged image; CMake target emits `build/compositor.exe` | Separate Ring 3 compositor and first-frame acknowledgement path are present. Safe multi-process surface lifetime, restart, and public DWMAPI linkage remain open. | Keep this as the compositor owner; harden metadata reads and map shutdown/restart/fence behavior. |

## First call-path sketches

### Window and paint path

`USER32 export / built-in procedure` → mode-neutral `win32k_client` request →
kernel `win32k::freestanding::dispatchRequest` → Win32k-owned window/message
state → current drawing/surface producers → `MicaCompositorMetadataStore` →
kernel compositor/display consumer → VirtIO-GPU scanout.

The producer/consumer arrows after window state need symbol-level confirmation;
the presence of metadata and a scanout is not proof of safe mappings,
per-process surfaces, or DWM composition.

### DWMAPI path

Native PE import/export → `tools/dlls/abi/dwmapi_system32.cpp` → shared ABI
implementation fragment → exported `Dwm*` function. Separately, shell code can
include the inline `DwmCoordinator` in `dwmapi.hpp`. No IPC connection between
these two paths or a real DWM service is currently established in this audit.

### Explorer and display

`kiinit` creates compositor sections → launches `compositor.exe` through the
Ps image path → waits for compositor frame acknowledgement → launches Explorer
→ waits for a visible Explorer surface → compositor presents through the
display backend. This is a real separate Ring 3 process path, but it does not
establish two independent app windows or validated public DWMAPI/DComp IPC.

## Evidence ledger

| Source | Revision / profile | Classification | Confidence | Result / open work |
|---|---|---|---|---|
| Local files listed above | Base `89138cfee1008dd38f7f0352cfc058ff2121cc7c`; dirty worktree on `DWM` | Repository observation | VERIFIED_LOCAL_SOURCE | Source/build inclusion inspected selectively; comprehensive call graph and export inventory remain open. |
| `docs/KERNEL_ARCHITECTURE.md` and `docs/KERNEL_INTEGRATION_STATUS.md` | Current checkout | Project architecture/evidence records | VERIFIED_LOCAL_DOCUMENT | Establish canonical modular kernel, bootstrap display, and incomplete Ring 3 desktop process integration; verify against current runtime before stronger claims. |
| [Microsoft Learn DWMAPI](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/) | Current documentation viewed 2026-11-20; proposed Windows 10 19041-family profile not binary-tested | Public contract | VERIFIED_PUBLIC | `DwmFlush` waits until outstanding surface updates have been made at a later Present; `DwmIsCompositionEnabled` provides an output value on success; `DwmSetWindowAttribute` value type/size depends on the enum. Current exports do not implement these effects and report `E_NOTIMPL`. |
| `kawapure/DWM-Documentation` | Proposed private reference family: uDWM 10.0.19041.2075 and dwmcore 10.0.19041.2311, as stated by the specification; commit SHA not yet acquired | Reconstructed private hypothesis | INFERRED / UNKNOWN | Headers are explicitly incomplete; acquire exact repository SHA and inspect required files before using any private layout. |
| WPF `WpfGfx` | Historical architectural reference only; revision not pinned | Public source, historical | UNKNOWN | May inform independent architecture; not evidence of current DWM implementation. |
| ReactOS / Wine | Local sibling ReactOS not yet inspected for this feature; Wine not consulted | Comparative implementation | UNKNOWN | Use only for test cases/comparative behavior, not sole Windows contract authority; licensing review before code reuse. |
| Reference Windows binaries | No binary or runtime evidence collected | Binary behavior | UNKNOWN | No private export/layout equivalence claim is made. |

## Prioritized gaps and next slices

1. **DWMAPI operation path:** dispatcher supports packet validation only;
   implement canonical per-window operation state, safe handles and frame
   acknowledgement before wiring public exports. Do not return success early.
2. **Window authority:** the real compositor process still reads the legacy
   metadata contract; connect future scene updates to validated owner handles.
3. **Compositor metadata validation:** validate headers/counts before
   dereferencing shared pointers and map rundown safety. Initial header/count
   checks have been added to `copyStableMetadata`, pending integration proof.
4. **Surface trust and lifetime:** map every metadata writer/reader, address
   mapping, active-writer protocol, resize, process rundown and PFN release.
5. **USER32/GDI/control paint:** map built-in BUTTON messages through
   BeginPaint/EndPaint, pixel writes, damage and present; keep COMCTL32 separate.
6. **DComp:** verify SDK identities/vtables before implementing a selected
   public transaction slice.

The initial slices intentionally do not introduce a second compositor or
window table. The mode-neutral packet contract has a C/C++ layout validator,
kernel dispatcher validation, and a Win32k DLL bridge; valid requests still
return `STATUS_NOT_SUPPORTED`, so there is no operational composition IPC.
Fabricated Horizon shell metrics and Suite 285 were removed; interactive
Winlogon is not implemented. The protocol fixture and C11 header check passed;
`dwmapi_boot`, `win32k_boot`, `gdi32_boot`, `compositor_boot`,
`ntoskrnl_core_baremetal`, and `micant_tests` compiled in the fresh build.
`micant_legacy_tests` did not compile due unrelated errors in
`winnt_ucrtbase_runtime_abi.cpp`. The first QEMU attempt failed during default
build configuration because the environment injected `-arch arm64` into the
x86_64 compiler. The initial QEMU rerun failed because the new range validator
treated a normal full-window rectangle as the client backing surface; a
subsequent retained serial run identified an active window with no backing
store yet, which the existing renderer intentionally skips. Validation now
checks the renderer's client source bounds and accepts a fully absent backing
store only when pointer, size and pitch are all zero. The latest full QEMU run
captured `app-window.ppm` with the themed My Computer window opened by one mouse
click; it later failed the production USER32 BUTTON acceptance check. The
specific visual request is therefore verified, but the full integration suite
is not passing and the broader window/control/composition requirements remain
incomplete.
