#include "stdafx.h"
#include "ShaderLibrary.h"
#include "PhongLighting.glsl.h"
#include "ShadowMap.h"

namespace SEASON3B {

// Global shader library instance
ShaderLibrary* g_ShaderLibrary = nullptr;

ShaderLibrary::ShaderLibrary()
    : m_bInitialized(false)
    , m_CollectCount(0)
    , m_LightCount(0)
    , m_ActiveLightCount(0)
    , m_ShadowActive(false)
{
}

ShaderLibrary::~ShaderLibrary()
{
    Shutdown();
}

bool ShaderLibrary::Initialize()
{
    if (m_bInitialized) {
        return true;
    }

    // Create Phong shader program
    m_PhongShader = std::make_unique<ShaderProgram>();

    // Compile Phong shaders
    std::string error;
    if (!m_PhongShader->CompileFromSource(
        Shaders::PHONG_VERTEX_SHADER,
        Shaders::PHONG_FRAGMENT_SHADER,
        &error
    )) {
        LogShaderError("Phong", error);
        m_PhongShader.reset();
        return false;
    }

    // Compile chrome/metal/oil environment-mapping shader (per-pixel reflection).
    // Non-fatal: if it fails, chrome meshes simply fall back to per-vertex rendering.
    m_ChromeShader = std::make_unique<ShaderProgram>();
    std::string chromeError;
    if (!m_ChromeShader->CompileFromSource(
        Shaders::CHROME_VERTEX_SHADER,
        Shaders::CHROME_FRAGMENT_SHADER,
        &chromeError
    )) {
        LogShaderError("Chrome", chromeError);
        m_ChromeShader.reset();
        // continue: Phong (diffuse) shader is still usable
    }

    // Compile terrain shader (faithful texColor * vertexColor modulate).
    // Non-fatal: if it fails, terrain simply stays on the fixed-function path.
    m_TerrainShader = std::make_unique<ShaderProgram>();
    std::string terrainError;
    if (!m_TerrainShader->CompileFromSource(
        Shaders::TERRAIN_VERTEX_SHADER,
        Shaders::TERRAIN_FRAGMENT_SHADER,
        &terrainError
    )) {
        LogShaderError("Terrain", terrainError);
        m_TerrainShader.reset();
    }

    m_bInitialized = true;
    g_ErrorReport.Write(L"> ShaderLibrary initialized successfully.\r\n");
    return true;
}

void ShaderLibrary::Shutdown()
{
    if (m_PhongShader) {
        m_PhongShader->Release();
        m_PhongShader.reset();
    }
    if (m_ChromeShader) {
        m_ChromeShader->Release();
        m_ChromeShader.reset();
    }
    if (m_TerrainShader) {
        m_TerrainShader->Release();
        m_TerrainShader.reset();
    }
    m_bInitialized = false;
    g_ErrorReport.Write(L"> ShaderLibrary shut down.\r\n");
}

void ShaderLibrary::UsePhongShader()
{
    if (m_PhongShader && m_PhongShader->IsValid()) {
        m_PhongShader->Use();
    }
}

void ShaderLibrary::UseCharacterLighting(const float* lightDir, const float* bodyLight, float alpha)
{
    if (!m_PhongShader || !m_PhongShader->IsValid()) {
        return;
    }

    m_PhongShader->Use();
    if (lightDir) {
        m_PhongShader->SetVec3f("uLightDir", lightDir[0], lightDir[1], lightDir[2]);
    }
    if (bodyLight) {
        m_PhongShader->SetVec3f("uBodyLight", bodyLight[0], bodyLight[1], bodyLight[2]);
    }
    m_PhongShader->SetFloat("uAlpha", alpha);
    m_PhongShader->SetInt("uTexture", 0);  // engine binds the texture on unit 0
    UploadDynamicLights(m_PhongShader.get());
    UploadShadow(m_PhongShader.get());
}

// ===== DYNAMIC POINT LIGHTS =====

void ShaderLibrary::BeginFrameLights()
{
    // Per-frame sync point: merge the lights collected since the last call into
    // the persistent set, then advance each light's fade. Lights fade in when
    // (re)registered and fade out when their source stops registering (e.g. it
    // left the frustum), which smooths the otherwise-hard pop-in/out.
    const float FADE_STEP   = 0.12f;          // ~0.1s at 60fps
    const float MATCH_DIST2 = 60.f * 60.f;    // re-match radius^2 (emitters are ~static)

    for (int i = 0; i < m_LightCount; ++i) m_Lights[i].seen = false;

    for (int c = 0; c < m_CollectCount; ++c) {
        const DynLight& L = m_CollectLights[c];
        int match = -1;
        for (int i = 0; i < m_LightCount; ++i) {
            float dx = m_Lights[i].pos[0] - L.pos[0];
            float dy = m_Lights[i].pos[1] - L.pos[1];
            float dz = m_Lights[i].pos[2] - L.pos[2];
            if (dx * dx + dy * dy + dz * dz <= MATCH_DIST2) { match = i; break; }
        }
        if (match < 0 && m_LightCount < MAX_DYNAMIC_LIGHTS) {
            match = m_LightCount++;
            m_Lights[match].fade = 0.f;  // new light fades in from dark
        }
        if (match >= 0) {
            PersistLight& P = m_Lights[match];
            P.pos[0] = L.pos[0]; P.pos[1] = L.pos[1]; P.pos[2] = L.pos[2];
            P.color[0] = L.color[0]; P.color[1] = L.color[1]; P.color[2] = L.color[2];
            P.range = L.range;
            P.seen = true;
        }
    }
    m_CollectCount = 0;

    // Advance fades; compact out lights that have fully faded.
    int n = 0;
    for (int i = 0; i < m_LightCount; ++i) {
        float target = m_Lights[i].seen ? 1.f : 0.f;
        float& f = m_Lights[i].fade;
        if (f < target) { f += FADE_STEP; if (f > target) f = target; }
        else if (f > target) { f -= FADE_STEP; if (f < target) f = target; }
        if (f > 0.001f) {
            if (n != i) m_Lights[n] = m_Lights[i];
            ++n;
        }
    }
    m_LightCount = n;
}

void ShaderLibrary::AddDynamicLight(float x, float y, float z, const float* color, float range)
{
    if (m_CollectCount >= MAX_DYNAMIC_LIGHTS || !color || range <= 0.f) {
        return;  // budget reached (first-come); scenes rarely exceed the cap
    }
    DynLight& L = m_CollectLights[m_CollectCount++];
    L.pos[0] = x; L.pos[1] = y; L.pos[2] = z;
    L.color[0] = color[0]; L.color[1] = color[1]; L.color[2] = color[2];
    L.range = range;
}

void ShaderLibrary::SetViewMatrix(const float* m16)
{
    if (!m16) {
        return;
    }
    // Transform each persistent light's world position into view space (the space
    // shared by object and terrain shaders) and scale its color by the fade.
    int n = 0;
    for (int i = 0; i < m_LightCount && n < MAX_DYNAMIC_LIGHTS; ++i) {
        const float* w = m_Lights[i].pos;
        const float  f = m_Lights[i].fade;
        m_LightViewPos[n * 3 + 0] = m16[0] * w[0] + m16[4] * w[1] + m16[8]  * w[2] + m16[12];
        m_LightViewPos[n * 3 + 1] = m16[1] * w[0] + m16[5] * w[1] + m16[9]  * w[2] + m16[13];
        m_LightViewPos[n * 3 + 2] = m16[2] * w[0] + m16[6] * w[1] + m16[10] * w[2] + m16[14];
        m_LightColor[n * 3 + 0] = m_Lights[i].color[0] * f;
        m_LightColor[n * 3 + 1] = m_Lights[i].color[1] * f;
        m_LightColor[n * 3 + 2] = m_Lights[i].color[2] * f;
        m_LightRange[n] = m_Lights[i].range;
        ++n;
    }
    m_ActiveLightCount = n;

    // Shadow mapping: build the view->light-clip matrix and bind the depth map.
    m_ShadowActive = false;
    if (g_ShadowMap && g_ShadowMap->IsValid()) {
        g_ShadowMap->ComputeLightFromView(m16, m_ShadowFromView);
        g_ShadowMap->BindForReading(GL_TEXTURE1);  // sampler uShadowMap on unit 1
        m_ShadowActive = true;
    }
}

void ShaderLibrary::UploadShadow(ShaderProgram* prog)
{
    if (!prog) return;
    prog->SetInt("uShadowMap", 1);   // texture unit 1
    prog->SetFloat("uShadowEnabled", m_ShadowActive ? 1.0f : 0.0f);
    if (m_ShadowActive) {
        prog->SetMatrix4fv("uLightFromView", m_ShadowFromView);
    }
}

void ShaderLibrary::UploadDynamicLights(ShaderProgram* prog)
{
    if (!prog) {
        return;
    }
    prog->SetInt("uLightCount", m_ActiveLightCount);
    if (m_ActiveLightCount > 0) {
        prog->SetVec3Array("uLightPos",   m_ActiveLightCount, m_LightViewPos);
        prog->SetVec3Array("uLightColor", m_ActiveLightCount, m_LightColor);
        prog->SetFloatArray("uLightRange", m_ActiveLightCount, m_LightRange);
    }
}

void ShaderLibrary::UseChromeLighting(int mode, const float* bodyLight, float alpha,
                                      float wave, float wave2, float worldTime,
                                      const float* L, const float* blend)
{
    if (!m_ChromeShader || !m_ChromeShader->IsValid()) {
        return;
    }

    m_ChromeShader->Use();
    m_ChromeShader->SetInt("uChromeMode", mode);
    if (bodyLight) {
        m_ChromeShader->SetVec3f("uBodyLight", bodyLight[0], bodyLight[1], bodyLight[2]);
    }
    m_ChromeShader->SetFloat("uAlpha", alpha);
    m_ChromeShader->SetFloat("uWave", wave);
    m_ChromeShader->SetFloat("uWave2", wave2);
    m_ChromeShader->SetFloat("uWorldTime", worldTime);
    if (L) {
        m_ChromeShader->SetVec3f("uChromeL", L[0], L[1], L[2]);
    }
    if (blend) {
        m_ChromeShader->SetVec2f("uBlend", blend[0], blend[1]);
    }
    m_ChromeShader->SetInt("uTexture", 0);
}

bool ShaderLibrary::IsChromeShaderValid() const
{
    return m_bInitialized && m_ChromeShader && m_ChromeShader->IsValid();
}

void ShaderLibrary::UseTerrainShader()
{
    if (!m_TerrainShader || !m_TerrainShader->IsValid()) {
        return;
    }
    m_TerrainShader->Use();
    m_TerrainShader->SetInt("uTexture", 0);  // engine binds the tile texture on unit 0
    UploadDynamicLights(m_TerrainShader.get());
    UploadShadow(m_TerrainShader.get());
}

bool ShaderLibrary::IsTerrainShaderValid() const
{
    return m_bInitialized && m_TerrainShader && m_TerrainShader->IsValid();
}

void ShaderLibrary::SetPhongLighting(
    const vec3_t& ambientLight,
    const vec3_t& lightDirection,
    const vec3_t& lightColor,
    const vec3_t& viewPosition
)
{
    if (!m_PhongShader || !m_PhongShader->IsValid()) {
        return;
    }

    m_PhongShader->Use();

    // Set ambient light
    m_PhongShader->SetVec3f("uAmbientLight", ambientLight[0], ambientLight[1], ambientLight[2]);

    // Set light direction (normalized)
    m_PhongShader->SetVec3f("uLightDirection", lightDirection[0], lightDirection[1], lightDirection[2]);

    // Set light color
    m_PhongShader->SetVec3f("uLightColor", lightColor[0], lightColor[1], lightColor[2]);

    // Set view position
    m_PhongShader->SetVec3f("uViewPos", viewPosition[0], viewPosition[1], viewPosition[2]);

    // Set texture unit (texture 0 is default)
    m_PhongShader->SetInt("uTexture", 0);
}

void ShaderLibrary::SetPhongMaterial(float shininess)
{
    if (!m_PhongShader || !m_PhongShader->IsValid()) {
        return;
    }

    m_PhongShader->Use();
    m_PhongShader->SetFloat("uMaterialShininess", shininess);
}

void ShaderLibrary::SetMatrices(
    const float* pModelMatrix,
    const float* pViewMatrix,
    const float* pProjectionMatrix
)
{
    if (!m_PhongShader || !m_PhongShader->IsValid()) {
        return;
    }

    m_PhongShader->Use();

    if (pModelMatrix) {
        m_PhongShader->SetMatrix4fv("uModelMatrix", pModelMatrix);
    }
    if (pViewMatrix) {
        m_PhongShader->SetMatrix4fv("uViewMatrix", pViewMatrix);
    }
    if (pProjectionMatrix) {
        m_PhongShader->SetMatrix4fv("uProjectMatrix", pProjectionMatrix);
    }
}

void ShaderLibrary::SetNormalMatrix(const float* pNormalMatrix)
{
    if (!m_PhongShader || !m_PhongShader->IsValid()) {
        return;
    }

    if (pNormalMatrix) {
        m_PhongShader->SetMatrix3fv("uNormalMatrix", pNormalMatrix);
    }
}

void ShaderLibrary::DisableShaders()
{
    glUseProgram(0);
}

bool ShaderLibrary::IsPhongShaderValid() const
{
    return m_bInitialized && m_PhongShader && m_PhongShader->IsValid();
}

void ShaderLibrary::LogShaderError(const std::string& shaderName, const std::string& error)
{
    m_LastError = error;
    g_ErrorReport.Write(L"> Shader Error (%S): %S\r\n", shaderName.c_str(), error.c_str());
}

}  // namespace SEASON3B
