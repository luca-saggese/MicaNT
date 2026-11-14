# MicaNT AMD64 ACPI and Power Management Specification

**Status:** Proposed. ACPI discovery or a firmware power-off call alone does not establish power-management support. **Owners:** HAL/ACPI platform layer, Io PnP/power IRPs, Ke timers/idle, Mm suspend-sensitive mappings, driver stacks.

## Objective and supported profile

Provide validated firmware discovery, device power transitions, orderly shutdown/reboot, and a staged path to sleep/resume on the canonical AMD64 kernel. Initial profile: ACPI table discovery and validation, static device enumeration, shutdown/reboot, and supported device D-states. System suspend/resume is a later gate because it requires coordinated driver, interrupt, timer, DMA and memory restoration. Current architectural status documents an ACPI pointer in the UEFI handoff, not a complete ACPI subsystem; source presence is not operational evidence.

## Table and namespace architecture

HAL validates RSDP, XSDT/RSDT and each SDT checksum/length/address range before mapping. Keep physical addresses distinct from virtual pointers, use bounded mappings, reject overlap/overflow and duplicate singleton tables, and retain validated table ownership for its users. Parse MADT, FADT, HPET, MCFG, and selected platform tables behind typed read-only views. DMAR/IVRS handling belongs to IOMMU work; AML namespace evaluation must not be duplicated there.

Device discovery is owned by canonical PnP/Io. AML is not executed in interrupt context. If ACPI namespace methods are required, choose a cleanly licensed AML interpreter compatible with freestanding C++23 constraints, or implement a deliberately bounded AML subset with explicit unsupported-opcode errors. No arbitrary method execution during early bootstrap. Namespace objects, method evaluation, resource templates and `_STA`, `_HID`, `_CID`, `_CRS`, `_PRW`, `_PSx`, `_Sx` behavior need per-method bounds, synchronization, timeout and recursion limits. Firmware resource claims are validated before Io assigns MMIO, ports, interrupts or DMA.

## Power policy and transitions

Po owns system/device policy; Io delivers documented PnP and power IRPs through each device stack; bus/function drivers own hardware-specific quiesce/restore. Track supported device states and capabilities from enumeration; never report a transition supported until every stack participant can complete or forward it. Shutdown sequence: reject new work, flush file/cache state through M14 durability contracts, stop services at Ring 3, drain/cancel IRPs, stop DMA and mask device interrupts, persist required state, then invoke validated firmware reset/shutdown mechanism with bounded fallback. Reboot and power-off are separate outcomes; fallback reset must not be described as successful power-off.

Device D-state changes require query/commit and rollback. System sleep must order children before parents on suspend and parents before children on resume, preserving dependency constraints. Snapshot only required state; stop bus mastering before invalidating any DMA buffer, quiesce queues, drain completions, invalidate IOTLB if present, and retain pages until hardware can no longer access them. On a failed suspend, unwind completed transitions in reverse order; if restoration fails, remain awake in a safe degraded state and report failure.

CPU idle states, thermal events, fan control, battery/AC reporting and wake events are capability-driven extensions. Use firmware-described safe controls only; no speculative MSR writes or guessed fan offsets. Clock/timer ownership and monotonic continuity are in the time specification. Battery and lid/power-button handling require real ACPI/device evidence.

## Concurrency, IRQL and security

Firmware table parsing may run during initialization at passive context; AML evaluation and device transitions are passive/blocking work. ISR only acknowledges/latches event and queues bounded DPC/work item. Never hold a spinlock across AML, filesystem flush, IRP wait or firmware sleep. Serialize namespace evaluation and per-device transitions with cancellable passive-level locks. Validate all firmware-provided lengths, package depth and buffers; AML is untrusted input. Do not expose kernel pointers or raw firmware contents to unprivileged callers.

## Phases and acceptance

| Phase | Gate |
|---|---|
| A0 | ACPI table inventory, physical mapping lifetime and validation corpus; malformed tables rejected. |
| A1 | MADT/FADT/HPET/MCFG discovery connected to HAL/PnP without duplicate ownership. |
| A2 | Bounded AML policy or licensed interpreter decision; resource enumeration and explicit unsupported methods. |
| A3 | Real shutdown and reboot on QEMU plus supported physical boards; flush completion and driver/DMA quiescence evidenced. |
| A4 | Device D-state transitions with repeated query/start/stop and rollback. |
| A5 | System suspend/resume, wake sources, controller/interrupt/timer restoration and failure recovery. |
| A6 | Thermal, fan, battery and idle policy only for enumerated, tested hardware. |

QEMU acceptance uses persistent disks and verifies committed data survives shutdown/reboot, device state change, resume controller/interrupt operation, injected suspend failure rollback, and no active DMA into freed buffers. Hardware acceptance records board/firmware, wake source and device matrix, cycles suspend/resume repeatedly, validates battery/thermal reporting only where present, and verifies reset/shutdown behavior. Hosted tests cover parser fuzzing and transition state machines, not physical power state. Fault injection includes checksum/range errors, AML timeout, driver veto/failure, device nonresponse, flush failure and resume failure. Shutdown/reboot acceptance does not imply suspend support.

## Risks and references

Unknown tables/methods must fail explicitly or be ignored only when optional by contract. Lack of AML, safe firmware method, or driver state support means that capability is unavailable. See [UEFI ACPI specification](https://uefi.org/specifications), [Windows driver power support](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/support-for-power-management), [handling power IRPs](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/handling-power-irps), and MicaNT [architecture](KERNEL_ARCHITECTURE.md), [PnP](KERNEL_PNP_DRIVER_LIFECYCLE_SPEC.md), [time](KERNEL_TIME_CLOCK_TIMER_SPEC.md).
