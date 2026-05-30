# tools/

Utility scripts for this repository.

## Asset pipeline (Python)

Converts MU's proprietary assets to/from Blender-native formats. Requires
Python 3.12 and Pillow (`python -m pip install Pillow`).

- `convert_textures.py` — OZJ/OZT → PNG
- `convert_bmd_to_gltf.py` — BMD → glTF 2.0 (GLB)
- `convert_gltf_to_bmd.py` — GLB → BMD (reverse / "convert back")
- `batch_convert_all.py` — convert the whole `Data/` tree in parallel
- `png_to_ozj.py` — image → OZJ
- **`Png-to-OZJ.bat`** — drag a PNG (or several) onto this to create sibling `.OZJ`
- **`Glb-to-BMD.bat`** — drag a GLB (or several) onto this to create sibling `.BMD`
  (uses the original `.bmd` as a template for rig/animations; backs it up to
  `.bmd.orig` before overwriting)
- `mu_formats/` — shared library (cipher, BMD parse+write, GLB read+write, texture codecs)

### Viewer

- **`gltf-explorer.html`** — open in a browser, click **Open folder…** and pick
  `Data/` (or a subfolder). Lists every `.glb` in a searchable sidebar and renders
  the selected model in 3D with its textures (Three.js). Drag to orbit, scroll to
  zoom, ↑/↓ to step through. Needs internet (Three.js via CDN).

- **`map-editor.html`** — open in a browser, **Open Data folder…**, pick a world,
  **Load world**. Renders the terrain (heightmap + tile textures) and every placed
  object as its GLB at the real position/angle/scale. Select objects (click or list),
  move/rotate/scale with the gizmo (W/E/R), add/duplicate/delete, then **Export .obj**
  to download a re-encrypted `EncTerrainN.obj` to drop back into the world folder.
  Needs internet (Three.js via CDN). See `docs/asset-pipeline.md` for details/caveats.

Full reference (formats, status, gotchas, how-to):
[`../docs/asset-pipeline.md`](../docs/asset-pipeline.md).

## Other tools

- `ResxGen/`, `DialogImporter/` — C# tools for the translation/resource system
  (see [`../docs/translation-system.md`](../docs/translation-system.md)).
