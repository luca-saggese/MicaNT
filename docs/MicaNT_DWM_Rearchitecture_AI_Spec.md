# MicaNT — Windows-faithful Desktop/Window/Composition Re-architecture

**Implementation directive for an autonomous coding agent**  
**Status:** Proposed engineering specification; evidence-based migration, not a claim of Windows source equivalence.  
**Project:** https://github.com/luca-saggese/MicaNT  
**Principal reverse-engineering reference:** https://github.com/kawapure/DWM-Documentation  
**Target:** A real, independently implemented, NT/Win32-compatible windowing and composition architecture, preserving MicaNT's canonical modular kernel.

> **CORE DIRECTIVE:** Refactor the *existing* MicaNT implementation in small verified increments. Do not add an alternative compositor or executive, move functionality to the correct owner, or claim Windows compatibility based on matching names, exports, stub returns, screenshots, or synthetic reports. Public Windows ABIs must match documented contracts exactly; undocumented private structures may be reproduced only for a clearly pinned binary build and where sufficient evidence exists. Treat incomplete reconstructions as hypotheses, not facts. The goal is behavioral and architectural fidelity, including correct process isolation and end-to-end execution.

## 0. Mandatory source hierarchy and evidence policy

Read first:

1. `MicaNT/AGENTS.md`, `docs/KERNEL_ARCHITECTURE.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, `docs/CLEAN_ROOM.md`, `CMakeLists.txt`, integration tests and the build manifest policy.
2. All of `kawapure/DWM-Documentation`: `README.md`, `shared/idwmchannel.h`, `udwm/compositor.h`, `udwm/visual.h`, `udwm/visualcollection.h`, `udwm/resource.h`, `udwm/resourceproxy.h`, `udwm/baseobject.h`, `udwm/dynarray.h`.
3. Microsoft Learn Windows desktop APIs and SDK/WDK headers: USER32, Win32k, DWM, DirectComposition, GDI, UXTheme, COMCTL32, DXGI/D3D, windows/desktop sessions; use Microsoft `win32metadata` for public signatures and SDK headers for authoritative details.
4. WPF MIL open-source source as a **historical architectural reference**, not current dwmcore implementation: `https://github.com/dotnet/wpf/tree/main/src/Microsoft.DotNet.Wpf/src/WpfGfx`.
5. Wine/ReactOS for comparative interface behavior and test cases; observe applicable license obligations and avoid copying LGPL source into MIT implementation without explicit compatible licensing review.
6. Windows-targeted behavior and export tests on *legitimately obtained* reference OS binaries where feasible. Record precise Windows version/build, PE architecture, export ordinal/name/forwarder, observed HRESULT/NTSTATUS and limitations. Do not rely on private source leaks, proprietary decompilation/code copying, or infer binary ABI layouts from guesses.

**IMPORTANT:** kawapure's headers are reconstructed pseudo-C++ from `uDWM.dll 10.0.19041.2075` and `dwmcore.dll 10.0.19041.2311` (Windows 10 2004–22H2 family), *not* complete source nor a stable API specification for Windows 11. The author's `IDwmChannel` is expressly unfinished; its vtable and parameter types are only partially understood. Maintain an `evidence.md` ledger with source URL, commit SHA, relevant line/file, target build, confidence: VERIFIED_PUBLIC / VERIFIED_BINARY_BUILD / INFERRED / UNKNOWN, and validation result. Never label an inference `verified`.

**Compatibility profiles:** Define an explicit public API/ABI profile (initially Windows 10 build 19041-family, with Windows 11 differences tracked separately). For private interfaces, pin the exact reference binary build; do not assume both DLL patch revisions are identical or stable across builds. Do not import private classes into application-visible APIs.

## 1. Audit of existing MicaNT — verified starting points

These findings are based on repository files inspected; the agent must revalidate them against the current branch before editing:

