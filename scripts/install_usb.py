#!/usr/bin/env python3
"""Install the current MicaNT AMD64 UEFI build onto a USB drive.

This script erases the selected device. It stages the same basic UEFI and FAT32
layout used by the QEMU boot path, then creates a single GPT/FAT32 partition.
It builds the AMD64 boot artifacts first. Linux and macOS are supported; see
--help for build options and the required explicit confirmation flag.
"""

from __future__ import annotations

import argparse
import os
import plistlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(args: list[str]) -> None:
    subprocess.run(args, check=True)


def require_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise RuntimeError(f"Missing {description}: {path}\nBuild the MicaNT boot artifacts first.")
    return path


def stage(root: Path) -> None:
    boot = require_file(ROOT / "build/esp/EFI/BOOT/BOOTX64.EFI", "UEFI bootloader")
    kernel = require_file(ROOT / "build/baremetal/ntoskrnl.exe", "bare-metal kernel")
    gpu = require_file(ROOT / "build/virtio_gpu.sys", "VirtIO GPU driver")

    boot_dir = root / "EFI/BOOT"
    boot_dir.mkdir(parents=True)
    shutil.copy2(boot, boot_dir / "BOOTX64.EFI")
    kernel_dir = root / "EFI/MicaNT"
    kernel_dir.mkdir(parents=True)
    shutil.copy2(kernel, kernel_dir / "ntoskrnl.exe")
    drivers = kernel_dir / "System32/drivers"
    drivers.mkdir(parents=True)
    shutil.copy2(gpu, drivers / "virtio_gpu.sys")
    for name in ("storahci.sys", "hidusb.sys", "usbstor.sys", "usbxhci.sys",
                 "i210.sys", "virtio_net.sys"):
        candidate = ROOT / "build" / name
        if candidate.is_file():
            shutil.copy2(candidate, drivers / name)

    # Keep the whole built DLL set together, as the QEMU system volume does.
    system32 = root / "MicaNT/System32"
    system32.mkdir(parents=True)
    dll_dir = ROOT / "build/system32"
    dlls = sorted(dll_dir.glob("*.dll")) if dll_dir.is_dir() else []
    if not any(path.name.lower() == "ntdll.dll" for path in dlls):
        raise RuntimeError(f"NTDLL is missing from {dll_dir}; build the native system DLLs first.")
    for source in dlls:
        shutil.copy2(source, system32 / source.name)

    # Include the standard desktop/runtime images when present. The kernel can
    # still boot without optional tools, but the normal desktop needs these.
    for name in ("explorer.exe", "compositor.exe", "procdump.exe", "proctop.exe",
                 "lucecrash.exe", "svchost.exe"):
        source = ROOT / "build" / name
        if source.is_file():
            shutil.copy2(source, system32 / name)

    # The boot-time SEC_IMAGE integration check opens this fixture from the
    # mounted system volume. qemu.py --build-only builds it, but it is not an
    # optional desktop app and must be present on bare-metal installations.
    image_fixture = require_file(ROOT / "build/fileimage.exe",
                                 "file-backed SEC_IMAGE verification image")
    shutil.copy2(image_fixture, system32 / image_fixture.name)
    for relative in ("assets/wallpaper.png",):
        source = ROOT / relative
        if source.is_file():
            shutil.copy2(source, system32 / source.name)

    (root / "Windows/Minidump").mkdir(parents=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", help="whole USB device (Linux: /dev/sdb; macOS: /dev/diskN)")
    parser.add_argument("--yes", action="store_true", help="confirm erasing and repartitioning this device")
    parser.add_argument("--cxx", help="LLVM-MinGW x86_64-w64-mingw32-clang++ path")
    parser.add_argument("--cmake-build-dir", type=Path,
                        help="CMake build directory (default: build/qemu-cmake-mingw)")
    args = parser.parse_args()

    if sys.platform not in ("linux", "darwin"):
        parser.error("USB installation is supported on Linux and macOS")
    if os.geteuid() != 0:
        parser.error("run with sudo so the script can partition and mount the USB device")
    # Keep macOS /dev/diskN spelling: Path.resolve() follows its device-node
    # alias to /dev/rdiskN, which diskutil rejects for partitionDisk.
    device = Path(os.path.abspath(args.device))
    if not device.exists() or not device.is_block_device():
        parser.error(f"not a block device: {device}")
    if sys.platform == "linux":
        if device.name[-1:].isdigit():
            parser.error("pass the whole USB device (such as /dev/sdb), not a partition")
        size = int(subprocess.check_output(
            ["blockdev", "--getsize64", str(device)], text=True).strip())
    else:
        if not device.name.startswith("disk") or not device.name[4:].isdigit():
            parser.error("pass the whole macOS disk (such as /dev/disk2), not a partition or rdisk device")
        disk_info = plistlib.loads(subprocess.check_output(
            ["diskutil", "info", "-plist", str(device)]))
        size = int(disk_info.get("TotalSize", 0))
    if size < 512 * 1024 * 1024:
        parser.error("USB device must be at least 512 MiB")
    print(f"Target: {device} ({size / (1024**3):.1f} GiB). All data on it will be erased.", flush=True)
    if not args.yes:
        parser.error("nothing written; rerun with --yes after checking the target device")

    print("Building the latest MicaNT AMD64 UEFI and desktop artifacts...", flush=True)
    build_command = [sys.executable, str(ROOT / "qemu.py"), "--build-only"]
    if args.cxx:
        build_command.extend(["--cxx", args.cxx])
    if args.cmake_build_dir:
        build_command.extend(["--cmake-build-dir", str(args.cmake_build_dir)])
    run(build_command)

    tools = (("parted", "mkfs.fat", "mount", "umount", "blockdev", "partprobe")
             if sys.platform == "linux" else ("diskutil",))
    missing = [tool for tool in tools if shutil.which(tool) is None]
    if missing:
        raise RuntimeError("Missing required system tools: " + ", ".join(missing))
    with tempfile.TemporaryDirectory(prefix="micant-usb-") as temp:
        mountpoint = Path(temp) / "mount"
        source = Path(temp) / "tree"
        mountpoint.mkdir()
        source.mkdir()
        stage(source)

        if sys.platform == "linux":
            partition_suffix = "p1" if device.name.startswith(("nvme", "mmcblk")) else "1"
            partition = Path(str(device) + partition_suffix)
            run(["parted", "--script", str(device), "mklabel", "gpt",
                 "mkpart", "MICA_SYS", "fat32", "1MiB", "100%",
                 "set", "1", "esp", "on"])
            run(["partprobe", str(device)])
            run(["mkfs.fat", "-F", "32", "-n", "MICANT", str(partition)])
            run(["mount", str(partition), str(mountpoint)])
            mounted_path = mountpoint
        else:
            run(["diskutil", "partitionDisk", str(device), "GPT", "FAT32",
                 "MICANT", "R"])
            listing = plistlib.loads(subprocess.check_output(
                ["diskutil", "list", "-plist", str(device)]))
            partitions = [part for disk in listing.get("AllDisksAndPartitions", [])
                          for part in disk.get("Partitions", [])]
            micant_partition = next(
                (part.get("DeviceIdentifier") for part in partitions
                 if part.get("VolumeName") == "MICANT"), None)
            if not micant_partition:
                raise RuntimeError("diskutil created the disk but did not report the MICANT volume")
            partition = Path("/dev") / micant_partition
            info = subprocess.check_output(["diskutil", "info", "-plist", str(partition)])
            mount_value = plistlib.loads(info).get("MountPoint")
            if not mount_value:
                raise RuntimeError(f"diskutil did not mount the new partition {partition}")
            mounted_path = Path(mount_value)
        try:
            shutil.copytree(source, mounted_path, dirs_exist_ok=True)
            run(["sync"])
        finally:
            if sys.platform == "linux":
                run(["umount", str(mounted_path)])
            else:
                run(["diskutil", "unmount", str(partition)])
    print(f"MicaNT USB installed on {device}. Boot it in UEFI mode (AMD64).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"install_usb.py: {error}", file=sys.stderr)
        raise SystemExit(1)
