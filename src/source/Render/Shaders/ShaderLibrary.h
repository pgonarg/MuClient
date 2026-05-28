#pragma once

#include "ShaderProgram.h"
#include <memory>

// Forward declarations
typedef float vec3_t[3];

namespace SEASON3B {

class ShaderLibrary
{
public:
    ShaderLibrary();
    ~ShaderLibrary();

    // Initialize shader library
    bool Initialize();

    // Shutdown shader library
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_bInitialized; }

    // ===== PHONG SHADER METHODS =====

    // Use Phong shader program
    void UsePhongShader();

    // Activate the per-pixel character lighting shader and set its uniforms.
    // lightDir  : object-local light vector (the engine's LightPosition; NOT normalized)
    // bodyLight : per-character body/terrain light color (BMD::BodyLight)
    // alpha     : object alpha (fade)
    // Binds sampler uTexture to texture unit 0 (the engine binds the texture there).
    void UseCharacterLighting(const float* lightDir, const float* bodyLight, float alpha);

    // Activate the per-pixel chrome/metal/oil environment-mapping shader.
    // mode      : chrome variant (0..8, see PhongLighting.glsl.h)
    // bodyLight : chrome color (BMD::BodyLight, possibly alpha-scaled by caller)
    // alpha     : object alpha
    // wave/wave2/worldTime/L/blend : per-frame parameters mirrored from RenderMesh
    void UseChromeLighting(int mode, const float* bodyLight, float alpha,
                           float wave, float wave2, float worldTime,
                           const float* L, const float* blend);

    // Check if chrome shader compiled successfully
    bool IsChromeShaderValid() const;

    // Activate the terrain shader (faithful texColor * vertexColor modulate).
    // Binds sampler uTexture to texture unit 0 (the engine binds the tile texture there).
    void UseTerrainShader();

    // Check if terrain shader compiled successfully
    bool IsTerrainShaderValid() const;

    // ===== DYNAMIC POINT LIGHTS =====
    // Per-frame light collection. AddDynamicLight is called (via AddTerrainLight)
    // as emitters register; BeginFrameLights promotes the previous frame's
    // collected set to "active" and resets the collector. SetViewMatrix captures
    // the camera view matrix and transforms active lights into view space, where
    // both object (object-local verts) and terrain (world verts) shaders evaluate
    // them uniformly (gl_ModelViewMatrix * gl_Vertex == view space for both).
    static const int MAX_DYNAMIC_LIGHTS = 16;
    void BeginFrameLights();
    void AddDynamicLight(float x, float y, float z, const float* color, float range);
    void SetViewMatrix(const float* m16);
    int  GetActiveLightCount() const { return m_ActiveLightCount; }

    // Get Phong shader (for advanced usage)
    ShaderProgram* GetPhongShader() { return m_PhongShader.get(); }

    // Set Phong lighting parameters
    void SetPhongLighting(
        const vec3_t& ambientLight,      // Ambient light color [0-1]
        const vec3_t& lightDirection,    // Light direction (normalized)
        const vec3_t& lightColor,        // Light color [0-1]
        const vec3_t& viewPosition       // Camera/view position in world space
    );

    // Set Phong material properties
    void SetPhongMaterial(float shininess = 32.0f);

    // Set model/view/projection matrices
    void SetMatrices(
        const float* pModelMatrix,      // 4x4 model matrix
        const float* pViewMatrix,       // 4x4 view matrix
        const float* pProjectionMatrix  // 4x4 projection matrix
    );

    // Set normal matrix (3x3 inverse-transpose of model matrix upper-left for proper normal transformation)
    void SetNormalMatrix(const float* pNormalMatrix);

    // ===== SHADER STATE =====

    // Use fixed-function pipeline (disable shaders)
    void DisableShaders();

    // Check if Phong shader compiled successfully
    bool IsPhongShaderValid() const;

    // Get last error message
    const std::string& GetLastError() const { return m_LastError; }

private:
    std::unique_ptr<ShaderProgram> m_PhongShader;
    std::unique_ptr<ShaderProgram> m_ChromeShader;
    std::unique_ptr<ShaderProgram> m_TerrainShader;
    bool m_bInitialized;
    std::string m_LastError;

    // Dynamic point lights.
    // Emitters register into the collect buffer each frame (AddDynamicLight).
    // BeginFrameLights merges that buffer into a persistent set with per-light
    // fade, so lights fade in/out over a few frames instead of hard-popping when
    // their source enters/leaves the frustum (sources are frustum-culled, which
    // is what causes the pop). Lights persist (and fade) even after their source
    // stops registering, until fully faded out.
    struct DynLight { float pos[3]; float color[3]; float range; };
    DynLight m_CollectLights[MAX_DYNAMIC_LIGHTS];   // raw, since last BeginFrameLights
    int      m_CollectCount;

    struct PersistLight { float pos[3]; float color[3]; float range; float fade; bool seen; };
    PersistLight m_Lights[MAX_DYNAMIC_LIGHTS];
    int          m_LightCount;

    float    m_LightViewPos[MAX_DYNAMIC_LIGHTS * 3];  // view-space positions (uniform-ready)
    float    m_LightColor[MAX_DYNAMIC_LIGHTS * 3];    // fade-scaled colors (uniform-ready)
    float    m_LightRange[MAX_DYNAMIC_LIGHTS];        // ranges (uniform-ready)
    int      m_ActiveLightCount;                       // count uploaded this frame

    // Upload the active view-space lights to a program's uLight* uniforms.
    void UploadDynamicLights(ShaderProgram* prog);

    // Shadow mapping: uLightFromView maps a view-space position into the sun's
    // clip space for the shadow-map lookup. Computed in SetViewMatrix from the
    // camera view + the shadow map's light matrices; bound shadow depth tex on unit 1.
    float m_ShadowFromView[16];
    bool  m_ShadowActive;
    void  UploadShadow(ShaderProgram* prog);

    // Helper: Log shader compilation
    void LogShaderError(const std::string& shaderName, const std::string& error);
};

// Global shader library instance
extern ShaderLibrary* g_ShaderLibrary;

}  // namespace SEASON3B
