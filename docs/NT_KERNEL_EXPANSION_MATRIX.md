# NT Kernel Expansion Matrix

This is a working comparison inventory, not a feature-completion claim. The
ReactOS paths below were checked against
[`reactos/reactos` revision `8fe3cee05606aecd9442674e40038649f048d10d`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d).
For current MicaNT claims, see
[`KERNEL_ARCHITECTURE.md`](KERNEL_ARCHITECTURE.md) and
[`KERNEL_INTEGRATION_STATUS.md`](KERNEL_INTEGRATION_STATUS.md). Run
[`nt_reference_inventory.py`](../scripts/nt_reference_inventory.py) against a
local ReactOS checkout to discover source files and candidate declarations:

```bash
python3 scripts/nt_reference_inventory.py /path/to/reactos \
  --output /tmp/reactos-nt-inventory.json
```

Its output is lexical; it does not infer operation semantics or completeness.

## Status key

- **Verified (bounded)** — named behavior exercised by the cited local test or
  QEMU path; this does not imply subsystem completeness.
- **Partial / not integrated** — relevant code exists but the full NT path or
  required behavior is absent or not exercised.
- **Unverified** — source declarations or implementations exist, but no
  applicable behavior test has been established here.
- **Not present in the active boot path** — no connected implementation was
  identified for the named behavior.

## Subsystem comparison

