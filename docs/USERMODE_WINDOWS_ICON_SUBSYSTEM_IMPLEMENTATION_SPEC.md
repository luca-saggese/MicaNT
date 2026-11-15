# MicaNT — Windows-Compatible Icon Subsystem Implementation Specification

**Path:** `docs/USERMODE_WINDOWS_ICON_SUBSYSTEM_IMPLEMENTATION_SPEC.md`  
**Status:** Proposed implementation specification; not proof of implemented compatibility  
**Platform:** AMD64, C++23, canonical modular NT-compatible kernel, Ring 3 Win32 desktop  
**Primary owners:** User32, GDI32, Shell32, Comctl32, PE resource loader, user-mode image decoders, Explorer; existing canonical Win32k/graphics services  
**Related specifications:** `USERMODE_IMAGE_CODEC_AND_WALLPAPER_SPEC.md`, `USERMODE_FONT_ENGINE_TRUETYPE_OPENTYPE_SPEC.md`, `KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`, `KERNEL_INTEGRATION_STATUS.md`

## 0. Mandatory repository and implementation rules

Read `AGENTS.md`, `docs/KERNEL_ARCHITECTURE.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, the actual source and build target graph before changes. Preserve the existing working tree and the single canonical modular kernel. The archived monolithic implementation is at most a historical behavior reference; do not compile, link, or reintroduce it. Do not create a second graphics manager, memory manager, object manager or PE loader. Never claim success based on an exported function, an instantiated hosted class, a static mock report, a successful build alone or a diagnostic screen. Record distinct evidence: **specified / source implemented / linked into actual PE / executed in CPL3 / verified in QEMU / verified on physical hardware**.

When implementing, prefer reusing real existing components and advancing a minimum connected end-to-end slice. Keep parsers, image decoding, shell decisions and caches in Ring 3. Use the existing kernel's validated syscall/surface and security boundaries. Do not expose `micant/km/` private headers to user-mode DLL targets. External icon artwork and code must have recorded licenses and attribution; the API implementation must be clean-room against public Win32 contracts.

## 1. Objective and completion criteria

Make a Windows-compatible application able to load a real icon from an `.ico` file or from `RT_GROUP_ICON`/`RT_ICON` resources in a PE executable or DLL, receive a valid `HICON`, query or duplicate it, draw it through GDI/User32, assign it to a window, and release it with correct ownership semantics. Extend this to Explorer, file type associations, common-control image lists, icon caching, shell namespace and taskbar/desktop rendering. Explicitly support transparency, DPI, icon variants and reliable invalidation.

**First demonstrable outcome:** a Ring 3 test program opens a real icon-containing PE from the filesystem, calls `LoadIconW` or `LoadImageW`, assigns the result with `WM_SETICON`, and renders it with `DrawIconEx` onto the MicaNT desktop in QEMU. Explorer then shows that resource's real icon in a window/title/taskbar and a file listing. Boot-diagnostic hardcoded icon rectangles do not qualify.

**Non-goals for initial slice:** full SVG runtime, every shell COM extension, all icon codecs, Windows 11 binary-internal implementation parity, arbitrary accelerated 3D composition, complete thumbnail extraction, every `IExtractIcon`/`IShellItemImageFactory` case, loading Microsoft proprietary icon assets without licenses.

## 2. Verified repository starting point and gaps

Snapshot findings from repository inspection (revalidate at implementation time):

- `tools/explorer_shell/desktop.cpp` draws four hardcoded icons (Computer, Documents, Network, Recycle Bin) through rectangular pixel fills; it does not load real `.ico`/PE resources.
- `include/micant/um/subsystems/imaging/bmp_png_decoder.hpp` contains bounded BMP/PNG decoder logic and refers to `image_decoder.hpp`. Reuse its actual supported variants; its existence does not imply ICO-compatible DIB support.
- `include/micant/um/subsystems/winuser/user32.hpp`, `gdi32.hpp`, `shell32.hpp` and `comctl32.hpp` represent various Win32 API layers. The real Ring 3 DLL targets are under `tools/dlls/`. Audit exports and behavior independently.
- `tools/dlls/shell32.cpp` includes placeholder-like exports, including `ShellExecuteExW` reporting success without demonstrating actual shell work. No placeholder icon API may imitate success.
- `include/micant/um/subsystems/winuser/shell32.hpp` includes a private kernel filesystem header; any actual user-mode linkage must obey the boundary audit.
- `kernel/win32k/freestanding.cpp`, the shared Win32k request boundary, `tools/explorer_app.cpp` and the modular Explorer shell are relevant existing owners.
- Existing documentation reports a bounded GDI surface/`BitBlt` bootstrap, not a verified complete Windows icon subsystem or GPU-accelerated composition.

Create a source-to-export inventory with the actual status of each API below, including import library, calling convention, C/C++ ABI, target DLL, data ownership, and bare-metal execution proof. Update this document as evidence changes.

## 3. Architectural model

```text
Explorer / taskbar / Start / Win32 applications (Ring 3)
    |                     |                       |
    | User32 HICON        | Shell32 icon lookup   | Comctl32 HIMAGELIST
    +---------------------+-----------------------+
                          |
          User-mode Icon Service (shared primitives)
          source resolver / resource selection / object lifetime
          ICO + RT_GROUP_ICON + RT_ICON resource reader
          bounded DIB/PNG decoder + icon variant/scaler
                          |
       BGRA8 premultiplied surface + optional AND mask
                          |
       User32 DrawIconEx / GDI32 blit and alpha composite
                          |
           canonical Win32k graphics client ABI
                          |
           Ring 0 graphics service / VirtIO-GPU
