#!/usr/bin/env python3
"""Convert image files (PNG/JPG/BMP/...) to MU OZJ textures.

Drag-and-drop friendly: every path passed as an argument is converted to a
sibling ``.OZJ`` (same folder, same stem). Intended to be driven by
``Png-to-OZJ.bat`` but also usable directly:

    python tools/png_to_ozj.py mytexture.png [more.png ...]
    python tools/png_to_ozj.py -q 90 mytexture.png

OZJ is opaque (RGB). For textures that need alpha, use OZT instead (the engine
loads ``.tga`` references from ``.OZT``); this tool intentionally targets OZJ.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image  # noqa: E402
from mu_formats.textures import encode_ozj  # noqa: E402


def convert(src: Path, quality: int) -> Path:
    img = Image.open(src)
    img.load()
    dst = src.with_suffix(".OZJ")
    dst.write_bytes(encode_ozj(img, quality=quality))
    return dst


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Convert images to MU OZJ.")
    ap.add_argument("inputs", nargs="*", help="image file(s) to convert")
    ap.add_argument("-q", "--quality", type=int, default=95,
                    help="JPEG quality 1-100 (default 95)")
    args = ap.parse_args(argv)

    if not args.inputs:
        print("Drag one or more image files onto Png-to-OZJ.bat,")
        print("or run: python tools/png_to_ozj.py <file.png>")
        return 1

    rc = 0
    for raw in args.inputs:
        src = Path(raw)
        try:
            dst = convert(src, args.quality)
            w, h = Image.open(src).size
            note = "" if (w & (w - 1) == 0 and h & (h - 1) == 0) else \
                "  [warning: non power-of-two - may not render correctly]"
            print(f"OK  {src.name}  ->  {dst.name}  ({w}x{h}){note}")
        except Exception as e:  # noqa: BLE001
            rc = 1
            print(f"ERROR  {src}: {type(e).__name__}: {e}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
