# MicaNT AMD64 Time, Clocksource and Timer Specification

**Status:** Proposed; BSP bootstrap timer behavior is not high-resolution or SMP timekeeping. **Owner:** HAL/AMD64 clocksource and clockevent backends, Ke timer queues/DPC delivery, Po suspend/resume coordination.

## Objective and time domains

Provide monotonic interval measurement, wall-clock time and reliable one-shot/periodic deadlines through one Ke timer service. `KeQuerySystemTime`-like civil/system time and monotonic performance counters are distinct; wall-clock corrections must never move timeout deadlines backward. Accurate time is needed for expiry, authentication and TLS certificate validation, but network/TLS consumers remain Ring 3 or their canonical subsystem.

## Clocksource and clockevents

HAL discovers PIT only as early bootstrap fallback; validates HPET table/MMIO when present; configures Local APIC timer per CPU; probes invariant TSC and synchronization characteristics before selecting it. Calibrate against a stable reference with bounded samples, detect drift across CPUs and frequency changes, and maintain conversion using overflow-safe fixed-point arithmetic. If invariant/synchronized TSC criteria fail, use a suitable platform source. Publish source identity, resolution and uncertainty; never advertise precision beyond measurement.

Clockevent devices provide one-shot deadlines where possible. Ke owns timer objects, absolute monotonic deadline, cancellation generation and queueing. Per-CPU queues are allowed only with safe migration on CPU offline and correct affinity. Expiration transitions once from armed to firing/cancelled; cancellation waits or references callback state to prevent use-after-free. DPC delivery is bounded and nonblocking; pageable work is queued to passive workers. Timer coalescing is opt-in policy with maximum lateness, never applied to waits requiring exact timeout contract.

RTC/firmware wall clock initializes civil time only after validating ranges and timezone policy; do not conflate local time with UTC. Suspend/resume reconciles elapsed time from a persistent source if available and marks uncertainty if unavailable. CPU frequency changes do not affect invariant source conversion; otherwise recalibrate without discontinuous monotonic time.

## Concurrency and IRQL

Timer insertion/cancel APIs specify allowed IRQL, lock and callback context. ISR acknowledges device, updates bounded per-CPU clockevent state and queues DPC. Avoid global lock contention and never invoke arbitrary callbacks under timer lock. SMP time comparisons use a single normalized monotonic domain; cross-CPU deadline migration preserves absolute deadline. Memory ordering protects state/generation publication; wraparound arithmetic is bounded to documented maximum intervals.

## Phases and acceptance

| Phase | Gate |
|---|---|
| T0 | Existing timer/IRQL path audit and monotonic/civil API semantics. |
| T1 | PIT/HPET/APIC source and clockevent abstraction with calibration. |
| T2 | High-resolution one-shot timers, cancellation and DPC delivery. |
| T3 | Per-CPU timers, migration, SMP skew/drift and deadline stress. |
| T4 | RTC/wall-clock, suspend/resume and long-duration stability. |

Acceptance measures monotonic nondecreasing behavior, deadline error/jitter distributions, calibration repeatability, timer cancellation races, periodic drift, CPU frequency change, suspend/resume continuity and DPC queue bounds. QEMU tests use deterministic virtual clocks where available and are separated from physical precision evidence. Hardware tests record CPU/board/source, compare per-CPU counters, run long-duration drift and jitter measurements. Hosted tests validate conversion/queue algorithms only. Inject missing/invalid HPET, non-invariant TSC, skew, wraparound, timer storm, CPU offlining and cancellation-versus-expiration races.

## References

- [UEFI ACPI specifications](https://uefi.org/specifications) (HPET/FADT); [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html); [AMD64 APM](https://www.amd.com/en/search/documentation/hub.html).
- MicaNT [SMP](KERNEL_SMP_MULTICORE_IMPLEMENTATION_SPEC.md), [ACPI/power](KERNEL_ACPI_POWER_MANAGEMENT_SPEC.md), [scheduling](KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md).
