# MicaNT — Resource Groups and Quotas Specification

**Status:** Proposed; not verified  
**Primary owners:** Ps, Ke, Mm, Ob; later Io and network  
**Related:** `KERNEL_PROCESS_SILO_SPEC.md`, `KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md`, `KERNEL_MEMORY_PRESSURE_PAGING_SPEC.md` (when present)

## 1. Purpose

Implement kernel-enforced resource accounting and limits for groups of processes. This is a reusable NT Executive feature, not a duplicate cgroups subsystem and not intrinsically tied to Docker. A resource group may be bound to a process silo or used by a host policy. The first version supports hard process/thread counts and memory commitments. CPU and I/O budgets follow only after measured canonical infrastructure exists.

## 2. Core semantics

- A group has an Ob-managed lifetime, opaque ID, trusted policy owner and reference-controlled membership.
- Resource usage is **charged before publication** and uncharged on the exact owning resource's final release; failure paths roll charges back once.
- A limit is enforced against the group's current counter plus requested charge with overflow-safe arithmetic.
- Reservations, committed resources, resident pages and shared ownership are distinct concepts. Do not conflate commit charge with PFN count.
- No resource is charged twice due to a clone, handle duplication or page alias, unless the chosen charge policy intentionally bills both and is documented.
- Requests exceeding a hard limit fail with an appropriate real status; quotas never generate fake success or silent overcommit.
- Limits cannot be weakened by unprivileged members; group migration and policy changes are privileged and transactional.
- First release is flat. Hierarchical accounting is an extension with separate design for nested counters and lock ordering.

## 3. Proposed resource classes

| Resource | Initial guarantee | Future extension |
|---|---|---|
| Process count | Hard maximum on creation | Nested group accounting |
| Thread count | Hard maximum on creation | Per-class/thread priorities |
| Commit memory | Hard committed-byte charge | Working-set cap and reclamation |
| Resident memory | Observation only until Mm accounting is verified | Soft/hard resident targets |
| CPU time | Monotonic measurement per group | Periodic budget, throttling, weight |
| I/O | Counters for bytes/ops | Async scheduling and bandwidth budgets |
| Network | Counters per endpoint/group | Throughput shaping and connection caps |
| Handles | Observability initially | Limits where Ob owns correct global cost |

## 4. Accounting design

A process holds an immutable or privileged-transitioned group binding. Group counters should derive from the canonical owner of each resource: Ps for process/thread lifetime, Mm for commit and PFN accounting, Ke for CPU runtime, Io for dispatched/completed I/O, network for packet/socket accounting. Do not infer usage from periodically enumerating processes.

Define precisely the charging point and release point for every resource. Examples:

- Process: reserve count before any externally visible process publication; release after final process lifetime/rundown, not merely when a handle closes.
- Thread: count at successful thread publication and release at final thread object teardown; termination vs reaping distinction must be explicit.
- Memory: reserve commit on successful committing operation; rollback failed mappings/clones; decommit on final ownership release. Share policy must avoid accidental global charging anomalies.
- CPU: increment using scheduler-observed runtime deltas, with monotonic timestamps and overflow handling; do not attribute idle work to a user group.
- I/O: count bytes actually transferred and distinguish submitted, completed, cancelled and failed operations.

### Hard and soft policy

Hard limits block/rescind new allocations, not forcibly free in-use kernel resources. Soft limits can signal local pressure and ask Ring 3 policy to respond; they must never violate host reserves or security invariants. For CPU quota, define a budget window and a throttle mechanism that preserves deadlines of essential kernel cleanup workers.

## 5. Concurrency, IRQL and performance

Counters must use appropriate atomics or owner-specific locking. Require bounded updates on hot paths and avoid a global lock on every page fault or interrupt. Overflow checks, counter underflow detection and final accounting assertions are mandatory. Never run arbitrary callbacks while holding quota locks. Contended quota reservations must not deadlock with Ps/Mm/Ob teardown. Do not allocate memory to perform a mandatory accounting operation in an out-of-memory path.

Provide a versioned read-only snapshot interface for managers; samples may be slightly delayed but must not misreport negative counts or silently reset. Expose counters with clear units, sampling windows and meanings.

## 6. Policy and security

Only authorized host managers may create or alter groups, change limits, move a process or grant exceptions. A silo member cannot reset its counters by opening new handles, moving threads or forking. Bind management handles through Ob/Se checks; prevent cross-silo quota manipulation. Container shutdown must not affect unrelated groups.

## 7. Incremental phases

- **R0 — Accounting contract:** define ownership/charge/release points, add local assertions and host fault-injection tests.
- **R1 — Process/thread hard limits:** enforce in canonical Ps, test success, denied creation, rollback and exhaustion in QEMU.
- **R2 — Memory commit limits:** enforce at Mm commit/clone/section paths, test decommit, COW, fault failure and teardown.
- **R3 — CPU usage and measured scheduling:** accurate per-group CPU metrics; only then implement bounded weighted or budget policy if Ke supports it.
- **R4 — I/O and network accounting:** track cancellable operations, reset, teardown and per-interface stats without replacing Io/TCPIP.
- **R5 — Stress/SMP:** simultaneous reservations, racing close/create, group deletion and CPU migration.

## 8. Acceptance matrix

1. A group with process limit 2 starts exactly two processes and rejects a third with a real status; after final teardown, it can start another.
2. Thread count is consistent under immediate cancellation, abnormal exit, delayed reaper, duplicated handles and failed copyout.
3. Commit memory limit enforces actual commit and restores quota after decommit/process death; shared page mappings have documented charge behavior.
4. CPU accounting increases only for scheduled member threads, remains monotonic and agrees with independently observed run windows within a documented tolerance.
5. I/O byte counters reflect actual completed data, not requested bytes; timeout/cancellation preserves correct totals.
6. Concurrent updates never underflow, overflow, leave stuck reservations, or block kernel shutdown.
7. A non-member cannot edit group configuration or query restricted member details.
8. The normal BSP QEMU regression suite and memory/handle leak checks pass; SMP tests are separately gated.

## 9. Non-goals and scope discipline

No new scheduler, page allocator, cgroups filesystem, Docker daemon or userspace policy engine inside the kernel. Container orchestration uses these kernel controls from Ring 3. Do not call resource groups complete based on counters alone: at least process/thread and memory limits must be enforced in actual production paths.

## 10. Done criteria

A versioned, security-checked group can be created, assigned to processes and used to enforce process/thread and memory commitment limits while the canonical kernel recovers all quota charges on success, failure and teardown, demonstrated in QEMU. CPU/I/O/network features are individually marked pending until their own acceptance tests pass.
