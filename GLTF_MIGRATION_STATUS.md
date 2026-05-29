# glTF 2.0 Migration Progress

## Overview
Migration from custom BMD binary format to industry-standard glTF 2.0, enabling Blender as the primary asset authoring tool.

## Current Status: Phase 1 (Core Loader) - In Progress

### ✅ Completed
- [x] GltfLoader.h header with data structures
  - Mesh, Bone, Animation, Material, Texture types
  - Vec3Wrapper, Vec4Wrapper for proper container storage
  - Accessor, BufferView, Buffer for glTF specification
  
- [x] GltfLoader.cpp implementation
  - GLB (binary glTF) file parsing with header validation
  - Separate .gltf (JSON) + .bin (binary buffer) parsing
  - JSON parsing using nlohmann/json.hpp
  - Mesh geometry extraction (vertices, normals, UVs, indices)
  - Material parsing from PBR metallic/roughness
  - Basic utility methods for binary data reading
  
- [x] ModelLoader.h/cpp
  - Unified Model interface that supports both glTF and BMD
  - GltfModelWrapper and BmdModelWrapper adapters
  - Auto-detection of format by file extension
  - Factory methods for loading either format

- [x] Build System Updates
  - Added ThirdParty directory to CMake include paths
  - Automatic source file discovery (GLOB_RECURSE)

### 🔄 In Progress
- [ ] Build compilation (fixing any errors)
- [ ] Phase 2: Skeleton & Animation parsing (ParseSkin, ParseAnimation)

### ⏳ Planned
- [ ] Phase 3: Rendering integration (GltfRenderProxy)
- [ ] Phase 4: Asset conversion tool (BMD → glTF)
- [ ] Phase 5: Blender addon development
- [ ] Phase 6: Full asset migration

## Architecture

### Data Flow
```
Blender Model
    ↓
Export as .glb or .gltf + .bin
    ↓
glTF Loader (GltfLoader.cpp)
    ↓
Model Wrapper (GltfModelWrapper)
    ↓
Rendering System (adapt to existing RenderMesh)
    ↓
Shader System (Phong, Chrome, SSAO)
```

### File Structure
```
src/source/Render/Models/
├── GltfLoader.h          # glTF parser header
├── GltfLoader.cpp        # glTF parser implementation
├── ModelLoader.h         # Unified model interface
├── ModelLoader.cpp       # Format-agnostic loader
├── ZzzBMD.h/cpp         # Existing BMD model system
└── ... (other model files)
```

### Key Dependencies
- **nlohmann/json.hpp**: Header-only JSON library (already in project)
- **vec3_t, vec4_t**: Project's vector types (float[3], float[4])
- **ZzzBMD.h**: Existing rendering system (RenderMesh interface)

## Next Steps

### Immediate (This Session)
1. ✅ Verify Phase 1 build succeeds
2. ⏳ Complete ParseSkin() for skeleton loading
3. ⏳ Complete ParseAnimation() for animation tracks
4. ⏳ Create test glTF model from Blender
5. ⏳ Test loading and rendering

### Short Term (Next Session)
1. Create GltfRenderProxy adapter class
2. Integrate with RenderMesh system
3. Test material and rendering
4. Verify bone transforms and animations

### Medium Term
1. Create BMD → glTF converter tool
2. Batch convert all existing assets
3. Develop Blender exporter addon
4. Create documentation for asset creators

## Known Issues & Workarounds
- Array types (vec3_t) cannot be stored directly in std::vector
  - **Solution**: Created Vec3Wrapper and Vec4Wrapper structs
  
- json.hpp needs to be in include path
  - **Solution**: Added ThirdParty to CMakeLists.txt include_directories

## Testing Plan

### Unit Tests
- [ ] Load simple glTF cube
- [ ] Verify vertex data parsing
- [ ] Check material parsing
- [ ] Test both GLB and separate .gltf + .bin formats

### Integration Tests
- [ ] Load character model with skeleton
- [ ] Verify bone hierarchy
- [ ] Play animation sequences
- [ ] Check rendering with lighting and shadows

### Performance Tests
- [ ] Measure load time (glTF vs BMD)
- [ ] Memory usage comparison
- [ ] Renderer frame rate stability

## References
- glTF 2.0 Spec: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html
- nlohmann/json: https://github.com/nlohmann/json
- Blender glTF Export: https://docs.blender.org/manual/en/latest/addons/import_export/scene_gltf2.html

## Questions & Decisions
1. **Skeleton Mapping**: Should we keep BMD bone naming or adopt glTF node names?
   - Decision: Use glTF node names, update game code to reference by name or index
   
2. **Animation Blending**: Does glTF support the same action blending as BMD?
   - Investigation: glTF animations are separate tracks; may need wrapper for action system
   
3. **Backwards Compatibility**: Continue supporting BMD files?
   - Decision: Yes, keep dual-format support during transition; eventually deprecate BMD
