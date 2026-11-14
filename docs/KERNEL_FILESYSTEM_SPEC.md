# MicaNT AMD64 — Filesystem Architecture and Implementation Specification

**Document:** `docs/KERNEL_FILESYSTEM_SPEC.md`  
**Status:** Proposed implementation specification (not a claim of existing support)  
**Target:** MicaNT modular NT-compatible kernel, C++23, AMD64/UEFI  
**Scope:** FAT32, ext4, NTFS, exFAT; later ReFS and ZFS; CDFS/UDF as optional read-only drivers  
**Relationship:** `docs/KERNEL_ARCHITECTURE.md`, `docs/KERNEL_MILESTONES.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, `AGENTS.md`

## 1. Executive decision

MicaNT shall implement **one canonical NT I/O and filesystem stack**, with filesystem-specific drivers beneath a common Object Manager / I/O Manager contract. No filesystem shall implement a competing handle table, process manager, VAD allocator, block cache, or independent public NT API.

Recommended rollout:

1. **FAT32:** preserve UEFI EFI System Partition (ESP) interoperability; audit and complete kernel-mode FAT32 access as needed. Do not confuse UEFI firmware file access with a native NT filesystem driver.
2. **ext4 read-only:** first modern on-disk filesystem integrated into the freestanding kernel, using genuine block-device reads. Use it to enable file-backed `SEC_IMAGE` and the first real PE process from persistent storage.
3. **ext4 read-write:** add crash-safe metadata/data write paths, JBD2 journaling/recovery and robust flush/barrier support; enable writing only after recovery tests pass.
4. **NTFS:** implement a progressively complete on-disk driver for Windows compatibility, initially read-only, then write-capable once its own consistency model is validated.
5. **exFAT:** support removable media with robust read-only then read-write operation, including allocation bitmap and safe flush behavior.
6. **ReFS and ZFS:** long-term, isolated initiatives requiring distinct, carefully validated integrity, transaction and recovery models. Existing hosted simulators are **not** substitutes for on-disk implementations.
7. **CDFS / UDF:** optional separate read-only optical/ISO-image targets; not prerequisites for the first PE launch.

**Non-goals for the initial filesystem milestone:** a complete NTFS implementation; complete NTFS ACL equivalence on ext4; production ZFS/ReFS; arbitrary Windows driver compatibility; an entire Linux VFS inside NT; claims of bare-metal readiness based on hosted unit tests.

## 2. Verified source inventory versus operational status

At the repository state examined in November 2026, the source tree contains:

| Component | Existing code | What has **not** been established |
|---|---|---|
| FAT32 | `include/micant/km/drivers/fs/fat32.hpp` implements FAT structures and device-oriented operations | A fully integrated, production NT filesystem driver behind real freestanding IRPs |
| ext2/ext3/ext4 | `kernel/fsrtl/extfs.cpp`, `include/micant/km/drivers/fs/extfs.hpp`; superblock, inode, extent, directory and path parsing; described as **read-only** | Freestanding linkage, real device mount, native `IRP_MJ_READ`, journaling or write support |
| NTFS | `kernel/fsrtl/ntfs.cpp`, `include/micant/km/drivers/fs/ntfs.hpp` | Complete real-disk I/O and correct MFT/NT contracts in bare-metal; several IRP routines currently report success without performing I/O |
| exFAT | `kernel/fsrtl/exfat.cpp`, `include/micant/km/drivers/fs/exfat.hpp` | Real bare-metal integration; Fast I/O and other routines currently have placeholder success paths |
| ReFS | `kernel/fsrtl/refs.cpp` and associated headers | Production ReFS on-disk compatibility, transactional integrity or crash recovery |
| ZFS | `kernel/fsrtl/zfs.cpp` and associated headers; a RAM-backed demonstration pool | Real-disk OpenZFS compatibility, production pool import and durability |
| CDFS / UDF | `kernel/fsrtl/cdfs.cpp` and relevant headers | Freestanding optical filesystem service verified end to end |

**Build evidence:** the canonical hosted `NTOSKRNL_SUBSYSTEM_SOURCES` lists extfs, NTFS, exFAT, ReFS, ZFS and CDFS. The `ntoskrnl_core_baremetal` target inspected did **not** link those filesystem translation units. Source presence, nominal driver registration, passing hosted tests and fabricated `STATUS_SUCCESS` paths are not evidence of usable filesystem support.

This inventory is a dated baseline: before implementation, recheck the current worktree and preserve uncommitted changes.

## 3. Architecture: one NT filesystem pipeline

```text
Native user caller / system service
           |
   NTDLL NtCreateFile / NtReadFile / NtWriteFile / NtQueryDirectoryFile
           |
       SYSCALL + buffer capture
           |
       Object Manager (Ob)
           |
       I/O Manager (Io): FILE_OBJECT, DEVICE_OBJECT, IRP, completion
           |
       Volume / mount manager (GPT, partitions, VPB-equivalent)
           |
       Filesystem driver dispatch (FAT32 / ext4 / NTFS / exFAT / ...)
           |
       Cache Manager (Cc) / FsRtl, as available
           |
       Block device stack (VirtIO-blk first; AHCI/NVMe later)
           |
       Real disk / QEMU disk image

  File object + section backing ----> Mm SEC_IMAGE / SEC_COMMIT
                                          |
                                  Ps PE process startup
