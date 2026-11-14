# MicaNT Kernel Architecture

This is the authoritative specification for the active kernel architecture.
It supersedes older descriptions where they conflict. The design has one
modular kernel based on `ntoskrnl_core`; the archived standalone source under
[`old/monolithic-kernel/`](../old/monolithic-kernel/) is historical only.

## A. Kernel identity

MicaNT has one canonical kernel implementation: the modular subsystem sources
collected in `ntoskrnl_core`. Bare-metal and host-test builds must use those
same canonical subsystem implementations. Platform-specific code and test
adapters may differ; they must not implement a competing Executive.

`ntoskrnl_core` remains a CMake static library and `micant_kernel` remains a
hosted executable built from `kernel/init/kiinit.cpp`; the host harness is not
a freestanding kernel. An AMD64 freestanding bootstrap PE now validates a
versioned UEFI handoff and renders the shared Explorer desktop/taskbar drawing
code through the separately loaded VirtIO-GPU driver. It is an initial boot
vertical slice, not yet the full
Executive or a user-mode `explorer.exe` process.

## B. Build targets

| Target | Current role | Output / status |
|---|---|---|
| `ntoskrnl_core` | Shared modular subsystem library | CMake static library |
| `micant_tests` | Host unit-test runner linked to `ntoskrnl_core` | `${CMAKE_BINARY_DIR}/host/micant_tests` |
| `micant_kernel` | Hosted initialization harness linked to `ntoskrnl_core` | `${CMAKE_BINARY_DIR}/host/micant_kernel`; not a kernel PE |
| `bootx64` | Bootloader object target in CMake | Does not by itself produce `BOOTX64.EFI` |
| `ntoskrnl_core_baremetal` | Freestanding AMD64 bootstrap, shared desktop renderer, CPL3 diagnostic | `build/baremetal/ntoskrnl.exe`; QEMU verified, Executive integration incomplete |
| `bootx64_efi` | UEFI PE bootloader | `build/esp/EFI/BOOT/BOOTX64.EFI` |
| `virtio_gpu_boot` | Separate boot-start VirtIO-GPU display driver | `build/virtio_gpu.sys`; VirtIO 2D verified in QEMU |
| `explorer_runtime_boot` | Build-only aggregate for Explorer and partial user-mode DLLs; gated by transitive header audit | Build is currently rejected because `win32k_boot` includes private kernel headers; Explorer is not loaded by the kernel |
| `kernel32_boot` | Freestanding TEB-backed process/thread ID, last-error, and UTF-8 conversion subset | `build/system32/kernel32.dll`; 6 exports, no imports, not a complete Kernel32 |
| `ucrtbase_boot` | Freestanding user-mode memory/string and UTF-8 locale-profile subset with native static TLS | `build/system32/ucrtbase.dll`; 26 exports, no imports, not a complete UCRT |
| `win32k_boot` | Legacy user-mode DLL wrapping the Win32k implementation | Rejected by the user-mode boundary audit because it depends on `micant/km/` headers |

The bootstrap kernel is PE32+ AMD64, uses the Native subsystem, and has no
external PE imports. It installs a kernel-owned page-table root, GDT/TSS and
diagnostic IDT. It retains firmware identity mappings and the initial firmware
stack. The canonical PFN allocator now enrolls usable firmware pages within
an explicit first-256-MiB bootstrap window; physical writes, real zeroing,
references and contiguous release are verified in QEMU. Full virtual-memory
integration, scheduler and process environment remain missing. A host target
passing does not validate this artifact.

Kernel C++ constructors now run after the PFN-backed heap and initialize the
canonical global Ob handle table. Creation/duplication/reference/close paths
are exercised in QEMU; native NtClose and namespace initialization are still
absent. Opt-in `python3 qemu.py --headless --runtime-selftest` additionally
verifies final LIFO destructors and complete page reclamation, then halts.
Its mode-tagged artifacts are not accepted by normal staging or normal boot
validation. Regular boot keeps initialized globals alive.

The build manifest required for staging is
`build/baremetal/kernel-build-manifest.json`. Schema version 2 must
identify the build, Git commit, toolchain, configuration, target, architecture,
all three artifact paths, PE machine/subsystem/entry point, and each artifact's
SHA-256. Staging accepts only a manifest matching the current clean checkout
and the exact bootloader, modular kernel and display driver files that will be
installed. Staging must not recompile these files after validation. No
previous-build or monolithic fallback is allowed.

