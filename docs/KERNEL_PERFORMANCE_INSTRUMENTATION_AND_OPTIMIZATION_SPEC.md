# MicaNT — Performance Instrumentation and System-Wide Optimization Specification

**Document:** `docs/KERNEL_PERFORMANCE_INSTRUMENTATION_AND_OPTIMIZATION_SPEC.md`  
**Status:** Implementation specification — proposed; not evidence that instrumentation or optimizations are operational  
**Platform:** AMD64, C++23, canonical modular NT-compatible kernel, UEFI/QEMU and selected physical hardware  
**Owners:** HAL/Ke, Ps, Mm, Io/FsRtl, Win32k, graphics service/VirtIO-GPU, Explorer/compositor and user-mode tooling  
**Priority:** Critical, immediately applicable to the current desktop  
**Related:** `AGENTS.md`, `docs/KERNEL_ARCHITECTURE.md`, `docs/KERNEL_MILESTONES.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, `docs/KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md`, `docs/KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`, `docs/BAREMETAL_TELEMETRY.md`, `docs/LIVE_TELEMETRY.md`.

## 1. Objective

Provide **small, always-available performance accounting** with optional higher-detail instrumentation, enabling repeatable attribution of desktop stutter and systematic optimization throughout MicaNT. The fundamental outcome is not a dashboard: it is a causal, timestamped view of **input → runnable thread → UI dispatch → surface update → compositor → display driver submit/completion**. Optimize one measured bottleneck at a time and compare before/after under equivalent workloads.

Deliverables:

1. A reproducible baseline of desktop responsiveness in QEMU, with measured distributions rather than anecdotal FPS.
2. One canonical, fixed-format event/counter facility that works in kernel and controlled Ring 3 producers, without a second scheduler or graphics path.
3. Minimal production-level counters always compiled, with compile-time detailed tracing levels and bounded runtime filtering.
4. Correlation of actual input events to compositor presentation, including loss, coalescing, queueing and timeout evidence.
5. Low-overhead, bounded export and host-side analysis (JSON/CSV and readable summary).
6. Evidence-based removal of idle spin and avoidable full-frame redraws/copies.
7. Regression tests, budget reporting and ongoing performance gates without fabricated results or universal thresholds.

**Do not** turn performance instrumentation into invasive telemetry. Measurements are local, opt-in for detailed traces, bounded, zero-network by default, and do not upload device/user data.

## 2. Current baseline and hypotheses — verify before coding

Inspect current HEAD and the actual QEMU build graph; status below is based on inspected `main` sources and is not proof of runtime behavior.

| Component | Observed source behavior | Hypothesis / validation needed |
|---|---|---|
| `tools/compositor_app.cpp` | Main loop checks shared `frameSequence` and executes `pause` repeatedly when unchanged or invalid | Potential CPU saturation, starvation or bad wake latency; measure on the actual QEMU scheduler |
| `tools/compositor_app.cpp` | `compose()` initializes a complete `g_frame`, loops through surfaces, blends and issues user service `0x81` with a complete output buffer | Potential unnecessary per-frame work, memory traffic and device transfer; measure per stage |
| `include/micant/shared/compositor_surface.h` | Has per-surface dirty rectangle fields and a cursor payload | Presence of fields does not prove dirty updates propagate end-to-end |
| `tools/compositor_app.cpp` | Frame sequence is a lockless shared-memory publication mechanism | Validate coherent snapshot, publication ordering, and whether frames can be lost or raced |
| `docs/BAREMETAL_TELEMETRY.md` / `docs/LIVE_TELEMETRY.md` | Contain historical telemetry descriptions | Do not treat historical figures as verification of current modular desktop |
| `docs/KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md` | Specifies interactive scheduling behavior | Specification is not proof of end-to-end ready-to-run latency |

**Initial suspects, not confirmed root causes:** compositor busy-polling, full-frame clear/blend/present, scheduling latency, frequent redundant frame publications, sync/present waits, framebuffer/scanout copies, font/asset/theme work on UI path, or VM/host configuration.

At 1024×768×4 B per pixel, a full frame is 3,145,728 B (= 3 MiB), corresponding to ~180 MiB/s for one full-frame pass at 60 FPS before accounting for additional passes, copies or PCI/device overhead. This is a *reference calculation*, not a measured bottleneck.

## 3. Design principles and invariants

1. **Always-on, near-zero-overhead core:** counters and failure/timing summaries do not allocate, lock globally, format strings or call I/O on hot paths.
2. **One timing source:** HAL exposes calibrated monotonic timestamp and conversion information, including its validity and precision; prefer a stable TSC when supported, with safe fallback.
3. **Causal correlation:** pass explicit operation IDs across Ps/Win32k/compositor/driver boundaries; no inference solely from adjacent timestamps.
4. **No behavioral changes in instrumentation:** disabled detailed tracing must compile out; enabled probes must not modify ordering, spinlocks, IRQL or user-mode dispatch.
5. **Bounded memory and deterministic failure:** fixed rings and counters, explicit lost-event accounting, no blocking on full rings.
6. **Correct production ownership:** Ke owns scheduling metrics; Mm owns PFN/fault metrics; Io owns request metrics; graphics owns submit/fences; UI owns paint/layout; host tools analyze only exported records.
7. **No fake claims:** distinguish source code present, built, booted, exercised in QEMU, measured with known configuration, and verified on physical hardware.
8. **Performance is multidimensional:** report latency distributions, frame pacing, CPU cost, bandwidth/copies, allocation pressure and errors, not just FPS.
9. **No mandatory diagnostics dependency:** system boots and desktop operates when tracing is unavailable, disabled, exhausted or its exporter fails.
10. **Measurement privacy:** no passwords, file contents, screen pixels, unredacted user strings or raw pointer secrets in trace records.

## 4. Compile-time and runtime profiles

Expose one configuration option consistently to bare-metal kernel and relevant Ring 3 targets:

```cmake
# Suggested configuration — adapt to the existing build conventions.
set(MICANT_PERF_LEVEL "0" CACHE STRING "Performance tracing level: 0, 1, or 2")
```

| Level | Role | Included | Hot path behavior |
|---|---|---|---|
| **0 / Minimal** | Production baseline, always available | Small scalar counters, dropped/failed operations, key scheduler and compositor totals | Counter updates only where owned; no event stream |
| **1 / Instrumented** | Development/default benchmarking | Timestamped phase events, bounded histograms, selected per-thread and device metrics | Fixed-size records to per-CPU or owner-local rings, runtime mask/sample support |
| **2 / Deep** | Targeted diagnosis | Additional scheduler state, queue transitions, detailed driver events, safe sampling and optional stack identifiers | Strict budgets; may perturb latency and is never compared blindly with Level 0 |

A compile-time check must reject invalid levels. Use macros/inlines that do not evaluate trace-only expensive arguments at Level 0. Keep minimal counters in freestanding-safe types. Runtime masks can disable groups at Levels 1–2 but cannot justify a blocking sink.

**Avoid promising precise “zero overhead.”** Record the overhead of instrumentation with A/B builds and report its uncertainty.

## 5. Clock contract and measurement correctness

### 5.1 Clock API

Reuse existing HAL bootstrap high-resolution clock / QueryPerformanceCounter support where appropriate; move to a canonical generic clock owner only through an explicit migration. Define an internal record containing `ticks`, `clockId`, `cpuId`, `calibrationGeneration`; provide conversion to nanoseconds with checked arithmetic.

- TSC must be checked for invariance, reliability and cross-core comparability before cross-CPU subtraction.
- Do not assume TSC frequency is CPU boost frequency; preserve rational conversion / overflow-safe multiply-divide.
- On unsupported or unstable systems, use the supported monotonic fallback and publish clock quality.
- Preserve semantics through sleep/resume, CPU migration, wraparound and changes in calibration.
- At high IRQL, timestamp reading must not allocate, sleep or access unsafe mapping.
- For multiprocessor traces, track core affinity/migration; if the clock is not comparable across cores, label cross-core latency *unavailable*, not zero.
- Distinguish wall-clock time, CPU time, ready latency, runnable queue residence, device completion and presentation time.

### 5.2 Calibration tests

Check monotonicity and conversion against a separate bounded timer interval; check QEMU TCG and any supported hardware acceleration mode independently. Do not assign host-vs-guest performance meaning to timings without noting VM clock behavior.

## 6. Core event and counter model

Introduce a **versioned private fixed-layout ABI** in `include/micant/shared/` only when genuinely crossing Ring 0/Ring 3. Kernel-private implementation remains under `include/micant/km/`; user-facing wrappers may live under `include/micant/um/`.

Suggested fields for each event: magic/version or ring version, event type, record size, flags, clock ID, timestamp ticks, CPU ID, PID/TID (where available), operation/correlation ID, and two or three numeric payload fields. Avoid any STL, pointers, strings, variable-sized text or unvalidated user memory in the record ABI.

Event groups:

- `Ke`: Ready, Dispatch, SwitchOut, WaitBegin, Wake, Preempt, IdleEnter/Exit, DPCBegin/End.
- `Ps`: Process/ThreadCreate and Exit, user entry, priority/affinity change.
- `Mm`: FaultStart/End (categorize), allocation fail, PFN free/commit/copy-up, map/unmap.
- `Io/FsRtl`: Submit/Complete, bytes, queue depth, cache hit/miss and error.
- `Input/Win32k`: IRQ or raw event, queue insert, dequeue, hit test, message dispatch, surface publish.
- `Compositor`: Wake, Snapshot, DirtyCalc, Clear/Blit/Blend/Effects, ComposeEnd, PresentRequest.
- `Graphics`: QueueSubmit, DMA/TransferBegin/End, FenceComplete, ScanoutFlip/Completion (only if actually observable), timeout/reset.
- `User UI`: Layout, Paint, FontRaster, ImageDecode, ThemeRecompile, etc. with optional sampling.

**Important:** Do not label a driver command completion as photon-on-screen time. If present/vblank visibility is not observable, report `submit-to-completion` and explicitly mark actual display latency as unknown.

### 6.1 Rings and synchronization

For SMP use per-CPU fixed-size rings where feasible, or per-producer single-writer rings; a shared multi-writer ring needs an auditable atomic reservation/publication protocol. Avoid races on commit/visibility, partial records, torn writes and false sharing. Lost records count explicitly; readers never mistake an overwritten record for a valid one. Ring overflow never blocks ISR/DPC or compositor.

Choose ring capacity from a configurable fixed memory budget, initialized early but not required for kernel bootstrap. A system without sufficient memory falls back to minimal counters. Correctly detach/reclaim per-thread/process resources after termination and driver removal.

### 6.2 Histograms and aggregation

Track count, min, max, sum with saturation and bounded approximate histogram buckets (e.g. logarithmic) sufficient for P50/P95/P99. Explicitly mark estimates and low sample counts. Avoid storing all samples in kernel memory. Maintain separate failed/skipped/overwritten/cancelled counts.

## 7. Query/export and local analysis

A privileged, versioned read-only query path exposes snapshots of counters and bounded trace pages. Reuse the established NT syscall/device/Io/Ob security model instead of adding a magical raw-memory mapping.

Requirements:

- Input size/version validation, access check, user-buffer capture and bounded copying.
- Snapshot generation and ring cursor so consumers can detect gaps and avoid indefinite reads.
- Optional Ring 3 diagnostic collector flushes asynchronously to local files or serial in **batches outside hot paths**.
- Host tool parses streams, verifies schema and timestamps, computes percentiles and emits a text summary plus CSV/JSON.
- Correlation IDs survive authorized IPC/cross-process hops without becoming privilege-bearing handles.
- No network export by default, no personal data, and no dependency on a live monitoring server.
- A collector crash must not change scheduling, desktop or device behavior.

Do not confuse `tools/perfmon_app.cpp`, `tools/xperf_app.cpp`, `scripts/record_telemetry.py`, or historical telemetry documents with validated ingestion of this new binary trace format; integrate only after checking the actual production linkage and behavior.

## 8. End-to-end desktop timeline

Use unique IDs at these boundaries:

```text
Physical / QEMU input event [inputId]
  -> input IRQ / HID decoded
  -> Win32k event enqueue [inputId]
  -> UI thread becomes Ready [threadId]
  -> scheduler selects Running
  -> user message dequeue / dispatch
  -> application paints into surface [surfaceId, generation]
  -> Win32k publishes frameSequence [frameId]
  -> compositor event wakes [frameId]
  -> compose snapshot + dirty regions
  -> present syscall [frameId]
  -> VirtIO-GPU submit [commandId]
  -> device completion / scanout update (if observed)
