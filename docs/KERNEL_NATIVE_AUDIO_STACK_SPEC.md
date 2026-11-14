# MicaNT Native Audio Stack — Implementation Specification

## Objective and scope

Implement one usable audio stack for the canonical AMD64 modular kernel and
Ring 3 processes. The supported end state is real PCM render and capture on a
documented hardware profile, shared multi-process mixing, endpoint management,
and bounded Windows audio API compatibility. Driver detection, exported
interfaces, test fixtures, generated PCM, and hosted-only behavior do not
count as hardware support.

The initial hardware path is Intel High Definition Audio (HDA) in QEMU. USB
Audio Class and physical-machine validation follow after the HDA path is
stable. Keep codec parsing, general mixing, format conversion, and media
decoding in Ring 3. Kernel code owns controller resources, DMA, interrupts,
stream state, and safe transfer of PCM buffers across the established NT
boundary. Do not add a parallel audio manager or a new kernel ABI.

## Existing implementation findings

The current `drivers/hdaudio` implementation is not the production backend:
its headers mix kernel PCI access with user-mode PrismAudio/SCM/loader code,
assume a specific PCI ID and codec topology, and include a synthesized tone
path. `ring1_wavert_audio_dma.hpp` reports fixed virtual addresses and success
without allocating or mapping DMA memory. Existing WASAPI and PrismAudio
interfaces are not evidence of a connected endpoint or real stream. Replace
or bypass these paths only through the canonical owners and make unsupported
operations fail explicitly.

## Architecture and ownership

### Ring 0

- Enumerate PCI class `04/03` controllers via the canonical PCI/PnP path.
- Claim resources, enable memory/bus mastering as required, map the BAR with
  the canonical MMIO mapper, reset the controller, and unwind in reverse order
  on every failure or removal.
- Enumerate codecs through CORB/RIRB with bounded waits, then inspect function
  groups, widgets, pin defaults, capabilities, and connection lists using
  checked verb transactions.
- Configure selected codec paths and controller output/input streams only
  after discovering capabilities. Allocate and pin physically valid BDL and
  PCM pages through Mm; never accept caller-supplied physical addresses.
- Track stream lifecycle, DMA position, interrupts/DPC, underrun/overrun,
  timeout, cancellation, device invalidation, power transitions, and cleanup.
- Expose a narrow, mode-neutral audio transfer contract through established
  NT device I/O/syscall mechanisms. User-mode targets must not include
  `micant/km/` headers or receive kernel object pointers.

### Ring 3

- A single Audio Engine owns endpoint records, sessions, shared render/capture
  streams, conversion/resampling, channel mapping, clipping, per-stream/session
  controls, endpoint master volume/mute, and event-driven buffer production.
- Reuse PrismAudio only where its source queue and DSP behavior can be connected
  to real backend buffers and lifecycle. It must not claim playback while
  merely retaining submitted buffers.
- WASAPI/Core Audio clients and compatibility APIs converge on the same engine
  and endpoints. Media Foundation and codecs may supply PCM but are not needed
  for first playback.
- Real-time work is bounded: no filesystem I/O, unbounded allocation, global
  blocking locks, or unpredictable application callbacks on the hardware
  production path.

## Milestones and acceptance

### M18 — Native audio stack

The milestones below are sequential integration gates within M18, not separate
claims of complete Windows audio support.

