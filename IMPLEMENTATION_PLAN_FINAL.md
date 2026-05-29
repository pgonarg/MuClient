# Final Implementation Plan: Asset Migration & Build Integration

## Overview
Complete the glTF asset migration workflow:
1. **Phase 4 Complete**: GLB binary serialization for converter
2. **Build Integration**: CMake script to detect and include modified assets
3. **Loader Enhancement**: Asset loader prefers glTF over BMD

**Estimated Duration**: 11-15 hours  
**Start Date**: May 29, 2026  
**Goal**: One-command workflow for assets (convert once, edit freely, build automatically)

---

## Part 1: Phase 4 - GLB Binary Serialization (8-10 hours)

### Step 1.1: Implement GltfFileWriter::WriteGlb()
**Location**: `src/source/Render/Models/GltfFileWriter.cpp`  
**What it does**: Main entry point for writing GLB files

```cpp
bool GltfFileWriter::WriteGlb(const wchar_t* filePath, const gltf::GltfModel& model)
{
    // 1. Pack binary data (vertices, indices)
    std::vector<uint8_t> binaryData;
    PackVertexData(model, binaryData);
    
    // 2. Create JSON structure
    auto gltfJson = CreateGltfJson(model, binaryData.size());
    std::string jsonString = gltfJson.dump();
    
    // 3. Write GLB container
    return WriteGlbContainer(filePath, jsonString, binaryData);
}
```

**Checklist**:
- [ ] Declare implementation in GltfFileWriter.cpp
- [ ] Handle error cases (null model, invalid path)
- [ ] Return success/failure status

### Step 1.2: Implement PackVertexData()
**Purpose**: Pack all vertex data into linear binary buffer

**Buffer Layout**:
```
Offset      Data                    Size
0           Positions (VEC3)        vertexCount * 12 bytes
[offset0]   Normals (VEC3)          vertexCount * 12 bytes
[offset1]   TexCoords (VEC2)        vertexCount * 8 bytes
[offset2]   Indices (uint32)        indexCount * 4 bytes
```

**Implementation**:
```cpp
void GltfFileWriter::PackVertexData(const gltf::GltfModel& model,
                                    std::vector<uint8_t>& buffer)
{
    size_t offset = 0;
    
    // Pack positions for all meshes
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            // Write vec3_t (3 floats = 12 bytes)
            buffer.insert(buffer.end(), 
                         (uint8_t*)&vertex.position[0],
                         (uint8_t*)&vertex.position[0] + 12);
        }
    }
    
    // Pack normals
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            buffer.insert(buffer.end(),
                         (uint8_t*)&vertex.normal[0],
                         (uint8_t*)&vertex.normal[0] + 12);
        }
    }
    
    // Pack UV coords
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            buffer.insert(buffer.end(),
                         (uint8_t*)&vertex.texCoord[0],
                         (uint8_t*)&vertex.texCoord[0] + 8);
        }
    }
    
    // Pack indices
    for (const auto& mesh : model.meshes) {
        for (uint32_t idx : mesh.indices) {
            buffer.insert(buffer.end(),
                         (uint8_t*)&idx,
                         (uint8_t*)&idx + 4);
        }
    }
}
```

**Checklist**:
- [ ] Pack positions accessor
- [ ] Pack normals accessor
- [ ] Pack UV coordinates accessor
- [ ] Pack triangle indices accessor
- [ ] Handle multiple meshes correctly

### Step 1.3: Implement CreateGltfJson()
**Purpose**: Generate proper glTF 2.0 JSON structure

**JSON Structure** (simplified example):
```json
{
  "asset": {
    "version": "2.0",
    "generator": "MuUnique BMD→glTF Converter"
  },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [
    { "mesh": 0, "name": "RootNode" },
    { "mesh": 1, "parent": 0 },
    { "mesh": 2, "parent": 0 }
  ],
  "meshes": [
    { "name": "Mesh_0", "primitives": [{ "attributes": {...}, "indices": 0 }] }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "type": "VEC3", "count": 1024 },
    { "bufferView": 1, "componentType": 5126, "type": "VEC3", "count": 1024 },
    { "bufferView": 2, "componentType": 5126, "type": "VEC2", "count": 1024 },
    { "bufferView": 3, "componentType": 5125, "type": "SCALAR", "count": 3072 }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 12288 },
    { "buffer": 0, "byteOffset": 12288, "byteLength": 12288 },
    { "buffer": 0, "byteOffset": 24576, "byteLength": 8192 },
    { "buffer": 0, "byteOffset": 32768, "byteLength": 12288 }
  ],
  "buffers": [
    { "byteLength": 45056 }
  ]
}
```

