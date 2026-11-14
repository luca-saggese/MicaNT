# MicaNT Kernel Crash, Debug and Recovery Specification

**Status:** Proposed high-priority reliability work; no crash dump or debugger capability is implied. **Owner:** Ke bugcheck/traps and DPC-safe crash coordination; HAL serial/CPU capture; Mm page-table metadata; Io/storage only in a separately safe dump path; build tooling owns symbols.

## Objective and baseline

On unrecoverable kernel state, stop safely, preserve bounded evidence and make offline diagnosis possible. Recovery means booting diagnostics or restoring a known configuration, not continuing after arbitrary memory corruption. Current architecture records diagnostic traps and serial output in bootstrap; these do not constitute a central bugcheck, unwind, dump or recovery path.

## Fatal path design

Route fatal exceptions and explicit bugcheck requests to a non-returning Ke path with a stable internal reason code, parameters, CPU ID and captured trap frame. Capture general registers, RIP/RSP/RFLAGS, CR2 for page faults, relevant control registers, segment selectors, error/vector, IRQL and current thread/process identity only when safely readable. Do not fabricate Windows bugcheck codes; if exposing a documented code, use its documented contract, otherwise use an explicitly MicaNT-private reason namespace.

Preallocate per-CPU crash records and a fixed circular log at boot. Fatal path uses no general allocator, filesystem, scheduler, driver callback, pageable code or lock that may be held. One CPU becomes dump coordinator via atomic claim; others enter a minimal stop rendezvous with timeout and write their already-reserved context slots. On timeout mark context missing. Mask/stop interrupt sources only as safe and avoid waiting for a nonresponsive processor indefinitely. Nested fault/double fault uses dedicated emergency stack/IST where supported and terminates without recursive logging.

AMD64 unwinding uses validated PE x64 `.pdata`/`.xdata` unwind metadata and bounded stack ranges; leaf frames and incomplete metadata are labeled, not guessed. Symbolization is offline from exact build ID, image base, PDB/DWARF-equivalent symbol artifact and linker map. Kernel build output must record hashes/build identity so RIP-to-symbol resolution is reproducible. Remote debugger transport is optional and disabled by default; serial debugger protocol must be bounded, authenticated if interactive, and never expose memory reads to unauthenticated input.

## Dump format, persistence and recovery

Define versioned header, architecture, build ID, reason, checksums, record lengths, physical/virtual ranges and truncation flags. Minimal dump contains CPU contexts, trap frame, bounded stacks, bugcheck metadata and circular log. Kernel memory dump is a later extension with page selection and integrity checks. Use a preallocated reserved region or dedicated raw dump path with exclusive ownership; do not depend on ordinary filesystem/driver code after bugcheck. A crash-dump storage backend is enabled only after its block device can write in polling mode with interrupts masked and no dynamic allocation; otherwise serial/firmware-independent memory handoff is fallback.

Crash dumps may expose credentials, keys and user data. Restrict write destination/access, support explicit local opt-in and retention controls, avoid network upload, and provide secure erase policy where the medium permits. Recovery mode is a bootloader/kernel diagnostic choice with bounded serial, memory and storage checks. Last-known-good configuration requires versioned, transactional configuration metadata and is not assumed present. Repeated boot failure detection must be local and must not discard user data.

## Phases and acceptance

| Phase | Gate |
|---|---|
| C0 | Central nonreturning bugcheck, preallocated records/log, nested-fault containment. |
| C1 | BSP trap capture and offline symbol artifact/build-ID agreement. |
| C2 | Bounded AMD64 unwind and minimal integrity-checked dump to serial/reserved storage. |
| C3 | SMP stop rendezvous and per-CPU contexts, timeout behavior. |
| C4 | Raw/persistent storage backend and dump verification; kernel dump as optional extension. |
| C5 | Recovery boot path, boot-failure policy, optional debugger transport/watchdog. |

Inject controlled kernel page fault, invalid opcode, stack overflow and diagnostic deadlock; test double fault only where hardware setup makes safe containment possible. Exercise crash during I/O and active DMA, SMP crash with one stalled CPU, complete/truncated dump checksums, and exact RIP symbolization. QEMU acceptance compares generated dump metadata and symbols against the exact image and verifies the system halts/reboots only according to explicit policy. Hardware acceptance captures UART output and persistent dump on supported real storage, including device failure fallback. Hosted tests validate encoders/unwind parser only. Fault injection must never corrupt production volumes; use disposable images/devices.

## References

- [Microsoft kernel-mode dump files](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/kernel-mode-dump-files), [bug check reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-code-reference2), [PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
- [AMD64 Architecture Programmer’s Manual](https://www.amd.com/en/search/documentation/hub.html); MicaNT [architecture](KERNEL_ARCHITECTURE.md), [milestones](KERNEL_MILESTONES.md).
