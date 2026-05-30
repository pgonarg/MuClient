"""BMD model parser.

Faithful port of BMD::Open2 (src/source/Render/Models/ZzzBMD.cpp).

On-disk layout (all little-endian). Versions:
  0x0A : unencrypted, body starts at offset 4
  0x0C : body is MU-encrypted; [int32 encSize][encrypted bytes]
  0x0E : newer key (webzen#@!...), not handled by the engine yet -> unsupported

Body:
  char  Name[32]
  int16 NumMeshs, NumBones, NumActions
  per mesh:
    int16 NumVertices, NumNormals, NumTexCoords, NumTriangles, Texture
    Vertex   [NumVertices]   (stride 16: int16 Node, pad2, float3 Position)
    Normal   [NumNormals]    (stride 20: int16 Node, pad2, float3 Normal, int16 Bind, pad2)
    TexCoord [NumTexCoords]  (stride  8: float U, float V)
    Triangle [NumTriangles]  (stride 64 = sizeof Triangle_t2)
    char  TextureFileName[32]
  per action:
    int16 NumAnimationKeys
    uint8 LockPositions
    if LockPositions and keys>0: float3 Positions[keys]
  per bone:
    uint8 Dummy
    if not Dummy:
      char  Name[32]
      int16 Parent
      per action (if keys>0): float3 Position[keys], float3 Rotation[keys]
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

from .crypto import map_file_decrypt

# C struct strides (4-byte alignment, matching the MSVC/MinGW layout the
# engine relies on). Derived field-by-field, not via Python struct sizing.
VERTEX_STRIDE = 16     # short Node; (pad); float3 Position
NORMAL_STRIDE = 20     # short Node; (pad); float3 Normal; short Bind; (pad)
TEXCOORD_STRIDE = 8    # float U; float V
TRIANGLE_STRIDE = 64   # sizeof(Triangle_t2) — the on-disk per-triangle record

# Field offsets inside a triangle record we actually need.
TRI_POLYGON = 0        # uint8: vertex count of the face (3 or 4)
TRI_VINDEX = 2         # int16[4]
TRI_NINDEX = 10        # int16[4]
TRI_TINDEX = 18        # int16[4]


@dataclass
class Mesh:
    texture_id: int
    texture_name: str
    positions: List[tuple]          # bone-local float3 per vertex
    vertex_bone: List[int]          # Node (bone index) per vertex
    normals: List[tuple]            # bone-local float3 per normal
    normal_bone: List[int]
    texcoords: List[tuple]          # (u, v)
    # Each face: (count, [vIdx..], [nIdx..], [tIdx..]) with count in {3,4}.
    faces: List[tuple] = field(default_factory=list)


@dataclass
class BoneKey:
    position: tuple
    rotation: tuple                 # Euler radians (x, y, z)


@dataclass
class Bone:
    name: str
    parent: int
    dummy: bool
    # Per-action list of per-key BoneKey. keys[action][key].
    keys: List[List[BoneKey]] = field(default_factory=list)


@dataclass
class Action:
    num_keys: int
    lock_positions: bool
    positions: List[tuple] = field(default_factory=list)


@dataclass
class BmdModel:
    name: str
    version: int
    meshes: List[Mesh]
    bones: List[Bone]
    actions: List[Action]


class BmdError(Exception):
    pass


class NotAModelError(BmdError):
    """The .bmd file is not a 3D model and should be skipped, not errored.

    Two cases:
      * Data tables (item.bmd, MasterSkillTree.bmd, NPCDialogue.bmd, minimap.bmd,
        the Data/Local/* files, ...) reuse the .bmd extension but are Bux-XOR
        encrypted game data. They lack the 'BMD' magic; the Bux header bytes vary
        because the cipher is data-dependent, so we key off the missing magic
        rather than a fixed signature (matching the engine's own BMD::Open2).
      * Empty placeholder models (e.g. Object3/Object25.bmd) — valid 'BMD' magic
        but zero meshes/bones; nothing to export.
    """


class _Reader:
    __slots__ = ("buf", "pos")

    def __init__(self, buf: bytes, pos: int = 0):
        self.buf = buf
        self.pos = pos

    def i16(self) -> int:
        v = struct.unpack_from("<h", self.buf, self.pos)[0]
        self.pos += 2
        return v

    def u8(self) -> int:
        v = self.buf[self.pos]
        self.pos += 1
        return v

    def cstr(self, n: int) -> str:
        raw = self.buf[self.pos:self.pos + n]
        self.pos += n
        return raw.split(b"\x00", 1)[0].decode("latin-1", "replace")

    def take(self, n: int) -> bytes:
        raw = self.buf[self.pos:self.pos + n]
        self.pos += n
        return raw


def _decrypt_body(raw: bytes, ptr: int) -> bytes:
    enc_size = struct.unpack_from("<i", raw, ptr)[0]
    ptr += 4
    enc = raw[ptr:ptr + enc_size]
    return map_file_decrypt(enc)


def parse_bmd(path: Path) -> BmdModel:
    raw = Path(path).read_bytes()
    if raw[:3] != b"BMD":
        # No 'BMD' magic => not a model (Bux-encrypted data table, minimap, etc.).
        raise NotAModelError(f"not a model (no BMD magic): {path}")
    version = raw[3]

    if version in (0x0A, 0x00):
        # 0x00 is an empty placeholder stub; body (if any) is unencrypted at +4.
        r = _Reader(raw, 4)
    elif version == 0x0C:
        body = _decrypt_body(raw, 4)
        r = _Reader(body, 0)
    elif version == 0x0E:
        raise BmdError(f"BMD version 0x0E not supported (new key): {path}")
    else:
        raise BmdError(f"unknown BMD version {version:#x}: {path}")

    name = r.cstr(32)
    num_meshs = r.i16()
    num_bones = r.i16()
    num_actions = r.i16()

    if num_meshs <= 0 and num_bones <= 0:
        raise NotAModelError(f"empty placeholder model (no meshes/bones): {path}")

    meshes: List[Mesh] = []
    for _ in range(num_meshs):
        nv = r.i16()
        nn = r.i16()
        nt = r.i16()
        ntri = r.i16()
        tex = r.i16()

        positions, vbone = [], []
        vbuf = r.take(nv * VERTEX_STRIDE)
        for k in range(nv):
            off = k * VERTEX_STRIDE
            node = struct.unpack_from("<h", vbuf, off)[0]
            px, py, pz = struct.unpack_from("<3f", vbuf, off + 4)
            vbone.append(node)
            positions.append((px, py, pz))

        normals, nbone = [], []
        nbuf = r.take(nn * NORMAL_STRIDE)
        for k in range(nn):
            off = k * NORMAL_STRIDE
            node = struct.unpack_from("<h", nbuf, off)[0]
            nx, ny, nz = struct.unpack_from("<3f", nbuf, off + 4)
            nbone.append(node)
            normals.append((nx, ny, nz))

        texcoords = []
        tbuf = r.take(nt * TEXCOORD_STRIDE)
        for k in range(nt):
            u, v = struct.unpack_from("<2f", tbuf, k * TEXCOORD_STRIDE)
            texcoords.append((u, v))

        faces = []
        fbuf = r.take(ntri * TRIANGLE_STRIDE)
        for k in range(ntri):
            off = k * TRIANGLE_STRIDE
            count = fbuf[off + TRI_POLYGON]
            vi = struct.unpack_from("<4h", fbuf, off + TRI_VINDEX)
            ni = struct.unpack_from("<4h", fbuf, off + TRI_NINDEX)
            ti = struct.unpack_from("<4h", fbuf, off + TRI_TINDEX)
            faces.append((count, vi, ni, ti))

        tex_name = r.cstr(32)
        meshes.append(Mesh(tex, tex_name, positions, vbone,
                           normals, nbone, texcoords, faces))

    actions: List[Action] = []
    for _ in range(num_actions):
        nk = r.i16()
        lock = bool(r.u8())
        positions = []
        if lock and nk > 0:
            pbuf = r.take(nk * 12)
            for k in range(nk):
                positions.append(struct.unpack_from("<3f", pbuf, k * 12))
        actions.append(Action(nk, lock, positions))

    bones: List[Bone] = []
    for _ in range(num_bones):
        dummy = bool(r.u8())
        if dummy:
            bones.append(Bone(name="", parent=-1, dummy=True, keys=[]))
            continue
        bname = r.cstr(32)
        parent = r.i16()
        per_action: List[List[BoneKey]] = []
        for a in actions:
            nk = a.num_keys
            keys: List[BoneKey] = []
            if nk > 0:
                posbuf = r.take(nk * 12)
                rotbuf = r.take(nk * 12)
                for k in range(nk):
                    p = struct.unpack_from("<3f", posbuf, k * 12)
                    rot = struct.unpack_from("<3f", rotbuf, k * 12)
                    keys.append(BoneKey(p, rot))
            per_action.append(keys)
        bones.append(Bone(bname, parent, False, per_action))

    return BmdModel(name, version, meshes, bones, actions)
