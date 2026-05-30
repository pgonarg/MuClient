"""OZJ / OZT texture containers.

Empirically verified against this client's assets:
  - OZJ = 24-byte header  + standard JPEG  (JPEG SOI begins at offset 24)
  - OZT =  4-byte header  + standard TGA   (TGA header begins at offset 4)

Both wrap an otherwise-standard image; we strip the header and hand the
remainder to Pillow. We locate the real payload defensively (scan for the
JPEG SOI / a plausible TGA header) so odd header sizes still decode.
"""

from __future__ import annotations

import io
from pathlib import Path

from PIL import Image

OZJ_HEADER = 24
OZT_HEADER = 4

# Canonical 24-byte OZJ header. The engine skips exactly 24 bytes
# (ZzzTexture.cpp: fseek(fp, 24, SEEK_SET)) and decodes the rest as JPEG, so the
# content is cosmetic — we mirror a real OZJ header for tool compatibility.
OZJ_HEADER_BYTES = bytes((
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46,
    0x49, 0x46, 0x00, 0x01, 0x02, 0x01, 0x00, 0x48,
    0x00, 0x48, 0x00, 0x00, 0xFF, 0xED, 0x06, 0xEA,
))

# Pillow needs TGA to be opened from a path or a stream it recognises; feeding
# raw bytes via BytesIO works for both JPEG and TGA.


def _jpeg_payload(data: bytes) -> bytes:
    """Return the JPEG stream inside an OZJ blob."""
    # Fast path: documented 24-byte header.
    if data[OZJ_HEADER:OZJ_HEADER + 2] == b"\xff\xd8":
        return data[OZJ_HEADER:]
    # Fallback: find the last SOI that is followed by a valid APP/marker. The
    # OZJ header itself starts with ff d8 ff e0, so we scan from offset 1.
    idx = data.find(b"\xff\xd8\xff", 1)
    if idx != -1:
        return data[idx:]
    # Last resort: assume no header.
    return data


def _tga_payload(data: bytes) -> bytes:
    """Return the TGA stream inside an OZT blob."""
    # Documented 4-byte header; validate the TGA image-type byte (offset +2).
    cand = data[OZT_HEADER:]
    if len(cand) > 18 and cand[1] in (0, 1) and cand[2] in (0, 1, 2, 3, 9, 10, 11):
        return cand
    return data[OZT_HEADER:]


def load_ozj(path: Path) -> Image.Image:
    """Decode an OZJ file to a Pillow image (RGB)."""
    data = Path(path).read_bytes()
    img = Image.open(io.BytesIO(_jpeg_payload(data)))
    img.load()
    return img.convert("RGB")


def load_ozt(path: Path) -> Image.Image:
    """Decode an OZT file to a Pillow image (RGBA — OZT carries alpha)."""
    data = Path(path).read_bytes()
    img = Image.open(io.BytesIO(_tga_payload(data)))
    img.load()
    # OZT is typically 32-bit BGRA TGA; Pillow handles the ordering. Keep alpha.
    return img.convert("RGBA")


def texture_png_name(src) -> str:
    """Deterministic PNG filename for a texture, collision-free.

    MU co-locates a model's textures with the model, and ~180 folders contain
    both ``foo.ozj`` (opaque) and ``foo.ozt`` (alpha). Collapsing both to
    ``foo.png`` would lose one variant, so when the sibling of the *other*
    proprietary type exists we disambiguate as ``foo_ozj.png`` / ``foo_ozt.png``.
    Otherwise the clean ``foo.png`` is used. Both the texture converter and the
    BMD material resolver call this so URIs always match on disk.
    """
    src = Path(src)
    stem, ext = src.stem, src.suffix.lower()
    if ext in (".ozj", ".ozt"):
        other = ".ozt" if ext == ".ozj" else ".ozj"
        parent = src.parent
        if parent.is_dir():
            names = {p.name.lower() for p in parent.iterdir()}
            if f"{stem.lower()}{other}" in names:
                return f"{stem}_{ext[1:]}.png"
    return f"{stem}.png"


# BMD models reference textures by a "game" extension; the engine maps these
# to the proprietary container that actually ships on disk.
_GAME_EXT_TO_SRC = {".jpg": ".ozj", ".jpeg": ".ozj", ".tga": ".ozt"}


def resolve_model_texture_png(tex_dir, stored_name: str):
    """PNG filename a BMD material should reference, matching converter output.

    ``stored_name`` is the name embedded in the BMD (e.g. ``skin.tga`` or
    ``armor.jpg``). We map the game extension to its proprietary source
    (.tga->.ozt, .jpg->.ozj), find the real file co-located with the model,
    and return the same PNG name :func:`texture_png_name` would assign it
    (including the ozj/ozt disambiguator when both variants exist).
    """
    name = (stored_name or "").strip().strip("\x00")
    if not name:
        return None
    stem = Path(name).stem
    ext = Path(name).suffix.lower()
    src_ext = _GAME_EXT_TO_SRC.get(ext)

    listing = {}
    tdir = Path(tex_dir) if tex_dir else None
    if tdir and tdir.is_dir():
        listing = {p.name.lower(): p for p in tdir.iterdir()}

    candidates = ([src_ext] if src_ext else []) + [".ozj", ".ozt"]
    for ce in candidates:
        hit = listing.get(f"{stem.lower()}{ce}")
        if hit is not None:
            return texture_png_name(hit)

    # Not found on disk: synthesize using the mapped source ext so the
    # collision rule still applies against any sibling that does exist.
    probe_ext = src_ext or ext or ".ozj"
    return texture_png_name((tdir or Path(".")) / f"{stem}{probe_ext}")


def encode_ozj(img: Image.Image, quality: int = 95) -> bytes:
    """Encode a Pillow image to OZJ bytes (24-byte header + RGB JPEG).

    The engine decodes OZJ with libjpeg-turbo as RGB 4:4:4, so we drop any alpha
    and disable chroma subsampling (subsampling=0 == 4:4:4) to match and to keep
    edited detail crisp.
    """
    rgb = img.convert("RGB")
    buf = io.BytesIO()
    rgb.save(buf, format="JPEG", quality=quality, subsampling=0, optimize=True)
    return OZJ_HEADER_BYTES + buf.getvalue()


def load_texture(path: Path) -> Image.Image:
    """Decode an OZJ/OZT/standard image by extension."""
    p = Path(path)
    ext = p.suffix.lower()
    if ext == ".ozj":
        return load_ozj(p)
    if ext == ".ozt":
        return load_ozt(p)
    img = Image.open(p)
    img.load()
    return img