```

### 3.1 Ownership boundaries

- **Hal / Bus / Storage:** PCI discovery, device MMIO, IRQ/DMA, sector read/write, queue completion, cache flush and barriers.
- **Io:** disk/partition/volume/file device objects, IRP dispatch, cancellation, completion, file object lifecycle, share-access and create/open semantics.
- **FsRtl / filesystem driver:** on-disk format, directory lookup, extent translation, metadata updates, journal and per-volume locks.
- **Cc:** coherent caching with dirty tracking, writeback and invalidation. A bounded uncached path is acceptable initially.
- **Ob:** typed objects, handles, access checks, references; handles are not raw pointers or fabricated IDs.
- **Mm:** file-backed sections, `SEC_IMAGE` image mapping, shared PFNs, page fault and image lifetime, coherent file/page-cache interactions.
- **Ps / Ke:** process and thread lifecycle, file-backed PE launch, scheduling and cancellation; no filesystem-owned process manager.
- **Se:** NT access token and security descriptor authorization. POSIX permissions alone are not full NT security semantics.

File system drivers may provide private helpers but cannot circumvent this pipeline in public syscall paths.

### 3.2 Mode and execution constraints

- x86_64/AMD64 only; use little-endian on-disk structures with explicit bounded parsing.
- Do not use host `std::mutex`, host file APIs, pthreads, host exceptions, or an imported STL runtime from the PE kernel. Adapt algorithms to freestanding allocation, synchronization and I/O.
- I/O can block only at an appropriate IRQL. Never wait for storage completion while holding spinlocks or with interrupts disabled.
- Error paths must return accurate NTSTATUS values, maintain `IoStatus.Information`, and leave buffers untouched or correctly bounded on failure.
- Respect the user-mode / kernel-mode boundary: NTDLL and Kernel32 cannot directly reference private `micant/km/` internals.
- Never return `STATUS_SUCCESS` for a read/write/flush operation that has not actually completed or been safely queued under the applicable contract.

## 4. Foundation milestone F0 — Block I/O and file objects

**This is a prerequisite for all real-disk filesystem claims and for file-backed `SEC_IMAGE`.**

### 4.1 VirtIO-blk first

Implement or complete a native AMD64 VirtIO-blk backend in the storage subsystem, without inventing a parallel disk stack:

- PCI device identification, BAR/MMIO or negotiated transport, feature negotiation and virtqueues.
- Capacity and logical block-size discovery; bounds and overflow-safe LBA calculation.
- Bounded scatter/gather descriptors and DMA-safe buffers; correct ownership until completion.
- Read, write, flush and failure status; complete requests exactly once.
- IRQ-driven completion when the interrupt infrastructure is ready; bounded polling only for explicitly documented early-boot situations.
- Device teardown, queue reset and timeout/error handling.
- QEMU persistence: use a **real backing disk image**, not a process-local synthetic byte array.

Define an internal `BlockDevice` interface with explicit `readBlocks`, `writeBlocks`, `flush`, block size, capacity and request completion. Do not standardize a new public NT ABI for it. Ensure it is usable by ext4 and the later NTFS/exFAT drivers.

### 4.2 Partitions and mount

- Read and validate GPT primary/backup headers, partition-entry bounds and checksums; permit a separately specified MBR fallback.
- Determine the volume's device offset and length; never let a filesystem read beyond its partition.
- Mount a volume only after validating its filesystem superblock/boot sector and feature bits.
- Expose an NT-style `\Device\HarddiskVolumeN`-equivalent object and namespace mapping; drive letters are policy, not filesystem identifiers.
- Maintain mounted/unmounting state; reject new opens during teardown and wait for or cancel outstanding operations safely.

### 4.3 File object and IRP baseline

Minimum required operations:

| IRP / Native service | Required behavior |
|---|---|
| `IRP_MJ_CREATE` / `NtCreateFile` | Resolve name, disposition, desired access, share flags; create a referenced file object |
| `IRP_MJ_READ` / `NtReadFile` | Read real disk contents at a checked file offset; correct EOF, byte count and completion |
| `IRP_MJ_WRITE` / `NtWriteFile` | Reject on read-only volumes; later queue real writes with durable flush semantics |
| `IRP_MJ_DIRECTORY_CONTROL` | Enumerate actual directory entries with bounded output and continuation state |
| `IRP_MJ_QUERY_INFORMATION` | File size, type, timestamps and supported attributes; explicit status for unsupported classes |
| `IRP_MJ_CLEANUP` | Release per-handle resources and pending operations at last handle close |
| `IRP_MJ_CLOSE` | Drop remaining file-object references without double-free |
| `IRP_MJ_FLUSH_BUFFERS` | Perform/propagate actual device flush when write-enabled |
| `NtCreateSection(SEC_IMAGE)` | Take a referenced file object; preserve backing until the last image view is gone |

Do not mark the driver integrated merely because it registers `IRP_MJ_*` callbacks.

### F0 acceptance

- QEMU mounts a real partition from a persistent virtual disk image.
- A kernel service opens and reads a known file, verifies bytes and EOF, and closes it.
- A failed read returns failure; it does not silently return zeroes or stale data as success.
- Repeated open/read/close restores file/volume references and pool/PFN accounting.
- Invalid partition boundaries, bad filesystem signatures and an unplugged/erroring block device fail safely.

## 5. F1 — ext4 read-only: first modern system volume

**Choice:** ext4 initially read-only on a genuine disk image; FAT32 remains the ESP filesystem.

### 5.1 Use and adapt existing code

Start from `kernel/fsrtl/extfs.cpp` and `include/micant/km/drivers/fs/extfs.hpp`. Preserve correct superblock, inode and extent parsing; replace hosted ownership, vectors and locks where the freestanding runtime cannot support them. Do not duplicate the ext4 parser or use Linux host APIs in Ring 0.

### 5.2 On-disk validation

- Verify magic `0xEF53`, superblock size and supported block-size range.
- Handle feature flags according to ext4's compat / ro-compat / incompat rules: reject unknown **incompat** flags; for read-only operation handle unknown ro-compat flags conservatively and document supported features.
- Validate 64-bit block counts, group descriptor sizes, inode sizes, group/bitmap bounds and arithmetic against volume length.
- Handle extent-header depth and entry counts with strict validation; reject malformed trees, cycles, overflow and impossible physical ranges.
- Support sparse regions by returning zero-filled reads with correct byte count; never treat missing physical blocks as arbitrary address zero.
- Validate directory entry record length, name length and continuation; avoid unbounded allocation on corrupt metadata.
- Treat htree indexed directories and inline data as explicitly supported only after implementing/validating their on-disk semantics.
- Support Unicode/UTF-8 filename conversion correctly; do not replace arbitrary Unicode bytes with `?` and silently claim full filename compatibility.
- Symlinks: enforce depth/cycle limits and reject unsupported reparse semantics honestly.
- A volume requiring journal recovery must **not** be mounted writeable; initial policy may reject mount or allow an explicitly documented, consistency-constrained read-only mode.

### 5.3 File operations

Implement actual `IRP_MJ_CREATE`, `READ`, `DIRECTORY_CONTROL`, `QUERY_INFORMATION`, `CLEANUP` and `CLOSE` on the existing Io stack. All write/create/rename/truncate/deletion operations must return an appropriate read-only or unsupported status. Support offset-based reads for PE files and page-sized reads for Mm.

### 5.4 Minimal native process dependency

The first persistent-storage process-launch test shall:

1. Mount a known ext4 image in QEMU.
2. Open a real PE32+ file through `NtCreateFile` or its kernel equivalent.
3. Create a referenced `SEC_IMAGE` section from that file object.
4. Map headers and PE sections with correct RVA, file-size/virtual-size handling, zero fill and page protections.
5. Create a process-owned address space through Ps/Mm/Ob, map the image, and execute the first user-mode thread.
6. Close the file handle while the mapping is alive; show the image remains valid until the section and mapping references drain.
7. Exit and reclaim all file/section/process objects and PFNs.

The section's file-backed storage must come from the actual virtual disk. A compile-time embedded PE fixture does **not** satisfy this criterion.

### F1 acceptance

- Read-only ext4 mount, nested path lookup, directory enumeration and real 4 KiB/unaligned file reads verified in QEMU.
- Valid and intentionally corrupt images produce appropriate results; no kernel crash due to malformed metadata.
- An unclean filesystem with unresolved journal requirements is handled according to the explicit mount policy.
- The PE file-backed `SEC_IMAGE` launch passes end to end through canonical Io → Ob → Mm → Ps → Ke.

## 6. F2 — ext4 read-write with JBD2

**Safety gate:** do not enable writes on arbitrary ext4 media until transaction and recovery tests pass. Read-only mode remains the stable fallback.

### 6.1 Required write features

- Inode and block allocation and deallocation; accurate bitmaps and free counts.
- Extent insertion/splitting/merging and validated index updates.
- File and directory create/write/extend/truncate/rename/unlink; directory entry and link-count consistency.
- Correct sparse file semantics and size/mtime/ctime updates.
- Writeback ownership and cache invalidation coherent with Mm mapped pages and `SEC_IMAGE` rules.
- Concurrency between independent file objects and directory operations with defined lock order.

### 6.2 Journal and durability

- JBD2 transaction start, metadata logging, commit records and ordered flushes/barriers.
- Journal superblock validation, sequence handling, wraparound and recovery on next mount.
- Recovery shall be idempotent; never replay unvalidated data outside the volume.
- Honor flush/FUA requirements end to end: a successful `NtFlushBuffersFile` must not merely clear a dirty flag.
- Define the supported ext4 `data=ordered` semantics; do not claim `data=journal` or `data=writeback` support until implemented.
- Ensure metadata checksums and modern ext4 flags are either implemented or rejected before writes.

### F2 acceptance

- Create, overwrite, extend, rename, unlink and fsync real files; remount and verify contents.
- Crash injection at metadata/journal commit boundaries; remount, replay and run an independent filesystem checker where applicable.
- No lost references or allocator bitmap corruption after forced QEMU termination/power-cut tests.
- Read-only fallback on unsupported/inconsistent volumes.

## 7. F3 — NTFS for NT compatibility

**Why:** NTFS is the natural interoperability target for a Windows-compatible filesystem ABI, but a robust implementation is substantially more complex than extending the ext4 reader.

### 7.1 Initial read-only scope

- Validate NTFS boot sector, bytes/sector, sectors/cluster, MFT record sizing and volume boundaries.
- Parse `FILE` MFT records and update sequence arrays with proper fixups, strict record and attribute bounds.
- Implement resident/nonresident attributes, data runs, sparse ranges, `$ATTRIBUTE_LIST` when needed, and correct logical file reads.
- Resolve directories through `$INDEX_ROOT` / `$INDEX_ALLOCATION` and name namespaces; support Unicode/case-folded comparisons using appropriate NT behavior rather than ASCII-only lowering.
- Report file attributes, sizes, timestamps and identities through Io without fabricated values.
- Handle reparse points, hard links and alternate data streams only after their on-disk and object namespace semantics are implemented; otherwise return a clear unsupported status.
- Refuse writes and avoid pretending that a valid header implies a valid filesystem.

### 7.2 Read-write expansion

- MFT record allocation/reuse; attribute update, runlist allocation and directory index modification.
- `$Bitmap` and allocation consistency.
- `$LogFile`-compatible recovery semantics appropriate to supported NTFS versions, or a clearly restricted write policy until safe.
- NT security descriptors (`$Secure`), ACL inheritance, audit and Se integration; share modes, delete-pending, alternate streams, reparse points and byte-range locks.
- Correct name normalization/case behavior and Win32 namespace mapping.
- Write ordering, flush durability and crash-recovery tests on actual disk images.

### F3 acceptance

Mount and read known NTFS images in QEMU; validate nonresident data, nested directories, edge-case Unicode names and documented unsupported features. Require independent compatibility testing with Windows-created volumes before enabling general writes.

## 8. F4 — exFAT removable-media support

exFAT prioritizes interoperable removable storage; **it is not a replacement for a journaling system filesystem**.

### 8.1 Required support

- Verify volume boot region, backup boot region and checksums; geometry and cluster heap bounds.
- Parse allocation bitmap, upcase table, directory entry sets and filename hashes; reject inconsistencies and cycles.
- Correct contiguous/no-FAT-chain versus FAT-chain files, cluster allocation and EOC handling.
- Case-insensitive path matching according to exFAT upcase-table behavior.
- Read-only file and directory APIs first; then allocation, create/write/truncate/delete/rename and metadata updates.
- On removable devices, make flush, unmount and media-change handling explicit; never report an in-memory write as durable without device completion.

### F4 acceptance

Read files from independently formatted exFAT images in QEMU; write and remount when write support is enabled, validate with independent tools, and test interrupted writes and media removal.

## 9. F5 — ReFS and ZFS: optional advanced-storage tracks

### ReFS

- Treat current `refs.cpp` registration/reporting code as an unverified subsystem, not a disk-format implementation.
- Before writing a driver, identify supported on-disk ReFS versions and documented/verified structures. The format and compatibility constraints may prevent a faithful implementation from public specifications alone.
- Correctness targets would include checksummed metadata, copy-on-write tree updates, integrity streams where supported, salvage/recovery, snapshot/block cloning semantics and crash consistency.
- Do not mark service registration as ReFS volume support.

### ZFS

- Treat the current RAM-disk RAID-Z demonstration as a simulator, not OpenZFS-compatible disk support.
- A real implementation requires pool/vdev label import, on-disk metadata, checksummed block pointers, transaction groups, copy-on-write trees, ARC or an alternate validated cache, snapshots and crash recovery.
- Consider the license and integration implications of external OpenZFS implementations separately from MicaNT's own engineering rules.
- Defer production pool writes until independent import, scrub, replay and power-loss recovery tests pass.

**Priority:** neither ReFS nor ZFS should block ext4 → `SEC_IMAGE` → native processes, or the first generally usable NTFS/exFAT paths.

## 10. NT semantic compatibility over non-NT filesystems

A filesystem's on-disk format does **not** automatically implement NT filesystem semantics. The Io/Se/Ob boundary must define and test:

- `FILE_OBJECT` and device object references, handle duplication and cleanup vs close.
- Desired access, share-access checks, create dispositions, overwrite vs supersede, delete-on-close and delete-pending.
- Case sensitivity/insensitivity per namespace, Unicode conversion and reparse-point path resolution.
- Security descriptors/ACLs and ownership; ext4 UID/GID and POSIX mode bits are not equivalent to NT DACLs.
- File IDs, hard links, timestamps, byte-range locks and opportunistic-lock contracts when implemented.
- Alternate data streams: do not claim native ADS on ext4 unless a durable mapping scheme is specified; avoid silent lossy emulation.
- Memory-mapped files and `SEC_IMAGE`: page protection, file lifetime, mapped-view cleanup, truncation conflicts and cache coherence.
- Correct handling of directories as separate objects and the semantics of trailing separators, relative paths and NT namespace prefixes.

For unsupported semantics, return explicit NTSTATUS failures. Do not silently advertise Windows parity for a restricted ext4 volume.

## 11. Performance and reliability specification

**Do not assume ext4 is always faster than NTFS.** Benchmark the implemented kernel paths using identical QEMU disk configuration, file sizes, cache state and flush policy.

### Performance architecture

- Prefer asynchronously completed IRPs with bounded request batching and vectored I/O; preserve ordering where required.
- Use a shared Cc page/buffer cache rather than distinct, incoherent copies in each filesystem.
- Readahead for sequential reads; bounded metadata and inode caches with pressure-aware eviction.
- Avoid repeated inode and directory scans by implementing validated directory indexing where supported.
- Track actual service time from device submission through completion, not only CPU time in a parser.
- Add counters for cache hit/miss, read amplification, allocation failures, queue depth, flush latency and filesystem error returns.

### Reliability and security

- Treat all on-disk bytes as untrusted; check sizes before allocation and indexes before reads.
- Use checked 64-bit arithmetic for LBAs, byte ranges, cluster sizes and runlists; reject integer overflows.
- Bound recursion/tree depth, link resolution, journal length and filename length.
- Explicit lock ordering: Volume → directory/inode metadata → per-file state → cache, subject to the final Io/Cc contract. No blocking disk wait while holding a spinlock.
- Preserve backing when mapping cleanup fails; avoid double frees and use-after-free across Io, Ob and Mm.
- Verify dirty-state, checkpoint, flush and recovery semantics under power-loss testing before calling a write-capable volume production-ready.
- Separate fault-injection controls from production paths; never synthesize `STATUS_SUCCESS` to satisfy a smoke test.

## 12. Implementation batches and non-negotiable acceptance gates

| Batch | Deliverable | QEMU / integration gate |
|---|---|---|
| F0.1 | Native VirtIO-blk block reads, partition bounds and real image attachment | Known blocks read exactly, failed I/O returns failure |
| F0.2 | Io/Ob file object open/read/close and volume mount | Genuine file bytes flow through IRP dispatch |
| F1.1 | Freestanding ext4 read-only metadata and path traversal | Directories, symlinks under limits and noncontiguous extents verified |
| F1.2 | ext4 IRP read and errors | `NtCreateFile` + `NtReadFile` through syscall boundary in CPL3 |
| F1.3 | File-backed `SEC_IMAGE` and native PE launch | Io → Ob → Mm → Ps → Ke lifecycle verified, no embedded-image substitution |
| F2 | ext4 RW + JBD2 | Remount + independent checking + power-cut recovery |
| F3 | NTFS RO then RW | Independent NTFS images, then restricted write/recovery tests |
| F4 | exFAT RO then RW | Independently formatted/remounted removable images |
| F5 | ReFS/ZFS research and restricted pilots | No production-support claim without real on-disk tests |

**Do not begin F2/F3/F4/F5 merely to claim broader filesystem coverage while F0/F1 have not produced a real file-backed process.** Workers may analyze independently, but shared interfaces are integrated sequentially.

### Minimal test matrix for each implemented filesystem

1. Valid disk-image mount and clean unmount.
2. Invalid signature, truncated image and integer overflow inputs.
3. Directory enumeration, nested lookup, long names and expected case behavior.
4. Aligned and unaligned reads, EOF and sparse data.
5. Concurrent handles, share conflicts and cleanup after failed operations.
6. Page cache and mapped-file consistency once enabled.
7. Repeated mount/open/read/close with zero object/PFN leaks.
8. Block read failures, short reads, queue resets and interrupted I/O.
9. Corrupt inode, extent, directory and metadata-journal cases appropriate to format.
10. Write-enabled modes: flush, power-cut, recovery and independent filesystem checking.

### Status reporting

Document each capability as one of: **source present**, **object compiled**, **PE-linked**, **driver initialized**, **real image mounted**, **IRP operation exercised in QEMU**, **file-backed PE launched**, or **durability/recovery verified**. Never imply that one state proves the next.

Repository progress belongs in `docs/KERNEL_INTEGRATION_STATUS.md`; milestone acceptance belongs in `docs/KERNEL_MILESTONES.md`. This specification defines requirements rather than asserting those milestones have already passed.

## 13. Work allocation and interaction with M4, M6–M7 and M12

- **Immediate kernel dependency:** F0.1 → F0.2 → F1.1 → F1.2 → F1.3. This unblocks the missing `SEC_IMAGE` file-backed process creation path.
- **M4/M7 interaction:** keep process/thread ownership, exception dispatch and loader logic in Ps/Ke/Mm; filesystem code exposes an actual referenced file object and byte-range reads. Do not move process creation into ext4.
- **M6 syscall work:** integrate `NtCreateFile`, `NtReadFile`, `NtClose`, section creation and process launch through their canonical dispatchers.
- **M12 POSIX/VFS:** add a POSIX personality above the underlying Io/filesystem services; do not build another disk driver or file cache.
- **Later storage initiatives:** ext4 writes with JBD2, NTFS, exFAT and advanced filesystems should proceed after the first real disk-based native process launch.

Recommended implementation ownership: Storage/HAL owns block devices; Io/Ob owns handles and IRPs; FsRtl/extfs owns ext4; Mm owns mapped image pages; Ps/Ke owns process execution; Se owns access policy. Each batch should complete end-to-end through the owning modules rather than terminate after an isolated hosted test.

## 14. References to consult during implementation

- Microsoft Learn / WDK: `IRP_MJ_CREATE`, `IRP_MJ_READ`, `IRP_MJ_WRITE`, `IRP_MJ_CLEANUP`, `IRP_MJ_CLOSE`, `IRP_MJ_FLUSH_BUFFERS`, `FILE_OBJECT`, `NtCreateFile`, `NtReadFile`, `ZwCreateSection`, `SEC_IMAGE` and image section rules.
- Microsoft PE/COFF format specification (PE32+ image headers, section layout and protections).
- Linux kernel documentation: ext4 disk layout, extents and JBD2 journal format; exFAT on-disk specification and FAT32 specification.
- NTFS publicly documented on-disk structures and compatibility tests; Windows-generated test images.
- Existing local source: `kernel/fsrtl/extfs.cpp`, `kernel/fsrtl/ntfs.cpp`, `kernel/fsrtl/exfat.cpp`, `kernel/fsrtl/refs.cpp`, `kernel/fsrtl/zfs.cpp`, `kernel/io/iomgr.cpp`, `kernel/mm/virtual.cpp`, `kernel/ps/image.cpp`, relevant `include/micant/km/` headers, CMake targets and QEMU tooling.
- ReactOS sources: I/O manager, file objects, Cache Manager, memory-mapped image sections, filesystem drivers and their tests. Use as architectural reference under project licensing policy.

> **Final success criterion:** MicaNT's AMD64 kernel reads an actual PE32+ file from a persistent ext4-backed QEMU disk through canonical block Io and filesystem IRPs, creates a correctly referenced `SEC_IMAGE` section, executes the file as a Ps-managed Ring 3 process, and reclaims all file, section, VAD, handle and PFN resources at exit. Subsequent filesystem write support must additionally prove crash-consistent persistence.

## 15. Durability, cache coherence and device failure (M14 extension)

This section extends the existing single M14 filesystem pipeline; it does not create a second cache or filesystem architecture. `Cc` owns cached file data, each filesystem driver owns on-disk metadata rules, `Io` owns IRP ordering/completion and block-device error propagation, and the storage/HAL driver owns controller commands. `Mm` and `Cc` must share file-backed page identity so mapped writes, cached reads, paging I/O and ordinary file I/O cannot observe unrelated copies.

### 15.1 Flush and persistence contract

- Define dirty state at page, file, filesystem transaction and device queue levels. A successful flush must drain all earlier writes covered by its contract and propagate the first failure; clearing a dirty bit or completing an IRP before device completion is not durability.
- Preserve ordering through filesystem → Cc → Io → block driver. Use device cache flush commands and FUA/barrier only when the device/controller advertises and implements them. If ordering/durability cannot be guaranteed, expose the limitation and fail a requested durable operation rather than synthesize success.
- FAT32 has no journal. A completed write/flush can only claim the guarantees supported by the device's cache and ordered sector writes; it cannot promise atomic metadata updates or journal recovery. ext4 writable mode remains gated on JBD2 transaction/replay behavior described in F2.
- On writeback failure, retain dirty/error state, report the failing file/device status, prevent unsafe eviction and allow retry or explicit unmount failure. Never convert a removed device or timeout into a successful short write.

### 15.2 Coherence, locking and lifecycle

Use per-file/section range synchronization for write, rename, truncate, paging and memory-mapped modification. Define lock order between Cc, filesystem inode/metadata locks, Mm VAD/page locks and Io request queues. Paging I/O cannot recursively enter a cache path that waits on the same page. Rename is atomic only on filesystems whose metadata operation and recovery protocol provide that guarantee; document per-volume behavior. Oplocks/leases are not implied; add them only with the corresponding public contract and break/timeout lifecycle.

Unmount transitions mounted → quiescing → drained → unmounted; reject new opens, flush dirty state, cancel/complete in-flight IRPs and preserve referenced sections until their backing object lifetime ends. Surprise removal stops new requests immediately, fails outstanding operations accurately, blocks further MMIO/DMA, and retains pinned pages until device quiescence. Corrupt volumes are mounted read-only or rejected according to a filesystem-specific policy; do not silently repair metadata. Storage timeout recovery/reset is owned by the block driver and must complete each request exactly once.

### 15.3 Acceptance and fault injection

Add power-loss fault injection at block write, cache flush, FUA/barrier, journal commit and metadata update boundaries. Reboot a disposable image and verify file contents and metadata using an independent checker where appropriate. Exercise flush concurrent with mapped writes, page eviction, rename, cancellation, storage timeout and surprise removal. Verify cached and mapped views agree, errors reach callers, mount/unmount drains references, and no dirty data is discarded on shutdown. Test each filesystem separately; never infer journaling from a successful FAT32 remount. QEMU persistence tests require a real backing image, while physical claims require a documented controller/device cache profile and abrupt-power test setup.

Related roadmap dependencies: [M19 crash/debug](KERNEL_CRASH_DEBUG_RECOVERY_SPEC.md), [M21 ACPI/power](KERNEL_ACPI_POWER_MANAGEMENT_SPEC.md), [M22 DMA](KERNEL_IOMMU_DMA_PROTECTION_SPEC.md), [M24 paging](KERNEL_MEMORY_PRESSURE_PAGING_SPEC.md).
