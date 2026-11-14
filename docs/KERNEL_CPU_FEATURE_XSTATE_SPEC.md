# MicaNT — AMD64 CPU Feature Discovery and XSTATE Specification

**Status:** Proposed engineering specification; implementation and acceptance are not implied  
**Platform:** AMD64 / x86-64, freestanding C++23  
**Canonical owners:** HAL (discovery, control registers), Ke (per-thread extended state and scheduling), Ps (thread lifecycle), Mm (state allocation), Se (privileged exposure)  
**Related:** `AGENTS.md`, `docs/KERNEL_MILESTONES.md`, `docs/KERNEL_INTEGRATION_STATUS.md`, `docs/KERNEL_SCHEDULING_RESPONSIVENESS_SPEC.md`

## 1. Objective and scope

Allow native user-mode PE applications to use genuinely supported processor instructions, with no per-operation syscall, while MicaNT enables and preserves the necessary architectural state correctly. Begin with SSE/AVX/AVX2; extend to AVX-512 and AMX only on CPUs and emulators that expose all required facilities. Feature reporting must distinguish *hardware advertised*, *OS enabled*, and *safe for this thread/processor set*.

This is not a general compute engine or a device-driver catalog. Ordinary CPU instructions execute in Ring 3; the kernel owns capability discovery, privileged enablement, thread state and exception handling.

## 2. Repository starting point

Relevant existing paths to inspect, not assume complete: `include/micant/km/core/ke.hpp`, `kernel/hal/amd64_bootstrap.cpp`, `kernel/ps/address_space.cpp`, `include/micant/km/drivers/compute/amx.hpp`, `drivers/amx/`, and the current AMD64 context-switch implementation. The imported AMX driver directory is a hosted model in the documented driver integration inventory; it is not proof of freestanding AMX support. Verify the *current worktree* before coding.

Do not introduce a competing HAL, context switcher or XSTATE manager. Do not regress the already-tested CPL3/SYSCALL/GS paths. Do not import hosted C++ runtime dependencies into Ring 0.

## 3. Capability model

For each logical processor record an immutable-after-onlining capability snapshot (with controlled refresh on hotplug):

- CPUID maxima and relevant leaves/subleaves: baseline, structured features, topology, xsave and extended AMD leaves as appropriate.
- Features directly usable without extra OS state, such as selected AES/PCLMUL/SHA instructions, separately from policy-disabled features.
- OSXSAVE support, XSAVE-family instructions, XSAVE area sizes/layout and XSTATE component offsets/alignment.
- XCR0 enabled bitmask, and supervisor-state support (IA32_XSS where supported) without treating hardware presence as enablement.
- Required feature dependencies: e.g. AVX requires x87/SSE/YMM state and OSXSAVE; AVX-512 additionally requires opmask and upper-ZMM components; AMX additionally requires tile configuration and tile-data support and a valid per-thread allocation/permission policy.
- CPU core topology, core type and feature heterogeneity. A system-wide common denominator must be reported separately from optional per-core features.

Unrecognized bits must be ignored or explicitly rejected, not arbitrarily enabled. CPUID is advisory: successful control-register configuration and runtime probes are independent verification steps.

## 4. Privileged enablement

HAL owns CR0/CR4, OSXSAVE, XCR0 and relevant MSR setup. Set only documented combinations, check dependency masks and account for CPUs lacking a given XSTATE component. Never execute XSETBV with unsupported bits. Enable features before publishing them to user mode; preserve architectural rules for initialization, interrupt masking and per-CPU bring-up. State configuration must remain coherent across active processors.

Use correctly aligned, size-bounded XSTATE buffers. Define exact allocation and failure semantics for large states (especially AMX tile data). No arbitrary large allocation on interrupt, syscall exit or thread context-switch fast paths.

## 5. Thread lifecycle and context switching

The canonical Ke/Ps thread object owns a reference to its state and the policy for allowed features. Specify:

