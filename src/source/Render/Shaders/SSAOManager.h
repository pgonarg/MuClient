#pragma once

#include <GL/glew.h>

// SSAO Manager: handles G-Buffer capture, SSAO computation, blur, and composite.
// Integrates with existing Bloom pipeline to darken scene by ambient occlusion.

namespace SEASON3B {

struct SSAOSettings
{
    float radius;           // Sampling radius in view space (default: 1.0)
    float bias;             // Self-occlusion prevention (default: 0.025)
    float intensity;        // AO darkening strength (default: 1.0)
    int   sampleCount;      // Samples per pixel: 4, 8, or 16 (default: 8)
    float blurRadius;       // Bilateral blur kernel size (default: 4.0)
    bool  enabled;          // Enable/disable SSAO rendering
};

class SSAOManager
{
public:
    SSAOManager();
    ~SSAOManager();

    // Initialize SSAO system: compile shaders, create FBOs, etc.
    bool Initialize();

    // Shutdown and cleanup all GPU resources
    void Shutdown();

    // Check if SSAO system is initialized and available
    bool IsInitialized() const { return m_bInitialized; }

    // Check if SSAO is enabled
    bool IsEnabled() const { return m_settings.enabled; }

    // ---- Rendering Pipeline ----

    // Call at start of frame: prepare G-Buffer capture
    void BeginGBufferCapture(int width, int height);

    // Call after 3D scene is rendered: capture position/normal into G-Buffer
    void EndGBufferCapture();

    // Compute SSAO from captured G-Buffer, blur, and composite into scene
    // Call after Bloom::ApplyBloom() and before UI rendering
    bool ApplySSAO(GLuint sceneColorTexture);

    // ---- Settings ----

    void SetRadius(float r)             { m_settings.radius = r; }
    void SetBias(float b)               { m_settings.bias = b; }
    void SetIntensity(float i)          { m_settings.intensity = i; }
    void SetSampleCount(int count)      { m_settings.sampleCount = count; }
    void SetBlurRadius(float r)         { m_settings.blurRadius = r; }
    void SetEnabled(bool enabled)       { m_settings.enabled = enabled; }

    float GetRadius() const             { return m_settings.radius; }
    float GetBias() const               { return m_settings.bias; }
    float GetIntensity() const          { return m_settings.intensity; }
    int   GetSampleCount() const        { return m_settings.sampleCount; }
    float GetBlurRadius() const         { return m_settings.blurRadius; }

    // Error reporting
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool m_bInitialized;
    std::string m_lastError;
    SSAOSettings m_settings;

    // Screen dimensions (updated in BeginGBufferCapture)
    int m_screenWidth;
    int m_screenHeight;

    // ---- Shader Programs ----
    GLuint m_gBufferProgram;        // G-Buffer capture shader
    GLuint m_ssaoComputeProgram;    // SSAO computation shader
    GLuint m_blurProgram;           // Bilateral blur shader
    GLuint m_compositeProgram;      // Final composite shader

    // ---- G-Buffer FBO ----
    GLuint m_gBufferFBO;            // Framebuffer for G-Buffer
    GLuint m_gBufferColorTex;       // Color texture: [pos.xyz] + [depth]
    GLuint m_gBufferDepthRBO;       // Depth renderbuffer

    // ---- SSAO Intermediate Buffers ----
    GLuint m_ssaoFBO[2];            // Ping-pong FBO for SSAO + blur
    GLuint m_ssaoTex[2];            // Ping-pong textures for SSAO result
    GLuint m_aoResultFBO;           // Final AO result texture (post-blur)
    GLuint m_aoResultTex;           // Final AO result

    // ---- Noise Texture ----
    GLuint m_noiseTexture;          // Random vector texture for sample randomization
    static const int NOISE_SIZE = 4; // 4x4 noise texture (will tile on screen)

    // ---- Helper Methods ----
    bool CompileShaders();
    bool CreateFramebuffers();
    bool CreateNoiseTexture();
    void RenderFullscreenQuad();
    void LogError(const std::string& msg);
    void Cleanup();

    // Shader uniform setup helpers
    void SetupGBufferUniforms();
    void SetupSSAOComputeUniforms();
    void SetupBlurUniforms();
    void SetupCompositeUniforms(GLuint sceneColorTex);
};

// Global SSAO manager instance (created/destroyed in Winmain)
extern SSAOManager* g_SSAOManager;

}  // namespace SEASON3B