```

This is a **logical** diagram, not a requirement for a new central daemon. A reusable user-mode library/component can own decoding and caching; APIs continue to be exported by their appropriate Windows-compatible DLLs. `HICON`, `HBITMAP`, and `HIMAGELIST` are different object types, with independent lifetime, access validation and teardown. Do not interchange raw pointers or rely on process-global unchecked casts. If Win32k owns any handle backing, that ownership must be explicit and implemented through the canonical object/graphics service; do not create a parallel handle table.

## 4. Public compatibility contract: function families

### 4.1 User32 icon creation, loading, lifetime

| API | Required behavior / contract |
|---|---|
| `LoadIconA/W` | Load named/ordinal `RT_GROUP_ICON` from module or stock icon with null instance; shared icon lifetime; nominal system icon size. |
| `LoadImageA/W` | `IMAGE_ICON` first; named/ordinal resource or `LR_LOADFROMFILE`; desired width/height, relevant flags `LR_DEFAULTSIZE`, `LR_SHARED`, `LR_MONOCHROME`, `LR_CREATEDIBSECTION` where applicable. Other image types are separate work, not false successes. |
| `CreateIcon` | Construct monochrome/color icon from caller-provided XOR/AND planes with documented bitmap validation. |
| `CreateIconIndirect` | Consume `ICONINFO` and valid GDI masks/color bitmap; create independent owned icon, not an alias of input bitmaps. |
| `CreateIconFromResource` / `CreateIconFromResourceEx` | Decode the resource image format and flags; distinguish default sharing semantics from owned handle behavior. |
| `CopyIcon` | Return independent owned copy of icon content and metadata. |
| `CopyImage` (`IMAGE_ICON`) | Resize/copy icon, validate flags, retain possible `LR_COPYRETURNORG` sharing/ownership semantics. |
| `DestroyIcon` | Release **owned** icon; never free shared stock/module icon; invalid/double destruction must not cause UAF. |
| `GetIconInfo` / `GetIconInfoExA/W` | Return metadata and caller-owned bitmap handles that must be disposed through GDI; handle mask geometry and hotspots consistently. |
| `LookupIconIdFromDirectory` / `LookupIconIdFromDirectoryEx` | Choose the best `RT_ICON` resource identifier from a *group* based on size, depth and requested flags. |
| `LoadIconMetric` / `LoadIconWithScaleDown` | Later DPI-aware helper support where applicable; do not pretend these are standard User32 exports (Windows places them in Comctl32). |

Implement A/W conversions with clear encoding and errors, not lossy casts. Preserve exact documented public signatures, target DLL ownership and export names/ordinals when relevant. Test `GetLastError`/error outcomes on malformed arguments. `LoadIcon` is a shared icon; a `CopyIcon` produces an owned instance. Shared icons may outlive individual callers according to module/cache lifetime rules, and `DestroyIcon` must not free them. Microsoft documents these distinctions explicitly.

### 4.2 User32 display and window integration

| API / contract | Required behavior |
|---|---|
| `DrawIcon`, `DrawIconEx` | Draw into validated device context with scaling, clipping, `DI_NORMAL`, `DI_IMAGE`, `DI_MASK`, `DI_COMPAT`, brush background where supported; preserve alpha channel and legacy AND/XOR behavior. |
| `GetSystemMetrics(SM_CXICON/SM_CYICON/SM_CXSMICON/SM_CYSMICON)` | Return display/session metrics for appropriate DPI profile, not hardcoded assumptions hidden in rendering logic. |
| `WM_SETICON` / `WM_GETICON` | Store/retrieve small and large window icons, ownership clearly documented; update nonclient/taskbar surfaces and repaint on change. |
| `WNDCLASSEX.hIcon/hIconSm` | Register class defaults, inherit to windows when no explicit window icon, support correct class lifetime. |
| `GetClassLongPtr` / `SetClassLongPtr` with `GCLP_HICON`, `GCLP_HICONSM` | Access/change class icon handles and send necessary invalidations; preserve cross-process restrictions. |
| `STM_SETIMAGE` / `STM_GETIMAGE` (`IMAGE_ICON`) | Later control integration using correct handle lifetime; no unnecessary duplicate icon storage. |

Drawing must use alpha-premultiplied BGRA8 and correct source-over composition when 32-bit alpha is present. For classic icons without meaningful alpha, use AND mask fallback and proper 1bpp bit order, stride and bottom-up DIB orientation. Take care not to *double-premultiply* alpha or treat the classic 32bpp reserved channel as trustworthy without validating semantics.

### 4.3 PE resources / Kernel32 and loader integration

Implement/reuse `FindResourceA/W`, `FindResourceExA/W`, `LoadResource`, `LockResource`, `SizeofResource`, `EnumResourceNamesA/W`, `EnumResourceTypesA/W` and `FreeResource` documented behavior where required by the icon path. Preserve module reference/lifetime and stable handles. Do not expose raw unvalidated PE addresses. Resource readers must follow bounds through DOS header → NT header → section table → resource data directory → resource directory tree → resource data entry → mapped byte range.

Icon-specific resource IDs: `RT_ICON = 3`, `RT_GROUP_ICON = 14` (and optionally `RT_CURSOR = 1`, `RT_GROUP_CURSOR = 12` for later cursor reuse). Handle integer and named resources and language variants. Handle RVA-to-file-offset mapping of file-backed PE carefully, including `VirtualSize`, `SizeOfRawData`, alignment, integer overflow, nested resource directory entries and cycles. The PE loader and icon reader share a *validated resource accessor*, not a second PE loader. Resource handle lifetime must remain valid while referenced, including if the originating module is unloaded or replaced.

### 4.4 Shell32 / Shlwapi / common controls

| API or interface | Phase / semantics |
|---|---|
| `ExtractIconExA/W` | Number of icons and extraction into large/small `HICON`; resource ordinal handling and cleanup. |
| `ExtractIconA/W` | Indexed icon retrieval, compatible error paths and handle ownership. |
| `PrivateExtractIconsA/W` | Per-icon target sizing, resource id output, count and validation. |
| `SHGetFileInfoA/W` | File/folder/icon lookup, `SHGFI_ICON`, `SHGFI_SYSICONINDEX`, `SHGFI_SMALLICON/LARGEICON`, `SHGFI_USEFILEATTRIBUTES`; correct ownership of returned icons. |
| `SHGetImageList` | System image list sized for display/DPI, when supported by shell architecture. |
| `IExtractIconA/W` | Later shell namespace callback for non-filesystem/custom icon sources; never execute untrusted extension code in a privileged service. |
| `IShellItemImageFactory` | Later shell item images/thumbnails, not required for icon baseline. |
| `ImageList_Create`, `ImageList_Destroy`, `ImageList_Add`, `ImageList_AddIcon`, `ImageList_ReplaceIcon`, `ImageList_GetIcon`, `ImageList_Draw/DrawEx`, `ImageList_GetImageCount`, `ImageList_Remove`, `ImageList_SetIconSize`, `ImageList_GetIconSize` | `comctl32.dll`, bounded `HIMAGELIST` object store and correct image ownership. |
| File associations | Association/progID/default icon lookup using canonical registry/configuration, with per-user overrides and fallbacks. |
| Shortcut resolution | `.lnk` target/icon path and icon index, later phase; validate all paths and references. |

Do not fabricate icon indices or return `TRUE` without work. The API matrix must map **all** public entrypoints to their actual DLL, implementation phase and tests. Win32 import compatibility should not be claimed until a compiled client calls them through the built DLL boundary.

## 5. ICO and icon group format

### 5.1 `.ico` parser

Read `ICONDIR` (reserved/type/count) and `ICONDIRENTRY` records using explicit little-endian readers; do not `reinterpret_cast` untrusted file buffers into native structs. Zero width or height in an ICO entry conventionally denotes 256. Validate each `dwBytesInRes`, `dwImageOffset`, range, overlap policy, image count and cumulative bytes; reject integer overflow, truncated or overlapping corrupt records as appropriate. Bound encoded bytes, count, dimensions, pixels and total decoded memory before allocation.

Decode both resource forms: classic icon DIB (BMP-style without a BMP file header) and PNG payload. DIB icons conventionally carry XOR color data plus AND mask, and DIB height may represent combined XOR+AND planes: inspect bit depth/compression and derive logical dimensions consistently. Support common 1/4/8/24/32 bpp families incrementally with color table and row padding; explicit unsupported return for RLE/bitfields not implemented. Modern PNG payloads should reuse the real Ring 3 PNG codec. Respect binary transparency and antialiased alpha. Do not conflate `BITMAPINFOHEADER` fields of an ICO DIB with an ordinary `.bmp` file.

### 5.2 PE group resources

`RT_GROUP_ICON` contains `GRPICONDIR` plus `GRPICONDIRENTRY` records whose final field is a *resource ID* (`nID`), **not a byte offset**. Resolve each candidate through `RT_ICON` using the same module/resource language selection policy. Reject missing IDs, malformed groups, duplicate/cyclic resource mappings as required, invalid sizes and data entries outside the PE image. Select the best available candidate by requested pixels and color capabilities, avoiding unnecessary blurry upscaling. A single `.exe` may contain multiple distinct icon groups.

### 5.3 Multiresolution and DPI

Maintain source variants (typical 16/20/24/32/40/48/64/128/256 px, as actually present) and a DPI-qualified raster surface cache. Support nearest suitable variant and high-quality downscale in user mode; never assume requested dimensions are physically available in an asset. Define a deterministic tie-breaker, DPI-change invalidation and explicit failure/fallback path. Do not report a non-existent variant as hardware/native-resolution output.

## 6. Shared internal representation and handle lifetime

Define a compact private representation such as:

```cpp
struct IconBitmapView {
    uint32_t width, height, stride;
    PixelFormat format; // premultiplied BGRA8
    const uint8_t* pixels;
};
struct IconVariant { uint32_t pixelWidth, pixelHeight, nominalDpi; /* owned image */ };
struct IconMetadata { bool isShared; bool isIcon; int32_t hotspotX, hotspotY; /* source identity */ };
```

These are **conceptual private structures**, not a required ABI. An `HICON` must refer to a validated object rather than a transient decoder pointer. Define process-local vs session-shared semantics, reference counts, ownership of bitmaps and modules, cache eviction and teardown order. Borrowed handles from window/class icon fields must not accidentally grant delete access. `CopyIcon` returns an owned copy; `GetIconInfo` returns independent GDI bitmaps; image-list ownership and `ImageList_GetIcon` copy behavior must be tested. On process termination, reclaim all owned icon objects, backing surfaces, pending requests and callback references without freeing shared assets still in use.

**Concurrency:** synchronized handle lookup/close and cache updates, generation IDs to avoid ABA, immutable published pixel buffers, no callbacks under locks, no use-after-free when modules unload or Explorer restarts. Cache memory pressure must trigger bounded eviction, not uncontrolled growth.

## 7. Drawing contract: GDI/User32 and compositor

`DrawIconEx` validates DC, source HICON, destination coordinates, size, flags, clipping and target surface. Use the existing GDI DIB surface/blit path and canonical Win32k client ABI. Handle destination alpha, source-over math, gamma limitations (if not implemented, document), scaling and DPI. A successful call must modify the actual target pixels. On malformed handle or invalid DC, fail without memory modification. Treat transparent pixels correctly, support legacy AND masks, and avoid reading outside either bitmap during clipping.

Windows and Explorer must use `WM_SETICON`, class icon defaults and shell icon surfaces, with consistent repaint and taskbar icon references. Do **not** draw icons by writing to GOP/VRAM from application code. The compositor handles presentation and damage tracking; for icons on static desktop backgrounds use cached decoded surfaces and repaint only invalidated regions. Keep a software backend functional with VirtIO 2D; acceleration is optional and separately measured.

## 8. Shell identity, file associations and icon resolver

Implement an explicit resolver precedence policy consistent with Win32 shell behavior as far as supported:

1. Explicit icon associated with a shell item/shortcut or explicit `DefaultIcon` association when applicable.
2. PE executable resource icon (preferred group and language).
3. Folder/file association icon from registered ProgID and per-user/system configuration.
4. Known-folder/device/namespace icon from licensed MicaNT system asset pack.
5. Generic file/folder/application fallback.

The real ordering may vary by item kind and shell API; verify each supported contract against Microsoft documentation and tests. Never infer an icon solely from the file extension if a more-specific authoritative source is available. Use canonical Cm registry and profile data for user overrides. Normalize paths, resolve symlinks/reparse points safely, enforce caller ACLs, and do not leak icon bytes or existence metadata for inaccessible files. Prevent icon extraction from triggering execution of the PE payload or `DllMain` merely to enumerate resource data.

### System icon artwork

Use Microsoft **Fluent UI System Icons** (`https://github.com/microsoft/fluentui-system-icons`) as an optional modern look-and-feel source subject to verifying the current upstream MIT `LICENSE`, copyright notice and redistribution requirements. Keep a dedicated third-party license notice, a reproducible source/version pin and provenance manifest. Most icons are SVG: choose a small finite subset for known folders, files and UI actions, then convert to raster PNG/icon variants **at build time** using licensed tooling. An SVG runtime is not a prerequisite for IC0–IC4. Windows-proprietary icons are not automatically redistributable; never scrape OS installation icon binaries as default assets. Icons embedded in user-provided applications remain the property/license responsibility of those applications.

