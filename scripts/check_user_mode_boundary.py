#!/usr/bin/env python3
"""Reject native user-mode targets that transitively include private kernel headers."""

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Variant:
    target: str
    source: str
    definitions: tuple[str, ...] = ()
    includes: tuple[str, ...] = ()


def variants() -> tuple[Variant, ...]:
    result = [
        Variant("ntdll_boot", "tools/dlls/ntdll.cpp"),
        Variant("shell32_boot", "tools/dlls/shell32_icon.cpp",
                ("MICANT_NATIVE_USERMODE",)),
        Variant("comctl32_boot", "tools/dlls/comctl32_icon.cpp",
                ("MICANT_NATIVE_USERMODE",)),
        Variant("dwrite_boot", "tools/dlls/dwrite.cpp",
                ("MICANT_NATIVE_USERMODE",)),
        Variant("ws2_32_boot", "tools/dlls/ws2_32.cpp", ("MICANT_NATIVE_USERMODE",)),
        Variant("dnsapi_boot", "tools/dlls/dnsapi.cpp", ("MICANT_NATIVE_USERMODE",)),
        Variant("crypt32_boot", "tools/dlls/crypt32.cpp", ("MICANT_NATIVE_USERMODE",)),
        Variant("winhttp_boot", "tools/dlls/winhttp.cpp", ("MICANT_NATIVE_USERMODE",)),
        Variant("uxtheme_boot", "tools/dlls/uxtheme.cpp",
                ("MICANT_NATIVE_USERMODE", "MICANT_WIN32K_IMPORTS")),
        Variant("luceglass_boot", "tools/dlls/luceglass.cpp",
                ("MICANT_NATIVE_USERMODE", "MICANT_WIN32K_IMPORTS")),
        Variant("svchost_boot", "tools/svchost_app.cpp", ("MICANT_NATIVE_USERMODE",)),
        Variant("svchost_boot", "tools/dlls/dhcpcsvc.cpp", ("MICANT_NATIVE_USERMODE",)),
        Variant("kernel32_boot", "kernel/ps/winnt_kernel32_advapi32_system32_abi.cpp",
                ("MICANT_FREESTANDING_KERNEL32",)),
        Variant("ucrtbase_boot", "kernel/ps/winnt_ucrtbase_runtime_abi.cpp",
                ("MICANT_FREESTANDING_UCRT",)),
        Variant("win32k_boot", "kernel/ps/winnt_win32k_server.cpp",
                ("BUILD_SYSTEM32_STANDALONE_DLL", "MICANT_WIN32K_EXPORTS")),
    ]
    for dll in ("USER32", "GDI32", "IMM32"):
        result.append(Variant(
            f"{dll.lower()}_boot",
            "kernel/ps/winnt_user32_gdi32_imm32_system32_abi.cpp",
            ("BUILD_SYSTEM32_STANDALONE_DLL",
             f"BUILD_SYSTEM32_{dll}_DLL", "MICANT_WIN32K_IMPORTS"),
        ))
    result.extend((
        Variant("micant_win32k", "kernel/ps/winnt_win32k_server.cpp",
                ("BUILD_SYSTEM32_STANDALONE_DLL", "MICANT_WIN32K_EXPORTS")),
        Variant("micant_user32", "kernel/ps/winnt_user32_gdi32_imm32_system32_abi.cpp",
                ("BUILD_SYSTEM32_STANDALONE_DLL", "BUILD_SYSTEM32_USER32_DLL",
                 "MICANT_WIN32K_IMPORTS")),
        Variant("micant_gdi32", "kernel/ps/winnt_user32_gdi32_imm32_system32_abi.cpp",
                ("BUILD_SYSTEM32_STANDALONE_DLL", "BUILD_SYSTEM32_GDI32_DLL",
                 "MICANT_WIN32K_IMPORTS")),
        Variant("micant_imm32", "kernel/ps/winnt_user32_gdi32_imm32_system32_abi.cpp",
                ("BUILD_SYSTEM32_STANDALONE_DLL", "BUILD_SYSTEM32_IMM32_DLL",
                 "MICANT_WIN32K_IMPORTS")),
    ))
    for source in (
        "tools/explorer_shell_app.cpp",
        "tools/explorer_shell/desktop.cpp",
        "tools/explorer_shell/taskbar.cpp",
        "tools/explorer_shell/start_menu.cpp",
        "tools/explorer_shell/notification_area.cpp",
        "tools/explorer_shell/task_switcher.cpp",
    ):
        result.append(Variant("explorer_boot", source, includes=("tools",)))
    result.append(Variant("mac_shell_boot", "tools/mac_shell_app.cpp",
                          includes=("tools",)))
    return tuple(result)


def dependencies(compiler: str, root: Path, variant: Variant) -> tuple[str, ...]:
    source = root / variant.source
    executable = Path(compiler).name.lower()
    msvc = executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}
    if msvc:
        command = [compiler, "/nologo", "/showIncludes", "/EP", "/TP",
                   "/std:c++latest", f"/I{root / 'include'}"]
        command.extend(f"/I{root / include}" for include in variant.includes)
        command.extend(f"/D{definition}" for definition in variant.definitions)
        command.append(str(source))
    else:
        command = [compiler, "-M", "-MT", "user_mode_boundary_check",
                   "-std=c++23", "-I", str(root / "include")]
        for include in variant.includes:
            command.extend(("-I", str(root / include)))
        command.extend(f"-D{definition}" for definition in variant.definitions)
        command.append(str(source))

    completed = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"{variant.target}: dependency scan failed:\n{detail}")

    output = completed.stdout + "\n" + completed.stderr
    found = set(re.findall(r"(?<![A-Za-z0-9_])micant[/\\]+km[/\\]+[^\s\"<>]+", output))
    return tuple(sorted(found))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--target", action="append", dest="targets")
    args = parser.parse_args()
    root = args.root.resolve()
    all_variants = variants()
    if not args.targets:
        selected = all_variants
    elif "explorer_runtime_boot" in args.targets:
        selected = tuple(variant for variant in all_variants
                         if variant.target.endswith("_boot"))
    else:
        target_names = set(args.targets)
        selected = tuple(variant for variant in all_variants
                         if variant.target in target_names)
        unknown = target_names - {variant.target for variant in all_variants}
        if unknown:
            parser.error(f"unknown user-mode target(s): {', '.join(sorted(unknown))}")

    violations = []
    try:
        for variant in selected:
            private_headers = dependencies(args.compiler, root, variant)
            if private_headers:
                violations.append((variant.target, private_headers))
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 2

    if violations:
        print("Native user-mode boundary check failed:", file=sys.stderr)
        for target, headers in violations:
            print(f"  {target} depends on private kernel headers:", file=sys.stderr)
            for header in headers:
                print(f"    {header}", file=sys.stderr)
        return 1

    names = ", ".join(variant.target for variant in selected)
    print(f"Native user-mode boundary check passed: {names}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