| Existing path | Observation | Required follow-up |
|---|---|---|
| `include/micant/shared/compositor_surface.h` | `MicaCompositorMetadataStore` v15 groups raw surface pointers, HWND-like IDs, dirty rectangles, cursor, theme, caption bitmaps, direct addresses, active writer counters and frame sequences. | Map producers/consumers, ownership and trust boundaries; plan transition to explicit window state, surface handles, compositor scene and safe IPC. Preserve current data path until replacement passes tests. |
| `kernel/win32k/freestanding.cpp` | Owns window state, compositor wake-up, shared metadata, deferred reclamation, damage and synchronization. | Audit kernel vs compositor responsibilities, memory lifetime, races and critical sections. Do not migrate kernel-only authority to an untrusted process. |
| `kernel/ps/winnt_win32k_server.cpp` | Implements a graphics/window-serving path with locally managed DCs, bitmaps and screen pixels. | Identify whether hosted, user-mode or kernel mode in each build; eliminate duplicate authorities only after call-graph proof. |
| `include/micant/um/subsystems/winuser/dwmapi.hpp` | Contains a broad class/enum/runtime implementation coupled to kernel32, user32, graphics and COM headers. | Separate public ABI declaration, user-mode DLL forwarding, server protocol and actual compositor logic. Verify enum values per target SDK. |
| `tools/dlls/abi/dwmapi_system32.cpp` and `tools/dlls/abi/ole32_dwmapi_winmm_impl.inc` | `DwmFlush()` returns success unconditionally; `DwmExtendFrameIntoClientArea()` only checks arguments; `DwmIsCompositionEnabled()` reports true; `DwmSet/GetWindowAttribute()` use a process-global dark-mode field and zero-fill others. | These are not valid behavioral implementations. Route to validated per-window compositor state and correct async synchronization; unsupported behavior must not be falsely reported as completed. |
| `include/micant/um/servers/ring3_winlogon_explorer_dwm.hpp`, `kernel/ps/ring3_desktop_shell.cpp` | Reports constant PIDs, planes, latency and readiness flags via a synthetic engine; logging is not proof of actual processes. | Remove synthetic success from runtime claims and acceptance gates. Require actual EPROCESS/ETHREAD, Ring 3 execution, imported DLLs, IPC, produced pixels and measured evidence. |
| `include/micant/um/subsystems/graphics/dcomp.hpp` | Defines broad DirectComposition concepts and COM GUIDs. | Audit correct IIDs, vtables, QueryInterface behavior and ownership using SDK, not header comments. |
| `tools/dlls/user32.cpp`, `tools/dlls/gdi32.cpp`, `tools/dlls/comctl32.cpp`, `tools/dlls/uxtheme.cpp`, `kernel/win32k/win32k.cpp` | Existing components span window creation, control drawing and server-side logic. | Document the precise window and control paint path and remove layered duplicates incrementally. |
| `tools/explorer_shell/*`, `tools/explorer_app.cpp` | Shell/taskbar/desktop code exists. | Explorer remains shell/client, never the compositor or owner of all other windows. |

This audit is **not exhaustive**. The first agent deliverable must be a call graph and ownership inventory covering every active build target, link-time owner, duplicated drawing routine, exported function, and IPC operation.

## 2. Non-negotiable responsibility boundaries

### Windowing and input

- **Kernel-mode Win32k/NTUSER (canonical window authority):** verified HWND/class/window object lifetime; parent/owner; thread and desktop/window-station association; styles, geometry, visibility, Z-order, activation, focus/capture; message-queue lifecycle; input routing, hit testing and security; redirection surface access control. Native mode crossings must be validated.
- **USER32.dll (user-mode API):** CreateWindowEx/DestroyWindow/ShowWindow/SetWindowPos/GetMessage/DispatchMessage/DefWindowProc/painting API shims; never own independent globally authoritative HWND state.
- **GDI32 / applicable graphics engine:** DC and raster operations, font/text/bitmap pipelines, invalid-region rendering into owned window surfaces. Do not paint app content in DWM.
- **COMCTL32 / control class procedures:** buttons, edits, list/tree/toolbar etc. USER32's built-in classes (e.g. `BUTTON`, `EDIT`) must not be incorrectly attributed to COMCTL32. Deliver `WM_PAINT`, state transitions, focus, accessibility hooks and `WM_COMMAND`/`WM_NOTIFY` correctly.
- **UXTheme.dll:** visual styles, themed control parts/states, colors and metrics. It does not substitute for the HWND/control state machine.

