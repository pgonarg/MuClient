#include "stdafx.h"
#include "ShaderRenderingHelper.h"
#include "ShaderLibrary.h"
#include "MeshBufferManager.h"
// Full definitions of BMD / Mesh_t (engine model structures) used below.
#include "Render/Models/ZzzBMD.h"

// Forward declarations from other modules
extern BMD* Models;

namespace SEASON3B {

bool ShaderRenderingHelper::IsShaderRenderingAvailable()
{
    return g_ShaderLibrary &&
           g_ShaderLibrary->IsPhongShaderValid() &&
           g_MeshBufferManager &&
           g_MeshBufferManager->IsInitialized();
}

bool ShaderRenderingHelper::TryRenderMeshShaded(
    OBJECT* o,
    int meshIndex,
    float alpha
)
{
    if (!IsShaderRenderingAvailable() || !o || o->Type < 0 || meshIndex < 0) {
        return false;
    }

    BMD* bmd = &Models[o->Type];
    if (!bmd || meshIndex >= bmd->NumMeshs) {
        return false;
    }

    try {
        Mesh_t* mesh = &bmd->Meshs[meshIndex];
        if (!mesh || mesh->NumVertices == 0 || mesh->NumTriangles == 0) {
            return false;
        }

        SetupCharacterLighting(o, nullptr);

        // Render via shader system
        if (g_MeshBufferManager->UpdateAndRenderMesh(mesh, meshIndex, mesh->NumVertices,
                                                      mesh->NumTriangles * 3, nullptr, nullptr)) {
            return true;  // Shader rendering succeeded
        }

        return false;  // Shader rendering failed, fall back
    }
    catch (const std::exception& e) {
        g_ErrorReport.Write(L"> Shader render exception: %S\r\n", e.what());
        return false;
    }
}

bool ShaderRenderingHelper::TryRenderPartShaded(
    OBJECT* o,
    CHARACTER* c,
    PART_t* part,
    int meshType,
    float alpha
)
{
    // Shader rendering not yet fully implemented - use original rendering for now
    return false;
}

void ShaderRenderingHelper::SetupCharacterLighting(OBJECT* o, CHARACTER* c)
{
    if (!IsShaderRenderingAvailable())
    {
        return;
    }

    // Extract lighting from character or object
    vec3_t ambientLight = { 0.4f, 0.4f, 0.4f };
    vec3_t lightDirection = { 0.f, -1.f, 0.f };
    vec3_t lightColor = { 1.f, 1.f, 1.f };
    vec3_t viewPos = { 0.f, 0.f, 100.f };

    if (c && c->Light[0] > 0)
    {
        // Use character's light values
        ambientLight[0] = c->Light[0];
        ambientLight[1] = c->Light[1];
        ambientLight[2] = c->Light[2];
    }
    else if (o && o->Light[0] > 0)
    {
        // Use object's light values
        ambientLight[0] = o->Light[0];
        ambientLight[1] = o->Light[1];
        ambientLight[2] = o->Light[2];
    }

    // Set lighting uniforms in shader
    g_ShaderLibrary->SetPhongLighting(
        ambientLight,
        lightDirection,
        lightColor,
        viewPos
    );
}

void ShaderRenderingHelper::ResetPipeline()
{
    if (g_ShaderLibrary)
    {
        g_ShaderLibrary->DisableShaders();
    }
}

}  // namespace SEASON3B
