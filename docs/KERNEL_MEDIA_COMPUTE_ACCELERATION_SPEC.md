# MicaNT — Media and Compute Acceleration Specification

**Status:** Proposed  
**Owners:** GPU/audio/accelerator device drivers (Ring 0); Media Foundation, DXVA/D3D12 Video, PrismAudio, AI and crypto runtimes (Ring 3)  
**Related:** `docs/KERNEL_GPU_DRIVER_IMPLEMENTATION_SPEC.md`, `docs/KERNEL_AI_INFERENCE_IMPLEMENTATION_SPEC.md`, `KERNEL_CPU_FEATURE_XSTATE_SPEC.md`, `KERNEL_HARDWARE_ACCELERATOR_FRAMEWORK_SPEC.md`

## 1. Objective

Allow applications to select a **verified** software or hardware path for media processing and computational workloads. CPU SIMD acceleration runs directly in Ring 3; dedicated video engines, GPU/NPU/DMA/cryptographic accelerators require real canonical drivers with validated memory and completion semantics. Media parsing, demuxing, codecs, inference graphs and application policies stay out of Ring 0.

Do not equate a public API/header or a GPU model with working hardware decoding, encoding or AI acceleration.

## 2. Starting inventory

Inspect `include/micant/um/subsystems/graphics/{dxva2,d3d12video}.hpp`, `include/micant/um/subsystems/media/{mfplat,mfreadwrite,wasapi,prismaudio}.hpp`, `include/micant/km/drivers/video/{dxgkrnl,wddm}.hpp`, `drivers/virtio_gpu/`, `include/micant/km/drivers/compute/{npu,qat,dsa}.hpp`, `drivers/{npu,qat,dsa}/`. The verified VirtIO 2D display path is **not** a proof of video decode acceleration. Imported driver headers tested hosted are not functioning bare-metal hardware devices.

## 3. Pipeline and ownership

**Ring 3:** container/file parsing, codec library dispatch, pixel/sample conversion, runtime sessions, decoder configuration, certified model/operator parsing, output verification, secure resource ownership. Favor mature libraries where licensing and freestanding Windows ABI support are viable.

**Ring 0:** hardware discovery, command/descriptor validation, MMIO, DMA/IOMMU and shared buffer lifetime, fences/interrupts, queue scheduling, reset, device loss and privileged firmware interaction.

**Cross-boundary:** versioned, bounded buffers and capability profiles; handle-based ownership; explicit supported formats and errors. Preserve user/kernel include boundary rules.

## 4. Hardware capability negotiation

For each device expose *actual driver-enabled* profiles: decode vs encode, codec/profile/level, chroma format, bit depth, maximum resolution, surfaces, queue depth, output memory format, secure/protected-content restrictions when supported and documented. For compute devices expose accepted operator/kernel family, precision, shape limits, memory capacity and queue concurrency. Optional offloads disabled until tested. Never infer AV1 encode from AV1 decode support, or vice versa.

Selected application paths must fall back to software when safe and supported. Fallback is not evidence of hardware acceleration: report the chosen backend and verify the device actually did work.

## 5. Video decode/encode

- Establish a correct Ring 3 software reference for selected H.264, HEVC, VP9 and AV1 test vectors (choose a small subset first; licensing/patent obligations require review).
- Bind **one** actual media engine supported by MicaNT through the owning GPU driver and DXVA/D3D12 Video-equivalent user-mode service.
- Validate bitstream descriptors, surface bounds, crop/stride/chroma, reference frames, fences and resource sharing.
- Prove decode output equivalence by frame hashes or toleranced pixel comparison against reference vectors; note permitted implementation differences.
- For encode, validate decodable output, parameter constraints, timestamp ordering and no stale/disclosed surface data.
- No arbitrary bitstream parsing in GPU interrupt/DPC paths.