## C. Boot sequence

The intended boot sequence is:

```text
UEFI
  ↓
BOOTX64.EFI
  ↓
MicaNT Loader Parameter Block
  ↓
Modular Kernel Entry
  ↓
KiSystemStartup
  ↓
Phase 0
  ↓
Phase 1
  ↓
Executive
  ↓
Ring 3
```

The current bootloader is `boot/bootx64.cpp`; it locates
`\EFI\MicaNT\ntoskrnl.exe` and passes a versioned, size-checked
`UefiKernelHandoffV3` POD contract (120 bytes). The legacy STL loader block remains local
to the bootloader and is not passed across `ExitBootServices`. The handoff
contains a pointer and byte range for the final UEFI memory map, GOP metadata,
the ACPI root pointer, and the mapped display driver's base, size and entry.
It also carries an optional base and file size for a user PE staged from the
EFI System Partition. The bootloader probes `explorer.exe` before the existing
`app.exe` and `surshell_app.exe` fallbacks; the kernel validates the descriptor
and mapped range but does not yet load it as a process.
Its pointer fields are valid only while the
bootloader image and identity-mapped firmware memory remain available.

The current kernel entry validates the handoff, installs its page-table root,
validates and consumes conventional-memory descriptors through the canonical
PFN database, maps supervisor-only uncached MMIO and calls the separate display driver's
entry through the existing private `boot_driver` contract. This is not a WDK
driver-loading ABI or a complete I/O Manager. Desktop and cursor pixels are
presented through `DisplayDevice::PresentFrame`, not drawn into GOP memory.
Without VirGL, the driver explicitly reports 2D scanout only.

The HAL then installs GDT/TSS/IDT and executes a diagnostic at CPL3 through
`iretq`. A checked CPU-generated breakpoint frame verifies user execution and
return to Ring 0. Four further probes must raise checked page faults: reading
kernel memory, writing the user code page, executing the NX user stack, and
reading supervisor MMIO. The user mappings occupy a reserved, initially empty
PML4 slot, separate from the retained supervisor firmware mappings.

After these probes, a polled PS/2 loop moves a software cursor through the
display driver and logs left-button presses. This is **not** a running
`explorer.exe`: Start and windows are not wired to the Win32 message loop.
The existing Explorer application, Win32 DLL/server path, syscall dispatch,
PE/DLL process loading, allocator/runtime, interrupts and Executive scheduling
must still be integrated. There is no claim of an accelerated DWM session or
complete user-process isolation from these bounded diagnostics.

## D. Kernel subsystem ownership

| Resource / responsibility | Owner |
|---|---|
| Thread scheduling, dispatch, timers, and interrupt dispatch policy | `Ke` |
| Process and thread lifecycle | `Ps`, using `Ke` scheduling primitives |
| Address spaces, virtual memory, physical pages, and mappings | `Mm` |
| Kernel objects, namespace, references, and handles | `Ob` |
| Drivers, devices, IRPs, and I/O dispatch | `Io` |
| Registry configuration | `Cm` |
| Tokens and access control | `Se` |
| File-system runtime interfaces | `FsRtl` |
| Cache and cached file data | `Cc` |
| CPU registers, interrupt-controller hardware, timers, MMIO, and context-switch mechanism | `Hal` / architecture platform code |
| ACPI table mapping, CPU discovery, APIC and processor-local hardware state | `Hal` / AMD64 platform; executive policy stays in Ke/Io/Po |
| IOMMU unit programming, IOTLB and interrupt-remap mechanisms | `Hal`; DMA request ownership and buffers stay in Io/Mm |
| Device discovery, resource arbitration, IRP device-stack lifecycle and removal | `Io` (PnP manager) with bus drivers and `Ob` references |
| System/device power policy and transition orchestration | `Po`/`Io`; hardware-specific quiesce/restore remains in drivers/HAL |
| CPU scheduling, per-CPU queues, DPC/APC policy and timer objects | `Ke`; per-CPU hardware timer mechanism stays in HAL |
| Fatal exception capture, bugcheck coordination and crash records | `Ke` with HAL CPU/serial capture; dump output is a restricted crash backend |
| Loader handover and early initialization sequencing | `kernel/init/` |