**Implementation**:
```cpp
nlohmann::json GltfFileWriter::CreateGltfJson(const gltf::GltfModel& model,
                                              size_t bufferByteLength)
{
    nlohmann::json gltf;
    
    // Asset metadata
    gltf["asset"]["version"] = "2.0";
    gltf["asset"]["generator"] = "MuUnique BMD→glTF Converter v1.0";
    
    // Scene setup
    gltf["scene"] = 0;
    gltf["scenes"][0]["nodes"] = nlohmann::json::array();
    for (size_t i = 0; i < model.meshes.size(); i++) {
        gltf["scenes"][0]["nodes"].push_back(i);
    }
    
    // Nodes (one per mesh)
    for (size_t i = 0; i < model.meshes.size(); i++) {
        nlohmann::json node;
        node["mesh"] = i;
        node["name"] = model.meshes[i].name;
        gltf["nodes"].push_back(node);
    }
    
    // Meshes
    for (size_t i = 0; i < model.meshes.size(); i++) {
        nlohmann::json mesh;
        mesh["name"] = model.meshes[i].name;
        // Add primitive with vertex data accessors
        // (implementation continues...)
        gltf["meshes"].push_back(mesh);
    }
    
    // Accessors, BufferViews, Buffers
    // (implementation continues...)
    
    gltf["buffers"][0]["byteLength"] = bufferByteLength;
    
    return gltf;
}
```

**Checklist**:
- [ ] Create asset metadata
- [ ] Create scene and nodes
- [ ] Create mesh primitives
- [ ] Create accessors for each vertex attribute
- [ ] Create buffer views mapping to buffer offsets
- [ ] Create buffer metadata

### Step 1.4: Implement WriteGlbContainer()
**Purpose**: Write GLB file format (magic header + JSON chunk + binary chunk)

**GLB File Format**:
```
Byte Range    Field           Description
0-3           Magic           0x46546C67 ("glTF")
4-7           Version         2 (as uint32)
8-11          Length          Total file size in bytes
12-15         JSONChunkLen    JSON data size
16-19         JSONChunkType   0x4E4F534A ("JSON")
20+           JSONChunkData   JSON string (padded to 4-byte boundary)
...+0         BINChunkLen     Binary data size
...+4         BINChunkType    0x00004E42 ("BIN\0")
...+8         BINChunkData    Binary buffer
```

**Implementation**:
```cpp
bool GltfFileWriter::WriteGlbContainer(const wchar_t* filePath,
                                       const std::string& jsonString,
                                       const std::vector<uint8_t>& binaryData)
{
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Calculate sizes
    uint32_t jsonChunkLen = jsonString.size();
    uint32_t jsonChunkLenPadded = ((jsonChunkLen + 3) / 4) * 4;  // 4-byte align
    uint32_t binChunkLen = binaryData.size();
    uint32_t totalFileSize = 28 + jsonChunkLenPadded + 8 + binChunkLen;
    
    // Write GLB header
    uint32_t magic = 0x46546C67;      // "glTF"
    uint32_t version = 2;
    file.write((const char*)&magic, 4);
    file.write((const char*)&version, 4);
    file.write((const char*)&totalFileSize, 4);
    
    // Write JSON chunk
    uint32_t jsonType = 0x4E4F534A;   // "JSON"
    file.write((const char*)&jsonChunkLenPadded, 4);
    file.write((const char*)&jsonType, 4);
    file.write(jsonString.c_str(), jsonString.size());
    
    // Pad JSON to 4-byte boundary
    uint32_t paddingSize = jsonChunkLenPadded - jsonChunkLen;
    for (uint32_t i = 0; i < paddingSize; i++) {
        file.write(" ", 1);  // Pad with spaces
    }
    
    // Write binary chunk
    uint32_t binType = 0x00004E42;    // "BIN\0"
    file.write((const char*)&binChunkLen, 4);
    file.write((const char*)&binType, 4);
    file.write((const char*)binaryData.data(), binaryData.size());
    
    file.close();
    return true;
}
```

**Checklist**:
- [ ] Write GLB magic number (0x46546C67)
- [ ] Write version (2)
- [ ] Calculate and write total file size
- [ ] Write JSON chunk header and data
- [ ] Pad JSON to 4-byte boundary
- [ ] Write binary chunk header and data
- [ ] Close file and return success

### Step 1.5: Update BmdToGltfConverter::ConvertAndSave()
**Purpose**: Wire the file writer into the converter

```cpp
bool BmdToGltfConverter::ConvertAndSave(
    const BMD* bmdModel,
    const wchar_t* outputPath,
    const ConversionOptions& options)
{
    if (!bmdModel || !outputPath) return false;

    // Convert BMD to glTF
    auto gltfModel = Convert(bmdModel, options);
    if (!gltfModel) return false;

    // Write to GLB file
    bool success = GltfFileWriter::WriteGlb(outputPath, *gltfModel);
    
    return success;
}
```

