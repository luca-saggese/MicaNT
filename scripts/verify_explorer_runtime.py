#!/usr/bin/env python3
"""Check the real Explorer PE import closure without loading host Windows DLLs."""

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import NoReturn

from verify_kernel_architecture import inspect_pe


ROOT = Path(__file__).resolve().parents[1]
Symbol = str | int


def module_name(name: str) -> str:
    lowered = name.lower()
    if "/" in lowered or "\\" in lowered or not lowered:
        raise RuntimeError(f"Invalid PE module name: {name!r}")
    return lowered if lowered.endswith(".dll") else lowered + ".dll"


def api_set_host(name: str) -> str:
    name = module_name(name)
    if name.startswith("api-ms-win-crt-"):
        return "ucrtbase.dll"
    return name


@dataclass(frozen=True)
class Import:
    module: str
    symbol: Symbol


class PeSymbols:
    """Bounded file-backed PE32+ import/export reader; it never executes an image."""

    def __init__(self, path: Path) -> None:
        is_dll = path.suffix.lower() == ".dll"
        expected_subsystem = "WINDOWS_GUI"
        if is_dll:
            data = path.read_bytes()
            if len(data) < 0x40 or data[:2] != b"MZ":
                raise RuntimeError(f"{path}: missing DOS signature")
            pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
            if pe_offset > len(data) - 24 or data[pe_offset:pe_offset + 4] != b"PE\0\0":
                raise RuntimeError(f"{path}: invalid PE signature or header offset")
            subsystem_offset = pe_offset + 24 + 68
            if subsystem_offset + 2 > len(data):
                raise RuntimeError(f"{path}: truncated PE subsystem field")
            subsystem = struct.unpack_from("<H", data, subsystem_offset)[0]
            expected_subsystem = {1: "NATIVE", 2: "WINDOWS_GUI"}.get(subsystem, "")
            if not expected_subsystem:
                raise RuntimeError(f"{path}: unsupported DLL PE subsystem {subsystem}")
        inspect_pe(path, expected_subsystem, allow_imports=True,
                   allow_no_entry_point=is_dll)
        self.path = path
        self.data = path.read_bytes()
        pe = self.unpack("<I", 0x3C)[0]
        optional = pe + 24
        optional_size = self.unpack("<H", pe + 20)[0]
        self.header_size = self.unpack("<I", optional + 60)[0]
        section_count = self.unpack("<H", pe + 6)[0]
        section_table = optional + optional_size
        self.sections: list[tuple[int, int, int, int]] = []
        for index in range(section_count):
            section = section_table + index * 40
            virtual_size, rva, raw_size, raw = self.unpack("<IIII", section + 8)
            self.sections.append((rva, max(virtual_size, raw_size), raw, raw_size))
        count = self.unpack("<I", optional + 108)[0]
        if count > 16 or count * 8 > optional_size - 112:
            self.fail("data directories exceed the optional header")
        self.directories = [
            self.unpack("<II", optional + 112 + index * 8)
            for index in range(count)
        ]
        self.imports = self.read_imports()
        self.exports = self.read_exports()

    def fail(self, message: str) -> NoReturn:
        raise RuntimeError(f"{self.path}: {message}")

    def unpack(self, layout: str, offset: int) -> tuple[int, ...]:
        if offset < 0 or struct.calcsize(layout) > len(self.data) - offset:
            self.fail(f"truncated PE data at file offset 0x{offset:x}")
        return struct.unpack_from(layout, self.data, offset)

    def offset(self, rva: int, size: int = 1) -> int:
        if rva < 0 or size <= 0:
            self.fail("invalid RVA range")
        if rva < self.header_size and size <= self.header_size - rva:
            if size <= len(self.data) - rva:
                return rva
        for start, virtual_size, raw, raw_size in self.sections:
            if start <= rva < start + virtual_size:
                delta = rva - start
                if size <= raw_size - delta and size <= len(self.data) - raw - delta:
                    return raw + delta
        self.fail(f"RVA 0x{rva:x} size {size} is not file-backed")

    def string(self, rva: int) -> str:
        start = self.offset(rva)
        end = self.data.find(b"\0", start)
        if end < 0:
            self.fail("unterminated PE string")
        self.offset(rva, end - start + 1)
        try:
            return self.data[start:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise RuntimeError(f"{self.path}: non-ASCII PE symbol") from error

    def directory(self, index: int) -> tuple[int, int]:
        if index >= len(self.directories):
            return (0, 0)
        rva, size = self.directories[index]
        if bool(rva) != bool(size):
            self.fail("incomplete PE directory")
        if size:
            self.offset(rva, size)
        return rva, size

    def read_imports(self) -> list[Import]:
        rva, size = self.directory(1)
        imports: list[Import] = []
        if not size:
            return imports
        for descriptor in range(rva, rva + size - 19, 20):
            lookup, stamp, chain, name, iat = self.unpack("<IIIII", self.offset(descriptor, 20))
            if not any((lookup, stamp, chain, name, iat)):
                return imports
            if not name or not iat:
                self.fail("invalid import descriptor")
            dll = module_name(self.string(name))
            thunk = lookup or iat
            index = 0
            while True:
                value = self.unpack("<Q", self.offset(thunk + index * 8, 8))[0]
                self.offset(iat + index * 8, 8)
                if value == 0:
                    break
                if value & (1 << 63):
                    if value & 0x7FFFFFFFFFFF0000:
                        self.fail("invalid ordinal import")
                    symbol: Symbol = value & 0xFFFF
                else:
                    if value > 0xFFFFFFFF:
                        self.fail("import name RVA exceeds 32 bits")
                    self.offset(value, 3)
                    symbol = self.string(value + 2)
                    if not symbol:
                        self.fail("empty import symbol")
                imports.append(Import(dll, symbol))
                index += 1
        self.fail("unterminated import descriptor table")

    def read_exports(self) -> dict[Symbol, int | str]:
        rva, size = self.directory(0)
        exports: dict[Symbol, int | str] = {}
        if not size:
            return exports
        if size < 40:
            self.fail("truncated export directory")
        fields = self.unpack("<IIHHIIIIIII", self.offset(rva, 40))
        ordinal_base, functions, names, function_rva, name_rva, ordinal_rva = fields[5:]
        if functions > len(self.data) // 4 or names > len(self.data) // 4:
            self.fail("invalid export table counts")
        for index in range(functions):
            function = self.unpack("<I", self.offset(function_rva + index * 4, 4))[0]
            if function == 0:
                continue
            if rva <= function < rva + size:
                forwarder = self.string(function)
                if len(forwarder) + 1 > rva + size - function:
                    self.fail("forwarded export exceeds its directory")
                exports[ordinal_base + index] = forwarder
            else:
                # Exported data may occupy zero-filled sections, so it need
                # not have raw file bytes, but must lie within the PE image.
                if not any(start <= function < start + length
                           for start, length, _, _ in self.sections):
                    self.fail("export RVA is outside the image sections")
                exports[ordinal_base + index] = function
        for index in range(names):
            name = self.unpack("<I", self.offset(name_rva + index * 4, 4))[0]
            ordinal = self.unpack("<H", self.offset(ordinal_rva + index * 2, 2))[0]
            symbol = self.string(name)
            if ordinal >= functions or not symbol or symbol in exports:
                self.fail("invalid named export")
            value = exports.get(ordinal_base + ordinal)
            if value is None:
                self.fail("named export has an empty function slot")
            exports[symbol] = value
        return exports


def verify_runtime(executable: Path, system32: Path) -> list[str]:
    entry = PeSymbols(executable)
    available: dict[str, Path] = {}
    for path in system32.iterdir():
        if path.is_file() and path.suffix.lower() == ".dll":
            name = module_name(path.name)
            if name in available:
                raise RuntimeError(f"Ambiguous case-insensitive DLL name: {name}")
            available[name] = path
    loaded: dict[str, PeSymbols] = {executable.name.lower(): entry}
    pending = [entry]
    errors: set[str] = set()

    def resolve(name: str, symbol: Symbol, chain: tuple[tuple[str, Symbol], ...]) -> None:
        host = api_set_host(name)
        key = (host, symbol)
        if key in chain:
            errors.add(f"Forwarded export cycle: {host}!{symbol}")
            return
        if len(chain) >= 64:
            errors.add(f"Forwarded export depth exceeds 64: {host}!{symbol}")
            return
        if host not in loaded:
            path = available.get(host)
            if path is None:
                errors.add(f"Missing DLL: {host} (required by {name}!{symbol})")
                return
            loaded[host] = PeSymbols(path)
            pending.append(loaded[host])
        target = loaded[host].exports.get(symbol)
        if target is None:
            errors.add(f"Missing export: {host}!{symbol}")
        elif isinstance(target, str):
            module, separator, exported = target.rpartition(".")
            if not separator or not module or not exported:
                raise RuntimeError(f"Malformed forwarded export: {host}!{symbol} -> {target}")
            forwarded: Symbol = exported
            if exported.startswith("#"):
                if not exported[1:].isdigit():
                    raise RuntimeError(f"Malformed forwarded ordinal: {target}")
                forwarded = int(exported[1:])
            resolve(module, forwarded, chain + (key,))

    while pending:
        image = pending.pop()
        for imported in image.imports:
            resolve(imported.module, imported.symbol, ())
    return sorted(errors)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--explorer", type=Path, default=ROOT / "build/explorer.exe")
    parser.add_argument("--system32", type=Path, default=ROOT / "build/system32")
    args = parser.parse_args()
    try:
        errors = verify_runtime(args.explorer, args.system32)
        if errors:
            print("Explorer runtime import closure FAILED:", file=sys.stderr)
            for error in errors:
                print(f"  {error}", file=sys.stderr)
            print("No host DLL fallback or synthetic import trampoline is permitted.", file=sys.stderr)
            return 1
    except (OSError, RuntimeError) as error:
        print(f"Explorer runtime check failed: {error}", file=sys.stderr)
        return 1
    print("Explorer PE import closure passed; this alone does not verify process startup.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
