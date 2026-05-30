# Modding Guide

How to edit this game's **textures**, **3D models**, and **maps** using the tools
in `Tools/`. This is the task-oriented guide; for binary format details and the
converter internals see [`asset-pipeline.md`](asset-pipeline.md).

---

## 1. How assets are organized

There are **three** copies of the asset tree — know which is which:

| Location | Role | Edited? | In git? |
|---|---|---|---|
| **`Data/`** (repo root) | Editable **source** assets + their converted glTF/PNG | Yes — you edit here | No (git-ignored) |
| **`src/bin/Data/`** | What the build copies to the executable | No | Yes |
| **`build-mingw/src/Data/`** | Runtime assets next to `Main.exe` | No (generated) | No |

- The game (`Main.exe`) reads from the folder **next to the executable**
  (`build-mingw/src/Data/`), populated from `src/bin/Data/` at build time.
- `Data/` at the repo root is the **editing workspace** — Blender-friendly glTF/PNG
  live here alongside the original BMD/OZJ. The game never reads it directly.

**To make an edit show up in-game**, put the finished asset where the running
client reads it — either drop it into `build-mingw/src/Data/...` directly, or into
`src/bin/Data/...` and rebuild (the build copies it across). Always keep a backup
of the original first.

---

## 2. Prerequisites

- **Python 3.12** with **Pillow**: `python -m pip install Pillow`
- A modern **browser** (Chrome / Edge / Firefox) for the HTML tools
- **One-time conversion** of the asset tree (creates the glTF/PNG you'll edit):
  ```
  python Tools/batch_convert_all.py Data
  ```
  This writes a `.glb` next to every model and a `.png` next to every texture,
  inside `Data/`. ~12.5k files, a couple of minutes.
- For the cross-folder objects feature only: the ability to **build the client**
  (see [`build-guide.md`](build-guide.md) / `CLAUDE.md`).

---

## 3. Edit a texture

Textures ship as **OZJ** (opaque, JPEG-based) and **OZT** (alpha, TGA-based); the
batch step already converted them to PNG in `Data/`.

1. Find the texture's PNG in `Data/...` and edit it (or make a higher-res version).
   Keep dimensions **power-of-two** (256, 512, 1024…) — the GPU needs it.
2. Drag the PNG onto **`Tools/Png-to-OZJ.bat`** → it writes a sibling `.OZJ`.
   (Run `python Tools/png_to_ozj.py file.png` for the same thing.)
3. Copy the `.OZJ` over the original in the folder the game reads
   (`build-mingw/src/Data/.../`), keeping the **same filename**.
4. Run the client.

Notes:
- OZJ is opaque (alpha is dropped). Textures that need transparency are **OZT**;
  there's no OZT drag-tool yet — ask if you need one.
- The engine has no PNG loader, so the file you ship must be OZJ/OZT, not PNG.

---

## 4. Edit a 3D model

Round-trips **GLBs produced by this repo's converter** (the `.glb` files in
`Data/`). Light geometry/UV/material edits in Blender work well.

1. Open the model's `.glb` from `Data/...` in **Blender** (default import settings,
   **"+Y Up" on**). It imports upright thanks to the `MU_Orient` node.
2. Edit geometry, UVs, or materials.
3. **Export** back to `.glb` (glTF 2.0 Binary, default settings), keeping it **next
   to the original `.bmd`** with the same name.
4. Drag the `.glb` onto **`Tools/Glb-to-BMD.bat`** → it writes the `.bmd`
   (the pristine original is backed up once to `<name>.bmd.orig`).
   (Run `python Tools/convert_gltf_to_bmd.py model.glb` for the same thing.)
5. Copy the `.bmd` (and any edited textures, as OZJ/OZT) into the game's folder.

How it works / caveats:
- The reverse converter reuses the original `.bmd` as a **template** for the
  skeleton, animations, and texture names, replacing only the geometry — so
  **animations are preserved** for normal edits.
- It's scoped to our GLBs (single-bone, bind-pose, MU-native). Heavy re-rigging is
  out of scope. Without the template it falls back to a static, no-animation model.

