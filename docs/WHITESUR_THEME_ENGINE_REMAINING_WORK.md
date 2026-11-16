# WhiteSur Theme Engine — Work Tracker

This is the execution checklist for completing the requirements in
[`USERMODE_GTK_VISUAL_STYLES_THEME_ENGINE_SPEC.md`](./USERMODE_GTK_VISUAL_STYLES_THEME_ENGINE_SPEC.md).
The specification defines contracts and acceptance; this file records progress
and exact evidence. A checkbox means the requirement has implementation and
the evidence named here, not just declarations or a successful compile.

## Current baseline

- [x] UxTheme has generation-tagged process-local handles and real
  snapshot-backed style queries, including color, margins, sizes and content
  extents.
- [x] GDI brush/fill operations and UxTheme rendering support clipped opaque,
  rounded and translucent parts on selected DIB-backed memory DCs.
- [x] Explorer exercises the UxTheme toolbar and rounded EDIT drawing paths in
  QEMU and compares DIB pixels with expected colors.
- [x] Explorer can select packaged Light, Dark and Light Purple profiles in
  the current session. Win32k publishes the new snapshot and sends
  `WM_THEMECHANGED` to active windows.
- [x] The acceptance specification requires moving windows by their captions
  and resizing from supported edges/corners, including after a theme switch.
- [x] Win32k now starts pointer move/resize gestures from its caption and edge/
  corner hit tests, updates the window rectangle during captured mouse motion,
  and queues `WM_MOVE`/`WM_SIZE`; Explorer rebuilds its DIB surface on resize.
  QEMU recorded caption drag and all four edges/eight corners. The minimum
  outer size now preserves a 160x100 client, and Explorer rebuilt its DIB at
  both clamps. Caption hover/pressed pixel states, maximize/restore,
  minimize/taskbar-restore, close, activation, cleanup and reopen also passed
  in `/private/tmp/micant-theme-qemu-nonclient-full`.
- [x] Fixed the live Light-to-Dark revert in commit `e0112a6`: Explorer now
  reloads persisted `active.profile` for `WM_SETTINGCHANGE`, while
  `WM_THEMECHANGED` redraws the snapshot already published by the selector.
  Previously, the latter reloaded persisted Light over the session Dark
  snapshot.
- [x] Clean QEMU `--verify-input` at commit `708051a` passed the Light-to-Dark
  titlebar pixel assertion, then dragged and edge-resized the still-open
  window. Evidence: `/private/tmp/micant-theme-clean-708051a-run`, including
  `live-window-light.ppm`, `theme-dark.ppm`,
  `post-switch-caption-drag.ppm`, and `post-switch-edge-resize.ppm`. The clean
  manifest records commit `708051ad990dadf5882e15a8cac6e4accfaf9ab6` and
  `working_tree_dirty: false`; baremetal target and native user-mode boundary
  checks passed. The sampled titlebar changed from `(250,250,250)` to
  `(54,54,54)`.
- [x] The integrated `python3 qemu.py --headless` build and `--verify-input`
  run passed with a stable serial log after the Win32k client-coordinate fix.
  It covers Explorer theme pixels, parent navigation, caption actions, all
  resize edges/corners and minimum client size, three reopen cycles, and a live
  Light-to-Dark switch followed by move/resize. Evidence is in
  `/private/tmp/micant-theme-client-coordinate-fix-reopen`; the per-feature
  historical entries below retain earlier runs that stopped before this fix.

## Remaining acceptance work

### 1. UxTheme and GDI behavior

- [x] `EndBufferedAnimation` no longer reports success for the unsupported
  animation path: the real `uxtheme.dll` export returns `E_NOTIMPL`. Explorer
  called the export in Ring 3 and logged the expected status in
  `/private/tmp/micant-uxtheme-failure-contract-run/serial.log`; QEMU input,
  window geometry, cleanup and stable boot passed in the same run. This closes
  one fabricated-success case, not the full export audit.
- [x] Unsupported `BeginBufferedAnimation` clears both optional HDC outputs
  before returning null. Explorer calls the freestanding DLL export with
  sentinel outputs in Ring 3; QEMU logged the cleared outputs together with
  `E_NOTIMPL` for the unsupported end/stop operations in
  `/private/tmp/micant-uxtheme-buffer-output-run`.
- [x] `BufferedPaintStopAllAnimations` now returns `E_NOTIMPL` and is exported
  by the actual freestanding DLL. `GetThemeMargins` clears its output on
  failure; `OpenThemeData`/`IsThemeActive` require a fresh valid Win32k
  snapshot; transparency includes style opacity. Explorer's QEMU probe logged
  correct margin-output and unsupported-animation statuses at
  `/private/tmp/micant-uxtheme-query-failure-contract-run-2/serial.log`.
  That run built/linked the baremetal target and real DLL but timed out in the
  pre-desktop animated-cursor sleep path before the interactive suite; those
  extra query changes are not fully QEMU verified yet.
