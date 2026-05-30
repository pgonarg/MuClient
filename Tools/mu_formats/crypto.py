"""MU map-file cipher.

Direct port of MapFileDecrypt / MapFileEncrypt from
src/source/Render/Terrain/ZzzLodTerrain.h.

The decrypted output is the same length as the input (the cipher is a
byte-for-byte stream transform, not a compressor).
"""

from __future__ import annotations

# Fixed 16-byte XOR key and initial rolling key, identical to the engine.
_MAP_XOR_KEY = bytes((0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
                      0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2))
_MAP_KEY_INIT = 0x5E


def map_file_decrypt(src: bytes) -> bytes:
    """Decrypt a MU-encrypted byte stream (XOR key + rolling subtract)."""
    out = bytearray(len(src))
    w = _MAP_KEY_INIT
    for i, b in enumerate(src):
        out[i] = (b ^ _MAP_XOR_KEY[i % 16]) - (w & 0xFF) & 0xFF
        w = (b + 0x3D) & 0xFF
    return bytes(out)


def map_file_encrypt(src: bytes) -> bytes:
    """Encrypt a byte stream back to MU map-file form (inverse of decrypt)."""
    out = bytearray(len(src))
    w = _MAP_KEY_INIT
    for i, b in enumerate(src):
        enc = ((b + (w & 0xFF)) & 0xFF) ^ _MAP_XOR_KEY[i % 16]
        out[i] = enc
        w = (enc + 0x3D) & 0xFF
    return bytes(out)


# Simple 3-byte XOR used for small data blobs (bBuxCode in _crypt.h).
_BUX_CODE = bytes((0xFC, 0xCF, 0xAB))


def bux_convert(src: bytes) -> bytes:
    """Apply the 3-byte Bux XOR (self-inverse)."""
    return bytes(b ^ _BUX_CODE[i % 3] for i, b in enumerate(src))