### Desktop composition

- **dwm.exe (real Ring 3 process, per interactive session as appropriate):** compositor lifecycle, internal scene and frame scheduling, surface consumption, dirty/occlusion tracking, transforms/effects, presentation, event-driven synchronization and isolation. Do not fake a PID or execute the whole compositor inside kernel initialization or Explorer.
- **uDWM.dll (private coordination layer in reference model):** window-to-scene representation, visual hierarchy and resource proxies, forwarding scene changes to the MIL engine through an internal channel. Reconstruct modeled classes only to the evidence level supported by pinned reference builds.
- **dwmcore.dll (private MIL engine):** authoritative composition resources, channels, committed scene snapshots, rendering scheduling and backend access. Implement MicaNT's own engine; no assertion of bit-for-bit DLL equivalence.
- **dwmapi.dll (public client ABI):** correct exported DWM functions and validation; talk to appropriate service/window state rather than process-global placeholders. Separate public ABI and implementation. `DwmFlush` must actually synchronize per documented semantics; `DwmSet/GetWindowAttribute` must be per-window and type/size checked.
- **dcomp.dll (public COM ABI):** correct `IDCompositionDevice*`, `IDCompositionTarget`, `IDCompositionVisual*`, effects, surface and transaction semantics, including vtable ordering, GUIDs and QueryInterface/AddRef/Release. Shared/private DWM-specific DComp IIDs must be separately researched and never conflated with public interfaces.
- **DXGI/D3D/WDDM/HAL/display driver:** allocate or share resources and perform rendering/presentation and safe synchronization. Start with real VirtIO 2D copy/composite backend; hardware GPU optimization is a later milestone, not grounds for claiming DirectFlip/MPO support.
- **Explorer.exe:** desktop shell/taskbar/file manager only; may create windows and surfaces, but must not be sole compositor implementation.

Non-client painting is cooperative between system window management and composition and varies by Windows build. Avoid the false simplification “DWM draws every titlebar button” or “USER32 draws the entire frame”. Define a version-scoped behavior contract and test it.

## 3. Mandatory render/control/interaction paths

### 3.1 Classic Win32 child BUTTON, end to end

`app.exe` creates top-level `HWND` via `USER32!CreateWindowExW` -> validated NTUSER/window object and thread/desktop state -> registers/creates child built-in `BUTTON` HWND -> input driver and USER hit testing -> button window proc receives `WM_LBUTTONDOWN` and `WM_LBUTTONUP` -> control state changes and invalidation -> `WM_PAINT`, `BeginPaint` and UXTheme/GDI render pixels in a real redirection/window surface -> damage notification with correct dirty region and generation -> DWM consumes updated surface -> scene composition and VirtIO GPU scanout -> parent receives `WM_COMMAND/BN_CLICKED` on a valid click.

Test: two windows in different processes; overlap; clicking through non-client/client areas; focus; disabled button; resize; dragging with no unnecessary client repaint; closing one process without UAF/stale visual; no content borrowed from dead process.

### 3.2 DirectComposition visual update

Client creates public DComp COM device/target/visuals and sets content, opacity, clip and transforms -> public API queues changes -> `Commit()` atomically publishes transaction to appropriate composition scene -> compositor validates handles, references and the process's authority -> MIL-like resource/channel tracks changes -> DWM renders/presents at proper frame boundary. `Commit` does not necessarily imply screen presentation has completed. Test `WaitForCommitCompletion` or relevant documented sync operation separately.

### 3.3 Move/resize and damage

Moving a valid already-rendered window should update scene position and Z-order without forcing an unconditional repaint of its client pixels. Resizing must preserve/crop/reallocate content according to a documented policy; invalidate only necessary regions. Track surface content generation separately from visual property/transaction generation. Verify occlusion, opacity, clipping and alpha blending against reference images with known tolerance.

### 3.4 Rendering/presentation

MicaNT compositor initial backend may render into a bounded system-memory scanout for VirtIO 2D. Show logs + captured frames + checksums and actual frame sequence/ack, not fabricated latency or planes. Later isolate hardware texture/sharing, DXGI, DirectFlip and MPO behind backend capabilities that are off until proven. A “successful” call with no effect is a correctness defect for implemented APIs.

