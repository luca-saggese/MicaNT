#!/usr/bin/env python3
"""Boot the verified modular kernel in QEMU/OVMF and check serial milestones."""

import argparse
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

from fat32_image import create_fat32_image, create_gpt_disk_image
from m10_tls_peer import M10TlsPeer

from verify_kernel_architecture import validate_build_manifest


ROOT = Path(__file__).resolve().parents[1]
HDA_RENDER_RATE = 48000
HDA_RENDER_CHANNELS = 2
HDA_RENDER_SAMPLE_WIDTH = 2
HDA_RENDER_MAX_SECONDS = 30
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
    expected_resolution: tuple[int, int] | None = None,
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
                expected_header = (
                    f"{expected_resolution[0]} {expected_resolution[1]}\n".encode()
                    if expected_resolution else b"1024 768\n"
                )
                if (image.readline() != b"P6\n" or
                        image.readline() != expected_header or
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


def verify_theme_test(
    qmp_path: Path,
    temp: Path,
    serial_path: Path,
    diagnostics_dir: Path | None = None,
    expected_resolution: tuple[int, int] | None = None,
) -> None:
    """Drive the dedicated Ring 3 theme client and compare real scanout pixels."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            if "QMP" not in json.loads(stream.readline()):
                raise RuntimeError("QEMU did not send a QMP greeting")

            def execute(command: str, arguments: dict | None = None) -> None:
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

            pointer = [0, 0]

            def move_pointer(x: int, y: int) -> None:
                for axis, index in (("x", 0), ("y", 1)):
                    remaining = (x if index == 0 else y) - pointer[index]
                    while remaining:
                        step = max(-64, min(64, remaining))
                        execute("input-send-event", {"events": [{
                            "type": "rel", "data": {"axis": axis, "value": step},
                        }]})
                        pointer[index] += step
                        remaining -= step
                        time.sleep(0.01)
                time.sleep(0.15)

            def move_to(x: int, y: int) -> None:
                move_pointer(x, y)

            def screen(name: str) -> bytes:
                path = temp / name
                execute("screendump", {"filename": str(path)})
                if diagnostics_dir is not None:
                    shutil.copyfile(path, diagnostics_dir / name)
                with path.open("rb") as image:
                    width, height = expected_resolution or (1024, 768)
                    if (image.readline() != b"P6\n" or
                            image.readline() != f"{width} {height}\n".encode() or
                            image.readline() != b"255\n"):
                        raise RuntimeError("Unexpected VirtIO scanout screenshot format")
                    pixels = image.read()
                if len(pixels) != width * height * 3:
                    raise RuntimeError("Truncated VirtIO scanout screenshot")
                return pixels

            def pixel_at(image: bytes, x: int, y: int) -> bytes:
                offset = (y * width + x) * 3
                return image[offset:offset + 3]

            startup = serial_path.read_text(encoding="utf-8", errors="replace")
            for marker in (
                "[MicaNT ThemeTest] Progress, tab, and tree classes initialized",
                "[MicaNT ThemeTest] Base snapshot published",
                "[MicaNT ThemeTest] Initial HTHEME snapshot pinned",
                "[MicaNT ThemeTest] Production control window started",
                "[MicaNT ThemeTest] Dynamic window table: 24 windows created, resized, and destroyed",
            ):
                if marker not in startup:
                    raise RuntimeError(f"theme_test.exe startup lacked required evidence: {marker}")
            if ("[MicaNT ThemeTest] Active WhiteSur Light loaded" not in startup and
                    "[MicaNT ThemeTest] Active WhiteSur Dark loaded" not in startup):
                raise RuntimeError("theme_test.exe startup did not load a WhiteSur profile")
            width, height = expected_resolution or (1024, 768)
            # The guest initializes its PS/2 cursor at the display center.
            pointer[:] = [width // 2, height // 2]
            client_probes = [(x, min(550, height - 1)) for x in (100, 300, 500)
                             if x < width]
            visible_since = 0.0
            startup_screen = b""
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                startup_screen = screen("theme-test-wait.ppm")
                samples = [pixel_at(startup_screen, x, y)
                           for x, y in client_probes]
                neutral_client_pixel = bool(samples) and all(
                    max(sample) - min(sample) < 32 for sample in samples)
                if neutral_client_pixel:
                    for channel in range(3):
                        values = [sample[channel] for sample in samples]
                        if max(values) - min(values) >= 24:
                            neutral_client_pixel = False
                            break
                now = time.monotonic()
                if neutral_client_pixel:
                    if not visible_since:
                        visible_since = now
                    elif now - visible_since >= 0.5:
                        break
                else:
                    visible_since = 0.0
                time.sleep(0.1)
            else:
                raise RuntimeError("Theme-test parent client did not reach scanout")
            if diagnostics_dir is not None:
                shutil.copyfile(temp / "theme-test-wait.ppm",
                                diagnostics_dir / "theme-test-start.ppm")
            print("theme_test.exe WhiteSur startup screenshot captured.", flush=True)

            def set_left_button(down: bool) -> None:
                execute("input-send-event", {"events": [{
                    "type": "btn", "data": {"button": "left", "down": down},
                }]})
                time.sleep(0.12)

            def different_pixels(first: bytes, second: bytes) -> int:
                return sum(first[offset:offset + 3] != second[offset:offset + 3]
                           for offset in range(0, min(len(first), len(second)), 3))

            def parent_client_visible(image: bytes, offset_x: int = 0,
                                      offset_y: int = 0) -> bool:
                points = [(x + offset_x, 550 + offset_y)
                          for x in (100, 300, 500)
                          if x + offset_x < width and 550 + offset_y < height]
                samples = [pixel_at(image, x, y) for x, y in points]
                return bool(samples) and all(
                    max(sample) - min(sample) < 32 for sample in samples
                ) and all(
                    max(sample[channel] for sample in samples) -
                    min(sample[channel] for sample in samples) < 24
                    for channel in range(3)
                )

            # Leave the pointer away from the window so the cursor itself does
            # not dominate the before/after comparison.
            move_to(max(0, width - 16), max(0, height - 16))
            time.sleep(0.25)
            before_drag = screen("theme-window-before-drag.ppm")
            caption_x = min(width - 40, 272)
            caption_y = 74
            if caption_x <= 72 or caption_y >= height:
                raise RuntimeError("Theme-test window caption is outside the scanout")
            move_to(caption_x, caption_y)
            set_left_button(True)
            move_pointer(caption_x + 60, caption_y + 30)
            time.sleep(0.25)
            set_left_button(False)
            dragged = screen("theme-window-dragged.ppm")
            drag_changed_pixels = different_pixels(before_drag, dragged)
            drag_surface_visible = parent_client_visible(dragged, 60, 30)

            move_to(caption_x + 60, caption_y + 30)
            set_left_button(True)
            move_pointer(caption_x, caption_y)
            time.sleep(0.25)
            set_left_button(False)
            move_to(max(0, width - 16), max(0, height - 16))
            time.sleep(0.25)
            returned = screen("theme-window-after-drag-return.ppm")
            return_surface_visible = parent_client_visible(returned)
            if drag_changed_pixels >= 10000 and drag_surface_visible and return_surface_visible:
                print("theme_test.exe caption drag and return reached scanout.", flush=True)
            else:
                print(
                    "theme_test.exe caption drag was not verified: "
                    f"changed={drag_changed_pixels}, "
                    f"shifted_client_visible={int(drag_surface_visible)}, "
                    f"returned_client_visible={int(return_surface_visible)}.",
                    flush=True,
                )


def verify_cmd_terminal(
    qmp_path: Path,
    temp: Path,
    diagnostics_dir: Path,
    doom_test: bool = False,
) -> None:
    """Type real commands into cmd.exe and retain terminal scanout evidence."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(qmp_path))
        with connection.makefile("rwb") as stream:
            if "QMP" not in json.loads(stream.readline()):
                raise RuntimeError("QEMU did not send a QMP greeting")

            def execute(command: str, arguments: dict | None = None) -> None:
                stream.write((json.dumps({
                    "execute": command, "arguments": arguments or {},
                }) + "\n").encode())
                stream.flush()
                while True:
                    line = stream.readline()
                    if not line:
                        raise RuntimeError("QMP closed during CMD terminal verification")
                    response = json.loads(line)
                    if "error" in response:
                        raise RuntimeError(f"QMP {command} failed: {response['error']}")
                    if "return" in response:
                        return

            execute("qmp_capabilities")

            def screen(name: str) -> bytes:
                path = temp / name
                execute("screendump", {"filename": str(path)})
                shutil.copyfile(path, diagnostics_dir / name)
                with path.open("rb") as image:
                    if (image.readline() != b"P6\n" or
                            image.readline() != b"1024 768\n" or
                            image.readline() != b"255\n"):
                        raise RuntimeError("Unexpected CMD scanout screenshot format")
                    pixels = image.read()
                if len(pixels) != 1024 * 768 * 3:
                    raise RuntimeError("Truncated CMD scanout screenshot")
                return pixels

            def region_diff(left: bytes, right: bytes) -> int:
                # Restrict comparisons to the terminal client area and ignore
                # animated desktop/cursor pixels around the window.
                changed = 0
                for y in range(122, 580):
                    row = y * 1024
                    for x in range(124, 876):
                        offset = (row + x) * 3
                        if left[offset:offset + 3] != right[offset:offset + 3]:
                            changed += 1
                return changed

            pointer = [512, 384]

            def move_to(x: int, y: int) -> None:
                for axis, index in (("x", 0), ("y", 1)):
                    remaining = (x if index == 0 else y) - pointer[index]
                    while remaining:
                        step = max(-64, min(64, remaining))
                        execute("input-send-event", {"events": [{
                            "type": "rel", "data": {"axis": axis, "value": step},
                        }]})
                        pointer[index] += step
                        remaining -= step
                        time.sleep(0.01)
                time.sleep(0.15)

            def move_by(dx: int, dy: int) -> None:
                for axis, distance in (("x", dx), ("y", dy)):
                    while distance:
                        step = max(-64, min(64, distance))
                        execute("input-send-event", {"events": [{
                            "type": "rel", "data": {"axis": axis, "value": step},
                        }]})
                        pointer[0 if axis == "x" else 1] += step
                        distance -= step
                time.sleep(0.2)

            def set_left_button(down: bool) -> None:
                execute("input-send-event", {"events": [{
                    "type": "btn", "data": {"button": "left", "down": down},
                }]})
                time.sleep(0.12)

            def drag(start_x: int, start_y: int, dx: int, dy: int) -> None:
                move_to(start_x, start_y)
                set_left_button(True)
                while dx or dy:
                    step_x = max(-64, min(64, dx))
                    step_y = max(-64, min(64, dy))
                    move_by(step_x, step_y)
                    dx -= step_x
                    dy -= step_y
                set_left_button(False)

            def wait_for_window_change(before: bytes, name: str,
                                       threshold: int = 1200) -> bytes:
                deadline = time.monotonic() + 5
                latest = before
                while time.monotonic() < deadline:
                    latest = screen(name)
                    if region_diff(before, latest) >= threshold:
                        time.sleep(0.25)
                        latest = screen(name)
                        if region_diff(before, latest) >= threshold:
                            return latest
                    time.sleep(0.1)
                raise RuntimeError(
                    f"CMD window action {name} produced no stable visible change; "
                    f"changed pixels: {region_diff(before, latest)}")

            def click_terminal() -> None:
                move_to(180, 160)
                for down in (True, False):
                    execute("input-send-event", {"events": [{
                        "type": "btn", "data": {"button": "left", "down": down},
                    }]})
                time.sleep(0.2)

            def key(qcode: str) -> None:
                for down in (True, False):
                    execute("input-send-event", {"events": [{
                        "type": "key", "data": {
                            "down": down, "key": {"type": "qcode", "data": qcode},
                        },
                    }]})
                time.sleep(0.075 if doom_test else 0.025)

            def hold_key(qcode: str, duration: float) -> None:
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": True, "key": {"type": "qcode", "data": qcode},
                    },
                }]})
                time.sleep(duration)
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": False, "key": {"type": "qcode", "data": qcode},
                    },
                }]})

            def shifted_key(qcode: str) -> None:
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": True, "key": {"type": "qcode", "data": "shift"},
                    },
                }]})
                key(qcode)
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": False, "key": {"type": "qcode", "data": "shift"},
                    },
                }]})

            def type_text(text: str) -> None:
                for character in text:
                    if character == " ":
                        qcode = "spc"
                    elif character == ".":
                        qcode = "dot"
                    elif character == "-":
                        qcode = "minus"
                    elif character == ":":
                        shifted_key("semicolon")
                        continue
                    elif character == "(":
                        shifted_key("9")
                        continue
                    elif character == ")":
                        shifted_key("0")
                        continue
                    elif "A" <= character <= "Z":
                        shifted_key(character.lower())
                        continue
                    elif "a" <= character <= "z" or "0" <= character <= "9":
                        qcode = character
                    else:
                        raise RuntimeError(f"CMD QMP helper cannot type {character!r}")
                    key(qcode)

            def echo_reference(text: str, name: str) -> tuple[bytes, tuple[int, int, int, int]]:
                """Render reference text through CMD/GDI and return its glyph bounds."""
                type_text("cls")
                key("ret")
                baseline = screen(name)
                type_text("echo " + text)
                key("ret")
                baseline = capture_after_input(baseline, name, "echo reference")
                # Find bright glyph pixels in the client area, then retain the
                # last glyph row band (the echo output, below the prompt line).
                rows: list[tuple[int, int, int]] = []
                for y in range(122, 580):
                    xs = [x for x in range(124, 876)
                          if max(baseline[(y * 1024 + x) * 3:(y * 1024 + x) * 3 + 3]) > 180]
                    if xs:
                        rows.append((y, min(xs), max(xs)))
                bands: list[list[tuple[int, int, int]]] = []
                for row in rows:
                    if not bands or row[0] > bands[-1][-1][0] + 1:
                        bands.append([row])
                    else:
                        bands[-1].append(row)
                if not bands:
                    raise RuntimeError(f"CMD echo reference produced no visible glyphs: {text}")
                band = bands[-1]
                return baseline, (min(r[1] for r in band), band[0][0],
                                  max(r[2] for r in band) + 1, band[-1][0] + 1)

            def assert_echo_text(target: bytes, reference: bytes,
                                 bounds: tuple[int, int, int, int], label: str) -> None:
                x0, y0, x1, y1 = bounds
                width, height = x1 - x0, y1 - y0
                points: list[tuple[int, int]] = []
                for row in range(height):
                    for column in range(width):
                        offset = ((y0 + row) * 1024 + x0 + column) * 3
                        if max(reference[offset:offset + 3]) > 100:
                            points.append((column, row))
                if not points:
                    raise RuntimeError(f"CMD {label} reference contains no glyph pixels")
                # Compare glyph pixels independently of the GDI text color;
                # status output is red while echo references use the normal
                # terminal text attribute.
                for y in range(122, 580 - height):
                    for x in range(124, 876 - width):
                        if all(max(target[((y + dy) * 1024 + x + dx) * 3:
                                          ((y + dy) * 1024 + x + dx) * 3 + 3]) > 100
                               for dx, dy in points):
                            return
                raise RuntimeError(f"CMD {label} output did not contain the expected visible text")

            last = screen("cmd-terminal-00-startup.ppm")
            click_terminal()

            def capture_after_input(before: bytes, name: str, label: str) -> bytes:
                deadline = time.monotonic() + 5
                latest = before
                stable = 0
                previous = before
                while time.monotonic() < deadline:
                    latest = screen(name)
                    if region_diff(before, latest) >= 50:
                        # The first changed frame often contains only the typed
                        # command. Wait for the terminal to finish painting its
                        # response before retaining the command screenshot.
                        if region_diff(previous, latest) < 4:
                            stable += 1
                        else:
                            stable = 0
                        if stable >= 2:
                            return latest
                    previous = latest
                    time.sleep(0.15)
                raise RuntimeError(
                    f"CMD terminal did not visibly update after {label}; "
                    f"changed pixels in client region: {region_diff(before, latest)}")

            def submit(command: str, name: str) -> bytes:
                nonlocal last
                type_text(command)
                key("ret")
                last = capture_after_input(last, name, command)
                return last

            if doom_test:
                shell_before = screen("doom-01-before-launch.ppm")
                # CMD resolves the executable extension from the current
                # System32 directory; omitting the period also avoids QMP's
                # layout-dependent punctuation key mapping.
                # Use the launch echo as the baseline. Comparing with the
                # pre-command terminal would mistake "Started process" text
                # for a game window.
                launch_output = submit("doom", "doom-02-launched.ppm")
                deadline = time.monotonic() + 60
                title = launch_output
                while time.monotonic() < deadline:
                    title = screen("doom-03-title.ppm")
                    if region_diff(launch_output, title) > 10000:
                        break
                    time.sleep(0.2)
                if region_diff(launch_output, title) <= 10000:
                    click_terminal()
                    try:
                        submit("echo doom process probe", "doom-03-process-state.ppm")
                    except RuntimeError:
                        screen("doom-03-process-state.ppm")
                    raise RuntimeError("doom.exe did not present its title screen in the guest")

                menu_frames = [title]
                for index, label in enumerate(("main-menu", "episode", "skill", "e1m1"), 1):
                    key("ret")
                    frame_name = f"doom-0{index + 3}-{label}.ppm"
                    deadline = time.monotonic() + (20 if label == "e1m1" else 5)
                    frame = menu_frames[-1]
                    while time.monotonic() < deadline:
                        frame = screen(frame_name)
                        if region_diff(menu_frames[-1], frame) >= 1500:
                            break
                        time.sleep(0.25)
                    if region_diff(menu_frames[-1], frame) < 1500:
                        raise RuntimeError(f"DOOM {label} screen did not visibly advance")
                    menu_frames.append(frame)
                level = menu_frames[-1]
                hold_key("up", 1.5)
                time.sleep(0.5)
                moved = screen("doom-08-e1m1-after-forward.ppm")
                if region_diff(level, moved) < 2500:
                    raise RuntimeError("DOOM E1M1 view did not change after forward movement")

                # The game window is at (80, 60), 656x439. Close its caption
                # and verify the original terminal can accept another command.
                move_to(718, 70)
                set_left_button(True)
                set_left_button(False)
                time.sleep(1.0)
                returned = screen("doom-09-returned-to-cmd.ppm")
                if region_diff(moved, returned) < 1000:
                    raise RuntimeError("closing DOOM did not return visibly to the CMD desktop")
                submit("echo doom closed", "doom-10-clean-exit.ppm")
                screen("doom-11-shell-after-game.ppm")
                print("DOOM E1M1 title/menu/level/movement and clean window exit passed.", flush=True)
                return

            # Backspace corrects the mistyped suffix before Enter reaches CMD.
            type_text("helpz")
            key("backspace")
            key("ret")
            last = capture_after_input(last, "cmd-terminal-01-help.ppm", "help/Backspace/Enter")
            submit("cd system32", "cmd-terminal-02-cd-system32.ppm")
            dir_reference, dir_bounds = echo_reference(
                "cmd-m0-known.txt", "cmd-terminal-ref-dir.ppm")
            type_reference, type_bounds = echo_reference(
                "luceos cmd m0 real fat file fixture", "cmd-terminal-ref-type.ppm")
            process_reference, process_bounds = echo_reference(
                "Started process", "cmd-terminal-ref-process.ppm")
            process_exit_reference, process_exit_bounds = echo_reference(
                "exited with code 0x00000000", "cmd-terminal-ref-process-exit.ppm")
            process_error_reference, process_error_bounds = echo_reference(
                "CreateProcessW failed (Win32 error", "cmd-terminal-ref-process-error.ppm")
            status_reference, status_bounds = echo_reference(
                "Command failed (NTSTATUS 0xC0000034).", "cmd-terminal-ref-status.ppm")
            submit("cls", "cmd-terminal-clear-before-dir.ppm")
            dir_screen = submit("dir", "cmd-terminal-03-dir.ppm")
            assert_echo_text(dir_screen, dir_reference, dir_bounds, "dir")
            submit("cls", "cmd-terminal-clear-before-type.ppm")
            type_screen = submit("type cmd-m0-known.txt", "cmd-terminal-04-type.ppm")
            assert_echo_text(type_screen, type_reference, type_bounds, "type")
            process_screen = submit("fileimage", "cmd-terminal-04-process-launch.ppm")
            assert_echo_text(process_screen, process_reference, process_bounds,
                             "file-backed PE process launch")
            process_exit_screen = submit("echo reap", "cmd-terminal-04-process-exit.ppm")
            assert_echo_text(process_exit_screen, process_exit_reference, process_exit_bounds,
                             "file-backed PE process exit status")
            submit("cd ..", "cmd-terminal-05-cd-parent.ppm")
            submit("cd cmd-m0-no-such-dir", "cmd-terminal-06-cd-missing.ppm")
            assert_echo_text(last, status_reference, status_bounds, "missing cd NTSTATUS")
            missing_file = submit("type cmd-m0-no-such-file.txt",
                                  "cmd-terminal-06-type-missing.ppm")
            assert_echo_text(missing_file, status_reference, status_bounds,
                             "missing type NTSTATUS")
            unknown_command = submit("cmd-m0-no-such-command",
                                     "cmd-terminal-06-command-unknown.ppm")
            assert_echo_text(unknown_command, process_error_reference,
                             process_error_bounds, "unknown command failure")

            key("up")
            history_screen = screen("cmd-terminal-07-history.ppm")
            if region_diff(last, history_screen) < 20:
                raise RuntimeError("CMD Up-arrow history did not change the terminal input")
            last = history_screen

            # Fill beyond one viewport, then check both scroll directions.
            for index in range(15):
                submit("help", f"cmd-terminal-history-{index:02d}.ppm")
            key("pgup")
            scroll_up = screen("cmd-terminal-08-scroll-up.ppm")
            if region_diff(last, scroll_up) < 20:
                raise RuntimeError("CMD PageUp did not scroll terminal history")
            last = scroll_up
            key("pgdn")
            scroll_down = screen("cmd-terminal-09-scroll-down.ppm")
            if region_diff(last, scroll_down) < 20:
                raise RuntimeError("CMD PageDown did not restore the terminal viewport")

            # The initial outer frame is positioned at (120, 90), sized
            # 760x500. Exercise each caption control on the standard system
            # frame, then drag and resize the restored window by actual QMP
            # mouse input. Retain every transition for visual diagnosis.
            move_to(835, 104)  # Maximize caption button.
            set_left_button(True)
            set_left_button(False)
            maximized = wait_for_window_change(
                scroll_down, "cmd-window-maximized.ppm", 5000)

            move_to(974, 19)  # Restore caption button on the maximized frame.
            set_left_button(True)
            set_left_button(False)
            restored = wait_for_window_change(
                maximized, "cmd-window-restored.ppm", 5000)

            move_to(806, 104)  # Minimize caption button.
            set_left_button(True)
            set_left_button(False)
            minimized = wait_for_window_change(
                restored, "cmd-window-minimized.ppm", 5000)
            move_to(300, 748)  # Restore from the taskbar.
            set_left_button(True)
            set_left_button(False)
            taskbar_restored = wait_for_window_change(
                minimized, "cmd-window-taskbar-restored.ppm", 5000)

            drag(350, 104, 30, 20)  # Move the titlebar.
            dragged = wait_for_window_change(
                taskbar_restored, "cmd-window-dragged.ppm", 1200)
            drag(908, 608, 25, 20)  # Resize from the bottom-right grip.
            resized = wait_for_window_change(
                dragged, "cmd-window-resized.ppm", 1200)

            move_to(912, 124)  # Close caption button after move and resize.
            set_left_button(True)
            set_left_button(False)
            closed = wait_for_window_change(
                resized, "cmd-window-closed.ppm", 1200)

            print(
                "CMD QMP key input, Backspace, history, real directory/file commands, "
                "scrolling, window controls and retained screenshots passed. Evidence: "
                f"{diagnostics_dir.resolve()}", flush=True)


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

            def send_key(key: str, down: bool) -> None:
                execute("input-send-event", {"events": [{
                    "type": "key", "data": {
                        "down": down, "key": {"type": "qcode", "data": key},
                    },
                }]})
                time.sleep(0.15)

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
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                startup_serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if ("[MicaNT Theme] GetThemeSysColor resolves themed button face" in startup_serial and
                        "[MicaNT Theme] GetThemeSysFont returns GDI font metadata and metrics" in startup_serial and
                        "[MicaNT Theme] GetThemeFont combines CSS snapshot fields with the GDI system face" in startup_serial and
                        "[MicaNT Theme] Userenv resolved profile path from current token SID" in startup_serial and
                        "[MicaNT Theme] Full notification queue rejected theme atomically and recovered" in startup_serial and
                        "[MicaNT Theme] Unavailable profile kept the previous complete theme" in startup_serial and
                        "[MicaNT Compositor] Window opacity, radius, shadow and Base pixels verified" in startup_serial and
                        "[MicaNT Theme] Auto schedule boundary Light/Dark pixels verified" in startup_serial and
                        "[MicaNT Theme] Malformed theme snapshot kept the previous complete theme" in startup_serial and
                        "[MicaNT Theme] Valid profile recovered and original theme restored" in startup_serial and
                        "[MicaNT Explorer] Missing icon fell back to the built-in desktop glyph" in startup_serial):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Explorer did not complete the pre-window theme probes")
            for axis, distance in (("x", -472), ("y", -364)):
                while distance:
                    step = max(-64, min(64, distance))
                    execute("input-send-event", {"events": [{
                        "type": "rel", "data": {"axis": axis, "value": step},
                    }]})
                    distance -= step
                    time.sleep(0.01)
            time.sleep(0.5)
            after = screen("after.ppm")
            paint_marker = "[MicaNT Explorer] Application window paint presented"
            paint_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(paint_marker)
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
            paint_deadline = time.monotonic() + 15
            while time.monotonic() < paint_deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(paint_marker) > paint_count:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Explorer application window did not present a WM_PAINT surface")
            opened = wait_for_changed_screen(after, "app-window.ppm", 10000, timeout=15)
            title_background = pixel_at(opened, 500, 105)
            title_text_rows = sum(
                any(pixel_at(opened, x, y) != title_background
                    for x in range(204, 450))
                for y in range(95, 130)
            )
            if title_text_rows <= 8:
                raise RuntimeError("Caption title did not use the shared GDI font raster")
            if "[MicaNT Theme] WhiteSur window icon loaded for caption" not in serial_path.read_text(
                encoding="utf-8", errors="replace"
            ):
                raise RuntimeError("Explorer did not load a WhiteSur caption icon")
            title_icon_colors = {
                pixel_at(opened, x, y)
                for y in range(95, 105) for x in range(204, 236)
                if pixel_at(opened, x, y) != title_background
            }
            if len(title_icon_colors) < 3:
                raise RuntimeError("WhiteSur window icon did not reach the caption pixels")
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if ("[MicaNT Theme] Buffered DIB paint init, discard, update and cleanup verified" in serial and
                        "[MicaNT Theme] DrawThemeText clips to bounds and restores DC state" in serial):
                    break
                if "[MicaNT Theme] Buffered DIB paint lifecycle mismatch" in serial:
                    raise RuntimeError("Buffered DIB paint lifecycle failed")
                if "[MicaNT Theme] DrawThemeText clipping or DC restore mismatch" in serial:
                    raise RuntimeError("DrawThemeText clipping or DC restore failed")
                time.sleep(0.1)
            else:
                raise RuntimeError("Explorer did not execute the Buffered Paint and DrawThemeText probes")
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if ("[MicaNT Theme] Production comctl32 progress control uses WhiteSur pixels" in serial and
                        "[MicaNT Theme] Production User32 BUTTON painted and BN_CLICKED delivered" in serial and
                        "[MicaNT Theme] BUTTON text messages update, read back and reject overflow" in serial and
                        "[MicaNT Theme] User32 child SetFocus/GetFocus round trip verified" in serial and
                        "[MicaNT Theme] User32 automatic/manual checkbox states and BN_CLICKED verified" in serial and
                        "[MicaNT Theme] User32 automatic/manual 3-state and radio checks verified" in serial and
                        "[MicaNT Theme] User32 auto-radio exclusive groups verified" in serial and
                        "[MicaNT Theme] Production User32 menu ownership and query round-trip verified" in serial and
                        "[MicaNT Theme] Production User32 popup mouse selection ready" in serial and
                        "[MicaNT Theme] SysTabControl32 init, item messages, selection and paint verified" in serial and
                        "[MicaNT Theme] SysTabControl32 keyboard focus and arrow request verified" in serial and
                        "[MicaNT Theme] Production User32 EDIT text, WM_CHAR, readonly and rejected styles verified" in serial and
                        "[MicaNT Theme] Production User32 SCROLLBAR range, page and position messages verified" in serial and
                        "[MicaNT Theme] Window SB_VERT range, page, position and arrows verified" in serial and
                        "[MicaNT Theme] Attached WS_HSCROLL/WS_VSCROLL pixels and range/page/position ready" in serial):
                    break
                if "[MicaNT Theme] User32 child focus round trip mismatch" in serial:
                    raise RuntimeError("Production User32 child SetFocus/GetFocus failed")
                if "[MicaNT Theme] Production User32 menu round-trip failed" in serial:
                    raise RuntimeError("Production User32 menu API round-trip failed")
                if "[MicaNT Theme] Production User32 menu bar creation failed" in serial:
                    raise RuntimeError("Production User32 menu bar could not be attached or painted")
                if "[MicaNT Theme] Production User32 popup creation failed" in serial:
                    raise RuntimeError("Production User32 popup menu could not be created")
                if "[MicaNT Theme] Production User32 BUTTON state/notification mismatch" in serial:
                    raise RuntimeError("Production User32 BUTTON state or notification failed")
                if "[MicaNT Theme] BUTTON text message mismatch" in serial:
                    raise RuntimeError("Production User32 BUTTON text messages failed")
                if "[MicaNT Theme] User32 checkbox state/notification mismatch" in serial or \
                        "[MicaNT Theme] User32 checkbox creation failed" in serial:
                    raise RuntimeError("Production User32 checkbox state or notification failed")
                if "[MicaNT Theme] User32 3-state/radio message mismatch" in serial or \
                        "[MicaNT Theme] User32 3-state/radio creation failed" in serial:
                    raise RuntimeError("Production User32 three-state/radio behavior failed")
                if "[MicaNT Theme] User32 auto-radio exclusivity mismatch" in serial or \
                        "[MicaNT Theme] User32 auto-radio group creation failed" in serial:
                    raise RuntimeError("Production User32 auto-radio exclusivity failed")
                if "[MicaNT Theme] SysTabControl32 message or creation mismatch" in serial or \
                        "[MicaNT Theme] SysTabControl32 initialization or creation failed" in serial:
                    raise RuntimeError("Production SysTabControl32 message behavior failed")
                if "[MicaNT Theme] Production User32 EDIT message mismatch" in serial or \
                        "[MicaNT Theme] Production User32 EDIT creation failed" in serial:
                    raise RuntimeError("Production User32 EDIT behavior failed")
                if "[MicaNT Theme] Production User32 SCROLLBAR message mismatch" in serial or \
                        "[MicaNT Theme] Production User32 SCROLLBAR creation failed" in serial:
                    raise RuntimeError("Production User32 SCROLLBAR behavior failed")
                if "[MicaNT Theme] Window SB_VERT state message mismatch" in serial:
                    raise RuntimeError("Production User32 window scrollbar state failed")
                if "[MicaNT Theme] Attached scrollbar state setup failed" in serial or \
                        "[MicaNT Theme] Attached scrollbar probe window creation failed" in serial:
                    raise RuntimeError("Attached WS_HSCROLL/WS_VSCROLL probe setup failed")
                time.sleep(0.1)
            else:
                raise RuntimeError("Explorer did not paint and click the production User32 BUTTON")
            menu_popup = screen("theme-menu-popup.ppm")
            menu_changed = sum(
                pixel_at(before, x, y) != pixel_at(menu_popup, x, y)
                for y in range(198, 278) for x in range(678, 922)
            )
            if menu_changed < 100:
                raise RuntimeError("Themed User32 popup did not change its screen pixels")
            move_to(720, 213)
            set_left_button(True)
            time.sleep(0.1)
            set_left_button(False)
            menu_mouse_command = "[MicaNT Theme] User32 popup mouse owner command received"
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if menu_mouse_command in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("User32 popup mouse selection did not send WM_COMMAND")
            keyboard_ready = "[MicaNT Theme] Production User32 popup keyboard selection ready"
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if keyboard_ready in serial_path.read_text(
                        encoding="utf-8", errors="replace"):
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("User32 popup did not reopen for keyboard selection")
            for qcode in ("down", "ret"):
                for down in (True, False):
                    execute("input-send-event", {"events": [{
                        "type": "key", "data": {"down": down,
                            "key": {"type": "qcode", "data": qcode}},
                    }]})
            keyboard_command = "[MicaNT Theme] User32 popup keyboard owner command received"
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if keyboard_command in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("User32 popup keyboard selection did not send WM_COMMAND")
            # TrackPopupMenu runs a modal message loop. Exercise the radio only
            # after both popup sessions have returned to Explorer's main loop.
            auto_radio_before = screen("theme-auto-radio-before.ppm")
            move_to(210, 428)
            set_left_button(True)
            time.sleep(0.1)
            set_left_button(False)
            deadline = time.monotonic() + 3
            radio_input_marker = "[MicaNT Theme] Physical auto-radio input kept one group selection"
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if radio_input_marker in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Physical auto-radio input did not preserve exclusivity")
            move_to(490, 500)
            auto_radio_after = screen("theme-auto-radio-after.ppm")
            radio_glyph_changed = sum(
                pixel_at(auto_radio_before, x, y) != pixel_at(auto_radio_after, x, y)
                for y in range(421, 434) for x in range(204, 217))
            if radio_glyph_changed < 1:
                raise RuntimeError("Auto-radio checked glyph did not change its screen pixels")
            menu_bar_ready = "[MicaNT Theme] Production User32 menu bar attached and painted"
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if menu_bar_ready in serial:
                    break
                if "[MicaNT Theme] Production User32 menu bar creation failed" in serial:
                    raise RuntimeError("Production User32 menu bar could not be attached or painted")
                time.sleep(0.05)
            else:
                raise RuntimeError("Explorer did not attach and paint the User32 menu bar")
            menu_bar = screen("theme-menu-bar.ppm")
            menu_bar_changed = sum(
                pixel_at(opened, x, y) != pixel_at(menu_bar, x, y)
                for y in range(130, 162) for x in range(190, 430)
            )
            if menu_bar_changed < 80:
                raise RuntimeError("Attached User32 menu bar did not change its client pixels")
            move_to(215, 145)
            set_left_button(True)
            time.sleep(0.1)
            set_left_button(False)
            menu_bar_command = "[MicaNT Theme] User32 menu bar owner command received"
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if menu_bar_command in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("User32 menu bar click did not send WM_COMMAND")
            checkbox_normal = screen("theme-checkbox-unchecked.ppm")
            move_to(590, 364)
            checkbox_hover = screen("theme-checkbox-hover.ppm")
            set_left_button(True)
            time.sleep(0.2)
            checkbox_pressed = screen("theme-checkbox-pressed.ppm")
            set_left_button(False)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if "[MicaNT Theme] Physical checkbox input toggled checked state" in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Physical checkbox input did not toggle its checked state")
            checkbox_checked = screen("theme-checkbox-checked.ppm")
            def checkbox_region_changed(left: bytes, right: bytes) -> int:
                return sum(pixel_at(left, x, y) != pixel_at(right, x, y)
                           for y in range(350, 378) for x in range(530, 680))
            if checkbox_region_changed(checkbox_normal, checkbox_hover) < 2 or \
                    checkbox_region_changed(checkbox_hover, checkbox_pressed) < 2 or \
                    checkbox_region_changed(checkbox_normal, checkbox_checked) < 2:
                raise RuntimeError("User32 checkbox state did not change its rendered pixels")
            move_to(590, 320)
            set_left_button(True)
            set_left_button(False)
            edit_normal = screen("theme-edit-normal.ppm")
            move_to(340, 314)
            set_left_button(True)
            set_left_button(False)
            move_to(490, 314)
            edit_focused = screen("theme-edit-focused.ppm")
            def edit_region_changed(left: bytes, right: bytes) -> int:
                return sum(pixel_at(left, x, y) != pixel_at(right, x, y)
                           for y in range(300, 328) for x in range(200, 480))
            if edit_region_changed(edit_normal, edit_focused) < 1:
                raise RuntimeError("User32 EDIT focus did not change its rendered pixels")
            tab_normal = screen("theme-tab-normal.ppm")
            move_to(440, 275)
            set_left_button(True)
            time.sleep(0.1)
            set_left_button(False)
            mouse_notification = "[MicaNT Theme] SysTabControl32 mouse selection notification verified"
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if mouse_notification in serial_path.read_text(
                        encoding="utf-8", errors="replace"):
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("SysTabControl32 mouse selection did not notify its parent")
            move_to(590, 500)
            tab_selected = screen("theme-tab-selected.ppm")
            def tab_region_changed(left: bytes, right: bytes) -> int:
                return sum(pixel_at(left, x, y) != pixel_at(right, x, y)
                           for y in range(260, 290) for x in range(200, 520))
            if tab_region_changed(tab_normal, tab_selected) < 4:
                raise RuntimeError("SysTabControl32 selection did not change tab pixels")
            send_key("left", True)
            send_key("left", False)
            keyboard_notification = "[MicaNT Theme] SysTabControl32 keyboard selection notification verified"
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if keyboard_notification in serial_path.read_text(
                        encoding="utf-8", errors="replace"):
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("SysTabControl32 left-key selection did not notify its parent")
            tab_keyboard_selected = screen("theme-tab-keyboard-selected.ppm")
            if tab_region_changed(tab_selected, tab_keyboard_selected) < 4:
                raise RuntimeError("SysTabControl32 keyboard selection did not change tab pixels")
            scrollbar_normal = screen("theme-scrollbar-normal.ppm")
            move_to(450, 389)
            scrollbar_hover = screen("theme-scrollbar-hover.ppm")
            def scrollbar_region_changed(left: bytes, right: bytes) -> int:
                return sum(pixel_at(left, x, y) != pixel_at(right, x, y)
                           for y in range(380, 400) for x in range(370, 550))
            if scrollbar_region_changed(scrollbar_normal, scrollbar_hover) < 1:
                raise RuntimeError("User32 SCROLLBAR hover did not change its rendered pixels")
            move_to(375, 389)
            set_left_button(True)
            set_left_button(False)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if "[MicaNT Theme] SCROLLBAR arrow input sent WM_HSCROLL" in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("User32 SCROLLBAR arrow did not notify WM_HSCROLL")
            scrollbar_scrolled = screen("theme-scrollbar-scrolled.ppm")
            if scrollbar_region_changed(scrollbar_normal, scrollbar_scrolled) < 1:
                raise RuntimeError("User32 SCROLLBAR arrow did not change its rendered thumb")

            attached_normal = screen("theme-attached-scroll-normal.ppm")
            move_to(342, 231)
            attached_hover = screen("theme-attached-scroll-hover.ppm")
            def attached_scroll_region_changed(left: bytes, right: bytes) -> int:
                return sum(pixel_at(left, x, y) != pixel_at(right, x, y)
                           for y in range(180, 300) for x in range(334, 350))
            if attached_scroll_region_changed(attached_normal, attached_hover) < 1:
                raise RuntimeError("Attached WS_VSCROLL hover did not change themed pixels")
            move_to(342, 188)
            set_left_button(True)
            set_left_button(False)
            deadline = time.monotonic() + 2
            arrow_marker = "[MicaNT Theme] Attached WS_VSCROLL arrow notified and position read back"
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if arrow_marker in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Attached WS_VSCROLL arrow failed notification or position readback")
            attached_arrow = screen("theme-attached-scroll-arrow.ppm")
            if attached_scroll_region_changed(attached_normal, attached_arrow) < 1:
                raise RuntimeError("Attached WS_VSCROLL arrow did not move themed pixels")
            move_to(342, 231)
            set_left_button(True)
            move_to(342, 252)
            time.sleep(0.1)
            set_left_button(False)
            deadline = time.monotonic() + 2
            thumb_marker = "[MicaNT Theme] Attached WS_VSCROLL thumb drag notified and position read back"
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if thumb_marker in serial:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Attached WS_VSCROLL thumb drag failed notification or position readback")
            attached_thumb = screen("theme-attached-scroll-thumb.ppm")
            if attached_scroll_region_changed(attached_arrow, attached_thumb) < 1:
                raise RuntimeError("Attached WS_VSCROLL thumb drag did not move themed pixels")

            button_normal = screen("theme-button-normal.ppm")
            move_to(590, 320)
            button_hover = screen("theme-button-hover.ppm")
            set_left_button(True)
            time.sleep(0.2)
            button_pressed = screen("theme-button-pressed.ppm")
            set_left_button(False)
            def button_region_changed(left: bytes, right: bytes) -> int:
                changed = 0
                for y in range(300, 340):
                    for x in range(530, 650):
                        if pixel_at(left, x, y) != pixel_at(right, x, y):
                            changed += 1
                return changed
            if button_region_changed(button_normal, button_hover) < 4:
                raise RuntimeError("User32 BUTTON hover did not change its rendered pixels")
            if button_region_changed(button_hover, button_pressed) < 4:
                raise RuntimeError("User32 BUTTON pressed state did not change its rendered pixels")
            # While the button owns capture, leaving its bounds must clear the
            # pressed visual without delivering a click. Reentering and
            # releasing inside must produce exactly one notification.
            notification = "[MicaNT Theme] BUTTON capture probe notification delivered"
            serial = serial_path.read_text(encoding="utf-8", errors="replace")
            notification_baseline = serial.count(notification)
            if notification_baseline < 1:
                raise RuntimeError("BUTTON notification probe did not reach its initial click")
            move_to(590, 320)
            set_left_button(True)
            drag_by(100, 0)
            button_outside = screen("theme-button-capture-outside.ppm")
            if button_region_changed(button_normal, button_outside) >= \
                    button_region_changed(button_normal, button_pressed):
                raise RuntimeError("BUTTON capture did not cancel pressed pixels outside its bounds")
            set_left_button(False)
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(notification) != notification_baseline:
                    raise RuntimeError("BUTTON release outside its bounds delivered a click")
                time.sleep(0.05)
            drag_by(-100, 0)
            set_left_button(True)
            drag_by(100, 0)
            drag_by(-100, 0)
            button_reentered = screen("theme-button-capture-reentered.ppm")
            if button_region_changed(button_hover, button_reentered) < 4:
                raise RuntimeError("BUTTON capture did not restore pressed pixels on reentry")
            set_left_button(False)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(notification) == notification_baseline + 1:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("BUTTON release inside its bounds did not deliver one click")
            keyboard_baseline = serial_path.read_text(
                encoding="utf-8", errors="replace").count(notification)
            move_to(590, 320)
            button_keyboard_hover = screen("theme-button-keyboard-hover.ppm")
            if button_region_changed(button_hover, button_keyboard_hover) < 4:
                raise RuntimeError("Focused BUTTON pixels did not differ from ordinary hover")
            send_key("spc", True)
            button_space_pressed = screen("theme-button-space-pressed.ppm")
            if button_region_changed(button_keyboard_hover, button_space_pressed) < 4:
                raise RuntimeError("Focused BUTTON Space keydown did not render pressed state")
            send_key("spc", False)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                serial = serial_path.read_text(encoding="utf-8", errors="replace")
                if serial.count(notification) == keyboard_baseline + 1:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Focused BUTTON Space keyup did not deliver one click")

            # Move focus to the parent window, then ensure Space no longer
            # activates the child control.
            move_to(400, 360)
            set_left_button(True)
            set_left_button(False)
            time.sleep(0.2)
            move_to(590, 320)
            button_unfocused_hover = screen("theme-button-unfocused-hover.ppm")
            if button_region_changed(button_keyboard_hover, button_unfocused_hover) < 4:
                raise RuntimeError("BUTTON focus indicator did not clear after focus moved")
            unfocused_baseline = serial_path.read_text(
                encoding="utf-8", errors="replace").count(notification)
            send_key("spc", True)
            send_key("spc", False)
            time.sleep(0.3)
            serial = serial_path.read_text(encoding="utf-8", errors="replace")
            if serial.count(notification) != unfocused_baseline:
                raise RuntimeError("Unfocused BUTTON activated from Space key input")
            # The first Explorer window is placed at (190,90); exercise its
            # first drive row and the parent button before opening another view.
            def click_at(x: int, y: int) -> None:
                move_to(x, y)
                execute("input-send-event", {"events": [
                    {"type": "btn", "data": {"button": "left", "down": True}},
                ]})
                time.sleep(0.08)
                execute("input-send-event", {"events": [
                    {"type": "btn", "data": {"button": "left", "down": False}},
                ]})

            if "[MicaNT Theme] Explorer row text uses active theme pixel" not in \
                    serial_path.read_text(encoding="utf-8", errors="replace"):
                raise RuntimeError("Explorer file row missed the active theme text color")

            serial = serial_path.read_text(encoding="utf-8", errors="replace")
            if "[MicaNT Explorer] Initial filesystem directory is empty" not in serial:
                opened_directory = False
                for row_index in range(8):
                    click_at(390, 218 + row_index * 26)
                    deadline = time.monotonic() + 1.5
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(encoding="utf-8", errors="replace")
                        if "[MicaNT Explorer] Filesystem directory enumerated after folder activation" in serial:
                            opened_directory = True
                            break
                        time.sleep(0.1)
                    if opened_directory:
                        break
                if not opened_directory:
                    raise RuntimeError("Explorer folder row did not open from mouse input")
                click_at(210, 150)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    serial = serial_path.read_text(encoding="utf-8", errors="replace")
                    if "[MicaNT Explorer] Parent filesystem directory enumerated" in serial:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError("Explorer parent-folder button did not navigate upward")
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
            caption_deadline = time.monotonic() + 5
            while time.monotonic() < caption_deadline:
                overlapping = screen("overlapping-windows.ppm")
                inactive_caption = pixel_at(overlapping, 280, 105)
                active_caption = pixel_at(overlapping, 385, 132)
                if inactive_caption != active_caption:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Active and inactive caption pixels did not differ")
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
            resized_marker_count = serial_path.read_text(
                encoding="utf-8", errors="replace").count(
                    "[MicaNT Explorer] Application window surface resized")
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
                    # The larger WhiteSur Theme Controls Test host is now the
                    # older window brought to front above; close its caption.
                    move_to(930, 116)
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
                # Closing the last app leaves no foreground target. Reactivate
                # the desktop before sending Enter to open the next cycle.
                paint_marker = "[MicaNT Explorer] Application window paint presented"
                paint_count = serial_path.read_text(
                    encoding="utf-8", errors="replace").count(paint_marker)
                paint_failure = "[MicaNT Explorer] Application window paint failed"
                paint_failure_count = serial_path.read_text(
                    encoding="utf-8", errors="replace").count(paint_failure)
                move_to(500, 600)
                execute("input-send-event", {"events": [
                    {"type": "btn", "data": {"button": "left", "down": True}},
                ]})
                time.sleep(0.08)
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
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    serial = serial_path.read_text(encoding="utf-8", errors="replace")
                    if serial.count(paint_marker) > paint_count:
                        break
                    if serial.count(paint_failure) > paint_failure_count:
                        raise RuntimeError("Reopened Explorer window failed to present its DIB")
                    time.sleep(0.05)
                else:
                    raise RuntimeError("Reopened Explorer window did not complete WM_PAINT")
                reopen_pixel_deadline = time.monotonic() + 5
                while time.monotonic() < reopen_pixel_deadline:
                    reopened = screen(f"reopen-cycle-{cycle + 1}.ppm")
                    if pixel_at(reopened, 280, 105) != pixel_at(after, 280, 105):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError("Reopened Explorer titlebar did not reach scanout")
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
                    light_title_pixel = pixel_at(desktop_light, 240, 126)
                    light_address_field_pixel = pixel_at(desktop_light, 280, 160)
                    light_start_surface_pixel = pixel_at(desktop_light, 90, 733)

                    def click_left() -> None:
                        set_left_button(True)
                        set_left_button(False)

                    # Open Start from the taskbar, then select the right-column
                    # Theme Appearance entry (row 5) to switch Light to Dark.
                    move_to(50, 748)
                    click_left()
                    time.sleep(0.3)
                    light_start_menu = wait_for_changed_screen(
                        desktop_light, "theme-menu-light.ppm", 5000)
                    light_start_menu_pixel = pixel_at(light_start_menu, 330, 440)
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
                    if pixel_at(dark_screen, 240, 126) == light_title_pixel:
                        raise RuntimeError("Live titlebar pixel did not follow the Dark theme")
                    if pixel_at(dark_screen, 90, 733) == light_start_surface_pixel:
                        raise RuntimeError("Taskbar Start surface pixel did not follow the Dark theme")
                    if pixel_at(dark_screen, 280, 160) == light_address_field_pixel:
                        raise RuntimeError("Explorer address field pixel did not follow the Dark theme")
                    move_to(50, 748)
                    click_left()
                    time.sleep(0.3)
                    dark_start_menu = screen("theme-menu-dark.ppm")
                    if pixel_at(dark_start_menu, 330, 440) == light_start_menu_pixel:
                        raise RuntimeError("Start menu surface pixel did not follow the Dark theme")
                    move_to(50, 748)
                    click_left()
                    if serial_path.read_text(encoding="utf-8", errors="replace").count(
                        "[MicaNT Theme] GetThemeSysColor resolves themed button face") < 2:
                        raise RuntimeError("GetThemeSysColor did not follow the runtime theme reload")

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
                    post_switch_resize = wait_for_changed_screen(
                        post_switch_move, "post-switch-edge-resize.ppm", 1000)

                    # The current live window is at (247,139), approximately
                    # 520x340 after the preceding caption drag and edge resize.
                    # Capture pressed-state pixels away from the maximize glyph,
                    # releasing outside the button so the window geometry stays put.
                    caption_button_point = (705, 149)
                    caption_button_center = (716, 157)
                    move_to(*caption_button_center)
                    set_left_button(True)
                    time.sleep(0.2)
                    dark_pressed = screen("caption-dark-pressed.ppm")
                    dark_accent_pixel = pixel_at(dark_pressed, *caption_button_point)
                    move_to(680, 157)
                    set_left_button(False)

                    move_to(50, 748)
                    click_left()
                    time.sleep(0.3)
                    move_to(250, 536)
                    click_left()
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(encoding="utf-8", errors="replace")
                        if "[MicaNT Theme] Runtime profile switch to Light Purple passed" in serial:
                            break
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("Explorer did not apply the Light Purple theme from Start")
                    move_to(*caption_button_center)
                    set_left_button(True)
                    time.sleep(0.2)
                    purple_pressed = screen("caption-purple-pressed.ppm")
                    purple_accent_pixel = pixel_at(purple_pressed, *caption_button_point)
                    move_to(680, 157)
                    set_left_button(False)
                    if dark_accent_pixel == purple_accent_pixel:
                        raise RuntimeError("Light Purple accent did not change the caption button pixel")
                    if purple_accent_pixel != bytes((0x9A, 0x57, 0xA3)):
                        raise RuntimeError(
                            f"Light Purple caption button pixel was {purple_accent_pixel}, expected (154, 87, 163)")
                    move_to(50, 748)
                    click_left()
                    time.sleep(0.3)
                    purple_start = screen("theme-menu-purple.ppm")
                    purple_start_pixel = pixel_at(purple_start, 100, 360)
                    if purple_start_pixel != bytes((0x9A, 0x57, 0xA3)):
                        raise RuntimeError(
                            f"Light Purple Start surface pixel was {purple_start_pixel}, expected (154, 87, 163)")
                    move_to(250, 536)
                    click_left()
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        serial = serial_path.read_text(encoding="utf-8", errors="replace")
                        if ("[MicaNT Theme] Auto policy selected Light (06:00-18:00)" in serial or
                                "[MicaNT Theme] Auto policy selected Dark (18:00-06:00)" in serial):
                            break
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("Explorer did not expose the automatic theme policy")

                    move_to(500, 146)
                    click_left()
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


