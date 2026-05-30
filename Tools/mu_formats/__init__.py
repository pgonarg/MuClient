"""MU asset format helpers (Blender-centric pipeline, Phase 2).

Pure-Python readers/decoders for the proprietary formats used by the client:
  - MU map-file cipher (used by version-0xC BMD models and terrain files)
  - BMD model parser (geometry, skeleton, bind pose, materials, animations)
  - OZJ / OZT texture containers (JPEG / TGA with a small prepended header)

These mirror the C++ engine logic:
  - cipher  -> src/source/Render/Terrain/ZzzLodTerrain.h (MapFileDecrypt)
  - BMD     -> src/source/Render/Models/ZzzBMD.cpp        (BMD::Open2)
"""