## 9. Security and robustness requirements

All `.ico` files and PE resources are untrusted input. Provide checked arithmetic for headers, table lengths, offsets, dimensions, strides, masks, palettes, recursion depth, image count and decoded memory. Reject zero/huge dimensions, excessive PNG decompression, malformed zlib/chunks, invalid bitmap headers and resource trees, self-referential pointers, overlapping/escaping data ranges and unsupported pixel formats. Set configurable per-request size, time/effort and aggregate cache budgets. Use staged immutable outputs: publish an HICON only once every validation/decoding step succeeds; on failure release all partial allocations and do not poison global caches. Fuzz parsers with libFuzzer/AFL-compatible harnesses where toolchains permit, plus deterministic malformed corpus in hosted CI.

Cross-process icon/window-handle access must obey Win32k process/session rules; an unprivileged caller cannot forge HICON metadata, free another process's owned handles, read arbitrary kernel/user addresses through `GetIconInfo` or escape sandbox boundaries through a specially crafted resource. No raw untrusted user pointer is dereferenced in Ring 0. PE resource mapping is read-only and module lifetime safe. Record source licenses and keep asset parsing separate from shell trust decisions.

## 10. Detailed implementation plan and gates

### IC0 — Inventory, verified parser contract and portable decoder

- Audit actual `tools/dlls/{user32,gdi32,shell32,comctl32}.cpp`, `image_decoder.hpp`, BMP/PNG decoder and PE loader resource logic.
- Record existing API results as real/hosted/stub/unimplemented; identify import closure for test clients and Explorer.
- Implement standalone bounded ICO directory parsing and resource-image selection, with DIB-ICO + embedded-PNG decoding into premultiplied BGRA8.
- Build hosted tests using independently constructed sample `.ico` files and malformed cases.

