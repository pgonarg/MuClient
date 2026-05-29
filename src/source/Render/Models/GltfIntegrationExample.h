#pragma once

// Example: How to load and render a glTF model in the game
//
// Usage:
//   GltfRenderProxy* model = GltfRenderSystem::GetInstance().LoadModel(L"path/to/model.glb");
//
//   // In render loop:
//   vec3_t position = {0, 0, 0};
//   vec3_t rotation = {0, 0, 0};
//   vec3_t bodyLight = {1, 1, 1};
//   GltfRenderSystem::GetInstance().RenderModel(model, position, rotation,
//                                                1.0f, -1, 0.0f, 1.0f, bodyLight);
//
//   // On cleanup:
//   GltfRenderSystem::GetInstance().UnloadModel(L"path/to/model.glb");

#include "GltfRenderSystem.h"
#include "GltfRenderProxy.h"
#include "ModelLoader.h"

// Direct usage example (if you have access to a glTF model):
inline void ExampleLoadGltfModel()
{
    // Load a glTF model
    const wchar_t* modelPath = L"Data/Models/character.glb";

    // Method 1: Using GltfRenderSystem (recommended for in-game use)
    GltfRenderProxy* model = GltfRenderSystem::GetInstance().LoadModel(modelPath);

    if (!model) {
        // Failed to load model
        return;
    }

    // Print model info
    int meshCount = model->GetMeshCount();
    int boneCount = model->GetBoneCount();
    // printf("Loaded glTF model: %d meshes, %d bones\n", meshCount, boneCount);

    // Method 2: Direct loading for testing
    auto gltfModel = GltfLoader::Load(modelPath);
    if (!gltfModel) {
        return;
    }

    auto renderProxy = std::make_unique<GltfRenderProxy>(std::move(gltfModel));

    // Setup bone transforms (identity for now, or from animation)
    float boneMatrices[200][3][4];
    renderProxy->SetupBoneTransforms(boneMatrices, 0.0f);  // Frame 0

    // Transform vertices for a specific mesh
    vec3_t positions[5000];  // Preallocate for up to 5000 vertices
    vec3_t normals[5000];

    if (renderProxy->GetMeshCount() > 0) {
        // renderProxy->TransformVertices(0, boneMatrices, positions, normals);
        // Now you have transformed vertex positions and normals
        // Can pass to shader for rendering
    }

    // For each mesh in the model:
    for (int i = 0; i < renderProxy->GetMeshCount(); i++) {
        uint32_t renderFlags = renderProxy->GetRenderFlagsForMesh(i);
        int materialIdx = renderProxy->GetMaterialIndex(i);

        // This is where you would:
        // 1. Bind vertex/normal data to GPU
        // 2. Set material properties
        // 3. Draw the mesh
        // (Implementation depends on your rendering backend)
    }
}

// Integration with CHARACTER/OBJECT system:
//
// To render a glTF model for a CHARACTER:
//
//   CHARACTER* ch = GetCharacter(...);
//   GltfRenderProxy* model = GltfRenderSystem::GetInstance().LoadModel(modelPath);
//
//   // In character rendering function:
//   vec3_t pos = ch->Object.Position;
//   vec3_t angle = ch->Object.Angle;
//   GltfRenderSystem::GetInstance().RenderModel(model, pos, angle,
//       ch->Object.Scale, -1, ch->Object.AnimationFrame, 1.0f, ch->Light);
