# Windows Windowing and Composition Interface Evidence

**Profile under implementation:** AMD64 desktop, initially Windows 10 19041
public behavior. Current Microsoft Learn pages were consulted on 2026-11-20;
the exact Windows SDK / win32metadata package revision is not pinned yet.
Private uDWM/dwmcore interfaces are not part of this public contract ledger.

Confidence terms: `VERIFIED_PUBLIC` means the behavior/signature is stated in
Microsoft documentation; `VERIFIED_LOCAL` means observed in the named local
source or built PE; `INFERRED` means a design conclusion that still needs
runtime proof; `UNKNOWN` means evidence has not been collected.

## DWMAPI client exports

| Export | Public contract evidence | Current MicaNT implementation | Confidence / gap |
|---|---|---|---|
| `DwmIsCompositionEnabled(BOOL*)` | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmiscompositionenabled): `HRESULT`; output is written only on success; composition is always enabled in Windows 8+ subject to compatibility-manifest behavior. | Null output -> `E_INVALIDARG`; otherwise waits up to one second for a real Win32k-acknowledged compositor frame, writes TRUE only on success, and propagates failure without changing output. The desktop compatibility-manifest distinction is not implemented. | Contract `VERIFIED_PUBLIC`; implementation and `dwmapi_boot` compile `VERIFIED_LOCAL`; runtime getter and Windows manifest compatibility `UNKNOWN`. |
| `DwmExtendFrameIntoClientArea(HWND, MARGINS*)` | [DWM functions](https://learn.microsoft.com/en-us/windows/win32/dwm/functions): public API extends the frame into client area. The margins are an input structure. | Null required argument -> `E_INVALIDARG`; valid args -> `E_NOTIMPL`. No per-window propagation. | Behavior description `VERIFIED_PUBLIC`; margin validation/effect `UNKNOWN`. |
| `DwmEnableBlurBehindWindow(HWND, DWM_BLURBEHIND*)` | [DWM functions](https://learn.microsoft.com/en-us/windows/win32/dwm/functions): enables blur behind the specified window. | Null required argument -> `E_INVALIDARG`; valid args -> `E_NOTIMPL`; no blur is claimed. | Behavior description `VERIFIED_PUBLIC`; flag/region validation and effect `UNKNOWN`. |
| `DwmFlush()` | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush): blocks until a subsequent Present has applied outstanding DirectX surface updates. | Ring 3 DLL submits a versioned flush-latest request. Kernel snapshots an even stable publication while attached to the compositor address space, then waits for that frame's actual acknowledgement with a bounded timeout. | Contract `VERIFIED_PUBLIC`; QEMU Ring 3 result `S_OK` after Explorer's acknowledged frame in `build/dwm-qemu-evidence-20261120-lease-fix/serial.log`. Arbitrary DirectX surface updates are not implemented. |
| `DwmGetWindowAttribute(HWND, DWORD, PVOID, DWORD)` | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmgetwindowattribute) and [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute): output type/direction and byte size depend on the enum. | Getter allowlist validates attribute direction and exact `BOOL`/`UINT`/enum/`RECT` byte count before returning `E_NOTIMPL`. | Current docs type matrix `VERIFIED_PUBLIC`; exact 19041 SDK revision and Windows version behavior `UNKNOWN`. |
| `DwmSetWindowAttribute(HWND, DWORD, LPCVOID, DWORD)` | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute) and [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute): input direction/type/size depends on the enum; `DWMWA_NCRENDERING_ENABLED` is getter-only. `DWMWA_TRANSITIONS_FORCEDISABLED` is setter-only and takes `BOOL`. | Exact-size validation runs in Ring 3. `DWMWA_TRANSITIONS_FORCEDISABLED` is sent in a versioned packet to Win32k and stored in the target canonical window record; only the owning process may set it, and compositor callers are denied. Enabling it suppresses its minimize/restore animation. Other valid setters return `E_NOTIMPL`. | Contract `VERIFIED_PUBLIC`; implementation and `dwmapi_boot` build `VERIFIED_LOCAL`; runtime attribute effect not yet exercised in QEMU. |
| `DwmGetCompositionTimingInfo(HWND, DWM_TIMING_INFO*)` | [DWM functions](https://learn.microsoft.com/en-us/windows/win32/dwm/functions): retrieves composition timing for a window. | Null output -> `E_INVALIDARG`; otherwise `E_NOTIMPL`; no synthetic counters are returned. | Behavior description `VERIFIED_PUBLIC`; structure-version validation and timing source `UNKNOWN`. |

Current `dwmapi.dll` PE export table was inspected with LLVM `llvm-readobj
--coff-exports` from `build/system32/dwmapi.dll`. It is AMD64 PE32+ and exports
exactly the seven names above, with linker-assigned ordinals 1–7. The public
documentation identifies these functions by name; the observed ordinal order
is a local build fact, not a Windows ordinal compatibility claim.

## DirectComposition declarations

| Surface | Public evidence | Current MicaNT state | Confidence / gap |
|---|---|---|---|
| `DCompositionCreateDevice` | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice): takes `IDXGIDevice*`, requested IID and required output pointer. | Factory clears valid output pointer and returns `E_NOTIMPL`; it does not instantiate a mock device. | Public signature and corrected `IDCompositionDevice` IID `VERIFIED_PUBLIC`; complete vtable parity `UNKNOWN`. |
| `DCompositionCreateDevice2/3` | [Device2](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice2), [Device3](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice3): optional rendering device, requested IID, required output pointer. | Factories clear output and return `E_NOTIMPL`. | Public signatures `VERIFIED_PUBLIC`; interface identity/vtables need SDK comparison. |
| `DCompositionCreateSurfaceHandle` | [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatesurfacehandle): required output handle; desired access uses only `READ=1`, `WRITE=2`, `ALL_ACCESS=3`, or zero; optional security attributes control descriptor/inheritance. | Null output -> `E_POINTER`; unknown access bits -> `E_INVALIDARG`; documented masks return `E_NOTIMPL`; no synthetic handle is generated. | Access values/signature `VERIFIED_PUBLIC`; status-validation slice is local; security attributes and handle object are not implemented. |
| `IDComposition*` interfaces | [DComp header index](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/): documented public COM interfaces and methods. | Header contains broad interface declarations; factory creation is unavailable. | Full GUID, inheritance, method order and structure parity with the pinned SDK are `UNKNOWN`; private DWM interfaces remain separate. |

