# MicaNT — AMD64 Graphics Driver Architecture and Implementation Specification

**Document:** `docs/KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`  
**Status:** Proposed implementation specification — not proof of hardware support  
**Scope:** MicaNT modular NT-compatible kernel, AMD64/x86-64 only, C++23, UEFI  
**Primary owners:** HAL, Io/PnP, Mm, Ke, graphics kernel/Win32k and user-mode graphics runtime

## 1. Objective and non-goals

Implement a reliable graphics stack that works first on the existing QEMU VirtIO-GPU platform and on a broad range of UEFI AMD64 machines through a GOP framebuffer fallback, then progressively supports accelerated Intel, AMD and NVIDIA devices through **native MicaNT miniports**. Reuse Linux driver documentation, hardware knowledge and suitable components only where their technical and licensing dependencies are understood. Preserve the Windows-facing API and kernel/user-mode boundary rather than treating Linux DRM `.ko` binaries as NT drivers.

**Required outcomes:** boot-time display and diagnostic console; a usable software-composited desktop on supported firmware framebuffers; device discovery and device-owned scanout; reliable buffer ownership and synchronization; an incremental path to hardware-accelerated rendering. Hardware acceleration, WDDM compatibility and arbitrary GPU generations are **not** implied by framebuffer functionality.

**Non-goals for initial phases:** loading Linux kernel modules directly; implementing all of Linux DRM/KMS; implementing full WDDM/DXGKRNL, Direct3D 12 or arbitrary Mesa backends; supporting every Intel/AMD/NVIDIA SKU; treating firmware GOP as a 3D API; bypassing Mm/Iommu/Io ownership rules for performance.

## 2. Current MicaNT baseline and evidence levels

The fork already has an AMD64 UEFI handoff and a separate VirtIO-GPU boot-start driver (`drivers/virtio_gpu/virtio_gpu.cpp`) with QEMU 2D scanout and software desktop/input diagnostics. Graphics-related code also exists under `kernel/win32k/`, user-mode graphics components and other subsystem headers. The existing tests establish a **limited bootstrap display path**, not an accelerated desktop session or native Intel/AMD/NVIDIA driver.

Before coding, verify the current revision, `CMakeLists.txt` sources and `docs/KERNEL_INTEGRATION_STATUS.md`. Report each feature distinctly as *declared*, *compiled*, *linked*, *initialized*, *exercised in QEMU*, or *validated on physical hardware*. A file name, WDDM-like class, supported PCI identifier, or API export alone does not prove device support.

## 3. Architectural decision

Keep **one canonical graphics kernel service** and a narrow miniport interface. A platform-independent display manager must select a device and backend, own the framebuffer or scanout surface, and communicate with user-mode presentation code. HAL owns CPU-visible mapping, PCI configuration, interrupt vectors and platform facilities; Mm owns virtual mappings and page pinning; Io/PnP owns device lifecycle and requests; Ke owns synchronization and DPCs. No alternate process scheduler, memory manager or filesystem layer may be introduced by GPU code.

```text
User application / Window manager / Compositor (Ring 3)
                 |
       Win32k / graphics client ABI
                 |
      MicaNT graphics kernel service
                 |
    Native display / GPU miniport contract
        /              |                 \
   GOP framebuffer  VirtIO-GPU       Vendor miniport
   (fallback)       (QEMU)          Intel / AMD / NVIDIA
        \              |                 /
        HAL + Io/PnP + Mm + Ke + DMA/IOMMU
                 |
            AMD64 hardware
```

**GOP ownership:** firmware initializes video mode before `ExitBootServices`. The bootloader passes a validated descriptor (physical base, dimensions, stride, pixel format, size) to the kernel. After exit, the OS must not call GOP boot services. Preserve and map the framebuffer with correct memory attributes; do not assume its memory can be freed, cached as ordinary RAM, or reprogrammed without a native driver.

**Driver precedence:** explicitly configured backend → initialized vendor miniport with functioning scanout → VirtIO-GPU where present → valid GOP framebuffer → serial-only recovery. Fallback must remain available if a vendor miniport fails after initialization, provided it does not corrupt the framebuffer or firmware ownership.

