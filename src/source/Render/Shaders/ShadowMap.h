#pragma once

#include <GL/glew.h>

namespace SEASON3B {

// Directional ("sun") shadow map.
//
// Phase 1: owns an FBO with a depth texture and renders shadow casters from the
// sun's point of view into it. The depth pass re-invokes the engine's object/
// character render with the GL modelview set to the light's view, while
// g_ShadowDepthPass (below) tells BMD::RenderMesh to skip its lighting shaders
// and just write depth (color is masked off).
//
// Phase 2 (later): the diffuse/terrain shaders sample this depth texture to
// darken the sun term where a fragment is occluded.
class ShadowMap
{
public:
    ShadowMap();
    ~ShadowMap();

    bool Initialize(int size = 2048);
    void Shutdown();
    bool IsValid() const { return m_bValid; }

    // Begin/end the depth-only caster pass. focusWorldPos is the point the shadow
    // frustum centers on (typically the hero position). restoreW/H is the main
    // viewport to restore afterwards.
    void BeginDepthPass(const float* focusWorldPos);
    void EndDepthPass(int restoreW, int restoreH);

    // Bind the depth texture for sampling in the main pass (Phase 2).
    void BindForReading(GLenum textureUnit);

    // DEBUG: draw the depth texture as a small quad in the top-right corner so we
    // can confirm casters are captured. Remove once Phase 2 is verified.
    void RenderDebugQuad(int screenW, int screenH);

    // uLightFromView = LightProj * LightView * inverse(camView). camView is the
    // captured camera modelview (column-major 16). Result is column-major 16.
    void ComputeLightFromView(const float* camView, float* out16) const;

    GLuint GetDepthTexture() const { return m_DepthTex; }
    int    GetSize() const { return m_Size; }

    // Sun direction (the direction the light travels). Configurable.
    void SetSunDir(float x, float y, float z);

private:
    GLuint m_Fbo;
    GLuint m_DepthTex;
    int    m_Size;
    bool   m_bValid;
    GLint  m_PrevFbo;          // framebuffer bound before the depth pass (e.g. Bloom's)
    GLint  m_PrevViewport[4];  // viewport before the depth pass (restored exactly)

    float  m_SunDir[3];     // normalized, direction light travels
    float  m_LightView[16]; // column-major
    float  m_LightProj[16]; // column-major

    float  m_HalfExtent;    // ortho half-size (world units)
    float  m_Distance;      // light eye distance from focus
    float  m_Near, m_Far;
};

// Global instance + depth-pass flag (defined in ShadowMap.cpp / ZzzBMD.cpp).
extern ShadowMap* g_ShadowMap;

}  // namespace SEASON3B

// Set true only during the shadow depth pass so BMD::RenderMesh writes depth
// without running the lighting shaders. Global (matches g_UseShaderLighting).
extern bool g_ShadowDepthPass;
