# MicaNT — Process Silo: Functional and Implementation Specification

**Status:** Proposed; not implementation evidence  
**Target:** Canonical AMD64 C++23 NT Executive  
**Owners:** Ps (primary), Ob, Se, Ke, Io, Cm, Mm  
**Related:** `KERNEL_MILESTONES.md`, `KERNEL_ARCHITECTURE.md`, `KERNEL_INTEGRATION_STATUS.md`, `KERNEL_CONTAINER_SECURITY_SPEC.md`, `KERNEL_RESOURCE_GROUPS_SPEC.md`, `UNARYFS_FUNCTIONAL_SPEC.md` (when present)

## 1. Goal and boundary

Provide a kernel-enforced **process silo**: a lifetime-managed group of NT processes that shares a constrained view of selected host resources. A silo is a grouping and isolation mechanism, **not** a second Executive, not a virtual machine, and not an implementation of Docker itself. Silo membership is established by trusted process creation and is immutable for the lifetime of each member except through an explicit privileged transition whose semantics are independently specified and verified.

The first version isolates process identity and opens, selected Ob names, and process teardown. File and network views are optional handles to separately owned facilities; do not embed UnaryFS or TCP/IP logic in Ps. Do not promise full Windows Server silo or Windows container compatibility.

### Invariants

1. The host has a privileged root context. A process belongs to exactly one silo context at a time.
2. Default inter-silo process and thread access is denied **at reference/open time**, not merely hidden during enumeration.
3. An inherited or duplicated handle cannot bypass silo rules; authorization is checked at transfer, and pre-existing rights are handled by a stated revocation policy.
4. Child creation inherits its parent's silo unless a trusted creator explicitly selects an authorized destination.
5. A silo cannot gain privileges, device access or a stronger token merely by joining another silo.
6. Silo teardown completes after all process and kernel object references are released. No PID, kernel pointer or object memory is reused prematurely.
7. Processes inside one silo continue using canonical Ps/Ke/Mm; no scheduler or page-table implementation is duplicated.

## 2. Canonical model

- **Silo object:** Ob-backed kernel object with unique opaque ID, owner security descriptor, state, rundown protection, process membership references, optional resource-group reference and optional namespace/view references.
- **Membership:** stored in the canonical `EProcess` lifecycle; thread membership follows owning process. Do not rely solely on PEB or user-provided identifiers.
- **Scope:** host, silo A, silo B. First release has no arbitrary nested silo hierarchies. Add nesting only when its access and ownership semantics are demonstrated.
- **States:** `Created -> Active -> Stopping -> Draining -> Dead`. Failure during activation returns to `Created` with complete rollback; `Dead` cannot be reactivated.
- **Ownership:** silo references its policy objects; processes reference the silo; outstanding object references keep their actual backing objects alive independent of visibility.
- **Management interface:** private, versioned, access-checked NT control interface through an existing canonical owner. Names in this specification are conceptual and are **not** new public Windows APIs.

## 3. Process lifecycle

**Create:** validate caller rights, security descriptor, optional limits and namespace capabilities; allocate an Ob object and publish only after all subordinate references are ready. A failed user copyout must remove the unpublished handle and roll back subordinate ownership.  
**Launch:** validate the executable through the normal loader, bind the silo before exposing the child to scheduling, propagate context to children, and preserve the context across native process/thread APIs.  
**Open/query:** apply silo visibility and access checks before resolving process/thread references from IDs. Process enumeration may return only visible processes but must not be treated as security enforcement.  
**Duplicate/inherit:** constrain transfers to a destination silo using both source granted rights and destination policy. Privileged broker exceptions must be explicitly attributable and narrowly scoped.  
**Stop:** deny new members, request termination, cancel outstanding work where supported, release namespace/resource references after rundown. Never forcibly free running threads, active CR3 roots or mapped PFNs.

### Identity and special cases

- PID/TID uniqueness and stale-ID behavior follow canonical Ps rules.
- Host-owned system processes and kernel workers are not silently reclassified as members of application silos.
- User-mode brokers and shared services may be accessed through explicit named capabilities, not unrestricted cross-silo handles.
- Debug/inspect rights, process injection, cross-process memory access, section mapping and APC delivery require distinct checks. Process-name filtering is insufficient.
- Inherited console, pipe and standard I/O handles follow transfer policy; refuse invalid inheritance rather than substituting dummy handles.

