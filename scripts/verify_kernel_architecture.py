#!/usr/bin/env python3
"""Verify the single-modular-kernel source and artifact boundary."""

import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARCHIVED_SOURCE = ROOT / "old" / "monolithic-kernel" / "ntoskrnl.cpp"
ARCHIVED_README = ROOT / "old" / "monolithic-kernel" / "README.md"
ACTIVE_MONOLITH = ROOT / "kernel" / "init" / "ntoskrnl.cpp"
ENTRY_DEFINITION = re.compile(
    r'extern\s+"C"\s+[\w:]+\s+KiSystemStartup\s*\(', re.MULTILINE
)
FORBIDDEN_BUILD_PATHS = (
    "kernel/init/ntoskrnl.cpp",
    "old/monolithic-kernel/ntoskrnl.cpp",
)
BUILD_FILES = (
    ROOT / "CMakeLists.txt",
    ROOT / ".github" / "workflows" / "ci.yml",
    ROOT / "scripts" / "build_incremental.ps1",
    ROOT / "scripts" / "stage_micant.py",
    ROOT / "scripts" / "boot_micant.py",
    ROOT / "qemu.py",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_pe(
    path: Path, expected_subsystem: str, *, allow_imports: bool = False,
    allow_no_entry_point: bool = False,
) -> dict[str, int]:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        fail(f"{path}: missing DOS signature")

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset > len(data) - 24 or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        fail(f"{path}: invalid PE signature or header offset")

    machine, section_count, _, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", data, pe_offset + 4
    )
    optional_offset = pe_offset + 24
    if optional_size < 70 or optional_offset + optional_size > len(data):
        fail(f"{path}: truncated PE optional header")

    magic = struct.unpack_from("<H", data, optional_offset)[0]
    entry_rva = struct.unpack_from("<I", data, optional_offset + 16)[0]
    image_size = struct.unpack_from("<I", data, optional_offset + 56)[0]
    subsystem = struct.unpack_from("<H", data, optional_offset + 68)[0]
    expected_subsystem_value = {
        "NATIVE": 1,
        "WINDOWS_GUI": 2,
        "EFI_APPLICATION": 10,
    }.get(expected_subsystem)

    if machine != 0x8664:
        fail(f"{path}: expected AMD64 PE machine, got 0x{machine:04x}")
    if magic != 0x20B:
        fail(f"{path}: expected PE32+ optional header, got 0x{magic:04x}")
    if expected_subsystem_value is None or subsystem != expected_subsystem_value:
        fail(f"{path}: unexpected PE subsystem {subsystem}")
    if (entry_rva == 0 and not allow_no_entry_point) or entry_rva >= image_size:
        fail(f"{path}: entry point RVA is outside the PE image")
    if section_count == 0:
        fail(f"{path}: PE image has no sections")

    section_table = optional_offset + optional_size
    if section_table + section_count * 40 > len(data):
        fail(f"{path}: truncated PE section table")

    if entry_rva:
        executable_entry = False
        for index in range(section_count):
            section = section_table + index * 40
            virtual_size, virtual_address, raw_size = struct.unpack_from(
                "<III", data, section + 8
            )
            characteristics = struct.unpack_from("<I", data, section + 36)[0]
            section_end = virtual_address + max(virtual_size, raw_size)
            if (
                virtual_address <= entry_rva < section_end
                and characteristics & 0x20000000
            ):
                executable_entry = True
                break
        if not executable_entry:
            fail(f"{path}: entry point is not in an executable section")

    if optional_size < 112:
        fail(f"{path}: truncated PE data-directory count")
    number_of_directories = struct.unpack_from(
        "<I", data, optional_offset + 108
    )[0]
    if number_of_directories > 1:
        if optional_size < 128:
            fail(f"{path}: truncated PE import directory entry")
        import_rva, import_size = struct.unpack_from(
            "<II", data, optional_offset + 120
        )
        if not allow_imports and (import_rva != 0 or import_size != 0):
            fail(f"{path}: freestanding image has an external import directory")

    return {
        "machine": machine,
        "magic": magic,
        "subsystem": subsystem,
        "entry_point_rva": entry_rva,
    }


