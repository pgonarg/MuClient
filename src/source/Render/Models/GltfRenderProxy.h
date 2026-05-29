#pragma once

#include "GltfLoader.h"
#include "Core/Globals/_types.h"
#include <memory>

// Adapter class that converts glTF data to engine's rendering format
// Allows glTF models to be rendered using the existing BMD rendering pipeline

class GltfRenderProxy {
public:
    GltfRenderProxy(std::unique_ptr<gltf::GltfModel> model);
    ~GltfRenderProxy();

    // Material handling
    void SetupMaterialRenderFlags(int meshIndex);
    uint32_t GetRenderFlagsForMesh(int meshIndex) const;
    int GetMaterialIndex(int meshIndex) const;

    // Bone transformation
    void SetupBoneTransforms(float (*boneMatrices)[3][4], float animationFrame = 0.0f);
    void ApplyBoneWeights(int meshIndex, float (*boneMatrices)[3][4]);

    // Vertex transformation
    void TransformVertices(int meshIndex, float (*boneMatrices)[3][4], vec3_t* outPositions, vec3_t* outNormals);

    // Lighting setup
    void SetupNormalTransforms(int meshIndex, float (*boneMatrices)[3][4]);

    // Quaternion utilities
    static void QuaternionToMatrix(const gltf::Vec4Wrapper& quat, float matrix[3][4]);
    static void MatrixMultiply(const float mat1[3][4], const float mat2[3][4], float result[3][4]);
    static void TransformVec3(const float matrix[3][4], const vec3_t in, vec3_t out);

    // Getters
    int GetMeshCount() const { return model->meshes.size(); }
    int GetBoneCount() const { return model->bones.size(); }
    const gltf::Mesh& GetMesh(int idx) const { return model->meshes[idx]; }
    const gltf::Bone& GetBone(int idx) const { return model->bones[idx]; }
    const gltf::Material& GetMaterial(int idx) const { return model->materials[idx]; }

private:
    std::unique_ptr<gltf::GltfModel> model;
    std::vector<vec3_t> transformedPositions;
    std::vector<vec3_t> transformedNormals;

    // Helper to apply bone weights to a vertex
    void SkinVertex(const gltf::Vertex& vertex, const float (*boneMatrices)[3][4],
                    vec3_t& outPos, vec3_t& outNormal);

    // Convert quaternion rotation to Euler angles (if needed for compatibility)
    void QuaternionToEuler(const gltf::Vec4Wrapper& quat, vec3_t& eulerAngles);
};
