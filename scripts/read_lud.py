#!/usr/bin/env python3
"""Decode and validate LuceOS process and kernel crash dumps."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path


MAGIC = b"LUCDMP1\0"
HEADER = struct.Struct("<8sIIHHIQQ")
RECORD = struct.Struct("<III")
REGION = struct.Struct("<QQIIII")
DATA = struct.Struct("<QII")
PROCESS = struct.Struct("<IIIHH")
MODULE = struct.Struct("<QQHHHH")
THREAD_SIZE = 104  # 80-byte SYSTEM_THREAD_INFORMATION + TEB and stack bounds
CRASH_FLAG = 1 << 2
CRASH_FLAG_SYNTHETIC = 1 << 31
CRASH_CONTEXT_SIZE = 248
CRASH_CONTEXT_PREFIX = struct.Struct("<4sHHII4QQ4I4Q16QQQ")
CRASH_STACK_MAX = 512


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def u16(data: bytes) -> str:
    return data.decode("utf-16-le", errors="replace").rstrip("\0")


def parse(path: Path, extract: Path | None) -> tuple[dict, list[dict], bool, list[str]]:
    raw = path.read_bytes()
    errors: list[str] = []
    records: list[dict] = []
    if len(raw) < HEADER.size:
        raise ValueError(f"file is shorter than the {HEADER.size}-byte header")

    magic, version, header_size, arch, flags, pid, build_id, capture_time = HEADER.unpack_from(raw)
    if magic != MAGIC:
        raise ValueError("invalid LuceOS dump signature")
    if version != 1:
        errors.append(f"unsupported format version {version}")
    if header_size < HEADER.size or header_size > len(raw):
        raise ValueError(f"invalid header size {header_size}")

    header = {
        "version": version,
        "header_size": header_size,
        "architecture": "AMD64" if arch == 0x8664 else f"unknown (0x{arch:04x})",
        "flags": flags,
        "live_best_effort": bool(flags & 1),
        "mini_dump": bool(flags & 2),
        "kernel_crash": bool(flags & CRASH_FLAG),
        "pid": pid,
        "kernel_build_id": build_id,
        "capture_time": capture_time,
        "file_size": len(raw),
    }

    offset = header_size
    ended = False
    data_index = 0
    crash_context: dict | None = None
    crash_stack_seen = False
    while offset < len(raw):
        if len(raw) - offset < RECORD.size:
            errors.append(f"truncated record header at file offset 0x{offset:x}")
            break
        kind, size, expected_crc = RECORD.unpack_from(raw, offset)
        offset += RECORD.size
        if size > len(raw) - offset:
            errors.append(f"record type {kind} declares {size} bytes past EOF at 0x{offset:x}")
            break
        payload = raw[offset:offset + size]
        offset += size
        actual_crc = crc32(payload)
        valid_crc = actual_crc == expected_crc
        item: dict = {"type": kind, "payload_size": size, "crc32": expected_crc,
                      "crc_valid": valid_crc}
        if not valid_crc:
            errors.append(f"record type {kind} CRC mismatch (expected {expected_crc:08x}, got {actual_crc:08x})")

        if kind == 0xFFFFFFFF:
            if size != 0 or expected_crc != 0:
                errors.append("end marker must have an empty payload and zero checksum")
            records.append(item)
            ended = True
            if offset != len(raw):
                errors.append(f"{len(raw) - offset} trailing bytes after end marker")
            break
        if kind == 1:
            if size != REGION.size:
                errors.append(f"invalid region payload size {size}")
            else:
                base, length, state, protect, region_type, reserved = REGION.unpack(payload)
                item.update(base=base, size=length, state=state, protect=protect,
                            region_type=region_type, reserved=reserved)
        elif kind == 2:
            if size < DATA.size:
                errors.append(f"short data record ({size} bytes)")
            else:
                base, byte_count, data_crc = DATA.unpack_from(payload)
                chunk = payload[DATA.size:]
                valid_data_crc = len(chunk) == byte_count and crc32(chunk) == data_crc
                if len(chunk) != byte_count:
                    errors.append(f"data record length says {byte_count}, contains {len(chunk)}")
                if not valid_data_crc:
                    errors.append(f"data block at 0x{base:x} CRC/length mismatch")
                item.update(base=base, byte_count=byte_count, data_crc32=data_crc,
                            data_crc_valid=valid_data_crc,
                            preview=chunk[:32].hex(" "))
                if extract is not None:
                    extract.mkdir(parents=True, exist_ok=True)
                    (extract / f"{data_index:04d}-0x{base:016x}.bin").write_bytes(chunk)
                data_index += 1
        elif kind == 3:
            if size != DATA.size:
                errors.append(f"invalid unreadable-range payload size {size}")
            else:
                base, byte_count, data_crc = DATA.unpack(payload)
                item.update(base=base, byte_count=byte_count, data_crc32=data_crc)
        elif kind == 4:
            if size < PROCESS.size:
                errors.append(f"short process record ({size} bytes)")
            else:
                process_pid, handles, threads, name_bytes, reserved = PROCESS.unpack_from(payload)
                name = payload[PROCESS.size:]
                if len(name) != name_bytes or name_bytes % 2:
                    errors.append("invalid process image-name length")
                item.update(pid=process_pid, handle_count=handles, thread_count=threads,
                            image_name=u16(name[:name_bytes]), reserved=reserved)
        elif kind == 5:
            if size != THREAD_SIZE:
                errors.append(f"thread record is {size} bytes; expected {THREAD_SIZE}")
            else:
                kt, ut, create = struct.unpack_from("<qqq", payload, 0)
                start = struct.unpack_from("<Q", payload, 32)[0]
                thread_id = struct.unpack_from("<Q", payload, 48)[0]
                priority, base_priority = struct.unpack_from("<ii", payload, 56)
                switches, state = struct.unpack_from("<II", payload, 64)
                wait = struct.unpack_from("<I", payload, 72)[0]
                stack_limit = struct.unpack_from("<Q", payload, 96)[0]
                teb, stack_base = struct.unpack_from("<QQ", payload, 80)
                item.update(thread_id=thread_id, start_address=start, priority=priority,
                            base_priority=base_priority, state=state, wait_reason=wait,
                            kernel_time=kt, user_time=ut, create_time=create,
                            context_switches=switches, teb=teb, stack_base=stack_base,
                            stack_limit=stack_limit)
        elif kind == 6:
            if size < MODULE.size:
                errors.append(f"short module record ({size} bytes)")
            else:
                base, image_size, path_bytes, r0, r1, r2 = MODULE.unpack_from(payload)
                name = payload[MODULE.size:]
                if len(name) != path_bytes or path_bytes % 2:
                    errors.append("invalid module path length")
                item.update(base=base, image_size=image_size,
                            path=u16(name[:path_bytes]), reserved=[r0, r1, r2])
        elif kind == 7:
            if size != CRASH_CONTEXT_SIZE:
                errors.append(f"kernel crash context is {size} bytes; expected {CRASH_CONTEXT_SIZE}")
            else:
                if crash_context is not None:
                    errors.append("multiple kernel crash context records")
                fields = CRASH_CONTEXT_PREFIX.unpack(payload[:CRASH_CONTEXT_PREFIX.size])
                magic, crash_version, context_size, stop_code, crash_flags = fields[:5]
                params = fields[5:9]
                build_id = fields[9]
                cpu, crash_pid, tid, stack_bytes = fields[10:14]
                rip, rsp, rflags, cr2 = fields[14:18]
                gprs = fields[18:34]
                stack_limit, stack_base = fields[34:36]
                if magic != b"CRSH":
                    errors.append("kernel crash context has invalid CRSH signature")
                if crash_version != 1 or context_size != CRASH_CONTEXT_SIZE:
                    errors.append(f"unsupported kernel crash context version/size {crash_version}/{context_size}")
                if stack_bytes > CRASH_STACK_MAX:
                    errors.append(f"kernel crash stack length {stack_bytes} exceeds {CRASH_STACK_MAX}")
                crash_context = {
                    "stop_code": stop_code, "flags": crash_flags,
                    "synthetic": bool(crash_flags & CRASH_FLAG_SYNTHETIC),
                    "parameters": list(params), "build_id": build_id,
                    "cpu": cpu, "pid": crash_pid, "tid": tid,
                    "stack_bytes": stack_bytes, "rip": rip, "rsp": rsp,
                    "rflags": rflags, "cr2": cr2,
                    "gprs": list(gprs), "stack_limit": stack_limit,
                    "stack_base": stack_base,
                }
                item.update(crash_context)
                if header["kernel_build_id"] not in (0, build_id):
                    errors.append("kernel crash context build ID disagrees with dump header")
                if header["pid"] != 0:
                    errors.append("kernel crash dump header PID must be zero")
        elif kind == 8:
            if size < DATA.size:
                errors.append(f"short kernel stack record ({size} bytes)")
            else:
                base, byte_count, data_crc = DATA.unpack_from(payload)
                chunk = payload[DATA.size:]
                valid_data_crc = len(chunk) == byte_count and crc32(chunk) == data_crc
                if len(chunk) != byte_count:
                    errors.append(f"kernel stack record length says {byte_count}, contains {len(chunk)}")
                if byte_count > CRASH_STACK_MAX:
                    errors.append(f"kernel stack length {byte_count} exceeds {CRASH_STACK_MAX}")
                if not valid_data_crc:
                    errors.append("kernel stack data CRC/length mismatch")
                if crash_stack_seen:
                    errors.append("multiple kernel crash stack records")
                crash_stack_seen = True
                if crash_context is None:
                    errors.append("kernel stack record precedes crash context")
                else:
                    if byte_count != crash_context["stack_bytes"]:
                        errors.append("kernel stack record length disagrees with crash context")
                    if base != crash_context["rsp"]:
                        errors.append("kernel stack record base disagrees with captured RSP")
                item.update(base=base, byte_count=byte_count, data_crc32=data_crc,
                            data_crc_valid=valid_data_crc,
                            preview=chunk[:32].hex(" "))
                if extract is not None:
                    extract.mkdir(parents=True, exist_ok=True)
                    (extract / f"{data_index:04d}-0x{base:016x}-kernel-stack.bin").write_bytes(chunk)
                data_index += 1
        else:
            item["unknown_payload_hex"] = payload[:64].hex(" ")
        records.append(item)

    if header["kernel_crash"]:
        if crash_context is None:
            errors.append("kernel crash flag is set but type 7 context is missing")
        if crash_context is not None and crash_context["stack_bytes"] and not crash_stack_seen:
            errors.append("kernel crash context declares a stack but type 8 record is missing")
    elif crash_context is not None or crash_stack_seen:
        errors.append("kernel crash records require the kernel crash header flag")
    return header, records, ended, errors


def fmt_addr(value: int) -> str:
    return f"0x{value:016X}"


def report(header: dict, records: list[dict], ended: bool, errors: list[str]) -> str:
    dump_kind = "kernel crash dump" if header["kernel_crash"] else "process dump"
    lines = [f"LuceOS {dump_kind} v{header['version']}",
             f"Header PID: {header['pid']}  |  {header['architecture']}  |  {header['file_size']:,} bytes",
             f"Capture: {'live best-effort' if header['live_best_effort'] else 'snapshot'}"
             f"{'  |  mini dump' if header['mini_dump'] else '  |  full dump'}",
             f"Kernel build ID: {header['kernel_build_id']}  |  capture time: {header['capture_time']}", ""]
    counts: dict[int, int] = {}
    for rec in records:
        counts[rec["type"]] = counts.get(rec["type"], 0) + 1
    names = {1: "regions", 2: "data blocks", 3: "unreadable ranges", 4: "process records",
             5: "thread records", 6: "modules", 7: "kernel contexts", 8: "kernel stacks",
             0xFFFFFFFF: "end markers"}
    lines.append("Contents: " + ", ".join(f"{count} {names.get(kind, f'type {kind}') }"
                                            for kind, count in sorted(counts.items())))
    lines.append(f"Capture status: {'complete' if ended else 'PARTIAL (no end marker)'}")
    for rec in records:
        kind = rec["type"]
        if kind == 4:
            lines += ["", f"Process: {rec.get('image_name', '<invalid name>')} (PID {rec.get('pid', '?')})",
                      f"  Threads: {rec.get('thread_count', '?')}  |  handles: {rec.get('handle_count', '?')}"]
        elif kind == 5:
            lines += ["", f"Thread {rec.get('thread_id', '?')}: start {fmt_addr(rec.get('start_address', 0))}",
                      f"  TEB {fmt_addr(rec.get('teb', 0))}  |  stack {fmt_addr(rec.get('stack_limit', 0))}–{fmt_addr(rec.get('stack_base', 0))}",
                      f"  State {rec.get('state', '?')}  |  priority {rec.get('priority', '?')}  |  user/kernel time {rec.get('user_time', '?')}/{rec.get('kernel_time', '?')}"]
        elif kind == 6:
            lines += ["", f"Module: {rec.get('path', '<invalid path>')}",
                      f"  Base {fmt_addr(rec.get('base', 0))}  |  size 0x{rec.get('image_size', 0):X}"]
        elif kind == 1:
            lines.append(f"Region {fmt_addr(rec['base'])}–{fmt_addr(rec['base'] + rec['size'])}: "
                         f"{rec['size']:,} bytes  state=0x{rec['state']:X} protect=0x{rec['protect']:X} type=0x{rec['region_type']:X}")
        elif kind == 2:
            lines.append(f"Data {fmt_addr(rec['base'])}: {rec['byte_count']:,} bytes  "
                         f"CRC={'OK' if rec.get('data_crc_valid') else 'BAD'}  preview={rec.get('preview', '')}")
        elif kind == 3:
            lines.append(f"Unreadable {fmt_addr(rec['base'])}: {rec['byte_count']:,} bytes")
        elif kind == 7:
            lines += ["", f"Kernel bugcheck: 0x{rec['stop_code']:08X}  |  CPU {rec['cpu']}  |  PID/TID {rec['pid']}/{rec['tid']}",
                      "  Parameters: " + ", ".join(fmt_addr(value) for value in rec["parameters"]),
                      f"  RIP {fmt_addr(rec['rip'])}  RSP {fmt_addr(rec['rsp'])}  RFLAGS {fmt_addr(rec['rflags'])}  CR2 {fmt_addr(rec['cr2'])}",
                      f"  Stack {fmt_addr(rec['stack_limit'])}–{fmt_addr(rec['stack_base'])}  |  captured {rec['stack_bytes']} bytes",
                      f"  Context build ID: {rec['build_id']}  |  flags 0x{rec['flags']:X}"]
            if rec["synthetic"]:
                lines.append("  Provenance: SYNTHETIC FIXTURE (not captured by the kernel)")
            lines.append("  GPRs: " + " ".join(fmt_addr(value) for value in rec["gprs"]))
        elif kind == 8:
            lines.append(f"Kernel stack {fmt_addr(rec['base'])}: {rec['byte_count']} bytes  "
                         f"CRC={'OK' if rec.get('data_crc_valid') else 'BAD'}  preview={rec.get('preview', '')}")
    lines += ["", f"Integrity: {'OK' if not errors else f'{len(errors)} issue(s)'}"]
    lines.extend(f"  ERROR: {error}" for error in errors)
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="input .LUD file")
    parser.add_argument("--extract", type=Path, help="write each memory data block as a separate .bin file")
    parser.add_argument("--json", action="store_true", help="print decoded records as JSON")
    args = parser.parse_args()
    try:
        header, records, ended, errors = parse(args.dump, args.extract)
    except (OSError, ValueError) as exc:
        print(f"read_lud: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({"header": header, "records": records, "complete": ended,
                          "errors": errors}, indent=2))
    else:
        print(report(header, records, ended, errors))
        if args.extract is not None:
            print(f"\nExtracted memory blocks: {args.extract}")
    return 0 if ended and not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
