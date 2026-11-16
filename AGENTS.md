MicaNT Engineering and Agent Execution Rules
These repository instructions apply to coding agents working on MicaNT. Read the directly relevant implementation and specifications before changing code. docs/KERNEL_MILESTONES.md defines the current milestone IDs and acceptance scope; docs/KERNEL_INTEGRATION_STATUS.md records evidence and historical work, not a second milestone definition.
1. Mission and architecture
MicaNT is an AMD64/x86_64-only, C++23, NT-compatible operating system. The production target is the single canonical modular kernel (ntoskrnl_core and its ntoskrnl_core_baremetal build), started through UEFI. The monolithic implementation under old/ is archival and must never be built, linked, staged, or used to satisfy missing symbols.
- Complete existing canonical modules (Ke, Mm, Ob, Ps, Io, Se, Cm, etc.) instead of introducing competing managers, tables, schedulers, or host-only substitutes.
- Reuse working MicaNT code. Make changes in the subsystem that owns the functionality, including necessary caller, callee, ownership, failure, and cleanup paths.
- Preserve the UEFI handoff, PFN allocator, AMD64 paging, CPL3 boundary, SYSCALL entry, kernel runtime, and functioning drivers/tests. Changes to these paths must not silently weaken existing invariants.
- Do not introduce ARM64 or other architecture paths, arbitrary hosted dependencies, or a new kernel ABI.
- Keep implementation readable and idiomatic C++23; use explicit resource ownership, bounded arithmetic, and synchronization appropriate to the current execution/IRQL context. Avoid cosmetic rewrites and unrelated features.
- Preserve zero-telemetry behavior.
2. Windows compatibility and references
- Treat public Microsoft Windows/NT/WDK contracts as the required external behavior. Use official win32metadata for Win32 declarations where applicable and Microsoft Learn/WDK for documented NT behavior. Match public names, exports, calling conventions, structures, status codes, access checks, lifetimes, and error semantics. Never invent public Windows API names or return synthetic success for unimplemented behavior.
- Study the corresponding ReactOS source from the local sibling repository ../reactos when it can resolve implementation details; Wine and other public implementations may help with user-mode behavior. Compare first with MicaNT's existing implementation, then integrate the relevant behavior into the canonical MicaNT design.
- Respect third-party licenses, notices, and provenance. Do not import confidential or leaked code, or misrepresent borrowed code as original. Follow CONTRIBUTING.md, docs/CLEAN_ROOM.md, and docs/LEGAL.md for contribution requirements.
- When a contract is uncertain, investigate the specific point rather than converting uncertainty into a general project-wide research phase. Unsupported variants must fail explicitly and correctly.
3. Strict Ring 0 / Ring 3 separation
- User-mode DLLs, including NTDLL, Kernel32, KernelBase, Advapi32, User32 and GDI32, must not depend on private micant/km/ headers or instantiate kernel Executive objects.
- Place shared ABI declarations in mode-neutral micant/shared/ interfaces. Reach kernel services through the native syscall boundary or another established NT subsystem interface.
- Hosted adapters are for isolated tests and must not masquerade as freestanding NT services. A host test passing is not proof of QEMU, Ring 3, process, or driver integration.
- Preserve transitive include-boundary checks for native user-mode targets.
4. Task completion is not batch completion
The user's stated objective, not an individual patch, test, commit, or status message, defines when the coding task is complete. A functional batch is only an internal work unit.

When assigned a milestone:
1. Inspect the current worktree and preserve existing modifications. Read the current milestone row and only the sources needed for its open requirements; avoid repeated full-repository audits.
2. Identify the shortest dependency chain to a real end-to-end implementation, including dependent work in another canonical subsystem when necessary.
3. Implement that path, covering success, failure, rollback, access validation, state transitions, and cleanup. Do not replace a difficult core requirement with an easier adjacent microtask.
4. Continue into the next unsatisfied acceptance requirement within the same run while execution remains available. Treat progress messages and local checkpoints as informational, not as completion events.
5. State completion only if every required acceptance item is implemented. State verification separately; unexecuted tests mean implemented but unverified, never verified.
Never stop solely because a small edit is finished, an issue is understood, a test passes, a commit is possible, a checkpoint was written, or the next dependency is complex. A missing dependency is normally the next implementation target, not a reason to stop.

