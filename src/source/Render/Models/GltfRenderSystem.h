#pragma once

#include "GltfRenderProxy.h"
#include "Core/Globals/_types.h"
#include <memory>
#include <unordered_map>
#include <string>

// High-level rendering system for glTF models in the game
// Manages loading, animation, and rendering of glTF assets

class GltfRenderSystem {
public:
    static GltfRenderSystem& GetInstance();

    // Load a glTF model from file (cached)
    GltfRenderProxy* LoadModel(const wchar_t* filePath);

    // Render a loaded glTF model
    // position: world position
    // rotation: rotation angles (pitch, yaw, roll)
    // scale: model scale
    // animationIndex: which animation to play (-1 = none)
    // animationFrame: current frame of animation (0.0-1.0)
    // alpha: transparency (0.0-1.0)
    void RenderModel(GltfRenderProxy* model,
                     const vec3_t position,
                     const vec3_t rotation,
                     float scale = 1.0f,
                     int animationIndex = -1,
                     float animationFrame = 0.0f,
                     float alpha = 1.0f,
                     const vec3_t bodyLight = nullptr);

    // Unload a model from cache
    void UnloadModel(const wchar_t* filePath);

    // Clear all cached models
    void ClearCache();

    ~GltfRenderSystem();

private:
    GltfRenderSystem() = default;

    struct CachedModel {
        std::unique_ptr<GltfRenderProxy> proxy;
        int refCount = 1;
    };

    std::unordered_map<std::string, CachedModel> modelCache;

    // Helper: Convert wide string path to std::string for caching
    std::string WideToString(const wchar_t* wstr);

    // Helper: Render a single mesh with transformation
    void RenderMeshWithTransform(GltfRenderProxy* model, int meshIndex,
                                  float (*boneMatrices)[3][4],
                                  const vec3_t position,
                                  const vec3_t rotation,
                                  float scale,
                                  float alpha,
                                  const vec3_t bodyLight);
};
