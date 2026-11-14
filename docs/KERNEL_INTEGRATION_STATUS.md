# Modular Kernel Integration Status

This is the integration progress record required by
[ADR-0001](adr/0001-single-modular-kernel.md) and
[KERNEL_ARCHITECTURE.md](KERNEL_ARCHITECTURE.md). It does not replace either
specification. The compatibility inventory is
[FREESTANDING_GAP_MATRIX.md](FREESTANDING_GAP_MATRIX.md).

## Verified baseline

- One active modular AMD64 Native PE kernel, no external PE imports.
- Versioned UEFI handoff; separately loaded VirtIO-GPU 2D driver.
- Supervisor-owned page tables, GDT/TSS/IDT, checked CPL3 entry/return,
  supervisor/RO/NX protection probes.
- Hardware syscall entry with a kernel stack, register/SIMD preservation,
  bounded user-buffer checks and actual user-originated VirtIO presentation.
- Isolated CPL3 TEB/GS probe: user code writes and reads
  `TEB64.LastErrorValue` at GS offset `0x68` and `TlsSlots[0]` at `0x1480`;
  compile-time layout assertions and the boot harness verify both accesses
  after a checked ring transition. The diagnostic TEB is mapped in two user,
  writable, NX pages; this does not initialize a process's TLS allocation map
  or static TLS template.
- Polled PS/2 cursor movement and click delivery; QMP pixel/input checks pass.
- Original bounded PE diagnostic mapped by the canonical native loader and
  entered at CPL3. This is not a process, DLL startup, or Explorer.
- A fixed-size Win32k request is marshalled across the syscall boundary and
  exercised from CPL3 for class registration, window creation, client-rectangle
  query and destruction. The request service and its bounded freestanding
  manager are verified; this does not establish Explorer or GDI compatibility.
- Canonical PFN database initialized from validated firmware conventional
  ranges; actual physical writes, reference retention, complete page zeroing
  and contiguous release verified before display initialization.

Reproduced from the current sources, not by accepting an old kernel binary:

| Command | Result |
|---|---|
| `python3 qemu.py --build-only` | Passed; kernel, bootloader, driver and manifest generated/validated |
| `python3 qemu.py --headless` | Passed; ordered serial milestones, stable idle, PS/2 and scanout checks |
| `cmake --build build/qemu-cmake-mingw --target ntoskrnl_core` | Passed; canonical core builds the kernel-owned Win32k dispatcher and WDDM adapter |
| `cmake --build build/qemu-cmake-mingw --target micant_tests -j 4` | Test runner compiles, but link fails on duplicate `micant::ucrt::initializeOnExitTable`; tests did not run |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Iinclude test/uefi_handoff_contract_test.cpp -o build/uefi-handoff-contract-test && build/uefi-handoff-contract-test` | Passed; validates V3 user-image file range serialization |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Iinclude test/ke_context_hosted_test.cpp -o build/ke-context-hosted-test && build/ke-context-hosted-test` | Passed; hosted Ke behavior remains intact |
| `cmake --build build/qemu-cmake-mingw --target explorer_runtime_boot` | Rejected by the new boundary check: `win32k_boot` transitively includes private `micant/km/` headers |
| `c++ -std=c++23 -I include test/win32k_freestanding_test.cpp kernel/win32k/freestanding.cpp -o /tmp/micant_win32k_test && /tmp/micant_win32k_test` | Passed; standalone Win32k manager lifecycle and message test |
| `cmake -S . -B build/host-check -G Ninja && cmake --build build/host-check --target micant_tests -j 4` | Failed before test execution on existing host-build errors in `ntdll.hpp` and inline assembly |
| `python3 -m unittest discover -s test -p test_explorer_runtime.py -v` | 12 passed |
| `python3 scripts/verify_kernel_architecture.py` | Passed |

### User-mode Win32k boundary increment

The freestanding Win32k request service is now called by the `win32k.dll`
client through the fixed-size syscall request ABI. The client no longer links
the hosted `TitanWin32kSubsystem`, scheduler, Executive or private kernel
headers. Class procedures are retained by the kernel window manager but called
from user mode by the client. Unsupported GDI exports fail rather than
presenting a simulated success.

| Command | Result |
|---|---|
| `python3 scripts/check_user_mode_boundary.py --root . --compiler build/llvm-mingw-20260616-ucrt-macos-universal/bin/x86_64-w64-mingw32-clang++ --target win32k_boot` | Passed |
| `cmake --build build/qemu-cmake-mingw --config Release --target win32k_boot` | Passed |
| `python3 qemu.py --check-explorer` | Boundary checks and PE linking passed; runtime import-closure validation then failed on unresolved Kernel32/UCRT exports. Explorer is not yet runnable |
| `python3 qemu.py --headless` | Passed; modular boot, scanout and PS/2 movement/click validation |

### Explorer software GDI increment

The user-mode Win32k client now provides bounded DIB/compatible-bitmap
surfaces, compatible memory DCs, bitmap selection, clipped `SRCCOPY` BitBlt,
and presentation through the kernel's validated user-buffer service. Pixel
storage is fixed-capacity; unsupported stock objects, other raster operations,
and invalid handles fail explicitly. This is software composition/presentation,
not GPU-accelerated composition, a full GDI implementation, or proof that
Explorer has executed as a process.

| Command | Result |
|---|---|
| `cmake --build build/qemu-cmake-mingw --config Release --target explorer_runtime_boot` | Passed; Win32k/User32/GDI32/IMM32 and Explorer PE linked, and all user/kernel boundary checks passed |
| `python3 qemu.py --headless` | Passed before the software GDI addition; QEMU does not yet execute the generated Explorer PE |

The runner invokes the affected build before staging and validates the exact
kernel/bootloader/driver artifact set. An up-to-date Ninja result is not a
substitute for those checks. Host tests and PE linking do not establish boot
integration.

## Integration increments

### Native MBR/GPT discovery

The canonical storage parser is shared by the hosted `PartitionManager`, the
AHCI provider's FAT32 probe, and FsRtl's real-disk mount path. It validates MBR
primary and bounded EBR logical partitions, GPT header/entry-array CRC32, backup
GPT recovery, non-overlapping extents, arithmetic and device LBA bounds. The
parser uses caller-owned fixed scratch buffers and has no hosted runtime or
third-party dependency. FsRtl mounts FAT32 partitions by type while preserving
the existing FAT32 superfloppy path; AHCI now selects an eligible disk without
assuming LBA 0 is itself a FAT boot sector.

| Command | Result |
|---|---|
| AMD64 freestanding `ntoskrnl_core_baremetal` and `storahci.sys` build | Passed |
| `python3 qemu.py --headless` | Passed using a GPT-partitioned FAT32 AHCI disk; boot self-test covered MBR/EBR loop rejection, GPT CRCs, backup recovery, corrupt extents, arithmetic overflow and LBA bounds; FsRtl mounted the real GPT partition and file-backed `SEC_IMAGE` passed |
| Standalone `test/partition_manager_contract_test.cpp` host build/run | Passed |
| `cmake --build build/host-check --target micant_tests -j 8` | Did not build due existing unrelated `iomgr.cpp` errors (`resolveHandle` missing and `FileObject*` type mismatch); this feature's standalone host contract passed |

UEFI GPT layout and checksum requirements follow the public [UEFI 2.10 GPT
format](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html).
No SerenityOS source was incorporated.

### Kernel completion sprint milestones

These milestones use the numbering in the kernel completion sprint, which
differs from the historical integration inventory below.

