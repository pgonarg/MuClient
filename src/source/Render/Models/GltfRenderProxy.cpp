#include "GltfRenderProxy.h"
#include <cmath>
#include <cstring>
#include "Core/Globals/_functions.h"

GltfRenderProxy::GltfRenderProxy(std::unique_ptr<gltf::GltfModel> m)
    : model(std::move(m))
{
    transformedPositions.resize(GetMeshCount() * 1000);  // Preallocate space
    transformedNormals.resize(GetMeshCount() * 1000);
}

GltfRenderProxy::~GltfRenderProxy() = default;

uint32_t GltfRenderProxy::GetRenderFlagsForMesh(int meshIndex) const
{
    if (meshIndex < 0 || meshIndex >= (int)model->meshes.size())
        return RENDER_TEXTURE;

    int matIndex = model->meshes[meshIndex].materialIndex;
    if (matIndex < 0 || matIndex >= (int)model->materials.size())
        return RENDER_TEXTURE;

    const gltf::Material& mat = model->materials[matIndex];

    // Use render flags from glTF metadata if available
    if (mat.renderFlags != 0)
        return mat.renderFlags;

    // Default based on material properties
    uint32_t flags = RENDER_TEXTURE;

    if (mat.metallicFactor > 0.5f)
        flags |= RENDER_CHROME;

    if (mat.baseColorTexture >= 0)
        flags |= RENDER_TEXTURE;

    return flags;
}

int GltfRenderProxy::GetMaterialIndex(int meshIndex) const
{
    if (meshIndex < 0 || meshIndex >= (int)model->meshes.size())
        return -1;
    return model->meshes[meshIndex].materialIndex;
}

void GltfRenderProxy::QuaternionToMatrix(const gltf::Vec4Wrapper& quat, float matrix[3][4])
{
    // Normalize quaternion
    float qx = quat.x, qy = quat.y, qz = quat.z, qw = quat.w;
    float len = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
    if (len > 0.0001f) {
        qx /= len; qy /= len; qz /= len; qw /= len;
    }

    // Convert quaternion to rotation matrix
    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float xw = qx * qw, yw = qy * qw, zw = qz * qw;

    matrix[0][0] = 1.0f - 2.0f * (yy + zz);
    matrix[0][1] = 2.0f * (xy - zw);
    matrix[0][2] = 2.0f * (xz + yw);
    matrix[0][3] = 0.0f;

    matrix[1][0] = 2.0f * (xy + zw);
    matrix[1][1] = 1.0f - 2.0f * (xx + zz);
    matrix[1][2] = 2.0f * (yz - xw);
    matrix[1][3] = 0.0f;

    matrix[2][0] = 2.0f * (xz - yw);
    matrix[2][1] = 2.0f * (yz + xw);
    matrix[2][2] = 1.0f - 2.0f * (xx + yy);
    matrix[2][3] = 0.0f;
}

void GltfRenderProxy::MatrixMultiply(const float mat1[3][4], const float mat2[3][4], float result[3][4])
{
    // Multiply two 3x4 matrices (treating as 3x3 rotation + 3x1 translation)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            result[i][j] = 0.0f;
            for (int k = 0; k < 3; k++) {
                result[i][j] += mat1[i][k] * mat2[k][j];
            }
            if (j == 3) {
                // Add translation from mat1
                result[i][j] += mat1[i][3];
            }
        }
    }
}

void GltfRenderProxy::TransformVec3(const float matrix[3][4], const vec3_t in, vec3_t out)
{
    out[0] = matrix[0][0] * in[0] + matrix[0][1] * in[1] + matrix[0][2] * in[2] + matrix[0][3];
    out[1] = matrix[1][0] * in[0] + matrix[1][1] * in[1] + matrix[1][2] * in[2] + matrix[1][3];
    out[2] = matrix[2][0] * in[0] + matrix[2][1] * in[1] + matrix[2][2] * in[2] + matrix[2][3];
}

void GltfRenderProxy::SetupBoneTransforms(float (*boneMatrices)[3][4], float animationFrame)
{
    // Initialize all bone matrices to identity
    for (int i = 0; i < (int)model->bones.size(); i++) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                boneMatrices[i][r][c] = (r == c && c < 3) ? 1.0f : 0.0f;
            }
        }
    }

    // Apply local transforms
    for (int i = 0; i < (int)model->bones.size(); i++) {
        const gltf::Bone& bone = model->bones[i];

        // Use local transform from glTF
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                boneMatrices[i][r][c] = bone.localTransform.data[r][c];
            }
        }

        // Apply parent transform if not root
        if (bone.parent >= 0 && bone.parent < (int)model->bones.size()) {
            float combined[3][4];
            MatrixMultiply(boneMatrices[bone.parent], boneMatrices[i], combined);
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 4; c++) {
                    boneMatrices[i][r][c] = combined[r][c];
                }
            }
        }
    }
}

