#!/usr/bin/env python3
"""Boot the verified modular kernel in QEMU/OVMF and check serial milestones."""

import argparse
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from fat32_image import create_fat32_image, create_gpt_disk_image
from m10_tls_peer import M10TlsPeer

from verify_kernel_architecture import validate_build_manifest


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_BOOTSTRAP_SERIAL = (
    "[MicaNT COM1] EfiMain reached on physical x86_64 silicon!",
    "[MicaNT Boot] Probing FAT32 volume for \\EFI\\MicaNT\\ntoskrnl.exe...",
    "[MicaNT Boot] ExitBootServices SUCCESS!",
    "[MicaNT] Modular KiSystemStartup reached",
    "[MicaNT] Phase 0 started",
    "[MicaNT] Kernel-owned page-table root active",
    "[MicaNT Mm] Canonical physical allocation, zeroing and release verified",
    "[MicaNT Mm] PFN-backed page tables, TLB and reclaim verified",
    "[MicaNT Ex] Canonical page-backed C and C++ allocation verified",
    "[MicaNT Mm] Canonical VAD tree exclusion and cleanup verified",
    "[MicaNT Mm] VAD reserve, commit, mapping and release verified",
    "[MicaNT Mm] Managed protection, decommit and PFN reclamation verified",
    "[MicaNT Mm] QEMU managed noncommitted and read-only faults verified",
    "[MicaNT Ex] Kernel C++ constructors dispatched",
    "[MicaNT Ob] Canonical kernel handle storage and references verified",
    "[MicaNT Ke] Ready-queue withdrawal verified",
    "[MicaNT Ldr] Independent TLS templates verified",
)
# The runtime self-test halts immediately after global construction/teardown,
# before the Ob, Ke and loader diagnostics in the normal bootstrap path.
EXPECTED_RUNTIME_SELFTEST_SERIAL = EXPECTED_BOOTSTRAP_SERIAL[:13] + (
    "[MicaNT Ex] Compiler-generated construction and LIFO teardown verified",
    "[MicaNT Ex] Runtime self-test halted",
)
EXPECTED_TIMER_SELFTEST_SERIAL = EXPECTED_BOOTSTRAP_SERIAL + (
    "[MicaNT Hal] Timer interrupt source verified (scheduler preemption not tested)",
    "[MicaNT Hal] Timer-only self-test halted",
)
EXPECTED_SERIAL = EXPECTED_BOOTSTRAP_SERIAL + (
    "[MicaNT Ke] Allocation-free ready queue selected cooperative contexts",
    "[MicaNT Ke] Kernel thread block and wake verified",
    "[MicaNT Ke] Kernel APC FIFO verified",
    "[MicaNT Ke] Kernel event wait and wake verified",
    "[MicaNT Ps] Kernel thread lifecycle verified",
    "[MicaNT Ps] CPL3 execution and Ring 0 trap return verified",
    "[MicaNT Ps] GS-based TEB TLS slot read/write verified",
    "[MicaNT Mm] User/kernel isolation and read-only user code verified",
    "[MicaNT Mm] Supervisor access checks and non-executable user stack verified",
    "[MicaNT Mm] PFN-backed executable user page verified",
    "[MicaNT Hal] QPC scale matched APIC timing; timeout and deadline rollover passed",
    "[MicaNT Display] OEM driver unavailable; GOP framebuffer pass-through active",
    "[MicaNT Display] Desktop presented through GOP framebuffer",
    "[MicaNT Explorer] Desktop and taskbar rendered",
    "[MicaNT Hal] CPL3 calibrated QPC precision and KUSER uptime advancement verified",
    "[MicaNT Ke] CPL3 syscall ABI, register preservation and user-buffer checks verified",
    "[MicaNT Mm] CPL3 native allocation and release verified",
    "[MicaNT Ke] CPL3 native event polling verified",
    "[MicaNT Ps] Process-owned CPL3 context and termination verified",
    "[MicaNT Ps] Two CPL3 threads, TLS slots and event waits verified",
    "[MicaNT Ps] Three CPL3 thread termination verified",
    "[MicaNT Ps] File-backed SEC_IMAGE process image verified",
    "[MicaNT Hal] Local APIC periodic interrupts verified",
    "[MicaNT Ke] Timer-preempted kernel contexts verified",
    "[MicaNT Ke] Timer-preempted CPL3 contexts verified",
    "[MicaNT Ex] Canonical checked user copies verified at CPL3 boundary",
    "[MicaNT Ps] Bounded native PE mapping and compiled user entry verified",
    "[MicaNT Ke] Runtime idle wait and wake verified",
    "[MicaNT Ps] Deferred reaper verified",
    "[MicaNT Ke] Runtime DPC/APC delivery verified",
    "[MicaNT Input] PS/2 mouse interrupt routing active",
    "[MicaNT Explorer] Shell idle loop entered",
    "[MicaNT Ke] Runtime scheduler clock ticking",
)