Platform code implements hardware mechanisms; it must not become a second
owner of Executive state. The AMD64 implementation belongs in the existing
HAL/platform boundary. Host-only facilities belong in a test adapter and must
not be linked into the bare-metal image.

## E. Initialization and dependency rules

Initialization order is a dependency graph, not the order of files in a build
list. Early bootstrap APIs may be separate from full subsystem initialization
where a dependency requires it.

### Phase 0 — early kernel

1. Establish serial logging and CPU context.
2. Validate and consume the loader memory map; establish the initial allocator.
3. Establish kernel mappings and paging.
4. Install IDT/trap handling and the minimum HAL.
5. Initialize only the Executive bootstrap needed to continue.
6. Initialize the bounded freestanding heap/runtime before using facilities
   that depend on it.

### Phase 1 — Executive

1. Complete `Mm`.
2. Initialize `Ob`, dispatcher/scheduler primitives, and `Ps` in dependency
   order.
3. Initialize `Io`, `Se`, and `Cm`.
4. Initialize `Cc` and `FsRtl`.
5. Load boot-start devices.
6. Create the first Ring 3 process only when its loader and isolation path are
   implemented.

Each phase must propagate initialization failures. A stub that reports success
does not satisfy initialization.

Dependencies flow from higher-level Executive subsystems to lower-level
kernel/platform contracts through declared interfaces. Hardware details do
not leak into portable subsystem logic. User-mode components are not part of
the kernel image. Shared source may be compiled with separate host and
freestanding toolchains, but the tested subsystem implementation must remain
the same.

## F. Legacy policy and migration matrix

The archived `old/monolithic-kernel/ntoskrnl.cpp` is excluded from active
compilation, linking, staging, installation, and symbol resolution. It may be
consulted as historical MicaNT behavior and migration evidence, but it is not
a supported kernel or an alternate source of active symbols. Any retained or
adapted behavior must be reviewed and integrated into its canonical subsystem
owner; the archived kernel itself must never be restored as a second kernel.

| Legacy function area | Canonical destination | Current migration status |
|---|---|---|
| UEFI handover and loader data | `kernel/init/`, shared boot contract | Versioned POD handoff and bounded, strided memory map validated; usable pages in the first 256 MiB enrolled, higher RAM deferred explicitly |
| Serial debug | `Hal` / AMD64 platform | Bootstrap kernel initializes COM1 and emits verified serial milestones; HAL ownership is pending |
| IDT and exceptions | `Ke` + `Hal` / AMD64 | Diagnostic GDT/TSS/IDT and checked #BP/#PF returns verified in QEMU; general exception dispatch pending |
| APIC and timer | `Hal` + `Ke` | Headers/contracts exist; modular bare-metal initialization is not verified |
| Initial paging and PFN allocator | `Mm` + `Hal` mechanism | Canonical PFN allocator uses borrowed boot metadata and validated conventional ranges; allocation/reference/real-zeroing/contiguous-release verified; firmware mappings retained, full VM integration pending |
| AMD64 syscall dispatch | `Ke` + native API boundary | Protected hardware entry/return, user-buffer validation and private display/input services verified; Executive NT dispatch remains pending |
| Ring 3 transition | `Ps` + platform code | Hardware CPL3 entry/return and four page-protection probes verified; not a scheduled user process |
| PE image loading | Native loader + `Ps` | Bounded canonical PE view/module binding tested on host; mapped compiled diagnostic executes at CPL3 in QEMU; process/DLL initialization remains pending |
| AHCI, USB/HID, and boot storage | `Io` + canonical drivers + `Hal` | Driver headers/implementations exist; boot-start integration is not verified; the QEMU bootstrap separately polls PS/2 mouse packets for a software cursor |
| Display and shell input | Boot video + `Hal` / display driver | Separate VirtIO 2D driver presents shared desktop/taskbar and cursor; mouse movement and click delivery verified, but no Start/menu/window actions or user-mode Explorer session |
| Bare-metal Object Manager | `Ob` | Canonical global handle table initialized by the freestanding C++ lifecycle; bounded create/duplicate/reference/close accounting verified in QEMU; namespace and native `NtClose` integration remain pending |
| Bare-metal I/O Manager | `Io` | Host implementation exists; no freestanding IRP/device-stack integration; the display driver's private boot contract is not WDK loading |
| Runtime and idle loop | `Ex`, `Ke`, `kernel/init/` | PFN-backed allocation, global constructors, bounded LIFO destructors, and physical-page reclamation verified in QEMU; polls PS/2 with interrupts disabled; general scheduler and Executive idle thread remain pending |

