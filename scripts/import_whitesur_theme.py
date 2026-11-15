#!/usr/bin/env python3
"""Import only generated WhiteSur GTK 3 CSS and referenced assets.

This is a development-time packaging helper. It does not run upstream shell
scripts or install anything on the host. The production system consumes the
resulting ordinary files and has no GTK/GResource dependency.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import pathlib
import re
import subprocess
import tarfile
import tempfile
import zipfile


ARCHIVE_SHA256 = "e06899f596706ad9a4aace9bec5f739d63e7f54530a488670026b027d915081c"
UPSTREAM_COMMIT = "99247ecd219bdc1c7409baff062b4aafcf1102a7"
ARCHIVE_ROOT = "WhiteSur-gtk-theme-2026-09-10"
RELEASE_DIR = f"{ARCHIVE_ROOT}/release"
RESOURCE_PREFIX = "/org/gnome/theme/"
URL_PATTERN = re.compile(r"url\(\s*(?:\"([^\"]+)\"|'([^']+)'|([^\s)]+))\s*\)", re.I)
ACCENT_PATTERN = re.compile(
    r"\$theme_color_(default|blue|purple|pink|red|orange|yellow|green|grey):\s*(#[0-9A-Fa-f]{6})"
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_relative(value: str) -> pathlib.PurePosixPath:
    path = pathlib.PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"unsafe package path: {value!r}")
    return path


def extract_gresource(data: bytes, gresource_tool: str) -> tuple[bytes, dict[str, bytes], list[str]]:
    with tempfile.NamedTemporaryFile(prefix="micant-whitesur-", suffix=".gresource") as resource_file:
        resource_file.write(data)
        resource_file.flush()
        listed = subprocess.run(
            [gresource_tool, "list", resource_file.name], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        ).stdout.splitlines()
        names: set[str] = set()
        for name in listed:
            if not name.startswith(RESOURCE_PREFIX):
                raise ValueError(f"unexpected GResource root: {name}")
            safe_relative(name[len(RESOURCE_PREFIX):])
            names.add(name)
        css_name = RESOURCE_PREFIX + "gtk.css"
        if css_name not in names:
            raise ValueError("GTK resource has no generated gtk.css")
        css = subprocess.run(
            [gresource_tool, "extract", resource_file.name, css_name], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ).stdout
        text = css.decode("utf-8", "strict")
        paths: set[str] = set()
        missing: set[str] = set()
        for match in URL_PATTERN.finditer(text):
            value = next((part for part in match.groups() if part is not None), "")
            if value.startswith(("resource:", "file:", "https:", "http:")) or "://" in value:
                raise ValueError(f"non-package URL in GTK CSS: {value}")
            path = safe_relative(value)
            if path.parts[0] not in ("assets", "windows-assets"):
                raise ValueError(f"asset URL outside allowed roots: {value}")
            resource_name = RESOURCE_PREFIX + path.as_posix()
            if resource_name not in names:
                missing.add(path.as_posix())
            else:
                paths.add(path.as_posix())
        assets: dict[str, bytes] = {}
        for path in sorted(paths):
            output = subprocess.run(
                [gresource_tool, "extract", resource_file.name, RESOURCE_PREFIX + path],
                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            ).stdout
            assets[path] = output
        return css, assets, sorted(missing)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("System/Themes/WhiteSur"))
    parser.add_argument("--gresource", default="gresource")
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()

    archive_bytes = args.archive.read_bytes()
    actual_sha = digest(archive_bytes)
    if actual_sha != ARCHIVE_SHA256:
        raise SystemExit(f"archive SHA-256 mismatch: expected {ARCHIVE_SHA256}, got {actual_sha}")
    args.output.mkdir(parents=True, exist_ok=True)
    repository_root = pathlib.Path(__file__).resolve().parents[1]

    manifest: dict[str, object] = {
        "schemaVersion": 1,
        "id": "WhiteSur",
        "name": "WhiteSur GTK Theme",
        "source": {
            "repository": "https://github.com/vinceliuice/WhiteSur-gtk-theme",
            "commit": UPSTREAM_COMMIT,
            "archive": args.archive.name,
            "archiveSha256": actual_sha,
        },
        "license": "LICENSES/COPYING",
        "defaultVariant": "Light",
        "variants": {},
    }

    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as source_zip:
        license_data = source_zip.read(f"{ARCHIVE_ROOT}/COPYING")
        authors_data = source_zip.read(f"{ARCHIVE_ROOT}/.github/AUTHORS")
        palette_text = source_zip.read(f"{ARCHIVE_ROOT}/src/sass/_colors-palette.scss").decode("utf-8", "strict")
        accent_palette = {name: color.lower() for name, color in ACCENT_PATTERN.findall(palette_text)}
        if set(accent_palette) != {"default", "blue", "purple", "pink", "red", "orange", "yellow", "green", "grey"}:
            raise ValueError("upstream accent palette is incomplete or changed")
        manifest["accentPalette"] = accent_palette
        (args.output / "LICENSES").mkdir(exist_ok=True)
        (args.output / "LICENSES" / "COPYING").write_bytes(license_data)
        (args.output / "LICENSES" / "AUTHORS").write_bytes(authors_data)

        with tempfile.TemporaryDirectory(prefix="micant-theme-compiler-") as temporary:
            compiler_binary = pathlib.Path(temporary) / "compile_profile"
            subprocess.run([
                args.compiler, "-std=c++23", "-O2", "-I", str(repository_root / "include"),
                str(repository_root / "tools" / "theme" / "compile_profile.cpp"),
                "-o", str(compiler_binary),
            ], check=True)

            variants = (
                ("Light", "Light", False),
                ("Dark", "Dark", False),
                ("Light-solid", "Solid/Light", True),
                ("Dark-solid", "Solid/Dark", True),
            )
            for source_variant, package_path, solid in variants:
                variant_id = package_path.replace("/", "")
                tar_name = f"WhiteSur-{source_variant}.tar.xz"
                packed = source_zip.read(f"{RELEASE_DIR}/{tar_name}")
                with tarfile.open(fileobj=io.BytesIO(packed), mode="r:xz") as release:
                    resource_member = f"WhiteSur-{source_variant}/gtk-3.0/gtk.gresource"
                    member = release.getmember(resource_member)
                    if not member.isfile() or member.size > 8 * 1024 * 1024:
                        raise ValueError(f"invalid or oversized GTK resource for {source_variant}")
                    resource_data = release.extractfile(member).read()
                css, assets, missing_assets = extract_gresource(resource_data, args.gresource)
                variant_dir = args.output / package_path
                variant_dir.mkdir(parents=True, exist_ok=True)
                css_path = variant_dir / "gtk.css"
                style_path = variant_dir / "native.style"
                css_path.write_bytes(css)
                subprocess.run([str(compiler_binary), str(css_path), str(style_path)], check=True)
                asset_hashes: dict[str, str] = {}
                for name, data in assets.items():
                    relative = safe_relative(name)
                    destination = variant_dir.joinpath(*relative.parts)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(data)
                    asset_hashes[name] = digest(data)
                manifest["variants"][variant_id] = {
                    "entryCss": f"{package_path}/gtk.css",
                    "nativeStyle": f"{package_path}/native.style",
                    "nativeStyleSha256": digest(style_path.read_bytes()),
                    "solid": solid,
                    "cssSha256": digest(css),
                    "assetRoot": package_path,
                    "assetCount": len(assets),
                    "assets": asset_hashes,
                    "missingAssetReferences": missing_assets,
                    "sourceGresourceSha256": digest(resource_data),
                    "sourceReleaseTarSha256": digest(packed),
                }

            purple_dir = args.output / "LightPurple"
            purple_dir.mkdir(parents=True, exist_ok=True)
            purple_style = purple_dir / "native.style"
            subprocess.run([
                str(compiler_binary), str(args.output / "Light" / "gtk.css"),
                str(purple_style), "#9a57a3",
            ], check=True)
            manifest["accentVariants"] = {
                "LightPurple": {
                    "nativeStyle": "LightPurple/native.style",
                    "nativeStyleSha256": digest(purple_style.read_bytes()),
                    "sourceVariant": "Light",
                    "accent": accent_palette["purple"],
                }
            }

    (args.output / "theme.manifest").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"Imported WhiteSur Light/Dark and Solid variants into {args.output}")
    print(f"Archive SHA-256: {actual_sha}; upstream commit: {UPSTREAM_COMMIT}")
    for variant, details in manifest["variants"].items():
        if details["missingAssetReferences"]:
            print(f"{variant} missing asset references use Base fallback: "
                  f"{', '.join(details['missingAssetReferences'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
