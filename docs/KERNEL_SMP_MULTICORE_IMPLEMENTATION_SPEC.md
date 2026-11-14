# MicaNT AMD64 SMP and Multicore Kernel Specification

**Status:** Proposed; no SMP acceptance is implied. **Owner:** HAL/AMD64 mechanisms and Ke scheduling; Ps, Mm, Ob and Io own their existing state. **Scope:** x86_64 processors discovered through ACPI MADT, AP startup, concurrent Executive operation, CPU lifecycle.

## Goal and baseline

Extend the canonical modular kernel from BSP operation to multiple processors while preserving one Ke scheduler, one Mm, and one set of Executive objects. `docs/KERNEL_ARCHITECTURE.md` describes current GDT/TSS/IDT and context switching as bootstrap facilities; M3 evidence is BSP-only. Recheck current code before implementation. Do not infer AP support from MADT parsing, CPU-count logging, or a successful build.

## Architecture and ownership

The AMD64 HAL owns APIC register access, AP boot rendezvous, per-CPU entry/exit, interrupt routing and local timers. Ke owns processor/thread state, ready queues, dispatch, DPC/APC policy, IRQL transitions, affinity and reschedule requests. Ps owns process/thread lifecycle and address-space association; Mm owns page tables, address-space lifetime and TLB invalidation; Ob owns references; Io owns request/device lifecycle. `KPRCB`-like scheduling state and `KPCR`-like processor-local state are internal MicaNT designs unless a public ABI specifically requires a layout; do not expose or claim undocumented Windows-private layouts as ABI.

## Hardware bring-up and processor-local state

Parse ACPI RSDP → XSDT/RSDT → MADT with checksum, length, alignment, entry-type, duplicate-ID and arithmetic validation. Support enabled xAPIC processor entries first; treat online-capable entries as offline until a documented hot-add path exists. Reject unsupported topology safely. Map LAPIC/IOAPIC MMIO as uncached supervisor memory. Select xAPIC MMIO or x2APIC MSRs only after CPUID, firmware mode and APIC-base capability checks; do not switch mode while interrupts are active without an explicit transition.

BSP initializes a bounded AP trampoline/startup record and a unique stack, GDT, TSS, IDT-visible per-CPU context, GS/KPCR base, idle thread and PRCB before sending INIT/SIPI. AP validates generation, processor identity and shared boot record, establishes local architectural state, reports a release-published online state, then enters the Ke idle loop. Startup timeout or partial initialization leaves that AP offline and reclaims only resources proven unused. Avoid concurrent use of a stack or startup page before AP acknowledgement.

## Scheduling, interrupts and memory ordering

Start with a single global ready-queue lock and explicit per-CPU current/idle state; evolve to per-CPU queues only after correctness evidence. Every thread is Ready on exactly one eligible queue or Running on exactly one CPU. Affinity masks must reference online processors; affinity changes and migration transfer queue ownership atomically and trigger reschedule IPIs where needed. Define a lock order covering scheduler, dispatcher objects, Ps rundown and address-space teardown. No blocking, pageable access or allocation at DIRQL/high IRQL.

Use acquire/release or stronger atomics for publication, completion and CPU state transitions; `volatile` is not synchronization. Document barriers around AP startup, IPI mailboxes, run-queue transfer and TLB invalidation. Spinlocks must disable/prevent local re-entry as required and use acquire/release semantics, with checked ownership/IRQL assertions in debug builds. Reference-count transitions require overflow/underflow-safe atomics and a clear last-reference reclamation context. Introduce RCU/epochs only if measured read-side constraints justify deferred reclamation; otherwise use references plus rundown.

TLB shootdown: Mm serializes page-table changes, snapshots CPUs that may execute the address space, publishes invalidation range/generation, sends IPIs, and waits for acknowledgements before PFN reuse. CPUs entering an address space compare its generation and flush locally when stale. Address-space membership changes race safely with process exit and thread migration. Never free a page table/PFN while a CPU may retain a translation.

Local APIC timer/clockevent state is per CPU; monotonic time is based on the canonical clocksource, not independent unsynchronized counters. DPC queues and dispatch requests are CPU-affine or explicitly migratable; APC delivery remains thread-context work at legal IRQL. Interrupt handlers acknowledge the correct controller and defer work. Preserve per-thread FPU/SIMD/XSTATE according to CPUID/XCR0-supported state size, with lazy/eager policy documented and no cross-CPU state leakage. A CPU with incompatible enabled XSTATE features must not receive a thread whose saved state cannot be restored there.

## Lifecycle, failures and CPU removal

Process termination marks threads non-runnable, requests remote reschedules, drains waits/APCs/DPC references, and waits until no CPU executes the address space before Mm teardown. Per-CPU idle threads remain available on every online CPU. Initial supported profile is static CPU membership: CPU offline/hotplug is a later phase requiring interrupt migration, timer shutdown, queue evacuation, TLB rendezvous, driver affinity updates and rollback. Do not online heterogeneous feature sets unless Ke maintains a safe common execution baseline.

## Phases and acceptance

| Phase | Deliverable and gate |
|---|---|
| S0 | Audit invariants, locks, IRQL, context/XSTATE and ownership; write lock-order table and failure states. |
| S1 | MADT validation, LAPIC mode selection, BSP local timer/interrupt regression. Invalid tables fail closed. |
| S2 | One AP startup, private stack/GDT/TSS/KPCR/PRCB/idle thread; startup timeout and retry-safe rollback. |
| S3 | Two-CPU Ke scheduling, reschedule IPI, affinity, migration, DPC/APC and timer tests. |
| S4 | Mm generation-based shootdown and Ps/Ob teardown/rundown under concurrent execution. |
| S5 | Stress and supported-platform matrix; CPU offline only as a separately gated extension. |

Acceptance requires actual work on 1/2/4/8 configured CPUs (1 CPU is regression), mass thread creation/exit, affinity pin/unpin, migration accounting, concurrent TLB map/unmap while another core accesses the address space, Ob/Ps/Mm/Ke stress, concurrent interrupts/DPCs, termination during active syscalls, and timer drift/jitter bounds. QEMU must demonstrate distinct AP execution and successful workload completion; physical hardware must repeat supported topology tests and record firmware/CPU details. Hosted tests may exercise queue algorithms and state machines only. Fault injection covers AP timeout, malformed MADT, IPI loss/timeout, allocation failure, CPU removal request, and interrupted shootdown. No CPU recognition alone closes this milestone.

## Security and unsupported hardware

Unrecognized MADT records can be skipped only when the ACPI contract permits; malformed lengths/checksums and duplicate enabled IDs fail initialization. Keep interrupts masked until IDT/APIC routing is valid. x2APIC, hotplug, NUMA scheduling, mixed XSTATE capabilities, and non-AMD64 processors are unsupported until their gates pass. Debug counters are local and opt-in; no telemetry export.

## References

- [UEFI ACPI specifications](https://uefi.org/specifications) (MADT, processor topology and firmware tables).
- [Intel 64 and IA-32 SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) and [AMD64 Architecture Programmer’s Manual](https://www.amd.com/en/search/documentation/hub.html) (APIC, x2APIC, SYSCALL, XSAVE and memory ordering).
- MicaNT: [architecture](KERNEL_ARCHITECTURE.md), [scheduling](KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md), [milestones](KERNEL_MILESTONES.md).