- [x] The freestanding `uxtheme.dll` now exports every name in the existing
  UxTheme export registry and ordinal 47, forwarding each to its bounded
  implementation or explicit unsupported result. The ABI audit corrected
  ordinal 47 to `DrawThemeBackgroundEx(HTHEME,HDC,int,int,RECT*,DTBGOPTS*)`;
  the standard and `DTBG_CLIPRECT` paths are now implemented and verified;
  other defined option flags fail explicitly. `qemu.py
  --check-explorer` passed, and `llvm-readobj --coff-exports` confirmed its
  name and ordinal. An earlier full run passed UxTheme/GDI pixels but stopped
  at the maximize caption hit-target assertion; the later integrated run
  listed in Current baseline passed the complete input suite.
- [x] Audited every freestanding UxTheme export wrapper against the local
  public/ReactOS declarations, including ordinal 47; no parameter-count or
  calling-convention mismatch was found. Bounded implementations and explicit
  `E_NOTIMPL`/null/false paths are listed in `uxtheme.hpp`.
  `GetThemePartSize` supports only `TS_MIN`; other variants remain unsupported.
  `GetThemePropertyOrigin` also explicitly returns `E_NOTIMPL` and clears its
  output; Explorer verified the status and sentinel clearing through the real
  DLL import in the latest QEMU run.
  The real import/export probe and the integrated QEMU run passed. The audit
  also found that the process-local handle table has no lock, so concurrent
  calls still need synchronization if UxTheme is used from multiple threads.
- [x] `GetThemeSysColor` maps supported system-color IDs to the corresponding
  foreground/background/border values in the pinned theme snapshot; unmapped
  IDs and stale handles return `CLR_INVALID`. Explorer checked `COLOR_BTNFACE`
  through both a real BUTTON theme handle and the global null-handle path, plus
  an unmapped ID. The QEMU marker repeated after Light-to-Dark runtime
  replacement in `/private/tmp/micant-theme-syscolor-run/serial.log`.
- [x] The QEMU import-path matrix covers invalid/stale handles, clipped and
  rounded drawing, alpha, handle-capacity exhaustion/cleanup and GDI dispatch
  failure. These probes all passed in the integrated run
  `/private/tmp/micant-theme-client-coordinate-fix-reopen`.
- [x] `GetThemeBool(TMT_TRANSPARENT)` now reports effective background
  transparency from CSS alpha and theme opacity; other boolean properties and
  parts still return `E_NOTIMPL` after clearing the output. Explorer queried
  the real DLL for the active button style and an unsupported property. QEMU
  logged `UxTheme TMT_TRANSPARENT follows alpha and opacity` in
  `/private/tmp/micant-theme-uxtheme-transparent-bool-run/serial.log`. The
  baremetal build and focused probe passed; the enclosing input run later
  stopped at the parallel parent-folder navigation assertion.
- [x] `DrawThemeBackgroundEx` supports null options and `DTBG_CLIPRECT` by
  delegating to the same bounded GDI renderer. Known unimplemented flags return
  `E_NOTIMPL`; malformed size and unknown bits return `E_INVALIDARG`. Explorer
  checks a clipped and an untouched pixel plus both error paths through the real
  DLL export. QEMU logged `DrawThemeBackgroundEx clipping and errors verified`
  in `/private/tmp/micant-theme-drawthemebackgroundex-errors-run/serial.log`;
  build and focused pixel/error checks passed before the enclosing
  parent-folder input assertion failed.
- [x] `GetThemeEnumValue(TMT_BGTYPE)` reports `BT_BORDERFILL` only for a
  supported style carrying a background or border; unsupported properties
  clear their output and return `E_NOTIMPL`. The actual Explorer import checked
  both the value and failure case in QEMU, logging
  `UxTheme TMT_BGTYPE reports borderfill` in
  `/private/tmp/micant-theme-uxtheme-bgtype-enum-run/serial.log`. Build and
  focused probes passed; the enclosing input suite still fails later at parent
  folder navigation.
- [x] `DrawThemeText` now draws supported one-line text through the caller's
  GDI DC and the theme foreground (inheriting WhiteSur HeaderBar foreground
  when a control omits it). Unsupported flags and text clipping return
  `E_NOTIMPL`; invalid pointers/rectangles and GDI failure return errors. QEMU
  reached Explorer's DIB pixel probe and logged the GDI text-render marker in
  `/private/tmp/micant-uxtheme-drawtext-inherited-run`. The broader
  `--verify-input` run later failed the concurrent parent-folder navigation
  check, so full interaction acceptance is not claimed.
- [x] Unsupported `GetThemeFont` and `GetThemeSysFont` calls clear the
  caller's 92-byte `LOGFONTW` output before returning `E_NOTIMPL`. Explorer
  calls the actual freestanding `GetThemeFont` export with sentinel bytes and
  checks the HRESULT and cleared output. QEMU reached the probe and logged
  `Unsupported font query clears LOGFONTW output` in
  `/private/tmp/micant-uxtheme-font-failure-output-run2/serial.log`. The full
  input suite later stopped at the concurrent root-folder navigation test;
  this records the focused runtime probe, not a passing full suite.