1. Initial architectural state and zeroing (avoid cross-process register-data leakage).
2. State-save/restore selection (FXSAVE/FXRSTOR versus validated XSAVE-family path) and fallback.
3. User/kernel context transitions, signal/exception dispatch, APC, nested interrupt and migration behavior.
4. State release after thread termination and cancellation, without freeing buffers still in use.
5. Affinity constraints if a thread uses a feature unavailable on some CPUs; reject impossible migrations or constrain placement explicitly.
6. AMX per-thread opt-in/permission if the chosen public/compatibility contract requires it; report an explicit failure when unsupported.

A user-triggered `#UD` for an unavailable instruction is not proof of working feature detection; exception dispatch must report the correct user exception instead of crashing the kernel.

## 6. User-mode discovery and compatibility

Preserve the processor's real CPUID visibility unless there is an explicitly documented guest/sandbox virtualization policy. Supply system/processor feature reporting through the **existing appropriate NT/Win32 contracts**, where implemented, and never invent public Windows function names. Libraries may perform their own CPUID dispatch, but must not select instructions that the OS has not enabled.

No API should claim AVX-512 or AMX merely because a header, sample or hosted test mentions them. ISA accelerated cryptographic primitives belong to user-mode libraries except for narrow kernel-owned, audited cryptographic functions.

## 7. Reliability and security

- Bound all XSTATE offsets, masks, sizes and alignment calculations against integer overflow.
- Scrub reused sensitive state; test thread and process separation.
- Do not leak physical/virtual addresses or privileged capability tokens into untrusted clients.
- Handle processor mismatch, CPU hotplug, unsupported features, OOM and cancellation with explicit rollback.
- Benchmark eager versus lazy state allocation and do not assume that lazy FP switching is safe or appropriate.
- Ensure user-originated malformed contexts cannot set supervisor state or privileged register values.

## 8. Phases and acceptance

| Phase | Required implementation | Exit evidence |
|---|---|---|
| C0 — Inventory | Locate canonical CPU/context paths, identify existing enabled XSTATE | Source/ABI mapping, no duplicated subsystem |
| C1 — Discovery | Per-CPU validated CPUID feature records; hardware vs OS-enabled distinction | Hosted synthetic CPUID tables + target queries |
| C2 — Baseline XSTATE | XCR0 policy; aligned XSAVE state; switch/interrupt/termination handling | QEMU user-mode SIMD preservation and isolation |
| C3 — Wide vectors | AVX2 and supported AVX-512 components and migration policy | Numeric tests with repeated preemption/switches; unsupported tests explicitly skipped |
| C4 — AMX | Tile state sizing, opt-in and safe lifecycle | Tests on capable emulator/hardware, not claimed if unavailable |
| C5 — Performance | Measure context-switch/state memory overhead, feature dispatch | Before/after metrics with no unexplained regression |

### Required tests

- Test real user PE programs doing scalar, SSE/AVX2, AVX-512 and AMX computations when supported; compare numeric outputs to reference values.
- Alternate two threads using different vector state, under timer preemption and voluntary switches; check zero cross-thread contamination.
- Vary XSTATE area size and alignment; inject allocation failures and thread termination during state creation.
- On QEMU, distinguish emulated CPU feature availability from actual tested operations; do not claim physical CPU support from QEMU alone.
- Repeat on 1, 2, 4 vCPUs **after** SMP has an independently verified baseline.
- On physical hardware, record CPU vendor/family/model, microcode, feature mask, XCR0 and selected implementation paths without exposing sensitive identifiers in public logs.

## 9. Out of scope

A new SIMD compiler, codec implementation, general AI runtime, kernel-mode media decoder, standalone AMX `.sys` that duplicates Ke ownership, and unsupported ARM64 architecture work.

## 10. Definition of done

The canonical kernel has an accurately reported, safely enabled AMD64 feature set, preserves permitted extended register state across thread/interrupt/process transitions, rejects unsupported features safely, and executes real Ring 3 numerical tests on the claimed hardware configurations. Compilation and hosted emulation alone are insufficient.
