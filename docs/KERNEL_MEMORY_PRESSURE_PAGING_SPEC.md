# MicaNT Memory Pressure, Paging and Reclamation Specification

**Status:** Proposed; the bootstrap PFN allocator and existing address-space work do not imply working sets, paging or pagefile support. **Owner:** canonical Mm; Io/Cc/FsRtl backing I/O; Ps address-space lifecycle; Ke waits/work; Ob references.

## Objective and boundaries

Extend the existing Memory Manager to reclaim resident memory, resolve file-backed faults and report resource exhaustion with NT-compatible failure semantics. Do not create another allocator, page cache, VAD tree or hosted substitute. Current integration status documents bounded allocator/address-space evidence; verify source state before implementation. NUMA is a later extension.

## Page state and accounting

Mm owns PFN state and explicit references for process mappings, cache pages, I/O pins and page-table use. Define legal transitions among free, zeroed, active, standby, modified, transition, pinned/locked and bad pages; counters must reconcile and prevent double-free. Zero pages before user exposure. Commit accounting reserves backing capacity before promising pageable private memory; distinguish reservation, resident, shared/file-backed and nonpageable pool. Commit limit derives from usable RAM plus configured backing store, not an arbitrary constant. Allocation failure returns the correct status and cleans partial mappings/references.

Working sets are per-process resident sets with bounded metadata; trimming selects reclaimable pages under pressure while respecting locked pages, active faults and shared mappings. File-backed pages use one canonical Cc/Mm coherence identity. Modified data is written through Io to the backing file before reclaim; standby clean pages may be reused after reference/race checks. Pagefile is a separate Mm backing object on a validated block/file path and must not be used until M14 flush/recovery semantics are proven. COW reserves commit and creates a private zeroed copy atomically; concurrent faults/unmap/terminate coordinate on VAD/page state and process rundown.

## Fault, I/O and teardown behavior

Fault path validates address, protection, user/kernel origin and process lifetime before allocating. Soft faults reuse resident/standby pages; hard file/pagefile faults queue passive I/O and wait only at legal IRQL. No storage I/O while holding PFN or page-table spinlocks. In-flight page-in holds process, VAD, file/section, PFN and IRP references; after completion it rechecks mapping generation and access, discarding safely if unmapped or terminated. Dirty/writeback errors remain visible and prevent false clean state. Unsupported backing types fail explicitly.

Low-memory policy first trims clean reclaimable working sets and cache, then writes modified pages if configured. Notify processes through documented-compatible mechanisms only when implemented. Kernel nonpaged pool exhaustion must fail allocation and trigger bounded diagnostics; never reclaim memory backing a live DMA or locked buffer. OOM selection is deterministic, excludes critical kernel workers according to explicit policy, and terminates through Ps rundown without violating object references. Lazy loading is allowed only for immutable, validated image/file regions.

## Phases and acceptance

| Phase | Gate |
|---|---|
| MMP0 | PFN references/state audit, commit accounting and fault-race model. |
| MMP1 | Working-set metadata and clean-page trimming; no pagefile yet. |
| MMP2 | Cc/file-backed read paging with coherent mapped-file identity. |
| MMP3 | Dirty tracking/writeback, standby/modified transitions and flush failure semantics. |
| MMP4 | Pagefile, swap I/O, commit limits and OOM recovery. |
| MMP5 | Prolonged pressure stress, cache eviction and optional large pages/NUMA planning. |

Acceptance: near-exhaustion recovers clean pages; concurrent allocations preserve PFN accounting; COW under pressure isolates contents; file-cache eviction rereads correct bytes; pagefile reads/writes survive restart policy; process termination during page-in drains references; injected I/O failures return errors; zeroing prevents cross-process disclosure; repeated stress returns reclaimable PFNs. QEMU runs use bounded RAM and a disposable persistent pagefile volume. Physical tests measure latency/working-set behavior and verify failure handling with real storage. Hosted tests may validate accounting/state transitions only. Inject allocation failure at every phase, unmap during page-in, disk removal, short I/O, writeback failure and PFN exhaustion.

## Security and limits

Do not expose stale physical contents, trust user page tables, or permit unbounded page-fault recursion. Kernel stack, interrupt, DMA, page-table and locked pages need explicit nonpageable/lifetime policy. Large pages only where alignment, backing and split-on-demand behavior are implemented. Windows working-set semantics guide public behavior; internal algorithms are MicaNT designs.

## References

- Microsoft [Working Set](https://learn.microsoft.com/en-us/windows/win32/memory/working-set), [Memory management](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/memory-management).
- MicaNT [architecture](KERNEL_ARCHITECTURE.md), [filesystem/cache and persistence](KERNEL_FILESYSTEM_SPEC.md), [milestones](KERNEL_MILESTONES.md).
