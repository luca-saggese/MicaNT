# MicaNT Plug and Play and Driver Lifecycle Specification

**Status:** Proposed; the repository contains PnP-related code and models, but no complete freestanding PnP lifecycle is claimed. **Owner:** canonical Io/PnP manager, Ob device/driver objects, HAL/bus enumerators, Mm mappings, Ke work and synchronization, Se access policy.

## Objective and model

Connect real bus enumeration to a canonical device tree and safe driver/device lifecycle. Keep one Io manager, one device namespace and one IRP implementation. Recheck `kernel/pnp/` and `kernel/io/` before implementation; registration or hosted state tests do not establish a live device stack. Follow documented NT/WDK contracts where exposed; internal state structures remain private MicaNT design.

Represent each enumerated device with a stable identity, parent bus, resource requirements/assignments, state and references. Model PDO, FDO and optional filter DO roles in a device stack, with attached order, dispatch forwarding and completion ownership. Bus drivers create PDOs; Io/PnP owns enumeration/state transitions; function/filter drivers own hardware behavior. Device interfaces are registered only while started and removed before teardown; namespace exposure requires access checks through Se/Ob. Driver load/init/unload must have explicit image/object references, entry failure cleanup and no fabricated successful initialization.

## Lifecycle and resources

State machine: discovered → resources-assigned → start-pending → started → query-stop/remove → stopped or removal-pending → surprise-removed → deleted. Reject illegal transitions. Start sends resources bottom-up as appropriate; stop/remove quiesces new I/O, cancels queued requests, drains in-flight IRPs and DMA, detaches interfaces, releases interrupt/MMIO/DMA resources and drops references only after completions/rundown. Query-remove can be vetoed by active users; surprise removal cannot be vetoed and must prevent further bus access safely. Cancellation races with completion have exactly one terminal completion.

Resource arbitration is owned by the bus/PnP layer: validate PCI BAR sizes/ranges, IRQ vectors/MSI/MSI-X ownership, MMIO cache attributes and DMA constraints; conflicting assignments fail start with explicit status. Never map device memory as ordinary cached RAM. Interrupt assignment must use canonical HAL vectors and cleanup on failed start. Power transitions use Po/Io contracts and are coordinated with device-stack order. USB hotplug and PCI rescan are staged capabilities, not implied by static QEMU enumeration.

Driver failures are isolated by marking the device/stack failed, completing or failing affected IRPs, preserving unrelated devices and enabling bounded reset/retry where hardware supports it. Ring 0 driver crashes remain kernel-fatal unless a separately proven containment mechanism exists; do not promise process-like isolation. Driver load policy, image integrity and signing gates belong to security policy, not PnP.

## Concurrency, IRQL and ownership

Enumeration, start/stop, resource negotiation and driver loading run at passive context and may wait. ISR/DPC paths only signal bounded state and queue work. Each device stack has rundown protection; remove-lock-like internal mechanisms prevent object destruction while dispatch/completion is active. Lock order covers PnP tree, device state, queue and driver-private locks. Never call driver callbacks under global tree spinlock. Object references and in-flight IRP references are distinct; unload waits for both to drain or returns failure.

## Phases and acceptance

| Phase | Gate |
|---|---|
| P0 | Device/driver object and state-machine audit; hosted transition/cancellation tests. |
| P1 | Real PCI and ACPI/bus enumeration into parented PDO tree; resource validation. |
| P2 | Driver load/init and start/stop with MMIO and interrupt ownership and rollback. |
| P3 | IRP cancellation, query-remove, surprise removal, reset and reference rundown. |
| P4 | USB/PCI hotplug, power IRPs, repeated stress and physical platform matrix. |

QEMU tests require actual enumerated devices, repeated driver start/stop/unload where supported, removal during I/O, surprise unplug, reset while queued, resource conflict injection, cancellation/completion race and zero leaked references/mappings/IRPs. Hardware tests repeat on supported PCI/USB devices, with hotplug only where electrically/firmware supported. Hosted tests exercise state machines, not enumeration. Fault injection covers allocation failure at each transition, driver initialization failure, BAR conflict, interrupt setup failure, timeout, cancellation and removal. Use counters to show every accepted request reaches one terminal completion and resources return to baseline.

## References

- Microsoft [PnP driver design guidelines](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/pnp-driver-design-guidelines), [power management](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/support-for-power-management), [IRP dispatch routines](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/required-dispatch-routines).
- MicaNT [architecture](KERNEL_ARCHITECTURE.md), [ACPI/power](KERNEL_ACPI_POWER_MANAGEMENT_SPEC.md), [IOMMU/DMA](KERNEL_IOMMU_DMA_PROTECTION_SPEC.md), [filesystem storage](KERNEL_FILESYSTEM_SPEC.md).