- [x] `GetThemeSysFont` reads the actual GDI `DEFAULT_GUI_FONT` through
  `GetObjectW`; Explorer created an `HFONT` from the returned `LOGFONTW`, read
  its metadata back, selected it into a memory DC and checked nonzero text
  metrics. An invalid font ID returned `E_INVALIDARG` with a cleared output.
  The full QEMU input/theme-switch run passed in
  `/private/tmp/micant-theme-font-snapshot-v2-final`. System font roles 801–806 currently
  share the one system GUI font exposed by GDI; separate per-role system
  metrics are not available yet.
- [x] GDI32 `GetObjectW` returns size/copy results for valid `HFONT` handles
  using their actual requested family, weight, italic and point-size metadata;
  invalid and non-font handles fail. Explorer verified the font round trip and
  text metrics in the QEMU run above. Other GDI object types remain unsupported.
- [x] Snapshot ABI v2 carries explicit CSS font-family, font-size,
  font-weight and font-style metadata, with per-state inheritance.
  `GetThemeFont` starts from the real GDI system font and overlays only fields
  present in the selected compiled style. It returns `E_PROP_ID_UNSUPPORTED`
  and clears `LOGFONTW` when that style contains no font metadata. Explorer
  verified both paths via the real DLL, including a synthetic metadata
  snapshot. The font-specific theme suite passed in a focused host harness.
  Full QEMU input, window geometry and Light-to-Dark profile switching passed
  in `/private/tmp/micant-theme-font-snapshot-v2-final`. WhiteSur's common button
  rules currently omit explicit font declarations, so those queries correctly
  report the property as unsupported. The source stylesheet declares no font
  family; the actual GDI system GUI face supplies inherited family data.
- [x] Implemented a bounded Buffered Paint lifecycle in the real `uxtheme.dll`:
  per-thread nested init/uninit, generation-tagged thread-owned handles,
  top-down 32bpp DIB buffers, one-shot discard/update, and failure cleanup.
  This slice accepts only `BPBF_TOPDOWNDIB`, a null `PAINTPARAMS`, and
  zero-origin rectangles up to 16M pixels; unsupported formats/parameters and
  `BufferedPaintSetAlpha` fail explicitly. Explorer exercised the actual DLL
  imports and checked discard pixels, updated pixels, nesting, stale handles,
  rejected-format output clearing and cleanup. Full `python3 qemu.py --headless`
  with `--verify-input` passed at
  `/private/tmp/micant-theme-buffered-paint-final-3`.
- [x] `DrawThemeText` now clips glyph writes to its caller rectangle and restores
  its DC clip/text state on both success and failure. The production GDI DLL
  exposes a private, one-shot clip scope for UxTheme; `FontManager` applies it
  while rasterizing glyph masks. Explorer verified that writes inside a narrow
  rectangle appear, surrounding pixels remain byte-for-byte unchanged, and a
  subsequent draw succeeds after clip restoration. Full
  `python3 qemu.py --headless` with `--verify-input` passed at
  `/private/tmp/micant-theme-text-clip-final-2`.
- [x] UxTheme color, part-size, integer, background-content and background-extent
  queries now clear caller outputs before returning an error. Explorer exercised
  the real freestanding DLL imports with invalid handles and sentinel outputs;
  QEMU logged `Failed UxTheme color, size, int and rect queries clear outputs`
  in `/private/tmp/micant-theme-uxtheme-failure-output-run/serial.log`. Build
  and the focused Ring 3 probe passed. The latest full QEMU input run also
  passed; legacy hosted assertions that expected synthetic query/draw success
  were corrected to expect documented failure statuses and cleared outputs.
- [x] Explorer closes a real DLL-created theme handle, then queries that stale
  token. QEMU verified `ERROR_INVALID_HANDLE` and a cleared color output in
  `/private/tmp/micant-theme-uxtheme-stale-handle-run/serial.log`. The focused
  Ring 3 check passed after the baremetal build; the latest integrated input
  run passed as well.
- [x] The Explorer process opens all 64 supported UxTheme handles, confirms
  the 65th open fails with null, closes the complete table, then opens and
  closes a new handle. QEMU logged `UxTheme handle exhaustion fails and
  recovers cleanly` in
  `/private/tmp/micant-theme-uxtheme-handle-capacity-run/serial.log`. The
  capacity/cleanup probe and the full input run passed after the build.

### 2. Win32 and common controls

- [x] The production comctl32.dll registers msctls_progress32 through the
  real User32 DLL and renders the range fill through uxtheme.dll and GDI;
  explicit PBM_SETBARCOLOR / PBM_SETBKCOLOR values override theme colors,
  and failed theme draws use the Base GDI colors. The bounded parser maps
  WhiteSur progressbar trough/progress descendants into the native normal and
  selected slots. Explorer creates the actual DLL control, sets a 0–100 range
  and 50 position, copies its rendered surface to a DIB, and compares both
  sample pixels with the active snapshot colors in QEMU. The build, focused
  Ring 3/DIB assertion and latest integrated --verify-input run all passed.