Intel Quick Sync, AMD VCN and NVIDIA NVDEC/NVENC are different vendor-specific media engines; support must be proved per concrete device/driver/firmware combination, not announced as general availability. VirtIO-GPU 2D alone does not supply these media blocks.

## 6. Audio and signal processing

Use the **canonical Ring 3 audio engine and WASAPI interfaces** for render/capture/mixing; hardware audio controllers merely transfer PCM and report state. CPU vector paths can implement conversion/resampling/mixing in Ring 3, selected by OS-enabled features. Dedicated DSP audio only when the relevant driver and firmware are present. Hardware audio or software codec availability does not imply support for microphone input/output on physical devices.

## 7. AI and compute accelerators

- CPU baseline: deterministic/reference numerical implementation, with SSE/AVX/AMX variants where the OS has correctly enabled the ISA.
- NPU/GPU backend: isolated userspace model parsing and operator graph lowering; kernel/device driver only validates and queues trusted command representations and memory bindings.
- DSA: explicit memory move/fill/compare tests with byte-exact outputs and validated DMA ownership.
- QAT: selected crypto/compression operations, byte-exact or cryptographic vector tests; keys and sensitive buffers scoped to owning context and scrubbed appropriately.
- Keep a capability-driven fallback; compare performance/energy only after correctness is demonstrated.

## 8. Security and reliability

Untrusted media and model files can be malicious. Fuzz parsers in Ring 3, validate cross-boundary offsets and lengths, zero private buffers before sharing, enforce device/context access checks, and handle driver reset mid-operation without dangling mappings. Test buffer exhaustion, partial completion, cancellation, process termination, power state changes, malformed descriptors and IOMMU faults. Firmware authenticity and vendor driver licensing require separate review.

## 9. Milestone phases

| Phase | Target | Acceptance |
|---|---|---|
| M0 | Inventory/API correctness and software baseline | Ring 3 reference decode/compute outputs, imported API audit |
| M1 | SIMD dispatch and audio/image kernels | Real user-mode vector execution, correct state and fallback |
| M2 | One hardware media decode backend | GPU device verified, real frames decoded and validated |
| M3 | One hardware encode backend | Valid encoded stream, tested error/cancel/reset |
| M4 | One compute accelerator backend | Real NPU/DSA/QAT/GPU job with completion and accuracy |
| M5 | Multi-client safety and performance | Isolation, contention, latency/throughput/energy profiling |

Milestone IDs here are **local phases**, not new official repository milestone numbers; avoid conflicts with `docs/KERNEL_MILESTONES.md`.

## 10. Acceptance test matrix

| Test | Hosted software | AMD64 QEMU | Physical hardware |
|---|---|---|---|
| Vector algorithm accuracy | Yes | If virtual CPU exposes feature | Required for claimed ISA/device |
| Software decode sample | Yes | User-mode execution | Optional repeat |
| Hardware video decode | Reference only | Only if actual virtual backend supports it | Required per GPU model |
| Hardware encoder | Reference only | Only with supported virtual hardware | Required per GPU model |
| NPU/DSA/QAT job | Simulated interface tests only | Only if device genuinely modeled | Required for each claimed device |
| Reset/cancel/cleanup | Mock negative tests | Device-capable fault tests | Supported-device test |

Persist exact device IDs, driver version, microcode/firmware where relevant, test vectors, SHA hashes, timestamps and pass/fail evidence. Keep source revisions and test output reproducible. No claim of hardware acceleration from CPU-only fallback output.

## 11. Out of scope

Whole GPU graphics stack replacement, standalone kernel codec parsers, unbounded driver-specific public APIs, universal Windows application compatibility, arbitrary vendor GPU/NPU support, DRM bypass, and kernel-hosted inference model interpretation.

## 12. Definition of done

At least one actual user-mode workload selects and completes a verified hardware path through the canonical driver, produces correct measured output, survives lifecycle/fault cases, and preserves Ring 0/3 boundaries. Each additional codec/profile, device or compute operator requires its own evidence. Reference software paths remain available and explicitly labeled.
