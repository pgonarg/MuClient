#include "GltfRenderSystem.h"
#include "ModelLoader.h"
#include "Core/Globals/_functions.h"
#include <algorithm>
#include <codecvt>
#include <locale>

GltfRenderSystem& GltfRenderSystem::GetInstance()
{
    static GltfRenderSystem instance;
    return instance;
}

std::string GltfRenderSystem::WideToString(const wchar_t* wstr)
{
    if (!wstr) return "";

    std::wstring wide(wstr);
    std::string result(wide.begin(), wide.end());

    // Convert to lowercase for consistent caching
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

GltfRenderProxy* GltfRenderSystem::LoadModel(const wchar_t* filePath)
{
    if (!filePath) return nullptr;

    std::string cacheKey = WideToString(filePath);

    // Check if already loaded
    auto it = modelCache.find(cacheKey);
    if (it != modelCache.end()) {
        it->second.refCount++;
        return it->second.proxy.get();
    }

    // Load model using ModelLoader
    auto model = ModelLoader::Load(filePath);
    if (!model) return nullptr;

    // Extract glTF model if it's a glTF wrapper
    std::unique_ptr<gltf::GltfModel> gltfModel;
    if (auto* gltfWrapper = dynamic_cast<GltfModelWrapper*>(model.get())) {
        // Get the underlying glTF model
        // Note: This requires exposing the model from the wrapper
        // For now, we'll create a new loader call
    }

    // Use GltfLoader directly for now
    auto gltfProxy = std::make_unique<GltfRenderProxy>(GltfLoader::Load(filePath));
    if (!gltfProxy || gltfProxy->GetMeshCount() == 0) {
        return nullptr;
    }

    CachedModel cached;
    cached.proxy = std::move(gltfProxy);
    cached.refCount = 1;

    GltfRenderProxy* result = cached.proxy.get();
    modelCache[cacheKey] = std::move(cached);

    return result;
}

void GltfRenderSystem::UnloadModel(const wchar_t* filePath)
{
    if (!filePath) return;

    std::string cacheKey = WideToString(filePath);
    auto it = modelCache.find(cacheKey);
    if (it != modelCache.end()) {
        it->second.refCount--;
        if (it->second.refCount <= 0) {
            modelCache.erase(it);
        }
    }
}

void GltfRenderSystem::ClearCache()
{
    modelCache.clear();
}

void GltfRenderSystem::RenderMeshWithTransform(GltfRenderProxy* model, int meshIndex,
                                               float (*boneMatrices)[3][4],
                                               const vec3_t position,
                                               const vec3_t rotation,
                                               float scale,
                                               float alpha,
                                               const vec3_t bodyLight)
{
    if (!model || meshIndex < 0 || meshIndex >= model->GetMeshCount()) {
        return;
    }

    // TODO: Transform vertices using boneMatrices
    // TODO: Bind vertex/normal data to GPU buffers
    // TODO: Set material render flags
    // TODO: Call OpenGL rendering functions

    // For now, this is a placeholder. The actual implementation would:
    // 1. Get transformed vertex positions and normals from model
    // 2. Create vertex buffers for this frame
    // 3. Set shader uniforms for transformation matrices
    // 4. Draw the mesh with appropriate render flags
}

void GltfRenderSystem::RenderModel(GltfRenderProxy* model,
                                  const vec3_t position,
                                  const vec3_t rotation,
                                  float scale,
                                  int animationIndex,
                                  float animationFrame,
                                  float alpha,
                                  const vec3_t bodyLight)
{
    if (!model || model->GetMeshCount() == 0) {
        return;
    }

    // Setup bone transforms
    float boneMatrices[200][3][4];
    model->SetupBoneTransforms(boneMatrices, animationFrame);

    // Render each mesh
    for (int i = 0; i < model->GetMeshCount(); i++) {
        uint32_t renderFlags = model->GetRenderFlagsForMesh(i);

        RenderMeshWithTransform(model, i, boneMatrices,
                                position, rotation, scale,
                                alpha, bodyLight);
    }
}

GltfRenderSystem::~GltfRenderSystem()
{
    ClearCache();
}
