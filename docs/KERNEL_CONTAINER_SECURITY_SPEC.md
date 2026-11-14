# MicaNT — Container Security and Isolation Policy Specification

**Status:** Proposed  
**Primary owners:** Se/Ob/Ps/Io/Mm, canonical network WFP enforcement  
**Related:** `KERNEL_PROCESS_SILO_SPEC.md`, `KERNEL_RESOURCE_GROUPS_SPEC.md`, `KERNEL_NETWORK_NAMESPACE_SPEC.md`, `UNARYFS_FUNCTIONAL_SPEC.md` (when present)

## 1. Purpose

Define the security contract for container-like workloads in MicaNT. The kernel owns enforcement; Ring 3 owns policy authoring and orchestration. This is not permission to execute arbitrary installer, driver or application code with kernel privileges. A silo, resource group, UnaryFS view and network namespace are independently useful resources coordinated under a single least-privilege policy.

Container isolation is a **shared-kernel boundary**, not VM-grade isolation. Root/Administrator inside a container must not map to unrestricted host kernel or filesystem authority. Never claim that an untrusted NT driver is safely contained just by executing its controlling application in a silo.

## 2. Threat model

**Untrusted:** application PE, dependencies, child processes, file paths, registry content, user buffers, network traffic and image metadata.  
**Trusted:** canonical kernel enforcement; a narrowly privileged host manager and its authenticated configuration, when implemented and verified.  
**Adversaries:** hostile process attempting cross-silo access, path escape, raw disk write, privileged IOCTL, DMA setup, handle smuggling, registry/namespace shadowing, resource exhaustion or network escape.  
**Not fully covered:** CPU speculative side channels, arbitrary Ring 0 driver execution, malicious firmware, physical attacks and hardware flaws; require separate mitigations or virtualization.

## 3. Policy object and enforcement

One immutable-at-use **ContainerPolicy** is signed or authorized by the trusted manager (signature and trust mechanism to be specified for deployment), copied into kernel-owned state after validation, and bound before any user thread runs. The first release can use a host-owned opaque policy object with versioned configuration and ACL-based authorization; do not invent a Windows public API.

Policy categories:

| Category | Minimum restriction |
|---|---|
| Ps | Process/thread opens and cross-silo memory/APC denied by default |
| Ob | Namespace and handle transfer controls, privileged broker exceptions |
| Se | Token/privilege intersection; no elevation through container membership |
| Io | Allowed device classes, IOCTL restriction, direct-volume/raw-device access denied |
| UnaryFS | Read-only BaseFS, scoped writable layers, immutable objects, no host filesystem bypass |
| Cm | Registry scope or explicit unsupported status; no unrestricted host HKLM mutation |
| Network | Namespace bind, controlled egress/ingress, WFP firewall checks |
| Resource | Required quotas, no unbounded group creation |
| Loader | Controlled DLL/image resolution and no arbitrary system executable shadowing |

## 4. Filesystem isolation

Filesystem access follows canonical Io/FsRtl/Ob/Se checks. UnaryFS can provide scoped overlay views but is **not** itself a complete security sandbox. Host-protected files remain read-only or immutable independent of layer order. Enforce canonical identity checks against path aliasing, hard links, reparse points, already-open handles and writeable memory mappings. Prevent raw-device access from bypassing overlays. A host-approved bind mount must expose precisely the intended path and access mode; read-only binds deny write, rename, delete, metadata changes and writable sections.

If UnaryFS is absent or fails to initialize, a configured container must fail closed rather than fall back to host System32 with write access.

## 5. Token, objects and process boundaries

- Default-deny cross-silo process and thread opens, direct ClientId access, injection, APC and cross-process memory operations.
- Strip or deny privileges unnecessary for workload; do not equate container Administrator with host Administrator.
- Handle inheritance and duplication require destination checks before publication; no leaking privileged kernel handles.
- Host broker access is narrow, attributable, time-bounded where possible and audited locally.
- Named synchronization objects and IPC must follow configured namespace scope; explicit host-shared services use separate ACLs and allowlists.
- Protected resources cannot be modified merely by changing file attributes, the process token, or layer metadata.

## 6. Devices and drivers

Reject unapproved direct PCI configuration, MMIO, physical-memory mappings, `/Device` openings, kernel driver loading and raw block access. Constrain IoControl requests by the actual exposed device object's security policy and code-level validation. A device passthrough capability is a future privileged feature and needs IOMMU/DMA isolation and a dedicated threat review; it is not implied by a container ID.

## 7. Network and isolation

The network namespace must enforce bind/route/interface separation; WFP is the single enforcement datapath. Incoming host traffic is blocked unless explicitly published. Default egress policy is configurable and must remain consistent with address/port translations. A DNS or proxy configuration cannot silently grant direct host service access.

## 8. Resource exhaustion and denial of service

Memory, process/thread and handle limits should be enforced by resource groups. Limit creation and management requests, nested child creation, open sockets, pending I/O and packet memory. Kernel cleanup workers and system reserves must remain runnable when a container exhausts its quota. Use bounded parsing for image metadata, IPC, filesystem layers and policy records.

## 9. Management and lifecycle

A trusted Ring 3 service performs create -> configure -> activate -> stop -> delete using kernel handles to the canonical objects. No partially configured container becomes runnable. On service crash, kernel invariants persist; a documented recovery process either reattaches or drains the container. Teardown invalidates policy changes safely but preserves in-use object references until completed.

## 10. Security verification stages

- **CS0 — Enforcement inventory:** map all direct and indirect access paths; identify gaps and fail-closed dependencies.
- **CS1 — Process/Ob boundary:** deny cross-silo opens, handle inheritance/duplication and object namespace escape.
- **CS2 — File boundary:** prove UnaryFS confinement and immutable system protection, including sections, aliases, reparse and raw device paths.
- **CS3 — Network/device boundary:** block unauthorized interface/port, IOCTL, MMIO and driver load requests.
- **CS4 — Policy lifecycle:** privilege changes, revocation, manager failure, process termination and rollback.
- **CS5 — Fuzz/fault/SMP:** hostile paths, malformed policies, fail-injection, race conditions, multiple CPUs.

## 11. Mandatory negative tests

- Known PID in another silo, duplicate of a high-right handle, inherited handle from privileged parent.
- `NtReadVirtualMemory`/write, APC injection, thread open and debug/inspect against unauthorized target.
- System32 overlay with a base immutable object, whiteout/rename/metadata override attempt, mapped-file write and raw volume opening.
- Symlink/reparse traversal, hard links, case folding, parent path, alias and path race.
- Attempts to load kernel `.sys`, map MMIO or issue privileged storage/PCI IOCTLs.
- Cross-namespace port bind, interface reconfiguration, unknown ingress and unexpected broadcast.
- Quota exhaustion, partial startup, manager termination and simultaneous container teardown.

Each test must verify **the host remains unchanged**, not merely that the API returned an error. Kernel faults and leaks are failures. Reproduce tests on real QEMU freestanding artifacts; keep hardware evidence separate.

## 12. Definition of done

For the advertised supported profile, an unprivileged Ring 3 container cannot modify protected host state or access another container's protected objects or network endpoints, and cannot gain host privileges via device access, handles or namespace aliases. Every exception is explicitly authorized and tested. Documentation must not claim a complete security sandbox until all specified enforcement boundaries have been verified.
