# MicaNT — User-Mode Image Codecs and Desktop Wallpaper Specification

**Document:** `docs/USERMODE_IMAGE_CODEC_AND_WALLPAPER_SPEC.md`  
**Status:** Proposed implementation specification; no codec or wallpaper acceptance is implied  
**Platform:** AMD64, C++23, modular NT-compatible MicaNT  
**Primary owners:** Ring 3 imaging runtime / `windowscodecs.dll`, Explorer shell, user profile/settings service, compositor; canonical Io/FsRtl, Mm, Ob, Ps, Se and graphics service for kernel facilities  
**Initial target:** A genuine JPEG file on the QEMU guest filesystem becomes the desktop wallpaper behind real Explorer windows, using a decoder and compositor running in Ring 3.

## 1. Purpose

Provide secure, reusable decoding of common raster image formats and integrate the resulting surfaces with the MicaNT desktop. The initial product requirement is to select a JPEG image as desktop wallpaper and persist the choice for the current user. The same subsystem shall later serve PNG/BMP/TIFF, icons, thumbnails, image viewers, GDI+ and Windows Imaging Component (WIC) clients. Do not build a one-off JPEG parser into Explorer or a decoder into the kernel.

**Definition of a genuine result:** Explorer, executing as a real CPL3 process, opens an image through the canonical guest filesystem, uses a real user-mode decoder to produce verified pixels, then submits a wallpaper surface to the compositor. A hardcoded image, host-side image conversion, diagnostic kernel drawing, exported API name, or successful link alone does not meet this requirement.

## 2. Baseline and evidence

The repository contains `include/micant/um/subsystems/winuser/gdiplus.hpp`, `include/micant/um/subsystems/graphics/d2d1.hpp`, user-mode software GDI DIB/bitmap/`SRCCOPY` primitives and a limited VirtIO-GPU 2D scanout path. The available integration record describes software composition/presentation, not proof of a running Explorer, an operational WIC runtime, a JPEG decoder, or hardware-accelerated blending. `gdiplus.hpp` currently includes private `micant/km/hal/bootvid.hpp`; the implementation must correct any such Ring 3/Ring 0 boundary violations rather than propagate them.

At task start re-check `AGENTS.md`, `docs/KERNEL_ARCHITECTURE.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, `docs/KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`, the current build graph and actual sources. Reclassify every component as *declared*, *compiled*, *linked*, *QEMU executed* or *physical-hardware verified*. The statements above are a baseline for investigation, not a substitute for current source inspection.

## 3. Architectural invariants

1. **Decode in Ring 3.** JPEG/PNG/BMP/TIFF bitstreams are untrusted input. Parsers, entropy decoders, metadata, image color conversion, thumbnails and scaling belong outside the kernel.
2. **One canonical I/O and display path.** Ring 3 uses NT file APIs backed by Io/FsRtl and returns ordinary user-mode image buffers or shared Mm sections to the existing compositor. The graphics service/miniport owns final presentation, DMA, scanout and device fences.
3. **Do not conflate GPU presentation with accelerated decoding or composition.** VirtIO-GPU 2D scanout is not evidence that JPEG decoding, resizing or alpha blending occurred on a GPU. CPU decoding is an acceptable initial implementation.
4. **One reusable imaging core.** Format-specific decoders plug into a small internal imaging service; WIC, GDI+, Explorer and other applications may all reuse it. Avoid duplicate parsers and incompatible pixel/metadata conventions.
5. **Support explicit capability reporting.** Unsupported format variations must fail with specific errors. Never return success with synthesized pixels or omit failed decode paths.
6. **No monolithic kernel resurrection.** Follow `AGENTS.md`; do not compile or link archived `old/` paths into the canonical build.

## 4. End-to-end data flow

```text
Guest image file (e.g. C:\Users\TestUser\Pictures\wallpaper.jpg)
       |
       v
Ring 3 Win32/NT file APIs -> canonical Io/FsRtl and filesystem
       |
       v
Ring 3 image service: sniff -> codec -> decode -> color transform -> BGRA8
       |
       v
Ring 3 wallpaper manager (per-user configuration, scale/crop, cache)
       |
       v
Ring 3 compositor (background surface + window surfaces + cursor policy)
       |
       v
Canonical graphics service -> VirtIO-GPU 2D scanout / GOP fallback
                                          [future: verified GPU blend backend]