## Local request/ownership observations

| Local item | Evidence | Status |
|---|---|---|
| Versioned composition packets | `include/micant/shared/composition_protocol.h`; C and C++ packet-size assertions plus validators. | `VERIFIED_LOCAL`; acquisition/release use generation-checked lease handles and no user-visible addresses. `DWMWA_TRANSITIONS_FORCEDISABLED` has an owner-checked per-window request. Attach/commit/render remain unsupported. |
| Win32k client bridge | `kernel/ps/winnt_win32k_server.cpp`, export `MicaWin32kCompositionRequest`; copies user packet through `NtReadVirtualMemory`, validates fixed packet and sends only inline bytes over service 0x86. | `VERIFIED_LOCAL`; full `win32k_boot` build passed. |
| Kernel composition dispatcher | `kernel/win32k/freestanding.cpp`, `MICANT_WIN32K_OP_COMPOSITION_REQUEST`. | Acquisition and release are backed by a bounded generation-checked lease table; window transitions-disabled is owner-checked. Unsupported attach/commit/render requests still return `STATUS_NOT_SUPPORTED`. |
| Surface acquisition | `ACQUIRE_WINDOW_SURFACE` pins the canonical root-window backing storage; `RELEASE_WINDOW_SURFACE` drains the lease. Resize/destroy/process exit invalidate new uses while retained storage remains until release; release callback failures preserve the window record for retry. | Hosted lifecycle regression source covers resize, destroy, stale/double release, and process exit. Bare-metal compile/link passed; the lifecycle regression has not run in QEMU. |
| Runtime proof | `build/dwm-qemu-evidence-20261120-lease-fix/app-window.ppm` shows themed My Computer with a working Refresh child. Its serial records compositor-held surface leases, `DwmFlush` returning `S_OK` after frame acknowledgement, and the `BN_CLICKED` refresh. `build/dwm-qemu-evidence-20261120-theme-test-final` passed theme-app startup and scanout. | Surface lease, flush, button and theme startup/scanout are verified. The drag probe reported 192 changed pixels (below its 10,000 threshold), although shifted and returned client areas remained visible. The default QEMU runner still fails waiting for a legacy Explorer control-probe matrix not initialized in this path. DComp attach/commit/render, full DWMAPI coverage and multi-window scene composition remain incomplete. |