This matrix is a migration inventory, not evidence that these functions
already work on hardware.

The per-translation-unit dependency audit is maintained in
[`FREESTANDING_GAP_MATRIX.md`](FREESTANDING_GAP_MATRIX.md). Verified integration
increments and their remaining prerequisites are recorded separately in
[`KERNEL_INTEGRATION_STATUS.md`](KERNEL_INTEGRATION_STATUS.md).

## G. Verification levels

These results are distinct and must be reported separately:

1. **Compilation** — a target compiles and links.
2. **Unit tests** — host tests exercise shared subsystem behavior.
3. **Kernel integration** — real kernel targets initialize together.
4. **QEMU/OVMF boot** — firmware loads and executes the modular bootstrap,
   which presents the desktop/taskbar through VirtIO, verifies CPL3 probes,
   and reaches its polled input loop.
5. **Hardware boot** — the kernel runs on identified physical hardware.
6. **Windows compatibility** — externally visible Windows contracts are
   validated against approved metadata and public documentation.

The existing host build failure on the current ARM64 macOS environment and
the stale, relocated `build/` cache are not evidence of a successful host
baseline. The QEMU smoke test builds into an isolated CMake cache, captures
serial output from the kernel itself, and requires driver, CPL3 and protection
milestones in order. The headless run also injects PS/2 movement and a click
through QMP, compares actual scanout pixels before/after movement, checks that
pixels outside the old/new cursor rectangles remain unchanged, and requires
the kernel's click diagnostic. This does not verify the full Executive or a
user-mode Explorer process.

Build and launch the visible QEMU session from the repository root with:

```bash
python3 qemu.py
```

The runner configures the cross build, produces and validates the kernel,
bootloader, display driver and manifest, then starts QEMU `q35` with one AMD64 virtual CPU,
OVMF pflash, `virtio-vga`, a read-only FAT ESP, and a snapshot. It uses a visible display by
default and leaves QEMU open after serial milestones pass. Use `--headless` for
the automated smoke test, `--ovmf-code <path>` to select firmware explicitly,
or `--cxx <path>` / `MICANT_CXX` to select LLVM-MinGW.

The standalone handoff layout/copy contract can also run on the host without
the full Executive test harness:

```bash
clang++ -std=c++23 -Iinclude test/uefi_handoff_contract_test.cpp -o build/uefi_handoff_contract_test
build/uefi_handoff_contract_test
```

## H. Real Explorer runtime closure

The actual shell and its canonical User32/GDI32/IMM32 -> Win32k chain can be
built with the same cross-toolchain in the isolated QEMU cache:

```bash
python3 qemu.py --check-explorer
python3 -m unittest discover -s test -p test_explorer_runtime.py -v
```

The first command deliberately does **not** launch QEMU or replace the
bootstrap desktop. It builds the actual sources, then checks PE imports
transitively against exports in `build/system32`, including ordinal imports
and forwarded exports. DLL matching is case-insensitive; CRT API-set names
are routed to `ucrtbase.dll`. Other API sets require their own contract and
are not guessed. The parser bounds-checks file-backed directory/string/table
ranges and rejects malformed images, missing exports and forwarder cycles.
The freestanding kernel/driver validator still forbids imports by default.
Passing this preflight would establish symbol closure only, not behavioral
compatibility, DLL initialization, isolation or a functioning user process.

The windowing DLL targets retain the toolchain's DLL startup entry rather
than overriding the PE entry with `DllMain`: their C++ static objects require
initialization before the user entry is called. No Microsoft DLLs are copied
into the build or used as a fallback.

The current preflight fails on native Kernel32 and UCRT requirements:

- Synchronization/TLS: critical sections, SRW locks, condition variables,
  `TlsGetValue`.
- Loader and virtual memory: module/procedure lookup, `VirtualProtect`,
  `VirtualQuery`.