**Gate IC0:** decode two genuine ICO variants (PNG and classic DIB) with correct dimensions, transparency, channel order and correct failure behavior on corrupted files. No fake textures.

### IC1 — PE resource extraction, `HICON` and User32 base

- Implement `RT_GROUP_ICON` traversal and `RT_ICON` resolution using the canonical PE resource accessor.
- Implement `LoadIconA/W`, `LoadImageA/W(IMAGE_ICON)`, `CreateIconIndirect`, `CreateIconFromResourceEx`, `DestroyIcon`, `CopyIcon`, `GetIconInfo`, `LookupIconIdFromDirectoryEx` first; phase remaining APIs behind explicit missing-support errors.
- Establish owned/shared objects, module references, handle validation and cleanup.
- Add a Ring 3 PE fixture with at least two icon groups and 16/32/256px variants, and load it without executing the file.

**Gate IC1:** icon from real mounted `.exe`/`.dll` can be retrieved through a built DLL export by a real CPL3 client; shared icon is not destroyed by application; copy survives releasing the source object/module as contracted.

### IC2 — GDI rendering and window icon integration

- Implement `DrawIcon`/`DrawIconEx`, clipping, alpha/AND masks and scale into real user-mode GDI surfaces.
- Support `WM_SETICON/WM_GETICON`, class icon defaults and correct nonclient/taskbar invalidation.
- Verify user-to-kernel graphics request through existing Win32k ABI, actual scanout and pixel checks.

