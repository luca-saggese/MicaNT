"""Create a small deterministic FAT32 disk image from a directory tree."""

import math
import os
from pathlib import Path
import zlib


SECTOR_SIZE = 512
SECTORS_PER_CLUSTER = 1
TOTAL_SECTORS = 131072
RESERVED_SECTORS = 32
FAT_COUNT = 2
FAT_SECTORS = 1024
DATA_START = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS
# Private GPT type GUID for the dedicated, raw LuceOS crash-record partition.
# GPT stores GUID fields in on-disk little-endian form; keep these bytes as the
# canonical on-disk representation shared with the kernel's partition probe.
LUCEOS_CRASHDUMP_TYPE_GUID = bytes.fromhex(
    "4c554344-4d50-4f53-8000-4c5543454f53".replace("-", "")
)
LUCEOS_CRASHDUMP_SECTORS = 8192


def _short_name(name: str, used: set[bytes]) -> bytes:
    stem, dot, extension = name.rpartition(".")
    if not dot:
        stem, extension = name, ""
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'`"
    clean_stem = "".join(
        character for character in stem.upper() if character in allowed
    ) or "_"
    clean_extension = "".join(
        character for character in extension.upper() if character in allowed
    )[:3]
    direct = (clean_stem[:8].ljust(8) + clean_extension.ljust(3)).encode("ascii")
    if len(clean_stem) <= 8 and len(extension) <= 3 and direct not in used:
        candidate = direct
    else:
        suffix = 1
        while True:
            tail = f"~{suffix}"
            candidate = (
                clean_stem[: 8 - len(tail)] + tail
            ).ljust(8)
            candidate = (candidate + clean_extension.ljust(3)).encode("ascii")
            if candidate not in used:
                break
            suffix += 1
    used.add(candidate)
    return candidate


def _checksum(short_name: bytes) -> int:
    value = 0
    for byte in short_name:
        value = (((value & 1) << 7) + (value >> 1) + byte) & 0xFF
    return value


def _directory_entries(name: str, short_name: bytes, attributes: int,
                       first_cluster: int, size: int) -> list[bytes]:
    records: list[bytes] = []
    if name.upper() != name or len(name) > 12 or "." in name[:-1]:
        encoded = name.encode("utf-16le")
        units = [encoded[i] | encoded[i + 1] << 8 for i in range(0, len(encoded), 2)]
        if len(units) > 247:
            raise ValueError(f"FAT32 path component is too long: {name}")
        units.append(0)
        units.extend([0xFFFF] * ((13 - len(units) % 13) % 13))
        chunks = [units[i:i + 13] for i in range(0, len(units), 13)]
        offsets = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
        checksum = _checksum(short_name)
        for index in range(len(chunks), 0, -1):
            entry = bytearray(32)
            entry[0] = index | (0x40 if index == len(chunks) else 0)
            entry[11] = 0x0F
            entry[13] = checksum
            for offset, unit in zip(offsets, chunks[index - 1]):
                entry[offset:offset + 2] = unit.to_bytes(2, "little")
            records.append(bytes(entry))

    entry = bytearray(32)
    entry[:11] = short_name
    entry[11] = attributes
    entry[20:22] = ((first_cluster >> 16) & 0xFFFF).to_bytes(2, "little")
    entry[26:28] = (first_cluster & 0xFFFF).to_bytes(2, "little")
    entry[28:32] = size.to_bytes(4, "little")
    records.append(bytes(entry))
    return records