- Time, conversion and exception/unwind support: real QPC/system time,
  character conversion, `RaiseException`, `Rtl*` unwind/context operations.
- C runtime: allocator and aligned allocation, memory/string functions,
  on-exit initialization, stream formatting, locale and conversion.

The existing full Kernel32 implementation contains simulated time/handles
and a single reused allocation arena; the UCRT wrapper forwards allocation
back into a hosted C runtime. Those implementations cannot be treated as a
native runtime just because their host tests pass. The separate freestanding
`kernel32_boot` target exports TEB-backed process/thread ID and last-error
accessors plus the bounded UTF-8 conversion APIs; it does not enable the
incomplete hosted candidate.

The separately selected `MICANT_FREESTANDING_UCRT` branch now builds a
no-import native memory/string subset from the same canonical source.
It has a bounded 64 MiB coalescing heap and real PE static TLS for `errno`;
the process loader must initialize that TLS before allocation is called.
It also exports tested freestanding `ceilf`, on-exit table, and initializer
dispatch routines, plus the active UTF-8 locale codepage and maximum multibyte
length queries. These do not replace full DLL CRT startup, which the kernel
loader does not yet perform. Aligned allocation and release use the bounded
arena; the CRT invalid-parameter handler, stdio and full locale configuration
and unwind contracts remain missing. `kernel32_boot` accesses the current TEB's process ID, thread ID and last-error
fields at compile-time-verified GS offsets. Its UTF-8 conversion exports
support the project's declared UTF-8 ACP and `CP_UTF8`, including strict
invalid-input flags, surrogate pairs, size queries and buffer/error checks.
They do not implement other Windows code pages. The conversion code is covered
by sanitized host tests; the DLL is linked but not called by a native process.
An isolated CPL3 probe verifies the TEB fields, but the native process loader
does not yet initialize a real process's TEB/GS. The last direct PE import
inventory found 23 missing Kernel32 exports and 21 missing UCRT exports; the
current aggregate build is rejected earlier by the boundary check. No success
fallback is introduced.

Native user-mode targets run a compiler-preprocessed dependency audit before
building. The current `win32k_boot`/`micant_win32k` implementation fails that
audit: `kernel/ps/winnt_win32k_server.cpp` includes
`micant/km/win32k/win32k.hpp`, which transitively pulls in Executive, MM, I/O,
driver and HAL headers. The check intentionally blocks the DLL and the
`explorer_runtime_boot` aggregate rather than treating this simulated user-mode
service as a kernel implementation. It must be integrated into the canonical
kernel owner and reached through the supported user-mode service boundary
before the Explorer DLL aggregate can build again.

Structural inspection of the archived standalone kernel identified its
loader/import/DLL-attach/syscall sequence, but also fixed-result/no-op import
trampolines. They were not copied, built or enabled as a workaround. Before
the actual Explorer can be launched, canonical runtime implementations,
bounded PE/DLL loading and initialization, process/TEB/TLS setup, validated
syscall buffers, input delivery and process lifecycle must be connected and
exercised. The previous CPL3/PS2/VirtIO smoke test remains a separate,
strictly smaller verification level.

### Native syscall boundary

The modular AMD64 bootstrap now installs `STAR`, `LSTAR`, `SFMASK` and
`EFER.SCE`. The entry uses `SWAPGS` and a supervisor-owned syscall stack;
it never calls C++ on the user stack. It preserves general registers and
XMM0-XMM15, reserves the Windows x64 home space, clears the direction flag
before C++, and returns through a checked `IRETQ` frame. RIP must name an
executable user page, RSP must name writable user memory, and privileged
RFLAGS are stripped. IRQs remain disabled: this is a single-CPU polled
bootstrap, not yet a preemptive Executive or a multi-process syscall engine.

The GUI helper, Explorer and Win32k use the same private service transport:
service number in RAX, first argument in R10, second in RDX. The service IDs
are internal transport identifiers, not declared Windows syscall numbers.
Shared graphics POD layouts are in `include/micant/shared/user_graphics.hpp`.

The connected services query the actual display, present user pixels through
the boot-start driver, poll PS/2 input into the existing Win32k event layout,
and write bounded diagnostic bytes to COM1. Buffer validation walks every
page and checks effective user/write/execute permissions at all four levels;
supervisor pointers, wraparound and partially mapped buffers fail explicitly.
Surface parameters and pixel extents are validated before any driver call;
pixels are copied into kernel-owned backing storage. Unsupported services
return `STATUS_NOT_SUPPORTED`, never success-shaped placeholders.