**Gate IC2:** QEMU shows a real PE resource icon in a test window, including alpha transparency, correct background preservation and dynamic icon replacement.

### IC3 — Comctl32 image lists and Shell32 extraction

- Implement bounded `HIMAGELIST` image lists and the core `ImageList_*` API family.
- Implement `ExtractIconEx`, `PrivateExtractIcons`, `SHGetFileInfo` for regular filesystem files, DLLs and executables, including small/large selection and fallback.
- Provide APIs through the correct DLLs and call them through actual import resolution.

**Gate IC3:** Explorer can enumerate folder entries and obtain file-type/application icons without hardcoded icon drawing; image-list indices remain stable until documented invalidation/removal.

### IC4 — Desktop, taskbar, Start menu and association integration

- Replace hardcoded `drawIcon()` glyphs in `tools/explorer_shell/desktop.cpp` with the icon resolver; maintain safe built-in fallback if asset pack is missing.
- Display icons from actual PE resources, generic known folders and association policy.
- Add icon changes in window title/taskbar via `WM_SETICON`, DPI preferences, user profile overrides and cache invalidation.
- Package licensed Fluent-derived raster assets with an attribution manifest.

**Gate IC4:** QEMU desktop/taskbar shows real decoded icons; opening/moving/closing windows preserves icon appearance, no flicker, no leaks. The fallback icon renders if source file missing or malicious.

