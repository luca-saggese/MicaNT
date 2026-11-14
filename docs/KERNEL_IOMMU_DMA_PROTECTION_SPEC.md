# MicaNT AMD64 IOMMU and DMA Protection Specification

**Status:** Proposed; no hardware DMA isolation is currently claimed by this specification. **Owner:** HAL/IOMMU mechanism, Io DMA adapter and device lifecycle, Mm PFN/pinning, PnP device identity; drivers retain request ownership.

## Goal and boundary

Provide safe DMA lifetime semantics first, then hardware translation/isolation. Correct pinning and completion ownership prevent use-after-free but do not stop a malicious or defective device from DMAing outside its buffers. IOMMU protection is not accepted until translation is enabled and negative hardware tests pass. Preserve one canonical Mm and Io DMA path.

## Discovery and translation design

Parse ACPI DMAR (Intel) or IVRS (AMD) with checksums, bounded structures, reserved-field validation and scope-to-PCI requester-ID mapping. Cross-check PCI topology and firmware exclusions; ambiguous/unsupported scopes disable remapping for affected devices and prohibit claiming isolation. HAL enumerates units, capabilities, address widths, page sizes, interrupt-remapping support and quirks. Intel VT-d and AMD-Vi backends implement vendor register/table mechanics behind a private MicaNT `IommuBackend` proposal; this is not a public NT API. Domains bind a stable device identity to second-level tables; default policy is deny/unassigned, with explicit allow-list passthrough only for early boot where platform safety requires it.

Io DMA mapping pins referenced PFNs, allocates checked IOVAs, emits bounded scatter/gather segments, maps permissions, performs required cache maintenance, invalidates IOTLB and only then publishes descriptors. Completion/cancellation/reset stops device access, waits for completion or proven bus-master disable, unmaps and invalidates, then unpins and releases IOVA. No buffer is freed solely because the IRP was canceled. Coherent and noncoherent devices follow architecture-defined cache rules. Interrupt remapping is a separate capability gate.

Each request has one owner and monotonic states (prepared → mapped → submitted → completed/cancel-requested → quiesced → unmapped). Exactly one terminal completion wins races. Device reset/removal disables bus mastering or establishes equivalent isolation before releasing memory. IOVA exhaustion, mapping failure and IOMMU faults propagate failure; never silently fall back to unconstrained DMA after remapping is active.

## Driver integration order

Adapt canonical DMA ownership for AHCI, NVMe, xHCI, VirtIO, Intel I210, audio and GPU drivers. Inventory coherent rings versus streaming buffers and device address width per driver. First test lifetime-safe pin/map/unmap with existing identity DMA assumptions; next gate hardware backend per platform. Keep bootstrap device paths explicit and report unprotected status. Device removal and surprise removal join PnP teardown; IOMMU fault handling records requester/address/access, masks or quarantines the device, and schedules recovery outside interrupt context.

## IRQL, concurrency and failure behavior

DMA map/unmap may require allocation and invalidation waits, so define passive/APC-level contract; ISR/DPC may only use preallocated mappings and bounded completion operations. Domain tables are protected by per-unit synchronization; never wait for IOTLB completion while holding a global scheduler/Io spinlock. Mapping teardown is idempotent and generation-tagged. On timeout, retain pinned pages and quarantine device/domain rather than risk stale DMA. Firmware reserved-memory regions and graphics apertures must not be mapped as ordinary RAM.

## Phases and measurable acceptance

| Phase | Gate |
|---|---|
| D0 | DMA inventory, ownership API and pin/unpin lifetime tests; no hardware isolation claim. |
| D1 | ACPI/PCI scope validation and capability reporting; malformed/ambiguous input disables affected device safely. |
| D2 | Single vendor backend, domain tables, enable/disable and invalidation on QEMU/IOMMU-capable hardware. |
| D3 | Real driver mappings for storage, xHCI and network; streaming and scatter/gather paths. |
| D4 | Interrupt remapping and fault quarantine where available. |
| D5 | Physical platform matrix, surprise removal and sustained concurrent I/O. |

Tests cover valid/invalid mapping and permission, scatter/gather boundary, IOVA exhaustion, unmap during teardown, reset with pending DMA, device removal in-flight, delayed completion/cancel race, injected DMA fault, and a device attempting an unauthorized page access. QEMU acceptance must prove hardware translation enabled (register/table evidence plus denied access test) and check allowed buffers still work. Physical acceptance records firmware, IOMMU mode, device scope and fault evidence, with AHCI/NVMe/xHCI/VirtIO/I210/audio/GPU as supported. Hosted tests prove only state/allocator logic. Compare PFN references before/after; no unauthorized page changes; no reuse before invalidation. Distinguish lifetime-safety acceptance from IOMMU isolation acceptance.

## Security and unsupported modes

Unknown requester IDs, unsupported remapping modes, invalidation timeout and translation faults fail closed for the affected device. Do not enable passthrough and call it protection. ATS/PASID/SVA, SR-IOV, nested translation and confidential-computing interaction are out of scope. DMA buffers can contain secrets; sanitize on reuse according to Mm policy.

## References

- [Intel VT-d architecture specification](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf).
- [AMD IOMMU specification](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/specifications/48882_IOMMU.pdf).
- [UEFI ACPI specifications](https://uefi.org/specifications) (DMAR); MicaNT [architecture](KERNEL_ARCHITECTURE.md), [PnP lifecycle](KERNEL_PNP_DRIVER_LIFECYCLE_SPEC.md), [filesystem/storage](KERNEL_FILESYSTEM_SPEC.md).
