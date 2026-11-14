# MicaNT — AMD64 Scheduling, Priority and Desktop Responsiveness Specification

**Status:** Proposed specification (not an implementation claim)  
**Target:** MicaNT modular NT-compatible kernel, AMD64/x86_64 only  
**Scope:** Ke scheduler, Ps process/thread policy, dispatcher synchronization, I/O completion, input and user-mode compositor  
**Canonical implementation:** `include/micant/km/core/ke.hpp`, `kernel/ke/`, `kernel/ps/`, with architecture-specific mechanisms in HAL AMD64  
**Related documents:** `docs/KERNEL_ARCHITECTURE.md`, `docs/KERNEL_MILESTONES.md`, `docs/KERNEL_INTEGRATION_STATUS.md`

## 1. Purpose

Provide NT-compatible priority semantics and low-latency interactive scheduling. MicaNT must remain responsive while normal applications perform CPU-bound computation, I/O, compilation and background maintenance. The design draws on macOS/XNU *Quality of Service* (QoS) as a **policy inspiration**, not as a replacement for the Windows NT API and scheduling contract.

The kernel is **AMD64-only**. No ARM64, heterogeneous-core, or other-ISA implementation is in scope. The design must extend the **existing canonical Ke priority scheduler**; it must not introduce a second scheduler or an independent QoS run queue.

### 1.1 Non-goals

- Exposing macOS QoS classes as public Win32/NT APIs.
- Guaranteeing that an application with a blocked UI thread remains responsive.
- Declaring Windows scheduler compatibility from internal tests alone.
- Implementing D3D12, GPU residency, full WDDM, SMP, or a compositor as prerequisites to validating the basic scheduling policy.
- Faking performance results, creating fixed priority reports, or replacing hardware interrupt scheduling with hosted thread primitives.

## 2. Baseline and evidence boundaries

At the previously reviewed revision, `KThread` contains `basePriority`, `priority`, and `quantumRemaining`, with 32 priority queues, selection of the highest-priority ready thread, quantum rotation, AMD64 context switches and BSP timer preemption. Check the current source before implementation; the status documents may describe more recent changes.

**Do not infer** from the presence of these fields that dynamic boosts, starvation protection, priority inheritance, correct SMP scheduling, full NT priority-class handling or end-to-end compositor responsiveness already work. Each requires separate integration and tests.

## 3. Compatibility contract and scheduling model

### 3.1 Thread-first scheduling

The runnable unit is a **thread**. A process supplies default priority class, policy and accounting information; individual threads can have different relative priorities and workloads. The scheduler selects runnable threads, not processes as indivisible entities.

Use the NT priority space **0–31** and preserve Windows-facing semantics:

| Range | Intended semantics |
|---|---|
| `0` | Reserved for system/idle-related use; not a general application priority |
| `1–15` | Variable-priority range, eligible for bounded dynamic adjustment |
| `16–31` | Real-time range; no automatic elevation of ordinary interactive work into this range |

Windows **process priority classes** and **thread relative priorities** must be translated according to documented Windows contracts rather than by a made-up API mapping. Maintain the distinction between `basePriority` and effective dispatch priority. Do not change the numerical meaning of exported Windows/NT priority APIs to imitate macOS.

### 3.2 Policy metadata (private to MicaNT)

When needed, augment the canonical thread/process objects with **internal**, non-ABI policy metadata:

- `basePriority`: established from compatible process/thread priority settings.
- `effectivePriority`: priority used for ready-queue placement (existing `priority` may serve this role).
- `boostReason`: optional audited reason for a temporary boost.
- `boostExpiry` or decay state: ensures boosts are temporary.
- `readySince`: timestamp or tick for observing ready latency and starvation.
- `quantumRemaining`: existing scheduling budget.
- `foregroundInteraction` / workload hint: private, validated signal from the windowing subsystem, **not** a trusted arbitrary app privilege.

Only add fields that are needed by a specific policy and can be maintained correctly through the entire thread lifecycle. No duplicated run queue, process manager, or synthetic PID/CR3.

### 3.3 Internal workload hints

The following categories are **advisory implementation policy**. They are not new Windows-visible priority classes and do not imply fixed NT priority numbers:

| Hint | Workload examples | Policy intent |
|---|---|---|
| Interactive | Window input, animation, foreground response | Prompt dispatch when ready; bounded temporary boost |
| User initiated | User-triggered work that blocks a visible operation | Propagate urgency through supported waits and workers |
| Default | Ordinary application code | Fair CPU time within compatible NT priorities |
| Utility | Long-running user-visible work | Lower latency sensitivity; preserve throughput |
| Background | Indexing, cleanup, deferred maintenance | Avoid competing with interactive tasks unnecessarily |

Avoid permanently assigning the compositor or all GUI threads to real-time priority. System-critical interrupt handling remains governed by IRQL and interrupt/DPC rules rather than arbitrary user-mode QoS hints.

## 4. Required Ke scheduling invariants

1. For each online processor, a runnable thread is either **Running** on one CPU or **Ready** on one eligible queue, never both.
2. A non-runnable thread (waiting, terminating, terminated, reaping) must not appear in a ready queue.
3. Each ready queue is ordered consistently with the canonical priority selection rules, preserving FIFO/quantum rotation among eligible equal-priority threads.
4. A priority change of a queued thread must move it **atomically** to the correct queue; updating only a priority field is not sufficient.
5. Dispatching a higher-priority thread must preserve the interrupted thread's register, XSAVE/FXSAVE state, IRQL/interrupt-state and process address-space contract.
6. Never call pageable, blocking, allocating or user-mode code while holding a scheduler spinlock or executing at incompatible IRQL.
7. No thread may be dispatched after termination or while its kernel stack, trap frame, APC state or address space is being released.
8. At high IRQL, record deferred dispatch rather than switching in an unsafe context. Complete dispatch at a documented safe point.
9. User-controlled policy inputs may request hints, but must not grant real-time privileges or bypass explicit NT access checks.
10. On SMP extension, all transitions need explicit ownership, memory-ordering, processor affinity, per-CPU state and reschedule-IPI rules; BSP-only tests do not establish SMP correctness.

## 5. Scheduling mechanisms

### 5.1 Wakeup preemption

When a thread becomes Ready, compare its effective priority with the currently running eligible thread. If strictly higher and switching is safe, preempt promptly; otherwise request dispatch at the next valid boundary. Do not require the currently running application to call `yield` voluntarily.

Wakeups may originate from input, event signaling, timers, I/O completion or dispatcher objects. They must converge on the **same canonical enqueue/dispatch path**.

### 5.2 Dynamic boost and decay

For threads in the **variable-priority range only**, introduce bounded, event-driven boosts where consistent with NT compatibility behavior. Inputs may include documented I/O completion, selected dispatcher waits and foreground-interaction policy.

A safe starting algorithm is:

1. Determine the documented/allowed boost for the wake reason.
2. Compute a bounded effective priority within the variable range; do not cross into `16–31` automatically.
3. If the thread is Ready, relocate it to the correct ready queue under scheduler synchronization.
4. Apply normal quantum accounting; decay the temporary boost back toward the base priority according to an explicit, tested rule.
5. Cancel stale boosts when the thread terminates or its relevant state changes.

**Do not** boost every awakened thread indiscriminately, grant permanent priority increases after mouse movement, or let an untrusted application raise its own priority indefinitely.

### 5.3 Foreground and input responsiveness

Input ISR/DPC work should be minimal: capture events, acknowledge hardware and wake the responsible kernel/user-mode consumer. The message loop must run in user mode rather than inside an interrupt handler.

Use a small, bounded interaction boost for the relevant UI thread **when it transitions to Ready**. Keep it separate from the compositor's own budget. Do not transfer a boost to every thread in the foreground process; a CPU-heavy rendering or compilation worker should not automatically receive interactive scheduling priority.

### 5.4 Priority inversion and inheritance

Priority inversion is real when a high-priority thread waits on a resource held by a lower-priority thread. Implement mitigation only for synchronization primitives whose **documented contract and ownership model** support it (for example, an appropriately designed owned mutex). Windows NT dispatcher objects do not all implement generic priority inheritance.

For each supported primitive, specify owner, waiter ordering, boost/restore rules, cancellation, timeout, recursion and owner termination. Preserve transitive ownership constraints where implemented. Never infer resource ownership for events, semaphores or arbitrary waits.