## 4. Internal class/interface reconstruction policy

Build a reference inventory with a source-confidence column. Distinguish **matching symbol names**, **compatible callable ABI**, **matching behavior**, and **matching internal object layout**; these are different claims.

### 4.1 `uDWM!CCompositor`

From `udwm/compositor.h`: `CCompositor` derives from `CBaseObject`; methods `Create`, `Initialize`, `Shutdown`, proxy creation and shared-handle proxy creation. Members include an `IDwmChannel* m_pMilChannel`, reportedly obtained from `dwmcore!MilConnection_CreateChannel`, and a pointer to a DComp private interface reportedly obtained through `dcomp!DCompositionCreateDevice3`. The author explicitly says the DComp interface type is uncertain. Model these responsibilities but **do not** assert that the public `IDirectCompositionDevice` is the actual private interface.

### 4.2 `shared!IDwmChannel`

Reverse-engineered COM-like interface with `IUnknown` methods and `Commit`, `SynchronizedCommit`, `Destroy`, `SyncFlush`, `CreateResource`, `CreateSharedResource`, `DuplicateSharedResource`, `ReleaseResource`, `VisualSetOffset`, `VisualSetSize`, `VisualSetTransform`, `VisualSetClip`, `VisualSetContent`, child insertion/removal/movement, partition operations. The reference file labels this interface *unfinished*. Some parameter types and semantics are unknown. **Never silently copy this as proven full vtable ABI or expose it to ordinary Win32 programs.** Implement an internal typed channel contract and a separate version-pinned compatibility adapter only if adequate evidence exists.

### 4.3 `uDWM!CVisual`

Reference `udwm/visual.h` describes `CVisual`, with parent, `VisualCollection`, `CVisualProxy`, `CResourceProxy` content, visual state and methods such as `SetContent`, `SetSize`, `SetOpacity`, `SetDirtyFlags`, `SetDirtyChildren`, `UpdateLayout`, `UpdateTransform`, `RenderRecursive`, hit testing. Published object size **240 bytes for one specific x64 binary version**, not a general ABI requirement. Implement correct scene ownership and parent/child lifetime; preserve layout only for a proven, necessary version-pinned private compatibility test.

### 4.4 `VisualCollection`, `CResource`, `CResourceProxy`, `CBaseObject`

Mirror demonstrable ownership behaviors: child ordering/removal, resource lifecycle/reference counts, shared-resource handles, proxies across channels. The author notes `VisualCollection` can be inlined and pseudo-classes may not exist in the final binary. Do not force a source class to exist just to mimic a reverse-engineered convenience type.

### 4.5 Cross-module boundaries

The public ABI must be C/COM ABI, with verified PE exports, calling conventions, pointer width, structure alignment, HRESULT and NTSTATUS. Avoid passing C++ STL objects/exceptions across DLL boundaries and avoid raw kernel pointers to untrusted user mode. Internal MicaNT classes may be clean C++23 and retain original names where genuinely helpful.

**Private ABI success criteria:** do not claim “identical classes and calls” until exact version-specific binary evidence verifies layout, vtables, call conventions and semantics. Otherwise state `behaviorally analogous`, `inferred layout` or `not implemented`.

## 5. Proposed canonical module ownership (adapt to current repository)

Do not build these paths blindly; first map existing targets and migrate to canonical owners:

```
kernel/win32k/              NTUSER/window, input, window manager, surface authority
kernel/mm/                  memory sharing, validated mappings, lifetime
kernel/io/ + drivers/       GPU/driver interface and present, no app UI rendering
include/micant/shared/      explicit versioned ABI contracts (no raw unchecked addresses)
tools/dlls/user32*          user32 public ABI and client marshaling
tools/dlls/gdi32*           GDI public ABI and real paint/damage path
tools/dlls/comctl32*        common controls
 tools/dlls/uxtheme*        visual styles
 tools/dlls/abi/dwmapi*     dwmapi public exports, forwarding and validation
 tools/dlls/dcomp*          DComp COM public ABI, channels, transactions
 tools/dwm/                 actual dwm.exe user process (or canonical existing equivalent)
 tools/dwmcore/             private composition engine implementation
 tools/udwm/                window-visual coordination/proxies
 tools/explorer_shell/      shell only
 test/                      host tests + QEMU real-process integration tests
 docs/                      contracts, evidence, migration status
```

