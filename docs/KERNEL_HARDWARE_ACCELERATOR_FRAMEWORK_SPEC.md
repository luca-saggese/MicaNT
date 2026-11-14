# MicaNT — Hardware Accelerator Framework Specification

**Status:** Proposed; no device availability asserted  
**Platform:** AMD64, canonical modular kernel  
**Owners:** HAL/PCI/IOMMU, Io/Ob/Se, Mm/Ke, existing device-specific drivers  
**Consumers:** Ring 3 media, cryptographic, compute and AI runtimes

## 1. Purpose

Provide a **small, common device-capability and submission contract** for heterogeneous hardware accelerators while preserving each device's canonical driver and user-mode API. The framework must not become a second scheduler, memory manager, DMA allocator, GPU subsystem or NPU runtime.

It serves GPU media engines, NPU, Intel DSA, Intel QAT and future supported PCIe accelerators. Direct CPU instructions such as AES-NI/AVX/AMX do **not** use this device-submission path; their enablement belongs to `KERNEL_CPU_FEATURE_XSTATE_SPEC.md`.

## 2. Existing MicaNT inventory

Inspect `include/micant/km/drivers/compute/{npu,dsa,qat,amx}.hpp`, `drivers/{npu,dsa,qat}/`, `include/micant/km/drivers/video/`, `docs/KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, and `docs/DRIVERS_INTEGRATION_STATUS.md`. Existing imported driver headers and hosted tests are **not evidence of a loadable freestanding driver or real device submission**. Reuse HAL's PCI/MMIO, Io object and DMA ownership paths as they actually exist. Archive-only `old/` code is not a production fallback.

## 3. Design invariants

1. One canonical owning driver per physical device/function; no duplicate device-manager hierarchy.
2. Ring 3 selects algorithms/workloads and parses untrusted job formats; Ring 0 validates queues, buffers, permissions and hardware commands.
3. All descriptors and DMA access are bounded and owned until completion/cancellation, including reset and surprise removal.
4. Unsupported capabilities are absent or explicitly rejected; no fake acceleration or synthetic success.
5. A process cannot use another process's buffers, device context or completion handles without the proper NT rights.
6. A device reset may invalidate every in-flight request; submitters receive a deterministic status.

## 4. Logical interfaces (internal, not public NT APIs)

- **Capability provider:** immutable/versioned device ID, class (video / neural / data movement / cryptography), firmware/driver version and supported operation profiles.
- **Device context:** access-checked reference, per-process ownership, quotas and lifecycle callbacks.
- **Memory binding:** validated, pinned buffers and DMA/IOMMU mapping handles; expose neither raw physical addresses nor unrestricted MMIO to untrusted Ring 3.
- **Submission queue:** bounded descriptors, backpressure, completion/cancel/reset semantics and optional fence/event signaling.
- **Health and reset:** device-loss reporting, fault counters and safe reinitialization.

Use the existing Io/Ob/Ke boundary and a shared ABI only where needed to avoid coupling Ring 3 DLLs to `micant/km/`. Do not create a custom public Windows ABI masquerading as WDK semantics.

## 5. DMA and interrupt contract

- Validate BAR ownership, PCI capabilities and device feature negotiation before MMIO or queue use.
- Pin pages while DMA is live; ensure cache/coherency constraints and correct buffer direction.
- Use IOMMU mappings when operational; otherwise document weaker DMA isolation and prohibit unsafe mappings.
- Validate descriptor lengths, counts, alignment, wraparound and scatter/gather chains before enqueue.
- Route IRQ/MSI/MSI-X to canonical HAL and Ke DPC paths. Interrupt code must not allocate or block arbitrarily.
- On cancel, process termination, driver unload, power transitions and device reset, drain or invalidate outstanding work without use-after-free.
- Ensure device firmware can be rejected or disabled safely if untrusted or incompatible.

## 6. Scheduling and resource accounting

A bounded hardware queue is not a second CPU scheduler. Use existing Ke waits/completions and resource groups where available. Reserve/share queue slots fairly, enforce memory and in-flight-job quotas, and make priority policy explicit. Avoid global locks held while waiting for device completions. Measure submission overhead, queue depth, fairness and utilization.

## 7. Ring 3 access and vendor integration

Drivers can expose compatible user-mode interfaces such as D3D12 Video for graphics and appropriate device-specific runtimes for NPUs, DMA or cryptography. Hardware capability discovery should not force every application to speak one MicaNT-only job format. Prefer standards/public Windows contracts and well-scoped adapters. Vendor proprietary firmware and userspace requirements must be separately inventoried; do not claim they can be bypassed.

## 8. Phased implementation

| Phase | Implementation | Acceptance |
|---|---|---|
| H0 | Inventory and capability contract; select **one real device** | Source-to-driver ownership map, test device and backend chosen |
| H1 | Enumerate/open a device with access checks | Real PCI identification, no fabricated device availability |
| H2 | DMA/IOMMU and queue lifecycle | Real buffer mapping + submission + completion + cleanup |
| H3 | Multi-client and cancellation | Two Ring 3 clients; queue saturation; denied accesses; teardown |
| H4 | Reset/hot-unplug/power transition | No invalid DMA, handle leak or stale completion after reset |
| H5 | Extend to next accelerator class | Independent capability, numerical or byte-exact correctness evidence |

## 9. Test plan

Tests must include numeric/reference outputs for compute, exact copy verification for DMA engines, or decoded frames where appropriate. Track the full path **Ring 3 client → Nt/Io boundary → owning driver → real device → completion → owned buffer release**. Use QEMU only for devices truly modeled by QEMU; use hardware labs for DSA/QAT/NPU features unavailable in virtualization. Fault injection: invalid descriptors, queue exhaustion, reset, interrupts lost/delayed, cancel/close while in flight, buffer alias and memory pressure.

Record build, QEMU, physical hardware and negative tests separately. No cross-subsystem acceptance by proxy.

## 10. Security and policy

Apply NT object/handle checks, resource-group budgets and least-privilege device policy. Validate all user pointers and indirect data. Expose device state and performance counters through controlled interfaces; no external telemetry. Use signed/validated firmware policy where supported and never silently execute arbitrary device firmware.

## 11. Definition of done

A selected real accelerator completes a verified request from an unprivileged Ring 3 application through the canonical driver, with correct isolation, DMA lifetime, interrupt/completion, cancellation, reset, resource accounting and reproducible evidence. Additional device classes are independently accepted; one device cannot establish universal framework capability.
