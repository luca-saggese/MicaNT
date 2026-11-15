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
- [ ] A build of the latest uncommitted shared worktree is still pending. That
  tree contains unrelated Task Manager/kernel changes; its earlier build
  failed in `kernel/init/kiinit.cpp` (`third`/`fourth` undeclared) and
  `kernel/ps/psmgr.cpp` (exceptions disabled and missing
  `ob::ObjectNamespace`).

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
  it returns `E_NOTIMPL` until that rendering path exists. `qemu.py
  --check-explorer` passed, and `llvm-readobj --coff-exports` confirmed its
  name and ordinal. Full QEMU reached both app windows and passed UxTheme/GDI
  pixel probes, then failed the existing maximize caption hit-target assertion;
  therefore the interactive regression remains unverified.
- [ ] Audit every exported UxTheme function against its public signature,
  result codes, handle lifetime and documented unsupported cases.
- [ ] Exercise queries and drawing from the actual freestanding `uxtheme.dll`
  and GDI32 imports in QEMU, including invalid/stale handles, clipping,
  rounded fills, alpha and allocation/dispatch failures.
- [ ] Add and verify the missing drawing paths needed for text, fonts and
  buffered paint, or return the documented unsupported result until implemented.

### 2. Win32 and common controls

- [ ] Connect actual production DLL renderers for buttons, checkboxes, radio
  buttons, textboxes, menus, tabs, scrollbars and progress bars to UxTheme/GDI.
- [ ] Verify normal, hover, pressed, checked, focused and disabled states,
  dimensions, clipping, keyboard/mouse input and theme changes in QEMU.
- [ ] Preserve explicit application colors/styles where the Win32 contract
  gives them precedence; verify all failure paths instead of reporting success.

### 3. Nonclient window decoration and geometry

- [x] Render and verify active titlebar colors in Light and Dark. QEMU compared
  the same live-window caption pixel before/after the runtime switch; the Dark
  sample passed after the Explorer broadcast fix above. Caption buttons use
  theme snapshot normal/hover/pressed/inactive colors, opacity and borders.
- [ ] Replace generic 8x8 caption title glyphs with the shared font service and
  carry safe per-window icon pixels through the compositor surface protocol.
  The compositor currently has no callable font service or process-safe icon
  snapshot; process-local HICON values must not be sent in shared metadata.
  Reusing `FontManager` directly requires NTDLL file/VM syscalls. A local
  experiment linked its NTDLL dependency and built, but QEMU trapped with
  `#UD` in the compositor after the first app window was created; keep the
  current 8x8 fallback until the compositor's import/syscall path is resolved.
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
- [x] QEMU compares inactive and active caption pixels in overlapping Light
  windows, then cycles the live app from Dark to Light Purple and verifies the
  pressed caption button pixel is exactly `#9A57A3` (diagnostics:
  `/private/tmp/micant-nonclient-inactive-accent-final-run`).

### 4. Explorer theme coverage

- [ ] Route remaining fixed-color shell surfaces and text through the active
  theme model or UxTheme/GDI.
- [ ] Capture and compare QEMU pixels for Explorer chrome, titlebar, toolbar,
  address field, taskbar, Start menu and notification area in each profile.

### 5. Variants and accents

- [ ] Verify Light and Dark selection in QEMU, not only profile loading/build.
- [ ] Make accent selection available through the user-facing settings path
  and verify an alternate accent in controls, shell surfaces and decorations.
- [ ] Implement Auto policy and verify effective theme changes on schedule or
  system preference notification without restarting Explorer.

### 6. Preferences and multiuser settings

- [ ] Add validated, versioned per-user theme/accent/effect preferences using
  the established Cm/Userenv/profile owner and access controls.
- [ ] Persist only after successful theme publication; test corrupt/missing
  settings, rollback, restart persistence and distinct settings for two users.

### 7. Runtime notifications and atomic replacement

- [x] Win32k preflights notification queue capacity, publishes the validated
  snapshot and queues `WM_THEMECHANGED` for active windows under its update
  lock; Explorer redraws app windows on receipt.
- [x] Exercise Light-to-Dark selection in QEMU; the profile-specific serial
  marker arrived after Explorer redrew existing surfaces without restarting.
- [ ] Test queue exhaustion and failed reload preserve the previous complete
  theme.

### 8. Compositor effects

- [x] DIB-backed UxTheme parts support clipped rounded fills and alpha blend.
- [ ] Implement and verify software-compositor window transparency, rounded
  window masks, shadows and bounded blur, with resource-failure fallback to
  basic composition/Base visuals.
- [ ] Keep VirtIO-GPU 2D scanout evidence distinct from GPU-composition claims;
  no hardware or accelerated-composition verification is established.

### 9. Fallback and robustness

- [ ] Add repeatable QEMU cases for missing package/profile, malformed CSS,
  missing assets and bounded allocation/compile/render failure.
- [ ] For each case, verify Explorer, input, focus, move/resize and presentation
  stay operational and the previous valid theme or Base is rendered.
- [ ] Restore the package and verify a later valid load succeeds.

### 10. Parser support inventory

- [ ] Record supported WhiteSur selectors, states, properties, color functions,
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