Do not claim that an instruction can override execution-environment limits. If the environment actually ends the run or genuinely blocks further work, preserve a precise resume point and report the concrete limitation. Do not mislabel an ordinary engineering problem as an environment limit.
5. Blocker and dependency policy
Before treating a requirement as blocked:
- Identify the exact function/interface/invariant and why the current implementation cannot proceed.
- Examine the existing MicaNT owner and its call sites; consult the narrow ReactOS/WDK reference if needed.
- Implement the smallest real missing dependency within the requested scope and reconnect the original path.
- If one dependency is genuinely blocked, move to another independent required part of the same milestone; do not switch to decorative features or unrelated tests.
Avoid placeholder returns, hard-coded fake handles/PIDs/CR3, artificial success, or duplicate bootstrap-only implementations as substitutes for canonical integration. A bounded implementation may be useful, but must be described honestly and must not be called general NT support.
6. Test, QEMU, and commit policy
The current user's explicit instructions govern whether and when builds, tests, QEMU, documentation updates, and commits occur. Generic repository test recommendations are defaults, not permission to disregard a no tests, no QEMU, or no commits request.
- When verification is explicitly deferred: do not execute build commands, test binaries, sanitizers, QEMU, or architecture verification; continue inspecting and editing code. Keep a short record of checks to run later. Do not report passed tests that were not executed.
- When commits are explicitly deferred: keep all work in the existing worktree; do not create WIP or final commits, reset, clean, or overwrite unrelated local modifications. Do not use a missing commit as a reason to stop.
- When verification is authorized: prefer incremental compilation and targeted tests; run integrated kernel/QEMU checks at meaningful integration boundaries, not after each small function. Reuse persistent CMake/Ninja build directories. Do not rebuild Explorer for unrelated Ke/Mm/Ps work.
- Before calling a bare-metal milestone verified: link the actual ntoskrnl_core_baremetal target and exercise the required behavior in QEMU. A hosted simulation, mere compilation, diagnostic fixture, or log marker is insufficient proof of process/system integration.
- When commits are authorized: commit coherent, attributable changes selectively; preserve user changes and never let a commit terminate the assigned milestone automatically.
Relevant normal-mode commands, only when authorized, include python3 qemu.py --headless, python3 qemu.py --headless --runtime-selftest, python3 scripts/verify_kernel_architecture.py, and narrowly relevant host regressions. Inspect serial/QMP evidence and distinguish environmental failures from code failures.
7. Milestones, status and reporting
- Canonical milestone scope: docs/KERNEL_MILESTONES.md. Any older numbered milestone inventory inside docs/KERNEL_INTEGRATION_STATUS.md is historical; do not reinterpret current M4 as the old Io/syscall M4.
- Evidence ledger: docs/KERNEL_INTEGRATION_STATUS.md. Update it at meaningful integration boundaries or when explicitly requested, not after every minor helper change. Do not use documentation updates as substitute deliverables for implementation.
- A milestone's unfinished items are actionable work, not optional future suggestions. If M4 requires exception dispatch or file-backed SEC_IMAGE for its acceptance path, implement the necessary Ke/Mm/Io/Ob/Ps dependencies rather than declaring those parts out of scope solely because they cross subsystem boundaries.
- Keep status reporting concise and accurate: implemented vs linked vs executed in QEMU vs fully supported, and verified vs unverified. Do not automatically end the task after a progress summary.
- If a run is forcibly interrupted, record the exact in-progress function, files touched, pending worktree changes, known blockers, deferred checks, and the next concrete edit. On resumption, start there rather than repeating a repository-wide plan.
8. End-of-task gate
Before ending a milestone assignment, check all of the following:
- Does the code implement the stated milestone requirements, including necessary dependencies and cleanup?
- Is the production AMD64 modular kernel path connected, rather than only an isolated hosted or bootstrap simulation?
- Are unsupported behaviors explicit, without fabricated success?
- Were the user's restrictions on tests, QEMU, commits, and local changes respected?
- Is the verification status truthful, including any deferred checks?
If an acceptance item remains and a concrete edit can still advance it, continue implementing instead of presenting a final completion report. If an actual runtime limit interrupts the job, clearly report an interrupted task and a precise resume point; do not claim the milestone is finished.

