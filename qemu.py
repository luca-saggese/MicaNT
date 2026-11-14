#!/usr/bin/env python3
"""Build the AMD64 modular boot artifacts and launch them in QEMU/OVMF."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build" / "qemu-cmake-mingw"
KERNEL = ROOT / "build" / "baremetal" / "ntoskrnl.exe"
BOOTLOADER = ROOT / "build" / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"
DISPLAY_DRIVER = ROOT / "build" / "virtio_gpu.sys"
MANIFEST = ROOT / "build" / "baremetal" / "kernel-build-manifest.json"
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

from verify_kernel_architecture import (
    inspect_pe,
    sha256,
    validate_build_manifest,
)


def find_compiler(requested: str | None) -> Path:
    candidates = []
    if requested:
        resolved = shutil.which(requested)
        if resolved:
            return Path(os.path.abspath(resolved))
        path = Path(requested).expanduser()
        if path.is_file():
            return path.absolute()
        raise RuntimeError(f"requested cross-compiler was not found: {requested}")

    environment_compiler = os.environ.get("MICANT_CXX")
    if environment_compiler:
        return find_compiler(environment_compiler)

    path_compiler = shutil.which("x86_64-w64-mingw32-clang++")
    if path_compiler:
        return Path(os.path.abspath(path_compiler))

    bundled = sorted(
        (ROOT / "build").glob(
            "llvm-mingw-*/bin/x86_64-w64-mingw32-clang++"
        )
    )
    candidates.extend(path for path in bundled if path.is_file())
    if candidates:
        return candidates[-1].absolute()

    raise RuntimeError(
        "AMD64 PE cross-compiler not found; install LLVM-MinGW or set MICANT_CXX"
    )


def find_c_compiler(cxx: Path) -> Path:
    sibling = cxx.with_name(cxx.name.replace("clang++", "clang"))
    if sibling.is_file():
        return sibling
    c_compiler = shutil.which("x86_64-w64-mingw32-clang")
    if c_compiler:
        return Path(os.path.abspath(c_compiler))
    raise RuntimeError(f"matching C compiler was not found next to {cxx}")


def find_ovmf(requested: Path | None) -> Path:
    if requested:
        firmware = requested.expanduser().resolve()
        if firmware.is_file():
            return firmware
        raise RuntimeError(f"OVMF code image does not exist: {firmware}")

    environment_firmware = os.environ.get("MICANT_OVMF_CODE")
    if environment_firmware:
        return find_ovmf(Path(environment_firmware))

    candidates = [
        Path("/opt/homebrew/share/qemu/edk2-x86_64-code.fd"),
        Path("/usr/share/qemu/edk2-x86_64-code.fd"),
        Path("/usr/share/OVMF/OVMF_CODE.fd"),
        Path("/usr/share/edk2/x64/OVMF_CODE.fd"),
    ]
    candidates.extend(
        sorted(
            Path("/opt/homebrew/Cellar/qemu").glob(
                "*/share/qemu/edk2-x86_64-code.fd"
            ),
            reverse=True,
        )
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(
        "OVMF code image not found; provide --ovmf-code or MICANT_OVMF_CODE"
    )


def find_qemu() -> str:
    qemu = shutil.which(os.environ.get("MICANT_QEMU", "qemu-system-x86_64"))
    if not qemu:
        raise RuntimeError("qemu-system-x86_64 was not found on PATH")
    return qemu


def choose_display(qemu: str, requested: str | None) -> str:
    if requested:
        return requested
    result = subprocess.run(
        [qemu, "-display", "help"],
        check=True,
        capture_output=True,
        text=True,
    )
    listing = result.stdout + result.stderr
    available = set()
    collecting = False
    for line in listing.splitlines():
        if "Available display backend types:" in line:
            collecting = True
            continue
        if collecting:
            available.update(line.split())

    preferred = {
        "darwin": ("cocoa", "sdl"),
        "linux": ("gtk", "sdl"),
        "win32": ("win32", "sdl"),
    }.get(sys.platform, ("sdl", "gtk", "cocoa"))
    for backend in preferred:
        if backend in available:
            return backend
    raise RuntimeError(
        "QEMU has no supported graphical display backend; use --headless or --display"
    )


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def write_manifest(
    cxx: Path, *, runtime_selftest: bool = False, timer_selftest: bool = False
) -> None:
    if not all(path.is_file() for path in (KERNEL, BOOTLOADER, DISPLAY_DRIVER)):
        raise RuntimeError("CMake completed without producing all boot PE artifacts")

    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    working_tree_dirty = bool(
        subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    )
    toolchain = subprocess.run(
        [str(cxx), "--version"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()[0]

    kernel_pe = inspect_pe(KERNEL, "NATIVE")
    bootloader_pe = inspect_pe(BOOTLOADER, "EFI_APPLICATION")
    display_driver_pe = inspect_pe(DISPLAY_DRIVER, "NATIVE")
    manifest = {
        "schema_version": 2,
        "build_id": f"{commit[:12]}-{time.time_ns()}",
        "git_commit": commit,
        "working_tree_dirty": working_tree_dirty,
        "toolchain": toolchain,
        "configuration": "Release",
        "runtime_selftest": runtime_selftest,
        "timer_selftest": timer_selftest,
        "target": "ntoskrnl_core_baremetal",
        "architecture": "AMD64",
        "artifacts": {
            "kernel": {
                "path": "build/baremetal/ntoskrnl.exe",
                "sha256": sha256(KERNEL),
                "machine": "AMD64",
                "subsystem": "NATIVE",
                "entry_point_rva": kernel_pe["entry_point_rva"],
            },
            "bootloader": {
                "path": "build/esp/EFI/BOOT/BOOTX64.EFI",
                "sha256": sha256(BOOTLOADER),
                "machine": "AMD64",
                "subsystem": "EFI_APPLICATION",
                "entry_point_rva": bootloader_pe["entry_point_rva"],
            },
            "display_driver": {
                "path": "build/virtio_gpu.sys",
                "sha256": sha256(DISPLAY_DRIVER),
                "machine": "AMD64",
                "subsystem": "NATIVE",
                "entry_point_rva": display_driver_pe["entry_point_rva"],
            },
        },
    }
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Build manifest written: {MANIFEST.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", help="LLVM-MinGW x86_64-w64-mingw32-clang++ path")
    parser.add_argument(
        "--cmake-build-dir", type=Path,
        help="use a separate CMake build directory while sharing boot artifact output paths",
    )
    parser.add_argument("--ovmf-code", type=Path, help="OVMF x86_64 code firmware image")
    parser.add_argument("--display", help="QEMU display backend (auto-detected by default)")
    parser.add_argument("--timeout", type=int, default=90, help="boot milestone timeout")
    parser.add_argument("--diagnostics-dir", type=Path,
                        help="new directory retaining serial/QEMU logs and the exact kernel/manifest")
    parser.add_argument(
        "--stable-seconds", type=int, default=5, help="required shell idle duration"
    )
    parser.add_argument(
        "--headless", action="store_true", help="run QEMU without opening a display"
    )
    parser.add_argument(
        "--build-only", action="store_true", help="build and validate artifacts without launching QEMU"
    )
    parser.add_argument(
        "--check-explorer", action="store_true",
        help="build the real Explorer/Win32 DLLs and check their native import closure; do not boot",
    )
    parser.add_argument(
        "--runtime-selftest", action="store_true",
        help="verify real kernel C++ construction/teardown, then halt instead of booting the desktop",
    )
    parser.add_argument(
        "--timer-selftest", action="store_true",
        help="verify scheduler-clock tick delivery, then halt before process tests and desktop rendering",
    )
    parser.add_argument(
        "--usb-test", action="store_true",
        help="attach QEMU xHCI, USB HID and read-only USB mass storage devices",
    )
    parser.add_argument(
        "--network-test", action="store_true",
        help="verify Ring 3 DHCP, DNS, TCP and certificate-validated HTTPS over QEMU virtio-net",
    )
    args = parser.parse_args()
    global BUILD_DIR
    if args.cmake_build_dir:
        BUILD_DIR = args.cmake_build_dir.expanduser()
        if not BUILD_DIR.is_absolute():
            BUILD_DIR = ROOT / BUILD_DIR

    if args.timeout <= 0 or args.stable_seconds <= 0:
        parser.error("--timeout and --stable-seconds must be positive")
    if args.build_only and args.check_explorer:
        parser.error("--build-only and --check-explorer are mutually exclusive")
    if args.runtime_selftest and args.check_explorer:
        parser.error("--runtime-selftest and --check-explorer are mutually exclusive")
    if args.timer_selftest and (args.runtime_selftest or args.check_explorer):
        parser.error("--timer-selftest is mutually exclusive with runtime self-test and Explorer checks")
    if args.network_test and (args.runtime_selftest or args.timer_selftest or args.check_explorer):
        parser.error("--network-test requires a normal kernel boot")

    try:
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        if not cmake:
            raise RuntimeError("cmake was not found on PATH")
        if not ninja:
            raise RuntimeError("ninja was not found on PATH")
        cxx = find_compiler(args.cxx)
        cc = find_c_compiler(cxx)
        qemu = find_qemu() if not (args.build_only or args.check_explorer) else None
        display = (
            choose_display(qemu, args.display)
            if qemu and not args.headless
            else None
        )

        run(
            [
                cmake,
                "-S",
                str(ROOT),
                "-B",
                str(BUILD_DIR),
                "-G",
                "Ninja",
                f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake' / 'toolchains' / 'llvm-mingw.cmake'}",
                f"-DCMAKE_C_COMPILER={cc}",
                f"-DCMAKE_CXX_COMPILER={cxx}",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DMICANT_ENABLE_BAREMETAL:BOOL=ON",
                f"-DMICANT_BOOTSTRAP_RUNTIME_SELFTEST:BOOL={'ON' if args.runtime_selftest else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_TIMER_SELFTEST:BOOL={'ON' if args.timer_selftest else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_NETWORK_SELFTEST:BOOL={'ON' if args.network_test else 'OFF'}",
            ]
        )
        if args.check_explorer:
            run([
                cmake, "--build", str(BUILD_DIR), "--config", "Release",
                "--target", "explorer_runtime_boot",
            ])
            run([sys.executable, str(SCRIPTS / "verify_explorer_runtime.py")])
            return 0
        run(
            [
                cmake,
                "--build",
                str(BUILD_DIR),
                "--config",
                "Release",
                "--target",
                "ntoskrnl_core_baremetal",
                "bootx64_efi",
                "virtio_gpu_boot",
                "ahci_oem_boot",
                "hidusb_oem_boot",
                "usbstor_oem_boot",
                "xhci_oem_boot",
                "i210_oem_boot",
                "virtio_net_oem_boot",
                "ws2_32_service_boot",
                "dnsapi_service_boot",
                "crypt32_service_boot",
                "winhttp_service_boot",
                "svchost_boot",
                "network_probe_boot",
                "https_probe_boot",
                "file_image_boot",
            ]
        )
        write_manifest(
            cxx,
            runtime_selftest=args.runtime_selftest,
            timer_selftest=args.timer_selftest,
        )
        validate_build_manifest(
            ROOT,
            allow_dirty=True,
            runtime_selftest=args.runtime_selftest,
            timer_selftest=args.timer_selftest,
        )

        if args.build_only:
            print("Modular kernel, UEFI bootloader and display driver build passed.")
            return 0

        firmware = find_ovmf(args.ovmf_code)
        boot_command = [
            sys.executable,
            str(SCRIPTS / "boot_micant.py"),
            "--ovmf-code",
            str(firmware),
            "--timeout",
            str(args.timeout),
            "--stable-seconds",
            str(args.stable_seconds),
            "--display",
            "none" if args.headless else display,
        ]
        if args.diagnostics_dir:
            boot_command.extend(["--diagnostics-dir", str(args.diagnostics_dir.resolve())])
        if args.runtime_selftest:
            boot_command.append("--runtime-selftest")
        elif args.timer_selftest:
            boot_command.append("--timer-selftest")
        elif not args.headless:
            boot_command.append("--interactive")
        elif args.usb_test:
            boot_command.append("--verify-usb-input")
        else:
            boot_command.append("--verify-input")
        if args.usb_test:
            boot_command.append("--usb-test")
        if args.network_test:
            boot_command.append("--network-test")
        run(boot_command)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        operation = "Explorer runtime check" if args.check_explorer else "QEMU launch"
        print(f"MicaNT {operation} failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