def verify_cpl3_surface(
    qmp_path: Path,
    temp: Path,
    diagnostics_dir: Path | None = None,
) -> None:
    """Verify the CPL3 surface before later desktop rendering can replace it."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            greeting = json.loads(stream.readline())
            if "QMP" not in greeting:
                raise RuntimeError("QEMU did not send a QMP greeting")

            def execute(command: str, arguments: dict | None = None) -> dict:
                stream.write((json.dumps({
                    "execute": command, "arguments": arguments or {},
                }) + "\n").encode())
                stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if "error" in response:
                        raise RuntimeError(f"QMP {command} failed: {response['error']}")
                    if "return" in response:
                        return response["return"]

            execute("qmp_capabilities")
            image_path = temp / "cpl3-surface.ppm"
            execute("screendump", {"filename": str(image_path)})
            if diagnostics_dir is not None:
                shutil.copyfile(image_path, diagnostics_dir / image_path.name)
            with image_path.open("rb") as image:
                if (image.readline() != b"P6\n" or
                        image.readline() != b"1024 768\n" or
                        image.readline() != b"255\n"):
                    raise RuntimeError("Unexpected VirtIO scanout screenshot format")
                pixel = image.read(3)
            expected = bytes((0x11, 0x22, 0x33))
            if pixel != expected:
                raise RuntimeError(
                    "CPL3 surface presentation did not reach VirtIO scanout "
                    f"at (0, 0): expected #{expected.hex()}, observed #{pixel.hex()}"
                )


def set_qemu_network_link(qmp_path: Path, up: bool) -> None:
    """Toggle the M10 virtio link through QMP to force real TCP retransmission."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            greeting = json.loads(stream.readline())
            if "QMP" not in greeting:
                raise RuntimeError("QEMU did not send a QMP greeting")
            stream.write(b'{"execute":"qmp_capabilities"}\n')
            stream.flush()
            while True:
                response = json.loads(stream.readline())
                if "error" in response:
                    raise RuntimeError(f"QMP capability negotiation failed: {response['error']}")
                if "return" in response:
                    break
            command = json.dumps({
                "execute": "set_link",
                "arguments": {"name": "micant_virtio_net", "up": up},
            }).encode() + b"\n"
            stream.write(command)
            stream.flush()
            while True:
                response = json.loads(stream.readline())
                if "error" in response:
                    raise RuntimeError(f"QMP set_link failed: {response['error']}")
                if "return" in response:
                    return