```

No direct application MMIO or unrestricted scanout mapping. Each surface and file handle has an explicit owner and release path.

## 5. Internal imaging contract

Design a small private, versioned, C-compatible boundary where binaries need to communicate; keep C++ convenience wrappers internal to their modules. Representative concepts, not prescribed ABI symbols:

- `ImageProbe(input)` identifies a format from validated magic/signature and reports confidence, not just the filename extension.
- `ImageDecoderOpen(stream, limits)` returns a decoder object and immutable frame metadata.
- `ImageGetFrameCount`, `ImageGetFrameInfo`, `ImageReadMetadata` have bounded results and explicit unsupported statuses.
- `ImageDecodeFrame(frame, region, destination, stride, pixelFormat)` writes only within a validated caller-owned buffer or returned surface.
- `ImageClose` releases all temporary buffers and stream references, including on failures.
- Optional `ImageScale/Transform` runs after decode; keep image decode separate from presentation.

**Common descriptor:** width, height, frame count, orientation, source color model, bits/channel, alpha semantics, output format, byte stride, buffer byte length, optional ICC profile and metadata length. Use checked integer arithmetic on all products, offsets, dimensions, strides and allocations. Define an output baseline of **BGRA8** with a documented alpha convention (for instance, premultiplied alpha for compositing); ensure opaque JPEG always has full alpha. For more advanced images, preserve enough metadata to avoid silently misinterpreting orientation or colors.

Implement explicit errors for unknown format, unsupported feature, malformed stream, truncated input, too-large dimensions, allocation exhaustion, access denied, cancelled decode and I/O failure. Handle image truncation between file open and read.

## 6. Codec requirements and order

The shared Ring-3 imaging core also provides **encoding** for BMP, PNG and
baseline JPEG. Encoding consumes the common premultiplied BGRA8 frame and
publishes a bounded, allocator-owned byte buffer only after the complete
container has been produced. BMP discards alpha explicitly; PNG preserves it;
JPEG composites premultiplied pixels against a documented opaque background
before color conversion. The selected encoder must be explicit (or resolved
from a validated encoder identifier), never inferred from a destination file
extension. Invalid dimensions, quality values, allocation exhaustion, and
output-limit exhaustion return errors without replacing the caller's prior
output. File saves use the caller's access-controlled NT handle and report
partial writes and flush failures; they must not report success after only an
in-memory encode.

The initial encoder slice supports BMP 24-bpp, PNG 8-bit RGB/RGBA, and JPEG
8-bit baseline sequential. Progressive JPEG and unsupported pixel/container
variants return an explicit unsupported status. Encoded output is subject to
the same configured intermediate and encoded-byte budgets as decode input.

| Codec | First interoperable slice | Subsequent coverage | Notes |
|---|---|---|---|
| BMP | Common uncompressed 24/32-bpp DIB and validated headers | Palettes, bitfields, RLE variants where useful | Validate row padding, bottom-up/top-down layout, overflow and offsets |
| JPEG/JPG | Baseline 8-bit sequential grayscale and YCbCr; valid color conversion to BGRA8 | Progressive JPEG, EXIF orientation, ICC, CMYK policy, subsampling variants | Distinguish unsupported from corrupt; baseline JPEG required for first wallpaper |
| PNG | Common 8-bit RGB/RGBA, correct filters and lossless inflate | Palette, grayscale, 16-bit down-conversion, interlacing, ICC/gamma, APNG policy | Bounds on compressed/uncompressed sizes and CRC behavior |
| TIFF | Not required for first wallpaper | Selected classic TIFF single/multipage with explicitly advertised compressions; later BigTIFF if justified | Offset/IFD graph, decompression and multipage limits; do not claim universal TIFF support |

GIF, WebP, ICO, HEIF/HEIC and AVIF are outside first acceptance unless separately approved. Files can have deceptive extensions; codec selection must validate the stream signature.

### Codec sourcing policy

Before importing third-party decoder code, evaluate license, notice obligations, provenance, allocation model, threading assumptions, POSIX/host CRT calls, and freestanding/NT Ring 3 portability. Prefer a well-maintained, auditable implementation over homegrown full-format parsers when integration is feasible. Preserve applicable licenses and attribution. Do not import binary-only Windows DLLs, proprietary implementations, or Linux kernel decoder modules as a shortcut. Keep all third-party parsing code confined to Ring 3.

## 7. WIC compatibility boundary

Implement `windowscodecs.dll` progressively as the compatibility front end, not as an all-at-once COM reimplementation prerequisite for wallpaper.

Initial WIC-compatible slice, guided by win32metadata and public ABI contracts:

- Imaging factory creation and refcounted COM-style object lifetimes.
- Decoder discovery by container format and actual bitstream.
- Decoder creation from a validated file/stream where supported.
- Frame count, size, pixel format and copy-pixels operations.
- Format converter for supported inputs to BGRA8.
- Meaningful HRESULT, lifetime, thread/apartment and stream semantics for each implemented interface.

Follow-up: `IWICBitmapScaler`, metadata readers, palette conversion, color contexts/ICC, and relevant `IWICBitmapSource` interfaces. Export compatibility does not imply all WIC capabilities; maintain an explicit feature table and supported HRESULTs. Avoid introducing an entire OLE/COM subsystem in kernel mode.

## 8. GDI+ and Direct2D integration

Route supported `GdipLoadImageFromFile`, `GdipCreateBitmapFromFile`, image properties and associated raster operations through the shared Ring 3 decoder. Connect decoded surfaces to the existing software GDI DIB path where appropriate. Do not create a duplicate JPEG decoder under GDI+.

Direct2D should consume an image surface through its user-mode graphics abstraction when that path is ready. There must be no user-mode inclusion of private `micant/km/` headers; enforce the repository's boundary checker and place shared POD layout/contracts only in sanctioned shared headers. A successful header compile or API registration does not count as image decode acceptance.

## 9. Wallpaper manager

The shell owns policy and per-user wallpaper selection; the compositor owns presentation of an immutable or versioned wallpaper surface. Prefer a dedicated user-mode wallpaper manager within Explorer or an existing shell service rather than embedding policy in Win32k or the miniport.

Requirements:

- Open a user-selected image using the authenticated user's token; no privileged bypass of filesystem ACLs.
- Decode once on selection, session initialization, file change or display-configuration change; cache the converted surface and invalidate it when needed.
- Support **Fill**, **Fit**, **Stretch**, **Center** and **Tile** with deterministic clipping, aspect-ratio handling and background color.
- Detect display geometry (width, height, stride, pixel format) through the graphics service; never assume a fixed 1920×1080 resolution.
- Submit wallpaper behind normal application windows, not on top of them, and preserve window Z-order and focus independently.
- Make an update atomic at the compositor boundary: keep the prior good wallpaper until the replacement is fully decoded and validated.
- Recover from deleted, inaccessible, malformed or oversized images by falling back to a known-safe solid color or built-in image, without crashing Explorer.
- Persist the setting **per user** through the canonical profile/registry configuration path when it exists. Until profile persistence is available, support an explicitly temporary session-only setting and do not claim persistence.
- Support same-user re-login and different-user wallpaper isolation. Never use a single global hardcoded `C:\Users\admin` path.
- Do not use a privileged service to read arbitrary files on behalf of the caller without access checks and file-handle ownership.

**Illustrative setting model (not a proposed registry ABI):** `{image_path, style, background_color, version}` scoped to the authenticated user. Validate/normalize paths through existing NT facilities, not ad-hoc string-based security decisions.

## 10. Scaling and memory policy

Use overflow-safe image dimensions and compute the output surface budget before allocating. Limit decoded pixels and intermediate allocation according to a configurable per-process/resource policy and available memory, not one hidden format-specific constant. Treat huge or malicious files as failures. Image bounds policies and resource quotas are security controls; they must not inherit unrelated bootstrap address-space caps.

Keep decode buffers private until valid. Decode asynchronously or on a worker thread when available so slow files do not block the input loop. Cancellation and process/session termination must release file handles, temporary allocations, surfaces and wait objects. Cache one display-sized wallpaper surface per active display/style plus small bounded metadata, rather than decoding the original for every repaint. A renderer can use software bilinear scaling initially; record future hardware-accelerated scaling as a separate measurable improvement.

## 11. Security and trust boundaries

Image inputs are adversarial. The codec implementation must cover:

- Length-delimited reads; malformed signatures, chunks, markers, IFD chains and cyclic offsets.
- Checked allocation sizes, row strides, integer arithmetic, file offsets and dimension products.
- Bounded memory/time work where feasible; guard against decompression bombs and pathological metadata.
- Strict frame count and metadata caps; limits must be documented and appropriately reported.
- No unchecked parsing into kernel buffers; no caller-controlled device commands or direct DMA from decoder state.
- Process separation for optional higher-risk codec helpers if the runtime supports it; otherwise maintain a narrow, audited Ring 3 boundary.
- Reliable cleanup after parser exceptions/errors, truncated reads, cancellation, OOM and failed presentation.
- Inappropriate image files must not produce synthetic success, kernel panic or durable corruption.
- Per-user ACL enforcement, shared-surface access control, and correct handling of logout and process death.

Add fuzz targets for the format parsers in a hosted harness while verifying the identical parser logic used by the guest. Fuzz success is not a substitute for guest-side integration tests.

## 12. Detailed implementation stages

### I0 — Core decoder and first JPEG wallpaper

**I0.1** Audit loader/Explorer filesystem APIs, graphics buffer interface, user-mode DLL dependencies and existing GDI paths. Record any actual blockers and assign owners.

**I0.2** Define common image descriptors, checked buffer helpers, a format probe and decoder lifecycle. Establish deterministic tests using small known pixel fixtures.

**I0.3** Implement or integrate a genuine **baseline JPEG** decoder in Ring 3, including grayscale/YCbCr conversion, error reporting and bounds checking. Produce a BGRA8 buffer and verify pixel results against independent fixture references within reasonable rounding tolerance.

**I0.4** Implement simple BMP and PNG decoders (or integrate audited libraries) as an independent extension of the same decoder interface; test opaque, transparent and malformed cases. Do not block first JPEG wallpaper on advanced TIFF.

**I0.5** Connect the decoder to a guest file opened by Explorer or a minimal CPL3 test program through the canonical I/O path. No host pre-conversion or embedded bitmap.

**I0.6** Create the wallpaper surface, apply Fill/Fit/Center at minimum, and present behind the desktop/windows through the existing compositor and VirtIO-GPU scanout. Keep a solid-color fallback.

**I0.7** Verify a real JPEG appears in QEMU and survives repeated redraw and window overlap. Distinguish a diagnostic CPL3 client from real Explorer if Explorer's startup remains blocked by unrelated prerequisites.

**I0 acceptance:** real guest `.jpg` bytes -> Ring 3 decoding -> verified BGRA8 pixels -> compositor wallpaper -> QEMU scanout; unsupported/malformed inputs fail safely, and existing graphics and kernel boot tests remain valid.

### I1 — Wallpaper lifecycle and user settings

**I1.1** Implement all five modes: Fill, Fit, Stretch, Center, Tile, including aspect ratio, clipping, scaling, and multi-resolution tests.

**I1.2** Implement atomic replace, cache invalidation, display mode change, file unreadable/deleted and OOM fallback.

**I1.3** Use the session's user token to access wallpaper data. Persist choice in canonical per-user profile/registry only when those services operate; otherwise mark setting ephemeral.

**I1.4** Ensure logout/session termination cleans all image/surface references and a second user cannot access the first user's private wallpaper settings.

**I1 acceptance:** image change visible without Explorer restart; correct style and per-session ownership; per-user persistence tested after reboot where storage/profile support exists.

### I2 — WIC and GDI+ reusable interfaces

**I2.1** Implement the minimal public `windowscodecs.dll` factory/decoder/frame/format-conversion operations needed by a concrete Ring 3 application.

**I2.2** Wire GDI+ image creation/loading to the same imaging core. Implement clear HRESULT/GDI+ status translations.

**I2.3** Ensure DIB/Direct2D consumers receive correct stride, alpha and lifetime information; validate that user-mode modules do not include `micant/km/` headers.

**I2.4** Add one non-Explorer CPL3 WIC/GDI+ sample proving decode API reuse; exercise both correct and malformed files.

**I2 acceptance:** supported image formats load via a real WIC/GDI+ client and yield the same expected pixels as the internal decoder, with correct cleanup and failures.

### I3 — Extended formats and quality

**I3.1** Add progressive JPEG and common metadata/orientation requirements; define handling of ICC, CMYK and EXIF rather than silently outputting incorrect colors.

**I3.2** Extend PNG to palette/interlaced/16-bit where warranted, with documented gamma/color behavior.

**I3.3** Add bounded TIFF decoding of clearly specified compressions and single/multipage access. Test endian variants, recursive/cyclic directory links and malformed offsets.

**I3.4** Expand scaler quality, multi-monitor behavior if supported, thumbnails and reusable image caching. Hardware-assisted scaling is optional and cannot be claimed without real accelerator evidence.

**I3 acceptance:** feature matrix identifies exact supported subformats and provides both hosted and QEMU evidence; unsupported cases fail explicitly and safely.

## 13. Acceptance and regression matrix

| ID | Test | Required evidence |
|---|---|---|
| IMG-01 | Decode baseline JPEG fixture | Correct dimensions, pixel checksum/tolerance, BGRA8 alpha |
| IMG-02 | Decode PNG with alpha and BMP 24/32-bpp | Correct stride/channel order/alpha and pixel references |
| IMG-03 | Wrong extension and unknown signature | Probe uses content, returns accurate format/error |
| IMG-04 | Truncated JPEG/PNG, corrupt headers, invalid offsets | Error, no overwrite, no leak, no kernel crash |
| IMG-05 | Huge dimension/decompression-bomb fixture | Bounded rejection, no integer overflow |
| IMG-06 | Pixel scaling: Fill/Fit/Stretch/Center/Tile | Deterministic geometry and clipping in software tests |
| IMG-07 | CPL3 opens real JPEG from FAT32/guest filesystem | File read/close through NT/Io/FsRtl, decoded pixel evidence |
| IMG-08 | Explorer wallpaper and overlapping windows | QEMU screenshot and serial/trace evidence from real processes |
| IMG-09 | Repeated wallpaper switches + redraw | No surface, PFN, handle or decoder leaks |
| IMG-10 | Missing/inaccessible file and decoder failure | Previous wallpaper or safe fallback preserved |
| IMG-11 | User A/B wallpaper and ACL isolation | B cannot read A's private file or inherit A's settings |
| IMG-12 | WIC/GDI+ client load | Actual API call, correct pixels and failure semantics |
| IMG-13 | TIFF supported subset and hostile fixtures | Multipage/compression-specific evidence; unsupported explicit |
| IMG-14 | Device fallback and accelerated claims | VirtIO 2D software path works; hardware claims require separate proof |

Record test commands, machine/QEMU configuration, input hashes, commit SHA, serial logs, expected and observed results. Do not claim a step *verified in QEMU* if only compiled or passed hosted tests. A headless screenshot/pixel check must prove the intended **wallpaper** rather than a preexisting diagnostic desktop.

## 14. Integration boundaries and dependencies

- **Io/FsRtl:** canonical filesystem; no new filesystem implementation. Start with a file on guest FAT32 and extend to other filesystem providers automatically through NT file I/O.
- **Ps/Mm/Ob/Se:** own process, address space, handles, section-backed surfaces and permissions. No private image-parser memory manager.
- **Explorer/Win32k/compositor:** presentation and per-user desktop semantics; preserve Z-order and window interaction.
- **Graphics miniport:** owns scanout and eventual accelerated backend; decoders do not talk to GPU registers.
- **Userenv/Cm:** optional dependency for genuine per-user persistence; do not fake storage until it is implemented.
- **M4/M6/M7/M8 and graphical startup:** if missing facilities block Explorer, use a small CPL3 decode-and-present fixture to establish imaging independently, then repeat end-to-end with Explorer once available. Do not mislabel the fixture as Explorer.
- **UnaryFS:** no dependency for v1; optional later as user/app-specific storage provider below the file API.

## 15. Performance requirements

Performance targets are design goals, not current measurements. Track cold/warm wallpaper load latency, decode time, scaling time, peak decode/converted memory, image cache hits, frame presentation latency, damaged region count and compositor wakeups. An unchanged wallpaper must not be re-decoded or recopied on every window repaint. Prefer immutable per-display BGRA8 wallpaper surface caching and dirty-region presentation. Preserve responsiveness of input and other windows when decoding a large image.

No GPU speedup shall be inferred from accelerated scanout. Validate any future GPU scaler/blend via actual device command submission/fence completion and compare quality and timing against the software baseline.

## 16. Delivery and definition of done

- Integrate changes through the canonical build, user-mode DLL and application packaging; don't add host-only implementation masquerading as guest functionality.
- Keep a traceable list of modified files, exact supported format variants, tests and observed results.
- Update `docs/KERNEL_INTEGRATION_STATUS.md` only with achieved evidence; don't mark Explorer, WIC, TIFF or GPU acceleration complete because the specification or headers exist.
- Never commit or overwrite unrelated work without authorization; follow `AGENTS.md` and the repository's applicable collaboration rules.

**First meaningful completion:** a genuine JPEG file read from a QEMU guest filesystem is decoded by Ring 3 code and displayed as the wallpaper of a real Explorer desktop, preserving interaction with foreground windows. If Explorer is blocked, a real CPL3 imaging test may verify the decoder independently, but the Explorer wallpaper goal remains open.

**Full scope completion:** I0–I3 supported variants, user-profile persistence, explicit WIC/GDI+ interoperability, fault-tolerant image parsing, and QEMU acceptance have all been demonstrated; optional GPU acceleration is tracked separately.
