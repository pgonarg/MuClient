# Asset Pipeline (Blender-centric)

Reference for the asset-conversion pipeline that moves MU's proprietary assets
into Blender-native formats (glTF 2.0 / PNG) and back. This is the canonical
document for the work tracked by the plan
`crispy-shimmying-lark` (Blender-centric asset pipeline refactor).

> **Audience:** future contributors and AI assistants (Claude Code). It captures
> the format details and gotchas that are expensive to re-derive, plus the
> current state so work can resume without re-investigating.

---

## Goal

- Single source of truth for editable assets in **`Data/`** at the repo root
  (outside the build folder), instead of `src/bin/Data/`.
- Convert proprietary formats to Blender-native ones:
  - **BMD** models → **glTF 2.0 (GLB)**
  - **OZJ/OZT** textures → **PNG**
  - terrain → mesh (not yet implemented)
- Eventually: edit in Blender → rebuild → see it in-game, with the build syncing
  only changed files.

## Current status (2026-05-29)

| Phase | What | State |
|------|------|-------|
| 1 | Copy `src/bin/Data/` → repo-root `Data/` | **Done.** 13,160 files / 158 dirs / 740,541,794 bytes, verified exact. `Data/` is git-ignored (`/Data/`). |
| 2 | Python converters (textures, BMD; orchestrator) | **Done & run.** 7,481 PNG + 5,114 GLB produced, 0 errors, 186 non-model `.bmd` skipped. Terrain converter and animation export **not** done. |
| 3 | Build reads from `Data/` + incremental sync | **Not started.** Build still copies from `src/bin/` (`src/CMakeLists.txt:157`). |
| 4 | Blender export addon | Not started. |
| 5 | In-engine verification of converted assets | Not started (GLBs validated structurally + numerically only). |

The converted PNG/GLB outputs live **inside `Data/`** next to their sources and
are git-ignored along with everything else under `Data/`.

---

## Tooling (`tools/`)

Python 3.12. Requires **Pillow** (`python -m pip install Pillow`); NumPy is used
for the BMD math. glTF is written by a hand-rolled GLB writer (no extra deps).

```
tools/
  mu_formats/            shared library
    crypto.py            MU map-file cipher (MapFileDecrypt/Encrypt) + Bux XOR
    bmd.py               BMD model parser (geometry/skeleton/bind pose/materials)
    glb.py               minimal glTF 2.0 / GLB writer
    textures.py          OZJ/OZT decode, PNG naming, OZJ encode
  convert_textures.py    OZJ/OZT -> PNG          (single file or --batch)
  convert_bmd_to_gltf.py BMD -> GLB              (single file or --batch)
  batch_convert_all.py   parallel orchestrator + conversion.log
  png_to_ozj.py          image -> OZJ            (drag-drop helper)
  Png-to-OZJ.bat         drag a PNG onto this to get a sibling .OZJ
```

### Common commands

```bash
# Full tree (textures + models), parallel, writes <root>/conversion.log
python tools/batch_convert_all.py Data
python tools/batch_convert_all.py Data --only textures
python tools/batch_convert_all.py Data --only models
python tools/batch_convert_all.py Data --dry-run        # counts only

# One-offs
python tools/convert_textures.py Data/Monster/FMbody_R.OZJ
python tools/convert_bmd_to_gltf.py Data/Monster/monster186.bmd -o out.glb
python tools/png_to_ozj.py mytexture.png                # or drag onto the .bat
```

Flags worth knowing:
- `convert_bmd_to_gltf.py --no-orient` — emit raw MU orientation (see below).
- `batch_convert_all.py --workers N`, `--no-orient`, `--log PATH`.

---

## Format details (verified against engine source + real files)

These mirror the C++ engine. Keep them in sync if the engine changes.

### MU map-file cipher
Port of `MapFileDecrypt`/`MapFileEncrypt` in
`src/source/Render/Terrain/ZzzLodTerrain.h`. 16-byte XOR key + a rolling
subtract; output length equals input length (not a compressor). Used by
version-`0x0C` BMD bodies and terrain files. → `mu_formats/crypto.py`.

### BMD models
Port of `BMD::Open2` in `src/source/Render/Models/ZzzBMD.cpp`.

- Header: `'BMD'` magic + 1 version byte.
  - `0x0A` — unencrypted, body at offset 4.
  - `0x0C` — `int32 size` then cipher-encrypted body.
  - `0x00` — empty placeholder stub (handled, then skipped as empty).
  - `0x0E` — newer key (`webzen#@!...`), **unsupported** by the engine and the
    converter.
- C struct strides (with 4-byte padding — read explicitly, do not trust naïve
  `struct.calcsize`): `Vertex=16`, `Normal=20`, `TexCoord=8`,
  per-triangle record `=64` (== `sizeof(Triangle_t2)`).
