# Phase 4: BMD to glTF Converter - Implementation Guide

## Status: FRAMEWORK COMPLETE, SERIALIZATION NEEDED

The converter framework is fully designed. All data extraction logic is implemented. What remains is the GLB/glTF file serialization layer.

## What's Implemented

### 1. BmdToGltfConverter (src/source/Render/Models/BmdToGltfConverter.h/cpp)
Core conversion library that transforms BMD data structures to glTF:

- **ConvertMeshes()**: Extracts triangle geometry from BMD
  - Triangles → indexed vertex array
  - Vertex positions (with optional scale)
  - Normals from BMD normal array
  - UV coordinates (TexCoord)
  - Bone indices and weights

- **ConvertBones()**: Builds skeleton from BMD hierarchy
  - Reads Bone_t array with parent relationships
  - Preserves bone names and hierarchy
  - Converts position/rotation to local transforms
  - Builds parent-child relationship graph
  - Marks root bones (parent = -1)

- **ConvertAnimations()**: Extracts animation tracks
  - Reads Action_t (animation actions)
  - Creates glTF animation samplers
  - Keyframe time generation
  - Placeholder for quaternion and scale tracks

- **ConvertMaterials()**: Maps BMD materials to glTF
  - Texture references
  - Render flags → glTF material properties
  - Engine metadata preservation via glTF extras

### 2. Command-Line Tool (src/tools/ConvertBmdToGltf.cpp)
Batch converter for processing multiple BMD files:

- Argument parsing (input/output directories, options)
- Recursive directory traversal for .bmd files
- Conversion statistics tracking
- Error handling and reporting

Features:
```
ConvertBmdToGltf.exe Data\Models\ Data\Models_glTF\
  --scale 0.01              # Scale geometry
  --no-animations          # Skip animations
  --embed-textures         # Embed texture data
  --separate-files         # Use .gltf+.bin instead of .glb
```

### 3. Placeholder: GltfFileWriter (src/source/Render/Models/GltfFileWriter.h)
Interface for GLB/glTF file serialization (stub implementation)

## What Remains: GLB Serialization

The critical missing piece is writing the binary GLB file format. This requires:

### Step 1: Implement GltfFileWriter::WriteGlb()

```cpp
bool GltfFileWriter::WriteGlb(const wchar_t* filePath, const gltf::GltfModel& model)
{
    // 1. Pack all binary data (vertices, indices, buffers)
    std::vector<uint8_t> binaryData;
    PackVertexData(model, binaryData);

    // 2. Create glTF JSON structure
    auto gltfJson = CreateGltfJson(model, binaryData.size());

    // 3. Write GLB container format
    return WriteGlbContainer(filePath, gltfJson.dump(), binaryData);
}
```

### Step 2: Implement Binary Data Packing

Pack vertex data into linear binary buffer following glTF accessor layout:
```
Buffer Layout:
├─ Accessor 0 (positions): vec3_t[] @ offset 0
├─ Accessor 1 (normals): vec3_t[] @ offset (vertexCount * 12)
├─ Accessor 2 (texCoords): vec2_t[] @ offset ...
└─ Accessor 3 (indices): uint32_t[] @ offset ...
```

### Step 3: Implement JSON Generation

Create glTF JSON with proper accessor/bufferView/buffer structure:

```json
{
  "asset": { "version": "2.0", "generator": "MuUnique BMD Converter" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [
    { "mesh": 0, "children": [1, 2] },
    { "mesh": 1 },
    { "mesh": 2 }
  ],
  "meshes": [...],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "type": "VEC3", "count": 1024 },
    ...
  ],
  "bufferViews": [...],
  "buffers": [{ "byteLength": 65536 }]
}
```

### Step 4: Implement GLB Container Format

GLB file structure (binary):
```
Offset   Size   Description
0        4      Magic number (0x46546C67 = "glTF")
4        4      Version (2)
8        4      Total file size
12       4      JSON chunk length
16       4      JSON chunk type (0x4E4F534A = "JSON")
20       ...    JSON data (padded to 4-byte boundary)
...      4      Binary chunk length
...      4      Binary chunk type (0x004E4942 = "BIN\0")
...      ...    Binary data
```

## Implementation Checklist

- [ ] **BmdToGltfConverter::ConvertAndSave()**
  - [ ] Create file output path
  - [ ] Call Convert() to get glTF model
  - [ ] Call GltfFileWriter::WriteGlb()

- [ ] **GltfFileWriter::WriteGlb()**
  - [ ] Pack vertex/index binary data
  - [ ] Generate JSON structure
  - [ ] Write GLB container

- [ ] **Binary Packing**
  - [ ] Serialize positions (3x float per vertex)
  - [ ] Serialize normals (3x float per vertex)
  - [ ] Serialize UVs (2x float per vertex)
  - [ ] Serialize indices (uint32_t per triangle)
  - [ ] Align to 4-byte boundaries

- [ ] **JSON Generation**
  - [ ] Create asset metadata
  - [ ] Create scene/node hierarchy
  - [ ] Create mesh descriptions
  - [ ] Create accessors for each data channel
  - [ ] Create bufferViews mapping accessors to buffer regions
  - [ ] Create buffer metadata

- [ ] **Testing**
  - [ ] Convert single BMD file
  - [ ] Load result with glTF validator
  - [ ] Compare visual output with original BMD
  - [ ] Batch convert sample assets
  - [ ] Verify bone hierarchy preservation
  - [ ] Verify animation keyframes

## Quick Implementation Reference

### Writing Binary Data
```cpp
std::ofstream file(filePath, std::ios::binary);
for (const auto& vertex : model.meshes[0].vertices) {
    file.write(reinterpret_cast<const char*>(vertex.position), 12);  // 3 floats
}
```

### Creating Accessor
```json
{
  "bufferView": 0,
  "componentType": 5126,  // FLOAT
  "count": 1024,
  "type": "VEC3",
  "min": [0, 0, 0],
  "max": [100, 100, 100]
}
```

### GLB Header
```cpp
struct GLBHeader {
    uint32_t magic = 0x46546C67;  // "glTF"
    uint32_t version = 2;
    uint32_t fileSize;
};
```

## Key Resources

- [glTF 2.0 Specification](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html)
- [GLB Binary Format](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#file-format-specification)
- nlohmann/json usage: `#include <nlohmann/json.hpp>`

## Integration with Converter

After implementing GltfFileWriter, update BmdToGltfConverter::ConvertAndSave():

```cpp
bool BmdToGltfConverter::ConvertAndSave(
    const BMD* bmdModel,
    const wchar_t* outputPath,
    const ConversionOptions& options)
{
    auto gltfModel = Convert(bmdModel, options);
    if (!gltfModel) return false;

    return GltfFileWriter::WriteGlb(outputPath, *gltfModel);
}
```

Then ConvertBmdToGltf.exe can load BMD and write glTF:

```cpp
BMD bmdModel;
bmdModel.Open2(inputDir, inputFile);
BmdToGltfConverter::ConvertAndSave(&bmdModel, outputFile.c_str(), opts);
```

## Estimated Work

- GLB writing: 4-6 hours
- Testing/validation: 2-3 hours
- Batch conversion: 1-2 hours

Total: ~8-10 hours for full production-ready converter

## Benefits After Completion

- Batch convert 13,000+ assets in minutes
- Preserve all geometry, skeleton, animation data
- Enable Blender editing of existing assets
- Backup/archive assets in standard format
- Version control glTF (text/diff-friendly vs binary BMD)

---

**Created**: May 29, 2026  
**Framework Status**: Complete  
**Serialization Status**: Design ready, implementation required