- [ ] Complete production menu drawing, popup hit testing, selection and
  command dispatch. The canonical `user32_boot` now exports bounded menu
  creation, item mutation/query, submenu ownership and HWND attachment APIs.
  `TrackPopupMenu`/`TrackPopupMenuEx` now open a bounded modal popup, draw menu,
  selected, checked and disabled rows through MENU UxTheme with clipped GDI
  Base fallback, support mouse hover/click and arrow/Enter/Escape selection,
  and send `WM_COMMAND` to the owner. Destroying the source menu or owner closes
  the popup. Popup mouse and keyboard owner-command dispatch plus menu
  ownership/query round-trip passed in
  `/private/tmp/micant-theme-item2-controls-9`. A bounded attached menu bar and
  `DrawMenuBar` are implemented; click dispatch passed, but the current
  screenshot does not establish visible menu text/background, so its pixel
  rendering remains open. Nested submenu display remains unsupported.
  `BUTTON`, `EDIT`, `SysTabControl32`, and `SCROLLBAR` have production
  UxTheme/GDI renderers with explicit fallbacks.
- [x] The canonical `user32_boot` now registers `BUTTON` lazily and renders
  `BS_PUSHBUTTON`/`BS_DEFPUSHBUTTON` through UxTheme/GDI, with a Base fallback
  when theme/DC resources fail. It implements pressed/focus/disabled/hover
  state, Space and mouse activation, `BM_GETSTATE`/`BM_GETCHECK`/`BM_SETSTATE`/
  `BM_SETSTYLE`/`BM_CLICK`, and parent `WM_COMMAND/BN_CLICKED`; unsupported
  `BS_*` types fail creation. Explorer's actual User32 import verified
  `BM_GETSTATE`, `BM_CLICK`, and the parent notification. QEMU sampled the
  control's DIB-backed screen pixels in normal (`#F0F0F0`), hover (`#E5EEF9`)
  and pressed (`#D7E4F2`) states in
  `/private/tmp/micant-theme-user32-button-final`.
- [x] Production User32 `GetCapture`/`SetCapture`/`ReleaseCapture` now use
  canonical Win32k state, route captured mouse moves and releases beyond window
  bounds, send `WM_CAPTURECHANGED`, and clear capture on window/process teardown.
  BUTTON tracks an armed press separately from its visible pressed state, so
  dragging outside cancels the visual and reentry restores it; it sends
  `BN_CLICKED` only when released inside. QEMU checked outside/reentry pixels
  and notification counts in
  `/private/tmp/micant-theme-button-capture-verified`. Capture ownership is
  process-scoped; Win32 per-thread capture routing remains unsupported.
- [x] GDI32 now clears its local DIB selection when `SelectObject` restores the
  stock/default bitmap. Previously the local DC table kept reporting the DIB
  selected, so `DeleteObject` rejected cleanup and repeated themed control
  paints exhausted Win32k bitmap slots. QEMU verified the fix by creating and
  retaining a second Explorer window after the common-control probes; active
  and inactive caption pixels differed (`#FAFAFA` / `#FFFFFF`) in
  `/private/tmp/micant-item3-dib-selection-fix`. The integrated run continued
  through caption, drag and resize checks, then stopped at the later reopen
  screenshot area threshold.
- [x] Production User32 `SetFocus`/`GetFocus` now track a focused HWND
  separately from the foreground top-level window. Win32k sends focus change
  messages, routes keyboard input to the focused control, and restores the
  parent/top-level focus when a focused child is hidden or destroyed. BUTTON
  arms Space only while focused and emits one click on keyup. Explorer verified
  the child focus API round trip; QEMU checked Space pressed pixels, one
  `BN_CLICKED`, and no activation after moving focus away in
  `/private/tmp/micant-theme-button-focus-run-2`. Focus ownership is
  process-scoped, so per-thread focus and Tab traversal remain open.
- [x] The production BUTTON WndProc handles `WM_SETTEXT`, `WM_GETTEXT`, and
  `WM_GETTEXTLENGTH` against its owned 128-WCHAR label. Updates are copied only
  when they fit, then invalidate the control; reads honor the caller's
  capacity and terminate the result. Explorer changed/read back an alternate
  label and confirmed an overlong update leaves the old label intact. The full
  QEMU input suite, including button hover/press pixels and Light-to-Dark, passed
  at `/private/tmp/micant-theme-button-text-final-2`.
