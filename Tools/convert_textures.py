#!/usr/bin/env python3
"""Convert MU OZJ/OZT textures to PNG.

  OZJ -> PNG (RGB)   : 24-byte header + JPEG
  OZT -> PNG (RGBA)  : 4-byte header + TGA (carries alpha)

Output PNGs are written next to the source (same stem) so BMD->glTF material
URIs resolve as siblings.

Usage:
    python tools/convert_textures.py <file.ozj|file.ozt> [-o out.png]
    python tools/convert_textures.py --batch Data
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mu_formats.textures import load_texture, texture_png_name  # noqa: E402

TEXTURE_EXTS = (".ozj", ".ozt")


def convert_file(src: Path, dst: Path | None = None) -> None:
    img = load_texture(src)
    if dst is None:
        dst = src.with_name(texture_png_name(src))
    dst.parent.mkdir(parents=True, exist_ok=True)
    img.save(dst, format="PNG")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Convert OZJ/OZT textures to PNG.")
    ap.add_argument("input", help="OZJ/OZT file, or root dir with --batch")
    ap.add_argument("-o", "--output", help="output PNG (single-file mode)")
    ap.add_argument("--batch", action="store_true",
                    help="treat input as a directory and convert all OZJ/OZT")
    args = ap.parse_args(argv)

    root = Path(args.input)
    if not args.batch:
        out = Path(args.output) if args.output else None
        convert_file(root, out)
        print(f"wrote {out or root.with_name(texture_png_name(root))}")
        return 0

    files = [p for p in root.rglob("*") if p.suffix.lower() in TEXTURE_EXTS]
    ok = errors = 0
    for f in files:
        try:
            convert_file(f)
            ok += 1
        except Exception as e:  # noqa: BLE001
            errors += 1
            print(f"  ERROR {f}: {type(e).__name__}: {e}")
    print(f"OZJ/OZT->PNG: converted={ok} errors={errors}")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
