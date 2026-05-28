#pragma once

#include <GL/glew.h>
#include <map>
#include <memory>
#include <cstring>
#include <vector>

// Math types from MuMain engine
// NOTE: vec3_t/mat4_t may also be defined by engine headers; guard to avoid
// redefinition when this header is included alongside ZzzBMD.h etc.
#ifndef MUMAIN_VEC3_T_DEFINED
#define MUMAIN_VEC3_T_DEFINED
typedef float vec3_t[3];
#endif
typedef float mat3_t[9];   // 3x3 matrix
#ifndef MUMAIN_MAT4_T_DEFINED
#define MUMAIN_MAT4_T_DEFINED
typedef float mat4_t[16];  // 4x4 matrix
#endif

// NOTE: Mesh_t and Triangle_t are engine types (typedef struct {...}) defined in
// Render/Models/ZzzBMD.h. Do NOT forward-declare them here as `struct Mesh_t;`
// — a struct-tag declaration is a different type than the anonymous-struct
// typedef, which triggers C2371 redefinition. The .cpp files include ZzzBMD.h
// to get the full definitions; the public API here uses void* to stay decoupled.

namespace SEASON3B {

// Per-mesh GPU buffer state
struct MeshBuffer {
    GLuint VAO;                 // Vertex Array Object
    GLuint VertexVBO;           // Vertex buffer (positions, normals, texcoords interleaved)
    GLuint IndexBuffer;         // Index buffer (triangle indices)
    GLuint VertexCount;         // Number of vertices
    GLuint IndexCount;          // Number of indices
    GLuint CacheFrameTag;       // Frame tag to detect mesh changes
    bool bValid;                // Whether buffers are valid/ready to render
};

class MeshBufferManager
{
public:
    MeshBufferManager();
    ~MeshBufferManager();

    // Initialize the manager
    bool Initialize();

    // Shutdown and cleanup all buffers
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_bInitialized; }

    // Update mesh buffers from CPU data and render
    // mesh: void pointer to the mesh data (BMD structure or equivalent)
    // meshIndex: index into mesh array
    // vertexCount: number of vertices in this mesh
    // indexCount: number of indices/triangles in this mesh
    // vertexData: pointer to interleaved vertex data [pos3 normal3 texcoord2]
    // indexData: pointer to index data (GLuint array)
    bool UpdateAndRenderMesh(
        void* mesh,
        int meshIndex,
        GLuint vertexCount,
        GLuint indexCount,
        const void* vertexData,  // Interleaved: [float3 pos][float3 normal][float2 texcoord]
        const GLuint* indexData
    );

    // Get or create buffer for a mesh (advanced usage)
    MeshBuffer* GetOrCreateBuffer(int meshIndex, GLuint vertexCount, GLuint indexCount);

    // Set the model matrix (used for normal matrix computation)
    void SetModelMatrix(const float* pModelMatrix);

    // Submit a mesh for rendering (assumes buffers are already updated)
    bool SubmitMesh(int meshIndex);

    // Clear all cached buffers (useful for full mesh system reset)
    void ClearCache();

    // Disable shader rendering and fall back to immediate-mode
    static void DisableMeshShaders();

    // Error reporting
    const std::string& GetLastError() const { return m_LastError; }

private:
    bool m_bInitialized;
    std::string m_LastError;
    std::map<int, MeshBuffer> m_MeshBuffers;  // Per-mesh buffer cache
    mat4_t m_ModelMatrix;  // Current model matrix for normal matrix computation

    // Internal helpers
    void ComputeNormalMatrix(const float* pModelMatrix, float* pOutNormalMatrix);
    bool InvertMatrix3x3(const float* pMat3, float* pOutMat3);
    bool InvertMatrix4x4(const float* pMat4, float* pOutMat4);
    GLuint CreateVAO(GLuint vertexVBO, GLuint indexBuffer);
    GLuint CreateVertexVBO(GLuint vertexCount, const void* vertexData);
    GLuint CreateIndexBuffer(GLuint indexCount, const GLuint* indexData);

    bool UpdateVertexVBO(GLuint vbo, GLuint vertexCount, const void* vertexData);
    bool UpdateIndexBuffer(GLuint ibo, GLuint indexCount, const GLuint* indexData);

    void LogError(const std::string& error);
    void DeleteMeshBuffer(MeshBuffer& buffer);
};

// Global mesh buffer manager instance
extern MeshBufferManager* g_MeshBufferManager;

}  // namespace SEASON3B
