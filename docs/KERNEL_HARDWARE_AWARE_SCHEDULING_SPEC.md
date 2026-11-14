# MicaNT — Hardware-Aware CPU Scheduling Specification

**Status:** Proposed  
**Owners:** Ke scheduler, HAL topology/telemetry, Ps affinity, Mm memory locality, power management policy  
**Relationship:** Extension of M3/M13; depends on verified baseline Ke lifecycle and eventual SMP

## 1. Goal

Place threads on CPUs using accurate hardware topology and available performance/efficiency hints, without compromising NT priority, affinity, fairness, deadlines or responsiveness. Keep **policy** separate from **mechanism**: HAL reports capabilities; Ke makes scheduling decisions; optional Ring 3 policy can propose bounded hints but cannot override safety checks.

The initial target is heterogenous-core CPU performance and energy-aware placement. Intel Hardware Feedback Interface (HFI) and ACPI CPPC are **optional data sources**, never requirements for a working scheduler. Unsupported systems fall back to the canonical policy.

## 2. Existing components and limitations

Inspect current `include/micant/km/core/ke.hpp`, Ke scheduling sources, `include/micant/km/drivers/compute/hfi.hpp`, `drivers/hfi/`, `docs/KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md` and HAL ACPI/topology code. Imported HFI hosted models do not prove real per-CPU hardware feedback. The published M3 acceptance is a bounded BSP bootstrap; do not imply complete SMP or x2APIC readiness.

## 3. Capabilities and topology

Discover/validate logical CPUs, core sharing, SMT siblings, cache topology, NUMA domains when available, CPU frequency domains, and effective ISA support of each CPU. Map processor groups/affinity and avoid identifying processors exclusively by a global small integer if the ABI later supports larger systems.

HFI/CPPC capability input includes validity, generation, freshness, reliability and bounds. Reject invalid tables and stale/unknown feedback; never turn performance hints directly into privileged MSR writes without HAL validation.

## 4. Scheduling invariants

- Preserve NT-compatible priority classes, scheduling and wait/wakeup semantics for supported contracts.
- Affinity and process/thread security constraints are hard filters; a hint cannot place a thread on a forbidden CPU.
- Tasks requiring CPU-specific instructions must only execute on compatible enabled cores, or receive explicit refusal/fallback.
- Avoid migration ping-pong, starvation and priority inversion through hysteresis and periodic fairness correction.
- Do not perform complex allocation, I/O or vendor telemetry parsing on interrupt paths.
- Limit thermal/power responses to capabilities reported by HAL; fail open to safe baseline scheduling, not to uncontrolled hardware access.
- Make preemption, APC/DPC, idle thread and CPU on/offline behavior correct before adding heuristics.

## 5. Initial placement policy

1. Compute eligible CPUs from affinity, online state, required instruction features and isolation policy.
2. Choose canonical scheduling priority first.
3. Within eligible CPUs, use locality and bounded performance/efficiency scoring if hints are valid.
4. Apply migration hysteresis and honor latency/throughput classes only within verified resource budgets.
5. Keep a baseline deterministic fallback that can be selected at boot or runtime for debugging.

Do not expose unchecked application-supplied scores to the scheduler. The first release should be algorithmically simple and measurable, not ML-based.

## 6. Power and thermal coordination

Use ACPI CPPC or trusted hardware feedback as a scheduling input; device/CPU power-state control remains owned by HAL/Power Management. A critical thermal event may override normal performance hints. Avoid measuring success only as throughput: account for energy, temperature, responsiveness, and p95/p99 wakeup latency. Do not promise specific battery-life benefits until physical measurements exist.

## 7. Phases and milestones

| Phase | Scope | Evidence |
|---|---|---|
| S0 | HAL topology validation and scheduler baseline metrics | Reproducible 1-vCPU baseline |
| S1 | SMP-ready affinity/placement and migration | 2/4 vCPU scheduling correctness after SMP acceptance |
| S2 | ISA-aware placement, heterogeneous feature restrictions | No illegal instruction / bad core placement |
| S3 | HFI/CPPC ingestion with validation and freshness | Fault injection + fallback when no hints |
| S4 | Performance/efficiency policy and hints | Measured latency, fairness and throughput comparisons |
| S5 | Physical hybrid CPU validation and power coordination | Device-specific performance/energy evidence |

## 8. Tests

- Repeated wake/sleep, preemption, migration, CPU idle and hotplug with correct per-CPU stack/IRQL state.
- Saturate one CPU then change affinity under load; verify waiters and no ready-queue loss.
- Two CPU classes with synthetic capability differences: ensure restricted threads never land on a feature-incompatible CPU.
- Compare baseline versus hardware-aware scheduling on mixed compute, interactive, network and audio tasks; record p50/p95/p99 latency and fairness.
- Feed malformed/stale HFI/CPPC data and force fallback. CPU throttling and changing load must not deadlock or starve threads.
- Test real hybrid processors separately from QEMU; an emulated feature bit is not proof of hardware-aware scheduling.

## 9. Boundaries and risk

Per-process/application QoS and deadline APIs, if exposed, must use documented NT-compatible contracts or explicitly internal interfaces. Keep global policy in user-mode services when possible. Hardware counter access and microcode/firmware control are HAL-owned and privileged. Avoid making changes to stable Ps/Ke paths concurrently with other agent work without dedicated worktree and regression plan.

## 10. Definition of done

On supported hardware, MicaNT's scheduler chooses among eligible processors using validated topology/hardware feedback, preserving affinity, feature compatibility, priority/fairness and teardown behavior. Measurements show the impact against the existing scheduler, with a safe disabled-feature fallback. QEMU and hardware evidence remain separate.