Correct final paths to actual build conventions; preserve one authoritative `ntoskrnl_core` and avoid a second monolithic kernel. `dwmcore.dll` and `uDWM.dll` may initially be linked as private modules into `dwm.exe`; only emit separate DLL binaries when actual loader/import lifecycle supports them. The architecture, not a fake artifact name, is the early acceptance target.

## 6. Refactoring plan — execute sequentially, one verified vertical slice at a time

### Phase 0 — baseline and traceability (NO large refactor)

- Read AGENTS/architecture/integration/security and trace CMake targets. Capture current commit SHA, boot configuration, QEMU screenshots/serial and test results.
- Create `docs/graphics/WINDOWING_COMPOSITION_AUDIT.md` with **path -> target -> layer -> owner -> exports -> caller -> real/stub/synthetic -> tests -> planned destination**.
- Build full producer/consumer map of `MicaCompositorMetadataStore`, `dwmapi` functions, DComp objects, `user32`/GDI/WIN32K, Explorer drawing, kernel/user boundaries.
- Create `docs/graphics/WINDOWS_INTERFACE_EVIDENCE.md` with public/private ABI inventory and source confidence.
- Inventory all actual `.dll/.exe` exports via `llvm-readobj --coff-exports` or equivalent. Compare public names, ordinals (when necessary), signatures, structure sizes, IIDs and COM vtables with SDK and reference Windows version. Do not use a Wine `.spec` as sole proof of Windows-private ordinal mappings.
- Write down **what currently works, what fails, and what is synthetic**. Tests must run with actual canonical images; no legacy fallback or stale artifacts.

**Exit:** audit and reproducible baseline, with no false success claims; no assertion that a prototype equals a fully implemented DWM.

### Phase 1 — establish correct owners and contracts

- Define the window authority contract in Win32k: HWND lifecycle, scene-facing window events, surface ownership and validated per-process handles.
- Define separate `WindowState`, `WindowSurface`, `VisualNode`, `SharedResource`, `SceneTransaction`, `FrameFence`, `DamageRegion`, `PresentBackend` and their owner/lifetime/threading rules.
- Create versioned IPC/protocol with rights checks, checked sizes, overflow checks, handle generation, channel teardown, fences, producer/consumer memory ordering, safe unmap and recovery on process exit. No arbitrary kernel pointers in user-visible metadata.
- Migrate one producer and consumer behind an adapter, leaving the old path operational until equivalence is demonstrated. Document deprecation criteria.

**Exit:** one window can render through a validated shared surface and a distinct logical compositor scene, with no lifetime leak.

### Phase 2 — real Ring 3 DWM and basic composition

- Ensure a genuine EPROCESS/ETHREAD and image loader path for `dwm.exe` with actual startup; no hard-coded PID or counters.
- Attach compositor to interactive session/desktop with appropriate access checks. Ensure clear lifetime relative to session manager, winlogon and Explorer.
- Consume scene/window updates through validated channels; build an internal visual tree with parent, children, resource proxy, opacity, offset, clip, Z-order and dirty propagation.
- Build software/2D VirtIO backend that composites at least two real user-process windows + cursor into an actual presented scanout.
- Add crash/restart and teardown tests; no kernel desktop paint path masquerading as compositor.

**Exit:** headless QEMU verifies actual separate Ring 3 compositor and window apps, pixel output, and correct process teardown.

### Phase 3 — real USER32/GDI/control pipeline

- Validate the standard window class and child control registration/creation, message dispatch, paint/DC lifecycle, invalidation and clipping.
- Implement one working built-in `BUTTON` with normal/hot/pressed/disabled/focus and parent `WM_COMMAND/BN_CLICKED`. Expand to `EDIT` and `STATIC` before advanced COMCTL32.
- Keep USER32 built-in control ownership distinct from COMCTL32 common controls and UXTheme rendering.
- Make `WM_PAINT` update actual redirection surface and issue scoped damage. Moving a window should not trigger a full client repaint unless required.

