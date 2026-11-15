# LuceOS Task Manager and System Monitor

## Purpose and current state

This specification describes an incremental path from the current `taskmgr.exe` placeholder to a native Ring 3 diagnostic and process-management application. LuceOS is the migration name; implementation must use the repository's existing `micant` namespaces and canonical modular kernel until the migration is complete.

The current application in `tools/taskmgr_app.cpp` draws one fixed-size surface, presents it, prints fabricated process, CPU, memory and machine values, then exits. It has no refresh loop, process snapshot request, real controls, navigation, graph history, or process action. `kernel/ex/sysinfo_taskmgr.cpp` currently only calls `InitializeSysInfoTaskmgrExports()`. Its header contains hard-coded sample data and an unrelated synthetic scheduler/resilience engine; these are not production telemetry and must not be used. No feature is considered supported until it is connected through the production Ring 3/kernel path and demonstrated in QEMU.

The kernel milestone ledger does not define a Task Manager milestone. This work is an application integration effort with dependencies on the partial M4/M6/M7 kernel lifecycle, GUI/session services, and the performance instrumentation specification. Each slice below states its own gate; historical integration notes are evidence only.

## Architectural constraints

- `taskmgr.exe` remains a PE Ring 3 program using documented NT/Win32 contracts implemented by the canonical services.
- Kernel data is obtained through existing or correctly implemented native syscall APIs. No user DLL includes `micant/km` headers and no Task Manager-only process table or telemetry engine is introduced.
- Public NT structures, information classes, access masks, status codes and ownership must match Microsoft contracts. Unsupported classes and fields fail or display unavailable; they never return fabricated success/data.
- Snapshots hold referenced process/thread objects while collecting data, tolerate concurrent create/exit, never expose kernel pointers, validate output buffers, and release references on every path.
- Per-process actions operate on an authorized object handle, not a PID as authority, so PID reuse does not redirect an action. Se/Ob access checks and protected-process policy remain authoritative.
- Measurements are opt-in application sampling, with no background telemetry or user tracking. Sampling and presentation are separate, bounded, timer-driven, and reduced while minimized.
- Preserve existing local changes, UEFI handoff, AMD64 paging, PFN allocator, syscall entry, GUI and driver paths. Never use `old/` as implementation material or link input.

## Existing components to inspect and reuse

| Area | Canonical starting points | Current known limitation |
|---|---|---|
| Task Manager | `tools/taskmgr_app.cpp`, `tools/gui_common.hpp`, native app targets in `CMakeLists.txt` | Static demo, fixed rows and values, exits immediately |
| System query | `kernel/ex/sysinfo_taskmgr.cpp`, `include/micant/km/executive/sysinfo_taskmgr.hpp`, syscall dispatch and NTDLL declarations | Export initializer only in the `.cpp`; header is synthetic and not evidence of a live query path |
| Process/thread ownership | `kernel/ps/`, `include/micant/km/executive/ps.hpp`, Ob process/thread object integration | M4 lifecycle remains partial; inspect exact object/query APIs before adding any contract |
| Scheduler accounting | `kernel/ke/`, `include/micant/km/core/` | Do not infer online CPU count from CPUID; inspect counters and clock source |
| Memory accounting | `kernel/mm/`, PFN database and process address-space owner | Do not equate reservations, commit and resident physical pages |
| Handles/security | `kernel/ob/`, `kernel/se/`, process/thread handles and tokens | PID alone does not authorize management |
| I/O and devices | `kernel/io/`, `kernel/fsrtl/`, storage and network owners | Provider coverage must be explicit per device/interface |
| GUI | `include/micant/um/subsystems/winuser/`, GDI, theme engine and compositor | Current Task Manager does not use a normal interactive window/message loop |
| Sessions/login | `tools/winlogon_app.cpp`, `tools/logonui_app.cpp`, `include/micant/um/servers/winlogon.hpp`, session manager | Current applications explicitly report missing authenticated IPC/secure desktop/session lifecycle |
| Performance framework | `docs/KERNEL_PERFORMANCE_INSTRUMENTATION_AND_OPTIMIZATION_SPEC.md`, existing provider owners | Reuse its counters; do not create a second telemetry subsystem |
| QEMU | `qemu.py`, QMP automation and serial/screenshot evidence | Existing tests may prove only bootstrap behavior; each result must identify the exact target and scenario |

This inventory is a starting point, not a claim that every path is linked to `ntoskrnl_core_baremetal`. At each slice, verify the actual build graph and syscall/export route.

## API and data contracts

### Process and thread snapshots