---

## 5. Browse / identify models

Open **`Tools/gltf-explorer.html`** in a browser, click **Open folder…**, pick
`Data/` (or a subfolder). Every `.glb` is listed and searchable; click one to view
it in 3D with its textures. Use this to find the right model number for a map, or
to eyeball a converted model. (Loads Three.js from a CDN — needs internet.)

---

## 6. Edit a map

Open **`Tools/map-editor.html`**, **Open Data folder…**, pick a **world**, **Load
world**. The terrain (heightmap + tile textures) and every placed object render in
3D.

**Editing:**
- **Select**: click an object, or pick it in the left list.
- **Transform**: gizmo — `W` move, `E` rotate, `R` scale. Or type exact
  Position/Angle/Scale in the inspector.
- **Add / Duplicate / Delete**: `+ Object`, `Ctrl+D`, `Del`.
- **Change a model**: the inspector **Model** dropdown (or scrub the `Type #` to
  preview live).
- **Camera**: drag to orbit, scroll to zoom (toward cursor), `F` to focus selection.

**Import a model from another folder** (e.g. an `Object2` model into Devias):
- Click **Import…**, filter, pick a model from any `Object{M}/` folder.
- It's assigned a free slot (`Type < 160`) in the current world and previewed.

**Export:**
- Click **Export .obj** → downloads a re-encrypted `EncTerrain{N}.obj`.
- If you imported cross-folder models, it also downloads **`ExtraObjects.txt`**.
- Drop **both** files into the game's `Data/World{N}/` folder.
- ⚠️ **Back up the original `EncTerrain{N}.obj` first** — export overwrites it.

Notes:
- World object placement is **client-side visual** data; the server uses the
  `.att` files, so map edits don't need server changes.
- **World 1 (Lorencia)** stores objects by name (`Tree`, `House`, `Bridge`…) at
  fixed type indices, not `ObjectNN`. The editor knows this table, so Lorencia
  edits Just Work.

---

## 7. Cross-folder objects (`ExtraObjects.txt`)

By default a map can only use models from its own `Object{N}/` folder (the `.obj`
references models by slot index). To use a model from **another** folder without
copying assets, the client reads an optional **`Data/World{N}/ExtraObjects.txt`**
at world load:

```
# slot folder file   -> loads Object{folder}\Object{file}.bmd into the slot
120 2 16
```

- The **map editor generates this file** for you from your imports (Section 6).
- Implemented in `src/source/World/MapInfra/MapManager.cpp` (just before the world
  objects load). Client-only; absent file = no change.
- **Requires a client build** for this feature to be active. Then drop
  `EncTerrain{N}.obj` + `ExtraObjects.txt` together into `Data/World{N}/`.

---

## 8. Deploying & running

1. Put the finished assets where the client reads them
   (`build-mingw/src/Data/...`), or into `src/bin/Data/...` and rebuild.
2. Build the client when you changed C++ (`MapManager.cpp` for `ExtraObjects.txt`)
   — see `CLAUDE.md` / [`build-guide.md`](build-guide.md). Don't forget the
   post-build steps (`config.ini` server IP, runtime DLLs).
3. Run `build-mingw/src/Main.exe`.

---

## 9. Gotchas

- **Back up originals** before overwriting (`.bmd`, `.obj`, OZJ). The model
  converter auto-backs up to `.bmd.orig`; the map editor does **not** back up the
  `.obj` — do it yourself.
- **Power-of-two** texture dimensions only.
- **Blender export**: keep "+Y Up" on (the default) or models tip over.
- **OZJ = opaque, OZT = alpha.** Don't ship a PNG; the engine can't read it.
- The HTML tools need **internet** (Three.js via CDN) and a desktop browser
  (WebGL2).

---

## Reference

- Format details, converter internals, coordinate conventions, status:
  [`asset-pipeline.md`](asset-pipeline.md)
- Tool list: [`../Tools/README.md`](../Tools/README.md)
- Building: [`build-guide.md`](build-guide.md), `CLAUDE.md`
