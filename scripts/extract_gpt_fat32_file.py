#!/usr/bin/env python3
"""Read one file from the GPT Basic Data FAT32 partition of a disk image."""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path


SECTOR_SIZE = 512
GPT_BASIC_DATA = bytes.fromhex("A2A0D0EBE5B9334487C068B6B72699C7")
GPT_HEADER = struct.Struct("<8sIIIIQQQQ16sQIII")


class ImageError(ValueError):
    pass


def _read_at(stream, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0:
        raise ImageError("negative disk offset or length")
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise ImageError("truncated disk image")
    return data


def _partition(stream) -> tuple[int, int]:
    sector = _read_at(stream, SECTOR_SIZE, SECTOR_SIZE)
    if sector[:8] != b"EFI PART":
        raise ImageError("missing primary GPT header")
    header_size = struct.unpack_from("<I", sector, 12)[0]
    expected_header_crc = struct.unpack_from("<I", sector, 16)[0]
    if not 92 <= header_size <= SECTOR_SIZE:
        raise ImageError("invalid GPT header size")
    checked = bytearray(sector[:header_size])
    checked[16:20] = b"\0" * 4
    if zlib.crc32(checked) & 0xFFFFFFFF != expected_header_crc:
        raise ImageError("GPT header CRC mismatch")
    entries_lba = struct.unpack_from("<Q", sector, 72)[0]
    entry_count, entry_size, entries_crc = struct.unpack_from("<III", sector, 80)
    if not entry_count or entry_count > 4096 or entry_size != 128:
        raise ImageError("unsupported GPT partition table geometry")
    table_size = entry_count * entry_size
    table = _read_at(stream, entries_lba * SECTOR_SIZE, table_size)
    if zlib.crc32(table) & 0xFFFFFFFF != entries_crc:
        raise ImageError("GPT partition table CRC mismatch")
    found = []
    for index in range(entry_count):
        entry = table[index * entry_size:(index + 1) * entry_size]
        if entry[:16] == GPT_BASIC_DATA:
            first, last = struct.unpack_from("<QQ", entry, 32)
            if first > last:
                raise ImageError("invalid Basic Data partition extent")
            found.append((first, last - first + 1))
    if len(found) != 1:
        raise ImageError(f"expected one GPT Basic Data partition, found {len(found)}")
    return found[0]


def _fat_geometry(stream, partition_lba: int) -> tuple[int, int, int, int, int]:
    boot = _read_at(stream, partition_lba * SECTOR_SIZE, SECTOR_SIZE)
    if boot[510:512] != b"\x55\xaa":
        raise ImageError("invalid FAT boot sector signature")
    bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
    sectors_per_cluster = boot[13]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fat_count = boot[16]
    sectors_per_fat = struct.unpack_from("<I", boot, 36)[0]
    root_cluster = struct.unpack_from("<I", boot, 44)[0]
    if (bytes_per_sector != SECTOR_SIZE or not sectors_per_cluster or
            sectors_per_cluster & (sectors_per_cluster - 1) or not reserved or
            not fat_count or not sectors_per_fat or root_cluster < 2):
        raise ImageError("unsupported FAT32 geometry")
    fat_start = partition_lba + reserved
    data_start = fat_start + fat_count * sectors_per_fat
    return sectors_per_cluster, fat_start, sectors_per_fat, data_start, root_cluster


def _cluster_bytes(stream, cluster: int, sectors_per_cluster: int,
                   data_start: int) -> bytes:
    if cluster < 2:
        raise ImageError("invalid FAT cluster")
    sector = data_start + (cluster - 2) * sectors_per_cluster
    return _read_at(stream, sector * SECTOR_SIZE,
                    sectors_per_cluster * SECTOR_SIZE)


def _cluster_chain(stream, first: int, sectors_per_cluster: int,
                   fat_start: int, sectors_per_fat: int,
                   data_start: int) -> bytes:
    fat_bytes = sectors_per_fat * SECTOR_SIZE
    visited: set[int] = set()
    parts: list[bytes] = []
    cluster = first
    while True:
        if cluster in visited:
            raise ImageError("FAT cluster chain contains a loop")
        if cluster < 2 or cluster * 4 + 4 > fat_bytes:
            raise ImageError("FAT cluster chain points outside the FAT")
        visited.add(cluster)
        parts.append(_cluster_bytes(stream, cluster, sectors_per_cluster, data_start))
        fat_entry = _read_at(stream, fat_start * SECTOR_SIZE + cluster * 4, 4)
        next_cluster = struct.unpack("<I", fat_entry)[0] & 0x0FFFFFFF
        if next_cluster >= 0x0FFFFFF8:
            break
        if next_cluster == 0x0FFFFFF7 or next_cluster == 0:
            raise ImageError("FAT cluster chain is bad or prematurely free")
        cluster = next_cluster
    return b"".join(parts)


def _find_short_entry(directory: bytes, name: bytes, want_directory: bool) -> tuple[int, int] | None:
    for offset in range(0, len(directory), 32):
        entry = directory[offset:offset + 32]
        if len(entry) < 32 or entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F:
            continue
        is_directory = bool(entry[11] & 0x10)
        if is_directory != want_directory or entry[:11] != name:
            continue
        first_cluster = (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]
        size = struct.unpack_from("<I", entry, 28)[0]
        return first_cluster, size
    return None


def _lfn_piece(entry: bytes) -> tuple[int, str]:
    ordinal = entry[0] & 0x1F
    units = (entry[1:11] + entry[14:26] + entry[28:32])
    decoded = units.decode("utf-16le", errors="strict")
    return ordinal, decoded.split("\0", 1)[0].rstrip("\uffff")


def extract_minidump(image: Path, output: Path) -> tuple[Path, str]:
    with image.open("rb") as stream:
        partition_lba, _ = _partition(stream)
        geometry = _fat_geometry(stream, partition_lba)
        sectors_per_cluster, fat_start, sectors_per_fat, data_start, root = geometry
        root_bytes = _cluster_chain(stream, root, sectors_per_cluster,
                                    fat_start, sectors_per_fat, data_start)
        windows = _find_short_entry(root_bytes, b"WINDOWS    ", True)
        if not windows:
            raise ImageError("Windows directory not found in Basic Data volume")
        windows_bytes = _cluster_chain(stream, windows[0], sectors_per_cluster,
                                       fat_start, sectors_per_fat, data_start)
        minidump = _find_short_entry(windows_bytes, b"MINIDUMP   ", True)
        if not minidump:
            raise ImageError("Windows\\Minidump directory not found")
        directory = _cluster_chain(stream, minidump[0], sectors_per_cluster,
                                   fat_start, sectors_per_fat, data_start)
        candidates: list[tuple[str, int, int]] = []
        long_name_parts: dict[int, str] = {}
        for offset in range(0, len(directory), 32):
            entry = directory[offset:offset + 32]
            if len(entry) < 32 or entry[0] == 0:
                break
            if entry[0] == 0xE5:
                long_name_parts.clear()
                continue
            if entry[11] == 0x0F:
                try:
                    ordinal, piece = _lfn_piece(entry)
                    if ordinal:
                        long_name_parts[ordinal] = piece
                except UnicodeDecodeError as error:
                    raise ImageError("invalid UTF-16 in FAT long filename") from error
                continue
            if entry[11] & 0x10:
                long_name_parts.clear()
                continue
            if entry[8:11] != b"DMP":
                long_name_parts.clear()
                continue
            first_cluster = (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]
            size = struct.unpack_from("<I", entry, 28)[0]
            if first_cluster >= 2 and size:
                short_name = entry[:11]
                short_filename = (short_name[:8].decode("ascii").rstrip() + "." +
                                  short_name[8:11].decode("ascii").rstrip())
                filename = "".join(long_name_parts[index] for index in sorted(long_name_parts))
                if not filename:
                    filename = short_filename
                candidates.append((filename, first_cluster, size))
            long_name_parts.clear()
        if len(candidates) != 1:
            raise ImageError(f"expected one .dmp file in Minidump, found {len(candidates)}")
        data = _cluster_chain(stream, candidates[0][1], sectors_per_cluster,
                              fat_start, sectors_per_fat, data_start)
        if candidates[0][2] > len(data):
            raise ImageError("dump size exceeds its FAT cluster chain")
        payload = data[:candidates[0][2]]
        if not payload.startswith(b"LUCDMP1\0"):
            raise ImageError("exported file is not a LuceOS LUCDMP dump")
        filename = candidates[0][0]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    return output, filename


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        output, filename = extract_minidump(args.image, args.output)
        print(f"FAT file: {filename}\nExtracted {output}")
    except (OSError, ImageError) as error:
        print(f"extract_gpt_fat32_file: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
