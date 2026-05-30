"""Minimal glTF 2.0 / GLB writer.

Just enough of the spec to emit skinned static meshes with materials and
image references: buffers, bufferViews, accessors, meshes, nodes, skins,
materials, textures, images, samplers. Animations can be layered on later by
appending accessors + an "animations" array.

GLB layout: 12-byte header + JSON chunk + BIN chunk (both 4-byte aligned).
"""

from __future__ import annotations

import json
import struct
from typing import Any, Dict, List

# glTF component types
FLOAT = 5126
UNSIGNED_INT = 5125
UNSIGNED_SHORT = 5123
UNSIGNED_BYTE = 5121

# glTF target hints
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


class GlbBuilder:
    def __init__(self) -> None:
        self.json: Dict[str, Any] = {
            "asset": {"version": "2.0", "generator": "mu_formats glb writer"},
            "scenes": [{"nodes": []}],
            "scene": 0,
            "nodes": [],
            "meshes": [],
            "accessors": [],
            "bufferViews": [],
            "buffers": [],
        }
        self._bin = bytearray()

    # --- binary / accessor helpers ------------------------------------
    def _align(self) -> None:
        while len(self._bin) % 4:
            self._bin.append(0)

    def _buffer_view(self, data: bytes, target: int | None = None) -> int:
        self._align()
        offset = len(self._bin)
        self._bin += data
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.json["bufferViews"].append(view)
        return len(self.json["bufferViews"]) - 1

    def add_vec3(self, vecs: List[tuple], target: int = ARRAY_BUFFER) -> int:
        data = b"".join(struct.pack("<3f", *v) for v in vecs)
        view = self._buffer_view(data, target)
        mins = [min(v[i] for v in vecs) for i in range(3)] if vecs else [0, 0, 0]
        maxs = [max(v[i] for v in vecs) for i in range(3)] if vecs else [0, 0, 0]
        self.json["accessors"].append({
            "bufferView": view, "componentType": FLOAT,
            "count": len(vecs), "type": "VEC3", "min": mins, "max": maxs,
        })
        return len(self.json["accessors"]) - 1

    def add_vec2(self, vecs: List[tuple]) -> int:
        data = b"".join(struct.pack("<2f", *v) for v in vecs)
        view = self._buffer_view(data, ARRAY_BUFFER)
        self.json["accessors"].append({
            "bufferView": view, "componentType": FLOAT,
            "count": len(vecs), "type": "VEC2",
        })
        return len(self.json["accessors"]) - 1

    def add_indices(self, idx: List[int]) -> int:
        data = b"".join(struct.pack("<I", i) for i in idx)
        view = self._buffer_view(data, ELEMENT_ARRAY_BUFFER)
        self.json["accessors"].append({
            "bufferView": view, "componentType": UNSIGNED_INT,
            "count": len(idx), "type": "SCALAR",
        })
        return len(self.json["accessors"]) - 1

    def add_joints_u16(self, joints: List[tuple]) -> int:
        data = b"".join(struct.pack("<4H", *j) for j in joints)
        view = self._buffer_view(data, ARRAY_BUFFER)
        self.json["accessors"].append({
            "bufferView": view, "componentType": UNSIGNED_SHORT,
            "count": len(joints), "type": "VEC4",
        })
        return len(self.json["accessors"]) - 1

    def add_weights_f32(self, weights: List[tuple]) -> int:
        data = b"".join(struct.pack("<4f", *w) for w in weights)
        view = self._buffer_view(data, ARRAY_BUFFER)
        self.json["accessors"].append({
            "bufferView": view, "componentType": FLOAT,
            "count": len(weights), "type": "VEC4",
        })
        return len(self.json["accessors"]) - 1

    def add_mat4(self, mats: List[List[float]]) -> int:
        """mats: list of 16 floats each, already column-major for glTF."""
        data = b"".join(struct.pack("<16f", *m) for m in mats)
        view = self._buffer_view(data)
        self.json["accessors"].append({
            "bufferView": view, "componentType": FLOAT,
            "count": len(mats), "type": "MAT4",
        })
        return len(self.json["accessors"]) - 1

    # --- structural helpers -------------------------------------------
    def add_node(self, node: Dict[str, Any]) -> int:
        self.json["nodes"].append(node)
        return len(self.json["nodes"]) - 1

    def add_to_scene(self, node_index: int) -> None:
        self.json["scenes"][0]["nodes"].append(node_index)

    def add_mesh(self, mesh: Dict[str, Any]) -> int:
        self.json["meshes"].append(mesh)
        return len(self.json["meshes"]) - 1

    def add_skin(self, skin: Dict[str, Any]) -> int:
        self.json.setdefault("skins", []).append(skin)
        return len(self.json["skins"]) - 1

    def add_material(self, material: Dict[str, Any]) -> int:
        self.json.setdefault("materials", []).append(material)
        return len(self.json["materials"]) - 1

    def add_image_uri(self, uri: str) -> int:
        self.json.setdefault("images", []).append({"uri": uri})
        return len(self.json["images"]) - 1

    def add_texture(self, image_index: int, sampler_index: int = 0) -> int:
        self.json.setdefault("samplers", [{"wrapS": 10497, "wrapT": 10497,
                                           "magFilter": 9729, "minFilter": 9987}])
        self.json.setdefault("textures", []).append(
            {"source": image_index, "sampler": sampler_index})
        return len(self.json["textures"]) - 1

    # --- serialization ------------------------------------------------
    def to_glb(self) -> bytes:
        self.json["buffers"] = [{"byteLength": len(self._bin)}]
        json_bytes = json.dumps(self.json, separators=(",", ":")).encode("utf-8")
        while len(json_bytes) % 4:
            json_bytes += b" "
        bin_bytes = bytes(self._bin)
        while len(bin_bytes) % 4:
            bin_bytes += b"\x00"

        total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
        out = bytearray()
        out += struct.pack("<III", 0x46546C67, 2, total)            # "glTF", ver 2
        out += struct.pack("<II", len(json_bytes), 0x4E4F534A)      # JSON chunk
        out += json_bytes
        out += struct.pack("<II", len(bin_bytes), 0x004E4942)       # BIN chunk
        out += bin_bytes
        return bytes(out)
