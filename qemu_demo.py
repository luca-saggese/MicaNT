#!/usr/bin/env python3
"""Launch the MicaNT desktop interactively in QEMU at 1024x768.

The canonical QEMU runner configures the guest's fixed 1024x768 VirtIO scanout,
builds the modular UEFI boot artifacts, and opens the detected host display.
Any additional arguments are forwarded to qemu.py (for example
--cmake-build-dir or --diagnostics-dir).
"""

import subprocess
import sys
from pathlib import Path

from qemu import choose_display, find_qemu


ROOT = Path(__file__).resolve().parent


def main() -> int:
    forwarded = sys.argv[1:]
    has_build_dir = any(
        argument == "--cmake-build-dir" or argument.startswith("--cmake-build-dir=")
        for argument in forwarded
    )
    if not has_build_dir:
        # Keep the interactive demo's objects out of the shared qemu.py build
        # tree, which may contain artifacts that cannot be replaced in place.
        forwarded.extend([
            "--cmake-build-dir",
            str(ROOT / "build" / "qemu-demo-cmake"),
        ])
    disable_debug_hud = "--no-compositor-debug-hud" in forwarded
    if disable_debug_hud:
        forwarded = [argument for argument in forwarded
                     if argument not in ("--no-compositor-debug-hud",
                                         "--compositor-debug-hud")]
    elif "--compositor-debug-hud" not in forwarded:
        # The interactive demo is the diagnostic desktop entry point. Keep
        # qemu.py's unattended screenshot tests HUD-free unless requested.
        forwarded.append("--compositor-debug-hud")
    if "--headless" in forwarded:
        print("qemu_demo.py always opens a visible interactive QEMU window.", file=sys.stderr)
        return 2
    for index, argument in enumerate(forwarded):
        if argument == "--display" and index + 1 < len(forwarded):
            if forwarded[index + 1] == "none":
                print("qemu_demo.py requires a visible QEMU display backend.", file=sys.stderr)
                return 2
        elif argument.startswith("--display=") and argument.partition("=")[2] == "none":
            print("qemu_demo.py requires a visible QEMU display backend.", file=sys.stderr)
            return 2

    has_display_override = any(
        argument == "--display" or argument.startswith("--display=")
        for argument in forwarded
    )
    if not has_display_override:
        backend = choose_display(find_qemu(), None)
        if backend in ("cocoa", "gtk"):
            forwarded.extend(["--display", f"{backend},zoom-to-fit=on"])

    command = [sys.executable, str(ROOT / "qemu.py"), *forwarded]
    return subprocess.run(command, cwd=ROOT, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
