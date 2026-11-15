#!/usr/bin/env python3
"""Import a small, reproducible WhiteSur icon subset into System/Icons.

The source checkout must be the pinned upstream revision below. Only generic
folder, device, network, trash and file type icons are selected; branded app
and Apple artwork is intentionally excluded.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


PINNED_COMMIT = "73d8040da51a9ed74e47c7366e7e9ff437601a5c"
SIZES = (16, 24, 32, 48, 64, 96, 128, 256)
ICONS = {
    "places": (
        "folder.svg", "folder-open.svg", "folder-documents.svg",
        "folder-download.svg", "folder-images.svg", "folder-music.svg",
        "folder-videos.svg", "user-desktop.svg", "user-trash.svg",
        "user-trash-full.svg",
    ),
    "devices": ("computer.svg", "drive-harddisk.svg", "drive-removable-media.svg"),
    "apps": ("network-workgroup.svg",),
    "mimes": (
        "text-x-generic.svg", "application-pdf.svg", "package-x-generic.svg",
        "image-x-generic.svg", "audio-x-generic.svg", "video-x-generic.svg",
        "application-x-executable.svg", "application-x-sharedlib.svg",
        "application-x-object.svg", "application-json.svg", "text-xml.svg",
    ),
}


def run(source: Path, output: Path, renderer: str) -> None:
    commit = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != PINNED_COMMIT:
        raise SystemExit(f"Expected WhiteSur {PINNED_COMMIT}, found {commit}")
    for tool_file in ("COPYING", "AUTHORS", "src/index.theme"):
        if not (source / tool_file).is_file():
            raise SystemExit(f"Pinned source is missing {tool_file}")

    output.mkdir(parents=True, exist_ok=True)
    for name in ("COPYING", "AUTHORS"):
        shutil.copyfile(source / name, output / name)
    shutil.copyfile(source / "src/index.theme", output / "UPSTREAM-index.theme")

    imported: list[str] = []
    for category, names in ICONS.items():
        for name in names:
            src = source / "src" / category / "scalable" / name
            if not src.is_file():
                raise SystemExit(f"Pinned source is missing selected icon: {src}")
            svg_dir = output / category / "scalable"
            svg_dir.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, svg_dir / name)
            stem = Path(name).stem
            for size in SIZES:
                png_dir = output / category / str(size)
                png_dir.mkdir(parents=True, exist_ok=True)
                subprocess.run(
                    [renderer, "-w", str(size), "-h", str(size), "-o",
                     str(png_dir / f"{stem}.png"), str(src)],
                    check=True,
                )
            imported.append(f"- `{category}/{name}`")

    directories = ",".join(f"{category}/{size}" for category in ICONS for size in SIZES)
    (output / "index.theme").write_text(
        "[Icon Theme]\nName=WhiteSur (MicaNT subset)\nComment=Curated generic icons from WhiteSur\n"
        "Inherits=hicolor\nDirectories=" + directories + "\n\n" +
        "".join(f"[{category}/{size}]\nSize={size}\nType=Fixed\n" for category in ICONS for size in SIZES),
        encoding="utf-8",
    )
    (output / "ATTRIBUTIONS.md").write_text(
        "# WhiteSur icon assets in MicaNT\n\n"
        f"- Source: https://github.com/vinceliuice/WhiteSur-icon-theme\n"
        f"- Pinned commit: `{PINNED_COMMIT}` (release 2026-09-10)\n"
        "- License: GPL-3.0 as stated by the upstream repository; see `COPYING`.\n"
        "- Author notice: retained verbatim in `AUTHORS`.\n"
        "- Changes: selected generic SVGs copied unchanged from `src/<category>/scalable`; "
        "PNG variants generated at 16, 24, 32, 48, 64, 96, 128 and 256 px with rsvg-convert.\n"
        "- Excluded: Apple logos and branded third-party application icons.\n"
        "- Separation: this directory is independently licensed artwork; it does not change "
        "the MIT license of MicaNT source code.\n\n"
        "## Imported upstream SVGs\n\n" + "\n".join(imported) + "\n",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="checkout of the pinned upstream repository")
    parser.add_argument("--output", type=Path, default=Path("System/Icons/WhiteSur"))
    parser.add_argument("--renderer", default="rsvg-convert")
    args = parser.parse_args()
    run(args.source.resolve(), args.output.resolve(), args.renderer)


if __name__ == "__main__":
    main()