- **~186 `.bmd` files are not models.** They lack the `'BMD'` magic (they are
  Bux-XOR `0xFC 0xCF 0xAB`-style game data: `item.bmd`, `MasterSkillTree.bmd`,
  `NPCDialogue.bmd`, every `Data/Local/*`, the per-world `minimap.bmd`, …). The
  Bux header bytes vary because the cipher is data-dependent, so the converter
  keys off the **missing magic**, not a fixed signature, and raises
  `NotAModelError` to skip them cleanly (matching the engine's own rejection).
- Bone Euler→matrix uses `AngleMatrix` (ZzzMathLib.cpp): rotation order
  **Z·Y·X**, global transform composes as `parent ∘ local`. Bind pose = action 0,
  key 0. Animation tracks are parsed but **not yet exported**.

### OZJ / OZT textures
- **OZJ** = 24-byte header + standard **JPEG** (engine: `ZzzTexture.cpp` does
  `fseek(fp, 24, SEEK_SET)` then libjpeg-turbo decodes RGB 4:4:4). Header content
  is ignored on read; the converter writes a canonical 24-byte header.
- **OZT** = 4-byte header + standard **TGA** (32-bit, carries alpha → PNG RGBA).

### Texture naming (collision-safe)
BMD materials reference textures by a **game** extension that the engine maps to
the shipped container: `.jpg → OZJ`, `.tga → OZT`. ~180 folders contain both
`foo.ozj` (opaque) and `foo.ozt` (alpha); collapsing both to `foo.png` would lose
a variant. The converter therefore:
- names colliding outputs `foo_ozj.png` / `foo_ozt.png` (else clean `foo.png`),
- resolves a BMD's `.jpg`/`.tga` reference to the matching PNG via
  `resolve_model_texture_png()` so material URIs always match files on disk.

`texture_png_name()` and `resolve_model_texture_png()` are the single source of
truth for this mapping; both converters call them.

---

## Orientation (why models stand up in Blender)

The engine's `GltfLoader` reads `POSITION` **raw** and never walks the glTF node
graph — it treats stored coordinates directly as MU **Z-up**. Blender's importer,
by contrast, applies a +90° X (Y-up→Z-up) conversion on import.

So the converter **keeps vertex data MU-native** (engine-correct) and adds a
single root node `MU_Orient` rotated **−90° about X**. Blender's +90° and the
node's −90° cancel, so models import **upright and axis-aligned**; the engine
ignores the node entirely. Verified numerically: the Blender-import transform
chain reproduces the original MU bounding box.

- Default: orientation node **on**.
- `--no-orient`: omit it (raw MU orientation).
- Assumes Blender's import option **“+Y Up” is checked** (the default). If you
  uncheck it, the model tips the other way.

---

## Known limitations / next steps

1. **Edited textures are not yet picked up by a build.** Three blockers:
   (a) the build copies from `src/bin/`, not `Data/` (Phase 3);
   (b) the engine has **no PNG loader** — it loads OZJ/OZT by the BMD's texture
   name; (c) GLB image URIs are parsed but not decoded into GL textures.
   *Workaround today:* use `Png-to-OZJ.bat` and drop the `.OZJ` into the matching
   `src/bin/Data/...` folder, then rebuild. *Proper fix:* Phase 3 + a build step
   that re-encodes changed PNGs to OZJ/OZT (or add PNG loading in-engine).
2. **Material maps beyond base color are not rendered.** `PhongLighting.glsl.h`
   exposes a single diffuse sampler (`uTexture`) + shadow map. `normalTexture` is
   parsed but never sampled; `metallicFactor` only toggles the legacy chrome
   effect. Normal/roughness/metallic maps need real shader work (samplers,
   tangents, a PBR/normal-mapped path) and matching converter output.
3. **No terrain converter** (`convert_terrain.py`) — needs the `ZzzLodTerrain`
   format (`.att` / height / `.map`) investigation.
4. **No animation export** — the parser already reads keys; export is additive.
5. **~1% of GLB material URIs are cross-folder texture shares.** The engine
   resolves textures via a global registry, not strict co-location; the converter
   assumes co-location. Fix: a global stem→path index fallback in
   `resolve_model_texture_png` emitting relative URIs.

---

## Tips for resuming (AI assistants)

- Validate format assumptions against **real files** in `Data/` and the engine
  source before trusting them; the strides/headers above were all confirmed that
  way.
- The converters are idempotent — re-running overwrites outputs. After a change to
  BMD/material logic, re-run `--only models`; for textures, `--only textures`.
- `Data/` is large (~700 MB) and git-ignored; never commit its contents. The
  Python tools under `tools/` **are** tracked (caches are git-ignored).
- Keep this doc and `mu_formats/` in sync with the engine if BMD/texture/terrain
  loading changes.