**Exit:** click on an actual child button in a Ring 3 process updates content and reaches its parent; screenshots and event traces match expected UI; two apps do not corrupt each other's surfaces.

### Phase 4 — public DWMAPI fidelity

- Split `dwmapi.dll` export layer from compositor implementation. Implement validated per-HWND `DwmSetWindowAttribute`, `DwmGetWindowAttribute`, `DwmIsCompositionEnabled`, `DwmExtendFrameIntoClientArea`, `DwmFlush`, thumbnails and iconic APIs progressively.
- Use verified `DWMWINDOWATTRIBUTE`/structure versions, default values, size checking, error handling, destroyed-HWND semantics and Windows build conditions.
- Implement actual state propagation and ack/fence semantics. Remove global dark mode state and unconditional `S_OK` placeholders.

**Exit:** each advertised export has a behavior test; undocumented/unsupported functionality clearly marked, not claimed complete.

### Phase 5 — DComp and MIL-like channel/resource transactions

- Preserve exact documented DComp COM identities, QueryInterface, refcount and vtable/ABI for selected profile.
- Implement device/target/visual creation, contents, transforms, opacity, clip, child ordering, transaction batching and commit scheduling. Include resource/channel destruction and shared handle security.
- Map uDWM-ish visual coordination -> private channel -> dwmcore-ish engine. Use names matching kawapure *only where validated* and distinguish internal adapter from exposed ABI.
- Test two producer processes, nested visuals, commit atomicity, no rendering of half-applied transaction, failure recovery and resource reuse.

**Exit:** observable output and synchronization behavior, not a syntactic class replica.

### Phase 6 — fidelity/performance and extended paths

- Add non-client geometry, frame margins, caption behavior, shadows, thumbnail, occlusion, DPI/multimonitor, animations, high-resolution frame timing, effects, graphics memory pressure, tearing/vsync and session lifecycle, each behind independent tests.
- Only then evaluate DXGI shared GPU resources, GPU compositor and optional direct presentation/MPO, disabled unless genuinely supported.
- Compare against a pinned Windows reference profile and record known differences. Avoid claims of identical Windows internal implementation where private evidence does not support it.

## 7. Test suite / acceptance gates (mandatory)

**ABI:** PE export name/ordinal/forwarder, `extern "C"`/calling convention, struct sizes/alignments, GUIDs, COM vtable indices and HRESULT behavior. Reference-build applicability recorded.

**Window lifecycle:** create/destroy; parent/owner; activate/focus/capture; child controls; resize/move; clipping; Z-order; destruction/reparenting; process death and compositor restart.

**Control semantics:** `BUTTON` initial/hover/pressed/disabled/focus; `WM_PAINT`/`BeginPaint`/`EndPaint`; mouse down/up and valid `BN_CLICKED`, no spurious notifications, correct user-mode address separation.

**Composition:** two independently produced surfaces, alpha/clip/scale/offset, nested visuals, commit atomicity, dirty rectangles, stable content when moving window, occlusion, proper fence/ack, correct scanout pixels and cursor.

**Concurrency/security:** malicious/invalid command lengths, out-of-range handle and surface IDs, stale generation, corrupt dimensions, user pointers, process exit during commit, concurrent resize and rendering, overflow and underflow, permission checks on shared resources. Every error propagates, no silent success.

**Proof:** host test + freestanding build + QEMU serial + current checkout build manifest + actual window and button event traces + image or pixel checks; no saved/stale screenshot as success. Record which specific functionality was validated and whether a physical GPU or merely VirtIO 2D was tested.

## 8. Work rules for AI coding agents