## 4. Miniport ABI and lifetime

Define a **versioned, private C-compatible POD** boundary suitable for kernel/driver module linkage. Place mode-neutral structures only in `include/micant/shared/` if genuinely needed across a binary boundary; private implementation belongs under `include/micant/km/`. Do not expose kernel private headers to user-mode DLLs.

Minimum operations, with explicit success/failure and device context:

- `Probe` and `StartDevice`: detect supported PCI IDs, obtain/validate BAR and interrupt resources, initialize device state.
- `EnumerateModes` and `SetMode`: report only actual modes; a GOP backend may legitimately support only its firmware-selected mode.
- `AcquireScanout` / `Present`: surface dimensions, format, pitch and bounded update regions; process asynchronous completion when supported.
- `CreateBuffer` / `DestroyBuffer`, `MapBuffer` / `UnmapBuffer`: ownership, page alignment, pinned backing and coherent mapping rules.
- `Submit` and `WaitFence`: **optional** acceleration extension, not a requirement for baseline display; bounded, validated submissions.
- `HandleInterrupt` / `ScheduleDpc`: acknowledge interrupts and queue deferred work without sleeping in ISR context.
- `Quiesce`, `Reset`, `StopDevice`, `RemoveDevice`: deterministic cleanup on normal shutdown, device failure and partial initialization.

Suggested state machine: `Discovered → ResourcesAssigned → Initialized → ScanoutReady → Running → Quiescing → Stopped`, plus `Failed`. The system must reject invalid transitions. Each resource (BAR mapping, DMA object, IRQ registration, framebuffer surface, firmware image reference) has one owner and a defined release order. Failed initialization must roll back every acquired resource.

**ABI requirements:** fixed-width types, size/version validation, explicit calling convention, no STL types across driver module boundaries, no exceptions across boundaries, no raw user-mode pointer access in miniports. Maintain compatibility with existing boot-driver ABI until migration is explicit and tested.

## 5. Boot display: GOP framebuffer (Phase G0)

1. Validate the UEFI framebuffer handoff: physical address overflow, total byte range, `pixelsPerScanLine >= width`, supported pixel layout, memory descriptors, framebuffer length, and absence of reserved/RAM ownership confusion.
2. Create a HAL/MM mapping using an appropriate write-combining or uncached policy as permitted by hardware/UEFI descriptors and PAT/MTRR; avoid conflicting cache aliases.
3. Implement software `Clear`, `BlitRect`, `PresentDirtyRects` and stride-aware pixel conversion for supported RGB/BGR firmware formats. Respect bounds and integer overflow.
4. Implement a permanent boot console/panic rendering path with minimal allocation and without waiting on a user process.
5. Keep a read-only capability report: fixed mode, scanout only, no GPU command queue, no acceleration, no rotation or multiple monitors unless explicitly implemented.
6. Ensure the kernel can hand off presentation ownership to the full compositor without losing the framebuffer or double-mapping its physical pages.

**G0 acceptance:** on at least two distinct UEFI AMD64 configurations, actual framebuffer writes produce correct pixels after `ExitBootServices`; invalid handoff and mapping errors fall back to serial diagnostics; QEMU OVMF smoke validates modes, stride, colors, bounded writes and normal boot. Physical-machine tests are needed before claiming generic physical hardware support.

## 6. VirtIO-GPU as integration reference (Phase G1)

Retain the existing VirtIO-GPU driver rather than replacing it. Consolidate its scanout/presentation path behind the native miniport interface, preserving existing QEMU serial, PS/2 and pixel probes.

- Verify PCI enumeration, virtqueue descriptor ownership, feature negotiation, resource creation, scanout configuration and completion fences.
- Enforce bounded guest memory accesses, well-formed command buffers, error handling and deterministic teardown.
- Establish a single source of truth for front/back buffers and composition surfaces; do not allocate unmanaged shadow buffers per subsystem.
- Make presentation asynchronous where supported; guard against a hung device using bounded wait and fallback/recovery.
- Run both regular scanout and headless QMP pixel checks; do not call VirtIO 2D support VirGL or accelerated Vulkan unless separately verified.