### 5.5 Starvation mitigation

Observe ready-queue latency for variable-priority threads under sustained CPU load. Introduce bounded, deterministic anti-starvation behavior only if it is compatible with required NT scheduling semantics. Never silently demote real-time threads or promise fairness when an unrestricted real-time thread monopolizes a CPU.

### 5.6 Quantum policy

Retain the canonical NT-compatible quantum accounting and equal-priority rotation. Any workstation/server or foreground quantum adjustment must be explicitly specified, justified against Windows-facing behavior, and tested. A boost must not accidentally reset the quantum on every timer tick or create an infinite quantum.

## 6. Ps, Ob, Mm and dispatcher integration

- **Ps** owns process/thread lifecycle, compatible priority-class settings and the association between each thread and its address space.
- **Ke** owns thread states, ready queues, preemption, waits, timer handling and dispatch priority.
- **Ob** owns handles, references and lifetime. Scheduling must never assume a process/thread object remains alive solely because a user handle existed earlier.
- **Mm** owns process mappings and memory protection. Context switching must use the actual process CR3, keep kernel mappings valid and avoid use-after-free on address-space teardown.
- **Io** signals the existing wait/ready path on completion rather than creating an independent scheduler.
- **Win32k/user-mode window manager** provides carefully validated foreground/input hints; it must not manipulate private Ke structures from Ring 3.

Ensure process and thread termination cancels or drains relevant wait/boost state before objects can be reclaimed. The current M4 lifecycle work remains a prerequisite for a complete policy.

## 7. Desktop architecture and responsiveness

### 7.1 Separation of responsibilities

The desired future desktop pipeline is:

`Input IRQ → Ke wakeup → window-server message queue → application UI thread → application surface → independent compositor → display driver`

Keep application rendering independent of final desktop composition. One frozen application must not necessarily freeze cursor movement, window manipulation or the presentation of already-composed surfaces.

### 7.2 Compositor rules

- The compositor should be a **separate user-mode process/service**, with a small number of latency-sensitive threads.
- It must not perform unbounded synchronous file/network I/O on its presentation-critical thread.
- Applications submit surfaces/buffers through validated shared memory and synchronization interfaces.
- A stalled application may show its last valid frame; it must not own the global desktop presentation lock indefinitely.
- Keep pointer movement and input acknowledgement independent from a blocked application's message loop.
- Coordinate presentation with the display refresh interval where supported, but distinguish software scanout, compositor correctness and hardware acceleration.

Responsiveness is jointly a scheduling, synchronization, memory-pressure, I/O and compositor problem. Priority alone cannot fix blocked UI threads or severe page-fault/I/O stalls.

## 8. Observability and performance requirements

Instrumentation must be **local and opt-in**, consistent with MicaNT's zero-telemetry policy. Prefer bounded kernel counters and timestamped trace buffers that can be inspected over serial/QEMU or local debugging facilities.

Minimum counters:

- Thread Ready → Running latency, segmented by priority/workload.
- Input event → consumer wake and input event → application dispatch latency.
- Timer tick jitter and deferred-preemption count.
- Quantum expiration, involuntary switch and voluntary block counts.
- Ready-queue depth and starvation-duration distributions.
- Priority-boost application, expiry and inversion-mitigation counts.
- Compositor missed presentation deadlines and input-event drops, once a real compositor exists.

### 8.1 Initial engineering targets (not achieved measurements)

| Metric | Proposed target | Qualification |
|---|---|---|
| Interactive Ready → Running latency | P95 < 5 ms | Under sustained normal-priority CPU contention; measured on a supported test configuration |
| 60 Hz composition | 16.67 ms frame budget | Target cadence, not a guarantee that all frames render in time |
| Lost keyboard/mouse events | 0 in bounded stress tests | Given documented queue capacity and load envelope |
| Normal-priority CPU-bound fairness | No indefinite starvation in bounded tests | Excludes privileged real-time monopolization |
| Kernel lock/IRQL safety | 0 invariant violations | Verified using debug assertions and targeted stress |

These numbers are **proposals for acceptance tests**; neither macOS nor MicaNT is claimed to guarantee them universally. Keep QEMU TCG results separate from physical AMD64 performance data.