def validate_build_manifest(
    root: Path = ROOT,
    allow_dirty: bool = False,
    *,
    runtime_selftest: bool = False,
    timer_selftest: bool = False,
) -> None:
    manifest_path = root / "build" / "baremetal" / "kernel-build-manifest.json"
    kernel_path = root / "build" / "baremetal" / "ntoskrnl.exe"
    bootloader_path = root / "build" / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"

    if not manifest_path.is_file():
        fail(f"required modular-kernel build manifest is missing: {manifest_path}")
    if not kernel_path.is_file():
        fail(f"required modular kernel is missing: {kernel_path}")
    if not bootloader_path.is_file():
        fail(f"required UEFI bootloader is missing: {bootloader_path}")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read build manifest {manifest_path}: {error}")

    if not isinstance(manifest, dict):
        fail("kernel build manifest root must be an object")
    if manifest.get("schema_version") != 2:
        fail("unsupported or missing kernel build manifest schema_version")
    if manifest.get("runtime_selftest", False) is not runtime_selftest:
        fail("kernel build manifest does not match the requested runtime self-test mode")
    if manifest.get("timer_selftest", False) is not timer_selftest:
        fail("kernel build manifest does not match the requested timer self-test mode")
    if not manifest.get("build_id"):
        fail("kernel build manifest has no build_id")
    if manifest.get("target") != "ntoskrnl_core_baremetal":
        fail("manifest does not identify the modular ntoskrnl_core bare-metal target")
    if manifest.get("architecture") != "AMD64":
        fail("manifest does not identify AMD64")
    if not manifest.get("toolchain") or not manifest.get("configuration"):
        fail("manifest is missing toolchain or configuration metadata")

    try:
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot verify Git build provenance: {error}")

    if manifest.get("git_commit") != head:
        fail("manifest Git commit does not match the current checkout")
    if status.strip() and not allow_dirty:
        fail("refusing to stage artifacts from a dirty working tree")
    if allow_dirty and manifest.get("working_tree_dirty") != bool(status.strip()):
        fail("manifest dirty-tree state does not match the current checkout")

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict):
        fail("manifest artifacts must be an object")

    for name, path, expected_path, subsystem in (
        ("kernel", kernel_path, "build/baremetal/ntoskrnl.exe", "NATIVE"),
        (
            "bootloader",
            bootloader_path,
            "build/esp/EFI/BOOT/BOOTX64.EFI",
            "EFI_APPLICATION",
        ),
        ("display_driver", root / "build" / "virtio_gpu.sys",
         "build/virtio_gpu.sys", "NATIVE"),
    ):
        record = artifacts.get(name)
        if not isinstance(record, dict):
            fail(f"manifest is missing the {name} artifact record")
        if record.get("path") != expected_path:
            fail(f"manifest {name} path does not match the staged artifact")
        try:
            artifact_hash = sha256(path)
            pe = inspect_pe(path, subsystem)
        except OSError as error:
            fail(f"cannot inspect {name} artifact {path}: {error}")
        if record.get("sha256") != artifact_hash:
            fail(f"manifest {name} SHA-256 does not match the artifact")
        if record.get("entry_point_rva") != pe["entry_point_rva"]:
            fail(f"manifest {name} entry point does not match the PE image")
        if record.get("machine") != "AMD64" or record.get("subsystem") != subsystem:
            fail(f"manifest {name} PE metadata does not match the artifact")


def validate_architecture() -> None:
    errors = []
    if ACTIVE_MONOLITH.exists():
        errors.append(f"active monolithic kernel path was recreated: {ACTIVE_MONOLITH}")
    if not ARCHIVED_SOURCE.is_file() or not ARCHIVED_README.is_file():
        errors.append("the historical standalone kernel archive is incomplete")

    active_sources = sorted((ROOT / "kernel").rglob("*.cpp"))
    definitions = [
        path
        for path in active_sources
        if ENTRY_DEFINITION.search(path.read_text(encoding="utf-8", errors="replace"))
    ]
    if len(definitions) != 1:
        errors.append(
            "expected one active KiSystemStartup definition, found "
            + ", ".join(str(path.relative_to(ROOT)) for path in definitions)
        )

    for path in BUILD_FILES:
        if not path.is_file():
            continue
        contents = path.read_text(encoding="utf-8", errors="replace")
        for forbidden in FORBIDDEN_BUILD_PATHS:
            if forbidden in contents:
                errors.append(f"{path.relative_to(ROOT)} references archived source {forbidden}")

    if errors:
        raise RuntimeError("\n".join(errors))
    print("Kernel architecture checks passed: one active modular startup, no monolith build input.")


def main() -> int:
    try:
        validate_architecture()
        if "--verify-build" in sys.argv[1:]:
            validate_build_manifest()
    except RuntimeError as error:
        print(f"Kernel architecture check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