def count_tcp_payload_frames(capture: Path, payload: bytes) -> int:
    """Count captured IPv4/TCP frames carrying the exact payload bytes."""
    data = capture.read_bytes()
    if len(data) < 24:
        return 0
    magic = data[:4]
    if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1"):
        byteorder = "little"
    elif magic in (b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        byteorder = "big"
    else:
        raise RuntimeError("QEMU M10 capture is not a classic pcap file")
    offset = 24
    matches = 0
    while offset + 16 <= len(data):
        captured = int.from_bytes(data[offset + 8:offset + 12], byteorder)
        offset += 16
        if captured > len(data) - offset:
            break
        frame = memoryview(data)[offset:offset + captured]
        offset += captured
        if len(frame) < 14 or int.from_bytes(frame[12:14], "big") != 0x0800:
            continue
        if len(frame) < 34 or frame[23] != 6:
            continue
        ip_header = (frame[14] & 0x0F) * 4
        tcp_start = 14 + ip_header
        if ip_header < 20 or tcp_start + 20 > len(frame):
            continue
        tcp_header = ((frame[tcp_start + 12] >> 4) & 0x0F) * 4
        payload_start = tcp_start + tcp_header
        ip_length = int.from_bytes(frame[16:18], "big")
        payload_end = min(14 + ip_length, len(frame))
        if tcp_header < 20 or payload_start > payload_end:
            continue
        if bytes(frame[payload_start:payload_end]) == payload:
            matches += 1
    return matches


def verify_mouse(
    qmp_path: Path,
    temp: Path,
    serial_path: Path,
    diagnostics_dir: Path | None = None,
) -> None:
    """Exercise the emulated mouse and verify its actual scanout pixels."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            def receive() -> dict:
                line = stream.readline()
                if not line:
                    raise RuntimeError("QMP closed during mouse verification")
                return json.loads(line)

            if "QMP" not in receive():
                raise RuntimeError("QEMU did not send a QMP greeting")

            def execute(command: str, arguments: dict | None = None) -> None:
                stream.write((json.dumps({
                    "execute": command, "arguments": arguments or {},
                }) + "\n").encode())
                stream.flush()
                while True:
                    response = receive()
                    if "error" in response:
                        raise RuntimeError(f"QMP {command} failed: {response['error']}")
                    if "return" in response:
                        return

            def screen(name: str) -> bytes:
                path = temp / name
                execute("screendump", {"filename": str(path)})
                if diagnostics_dir is not None:
                    shutil.copyfile(path, diagnostics_dir / name)
                with path.open("rb") as image:
                    if (image.readline() != b"P6\n" or
                            image.readline() != b"1024 768\n" or
                            image.readline() != b"255\n"):
                        raise RuntimeError("Unexpected VirtIO scanout screenshot format")
                    pixels = image.read()
                if len(pixels) != 1024 * 768 * 3:
                    raise RuntimeError("Truncated VirtIO scanout screenshot")
                return pixels

            execute("qmp_capabilities")
            startup_serial = serial_path.read_text(encoding="utf-8", errors="replace")
            irq_enabled = "[MicaNT Input] PS/2 mouse interrupt routing active" in startup_serial
            before = screen("before.ppm")
            execute("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "x", "value": 40}},
                {"type": "rel", "data": {"axis": "y", "value": 20}},
            ]})
            time.sleep(0.5)
            after = screen("after.ppm")
            old_x, old_y = (1024 - 16) // 2, (768 - 16) // 2
            new_x, new_y = old_x + 40, old_y + 20
            old = (old_y * 1024 + old_x) * 3
            new = (new_y * 1024 + new_x) * 3
            if (before[old:old + 3] == after[old:old + 3] or
                    before[old:old + 3] != after[new:new + 3]):
                raise RuntimeError("PS/2 movement did not relocate the visible cursor")
            for y in range(768):
                for x in range(1024):
                    if ((old_x <= x < old_x + 16 and old_y <= y < old_y + 16) or
                            (new_x <= x < new_x + 16 and new_y <= y < new_y + 16)):
                        continue
                    pixel = (y * 1024 + x) * 3
                    if before[pixel:pixel + 3] != after[pixel:pixel + 3]:
                        raise RuntimeError("Mouse movement corrupted pixels outside cursor rectangles")
            execute("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": True}},
            ]})
            time.sleep(0.1)
            execute("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": False}},
            ]})
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                click_seen = "[MicaNT Input] Left button pressed; user-mode shell not started" in serial
                irq_seen = "[MicaNT Input] PS/2 mouse IRQ byte consumed" in serial
                if click_seen and (not irq_enabled or irq_seen):
                    return
                time.sleep(0.1)
            if irq_enabled and "[MicaNT Input] PS/2 mouse IRQ byte consumed" not in serial:
                raise RuntimeError("PS/2 IRQ route was enabled but no ISR byte reached the consumer")
            raise RuntimeError("PS/2 left click did not reach the kernel input loop")


def inject_usb_keyboard(qmp_path: Path) -> None:
    """Send a press/release pair; the kernel must report both via HID reports."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            def receive() -> dict:
                line = stream.readline()
                if not line:
                    raise RuntimeError("QMP closed during USB HID verification")
                return json.loads(line)

            if "QMP" not in receive():
                raise RuntimeError("QEMU did not send a QMP greeting")
            stream.write(b'{"execute":"qmp_capabilities"}\n')
            stream.flush()
            while "return" not in receive():
                pass
            for down in (True, False):
                stream.write((json.dumps({
                    "execute": "input-send-event",
                    "arguments": {"events": [{"type": "key", "data": {
                        "down": down, "key": {"type": "qcode", "data": "a"},
                    }}]},
                }) + "\n").encode())
                stream.flush()
                while True:
                    response = receive()
                    if "error" in response:
                        raise RuntimeError(f"QMP USB key injection failed: {response['error']}")
                    if "return" in response:
                        break
                time.sleep(0.15)


def detach_usb_device(qmp_path: Path, device_id: str) -> None:
    """Hot-unplug an emulated USB device through QMP."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            def receive() -> dict:
                line = stream.readline()
                if not line:
                    raise RuntimeError("QMP closed during USB disconnect verification")
                return json.loads(line)

            if "QMP" not in receive():
                raise RuntimeError("QEMU did not send a QMP greeting")
            stream.write(b'{"execute":"qmp_capabilities"}\n')
            stream.flush()
            while "return" not in receive():
                pass
            stream.write((json.dumps({
                "execute": "device_del", "arguments": {"id": device_id},
            }) + "\n").encode())
            stream.flush()
            while True:
                response = receive()
                if "error" in response:
                    raise RuntimeError(f"QMP USB device removal failed: {response['error']}")
                if "return" in response:
                    return


def serial_progress(serial_log: str, expected: tuple[str, ...] = EXPECTED_SERIAL) -> int:
    """Return how many expected messages occur, in order, in the serial log."""
    cursor = 0
    for index, marker in enumerate(expected):
        found = serial_log.find(marker, cursor)
        if found < 0:
            return index
        cursor = found + len(marker)
    return len(expected)


def serial_has_failure(serial_log: str) -> bool:
    if "FATAL:" in serial_log:
        return True
    gop_fallback = "[MicaNT Display] OEM driver unavailable; GOP framebuffer pass-through active" in serial_log
    for line in serial_log.splitlines():
        if not line.startswith("[MicaNT") or (" failed" not in line and "Invalid " not in line):
            continue
        if gop_fallback and line.startswith("[MicaNT Io] Boot-start driver failed: virtio_gpu.sys"):
            continue
        if line.startswith(("[MicaNT Io] Boot-start driver failed: virtio_gpu.sys",
                            "[MicaNT Io] Boot-start driver failed: storahci.sys",
                            "[MicaNT Io] Boot-start driver failed: usbxhci.sys",
                            "[MicaNT Io] Boot-start driver failed: i210.sys",
                            "[MicaNT Io] Boot-start driver failed: virtio_net.sys",
                            "[MicaNT OEM] AHCI claim, BAR, DMA or SATA initialization failed",
                            "[MicaNT OEM] i210.sys: controller startup failed")):
            continue
        return True
    return False


def run_qemu(
    ovmf_code: Path,
    timeout: int,
    stable_seconds: int,
    display: str,
    interactive: bool,
    verify_input: bool,
    runtime_selftest: bool = False,
    timer_selftest: bool = False,
    diagnostics_dir: Path | None = None,
    usb_test: bool = False,
    verify_usb_input: bool = False,
    network_test: bool = False,
) -> None:
    if runtime_selftest and (interactive or verify_input):
        raise RuntimeError("runtime self-test halts before desktop/input; incompatible runner options")
    if timer_selftest and (interactive or verify_input or runtime_selftest):
        raise RuntimeError("timer self-test halts before process tests and desktop/input; incompatible runner options")
    if verify_usb_input and not usb_test:
        raise RuntimeError("USB HID verification requires --usb-test")
    validate_build_manifest(
        ROOT,
        allow_dirty=True,
        runtime_selftest=runtime_selftest,
        timer_selftest=timer_selftest,
    )
    expected = (
        EXPECTED_TIMER_SELFTEST_SERIAL if timer_selftest else
        EXPECTED_RUNTIME_SELFTEST_SERIAL if runtime_selftest else
        EXPECTED_SERIAL
    )

    qemu = shutil.which(os.environ.get("MICANT_QEMU", "qemu-system-x86_64"))
    if not qemu:
        raise RuntimeError("qemu-system-x86_64 was not found on PATH")
    if not ovmf_code.is_file():
        raise RuntimeError(f"OVMF code image does not exist: {ovmf_code}")

    is_apple_silicon_tcg = (
        platform.system() == "Darwin"
        and platform.machine().lower() in ("arm64", "aarch64")
    )
    if is_apple_silicon_tcg:
        print(
            "[MicaNT Boot] Notice: Running AMD64 kernel under QEMU TCG software emulation on Apple Silicon.",
            flush=True,
        )
        print(
            "[MicaNT Boot] TCG translates x86_64 to ARM64 in software; scaling timeout by 2x to accommodate JIT latency.",
            flush=True,
        )
        timeout = max(timeout, timeout * 2)

    kernel = ROOT / "build" / "baremetal" / "ntoskrnl.exe"
    bootloader = ROOT / "build" / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"
    if diagnostics_dir is not None:
        diagnostics_dir.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(kernel, diagnostics_dir / kernel.name)
        shutil.copyfile(kernel.parent / "kernel-build-manifest.json",
                        diagnostics_dir / "kernel-build-manifest.json")
        print(f"QEMU diagnostics: {diagnostics_dir.resolve()}", flush=True)

    # Keep QMP sockets in the platform temporary directory. Some sandboxed
    # filesystems allow repository edits but deny AF_UNIX sockets under the
    # checkout, even when ordinary files are writable there.
    with tempfile.TemporaryDirectory(prefix="micant-qemu-") as temp_root:
        temp = Path(temp_root)
        esp = temp / "esp"
        boot_dir = esp / "EFI" / "BOOT"
        kernel_dir = esp / "EFI" / "MicaNT"
        boot_dir.mkdir(parents=True)
        kernel_dir.mkdir(parents=True)
        shutil.copyfile(bootloader, boot_dir / "BOOTX64.EFI")
        shutil.copyfile(kernel, kernel_dir / "ntoskrnl.exe")
        driver_dir = kernel_dir / "System32" / "drivers"
        driver_dir.mkdir(parents=True)
        shutil.copyfile(ROOT / "build" / "virtio_gpu.sys", driver_dir / "virtio_gpu.sys")
        for name in ("storahci.sys", "hidusb.sys", "usbstor.sys", "usbxhci.sys", "i210.sys", "virtio_net.sys"):
            source = ROOT / "build" / name
            if source.is_file():
                shutil.copyfile(source, driver_dir / name)
        # The FAT32 data volume is the system volume: FsRtl selects it by the
        # MicaNT\System32\ntdll.dll marker at the volume root, and the Ps
        # loader opens the real DLLs from there for file-backed SEC_IMAGE.
        system32_dir = esp / "MicaNT" / "System32"
        system32_dir.mkdir(parents=True)
        hosts_dir = system32_dir / "drivers" / "etc"
        hosts_dir.mkdir(parents=True, exist_ok=True)
        (hosts_dir / "hosts").write_text("10.0.2.2 host\n", encoding="ascii")
        service_host = ROOT / "build" / "svchost.exe"
        shutil.copyfile(service_host, system32_dir / service_host.name)
        if network_test:
            shutil.copyfile(ROOT / "build" / "network-probe.exe", system32_dir / "network-probe.exe")
            shutil.copyfile(ROOT / "build" / "https-probe.exe", system32_dir / "https-probe.exe")
            (system32_dir / "m10-network-test.flag").write_bytes(b"M10\n")
        for source in sorted((ROOT / "build" / "system32").glob("*.dll")):
            shutil.copyfile(source, system32_dir / source.name)
        shutil.copyfile(ROOT / "build" / "network-system32" / "ws2_32.dll",
                        system32_dir / "ws2_32.dll")
        shutil.copyfile(ROOT / "build" / "network-system32" / "dnsapi.dll",
                        system32_dir / "dnsapi.dll")
        for service_dll in ("crypt32.dll", "winhttp.dll"):
            shutil.copyfile(ROOT / "build" / "network-system32" / service_dll,
                            system32_dir / service_dll)
        # The Ps loader rejects a DLL with a DllMain entry point because DLL
        # initializers are not part of M4. Use the entry-point-free NTDLL
        # build for the file-backed image verification volume.
        for source in sorted((ROOT / "build" / "fileimage-system32").glob("*.dll")):
            shutil.copyfile(source, system32_dir / source.name)
        # A relocatable executable used to verify the file-backed SEC_IMAGE
        # main-image path against the mounted volume.
        image_exe = ROOT / "build" / "fileimage.exe"
        if image_exe.is_file():
            shutil.copyfile(image_exe, system32_dir / "fileimage.exe")
        usb_proof = system32_dir / "micant-usb-proof.txt"
        usb_proof.write_bytes(b"MicaNT USB mass-storage FAT32 post-EBS proof v1\n")
        tls_peer = M10TlsPeer(esp).start() if network_test else None
        fat_volume = temp / "ahci-fat32-volume.img"
        ahci_image = temp / "ahci-gpt-fat32.img"
        create_fat32_image(esp, fat_volume)
        create_gpt_disk_image(fat_volume, ahci_image)
        log_directory = diagnostics_dir if diagnostics_dir is not None else temp
        serial_path = log_directory / "serial.log"
        qemu_output_path = log_directory / "qemu.log"
        network_capture_path = log_directory / "network.pcap"

        command = [
            qemu,
            "-machine", "q35" if usb_test else "q35,usb=off",
            "-accel", "tcg",
            "-cpu", "qemu64,+invtsc" + (",+rdrand" if network_test else "") +
                ",vendor=GenuineIntel",
            "-device", "intel-iommu,intremap=on,caching-mode=on",
            "-rtc", "base=utc",
            "-smp", "1",
            "-m", "1024",
            "-vga", "none",
            "-device", "virtio-vga",
            "-drive",
            f"if=pflash,format=raw,unit=0,readonly=on,file={ovmf_code.resolve()}",
            "-drive", f"file=fat:ro:{esp},format=raw,if=ide,index=0",
            "-drive", f"file={ahci_image},format=raw,if=none,id=ahci_disk",
            "-device", "ide-hd,drive=ahci_disk,bus=ide.1",
            "-display", display,
            "-serial", f"file:{serial_path}",
            "-monitor", "none",
            "-qmp", f"unix:{temp / 'qmp.sock'},server=on,wait=off",
            "-snapshot",
            "-no-reboot",
            "-no-shutdown",
        ]
        network_peer = None
        network_inbound_port = None
        if network_test:
            network_peer = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            network_peer.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                network_peer.bind(("0.0.0.0", 18080))
                network_peer.listen(1)
                network_peer.setblocking(False)
            except OSError as error:
                network_peer.close()
                raise RuntimeError(f"Cannot bind controlled TCP peer on port 18080: {error}") from error
            port_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                port_probe.bind(("127.0.0.1", 0))
                network_inbound_port = port_probe.getsockname()[1]
            finally:
                port_probe.close()
            command.extend([
                "-netdev",
                f"user,id=micant_net,dns=10.0.2.3,hostfwd=tcp:127.0.0.1:{network_inbound_port}-:18081",
                "-device", "virtio-net-pci,id=micant_virtio_net,netdev=micant_net,disable-legacy=on",
                "-object",
                f"filter-dump,id=micant_net_capture,netdev=micant_net,file={network_capture_path}",
            ])
        if usb_test:
            # Exercise the native controller and class drivers after
            # ExitBootServices. Keep the AHCI copy for the existing regression
            # path and give USBSTOR a separate read-only handle to the fixture.
            command.extend([
                "-drive", f"file={ahci_image},format=raw,if=none,id=usb_disk,readonly=on",
                "-device", "qemu-xhci,id=xhci",
                "-device", "usb-hub,id=usb_hub,bus=xhci.0,port=1",
                "-device", "usb-kbd,id=usb_keyboard,bus=xhci.0,port=1.1",
                "-device", "usb-mouse,id=usb_mouse,bus=xhci.0,port=1.2",
                "-device", "usb-storage,id=usb_storage,drive=usb_disk,bus=xhci.0",
            ])
        started = time.monotonic()
        complete_since = None
        surface_verified = False
        usb_input_sent = False
        network_outbound_peer_done = False
        network_loss_injected = False
        network_inbound_peer_done = False

        with qemu_output_path.open("w", encoding="utf-8") as qemu_output:
            process = subprocess.Popen(
                command,
                cwd=ROOT,
                stdout=qemu_output,
                stderr=subprocess.STDOUT,
            )
            try:
                while time.monotonic() - started < timeout:
                    return_code = process.poll()
                    if return_code is not None:
                        qemu_output.flush()
                        details = qemu_output_path.read_text(
                            encoding="utf-8", errors="replace"
                        )
                        raise RuntimeError(
                            f"QEMU exited with status {return_code} before the "
                            f"required boot state.\n{details}"
                        )

                    serial_log = (
                        serial_path.read_text(encoding="utf-8", errors="replace")
                        if serial_path.exists()
                        else ""
                    )
                    if network_test and network_peer is not None and not network_outbound_peer_done:
                        try:
                            peer_connection, _ = network_peer.accept()
                        except BlockingIOError:
                            peer_connection = None
                        if peer_connection is not None:
                            with peer_connection:
                                peer_connection.settimeout(5)
                                expected_payload = b"MICANT-M10-PING"
                                received = bytearray()
                                while len(received) < len(expected_payload):
                                    block = peer_connection.recv(len(expected_payload) - len(received))
                                    if not block:
                                        break
                                    received.extend(block)
                                if bytes(received) != expected_payload:
                                    raise RuntimeError("Ring 3 TCP probe sent an unexpected payload")
                                set_qemu_network_link(temp / "qmp.sock", False)
                                try:
                                    peer_connection.sendall(b"MICANT-M10-PONG")
                                    # Exceed the peer's initial TCP retransmit
                                    # timeout while the guest link is down.
                                    time.sleep(2.2)
                                finally:
                                    set_qemu_network_link(temp / "qmp.sock", True)
                                network_loss_injected = True
                            network_outbound_peer_done = True
                    if (network_test and not network_inbound_peer_done and network_inbound_port and
                            "MicaNT guest TCP listener ready." in serial_log):
                        try:
                            with socket.create_connection(("127.0.0.1", network_inbound_port), timeout=0.5) as peer:
                                peer.settimeout(5)
                                peer.sendall(b"MICA-ACCEPT-PING\n")
                                expected_payload = b"MICA-ACCEPT-PONG\n"
                                received = bytearray()
                                while len(received) < len(expected_payload):
                                    block = peer.recv(len(expected_payload) - len(received))
                                    if not block:
                                        break
                                    received.extend(block)
                                if bytes(received) != expected_payload:
                                    raise RuntimeError("Ring 3 TCP accept path returned an unexpected payload")
                            network_inbound_peer_done = True
                        except (ConnectionRefusedError, TimeoutError, OSError):
                            pass
                    if network_test and "Boot-start driver failed: virtio_net.sys" in serial_log:
                        failure = next((line for line in serial_log.splitlines()
                                        if "Boot-start driver failed: virtio_net.sys" in line),
                                       "VirtIO-net DriverEntry failed")
                        raise RuntimeError(f"QEMU virtio-net miniport failed: {failure}")
                    if network_test and "[MicaNT DHCP] Ring 3 lease acquisition failed status=" in serial_log:
                        failure = next((line for line in serial_log.splitlines()
                                        if "[MicaNT DHCP] Ring 3 lease acquisition failed status=" in line),
                                       "Ring 3 DHCP client failed")
                        raise RuntimeError(f"QEMU DHCP integration failed: {failure}")
                    if network_test and "DNS A resolution failed through Winsock/AFD." in serial_log:
                        raise RuntimeError("QEMU Ring 3 DNS A resolution failed")
                    if network_test and "M10 TCP client or listen/accept payload validation failed" in serial_log:
                        raise RuntimeError("QEMU Ring 3 TCP exchange failed")
                    if network_test and "M10 HTTPS probe failed:" in serial_log:
                        raise RuntimeError("QEMU Ring 3 HTTPS validation or hostname rejection failed")
                    if network_test and tls_peer is not None and tls_peer.error is not None:
                        raise RuntimeError(f"Controlled M10 TLS peer failed: {tls_peer.error}")
                    if (network_test and
                            "[MicaNT DHCP] Ring 3 DORA lease acquired through Winsock/AFD" in serial_log and
                            "DNS A record for example.com resolved through Winsock/AFD." in serial_log and
                            "M10 TCP client exchange passed" in serial_log and
                            "[MicaNT Net] Malformed RX frame corpus passed through NDIS queue" in serial_log and
                            "M10 TCP listen/accept exchange passed" in serial_log and
                            "M10 HTTPS trusted payload passed" in serial_log and
                            "M10 HTTPS mismatched host rejected" in serial_log and
                            "M10 HTTPS unknown issuer rejected" in serial_log and
                            "M10 HTTPS expired certificate rejected" in serial_log and
                            "M10 HTTPS probe passed" in serial_log and
                            network_outbound_peer_done and network_inbound_peer_done):
                        if not network_capture_path.is_file() or network_capture_path.stat().st_size <= 24:
                            raise RuntimeError("QEMU M10 test completed without captured NIC packets")
                        if not network_loss_injected:
                            raise RuntimeError("QEMU M10 packet-loss fault was not injected")
                        retransmissions = count_tcp_payload_frames(
                            network_capture_path, b"MICANT-M10-PONG")
                        if retransmissions < 2:
                            raise RuntimeError(
                                "QEMU M10 packet-loss recovery lacked a captured TCP payload retransmission "
                                f"(observed {retransmissions})"
                            )
                        print("QEMU Ring 3 DHCP, DNS, TCP retransmission, malformed RX and HTTPS validation passed.", flush=True)
                        return
                    if (verify_usb_input and not usb_input_sent and
                            "USB host: matched HID interrupt interface" in serial_log):
                        # Wait until the bus reports a real boot-HID interface,
                        # then allow endpoint configuration to finish before
                        # sending a press/release pair for transfer completion.
                        time.sleep(0.5)
                        inject_usb_keyboard(temp / "qmp.sock")
                        usb_input_sent = True
                    if serial_has_failure(serial_log):
                        raise RuntimeError(f"Kernel bootstrap failed:\n{serial_log[-8000:]}")
                    if (not surface_verified and
                            "[MicaNT Test] CPL3 surface presentation completed" in serial_log):
                        verify_cpl3_surface(
                            temp / "qmp.sock", temp, diagnostics_dir,
                        )
                        surface_verified = True
                        print("CPL3 surface reached VirtIO scanout before desktop redraw.", flush=True)
                    progress = serial_progress(serial_log, expected)
                    if progress == len(expected):
                        time_evidence = timer_selftest or runtime_selftest or (
                            "[MicaNT Hal] CPL3 UTC SystemTime advanced and matched KUSER" in serial_log or
                            "[MicaNT Hal] CPL3 UTC SystemTime correctly unavailable for local firmware time" in serial_log
                        )
                        if not time_evidence:
                            raise RuntimeError("CPL3 time probe reported neither UTC success nor explicit unavailable status")
                        if complete_since is None:
                            complete_since = time.monotonic()
                        elif time.monotonic() - complete_since >= stable_seconds:
                            if verify_usb_input:
                                if not usb_input_sent:
                                    inject_usb_keyboard(temp / "qmp.sock")
                                deadline = time.monotonic() + 10
                                while time.monotonic() < deadline:
                                    usb_serial = serial_path.read_text(
                                        encoding="utf-8", errors="replace")
                                    if ("usbxhci.sys: native host registered" in usb_serial and
                                            "hidusb.sys: completed USB HID activity report" in usb_serial and
                                            "hidusb.sys: completed USB HID release report" in usb_serial and
                                            "usbstor.sys: BOT/SCSI device mounted" in usb_serial):
                                        prior_cleanups = usb_serial.count(
                                            "USB host: disconnect cleanup completed")
                                        detach_usb_device(temp / "qmp.sock", "usb_keyboard")
                                        cleanup_deadline = time.monotonic() + 10
                                        while time.monotonic() < cleanup_deadline:
                                            usb_serial = serial_path.read_text(
                                                encoding="utf-8", errors="replace")
                                            if usb_serial.count(
                                                    "USB host: disconnect cleanup completed") > prior_cleanups:
                                                storage_cleanups = usb_serial.count(
                                                    "USB host: disconnect cleanup completed")
                                                detach_usb_device(temp / "qmp.sock", "usb_storage")
                                                storage_deadline = time.monotonic() + 10
                                                while time.monotonic() < storage_deadline:
                                                    usb_serial = serial_path.read_text(
                                                        encoding="utf-8", errors="replace")
                                                    if usb_serial.count(
                                                            "USB host: disconnect cleanup completed") > storage_cleanups:
                                                        print("USB xHCI HID press/release, HID and storage detach cleanup, and BOT/FAT evidence passed.", flush=True)
                                                        return
                                                    time.sleep(0.1)
                                                raise RuntimeError("USB storage detached but xHCI cleanup did not complete")
                                            time.sleep(0.1)
                                        raise RuntimeError("USB keyboard detached but xHCI cleanup did not complete")
                                    time.sleep(0.1)
                                raise RuntimeError(
                                    "USB test lacked real xHCI HID press/release and USBSTOR mount evidence")
                            if verify_input:
                                verify_mouse(
                                    temp / "qmp.sock", temp, serial_path, diagnostics_dir,
                                )
                                print("PS/2 movement, scanout updates and left-click delivery passed.", flush=True)
                            result = (
                                "timer self-test" if timer_selftest else
                                "C++ runtime self-test" if runtime_selftest else
                                "modular boot"
                            )
                            print(f"QEMU {result} passed; serial remained stable for "
                                  f"{stable_seconds}s.", flush=True)
                            if interactive:
                                print("QEMU is running interactively; close its window or press Ctrl+C to stop.", flush=True)
                                return_code = process.wait()
                                if return_code != 0:
                                    raise RuntimeError(
                                        f"QEMU exited with status {return_code} "
                                        "after the modular shell started"
                                    )
                            return
                    else:
                        complete_since = None
                    time.sleep(0.1)

                serial_log = (
                    serial_path.read_text(encoding="utf-8", errors="replace")
                    if serial_path.exists()
                    else ""
                )
                progress = serial_progress(serial_log, expected)
                missing = (expected[progress] if progress < len(expected)
                           else "stable idle interval")
                qemu_output.flush()
                details = qemu_output_path.read_text(encoding="utf-8", errors="replace")
                raise RuntimeError(
                    f"QEMU boot timed out after {timeout}s; next required serial "
                    f"milestone: {missing}\nSerial tail:\n{serial_log[-8000:]}"
                    f"\nQEMU output:\n{details[-4000:]}"
                )
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                if network_peer is not None:
                    network_peer.close()
                if tls_peer is not None:
                    tls_peer.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ovmf-code",
        type=Path,
        default=os.environ.get("MICANT_OVMF_CODE"),
        help="path to the x86_64 OVMF code image (or MICANT_OVMF_CODE)",
    )
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--diagnostics-dir", type=Path)
    parser.add_argument("--stable-seconds", type=int, default=5)
    parser.add_argument("--display", default="none")
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--verify-input", action="store_true")
    parser.add_argument("--usb-test", action="store_true")
    parser.add_argument("--verify-usb-input", action="store_true")
    parser.add_argument("--network-test", action="store_true")
    parser.add_argument("--runtime-selftest", action="store_true")
    parser.add_argument("--timer-selftest", action="store_true")
    args = parser.parse_args()

    if args.ovmf_code is None:
        parser.error("--ovmf-code or MICANT_OVMF_CODE is required")
    if args.timeout <= 0 or args.stable_seconds <= 0:
        parser.error("--timeout and --stable-seconds must be positive")

    try:
        if args.interactive and args.display == "none":
            parser.error("--interactive requires a visible --display backend")
        run_qemu(
            args.ovmf_code,
            args.timeout,
            args.stable_seconds,
            args.display,
            args.interactive,
            args.verify_input,
            args.runtime_selftest,
            args.timer_selftest,
            args.diagnostics_dir.resolve() if args.diagnostics_dir is not None else None,
            args.usb_test,
            args.verify_usb_input,
            args.network_test,
        )
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"QEMU boot test failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