## 9. Implementation sequence and dependencies

Do not restart the completed BSP scheduler implementation or delay M4 to implement GUI QoS. Integrate in the following order:

1. **Stabilize M4 lifecycle:** termination, APC/wait cleanup, reaper and valid process/thread object lifetime.
2. **Stabilize Ke wait/wakeup and preemption:** one canonical dispatch path and safe IRQL transitions.
3. **Add scheduling instrumentation:** ready latency, wake reasons, quantum accounting and lock-safe counters.
4. **Implement dynamic priority policy:** bounded boost/decay, priority relocation and guarded cancellation.
5. **Support selected synchronization inheritance/mitigation:** only where object ownership is explicit and compatible.
6. **Expose a private interaction hint channel:** from the validated window-server boundary to Ps/Ke.
7. **Integrate the compositor:** separate user-mode presentation, shared surfaces and frame deadline accounting.
8. **Extend to SMP:** per-CPU queues/ownership, affinity and reschedule IPIs when the underlying HAL and Ke are ready.

Each phase must modify the canonical owner and connect its producer, consumer, error path and lifetime handling. Do not claim a phase complete based on data structures, hosted-only tests or serial markers alone.

## 10. Acceptance matrix

| Case | Required observation |
|---|---|
| Two CPU-bound equal-priority threads | Both receive CPU time; quantum rotation verified |
| High-priority thread becomes Ready | Preempts eligible lower-priority thread without voluntary yield |
| UI event under CPU saturation | Consumer thread wakes with bounded latency, without boosting unrelated workers |
| Temporary boost | Priority rises only within legal range, then returns to base |
| Queued thread changes priority | It moves queues atomically; no duplication or loss |
| Thread blocks / wakes / terminates | No stale ready entry, pending boost or dangling reference |
| Supported owned lock inversion | Bounded mitigation and correct restoration on release/termination |
| Unsupported primitive | No invented inheritance semantics |
| Application UI thread hangs | Other apps and independent compositor remain schedulable |
| Heavy background I/O | No scheduler lock blocking or unbounded compositor critical-path wait |
| Process teardown with runnable/waiting threads | No use-after-free, stale CR3 or destroyed stack dispatch |
| BSP QEMU regression | Kernel boot, timer, syscall, CPL3, VirtIO input/scanout still work |

When authorized to run checks, use focused unit tests for queue/boost invariants, then the real AMD64 bare-metal QEMU path. Perform stress/latency measurements on physical x64 hardware before making end-user smoothness claims. Respect an explicit user instruction to defer test/build/QEMU and label results **unverified** in that mode.

## 11. Policy/API boundary

Do not invent public NT APIs, privilege rules or Windows scheduler promises. Research Windows priority APIs and thread/process class semantics in public Microsoft specifications before changing externally visible behavior. macOS QoS is used only to inform the private policy for responsiveness.

Useful public references:

- Microsoft: [Scheduling Priorities](https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities)
- Microsoft: [Priority Boosts](https://learn.microsoft.com/en-us/windows/win32/procthread/priority-boosts)
- Microsoft: [Process Priority Class](https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities#priority-class)
- Apple: [Energy Efficiency Guide — Prioritize Work at the Task Level](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/EnergyGuide-iOS/PrioritizeWorkWithQoS.html)
- Apple: [Dispatch QoS](https://developer.apple.com/documentation/dispatch/dispatchqos)

**Definition of done:** NT-compatible process/thread priorities remain intact; dynamic interactive behavior uses the canonical Ke/Ps/Ob/Mm paths; user-mode compositor responsiveness is demonstrable under load; all claims distinguish implementation, QEMU evidence and physical hardware measurements.

### Multicore dependency

SMP is a distinct dependency tracked by [M20](KERNEL_MILESTONES.md), with processor bring-up, per-CPU scheduling, affinity migration, reschedule IPIs and Mm shootdown acceptance in [KERNEL_SMP_MULTICORE_IMPLEMENTATION_SPEC.md](KERNEL_SMP_MULTICORE_IMPLEMENTATION_SPEC.md). Existing BSP scheduling evidence does not satisfy those gates. M13's responsiveness measurements may proceed on BSP while multicore-specific claims remain deferred.
