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
    cxx: Path, *, runtime_selftest: bool = False, timer_selftest: bool = False,
    taskmgr_selftest: bool = False, theme_test: bool = False,
    textbox_test: bool = False,
    process_memory_selftest: bool = False,
    cmd_test: bool = False,
    reactos_app: str | None = None,
    doom_test: bool = False,
    audio_controller_test: bool = False,
    audio_render_test: bool = False,
    audio_capture_test: bool = False,
    crash_dump_selftest: bool = False,
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
        "taskmgr_selftest": taskmgr_selftest,
        "theme_test": theme_test,
        "textbox_test": textbox_test,
        "process_memory_selftest": process_memory_selftest,
        "cmd_test": cmd_test,
        "reactos_app": reactos_app,
        "doom_test": doom_test,
        "audio_controller_test": audio_controller_test,
        "audio_render_test": audio_render_test,
        "audio_capture_test": audio_capture_test,
        "crash_dump_selftest": crash_dump_selftest,
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
    parser.add_argument(
        "--resize", action="store_true",
        help="enable QEMU display zoom-to-fit so the guest display follows window resizing",
    )
    parser.add_argument(
        "--graphics-backend", choices=("auto", "virtio2d", "virgl"), default="auto",
        help="select VirtIO-GPU 2D, or attempt VirGL only when QEMU and the display support it",
    )
    parser.add_argument(
        "--resolution", choices=("800x600", "1024x768", "1280x1024", "1920x1080"),
        help="request a guest scanout mode through VirtIO-VGA EDID",
    )
    parser.add_argument("--timeout", type=int, default=90, help="boot milestone timeout")
    parser.add_argument("--memory-mib", type=int, default=1024,
                        choices=(256, 500, 512, 1024, 2048, 4096),
                        help="QEMU RAM profile used by the bare-metal boot")
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
        "--taskmgr-selftest", action="store_true",
        help="launch taskmgr.exe in QEMU and verify its live process snapshot syscall",
    )
    parser.add_argument(
        "--theme-test", action="store_true",
        help="launch theme_test.exe in QEMU for dedicated Win32 theme verification",
    )
    parser.add_argument(
        "--textbox-test", action="store_true",
        help="launch a small Ring 3 STATIC textbox paint probe in QEMU",
    )
    parser.add_argument(
        "--compositor-local-stats", action="store_true",
        help="enable aggregated compositor timing, repaint and present counters",
    )
    parser.add_argument("--kernel-perf-diagnostics", action="store_true",
                        help="log aggregated 5-second timer, thread-state and context-switch counters")
    parser.add_argument("--compositor-debug-hud", action="store_true",
                        help="build the compositor with its diagnostic HUD enabled")
    parser.add_argument(
        "--process-memory-selftest", action="store_true",
        help="exercise process memory queries, bounded dump streaming and cleanup in QEMU",
    )
    parser.add_argument(
        "--cmd-test", action="store_true",
        help="launch the Ring 3 CMD terminal and exercise it through QMP",
    )
    parser.add_argument(
        "--reactos-apps-dir", type=Path,
        help="stage a verified original AMD64 app bundle at C:\\ReactOSApps",
    )
    parser.add_argument(
        "--reactos-app",
        choices=("winver", "notepad", "calc", "winmine", "sol",
                 "mspaint", "regedit", "taskmgr", "wordpad", "cmd"),
        help="launch one original ReactOS PE after Explorer and interact using QMP",
    )
    parser.add_argument(
        "--reactos-debug-image", type=Path,
        help="diagnostic-only replacement image for ReactOS Calc; never used as compatibility evidence",
    )
    parser.add_argument(
        "--doom-test", action="store_true",
        help="run real doom.exe in the guest, play E1M1 with QMP, and retain screenshots",
    )
    parser.add_argument(
        "--audio-controller-test", action="store_true",
        help="attach QEMU Intel HDA and verify canonical PCI BAR mapping and controller reset",
    )
    parser.add_argument(
        "--audio-render-test", action="store_true",
        help="verify Ring 3 PCM submission, HDA DMA completion, and captured QEMU WAV samples",
    )
    parser.add_argument(
        "--audio-capture-test", action="store_true",
        help="exercise Ring 3 HDA ADC DMA packets and event timestamps",
    )
    parser.add_argument(
        "--interactive-after-cmd-test", action="store_true",
        help="keep the visible CMD-test QEMU session open for manual use after QMP verification",
    )
    parser.add_argument(
        "--interactive-after-doom-test", action="store_true",
        help="leave the verified Doom E1M1 session running in the visible QEMU window",
    )
    parser.add_argument(
        "--crash-dump-selftest", action="store_true",
        help="fault the kernel, persist a crash dump, reboot, recover it to C:\\Windows\\Minidump and decode it",
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
    if args.doom_test:
        args.cmd_test = True
        args.memory_mib = 512
        if args.timeout == 90:
            args.timeout = 240
    if args.reactos_app:
        args.memory_mib = 512
        if args.reactos_apps_dir is None:
            parser.error("--reactos-app requires --reactos-apps-dir")
        if args.timeout == 90:
            args.timeout = 180
    elif args.reactos_apps_dir is not None:
        parser.error("--reactos-apps-dir requires --reactos-app")
    if args.reactos_debug_image is not None and args.reactos_app != "calc":
        parser.error("--reactos-debug-image is only valid with --reactos-app calc")
    if args.reactos_app and (args.cmd_test or args.doom_test or args.taskmgr_selftest or
                             args.theme_test or args.textbox_test or args.process_memory_selftest or
                             args.network_test or args.usb_test or
                             args.audio_controller_test or args.audio_render_test or args.audio_capture_test or
                             args.crash_dump_selftest or args.runtime_selftest or
                             args.timer_selftest):
        parser.error("--reactos-app has an isolated direct-launch QEMU profile")
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
    if args.taskmgr_selftest and (args.runtime_selftest or args.timer_selftest or args.usb_test or args.network_test or args.check_explorer):
        parser.error("--taskmgr-selftest requires the unattended normal desktop boot path")
    if args.theme_test and (args.runtime_selftest or args.timer_selftest or args.usb_test or args.network_test or args.check_explorer or args.taskmgr_selftest):
        parser.error("--theme-test requires the unattended normal desktop boot path")
    if args.textbox_test and (args.runtime_selftest or args.timer_selftest or args.usb_test or
                              args.network_test or args.check_explorer or args.taskmgr_selftest or
                              args.theme_test or args.cmd_test or args.process_memory_selftest):
        parser.error("--textbox-test requires its isolated desktop boot path")
    if args.process_memory_selftest and (args.runtime_selftest or args.timer_selftest or args.usb_test or args.network_test or args.check_explorer):
        parser.error("--process-memory-selftest requires the normal desktop boot path")
    if args.cmd_test and (args.runtime_selftest or args.timer_selftest or args.usb_test or
                          args.network_test or args.check_explorer or args.taskmgr_selftest or
                          args.theme_test or args.textbox_test or args.process_memory_selftest):
        parser.error("--cmd-test requires its dedicated unattended QMP input profile")
    if args.cmd_test and args.memory_mib != 512:
        parser.error("--cmd-test and --doom-test must run with --memory-mib 512")
    if args.interactive_after_cmd_test and (not args.cmd_test or args.headless):
        parser.error("--interactive-after-cmd-test requires visible --cmd-test mode")
    if args.interactive_after_doom_test and (not args.doom_test or args.headless):
        parser.error("--interactive-after-doom-test requires visible --doom-test mode")
    if args.crash_dump_selftest and (args.runtime_selftest or args.timer_selftest or
            args.usb_test or args.network_test or args.check_explorer or args.taskmgr_selftest or
            args.theme_test or args.textbox_test or args.process_memory_selftest or args.cmd_test):
        parser.error("--crash-dump-selftest has its own two-boot QEMU profile")
    if args.crash_dump_selftest and args.diagnostics_dir is not None and args.diagnostics_dir.exists():
        parser.error("--diagnostics-dir must be a new directory for crash-dump evidence")

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
        if args.resize:
            if args.headless:
                parser.error("--resize requires a graphical display")
            if display is None:
                parser.error("--resize requires a graphical display backend")
            display_options = display.split(",")
            if not any(option.startswith("zoom-to-fit=") for option in display_options[1:]):
                display = f"{display},zoom-to-fit=on"

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
                f"-DMICANT_BOOTSTRAP_TASKMGR_SELFTEST:BOOL={'ON' if args.taskmgr_selftest else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_THEME_TEST:BOOL={'ON' if args.theme_test else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_TEXTBOX_TEST:BOOL={'ON' if args.textbox_test else 'OFF'}",
                f"-DMICANT_COMPOSITOR_LOCAL_STATS:BOOL={'ON' if args.compositor_local_stats else 'OFF'}",
                f"-DMICANT_KERNEL_PERF_DIAGNOSTICS:BOOL={'ON' if args.kernel_perf_diagnostics else 'OFF'}",
                f"-DMICANT_COMPOSITOR_DEBUG_HUD:BOOL={'ON' if args.compositor_debug_hud else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_PROCESS_MEMORY_SELFTEST:BOOL={'ON' if args.process_memory_selftest else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_CMD_TEST:BOOL={'ON' if args.cmd_test else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_REACTOS_APP:STRING={args.reactos_app or ''}",
                f"-DMICANT_BOOTSTRAP_CRASH_DUMP_SELFTEST:BOOL={'ON' if args.crash_dump_selftest else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST:BOOL={'ON' if args.audio_controller_test or args.audio_render_test else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_AUDIO_RENDER_TEST:BOOL={'ON' if args.audio_render_test else 'OFF'}",
                f"-DMICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST:BOOL={'ON' if args.audio_capture_test else 'OFF'}",
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
                "ehci_oem_boot",
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
                "advapi32_boot",
                "msvcrt_boot",
                "shell32_boot",
                "comctl32_boot",
                "explorer_runtime_boot",
                "taskmgr_boot",
                "procdump_boot",
                "proctop_boot",
                "lucecrash_boot",
                "theme_test_boot",
                "textbox_probe_boot",
                "taskmgr_probe_boot",
                "compositor_boot",
            ]
        )
        if args.cmd_test:
            run([cmake, "--build", str(BUILD_DIR), "--config", "Release", "--target", "cmd_boot"])
        if args.doom_test:
            run([cmake, "--build", str(BUILD_DIR), "--config", "Release", "--target", "doom_boot"])
        if args.audio_render_test:
            run([cmake, "--build", str(BUILD_DIR), "--config", "Release", "--target", "audio_probe_boot"])
        if args.audio_capture_test:
            run([cmake, "--build", str(BUILD_DIR), "--config", "Release", "--target", "audio_capture_probe_boot"])
        write_manifest(
            cxx,
            runtime_selftest=args.runtime_selftest,
            timer_selftest=args.timer_selftest,
            taskmgr_selftest=args.taskmgr_selftest,
            theme_test=args.theme_test,
            textbox_test=args.textbox_test,
            process_memory_selftest=args.process_memory_selftest,
            cmd_test=args.cmd_test,
            reactos_app=args.reactos_app,
            doom_test=args.doom_test,
            audio_controller_test=args.audio_controller_test or args.audio_render_test or args.audio_capture_test,
            audio_render_test=args.audio_render_test,
            audio_capture_test=args.audio_capture_test,
            crash_dump_selftest=args.crash_dump_selftest,
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
            "--memory-mib",
            str(args.memory_mib),
            "--display",
            "none" if args.headless else display,
            "--graphics-backend",
            args.graphics_backend,
        ]
        if args.resolution:
            boot_command.extend(["--resolution", args.resolution])
        if args.diagnostics_dir:
            boot_command.extend(["--diagnostics-dir", str(args.diagnostics_dir.resolve())])
        elif args.reactos_app:
            evidence_name = f"reactos-{args.reactos_app}-qemu-evidence-{time.time_ns()}"
            evidence_dir = ROOT / "build" / evidence_name
            boot_command.extend(["--diagnostics-dir", str(evidence_dir)])
        elif args.cmd_test:
            evidence_dir = ROOT / "build" / f"cmd-m0-qemu-evidence-{time.time_ns()}"
            boot_command.extend(["--diagnostics-dir", str(evidence_dir)])
        elif args.textbox_test:
            evidence_dir = ROOT / "build" / f"textbox-probe-qemu-evidence-{time.time_ns()}"
            boot_command.extend(["--diagnostics-dir", str(evidence_dir)])
        elif args.crash_dump_selftest:
            evidence_dir = ROOT / "build" / f"crash-dump-qemu-evidence-{time.time_ns()}"
            boot_command.extend(["--diagnostics-dir", str(evidence_dir)])
        elif args.audio_render_test:
            evidence_dir = ROOT / "build" / f"audio-render-qemu-evidence-{time.time_ns()}"
            boot_command.extend(["--diagnostics-dir", str(evidence_dir)])
        elif args.audio_capture_test:
            evidence_dir = ROOT / "build" / f"audio-capture-qemu-evidence-{time.time_ns()}"
            boot_command.extend(["--diagnostics-dir", str(evidence_dir)])
        if args.runtime_selftest:
            boot_command.append("--runtime-selftest")
        elif args.timer_selftest:
            boot_command.append("--timer-selftest")
        elif args.taskmgr_selftest:
            boot_command.append("--taskmgr-selftest")
        elif args.theme_test:
            boot_command.append("--theme-test")
            if not args.headless:
                boot_command.append("--interactive")
        elif args.textbox_test:
            boot_command.append("--textbox-test")
        elif args.cmd_test:
            boot_command.append("--cmd-test")
            if args.doom_test:
                boot_command.append("--doom-test")
            if args.interactive_after_cmd_test or args.interactive_after_doom_test:
                boot_command.append("--interactive")
        if args.reactos_app:
            boot_command.extend([
                "--reactos-apps-dir", str(args.reactos_apps_dir.expanduser().resolve()),
                "--reactos-app", args.reactos_app,
            ])
            if args.reactos_debug_image is not None:
                boot_command.extend([
                    "--reactos-debug-image",
                    str(args.reactos_debug_image.expanduser().resolve()),
                ])
            if not args.headless:
                boot_command.append("--interactive")
        elif args.crash_dump_selftest:
            boot_command.append("--crash-dump-selftest")
        elif args.audio_controller_test:
            pass  # HDA A0.1 has a serial acceptance check; skip unrelated input automation.
        elif args.audio_render_test:
            pass  # The Ring 3 render client is exercised by the boot profile itself.
        elif args.audio_capture_test:
            pass  # The Ring 3 audio client is exercised by the boot profile itself.
        elif not args.headless:
            boot_command.append("--interactive")
        elif args.usb_test:
            boot_command.append("--verify-usb-input")
        elif (not args.process_memory_selftest and not args.resolution and
              not args.cmd_test and not args.doom_test):
            boot_command.append("--verify-input")
        if args.process_memory_selftest:
            boot_command.append("--process-memory-selftest")
        if args.usb_test:
            boot_command.append("--usb-test")
        if args.network_test:
            boot_command.append("--network-test")
        if args.audio_controller_test:
            boot_command.append("--audio-controller-test")
        if args.audio_render_test:
            boot_command.append("--audio-render-test")
        if args.audio_capture_test:
            boot_command.append("--audio-capture-test")
        run(boot_command)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        operation = "Explorer runtime check" if args.check_explorer else "QEMU launch"
        print(f"MicaNT {operation} failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
