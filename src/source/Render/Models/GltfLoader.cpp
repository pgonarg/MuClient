#include "stdafx.h"
#include "GltfLoader.h"
#include <fstream>
#include <cstring>

namespace gltf {

// Helper: Convert wide string to regular string (ASCII subset)
static std::string WideToString(const wchar_t* wide) {
    if (!wide) return "";
    std::string result;
    while (*wide) {
        result += (char)(*wide);
        wide++;
    }
    return result;
}

// Helper: Get directory from file path
static std::string GetDirectory(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? "" : path.substr(0, pos + 1);
}

// Read binary file
static std::vector<uint8_t> ReadFile(const wchar_t* filePath) {
    std::vector<uint8_t> data;
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return data;

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    data.resize(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// GLB file format: 12-byte header + JSON chunk + binary chunk
std::unique_ptr<GltfModel> GltfLoader::LoadFromGlb(const std::vector<uint8_t>& glbData) {
    if (glbData.size() < 20) return nullptr;  // Minimum GLB size

    auto model = std::make_unique<GltfModel>();

    // Check magic number "glTF"
    uint32_t magic = *reinterpret_cast<const uint32_t*>(glbData.data());
    if (magic != 0x46546c67) return nullptr;  // 0x46546c67 = "glTF" in little-endian

    uint32_t version = *reinterpret_cast<const uint32_t*>(glbData.data() + 4);
    uint32_t fileSize = *reinterpret_cast<const uint32_t*>(glbData.data() + 8);

    if (version != 2) return nullptr;  // Only support glTF 2.0

    // Parse JSON chunk
    uint32_t jsonChunkSize = *reinterpret_cast<const uint32_t*>(glbData.data() + 12);
    uint32_t jsonChunkType = *reinterpret_cast<const uint32_t*>(glbData.data() + 16);

    if (jsonChunkType != 0x4e4f534a) return nullptr;  // "JSON" in little-endian

    std::string jsonStr(reinterpret_cast<const char*>(glbData.data() + 20), jsonChunkSize);

    // Parse binary chunk
    uint32_t binOffset = 20 + jsonChunkSize;
    uint32_t binChunkSize = *reinterpret_cast<const uint32_t*>(glbData.data() + binOffset);

    std::vector<uint8_t> binData(glbData.begin() + binOffset + 8,
                                  glbData.begin() + binOffset + 8 + binChunkSize);

    // Parse JSON
    try {
        auto root = nlohmann::json::parse(jsonStr);

        // Parse accessors, bufferViews, buffers
        std::vector<Accessor> accessors;
        if (root.contains("accessors")) {
            for (const auto& acc : root["accessors"]) {
                Accessor a;
                a.bufferView = acc.value("bufferView", 0);
                a.byteOffset = acc.value("byteOffset", 0);
                a.componentType = acc["componentType"];
                a.count = acc["count"];
                a.type = acc["type"];
                accessors.push_back(a);
            }
        }

        std::vector<BufferView> bufferViews;
        if (root.contains("bufferViews")) {
            for (const auto& bv : root["bufferViews"]) {
                BufferView v;
                v.buffer = bv.value("buffer", 0);
                v.byteOffset = bv.value("byteOffset", 0);
                v.byteLength = bv["byteLength"];
                v.byteStride = bv.value("byteStride", 0);
                v.target = bv.value("target", 0);
                bufferViews.push_back(v);
            }
        }

        // For GLB, there's only one buffer (the binary chunk)
        Buffer buf;
        buf.uri = "";
        buf.byteLength = binChunkSize;
        buf.data = binData;
        std::vector<Buffer> buffers = { buf };

        // Parse meshes
        if (root.contains("meshes")) {
            int meshIdx = 0;
            for (const auto& meshJson : root["meshes"]) {
                Mesh mesh;
                mesh.name = meshJson.value("name", "Mesh" + std::to_string(meshIdx));
                mesh.materialIndex = -1;

                ParseMesh(meshJson, meshIdx, mesh, accessors, bufferViews, buffers);
                model->meshes.push_back(mesh);
                meshIdx++;
            }
        }

        // Parse skins (skeletons)
        if (root.contains("skins")) {
            int skinIdx = 0;
            for (const auto& skinJson : root["skins"]) {
                ParseSkin(skinJson, skinIdx, model->bones, accessors, bufferViews, buffers, root["nodes"]);
                skinIdx++;
            }
        }

        // Parse animations
        if (root.contains("animations")) {
            for (const auto& animJson : root["animations"]) {
                Animation anim;
                anim.name = animJson.value("name", "Animation");
                anim.duration = 0.0f;

                ParseAnimation(animJson, anim, accessors, bufferViews, buffers);
                model->animations.push_back(anim);
            }
        }

        // Parse materials
        if (root.contains("materials")) {
            for (const auto& matJson : root["materials"]) {
                Material mat;
                mat.name = matJson.value("name", "Material");
                ParseMaterial(matJson, mat);
                model->materials.push_back(mat);
            }
        }

        // Parse textures
        if (root.contains("images")) {
            for (const auto& imgJson : root["images"]) {
                TextureInfo tex;
                tex.name = imgJson.value("name", "Texture");
                tex.uri = imgJson.value("uri", "");
                model->textures.push_back(tex);
            }
        }

        return model;
    } catch (const std::exception& e) {
        return nullptr;
    }
}

std::unique_ptr<GltfModel> GltfLoader::LoadFromGltf(const wchar_t* gltfPath) {
    // Load separate .gltf (JSON) and .bin (binary) files
    std::vector<uint8_t> gltfData = ReadFile(gltfPath);
    if (gltfData.empty()) return nullptr;

    auto model = std::make_unique<GltfModel>();

    std::string jsonStr(reinterpret_cast<const char*>(gltfData.data()), gltfData.size());

    try {
        auto root = nlohmann::json::parse(jsonStr);

        // Get directory for loading external .bin file
        std::string gltfDir = GetDirectory(WideToString(gltfPath));

        // Parse accessors and bufferViews
        std::vector<Accessor> accessors;
        if (root.contains("accessors")) {
            for (const auto& acc : root["accessors"]) {
                Accessor a;
                a.bufferView = acc.value("bufferView", 0);
                a.byteOffset = acc.value("byteOffset", 0);
                a.componentType = acc["componentType"];
                a.count = acc["count"];
                a.type = acc["type"];
                accessors.push_back(a);
            }
        }

        std::vector<BufferView> bufferViews;
        if (root.contains("bufferViews")) {
            for (const auto& bv : root["bufferViews"]) {
                BufferView v;
                v.buffer = bv.value("buffer", 0);
                v.byteOffset = bv.value("byteOffset", 0);
                v.byteLength = bv["byteLength"];
                v.byteStride = bv.value("byteStride", 0);
                v.target = bv.value("target", 0);
                bufferViews.push_back(v);
            }
        }

        // Load buffers from external files
        std::vector<Buffer> buffers;
        if (root.contains("buffers")) {
            for (const auto& bufJson : root["buffers"]) {
                Buffer buf;
                buf.uri = bufJson.value("uri", "");
                buf.byteLength = bufJson["byteLength"];

                if (!buf.uri.empty()) {
                    std::string fullPath = gltfDir + buf.uri;
                    // Convert to wide string for ReadFile (ASCII subset)
                    std::wstring widePath;
                    for (char c : fullPath) {
                        widePath += (wchar_t)c;
                    }
                    buf.data = ReadFile(widePath.c_str());
                }

                buffers.push_back(buf);
            }
        }

        // Parse meshes
        if (root.contains("meshes")) {
            int meshIdx = 0;
            for (const auto& meshJson : root["meshes"]) {
                Mesh mesh;
                mesh.name = meshJson.value("name", "Mesh" + std::to_string(meshIdx));
                mesh.materialIndex = -1;

                ParseMesh(meshJson, meshIdx, mesh, accessors, bufferViews, buffers);
                model->meshes.push_back(mesh);
                meshIdx++;
            }
        }

        // Parse skins
        if (root.contains("skins")) {
            int skinIdx = 0;
            for (const auto& skinJson : root["skins"]) {
                ParseSkin(skinJson, skinIdx, model->bones, accessors, bufferViews, buffers, root["nodes"]);
                skinIdx++;
            }
        }

        // Parse animations
        if (root.contains("animations")) {
            for (const auto& animJson : root["animations"]) {
                Animation anim;
                anim.name = animJson.value("name", "Animation");
                anim.duration = 0.0f;

                ParseAnimation(animJson, anim, accessors, bufferViews, buffers);
                model->animations.push_back(anim);
            }
        }

        // Parse materials
        if (root.contains("materials")) {
            for (const auto& matJson : root["materials"]) {
                Material mat;
                mat.name = matJson.value("name", "Material");
                ParseMaterial(matJson, mat);
                model->materials.push_back(mat);
            }
        }

        return model;
    } catch (const std::exception& e) {
        return nullptr;
    }
}

std::unique_ptr<GltfModel> GltfLoader::Load(const wchar_t* filePath) {
    // Check file extension
    std::wstring path(filePath);
    if (path.length() >= 4) {
        std::wstring ext = path.substr(path.length() - 4);
        if (ext == L".glb") {
            std::vector<uint8_t> data = ReadFile(filePath);
            return LoadFromGlb(data);
        } else if (ext == L".gltf") {
            return LoadFromGltf(filePath);
        }
    }
    return nullptr;
}

// Stub implementations for parser helpers (to be completed in next phase)
void GltfLoader::ParseAccessor(const nlohmann::json& accessorJson, Accessor& accessor) {
    // Already handled in Load functions above
}

void GltfLoader::ParseBufferView(const nlohmann::json& bvJson, BufferView& bv) {
    // Already handled in Load functions above
}

void GltfLoader::ParseMesh(const nlohmann::json& meshJson, int meshIndex, Mesh& mesh,
                           const std::vector<Accessor>& accessors,
                           const std::vector<BufferView>& bufferViews,
                           const std::vector<Buffer>& buffers) {
    if (!meshJson.contains("primitives")) return;

    // For now, merge all primitives into one mesh
    // TODO: Handle multiple primitives with different materials

    for (const auto& prim : meshJson["primitives"]) {
        if (prim.contains("material")) {
            mesh.materialIndex = prim["material"];
        }

        const auto& attributes = prim.value("attributes", nlohmann::json::object());

        // Get position data (required)
        if (!attributes.contains("POSITION")) continue;

        int posAccessorIdx = attributes["POSITION"];
        if (posAccessorIdx >= accessors.size()) continue;

        const Accessor& posAcc = accessors[posAccessorIdx];
        const BufferView& posBv = bufferViews[posAcc.bufferView];
        const Buffer& posBuf = buffers[posBv.buffer];

        uint8_t* posData = const_cast<uint8_t*>(posBuf.data.data()) + posBv.byteOffset + posAcc.byteOffset;
        int posStride = posBv.byteStride ? posBv.byteStride : 12;

        // Create vertices with positions
        mesh.vertices.resize(posAcc.count);
        for (int i = 0; i < posAcc.count; i++) {
            ReadVec3(mesh.vertices[i].position, posData, posAcc.componentType, posStride);
            posData += posStride;
        }

        // Extract normal data if available
        if (attributes.contains("NORMAL")) {
            int normAccessorIdx = attributes["NORMAL"];
            if (normAccessorIdx < accessors.size()) {
                const Accessor& normAcc = accessors[normAccessorIdx];
                const BufferView& normBv = bufferViews[normAcc.bufferView];
                const Buffer& normBuf = buffers[normBv.buffer];

                uint8_t* normData = const_cast<uint8_t*>(normBuf.data.data()) + normBv.byteOffset + normAcc.byteOffset;
                int normStride = normBv.byteStride ? normBv.byteStride : 12;

                for (int i = 0; i < normAcc.count && i < mesh.vertices.size(); i++) {
                    ReadVec3(mesh.vertices[i].normal, normData, normAcc.componentType, normStride);
                    normData += normStride;
                }
            }
        }

        // Extract texcoord data if available
        if (attributes.contains("TEXCOORD_0")) {
            int texAccessorIdx = attributes["TEXCOORD_0"];
            if (texAccessorIdx < accessors.size()) {
                const Accessor& texAcc = accessors[texAccessorIdx];
                const BufferView& texBv = bufferViews[texAcc.bufferView];
                const Buffer& texBuf = buffers[texBv.buffer];

                uint8_t* texData = const_cast<uint8_t*>(texBuf.data.data()) + texBv.byteOffset + texAcc.byteOffset;
                int texStride = texBv.byteStride ? texBv.byteStride : 8;

                for (int i = 0; i < texAcc.count && i < mesh.vertices.size(); i++) {
                    // Read 2D UV coordinates
                    if (texAcc.componentType == 5126) {  // FLOAT
                        mesh.vertices[i].texCoord[0] = *reinterpret_cast<const float*>(texData);
                        mesh.vertices[i].texCoord[1] = *reinterpret_cast<const float*>(texData + 4);
                    }
                    texData += texStride;
                }
            }
        }

        // Extract index data
        if (prim.contains("indices")) {
            int idxAccessorIdx = prim["indices"];
            if (idxAccessorIdx < accessors.size()) {
                const Accessor& idxAcc = accessors[idxAccessorIdx];
                const BufferView& idxBv = bufferViews[idxAcc.bufferView];
                const Buffer& idxBuf = buffers[idxBv.buffer];

                uint8_t* idxData = const_cast<uint8_t*>(idxBuf.data.data()) + idxBv.byteOffset + idxAcc.byteOffset;

                int stride = (idxAcc.componentType == 5125) ? 4 : (idxAcc.componentType == 5123 ? 2 : 1);

                for (int i = 0; i < idxAcc.count; i++) {
                    uint32_t idx = ReadUint32(idxData, 0);
                    mesh.indices.push_back(idx);
                    idxData += stride;
                }
            }
        }
    }
}

void GltfLoader::ParseSkin(const nlohmann::json& skinJson, int skinIndex,
                           std::vector<Bone>& bones,
                           const std::vector<Accessor>& accessors,
                           const std::vector<BufferView>& bufferViews,
                           const std::vector<Buffer>& buffers,
                           const nlohmann::json& nodesJson) {
    if (!skinJson.contains("joints")) return;

    auto joints = skinJson["joints"];
    std::vector<int> jointIndices;
    for (const auto& joint : joints) {
        jointIndices.push_back(joint);
    }

    // Read inverse bind matrices
    std::vector<Mat34Wrapper> inverseBindMatrices;
    if (skinJson.contains("inverseBindMatrices")) {
        int ibmAccessorIdx = skinJson["inverseBindMatrices"];
        if (ibmAccessorIdx < accessors.size()) {
            const Accessor& ibmAcc = accessors[ibmAccessorIdx];
            const BufferView& bv = bufferViews[ibmAcc.bufferView];
            const Buffer& buf = buffers[bv.buffer];

            uint8_t* data = const_cast<uint8_t*>(buf.data.data()) + bv.byteOffset + ibmAcc.byteOffset;
            int stride = bv.byteStride ? bv.byteStride : 64;  // 4x4 matrix = 64 bytes

            for (int i = 0; i < ibmAcc.count && i < jointIndices.size(); i++) {
                Mat34Wrapper mat;
                ReadMat4(mat.data, data, ibmAcc.componentType, stride);
                inverseBindMatrices.push_back(mat);
                data += stride;
            }
        }
    }

    // Create bones from joint nodes
    bones.clear();
    bones.resize(jointIndices.size());

    for (size_t i = 0; i < jointIndices.size(); i++) {
        int nodeIdx = jointIndices[i];
        if (nodeIdx >= nodesJson.size()) continue;

        const auto& nodeJson = nodesJson[nodeIdx];
        Bone& bone = bones[i];

        bone.name = nodeJson.value("name", "Joint" + std::to_string(i));
        bone.parent = -1;  // Will be set later

        // Get local transform from node
        if (nodeJson.contains("translation")) {
            auto trans = nodeJson["translation"];
            bone.localTransform.data[0][3] = trans[0];
            bone.localTransform.data[1][3] = trans[1];
            bone.localTransform.data[2][3] = trans[2];
        } else {
            bone.localTransform.data[0][3] = 0;
            bone.localTransform.data[1][3] = 0;
            bone.localTransform.data[2][3] = 0;
        }

        if (nodeJson.contains("rotation")) {
            // TODO: Convert quaternion to rotation matrix
            // For now, identity rotation
            bone.localTransform.data[0][0] = 1; bone.localTransform.data[0][1] = 0; bone.localTransform.data[0][2] = 0;
            bone.localTransform.data[1][0] = 0; bone.localTransform.data[1][1] = 1; bone.localTransform.data[1][2] = 0;
            bone.localTransform.data[2][0] = 0; bone.localTransform.data[2][1] = 0; bone.localTransform.data[2][2] = 1;
        } else if (nodeJson.contains("matrix")) {
            // Use matrix directly if provided
            // TODO: Extract 3x4 from 4x4 matrix
        } else {
            // Identity rotation
            bone.localTransform.data[0][0] = 1; bone.localTransform.data[0][1] = 0; bone.localTransform.data[0][2] = 0;
            bone.localTransform.data[1][0] = 0; bone.localTransform.data[1][1] = 1; bone.localTransform.data[1][2] = 0;
            bone.localTransform.data[2][0] = 0; bone.localTransform.data[2][1] = 0; bone.localTransform.data[2][2] = 1;
        }

        // Set inverse bind matrix
        if (i < inverseBindMatrices.size()) {
            bone.inverseBindMatrix = inverseBindMatrices[i];
        }
    }

    // Set up parent-child relationships by traversing node hierarchy
    for (size_t i = 0; i < jointIndices.size(); i++) {
        int nodeIdx = jointIndices[i];
        if (nodeIdx >= nodesJson.size()) continue;

        const auto& nodeJson = nodesJson[nodeIdx];
        if (nodeJson.contains("children")) {
            for (const auto& childNodeIdx : nodeJson["children"]) {
                // Find this child in our joint list
                for (size_t j = 0; j < jointIndices.size(); j++) {
                    if (jointIndices[j] == childNodeIdx) {
                        bones[j].parent = i;
                        bones[i].children.push_back(j);
                        break;
                    }
                }
            }
        }
    }
}

void GltfLoader::ParseAnimation(const nlohmann::json& animJson, Animation& anim,
                               const std::vector<Accessor>& accessors,
                               const std::vector<BufferView>& bufferViews,
                               const std::vector<Buffer>& buffers) {
    if (!animJson.contains("channels") || !animJson.contains("samplers")) return;

    auto channels = animJson["channels"];
    auto samplers = animJson["samplers"];

    // Parse samplers (input times and output values)
    anim.samplers.resize(samplers.size());
    for (size_t i = 0; i < samplers.size(); i++) {
        const auto& samplerJson = samplers[i];
        AnimationSampler& sampler = anim.samplers[i];

        sampler.interpolation = samplerJson.value("interpolation", "LINEAR");

        // Read input times
        if (samplerJson.contains("input")) {
            int inputAccessorIdx = samplerJson["input"];
            if (inputAccessorIdx < accessors.size()) {
                const Accessor& inputAcc = accessors[inputAccessorIdx];
                const BufferView& bv = bufferViews[inputAcc.bufferView];
                const Buffer& buf = buffers[bv.buffer];

                uint8_t* data = const_cast<uint8_t*>(buf.data.data()) + bv.byteOffset + inputAcc.byteOffset;

                for (int j = 0; j < inputAcc.count; j++) {
                    sampler.inputTimes.push_back(ReadFloat(data, 0));
                    data += 4;  // float = 4 bytes
                }

                // Update animation duration
                if (!sampler.inputTimes.empty()) {
                    anim.duration = std::max(anim.duration, sampler.inputTimes.back());
                }
            }
        }

        // Read output values
        if (samplerJson.contains("output")) {
            int outputAccessorIdx = samplerJson["output"];
            if (outputAccessorIdx < accessors.size()) {
                const Accessor& outputAcc = accessors[outputAccessorIdx];
                const BufferView& bv = bufferViews[outputAcc.bufferView];
                const Buffer& buf = buffers[bv.buffer];

                uint8_t* data = const_cast<uint8_t*>(buf.data.data()) + bv.byteOffset + outputAcc.byteOffset;

                // Determine output type from accessor type
                if (outputAcc.type == "VEC3") {
                    // Translation or scale
                    for (int j = 0; j < outputAcc.count; j++) {
                        Vec3Wrapper vec;
                        vec.x = ReadFloat(data, 0);
                        vec.y = ReadFloat(data, 4);
                        vec.z = ReadFloat(data, 8);
                        sampler.positions.push_back(vec);
                        data += 12;
                    }
                } else if (outputAcc.type == "VEC4") {
                    // Quaternion rotation
                    for (int j = 0; j < outputAcc.count; j++) {
                        Vec4Wrapper quat;
                        quat.x = ReadFloat(data, 0);
                        quat.y = ReadFloat(data, 4);
                        quat.z = ReadFloat(data, 8);
                        quat.w = ReadFloat(data, 12);
                        sampler.quaternions.push_back(quat);
                        data += 16;
                    }
                }
            }
        }
    }

    // Parse channels (connect samplers to bone targets)
    for (const auto& channelJson : channels) {
        AnimationChannel channel;

        if (channelJson.contains("target")) {
            const auto& target = channelJson["target"];
            channel.nodeIndex = target.value("node", 0);
            channel.targetPath = target.value("path", "translation");
        }

        channel.samplerIndex = channelJson.value("sampler", 0);
        anim.channels.push_back(channel);
    }
}

void GltfLoader::ParseMaterial(const nlohmann::json& matJson, Material& mat) {
    mat.baseColorFactor[0] = mat.baseColorFactor[1] = mat.baseColorFactor[2] = 1.0f;
    mat.baseColorFactor[3] = 1.0f;
    mat.baseColorTexture = -1;
    mat.normalTexture = -1;
    mat.metallicFactor = 0.0f;
    mat.roughnessFactor = 1.0f;
    mat.renderFlags = 0x00000002;  // RENDER_TEXTURE default
    mat.chromeType = 0;
    mat.lightMapIndex = -1;

    // Parse PBR metallic/roughness
    if (matJson.contains("pbrMetallicRoughness")) {
        const auto& pbr = matJson["pbrMetallicRoughness"];
        mat.metallicFactor = pbr.value("metallicFactor", 0.0f);
        mat.roughnessFactor = pbr.value("roughnessFactor", 1.0f);

        if (pbr.contains("baseColorFactor")) {
            auto bc = pbr["baseColorFactor"];
            mat.baseColorFactor[0] = bc[0];
            mat.baseColorFactor[1] = bc[1];
            mat.baseColorFactor[2] = bc[2];
            mat.baseColorFactor[3] = bc[3];
        }

        if (pbr.contains("baseColorTexture")) {
            mat.baseColorTexture = pbr["baseColorTexture"]["index"];
        }
    }

    // Parse engine metadata from extras
    if (matJson.contains("extras")) {
        const auto& extras = matJson["extras"];
        mat.renderFlags = extras.value("renderFlags", 0x00000002);
        mat.chromeType = extras.value("chromeType", 0);
        mat.lightMapIndex = extras.value("lightMapIndex", -1);
    }
}

void GltfLoader::ReadVec3(vec3_t out, const uint8_t* data, int componentType, int stride) {
    if (componentType == 5126) {  // FLOAT
        out[0] = *reinterpret_cast<const float*>(data);
        out[1] = *reinterpret_cast<const float*>(data + 4);
        out[2] = *reinterpret_cast<const float*>(data + 8);
    }
}

void GltfLoader::ReadVec4(vec4_t out, const uint8_t* data, int componentType, int stride) {
    if (componentType == 5126) {  // FLOAT
        out[0] = *reinterpret_cast<const float*>(data);
        out[1] = *reinterpret_cast<const float*>(data + 4);
        out[2] = *reinterpret_cast<const float*>(data + 8);
        out[3] = *reinterpret_cast<const float*>(data + 12);
    }
}

void GltfLoader::ReadMat4(vec34_t out, const uint8_t* data, int componentType, int stride) {
    if (componentType == 5126) {  // FLOAT
        float* fdata = (float*)data;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                out[i][j] = fdata[i * 4 + j];
            }
        }
    }
}

uint32_t GltfLoader::ReadUint32(const uint8_t* data, int offset) {
    return *reinterpret_cast<const uint32_t*>(data + offset);
}

float GltfLoader::ReadFloat(const uint8_t* data, int offset) {
    return *reinterpret_cast<const float*>(data + offset);
}

}  // namespace gltf
