#!/usr/bin/env python3
"""Generate a PE group-cursor resource pair from a standalone CUR file."""

import argparse
import pathlib
import struct


def read_cursor(path: pathlib.Path) -> tuple[bytes, bytes, int, int, int, int]:
    data = path.read_bytes()
    if len(data) < 6:
        raise ValueError("CUR header is truncated")
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if reserved != 0 or kind != 2 or count == 0 or count > 1024:
        raise ValueError("input is not a valid CUR directory")
    if len(data) < 6 + count * 16:
        raise ValueError("CUR directory is truncated")

    candidates = []
    for index in range(count):
        width, height, colors, entry_reserved, hot_x, hot_y, size, offset = \
            struct.unpack_from("<BBBBHHII", data, 6 + index * 16)
        width = width or 256
        height = height or 256
        end = offset + size
        if (entry_reserved != 0 or hot_x >= width or hot_y >= height or size < 5 or
                offset < 6 + count * 16 or end > len(data)):
            raise ValueError("CUR image entry is invalid")
        candidates.append((width, height, colors, hot_x, hot_y, data[offset:end]))

    width, height, colors, hot_x, hot_y, image = min(
        candidates, key=lambda item: abs(item[0] - 32) + abs(item[1] - 32))
    group_entry = struct.pack(
        "<BBBBHHIH", width & 0xFF if width < 256 else 0,
        height & 0xFF if height < 256 else 0, colors, 0,
        hot_x, hot_y, len(image) + 4, 2)
    group = struct.pack("<HHH", 0, 2, 1) + group_entry
    payload = struct.pack("<HH", hot_x, hot_y) + image
    return group, payload, width, height, hot_x, hot_y


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("group", type=pathlib.Path)
    parser.add_argument("payload", type=pathlib.Path)
    parser.add_argument("rc", type=pathlib.Path)
    args = parser.parse_args()
    group, payload, *_ = read_cursor(args.source)
    args.group.parent.mkdir(parents=True, exist_ok=True)
    args.payload.parent.mkdir(parents=True, exist_ok=True)
    args.rc.parent.mkdir(parents=True, exist_ok=True)
    args.group.write_bytes(group)
    args.payload.write_bytes(payload)
    args.rc.write_text(
        f'1 12 DISCARDABLE "{args.group.resolve()}"\n'
        f'2 1 DISCARDABLE "{args.payload.resolve()}"\n',
        encoding="ascii")


if __name__ == "__main__":
    main()