**G1 acceptance:** uninterrupted normal boot and input tests; dirty-rectangle presents, mode negotiation, invalid resource rejection and recovery from simulated device timeouts; no leaked DMA/pinned pages after driver unload.

## 7. PCI, MMIO, DMA, interrupts and firmware foundations (Phase G2)

### 7.1 PCI/PnP

Enumerate PCI functions by segment/bus/device/function and validate class/subclass, vendor/device/revision, BAR sizes/types and command bits. Discover capability chains and optional MSI/MSI-X; never trust a BAR address or length before resource validation. Track device state in Io/PnP, not in a separate GPU-specific PCI manager.

### 7.2 DMA and IOMMU

Create a HAL/IOMMU DMA API with explicit device-domain ownership, address translation, buffer pinning, direction, cache synchronization and scatter/gather lists. Without a validated IOMMU, use conservative constrained/bounce buffers rather than mapping arbitrary kernel memory into device-accessible ranges. For GPUs requiring GPU virtual memory, keep GPU-VA management distinct from CPU VADs while still using canonical Mm to own physical pages.

### 7.3 Interrupt handling

Support MSI/MSI-X or validated INTx routing; ISR performs acknowledgement and minimal queueing only. Completion processing occurs via Ke DPC/work items. Interrupt storms and device timeouts must be bounded and diagnosable. Never hold locks across GPU waits or call pageable paths at incompatible IRQL.

### 7.4 Firmware

Determine each hardware generation's required firmware (e.g., AMD microcode packages, Intel GuC/HuC where needed, NVIDIA GSP). Record firmware origin, format, required version and licensing/distribution constraints. Failed firmware load must not silently report an accelerated device.

**G2 acceptance:** verified resource lifecycle on QEMU-emulated PCI hardware first; explicit injected failures for BAR mapping, DMA allocation, IRQ installation and device reset; physical validation for each selected device before claiming vendor support.

## 8. Vendor miniport strategy (Phase G3)

Port **one well-defined GPU family per batch**; require an exact PCI ID/generation matrix and documented minimum capabilities. Do not implement a family-wide `supported=true` before a specific device has passed actual scanout.

| Family | Linux technical references | Minimum MicaNT target | Major dependency |
|---|---|---|---|
| Intel integrated / Arc | `i915`, `xe`, relevant display engine and Mesa `iris`/`anv` paths | modeset + native scanout on one identified generation | display registers, firmware, GGTT/PPGTT or Xe VM |
| AMD Radeon / ATI | `amdgpu`, legacy `radeon` as separate line; Mesa RADV | modeset + native scanout on one identified RDNA generation | DC/display engine, GPU VM, firmware, ring and fence |
| NVIDIA | `nouveau` and NVIDIA open GPU kernel modules as reference | modeset + scanout on one explicitly supported generation | GSP/firmware, proprietary user-mode constraints, GPU VM |

A direct Linux `.ko` cannot be loaded in MicaNT. Do not wholesale import Linux DRM/KMS, Linux kernel allocators, locking APIs, task model, or GPL-derived code without a deliberate license/provenance decision. Favor small hardware-specific native implementations against the MicaNT ABI. The feasibility of adapting source is **per file, version and license**; never assume NVIDIA open kernel modules include redistributable firmware or provide a complete user-mode stack.

**G3 acceptance for each device:** hardware ID match, resource map, reset/initialization, one valid mode, scanout update, interrupt or polling-completion behavior, timeout recovery and cleanup demonstrated on that physical GPU; published evidence identifies machine and firmware versions.

## 9. Acceleration and Mesa integration (Phase G4)

Only after scanout, DMA and synchronization are stable:

1. Implement GPU buffer objects, VRAM/GTT residency, GPU virtual-address mappings and valid fault/reset behavior where hardware supports them.
2. Implement bounded command submission, queues, fences, timeline/synchronization semantics and a watchdog/TDR-like recovery path.
3. Enforce per-process GPU address-space isolation and kernel validation of every submitted object and range. A user-mode process must not submit unvalidated MMIO/register writes.
4. Select **one** Mesa backend (for example `anv` for a supported Intel device or RADV for supported AMD hardware) and audit its actual DRM ioctl dependencies. Either adapt those calls to a narrow native MicaNT service, or implement a strictly scoped compatibility adapter within user mode/kernel client boundaries.
5. Produce an independent Vulkan conformance/evidence plan. Direct3D/DirectX compatibility is a separate user-mode project: exposing interfaces is not proof of hardware execution.