**Checklist**:
- [ ] Call Convert() to get glTF model
- [ ] Call GltfFileWriter::WriteGlb()
- [ ] Return success/failure

---

## Part 2: Build Integration (2-3 hours)

### Step 2.1: Create CMake Asset Change Detection Script
**Location**: `cmake/DetectAssetChanges.cmake`  
**Purpose**: Scan glTF folder and identify modified/new assets

```cmake
# Function to detect changes in converted assets
function(detect_gltf_changes SOURCE_DIR OUTPUT_DIR CHANGED_FILES_VAR)
    file(GLOB_RECURSE GLTF_FILES "${SOURCE_DIR}/*.glb" "${SOURCE_DIR}/*.gltf")
    
    set(CHANGED_FILES "")
    
    foreach(GLTF_FILE ${GLTF_FILES})
        # Check if file is newer than build timestamp
        if(NOT EXISTS "${OUTPUT_DIR}/.asset_build_timestamp")
            # First build, include all
            list(APPEND CHANGED_FILES "${GLTF_FILE}")
        else()
            file(TIMESTAMP "${OUTPUT_DIR}/.asset_build_timestamp" BUILD_TIME)
            file(TIMESTAMP "${GLTF_FILE}" FILE_TIME)
            
            if(FILE_TIME GREATER BUILD_TIME)
                list(APPEND CHANGED_FILES "${GLTF_FILE}")
            endif()
        endif()
    endforeach()
    
    set(${CHANGED_FILES_VAR} "${CHANGED_FILES}" PARENT_SCOPE)
endfunction()
```

**Checklist**:
- [ ] Find all .glb and .gltf files in Models_gltf/
- [ ] Compare timestamps with build timestamp
- [ ] Return list of changed files

### Step 2.2: Add CMake Build Command
**Location**: `src/CMakeLists.txt` (add to Main target post-build)

```cmake
# Copy modified glTF assets to build output
add_custom_command(TARGET Main POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../Data/Models_gltf"
        "-DOUTPUT_DIR=$<TARGET_FILE_DIR:Main>/Data"
        "-P" "${REPO_ROOT}/cmake/CopyModifiedAssets.cmake"
    VERBATIM
)
```

**Checklist**:
- [ ] Create post-build command
- [ ] Pass Models_gltf directory to script
- [ ] Pass build output directory
- [ ] Mark CMake files as CONFIGURE_DEPENDS

### Step 2.3: Create Asset Copy Script
**Location**: `cmake/CopyModifiedAssets.cmake`  
**Purpose**: Copy changed glTF files to build output

```cmake
# Copy modified glTF assets and update timestamp
function(copy_modified_assets)
    # Detect changes
    detect_gltf_changes("${SOURCE_DIR}" "${OUTPUT_DIR}" CHANGED_FILES)
    
    # Copy each changed file
    foreach(FILE ${CHANGED_FILES})
        file(RELATIVE_PATH REL_PATH "${SOURCE_DIR}" "${FILE}")
        set(DEST "${OUTPUT_DIR}/Models_gltf/${REL_PATH}")
        
        # Create destination directory
        get_filename_component(DEST_DIR "${DEST}" DIRECTORY)
        file(MAKE_DIRECTORY "${DEST_DIR}")
        
        # Copy file
        file(COPY "${FILE}" DESTINATION "${DEST_DIR}")
        message(STATUS "Copied modified asset: ${REL_PATH}")
    endforeach()
    
    # Update build timestamp
    file(WRITE "${OUTPUT_DIR}/.asset_build_timestamp" "${CMAKE_CURRENT_TIMESTAMP}")
endfunction()

copy_modified_assets()
```

**Checklist**:
- [ ] Call change detection
- [ ] Create output directories if needed
- [ ] Copy modified files with preservation of directory structure
- [ ] Update timestamp file for next build
- [ ] Print status messages

---

## Part 3: Asset Loader Enhancement (1-2 hours)

### Step 3.1: Update ModelLoader::Load()
**Location**: `src/source/Render/Models/ModelLoader.cpp`

**Current Logic**:
```cpp
auto Load(filePath) {
    if (endsWith(filePath, ".bmd")) return LoadAsBmd(filePath);
    if (endsWith(filePath, ".glb")) return LoadAsGltf(filePath);
    // ...
}
```

