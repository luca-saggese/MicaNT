# User Mode Windows Cursor Subsystem Implementation Specification

## Purpose and acceptance boundary

This specification defines the implementation path for Win32 cursor support in
MicaNT's canonical AMD64 modular system. The target is an Explorer desktop in
Ring 3 using real CUR/ANI assets, mouse input, and the existing compositor. A
hosted test or a static cursor alone does not satisfy end-to-end acceptance.

## Ownership and Ring boundaries

* Ring 3 User32 owns public API validation, cursor handle lifetime, CUR/ANI
  parsing, PE cursor resource selection, animation scheduling, and theme asset
  loading. It uses the existing image decoder and PE/resource loader.
* The shared `micant/shared` ABI defines only fixed-width request and cursor
  metadata structures. It contains no kernel-private object types or pointers
  whose lifetime is not validated.
* Ring 0 Win32k owns per-interactive-desktop pointer coordinates, clipping,
  visibility count, active cursor identity/metadata, capture, hit testing, and
  mouse message routing. It validates caller ownership and secure-desktop access.
* The Ring 3 compositor owns software cursor rasterization as an independent
  overlay over the existing window surfaces. It damages the prior and next
  cursor bounds and never writes cursor pixels into application surfaces.
* VirtIO-GPU owns any device cursor command path. Hardware cursor mode is
  advertised only after device capability, queue ownership, completion, reset,
  and removal paths are verified; otherwise the compositor overlay is used.

## ABI and state model

Use the existing Win32k request/syscall path and compositor shared metadata;
do not create a second input service or kernel ABI. Requests carry bounded
coordinates, clip rectangle, visibility delta, cursor dimensions/hotspot, and
an owner-scoped cursor token. Pixel storage is copied or referenced through an
explicitly owned shared section with validated size and rundown. Win32k tracks
the pointer per interactive desktop, while User32 handles are per-process.
Window class cursors and thread-selected cursors retain references; active
desktop state retains its own reference. Process teardown, window destruction,
cursor replacement, and desktop switching release each reference exactly
once. Shared system handles cannot be destroyed by applications.

## Formats and resource loading

### CUR

Validate the ICONDIR header and CUR type, bound entry count and total decoded
bytes, validate every offset/length with checked arithmetic, and select the
best entry for requested pixel size/DPI. Preserve entry hotspot coordinates
and reject hotspots outside the decoded image. Decode PNG payloads through the
existing Ring 3 image decoder. Decode DIB payloads with bounded dimensions,
supported bit depths, palette and row-stride checks; apply AND/XOR masks and
alpha semantics, returning premultiplied BGRA8. Truncated, overlapping-invalid,
overflowing, or unsupported payloads fail without leaking allocations.

### ANI

Parse RIFF ACON chunks with padded-length validation and bounded traversal.
Validate `anih`, `rate`, `seq`, and `icon` chunks, frame/step limits, sequence
indices, frame CUR payloads, and per-step timing. Unknown chunks are skipped
only after their declared bounds are validated. A timer/message-loop callback
advances the current step without busy-waiting and can be cancelled on handle
destruction, process teardown, replacement, or theme change.

### PE resources

Use the canonical PE resource loader for RT_CURSOR and RT_GROUP_CURSOR,
including numeric and named identifiers. Group entries select the closest
size/depth, resolve the referenced cursor resource, associate its hotspot,
and retain the module/resource lifetime under the existing loader ownership
rules. Do not duplicate resource directory traversal.

## User32 API contract

Implement ABI-correct A/W exports for LoadCursor, LoadCursorFromFile,
LoadImage(IMAGE_CURSOR), CreateCursor, CreateIconIndirect for cursor objects,
CopyCursor, DestroyCursor, SetCursor/GetCursor, ShowCursor, GetCursorInfo,
SetSystemCursor, Get/SetCursorPos, ClipCursor/GetClipCursor, SetCapture and
ReleaseCapture. Add only documented system-parameter actions supported by the
configuration owner. Return failure and the documented error when an operation
is unsupported or invalid; never fabricate handles or success. Shared cursor
destruction is a no-op per the public contract, and owned handles are validated
against process ownership and generation to prevent stale-handle reuse.

## Input, hit testing, and messages

The existing PS/2 and USB HID paths continue to converge on canonical Win32k
mouse input. Win32k updates screen coordinates and clip bounds, honors capture,
performs topmost-window and nonclient hit testing, and routes mouse messages.
It sends WM_SETCURSOR with the correct hit-test/context so the owning thread can
choose class, arrow, I-beam, resize, move, link, help, or wait cursors. The
cursor hotspot is the event point. Capture changes and window/session rundown
clear stale cursor associations. Unprivileged processes cannot alter the
secure logon desktop pointer.