Mesa porting depends on matching compiler ABI, loader/runtime, threading, TLS, memory mapping, dynamic libraries and synchronization. A port that links but never submits real commands is only a build milestone.

**G4 acceptance:** render a deterministic GPU-computed frame to a real device-owned surface, verify fence completion, test memory isolation across two processes and recover from forced queue/device failure. Publish real device and API coverage; no blanket `D3D12 supported` claim.

## 10. Desktop responsiveness and presentation

Keep the compositor in user mode once Ps/NTDLL/Win32k startup exists. Applications render into independently owned surfaces; the compositor schedules presentation on a short bounded frame budget without waiting on a stalled application. Input is collected through an interrupt-driven path and passed through message queues.

Use the canonical 32-level NT scheduler with dynamic interactive boosts and priority decay rather than inventing a second GPU scheduler for CPU threads. Keep the GPU's hardware command scheduler separate from Ke's CPU thread scheduling; connect completion via fence/event objects and wakeups. A slow application's last complete surface should remain compositable.

Metrics (design targets, not current achievements): at 60 Hz, frame budget about 16.7 ms; initial P95 input-to-ready UI thread under 5 ms under specified CPU load; track missed frames, queue latency, dropped input events, GPU reset and memory pressure. QEMU TCG is not a reliable predictor of physical GPU performance.

## 11. NT compatibility and security

- Preserve Windows user/kernel boundaries and documented exports. Private miniport APIs are internal; user-mode clients use approved NTDLL/Win32k/graphics services.
- GPU and CPU memory ownership must remain consistent across Ob handles, Mm PFNs/VADs, Io device objects and per-process lifetimes.
- Validate every descriptor, buffer size, stride, command length, scatter/gather range, firmware size, pointer, alignment, mode and overflow.
- Reset, device removal, process termination and failed submission must reclaim GPU objects without freeing buffers still referenced by outstanding fences.
- Support IRQL-correct synchronization, no user callbacks under kernel locks, no pageable memory access at device interrupt level and no deadlocks during TDR/recovery.
- Handle system shutdown, sleep/resume and device loss only when the corresponding HAL/Po/Io lifecycle exists; otherwise return explicit unsupported status.
- Do not substitute hardcoded capability reports, CPU software rendering, or host Mesa calls for actual hardware acceleration claims.

## 12. Build and verification discipline

Development is phased and incremental. Respect current user instructions regarding tests and commits. Unit tests cover parsers, modes, overflow/limits, state transitions and rollback; QEMU verifies actual VirtIO scanout and compositor behavior; hardware tests are required for Intel/AMD/NVIDIA acceleration.

**QEMU test matrix:** OVMF GOP fallback; VirtIO-GPU 2D; invalid/absent GPU; display reset; reduced guest RAM; failed DMA allocation; interrupt timeout; page protections; QMP pixel comparisons; normal runtime and cleanup self-tests as appropriate.

**Physical GPU matrix:** exact board/PCI ID, firmware/UEFI build, CPU/chipset, IOMMU configuration, monitor connection and resolution; scanout; hotplug where supported; GPU allocation/fence; process isolation; suspend/resume only when implemented; recovery from device error. Preserve serial logging for failures with no video output.

For every check record: command, build SHA, hardware/QEMU configuration, observed result, unsupported conditions and clean-up status. A passing host test never substitutes for QEMU or physical device verification.

## 13. Integration phases and exit criteria

