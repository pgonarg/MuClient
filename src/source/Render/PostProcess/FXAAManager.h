#pragma once

/// Fast Approximate Anti-Aliasing (FXAA) post-process effect
///
/// Smooths jagged edges in the final rendered image by detecting edge-aligned
/// pixels and blending them. Works on any pre-rendered texture without scene
/// modification.
///
/// Typical usage:
///   1. Bloom::BeginCapture()     // scene renders to FBO
///   2. RenderGameWorld()         // 3D rendering
///   3. Bloom::ApplyBloom()       // bloom post-process
///   4. SSAO::Apply()             // ambient occlusion
///   5. ToneMapping::Apply()      // tone mapping
///   6. FXAAManager::Apply()      // FXAA as final post-process
///   7. RenderUI()                // 2D UI unaffected
///
/// FXAAManager manages the shader program, FBO for processing, and applies
/// the FXAA pass to the input texture. Enable/disable for comparison testing.
class FXAAManager
{
public:
    /// One-time setup: compile shader, verify extension support.
    /// Returns true on success. Must be called after glewInit().
    static bool Initialize();

    /// Release all GPU resources.
    static void Shutdown();

    /// Apply FXAA post-processing to the current framebuffer.
    /// Reads the current backbuffer, applies FXAA, and renders result to screen.
    /// inputTex: optional pre-captured texture; if 0, reads from current framebuffer
    static void Apply(unsigned int inputTex = 0);

    // ── Tuneable parameters ────────────────────────────────────────────────────

    /// Enable/disable FXAA at runtime (for A/B testing)
    static bool  IsEnabled()           { return s_enabled; }
    static void  SetEnabled(bool v)    { s_enabled = v; }

    /// Blend strength: 0.0 = no blending, 1.0 = full FXAA
    /// Higher values smooth more aggressively (default: 1.0)
    static float GetBlendStrength()       { return s_blendStrength; }
    static void  SetBlendStrength(float s) { s_blendStrength = (s < 0.0f) ? 0.0f : (s > 1.0f) ? 1.0f : s; }

    // ── Status checks ──────────────────────────────────────────────────────────
    /// Check if initialization was successful
    static bool IsInitialized()        { return s_initialized; }

    /// Get last error message
    static const char* GetLastError()  { return s_lastError; }

private:
    static bool  CreateFBO(int width, int height);
    static void  DestroyFBO();
    static bool  CompileShaders();
    static void  DeleteShaders();
    static unsigned int CompileShader(unsigned int type, const char* src);
    static unsigned int LinkProgram(unsigned int vert, unsigned int frag);
    static void  RenderFullscreenQuad();

    // ── State ──────────────────────────────────────────────────────────────────
    static bool  s_initialized;
    static bool  s_enabled;
    static float s_blendStrength;
    static const char* s_lastError;

    // Processing FBO (input texture, output color)
    static unsigned int s_fxaaFbo;
    static unsigned int s_inputTex;
    static unsigned int s_outputTex;
    static int   s_fboWidth;
    static int   s_fboHeight;

    // GLSL program
    static unsigned int s_fxaaProgram;
    static int   s_uTextureLocation;
    static int   s_uRcpFrameLocation;
};
