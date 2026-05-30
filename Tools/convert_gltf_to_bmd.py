#!/usr/bin/env python3
"""Convert a GLB back to a MU BMD model ("convert back the models").

Targets GLBs produced by convert_bmd_to_gltf.py (this repo's forward converter),
including light geometry/UV/texture edits done in Blender. Because our forward
GLB is bind-pose only (no animation tracks), the reverse step reuses the
**original sibling .bmd as a template** for the skeleton, animations and texture
names, and replaces only the geometry from the GLB. If no template is found it
falls back to a static, bind-pose-only model reconstructed from the GLB joints.

Output defaults to <stem>.bmd next to the GLB (overwriting). The pristine
original is backed up once to <stem>.bmd.orig before the first overwrite.

Usage:
    python tools/convert_gltf_to_bmd.py model.glb [-o out.bmd]
    python tools/convert_gltf_to_bmd.py model.glb --template original.bmd
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path
from typing import List, Optional

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mu_formats.bmd import (Action, Bone, BoneKey, BmdModel, Mesh,  # noqa: E402
                            NotAModelError, parse_bmd, write_bmd)
from mu_formats.glb import GlbReader  # noqa: E402
import convert_bmd_to_gltf as fwd  # noqa: E402  (reuse bind-matrix math)

_ROUND = 5  # dedup precision for positions/normals/uvs


def _gather_primitives(r: GlbReader):
    prims = []
    for mesh in r.meshes:
        for p in mesh["primitives"]:
            prims.append(p)
    return prims


def _joint_to_bone(r: GlbReader):
    """Map a JOINTS_0 component value -> bone (node) index via skin.joints."""
    if not r.skins:
        return None
    joints = r.skins[0]["joints"]
    return joints  # joints[jval] == node index == bone index (our writer)


def _rebuild_mesh(prim, r, glob, joints_map, template_mesh) -> Mesh:
    attrs = prim["attributes"]
    pos = r.accessor(attrs["POSITION"])
    nrm = r.accessor(attrs["NORMAL"]) if "NORMAL" in attrs else None
    uv = r.accessor(attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None
    jnt = r.accessor(attrs["JOINTS_0"]) if "JOINTS_0" in attrs else None
    idx_raw = r.accessor(prim["indices"])
    idx = [i[0] for i in idx_raw]

    def bone_of(corner: int) -> int:
        if jnt is None or joints_map is None:
            return 0
        return joints_map[jnt[corner][0]]

    inv_cache = {}

    def inv_glob(node: int):
        m = inv_cache.get(node)
        if m is None:
            base = glob[node] if (glob is not None and 0 <= node < len(glob)) else np.eye(4)
            m = np.linalg.inv(base)
            inv_cache[node] = m
        return m

    verts: List[tuple] = []
    vbone: List[int] = []
    vmap = {}
    norms: List[tuple] = []
    nbone: List[int] = []
    nbind: List[int] = []
    nmap = {}
    uvs: List[tuple] = []
    umap = {}
    faces: List[tuple] = []

    def add_vertex(corner):
        node = bone_of(corner)
        p = np.array([pos[corner][0], pos[corner][1], pos[corner][2], 1.0])
        lp = inv_glob(node) @ p
        key = (node, round(lp[0], _ROUND), round(lp[1], _ROUND), round(lp[2], _ROUND))
        vi = vmap.get(key)
        if vi is None:
            vi = len(verts)
            vmap[key] = vi
            verts.append((float(lp[0]), float(lp[1]), float(lp[2])))
            vbone.append(node)
        return vi, node

    def add_normal(corner, node, vidx):
        if nrm is None:
            return 0
        R = (glob[node][:3, :3] if (glob is not None and 0 <= node < len(glob))
             else np.eye(3))
        ln = R.T @ np.array(nrm[corner])
        n = ln / (np.linalg.norm(ln) or 1.0)
        key = (node, round(n[0], _ROUND), round(n[1], _ROUND), round(n[2], _ROUND))
        ni = nmap.get(key)
        if ni is None:
            ni = len(norms)
            nmap[key] = ni
            norms.append((float(n[0]), float(n[1]), float(n[2])))
            nbone.append(node)
            nbind.append(vidx)
        return ni

    def add_uv(corner):
        if uv is None:
            return 0
        u, v = uv[corner][0], uv[corner][1]
        key = (round(u, _ROUND), round(v, _ROUND))
        ti = umap.get(key)
        if ti is None:
            ti = len(uvs)
            umap[key] = ti
            uvs.append((float(u), float(v)))
        return ti

    for t in range(0, len(idx) - 2, 3):
        corners = idx[t:t + 3]
        vidx, nidx, tidx = [], [], []
        for c in corners:
            vi, node = add_vertex(c)
            vidx.append(vi)
            nidx.append(add_normal(c, node, vi))
            tidx.append(add_uv(c))
        faces.append((3, tuple(vidx) + (0,), tuple(nidx) + (0,), tuple(tidx) + (0,)))

    tex_id = template_mesh.texture_id if template_mesh else 0
    tex_name = template_mesh.texture_name if template_mesh else ""
    mesh = Mesh(tex_id, tex_name, verts, vbone, norms, nbone, uvs, faces)
    mesh.normal_bind = nbind  # consumed by write_bmd
    return mesh


def gltf_to_bmd(glb_path: Path, out_path: Optional[Path] = None,
                template_path: Optional[Path] = None) -> Path:
    glb_path = Path(glb_path)
    r = GlbReader.from_file(glb_path)
    prims = _gather_primitives(r)
    joints_map = _joint_to_bone(r)

    if template_path is None:
        cand = glb_path.with_suffix(".bmd")
        if cand.exists():
            template_path = cand
    template = None
    if template_path is not None:
        try:
            template = parse_bmd(template_path)
        except NotAModelError:
            template = None

    if template is not None:
        _, glob = fwd._bind_matrices(template)
        meshes = []
        for i, prim in enumerate(prims):
            tmesh = template.meshes[i] if i < len(template.meshes) else None
            meshes.append(_rebuild_mesh(prim, r, glob, joints_map, tmesh))
        model = BmdModel(name=template.name, version=0x0C, meshes=meshes,
                         bones=template.bones, actions=template.actions)
    else:
        model = _fallback_from_glb(r, prims, joints_map)

    if out_path is None:
        out_path = glb_path.with_suffix(".bmd")
    out_path = Path(out_path)
    # Back up the pristine original once before overwriting it.
    if out_path.exists():
        backup = out_path.with_suffix(out_path.suffix + ".orig")
        if not backup.exists():
            backup.write_bytes(out_path.read_bytes())
    write_bmd(model, out_path)
    return out_path


def _quat_to_euler_zyx(x, y, z, w):
    """Quaternion -> (rx, ry, rz) radians matching AngleMatrix's Z*Y*X order."""
    # Build rotation matrix, then extract Z*Y*X Euler.
    r = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    sp = -r[2, 0]
    sp = max(-1.0, min(1.0, sp))
    ry = math.asin(sp)
    if abs(sp) < 0.99999:
        rx = math.atan2(r[2, 1], r[2, 2])
        rz = math.atan2(r[1, 0], r[0, 0])
    else:
        rx = math.atan2(-r[1, 2], r[1, 1])
        rz = 0.0
    return (rx, ry, rz)