Prefer the public `NtQuerySystemInformation(SystemProcessInformation)` contract for enumeration, with `NtQueryInformationProcess` and `NtQueryInformationThread` for details where those classes are implemented. Use shared mode-neutral ABI declarations and the existing NTDLL syscall boundary. The owner is Ps/Ob for object snapshots and syscall plumbing; Ex owns the system-information dispatch only where repository architecture already places it.

The first supported snapshot should provide only fields backed by live owners: process ID, parent ID, image name, thread count and lifecycle state. Add session ID, SID, creation time, user/kernel time, working set, private commit, handle count, I/O counters, context switches, base/dynamic priority and exit status only as their owners provide consistent accounting. Follow the documented variable-length record layout, alignment, `STATUS_INFO_LENGTH_MISMATCH` sizing behavior, and return-length semantics. Validate pointers, lengths, integer overflow and user address ranges before writing. Take a stable referenced-object snapshot, collect outside global locks, and unwind all references on allocation or copy failure.

For detail/action calls, open the process/thread object with the requested access using Ob and Se. Termination, priority and affinity require their documented rights and privilege/policy checks. If a contract is not implemented, return an explicit unsupported/access error rather than success. Handle process termination races through object references and rundown; callers must close handles.

### Resource counters

CPU percentages are computed in Ring 3 (or the established performance provider) from monotonic deltas between coherent 100 ns counters. Preserve idle, kernel, user, DPC and interrupt totals independently; clamp/mark invalid on counter reset, overflow, clock discontinuity or zero elapsed time. Include only processors actually online. Per-process CPU is a delta of that process's user+kernel time; system-wide counters must reconcile within documented sampling limits.

Memory fields are distinct: installed/usable physical bytes, available/free/standby pages, resident working set, private commit, system commit and commit limit. Mm supplies counts and Ps supplies process working-set/commit ownership. If a field is not accounted, return a documented unsupported status or render “Unavailable”.

Disk counters belong to the storage/Io device owners; network counters belong to the interface/TCPIP owner; GPU counters belong to the actual graphics driver/compositor. Report bytes, operations, errors and latency only when measured by those providers. A missing provider is “Unavailable”, never zero activity. GPU memory/queue/frame/present metrics remain unsupported until the driver tracks them.

## Functional roadmap and ownership

Work proceeds through these independently reviewable macro blocks. One integrator owns `main`, stages only task files and makes commits. Parallel workers must claim disjoint files before editing; shared ABI, syscall dispatch, build graph, `AGENTS.md`, and status-ledger changes are serialized.

| Block | Owner | Main files/owners | Deliverable and gate |
|---|---|---|---|
| 0. Specification and baseline | Integrator | This document; QEMU scripts/logs | Inventory, dependencies, status semantics and test plan agreed before code changes |
| A. Live process snapshot | Ps/Ob/Ex + NTDLL integrator | Canonical Ps/Ob query owners, `kernel/ex/sysinfo_taskmgr.cpp`, shared NT ABI, syscall/NTDLL declarations | Real process/thread rows via user query; correct sizing, validation, concurrent exit cleanup; QEMU shows actual process set and detects a launched/terminated fixture |
| B. Performance providers | Ke/Mm/Io/TCPIP/GPU owners | Existing performance instrumentation/provider files; shared query ABI only by agreement | Live CPU and memory first; storage/network next where counters exist; GPU explicitly unavailable without provider. Delta graphs and overhead budget measured against identical QEMU baseline |
| C. Interactive UI | Win32k/User32/GDI/theme and Task Manager owner | `tools/taskmgr_app.cpp`, GUI/common-control/theme owner files | Resizable navigable Processes/Performance/Details views, search/sort, timer refresh, graph ring buffer, theme fallback; no hard-coded rows or fabricated counters |
| D. Process management | Ps/Ob/Se/NTDLL + UI owner | Existing process syscalls/object access APIs and Task Manager actions | End task/process, priority and affinity only where contracts exist; object-handle authority, real denial reporting, PID-reuse safety and cleanup |
| E. Secure keyboard and Winlogon | Input/HAL + Winlogon/session + GUI owners | Trusted keyboard path, session/secure desktop owner, Winlogon and UI launch path | Ctrl+Shift+Esc launches once per chord from system input path. Ctrl+Alt+Del is SAS routed to secure desktop with Task Manager option; no hotkey registration substitute or fake authentication |
| F. Sessions, startup and services | Session Manager/Userenv/SCM + UI | Existing server and registry/service owners | Real session data first; startup entries and service controls only when their canonical providers support enumeration and authorized mutation; otherwise clearly unavailable |
| G. Integration, fault and performance verification | Integrator + subsystem owners | `qemu.py`, QMP helpers, targeted regressions, evidence ledger at integration boundary | Bare-metal target linked; process churn, CPU load, actions, shortcuts, Explorer stall, provider absence and cleanup tested with serial/QMP/screenshots. Publish measured overhead and unsupported list |