### IC5 — Advanced compatibility and resilience

- Finish less common User32 icon APIs and flag combinations; resource language variants and extended `GetIconInfoEx`.
- Implement shortcut icon locations, shell namespace extensions where supported, system image lists and DPI changes.
- Fuzz and harden PE/ICO parsers; enforce resource/caching budgets; graceful recovery after corrupted icon/driver reset.
- Extend icon candidate selection, bit depths, interop with Windows-generated ICO files and multi-monitor DPI where supported.

**Gate IC5:** explicit compatibility test matrix, stress tests and failure-injection coverage; only evidenced API/case combinations marked supported.

## 11. Acceptance tests and evidence

| ID | Test | Expected result |
|---|---|---|
| ICON-01 | ICO 16/32/256 entries, PNG and DIB | Exact pixel samples and dimensions after decode |
| ICON-02 | ICO malformed size/offset/count | Fail cleanly, no OOB, leak or partial object publication |
| ICON-03 | DIB AND/XOR transparency, 32-bit alpha | Correct masking and alpha compositing |
| ICON-04 | PE with two RT_GROUP_ICON groups | Select right group/RT_ICON variants without executing PE |
| ICON-05 | Corrupt .rsrc RVA, directory cycle, bad language entry | Bounded deterministic error, no kernel panic |
| ICON-06 | `LoadIconW`, `LoadImageW`, `LookupIconIdFromDirectoryEx` | Correct resource and API semantics |
| ICON-07 | Shared `HICON` from `LoadIcon` / `LR_SHARED` | No destruction by caller, no cache UAF |
| ICON-08 | `CopyIcon` and `DestroyIcon` owned | Independent copy, deterministic resource release |
| ICON-09 | `GetIconInfo` | Valid GDI bitmaps returned and released correctly |
| ICON-10 | `DrawIconEx` with clipping and varying alpha | Expected framebuffer pixels, no out-of-bounds |
| ICON-11 | `WM_SETICON`, `WM_GETICON` | Correct window state and repaint |
| ICON-12 | `ExtractIconEx` / `SHGetFileInfo` | Real exe/file icons, fallback and error handling |
| ICON-13 | `ImageList_*` add/replace/remove/draw | Stable handles and documented ownership |
| ICON-14 | Explorer desktop/folder/taskbar | Real icons displayed in QEMU, not hardcoded rectangles |
| ICON-15 | Cache invalidation after file update | New image appears without stale references |
| ICON-16 | High DPI and multiple source sizes | Correct selection or documented scale fallback |
| ICON-17 | Repeated launch/terminate of Explorer and source modules | No leaked icon handles, GDI objects, PFN or surfaces |
| ICON-18 | Unreadable files/cross-user ACL | Denied access without metadata leakage |
| ICON-19 | Encoded image budget/fuzz corpus | Bounded failure without resource exhaustion |
| ICON-20 | QEMU boot regression | M4/graphics/Explorer existing checks remain valid; no tests silently skipped |

