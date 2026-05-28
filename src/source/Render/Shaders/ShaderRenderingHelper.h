#pragma once

#include <GL/glew.h>

// Forward declarations
// OBJECT, CHARACTER, and PART_t are defined in w_CharacterInfo.h
// which is included via stdafx.h, so no forward declaration needed here

namespace SEASON3B {

// Helper class to manage shader rendering with automatic fallback
class ShaderRenderingHelper
{
public:
    // Check if shader rendering is available
    static bool IsShaderRenderingAvailable();

    // Try to render a mesh with shaders (returns false if shaders not available)
    // On success, caller should skip calling RenderMesh directly
    static bool TryRenderMeshShaded(
        OBJECT* o,
        int meshIndex,
        float alpha
    );

    // Try to render a character part with shaders
    // On success, caller should skip calling RenderPartObject directly
    static bool TryRenderPartShaded(
        OBJECT* o,
        CHARACTER* c,
        PART_t* part,
        int meshType,
        float alpha
    );

    // Set up lighting uniforms for character rendering
    static void SetupCharacterLighting(OBJECT* o, CHARACTER* c);

    // Reset to fixed-function pipeline after shader rendering
    static void ResetPipeline();
};

}  // namespace SEASON3B