## Composition and device cursor

Software mode composites premultiplied BGRA with hotspot-adjusted placement,
screen clipping, and old/new dirty rectangles. Recomposition restores the
underlying desktop from canonical surfaces, avoiding trails and full desktop
redraws. Cursor movement is independent of Explorer.

Hardware mode is an optional optimization. Inspect VirtIO-GPU cursor resource,
update, and move commands and the active device profile. Reuse the driver's
existing request queues, DMA/buffer ownership, fences and reset paths. Validate
maximum dimensions and completion/error behavior. Fall back to software on
unsupported capability, timeout, reset, or removal, preserving the active
image and hotspot.

## Themes, DPI, and persistence

Provide a replaceable per-user theme mapping IDC aliases to cursor files,
including arrow, I-beam, cross, horizontal/vertical/diagonal resize, move, link,
help, unavailable, up, pen, busy, and working. Mini Mouse Cursor is the initial
theme and must be imported with exact upstream SHA, original MIT license,
copyright, source URL, and an inventory of selected assets. Do not embed assets
in Explorer. Resolve image size from effective DPI and system metrics, preserve
hotspots when scaling, and use explicit fallback mappings if a theme file is
missing. Persist settings through the existing user configuration owner when
available; bootstrap uses a built-in safe arrow until then. Theme replacement
invalidates cache entries and releases old resources after active users drain.

## Security and failure handling

CUR/ANI files are untrusted. Enforce size, dimensions, frame, chunk, arithmetic,
and allocation limits before allocation or decode. Reject malformed offsets,
hotspots, sequences, and payloads. Validate all handles and cross-process
ownership; use rundown for active cursor references. Restrict secure desktop
control to its owner. Keep file parsing in Ring 3 and share only validated,
bounded image data with the compositor/kernel. Every failed operation leaves
previous cursor state intact and releases temporary resources.

## Test plan and acceptance

Unit tests cover CUR DIB/PNG, multi-entry selection, alpha and AND/XOR masks,
hotspots, malformed/truncated inputs, ANI rates/sequences/unknown chunks,
frame limits, handle copy/destroy/shared ownership, visibility, clipping,
position, replacement, process teardown, and malformed ABI buffers. API tests
must call the actual User32 exports. Mini Mouse files are real fixtures.

QEMU acceptance uses the canonical UEFI AMD64 modular kernel, real Ring 3
Explorer, and filesystem-staged theme assets. Record source SHA, build/QEMU
commands, serial logs, QMP input and pixel/screenshot evidence. Verify arrow,
movement, hotspot, text, resize, move, link, animation, custom cursor lifetime,
surface integrity, desktop security where present, and regressions. Report
hardware cursor only with device command/completion evidence; otherwise record
the verified software fallback and its device limitation. Each criterion is
reported separately as implemented, linked, or executed and verified.

## Phases and dependencies

1. Inventory current User32, Win32k, input, compositor, image, PE resources,
   Explorer, and VirtIO-GPU; establish baseline and preserve existing changes.
2. Define shared ABI and owner-scoped cursor lifecycle; expose real Win32k
   position/state services and connect the initial User32 API path.
3. Implement bounded CUR DIB/PNG decoding atop the existing image pipeline;
   add malformed-input and real-fixture tests.
4. Implement handle semantics and ABI-correct User32 loading, selection,
   visibility, position, clipping, and cleanup APIs.
5. Implement ANI parsing and timer-driven sequence playback; then PE cursor
   resource loading via the canonical loader.
6. Integrate mouse hit testing, capture, WM_SETCURSOR, and context cursor
   selection across windows and controls.
7. Add compositor cursor overlay and dirty-region restoration; prove no trails
   or surface corruption under movement and resize.
8. Import and attribute selected Mini Mouse CUR/ANI assets, make theme mapping
   replaceable, and add DPI/configuration behavior with bootstrap fallback.
9. Evaluate and, if supported, implement VirtIO-GPU hardware cursor commands
   using canonical queue ownership; otherwise record evidence for software mode.
10. Run API/lifecycle regressions, then full QEMU Explorer matrix and record
    evidence. Continue until all attainable acceptance criteria are closed;
    unsupported device capability remains an explicit limitation, not a pass.

## Dependencies and non-goals

Explorer depends on working window classes, message delivery, filesystem asset
loading, and compositor presentation. Cursor implementation depends on the
existing native syscall/windowing request transport, image decoder, PE resource
loader, timer/message loop, and Userenv/Cm configuration services as they
become available. This work does not reactivate `old/`, add a second compositor
or input manager, add a new general kernel ABI, or claim Windows-wide cursor
support beyond implemented and verified behavior.