**Evidence:** record exact git revision, QEMU command line, guest output, screenshot/pixel probe, which PE and resource ID were used, supported flags and API matrix. `--check-explorer`, a linked DLL, a header export and hosted tests alone are not proof of rendering in QEMU. Prefer deterministic pixel probes with actual named fixture contents.

## 12. Dependencies and integration rules

IC0 depends on stable Ring 3 file access and a working bounded decoder. IC1 depends on validated PE resource loading, loader and exported DLL ABI. IC2 depends on GDI surfaces and Win32k presentation. IC3/IC4 depend on Explorer and Shell32 integration. Do not block all icon progress on completed GPU hardware acceleration, full WIC, TIFF, Unicode complex shaping, general multiuser logon or complete Windows shell COM. The minimal QEMU acceptance path can use a deliberately provisioned, permitted fixture file and a diagnostic user session while correctly documenting limits. Multiuser profile icon caches and shell policy are later integrations.

Do not let ancillary work on M4, memory, registry or filesystem expand into open-ended milestone completion. Implement only the missing narrow contracts that directly block icon IC0–IC4 and hand off unrelated issues to their canonical owner. If the current worktree contains concurrent modifications, make no destructive edits, preserve all changes, and report concrete conflicts.

## 13. Documentation and implementation deliverables

1. An up-to-date API ownership and status matrix in this document: User32, Kernel32 resource APIs, GDI, Shell32, Comctl32, Explorer.
2. Bounded image parsers, test assets, PE fixture with icon resources, resource extraction tests and a working `HICON` implementation.
3. Real Ring 3 DLL export integration and import-closure checks, not hosted-only implementations.
4. Graphics presentation and Explorer integration with pixel-verified QEMU evidence.
5. Asset provenance/third-party license manifest for any bundled Fluent icon files and build-time conversion scripts.
6. Regression and fuzz-test records, documented unsupported flags/API surface, update to `docs/KERNEL_INTEGRATION_STATUS.md` only after verified gains.

### Current implementation evidence (2026-11-14)

This matrix records the current source status; a linked export is not evidence of CPL3 use or icon pixels on scanout.