| Area | ReactOS source areas and behavioral responsibilities | Dependencies and relevant tests | MicaNT evidence and current status |
|---|---|---|---|
| **Ke — Kernel Executive** | [`ntoskrnl/ke/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/ke): dispatcher and wait semantics, thread scheduling, interrupts, timers, APC/DPC, synchronization, context switching. | Requires architecture support, interrupt/timer delivery, and Mm/Ps integration. ReactOS kernel tests: [`modules/rostests/kmtests/ntos_ke`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_ke). | A cooperative AMD64 context switch between real kernel stacks, including Windows x64 nonvolatile GPR/SIMD and FP control preservation, is verified by the bootstrap QEMU test. Boot remains single-CPU and polled; interrupt-driven preemption, a scheduler, and multi-thread workload execution are not established. |
| **Mm — Memory Manager** | [`ntoskrnl/mm/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/mm), including ARM3: physical-page lists, virtual mappings, page faults, sections, system PTEs, MDLs, image loading. | Depends on boot memory map, architecture page tables, synchronization, and I/O for paging. Tests: [`modules/rostests/kmtests/ntos_mm`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_mm). | The canonical PFN database is sized from the UEFI conventional-memory map; 2/4 GiB QEMU boots exercised allocation, access and reclamation above 1 GiB. The physical access aperture is temporary and validated. General VAD/section-backed paging remains bounded to the AMD64 backend address slot and is not general NT VM support; see [`mm.hpp`](../include/micant/km/executive/mm.hpp). |
| **Ps — Process Manager** | [`ntoskrnl/ps/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/ps): process/thread creation and teardown, scheduling handoff, image startup, job/accounting, notifications. | Depends on Ke, Mm, Ob, Se, loader, and system services. Tests: [`modules/rostests/kmtests/ntos_ps`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_ps). | Process/thread data structures and hosted manager paths exist in [`ps.hpp`](../include/micant/km/executive/ps.hpp). A separately scheduled Ring 3 process with independent address space, full loader lifecycle, and QEMU termination/reclamation is not established. |
| **Ob — Object Manager** | [`ntoskrnl/ob/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/ob): object types, references, namespaces, handle tables, access checks and teardown. | Depends on Ex allocation, Ps process handles, and Se access control. Tests: [`modules/rostests/kmtests/ntos_ob`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_ob). | Canonical global handle table initialization and bounded create/duplicate/reference/close accounting are verified in QEMU; full namespace and native `NtClose` integration remain incomplete. See [`ob.hpp`](../include/micant/km/executive/ob.hpp) and [`KERNEL_INTEGRATION_STATUS.md`](KERNEL_INTEGRATION_STATUS.md). |
| **Io — I/O Manager** | [`ntoskrnl/io/iomgr/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/io/iomgr): driver/device lifecycle and stacks, IRP allocation/dispatch/completion/cancel, file objects, interrupt objects, MDL I/O. | Depends on Ob, Mm, Ke, Cm, PnP, and filesystem drivers. Tests: [`modules/rostests/kmtests/ntos_io`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_io). | I/O structures and hosted operations exist in [`io.hpp`](../include/micant/km/executive/io.hpp). The QEMU VirtIO display driver uses a private boot-driver contract, not WDK loading or a complete Io/IRP stack. Full IRP/device-stack driver loading is not present in the active boot path. |
| **Cm — Configuration Manager** | [`ntoskrnl/config/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/config): registry hive/cell management, key/value syscalls, boot hive loading, KCB/NCB caching, notifications and security. | Depends on Ob, Mm, Cc, Se, and boot/storage. Tests: [`modules/rostests/kmtests/ntos_cm`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_cm). | An in-memory registry implementation is declared in the user/kernel subsystem sources; persistence, boot hive loading, and integrated syscall security are not demonstrated by current QEMU evidence. |
| **Se — Security Reference Monitor** | [`ntoskrnl/se/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/se): tokens, SID/ACL/SD operations, access checks, impersonation, privileges and auditing. | Depends on Ob, Ps, and Cm. Tests: [`modules/rostests/kmtests/ntos_se`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_se). | Security types and host-side routines exist; a complete kernel access-check path integrated with object handles and QEMU is not verified. Treat declarations in [`se.hpp`](../include/micant/km/executive/se.hpp) as unverified until behavior tests establish their contracts. |
| **Cc — Cache Manager** | [`ntoskrnl/cc/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/cc): cache maps/views, copy and pin I/O, MDL reads, lazy writer and file-size synchronization. | Depends on Mm, Io, FsRtl, and filesystem implementations. Tests: [`modules/rostests/kmtests/ntos_cc`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_cc). | Cache-related code is present in [`cc.hpp`](../include/micant/km/executive/cc.hpp); persistent-file and filesystem integration through a kernel cache manager is not established. |
| **FsRtl — Filesystem Runtime** | [`ntoskrnl/fsrtl/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/fsrtl): name matching, locks, notifications, MCB mapping, oplocks, fast I/O, filter support and filesystem notifications. | Depends on Io, Ob, Ke and the filesystem driver; tests: [`modules/rostests/kmtests/ntos_fsrtl`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_fsrtl). | Helper implementations are present in [`fsrtl.hpp`](../include/micant/km/executive/fsrtl.hpp), but kernel filesystem/IRP integration and corresponding QEMU behavior are not verified. |
| **Ex — Executive Support** | [`ntoskrnl/ex/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/ex): pool and lookaside allocation, push locks/resources, rundown, callbacks, work items, executive support routines. | Foundational dependency for Ke, Ob, Mm, Io, Ps and Se. Tests are distributed across subsystem suites and [`modules/rostests/kmtests/ntos_ex`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/ntos_ex). | PFN-backed C/C++ bootstrap allocation and global constructor/destructor dispatch are verified in QEMU. This is not a general Windows-compatible paged/nonpaged pool or complete Ex surface. |
| **Lpc — Local Procedure Call** | [`ntoskrnl/lpc/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/lpc): port creation/connect/listen, send/reply, request-wait-reply and message lifecycle. | Depends on Ob, Ps, Ke, Mm and security; the ReactOS research snapshot did not identify a dedicated `ntos_lpc` kernel-mode test directory. | LPC declarations and user-mode service code exist, but kernel-native port syscalls and an integrated service rendezvous are not verified in the boot path. |
| **Po — Power Manager** | [`ntoskrnl/po/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/po): power IRPs, device/system power transitions, callbacks, idle/wake handling. | Depends on Io/PnP, Ke timers, and HAL/ACPI. Tests are partly exercised through driver and power-related suites. | Headers and power-management code are present; device/system power lifecycle is not verified in QEMU. |
| **PnP — Plug and Play** | [`ntoskrnl/io/pnpmgr/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/ntoskrnl/io/pnpmgr): device-node enumeration, resource arbitration, start/remove IRPs, interfaces and driver matching. | Depends on Io, Cm, Ob, Mm, Se and HAL buses; tests: `modules/rostests/kmtests/ntos_io` and bus-specific tests. | Device declarations and bus implementations exist, but active boot is a bounded UEFI handoff with one private display-driver entry. General bus enumeration, resource arbitration, and PnP driver start/remove are not verified. |
| **HAL — Hardware Abstraction Layer** | [`hal/halx86/`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/hal/halx86): platform startup, interrupt controller/timer variants, ACPI bus integration, SMP/IPI and DMA services. | Depends on architecture startup, firmware tables, Ke interrupt/timer abstractions; tests: [`modules/rostests/kmtests/hal`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d/modules/rostests/kmtests/hal). | AMD64 UEFI/OVMF handoff, page-table/protection diagnostics, syscall entry/return, and VirtIO 2D boot path are verified in QEMU. General APIC timer interrupt dispatch, SMP, and a WDK-compatible HAL service surface are not established. |

## Functional analysis checklist

For each area, extend this matrix with specific operations, not just symbol
names. For each operation record: contract/source reference; MicaNT owner and
implementation; preconditions and failure statuses; synchronization and
lifecycle; edge cases; available upstream and MicaNT tests; boot, driver, and
service dependencies; and the test level that actually passed. Keep separate
rows for implemented-and-tested, implemented-but-untested, partial, incorrect,
simulated/hardcoded, missing, duplicate, and not-applicable behavior.

### Boot, driver, and service dependency paths

These are coverage questions for the selected NT profile, not claims that
MicaNT currently implements them.

| Area | Boot-critical questions | Driver execution questions | NT service execution questions |
|---|---|---|---|
| Ke | Can boot establish an idle thread and transition to the dispatcher? | Are IRQL, interrupt, DPC/APC, wait, and timer rules real at required execution levels? | Can service threads block, wake, receive APCs, and be scheduled fairly? |
| Mm | Are kernel mappings, page allocation, and boot image mappings established before later phases? | Are nonpaged buffers, MDLs, DMA mappings, and device-memory protections correct? | Can separate process address spaces, stacks, image sections, and demand faults be created and reclaimed? |
| Ps | Is a system process/thread created with a valid address space and startup context? | Can drivers create/terminate system threads and safely observe process/thread notifications? | Can native processes and threads start, wait, terminate, and release resources? |
| Ob | Are required object types and early handles initialized in dependency order? | Do device/file objects, reference counts, access checks, and handle visibility follow the DDI? | Are per-process handles, namespace objects, inheritance, and close semantics correct? |
| Io | Are boot-start drivers selected, ordered, loaded, and initialized from configuration? | Do device stacks, IRP pending/cancel/completion, interrupts, and unload/remove paths work? | Can services open devices/files and issue synchronous/asynchronous I/O through Nt calls? |
| Cm | Are system hives mounted before driver/service configuration is required? | Can drivers query service keys, parameters, and configuration notifications? | Can service configuration be read and updated persistently with expected access control? |
| Se | Are the initial system token and security state initialized before protected objects? | Are driver/device ACLs, impersonation, and access-state checks correct? | Do service logon tokens, privileges, impersonation, and object access checks work? |
| Cc | Can required boot files be read without relying on an unavailable cache path? | Are cache maps, pinned data, MDL I/O, flush ordering, and lazy writes coherent? | Are file reads/writes and executable image data coherent and persistent? |
| FsRtl | Are filesystem mount/notification helpers available before services need storage? | Do file locks, oplocks, name matching, notifications, and fast-I/O fallbacks satisfy their contracts? | Can services use filesystem paths, directory notifications, and file-locking behavior? |
| Ex | Is early allocation and C++ lifecycle initialized before dependent subsystems? | Are pool, work-item, callback, rundown, and synchronization contracts available at required IRQL? | Can worker/callback facilities execute and drain during service/process shutdown? |
| Lpc | Are the system service process and its initial communication ports established? | Which documented driver-facing (if any) IPC interfaces are required by the profile? | Can services create/connect ports, exchange bounded messages, and recover from peer exit? |
| Po | Are firmware/platform power capabilities discovered without blocking basic startup? | Do power IRPs, device state transitions, and completion ordering work? | Are system power requests and service notifications delivered consistently? |
| PnP | Are boot devices available before driver-group and service startup ordering? | Can buses enumerate, arbitrate resources, start, rebalance, and remove device stacks? | Do device arrival/removal notifications and service dependencies follow transitions? |
| HAL | Does the handoff establish CPU state, memory mappings, traps, and required console/storage paths? | Are interrupts, timers, DMA, MMIO, and bus services delivered through canonical APIs? | Do system-information and timer services use hardware-backed sources and safe syscall paths? |

### Semantic cases to turn into tests

Examples to include when implementing the corresponding operation: Ke wait
timeouts/alertable APC delivery and ready-queue transitions; Mm zero-size,
overflow, protection, partial mapping, and teardown cases; Ps process-creation
rollback and deferred thread-stack reclamation; Ob access-mask denial,
inheritance, duplicate/close races, and final reference deletion; Io pending
completion, cancellation races, stack unwinding, remove-lock lifetime, and
driver failure rollback; Cm malformed hive/cell references, notification
ordering, and flush failure; Se ACL inheritance, restricted tokens, and
privilege checks; Cc dirty-page flush failure and concurrent file-size changes;
FsRtl wildcard/name corner cases, byte-range-lock conflicts, and notification
overflow; Ex allocation failure and work-queue shutdown; LPC peer termination
and message-size rejection; Po interrupted transitions; PnP resource conflict,
surprise removal, and restart; HAL spurious interrupts, timer rollover, and
device-removal races. These are test prompts, not evidence that ReactOS or
MicaNT has passed each case.
