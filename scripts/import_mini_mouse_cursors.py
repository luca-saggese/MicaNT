#!/usr/bin/env python3
"""Import the pinned Mini Mouse Cursor assets and retain upstream attribution."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
from pathlib import Path


PINNED_COMMIT = "b01df120a3c370a870427d13d954359c0dd709cf"
ASSETS = (
    "mini_mouse_arrow.cur", "mini_mouse_beam.cur", "mini_mouse_busy.ani",
    "mini_mouse_cross.cur", "mini_mouse_ew.cur", "mini_mouse_helpsel.cur",
    "mini_mouse_link.cur", "mini_mouse_move.cur", "mini_mouse_nesw.cur",
    "mini_mouse_ns.cur", "mini_mouse_nwse.cur", "mini_mouse_pen.cur",
    "mini_mouse_unavail.cur", "mini_mouse_up.cur", "mini_mouse_working.ani",
)
CURSOR_IDS = (
    ("IDC_ARROW", "mini_mouse_arrow.cur"),
    ("IDC_IBEAM", "mini_mouse_beam.cur"),
    ("IDC_WAIT", "mini_mouse_busy.ani"),
    ("IDC_CROSS", "mini_mouse_cross.cur"),
    ("IDC_SIZEWE", "mini_mouse_ew.cur"),
    ("IDC_HELP", "mini_mouse_helpsel.cur"),
    ("IDC_HAND", "mini_mouse_link.cur"),
    ("IDC_SIZEALL", "mini_mouse_move.cur"),
    ("IDC_SIZENESW", "mini_mouse_nesw.cur"),
    ("IDC_SIZENS", "mini_mouse_ns.cur"),
    ("IDC_SIZENWSE", "mini_mouse_nwse.cur"),
    ("IDC_APPSTARTING", "mini_mouse_working.ani"),
    ("IDC_NO", "mini_mouse_unavail.cur"),
    ("IDC_UPARROW", "mini_mouse_up.cur"),
    ("IDC_PEN", "mini_mouse_pen.cur (custom extension; no standard IDC mapping)"),
)


def run(source: Path, output: Path) -> None:
    commit = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != PINNED_COMMIT:
        raise SystemExit(f"Expected small-cursor {PINNED_COMMIT}, found {commit}")
    for name in (*ASSETS, "LICENSE", "README.md", "INSTALL.md", "mini-mouse-cursor.inf"):
        if not (source / name).is_file():
            raise SystemExit(f"Pinned source is missing {name}")

    output.mkdir(parents=True, exist_ok=True)
    for name in (*ASSETS, "LICENSE"):
        shutil.copyfile(source / name, output / name)
    hashes = [
        f"- `{name}` — SHA-256 `{hashlib.sha256((output / name).read_bytes()).hexdigest()}`"
        for name in (*ASSETS, "LICENSE")
    ]
    mapping = [f"- `{identifier}` → `{filename}`" for identifier, filename in CURSOR_IDS]
    (output / "ATTRIBUTIONS.md").write_text(
        "# Mini Mouse Cursor assets\n\n"
        "- Source: https://github.com/surjolive/small-cursor\n"
        f"- Upstream commit: `{PINNED_COMMIT}` (`master`).\n"
        "- Copyright: Copyright (c) 2026 Tech World.\n"
        "- License: MIT; the original upstream `LICENSE` is retained in this directory.\n"
        "- Scope: only the 13 CUR and 2 ANI assets were imported; README, INF and installer "
        "content are not installed or included in the system image. The upstream README and "
        "INSTALL.md were reviewed for this import.\n"
        "- Asset bytes are copied unchanged. CUR hotspots remain in their original files.\n\n"
        "## Default IDC associations\n\n" + "\n".join(mapping) +
        "\n\n## Imported file hashes\n\n" + "\n".join(hashes) + "\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="checkout of pinned upstream repository")
    parser.add_argument("--output", type=Path, default=Path("System/Cursors/MiniMouse"))
    args = parser.parse_args()
    run(args.source, args.output)


if __name__ == "__main__":
    main()