```

Clearly handle input coalescing, frame coalescing, dropped or superseded generations and events that never result in a new frame. Do not equate every mouse movement with one full completed frame. A frame may be produced without input (animation, timer), and multiple inputs can produce one frame.

Required measurements: input-to-Win32k, queue residence, wake-to-run, input-to-app-dequeue, paint duration, publish-to-wake, compositor CPU time, compositor idle CPU time, syscall/present time, submit-to-device-completion, end-to-end *observable* latency and frame presentation interval.

## 9. Workloads and reproducible baseline (PERF0)

Before the first performance optimization, capture an unmodified baseline from an exact source SHA/build config.

Scenarios, each repeated sufficiently to characterize variability:

| Scenario | Duration | Expected stimuli |
|---|---:|---|
| Stable idle | 30 s | Explorer and compositor running, no injected input |
| Mouse motion | 30 s | Reproducible QMP HID/PS2 mouse path, known positions/velocity |
| Window dragging | 30 s | Repeatable pointer drag over one real Explorer window |
| Two-window interaction | 30 s | Focus changes, overlap, close/reopen or move |
| Theme comparison | 30 s each | Base style, WhiteSur light/dark, blur enabled/disabled only if installed |
| Input under background load | 30 s | Deterministic, bounded CPU/I/O workload with interactive operations |

Collect: min/median/P95/P99/max frame interval and compose duration; missed 16.67/33.33 ms targets; input latency; scheduler ready latency; CPU time by thread; spin/idle time; actual presents and bytes; failed/outstanding GPU commands; update region area; memory/fault statistics. Record stdout/serial/QMP evidence separately from counters.

**Environment ledger:** Git SHA, toolchain, CMake flags, `MICANT_PERF_LEVEL`, QEMU and OVMF versions, TCG vs KVM/HVF/WHPX, vCPU/RAM/display/graphics settings, screen resolution, refresh contract, host CPU, host load and exact QMP replay sequence. Do not compare different accelerator types as a single baseline.

QEMU TCG measurements quantify the emulated configuration; they do not predict bare-metal performance. Avoid claiming a physical monitor refresh rate or true input-to-photon latency from QEMU software screenshots.

## 10. Immediate improvement candidate: eliminate compositor busy-spin (PERF4)

**Gate:** measure CPU time in idle polling and actual wake/scheduling latency first.

Replace repeated `pause` polling of `frameSequence` with **event-driven wait/wakeup** using the canonical Ke dispatcher object exposed through the proper user-mode NT wait ABI. Producer publishes metadata/surfaces with correct memory ordering, then signals the compositor wake event. The compositor wakes, consumes the latest coherent generation(s), and blocks again. Timed wakeups are required only for animation deadlines, recovery checks or explicit refresh pacing.

Correctness:

- Avoid **lost wakeups**: atomically re-check generation after registering wait/arming the notification, and use correct auto-/manual-reset semantics.
- Coalesce redundant frame requests but never miss a generation that requires an update.
- Preserve a robust sequence publication protocol and coherent surface snapshots. An acquire load of `frameSequence` alone does not guarantee no producer mutation mid-compose; adopt validated double-read/retry or immutable per-generation ownership where appropriate.
- Avoid sleeping in ISR/DPC or taking kernel locks across user notifications.
- Handle process death, compositor restart, device loss, absent consumers and event handle cleanup.
- A timed fallback should not become another short-interval polling loop.

**Acceptance:** idle compositor becomes genuinely blocked rather than spinning, CPU accounting demonstrates reduction, and mouse input/wake latency remains within an observed acceptable range. Measure before and after, including regressions under load.

## 11. Damage tracking and partial composition (PERF5)

Keep a **single authoritative damage representation** from UI painting to surface publication to compositor to graphics driver, with checked coordinates and explicit generation/ownership.

- Merge rects conservatively; cap rect count and area, switch to full frame only when measured cheaper or required.
- Compute damage from moving windows including **old and new bounds**, shadows, rounded corners, occlusion changes and exposed regions.
- Cursor moves damage old and new cursor bounds, unless a real hardware cursor plane bypasses the composed frame.
- Theme switch, wallpaper change, screen mode or device reset can force full redraw.
- Preserve composition correctness for translucent windows: damage may depend on underlying surface changes.
- Do not copy or clear unaffected framebuffer pixels in partial mode; use retained frame with explicit validity.
- Design graphics present ABI to accept bounded dirty rects only if the backend genuinely honors them. If VirtIO path always copies full frame, preserve accurate accounting and avoid claiming end-to-end partial present.
- Use coherent front/back surface ownership and fences; never modify a buffer still in use by device.

Test: moving overlapping windows, alpha blending, shadow/blur and cursor crossings against a known-correct full-compose reference frame (pixel-perfect or documented tolerance), including out-of-bounds/negative coordinates and malformed dirty inputs.

## 12. Scheduler and input responsiveness (PERF2/PERF7)

Instrument Ke/dispatcher before altering scheduling policy:

- Time Ready → Running per thread, reason and priority bucket.
- Track context switch, preemption, wakeup origin, lock hold (when safely measurable), DPC/IRQ execution and timer wakeups.
- Observe compositor vs UI thread CPU time and starvation; distinguish runnable waiting from blocking on device/IPC.
- Respect canonical NT 0–31 priorities and existing ready queue ownership; do not add a second QoS scheduler.
- Implement any bounded interactive priority boost only after proving a wake latency problem, with correct decay and regression tests.
- Never permanently elevate arbitrary GUI threads to real-time priority or poll at high priority to hide a missing wakeup mechanism.

Input tracing must include original QMP/driver event and actual Win32k dispatch, not artificial fixed timestamps. Account for HID report batching and mouse coalescing.

## 13. Graphics driver and memory/copy instrumentation (PERF2/PERF7)

Instrument native graphics service, VirtIO-GPU and available GOP/physical fallback:

- Surface bytes published, copied, mapped and submitted.
- DMA/memory pinning, transfers, queue occupancy, outstanding descriptors, errors.
- Time from user `0x81` enter/exit, kernel validation and capture, driver submit and completion.
- Resource creation/destruction and fence wait times.
- Queue timeouts and reset/recovery.
- Present interval and burstiness.

Identify where copies occur and who owns source/destination buffers before changing ABI or making zero-copy claims. Use checked pin/unpin and lifetime, no direct untrusted user buffers to DMA. After correctness, evaluate incremental transfer and buffer reuse as measurable optimizations.

**Hardware acceleration claims:** VirtIO 2D scanout, `TRANSFER_TO_HOST_2D` or resource present do not prove GPU-accelerated blending. Report CPU composition and device presentation separately.

## 14. Memory, filesystem, font, image, theme and startup measurements

After frame/input paths, add minimal counters to other canonical owners:

- **Mm:** PFN free/used, fragmentation proxy, committed pages, faults by reason, shared mappings, COW and allocation failures; avoid expensive scans per event.
- **Io/FsRtl:** request throughput/latency, queueing, page-cache hit/miss where implemented, file-backed image load timeline.
- **Ps/loader:** process startup, image mapping, DLL import/initializer times, thread initialization.
- **Fonts/images:** decode/rasterize/shape latency, bytes decoded, cache hit/miss and evictions.
- **Theme engine:** CSS parse/compile only on theme load, style cache lookup, repaint invalidation, blur/shadow regions and CPU time.
- **Security/registry/network:** targeted counters when suspected; no detailed probes everywhere by default.

Instrument allocations, cache misses and per-event work rather than reporting a single huge “system time.” Expand subsystem probes only as measurements establish need.

## 15. Optimization discipline and regression policy

Before each optimization:

1. Capture repeatable baseline at the same perf level and workload.
2. Identify the most expensive stage, with uncertainty and counter evidence.
3. State a falsifiable hypothesis (e.g. eliminating idle spin reduces idle CPU without worsening P99 wake latency).
4. Implement the smallest change in the canonical owner, including error/cleanup paths.
5. Run correctness and safety regressions before performance comparison.
6. Compare baseline/after using identical VM and host conditions and multiple repetitions; report medians and dispersion.
7. Retain or revert only the measured change, avoiding wide unrelated refactors.
8. Record before/after plus any regressions in the evidence ledger.

**Illustrative targets, not current achievements:** for a 60 Hz output, 16.67 ms period is a reference budget; count overruns at >16.67 and >33.33 ms, report P95/P99; target negligible compositor CPU while truly idle; investigate rather than conceal missed frames. Set quantitative acceptance thresholds only after baseline on a stable hardware/VM profile. Performance gates must report noisy/inconclusive samples rather than arbitrary pass/fail.

Suggested optimization order (subject to baseline): eliminate spin → event-driven publish/wakeup → eliminate redundant presents → dirty regions → buffer ownership/copy reduction → scheduling boosts when justified → CPU SIMD after profiling → GPU accelerated backend once real support exists.

## 16. Test matrix and fault injection

**Unit/hosted:** record encode/decode, saturating counters, monotonic timestamp conversions and overflow, ring overwrite/loss, histogram percentile approximation, correlation ID reuse rejection, parser version errors, multi-producer visibility with concurrency stress, damaged-region clipping/merging.

**Freestanding kernel:** IRQL-safe probes, bounded ring initialization and shutdown, per-CPU safety, access control on read API, failed allocation fallback, proper process/driver cleanup, scheduler instrumentation without state corruption.

**QEMU integration:** boot modular UEFI kernel, actual Explorer/compositor CPL3 where available, QMP input replay, ring sample extraction, idle/drag/multi-window traces, real user `0x81` present path, VirtIO queue completion; compare runs at PERF 0/1/2 and with traces disabled at runtime.

**Faults:** exporter stopped/crashed, trace buffer full, invalid client parameters, disconnected device, compositor restart, missed events, malformed shared metadata, timer discontinuity, invalid/overlapping dirty regions, high update rate and low memory.

**Physical hardware:** repeat on explicitly identified device after QEMU passes; record firmware, GPU, CPU frequency/clock mode, memory, display, sample count and wall-clock conditions. Never transpose QEMU FPS to physical support claims.

## 17. Acceptance gates (PERF0–PERF7)

| Gate | Deliverable and objectively testable condition |
|---|---|
| **PERF0** | Baseline scenarios captured on an exact SHA with reproducible QEMU config, statistics and limitations |
| **PERF1** | Clock/counter/ring trace facility builds at levels 0–2; overflow, alignment, concurrency and timing validation pass |
| **PERF2** | End-to-end event producers connected in Ke/Ps/Mm/Io/Win32k/compositor/graphics where supported, with bounded overhead |
| **PERF3** | Host-side collector correlates input-to-completion and reports P50/P95/P99, lost samples and unknown boundaries correctly |
| **PERF4** | Compositor idle busy-spin removed, no lost wakeups, actual idle CPU reduction and acceptable measured input response |
| **PERF5** | Correct damage accounting and composition, verified against full-frame reference; fewer pixels processed in targeted scenes |
| **PERF6** | Automated scenario runner/regression report, artifact/version control, same-mode A/B comparison and inconclusive handling |
| **PERF7** | Subsystem optimization batches prioritized by measured costs, with regression tests and before/after reports |

Do not mark the entire project complete upon implementing PERF1 or an optimization patch. Each gate must be explicitly labeled **proposed / implemented / linked / exercised in QEMU / verified**, with evidence for the claimed level.

## 18. Implementation order and ownership / parallel work

**First task:** add this spec and inspect the minimal source call graph. **Second:** baseline unmodified code (PERF0). **Third:** implement PERF1–PERF3. **Fourth:** fix the highest-impact demonstrated bottleneck, likely investigating the compositor loop and full-frame work, without predetermining the outcome.

Parallelizable independent batches (only when file ownership is disjoint):

- A: HAL clock and core ring/counter ABI; owns shared/clock and perf infrastructure.
- B: Ke/Ps/Mm/Io event sites; depends on A's frozen ABI, owns kernel call sites.
- C: Win32k/Explorer/compositor frame correlation and wakeup; owns graphics user-mode path.
- D: VirtIO-GPU instrumentation and transfer accounting; owns graphics service/driver.
- E: QEMU scenario replay, trace analysis and documentation; owns host tooling/tests.

The integrator coordinates shared ABI changes and commits only coherent increments. Follow current user instructions for branch, commits and testing. Respect existing worktree and `AGENTS.md`; no destructive commands or independent commits to a shared working tree without serialization.

## 19. Evidence report template

Every performance report must record:

```text
MicaNT PERF RUN
Git SHA / dirty state:
Build + MICANT_PERF_LEVEL:
QEMU / accelerator / OVMF / vCPU / RAM / GPU / resolution:
Host details and load:
Scenario / replay seed / duration / repetitions:
Clock source / calibration quality:
Frames produced / submitted / completed / dropped:
Frame interval P50 / P95 / P99 / max:
Compose CPU time P50 / P95 / P99:
Input-to-dispatch and ready-to-run P50 / P95 / P99:
Present syscall / device completion P50 / P95 / P99:
Pixels processed / bytes copied / full vs dirty updates:
Thread idle / spin / runnable CPU time:
Lost trace events / clock discontinuities / unsupported metrics:
Status: implemented / booted / QEMU-measured / hardware-measured
Before/after comparison and confidence limitations:
Known regressions / next bottleneck:
```

Never populate absent data with zeros or guessed values. Use `N/A — not observable` and explain why.

## 20. Explicit exclusions

Do **not** add internet-facing telemetry, upload traces, inspect user content, create an alternate kernel service or replace Ke/Win32k, require debugging output to keep the desktop alive, force GPU acceleration as a prerequisite, claim 60 FPS from static screenshots, infer actual scanout from a VirtIO queue ACK, or stop at adding counters without executing the end-to-end experiment.

## 21. Definition of done

The first useful release is achieved when the exact QEMU scenario reveals **where** time is spent, makes the idle compositor block instead of burn CPU if measured to be at fault, and produces a before/after comparison of responsiveness without sacrificing correctness. The full performance initiative remains open until all acceptance gates are backed by code and evidence, prioritized regressions are resolved, and continuous local performance tracking is practical.

**Operating principle:** **Measure → attribute → optimize → remeasure → retain only proven improvements.**