- [x] Production User32 BUTTON now accepts `BS_CHECKBOX`, `BS_AUTOCHECKBOX`,
  `BS_3STATE`, `BS_AUTO3STATE` and manual `BS_RADIOBUTTON`, tracks check state
  through `BM_GETCHECK`/`BM_SETCHECK` and `BM_GETSTATE`, and draws glyphs and
  labels through the real BUTTON checkbox/radio UxTheme parts with a GDI
  fallback. Automatic checkbox/three-state activation toggles only on an
  enabled click; manual styles retain state and still notify once. Explorer
  verified automatic/manual checkbox transitions, programmatic state set,
  notification count and physical mouse activation in QEMU. QMP screenshots
  passed pixel comparisons for unchecked, hover, pressed and checked states at
  `/private/tmp/micant-theme-checkbox-radio-block1-escalated`. The full
  `qemu.py` run later stopped at the preexisting push-button pressed-pixel
  assertion, so complete input-suite success is not claimed. Auto-radio group
  exclusivity and explicit three-state/radio pixel probes remain open. Added
  automatic/manual three-state and manual radio message/notification checks;
  they passed on QEMU serial. Added a production `EDIT` class with bounded text
  storage, text messages, `WM_CHAR` append/backspace, readonly behavior,
  `EN_CHANGE`, themed UxTheme/GDI drawing and Base fallback. Explorer's QEMU
  probe verified text round-trip, input, readonly, unsupported-style rejection,
  and GDI-rendered text/focus pixels. QEMU also passed checkbox state pixels,
  EDIT focus pixels, BUTTON hover/pressed/capture and Space activation. The
  latest full run `/private/tmp/micant-theme-tab-final` verified
  `SysTabControl32` initialization, item round-trips, selection pixels, mouse
  notification, focused-arrow notification and keyboard selection pixels. The
  production comctl32 implementation has bounded item storage and supports
  insert/delete/get/set/current-selection/hit-test messages, themed GDI paint,
  mouse and focused-arrow selection, and `WM_NOTIFY/TCN_SELCHANGE`. The
  integrated run `/private/tmp/micant-theme-controls-focus-verified` now
  confirms that the focused BUTTON ring differs from ordinary hover and clears
  after focus loss; Space activation and focus-loss/no-activation checks pass.
  The same QEMU run passed SCROLLBAR range/page/position readback, hover pixels,
  arrow input, `WM_HSCROLL`, and thumb movement, plus the production menu
  ownership/query round-trip. Popup rendering/input passed separately in
  `/private/tmp/micant-theme-controls-popup-final`; mouse and keyboard both
  delivered the expected owner commands. That run also verified window-owned
  SB_VERT range/page/position and arrow-enable API round trips. The full input
  suite later stopped at the Explorer Control Panel text-color pixel assertion.
  Window-owned scrollbar state is now input-routed as detailed below. Complete
  disabled/clipping, tab traversal, per-thread capture, attached-window
  scrollbar visual styling and attached menu bar pixel coverage remain open.
  Added production `BS_AUTORADIOBUTTON` support with same-parent `WS_GROUP`
  exclusivity for `BM_CLICK`, mouse and Space activation. Explorer/QEMU probes
  cover two independent groups, notification counts and physical selection
  pixels in `/private/tmp/micant-theme-item2-controls-9`. Group boundaries
  introduced by non-BUTTON siblings are not tracked.
  A production `SCROLLBAR` child class now owns bounded range/page/position
  state, exports SB_CTL get/set APIs, paints UxTheme parts with a Base fallback,
  and supports arrow/page/thumb input with capture and parent scroll
  notifications. QEMU verified its range/page/position readback, hover pixels,
  arrow input, `WM_HSCROLL` and thumb movement in
  `/private/tmp/micant-theme-controls-popup-final`. SB_CTL is fully rendered;
  attached-window SB_HORZ/SB_VERT APIs now retain bounded range/page/position
  state and route arrow and thumb input. QEMU verified arrow and thumb
  notifications and position readback in
  `/private/tmp/micant-theme-item2-controls-9`. Attached bars still need a
  conclusive pixel/style check. Repeat timers and complete disabled/clipping
  coverage remain open.
- [x] Implement PBM range, position, step and color messages; render the real
  production progress class through UxTheme/GDI with explicit-color precedence
  and Base fallback. QEMU confirmed trough/fill DIB pixels against the active
  WhiteSur snapshot. This focused probe passed; the enclosing `--verify-input`
  run still stops at the concurrent parent-folder navigation assertion.
- [ ] Verify normal, hover, pressed, checked and disabled states, dimensions,
  clipping, Tab traversal and complete mouse input/thread capture behavior
  across all controls in QEMU.
- [ ] Preserve explicit application colors/styles where the Win32 contract
  gives them precedence; verify all failure paths instead of reporting success.

### 3. Nonclient window decoration and geometry

- [x] Render and verify active titlebar colors in Light and Dark. QEMU compared
  the same live-window caption pixel before/after the runtime switch; the Dark
  sample passed after the Explorer broadcast fix above. Caption buttons use
  theme snapshot normal/hover/pressed/inactive colors, opacity and borders.
- [x] Caption titles now use the shared GDI font path and per-process font
  selection; bounded title coverage and copied BGRA HICON snapshots travel in
  a separate compositor decoration section. Window metadata carries only
  checked offsets, dimensions, strides and generations; it contains no process
  pointers or raw icon handles. Replacement advances the generation, and
  window destruction/reuse clears retained icon/title pixels. The decoration
  section is writable only in the trusted compositor mapping, leaving window
  content pixels read-only there. The shared surface pool is 20 slots to
  accommodate live control probes and reopened windows within QEMU memory.
  `DecodeIconFile` now accepts a standalone PNG payload as well as an ICO
  container, so User32 `LoadImageW(..., IMAGE_ICON, LR_LOADFROMFILE)` can load
  the packaged WhiteSur folder image. QEMU requires the load marker and
  distinct pixels above the title text; the earlier overlapping-pixel probe
  alone could mistake a title glyph for an icon.
