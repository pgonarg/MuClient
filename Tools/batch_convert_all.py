#!/usr/bin/env python3
"""Batch-convert the MU asset tree to Blender-native formats.

Walks the asset root and converts, in parallel:
  - OZJ/OZT  -> PNG  (textures, written next to source)
  - BMD      -> GLB  (models; Bux-encrypted .bmd data tables are skipped)

Writes a conversion log with per-file errors and a final summary.
Terrain (.map/.att) conversion is handled separately by convert_terrain.py.

Usage:
    python tools/batch_convert_all.py [DATA_ROOT]
    python tools/batch_convert_all.py Data --only textures
    python tools/batch_convert_all.py Data --workers 8 --no-orient
    python tools/batch_convert_all.py Data --dry-run
"""

from __future__ import annotations

import argparse
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mu_formats.bmd import NotAModelError, parse_bmd  # noqa: E402
from mu_formats.glb import GlbBuilder  # noqa: E402
from mu_formats.textures import load_texture, texture_png_name  # noqa: E402
import convert_bmd_to_gltf as bmd_conv  # noqa: E402

TEXTURE_EXTS = (".ozj", ".ozt")

# Worker results: (kind, status, path, message)
#   status in {"ok", "skip", "error"}


def _convert_texture(path_str: str) -> tuple:
    path = Path(path_str)
    try:
        img = load_texture(path)
        img.save(path.with_name(texture_png_name(path)), format="PNG")
        return ("texture", "ok", path_str, "")
    except Exception as e:  # noqa: BLE001
        return ("texture", "error", path_str, f"{type(e).__name__}: {e}")


def _convert_model(path_str: str, orient: bool) -> tuple:
    path = Path(path_str)
    try:
        model = parse_bmd(path)
        glb = bmd_conv.build_glb(model, orient=orient, tex_dir=path.parent)
        path.with_suffix(".glb").write_bytes(glb)
        return ("model", "ok", path_str, "")
    except NotAModelError:
        return ("model", "skip", path_str, "data table (Bux)")
    except Exception as e:  # noqa: BLE001
        return ("model", "error", path_str, f"{type(e).__name__}: {e}")


def _gather(root: Path, only: str):
    textures, models = [], []
    if only in ("all", "textures"):
        textures = [str(p) for p in root.rglob("*")
                    if p.suffix.lower() in TEXTURE_EXTS]
    if only in ("all", "models"):
        models = [str(p) for p in root.rglob("*") if p.suffix.lower() == ".bmd"]
    return textures, models


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Batch convert MU assets.")
    ap.add_argument("root", nargs="?", default="Data", help="asset root (default: Data)")
    ap.add_argument("--only", choices=("all", "textures", "models"), default="all")
    ap.add_argument("--workers", type=int, default=0, help="0 = os.cpu_count()")
    ap.add_argument("--no-orient", action="store_true",
                    help="omit the Blender-upright root node (raw MU orientation)")
    ap.add_argument("--log", default=None, help="log path (default: <root>/conversion.log)")
    ap.add_argument("--dry-run", action="store_true", help="list counts, convert nothing")
    args = ap.parse_args(argv)

    root = Path(args.root)
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2

    textures, models = _gather(root, args.only)
    print(f"discovered: {len(textures)} textures, {len(models)} .bmd files")
    if args.dry_run:
        return 0

    log_path = Path(args.log) if args.log else root / "conversion.log"
    workers = args.workers or None
    tallies = {"texture": {"ok": 0, "skip": 0, "error": 0},
               "model": {"ok": 0, "skip": 0, "error": 0}}
    errors = []
    start = time.time()

    with log_path.open("w", encoding="utf-8") as log, \
            ProcessPoolExecutor(max_workers=workers) as ex:
        futures = []
        for t in textures:
            futures.append(ex.submit(_convert_texture, t))
        for m in models:
            futures.append(ex.submit(_convert_model, m, not args.no_orient))

        total = len(futures)
        done = 0
        for fut in as_completed(futures):
            kind, status, path, msg = fut.result()
            tallies[kind][status] += 1
            if status == "error":
                errors.append((path, msg))
                log.write(f"ERROR\t{path}\t{msg}\n")
            done += 1
            if done % 500 == 0 or done == total:
                pct = 100 * done / total if total else 100
                print(f"\r  {done}/{total} ({pct:4.1f}%)  "
                      f"tex ok={tallies['texture']['ok']} err={tallies['texture']['error']}  "
                      f"mdl ok={tallies['model']['ok']} skip={tallies['model']['skip']} "
                      f"err={tallies['model']['error']}", end="", flush=True)
        print()

        elapsed = time.time() - start
        summary = (
            f"\n=== Conversion summary ({elapsed:.1f}s) ===\n"
            f"Textures: ok={tallies['texture']['ok']} error={tallies['texture']['error']}\n"
            f"Models:   ok={tallies['model']['ok']} skipped={tallies['model']['skip']} "
            f"error={tallies['model']['error']}\n"
        )
        log.write(summary)
        print(summary)

    if errors:
        print(f"{len(errors)} error(s) logged to {log_path}")
        for p, m in errors[:10]:
            print(f"  {p}: {m}")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
