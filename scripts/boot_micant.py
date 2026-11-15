#!/usr/bin/env python3
"""Boot the verified modular kernel in QEMU/OVMF and check serial milestones."""

import argparse
import json
import os
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
    "[MicaNT Ke] Runtime scheduler clock ticking",
    "[MicaNT Explorer] Kernel idle loop entered",
    "[MicaNT Explorer] SEC_IMAGE process and CPL3 initial thread started",
    "[MicaNT Explorer] Ring 3 entry reached; Win32k class registered",
    "[MicaNT Explorer] First user-mode window created",
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

            def move_to(x: int, y: int) -> None:
                # Clamp the relative PS/2 pointer to the origin, then move in
                # bounded deltas to avoid packet truncation.
                events = []
                def append_motion(axis: str, distance: int) -> None:
                    while distance:
                        step = max(-64, min(64, distance))
                        events.append({"type": "rel", "data": {
                            "axis": axis, "value": step,
                        }})
                        distance -= step
                append_motion("x", -1200)
                append_motion("y", -1200)
                append_motion("x", x)
                append_motion("y", y)
                for event in events:
                    execute("input-send-event", {"events": [event]})
                    time.sleep(0.02)
                time.sleep(0.2)

            def move_by(dx: int, dy: int) -> None:
                if abs(dx) > 64 or abs(dy) > 64:
                    raise ValueError("Pointer drag deltas must fit one PS/2 packet")
                execute("input-send-event", {"events": [
                    {"type": "rel", "data": {"axis": "x", "value": dx}},
                    {"type": "rel", "data": {"axis": "y", "value": dy}},
                ]})
                time.sleep(0.2)

            def drag_by(dx: int, dy: int) -> None:
                while dx or dy:
                    step_x = max(-64, min(64, dx))
                    step_y = max(-64, min(64, dy))
                    move_by(step_x, step_y)
                    dx -= step_x
                    dy -= step_y

            def set_left_button(down: bool) -> None:
                execute("input-send-event", {"events": [
                    {"type": "btn", "data": {"button": "left", "down": down}},
                ]})
                time.sleep(0.1)

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

            def changed_pixels(left: bytes, right: bytes) -> int:
                return sum(1 for offset in range(0, len(left), 3)
                           if left[offset:offset + 3] != right[offset:offset + 3])

            def pixel_at(image: bytes, x: int, y: int) -> bytes:
                offset = (y * 1024 + x) * 3
                return image[offset:offset + 3]

            def wait_for_changed_screen(reference: bytes, name: str,
                                        minimum_pixels: int, timeout: float = 5.0) -> bytes:
                deadline = time.monotonic() + timeout
                latest = b""
                previous = b""
                stable_since = 0.0
                while time.monotonic() < deadline:
                    latest = screen(name)
                    if changed_pixels(reference, latest) >= minimum_pixels:
                        now = time.monotonic()
                        if latest == previous:
                            if not stable_since:
                                stable_since = now
                            elif now - stable_since >= 0.5:
                                return latest
                        else:
                            previous = latest
                            stable_since = 0.0
                    else:
                        previous = latest
                        stable_since = 0.0
                    time.sleep(0.1)
                raise RuntimeError(
                    f"Explorer scanout for {name} changed by "
                    f"{changed_pixels(reference, latest)} pixels; expected at least "
                    f"{minimum_pixels} within {timeout:g}s"
                )

            execute("qmp_capabilities")
            startup_serial = serial_path.read_text(encoding="utf-8", errors="replace")
            irq_enabled = "[MicaNT Input] PS/2 mouse interrupt routing active" in startup_serial
            before = screen("before.ppm")
            # The first CPL3 scanout can precede Explorer's desktop paint and
            # focus setup. Let the real desktop surface settle before sending
            # the click/Enter sequence below.
            try:
                before = wait_for_changed_screen(
                    before, "desktop-ready.ppm", 1000, timeout=10.0,
                )
            except RuntimeError as error:
                cpl3_path = temp / "cpl3-surface.ppm"
                with cpl3_path.open("rb") as image:
                    if (image.readline() != b"P6\n" or
                            image.readline() != b"1024 768\n" or
                            image.readline() != b"255\n"):
                        raise RuntimeError("Unexpected initial CPL3 screenshot format") from error
                    initial_cpl3 = image.read()
                if changed_pixels(initial_cpl3, before) < 1000:
                    raise error
                print(
                    "Explorer desktop was already visible at the initial scanout.",
                    flush=True,
                )
            execute("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "x", "value": 40}},
                {"type": "rel", "data": {"axis": "y", "value": 20}},
            ]})
            time.sleep(0.5)
            after = screen("after.ppm")
            execute("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": True}},
            ]})
            time.sleep(0.1)
            execute("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": False}},
            ]})
            for down in (True, False):
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": down,
                        "key": {"type": "qcode", "data": "ret"},
                    },
                }]})
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if "[MicaNT Explorer] First application window created" in serial_path.read_text(
                    encoding="utf-8", errors="replace"
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Explorer did not dispatch Enter into an application window action")
            opened = wait_for_changed_screen(after, "app-window.ppm", 10000)
            for down in (True, False):
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": down,
                        "key": {"type": "qcode", "data": "ret"},
                    },
                }]})
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if "[MicaNT Explorer] Second overlapping application window created" in serial_path.read_text(
                    encoding="utf-8", errors="replace"
                ):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Explorer did not create its second overlapping user-mode window")
            overlapping = wait_for_changed_screen(opened, "overlapping-windows.ppm", 5000)
            # The second window starts at (222,119), size 500x320. Probe a
            # pixel inside its maximize button but away from the glyph/cursor.
            normal_caption_button = pixel_at(overlapping, 660, 129)
            move_to(672, 140)
            time.sleep(0.3)
            caption_hover = screen("caption-button-hover.ppm")
            hover_caption_button = pixel_at(caption_hover, 660, 129)
            if hover_caption_button == normal_caption_button:
                raise RuntimeError("Themed caption button hover did not change its pixel")
            set_left_button(True)
            time.sleep(0.2)
            caption_pressed = screen("caption-button-pressed.ppm")
            pressed_caption_button = pixel_at(caption_pressed, 660, 129)
            if pressed_caption_button == hover_caption_button:
                raise RuntimeError("Themed caption button pressed state did not change its pixel")
            set_left_button(False)
            resized_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window surface resized")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(
                    "[MicaNT Explorer] Application window surface resized"
                ) > resized_marker_count:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Maximize caption hit target did not resize the app surface")
            maximized = wait_for_changed_screen(
                caption_pressed, "caption-maximized-window.ppm", 5000)

            resized_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window surface resized")
            move_to(974, 19)
            set_left_button(True)
            set_left_button(False)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(
                    "[MicaNT Explorer] Application window surface resized"
                ) > resized_marker_count:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Restore caption hit target did not resize the app surface")
            restored = wait_for_changed_screen(
                maximized, "caption-restored-window.ppm", 5000)

            move_to(643, 140)
            set_left_button(True)
            set_left_button(False)
            minimized = wait_for_changed_screen(
                restored, "caption-minimized-window.ppm", 5000)
            move_to(300, 748)
            set_left_button(True)
            set_left_button(False)
            restored_from_taskbar = wait_for_changed_screen(
                minimized, "taskbar-restored-window.ppm", 5000)

            move_to(350, 140)
            moved_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window move notification received")
            set_left_button(True)
            move_by(40, 30)
            set_left_button(False)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(
                    "[MicaNT Explorer] Application window move notification received"
                ) > moved_marker_count:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Caption drag did not deliver WM_MOVE to Explorer")
            caption_dragged = wait_for_changed_screen(
                overlapping, "caption-dragged-window.ppm", 1000)

            move_to(760, 466)
            resized_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window surface resized")
            set_left_button(True)
            move_by(40, 35)
            set_left_button(False)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(
                    "[MicaNT Explorer] Application window surface resized"
                ) > resized_marker_count:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Bottom-right resize did not rebuild the Explorer surface")
            edge_resized = wait_for_changed_screen(
                caption_dragged, "edge-resized-window.ppm", 1000)

            # The initial bottom-right drag leaves the second app window at
            # x=262, y=149, 540x355. Exercise every remaining Win32k edge and
            # corner hit-test path with a bounded inward/outward delta.
            resize_cases = (
                ("left-edge-resized-window.ppm", 264, 326, -20, 0),
                ("right-edge-resized-window.ppm", 800, 326, 20, 0),
                ("top-edge-resized-window.ppm", 500, 151, 0, -20),
                ("bottom-edge-resized-window.ppm", 500, 502, 0, 20),
                ("top-left-resized-window.ppm", 244, 131, -10, -10),
                ("top-right-resized-window.ppm", 820, 121, 10, -10),
                ("bottom-left-resized-window.ppm", 234, 522, -10, 10),
                ("bottom-right-resized-window-2.ppm", 830, 532, 10, 10),
            )
            resize_reference = edge_resized
            for name, x, y, dx, dy in resize_cases:
                move_to(x, y)
                resized_marker_count = serial_path.read_text(
                    encoding="utf-8", errors="replace").count(
                        "[MicaNT Explorer] Application window surface resized")
                set_left_button(True)
                move_by(dx, dy)
                set_left_button(False)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    serial = serial_path.read_text(
                        encoding="utf-8", errors="replace")
                    if serial.count(
                        "[MicaNT Explorer] Application window surface resized"
                    ) > resized_marker_count:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f"Resize hit target did not deliver WM_SIZE: {name}")
                resize_reference = wait_for_changed_screen(
                    resize_reference, name, 1000)

            # Clamp both axes while keeping the opposite edge anchored. The
            # minimum outer size must still leave Explorer a 160x100 client.
            move_to(224, 326)
            resized_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window surface resized")
            set_left_button(True)
            drag_by(500, 0)
            set_left_button(False)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(
                    "[MicaNT Explorer] Application window surface resized"
                ) > resized_marker_count:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Minimum-width clamp did not rebuild a 160px client surface")
            resize_reference = wait_for_changed_screen(
                resize_reference, "minimum-width-window.ppm", 1000)

            move_to(750, 111)
            resized_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window surface resized")
            set_left_button(True)
            drag_by(0, 400)
            set_left_button(False)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(
                    "[MicaNT Explorer] Application window surface resized"
                ) > resized_marker_count:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Minimum-height clamp did not rebuild a 100px client surface")
            resize_reference = wait_for_changed_screen(
                resize_reference, "minimum-height-window.ppm", 1000)

            moved_resized = screen("moved-resized-window.ppm")
            # Click the exposed portion of the older My Computer window to
            # prove z-order activation; clicking the resized front window only
            # moves the cursor and produces no meaningful scanout change.
            move_to(240, 250)
            execute("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": True}},
            ]})
            time.sleep(0.1)
            execute("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": False}},
            ]})
            brought_forward = wait_for_changed_screen(
                moved_resized, "brought-forward-window.ppm", 1000)

            for close_index in range(2):
                if close_index == 0:
                    # At the end of the edge/min-size matrix the second app is
                    # 170px wide at x=672; activate its drawn close target.
                    move_to(821, 425)
                    set_left_button(True)
                    set_left_button(False)
                else:
                    for down in (True, False):
                        execute("input-send-event", {"events": [{
                            "type": "key", "data": {
                                "down": down,
                                "key": {"type": "qcode", "data": "esc"},
                            },
                        }]})
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    serial = serial_path.read_text(encoding="utf-8", errors="replace")
                    if serial.count("[MicaNT Explorer] Application window destroyed by its owner") >= close_index + 1:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError("Explorer did not independently destroy the active application window")
                if close_index == 0:
                    one_window = wait_for_changed_screen(
                        brought_forward, "one-window-after-close.ppm", 5000)
            destroyed_count = 2
            for cycle in range(3):
                for down in (True, False):
                    execute("input-send-event", {"events": [{
                        "type": "key", "data": {
                            "down": down,
                            "key": {"type": "qcode", "data": "ret"},
                        },
                    }]})
                reopened = wait_for_changed_screen(
                    after, f"reopen-cycle-{cycle + 1}.ppm", 5000)
                for down in (True, False):
                    execute("input-send-event", {"events": [{
                        "type": "key", "data": {
                            "down": down,
                            "key": {"type": "qcode", "data": "esc"},
                        },
                    }]})
                destroyed_count += 1
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    serial = serial_path.read_text(encoding="utf-8", errors="replace")
                    if serial.count("[MicaNT Explorer] Application window destroyed by its owner") >= destroyed_count:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError("Repeated Explorer window destruction did not complete")
                time.sleep(0.25)
                screen(f"closed-cycle-{cycle + 1}.ppm")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                irq_seen = "[MicaNT Input] PS/2 mouse IRQ byte consumed" in serial
                explorer_key_seen = (
                    "[MicaNT Explorer] Keyboard input queued to the active window" in serial
                )
                explorer_mouse_seen = (
                    "[MicaNT Explorer] Mouse click queued to the hit window" in serial
                )
                explorer_application_window_closed = (
                    serial.count("[MicaNT Explorer] Application window destroyed by its owner") >= 2
                )
                if (not irq_enabled or irq_seen) and explorer_mouse_seen and explorer_key_seen and explorer_application_window_closed:
                    desktop_light = wait_for_changed_screen(opened, "interactive.ppm", 10000)

                    # Keep a real app window alive across the Light-to-Dark
                    # switch, then repeat caption drag and edge resize against
                    # its updated decoration geometry.
                    move_to(40, 60)
                    set_left_button(True)
                    set_left_button(False)
                    for down in (True, False):
                        execute("input-send-event", {"events": [{
                            "type": "key", "data": {
                                "down": down,
                                "key": {"type": "qcode", "data": "ret"},
                            },
                        }]})
                    desktop_light = wait_for_changed_screen(
                        desktop_light, "live-window-light.ppm", 5000)
                    light_title_pixel = pixel_at(desktop_light, 230, 126)

                    def click_left() -> None:
                        set_left_button(True)
                        set_left_button(False)

                    # Open Start from the taskbar, then select the right-column
                    # Theme Appearance entry (row 5) to switch Light to Dark.
                    move_to(50, 748)
                    click_left()
                    time.sleep(0.3)
                    wait_for_changed_screen(desktop_light, "theme-menu-light.ppm", 5000)
                    move_to(250, 536)
                    click_left()
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(encoding="utf-8", errors="replace")
                        if "[MicaNT Theme] Runtime profile switch to Dark passed" in serial:
                            break
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("Explorer did not apply the Dark theme from Start")
                    time.sleep(0.5)
                    dark_screen = screen("theme-dark.ppm")
                    if changed_pixels(desktop_light, dark_screen) == 0:
                        raise RuntimeError("Runtime theme switch produced no scanout change")
                    if pixel_at(dark_screen, 230, 126) == light_title_pixel:
                        raise RuntimeError("Live titlebar pixel did not follow the Dark theme")

                    moved_marker_count = serial_path.read_text(
                        encoding="utf-8", errors="replace").count(
                            "[MicaNT Explorer] Application window move notification received")
                    move_to(300, 130)
                    set_left_button(True)
                    move_by(25, 20)
                    set_left_button(False)
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(
                            encoding="utf-8", errors="replace")
                        if serial.count(
                            "[MicaNT Explorer] Application window move notification received"
                        ) > moved_marker_count:
                            break
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("Caption drag failed after the live theme switch")
                    post_switch_move = wait_for_changed_screen(
                        dark_screen, "post-switch-caption-drag.ppm", 1000)

                    resized_marker_count = serial_path.read_text(
                        encoding="utf-8", errors="replace").count(
                            "[MicaNT Explorer] Application window surface resized")
                    move_to(745, 456)
                    set_left_button(True)
                    move_by(20, 20)
                    set_left_button(False)
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(
                            encoding="utf-8", errors="replace")
                        if serial.count(
                            "[MicaNT Explorer] Application window surface resized"
                        ) > resized_marker_count:
                            break
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("Edge resize failed after the live theme switch")
                    wait_for_changed_screen(
                        post_switch_move, "post-switch-edge-resize.ppm", 1000)

                    destroyed_count = serial.count(
                        "[MicaNT Explorer] Application window destroyed by its owner")
                    for down in (True, False):
                        execute("input-send-event", {"events": [{
                            "type": "key", "data": {
                                "down": down,
                                "key": {"type": "qcode", "data": "esc"},
                            },
                        }]})
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(
                            encoding="utf-8", errors="replace")
                        if serial.count(
                            "[MicaNT Explorer] Application window destroyed by its owner"
                        ) > destroyed_count:
                            break
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("Post-switch test window did not close cleanly")
                    return
                time.sleep(0.1)
            if irq_enabled and "[MicaNT Input] PS/2 mouse IRQ byte consumed" not in serial:
                raise RuntimeError("PS/2 IRQ route was enabled but no ISR byte reached the consumer")
            if "[MicaNT Explorer] Keyboard input queued to the active window" not in serial:
                raise RuntimeError("Keyboard input did not reach Explorer's active window queue")
            if "[MicaNT Explorer] Mouse click queued to the hit window" not in serial:
                raise RuntimeError("Mouse click did not reach an Explorer window queue")
            if "[MicaNT Explorer] Application window destroyed by its owner" not in serial:
                raise RuntimeError("Explorer did not dispatch Escape into window cleanup")
            raise RuntimeError("PS/2 mouse input did not reach the kernel consumer")


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
        if marker == "[MicaNT Display] OEM driver unavailable; GOP framebuffer pass-through active":
            candidates = (
                marker,
                "[MicaNT OEM] virtio_gpu.sys: VirtIO 1.0 2D scanout initialized",
            )
            matches = [(serial_log.find(candidate, cursor), candidate) for candidate in candidates]
            matches = [(position, candidate) for position, candidate in matches if position >= 0]
            if not matches:
                return index
            found, matched_marker = min(matches, key=lambda item: item[0])
            cursor = found + len(matched_marker)
            continue
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
        # The SEC_IMAGE import-preflight test deliberately asks the loader to
        # reject a fixture with an unresolved native import, then verifies
        # that all partially created image state was rolled back. This is an
        # expected negative test result, not a kernel bootstrap failure.
        if line.startswith("[MicaNT Ps] SEC_IMAGE initialization import binding failed status="):
            continue
        if (line.startswith("[MicaNT Ps] SEC_IMAGE initialization TLS inspection failed status=") and
                "[MicaNT Ps] Image callbacks rejection returned" in serial_log):
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
    memory_mib: int = 1024,
    taskmgr_selftest: bool = False,
) -> None:
    if memory_mib not in (256, 512, 1024, 2048, 4096):
        raise RuntimeError("unsupported QEMU RAM profile")
    if runtime_selftest and (interactive or verify_input):
        raise RuntimeError("runtime self-test halts before desktop/input; incompatible runner options")
    if timer_selftest and (interactive or verify_input or runtime_selftest):
        raise RuntimeError("timer self-test halts before process tests and desktop/input; incompatible runner options")
    if taskmgr_selftest and (interactive or verify_input or runtime_selftest or timer_selftest or network_test or usb_test):
        raise RuntimeError("Task Manager self-test requires the unattended desktop boot path")
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
        # Keep redistributable system fonts on the mounted system volume so
        # user-mode font consumers can load them through the normal file path.
        # The TTFs and their upstream notices are kept together in the source
        # tree under System/Fonts; do not embed font data in the kernel image.
        font_source_dir = ROOT / "System" / "Fonts"
        required_fonts = (
            "Selawik-Regular.ttf",
            "Selawik-Semibold.ttf",
            "LiberationSans-Regular.ttf",
            "LiberationSerif-Regular.ttf",
            "LiberationMono-Regular.ttf",
            "Carlito-Regular.ttf",
            "DejaVuSans-Regular.ttf",
        )
        missing_fonts = [name for name in required_fonts if not (font_source_dir / name).is_file()]
        if missing_fonts:
            raise RuntimeError(f"Required system font files are missing: {', '.join(missing_fonts)}")
        shutil.copytree(font_source_dir, esp / "MicaNT" / "System" / "Fonts")
        # Keep user-replaceable cursor files on the mounted system volume, not
        # embedded in User32, Win32k, or the kernel image.
        cursor_source_dir = ROOT / "System" / "Cursors" / "MiniMouse"
        required_cursor_files = ("ATTRIBUTIONS.md", "LICENSE", "mini_mouse_arrow.cur")
        missing_cursor_files = [name for name in required_cursor_files
                                if not (cursor_source_dir / name).is_file()]
        if missing_cursor_files:
            raise RuntimeError(
                f"Mini Mouse cursor theme is incomplete: {', '.join(missing_cursor_files)}")
        shutil.copytree(cursor_source_dir,
                        esp / "MicaNT" / "System" / "Cursors" / "MiniMouse")
        # Themes are ordinary Ring 3 data packages. Keep CSS and selected
        # generated assets on the system volume rather than embedding them in
        # User32, Win32k, the compositor, or the kernel image.
        theme_source_dir = ROOT / "System" / "Themes" / "WhiteSur"
        theme_profile_source = ROOT / "System" / "Themes" / "active.profile"
        required_theme_files = (
            "theme.manifest",
            "LICENSES/COPYING",
            "LICENSES/AUTHORS",
            "Light/gtk.css",
            "Light/native.style",
            "Dark/gtk.css",
            "Dark/native.style",
        )
        missing_theme_files = [name for name in required_theme_files
                               if not (theme_source_dir / name).is_file()]
        if missing_theme_files:
            raise RuntimeError(
                f"WhiteSur theme package is incomplete: {', '.join(missing_theme_files)}")
        if not theme_profile_source.is_file():
            raise RuntimeError("System theme selection profile is missing")
        shutil.copytree(theme_source_dir,
                        esp / "MicaNT" / "System" / "Themes" / "WhiteSur")
        shutil.copy2(theme_profile_source,
                     esp / "MicaNT" / "System" / "Themes" / "active.profile")
        icon_source_dir = ROOT / "System" / "Icons" / "WhiteSur"
        required_icon_files = ("COPYING", "AUTHORS", "ATTRIBUTIONS.md", "index.theme")
        missing_icon_files = [name for name in required_icon_files
                              if not (icon_source_dir / name).is_file()]
        if missing_icon_files:
            raise RuntimeError(
                f"WhiteSur icon theme is incomplete: {', '.join(missing_icon_files)}")
        shutil.copytree(icon_source_dir, esp / "MicaNT" / "System" / "Icons" / "WhiteSur")
        wallpaper_asset = ROOT / "assets" / "wallpaper.png"
        if not wallpaper_asset.is_file():
            raise RuntimeError(f"Packaged desktop wallpaper is missing: {wallpaper_asset}")
        shutil.copyfile(wallpaper_asset, system32_dir / "wallpaper.png")
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
        explorer_image = ROOT / "build" / "explorer.exe"
        if not explorer_image.is_file():
            raise RuntimeError("Explorer image was not built for the QEMU system volume")
        shutil.copyfile(explorer_image, system32_dir / explorer_image.name)
        if taskmgr_selftest:
            taskmgr_image = ROOT / "build" / "taskmgr.exe"
            if not taskmgr_image.is_file():
                raise RuntimeError("Task Manager image was not built for the QEMU system volume")
            shutil.copyfile(taskmgr_image, system32_dir / taskmgr_image.name)
            taskmgr_probe_image = ROOT / "build" / "taskmgr_probe.exe"
            if not taskmgr_probe_image.is_file():
                raise RuntimeError("Task Manager action probe was not built for the QEMU system volume")
            shutil.copyfile(taskmgr_probe_image, system32_dir / taskmgr_probe_image.name)
        compositor_image = ROOT / "build" / "compositor.exe"
        if not compositor_image.is_file():
            raise RuntimeError("Ring 3 compositor image was not built for the QEMU system volume")
        shutil.copyfile(compositor_image, system32_dir / compositor_image.name)
        shutil.copyfile(ROOT / "build" / "network-system32" / "ws2_32.dll",
                        system32_dir / "ws2_32.dll")
        shutil.copyfile(ROOT / "build" / "network-system32" / "dnsapi.dll",
                        system32_dir / "dnsapi.dll")
        for service_dll in ("crypt32.dll", "winhttp.dll"):
            shutil.copyfile(ROOT / "build" / "network-system32" / service_dll,
                            system32_dir / service_dll)
        # Keep the production System32 closure intact. The file-image probe
        # opens its own executable from this volume and should exercise the
        # same NTDLL that an interactive process will load.
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
            "-m", str(memory_mib),
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
                        if taskmgr_selftest:
                            if "Task Manager: native controls self-test failed." in serial_log:
                                raise RuntimeError("Task Manager tab, search or selection self-test failed")
                            if "Task Manager: native controls validated." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: process snapshot unavailable from the kernel provider." in serial_log:
                                raise RuntimeError("Task Manager syscall returned no valid process snapshot")
                            if "Task Manager: memory availability unavailable from the kernel provider." in serial_log:
                                raise RuntimeError("Task Manager syscall returned no valid memory availability snapshot")
                            if "Task Manager: CPU processor snapshot unavailable." in serial_log:
                                raise RuntimeError("Task Manager syscall returned no valid CPU performance snapshot")
                            if "Task Manager: native process image name unavailable." in serial_log:
                                raise RuntimeError("Task Manager process snapshot omitted its real image name")
                            if "Task Manager: authorized process termination denied." in serial_log:
                                raise RuntimeError("Task Manager could not terminate the authorized QEMU probe process")
                            if "Task Manager: kernel process snapshot received." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: memory availability snapshot received." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: CPU processor snapshot validated." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: process and thread CPU totals reconciled." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: process CPU times validated." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: process CPU times regressed." in serial_log:
                                raise RuntimeError("Task Manager process CPU counters regressed")
                            if "Task Manager: native process image name validated." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: authorized process termination verified." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
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
                                print("Explorer input, window cleanup and Light-to-Dark theme switch passed.", flush=True)
                            result = (
                                "Task Manager process-snapshot self-test" if taskmgr_selftest else
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
    parser.add_argument("--memory-mib", type=int, choices=(256, 512, 1024, 2048, 4096), default=1024)
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
    parser.add_argument("--taskmgr-selftest", action="store_true")
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
            args.memory_mib,
            args.taskmgr_selftest,
        )
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"QEMU boot test failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