- [x] Win32k clamps resize against client-area minima, accounting for the
  active frame and caption metrics on all four edges/corners; the minimum
  outer size therefore preserves a 160x100 client. The QEMU input test now
  captures the resize marker before releasing the maximize button, avoiding a
  race with Explorer dispatch. Full QEMU passed maximize/restore, move, all
  eight resize targets, minimum width/height, close/reopen, and stable boot in
  `/private/tmp/micant-nonclient-min-client-fixed-run`.
- [x] QEMU repeated caption drag and edge resize after the live Light-to-Dark
  change; both passed and the client surface remained live in the same
  interactive run above.
- [x] Freestanding Win32k now packs WM_MOUSEMOVE and button lParam values from
  the client origin, subtracting frame and caption metrics while preserving
  screen-space hit testing and capture. The full QEMU run verified the Explorer
  parent button and folder row at their actual client targets, then passed all
  caption, drag, resize, clamp, reopen and runtime theme-switch checks in
  `/private/tmp/micant-theme-client-coordinate-fix-reopen`.
- [x] QEMU compares inactive and active caption pixels in overlapping Light
  windows, then cycles the live app from Dark to Light Purple and verifies the
  pressed caption button pixel is exactly `#9A57A3` (diagnostics:
  `/private/tmp/micant-nonclient-inactive-accent-final-run`).
- [x] The integrated QEMU input run verified caption title/icon pixels,
  maximize/restore, caption dragging, all eight resize edges/corners, minimum
  client dimensions, close/reopen cycles and live Light-to-Dark switching;
  compositor surface allocation remained available through the reopen matrix
  (`/private/tmp/micant-icon-real-pixel`).

### 4. Explorer theme coverage

- [x] The taskbar Start surface and its leading accent stripe now use the
  active theme's selected and accent colors instead of fixed XP green. The
  real Explorer taskbar DIB checks the Start surface pixel against the active
  selected color and QEMU logged `Taskbar Start surface uses active theme
  pixel` in `/private/tmp/micant-theme-taskbar-start-run/serial.log`.
  Build/link and the theme pixel probe passed; the broader input suite later
  stopped at the concurrent root-folder navigation test.
- [x] Volume and network glyphs in the notification area now use the active
  accent foreground instead of fixed off-white pixels. Explorer checks a real
  tray DIB pixel, and QEMU logged `Notification icons use accent foreground
  pixel` in `/private/tmp/micant-theme-notification-icons-run/serial.log`.
  The full input run again stopped at the concurrent root-folder navigation
  test after reaching both theme pixel probes.
- [x] The app-window status strip now resolves `StatusBar` background and
  foreground from the theme snapshot, with window-surface/text fallbacks. A
  pixel assertion checks the live DIB, and QEMU logged `Explorer status bar
  uses StatusBar theme pixel` in
  `/private/tmp/micant-theme-statusbar-run/serial.log`. As above, this run
  passed the theme probe but the full input suite later failed the unrelated
  root-folder navigation check.
- [x] Explorer file names use the active Window foreground and type/date
  columns use the toolbar secondary foreground instead of fixed colors. The
  QEMU input script opens Control Panel to render a static row and the real
  window DIB checks for an exact theme foreground pixel; the marker passed in
  `/private/tmp/micant-theme-explorer-row-text-run/serial.log`. The later
  filesystem-navigation assertion still fails on the concurrently changed
  root directory path, after this theme check passed.
- [x] Route remaining semantic shell colors through the active theme palette:
  desktop labels use the theme accent foreground, task switch and missing-icon
  glyphs use the active accent, and the attached-scrollbar probe uses the
  theme surface color. Address, toolbar, sidebar, status, Start, taskbar and
  notification colors already consume the snapshot-derived palette. Explorer
  reads the accent from the compiled button state that carries the profile
  accent, so Light Purple reaches Start and taskbar surfaces as well as caption
  decorations; packaged multicolor Windows marks remain artwork.
- [x] QEMU now samples the Explorer address field and Start-menu surface across
  the live Light-to-Dark switch, alongside the existing titlebar, taskbar,
  notification and accent checks. The complete input run passed the address
  and Start-menu pixel comparisons together with window move/resize and reopen
  (`/private/tmp/micant-icon-real-pixel`).

### 5. Variants and accents

- [x] Verify Light and Dark selection and the live Light-to-Dark switch in the
  QEMU input matrix, including titlebar, address field, Start menu, taskbar and
  post-switch move/resize pixels (`/private/tmp/micant-theme-point5-input-run11`).
- [x] Light Purple is available through Start → Theme Appearance. QEMU verified
  the exact `(154,87,163)` accent in the pressed caption button and Start header
  (`caption-purple-pressed.ppm`, `theme-menu-purple.ppm` in the same run).