void GltfRenderProxy::SkinVertex(const gltf::Vertex& vertex, const float (*boneMatrices)[3][4],
                                  vec3_t& outPos, vec3_t& outNormal)
{
    vec3_t accum_pos = {0, 0, 0};
    vec3_t accum_normal = {0, 0, 0};
    float total_weight = 0.0f;

    // Apply influence of up to 4 bones
    for (int i = 0; i < 4; i++) {
        if (vertex.weights[i] <= 0.001f) continue;  // Skip zero weights

        int boneIdx = vertex.joints[i];
        if (boneIdx < 0 || boneIdx >= 200) continue;  // Sanity check

        float w = vertex.weights[i];
        total_weight += w;

        vec3_t transformed_pos;
        TransformVec3(boneMatrices[boneIdx], vertex.position, transformed_pos);
        VectorScale(transformed_pos, w, transformed_pos);
        VectorAdd(accum_pos, transformed_pos, accum_pos);

        // Transform normal (rotation only, no translation)
        vec3_t rotated_normal;
        float rot_matrix[3][4];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                rot_matrix[r][c] = boneMatrices[boneIdx][r][c];
            }
            rot_matrix[r][3] = 0.0f;
        }
        TransformVec3(rot_matrix, vertex.normal, rotated_normal);
        VectorScale(rotated_normal, w, rotated_normal);
        VectorAdd(accum_normal, rotated_normal, accum_normal);
    }

    // Normalize by total weight
    if (total_weight > 0.001f) {
        VectorScale(accum_pos, 1.0f / total_weight, outPos);
        VectorScale(accum_normal, 1.0f / total_weight, outNormal);
    } else {
        VectorCopy(vertex.position, outPos);
        VectorCopy(vertex.normal, outNormal);
    }
}

void GltfRenderProxy::ApplyBoneWeights(int meshIndex, float (*boneMatrices)[3][4])
{
    if (meshIndex < 0 || meshIndex >= (int)model->meshes.size())
        return;

    const gltf::Mesh& mesh = model->meshes[meshIndex];
    transformedPositions.resize(mesh.vertices.size());
    transformedNormals.resize(mesh.vertices.size());

    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        SkinVertex(mesh.vertices[i], boneMatrices,
                   transformedPositions[i], transformedNormals[i]);
    }
}

void GltfRenderProxy::TransformVertices(int meshIndex, float (*boneMatrices)[3][4],
                                        vec3_t* outPositions, vec3_t* outNormals)
{
    if (meshIndex < 0 || meshIndex >= (int)model->meshes.size())
        return;

    const gltf::Mesh& mesh = model->meshes[meshIndex];

    if (!outPositions || !outNormals)
        return;

    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        SkinVertex(mesh.vertices[i], boneMatrices, outPositions[i], outNormals[i]);
    }
}

void GltfRenderProxy::SetupNormalTransforms(int meshIndex, float (*boneMatrices)[3][4])
{
    // Pre-compute transformed normals for lighting calculations
    ApplyBoneWeights(meshIndex, boneMatrices);
}

void GltfRenderProxy::QuaternionToEuler(const gltf::Vec4Wrapper& quat, vec3_t& eulerAngles)
{
    // Convert quaternion to Euler angles (roll, pitch, yaw)
    // Using ZYX convention (yaw, pitch, roll)
    float qx = quat.x, qy = quat.y, qz = quat.z, qw = quat.w;

    // Roll (x-axis rotation)
    float sinr_cosp = 2 * (qw * qx + qy * qz);
    float cosr_cosp = 1 - 2 * (qx * qx + qy * qy);
    eulerAngles[0] = std::atan2(sinr_cosp, cosr_cosp);

    // Pitch (y-axis rotation)
    float sinp = 2 * (qw * qy - qz * qx);
    if (std::abs(sinp) >= 1)
        eulerAngles[1] = std::copysign(3.14159f / 2, sinp);
    else
        eulerAngles[1] = std::asin(sinp);

    // Yaw (z-axis rotation)
    float siny_cosp = 2 * (qw * qz + qx * qy);
    float cosy_cosp = 1 - 2 * (qy * qy + qz * qz);
    eulerAngles[2] = std::atan2(siny_cosp, cosy_cosp);
}

void GltfRenderProxy::SetupMaterialRenderFlags(int meshIndex)
{
    // Called to pre-compute material render flags (can be cached if needed)
    GetRenderFlagsForMesh(meshIndex);
}