| Phase | Deliverable | Exit criterion |
|---|---|---|
| G0 | UEFI GOP framebuffer backend | Correct framebuffer after `ExitBootServices`; serial fallback; physical validation |
| G1 | Canonical VirtIO-GPU miniport integration | Existing QEMU tests preserved; present/fence/cleanup validation |
| G2 | PCI/MMIO/DMA/IRQ resource foundation | Resource ownership, protection and rollback exercised |
| G3-I | Native Intel scanout for one generation | Real Intel device displays content; reset/cleanup |
| G3-A | Native AMD scanout for one generation | Real AMD device displays content; reset/cleanup |
| G3-N | Native NVIDIA scanout for one generation | Real NVIDIA device displays content; reset/cleanup |
| G4 | First accelerated Mesa/Vulkan backend | Real GPU command execution, fence and cross-process isolation |
| G5 | Native NT graphics compatibility expansion | Tested subset of DXGI/Win32k/WDDM contracts, not nominal export count |

The order of G3 vendor phases may change based on available real test hardware. G0/G1 can progress while Mm/Io/Ps are being completed, but G2–G4 depend on mature physical memory, device lifecycle and process isolation. These phases must **not block completion of the current MicaNT M4 process lifecycle or filesystem-backed `SEC_IMAGE` work**.

## 14. Repository implementation map

Inspect and reuse rather than duplicate:

- `boot/bootx64.cpp` and the versioned UEFI handoff: framebuffer descriptor transport.
- `kernel/init/kiinit.cpp`: initialize the selected display backend after HAL/Mm startup.
- `drivers/virtio_gpu/virtio_gpu.cpp`: current VirtIO-GPU backend and QEMU evidence.
- `kernel/hal/` and `include/micant/km/hal/`: PCI, MMIO, interrupt and DMA integration points.
- `kernel/mm/virtual.cpp` and `include/micant/km/executive/mm.hpp`: validated mapping and page ownership.
- `kernel/io/iomgr.cpp` and `include/micant/km/executive/io.hpp`: device/PnP/IRP lifecycle.
- `kernel/win32k/` and existing user-mode graphics APIs: presentation/window-system boundary.
- `include/micant/shared/boot_driver.hpp`: existing boot-driver ABI to preserve or explicitly version.
- `qemu.py`, `scripts/boot_micant.py`, and `docs/KERNEL_INTEGRATION_STATUS.md`: current boot and visual verification.

New files should be introduced only where a clear owner exists (e.g., HAL GOP map backend, Io display class, vendor-specific miniport). Do not create a second graphics kernel service or compile kernel-private headers into user-mode graphics DLLs.

## 15. Research and external references

Use primary upstream documentation relevant to a selected device and feature, with revision pinning:

- Linux DRM/KMS documentation: <https://docs.kernel.org/gpu/index.html>
- Linux amdgpu driver documentation: <https://docs.kernel.org/gpu/amdgpu/index.html>
- Linux i915 and xe: <https://docs.kernel.org/gpu/i915.html>, <https://docs.kernel.org/gpu/xe/index.html>
- Linux nouveau: <https://nouveau.freedesktop.org/>
- NVIDIA open GPU kernel modules: <https://github.com/NVIDIA/open-gpu-kernel-modules>
- Mesa 3D: <https://docs.mesa3d.org/>
- Microsoft WDDM driver model: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- UEFI specification, GOP protocol and `ExitBootServices`: <https://uefi.org/specifications>
- VirtIO GPU specification: <https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html>

Technical reference does not imply permission to import code. Evaluate licenses and firmware terms at the actual file/package level. Where external documentation differs from the selected device's revision, record the exact supported hardware and revision-specific behavior.

## 16. Final definition of done

An implementation milestone is **complete** only when its entire stated end-to-end path works under the required evidence level. A header, a simulator, a compiled `.sys`, a host unit test, a QEMU bootstrap image, and a real physical GPU driver are distinct results. Preserve those distinctions in status reporting. Continue work on the next dependency within the authorized scope; do not stop after an isolated helper, a small test or a commit unless the actual task is complete or the execution environment imposes a limit.

### PnP and DMA prerequisites

Hardware driver lifecycle and resource teardown follow [M23 PnP](KERNEL_PNP_DRIVER_LIFECYCLE_SPEC.md). GPU DMA buffers must use the canonical Io/Mm ownership contract; hardware isolation claims require the separate IOMMU gates in [M22](KERNEL_IOMMU_DMA_PROTECTION_SPEC.md). The existing VirtIO-GPU bootstrap scanout evidence does not imply either capability.