| Family / entrypoints | Owner and current bounded behavior | Evidence / gaps |
|---|---|---|
| ICO decode | `include/micant/um/subsystems/imaging/ico_decoder.hpp`: PNG and classic 1/4/8/24/32bpp BI_RGB DIB, size selection, scaling and bounded file access | Standalone host harness checks PNG/DIB, malformed offsets, alpha/channel bytes; no fuzz corpus yet |
| PE resources | `include/micant/um/native/pe.hpp`: file-backed resource tree, numeric group names, language fallback, `RT_GROUP_ICON`→`RT_ICON` numeric IDs | Host fixture decodes a PE resource without executing it; named group resources unsupported |
| User32 HICON | `kernel/ps/winnt_user32_gdi32_imm32_system32_abi.cpp`: owned handles, `CopyIcon`, `CopyImage(IMAGE_ICON, flags=0)`, `DestroyIcon`, `CreateIconFromResource[Ex]`, `LoadImageW` ICO `LR_LOADFROMFILE`, ASCII-only `LoadImageA`; icon/class window fields and `WM_SETICON/GETICON` | User32 PE links and boundary checks pass. `LoadIconA/W` exports deliberately fail until module-resource/stock-artwork support exists; `LR_SHARED`, nonzero `CopyImage` flags, `CreateIconIndirect`, `GetIconInfo[Ex]` unsupported |
| Drawing / class icons | Same User32 owner plus Win32k alpha source-over operation; `DrawIcon/DrawIconEx` supports `DI_IMAGE`/`DI_NORMAL`, class large/small icons and `Get/SetClassLongPtrA/W` | Pixel blend host checks passed. Latest full QEMU boot failed before the icon path at the second Explorer surface composition check; a separate runtime self-test passed. No CPL3 icon client or icon scanout pixel probe yet. Mask-only/compat flags, brush background and taskbar repaint remain open |
| Shell32 extraction | `tools/dlls/shell32_icon.cpp`, `shell32_boot`: A/W `ExtractIcon`, `ExtractIconEx`, `PrivateExtractIcons`, `SHGetFileInfo` for ASCII `.ico` paths and numeric PE group icons; owned HICON output | Built into QEMU artifact set and imports only User32/NTDLL. ANSI paths are limited to ASCII pending an ACP converter. No CPL3 caller yet; full QEMU currently fails in Explorer before shell icon use. Generic associations, folders and cache invalidation remain open |
| Comctl32 image lists | `tools/dlls/comctl32_icon.cpp`, `comctl32_boot`: bounded process-local handles and image storage, `ImageList_Create/Destroy/Add/AddIcon/ReplaceIcon/Draw/Remove/GetImageCount/GetIconSize/SetIconSize` through GDI/User32; unsupported mask/drag/serialization APIs fail | Target builds and passes its boundary check; QEMU runtime self-test passes, but no CPL3 call or image-list scanout proof yet. The hosted adapter accepts real HBITMAP inputs and rejects fake HICONs |
| Explorer / assets | Desktop loads cached 48 px PNGs for Computer, Documents, Network and Recycle Bin from `System/Icons/WhiteSur`; built-in glyphs remain the failure fallback. The boot image packages a curated WhiteSur GPL-3.0 asset subset and its notices. | `python3 qemu.py --headless` passed on 2026-11-15; `desktop-ready.ppm` shows all four raster icons on the desktop. Source: WhiteSur `73d8040da51a9ed74e47c7366e7e9ff437601a5c`; `scripts/import_whitesur_icons.py` reproducibly imports the allowlisted generic SVGs and rasterizes requested sizes. Explorer file-type associations, navigation/list icons, per-file extraction and live trash/device state remain open |

**Definition of done:** an unmodified Win32-style client can call the implemented subset of Windows icon APIs on actual icon-bearing files and see correct output through Explorer on the canonical MicaNT QEMU path, with independently tested ownership, failure behavior and no hardcoded icon substitute. Full parity means every declared supported API/flag/object lifetime is tested; do not use the term “complete Windows icon compatibility” before that matrix passes.

## 14. Public normative references

- Microsoft Learn, [About Icons](https://learn.microsoft.com/en-us/windows/win32/menurc/about-icons).
- Microsoft Learn, [Using Icons](https://learn.microsoft.com/en-us/windows/desktop/menurc/using-icons).
- Microsoft Learn, [LoadImageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadimagew).
- Microsoft Learn, [LookupIconIdFromDirectoryEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-lookupiconidfromdirectoryex).
- Microsoft Learn, [DestroyIcon](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroyicon).
- Microsoft, [Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons) and its upstream MIT LICENSE (verify exact pinned revision before redistribution).

The normative API documentation governs actual Windows behavior where this implementation plan's examples are incomplete. Record each intentional compatibility difference and avoid inferring guarantees from names alone.