| Milestone | Current evidence and remaining work |
|---|---|
| M1: section mapping | Anonymous section mapping through Mm and Ob exercised in QEMU, including shared pages and cleanup. |
| M2: independent address spaces | Bounded BSP acceptance path exercised through Ps, Ke, Mm and Ob in QEMU, including private pages, shared section, survivor and PFN rollback. General process creation and termination remain outstanding. |
| M3: preemptive scheduler | Bootstrap acceptance criteria completed on the single BSP in QEMU: timer-preempted kernel threads without voluntary yield, block/wakeup and bounded cleanup. General lifecycle and APC rundown remain M4 work; this is not a claim of full NT scheduler compatibility. |
| M4: process and thread lifecycle | QEMU exercises bounded Ps/Ob thread and process lifecycle, APC rundown, termination, waits, deferred reaping, native `NtOpenProcess`/`NtOpenThread` CID opens with granted-right checks, `NtQueryInformationThread(ThreadBasicInformation)`, UserMode process DACL allow/deny in the target process context, centralized Ob valid-mask enforcement, generic and `MAXIMUM_ALLOWED` reference/duplicate rights, duplicate-handle open callbacks and failed-duplicate rollback, `OBJ_PROTECT_CLOSE` through `NtClose`/`ObCloseHandle` and forced teardown, open-callback rollback, transactional handle inheritance callbacks, Se DACL ACE ordering, nested Ke cross-process attach, denied address-space teardown while an attach reference is live, and process lifetime after its last public handle closes. An 8-thread host stress test races handle reference against close and verifies balanced counts. A separate host contract test races named opens against directory unlink, covers fail-closed access checks, and verifies `ObInsertObject` publication/rollback. General native creation, complete security checks, SMP kernel synchronization, freestanding named-object namespace support, and broader lifecycle coverage remain explicitly unsupported. |
| M5: NT system calls | Section, close and bounded current-process allocation/free calls exercised from CPL3. The services needed for native process/thread creation, synchronization and termination are incomplete. |
| M6: first Ring 3 PE process | Partial. QEMU now exercises an object-backed PE process through CPL3 `NtCreateProcessEx` cloning, `NtQueryInformationProcess(ProcessBasicInformation)`, `NtOpenProcess` with restricted-right denial, `NtCreateThreadEx`/resume/wait, `NtQueryInformationThread(ThreadBasicInformation)`, `NtOpenThread` by ClientId and handle close. The child PE thread executes in Ring 3; its PEB is cloned and its TEB is initialized by thread creation. Process creation still uses eager address-space cloning rather than a file-backed image section. Timeout/APC coverage and general lifecycle/error-path coverage remain incomplete, so M6 is not complete. |
| M7: two threads, TLS and synchronization | Not complete. Two user threads in a real process, independent TLS, event synchronization and a verifiable exit status have not been exercised. |
| M8: POSIX, Linux-style VFS and `sh.exe` | Planned; no M8 integration verification recorded. Scope, dependencies, phases and acceptance criteria are defined in [KERNEL_MILESTONES.md](KERNEL_MILESTONES.md#m8--posix-vfs-linux-style-e-shexe) and [the POSIX/VFS specification](<POSIX, VFS Linux.md>). |

The detailed increments and commands below record the checks actually run;
an isolated diagnostic does not establish a complete milestone contract.

### File-backed M4 process/thread lifecycle increment

The file-backed `SEC_IMAGE` fixture now proceeds through an Ob-owned process,
starts the mapped PE entry point as a CPL3 thread, terminates through
`NtTerminateThread`, waits and closes the child thread handle in its owning
handle table, waits for process termination from the parent, and checks PFN
recovery after cleanup. The child handle-table ownership check caught and
corrected a test rollback path that could otherwise close a same-valued parent
handle. This verifies the exercised lifecycle slice; it does not launch
`explorer.exe` or close general M4 NT lifecycle contracts.

| Command | Result |
|---|---|
| `TMPDIR=/private/tmp python3 qemu.py --headless --timeout 120 --diagnostics-dir /private/tmp/micant-m4-fileimage-lifecycle-rollback` | Passed; modular bare-metal kernel linked, file-backed process/thread lifecycle, bootstrap two/three-thread tests, timer-preempted Ring 0/Ring 3, APC/DPC, reaper, PS/2 input and 5-second serial stability verified |

The current M3 implementation batch has been incrementally built and booted
in QEMU. Its LAPIC calibration uses five PIT measurements, takes the
median, rejects an unstable middle range, and corrects for the PIT interval's
rounding. The clock backend also accepts firmware-selected x2APIC and uses
MSRs for its local controller accesses. Ke adds a higher-priority dispatch
at selected safe IRQL, spinlock, event and syscall boundaries; callers which
mask interrupts outside these boundaries can still defer dispatch until a
clock tick. Earlier QEMU results below apply to the previous implementation;
do not establish independent clock accuracy. The new batch
also saves and initializes x87/SSE/AVX/AVX-512 state across thread switches and timer
interrupts, rejecting additional XCR0 components; it tracks the GS swap state
of a suspended syscall and discovers firmware interrupt topology from mapped
ACPI tables. ACPI discovery copies
firmware tables through a temporary physical mapping after checking their
ranges and write-back capability against allowed UEFI memory descriptors;
the absence of conflicting firmware cache aliases is not established. The IOAPIC backend discovers
hardware GSI ranges and prepares routes. A BSP PS/2 IRQ12 path installs a
gate, captures bytes into bounded queues, sends EOI and falls back to polling
when setup fails. CR8 maps selected device DIRQLs to APIC priority classes.
Failed IOAPIC discovery releases its temporary MMIO mappings in reverse order.
The BSP Ke DPC queue is now intrusive and allocation-free; pending callbacks
are serviced at safe lowering, lock, syscall and interrupt-return boundaries.
The documented [KeInsertQueueDpc](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keinsertqueuedpc)
and [APC types](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/types-of-apcs)
contracts informed this batch.
Kernel APC delivery for the current thread is similarly deferred to passive
boundaries, with nested critical and guarded regions. Cross-thread insertion,
user APC delivery and APC rundown are still incomplete. Deferred normal APC
callbacks use four per-thread slots; if all are occupied, further kernel
APCs remain queued until capacity becomes available.
Additional device routing, additional extended-state components and independent accuracy
verification are outstanding. The larger switch and interrupt frames require
resident stack headroom; the new guards do not replace guard pages or prove
that all kernel paths have enough stack. The boot thread now starts on a
dedicated resident stack with known bounds; the separate Ring-3 entry stack is
retained for bootstrap probes. Interrupt and switch headroom checks distinguish
the active bootstrap, syscall and user-entry stacks, including nested IRQs.
The QEMU run exercised the new boot stack; guard-page behavior is not implemented.

Current checks: `python3 qemu.py --headless` passed after fixing compilation
and linking errors; architecture verification and the seven bootstrap-runner
unit checks passed; the host ready-queue regression passed with ASan/UBSan.
A separate boot runner invocation observed ACPI discovery, IOAPIC range
mapping and PS/2 route activation, and passed input/scanout checks. The input
consumer also polls the controller, so these observations alone do not prove
interrupt delivery. x2APIC, AVX-512, pending runtime DPC/APC delivery and
clock accuracy have not been independently exercised.

After commit `46c0d21`, a further M3 batch adds CR8/APIC priority-transition
serialization and boot diagnostics for DPC/APC delivery and AVX/AVX-512
register preservation. PS/2 input now records whether a consumed byte came
from the ISR; the runner requires such a byte when the IRQ route is active,
while preserving polling as recovery for missed edges. The combined batch was incrementally built and passed `python3 qemu.py --headless`: the timer, two non-yielding workers, block/wakeup, queue withdrawal, bounded Ps cleanup, clock IRQL and runtime DPC/APC diagnostics all passed. The runner also passed its input and scanout checks; an ISR byte is required if the PS/2 route is active. The QEMU default CPU run does not establish AVX, AVX-512 or x2APIC coverage. The architecture check, seven bootstrap-runner tests, sanitized host ready-queue regression and undefined-symbol check passed. No physical hardware or independent timer accuracy validation was performed.
The Intel [system programming manual](https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf)
was consulted for x2APIC register access.

Ps process objects now use Ob handles and references: attached threads pin
their process, and the final terminated thread's reap closes the address space
even when an external handle remains. The bootstrap test covers a self-handle,
access and object-type checks, exit status, object counts and PFN recovery.
`NtTerminateProcess` for another process remains unimplemented; native process
creation and waitable process objects are also outstanding. The contract was
checked against [ZwTerminateProcess](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-zwterminateprocess).

The BSP clock vector is masked through CR8 when Ke raises IRQL to CLOCK_LEVEL;
HIGH_LEVEL masks all priority classes. The QEMU diagnostic checks nested
raising, preserved interrupt enable state, a pending clock interrupt and its
delivery after lowering IRQL. This covers the routed xAPIC clock only, not
general DIRQL, software interrupts, x2APIC or SMP. The contracts consulted were
[KeRaiseIrql](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keraiseirql),
[KeLowerIrql](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kelowerirql)
and [hardware priorities](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/managing-hardware-priorities).
ReactOS `hal/halx86/mp/mpsirql.c` was consulted as an architectural reference.

### Bootstrap local APIC timer diagnostic

On QEMU TCG, a temporary BSP-only xAPIC setup delivers at least three periodic
timer interrupts through an IST-free interrupt gate and acknowledges them
with the local APIC EOI register. The diagnostic masks other local sources and
the legacy PIC, drains any pending test interrupt, restores the saved controller
configuration and gates, and checks that IF remains disabled. The timer's
frequency is not calibrated and its prior countdown phase is not preserved.
During the bounded QEMU diagnostic, clock ticks now select from the canonical
Ke ready queue at quantum end and preempt two CPL0 kernel threads that never
yield. Both workers advance, their GPR and XMM canaries survive, and their
queued contexts are withdrawn before the timer is restored. The interrupt
entry saves GPRs and FXSAVE state and sends EOI before switching. A second
bounded test runs two process-owned CPL3 contexts with interrupts enabled:
both must advance shared counters before either issues a syscall, so the
second context can run only after timer preemption. It also checks GPR and
XMM canaries, separate TLS, event synchronization, process exit and cleanup.
After the diagnostic restores the controller, the bootstrap can install a
persistent BSP xAPIC scheduler clock. QEMU reruns the CPL3 preemption test
with this clock active and user IF preserved across syscalls, then verifies
ongoing ticks during the normal polling loop and PS/2 input. The LAPIC period
is measured against an approximately 10 ms PIT channel 2 interval at startup;
calibration requires the speaker gate to be inactive, restores its port state,
and leaves channel 2 programmed in mode 0. The legacy PIC and other local APIC
vectors remain masked, and x2APIC is unsupported; if setup is unavailable the kernel logs that the
clock is unavailable and keeps the previous interrupt-disabled loop. AVX and
other extended XSAVE state, full hardware IRQL, system timekeeping and general
interrupt routing are not implemented. Persistent preemption is rejected if
OSXSAVE is enabled with XCR0 components beyond x87/SSE, because the switch
currently saves only FXSAVE state. QEMU exercised the supported configuration;
the rejection path has not been exercised with extended state enabled.
PIT mode and gate behavior were checked against Intel's
[I/O platform timer documentation](https://edc.intel.com/content/www/th/th/design/ipla/software-development-platforms/servers/platforms/intel-pentium-silver-and-intel-celeron-processors-datasheet-volume-1-of-2/001/timer-programming/);
ReactOS `hal/halx86/generic/timer.c` was consulted for the PIT clock reference.
On each scheduler clock tick Ke now selects a higher-priority ready thread
without waiting for quantum expiration; equal-priority threads rotate at the
quantum boundary, and a lower-priority ready thread does not displace the
running thread. The queue test checks these three cases and rejected-switch
rollback in the QEMU boot and under host ASan/UBSan. Preemption on the tick
does not yet provide the immediate dispatch required when a higher-priority
thread becomes ready. See Microsoft's
[scheduling priorities](https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities);
ReactOS `ntoskrnl/ke/dpc.c` and `ntoskrnl/ke/time.c` were consulted for
quantum-end dispatch.
On the bare-metal BSP, Ke spinlocks now mask interrupts on acquisition and
restore the saved IF state on release. The same timer diagnostic verifies
that no tick is delivered while nested locks are held and that interrupts
resume after the outer lock is released. This all-interrupt masking is a
bootstrap serialization measure, not the complete NT IRQL contract.
The bootstrap dispatcher can also block the current kernel thread and wake it
through the canonical ready queue. QEMU exercises two waiting/resuming threads,
the pre-clock no-runnable-thread case and rollback when a selected context is invalid.
This is an internal primitive, not yet an NT event wait, timeout or APC path.
After the BSP scheduler clock starts, Ke can select a resident idle context
when no thread is ready. The idle context halts until an interrupt; QEMU
exercises a blocked thread, timer wake of another context, resumption and
cleanup of the original waiter. This is a single-CPU bootstrap idle path,
not SMP idle scheduling or power management. ReactOS
`ntoskrnl/ke/amd64/kiinit.c` and `ntoskrnl/ke/amd64/stubs.c` were consulted
for idle-thread ownership and loop structure.
The freestanding Ke synchronization implementation now has a BSP-only
internal event wait queue built on that primitive. QEMU checks a pair of
kernel contexts handing off through synchronization events, notification of
two simultaneous waiters, retained notification state, auto-reset state and
cleanup after a wait fails for lack of a runnable thread. Event storage must
outlive its waiters. The event now has a canonical Ob object and per-process
handle: an in-kernel waiter retains an object reference across a context
switch and another context closes a handle before signaling through a duplicate.
The CPL3 diagnostic calls the actual NTDLL `NtCreateEvent`, `NtSetEvent`,
`NtWaitForSingleObject` and `NtClose` exports, verifying zero-timeout polling,
auto-reset, previous state and invalid pointer/handle cases. Kernel tests also
check access rights and invalid event type. The bounded BSP path now supports
non-alertable, infinite event waits from process-owned CPL3 threads with their
own kernel stacks; timed and alertable waits, named events, public KEVENT ABI,
priority boost and SMP remain unimplemented.
The event semantics were checked against
[KeInitializeEvent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keinitializeevent)
and [KeSetEvent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesetevent).
Native service contracts are [ZwCreateEvent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwcreateevent),
[ZwSetEvent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwsetevent)
and [ZwWaitForSingleObject](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwwaitforsingleobject);
ReactOS `ntoskrnl/ke/eventobj.c` and `ntoskrnl/ke/wait.c` were consulted as
architectural references, without copying code.

On the BSP, each Ke context switch now selects the incoming thread's resident
kernel stack for syscall entry and updates `TSS.RSP0` after the TSS is active;
the bootstrap thread uses its original fallback stacks. The QEMU Ps thread
test checks the selected syscall stack and its restoration, while the timer
preemption test checks both stack values on each worker interrupt and after
return to bootstrap. No syscall was issued by those worker contexts: a real
user-thread transition, per-thread GS/TEB state and blocking syscall still
need integrated verification. ReactOS `ntoskrnl/ke/amd64/thrdini.c` was
consulted for the TSS stack update; the hardware reference is the Intel SDM
linked below.

Freestanding Ps now owns a bounded kernel-thread lifecycle: it allocates a
resident kernel stack and Ke context, attaches the thread to its process root,
queues and runs it, and reaps the terminated thread from another context before
releasing the stack and address space. QEMU exercises this path, rejects
premature cleanup and duplicate readying, and checks PFN accounting. This is
not yet an implementation of `NtCreateThreadEx` or general process termination.
The later bounded Ob thread factory and its verification are described above.
A bounded M4 batch lets the existing non-alertable BSP
`NtWaitForSingleObject` path wait on an Ob process handle with `SYNCHRONIZE`.
The retained object reference protects its notification event; the event is
signaled only after explicit reap of the last attached thread closes the
address space. QEMU exercised zero-timeout polling before and after reap and repeat waits, plus rejection of termination with a queued user APC or disabled APC delivery. A blocked waiter also resumed after reap despite its handle being closed during the wait; its retained Ob reference kept the object alive. That verified run used explicit reap; a later unverified deferred reaper is
described below. General process termination, alertable waits and timeouts remain incomplete. The
process signaling requirement is documented in [Terminating a Process](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process).

A bounded M4 batch adds user-APC rundown on return from a bootstrap Ps
kernel-thread start routine. It runs callbacks on the terminating thread at
PASSIVE_LEVEL, disables further APC insertion before invoking them, and does
not access an APC after its callback, which may destroy it. The initial user
rundown test passed in QEMU; an expanded QEMU test also delivered queued normal
and special kernel APCs before user rundown. The QEMU-tested version required every queued user APC to provide a rundown
routine and rejected APC-disable regions or residual kernel APCs. A later
unverified change removes a borrowed APC with no callback without freeing its
storage; its owner remains responsible for that storage. Process-termination
and never-started-thread rundown remain unimplemented. QEMU also exercised rejection of reentrant rundown and a callback that frees its own APC. ReactOS `ntoskrnl/ps/kill.c` was consulted for
its current-thread rundown and kernel-queue exit handling; Microsoft documents
[APC types](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/types-of-apcs)
and [waits and termination](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/waits-and-apcs).

The later Ring 3 APC continuation test exposed `NtContinue` rejecting the x87
default control word (`0x037f`) because its accepted-bit mask omitted bit 6;
the mask now accepts that architecturally valid value. APC continuation
contexts also use flat user DS/ES selectors instead of the kernel selectors
active at syscall return. Commit `cf1f4d9` fixes both and QEMU now advances past
the prior `STATUS_INVALID_PARAMETER`/`#UD`. The expanded callback test then
exposed a fixture bug: its user thread used RCX as the output pointer after
`SYSCALL`, which replaces RCX with the return RIP. Saving the argument in R12
fixed the test, and `python3 qemu.py --headless` passed with the serial console
stable for five seconds. Normal user-APC dispatch is now exercised by this
bounded path; its general interleavings and M4's broader access/lifecycle
contracts remain open.

The same CPL3 fixture queues two distinct user APCs to the suspended child and
requires both callbacks in FIFO order before the thread returns
`STATUS_USER_APC`. The complete QEMU boot passed with this case. A separate
termination test covers rundown of a queued user APC on a remotely terminated
event waiter. A new CPL3 fixture queues an invalid callback followed by a
sentinel that terminates with a distinct status. The child process exits with
`STATUS_ACCESS_VIOLATION`, and the parent waits for and closes the thread and
process; this confirms the later APC was not dispatched. Commit `6e28db0`
suppresses APC injection during both chances of `NtRaiseException` while
preserving explicit `NtTestAlert` delivery. `python3 qemu.py --headless` passed
with the fixture, stable for five seconds. Broader callback rundown and
interleaving cases remain open.

The Ring 3 process-creation fixture also passes named `ObjectAttributes` to
`NtCreateProcessEx` and `NtCreateThreadEx`. Both reject the unsupported
freestanding Ps namespace with `STATUS_NOT_IMPLEMENTED` and leave the output
handle unpublished. The full `python3 qemu.py --headless` run passed after
adding these cases.

The same Ring 3 fixture now verifies that `NtCreateProcessEx` returns
`STATUS_NOT_IMPLEMENTED` and leaves its output handle null for an unknown flag,
non-null debug port, non-null exception port and `InJob`. These are explicit
unsupported-input checks; they do not establish general process creation or
security-descriptor/metadata semantics. It also passes a query-only process
handle as the parent and verifies `STATUS_ACCESS_DENIED` with no child handle.
QEMU passed with all five cases.

The kernel-side native thread fixture passes a commit size larger than its
reserve and a reserve above `BootstrapManagedSize` through `NtCreateThreadEx`.
Both return `STATUS_INVALID_PARAMETER`; the test confirms null output handles,
unchanged Ps thread-object count and unchanged ready-queue count. The Ring 3
process fixture now repeats both negative cases through the NTDLL syscall
trampoline and checks the status and null handle. QEMU passed with both layers
of coverage. This verifies stack-bound validation and rollback before thread
allocation, not later allocation/copyout rollback.

A bounded internal Ps factory now creates Ob-backed thread objects. A lifecycle
reference keeps each object alive if its last handle closes before reap. The
Ke switch commit signals thread termination before the outgoing stack is
reaped; cancellation before start signals it as well. QEMU exercised polling,
a blocked waiter whose handle was closed, wakeup before reap, repeat wait,
and closing the last handle before cancellation/reap. Native thread creation,
automatic reaping and general remote termination remain outstanding. The
[thread termination contract](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-thread)
requires signaling and object lifetime independent of open handles.

The current batch assigns distinct nonzero process and
thread IDs to bootstrap Ps objects and populates user TEB IDs. It registers
resident Ps thread contexts for BSP APC insertion, rejects insertion after a
termination request, and adds an event-wait interruption path for a remote
kernel APC. The wait resumes after kernel APC delivery and rechecks a signal
that arrived while the waiter was detached. Tests cover cross-thread insertion and interrupt/resume. The
remote event-wait termination path is also extended to deliver pending kernel
APCs and run user rundown in the target thread after releasing the wait's Ob
reference. Cancellation before
start now schedules a target only for APC cleanup when it has queued APCs;
its original entry is skipped and reap remains unavailable until that cleanup
finishes. The batch further adds
an Ob-owned deferred reaper, last-thread process termination, passive-boundary
termination requests for ready or waiting threads, a current-thread exit path
that delivers kernel APCs and rundowns user APCs, and a private composition
helper that loads supplied images and constructs a user thread using existing
Ps/Mm/Ob/Ke operations. The helper is not the native `NtCreateUserProcess` or `NtCreateThreadEx` ABI; their
Microsoft declarations and full startup semantics remain unresolved. A further
batch adds the documented WDK `PsCreateSystemThread` and
`PsTerminateSystemThread` entry points, a persistent system process, kernel-only
handle tagging, syscall previous-mode tracking and a lifecycle test. It uses
existing Ps/Ob/Ke ownership and starts the kernel routine in a critical region.
Object names, security descriptors, general access checks and the full driver
loading path remain unsupported. The contract consulted is
[PsCreateSystemThread](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-pscreatesystemthread). The
first QEMU run after adding client IDs was interrupted at the user's request;
it does not establish that the ID or later code works.

The batch also moves deferred termination to the actual user-return
boundary, serializes thread publication, cancellation and reap against APC
insertion, and captures bootstrap process image inputs before changing CR3.
With the scheduler clock active, zero-reference process objects are queued
for deletion by the passive reaper; early bootstrap still uses synchronous
deletion. ReactOS
`sdk/include/ndk/psfuncs.h`, `ntoskrnl/ps/thread.c` and
`ntoskrnl/ps/process.c` were consulted where Microsoft metadata does not
provide the native creation contracts. Native creation remains incomplete:
the canonical thread path needs full AMD64 context, caller-owned stack and
suspension semantics, while process creation needs executable file-backed
sections or address-space cloning and the corresponding access checks.

The subsequent integrated validation built and linked the bare-metal kernel
and passed `python3 qemu.py --headless`, including the lifecycle, APC,
deferred reaper, user-thread, preemption, input and idle markers. The separate
`python3 qemu.py --headless --runtime-selftest` passed runtime teardown. The
seven bootstrap runner unit tests, architecture check, hosted Ke context and
canonical Ob handle regressions also passed. These results cover the bounded paths exercised by
those tests; they do not complete M4 or validate general native creation,
SMP, arbitrary access checks or all APC interleavings.

A subsequent unverified increment stores the requested exit status per Ps
thread and adds an internal single-BSP request path for an unstarted thread,
a ready user thread, or a user-mode event wait that can be cancelled. Target
context performs APC rundown and exit at startup or at the user-return
boundary. Started kernel threads and other wait types remain unsupported.
This private mechanism is not wired to a native thread-termination API and
does not establish its Windows contract. The bare-metal kernel and its
lifecycle test translation unit compile and link; the new test has not been
run in QEMU.
The cross-toolchain `micant_tests` target compiled its test runner. Its first
link exposed a duplicate UCRT `initializeOnExitTable` definition, corrected
by making the header definition inline. A subsequent link passed that point
but still failed on three unresolved User32/GDI32/IMM32 coordinator and
Executive initialization symbols. The general suite was therefore not run;
the cross-toolchain `ntoskrnl_core` target itself linked successfully. The
focused UCRT on-exit test and a two-translation-unit link/run check passed.

An M4 increment adds bounded `NtTerminateThread`, `NtResumeThread`,
`NtCreateThreadEx` and `NtCreateProcessEx` service routes and matching NTDLL
exports. Thread creation resolves `RtlUserThreadStart` in the target process,
captures its routine and parameter, reserves and commits a bounded stack,
and can leave the initial thread suspended until resume. The client ID and TEB
output attributes are captured and copied back through the syscall boundary.
The distinct null-handle termination cases follow the documented last-thread
and peer-thread rules; named process and thread handles require their terminate
access rights.
Process creation supports an empty address space with a system-process parent,
eager cloning of private and loaded-image pages, independently captured loader
metadata, shared `ViewShare` sections and inheritable Ob handles at their
original values. Cloned image
pages are privately owned rather than backed by a full file image section;
file-backed image sections, security attributes and other creation flags are
not implemented. Cloning currently holds single-BSP interrupt exclusion while
copying the address space; it is not an SMP or low-latency snapshot mechanism.
The exposed thread-creation path currently rejects a stack
whose initial commit is smaller than its reservation, and rejects an invalid
start address before execution because user exception dispatch is absent.
Thread stack growth and guard-page fault
delivery, other creation attributes, broader suspension and complete AMD64 context
creation also remain outstanding. The normal QEMU run exercises the linked
bootstrap tests for access checks, initial suspension, system-parent process
creation, native thread execution, cloned image lifetime and deferred cleanup;
this does not establish the unsupported general contracts. The current increment
built and linked; the hosted Ob handle
and sanitized Mm backend regressions passed. A hosted Mm clone test exercises
sparse pages, protections, independent writes, image-page ownership, shared-view
lifetime, reentrant section-owner release and allocation-failure cleanup with
a simulated paging backend. The sanitized PE regression also checks that
cloned loader metadata adopts existing image bytes without rewriting them.
The Mm backend also has a private per-page guard transition and can release a
guarded PTE; a sanitized hosted backend test covers its state changes and
failure handling. Private address-space cloning preserves that guard state;
the hosted clone test checks both copying and rollback after a target guard
failure. A bounded CPL3 guard-fault path now saves and restores GPR and
extended state and advances the current thread's guard on the BSP; it does
not implement user exception dispatch or enable partially committed native
thread stacks. Process
termination now schedules never-started threads before cancelling event waits,
so a failed scheduling preflight cannot reorder the existing event waiters.
After APC rundown has become irreversible, a missing successor or failed
context switch is treated as a kernel invariant failure; early bootstrap
still cannot guarantee a successor if callbacks consume the only ready thread.
The scheduler idle thread uses the permanent kernel paging root, and exit
preflight checks that an idle candidate is actually selectable.
Failed native process creation withdraws its unpublished handle before
leaving exclusion and finishes close callbacks afterward. For failures after
creation of an unpublished thread handle, the thread becomes terminal and
nonqueueable under exclusion before its reap and close run outside it. The user-mode
process-handle and resume-count probes also preserve alignment failures.
A CPL3 fixture exercises those alignment failures during normal QEMU boot.
`python3 qemu.py --headless` passed the modular boot, timer preemption,
reaper, input and five-second stability checks; the separate runtime-selftest
mode passed the runtime teardown path. The latter
halts before the Ps/Ke process diagnostics. Neither result completes M4.

The subsequent image-section integration resolves a non-null process section
handle using the canonical Ob section type and `SECTION_MAP_EXECUTE` before
returning `STATUS_NOT_IMPLEMENTED` for the still-missing file-backed image
path. QEMU exercises invalid handles, wrong object types and denied access,
as well as the unsupported valid-section case. The freestanding kernel has
no canonical file object/read backend or file-backed `SEC_IMAGE` yet; the
private bootstrap image loader is not a substitute for those M6 dependencies.
Internal partially committed thread stacks now create a non-present guard PTE
immediately below the committed range and retain its address in the kernel.
QEMU exercises creation, accessibility checks, two actual CPL3 guard faults,
register preservation and cleanup. The hook runs with interrupts disabled on
the shared bootstrap IST and must not wait or switch contexts. Native thread
creation still rejects partial commitment until stack-overflow exception
delivery and stack-guarantee semantics are integrated.

The current unverified M4 work adds the AMD64 TEB deallocation-stack and
guaranteed-stack fields and initializes the former from the kernel-owned stack
reservation. It also records a failed pre-clock synchronous process cleanup
so a later termination request can retry remaining resources, preserving the
original exit status. Ob-backed processes also retain a reaper-queue reference
until the runtime clock can service cleanup, or until a synchronous retry
withdraws that reference. These changes have not been built or exercised in
this work batch.

Continuation: M4 is still partial. The next dependency for general stack
overflow is a thread-owned AMD64 exception frame and user-mode exception
dispatch/continuation; the current shared IST guard hook cannot wait or
switch contexts, and its overflow branch intentionally remains unsupported.
File-backed process creation separately needs the canonical freestanding Io
file object/read path and Mm `SEC_IMAGE` ownership. Existing local changes
to `ps.hpp`, `usermode.hpp`, Ps, Mm, HAL and the Ps fixtures must be kept.
No build, tests, or QEMU were run after the TEB and cleanup-retry changes.

Subsequent M4 work links the AMD64 exception frame producer to the NTDLL
dispatcher and `NtContinue`, and adds an Ob file object, an AHCI/FAT32 read
backend and a PE32+ `SEC_IMAGE` path through Mm and Ps. These paths are not yet
verified end-to-end: the QEMU boot configuration reports that the file provider
is unavailable, and the boot fixtures do not exercise user exception delivery
or a file-backed process. FAT volume discovery still uses fixed partition offsets.
Unsupported cases must therefore not be treated as general NT support.

The modular bare-metal kernel, hosted `ntoskrnl_core`, NTDLL and Kernel32 boot
targets linked after correcting hosted build regressions. The architecture and
native DLL header-boundary checks passed. A direct QEMU boot with input checks
passed the existing lifecycle, two- and three-thread, timer, reaper and runtime
diagnostics. Its enlarged stack fixture exercised five successive guard faults.
Private image-view pages are now freed individually during unmap; the existing
PFN baseline checks pass after process cloning and teardown. These results do
not establish exception delivery, stack-overflow handling or file-backed image
startup. No physical hardware verification was performed.
The seven bootstrap-runner Python tests also passed. A separate native macOS
`micant_tests` build was attempted on the ARM64 host and failed on existing
AMD64 inline assembly and Microsoft-specific integer syntax; it was not run.
The stale pre-existing host CMake cache points to a different checkout, so the
attempt used an isolated temporary build directory.

The freestanding `KThread` now keeps resident APC objects in intrusive FIFO
queues rather than constructing allocating `std::deque`s. QEMU verifies FIFO
delivery for the current kernel thread and duplicate insertion rejection; Ps
refuses to reap a thread with queued APCs until rundown is implemented. Full target-thread lifetime/lookup semantics, general rundown and alertable user waits
remain incomplete. ReactOS `ntoskrnl/ke/apc.c` was consulted for queue and
rundown context.
Ps can also roll back a not-yet-started thread, withdrawing it from the Ke
ready queue before it is reaped. QEMU checks cancellation both before and
after queueing, rejection after selection or while running, and a clean PFN
baseline. This BSP-only internal operation is not arbitrary thread termination.
The local ReactOS `ntoskrnl/ps/kill.c` was inspected for thread termination
and deferred cleanup; the public self-termination contract is described by
[PsTerminateSystemThread](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-psterminatesystemthread).
The freestanding `NtTerminateProcess` implementation accepts the current
process pseudo-handle for a process with one active thread. It
records the exit status, marks the thread terminated and switches through Ke;
another context reaps the stack and closes the address space. QEMU checks a
no-runnable-thread failure without changing process state, rejects the
unsupported multi-thread cases, verifies the exit status and restores the PFN baseline
after releasing a private allocation. A bounded multi-thread case also permits
termination when the other threads are blocked on events or ready after an
event wait: blocked waits are removed from the event and readied, and each
thread resumes in the kernel, releases its retained event reference and
terminates before returning to user mode. QEMU verifies this with three CPL3
threads blocked on an unsignaled event, the event and section Ob object counts,
and PFN cleanup. The Ke test also cancels and restores a wait before signaling
it. Other thread states, APCs and general multi-thread termination remain unsupported;
the later bounded remote-process path is described below. The contract is
[ZwTerminateProcess](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntddk/nf-ntddk-zwterminateprocess);
ReactOS `ntoskrnl/ps/kill.c` was consulted for termination and cleanup.

The bounded `NtTerminateProcess` path also accepts a handle to a different
process with PROCESS_TERMINATE access when it has no attached threads, or when
all active threads are in the supported bootstrap event-wait path. In the
latter case their waits are cancelled and each thread resumes in its own
kernel context to release the retained object reference and terminate. QEMU
exercised both the empty process and a blocked thread, exit status, signaling,
handle closure and PFN recovery. Arbitrary ready/running threads, APC rundown
on this path, asynchronous reaping and other wait types remain unsupported.
The access and termination contract is [ZwTerminateProcess](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-zwterminateprocess).

An additional QEMU test now creates a real Ps process and Ke thread with a
private CR3, PFN-backed user stack and executable page, enters CPL3, performs
`NtAllocateVirtualMemory`, writes and reads the allocation, and exits through
`NtTerminateProcess`. A separate context checks the exit status, reaps the
thread, closes the process and checks the PFN baseline. The termination syscall
is routed for this current-process case. The test constructs a
minimal PE32+ fixture and uses the existing `PeImageView64` parser and mapper
to copy it into private Mm pages, then makes its headers read-only and code
execute/read before entering its PE entry point. This is not `SEC_IMAGE`, a
file-backed image section, the production process loader or an NTDLL startup;
it does not create a PEB or initialize static TLS, and does not complete M6 or M7.
The test now allocates a minimal user TEB and the context switch sets the
thread's GS base; CPL3 writes and reads its first TEB TLS slot, and the test
checks the stored value and bootstrap GS restoration after exit. This is not
static PE TLS initialization. The entry can enable interrupts when the
scheduler clock owns the routing; the bootstrap syscall/GS handling is
not a general user-thread context switch.

A second QEMU test runs two Ps-owned CPL3 threads from a PE fixture. Thread A
allocates and checks memory through a syscall, blocks on
an Ob event, resumes after thread B signals it, checks its GS-based TEB slot,
signals B and terminates the process. B uses a different TEB slot and blocks
on the second event; both slots, the exit status, deferred reaping and PFN
baseline are checked. Event handles belong to the process's Ob table and
waits retain object references across Ke switches. The test maps an NTDLL PE
into the same process, resolves its exports and calls its real allocation,
event, wait and termination stubs. The fixture includes a PE TLS directory;
the Ps loader reads templates through `PeImageView64`, validates writable
indices and assigns distinct module indices; the Ps user-thread initializer allocates separate
stacks, TEBs, TLS vectors and template copies.
Both CPL3 threads verify and change their own copy. The Ps reaper releases
those allocations from another context; an invalid TLS layout exercises
post-allocation rollback with PFN accounting. QEMU checks that unsupported
TLS callbacks are rejected before changing the index. QEMU exercises two PE
TLS modules with different templates and checks the separate values in two
and three user threads. This is bounded to sixteen modules and one page per
thread, without callbacks, and is not full loader initialization. Ps now owns
the bounded image-loading sequence: it allocates
Mm section views backed by canonical Ob objects and PFNs, uses the existing
`NativeUserImageSet64` loader to map both
PE files and bind the fixture's imports to NTDLL, and applies RO/RW/RX section
protections before the user code calls its IAT. QEMU checks rejection of W+X,
duplicate-name rollback with PFN accounting, and an unresolved import before
loading the missing DLL. QEMU verifies that both images are section views and
that their Ob object count and PFNs return to baseline on process teardown.
This loader borrows the names and source files, only
supports contiguous page-aligned readable sections, and requires the active
process root. The threads are still initiated by the kernel test; file-backed
`SEC_IMAGE` semantics, normal NTDLL startup, complete PEB loader and process
parameters, TLS callbacks, native
user-thread creation, normal preemption
and general process termination remain outstanding. The test is not
sufficient to declare M6 or M7 complete.

Ps allocates a process-owned PEB and loader-data page after the images are
finalized and links the PEB from each user TEB. Its public memory-order module
list contains the mapped image bases and corresponding PE checksum and
timestamp; entries are linked in address order. QEMU checks the shared PEB,
the documented initial `BeingDebugged` byte, the list links and all three
module bases, then frees both pages with the process. Full DLL names remain
empty because the bootstrap sources have no verified file paths; process
parameters and other startup state remain absent. The limited layout follows the public
[PEB](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-peb)
and [TEB](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb)
declarations and [PEB_LDR_DATA](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-peb_ldr_data);
ReactOS `sdk/include/ndk/peb_teb.h` was consulted for context.
ReactOS `ntoskrnl/ke/amd64/thrdini.c` and `ntoskrnl/ps/thread.c` were consulted
for user thread startup and context ownership.
Hardware reference: [Intel 64 and IA-32 SDM, volume 3A](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf).
The local ReactOS `hal/halx86/generic/timer.c` and `ntoskrnl/ke/time.c`
were consulted for clock interrupt and dispatch ordering, without copying code.

| Check | Result |
|---|---|
| `python3 qemu.py --headless` | Passed; LAPIC IRQ, timer-preempted kernel and CPL3 contexts, register canaries, existing boot, scanout and input checks |
| `python3 qemu.py --headless` | Passed; persistent BSP scheduler clock, CPL3 preemption with syscall IF preservation, idle ticks and polled PS/2 input |
| `python3 qemu.py --headless` | Passed; Ke idle context selected for an empty ready queue, timer wake and waiting-thread resumption |
| `python3 qemu.py --headless` | Passed; clock IRQL masking and deferred IRQ delivery together with idle and preemption regression |
| `python3 qemu.py --headless` | Passed; Ps/Ob process handle lifetime, self-handle termination, delayed reap and PFN/object counts |
| `clang++ ... -fsanitize=address,undefined test/ke_freestanding_ready_queue_test.cpp test/kernel_ke_ready_queue_test.cpp` | Passed; ready-queue regression before idle initialization |
| `python3 qemu.py --headless` | Passed; PIT-calibrated LAPIC period and persistent clock; no independent accuracy measurement was made |
| `python3 qemu.py --headless` | Passed; Ob event references and access, Ke wait/wake, two notification waiters, CPL3 NTDLL event polling and error checks |
| `python3 qemu.py --headless` | Passed; Ps-owned CPL3 context, native allocation, current-process termination, exit status and PFN cleanup |
| `python3 qemu.py --headless` | Passed; Ps image loading through Mm/Ob sections and PE import binding, two CPL3 threads, NTDLL, Ps-created TLS and stacks, blocking events, exit status, image/TLS rollback and PFN/Ob cleanup |
| `python3 qemu.py --headless` | Passed; kernel APC FIFO order, duplicate insertion and refusal to reap a thread with queued APCs |
| `python3 qemu.py --headless` | Passed; three CPL3 threads, cancellation of unsignaled event waits, wait restoration, process termination and event/section object reclamation |
| `clang++ ... -fsanitize=address,undefined test/mm_process_backend_test.cpp` | Passed; ordinary section protection and backend regression |
| `llvm-readobj --coff-imports build/qemu-cmake-mingw/ntdll-process.dll` | Passed; no external PE imports |
| `python3 -m unittest discover -s test -p 'test_bootstrap_runtime_runner.py' -v` | 7 passed; ordered markers and runtime mode checks |

### Bootstrap VAD reserve, commit, protection and release

The cooperative Ke bootstrap now initializes its thread and processor CR3
from the hardware register and rejects a context switch if the saved state
does not match it. The BSP-only cooperative switch accepts registered roots,
reloads CR3 for a different root and updates the current processor state.
QEMU exercises the existing register and stack canaries with both shared and
distinct roots. This does not yet establish preemption or process lifecycle.

The AMD64 bootstrap can create PFN-backed page-table roots with private user
slot 128 and shared supervisor branches. With interrupts disabled on the BSP,
the Mm backend operates on the active root, checks its owner context and
reloads CR3 when switching roots. QEMU maps distinct private pages at the same
virtual address in two roots and maps one section into both; it verifies the
private values, shared updates, user/kernel access checks, independent
teardown and PFN accounting. The roots are currently exercised directly by
the diagnostic. Freestanding `EProcess` now owns its root, VAD space and Ob
handle table, binds threads to its CR3 and closes those resources after its
threads detach. The current-process section operations and `NtClose` use the
owning thread's address space and handle table. QEMU runs two Ke contexts
owned by different Ps objects, exercises shared sections and distinct private
pages through Ps, Ke, Mm and Ob, rejects an absent per-process handle and a
conflicting view, and resumes the second context after the first process's
cleanup. The PFN count returns to baseline; injected root-allocation failures
also leave the count and shared root slots unchanged. This completes the
bounded bootstrap M2 acceptance path, not `NtCreateProcessEx`, normal process
termination or arbitrary process-handle lookup. Those remain future work.

| Check | Result |
|---|---|
| `python3 qemu.py --headless` | Passed; Ps-owned roots, Ke CR3 switches, isolated handles and pages, shared section, survivor after cleanup, injected root failures and PFN accounting |
| `python3 qemu.py --headless --runtime-selftest` | Passed; bootstrap runtime teardown; this mode halts before the Ps/Ke process-context diagnostic |
| `clang++ ... -fsanitize=address,undefined test/mm_process_backend_test.cpp` | Passed; backend rollback and teardown regression |
| `python3 qemu.py --headless` | Passed; loaded NTDLL allocation/free, decommit/recommit, error cases and process-owned allocation paths |
| `clang++ ... -fsanitize=address,undefined test/ke_context_hosted_test.cpp` | Passed; hosted context regression |
| `python3 scripts/verify_kernel_architecture.py` | Passed; one active modular kernel |

The canonical `ProcessAddressSpace` can use the AMD64 bootstrap paging backend
and the UEFI-derived PFN database. The boot test reserves an unmapped VAD,
rejects an overlap, commits a page inside it, reads and writes that real
virtual mapping, repeats the commit, and releases the reservation and its
PFNs and page tables. A second boot test reserves 128 KiB and commits a 4 KiB
page, then tests read/write access, a read-only protection change, decommit,
recommit and release across four cycles with PFN accounting. On QEMU TCG it
also checks actual page faults for an uncommitted neighboring page, a write to
a read-only page and a decommitted page, checking the fault address and error
code. The diagnostic temporarily enables supervisor write protection and uses
its own IDT before restoring the previous state.

Both the bootstrap and process tests use a bounded 16 MiB window; only the
process-root path provides the isolation described above. The backend requires the BSP with interrupts
disabled and has no SMP TLB shootdown. Commit eagerly allocates physical pages;
private pages support read-only, read-write and execute/read protection. Demand paging and
general NT syscall coverage remain unimplemented.
Backend-chosen addresses are 64 KiB aligned; explicit native addresses are
rounded to 4 KiB, independently of the Win32 reservation contract. A failed
unmap retains its VAD/PFN state for an explicit, retryable `close()`; destroying
an address space that still cannot release its mappings is an ownership
invariant failure. A failed multi-page protect can leave partial changes if
rollback itself fails; the tracked page protections retain the actual state.

The relevant public contracts are
[NtAllocateVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntallocatevirtualmemory)
and [NtFreeVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntfreevirtualmemory).
The freestanding `NtProtectVirtualMemory` service is not yet routed. Its
native declaration and complete error and capture semantics still require an
approved Microsoft contract; the Microsoft WDK metadata project tracks the
absence of NTDLL declarations in [issue 58](https://github.com/microsoft/wdkmetadata/issues/58).
The local ReactOS `ntoskrnl/mm/ARM3/virtual.c` implementation was inspected as
a reference, but is not by itself the required compatibility contract.
Structural comparison used ReactOS commit
`8fe3cee05606aecd9442674e40038649f048d10d`, specifically
`ntoskrnl/mm/ARM3/vadnode.c` and `ntoskrnl/mm/ARM3/virtual.c`; the MicaNT
implementation is independent.

The bootstrap test writes a PFN-backed private page while it is read/write,
changes it to execute/read, enters CPL3 at that page, and verifies the trap,
write protection and PFN reclamation. This is an Mm execution/protection test,
not an image-section mapper or a running PE process. The execute/read contract
is documented in [Memory Protection Constants](https://learn.microsoft.com/en-us/windows/win32/memory/memory-protection-constants).

The bootstrap syscall dispatcher now routes the current-process subset of
`NtAllocateVirtualMemory` and `NtFreeVirtualMemory` to the owning address
space. A CPL3 diagnostic calls the actual NTDLL exports and checks reserve,
commit, decommit, recommit, release, invalid pointers and release-size errors;
PFN accounting is checked after the diagnostic. Unsupported process handles,
nonzero `ZeroBits` and unimplemented allocation/protection flags fail rather
than simulating success. This does not establish the full native VM contract.

#### Hosted virtual-memory service boundary

The hosted native service currently uses a single private
`ProcessAddressSpace` model, not a process-bound baremetal address space.
It accepts the current-process pseudo-handle, zero `ZeroBits`, and read-only or
read-write protection; unsupported cases fail explicitly. The hosted model
does not provide real accessible user mappings. Its current protect and
decommit service wrappers support only a whole VAD. The native dispatcher has
no protect registration, and the freestanding bootstrap does not link or
dispatch `kernel/mm/virtual.cpp`.

The separate System32 ABI entrypoints still use a shared arena and `calloc`,
respectively, not this address space or the real syscall path. Their existing
audit is therefore not evidence of canonical VM integration. Some other hosted
tests still use a zero process handle or nonzero release size and need to be
updated rather than relying on the old service behavior.

The hosted Win32 bridge rejects zero sizes before reservation rounding and
requires 64 KiB-aligned reservation results. If a broken allocator returns an
unaligned reservation, the bridge attempts release; it reports the release
failure through `GetLastError`, or `ERROR_INVALID_ADDRESS` after successful
cleanup. A failed cleanup can retain an inaccessible reservation: the Win32
API has no output parameter for returning that address on failure. The
allocator's 64 KiB alignment invariant is therefore essential, not a
substitute for recovery.

#### Bootstrap section views

The existing section control area retains its PFNs until its last owner is
released, and hosted VADs keep the section object alive until their views are
unmapped. AVL removal transfers that ownership when replacing a node with its
successor, so the successor's view does not lose its section reference.
Allocation failure is reported rather than publishing an invalid
section. A limited freestanding `SEC_COMMIT` path validates RW/RO protection,
explicitly zeroes allocated PFNs and rejects unsupported variants. The
bootstrap paging backend can map these PFNs into two VADs, with one backing
reference per view and independent unmapping. Failed mappings are rolled back;
if an unmap fails, the VAD retains ownership for a subsequent cleanup attempt.
In QEMU, the boot test writes through one view and reads through the other,
checks the read-only mapping and fault probe, unmaps the first view, and checks
the remaining view and PFN accounting after the last unmap.

The AMD64 bootstrap dispatcher now accepts the four register arguments and
copies stack arguments from the user stack. A limited anonymous `SEC_COMMIT`
section service in `kernel/mm/virtual.cpp` uses the canonical Ob handle table
and the bootstrap paging backend. Unnamed `OBJECT_ATTRIBUTES` with no other
attributes are captured from user memory and supported. A CPL3 diagnostic issues create, map, unmap
and close syscalls, checks handle access denial, shared data and the lifetime
of a view after close, and checks PFN accounting after cleanup. An additional
CPL3 PE diagnostic loads the real NTDLL PE, resolves the four section/close
exports, and calls them through the syscall boundary. This uses a bounded
build-time fixture and explicit export resolution, not process DLL startup or
an application import table. The client also checks an invalid output pointer,
access denial, unsupported inheritance and a repeated unmap. The boot ownership test checks that the Ob
section object itself remains alive after its last handle closes until its
last view is unmapped.
An injected second-page mapping failure in QEMU verifies that the first real
PTE is removed, the VAD disappears and PFN accounting returns to baseline.
The internal backend also changes view page protections and rejects upgrading a
view beyond its retained access; a bare-metal QEMU test exercises those changes.

M1's anonymous-section bootstrap acceptance path is complete: the loaded
NTDLL exports reach the syscall dispatcher, Ob and hardware-backed Mm, and
the QEMU tests above cover sharing, protection, unmapping and reclamation.
This does not establish general NT section compatibility. The service is
limited to the bootstrap current-process pseudo-handle and bounded bootstrap
address window. A QEMU CPL3 probe
also checks nonzero section offsets, 64 KiB rounding and distinct backing pages;
process handle access checks, normal NTDLL loading and import binding, complete
argument and protection semantics, a native protection syscall, and a real
process address space remain unimplemented. `ViewShare` is rejected until
child-process view inheritance is available. The bootstrap test depends on
the limited freestanding `SectionObject` implementation. Microsoft contracts:
[create](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatesection),
[map](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwmapviewofsection),
[unmap](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwunmapviewofsection).
The active AMD64 backend prepares every page-table level and checks every leaf
before publishing a view; a failed preparation publishes no leaf. It also
validates an entire view before unmapping it. QEMU injects allocation failures
at each page-table level, an invalid PFN, and a conflicting PTE, and verifies
that existing mappings and PFN accounting remain intact. These operations
currently rely on the single BSP and disabled interrupts; SMP and TLB
shootdown remain future work. The generic per-page backend retains a failed
rollback and retries it before a later section map; a persistent backend
failure can still retain its mapping and backing.
ReactOS comparison used `ntoskrnl/mm/section.c` and
`ntoskrnl/mm/ARM3/section.c` at the revision above.

| Command | Result |
|---|---|
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/mm_avl_section_owner_test.cpp -o build/mm_avl_section_owner_test && build/mm_avl_section_owner_test` | Passed; successor ownership and final release without host traps |
| `clang++ -std=c++23 -DMICANT_FREESTANDING -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/mm_shared_backend_test.cpp -o /tmp/micant-mm-shared-test && /tmp/micant-mm-shared-test` | Passed; two backend views, ownership and mapping failure rollback |
| `python3 qemu.py --headless` | Passed; section lifecycle through direct syscalls and loaded NTDLL exports, access denial, Ob lifetime, PFN accounting and existing modular boot checks |
| `x86_64-w64-mingw32-clang++ ... tools/dlls/ntdll.cpp` and `llvm-objdump` | Built a native PE DLL and verified the four section/close exports and syscall instructions |

#### Bootstrap VAD verification

| Command | Result |
|---|---|
| `python3 qemu.py --headless` | Passed; VAD/mapping marker and existing CPL3, syscall, scanout and PS/2 checks |
| `python3 qemu.py --headless --runtime-selftest` | Passed; VAD/mapping marker and runtime teardown |
| `python3 -c 'import subprocess; subprocess.run(["python3", "qemu.py", "--headless"], timeout=180, check=True); subprocess.run(["python3", "qemu.py", "--headless", "--runtime-selftest"], timeout=180, check=True)'` | Passed; each QEMU run also had a 180-second external timeout |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/mm_process_backend_test.cpp -o build/mm_process_backend_test-asan && build/mm_process_backend_test-asan` | Passed; backend failures, rollback and teardown |
| `python3 -m unittest discover -s test -p 'test_bootstrap_runtime_runner.py' -v` | 7 passed |

### PE TLS template initialization

The bounded native PE loader can initialize separate aligned storage blocks
from an inspected image's TLS template, including zero-fill. It rejects
insufficient, misaligned or overlapping storage. A bare-metal boot test maps
an original synthetic PE and verifies two independent template copies and
error paths. These are storage blocks, not scheduled threads: TLS index
allocation, per-thread TLS vectors, callbacks, user mappings and process
startup remain unimplemented.

The template and zero-fill fields follow the
[Microsoft PE/COFF TLS directory](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-tls-directory).

| Command | Result |
|---|---|
| `python3 qemu.py --headless` | Passed; TLS template marker and existing CPL3, syscall, display and input checks |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/pe_tls_freestanding_test.cpp -o build/pe-tls-freestanding-test && build/pe-tls-freestanding-test` | Passed; TLS template and failure paths |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/pe_image_view_test.cpp -o build/pe-image-view-test && build/pe-image-view-test` | Passed; native PE mapping and import regression |

### Bootstrap Ke ready-queue withdrawal

The freestanding `PriorityScheduler` can withdraw a queued thread and place it
in the internal `Transition` state for its lifecycle owner. It also rejects
queueing the current thread. The boot test exercises head, middle and tail
withdrawal, changed priority, requeueing, and FIFO order. This does not
implement thread termination, preemption, SMP scheduling, or interrupt-safe
synchronization; callers must retain thread ownership and keep interrupts
disabled in the bootstrap.

| Command | Result |
|---|---|
| `python3 qemu.py --headless` | Passed; ready-queue withdrawal marker and existing boot checks |
| `python3 qemu.py --headless --runtime-selftest` | Passed; ready-queue withdrawal and runtime teardown |
| `clang++ -std=c++23 -DMICANT_FREESTANDING -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/ke_freestanding_ready_queue_test.cpp test/kernel_ke_ready_queue_test.cpp -o build/ke-freestanding-ready-test && build/ke-freestanding-ready-test` | Passed; queue and withdrawal regression |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -Iinclude test/ke_context_hosted_test.cpp -o build/ke-context-hosted-test && build/ke-context-hosted-test` | Passed; hosted context regression |

### Bootstrap PFN-backed paging and VAD tree

The AMD64 bootstrap now obtains intermediate page-table pages from the
canonical UEFI-initialized PFN database, zeroes them before use, and releases
empty owned tables after unmapping. Failed page-table allocation rolls back
newly allocated levels. The QEMU test writes and reads a physical frame through
a virtual mapping, exercises duplicate rejection and injected allocation
failures, remaps after invalidation, and checks reclamation. It runs only on
the bootstrap CPU with interrupts disabled; there is no remote TLB shootdown.
Firmware mappings are not owned by this path.

The same kernel executes the canonical `MmAvlTable` using the page-backed
allocator. The test checks partial overlap rejection, gap searches, removal and
PFN accounting after tree destruction. This is a tree test, **not** a
process address space, reservation, commitment, or section-mapping test. The
later bootstrap VAD increment above connects `ProcessAddressSpace` to the
hardware backend; native virtual-memory syscalls, section views, per-process
isolation and general fault handling remain incomplete.

ReactOS reference, revision
[`8fe3cee05606aecd9442674e40038649f048d10d`](https://github.com/reactos/reactos/tree/8fe3cee05606aecd9442674e40038649f048d10d):
`ntoskrnl/mm/ARM3/vadnode.c`, `virtual.c`, `section.c`, and `pagfault.c`.
The [WDK allocation contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwallocatevirtualmemory)
distinguishes reserving a range from committing pages within it; the
[section contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/managing-memory-sections)
requires views and section objects to retain their respective lifetimes.

| Command | Result |
|---|---|
| `python3 qemu.py --headless` | Passed; PFN-backed page-table and VAD-tree markers, existing CPL3, syscall, display and mouse checks |
| `python3 qemu.py --headless --runtime-selftest` | Passed; VAD cleanup and existing C++ lifecycle teardown |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/mm_vad_ranges_test.cpp -o build/mm-vad-ranges-test && build/mm-vad-ranges-test` | Passed; hosted VAD regression |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -DMICANT_FREESTANDING -Iinclude test/mm_avl_freestanding_test.cpp -o build/mm-avl-freestanding-test && build/mm-avl-freestanding-test` | Passed; shared VAD tree including gap-search stress |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Iinclude test/mm_page_table_entry_test.cpp -o build/mm-page-table-entry-test && build/mm-page-table-entry-test` | Passed; PTE layout and NX encoding |
| `python3 -m unittest discover -s test -p 'test_bootstrap_runtime_runner.py' -v` | Passed; 7 runner tests |

### Hosted VAD range exclusion

The canonical hosted `ProcessAddressSpace` now rejects overlapping reservations
and section views, including partial overlaps, and searches gaps for automatic
placements using inclusive bounds and checked arithmetic. The AVL insertion
also rejects overlapping ranges; failed section mappings no longer fall back
to an unrelated anonymous allocation. The Executive's separate
`NtMapViewOfSection` path returns `NotImplemented`: its former host-buffer
address was not a mapped process view.

This is a hosted model, not a freestanding process address space. A non-null
commit-only request returns `NotImplemented` rather than inserting another VAD
or claiming to commit part of a reservation: the model tracks commitment per
VAD, not per page. Explicit unaligned allocation requests also return
`NotImplemented`; allocation-granularity rounding and the full Windows
allocation, section, and process-handle contracts remain unimplemented.
The [WDK allocation contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwallocatevirtualmemory)
distinguishes reserving pages from committing a previously reserved range.

| Command | Result |
|---|---|
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/mm_vad_ranges_test.cpp -o build/mm-vad-ranges-test && build/mm-vad-ranges-test` | Passed; hosted overlap, search, bounds, section caller and VAD removal checks |
| `clang++ -std=c++23 -ffunction-sections -fdata-sections -Wno-unused-parameter -I include test/executive_unimplemented_test.cpp kernel/ps/psmgr.cpp kernel/mm/virtual.cpp kernel/io/iomgr.cpp -Wl,-dead_strip -o build/executive-unimplemented-test && build/executive-unimplemented-test` | Passed; Executive failure reporting and unchanged mapping outputs |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Iinclude test/pfn_database_bootstrap_test.cpp -o build/pfn-vad-regression-test && build/pfn-vad-regression-test` | Passed; PFN database regression |

### Freestanding Ke ready queues

The canonical `PriorityScheduler` now has an allocation-free freestanding
ready-queue implementation using links owned by `KThread`. It selects the
highest priority first and preserves FIFO order within a priority, rejects
duplicate queueing, and puts selected threads into `Standby` without counting
a context switch. The existing cooperative AMD64 context switch accepts a
selected thread and rejects a thread still in a ready queue.

The QEMU bootstrap test enqueues and selects two existing kernel contexts,
checks duplicate rejection and the switch counter, and actually switches
through both contexts. This is limited to the bootstrap's single CPU with
interrupts disabled and a shared address space. It is not a preemptive
scheduler, an interrupt-safe SMP queue, thread creation, event waits, or
Ring 3 process execution. Ke's existing spinlock does not mask hardware
interrupts, so this queue must not be used by interrupt-enabled scheduling
paths until interrupt synchronization is implemented.

The priority ordering follows the documented
[Windows scheduling priority model](https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities);
the broader Ke comparison and ReactOS reference paths remain recorded in
[NT_KERNEL_EXPANSION_MATRIX.md](NT_KERNEL_EXPANSION_MATRIX.md).

| Command | Result |
|---|---|
| `clang++ -std=c++23 -DMICANT_FREESTANDING -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Iinclude test/ke_freestanding_ready_queue_test.cpp test/kernel_ke_ready_queue_test.cpp -o build/ke-freestanding-ready-test && build/ke-freestanding-ready-test` | Passed on the host; priority, FIFO, duplicate, state and counter checks |
| `clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -I include test/ke_context_hosted_test.cpp -o build/ke-context-hosted-test && build/ke-context-hosted-test` | Passed; hosted context regression |
| `python3 qemu.py --headless` | Passed; ready-queue marker, cooperative context canaries, boot, scanout and PS/2 checks |

### Executive failure reporting

The hosted Executive's `NtTerminateProcess` and `NtProtectVirtualMemory` no
longer report success without terminating a process or changing protection.
`NtQueryInformationProcess(ProcessBasicInformation)` no longer returns invented
process IDs or a fabricated PEB address. These paths now report
`STATUS_NOT_IMPLEMENTED` after their existing basic argument checks; output
values remain unchanged on that failure.

The hosted implementations in `kernel/ps/psmgr.cpp` and
`kernel/mm/virtual.cpp` are not connected to the bootstrap syscall service.
This change does not implement process termination, process queries, virtual
memory protection, process address spaces, or native syscall dispatch.

| Command | Result |
|---|---|
| `clang++ -std=c++23 -ffunction-sections -fdata-sections -Wno-unused-parameter -I include test/executive_unimplemented_test.cpp kernel/ps/psmgr.cpp kernel/mm/virtual.cpp -Wl,-dead_strip -o build/executive-unimplemented-test && build/executive-unimplemented-test` | Passed on the macOS host; checks failure status and unchanged outputs |
| `cmake --build build/qemu-cmake-mingw --config Release --target ntoskrnl_core -j 4` | Passed; hosted Executive static library rebuilt with the cross-toolchain |

No QEMU test was run for this hosted-only change; it is not evidence of
bare-metal process or memory behavior.

### Protected syscall boundary

Commit `d8e8d42`: one shared user-service ABI helper; real AMD64 entry/return
and bounded graphics/input services. There is still no Executive scheduler,
general NT service dispatch, interrupt-driven input, RTC service or accelerated
composition session.

### Bounded native image preparation

The allocation-free `PeImageView64` lives in the canonical
[`pe.hpp`](../include/micant/um/native/pe.hpp); the existing hosted
`PeLoader::inspect` delegates to it. `NativeUserImageSet64` in the canonical
[`usermode.hpp`](../include/micant/km/executive/usermode.hpp) owns no process
or address space: it borrows image storage and resolves bounded imports,
exports and forwarders without synthetic trampolines.

Host ASan/UBSan tests cover malformed/truncated PE input, mapping, relocations,
named/ordinal imports and exports, missing bindings, forwarder cycles, overlap,
TLS layout and real Explorer/windowing/UCRT images:

```bash
clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -Iinclude test/pe_image_view_test.cpp \
  -o build/pe_image_view_test
build/pe_image_view_test build/explorer.exe build/system32/user32.dll \
  build/system32/gdi32.dll build/system32/imm32.dll build/system32/win32k.dll
```

Result: passed. The QEMU diagnostic constructs an original small PE containing
the compiled syscall client, maps its headers RO/NX and code RX, then verifies
actual execution/return at CPL3. Minimal memory/string primitives are supplied
by [`kernel/ex/freestanding.cpp`](../kernel/ex/freestanding.cpp).

**Remaining:** firmware/user image handover, owned physical pages and address
spaces, process/thread lifecycle, PEB/TEB and static TLS initialization,
DLL attach/detach and complete native runtime closure. Inspecting TLS does not
initialize TLS. The older hosted loader/trampoline path is not a native
fallback and is not enabled by this increment.

### Native UCRT memory subset

The canonical UCRT source has an isolated freestanding memory/string branch
with a bounded 64 MiB coalescing arena and native PE static TLS for `errno`.
The supported `ucrtbase_boot` CMake target builds it into
`build/system32/ucrtbase.dll`; it is included in the Explorer build aggregate,
which remains blocked by the Win32k boundary violation below.
It exports 26 memory/string/errno/math/startup functions, including `ceilf`,
on-exit table operations, initializer-table dispatch, and aligned allocation,
with no external PE imports. `_aligned_malloc` validates nonzero size and
power-of-two alignment, sets `errno` on invalid input or allocation failure,
and `_aligned_free` returns the block to the same arena. The CRT invalid
parameter handler contract is not implemented.

Verified:

```bash
clang++ -std=c++23 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  test/ucrt_bounded_arena_test.cpp -o build/native-ucrt-arena-sanitized
build/native-ucrt-arena-sanitized
clang++ -std=c++23 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -Iinclude test/ucrt_ceilf_test.cpp \
  -o build/ucrt-ceilf-test
build/ucrt-ceilf-test
clang++ -std=c++23 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -Iinclude test/ucrt_onexit_test.cpp \
  -o build/ucrt-onexit-test
build/ucrt-onexit-test
cmake --build build/qemu-cmake-mingw --target ucrtbase_boot
build/pe_image_view_test build/system32/ucrtbase.dll
```

Results: passed. LLVM PE inspection confirms the actual target's empty import
table and valid TLS template/index; `_errno` disassembly reads the GS TLS vector
at offset `0x58`. The native target uses `-fno-emulated-tls`, no hosted CRT,
and an entry appropriate only to its constant-initialized memory subset.
The windowing DLLs still retain their real CRT constructor startup.
The canonical hosted UCRT branch also compiles with the cross-toolchain.

This is not a staged boot module or complete UCRT. TLS has not been executed
by the kernel; mapping/inspection is not a substitute. Its contract follows
the public [CRT errno documentation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/errno-doserrno-sys-errlist-and-sys-nerr)
and [PE TLS specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-tls-section).
The PE closure measured before the boundary gate was introduced still has 23
Kernel32 and 21 UCRT missing exports (44 total), down from 73. The current
`python3 qemu.py --check-explorer` stops earlier: CMake rejects `win32k_boot`
because its transitive dependencies include private `micant/km/` headers. The
initializer and on-exit helpers are not full DLL CRT startup; the kernel loader
does not yet dispatch module initializers or TLS. No fake export, host DLL or
synthetic import trampoline fills those gaps.

### Freestanding Kernel32 last-error accessors

The canonical Kernel32 ABI source has a freestanding `kernel32_boot` target
that exports `GetCurrentProcessId`, `GetCurrentThreadId`, `GetLastError`, and
`SetLastError`. The ID accessors read `TEB64.UniqueProcessId` / `UniqueThreadId`
at GS offsets `0x40` / `0x48`; last-error accessors read/write offset `0x68`.
Compile-time assertions pin these offsets and the PEB pointer at `0x60`. The
DLL has no external PE imports. The native process loader does not yet
initialize a real process TEB or GS base; only the isolated CPL3 diagnostic
TEB has been exercised.
The API contracts are documented by Microsoft Learn for
[`GetCurrentProcessId`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocessid),
[`GetCurrentThreadId`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadid),
[`GetLastError`](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror),
and
[`SetLastError`](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setlasterror).

Verified:

```bash
cmake --build build/qemu-cmake-mingw --target kernel32_boot
cmake --build build/qemu-cmake-mingw --target ntoskrnl_core
python3 scripts/verify_kernel_architecture.py
python3 qemu.py --headless
```

Results: passed. LLVM PE inspection confirms the four named exports; headless
QEMU confirms the isolated TEB GS field probe and the existing syscall, input,
and scanout checks. This does not establish real process TEB
initialization. Later increments resolved those conversion imports and two
locale-profile imports; the current preflight reports 23 missing Kernel32
exports and 21 missing UCRT exports (44 total), so import closure remains
incomplete.

### Freestanding UTF-8 / UTF-16 conversion

The canonical Kernel32 ABI source now exports `MultiByteToWideChar` and
`WideCharToMultiByte` from `kernel32_boot`. This implementation supports
`CP_UTF8` and `CP_ACP` (the project's `GetACP()` is 65001), UTF-8 scalar
validation, UTF-16 surrogate pairs, `-1` null-terminated lengths, size queries,
strict invalid-input flags, replacement characters when strict mode is off,
and insufficient-buffer/error reporting. Other code pages and UTF-8 default
character pointers are rejected; this is a bounded UTF-8 profile, not full
Windows NLS/code-page support.

The public contracts are documented by Microsoft Learn for
[`MultiByteToWideChar`](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar)
and
[`WideCharToMultiByte`](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte).
The allocation-free conversion routines have ASan/UBSan host coverage:

```bash
clang++ -std=c++23 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  test/utf8_conversion_test.cpp -o build/utf8-conversion-test
build/utf8-conversion-test
cmake --build build/qemu-cmake-mingw --target kernel32_boot
build/llvm-mingw-20260616-ucrt-macos-universal/bin/llvm-readobj \
  --coff-exports build/system32/kernel32.dll
python3 qemu.py --check-explorer
```

Results: host conversion tests and DLL build passed; LLVM PE inspection confirms
the two new named exports. The preflight resolves both new imports and fails on
the remaining 46 exports (23 Kernel32, 23 UCRT). The full Explorer executable
still is not loaded or executed.

### Freestanding UCRT UTF-8 locale profile

The freestanding UCRT now exports `___lc_codepage_func` and
`___mb_cur_max_func`, returning codepage 65001 and maximum encoded character
length 4 for the UTF-8 locale profile. The canonical hosted UCRT helper's
return type now matches the MinGW UCRT declaration (`int`) and reports the
same maximum. This does not implement locale selection, locale data, or
`localeconv`.

The cross-toolchain declaration for `___mb_cur_max_func` is in its public
`stdlib.h` and declares an `int` return; `___lc_codepage_func` is declared in
`locale.h` with an `unsigned int` return. Verification:

```bash
cmake --build build/qemu-cmake-mingw --target ucrtbase_boot
build/llvm-mingw-20260616-ucrt-macos-universal/bin/llvm-readobj \
  --coff-exports --coff-imports build/system32/ucrtbase.dll
python3 qemu.py --check-explorer
```

Results: the freestanding UCRT target build passed and LLVM confirms both
named exports with no external imports. The previously built runtime resolves
both locale imports but still has 44 missing imports (23 Kernel32, 21 UCRT).
A legacy `build/` host cache points to a different, removed source checkout
and could not run `cmake --build build --target micant_tests`; it was left
untouched.

### Strict user-mode / kernel-mode build boundary

`scripts/check_user_mode_boundary.py` runs the configured C++ compiler's
preprocessor dependency scan for each native user-mode target variant. CMake
adds the corresponding check as a prerequisite of each DLL/application target
and of the Explorer aggregate. The compliant freestanding Kernel32, UCRT,
User32, GDI32 and IMM32 source variants pass. The Win32k DLL variant is
rejected: `kernel/ps/winnt_win32k_server.cpp` depends on `micant/km/win32k/`
and many transitive `micant/km/` Executive, memory, I/O, driver and HAL
headers. As a result, `win32k_boot` and `explorer_runtime_boot` are currently
blocked by design. That implementation must move to the canonical kernel
owner and expose real user-mode service calls before this gate can pass.

A kernel-owned Win32k dispatch entry and WDDM adapter now compile into
`ntoskrnl_core`; the standard Executive dispatcher installs that handler.
The entry rejects in-range shadow calls with `STATUS_NOT_SUPPORTED` rather
than invoking legacy handlers that dereference user pointers directly and
cannot return handles. It is a safety gate, not a working Win32k service.
This is only the hosted modular-kernel step, not a completed migration:
`win32k_boot` still contains the legacy user-mode implementation, the new
module is not part of `ntoskrnl_core_baremetal`, and its object currently has
unresolved C++ standard-library/runtime symbols that the freestanding kernel
cannot import. The next step is a bounded freestanding service implementation
and a user-mode syscall client with validated request marshalling and return
values. Explorer remains blocked.

### Allocation-free firmware memory descriptors

The existing UEFI memory-type and descriptor declarations now live in
[`boot_contract.hpp`](../include/micant/km/hal/boot_contract.hpp), which the
hosted UEFI header already includes. There is one unchanged declaration, not
a second boot memory-map format. Static assertions preserve the 40-byte
descriptor and field offsets; `UefiKernelHandoffV3` is 120 bytes and can carry
the optional staged user-PE file range. The kernel does not yet consume that
range to create a process.
The standalone handoff test and headless QEMU boot pass after the V3 extension.
This only removes a hosted-header dependency; PFN enrollment is not claimed
by the extraction itself.

### Canonical PFN boot backing

Commit `9b6d735` integrates the isolated Mm worker's canonical `PfnDatabase`
adapter. It keeps one manager and shared allocation/reference/free algorithms;
intrusive PFN links replace dynamic page lists. Native metadata is borrowed,
not allocated through a hosted runtime. Hosted legacy initialization remains
available. Initial native pages are Free, never falsely reported Zeroed.

The boot integration owns stable metadata for **65,536 PFNs (256 MiB)**.
It validates descriptor version/stride/range/overlap and supervisor mappings,
enrolls only non-runtime conventional pages above the first MiB, and leaves
all other PFNs reserved. Higher and intentionally unmanaged conventional
pages are counted and logged explicitly, not silently reclaimed.
Loader/firmware, kernel, driver, MMIO and runtime-service pages are not
reclassified as free.

The supplied zero routine verifies writable supervisor identity backing
before clearing actual RAM. Boot dirties all 4096 bytes of an allocated
physical page, verifies reference retention, frees and zeros it, checks every
byte, reallocates that zeroed frame, and verifies two contiguous frames and
restored free/active counts. The smoke runner now requires this real
verification before the existing display/CPL3/input markers.

```bash
clang++ -std=c++23 -DMICANT_FREESTANDING -Wall -Wextra -Werror \
  -Wno-unused-parameter -fsanitize=address,undefined -Iinclude \
  test/pfn_database_bootstrap_test.cpp -o build/pfn-native-sanitized
build/pfn-native-sanitized
clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -Iinclude test/pfn_database_bootstrap_test.cpp \
  -o build/pfn-hosted-sanitized
build/pfn-hosted-sanitized
python3 qemu.py --headless
```

Results: native and hosted sanitizer tests passed, including sparse and
invalid maps, references, zeroing, contiguous allocation, exhaustion and
reinitialization; QEMU passed every old and new required milestone.
The isolated production-flag PFN probe needs only memory copying and its
supplied zero routine, not heap, mutex, TLS, atexit or static-guard symbols.

**Limits:** this is the canonical physical allocator component, not the full
Mm virtual-memory API. Borrowed backing and its private atomic lock are for
the current IRQ-disabled bootstrap only; they do not implement Ke IRQL or
scheduled waits. No parallel bare-metal memory manager is activated.

The subsequent sparse-map regression (`9a10596`) prevents partial physical
release when a requested contiguous range crosses an allocated page and a
reserved hole. Native and hosted sanitizer tests check PFN 0, holes, wrapping
and oversized release ranges without mutating counts or invoking the zero
callback. Headless boot passed again after integration.

### PFN-backed kernel C/C++ allocation

The private runtime frontends in `kernel/ex/freestanding.cpp` now allocate
through canonical `PfnDatabase::allocateContiguousPages` and release through
its corresponding free path. The boot callback verifies actual writable
supervisor identity backing for every frame before returning a mapping.
No user-mode arena, hosted CRT, new memory manager or dummy export is used.
The existing no-import Native kernel PE still links.

`python3 qemu.py --headless` passes a new mandatory runtime check exercising
actual `malloc`, `calloc`, preserving grow/shrink/failed `realloc`, `free`,
ordinary/array/over-aligned/nothrow C++ allocation and deletion, real RAM
contents, overlap-safe memory moves, overflow rejection logging and restored
PFN accounting. Throwing allocation failure and invalid frees explicitly
stop rather than pretending success. The runtime has no unresolved hosted
symbols in the freestanding object probe.

This is deliberately a page-granular bootstrap allocator: each allocation
consumes contiguous pages, with in-page ownership metadata. It is not a
fragmentation-resistant Executive pool, a pageable heap, an SMP allocator,
general C++ library support, constructor registration, synchronization or
exception/unwind runtime. Its private contract requires one CPU and disabled
interrupts, matching the current boot. Higher managers still need their
shared runtime and lifecycle dependencies from the gap matrix.

### Canonical Ex checked-copy boundary

The existing status-returning helpers in `probe.hpp` now bind a private,
allocation-free native range checker rather than the hosted synthetic
permission map. The binding uses the effective permissions from the active
AMD64 hardware tables and rejects use with interrupts enabled. The native
branch has no C++ `try/catch`; it checks the complete user span and the
supervisor source/destination before copying. The hosted synthetic backend
and exception-enabled helpers retain their existing behavior.

All current bootstrap graphics, serial and mouse syscall buffer accesses now
use those canonical helpers. The real CPL3 diagnostic still checks returned
contents, null/RO/unmapped/supervisor/wrapping rejection, registers and SIMD;
the submitted pixels are checked through QMP. The boot also requires observed
successful and rejected canonical-copy paths, not just linked symbols.

`python3 qemu.py --headless` passed after wiring. Both standalone modes below
passed ASan/UBSan, including absent backend, binding validation, misalignment,
overflow, cross-page protection, untouched rejected destinations and kernel
source/destination validation:

```bash
clang++ -std=c++23 -DMICANT_FREESTANDING -fno-exceptions -fno-rtti \
  -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined \
  -Iinclude test/bootstrap_probe_test.cpp -o build/bootstrap-probe-native-sanitized
build/bootstrap-probe-native-sanitized
clang++ -std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -Iinclude test/bootstrap_probe_test.cpp \
  -o build/bootstrap-probe-hosted-sanitized
build/bootstrap-probe-hosted-sanitized
```

These are private C++ status adapters, **not** exported WDK `ProbeForRead` or
`ProbeForWrite` implementations: the documented Windows ABI is void and
raises exceptions. See the official [read](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-probeforread)
and [write](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-probeforwrite)
contracts. Native validation/copy requires immutable mappings on one CPU with
interrupts disabled; this is not recoverable access to arbitrary faulting
memory or protection against concurrent unmapping. Before enabling scheduling,
IRQs or SMP, replace this restricted backend with proper pinning/fault
recovery. No hosted synthetic access permission is accepted by the kernel.

### Kernel C++ lifecycle and canonical Ob handle storage

The private kernel runtime now dispatches real compiler-generated `.ctors`
and PE `.CRT` initialization after the PFN heap. Bounded `atexit` registrations
run in LIFO order on final shutdown; overflow, invalid phases and library
contract failures explicitly log and halt. The compiler ABI abort path logs
its original format verbatim, not fabricated formatted arguments.

The actual `kernel/ob/obref.cpp` is now linked, with its one original
`g_KernelHandleTable`. Its compiler-generated global constructor allocates the
canonical vector through the real kernel heap. Boot verifies actual handle
creation, granted access, duplication/inheritance, reference/handle counters,
close, rejected double close and empty-table restoration. There is no second
Ob or alternate table. Native `NtClose` is deliberately absent until the
sync/Io/Mm/Cm/Se/Lpc close paths exist: the hosted implementation is preserved,
not replaced by fake close hooks. Namespace/security/process ownership remain
unintegrated.

Verified:

| Command | Result |
|---|---|
| `python3 qemu.py --headless --runtime-selftest` | Passed; genuine C/global constructors allocate/write/check memory, destructors execute in order 21 and all PFN allocation accounting returns to its pre-construction value, including Ob vector destruction |
| `python3 qemu.py --headless` | Passed; normal boot keeps globals alive and preserves CPL3/protection/scanout/mouse tests |
| `python3 -m unittest discover -s test -p 'test_*runtime*.py' -v` | 19 passed; mode-specific milestones, fatal detection, provenance and normal-mode restoration |
| `build/bootstrap-object-handles-sanitized` | Passed; ASan/UBSan canonical handle lifecycle |
| Cross-toolchain compilation of hosted `kernel/ob/obref.cpp` | Passed; native gating does not remove hosted close functionality |
| `llvm-nm -u build/baremetal/ntoskrnl.exe` / `llvm-readobj --coff-imports build/baremetal/ntoskrnl.exe` | No undefined symbols or external PE imports |

The self-test is opt-in and halts after final teardown, never using destroyed
objects to continue a desktop boot. Normal invocations explicitly reset the
CMake option to OFF. Its manifest records the mode; ordinary staging rejects
self-test artifacts. This foundation is still single-CPU/IRQ-disabled,
bounded to 4096 initializers per table and 1024 destructor registrations,
without C++ exceptions, arbitrary libc++ linkage, native TLS, thread-safe
static guards or a general shutdown/session lifecycle.

## Historical integration inventory

The following inventory uses an earlier, different milestone numbering and
describes the state at that point in the integration record. For the kernel
completion sprint use the current table above.

| Milestone | State | Missing acceptance evidence |
|---|---|---|
| M1: freestanding foundation | Partial; PFN heap and kernel constructor/destructor dispatch verified | Shared C++ library/synchronization and broader Executive initialization |
| M2: Mm + Ob | PFN and canonical kernel handle storage verified | Full mapping/freeing/address spaces, namespace/security and NtClose closure |
| M3: Ke + Ps | Cooperative AMD64 context switching verified in QEMU | IRQ/timer backend, scheduler, independent address spaces, process/thread lifecycle |
| M4: Io + syscall | Partial platform boundary only | Canonical driver/device/IRP initialization, completion and NT service routing |
| M5: Ring 3 processes | Diagnostic only | Real process loader, PEB/TEB/TLS, termination and two concurrent processes |
| M6: system services | Not integrated | Registry/filesystem/service/console startup and native Win32 session |
| Interactive Explorer | Not running | Runtime closure, actual shell process, Start/window/message-loop verification |

The Ke bootstrap switch saves/restores the Windows x64 nonvolatile GPRs
(including RSI/RDI), XMM6-XMM15 and floating-point control words. Its QEMU
self-test switches between two real stacks, checks preserved-register canaries,
thread states and five transitions, and returns to the bootstrap stack. The
backend remains cooperative, single-CPU, interrupt-disabled and limited to a
shared address space; this does not establish a dispatcher, preemption,
independent processes or an Explorer process.

The displayed bootstrap taskbar is shared drawing code, not proof of an
Explorer process. VirtIO 2D scanout is not VirGL or an accelerated DWM.

## Parallel audit boundaries

Independent Mm/Cc/FsRtl, Ob/Se/Cm/auxiliary, Ke/Ps and Io/PnP/Po audits use
separate `copilot/integration-*-615faeb8` branches/worktrees based on `d8e8d42`.
They do not change production interfaces. The primary stream owns shared
runtime/build decisions and integration. Worker compilation probes are
compatibility experiments, not production links.

No new allocator, mapping, object, process, IRQL or driver ABI is frozen by
this audit: use the existing canonical contracts. Any required signature
change must be reviewed centrally before worker implementation. Integrate in
dependency order: runtime/backing, Mm, Ob, Ke, Ps, Io, then higher services.
Advanced incomplete features are not prerequisites for the first boot.


### Ps loader metadata batch

Ps can copy caller-supplied UTF-16 full module paths into the PEB loader list
and initialize the documented `ImagePathName` and `CommandLine` fields of a
process-owned parameter block. The bootstrap caller supplies these values; this
does not implement filesystem lookup or complete process parameters. Oversized
combined loader strings are rejected before import binding and protection changes.

The host `ps_image_metadata_test` passed under AddressSanitizer and
UndefinedBehaviorSanitizer. AMD64 freestanding syntax checks passed for the Ps
implementation and the extended two-user-threads test. That test now checks the
metadata through process PEB and loader entries. `python3 qemu.py --headless`
passed after the test's stack use was reduced to avoid an unavailable
freestanding stack-probe symbol. This is not evidence of M6 or M7 completion.

Public layout references: [PEB](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-peb),
[PEB_LDR_DATA](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-peb_ldr_data),
[RTL_USER_PROCESS_PARAMETERS](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-rtl_user_process_parameters),
and [UNICODE_STRING](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_unicode_string).

### M10 native network closure checkpoint

The bare-metal QEMU scenario now reaches the canonical virtio-net miniport and
exercises Ring 3 DHCP, DNS, TCP connect/send/receive, TCP listen/accept and
WinHTTP TLS 1.3. The HTTPS peer provisions a temporary trust root, sends a
known payload and closes with TLS `close_notify`; the guest also rejects an
incorrect hostname, an unknown issuer and an expired certificate. The guest
processes use the shared KUSER mapping, and newly bootstrapped kernel threads
start at `PASSIVE_LEVEL` after a dispatcher switch.

| Command | Result |
|---|---|
| `python3 qemu.py --network-test --headless --timeout 240` | Passed in an isolated copy of the current source, linking `ntoskrnl_core_baremetal`; it verified DHCP/DNS, TCP client and listener/accept, HTTPS payload and all three certificate rejection cases. The isolated test build skipped only the unrelated two/three-user-thread M4 smoke tests, which currently fail before network initialization on the main worktree. |
| M10 fault injection in the same QEMU run | Passed; QMP lowered the virtio link while the TCP peer sent `MICANT-M10-PONG`; the pcap contains two payload copies after link recovery. A malformed Ethernet/ARP/IPv4/UDP/TCP/IPv6 corpus traversed the canonical RX queue and parser before DHCP without stopping the kernel. |

The M10 N0–N5 functional closure, including loss recovery and malformed RX
handling, is verified on the bare-metal network path. Repeat the full boot and
all M4 smoke tests together after resolving the current pre-network M4 test
failure. N6, SMP and general network compatibility are outside this closure.

### Ring 0 reliability roadmap planning record

Planning-only references were added for proposed milestones M19–M27 in
[`KERNEL_MILESTONES.md`](KERNEL_MILESTONES.md). Their specifications define
future acceptance gates for crash/debug, SMP, ACPI/power, IOMMU/DMA, PnP,
memory pressure, hardening, timekeeping and virtualization. No implementation,
freestanding linkage, QEMU result or physical-hardware result is asserted by
this planning record. Existing historical evidence above remains unchanged.