def create_fat32_image(source: Path, destination: Path) -> None:
    """Write a whole-disk, partitionless FAT32 image containing source."""
    if not source.is_dir():
        raise ValueError(f"FAT32 source is not a directory: {source}")
    if destination.exists():
        raise FileExistsError(destination)

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    fat = [0] * (FAT_SECTORS * SECTOR_SIZE // 4)
    fat[0], fat[1], fat[2] = 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF
    next_cluster = 3

    def allocate(data: bytes) -> tuple[int, int]:
        nonlocal next_cluster
        count = math.ceil(len(data) / SECTOR_SIZE) if data else 0
        if count == 0:
            return 0, 0
        first = next_cluster
        for cluster in range(first, first + count):
            if cluster >= len(fat) or cluster - 2 >= TOTAL_SECTORS - DATA_START:
                raise ValueError("FAT32 image capacity exceeded")
            fat[cluster] = cluster + 1 if cluster + 1 < first + count else 0x0FFFFFFF
        next_cluster += count
        for index in range(count):
            offset = DATA_START * SECTOR_SIZE + (first + index - 2) * SECTOR_SIZE
            chunk = data[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
            image[offset:offset + len(chunk)] = chunk
        return first, len(data)

    def build_directory(path: Path, cluster: int, parent_cluster: int) -> None:
        entries = [
            _directory_entries(".", b".          ", 0x10, cluster, 0)[-1],
            _directory_entries("..", b"..         ", 0x10, parent_cluster, 0)[-1],
        ]
        used: set[bytes] = {b".          ", b"..         "}
        children = sorted(path.iterdir(), key=lambda item: item.name.lower())
        pending: list[tuple[Path, bytes, int, int, bool]] = []
        for child in children:
            short = _short_name(child.name, used)
            if child.is_dir():
                child_cluster = next_cluster
                allocate(b"\0" * SECTOR_SIZE)
                pending.append((child, short, child_cluster, 0, True))
            elif child.is_file():
                data = child.read_bytes()
                first, size = allocate(data)
                pending.append((child, short, first, size, False))
        for child, short, child_cluster, size, is_directory in pending:
            entries.extend(_directory_entries(
                child.name, short, 0x10 if is_directory else 0x20,
                child_cluster, size,
            ))
        entries.append(bytes(32))
        directory_data = b"".join(entries)
        required_clusters = max(1, math.ceil(len(directory_data) / SECTOR_SIZE))
        if required_clusters > 1:
            extra_first, _ = allocate(b"\0" * ((required_clusters - 1) * SECTOR_SIZE))
            fat[cluster] = extra_first
            chain = list(range(extra_first, extra_first + required_clusters - 1))
        else:
            chain = []
        chain.insert(0, cluster)
        for index, current_cluster in enumerate(chain):
            chunk = directory_data[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
            offset = DATA_START * SECTOR_SIZE + (current_cluster - 2) * SECTOR_SIZE
            image[offset:offset + len(chunk)] = chunk
        for child, _, child_cluster, _, is_directory in pending:
            if is_directory:
                build_directory(child, child_cluster, cluster)

    root_cluster = 2
    build_directory(source, root_cluster, root_cluster)

    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"MICANT  "
    boot[11:13] = SECTOR_SIZE.to_bytes(2, "little")
    boot[13] = SECTORS_PER_CLUSTER
    boot[14:16] = RESERVED_SECTORS.to_bytes(2, "little")
    boot[16] = FAT_COUNT
    boot[32:36] = TOTAL_SECTORS.to_bytes(4, "little")
    boot[36:40] = FAT_SECTORS.to_bytes(4, "little")
    boot[44:48] = root_cluster.to_bytes(4, "little")
    boot[48:50] = (1).to_bytes(2, "little")
    boot[50:52] = (6).to_bytes(2, "little")
    boot[64] = 0x80
    boot[66] = 0x29
    boot[67:71] = b"MICA"
    boot[71:82] = b"MICANT TEST"
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    image[:SECTOR_SIZE] = boot
    fsinfo = bytearray(SECTOR_SIZE)
    fsinfo[0:4] = b"RRaA"
    fsinfo[484:488] = b"rrAa"
    fsinfo[488:492] = (len(fat) - next_cluster).to_bytes(4, "little")
    fsinfo[492:496] = next_cluster.to_bytes(4, "little")
    fsinfo[508:512] = b"\x00\x00\x55\xAA"
    image[SECTOR_SIZE:2 * SECTOR_SIZE] = fsinfo
    for copy in range(FAT_COUNT):
        offset = (RESERVED_SECTORS + copy * FAT_SECTORS) * SECTOR_SIZE
        for index, value in enumerate(fat):
            start = offset + index * 4
            image[start:start + 4] = value.to_bytes(4, "little")

    with destination.open("xb") as output:
        output.write(image)


def create_gpt_disk_image(volume: Path, destination: Path, *,
                          reserve_crash_dump: bool = False) -> None:
    """Place a FAT32 volume in GPT; optionally reserve a private raw dump partition."""
    volume_sectors = volume.stat().st_size // SECTOR_SIZE
    if volume_sectors == 0 or volume.stat().st_size % SECTOR_SIZE:
        raise ValueError("GPT partition image must contain whole sectors")
    partition_start = 2048
    crash_sectors = LUCEOS_CRASHDUMP_SECTORS if reserve_crash_dump else 0
    total_sectors = partition_start + volume_sectors + crash_sectors + 4096
    backup_header_lba = total_sectors - 1
    backup_entries_lba = backup_header_lba - 32
    first_usable = 34
    last_usable = backup_entries_lba - 1
    partition_last = partition_start + volume_sectors - 1
    if partition_last > last_usable:
        raise ValueError("GPT disk image has no room for backup metadata")

    entries = bytearray(128 * 128)
    entry = memoryview(entries)[:128]
    entry[0:16] = bytes.fromhex("A2A0D0EBE5B9334487C068B6B72699C7")
    entry[16:32] = bytes(range(1, 17))
    entry[32:40] = partition_start.to_bytes(8, "little")
    entry[40:48] = partition_last.to_bytes(8, "little")
    entry[56:72] = "MICA_SYS".encode("utf-16le")
    if reserve_crash_dump:
        # Slot four intentionally leaves the three positions consumed by the
        # current FAT boot mapper untouched. The reserved range starts exactly
        # after the FAT volume and ends before GPT's backup metadata area.
        crash = memoryview(entries)[3 * 128:4 * 128]
        crash[0:16] = LUCEOS_CRASHDUMP_TYPE_GUID
        crash[16:32] = bytes.fromhex("4c5543444d504f530001000000000001")
        crash_start = partition_start + volume_sectors
        crash[32:40] = crash_start.to_bytes(8, "little")
        crash[40:48] = (crash_start + crash_sectors - 1).to_bytes(8, "little")
        crash[56:88] = "LUCEOS_CRASHDUMP".encode("utf-16le")
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk_guid = bytes(range(0xA0, 0xB0))

    def make_header(current_lba: int, alternate_lba: int, entries_lba: int) -> bytes:
        header = bytearray(SECTOR_SIZE)
        header[0:8] = b"EFI PART"
        header[8:12] = (0x00010000).to_bytes(4, "little")
        header[12:16] = (92).to_bytes(4, "little")
        header[24:32] = current_lba.to_bytes(8, "little")
        header[32:40] = alternate_lba.to_bytes(8, "little")
        header[40:48] = first_usable.to_bytes(8, "little")
        header[48:56] = last_usable.to_bytes(8, "little")
        header[56:72] = disk_guid
        header[72:80] = entries_lba.to_bytes(8, "little")
        header[80:84] = (128).to_bytes(4, "little")
        header[84:88] = (128).to_bytes(4, "little")
        header[88:92] = entries_crc.to_bytes(4, "little")
        header[16:20] = (zlib.crc32(header[:92]) & 0xFFFFFFFF).to_bytes(4, "little")
        return bytes(header)

    with destination.open("xb") as output:
        output.truncate(total_sectors * SECTOR_SIZE)
        mbr = bytearray(SECTOR_SIZE)
        protective = 446
        mbr[protective + 4] = 0xEE
        mbr[protective + 8:protective + 12] = (1).to_bytes(4, "little")
        mbr[protective + 12:protective + 16] = min(total_sectors - 1, 0xFFFFFFFF).to_bytes(4, "little")
        mbr[510:512] = b"\x55\xAA"
        output.seek(0); output.write(mbr)
        output.seek(SECTOR_SIZE); output.write(make_header(1, backup_header_lba, 2))
        output.seek(2 * SECTOR_SIZE); output.write(entries)
        output.seek(backup_entries_lba * SECTOR_SIZE); output.write(entries)
        output.seek(backup_header_lba * SECTOR_SIZE)
        output.write(make_header(backup_header_lba, 1, backup_entries_lba))
        output.seek(partition_start * SECTOR_SIZE)
        with volume.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                output.write(chunk)