- [x] Add an Auto choice to the same user-facing selector. It chooses Light
  from 06:00 through 17:59 and Dark otherwise, then a Ring 3 worker posts a
  private schedule tick every second; Explorer reloads/publishes and redraws
  the matching snapshot in the existing process. QEMU verified worker startup,
  Auto selection from Purple and the current `Dark (18:00-06:00)` effective
  choice in serial (`/private/tmp/micant-theme-point5-input-run11`).
- [x] Exercise the scheduled update path with a deterministic 17:59 → 18:00
  QEMU clock fixture. Explorer published Light then Dark, checked that the
  taskbar DIB pixel changed with the effective theme, restored its prior
  snapshot/profile, and the full input run passed
  (`/private/tmp/micant-theme-point5-auto-boundary-1794806259`).

### 6. Preferences and multiuser settings

- [x] Define a validated, versioned 32-byte preference record for theme,
  Light/Dark/Auto mode, accent, schedule and effect flags. The parser rejects
  unsupported versions, malformed lengths, unknown values and reserved bits;
  focused validation cases were added to the theme suite.
- [x] Add the native `NtQueryInformationToken(TokenUser)` path for the current
  process token, with a shared ABI, NTDLL export, syscall dispatch, output
  validation and explicit failures for unsupported classes and token handles.
  QEMU exercised the invalid-class/invalid-handle CPL3 cases, then Explorer
  successfully resolved `C:\Users\S-...` from its real token SID
  (`/private/tmp/micant-theme-point6-token-1794804678`).
- [ ] Connect these preferences to per-user profile storage through the
  established Cm/Userenv/profile owner and access controls. Current work only
  resolves a SID-based profile path; it does not create the profile directory,
  enforce its ACL or persist preferences.
- [ ] Persist only after successful theme publication; test corrupt/missing
  settings, rollback, restart persistence and distinct settings for two users.

### 7. Runtime notifications and atomic replacement

- [x] Win32k preflights notification queue capacity, publishes the validated
  snapshot and queues `WM_THEMECHANGED` for active windows under its update
  lock; Explorer redraws app windows on receipt.
- [x] Exercise Light-to-Dark selection in QEMU; the profile-specific serial
  marker arrived after Explorer redrew existing surfaces without restarting.
- [x] Fill the Win32k notification queue, attempt a Dark snapshot publication,
  then verify in Explorer and QEMU that the prior profile and full snapshot are
  unchanged and the queue drains cleanly
  (`/private/tmp/micant-theme-point7-queue-1794805019`).
- [x] Attempt to load an unavailable packaged profile and compare both the
  Explorer profile and full Win32k snapshot to the previous state. QEMU logged
  the fallback and rollback markers before completing the input/move/resize
  suite (`/private/tmp/micant-theme-point7-missing-profile-retry-1794805309`).
- [x] Corrupt the snapshot version after reading a valid snapshot, reject it
  before publication, and verify that the previous complete snapshot remains
  active in QEMU (`/private/tmp/micant-theme-point7-malformed-snapshot-1794806550`).
- [ ] Exercise `loadActiveVisualTheme` against a file that exists but contains
  malformed compiled-style bytes; the direct invalid-version publication path
  is covered, but no corrupt package fixture exists yet.

### 8. Compositor effects

- [x] DIB-backed UxTheme parts support clipped rounded fills and alpha blend.
- [x] Software composition applies premultiplied window opacity, a bounded
  corner mask and a six-pixel shadow when the Window theme style supplies a
  radius; damage expands around the shadow. Missing/invalid style keeps the
  opaque square Base path. A synthetic compositor pixel probe for corner,
  backdrop opacity, shadow and Base fallback passed in QEMU
  (`/private/tmp/micant-theme-point8-effects-corrected-1794805980`).
- [ ] Add a stable backdrop scratch pipeline for bounded blur and connect
  transparency/radius/shadow/blur preferences to per-user settings. Verify
  effects on real Explorer windows and after move/resize, including resource
  failure fallback; current QEMU pixel proof is a synthetic compositor surface.
- [ ] Keep VirtIO-GPU 2D scanout evidence distinct from GPU-composition claims;
  no hardware or accelerated-composition verification is established.

### 9. Fallback and robustness

- [x] After queue exhaustion, unavailable-profile and malformed-snapshot
  failures, reload a valid Dark profile and then restore the original profile.
  QEMU confirmed both publications succeeded and the full interactive suite
  remained operational (`/private/tmp/micant-theme-point9-valid-recovery-1794806800`).
- [x] Request a missing packaged desktop icon size, verify its load fails, and
  compare the resulting built-in computer glyph pixel before restoring the
  normal desktop. The same QEMU run continued through the input and
  move/resize suite (`/private/tmp/micant-theme-point9-missing-icon-1794807056`).
- [ ] Add repeatable QEMU cases for malformed CSS and bounded
  allocation/compile/render failure; missing profile, invalid snapshot version
  and missing icon asset are covered above.
- [ ] For each case, verify Explorer, input, focus, move/resize and presentation
  stay operational and the previous valid theme or Base is rendered.
- [ ] Restore the package and verify a later valid load succeeds.

### 10. Parser support inventory

#### Implemented bounded subset (source inventory; not a coverage claim)