The QEMU smoke test now requires a real CPL3 syscall round trip. It verifies
register preservation, display dimensions, a zero physical framebuffer
pointer, read-only/supervisor/null/wrapping/cross-page output rejection,
bounded console output with the second argument, invalid surface rejection,
and an actual user-originated 2x2 surface submission. QMP checks the resulting
RGB `(17, 34, 51)` pixels at the scanout origin before testing mouse movement.
These diagnostics deliberately do not stand in for an Explorer process.

PE table validation follows the public
[Microsoft PE/COFF format specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format);
the CRT API-set preflight follows the
[Universal CRT deployment contract](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment).

## I. Reference implementation research and coverage inventories

The implementation method is **Study → Compare → Reuse → Redesign →
Modernize → Optimize → Validate → Integrate**. Microsoft documentation and
`win32metadata` establish the external Windows contract; ReactOS is a priority
behavioral and architectural reference for kernel NT, and Wine is useful for
user-mode behavior. A reference does not override the Microsoft contract or
the canonical MicaNT subsystem owner. Reuse or adaptation is limited to code
whose applicable terms permit it; otherwise, use the reference to inform a
MicaNT implementation.

[`NT_KERNEL_EXPANSION_MATRIX.md`](NT_KERNEL_EXPANSION_MATRIX.md) records
subsystem responsibilities, dependencies, tests, and current evidence-backed
MicaNT status. [`NT_ABI_STRUCTURE_MATRIX.md`](NT_ABI_STRUCTURE_MATRIX.md)
separates public and partially visible layouts from private internal
structures and tracks ABI assertions still needed. Both are inventories, not
claims of completed compatibility. The optional
[`scripts/nt_reference_inventory.py`](../scripts/nt_reference_inventory.py)
scans a local ReactOS checkout for candidate definitions, files, and tests;
its lexical results are discovery aids and require semantic review.

## J. Reliability and hardware enablement roadmap

These are planned extensions, not current architecture capabilities. Current bootstrap evidence is BSP-only and must not be read as SMP, system power management, IOMMU protection, general PnP, paging, high-resolution timekeeping, debugger/dump support or virtualization.

- **SMP:** ACPI MADT discovery and APIC programming are AMD64 HAL responsibilities. HAL establishes per-CPU execution context; Ke owns scheduling and interprocessor rescheduling; Mm owns address-space generations and TLB shootdown; Ps/Ob own lifecycle and references. AP online state is not an Executive process/thread abstraction.
- **DMA protection:** HAL implements vendor IOMMU units; Io owns mapping/request lifetime and Mm pins PFNs. DMA cancellation does not release pages until device access is quiesced and translation invalidated. Identity DMA and correct buffer lifetime are not hardware isolation.
- **PnP and power:** Io's PnP manager owns the single device tree, PDO/FDO/filter stacks, resources and IRP lifecycle. Po coordinates transitions; each participating driver quiesces and restores its own hardware. ACPI AML and tables are firmware inputs, not authority to bypass Io/Se access checks.
- **Crash and recovery:** Ke's fatal path uses preallocated per-CPU records and must not require normal allocation, held locks, scheduler operation, filesystem availability or compromised callbacks. Dump storage is a separate restricted backend; a crash stop is not general recovery.
- **Memory pressure and persistence:** Mm remains the sole owner of PFNs, working sets and paging. Cc/FsRtl/Io provide coherent file backing and durable flush semantics; FAT32's lack of journaling must not be presented as journaled crash consistency.
- **Time:** HAL provides clocksource/clockevent mechanisms; Ke owns monotonic deadlines and timers. Wall-clock corrections and RTC time cannot move monotonic waits backward.
- **Virtualization:** a future manager may compose existing Mm/Ke/HAL/IOMMU services only after those foundations pass their gates. It is not a prerequisite for the first usable kernel.

Detailed phase gates and acceptance evidence are linked from [KERNEL_MILESTONES.md](KERNEL_MILESTONES.md) and its M19–M27 specifications. No planned capability is promoted to present support without an owner-connected implementation and the corresponding QEMU/physical evidence.
