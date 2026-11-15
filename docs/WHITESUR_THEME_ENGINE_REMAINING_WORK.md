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
  QEMU serial recorded both notifications, and
  `caption-dragged-window.ppm` / `edge-resized-window.ppm` show the resulting
  scanout. The executed gesture covered caption drag and bottom-right resize.
- [x] Canonical `ntoskrnl_core_baremetal` and Explorer dependencies built and
  linked at source commit `b98f1be`; QEMU completed input, window cleanup,
  reopen-cycle, and Light-to-Dark runtime switch paths. The run used a
  temporary harness adjustment to click the exposed background window while
  preserving the pixel threshold. Diagnostics are at
  `/private/tmp/micant-theme-qemu-verify-b98f1be-v2`.
- [ ] A build of the latest uncommitted shared worktree is still pending. That
  tree contains unrelated Task Manager/kernel changes; its earlier build
  failed in `kernel/init/kiinit.cpp` (`third`/`fourth` undeclared) and
  `kernel/ps/psmgr.cpp` (exceptions disabled and missing
  `ob::ObjectNamespace`).

## Remaining acceptance work

### 1. UxTheme and GDI behavior

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

- [ ] Render themed titlebars, borders, caption buttons and icons from theme
  data in the software compositor.
- [ ] Verify close/minimize/maximize, all supported edge/corner resize hit
  targets, minimum-size clamping, and client/surface dimensions against
  Win32k hit testing and compositor output. QEMU currently covers caption drag
  and bottom-right resize only.
- [ ] Repeat move/resize and caption-button tests while active/inactive and
  after Light/Dark/accent switches; compare pixels and hit targets.

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
