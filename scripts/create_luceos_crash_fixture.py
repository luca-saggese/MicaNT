#!/usr/bin/env python3
"""Create a deterministic, explicitly synthetic LUCDMP kernel crash fixture."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


HEADER = struct.Struct("<8sIIHHIQQ")
RECORD = struct.Struct("<III")
CRASH_CONTEXT = struct.Struct("<4sHHII4QQ4I4Q16QQQ")
DATA = struct.Struct("<QII")
CRASH_FLAG = 1 << 2
CRASH_FLAG_SYNTHETIC = 1 << 31


def crc32(payload: bytes) -> int:
    return zlib.crc32(payload) & 0xFFFFFFFF


def record(kind: int, payload: bytes) -> bytes:
    return RECORD.pack(kind, len(payload), crc32(payload)) + payload


def build_dump() -> bytes:
    stack_base = 0xFFFF800000120000
    stack_limit = stack_base - 0x8000
    rsp = stack_base - 0x100
    stack = bytes((index * 37 + 11) & 0xFF for index in range(96))
    build_id = 0x53494D554C415445  # "SIMULATE"
    header = HEADER.pack(b"LUCDMP1\0", 1, HEADER.size, 0x8664,
                         CRASH_FLAG, 0, build_id, 0)
    context = CRASH_CONTEXT.pack(
        b"CRSH", 1, CRASH_CONTEXT.size,
        0x1000000E, CRASH_FLAG_SYNTHETIC,
        14, 0x10, 0xFFFF800000001234, 0xDEADBEEF,
        build_id, 0, 4, 12, len(stack),
        0xFFFF800000001234, rsp, 0x202, 0xDEADBEEF,
        0x1111111111111111, 0x2222222222222222,
        0x3333333333333333, 0x4444444444444444,
        rsp, 0x6666666666666666, 0x7777777777777777,
        0x8888888888888888, *range(8, 16),
        stack_limit, stack_base,
    )
    assert len(context) == CRASH_CONTEXT.size == 248
    stack_record = DATA.pack(rsp, len(stack), crc32(stack)) + stack
    return b"".join((header, record(7, context), record(8, stack_record),
                     RECORD.pack(0xFFFFFFFF, 0, 0)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path,
                        nargs="?", default=Path("test/fixtures/luceos-crash-synthetic.dmp"))
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_dump())
    print(f"Wrote synthetic LuceOS crash fixture: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
