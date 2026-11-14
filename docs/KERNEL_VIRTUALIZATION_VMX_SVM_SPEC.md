# MicaNT AMD64 Hardware Virtualization (VMX/SVM) Specification

**Status:** Later extension, not a prerequisite for a usable first system. **Owners:** a future canonical virtualization subsystem; HAL exposes CPU controls, Mm owns host/guest memory backing, Ke schedules vCPUs, Io/PnP owns assigned devices, IOMMU owns DMA isolation.

## Scope and gate

Design for Intel VT-x and AMD-V without introducing an alternate Executive or weakening process isolation. Do not begin an operational hypervisor before the canonical kernel has stable Mm mappings, interrupt handling, SMP scheduling, DMA/IOMMU policy and crash recovery. Feature discovery alone is not virtualization support. This document is a staged design boundary, not a commitment to implement both vendors simultaneously.

## Architecture

Keep VM/VCPU objects as referenced Ob-managed objects with explicit security descriptors and handles; guest lifecycle is owned by a future virtualization manager, while existing Ps continues to own host processes/threads. A vCPU is scheduled as a Ke execution context with pinned VMCS/VMCB state and explicit host-state save/restore. Mm owns guest physical backing and EPT/NPT second-level tables with permissions, dirty/access tracking and invalidation. HAL vendor backends own VMXON/VMCB setup, VM entry/exit, control validation and CPU-local state. Io device emulation uses existing device stacks; direct assignment requires IOMMU domain and interrupt remapping.

Initialize only if CPUID, feature-control MSRs, firmware lock policy and vendor requirements permit; failure leaves ordinary OS boot unaffected. Validate VMCS/VMCB controls against allowed masks. Entry/exit handlers use nonpageable per-CPU stacks, validate exit reason/qualification and boundedly dispatch to a VM worker. EPT/NPT violations distinguish guest demand faults from protection violations; unrecognized exits fail the VM explicitly. Host secrets and privileged mappings never enter guest mapping. Pause/stop/reset and destruction drain vCPU execution and device DMA before freeing guest pages.

Device virtualization begins with software emulation in Ring 0 only where bounded and necessary; complex device models and guest tooling belong in Ring 3. Passthrough is excluded until IOMMU, interrupt remapping, reset and hot-unplug paths are verified. Nested virtualization, live migration, confidential VMs and SR-IOV/SVA are separate projects.

## Dependencies and phases

| Phase | Gate |
|---|---|
| V0 | Requirements/threat model and hardware inventory; no VM execution. |
| V1 | Stable Mm, Ke/SMP, exception, timer, crash and IOMMU prerequisites. |
| V2 | One vendor backend, isolated minimal guest, VM entry/exit and halt test. |
| V3 | EPT/NPT memory protection, interrupt injection and vCPU scheduling. |
| V4 | Lifecycle, reset, failure containment, emulated devices and stress. |
| V5 | Optional IOMMU-protected assignment only after separate security gates. |

QEMU tests boot a minimal guest, exercise exits, timer/interrupt delivery, guest memory boundary violations and destroy/recreate loops while proving host survival. Physical acceptance records CPU vendor/firmware, tests each backend, host interrupt and memory isolation, and injects VM entry failure. Hosted tests cover control validation/state machines only. Fault injection covers unsupported MSRs, invalid control fields, EPT/NPT allocation failure, VM exit storm, vCPU termination, IOMMU fault and device removal. No virtualization milestone is accepted on a guest launch log alone.

## Security and references

VMX/SVM entry paths are security-critical; malformed guest state must never become host state. Verify mitigation interactions and side-channel assumptions; do not claim confidentiality from ordinary EPT/NPT. Use vendor manuals as normative and reject unsupported capabilities. References: [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), [AMD64 APM](https://www.amd.com/en/search/documentation/hub.html), Intel [VT-d](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf), AMD [IOMMU](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/specifications/48882_IOMMU.pdf); MicaNT [SMP](KERNEL_SMP_MULTICORE_IMPLEMENTATION_SPEC.md), [IOMMU](KERNEL_IOMMU_DMA_PROTECTION_SPEC.md), [memory](KERNEL_MEMORY_PRESSURE_PAGING_SPEC.md).