| Gate | Deliverable | Acceptance evidence |
|---|---|---|
| A0.1 Controller bring-up | Canonical PCI discovery, BAR mapping, reset, capability/resource validation, rollback | QEMU identifies the emulated HDA controller from live PCI config, maps its actual BAR, completes reset with bounded timeout, and releases resources on an injected/observed failure path. No codec or playback success is inferred from PCI detection. |
| A0.2 Codec discovery | CORB/RIRB transaction engine, codec/function-group/widget discovery, topology and pin capability inventory | QEMU serial evidence includes controller/codec IDs and bounded topology; timeouts, absent codecs, malformed ranges, and command failures fail explicitly. |
| A0.3 Real PCM render | Codec route setup, real Mm-backed DMA BDL/buffer, output stream lifecycle and completion handling | QEMU HDA receives a known PCM waveform from a Ring 3 client; controller DMA completion and captured/emulated output samples are checked. Repeated start/stop and error rollback leave no pinned pages or mappings. |
| A1 Capture | ADC/pin route, input DMA and event-driven position/timestamps, gain/mute distinction | Ring 3 capture verifies samples from a configured QEMU audio input source; no generated buffer is accepted as capture evidence. Test overrun, unplug/reset and cleanup. |
| A2 Shared Audio Engine | Multi-client mixing, conversion/resampling, channel map, clipping, per-stream/session/master volume and mute, panning, worker scheduling | Three Ring 3 processes produce distinct signals concurrently; mixed samples are checked and sent to the real endpoint. Verify controls, continuity, underrun policy, client termination and device invalidation. |
| A3 WASAPI | Endpoint enumerator, render/capture clients, clocks, sessions/volume, event-driven buffer signaling; shared/exclusive contracts | Ring 3 PE enumerates real endpoints and renders/captures PCM. Unsupported formats/modes and conflicts return documented errors. No fabricated success. |
| A4 Endpoint management | Stable endpoint IDs, roles/defaults, state and notifications, jack state and invalidation | Live render/capture endpoints match discovered topology; removal/change invalidates affected streams and notifies clients; persisted choices only when canonical storage is available. |
| A5 API convergence | WinMM waveOut/waveIn, DirectSound and XAudio2/PrismAudio adapters over the same engine | Each supported API produces/captures through shared endpoint/session state; unsupported surface areas fail correctly. |
| A6 USB Audio | USB Audio Class 1.0/2.0 playback/capture using canonical USB/xHCI isochronous transfers | Supported USB headset and microphone use the same engine and WASAPI endpoints; descriptor bounds, alternate settings, clocks/feedback, hotplug and cleanup are exercised. |
| A7 Physical profile | Record actual controller, codec, PCI/USB IDs and supported formats; repeat core lifecycle, render/capture, controls and concurrency | Hardware evidence is documented separately from QEMU. No universal compatibility claim based on one profile. |

## Required invariants and failure behavior

- Preserve UEFI handoff, canonical modular kernel, PCI ownership, Mm PFN and
  page-table rules, IRQL constraints, Ring 3 boundary, and existing drivers.
- Use checked arithmetic and bounded waits for register ranges, DMA lengths,
  periods, descriptor counts, sample positions, and user buffers.
- Maintain explicit ownership from device start through stop/removal. A failed
  stage rolls back only acquired resources in reverse order; device removal
  prevents new I/O and drains in-flight work before unmapping/freeing memory.
- Do not report an endpoint until its hardware route and stream capabilities
  are known. Do not report a started stream until hardware has accepted it.
- Never use hardcoded physical/virtual DMA addresses, dummy codec responses,
  synthetic capture data, counters as sole evidence, or silent fallback success.
- Distinguish implemented, linked, QEMU-exercised, and physical-hardware
  verified behavior in milestone reporting and the integration ledger.

## Verification and completion policy

At each integration gate, use focused compilation and tests, then the actual
`ntoskrnl_core_baremetal` path and QEMU end-to-end checks when that gate touches
kernel integration. Inspect serial/QMP/audio capture evidence and verify
resource cleanup. Hosted tests are supplemental. Physical-device validation
is separate from QEMU. Commit coherent blocks only after their prescribed
verification; a commit does not close M18. Continue to the next unsatisfied
gate while the task remains active.

Full M18 completion requires real render and capture, verified shared mixing
and controls, usable Ring 3 WASAPI, live endpoint lifecycle, converged supported
compatibility APIs, USB Audio acceptance for the declared profile, physical
profile evidence, and cleanup/error-path coverage. Any unsupported variant
must remain explicit.