def validate_hda_render_wav(path: Path) -> tuple[int, int]:
    """Require a bounded RIFF PCM capture with nonzero signed-16 stereo samples."""
    if not path.is_file():
        raise RuntimeError(f"HDA render capture was not created: {path}")
    maximum_frames = HDA_RENDER_RATE * HDA_RENDER_MAX_SECONDS
    maximum_bytes = maximum_frames * HDA_RENDER_CHANNELS * HDA_RENDER_SAMPLE_WIDTH + (1 << 20)
    file_size = path.stat().st_size
    if file_size < 44 or file_size > maximum_bytes:
        raise RuntimeError(
            f"HDA render WAV size outside bounds ({file_size} bytes; "
            f"expected 44..{maximum_bytes})"
        )

    # QEMU's wav audiodev emits the canonical PCM chunks but leaves the two
    # size fields at zero on a normal QMP quit. Once QEMU has closed the file,
    # fill those container lengths from the bounded file size; do not touch PCM.
    with path.open("r+b") as capture:
        header = capture.read(44)
        if (header[:4] == b"RIFF" and header[8:16] == b"WAVEfmt " and
                header[36:40] == b"data" and header[4:8] == b"\0" * 4 and
                header[40:44] == b"\0" * 4):
            capture.seek(4)
            capture.write(struct.pack("<I", file_size - 8))
            capture.seek(40)
            capture.write(struct.pack("<I", file_size - 44))

    try:
        with wave.open(str(path), "rb") as capture:
            if (capture.getcomptype() != "NONE" or
                    capture.getnchannels() != HDA_RENDER_CHANNELS or
                    capture.getsampwidth() != HDA_RENDER_SAMPLE_WIDTH or
                    capture.getframerate() != HDA_RENDER_RATE):
                raise RuntimeError(
                    "HDA render WAV must be uncompressed signed 16-bit stereo at 48 kHz "
                    f"(got {capture.getcomptype()}, {capture.getnchannels()}ch, "
                    f"{capture.getsampwidth() * 8}bit, {capture.getframerate()}Hz)"
                )
            frame_count = capture.getnframes()
            if frame_count <= 0 or frame_count > maximum_frames:
                raise RuntimeError(
                    f"HDA render WAV frame count outside bounds: {frame_count}"
                )

            # Three unity-clock source frames begin at -32768. With each
            # stream at 0.125 gain, their checked mix is -12288 per channel.
            first_frame = capture.readframes(1)
            if len(first_frame) != 4 or struct.unpack("<hh", first_frame) != (-12288, -12288):
                raise RuntimeError(
                    "HDA render WAV does not begin with the expected three-stream mix"
                )
            spectral_frames = min(frame_count, 4096)
            spectral_data = capture.readframes(spectral_frames)
            if len(spectral_data) != spectral_frames * 4:
                raise RuntimeError("HDA render WAV ended before the mixed sample window")
            left_samples = struct.unpack("<%dh" % (spectral_frames * 2),
                                         spectral_data)[::2]
            for frequency in (440, 660, 880):
                coefficient = 2.0 * math.cos(2.0 * math.pi * frequency / HDA_RENDER_RATE)
                previous = previous2 = current = 0.0
                for sample in left_samples:
                    current = sample + coefficient * previous - previous2
                    previous2, previous = previous, current
                amplitude = math.sqrt(max(
                    previous * previous + previous2 * previous2 -
                    coefficient * previous * previous2, 0.0
                )) / spectral_frames
                if amplitude < 500.0:
                    raise RuntimeError(
                        f"HDA render WAV is missing the mixed {frequency} Hz source "
                        f"(measured amplitude {amplitude:.1f})"
                    )
            capture.rewind()

            peak = 0
            nonzero_samples = 0
            frames_read = 0
            while frames_read < frame_count:
                request = min(8192, frame_count - frames_read)
                data = capture.readframes(request)
                expected_bytes = request * HDA_RENDER_CHANNELS * HDA_RENDER_SAMPLE_WIDTH
                if len(data) != expected_bytes:
                    raise RuntimeError("HDA render WAV data chunk is truncated")
                for (sample,) in struct.iter_unpack("<h", data):
                    magnitude = abs(sample)
                    if magnitude:
                        nonzero_samples += 1
                        peak = max(peak, magnitude)
                frames_read += request
    except (wave.Error, EOFError) as error:
        raise RuntimeError(f"HDA render capture is not a valid RIFF/WAVE file: {error}") from error

    if nonzero_samples == 0:
        raise RuntimeError("HDA render WAV contains only silent PCM samples")
    return frame_count, peak