The import parser lives in `include/micant/um/subsystems/theme/gtk_css.hpp`;
`native_theme.hpp` narrows parsed rules to the native snapshot model. The
following describes current code, not all CSS that WhiteSur ships:

- The lexer accepts whitespace and `/* ... */` comments, rule blocks, comma
  separated selector lists, and declarations with quoted strings. Bounds are
  1 MiB input, 8192 rules, 256 selectors per rule, 64 declarations per rule,
  32768 compiled declarations, and 256 bytes per selector.
- Selector text may contain node names, `.class`, `#id`, universal `*`,
  descendant/child/adjacent/general-sibling combinators, attribute syntax and
  pseudo syntax. Pseudo names recognized by parsing are `:hover`, `:active`,
  `:checked`, `:selected`, `:disabled`, `:focus`, `:backdrop`,
  `:first-child`, `:last-child`, `:only-child`, `:not(...)`, `:dir(...)`,
  `:drop(...)`, `:link`, `:visited`, `:indeterminate`, `:first`, and `:last`.
  However native style compilation currently classifies only a bare supported
  node or `node:pseudo` for Button, Edit/entry, CheckBox, Radio, ComboBox,
  Menu/MenuItem, Tab, Progress, ScrollBar, Slider/scale, ToolBar, StatusBar,
  ToolTip, HeaderBar and Window. The two WhiteSur Progress selectors
  `progressbar trough` and `progressbar progress` map to Progress normal and
  selected states. Other complex selectors, classes, IDs, attributes, and the
  additional structural/state pseudos parse but are counted as
  `unsupportedSimpleRules`; they do not participate in style matching.
- Parsed at-rules are limited to `@define-color name value;` and
  `@keyframes name { ... }`. Keyframe bodies are skipped and counted in
  `omittedKeyframeBlocks`; they are not animated. Other at-rules (including
  `@import`, `@media`, and `@supports`) return `CssStatus::Unsupported`.
- Native properties are `color`, `background-color`, `border-color`,
  `border-width`, `border` (width only), `border-radius`, `min-width`,
  `min-height`, `padding`, `font-size`, and `opacity`. Unknown properties and
  values that fail their typed conversion increment `unsupportedProperties`.
  Lengths accept non-negative unitless numbers and `px` only; percentages and
  other units are rejected. Padding supports CSS one-to-four-value expansion.
- Colors accept `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `currentColor`,
  `transparent`, `black`, `white`, `@define-color` references, and bounded
  `rgb()`, `rgba()`, `alpha()`, `mix()`, `shade()`, `lighter()`, and `darker()`
  forms. Function nesting is limited to depth 8. Accent replacement applies
  to the configured source color and selected/accent symbolic names.
- The compiler does not resolve CSS `url(...)` or other asset references.
  Background images, gradients, borders with style/color shorthands, font
  family/weight, transitions, transforms, shadows, and material effects are
  not represented by the native style snapshot. WhiteSur profiles contain
  such declarations (for example `background-image`, `box-shadow`, and
  `transition`); their presence in the input does not mean they are rendered.

This inventory was derived from source inspection plus property examples in
`System/Themes/WhiteSur/Light/gtk.css` and `Dark/gtk.css`. Parser boundary and
fixture coverage is still open below; this section alone does not satisfy the
required bounded tests or asset-safety checks.

Concrete profile examples: Light defines `entry { min-height: 24px; padding:
2px 8px; border-radius: 6px; color: #363636; background-color: rgba(0, 0, 0,
0.05); }` and `entry:focus` uses `alpha(mix(white,#0860F2,0.75),0.75)`. These
declarations exercise the native property/color subset. The same profile also
has `entry.search`, `entry image`, `entry.flat`, descendant selectors,
`transition`, `animation-timing-function`, `box-shadow`, and `background-image`;
those selectors/declarations parse as text, but the unsupported selector or
property path is skipped/reported by native compilation, with no asset fetch or
effect rendering.

- [x] Record supported WhiteSur selectors, states, properties, color functions,
  units and asset references with examples from the imported profiles.
- [ ] Record exclusions and diagnostics for unsupported constructs; expand
  selector/state/property coverage only with bounded parser tests.
- [ ] Verify malformed, oversized and traversal inputs fail without replacing
  the active snapshot.

### 11. Integrated acceptance and evidence

- [ ] Run the canonical bare-metal build and full QEMU matrix from §18 of the
  theme engine specification on a source-matched image.
- [ ] Include Light, Dark, Auto, alternate accent, runtime switch, all target
  control states, caption controls, window move/resize, fallback cases and
  stable scanout/serial evidence.
- [ ] Update the evidence ledger with exact command, source SHA, serial/QMP
  logs and pixel/screenshot results; keep hardware/GPU evidence separate.

## Next concrete steps

1. Verify additional resize edges/corners, min-size clamping, caption buttons,
   and gestures after a theme switch.
2. Complete theme-backed common controls and nonclient drawing, then extend
   QEMU state and pixel coverage.
3. Continue through the unchecked acceptance items in dependency order and
   check items only after implementation plus their stated evidence.