def _fallback_from_glb(r: GlbReader, prims, joints_map) -> BmdModel:
    """Static, bind-pose-only model when no template BMD is available."""
    nodes = r.nodes
    joints = r.skins[0]["joints"] if r.skins else []
    actions = [Action(num_keys=1, lock_positions=False, positions=[])]
    bones: List[Bone] = []
    node_to_bone = {n: i for i, n in enumerate(joints)}
    for bone_i, node_i in enumerate(joints):
        node = nodes[node_i]
        t = node.get("translation", [0.0, 0.0, 0.0])
        q = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
        euler = _quat_to_euler_zyx(*q)
        parent = -1
        for pj, pn in enumerate(joints):
            if "children" in nodes[pn] and node_i in nodes[pn]["children"]:
                parent = pj
                break
        bones.append(Bone(name=node.get("name", f"Bone{bone_i}"), parent=parent,
                          dummy=False, keys=[[BoneKey(tuple(t), euler)]]))

    # Bind matrices straight from joint locals.
    tmp = BmdModel("blender.smd", 0x0C, [], bones, actions)
    _, glob = fwd._bind_matrices(tmp)
    meshes = [_rebuild_mesh(p, r, glob, joints_map, None) for p in prims]
    return BmdModel("blender.smd", 0x0C, meshes, bones, actions)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Convert GLB back to MU BMD.")
    ap.add_argument("inputs", nargs="*", help="GLB file(s) to convert")
    ap.add_argument("-o", "--output", help="output BMD (single-file mode)")
    ap.add_argument("--template", help="original BMD for rig/animation/texture names")
    args = ap.parse_args(argv)

    if not args.inputs:
        print("Drag a .glb onto Glb-to-BMD.bat, or run:")
        print("  python tools/convert_gltf_to_bmd.py model.glb")
        return 1

    rc = 0
    for raw in args.inputs:
        src = Path(raw)
        try:
            out = Path(args.output) if args.output else None
            tmpl = Path(args.template) if args.template else None
            # Determine the mode before conversion (which may create the .bmd).
            had_template = (tmpl.exists() if tmpl else src.with_suffix(".bmd").exists())
            dst = gltf_to_bmd(src, out, tmpl)
            mode = "template: rig+anim preserved" if had_template else "static (no animation)"
            print(f"OK  {src.name}  ->  {dst.name}  [{mode}]")
        except Exception as e:  # noqa: BLE001
            rc = 1
            print(f"ERROR  {src}: {type(e).__name__}: {e}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