## Private reference notes (not compatibility contracts)

The public [kawapure/DWM-Documentation repository](https://github.com/kawapure/DWM-Documentation)
states that its pseudo-C++ notes analyze `uDWM.dll` 10.0.19041.2075 and
`dwmcore.dll` 10.0.19041.2311 and warns that layouts may change across Windows
releases. The exact repository commit SHA was not recoverable during this
pass, so the notes below are tied to the repository's current `main` view and
are not pinned binary evidence.

| Reference note | Source | Confidence / use |
|---|---|---|
| `CCompositor` proxies uDWM information to the MIL server; its initializer calls `MilConnection_CreateChannel` and `DCompositionCreateDevice3`. The referenced DComp IID is called undocumented, and the author says `IDirectCompositionDevice` may be an incorrect type. | [udwm/compositor.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/compositor.h) | `INFERRED_PRIVATE`; supports keeping public DComp separate from a future internal channel. No private IID or layout is implemented from this note. |
| `CVisual` is reported as 240 bytes in the analyzed x64 binary and has dirty propagation, content/parent, transform, size, opacity and rendering methods. | [udwm/visual.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/visual.h) | `INFERRED_PRIVATE`; architectural vocabulary only. Not a stable ABI or class-size target. |
| `VisualCollection` is described as inlined inside `CVisual`; the note says the class does not exist outside that owner. | [udwm/visualcollection.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/visualcollection.h) | `INFERRED_PRIVATE`; do not force a corresponding binary class into MicaNT. |
| `CResource` owns a channel pointer; `CResourceProxy` models a resource proxy over a channel; `CBaseObject` describes reference counting. | [udwm/resource.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/resource.h), [udwm/resourceproxy.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/resourceproxy.h), [udwm/baseobject.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/baseobject.h) | `INFERRED_PRIVATE`; not copied and not treated as an exact object model. |
| `IDwmChannel` is explicitly unfinished in the supplied specification. Its full header could not be retrieved in this source pass; parameter/vtable details remain unknown. | [shared/idwmchannel.h](https://github.com/kawapure/DWM-Documentation/blob/main/shared/idwmchannel.h) | `UNKNOWN`; do not implement or expose this private interface from the current notes. |
| `DynArray` documentation was not available through the source retrieval path in this pass. | [udwm/dynarray.h](https://github.com/kawapure/DWM-Documentation/blob/main/udwm/dynarray.h) | `UNKNOWN`; no private allocation layout is used. |

## Source boundaries

* Public contracts above use Microsoft Learn. They do not establish private
  `uDWM.dll`, `dwmcore.dll`, channel, or object-layout behavior.
* The initial compatibility profile remains Windows 10 19041-family, while
  several documented attributes are Windows 11 additions. Those must not be
  silently treated as 19041 support.
* Before implementing per-window attribute state, pin a Windows SDK or
  `microsoft/win32metadata` revision and record for each enum: availability,
  input/output direction, exact type/size, default, invalid values and
  destroyed-HWND behavior. The current DLL allowlist uses Microsoft Learn's
  current table and deliberately does not implement attribute state.
* No Windows reference binary or runtime was used for this ledger. No matching
  export ordinals or private ABI layouts are asserted.
