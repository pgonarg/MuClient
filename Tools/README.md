# tools/

Utility scripts for this repository.

## Asset pipeline (Python)

Converts MU's proprietary assets to/from Blender-native formats. Requires
Python 3.12 and Pillow (`python -m pip install Pillow`).

- `convert_textures.py` — OZJ/OZT → PNG
- `convert_bmd_to_gltf.py` — BMD → glTF 2.0 (GLB)
- `batch_convert_all.py` — convert the whole `Data/` tree in parallel
- `png_to_ozj.py` — image → OZJ
- **`Png-to-OZJ.bat`** — drag a PNG (or several) onto this to create sibling `.OZJ`
- `mu_formats/` — shared library (cipher, BMD parser, GLB writer, texture codecs)

Full reference (formats, status, gotchas, how-to):
[`../docs/asset-pipeline.md`](../docs/asset-pipeline.md).

## Other tools

- `ResxGen/`, `DialogImporter/` — C# tools for the translation/resource system
  (see [`../docs/translation-system.md`](../docs/translation-system.md)).
