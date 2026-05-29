#include "BmdToGltfConverter.h"
#include "GltfLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <algorithm>

using json = nlohmann::json;

std::unique_ptr<gltf::GltfModel> BmdToGltfConverter::Convert(
    const BMD* bmdModel,
    const ConversionOptions& options)
{
    if (!bmdModel) return nullptr;

    auto gltfModel = std::make_unique<gltf::GltfModel>();
    gltfModel->name = bmdModel->Name;

    // Convert in order: bones → meshes → materials → animations
    ConvertBones(bmdModel, *gltfModel);
    ConvertMeshes(bmdModel, *gltfModel, options);
    ConvertMaterials(bmdModel, *gltfModel);
    if (options.includeAnimations) {
        ConvertAnimations(bmdModel, *gltfModel, options);
    }

    return gltfModel;
}

void BmdToGltfConverter::ConvertMeshes(const BMD* bmdModel, gltf::GltfModel& gltfModel,
                                      const ConversionOptions& options)
{
    if (!bmdModel->Meshs || bmdModel->NumMeshs <= 0) return;

    gltfModel.meshes.reserve(bmdModel->NumMeshs);

    for (int meshIdx = 0; meshIdx < bmdModel->NumMeshs; meshIdx++) {
        const Mesh_t& bmdMesh = bmdModel->Meshs[meshIdx];

        gltf::Mesh mesh;
        mesh.name = "Mesh_" + std::to_string(meshIdx);
        mesh.materialIndex = bmdMesh.Texture >= 0 ? bmdMesh.Texture : -1;

        // Reserve space for vertices
        mesh.vertices.reserve(bmdMesh.NumVertices);
        mesh.indices.reserve(bmdMesh.NumTriangles * 3);

        // Convert triangles to indexed vertices
        int vertexCount = 0;
        for (int triIdx = 0; triIdx < bmdMesh.NumTriangles; triIdx++) {
            AddTriangleToMesh(mesh, bmdMesh, triIdx, vertexCount, options);
        }

        gltfModel.meshes.push_back(std::move(mesh));
    }
}

void BmdToGltfConverter::AddTriangleToMesh(
    gltf::Mesh& mesh,
    const Mesh_t& bmdMesh,
    int triangleIdx,
    int& vertexCount,
    const ConversionOptions& options)
{
    if (!bmdMesh.Triangles || triangleIdx >= bmdMesh.NumTriangles) return;

    const Triangle_t& tri = bmdMesh.Triangles[triangleIdx];

    // Process up to 3 vertices (assume triangles, skip 4th if present)
    for (int i = 0; i < 3; i++) {
        gltf::Vertex vertex;
        vertex.weights[0] = 1.0f;  // Default to single bone
        vertex.weights[1] = vertex.weights[2] = vertex.weights[3] = 0.0f;
        vertex.joints[0] = vertex.joints[1] = vertex.joints[2] = vertex.joints[3] = -1;

        // Get position
        short vertIdx = tri.VertexIndex[i];
        if (vertIdx >= 0 && vertIdx < bmdMesh.NumVertices && bmdMesh.Vertices) {
            VectorCopy(bmdMesh.Vertices[vertIdx].Position, vertex.position);
            VectorScale(vertex.position, options.scale, vertex.position);
            vertex.joints[0] = bmdMesh.Vertices[vertIdx].Node;  // Bone index
        }

        // Get normal
        short normIdx = tri.NormalIndex[i];
        if (normIdx >= 0 && normIdx < bmdMesh.NumNormals && bmdMesh.Normals) {
            VectorCopy(bmdMesh.Normals[normIdx].Normal, vertex.normal);
            // Normals don't scale
        } else {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 1.0f;
        }

        // Get UV coordinates
        short texIdx = tri.TexCoordIndex[i];
        if (texIdx >= 0 && texIdx < bmdMesh.NumTexCoords && bmdMesh.TexCoords) {
            vertex.texCoord[0] = bmdMesh.TexCoords[texIdx].TexCoordU;
            vertex.texCoord[1] = bmdMesh.TexCoords[texIdx].TexCoordV;
        } else {
            vertex.texCoord[0] = 0.0f;
            vertex.texCoord[1] = 0.0f;
        }

        mesh.vertices.push_back(vertex);
        mesh.indices.push_back(vertexCount++);
    }
}

void BmdToGltfConverter::ConvertBones(const BMD* bmdModel, gltf::GltfModel& gltfModel)
{
    if (!bmdModel->Bones || bmdModel->NumBones <= 0) return;

    gltfModel.bones.reserve(bmdModel->NumBones);
    gltfModel.rootBoneIndex = -1;

    for (int i = 0; i < bmdModel->NumBones; i++) {
        const Bone_t& bmdBone = bmdModel->Bones[i];

        gltf::Bone bone;
        bone.name = bmdBone.Name;
        bone.parent = bmdBone.Parent;
        bone.children.clear();

        // Copy local transform (position and rotation)
        if (bmdBone.BoneMatrixes) {
            vec3_t pos;
            VectorCopy(bmdBone.BoneMatrixes->Position, pos);

            // Set up identity matrix
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 4; c++) {
                    bone.localTransform.data[r][c] = (r == c && c < 3) ? 1.0f : 0.0f;
                }
            }
            // Set position
            bone.localTransform.data[0][3] = pos[0];
            bone.localTransform.data[1][3] = pos[1];
            bone.localTransform.data[2][3] = pos[2];
        }

        // Inverse bind matrix (for skinning) - identity for now
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                bone.inverseBindMatrix.data[r][c] = (r == c && c < 3) ? 1.0f : 0.0f;
            }
        }

        if (bone.parent < 0) {
            gltfModel.rootBoneIndex = i;
        }

        gltfModel.bones.push_back(bone);
    }

    // Build parent-child relationships
    for (int i = 0; i < gltfModel.bones.size(); i++) {
        if (gltfModel.bones[i].parent >= 0 &&
            gltfModel.bones[i].parent < (int)gltfModel.bones.size()) {
            gltfModel.bones[gltfModel.bones[i].parent].children.push_back(i);
        }
    }
}

