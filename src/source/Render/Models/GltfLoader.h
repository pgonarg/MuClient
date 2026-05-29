#pragma once

#include <vector>
#include <string>
#include <memory>
#include "Core/Globals/_types.h"
#include <json.hpp>

namespace gltf {

struct Accessor {
    int bufferView;
    int byteOffset;
    int componentType;  // 5120-5125 (GL types)
    int count;
    std::string type;   // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
    std::vector<float> min, max;
};

struct BufferView {
    int buffer;
    int byteOffset;
    int byteLength;
    int byteStride;  // 0 means tightly packed
    int target;      // 34962 (ARRAY_BUFFER) or 34963 (ELEMENT_ARRAY_BUFFER)
};

struct Buffer {
    std::string uri;  // Data URI or relative path
    int byteLength;
    std::vector<uint8_t> data;
};

struct Vertex {
    vec3_t position;        // float[3]
    vec3_t normal;
    vec2_t texCoord;        // float[2]
    int joints[4];          // Bone indices
    float weights[4];       // Bone weights
};

struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int materialIndex;      // -1 if no material
};

struct Mat34Wrapper {
    float data[3][4];
    Mat34Wrapper() {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                data[i][j] = (i == j) ? 1.0f : 0.0f;  // Identity
            }
        }
    }
};

struct Bone {
    std::string name;
    int parent;             // -1 for root
    Mat34Wrapper localTransform;
    Mat34Wrapper inverseBindMatrix;
    std::vector<int> children;
};

struct AnimationChannel {
    int nodeIndex;
    std::string targetPath;  // "translation", "rotation", "scale"
    int samplerIndex;
};

struct Vec3Wrapper {
    float x, y, z;
    Vec3Wrapper() : x(0), y(0), z(0) {}
    Vec3Wrapper(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Vec4Wrapper {
    float x, y, z, w;
    Vec4Wrapper() : x(0), y(0), z(0), w(1) {}
    Vec4Wrapper(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct AnimationSampler {
    std::vector<float> inputTimes;      // Keyframe times
    std::vector<Vec4Wrapper> quaternions;  // For rotation
    std::vector<Vec3Wrapper> positions;    // For translation
    std::vector<Vec3Wrapper> scales;       // For scale
    std::string interpolation;  // "LINEAR", "STEP", "CUBICSPLINE"
};

struct Animation {
    std::string name;
    std::vector<AnimationChannel> channels;
    std::vector<AnimationSampler> samplers;
    float duration;
};

struct Material {
    std::string name;
    vec4_t baseColorFactor;     // float[4]
    int baseColorTexture;       // -1 if none
    float metallicFactor;
    float roughnessFactor;
    int normalTexture;          // -1 if none

    // Engine-specific metadata (from glTF extras)
    uint32_t renderFlags;
    int chromeType;
    int lightMapIndex;
};

struct TextureInfo {
    std::string uri;
    std::string name;
};

class GltfModel {
public:
    std::string name;
    std::vector<Mesh> meshes;
    std::vector<Bone> bones;
    std::vector<Animation> animations;
    std::vector<Material> materials;
    std::vector<TextureInfo> textures;

    int rootBoneIndex;  // Index of root bone in skeleton

    GltfModel() : rootBoneIndex(-1) {}
};

class GltfLoader {
public:
    // Load from .glb (binary) or .gltf (JSON + external bin) file
    static std::unique_ptr<GltfModel> Load(const wchar_t* filePath);

    // Load from .glb binary data
    static std::unique_ptr<GltfModel> LoadFromGlb(const std::vector<uint8_t>& glbData);

    // Load from .gltf JSON and associated .bin file
    static std::unique_ptr<GltfModel> LoadFromGltf(const wchar_t* gltfPath);

private:
    // Helper methods for parsing
    static void ParseAccessor(const nlohmann::json& accessorJson, Accessor& accessor);
    static void ParseBufferView(const nlohmann::json& bvJson, BufferView& bv);
    static void ParseMesh(const nlohmann::json& meshJson, int meshIndex, Mesh& mesh,
                          const std::vector<Accessor>& accessors,
                          const std::vector<BufferView>& bufferViews,
                          const std::vector<Buffer>& buffers);
    static void ParseSkin(const nlohmann::json& skinJson, int skinIndex,
                          std::vector<Bone>& bones,
                          const std::vector<Accessor>& accessors,
                          const std::vector<BufferView>& bufferViews,
                          const std::vector<Buffer>& buffers,
                          const nlohmann::json& nodesJson);
    static void ParseAnimation(const nlohmann::json& animJson, Animation& anim,
                               const std::vector<Accessor>& accessors,
                               const std::vector<BufferView>& bufferViews,
                               const std::vector<Buffer>& buffers);
    static void ParseMaterial(const nlohmann::json& matJson, Material& mat);

    // Utility methods
    static void ReadVec3(vec3_t out, const uint8_t* data, int componentType, int stride);
    static void ReadVec4(vec4_t out, const uint8_t* data, int componentType, int stride);
    static void ReadMat4(vec34_t out, const uint8_t* data, int componentType, int stride);
    static uint32_t ReadUint32(const uint8_t* data, int offset);
    static float ReadFloat(const uint8_t* data, int offset);
};

}  // namespace gltf
