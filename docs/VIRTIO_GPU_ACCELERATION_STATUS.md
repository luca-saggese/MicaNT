# VirtIO-GPU and VirGL status

Checked on 2026-11-15 with the local Homebrew QEMU 11.1.3 build on macOS.

## Host capability

- QEMU reports only `tcg` as an accelerator.
- The available display backends are `cocoa` and `dbus`; GTK and SDL are absent.
- The device list contains `virtio-vga`, but not `virtio-vga-gl`. Its reported properties do not include a VirGL switch.
- An explicit `-device virtio-vga-gl` launch fails with “not a valid device model name”. `--graphics-backend virgl` reports this host limitation before launching the guest. The default `auto` now checks the installed device/display and briefly probes GL initialization before selecting VirGL; it otherwise keeps the existing deterministic 2D profile.

This QEMU installation cannot verify VirGL negotiation, 3D submission, GPU completion, or a 2D-versus-VirGL performance comparison.

## VirtIO 2D boot evidence

The canonical `ntoskrnl_core_baremetal` and boot-start driver were built, then run with `qemu.py --headless --graphics-backend virtio2d`. Serial evidence is in `/private/tmp/micant-virtio2d-query/serial.log`; QMP screenshots, including `overlapping-windows.ppm`, are in the same directory.

The serial log shows PCI `1AF4:1050` discovery, control queue initialization, “VIRGL not offered; using VirtIO 2D scanout”, successful `virtio_gpu.sys` DriverEntry, CPL3 presentation to VirtIO scanout, compositor entry, and the live capability query `SOFTWARE_COMPOSITOR + VIRTIO_2D_SCANOUT`. A prior run reached two overlapping application windows and stopped at `Caption drag failed after the live theme switch`; the latest run stopped earlier when Explorer could not open the Task Manager probe image. The full QEMU regression is therefore not passing consistently. The retained captures do show the initial desktop and overlapping-window states.

The first sandboxed QEMU attempts could not bind the runner's QMP Unix socket. Running the authorized verification with the necessary local socket access allowed the boot and screenshot checks to proceed.

## Current implementation boundary

- The Ring 3 compositor composes windows on the CPU and calls the `PresentSurface` service for dirty rectangles. The VirtIO 2D driver keeps a persistent DMA scanout shadow and transfers the changed rectangle; full-frame comparison is limited to full-frame submissions.
- `PresentComposition` is present in the graphics service/driver ABI, but the Ring 3 compositor does not call it. Its driver path supports opaque layer copies only; it does not implement alpha blending, z-order composition, or dirty-region VirGL composition. Therefore the current desktop is not GPU-composited.
- `NtGdiGetVideoInfo` now reports transport and composition states separately, and the Ring 3 compositor queries and logs this capability once at startup. A VirGL device begins as available; the state becomes accelerated only after a successful composition call submits and completes a 3D copy command. VirtIO 2D reports software composition plus 2D scanout. GOP fallback reports GOP transport plus software composition.
- If an advertised VIRGL feature set is rejected during VirtIO negotiation, the driver resets the device and retries with VERSION_1 only. Failed context/resource setup releases the partial 3D output resource and context before continuing with 2D.

## Remaining acceptance work

VirGL can be completed only on a host QEMU with a working `virtio-vga-gl` device and GTK/SDL GL display, typically a Linux installation with VirGL renderer support. After that, the compositor still needs a real call path for GPU composition and implementation of alpha blending, clipping, dirty regions, reusable resources, synchronization/fences, and completion/error counters. G1–G8 and performance comparison remain unverified; this status does not claim accelerated desktop composition.