- **QEMU execution:** Always run QEMU commands with elevated sandbox permissions (`sandbox_permissions: "require_escalated"`) from the very first attempt. Never attempt to launch QEMU inside the restricted sandbox. Request authorization if required by the execution environment, and do not fall back to sandboxed execution.

9. Process-memory debugging with procdump and proctop
- Build both native Ring 3 tools with `cmake --build build/qemu-cmake-mingw --target procdump_boot proctop_boot`. The QEMU boot image and `scripts/stage_micant.py` place them in `C:\MicaNT\System32`.
- Use `proctop.exe --output C:\MicaNT\process-sample-01.txt --interval 2 --count 30` to record a bounded series of active-process memory snapshots. `--interval` is in seconds. Without `--count`, proctop continues sampling until the process is terminated. Choose a new output path for each run: the current filesystem profile cannot truncate an existing report. The same tables are printed to the process console and written to the file.
- Each process row reports PID, image name, thread count, handle count, resident working set, and private committed bytes. Use working set to compare per-process resident memory over time. Private commit is committed virtual memory, not resident RAM. The process snapshot does not currently provide a reliable virtual-size counter; do not infer system-wide RAM attribution by summing process working sets.
- Use `procdump.exe --pid 149 --report` for a readable per-process report or `--json` for machine-readable output. Replace `149` with the target PID from proctop.
- Capture memory with `procdump.exe --pid 149 --mini C:\MicaNT\compositor-mini.lud` or `--full C:\MicaNT\compositor-full.lud`. Mini captures selected image, private, and stack ranges; full attempts all committed readable user ranges. A capture is live and best-effort; a missing end marker means the file contains only a partial capture. `.LUD` files are LuceOS diagnostics, not Windows minidumps.
- Decode a dump on the host with `python3 scripts/read_lud.py path/to/compositor-mini.lud`; use `--json` for structured output and `--extract directory` to extract memory blocks. The process-memory QEMU self-test writes `PROCDMP.LUD` and `PROCTOP.TXT` to the persistent `artifacts/process-memory/process-memory-dump-disk.img`; the extracted report files are kept beside that image for inspection.
- For compositor or other process regressions, collect repeated proctop snapshots before and after the change, compare the target PID's working set and private commit, then capture a `.LUD` dump for the target process. Record the exact command, PID, time, and whether the capture was complete with the debugging evidence.

10. Kernel crash dump recovery and analysis
- After a kernel bugcheck, reboot the system and inspect `C:\Windows\Minidump` for `LUCEOS-<identifier>.dmp`. The boot-start AHCI owner exposes only the pre-reserved GPT crash-dump extent to the kernel; the fatal path does not call the filesystem. The Ring 3 `lucecrash.exe` recovery service validates the dump, writes and flushes the file, reads it back, and only then acknowledges the reserved copy.
- A `.dmp` from this path uses the LuceOS `LUCDMP1` format and CRCs shared with `procdump`; it is not a Microsoft Windows minidump. Analyze it on the host with `python3 scripts/read_lud.py path/to/LUCEOS-<identifier>.dmp --json`. Require `complete: true`, an empty `errors` list, `header.kernel_crash: true`, and valid record CRCs before relying on its contents. The crash context reports the stop code and parameters, RIP/RSP/RFLAGS, CR2, GPRs, current PID/TID, bounded stack data when valid stack bounds are available, and the kernel build ID.
- For a QEMU crash-dump self-test, inspect `build/crash-dump-qemu-evidence-*/crash-first-boot.serial.log` and `crash-recovery-boot.serial.log`, then decode `recovered-minidump.dmp` with the same command. The first boot must report `persistent dump committed=1`; the recovery boot must report the validated, flushed, and acknowledged `C:\Windows\Minidump\LUCEOS-<identifier>.dmp` path. The host test also extracts the FAT file from `crash-dump-disk.img` and checks its CRCs and structure.
- If the fatal log reports `persistent dump committed=0`, no persistent dump was saved. Preserve the serial evidence and report that the reserved raw-write backend was unavailable or failed; do not claim recovery, access the reserved extent as an ordinary file, or write arbitrary disk sectors.
