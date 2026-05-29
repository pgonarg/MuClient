#pragma once

#include "GltfLoader.h"
#include "Render/Models/ZzzBMD.h"
#include <memory>
#include <string>

// Converts BMD (proprietary binary) models to glTF 2.0 format
// Preserves geometry, skeleton, animations, and material metadata

class BmdToGltfConverter {
public:
    struct ConversionOptions {
        bool includeAnimations = true;
        bool includeLightmaps = false;
        bool singleFile = true;  // .glb vs .gltf + .bin
        bool embedTextures = false;  // Embed texture data or link by path
        float scale = 1.0f;  // Scale factor for geometry
    };

    // Convert a BMD model to glTF
    // Returns null on failure
    static std::unique_ptr<gltf::GltfModel> Convert(
        const BMD* bmdModel,
        const ConversionOptions& options = ConversionOptions());

    // Convert and write to file
    static bool ConvertAndSave(
        const BMD* bmdModel,
        const wchar_t* outputPath,
        const ConversionOptions& options = ConversionOptions());

private:
    static void ConvertMeshes(const BMD* bmdModel, gltf::GltfModel& gltfModel,
                              const ConversionOptions& options);

    static void ConvertBones(const BMD* bmdModel, gltf::GltfModel& gltfModel);

    static void ConvertAnimations(const BMD* bmdModel, gltf::GltfModel& gltfModel,
                                 const ConversionOptions& options);

    static void ConvertMaterials(const BMD* bmdModel, gltf::GltfModel& gltfModel);

    // Utility: Convert BMD triangle to glTF indices and vertices
    static void AddTriangleToMesh(
        gltf::Mesh& mesh,
        const Mesh_t& bmdMesh,
        int triangleIdx,
        int& vertexCount,
        const ConversionOptions& options);

    // Utility: Build bone hierarchy from BMD
    static int BuildBoneHierarchy(const BMD* bmdModel, gltf::GltfModel& gltfModel);
};