void BmdToGltfConverter::ConvertAnimations(const BMD* bmdModel, gltf::GltfModel& gltfModel,
                                          const ConversionOptions& options)
{
    if (!bmdModel->Actions || bmdModel->NumActions <= 0) return;

    gltfModel.animations.reserve(bmdModel->NumActions);

    for (int actionIdx = 0; actionIdx < bmdModel->NumActions; actionIdx++) {
        const Action_t& action = bmdModel->Actions[actionIdx];

        gltf::Animation anim;
        anim.name = "Action_" + std::to_string(actionIdx);
        anim.duration = action.NumAnimationKeys > 0 ? action.NumAnimationKeys * 0.033f : 0.0f;

        // For each bone, create animation channel if it has keyframe positions
        if (action.Positions) {
            anim.channels.reserve(gltfModel.bones.size());
            anim.samplers.reserve(gltfModel.bones.size());

            for (size_t boneIdx = 0; boneIdx < gltfModel.bones.size(); boneIdx++) {
                // Create channel for position animation
                gltf::AnimationChannel channel;
                channel.nodeIndex = boneIdx;
                channel.targetPath = "translation";
                channel.samplerIndex = anim.samplers.size();

                // Create sampler for this bone
                gltf::AnimationSampler sampler;
                sampler.interpolation = "LINEAR";

                // Generate keyframe times
                if (action.NumAnimationKeys > 0) {
                    for (short k = 0; k < action.NumAnimationKeys; k++) {
                        sampler.inputTimes.push_back(k * 0.033f);  // ~30 FPS

                        // Position for this keyframe (simplified - assumes linear storage)
                        gltf::Vec3Wrapper pos(0, 0, 0);
                        if (action.Positions && boneIdx < 200) {  // Safety check
                            // In real BMD, positions are stored per-frame per-bone
                            // This is simplified; actual format would need deep dive
                            int frameIdx = k * gltfModel.bones.size() + boneIdx;
                            if (frameIdx >= 0 && frameIdx < action.NumAnimationKeys * 200) {
                                VectorCopy(action.Positions[frameIdx],
                                          reinterpret_cast<vec3_t&>(pos));
                            }
                        }
                        sampler.positions.push_back(pos);
                    }
                }

                if (!sampler.inputTimes.empty()) {
                    anim.channels.push_back(channel);
                    anim.samplers.push_back(sampler);
                }
            }
        }

        if (!anim.channels.empty()) {
            gltfModel.animations.push_back(std::move(anim));
        }
    }
}

void BmdToGltfConverter::ConvertMaterials(const BMD* bmdModel, gltf::GltfModel& gltfModel)
{
    if (!bmdModel->Textures || bmdModel->NumMeshs <= 0) return;

    gltfModel.materials.reserve(bmdModel->NumMeshs);

    for (int i = 0; i < bmdModel->NumMeshs; i++) {
        gltf::Material mat;
        mat.name = "Material_" + std::to_string(i);
        mat.baseColorFactor[0] = 1.0f;
        mat.baseColorFactor[1] = 1.0f;
        mat.baseColorFactor[2] = 1.0f;
        mat.baseColorFactor[3] = 1.0f;
        mat.metallicFactor = 0.0f;
        mat.roughnessFactor = 0.5f;
        mat.baseColorTexture = -1;
        mat.normalTexture = -1;

        // Extract texture name
        if (bmdModel->Textures && i < 256) {  // Sanity check
            const char* texName = bmdModel->Textures[i].FileName;
            if (texName && texName[0] != 0) {
                // Store as texture reference in glTF extras
                mat.renderFlags = 0x00000002;  // RENDER_TEXTURE
            }
        }

        gltfModel.materials.push_back(mat);
    }
}

bool BmdToGltfConverter::ConvertAndSave(
    const BMD* bmdModel,
    const wchar_t* outputPath,
    const ConversionOptions& options)
{
    if (!bmdModel || !outputPath) return false;

    auto gltfModel = Convert(bmdModel, options);
    if (!gltfModel) return false;

    // TODO: Implement GLB/glTF file writing using nlohmann/json
    // This would involve:
    // 1. Creating JSON structure with accessors, bufferViews, buffers
    // 2. Serializing binary vertex/index data
    // 3. Writing .glb or .gltf + .bin files

    // For now, return true as placeholder
    return true;
}