**New Logic**:
```cpp
auto Load(filePath) {
    // Extract base name without extension
    // e.g., "character.bmd" → "character"
    std::wstring baseName = GetBaseName(filePath);
    
    // Try to find glTF version in Models_gltf/
    std::wstring gltfPath = L"Data/Models_gltf/" + baseName + L".glb";
    if (FileExists(gltfPath)) {
        return LoadAsGltf(gltfPath);  // ← USE CONVERTED ASSET
    }
    
    // Fall back to original format
    if (endsWith(filePath, ".bmd")) {
        return LoadAsBmd(filePath);
    }
    if (endsWith(filePath, ".glb")) {
        return LoadAsGltf(filePath);
    }
    
    return nullptr;
}
```

**Implementation**:
```cpp
std::unique_ptr<Model> ModelLoader::Load(const wchar_t* filePath)
{
    if (!filePath) return nullptr;

    // Extract base name (e.g., "character" from "character.bmd")
    fs::path path(filePath);
    std::wstring baseName = path.stem().wstring();

    // Check for glTF version in Models_gltf/
    fs::path gltfPath = L"Data/Models_gltf/" / (baseName + L".glb");
    if (fs::exists(gltfPath)) {
        auto gltfModel = LoadAsGltf(gltfPath.c_str());
        if (gltfModel) {
            // Found and loaded glTF version (MODIFIED ASSET)
            return gltfModel;
        }
    }

    // Fall back to original file
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext == ".bmd") {
        return LoadAsBmd(filePath);
    } else if (ext == ".glb" || ext == ".gltf") {
        return LoadAsGltf(filePath);
    }

    return nullptr;
}
```

**Checklist**:
- [ ] Extract base name from file path
- [ ] Check Models_gltf/ for glTF version
- [ ] Return glTF if found and valid
- [ ] Fall back to original format if not found
- [ ] Handle all file extensions

### Step 3.2: Add Filesystem Include
**Location**: Top of ModelLoader.cpp

```cpp
#include <filesystem>
namespace fs = std::filesystem;
```

**Checklist**:
- [ ] Add filesystem header
- [ ] Create namespace alias

---

## Testing Checklist

### Phase 4 Testing (Converter)
- [ ] Load test BMD file
- [ ] Convert to GLB
- [ ] Validate GLB file structure
- [ ] Load converted GLB in glTF validator
- [ ] Verify vertices/normals/indices are correct
- [ ] Check GLB file size is reasonable
- [ ] Test with multiple BMD files

### Build Integration Testing
- [ ] Create Models_gltf/ folder structure
- [ ] Copy test .glb file to Models_gltf/
- [ ] Run cmake build
- [ ] Check file is copied to build output
- [ ] Modify test file (change timestamp)
- [ ] Rebuild
- [ ] Verify modified file is copied again

### Loader Testing
- [ ] Load asset by original name (e.g., "character.bmd")
- [ ] Check loader prefers Models_gltf/ version
- [ ] Rename glTF file
- [ ] Verify loader falls back to BMD
- [ ] Test missing asset handling

---

## Implementation Order

1. **Start with GltfFileWriter implementation** (2-3 hours)
   - WriteGlb()
   - PackVertexData()
   - CreateGltfJson()
   - WriteGlbContainer()

2. **Test converter** (1 hour)
   - Convert sample BMD
   - Validate output
   - Fix any issues

3. **CMake integration** (1-2 hours)
   - Add change detection
   - Add post-build command
   - Test with sample files

4. **Update asset loader** (1 hour)
   - Modify ModelLoader::Load()
   - Add filesystem checks
   - Test fallback logic

5. **Full integration testing** (1-2 hours)
   - End-to-end workflow
   - Document any issues
   - Finalize

---

## Progress Tracking

Each implementation step will:
1. Be coded and tested
2. Be committed to git with detailed message
3. Be documented in a progress section below
4. Update this master plan document

This ensures if context collapses, the next session can:
- Check git log for what's been done
- Read this document for status
- Continue from exact point of interruption

---

## Status Log

### Session 1: [To be filled as work progresses]
- [ ] Phase 4 Step 1.1: GltfFileWriter::WriteGlb() - START HERE
- [ ] Phase 4 Step 1.2: PackVertexData()
- [ ] Phase 4 Step 1.3: CreateGltfJson()
- [ ] Phase 4 Step 1.4: WriteGlbContainer()
- [ ] Phase 4 Step 1.5: ConvertAndSave() integration
- [ ] Phase 4 Testing
- [ ] Phase 2 Steps 2.1-2.3: CMake integration
- [ ] Phase 2 Testing
- [ ] Phase 3 Steps 3.1-3.2: Loader enhancement
- [ ] Phase 3 Testing
- [ ] Final integration test

---

**Document Version**: 1.0  
**Created**: May 29, 2026  
**Last Updated**: [Will update as implementation progresses]
