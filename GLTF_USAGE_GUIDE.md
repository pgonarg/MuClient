# glTF 2.0 Usage Guide

Quick-start instructions for loading, converting, and rendering glTF models in the game.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Load a glTF Model in Game Code](#load-a-gltf-model-in-game-code)
3. [Batch Convert Existing BMD Assets](#batch-convert-existing-bmd-assets)
4. [Export from Blender (Phase 5)](#export-from-blender-phase-5)
5. [Common Tasks](#common-tasks)
6. [Troubleshooting](#troubleshooting)

---

## Getting Started

### What You Need

- **Game Code**: C++ project already supports glTF loading
- **Models**: `.glb` or `.gltf` files (Blender export or converted from BMD)
- **Assets Folder**: Place glTF files in `Data/Models/` directory

### Supported Formats

| Format | File Extension | Status | Notes |
|--------|---|---|---|
| glTF Binary | `.glb` | ✅ Supported | Single-file, recommended |
| glTF Text | `.gltf` + `.bin` | ✅ Supported | Separate JSON + binary |
| BMD (legacy) | `.bmd` | ✅ Convertible | Use converter tool |

---

## Load a glTF Model in Game Code

### Simplest Approach: One-Line Load

```cpp
#include "Render/Models/GltfRenderSystem.h"

// Load model (cached automatically)
GltfRenderProxy* model = GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/character.glb");

if (!model) {
    // Failed to load
    return;
}
```

### In Your Render Loop

```cpp
// Prepare position and rotation
vec3_t position = {100, 50, 0};     // World position
vec3_t rotation = {0, 0, 0};        // Pitch, yaw, roll (radians)
vec3_t bodyLight = {1, 1, 1};       // Lighting color

// Render the model
GltfRenderSystem::GetInstance().RenderModel(
    model,                          // Model to render
    position,                       // World position
    rotation,                       // Rotation
    1.0f,                          // Scale (1.0 = normal size)
    -1,                            // Animation index (-1 = no animation)
    0.0f,                          // Animation frame (0.0-1.0)
    1.0f,                          // Alpha/opacity (0.0-1.0)
    bodyLight                      // Light color
);
```

### Clean Up When Done

```cpp
// Unload when model is no longer needed
GltfRenderSystem::GetInstance().UnloadModel(L"Data/Models/character.glb");
```

---

## Batch Convert Existing BMD Assets

### Setup

1. Create output directory:
   ```
   mkdir Data\Models_glTF
   ```

2. Ensure you have BMD files in a directory:
   ```
   Data\Models\                 ← BMD files here
   ```

### Run the Converter

```bash
# Basic conversion (all BMD files → glTF)
ConvertBmdToGltf.exe Data\Models\ Data\Models_glTF\

# With scale adjustment (for models that are too big/small)
ConvertBmdToGltf.exe Data\Models\ Data\Models_glTF\ --scale 0.01

# Skip animations if you only need static geometry
ConvertBmdToGltf.exe Data\Models\ Data\Models_glTF\ --no-animations

# Use separate files instead of .glb
ConvertBmdToGltf.exe Data\Models\ Data\Models_glTF\ --separate-files
```

### Options Explained

| Option | Purpose | Example |
|--------|---------|---------|
| `--scale <n>` | Scale geometry | `--scale 0.01` for small models |
| `--no-animations` | Skip animation data | Faster conversion, smaller files |
| `--embed-textures` | Include textures in .glb | For self-contained files |
| `--separate-files` | Use .gltf + .bin format | For easier inspection |
| `--help` | Show all options | Get more information |

### Output

```
Converted files in Data\Models_glTF\
├── character.glb
├── weapon.glb
├── effect.glb
└── (all BMD files converted)

Summary:
✓ 12,000 files successfully converted
✗ 50 files failed (check error log)
```

---

## Export from Blender (Phase 5)

*Note: This feature is coming in Phase 5. For now, use the converter tool.*

### Prerequisites

- Blender 3.0 or later
- Mu Unique glTF Exporter addon (installed)

### Export Process

1. **Create/open your model in Blender**
   - Import a reference BMD model, or
   - Create from scratch

2. **Set render properties**
   - In the Mu Exporter panel:
     - Set "Render Flags" (Texture, Chrome, Metal, etc.)
     - Set "Chrome Type" if applicable
     - Set "Lightmap Index" if needed

3. **Export to glTF**
   - File → Export → glTF 2.0 (.glb/.gltf)
   - Click "Export glTF 2.0"
   - Choose single-file (.glb) or separate (.gltf + .bin)

4. **Test in game**
   - Load your exported file
   - Verify materials and animations

---

## Common Tasks

### Task: Load a Character Model

```cpp
#include "Engine/Object/w_CharacterInfo.h"
#include "Render/Models/GltfRenderSystem.h"

void RenderCharacterGltf(CHARACTER* ch)
{
    // Load model once (cached)
    static GltfRenderProxy* model = nullptr;
    if (!model) {
        model = GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/char_fighter.glb");
    }

    if (!model) return;

    // Get character position and rotation
    vec3_t position;
    VectorCopy(ch->Object.Position, position);
    
    vec3_t rotation;
    VectorCopy(ch->Object.Angle, rotation);

    // Render with animation
    GltfRenderSystem::GetInstance().RenderModel(
        model,
        position,
        rotation,
        ch->Object.Scale,
        ch->CurrentAction,              // Current animation
        ch->Object.AnimationFrame,      // Frame within animation
        1.0f,                          // Full opacity
        ch->Light                      // Character's light color
    );
}
```

### Task: Load a Static Prop

```cpp
void RenderStaticProp()
{
    static GltfRenderProxy* model = nullptr;
    if (!model) {
        model = GltfRenderSystem::GetInstance().LoadModel(L"Data/Props/tree.glb");
    }

    vec3_t position = {100, 50, 0};
    vec3_t rotation = {0, 0, 0};
    vec3_t light = {1, 1, 1};

    // Static prop: no animation
    GltfRenderSystem::GetInstance().RenderModel(
        model, position, rotation, 1.0f,
        -1, 0.0f, 1.0f, light
    );
}
```

### Task: Get Model Information

```cpp
// Access model data directly
auto gltfModel = GltfLoader::Load(L"Data/Models/character.glb");

if (gltfModel) {
    printf("Meshes: %zu\n", gltfModel->meshes.size());
    printf("Bones: %zu\n", gltfModel->bones.size());
    printf("Animations: %zu\n", gltfModel->animations.size());
    
    // Print bone names
    for (const auto& bone : gltfModel->bones) {
        printf("  Bone: %s (parent: %d)\n", bone.name.c_str(), bone.parent);
    }
}
```

### Task: Load Multiple Models with Different Animations

```cpp
struct ModelVariant {
    GltfRenderProxy* idle;
    GltfRenderProxy* walk;
    GltfRenderProxy* attack;
};

ModelVariant LoadCharacterVariants()
{
    ModelVariant variant;
    variant.idle = GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/char_idle.glb");
    variant.walk = GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/char_walk.glb");
    variant.attack = GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/char_attack.glb");
    return variant;
}

// Later: render based on state
if (isAttacking) {
    // Render attack animation
} else if (isMoving) {
    // Render walk animation
} else {
    // Render idle animation
}
```

---

## Troubleshooting

### Model Fails to Load

**Error**: `GltfLoader::Load()` returns null

**Solutions**:
1. Check file path is correct and file exists
2. Ensure file is valid glTF 2.0 (.glb or .gltf)
3. Try validating with [glTF Validator](https://github.khronos.org/glTF-Validator/)

```cpp
// Debug: check file existence
#include <filesystem>
namespace fs = std::filesystem;

if (!fs::exists(L"Data/Models/character.glb")) {
    printf("File not found!\n");
}
```

### Model Renders But Looks Wrong

**Symptoms**: Missing textures, wrong colors, deformed geometry

**Solutions**:
1. **Check scale**: Model might be too big or too small
   - Use `--scale` option in converter
   - Or scale in Blender before export

2. **Check normals**: Lighting might be inverted
   - Ensure normals face outward in Blender
   - Re-export from Blender

3. **Check materials**: Textures might not be found
   - Place texture files next to model
   - Check texture paths in glTF file

### Animation Doesn't Play

**Solutions**:
1. Check animation index is valid (< total animations)
2. Check animation frame is 0.0-1.0 range
3. Verify model was exported with animations

```cpp
// Debug: print animation info
auto model = GltfLoader::Load(path);
printf("Animations: %zu\n", model->animations.size());
for (size_t i = 0; i < model->animations.size(); i++) {
    printf("  %zu: %s (duration: %.2f)\n", i, 
           model->animations[i].name.c_str(),
           model->animations[i].duration);
}
```

### Converter Fails

**Error**: "Failed to load BMD" or "Conversion failed"

**Solutions**:
1. Check BMD file is valid (can load in game normally)
2. Check output directory has write permissions
3. Check disk space is available
4. Try with `--scale` option

```bash
# Verbose output (if implemented)
ConvertBmdToGltf.exe Data\Models\ Data\Models_glTF\ --verbose
```

---

## Performance Tips

### 1. Cache Models

Models are automatically cached by file path:

```cpp
// First call: loads from disk
GltfRenderProxy* model1 = GltfRenderSystem::GetInstance().LoadModel(path);

// Second call: returns cached copy (fast)
GltfRenderProxy* model2 = GltfRenderSystem::GetInstance().LoadModel(path);

// Same pointer (cached)
assert(model1 == model2);
```

### 2. Unload When Done

```cpp
// Reduces memory usage if model is no longer needed
GltfRenderSystem::GetInstance().UnloadModel(path);
```

### 3. Use LOD (Level of Detail)

If you have multiple versions:

```cpp
GltfRenderProxy* GetCharacterModel(float distance)
{
    if (distance > 100) {
        return GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/char_lod2.glb");
    } else {
        return GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/char_lod0.glb");
    }
}
```

### 4. Batch Load at Startup

```cpp
void PreloadCommonModels()
{
    GltfRenderSystem& system = GltfRenderSystem::GetInstance();
    system.LoadModel(L"Data/Models/character.glb");
    system.LoadModel(L"Data/Models/weapon.glb");
    system.LoadModel(L"Data/Models/effect.glb");
    // Models stay in memory for fast access
}
```

---

## File Organization

Recommended directory structure:

```
Data/
├── Models/                  ← Original BMD files
│   ├── character/
│   ├── weapon/
│   ├── effect/
│   └── prop/
├── Models_glTF/            ← Converted glTF files (after Phase 4)
│   ├── character/
│   ├── weapon/
│   ├── effect/
│   └── prop/
└── Textures/               ← Material textures
    ├── character_skin.jpg
    ├── weapon_metal.jpg
    └── ...
```

---

## Next Steps

### To Start Using glTF Today

1. ✅ glTF loader is ready → Load `.glb` files in game code
2. ⏳ Converter framework is ready → Implement Phase 4 serialization to batch convert BMD
3. ⏳ Blender addon planned → Phase 5 for creating new assets in Blender

### To Get a Test Model

**Option A**: Convert existing BMD (once Phase 4 is complete)
```bash
ConvertBmdToGltf.exe Data\Models\sample.bmd Data\Models\sample.glb
```

**Option B**: Create simple test in Blender (once Phase 5 is complete)
1. Create cube in Blender
2. Add material
3. Export as .glb
4. Load in game

### What to Try First

1. Create a simple `.glb` file in Blender (cube with color)
2. Place in `Data/Models/test.glb`
3. Load in game with `GltfRenderSystem::GetInstance().LoadModel(L"Data/Models/test.glb")`
4. Render with `RenderModel()` at a fixed position
5. See if it appears on screen

---

## Quick Reference

| Task | Code |
|------|------|
| Load model | `GltfRenderSystem::GetInstance().LoadModel(path)` |
| Render | `GltfRenderSystem::GetInstance().RenderModel(model, pos, rot, scale, anim, frame, alpha, light)` |
| Unload | `GltfRenderSystem::GetInstance().UnloadModel(path)` |
| Get info | `GltfLoader::Load(path)` then access `.meshes`, `.bones`, `.animations` |
| Convert BMD | `ConvertBmdToGltf.exe input/ output/ --scale 0.01` |

---

## Support Resources

- **Implementation Examples**: `src/source/Render/Models/GltfIntegrationExample.h`
- **Phase 3 Details**: `PHASE_3_COMPLETE.md` (rendering system docs)
- **Phase 4 Details**: `PHASE_4_IMPLEMENTATION_GUIDE.md` (converter docs)
- **Progress**: `GLTF_MIGRATION_PROGRESS.md` (overall status)

---

**Last Updated**: May 29, 2026  
**Version**: 1.0 (Phases 1-4 ready, Phase 5 planned)