## 4. Object namespaces

Introduce a **silo resolution context** at the canonical Ob name lookup boundary for eligible directories. Explicitly enumerate which object types are private, shared by policy, or always host-global. Avoid cloning the entire NT object directory. Named mutex/event, named pipe and related objects must be scoped by resolved object identity. Protect link traversal, symbolic links and handle-based reopen against path aliases.

The first version may support only a bounded private directory and specific object types; unsupported names fail explicitly. File and registry namespace overlays are separate capabilities to be added when the owning Io/FsRtl/Cm services are ready.

## 5. Synchronization and security

Use existing Ob/Ps guards, IRQL constraints and deferred cleanup. Any deletion that may invoke callbacks or blocking I/O must execute at a safe IRQL. Never hold a global membership lock across a context switch, page fault, process callback, wait or driver invocation. Use reference-counted or rundown-protected bindings so concurrently exiting processes cannot race silo destruction.

Access checks include caller token, object ACL, granted access, silo boundary policy and explicit broker exemptions. A privileged host manager cannot be spoofed by setting a silo ID in an ordinary syscall. Audit decisions locally without exporting telemetry.

## 6. Integration contracts

| Owner | Responsibility | Explicit non-responsibility |
|---|---|---|
| Ps | Membership, creation/termination, PID visibility | Filesystem resolution |
| Ob | Namespace, object references, handle duplication | Network routing |
| Se | Access policy and token boundaries | Inventing a new security token model |
| Ke/Mm | Thread scheduling, safe address-space lifecycle | Separate per-silo scheduler/MM |
| Io/FsRtl | Optional bound UnaryFS view and file access | Membership identity |
| Network stack | Optional network namespace reference | Silo lifetime owner |
| Ring 3 manager | Configuration, orchestration, reporting | Security enforcement |

## 7. Incremental delivery

- **S0 — Object and lifecycle:** create/destroy empty silo, permissions, reference and publish rollback, QEMU and host lifecycle tests.
- **S1 — Process binding:** create child inside silo, inherit to grandchildren, isolated PID enumeration, exit/teardown.
- **S2 — Security boundary:** cross-silo `NtOpenProcess`/`NtOpenThread`, direct ID opens, handle inheritance/duplication, APC and memory access denial; broker exceptions.
- **S3 — Named Ob scope:** two silos with identically named event/mutex objects, no leakage through aliases or cached handles.
- **S4 — Optional bindings:** attach independently verified resource group, UnaryFS view and network namespace with atomically published references.
- **S5 — Stress and recovery:** concurrent create/terminate/rundown, low-memory fault injection, repeated reboot and resource accounting.

## 8. Acceptance tests

**QEMU S0/S1:** two processes run in separate silos; child inherits correctly; unique IDs, no bogus process objects, all allocated resources reclaimed.  
**QEMU S2:** process A cannot open or duplicate unauthorized handles into B by PID, known handle, inheritance, APC or cross-process memory path; authorized host broker can perform one narrowly permitted operation.  
**QEMU S3:** same name in two silos resolves to different objects; object names and alternate paths cannot escape policy.  
**QEMU S4:** service can stop a silo while active I/O drains; no use-after-free, leaked PFN or handle.  
**Stress:** parallel exits, thread creation, handle duplication, untrusted buffer copyout failures, open races, refcount exhaustion and low-memory injection. Repeat at 1 BSP initially and, after SMP support, on 2+ vCPUs.  
**Hardware:** rerun lifecycle and isolation tests on real hardware, recording driver dependencies separately.

Success requires actual canonical freestanding execution. Hosted models and build success are not QEMU proof. An incomplete boundary must fail closed, with no synthetic success.

## 9. Out of scope

Full Windows container ABI, nested silos, universal device virtualization, Linux namespaces, a Linux PID ABI, a second scheduler, filesystem packaging and OCI image pulling. Those belong to later independently tested layers.

## 10. Completion gate

Silo lifecycle, process membership, inter-silo object access and teardown must be enforced and demonstrated from real CPL3 processes. Optional namespace facilities are not considered present until their owners have independent acceptance evidence. Maintain an evidence ledger without rewriting historical milestone results.
