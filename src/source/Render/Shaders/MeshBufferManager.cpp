#include "stdafx.h"
#include "MeshBufferManager.h"
#include "ShaderLibrary.h"
// Full definitions of Mesh_t / Triangle_t (engine BMD model structures).
// Required here because UpdateAndRenderMesh() casts the incoming void* to
// Mesh_t* and dereferences members (Vertices, Normals, Triangles, etc.).
#include "Render/Models/ZzzBMD.h"

namespace SEASON3B {

// Global mesh buffer manager instance
MeshBufferManager* g_MeshBufferManager = nullptr;

MeshBufferManager::MeshBufferManager()
    : m_bInitialized(false)
{
    // Initialize model matrix to identity
    memset(m_ModelMatrix, 0, sizeof(m_ModelMatrix));
    m_ModelMatrix[0] = 1.0f;
    m_ModelMatrix[5] = 1.0f;
    m_ModelMatrix[10] = 1.0f;
    m_ModelMatrix[15] = 1.0f;
}

MeshBufferManager::~MeshBufferManager()
{
    Shutdown();
}

bool MeshBufferManager::Initialize()
{
    if (m_bInitialized) {
        return true;
    }

    m_bInitialized = true;
    g_ErrorReport.Write(L"> MeshBufferManager initialized.\r\n");
    return true;
}

void MeshBufferManager::Shutdown()
{
    ClearCache();
    m_bInitialized = false;
    g_ErrorReport.Write(L"> MeshBufferManager shut down.\r\n");
}

bool MeshBufferManager::UpdateAndRenderMesh(
    void* pMeshVoid,
    int meshIndex,
    GLuint vertexCount,
    GLuint indexCount,
    const void* vertexData,
    const GLuint* indexData
)
{
    if (!m_bInitialized || !g_ShaderLibrary || !g_ShaderLibrary->IsPhongShaderValid()) {
        return false;
    }

    if (!pMeshVoid || vertexCount == 0 || indexCount == 0) {
        return false;
    }

    try {
        // Cast mesh pointer to Mesh_t if available
        Mesh_t* pMesh = reinterpret_cast<Mesh_t*>(pMeshVoid);

        // Check if mesh has valid data
        if (!pMesh->Vertices || !pMesh->Normals || !pMesh->TexCoords || !pMesh->Triangles) {
            return false;
        }

        // Get or create buffer
        MeshBuffer* pBuffer = GetOrCreateBuffer(meshIndex, pMesh->NumVertices, pMesh->NumTriangles * 3);
        if (!pBuffer || !pBuffer->bValid) {
            return false;
        }

        // Build interleaved vertex buffer [pos3 normal3 texcoord2]
        std::vector<float> interleavedVertices;
        interleavedVertices.reserve(pMesh->NumVertices * 8);  // 8 floats per vertex

        for (int i = 0; i < pMesh->NumVertices; ++i) {
            // Position
            interleavedVertices.push_back(pMesh->Vertices[i].Position[0]);
            interleavedVertices.push_back(pMesh->Vertices[i].Position[1]);
            interleavedVertices.push_back(pMesh->Vertices[i].Position[2]);

            // Normal
            interleavedVertices.push_back(pMesh->Normals[i].Normal[0]);
            interleavedVertices.push_back(pMesh->Normals[i].Normal[1]);
            interleavedVertices.push_back(pMesh->Normals[i].Normal[2]);

            // TexCoord
            interleavedVertices.push_back(pMesh->TexCoords[i].TexCoordU);
            interleavedVertices.push_back(pMesh->TexCoords[i].TexCoordV);
        }

        // Build index buffer from triangles
        std::vector<GLuint> indices;
        indices.reserve(pMesh->NumTriangles * 3);

        for (int i = 0; i < pMesh->NumTriangles; ++i) {
            Triangle_t& tri = pMesh->Triangles[i];
            if (tri.Polygon == 3) {  // Triangle
                indices.push_back(tri.VertexIndex[0]);
                indices.push_back(tri.VertexIndex[1]);
                indices.push_back(tri.VertexIndex[2]);
            } else if (tri.Polygon == 4) {  // Quad - split into 2 triangles
                indices.push_back(tri.VertexIndex[0]);
                indices.push_back(tri.VertexIndex[1]);
                indices.push_back(tri.VertexIndex[2]);

                indices.push_back(tri.VertexIndex[0]);
                indices.push_back(tri.VertexIndex[2]);
                indices.push_back(tri.VertexIndex[3]);
            }
        }

        // Update GPU buffers
        if (!UpdateVertexVBO(pBuffer->VertexVBO, pMesh->NumVertices, interleavedVertices.data())) {
            return false;
        }

        if (!UpdateIndexBuffer(pBuffer->IndexBuffer, indices.size(), indices.data())) {
            return false;
        }

        // Update index count for rendering
        pBuffer->IndexCount = indices.size();

        // Render the mesh
        return SubmitMesh(meshIndex);
    }
    catch (const std::exception& e) {
        LogError(std::string("Exception: ") + e.what());
        return false;
    }
}

MeshBuffer* MeshBufferManager::GetOrCreateBuffer(int meshIndex, GLuint vertexCount, GLuint indexCount)
{
    // Check if buffer already exists
    auto it = m_MeshBuffers.find(meshIndex);
    if (it != m_MeshBuffers.end()) {
        // Verify it's still valid
        if (it->second.bValid && it->second.VertexCount == vertexCount) {
            return &it->second;
        }
        // Size mismatch or invalid, delete and recreate
        DeleteMeshBuffer(it->second);
        m_MeshBuffers.erase(it);
    }

    // Create new buffer
    MeshBuffer newBuffer;
    newBuffer.VertexCount = vertexCount;
    newBuffer.IndexCount = indexCount;
    newBuffer.CacheFrameTag = 0;
    newBuffer.bValid = false;

    // Create GPU buffers
    newBuffer.VertexVBO = CreateVertexVBO(vertexCount, nullptr);
    if (!newBuffer.VertexVBO) {
        LogError("Failed to create vertex VBO");
        return nullptr;
    }

    newBuffer.IndexBuffer = CreateIndexBuffer(indexCount, nullptr);
    if (!newBuffer.IndexBuffer) {
        glDeleteBuffers(1, &newBuffer.VertexVBO);
        LogError("Failed to create index buffer");
        return nullptr;
    }

    newBuffer.VAO = CreateVAO(newBuffer.VertexVBO, newBuffer.IndexBuffer);
    if (!newBuffer.VAO) {
        glDeleteBuffers(1, &newBuffer.VertexVBO);
        glDeleteBuffers(1, &newBuffer.IndexBuffer);
        LogError("Failed to create VAO");
        return nullptr;
    }

    newBuffer.bValid = true;

    // Store in cache
    m_MeshBuffers[meshIndex] = newBuffer;
    return &m_MeshBuffers[meshIndex];
}

bool MeshBufferManager::SubmitMesh(int meshIndex)
{
    auto it = m_MeshBuffers.find(meshIndex);
    if (it == m_MeshBuffers.end() || !it->second.bValid) {
        LogError("Invalid mesh index or buffer");
        return false;
    }

    MeshBuffer& buffer = it->second;

    // Use shader program
    if (!g_ShaderLibrary) {
        return false;
    }

    g_ShaderLibrary->UsePhongShader();

    // Compute and set normal matrix from model matrix
    mat3_t normalMatrix;
    ComputeNormalMatrix((const float*)m_ModelMatrix, (float*)normalMatrix);
    g_ShaderLibrary->SetNormalMatrix((const float*)normalMatrix);

    // Bind VAO
    glBindVertexArray(buffer.VAO);

    // Draw
    glDrawElements(GL_TRIANGLES, buffer.IndexCount, GL_UNSIGNED_INT, nullptr);

    // Unbind
    glBindVertexArray(0);

    return true;
}

void MeshBufferManager::SetModelMatrix(const float* pModelMatrix)
{
    if (pModelMatrix) {
        memcpy(m_ModelMatrix, pModelMatrix, sizeof(m_ModelMatrix));
    } else {
        // Reset to identity
        memset(m_ModelMatrix, 0, sizeof(m_ModelMatrix));
        m_ModelMatrix[0] = 1.0f;
        m_ModelMatrix[5] = 1.0f;
        m_ModelMatrix[10] = 1.0f;
        m_ModelMatrix[15] = 1.0f;
    }
}

void MeshBufferManager::ClearCache()
{
    for (auto& pair : m_MeshBuffers) {
        DeleteMeshBuffer(pair.second);
    }
    m_MeshBuffers.clear();
}

GLuint MeshBufferManager::CreateVAO(GLuint vertexVBO, GLuint indexBuffer)
{
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    if (!vao) {
        return 0;
    }

    glBindVertexArray(vao);

    // Bind vertex VBO
    glBindBuffer(GL_ARRAY_BUFFER, vertexVBO);

    // Vertex Layout:
    // Attribute 0: Position (3 floats, offset 0, stride 32)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);

    // Attribute 1: Normal (3 floats, offset 12, stride 32)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);

    // Attribute 2: TexCoord (2 floats, offset 24, stride 32)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);

    // Bind index buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

    glBindVertexArray(0);

    return vao;
}

GLuint MeshBufferManager::CreateVertexVBO(GLuint vertexCount, const void* vertexData)
{
    if (vertexCount == 0) {
        return 0;
    }

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    if (!vbo) {
        return 0;
    }

    // Vertex format: [pos3 normal3 texcoord2] = 32 bytes per vertex
    GLuint bufferSize = vertexCount * 32;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bufferSize, vertexData, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return vbo;
}

GLuint MeshBufferManager::CreateIndexBuffer(GLuint indexCount, const GLuint* indexData)
{
    if (indexCount == 0) {
        return 0;
    }

    GLuint ibo = 0;
    glGenBuffers(1, &ibo);
    if (!ibo) {
        return 0;
    }

    GLuint bufferSize = indexCount * sizeof(GLuint);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, bufferSize, indexData, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return ibo;
}

bool MeshBufferManager::UpdateVertexVBO(GLuint vbo, GLuint vertexCount, const void* vertexData)
{
    if (!vbo || !vertexData || vertexCount == 0) {
        return false;
    }

    GLuint bufferSize = vertexCount * 32;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Use glBufferSubData for partial updates, or glBufferData for full replacement
    // For safety, we use glBufferData which reallocates
    glBufferData(GL_ARRAY_BUFFER, bufferSize, vertexData, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

bool MeshBufferManager::UpdateIndexBuffer(GLuint ibo, GLuint indexCount, const GLuint* indexData)
{
    if (!ibo || !indexData || indexCount == 0) {
        return false;
    }

    GLuint bufferSize = indexCount * sizeof(GLuint);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, bufferSize, indexData, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return true;
}

void MeshBufferManager::LogError(const std::string& error)
{
    m_LastError = error;
    g_ErrorReport.Write(L"> MeshBufferManager Error: %S\r\n", error.c_str());
}

void MeshBufferManager::DeleteMeshBuffer(MeshBuffer& buffer)
{
    if (buffer.VAO) {
        glDeleteVertexArrays(1, &buffer.VAO);
        buffer.VAO = 0;
    }
    if (buffer.VertexVBO) {
        glDeleteBuffers(1, &buffer.VertexVBO);
        buffer.VertexVBO = 0;
    }
    if (buffer.IndexBuffer) {
        glDeleteBuffers(1, &buffer.IndexBuffer);
        buffer.IndexBuffer = 0;
    }
    buffer.bValid = false;
}

void MeshBufferManager::DisableMeshShaders()
{
    glUseProgram(0);
}

void MeshBufferManager::ComputeNormalMatrix(const float* pModelMatrix, float* pOutNormalMatrix)
{
    if (!pModelMatrix || !pOutNormalMatrix) {
        return;
    }

    // Extract 3x3 upper-left from 4x4 model matrix
    float mat3[9] = {
        pModelMatrix[0], pModelMatrix[1], pModelMatrix[2],
        pModelMatrix[4], pModelMatrix[5], pModelMatrix[6],
        pModelMatrix[8], pModelMatrix[9], pModelMatrix[10]
    };

    // Invert the 3x3 matrix
    float invMat3[9];
    if (!InvertMatrix3x3(mat3, invMat3)) {
        // If inversion fails, use identity
        memset(invMat3, 0, sizeof(invMat3));
        invMat3[0] = 1.0f;
        invMat3[4] = 1.0f;
        invMat3[8] = 1.0f;
    }

    // Transpose the inverse (inverse-transpose of model matrix)
    pOutNormalMatrix[0] = invMat3[0];
    pOutNormalMatrix[1] = invMat3[3];
    pOutNormalMatrix[2] = invMat3[6];
    pOutNormalMatrix[3] = invMat3[1];
    pOutNormalMatrix[4] = invMat3[4];
    pOutNormalMatrix[5] = invMat3[7];
    pOutNormalMatrix[6] = invMat3[2];
    pOutNormalMatrix[7] = invMat3[5];
    pOutNormalMatrix[8] = invMat3[8];
}

bool MeshBufferManager::InvertMatrix3x3(const float* pMat3, float* pOutMat3)
{
    if (!pMat3 || !pOutMat3) {
        return false;
    }

    // Compute determinant
    float det = pMat3[0] * (pMat3[4] * pMat3[8] - pMat3[5] * pMat3[7]) -
                pMat3[1] * (pMat3[3] * pMat3[8] - pMat3[5] * pMat3[6]) +
                pMat3[2] * (pMat3[3] * pMat3[7] - pMat3[4] * pMat3[6]);

    // Check if singular
    if (fabs(det) < 1e-6f) {
        return false;
    }

    // Compute inverse
    float invDet = 1.0f / det;

    pOutMat3[0] = (pMat3[4] * pMat3[8] - pMat3[5] * pMat3[7]) * invDet;
    pOutMat3[1] = (pMat3[2] * pMat3[7] - pMat3[1] * pMat3[8]) * invDet;
    pOutMat3[2] = (pMat3[1] * pMat3[5] - pMat3[2] * pMat3[4]) * invDet;

    pOutMat3[3] = (pMat3[5] * pMat3[6] - pMat3[3] * pMat3[8]) * invDet;
    pOutMat3[4] = (pMat3[0] * pMat3[8] - pMat3[2] * pMat3[6]) * invDet;
    pOutMat3[5] = (pMat3[2] * pMat3[3] - pMat3[0] * pMat3[5]) * invDet;

    pOutMat3[6] = (pMat3[3] * pMat3[7] - pMat3[4] * pMat3[6]) * invDet;
    pOutMat3[7] = (pMat3[1] * pMat3[6] - pMat3[0] * pMat3[7]) * invDet;
    pOutMat3[8] = (pMat3[0] * pMat3[4] - pMat3[1] * pMat3[3]) * invDet;

    return true;
}

bool MeshBufferManager::InvertMatrix4x4(const float* pMat4, float* pOutMat4)
{
    // Placeholder for future use (not needed yet)
    // For now, just return false
    return false;
}

}  // namespace SEASON3B