def stop_qemu_cleanly(qmp_path: Path, process: subprocess.Popen) -> None:
    """Ask QEMU to quit so audio backends finalize their container headers."""
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(5)
            connection.connect(str(qmp_path))
            with connection.makefile("rwb") as stream:
                stream.readline()  # QMP greeting.
                stream.write(b'{"execute":"qmp_capabilities"}\n')
                stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if "error" in response:
                        raise RuntimeError(f"QMP negotiation failed: {response['error']}")
                    if "return" in response:
                        break
                stream.write(b'{"execute":"quit"}\n')
                stream.flush()
        process.wait(timeout=10)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


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
        if (line.startswith("[MicaNT TaskmgrTest] Process output buffer probe failed") and
                "Task Manager: short and invalid query buffers rejected." in serial_log):
            continue
        if ("[MicaNT Theme] UxTheme failed margin query clears its output" in line and
                "[MicaNT Theme] UxTheme failed margin query output mismatch" not in line):
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
        if (line.startswith("[MicaNT Io] Boot-start driver failed: ehci.sys") and
                "[MicaNT OEM] ehci.sys: PCI class 0C/03/20 controller not found" in serial_log):
            continue
        return True
    return False


def run_crash_dump_two_boots(
    command: list[str], ahci_image: Path, temp: Path,
    log_directory: Path, timeout: int, stable_seconds: int,
) -> None:
    """Persist one controlled crash across two real boots and decode its FAT export."""
    if "-snapshot" not in command:
        raise RuntimeError("crash dump self-test expected the normal snapshot option")
    persistent_command = [
        arg.replace("fat:ro:", "fat:rw:") if arg.startswith("file=fat:ro:") else arg
        for arg in command if arg != "-snapshot"
    ]
    extracted = log_directory / "recovered-minidump.dmp"
    phase_specs = (
        ("crash-first-boot", "[LuceOS Crash] persistent dump committed=1"),
        ("crash-recovery-boot", "[LuceCrashRecovery] validated, flushed, and acknowledged "),
    )

    for phase, marker in phase_specs:
        phase_serial = log_directory / f"{phase}.serial.log"
        phase_qemu_log = log_directory / f"{phase}.qemu.log"
        phase_qmp = temp / f"{phase}.qmp.sock"
        phase_command = list(persistent_command)
        for index, arg in enumerate(phase_command[:-1]):
            if arg == "-serial":
                phase_command[index + 1] = f"file:{phase_serial}"
            elif arg == "-qmp":
                phase_command[index + 1] = f"unix:{phase_qmp},server=on,wait=off"

        print(f"Starting {phase} with persistent AHCI image {ahci_image}", flush=True)
        started = time.monotonic()
        with phase_qemu_log.open("w", encoding="utf-8") as qemu_output:
            process = subprocess.Popen(
                phase_command, cwd=ROOT, stdout=qemu_output,
                stderr=subprocess.STDOUT,
            )
            try:
                while time.monotonic() - started < timeout:
                    if phase_serial.is_file():
                        serial = phase_serial.read_text(encoding="utf-8", errors="replace")
                        if marker in serial:
                            if phase == "crash-first-boot":
                                if "[LuceOS Crash] persistent dump committed=0" in serial:
                                    raise RuntimeError("kernel reported that crash dump persistence failed")
                                print("First boot committed the persistent crash record.", flush=True)
                            else:
                                match = re.search(
                                    r"\[LuceCrashRecovery\] validated, flushed, and acknowledged "
                                    r"(C:\\Windows\\Minidump\\LUCEOS-[0-9A-Fa-f]{16}\.dmp)",
                                    serial,
                                )
                                if not match:
                                    raise RuntimeError("recovery marker lacked the expected Minidump path")
                                expected_name = match.group(1).rsplit("\\", 1)[-1]
                                print(f"Ring 3 recovery confirmed {match.group(1)}", flush=True)
                            break
                        if "[LuceOS Crash] persistent dump committed=0" in serial:
                            raise RuntimeError("kernel could not persist the crash record")
                    return_code = process.poll()
                    if return_code is not None:
                        raise RuntimeError(
                            f"QEMU exited during {phase} before the required serial marker "
                            f"(exit={return_code})"
                        )
                    time.sleep(0.1)
                else:
                    tail = (phase_serial.read_text(encoding="utf-8", errors="replace")[-8000:]
                            if phase_serial.is_file() else "(no serial output)")
                    raise RuntimeError(f"timeout waiting for {phase} marker:\n{tail}")

                if phase == "crash-first-boot":
                    shutil.copyfile(phase_serial, log_directory / "serial.log")
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                    continue

                stable_start = time.monotonic()
                while time.monotonic() - stable_start < stable_seconds:
                    if process.poll() is not None:
                        raise RuntimeError("recovery boot halted before the stable persistence interval")
                    time.sleep(0.1)
                shutil.copyfile(phase_serial, log_directory / "serial.log")
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                shutil.copyfile(ahci_image, log_directory / "crash-dump-disk.img")
                extracted_result = subprocess.run(
                    [sys.executable, str(ROOT / "scripts" / "extract_gpt_fat32_file.py"),
                     str(ahci_image), str(extracted)],
                    cwd=ROOT, check=True, capture_output=True, text=True,
                )
                match_name = expected_name
                if not match_name.startswith("LUCEOS-") or not match_name.endswith(".dmp"):
                    raise RuntimeError("Ring 3 recovery exported an unexpected filename")
                if f"FAT file: {match_name}" not in extracted_result.stdout:
                    raise RuntimeError(
                        "the recovered FAT directory entry does not match the acknowledged filename"
                    )
                print(extracted_result.stdout.strip(), flush=True)
                decoded = subprocess.run(
                    [sys.executable, str(ROOT / "scripts" / "read_lud.py"),
                     str(extracted), "--json"],
                    cwd=ROOT, check=True, capture_output=True, text=True,
                )
                report = json.loads(decoded.stdout)
                if not report.get("complete") or report.get("errors"):
                    raise RuntimeError("host decoder rejected the recovered dump structure or CRC")
                if not report.get("header", {}).get("kernel_crash"):
                    raise RuntimeError("recovered file is not marked as a kernel crash dump")
                print(
                    f"Recovered and host-decoded {match_name}; CRC/structure valid, "
                    f"records={len(report.get('records', []))}.", flush=True,
                )
                return
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()

    raise RuntimeError("two-boot crash dump verification ended without an exported dump")


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
    graphics_backend: str = "auto",
    theme_test: bool = False,
    process_memory_selftest: bool = False,
    cmd_test: bool = False,
    doom_test: bool = False,
    crash_dump_selftest: bool = False,
    resolution: tuple[int, int] | None = None,
    usb_ehci_test: bool = False,
    audio_controller_test: bool = False,
    audio_render_test: bool = False,
    audio_capture_test: bool = False,
) -> None:
    if memory_mib not in (256, 500, 512, 1024, 2048, 4096):
        raise RuntimeError("unsupported QEMU RAM profile")
    if runtime_selftest and (interactive or verify_input):
        raise RuntimeError("runtime self-test halts before desktop/input; incompatible runner options")
    if timer_selftest and (interactive or verify_input or runtime_selftest):
        raise RuntimeError("timer self-test halts before process tests and desktop/input; incompatible runner options")
    if usb_test and usb_ehci_test:
        raise RuntimeError("choose either the xHCI USB test or the EHCI USB test")
    if taskmgr_selftest and (interactive or verify_input or runtime_selftest or timer_selftest or network_test or usb_test or usb_ehci_test):
        raise RuntimeError("Task Manager self-test requires the unattended desktop boot path")
    if theme_test and (verify_input or runtime_selftest or timer_selftest or network_test or usb_test or usb_ehci_test or taskmgr_selftest):
        raise RuntimeError("Theme test cannot be combined with unrelated automated desktop runners")
    if process_memory_selftest and (interactive or verify_input or runtime_selftest or timer_selftest or network_test or usb_test or usb_ehci_test):
        raise RuntimeError("Process-memory self-test requires the unattended desktop boot path")
    if cmd_test and (verify_input or runtime_selftest or timer_selftest or
                     network_test or usb_test or usb_ehci_test or taskmgr_selftest or theme_test or
                     process_memory_selftest or verify_usb_input):
        raise RuntimeError("CMD test requires its dedicated QMP input path")
    if cmd_test and memory_mib != 512:
        raise RuntimeError("CMD test must run within its 500 MiB profile: set --memory-mib 512")
    if doom_test and not cmd_test:
        raise RuntimeError("Doom test requires the CMD graphical terminal QMP profile")
    if cmd_test and diagnostics_dir is None:
        raise RuntimeError("CMD test requires --diagnostics-dir to retain screenshots and evidence")
    if crash_dump_selftest and (interactive or verify_input or verify_usb_input or
                                runtime_selftest or timer_selftest or network_test or
                                usb_test or usb_ehci_test or taskmgr_selftest or theme_test or
                                process_memory_selftest or cmd_test):
        raise RuntimeError("Crash dump self-test requires its isolated two-boot profile")
    if crash_dump_selftest and diagnostics_dir is None:
        raise RuntimeError("Crash dump self-test requires --diagnostics-dir to retain the disk image and logs")
    if process_memory_selftest and (interactive or verify_input or runtime_selftest or timer_selftest or network_test or usb_test or usb_ehci_test):
        raise RuntimeError("Process-memory self-test requires the unattended desktop boot path")
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

    device_help = subprocess.run(
        [qemu, "-device", "help"], capture_output=True, text=True, check=False
    )
    display_help = subprocess.run(
        [qemu, "-display", "help"], capture_output=True, text=True, check=False
    )
    device_listing = device_help.stdout + device_help.stderr
    display_listing = display_help.stdout + display_help.stderr
    display_backend = display.split(",", 1)[0]
    virgl_device_available = "virtio-vga-gl" in device_listing
    gl_display_available = (
        display_backend in {"gtk", "sdl"} and display_backend in display_listing
    )
    virgl_available = virgl_device_available and gl_display_available
    virgl_probe_error = ""
    if virgl_available:
        try:
            probe = subprocess.run(
                [qemu, "-machine", "q35", "-nodefaults", "-m", "128",
                 "-display", f"{display_backend},gl=on", "-device", "virtio-vga-gl", "-S"],
                capture_output=True, text=True, check=False, timeout=3,
            )
            virgl_available = probe.returncode == 0
            if not virgl_available:
                virgl_probe_error = (probe.stderr or probe.stdout).strip()
        except subprocess.TimeoutExpired:
            # QEMU stayed alive with the requested device and GL display
            # initialized. subprocess.run terminates the short-lived probe.
            virgl_available = True
        except OSError as error:
            virgl_available = False
            virgl_probe_error = str(error)
    if graphics_backend == "virgl" and not virgl_available:
        if not virgl_device_available:
            reason = "virtio-vga-gl is absent from the installed QEMU device list"
        elif not gl_display_available:
            reason = "the selected display is not an available GTK/SDL GL backend"
        else:
            reason = "the QEMU GL startup probe failed"
            if virgl_probe_error:
                reason += f": {virgl_probe_error}"
        raise RuntimeError(
            f"VirGL was required, but {reason}"
        )
    use_virgl = graphics_backend == "virgl" or (
        graphics_backend == "auto" and virgl_available
    )
    qemu_display = f"{display_backend},gl=on" if use_virgl else display
    print(
        "QEMU graphics backend: " + ("VirGL" if use_virgl else "VirtIO 2D"),
        flush=True,
    )
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
        if resolution:
            (kernel_dir / "boot.ini").write_text(
                f"timeout=0\nresolution={resolution[0]}x{resolution[1]}\n",
                encoding="ascii",
            )
        driver_dir = kernel_dir / "System32" / "drivers"
        driver_dir.mkdir(parents=True)
        shutil.copyfile(ROOT / "build" / "virtio_gpu.sys", driver_dir / "virtio_gpu.sys")
        for name in ("storahci.sys", "hidusb.sys", "usbstor.sys", "usbxhci.sys", "ehci.sys", "i210.sys", "virtio_net.sys"):
            source = ROOT / "build" / name
            if source.is_file():
                shutil.copyfile(source, driver_dir / name)
        # The FAT32 data volume is the system volume: FsRtl selects it by the
        # MicaNT\System32\ntdll.dll marker at the volume root, and the Ps
        # loader opens the real DLLs from there for file-backed SEC_IMAGE.
        system32_dir = esp / "MicaNT" / "System32"
        system32_dir.mkdir(parents=True)
        # The crash recovery service only opens this directory after restart;
        # the fatal path itself writes to the dedicated reserved partition.
        (esp / "Windows" / "Minidump").mkdir(parents=True)
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
        if theme_test:
            # A truncated compiled-style fixture exercises parsing/validation
            # rollback independently from the missing-profile case.
            malformed_style = (
                esp / "MicaNT" / "System" / "Themes" / "WhiteSur" /
                "Probe" / "native.style"
            )
            malformed_style.parent.mkdir(parents=True, exist_ok=True)
            malformed_style.write_bytes(b"MicaNT invalid compiled theme fixture\n")
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
        if cmd_test:
            fixture = system32_dir / "cmd-m0-known.txt"
            fixture.write_bytes("luceos cmd m0 real fat file fixture\r\n".encode("utf-8"))
            shutil.copyfile(fixture, diagnostics_dir / fixture.name)
        if audio_render_test:
            audio_probe = ROOT / "build" / "audio_probe.exe"
            if not audio_probe.is_file():
                raise RuntimeError("Ring 3 audio render probe is missing from the system volume")
            shutil.copyfile(audio_probe, system32_dir / audio_probe.name)
        if audio_capture_test:
            audio_probe = ROOT / "build" / "audio_capture_probe.exe"
            if not audio_probe.is_file():
                raise RuntimeError("Ring 3 audio capture probe is missing from the system volume")
            shutil.copyfile(audio_probe, system32_dir / audio_probe.name)
        if doom_test:
            doom_image = ROOT / "build" / "doom.exe"
            if not doom_image.is_file():
                raise RuntimeError("DOOM image was not built for the system volume")
            shutil.copyfile(doom_image, system32_dir / "doom.exe")
            # CMD starts in C:\MicaNT. Stage the same PE there for the QMP
            # launch so the test does not depend on guest keyboard casing.
            shutil.copyfile(doom_image, esp / "MicaNT" / "doom.exe")
            (system32_dir / "doom.log").write_bytes(b"doom startup\r\n")
            wad_source = ROOT / "System" / "Games" / "Freedoom"
            wad_target = esp / "MicaNT" / "System" / "Games" / "Freedoom"
            for name in ("freedoom1.wad", "COPYING.txt", "ATTRIBUTION.md"):
                source = wad_source / name
                if not source.is_file():
                    raise RuntimeError(f"Freedoom package is incomplete: {source}")
            shutil.copytree(wad_source, wad_target)
        for source in sorted((ROOT / "build" / "system32").glob("*.dll")):
            shutil.copyfile(source, system32_dir / source.name)
        explorer_image = ROOT / "build" / "explorer.exe"
        if not explorer_image.is_file():
            raise RuntimeError("Explorer image was not built for the QEMU system volume")
        shutil.copyfile(explorer_image, system32_dir / explorer_image.name)
        mac_shell_image = ROOT / "build" / "mac_shell.exe"
        if not mac_shell_image.is_file():
            raise RuntimeError("MacShell widget owner was not built for the QEMU system volume")
        shutil.copyfile(mac_shell_image, system32_dir / mac_shell_image.name)
        procdump_image = ROOT / "build" / "procdump.exe"
        if not procdump_image.is_file():
            raise RuntimeError("Process memory diagnostic tool was not built for the system volume")
        shutil.copyfile(procdump_image, system32_dir / procdump_image.name)
        proctop_image = ROOT / "build" / "proctop.exe"
        if not proctop_image.is_file():
            raise RuntimeError("Process memory top utility was not built for the system volume")
        shutil.copyfile(proctop_image, system32_dir / proctop_image.name)
        lucecrash_image = ROOT / "build" / "lucecrash.exe"
        if not lucecrash_image.is_file():
            raise RuntimeError("Kernel crash recovery service was not built for the system volume")
        shutil.copyfile(lucecrash_image, system32_dir / lucecrash_image.name)
        if theme_test:
            theme_test_image = ROOT / "build" / "theme_test.exe"
            if not theme_test_image.is_file():
                raise RuntimeError("Dedicated theme test image was not built for the system volume")
            shutil.copyfile(theme_test_image, system32_dir / theme_test_image.name)
        if taskmgr_selftest:
            taskmgr_image = ROOT / "build" / "taskmgr.exe"
            if not taskmgr_image.is_file():
                raise RuntimeError("Task Manager image was not built for the QEMU system volume")
            shutil.copyfile(taskmgr_image, system32_dir / taskmgr_image.name)
            taskmgr_probe_image = ROOT / "build" / "taskmgr_probe.exe"
            if not taskmgr_probe_image.is_file():
                raise RuntimeError("Task Manager action probe was not built for the QEMU system volume")
            shutil.copyfile(taskmgr_probe_image, system32_dir / taskmgr_probe_image.name)
        if cmd_test:
            cmd_image = ROOT / "build" / "cmd.exe"
            if not cmd_image.is_file():
                raise RuntimeError("CMD image was not built for the QEMU system volume")
            shutil.copyfile(cmd_image, system32_dir / cmd_image.name)
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
        if cmd_test and not image_exe.is_file():
            raise RuntimeError("CMD process-creation fixture fileimage.exe is missing")
        if image_exe.is_file():
            shutil.copyfile(image_exe, system32_dir / "fileimage.exe")
            if cmd_test:
                shutil.copyfile(image_exe, diagnostics_dir / "fileimage.exe")
        usb_proof = system32_dir / "micant-usb-proof.txt"
        usb_proof.write_bytes(b"MicaNT USB mass-storage FAT32 post-EBS proof v1\n")
        tls_peer = M10TlsPeer(esp).start() if network_test else None
        if doom_test:
            # Retain the complete UEFI source volume with Doom evidence so a
            # second real boot can run the guest crash recovery service on
            # the same persistent AHCI image.
            shutil.copytree(esp, diagnostics_dir / "esp")
        fat_volume = temp / "ahci-fat32-volume.img"
        ahci_image = temp / "ahci-gpt-fat32.img"
        create_fat32_image(esp, fat_volume)
        create_gpt_disk_image(fat_volume, ahci_image, reserve_crash_dump=True)
        ehci_usb_image = temp / "ehci-usb-fat32.img"
        if usb_ehci_test:
            # Use an independent removable FAT volume containing only the
            # proof file. Duplicating the AHCI system tree would make early
            # boot system-root discovery see two valid MicaNT installations.
            usb_source = temp / "ehci-usb-source"
            usb_proof_file = usb_source / "MicaNT" / "System32" / "micant-usb-proof.txt"
            usb_proof_file.parent.mkdir(parents=True)
            usb_proof_file.write_bytes(b"MicaNT USB mass-storage FAT32 post-EBS proof v1\n")
            create_fat32_image(usb_source, ehci_usb_image)
        log_directory = diagnostics_dir if diagnostics_dir is not None else temp
        serial_path = log_directory / "serial.log"
        qemu_output_path = log_directory / "qemu.log"
        network_capture_path = log_directory / "network.pcap"

        graphics_device = ("virtio-vga-gl" if use_virgl else "virtio-vga") + \
            ",id=micant_display"
        if resolution:
            graphics_device += f",xres={resolution[0]},yres={resolution[1]}"
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
            "-device", graphics_device,
            "-drive",
            f"if=pflash,format=raw,unit=0,readonly=on,file={ovmf_code.resolve()}",
            "-drive", f"file=fat:ro:{esp},format=raw,if=ide,index=0",
            "-drive", f"file={ahci_image},format=raw,if=none,id=ahci_disk,snapshot=off",
            "-device", "ide-hd,drive=ahci_disk,bus=ide.1",
            "-display", qemu_display,
            "-serial", f"file:{serial_path}",
            "-monitor", "none",
            "-qmp", f"unix:{temp / 'qmp.sock'},server=on,wait=off",
            "-snapshot",
            "-no-reboot",
            "-no-shutdown",
        ]
        audio_capture_path = diagnostics_dir / "hda-render.wav" if audio_render_test else None
        if audio_controller_test or audio_render_test or audio_capture_test:
            if audio_render_test:
                audio_device = (
                    f"driver=wav,id=micant_audio,path={audio_capture_path},"
                    f"out.frequency={HDA_RENDER_RATE},out.channels={HDA_RENDER_CHANNELS},out.format=s16"
                )
            else:
                audio_device = "driver=none,id=micant_audio"
            codec_model = "hda-duplex" if audio_capture_test else "hda-output"
            command.extend([
                "-audiodev", audio_device,
                "-device", "ich9-intel-hda,id=micant_hda",
                "-device", f"{codec_model},bus=micant_hda.0,audiodev=micant_audio",
            ])
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
        elif usb_ehci_test:
            # Reproduce the dual-EHCI layout of the target MCP79 platform:
            # the first controller has no attached device and the USB disk is
            # on the second. The guest must reject the empty controller, claim
            # the controller with a connected root port, then mount/read FAT32.
            command.extend([
                "-drive", f"file={ehci_usb_image},format=raw,if=none,id=usb_disk,readonly=on",
                "-device", "usb-ehci,id=ehci_empty",
                "-device", "usb-ehci,id=ehci_storage",
                "-device", "usb-storage,id=usb_storage,drive=usb_disk,bus=ehci_storage.0",
                "-trace", f"enable=usb_ehci_*,file={log_directory / 'ehci.trace'}",
            ])
        if crash_dump_selftest:
            run_crash_dump_two_boots(
                command, ahci_image, temp, diagnostics_dir, timeout, stable_seconds,
            )
            return
        started = time.monotonic()
        complete_since = None
        surface_verified = False
        theme_test_verified = False
        process_memory_verified = False
        cmd_test_verified = False
        usb_input_sent = False
        usb_ehci_verified = False
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
                    if usb_ehci_test:
                        if "ehci.sys: no usable PCI EHCI controller" in serial_log:
                            raise RuntimeError("EHCI driver could not claim either QEMU EHCI controller")
                        if "usbstor.sys: known FAT32 file read did not match" in serial_log:
                            raise RuntimeError("EHCI mass storage mounted, but the FAT32 proof file read failed")
                        usb_ehci_verified = (
                            "ehci.sys: no connected device on this controller; trying next EHCI function" in serial_log and
                            "ehci.sys: PCI EHCI controller claimed and registered with canonical USB bus" in serial_log and
                            "usbstor.sys: BOT/SCSI device mounted through canonical FAT block path" in serial_log and
                            "usbstor.sys: verified known file bytes through USB block device and FAT32" in serial_log
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
                    if process_memory_selftest and "[MicaNT ProcessMemoryTest] FAILED" in serial_log:
                        raise RuntimeError(
                            "procdump process-memory integration self-test failed:\n"
                            f"{serial_log[-8000:]}"
                        )
                    if (process_memory_selftest and
                            "[MicaNT ProcessMemoryTest] report, page read, dump CRC and release verified" in serial_log):
                        process_memory_verified = True
                    if (not surface_verified and
                            "[MicaNT Test] CPL3 surface presentation completed" in serial_log):
                        verify_cpl3_surface(
                            temp / "qmp.sock", temp, diagnostics_dir, resolution,
                        )
                        surface_verified = True
                        print("CPL3 surface reached VirtIO scanout before desktop redraw.", flush=True)
                    progress = serial_progress(serial_log, expected)
                    if progress == len(expected):
                        if cmd_test and "[MicaNT CMD] Ring 3 terminal process launched" not in serial_log:
                            complete_since = None
                            time.sleep(0.1)
                            continue
                        if theme_test and (
                                "[MicaNT ThemeTest] Production control window started" not in serial_log or
                                "[MicaNT ThemeTest] Dynamic window table: 24 windows created, resized, and destroyed" not in serial_log):
                            complete_since = None
                            time.sleep(0.1)
                            continue
                        if process_memory_selftest and not process_memory_verified:
                            complete_since = None
                            time.sleep(0.1)
                            continue
                        if audio_render_test and (
                                serial_log.count("[MicaNT Audio] PCM stream completion verified") < 2 or
                                "[MicaNT Audio] Injected render failure released all DMA resources" not in serial_log or
                                "[MicaNT Audio] Three-stream converted mix submitted to HDA" not in serial_log):
                            complete_since = None
                            time.sleep(0.1)
                            continue
                        if audio_capture_test and (
                                serial_log.count("[MicaNT Audio] Capture MSI route enabled") < 1 or
                                "[MicaNT Audio] Injected capture setup failure released all DMA resources" not in serial_log or
                                "[MicaNT Audio] Ring 3 observed the injected capture setup failure" not in serial_log or
                                serial_log.count("[MicaNT Audio] Ring 3 ADC DMA packets and timestamps received; input gain control=") < 1):
                            if "[MicaNT Audio] Ring 3 ADC capture test failed NTSTATUS=" in serial_log:
                                raise RuntimeError(
                                    "Ring 3 HDA ADC DMA capture test failed:\n" + serial_log[-8000:]
                                )
                            complete_since = None
                            time.sleep(0.1)
                            continue
                        if (audio_controller_test or audio_render_test) and (
                                "[MicaNT HDA] PCI 04/03 controller claimed, BAR0 mapped, reset complete"
                                not in serial_log):
                            raise RuntimeError(
                                "QEMU HDA controller did not complete canonical PCI claim, BAR mapping, and reset:\n"
                                f"{serial_log[-8000:]}"
                            )
                        if (audio_controller_test or audio_render_test) and (
                                "[MicaNT HDA] Injected post-reset failure rolled back BAR mapping, Bus Master, and PCI claim"
                                not in serial_log):
                            raise RuntimeError("QEMU HDA rollback fault injection was not observed")
                        if (audio_controller_test or audio_render_test) and (
                                "[MicaNT HDA] CORB/RIRB codec topology verified" not in serial_log):
                            raise RuntimeError(
                                "QEMU HDA did not complete bounded CORB/RIRB codec discovery:\n"
                                f"{serial_log[-8000:]}"
                            )
                        if theme_test and not theme_test_verified:
                            verify_theme_test(
                                temp / "qmp.sock", temp, serial_path, diagnostics_dir,
                                resolution,
                            )
                            theme_test_verified = True
                        if cmd_test and not cmd_test_verified:
                            verify_cmd_terminal(
                                temp / "qmp.sock", temp, diagnostics_dir, doom_test,
                            )
                            cmd_test_verified = True
                        time_evidence = timer_selftest or runtime_selftest or (
                            "[MicaNT Hal] CPL3 UTC SystemTime advanced and matched KUSER" in serial_log or
                            "[MicaNT Hal] CPL3 UTC SystemTime correctly unavailable for local firmware time" in serial_log
                        )
                        if not time_evidence:
                            raise RuntimeError("CPL3 time probe reported neither UTC success nor explicit unavailable status")
                        if taskmgr_selftest:
                            theme_evidence = (
                                "Task Manager: system theme loaded." in serial_log or
                                "Task Manager: system theme unavailable; fallback active." in serial_log
                            )
                            if not theme_evidence:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: native controls self-test failed." in serial_log:
                                raise RuntimeError("Task Manager tab, search or selection self-test failed")
                            if "Task Manager: query failure self-test failed." in serial_log:
                                raise RuntimeError("Task Manager malformed-buffer queries were not rejected")
                            if "Task Manager: short and invalid query buffers rejected." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
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
                            if "Task Manager: private commit pages validated." not in serial_log:
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
                            if "Task Manager: native window paint presented." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: native window resize delivered." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                            if "Task Manager: native window close processed." not in serial_log:
                                complete_since = None
                                time.sleep(0.1)
                                continue
                        if complete_since is None:
                            complete_since = time.monotonic()
                        elif time.monotonic() - complete_since >= stable_seconds:
                            if usb_ehci_test and not usb_ehci_verified:
                                complete_since = None
                                time.sleep(0.1)
                                continue
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
                            if audio_render_test:
                                if process.poll() is None:
                                    stop_qemu_cleanly(temp / "qmp.sock", process)
                                frame_count, peak = validate_hda_render_wav(audio_capture_path)
                                print(
                                    "QEMU HDA PCM capture passed: "
                                    f"{frame_count} signed-16 stereo frames at 48 kHz, peak={peak}; "
                                    f"evidence={audio_capture_path}",
                                    flush=True,
                                )
                            result = (
                                "DOOM E1M1 gameplay QMP self-test" if doom_test else
                                "CMD graphical terminal QMP self-test" if cmd_test else
                                "Dedicated theme test app" if theme_test else
                                "Task Manager process-snapshot self-test" if taskmgr_selftest else
                                "timer self-test" if timer_selftest else
                                "C++ runtime self-test" if runtime_selftest else
                                "modular boot"
                            )
                            if cmd_test:
                                diagnostics_serial = diagnostics_dir / "serial.log"
                                diagnostics_qemu = diagnostics_dir / "qemu.log"
                                if serial_path.resolve() != diagnostics_serial.resolve():
                                    shutil.copyfile(serial_path, diagnostics_serial)
                                if qemu_output_path.resolve() != diagnostics_qemu.resolve():
                                    shutil.copyfile(qemu_output_path, diagnostics_qemu)
                            print(f"QEMU {result} passed; serial remained stable for "
                                  f"{stable_seconds}s.", flush=True)
                            if usb_ehci_test:
                                print("Dual-EHCI selection, USB mass-storage mount, and FAT32 file read passed.", flush=True)
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
                if process_memory_selftest and ahci_image.is_file():
                    persistent_disk = ROOT / "artifacts" / "process-memory" / "process-memory-dump-disk.img"
                    persistent_disk.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ahci_image, persistent_disk)
                    if diagnostics_dir is not None:
                        shutil.copyfile(ahci_image,
                            diagnostics_dir / "process-memory-dump-disk.img")
                if doom_test and diagnostics_dir is not None and ahci_image.is_file():
                    shutil.copyfile(ahci_image, diagnostics_dir / "doom-qemu-disk.img")
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
    parser.add_argument("--memory-mib", type=int,
                        choices=(256, 500, 512, 1024, 2048, 4096), default=1024)
    parser.add_argument("--diagnostics-dir", type=Path)
    parser.add_argument("--stable-seconds", type=int, default=5)
    parser.add_argument("--display", default="none")
    parser.add_argument(
        "--graphics-backend", choices=("auto", "virtio2d", "virgl"), default="auto"
    )
    parser.add_argument(
        "--resolution", choices=("800x600", "1024x768", "1280x1024", "1920x1080")
    )
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--verify-input", action="store_true")
    parser.add_argument("--usb-test", action="store_true")
    parser.add_argument("--usb-ehci-test", action="store_true",
                        help="test dual-EHCI selection, USB mass storage, and a FAT32 file read")
    parser.add_argument("--verify-usb-input", action="store_true")
    parser.add_argument("--network-test", action="store_true")
    parser.add_argument("--runtime-selftest", action="store_true")
    parser.add_argument("--timer-selftest", action="store_true")
    parser.add_argument("--taskmgr-selftest", action="store_true")
    parser.add_argument("--theme-test", action="store_true")
    parser.add_argument("--cmd-test", action="store_true")
    parser.add_argument("--doom-test", action="store_true",
                        help="launch doom.exe, play E1M1 with QMP, and retain scanout screenshots")
    parser.add_argument("--process-memory-selftest", action="store_true")
    parser.add_argument("--crash-dump-selftest", action="store_true")
    parser.add_argument("--audio-controller-test", action="store_true")
    parser.add_argument("--audio-render-test", action="store_true",
                        help="capture and validate real Ring 3 HDA PCM output as WAV")
    parser.add_argument("--audio-capture-test", action="store_true",
                        help="exercise Ring 3 HDA ADC DMA packets and event timestamps")
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
            args.graphics_backend,
            args.theme_test,
            args.process_memory_selftest,
            args.cmd_test,
            args.doom_test,
            args.crash_dump_selftest,
            tuple(int(part) for part in args.resolution.split("x"))
                if args.resolution else None,
            args.usb_ehci_test,
            args.audio_controller_test,
            args.audio_render_test,
            args.audio_capture_test,
        )
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"QEMU boot test failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