## UI and theme behavior

Use the existing Win32 window, message, paint, control, font, icon and theme services. Do not build a second window manager or graphics stack. The base theme is mandatory; Light/Dark/Auto, accent, DPI scaling, rounded corners and transparency are enabled only where the shared Theme Engine/compositor exposes them. Layout adapts to window size. Navigation includes Processes, Performance, App history, Startup apps, Users, Details and Services; unsupported pages explain the absent provider rather than render placeholder rows.

Processes and Details share the same live snapshot model. Sorting and search run on the cached snapshot, not through repeated syscalls. A timer requests only changed measurements at a configurable cadence; Pause freezes display sampling, minimized windows reduce or stop refresh, and process exit removes rows without retaining object references. Graphs use bounded circular buffers. UI timing percentiles are shown only if the Performance Instrumentation Framework exposes them.

## Secure input and session behavior

Ctrl+Shift+Esc is recognized by the trusted system input/session path, including modifier transitions and key-up, with repeat suppression. It launches `taskmgr.exe` in the current interactive session independently of Explorer. Ctrl+Alt+Del is recognized before ordinary application routing and delivered as a system SAS to Winlogon on a secure desktop. Task Manager launch uses the session's authorized token. Lock, user switch, sign-out and password change appear only if implemented by session/authentication services; no synthetic authentication is permitted. If secure desktop infrastructure is absent, SAS-dependent controls must fail closed and the feature remains incomplete.

## Error handling, consistency and overhead

- Distinguish unsupported provider, access denied, process exited, retry/resize required, allocation failure and malformed user buffer.
- Use bounded allocation and arithmetic; never hold Ps/Ob global locks while copying to user memory or rendering.
- Sampling does not suspend the system. On query failure, retain the last valid sample with a stale/error indicator; do not replace it with zeros.
- Detect counter wrap/reset and clock discontinuity before percentage calculation. Keep raw counters separate from derived percentages.
- Measure snapshot latency, CPU sample duration, UI refresh/draw duration, Task Manager CPU/working set, syscalls per sample and compositor frame impact. Establish baseline and monitored-run measurements with the same QEMU machine, workload and refresh interval before setting an overhead budget.
- No synthetic fault/heal injection in production UI; fault injection belongs in dedicated tests with explicit test-only controls.

## Verification plan and acceptance

At every macro-block boundary, use the narrowest available regression and `python3 qemu.py --headless` with serial/QMP evidence. For behavior claims requiring runtime integration, link `ntoskrnl_core_baremetal` and exercise actual PE Ring 3 applications. Use QMP to launch/terminate controlled processes, induce CPU load, inject keyboard chords, stall Explorer, and capture screenshots. Record the exact build, QEMU invocation, log, and observed process/action/status. Run related Ps/Ke/Mm/Ob/Win32k and user-mode include-boundary regressions. Fault cases include concurrent process exit during enumeration, undersized/invalid buffers, PID reuse, denied rights, missing disk/network/GPU providers, counter reset, memory pressure, UI close during sampling, and repeated shortcut key events.

Completion requires all of the user's listed criteria to be implemented and demonstrated: live process and supported resource data, functional Processes/Performance/Details pages, authorized actions, session/startup/service integration where providers exist, secure Ctrl+Shift+Esc and Ctrl+Alt+Del flow, theme fallback, bounded overhead and cleanup. Any unsupported service/provider must be identified explicitly. Passing a build, static test, hosted test, screenshot of a static screen, or boot log alone is not end-to-end acceptance. Update `docs/KERNEL_INTEGRATION_STATUS.md` only at a meaningful kernel integration boundary; this document does not redefine kernel milestone scope.

## Initial implementation order

1. Remove synthetic production values and establish the smallest real `SystemProcessInformation` query using the existing Ps/Ob object model; verify actual process rows and sizing/failure paths.
2. Add real CPU and memory counters from Ke/Mm, then calculate time deltas in the application and render live history.
3. Build the interactive UI on the existing GUI/event path; then add authorized process management.
4. Implement the trusted shortcut/SAS session path and prove both with QMP, accounting for the currently incomplete Winlogon/secure-desktop infrastructure.
5. Connect storage/network/GPU, app history, startup, users and services only to real owners; publish unavailable states for gaps.
6. Run the integrated acceptance and stress/fault suite, measure overhead, update evidence, and commit each coherent verified block.
