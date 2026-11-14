# Running MicaNT in QEMU on Apple Silicon (macOS AArch64)

This guide documents the behavior, performance characteristics, and known artifacts when running the AMD64 MicaNT kernel inside QEMU on Apple Silicon hosts (M1/M2/M3/M4 macOS).

---

## 1. Architectural Constraints

1. **Target Architecture**: MicaNT is an AMD64 (`x86_64`) operating system kernel. It strictly uses AMD64 hardware structures:
   - 4-level paging (`PML4` -> `PDPT` -> `PD` -> `PT`) with x86 NX and supervisor bits.
   - AMD64 Segment Descriptors: GDT, TSS, IDT, and Task State Segment.
   - Hardware `SYSCALL`/`SYSRET` MSRs: `0xC0000080` (`IA32_EFER`), `0xC0000081` (`IA32_STAR`), `0xC0000082` (`IA32_LSTAR`).
   - x86-64 System V / Microsoft x64 calling conventions and APIC interrupt delivery.

2. **Apple Hypervisor (`hvf`) Incompatibility**:
   - Modern Macs run on Apple Silicon, which uses ARM64 (`AArch64`) architecture.
   - macOS `Hypervisor.framework` (`-accel hvf`) only accelerates **virtualization of the host architecture** (ARM64 guests on ARM64 hosts). It cannot virtualize an x86_64 guest on an ARM64 host.

3. **Software Emulation (`-accel tcg`)**:
   - On an Apple Silicon Mac, `qemu-system-x86_64` must run with QEMU's Tiny Code Generator (`-accel tcg`).
   - TCG interprets and JIT-translates every x86_64 instruction into ARM64 instructions entirely in user-space software.

---

## 2. Known TCG Emulation Artifacts & Failure Modes

When developing or debugging kernel code under `-accel tcg`, be aware of the following emulation-specific phenomena:

### A. Execution Throughput (10x–30x Slowdown)
- Software instruction translation has high overhead.
- Operations that take milliseconds on bare-metal or KVM can take multiple seconds under TCG.
- Ensure test harnesses use extended timeouts (`--timeout 120` or higher) to avoid false timeout failures.

### B. Clock & Timer Calibration Skew
- MicaNT calibrates its BSP scheduler clock against the 8254 PIT, TSC, and LAPIC timers.
- Under TCG, cycle-count progression (`RDTSC`) does not match wall-clock time. Software JIT bursts cause clock drift and jitter.
- Timer wait loops and sleep tests can report unexpected variances that are artifacts of host JIT execution rather than kernel timer logic.

### C. Memory Ordering & Atomic Emulation
- AMD64 hardware provides **Total Store Order (TSO)** with strong memory guarantees.
- ARM64 hardware provides **Weak Memory Ordering (WMO)**.
- While QEMU inserts memory barriers to emulate x86 ordering, high-frequency spinlocks, lock-free queues, and interlocked atomic instructions (`lock cmpxchg`) incur additional latency and may expose subtle timing races under multi-thread emulation.

### D. Interrupt Delivery Latency
- Emulated hardware interrupts (such as xHCI MSI/MSI-X and virtio-net queue notifications) are injected asynchronously by QEMU's main event loop.
- Under heavy TCG load, interrupt delivery can be delayed by several hundred milliseconds, leading to apparent "missed interrupt" false positives in tight acceptance tests.

---

## 3. Recommended Development Workflows

For production development and hardware driver validation:
- **Native x86_64 Hardware**: Run directly on physical x86-64 hardware (MicaNT boots via standard UEFI GOP and AHCI/NVMe).
- **Hardware-Accelerated KVM**: On an x86-64 Linux host, launch QEMU with `-accel kvm -cpu host` for near-native performance and cycle-accurate APIC timing.
- **Apple Silicon Development**: Use QEMU with `-accel tcg` for initial compilation checks and functional smoke tests, keeping in mind that timing, scheduling latency, and interrupt responsiveness should be evaluated against TCG latency tolerances.
