# MicaNT AMD64 Kernel Security Hardening Specification

**Status:** Proposed; each mitigation is capability-gated and individually verified. **Owners:** HAL/AMD64 CPU setup, Mm page protections/address spaces, Ke exception/context switching, Se/Ob access control, Io driver/IOCTL validation, secure boot loader policy.

## Objective and invariant

Reduce kernel attack surface while preserving UEFI boot, NT contracts, user/kernel separation and recoverable rollout. Do not enable an instruction or protection before boot, exception and context-switch paths save/restore its required state. No mitigation claim follows from a CPUID bit or compile flag.

## Mitigation plan

At boot, enumerate CPUID and MSR capabilities and construct an immutable per-boot capability record. Establish NX on data mappings; enforce W^X for kernel/user mappings and section protections; validate user pointers by probing/capturing safely at documented syscall boundaries; never trust nested pointers/lengths or IOCTL buffers. SMEP blocks supervisor execution from user pages; SMAP requires carefully scoped AC enable/restore around validated copies. Enable each only after page-table flags, fault handling, exception entry and all copy routines pass negative and rollback tests. Kernel/user address isolation requires transition mappings and SYSCALL/interrupt/exception audits before enabling; preserve required per-CPU entry data.

Kernel ASLR requires relocation-complete images, entropy from a trusted source and no fixed address dependencies; otherwise mark unavailable. Stack canaries and compiler stack protection require freestanding runtime support and a nonrecursive failure path. Pool metadata guards, redzones/quarantine and integer-overflow checked arithmetic supplement, not replace, ownership review. CET shadow stacks/IBT are a later CPU-specific extension with XSTATE, interrupt/trap and context-switch support. Speculative-execution mitigations follow vendor guidance and are selected by vulnerability/capability; measure cost and preserve safe defaults.

Se owns token/privilege evaluation and access checks; Ob applies object security and handle rights; Io validates device interfaces and every IOCTL input/output size/access; driver load policy enforces image provenance and trust. Secure Boot integration is distinct: authenticate boot chain and policy keys, define update/rollback protection, and fail safely when keys/policy are unavailable. Image integrity and driver signing require an explicit trust and revocation model. DMA defense depends on IOMMU milestone and must report unprotected devices honestly. Local audit records are bounded and access-controlled; no telemetry. Dumps and secrets are protected per crash/debug specification.

## Ownership, concurrency and rollout

HAL performs capability discovery and CPU controls on every CPU before it is scheduled; SMP requires consistent feature policy. Mm owns page permissions and TLB shootdown; a permission change is not complete until stale translations are invalidated. Ke exception and context paths preserve control state. Se/Ob changes retain compatible authorization semantics and reference lifetimes. Each mitigation is a boot policy bit with a known rollback path for development builds; production must not silently downgrade after partial activation. Unsupported CPU features remain off and are not emulated as success.

## Phases and acceptance

| Phase | Gate |
|---|---|
| H0 | Threat model, CPU inventory and page/copy/driver attack-surface audit. |
| H1 | NX and W^X audits with attempted writable-executable mappings rejected. |
| H2 | User-pointer, IOCTL length/overflow, Se/Ob access and pool-corruption tests. |
| H3 | SMEP/SMAP and address isolation on supported CPUs, per-CPU setup and rollback validated. |
| H4 | ASLR, stack protection, image integrity and driver trust policy. |
| H5 | CET and applicable speculative mitigations as separate hardware gates. |
| H6 | Secure Boot chain, revocation/update and physical recovery tests. |

Hosted negative tests cover arithmetic and access policy; freestanding tests verify actual page tables and CPU state; QEMU tests use CPU models with/without features and assert forbidden execution/access faults without breaking syscall/interrupt return; physical acceptance includes multiple vendor CPUs, firmware Secure Boot modes, DMA-capable devices and boot rollback. Fault injection covers unsupported CPUID, MSR fault/denial, feature mismatch on AP, TLB shootdown timeout, malformed image, invalid IOCTL and audit-buffer exhaustion. No test may assert a mitigation merely from a status string.

## References

- [Intel 64 and IA-32 SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), [AMD64 APM](https://www.amd.com/en/search/documentation/hub.html), [Microsoft driver security checklist](https://learn.microsoft.com/en-us/windows-hardware/drivers/driversecurity/driver-security-checklist).
- MicaNT [architecture](KERNEL_ARCHITECTURE.md), [IOMMU](KERNEL_IOMMU_DMA_PROTECTION_SPEC.md), [crash/debug](KERNEL_CRASH_DEBUG_RECOVERY_SPEC.md), [clean-room policy](CLEAN_ROOM.md).
