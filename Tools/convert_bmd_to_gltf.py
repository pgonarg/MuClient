#!/usr/bin/env python3
"""Convert MU BMD models to glTF 2.0 (GLB).

Emits a skinned, bind-pose GLB per model:
  - one glTF joint node per BMD bone (local TRS from bind pose)
  - a skin with inverse-bind matrices
  - one mesh primitive per BMD mesh (POSITION/NORMAL/TEXCOORD_0/JOINTS_0/WEIGHTS_0)
  - a material per mesh referencing the texture as a sibling .png

Coordinates are emitted in the engine's native MU space (no Y-up conversion)
so converted GLBs load through the existing GltfLoader/GltfRenderProxy exactly
like the original BMD. Pass --y-up to rotate into Blender-native orientation.

Bind pose uses action 0, key 0 — matching how the engine seeds BoneTransform.
Animation tracks are not exported yet (geometry + skeleton + materials only);
the parser already extracts keys, so animations can be appended later.

Usage:
    python tools/convert_bmd_to_gltf.py <input.bmd> [-o out.glb]
    python tools/convert_bmd_to_gltf.py --batch Data [--out-suffix .glb]
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path
from typing import List, Optional

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mu_formats.bmd import BmdModel, Mesh, NotAModelError, parse_bmd  # noqa: E402
from mu_formats.glb import GlbBuilder  # noqa: E402
from mu_formats.textures import resolve_model_texture_png  # noqa: E402

DEG2RAD = math.pi / 180.0

# Vertex data stays in MU-native (Z-up) space so the engine's GltfLoader — which
# reads POSITION raw and ignores the scene graph — renders it correctly. To make
# the model stand upright in Blender we add a single root node rotated -90 deg
# about X. Blender's importer applies its own +90 deg Y-up->Z-up conversion on
# top, the two cancel, and MU Z-up shows as Blender Z-up. The engine ignores this
# node entirely (it never traverses nodes for mesh geometry).
# Quaternion [x, y, z, w] for a -90 deg rotation about X:
_ORIENT_ROOT_QUAT = [-0.7071067811865476, 0.0, 0.0, 0.7071067811865476]


def _angle_matrix(angles) -> np.ndarray:
    """Port of AngleMatrix: rotation = Z*Y*X, returns a 3x3 rotation matrix.

    Note: BMD bone rotations are stored in *radians* (AngleQuaternion uses the
    raw value as a half-angle without deg->rad scaling), so we do not rescale.
    """
    rx, ry, rz = angles
    sr, cr = math.sin(rx), math.cos(rx)
    sp, cp = math.sin(ry), math.cos(ry)
    sy, cy = math.sin(rz), math.cos(rz)
    return np.array([
        [cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy],
        [cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy],
        [-sp,     sr * cp,                cr * cp],
    ], dtype=np.float64)


def _local_matrix(pos, rot) -> np.ndarray:
    m = np.eye(4, dtype=np.float64)
    m[:3, :3] = _angle_matrix(rot)
    m[:3, 3] = pos
    return m


def _quat_from_matrix(r: np.ndarray):
    """Return (x, y, z, w) quaternion from a 3x3 rotation matrix."""
    t = np.trace(r)
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (r[2, 1] - r[1, 2]) / s
        y = (r[0, 2] - r[2, 0]) / s
        z = (r[1, 0] - r[0, 1]) / s
    elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
        s = math.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2
        w = (r[2, 1] - r[1, 2]) / s
        x = 0.25 * s
        y = (r[0, 1] + r[1, 0]) / s
        z = (r[0, 2] + r[2, 0]) / s
    elif r[1, 1] > r[2, 2]:
        s = math.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2
        w = (r[0, 2] - r[2, 0]) / s
        x = (r[0, 1] + r[1, 0]) / s
        y = 0.25 * s
        z = (r[1, 2] + r[2, 1]) / s
    else:
        s = math.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2
        w = (r[1, 0] - r[0, 1]) / s
        x = (r[0, 2] + r[2, 0]) / s
        y = (r[1, 2] + r[2, 1]) / s
        z = 0.25 * s
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    return (x / n, y / n, z / n, w / n)


def _bind_matrices(model: BmdModel):
    """Return (local[bone], global[bone]) 4x4 bind matrices for action 0/key 0."""
    n = len(model.bones)
    local = [np.eye(4) for _ in range(n)]
    for i, b in enumerate(model.bones):
        if b.dummy or not b.keys or not b.keys[0]:
            continue
        k = b.keys[0][0]
        local[i] = _local_matrix(k.position, k.rotation)

    glob = [None] * n
    for i, b in enumerate(model.bones):
        if b.dummy:
            glob[i] = np.eye(4)
            continue
        p = b.parent
        if 0 <= p < n and glob[p] is not None:
            glob[i] = glob[p] @ local[i]
        else:
            glob[i] = local[i]
    return local, glob


def _col_major(m: np.ndarray) -> List[float]:
    return m.T.flatten().tolist()


def _png_uri(texture_name: str, tex_dir: Optional[Path]) -> Optional[str]:
    # Maps the BMD-stored game name (.jpg/.tga) to the converter's PNG output,
    # resolving the proprietary source on disk and ozj/ozt collisions.
    return resolve_model_texture_png(tex_dir, texture_name)


def build_glb(model: BmdModel, orient: bool = True,
              tex_dir: Optional[Path] = None) -> bytes:
    g = GlbBuilder()
    local, glob = _bind_matrices(model)
    # Vertices are always emitted in MU-native space; orientation for Blender is
    # handled by a root node (see _ORIENT_ROOT_QUAT), not by baking coordinates.
    axis = np.eye(4)

    # Joint nodes: one per bone, children listed via parent links.
    children = {i: [] for i in range(len(model.bones))}
    roots = []
    for i, b in enumerate(model.bones):
        if 0 <= b.parent < len(model.bones):
            children[b.parent].append(i)
        else:
            roots.append(i)

    joint_node_index = list(range(len(model.bones)))  # bone i -> node i
    for i, b in enumerate(model.bones):
        t = local[i][:3, 3].tolist()
        q = _quat_from_matrix(local[i][:3, :3])
        node = {
            "name": (b.name or f"Bone{i}"),
            "translation": t,
            "rotation": list(q),
        }
        if children[i]:
            node["children"] = children[i]
        g.add_node(node)

    # Inverse bind matrices in the (optionally) reoriented space.
    ibm = []
    for i in range(len(model.bones)):
        gm = axis @ glob[i]
        ibm.append(_col_major(np.linalg.inv(gm)))
    ibm_accessor = g.add_mat4(ibm)
    skin_index = g.add_skin({"joints": joint_node_index,
                             "inverseBindMatrices": ibm_accessor})

    # Build one primitive per mesh.
    primitives = []
    image_cache = {}
    for mesh in model.meshes:
        prim = _build_primitive(g, mesh, glob, axis, image_cache, tex_dir)
        if prim is not None:
            primitives.append(prim)

    mesh_index = g.add_mesh({"name": model.name or "model",
                             "primitives": primitives})
    mesh_node = g.add_node({"name": model.name or "model",
                            "mesh": mesh_index, "skin": skin_index})

    top_nodes = roots + [mesh_node]
    if orient:
        # Single root that cancels Blender's Y-up->Z-up so the model stands
        # upright on import. Engine ignores it (never walks the node graph).
        orient_root = g.add_node({
            "name": "MU_Orient",
            "rotation": list(_ORIENT_ROOT_QUAT),
            "children": top_nodes,
        })
        g.add_to_scene(orient_root)
    else:
        for n in top_nodes:
            g.add_to_scene(n)
    return g.to_glb()


def _build_primitive(g: GlbBuilder, mesh: Mesh, glob, axis, image_cache, tex_dir):
    if not mesh.faces or not mesh.positions:
        return None

    # Pre-transform vertices/normals into bind-pose model space.
    vpos = []
    for p, node in zip(mesh.positions, mesh.vertex_bone):
        m = axis @ glob[node] if 0 <= node < len(glob) else axis
        v = m @ np.array([p[0], p[1], p[2], 1.0])
        vpos.append((float(v[0]), float(v[1]), float(v[2])))
    nrm = []
    for nvec, node in zip(mesh.normals, mesh.normal_bone):
        m = axis[:3, :3] @ (glob[node][:3, :3] if 0 <= node < len(glob) else np.eye(3))
        v = m @ np.array([nvec[0], nvec[1], nvec[2]])
        ln = math.sqrt(float(v @ v)) or 1.0
        nrm.append((float(v[0] / ln), float(v[1] / ln), float(v[2] / ln)))

    # Expand per-corner (BMD indexes position/normal/uv independently).
    out_pos, out_nrm, out_uv, out_joints, out_weights, out_idx = [], [], [], [], [], []
    nverts = len(mesh.positions)
    nnorms = len(mesh.normals)
    nuvs = len(mesh.texcoords)
    for (count, vi, ni, ti) in mesh.faces:
        corners = []
        for c in range(count):
            vidx, nidx, tidx = vi[c], ni[c], ti[c]
            if not (0 <= vidx < nverts):
                corners = []
                break
            pos = vpos[vidx]
            normal = nrm[nidx] if 0 <= nidx < nnorms else (0.0, 0.0, 1.0)
            uv = mesh.texcoords[tidx] if 0 <= tidx < nuvs else (0.0, 0.0)
            base = len(out_pos)
            out_pos.append(pos)
            out_nrm.append(normal)
            out_uv.append((uv[0], uv[1]))
            bone = mesh.vertex_bone[vidx]
            out_joints.append((max(bone, 0), 0, 0, 0))
            out_weights.append((1.0, 0.0, 0.0, 0.0))
            corners.append(base)
        if len(corners) == 3:
            out_idx += [corners[0], corners[1], corners[2]]
        elif len(corners) == 4:
            out_idx += [corners[0], corners[1], corners[2],
                        corners[0], corners[2], corners[3]]

    if not out_idx:
        return None

    attributes = {
        "POSITION": g.add_vec3(out_pos),
        "NORMAL": g.add_vec3(out_nrm),
        "TEXCOORD_0": g.add_vec2(out_uv),
        "JOINTS_0": g.add_joints_u16(out_joints),
        "WEIGHTS_0": g.add_weights_f32(out_weights),
    }
    prim = {"attributes": attributes, "indices": g.add_indices(out_idx)}

    uri = _png_uri(mesh.texture_name, tex_dir)
    if uri:
        if uri not in image_cache:
            img = g.add_image_uri(uri)
            tex = g.add_texture(img)
            mat = g.add_material({
                "name": Path(uri).stem,
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": tex},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
                "alphaMode": "MASK",
                "doubleSided": True,
            })
            image_cache[uri] = mat
        prim["material"] = image_cache[uri]
    return prim


def convert_file(src: Path, dst: Path, orient: bool = True) -> None:
    model = parse_bmd(src)
    glb = build_glb(model, orient=orient, tex_dir=src.parent)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(glb)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Convert BMD models to glTF (GLB).")
    ap.add_argument("input", help="BMD file, or root dir with --batch")
    ap.add_argument("-o", "--output", help="output GLB (single-file mode)")
    ap.add_argument("--batch", action="store_true",
                    help="treat input as a directory and convert all *.bmd")
    ap.add_argument("--no-orient", action="store_true",
                    help="omit the root node that makes models upright in Blender "
                         "(emit raw MU-native orientation)")
    args = ap.parse_args(argv)
    orient = not args.no_orient

    root = Path(args.input)
    if not args.batch:
        out = Path(args.output) if args.output else root.with_suffix(".glb")
        convert_file(root, out, orient=orient)
        print(f"wrote {out}")
        return 0

    files = sorted(root.rglob("*.bmd"))
    ok = skipped = errors = 0
    for f in files:
        try:
            convert_file(f, f.with_suffix(".glb"), orient=orient)
            ok += 1
        except NotAModelError:
            skipped += 1
        except Exception as e:  # noqa: BLE001
            errors += 1
            print(f"  ERROR {f}: {type(e).__name__}: {e}")
    print(f"BMD->GLB: converted={ok} skipped(data tables)={skipped} errors={errors}")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