1. **Inspect before changing.** Read repository instructions and current integration status; do not infer entire implementation from file names or comments.
2. **Implement incrementally.** One architectural boundary or vertical slice per PR; no all-at-once mass rewrite, new parallel Window Manager, or speculative deletion.
3. **Follow source authority.** Public SDK/contracts take precedence over Wine and incomplete RE headers. Use kawapure for pinned internal architectural hypotheses, not general public contracts.
4. **No fake success.** Do not manufacture PIDs, timings, object counts, feature flags, exports, tests, visuals or GPU capabilities. Remove synthetic reporting from acceptance paths.
5. **No silent stubs.** If a feature cannot be implemented, return truthful documented error where appropriate and mark it unimplemented; do not return success for absent effects.
6. **Preserve system invariants.** Kernel owns security, handles and mappings; user-mode compositor cannot dereference arbitrary kernel pointers. No duplicate executive/window authorities.
7. **Compile and run.** Every increment must pass impacted hosted unit tests, real freestanding target, QEMU boot and targeted integration tests; failures reported honestly.
8. **Provide traceability.** Every moved file/export/class gets evidence source, old owner, new owner, behavior test, migration status and rollback plan.
9. **Enforce clean-room licensing.** Public docs, headers, lawful behavior tests and RE descriptions may inform independently written code. Do not paste incompatible LGPL or leaked proprietary source.
10. **Do not broaden scope.** Do not simultaneously rewrite unrelated HAL, scheduler, filesystem, security subsystem, UI style or architecture without a demonstrated dependency.

## 9. Required AI-agent output for each stage

Deliver at each phase:

- **Findings:** precise path/symbol and why current owner or behavior is incorrect, distinguishing observation from inference.
- **Evidence matrix:** reference URL, source revision/build, public vs private, confidence and open unknowns.
- **Architecture change:** before/after call graph, memory/IPC boundary, ownership/lifetime model, compatibility impact and risk.
- **Patch:** minimal code changes, build target and migration adapter, no unverified fabricated compatibility logic.
- **Proof:** build commands, test command/results, QEMU serial/checksum/screenshot results and any failing test.
- **Status:** implemented/partial/not implemented, known Windows mismatch, rollback note and next bounded step.

**Do not merge or claim complete** when the current run has only hosted tests, stub-based exports, boot splash output or synthetic metrics.

## 10. First task to execute NOW

> Perform **Phase 0 only**, without undertaking a giant rewrite. Read the current MicaNT repository and kawapure repo end-to-end. Enumerate `dwmapi`, `dcomp`, `user32`, `gdi32`, `comctl32`, `uxtheme`, `win32k`, compositor shared memory, Explorer, true process launcher and graphics driver. Produce the audit matrix, exact call paths and proposed single-source ownership mapping. Pin sources/versions. Identify and prioritize every fake success path (especially synthetic Ring3 metrics and DWMAPI unconditional success), mismatched responsibility and unsafe cross-process pointer. Define the minimal first vertical slice and tests; then implement that slice only after the audit is approved or clearly recorded. Do not assert private interface equivalence without evidence.

## Reference URLs

- MicaNT repository: https://github.com/luca-saggese/MicaNT
- Canonical architecture: https://github.com/luca-saggese/MicaNT/blob/main/docs/KERNEL_ARCHITECTURE.md
- Integration status: https://github.com/luca-saggese/MicaNT/blob/main/docs/KERNEL_INTEGRATION_STATUS.md
- Clean-room policy: https://github.com/luca-saggese/MicaNT/blob/main/docs/CLEAN_ROOM.md
- DWM internals: https://github.com/kawapure/DWM-Documentation
- Compositor: https://github.com/kawapure/DWM-Documentation/blob/main/udwm/compositor.h
- IDwmChannel: https://github.com/kawapure/DWM-Documentation/blob/main/shared/idwmchannel.h
- Visual: https://github.com/kawapure/DWM-Documentation/blob/main/udwm/visual.h
- Visual collection: https://github.com/kawapure/DWM-Documentation/blob/main/udwm/visualcollection.h
- DWM API: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/
- DirectComposition: https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components
- Public Win32 metadata: https://github.com/microsoft/win32metadata
- WPF MIL (historical): https://github.com/dotnet/wpf/tree/main/src/Microsoft.DotNet.Wpf/src/WpfGfx
- Wine DWM exports comparison: https://github.com/wine-mirror/wine/blob/master/dlls/dwmapi/dwmapi.spec
- ReactOS DWM comparison: https://github.com/reactos/reactos/tree/master/dll/win32/dwmapi
